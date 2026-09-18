#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "core/audio/misturador.h"
#include "core/audio/wav.h"
#include "core/audio/qcp.h"
#include "core/audio/duracao.h"

using zb2::audio::kLimitePcm16;
using zb2::audio::kVolumeMaximo;
using zb2::audio::Misturador;

namespace {

// Uma senoide de inteiros pequenos e determinista: nao ha `rand()` do hospede
// em teste nenhum (P4), e um valor declarado e comparavel entre corridas.
std::vector<std::int16_t> Onda(std::size_t quantas, std::int16_t amplitude) {
  std::vector<std::int16_t> v(quantas);
  for (std::size_t k = 0; k < quantas; ++k) {
    v[k] = ((k % 4) < 2) ? amplitude : static_cast<std::int16_t>(-amplitude);
  }
  return v;
}

std::vector<std::uint8_t> QcpDoChessbotsMontado() {
  std::string caminho;
  if (const char* env = std::getenv("ZB2_CHESSBOTS_MOD")) caminho = env;
  if (caminho.empty()) {
    caminho = "/home/rafaelfrequiao/projects/zeebo-lab/games/brew/mod/263019/chessbots.mod";
  }
  std::ifstream arquivo(caminho, std::ios::binary);
  if (!arquivo) return {};
  const std::vector<std::uint8_t> modulo((std::istreambuf_iterator<char>(arquivo)),
                                         std::istreambuf_iterator<char>());
  const std::uint8_t marca[] = {'R','I','F','F', 0,0,0,0, 'Q','L','C','M'};
  for (std::size_t p = 0; p + sizeof(marca) <= modulo.size(); ++p) {
    if (std::memcmp(modulo.data() + p, marca, 4) != 0 ||
        std::memcmp(modulo.data() + p + 8, marca + 8, 4) != 0) continue;
    const std::uint32_t tamanho = std::uint32_t(modulo[p + 4]) |
        (std::uint32_t(modulo[p + 5]) << 8) | (std::uint32_t(modulo[p + 6]) << 16) |
        (std::uint32_t(modulo[p + 7]) << 24);
    if (tamanho >= 4 && std::uint64_t(tamanho) + 8 <= modulo.size() - p) {
      return {modulo.begin() + p, modulo.begin() + p + tamanho + 8};
    }
  }
  return {};
}

TEST(Qcp, ChessbotsMontadoDecodificaQcelpParaPcm) {
  // Le o recurso embutido no .mod montado. Nao ha fixture copiada, download,
  // processo externo, nem ficheiro temporario: o decoder recebe estes bytes.
  const auto bytes = QcpDoChessbotsMontado();
  if (bytes.empty()) GTEST_SKIP() << "chessbots.mod montado nao encontrado; use ZB2_CHESSBOTS_MOD";
  const auto som = zb2::audio::DescodificarQcp(bytes);
  ASSERT_TRUE(som.ok()) << som.motivo;
  EXPECT_EQ(som.codec, zb2::audio::CodecQcp::Qcelp);
  EXPECT_EQ(som.taxa, 8000u);
  EXPECT_EQ(som.canais, 1u);
  ASSERT_FALSE(som.amostras.empty());
  EXPECT_NE(std::count_if(som.amostras.begin(), som.amostras.end(),
                          [](std::int16_t a) { return a != 0; }), 0);
}

TEST(Qcp, CabecalhoMalformadoDaMotivoSemPcm) {
  const std::vector<std::uint8_t> bytes = {'R','I','F','F', 4,0,0,0, 'W','A','V','E'};
  const auto som = zb2::audio::DescodificarQcp(bytes);
  EXPECT_FALSE(som.ok());
  EXPECT_FALSE(som.motivo.empty());
  EXPECT_TRUE(som.amostras.empty());
}

TEST(Wav, IgnoraSufixoDepoisDoTamanhoDeclaradoNoRiff) {
  std::vector<std::uint8_t> wav = {
      'R','I','F','F', 38,0,0,0, 'W','A','V','E',
      'f','m','t',' ', 16,0,0,0, 1,0, 1,0, 0x22,0x56,0,0,
      0x44,0xac,0,0, 2,0,16,0, 'd','a','t','a', 2,0,0,0, 0x34,0x12,
      'J','U','N','K', 0xff,0xff,0xff,0xff};
  const auto som = zb2::audio::DescodificarWav(wav);
  ASSERT_TRUE(som.has_value());
  ASSERT_EQ(som->amostras.size(), 1u);
  EXPECT_EQ(som->amostras[0], 0x1234);
}

TEST(Wav, IMAAdpcmMonoDecodificaPreditorEOrdemDosNibbles) {
  // RIFF/WAVE, fmt IMA-ADPCM mono, bloco de 5 bytes: predictor 0, indice 0,
  // nibbles 7 e 7. A tabela IMA produz 0, 11, 41.
  const std::vector<std::uint8_t> wav = {
      'R','I','F','F', 45,0,0,0, 'W','A','V','E',
      'f','m','t',' ', 20,0,0,0, 17,0, 1,0, 0x22,0x56,0,0,
      0,0,0,0, 5,0, 4,0, 2,0, 3,0,
      'd','a','t','a', 5,0,0,0, 0,0,0,0, 0x77, 0};
  const auto som = zb2::audio::DescodificarWav(wav);
  ASSERT_TRUE(som.has_value());
  EXPECT_EQ(som->taxa, 22050u);
  EXPECT_EQ(som->canais, 1u);
  ASSERT_EQ(som->amostras.size(), 3u);
  EXPECT_EQ(som->amostras[0], 0);
  EXPECT_EQ(som->amostras[1], 11);
  EXPECT_EQ(som->amostras[2], 41);
}

TEST(Wav, IMAAdpcmUsaATabelaPadraoNoPasso50) {
  // Indice inicial 20 corresponde ao passo IMA 50. Nibble 4: 50 + 50/8 = 56.
  const std::vector<std::uint8_t> wav = {
      'R','I','F','F', 45,0,0,0, 'W','A','V','E',
      'f','m','t',' ', 20,0,0,0, 17,0, 1,0, 0x22,0x56,0,0,
      0,0,0,0, 5,0, 4,0, 2,0, 3,0,
      'd','a','t','a', 5,0,0,0, 0,0,20,0, 0x04, 0};
  const auto som = zb2::audio::DescodificarWav(wav);
  ASSERT_TRUE(som.has_value());
  ASSERT_GE(som->amostras.size(), 2u);
  EXPECT_EQ(som->amostras[1], 56);
}

TEST(Wav, IMAAdpcmUsaATabelaPadraoNoPasso6484) {
  // Indice 71 e passo IMA 6484; nibble 4 soma 6484 + floor(6484/8) = 7294.
  const std::vector<std::uint8_t> wav = {
      'R','I','F','F', 45,0,0,0, 'W','A','V','E',
      'f','m','t',' ', 20,0,0,0, 17,0, 1,0, 0x22,0x56,0,0,
      0,0,0,0, 5,0, 4,0, 2,0, 3,0,
      'd','a','t','a', 5,0,0,0, 0,0,71,0, 0x04, 0};
  const auto som = zb2::audio::DescodificarWav(wav);
  ASSERT_TRUE(som.has_value());
  ASSERT_GE(som->amostras.size(), 2u);
  EXPECT_EQ(som->amostras[1], 7294);
}

TEST(Cronometragem, Mp3Layer3ContaFramesPelaMoldura) {
  // MPEG-1 Layer III, 128 kb/s, 44.1 kHz, sem padding: moldura de 417 bytes.
  std::vector<std::uint8_t> mp3(417, 0);
  mp3[0] = 0xff; mp3[1] = 0xfb; mp3[2] = 0x90; mp3[3] = 0;
  const auto t = zb2::audio::CronometrarFluxo(mp3);
  ASSERT_TRUE(t.has_value());
  EXPECT_EQ(t->tipo, zb2::audio::TipoCronometrado::Mp3);
  EXPECT_EQ(t->milissegundos, 27u);  // ceil(1152 / 44100 s)
}

TEST(Cronometragem, Mp3ContaMoldurasCompletasAntesDaCaudaTruncada) {
  std::vector<std::uint8_t> mp3(418, 0);
  mp3[0] = 0xff; mp3[1] = 0xfb; mp3[2] = 0x90;
  const auto t = zb2::audio::CronometrarFluxo(mp3);
  ASSERT_TRUE(t.has_value());
  EXPECT_EQ(t->milissegundos, 27u);
}

TEST(Cronometragem, Mp3SaltaFooterDeId3v24) {
  std::vector<std::uint8_t> mp3(10 + 10 + 417, 0);
  mp3[0] = 'I'; mp3[1] = 'D'; mp3[2] = '3'; mp3[3] = 4; mp3[5] = 0x10;
  mp3[20] = 0xff; mp3[21] = 0xfb; mp3[22] = 0x90;
  const auto t = zb2::audio::CronometrarFluxo(mp3);
  ASSERT_TRUE(t.has_value());
  EXPECT_EQ(t->milissegundos, 27u);
}

TEST(Cronometragem, MidiUsaTempoETicksDoTrack) {
  const std::vector<std::uint8_t> midi = {
      'M','T','h','d', 0,0,0,6, 0,0, 0,1, 1,0xe0,
      'M','T','r','k', 0,0,0,20,
      0, 0xff,0x51,3, 0x07,0xa1,0x20,  // tempo 500000 us/qn
      0, 0x90,60,64, 0x83,0x60, 0x80,60,0, 0,0xff,0x2f,0};
  const auto t = zb2::audio::CronometrarFluxo(midi);
  ASSERT_TRUE(t.has_value());
  EXPECT_EQ(t->tipo, zb2::audio::TipoCronometrado::Midi);
  EXPECT_EQ(t->milissegundos, 500u);
}

TEST(Cronometragem, MidiRespeitaUltimoTempoNoMesmoTick) {
  const std::vector<std::uint8_t> midi = {
      'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,96,
      'M','T','r','k', 0,0,0,18,
      0,0xff,0x51,3, 0x0f,0x42,0x40,  // 1000000 us/qn
      0,0xff,0x51,3, 0x07,0xa1,0x20,  // 500000 us/qn; vence no mesmo tick
      96, 0xff,0x2f,0};
  const auto t = zb2::audio::CronometrarFluxo(midi);
  ASSERT_TRUE(t.has_value());
  EXPECT_EQ(t->milissegundos, 500u);
}

TEST(Cronometragem, MidiSmpteCronometraSemMapaDeTempo) {
  const std::vector<std::uint8_t> midi = {
      'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0xe7,40,  // -25 fps, 40 ticks/frame
      'M','T','r','k', 0,0,0,5, 0x87,0x68, 0xff,0x2f,0};  // tick 1000
  const auto t = zb2::audio::CronometrarFluxo(midi);
  ASSERT_TRUE(t.has_value());
  EXPECT_EQ(t->milissegundos, 1000u);
}

TEST(Cronometragem, MidiComDuracaoForaDeUint32ERecusado) {
  const std::vector<std::uint8_t> midi = {
      'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,1,
      'M','T','r','k', 0,0,0,8, 0xff,0xff,0xff,0x7f, 0xff,0x2f,0};
  EXPECT_FALSE(zb2::audio::CronometrarFluxo(midi).has_value());
}

TEST(Misturador, ContaAsAmostrasEAsNaoNulas) {
  Misturador m;
  const auto onda = Onda(100, 1000);
  m.Misturar(onda.data(), onda.size(), kVolumeMaximo, false);
  m.FecharBloco();
  EXPECT_EQ(m.MedidaAcumulada().amostras_recebidas, 100u);
  EXPECT_EQ(m.MedidaAcumulada().amostras_nao_nulas, 100u);
  EXPECT_EQ(m.MedidaAcumulada().blocos, 1u);
  EXPECT_EQ(m.MedidaAcumulada().pico, 1000);
}

TEST(Misturador, SilencioNaoContaComoAmostraNaoNula) {
  // O criterio da etapa 5 e "o misturador reporta amostras NAO NULAS". Se zeros
  // contassem, o criterio passaria com um buffer todo a zero -- e um numero que
  // passa por engano e pior do que nenhum.
  Misturador m;
  const std::vector<std::int16_t> zeros(64, 0);
  m.Misturar(zeros.data(), zeros.size(), kVolumeMaximo, false);
  m.FecharBloco();
  EXPECT_EQ(m.MedidaAcumulada().amostras_recebidas, 64u);
  EXPECT_EQ(m.MedidaAcumulada().amostras_nao_nulas, 0u);
  EXPECT_EQ(m.MedidaAcumulada().pico, 0);
}

TEST(Misturador, VolumeMetadeBaixaOPicoAMetade) {
  Misturador m;
  const auto onda = Onda(16, 2000);
  m.Misturar(onda.data(), onda.size(), 50, false);
  m.FecharBloco();
  EXPECT_EQ(m.MedidaAcumulada().pico, 1000);
}

TEST(Misturador, MudoZeraOPicoSemDescartarAContagem) {
  // MUTE (MM_PARM_MUTE) e um pedido ACEITO: as amostras continuam a passar e a
  // ser contadas, mas o que sai e zero. As duas contagens dizem coisas
  // diferentes de proposito.
  Misturador m;
  const auto onda = Onda(32, 3000);
  m.Misturar(onda.data(), onda.size(), kVolumeMaximo, true);
  m.FecharBloco();
  EXPECT_EQ(m.MedidaAcumulada().amostras_recebidas, 32u);
  EXPECT_EQ(m.MedidaAcumulada().amostras_nao_nulas, 32u);
  EXPECT_EQ(m.MedidaAcumulada().pico, 0);
}

TEST(Misturador, VolumeAcimaDoMaximoETravadoEContado) {
  // Travar em silencio seria um valor errado sem sintoma; recusar a voz inteira
  // deixaria o jogo mudo. Trava e CONTA.
  Misturador m;
  const auto onda = Onda(8, 1000);
  m.Misturar(onda.data(), onda.size(), 200, false);
  m.FecharBloco();
  EXPECT_EQ(m.MedidaAcumulada().escalas_limitadas, 1u);
  EXPECT_EQ(m.MedidaAcumulada().pico, 1000);  // o mesmo que volume 100
}

TEST(Misturador, DuasVozesSomamNoMesmoBloco) {
  // O pico e do BLOCO, e nao da chamada: e o que torna a soma de vozes visivel.
  Misturador m;
  // Duas vozes que comecam no MESMO instante do bloco: e a posicao que as soma.
  const auto onda = Onda(16, 500);
  m.MisturarNoBloco(onda.data(), onda.size(), kVolumeMaximo, false, 0);
  m.MisturarNoBloco(onda.data(), onda.size(), kVolumeMaximo, false, 0);
  m.FecharBloco();
  EXPECT_EQ(m.MedidaAcumulada().pico, 1000);
  EXPECT_EQ(m.MedidaAcumulada().blocos, 1u);
  EXPECT_EQ(m.MedidaAcumulada().amostras_recebidas, 32u);
}

TEST(Misturador, SomaQuePassaDe16BitsSaturaEConta) {
  Misturador m;
  const std::vector<std::int16_t> alto(8, kLimitePcm16);
  // Oito amostras no maximo, duas vezes, no mesmo instante do bloco: a soma
  // passa o limite e SATURA -- e a saturacao e CONTADA.
  m.MisturarNoBloco(alto.data(), alto.size(), kVolumeMaximo, false, 0);
  m.MisturarNoBloco(alto.data(), alto.size(), kVolumeMaximo, false, 0);
  m.FecharBloco();
  EXPECT_EQ(m.MedidaAcumulada().pico, kLimitePcm16);
  EXPECT_EQ(m.MedidaAcumulada().saturacoes, 8u);
}

TEST(Misturador, FecharBlocoVazioNaoContaBloco) {
  // GUARDA CONTRA UMA CONTAGEM A MAIS: um bloco vazio nao e um bloco. Sem isto,
  // um laco que fecha o bloco a cada passo inflacionava a contagem sem que
  // nenhuma amostra tivesse passado.
  Misturador m;
  m.FecharBloco();
  m.FecharBloco();
  EXPECT_EQ(m.MedidaAcumulada().blocos, 0u);
  EXPECT_EQ(m.MedidaAcumulada().amostras_recebidas, 0u);
}

TEST(Misturador, BlocoGrandeNaoAlocaMaisDoQueOLimite) {
  // Um pedido maior do que o bloco e DIVIDIDO, nao descartado nem alocado.
  Misturador m;
  const auto onda = Onda(20000, 100);
  m.Misturar(onda.data(), onda.size(), kVolumeMaximo, false);
  m.FecharBloco();
  EXPECT_EQ(m.MedidaAcumulada().amostras_recebidas, 20000u);
  EXPECT_GT(m.MedidaAcumulada().blocos, 1u);
}

TEST(Misturador, ReporLimpaAContagem) {
  Misturador m;
  const auto onda = Onda(4, 100);
  m.Misturar(onda.data(), onda.size(), kVolumeMaximo, false);
  m.FecharBloco();
  m.Repor();
  EXPECT_EQ(m.MedidaAcumulada().amostras_recebidas, 0u);
  EXPECT_EQ(m.AmostrasNoBloco(), 0u);
}

}  // namespace
