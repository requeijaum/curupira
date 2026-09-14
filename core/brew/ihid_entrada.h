#ifndef ZB2_CORE_BREW_IHID_ENTRADA_H
#define ZB2_CORE_BREW_IHID_ENTRADA_H

// ETAPA 8 -- A ENTRADA, do lado de FORA do guest: os sinais do BREW e a lista de
// eventos que se injecta no controle.
//
// ---------------------------------------------------------------------------
// P4 (determinismo por construcao), e porque este ficheiro existe
// ---------------------------------------------------------------------------
// NAO HA NESTE FICHEIRO UMA UNICA LEITURA DO TECLADO DO HOSPEDEIRO, e nao ha
// `time()`, `clock()`, `rand()` nem `SDL_GetTicks()`. O tempo e um CONTADOR que
// so avanca por `Avancar()`, e o estado do controle e uma FUNCAO PURA do guiao e
// do instante.
//
// O DEFEITO QUE ISTO IMPEDE, medido, e nao suposto: a arvore antiga lia o
// relogio do hospedeiro DENTRO do handler de HLE e escrevia-o no campo
// `pdwTimestamp` que o jogo recebe --
//   research/sources/zeebulator/tools/game_probe.cpp, `GetNextButtonEvent`:
//       uint32_t timestamp_addr = core.GetRegister(zeebulator::kR2);
//       if (timestamp_addr != 0) core.GetMemory().Write32(timestamp_addr, SDL_GetTicks());
// O jogo via o relogio da maquina de quem estava a jogar, e duas corridas do
// mesmo titulo deixavam de ser comparaveis. Aqui o `pdwTimestamp` sai do contador
// injectado, e um teste compara duas corridas do MESMO guiao.

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "core/brew/ajudantes.h"
#include "core/brew/interface.h"
#include "core/cpu/cpu.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"

