#ifndef ZB2_CORE_BREW_WIDGET_H
#define ZB2_CORE_BREW_WIDGET_H

// A HIERARQUIA DE WIDGETS DO BREW: `IRootForm` -> `IForm` -> `IHandler`, e
// `IWidget` -> `IHandler` (com `IDrawDecorator` -> `IDecorator` -> `IWidget`).
//
// PORQUE ISTO EXISTE, e a medicao que o pediu.
//
// Depois de a bateria passar a entregar `EVT_APP_START` ao `HandleEvent` do
// applet (commit `895beb1`), apareceu uma camada que nunca se tinha visto: os
// titulos que ARRANCAM tentam construir a propria interface, e pedem `IRootForm`.
// MEDIDO no `tectoy` (o Z-Wheel, `mod/274755/tectoy.mod`), tres pedidos ao slot 3
// do objecto que o `CreateInstance` devolveu para `AEECLSID_CRootForm`:
//
//     r0=0x80060400 r1=0x00000800 r2=0x00005000 r3=0x8007ff48  lr=0x0002f74c
//     r0=0x80060400 r1=0x00000801 r2=0x00005001 r3=0x00000000  lr=0x00068b2c
//     r0=0x80060400 r1=0x00000800 r2=0x00005002 r3=0x8007ff4c  lr=0x0002f74c
//
// Os numeros NAO sao interpretados de ouvido: vem dos cabecalhos, por
// `tools/gerar_slots.py`, e estao em `tools/brew_slots.inc`:
//     slot 3 de IRootForm  = `HandleEvent`      (IHandler = INHERIT_IQI(3)+2)
//     r1 = 0x800           = `EVT_WDG_GETPROPERTY`   (AEEIWidget.h:65)
//     r1 = 0x801           = `EVT_WDG_SETPROPERTY`   (AEEIWidget.h:66)
//     r2 = 0x5000          = `WID_FORM`         (AEEIForm.h:33, `PROP_FORM + 0`)
//     r2 = 0x5001          = `WID_TITLE`        (AEEIForm.h:34)
//     r2 = 0x5002          = `WID_SOFTKEYS`     (AEEIForm.h:36)
//
// E o desmonte do `tectoy` mostra o que ele faz com a resposta, em `0x68acc`:
//
//     68ae0  ldr  r0, [r5, #36]     ; o objecto IRootForm que ele guardou
//     68ae8  mov  r1, #0x5000       ; WID_FORM
//     68aec  bl   0x2f72c           ; HandleEvent(po, EVT_WDG_GETPROPERTY, r1, &saida)
//     68af0  movs r4, r0            ; <<< a resposta: 0 = FALHA para este titulo
//     68b58  ldr  r0, [sp, #4]      ; o widget que a saida devia ter recebido
//     68b60  bl   0x303c8           ; HandleEvent(widget, EVT_WDG_SETPROPERTY, 0x130, 0xff)
//     68b68  ldr  r0, [sp, #4]      ; e depois IWidget_Release(widget)
//     68b74  ldr  r1, [r1, #4]      ; slot 1 = Release
//
// Ou seja: **a saida tem de ser ESCRITA com um ponteiro valido**. No estado
// anterior a este modulo, o despacho devolvia `AEE_EUNSUPPORTED` (20) sem
// escrever a saida; o titulo ficava com ZERO no ponteiro e ia buscar a "vtable"
// ao endereco 0 -- que e, no `.mod`, a primeira instrucao ARM, e o `bx` para ela
// sai do modulo. Isso esta medido nos outros titulos como
// `saiu_do_modulo_para_0x...` (o `alpineracerex`, por exemplo).
//
// ---------------------------------------------------------------------------
// O QUE ESTE MODULO **NAO** FAZ
// ---------------------------------------------------------------------------
// Nao ha rasterizador de widgets, nao ha ficheiro de tema (`IResFile`) e nao ha
// imagens. Por isso:
//   - as propriedades de APARENCIA sao GUARDADAS e devolvidas, mas nao pintam
//     nada: pinta-las seria o "retangulo que o emulador desenha", e a bateria
//     mediria o emulador a desenhar-se a si proprio;
//   - `IWidget::Draw` RECUSA e REGISTA (P2). Um `Draw` que devolvesse sucesso sem
//     desenhar seria exactamente o stub silencioso que descartou 86 377 chamadas
//     de `glCullFace` na arvore antiga;
//   - `ResolveForm` (URL `form:`) RECUSA: nao ha `ISHELL_RegisterHandler`.
// Tudo o que recusa fica nomeado na contagem de faltas, e aparece no relatorio
// da bateria.

#include <cstdint>
#include <string>
#include <vector>

#include "core/brew/interface.h"
#include "core/cpu/cpu.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"
#include "tools/brew_slots.inc"

