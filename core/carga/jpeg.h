#ifndef ZB2_CORE_CARGA_JPEG_H
#define ZB2_CORE_CARGA_JPEG_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace zb2 {

// JPEG descodificado para RGB565, na ordem das linhas do ficheiro. JPEG nao tem
// alfa; quem o entrega como IImageDecoder deve anunciar AEE_RO_COPY.
struct ImagemJpeg {
  std::uint32_t largura = 0;
  std::uint32_t altura = 0;
  std::vector<std::uint16_t> pixels;

  static constexpr std::uint16_t Rgb565(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return static_cast<std::uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
  }
};

// Descodifica um JPEG completo iniciado em SOI. A rotina nunca le alem de
// `tamanho`, limita a area antes de alocar e devolve motivo para recusa.
bool DescodificarJpeg(const std::uint8_t* dados, std::size_t tamanho, ImagemJpeg* saida,
                      std::string* motivo, std::uint32_t teto_de_pixels = 0u);

}  // namespace zb2
#endif  // ZB2_CORE_CARGA_JPEG_H
