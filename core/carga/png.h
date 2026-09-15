#ifndef ZB2_CORE_CARGA_PNG_H
#define ZB2_CORE_CARGA_PNG_H

// DESCODIFICADOR PNG -- o formato que os quatro titulos da frente `imgdec`
// entregam ao `IImageDecoder` da consola.
//
// PORQUE EXISTE, e como se sabe que o formato e este (MEDIDO, nao deduzido): os
// quatro titulos que queimam os 8 M passos num laco de conversao de imagem
// (`abd` 279369, `peggle` 278962, `torkandkral` 280463, `heavyweaponbrew`
// 278200) pedem `AEECLSID_PNGDECODER_BREW` (0x01030766) ao `CreateInstance`,
// perguntam-lhe `AEEIID_IForceFeed` (0x0101eb0b), escrevem-lhe um recurso e
// pedem-lhe o bitmap. O recurso, lido do `.bar` de cada um com
// `tools/medir_bar.py recurso <bar> <tipo> <id>`:
//
//   heavyweaponbrew  heavyweapon.bar tipo=20480 id=9346  7191 B  PNG 943x44  bd=8 ct=6 (RGBA)
//   heavyweaponbrew  heavyweapon.bar tipo=20480 id=9136   263 B  PNG 21x20   bd=4 ct=3 (paleta)
//   heavyweaponbrew  heavyweapon.bar tipo=20480 id=9162  4368 B  PNG 60x90   bd=8 ct=6 (RGBA)
//   heavyweaponbrew  heavyweapon.bar tipo=20480 id=9133   927 B  PNG 63x30   bd=8 ct=3 (paleta)
//   peggle           resources.bar   tipo=6     id=5000 64629 B  PNG 252x252 bd=8 ct=3 (PLTE+tRNS)
//   abd              data.bar        tipo=20480 id=9073  1148 B  PNG 291x125 bd=8 ct=6 (RGBA)
//   torkandkral      data.bar        tipo=20480 id=9008  1148 B  PNG 291x125 bd=8 ct=6 (RGBA)
//
// Os quatro comecam pela assinatura `89 50 4e 47 0d 0a 1a 0a` (medido). Os tres
// titulos que pedem com `tipo=20480` recebem o PNG CRU; o `peggle` pede com
// `tipo=6` e recebe um `AEEResBlob` -- o mime vem impresso no proprio recurso
// (`image/png`, `core/carga/bar.h:183`) e o dado comeca em `bDataOffset`.
//
// O QUE ESTE DESCODIFICADOR SUPORTA, e o que RECUSA COM O NOME:
//   - 1, 2, 4 e 8 bits por amostra; o 16 RECUSA com o numero (nao ha nenhum nos
//     recursos medidos, e arredonda-lo para 8 seria escolher um arredondamento
//     que o SDK nao declara). O `heavyweaponbrew` entrega ALEM dos 8 bits um
//     PNG de PALETA DE 4 BITS (id 9136, 21x20, medido);
//   - color types 0 (cinza), 2 (RGB), 3 (paleta, com PLTE e tRNS opcional),
//     4 (cinza+alfa) e 6 (RGBA);
//   - entrelacado (Adam7) RECUSA -- nenhum dos quatro o usa, e um Adam7 servido
//     como sequencial daria uma imagem com a forma errada em silencio;
//   - o CRC32 de TODOS os chunks e conferido (PNG, secao 5.5); um chunk
//     corrompido recusa em vez de desenhar lixo.
//
// A saida e RGB565, a mesma profundidade dos DIBs desta arvore (`CamposDoIdib`,
// `kEsquemaDeCor565`): o jogo le `pBmp` e converte-o no laco que ja existe.
//
// NADA AQUI LANCA: uma recusa e um `bool` falso com o motivo escrito (P2), como
// no `inflate.h` e no `bar.h`. O `Inflar` desta mesma pasta e reusado -- o PNG e
// inflate + filtros por linha, e nao ha uma segunda implementacao do RFC1950.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace zb2 {

// Uma imagem descodificada. `pixels` tem `largura * altura` elementos RGB565
// (5 bits R, 6 G, 5 B), na ordem de LEITURA: a primeira linha do ficheiro e a
// primeira do vector.
struct ImagemPng {
  std::uint32_t largura = 0;
  std::uint32_t altura = 0;
  // VERDADEIRO quando alguma amostra de alfa do ficheiro e menor que 255. O
  // `rgb565` nao carrega alfa nenhum; o que se serve e a informacao do
  // `GetRop` (`AEERasterOp.h:26-33`, COPY para opacas, TRANSPARENT para as
  // outras). Fica medido em vez de deduzido do color type: um PNG com canal de
  // alfa todo a 255 e uma imagem OPACA.
  bool tem_alpha = false;
  std::vector<std::uint16_t> pixels;

  static constexpr std::uint16_t Rgb565(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return static_cast<std::uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
  }
};

// O CRC-32 do PNG (ISO/IEC 15948, secao 5.5): o polinomio reflectido
// 0xEDB88320, iniciado a 0xFFFFFFFF e invertido no fim, calculado sobre o TIPO
// seguido dos DADOS de um chunk.
//
// Expoe-se pela mesma razao que o `Adler32` do `inflate.h`: um teste que MONTA um
// PNG (nao ha um codificador de PNG nesta arvore, e nao passa a haver) tem de
// escrever o CRC certo -- e escreve-o com ESTA funcao, e nao com uma segunda
// copia da regra a concordar consigo mesma.
std::uint32_t Crc32DePng(const std::uint8_t* dados, std::size_t n);

// Descodifica um PNG COMPLETO que comeca em `dados` (assinatura incluida).
//
// Devolve `true` e preenche `saida`; devolve `false` e escreve o motivo medido em
// `motivo` (pode ser nulo). Nunca le fora dos `tamanho` bytes.
//
// `teto_de_pixels` limita a area (0 = o limite por omissao): um cabecalho
// mentiroso com 65535x65535 e recusado ANTES de qualquer alocacao, e nao depois
// de o emulador tentar 8 GB.
bool DescodificarPng(const std::uint8_t* dados, std::size_t tamanho, ImagemPng* saida,
                     std::string* motivo, std::uint32_t teto_de_pixels = 0u);

}  // namespace zb2

#endif  // ZB2_CORE_CARGA_PNG_H