namespace zb2::brew {

// `AEECLSID_CRootForm` -- `platform/ui/inc/AEECRootForm.h:35`,
// `#define AEECLSID_CRootForm 0x1028e51`. E o IID que o `IShell::CreateInstance`
// do corpus pede para construir a interface, e o indice dele na tabela
// `kGenericos` de `interface.cpp` e o que diz QUAL objecto o titulo recebe.
constexpr std::uint32_t kIidRootForm = 0x01028e51u;
constexpr std::uint32_t kIndiceDoRootForm = 4;

// ---------------------------------------------------------------------------
// A faixa de saida deste modulo
// ---------------------------------------------------------------------------
//
// UM BLOCO DE 64 INDICES POR OBJECTO DE WIDGET. E preciso um bloco por objecto
// porque o despacho decide pelo INDICE: com uma so vtable para todos, o `slot 7`
// nao diria de QUE widget era -- e saber isso e o proposito todo do endereco por
// slot (o mesmo defeito do "slot_de_saida_2" sem nome, ja medido nesta arvore).
//
// O bloco da raiz NAO esta aqui: o `IRootForm` e o objecto generico que o
// `ConstruirObjeto` do `bateria.cpp` ja escreveu, e os indices dele sao
// `VtGenerico(kIndiceDoRootForm) + slot`. Inventar um segundo objecto obrigaria
// a mudar a ferramenta; reconhecer os indices existentes nao.
constexpr std::uint32_t kBaseDaFaixaDosWidgets = 60000;
constexpr std::uint32_t kSlotsPorObjetoDeWidget = 64;

// Os widgets que o modulo constroi, e para que propriedade cada um serve.
// A ORDEM e a ordem de `construir`, e o indice do bloco e o indice nesta tabela.
enum : std::uint32_t {
  kWidgetDoForm = 0,      // `WID_FORM` e `WID_CONTAINER` -- o contentor raiz
  kWidgetDoTitulo = 1, // `WID_TITLE`
  kWidgetDasSoftkeys = 2, // `WID_SOFTKEYS`
  kWidgetDoFundo = 3,     // `WID_BACKGROUND`
};
constexpr std::uint32_t kQuantosWidgets = 4;

// Onde os objectos ficam na memoria do guest. DECLARADO, e fora de tudo o que
// existe: a faixa do modulo e `0..tamanho` (base ZERO, medida), o heap comeca em
// `0x80200000`, a `interface.h` ocupa `0x8001xxxx`..`0x8007xxxx` e a entrada
// ocupa `0x8103xxxx`/`0x8104xxxx` (ver `core/brew/ihid_entrada.h`).
constexpr std::uint32_t kVtableDosWidgets = 0x81060000u;
constexpr std::uint32_t kObjetoDosWidgets = 0x81070000u;
constexpr std::uint32_t kPassoDoObjetoDeWidget = 0x100u;
// O fim da faixa dos objectos: uma guarda, para um pedido fora dela RECUSAR em
// vez de escrever num sitio qualquer.
constexpr std::uint32_t kFimDosObjetosDeWidget =
    kObjetoDosWidgets + kQuantosWidgets * kPassoDoObjetoDeWidget;

// O que um objecto de widget NOSSO guarda, por deslocamento. E uma ESCOLHA
// nossa (o objecto e criado por nos), e esta escrita aqui porque os testes e o
// despacho a usam.
//
//     +0  vtable
//     +4  contagem de referencias   (a convencao de `ConstruirObjeto`)
//     +8  IWidget *pai
//     +12 AEERect: x
//     +16 AEERect: y
//     +20 AEERect: dx
//     +24 AEERect: dy
//     +28 RGBVAL cor de fundo (`PROP_BGCOLOR`)
//     +32 RGBVAL cor de frente (`PROP_FGCOLOR`)
//     +36 IModel *modelo
//     +40 `HandlerDesc` alojado: pfn
//     +44 `HandlerDesc` alojado: pCxt
//     +48 `HandlerDesc` alojado: pfnFree
constexpr std::uint32_t kW_Pai = 8;
constexpr std::uint32_t kW_Rect = 12;
constexpr std::uint32_t kW_CorDeFundo = 28;
constexpr std::uint32_t kW_CorDeFrente = 32;
constexpr std::uint32_t kW_Modelo = 36;
constexpr std::uint32_t kW_Handler = 40;
constexpr std::uint32_t kTamanhoDoObjetoDeWidget = 0x40;

// A largura e a altura DECLARADAS do ecra. Sao as mesmas que o
// `IShell::GetDeviceInfo` publica (320x240) -- uma so verdade para o tamanho do
// ecra dentro do emulador.
constexpr std::uint32_t kLarguraDoEcra = 320;
constexpr std::uint32_t kAlturaDoEcra = 240;

// ---------------------------------------------------------------------------
// O resultado de um pedido
// ---------------------------------------------------------------------------
enum class Atendido {
  NaoEMeu,        // o indice nao e desta faixa: quem chamou decide
  Feito,          // atendido -- nao regista nada
  NaoImplementado,// atendido com RECUSA, e ja registado (P2)
};

// O estado de um widget, do lado de fora da memoria do guest. Serve para os
// testes e para o relatorio poder dizer quantas propriedades foram guardadas.
struct ResumoDeWidget {
  std::uint32_t objeto = 0;
  std::uint32_t pai = 0;
  std::uint32_t cor_de_fundo = 0;
  std::uint32_t modelo = 0;
  bool tem_handler = false;
  std::uint32_t handler_pfn = 0;
  std::uint32_t handler_pcxt = 0;
};

class Widgets {
 public:
  Widgets(Memoria& mem, Traco& traco);

