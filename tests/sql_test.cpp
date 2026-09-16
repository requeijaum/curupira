// OS TESTES DA PONTE SOBRE SQLITE (`core/brew/sql.h`).
//
// O CASO DE TESTE E O DIALECTO DO PROPRIO MODULO, e nao um SQL inventado aqui. As
// instrucoes abaixo foram EXTRAIDAS do texto do `tectoy.mod`
// (`/home/rafaelfrequiao/nand_strings/274755__tectoy.mod.txt`): sao as que o jogo
// carrega e manda, com os nomes de tabela, colunas e os `%d`/`%s` que ele proprio
// preenche. Um teste com SQL escolhido por nos provaria que o SQLite funciona --
// nao que a PONTE serve o que este jogo pede.
//
// AS FAMILIAS DO CENSO, e onde cada uma estava no modulo:
//   CREATE TABLE   PREFSINFO, DBINFO (3 variantes), ASSETS, GAMEINFO, TITLETEXT,
//                  DLITEMINFO -- o texto exacto, incluindo `DEFAULT DB_VERSION`;
//   INSERT OR REPLACE  DBINFO, PREFSINFO, ASSETS, GAMEINFO, TITLETEXT, DLITEMINFO;
//   SELECT         `SELECT version, subversion FROM DBINFO`,
//                  `SELECT * FROM PREFSINFO %s`,
//                  `SELECT path FROM ASSETS WHERE owner = %d AND type = %d AND
//                   (language = %d OR language = %d)`,
//                  `SELECT * FROM GAMEINFO, TITLETEXT WHERE
//                   GAMEINFO.game_id=TITLETEXT.game_id AND TITLETEXT.lang_id=%d %s`;
//   UPDATE/DELETE  `UPDATE GAMEINFO SET playcount=%d, dt_lastplayed=%d WHERE game_id=%d`,
//                  `DELETE FROM DLITEMINFO WHERE item_id = %d`;
//   PRAGMA/TRANS.  `PRAGMA integrity_check`, os quatro `PRAGMA main.*`,
//                  `PRAGMA legacy_file_format = OFF`, `BEGIN`/`END`/`ROLLBACK`.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/brew/sql.h"

