// Testes do comparador de corridas (etapa 9 do PLANO).
//
// O que estes testes tem de provar, e o criterio da etapa dito em afirmacoes:
//   (a) um titulo que PIORA faz o comparador falhar, com o numero;
//   (b) uma diferenca de CONFIGURACAO e recusada, e nao comparada;
//   (c) um titulo que MELHORA nao pode falhar;
//   (d) um campo que a tabela nao declara e uma recusa, e nao um silencio;
//   (e) a PROVENIENCIA: dois corpus diferentes com os mesmos 62 pasta/mod nao se
//       comparam (recusa 3), um modulo de outro tamanho tambem nao, e um `build`
//       diferente NAO recusa -- comparar duas versoes do emulador e o uso normal.
//
// Cada um destes testes foi PROVADO POR VIOLACAO: a guarda correspondente em
// tools/comparar.cpp foi quebrada de proposito e o teste ficou VERMELHO. O log
// dessas violacoes esta no relatorio da etapa. Uma guarda que passa sem a
// mudanca nao e guarda.
//
// Os dados sao sinteticos e curtos de proposito: um teste que dependesse do
// corpus de 62 titulos e da midia externa so correria na maquina do autor.
// O corpus real entra na corrida de regressao (`tools/regressao.sh`), nao aqui.

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

#include "tools/comparar.h"

namespace {

using zb2::comparar::CompararTextos;
using zb2::comparar::kConfigIncompativel;
using zb2::comparar::kFormato;
using zb2::comparar::kRegressao;
using zb2::comparar::kSemRegressao;

// Uma ficha com a MESMA forma e a MESMA ordem de campos que o `tools/bateria.cpp`
// escreve. A ordem e a do JSON real (`tools/bateria.cpp`, bloco do `json +=`),
// para que os dados de teste nao sejam um formato paralelo.
struct Ficha {
  std::string pasta = "12875";
  std::string mod = "imicro3d";
  long tamanho = 0;
  bool carga = true;
  bool modulo = true;
  bool vtable = false;
  bool applet = true;
  long passos_carga = 60;
  long passos_create = 340;
  long passos_start = 0;
  long recusadas = 0;
  std::string motivo = "retornou | create:retornou";
  long pixels = 0;
  long cores = 1;
  long textos = 0;
  long blits = 0;
  std::string faltas = "{}";
  // Campo NOVO e OPCIONAL: vazio quer dizer "a ficha nao o tem", que e a forma
  // de todas as corridas anteriores ao commit que o criou.
  std::string pressupostos = "";
};

const char* B(bool v) { return v ? "true" : "false"; }

// O CORPO: a lista de fichas, que e a mesma nas duas formas.
std::string Corpo(const std::vector<Ficha>& fichas) {
  std::ostringstream s;
  s << "[";
  for (std::size_t i = 0; i < fichas.size(); ++i) {
    const Ficha& f = fichas[i];
    if (i != 0) s << ",";
    s << "\n  {\"mod\":\"" << f.mod << "\",\"pasta\":\"" << f.pasta
      << "\",\"tamanho\":" << f.tamanho << ",\"carga\":" << B(f.carga)
      << ",\"modulo\":" << B(f.modulo) << ",\"vtable\":" << B(f.vtable)
      << ",\"applet\":" << B(f.applet) << ",\"passos_carga\":" << f.passos_carga
      << ",\"passos_create\":" << f.passos_create << ",\"passos_start\":" << f.passos_start
      << ",\"recusadas\":" << f.recusadas
      << ",\"motivo\":\"" << f.motivo << "\",\"pixels\":" << f.pixels
      << ",\"cores\":" << f.cores << ",\"textos\":" << f.textos
      << ",\"blits\":" << f.blits << ",\"faltas\":" << f.faltas;
    if (!f.pressupostos.empty()) s << ",\"pressupostos\":" << f.pressupostos;
    s << "}";
  }
  s << "\n]";
  return s.str();
}

// A LISTA NUA, a forma antiga (antes do commit cfb031e): continua aceite, para as
// corridas ja guardadas por ai. Ver `Comparar.CabecalhoIgualNaoFalha` para a forma
// nova -- os testes da lista nua provam que a transicao nao quebrou.
std::string Montar(const std::vector<Ficha>& fichas) { return Corpo(fichas) + "\n"; }

// Duas entradas DIFERENTES, para o teste poder dizer "isto nao e o mesmo corpus"
// sem depender do SHA-256 verdadeiro (que tem vectores proprios em
// tests/sha256_test.cpp). O que se prova aqui e a REACCAO a diferenca.
const char* kShaA =
    "348106f1aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaad8b";
const char* kShaB =
    "348106f1bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbd8b";

// A corrida COM cabecalho de proveniencia, na forma que o `bateria.cpp` escreve
// desde cfb031e. `titulos_do_cabecalho` existe para o teste poder escrever um
// cabecalho que CONTRADIGA a lista (o -1 significa "a contagem certa").
std::string MontarCom(const std::vector<Ficha>& fichas, const std::string& sha = kShaA,
                      const std::string& build = "buildA", long titulos_do_cabecalho = -1,
                      const std::string& binario = "binA") {
  const long n = (titulos_do_cabecalho >= 0) ? titulos_do_cabecalho
                                             : static_cast<long>(fichas.size());
  return "{\n  \"config\": {\"corpus_sha256\": \"" + sha + "\", \"titulos\": " +
         std::to_string(n) + ", \"build\": \"" + build + "\", \"binario_sha256\": \"" +
         binario + "\"},\n  \"titulos\": " + Corpo(fichas) + "\n}\n";
}

std::string Tem(const std::string& texto, const std::string& agulha) {
  return texto.find(agulha) != std::string::npos ? "" : "NAO contem: " + agulha + "\n---\n" + texto;
}

bool Contem(const std::string& texto, const std::string& agulha) {
  return texto.find(agulha) != std::string::npos;
}

}  // namespace

