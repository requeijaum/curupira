// Implementacao de `tools/comparar.h`. Ver la o porque de o ficheiro existir.

#include "tools/comparar.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace zb2::comparar {
namespace {

// ---------------------------------------------------------------------------
// 1. O ANALISADOR DE JSON.
//
// Nao ha dependencia externa de JSON na arvore, e nao se acrescenta uma so para
// isto. Mas o analisador e ESTRITO de proposito (P2: o caminho nao entendido
// RECUSA e REGISTA, nunca "devolve sucesso e nao faz nada"). Recusa, com a
// linha, o que nao entender:
//   - texto depois do valor;
//   - numero nao inteiro (a bateria escreve contagens; um `1.5` e outro formato);
//   - chave repetida no mesmo objecto (duas verdades sobre o mesmo campo);
//   - escape que nao existe.
// Um analisador permissivo aqui seria o equivalente JSON do stub silencioso:
// compararia menos do que aquilo que diz ter comparado.
// ---------------------------------------------------------------------------
struct Json {
  enum Tipo { kNulo, kBool, kNumero, kTexto, kObjeto, kLista } tipo = kNulo;
  bool b = false;
  std::int64_t numero = 0;
  std::string texto;
  std::vector<std::pair<std::string, Json>> objeto;
  std::vector<Json> lista;

  const Json* Campo(const std::string& nome) const {
    for (const auto& par : objeto) {
      if (par.first == nome) return &par.second;
    }
    return nullptr;
  }
};

class Analisador {
 public:
  explicit Analisador(const std::string& texto) : t_(texto) {}

  bool Correr(Json* saida, std::string* erro) {
    if (!Valor(saida)) { *erro = erro_; return false; }
    Espacos();
    if (p_ != t_.size()) {
      Falhar("texto depois do valor");
      *erro = erro_;
      return false;
    }
    return true;
  }

 private:
  const std::string& t_;
  std::size_t p_ = 0;
  std::string erro_;

