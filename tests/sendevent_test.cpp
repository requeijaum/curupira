#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <string>

#include "core/brew/ajudantes.h"
#include "core/brew/despacho.h"
#include "core/cpu/arm_interpreter.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"
#include "tools/brew_slots.inc"

namespace zb2::brew {
namespace {

// ===========================================================================
// ISHELL_SendEvent -- IShell slot 21 (tools/brew_slots.inc:34).
//
// A VTABLE tem SEIS argumentos, e foi MEDIDO no SDK extraido:
//   platform/system/inc/AEEIShell.h:309
//     boolean (*SendEvent)(iname *po, uint16 wFlags, AEECLSID clsApp,
//                          AEEEvent evt, uint16 wParam, uint32 dwParam);
//   platform/deprecated/inc/AEEShell.h:278
//     #define ISHELL_SendEvent(p,cls,ec,wp,dw) IShell_SendEvent(p,0,cls,ec,wp,dw)
//   platform/deprecated/inc/AEEShell.h:279  (PostEvent = O MESMO SLOT, com
//     EVTFLG_ASYNC|EVTFLG_UNIQUE; AEEShell.h:47-48 EVTFLG_ASYNC = 0x0002)
//
// A ENTREGA E SINCRONA, e nao e opiniao. MEDIDO no `tectoy.mod` (274755,
// Z-Wheel da Tectoy), palavras lidas do ficheiro em 0x6a374..0x6a3a4:
//   6a374 e28d3008  add r3, sp, #8     ; r3 = &saida (na pilha do guest)
//   6a378 e3a02004  mov r2, #4         ; wParam = 4 (selector: PrefsDB)
//   6a37c e88d000c  stm sp, {r2, r3}   ; [sp]=4  [sp+4]=&saida
//   6a384 e59f30ac  ldr r3, [pc,#172]  ; r3 = *0x6a438 = 0x00007b0a (evt)
//   6a388 e592c054  ldr ip, [r2,#84]   ; 84/4 = 21 -> o slot
//   6a390 e3a01000  mov r1, #0         ; wFlags = 0 -> SINCRONO
//   6a39c e3500000  cmp r0, #0
//   6a3a0 159d0008  ldrne r0, [sp,#8]  ; LE A RESPOSTA NA INSTRUCAO SEGUINTE
// Uma fila drenada no quadro seguinte entregaria o evento DEPOIS de o chamador
// ja ter lido zero. Por isso estes testes exigem a resposta no MESMO retorno.
//
// `boolean`, e nao `AEEError`: o valor de recusa tem de ser 0 (FALSE). O ramo
// generico do despacho devolvia `kAeeUnsupported = 20` (`ajudantes.h:56`), e 20
// e TRUE -- o `tectoy` seguia o ramo do sucesso com lixo. Os testes verificam
// EXPRESSAMENTE que a recusa nunca e 20.
// ===========================================================================

constexpr std::uint32_t kBase = 0x00000000u;
constexpr std::uint32_t kPilha = 0x80080000u;
constexpr std::uint32_t kHeap = 0x80200000u;
constexpr std::uint32_t kHeapTam = 0x00C00000u;
constexpr std::uint32_t kTabela = 0x80010000u;
constexpr std::uint32_t kSentinela = 0xFFFFFFF0u;
constexpr std::uint32_t kTamanhoDoModulo = 0x00100000u;

// A pilha de argumentos do teste, o ppObj do create e a celula de resposta:
// tres enderecos DISTINTOS, porque no `tectoy` tambem o sao.
constexpr std::uint32_t kSp = 0x80090000u;
constexpr std::uint32_t kPpObj = 0x80091000u;
constexpr std::uint32_t kSaida = 0x80092000u;
constexpr std::uint32_t kContador = 0x80093000u;

// O applet falso, na memoria do guest. O objecto e a vtable vivem fora do
// modulo (sao dados); o `HandleEvent` vive DENTRO (e codigo do titulo).
constexpr std::uint32_t kApplet = 0x80094000u;
constexpr std::uint32_t kVtableApplet = 0x80095000u;
constexpr std::uint32_t kHandleEvent = 0x00000400u;   // dentro do modulo
constexpr std::uint32_t kHandleReentra = 0x00000500u;  // dentro do modulo
constexpr std::uint32_t kForaDoModulo = 0x70000000u;   // fora, de proposito

constexpr std::uint32_t kClsidDoTitulo = 0x01070798u;  // MEDIDO: corpus62.json
constexpr std::uint32_t kEvtPrefsDb = 0x00007b0au;     // MEDIDO: *0x6a438
constexpr std::uint32_t kEvtflgAsync = 0x0002u;        // AEEShell.h:48
constexpr std::uint32_t kResposta = 0xCAFE0001u;

class Bancada {
 public:
  Bancada() {
    unsetenv("ZB2_ENTRADA");
    saidas_.base = 0xF0000000u;
    saidas_.passo = 4;
    saidas_.quantos = 100000;
    saidas_.ativa = true;
    cpu_.ConfigurarSaidas(saidas_);
    al_ = new Alocador(mem_, kHeap, kHeapTam, nullptr);
    despacho_ = new Despacho(mem_, traco_, *al_, vfs_);
    despacho_->DefinirVtableBitmap(saidas_);
    despacho_->DefinirVtableFicheiro(saidas_.Endereco(kVtableFileObj));
    despacho_->InstalarAjudantes(saidas_, kTabela);
    despacho_->DefinirFaixaDoModulo(kBase, kTamanhoDoModulo);
    cpu_.Repor(kBase, kPilha);
  }
  ~Bancada() {
    delete despacho_;
    delete al_;
  }

