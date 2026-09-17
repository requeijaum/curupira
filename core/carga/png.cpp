#include "core/carga/png.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "core/carga/inflate.h"

// ---------------------------------------------------------------------------
// AS REFERENCIAS DESTE FICHEIRO (nenhuma delas escrita de memoria):
//
//   - a assinatura de 8 bytes, os nomes dos chunks, a ordem IHDR/PLTE/tRNS/
//     IDAT/IEND, o bit de ANCILLARY (0x20 no primeiro byte do tipo) e os cinco
//     filtros por linha (0..4) sao a especificacao PNG (ISO/IEC 15948, secoes
//     5.2, 5.5, 9):  `89 50 4e 47 0d 0a 1a 0a`, CRC32 do tipo+dados.
//   - o zlib do IDAT e o `Inflar` DESTA PASTA (`core/carga/inflate.h`), que ja
//     existe e ja e provado por `tests/pack_test.cpp` nos tres tipos de bloco.
//     Uma segunda implementacao do RFC1950 aqui seria uma segunda chance de o
//     RFC1950 divergir -- e o PNG nao acrescenta nada ao zlib.
//   - as dimensoes e o color type dos recursos dos quatro titulos: medidos em
//     `tools/medir_bar.py recurso` e escritos no cabecalho `png.h`.
//
// O QUE ESTE FICHEIRO NAO FAZ, de proposito: nao redimensiona, nao converte
// espaco de cor, nao le gAMA/cHRM/sRGB, e nao guarda alfa nenhum. O que sai e o
// que os DIBs desta arvore usam (RGB565) mais a informacao de "tem alfa", que e
// o que o `GetRop` do `IImageDecoder` promete.
// ---------------------------------------------------------------------------

