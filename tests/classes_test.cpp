#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include <cstdlib>

#include "core/brew/ajudantes.h"
#include "core/brew/classes.h"
#include "core/brew/ecra.h"   // kBaseDoEcraNoGuest (GetColorBuffer)
#include "core/brew/egl.h"     // kIidEgl* (os sete IIDs QUALCOMM)
#include "core/brew/clsids.h"
#include "core/brew/despacho.h"
#include "core/brew/interface.h"
#include "core/carga/inflate.h"   // Adler32: o stream zlib que o teste monta
#include "core/carga/png.h"       // ImagemPng::Rgb565/Crc32DePng (o descodificador)
#include "core/brew/vfs.h"
#include "core/cpu/arm_interpreter.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"
#include "tools/gl_slots.inc"
#include "tools/igles_slots.inc"
#include "tools/brew_slots.inc"
#include "tools/clsids.inc"

namespace zb2::brew {
namespace {

// ===========================================================================
// AS TRES CLASSES DO ARRANQUE: `AEECLSID_AppHistory`, `AEECLSID_TEXTCTL` e
// `AEECLSID_VALUEMODEL_1`.
//
// A MEDICAO QUE ESTES TESTES REPRODUZEM vem de duas fontes INDEPENDENTES, e o
// que os testes provam e que as duas CONCORDAM:
//
//   (1) o MODULO do titulo, desmontado. Uma sonda temporaria (um objecto com uma
//       vtable de 64 slots, um endereco de saida distinto por slot) mostrou qual
//       o INDICE de cada pedido; o desmonte do modulo deu o NOME pelo `ldr`:
//
//         tectoy 0x6c3e8  ldr r2, [r1, #0x14]   -> slot 5  -> `Top`
//         zenonia 0x45998 ldr r2, [r1, #0x18]   -> slot 6  -> `SetRect`
//         zenonia 0x459b0 ldr r2, [r1, #0x20]   -> slot 8  -> `SetProperties`
//         zenonia 0x459c8 ldr r2, [r1, #0x48]   -> slot 18 -> `SetInputMode`
//         zenonia 0x459e0 ldr r2, [r1, #0x10]   -> slot 4  -> `SetActive`
//         zenonia 0x66330 ldr r1, [r1, #0x14]   -> slot 5  -> `IsActive`
//         zenonia 0x6635c ldr ip, [r1, #0x08]   -> slot 2  -> `HandleEvent`
//
//       E os ARGUMENTOS concordam com o cabecalho: em 0x45a08 esta o literal
//       `0x80010000`, que e `TP_FRAME|TP_FIXSETRECT` (`AEEText.h:41,49`);
//       o `SetInputMode` leva **3** = `AEE_TM_LETTERS` (`AEEText.h:76`); o
//       `SetActive` leva **1** (o `boolean` do cabecalho).
//
//   (2) o CABECALHO, pela cadeia de heranca, lido por `tools/gerar_slots.py`.
//
// Se o gerador vier a ler o cabecalho de outra maneira, o teste 2 cai. **E isso
// que ele existe para apanhar** -- a lista de slots escrita a mao ja divergiu
// duas vezes neste trabalho, e da primeira o efeito foi servir bytes aleatorios.
// ===========================================================================

constexpr std::uint32_t kHeap = 0x80200000u;
constexpr std::uint32_t kHeapTam = 0x00C00000u;

class Bancada {
 public:
  Bancada() {
    saidas_.base = 0xF0000000u;
    saidas_.passo = 4;
    saidas_.quantos = 100000;  // o mesmo numero da bateria
    saidas_.ativa = true;
    cpu_.ConfigurarSaidas(saidas_);
    traco_.JuntarDestino(&destino_);
    ConstruirClasses(mem_, saidas_, traco_);
  }

  std::size_t Faltas(const std::string& nome) const {
    const auto& f = traco_.ContagemFaltas();
    const auto it = f.find(nome);
    return it == f.end() ? 0 : static_cast<std::size_t>(it->second);
  }
  // O DETALHE da recusa, que e o que a lista de demanda mostra. O destino de
  // memoria existe para isto: um teste que so contasse as faltas nao provava que
  // o NOME da classe aparece -- e e o nome que este trabalho foi buscar.
  std::string Detalhe(const std::string& nome) const {
    for (const auto& ev : destino_.eventos) {
      // O `RegistarFalta` emite com o prefixo `NAO_IMPLEMENTADO: ` (traco.cpp:122);
      // o detalhe e o que o relatorio mostra.
      if (ev.nome == "NAO_IMPLEMENTADO: " + nome) return ev.detalhe;
    }
    return "";
  }
  Traco& T() { return traco_; }
  Memoria& M() { return mem_; }
  ArmInterpreter& Cpu() { return cpu_; }
  const Saidas& S() const { return saidas_; }