// (0) O caso base: duas corridas iguais. Sem isto, "falha quando piora" poderia
// ser um comparador que falha sempre.
TEST(Comparar, IgualNaoFalha) {
  const std::string doc = Montar({{}});
  const auto r = CompararTextos(doc, doc, "ref.json", "nova.json");
  EXPECT_EQ(r.codigo, kSemRegressao) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "SEM REGRESSOES")) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "REGRESSOES (0)")) << r.relatorio;
}

// (a) GUARDA: um titulo que PIORA tem de falhar, e o numero tem de estar dito.
TEST(Comparar, AppletPerdidoFalha) {
  Ficha antes;
  Ficha depois;
  depois.applet = false;  // 22 -> 21 no corpus real
  const auto r = CompararTextos(Montar({antes}), Montar({depois}), "ref.json", "nova.json");
  EXPECT_EQ(r.codigo, kRegressao) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "REGRESSOES (1)")) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "imicro3d (12875): applet true -> false")) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "FALHA")) << r.relatorio;
}

// (a) outra vez, noutro degrau do arranque, e num campo numerico.
TEST(Comparar, ModuloPerdidoFalha) {
  Ficha antes;
  Ficha depois;
  depois.modulo = false;
  const auto r = CompararTextos(Montar({antes}), Montar({depois}), "a.json", "b.json");
  EXPECT_EQ(r.codigo, kRegressao) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "modulo true -> false")) << r.relatorio;
}

TEST(Comparar, PixelsQueCaemFalham) {
  Ficha antes;
  antes.pixels = 4800;
  antes.cores = 7;
  Ficha depois;
  depois.pixels = 0;
  depois.cores = 1;
  const auto r = CompararTextos(Montar({antes}), Montar({depois}), "a.json", "b.json");
  EXPECT_EQ(r.codigo, kRegressao) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "pixels 4800 -> 0")) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "cores 7 -> 1")) << r.relatorio;
}

// (c) GUARDA: um titulo que MELHORA nao pode falhar.
TEST(Comparar, MelhoriaNaoFalha) {
  Ficha antes;
  antes.applet = false;
  antes.pixels = 0;
  antes.cores = 1;
  Ficha depois;
  depois.applet = true;
  depois.pixels = 10;
  depois.cores = 3;
  const auto r = CompararTextos(Montar({antes}), Montar({depois}), "a.json", "b.json");
  EXPECT_EQ(r.codigo, kSemRegressao) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "REGRESSOES (0)")) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "MELHORIAS (3)")) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "applet false -> true")) << r.relatorio;
}

