#include "core/carga/jpeg.h"

#include <csetjmp>
#include <cstdio>
#include <limits>
#include <utility>

extern "C" {
#include <jpeglib.h>
}

namespace zb2 {
namespace {

constexpr std::uint32_t kTetoPadraoDePixels = 1024u * 1024u;

struct ErroDoJpeg {
  jpeg_error_mgr base;
  std::jmp_buf salto;
  char texto[JMSG_LENGTH_MAX] = {};
};

void ErroFatalJpeg(j_common_ptr cinfo) {
  auto* erro = reinterpret_cast<ErroDoJpeg*>(cinfo->err);
  (*cinfo->err->format_message)(cinfo, erro->texto);
  std::longjmp(erro->salto, 1);
}

// libjpeg escreve avisos (por exemplo, EOF prematuro) em stderr por omissao.
// O contrato do Curupira devolve a recusa no `motivo`; testes e frontend nao
// devem ganhar texto solto de uma biblioteca. Erros fatais continuam acima.
void AvisoJpeg(j_common_ptr) {}

void Motivo(std::string* destino, const char* texto) {
  if (destino != nullptr) *destino = texto;
}

}  // namespace

bool DescodificarJpeg(const std::uint8_t* dados, std::size_t tamanho, ImagemJpeg* saida,
                      std::string* motivo, std::uint32_t teto_de_pixels) {
  if (saida == nullptr) {
    Motivo(motivo, "saida JPEG nula");
    return false;
  }
  *saida = ImagemJpeg{};
  if (dados == nullptr || tamanho < 3u || dados[0] != 0xffu || dados[1] != 0xd8u ||
      dados[2] != 0xffu) {
    Motivo(motivo, "nao comeca por JPEG SOI (ff d8 ff)");
    return false;
  }
  if (tamanho > static_cast<std::size_t>(std::numeric_limits<unsigned long>::max())) {
    Motivo(motivo, "fluxo JPEG maior que a API libjpeg aceita");
    return false;
  }

  jpeg_decompress_struct jpeg = {};
  ErroDoJpeg erro = {};
  jpeg.err = jpeg_std_error(&erro.base);
  erro.base.error_exit = ErroFatalJpeg;
  erro.base.output_message = AvisoJpeg;
  if (setjmp(erro.salto) != 0) {
    jpeg_destroy_decompress(&jpeg);
    Motivo(motivo, erro.texto[0] == 0 ? "libjpeg recusou o fluxo" : erro.texto);
    return false;
  }

  jpeg_create_decompress(&jpeg);
  jpeg_mem_src(&jpeg, const_cast<unsigned char*>(reinterpret_cast<const unsigned char*>(dados)),
               static_cast<unsigned long>(tamanho));
  if (jpeg_read_header(&jpeg, TRUE) != JPEG_HEADER_OK) {
    jpeg_destroy_decompress(&jpeg);
    Motivo(motivo, "cabecalho JPEG incompleto");
    return false;
  }
  jpeg.out_color_space = JCS_RGB;
  jpeg_start_decompress(&jpeg);

  const std::uint64_t pixels = static_cast<std::uint64_t>(jpeg.output_width) * jpeg.output_height;
  const std::uint32_t teto = teto_de_pixels == 0 ? kTetoPadraoDePixels : teto_de_pixels;
  if (jpeg.output_width == 0 || jpeg.output_height == 0 || pixels > teto ||
      pixels > std::numeric_limits<std::size_t>::max() / sizeof(std::uint16_t)) {
    jpeg_destroy_decompress(&jpeg);
    Motivo(motivo, "dimensoes JPEG excedem o teto de pixels");
    return false;
  }
  if (jpeg.output_components != 3) {
    jpeg_destroy_decompress(&jpeg);
    Motivo(motivo, "libjpeg nao entregou RGB de tres componentes");
    return false;
  }

  ImagemJpeg imagem;
  imagem.largura = jpeg.output_width;
  imagem.altura = jpeg.output_height;
  imagem.pixels.resize(static_cast<std::size_t>(pixels));
  JSAMPARRAY linha = (*jpeg.mem->alloc_sarray)(reinterpret_cast<j_common_ptr>(&jpeg), JPOOL_IMAGE,
                                                jpeg.output_width * jpeg.output_components, 1);
  std::size_t y = 0;
  while (jpeg.output_scanline < jpeg.output_height) {
    if (jpeg_read_scanlines(&jpeg, linha, 1) != 1) {
      jpeg_destroy_decompress(&jpeg);
      Motivo(motivo, "libjpeg nao devolveu a linha pedida");
      return false;
    }
    for (std::size_t x = 0; x < imagem.largura; ++x) {
      const JSAMPLE* rgb = linha[0] + x * 3u;
      imagem.pixels[y * imagem.largura + x] =
          ImagemJpeg::Rgb565(rgb[0], rgb[1], rgb[2]);
    }
    ++y;
  }
  jpeg_finish_decompress(&jpeg);
  jpeg_destroy_decompress(&jpeg);
  *saida = std::move(imagem);
  if (motivo != nullptr) motivo->clear();
  return true;
}

}  // namespace zb2
