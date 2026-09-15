#include "core/brew/classes.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/brew/clsids.h"
#include "core/brew/ecra.h"
#include "core/brew/egl.h"   // NomeDoIidDaFamiliaGl + kIidEgl10/11
#include "core/brew/igl.h"
#include "tools/brew_slots.inc"
// A CONTAGEM DO QEGL prende-se ao IEGL gerado: o QEGL e o IEGL sem o
// `GetProcAddress` (slot 8). Ver o `static_assert` no fim das tabelas abaixo --
// o `27` deixou de ser um numero escrito neste ficheiro.
#include "tools/gl_slots.inc"
// Os TRES numeros. Vem do mesmo `.inc` gerado que o `NomeDoClsid` usa, e nao de
// literais escritos neste ficheiro: e essa a unica forma de o nome e o numero
// nao poderem divergir.
#include "tools/clsids.inc"
// OS 148 NOMES DO IGLES11, GERADOS de `AEEGLES10.h` + `AEEGLES11.h`
// (`tools/nomear_igles.py`, guarda `tools/verificar_slots_igles.sh`). Antes
// disto a lista de demanda tinha DEZASSETE entradas `IGLES11::slotN` -- e a
// maior, o `slot67`, e pedida por dez titulos e chama-se `GetString`.
#include "tools/igles_slots.inc"