// (c) e o mesmo com o corpus real: applet 22 -> 23 (o LEDGER mede 1 -> 22).
TEST(Comparar, AppletGanhoNaoFalha) {
  Ficha a;
  a.pasta = "12875";
  a.mod = "imicro3d";
  a.applet = false;   // na referencia dos 62: applet 22 de 62
  Ficha b;
  b.pasta = "276212";
  b.mod = "pacmania";
  b.applet = true;
  Ficha a_depois = a;
  a_depois.applet = true;   // o titulo GANHOU o applet
  const auto r = CompararTextos(Montar({a, b}), Montar({a_depois, b}), "a.json", "b.json");
  EXPECT_EQ(r.codigo, kSemRegressao) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "REGRESSOES (0)")) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "applet: 1 -> 2")) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "applet false -> true")) << r.relatorio;
}

// (b) GUARDA: numero de titulos diferente e RECUSA, e nao uma regressao de quem
// desapareceu. Este e o erro medido no desenho: "comparei numeros de corridas
// diferentes duas vezes e chamei a um deles regressao".
TEST(Comparar, NumeroDeTitulosDiferenteERecusado) {
  Ficha a;
  a.pasta = "12875";
  a.mod = "imicro3d";
  Ficha b;
  b.pasta = "276212";
  b.mod = "pacmania";
  const auto r = CompararTextos(Montar({a, b}), Montar({a}), "ref.json", "nova.json");
  EXPECT_EQ(r.codigo, kConfigIncompativel) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "NAO sao a mesma configuracao")) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "so na referencia: 276212/pacmania")) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "Nenhum numero foi comparado")) << r.relatorio;
  // E a prova negativa: nenhuma secao de resultado pode ter sido impressa.
  EXPECT_FALSE(Contem(r.relatorio, "REGRESSOES")) << r.relatorio;
}

// (b) com a MESMA contagem: o corpus mudou de titulos, e nao de tamanho. Uma
// contagem igual nao e uma configuracao igual.
TEST(Comparar, CorpusDiferenteComMesmaContagemERecusado) {
  Ficha a;
  a.pasta = "12875";
  a.mod = "imicro3d";
  Ficha b;
  b.pasta = "276212";
  b.mod = "outro_titulo";  // mesmo numero de titulos, outro corpus
  const auto r = CompararTextos(Montar({a}), Montar({b}), "ref.json", "nova.json");
  EXPECT_EQ(r.codigo, kConfigIncompativel) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "so na corrida: 276212/outro_titulo")) << r.relatorio;
}

// (d) GUARDA: um campo que a tabela nao declara e RECUSA. Sem isto, um campo
// novo da bateria entraria em vigor sem ninguem declarar o que a sua subida
// quer dizer -- que e o stub silencioso aplicado ao instrumento.
TEST(Comparar, CampoDesconhecidoERecusado) {
  const std::string doc =
      "[{\"mod\":\"m\",\"pasta\":\"1\",\"tamanho\":0,\"carga\":true,\"modulo\":true,"
      "\"vtable\":false,\"applet\":true,\"passos_carga\":1,\"passos_create\":1,"
      "\"passos_start\":0,"
      "\"recusadas\":0,\"motivo\":\"x\",\"pixels\":0,\"cores\":1,\"textos\":0,"
      "\"blits\":0,\"faltas\":{},\"campo_novo_da_bateria\":7}]";
  const auto r = CompararTextos(doc, doc, "a.json", "b.json");
  EXPECT_EQ(r.codigo, kFormato) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "campo desconhecido 'campo_novo_da_bateria'")) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "kCampos")) << r.relatorio;
}

// (d) e o outro lado: um campo declarado que desapareceu do JSON.
TEST(Comparar, CampoEmFaltaERecusado) {
  const std::string doc =
      "[{\"mod\":\"m\",\"pasta\":\"1\",\"tamanho\":0,\"carga\":true,\"modulo\":true,"
      "\"vtable\":false,\"applet\":true,\"passos_carga\":1,\"passos_create\":1,"
      "\"passos_start\":0,"
      "\"recusadas\":0,\"motivo\":\"x\",\"pixels\":0,\"textos\":0,\"blits\":0,"
      "\"faltas\":{}}]";  // sem o campo "cores"
  const auto r = CompararTextos(doc, doc, "a.json", "b.json");
  EXPECT_EQ(r.codigo, kFormato) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "faltam: cores")) << r.relatorio;
}

