#ifndef ZB2_CORE_CARGA_PACK_H
#define ZB2_CORE_CARGA_PACK_H

// PACK -- o formato dos ficheiros `.pkg` do Zeebo (as ROMs do jogo).
//
// E o formato que destrava a familia Neo Geo: os titulos Data East sao
// EMULADORES de Neo Geo dentro do Zeebo, e as ROMs do jogo vivem dentro do
// `.pkg` da pasta do titulo (`<stem>.pkg`), com o BIOS noutro pacote
// (`boot.pkg`, uma unica entrada chamada `boot.rom`). O guest abre-os por
// caminho -- e `core/brew/vfs.h` e quem liga o pacote ao `IFileMgr`.
//
// ---------------------------------------------------------------------------
// O FORMATO, MEDIDO (e nao transcrito de outro emulador)
// ---------------------------------------------------------------------------
// O comando que refaz a medicao inteira esta em `tests/pack_test.cpp` (o teste
// do `boot.pkg` real) e no censo independente em Python que acompanha esta
// etapa. Os 9 `.pkg` do corpus foram lidos campo a campo -- 127 entradas --
// e o formato abaixo vale EM TODAS ELAS:
//
//   @0    4 bytes   "PACK"                                    (ASCII, sem NUL)
//   @4    u32 LE    numero de entradas (`count`)
//   @8    u32 LE    offset ABSOLUTO da TABELA DE NOMES, no fim do ficheiro
//   @12   256 bytes de padding, todos zero (medido nos 9 ficheiros)
//   @268  `count` registos de 20 bytes:
//                   +0  u32   constante 0x00020000 em 127 de 127 entradas
//                   +4  u32   hash do conteudo (NENHUMA funcao conhecida: nao e
//                             crc32 nem adler32 do comprimido nem do
//                             descomprimido, nem fnv/djb2 do nome). LIDO E NAO
//                             USADO: o indice de confianca e o adler32 do
//                             proprio stream zlib, que o RFC1950 obriga.
//                   +8  u32   tamanho COMPRIMIDO
//                   +12 u32   tamanho DESCOMPRIMIDO
//                   +16 u32   offset ABSOLUTO dos dados
//   @tabela        UM unico stream zlib (RFC1950) que descomprime para `count`
//                  nomes de 256 bytes, ASCII, com NUL no fim, NA MESMA ORDEM
//                  dos registos.
//
// Cada entrada e um stream zlib CRU (RFC1950), NAO gzip: os 127 comecam em
// `78 da` (CM=8 deflate, CINFO=7, FLEVEL=3, FDICT=0), e o cabecalho gzip
// (`1f 8b`) nao aparece em nenhum. Ler um `78 da` como gzip e o erro que faz um
// descompressor parecer que funciona num ficheiro e falhar no seguinte.
//
// O layout dos dados e CONTIGUO, e isto foi medido: o primeiro dado comeca em
// `268 + 20*count` (sem buraco depois dos registos), nao ha buracos entre
// entradas, e o fim do ultimo dado E O OFFSET DA TABELA DE NOMES em todos os 9
// ficheiros. Quem le uma entrada le exatamente `tamanho_comprimido` bytes.
//
// ---------------------------------------------------------------------------
// O QUE ESTE MODULO NAO FAZ
// ---------------------------------------------------------------------------
// Nao conhece caminhos, nao conhece o `IFileMgr` e nao sabe que um nome de
// entrada e um ficheiro do guest: isso e a VFS (`core/brew/vfs.h`). Aqui le-se
// o recipiente e recusa-se em voz alta o que nao bate com a medicao acima -- sem
// lancar excecao, como todo o resto da arvore.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace zb2 {

// Uma entrada do indice. Os nomes dos campos sao os da medicao (secao acima).
struct EntradaDoPack {
  std::string nome;
  std::uint32_t tamanho_comprimido = 0;
  std::uint32_t tamanho_descomprimido = 0;
  std::uint32_t offset = 0;  // absoluto, no ficheiro do pacote
  std::uint32_t hash = 0;    // lido do registo; nao se usa como criterio
};

class Pacote {
 public:
  Pacote() = default;

  // Le o recipiente a partir dos bytes do ficheiro inteiro. Os bytes ficam
  // GUARDADOS dentro do `Pacote` (nao ha copia por entrada), pelo que o objecto
  // tem de continuar vivo enquanto as entradas forem extraidas.
  //
  // Devolve false e escreve o motivo quando os bytes nao batem com o formato
  // medido: assinatura, `count`, offset da tabela, tabela de nomes que nao
  // descomprime para `count`*256 bytes, entrada cujos dados saem do ficheiro,
  // nome sem NUL nos 256 bytes, ou dados que nao comecam por um cabecalho zlib.
  // Nunca lanca.
  bool Parse(std::vector<std::uint8_t> bytes, std::string* motivo);

  bool Valido() const { return valido_; }
  const std::string& Motivo() const { return motivo_; }

  const std::vector<EntradaDoPack>& ListaDeEntradas() const { return entradas_; }
  std::size_t NumeroDeEntradas() const { return entradas_.size(); }

  // A entrada com este nome (sem distincao de maiusculas/minusculas), ou nulo.
  // Quando o pacote traz o mesmo nome duas vezes, ganha a PRIMEIRA -- e a
  // repeticao fica contada em `NomesRepetidos()`.
  const EntradaDoPack* Procurar(const std::string& nome) const;
  std::size_t NomesRepetidos() const { return nomes_repetidos_; }

  // Descomprime uma entrada (zlib, RFC1950) para `saida`.
  // `motivo` diz QUAL entrada e o que falhou: "entrada 3 (066-c3.bin): ...".
  bool Extrair(const EntradaDoPack& entrada, std::vector<std::uint8_t>* saida,
               std::string* motivo) const;
  bool Extrair(std::size_t indice, std::vector<std::uint8_t>* saida, std::string* motivo) const;
  bool ExtrairPorNome(const std::string& nome, std::vector<std::uint8_t>* saida,
                      std::string* motivo) const;

  // O que o ficheiro declarava e nao foi usado como criterio, declarado aqui em
  // vez de ficar em silencio: entradas cujo campo constante nao era 0x00020000.
  std::size_t ConstanteDivergente() const { return constante_divergente_; }

 private:
  std::vector<std::uint8_t> bytes_;
  std::vector<EntradaDoPack> entradas_;
  bool valido_ = false;
  std::string motivo_;
  std::size_t nomes_repetidos_ = 0;
  std::size_t constante_divergente_ = 0;
};

// Os numeros da medicao, num so sitio, para o teste os poder usar sem os repetir
// e para uma recusa poder dizer QUAL campo falhou.
namespace pack_campos {
constexpr char kAssinatura[5] = "PACK";
constexpr std::uint32_t kCabecalho = 268u;  // 12 + 256 de padding
constexpr std::uint32_t kRegisto = 20u;
constexpr std::uint32_t kNome = 256u;
constexpr std::uint32_t kConstante = 0x00020000u;
}  // namespace pack_campos

}  // namespace zb2

#endif  // ZB2_CORE_CARGA_PACK_H
