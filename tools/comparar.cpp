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
  // UM CAMPO OPCIONAL E UM CAMPO QUE NASCEU DEPOIS DE UMA REFERENCIA.
  //
  // O campo desconhecido continua a RECUSAR (e a regra que esta ferramenta
  // existe para ter). O que isto permite e o contrario: uma ficha ANTIGA a que
  // falte um campo que a bateria so passou a escrever mais tarde. Sem isto, o
  // primeiro campo novo tornaria ilegivel toda a referencia versionada -- e a
  // referencia e o unico ponto de comparacao que temos.
  //
  // O TRATAMENTO DEPENDE DA CLASSE, e a razao e aritmetica: um mapa ausente
  // vale o mapa VAZIO, e o codigo ja trata "chave ausente" como zero. Um numero
  // ou um booleano ausente nao tem valor natural nenhum, e adivinhar um seria o
  // stub silencioso aplicado ao comparador -- por isso NAO SE COMPARA e o nome
  // do campo vai ao relatorio (ver `nao_comparados`).
  //
  // Ate ao commit das recusas por fase isto so era usado por `kMapaDeContagens`
  // e o codigo punha o mapa vazio em QUALQUER classe. O primeiro campo numerico
  // opcional fazia `jr->tipo != jn->tipo` e a ferramenta recusava a referencia
  // inteira. **Estava certo no proposito e errado na classe.**
  bool opcional = false;
};