// O analisador e ESTRITO: duas verdades sobre o mesmo campo nao entram.
TEST(Comparar, ChaveRepetidaERecusada) {
  const std::string doc = "[{\"mod\":\"m\",\"pasta\":\"1\",\"pasta\":\"2\"}]";
  const auto r = CompararTextos(doc, doc, "a.json", "b.json");
  EXPECT_EQ(r.codigo, kFormato) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "chave repetida: pasta")) << r.relatorio;
}

TEST(Comparar, JsonCortadoERecusado) {
  const auto r = CompararTextos("[", "[]", "a.json", "b.json");
  EXPECT_EQ(r.codigo, kFormato) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "a.json")) << r.relatorio;
}

// Um objecto de topo que nao seja a forma declarada (nem a lista nua, nem
// `config` + `titulos`) recusa, e a mensagem diz QUAIS sao as duas formas.
TEST(Comparar, ObjetoForaDaFormaERecusado) {
  const auto r = CompararTextos("{}", "{}", "a.json", "b.json");
  EXPECT_EQ(r.codigo, kFormato) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "lista nua de fichas")) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "`config` e `titulos`")) << r.relatorio;
}

TEST(Comparar, ContagemNegativaERecusada) {
  Ficha a;
  a.faltas = "{\"IShell::slot41\":-1}";
  const auto r = CompararTextos(Montar({a}), Montar({a}), "a.json", "b.json");
  EXPECT_EQ(r.codigo, kFormato) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "nao e contagem")) << r.relatorio;
}

// Os contadores que a tabela declara NEUTROS mudam, e isso nao pode ser falha.
// E o caso medido no corpus: o `activitycenter` gasta o orcamento a atravessar
// 444389 instrucoes recusadas, e o LEDGER registra que mais demanda acompanha
// mais progresso ("os helpers... aprofundaram a demanda").
TEST(Comparar, ContadoresNeutrosNaoFalhamEIndaAssimSaoDitos) {
  Ficha antes;
  Ficha depois;
  depois.passos_carga = 4000000;
  depois.recusadas = 444389;
  depois.motivo = "orcamento_esgotado | create:orcamento_esgotado";
  depois.faltas = "{\"IShell::slot41\":1}";
  const auto r = CompararTextos(Montar({antes}), Montar({depois}), "a.json", "b.json");
  EXPECT_EQ(r.codigo, kSemRegressao) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "REGRESSOES (0)")) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "passos_carga 60 -> 4000000")) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "recusadas 0 -> 444389")) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "faltas.IShell::slot41 0 -> 1")) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "motivo")) << r.relatorio;
}

// O mesmo para as chaves que NASCEM e para as que DESAPARECEM: uma chave ausente
// vale zero, e nao "nao sei".
TEST(Comparar, ChaveDeFaltaQueNasceEDepoisDesaparece) {
  Ficha antes;
  antes.faltas = "{\"IShell::slot41\":3}";
  Ficha depois;  // sem a chave: vale 0
  const auto r = CompararTextos(Montar({antes}), Montar({depois}), "a.json", "b.json");
  EXPECT_EQ(r.codigo, kSemRegressao) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "faltas.IShell::slot41 3 -> 0")) << r.relatorio;
}

// A linha de cabecalho: os degraus medidos, antes e depois. E o numero que a
// etapa 9 pede, num so sitio.
TEST(Comparar, DegrausDaReferenciaEestaoNoRelatorio) {
  Ficha a;
  Ficha b;
  b.pasta = "276212";
  b.mod = "pacmania";
  b.modulo = false;   // a ficha B fica em 1 dos 2, e e assim que se ve a conta
  Ficha b_depois = b;
  b_depois.modulo = true;
  const auto r = CompararTextos(Montar({a, b}), Montar({a, b_depois}), "a.json", "b.json");
  EXPECT_TRUE(Contem(r.relatorio, "carga: 2 -> 2")) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "modulo: 1 -> 2")) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "applet: 2 -> 2")) << r.relatorio;
}

