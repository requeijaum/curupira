#include "core/brew/classes.h"

#include <cstdio>
#include <string>

#include "core/brew/clsids.h"
#include "tools/brew_slots.inc"
// Os TRES numeros. Vem do mesmo `.inc` gerado que o `NomeDoClsid` usa, e nao de
// literais escritos neste ficheiro: e essa a unica forma de o nome e o numero
// nao poderem divergir.
#include "tools/clsids.inc"

namespace zb2::brew {

namespace {

// O nome do slot de cada classe, vindo da tabela GERADA (`tools/brew_slots.inc`,
// gerada de `AEEText.h`, `AEEIAppHistory.h` e `AEEIValueModel.h`). Um nome de
// slot escrito a mao seria exactamente o numero transcrito de memoria que o
// gerador existe para impedir. Excecao: IThread (AEEThread.h + AEEIRscPool.h +
// AEEIQI.h), sem entrada no gerador; nomes declarados abaixo, na ordem do
// cabecalho (IQI 3 + RscPool 4 + Thread 5 = 12).
const char* NomeDoSlotThread(unsigned slot) {
  switch (slot) {
    case 0: return "AddRef";
    case 1: return "Release";
    case 2: return "QueryInterface";
    case 3: return "Malloc";
    case 4: return "Free";
    case 5: return "HoldRsc";
    case 6: return "ReleaseRsc";
    case 7: return "Start";
    case 8: return "Exit";
    case 9: return "Join";
    case 10: return "Suspend";
    case 11: return "GetResumeCBK";
    default: return "?";
  }
}

const char* NomeDoSlotPNGDecoderBREW(unsigned slot) {
  switch (slot) {
    case 0: return "AddRef";
    case 1: return "Release";
    case 2: return "QueryInterface";
    case 3: return "GetBitmap";
    case 4: return "GetRop";
    default: return "?";
  }
}

// QEGL (0x0103d8ec): DECLARADO, nao medido no SDK. So aparece em comentarios
// de teste OpenVG (UTOpenVGSuite.c:201) e numa tabela propria do zeebulator --
// nenhum `.h`/`.bid` o define. Por isso so IQI (3 slots) com nomes genericos:
// inventar slots EGL seria mentir comportamento que nao se mediu.
const char* NomeDoSlotQEGL(unsigned slot) {
  switch (slot) {
    case 0: return "AddRef";
    case 1: return "Release";
    case 2: return "QueryInterface";
    default: return "?";
  }
}

const char* (*const kNomeDoSlot[])(unsigned) = {
    &brew_slots::NomeDeAppHistory,
    &brew_slots::NomeDeValueModel,
    &brew_slots::NomeDeTextCtl,
    &NomeDoSlotThread,
    &NomeDoSlotPNGDecoderBREW,
    &NomeDoSlotQEGL,
};

// Quantos slots cada interface TEM, do mesmo cabecalho (IThread: 12, medido em
// AEEThread.h/AEEIRscPool.h/AEEIQI.h).
const std::uint32_t kSlotsDaInterface[] = {
    brew_slots::kAppHistorySlots,
    brew_slots::kValueModelSlots,
    brew_slots::kTextCtlSlots,
    12,
    5,
    3,
};

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

bool AtenderClasse(ICpu& cpu, std::uint32_t indice, Traco& traco) {
  if (indice < kVtableClasseBase || indice >= VtClasse(kQuantasClasses)) return false;
  const std::uint32_t k = (indice - kVtableClasseBase) / kSlotsDaClasse;
  const std::uint32_t slot = (indice - kVtableClasseBase) % kSlotsDaClasse;

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
