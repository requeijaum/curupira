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
#include <memory>
#include <string>
#include <vector>

#include "core/audio/misturador.h"
#include "core/brew/ajudantes.h"
#include "core/brew/recursos.h"

// A CABLAGEM DO GL (etapa 6): o IGL e o IEGL. Ver docs/rewrite/REMENDO-GL-DESPACHO.md.
//
// ESTE `#define` E A GUARDA DO TESTE. O `tests/gl_cablagem_test.cpp` corre de
// verdade quando ele existe e SALTA com o motivo escrito quando nao existe -- e um
// teste que passa sem ter corrido e pior do que um teste vermelho.
#define ZB2_CABLAGEM_GL 1
#include "core/brew/egl.h"
#include "core/brew/igl.h"

// A CABLAGEM DO GL (etapa 6): o IGL e o IEGL. Ver docs/rewrite/REMENDO-GL-DESPACHO.md.
//
// ESTE `#define` E A GUARDA DO TESTE. O `tests/gl_cablagem_test.cpp` corre de
// verdade quando ele existe e SALTA com o motivo escrito quando nao existe -- e um
// teste que passa sem ter corrido e pior do que um teste vermelho.
#define ZB2_CABLAGEM_GL 1
#include "core/brew/egl.h"
#include "core/brew/igl.h"
#include "core/brew/ihiddevice.h"
#include "core/brew/arquivo.h"
#include "core/brew/imedia.h"
#include "core/brew/interface.h"
#include "core/brew/tela.h"
#include "core/brew/vfs.h"
#include "core/brew/widget.h"
#include "core/cpu/cpu.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"

namespace zb2::brew {

// Um temporizador pedido pelo guest. UM so, porque e o que os titulos pedem: o
// laco de quadro, re-armado pelo proprio callback.
//
// O PAR E `(pfn, pUser)`, E NAO UM `AEECallback*`. Os argumentos veem SEPARADOS
// do guest, como o cabecalho manda:
//
//     int SetTimer(IShell *po, int32 dwMsecs, void (*pfn)(void *), void *pUser);
//     platform/system/inc/AEEIShell.h:299, em `INHERIT_IShell`
//
// MEDIDO no `asq` (`mod/280214/asq.mod`), no `EVT_APP_START`, em `0x8cf34`:
//
//     8cf34  ldr  r2, [r5, #12]     ; r2 = applet->m_pIShell
//     8cf38  mov  r3, r5            ; r3 = pMe        -- o CONTEXTO
//     8cf3c  mov  r0, r2            ; r0 = po
//     8cf44  mov  r1, #100          ; r1 = 100 ms     -- a DURACAO
//     8cf48  ldr  r2, [pc, #572]    ; r2 = *0x8d18c = 0x8c3e8  -- a FUNCAO
//     8cf4c  ldr  ip, [ip, #44]     ; slot 11 = SetTimer
//     8cf54  bx   ip
//
// Este codigo lia o r1 como ponteiro de `AEECallback` e o r2 como milissegundos
// -- os dois campos TROCADOS. O `PrepararCallbackDoTemporizador` lia depois
// `[100]` como par `(funcao, contexto)`, caia fora do modulo, e RECUSAVA. O
// sintoma era "nenhum titulo arma o laco de quadro" -- e sem laco de quadro
// nenhum titulo chega ao codigo que desenha.
struct Temporizador {
  bool ativo = false;
  std::uint32_t pfn = 0;    // void (*pfn)(void *pUser)
  std::uint32_t puser = 0;  // pUser
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


  // `Faltas()`, `VfsRef()`, `ArquivosRef()`, `Blits()` e `Backlights()` SAIRAM.
  //
  // Eram acessores que ninguem chamava -- e o `Faltas()` era pior do que inutil:
  // devolvia `faltas_`, um mapa que **nunca e escrito em lado nenhum**. Quem o
  // chamasse lia um mapa VAZIO e concluia "nao falta nada".
  //
  // **E a terceira vez que esta classe de defeito aparece nesta arvore:**
  // o `e.tamanho` esteve a zero e fazia o degrau `vtable` parecer falso; o
  // `Despacho::Faltas()` foi encontrado pelo sub-agente do GL, que o leu e imprimiu
  // "sem_nome" nas 41 chamadas; e o `e.create` media lixo no slot de saida --
  // **o `applet 41` estava inflado e o honesto era 22.**
  //
  // A licao e sempre a mesma: **um campo que nao mede e pior do que um campo ausente,
  // porque um ausente nao se le.** Uma API que devolve um valor plausivel e errado
  // faz o instrumento mentir sem avisar.
  std::uint32_t Textos() const { return textos_; }