// O CONTRATO DO FORMATO, DOS DOIS LADOS. A lista abaixo e a lista de campos que
// o `tools/bateria.cpp` escreve, transcrita do bloco `json +=` daquele ficheiro
// (**17 campos**, na ordem em que sao escritos). Uma ficha com estes campos tem de
// ser ACEITE; um 18.o campo tem de ser RECUSADO (o teste seguinte prova-o). Se a
// bateria ganhar um campo, e o teste `CampoDesconhecidoERecusado` que diz o nome do
// campo que falta declarar em `kCampos`.
//
// A CONTAGEM E AFIRMADA de proposito. Quando o `passos_start` entrou, este ASSERT
// ficou vermelho -- e foi ele que obrigou a actualizar as fichas de teste e o
// `comparar_guarda.sh` no MESMO commit. **Sem ele, o campo novo entrava na tabela e
// as fichas antigas passavam a ser recusadas em silencio.**
TEST(Comparar, FichaComOsCamposDaBateriaEhAceite) {
  const char* campos[] = {"pasta",         "mod",      "tamanho",  "carga",
                          "modulo",        "vtable",   "applet",   "passos_carga",
                          "passos_create", "passos_start", "recusadas", "motivo", "pixels",
                          "cores",         "textos",   "blits",    "faltas"};
  ASSERT_EQ(sizeof(campos) / sizeof(campos[0]), 17u);
  const std::string doc = Montar({{}});
  for (const char* c : campos) {
    EXPECT_TRUE(Contem(doc, std::string("\"") + c + "\":"))
        << "a ficha de teste nao tem o campo " << c;
  }
  const auto r = CompararTextos(doc, doc, "a.json", "b.json");
  EXPECT_EQ(r.codigo, kSemRegressao) << r.relatorio;
  EXPECT_FALSE(Contem(r.relatorio, "RECUSADO")) << r.relatorio;
}

// A mesma tabela, pelo lado do COMPORTAMENTO: qualquer um dos campos numericos
// declarados como "maior melhor" tem de falhar quando cai. Sem isto, um campo
// poderia ser declarado e nunca ser consultado -- a guarda que passa por nao
// correr.
TEST(Comparar, CadaCampoMaiorMelhorFalhaAoCair) {
  const char* numericos[] = {"pixels", "cores", "textos", "blits"};
  for (const char* campo : numericos) {
    Ficha antes;
    Ficha depois;
    std::string v = campo;
    if (v == "pixels") { antes.pixels = 100; depois.pixels = 1; }
    else if (v == "cores") { antes.cores = 9; depois.cores = 2; }
    else if (v == "textos") { antes.textos = 5; depois.textos = 0; }
    else { antes.blits = 5; depois.blits = 0; }
    const auto r = CompararTextos(Montar({antes}), Montar({depois}), "a.json", "b.json");
    EXPECT_EQ(r.codigo, kRegressao) << campo << "\n" << r.relatorio;
    EXPECT_TRUE(Contem(r.relatorio, std::string(campo))) << campo << "\n" << r.relatorio;
  }
  const char* booleanos[] = {"carga", "modulo", "vtable", "applet"};
  for (const char* campo : booleanos) {
    Ficha antes;
    Ficha depois;
    std::string v = campo;
    if (v == "carga") depois.carga = false;
    else if (v == "modulo") depois.modulo = false;
    else if (v == "vtable") { antes.vtable = true; depois.vtable = false; }
    else depois.applet = false;
    const auto r = CompararTextos(Montar({antes}), Montar({depois}), "a.json", "b.json");
    EXPECT_EQ(r.codigo, kRegressao) << campo << "\n" << r.relatorio;
  }
}

// ---------------------------------------------------------------------------
// O CABECALHO DE PROVENIENCIA (forma escrita pelo `bateria.cpp` desde cfb031e).
//
// A guarda de configuracao tinha um buraco: derivava a identidade da lista de
// `pasta/mod`, logo dois corpus DIFERENTES que mantivessem os mesmos 62 pasta/mod
// eram indistinguiveis -- e duas dumps da mesma ROM dao o mesmo corpus com bytes
// diferentes. Estes testes fecham-no.
// ---------------------------------------------------------------------------

TEST(Comparar, CabecalhoIgualNaoFalha) {
  const std::string doc = MontarCom({{}});
  const auto r = CompararTextos(doc, doc, "a.json", "b.json");
  EXPECT_EQ(r.codigo, kSemRegressao) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "proveniencia da referencia: corpus_sha256=")) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "SEM REGRESSOES")) << r.relatorio;
}

