#include "core/video/atitc.h"

namespace zb2::video {
namespace {

std::uint16_t Ler16(const std::uint8_t* p) {
  return static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
}
std::uint32_t Ler32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
std::uint8_t Expandir5(std::uint8_t v) { return static_cast<std::uint8_t>((v << 3) | (v >> 2)); }
std::uint8_t Expandir6(std::uint8_t v) { return static_cast<std::uint8_t>((v << 2) | (v >> 4)); }

Rgba Cor0(std::uint16_t valor) {
  return {Expandir5(static_cast<std::uint8_t>((valor >> 10) & 31)),
          Expandir5(static_cast<std::uint8_t>((valor >> 5) & 31)),
          Expandir5(static_cast<std::uint8_t>(valor & 31)), 255};
}
Rgba Cor1(std::uint16_t valor) {
  return {Expandir5(static_cast<std::uint8_t>((valor >> 11) & 31)),
          Expandir6(static_cast<std::uint8_t>((valor >> 5) & 63)),
          Expandir5(static_cast<std::uint8_t>(valor & 31)), 255};
}
std::uint8_t Interpolar(std::uint8_t a, std::uint8_t b, std::uint32_t codigo) {
  switch (codigo) {
    case 0: return a;
    case 1: return static_cast<std::uint8_t>((2 * a + b) / 3);
    case 2: return static_cast<std::uint8_t>((3 * a + 5 * b) / 8);
    default: return b;
  }
}

}  // namespace

std::optional<std::vector<Rgba>> DescodificarAtitc(const std::uint8_t* dados, std::size_t tamanho,
                                                    std::uint32_t largura, std::uint32_t altura,
                                                    FormatoAtitc formato) {
  if (dados == nullptr || largura == 0 || altura == 0) return std::nullopt;
  const std::uint64_t blocos_x = (static_cast<std::uint64_t>(largura) + 3) / 4;
  const std::uint64_t blocos_y = (static_cast<std::uint64_t>(altura) + 3) / 4;
  const std::size_t por_bloco = formato == FormatoAtitc::RgbaExplicito ? 16u : 8u;
  const std::uint64_t necessarios = blocos_x * blocos_y * por_bloco;
  const std::uint64_t texels = static_cast<std::uint64_t>(largura) * altura;
  if (necessarios > tamanho || texels > std::vector<Rgba>().max_size()) return std::nullopt;
  std::vector<Rgba> saida(static_cast<std::size_t>(texels));
  for (std::uint64_t by = 0; by < blocos_y; ++by) {
    for (std::uint64_t bx = 0; bx < blocos_x; ++bx) {
      const std::uint8_t* bloco = dados + (by * blocos_x + bx) * por_bloco;
      const std::uint8_t* cor = bloco + (formato == FormatoAtitc::RgbaExplicito ? 8 : 0);
      const Rgba a = Cor0(Ler16(cor));
      const Rgba b = Cor1(Ler16(cor + 2));
      const std::uint32_t seletores = Ler32(cor + 4);
      for (std::uint32_t k = 0; k < 16; ++k) {
        const std::uint32_t x = static_cast<std::uint32_t>(bx * 4 + k % 4);
        const std::uint32_t y = static_cast<std::uint32_t>(by * 4 + k / 4);
        if (x >= largura || y >= altura) continue;
        const std::uint32_t codigo = (seletores >> (2 * k)) & 3;
        Rgba& pixel = saida[static_cast<std::size_t>(y) * largura + x];
        pixel = {Interpolar(a.r, b.r, codigo), Interpolar(a.g, b.g, codigo),
                 Interpolar(a.b, b.b, codigo),
                 formato == FormatoAtitc::RgbaExplicito
                     ? static_cast<std::uint8_t>(((bloco[k / 2] >> ((k & 1) * 4)) & 15) * 17)
                     : static_cast<std::uint8_t>(255)};
      }
    }
  }
  return saida;
}

}  // namespace zb2::video