  // O applet falso: objecto -> vtable -> [vtable+8] = HandleEvent (slot 2, a
  // ordem do `AEEAppGen.c`: AddRef=0, Release=1, HandleEvent=2).
  void MontarApplet(std::uint32_t handle_event) {
    mem_.Escrever32(kApplet, kVtableApplet);
    mem_.Escrever32(kVtableApplet + 0, 0);
    mem_.Escrever32(kVtableApplet + 4, 0);
    mem_.Escrever32(kVtableApplet + 8, handle_event);
  }

  // O HandleEvent que RESPONDE: escreve no *dwParam e devolve TRUE.
  //   400  e59fc008  ldr r12,[pc,#8]   ; r12 = 0xCAFE0001
  //   404  e583c000  str r12,[r3]      ; *dwParam = r12
  //   408  e3a00001  mov r0,#1         ; TRUE
  //   40c  e12fff1e  bx  lr
  //   410            .word 0xCAFE0001
  void MontarHandlerQueResponde() {
    mem_.Escrever32(kHandleEvent + 0x00, 0xe59fc008u);
    mem_.Escrever32(kHandleEvent + 0x04, 0xe583c000u);
    mem_.Escrever32(kHandleEvent + 0x08, 0xe3a00001u);
    mem_.Escrever32(kHandleEvent + 0x0c, 0xe12fff1eu);
    mem_.Escrever32(kHandleEvent + 0x10, kResposta);
  }

  // O HandleEvent que REENTRA: conta as entregas e volta a chamar o slot 21.
  //   500  e1a0400e  mov r4,lr         ; GUARDA A SENTINELA -- o `mov lr,pc`
  //                                   ; de 0x524 destroi o lr, e sem isto o
  //                                   ; tratador voltava para si proprio (foi
  //                                   ; o que aconteceu na primeira versao
  //                                   ; deste teste: passou VERDE a gastar 4
  //                                   ; milhoes de passos num ciclo).
  //   504  e59fc024  ldr r12,[pc,#36]  ; r12 = &contador
  //   508  e59c1000  ldr r1,[r12]
  //   50c  e2811001  add r1,r1,#1
  //   510  e58c1000  str r1,[r12]
  //   514  e59fc018  ldr r12,[pc,#24]  ; r12 = endereco de saida do slot 21
  //   518  e3a01000  mov r1,#0         ; wFlags = 0
  //   51c  e3a02000  mov r2,#0         ; clsApp = 0 (o applet activo)
  //   520  e3a0307b  mov r3,#0x7b      ; evt
  //   524  e1a0e00f  mov lr,pc         ; lr = 0x52c
  //   528  e12fff1c  bx  r12
  //   52c  e12fff14  bx  r4            ; devolve o que a chamada aninhada deu
  //   530            .word &contador
  //   534            .word saida(slot 21)
  void MontarHandlerQueReentra() {
    mem_.Escrever32(kHandleReentra + 0x00, 0xe1a0400eu);
    mem_.Escrever32(kHandleReentra + 0x04, 0xe59fc024u);
    mem_.Escrever32(kHandleReentra + 0x08, 0xe59c1000u);
    mem_.Escrever32(kHandleReentra + 0x0c, 0xe2811001u);
    mem_.Escrever32(kHandleReentra + 0x10, 0xe58c1000u);
    mem_.Escrever32(kHandleReentra + 0x14, 0xe59fc018u);
    mem_.Escrever32(kHandleReentra + 0x18, 0xe3a01000u);
    mem_.Escrever32(kHandleReentra + 0x1c, 0xe3a02000u);
    mem_.Escrever32(kHandleReentra + 0x20, 0xe3a0307bu);
    mem_.Escrever32(kHandleReentra + 0x24, 0xe1a0e00fu);
    mem_.Escrever32(kHandleReentra + 0x28, 0xe12fff1cu);
    mem_.Escrever32(kHandleReentra + 0x2c, 0xe12fff14u);
    mem_.Escrever32(kHandleReentra + 0x30, kContador);
    mem_.Escrever32(kHandleReentra + 0x34,
                    saidas_.Endereco(kBaseDoShell + brew_slots::kShell_SendEvent));
    mem_.Escrever32(kContador, 0);
  }

