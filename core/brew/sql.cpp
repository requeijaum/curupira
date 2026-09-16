// A PONTE SOBRE SQLITE -- ver `core/brew/sql.h` para as decisoes (onde o banco e
// aberto, porque nao e um subconjunto a mao, e o que o callback recebe).

#include "core/brew/sql.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <system_error>

// O CAMINHO DO AMALGAMADO E EXPLICITO, e nao um `-I` global: quem depende do
// SQLite e este ficheiro, e um cabecalho de terceiros que entra por uma flag
// passa a estar disponivel para a arvore inteira sem ninguem dar por isso.
#include "third_party/sqlite3/sqlite3.h"

#if defined(_WIN32)
#include <process.h>
#define ZB2_PID _getpid
#else
#include <unistd.h>
#define ZB2_PID getpid
#endif

namespace zb2::brew {
namespace {

// UM CONTADOR POR INSTANCIA, para dois bancos do mesmo titulo (ou duas bancadas de
// teste no mesmo processo) nao partilharem pasta: o scratch e por objecto, e um
// scratch partilhado faria uma corrida depender da anterior.
std::uint64_t ProximaInstancia() {
  static std::uint64_t n = 0;
  return ++n;
}

// O NOME DO FICHEIRO, reduzido a um nome de ficheiro.
//
// O jogo nomeia o banco com um caminho (`tt_prefs.db`, e noutros titulos algo como
// `fs:/~0x01070798/tt_prefs.db`); o scratch e UMA pasta nossa, e um caminho com
// barras dentro dela sairia da pasta -- ou, pior, escreveria num sitio escolhido
// pelo modulo. Fica so o ultimo componente, e cada caracter que nao seja
// `[A-Za-z0-9._-]` vira `_`.
std::string NomeSeguro(const std::string& bruto) {
  std::size_t fim = bruto.size();
  while (fim > 0 && (bruto[fim - 1] == '/' || bruto[fim - 1] == '\\' || bruto[fim - 1] == ':')) --fim;
  const std::size_t corte = bruto.find_last_of("/\\:", fim);
  const std::size_t inicio = (corte == std::string::npos) ? 0 : corte + 1;
  std::string s;
  for (std::size_t k = inicio; k < fim; ++k) {
    const char c = bruto[k];
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                    c == '.' || c == '_' || c == '-';
    s.push_back(ok ? c : '_');
  }
  if (s.empty() || s == "." || s == "..") s = "banco";
  return s;
}

// O CONTEXTO DO TRAMPOLIM. O `sqlite3_exec` fala C: o unico estado que atravessa
// a chamada e este `void *`.
struct Contexto {
  const PonteSqlite::Entrega* entrega = nullptr;
};

// O TRAMPOLIM: uma chamada POR LINHA, com os valores ja em texto -- a forma do
// `sqlite3_exec`. O `1` de retorno aborta a instrucao (o `sqlite3_exec` responde
// `SQLITE_ABORT`), e e o que o contrato manda quando o callback do jogo pede para
// parar.
int Trampolim(void* p, int colunas, char** valores, char** nomes) {
  auto* ctx = static_cast<Contexto*>(p);
  if (ctx == nullptr || ctx->entrega == nullptr) return 0;
  const PonteSqlite::Linha linha{colunas, valores, nomes};
  return (*ctx->entrega)(linha) ? 0 : 1;
}

}  // namespace

PonteSqlite::~PonteSqlite() {
  Fechar();
  // O SCRATCH E NOSSO E DESAPARECE COM O OBJECTO: deixar ficheiros de corridas
  // antigas na pasta temporaria faz a corrida seguinte herdar um estado que
  // ninguem pediu -- e o defeito de metodo que esta arvore ja pagou uma vez.
  if (!pasta_.empty()) {
    std::error_code ec;
    std::filesystem::remove_all(pasta_, ec);
  }
}

const char* PonteSqlite::Versao() { return sqlite3_libversion(); }

bool PonteSqlite::Abrir(const std::string& nome, const std::vector<std::uint8_t>* semente,
                        std::string* motivo) {
  Fechar();
  nome_do_jogo_ = nome;
  if (pasta_.empty()) {
    std::error_code ec;
    char marca[64];
    std::snprintf(marca, sizeof(marca), "zb2_sqlite_%ld_%llu", static_cast<long>(ZB2_PID()),
                  static_cast<unsigned long long>(ProximaInstancia()));
    pasta_ = (std::filesystem::temp_directory_path(ec) / marca).string();
    if (ec) {
      *motivo = "sem pasta temporaria: " + ec.message();
      pasta_.clear();
      return false;
    }
    std::filesystem::create_directories(pasta_, ec);
    if (ec) {
      *motivo = "nao se criou o scratch '" + pasta_ + "': " + ec.message();
      pasta_.clear();
      return false;
    }
  }
  caminho_ = pasta_ + "/" + NomeSeguro(nome);
  if (semente != nullptr && !semente->empty()) {
    // A COPIA DO FICHEIRO DO JOGO, UMA VEZ. O jogo abre o mesmo banco mais de uma
    // vez e le o que gravou na primeira abertura: recopiar aqui apagaria as
    // preferencias e o defeito era NOSSO. O `existe` e o que separa as duas coisas.
    std::error_code ec;
    if (!std::filesystem::exists(caminho_, ec)) {
      std::FILE* f = std::fopen(caminho_.c_str(), "wb");
      if (f == nullptr) {
        *motivo = "nao se escreveu o scratch '" + caminho_ + "'";
        return false;
      }
      const std::size_t escrito = std::fwrite(semente->data(), 1, semente->size(), f);
      std::fclose(f);
      if (escrito != semente->size()) {
        *motivo = "copia incompleta do banco para '" + caminho_ + "'";
        return false;
      }
    }
  }
  const int r = sqlite3_open_v2(caminho_.c_str(), &db_,
                                SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
  if (r != SQLITE_OK) {
    *motivo = db_ != nullptr ? sqlite3_errmsg(db_) : "sqlite3_open_v2 falhou sem mensagem";
    // Um `sqlite3` que nao abriu TEM de ser fechado, senao fica memoria do motor
    // pendurada por causa de uma abertura que falhou.
    if (db_ != nullptr) sqlite3_close(db_);
    db_ = nullptr;
    return false;
  }
  return true;
}

void PonteSqlite::Fechar() {
  if (db_ != nullptr) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
  caminho_.clear();
}

int PonteSqlite::Executar(const std::string& sql, const Entrega& entrega, std::string* motivo) {
  if (db_ == nullptr) {
    *motivo = "nenhum banco aberto";
    return SQLITE_MISUSE;
  }
  Contexto ctx{&entrega};
  char* erro = nullptr;
  const int r = sqlite3_exec(db_, sql.c_str(), Trampolim, &ctx, &erro);
  if (erro != nullptr) {
    *motivo = erro;
    sqlite3_free(erro);
  } else {
    motivo->clear();
  }
  return r;
}

}  // namespace zb2::brew
