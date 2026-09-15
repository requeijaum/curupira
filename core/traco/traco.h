#ifndef ZB2_CORE_TRACO_TRACO_H
#define ZB2_CORE_TRACO_TRACO_H

// Registador unico de eventos. E o modulo que existe para tornar IMPOSSIVEL
// repetir os defeitos de instrumento do Zeebulator antigo.
//
// Cada defeito medido, e a regra que o impede:
//
// 1. 71 dos 84 handlers de GL nao tinham log nenhum -- a conclusao natural de
//    quem lia o log era "o jogo nao chama aquilo", e chamava. -> `Nome` e
//    obrigatorio e nao tem valor por omissao. Nao ha como emitir sem nome.
//
// 2. Uma linha de log estava DUPLICADA (GlBindTexture) e parecia que o jogo
//    amarrava duas vezes. -> cada emissao passa por um ponto unico, e um teste
//    conta as emissoes por nome.
//
// 3. Logs de depuracao nasceram na pasta do titulo, que e midia de ROM do
//    utilizador. -> o destino tem de ser ABSOLUTO; um caminho relativo faz o
//    abrir FALHAR e a razao fica registada. Nunca se escreve "ao lado do que
//    esta a correr".
//
// 4. Contadores comparados entre configuracoes diferentes (interpretador contra
//    JIT) e chamados de regressao. -> cada contador guarda a etiqueta de
//    configuracao em que foi medido, e comparar duas etiquetas diferentes e um
//    erro de quem compara, nao um numero.
//
// 5. "Sem linha no log" era lido como "nao aconteceu". -> ha um contador
//    explicito de RECUSAS por nome: um caminho nao implementado nao fica mudo,
//    fica contado (principio P2).
//
// 6. Um subsistema inteiro (a vtable do IGL) podia estar por preencher e
//    ninguem sabia. -> `RegistarFalta` e o caminho obrigatorio para "nao
//    implementado", e o relatorio final lista o que ficou por fazer.
//
// 7. Trabalho de investigacao a poluir o artefacto. -> as marcas de depuracao
//    usam o prefixo `[DEBUG-` e ha um metodo que as lista, para a limpeza ser um
//    `grep` e nao uma varredura.

#include <cstdint>
#include <map>
#include <cstdio>
#include <string>
#include <vector>

#include "core/tempo/tempo.h"

namespace zb2 {

// UM ENDERECO ESCREVE-SE EM HEXADECIMAL, e isto existe porque nao era assim.
//
// Havia 11 sitios que faziam `"0x" + std::to_string(v)`: isso imprime o valor em
// DECIMAL com um prefixo hexadecimal a frente. O resultado e um numero que NAO
// EXISTE -- `saiu_do_modulo_para_0x2148007884` era 2148007884 = `0x8007FFCC`, a
// pilha; e `pfn=0x145884` era 145884 = `0x239DC`, dentro do `ddragonz.mod`.
//
// Quem o apanhou foi o sub-agente do rasterizador, depois de gastar duas leituras
// erradas por causa disto. E o estrago nao fica no log: **fica no LEDGER**, que e
// onde eu procuro os enderecos para voltar a um sitio. Um ledger com numeros que
// nao existem e pior do que um ledger vazio.
//
// Mesma familia do campo `tamanho` que esteve a zero: um numero que parece medido e
// nao e.
inline std::string Hex(std::uint32_t v) {
  char b[16];
  std::snprintf(b, sizeof(b), "0x%08x", v);
  return b;
}



enum class Nivel { Depuracao, Informacao, Aviso, Erro };
enum class Area { Cpu, Memoria, Carga, Brew, Video, Audio, Entrada, Guarda, Teste, Traco };

const char* Nome(Area a);
const char* Nome(Nivel n);

struct Evento {
  Ns quando = 0;
  Area area = Area::Traco;
  Nivel nivel = Nivel::Informacao;
  std::string nome;    // nunca vazio
  std::string detalhe; // pode ser vazio
};

// Um destino de eventos. Existe como interface para os testes poderem observar
// sem tocar em ficheiros, e para o ficheiro ser so UM destino entre outros.
class Destino {
 public:
  virtual ~Destino() = default;
  virtual void Escrever(const Evento& e) = 0;
};

// Acumula em memoria. Usado pelos testes e pela bateria.
class DestinoMemoria : public Destino {
 public:
  void Escrever(const Evento& e) override { eventos.push_back(e); }
  std::size_t Quantos(Area a, Nivel n) const;
  std::size_t QuantosComNome(const std::string& nome) const;
  std::vector<Evento> eventos;
};

// Escreve num ficheiro. O caminho tem de ser absoluto; ver regra 3 acima.
class DestinoFicheiro : public Destino {
 public:
  explicit DestinoFicheiro(const std::string& caminho_absoluto);
  ~DestinoFicheiro() override;
  void Escrever(const Evento& e) override;