const Campo kCampos[] = {
    {"pasta", kIdentidadeComoTexto, kIdentidade,
     "CHAVE do titulo, com o `mod`: e o par que diz que as duas fichas falam do "
     "MESMO titulo. Nao e metrica."},
    {"mod", kIdentidadeComoTexto, kIdentidade, "CHAVE do titulo (ver `pasta`)."},
    {"tamanho", kNumero, kIdentidade,
     "IDENTIDADE DA ENTRADA, e nao metrica de progresso: um `.mod` de outro "
     "tamanho e OUTRO ficheiro, e comparar pixels entre entradas diferentes e "
     "exactamente o erro que esta ferramenta existe para impedir. MEDIDO no "
     "baseline de 940ffef: 0 zeros, min 25776, max 8705936. O campo so passou a "
     "ser utilizavel depois de o `bateria.cpp` voltar a escrever `e.tamanho` "
     "(commit f2c4709; a atribuicao tinha-se perdido em d75281d sem nada acusar). "
     "Antes disso isto era kNeutro, porque estar a zero nao discrimina nada."},
    {"carga", kBooleano, kMaiorMelhor,
     "P3: e o primeiro degrau medido, e `true` so acontece se o MOD foi lido e "
     "mapeado. Referencia: 62 de 62."},
    {"modulo", kBooleano, kMaiorMelhor,
     "`AEEMod_Load` devolveu ponteiro de modulo nao nulo. Referencia: 48 de 62. "
     "Um titulo que perde isto PAROU mais cedo -- e regressao por definicao."},
    {"vtable", kBooleano, kMaiorMelhor,
     "a vtable do modulo tem 4 slots dentro do modulo. MEDIDO: 48 de 62 no "
     "baseline de 940ffef, a COINCIDIR com os 48 `modulo` -- a concordancia entre "
     "os dois degraus e a verificacao que faltava. Antes de f2c4709 este campo "
     "dava 0 de 62 porque comparava com `e.tamanho`, que estava a zero: um degrau "
     "do arranque MORTO, que se lia como 'os modulos nao tem vtable'."},
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
    // kNEUTRO PELO MESMO MOTIVO DO CONTADOR DE PASSOS: um pico de heap nao tem
    // direcao. Um pico MAIOR pode ser um jogo que chegou mais longe (carregou
    // mais recursos) ou um jogo que passou a alocar mal -- as duas leem-se no
    // mesmo numero, e por isso ele nao julga. O que ele responde e a pergunta que
    // nao tinha resposta nesta arvore: "este titulo caberia no heap de 64 MiB?".
    //
    // MEDIDO na corrida `corrida_heap2` (a referencia): 62 de 62 com valor, 0 a
    // zero, min 448, max 47 502 760 = 45,3 MiB no `gof` -- 71% dos 64 MiB do
    // heap. Os cinco maiores: gof 45,3, pbc 32,7, magdrop3 27,6, footparty 23,0,
    // activitycenter 23,0 MiB. NENHUM titulo registra falta de memoria.
    {"heap_pico", kNumero, kNeutro,
     "O maximo que o alocador chegou a ter alocado (um pico nao desce: o `Free` "
     "nao lhe toca). MEDIDO: 62 de 62 com valor, max 47 502 760 (45,3 MiB de 64 "
     "MiB, o `gof`). Sem direcao: mede se o titulo CABE, e nao se andou mais."},
    {"heap_blocos", kNumero, kNeutro,
     "Quantos blocos o alocador tem vivos no fim. MEDIDO: 1 em 62 de 62 -- "
     "nenhum titulo desta corrida chega ao fim com memoria pendurada. Um valor "
     "que suba acusa fuga; um que caia nao e progresso."},
    // kNEUTRO, e nao kMaiorMelhor. **Um contador de instrucoes executadas NAO TEM
    // DIRECCAO.**
    //
    // MEDIDO, e foram DOIS sub-agentes independentes que o apanharam:
    // servindo os 3 pedidos do `IRootForm` ao `tectoy`, o caminho dele ENCURTA
    // (300 -> 244 passos, porque deixa de andar pelo ramo de erro); e o `pbc` com a
    // cablagem do GL passa de 4000000 para 375. Nos dois casos o campo estava
    // declarado `[maior melhor]` e o juiz chamava REGRESSAO ao progresso.
    //
    // E a classe de defeito que o LEDGER ja descrevia: *"declarar um deles como
    // criterio transforma progresso em falha"*. Correr mais passos pode ser chegar
    // mais longe OU andar em circulos; o numero sozinho nao distingue. Quem
    // distingue sao os degraus (`applet`, `vtable`, `pixels`), esses com direcao.
    //
    // O campo continua a ser medido e reportado -- so nao julga.
    {"passos_start", kNumero, kNeutro,
     "PASSOS DA FASE DO `EVT_APP_START` -- o ciclo de vida do app a arrancar. "
     "MEDIDO: 0 em 62 titulos ate ao commit b42043e, porque a bateria criava o "
     "applet e PARAVA; **41 titulos tinha um applet nao nulo e nenhum arrancava o "
     "app** -- a lista de demanda ficava quase vazia (4 itens) porque os jogos nunca "
     "chegavam a pedir nada. Depois de a fase existir: 35 de 62 com `passos_start > "
     "0`, e a demanda passou de 4 para 11 itens. Maior e melhor: um titulo que corre "
     "mais passos aqui chegou mais longe. **Este campo e a diferenca entre medir o "
     "`CreateInstance` e medir a aplicacao.**"},
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
     "transformar progresso em falha (o guarda (c) da etapa 9).\n"
     "DESDE ESTE COMMIT o campo e a SOMA DAS TRES FASES. Antes era so a do "
     "`CreateInstance`: `ArmInterpreter::Repor` zera o contador e a bateria "
     "lia-o em absoluto depois de cada fase, logo a carga era apagada pelo "
     "`Repor` do create e o `EVT_APP_START` nunca era lido. Uma corrida ANTIGA "
     "e uma corrida NOVA nao medem a mesma coisa neste campo -- e mais uma "
     "razao para ele nao julgar."},
    // AS PARCELAS POR FASE. OPCIONAIS: as corridas anteriores ao commit que as
    // criou nao as tem, e recusa-las seria perder a referencia (o mesmo
    // tratamento que o campo `pressupostos` recebeu). NEUTRAS: sao a mesma
    // grandeza do total, so repartida.
    //
    // Existem para que a soma `recusadas` seja VERIFICAVEL de fora do
    // instrumento: `recusadas == carga + create + start + quadros`.
    {"recusadas_carga", kNumero, kNeutro,
     "PARCELA: recusas da fase de CARGA (o ponto de entrada do modulo). Era "
     "esta a parcela apagada pelo `cpu.Repor` do `CreateInstance`.",
     /*opcional=*/true},
    {"recusadas_create", kNumero, kNeutro,
     "PARCELA: recusas do `IModule::CreateInstance`. Era a UNICA que o campo "
     "`recusadas` publicava antes deste commit.",
     /*opcional=*/true},
    {"recusadas_start", kNumero, kNeutro,
     "PARCELA: recusas da fase do `EVT_APP_START` -- a fase onde os titulos "
     "fazem o trabalho de arranque. Nunca era lida.",
     /*opcional=*/true},
    {"recusadas_quadros", kNumero, kNeutro,
     "PARCELA: recusas do laco de quadro (`ZB2_QUADROS`). Zero quando o laco "
     "nao corre.",
     /*opcional=*/true},
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
    {"pressupostos", kMapaDeContagens, kNeutro,
     "OS VALORES DECLARADOS, contados por nome -- a outra metade da divida. Um "
     "caminho que responde um valor que nao mediu (o VID/PID do comando, o "
     "sinal do radio, o espaco livre) nao entra nas `faltas`, e ate este commit "
     "nao entrava em lado nenhum: era `Nivel::Informacao` e morria no traco. "
     "NEUTRO pela mesma medicao das `faltas`: quem anda mais longe encontra "
     "MAIS caminhos declarados, e a contagem SOBE sem que nada tenha piorado. "
     "Cada chave e comparada e as que nascem sao REPORTADAS com o nome. "
     "OPCIONAL: as corridas anteriores ao commit que criou o campo nao o tem, e "
     "recusa-las seria perder a referencia.",
     /*opcional=*/true},
};

