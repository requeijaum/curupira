#ifndef ZB2_CORE_BREW_VFS_H
#define ZB2_CORE_BREW_VFS_H

// A VFS do modulo: a pasta irma do `.mod`, SO DE LEITURA -- e as ROMs que vivem
// dentro dos `.pkg` dessa pasta.
//
// VIVE NO MOTOR. A VFS e uma decisao do emulador (nao escrever no modulo do jogo
// preserva a reprodutibilidade), e nao um pormenor do instrumento.
//
// ---------------------------------------------------------------------------
// OS PACOTES `.pkg` (a familia Neo Geo)
// ---------------------------------------------------------------------------
// Os titulos Data East sao EMULADORES de Neo Geo dentro do Zeebo. As ROMs do
// jogo nao estao soltas na pasta: estao dentro de `<stem>.pkg`, e o BIOS dentro
// de `boot.pkg` (uma unica entrada, `boot.rom`, 8192 bytes). O guest pede-os por
// CAMINHO, e os caminhos que ele pede sao de pasta:
//
//     .\<stem>\boot.rom            (%s\%s\%s com ("." , stem, "boot.rom"))
//     roms\<stem>\boot.rom
//     roms\neogeo\<stem>\boot.rom
//
// A REGRA, DECLARADA E NAO INVENTADA CASO A CASO: **a uniao das entradas de
// TODOS os `.pkg` da pasta do titulo e servida como o directorio `<stem>`** (o
// stem do `.mod`, que e o mesmo do ficheiro pedido), e tambem como o directorio
// de CADA pacote (o seu proprio stem), o que faz `boot/boot.rom` resolver.
// Os prefixos `roms\` e `roms\neogeo\` sao aceites e retirados antes do
// casamento, que e feito pelo sufixo `<dir>/<nome>`; a comparacao dos nomes nao
// distingue maiusculas de minusculas (os 127 nomes medidos sao todos minusculos,
// e o guest monta o caminho a partir do nome do ficheiro).
//
// Da pasta `279126` (karnovr) resulta entao:
//     `karnovr/boot.rom`   -> entrada `boot.rom` do `boot.pkg`     (8192 bytes)
//     `karnovr/066-p1.bin` -> entrada `066-p1.bin` do `karnovr.pkg`
//
// A REGRA FICA DECLARADA NO TRACO (`DeclararNoTraco`), com o numero de pacotes,
// o numero de entradas e os directorios aceites: uma ligacao que ninguem ve e
// uma ligacao que se perde em silencio -- e foi assim que este projeto perdeu a
// cablagem do `SetTimer` uma vez. Um pacote que nao parseia e um evento de ERRO
// com o motivo, nao um silencio.
//
// ---------------------------------------------------------------------------
// OS PACOTES `.pakz` (os assets dos titulos TTD, formato em core/carga/pakz.h)
// ---------------------------------------------------------------------------
// Os titulos com motor TTD guardam os assets num `resources.pakz` (LZMA, e nao
// zlib como o `.pkg`). O nome de cada entrada do `.pakz` JA E UM CAMINHO relativo
// (`ani/touxiang1`, `xui/mainmenu.xui`, `lua/config`), e a regra e a do `.pkg`
// mais um caso: a uniao das entradas de TODOS os `.pakz` da pasta e servida
// (a) em cada directorio servido (o stem do `.mod` e o stem de cada pacote) e
// (b) no proprio caminho da entrada, quando ele traz barra, e o caminho pode
// ter MAIS do que uma barra (`xui/textures/ball.atitc`) -- e assim que o motor
// TTD pede, e o `.pakz` guarda a entrada ja com o caminho. O casamento inteiro
// e por MEMBRESIA EXACTA numa uniao de caminhos completos, sem listas
// paralelas de directorios: `ani/touxiang1` resolve porque a entrada
// `ani/touxiang1` esta no indice, e `xui/textures/ball.atitc` resolve porque a
// entrada de dois andares esta no indice. Um nome solto da tabela (sem barra,
// como `z1.lua`) so resolve com o directorio servido a frente (`alice/z1.lua`)
// -- exactamente como um nome de entrada do `.pkg`; o caminho cru sem
// directorio nao fica inventado em cima da regra. As regras ficam declaradas
// no traco (`DeclararNoTraco`), com o
// numero de pacotes e o de entradas, uma ligacao que ninguem ve e uma ligacao
// que se perde em silencio. Um `.pakz` que nao parseia e um evento de ERRO com
// o motivo, como um `.pkg` recusado.
//
// ---------------------------------------------------------------------------
// OS RECIPIENTES `.aez` (os recursos dos titulos BREW 3D -- gof, rmp, pbc)
// ---------------------------------------------------------------------------
// O `.aez` (formato em `core/carga/aez.h`) e o terceiro recipiente, e o unico
// cujo indice traz CAMINHOS ABSOLUTOS: cada registo guarda o caminho que o guest
// pede (`/data/tracks/t1.w3t`, `/data/textures/main_menu.aei`, `/pt.lang`), e
// nao um nome relativo debaixo de um directorio servido como o `.pakz`.
//
// A REGRA E UMA SO, e sai da medicao do traco (300 quadros, `ZB2_TRACE=1`): o
// `gof` e o `rmp` pedem esses caminhos pelo `OpenFile` e o ficheiro NAO existe
// solto na pasta -- existe dentro de um `.aez` dela. O `OpenFile` pede
// `/data/textures/main_menu.aei` e o registo do `res.aez` diz exactamente
// `/data/textures/main_menu.aei` (medido: 30 dos 37 caminhos recusados do `gof`
// e 71 dos 71 do `rmp` estao num `.aez` da propria pasta). Logo:
//
//   **a uniao das entradas de TODOS os `.aez` da pasta e servida pelo caminho
//   DA PROPRIA ENTRADA, com a barra inicial retirada** (que e o que a
//   normalizacao do BREW ja fazia: `\` vira `/`, a barra inicial cai), sem
//   distinguir maiusculas de minusculas. Um nome de registo sem barra nenhuma
//   (`pt.lang`, `gb.lang`) serve-se pelo nome CRU, exactamente como o guest o
//   pede.
//
// As formas `<dir>/<caminho>` do `.pakz` NAO se inventam aqui: nenhum dos dois
// titulos medidos pede `gof/data/tracks/t1.w3t`, e uma ligacao inventada que
// ninguem pede e uma ligacao que so aparece no dia em que colide com outra.
// O `.aez` nao e aberto pelo jogo: nos 300 quadros do `gof` e do `rmp` o UNICO
// `OpenFile` aceite e o `config.cfg` solto (medido) -- quem le `/data/...` do
// `.aez` e o `IFileMgr` DA CONSOLA, e e isso que a VFS passa a fazer.
//
// E A CAIXA DOS NOMES: a pasta do titulo e FAT (insensivel), o disco desta
// maquina nao e. Um pedido com a caixa trocada (`/GalaxyOnFire1_Won.mp3` para
// `galaxyonfire1_won.mp3`) recusava por uma razao que nao existe na consola --
// medido no gof, 7 pedidos. A caixa EXATA ganha sempre; a ignorada e a segunda
// tentativa, e so para os ficheiros SOLTOS (as entradas de um recipiente ja
// eram casadas sem caixa).
//
// A VFS CONTINUA SO DE LEITURA. Nada aqui escreve na pasta do titulo, e o
// `IFileMgr` recusa os modos que mudam o ficheiro (ver `core/brew/arquivo.h`).

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core/carga/aez.h"
#include "core/carga/pack.h"
#include "core/carga/pakz.h"