  // Constroi os widgets e escreve as vtables deles na memoria do guest.
  //
  // RECUSA (e diz qual o indice que falta) quando a faixa de saida nao tem
  // espaco para `kQuantosWidgets * kSlotsPorObjetoDeWidget` indices -- escrever
  // fora da faixa daria um ponteiro de funcao que nunca e reconhecido, e o modulo
  // atenderia ZERO chamadas em silencio. Um limite que nao recusa nao e limite.
  bool Construir(const Saidas& saidas, std::string* motivo);

  // O indice pertence a este modulo?
  bool EMeu(std::uint32_t indice) const;

  // Atende um pedido da faixa de saida. Ver `Atendido`.
  Atendido Atender(ICpu& cpu, std::uint32_t indice);

  // O objecto `IRootForm` (o generico de `kIndiceDoRootForm`) e os widgets.
  std::uint32_t ObjetoDaRaiz() const;
  std::uint32_t Widget(std::uint32_t k) const;

  // Os dois ramos do `Atender`, PUBLICOS porque o TESTE os chama directamente,
  // com os MESMOS argumentos que o `tectoy` passou. Um teste que corre pela mesma
  // porta que a medicao prova alguma coisa; um teste que corre por uma porta
  // paralela prova que o teste e consistente consigo mesmo.
  Atendido AtenderRootForm(ICpu& cpu, std::uint32_t slot);
  Atendido AtenderWidget(ICpu& cpu, std::uint32_t k, std::uint32_t slot);

  // --- o que se mediu (P7: o instrumento e um produto) -------------------
  std::uint32_t PropriedadesLidas() const { return lidas_; }
  std::uint32_t PropriedadesEscritas() const { return escritas_; }
  std::uint32_t WidgetsEntregues() const { return entregues_; }
  std::uint32_t ChamadasDeDesenho() const { return desenhos_; }

  ResumoDeWidget Resumo(std::uint32_t k) const;

  // A pilha de formas: `InsertForm`/`RemoveForm`/`GetForm`.
  std::size_t FormasNaPilha() const { return pilha_.size(); }
  std::uint32_t FormaNoTopo() const { return pilha_.empty() ? 0 : pilha_.back(); }

  // Tudo o que ESTE modulo recusou, por nome. O `Traco` ja tem a contagem global;
  // esta lista e local, para um teste poder afirmar sobre ela sem depender da
  // ferramenta -- e para o relatorio poder dizer o que ficou por fazer sem
  // varrer o traco inteiro.
  const std::vector<std::string>& Recusas() const { return recusas_; }

 private:
  // O `HandlerDesc` do SDK: { PFNHANDLER pfn; void *pCxt; PFNFREEHANDLER pfnFree }
  // -- `AEEIHandler.h:38..42`, tres palavras.
  struct DescritorDeHandler {
    std::uint32_t pfn = 0, cxt = 0, livre = 0;
  };
  DescritorDeHandler LerHandler(std::uint32_t desc) const;
  static void EscreverHandler(Memoria& mem, std::uint32_t desc,
                              const DescritorDeHandler& h);
  Atendido QueryInterfaceDe(ICpu& cpu, bool com_widget, std::uint32_t po);
  // O widget que OCUPA o papel, ou o que o modulo construiu para ele quando
  // ninguem o atribuiu. A raiz CONSTROI os tres elementos comuns (titulo,
  // softkeys, fundo): "These interface elements are owned by the root form"
  // (`AEEIRootForm.h`). Devolver ZERO quando o jogo nunca os atribuiu seria
  // dizer que o `IRootForm` nao tem titulo -- e o jogo pede-os logo no arranque.
  std::uint32_t Papel(std::uint32_t papel, std::uint32_t k) const;

  Memoria& mem_;
  Traco& traco_;
  bool pronto_ = false;
  std::uint32_t ObjetoDaRaiz_ = 0;
  // Os papeis da raiz: que widget ocupa cada `WID_`. Zero = nenhum.
  std::uint32_t papel_form_ = 0;
  std::uint32_t papel_titulo_ = 0;
  std::uint32_t papel_softkeys_ = 0;
  std::uint32_t papel_fundo_ = 0;
  std::uint32_t display_ = 0;
  // O `HandlerDesc` instalado na raiz (`IHandler::SetHandler`).
  std::uint32_t pfn_ = 0, cxt_ = 0, livre_ = 0;
  // A pilha de formas (`IRootForm`). Guarda os ponteiros que o jogo empurrou.
  std::vector<std::uint32_t> pilha_;
  std::uint32_t lidas_ = 0, escritas_ = 0, entregues_ = 0, desenhos_ = 0;
  std::vector<std::string> recusas_;
};

}  // namespace zb2::brew

#endif  // ZB2_CORE_BREW_WIDGET_H