constexpr std::size_t kNCampos = sizeof(kCampos) / sizeof(kCampos[0]);

// ---------------------------------------------------------------------------
// 3. O CABECALHO DE PROVENIENCIA.
//
// O `bateria.cpp` escreve, desde o commit cfb031e, um objecto de topo com duas
// chaves -- e nao mais uma lista nua:
//
//   {"config": {"corpus_sha256": "<64 hex>", "titulos": 62, "build": "<git>"},
//    "titulos": [ ...as fichas, sem uma mudanca... ]}
//
// PORQUE ISTO EXISTE: a guarda de configuracao abaixo deriva a identidade da
// corrida da lista de `pasta/mod`, e **dois corpus diferentes com os mesmos 62
// pasta/mod eram indistinguiveis** -- duas dumps da mesma ROM dao o mesmo corpus
// e bytes diferentes. O resumo do corpus fecha esse buraco.
//
// As tres chaves sao DECLARADAS aqui, pela mesma razao das fichas: um campo novo
// no cabecalho tem de ser declarado, e nao ignorado. A direcao e kIdentidade
// (diferenca -> RECUSA, codigo 3) ou kNeutro (diferenca -> REPORTADA, nunca falha).
const Campo kCabecalho[] = {
    {"corpus_sha256", kTexto, kIdentidade,
     "resumo SHA-256 dos BYTES do ficheiro do corpus. Duas corridas com o mesmo "
     "resumo correram a MESMA especificacao; com resumos diferentes correram "
     "coisas diferentes, e compara-las seria o erro que esta ferramenta impede. "
     "O valor literal `desconhecido` (a bateria nao conseguiu ler o corpus) NAO e "
     "proveniencia: e reportado como ausencia, e nunca como identidade."},
    {"titulos", kNumero, kIdentidade,
     "a contagem de titulos, no cabecalho DE PROPOSITO, redundante com a lista. "
     "Um cabecalho que contradiga o corpo e uma mentira do instrumento, e recusa "
     "como formato (codigo 4) -- nao se compara um ficheiro que se contradiz."},
    {"build", kTexto, kNeutro,
     "o commit que construiu o binario, vindo do ambiente (`ZB2_BUILD`). E NEUTRO "
     "e so REPORTADO, e essa e a decisao que faz a ferramenta servir: comparar "
     "DUAS VERSOES DO EMULADOR e o uso normal -- se o `build` fosse criterio, o "
     "comparador recusaria precisamente a comparacao para que existe. Sem a "
     "variavel o valor e `desconhecido`, que e uma resposta honesta."},
    {"binario_sha256", kTexto, kNeutro,
     "resumo SHA-256 do EXECUTAVEL que produziu os numeros. Existe porque o `build` "
     "MENTE: MEDIDO pela auditoria do plano, a referencia dizia `build=eb62459` e os "
     "dados so podiam vir do codigo de `0286921` -- a corrida foi feita com a arvore "
     "SUJA e o campo guardou o commit do checkout. **Um campo que nao identifica o que "
     "mediu nao serve como proveniencia.** O hash do binario identifica-o: um binario "
     "diferente da outro hash, e o mesmo binario da sempre o mesmo. E NEUTRO pela mesma "
     "razao que o `build`: comparar duas versoes do emulador e o uso normal."},
};
constexpr std::size_t kNCabecalho = sizeof(kCabecalho) / sizeof(kCabecalho[0]);

