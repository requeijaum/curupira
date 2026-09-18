#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/carga/bmp.h"

namespace zb2 {
namespace {

void Le16(std::vector<std::uint8_t>* v, std::uint16_t n) {
  v->push_back(static_cast<std::uint8_t>(n));
  v->push_back(static_cast<std::uint8_t>(n >> 8));
}
void Le32(std::vector<std::uint8_t>* v, std::uint32_t n) {
  v->push_back(static_cast<std::uint8_t>(n));
  v->push_back(static_cast<std::uint8_t>(n >> 8));
  v->push_back(static_cast<std::uint8_t>(n >> 16));
  v->push_back(static_cast<std::uint8_t>(n >> 24));
}

std::vector<std::uint8_t> Bmp24DoTeste() {
  std::vector<std::uint8_t> v;
  v.push_back('B'); v.push_back('M'); Le32(&v, 70); Le32(&v, 0); Le32(&v, 54);
  Le32(&v, 40); Le32(&v, 2); Le32(&v, 2); Le16(&v, 1); Le16(&v, 24);
  Le32(&v, 0); Le32(&v, 16); Le32(&v, 0); Le32(&v, 0); Le32(&v, 0); Le32(&v, 0);
  // BMP positivo e bottom-up. Primeira linha no ficheiro: azul, preto, padding.
  v.insert(v.end(), {255, 0, 0, 0, 0, 0, 0, 0});
  // Segunda: vermelho, verde, padding. Esta e a primeira linha visual.
  v.insert(v.end(), {0, 0, 255, 0, 255, 0, 0, 0});
  return v;
}

TEST(Bmp, Descodifica24BppBiRgbComStrideEOrdemBottomUp) {
  const std::vector<std::uint8_t> dados = Bmp24DoTeste();
  ImagemBmp imagem;
  std::string motivo;
  ASSERT_TRUE(DescodificarBmp(dados.data(), dados.size(), &imagem, &motivo)) << motivo;
  EXPECT_EQ(imagem.largura, 2u);
  EXPECT_EQ(imagem.altura, 2u);
  ASSERT_EQ(imagem.pixels.size(), 4u);
  EXPECT_EQ(imagem.pixels[0], ImagemBmp::Rgb565(255, 0, 0));
  EXPECT_EQ(imagem.pixels[1], ImagemBmp::Rgb565(0, 255, 0));
  EXPECT_EQ(imagem.pixels[2], ImagemBmp::Rgb565(0, 0, 255));
  EXPECT_EQ(imagem.pixels[3], ImagemBmp::Rgb565(0, 0, 0));
}

TEST(Bmp, Descodifica32BppBiRgbTopDown) {
  std::vector<std::uint8_t> v;
  v.push_back('B'); v.push_back('M'); Le32(&v, 70); Le32(&v, 0); Le32(&v, 54);
  Le32(&v, 40); Le32(&v, 2); Le32(&v, 0xfffffffeu); Le16(&v, 1); Le16(&v, 32);
  Le32(&v, 0); Le32(&v, 16); Le32(&v, 0); Le32(&v, 0); Le32(&v, 0); Le32(&v, 0);
  // top-down: amarelo, magenta; quarto byte reservado em BI_RGB.
  v.insert(v.end(), {0, 255, 255, 0x33, 255, 0, 255, 0x77,
                     255, 255, 255, 0x00, 0, 0, 0, 0xff});
  ImagemBmp imagem;
  std::string motivo;
  ASSERT_TRUE(DescodificarBmp(v.data(), v.size(), &imagem, &motivo)) << motivo;
  EXPECT_EQ(imagem.pixels[0], ImagemBmp::Rgb565(255, 255, 0));
  EXPECT_EQ(imagem.pixels[1], ImagemBmp::Rgb565(255, 0, 255));
  EXPECT_EQ(imagem.pixels[2], ImagemBmp::Rgb565(255, 255, 255));
  EXPECT_EQ(imagem.pixels[3], ImagemBmp::Rgb565(0, 0, 0));
}

TEST(Bmp, RecusaCabecalhosOffsetsFormatosEBytesDePixelInvalidos) {
  const std::vector<std::uint8_t> valido = Bmp24DoTeste();
  auto recusa = [&](std::vector<std::uint8_t> v, const char* contem) {
    ImagemBmp imagem;
    std::string motivo;
    EXPECT_FALSE(DescodificarBmp(v.data(), v.size(), &imagem, &motivo));
    EXPECT_NE(motivo.find(contem), std::string::npos) << motivo;
    EXPECT_TRUE(imagem.pixels.empty());
  };
  auto ruim_magic = valido; ruim_magic[0] = 'Z'; recusa(ruim_magic, "BM");
  auto dib_curto = valido; dib_curto[14] = 12; recusa(dib_curto, "DIB");
  auto offset_no_dib = valido; offset_no_dib[10] = 20; recusa(offset_no_dib, "offset");
  auto bpp = valido; bpp[28] = 8; recusa(bpp, "bpp");
  auto rle = valido; rle[30] = 1; recusa(rle, "comprimido");
  auto truncado = valido; truncado.pop_back(); recusa(truncado, "truncados");
  auto tamanho = valido; tamanho[2] = 55; recusa(tamanho, "tamanho declarado");
}

}  // namespace
}  // namespace zb2
