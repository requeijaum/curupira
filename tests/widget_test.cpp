#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/brew/ajudantes.h"
#include "core/brew/despacho.h"
#include "core/brew/interface.h"
#include "core/brew/widget.h"
#include "core/cpu/arm_interpreter.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"
#include "tools/brew_slots.inc"

namespace zb2::brew {
namespace {

// ===========================================================================
// O WIDGET: `IRootForm` + `IForm` + `IHandler`, e os primeiros pedidos de
// INTERFACE que um titulo faz quando arranca.
//
// A MEDICAO QUE ESTES TESTES REPRODUZEM esta no `tectoy` (`mod/274755/tectoy.mod`),
// tirada com o traco do despacho ligado:
//
//     IRootForm::slot3 r0=0x80060400 r1=0x00000800 r2=0x00005000 r3=0x8007ff48 lr=0x0002f74c
//     IRootForm::slot3 r0=0x80060400 r1=0x00000801 r2=0x00005001 r3=0x00000000 lr=0x00068b2c
//     IRootForm::slot3 r0=0x80060400 r1=0x00000800 r2=0x00005002 r3=0x8007ff4c lr=0x0002f74c
//
// Os `r1`/`r2` NAO sao interpretados de ouvido: saem de `tools/brew_slots.inc`,
// que e gerado dos cabecalhos. `0x800` e `EVT_WDG_GETPROPERTY` (`AEEIWidget.h:65`),
// `0x801` e `EVT_WDG_SETPROPERTY` (`AEEIWidget.h:66`), `0x5000`/`0x5001`/`0x5002`
// sao `WID_FORM`/`WID_TITLE`/`WID_SOFTKEYS` (`AEEIForm.h:33,34,36`).
//
// E a razao de tudo isto esta no desmonte de `tectoy 0x68b68`: depois de pedir o
// widget, o titulo faz `IWidget_Release(widget)` -- com o ponteiro que a SAIDA
// devia ter recebido. Com a saida por escrever (o estado anterior), o ponteiro
// era ZERO e o `ldr r1,[0]` lia a primeira instrucao do `.mod` como vtable.
// ===========================================================================

constexpr std::uint32_t kBase = 0x00000000u;
constexpr std::uint32_t kPilha = 0x80080000u;
constexpr std::uint32_t kHeap = 0x80200000u;
constexpr std::uint32_t kHeapTam = 0x00C00000u;
constexpr std::uint32_t kTabela = 0x80010000u;
constexpr std::uint32_t kBaseDasEntradas = 20000;  // o mesmo numero da bateria
constexpr std::uint32_t kSentinela = 0xFFFFFFF0u;
constexpr std::uint32_t kArg0 = 0x80090000u;
// Um endereco de codigo DENTRO da faixa do modulo, para o callback do
// temporizador passar a guarda do `PrepararCallbackDoTemporizador`.
constexpr std::uint32_t kFuncaoDoTemporizador = 0x00000200u;
constexpr std::uint32_t kContexto = 0xC0FFEE00u;
constexpr std::uint32_t kShell = 0x80020000u;
// OS INDICES DA FAIXA DE SAIDA QUE A BATERIA LIGA AOS SLOTS DO IShell.
//
// Vem da tabela `kWire` de `tools/bateria.cpp`: o slot 11 do IShell
// (`kShell_SetTimer`, do cabecalho) esta ligado ao indice 1520, e o slot 12
// (`kShell_CancelTimer`) ao 1552. O `despacho` decide pelo INDICE, e nao pelo
// slot -- e por isso que a chamada de teste tem de entrar por aqui, como o guest
// entra.
//
// FRAGILIDADE DECLARADA, e ela vai no relatorio: este numero esta escrito TAMBEM
// em `bateria.cpp`, e `bateria.cpp` e partilhado (nao o posso alterar). A guarda
// certa e mover a tabela de cablagem e estes indices para o motor, para haver UMA
// fonte. Enquanto nao for feito, o que este teste prova e a SEMANTICA da
// assinatura (o r1 e a duracao, o r2 a funcao), que e o que ele mede.
constexpr std::uint32_t kSaidaSetTimer = 1520;
constexpr std::uint32_t kSaidaCancelTimer = 1552;

class Bancada {
 public:
  Bancada() {
    saidas_.base = 0xF0000000u;
    saidas_.passo = 4;
    saidas_.quantos = 100000;  // o mesmo da bateria
    saidas_.ativa = true;
    cpu_.ConfigurarSaidas(saidas_);
    al_ = new Alocador(mem_, kHeap, kHeapTam, nullptr);
    despacho_ = new Despacho(mem_, traco_, *al_, vfs_);
    despacho_->DefinirVtableBitmap(saidas_.Endereco(kVtableBitmap));
    despacho_->DefinirVtableFicheiro(saidas_.Endereco(kVtableFileObj));
    // O `InstalarAjudantes` tambem constroi os widgets -- e por isso esta frente
    // nao obriga a mudar `tools/bateria.cpp`.
    despacho_->InstalarAjudantes(saidas_, kTabela);
    despacho_->DefinirFaixaDoModulo(kBase, 0x00100000u);
    // O IShell e os objectos GENERICOS, escritos como a bateria os escreve: e o
    // `ConstruirObjeto` do generico `kIndiceDoRootForm` que poe o `IRootForm` nos
    // indices que o modulo do widget reconhece.
    ConstruirObjeto(mem_, saidas_, kShell, saidas_.Endereco(kVtableShell),
                    kSlotsPorVtable, kBaseDoShell);
    for (std::uint32_t k = 0; k < kNGenericos; ++k) {
      ConstruirObjeto(mem_, saidas_, ObjGenerico(k), saidas_.Endereco(VtGenerico(k)),
                      kSlotsPorVtable, VtGenerico(k));
    }
    cpu_.Repor(kBase, kPilha);
  }
  ~Bancada() {
    delete despacho_;
    delete al_;
  }

