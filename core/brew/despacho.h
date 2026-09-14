#ifndef ZB2_CORE_BREW_DESPACHO_H
#define ZB2_CORE_BREW_DESPACHO_H

// O DESPACHO DE HLE: o que o emulador faz quando o guest chama o sistema.
//
// VIVE NO MOTOR, e nao na ferramenta. Isto era o maior pedaco de comportamento do
// emulador dentro de `tools/bateria.cpp` -- cerca de 860 linhas -- e enquanto la
// esteve, **so a ferramenta de medicao sabia correr um jogo**, e nenhum teste
// chegava a nada disto.
//
// O desenho: o modulo do guest descobre o sistema por uma tabela de ponteiros de
// funcao (`AEEHelperFuncs`) e por vtables de interface. Esses ponteiros apontam
// para a FAIXA DE SAIDA; o laco de execucao para quando o PC entra nela, e este
// despacho decide pelo INDICE. Um endereco unico por slot e o que faz o registo
// dizer QUAL metodo foi pedido, em vez de "algo do shell".

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core/brew/ajudantes.h"
#include "core/brew/ihiddevice.h"
#include "core/brew/arquivo.h"
#include "core/brew/interface.h"
#include "core/brew/tela.h"
#include "core/brew/vfs.h"
#include "core/cpu/cpu.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"

namespace zb2::brew {

// Um temporizador pedido pelo guest. UM so, porque e o que os titulos pedem: o
// laco de quadro, re-armado pelo proprio callback.
struct Temporizador {
  bool ativo = false;
  std::uint32_t callback = 0;  // AEECallback* -- (funcao, contexto)
  std::int64_t vence_em_ms = 0;
};

struct ResultadoFase {
  std::uint64_t passos = 0;
  // PORQUE parou. `retornou` e uma fase concluida; `orcamento_de_tempo` e um
  // titulo que ainda estava a ANDAR, e isso muda o que se conclui dele.
  std::string motivo;
};

class Despacho {
 public:
  Despacho(Memoria& mem, Traco& traco, Alocador& alocador, Vfs& vfs);

  // Escreve o endereco de cada slot do `AEEHelperFuncs` que tem implementacao.
  //
  // UMA TABELA SO, de onde saem as escritas E a lista de saltos do preenchimento.
  // Havia duas -- as escritas e uma lista de "quem ja tem implementacao" que o
  // laco consultava -- e eu esquecime de acrescentar a segunda UMA vez, e o laco
  // apagou a implementacao. **Duas listas que tem de concordar sao zero listas.**
  void InstalarAjudantes(const Saidas& saidas, Endereco tabela);

  // Corre o guest ate a sentinela, o limite de passos, ou o orcamento de tempo.
  ResultadoFase Correr(ICpu& cpu, std::uint64_t limite, std::uint32_t pp_saida);

  // --- o estado que a ferramenta observa ---------------------------------
  Tela& TelaRef() { return tela_; }
  const Tela& TelaRef() const { return tela_; }
  Vfs& VfsRef() { return vfs_; }
  Arquivos& ArquivosRef() { return arquivos_; }
  const std::map<std::string, std::uint64_t>& Faltas() const { return faltas_; }
  void LimparFaltas() { faltas_.clear(); }
  std::uint32_t Textos() const { return textos_; }
  std::uint32_t Blits() const { return blits_; }
  std::uint32_t Backlights() const { return backlights_; }

  // --- A ENTRADA (etapa 8) -------------------------------------------------
  //
  // O `IHID`, o `IHIDDevice` e os sinais do BREW vivem num modulo proprio
  // (`core/brew/ihiddevice.{h,cpp}` + `core/brew/ihid_entrada.{h,cpp}`); aqui
  // esta so a CABLAGEM deles na faixa de saida.
  //
  // `base` e o primeiro indice desta faixa. O modulo NAO impoe um numero: quem
  // chama escolhe-o, e a `Saidas` tem de ter espaco para
  // `base + Sinais::kSlotsNecessarios + Ihid::kSlotsNecessarios`. Se nao tiver,
  // os dois modulos RECUSAM e dizem qual o indice que falta.
  bool InstalarEntrada(const Saidas& saidas, std::uint32_t base);