  // A chamada do guest, com a ABI do SDK: r1=wFlags r2=clsApp r3=evt
  // [sp]=wParam [sp+4]=dwParam.
  std::uint32_t ChamaSendEvent(std::uint32_t flags, std::uint32_t cls, std::uint32_t evt,
                               std::uint16_t wp, std::uint32_t dwp,
                               std::uint64_t limite = 200000) {
    cpu_.Set(kR0, 0x00001000u);  // o pIShell, que este slot nao usa
    cpu_.Set(kR1, flags);
    cpu_.Set(kR2, cls);
    cpu_.Set(kR3, evt);
    cpu_.Set(kSP, kSp);
    mem_.Escrever32(kSp, wp);
    mem_.Escrever32(kSp + 4, dwp);
    cpu_.Set(kLR, kSentinela);
    cpu_.Set(kPC, saidas_.Endereco(kBaseDoShell + brew_slots::kShell_SendEvent));
    const ResultadoFase r =
        despacho_->Correr(cpu_, limite, kPpObj);
    motivo_ = r.motivo;
    return cpu_.Get(kR0);
  }

  std::size_t Faltas(const std::string& nome) const {
    const auto& f = traco_.ContagemFaltas();
    const auto it = f.find(nome);
    return it == f.end() ? 0 : static_cast<std::size_t>(it->second);
  }
  std::size_t TotalDeFaltas() const {
    std::size_t n = 0;
    for (const auto& par : traco_.ContagemFaltas()) n += static_cast<std::size_t>(par.second);
    return n;
  }

  Memoria& Mem() { return mem_; }
  Despacho& D() { return *despacho_; }
  ArmInterpreter& Cpu() { return cpu_; }
  const std::string& Motivo() const { return motivo_; }