  std::size_t Linha() const {
    std::size_t l = 1;
    for (std::size_t i = 0; i < p_ && i < t_.size(); ++i) {
      if (t_[i] == '\n') ++l;
    }
    return l;
  }
  bool Falhar(const std::string& m) {
    if (erro_.empty()) {
      std::ostringstream o;
      o << "linha " << Linha() << " coluna " << (p_ + 1) << ": " << m;
      erro_ = o.str();
    }
    return false;
  }
  void Espacos() {
    while (p_ < t_.size() && (t_[p_] == ' ' || t_[p_] == '\t' || t_[p_] == '\n' ||
                              t_[p_] == '\r')) {
      ++p_;
    }
  }
  bool Ver(char c) {
    if (p_ < t_.size() && t_[p_] == c) { ++p_; return true; }
    return false;
  }
  bool Espera(char c) {
    if (Ver(c)) return true;
    std::string m = "esperava '";
    m += c;
    m += "'";
    return Falhar(m);
  }
  bool Texto(std::string* out) {
    if (!Espera('"')) return false;
    out->clear();
    while (p_ < t_.size()) {
      const char c = t_[p_++];
      if (c == '"') return true;
      if (c != '\\') {
        if (static_cast<unsigned char>(c) < 0x20) return Falhar("caractere de controle no texto");
        out->push_back(c);
        continue;
      }
      if (p_ >= t_.size()) return Falhar("barra invertida no fim do texto");
      const char e = t_[p_++];
      switch (e) {
        case '"': out->push_back('"'); break;
        case '\\': out->push_back('\\'); break;
        case '/': out->push_back('/'); break;
        case 'b': out->push_back('\b'); break;
        case 'f': out->push_back('\f'); break;
        case 'n': out->push_back('\n'); break;
        case 'r': out->push_back('\r'); break;
        case 't': out->push_back('\t'); break;
        case 'u': {
          unsigned cp = 0;
          for (int i = 0; i < 4; ++i) {
            if (p_ >= t_.size()) return Falhar("escape \\u cortado");
            const char h = t_[p_++];
            int d = -1;
            if (h >= '0' && h <= '9') d = h - '0';
            else if (h >= 'a' && h <= 'f') d = h - 'a' + 10;
            else if (h >= 'A' && h <= 'F') d = h - 'A' + 10;
            if (d < 0) return Falhar("escape \\u nao e hexadecimal");
            cp = cp * 16u + static_cast<unsigned>(d);
          }
          if (cp < 0x80u) {
            out->push_back(static_cast<char>(cp));
          } else if (cp < 0x800u) {
            out->push_back(static_cast<char>(0xC0u | (cp >> 6)));
            out->push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
          } else {
            out->push_back(static_cast<char>(0xE0u | (cp >> 12)));
            out->push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out->push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
          }
          break;
        }
        default: return Falhar("escape desconhecido");
      }
    }
    return Falhar("texto sem fim");
  }
  bool Numero(Json* v) {
    const std::size_t inicio = p_;
    if (Ver('-')) {}
    const std::size_t digitos = p_;
    while (p_ < t_.size() && t_[p_] >= '0' && t_[p_] <= '9') ++p_;
    if (p_ == digitos) return Falhar("numero sem digitos");
    if (p_ < t_.size() && (t_[p_] == '.' || t_[p_] == 'e' || t_[p_] == 'E')) {
      return Falhar("numero nao inteiro: a bateria escreve contagens inteiras");
    }
    std::uint64_t mag = 0;
    for (std::size_t i = digitos; i < p_; ++i) {
      const std::uint64_t d = static_cast<std::uint64_t>(t_[i] - '0');
      if (mag > (0x7FFFFFFFFFFFFFFFull - d) / 10ull) return Falhar("numero fora do intervalo");
      mag = mag * 10ull + d;
    }
    const bool negativo = t_[inicio] == '-';
    v->tipo = Json::kNumero;
    v->numero = negativo ? -static_cast<std::int64_t>(mag) : static_cast<std::int64_t>(mag);
    return true;
  }
  bool Valor(Json* v) {
    Espacos();
    if (p_ >= t_.size()) return Falhar("valor em falta");
    const char c = t_[p_];
    if (c == '{') return Objeto(v);
    if (c == '[') return Lista(v);
    if (c == '"') { v->tipo = Json::kTexto; return Texto(&v->texto); }
    if (t_.compare(p_, 4, "true") == 0) { p_ += 4; v->tipo = Json::kBool; v->b = true; return true; }
    if (t_.compare(p_, 5, "false") == 0) { p_ += 5; v->tipo = Json::kBool; v->b = false; return true; }
    if (t_.compare(p_, 4, "null") == 0) { p_ += 4; v->tipo = Json::kNulo; return true; }
    if (c == '-' || (c >= '0' && c <= '9')) return Numero(v);
    return Falhar("valor inesperado");
  }
  bool Objeto(Json* v) {
    v->tipo = Json::kObjeto;
    if (!Espera('{')) return false;
    Espacos();
    if (Ver('}')) return true;
    while (true) {
      Espacos();
      std::string chave;
      if (!Texto(&chave)) return false;
      if (v->Campo(chave) != nullptr) return Falhar("chave repetida: " + chave);
      Espacos();
      if (!Espera(':')) return false;
      Json filho;
      if (!Valor(&filho)) return false;
      v->objeto.emplace_back(chave, std::move(filho));
      Espacos();
      if (Ver(',')) continue;
      return Espera('}');
    }
  }
  bool Lista(Json* v) {
    v->tipo = Json::kLista;
    if (!Espera('[')) return false;
    Espacos();
    if (Ver(']')) return true;
    while (true) {
      Json filho;
      if (!Valor(&filho)) return false;
      v->lista.push_back(std::move(filho));
      Espacos();
      if (Ver(',')) continue;
      return Espera(']');
    }
  }
};

// ---------------------------------------------------------------------------
// 2. A TABELA DE DIRECOES.
//
// Cada campo do JSON da bateria tem de estar aqui, com a DIRECAO declarada. Um
// campo que nao esteja aqui faz o comparador RECUSAR (P2): um campo novo na
// bateria nao pode entrar em vigor sem que alguem declare o que a sua subida
// quer dizer. A alternativa -- ignorar o campo desconhecido -- e o stub
// silencioso aplicado ao instrumento: o comparador diria "sem regressoes"
// tendo comparado menos do que aquilo que existe.
//
// As tres direcoes:
//   kMaiorMelhor -- o numero subir e melhoria; descer e REGRESSAO.
//   kMenorMelhor -- o numero descer e melhoria; subir e REGRESSAO.
//   kNeutro      -- nao e criterio: uma mudanca e REPORTADA, nunca falha.
// kIdentidade marca os campos que sao a CHAVE do titulo (nao sao metricas).
//
// A MEDICAO QUE SUSTENTA CADA ESCOLHA esta na coluna `porque`. Os numeros citados
// saem da corrida de referencia, medida com:
//   cd <arvore>/src2
//   ./build/zb2_bateria "$corpus" "$mods" /tmp/ref.json
//   == 62 titulos | carga 62 | ponteiro de modulo 48 | applet 22 ==
// e a contagem de titulos com 1 cor, 0 pixels, e os no teto, sai de
//   python3 -c "import json;d=json.load(open('/tmp/ref.json'));..."
// (as expressoes exactas estao citadas campo a campo abaixo).
// ---------------------------------------------------------------------------
enum Classe { kIdentidadeComoTexto, kTexto, kBooleano, kNumero, kMapaDeContagens };
enum Direcao { kNeutro, kMaiorMelhor, kMenorMelhor, kIdentidade };

struct Campo {
  const char* nome;
  Classe classe;
  Direcao direcao;
  const char* porque;
};

const Campo kCampos[] = {
    {"pasta", kIdentidadeComoTexto, kIdentidade,
     "CHAVE do titulo, com o `mod`: e o par que diz que as duas fichas falam do "
     "MESMO titulo. Nao e metrica."},
    {"mod", kIdentidadeComoTexto, kIdentidade, "CHAVE do titulo (ver `pasta`)."},
    {"tamanho", kNumero, kNeutro,
     "MEDIDO: 0 nas 62 fichas da referencia (`max(.[].tamanho) == 0`), porque o "
     "`bateria.cpp` declara `e.tamanho` e nunca o escreve. Com o valor a 0 o "
     "campo nao discrimina nada, logo nao pode ser criterio; quando a bateria o "
     "preencher com o tamanho do `.mod`, a direcao certa passa a ser kIdentidade "
     "(o mesmo titulo tem de ter o mesmo tamanho -- um tamanho diferente e OUTRA "
     "entrada, nao um emulador pior). Ver o relatorio da etapa 9."},
    {"carga", kBooleano, kMaiorMelhor,
     "P3: e o primeiro degrau medido, e `true` so acontece se o MOD foi lido e "
     "mapeado. Referencia: 62 de 62."},
    {"modulo", kBooleano, kMaiorMelhor,
     "`AEEMod_Load` devolveu ponteiro de modulo nao nulo. Referencia: 48 de 62. "
     "Um titulo que perde isto PAROU mais cedo -- e regressao por definicao."},
    {"vtable", kBooleano, kMaiorMelhor,
     "a vtable do modulo tem 4 slots dentro do modulo. Referencia: 0 de 62 -- "
     "SEM VALOR hoje, porque o teste compara com `e.tamanho`, que e 0 (ver "
     "`tamanho`). Declarado a mesma, para nao deixar o campo fora da tabela."},
    {"applet", kBooleano, kMaiorMelhor,
     "`IModule::CreateInstance` escreveu ponteiro nao nulo. Referencia: 22 de 62. "
     "E o marco de ARRANQUE, nao de jogabilidade (a nota de honestidade do "
     "LEDGER): ganhar ou perder este bit e a mudanca de estado mais grosseira "
     "que a bateria mede."},
    {"pixels", kNumero, kMaiorMelhor,
     "MEDIDO: 0 nos 62 titulos da referencia (`max(.[].pixels) == 0`). Escrever "
     "pixel e trabalho de desenho que so existe se o titulo chegou ao desenho, e "
     "por isso maior e melhor; hoje o campo esta INERTE, e o comparador diz isso "
     "em vez de fingir que mede desenho."},
    {"cores", kNumero, kMaiorMelhor,
     "MEDIDO: 1 em 48 titulos e 0 em 14 na referencia (`Counter(.[].cores) == "
     "{1: 48, 0: 14}`). Mais cores distintas e mais desenho. Um titulo que "
     "descia de 1 para 0 perdeu o unico pixel que tinha."},
    {"textos", kNumero, kMaiorMelhor,
     "MEDIDO: soma 0 nos 62 titulos da referencia (`sum(.[].textos) == 0`) -- "
     "nenhum `DrawText` desenhou. Inerte hoje, pela mesma razao que `pixels`."},
    {"blits", kNumero, kMaiorMelhor,
     "MEDIDO: soma 0 nos 62 titulos da referencia (`sum(.[].blits) == 0`). "
     "Inerte hoje."},
    {"passos_carga", kNumero, kNeutro,
     "AMBIGUO, por medicao: na referencia ha 29 titulos no TETO de 4000000 "
     "passos e o motivo desses e `orcamento_esgotado` -- ou seja, um numero alto "
     "tanto pode ser 'trabalhou muito' como 'gastou o orcamento sem sair do "
     "sitio'. Um numero menor tambem nao e melhor sozinho: pode ser o titulo a "
     "parar MAIS CEDO (pior) ou trabalho removido (melhor). Um campo cujo "
     "significado depende de outro nao pode ser criterio; fica REPORTADO."},
    {"passos_create", kNumero, kNeutro,
     "AMBIGUO pela mesma medicao (7 titulos no teto na referencia)."},
    {"recusadas", kNumero, kNeutro,
     "AMBIGUO, por medicao: a recusa NAO para a fase. O `activitycenter` tem "
     "444389 instrucoes recusadas e o motivo da paragem e `orcamento_esgotado` "
     "-- o titulo ANDOU 444389 passos que nao sabemos executar. Logo o campo "
     "conta CUSTO, e nao progresso: um titulo que passa a andar mais longe "
     "encontra instrucoes novas e o numero SOBE. Declarado neutro para nao "
     "transformar progresso em falha (o guarda (c) da etapa 9)."},
    {"motivo", kTexto, kNeutro,
     "TEXTO LIVRE: diz PORQUE parou, e existe para um humano ler. Nao e metrica, "
     "logo nao pode ser criterio -- o mesmo progresso pode mudar a frase. Uma "
     "mudanca e reportada."},
    {"faltas", kMapaDeContagens, kNeutro,
     "AMBIGUO e por isso neutro, com o apoio do proprio LEDGER: 'os helpers "
     "entraram e NAO aumentaram os applets -- aprofundaram a demanda: os titulos "
     "que paravam no strlen agora param no IFileMgr'. Quem anda mais longe pede "
     "MAIS slots, e a contagem sobe. NAO se perde informacao: cada chave e "
     "comparada e as chaves que sobem ou nascem sao REPORTADAS com o nome. "
     "Referencia: 4 pedidos em 2 titulos (pacmania 3, zenonia 1)."},
};

constexpr std::size_t kNCampos = sizeof(kCampos) / sizeof(kCampos[0]);

const Campo* AcharCampo(const std::string& nome) {
  for (const Campo& c : kCampos) {
    if (nome == c.nome) return &c;
  }
  return nullptr;
}

// Um titulo visto por uma corrida: a chave (pasta/mod) e o objecto JSON.
struct Ficha {
  std::string chave;
  std::string pasta;
  std::string mod;
  const Json* dados = nullptr;
};

struct Corrida {
  std::string nome;
  std::vector<Ficha> fichas;              // na ordem em que o ficheiro as traz
  std::map<std::string, std::size_t> por_chave;
};

std::string LerTexto(const Json& j, const char* campo) {
  const Json* f = j.Campo(campo);
  if (f == nullptr || f->tipo != Json::kTexto) return {};
  return f->texto;
}

// O JSON so entra se estiver EXACTAMENTE no formato declarado: lista de fichas,
// uma ficha por titulo, e o conjunto de campos igual ao da tabela. Falta e sobra
// sao as duas recusas, com o nome do campo.
bool LerCorrida(const Json& raiz, const std::string& nome, Corrida* saida, std::string* erro) {
  saida->nome = nome;
  if (raiz.tipo != Json::kLista) {
    *erro = nome + ": o JSON da bateria e uma LISTA de fichas por titulo";
    return false;
  }
  if (raiz.lista.empty()) {
    *erro = nome + ": lista de fichas vazia";
    return false;
  }
  for (const Json& item : raiz.lista) {
    if (item.tipo != Json::kObjeto) {
      *erro = nome + ": cada ficha tem de ser um objecto";
      return false;
    }
    for (const auto& par : item.objeto) {
      if (AcharCampo(par.first) == nullptr) {
        *erro = nome + ": campo desconhecido '" + par.first +
               "'. Um campo novo tem de ser DECLARADO na tabela `kCampos` de "
               "tools/comparar.cpp, com a direcao e a medicao que a sustenta";
        return false;
      }
    }
    const std::size_t n = kNCampos;
    if (item.objeto.size() != n) {
      std::string faltam;
      for (const Campo& c : kCampos) {
        if (item.Campo(c.nome) == nullptr) faltam += std::string(faltam.empty() ? "" : ", ") + c.nome;
      }
      *erro = nome + ": ficha com " + std::to_string(item.objeto.size()) + " campos, e a tabela declara " +
              std::to_string(n) + (faltam.empty() ? "" : ("; faltam: " + faltam));
      return false;
    }
    Ficha f;
    f.pasta = LerTexto(item, "pasta");
    f.mod = LerTexto(item, "mod");
    if (f.pasta.empty() || f.mod.empty()) {
      *erro = nome + ": ficha sem 'pasta' ou sem 'mod'";
      return false;
    }
    f.chave = f.pasta + "/" + f.mod;
    f.dados = &item;
    if (saida->por_chave.count(f.chave) != 0) {
      *erro = nome + ": titulo repetido: " + f.chave;
      return false;
    }
    saida->por_chave[f.chave] = saida->fichas.size();
    saida->fichas.push_back(f);
  }
  return true;
}

bool Inteiro(const Json& ficha, const char* campo, std::int64_t* out, std::string* erro,
             const std::string& onde) {
  const Json* j = ficha.Campo(campo);
  if (j == nullptr || j->tipo != Json::kNumero) {
    *erro = onde + ": campo '" + campo + "' nao e um numero inteiro";
    return false;
  }
  if (j->numero < 0) {
    *erro = onde + ": campo '" + campo + "' negativo (" + std::to_string(j->numero) +
           "); a bateria escreve contagens";
    return false;
  }
  *out = j->numero;
  return true;
}

std::string CodigoDoTipo(const Json& j) {
  switch (j.tipo) {
    case Json::kNulo: return "nulo";
    case Json::kBool: return "booleano";
    case Json::kNumero: return "numero";
    case Json::kTexto: return "texto";
    case Json::kObjeto: return "objeto";
    case Json::kLista: return "lista";
  }
  return "?";
}

const char* NomeDaDirecao(Direcao d) {
  switch (d) {
    case kMaiorMelhor: return "maior melhor";
    case kMenorMelhor: return "menor melhor";
    case kNeutro: return "neutro (nao e criterio)";
    case kIdentidade: return "identidade";
  }
  return "?";
}

std::string SimNao(bool b) { return b ? "true" : "false"; }

}  // namespace
}  // namespace zb2::comparar