namespace zb2 {

class Traco;

namespace brew {

// Sem conteudo lido: o pacote foi recusado e nao tem entradas nenhumas.
constexpr std::size_t kPacoteRecusado = static_cast<std::size_t>(-1);

class Vfs {
 public:
  // Um pacote lido da pasta. `motivo` vazio = entrou no indice.
  struct PacoteRegistado {
    std::string ficheiro;
    bool pakz = false;  // `.pakz` (LZMA) em vez de `.pkg` (zlib)
    bool aez = false;   // `.aez` (registos gzip) em vez de `.pkg`/`.pakz`
    std::size_t entradas = 0;
    std::string motivo;
    // Os indices do conteudo lido, ou `kPacoteRecusado`. Os dois indices existem
    // porque nao sao o mesmo numero: esta lista tem os recusados e o conteudo
    // nao (ver `Origem`) -- e sao dois porque os dois recipientes vivem em
    // vectores separados (`conteudo_` para `.pkg`, `conteudo_pakz_` para
    // `.pakz`).
    std::size_t indice_no_conteudo = kPacoteRecusado;       // em `conteudo_`
    std::size_t indice_no_conteudo_pakz = kPacoteRecusado;  // em `conteudo_pakz_`
    std::size_t indice_no_conteudo_aez = kPacoteRecusado;   // em `conteudo_aez_`
  };

  // Regista o conteudo de uma pasta: os ficheiros SOLTOS (so os nomes
  // normalizados, que e o que o `Test`, o `OpenFile` e o `EnumInit` precisam de
  // saber) e os PACOTES `.pkg` e `.pakz`, que sao lidos e indexados aqui.
  //
  // Le a pasta uma so vez e substitui o que estava: registar duas pastas na
  // mesma `Vfs` nao acumula -- e uma pasta por titulo, como a corrida.
  void Registar(const std::string& pasta);

  bool Existe(const std::string& caminho) const;
  std::size_t Quantos() const { return nomes_.size(); }
  const std::set<std::string>& Nomes() const { return nomes_; }

