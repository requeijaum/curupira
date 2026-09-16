#ifndef ZB2_CORE_BREW_SQL_H
#define ZB2_CORE_BREW_SQL_H

// A PONTE SOBRE SQLITE DE VERDADE -- o `ISQLMgr`/`ISQLDatabase` da Z-Wheel.
//
// ---------------------------------------------------------------------------
// O QUE ISTO E, E PORQUE NAO E UM SUBCONJUNTO A MAO
// ---------------------------------------------------------------------------
//
// O console usava SQLite MESMO, e a prova esta na midia do proprio titulo:
// `debug_nand/mod/274755/tt_prefs.db` tem 4 KB e os primeiros bytes sao
// literalmente `SQLite format 3`. As instrucoes que o modulo carrega em texto sao
// o dialecto do SQLite -- `INSERT OR REPLACE`, `COLLATE NOCASE`, um JOIN entre
// `GAMEINFO` e `TITLETEXT`, `PRAGMA integrity_check` -- e os bancos do catalogo
// (`asset_cache` 11 KB, `tt_game_info` 25 KB, `tt_dlqueue.db` 12 KB) tem esquema
// e conteudo de verdade, com `PRAGMA integrity_check` a responder `ok`.
//
// A frente `zwheel` serviu um SUBCONJUNTO medido (`PRAGMA`/`CREATE`/`INSERT` do
// `DBINFO`/`SELECT` do `DBINFO`) e nomeou a peca seguinte: *"INSERT sem
// armazenamento PREFSINFO 8x -- o app grava e volta a ler, e precisa de um
// `SELECT *` de 4 colunas com chave TEXT"*. Ou seja: o caminho do subconjunto
// cresce para sempre, uma instrucao por vez, e nunca chega. O motor de SQL do
// console existe, esta em dominio publico e vem num ficheiro unico
// (`third_party/sqlite3/`, 3.50.2): **a ponte usa-o, e nao o imita.**
//
// O QUE ESTA CLASSE E: so a PONTE. Abre o banco que o jogo nomeia, executa a
// instrucao que ele manda e entrega as linhas na forma exacta do `sqlite3_exec`
// -- uma chamada ao callback POR LINHA, com os valores e os nomes das colunas ja
// em texto (`char *`). Nao ha aqui conhecimento nenhum do jogo: nem esquema, nem
// nomes de tabela, nem lista de instrucoes.
//
// ---------------------------------------------------------------------------
// ONDE O BANCO E ABERTO (medido, e a decisao esta escrita)
// ---------------------------------------------------------------------------
//
// O jogo nomeia o ficheiro (`tt_prefs.db`, `asset_cache`, `tt_game_info` -- textos
// medidos no `.mod`, `ZB2_TRACE=1` no `tectoy`) e os ficheiros EXISTEM na pasta do
// titulo. Essa pasta e a ROM do utilizador, e a VFS desta arvore e SO DE LEITURA
// por decisao (`core/brew/vfs.h`): um emulador que escreve no `.db` do corpus
// destroi a reprodutibilidade e a midia de quem o corre.
//
// A decisao, e a razao:
//
//   1. o ficheiro do jogo e LIDO pela VFS e COPIADO, na primeira abertura, para
//      um scratch em disco proprio (`<tmp>/zb2_sqlite_<pid>_<n>/<nome>`);
//   2. quem escreve e o SQLite, e escreve no SCRATCH -- o corpus fica intacto;
//   3. o scratch e por INSTANCIA da ponte (por titulo, na bateria) e e apagado no
//      destrutor: a corrida seguinte comeca do mesmo estado, e uma corrida de
//      regressao nao pode depender do que a corrida anterior gravou;
//   4. a copia faz-se UMA vez por nome: o jogo abre o mesmo `tt_prefs.db` duas
//      vezes e conta com o que gravou na primeira -- sem esta regra, a segunda
//      abertura apagaria as preferencias e o defeito seria NOSSO.
//
// A alternativa (abrir em memoria, semeado do ficheiro) foi recusada por uma
// razao medida: o `PRAGMA main.journal_mode = PERSIST` que o proprio modulo manda
// na abertura quer um ficheiro ao lado do banco, e um banco em memoria muda o
// comportamento do motor no sitio onde ele ainda esta a decidir o que fazer.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// O tipo do amalgamado NAO se expoe: quem inclui este cabecalho nunca ve o
// `sqlite3`. Uma cabecalho de terceiros a atravessar a arvore inteira e uma
// dependencia que se espalha por ficheiros que nao tem nada a ver com SQL.
struct sqlite3;