namespace zb2 {
namespace {

constexpr std::uint8_t kAssinatura[8] = {0x89u, 0x50u, 0x4eu, 0x47u,
                                         0x0du, 0x0au, 0x1au, 0x0au};

// O limite de AREA. O maior recurso medido nos quatro titulos tem 943x44 =
// 41 492 pixels; o limite e 1 Mi-pixel (2 MB em RGB565), que e o que a banda de
// pixels desta arvore serve com folga. Um cabecalho mentiroso e recusado ANTES
// de se alocar seja o que for.
constexpr std::uint32_t kTetoDePixelsPorOmissao = 1024u * 1024u;

std::uint32_t LerBE32(const std::uint8_t* p) {
  return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
         (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

}  // namespace

std::uint32_t Crc32DePng(const std::uint8_t* dados, std::size_t n) {
  std::uint32_t crc = 0xFFFFFFFFu;
  for (std::size_t k = 0; k < n; ++k) {
    crc ^= dados[k];
    for (int b = 0; b < 8; ++b) {
      crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
    }
  }
  return crc ^ 0xFFFFFFFFu;
}

namespace {

std::string BytesEmHex(const std::uint8_t* d, std::size_t n) {
  std::string s;
  char par[4];
  for (std::size_t k = 0; k < n; ++k) {
    std::snprintf(par, sizeof(par), "%02x ", d[k]);
    s += par;
  }
  if (!s.empty()) s.pop_back();
  return s;
}

const char* NomeDoColorType(std::uint8_t ct) {
  switch (ct) {
    case 0: return "0 (cinza)";
    case 2: return "2 (RGB)";
    case 3: return "3 (paleta)";
    case 4: return "4 (cinza+alfa)";
    case 6: return "6 (RGBA)";
    default: return "desconhecido";
  }
}

// As AMOSTRAS de um pixel: 1 na escala de cinza, 3 no RGB, 1 no indice de paleta,
// 2 no cinza+alfa e 4 no RGBA (PNG, tabela 11.1). `bd` (bits por amostra) vai
// separado porque a paleta de 4 bits do `heavyweaponbrew` (medida: 21x20, bd=4)
// tem UMA amostra de 4 bits por pixel -- a contagem de amostras nao muda com a
// profundidade, o empacotamento sim.
std::uint32_t CanaisDoColorType(std::uint8_t ct) {
  switch (ct) {
    case 0: return 1;
    case 2: return 3;
    case 3: return 1;
    case 4: return 2;
    case 6: return 4;
    default: return 0;
  }
}

std::uint8_t Paeth(std::uint8_t a, std::uint8_t b, std::uint8_t c) {
  const int p = static_cast<int>(a) + static_cast<int>(b) - static_cast<int>(c);
  const int pa = p > static_cast<int>(a) ? p - static_cast<int>(a) : static_cast<int>(a) - p;
  const int pb = p > static_cast<int>(b) ? p - static_cast<int>(b) : static_cast<int>(b) - p;
  const int pc = p > static_cast<int>(c) ? p - static_cast<int>(c) : static_cast<int>(c) - p;
  if (pa <= pb && pa <= pc) return a;
  if (pb <= pc) return b;
  return c;
}

}  // namespace

bool DescodificarPng(const std::uint8_t* dados, std::size_t tamanho, ImagemPng* saida,
                     std::string* motivo, std::uint32_t teto_de_pixels) {
  if (motivo != nullptr) motivo->clear();
  if (saida != nullptr) *saida = ImagemPng{};
  const std::uint32_t teto = teto_de_pixels != 0u ? teto_de_pixels : kTetoDePixelsPorOmissao;
  auto recusar = [&](const std::string& porque) {
    if (motivo != nullptr) *motivo = porque;
    return false;
  };

  if (dados == nullptr || tamanho < 8u) {
    return recusar("stream com " + std::to_string(tamanho) +
                   " bytes: mais curto que os 8 da assinatura PNG");
  }
  for (std::size_t k = 0; k < 8; ++k) {
    if (dados[k] != kAssinatura[k]) {
      return recusar("nao comeca pela assinatura PNG; os primeiros bytes sao [" +
                     BytesEmHex(dados, std::min<std::size_t>(tamanho, 8)) + "]");
    }
  }

  bool tem_ihdr = false;
  bool tem_iend = false;
  std::uint32_t largura = 0;
  std::uint32_t altura = 0;
  std::uint8_t profundidade = 0;
  std::uint8_t color_type = 0;
  std::vector<std::uint8_t> paleta;
  std::vector<std::uint8_t> trns;
  std::vector<std::uint8_t> idat;

  std::size_t o = 8u;
  while (o < tamanho) {
    if (tamanho - o < 8u) {
      return recusar("chunk truncado: restam " + std::to_string(tamanho - o) +
                     " bytes e o cabecalho de um chunk sao 8 (tamanho + tipo)");
    }
    const std::uint32_t tamanho_do_chunk = LerBE32(dados + o);
    const std::uint8_t* tipo = dados + o + 4u;
    // O `+12` e o tamanho + o tipo + o CRC, e o CRC esta DEPOIS dos dados. A
    // comparacao e feita em `std::size_t` (64 bits) porque `tamanho_do_chunk` e
    // um u32 do ficheiro: somado em 32 bits, um tamanho mentiroso dava a volta e
    // passava a guarda.
    if (static_cast<std::size_t>(tamanho_do_chunk) + 12u + o > tamanho) {
      const std::string nome(reinterpret_cast<const char*>(tipo), 4);
      return recusar("chunk " + nome + " declara " + std::to_string(tamanho_do_chunk) +
                     " bytes de dados, e o stream so tem " + std::to_string(tamanho - o - 8u) +
                     " depois do cabecalho");
    }
    const std::uint8_t* corpo = dados + o + 8u;
    const std::uint32_t crc_lido = LerBE32(corpo + tamanho_do_chunk);
    const std::uint32_t crc_calculado = Crc32DePng(tipo, tamanho_do_chunk + 4u);
    if (crc_lido != crc_calculado) {
      char det[160];
      const std::string nome(reinterpret_cast<const char*>(tipo), 4);
      std::snprintf(det, sizeof(det),
                    "CRC do chunk %s: o ficheiro diz 0x%08x e o recalculado e 0x%08x | tipo=[%s] dados=[%s]",
                    nome.c_str(), crc_lido, crc_calculado,
                    BytesEmHex(tipo, 4).c_str(),
                    BytesEmHex(corpo, std::min<std::size_t>(tamanho_do_chunk, 16)).c_str());
      return recusar(det);
    }

    if (std::memcmp(tipo, "IHDR", 4) == 0) {
      if (tamanho_do_chunk != 13u) {
        return recusar("IHDR com " + std::to_string(tamanho_do_chunk) + " bytes; o cabecalho pede 13");
      }
      largura = LerBE32(corpo);
      altura = LerBE32(corpo + 4u);
      profundidade = corpo[8];
      color_type = corpo[9];
      const std::uint8_t compressao = corpo[10];
      const std::uint8_t filtro = corpo[11];
      const std::uint8_t entrelacado = corpo[12];
      if (largura == 0u || altura == 0u) {
        return recusar("IHDR com " + std::to_string(largura) + "x" + std::to_string(altura));
      }
      if (largura > 0xFFFFu || altura > 0xFFFFu) {
        // O `cx`/`cy` de um IDIB sao uint16 (`AEEIDIB.h:48-50`): uma imagem maior
        // do que isso nao cabe no cabecalho que o jogo vai ler.
        return recusar("IHDR com " + std::to_string(largura) + "x" + std::to_string(altura) +
                       ": o cx/cy de um IDIB sao uint16");
      }
      if (static_cast<std::uint64_t>(largura) * altura > teto) {
        return recusar("IHDR com " + std::to_string(largura) + "x" + std::to_string(altura) +
                       " = " + std::to_string(static_cast<std::uint64_t>(largura) * altura) +
                       " pixels, acima do teto de " + std::to_string(teto));
      }
      // 1, 2, 4 e 8 bits por amostra. O 16 RECUSA: nenhum dos quatro titulos o
      // pede, e converte-lo para 8 seria escolher um arredondamento que o SDK
      // nao declara em lado nenhum. O `heavyweaponbrew` entrega um PNG de 4 bits
      // de paleta (21x20, medido), e o 1/2/4 sao o mesmo caminho de
      // desempacotamento.
      if (profundidade != 1u && profundidade != 2u && profundidade != 4u && profundidade != 8u) {
        return recusar(std::string("bit depth ") + std::to_string(profundidade) +
                       ": serve-se 1, 2, 4 e 8 bits por amostra");
      }
      if (CanaisDoColorType(color_type) == 0u) {
        return recusar(std::string("color type ") + NomeDoColorType(color_type) +
                       ": fora dos cinco que a especificacao define");
      }
      if (compressao != 0u) {
        return recusar("metodo de compressao " + std::to_string(compressao) + " (o PNG define 0 = deflate)");
      }
      if (filtro != 0u) {
        return recusar("metodo de filtro " + std::to_string(filtro) + " (o PNG define 0)");
      }
      if (entrelacado != 0u) {
        // Adam7 servido como sequencial daria uma imagem com a forma errada e
        // sem sintoma nenhum. Nenhum dos quatro titulos o usa (medido).
        return recusar("entrelacado " + std::to_string(entrelacado) +
                       " (Adam7): so se serve o nao entrelacado");
      }
      tem_ihdr = true;
    } else if (std::memcmp(tipo, "PLTE", 4) == 0) {
      if (tamanho_do_chunk == 0u || tamanho_do_chunk % 3u != 0u || tamanho_do_chunk > 768u) {
        return recusar("PLTE com " + std::to_string(tamanho_do_chunk) +
                       " bytes: tem de ser 3 por cor, no maximo 768");
      }
      paleta.assign(corpo, corpo + tamanho_do_chunk);
    } else if (std::memcmp(tipo, "tRNS", 4) == 0) {
      trns.assign(corpo, corpo + tamanho_do_chunk);
    } else if (std::memcmp(tipo, "IDAT", 4) == 0) {
      if (idat.size() + tamanho_do_chunk > 16u * 1024u * 1024u) {
        return recusar("IDAT acima de 16 MiB acumulados");
      }
      idat.insert(idat.end(), corpo, corpo + tamanho_do_chunk);
    } else if (std::memcmp(tipo, "IEND", 4) == 0) {
      tem_iend = true;
      break;
    } else if ((tipo[0] & 0x20u) == 0u) {
      // O bit 5 do primeiro byte do tipo diz se o chunk e ANCILLARY (minusculo)
      // ou CRITICO (maiusculo): um critico desconhecido significa que falta
      // informacao para o desenho estar certo (PNG, 5.4), e recusa-se.
      const std::string nome(reinterpret_cast<const char*>(tipo), 4);
      return recusar("chunk critico desconhecido " + nome);
    }
    o += 12u + tamanho_do_chunk;
  }

  if (!tem_ihdr) return recusar("sem IHDR");
  if (idat.empty()) return recusar("sem IDAT: nao ha imagem comprimida no stream");
  if (!tem_iend) return recusar("sem IEND: o stream acabou a meio de um PNG");
  const std::uint32_t canais = CanaisDoColorType(color_type);
  if (color_type == 3u && paleta.empty()) {
    return recusar("color type 3 (paleta) sem PLTE");
  }

  // O PASSO DE UMA LINHA EM BYTES e o que o PNG chama de "1 + bytes por linha":
  // `largura * amostras * bd` bits, arredondado para cima (ISO/IEC 15948, 7.2).
  // O salto do filtro (`bpp`) e o numero de BYTES de um pixel completo, tambem
  // arredondado para cima e nunca menor que um.
  const std::uint32_t bits_por_pixel = canais * profundidade;
  const std::uint64_t passo =
      (static_cast<std::uint64_t>(largura) * bits_por_pixel + 7u) / 8u;
  const std::uint32_t bpp = (bits_por_pixel + 7u) / 8u;
  const std::uint64_t esperado = static_cast<std::uint64_t>(altura) * (passo + 1u);
  if (esperado > 0xFFFFFFFFull) return recusar("imagem grande demais para o inflate");

  std::vector<std::uint8_t> cru;
  std::string porque;
  if (!Inflar(idat.data(), idat.size(), &cru, &porque, static_cast<std::uint32_t>(esperado))) {
    return recusar("IDAT: " + porque);
  }
  if (cru.size() != esperado) {
    return recusar("o IDAT descomprimiu " + std::to_string(cru.size()) + " bytes e " +
                   std::to_string(largura) + "x" + std::to_string(altura) + " em " +
                   std::to_string(canais) + " canal(is) pede " + std::to_string(esperado));
  }

  // --- OS FILTROS POR LINHA (PNG, secao 9) ---------------------------------
  //
  // Cada linha vem com um byte de filtro a frente. O filtro e definido sobre os
  // bytes JA RECONSTRUIDOS da esquerda (`a`, `bpp` bytes antes) e de cima (`b`),
  // com o canto em `c`. Aplicar `Sub` depois de `Up` -- ou ao contrario -- passa
  // um teste de "descomprimiu" e da uma imagem com riscas.
  for (std::uint32_t y = 0; y < altura; ++y) {
    const std::size_t base = static_cast<std::size_t>(y) * (passo + 1u);
    const std::uint8_t filtro = cru[base];
    std::uint8_t* linha = cru.data() + base + 1u;
    // A linha de CIMA e a anterior, e o dado dela comeca `passo + 1` bytes antes
    // (a linha desta comeca em `base + 1`).
    const std::uint8_t* anterior = (y == 0u) ? nullptr : (cru.data() + base - passo);
    for (std::uint64_t x = 0; x < passo; ++x) {
      const std::uint8_t a = (x >= bpp) ? linha[x - bpp] : 0u;
      const std::uint8_t b = (anterior != nullptr) ? anterior[x] : 0u;
      const std::uint8_t c = (anterior != nullptr && x >= bpp) ? anterior[x - bpp] : 0u;
      const std::uint8_t v = linha[x];
      switch (filtro) {
        case 0: break;  // None
        case 1: linha[x] = static_cast<std::uint8_t>(v + a); break;  // Sub
        case 2: linha[x] = static_cast<std::uint8_t>(v + b); break;  // Up
        case 3:  // Average
          linha[x] = static_cast<std::uint8_t>(
              v + static_cast<std::uint8_t>((static_cast<int>(a) + static_cast<int>(b)) / 2));
          break;
        case 4: linha[x] = static_cast<std::uint8_t>(v + Paeth(a, b, c)); break;  // Paeth
        default:
          return recusar("filtro " + std::to_string(filtro) + " na linha " + std::to_string(y) +
                         ": os filtros do PNG sao 0..4");
      }
    }
  }

  // --- A COR ---------------------------------------------------------------
  const std::size_t quantos = static_cast<std::size_t>(largura) * altura;
  saida->largura = largura;
  saida->altura = altura;
  saida->pixels.assign(quantos, 0u);
  saida->tem_alpha = false;
  std::uint8_t chave_cinza = 0u;
  std::uint8_t chave_r = 0u, chave_g = 0u, chave_b = 0u;
  bool tem_chave = false;
  if (color_type == 0u || color_type == 3u) {
    if (trns.size() >= (color_type == 3u ? 1u : 2u)) {
      tem_chave = true;
      chave_cinza = trns[color_type == 3u ? 0u : 1u];
    }
  } else if (color_type == 2u) {
    if (trns.size() >= 6u) {
      tem_chave = true;
      chave_r = trns[1];
      chave_g = trns[3];
      chave_b = trns[5];
    }
  }
  // AS AMOSTRAS DE UMA LINHA, JA A 8 BITS. Com bd=8 a linha E as amostras; com
  // bd 1/2/4 (que so os color types 0 e 3 admitem, e sempre com UMA amostra por
  // pixel) desempacota-se MSB primeiro, e na escala de cinza o valor e ESTICADO
  // para 0..255 -- e o que o PNG manda (15948, 12.5: "the sample is scaled"), e
  // sem o esticar o cinza de 1 bit sairia a preto em tudo menos no branco.
  // O TAMANHO E EM AMOSTRAS, E NAO EM BYTES DA LINHA. O laco dos pixels abaixo
  // indexa `amostras_da_linha[x]` com `x` em PIXELS (x < largura) e le
  // `p[0..canais-1]`, logo o buffer tem de ter `largura * canais` entradas -- e
  // nao `passo`, que e o numero de BYTES de uma linha.
  //
  // MEDIDO (ASan, corrida do `peggle` com a forma `0xA000-0xAFFF` do Thumb
  // servida): `heap-buffer-overflow ... WRITE of size 1` em `png.cpp:350` sobre
  // um vector de 2 bytes criado aqui. As duas contas so coincidem com
  // profundidade 8; com bd 1/2/4, `passo = ceil(largura * bd / 8) < largura`, e o
  // laco escrevia `largura - passo` bytes DEPOIS do fim do vector -- heap do
  // HOSPEDEIRO. As larguras pequenas escapavam por folga do `malloc` (o caso
  // `cinza-1bit` desta suite tem largura 2 e `passo` 1, e passava); a partir de
  // `largura > passo + folga` o glibc aborta a corrida com "corrupted size vs.
  // prev_size" ou "free(): invalid pointer".
  //
  // O `max` cobre as duas leituras: `passo` para o `memcpy` do caminho de 8 bits,
  // e `largura * canais` para o caminho dos 1/2/4 bits.
  const std::size_t amostras_necessarias = std::max<std::size_t>(
      static_cast<std::size_t>(passo), static_cast<std::size_t>(largura) * canais);
  std::vector<std::uint8_t> amostras_da_linha(amostras_necessarias, 0u);
  for (std::uint32_t y = 0; y < altura; ++y) {
    const std::uint8_t* linha = cru.data() + static_cast<std::size_t>(y) * (passo + 1u) + 1u;
    if (profundidade == 8u) {
      std::memcpy(amostras_da_linha.data(), linha, static_cast<std::size_t>(passo));
    } else {
      const std::uint32_t maximo = (1u << profundidade) - 1u;
      for (std::uint32_t x = 0; x < largura; ++x) {
        const std::uint32_t bit = x * profundidade;
        const std::uint8_t crua = static_cast<std::uint8_t>(
            (linha[bit / 8u] >> (8u - profundidade - (bit % 8u))) & maximo);
        amostras_da_linha[x] =
            (color_type == 0u) ? static_cast<std::uint8_t>(crua * 255u / maximo) : crua;
      }
    }
    for (std::uint32_t x = 0; x < largura; ++x) {
      const std::uint8_t* p = amostras_da_linha.data() + static_cast<std::size_t>(x) * canais;
      std::uint8_t r = 0, g = 0, b = 0, a = 255;
      if (color_type == 0u) {
        r = g = b = p[0];
        if (tem_chave && p[0] == chave_cinza) a = 0;
      } else if (color_type == 2u) {
        r = p[0]; g = p[1]; b = p[2];
        if (tem_chave && r == chave_r && g == chave_g && b == chave_b) a = 0;
      } else if (color_type == 3u) {
        const std::uint32_t indice = p[0];
        if (static_cast<std::size_t>(indice) * 3u + 2u >= paleta.size()) {
          return recusar("indice de paleta " + std::to_string(indice) + " sem entrada na PLTE de " +
                         std::to_string(paleta.size() / 3u) + " cores");
        }
        r = paleta[indice * 3u];
        g = paleta[indice * 3u + 1u];
        b = paleta[indice * 3u + 2u];
        if (tem_chave) a = (indice < trns.size()) ? trns[indice] : 255u;
      } else if (color_type == 4u) {
        r = g = b = p[0];
        a = p[1];
      } else {  // 6
        r = p[0]; g = p[1]; b = p[2]; a = p[3];
      }
      if (a < 255u) saida->tem_alpha = true;
      saida->pixels[static_cast<std::size_t>(y) * largura + x] = ImagemPng::Rgb565(r, g, b);
    }
  }
  return true;
}

}  // namespace zb2
