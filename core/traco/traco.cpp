#include "core/traco/traco.h"

#include <algorithm>
#include <cstdio>

namespace zb2 {
namespace {

bool CaminhoAbsoluto(const std::string& p) {
  if (p.empty()) return false;
  if (p[0] == '/') return true;
  return false;
}

}  // namespace

const char* Nome(Area a) {
  switch (a) {
    case Area::Cpu: return "cpu";
    case Area::Memoria: return "memoria";
    case Area::Carga: return "carga";
    case Area::Brew: return "brew";
    case Area::Video: return "video";
    case Area::Audio: return "audio";
    case Area::Entrada: return "entrada";
    case Area::Guarda: return "guarda";
    case Area::Teste: return "teste";
    case Area::Traco: return "traco";
  }
  return "?";
}

const char* Nome(Nivel n) {
  switch (n) {
    case Nivel::Depuracao: return "DEP";
    case Nivel::Informacao: return "INF";
    case Nivel::Aviso: return "AVS";
    case Nivel::Erro: return "ERR";
  }
  return "?";
}

std::size_t DestinoMemoria::Quantos(Area a, Nivel n) const {
  std::size_t c = 0;
  for (const Evento& e : eventos) {
    if (e.area == a && e.nivel == n) ++c;
  }
  return c;
}

std::size_t DestinoMemoria::QuantosComNome(const std::string& nome) const {
  std::size_t c = 0;
  for (const Evento& e : eventos) {
    if (e.nome == nome) ++c;
  }
  return c;
}

DestinoFicheiro::DestinoFicheiro(const std::string& caminho_absoluto) {
  // Regra 3: caminho relativo nasceria ao lado do que esta a correr -- e no
  // Zeebulator antigo isso foi a pasta de ROM do titulo, em midia de usuario.
  if (!CaminhoAbsoluto(caminho_absoluto)) {
    motivo_ = "caminho relativo recusado: um log relativo nasce ao lado do que "
              "esta a correr, e no projeto antigo isso foi a pasta do titulo, "
              "em midia de ROM do utilizador";
    return;
  }
  std::FILE* f = std::fopen(caminho_absoluto.c_str(), "wb");
  if (f == nullptr) {
    motivo_ = "nao consegui abrir para escrita";
    return;
  }
  f_ = f;
}

DestinoFicheiro::~DestinoFicheiro() {
  if (f_ != nullptr) std::fclose(static_cast<std::FILE*>(f_));
}

void DestinoFicheiro::Escrever(const Evento& e) {
  if (f_ == nullptr) return;
  std::fprintf(static_cast<std::FILE*>(f_), "%12lld %-7s %s %-28s | %s\n",
               static_cast<long long>(e.quando), Nome(e.area), Nome(e.nivel),
               e.nome.c_str(), e.detalhe.c_str());
}

Traco::Traco(std::string etiqueta_config, Tempo* tempo)
    : etiqueta_(std::move(etiqueta_config)), tempo_(tempo) {}

void Traco::Emitir(Area area, Nivel nivel, const std::string& nome, const std::string& detalhe) {
  if (nome.empty()) {
    // Nao existe emissao anonima (regra 1). Em vez de escrever uma linha sem
    // nome -- que foi exactamente o defeito dos 71 handlers mudos --, escreve-se
    // uma linha que denuncia a falta.
    Evento e;
    e.quando = tempo_ != nullptr ? tempo_->Agora() : 0;
    e.area = area;
    e.nivel = Nivel::Erro;
    e.nome = "EMISSAO_SEM_NOME";
    e.detalhe = "o autor da chamada nao deu nome ao evento";
    ++total_emitidos_;
    for (Destino* d : destinos_) d->Escrever(e);
    return;
  }
  Evento e;
  e.quando = tempo_ != nullptr ? tempo_->Agora() : 0;
  e.area = area;
  e.nivel = nivel;
  e.nome = nome;
  e.detalhe = detalhe;
  ++total_emitidos_;
  for (Destino* d : destinos_) d->Escrever(e);
}

void Traco::RegistarFalta(Area area, const std::string& o_que_falta, const std::string& porque) {
  if (o_que_falta.empty()) {
    Emitir(area, Nivel::Erro, "FALTA_SEM_NOME", "quem regista tem de dizer o que falta");
    return;
  }
  ++faltas_[o_que_falta];
  std::string detalhe = porque;
  Emitir(area, Nivel::Aviso, "NAO_IMPLEMENTADO: " + o_que_falta, detalhe);
}

void Traco::RegistarPressuposto(Area area, const std::string& o_que_se_assume,
                                const std::string& porque) {
  if (o_que_se_assume.empty()) {
    Emitir(area, Nivel::Erro, "PRESSUPOSTO_SEM_NOME",
           "quem declara um valor tem de dizer o que assume");
    return;
  }
  ++pressupostos_[o_que_se_assume];
  // `Informacao` e nao `Aviso`: um pressuposto declarado nao e um defeito. O que
  // o torna publico e a CONTAGEM, nao o nivel.
  Emitir(area, Nivel::Informacao, "PRESSUPOSTO: " + o_que_se_assume, porque);
}

void Traco::Depurar(const std::string& marca, const std::string& detalhe) {
  // Prefixo fixo: a limpeza de toda a instrumentacao de investigacao passa a ser
  // um `grep -r '\[DEBUG-'`, e nao uma varredura a mao (regra 7).
  std::string nome = "[DEBUG-" + marca + "]";
  marcas_.push_back(nome);
  Emitir(Area::Traco, Nivel::Depuracao, nome, detalhe);
}

std::vector<std::string> Traco::MarcasDeDepuracao() const {
  std::vector<std::string> v = marcas_;
  std::sort(v.begin(), v.end());
  v.erase(std::unique(v.begin(), v.end()), v.end());
  return v;
}

Traco::Comparacao Traco::Comparar(const Traco& a, const Traco& b, const std::string& nome) {
  Comparacao c;
  // Regra 4, e a licao mais cara da sessao anterior: eu comparei numeros de
  // corridas diferentes DUAS vezes e chamei a um deles regressao (o censo do
  // `abd` a comparar dois instantes da mesma sequencia, e o `SIMCARDCTL` a
  // comparar janelas de tempo diferentes). Aqui a comparacao recusa-se a
  // acontecer quando a configuracao nao e a mesma.
  if (a.etiqueta_ != b.etiqueta_) {
    c.comparavel = false;
    c.motivo = "configuracoes diferentes: '" + a.etiqueta_ + "' contra '" + b.etiqueta_ +
               "' -- remedir os dois lados antes de comparar";
    return c;
  }
  auto ia = a.faltas_.find(nome);
  auto ib = b.faltas_.find(nome);
  std::uint64_t va = ia == a.faltas_.end() ? 0 : ia->second;
  std::uint64_t vb = ib == b.faltas_.end() ? 0 : ib->second;
  c.comparavel = true;
  c.diferenca = static_cast<std::int64_t>(va) - static_cast<std::int64_t>(vb);
  return c;
}

}  // namespace zb2
