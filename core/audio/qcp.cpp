#include "core/audio/qcp.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/samplefmt.h>
}

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <limits>

namespace zb2::audio {
namespace {

constexpr std::size_t kAmostrasMaximas = 32u * 1024u * 1024u;

struct SilenciarAvisosDoFfmpeg {
  SilenciarAvisosDoFfmpeg() : nivel_anterior(av_log_get_level()) { av_log_set_level(AV_LOG_ERROR); }
  ~SilenciarAvisosDoFfmpeg() { av_log_set_level(nivel_anterior); }
  int nivel_anterior;
};

struct Entrada {
  const std::uint8_t* dados;
  std::size_t tamanho;
  std::size_t posicao = 0;
};

int LerEntrada(void* opaco, std::uint8_t* destino, int pedido) {
  auto* entrada = static_cast<Entrada*>(opaco);
  if (pedido <= 0 || entrada->posicao >= entrada->tamanho) return AVERROR_EOF;
  const std::size_t n = std::min<std::size_t>(static_cast<std::size_t>(pedido),
                                               entrada->tamanho - entrada->posicao);
  std::memcpy(destino, entrada->dados + entrada->posicao, n);
  entrada->posicao += n;
  return static_cast<int>(n);
}

std::int64_t ProcurarEntrada(void* opaco, std::int64_t deslocamento, int origem) {
  auto* entrada = static_cast<Entrada*>(opaco);
  if (origem == AVSEEK_SIZE) return static_cast<std::int64_t>(entrada->tamanho);
  const int base = origem & ~AVSEEK_FORCE;
  std::int64_t posicao = 0;
  if (base == SEEK_SET) posicao = deslocamento;
  else if (base == SEEK_CUR) posicao = static_cast<std::int64_t>(entrada->posicao) + deslocamento;
  else if (base == SEEK_END) posicao = static_cast<std::int64_t>(entrada->tamanho) + deslocamento;
  else return AVERROR(EINVAL);
  if (posicao < 0 || posicao > static_cast<std::int64_t>(entrada->tamanho)) return AVERROR(EINVAL);
  entrada->posicao = static_cast<std::size_t>(posicao);
  return posicao;
}

std::string ErroAv(int erro) {
  char texto[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(erro, texto, sizeof(texto));
  return texto;
}

bool CabecalhoQcpValido(const std::vector<std::uint8_t>& bytes, std::string* motivo) {
  if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
      std::memcmp(bytes.data() + 8, "QLCM", 4) != 0) {
    *motivo = "nao e RIFF/QLCM (QCP)";
    return false;
  }
  const std::uint32_t declarado = std::uint32_t(bytes[4]) | (std::uint32_t(bytes[5]) << 8) |
                                  (std::uint32_t(bytes[6]) << 16) | (std::uint32_t(bytes[7]) << 24);
  if (declarado < 4 || std::uint64_t(declarado) + 8 > bytes.size()) {
    *motivo = "tamanho RIFF declarado invalido";
    return false;
  }
  const std::size_t fim = static_cast<std::size_t>(declarado) + 8;
  bool fmt = false;
  bool data = false;
  std::size_t p = 12;
  for (; p + 8 <= fim;) {
    const std::uint32_t n = std::uint32_t(bytes[p + 4]) | (std::uint32_t(bytes[p + 5]) << 8) |
                            (std::uint32_t(bytes[p + 6]) << 16) | (std::uint32_t(bytes[p + 7]) << 24);
    const std::size_t corpo = p + 8;
    if (n > fim - corpo) {
      *motivo = "chunk QCP ultrapassa o RIFF declarado";
      return false;
    }
    if (std::memcmp(bytes.data() + p, "fmt ", 4) == 0) fmt = n >= 18;
    if (std::memcmp(bytes.data() + p, "data", 4) == 0) data = n != 0;
    p = corpo + n + (n & 1u);
    if (p > fim) {
      *motivo = "padding de chunk QCP ultrapassa o RIFF declarado";
      return false;
    }
  }
  if (p != fim) {
    *motivo = "bytes finais QCP sem cabecalho de chunk";
    return false;
  }
  if (!fmt || !data) {
    *motivo = !fmt ? "QCP sem chunk fmt valido" : "QCP sem chunk data";
    return false;
  }
  return true;
}

CodecQcp CodecDoAv(AVCodecID id) {
  if (id == AV_CODEC_ID_QCELP) return CodecQcp::Qcelp;
  if (id == AV_CODEC_ID_EVRC) return CodecQcp::Evrc;
  return CodecQcp::Desconhecido;
}

bool AcrescentarFrame(const AVFrame* quadro, Qcp* resultado, std::string* motivo) {
  const AVSampleFormat formato = static_cast<AVSampleFormat>(quadro->format);
  if (formato != AV_SAMPLE_FMT_S16 && formato != AV_SAMPLE_FMT_S16P &&
      formato != AV_SAMPLE_FMT_FLT && formato != AV_SAMPLE_FMT_FLTP) {
    *motivo = "formato PCM do FFmpeg nao suportado: " +
              std::string(av_get_sample_fmt_name(formato) != nullptr ? av_get_sample_fmt_name(formato) : "desconhecido");
    return false;
  }
  const int canais = quadro->ch_layout.nb_channels;
  if (canais <= 0 || canais != resultado->canais || quadro->nb_samples <= 0) {
    *motivo = "metadados PCM invalidos do FFmpeg";
    return false;
  }
  const std::size_t n = static_cast<std::size_t>(quadro->nb_samples) * canais;
  if (n > kAmostrasMaximas - resultado->amostras.size()) {
    *motivo = "PCM QCP excede o limite de 32 Mi amostras";
    return false;
  }
  const bool planar = av_sample_fmt_is_planar(formato) != 0;
  const bool flutuante = formato == AV_SAMPLE_FMT_FLT || formato == AV_SAMPLE_FMT_FLTP;
  resultado->amostras.reserve(resultado->amostras.size() + n);
  for (int amostra = 0; amostra < quadro->nb_samples; ++amostra) {
    for (int canal = 0; canal < canais; ++canal) {
      const std::uint8_t* origem = planar
          ? quadro->extended_data[canal] + static_cast<std::size_t>(amostra) * (flutuante ? 4 : 2)
          : quadro->extended_data[0] + static_cast<std::size_t>(amostra * canais + canal) *
                                         (flutuante ? 4 : 2);
      if (flutuante) {
        float valor;
        std::memcpy(&valor, origem, sizeof(valor));
        if (!std::isfinite(valor)) {
          *motivo = "PCM flutuante QCP nao finito";
          return false;
        }
        valor = std::clamp(valor, -1.0f, 1.0f);
        resultado->amostras.push_back(static_cast<std::int16_t>(std::lrintf(valor * 32767.0f)));
      } else {
        std::int16_t valor;
        std::memcpy(&valor, origem, sizeof(valor));
        resultado->amostras.push_back(valor);
      }
    }
  }
  return true;
}

}  // namespace

const char* NomeDoCodecQcp(CodecQcp codec) {
  switch (codec) {
    case CodecQcp::Qcelp: return "QCELP";
    case CodecQcp::Evrc: return "EVRC";
    default: return "desconhecido";
  }
}

Qcp DescodificarQcp(const std::vector<std::uint8_t>& bytes) {
  Qcp resultado;
  if (!CabecalhoQcpValido(bytes, &resultado.motivo)) return resultado;
  // O aviso de bitrate do QCELP e detalhe do demuxer, nao diagnostico do guest.
  // Erros continuam convertidos em `motivo`; nunca vazam para stderr.
  SilenciarAvisosDoFfmpeg silenciar_ffmpeg;

  Entrada entrada{bytes.data(), bytes.size()};
  constexpr int kTamanhoIo = 4096;
  std::uint8_t* buffer_io = static_cast<std::uint8_t*>(av_malloc(kTamanhoIo));
  if (buffer_io == nullptr) {
    resultado.motivo = "sem memoria para IO QCP";
    return resultado;
  }
  AVIOContext* io = avio_alloc_context(buffer_io, kTamanhoIo, 0, &entrada, LerEntrada, nullptr,
                                       ProcurarEntrada);
  if (io == nullptr) {
    av_free(buffer_io);
    resultado.motivo = "sem memoria para contexto IO QCP";
    return resultado;
  }
  AVFormatContext* formato = avformat_alloc_context();
  if (formato == nullptr) {
    avio_context_free(&io);
    resultado.motivo = "sem memoria para contexto QCP";
    return resultado;
  }
  formato->pb = io;
  formato->flags |= AVFMT_FLAG_CUSTOM_IO;
  int erro = avformat_open_input(&formato, nullptr, nullptr, nullptr);
  if (erro < 0) {
    resultado.motivo = "FFmpeg nao abriu QCP: " + ErroAv(erro);
    avformat_free_context(formato);
    avio_context_free(&io);
    return resultado;
  }
  const int indice = av_find_best_stream(formato, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
  if (indice < 0) {
    resultado.motivo = "QCP sem stream de audio";
    avformat_close_input(&formato);
    avio_context_free(&io);
    return resultado;
  }
  AVStream* stream = formato->streams[indice];
  resultado.codec = CodecDoAv(stream->codecpar->codec_id);
  if (resultado.codec == CodecQcp::Desconhecido) {
    resultado.motivo = "codec QCP nao e QCELP nem EVRC";
    avformat_close_input(&formato);
    avio_context_free(&io);
    return resultado;
  }
  const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
  if (codec == nullptr) {
    resultado.motivo = std::string("decoder FFmpeg ausente para ") + NomeDoCodecQcp(resultado.codec);
    avformat_close_input(&formato);
    avio_context_free(&io);
    return resultado;
  }
  AVCodecContext* contexto = avcodec_alloc_context3(codec);
  if (contexto == nullptr) {
    resultado.motivo = "sem memoria para decoder QCP";
    avformat_close_input(&formato);
    avio_context_free(&io);
    return resultado;
  }
  erro = avcodec_parameters_to_context(contexto, stream->codecpar);
  if (erro >= 0) erro = avcodec_open2(contexto, codec, nullptr);
  if (erro < 0) {
    resultado.motivo = "FFmpeg nao abriu decoder QCP: " + ErroAv(erro);
    avcodec_free_context(&contexto);
    avformat_close_input(&formato);
    avio_context_free(&io);
    return resultado;
  }
  const int canais = contexto->ch_layout.nb_channels;
  resultado.taxa = contexto->sample_rate;
  if (resultado.taxa == 0 || canais <= 0 || canais > 2) {
    resultado.motivo = "metadados QCP invalidos";
    avcodec_free_context(&contexto);
    avformat_close_input(&formato);
    avio_context_free(&io);
    return resultado;
  }
  resultado.canais = static_cast<std::uint16_t>(canais);
  AVPacket* pacote = av_packet_alloc();
  AVFrame* quadro = av_frame_alloc();
  if (pacote == nullptr || quadro == nullptr) {
    resultado.motivo = "sem memoria para pacote/frame QCP";
  } else {
    bool falhou = false;
    while ((erro = av_read_frame(formato, pacote)) >= 0) {
      if (pacote->stream_index != indice) {
        av_packet_unref(pacote);
        continue;
      }
      erro = avcodec_send_packet(contexto, pacote);
      av_packet_unref(pacote);
      if (erro < 0) { resultado.motivo = "FFmpeg recusou pacote QCP: " + ErroAv(erro); falhou = true; break; }
      while ((erro = avcodec_receive_frame(contexto, quadro)) >= 0) {
        if (!AcrescentarFrame(quadro, &resultado, &resultado.motivo)) { falhou = true; break; }
        av_frame_unref(quadro);
      }
      if (falhou || (erro != AVERROR(EAGAIN) && erro != AVERROR_EOF)) {
        if (!falhou) resultado.motivo = "FFmpeg falhou ao descodificar QCP: " + ErroAv(erro);
        falhou = true;
        break;
      }
    }
    if (!falhou && erro != AVERROR_EOF) resultado.motivo = "FFmpeg falhou ao ler QCP: " + ErroAv(erro);
    if (!falhou && resultado.motivo.empty()) {
      erro = avcodec_send_packet(contexto, nullptr);
      if (erro < 0 && erro != AVERROR_EOF) resultado.motivo = "FFmpeg nao terminou QCP: " + ErroAv(erro);
      while (resultado.motivo.empty() && (erro = avcodec_receive_frame(contexto, quadro)) >= 0) {
        if (!AcrescentarFrame(quadro, &resultado, &resultado.motivo)) break;
        av_frame_unref(quadro);
      }
      if (resultado.motivo.empty() && erro != AVERROR_EOF && erro != AVERROR(EAGAIN))
        resultado.motivo = "FFmpeg falhou ao terminar QCP: " + ErroAv(erro);
    }
    if (resultado.motivo.empty() && resultado.amostras.empty())
      resultado.motivo = "QCP nao produziu PCM";
  }
  av_frame_free(&quadro);
  av_packet_free(&pacote);
  avcodec_free_context(&contexto);
  avformat_close_input(&formato);
  avio_context_free(&io);
  // Um fluxo com erro depois de alguns pacotes nao devolve PCM parcial como se
  // fosse completo. O chamador recebe somente o motivo explicito.
  if (!resultado.motivo.empty()) {
    resultado.taxa = 0;
    resultado.canais = 0;
    resultado.amostras.clear();
  }
  return resultado;
}

}  // namespace zb2::audio
