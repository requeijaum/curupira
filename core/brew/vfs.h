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
// A VFS CONTINUA SO DE LEITURA. Nada aqui escreve na pasta do titulo, e o
// `IFileMgr` recusa os modos que mudam o ficheiro (ver `core/brew/arquivo.h`).

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core/carga/pack.h"

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
    std::size_t entradas = 0;
    std::string motivo;
    // O indice do conteudo lido, ou `kPacoteRecusado`. Os dois indices existem
    // porque nao sao o mesmo numero: esta lista tem os recusados e o conteudo
    // nao (ver `Origem`).
    std::size_t indice_no_conteudo = kPacoteRecusado;
  };

  // Regista o conteudo de uma pasta: os ficheiros SOLTOS (so os nomes
  // normalizados, que e o que o `Test`, o `OpenFile` e o `EnumInit` precisam de
  // saber) e os PACOTES `.pkg`, que sao lidos e indexados aqui.
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

  // Le o ficheiro virtual: solto na pasta, ou uma entrada de um `.pkg` (que e
  // descomprimida). Recusa com motivo quando nao existe ou quando o pacote
  // recusa a entrada.
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
  bool OrigemDe(const std::string& caminho, std::size_t* pacote, std::size_t* entrada) const;

  // Emite no Traco a regra que ligou os pacotes. IDEMPOTENTE: a declaracao sai
  // uma vez, mesmo que isto seja chamado em todos os titulos.
  void DeclararNoTraco(Traco* traco);

 private:
  // O caminho canonico `<dir>/<nome>` quando um pacote serve este pedido, ou
  // vazio. Aceita e retira os prefixos `roms/` e `roms/neogeo/`.
  std::string CaminhoDePacote(const std::string& limpo) const;

  std::string pasta_;
  std::set<std::string> nomes_;
  std::vector<PacoteRegistado> pacotes_;
  std::vector<Pacote> conteudo_;
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
    std::size_t pacote = 0;   // indice em `Pacotes()`
    std::size_t conteudo = 0; // indice em `conteudo()`
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