  // E VOLTARAM, porque a quarta vez foi a mesma classe de defeito AO CONTRARIO:
  // o motor contava (`++blits_` em `despacho.cpp`, `++updates_`, `++dibs_`) e a
  // FERRAMENTA lia variaveis globais (`g_blits`, `g_updates`, `g_dibs` em
  // `tools/bateria.cpp`) que **nunca eram incrementadas em lado nenhum**. As
  // colunas `textos` e `blits` do JSON escreviam ZERO em todas as corridas desde
  // que existem, e ninguem reparou porque zero e um numero plausivel para um
  // emulador que ainda nao desenha.
  //
  // Tirar os acessores nao resolveu: mudou o defeito de "ninguem le" para
  // "le-se a coisa errada". Agora ha UMA fonte, e e esta.
  std::uint32_t Blits() const { return blits_; }

  // O RELOGIO VIRTUAL, para quem precisa de afirmar que ele NAO andou.
  // (`tests/sendevent_test.cpp`: a entrega de um evento tem de ser instantanea
  // para o guest.)
  std::uint32_t AgoraMs() const { return static_cast<std::uint32_t>(agora_ms_); }
  std::uint32_t Updates() const { return updates_; }
  std::uint32_t Dibs() const { return dibs_; }



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

  // --- O WIDGET (IRootForm + IForm + IHandler + IWidget) -------------------
  //
  // O `IShell::CreateInstance` do corpus pede `AEECLSID_CRootForm`
  // (`0x01028e51`) e recebe o objecto GENERICO de indice `kIndiceDoRootForm`; os
  // slots desse objecto ja apontam para a faixa de saida, e este modulo passa a
  // ATENDE-LOS em vez de os recusar. Ver `core/brew/widget.h` para a medicao
  // (o `tectoy`, slot 3 com `EVT_WDG_GETPROPERTY`/`WID_FORM`/`WID_SOFTKEYS`).
  //
  // CHAMADO AUTOMATICAMENTE NO FIM DO `InstalarAjudantes`, e idempotente: nao
  // faz mal nenhum que quem dirige o titulo o chame tambem, e assim esta frente
  // nao obriga a mudar `tools/bateria.cpp`.
  bool InstalarWidgets(const Saidas& saidas);
  Widgets& WidgetsRef() { return widgets_; }
  const Widgets& WidgetsRef() const { return widgets_; }
  // `true` = o indice era do widget e ja foi atendido (com sucesso OU com recusa
  // registada). `false` = nao e desta faixa.
  bool AtenderWidgets(ICpu& cpu, std::uint32_t indice);

  // Aplica a entrada ate ao instante actual e marca os sinais registados.
  // `true` = ha um callback do titulo posto no PC (PC = funcao, R0 = contexto,
  // LR = sentinela); quem chama tem de o deixar correr.
  bool BombearEntrada(ICpu& cpu) { return ihid_.Bombear(cpu); }

