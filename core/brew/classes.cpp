#include "core/brew/classes.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "core/brew/clsids.h"
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

void ConstruirIgles(Memoria& mem, const Saidas& saidas, Traco& traco) {
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

// A LISTA DE EXTENSOES ANUNCIADA. **VAZIA, e de proposito.**
//
// ANUNCIAR UMA EXTENSAO E PROMETER SERVI-LA: o titulo que le
// `GL_OES_draw_texture` vai buscar o `glDrawTexivOES` ao `eglGetProcAddress` e
// saltar para o que vier de la. Anunciar sem servir e PIOR do que nao anunciar
// -- salta para uma funcao que nao existe.
//
// Esta string vazia e um TESTE, e nao uma resposta final: serve para medir se o
// que prende os dez titulos e mesmo a extensao que procuram. O resultado esta
// no relatorio.
constexpr const char* kExtensoesIgles = "";

// Os indices da zona de strings. Um endereco fixo por consulta, para duas
// consultas seguidas nao se pisarem.
constexpr std::uint32_t kStrIglesVendor = 0;
constexpr std::uint32_t kStrIglesRenderer = 1;
constexpr std::uint32_t kStrIglesVersion = 2;
constexpr std::uint32_t kStrIglesExtensions = 3;
constexpr std::uint32_t kStrIglesDesconhecida = 4;

bool AtenderClasse(ICpu& cpu, std::uint32_t indice, Traco& traco) {
  // O IGLES11Ext, faixa propria (15 slots, AEEGLES11Ext.h).
  if (indice >= kVtableIglesExt && indice < kVtableIglesExt + kIglesExtSlots) {
    const std::uint32_t slot = indice - kVtableIglesExt;
    char nome[64], det[192];
    std::snprintf(nome, sizeof(nome), "IGLES11Ext::%s", NomeDoSlotIglesExt(slot));
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
    cpu.Mem().Escrever32(ppo, 0);
    // IID desconhecido: cai na recusa nomeada abaixo.
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
