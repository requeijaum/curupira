#include "core/brew/classes.h"

#include <cstdio>
#include <string>

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
  switch (slot) {
    case 0: return "AddRef";
    case 1: return "Release";
    case 2: return "QueryInterface";
    default: return "?";
  }
}

void ConstruirIgles(Memoria& mem, const Saidas& saidas, Traco& traco) {
  ConstruirObjeto(mem, saidas, kObjetoIgles, saidas.Endereco(kVtableIgles),
                  kIglesSlots, kVtableIgles);
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
}

bool AtenderClasse(ICpu& cpu, std::uint32_t indice, Traco& traco) {
  // O IGLES11, faixa propria. Nenhum metodo desenhado: recusa com nome.
  if (indice >= kVtableIgles && indice < kVtableIgles + kIglesSlots) {
    const std::uint32_t slot = indice - kVtableIgles;
    char nome[64], det[160];
    if (slot < 3) {
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
