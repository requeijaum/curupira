#include "core/carga/bmp.h"

#include <limits>
#include <new>

namespace zb2 {
namespace {

constexpr std::uint32_t kTetoDePixelsPorOmissao = 1024u * 1024u;

std::uint16_t LerLE16(const std::uint8_t* p) {
  return static_cast<std::uint16_t>(p[0]) |
         static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[1]) << 8);
}

std::uint32_t LerLE32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) |
         (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) |
         (static_cast<std::uint32_t>(p[3]) << 24);
}

}  // namespace

bool DescodificarBmp(const std::uint8_t* dados, std::size_t tamanho, ImagemBmp* saida,
                     std::string* motivo, std::uint32_t teto_de_pixels) {
  if (motivo != nullptr) motivo->clear();
  if (saida != nullptr) *saida = ImagemBmp{};
  auto recusar = [&](const std::string& porque) {
    if (motivo != nullptr) *motivo = porque;
    return false;
  };
  if (saida == nullptr) return recusar("saida BMP nula");
  if (dados == nullptr || tamanho < 14u) {
    return recusar("stream BMP menor que o cabecalho de ficheiro de 14 bytes");
  }
  if (dados[0] != 'B' || dados[1] != 'M') return recusar("nao comeca por BMP BM");
  if (tamanho < 14u + 40u) return recusar("stream BMP menor que BITMAPINFOHEADER de 40 bytes");

  const std::uint32_t tamanho_declarado = LerLE32(dados + 2);
  const std::uint32_t offset_pixels = LerLE32(dados + 10);
  const std::uint32_t tamanho_dib = LerLE32(dados + 14);
  if (tamanho_dib < 40u) return recusar("DIB BMP menor que BITMAPINFOHEADER de 40 bytes");
  const std::uint64_t fim_dib = 14ull + tamanho_dib;
  if (fim_dib > tamanho) return recusar("DIB BMP passa o fim do stream");
  if (offset_pixels < fim_dib || offset_pixels > tamanho) {
    return recusar("offset dos pixels BMP fica antes do fim do DIB ou fora do stream");
  }

  const std::int64_t largura_assinada = static_cast<std::int32_t>(LerLE32(dados + 18));
  const std::int64_t altura_assinada = static_cast<std::int32_t>(LerLE32(dados + 22));
  if (largura_assinada <= 0 || altura_assinada == 0) {
    return recusar("dimensoes BMP invalidas (largura tem de ser positiva e altura nao nula)");
  }
  const std::uint64_t largura = static_cast<std::uint64_t>(largura_assinada);
  const std::uint64_t altura = altura_assinada < 0 ? static_cast<std::uint64_t>(-altura_assinada)
                                                   : static_cast<std::uint64_t>(altura_assinada);
  const std::uint16_t planos = LerLE16(dados + 26);
  const std::uint16_t bpp = LerLE16(dados + 28);
  const std::uint32_t compressao = LerLE32(dados + 30);
  if (planos != 1u) return recusar("BMP tem numero de planos diferente de 1");
  if (bpp != 24u && bpp != 32u) {
    return recusar("BMP usa bpp nao suportado (so BI_RGB 24/32 bpp)");
  }
  if (compressao != 0u) return recusar("BMP comprimido ou com mascaras nao suportado (so BI_RGB)");

  const std::uint64_t pixels = largura * altura;
  const std::uint64_t teto = teto_de_pixels != 0u ? teto_de_pixels : kTetoDePixelsPorOmissao;
  if (largura > std::numeric_limits<std::uint32_t>::max() ||
      altura > std::numeric_limits<std::uint32_t>::max() || pixels > teto) {
    return recusar("dimensoes BMP excedem o teto de pixels");
  }
  const std::uint64_t bytes_por_pixel = bpp / 8u;
  const std::uint64_t bytes_linha_sem_pad = largura * bytes_por_pixel;
  const std::uint64_t stride = (bytes_linha_sem_pad + 3u) & ~3ull;
  const std::uint64_t bytes_pixels = stride * altura;
  const std::uint64_t fim_pixels = static_cast<std::uint64_t>(offset_pixels) + bytes_pixels;
  if (fim_pixels > tamanho) return recusar("pixels BMP truncados para o stride declarado");
  if (tamanho_declarado != 0u &&
      (tamanho_declarado < fim_pixels || tamanho_declarado > tamanho)) {
    return recusar("tamanho declarado no cabecalho BMP nao contem os pixels no stream");
  }

  ImagemBmp imagem;
  imagem.largura = static_cast<std::uint32_t>(largura);
  imagem.altura = static_cast<std::uint32_t>(altura);
  try {
    imagem.pixels.resize(static_cast<std::size_t>(pixels));
  } catch (const std::bad_alloc&) {
    return recusar("sem memoria para pixels BMP");
  }
  const bool top_down = altura_assinada < 0;
  for (std::uint64_t y = 0; y < altura; ++y) {
    const std::uint64_t linha_no_ficheiro = top_down ? y : altura - 1u - y;
    const std::uint8_t* linha = dados + offset_pixels + linha_no_ficheiro * stride;
    for (std::uint64_t x = 0; x < largura; ++x) {
      const std::uint8_t* bgr = linha + x * bytes_por_pixel;
      imagem.pixels[static_cast<std::size_t>(y * largura + x)] =
          ImagemBmp::Rgb565(bgr[2], bgr[1], bgr[0]);
    }
  }
  *saida = std::move(imagem);
  return true;
}

}  // namespace zb2
