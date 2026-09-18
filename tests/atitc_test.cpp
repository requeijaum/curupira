#include <gtest/gtest.h>

#include <array>
#include <cstdint>

#include "core/video/atitc.h"

namespace zb2::video {
namespace {

TEST(Atitc, DecodificaOsQuatroCodigosRgbEmUmBloco) {
  // color0 e RGB555 vermelho, color1 e RGB565 verde. Os seletores 0..3
  // ocupam dois bits por texel, LSB primeiro.
  const std::array<std::uint8_t, 8> bloco = {0x00, 0x7c, 0xe0, 0x07,
                                               0xe4, 0xe4, 0xe4, 0xe4};
  const auto imagem = DescodificarAtitc(bloco.data(), bloco.size(), 4, 4, FormatoAtitc::Rgb);
  ASSERT_TRUE(imagem.has_value());
  ASSERT_EQ(imagem->size(), 16u);
  EXPECT_EQ((*imagem)[0].r, 255); EXPECT_EQ((*imagem)[0].g, 0);   EXPECT_EQ((*imagem)[0].a, 255);
  EXPECT_EQ((*imagem)[1].r, 170); EXPECT_EQ((*imagem)[1].g, 85);  EXPECT_EQ((*imagem)[1].a, 255);
  EXPECT_EQ((*imagem)[2].r, 95);  EXPECT_EQ((*imagem)[2].g, 159); EXPECT_EQ((*imagem)[2].a, 255);
  EXPECT_EQ((*imagem)[3].r, 0);   EXPECT_EQ((*imagem)[3].g, 255); EXPECT_EQ((*imagem)[3].a, 255);
}

TEST(Atitc, RgbaExplicitoLeNibblesERecortaOBloco) {
  // Alfa: nibble baixo do primeiro byte e texel 0; alto e texel 1.
  const std::array<std::uint8_t, 16> bloco = {0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
                                                0x00, 0x7c, 0xe0, 0x07, 0xe4, 0xe4, 0xe4, 0xe4};
  const auto imagem = DescodificarAtitc(bloco.data(), bloco.size(), 2, 3, FormatoAtitc::RgbaExplicito);
  ASSERT_TRUE(imagem.has_value());
  ASSERT_EQ(imagem->size(), 6u);
  EXPECT_EQ((*imagem)[0].a, 0);
  EXPECT_EQ((*imagem)[1].a, 17);
  // Recorte preserva a grade 4x4: linha 1 usa texels 4/5; linha 2, 8/9.
  EXPECT_EQ((*imagem)[2].a, 68);
  EXPECT_EQ((*imagem)[5].a, 153);
}

TEST(Atitc, RecusaBlocoTruncado) {
  const std::array<std::uint8_t, 8> bloco{};
  EXPECT_FALSE(DescodificarAtitc(bloco.data(), bloco.size(), 8, 4, FormatoAtitc::Rgb));
  EXPECT_FALSE(DescodificarAtitc(bloco.data(), bloco.size(), 4, 4, FormatoAtitc::RgbaExplicito));
}

}  // namespace
}  // namespace zb2::video