namespace zb2::brew {

// --- os slots dos sinais, do cabecalho do SDK -------------------------------
//
// Transcritos dos `INHERIT_*` e CONFERIDOS por `tools/gerar_slots.py` (a copia
// usada para conferir levava `AEEISignal.h`, `AEEISignalCtl.h` e
// `AEEISignalCBFactory.h` na lista de interfaces; o gerador da arvore ainda nao
// as tem -- ver a nota no relatorio):
//
//   ISignal          INHERIT_IQI (3) + Set                        -> Set = 3
//                    platform/system/inc/AEEISignal.h, INHERIT_ISignal
//   ISignalCtl       INHERIT_ISignal (4) + Detach + Enable         -> 4 e 5
//                    platform/system/inc/AEEISignalCtl.h, INHERIT_ISignalCtl
//   ISignalCBFactory INHERIT_IQI (3) + CreateSignal               -> 3
//                    platform/system/inc/AEEISignalCBFactory.h, INHERIT_ISignalCBFactory
constexpr std::uint32_t kISignal_Set = 3;
constexpr std::uint32_t kISignalCtl_Detach = 4;
constexpr std::uint32_t kISignalCtl_Enable = 5;
constexpr std::uint32_t kISignalCBFactory_CreateSignal = 3;

// Os IIDs, do cabecalho.
constexpr std::uint32_t kIidISignal = 0x010285f5u;          // AEEISignal.h
constexpr std::uint32_t kIidISignalCtl = 0x01041079u;       // AEEISignalCtl.h
constexpr std::uint32_t kIidISignalCBFactory = 0x01043541u; // AEEISignalCBFactory.h

// `AEECLSID_SignalCBFactory`.
//
// O valor NAO vem de memoria: `platform/deprecated/inc/AEESignalCBFactory.bid`
// declara `#define AEECLSID_SignalCBFactory AEECLSID_CSignalCBFactory`, e
// `platform/system/inc/AEECSignalCBFactory.h:23` da o numero --
//     #define AEECLSID_CSignalCBFactory 0x1041207
// MEDIDO no corpus, para nao ficar so na palavra do cabecalho: 37 dos 62 titulos
// embutem este valor como constante (38 ocorrencias), e o `tectoy` (Z-Wheel) e um
// deles -- o que fecha o caminho: o Z-Wheel cria a fabrica de sinais.
constexpr std::uint32_t kClsidSignalCBFactory = 0x01041207u;

// Os codigos de erro vem do enum de `core/brew/ajudantes.h` -- UMA fonte so para
// toda a arvore. Este modulo tinha uma copia propria deles, e a copia fez o que
// as copias fazem: divergiu (ver o comentario do enum).

// O objecto SINAL, como o nosso HLE o constroi na memoria do guest.
//
//     +0  vtable (do ISignalCtl, que e o objecto que o jogo entrega)
//     +4  contagem de referencias
//     +8  pfn     -- o `AEECallback`: a funcao
//     +12 pUser   -- o `AEECallback`: o contexto
//
// As palavras 0 e 4 sao o cabecalho ROPI que TODO o objecto nosso tem (e o que o
// `ConstruirObjeto` de `core/brew/interface.h` escreve, e o que o `AddRef` e o
// `Release` do despacho esperam). O par do `AEECallback` fica a seguir, e o
// deslocamento esta dito aqui porque e uma ESCOLHA NOSSA: o objecto e criado por
// nos, e o jogo so lhe toca pelos slots da vtable.
constexpr std::uint32_t kSinal_pfn = 8;
constexpr std::uint32_t kSinal_pUser = 12;
constexpr std::uint32_t kTamanhoDoSinal = 16;

// A sentinela de retorno de um callback.
//
// O DESPACHO DA ARVORE ja usa `0xFFFFFFF0` para isto (`core/brew/despacho.cpp`,
// `kSentinela`), mas o valor esta DENTRO de um `namespace` anonimo daquele
// ficheiro, logo nao se pode incluir. Fica aqui o mesmo valor, DECLARADO, e o
// chamador pode troca-lo com `Sinais::DefinirSentinela` na cablagem.
constexpr std::uint32_t kSentinelaPadrao = 0xFFFFFFF0u;

// --- os enderecos dos objectos deste modulo ---------------------------------
//
// DECLARADOS, com a razao: fora da faixa do modulo (0x00100000..0x01100000, que o
// despacho trata como "e codigo do titulo"), acima do heap (0x80200000) e
// distantes das faixas que a `interface.h` ja ocupa (0x8001/2/3/4/5/6/7xxxx).
constexpr std::uint32_t kVtableFabricaDeSinais = 0x81030000u;
constexpr std::uint32_t kObjFabricaDeSinais = 0x81031000u;
constexpr std::uint32_t kVtableSinal = 0x81040000u;
constexpr std::uint32_t kObjSinalBase = 0x81041000u;
constexpr std::uint32_t kPassoDoSinal = 0x40u;

// Quantos sinais o modulo aceita criar. E um LIMITE, e nao uma esperanca: passado
// ele, `CreateSignal` RECUSA (P2) em vez de escrever fora da faixa.
constexpr std::uint32_t kSinaisDisponiveis = 4;

// A faixa onde o modulo do titulo e carregado, usada para RECUSAR um callback que
// aponte para fora do modulo em vez de saltar para o vazio em silencio (P2).
//
// A BASE E ZERO, e isso e MEDIDO: os literais de um `.mod` sao offsets do ficheiro
// usados como enderecos absolutos (`tests/mod_base_test.cpp`: 51 literais do
// `pacmania.mod` caem em cima de cadeias reais com base zero, zero com a base
// antiga `0x00100000`). A faixa NAO pode ser uma constante: o TAMANHO depende do
// titulo, e quem o sabe e quem carrega o modulo -- por isso ha um `DefinirFaixa`.
constexpr std::uint32_t kBaseDoModulo = 0x00000000u;

// ---------------------------------------------------------------------------
// EntradaDoZeebo -- a lista de eventos PRE-DEFINIDA
// ---------------------------------------------------------------------------
//
// O guiao e uma lista de `(t_ms, tipo, uid, valor)`. O estado num instante e
// derivado do guiao por REPLAY, e nao por um cursor que se vai mexendo: assim
// perguntar duas vezes pelo mesmo instante da o mesmo resultado, e a ordem das
// perguntas nao muda nada. Um estado que depende da ORDEM das perguntas foi uma
// das classes de defeito medidas nesta sessao.
enum class TipoNaEntrada { Eixo, Botao };

struct EventoDeEntrada {
  std::uint32_t t_ms = 0;
  TipoNaEntrada tipo = TipoNaEntrada::Eixo;
  std::uint32_t uid = 0;
  std::int32_t valor = 0;
};

class EntradaDoZeebo {
 public:
  // A faixa do valor de um EIXO. O CENTRO esta MEDIDO; os extremos estao
  // DECLARADOS -- ver `core/brew/ihiddevice.h`, onde a medicao esta escrita.
  static constexpr std::int32_t kValorMin = 0;
  static constexpr std::int32_t kValorCentro = 128;
  static constexpr std::int32_t kValorMax = 255;