const Campo* AcharCampoDoCabecalho(const std::string& nome) {
  for (const Campo& c : kCabecalho) {
    if (nome == c.nome) return &c;
  }
  return nullptr;
}

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
  // Proveniencia. `tem_cabecalho` distingue a forma nova da lista nua antiga: uma
  // corrida com cabecalho e outra sem NAO sao comparaveis (recusa 3), porque de
  // uma delas nao se sabe QUAL corpus correu.
  bool tem_cabecalho = false;
  std::string corpus_sha256;
  std::string build;
  std::int64_t titulos_declarados = -1;
};

std::string LerTexto(const Json& j, const char* campo) {
  const Json* f = j.Campo(campo);
  if (f == nullptr || f->tipo != Json::kTexto) return {};
  return f->texto;
}

// O JSON so entra se estiver EXACTAMENTE no formato declarado: lista de fichas,
// uma ficha por titulo, e o conjunto de campos igual ao da tabela. Falta e sobra
// sao as duas recusas, com o nome do campo.
std::string CodigoDoTipo(const Json& j);  // definido abaixo; usado na recusa

bool LerCorrida(const Json& raiz, const std::string& nome, Corrida* saida, std::string* erro) {
  saida->nome = nome;
  // As DUAS formas aceites: o objecto com cabecalho (a forma escrita pelo
  // `bateria.cpp` desde cfb031e) e a lista nua (a forma antiga, aceite para as
  // corridas ja guardadas). Qualquer outra coisa recusa.
  const Json* lista = &raiz;
  if (raiz.tipo == Json::kObjeto) {
    for (const auto& par : raiz.objeto) {
      if (par.first != "config" && par.first != "titulos") {
        *erro = nome + ": chave desconhecida '" + par.first +
                "' no objecto de topo. A forma declarada tem `config` e `titulos`";
        return false;
      }
    }
    if (raiz.objeto.size() != 2) {
      *erro = nome + ": nem a lista nua de fichas, nem o objecto com `config` e `titulos` (" +
              std::to_string(raiz.objeto.size()) + " chaves no objecto de topo, e a forma declarada "
              "tem 2)";
      return false;
    }
    const Json* cfg = raiz.Campo("config");
    const Json* lst = raiz.Campo("titulos");
    if (cfg == nullptr || cfg->tipo != Json::kObjeto) {
      *erro = nome + ": `config` em falta ou nao e um objecto";
      return false;
    }
    if (lst == nullptr || lst->tipo != Json::kLista) {
      *erro = nome + ": `titulos` em falta ou nao e uma lista de fichas";
      return false;
    }
    for (const auto& par : cfg->objeto) {
      if (AcharCampoDoCabecalho(par.first) == nullptr) {
        *erro = nome + ": campo desconhecido no cabecalho '" + par.first +
               "'. Um campo novo tem de ser DECLARADO na tabela `kCabecalho` de "
               "tools/comparar.cpp, com a direcao e a medicao que a sustenta";
        return false;
      }
    }
    if (cfg->objeto.size() != kNCabecalho) {
      std::string faltam;
      for (const Campo& c : kCabecalho) {
        if (cfg->Campo(c.nome) == nullptr) {
          faltam += std::string(faltam.empty() ? "" : ", ") + c.nome;
        }
      }
      *erro = nome + ": cabecalho com " + std::to_string(cfg->objeto.size()) +
              " campos, e a tabela declara " + std::to_string(kNCabecalho) +
              (faltam.empty() ? "" : ("; faltam: " + faltam));
      return false;
    }
    for (const Campo& c : kCabecalho) {
      const Json* v = cfg->Campo(c.nome);
      const bool tipo_ok =
          (c.classe == kNumero) ? (v->tipo == Json::kNumero) : (v->tipo == Json::kTexto);
      if (!tipo_ok) {
        *erro = nome + ": cabecalho '" + c.nome + "' e " + CodigoDoTipo(*v) +
                " e a tabela declara-o como " + (c.classe == kNumero ? "numero" : "texto");
        return false;
      }
    }
    saida->tem_cabecalho = true;
    saida->corpus_sha256 = cfg->Campo("corpus_sha256")->texto;
    saida->build = cfg->Campo("build")->texto;
    saida->titulos_declarados = cfg->Campo("titulos")->numero;
    if (saida->titulos_declarados < 0) {
      *erro = nome + ": `titulos` negativo no cabecalho";
      return false;
    }
    lista = lst;
  } else if (raiz.tipo != Json::kLista) {
    *erro = nome +
            ": o JSON da bateria e uma LISTA de fichas por titulo, ou um objecto com "
            "`config` e `titulos`";
    return false;
  }
  if (lista->lista.empty()) {
    *erro = nome + ": lista de fichas vazia";
    return false;
  }
  // O cabecalho tem de bater com o corpo. Um ficheiro que se contradiz nao se
  // compara: seria comparar duas coisas diferentes DENTRO da mesma corrida.
  if (saida->tem_cabecalho &&
      saida->titulos_declarados != static_cast<std::int64_t>(lista->lista.size())) {
    *erro = nome + ": o cabecalho declara " + std::to_string(saida->titulos_declarados) +
            " titulos e a lista tem " + std::to_string(lista->lista.size()) +
            ". Corpo e cabecalho a discordar e uma mentira do instrumento";
    return false;
  }
  for (const Json& item : lista->lista) {
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
    // Cada campo DECLARADO tem de estar na ficha, menos os marcados `opcional`
    // (campos que nasceram depois de uma referencia; ver `struct Campo`). A
    // contagem exacta ja nao serve como prova: uma ficha antiga tem menos campos
    // do que a tabela declara e continua a ser legivel.
    {
      std::string faltam;
      std::size_t declarados_presentes = 0;
      for (const Campo& c : kCampos) {
        if (item.Campo(c.nome) != nullptr) {
          ++declarados_presentes;
        } else if (!c.opcional) {
          faltam += std::string(faltam.empty() ? "" : ", ") + c.nome;
        }
      }
      if (!faltam.empty()) {
        *erro = nome + ": ficha com " + std::to_string(item.objeto.size()) +
                " campos, e a tabela declara " + std::to_string(kNCampos) +
                "; faltam: " + faltam;
        return false;
      }
      // O analisador ja recusa chave repetida e campo desconhecido, logo
      // "todos os declarados presentes" e "nao ha mais nada" fecham a forma.
      if (declarados_presentes != item.objeto.size()) {
        *erro = nome + ": ficha com " + std::to_string(item.objeto.size()) +
                " campos e " + std::to_string(declarados_presentes) + " declarados";
        return false;
      }
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

  // Cada mudanca leva o NOME DO CAMPO alem da linha: e o que permite resumir a
  // lista por campo ("applet 21") em vez de obrigar a contar a mao. Com a
  // degradacao deliberada do `applet` a lista de neutros deu 63 linhas -- e um
  // relatorio de 63 linhas em que 21 sao a mesma coisa deixa de ser lido.
  std::vector<Mudanca> regressoes;
  std::vector<Mudanca> melhorias;
  std::vector<Mudanca> neutros;
  // Titulos cuja ENTRADA mudou (hoje: o `tamanho` do `.mod`). Nao sao regressoes
  // nem melhorias: sao a prova de que as duas corridas leram ficheiros diferentes,
  // e nesse caso nenhum numero dos dois lados e comparavel.
  std::vector<std::string> entradas_divergentes;
  // Campos OPCIONAIS nao comparados por faltarem num dos lados (ver abaixo).
  // Um conjunto, e nao uma linha por titulo: sao 62 linhas iguais.
  std::set<std::string> nao_comparados;
  // A direcao declarada acompanha cada linha. Um relatorio que diz "piorou" sem
  // dizer PORQUE aquele campo e um criterio obriga quem o le a ir ao codigo --
  // e e essa ida ao codigo que esta ferramenta existe para evitar.
  const auto marca = [](Direcao d) { return std::string("   [") + NomeDaDirecao(d) + "]"; };

  // -------------------------------------------------------------------------
  // A PROVENIENCIA, ANTES DE QUALQUER NUMERO.
  //
  // Duas corridas comparam-se se correram a MESMA especificacao. A lista de
  // `pasta/mod` (mais abaixo) diz que os titulos sao os mesmos; o resumo do corpus
  // diz que o FICHEIRO do corpus e o mesmo -- e era este o buraco que sobrava.
  // -------------------------------------------------------------------------
  if (cref.tem_cabecalho != cnova.tem_cabecalho) {
    r.codigo = kConfigIncompativel;
    out << "\nRECUSADO: uma corrida tem cabecalho de proveniencia e a outra nao.\n";
    out << "  " << nome_referencia << ": " << (cref.tem_cabecalho ? "COM" : "SEM")
        << " cabecalho\n";
    out << "  " << nome_corrida << ": " << (cnova.tem_cabecalho ? "COM" : "SEM")
        << " cabecalho\n";
    out << "  De uma delas nao se sabe QUAL corpus correu. Regrava a referencia com "
           "`tools/regressao.sh --atualizar`.\n";
    out << "  Nenhum numero foi comparado.\n";
    r.relatorio = out.str();
    return r;
  }
  if (cref.tem_cabecalho) {
    out << "proveniencia da referencia: corpus_sha256=" << cref.corpus_sha256
        << " build=" << cref.build << "\n";
    out << "proveniencia da corrida:    corpus_sha256=" << cnova.corpus_sha256
        << " build=" << cnova.build << "\n";
    // Os valores por nome, para a tabela continuar a ser a UNICA lista de campos:
    // uma segunda lista de nomes divergiria da primeira sem nada acusar.
    const auto valor_texto = [](const Corrida& c, const std::string& campo) -> std::string {
      if (campo == "corpus_sha256") return c.corpus_sha256;
      if (campo == "build") return c.build;
      return std::string();
    };
    const auto valor_numero = [](const Corrida& c, const std::string& campo) -> std::int64_t {
      if (campo == "titulos") return c.titulos_declarados;
      return 0;
    };
    std::vector<std::string> divergencias;
    for (const Campo& c : kCabecalho) {
      if (c.classe == kNumero) {
        const std::int64_t a = valor_numero(cref, c.nome);
        const std::int64_t b = valor_numero(cnova, c.nome);
        if (a == b) continue;
        const std::string linha = std::string("cabecalho.") + c.nome + " " + std::to_string(a) +
                                  " -> " + std::to_string(b);
        if (c.direcao == kIdentidade) divergencias.push_back(linha);
        else neutros.push_back({c.nome, linha + marca(c.direcao)});
        continue;
      }
      const std::string a = valor_texto(cref, c.nome);
      const std::string b = valor_texto(cnova, c.nome);
      if (a == b) continue;
      const std::string linha = std::string("cabecalho.") + c.nome + " " + a + " -> " + b;
      // `desconhecido` NAO e proveniencia: e ausencia dela. Uma ausencia nunca
      // pode RECUSAR (nao saber nao e "ser diferente"), mas tambem nao pode
      // passar calada -- entra na lista de neutros com a palavra dita.
      if (c.direcao == kIdentidade && (a == "desconhecido" || b == "desconhecido")) {
        neutros.push_back({c.nome, linha + "   [SEM PROVENIENCIA: nao e criterio]"});
      } else if (c.direcao == kIdentidade) {
        divergencias.push_back(linha);
      } else {
        neutros.push_back({c.nome, linha + marca(c.direcao)});
      }
    }
    if (!divergencias.empty()) {
      r.codigo = kConfigIncompativel;
      out << "\nRECUSADO: as duas corridas NAO correram a mesma especificacao.\n";
      for (const std::string& l : divergencias) out << "  " << l << "\n";
      out << "  Comparar isto seria comparar entradas diferentes. Nenhum numero foi comparado.\n";
      r.relatorio = out.str();
      return r;
    }
  }

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
  // Onde a comparacao parou por o formato nao bater (tipos diferentes).
  std::string problema_de_formato;

  for (const Ficha& fr : cref.fichas) {
    const Ficha& fn = cnova.fichas[cnova.por_chave[fr.chave]];
    const std::string onde = fr.mod + " (" + fr.pasta + ")";
    for (const Campo& c : kCampos) {
      if (c.classe == kIdentidadeComoTexto) continue;  // a chave; ja e igual
      const Json* jr = fr.dados->Campo(c.nome);
      const Json* jn = fn.dados->Campo(c.nome);
      // UM CAMPO OPCIONAL AUSENTE VALE O MAPA VAZIO. E a mesma regra que ja
      // governa as chaves dentro do mapa ("ausente vale zero"), levada um nivel
      // acima: assim "o campo nasceu" e "a contagem subiu" continuam a ser a
      // MESMA medida, e uma corrida velha compara-se com uma nova sem que o
      // campo novo apareca como regressao.
      static const Json kMapaVazio = [] {
        Json j;
        j.tipo = Json::kObjeto;
        return j;
      }();
      if (c.opcional && (jr == nullptr || jn == nullptr)) {
        if (c.classe == kMapaDeContagens) {
          if (jr == nullptr) jr = &kMapaVazio;
          if (jn == nullptr) jn = &kMapaVazio;
        } else {
          // UM NUMERO (OU BOOLEANO) OPCIONAL AUSENTE NAO SE COMPARA, E DIZ-SE.
          //
          // O `kMapaVazio` so serve o mapa, e a razao esta no `struct Campo`: um
          // mapa ausente VALE o mapa vazio, um numero ausente nao vale zero.
          // Substituir os dois pelo mapa vazio dava outro defeito, e ele e
          // silencioso do pior modo: `jr->tipo != jn->tipo` (objecto contra
          // numero) disparava o `problema_de_formato` e a ferramenta RECUSAVA a
          // referencia inteira no primeiro campo numerico novo -- exactamente o
          // que o `opcional` existe para impedir.
          //
          // Nao se compara, e o nome do campo aparece no relatorio: quem le fica
          // a saber que aquele campo nao foi julgado, em vez de ver um zero
          // inventado. Nenhum campo opcional e criterio (sao todos `kNeutro`),
          // logo isto nao esconde regressao nenhuma.
          nao_comparados.insert(c.nome);
          continue;
        }
      }
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
        // kIdentidade: nao e progresso, e ENTRADA. Um `.mod` de outro tamanho e
        // outro ficheiro, e a corrida nao se compara -- recusa, e nao "melhor" nem
        // "pior". Acumula e decide no fim, para o relatorio dizer TODOS os titulos
        // cuja entrada mudou, e nao so o primeiro.
        if (c.direcao == kIdentidade) {
          entradas_divergentes.push_back(linha);
          continue;
        }
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

  if (!entradas_divergentes.empty()) {
    r.codigo = kConfigIncompativel;
    out << "\nRECUSADO: as duas corridas NAO leram a mesma entrada.\n";
    for (const std::string& l : entradas_divergentes) out << "  " << l << "\n";
    out << "  Um modulo de outro tamanho e OUTRO ficheiro: comparar os numeros dos dois lados\n"
        << "  seria comparar entradas diferentes, que e o erro que esta ferramenta impede.\n"
        << "  Nenhum numero foi comparado. Regrava a referencia com\n"
        << "  `tools/regressao.sh --atualizar` se a midia mudou mesmo.\n";
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

  if (!nao_comparados.empty()) {
    out << "\nCAMPOS OPCIONAIS NAO COMPARADOS (" << nao_comparados.size()
        << ") -- faltam numa das corridas:\n  ";
    bool primeiro = true;
    for (const std::string& n : nao_comparados) {
      out << (primeiro ? "" : ", ") << n;
      primeiro = false;
    }
    out << "\n  Um numero ausente nao vale zero. Nao foram julgados; regrava a "
           "referencia para os passar a comparar.\n";
  }

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
