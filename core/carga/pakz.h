#ifndef ZB2_CORE_CARGA_PAKZ_H
#define ZB2_CORE_CARGA_PAKZ_H

// PAKZ -- o recipiente dos assets dos titulos TTD (`resources.pakz`).
//
// E o formato dos recursos dos jogos com motor "TTD" (o da Alice no Pais dos
// Espelhos, dos Zeebo Sports Tenis/Peteca/Volei, do Zeeboids, do Footparty, do
// Funsoccer, ...). O `resources.pakz` da pasta do titulo guarda centenas de
// ficheiros (imagens, sons, ecras XUI, scripts Lua) num so recipiente, e o
// jogo pede-os por CAMINHO, coberto em `core/brew/vfs.h` como os `.pkg` sao.
//
// ---------------------------------------------------------------------------
// O FORMATO, MEDIDO (e nao transcrito de outro emulador)
// ---------------------------------------------------------------------------
// O censo independente em Python que acompanha esta etapa leu campo a campo os
// 10 `resources.pakz` do corpus (7 487 entradas no total: 298 + 323 + 1117 +
// 368 + 401 + 474 + 1048 + 793 + 1373 + 1292). O formato abaixo vale EM TODAS:
//
//   @0    4 bytes   "PACK"                                 (ASCII, sem NUL)
//   @4    u32 LE    offset ABSOLUTO da tabela de registos, no fim do ficheiro
//   @8    u32 LE    tamanho da tabela (sempre multiplo de 64; 793 x 64 = 50752
//                   no alice/resources.pakz)
//   @12   ...       os dados de cada entrada, CONTIGUOS, comecando em 12: a soma
//                   dos tamanhos comprimidos de TODAS as entradas e exactamente
//                   `offset_da_tabela - 12`, e a ultima entrada acaba no offset
//                   da tabela, sem buracos (medido nos 7 487).
//   @tabela        `tabela / 64` registos de 64 bytes:
//                   +0  ..+55  nome: ASCII com NUL no fim. O campo TEM 56 bytes
//                              e NAO 40: os 10 ficheiros tem nomes de ate 43
//                              caracteres, que transbordam os 40 e continuam
//                              nos bytes 40..55 -- o NUL e que marca o fim, nao
//                              o limite do campo. (O zeebulator-upstream le so
//                              40 e chama aos bytes 40..55 "4 campos u32
//                              nao confirmados, sempre zero"; medido: em 354
//                              das 7 487 entradas esses bytes carregam o rabo
//                              do nome -- em 462 o campo classico de 40 acaba
//                              antes do NUL.)
//                   +56  u32 LE  offset ABSOLUTO dos dados desta entrada
//                   +60  u32 LE  tamanho COMPRIMIDO
//
// Cada entrada e um stream LZMA_ALONE (o ".lzma" classico): 5 bytes de
// propriedades (1 byte de props + 4 do tamanho do dicionario), 8 bytes do
// tamanho descomprimido (little-endian, 0xffffffffffffffff = desconhecido) e o
// corpo. Os 7 486 streams que descomprimem consomem EXACTAMENTE o tamanho
// comprimido declarado no registo -- quem le um campo maior ou menor le o
// ficheiro no sitio errado. O tamanho declarado no proprio stream bate com o
// que o descompressor produz em 7 486 de 7 486 (e a unica entrada que falha,
// `audio/super_league_theme.mp3` do 280647, falha no proprio LZMA -- o
// recipiente esta inteiro, o stream e que esta corrompido).
//
// ---------------------------------------------------------------------------
// O QUE ESTE MODULO NAO FAZ
// ---------------------------------------------------------------------------
// Nao conhece caminhos nem o `IFileMgr`: e a VFS (`core/brew/vfs.h`). Aqui
// le-se o recipiente e recusa-se em voz alta o que nao bate com a medicao --
// sem lancar excecao, como todo o resto da arvore.
//
// O LZMA vem do liblzma do sistema (`lzma_alone_decoder`), o mesmo decodificador
// independente com que a medicao em Python bateu byte a byte.

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace zb2 {

// Uma entrada do indice. `nome` e o caminho relativo, COM barras (`ani/x`),
// como o jogo o pede e como o ficheiro o guarda.
struct EntradaDoPakz {
  std::string nome;
  std::uint32_t offset = 0;  // absoluto, no ficheiro do recipiente
  std::uint32_t tamanho_comprimido = 0;
  std::uint32_t tamanho_descomprimido = 0;  // lido do stream LZMA_ALONE (bytes 5..12)
};

class Pakz {
 public:
  Pakz() = default;

  // Le o recipiente a partir dos bytes do ficheiro inteiro. Os bytes ficam
  // GUARDADOS dentro do `Pakz` (nao ha copia por entrada), pelo que o objecto
  // tem de continuar vivo enquanto as entradas forem extraidas.
  //
  // Devolve false e escreve o motivo quando os bytes nao batem com o formato
  // medido: assinatura, tabela que passa do fim do ficheiro, tamanho da tabela
  // que nao e multiplo de 64, nome sem NUL nos 56 bytes, nome com byte nao
  // imprimivel, entrada cujos dados saem do ficheiro, ou dados que nao sao
  // CONTIGUOS desde o byte 12 ate ao offset da tabela (a soma dos tamanhos
  // comprimidos tem de ser exactamente `offset_da_tabela - 12`). Nunca lanca.
  bool Parse(std::vector<std::uint8_t> bytes, std::string* motivo);

  // Indexa um PAKZ que permanece em disco sem trazer os payloads para memoria.
  // Le somente cabecalho, tabela e os 13 bytes LZMA de cada entrada. O handle
  // continua aberto; Extrair le depois apenas o bloco comprimido pedido.
  bool IndexarFicheiro(const std::string& caminho, std::string* motivo);

  bool Valido() const { return valido_; }
  const std::string& Motivo() const { return motivo_; }

  const std::vector<EntradaDoPakz>& ListaDeEntradas() const { return entradas_; }
  std::size_t NumeroDeEntradas() const { return entradas_.size(); }
  // Factos contados, nao criterios (como o `ConstanteDivergente` do PACK):
  // quantos nomes se repetem e quantos ocupam o campo do nome por inteiro
  // (40+ bytes, o rabo vive nos bytes 40..55 -- ver o cabecalho).
  std::size_t NomesRepetidos() const { return nomes_repetidos_; }
  std::size_t NomesNoCampoInteiro() const { return nomes_no_campo_inteiro_; }

  // A entrada com este nome (sem distincao de maiusculas/minusculas), ou nulo.
  // Quando o recipiente traz o mesmo nome duas vezes, ganha a PRIMEIRA.
  const EntradaDoPakz* Procurar(const std::string& nome) const;

  // Descomprime uma entrada (LZMA_ALONE, via liblzma) para `saida`.
  // `motivo` diz QUAL entrada e o que falhou.
  // Recusa quando o stream nao consome exactamente o tamanho comprimido do
  // registo, quando produz mais do que o tamanho declarado no proprio stream,
  // ou quando o liblzma recusa o corpo -- e o motivo diz qual das tres foi.
  bool Extrair(const EntradaDoPakz& entrada, std::vector<std::uint8_t>* saida,
               std::string* motivo) const;
  bool Extrair(std::size_t indice, std::vector<std::uint8_t>* saida, std::string* motivo) const;
  bool ExtrairPorNome(const std::string& nome, std::vector<std::uint8_t>* saida,
                      std::string* motivo) const;

 private:
  // Em modo IndexarFicheiro, `ficheiro_` e a fonte dos payloads. O stream e
  // mutavel pois Extrair e uma consulta logica const, mas move-se com Pakz.
  mutable std::ifstream ficheiro_;
  bool indexado_de_ficheiro_ = false;
  std::uint64_t tamanho_do_ficheiro_ = 0;
  std::vector<std::uint8_t> bytes_;
  std::vector<EntradaDoPakz> entradas_;
  bool valido_ = false;
  std::string motivo_;
  std::size_t nomes_repetidos_ = 0;
  std::size_t nomes_no_campo_inteiro_ = 0;
};

// Os numeros da medicao, num so sitio, para o teste os poder usar sem os
// repetir e para uma recusa poder dizer QUAL campo falhou.
namespace pakz_campos {
constexpr char kAssinatura[5] = "PACK";
constexpr std::uint32_t kCabecalho = 12u;
constexpr std::uint32_t kRegisto = 64u;
// O nome vive nos 56 primeiros bytes: o limite e o NUL, nao o byte 40.
constexpr std::uint32_t kCampoDoNome = 56u;
constexpr std::uint32_t kOffsetDoOffset = 56u;
constexpr std::uint32_t kOffsetDoTamanho = 60u;
// O cabecalho do stream LZMA_ALONE: 5 bytes de propriedades + 8 do tamanho.
constexpr std::uint32_t kPropsDoLzma = 5u;
constexpr std::uint32_t kTamanhoDeclaradoDoLzma = 8u;
// O tamanho "desconhecido" do LZMA_ALONE, e o teto para esse caso: o maior
// descomprimido medido no corpus e 1 048 973 bytes, e crescer sem teto e
// deixar o hospedeiro encher-se. Os 7 487 do corpus declaram o tamanho; os
// streams que o liblzma monta (como os do teste sintetico) usam o
// "desconhecido", logo o caminho de crescer e de verdade e nao so defesa.
constexpr std::uint64_t kTamanhoDesconhecidoDoLzma = 0xffffffffffffffffull;
constexpr std::uint64_t kTetoSemTamanhoDeclarado = 64u * 1024u * 1024u;
}  // namespace pakz_campos

}  // namespace zb2

#endif  // ZB2_CORE_CARGA_PAKZ_H