// (b) GUARDA COMPLETA: MESMOS titulos, corpus DIFERENTE. Sem o cabecalho este caso
// passava como comparacao valida.
TEST(Comparar, CorpusDiferenteComOsMesmosTitulosERecusado) {
  Ficha a;
  Ficha b;
  b.pasta = "276212";
  b.mod = "pacmania";
  const auto r =
      CompararTextos(MontarCom({a, b}, kShaA), MontarCom({a, b}, kShaB), "ref.json", "nova.json");
  EXPECT_EQ(r.codigo, kConfigIncompativel) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "cabecalho.corpus_sha256")) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, kShaA)) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, kShaB)) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "Nenhum numero foi comparado")) << r.relatorio;
  EXPECT_FALSE(Contem(r.relatorio, "REGRESSOES")) << r.relatorio;
}

// Sem proveniencia NAO se recusa -- nao saber nao e "ser diferente" -- mas tambem
// nao passa calado: fica dito, e a palavra e "SEM PROVENIENCIA".
TEST(Comparar, SemProvenienciaNaoRecusaMasDiz) {
  const auto r = CompararTextos(MontarCom({{}}, kShaA), MontarCom({{}}, "desconhecido"), "a.json",
                                "b.json");
  EXPECT_EQ(r.codigo, kSemRegressao) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "SEM PROVENIENCIA")) << r.relatorio;
}

// `build` divergente e o uso NORMAL da ferramenta: comparar duas versoes do
// emulador. Se fosse criterio, o comparador recusaria a comparacao para que existe.
TEST(Comparar, BuildDiferenteNaoFalha) {
  const auto r = CompararTextos(MontarCom({{}}, kShaA, "abc1234"),
                                MontarCom({{}}, kShaA, "def5678"), "a.json", "b.json");
  EXPECT_EQ(r.codigo, kSemRegressao) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "cabecalho.build")) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "REGRESSOES (0)")) << r.relatorio;
}

// Corpo e cabecalho a discordar e uma mentira do instrumento: recusa de formato,
// e nao uma comparacao com menos titulos.
TEST(Comparar, CabecalhoQueContradizAListaERecusado) {
  Ficha a;
  Ficha b;
  b.pasta = "276212";
  b.mod = "pacmania";
  const std::string doc = MontarCom({a, b}, kShaA, "buildA", 61);
  const auto r = CompararTextos(doc, doc, "a.json", "b.json");
  EXPECT_EQ(r.codigo, kFormato) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "o cabecalho declara 61 titulos e a lista tem 2")) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "mentira do instrumento")) << r.relatorio;
}

// O cabecalho tem a sua propria lista de campos declarados: um campo novo la
// dentro tambem e uma recusa com o nome, e nao um silencio.
TEST(Comparar, ChaveDesconhecidaNoCabecalhoERecusada) {
  std::string doc = MontarCom({{}});
  const std::string alvo = "\"build\": \"buildA\"";
  const std::size_t p = doc.find(alvo);
  ASSERT_NE(p, std::string::npos);
  doc.insert(p + alvo.size(), ", \"sobra\": 1");
  const auto r = CompararTextos(doc, doc, "a.json", "b.json");
  EXPECT_EQ(r.codigo, kFormato) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "campo desconhecido no cabecalho 'sobra'")) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "kCabecalho")) << r.relatorio;
}

TEST(Comparar, ChaveDesconhecidaNoTopoERecusada) {
  std::string doc = MontarCom({{}});
  doc.insert(1, "\"extra\": 1, ");
  const auto r = CompararTextos(doc, doc, "a.json", "b.json");
  EXPECT_EQ(r.codigo, kFormato) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "chave desconhecida 'extra'")) << r.relatorio;
}

// Uma corrida com proveniencia e outra sem: de uma delas NAO SE SABE qual corpus
// correu. Recusa, e nao uma comparacao a meias.
TEST(Comparar, UmaCorridaComCabecalhoEOutraSemERecusada) {
  const auto r = CompararTextos(MontarCom({{}}), Montar({{}}), "ref.json", "nova.json");
  EXPECT_EQ(r.codigo, kConfigIncompativel) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "COM cabecalho")) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "SEM cabecalho")) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "--atualizar")) << r.relatorio;
}

// AO CONTRARIO do `build`, o `tamanho` do `.mod` e IDENTIDADE: um modulo de outro
// tamanho e outro ficheiro, e nao um emulador pior.
TEST(Comparar, ModComOutroTamanhoERecusado) {
  Ficha antes;
  antes.tamanho = 90068;
  Ficha depois;
  depois.tamanho = 91000;
  const auto r = CompararTextos(MontarCom({antes}), MontarCom({depois}), "ref.json", "nova.json");
  EXPECT_EQ(r.codigo, kConfigIncompativel) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "imicro3d (12875): tamanho 90068 -> 91000")) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "Nenhum numero foi comparado")) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "--atualizar")) << r.relatorio;
}