  // A normalizacao das rotas do BREW, num so sitio: barras invertidas viram
  // normais, barras repetidas colapsam, e `..` NAO sai da pasta do modulo.
  //
  // Devolve vazio quando o nome nao corresponde a nada. UMA SO REGRA, para nao
  // haver duas verdades sobre que ficheiro existe -- foi por isso que o
  // `OpenFile` e o `Test` passaram a partilhar esta funcao.
  //
  // Devolve o nome do ficheiro solto, ou `<dir>/<nome>` quando o pedido e
  // servido por um pacote.
  std::string Normalizar(const std::string& bruto) const;

  // Le o ficheiro virtual: solto na pasta, ou uma entrada de um `.pkg`/`.pakz`/
  // `.aez` (que e descomprimida). Recusa com motivo quando nao existe ou quando
  // o pacote recusa a entrada.
  bool Ler(const std::string& caminho, std::vector<std::uint8_t>* bytes, std::string* motivo) const;

  // --- o que veio dos pacotes, para o Traco e para os testes ---
  const std::string& Pasta() const { return pasta_; }
  const std::vector<PacoteRegistado>& Pacotes() const { return pacotes_; }
  const std::set<std::string>& DiretoriosServidos() const { return diretorios_; }
  // Quantos nomes distintos os pacotes trazem (a uniao).
  std::size_t NomesNosPacotes() const { return nomes_de_entrada_.size(); }
  // Quantos caminhos `<dir>/<nome>` a uniao dos pacotes serve.
  std::size_t CaminhosServidos() const { return alias_.size(); }
  // De que pacote (indice em `Pacotes()`) e de que entrada vem este caminho.
  // O pacote sabe dizer qual dos tres recipientes e (`Pacotes()[pacote].pakz`
  // e `Pacotes()[pacote].aez`).
  bool OrigemDe(const std::string& caminho, std::size_t* pacote, std::size_t* entrada) const;


  // Emite no Traco a regra que ligou os pacotes. IDEMPOTENTE: a declaracao sai
  // uma vez, mesmo que isto seja chamado em todos os titulos.
  void DeclararNoTraco(Traco* traco);

 private:
  // O caminho canonico `<dir>/<nome>` quando um pacote serve este pedido, ou
  // vazio. Aceita e retira os prefixos `roms/` e `roms/neogeo/`.
  std::string CaminhoDePacote(const std::string& limpo) const;

  // O nome REAL de um ficheiro solto da pasta, com a caixa dele, ou vazio. A
  // caixa exata ganha (a 1.a tentativa e o `nomes_`); so depois a caixa
  // ignorada -- ver `Vfs::Normalizar`.
  std::string NomeReal(const std::string& limpo) const;

  std::string pasta_;
  std::set<std::string> nomes_;
  // `nomes_` com a caixa ignorada: minusculas -> o nome REAL. Existe porque a
  // pasta do titulo e um sistema de ficheiros de CONSOLA (FAT, insensivel a
  // caixa) e o guest pede `/GalaxyOnFire1_Won.mp3` a um ficheiro que em Linux
  // se chama `galaxyonfire1_won.mp3` -- medido no gof, 7 pedidos recusados so
  // por isso. Quando dois nomes so diferem na caixa, ganha o PRIMEIRO da ordem
  // do `std::set` (deterministico por construcao, como o resto da VFS).
  std::map<std::string, std::string> nomes_por_caixa_;
  std::vector<PacoteRegistado> pacotes_;
  std::vector<Pacote> conteudo_;      // os `.pkg` que parsearam
  std::vector<Pakz> conteudo_pakz_;   // os `.pakz` que parsearam
  std::vector<Aez> conteudo_aez_;     // os `.aez` que parsearam
  // De onde vem um caminho servido por um pacote.
  //
  // OS DOIS INDICES NAO SAO O MESMO NUMERO, e confundi-los foi um defeito real
  // deste codigo: `pacotes_` inclui os pacotes RECUSADOS e `conteudo_` so tem os
  // que parsearam, logo `conteudo_[p]` para um indice de `pacotes_` le um
  // `Pacote` que nao existe. O sintoma foi um `std::bad_alloc` dentro do
  // `Registar` (uma entrada inventada com um nome de megabytes) -- o tipo de
  // erro que uma excecao a escapar transforma em processo morto. Com os dois
  // indices guardados e NOMEADOS, nao ha o que trocar.
  struct Origem {
    std::size_t pacote = 0;        // indice em `Pacotes()`
    std::size_t conteudo = 0;      // indice em `conteudo_` (.pkg)
    std::size_t conteudo_pakz = 0; // indice em `conteudo_pakz_` (.pakz)
    std::size_t conteudo_aez = 0;  // indice em `conteudo_aez_` (.aez)
    bool pakz = false;             // qual dos vectores guarda a entrada
    bool aez = false;              // (e o `pakz` fica falso quando este e)
    std::size_t entrada = 0;
  };
  std::map<std::string, Origem> alias_;
  std::set<std::string> nomes_de_entrada_;
  std::set<std::string> diretorios_;
  bool declarado_ = false;
};

}  // namespace brew

}  // namespace zb2

#endif