namespace zb2::brew {

namespace {

// O nome do slot de cada classe, vindo da tabela GERADA (`tools/brew_slots.inc`,
// gerada de `AEEText.h`, `AEEIAppHistory.h`, `AEEIValueModel.h`, `AEEThread.h` +
// `AEEIRscPool.h` e `AEEIImageDecoder.h`). Um nome de slot escrito a mao seria
// exactamente o numero transcrito de memoria que o gerador existe para impedir.
//
// O `IThread` E O `IImageDecoder` SAIRAM DAQUI. Tinham um `switch` escrito neste
// ficheiro, com os nomes e as CONTAGENS (12 e 5) transcritos do cabecalho; agora
// sao o `NomeDeThread`/`NomeDeImageDecoder` e o `kThreadSlots`/
// `kImageDecoderSlots` do `.inc`. A cadeia (`INHERIT_IThread` ->
// `INHERIT_IRscPool` -> `INHERIT_IQI`) e lida pelo gerador: o slot 7 e o `Start`
// porque o cabecalho o poe la, e nao porque alguem o contou.
//
// O `QEGL` E A EXCECAO, e ela FICA -- a razao esta escrita e e uma medicao:
// nenhum cabecalho deste SDK o declara (so aparece em comentarios de teste OpenVG
// e numa tabela propria do zeebulator). Nomear os 24 metodos EGL que faltam a
// partir de outro emulador seria inventar ABI e, pior, mudar o nome das recusas
// (`QEGL::?` -> `QEGL::eglSwapBuffers`) sem uma fonte do SDK que o sustente. So
// os tres slots do `INHERIT_IQI` ficam nomeados, e o resto diz "?".
//
// A CONTAGEM do QEGL, essa, deixou de ser um `27` solto: ver o `static_assert`
// mais abaixo, que a prende ao `kIeglSlots` GERADO de `AEEGL.h`.
const char* NomeDoSlotQEGL(unsigned slot) {
  switch (slot) {
    case 0: return "AddRef";
    case 1: return "Release";
    case 2: return "QueryInterface";
    default: return "?";
  }
}

const char* NomeDoSlotCM(unsigned slot) {
  switch (slot) {
    case 0: return "AddRef";
    case 1: return "Release";
    case 2: return "QueryInterface";
    case 28: return "GetSSInfo";
    default: return "?";
  }
}

const char* (*const kNomeDoSlot[])(unsigned) = {
    &brew_slots::NomeDeAppHistory,
    &brew_slots::NomeDeValueModel,
    &brew_slots::NomeDeTextCtl,
    &brew_slots::NomeDeThread,
    &brew_slots::NomeDeImageDecoder,
    &NomeDoSlotQEGL,
    &NomeDoSlotCM,
};

// Quantos slots cada interface TEM, do mesmo cabecalho. As cinco primeiras vem
// do `.inc` GERADO -- nome e contagem da MESMA leitura, para nao divergirem.
const std::uint32_t kSlotsDaInterface[] = {
    brew_slots::kAppHistorySlots,
    brew_slots::kValueModelSlots,
    brew_slots::kTextCtlSlots,
    brew_slots::kThreadSlots,
    brew_slots::kImageDecoderSlots,
    kQeglSlots,
    29,
};

// E OS NUMEROS QUE ESTAVAM A MAO, CONFERIDOS. Nao e decoracao: era aqui que o
// `12`, o `5` e o `27` viviam, e um deles estar errado fazia o despacho registar
// `IThread::slot12` (fora da tabela) em vez do metodo pedido. Se o cabecalho do
// SDK mudar, e a COMPILACAO que cai, e nao a bateria quatro minutos depois.
static_assert(brew_slots::kThreadSlots == 12, "o IThread tem 12 slots (IQI 3 + RscPool 4 + 5)");
static_assert(brew_slots::kImageDecoderSlots == 5, "o IImageDecoder tem 5 slots (IQI 3 + 2)");
static_assert(brew_slots::kThread_Start == 7, "medido: `[r1,#0x1c]` = slot 7 e o Start");
static_assert(brew_slots::kThread_ReleaseRsc == 6, "o fim do IRscPool e o slot 6");
static_assert(brew_slots::kImageDecoder_GetBitmap == 3, "medido: o GetBitmap e o slot 3");
// O QEGL e o IEGL SEM o `GetProcAddress` (slot 8 do IEGL): 28 - 1 = 27. O 28 vem
// de `AEEGL.h` pelo `tools/gl_slots.inc` -- a mesma fonte que o IGL usa.
static_assert(kQeglSlots == gl_slots::kIeglSlots - 1,
              "o QEGL e o IEGL sem GetProcAddress (kIeglSlots - 1)");

// OS NOMES QUE A DEMANDA VAI MOSTRAR. O CLSID vem da constante gerada em
// `tools/clsids.inc` -- e nao de um literal escrito aqui.
struct Ficha {
  std::uint32_t clsid;    // o numero, da constante GERADA (`tools/clsids.inc`)
  const char* classe;     // o nome do CLSID, como o SDK o declara
  const char* interface;  // o nome da interface que o objecto entrega
};

constexpr Ficha kFichas[kQuantasClasses] = {
    {brew_clsids::kClsid_AppHistory, "AEECLSID_AppHistory", "IAppHistory"},
    {brew_clsids::kClsid_VALUEMODEL_1, "AEECLSID_VALUEMODEL_1", "IValueModel"},
    {brew_clsids::kClsid_TEXTCTL, "AEECLSID_TEXTCTL", "ITextCtl"},
    {brew_clsids::kClsid_THREAD, "AEECLSID_THREAD", "IThread"},
    {brew_clsids::kClsid_PNGDECODER_BREW, "AEECLSID_PNGDECODER_BREW", "IImageDecoder"},
    {0x0103d8ecu, "AEECLSID_QEGL", "QEGL"},
    {0x01011810u, "AEECLSID_CM", "ICM"},
};

// OS TRES CLSIDs, lidos do `.inc` gerado. Se um deles divergir do cabecalho, a
// guarda `clsids_do_sdk` falha o `ctest` -- que e a unica prova que interessa.
static_assert(brew_clsids::kClsid_AppHistory == 0x0100104fu,
              "AEECLSID_AppHistory tem de ser 0x0100104f (AEEAppHistory.bid:9)");
static_assert(brew_clsids::kClsid_TEXTCTL == 0x01003109u,
              "AEECLSID_TEXTCTL tem de ser 0x01003109 (AEEClassIDs.h:209)");
static_assert(brew_clsids::kClsid_VALUEMODEL_1 == 0x01028e3cu,
              "AEECLSID_VALUEMODEL_1 tem de ser 0x01028e3c (AEECLSID_VALUEMODEL_1.bid:31)");
static_assert(brew_clsids::kClsid_THREAD == 0x01001017u,
              "AEECLSID_THREAD tem de ser 0x01001017 (AEEClassIDs.h:84)");
static_assert(brew_clsids::kClsid_PNGDECODER_BREW == 0x01030766u,
              "AEECLSID_PNGDECODER_BREW tem de ser 0x01030766 (AEECPNGDecoderBREW.h:27)");

// O `IAppHistory` TEM 16 slots, e o `Top` e o slot 5 -- nao um numero escrito
// aqui: sai da cadeia de heranca (`INHERIT_IQI` = 3, mais `Forward`, `Back`,
// `Top`). O `tectoy` chama-o de facto (`0x6c3e8`, `ldr r2,[r1,#0x14]`).
static_assert(brew_slots::kAppHistory_Top == 5, "o Top do IAppHistory e o slot 5");

const std::size_t kClasseDoAppHistory = static_cast<std::size_t>(Classe::kAppHistory);

}  // namespace

std::uint32_t IndiceDaClasse(std::uint32_t clsid) {
  for (std::uint32_t k = 0; k < kQuantasClasses; ++k) {
    if (kFichas[k].clsid == clsid) return k;
  }
  return kQuantasClasses;
}

const char* NomeDaClasse(std::uint32_t k) {
  return k < kQuantasClasses ? kFichas[k].classe : "?";
}

const char* NomeDaInterface(std::uint32_t k) {
  return k < kQuantasClasses ? kFichas[k].interface : "?";
}

const char* NomeDoSlotDaClasse(std::uint32_t k, std::uint32_t slot) {
  return k < kQuantasClasses ? kNomeDoSlot[k](slot) : "?";
}

std::uint32_t ObjetoDoClsid(std::uint32_t clsid) {
  const std::uint32_t k = IndiceDaClasse(clsid);
  return k < kQuantasClasses ? ObjetoDaClasse(k) : 0;
}

namespace {

// Estado do unico ITextCtl (0x8F002000). Void methods sem retorno; o guest le
// de volta via IsActive/GetInputMode. Sem Memoria aqui: SetRect aceita o
// ponteiro sem o ler (validar rect exigiria Memoria no AtenderClasse).
std::uint32_t g_texto_ativo = 0;
std::uint32_t g_texto_props = 0;
std::int32_t g_texto_modo = 0;

}  // namespace

void ReporEstadoTextCtl() {
  g_texto_ativo = 0;
  g_texto_props = 0;
  g_texto_modo = 0;
}

// ---------------------------------------------------------------------------
// O IThread cooperativo (frente `ithread`).
//
// O QUE ISTO E, MEDIDO NA BATERIA (62 titulos, ZB2_QUADROS=300 ZB2_EVT_START=1,
// HEAD 68f5238): 22 titulos pedem `IThread::Start` UMA vez. As pilhas pedidas:
// dez titulos pedem 0x4000 (16 KiB), oito pedem 0x80000 (512 KiB), um pede
// 0x100000 (1 MiB), um pede 0x40000, um pede 0x10000 e um pede 0x4000 com pfn
// proprio. O `zeeboids` arma a fila num callback de temporizador
// (SetTimer(10 ms) -> callback -> CreateInstance -> Start; detalhes no
// relatorio 18-ithread.md). Nenhum titulo pede outro metodo do IThread.
//
// O DESENHO, COOPERATIVO e o do zeebx (`src/machine/thread.rs`, e o `IThread`
// novo em `/tmp/zx-new/src/machine.rs:10633-10760`): o `Start` aloca a pilha,
// guarda o `resume_pc`, poe `r0=this`, `r1=arg`, `sp=topo` e AGENDA a thread;
// o `Exit` termina e devolve o controlo; o `Suspend` e o ponto onde a thread
// devolve o controlo; o `GetResumeCBK` e o endereco pelo qual o
// `ISHELL_Resume` reconhece a thread. A ASSINATURA de cada metodo esta no
// cabecalho do SDK (`AEEThread.h`, `INHERIT_IThread`).
//
// O QUE ESTA FORA DESTE FICHEIRO, DE PROPOSITO: a CORRIDA da thread. O zeebx
// retoma as threads pendentes uma volta por quadro, no laço de eventos -- que
// neste motor e o `Despacho::Correr`, um ficheiro proibido para esta frente. O
// CONTRATO dessa ligacao esta em `classes.h` (`TemThreadPendente`,
// `PrepararRetomadaDeThread`, `ConcluirRetomadaDeThread`,
// `EnfileirarThreadPeloCallbackDeRetomada`) e e provado por testes; o
// `despacho.cpp` (outra frente) chama-o. Sem a ligacao, uma thread aceite pelo
// `Start` fica pendente e NAO corre: e o comportamento medido desta frente.
// ---------------------------------------------------------------------------
namespace {

// O estado de UMA thread, por endereco de objecto. So os cinco metodos da
// propria thread tem estado (`Start`, `Exit`, `Join`, `Suspend`,
// `GetResumeCBK`); os quatro do pool herdado (`Malloc`, `Free`, `HoldRsc`,
// `ReleaseRsc`) continuam a recusar com o nome.
struct EstadoDeThread {
  bool iniciada = false;
  bool terminada = false;
  bool suspensa = false;
  // `true` = e esta a thread que o hospedeiro retomou e que esta a correr neste
  // momento (`PrepararRetomadaDeThread` a marcou). E o que distingue um
  // `Suspend`/`Exit` de DENTRO da thread (cede o controlo) de um de FORA
  // ("has no effect" / "may be called from inside or outside", AEEThread.h).
  bool correndo = false;
  std::uint32_t pilha = 0;
  std::uint32_t retomar_pc = 0;  // o pfn no arranque; o lr no Suspend
  std::uint32_t rv = 0;
  std::uint32_t retomada_cbk = 0;
  // r0..r12 e sp, como o `THREAD_REGS` do zeebx. O PC guarda-se em
  // `retomar_pc`; o lr e sempre a sentinela quando a thread corre.
  std::uint32_t contexto[14] = {};
  struct Juntador {
    std::uint32_t pcb;
    std::uint32_t pn_rv;
  };
  std::vector<Juntador> juntadores;
};

std::map<std::uint32_t, EstadoDeThread> g_threads;
std::vector<std::uint32_t> g_pendentes;
std::uint32_t g_thread_corrente = 0;
// O `bump` do pool de pilhas. Avanca para tras nunca: o pool repõe-se por
// corrida em `ReporEstadoThreads` (via `ConstruirClasses`). Um pedido que nao
// caiba devolve 0, e o `Start` responde ENOMEMORY -- o codigo que o cabecalho
// promete ("ENOMEMORY: if the stack size requested could not be allocated").
std::uint32_t g_proxima_pilha = kPilhasDeThreadInicio;
// O `AEECallback` do BREW tem 28 bytes (zeebx `CALLBACK_SIZE`). O conteudo nao
// e usado: para o `ISHELL_Resume` do despacho, o ENDERECO do callback identifica
// a thread (zeebx `resume_callbacks`).
constexpr std::uint32_t kTamanhoDoCallbackDeRetomada = 28;
// O PISO DE UMA PILHA. Um pedido de zero bytes nao e uma thread (e um bug do
// jogo), e um bloco vazio poria o `sp` inicial igual a base -- a primeira
// escrita da thread cairia fora do bloco. O piso resolve os dois casos; nao e
// uma medida, e por isso fica dito.
constexpr std::uint32_t kMinimoDePilha = 0x1000;

std::uint32_t AlocarNoPoolDePilhas(std::uint32_t pedido) {
  const std::uint32_t bloco = (pedido + 7u) & ~static_cast<std::uint32_t>(7u);
  const std::uint32_t fim = g_proxima_pilha + bloco;
  if (bloco == 0 || fim > kPilhasDeThreadInicio + kPilhasDeThreadTamanho) return 0;
  const std::uint32_t p = g_proxima_pilha;
  g_proxima_pilha = fim;
  return p;
}

void ReporEstadoThreads() {
  g_threads.clear();
  g_pendentes.clear();
  g_thread_corrente = 0;
  g_proxima_pilha = kPilhasDeThreadInicio;
}

// Encerra uma thread: marca a terminação, escreve o rv nos ponteiros de saida
// dos juntadores e limpa a agenda. O callback de um juntador NAO e entregue:
// nao ha fila de callbacks de guest alcancavel deste ficheiro (o despacho nao
// tem `pending_calls`), e isso fica dito num pressuposto, nao escondido. A
// pilha fica ocupada ate ao `ReporEstadoThreads` da proxima corrida -- o bump
// nao devolve blocos, e por isso nao se finge que devolve.
void TerminarThread(ICpu& cpu, Traco& traco, std::uint32_t this_,
                    std::uint32_t rv) {
  const auto it = g_threads.find(this_);
  if (it == g_threads.end()) return;
  EstadoDeThread& t = it->second;
  t.terminada = true;
  t.suspensa = false;
  t.rv = rv;
  t.pilha = 0;
  g_pendentes.erase(std::remove(g_pendentes.begin(), g_pendentes.end(), this_),
                    g_pendentes.end());
  for (const auto& j : t.juntadores) {
    if (j.pn_rv != 0) cpu.Mem().Escrever32(j.pn_rv, rv);
    if (j.pcb != 0) {
      traco.RegistarPressuposto(Area::Brew, "IThread::Join",
                                "o callback do juntador nao e entregue (nao ha fila "
                                "de callbacks alcancavel deste ficheiro)");
    }
  }
  t.juntadores.clear();
}

// Os cinco metodos da propria thread. `false` = slot fora destes cinco (cai no
// ramo generico, que recusa COM O NOME -- e o que os testes esperam para o
// `Malloc` etc.).
bool AtenderThread(ICpu& cpu, Traco& traco, std::uint32_t slot) {
  const std::uint32_t this_ = cpu.Get(kR0);
  switch (slot) {
    case brew_slots::kThread_Start: {
      // int Start(IThread*, int nStackSz, PFNTHREAD pfStart, void* pvStart)
      const std::uint32_t pedido = cpu.Get(kR1);
      const std::uint32_t pfn = cpu.Get(kR2);
      const std::uint32_t arg = cpu.Get(kR3);
      EstadoDeThread& t = g_threads[this_];
      if (t.iniciada) {
        // "IThreads are not re-useable: An _Start() may only be called once"
        // (AEEThread.h) -- EALREADY.
        cpu.Set(kR0, kAeeAlready);
        return true;
      }
      // GUARDA, e nao medida: uma thread começa por uma funcao, e um pfn nulo
      // nao e uma thread. Nenhum dos 22 titulos pede pfn nulo.
      if (pfn == 0) {
        cpu.Set(kR0, kAeeBadParm);
        return true;
      }
      const std::uint32_t bloco = std::max(pedido, kMinimoDePilha);
      const std::uint32_t pilha = AlocarNoPoolDePilhas(bloco);
      if (pilha == 0) {
        // "ENOMEMORY: if the stack size requested could not be allocated"
        // (AEEThread.h). Nada fica iniciado: um Start falhado pode repetir-se.
        cpu.Set(kR0, kAeeNoMemory);
        return true;
      }
      // A pilha do ARM cresce para baixo: o topo do bloco e o sp inicial, e a
      // AAPCS pede alinhamento a 8 (zeebx: `& !7`).
      const std::uint32_t topo = (pilha + bloco) & ~static_cast<std::uint32_t>(7u);
      t.iniciada = true;
      t.terminada = false;
      t.suspensa = false;
      t.pilha = pilha;
      t.retomar_pc = pfn;
      std::memset(t.contexto, 0, sizeof(t.contexto));
      t.contexto[kR0] = this_;   // r0 = this (a assinatura da PFNTHREAD)
      t.contexto[kR1] = arg;     // r1 = pvStart
      t.contexto[kSP] = topo;    // sp = topo
      g_pendentes.push_back(this_);
      char det[128];
      std::snprintf(det, sizeof(det), "pilha=0x%08x..0x%08x pfn=0x%08x arg=0x%08x",
                    pilha, topo, pfn, arg);
      traco.Emitir(Area::Brew, Nivel::Informacao, "ITHREAD_INICIADA", det);
      cpu.Set(kR0, kAeeSuccess);
      return true;
    }
    case brew_slots::kThread_Exit: {
      // int Exit(IThread*, int nRv) -- "may be called from inside or outside
      // the thread". De DENTRO nao volta: a thread devolve o controlo.
      const auto it = g_threads.find(this_);
      if (it == g_threads.end() || !it->second.iniciada) {
        // "EFAILED: if the IThread's never been _Start()ed" (AEEThread.h).
        cpu.Set(kR0, kAeeFailed);
        return true;
      }
      if (it->second.terminada) {
        // "EALREADY: if the IThread is already stopped" (AEEThread.h).
        cpu.Set(kR0, kAeeAlready);
        return true;
      }
      const bool de_dentro = it->second.correndo;
      TerminarThread(cpu, traco, this_, cpu.Get(kR1));
      cpu.Set(kR0, kAeeSuccess);
      if (de_dentro) cpu.Set(kLR, kSentinelaDoHospedeiro);
      return true;
    }
    case brew_slots::kThread_Join: {
      // void Join(IThread*, AEECallback* pcb, int* pnRv)
      const std::uint32_t pcb = cpu.Get(kR1);
      const std::uint32_t pn_rv = cpu.Get(kR2);
      EstadoDeThread& t = g_threads[this_];
      if (t.terminada) {
        if (pn_rv != 0) cpu.Mem().Escrever32(pn_rv, t.rv);
        if (pcb != 0) {
          traco.RegistarPressuposto(Area::Brew, "IThread::Join",
                                    "a thread ja terminou e o callback do juntador "
                                    "nao e entregue (nao ha fila de callbacks)");
        }
      } else {
        t.juntadores.push_back({pcb, pn_rv});
      }
      return true;  // void: como o cabecalho, nao escreve r0
    }
    case brew_slots::kThread_Suspend: {
      // void Suspend(IThread*) -- "must only be called from within the
      // IThread. Calling from outside the IThread has no effect"
      // (AEEThread.h). E O PONTO onde a thread devolve o controlo.
      const auto it = g_threads.find(this_);
      if (it == g_threads.end() || !it->second.iniciada) return true;
      EstadoDeThread& t = it->second;
      if (!t.correndo || t.terminada) return true;
      // O retorno DENTRO da thread e o lr de agora (a instrucao a seguir a
      // chamada do Suspend); os registos guardam-se como estao (zeebx
      // `Suspend`). A sentinela no lr e o "devolve o controlo ao hospedeiro"
      // desta arvore: o laco do despacho ve o pc na sentinela e sai.
      t.retomar_pc = cpu.Get(kLR);
      for (std::uint32_t k = kR0; k <= kSP; ++k) t.contexto[k] = cpu.Get(kR0 + k);
      t.suspensa = true;
      cpu.Set(kLR, kSentinelaDoHospedeiro);
      return true;  // void
    }
    case brew_slots::kThread_GetResumeCBK: {
      // AEECallback* GetResumeCBK(IThread*) -- "the same callback to each
      // call, because it is by it that we recognize an ISHELL_Resume directed
      // at the thread" (zeebx). O despacho usa
      // `EnfileirarThreadPeloCallbackDeRetomada` com o endereco.
      EstadoDeThread& t = g_threads[this_];
      if (t.retomada_cbk == 0) {
        t.retomada_cbk = AlocarNoPoolDePilhas(kTamanhoDoCallbackDeRetomada);
      }
      cpu.Set(kR0, t.retomada_cbk);
      return true;
    }
    default:
      return false;
  }
}

}  // namespace

bool TemThreadPendente() {
  for (const std::uint32_t th : g_pendentes) {
    const auto it = g_threads.find(th);
    if (it != g_threads.end() && it->second.iniciada && !it->second.terminada) return true;
  }
  return false;
}

bool PrepararRetomadaDeThread(ICpu& cpu, Traco& traco) {
  while (!g_pendentes.empty()) {
    const std::uint32_t th = g_pendentes.front();
    g_pendentes.erase(g_pendentes.begin());
    const auto it = g_threads.find(th);
    if (it == g_threads.end() || !it->second.iniciada || it->second.terminada) continue;
    EstadoDeThread& t = it->second;
    // O CONTEXTO da thread, todo: r0..r12 e sp. O lr e a sentinela -- a thread
    // corre "para o sistema" e e o laco do despacho que a faz andar ate ao
    // proximo Suspend/Exit. O `Bx` respeita o bit 0 (ARM/Thumb), como o
    // `PrepararCallbackDoTemporizador` do despacho.
    for (std::uint32_t k = kR0; k <= kSP; ++k) cpu.Set(kR0 + k, t.contexto[k]);
    cpu.Set(kLR, kSentinelaDoHospedeiro);
    cpu.Bx(t.retomar_pc);
    t.suspensa = false;
    t.correndo = true;
    g_thread_corrente = th;
    char det[96];
    std::snprintf(det, sizeof(det), "objeto=0x%08x pc=0x%08x sp=0x%08x", th,
                  t.retomar_pc, t.contexto[kSP]);
    traco.Emitir(Area::Brew, Nivel::Depuracao, "ITHREAD_RETOMADA", det);
    return true;
  }
  return false;
}

void ConcluirRetomadaDeThread(ICpu& cpu, Traco& traco) {
  if (g_thread_corrente == 0) return;
  const std::uint32_t th = g_thread_corrente;
  g_thread_corrente = 0;
  const auto it = g_threads.find(th);
  if (it == g_threads.end()) return;
  EstadoDeThread& t = it->second;
  t.correndo = false;
  if (t.suspensa || t.terminada) return;  // cedeu, ou acabou de dentro
  // A funcao de entrada VOLTOU sem passar por Suspend/Exit: a thread acabou,
  // e o rv e o r0 do retorno (zeebx `resume_thread`: "Voltar sem ter passado
  // por Suspend significa que a funcao de entrada retornou").
  TerminarThread(cpu, traco, th, cpu.Get(kR0));
}

bool EnfileirarThreadPeloCallbackDeRetomada(std::uint32_t pcb) {
  for (auto& par : g_threads) {
    if (par.second.retomada_cbk != pcb) continue;
    if (!par.second.iniciada || par.second.terminada) return true;
    if (std::find(g_pendentes.begin(), g_pendentes.end(), par.first) ==
        g_pendentes.end()) {
      g_pendentes.push_back(par.first);
    }
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------

bool SlotDaClasseImplementado(std::uint32_t k, std::uint32_t slot) {
  // Os METODOS IMPLEMENTADOS, e a razao e uma medicao:
  // o `tectoy` chama `IAppHistory::Top` (slot 5) no `EVT_APP_START`, e o
  // `Release` (slot 1) ja e servido pela IBase de todos os objectos.
  //
  // O `Top` devolve `AEE_SUCCESS` porque a CONTA do cabecalho fecha:
  // `AEEIAppHistory.h` diz que ha uma entrada de historia por aplicacao
  // top-visible ("Rules on creating history entry for an app"), e esta maquina
  // tem UMA applet, criada e posta a correr por nos -- logo a lista nao esta
  // vazia, que e a unica condicao em que o `Top` devolve `AEE_EFAILED`.
  //
  // O `Back` (slot 4) devolve `AEE_ENOSUCH` (39): com UMA entrada nao ha
  // anterior, e o helper `IAppHistory_Bottom` do proprio cabecalho faz
  // `while (Back() == SUCCESS)` esperando ENOSUCH como fim-de-lista.
  if (k == kClasseDoAppHistory) {
    return slot == brew_slots::kAppHistory_Top || slot == brew_slots::kAppHistory_Back;
  }
  if (k == static_cast<std::uint32_t>(Classe::kThread)) {
    // Os CINCO metodos da propria thread tem estado e acao (ver o bloco do
    // IThread mais abaixo); os quatro do pool herdado (`Malloc`, `Free`,
    // `HoldRsc`, `ReleaseRsc`) continuam a recusar -- sem heap de thread
    // alcancavel deste ficheiro, servidos como recusa com nome.
    return slot == brew_slots::kThread_Start || slot == brew_slots::kThread_Exit ||
           slot == brew_slots::kThread_Join || slot == brew_slots::kThread_Suspend ||
           slot == brew_slots::kThread_GetResumeCBK;
  }
  if (k == static_cast<std::uint32_t>(Classe::kTextCtl)) {
    // O pacote de estado do `zenonia` (6 pedidos na bateria): guarda estado,
    // sem desenho. HandleEvent devolve FALSE (nao tratado); as void nao tocam r0.
    return slot == brew_slots::kTextCtl_HandleEvent ||
           slot == brew_slots::kTextCtl_SetActive || slot == brew_slots::kTextCtl_IsActive ||
           slot == brew_slots::kTextCtl_SetRect || slot == brew_slots::kTextCtl_SetProperties ||
           slot == brew_slots::kTextCtl_SetInputMode;
  }
  return false;
}

void ConstruirClasses(Memoria& mem, const Saidas& saidas, Traco& traco) {
  ReporEstadoTextCtl();
  // O ESTADO DAS THREADS TAMBEM E POR CORRIDA. Sem isto, a segunda Bancada de
  // um teste (ou o segundo titulo da bateria) via a thread da primeira: o
  // `Start` respondia EALREADY a quem nao tinha iniciado nada.
  ReporEstadoThreads();
  ConstruirIgles(mem, saidas, traco);
  for (std::uint32_t k = 0; k < kQuantasClasses; ++k) {
    const std::uint32_t quantos = kSlotsDaInterface[k];
    if (quantos == 0 || quantos > kSlotsDaClasse) {
      // Nao se recusa em silencio: a classe fica SEM objecto e o motivo vai para
      // o traco. Nunca acontece com os cabecalhos de hoje (28 e o maior), mas um
      // `static_assert` nao pode ler cabecalhos gerados noutra maquina.
      traco.RegistarFalta(Area::Brew, "classes_da_brewm_sem_slots",
                          std::string(NomeDaClasse(k)) + " diz " + std::to_string(quantos) +
                              " slots; o maximo desta faixa e 32");
      continue;
    }
    // UM ENDERECO DE SAIDA POR SLOT, e nao um stub para todos: e o que faz o
    // despacho saber QUAL metodo foi pedido (a mesma razao da faixa dos ajudantes).
    //
    // A VTABLE E CABLEADA ATE AO FIM DA FAIXA (`kSlotsDaClasse`), e nao so ate ao
    // numero de slots da interface. Os quatro que sobram (o `ITextCtl` tem 28)
    // ficariam a ZERO, e um `bx` para um deles saltaria para memoria que nao
    // existe -- sem sintoma ate acontecer. Cableados, um pedido fora da tabela
    // chega ao despacho e recebe uma recusa COM NOME (`ITextCtl::slot28`).
    ConstruirObjeto(mem, saidas, ObjetoDaClasse(k), saidas.Endereco(VtClasse(k)),
                    kSlotsDaClasse, VtClasse(k));
  }

  // A LEITURA DE VOLTA. Uma cablagem ja se perdeu numa edicao de texto neste
  // trabalho e o sintoma foi um pedido que parecia nao implementado; aqui o
  // defeito diz-se no arranque, e nao quatro minutos depois.
  for (std::uint32_t k = 0; k < kQuantasClasses; ++k) {
    const std::uint32_t vt = saidas.Endereco(VtClasse(k));
    if (mem.Ler32(ObjetoDaClasse(k)) != vt) {
      traco.RegistarFalta(Area::Brew, "classes_da_brewm_cablagem_perdida",
                          std::string(NomeDaClasse(k)) + ": o objecto nao aponta para a vtable");
      continue;
    }
    for (std::uint32_t s = 2; s < kSlotsDaClasse; ++s) {
      if (mem.Ler32(vt + s * 4) != saidas.Endereco(VtClasse(k) + s)) {
        char det[96];
        std::snprintf(det, sizeof(det), "%s slot %u", NomeDaClasse(k), s);
        traco.RegistarFalta(Area::Brew, "classes_da_brewm_cablagem_perdida", det);
      }
    }
  }

  // AS SETE EXTENSOES QUALCOMM (frente qualcomm): CINCO objectos para SETE
  // IIDs (o SurfaceManip e o ImageonExt atendem a V1 e a V2 com o MESMO
  // objecto -- a V2 herda a V1 com o mesmo prefixo de vtable, zeebx). Cada
  // objecto tem a vtable do SDK CABLEADA ate ao fim da tabela real, e nao um
  // slot a mais: a primeira chamada de um titulo nao pode cair num slot que nao
  // existe (o aviso do zeebx). O mesmo `ConstruirObjeto` das classes: objecto
  // +0 = vtable, +4 = contagem, slots 0/1 = AddRef/Release do despacho.
  struct VTableQualcomm {
    std::uint32_t objeto, vt, slots;
  };
  const VTableQualcomm qualcomm[] = {
      {kObjetoEglGetColorBuffer, kVtableEglGetColorBuffer, kEglGetColorBufferSlots},
      {kObjetoEglSurfaceManip, kVtableEglSurfaceManip, kEglSurfaceManipSlots},
      {kObjetoGlesImageonExt, kVtableGlesImageonExt, kGlesImageonExtSlots},
      {kObjetoGles10Ext, kVtableGles10Ext, kGles10ExtSlots},
      {kObjetoGles11ExtPak, kVtableGles11ExtPak, kGles11ExtPakSlots},
      {kObjetoEglOesSwapInterval, kVtableEglOesSwapInterval, kEglOesSwapIntervalSlots},
      {kObjetoEglGetPowerLevel, kVtableEglGetPowerLevel, kEglGetPowerLevelSlots},
  };
  for (const VTableQualcomm& e : qualcomm) {
    ConstruirObjeto(mem, saidas, e.objeto, saidas.Endereco(e.vt), e.slots, e.vt);
  }
  // ESTADO POR CORRIDA do SurfaceManip: sem escala, sem rotao, sem transparencia
  // -- o estado do motor desta arvore nao tem nenhum desses caminhos, e o
  // objecto tem de nascer a dizer a verdade (e nao com o lixo da corrida
  // anterior). O bloco fica dentro do objecto (a 0x100), abaixo da proxima
  // linha (0x8F241000 + 0x1000).
  mem.Escrever32(kObjetoEglSurfaceManip + 0x100u, 0);  // escala ligada
  // A LEITURA DE VOLTA (a mesma cerimonia das classes e do IGL: uma cablagem
  // perdida numa edicao ja custou uma corrida inteira).
  for (const VTableQualcomm& e : qualcomm) {
    if (mem.Ler32(e.objeto) != saidas.Endereco(e.vt)) {
      traco.RegistarFalta(Area::Brew, "classes_da_brewm_cablagem_perdida",
                          "objecto qualcomm sem vtable");
      continue;
    }
    for (std::uint32_t s = 2; s < e.slots; ++s) {
      if (mem.Ler32(saidas.Endereco(e.vt) + s * 4) != saidas.Endereco(e.vt + s)) {
        char det[96];
        std::snprintf(det, sizeof(det), "qualcomm slot %u", s);
        traco.RegistarFalta(Area::Brew, "classes_da_brewm_cablagem_perdida", det);
      }
    }
  }
}

const char* NomeDoSlotIgles(std::uint32_t slot) {
  // A TABELA E GERADA (`tools/igles_slots.inc`). Escrever os nomes a mao aqui
  // era exactamente o erro do mapa do IGLES11 da arvore antiga: uma ordem
  // copiada de outro emulador. `kIglesSlots` e `igles_slots::kQuantos` sao duas
  // fontes e o `static_assert` obriga-as a concordar.
  static_assert(kIglesSlots == igles_slots::kQuantos,
                "kIglesSlots (classes.h) diverge da conta dos cabecalhos do SDK");
  if (slot >= igles_slots::kQuantos) return "?";
  return igles_slots::kNomes[slot];
}

const char* NomeDoSlotIglesExt(std::uint32_t slot) {
  static_assert(kIglesExtSlots == igles_ext_slots::kQuantos,
                "kIglesExtSlots (classes.h) diverge de AEEGLES11Ext.h");
  if (slot >= igles_ext_slots::kQuantos) return "?";
  return igles_ext_slots::kNomes[slot];
}

// --- OS NOMES DOS SLOTS DAS SETE EXTENSOES QUALCOMM --------------------------
//
// Escritos aqui COM o ficheiro e a linha de cada metodo, porque estes sete
// cabecalhos nao estao na lista do `gerar_slots.py` (que so le os que o
// Toolset gera). A ordem e a ordem dos membros do `INHERIT_*` de cada um -- a
// MESMA regra que o gerador aplica aos que ele le. O teste fixa as ancoras
// (`tests/classes_test.cpp`, `AsExtensoesQualcommRecusamComONomeOQueNaoServem`):
// se esta tabela mudar de ordem, a recusa passa a ter o nome errado.
const char* NomeDoSlotEglGetColorBuffer(std::uint32_t slot) {
  static const char* const nomes[] = {
      "AddRef", "Release", "QueryInterface",  // INHERIT_IQueryInterface
      "GetColorBuffer",                       // AEEEGLGetColorBuffer.h:25
  };
  return slot < 4 ? nomes[slot] : "?";
}

const char* NomeDoSlotEglSurfaceManip(std::uint32_t slot) {
  static const char* const nomes[] = {
      "AddRef", "Release", "QueryInterface",      // INHERIT_IQueryInterface
      "SurfaceScaleEnable",                       // AEEEGLSurfaceManip.h:27
      "SetSurfaceScale",                          // :28
      "GetSurfaceScale",                          // :29
      "GetSurfaceScaleCaps",                      // :30
      "SurfaceRotateEnable",                      // :31
      "SetSurfaceRotate",                         // :32
      "GetSurfaceRotate",                         // :33
      "GetSurfaceRotateCaps",                     // :34
      "SurfaceTransparencyEnable",                // :35
      "SetSurfaceTransparency",                   // :36
      "GetSurfaceTransparency",                   // :37
      "SetSurfaceTransparencyMap",                // :38
      "GetSurfaceTransparencyMap",                // :39
      "GetSurfaceTransparencyCaps",               // :40
      "SurfaceColorKeyEnable",                    // :256 (INHERIT_IEGLSurfaceManip)
      "SetSurfaceColorKey",                       // :257
      "GetSurfaceColorKey",                       // :258
      "CreateCompositeSurface",                   // :259
      "SurfaceOverlayEnable",                     // :260
      "SurfaceOverlayLayerEnable",                // :261
      "SurfaceOverlayBind",                       // :262
      "GetSurfaceOverlayBinding",                 // :263
      "GetSurfaceOverlay",                        // :264
      "GetSurfaceOverlayCaps",                    // :265
  };
  return slot < 27 ? nomes[slot] : "?";
}

const char* NomeDoSlotGlesImageonExt(std::uint32_t slot) {
  static const char* const nomes[] = {
      "AddRef", "Release", "QueryInterface",   // INHERIT_IQueryInterface
      "PointSizePointerOES",                   // AEEGLESImageonEXT.h:27
      "BlendEquationSeparateEXT",              // :28
      "BlendFuncSeparateEXT",                  // :29
      "BlendEquationEXT",                      // :30
      "BindBufferQUALCOMM",                    // :31
      "DeleteBuffersQUALCOMM",                 // :32
      "GenBuffersQUALCOMM",                    // :33
      "BufferDataQUALCOMM",                    // :34
      "BufferSubDataQUALCOMM",                 // :35
      "IsBufferQUALCOMM",                      // :36
      "BufferDataATI",                         // :37
      "MeshListATI",                           // :38
      "DrawVertexBufferObjectATI",             // :39
      "GetPointerv",                           // :40
      "TexEnvi",                               // :41
      "TexEnviv",                              // :42
      "TexParameteri",                         // :43
      "TexParameteriv",                        // :44
      "TexParameterfv",                        // :45
      "TexParameterxv",                        // :46
      "GetMaterialfv",                         // :208 (INHERIT_IGLESImageonExt)
      "GetTexParameteriv",                     // :209
      "GetTexParameterfv",                     // :210
      "GetTexParameterxv",                     // :211
  };
  return slot < 27 ? nomes[slot] : "?";
}

const char* NomeDoSlotGles10Ext(std::uint32_t slot) {
  static const char* const nomes[] = {
      "AddRef", "Release", "QueryInterface",  // INHERIT_IQueryInterface
      "QueryMatrixxOES",                      // AEEGLES10Ext.h:27
  };
  return slot < 4 ? nomes[slot] : "?";
}

const char* NomeDoSlotGles11ExtPak(std::uint32_t slot) {
  static const char* const nomes[] = {
      "AddRef", "Release", "QueryInterface",   // INHERIT_IQueryInterface
      "GetTexGenfv",                           // AEEGLES11ExtPak.h:26
      "GetTexGeniv",                           // :27
      "GetTexGenxv",                           // :28
      "TexGenf",                               // :29
      "TexGeni",                               // :30
      "TexGenx",                               // :31
      "TexGenfv",                              // :32
      "TexGeniv",                              // :33
      "TexGenxv",                              // :34
      "BlendEquation",                         // :35
      "BlendFuncSeparate",                     // :36
      "BlendEquationSeparate",                 // :37
      "BindFramebufferOES",                    // :38
      "BindRenderbufferOES",                   // :39
      "CheckFramebufferStatusOES",             // :40
      "DeleteFramebuffersOES",                 // :41
      "DeleteRenderbuffersOES",                // :42
      "FramebufferRenderbufferOES",            // :43
      "FramebufferTexture2DOES",               // :44
      "GenerateMipmapOES",                     // :45
      "GenFramebuffersOES",                    // :46
      "GenRenderbuffersOES",                   // :47
      "GetFramebufferAttachmentParameterivOES", // :48
      "GetRenderbufferParameterivOES",         // :49
      "IsFramebufferOES",                      // :50
      "IsRenderbufferOES",                     // :51
      "RenderbufferStorageOES",                // :52
  };
  return slot < 30 ? nomes[slot] : "?";
}

const char* NomeDoSlotEglOesSwapInterval(std::uint32_t slot) {
  static const char* const nomes[] = {
      "AddRef", "Release", "QueryInterface",  // INHERIT_IQueryInterface
      "SwapInterval",                         // AEEEGLOESSwapInterval.h:26
      "GetSwapInterval",                      // :27
  };
  return slot < 5 ? nomes[slot] : "?";
}

const char* NomeDoSlotEglGetPowerLevel(std::uint32_t slot) {
  static const char* const nomes[] = {
      "AddRef", "Release", "QueryInterface",  // INHERIT_IQueryInterface
      "GetPowerLevel",                        // AEEEGLGetPowerLevel.h:24
  };
  return slot < 4 ? nomes[slot] : "?";
}

// --- O IGLES11 E O MOTOR DO IGL (frente glbloco) ------------------------------
namespace {

// POR QUE NAO HÁ AQUI UM SEGUNDO ESTADO COPIADO: o `core/brew/igl.cpp` ja
// implementa estes metodos (`kIgl_*`, os 80 slots do IGL) com o estado exacto
// que o rasterizador consome no desenho (`MontarEstado` ->
// `EstadoDeRasterizacao`). Uma copia deste estado no rasterizador ou neste
// ficheiro criaria DUAS verdades paralelas -- e a armadilha 2 desta casa sao
// exactamente as segundas copias a divergir em silencio (o `tools/bateria.cpp`
// tinha segundas copias de `kSlotId`, e mudar so um lado dava regressoes
// falsas). O IGLES11 passa a ter o SEU proprio motor do mesmo tipo, e o
// `AtenderClasse` desloca os argumentos pela moldura dele.
std::unique_ptr<Igl> g_igles_igl;

constexpr std::uint32_t kSemSlotNoIgl = 0xFFFFFFFFu;

// O numero do slot no IGL de 80 slots para cada slot IGLES11 desta frente. A
// correspondencia e POR NOME DO METODO, e nao por numero: as duas interfaces
// numeram os mesmos metodos em posicoes diferentes (o `kIgles_Enable` e o 56,
// o `kIgl_Enable` e o 28). Os nomes vem dos geradores
// (`tools/igles_slots.inc` / `tools/gl_slots.inc`), as DUAS leituras dos
// cabecalhos do SDK -- e nao de uma copia de outro emulador.
std::uint32_t SlotIglesNoIgl(std::uint32_t slot) {
  switch (slot) {
    case igles_slots::kIgles_Clear: return gl_slots::kIgl_Clear;
    case igles_slots::kIgles_ClearColorx: return gl_slots::kIgl_ClearColorx;
    case igles_slots::kIgles_CullFace: return gl_slots::kIgl_CullFace;
    case igles_slots::kIgles_Disable: return gl_slots::kIgl_Disable;
    case igles_slots::kIgles_DisableClientState: return gl_slots::kIgl_DisableClientState;
    case igles_slots::kIgles_Enable: return gl_slots::kIgl_Enable;
    case igles_slots::kIgles_EnableClientState: return gl_slots::kIgl_EnableClientState;
    case igles_slots::kIgles_Hint: return gl_slots::kIgl_Hint;
    case igles_slots::kIgles_LoadIdentity: return gl_slots::kIgl_LoadIdentity;
    case igles_slots::kIgles_MatrixMode: return gl_slots::kIgl_MatrixMode;
    case igles_slots::kIgles_ShadeModel: return gl_slots::kIgl_ShadeModel;
    case igles_slots::kIgles_TexParameterx: return gl_slots::kIgl_TexParameterx;
    case igles_slots::kIgles_Viewport: return gl_slots::kIgl_Viewport;
    default: return kSemSlotNoIgl;
  }
}

// So DOIS dos doze tem QUATRO argumentos reais, com o quarto NA PILHA por
// causa do `pMe` em r0: o `Viewport` (x, y, largura, altura) e o `ClearColorx`
// (r, g, b, a). Nos outros dez o r3 e o terceiro argumento real e nao se le a
// pilha.
bool SlotIglesTemQuartoNaPilha(std::uint32_t slot) {
  return slot == igles_slots::kIgles_Viewport || slot == igles_slots::kIgles_ClearColorx;
}

}  // namespace

const Igl* EstadoDoIgles11() { return g_igles_igl.get(); }

void ConstruirIgles(Memoria& mem, const Saidas& saidas, Traco& traco) {
  // O MOTOR DO IGLES11, RECONSTRUIDO POR CORRIDA: o mesmo `Igl` do despacho,
  // construido de novo a cada `ConstruirClasses` (uma vez por titulo na
  // bateria), para o estado nao vazar de um titulo para o outro.
  g_igles_igl = std::make_unique<Igl>(mem, traco);
  ConstruirObjeto(mem, saidas, kObjetoIgles, saidas.Endereco(kVtableIgles),
                  kIglesSlots, kVtableIgles);
  ConstruirObjeto(mem, saidas, kObjetoIglesExt, saidas.Endereco(kVtableIglesExt),
                  kIglesExtSlots, kVtableIglesExt);
  if (mem.Ler32(kObjetoIgles) != saidas.Endereco(kVtableIgles)) {
    traco.RegistarFalta(Area::Brew, "igles_cablagem_perdida",
                        "o objecto nao aponta para a vtable");
    return;
  }
  for (std::uint32_t s = 2; s < kIglesSlots; ++s) {
    if (mem.Ler32(saidas.Endereco(kVtableIgles) + s * 4) != saidas.Endereco(kVtableIgles + s)) {
      char det[96];
      std::snprintf(det, sizeof(det), "IGLES11 slot %u", s);
      traco.RegistarFalta(Area::Brew, "igles_cablagem_perdida", det);
    }
  }
  // A MESMA LEITURA DE VOLTA PARA O `IGLES11Ext`. Uma cablagem que nao se
  // confirma perde-se em silencio -- ja aconteceu neste trabalho.
  if (mem.Ler32(kObjetoIglesExt) != saidas.Endereco(kVtableIglesExt)) {
    traco.RegistarFalta(Area::Brew, "igles_ext_cablagem_perdida",
                        "o objecto nao aponta para a vtable");
    return;
  }
  for (std::uint32_t s = 2; s < kIglesExtSlots; ++s) {
    if (mem.Ler32(saidas.Endereco(kVtableIglesExt) + s * 4) !=
        saidas.Endereco(kVtableIglesExt + s)) {
      char det[96];
      std::snprintf(det, sizeof(det), "IGLES11Ext slot %u", s);
      traco.RegistarFalta(Area::Brew, "igles_ext_cablagem_perdida", det);
    }
  }
}

// AS STRINGS DO `glGetString`, escritas na memoria do guest quando pedidas.
//
// A ASSINATURA E DO CABECALHO, e nao de ouvido (`AEEGLES10.h`, slot 67):
//     int (*GetString)(iname *pMe, AEEGLenum name, AEEGLubyte const **ret)
// -- devolve um codigo AEE e escreve o PONTEIRO em `*ret`. O thunk `glGetString`
// do wrapper devolve esse ponteiro ao titulo.
//
// MEDIDO na corrida de referencia (`/tmp/corrida_base.json`, 62 titulos): os DEZ
// titulos da familia `emulator_neo` chamam este slot UMA vez, sempre com
//     r1=0x00001f03 (GL_EXTENSIONS)  r2=0x8007ff94 (um endereco na pilha)
// e nenhum deles pede GL_VENDOR, GL_RENDERER ou GL_VERSION. A recusa deste slot
// e o ultimo pedido de cada um antes de desistirem.
std::uint32_t EscreverStringIgles(Memoria& mem, std::uint32_t indice, const char* texto) {
  const std::uint32_t p = kZonaDeStringsIgles + indice * kPassoDeStringIgles;
  std::uint32_t k = 0;
  for (; texto[k] != 0 && k + 1 < kPassoDeStringIgles; ++k) {
    mem.Escrever8(p + k, static_cast<std::uint8_t>(texto[k]));
  }
  mem.Escrever8(p + k, 0);
  return p;
}

// A LISTA DE EXTENSOES ANUNCIADA. **SO O QUE ESTE FICHEIRO SERVE.**
//
// A regra, medida (relatorio 12-gl.md, parte C.4): anunciar uma extensao e
// PROMETER servi-la. Anunciar sem servir da **10 regressoes** -- os dez titulos
// `emulator_neo` passam o `InitGLExtensions`, saltam para o `glDrawTexivOES`
// prometido e morrem com zero pixels. O anuncio so pode entrar no MESMO commit
// em que o `glDrawTexivOES` (e as outras sete variantes) passa a ser servido no
// ramo `IGLES11Ext` deste ficheiro -- e foi o que este commit fez.
//
// SO `GL_OES_draw_texture`: e o unico nome cuja funcao existe aqui. Os nomes do
// zeebx (`GL_ATI_imageon_misc`, `GL_ATI_texture_compression_atitc`,
// `GL_ARB_vertex_buffer_object`) NAO entram: nao ha descodificador ATITC nem
// buffer de vertices nesta arvore, e o titulo tolera a ausencia dos outros
// (medido: tolera sete interfaces de extensao inteiras).
constexpr const char* kExtensoesIgles = "GL_OES_draw_texture ";

// Os indices da zona de strings. Um endereco fixo por consulta, para duas
// consultas seguidas nao se pisarem.
constexpr std::uint32_t kStrIglesVendor = 0;
constexpr std::uint32_t kStrIglesRenderer = 1;
constexpr std::uint32_t kStrIglesVersion = 2;
constexpr std::uint32_t kStrIglesExtensions = 3;
constexpr std::uint32_t kStrIglesDesconhecida = 4;

// --- O ESTADO DE GL DO IGLES11 QUE A EXTENSAO PRECISA ----------------------
//
// O `AtenderClasse` nao tem um objecto-hoardeiro: o estado das classes vive nos
// OBJECTOS do guest, e o do IGLES11 igual. Um bloco proprio na faixa
// `0x8F000000` -- a que a bateria inteira ja provou que o corpus nao toca (o
// comentario do `0x800C0000` em `classes.h`).
//
// Campos, todos u32:
//   +0  textura_ligada (o GLuint do `glBindTexture`; 0 = nenhuma)
//   +4  largura da imagem   +8  altura   +12 formato_do_pixel (o 6.o arg do
//       `glTexImage2D`)   +16 tipo (o 8.o)   +20 ponteiro (o 9.o: os texels no
//       espaco do guest)   +44 contador do `glGenTextures` (os ids so precisam
//       de ser unicos e distintos de zero)
//
// NAO HA AQUI RECORTE (`GL_TEXTURE_CROP_RECT_OES`): o pedaco da textura que o
// `glDrawTex*OES` desenha. Nenhum titulo do corpus o pede (as strings dos dez
// `.mod` nao o nomeiam), e o default da extensao e a textura INTEIRA. O desenho
// ja esta pronto a respeita-lo quando um titulo o usar.
constexpr std::uint32_t kEstadoIgles = 0x8F030000u;
constexpr std::uint32_t kIglesTexLigada = kEstadoIgles + 0u;
constexpr std::uint32_t kIglesTexLargura = kEstadoIgles + 4u;
constexpr std::uint32_t kIglesTexAltura = kEstadoIgles + 8u;
constexpr std::uint32_t kIglesTexFormato = kEstadoIgles + 12u;
constexpr std::uint32_t kIglesTexTipo = kEstadoIgles + 16u;
constexpr std::uint32_t kIglesTexPonteiro = kEstadoIgles + 20u;
constexpr std::uint32_t kIglesContador = kEstadoIgles + 44u;

// O `glDrawTex*OES` -- o blit de ecra do `GL_OES_draw_texture`.
//
// Coordenadas de JANELA (o zero de y fica EMBAIXO, como no OpenGL), sem
// passar pelas matrizes: e o caminho que um emulador usa para pôr a tela dele
// na tela do aparelho, e e o que os dez portes de arcade do console fazem.
// Largura ou altura negativas espelham o eixo (zeebx, `rasterizer.rs`,
// `draw_texture`).
//
// Os pixels vao para o BUFFER DO ECRA NO GUEST (`kBaseDoEcraNoGuest`, RGB565):
// o mesmo sitio onde o titulo escreveria; a sincronizacao do `IDisplay::Update`
// (e o absorver final da bateria) conta-os como desenho. Sem a Tela pelo meio
// nao ha rasterizador: e um blit, como a extensao manda. A amostragem e a do
// rasterizador desta arvore (GL_NEAREST, clamp; RGBA/RGB/LUMINANCE x
// GL_UNSIGNED_BYTE) para uma textura desenhada aqui nao mentir sobre o que
// desenhou.
bool DesenharRectTexturaIgles(Memoria& mem, float x, float y, float /*z*/, float w, float h,
                              std::string* motivo) {
  const std::uint32_t tex = mem.Ler32(kIglesTexLigada);
  if (tex == 0) {
    *motivo = "sem textura ligada (glBindTexture nao servido)";
    return false;
  }
  const std::uint32_t largura = mem.Ler32(kIglesTexLargura);
  const std::uint32_t altura = mem.Ler32(kIglesTexAltura);
  const std::uint32_t formato = mem.Ler32(kIglesTexFormato);
  const std::uint32_t tipo = mem.Ler32(kIglesTexTipo);
  const std::uint32_t ponteiro = mem.Ler32(kIglesTexPonteiro);
  if (largura == 0 || altura == 0 || ponteiro == 0) {
    *motivo = "textura sem imagem (glTexImage2D nao servido)";
    return false;
  }
  if (w == 0.0f || h == 0.0f) {
    *motivo = "rect de largura ou altura zero";
    return false;
  }
  const bool rgba = (formato == gl_slots::GL_RGBA && tipo == gl_slots::GL_UNSIGNED_BYTE);
  const bool rgb = (formato == gl_slots::GL_RGB && tipo == gl_slots::GL_UNSIGNED_BYTE);
  const bool lum = (formato == gl_slots::GL_LUMINANCE && tipo == gl_slots::GL_UNSIGNED_BYTE);
  if (!rgba && !rgb && !lum) {
    char d[96];
    std::snprintf(d, sizeof(d),
                  "textura com formato 0x%04x e tipo 0x%04x sem caminho de amostragem",
                  formato, tipo);
    *motivo = d;
    return false;
  }

  // JANELA -> ECRA. A janela do GL tem o zero embaixo; o buffer do guest cresce
  // para BAIXO. O rect (x, y) e o canto INFERIOR esquerdo; largura e altura
  // levam SINAL, e o sinal e o que espelha o eixo: o intervalo fica entre os
  // dois e a coordenada de textura anda com o canto (como no zeebx).
  const int wl = zb2::brew::kLarguraDoEcra;
  const int hl = zb2::brew::kAlturaDoEcra;
  const float esquerda = x, direita = x + w;
  const float topo = static_cast<float>(hl) - y, fundo = static_cast<float>(hl) - (y + h);
  const int x0 = std::max(0, static_cast<int>(std::floor(std::min(esquerda, direita))));
  const int x1 = std::min(wl, static_cast<int>(std::ceil(std::max(esquerda, direita))));
  const int y0 = std::max(0, static_cast<int>(std::floor(std::min(topo, fundo))));
  const int y1 = std::min(hl, static_cast<int>(std::ceil(std::max(topo, fundo))));
  if (x1 <= x0 || y1 <= y0) {
    *motivo = "rect fora do ecra";
    return false;
  }

  const float inv_l = (direita != esquerda) ? 1.0f / (direita - esquerda) : 0.0f;
  const float inv_t = (topo != fundo) ? 1.0f / (topo - fundo) : 0.0f;
  const int tecl = static_cast<int>(largura) - 1;
  const int teca = static_cast<int>(altura) - 1;
  for (int linha = y0; linha < y1; ++linha) {
    // v=0 na linha de BAIXO da imagem (a convencao do OpenGL e a do
    // rasterizador desta arvore): o canto inferior do rect mostra v0, que e a
    // PRIMEIRA linha dos dados do `glTexImage2D`.
    float v = (topo - static_cast<float>(linha)) * inv_t;
    v = std::min(1.0f, std::max(0.0f, v));
    const int ty = std::min(teca, static_cast<int>(std::floor(v * static_cast<float>(altura))));
    for (int col = x0; col < x1; ++col) {
      const float s = (static_cast<float>(col) - esquerda) * inv_l;
      const float u = std::min(1.0f, std::max(0.0f, s));
      const int tx = std::min(tecl, static_cast<int>(std::floor(u * static_cast<float>(largura))));
      const std::uint32_t origem = ponteiro + static_cast<std::uint32_t>(ty * static_cast<int>(largura) + tx) * (rgba ? 4u : (rgb ? 3u : 1u));
      std::uint8_t r, g, b;
      if (rgba) {
        r = mem.Ler8(origem + 0u);
        g = mem.Ler8(origem + 1u);
        b = mem.Ler8(origem + 2u);
      } else if (rgb) {
        r = mem.Ler8(origem + 0u);
        g = mem.Ler8(origem + 1u);
        b = mem.Ler8(origem + 2u);
      } else {
        r = g = b = mem.Ler8(origem);
      }
      const std::uint16_t rgb565 = static_cast<std::uint16_t>(((r >> 3) << 11) |
                                                              ((g >> 2) << 5) | (b >> 3));
      mem.Escrever16(zb2::brew::kBaseDoEcraNoGuest +
                         static_cast<std::uint32_t>(linha * wl + col) * 2u,
                     rgb565);
    }
  }
  return true;
}

namespace {

// A CABECA COMUM das sete extensoes qualcomm: AddRef/Release/QueryInterface.
// A contagem vive em `objeto + 4`, como em todas as interfaces desta casa
// (o `ConstruirObjeto` poe-la a 1). `iid1`/`iid2` sao os IIDs que o objecto se
// serve a si proprio (0 = nao ha segundo); o SurfaceManip e o ImageonExt
// atendem o par V1/V2. Devolve true se o slot foi atendido; false para o
// chamador seguir para os metodos proprios da interface.
bool AtenderCabecaQualcomm(ICpu& cpu, Traco& traco, std::uint32_t objeto,
                           std::uint32_t slot, const char* interface,
                           std::uint32_t iid1, std::uint32_t iid2) {
  Memoria& mem = cpu.Mem();
  if (slot == 0) {
    const std::uint32_t n = mem.Ler32(objeto + 4) + 1;
    mem.Escrever32(objeto + 4, n);
    cpu.Set(kR0, n);
    return true;
  }
  if (slot == 1) {
    const std::uint32_t n = mem.Ler32(objeto + 4);
    if (n == 0) {
      traco.RegistarFalta(Area::Brew, std::string(interface) + "::Release",
                          "Release de um objecto com contagem zero");
      cpu.Set(kR0, kAeeUnsupported);
      return true;
    }
    mem.Escrever32(objeto + 4, n - 1);
    cpu.Set(kR0, n - 1);
    return true;
  }
  if (slot == 2) {
    const std::uint32_t iid = cpu.Get(kR1), ppo = cpu.Get(kR2);
    if (ppo == 0) {
      traco.RegistarFalta(Area::Brew, std::string(interface) + "::QueryInterface",
                          "ppObj nulo");
      cpu.Set(kR0, kAeeBadParm);
      return true;
    }
    if (iid == iid1 || (iid2 != 0 && iid == iid2)) {
      mem.Escrever32(ppo, objeto);
      cpu.Set(kR0, kAeeSuccess);
    } else {
      mem.Escrever32(ppo, 0);
      char det[96];
      std::snprintf(det, sizeof(det), "iid=0x%08x sem objecto nesta interface", iid);
      traco.RegistarFalta(Area::Brew, std::string(interface) + "::QueryInterface", det);
      cpu.Set(kR0, kAeeUnsupported);
    }
    return true;
  }
  return false;
}

}  // namespace

bool AtenderClasse(ICpu& cpu, std::uint32_t indice, Traco& traco) {
  // O IGLES11Ext, faixa propria (15 slots, AEEGLES11Ext.h). A CABECA e os
  // OITO `DrawTex*OES` sao o que o `GL_OES_draw_texture` promete, e sao
  // SERVIDOS; os quatro de palette/weight (3..6) recusam COM NOME, porque nao
  // ha caminho para eles -- o titulo tolera a ausencia (medido, 12-gl.md C.1).
  if (indice >= kVtableIglesExt && indice < kVtableIglesExt + kIglesExtSlots) {
    const std::uint32_t slot = indice - kVtableIglesExt;
    char nome[64], det[192];
    Memoria& mem = cpu.Mem();

    // A CABECA. O wrapper do titulo (`GLES_ext.c`) chama `IGLES11EXT_Release`
    // no fim (`ReleaseNBI`) e `IGLES11EXT_QueryInterface` para se servir a si
    // proprio; a contagem vive no objecto, como em todas as interfaces.
    if (slot == igles_ext_slots::kIglesExt_AddRef) {
      const std::uint32_t n = mem.Ler32(kObjetoIglesExt + 4) + 1;
      mem.Escrever32(kObjetoIglesExt + 4, n);
      cpu.Set(kR0, n);
      return true;
    }
    if (slot == igles_ext_slots::kIglesExt_Release) {
      const std::uint32_t n = mem.Ler32(kObjetoIglesExt + 4);
      if (n == 0) {
        traco.RegistarFalta(Area::Brew, "IGLES11Ext::Release",
                            "Release de um objecto com contagem zero");
        cpu.Set(kR0, kAeeUnsupported);
        return true;
      }
      mem.Escrever32(kObjetoIglesExt + 4, n - 1);
      cpu.Set(kR0, n - 1);
      return true;
    }
    if (slot == igles_ext_slots::kIglesExt_QueryInterface) {
      const std::uint32_t iid = cpu.Get(kR1), ppo = cpu.Get(kR2);
      if (ppo == 0) {
        traco.RegistarFalta(Area::Brew, "IGLES11Ext::QueryInterface", "ppObj nulo");
        cpu.Set(kR0, kAeeBadParm);
        return true;
      }
      if (iid == kIidGles11Ext) {
        mem.Escrever32(ppo, kObjetoIglesExt);
        cpu.Set(kR0, kAeeSuccess);
      } else {
        mem.Escrever32(ppo, 0);
        cpu.Set(kR0, kAeeUnsupported);
      }
      return true;
    }

    // O `glDrawTex*OES` (slots 7..14): as OITO variantes desenham o mesmo rect,
    // so muda como os cinco numeros chegam. A decodificacao e a do zeemu
    // (`BrewEGL.cpp`, `handle_draw_tex_oes`), com o pMe no r0.
    if (slot >= igles_ext_slots::kIglesExt_DrawTexsOES &&
        slot <= igles_ext_slots::kIglesExt_DrawTexfvOES) {
      const bool s = (slot == igles_ext_slots::kIglesExt_DrawTexsOES ||
                      slot == igles_ext_slots::kIglesExt_DrawTexsvOES);
      const bool i = (slot == igles_ext_slots::kIglesExt_DrawTexiOES ||
                      slot == igles_ext_slots::kIglesExt_DrawTexivOES);
      const bool x = (slot == igles_ext_slots::kIglesExt_DrawTexxOES ||
                      slot == igles_ext_slots::kIglesExt_DrawTexxvOES);
      const bool vetorial = (slot == igles_ext_slots::kIglesExt_DrawTexsvOES ||
                             slot == igles_ext_slots::kIglesExt_DrawTexivOES ||
                             slot == igles_ext_slots::kIglesExt_DrawTexxvOES ||
                             slot == igles_ext_slots::kIglesExt_DrawTexfvOES);
      auto num = [&](std::uint32_t cruda) -> float {
        if (s) return static_cast<float>(static_cast<std::int16_t>(cruda & 0xFFFFu));
        if (i) return static_cast<float>(static_cast<std::int32_t>(cruda));
        if (x) return static_cast<float>(static_cast<std::int32_t>(cruda)) / 65536.0f;
        float f;
        std::memcpy(&f, &cruda, 4);
        return f;
      };
      auto comp = [&](std::uint32_t onde, int k) -> float {
        if (onde == 0) return 0.0f;
        if (s) return num(mem.Ler16(onde + 2u * static_cast<std::uint32_t>(k)));
        return num(mem.Ler32(onde + 4u * static_cast<std::uint32_t>(k)));
      };
      float xr, yr, zr, wr, hr;
      if (vetorial) {
        const std::uint32_t coords = cpu.Get(kR1);
        xr = comp(coords, 0); yr = comp(coords, 1); zr = comp(coords, 2);
        wr = comp(coords, 3); hr = comp(coords, 4);
      } else {
        xr = num(cpu.Get(kR1)); yr = num(cpu.Get(kR2)); zr = num(cpu.Get(kR3));
        const std::uint32_t sp = cpu.Get(kSP);
        wr = num(mem.Ler32(sp + 0u)); hr = num(mem.Ler32(sp + 4u));
      }
      std::string motivo;
      if (!DesenharRectTexturaIgles(mem, xr, yr, zr, wr, hr, &motivo)) {
        std::snprintf(nome, sizeof(nome), "IGLES11Ext::%s", NomeDoSlotIglesExt(slot));
        std::snprintf(det, sizeof(det), "%s (recto %.1f,%.1f %.1fx%.1f)", motivo.c_str(), xr, yr,
                      wr, hr);
        traco.RegistarFalta(Area::Brew, nome, det);
        cpu.Set(kR0, kAeeUnsupported);
        return true;
      }
      std::snprintf(nome, sizeof(nome), "IGLES11Ext::%s", NomeDoSlotIglesExt(slot));
      std::snprintf(det, sizeof(det), "recto (%.1f, %.1f %.1fx%.1f), textura %u", xr, yr, wr, hr,
                    mem.Ler32(kIglesTexLigada));
      traco.Emitir(Area::Brew, Nivel::Depuracao, nome, det);
      cpu.Set(kR0, kAeeSuccess);
      return true;
    }

    std::snprintf(nome, sizeof(nome), "IGLES11Ext::%s", NomeDoSlotIglesExt(slot));
    std::snprintf(det, sizeof(det), "r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x lr=0x%08x",
                  cpu.Get(kR0), cpu.Get(kR1), cpu.Get(kR2), cpu.Get(kR3), cpu.Get(kLR));
    traco.RegistarFalta(Area::Brew, nome, det);
    cpu.Set(kR0, kAeeUnsupported);
    return true;
  }
  // AS SETE EXTENSOES QUALCOMM (frente qualcomm), cada uma na sua faixa. A
  // CABECA (AddRef/Release/QueryInterface) e comum; o que cada interface SERVES
  // abaixo e so o que tem um efeito real nesta arvore -- o resto RECUSA COM O
  // NOME (P2): nunca "sucesso sem efeito".
  //
  // A contagem de slots e a dos cabecalhos do SDK (classes.h, com as linhas no
  // topo das tabelas de nomes): uma vtable maior do que a real faria a primeira
  // chamada do titulo cair num slot que nao existe (aviso do zeebx).
  // -------------------------------------------------------------------------
  // IEGLGetColorBuffer (AEEEGLGetColorBuffer.h): 4 slots.
  if (indice >= kVtableEglGetColorBuffer &&
      indice < kVtableEglGetColorBuffer + kEglGetColorBufferSlots) {
    const std::uint32_t slot = indice - kVtableEglGetColorBuffer;
    if (AtenderCabecaQualcomm(cpu, traco, kObjetoEglGetColorBuffer, slot,
                              "IEGLGetColorBuffer", kIidEglGetColorBuffer, 0)) {
      return true;
    }
    // int GetColorBuffer(void **ret) -- o UNICO metodo proprio (slot 3).
    // DEVOLVE O BUFFER DE COR DO ECRA NO ESPACO DO GUEST (0x82000000, RGB565,
    // 640x480 -- `core/brew/ecra.h`): e a superficie de desenho deste motor, e
    // e o caminho que as frentes de GL deixaram em falta. O titulo escreve
    // pixels nela e o absorver final da bateria conta-os (como no
    // `glDrawTex*OES` deste ficheiro).
    if (slot == 3) {
      const std::uint32_t ret = cpu.Get(kR1);
      if (ret == 0) {
        traco.RegistarFalta(Area::Brew, "IEGLGetColorBuffer::GetColorBuffer",
                            "ponteiro de retorno nulo");
        cpu.Set(kR0, kAeeBadParm);
        return true;
      }
      cpu.Mem().Escrever32(ret, kBaseDoEcraNoGuest);
      traco.Emitir(Area::Brew, Nivel::Depuracao, "IEGLGetColorBuffer::GetColorBuffer",
                   "buffer do ecra do guest (RGB565, 640x480)");
      cpu.Set(kR0, kAeeSuccess);
      return true;
    }
    char nome[64], det[160];
    std::snprintf(nome, sizeof(nome), "IEGLGetColorBuffer::%s",
                  NomeDoSlotEglGetColorBuffer(slot));
    std::snprintf(det, sizeof(det), "r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x lr=0x%08x",
                  cpu.Get(kR0), cpu.Get(kR1), cpu.Get(kR2), cpu.Get(kR3), cpu.Get(kLR));
    traco.RegistarFalta(Area::Brew, nome, det);
    cpu.Set(kR0, kAeeUnsupported);
    return true;
  }
  // IEGLSurfaceManip (AEEEGLSurfaceManip.h): 27 slots na V2, 17 na V1, o MESMO
  // prefixo de vtable -- um so objecto atende as duas IIDs (zeebx).
  if (indice >= kVtableEglSurfaceManip &&
      indice < kVtableEglSurfaceManip + kEglSurfaceManipSlots) {
    const std::uint32_t slot = indice - kVtableEglSurfaceManip;
    if (AtenderCabecaQualcomm(cpu, traco, kObjetoEglSurfaceManip, slot,
                              "IEGLSurfaceManip", kIidEglSurfaceManip,
                              kIidEglSurfaceManipV1)) {
      return true;
    }
    Memoria& mem = cpu.Mem();
    const std::uint32_t sp = cpu.Get(kSP);
    // O ESTADO DO MOTOR, por objecto (+0x100): 0 = escala desligada. Este motor
    // NAO tem escalador; o que se serve abaixo e a VERDADE desse estado, nunca
    // uma promessa de escalar.
    const std::uint32_t estado = kObjetoEglSurfaceManip + 0x100u;
    // As consultas que descrevem o estado real (sem escala, sem rotao, sem
    // transparencia, sem colorkey, sem overlay): SUCCESS com a resposta
    // verdadeira. E o mesmo desenho do zeebx (extension_call,
    // "as consultas que nao temos como responder de verdade: zeram a saida").
    if (slot == 5) {  // GetSurfaceScale(enabled, src, dst, ret)
      const std::uint32_t enabled = cpu.Get(kR3), src = mem.Ler32(sp + 0u),
                         dst = mem.Ler32(sp + 4u), ret = mem.Ler32(sp + 8u);
      if (enabled != 0) mem.Escrever32(enabled, 0);  // escala desligada
      for (const std::uint32_t rect : {src, dst}) {
        if (rect == 0) continue;
        mem.Escrever32(rect + 0u, 0);
        mem.Escrever32(rect + 4u, 0);
        mem.Escrever32(rect + 8u, kLarguraDoEcra);
        mem.Escrever32(rect + 12u, kAlturaDoEcra);
      }
      if (ret != 0) mem.Escrever32(ret, 1);  // EGL_TRUE
      cpu.Set(kR0, kAeeSuccess);
      return true;
    }
    if (slot == 6) {  // GetSurfaceScaleCaps(param, ret): 12 AEEEGLint (AEEEGLTypes.h)
      const std::uint32_t param = cpu.Get(kR3), ret = mem.Ler32(sp + 0u);
      if (param != 0) {
        // OS FACTORES reais deste motor: 1.0 (16.16) nos dois eixos -- nao ha
        // escalador, e dizer o contrario seria anunciar uma promessa.
        const std::uint32_t caps[12] = {
            1u << 16, 1u << 16,  // MinX/MaxXScaleFactor
            1u << 16, 1u << 16,  // MinY/MaxYScaleFactor
            1u, kLarguraDoEcra,  // Min/MaxSrcWidth
            1u, kAlturaDoEcra,   // Min/MaxSrcHeight
            1u, kLarguraDoEcra,  // Min/MaxDstWidth
            1u, kAlturaDoEcra,   // Min/MaxDstHeight
        };
        for (std::uint32_t k = 0; k < 12; ++k) mem.Escrever32(param + k * 4u, caps[k]);
      }
      if (ret != 0) mem.Escrever32(ret, 1);
      cpu.Set(kR0, kAeeSuccess);
      return true;
    }
    // Ligar uma capacidade que este motor nao tem NAO pode devolver sucesso.
    // Desligar O QUE JA ESTA DESLIGADO e o estado pedido == estado real, e e
    // esse o unico caso servido (o `requested state holds` e a verdade).
    const bool liga = cpu.Get(kR3) != 0;
    if (!liga && (slot == 3 /*SurfaceScaleEnable*/ || slot == 7 /*SurfaceRotateEnable*/ ||
                  slot == 11 /*SurfaceTransparencyEnable*/)) {
      const std::uint32_t ret = mem.Ler32(sp + 0u);
      if (ret != 0) mem.Escrever32(ret, 1);
      cpu.Set(kR0, kAeeSuccess);
      return true;
    }
    (void)estado;
    char nome[64], det[160];
    std::snprintf(nome, sizeof(nome), "IEGLSurfaceManip::%s",
                  NomeDoSlotEglSurfaceManip(slot));
    std::snprintf(det, sizeof(det), "r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x lr=0x%08x",
                  cpu.Get(kR0), cpu.Get(kR1), cpu.Get(kR2), cpu.Get(kR3), cpu.Get(kLR));
    traco.RegistarFalta(Area::Brew, nome, det);
    cpu.Set(kR0, kAeeUnsupported);
    return true;
  }
  // IGLESImageonExt (AEEGLESImageonEXT.h): 27 slots V2, 23 V1, mesmo prefixo.
  if (indice >= kVtableGlesImageonExt &&
      indice < kVtableGlesImageonExt + kGlesImageonExtSlots) {
    const std::uint32_t slot = indice - kVtableGlesImageonExt;
    if (AtenderCabecaQualcomm(cpu, traco, kObjetoGlesImageonExt, slot,
                              "IGLESImageonExt", kIidGlesImageonExt,
                              kIidGlesImageonExtV1)) {
      return true;
    }
    // NENHUM metodo proprio tem caminho nesta arvore (os parametros de textura
    // e os buffers de vertice nao tem motor; anunciar que existem faria o
    // titulo chamar funcao que nao existe). Recusa com o nome -- e e o nome que
    // faz a lista de demanda dizer o que falta, em vez de um numero.
    char nome[64], det[160];
    std::snprintf(nome, sizeof(nome), "IGLESImageonExt::%s",
                  NomeDoSlotGlesImageonExt(slot));
    std::snprintf(det, sizeof(det), "r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x lr=0x%08x",
                  cpu.Get(kR0), cpu.Get(kR1), cpu.Get(kR2), cpu.Get(kR3), cpu.Get(kLR));
    traco.RegistarFalta(Area::Brew, nome, det);
    cpu.Set(kR0, kAeeUnsupported);
    return true;
  }
  // IGLES10Ext (AEEGLES10Ext.h): 4 slots; o unico metodo, QueryMatrixxOES, nao
  // tem motor de matrizes publico aqui -- recusa com o nome.
  if (indice >= kVtableGles10Ext && indice < kVtableGles10Ext + kGles10ExtSlots) {
    const std::uint32_t slot = indice - kVtableGles10Ext;
    if (AtenderCabecaQualcomm(cpu, traco, kObjetoGles10Ext, slot, "IGLES10Ext",
                              kIidGles10Ext, 0)) {
      return true;
    }
    char nome[64], det[160];
    std::snprintf(nome, sizeof(nome), "IGLES10Ext::%s", NomeDoSlotGles10Ext(slot));
    std::snprintf(det, sizeof(det), "r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x lr=0x%08x",
                  cpu.Get(kR0), cpu.Get(kR1), cpu.Get(kR2), cpu.Get(kR3), cpu.Get(kLR));
    traco.RegistarFalta(Area::Brew, nome, det);
    cpu.Set(kR0, kAeeUnsupported);
    return true;
  }
  // IGLES11ExtPak (AEEGLES11ExtPak.h): 30 slots (texgen, blend separado, FBO
  // OES). Nenhum tem motor nesta arvore -- recusa com o nome.
  if (indice >= kVtableGles11ExtPak &&
      indice < kVtableGles11ExtPak + kGles11ExtPakSlots) {
    const std::uint32_t slot = indice - kVtableGles11ExtPak;
    if (AtenderCabecaQualcomm(cpu, traco, kObjetoGles11ExtPak, slot, "IGLES11ExtPak",
                              kIidGles11ExtPak, 0)) {
      return true;
    }
    char nome[64], det[160];
    std::snprintf(nome, sizeof(nome), "IGLES11ExtPak::%s",
                  NomeDoSlotGles11ExtPak(slot));
    std::snprintf(det, sizeof(det), "r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x lr=0x%08x",
                  cpu.Get(kR0), cpu.Get(kR1), cpu.Get(kR2), cpu.Get(kR3), cpu.Get(kLR));
    traco.RegistarFalta(Area::Brew, nome, det);
    cpu.Set(kR0, kAeeUnsupported);
    return true;
  }
  // IEGLOESSwapInterval (AEEEGLOESSwapInterval.h): 5 slots.
  if (indice >= kVtableEglOesSwapInterval &&
      indice < kVtableEglOesSwapInterval + kEglOesSwapIntervalSlots) {
    const std::uint32_t slot = indice - kVtableEglOesSwapInterval;
    if (AtenderCabecaQualcomm(cpu, traco, kObjetoEglOesSwapInterval, slot,
                              "IEGLOESSwapInterval", kIidEglOesSwapInterval, 0)) {
      return true;
    }
    char nome[64], det[160];
    std::snprintf(nome, sizeof(nome), "IEGLOESSwapInterval::%s",
                  NomeDoSlotEglOesSwapInterval(slot));
    std::snprintf(det, sizeof(det), "r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x lr=0x%08x",
                  cpu.Get(kR0), cpu.Get(kR1), cpu.Get(kR2), cpu.Get(kR3), cpu.Get(kLR));
    traco.RegistarFalta(Area::Brew, nome, det);
    cpu.Set(kR0, kAeeUnsupported);
    return true;
  }
  // IEGLGetPowerLevel (AEEEGLGetPowerLevel.h): 4 slots.
  if (indice >= kVtableEglGetPowerLevel &&
      indice < kVtableEglGetPowerLevel + kEglGetPowerLevelSlots) {
    const std::uint32_t slot = indice - kVtableEglGetPowerLevel;
    if (AtenderCabecaQualcomm(cpu, traco, kObjetoEglGetPowerLevel, slot,
                              "IEGLGetPowerLevel", kIidEglGetPowerLevel, 0)) {
      return true;
    }
    char nome[64], det[160];
    std::snprintf(nome, sizeof(nome), "IEGLGetPowerLevel::%s",
                  NomeDoSlotEglGetPowerLevel(slot));
    std::snprintf(det, sizeof(det), "r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x lr=0x%08x",
                  cpu.Get(kR0), cpu.Get(kR1), cpu.Get(kR2), cpu.Get(kR3), cpu.Get(kLR));
    traco.RegistarFalta(Area::Brew, nome, det);
    cpu.Set(kR0, kAeeUnsupported);
    return true;
  }
  // O IGLES11, faixa propria.
  if (indice >= kVtableIgles && indice < kVtableIgles + kIglesSlots) {
    const std::uint32_t slot = indice - kVtableIgles;
    char nome[64], det[160];

    if (slot == igles_slots::kIgles_GetString) {
      const std::uint32_t qual = cpu.Get(kR1);
      const std::uint32_t pret = cpu.Get(kR2);
      if (pret == 0) {
        traco.RegistarFalta(Area::Brew, "IGLES11::GetString", "ponteiro de retorno nulo");
        cpu.Set(kR0, kAeeBadParm);
        return true;
      }
      Memoria& mem = cpu.Mem();
      std::uint32_t p = 0;
      std::string o_que;
      switch (qual) {
        case gl_slots::GL_EXTENSIONS:
          p = EscreverStringIgles(mem, kStrIglesExtensions, kExtensoesIgles);
          o_que = std::string("GL_EXTENSIONS = \"") + kExtensoesIgles + "\"";
          break;
        case gl_slots::GL_VERSION:
          // DECLARADO pela interface que este objecto entrega, e nao medido na
          // maquina: o `QEGL::QueryInterface` responde `AEEIID_GLES11`, e a
          // string canonica do OpenGL ES 1.1 e esta. Mesma regra do
          // `EGL_VERSION = "1.0"` em `core/brew/egl.cpp`.
          p = EscreverStringIgles(mem, kStrIglesVersion, "OpenGL ES-CM 1.1");
          o_que = "GL_VERSION = \"OpenGL ES-CM 1.1\" (declarado pela interface IGLES11)";
          break;
        case gl_slots::GL_VENDOR:
        case gl_slots::GL_RENDERER:
          // O FABRICANTE E O NOME DO CHIP SAO AFIRMACOES SOBRE O HARDWARE, e nao
          // foram medidos. A string VAZIA mantem o guest vivo (um `strstr` sobre
          // ela devolve nulo em vez de rebentar) e nao inventa nada.
          p = EscreverStringIgles(mem,
                                  qual == gl_slots::GL_VENDOR ? kStrIglesVendor
                                                              : kStrIglesRenderer,
                                  "");
          o_que = std::string(qual == gl_slots::GL_VENDOR ? "GL_VENDOR" : "GL_RENDERER") +
                  " = \"\": sem medida do que a maquina responde";
          break;
        default: {
          // NUNCA NULO, nem para uma consulta desconhecida: o endereco e valido
          // e a string e vazia. A falta fica com o numero da consulta.
          p = EscreverStringIgles(mem, kStrIglesDesconhecida, "");
          char d[96];
          std::snprintf(d, sizeof(d), "consulta 0x%08x sem medida (devolvida a string vazia)",
                        qual);
          traco.RegistarFalta(Area::Brew, "IGLES11::GetString", d);
          mem.Escrever32(pret, p);
          cpu.Set(kR0, kAeeSuccess);
          return true;
        }
      }
      mem.Escrever32(pret, p);
      cpu.Set(kR0, kAeeSuccess);
      // RESPONDER UM VALOR QUE NAO SE MEDIU E `RegistarPressuposto`, e nao
      // `RegistarFalta` (traco.h: "um caminho nunca e os dois").
      traco.RegistarPressuposto(Area::Brew, "IGLES11::GetString", o_que);
      return true;
    }

    // O CAMINHO DE TEXTURA MINIMO QUE O `glDrawTex*OES` EXIGE. O blit desenha a
    // textura LIGADA: sem `glBindTexture`/`glTexImage2D` servidos a extensao
    // anunciada nao tem o que desenhar (e os dez titulos chamam estes tres logo
    // a seguir ao `InitGLExtensions`). O estado fica no bloco `kEstadoIgles`,
    // observavel e testavel como o resto das classes.
    if (slot == igles_slots::kIgles_GenTextures) {
      // `int GenTextures(iname *pMe, GLsizei n, GLuint *textures)` (AEEGLES10.h:88).
      const std::uint32_t n = cpu.Get(kR1), lista = cpu.Get(kR2);
      if (n == 0 || lista == 0) {
        traco.RegistarFalta(Area::Brew, "IGLES11::GenTextures", "n ou lista nulo");
        cpu.Set(kR0, kAeeBadParm);
        return true;
      }
      Memoria& mem = cpu.Mem();
      std::uint32_t contador = mem.Ler32(kIglesContador);
      for (std::uint32_t k = 0; k < n; ++k) {
        mem.Escrever32(lista + 4u * k, ++contador);
      }
      mem.Escrever32(kIglesContador, contador);
      cpu.Set(kR0, kAeeSuccess);
      return true;
    }
    if (slot == igles_slots::kIgles_BindTexture) {
      // `int BindTexture(iname *pMe, AEEGLenum target, AEEGLuint texture)`.
      const std::uint32_t alvo = cpu.Get(kR1), tex = cpu.Get(kR2);
      if (alvo != gl_slots::GL_TEXTURE_2D) {
        traco.RegistarFalta(Area::Brew, "IGLES11::BindTexture",
                            "alvo diferente de GL_TEXTURE_2D");
        cpu.Set(kR0, kAeeUnsupported);
        return true;
      }
      Memoria& mem = cpu.Mem();
      mem.Escrever32(kIglesTexLigada, tex);
      cpu.Set(kR0, kAeeSuccess);
      return true;
    }
    if (slot == igles_slots::kIgles_TexImage2D) {
      // `int TexImage2D(iname *pMe, target, level, internalformat, width,
      // height, border, format, type, pixels)` (AEEGLES10.h:127): para alem do
      // pMe sao NOVE argumentos, os seis ultimos na pilha.
      Memoria& mem = cpu.Mem();
      const std::uint32_t sp = cpu.Get(kSP);
      const std::uint32_t alvo = cpu.Get(kR1);
      const std::uint32_t larg = mem.Ler32(sp + 0u);
      const std::uint32_t alt = mem.Ler32(sp + 4u);
      const std::uint32_t formato = mem.Ler32(sp + 12u);
      const std::uint32_t tipo = mem.Ler32(sp + 16u);
      const std::uint32_t pixels = mem.Ler32(sp + 20u);
      if (alvo != gl_slots::GL_TEXTURE_2D) {
        traco.RegistarFalta(Area::Brew, "IGLES11::TexImage2D",
                            "alvo diferente de GL_TEXTURE_2D");
        cpu.Set(kR0, kAeeUnsupported);
        return true;
      }
      if (larg == 0 || alt == 0) {
        traco.RegistarFalta(Area::Brew, "IGLES11::TexImage2D",
                            "textura com largura ou altura zero");
        cpu.Set(kR0, kAeeUnsupported);
        return true;
      }
      if (pixels == 0) {
        traco.RegistarFalta(Area::Brew, "IGLES11::TexImage2D",
                            "sem ponteiro para os texels");
        cpu.Set(kR0, kAeeUnsupported);
        return true;
      }
      mem.Escrever32(kIglesTexLargura, larg);
      mem.Escrever32(kIglesTexAltura, alt);
      mem.Escrever32(kIglesTexFormato, formato);
      mem.Escrever32(kIglesTexTipo, tipo);
      mem.Escrever32(kIglesTexPonteiro, pixels);
      cpu.Set(kR0, kAeeSuccess);
      return true;
    }

    // A FRENTE GLBLOCO: os DOZE slots nomeados que 8 titulos pedem (medido na
    // bateria: abd, gof, pacmania, pbc, ridgeracer, rmp, tekken2, torkandkral).
    // Antes chegavam aqui e recebiam a recusa generica com nome; agora SERVEM
    // ESTADO, pelo motor do IGL de 80 slots (ver `SlotIglesNoIgl` acima).
    const std::uint32_t no_igl = SlotIglesNoIgl(slot);
    if (no_igl != kSemSlotNoIgl && g_igles_igl != nullptr) {
      // A MOLDURA DE CHAMADA DO IGLES11: leva `iname *pMe` em r0, logo o
      // primeiro argumento real esta em r1 (o mesmo deslocamento do QEGL). O
      // quarto argumento real, quando o metodo tem quatro, esta no primeiro
      // lugar da pilha (`cpu.Get(kSP)`), e nao em r3.
      ArgumentosGl av;
      av.reg[0] = cpu.Get(kR1);
      av.reg[1] = cpu.Get(kR2);
      av.reg[2] = cpu.Get(kR3);
      av.lr = cpu.Get(kLR);
      const std::uint32_t sp = cpu.Get(kSP);
      av.sp = sp + 4u;
      if (SlotIglesTemQuartoNaPilha(slot)) {
        if (sp < 0x00010000u) {
          std::snprintf(nome, sizeof(nome), "IGLES11::%s", NomeDoSlotIgles(slot));
          traco.RegistarFalta(Area::Brew, nome, "argumentos na pilha sem sp valido");
          cpu.Set(kR0, kAeeBadParm);
          return true;
        }
        av.reg[3] = cpu.Mem().Ler32(sp);
      }
      std::uint32_t retorno = 0;
      const ResultadoGl r = g_igles_igl->Executar(no_igl, av, &retorno);
      std::snprintf(nome, sizeof(nome), "IGLES11::%s", NomeDoSlotIgles(slot));
      if (r == ResultadoGl::Feito) {
        cpu.Set(kR0, kAeeSuccess);
        const std::string& motivo = g_igles_igl->Ultimas().back().motivo;
        traco.Emitir(Area::Brew, Nivel::Depuracao, nome,
                     motivo.empty() ? "estado servido pelo motor do IGL (igl.cpp)" : motivo);
        return true;
      }
      if (r == ResultadoGl::Recusado) {
        // A RECUSA DO MOTOR COM O MOTIVO DELE, e nao a recusa generica: a regra
        // desta casa e nunca "sucesso sem efeito". O pendente e o `Clear`: sem
        // superficie ligada ao objecto IGLES11, a limpeza acumula a mascara e
        // recusa a ESCRITA (o detalhe diz "TELA LIGADA").
        traco.RegistarFalta(Area::Brew, nome, g_igles_igl->Ultimas().back().motivo);
        cpu.Set(kR0, kAeeUnsupported);
        return true;
      }
      // `NaoImplementado` nao acontece com este mapeamento (todos os doze
      // existem no IGL); se acontecer, cai na recusa generica com nome.
      (void)retorno;
    }

    // CADA SLOT TEM NOME. Uma recusa `IGLES11::slot67` nao se pode ler; a mesma
    // recusa com `IGLES11::GetString` diz que dez titulos param no
    // `glGetString`. O `slot%u` fica so para um indice FORA da tabela, que nao
    // pode acontecer com esta faixa mas nao se apaga por isso.
    if (slot < igles_slots::kQuantos) {
      std::snprintf(nome, sizeof(nome), "IGLES11::%s", NomeDoSlotIgles(slot));
    } else {
      std::snprintf(nome, sizeof(nome), "IGLES11::slot%u", slot);
    }
    std::snprintf(det, sizeof(det),
                  "r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x lr=0x%08x", cpu.Get(kR0),
                  cpu.Get(kR1), cpu.Get(kR2), cpu.Get(kR3), cpu.Get(kLR));
    traco.RegistarFalta(Area::Brew, nome, det);
    cpu.Set(kR0, kAeeUnsupported);
    return true;
  }
  if (indice < kVtableClasseBase || indice >= VtClasse(kQuantasClasses)) return false;
  const std::uint32_t k = (indice - kVtableClasseBase) / kSlotsDaClasse;
  const std::uint32_t slot = (indice - kVtableClasseBase) % kSlotsDaClasse;

  const std::uint32_t k_qegl = static_cast<std::uint32_t>(Classe::kQEGL);
  if (k == k_qegl && slot == 2) {
    // `int QueryInterface(po, AEEIID, void**)` no QEGL: GLES10/11 -> IGLES11.
    // Mesma resposta pros dois (tabela 11 estende a 10; precedente unanime).
    const std::uint32_t iid = cpu.Get(kR1);
    const std::uint32_t ppo = cpu.Get(kR2);
    if (ppo == 0) {
      cpu.Set(kR0, kAeeBadParm);
      return true;
    }
    if (iid == kIidGles10 || iid == kIidGles11) {
      cpu.Mem().Escrever32(ppo, kObjetoIgles);
      cpu.Set(kR0, kAeeSuccess);
      char det[96];
      std::snprintf(det, sizeof(det), "iid=0x%08x -> IGLES11", iid);
      traco.Emitir(Area::Brew, Nivel::Depuracao, "QEGL_QUERYINTERFACE", det);
      return true;
    }
    // O `IGLES11Ext` -- OUTRO objecto, com a vtable de 15 slots de
    // `AEEGLES11Ext.h`. E daqui que o titulo tira o `glDrawTexivOES` que o
    // `GL_OES_draw_texture` anunciado no `glGetString` lhe promete. **Anunciar
    // sem servir seria pior do que nao anunciar**: o titulo saltaria para uma
    // funcao que nao existe.
    if (iid == kIidGles11Ext) {
      cpu.Mem().Escrever32(ppo, kObjetoIglesExt);
      cpu.Set(kR0, kAeeSuccess);
      traco.Emitir(Area::Brew, Nivel::Depuracao, "QEGL_QUERYINTERFACE",
                   "iid=0x0103d8eb -> IGLES11Ext");
      return true;
    }
    // O EGL10/EGL11 (AEEEGL10.h:22 / AEEEGL11.h:20): 'this', o PROPRIO
    // objecto QEGL -- o zeebx responde o mesmo para os dois
    // (`src/machine.rs` 9815-9860, `egl_query_interface`).
    if (iid == kIidEgl10 || iid == kIidEgl11) {
      cpu.Mem().Escrever32(ppo, ObjetoDaClasse(k));
      cpu.Set(kR0, kAeeSuccess);
      traco.Emitir(Area::Brew, Nivel::Depuracao, "QEGL_QUERYINTERFACE",
                   "iid=EGL10/EGL11 -> o proprio objecto QEGL");
      return true;
    }
    // AS SETE EXTENSOES QUALCOMM (frente qualcomm; medido: 14 titulos pedem um
    // de cada, 1x -- corrida /tmp/corrida_thrd.json). OBJECTOS REAIS, com as
    // vtables do SDK: o `GLES_ext.c` do proprio titulo guarda o objecto e chama
    // os SLOTS dele. O SurfaceManip e o ImageonExt atendem o par V1/V2 com o
    // MESMO objecto (a V2 e superconjunto da V1 com o mesmo prefixo de vtable).
    struct IidParaObjecto {
      std::uint32_t iid;
      std::uint32_t objeto;
      const char* nome;  // a linha exacta do cabecalho, para o traco
    };
    static const IidParaObjecto qualcomm[] = {
        {kIidEglGetColorBuffer, kObjetoEglGetColorBuffer, "AEEEGLGetColorBuffer.h:20"},
        {kIidEglSurfaceManip, kObjetoEglSurfaceManip, "AEEEGLSurfaceManip.h:252"},
        {kIidEglSurfaceManipV1, kObjetoEglSurfaceManip, "AEEEGLSurfaceManip.h:23"},
        {kIidGlesImageonExt, kObjetoGlesImageonExt, "AEEGLESImageonEXT.h:204"},
        {kIidGlesImageonExtV1, kObjetoGlesImageonExt, "AEEGLESImageonEXT.h:23"},
        {kIidGles10Ext, kObjetoGles10Ext, "AEEGLES10Ext.h:22"},
        {kIidGles11ExtPak, kObjetoGles11ExtPak, "AEEGLES11ExtPak.h:22"},
        {kIidEglOesSwapInterval, kObjetoEglOesSwapInterval, "AEEEGLOESSwapInterval.h:22"},
        {kIidEglGetPowerLevel, kObjetoEglGetPowerLevel, "AEEEGLGetPowerLevel.h:20"},
    };
    for (const IidParaObjecto& e : qualcomm) {
      if (iid == e.iid) {
        cpu.Mem().Escrever32(ppo, e.objeto);
        cpu.Set(kR0, kAeeSuccess);
        char det[128];
        std::snprintf(det, sizeof(det), "iid=0x%08x -> %s (%s)", iid,
                      NomeDoIidDaFamiliaGl(iid), e.nome);
        traco.Emitir(Area::Brew, Nivel::Depuracao, "QEGL_QUERYINTERFACE", det);
        return true;
      }
    }
    cpu.Mem().Escrever32(ppo, 0);
    // IID DE EXTENSAO SEM OBJECT O NESTA ARVORE: a recusa leva o NOME do IID
    // (P2) em vez de um "IID nao servido" anonimo -- e e esse nome que faz a
    // lista de demanda dizer o que falta.
    char det[120];
    std::snprintf(det, sizeof(det), "iid=0x%08x (%s): sem objecto desta interface nesta arvore",
                  iid, NomeDoIidDaFamiliaGl(iid));
    traco.RegistarFalta(Area::Brew, std::string("QEGL::QueryInterface ") + NomeDoIidDaFamiliaGl(iid),
                        det);
    cpu.Set(kR0, kAeeClassNotSupported);
    return true;
  }
  const std::uint32_t k_cm = static_cast<std::uint32_t>(Classe::kCM);
  if (k == k_cm && slot == 28) {
    // `int GetSSInfo(ICM* po, AEECMSSInfo* pInfo, uint32 nSize)` -- slot 28.
    // tectoymain.c:1037 / 0x87c40: zera buffer 0x340, confere +0xc == 5 (ONLINE),
    // 0x696a0: confere +0x00 == 2 (servico pleno) e +0x28 (intensidade do sinal).
    const std::uint32_t pinfo = cpu.Get(kR1);
    const std::uint32_t tamanho = cpu.Get(kR2);
    if (pinfo == 0 || tamanho < 0x2Au) {
      cpu.Set(kR0, kAeeBadParm);
      return true;
    }
    for (std::uint32_t off = 0; off < tamanho; ++off) {
      cpu.Mem().Escrever8(pinfo + off, 0);
    }
    cpu.Mem().Escrever32(pinfo + 0x00, 2);       // AEECM_SRV_STATUS_SRV
    cpu.Mem().Escrever32(pinfo + 0x0C, 5);       // SYS_OPRT_MODE_ONLINE
    cpu.Mem().Escrever16(pinfo + 0x28, 0x0048);   // 4 barras de sinal
    cpu.Set(kR0, kAeeSuccess);
    // DECLARADO, e agora CONTADO (ver `Traco::RegistarPressuposto`): nenhum
    // destes tres valores foi medido num Zeebo a funcionar.
    //
    // O `AEECMSSInfo`, o `GetSSInfo` e o `AEECLSID_CM` NAO EXISTEM no SDK
    // extraido (BREW MP 5.0.5.1): greps vazios em
    // `platform/` por `AEECMSSInfo`, `AEECM_SS_`, `GetSSInfo` e `0x01011810`.
    // A unica base dos offsets e a engenharia reversa do `tectoy` citada acima.
    //
    // A CALIBRACAO do 0x0048, que estava so no zeebx: a rotina 0x69830 do
    // tectoy faz faixas de nove, e `0x45..0x4d` e a faixa das 4 barras
    // (zeebx `src/machine/diversos.rs:568-569`, que declara o mesmo valor).
    traco.RegistarPressuposto(Area::Brew, "ICM::GetSSInfo",
                              "radio online (+0x00=2), servico pleno (+0x0C=5), sinal "
                              "0x0048 = 4 barras (faixa 0x45..0x4d, tectoy 0x69830); "
                              "interface AUSENTE do SDK, offsets so por engenharia reversa");
    return true;
  }
  // O IThread, cooperativo: Start, Exit, Join, Suspend e GetResumeCBK (o
  // bloco no topo deste ficheiro). `false` para os slots do pool (Malloc etc.),
  // que caem no ramo generico e recusam COM O NOME.
  if (k == static_cast<std::uint32_t>(Classe::kThread) &&
      AtenderThread(cpu, traco, slot)) {
    return true;
  }
  if (k == kClasseDoAppHistory && slot == brew_slots::kAppHistory_Back) {
    // `int Back(po)` -- sem anterior na lista de 1: ENOSUCH, o fim-de-lista
    // que o `IAppHistory_Bottom` do cabecalho espera (ver SlotDaClasseImplementado).
    cpu.Set(kR0, kAeeNoSuch);
    return true;
  }
  const std::uint32_t k_texto = static_cast<std::uint32_t>(Classe::kTextCtl);
  if (k == k_texto && slot == brew_slots::kTextCtl_SetActive) {
    // `void SetActive(po, boolean)`: r1 = 0/1. Guarda; void nao toca r0.
    g_texto_ativo = (cpu.Get(kR1) != 0) ? 1u : 0u;
    return true;
  }
  if (k == k_texto && slot == brew_slots::kTextCtl_IsActive) {
    // `boolean IsActive(po)`: devolve o guardado.
    cpu.Set(kR0, g_texto_ativo);
    return true;
  }
  if (k == k_texto && slot == brew_slots::kTextCtl_SetRect) {
    // `void SetRect(po, AEERect*)`: aceita sem ler (sem Memoria neste ramo).
    return true;
  }
  if (k == k_texto && slot == brew_slots::kTextCtl_SetProperties) {
    // `void SetProperties(po, uint32)`: guarda r1.
    g_texto_props = cpu.Get(kR1);
    (void)g_texto_props;
    return true;
  }
  if (k == k_texto && slot == brew_slots::kTextCtl_SetInputMode) {
    // `AEETextInputMode SetInputMode(po, m)`: devolve o anterior, guarda o novo.
    const std::int32_t anterior = g_texto_modo;
    g_texto_modo = static_cast<std::int32_t>(cpu.Get(kR1));
    cpu.Set(kR0, static_cast<std::uint32_t>(anterior));
    return true;
  }
  if (k == k_texto && slot == brew_slots::kTextCtl_HandleEvent) {
    // `boolean HandleEvent(po, evt, w, dw)`: sem widgets, nada tratado -> FALSE.
    cpu.Set(kR0, 0);
    return true;
  }
  if (SlotDaClasseImplementado(k, slot)) {
    // `int Top(po)` -- ver a justificacao em `SlotDaClasseImplementado`.
    cpu.Set(kR0, kAeeSuccess);
    return true;
  }

  // O CAMINHO NAO IMPLEMENTADO RECUSA E REGISTA, e o registo diz o NOME DO
  // METODO. E a diferenca entre uma demanda que se le (`ITextCtl::SetInputMode`)
  // e tres numeros que obrigam a ir ao cabecalho contar em cada ronda.
  char nome[64], det[160];
  const char* do_slot = NomeDoSlotDaClasse(k, slot);
  if (slot < kSlotsDaInterface[k]) {
    std::snprintf(nome, sizeof(nome), "%s::%s", NomeDaInterface(k), do_slot);
  } else {
    // Um pedido FORA da tabela do cabecalho: a vtable tem 32 slots e a interface
    // 28. Nao se inventa um nome -- diz-se que esta fora.
    std::snprintf(nome, sizeof(nome), "%s::slot%u", NomeDaInterface(k), slot);
  }
  std::snprintf(det, sizeof(det),
                "r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x lr=0x%08x %s",
                cpu.Get(kR0), cpu.Get(kR1), cpu.Get(kR2), cpu.Get(kR3), cpu.Get(kLR),
                DescreverClsid(kFichas[k].clsid).c_str());
  traco.RegistarFalta(Area::Brew, nome, det);
  cpu.Set(kR0, kAeeUnsupported);
  return true;
}

}  // namespace zb2::brew