  // Atende um pedido desta faixa. `false` = o indice nao e da entrada.
  bool AtenderEntrada(ICpu& cpu, std::uint32_t indice);

  // Aplica a entrada ate ao instante actual e marca os sinais registados.
  // `true` = ha um callback do titulo posto no PC (PC = funcao, R0 = contexto,
  // LR = sentinela); quem chama tem de o deixar correr.
  bool BombearEntrada(ICpu& cpu) { return ihid_.Bombear(cpu); }

  // A FAIXA DO MODULO DO TITULO (base e tamanho), para o modulo da entrada poder
  // recusar um callback que aponte para fora dela. A base e ZERO, e e medida.
  void DefinirFaixaDoModulo(std::uint32_t base, std::uint32_t tamanho) {
    base_do_modulo_ = base;
    sinais_.DefinirFaixaDoModulo(base, tamanho);
  }

  // --- o LACO DE QUADRO ----------------------------------------------------
  //
  // Em BREW o laco de quadro do jogo vive no `IShell::SetTimer`: o applet arma um
  // temporizador e o proprio callback re-arma o seguinte. O laco do despacho ja o
  // cumpre; isto existe para quem dirige o titulo de FORA poder correr quadros
  // depois do arranque -- que e o que a bateria (2 fases: carga e create) nao
  // faz, e sem o qual um menu nao chega a andar.
  bool TemporizadorArmado() const { return timer_.ativo; }
  std::uint32_t CallbackDoTemporizador() const { return timer_.callback; }
  bool PrepararCallbackDoTemporizador(ICpu& cpu);

  bool EntradaPronta() const { return entrada_pronta_; }
  EntradaDoZeebo& Entrada() { return entrada_; }
  Sinais& SinaisRef() { return sinais_; }
  Ihid& IhidRef() { return ihid_; }
  std::uint32_t BaseDaEntrada() const { return base_da_entrada_; }
  std::uint32_t BaseDosSinais() const { return base_da_entrada_; }
  std::uint32_t BaseDoIhid() const { return ihid_.BaseDasSaidas(); }

  // O que o despacho precisa de saber do titulo actual. Posto UMA vez por titulo,
  // no inicio -- e nao um estado que passa de um titulo para o outro.
  void SituarTitulo(const std::string& dir, const std::string& pasta) {
    dir_ = dir;
    pasta_ = pasta;
  }
  void DefinirVtableBitmap(std::uint32_t v) { vtable_bitmap_ = v; }
  void DefinirVtableFicheiro(std::uint32_t v) { vtable_ficheiro_ = v; }
  void DefinirApplet(std::uint32_t v) { applet_ = v; }

 private:
  Memoria& mem_;
  Traco& traco_;
  Alocador& al_;
  Vfs& vfs_;
  Arquivos arquivos_;
  Tela tela_;

  // A ENTRADA. `sinais_` antes de `ihid_`, porque o `Ihid` guarda a referencia
  // ao `Sinais` -- e a ordem de declaracao e a ordem de construcao.
  EntradaDoZeebo entrada_;
  Sinais sinais_;
  Ihid ihid_;
  std::uint32_t base_da_entrada_ = 0;
  std::uint32_t base_do_modulo_ = 0;
  bool entrada_pronta_ = false;

  std::int64_t agora_ms_ = 0;
  Temporizador timer_;

  std::uint32_t textos_ = 0;
  std::uint32_t blits_ = 0;
  std::uint32_t backlights_ = 0;
  std::uint32_t updates_ = 0;
  std::uint32_t dibs_ = 0;
  std::uint32_t applet_ = 0;
  std::uint32_t destino_ = 0;
  std::uint32_t vtable_bitmap_ = 0;
  std::uint32_t vtable_ficheiro_ = 0;

  std::string dir_;
  std::string pasta_;
  std::map<std::string, std::uint64_t> faltas_;
};

}  // namespace zb2::brew

#endif