  // A FAIXA DO MODULO DO TITULO (base e tamanho), para o modulo da entrada poder
  // recusar um callback que aponte para fora dela. A base e ZERO, e e medida.
  // O FIM DA FAIXA tambem vai para o despacho, e nao so para os modulos.
  //
  // MEDIDO, apontado pelo sub-agente `hid-entrada`: o `saiu_do_modulo` do laco usava
  // `pc >= kBase + 0x01000000`, 16 MB acima da base. Com a base a ZERO isso e
  // `0x01000000` -- e a bateria mostrava 12 titulos a saltar para la e a serem dados
  // como "fora do modulo", quando o limite real e o TAMANHO da imagem. **Um guest
  // que se perca entre o fim da imagem e os 16 MB continua a andar em silencio.**
  void DefinirFaixaDoModulo(std::uint32_t base, std::uint32_t tamanho) {
    base_do_modulo_ = base;
    faixa_base_ = base;
    faixa_fim_ = base + tamanho;
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
  std::uint32_t CallbackDoTemporizador() const { return timer_.pfn; }
  std::uint32_t ContextoDoTemporizador() const { return timer_.puser; }
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
  void SituarTitulo(const std::string& dir, const std::string& pasta,
                    std::uint32_t clsid = 0) {
    dir_ = dir;
    pasta_ = pasta;
    clsid_titulo_ = clsid;
    tem_clsid_ = (clsid != 0);
    // A REGRA QUE LIGOU OS PACOTES `.pkg` AO SISTEMA DE FICHEIROS FICA DITA.
    //
    // A `Vfs` foi registada antes deste objecto existir (a ferramenta chama
    // `Registar` antes de construir o `Despacho`), pelo que a declaracao e
    // emitida agora, uma so vez por titulo, e nao no momento do registo: sem
    // isto, `karnovr/boot.rom` resolver para uma entrada de `boot.pkg` seria uma
    // ligacao que ninguem ve -- e uma ligacao que ninguem ve perde-se em
    // silencio, que e o defeito que ja custou a cablagem do `SetTimer`.
    vfs_.DeclararNoTraco(&traco_);
  }
  std::uint32_t ClsidDoTitulo() const { return clsid_titulo_; }
  bool TemClsidDoTitulo() const { return tem_clsid_; }
  // --- O GL (etapa 6) ------------------------------------------------------
  //
  // O `Igl` e o `Egl` vivem AQUI, e nao na ferramenta: a cablagem e do motor.
  // `InstalarGl` escreve as duas vtables na faixa de saida e CONFIRMA com leitura
  // de volta (cada modulo ja faz a sua); e o `Correr` que entrega os pedidos.
  std::uint32_t InstalarGl(const Saidas& saidas);
  Igl& IglRef() { return igl_; }
  const Igl& IglRef() const { return igl_; }
  Egl& EglRef() { return egl_; }
  const Egl& EglRef() const { return egl_; }

  // --- O GL (etapa 6) ------------------------------------------------------
  //
  // O `Igl` e o `Egl` vivem AQUI, e nao na ferramenta: a cablagem e do motor.

  // A VTABLE DO IBitmap: GUARDA O ENDERECO **E CONSTROI A TABELA**.
  //
  // As duas coisas juntas de proposito. Enquanto foram duas, a ferramenta
  // guardava o endereco e ninguem construia a tabela: os slots 0 e 1 (`AddRef` e
  // `Release` da IBase) ficavam a ZERO e o `IBITMAP_Release` do guest fazia
  // `blx 0`. MEDIDO no `abd` (279369, 0x1469c) e no `torkandkral` (280463,
  // 0x135d4) -- ver `tests/entrada_despacho_test.cpp`, TEST(VtableDoBitmap, ...).
  void DefinirVtableBitmap(const Saidas& s) {
    vtable_bitmap_ = s.Endereco(kVtableBitmap);
    ConstruirVtableDoBitmap(mem_, s);
  }
  void DefinirVtableFicheiro(std::uint32_t v) { vtable_ficheiro_ = v; }
  void DefinirApplet(std::uint32_t v) { applet_ = v; }

  // --- ISHELL_SendEvent (IShell slot 21) -----------------------------------
  //
  // A VTABLE TEM SEIS ARGUMENTOS (MEDIDO, `AEEIShell.h:309`):
  //
  //   boolean SendEvent(IShell *po, uint16 wFlags, AEECLSID clsApp,
  //                     AEEEvent evt, uint16 wParam, uint32 dwParam)
  //     r0=po  r1=wFlags  r2=clsApp  r3=evt  [sp+0]=wParam  [sp+4]=dwParam
  //
  // O `ISHELL_SendEvent` de CINCO argumentos e so a macro que poe `wFlags=0`
  // (`AEEShell.h:278`), e o `ISHELL_PostEvent` e O MESMO SLOT com
  // `EVTFLG_ASYNC|EVTFLG_UNIQUE` (`AEEShell.h:279`).
  //
  // A ENTREGA E SINCRONA porque o chamador le a resposta na instrucao SEGUINTE
  // ao retorno -- MEDIDO no `tectoy.mod` (274755), `0x6a3a0: ldrne r0,[sp,#8]`,
  // e declarado no SDK (`AEEIShell.h:2851`, "sends events synchronously").
  // Uma fila drenada no quadro seguinte entrega DEPOIS de o chamador ja ter
  // lido zero -- ou seja, nao entrega nada.
  //
  // `pp_saida` e o `ppObj` do `IModule::CreateInstance`: o applet manda um
  // evento a si proprio DE DENTRO do create, antes de `applet_` existir
  // (`tools/bateria.cpp:785` so o define depois). E o que as duas fontes
  // fazem: zeebulator `core/brew/ishell.cpp:200-207`, zeebx
  // `src/machine/signal.rs:232-241`.
  //
  // Devolve `true` quando o `HandleEvent` do applet CORREU e VOLTOU; nesse caso
  // `*devolveu` traz o `boolean` do applet. `*passos_gastos` traz os passos que
  // a entrega consumiu -- saem do MESMO orcamento da fase, e nao de um
  // orcamento escondido.
  bool EntregarEventoAoApplet(ICpu& cpu, std::uint32_t clsapp, std::uint32_t evt,
                              std::uint16_t wp, std::uint32_t dwp, std::uint32_t pp_saida,
                              std::uint32_t* devolveu, std::uint64_t* passos_gastos);

  // O FIM DA FAIXA DO MODULO, e nao `kBase + 16 MB`.

 private:
  // O ANEL DAS ULTIMAS INSTRUCOES (ver `Correr`): guarda o PC e a palavra de cada uma
  // das ultimas 16, para o motivo da saida dizer COMO se chegou la.
  std::uint32_t anel_pc_[16] = {0};
  std::uint32_t anel_instr_[16] = {0};
  std::uint32_t ultimas_ = 0;

  // O CABECALHO PUBLICO DO IDIB (`AEEIDIB.h:42-55`), num so sitio.
  //
  // Havia tres copias a escrever offsets a mao (`CreateDIBitmap`,
  // `GetDestination`, `GetDeviceBitmap`) e as tres estavam erradas do mesmo
  // modo. Tres copias de um layout medido sao tres chances de ele divergir.
  void EscreverCabecalhoDeIdib(std::uint32_t obj, std::uint32_t pbmp, std::uint32_t largura,
                               std::uint32_t altura);
  // Para os testes poderem ver a contagem que saiu do `+4` do objecto.
 public:
  std::uint32_t ReferenciasDoBitmap(std::uint32_t obj) const {
    const auto it = refs_do_dib_.find(obj);
    return it == refs_do_dib_.end() ? 0 : it->second;
  }

 private:
  // "Sei servir esta classe?" -- a lista partilhada pelo `QueryClass` e pelo
  // `CheckPrivLevel`.
  bool ClasseConhecida(std::uint32_t cls) const;
  // O IDIB do ECRA. Devolve o endereco do objecto.
  std::uint32_t EscreverCabecalhoDoBitmapDoEcra();
  // A TELA -> O BUFFER DO GUEST. Depois disto, os dois lados sao iguais, e por
  // isso qualquer diferenca futura e do guest.
  void ExporEcraAoGuest();

 public:
  // O BUFFER DO GUEST -> A TELA. Corre no `IDisplay::Update` (que e quando o
  // BREW mostra o que foi desenhado) e uma vez no fim da medicao, para o caso
  // de um titulo que escreve pixels e nunca chama `Update`. Devolve quantos
  // pixels vieram do guest.
  std::uint32_t AbsorverEcraDoGuest();
  bool EcraExpostoAoGuest() const { return ecra_exposto_; }
  std::uint32_t PixelsVindosDoGuest() const { return pixels_do_guest_; }

 private:
  // O ecra so ganha pagina no guest quando ALGUEM pede o bitmap do ecra. Um
  // titulo que nunca o peca nao paga os 614 400 bytes nem as copias.
  bool ecra_exposto_ = false;
  std::uint32_t pixels_do_guest_ = 0;

  // A faixa dos objectos de bitmap (a mesma que o `SetDestination` ja aceita).
  static bool EUmObjectoDeBitmap(std::uint32_t p) {
    return p >= zb2::brew::kObjDibBase && p < zb2::brew::kObjDibBase + 0x1000;
  }
  // O ERRO DA ULTIMA OPERACAO DE FICHEIRO QUE FALHOU (`IFILEMGR_GetLastError`).
  //
  // Devolvia-se sempre 0 -- "sem erro" logo a seguir a uma recusa. Um jogo que
  // decida pelo codigo de erro decidia ao contrario.
  std::int32_t ultimo_erro_do_fm_ = kAeeSuccess;

  // A contagem de referencias dos IDIB vive AQUI, e nao no `+4` do objecto.
  //
  // O `+4` de um IDIB e o `pPaletteMap` (`AEEIDIB.h:44`), um PONTEIRO publico, e
  // a IBase generica desta arvore guarda a contagem nesse offset
  // (`core/brew/interface.cpp:18`, e os ramos `idx == 3/4` do `Atender`). Um
  // `AddRef` no bitmap do ecra punha `1` -- e depois `2`, `3` -- num campo que o
  // `IDIB_FlushPalette` (`AEEIDIB.h:83-86`) desreferencia.
  std::map<std::uint32_t, std::uint32_t> refs_do_dib_;

  Memoria& mem_;
  Traco& traco_;
  Alocador& al_;
  Vfs& vfs_;
  Arquivos arquivos_;
  // Recursos do `.bar`: conhece a VFS e o alocador do GUEST. O despacho fica só
  // com a ABI (registros/pilha); as três formas de LoadResDataEx têm testes em
  // `tests/recursos_test.cpp`.
  Recursos recursos_;
  Tela tela_;
  // O MISTURADOR e o IMedia. O `Media` tem referencias dentro, logo NAO e
  // atribuivel: nasce no `InstalarAjudantes`, quando a faixa de saida ja esta
  // configurada (ele guarda uma COPIA dela).
  audio::Misturador misturador_;
  std::unique_ptr<Media> media_;

  // A ENTRADA. `sinais_` antes de `ihid_`, porque o `Ihid` guarda a referencia
  // ao `Sinais` -- e a ordem de declaracao e a ordem de construcao.
  EntradaDoZeebo entrada_;
  Sinais sinais_;
  Ihid ihid_;
  std::uint32_t base_da_entrada_ = 0;
  std::uint32_t base_do_modulo_ = 0;
  bool entrada_pronta_ = false;
  bool widgets_prontos_ = false;

  std::int64_t agora_ms_ = 0;
  Temporizador timer_;

  // O GL e o EGL. Depois do que eles nao usam e antes de nada que os use: a ordem
  // de declaracao e a ordem de construcao.
  Igl igl_;
  Egl egl_;

  // O GL e o EGL. Depois do que eles nao usam e antes de nada que os use: a ordem
  // de declaracao e a ordem de construcao.
  // O WIDGET. Depois de `tela_` e dos objectos do shell, porque e construido por
  // `InstalarAjudantes` e nao no construtor.
  Widgets widgets_;

  // A PROFUNDIDADE DA ENTREGA DE EVENTOS (guarda de reentrancia): o
  // `HandleEvent` do applet pode mandar outro evento, e isso e uma cadeia que
  // so para com um tecto. `4` e o numero do zeebx (`src/machine/mod.rs:1541`,
  // `MAX_NESTING`); o zeebulator usa 1 (`core/brew/ishell.h:303`).
  //
  // ELA TAMBEM DESLIGA O LACO DE QUADRO enquanto a entrega corre: um
  // temporizador ou um sinal de entrada disparado de DENTRO do `HandleEvent`
  // seria reentrancia que o BREW nunca faz.
  int profundidade_de_evento_ = 0;
  static constexpr int kMaxProfundidadeDeEvento = 4;

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
  std::uint32_t clsid_titulo_ = 0;
  bool tem_clsid_ = false;
  std::uint32_t faixa_base_ = 0;
  std::uint32_t faixa_fim_ = 0;
};

}  // namespace zb2::brew

#endif