  // Uma chamada do guest a um endereco de saida, com os registos como o AAPCS
  // manda. O LR e a sentinela, que e o "retorno para o sistema" desta arvore.
  std::uint32_t ChamaSaida(std::uint32_t indice, std::uint32_t r0, std::uint32_t r1 = 0,
                           std::uint32_t r2 = 0, std::uint32_t r3 = 0,
                           std::uint64_t limite = 1000) {
    cpu_.Set(kR0, r0);
    cpu_.Set(kR1, r1);
    cpu_.Set(kR2, r2);
    cpu_.Set(kR3, r3);
    cpu_.Set(kSP, kArg0);
    cpu_.Set(kLR, kSentinela);
    cpu_.Set(kPC, saidas_.Endereco(indice));
    const ResultadoFase r = despacho_->Correr(cpu_, limite, kArg0);
    (void)r;
    return cpu_.Get(kR0);
  }

  // O pedido do `tectoy`: `HandleEvent(po, evt, w, out)` no objecto IRootForm.
  // Devolve o que ficou escrito na saida.
  std::uint32_t PedeAoRootForm(std::uint32_t evt, std::uint32_t w, std::uint32_t* saida) {
    mem_.Escrever32(kArg0, 0);
    // SEM SAIDA PEDIDA, O `dwParam` VAI A ZERO -- que e exactamente o que a
    // medicao mostra no `SETPROPERTY` do `tectoy` (`r3=0x00000000`). Passar-lhe o
    // endereco do buffer daria um ponteiro de widget ALHEIO, e o modulo recusaria
    // (correctamente) por uma razao que nao e a que este teste mede.
    const std::uint32_t d = (saida != nullptr) ? kArg0 : 0u;
    const std::uint32_t r =
        ChamaSaida(VtGenerico(kIndiceDoRootForm) + brew_slots::kIHandler_HandleEvent,
                   ObjGenerico(kIndiceDoRootForm), evt, w, d);
    if (saida != nullptr) *saida = mem_.Ler32(kArg0);
    return r;
  }

  std::uint32_t ChamaRootFormSlot(std::uint32_t slot, std::uint32_t r0, std::uint32_t r1 = 0,
                                  std::uint32_t r2 = 0, std::uint32_t r3 = 0) {
    return ChamaSaida(VtGenerico(kIndiceDoRootForm) + slot, r0, r1, r2, r3);
  }

  std::uint32_t ChamaWidget(std::uint32_t k, std::uint32_t slot, std::uint32_t r0,
                            std::uint32_t r1 = 0, std::uint32_t r2 = 0,
                            std::uint32_t r3 = 0) {
    return ChamaSaida(kBaseDaFaixaDosWidgets + k * kSlotsPorObjetoDeWidget + slot, r0, r1, r2,
                      r3);
  }

  std::size_t Faltas(const std::string& nome) const {
    const auto& f = traco_.ContagemFaltas();
    const auto it = f.find(nome);
    return it == f.end() ? 0 : static_cast<std::size_t>(it->second);
  }

  Memoria& Mem() { return mem_; }
  Despacho& D() { return *despacho_; }
  ArmInterpreter& Cpu() { return cpu_; }
  const Saidas& S() const { return saidas_; }