 private:
  Memoria mem_;
  Traco traco_{"teste_classes", nullptr};
  DestinoMemoria destino_;
  ArmInterpreter cpu_{mem_, &traco_};
  Saidas saidas_;
};

// ---------------------------------------------------------------------------
// 1. Os tres numeros tem NOME, e o nome vem do cabecalho.
// ---------------------------------------------------------------------------
TEST(Classes, OsTresCLSIDsDoCorpusTemNomeDoSDK) {
  // `AEEAppHistory.bid:9`       #define AEECLSID_AppHistory      0x0100104f
  EXPECT_STREQ(NomeDoClsid(0x0100104fu), "AEECLSID_AppHistory");
  // `AEEClassIDs.h:209`         #define AEECLSID_TEXTCTL (AEECLSID_TEXTCTL_10 + 0x100)
  EXPECT_STREQ(NomeDoClsid(0x01003109u), "AEECLSID_TEXTCTL");
  // `AEECLSID_VALUEMODEL_1.bid:31`
  EXPECT_STREQ(NomeDoClsid(0x01028e3cu), "AEECLSID_VALUEMODEL_1");
  // E AS CONSTANTES DO `.inc` GERADO dizem o mesmo. Se o cabecalho mudar, a
  // guarda `clsids_do_sdk` falha -- aqui prova-se que o MOTOR usa o `.inc`.
  EXPECT_EQ(brew_clsids::kClsid_AppHistory, 0x0100104fu);
  EXPECT_EQ(brew_clsids::kClsid_TEXTCTL, 0x01003109u);
  EXPECT_EQ(brew_clsids::kClsid_VALUEMODEL_1, 0x01028e3cu);
}

TEST(Classes, OClsidQueOSDKNaoDeclaraNaoRecebeNomeInventado) {
  // `0x01011810` e pedido pelo `tectoy` DEPOIS de a AppHistory ser servida, e
  // NAO esta em cabecalho nenhum deste SDK: `grep -rn` sobre a arvore extraida
  // (os `*.bid`, os `*.h` e o `ClassDB.xml` do Toolset) nao encontra o valor. O
  // motor diz "desconhecido", e nao um nome plausivel -- dois emuladores alheios
  // dao-lhe dois nomes DIFERENTES, e nenhum dos dois e prova.
  EXPECT_EQ(NomeDoClsid(0x01011810u), nullptr);
  EXPECT_EQ(DescreverClsid(0x01011810u), "desconhecido (0x01011810)");
  EXPECT_EQ(DescreverClsid(0x0100104fu), "AEECLSID_AppHistory (0x0100104f)");
  // Um valor absurdo tambem nao inventa nada.
  EXPECT_EQ(NomeDoClsid(0xDEADBEEFu), nullptr);
}

// ---------------------------------------------------------------------------
// 2. A TABELA DE SLOTS CONCORDA COM O MODULO. E o teste que prova o gerador.
// ---------------------------------------------------------------------------
TEST(Classes, OsSlotsMedidosNoModuloSaoOsQueOCabecalhoDa) {
  const std::uint32_t textctl = static_cast<std::uint32_t>(Classe::kTextCtl);
  const std::uint32_t hist = static_cast<std::uint32_t>(Classe::kAppHistory);
  // Os indices sao os do DESMONTE (a lista no topo do ficheiro); os nomes sao os
  // que o gerador leu de `AEEText.h` e `AEEIAppHistory.h`.
  EXPECT_STREQ(NomeDoSlotDaClasse(textctl, 2), "HandleEvent");
  EXPECT_STREQ(NomeDoSlotDaClasse(textctl, 4), "SetActive");
  EXPECT_STREQ(NomeDoSlotDaClasse(textctl, 5), "IsActive");
  EXPECT_STREQ(NomeDoSlotDaClasse(textctl, 6), "SetRect");
  EXPECT_STREQ(NomeDoSlotDaClasse(textctl, 8), "SetProperties");
  EXPECT_STREQ(NomeDoSlotDaClasse(textctl, 18), "SetInputMode");
  EXPECT_STREQ(NomeDoSlotDaClasse(hist, 5), "Top");
  EXPECT_STREQ(NomeDoSlotDaClasse(hist, 1), "Release");
  // As interfaces NAO sao as mesmas: um `SetRect` no IValueModel seria um nome
  // emprestado.
  EXPECT_STREQ(NomeDaInterface(static_cast<std::uint32_t>(Classe::kValueModel_1)), "IValueModel");
  EXPECT_EQ(brew_slots::kValueModelSlots, 9u);
}

// ---------------------------------------------------------------------------
// 3. O unico metodo implementado: o `IAppHistory::Top` do `tectoy`.
// ---------------------------------------------------------------------------
TEST(Classes, OTopDoAppHistoryRespondeESemFalta) {
  Bancada b;
  const std::uint32_t hist = static_cast<std::uint32_t>(Classe::kAppHistory);
  b.Cpu().Set(kR0, ObjetoDaClasse(hist));
  b.Cpu().Set(kR1, 0);
  EXPECT_TRUE(SlotDaClasseImplementado(hist, brew_slots::kAppHistory_Top));
  EXPECT_TRUE(AtenderClasse(b.Cpu(), VtClasse(hist) + brew_slots::kAppHistory_Top, b.T()));
  EXPECT_EQ(b.Cpu().Get(kR0), kAeeSuccess);
  EXPECT_EQ(b.Faltas("IAppHistory::Top"), 0u);
}

// ---------------------------------------------------------------------------
// 4. O que NAO esta implementado RECUSA, e a recusa diz o NOME DO METODO.
// ---------------------------------------------------------------------------
TEST(Classes, OMetodoNaoImplementadoRecusaComONomeDoMetodo) {
  Bancada b;
  const std::uint32_t textctl = static_cast<std::uint32_t>(Classe::kTextCtl);
  const std::uint32_t obj = ObjetoDaClasse(textctl);
  b.Cpu().Set(kR0, obj);
  b.Cpu().Set(kR1, 3);          // AEE_TM_LETTERS, medido no zenonia
  b.Cpu().Set(kLR, 0x000459ccu);  // o LR medido da chamada
  // SetTitle (slot 11) segue nao implementado: recusa com o nome do metodo.
  EXPECT_TRUE(AtenderClasse(b.Cpu(), VtClasse(textctl) + brew_slots::kTextCtl_SetTitle,
                            b.T()));
  EXPECT_EQ(b.Cpu().Get(kR0), kAeeUnsupported);
  // A DEMANDA PASSA A DIZER O NOME, e nao `0x01003109`.
  EXPECT_EQ(b.Faltas("ITextCtl::SetTitle"), 1u);
  const std::string d = b.Detalhe("ITextCtl::SetTitle");
  EXPECT_NE(d.find("AEECLSID_TEXTCTL (0x01003109)"), std::string::npos) << d;
  EXPECT_NE(d.find("r1=0x00000003"), std::string::npos) << d;
  EXPECT_NE(d.find("lr=0x000459cc"), std::string::npos) << d;
}

// ---------------------------------------------------------------------------
// 5. A FAIXA. Um indice de outra faixa nao pode ser apanhado por este ramo.
// ---------------------------------------------------------------------------
TEST(Classes, AChamadaDeOutraFaixaNaoEApanhada) {
  Bancada b;
  // Os indices que o despacho ja atende antes e depois deste ramo: o
  // `CreateInstance` do IShell (2002), a entrada (20000+), o IGL (30000) e a
  // tabela de ajudantes (1000+). Apanhar um deles seria a nona ocorrencia do
  // erro de ordem desta arvore.
  EXPECT_FALSE(AtenderClasse(b.Cpu(), kBaseDoShell + 2, b.T()));
  EXPECT_FALSE(AtenderClasse(b.Cpu(), 20000, b.T()));
  EXPECT_FALSE(AtenderClasse(b.Cpu(), 30000, b.T()));
  EXPECT_FALSE(AtenderClasse(b.Cpu(), kBaseAjudantes, b.T()));
  EXPECT_FALSE(AtenderClasse(b.Cpu(), 0, b.T()));
  // E a faixa propria tem de estar ACIMA da do GL (30000 + 28): duas faixas
  // sobrepostas apagam uma vtable em silencio -- foi o defeito que obrigou o IGL
  // a mudar de faixa.
  EXPECT_GE(kVtableClasseBase, 31028u + 1u);
  EXPECT_TRUE(AtenderClasse(b.Cpu(), VtClasse(0), b.T()));
}

// ---------------------------------------------------------------------------
// 6. A CABLAGEM. Cada slot tem o SEU endereco, e o objecto aponta para a vtable.
// ---------------------------------------------------------------------------
TEST(Classes, CadaSlotTemOSeuEnderecoEACablagemLeDeVolta) {
  Bancada b;
  for (std::uint32_t k = 0; k < kQuantasClasses; ++k) {
    const std::uint32_t vt = b.S().Endereco(VtClasse(k));
    EXPECT_EQ(b.M().Ler32(ObjetoDaClasse(k)), vt) << "classe " << NomeDaClasse(k);
    EXPECT_EQ(b.M().Ler32(ObjetoDaClasse(k) + 4), 1u);
    for (std::uint32_t s = 2; s < kSlotsDaClasse; ++s) {
      EXPECT_EQ(b.M().Ler32(vt + s * 4), b.S().Endereco(VtClasse(k) + s))
          << "classe " << NomeDaClasse(k) << " slot " << s;
    }
  }
  // E as entradas de IBase sao as do despacho (3 = AddRef, 4 = Release), as
  // mesmas de todos os outros objectos.
  EXPECT_EQ(b.M().Ler32(b.S().Endereco(VtClasse(0))), b.S().Endereco(3));
  EXPECT_EQ(b.M().Ler32(b.S().Endereco(VtClasse(0)) + 4), b.S().Endereco(4));
}

// ---------------------------------------------------------------------------
// 7. O `ObjetoDoClsid` -- o que o `CreateInstance` vai usar.
// ---------------------------------------------------------------------------
TEST(Classes, ObjetoDoClsidSoRespondeAsTresClasses) {
  EXPECT_EQ(ObjetoDoClsid(0x0100104fu), ObjetoDaClasse(0));
  EXPECT_EQ(ObjetoDoClsid(0x01028e3cu), ObjetoDaClasse(1));
  EXPECT_EQ(ObjetoDoClsid(0x01003109u), ObjetoDaClasse(2));
  EXPECT_EQ(ObjetoDoClsid(0x01001017u), ObjetoDaClasse(3));
  EXPECT_EQ(ObjetoDoClsid(0x01030766u), ObjetoDaClasse(4));
  EXPECT_EQ(ObjetoDoClsid(0x0103d8ecu), ObjetoDaClasse(5));
  EXPECT_EQ(ObjetoDoClsid(0x01011810u), ObjetoDaClasse(static_cast<std::uint32_t>(Classe::kCM)));
  EXPECT_EQ(ObjetoDoClsid(0xDEADBEEFu), 0u);
  EXPECT_EQ(IndiceDaClasse(0x01003109u), static_cast<std::uint32_t>(Classe::kTextCtl));
  EXPECT_EQ(IndiceDaClasse(0x01001017u), static_cast<std::uint32_t>(Classe::kThread));
  EXPECT_EQ(IndiceDaClasse(0x12345678u), kQuantasClasses);
}

TEST(Classes, OThreadTemDozeSlotsNaOrdemDoCabecalho) {
  const std::uint32_t th = static_cast<std::uint32_t>(Classe::kThread);
  EXPECT_STREQ(NomeDaInterface(th), "IThread");
  EXPECT_STREQ(NomeDaClasse(th), "AEECLSID_THREAD");
  EXPECT_STREQ(NomeDoSlotDaClasse(th, 0), "AddRef");
  EXPECT_STREQ(NomeDoSlotDaClasse(th, 2), "QueryInterface");
  EXPECT_STREQ(NomeDoSlotDaClasse(th, 3), "Malloc");
  EXPECT_STREQ(NomeDoSlotDaClasse(th, 6), "ReleaseRsc");
  EXPECT_STREQ(NomeDoSlotDaClasse(th, 7), "Start");
  EXPECT_STREQ(NomeDoSlotDaClasse(th, 8), "Exit");
  EXPECT_STREQ(NomeDoSlotDaClasse(th, 11), "GetResumeCBK");
  // O QUE ESTA IMPLEMENTADO, e o que NAO esta. Os cinco metodos da propria
  // thread (Start, Exit, Join, Suspend, GetResumeCBK) tem estado e acao; os
  // quatro do pool de recursos herdado (`Malloc`, `Free`, `HoldRsc`,
  // `ReleaseRsc`) nao -- nao ha heap de thread alcancavel deste ficheiro, e o
  // corpus nao chama nenhum deles (medido na bateria: 0 pedidos).
  EXPECT_TRUE(SlotDaClasseImplementado(th, brew_slots::kThread_Start));
  EXPECT_TRUE(SlotDaClasseImplementado(th, brew_slots::kThread_Exit));
  EXPECT_TRUE(SlotDaClasseImplementado(th, brew_slots::kThread_Join));
  EXPECT_TRUE(SlotDaClasseImplementado(th, brew_slots::kThread_Suspend));
  EXPECT_TRUE(SlotDaClasseImplementado(th, brew_slots::kThread_GetResumeCBK));
  EXPECT_FALSE(SlotDaClasseImplementado(th, brew_slots::kThread_Malloc));
  EXPECT_FALSE(SlotDaClasseImplementado(th, brew_slots::kThread_Free));
  EXPECT_FALSE(SlotDaClasseImplementado(th, brew_slots::kThread_HoldRsc));
  EXPECT_FALSE(SlotDaClasseImplementado(th, brew_slots::kThread_ReleaseRsc));
}

TEST(Classes, OThreadMallocRecusaComNomeEPorIssoNaoMente) {
  // O pool de recursos do IThread NAO esta implementado (sem heap de thread
  // alcancavel deste ficheiro; o corpus nao pede nenhum destes quatro slots).
  // A recusa tem de ter o NOME do metodo, como no resto da arvore.
  Bancada b;
  const std::uint32_t th = static_cast<std::uint32_t>(Classe::kThread);
  b.Cpu().Set(kR0, ObjetoDaClasse(th));
  EXPECT_TRUE(AtenderClasse(b.Cpu(), VtClasse(th) + brew_slots::kThread_Malloc, b.T()));
  EXPECT_EQ(b.Cpu().Get(kR0), kAeeUnsupported);
  EXPECT_EQ(b.Faltas("IThread::Malloc"), 1u);
}

// ---------------------------------------------------------------------------
// 8. O `ValueModel`: o titulo CRIA e nao chama nada (medido no `tectoy` -- o
//    unico pedido na fase do arranque e o `CreateInstance`). Logo nao ha um
//    metodo para implementar, e todos recusam com o nome.
// ---------------------------------------------------------------------------
TEST(Classes, OBackDoAppHistoryDevolveNoSuchESemFalta) {
  Bancada b;
  const std::uint32_t hist = static_cast<std::uint32_t>(Classe::kAppHistory);
  b.Cpu().Set(kR0, ObjetoDaClasse(hist));
  EXPECT_TRUE(SlotDaClasseImplementado(hist, brew_slots::kAppHistory_Back));
  EXPECT_TRUE(AtenderClasse(b.Cpu(), VtClasse(hist) + brew_slots::kAppHistory_Back, b.T()));
  EXPECT_EQ(b.Cpu().Get(kR0), kAeeNoSuch);
  EXPECT_EQ(b.Faltas("IAppHistory::Back"), 0u);
}

TEST(Classes, OPNGDecoderTemCincoSlotsNaOrdemDoCabecalho) {
  const std::uint32_t png =
      static_cast<std::uint32_t>(Classe::kPNGDecoderBREW);
  EXPECT_STREQ(NomeDaInterface(png), "IImageDecoder");
  EXPECT_STREQ(NomeDaClasse(png), "AEECLSID_PNGDECODER_BREW");
  EXPECT_STREQ(NomeDoSlotDaClasse(png, 3), "GetBitmap");
  EXPECT_STREQ(NomeDoSlotDaClasse(png, 4), "GetRop");
  // OS DOIS METODOS DA INTERFACE TEM CORPO (frente imgdec): o `GetBitmap` devolve
  // o bitmap descodificado e o `GetRop` a operacao de rasterizacao. A cabeca
  // (0..2) e do `INHERIT_IQI` e nao entra nesta lista -- a regra e a mesma do
  // `QEGL` e do `ITextCtl`. O slot 5 esta FORA da tabela do cabecalho (5 slots).
  EXPECT_FALSE(SlotDaClasseImplementado(png, 2));
  EXPECT_TRUE(SlotDaClasseImplementado(png, 3));
  EXPECT_TRUE(SlotDaClasseImplementado(png, 4));
  EXPECT_FALSE(SlotDaClasseImplementado(png, 5));
}


// ---------------------------------------------------------------------------
// 9. A TABELA DO IThread, DO IImageDecoder E DO IForceFeed E A GERADA.
//
// Estes dois primeiros tinham os nomes de slot num `switch` escrito a mao no
// `classes.cpp`, e as CONTAGENS (12 e 5) como literais na tabela
// `kSlotsDaInterface`. Passaram a vir do `tools/brew_slots.inc`, gerado de
// `AEEThread.h` + `AEEIRscPool.h` + `AEEIQI.h` e de `AEEIImageDecoder.h`.
//
// O QUE SE PROVA AQUI e a coincidencia entre as DUAS fontes: a medicao do modulo
// (`[r1,#0x1c]` = slot 7) e a leitura do cabecalho. Se o gerador voltar a contar
// a cabeca por outra regra -- foi essa a origem do erro de um em todo o `IFile` --
// estes `EXPECT` caem; um `switch` a mao, esse, concordava consigo proprio.
// ---------------------------------------------------------------------------
TEST(Classes, AThreadEaImageDecoderSaoAsTabelasGeradasDoCabecalho) {
  // AS CADEIAS, resolvidas pelo gerador a partir dos cabecalhos:
  //   INHERIT_IQI (3: AddRef, Release, QueryInterface)
  //   + INHERIT_IRscPool (4: Malloc, Free, HoldRsc, ReleaseRsc)  = 7
  //   + os 5 proprios do IThread                                 = 12
  EXPECT_EQ(brew_slots::kThread_Malloc, 3u);
  EXPECT_EQ(brew_slots::kThread_ReleaseRsc, 6u);
  EXPECT_EQ(brew_slots::kThread_Start, 7u);
  EXPECT_EQ(brew_slots::kThread_GetResumeCBK, 11u);
  EXPECT_EQ(brew_slots::kThreadSlots, 12u);
  EXPECT_EQ(brew_slots::kImageDecoder_GetBitmap, 3u);
  EXPECT_EQ(brew_slots::kImageDecoder_GetRop, 4u);
  EXPECT_EQ(brew_slots::kImageDecoderSlots, 5u);
  // O NOME e indexado PELO PROPRIO SLOT -- o slot 7 do IThread e o `Start`.
  EXPECT_STREQ(brew_slots::NomeDeThread(7), "Start");
  EXPECT_STREQ(brew_slots::NomeDeImageDecoder(3), "GetBitmap");
  EXPECT_STREQ(brew_slots::NomeDeImageDecoder(4), "GetRop");
  // E o limite e o da tabela do cabecalho, nao o da vtable (32).
  EXPECT_STREQ(brew_slots::NomeDeThread(12), "slot_fora_da_tabela");
  EXPECT_STREQ(brew_slots::NomeDeImageDecoder(5), "slot_fora_da_tabela");
  // O MOTOR USA ESSA TABELA: `kNomeDoSlot[]` em `classes.cpp` aponta para as
  // funcoes geradas, e nao para um `switch` deste ficheiro.
  const std::uint32_t th = static_cast<std::uint32_t>(Classe::kThread);
  const std::uint32_t png = static_cast<std::uint32_t>(Classe::kPNGDecoderBREW);
  EXPECT_STREQ(NomeDoSlotDaClasse(th, brew_slots::kThread_Start), "Start");
  EXPECT_STREQ(NomeDoSlotDaClasse(th, brew_slots::kThreadSlots - 1), "GetResumeCBK");
  EXPECT_STREQ(NomeDoSlotDaClasse(png, brew_slots::kImageDecoder_GetBitmap), "GetBitmap");
  EXPECT_STREQ(NomeDoSlotDaClasse(png, brew_slots::kImageDecoder_GetRop), "GetRop");
  // UMA SO LEITURA: a contagem e o nome do ultimo slot nao podem divergir --
  // sao o mesmo `metodos()` do gerador.
  EXPECT_STREQ(brew_slots::NomeDeThread(brew_slots::kThreadSlots - 1), "GetResumeCBK");
  // O `IForceFeed` entra na tabela na mesma: o cabecalho existe
  // (`platform/system/inc/AEEIForceFeed.h`) e nenhuma classe do motor o usa
  // ainda. IQI 3 + `Write` + `Reset`.
  EXPECT_EQ(brew_slots::kForceFeedSlots, 5u);
  EXPECT_EQ(brew_slots::kForceFeed_Write, 3u);
  EXPECT_STREQ(brew_slots::NomeDeForceFeed(3), "Write");
  EXPECT_STREQ(brew_slots::NomeDeForceFeed(4), "Reset");
}

TEST(Classes, AContagemDeSlotsDoThreadEDoPNGDecidemONomeDaRecusa) {
  // A CONTAGEM NAO E UM DETALHE DE ESTILO: e ela que decide se a recusa diz o
  // NOME do metodo ou `slotN`. O ultimo slot do cabecalho tem nome; o seguinte,
  // que a vtable de 32 slots tambem atende, ja esta fora da tabela. A fronteira
  // entre os dois E a contagem lida do cabecalho.
  const std::uint32_t th = static_cast<std::uint32_t>(Classe::kThread);
  const std::uint32_t png = static_cast<std::uint32_t>(Classe::kPNGDecoderBREW);
  Bancada b;
  b.Cpu().Set(kR0, ObjetoDaClasse(th));
  // DENTRO da tabela ainda ha quem recuse COM NOME: o `Malloc` do pool herdado
  // (slot 3), que esta frente nao implementa.
  EXPECT_TRUE(AtenderClasse(b.Cpu(), VtClasse(th) + brew_slots::kThread_Malloc, b.T()));
  EXPECT_EQ(b.Faltas("IThread::Malloc"), 1u);
  EXPECT_TRUE(AtenderClasse(b.Cpu(), VtClasse(th) + 12, b.T()));
  EXPECT_EQ(b.Faltas("IThread::slot12"), 1u);
  b.Cpu().Set(kR0, ObjetoDaClasse(png));
  EXPECT_TRUE(AtenderClasse(b.Cpu(), VtClasse(png) + 4, b.T()));
  EXPECT_EQ(b.Faltas("IImageDecoder::GetRop"), 1u);
  EXPECT_TRUE(AtenderClasse(b.Cpu(), VtClasse(png) + 5, b.T()));
  EXPECT_EQ(b.Faltas("IImageDecoder::slot5"), 1u);
  // E O QEGL, QUE NAO TEM CABECALHO NO SDK, fica como estava: so os tres slots do
  // `INHERIT_IQI` sao nomeados, e o resto diz "?" -- inventar os 24 nomes EGL que
  // faltam seria copiar a ABI de outro emulador. O que ESTA preso ao cabecalho e a
  // CONTAGEM: 26 e o ultimo slot dentro da tabela, 27 ja esta fora
  // (QEGL = IEGL sem `GetProcAddress`: `kIeglSlots` - 1 = 27, e o `kIeglSlots` vem
  // de `AEEGL.h` pelo `tools/gl_slots.inc`).
  const std::uint32_t q = static_cast<std::uint32_t>(Classe::kQEGL);
  b.Cpu().Set(kR0, ObjetoDaClasse(q));
  EXPECT_TRUE(AtenderClasse(b.Cpu(), VtClasse(q) + 26, b.T()));
  EXPECT_EQ(b.Faltas("QEGL::?"), 1u);
  EXPECT_TRUE(AtenderClasse(b.Cpu(), VtClasse(q) + 27, b.T()));
  EXPECT_EQ(b.Faltas("QEGL::slot27"), 1u);
}

TEST(Classes, OTextCtlGuardaEstadoESemFalta) {
  Bancada b;
  const std::uint32_t tc = static_cast<std::uint32_t>(Classe::kTextCtl);
  // IsActive comeca 0.
  b.Cpu().Set(kR0, ObjetoDaClasse(tc));
  EXPECT_TRUE(AtenderClasse(b.Cpu(), VtClasse(tc) + brew_slots::kTextCtl_IsActive, b.T()));
  EXPECT_EQ(b.Cpu().Get(kR0), 0u);
  // SetActive(1) -> IsActive 1; SetActive(0) -> IsActive 0.
  b.Cpu().Set(kR1, 1);
  EXPECT_TRUE(AtenderClasse(b.Cpu(), VtClasse(tc) + brew_slots::kTextCtl_SetActive, b.T()));
  EXPECT_TRUE(AtenderClasse(b.Cpu(), VtClasse(tc) + brew_slots::kTextCtl_IsActive, b.T()));
  EXPECT_EQ(b.Cpu().Get(kR0), 1u);
  b.Cpu().Set(kR1, 0);
  EXPECT_TRUE(AtenderClasse(b.Cpu(), VtClasse(tc) + brew_slots::kTextCtl_SetActive, b.T()));
  EXPECT_TRUE(AtenderClasse(b.Cpu(), VtClasse(tc) + brew_slots::kTextCtl_IsActive, b.T()));
  EXPECT_EQ(b.Cpu().Get(kR0), 0u);
  // SetInputMode devolve o anterior.
  b.Cpu().Set(kR1, 3);
  EXPECT_TRUE(
      AtenderClasse(b.Cpu(), VtClasse(tc) + brew_slots::kTextCtl_SetInputMode, b.T()));
  EXPECT_EQ(b.Cpu().Get(kR0), 0u);
  b.Cpu().Set(kR1, 5);
  EXPECT_TRUE(
      AtenderClasse(b.Cpu(), VtClasse(tc) + brew_slots::kTextCtl_SetInputMode, b.T()));
  EXPECT_EQ(b.Cpu().Get(kR0), 3u);
  // SetRect/SetProperties/HandleEvent aceitam sem falta.
  EXPECT_TRUE(AtenderClasse(b.Cpu(), VtClasse(tc) + brew_slots::kTextCtl_SetRect, b.T()));
  b.Cpu().Set(kR1, 0x80010000u);
  EXPECT_TRUE(
      AtenderClasse(b.Cpu(), VtClasse(tc) + brew_slots::kTextCtl_SetProperties, b.T()));
  b.Cpu().Set(kR1, 0);
  b.Cpu().Set(kR2, 0);
  b.Cpu().Set(kR3, 0x000a1000u);
  EXPECT_TRUE(
      AtenderClasse(b.Cpu(), VtClasse(tc) + brew_slots::kTextCtl_HandleEvent, b.T()));
  EXPECT_EQ(b.Cpu().Get(kR0), 0u);
  EXPECT_EQ(b.Faltas("ITextCtl::SetActive"), 0u);
  EXPECT_EQ(b.Faltas("ITextCtl::IsActive"), 0u);
  EXPECT_EQ(b.Faltas("ITextCtl::SetInputMode"), 0u);
}

TEST(Classes, OQEGLTemTresSlotsDeclaradosERecusaComNome) {
  const std::uint32_t q = static_cast<std::uint32_t>(Classe::kQEGL);
  EXPECT_STREQ(NomeDaInterface(q), "QEGL");
  EXPECT_STREQ(NomeDaClasse(q), "AEECLSID_QEGL");
  EXPECT_STREQ(NomeDoSlotDaClasse(q, 2), "QueryInterface");
  EXPECT_EQ(ObjetoDoClsid(0x0103d8ecu), ObjetoDaClasse(q));
  Bancada b;
  // IID desconhecido: escreve 0 + recusa nomeada.
  b.Cpu().Set(kR0, ObjetoDaClasse(q));
  b.Cpu().Set(kR1, 0x12345678u);
  b.Cpu().Set(kR2, 0x80100000u);
  EXPECT_TRUE(AtenderClasse(b.Cpu(), VtClasse(q) + 2, b.T()));
  EXPECT_EQ(b.Cpu().Get(kR0), kAeeClassNotSupported);
  EXPECT_EQ(b.M().Ler32(0x80100000u), 0u);
  // O IID SEM OBJECTO RECUSA COM O NOME (P2): a chave passa a dizer o IID, em
  // vez do "QEGL::QueryInterface" anonimo da medicao de 15/09.
  EXPECT_EQ(b.Faltas("QEGL::QueryInterface"), 0u);
  EXPECT_EQ(b.Faltas("QEGL::QueryInterface iid_fora_da_tabela"), 1u);
  // GLES10/11: IGLES11 + SUCCESS, sem falta.
  for (std::uint32_t iid : {0x0103d8ddu, 0x0103d8eau}) {
    b.Cpu().Set(kR0, ObjetoDaClasse(q));
    b.Cpu().Set(kR1, iid);
    b.Cpu().Set(kR2, 0x80100000u);
    EXPECT_TRUE(AtenderClasse(b.Cpu(), VtClasse(q) + 2, b.T()));
    EXPECT_EQ(b.Cpu().Get(kR0), kAeeSuccess);
    EXPECT_EQ(b.M().Ler32(0x80100000u), kObjetoIgles);
  }
  // Nem o GLES10 nem o GLES11 registam falta nenhuma: a unica desta bancada e
  // a do IID de fora da tabela, e continua com o nome no fim.
  EXPECT_EQ(b.Faltas("QEGL::QueryInterface"), 0u);
  EXPECT_EQ(b.Faltas("QEGL::QueryInterface iid_fora_da_tabela"), 1u);
  // ppo nulo: EBADPARM sem escrever.
  b.Cpu().Set(kR1, 0x0103d8eau);
  b.Cpu().Set(kR2, 0);
  EXPECT_TRUE(AtenderClasse(b.Cpu(), VtClasse(q) + 2, b.T()));
  EXPECT_EQ(b.Cpu().Get(kR0), kAeeBadParm);
}

TEST(Classes, OIglesRecusaComNomeESemMetodoFingido) {
  Bancada b;
  EXPECT_EQ(b.M().Ler32(kObjetoIgles), b.S().Endereco(kVtableIgles));
  b.Cpu().Set(kR0, kObjetoIgles);
  // O 79 E O `MatrixMode`, e a recusa TEM DE O DIZER: era `IGLES11::slot79` --
  // um numero cru que obrigava a contar campos no cabecalho a cada leitura da
  // lista de demanda. O nome vem de `tools/igles_slots.inc` (gerado).
  EXPECT_TRUE(AtenderClasse(b.Cpu(), kVtableIgles + igles_slots::kIgles_MatrixMode, b.T()));
  EXPECT_EQ(b.Cpu().Get(kR0), kAeeUnsupported);
  EXPECT_EQ(b.Faltas("IGLES11::MatrixMode"), 1u);
  EXPECT_EQ(b.Faltas("IGLES11::slot79"), 0u);
}

// OS NOMES DO IGLES11 SAO LIDOS DOS CABECALHOS, e nao escritos a mao.
//
// A tabela vem de `tools/nomear_igles.py` (AEEGLES10.h + AEEGLES11.h). Este
// teste fixa as ANCORAS que foram conferidas a mao: se o gerador mudar de
// ordem, a recusa passa a ter o nome errado -- que e PIOR do que nao ter nome,
// porque parece medido.
TEST(Classes, OsNomesDoIglesSaoOsDosCabecalhos) {
  EXPECT_EQ(igles_slots::kQuantos, kIglesSlots);
  EXPECT_STREQ(NomeDoSlotIgles(0), "AddRef");
  EXPECT_STREQ(NomeDoSlotIgles(2), "QueryInterface");
  EXPECT_STREQ(NomeDoSlotIgles(33), "BindTexture");
  EXPECT_STREQ(NomeDoSlotIgles(64), "GenTextures");
  EXPECT_STREQ(NomeDoSlotIgles(65), "GetError");
  EXPECT_STREQ(NomeDoSlotIgles(67), "GetString");
  EXPECT_STREQ(NomeDoSlotIgles(74), "LoadIdentity");
  EXPECT_STREQ(NomeDoSlotIgles(79), "MatrixMode");
  EXPECT_STREQ(NomeDoSlotIgles(104), "TexParameterx");
  EXPECT_STREQ(NomeDoSlotIgles(108), "Viewport");
  EXPECT_STREQ(NomeDoSlotIgles(147), "PointSizePointerOES");
  EXPECT_STREQ(NomeDoSlotIgles(148), "?");
}

// O `glGetString` DO IGLES11 -- slot 67, pedido por DEZ titulos.
//
// MEDIDO na corrida de referencia: os dez titulos `emulator_neo` (cninja,
// spinmast, strhoop, supbtime, karnovr, wizdfire, magdrop3, darkseal, baddudes,
// hbarrel) chamam este slot UMA vez, com r1=0x1f03 (GL_EXTENSIONS), e a recusa
// e o ultimo pedido de cada um. As strings do `.mod` dizem o resto:
// `GL_OES_draw_texture`, `glDrawTexivOES`, `InitGLExtensions failed`,
// `c:/my_code/emulator_neo/framework/GLES_ext.c`.
TEST(Classes, OGetStringDoIglesNuncaDevolveNuloESoAnunciaOServido) {
  Bancada b;
  const std::uint32_t pret = 0x80100000u;
  auto pedir = [&](std::uint32_t qual) {
    b.M().Escrever32(pret, 0xdeadbeefu);
    b.Cpu().Set(kR0, kObjetoIgles);
    b.Cpu().Set(kR1, qual);
    b.Cpu().Set(kR2, pret);
    EXPECT_TRUE(AtenderClasse(b.Cpu(), kVtableIgles + igles_slots::kIgles_GetString, b.T()));
    EXPECT_EQ(b.Cpu().Get(kR0), kAeeSuccess);
    const std::uint32_t p = b.M().Ler32(pret);
    EXPECT_NE(p, 0u) << "NUNCA NULO: ha um strstr medido sobre este resultado";
    std::string s;
    b.M().LerCadeia(p, &s, kPassoDeStringIgles);
    return s;
  };
  // A LISTA ANUNCIADA E SO O QUE SE SERVE: `GL_OES_draw_texture`, porque o
  // blit existe (servido em classes.cpp); nada mais -- anunciar o que nao
  // existe faz o titulo saltar para uma funcao que nao esta la.
  const std::string ext = pedir(gl_slots::GL_EXTENSIONS);
  EXPECT_NE(ext.find("GL_OES_draw_texture"), std::string::npos) << ext;
  EXPECT_EQ(ext.find("GL_ATI_"), std::string::npos) << ext;
  EXPECT_EQ(ext.find("GL_ARB_"), std::string::npos) << ext;
  EXPECT_EQ(pedir(gl_slots::GL_VERSION), "OpenGL ES-CM 1.1");
  EXPECT_EQ(pedir(gl_slots::GL_VENDOR), "");
  EXPECT_EQ(pedir(gl_slots::GL_RENDERER), "");
  // Nenhum destes quatro e uma RECUSA: sao pressupostos (traco.h, a regra de
  // fronteira). A lista de faltas do IGLES11::GetString tem de estar vazia.
  EXPECT_EQ(b.Faltas("IGLES11::GetString"), 0u);
  // Uma consulta sem medida: string vazia, endereco valido, E uma falta com o
  // numero -- e assim que se aprende o que os titulos pedem.
  EXPECT_EQ(pedir(0x1f04u), "");
  EXPECT_EQ(b.Faltas("IGLES11::GetString"), 1u);
  // Ponteiro de retorno nulo: EBADPARM, sem escrever em lado nenhum.
  b.Cpu().Set(kR0, kObjetoIgles);
  b.Cpu().Set(kR1, gl_slots::GL_EXTENSIONS);
  b.Cpu().Set(kR2, 0);
  EXPECT_TRUE(AtenderClasse(b.Cpu(), kVtableIgles + igles_slots::kIgles_GetString, b.T()));
  EXPECT_EQ(b.Cpu().Get(kR0), kAeeBadParm);
  EXPECT_EQ(b.Faltas("IGLES11::GetString"), 2u);
}

TEST(Classes, OValueModelNaoTemMetodoImplementadoEPorIssoRecusaComNome) {
  const std::uint32_t vm = static_cast<std::uint32_t>(Classe::kValueModel_1);
  for (std::uint32_t s = 0; s < brew_slots::kValueModelSlots; ++s) {
    if (s < 2) continue;  // IBase, servida por todos os objectos
    EXPECT_FALSE(SlotDaClasseImplementado(vm, s)) << "slot " << s;
  }
  Bancada b;
  EXPECT_TRUE(AtenderClasse(b.Cpu(), VtClasse(vm) + brew_slots::kValueModel_GetValue, b.T()));
  EXPECT_EQ(b.Cpu().Get(kR0), kAeeUnsupported);
  EXPECT_EQ(b.Faltas("IValueModel::GetValue"), 1u);
}

TEST(Classes, OCMRespondeGetSSInfoComRadioNoArEServicoPleno) {
  // AEECLSID_CM (0x01011810) -- o gerenciador de chamadas/rede Qualcomm (ICM).
  // tectoymain.c:1037 aborta se CreateInstance falhar; 0x87c40 chama slot 28
  // (GetSSInfo) com buffer de 0x340 bytes e espera +0xc == 5 (ONLINE),
  // 0x696a0 espera +0x00 == 2 (servico pleno) e sinal em +0x28 (4 barras).
  const std::uint32_t cm = static_cast<std::uint32_t>(Classe::kCM);
  EXPECT_STREQ(NomeDaInterface(cm), "ICM");
  EXPECT_STREQ(NomeDaClasse(cm), "AEECLSID_CM");
  EXPECT_STREQ(NomeDoSlotDaClasse(cm, 0), "AddRef");
  EXPECT_STREQ(NomeDoSlotDaClasse(cm, 1), "Release");
  EXPECT_STREQ(NomeDoSlotDaClasse(cm, 2), "QueryInterface");
  EXPECT_STREQ(NomeDoSlotDaClasse(cm, 28), "GetSSInfo");

  Bancada b;
  constexpr std::uint32_t kBuffer = 0x80200000u;
  // Preenche a area com lixo para testar que o buffer e zerado
  for (std::uint32_t off = 0; off < 0x340; off += 4) {
    b.M().Escrever32(kBuffer + off, 0xAAAAAAAAu);
  }

  // 1. Chamada valida
  b.Cpu().Set(kR0, ObjetoDaClasse(cm));
  b.Cpu().Set(kR1, kBuffer);
  b.Cpu().Set(kR2, 0x340u);
  EXPECT_TRUE(AtenderClasse(b.Cpu(), VtClasse(cm) + 28, b.T()));
  EXPECT_EQ(b.Cpu().Get(kR0), kAeeSuccess);
  EXPECT_EQ(b.M().Ler32(kBuffer + 0x00), 2u);        // AEECM_SRV_STATUS_SRV
  EXPECT_EQ(b.M().Ler32(kBuffer + 0x04), 0u);        // resto zerado
  EXPECT_EQ(b.M().Ler32(kBuffer + 0x08), 0u);
  EXPECT_EQ(b.M().Ler32(kBuffer + 0x0C), 5u);        // SYS_OPRT_MODE_ONLINE
  EXPECT_EQ(b.M().Ler16(kBuffer + 0x28), 0x0048u);    // 4 barras de sinal
  EXPECT_EQ(b.M().Ler32(kBuffer + 0x30), 0u);        // fim zerado

  // 2. Parametros invalidos (buffer nulo ou tamanho insuficiente)
  b.Cpu().Set(kR1, 0);
  EXPECT_TRUE(AtenderClasse(b.Cpu(), VtClasse(cm) + 28, b.T()));
  EXPECT_EQ(b.Cpu().Get(kR0), kAeeBadParm);

  b.Cpu().Set(kR1, kBuffer);
  b.Cpu().Set(kR2, 10u);  // menor que o offset minimo
  EXPECT_TRUE(AtenderClasse(b.Cpu(), VtClasse(cm) + 28, b.T()));
  EXPECT_EQ(b.Cpu().Get(kR0), kAeeBadParm);

  // 3. Slot nao implementado recusa com nome
  EXPECT_TRUE(AtenderClasse(b.Cpu(), VtClasse(cm) + 15, b.T()));
  EXPECT_EQ(b.Cpu().Get(kR0), kAeeUnsupported);
  EXPECT_EQ(b.Faltas("ICM::?"), 1u);
}

// ---------------------------------------------------------------------------
// 10. O IThread: a fila completa -- CreateInstance pelo slot 2 do IShell, leitura
//     do slot 7 NA VTABLE do guest, e chamada do Start por codigo ARM real.
//
// MEDIDO (bateria, 62 titulos, ZB2_QUADROS=300 ZB2_EVT_START=1, HEAD 68f5238):
// 22 dos 62 titulos pedem `IThread::Start` UMA vez a cada um. As pilhas pedidas
// (r1), por titulo: dez titulos pedem 0x4000 (16 KiB, familia `ttd`), oito pedem
// 0x80000 (512 KiB, dos callbacks de temporizador), um pede 0x100000 (1 MiB),
// um pede 0x40000, um pede 0x10000 e um pede 0x4000 com pfn proprio. O
// `zeeboids` faz a fila por um callback: SetTimer(10 ms) -> callback ->
// CreateInstance(AEECLSID_THREAD) -> Start, e depois ITHREAD_Stop/Resume noutros
// eventos (0x19440/0x195b8).
//
// O TESTE reproduz o caminho inteiro e NAO chama o slot interno: chama o
// `CreateInstance` do IShell (slot 2, como o titulo), confere a vtable do
// objecto na memoria do guest (a "cablagem" -- um teste que chamasse
// `VtClasse(k)+7` nao provaria que a vtable aponta para la), e chama o Start por
// codigo ARM que le `[r0]` e depois `[r1,#0x1c]` -- as MESMAS duas leituras que
// o modulo do titulo faz. Sem a cablagem, o teste cai na recusa; com ela, o
// Start responde SUCCESS e o resultado e visivel na memoria do guest.
// ---------------------------------------------------------------------------
constexpr std::uint32_t kPilhaDoTeste = 0x80080000u;
constexpr std::uint32_t kTabelaDoTeste = 0x80010000u;
constexpr std::uint32_t kTamanhoDoModuloDeTeste = 0x00100000u;
constexpr std::uint32_t kSentinelaDoTeste = 0xFFFFFFF0u;
constexpr std::uint32_t kPpObjDoTeste = 0x80091000u;
constexpr std::uint32_t kCelulaDoTeste = 0x80092000u;
constexpr std::uint32_t kCelulaDoTeste2 = 0x80093000u;
constexpr std::uint32_t kPngDoTeste = 0x80094000u;   // os bytes do PNG que o jogo "escreve"
constexpr std::uint32_t kRotinaDeInicio = 0x00000600u;  // codigo de teste do guest
constexpr std::uint32_t kRotinaDaThread = 0x00000640u;

class BancadaDoDespacho {
 public:
  BancadaDoDespacho() {
    unsetenv("ZB2_ENTRADA");
    saidas_.base = 0xF0000000u;
    saidas_.passo = 4;
    saidas_.quantos = 100000;  // o mesmo numero da bateria
    saidas_.ativa = true;
    cpu_.ConfigurarSaidas(saidas_);
    al_ = new Alocador(mem_, kHeap, kHeapTam, nullptr);
    despacho_ = new Despacho(mem_, traco_, *al_, vfs_);
    despacho_->DefinirVtableBitmap(saidas_);
    despacho_->DefinirVtableFicheiro(saidas_.Endereco(kVtableFileObj));
    despacho_->InstalarAjudantes(saidas_, kTabelaDoTeste);
    despacho_->DefinirFaixaDoModulo(0, kTamanhoDoModuloDeTeste);
    cpu_.Repor(0, kPilhaDoTeste);
  }
  ~BancadaDoDespacho() {
    delete despacho_;
    delete al_;
  }
  // Uma chamada do guest a um endereco de saida: poe os registos e corre o laco
  // do despacho ate a sentinela.
  ResultadoFase ChamaSaida(std::uint32_t indice, std::uint32_t r0, std::uint32_t r1 = 0,
                           std::uint32_t r2 = 0, std::uint32_t r3 = 0,
                           std::uint64_t limite = 1000) {
    cpu_.Set(kR0, r0);
    cpu_.Set(kR1, r1);
    cpu_.Set(kR2, r2);
    cpu_.Set(kR3, r3);
    cpu_.Set(kLR, kSentinelaDoTeste);
    cpu_.Set(kPC, saidas_.Endereco(indice));
    return despacho_->Correr(cpu_, limite, kPpObjDoTeste);
  }
  std::size_t Faltas(const std::string& nome) const {
    const auto& f = traco_.ContagemFaltas();
    const auto it = f.find(nome);
    return it == f.end() ? 0 : static_cast<std::size_t>(it->second);
  }
  Memoria& M() { return mem_; }
  Despacho& D() { return *despacho_; }
  ArmInterpreter& Cpu() { return cpu_; }
  const Saidas& S() const { return saidas_; }
  Traco& T() { return traco_; }