TEST(Comparar, MesmoTamanhoNaoRecusa) {
  Ficha a;
  a.tamanho = 90068;
  const auto r = CompararTextos(MontarCom({a}), MontarCom({a}), "a.json", "b.json");
  EXPECT_EQ(r.codigo, kSemRegressao) << r.relatorio;
}

// ===========================================================================
// O CAMPO `pressupostos`: novo, OPCIONAL, e neutro.
//
// A referencia versionada foi medida ANTES deste campo existir. Se o comparador
// exigisse o campo, a primeira corrida nova tornaria a referencia ilegivel -- e
// perder a referencia e perder a unica medida de regressao que temos.
// ===========================================================================
TEST(Comparar, UmaCorridaAntigaSemPressupostosContinuaLegivel) {
  Ficha velha;                       // sem o campo, como todas as de ate agora
  Ficha nova;
  nova.pressupostos = "{\"IShell::GetDeviceInfo\":1}";
  const auto r = CompararTextos(MontarCom({velha}), MontarCom({nova}), "ref.json", "nova.json");
  EXPECT_EQ(r.codigo, kSemRegressao) << r.relatorio;
  // O campo ausente vale o mapa VAZIO: a chave que nasce e reportada como
  // NEUTRA, com o nome, e nao como regressao nem como formato ilegivel.
  EXPECT_TRUE(Contem(r.relatorio, "pressupostos.IShell::GetDeviceInfo 0 -> 1")) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "NEUTROS QUE MUDARAM")) << r.relatorio;
}

TEST(Comparar, UmPressupostoNovoNaoERegressaoEUmQueDesapareceTambemNao) {
  Ficha antes;
  antes.pressupostos = "{\"IFileMgr::GetFreeSpace\":1}";
  Ficha depois;
  depois.pressupostos = "{\"IFileMgr::GetFreeSpace\":3,\"ICM::GetSSInfo\":1}";
  const auto r = CompararTextos(MontarCom({antes}), MontarCom({depois}), "ref.json", "nova.json");
  EXPECT_EQ(r.codigo, kSemRegressao) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "pressupostos.IFileMgr::GetFreeSpace 1 -> 3")) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "pressupostos.ICM::GetSSInfo 0 -> 1")) << r.relatorio;
  // Neutro quer dizer NEUTRO nos dois sentidos: o inverso tambem nao falha.
  const auto inverso = CompararTextos(MontarCom({depois}), MontarCom({antes}), "ref.json",
                                      "nova.json");
  EXPECT_EQ(inverso.codigo, kSemRegressao) << inverso.relatorio;
}

// O campo OPCIONAL nao abre a porta ao campo desconhecido: a recusa (d) fica.
TEST(Comparar, OCampoOpcionalNaoTornaQualquerCampoNovoAceite) {
  Ficha f;
  f.pressupostos = "{}";
  std::string doc = MontarCom({f});
  const std::string alvo = "\"pressupostos\":{}";
  const std::size_t p = doc.find(alvo);
  ASSERT_NE(p, std::string::npos) << doc;
  doc.insert(p + alvo.size(), ",\"invencao\":1");
  const auto r = CompararTextos(doc, doc, "a.json", "b.json");
  EXPECT_EQ(r.codigo, kFormato) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "campo desconhecido 'invencao'")) << r.relatorio;
}

// E um campo OBRIGATORIO em falta continua a ser recusa: so o que esta marcado
// `opcional` na tabela pode faltar.
TEST(Comparar, UmCampoObrigatorioEmFaltaContinuaARecusar) {
  std::string doc = MontarCom({{}});
  const std::string alvo = ",\"blits\":0";
  const std::size_t p = doc.find(alvo);
  ASSERT_NE(p, std::string::npos) << doc;
  doc.erase(p, alvo.size());
  const auto r = CompararTextos(doc, doc, "a.json", "b.json");
  EXPECT_EQ(r.codigo, kFormato) << r.relatorio;
  EXPECT_TRUE(Contem(r.relatorio, "faltam: blits")) << r.relatorio;
}