  // Le um guiao de texto. UMA LINHA POR EVENTO:
  //     <t_ms> <eixo|botao> <uid> <valor>
  // `#` comeca um comentario. O uid aceita decimal e hexadecimal (`0x...`).
  //
  // RECUSA (e diz por que) uma linha mal formada, um eixo fora de 0..255, um
  // botao fora de 0..1, e um instante que nao seja crescente. Um guiao com uma
  // linha invalida NAO e aceite pela metade: `Ler` devolve false e nao altera
  // nada -- meio guiao aplicado e uma corrida que nao se pode comparar com nada.
  static bool Ler(const std::string& texto, EntradaDoZeebo* saida, std::string* motivo);

  // Acrescenta um evento a mao (usado pelos testes e por quem monta o guiao em
  // codigo). Valida as mesmas regras, e devolve false com o motivo.
  bool Adicionar(const EventoDeEntrada& e, std::string* motivo);

  // --- o relogio INJECTADO ------------------------------------------------
  std::uint32_t Agora() const { return agora_ms_; }
  void Avancar(std::uint32_t ms) { agora_ms_ += ms; }
  void Repor(std::uint32_t t_ms) { agora_ms_ = t_ms; }

  // O estado no instante pedido: valor do UID, ou `nao_conhecido` a verdadeiro
  // quando o guiao nao fala desse UID.
  std::int32_t ValorEm(std::uint32_t t_ms, std::uint32_t uid, bool* nao_conhecido) const;
  std::int32_t ValorAgora(std::uint32_t uid, bool* nao_conhecido) const {
    return ValorEm(agora_ms_, uid, nao_conhecido);
  }

  const std::vector<EventoDeEntrada>& Guiao() const { return eventos_; }
  std::size_t Quantos() const { return eventos_.size(); }

 private:
  std::vector<EventoDeEntrada> eventos_;  // por ordem de tempo, nao decrescente
  std::uint32_t agora_ms_ = 0;
};

// ---------------------------------------------------------------------------
// Sinais -- a fabrica do BREW e os objectos de sinal
// ---------------------------------------------------------------------------
//
// PORQUE ISTO E NOSSO E NAO DO JOGO: o jogo nao constroi o par `(funcao,
// contexto)`. Ele PEDE-O a fabrica:
//
//   ISignalCBFactory_CreateSignal(piFactory, L_JoystickPositionCB, pMe, 0, &pISignalCtl);
//   IHIDDevice_RegisterForPositionChange(pIHIDDevice, (ISignal *)pISignalCtl);
//
// -- exatamente assim, e LIDO do sample do proprio SDK:
//   research/docs/sdk-extract/Zeebo SDK + BREW SDK 4.0.2 + BREW MP SDK/
//   ZeeboSDKPackage-1.2.4/.../samples/conftest_source/conftest/GamepadMgr.c,
//   `L_GamepadMgr_NewJoystick`. Dois factos do sample que o codigo daqui segue:
//   ele passa o `ISignalCtl` onde a interface pede um `ISignal` (o cast esta
//   escrito no sample), e o callback dele le os eventos num laco com
//   `IHIDDevice_GetNextButtonEvent(..., NULL, NULL)` -- ou seja, os dois ultimos
//   argumentos VAO A NULL, e um handler que escreva neles sem olhar para o
//   ponteiro mata o jogo.
class Sinais {
 public:
  Sinais(Memoria& mem, Traco& traco);

