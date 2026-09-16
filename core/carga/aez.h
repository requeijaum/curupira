#ifndef ZB2_CORE_CARGA_AEZ_H
#define ZB2_CORE_CARGA_AEZ_H

// AEZ -- o recipiente de recursos dos titulos BREW 3D (`res.aez`, `tracks.aez`,
// `textures.aez`, ...): uma sequencia de registos colados, cada um com um
// CAMINHO ABSOLUTO e um payload GZIP (ou CRU).
//
// PORQUE EXISTE: e o muro que prende o `gof` (277380) e o `rmp` (278282). Os dois
// MONTAM o 3D por quadro e nunca submetem geometria porque os recursos nao
// carregam: medido no traco da bateria (300 quadros), o `gof` recusa 35x o
// `OpenFile` de `/data/textures/main_menu.aei` e o `rmp` 91x em 71 caminhos
// (`/data/textures/*.aei`, `/data/tracks/t1..t18.w3t`). Nenhum destes caminhos
// existe SOLTO na pasta do titulo -- existe DENTRO de um `.aez` dela. E o
// `IFileMgr` da consola serve `/data/...` a partir do `.aez` da pasta, sem o
// jogo abrir o `.aez` por nome: nos 300 quadros do `gof` e do `rmp` o UNICO
// `OpenFile` aceite e o `config.cfg` solto (medido).
//
// ---------------------------------------------------------------------------
// O FORMATO, MEDIDO (12 `.aez` do corpus, 1 390 registos)
// ---------------------------------------------------------------------------
// Nao ha cabecalho de ficheiro nenhum: o ficheiro E a sequencia de registos,
// colados, do primeiro byte ao ultimo. O censo independente em Python que
// acompanha esta etapa leu os 12 ficheiros campo a campo -- 182 do `res.aez` do
// gof, 268+38+12+54+18+91+164+113+30+103+317 dos 11 do rmp -- e o cursor cai
// EXACTAMENTE no fim do ficheiro nos 12 (0 bytes de sobra, 0 bytes a faltar):
//
//   @0     u8        comprimento N do caminho (1..255). NAO ha terminador:
//                    o caminho ocupa exactamente N bytes e o campo seguinte
//                    comeca logo a seguir (os nomes do corpus tem espaco,
//                    `alien weapon.aem`, e mais nenhum byte especial).
//   @1     N bytes   caminho ASCII, COM barra inicial (`/data/tracks/t1.w3t`).
//                    Medido: os 1 390 sao imprimiveis (0x20..0x7e) e 1 387
//                    distintos, e os 3 repetidos vivem em ficheiros diferentes.
//   @1+N   u32 LE    tamanho DESCOMPRIMIDO da entrada.
//   @5+N   u32 LE    tamanho COMPRIMIDO no ficheiro, ou 0xffffffff = GUARDADA.
//   @9+N   ...       o payload: `tamanho_comprimido` bytes de GZIP (RFC1952)
//                    quando o 2.o campo nao e 0xffffffff, e `tamanho_descomprimido`
//                    bytes CRUS quando e.
//
// O `campo` que uma leitura rapida toma por um CRC, um tipo ou um id e o TAMANHO
// DESCOMPRIMIDO -- e nao uma hipotese: ele bate com o que o `zlib` produz nos
// 1 112 registos comprimidos, e os 278 GUARDADOS declaram-no como unico tamanho
// (nenhum dos 278 comeca em `1f 8b`). Exemplos reais do `res.aez` do gof:
//
//   "/data/meshes/alien weapon.aem"            12883 -> 9189 bytes de gzip
//   "/data/meshes/alien_battleship_01.aem"     43172 -> 19083
//   "/data/meshes/asteroid_part_04.aem"         1141 -> CRU (0xffffffff)
//
// OS NUMEROS DO GZIP, medidos nos 1 112 registos comprimidos: o stream consome
// EXACTAMENTE os `tamanho_comprimido` declarados (0 bytes de sobra em 1 112 de
// 1 112), e o RODAPE (CRC32 + ISIZE, os 8 bytes finais do RFC1952) confere com o
// que sai em 1 112 de 1 112. Quem le um campo maior ou menor le o registo
// seguinte no sitio errado -- e como o cursor so acaba no fim exacto do
// ficheiro, uma geometria mentirosa nao se esconde.
//
// ---------------------------------------------------------------------------
// O QUE ESTE MODULO NAO FAZ
// ---------------------------------------------------------------------------
// Nao conhece caminhos nem o `IFileMgr`: e a VFS (`core/brew/vfs.h`) que decide
// quem serve. Aqui le-se o recipiente e recusa-se em voz alta o que nao bate com
// a medicao -- sem lancar excecao, como todo o resto da arvore (`bool` + motivo).
//
// O GZIP e desembrulhado AQUI (cabecalho RFC1952 + CRC-32/ISIZE do rodape) sobre
// o descompressor de deflate da arvore, o `Inflar` dos `.pkg`: o `Inflar` recebe
// o corpo deflate dentro de um cabecalho zlib sintetico e a prova do gzip e o
// RODAPE dele. E a mesma tecnica que o `despacho.cpp` ja usa para o gzip do
// `.bar` -- e esta aqui, e nao la, porque o `despacho.cpp` e de outra frente. O
// CRC-32 do rodape e o `Crc32DePng` do `png.h` (o mesmo polinomio), para nao
// haver mais uma copia da regra.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace zb2 {