 private:
  Memoria mem_;
  Traco traco_{"teste_widget", nullptr};
  Vfs vfs_;
  Alocador* al_ = nullptr;
  Despacho* despacho_ = nullptr;
  Saidas saidas_;
  ArmInterpreter cpu_{mem_, &traco_};
};

// ---------------------------------------------------------------------------
// 1. O indice do objecto da raiz nao e um numero escolhido aqui.
// ---------------------------------------------------------------------------
TEST(Widget, OIndiceDaRaizEBatidoComOAEECLSID_CRootForm) {
  // `AEECRootForm.h:35` -- `#define AEECLSID_CRootForm 0x1028e51`.
  EXPECT_EQ(kIidRootForm, 0x01028e51u);
  // E a tabela de genericos de `interface.cpp` tem de concordar: e ELA que diz
  // qual objecto o `IShell::CreateInstance` devolve. Se a ordem daquela tabela
  // mudar, este teste cai -- e e o unico sitio onde isso se ve, porque a
  // consequencia seria o widget atender os indices de OUTRA interface.
  ASSERT_LT(kIndiceDoRootForm, kNGenericos);
  EXPECT_EQ(kGenericos[kIndiceDoRootForm].iid, kIidRootForm);
  EXPECT_STREQ(kGenericos[kIndiceDoRootForm].nome, "IRootForm");
}

// ---------------------------------------------------------------------------
// 2. A faixa curta RECUSA, e diz qual o indice que falta.
// ---------------------------------------------------------------------------
TEST(Widget, AFaixaDeSaidaCurtaRecusaEDizQualOIndice) {
  Memoria mem;
  Traco traco("teste_widget", nullptr);
  Widgets w(mem, traco);
  Saidas curta;
  curta.base = 0xF0000000u;
  curta.passo = 4;
  curta.quantos = 10;  // nao da nem para um widget
  curta.ativa = true;
  std::string motivo;
  EXPECT_FALSE(w.Construir(curta, &motivo));
  // O MOTIVO TEM DE NOMEAR O INDICE. "nao caber" sem o numero obriga a ir ao
  // codigo; com o numero, a correccao e uma linha.
  EXPECT_NE(motivo.find(std::to_string(kBaseDaFaixaDosWidgets +
                                       kQuantosWidgets * kSlotsPorObjetoDeWidget - 1)),
            std::string::npos)
      << "motivo=" << motivo;
  // E nao pode ter ficado meio-construido: `EMeu` continua falso.
  EXPECT_FALSE(w.EMeu(kBaseDaFaixaDosWidgets));
}

// ---------------------------------------------------------------------------
// 3. As vtables dos widgets apontam para a faixa de saida, slot a slot.
// ---------------------------------------------------------------------------
TEST(Widget, AsVtablesDosWidgetsApontamParaAFaixaDeSaida) {
  Bancada b;
  for (std::uint32_t k = 0; k < kQuantosWidgets; ++k) {
    const std::uint32_t objeto = b.D().WidgetsRef().Widget(k);
    ASSERT_NE(objeto, 0u) << "widget " << k;
    const std::uint32_t vtable = b.Mem().Ler32(objeto);
    // A IBase (slots 0 e 1) e a do despacho, como em todos os objectos ROPI.
    EXPECT_EQ(b.Mem().Ler32(vtable + 0), b.S().Endereco(3)) << "widget " << k;
    EXPECT_EQ(b.Mem().Ler32(vtable + 4), b.S().Endereco(4)) << "widget " << k;
    const std::uint32_t base = kBaseDaFaixaDosWidgets + k * kSlotsPorObjetoDeWidget;
    for (std::uint32_t i = 2; i < kSlotsPorObjetoDeWidget; ++i) {
      EXPECT_EQ(b.Mem().Ler32(vtable + i * 4), b.S().Endereco(base + i))
          << "widget " << k << " slot " << i;
    }
    // E o endereco do objecto NAO esta dentro da faixa de saida: um objecto de
    // dados que caia la dentro seria confundido com um ponteiro de funcao.
    EXPECT_FALSE(b.S().Contem(objeto));
  }
}

TEST(Widget, OsSlotsDeCadaWidgetNaoSeSobrepoem) {
  // Um bloco de 64 indices por objecto. Com blocos a sobrepor-se, o `slot 7` de
  // um widget seria atendido como sendo de OUTRO -- e o defeito seria mudo.
  Bancada b;
  std::vector<std::uint32_t> vistos;
  for (std::uint32_t k = 0; k < kQuantosWidgets; ++k) {
    for (std::uint32_t i = 0; i < kSlotsPorObjetoDeWidget; ++i) {
      const std::uint32_t indice = kBaseDaFaixaDosWidgets + k * kSlotsPorObjetoDeWidget + i;
      EXPECT_FALSE(b.S().Contem(b.S().Endereco(indice)) == false);
      for (std::uint32_t outro : vistos) EXPECT_NE(outro, indice);
      vistos.push_back(indice);
    }
  }
  EXPECT_EQ(vistos.size(), kQuantosWidgets * kSlotsPorObjetoDeWidget);
}

// ---------------------------------------------------------------------------
// 4. A SEQUENCIA DO `tectoy`, chamada a chamada.
// ---------------------------------------------------------------------------
TEST(Widget, OGetPropertyDoWidFormEntregaUmPonteiroValido) {
  Bancada b;
  std::uint32_t form = 0xDEADBEEFu;
  const std::uint32_t r =
      b.PedeAoRootForm(brew_slots::EVT_WDG_GETPROPERTY, brew_slots::WID_FORM, &form);
  // O TITULO COMPARA COM ZERO: num caminho do `tectoy` (`0x68af0 moveq r4,#3`)
  // e no outro (`0x2f74c moveq r0,#3`) a resposta 0 e FALHA. Devolver
  // `AEE_EUNSUPPORTED` (20) passava por acidente; o que NAO passava era a saida
  // por escrever.
  EXPECT_NE(r, 0u) << "o titulo le 0 como falha";
  EXPECT_GE(form, kObjetoDosWidgets);
  EXPECT_LT(form, kFimDosObjetosDeWidget);
  EXPECT_EQ(form, b.D().WidgetsRef().Widget(kWidgetDoForm));
  EXPECT_EQ(b.D().WidgetsRef().WidgetsEntregues(), 1u);
}

TEST(Widget, OGetPropertyDoWidSoftkeysEntregaOUTROWidget) {
  Bancada b;
  std::uint32_t form = 0, teclas = 0;
  b.PedeAoRootForm(brew_slots::EVT_WDG_GETPROPERTY, brew_slots::WID_FORM, &form);
  b.PedeAoRootForm(brew_slots::EVT_WDG_GETPROPERTY, brew_slots::WID_SOFTKEYS, &teclas);
  EXPECT_NE(teclas, 0u);
  // DOIS WIDGETS DIFERENTES. Devolver o mesmo ponteiro para papeis diferentes
  // seria uma mentira que so aparece quando o jogo mexer na extensao de um e
  // estragar a do outro.
  EXPECT_NE(form, teclas);
  EXPECT_EQ(teclas, b.D().WidgetsRef().Widget(kWidgetDasSoftkeys));
}

TEST(Widget, OSetPropertyDoWidTituloVazioEAceite) {
  Bancada b;
  // A medicao: `HandleEvent(po, 0x801, 0x5001, 0)` -- titulo NENHUM.
  const std::uint32_t r =
      b.PedeAoRootForm(brew_slots::EVT_WDG_SETPROPERTY, brew_slots::WID_TITLE, nullptr);
  EXPECT_NE(r, 0u);
  EXPECT_EQ(b.D().WidgetsRef().PropriedadesEscritas(), 1u);
}

TEST(Widget, OHandleEventDoWidgetGuardaACorDeFundo) {
  Bancada b;
  const std::uint32_t widget = b.D().WidgetsRef().Widget(kWidgetDasSoftkeys);
  // A medicao, `tectoy 0x303c8`: `HandleEvent(widget, 0x801, 0x130, 0xff)`.
  // `0x130` e `PROP_BGCOLOR` (`AEEWidgetProperties.h:108`), lido pelo gerador.
  const std::uint32_t r =
      b.ChamaWidget(kWidgetDasSoftkeys, brew_slots::kIHandler_HandleEvent, widget,
                    brew_slots::EVT_WDG_SETPROPERTY, brew_slots::PROP_BGCOLOR, 0xFFu);
  EXPECT_NE(r, 0u) << "o titulo (`0x303ec rsbs`) le 0 como falha";
  const ResumoDeWidget resumo = b.D().WidgetsRef().Resumo(kWidgetDasSoftkeys);
  EXPECT_EQ(resumo.cor_de_fundo, 0xFFu);
}

// ---------------------------------------------------------------------------
// 4b. AS STRUCTS DO GUEST TEM O TAMANHO DO CABECALHO -- E NAO O DOBRO
// ---------------------------------------------------------------------------
//
// MEDIDO NO SDK, e este era o defeito mais caro destes ficheiros:
//
//   `WidgetExtent` (`platform/ui/inc/AEEIWidget.h:38-43`):
//       typedef struct { int width; int height; } WidgetExtent;   -> 8 BYTES
//   `AEERect` (`platform/ui/inc/AEERect.h:23-26`):
//       typedef struct { int16 x,y; int16 dx,dy; } AEERect;       -> 8 BYTES
//
// O modulo escrevia QUATRO palavras de 32 bits (16 bytes) nas duas, e lia quatro
// da `WidgetExtent`. A `AEERect` era lida como quatro `uint32` no
// `core/brew/widget.cpp` e no `core/brew/despacho.cpp` (SetClipRect, GetClipRect,
// DrawRect).
//
// O SINTOMA TEM DUAS METADES, e as duas sao mudas:
//   1. a struct do guest sai com ZEROS onde o titulo espera valores (a largura
//      lia o par (x,y) empacotado, e o `GetExtent` devolvia o que la estava);
//   2. os 8 bytes SEGUINTES sao SOBRESCRITOS -- a struct do guest pode estar em
//      qualquer sitio (pilha, dentro de outro objecto), e o vizinho e que paga.
//
// O teste antigo (`OExtentDoWidgetEEscritoELido`) escrevia e lia quatro `uint32`
// e passava: **ele encodava a mesma suposicao errada**, e uma suite que encoda a
// suposicao nao a pode refutar. Estes dois poem uma SENTINELA nos 8 bytes a
// seguir a struct, que e a unica forma de a escrita fora do sitio aparecer.
constexpr std::uint32_t kSentinelaA = 0x11111111u;
constexpr std::uint32_t kSentinelaB = 0x22222222u;

TEST(Widget, OGetExtentEscreveUmaWidgetExtentDeOitoBytes) {
  Bancada b;
  const std::uint32_t widget = b.D().WidgetsRef().Widget(kWidgetDoTitulo);
  // O widget nasce com o rect do ecra inteiro (`widget.cpp`, `Construir`), e o
  // `WidgetExtent` so tem width e height -- logo 320 e 240.
  b.Mem().Escrever32(kArg0 + 8, kSentinelaA);
  b.Mem().Escrever32(kArg0 + 12, kSentinelaB);
  b.ChamaWidget(kWidgetDoTitulo, brew_slots::kIWidget_GetExtent, widget, kArg0);
  EXPECT_EQ(b.Mem().Ler32(kArg0 + 0), 320u) << "width";
  EXPECT_EQ(b.Mem().Ler32(kArg0 + 4), 240u) << "height";
  EXPECT_EQ(b.Mem().Ler32(kArg0 + 8), kSentinelaA)
      << "o `GetExtent` escreveu 16 bytes numa struct de 8";
  EXPECT_EQ(b.Mem().Ler32(kArg0 + 12), kSentinelaB)
      << "o `GetExtent` escreveu 16 bytes numa struct de 8";
}

TEST(Widget, OSetExtentLeUmaWidgetExtentDeOitoBytes) {
  Bancada b;
  const std::uint32_t widget = b.D().WidgetsRef().Widget(kWidgetDoTitulo);
  b.Mem().Escrever32(kArg0 + 0, 321u);
  b.Mem().Escrever32(kArg0 + 4, 123u);
  b.Mem().Escrever32(kArg0 + 8, kSentinelaA);
  b.Mem().Escrever32(kArg0 + 12, kSentinelaB);
  b.ChamaWidget(kWidgetDoTitulo, brew_slots::kIWidget_SetExtent, widget, kArg0);
  // ANTES DE TUDO: O QUE FICOU GUARDADO. O codigo antigo lia 16 bytes, logo a
  // largura guardada vinha de `[prc+8]` (fora da struct) e o teste antigo nao
  // olhava para ela.
  const ResumoDeWidget depois = b.D().WidgetsRef().Resumo(kWidgetDoTitulo);
  EXPECT_EQ(depois.largura, 321u) << "a largura vem de [prc+0], e nao de fora da struct";
  EXPECT_EQ(depois.altura, 123u) << "a altura vem de [prc+4], e nao de fora da struct";
  // E o que foi guardado e o que o `GetExtent` devolve -- width e height.
  b.Mem().Escrever32(kArg0 + 0, 0);
  b.Mem().Escrever32(kArg0 + 4, 0);
  b.ChamaWidget(kWidgetDoTitulo, brew_slots::kIWidget_GetExtent, widget, kArg0);
  EXPECT_EQ(b.Mem().Ler32(kArg0 + 0), 321u) << "width posto pelo `SetExtent`";
  EXPECT_EQ(b.Mem().Ler32(kArg0 + 4), 123u) << "height posto pelo `SetExtent`";
  EXPECT_EQ(b.Mem().Ler32(kArg0 + 8), kSentinelaA)
      << "a struct do cabecalho tem 8 bytes, e nao 16";
  EXPECT_EQ(b.Mem().Ler32(kArg0 + 12), kSentinelaB)
      << "a struct do cabecalho tem 8 bytes, e nao 16";
}

TEST(Widget, OGetClientRectEscreveUmaAEERectDeOitoBytes) {
  Bancada b;
  // `AEERect`: x, y, dx, dy -- quatro `int16`, 8 bytes (`AEERect.h:23-26`).
  b.Mem().Escrever32(kArg0 + 0, kSentinelaA);
  b.Mem().Escrever32(kArg0 + 4, kSentinelaA);
  b.Mem().Escrever32(kArg0 + 8, kSentinelaA);
  b.Mem().Escrever32(kArg0 + 12, kSentinelaB);
  b.ChamaRootFormSlot(brew_slots::kIRootForm_GetClientRect,
                      ObjGenerico(kIndiceDoRootForm), 0u, kArg0);
  EXPECT_EQ(b.Mem().Ler16(kArg0 + 0), 0u) << "x";
  EXPECT_EQ(b.Mem().Ler16(kArg0 + 2), 0u) << "y";
  EXPECT_EQ(b.Mem().Ler16(kArg0 + 4), static_cast<std::uint16_t>(kLarguraDoEcra)) << "dx";
  EXPECT_EQ(b.Mem().Ler16(kArg0 + 6), static_cast<std::uint16_t>(kAlturaDoEcra)) << "dy";
  EXPECT_EQ(b.Mem().Ler32(kArg0 + 8), kSentinelaA)
      << "o `GetClientRect` escreveu 16 bytes numa struct de 8";
  EXPECT_EQ(b.Mem().Ler32(kArg0 + 12), kSentinelaB)
      << "o `GetClientRect` escreveu 16 bytes numa struct de 8";
}

// ---------------------------------------------------------------------------
// 5. O QUE NAO ESTA IMPLEMENTADO RECUSA E REGISTA (P2)
// ---------------------------------------------------------------------------
TEST(Widget, ODrawDoWidgetRecusaERegista) {
  Bancada b;
  const std::size_t antes = b.D().WidgetsRef().Recusas().size();
  const std::uint32_t r =
      b.ChamaWidget(kWidgetDoFundo, brew_slots::kIWidget_Draw,
                    b.D().WidgetsRef().Widget(kWidgetDoFundo), 0x81000000u);
  // NAO e "devolver sucesso e nao desenhar": um `Draw` que dissesse que sim seria
  // o stub silencioso que descartou 86 377 chamadas de `glCullFace`.
  EXPECT_EQ(r, 0u);
  EXPECT_EQ(b.D().WidgetsRef().Recusas().size(), antes + 1);
  EXPECT_GE(b.D().WidgetsRef().ChamadasDeDesenho(), 1u);
  EXPECT_GE(b.Faltas("IWidget::Draw sem rasterizador"), 1u);
}

TEST(Widget, UmaPropriedadeDesconhecidaRecusaEDeixaNome) {
  Bancada b;
  // `0x7777` nao e `WID_` nenhum nem `FID_` nenhum.
  const std::uint32_t r =
      b.PedeAoRootForm(brew_slots::EVT_WDG_GETPROPERTY, 0x7777u, nullptr);
  EXPECT_EQ(r, 0u) << "FALSE e a resposta do contrato para 'nao atendido'";
  EXPECT_GE(b.Faltas("IRootForm GetProperty desconhecido"), 1u);
}

TEST(Widget, OQueryInterfaceRecusaUmIidDesconhecidoEZeroNoPonteiro) {
  Bancada b;
  b.Mem().Escrever32(kArg0, 0xDEADBEEFu);
  const std::uint32_t r = b.ChamaRootFormSlot(
      brew_slots::kIHandler_QueryInterface, ObjGenerico(kIndiceDoRootForm), 0x12345678u, kArg0);
  EXPECT_EQ(r, static_cast<std::uint32_t>(kAeeClassNotSupported));
  // O PONTEIRO TEM DE FICAR A ZERO. Deixar o valor antigo faria o jogo usar uma
  // interface que nao existe -- e um `IWidget_Draw` sobre lixo nao acusa.
  EXPECT_EQ(b.Mem().Ler32(kArg0), 0u);
  EXPECT_GE(b.Faltas("widget_QueryInterface_recusado"), 1u);
}

TEST(Widget, OQueryInterfaceRespondeAsInterfacesDaHierarquia) {
  Bancada b;
  const std::uint32_t po = ObjGenerico(kIndiceDoRootForm);
  for (std::uint32_t iid : {static_cast<std::uint32_t>(brew_slots::AEEIID_IHandler),
                            static_cast<std::uint32_t>(brew_slots::AEEIID_IForm),
                            static_cast<std::uint32_t>(brew_slots::AEEIID_IRootForm)}) {
    b.Mem().Escrever32(kArg0, 0);
    const std::uint32_t r =
        b.ChamaRootFormSlot(brew_slots::kIHandler_QueryInterface, po, iid, kArg0);
    EXPECT_EQ(r, 0u) << "iid=0x" << std::hex << iid;
    EXPECT_EQ(b.Mem().Ler32(kArg0), po);
  }
}

// ---------------------------------------------------------------------------
// 6. O RAMO DO WIDGET NAO CAI NO RAMO GENERICO
// ---------------------------------------------------------------------------
TEST(Widget, OIRootFormDeixaDeSerContadoComoFalta) {
  Bancada b;
  std::uint32_t saida = 0;
  b.PedeAoRootForm(brew_slots::EVT_WDG_GETPROPERTY, brew_slots::WID_FORM, &saida);
  // O NOME `IRootForm::slot3` E O QUE A BATERIA IMPRIMIA ANTES DESTE MODULO. Se
  // ele voltar a aparecer, o ramo do widget deixou de apanhar o pedido -- e o
  // numero da bateria pioraria sem nada a dizer por que.
  EXPECT_EQ(b.Faltas("IRootForm::slot3"), 0u);
  EXPECT_TRUE(b.D().AtenderWidgets(b.Cpu(), VtGenerico(kIndiceDoRootForm) +
                                              brew_slots::kIHandler_HandleEvent));
}

TEST(Widget, OAddRefEORelaseDaRaizContinuamNaIBase) {
  Bancada b;
  const std::uint32_t po = ObjGenerico(kIndiceDoRootForm);
  const std::uint32_t antes = b.Mem().Ler32(po + 4);
  b.ChamaSaida(3, po);  // AddRef, o endereco global (igual para todos os objectos)
  EXPECT_EQ(b.Mem().Ler32(po + 4), antes + 1);
  b.ChamaSaida(4, po);  // Release
  EXPECT_EQ(b.Mem().Ler32(po + 4), antes);
}

// ---------------------------------------------------------------------------
// 7. A ASSINATURA DO `SetTimer` -- o segundo argumento e a DURACAO
// ---------------------------------------------------------------------------
TEST(Widget, OSetTimerLeADuracaoDoR1EAFuncaoDoR2) {
  Bancada b;
  // `AEEIShell.h:299`: `int (*SetTimer)(iname *po, int32 dwMsecs,
  // void (*pfn)(void *), void *pUser)`. E a linha do `asq` em `0x8cf34`:
  // `mov r1,#100` e `ldr r2,[pc,#572]` (um endereco de CODIGO) com `mov r3,r5`.
  const std::uint32_t r = b.ChamaSaida(kSaidaSetTimer, kShell, 100u,
                                       kFuncaoDoTemporizador, kContexto);
  EXPECT_EQ(r, static_cast<std::uint32_t>(kAeeSuccess));
  EXPECT_TRUE(b.D().TemporizadorArmado());
  EXPECT_EQ(b.D().CallbackDoTemporizador(), kFuncaoDoTemporizador);
  EXPECT_EQ(b.D().ContextoDoTemporizador(), kContexto);
}

TEST(Widget, OCallbackDoTemporizadorCorreAFuncaoComOContexto) {
  Bancada b;
  b.ChamaSaida(kSaidaSetTimer, kShell, 100u, kFuncaoDoTemporizador, kContexto);
  ASSERT_TRUE(b.D().PrepararCallbackDoTemporizador(b.Cpu()));
  // O contrato do SDK: o callback e chamado com o `pUser` no r0 e volta para a
  // sentinela. Um par trocado aqui da um callback que arranca com o contexto
  // errado -- e o laco de quadro do jogo nunca se re-arma.
  EXPECT_EQ(b.Cpu().Get(kPC), kFuncaoDoTemporizador);
  EXPECT_EQ(b.Cpu().Get(kR0), kContexto);
  EXPECT_EQ(b.Cpu().Get(kLR), kSentinela);
  // E o pedido NAO fica armado outra vez: quem re-arma e o proprio callback.
  EXPECT_FALSE(b.D().TemporizadorArmado());
}

TEST(Widget, OSetTimerComARelacaoTrocadaNaoArmaOLaco) {
  Bancada b;
  // A VIOLACAO DELIBERADA DA ORDEM ANTIGA: com o periodo no r2 e a funcao no r1,
  // o par lido fica trocado. Este teste existe para que a correccao nao possa
  // voltar atras em silencio -- ele fica VERDE com a assinatura certa e VERMELHO
  // se alguem voltar a ler o r1 como funcao.
  b.ChamaSaida(kSaidaSetTimer, kShell, kFuncaoDoTemporizador, 100u, 0);
  EXPECT_EQ(b.D().CallbackDoTemporizador(), 100u)
      << "se isto passar a ser a funcao, a assinatura foi trocada outra vez";
  EXPECT_EQ(b.D().ContextoDoTemporizador(), 0u);
}


// ---------------------------------------------------------------------------
// 8. O `FID_ACTIVE` E O `FID_VISIBLE`: ACEITAR SEM APLICAR TEM DE DEIXAR RASTO
// ---------------------------------------------------------------------------
//
// MEDIDO em `core/brew/widget.cpp`, e o defeito e de INCONSISTENCIA DENTRO DA
// MESMA FUNCAO: o `FID_THEME` (linhas seguintes no mesmo `if`) REGISTA que o tema
// ficou por aplicar -- "um TRUE com o tema por aplicar e uma meia-verdade que fica
// escrita" -- e o `FID_ACTIVE`/`FID_VISIBLE`, seis linhas acima, devolviam TRUE e
// nao deixavam rasto NENHUM com o nome da propriedade.
//
// O SINTOMA: `escritas_` e um contador AGREGADO. Quem le a bateria sabe que
// "alguma propriedade foi escrita", e nao QUAL. E um titulo que ligue a
// visibilidade de uma forma fica a acreditar que ela esta visivel num emulador
// que nao tem rasterizador de widgets -- sem uma linha que o diga.
//
// Este e o caso de teste do `imedia` (P2): o que se aceita e NAO se aplica tem de
// deixar um evento com o NOME.
TEST(Widget, OSetPropertyDeFidActiveDeixaRastoComONome) {
  Bancada b;
  const std::uint32_t r =
      b.PedeAoRootForm(brew_slots::EVT_WDG_SETPROPERTY, brew_slots::FID_ACTIVE, nullptr);
  // TRUE e a resposta do contrato (o pedido foi entendido), como no tema.
  EXPECT_NE(r, 0u);
  EXPECT_GE(b.Faltas("IRootForm FID_ACTIVE nao aplicado"), 1u)
      << "TRUE sem rasto: e a meia-verdade que o FID_THEME, no mesmo `if`, ja nao "
         "pode ser";
}

TEST(Widget, OAppStartNaRaizGuardaEDevolveTrue) {
  Bancada b;
  // AEEAppStart em kArg0: error=0, clsApp, display.
  b.Mem().Escrever32(0x80100000u, 0);
  b.Mem().Escrever32(0x80100004u, 0x0100104fu);
  b.Mem().Escrever32(0x80100008u, 0x80030000u);
  b.Cpu().Set(kR0, 0x80060400u);
  b.Cpu().Set(kR1, 0);
  b.Cpu().Set(kR2, 0);
  b.Cpu().Set(kR3, 0x80100000u);
  const std::uint32_t antes = b.D().WidgetsRef().ArranquesNaRaiz();
  const std::uint32_t r = b.ChamaRootFormSlot(
      brew_slots::kIHandler_HandleEvent, 0x80060400u, 0, 0, 0x80100000u);
  EXPECT_NE(r, 0u);
  EXPECT_EQ(b.D().WidgetsRef().ArranquesNaRaiz(), antes + 1);
  EXPECT_EQ(b.Faltas("IRootForm HandleEvent nao atendido"), 0u);
}

TEST(Widget, OQueryClassRespondeOTituloEAsClassesServidas) {
  Bancada b;
  // AppHistory e classe servida -> TRUE mesmo sem CLSID do titulo.
  EXPECT_EQ(b.ChamaSaida(1541, 0x80020000u, 0x0100104fu), 1u);
  EXPECT_EQ(b.ChamaSaida(1541, 0x80020000u, 0x12345678u), 0u);
  EXPECT_EQ(b.ChamaSaida(1541, 0x80020000u, 0x01001001u), 1u);
  b.D().SituarTitulo("d", "p", 0x010292c3u);
  EXPECT_EQ(b.ChamaSaida(1541, 0x80020000u, 0x010292c3u), 1u);
  EXPECT_EQ(b.ChamaSaida(1541, 0x80020000u, 0x01001017u), 1u);
  EXPECT_EQ(b.ChamaSaida(1541, 0x80020000u, 0x01030766u), 1u);
  EXPECT_EQ(b.ChamaSaida(1541, 0x80020000u, 0x12345678u), 0u);
}

TEST(Widget, OGetClassEscreveOClsidDoTitulo) {
  Bancada b;
  constexpr std::uint32_t kGetClass =
      40000 + 0 * 32 + brew_slots::kAppHistory_GetClass;
  // Sem CLSID: EFAILED (1), sem escrever.
  b.Mem().Escrever32(0x80100000u, 0xDEADBEEFu);
  EXPECT_EQ(b.ChamaSaida(kGetClass, 0x8F000000u, 0x80100000u), 1u);
  EXPECT_EQ(b.Mem().Ler32(0x80100000u), 0xDEADBEEFu);
  // pcls nulo: EBADPARM (14).
  EXPECT_EQ(b.ChamaSaida(kGetClass, 0x8F000000u, 0), 14u);
  // Com CLSID: SUCCESS + escrito.
  b.D().SituarTitulo("d", "p", 0x010292c3u);
  EXPECT_EQ(b.ChamaSaida(kGetClass, 0x8F000000u, 0x80100000u), 0u);
  EXPECT_EQ(b.Mem().Ler32(0x80100000u), 0x010292c3u);
}

TEST(Widget, OSetPropertyDeFidVisibleDeixaRastoComONome) {
  Bancada b;
  const std::uint32_t r =
      b.PedeAoRootForm(brew_slots::EVT_WDG_SETPROPERTY, brew_slots::FID_VISIBLE, nullptr);
  EXPECT_NE(r, 0u);
  EXPECT_GE(b.Faltas("IRootForm FID_VISIBLE nao aplicado"), 1u)
      << "TRUE sem rasto: o emulador nao tem modelo de visibilidade e nao o diz";
}

}  // namespace
}  // namespace zb2::brew
