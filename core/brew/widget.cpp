#include "core/brew/widget.h"

#include <cstdio>

#include "core/brew/ajudantes.h"

// Ver `widget.h` para a medicao que pediu este modulo, e para o que ele NAO faz.

namespace zb2::brew {

// As constantes geradas vivem em `brew_slots` (o mesmo namespace de
// `tools/brew_slots.inc`). Aqui elas sao usadas SEM prefixo de proposito: o
// ficheiro fica legivel e a origem continua a ser UMA -- o `.inc` gerado.
using namespace brew_slots;

namespace {

// Os codigos de erro vem do enum de `core/brew/ajudantes.h` -- UMA fonte so.
// Este modulo tinha uma copia propria deles em duas das frentes anteriores, e a
// copia fez o que as copias fazem.

// --- AS STRUCTS DO GUEST TEM O TAMANHO DO CABECALHO --------------------------
//
// MEDIDO, e este era o defeito mais caro deste ficheiro:
//
//   `AEERect` (`platform/ui/inc/AEERect.h:23-26`):
//       typedef struct { int16 x, y; int16 dx, dy; } AEERect;        -> 8 BYTES
//   `WidgetExtent` (`platform/ui/inc/AEEIWidget.h:38-43`):
//       typedef struct { int width; int height; } WidgetExtent;      -> 8 BYTES
//
// Aqui escrevia-se QUATRO palavras de 32 bits (16 bytes) nas duas, e lia-se
// quatro da `WidgetExtent`. O comentario que la estava dizia "quatro `int32`
// seguidos ... (`AEEStdDef.h`)" -- e o `AEEStdDef.h` nao define a `AEERect`.
//
// O sintoma tem duas metades, e as duas sao mudas: (a) a struct do guest sai com
// outros valores (o campo 1 lia o par (x,y) empacotado); (b) os 8 bytes SEGUINTES
// sao sobrescritos -- e a struct do guest pode estar na pilha ou dentro de outro
// objecto, logo o vizinho e que paga.
//
// So se ve com uma SENTINELA a seguir a struct: o teste antigo escrevia e lia
// quatro `uint32` e passava, porque **encodava a mesma suposicao errada**.

// A `AEERect` DO GUEST: quatro `int16`. E ESTA que se usa quando o ponteiro vem
// do titulo (`IRootForm::GetClientRect`).
void EscreverRectDoGuest(Memoria& mem, std::uint32_t onde, std::uint32_t x, std::uint32_t y,
                         std::uint32_t dx, std::uint32_t dy) {
  if (onde == 0) return;
  mem.Escrever16(onde + 0, static_cast<std::uint16_t>(x));
  mem.Escrever16(onde + 2, static_cast<std::uint16_t>(y));
  mem.Escrever16(onde + 4, static_cast<std::uint16_t>(dx));
  mem.Escrever16(onde + 6, static_cast<std::uint16_t>(dy));
}

// O rect DENTRO DO NOSSO objecto de widget (`widget.h`, `kW_Rect`): quatro
// `uint32` no nosso esquema, que nao e o do guest. Duas coisas diferentes com o
// mesmo nome foram o que produziu o defeito -- por isso sao duas funcoes com
// nomes diferentes.
void EscreverRectNoObjeto(Memoria& mem, std::uint32_t onde, std::uint32_t x, std::uint32_t y,
                          std::uint32_t dx, std::uint32_t dy) {
  if (onde == 0) return;
  mem.Escrever32(onde + 0, x);
  mem.Escrever32(onde + 4, y);
  mem.Escrever32(onde + 8, dx);
  mem.Escrever32(onde + 12, dy);
}

// `WidgetExtent`: dois `int` -- LARGURA e ALTURA, e nao um rect.
void EscreverExtent(Memoria& mem, std::uint32_t onde, std::uint32_t largura,
                    std::uint32_t altura) {
  if (onde == 0) return;
  mem.Escrever32(onde + 0, largura);
  mem.Escrever32(onde + 4, altura);
}

void LerExtent(const Memoria& mem, std::uint32_t onde, std::uint32_t* largura,
               std::uint32_t* altura) {
  *largura = onde == 0 ? 0 : mem.Ler32(onde + 0);
  *altura = onde == 0 ? 0 : mem.Ler32(onde + 4);
}

}  // namespace

Widgets::Widgets(Memoria& mem, Traco& traco) : mem_(mem), traco_(traco) {}

std::uint32_t Widgets::Widget(std::uint32_t k) const {
  if (k >= kQuantosWidgets) return 0;
  return kObjetoDosWidgets + k * kPassoDoObjetoDeWidget;
}

std::uint32_t Widgets::ObjetoDaRaiz() const { return ObjetoDaRaiz_; }

bool Widgets::Construir(const Saidas& saidas, std::string* motivo) {
  if (pronto_) {
    if (motivo != nullptr) *motivo = "os widgets ja estavam construidos";
    return false;
  }
  // A FAIXA TEM DE CABER. `ConstruirObjeto` escreve
  // `saidas.Endereco(base + i)` para cada slot: um indice fora de `quantos` da um
  // endereco que o laco de execucao NUNCA reconhece como saída, e o pedido do
  // titulo passa a nao ser atendido -- em SILENCIO, que e a pior especie.
  const std::uint32_t ultimo =
      kBaseDaFaixaDosWidgets + kQuantosWidgets * kSlotsPorObjetoDeWidget - 1;
  if (ultimo >= saidas.quantos) {
    if (motivo != nullptr) {
      *motivo = "a faixa de saida tem " + std::to_string(saidas.quantos) +
                " indices e os widgets precisam do indice " + std::to_string(ultimo);
    }
    return false;
  }

  // O objecto da RAIZ e o generico de `kIndiceDoRootForm` -- o mesmo que o
  // `IShell::CreateInstance` devolve para `AEECLSID_CRootForm`, e o que o
  // `bateria.cpp` ja construiu com `ConstruirObjeto`. Aqui so se TOMA NOTA do
  // endereco e dos indices; nao se constroi um segundo objecto, porque um segundo
  // objecto obrigaria a mudar a ferramenta e a existirem duas verdades sobre o
  // `IRootForm`.
  ObjetoDaRaiz_ = ObjGenerico(kIndiceDoRootForm);

  for (std::uint32_t k = 0; k < kQuantosWidgets; ++k) {
    const std::uint32_t objeto = kObjetoDosWidgets + k * kPassoDoObjetoDeWidget;
    const std::uint32_t vtable = kVtableDosWidgets + k * kPassoDoObjetoDeWidget;
    ConstruirObjeto(mem_, saidas, objeto, vtable, kSlotsPorObjetoDeWidget,
                    kBaseDaFaixaDosWidgets + k * kSlotsPorObjetoDeWidget);
    // O CORPO do objecto. O `ConstruirObjeto` escreve o cabecalho ROPI (vtable e
    // contagem de referencias); o resto e deste modulo, e fica DECLARADO em
    // `widget.h`.
    mem_.Escrever32(objeto + kW_Pai, 0);
    // O rect começa no ecra inteiro: um widget sem `SetExtent` tem de ter uma
    // extensao definida, e "toda a tela" e a resposta que o cabecalho descreve
    // para o contentor raiz.
    EscreverRectNoObjeto(mem_, objeto + kW_Rect, 0, 0, kLarguraDoEcra, kAlturaDoEcra);
    mem_.Escrever32(objeto + kW_CorDeFundo, 0);
    mem_.Escrever32(objeto + kW_CorDeFrente, 0);
    mem_.Escrever32(objeto + kW_Modelo, 0);
    mem_.Escrever32(objeto + kW_Handler + 0, 0);
    mem_.Escrever32(objeto + kW_Handler + 4, 0);
    mem_.Escrever32(objeto + kW_Handler + 8, 0);
  }

  // LEITURA DE VOLTA da cablagem: cada slot 2..63 da vtable de cada widget tem
  // de apontar para o indice que este modulo espera. Sem isto, uma cablagem
  // perdida so apareceria como "o jogo nao pede nada" -- que foi o sintoma exacto
  // do `SetTimer` perdido, e custou uma corrida inteira.
  for (std::uint32_t k = 0; k < kQuantosWidgets; ++k) {
    const std::uint32_t vtable = kVtableDosWidgets + k * kPassoDoObjetoDeWidget;
    const std::uint32_t base = kBaseDaFaixaDosWidgets + k * kSlotsPorObjetoDeWidget;
    for (std::uint32_t i = 2; i < kSlotsPorObjetoDeWidget; ++i) {
      const std::uint32_t lido = mem_.Ler32(vtable + i * 4);
      if (lido != saidas.Endereco(base + i)) {
        traco_.RegistarFalta(Area::Brew, "cablagem_dos_widgets_perdida",
                             "vtable " + Hex(vtable) + " slot " +
                                 std::to_string(i));
        if (motivo != nullptr) *motivo = "cablagem dos widgets perdida";
        return false;
      }
    }
  }

  pronto_ = true;
  traco_.Emitir(Area::Brew, Nivel::Informacao, "WIDGETS_CONSTRUIDOS",
                "raiz=" + Hex(ObjetoDaRaiz_) + " widgets=" +
                    std::to_string(kQuantosWidgets) + " faixa=" +
                    std::to_string(kBaseDaFaixaDosWidgets) + ".." +
                    std::to_string(ultimo));
  return true;
}

bool Widgets::EMeu(std::uint32_t indice) const {
  // UM MODULO QUE NAO SE CONSTRUIU NAO RECLAMA INDICE NENHUM. Sem esta guarda,
  // uma construcao recusada (faixa curta, cablagem perdida) deixaria o modulo a
  // dizer "este indice e meu" e a atender com objectos por construir -- o pior
  // dos dois mundos: o pedido nao chega ao ramo generico (que o nomearia) e nao
  // e servido.
  if (!pronto_) return false;
  // Os objectos de widget deste modulo.
  if (indice >= kBaseDaFaixaDosWidgets &&
      indice < kBaseDaFaixaDosWidgets + kQuantosWidgets * kSlotsPorObjetoDeWidget) {
    return true;
  }
  // O objecto da raiz: os indices do generico de `kIndiceDoRootForm`. Os slots 0
  // e 1 NAO sao daqui -- o `ConstruirObjeto` aponta-os para o `AddRef`/`Release`
  // do despacho, e escrever por cima destruiria a IBase.
  const std::uint32_t base = VtGenerico(kIndiceDoRootForm);
  return indice >= base + 2 && indice < base + kSlotsPorObjetoDeWidget;
}

Atendido Widgets::Atender(ICpu& cpu, std::uint32_t indice) {
  if (!pronto_) return Atendido::NaoEMeu;
  const std::uint32_t base = VtGenerico(kIndiceDoRootForm);
  if (indice >= base + 2 && indice < base + kSlotsPorObjetoDeWidget) {
    return AtenderRootForm(cpu, indice - base);
  }
  if (indice >= kBaseDaFaixaDosWidgets &&
      indice < kBaseDaFaixaDosWidgets + kQuantosWidgets * kSlotsPorObjetoDeWidget) {
    const std::uint32_t delta = indice - kBaseDaFaixaDosWidgets;
    return AtenderWidget(cpu, delta / kSlotsPorObjetoDeWidget,
                         delta % kSlotsPorObjetoDeWidget);
  }
  return Atendido::NaoEMeu;
}

Widgets::DescritorDeHandler Widgets::LerHandler(std::uint32_t desc) const {
  DescritorDeHandler h;
  if (desc == 0) return h;
  h.pfn = mem_.Ler32(desc + 0);
  h.cxt = mem_.Ler32(desc + 4);
  h.livre = mem_.Ler32(desc + 8);
  return h;
}

void Widgets::EscreverHandler(Memoria& mem, std::uint32_t desc, const DescritorDeHandler& h) {
  if (desc == 0) return;
  mem.Escrever32(desc + 0, h.pfn);
  mem.Escrever32(desc + 4, h.cxt);
  mem.Escrever32(desc + 8, h.livre);
}

// ---------------------------------------------------------------------------
// O `QueryInterface` das interfaces da hierarquia
// ---------------------------------------------------------------------------
//
// Devolve o MESMO objecto para todas as interfaces da hierarquia, e a razao esta
// no cabecalho: `IRootForm` herda `IForm`, que herda `IHandler`, e no SDK a
// mesma instancia responde pelas tres. Recusar o `IWidget` de um objecto que e um
// `IHandler` faria o jogo desistir da interface que ja tem.
//
// Os IIDs sao GERADOS (`tools/brew_slots.inc`, de `AEEIHandler.h`,
// `AEEIForm.h`, `AEEIRootForm.h`, `AEEIWidget.h`, `AEEIDecorator.h`). Um IID
// escrito de memoria ja regrediu a bateria uma vez (22 applets para 1).
Atendido Widgets::QueryInterfaceDe(ICpu& cpu, bool com_widget, std::uint32_t po) {
  const std::uint32_t iid = cpu.Get(kR1);
  const std::uint32_t ppi = cpu.Get(kR2);
  const bool conhecida =
      iid == AEEIID_IHandler || iid == AEEIID_IForm || iid == AEEIID_IRootForm ||
      iid == AEEIID_IDecorator || (com_widget && iid == AEEIID_IWidget);
  if (ppi != 0) mem_.Escrever32(ppi, conhecida ? po : 0);
  cpu.Set(kR0, conhecida ? kAeeSuccess : kAeeClassNotSupported);
  if (!conhecida) {
    // RECUSA REGISTADA (P2). Nao se inventa um ponteiro para uma interface que
    // nao existe: quem chama fica a saber que nao ha.
    char det[64];
    std::snprintf(det, sizeof(det), "iid=0x%08x", iid);
    traco_.RegistarFalta(Area::Brew, "widget_QueryInterface_recusado", det);
    recusas_.push_back(std::string("QueryInterface ") + det);
    return Atendido::NaoImplementado;
  }
  return Atendido::Feito;
}

// ---------------------------------------------------------------------------
// O `HandleEvent` do IRootForm -- o pedido que a medicao mostrou
// ---------------------------------------------------------------------------
std::uint32_t Widgets::Papel(std::uint32_t papel, std::uint32_t k) const {
  return papel != 0 ? papel : Widget(k);
}

Atendido Widgets::AtenderRootForm(ICpu& cpu, std::uint32_t slot) {
  const std::uint32_t po = cpu.Get(kR0);
  switch (slot) {
    case kIHandler_QueryInterface:
      return QueryInterfaceDe(cpu, /*com_widget=*/false, po);

    case kIHandler_HandleEvent: {
      const std::uint32_t evt = cpu.Get(kR1) & 0xFFFFu;
      const std::uint32_t w = cpu.Get(kR2) & 0xFFFFu;
      const std::uint32_t d = cpu.Get(kR3);
      if (evt == EVT_WDG_SETPROPERTY) {
        // `WID_*`: guardar qual widget ocupa aquele papel. `d == 0` LIMPA, e o
        // cabecalho diz que isso e legal ("releasing any existing ... widget").
        std::uint32_t* destino = nullptr;
        std::uint32_t guardado = 0;
        switch (w) {
          case WID_FORM: case WID_CONTAINER: destino = &papel_form_; break;
          case WID_TITLE: destino = &papel_titulo_; break;
          case WID_SOFTKEYS: destino = &papel_softkeys_; break;
          case WID_BACKGROUND: destino = &papel_fundo_; break;
          default: destino = nullptr; break;
        }
        if (destino != nullptr) {
          if (d != 0 && (d < kObjetoDosWidgets || d >= kFimDosObjetosDeWidget)) {
            // Um ponteiro de widget que nao e NOSSO: aceitar poria o desenho num
            // sitio que nao existe (a mesma razao pela qual o `SetDestination`
            // do IDisplay so aceita um bitmap nosso).
            char det[96];
            std::snprintf(det, sizeof(det), "w=0x%04x d=0x%08x", w, d);
            traco_.RegistarFalta(Area::Brew, "IRootForm_SetProperty widget alheio", det);
            recusas_.push_back("SetProperty widget alheio");
            cpu.Set(kR0, 0);
            return Atendido::NaoImplementado;
          }
          *destino = d;
          ++escritas_;
          guardado = d;
          (void)guardado;
          cpu.Set(kR0, 1);
          return Atendido::Feito;
        }
        if (w == FID_DISPLAY) {
          // Guardar o IDisplay que o jogo quer usar. Nao ha nada a fazer com ele
          // hoje: o desenho do emulador vai para a `Tela` do motor, e nao ha
          // segundo destino. Aceitar e guardar e honesto; recusar faria o jogo
          // parar por uma coisa que ele ja tem.
          display_ = d;
          ++escritas_;
          cpu.Set(kR0, 1);
          return Atendido::Feito;
        }
        if (w == FID_ACTIVE || w == FID_VISIBLE) {
          // O VALOR NAO E APLICADO, E O REGISTO DI-LO.
          //
          // NAO HA MODELO DE VISIBILIDADE nem rasterizador de widgets: nada fica
          // visivel nem activo por causa deste pedido. E a MESMA situacao do TEMA,
          // seis linhas abaixo -- e ate agora tinha a resposta OPOSTA: o tema
          // registava, e o `FID_ACTIVE`/`FID_VISIBLE` devolviam TRUE com um
          // `++escritas_` e mais nada.
          //
          // O SINTOMA, medido: `escritas_` e um contador AGREGADO, logo quem lia a
          // bateria sabia que "alguma propriedade foi escrita" e NAO QUAL. Com um
          // nome no registo, a lista de demanda passa a dizer o que falta em vez de
          // somar tudo numa linha so.
          //
          // O TRUE fica: o pedido foi ENTENDIDO, que e o que o contrato promete.
          // O que nao pode ficar e um TRUE mudo (P2).
          char det[96];
          std::snprintf(det, sizeof(det), "w=0x%04x d=0x%08x", w, d);
          traco_.RegistarFalta(Area::Brew,
                               w == FID_ACTIVE ? "IRootForm FID_ACTIVE nao aplicado"
                                               : "IRootForm FID_VISIBLE nao aplicado",
                               det);
          recusas_.push_back(w == FID_ACTIVE ? "FID_ACTIVE nao aplicado"
                                             : "FID_VISIBLE nao aplicado");
          ++escritas_;
          cpu.Set(kR0, 1);
          return Atendido::NaoImplementado;
        }
        if (w == FID_THEME || w == FID_THEME_FNAME || w == FID_THEME_BASENAME) {
          // O tema NAO e aplicado: nao ha `IResFile` nem ficheiro de tema lido.
          // Devolve-se TRUE (o pedido foi entendido) e REGISTA-SE -- um TRUE com
          // o tema por aplicar e uma meia-verdade que fica escrita.
          char det[96];
          std::snprintf(det, sizeof(det), "w=0x%04x d=0x%08x", w, d);
          traco_.RegistarFalta(Area::Brew, "IRootForm tema nao aplicado (sem IResFile)", det);
          recusas_.push_back("tema nao aplicado");
          cpu.Set(kR0, 1);
          return Atendido::NaoImplementado;
        }
        // Propriedade DESCONHECIDA: FALSE e a resposta do contrato ("nao
        // atendido") -- e fica registada, porque um FALSE mudo seria
        // indistinguivel de "nao ha nada a fazer".
        char det[96];
        std::snprintf(det, sizeof(det), "SETPROPERTY w=0x%04x d=0x%08x", w, d);
        traco_.RegistarFalta(Area::Brew, "IRootForm SetProperty desconhecido", det);
        recusas_.push_back(std::string("SetProperty desconhecido ") + det);
        cpu.Set(kR0, 0);
        return Atendido::NaoImplementado;
      }
      if (evt == EVT_WDG_GETPROPERTY) {
        std::uint32_t valor = 0;
        bool conhecida = true;
        switch (w) {
          case WID_FORM: case WID_CONTAINER:
            valor = Papel(papel_form_, kWidgetDoForm);
            break;
          case WID_TITLE: valor = Papel(papel_titulo_, kWidgetDoTitulo); break;
          case WID_SOFTKEYS: valor = Papel(papel_softkeys_, kWidgetDasSoftkeys); break;
          case WID_BACKGROUND: valor = Papel(papel_fundo_, kWidgetDoFundo); break;
          case FID_ROOT: valor = ObjetoDaRaiz_; break;
          case FID_DISPLAY: valor = display_; break;
          default: conhecida = false; break;
        }
        if (!conhecida) {
          char det[96];
          std::snprintf(det, sizeof(det), "GETPROPERTY w=0x%04x d=0x%08x", w, d);
          traco_.RegistarFalta(Area::Brew, "IRootForm GetProperty desconhecido", det);
          recusas_.push_back(std::string("GetProperty desconhecido ") + det);
          cpu.Set(kR0, 0);
          return Atendido::NaoImplementado;
        }
        if (d != 0) mem_.Escrever32(d, valor);
        ++lidas_;
        if (valor != 0) ++entregues_;
        cpu.Set(kR0, 1);
        return Atendido::Feito;
      }
      if (evt == 0) {
        // `EVT_APP_START` (AEEEvent.h:23) entregue a raiz: guarda o arranque
        // para inspecao (error/cls/display de [d+0/+4/+8] quando d != 0) e
        // devolve TRUE sem empilhar forma nem despachar: pilha vazia = entrega
        // final. Sem `d` (=0) continua TRUE -- arranque sem args e legal.
        ++arranque_cont_;
        if (d != 0) {
          arranque_erro_ = mem_.Ler32(d);
          arranque_cls_ = mem_.Ler32(d + 4);
          arranque_display_ = mem_.Ler32(d + 8);
        }
        traco_.Emitir(Area::Brew, Nivel::Depuracao, "ROOTFORM_APP_START",
                      "arranque guardado na raiz");
        cpu.Set(kR0, 1);
        return Atendido::Feito;
      }
      // QUALQUER OUTRO EVENTO. O contrato do `IRootForm` e entregar os eventos
      // que ele nao trata a forma que esta no TOPO da pilha. Nao ha formas na
      // pilha (nada empurrou nenhuma), logo a resposta e FALSE -- e o FALSE
      // CONTINUA a ser dado.
      //
      // **MAS O FALSE NAO E UMA FALTA: E O CONTRATO -- e ele esta MEDIDO.** No
      // `tectoy` (274755), no sitio que oferece este evento a raiz (`evt=0x7b0a`
      // no traco, `lr=0x6b41c`), desmonte base ZERO:
      //
      //     6b400  ldr r1,[r0]      ; a vtable do IRootForm (guardado em pMe+0x24)
      //     6b408  ldr ip,[r1,#0xc] ; slot 3 = HandleEvent
      //     6b418  bx  ip           ; HandleEvent(raiz, 0x7b0a, 4, d)
      //     6b41c  cmp r0,#0        ; O TITULO LE A RESPOSTA
      //     6b420  bne 0x6b630      ; != 0 (TRUE) -> SALTA o switch PROPRIO dele
      //     6b424  ...              ; == 0 (FALSE) -> cai no switch e trata o evento
      //
      // Com TRUE este applet NAO tratava o proprio evento -- logo o FALSE e a
      // resposta de que ele DEPENDE, e nao uma recusa nossa. E o `0x7b0a` tambem
      // nao e evento de widget nenhum (`EVT_WDG_GETPROPERTY` = 0x800 e
      // `EVT_WDG_SETPROPERTY` = 0x801, `AEEIWidget.h:65-66`; o `w=4` tambem nao e
      // um `WID_*`): e um evento PRIVADO do applet, que ele oferece a raiz ANTES
      // de o tratar ele mesmo.
      //
      // POR ISSO O REGISTO DESTE CAMINHO E UM PRESSUPOSTO DECLARADO, e nao uma
      // falta: `RegistarFalta` e para o que NAO se conseguiu cumprir, e isto
      // cumpre-se. A contagem da bateria muda de balde (falta -> pressuposto) --
      // reclassificacao deliberada, com o `lr` e a razao medidas. O comportamento
      // NAO muda: continua FALSE, e o `Atendido::Feito` (e nao `NaoImplementado`)
      // passa a dizer o mesmo no codigo. E nao entra nas `Recusas()` do modulo:
      // um pedido cumprido pelo contrato nao e um pedido recusado.
      //
      // O `lr` ENTRA NO DETALHE, e e uma medida e nao enfeite: e ele que diz QUAL
      // sitio do modulo ofereceu o evento a raiz, e sem ele nao ha como ir ao
      // desmonte ver o que o titulo faz com a resposta (foi assim que se leu o
      // `Tectoy_FixupTime`).
      char det[160];
      std::snprintf(det, sizeof(det),
                    "evt=0x%04x w=0x%04x d=0x%08x pilha=%zu lr=0x%08x (o titulo LE a resposta)",
                    evt, w, d, pilha_.size(), cpu.Get(kLR));
      traco_.RegistarPressuposto(Area::Brew,
                                 "IRootForm HandleEvent privado do applet -> FALSE", det);
      cpu.Set(kR0, 0);
      return Atendido::Feito;
    }

    case kIHandler_SetHandler: {
      // `void SetHandler(po, HandlerDesc *pd)`: ENCHE o descritor com o handler
      // ANTERIOR e instala o novo ("On return, the handler descriptor will be
      // filled with the previous event handler"). Devolver o anterior e o
      // contrato, e e o que permite ao jogo encadear.
      const std::uint32_t pd = cpu.Get(kR1);
      const DescritorDeHandler novo = LerHandler(pd);
      const DescritorDeHandler anterior{pfn_, cxt_, livre_};
      EscreverHandler(mem_, pd, anterior);
      pfn_ = novo.pfn;
      cxt_ = novo.cxt;
      livre_ = novo.livre;
      cpu.Set(kR0, 0);
      return Atendido::Feito;
    }

    case kIRootForm_InsertForm: {
      // `int InsertForm(po, IForm *pf, IForm *pfBefore)`. `FORM_FIRST`=2,
      // `FORM_LAST`=1, `FORM_DEFAULT`=0 (`AEEIRootForm.h:32..38`).
      const std::uint32_t pf = cpu.Get(kR1);
      if (pf == 0) {
        cpu.Set(kR0, kAeeBadParm);
        return Atendido::Feito;
      }
      pilha_.push_back(pf);
      cpu.Set(kR0, kAeeSuccess);
      return Atendido::Feito;
    }

    case kIRootForm_RemoveForm: {
      const std::uint32_t pf = cpu.Get(kR1);
      for (std::size_t i = pilha_.size(); i-- > 0;) {
        if (pilha_[i] == pf) {
          pilha_.erase(pilha_.begin() + static_cast<std::ptrdiff_t>(i));
          cpu.Set(kR0, kAeeSuccess);
          return Atendido::Feito;
        }
      }
      // Recusa DECLARADA: tirar da pilha uma forma que la nao esta e um erro de
      // quem chama (AEE_EBADPARM), e nao um sucesso silencioso.
      cpu.Set(kR0, kAeeBadParm);
      return Atendido::Feito;
    }

    case kIRootForm_GetForm: {
      const std::uint32_t pr = cpu.Get(kR1);
      const bool proximo = cpu.Get(kR2) != 0;
      if (pilha_.empty()) {
        cpu.Set(kR0, 0);
        return Atendido::Feito;
      }
      // Sem forma de referencia: `TRUE` quer a primeira, `FALSE` a ultima. A
      // regra esta escrita no cabecalho, e nao foi inventada aqui.
      if (pr == 0) {
        cpu.Set(kR0, proximo ? pilha_.front() : pilha_.back());
        return Atendido::Feito;
      }
      for (std::size_t i = 0; i < pilha_.size(); ++i) {
        if (pilha_[i] == pr) {
          if (proximo && i + 1 < pilha_.size()) { cpu.Set(kR0, pilha_[i + 1]); return Atendido::Feito; }
          if (!proximo && i > 0) { cpu.Set(kR0, pilha_[i - 1]); return Atendido::Feito; }
          cpu.Set(kR0, 0);
          return Atendido::Feito;
        }
      }
      cpu.Set(kR0, 0);
      return Atendido::Feito;
    }

    case kIRootForm_ResolveForm: {
      // `form:` URLs precisam do `ISHELL_RegisterHandler`, que nao existe aqui.
      // RECUSA NOMEADA, e nao um ponteiro a zero sem explicacao.
      const std::uint32_t ppi = cpu.Get(kR2);
      if (ppi != 0) mem_.Escrever32(ppi, 0);
      traco_.RegistarFalta(Area::Brew, "IRootForm::ResolveForm", "sem ISHELL_RegisterHandler");
      recusas_.push_back("ResolveForm");
      cpu.Set(kR0, kAeeUnsupported);
      return Atendido::NaoImplementado;
    }

    case kIRootForm_GetClientRect: {
      // `void GetClientRect(po, IXYContainer **c, AEERect *r)`. O contentor e o
      // widget `WID_FORM`; o rect e o ecra inteiro -- e a AREA DE CLIENTE e toda
      // a tela porque nao ha barra de titulo nem softkeys desenhadas.
      const std::uint32_t pc2 = cpu.Get(kR1);
      const std::uint32_t pr = cpu.Get(kR2);
      if (pc2 != 0) {
        const std::uint32_t c = papel_form_ != 0 ? papel_form_ : Widget(kWidgetDoForm);
        mem_.Escrever32(pc2, c);
      }
      EscreverRectDoGuest(mem_, pr, 0, 0, kLarguraDoEcra, kAlturaDoEcra);
      cpu.Set(kR0, 0);
      return Atendido::Feito;
    }

    default: {
      // Os slots que a hierarquia TEM e que este modulo NAO implementa. Recusa
      // com nome (P2): um slot mudo seria lido como "o jogo nao pede aquilo".
      char det[64];
      std::snprintf(det, sizeof(det), "slot=%u", slot);
      traco_.RegistarFalta(Area::Brew, "IRootForm slot nao implementado", det);
      recusas_.push_back(std::string("IRootForm slot ") + det);
      cpu.Set(kR0, kAeeUnsupported);
      return Atendido::NaoImplementado;
    }
  }
}

// ---------------------------------------------------------------------------
// Um widget
// ---------------------------------------------------------------------------
Atendido Widgets::AtenderWidget(ICpu& cpu, std::uint32_t k, std::uint32_t slot) {
  if (k >= kQuantosWidgets) {
    traco_.RegistarFalta(Area::Brew, "widget de indice fora da tabela",
                         "k=" + std::to_string(k));
    cpu.Set(kR0, kAeeUnsupported);
    return Atendido::NaoImplementado;
  }
  const std::uint32_t objeto = Widget(k);
  switch (slot) {
    case kIHandler_QueryInterface:
      return QueryInterfaceDe(cpu, /*com_widget=*/true, objeto);

    case kIHandler_HandleEvent: {
      const std::uint32_t evt = cpu.Get(kR1) & 0xFFFFu;
      const std::uint32_t w = cpu.Get(kR2) & 0xFFFFu;
      const std::uint32_t d = cpu.Get(kR3);
      if (evt == EVT_WDG_SETPROPERTY) {
        // A MEDICAO que poe este caminho aqui: o `tectoy` faz
        //     68b60  bl 0x303c8   ; HandleEvent(widget, EVT_WDG_SETPROPERTY, 0x130, 0xff)
        // e `0x130` e `PROP_BGCOLOR` (`AEEWidgetProperties.h:108`, lido pelo
        // gerador). A cor e GUARDADA no objecto; nao pinta nada, porque nao ha
        // rasterizador de widgets -- e pinta-la seria o emulador a desenhar-se.
        switch (w) {
          case PROP_BGCOLOR: case PROP_ACTIVE_BGCOLOR: case PROP_INACTIVE_BGCOLOR:
            mem_.Escrever32(objeto + kW_CorDeFundo, d);
            ++escritas_;
            cpu.Set(kR0, 1);
            return Atendido::Feito;
          case PROP_FGCOLOR: case PROP_ACTIVE_FGCOLOR: case PROP_INACTIVE_FGCOLOR:
            mem_.Escrever32(objeto + kW_CorDeFrente, d);
            ++escritas_;
            cpu.Set(kR0, 1);
            return Atendido::Feito;
          case PROP_BORDERCOLOR: case PROP_BORDERSTYLE: case PROP_ACTIVE_BORDERCOLOR:
          case PROP_INACTIVE_BORDERCOLOR:
            // Guardar um numero de estilo/contorno sem os desenhar seria aceitar
            // uma propriedade que nao se cumpre. Recusa REGISTADA -- o `tectoy`
            // so pede `PROP_BGCOLOR`, logo isto nao o trava.
            {
              char det[64];
              std::snprintf(det, sizeof(det), "w=0x%04x d=0x%08x", w, d);
              traco_.RegistarFalta(Area::Brew, "IWidget estilo de contorno nao implementado", det);
              recusas_.push_back("estilo de contorno");
              cpu.Set(kR0, 0);
              return Atendido::NaoImplementado;
            }
          default: {
            char det[64];
            std::snprintf(det, sizeof(det), "k=%u w=0x%04x d=0x%08x", k, w, d);
            traco_.RegistarFalta(Area::Brew, "IWidget SetProperty desconhecido", det);
            recusas_.push_back(std::string("widget SetProperty ") + det);
            cpu.Set(kR0, 0);
            return Atendido::NaoImplementado;
          }
        }
      }
      if (evt == EVT_WDG_GETPROPERTY) {
        std::uint32_t valor = 0;
        bool conhecida = true;
        switch (w) {
          case PROP_BGCOLOR: case PROP_ACTIVE_BGCOLOR: case PROP_INACTIVE_BGCOLOR:
            valor = mem_.Ler32(objeto + kW_CorDeFundo);
            break;
          case PROP_FGCOLOR: case PROP_ACTIVE_FGCOLOR: case PROP_INACTIVE_FGCOLOR:
            valor = mem_.Ler32(objeto + kW_CorDeFrente);
            break;
          default: conhecida = false; break;
        }
        if (!conhecida) {
          char det[64];
          std::snprintf(det, sizeof(det), "k=%u w=0x%04x", k, w);
          traco_.RegistarFalta(Area::Brew, "IWidget GetProperty desconhecido", det);
          recusas_.push_back(std::string("widget GetProperty ") + det);
          cpu.Set(kR0, 0);
          return Atendido::NaoImplementado;
        }
        if (d != 0) mem_.Escrever32(d, valor);
        ++lidas_;
        cpu.Set(kR0, 1);
        return Atendido::Feito;
      }
      {
        char det[64];
        std::snprintf(det, sizeof(det), "k=%u evt=0x%04x", k, evt);
        traco_.RegistarFalta(Area::Brew, "IWidget HandleEvent nao atendido", det);
        recusas_.push_back(std::string("widget HandleEvent ") + det);
        cpu.Set(kR0, 0);
        return Atendido::NaoImplementado;
      }
    }

    case kIHandler_SetHandler: {
      const std::uint32_t pd = cpu.Get(kR1);
      const DescritorDeHandler novo = LerHandler(pd);
      const DescritorDeHandler anterior{mem_.Ler32(objeto + kW_Handler + 0),
                                       mem_.Ler32(objeto + kW_Handler + 4),
                                       mem_.Ler32(objeto + kW_Handler + 8)};
      EscreverHandler(mem_, pd, anterior);
      mem_.Escrever32(objeto + kW_Handler + 0, novo.pfn);
      mem_.Escrever32(objeto + kW_Handler + 4, novo.cxt);
      mem_.Escrever32(objeto + kW_Handler + 8, novo.livre);
      cpu.Set(kR0, 0);
      return Atendido::Feito;
    }

    case kIWidget_GetPreferredExtent:
    case kIWidget_GetExtent: {
      // `void GetExtent(IWidget *pif, WidgetExtent *pWExtent)` -- `AEEIWidget.h:103`.
      // A struct do guest tem DOIS `int`: largura e altura (`:38-43`), e nao um
      // `AEERect`. A extensao guardada tem a largura em `kW_Rect + 8` e a altura
      // em `kW_Rect + 12` (o nosso esquema interno, declarado em `widget.h`).
      const std::uint32_t pr = cpu.Get(kR1);
      EscreverExtent(mem_, pr, mem_.Ler32(objeto + kW_Rect + 8), mem_.Ler32(objeto + kW_Rect + 12));
      ++lidas_;
      cpu.Set(kR0, 1);
      return Atendido::Feito;
    }

    case kIWidget_SetExtent: {
      // `void SetExtent(IWidget *pif, WidgetExtent *pWExtent)` -- idem. O
      // cabecalho do `IWidget` diz que a `WidgetExtent` define "width and height,
      // without defining the bounds or placement of the widget": logo a ORIGEM
      // nao vem daqui, e fica a zero.
      const std::uint32_t pr = cpu.Get(kR1);
      if (pr == 0) {
        cpu.Set(kR0, 0);
        return Atendido::Feito;
      }
      std::uint32_t largura = 0, altura = 0;
      LerExtent(mem_, pr, &largura, &altura);
      mem_.Escrever32(objeto + kW_Rect + 0, 0);
      mem_.Escrever32(objeto + kW_Rect + 4, 0);
      mem_.Escrever32(objeto + kW_Rect + 8, largura);
      mem_.Escrever32(objeto + kW_Rect + 12, altura);
      ++escritas_;
      cpu.Set(kR0, 1);
      return Atendido::Feito;
    }

    case kIWidget_GetParent:
      cpu.Set(kR0, mem_.Ler32(objeto + kW_Pai));
      return Atendido::Feito;

    case kIWidget_SetParent:
      mem_.Escrever32(objeto + kW_Pai, cpu.Get(kR1));
      ++escritas_;
      cpu.Set(kR0, 0);
      return Atendido::Feito;

    case kIWidget_GetModel:
      cpu.Set(kR0, mem_.Ler32(objeto + kW_Modelo));
      return Atendido::Feito;

    case kIWidget_SetModel:
      mem_.Escrever32(objeto + kW_Modelo, cpu.Get(kR1));
      ++escritas_;
      cpu.Set(kR0, 0);
      return Atendido::Feito;

    case kIWidget_Draw: {
      // `void Draw(IWidget *po, IGraphics *pg)`.
      //
      // RECUSA REGISTADA, e a decisao esta escrita porque e uma escolha, nao uma
      // falta de tempo: nao ha rasterizador de widgets nem tema, e um `Draw` que
      // pintasse um retangulo com a cor guardada seria O EMULADOR A DESENHAR. A
      // bateria passaria a contar pixels do emulador como se fossem do jogo --
      // que e a classe de defeito que a arvore antiga cometeu ao medir a imagem
      // em vez da jogabilidade.
      ++desenhos_;
      char det[64];
      std::snprintf(det, sizeof(det), "k=%u igraphics=0x%08x", k, cpu.Get(kR1));
      traco_.RegistarFalta(Area::Brew, "IWidget::Draw sem rasterizador", det);
      recusas_.push_back(std::string("IWidget::Draw ") + det);
      cpu.Set(kR0, 0);
      return Atendido::NaoImplementado;
    }

    default: {
      char det[64];
      std::snprintf(det, sizeof(det), "k=%u slot=%u", k, slot);
      traco_.RegistarFalta(Area::Brew, "IWidget slot nao implementado", det);
      recusas_.push_back(std::string("IWidget slot ") + det);
      cpu.Set(kR0, kAeeUnsupported);
      return Atendido::NaoImplementado;
    }
  }
}

ResumoDeWidget Widgets::Resumo(std::uint32_t k) const {
  ResumoDeWidget r;
  const std::uint32_t objeto = Widget(k);
  if (objeto == 0) return r;
  r.objeto = objeto;
  r.largura = mem_.Ler32(objeto + kW_Rect + 8);
  r.altura = mem_.Ler32(objeto + kW_Rect + 12);
  r.pai = mem_.Ler32(objeto + kW_Pai);
  r.cor_de_fundo = mem_.Ler32(objeto + kW_CorDeFundo);
  r.modelo = mem_.Ler32(objeto + kW_Modelo);
  r.handler_pfn = mem_.Ler32(objeto + kW_Handler + 0);
  r.handler_pcxt = mem_.Ler32(objeto + kW_Handler + 4);
  r.tem_handler = r.handler_pfn != 0;
  return r;
}

}  // namespace zb2::brew