  // O codigo do teste, em ARM, no espaco do module.
  void EscreveCodigo(std::uint32_t base, const std::uint32_t* palavras, std::size_t quantas) {
    for (std::size_t k = 0; k < quantas; ++k) mem_.Escrever32(base + static_cast<std::uint32_t>(k * 4),
                                                             palavras[k]);
  }

 private:
  Memoria mem_;
  Traco traco_{"teste_classes_despacho", nullptr};
  Vfs vfs_;
  Alocador* al_ = nullptr;
  Despacho* despacho_ = nullptr;
  Saidas saidas_;
  ArmInterpreter cpu_{mem_, &traco_};
};

TEST(Classes, OIThreadCriaSePorCreateInstanceEStartAtendePeloSlotDaVtable) {
  BancadaDoDespacho b;
  // 1. CreateInstance(po, AEECLSID_THREAD, &ppo) -- o slot 2 do IShell.
  const ResultadoFase r = b.ChamaSaida(kBaseDoShell + 2, kObjShell, 0x01001017u, kPpObjDoTeste);
  (void)r;
  const std::uint32_t obj = b.M().Ler32(kPpObjDoTeste);
  ASSERT_EQ(obj, ObjetoDaClasse(static_cast<std::uint32_t>(Classe::kThread)))
      << "CreateInstance tem de devolver o objecto do IThread";
  ASSERT_EQ(b.M().Ler32(obj), b.S().Endereco(VtClasse(static_cast<std::uint32_t>(Classe::kThread))))
      << "o objecto tem de apontar para a vtable";
  // 2. A CABLAGEM, LIDA DA TABELA: o slot 7 do objecto aponta para o ramo do
  //    Start do despacho, e nao para o stub de recusa que servia tudo.
  const std::uint32_t vt = b.M().Ler32(obj);
  EXPECT_EQ(b.M().Ler32(vt + brew_slots::kThread_Start * 4),
            b.S().Endereco(VtClasse(static_cast<std::uint32_t>(Classe::kThread)) +
                           brew_slots::kThread_Start));
  // 3. A chamada REAL pelo guest: as mesmas leituras que o modulo faz
  //    (`ldr r1,[r0]`; `ldr ip,[r1,#0x1c]`), com o resultado guardado em [r2].
  //      600 e5901000  ldr r1, [r0]
  //      604 e591c01c  ldr ip, [r1, #0x1c]   ; slot 7
  //      608 e1a0e00f  mov lr, pc            ; lr = 0x610
  //      60c e12fff1c  bx ip
  //      610 e5820000  str r0, [r2]          ; o retorno do Start em [r2]
  //      614 e3a00000  mov r0, #0
  //      618 e59fe004  ldr lr, [pc, #4]      ; lr = [0x624] = sentinela
  //      61c e12fff1e  bx lr
  //      620 e1a00000  nop
  //      624 fffffff0                       ; literal da sentinela
  const std::uint32_t words[] = {0xe5901000u, 0xe591c01cu, 0xe1a0e00fu, 0xe12fff1cu,
                                 0xe5820000u, 0xe3a00000u, 0xe59fe004u, 0xe12fff1eu,
                                 0xe1a00000u, kSentinelaDoTeste};
  b.EscreveCodigo(kRotinaDeInicio, words, sizeof(words) / sizeof(words[0]));
  b.M().Escrever32(kCelulaDoTeste, 0xDEADBEEFu);
  b.Cpu().Set(kR0, obj);
  b.Cpu().Set(kR1, 0x4000);        // 16 KiB, o pedido MEDIDO de dez titulos
  b.Cpu().Set(kR2, kCelulaDoTeste);
  b.Cpu().Set(kR3, 0x80200048u);   // pvStart, o valor MEDIDO no zeeboids
  b.Cpu().Set(kLR, kSentinelaDoTeste);
  b.Cpu().Set(kPC, kRotinaDeInicio);
  const ResultadoFase t = b.D().Correr(b.Cpu(), 1000, kPpObjDoTeste);
  EXPECT_EQ(t.motivo, "retornou");
  // O REGISTO DO SUCCESS no guest, e nao o registo R0 (a rotina ja o mudou).
  EXPECT_EQ(b.M().Ler32(kCelulaDoTeste), kAeeSuccess)
      << "o Start tem de responder SUCCESS pelo slot 7 da vtable";
  // A thread ficou INICIADA e PENDENTE -- e a agenda para quem a retomar.
  EXPECT_TRUE(TemThreadPendente());
  EXPECT_EQ(b.Faltas("IThread::Start"), 0u);
}

TEST(Classes, OIThreadCooperaSuspendDevolveOControloEExitEncerra) {
  BancadaDoDespacho b;
  const std::uint32_t th = static_cast<std::uint32_t>(Classe::kThread);
  const std::uint32_t obj = ObjetoDaClasse(th);
  // O Start directo pela tabela (a cablagem ja esta provada no teste acima).
  b.ChamaSaida(VtClasse(th) + brew_slots::kThread_Start, obj, 0x4000, kRotinaDaThread,
               0x80200048u);
  EXPECT_EQ(b.Cpu().Get(kR0), kAeeSuccess);
  EXPECT_TRUE(TemThreadPendente());

  // A FUNCAO DA THREAD, ARM, em kRotinaDaThread:
  //      640 e5901000  ldr r1, [r0]         ; r0 = this
  //      644 e591c028  ldr ip, [r1, #0x28]  ; slot 10 = Suspend
  //      648 e1a0e00f  mov lr, pc           ; lr = 0x650
  //      64c e12fff1c  bx ip                ; Suspend(this)
  //      650 e3a0002a  mov r0, #42          ; o rv da thread
  //      654 e12fff1e  bx lr                ; lr = sentinela (posto na retomada)
  const std::uint32_t f[] = {0xe5901000u, 0xe591c028u, 0xe1a0e00fu, 0xe12fff1cu,
                             0xe3a0002au, 0xe12fff1eu};
  b.EscreveCodigo(kRotinaDaThread, f, sizeof(f) / sizeof(f[0]));

  // O callback de retomada: o mesmo endereco nas duas chamadas, e e por ELE que
  // o `ISHELL_Resume` do despacho reconhece a thread. CUIDADO: o handler
  // escreve o resultado no r0, e o r0 E o `this` da chamada -- e preciso repor
  // o objecto antes da segunda chamada (o titulo tambem o faz).
  b.Cpu().Set(kR0, obj);
  EXPECT_TRUE(AtenderClasse(b.Cpu(), VtClasse(th) + brew_slots::kThread_GetResumeCBK, b.T()));
  const std::uint32_t cbk = b.Cpu().Get(kR0);
  EXPECT_NE(cbk, 0u);
  b.Cpu().Set(kR0, obj);
  EXPECT_TRUE(AtenderClasse(b.Cpu(), VtClasse(th) + brew_slots::kThread_GetResumeCBK, b.T()));
  EXPECT_EQ(b.Cpu().Get(kR0), cbk);
  EXPECT_TRUE(EnfileirarThreadPeloCallbackDeRetomada(cbk));
  EXPECT_FALSE(EnfileirarThreadPeloCallbackDeRetomada(cbk + 4));

  // 1. A retomada: o despacho (aqui, o teste) corre a thread ate ela ceder.
  EXPECT_TRUE(PrepararRetomadaDeThread(b.Cpu(), b.T()));
  EXPECT_EQ(b.Cpu().Get(kPC), kRotinaDaThread);
  EXPECT_EQ(b.Cpu().Get(kLR), kSentinelaDoTeste);
  const ResultadoFase p1 = b.D().Correr(b.Cpu(), 1000, kPpObjDoTeste);
  EXPECT_EQ(p1.motivo, "retornou") << "o Suspend devolve o controlo ao hospedeiro";
  ConcluirRetomadaDeThread(b.Cpu(), b.T());
  // A thread ficou no SUSPEND: ainda nao terminou. O `Join` DECLARA o juntador
  // (void, nao escreve r0 -- como o cabecalho manda); a prova de que o juntador
  // trabalhou e o rv escrito no pnRv quando a thread terminar, la em baixo.
  b.Cpu().Set(kR0, obj);
  b.Cpu().Set(kR1, 0);              // pcb = 0: sem callback a entregar
  b.Cpu().Set(kR2, kCelulaDoTeste2);  // pnRv
  EXPECT_TRUE(AtenderClasse(b.Cpu(), VtClasse(th) + brew_slots::kThread_Join, b.T()));

  // 2. A segunda retomada (o jogo pediu-a pelo Resume do callback): continua NA
  //    INSTRUCAO A SEGUIR ao Suspend.
  EXPECT_TRUE(EnfileirarThreadPeloCallbackDeRetomada(cbk));
  EXPECT_TRUE(PrepararRetomadaDeThread(b.Cpu(), b.T()));
  EXPECT_EQ(b.Cpu().Get(kPC), kRotinaDaThread + 0x10);
  const ResultadoFase p2 = b.D().Correr(b.Cpu(), 1000, kPpObjDoTeste);
  EXPECT_EQ(p2.motivo, "retornou");
  ConcluirRetomadaDeThread(b.Cpu(), b.T());
  EXPECT_FALSE(TemThreadPendente());
  // O retorno da funcao de entrada TERMINA a thread (zeebx resume_thread): o
  // juntador recebe o rv no pnRv.
  EXPECT_EQ(b.M().Ler32(kCelulaDoTeste2), 42u);
}

TEST(Classes, OIThreadStartSegundaVezDaAlreadyEExitNaoIniciadaDaFailed) {
  BancadaDoDespacho b;
  const std::uint32_t th = static_cast<std::uint32_t>(Classe::kThread);
  const std::uint32_t obj = ObjetoDaClasse(th);
  // Primeiro Start: SUCCESS.
  b.ChamaSaida(VtClasse(th) + brew_slots::kThread_Start, obj, 0x4000, kRotinaDaThread, 0);
  EXPECT_EQ(b.Cpu().Get(kR0), kAeeSuccess);
  // Segundo Start no mesmo objecto: EALREADY (AEE_EALREADY = 26, AEEStdErr.h:26
  // -- "IThreads are not re-useable", AEEThread.h:113).
  b.ChamaSaida(VtClasse(th) + brew_slots::kThread_Start, obj, 0x4000, kRotinaDaThread, 0);
  EXPECT_EQ(b.Cpu().Get(kR0), kAeeAlready);
  // Exit sem nunca ter iniciado: EFAILED (AEEThread.h: "EFAILED: if the IThread's
  // never been _Start()ed").
  BancadaDoDespacho c;
  c.ChamaSaida(VtClasse(th) + brew_slots::kThread_Exit, ObjetoDaClasse(th), 3);
  EXPECT_EQ(c.Cpu().Get(kR0), kAeeFailed);
}


TEST(Classes, OPNGDecoderGetBitmapSemFluxoRecusaComONome) {
  Bancada b;
  const std::uint32_t png = static_cast<std::uint32_t>(Classe::kPNGDecoderBREW);
  // 1. Sem ponteiro de saida: BADPARM, e nada escrito (nao ha onde escrever).
  b.Cpu().Set(kR0, ObjetoDaClasse(png));
  EXPECT_TRUE(AtenderClasse(b.Cpu(), VtClasse(png) + brew_slots::kImageDecoder_GetBitmap, b.T()));
  EXPECT_EQ(b.Cpu().Get(kR0), kAeeBadParm);
  EXPECT_EQ(b.Faltas("IImageDecoder::GetBitmap"), 1u);
  // 2. Com ponteiro e SEM imagem: `AEE_EFAILED` (o codigo que o cabecalho
  //    promete), o ponteiro fica a ZERO -- o jogo le-o antes de usar -- e a falta
  //    fica com o MOTIVO (nenhum `Write` antes do `GetBitmap`).
  // Uma SEGUNDA bancada para o caso 2: o `Detalhe` devolve o PRIMEIRO evento com
  // aquele nome, e o da bancada anterior e o do `ppiBitmap` nulo.
  const std::uint32_t saida = 0x80091000u;
  Bancada c;
  c.M().Escrever32(saida, 0xDEADBEEFu);
  c.Cpu().Set(kR0, ObjetoDaClasse(png));
  c.Cpu().Set(kR1, saida);
  EXPECT_TRUE(AtenderClasse(c.Cpu(), VtClasse(png) + brew_slots::kImageDecoder_GetBitmap, c.T()));
  EXPECT_EQ(c.Cpu().Get(kR0), kAeeFailed);
  EXPECT_EQ(c.M().Ler32(saida), 0u);
  EXPECT_EQ(c.Faltas("IImageDecoder::GetBitmap"), 1u);
  EXPECT_NE(c.Detalhe("IImageDecoder::GetBitmap").find("fluxo vazio"), std::string::npos)
      << c.Detalhe("IImageDecoder::GetBitmap");
}

// UM PNG DE 2x2 RGBA, montado com as regras do formato (tamanho e CRC em
// big-endian) e um bloco deflate STORED -- nao ha compressor nesta arvore, e o
// `Adler32`/`Crc32DePng` do proprio motor escrevem-no. O ULTIMO pixel e
// totalmente transparente, para o `tem_alpha` (e o `GetRop`) terem o que medir.
std::vector<std::uint8_t> PngDoTeste() {
  const std::vector<std::uint8_t> cru = {0, 255, 0, 0, 255, 0, 255, 0, 255,
                                         0, 0, 0, 255, 255, 0, 0, 0, 0};
  std::vector<std::uint8_t> zlib_stream = {0x78u, 0x01u, 0x01u};
  const std::uint16_t n = static_cast<std::uint16_t>(cru.size());
  zlib_stream.push_back(static_cast<std::uint8_t>(n & 0xffu));
  zlib_stream.push_back(static_cast<std::uint8_t>((n >> 8) & 0xffu));
  zlib_stream.push_back(static_cast<std::uint8_t>((~n) & 0xffu));
  zlib_stream.push_back(static_cast<std::uint8_t>(((~n) >> 8) & 0xffu));
  zlib_stream.insert(zlib_stream.end(), cru.begin(), cru.end());
  const std::uint32_t adler = Adler32(cru.data(), cru.size());
  for (int i = 3; i >= 0; --i) zlib_stream.push_back(static_cast<std::uint8_t>((adler >> (8 * i)) & 0xffu));

  std::vector<std::uint8_t> v = {0x89u, 0x50u, 0x4eu, 0x47u, 0x0du, 0x0au, 0x1au, 0x0au};
  auto chunk = [&](const char* tipo, const std::vector<std::uint8_t>& dados) {
    const std::uint32_t tam = static_cast<std::uint32_t>(dados.size());
    for (int i = 3; i >= 0; --i) v.push_back(static_cast<std::uint8_t>((tam >> (8 * i)) & 0xffu));
    std::vector<std::uint8_t> com_tipo(tipo, tipo + 4);
    com_tipo.insert(com_tipo.end(), dados.begin(), dados.end());
    v.insert(v.end(), com_tipo.begin(), com_tipo.end());
    const std::uint32_t crc = Crc32DePng(com_tipo.data(), com_tipo.size());
    for (int i = 3; i >= 0; --i) v.push_back(static_cast<std::uint8_t>((crc >> (8 * i)) & 0xffu));
  };
  const std::vector<std::uint8_t> ihdr = {0, 0, 0, 2, 0, 0, 0, 2, 8, 6, 0, 0, 0};
  chunk("IHDR", ihdr);
  chunk("IDAT", zlib_stream);
  chunk("IEND", {});
  return v;
}

TEST(Classes, OPNGDecoderServeAImagemPelaCadeiaDaVtable) {
  BancadaDoDespacho b;
  const std::uint32_t k = static_cast<std::uint32_t>(Classe::kPNGDecoderBREW);
  // 1. CreateInstance(po, AEECLSID_PNGDECODER_BREW, &ppo) -- o slot 2 do IShell.
  b.ChamaSaida(kBaseDoShell + 2, kObjShell, 0x01030766u, kPpObjDoTeste);
  const std::uint32_t obj = b.M().Ler32(kPpObjDoTeste);
  ASSERT_EQ(obj, ObjetoDaClasse(k)) << "CreateInstance tem de devolver o objecto do descodificador";
  const std::uint32_t vt = b.M().Ler32(obj);
  ASSERT_EQ(vt, b.S().Endereco(VtClasse(k))) << "o objecto tem de apontar para a vtable";

  // 2. A CABLAGEM, LIDA DA TABELA (e nao por um id interno): o slot 3
  //    (`GetBitmap`) tem de apontar para o ramo do descodificador, e nao para o
  //    stub que recusava tudo. Era essa a falta medida nos quatro titulos.
  EXPECT_EQ(b.M().Ler32(vt + brew_slots::kImageDecoder_GetBitmap * 4),
            b.S().Endereco(VtClasse(k) + brew_slots::kImageDecoder_GetBitmap));
  EXPECT_EQ(b.M().Ler32(vt + brew_slots::kImageDecoder_GetRop * 4),
            b.S().Endereco(VtClasse(k) + brew_slots::kImageDecoder_GetRop));

  // 3. O `QueryInterface(AEEIID_IForceFeed)` pelo slot 2 da vtable -- o pedido
  //    que os quatro titulos fazem antes de escrever seja o que for.
  b.ChamaSaida(VtClasse(k) + brew_slots::kImageDecoder_QueryInterface, obj, kIidForceFeed,
               kCelulaDoTeste);
  EXPECT_EQ(b.Cpu().Get(kR0), kAeeSuccess);
  const std::uint32_t ff = b.M().Ler32(kCelulaDoTeste);
  ASSERT_EQ(ff, kObjetoForceFeed) << "o IForceFeed e a segunda interface do mesmo objecto";
  // A VTABLE E OUTRA, e esta na TABELA (40520): e o que faz o `Write` chegar ao
  // mesmo objecto por um caminho que o `ConstruirObjeto` cablou.
  EXPECT_EQ(b.M().Ler32(ff), b.S().Endereco(kVtableForceFeed));
  EXPECT_EQ(b.M().Ler32(b.S().Endereco(kVtableForceFeed) + brew_slots::kForceFeed_Write * 4),
            b.S().Endereco(kVtableForceFeed + brew_slots::kForceFeed_Write));

  // 4. O `Write` do PNG, pelo slot 3 do IForceFeed, com o fluxo nos bytes do
  //    teste. `kR0` e a interface de escrita, `kR1` o buffer, `kR2` o tamanho.
  const std::vector<std::uint8_t> png = PngDoTeste();
  b.M().EscreverBloco(kPngDoTeste, png.data(), static_cast<std::uint32_t>(png.size()));
  b.ChamaSaida(kVtableForceFeed + brew_slots::kForceFeed_Write, ff, kPngDoTeste,
               static_cast<std::uint32_t>(png.size()));
  EXPECT_EQ(b.Cpu().Get(kR0), kAeeSuccess);
  EXPECT_EQ(b.Faltas("IForceFeed::Write"), 0u);

  // 5. O `GetBitmap`, pelo slot 3 da vtable do descodificador.
  b.ChamaSaida(VtClasse(k) + brew_slots::kImageDecoder_GetBitmap, obj, kCelulaDoTeste2);
  EXPECT_EQ(b.Cpu().Get(kR0), kAeeSuccess);
  const std::uint32_t bitmap = b.M().Ler32(kCelulaDoTeste2);
  ASSERT_NE(bitmap, 0u);
  EXPECT_EQ(b.Faltas("IImageDecoder::GetBitmap"), 0u);

  // 6. O CABECALHO PUBLICO DO IDIB, campo a campo: e o que o jogo le
  //    (`[r5,#8]`, `[r5,#0x14]`, `[r5,#0x16]` no laco medido do `abd`).
  EXPECT_NE(bitmap, ObjetoDaClasse(k)) << "o bitmap nao e o descodificador";
  EXPECT_LT(bitmap, 0x80051000u) << "o IDIB tem de estar na pagina dos bitmaps";
  EXPECT_GE(bitmap, 0x80050000u);
  EXPECT_EQ(static_cast<std::uint32_t>(b.M().Ler16(bitmap + CamposDoIdib::kCx)), 2u);
  EXPECT_EQ(static_cast<std::uint32_t>(b.M().Ler16(bitmap + CamposDoIdib::kCy)), 2u);
  EXPECT_EQ(static_cast<std::uint32_t>(b.M().Ler16(bitmap + CamposDoIdib::kNPitch)), 4u);
  EXPECT_EQ(static_cast<std::uint32_t>(b.M().Ler8(bitmap + CamposDoIdib::kNDepth)), 16u);
  EXPECT_EQ(static_cast<std::uint32_t>(b.M().Ler8(bitmap + CamposDoIdib::kNColorScheme)),
            static_cast<std::uint32_t>(CamposDoIdib::kEsquemaDeCor565));
  EXPECT_EQ(b.M().Ler32(bitmap + CamposDoIdib::kPPaletteMap), 0u)
      << "o +4 de um IDIB e o pPaletteMap e TEM de ficar nulo (`IDIB_FlushPalette`)";
  EXPECT_EQ(b.M().Ler32(bitmap + CamposDoIdib::kPvt), b.M().Ler32(0x80050300u))
      << "a vtable do IDIB novo e a mesma do bitmap do ecra";

  // 7. OS PIXELS: o `pBmp` do cabecalho, em RGB565, pela ordem do ficheiro.
  const std::uint32_t pbmp = b.M().Ler32(bitmap + CamposDoIdib::kPBmp);
  ASSERT_NE(pbmp, 0u);
  EXPECT_EQ(b.M().Ler16(pbmp + 0), ImagemPng::Rgb565(255, 0, 0));
  EXPECT_EQ(b.M().Ler16(pbmp + 2), ImagemPng::Rgb565(0, 255, 0));
  EXPECT_EQ(b.M().Ler16(pbmp + 4), ImagemPng::Rgb565(0, 0, 255));
  EXPECT_EQ(b.M().Ler16(pbmp + 6), ImagemPng::Rgb565(0, 0, 0));

  // 8. O `GetRop`: a imagem tem um pixel transparente, logo NAO e opaca. O SDK
  //    promete `AEE_RO_COPY` para opacas e TRANSPARENT/BLEND para as outras.
  b.ChamaSaida(VtClasse(k) + brew_slots::kImageDecoder_GetRop, obj);
  EXPECT_EQ(b.Cpu().Get(kR0), 7u) << "AEE_RO_TRANSPARENT (AEERasterOp.h:31)";
  EXPECT_EQ(b.Faltas("IImageDecoder::GetRop"), 0u);

  // 9. O `Reset` limpa o fluxo: o `GetBitmap` seguinte ja nao tem imagem, e diz
  //    isso em vez de servir a antiga.
  b.ChamaSaida(kVtableForceFeed + brew_slots::kForceFeed_Reset, ff);
  b.M().Escrever32(kCelulaDoTeste2, 0xDEADBEEFu);
  b.ChamaSaida(VtClasse(k) + brew_slots::kImageDecoder_GetBitmap, obj, kCelulaDoTeste2);
  EXPECT_EQ(b.Cpu().Get(kR0), kAeeFailed);
  EXPECT_EQ(b.M().Ler32(kCelulaDoTeste2), 0u);
}

TEST(Classes, OForceFeedDoPngAtendeAquiTemVtableEOQiDeVolta) {
  BancadaDoDespacho b;
  const std::uint32_t k = static_cast<std::uint32_t>(Classe::kPNGDecoderBREW);
  const std::uint32_t ff = kObjetoForceFeed;
  // A CABECA, pelo slot 2 do proprio IForceFeed: o `QueryInterface` de volta para
  // o descodificador (as duas interfaces sao o mesmo objecto) e a recusa COM
  // NOME para o que este objecto nao serve.
  b.ChamaSaida(kVtableForceFeed + brew_slots::kForceFeed_QueryInterface, ff, kIidImageDecoder,
               kCelulaDoTeste);
  EXPECT_EQ(b.Cpu().Get(kR0), kAeeSuccess);
  EXPECT_EQ(b.M().Ler32(kCelulaDoTeste), ObjetoDaClasse(k));
  b.ChamaSaida(kVtableForceFeed + brew_slots::kForceFeed_QueryInterface, ff, 0x01001002u,
               kCelulaDoTeste);
  EXPECT_EQ(b.Cpu().Get(kR0), kAeeUnsupported);
  EXPECT_EQ(b.M().Ler32(kCelulaDoTeste), 0u);
  EXPECT_EQ(b.Faltas("IForceFeed::QueryInterface"), 1u);
  // O `AddRef`/`Release` do IForceFeed contam no proprio objecto (a faixa das
  // classes, e nao a dos bitmaps).
  b.ChamaSaida(kVtableForceFeed + brew_slots::kForceFeed_AddRef, ff);
  EXPECT_EQ(b.Cpu().Get(kR0), 2u) << "a contagem nasce a 1 (o `ConstruirObjeto` poe-a)";
  b.ChamaSaida(kVtableForceFeed + brew_slots::kForceFeed_Release, ff);
  EXPECT_EQ(b.Cpu().Get(kR0), 1u);
  // E a recusa do `GetRop` sem imagem nenhuma leva o MOTIVO (o COPY e o valor de
  // uma imagem opaca, e nao se marca nada como servido).
  b.ChamaSaida(VtClasse(k) + brew_slots::kImageDecoder_GetRop, ObjetoDaClasse(k));
  EXPECT_EQ(b.Cpu().Get(kR0), 2u) << "AEE_RO_COPY";
  EXPECT_EQ(b.Faltas("IImageDecoder::GetRop"), 1u);
}

}  // namespace
}  // namespace zb2::brew