 private:
  Memoria mem_;
  Traco traco_{"teste_sendevent", nullptr};
  Vfs vfs_;
  Alocador* al_ = nullptr;
  Despacho* despacho_ = nullptr;
  Saidas saidas_;
  ArmInterpreter cpu_{mem_, &traco_};
  std::string motivo_;
};

}  // namespace

// 1. A ENTREGA E SINCRONA E A RESPOSTA CHEGA A TEMPO.
//
// Este e o caso do `tectoy`: o applet escreve no `*dwParam` e o chamador le-o
// na instrucao seguinte ao retorno (0x6a3a0, `ldrne r0,[sp,#8]`).
TEST(SendEvent, EntregaSincronaEAsRespostaChegaNoMesmoRetorno) {
  Bancada b;
  b.D().SituarTitulo("/nao/existe", "274755", kClsidDoTitulo);
  b.MontarApplet(kHandleEvent);
  b.MontarHandlerQueResponde();
  b.D().DefinirApplet(kApplet);
  b.Mem().Escrever32(kSaida, 0);

  const std::uint32_t r = b.ChamaSendEvent(0, kClsidDoTitulo, kEvtPrefsDb, 4, kSaida);

  EXPECT_EQ(r, 1u) << "o applet devolveu TRUE; o slot tem de devolver o que ele deu";
  EXPECT_NE(r, kAeeUnsupported) << "20 nao e um `boolean`";
  EXPECT_EQ(b.Mem().Ler32(kSaida), kResposta)
      << "a resposta do applet tem de estar no *dwParam JA no retorno";
}

// 1b. O CONTEXTO DO CHAMADOR VOLTA INTACTO.
//
// A entrega corre codigo do guest por cima do guest: se os registos do chamador
// nao voltarem, o quadro em curso DELE fica estragado (o mesmo cuidado de
// `core/brew/imedia.cpp:397-403`).
TEST(SendEvent, OsRegistosDoChamadorVoltamIntactos) {
  Bancada b;
  b.D().SituarTitulo("/nao/existe", "274755", kClsidDoTitulo);
  b.MontarApplet(kHandleEvent);
  b.MontarHandlerQueResponde();
  b.D().DefinirApplet(kApplet);
  for (int r = 4; r <= 12; ++r) b.Cpu().Set(r, 0xA0000000u + static_cast<std::uint32_t>(r));
  const std::uint32_t cpsr = b.Cpu().Cpsr();

  (void)b.ChamaSendEvent(0, kClsidDoTitulo, kEvtPrefsDb, 4, kSaida);

  for (int r = 4; r <= 12; ++r) {
    EXPECT_EQ(b.Cpu().Get(r), 0xA0000000u + static_cast<std::uint32_t>(r)) << "r" << r;
  }
  EXPECT_EQ(b.Cpu().Get(kSP), kSp);
  EXPECT_EQ(b.Cpu().Cpsr(), cpsr);
}

// 2. A REENTRANCIA PARA NUM TECTO, e nao em ciclo infinito.
TEST(SendEvent, AReentranciaParaNoTecto) {
  Bancada b;
  b.D().SituarTitulo("/nao/existe", "274755", kClsidDoTitulo);
  b.MontarApplet(kHandleReentra);
  b.MontarHandlerQueReentra();
  b.D().DefinirApplet(kApplet);

  const std::uint32_t r = b.ChamaSendEvent(0, kClsidDoTitulo, kEvtPrefsDb, 4, kSaida);

  EXPECT_EQ(b.Mem().Ler32(kContador), 4u)
      << "o tecto de aninhamento e 4 (como zeebx, machine/mod.rs:1541)";
  EXPECT_EQ(r, 0u) << "a entrega mais funda foi recusada: FALSE sobe pela cadeia";
  EXPECT_NE(r, kAeeUnsupported);
  // A CADEIA TEM DE VOLTAR PELA SENTINELA, e nao ficar a gastar o orcamento: sem
  // esta linha o teste passava com o tratador em ciclo (ver `MontarHandlerQueReentra`).
  EXPECT_EQ(b.Motivo(), "retornou");
  EXPECT_EQ(b.Faltas("IShell::SendEvent(aninhamento)"), 1u);
}

// 3. O APPLET AINDA NAO ESTA REGISTADO: le-se o `ppObj` do `CreateInstance`.
//
// O `tectoy` manda este evento DE DENTRO do `CreateInstance`
// (zeebulator core/brew/ishell.cpp:200-207, zeebx machine/signal.rs:232-241),
// e a bateria so chama `DefinirApplet` DEPOIS do create (tools/bateria.cpp:785).
TEST(SendEvent, ForaDoRegistoOAppletSaiDoPpObj) {
  Bancada b;
  b.D().SituarTitulo("/nao/existe", "274755", kClsidDoTitulo);
  b.MontarApplet(kHandleEvent);
  b.MontarHandlerQueResponde();
  // NAO chamar DefinirApplet: e o estado de dentro do create.
  b.Mem().Escrever32(kPpObj, kApplet);
  b.Mem().Escrever32(kSaida, 0);

  const std::uint32_t r = b.ChamaSendEvent(0, kClsidDoTitulo, kEvtPrefsDb, 4, kSaida);

  EXPECT_EQ(r, 1u);
  EXPECT_EQ(b.Mem().Ler32(kSaida), kResposta);
}

// 3b. O MESMO VALE PARA O AJUDANTE `GetAppInstance` (AEEHelperFuncs 0x0C0).
//
// MEDIDO no `Rolimaz` (276809): o motor chama o ajudante 0x0c0 DENTRO do seu
// proprio `CreateInstance` (chamada em `pc=0x000047b8`, `lr=0x000047bc`) e usa o
// resultado como OBJECT0 -- `ldr r0,[r4,#0xc]` em `0x0000480c`, com `r4` = a
// resposta. Com `applet_` a zero a resposta e 0, a leitura de `[0+0xc]` cai no
// CABECALHO DO PROPRIO MODULO (`[0x0c]` = 1), o `1` passa a ser tratado como
// ponteiro, a leitura de `[1]` devolve `0x000fea00`, o `blx` seguinte salta para
// 0 e a entrada do modulo corre uma SEGUNDA vez. E o grupo G2 (9 titulos).
//
// A RESPOSTA CERTA E A MESMA QUE O `SendEvent` ja usa
// (`despacho.cpp:2262-2267`): enquanto o `CreateInstance` nao retorna, o applet
// e o que o `AEEApplet_New` do GUEST ja escreveu no `ppObj`
// (zeebx `src/machine/helper.rs`, caso "GetAppInstance").
TEST(GetAppInstance, DentroDoCreateOAppletSaiDoPpObj) {
  Bancada b;
  b.D().SituarTitulo("/nao/existe", "274755", kClsidDoTitulo);
  // NAO chamar DefinirApplet: e o estado de DENTRO do create.
  b.Mem().Escrever32(kPpObj, kApplet);

  // A CHAMADA E PELA TABELA, e nao pelo id interno: quem chama o id interno
  // testa o despacho e nao a cablagem -- o offset 0x0c0 tem de estar LIGADO.
  const std::uint32_t alvo = b.Mem().Ler32(kTabela + 0x0c0u);
  EXPECT_NE(alvo, 0u) << "o offset 0x0c0 nao esta cablado na tabela";
  b.Cpu().Set(kLR, kSentinela);
  b.Cpu().Set(kPC, alvo);
  (void)b.D().Correr(b.Cpu(), 1000, kPpObj);

  EXPECT_EQ(b.Cpu().Get(kR0), kApplet)
      << "o `GetAppInstance` dentro do create tem de devolver o ppObj, e nao 0";
}

// 3c. COM O APPLET JA REGISTADO, e ELE que responde (o `ppObj` nao ganha).
TEST(GetAppInstance, ComOAppletRegistadoRespondeEle) {
  constexpr std::uint32_t kAppletRegistado = 0x80096000u;
  Bancada b;
  b.D().SituarTitulo("/nao/existe", "274755", kClsidDoTitulo);
  b.D().DefinirApplet(kAppletRegistado);
  b.Mem().Escrever32(kPpObj, kApplet);

  const std::uint32_t alvo = b.Mem().Ler32(kTabela + 0x0c0u);
  b.Cpu().Set(kLR, kSentinela);
  b.Cpu().Set(kPC, alvo);
  (void)b.D().Correr(b.Cpu(), 1000, kPpObj);

  EXPECT_EQ(b.Cpu().Get(kR0), kAppletRegistado);
}

// 4a. O PostEvent (EVTFLG_ASYNC) e RECUSADO EM VOZ ALTA -- nao ha fila.
TEST(SendEvent, OAsincronoERecusadoComNome) {
  Bancada b;
  b.D().SituarTitulo("/nao/existe", "274755", kClsidDoTitulo);
  b.MontarApplet(kHandleEvent);
  b.MontarHandlerQueResponde();
  b.D().DefinirApplet(kApplet);
  b.Mem().Escrever32(kSaida, 0);

  const std::uint32_t r = b.ChamaSendEvent(kEvtflgAsync, kClsidDoTitulo, kEvtPrefsDb, 4, kSaida);

  EXPECT_EQ(r, 0u) << "FALSE, e nunca 20";
  EXPECT_NE(r, kAeeUnsupported);
  EXPECT_EQ(b.Faltas("IShell::SendEvent(EVTFLG_ASYNC)"), 1u) << "a recusa tem de ter NOME";
  EXPECT_EQ(b.Mem().Ler32(kSaida), 0u) << "nao foi entregue: nada escrito";
}

// 4b. UMA CLASSE QUE NAO E A DO TITULO nao tem destino aqui.
TEST(SendEvent, ClasseAlheiaERecusada) {
  Bancada b;
  b.D().SituarTitulo("/nao/existe", "274755", kClsidDoTitulo);
  b.MontarApplet(kHandleEvent);
  b.MontarHandlerQueResponde();
  b.D().DefinirApplet(kApplet);
  b.Mem().Escrever32(kSaida, 0);

  const std::uint32_t r = b.ChamaSendEvent(0, 0x01234567u, kEvtPrefsDb, 4, kSaida);

  EXPECT_EQ(r, 0u);
  EXPECT_NE(r, kAeeUnsupported);
  EXPECT_EQ(b.Mem().Ler32(kSaida), 0u);
}

// 4c. UM `HandleEvent` FORA DO MODULO nao se chama -- a mesma guarda que a
// entrada ja aplica aos callbacks (core/brew/despacho.h:172-181).
TEST(SendEvent, HandleEventForaDoModuloERecusado) {
  Bancada b;
  b.D().SituarTitulo("/nao/existe", "274755", kClsidDoTitulo);
  b.MontarApplet(kForaDoModulo);
  b.D().DefinirApplet(kApplet);
  b.Mem().Escrever32(kSaida, 0);

  const std::uint32_t r = b.ChamaSendEvent(0, kClsidDoTitulo, kEvtPrefsDb, 4, kSaida);

  EXPECT_EQ(r, 0u);
  EXPECT_NE(r, kAeeUnsupported);
  EXPECT_EQ(b.Faltas("IShell::SendEvent"), 1u) << "a recusa tem de ter NOME";
}

// 4d. SEM APPLET NENHUM (nem registado nem no ppObj): FALSE, e nunca 20.
TEST(SendEvent, SemAppletDevolveFalseENunca20) {
  Bancada b;
  b.D().SituarTitulo("/nao/existe", "274755", kClsidDoTitulo);
  b.Mem().Escrever32(kPpObj, 0);

  const std::uint32_t r = b.ChamaSendEvent(0, kClsidDoTitulo, kEvtPrefsDb, 4, kSaida);

  EXPECT_EQ(r, 0u);
  EXPECT_NE(r, kAeeUnsupported);
}


// 9. O RELOGIO E OS TEMPORIZADORES PARAM DURANTE A ENTREGA.
//
// A GUARDA QUE FALTAVA PROVAR. O commit que a escreveu dizia, em voz alta, que
// ela NAO tinha sido provada por violacao. Isto e essa prova.
//
// O `SendEvent` corre o `HandleEvent` do applet por DENTRO do laco de quadro,
// com um `Correr` aninhado. Se o relogio virtual continuasse a andar la dentro,
// um temporizador de quadro podia disparar NO MEIO do tratador -- reentrancia
// que o BREW nunca faz, e que poria dois callbacks do titulo a partilhar os
// mesmos registadores.
//
// A entrega e instantanea para o guest: ele le a resposta na instrucao SEGUINTE
// ao retorno (`tectoy.mod:0x6a3a0`, `ldrne r0,[sp,#8]`).
TEST(SendEvent, ORelogioNaoAndaEnquantoOEventoEEntregue) {
  Bancada b;
  b.D().SituarTitulo("/nao/existe", "274755", kClsidDoTitulo);
  b.MontarApplet(kHandleEvent);
  b.MontarHandlerQueResponde();
  b.D().DefinirApplet(kApplet);
  b.Mem().Escrever32(kSaida, 0);

  const std::uint32_t antes = b.D().AgoraMs();
  const std::uint32_t r = b.ChamaSendEvent(0, kClsidDoTitulo, kEvtPrefsDb, 4, kSaida);
  ASSERT_EQ(r, 1u) << "a entrega tem de correr para este teste medir o que ela faz";
  EXPECT_EQ(b.Mem().Ler32(kSaida), kResposta) << "correu mesmo codigo do titulo";

  EXPECT_EQ(b.D().AgoraMs(), antes)
      << "o relogio virtual andou durante a entrega: um temporizador podia ter "
         "disparado no meio do HandleEvent";
}

}  // namespace zb2::brew