  bool Abriu() const { return f_ != nullptr; }
  // Porque nao abriu: caminho relativo, directoria inexistente, permissao.
  // Vazio quando abriu.
  const std::string& Motivo() const { return motivo_; }

 private:
  void* f_ = nullptr;
  std::string motivo_;
};

class Traco {
 public:
  // A etiqueta de configuracao identifica COMO a corrida foi feita (CPU usada,
  // tamanho de bloco do JIT, interruptor ligado...). Existe para o defeito 4:
  // dois numeros so se comparam se a etiqueta for a mesma, e o proprio codigo
  // de comparacao recusa quando nao e.
  explicit Traco(std::string etiqueta_config = "padrao", Tempo* tempo = nullptr);

  void JuntarDestino(Destino* d) { destinos_.push_back(d); }

  // `nome` NAO tem valor por omissao. E deliberado: nao existe emissao anonima.
  void Emitir(Area area, Nivel nivel, const std::string& nome, const std::string& detalhe = "");

  // Caminho obrigatorio para "ainda nao implementado" (principio P2): em vez de
  // devolver sucesso e nao fazer nada, regista. `o_que_falta` e o nome do que
  // falta; aparece no relatorio final.
  void RegistarFalta(Area area, const std::string& o_que_falta, const std::string& porque = "");

  // 8. Um valor DECLARADO -- plausivel, escrito no codigo, e NAO MEDIDO -- ficava
  //    dito num `Emitir(..., Nivel::Informacao, ...)`, e a bateria so publica
  //    `ContagemFaltas()` (`tools/bateria.cpp:737`, `:936`). Resultado MEDIDO: o
  //    `HID_DECLARADO`, o `ICM_GETSSINFO` e os outros existiam no codigo e eram
  //    INVISIVEIS no artefacto que se le. Uma classe inteira de meia-verdade sem
  //    numero nenhum.
  //
  //    `RegistarPressuposto` e para essa classe o que o `RegistarFalta` e para a
  //    recusa: caminho unico, contado por nome, e publicado. O zeebx tem o
  //    equivalente ha muito -- imprime uma lista `assumptions` em cada corrida
  //    (`src/main.rs:969-974`).
  //
  //    A REGRA DE FRONTEIRA: recusar e `RegistarFalta`; responder um valor que
  //    nao se mediu e `RegistarPressuposto`. Um caminho nunca e os dois.
  void RegistarPressuposto(Area area, const std::string& o_que_se_assume,
                           const std::string& porque = "");

  // Marcas de depuracao: prefixo unico para a limpeza ser um `grep`.
  void Depurar(const std::string& marca, const std::string& detalhe);

  // --- consulta, usada pelos testes e pelo relatorio ---
  const std::string& EtiquetaConfig() const { return etiqueta_; }
  std::uint64_t TotalEmitidos() const { return total_emitidos_; }
  const std::map<std::string, std::uint64_t>& ContagemFaltas() const { return faltas_; }
  const std::map<std::string, std::uint64_t>& ContagemPressupostos() const { return pressupostos_; }
  std::vector<std::string> MarcasDeDepuracao() const;

  // Compara dois contadores. Recusa quando as etiquetas diferem -- e devolve a
  // razao, em vez de um numero que seria comparado a toa.
  struct Comparacao {
    bool comparavel = false;
    std::string motivo;
    std::int64_t diferenca = 0;
  };
  static Comparacao Comparar(const Traco& a, const Traco& b, const std::string& nome);

 private:
  std::string etiqueta_;
  Tempo* tempo_;
  std::vector<Destino*> destinos_;
  std::uint64_t total_emitidos_ = 0;
  std::map<std::string, std::uint64_t> faltas_;
  std::map<std::string, std::uint64_t> pressupostos_;
  std::vector<std::string> marcas_;
};

}  // namespace zb2

#endif  // ZB2_CORE_TRACO_TRACO_H
