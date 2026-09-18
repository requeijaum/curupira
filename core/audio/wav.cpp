#include "core/audio/wav.h"

#include <algorithm>
#include <cstring>

namespace zb2::audio {
namespace {

std::uint16_t Ler16(const std::uint8_t* p) {
  return static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
}
std::uint32_t Ler32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
constexpr std::int8_t kPassos[16] = {-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8};
constexpr std::int32_t kTabela[89] = {
  7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,
  157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,
  1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,
  10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
struct CanalIma {
  std::int32_t preditor = 0, indice = 0;
  std::int16_t Ler(std::uint8_t nibble) {
    const std::int32_t passo = kTabela[std::clamp(indice, 0, 88)];
    std::int32_t delta = passo >> 3;
    if ((nibble & 7) & 4) delta += passo;
    if ((nibble & 7) & 2) delta += passo >> 1;
    if ((nibble & 7) & 1) delta += passo >> 2;
    preditor += (nibble & 8) ? -delta : delta;
    preditor = std::clamp(preditor, -32768, 32767);
    indice = std::clamp(indice + kPassos[nibble & 15], 0, 88);
    return static_cast<std::int16_t>(preditor);
  }
};

std::vector<std::int16_t> DescodificarIma(const std::uint8_t* dados, std::size_t tamanho,
                                           std::uint16_t canais, std::uint16_t alinhamento) {
  std::vector<std::int16_t> saida;
  if (canais == 0 || alinhamento < 4 * canais) return saida;
  for (std::size_t inicio = 0; inicio < tamanho; inicio += alinhamento) {
    const std::size_t fim = std::min(tamanho, inicio + alinhamento);
    if (fim - inicio < 4 * canais) break;
    std::vector<CanalIma> estado(canais);
    for (std::uint16_t c = 0; c < canais; ++c) {
      const std::uint8_t* cab = dados + inicio + c * 4;
      estado[c].preditor = static_cast<std::int16_t>(Ler16(cab));
      estado[c].indice = cab[2];
      saida.push_back(static_cast<std::int16_t>(estado[c].preditor));
    }
    for (std::size_t grupo = inicio + 4 * canais; grupo < fim; grupo += 4 * canais) {
      std::vector<std::vector<std::int16_t>> por_canal(canais);
      for (std::uint16_t c = 0; c < canais; ++c) {
        const std::size_t lane = grupo + c * 4;
        for (std::size_t b = 0; b < 4 && lane + b < fim; ++b) {
          const std::uint8_t byte = dados[lane + b];
          por_canal[c].push_back(estado[c].Ler(byte & 15));
          por_canal[c].push_back(estado[c].Ler(byte >> 4));
        }
      }
      std::size_t frames = por_canal[0].size();
      for (std::uint16_t c = 1; c < canais; ++c) frames = std::min(frames, por_canal[c].size());
      for (std::size_t f = 0; f < frames; ++f) for (std::uint16_t c = 0; c < canais; ++c) saida.push_back(por_canal[c][f]);
    }
  }
  return saida;
}

}  // namespace

std::optional<Wav> DescodificarWav(const std::vector<std::uint8_t>& bytes) {
  if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
      std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) return std::nullopt;
  // O RIFF pode estar dentro de um resource blob. So os bytes declarados pelo
  // proprio RIFF pertencem a onda; o sufixo e do recipiente, nao outro chunk.
  const std::size_t fim_riff = std::size_t(Ler32(bytes.data() + 4)) + 8;
  if (fim_riff < 12 || fim_riff > bytes.size()) return std::nullopt;
  std::uint16_t formato = 0, canais = 0, alinhamento = 0, bits = 0;
  std::uint32_t taxa = 0;
  const std::uint8_t* dados = nullptr; std::size_t tamanho = 0; bool tem_formato = false;
  for (std::size_t p = 12; p + 8 <= fim_riff;) {
    const std::uint32_t n = Ler32(bytes.data() + p + 4);
    const std::size_t corpo = p + 8;
    if (n > fim_riff - corpo) return std::nullopt;
    if (std::memcmp(bytes.data() + p, "fmt ", 4) == 0 && n >= 16) {
      formato = Ler16(bytes.data() + corpo); canais = Ler16(bytes.data() + corpo + 2);
      taxa = Ler32(bytes.data() + corpo + 4); alinhamento = Ler16(bytes.data() + corpo + 12);
      bits = Ler16(bytes.data() + corpo + 14); tem_formato = true;
    } else if (std::memcmp(bytes.data() + p, "data", 4) == 0) { dados = bytes.data() + corpo; tamanho = n; }
    p = corpo + n + (n & 1u);
  }
  if (!tem_formato || dados == nullptr || canais == 0 || canais > 2 || taxa == 0 || taxa > 384000) return std::nullopt;
  Wav resultado; resultado.taxa = taxa; resultado.canais = canais;
  if (formato == 17) resultado.amostras = DescodificarIma(dados, tamanho, canais, alinhamento);
  else if (formato == 1 && bits == 16) {
    resultado.amostras.resize(tamanho / 2);
    for (std::size_t i = 0; i < resultado.amostras.size(); ++i) resultado.amostras[i] = static_cast<std::int16_t>(Ler16(dados + i * 2));
  } else if (formato == 1 && bits == 8) {
    resultado.amostras.reserve(tamanho);
    for (std::size_t i = 0; i < tamanho; ++i) resultado.amostras.push_back(static_cast<std::int16_t>((int(dados[i]) - 128) * 256));
  } else return std::nullopt;
  if (resultado.amostras.empty()) return std::nullopt;
  return resultado;
}

}  // namespace zb2::audio