  // COMO OS INDICES DA FAIXA DE SAIDA FICAM: o slot `i` de uma vtable aponta
  // para o endereco da saida `base_das_saidas + base_da_interface + i`. Sao dois
  // blocos, um por interface, com 8 indices cada.
  static constexpr std::uint32_t kBaseDaFabrica = 0;  // ISignalCBFactory
  static constexpr std::uint32_t kBaseDoSinal = 8;    // ISignal / ISignalCtl

  // Quantos indices da faixa de saida este modulo ocupa. O chamador TEM de
  // dimensionar a `Saidas` para caber; se nao couber, `Construir` RECUSA e diz
  // qual o indice que falta (P2: nada de escrever fora da faixa).
  static constexpr std::uint32_t kSlotsNecessarios = 16;

  // Constroi a fabrica e escreve as vtables. `base_das_saidas` e o primeiro
  // indice desta faixa; o modulo nao impoe um numero ao chamador.
  bool Construir(const Saidas& saidas, std::uint32_t base_das_saidas);

  // Atende um pedido da faixa. Devolve false quando o indice nao e deste modulo.
  bool Atender(ICpu& cpu, std::uint32_t indice);

  // --- o lado que o IHIDDevice usa ---------------------------------------
  bool Conhece(std::uint32_t endereco) const;
  // `ISignal_Set`: o contrato do `AEEISignal.h` diz que isto marca o sinal como
  // pronto e que o ENTREGADOR invoca o handler associado. Aqui o entregador e
  // este modulo, e ele enfileira o par `(pfn, pUser)`.
  void Marcar(std::uint32_t endereco);
  // Poe o proximo callback pendente no guest: PC = pfn, R0 = pUser,
  // LR = sentinela. Devolve false quando nao ha nada pendente.
  bool PrepararProximoCallback(ICpu& cpu);

  void DefinirSentinela(std::uint32_t s) { sentinela_ = s; }

  // A faixa do modulo do titulo: base e TAMANHO. Sem isto definido o tamanho e
  // zero, e TODO o callback e recusado -- o que e a resposta certa para "nao sei
  // onde esta o codigo do titulo", e nao um salto para o desconhecido.
  void DefinirFaixaDoModulo(std::uint32_t base, std::uint32_t tamanho) {
    base_do_modulo_ = base;
    tamanho_do_modulo_ = tamanho;
  }
  std::uint32_t BaseDoModulo() const { return base_do_modulo_; }
  std::uint32_t TamanhoDoModulo() const { return tamanho_do_modulo_; }

  // --- consulta, para testes e para o relatorio ---------------------------
  std::uint32_t ObjetoSinal(std::uint32_t n) const;
  std::uint32_t QuantosSinais() const { return quantos_sinais_; }
  std::uint32_t QuantosMarcados() const { return marcados_; }
  std::uint32_t QuantosEntregues() const { return entregues_; }
  std::uint32_t EnderecoDaFabrica() const { return objeto_fabrica_; }

 private:
  bool AtenderFabrica(ICpu& cpu, std::uint32_t slot);
  bool AtenderSinal(ICpu& cpu, std::uint32_t slot, std::uint32_t endereco);
  // Recusa em voz alta (P2): regista a falta, poe `AEE_EBADPARM` no r0 e devolve
  // `true` (o pedido ERA nosso, e foi recusado -- nao e "nao era meu").
  bool Recusar(ICpu& cpu, const char* o_que, const std::string& porque);

  Memoria& mem_;
  Traco& traco_;

  std::uint32_t base_ = 0;
  bool construido_ = false;

  std::uint32_t endereco_vtable_ = 0;
  std::uint32_t objeto_fabrica_ = 0;
  std::uint32_t endereco_vtable_sinal_ = 0;

  std::uint32_t quantos_sinais_ = 0;
  std::uint32_t marcados_ = 0;
  std::uint32_t entregues_ = 0;
  std::uint32_t sentinela_ = kSentinelaPadrao;
  std::uint32_t base_do_modulo_ = kBaseDoModulo;
  std::uint32_t tamanho_do_modulo_ = 0;

  std::deque<std::pair<std::uint32_t, std::uint32_t>> pendentes_;  // (pfn, pUser)
};

}  // namespace zb2::brew

#endif  // ZB2_CORE_BREW_IHID_ENTRADA_H