namespace zb2::brew {
namespace {

// ---------------------------------------------------------------------------
// 8. AS SETE EXTENSOES QUALCOMM DO QEGL (frente qualcomm).
//
// MEDIDO na bateria (corrida /tmp/corrida_thrd.json, 14 titulos): cninja,
// spinmast, strhoop, supbtime, karnovr, wizdfire, magdrop3, darkseal,
// baddudes, hbarrel, gof, pbc, ridgeracer e rmp pedem, no `QueryInterface` do
// objecto da CLASSE QEGL (slot 2), SETE IIDs de extensao -- 1x cada. O wrapper
// do SDK que os titulos trazem dentro do `.mod` (`GLES_ext.c`) faz estes
// pedidos no primeiro `eglGetProcAddress`, e GUARDA o objecto que receber;
// as funcoes de extensao (`eglGetColorBufferQUALCOMM`, `eglSetSurfaceScaleQUALCOMM`,
// ...) chamam depois os SLOTS desses objectos.
//
// A CONTAGEM DE SLOTS VEM DOS CABECALHOS DO SDK 4.0.2, e cada lista abaixo
// cita o ficheiro e o numero da linha do `INHERIT_*` onde o metodo aparece (a
// mesma regra do `gerar_slots.py`; estes cabecalhos nao estao na lista dele,
// e por isso a lista e escrita aqui COM a linha ao lado).
// ---------------------------------------------------------------------------
TEST(Classes, OQeglAtendeAsSeteExtensoesQualcommComObjetoDoTamanhoReal) {
  struct Esperado {
    std::uint32_t iid, objeto, vt, slots;
  };
  const Esperado es[] = {
      // AEEEGLGetColorBuffer.h:20 (4 = 3 da cabeca + GetColorBuffer)
      {kIidEglGetColorBuffer, kObjetoEglGetColorBuffer, kVtableEglGetColorBuffer,
       kEglGetColorBufferSlots},
      // AEEEGLSurfaceManip.h:23/252 (V2 = V1 + 10, o mesmo prefixo de vtable)
      {kIidEglSurfaceManip, kObjetoEglSurfaceManip, kVtableEglSurfaceManip,
       kEglSurfaceManipSlots},
      {kIidEglSurfaceManipV1, kObjetoEglSurfaceManip, kVtableEglSurfaceManip,
       kEglSurfaceManipSlots},
      // AEEGLESImageonEXT.h:23/204
      {kIidGlesImageonExt, kObjetoGlesImageonExt, kVtableGlesImageonExt,
       kGlesImageonExtSlots},
      {kIidGlesImageonExtV1, kObjetoGlesImageonExt, kVtableGlesImageonExt,
       kGlesImageonExtSlots},
      // AEEGLES10Ext.h:22 (4 slots: 3 + QueryMatrixxOES)
      {kIidGles10Ext, kObjetoGles10Ext, kVtableGles10Ext, kGles10ExtSlots},
      // AEEGLES11ExtPak.h:22 (30 slots)
      {kIidGles11ExtPak, kObjetoGles11ExtPak, kVtableGles11ExtPak, kGles11ExtPakSlots},
      // AEEEGLOESSwapInterval.h:22 (5 slots)
      {kIidEglOesSwapInterval, kObjetoEglOesSwapInterval,
       kVtableEglOesSwapInterval, kEglOesSwapIntervalSlots},
      // AEEEGLGetPowerLevel.h:20 (4 slots)
      {kIidEglGetPowerLevel, kObjetoEglGetPowerLevel, kVtableEglGetPowerLevel,
       kEglGetPowerLevelSlots},
  };
  const std::uint32_t q = static_cast<std::uint32_t>(Classe::kQEGL);
  Bancada b;
  for (const Esperado& e : es) {
    b.Cpu().Set(kR0, ObjetoDaClasse(q));
    b.Cpu().Set(kR1, e.iid);
    b.Cpu().Set(kR2, 0x80100000u);
    EXPECT_TRUE(AtenderClasse(b.Cpu(), VtClasse(q) + 2, b.T()));
    EXPECT_EQ(b.Cpu().Get(kR0), kAeeSuccess) << "iid=0x" << std::hex << e.iid;
    EXPECT_EQ(b.M().Ler32(0x80100000u), e.objeto) << "iid=0x" << std::hex << e.iid;
    // O objecto aponta para a vtable, e a vtable TEM O TAMANHO REAL DO SDK:
    // o primeiro indice ALEM da tabela nao pertence a esta faixa (o
    // `AtenderClasse` devolve false em vez de servir um slot que nao existe).
    EXPECT_EQ(b.M().Ler32(e.objeto), b.S().Endereco(e.vt));
    EXPECT_FALSE(AtenderClasse(b.Cpu(), e.vt + e.slots, b.T()));
  }
  // Nenhuma recusa pelo caminho: os sete atendem sem falta.
  EXPECT_EQ(b.Faltas("QEGL::QueryInterface"), 0u);
}

TEST(Classes, OGetColorBufferEntregaOBufferDoEcra) {
  Bancada b;
  // int GetColorBuffer(void **ret) -- slot 3 de AEEEGLGetColorBuffer.h:25.
  b.Cpu().Set(kR0, kObjetoEglGetColorBuffer);
  b.Cpu().Set(kR1, 0x80100000u);
  EXPECT_TRUE(AtenderClasse(b.Cpu(), kVtableEglGetColorBuffer + 3, b.T()));
  EXPECT_EQ(b.Cpu().Get(kR0), kAeeSuccess);
  // E O BUFFER DE COR DO ECRA VISTO PELO GUEST (ecra.h): RGB565, 640x480. O
  // titulo ganha a superficie a que o seu desenho vai parar.
  EXPECT_EQ(b.M().Ler32(0x80100000u), kBaseDoEcraNoGuest);
  // Ponteiro de saida nulo: EBADPARM, como o resto das cabecas.
  b.Cpu().Set(kR1, 0);
  EXPECT_TRUE(AtenderClasse(b.Cpu(), kVtableEglGetColorBuffer + 3, b.T()));
  EXPECT_EQ(b.Cpu().Get(kR0), kAeeBadParm);
  // O resto da interface REcusa com o nome (nao ha "sucesso sem efeito").
}

TEST(Classes, AsExtensoesQualcommRecusamComONomeOQueNaoServem) {
  Bancada b;
  struct Pedido {
    std::uint32_t vt, slot, objeto;
    const char* chave;
  };
  const Pedido pedidos[] = {
      // IGLES10Ext::QueryMatrixxOES (AEEGLES10Ext.h:27)
      {kVtableGles10Ext, 3, kObjetoGles10Ext, "IGLES10Ext::QueryMatrixxOES"},
      // IGLES11ExtPak::BlendEquation (AEEGLES11ExtPak.h:35)
      {kVtableGles11ExtPak, 12, kObjetoGles11ExtPak, "IGLES11ExtPak::BlendEquation"},
      // IGLES11ExtPak::GenFramebuffersOES (AEEGLES11ExtPak.h:46)
      {kVtableGles11ExtPak, 23, kObjetoGles11ExtPak, "IGLES11ExtPak::GenFramebuffersOES"},
      // IEGLGetPowerLevel::GetPowerLevel (AEEEGLGetPowerLevel.h:24)
      {kVtableEglGetPowerLevel, 3, kObjetoEglGetPowerLevel, "IEGLGetPowerLevel::GetPowerLevel"},
      // IEGLOESSwapInterval::SwapInterval (AEEEGLOESSwapInterval.h:26)
      {kVtableEglOesSwapInterval, 3, kObjetoEglOesSwapInterval, "IEGLOESSwapInterval::SwapInterval"},
      // IGLESImageonExt::TexEnvi (AEEGLESImageonEXT.h:41)
      {kVtableGlesImageonExt, 17, kObjetoGlesImageonExt, "IGLESImageonExt::TexEnvi"},
      // IGLESImageonExt::BufferDataQUALCOMM (AEEGLESImageonEXT.h:34)
      {kVtableGlesImageonExt, 10, kObjetoGlesImageonExt, "IGLESImageonExt::BufferDataQUALCOMM"},
      // IEGLSurfaceManip::SetSurfaceScale (AEEEGLSurfaceManip.h:28)
      {kVtableEglSurfaceManip, 4, kObjetoEglSurfaceManip, "IEGLSurfaceManip::SetSurfaceScale"},
  };
  for (const Pedido& p : pedidos) {
    b.Cpu().Set(kR0, p.objeto);
    EXPECT_TRUE(AtenderClasse(b.Cpu(), p.vt + p.slot, b.T()));
    EXPECT_EQ(b.Cpu().Get(kR0), kAeeUnsupported) << p.chave;
    EXPECT_EQ(b.Faltas(p.chave), 1u) << p.chave;
  }
}

TEST(Classes, AsCabecasDasExtensoesQualcommTemContagemEOQiASiMesmo) {
  Bancada b;
  // O `ConstruirObjeto` nasce com a contagem a 1: o AddRef devolve 2, e nao 1.
  b.Cpu().Set(kR0, kObjetoEglSurfaceManip);
  EXPECT_TRUE(AtenderClasse(b.Cpu(), kVtableEglSurfaceManip + 0, b.T()));
  EXPECT_EQ(b.Cpu().Get(kR0), 2u);
  b.Cpu().Set(kR0, kObjetoEglSurfaceManip);
  EXPECT_TRUE(AtenderClasse(b.Cpu(), kVtableEglSurfaceManip + 0, b.T()));
  EXPECT_EQ(b.Cpu().Get(kR0), 3u);
  // Release: devolve a contagem restante.
  b.Cpu().Set(kR0, kObjetoEglSurfaceManip);
  EXPECT_TRUE(AtenderClasse(b.Cpu(), kVtableEglSurfaceManip + 1, b.T()));
  EXPECT_EQ(b.Cpu().Get(kR0), 2u);
  // QueryInterface a si proprio: as duas IIDs do par respondem o mesmo objecto.
  for (std::uint32_t iid : {kIidEglSurfaceManip, kIidEglSurfaceManipV1}) {
    b.Cpu().Set(kR0, kObjetoEglSurfaceManip);
    b.Cpu().Set(kR1, iid);
    b.Cpu().Set(kR2, 0x80100000u);
    EXPECT_TRUE(AtenderClasse(b.Cpu(), kVtableEglSurfaceManip + 2, b.T()));
    EXPECT_EQ(b.Cpu().Get(kR0), kAeeSuccess);
    EXPECT_EQ(b.M().Ler32(0x80100000u), kObjetoEglSurfaceManip);
  }
  // E uma IID de fora recusa com o nome da interface.
  b.Cpu().Set(kR0, kObjetoEglSurfaceManip);
  b.Cpu().Set(kR1, 0x12345678u);
  b.Cpu().Set(kR2, 0x80100000u);
  EXPECT_TRUE(AtenderClasse(b.Cpu(), kVtableEglSurfaceManip + 2, b.T()));
  EXPECT_EQ(b.Cpu().Get(kR0), kAeeUnsupported);
  EXPECT_EQ(b.Faltas("IEGLSurfaceManip::QueryInterface"), 1u);
  // Os outros objectos tambem nascem com vtable cablada (leitura de volta).
  EXPECT_EQ(b.M().Ler32(kObjetoEglGetColorBuffer), b.S().Endereco(kVtableEglGetColorBuffer));
  EXPECT_EQ(b.M().Ler32(kObjetoGlesImageonExt), b.S().Endereco(kVtableGlesImageonExt));
  EXPECT_EQ(b.M().Ler32(kObjetoGles10Ext), b.S().Endereco(kVtableGles10Ext));
  EXPECT_EQ(b.M().Ler32(kObjetoGles11ExtPak), b.S().Endereco(kVtableGles11ExtPak));
  EXPECT_EQ(b.M().Ler32(kObjetoEglOesSwapInterval),
            b.S().Endereco(kVtableEglOesSwapInterval));
  EXPECT_EQ(b.M().Ler32(kObjetoEglGetPowerLevel), b.S().Endereco(kVtableEglGetPowerLevel));
}

TEST(Classes, OSurfaceScaleCapsDizAVerdadeSemEscalador) {
  Bancada b;
  // int GetSurfaceScaleCaps(dpy, surf, AEEEGLSurfaceScaleCaps *param, EGLBoolean *ret)
  // -- slot 6 de AEEEGLSurfaceManip.h:30. O struct tem DOZE AEEEGLint pela
  // ordem de AEEEGLTypes.h (Min/MaxXScaleFactor, Min/MaxYScaleFactor,
  // Min/MaxSrcWidth/Height, Min/MaxDstWidth/Height).
  //
  // A VERDADE DESTE MOTOR: nao ha escalador. O factor 1.0 (16.16) nos dois
  // eixos e o unico que existe, e as janelas de origem/destino sao as da tela.
  // pMe=r0, dpy=r1, surf=r2, param=r3, ret no PRIMEIRO lugar da pilha (sp).
  b.Cpu().Set(kR0, kObjetoEglSurfaceManip);
  b.Cpu().Set(kR1, 0);  // dpy
  b.Cpu().Set(kR2, 0);  // surf
  b.Cpu().Set(kR3, 0x80100000u);       // param
  b.Cpu().Set(kSP, 0x80100004u);       // sp aponta para o argumento da pilha
  b.M().Escrever32(0x80100004u, 0x80100100u);  // o argumento: *ret
  b.M().Escrever32(0x80100100u, 0);
  EXPECT_TRUE(AtenderClasse(b.Cpu(), kVtableEglSurfaceManip + 6, b.T()));
  EXPECT_EQ(b.Cpu().Get(kR0), kAeeSuccess);
  const std::uint32_t p = 0x80100000u;
  EXPECT_EQ(b.M().Ler32(p + 0u), 1u << 16);  // MinXScaleFactor
  EXPECT_EQ(b.M().Ler32(p + 4u), 1u << 16);  // MaxXScaleFactor
  EXPECT_EQ(b.M().Ler32(p + 8u), 1u << 16);  // MinYScaleFactor
  EXPECT_EQ(b.M().Ler32(p + 12u), 1u << 16); // MaxYScaleFactor
  EXPECT_EQ(b.M().Ler32(p + 16u), 1u);       // MinSrcWidth
  EXPECT_EQ(b.M().Ler32(p + 20u), kLarguraDoEcra);
  EXPECT_EQ(b.M().Ler32(p + 24u), 1u);       // MinSrcHeight
  EXPECT_EQ(b.M().Ler32(p + 28u), kAlturaDoEcra);
  EXPECT_EQ(b.M().Ler32(p + 32u), 1u);       // MinDstWidth
  EXPECT_EQ(b.M().Ler32(p + 36u), kLarguraDoEcra);
  EXPECT_EQ(b.M().Ler32(p + 40u), 1u);       // MinDstHeight
  EXPECT_EQ(b.M().Ler32(p + 44u), kAlturaDoEcra);
  EXPECT_EQ(b.M().Ler32(0x80100100u), 1u);   // EGL_TRUE no *ret
}

}  // namespace
}  // namespace zb2::brew
