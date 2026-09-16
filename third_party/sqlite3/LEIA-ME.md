# SQLite (a amalgamacao)

`sqlite3.c` e `sqlite3.h` sao a **amalgamacao** do SQLite, o ficheiro unico que o
projecto publica para ser copiado para dentro de outro projecto.

* **Versao:** 3.50.2 (`SQLITE_VERSION` em `sqlite3.h`).
* **Origem:** o pacote `libsqlite3-sys 0.35.0` do registo do cargo desta maquina
  (`~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/libsqlite3-sys-0.35.0/sqlite3/`),
  que por sua vez traz a amalgamacao publicada em <https://sqlite.org/>.
* **Licenca:** DOMINIO PUBLICO. O proprio cabecalho do ficheiro di-lo:
  *"a legal notice, here is a blessing"* -- o SQLite nao tem licenca nenhuma a
  cumprir e pode ser copiado para dentro de qualquer projecto.
* Os dois ficheiros NAO se editam a mao. Para subir de versao, copia-se de novo.

## Porque existe aqui

O console usava **SQLite mesmo**. O pacote da Z-Wheel traz
`debug_nand/mod/274755/tt_prefs.db` -- 4 KB cujos primeiros bytes sao literalmente
`SQLite format 3` -- e as instrucoes que o modulo carrega em texto sao o dialecto:
`INSERT OR REPLACE`, `COLLATE NOCASE`, JOIN entre `GAMEINFO` e `TITLETEXT`,
`PRAGMA integrity_check`. Ver `core/brew/sql.h` e `/tmp/pesquisa/sqlite.md`.

Reimplementar um subconjunto disto seria reinventar mal o que ja existe pronto e
em dominio publico -- e o subconjunto cresce para sempre: cada instrucao nova do
jogo e uma falta nova.

## Como e compilado

`CMakeLists.txt` (alvo `zb2_sqlite3`), com as flags magras que o despacho precisa:

    SQLITE_THREADSAFE=0        -- um so fio; o despacho corre no fio que o chamou
    SQLITE_OMIT_LOAD_EXTENSION -- nada carrega extensoes
    SQLITE_OMIT_DEPRECATED     -- as APIs retiradas ficam fora
    SQLITE_DEFAULT_MEMSTATUS=0 -- o contador de memoria do proprio SQLite, fora
