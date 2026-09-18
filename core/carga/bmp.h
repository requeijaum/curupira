#ifndef ZB2_CORE_CARGA_BMP_H
#define ZB2_CORE_CARGA_BMP_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace zb2 {

// BMP descodificado em RGB565, na ordem visual: primeira linha no vector e a
// primeira linha da imagem, quer o ficheiro BMP a guarde de baixo para cima ou
// em top-down. BMP BI_RGB nao leva alfa utilizavel neste contrato.
struct ImagemBmp {
  std::uint32_t largura = 0;
  std::uint32_t altura = 0;
  std::vector<std::uint16_t> pixels;

  static constexpr std::uint16_t Rgb565(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return static_cast<std::uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
  }
};

// Descodifica um BMP completo com BITMAPINFOHEADER (DIB >= 40), BI_RGB, 24 ou
// 32 bpp. Confere todos os limites antes de alocar ou ler pixels. Formatos que
// exigem paleta, mascaras ou descompressao devolvem false com motivo explicito.
bool DescodificarBmp(const std::uint8_t* dados, std::size_t tamanho, ImagemBmp* saida,
                     std::string* motivo, std::uint32_t teto_de_pixels = 0u);

}  // namespace zb2
#endif  // ZB2_CORE_CARGA_BMP_H