namespace zb2::comparar {

namespace {

// Uma mudanca medida: o campo da tabela e a linha pronta a imprimir.
struct Mudanca {
  std::string campo;
  std::string linha;
};

// "applet 21, motivo 21" -- quantas linhas de cada campo, da maior para a menor.
std::string ResumoPorCampo(const std::vector<Mudanca>& mudancas) {
  std::map<std::string, std::size_t> contagem;
  for (const Mudanca& m : mudancas) ++contagem[m.campo];
  std::vector<std::pair<std::size_t, std::string>> ordenado;
  for (const auto& par : contagem) ordenado.push_back({par.second, par.first});
  std::sort(ordenado.begin(), ordenado.end(), [](const auto& a, const auto& b) {
    if (a.first != b.first) return a.first > b.first;
    return a.second < b.second;
  });
  std::string s;
  for (const auto& par : ordenado) {
    if (!s.empty()) s += ", ";
    s += par.second + " " + std::to_string(par.first);
  }
  return s.empty() ? std::string("(nenhum)") : s;
}

}  // namespace

Resultado CompararTextos(const std::string& referencia, const std::string& corrida,
                         const std::string& nome_referencia, const std::string& nome_corrida) {
  return CompararTextos(referencia, corrida, nome_referencia, nome_corrida, Opcoes{});
}

Resultado CompararTextos(const std::string& referencia, const std::string& corrida,
                         const std::string& nome_referencia, const std::string& nome_corrida,
                         const Opcoes& opcoes) {
  Resultado r;
  std::ostringstream out;

  Json raiz_referencia;
  Json raiz_corrida;
  std::string erro;
  {
    Analisador a(referencia);
    if (!a.Correr(&raiz_referencia, &erro)) {
      r.codigo = kFormato;
      out << "RECUSADO: " << nome_referencia << " nao e JSON valido (" << erro
          << ")\n. Nada foi comparado.\n";
      r.relatorio = out.str();
      return r;
    }
  }
  {
    Analisador a(corrida);
    if (!a.Correr(&raiz_corrida, &erro)) {
      r.codigo = kFormato;
      out << "RECUSADO: " << nome_corrida << " nao e JSON valido (" << erro
          << ")\n. Nada foi comparado.\n";
      r.relatorio = out.str();
      return r;
    }
  }

  Corrida cref;
  Corrida cnova;
  if (!LerCorrida(raiz_referencia, nome_referencia, &cref, &erro) ||
      !LerCorrida(raiz_corrida, nome_corrida, &cnova, &erro)) {
    r.codigo = kFormato;
    out << "RECUSADO: " << erro << "\n. Nada foi comparado.\n";
    r.relatorio = out.str();
    return r;
  }

  out << "comparar: referencia=" << nome_referencia << " | corrida=" << nome_corrida << "\n";
  out << "titulos: " << cref.fichas.size() << " na referencia, " << cnova.fichas.size()
      << " na corrida\n";

  // -------------------------------------------------------------------------
  // A GUARDA DE CONFIGURACAO. Antes de comparar um numero, verificar que as duas
  // corridas falam da MESMA lista de titulos. Se nao falarem, RECUSA e nao
  // compara: e exactamente o erro que motivou a ferramenta ("comparei numeros de
  // corridas diferentes duas vezes e chamei a um deles regressao").
  // -------------------------------------------------------------------------
  std::vector<std::string> so_na_referencia;
  std::vector<std::string> so_na_corrida;
  for (const Ficha& f : cref.fichas) {
    if (cnova.por_chave.count(f.chave) == 0) so_na_referencia.push_back(f.chave);
  }
  for (const Ficha& f : cnova.fichas) {
    if (cref.por_chave.count(f.chave) == 0) so_na_corrida.push_back(f.chave);
  }
  if (!so_na_referencia.empty() || !so_na_corrida.empty()) {
    r.codigo = kConfigIncompativel;
    out << "\nRECUSADO: as duas corridas NAO sao a mesma configuracao.\n";
    out << "  titulos: " << cref.fichas.size() << " na referencia, " << cnova.fichas.size()
        << " na corrida\n";
    for (const std::string& c : so_na_referencia) {
      out << "  so na referencia: " << c << "\n";
    }
    for (const std::string& c : so_na_corrida) {
      out << "  so na corrida: " << c << "\n";
    }
    out << "  comparar isto seria comparar coisas diferentes. Nenhum numero foi comparado.\n";
    r.relatorio = out.str();
    return r;
  }

  // Campo a campo, titulo a titulo. Tudo o que muda entra em UMA das tres listas:
  // regressao (falha), melhoria, ou mudanca num campo neutro (so reporta).
  // Cada mudanca leva o NOME DO CAMPO alem da linha: e o que permite resumir a
  // lista por campo ("applet 21") em vez de obrigar a contar a mao. Com a
  // degradacao deliberada do `applet` a lista de neutros deu 63 linhas -- e um
  // relatorio de 63 linhas em que 21 sao a mesma coisa deixa de ser lido.
  std::vector<Mudanca> regressoes;
  std::vector<Mudanca> melhorias;
  std::vector<Mudanca> neutros;
  // A direcao declarada acompanha cada linha. Um relatorio que diz "piorou" sem
  // dizer PORQUE aquele campo e um criterio obriga quem o le a ir ao codigo --
  // e e essa ida ao codigo que esta ferramenta existe para evitar.
  const auto marca = [](Direcao d) { return std::string("   [") + NomeDaDirecao(d) + "]"; };
  // Onde a comparacao parou por o formato nao bater (tipos diferentes).
  std::string problema_de_formato;

  for (const Ficha& fr : cref.fichas) {
    const Ficha& fn = cnova.fichas[cnova.por_chave[fr.chave]];
    const std::string onde = fr.mod + " (" + fr.pasta + ")";
    for (const Campo& c : kCampos) {
      if (c.classe == kIdentidadeComoTexto) continue;  // a chave; ja e igual
      const Json* jr = fr.dados->Campo(c.nome);
      const Json* jn = fn.dados->Campo(c.nome);
      if (jr->tipo != jn->tipo) {
        problema_de_formato = onde + ": campo '" + c.nome + "' e " + CodigoDoTipo(*jr) +
                              " na referencia e " + CodigoDoTipo(*jn) + " na corrida";
        break;
      }
      if (c.classe == kBooleano) {
        const bool a = jr->b;
        const bool b = jn->b;
        if (a == b) continue;
        const std::string linha = onde + ": " + c.nome + " " + SimNao(a) + " -> " + SimNao(b);
        if (c.direcao == kMaiorMelhor) {
          if (a && !b) regressoes.push_back({c.nome, linha + marca(c.direcao)});
          else melhorias.push_back({c.nome, linha + marca(c.direcao)});
        } else if (c.direcao == kMenorMelhor) {
          if (b && !a) regressoes.push_back({c.nome, linha + marca(c.direcao)});
          else melhorias.push_back({c.nome, linha + marca(c.direcao)});
        } else {
          neutros.push_back({c.nome, linha + marca(c.direcao)});
        }
        continue;
      }
      if (c.classe == kNumero) {
        std::int64_t a = 0;
        std::int64_t b = 0;
        if (!Inteiro(*fr.dados, c.nome, &a, &erro, onde) ||
            !Inteiro(*fn.dados, c.nome, &b, &erro, onde)) {
          problema_de_formato = erro;
          break;
        }
        if (a == b) continue;
        const std::string linha = onde + ": " + c.nome + " " + std::to_string(a) + " -> " +
                                  std::to_string(b);
        if (c.direcao == kMaiorMelhor) {
          if (b < a) regressoes.push_back({c.nome, linha + marca(c.direcao)});
          else melhorias.push_back({c.nome, linha + marca(c.direcao)});
        } else if (c.direcao == kMenorMelhor) {
          if (b > a) regressoes.push_back({c.nome, linha + marca(c.direcao)});
          else melhorias.push_back({c.nome, linha + marca(c.direcao)});
        } else {
          neutros.push_back({c.nome, linha + marca(c.direcao)});
        }
        continue;
      }
      if (c.classe == kTexto) {
        if (jr->texto == jn->texto) continue;
        neutros.push_back({c.nome, onde + ": " + c.nome + " \"" + jr->texto + "\" -> \"" +
                                       jn->texto + "\"" + marca(c.direcao)});
        continue;
      }
      if (c.classe == kMapaDeContagens) {
        // Uma chave ausente vale ZERO: o `faltas` so escreve o que foi pedido.
        // Assim "a chave nasceu" e "a contagem subiu" sao a MESMA medida, e nao
        // duas regras com duas contas.
        for (const auto& par : jr->objeto) {
          if (par.second.tipo != Json::kNumero || par.second.numero < 0) {
            problema_de_formato = onde + ": '" + c.nome + "." + par.first + "' nao e contagem";
            break;
          }
        }
        if (!problema_de_formato.empty()) break;
        for (const auto& par : jn->objeto) {
          if (par.second.tipo != Json::kNumero || par.second.numero < 0) {
            problema_de_formato = onde + ": '" + c.nome + "." + par.first + "' nao e contagem";
            break;
          }
        }
        if (!problema_de_formato.empty()) break;
        std::set<std::string> chaves;
        for (const auto& par : jr->objeto) chaves.insert(par.first);
        for (const auto& par : jn->objeto) chaves.insert(par.first);
        for (const std::string& k : chaves) {
          std::int64_t a = 0;
          std::int64_t b = 0;
          const Json* va = jr->Campo(k);
          const Json* vb = jn->Campo(k);
          if (va != nullptr) a = va->numero;
          if (vb != nullptr) b = vb->numero;
          if (a == b) continue;
          const std::string linha = onde + ": " + c.nome + "." + k + " " + std::to_string(a) +
                                    " -> " + std::to_string(b);
          if (c.direcao == kMaiorMelhor) {
            if (b < a) regressoes.push_back({c.nome, linha + marca(c.direcao)});
            else melhorias.push_back({c.nome, linha + marca(c.direcao)});
          } else if (c.direcao == kMenorMelhor) {
            if (b > a) regressoes.push_back({c.nome, linha + marca(c.direcao)});
            else melhorias.push_back({c.nome, linha + marca(c.direcao)});
          } else {
            neutros.push_back({c.nome, linha + marca(c.direcao)});
          }
        }
        continue;
      }
    }
    if (!problema_de_formato.empty()) break;
  }

  if (!problema_de_formato.empty()) {
    r.codigo = kFormato;
    out << "\nRECUSADO: " << problema_de_formato
        << "\n  Duas corridas com tipos diferentes no mesmo campo nao sao comparaveis.\n"
        << "  Nada foi concluido sobre regressao.\n";
    r.relatorio = out.str();
    return r;
  }

  // O numero de cabecalho: os degraus de arranque, antes e depois. E o que se
  // le primeiro, e o que a etapa 9 pede ("o numero que piorou") num so sitio.
  out << "\n== degraus medidos (referencia -> corrida) ==\n";
  for (const Campo& c : kCampos) {
    if (c.classe != kBooleano || c.direcao != kMaiorMelhor) continue;
    std::size_t na = 0;
    std::size_t nb = 0;
    for (const Ficha& f : cref.fichas) {
      if (f.dados->Campo(c.nome)->b) ++na;
    }
    for (const Ficha& f : cnova.fichas) {
      if (f.dados->Campo(c.nome)->b) ++nb;
    }
    out << "  " << c.nome << ": " << na << " -> " << nb << "\n";
  }

  out << "\nREGRESSOES (" << regressoes.size() << ")";
  if (!regressoes.empty()) out << "  por campo: " << ResumoPorCampo(regressoes);
  out << ":\n";
  for (const Mudanca& m : regressoes) out << "  " << m.linha << "\n";
  if (regressoes.empty()) out << "  (nenhuma)\n";

  out << "\nMELHORIAS (" << melhorias.size() << ")";
  if (!melhorias.empty()) out << "  por campo: " << ResumoPorCampo(melhorias);
  out << ":\n";
  for (const Mudanca& m : melhorias) out << "  " << m.linha << "\n";
  if (melhorias.empty()) out << "  (nenhuma)\n";

  out << "\nNEUTROS QUE MUDARAM (" << neutros.size() << ")";
  if (!neutros.empty()) out << "  por campo: " << ResumoPorCampo(neutros);
  out << " -- NAO sao falha; a direcao de cada campo esta na tabela `kCampos` de "
         "tools/comparar.cpp:\n";
  if (neutros.empty()) {
    out << "  (nenhum)\n";
  } else if (opcoes.detalhar_neutros) {
    for (const Mudanca& m : neutros) out << "  " << m.linha << "\n";
  } else {
    out << "  (detalhe omitido por --sem-neutros)\n";
  }

  r.codigo = regressoes.empty() ? kSemRegressao : kRegressao;
  out << "\nresultado: " << regressoes.size() << " regressao(oes), " << melhorias.size()
      << " melhoria(s) em " << cref.fichas.size() << " titulos";
  out << (regressoes.empty() ? " -- SEM REGRESSOES\n" : " -- FALHA\n");
  r.relatorio = out.str();
  return r;
}

}  // namespace zb2::comparar
