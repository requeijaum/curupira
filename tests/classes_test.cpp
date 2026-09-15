#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "core/brew/ajudantes.h"
#include "core/brew/classes.h"
#include "core/brew/clsids.h"
#include "core/brew/interface.h"
#include "core/cpu/arm_interpreter.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"
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
  EXPECT_EQ(ObjetoDoClsid(0x01011810u), 0u);
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
  for (std::uint32_t s = 2; s < 12; ++s) {
    EXPECT_FALSE(SlotDaClasseImplementado(th, s)) << "slot " << s;
  }
}

TEST(Classes, OThreadStartRecusaComNome) {
  Bancada b;
  const std::uint32_t th = static_cast<std::uint32_t>(Classe::kThread);
  b.Cpu().Set(kR0, ObjetoDaClasse(th));
  EXPECT_TRUE(AtenderClasse(b.Cpu(), VtClasse(th) + 7, b.T()));
  EXPECT_EQ(b.Cpu().Get(kR0), kAeeUnsupported);
  EXPECT_EQ(b.Faltas("IThread::Start"), 1u);
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
  for (std::uint32_t s = 2; s < 5; ++s) {
    EXPECT_FALSE(SlotDaClasseImplementado(png, s)) << "slot " << s;
  }
}

TEST(Classes, OPNGDecoderGetBitmapRecusaComNome) {
  Bancada b;
  const std::uint32_t png =
      static_cast<std::uint32_t>(Classe::kPNGDecoderBREW);
  b.Cpu().Set(kR0, ObjetoDaClasse(png));
  EXPECT_TRUE(AtenderClasse(b.Cpu(), VtClasse(png) + 3, b.T()));
  EXPECT_EQ(b.Cpu().Get(kR0), kAeeUnsupported);
  EXPECT_EQ(b.Faltas("IImageDecoder::GetBitmap"), 1u);
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
  b.Cpu().Set(kR0, ObjetoDaClasse(q));
  EXPECT_TRUE(AtenderClasse(b.Cpu(), VtClasse(q) + 2, b.T()));
  EXPECT_EQ(b.Cpu().Get(kR0), kAeeUnsupported);
  EXPECT_EQ(b.Faltas("QEGL::QueryInterface"), 1u);
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

}  // namespace
}  // namespace zb2::brew