namespace zb2::brew {
namespace {

// --- onde esta a midia (a mesma procura dos outros testes) -------------------
std::string RaizDosMods() {
  std::vector<std::string> raizes;
  if (const char* env = std::getenv("ZB2_MODS")) {
    if (*env != '\0') raizes.push_back(env);
  } else {
    raizes.push_back("/home/rafaelfrequiao/projects/zeebo-lab/games/brew/mod");
    raizes.push_back("/media/rafaelfrequiao/8C5F-19E51/zeebo/ROMs/debug_nand/mod");
  }
  for (const std::string& r : raizes) {
    std::error_code ec;
    if (std::filesystem::exists(r + "/274755/tt_prefs.db", ec)) return r;
  }
  return {};
}

bool Existe(const std::string& caminho) {
  std::error_code ec;
  return std::filesystem::exists(caminho, ec);
}

std::vector<std::uint8_t> LerFicheiro(const std::string& caminho) {
  std::vector<std::uint8_t> v;
  std::ifstream f(caminho, std::ios::binary);
  if (!f) return v;
  f.seekg(0, std::ios::end);
  const std::streamoff n = f.tellg();
  f.seekg(0, std::ios::beg);
  v.resize(static_cast<std::size_t>(n));
  if (n > 0) f.read(reinterpret_cast<char*>(v.data()), n);
  return v;
}

std::string Digest(const std::vector<std::uint8_t>& v) {
  // Um resumo barato e suficiente para o que o teste mede: comparar o CONTEUDO
  // inteiro de ficheiros deste tamanho nao precisa de criptografia, precisa de
  // nao depender do tamanho so.
  std::uint64_t h = 1469598103934665603ull;
  for (std::uint8_t b : v) {
    h ^= b;
    h *= 1099511628211ull;
  }
  char b[32];
  std::snprintf(b, sizeof(b), "%016llx", static_cast<unsigned long long>(h));
  return b;
}

// O QUE O JOGO RECEBEU, na forma do `sqlite3_exec`: uma lista de linhas, cada uma
// com os nomes e os valores JA EM TEXTO (`char *`), com o `NULL` do SQL marcado --
// e nao convertido em cadeia vazia.
struct Linha {
  std::vector<std::string> nomes;
  std::vector<std::string> valores;
  std::vector<bool> nulos;
};
struct Resultado {
  std::vector<Linha> linhas;
  std::size_t chamadas = 0;
  bool parou_o_callback = false;
};

// A FUNCAO DE ENTREGA dos testes: guarda tudo e CONTINUA -- excepto quando o teste
// pede para parar, que e o outro lado do contrato (o `sqlite3_exec` responde
// `SQLITE_ABORT` quando o callback devolve diferente de zero).
PonteSqlite::Entrega Recolher(Resultado* r, std::size_t parar_apos = 0) {
  return [r, parar_apos](const PonteSqlite::Linha& linha) {
    Linha l;
    for (int i = 0; i < linha.colunas; ++i) {
      const char* nome = linha.nomes != nullptr ? linha.nomes[i] : nullptr;
      const char* valor = linha.valores != nullptr ? linha.valores[i] : nullptr;
      l.nomes.push_back(nome != nullptr ? nome : "");
      l.nulos.push_back(valor == nullptr);
      l.valores.push_back(valor != nullptr ? valor : "");
    }
    r->linhas.push_back(l);
    ++r->chamadas;
    if (parar_apos != 0 && r->chamadas >= parar_apos) {
      r->parou_o_callback = true;
      return false;
    }
    return true;
  };
}

// O valor da coluna `coluna` da primeira linha, ou vazio.
std::string Valor(const Resultado& r, const std::string& coluna) {
  if (r.linhas.empty()) return {};
  const Linha& l = r.linhas.front();
  for (std::size_t i = 0; i < l.nomes.size(); ++i) {
    if (l.nomes[i] == coluna) return l.valores[i];
  }
  return {};
}

// --- AS INSTRUCOES DO CENSO, texto por texto --------------------------------
constexpr const char* kCriaPrefs =
    "CREATE TABLE PREFSINFO(name TEXT PRIMARY KEY, strValue TEXT, dwValue INTEGER, flags INTEGER)";
constexpr const char* kCriaDbinfo =
    "CREATE TABLE DBINFO(version INTEGER DEFAULT DB_VERSION, subversion INTEGER "
    "DEFAULT DB_SUBVERSION)";
constexpr const char* kCriaAssets =
    "CREATE TABLE ASSETS(owner INTEGER, dslid INTEGER PRIMARY KEY, type INTEGER, version "
    "INTEGER, path TEXT, language INTEGER, title TEXT, startdate INTEGER, enddate INTEGER)";
constexpr const char* kCriaGameinfo =
    "CREATE TABLE GAMEINFO(game_id INTEGER PRIMARY KEY, class_id INTEGER, playcount INTEGER, "
    "dt_download  INTEGER, dt_lastplayed INTEGER, boxart_path TEXT, flags INTEGER, size INTEGER, "
    "unique(game_id, class_id))";
constexpr const char* kCriaTitletext =
    "CREATE TABLE TITLETEXT(game_id INTEGER, lang_id INTEGER, titletext TEXT, unique(game_id, "
    "lang_id))";
constexpr const char* kCriaDlitem =
    "CREATE TABLE DLITEMINFO(item_id INTEGER PRIMARY KEY, price INTEGER, size INTEGER, titletext "
    "TEXT, boxart_path TEXT, flags INTEGER, upgrade_id INTEGER )";

// ===========================================================================
// 1. A ABERTURA: os `PRAGMA`, as transaccoes e os `CREATE TABLE` do modulo.
// ===========================================================================
TEST(PonteSqlite, AberturaCriaTabelasComoOModuloManda) {
  PonteSqlite db;
  std::string motivo;
  ASSERT_TRUE(db.Abrir("tt_prefs.db", nullptr, &motivo)) << motivo;
  Resultado r;
  // A SEQUENCIA DA ABERTURA, pela ordem em que o modulo a manda (textos medidos):
  // os quatro setters, o `BEGIN TRANSACTION;` e a criacao das duas tabelas do
  // banco de preferencias.
  for (const char* s : {"PRAGMA main.journal_mode = PERSIST;",
                        "PRAGMA main.locking_mode = EXCLUSIVE;",
                        "PRAGMA main.synchronous = FULL;", "PRAGMA legacy_file_format = OFF;",
                        "PRAGMA encoding = \"UTF-16\";", "BEGIN TRANSACTION;", kCriaPrefs,
                        kCriaDbinfo, "INSERT OR REPLACE INTO DBINFO values (1, 0)",
                        "END TRANSACTION;"}) {
    motivo.clear();
    EXPECT_EQ(db.Executar(s, Recolher(&r), &motivo), PonteSqlite::kBom) << s << " -> " << motivo;
  }
  // E a versao volta a ser lida como o jogo a le.
  r = Resultado{};
  ASSERT_EQ(db.Executar("SELECT version, subversion FROM DBINFO", Recolher(&r), &motivo),
            PonteSqlite::kBom) << motivo;
  ASSERT_EQ(r.linhas.size(), 1u);
  EXPECT_EQ(Valor(r, "version"), "1");
  EXPECT_EQ(Valor(r, "subversion"), "0");
  // O MESMO `CREATE` outra vez e erro -- e e assim que o jogo sabe que a base ja
  // foi criada (nao ha aqui ajuda nenhuma nossa; e o SQLite).
  motivo.clear();
  EXPECT_NE(db.Executar(kCriaDbinfo, Recolher(&r), &motivo), PonteSqlite::kBom);
  EXPECT_NE(motivo.find("DBINFO"), std::string::npos) << "a mensagem nomeia a tabela: " << motivo;
}

TEST(PonteSqlite, OsOutrosEsquemasDoModuloCriamSemRecusa) {
  // Os `CREATE TABLE` do catalogo e da fila de descargas, com o texto EXACTO do
  // modulo -- incluindo os `DEFAULT` com macro (`DB_VERSION`, `CACHE_DB_VERSION`),
  // que um analisador a mao teria de imitar.
  PonteSqlite db;
  std::string motivo;
  ASSERT_TRUE(db.Abrir("asset_cache", nullptr, &motivo)) << motivo;
  Resultado r;
  for (const char* s : {kCriaAssets, kCriaDbinfo, kCriaGameinfo, kCriaTitletext, kCriaDlitem}) {
    motivo.clear();
    EXPECT_EQ(db.Executar(s, Recolher(&r), &motivo), PonteSqlite::kBom) << s << " -> " << motivo;
  }
  // E o `PRAGMA integrity_check` responde `ok` sobre este banco recem-criado.
  r = Resultado{};
  ASSERT_EQ(db.Executar("PRAGMA integrity_check", Recolher(&r), &motivo), PonteSqlite::kBom);
  ASSERT_EQ(r.linhas.size(), 1u);
  EXPECT_EQ(r.linhas[0].valores[0], "ok");
}

// ===========================================================================
// 2. A PECA QUE A FRENTE `zwheel` NOMEOU: 4 colunas com chave TEXT.
// ===========================================================================
TEST(PonteSqlite, OCicloDePreferenciasGravaELeDeVolta) {
  // Palavra por palavra do relatorio da frente anterior: *"INSERT sem armazenamento
  // PREFSINFO 8x -- o app grava e volta a ler, e precisa de um `SELECT *` de 4
  // colunas com chave TEXT"*. Aqui esta ele, com os valores de verdade do banco do
  // console (`tt_prefs.db`: `('Initialized', '', 1, 2)`).
  PonteSqlite db;
  std::string motivo;
  ASSERT_TRUE(db.Abrir("tt_prefs.db", nullptr, &motivo)) << motivo;
  Resultado r;
  ASSERT_EQ(db.Executar(kCriaPrefs, Recolher(&r), &motivo), PonteSqlite::kBom) << motivo;
  ASSERT_EQ(db.Executar("INSERT OR REPLACE INTO PREFSINFO values ('Initialized', '', 1, 2)",
                        Recolher(&r), &motivo),
            PonteSqlite::kBom)
      << motivo;
  // A PRIMEIRA ESCRITA NAO CHAMA CALLBACK NENHUM: um `INSERT` nao devolve linhas,
  // e o `sqlite3_exec` so chama o callback por linha de resultado.
  EXPECT_EQ(r.chamadas, 0u) << "um INSERT nao entrega linha";

  r = Resultado{};
  ASSERT_EQ(db.Executar("SELECT * FROM PREFSINFO", Recolher(&r), &motivo), PonteSqlite::kBom)
      << motivo;
  ASSERT_EQ(r.linhas.size(), 1u) << "o que o jogo gravou tem de voltar";
  EXPECT_EQ(r.linhas[0].nomes, (std::vector<std::string>{"name", "strValue", "dwValue", "flags"}));
  EXPECT_EQ(r.linhas[0].valores, (std::vector<std::string>{"Initialized", "", "1", "2"}));
  // O VALOR VAZIO E UMA CADEIA VAZIA, e nao um nulo: e o que esta no banco do
  // console, e a diferenca importa para as contagens.
  EXPECT_FALSE(r.linhas[0].nulos[1]);

  // A SUBSTITUICAO (`OR REPLACE`) e a chave TEXT: gravar a mesma chave duas vezes
  // deixa UMA linha, com o valor novo -- e e assim que o jogo evita o ecra de
  // configuracao inicial a cada abertura.
  r = Resultado{};
  ASSERT_EQ(db.Executar("INSERT OR REPLACE INTO PREFSINFO values ('Initialized', 'Sim', 3, 2)",
                        Recolher(&r), &motivo),
            PonteSqlite::kBom);
  r = Resultado{};
  ASSERT_EQ(db.Executar("SELECT * FROM PREFSINFO WHERE PREFSINFO.name = 'Initialized'",
                        Recolher(&r), &motivo),
            PonteSqlite::kBom);
  ASSERT_EQ(r.linhas.size(), 1u) << "a chave TEXT primaria e do motor, nao nossa";
  EXPECT_EQ(Valor(r, "strValue"), "Sim");
  EXPECT_EQ(Valor(r, "dwValue"), "3");
}

// ===========================================================================
// 3. A LINHA MAIS LARGA: o `ASSETS` do catalogo, nove colunas.
// ===========================================================================
TEST(PonteSqlite, OCatalogoDeNoveColunasChegaInteiro) {
  // O `INSERT OR REPLACE INTO ASSETS values (%d, %d, %d, %d, '%s', %d, '%s', %d, %d)` do
  // modulo, com a PRIMEIRA linha real do `asset_cache` do corpus. O subconjunto a
  // mao entregava QUATRO colunas no maximo; nove era "forma estranha" e recusava.
  PonteSqlite db;
  std::string motivo;
  ASSERT_TRUE(db.Abrir("asset_cache", nullptr, &motivo)) << motivo;
  Resultado r;
  ASSERT_EQ(db.Executar(kCriaAssets, Recolher(&r), &motivo), PonteSqlite::kBom) << motivo;
  ASSERT_EQ(db.Executar("INSERT OR REPLACE INTO ASSETS values (0, 10001, 6, 1, "
                        "'./assets/faq/en/setup.html', 538996325, 'Setup', 0, 0)",
                        Recolher(&r), &motivo),
            PonteSqlite::kBom)
      << motivo;
  r = Resultado{};
  // A consulta do modulo, com os `%d` preenchidos: o `language` e um dos UID de
  // idioma do console (`538996325` = ingles, medido no banco do corpus).
  ASSERT_EQ(db.Executar("SELECT path FROM ASSETS WHERE owner = 0 AND type = 6 AND "
                        "(language = 538996325 OR language = 538997605)",
                        Recolher(&r), &motivo),
            PonteSqlite::kBom)
      << motivo;
  ASSERT_EQ(r.linhas.size(), 1u);
  EXPECT_EQ(r.linhas[0].valores[0], "./assets/faq/en/setup.html");

  r = Resultado{};
  ASSERT_EQ(db.Executar("SELECT * FROM ASSETS", Recolher(&r), &motivo), PonteSqlite::kBom);
  ASSERT_EQ(r.linhas.size(), 1u);
  EXPECT_EQ(r.linhas[0].valores.size(), 9u) << "nove colunas, como o esquema";
  EXPECT_EQ(Valor(r, "title"), "Setup");
  EXPECT_EQ(Valor(r, "dslid"), "10001");
}

// ===========================================================================
// 4. O JOIN com `COLLATE NOCASE` -- a instrucao que faria o subconjunto crescer.
// ===========================================================================
TEST(PonteSqlite, OJoinDoCatalogoComCollateNocaseOrdena) {
  // `SELECT * FROM GAMEINFO, TITLETEXT WHERE GAMEINFO.game_id=TITLETEXT.game_id
  //  AND TITLETEXT.lang_id=%d %s` -- com o `%s` final a ser um dos dois `ORDER BY`
  // do modulo, um deles com `COLLATE NOCASE`. ONZE colunas no resultado.
  PonteSqlite db;
  std::string motivo;
  ASSERT_TRUE(db.Abrir("tt_game_info", nullptr, &motivo)) << motivo;
  Resultado r;
  for (const char* s : {kCriaGameinfo, kCriaTitletext}) {
    ASSERT_EQ(db.Executar(s, Recolher(&r), &motivo), PonteSqlite::kBom) << motivo;
  }
  // OS DOIS TITULOS SAO OS DO BANCO DO CONSOLE, e nao inventados: `FIFA 09` e
  // `Family Pack` (game_id 17996805 e 19231232, os valores reais do
  // `tt_game_info`). Eles servem porque a ordem MUDA com a colacao --
  // `F`+`I` (0x49) vem antes de `F`+`a` (0x61) na ordem binaria, e depois na
  // `NOCASE` -- e e exactamente isso que o `ORDER BY ... COLLATE NOCASE` do modulo
  // pede para a lista de jogos.
  ASSERT_EQ(db.Executar("INSERT OR REPLACE INTO GAMEINFO values (17996805, 17332968, 0, 0, 0, "
                        "'./assets/games/17996805', 2, 8925488)",
                        Recolher(&r), &motivo),
            PonteSqlite::kBom);
  ASSERT_EQ(db.Executar("INSERT OR REPLACE INTO GAMEINFO values (19231232, 17368006, 0, 0, 0, "
                        "'./assets/games/19231232', 2, 22469433)",
                        Recolher(&r), &motivo),
            PonteSqlite::kBom);
  ASSERT_EQ(db.Executar("INSERT OR REPLACE INTO TITLETEXT values (17996805, 538996325, 'FIFA 09')",
                        Recolher(&r), &motivo),
            PonteSqlite::kBom);
  ASSERT_EQ(db.Executar("INSERT OR REPLACE INTO TITLETEXT values (19231232, 538996325, "
                        "'Family Pack')",
                        Recolher(&r), &motivo),
            PonteSqlite::kBom);
  const std::string consulta =
      "SELECT * FROM GAMEINFO, TITLETEXT WHERE GAMEINFO.game_id=TITLETEXT.game_id AND "
      "TITLETEXT.lang_id=538996325 ORDER BY TITLETEXT.titletext ";
  // 1) A ORDEM BINARIA (o que o SQLite faz sem colacao): `FIFA 09` primeiro.
  r = Resultado{};
  ASSERT_EQ(db.Executar(consulta + "COLLATE BINARY ASC", Recolher(&r), &motivo), PonteSqlite::kBom)
      << motivo;
  ASSERT_EQ(r.linhas.size(), 2u);
  EXPECT_EQ(r.linhas[0].valores.size(), 11u) << "8 colunas do GAMEINFO + 3 do TITLETEXT";
  EXPECT_EQ(Valor(r, "titletext"), "FIFA 09");
  // 2) A ORDEM QUE O MODULO PEDE (`COLLATE NOCASE ASC`): `Family Pack` primeiro.
  // E a COLACAO A ORDENAR, e nao um `sort` nosso -- o subconjunto a mao nao tinha
  // `ORDER BY` nenhum, nem `JOIN` nenhum: esta consulta era uma recusa com o texto.
  r = Resultado{};
  ASSERT_EQ(db.Executar(consulta + "COLLATE NOCASE ASC", Recolher(&r), &motivo),
            PonteSqlite::kBom)
      << motivo;
  ASSERT_EQ(r.linhas.size(), 2u);
  EXPECT_EQ(Valor(r, "titletext"), "Family Pack");
}

// ===========================================================================
// 5. `UPDATE` e `DELETE` -- as duas instrucoes que a fila de descargas usa.
// ===========================================================================
TEST(PonteSqlite, OUpdateEDeleteDaFilaDeDescargas) {
  PonteSqlite db;
  std::string motivo;
  ASSERT_TRUE(db.Abrir("tt_dlqueue.db", nullptr, &motivo)) << motivo;
  Resultado r;
  ASSERT_EQ(db.Executar(kCriaDlitem, Recolher(&r), &motivo), PonteSqlite::kBom) << motivo;
  ASSERT_EQ(db.Executar("INSERT OR REPLACE INTO DLITEMINFO values (77, 1200, 4641248, "
                        "'Alien Breaker', './assets/games/20314112/', 0, 0)",
                        Recolher(&r), &motivo),
            PonteSqlite::kBom);
  ASSERT_EQ(db.Executar("UPDATE DLITEMINFO SET flags=3 WHERE item_id=77", Recolher(&r), &motivo),
            PonteSqlite::kBom)
      << motivo;
  r = Resultado{};
  ASSERT_EQ(db.Executar("SELECT flags FROM DLITEMINFO WHERE item_id = 77", Recolher(&r), &motivo),
            PonteSqlite::kBom);
  ASSERT_EQ(r.linhas.size(), 1u);
  EXPECT_EQ(r.linhas[0].valores[0], "3");
  ASSERT_EQ(db.Executar("DELETE FROM DLITEMINFO WHERE item_id = 77", Recolher(&r), &motivo),
            PonteSqlite::kBom);
  r = Resultado{};
  ASSERT_EQ(db.Executar("SELECT * FROM DLITEMINFO", Recolher(&r), &motivo), PonteSqlite::kBom);
  EXPECT_TRUE(r.linhas.empty()) << "o DELETE do modulo apaga a linha mesmo";
}

// ===========================================================================
// 6. O CONTRATO do `sqlite3_exec`: o callback que devolve diferente de zero PARA.
// ===========================================================================
TEST(PonteSqlite, OCallbackQueDevolveNaoZeroParaAInstrucao) {
  PonteSqlite db;
  std::string motivo;
  ASSERT_TRUE(db.Abrir("parada.db", nullptr, &motivo)) << motivo;
  Resultado r;
  ASSERT_EQ(db.Executar(kCriaPrefs, Recolher(&r), &motivo), PonteSqlite::kBom);
  for (const char* s : {"INSERT OR REPLACE INTO PREFSINFO values ('a', '', 1, 2)",
                        "INSERT OR REPLACE INTO PREFSINFO values ('b', '', 2, 2)",
                        "INSERT OR REPLACE INTO PREFSINFO values ('c', '', 3, 2)"}) {
    ASSERT_EQ(db.Executar(s, Recolher(&r), &motivo), PonteSqlite::kBom) << motivo;
  }
  Resultado parcial;
  const int rc = db.Executar("SELECT * FROM PREFSINFO", Recolher(&parcial, 1), &motivo);
  EXPECT_TRUE(parcial.parou_o_callback);
  EXPECT_EQ(parcial.chamadas, 1u) << "parou na primeira linha, como o callback pediu";
  // O `SQLITE_ABORT` e a resposta do proprio SQLite a um callback que para -- o
  // motor, e nao uma invencao nossa. O motivo pode vir vazio (o `sqlite3_exec`
  // devolve `sqlite3_errmsg`, que para um aborto nao tem texto nenhum), e por isso
  // o que o teste afirma e o CODIGO e o numero de chamadas.
  EXPECT_NE(rc, PonteSqlite::kBom);
}

TEST(PonteSqlite, AInstrucaoInvalidaViraErroComOMotivoDoMotor) {
  PonteSqlite db;
  std::string motivo;
  ASSERT_TRUE(db.Abrir("erros.db", nullptr, &motivo)) << motivo;
  Resultado r;
  const int rc = db.Executar("SELECT * FROM NAO_EXISTE", Recolher(&r), &motivo);
  EXPECT_NE(rc, PonteSqlite::kBom);
  EXPECT_NE(motivo.find("NAO_EXISTE"), std::string::npos)
      << "a mensagem tem de nomear o que faltou: " << motivo;
  EXPECT_TRUE(r.linhas.empty());
}

// ===========================================================================
// 7. OS BANCOS DE VERDADE DA MIDIA: abrir, integrar e ler o que la esta.
// ===========================================================================
TEST(PonteSqlite, OsBancosReaisDaMidiaAbremELeemOQueTemDentro) {
  const std::string raiz = RaizDosMods();
  if (raiz.empty()) {
    GTEST_SKIP() << "sem a midia do corpus nesta maquina (ZB2_MODS) -- PULAR";
  }
  // OS QUATRO BANCOS do `tectoy`, com os nomes EXACTOS que o modulo pede no
  // `ISQLMgr::Open` -- medidos no traco da frente `zwheel`
  // (`SQLMGR_OPEN "tt_prefs.db"`, `"asset_cache"`, `"tt_game_info"`).
  struct Caso {
    const char* nome;
    const char* tabela;
    int linhas;
  };
  const Caso casos[] = {{"tt_prefs.db", "PREFSINFO", 19},
                        {"asset_cache", "ASSETS", 114},
                        {"tt_game_info", "GAMEINFO", 59},
                        {"tt_dlqueue.db", "DLITEMINFO", 0}};
  for (const Caso& c : casos) {
    const std::string caminho = raiz + "/274755/" + c.nome;
    if (!Existe(caminho)) GTEST_SKIP() << "sem " << caminho << " -- PULAR";
    const std::vector<std::uint8_t> semente = LerFicheiro(caminho);
    ASSERT_FALSE(semente.empty()) << caminho;
    PonteSqlite db;
    std::string motivo;
    ASSERT_TRUE(db.Abrir(c.nome, &semente, &motivo)) << c.nome << " -> " << motivo;
    Resultado r;
    ASSERT_EQ(db.Executar("PRAGMA integrity_check", Recolher(&r), &motivo), PonteSqlite::kBom)
        << motivo;
    ASSERT_EQ(r.linhas.size(), 1u);
    EXPECT_EQ(r.linhas[0].valores[0], "ok") << c.nome;
    // A CONTAGEM REAL de linhas da tabela do banco do console: se o ficheiro foi
    // copiado inteiro e aberto pelo motor, o numero tem de ser o do corpus.
    r = Resultado{};
    ASSERT_EQ(db.Executar(std::string("SELECT * FROM ") + c.tabela, Recolher(&r), &motivo),
              PonteSqlite::kBom)
        << c.tabela << " -> " << motivo;
    EXPECT_EQ(static_cast<int>(r.linhas.size()), c.linhas)
        << c.nome << ": " << c.tabela << " tem " << r.linhas.size() << " linhas, medido "
        << c.linhas;
  }
}

TEST(PonteSqlite, EscreverNoBancoNaoTocaNoFicheiroDaMidia) {
  const std::string raiz = RaizDosMods();
  if (raiz.empty()) {
    GTEST_SKIP() << "sem a midia do corpus nesta maquina (ZB2_MODS) -- PULAR";
  }
  const std::string caminho = raiz + "/274755/tt_prefs.db";
  if (!Existe(caminho)) GTEST_SKIP() << "sem " << caminho << " -- PULAR";
  const std::vector<std::uint8_t> antes = LerFicheiro(caminho);
  ASSERT_FALSE(antes.empty());
  {
    PonteSqlite db;
    std::string motivo;
    ASSERT_TRUE(db.Abrir("tt_prefs.db", &antes, &motivo)) << motivo;
    // O SCRATCH E OUTRO FICHEIRO, e o caminho fica dito -- e o que o despacho
    // publica no traco.
    EXPECT_NE(db.Caminho(), caminho);
    EXPECT_EQ(db.NomeDoJogo(), "tt_prefs.db");
    Resultado r;
    ASSERT_EQ(db.Executar("INSERT OR REPLACE INTO PREFSINFO values ('Novo', 'x', 9, 2)",
                          Recolher(&r), &motivo),
              PonteSqlite::kBom)
        << motivo;
  }
  const std::vector<std::uint8_t> depois = LerFicheiro(caminho);
  EXPECT_EQ(Digest(antes), Digest(depois))
      << "a VFS e so de leitura: o banco da ROM do utilizador NAO pode mudar";
}

TEST(PonteSqlite, OBancoReabreComOQueFoiGravadoAntes) {
  // o jogo abre o MESMO banco mais de uma vez (o `tt_prefs.db` aparece duas vezes
  // no traco) e conta com o que gravou: e o defeito que a frente `zwheel` nomeou.
  // O scratch e por instancia, e a semente copia-se UMA vez -- sem esta regra, a
  // segunda abertura apagaria as preferencias.
  PonteSqlite db;
  std::string motivo;
  ASSERT_TRUE(db.Abrir("tt_prefs.db", nullptr, &motivo)) << motivo;
  Resultado r;
  ASSERT_EQ(db.Executar(kCriaPrefs, Recolher(&r), &motivo), PonteSqlite::kBom);
  ASSERT_EQ(db.Executar("INSERT OR REPLACE INTO PREFSINFO values ('Initialized', '', 1, 2)",
                        Recolher(&r), &motivo),
            PonteSqlite::kBom);
  // Fecha e volta a abrir, como o jogo faz entre as duas fases.
  const std::string caminho = db.Caminho();
  ASSERT_TRUE(db.Abrir("tt_prefs.db", nullptr, &motivo)) << motivo;
  EXPECT_EQ(db.Caminho(), caminho) << "o scratch da instancia e o mesmo";
  r = Resultado{};
  ASSERT_EQ(db.Executar("SELECT * FROM PREFSINFO", Recolher(&r), &motivo), PonteSqlite::kBom)
      << motivo;
  ASSERT_EQ(r.linhas.size(), 1u) << "o que a primeira abertura gravou continua la";
  EXPECT_EQ(Valor(r, "name"), "Initialized");
}

TEST(PonteSqlite, AVersaoDoMotorFicaDitaNoRelatorio) {
  // O relatorio tem de poder dizer QUAL motor correu, e nao "sqlite": a ponte e a
  // implementacao, e a versao dela e um facto da corrida.
  const std::string v = PonteSqlite::Versao();
  EXPECT_FALSE(v.empty());
  EXPECT_EQ(v.rfind("3.", 0), 0u) << "versao medida: " << v;
}

}  // namespace
}  // namespace zb2::brew