namespace zb2::brew {

class PonteSqlite {
 public:
  // UMA LINHA, com a forma EXACTA que o `sqlite3_exec` entrega ao callback:
  // `valores` e `nomes` sao vectores de `char *` (o `NULL` do SQL e um ponteiro
  // NULO dentro do vector), e `colunas` e o numero de colunas.
  //
  // E por isso um emprestimo (`char**` cru) e nao uma copia: quem recebe so
  // escreve, e a vida dos vectores e a da chamada que a entrega.
  struct Linha {
    int colunas = 0;
    char** valores = nullptr;
    char** nomes = nullptr;
  };

  // `false` = PARAR, que e o contrato do `sqlite3_exec`: um callback que devolve
  // diferente de zero aborta a instrucao (o `sqlite3_exec` responde `SQLITE_ABORT`).
  using Entrega = std::function<bool(const Linha&)>;

  PonteSqlite() = default;
  ~PonteSqlite();
  PonteSqlite(const PonteSqlite&) = delete;
  PonteSqlite& operator=(const PonteSqlite&) = delete;

  // Abre (ou cria) o banco de nome `nome`. `semente` e o conteudo que a VFS
  // encontrou para esse nome (`nullptr` = o ficheiro nao existe na midia e o
  // banco nasce vazio, que e o caso de um console sem nada descarregado).
  //
  // Devolve `false` com o motivo: sem motivo nao ha como saber se faltou o
  // ficheiro, a permissao ou o proprio motor.
  bool Abrir(const std::string& nome, const std::vector<std::uint8_t>* semente, std::string* motivo);

  void Fechar();
  bool Aberto() const { return db_ != nullptr; }

  // ONDE este banco foi realmente aberto (o caminho do scratch). O despacho
  // publica-o no traco: um ficheiro aberto num sitio que ninguem ve e o defeito
  // de instrumento que esta arvore ja pagou mais de uma vez.
  const std::string& Caminho() const { return caminho_; }
  // O nome que o JOGO deu, como ele o deu.
  const std::string& NomeDoJogo() const { return nome_do_jogo_; }

  // O `SQLITE_OK` do motor, sem trazer o cabecalho dele para aqui: `Executar`
  // devolve este valor quando a instrucao correu ate ao fim.
  static constexpr int kBom = 0;

  // Executa a instrucao e entrega as linhas ao callback, uma a uma.
  //
  // Devolve o codigo do SQLite (`0` = `SQLITE_OK`), e o motivo em texto quando o
  // codigo nao e zero. Uma instrucao sem resultado (um `CREATE`, um `INSERT`)
  // simplesmente nao chama o callback -- e o mesmo que o `sqlite3_exec`.
  int Executar(const std::string& sql, const Entrega& entrega, std::string* motivo);

  // A versao do motor que esta compilada aqui. Existe para o traco e para o
  // relatorio poderem dizer QUAL SQLite correu, em vez de "sqlite".
  static const char* Versao();

 private:
  sqlite3* db_ = nullptr;
  std::string pasta_;         // o scratch desta instancia
  std::string caminho_;       // o ficheiro aberto dentro do scratch
  std::string nome_do_jogo_;  // o nome cru que o jogo pediu
};

}  // namespace zb2::brew

#endif