// Uma entrada do indice. `nome` e o caminho do PROPRIO registo, COM a barra
// inicial, tal como o guest o pede (`/data/tracks/t1.w3t`).
struct EntradaDoAez {
  std::string nome;
  std::uint32_t offset = 0;               // absoluto, do 1.o byte do payload
  std::uint32_t tamanho_no_ficheiro = 0;  // bytes do payload no ficheiro
  std::uint32_t tamanho_descomprimido = 0;
  bool guardado = false;                  // payload CRU em vez de gzip
};

class Aez {
 public:
  Aez() = default;

  // Le o recipiente a partir dos bytes do ficheiro inteiro. Os bytes ficam
  // GUARDADOS dentro do `Aez` (nao ha copia por entrada), pelo que o objecto
  // tem de continuar vivo enquanto as entradas forem extraidas.
  //
  // Devolve false e escreve o motivo quando os bytes nao batem com o formato
  // medido: nome vazio ou com byte nao imprimivel, comprimento de caminho que
  // passa do fim, registo cuja geometria sai do ficheiro, payload comprimido
  // mais curto do que os 18 bytes de cabecalho+rodape do gzip, payload
  // comprimido que nao comeca em `1f 8b`, ou 2.o campo a zero. Nunca lanca.
  bool Parse(std::vector<std::uint8_t> bytes, std::string* motivo);

  bool Valido() const { return valido_; }
  const std::string& Motivo() const { return motivo_; }

  const std::vector<EntradaDoAez>& ListaDeEntradas() const { return entradas_; }
  std::size_t NumeroDeEntradas() const { return entradas_.size(); }
  // Factos contados, nao criterios: quantos nomes se repetem e quantos registos
  // vem GUARDADOS (o 2.o campo a 0xffffffff).
  std::size_t NomesRepetidos() const { return nomes_repetidos_; }
  std::size_t RegistosGuardados() const { return registos_guardados_; }

  // A entrada com este nome (sem distincao de maiusculas/minusculas), ou nulo.
  // Quando o recipiente traz o mesmo nome duas vezes, ganha a PRIMEIRA.
  const EntradaDoAez* Procurar(const std::string& nome) const;

  // Extrai uma entrada para `saida`: payload cru quando `guardado`, e GZIP
  // (RFC1952) desembrulhado e conferido pelo rodape (CRC32 + ISIZE) nos outros.
  // `motivo` diz QUAL entrada e o que falhou.
  bool Extrair(const EntradaDoAez& entrada, std::vector<std::uint8_t>* saida,
               std::string* motivo) const;
  bool Extrair(std::size_t indice, std::vector<std::uint8_t>* saida, std::string* motivo) const;
  bool ExtrairPorNome(const std::string& nome, std::vector<std::uint8_t>* saida,
                      std::string* motivo) const;

 private:
  std::vector<std::uint8_t> bytes_;
  std::vector<EntradaDoAez> entradas_;
  bool valido_ = false;
  std::string motivo_;
  std::size_t nomes_repetidos_ = 0;
  std::size_t registos_guardados_ = 0;
};

// Os numeros da medicao, num so sitio, para o teste os poder usar sem os repetir
// e para uma recusa poder dizer QUAL campo falhou.
namespace aez_campos {
// O 2.o campo do registo quando o payload NAO esta comprimido.
constexpr std::uint32_t kGuardado = 0xffffffffu;
// Cabecalho de um registo: 1 byte de comprimento + 4 + 4. Nao ha cabecalho de
// FICHEIRO nenhum.
constexpr std::uint32_t kMinimoDoRegisto = 9u;
// GZIP: 10 bytes de cabecalho + 8 de rodape (CRC32 + ISIZE).
constexpr std::uint32_t kMinimoDoGzip = 18u;
// O gzip do RFC1952, os dois primeiros bytes de qualquer payload comprimido.
constexpr std::uint8_t kGzipId1 = 0x1fu;
constexpr std::uint8_t kGzipId2 = 0x8bu;
// O teto de uma entrada: o maior descomprimido medido no corpus e 1 051 937
// (o `tex2d_main_menu.aei` do rmp; o maior do gof e o `main_menu.aei`, 1 051 871),
// e crescer sem teto e deixar o hospedeiro encher-se.
constexpr std::uint32_t kTetoDescomprimido = 64u * 1024u * 1024u;
}  // namespace aez_campos

}  // namespace zb2

#endif  // ZB2_CORE_CARGA_AEZ_H
