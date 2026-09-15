#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "core/brew/ajudantes.h"
#include "core/brew/classes.h"
#include "core/brew/despacho.h"
#include "core/brew/ihiddevice.h"
#include "core/cpu/arm_interpreter.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"
#include "tools/brew_slots.inc"

namespace zb2::brew {
namespace {

// ===========================================================================
// A CABLAGEM DA ENTRADA NO DESPACHO.
//
// Os testes de `tests/hid_test.cpp` provam o MODULO. Estes provam a LIGACAO: que
// uma chamada do guest passa pelo laco do despacho, entra no modulo da entrada e
// volta com a resposta do contrato -- e que o callback marcado por um sinal
// chega a correr codigo do titulo.
//
// Sem isto, a etapa 8 teria um modulo certo e um jogo que continua a nao
// responder, que e exatamente o sintoma de partida.
// ===========================================================================
// A BASE DO MODULO E ZERO, e e MEDIDA (tests/mod_base_test.cpp): os literais de um
// `.mod` sao offsets do ficheiro usados como enderecos absolutos.
constexpr std::uint32_t kBase = 0x00000000u;
constexpr std::uint32_t kPilha = 0x80080000u;
constexpr std::uint32_t kHeap = 0x80200000u;
constexpr std::uint32_t kHeapTam = 0x00C00000u;
constexpr std::uint32_t kTabela = 0x80010000u;
constexpr std::uint32_t kBaseDasEntradas = 20000;  // o mesmo numero da bateria
constexpr std::uint32_t kSentinela = 0xFFFFFFF0u;
constexpr std::uint32_t kRotina = 0x00000200u;
constexpr std::uint32_t kMarcador = 0x00000300u;
constexpr std::uint32_t kContexto = 0xC0FFEE00u;
constexpr std::uint32_t kArg0 = 0x80090000u;
// O TAMANHO do modulo do titulo, que a bancada declara (a base e zero).
constexpr std::uint32_t kTamanhoDoModulo = 0x00100000u;  // 1 MiB: o maior .mod do corpus tem 1,17 MB... ver o teste


class Bancada {
 public:
  explicit Bancada(const std::string& guiao = "") {
    if (!guiao.empty()) setenv("ZB2_ENTRADA", guiao.c_str(), 1);
    else unsetenv("ZB2_ENTRADA");
    saidas_.base = 0xF0000000u;
    saidas_.passo = 4;
    saidas_.quantos = 100000;  // o mesmo da bateria
    saidas_.ativa = true;
    cpu_.ConfigurarSaidas(saidas_);
    al_ = new Alocador(mem_, kHeap, kHeapTam, nullptr);
    despacho_ = new Despacho(mem_, traco_, *al_, vfs_);
    despacho_->DefinirVtableBitmap(saidas_);
    despacho_->DefinirVtableFicheiro(saidas_.Endereco(kVtableFileObj));
    despacho_->InstalarAjudantes(saidas_, kTabela);
    // A FAIXA DO MODULO. Sem isto o modulo da entrada recusa TODO o callback, e o
    // teste do callback ficaria vermelho por uma razao que nao e a que ele mede.
    despacho_->DefinirFaixaDoModulo(kBase, kTamanhoDoModulo);
    instalada_ = despacho_->InstalarEntrada(saidas_, kBaseDasEntradas);
    cpu_.Repor(kBase, kPilha);
    unsetenv("ZB2_ENTRADA");
  }
  ~Bancada() {
    delete despacho_;
    delete al_;
  }

  // Uma chamada do guest a um endereco de saida: poe os registos como o AAPCS
  // manda e corre o laco do despacho. O LR e a sentinela, que e o "retorno para
  // o sistema" desta arvore.
  std::uint32_t ChamaSaida(std::uint32_t indice, std::uint32_t r0, std::uint32_t r1 = 0,
                           std::uint32_t r2 = 0, std::uint32_t r3 = 0,
                           std::uint32_t na_pilha = 0, std::uint64_t limite = 1000) {
    cpu_.Set(kR0, r0);
    cpu_.Set(kR1, r1);
    cpu_.Set(kR2, r2);
    cpu_.Set(kR3, r3);
    cpu_.Set(kSP, kArg0);
    mem_.Escrever32(kArg0, na_pilha);
    cpu_.Set(kLR, kSentinela);
    cpu_.Set(kPC, saidas_.Endereco(indice));
    const ResultadoFase r = despacho_->Correr(cpu_, limite, kArg0);
    (void)r;
    return cpu_.Get(kR0);
  }

  std::size_t Faltas(const std::string& nome) const {
    const auto& f = traco_.ContagemFaltas();
    const auto it = f.find(nome);
    return it == f.end() ? 0 : static_cast<std::size_t>(it->second);
  }

  Memoria& Mem() { return mem_; }
  Vfs& AcessoAVfs() { return vfs_; }
  Despacho& D() { return *despacho_; }
  ArmInterpreter& Cpu() { return cpu_; }
  const Saidas& S() const { return saidas_; }
  bool Instalada() const { return instalada_; }
  Traco& Tr() { return traco_; }

 private:
  Memoria mem_;
  Traco traco_{"teste_entrada_despacho", nullptr};
  Vfs vfs_;
  Alocador* al_ = nullptr;
  Despacho* despacho_ = nullptr;
  Saidas saidas_;
  ArmInterpreter cpu_{mem_, &traco_};
  bool instalada_ = false;
};

}  // namespace

TEST(EntradaNoDespacho, AEntradaInstalaSeNaFaixaPedida) {
  Bancada b;
  ASSERT_TRUE(b.Instalada());
  EXPECT_TRUE(b.D().EntradaPronta());
  EXPECT_EQ(b.D().BaseDaEntrada(), kBaseDasEntradas);
  // AS ENTRADAS DA VTABLE TEM DE CAIR DENTRO DA FAIXA DE SAIDA. Um endereco de
  // saida fora dela nunca e reconhecido pelo laco, e o modulo atenderia zero
  // chamadas EM SILENCIO -- que e a forma exata de um stub mudo. O objecto em si
  // vive fora da faixa (e um objecto de dados, em 0x810xx000); o que tem de estar
  // la dentro e aquilo para onde os slots apontam.
  const std::uint32_t inicio = b.S().base;
  const std::uint32_t fim = b.S().base + b.S().quantos * b.S().passo;
  for (std::uint32_t slot = 3; slot <= 7; ++slot) {
    const std::uint32_t po = b.Mem().Ler32(b.Mem().Ler32(b.D().IhidRef().EnderecoDoIhid()) +
                                           slot * 4);
    EXPECT_GE(po, inicio) << "slot " << slot << " do IHID";
    EXPECT_LT(po, fim) << "slot " << slot << " do IHID";
  }
  for (std::uint32_t slot = 3; slot <= brew_slots::kHIDDevice_GetRumbleStatus; ++slot) {
    const std::uint32_t po =
        b.Mem().Ler32(b.Mem().Ler32(b.D().IhidRef().EnderecoDoDispositivo()) + slot * 4);
    EXPECT_GE(po, inicio) << "slot " << slot << " do IHIDDevice";
    EXPECT_LT(po, fim) << "slot " << slot << " do IHIDDevice";
  }
  // A base do IHID vem DEPOIS da faixa dos sinais: as duas metades nao se podem
  // sobrepor, ou um slot de uma interface seria atendido pela outra.
  EXPECT_EQ(b.D().BaseDosSinais(), kBaseDasEntradas);
  EXPECT_EQ(b.D().BaseDoIhid(), kBaseDasEntradas + Sinais::kSlotsNecessarios);
}

TEST(EntradaNoDespacho, OIHIDVemDoCreateInstanceComoObjectoASerio) {
  Bancada b;
  ASSERT_TRUE(b.Instalada());
  constexpr std::uint32_t kPPo = 0x00090010u;
  b.Mem().Escrever32(kPPo, 0);
  // IShell::CreateInstance(po, ClsId, ppobj) -- o slot 2 do IShell.
  EXPECT_EQ(b.ChamaSaida(2000 + brew_slots::kShell_CreateInstance, 0x80020000u, kClsidHid, kPPo),
            kAeeSuccess);
  EXPECT_EQ(b.Mem().Ler32(kPPo), b.D().IhidRef().EnderecoDoIhid())
      << "o AEECLSID_HID tem de dar o NOSSO IHID, e nao o objecto generico";
  // E a fabrica de sinais, sem a qual o jogo nao tem o par (funcao, contexto).
  b.Mem().Escrever32(kPPo, 0);
  EXPECT_EQ(b.ChamaSaida(2000 + brew_slots::kShell_CreateInstance, 0x80020000u,
                         kClsidSignalCBFactory, kPPo),
            kAeeSuccess);
  EXPECT_EQ(b.Mem().Ler32(kPPo), b.D().SinaisRef().EnderecoDaFabrica());
}

TEST(EntradaNoDespacho, GetConnectedDevicesChegaAoModuloERespondeOContrato) {
  // O DEFEITO QUE ISTO FIXA, medido: aqui estava um stub que devolvia `14` no r0
  // (o `GetNumberOfButtons` do IHIDDevice cablado no slot 7 do IHID). O sample do
  // SDK faz `if (iReturn != SUCCESS) return FALSE;` -- logo um jogo que pedisse a
  // lista de aparelhos desistia ALI, antes de chegar ao modulo da entrada.
  Bancada b;
  ASSERT_TRUE(b.Instalada());
  constexpr std::uint32_t kHandles = 0x0020a000u, kReq = 0x0020a100u;
  b.Mem().Escrever32(kReq, 0xFFFFFFFFu);
  const std::uint32_t indice = b.D().BaseDoIhid() + Ihid::kBaseDoIhid + kIHID_GetConnectedDevices;
  EXPECT_EQ(b.ChamaSaida(indice, b.D().IhidRef().EnderecoDoIhid(), 0x0106c3fdu, kHandles, 2, kReq),
            kAeeSuccess);
  EXPECT_EQ(b.Mem().Ler32(kReq), 1u) << "a contagem vai no 5.o argumento, nao no r0";
  EXPECT_EQ(b.Mem().Ler32(kHandles), 1u);
}

TEST(EntradaNoDespacho, UmSlotNaoImplementadoRecusaERegistra) {
  Bancada b;
  ASSERT_TRUE(b.Instalada());
  const std::uint32_t indice =
      b.D().BaseDoIhid() + Ihid::kBaseDoDispositivo + brew_slots::kHIDDevice_Rumble;
  EXPECT_EQ(b.ChamaSaida(indice, b.D().IhidRef().EnderecoDoDispositivo(), 65535, 65535),
            kAeeUnsupported);
  EXPECT_EQ(kAeeUnsupported, 20) << "AEE_EUNSUPPORTED e 20 (AEEStdErr.h:36)";
  EXPECT_EQ(b.Faltas("IHIDDevice::Rumble"), 1u);
}

TEST(EntradaNoDespacho, OCallbackDeTemporizadorForaDoModuloNaoSeChama) {
  // O CAMINHO DO LACO DE QUADRO, e a guarda dele: um callback de temporizador que
  // aponte para fora da faixa do modulo nao se chama. E a mesma guarda do lado
  // dos sinais (em `Sinais::PrepararProximoCallback`), do lado do temporizador.
  Bancada b;
  ASSERT_TRUE(b.Instalada());
  // 1) uma funcao DENTRO do modulo
  b.Cpu().Set(kSP, kArg0);
  // 1520 e o indice de saida do `IShell::SetTimer` na cablagem da bateria
  // (`kSlotIdSetTimer`). O slot 11 do IShell e o `SetTimer` pelo cabecalho, mas o
  // despacho atende-o por INDICE, e a cablagem da vtable e que decide o numero.
  //
  // OS ARGUMENTOS SAO OS DO CABECALHO: `int (*SetTimer)(iname *po, int32 dwMsecs,
  // void (*pfn)(void *), void *pUser)` (`AEEIShell.h:299`). O r1 e a DURACAO (1
  // ms), o r2 a FUNCAO e o r3 o CONTEXTO -- e nao um `AEECallback*` no r1. Este
  // teste escrevia o par num `AEECallback` na memoria e passava-o no r1, que era
  // a assinatura errada do despacho; a medicao que a corrigiu esta em
  // `core/brew/despacho.h` (`struct Temporizador`) e em `tests/widget_test.cpp`.
  ASSERT_EQ(b.ChamaSaida(1520u, 0x80020000u, 1u, kRotina, kContexto), kAeeSuccess);
  ASSERT_TRUE(b.D().TemporizadorArmado());
  EXPECT_TRUE(b.D().PrepararCallbackDoTemporizador(b.Cpu()));
  EXPECT_EQ(b.Cpu().Get(kPC), kRotina);
  EXPECT_EQ(b.Cpu().Get(kR0), kContexto);
  EXPECT_EQ(b.Cpu().Get(kLR), kSentinela);
  EXPECT_FALSE(b.D().TemporizadorArmado()) << "o callback re-arma o seguinte; o laco e do titulo";

  // 2) uma funcao FORA do modulo: recusa e REGISTA
  ASSERT_EQ(b.ChamaSaida(1520u, 0x80020000u, 1u, 0x50000000u, kContexto), kAeeSuccess);
  EXPECT_FALSE(b.D().PrepararCallbackDoTemporizador(b.Cpu()));
  EXPECT_EQ(b.Faltas("callback_de_temporizador"), 1u);
}

TEST(EntradaNoDespacho, OUidErradoNoPoDeUmSlotERecusado) {
  // A guarda do `po`: uma chamada a um slot do IHIDDevice com o po do IHID (ou
  // com lixo) nao pode ser atendida como se fosse um objecto nosso.
  Bancada b;
  ASSERT_TRUE(b.Instalada());
  const std::uint32_t indice = b.D().BaseDoIhid() + Ihid::kBaseDoDispositivo +
                               brew_slots::kHIDDevice_GetNumberOfButtons;
  EXPECT_EQ(b.ChamaSaida(indice, 0x12345678u, 0x0020b000u), kAeeBadParm);
  EXPECT_EQ(b.Faltas("IHIDDevice::slot"), 1u);
}

TEST(EntradaNoDespacho, OCallbackDoGuiaoChegaACorrerCodigoDoTitulo) {
  // END TO END, pela cablagem: o guiao entra pelo `ZB2_ENTRADA`, o relogio
  // virtual avanca no laco do despacho, a mudanca de eixo marca o sinal
  // registado, e o CALLBACK DO TITULO corre e escreve.
  Bancada b("5 eixo 0x0106c4d0 0");  // 0 = batente, contra o repouso 128
  ASSERT_TRUE(b.Instalada());
  ASSERT_EQ(b.D().Entrada().Quantos(), 1u) << "o guiao do ambiente nao foi lido";

  // O codigo do titulo: guarda o r0 (o CONTEXTO) num marcador e volta.
  //   00100200  ldr r1, [pc, #4]
  //   00100204  str r0, [r1]
  //   00100208  bx  lr
  //   00100210  .word marcador
  b.Mem().Escrever32(kRotina + 0, 0xE59F1004u);
  b.Mem().Escrever32(kRotina + 4, 0xE5810000u);
  b.Mem().Escrever32(kRotina + 8, 0xE12FFF1Eu);
  b.Mem().Escrever32(kRotina + 12, kMarcador);
  b.Mem().Escrever32(kMarcador, 0);

  // 1) o jogo cria o sinal pela fabrica, que vem do CreateInstance
  constexpr std::uint32_t kPPo = 0x00090010u, kSaida = 0x00202000u;
  b.Mem().Escrever32(kPPo, 0);
  ASSERT_EQ(b.ChamaSaida(2000 + brew_slots::kShell_CreateInstance, 0x80020000u,
                         kClsidSignalCBFactory, kPPo),
            kAeeSuccess);
  const std::uint32_t fabrica = b.Mem().Ler32(kPPo);
  ASSERT_NE(fabrica, 0u);
  b.Mem().Escrever32(kSaida, 0);
  b.Mem().Escrever32(kSaida + 4, 0);
  ASSERT_EQ(b.ChamaSaida(b.D().BaseDosSinais() + Sinais::kBaseDaFabrica +
                             kISignalCBFactory_CreateSignal,
                         fabrica, kRotina, kContexto, kSaida, kSaida + 4),
            kAeeSuccess);
  const std::uint32_t sinal = b.Mem().Ler32(kSaida + 4);
  ASSERT_NE(sinal, 0u);

  // 2) o jogo registra-se para a MUDANCA DE POSICAO -- o slot 14, o unico que a
  //    Z-Wheel chama (medido com ZEEB_LOG_HID_SLOT=1)
  ASSERT_EQ(b.ChamaSaida(b.D().BaseDoIhid() + Ihid::kBaseDoDispositivo +
                             brew_slots::kHIDDevice_RegisterForPositionChange,
                         b.D().IhidRef().EnderecoDoDispositivo(), sinal),
            kAeeSuccess);

  // 3) o titulo fica a girar no proprio codigo, e o laco do despacho corre
  b.Mem().Escrever32(kBase, 0xEAFFFFFEu);  // b . -- um laco que nao avanca
  b.Cpu().Repor(kBase, kPilha);
  const ResultadoFase r = b.D().Correr(b.Cpu(), 200, 0);
  EXPECT_EQ(r.motivo, "retornou") << "o callback correu e voltou pela sentinela";
  EXPECT_EQ(b.Mem().Ler32(kMarcador), kContexto)
      << "o callback do sinal de posicao nao chegou a correr codigo do titulo";
  EXPECT_EQ(b.D().IhidRef().SinalizacoesDePosicao(), 1u);
}


// O RELEASE DO IBitmap DO ECRA TEM DE TER ENDERECO.
//
// MEDIDO (traco por instrucao, ZB2_PCTRACE, corrida de 2026-09 na base ZERO):
//   `abd` (279369) em 0x14694-0x1469c faz
//       ldr r0,[sp]      ; r0 = 0x80050300 (o IBitmap do ecra)
//       ldr r1,[r0]      ; r1 = 0xF0007D00 (a vtable do bitmap)
//       ldr r1,[r1,#4]   ; r1 = 0            <-- O SLOT 1 (Release) ESTA A ZERO
//       blx r1           ; salta para o ENDERECO 0
//   e a partir dai executa o cabecalho do proprio `.mod` como codigo. O `push`
//   de lixo em 0x0 desloca o `sp` em 0x10 e, de volta em 0x146a0, o titulo le
//   0xF0027390 (o r3 derramado) como se fosse um objecto -- e acaba a chamar
//   `QEGL::eglInitialize(dpy=0xF0027390)`, que o `core/brew/egl.cpp` recusa.
//   **A recusa do EGL era o SINTOMA; a causa e este slot a zero.**
//
// `torkandkral` (280463) faz o mesmo em 0x135c8-0x135d4.
TEST(VtableDoBitmap, OReleaseDoBitmapDoEcraTemEndereco) {
  Bancada b;
  const std::uint32_t saida = 0x80090100u;
  b.Mem().Escrever32(saida, 0);
  // `IDisplay::GetDeviceBitmap(po, ppIBitmap)` -- o mesmo endereco de saida que
  // a `tools/bateria.cpp` cabla no slot 16 do IDisplay.
  b.ChamaSaida(1550, kObjDisplay, saida);
  const std::uint32_t bmp = b.Mem().Ler32(saida);
  ASSERT_EQ(bmp, kObjDibBase + 0x300);
  const std::uint32_t vt = b.Mem().Ler32(bmp);
  ASSERT_EQ(vt, b.S().Endereco(kVtableBitmap));
  EXPECT_NE(b.Mem().Ler32(vt + 0 * 4), 0u) << "IBitmap::AddRef sem endereco: o guest faz blx 0";
  EXPECT_NE(b.Mem().Ler32(vt + 1 * 4), 0u) << "IBitmap::Release sem endereco: o guest faz blx 0";
  EXPECT_NE(b.Mem().Ler32(vt + 2 * 4), 0u) << "IBitmap::QueryInterface sem endereco";
}

// A PROVA DE COMPORTAMENTO, e nao so de conteudo: o guest corre mesmo a
// sequencia do `abd` e o PC NAO pode chegar a zero.
TEST(VtableDoBitmap, OGuestReleaseOBitmapSemSaltarParaZero) {
  Bancada b;
  const std::uint32_t saida = 0x80090100u;
  b.ChamaSaida(1550, kObjDisplay, saida);
  const std::uint32_t bmp = b.Mem().Ler32(saida);
  ASSERT_NE(bmp, 0u);
  // A CONTAGEM NAO ESTA NO `+4` DO OBJECTO quando o objecto e um IDIB: esse
  // campo e o `pPaletteMap` (`AEEIDIB.h:44`), um ponteiro publico que o
  // `IDIB_FlushPalette` (`:83-86`) desreferencia. Tem de ficar NULO.
  EXPECT_EQ(b.Mem().Ler32(bmp + 4), 0u) << "pPaletteMap tem de ser um ponteiro nulo";
  EXPECT_EQ(b.D().ReferenciasDoBitmap(bmp), 1u) << "a contagem de referencias nasce a 1";
  // A MESMA sequencia de `abd` 0x14694: ldr r1,[r0]; ldr r1,[r1,#4]; blx r1
  const std::uint32_t kCodigo = 0x00000400u;
  b.Mem().Escrever32(kCodigo + 0, 0xE5901000u);  // ldr r1, [r0]
  b.Mem().Escrever32(kCodigo + 4, 0xE5911004u);  // ldr r1, [r1, #4]
  b.Mem().Escrever32(kCodigo + 8, 0xE12FFF31u);  // blx r1
  b.Cpu().Set(kR0, bmp);
  b.Cpu().Set(kSP, 0x80090000u);
  b.Cpu().Set(kLR, 0xFFFFFFF0u);
  b.Cpu().Set(kPC, kCodigo);
  b.D().Correr(b.Cpu(), 200, 0x80090000u);
  EXPECT_EQ(b.D().ReferenciasDoBitmap(bmp), 0u)
      << "o Release da IBase tinha de baixar a contagem de 1 para 0";
  EXPECT_EQ(b.Mem().Ler32(bmp + 4), 0u) << "e o pPaletteMap continua nulo";
}


TEST(ClipRect, OGetClipRectEscreveOitoBytesENaoDezasseis) {
  // `void IDISPLAY_GetClipRect(IDisplay*, AEERect*)` -- IDisplay slot 19.
  //
  // O AEERect sao 4x int16 = OITO bytes (`AEERect.h:21-24`). Escreviam-se
  // DEZASSEIS, e os oito a mais caiam por cima do que estivesse a seguir --
  // e o `AEERect` do guest esta quase sempre na PILHA, logo por cima das suas
  // proprias variaveis locais. Nada falhava aqui: falhava mais tarde, noutro
  // sitio, sem ligacao visivel a esta chamada.
  //
  // O `SetClipRect`, no mesmo ficheiro, JA lia int16 desde que isso foi medido
  // (`despacho.cpp`, "AEERect sao 4x int16 ... nao 4x u32"). Era metade do par.
  Bancada b;
  constexpr std::uint32_t kRect = 0x80210000u;
  constexpr std::uint16_t kGuarda = 0xBEEFu;

  // A SENTINELA: as quatro meias-palavras LOGO A SEGUIR ao AEERect. Se o
  // despacho escrever 16 bytes, come-as -- e e isso que este teste apanha.
  for (std::uint32_t k = 0; k < 8; k += 2) b.Mem().Escrever16(kRect + 8 + k, kGuarda);

  // Poe um clip conhecido pelo caminho normal (SetClipRect, 4x int16).
  b.Mem().Escrever16(kRect + 0, 10);
  b.Mem().Escrever16(kRect + 2, 20);
  b.Mem().Escrever16(kRect + 4, 100);
  b.Mem().Escrever16(kRect + 6, 50);
  b.ChamaSaida(1536, kObjDisplay, kRect);  // kSlotIdSetClipRect

  // Suja o rectangulo, para que a leitura de volta prove que escreveu mesmo.
  for (std::uint32_t k = 0; k < 8; k += 2) b.Mem().Escrever16(kRect + k, 0x5A5Au);
  b.ChamaSaida(1551, kObjDisplay, kRect);  // kSlotIdGetClipRect

  EXPECT_EQ(static_cast<std::int16_t>(b.Mem().Ler16(kRect + 0)), 10);
  EXPECT_EQ(static_cast<std::int16_t>(b.Mem().Ler16(kRect + 2)), 20);
  EXPECT_EQ(static_cast<std::int16_t>(b.Mem().Ler16(kRect + 4)), 100);
  EXPECT_EQ(static_cast<std::int16_t>(b.Mem().Ler16(kRect + 6)), 50);

  // E OS OITO BYTES A SEGUIR CONTINUAM INTACTOS. Este e o coracao do teste:
  // com a versao de 4x u32, as duas primeiras guardas ficavam a zero.
  for (std::uint32_t k = 0; k < 8; k += 2) {
    EXPECT_EQ(b.Mem().Ler16(kRect + 8 + k), kGuarda)
        << "byte " << (8 + k) << " do lado de fora do AEERect foi escrito";
  }
}

TEST(SetColor, OItemVaiNoR1ACorNoR2EORetornoEACorAnterior) {
  // `RGBVAL SetColor(IDisplay*, AEEClrItem clr, RGBVAL rgb)` -- AEEIDisplay.h:232.
  //
  // Tres defeitos de uma vez, e os tres escondiam-se uns aos outros:
  //   1. lia-se a cor do `r1`, que e o ITEM (1..16). O numero do item usado como
  //      cor da sempre um pixel quase preto.
  //   2. a cor (`r2`) e RGBVAL (`r<<8 | g<<16 | b<<24`); truncar com `& 0xFFFF`
  //      fazia RGB_WHITE e MAKE_RGB(255,0,0) darem AMBOS 0xFF00.
  //   3. devolvia-se 0 em vez da cor anterior. O idioma do cabecalho e guardar o
  //      retorno e repo-lo -- com zero, repunha-se PRETO.
  Bancada b;
  constexpr std::uint32_t kItem = 1;  // CLR_USER_TEXT
  constexpr std::uint32_t kBranco = 0xFFFFFF00u;   // RGB_WHITE
  constexpr std::uint32_t kVermelho = 0x0000FF00u; // MAKE_RGB(255,0,0)

  // A primeira chamada devolve a cor anterior do item, que e preto.
  EXPECT_EQ(b.ChamaSaida(1535, kObjDisplay, kItem, kBranco), 0u);
  // A segunda devolve o BRANCO que a primeira la deixou -- e nao zero.
  EXPECT_EQ(b.ChamaSaida(1535, kObjDisplay, kItem, kVermelho), kBranco);

  // RGB_NONE le sem escrever (AEERGBVAL.h:27).
  EXPECT_EQ(b.ChamaSaida(1535, kObjDisplay, kItem, 0xFFFFFFFFu), kVermelho);
  EXPECT_EQ(b.ChamaSaida(1535, kObjDisplay, kItem, 0xFFFFFFFFu), kVermelho)
      << "RGB_NONE nao pode ter escrito nada";

  // Itens DIFERENTES nao se pisam: o item 2 continua preto.
  EXPECT_EQ(b.ChamaSaida(1535, kObjDisplay, 2u, kBranco), 0u);
}

TEST(SetColor, BrancoEVermelhoNaoDaoOMesmoPixel) {
  // O coracao do defeito 2, isolado: com `& 0xFFFF` os dois davam 0xFF00.
  // A conversao tem de ser a MESMA do rasterizador (`Para565`), senao o mesmo
  // RGBVAL dava dois pixels diferentes conforme quem desenhasse.
  EXPECT_NE(Tela::RgbvalPara565(0xFFFFFF00u), Tela::RgbvalPara565(0x0000FF00u));
  EXPECT_EQ(Tela::RgbvalPara565(0xFFFFFF00u), 0xFFFFu);  // branco
  EXPECT_EQ(Tela::RgbvalPara565(0x0000FF00u), 0xF800u);  // vermelho puro
  EXPECT_EQ(Tela::RgbvalPara565(0x00FF0000u), 0x07E0u);  // verde puro
  EXPECT_EQ(Tela::RgbvalPara565(0xFF000000u), 0x001Fu);  // azul puro
  EXPECT_EQ(Tela::RgbvalPara565(0x00000000u), 0x0000u);  // preto
}

TEST(Ajudantes, OStrcatJuntaNoFimEDevolveODestino) {
  // `char *strcat(char *dst, const char *src)` -- AEEHelperFuncs 0x00C.
  // Faltava, e era a ajudante que MAIS TITULOS pediam: 10 dos 62.
  Bancada b;
  constexpr std::uint32_t kDst = 0x80210000u, kSrc = 0x80210100u;
  const char* a = "abc";
  const char* c = "de";
  for (std::uint32_t i = 0; i <= 3; ++i) b.Mem().Escrever8(kDst + i, static_cast<std::uint8_t>(a[i]));
  for (std::uint32_t i = 0; i <= 2; ++i) b.Mem().Escrever8(kSrc + i, static_cast<std::uint8_t>(c[i]));
  // Uma sentinela logo a seguir ao que devia ser escrito: "abcde" + NUL = 6 bytes.
  b.Mem().Escrever8(kDst + 6, 0x7Eu);

  EXPECT_EQ(b.ChamaSaida(1568, kDst, kSrc), kDst) << "strcat devolve o destino";
  std::string saiu;
  for (std::uint32_t i = 0; i < 6; ++i) {
    const std::uint8_t ch = b.Mem().Ler8(kDst + i);
    if (ch == 0) break;
    saiu.push_back(static_cast<char>(ch));
  }
  EXPECT_EQ(saiu, "abcde");
  EXPECT_EQ(b.Mem().Ler8(kDst + 5), 0u) << "tem de terminar em NUL";
  EXPECT_EQ(b.Mem().Ler8(kDst + 6), 0x7Eu) << "e nao escrever para la do NUL";
}

TEST(Ajudantes, OSleepAvancaORelogioVirtualENaoDormeDeVerdade) {
  // `void sleep(uint32 msecs)` -- AEEHelperFuncs 0x184, pedido por 9 dos 62.
  //
  // NAO pode dormir: parar o relogio do anfitriao dentro de uma medicao viola o
  // P4, pelo mesmo motivo que o orcamento por relogio violava. O que avanca e o
  // tempo VIRTUAL -- o mesmo que o `aee_GetUpTimeMS` devolve.
  Bancada b;
  const std::uint32_t antes = b.ChamaSaida(1540, 0);  // kSlotIdGetUpTime
  b.ChamaSaida(1569, 250);                            // sleep(250)
  EXPECT_EQ(b.ChamaSaida(1540, 0), antes + 250) << "o relogio virtual tem de andar";

  // O TECTO: um valor absurdo nao pode empurrar o relogio para o fim do mundo e
  // fazer vencer todos os temporizadores de uma vez.
  const std::uint32_t meio = b.ChamaSaida(1540, 0);
  b.ChamaSaida(1569, 0xFFFFFFFFu);
  EXPECT_EQ(b.ChamaSaida(1540, 0), meio + 60000u) << "um minuto virtual e o tecto";
}

TEST(Ajudantes, OStrcatEOSleepESTAOCABLADOSNaTabelaDoModulo) {
  // O TESTE ANTERIOR NAO CHEGAVA, e isso ficou provado: `ChamaSaida(1568, ...)`
  // entra no ramo do despacho DIRECTAMENTE e nunca passa pela tabela
  // `AEEHelperFuncs`. Arranquei a ligacao e os dois testes continuaram VERDES --
  // testavam a implementacao, e o que faltava era a CABLAGEM.
  //
  // O modulo nao chama 1568: chama o endereco que esta em `tabela + 0x00C`. Se
  // esse endereco for o do stub que recusa, a implementacao existe e nao serve
  // para nada -- que e exactamente o sintoma que o projecto ja apanhou uma vez
  // ("falta SetTimer" com o SetTimer escrito e a funcionar).
  Bancada b;
  EXPECT_EQ(b.Mem().Ler32(kTabela + 0x00C), b.S().Endereco(1568))
      << "AEEHelperFuncs[0x00C] (strcat) nao aponta para a implementacao";
  EXPECT_EQ(b.Mem().Ler32(kTabela + 0x184), b.S().Endereco(1569))
      << "AEEHelperFuncs[0x184] (sleep) nao aponta para a implementacao";

  // E a prova de que a tabela e mesmo a que o guest le: um ajudante que JA estava
  // cablado antes desta mudanca continua cablado (nao partimos nada ao lado).
  EXPECT_EQ(b.Mem().Ler32(kTabela + 0x008), b.S().Endereco(1505))
      << "strcpy (0x008) perdeu a cablagem";
}

TEST(Ajudantes, OStrncpyNaoTerminaQuandoEnchePreencheZerosQuandoSobra) {
  // `char *strncpy(char *dst, const char *src, size_t n)` -- AEEHelperFuncs 0x0C8.
  // As duas pontas que enganam, e que um `strcpy` limitado nao faz:
  //   - NAO termina em NUL se o src tiver n ou mais bytes;
  //   - ENCHE o resto com zeros se tiver menos.
  Bancada b;
  constexpr std::uint32_t kD = 0x80210200u, kS = 0x80210300u;
  for (std::uint32_t i = 0; i < 8; ++i) b.Mem().Escrever8(kD + i, 0x55u);
  const char* s = "abcdef";
  for (std::uint32_t i = 0; i <= 6; ++i) b.Mem().Escrever8(kS + i, static_cast<std::uint8_t>(s[i]));

  // n MENOR que o src: copia n e NAO termina.
  EXPECT_EQ(b.ChamaSaida(1570, kD, kS, 3), kD);
  EXPECT_EQ(b.Mem().Ler8(kD + 0), 'a');
  EXPECT_EQ(b.Mem().Ler8(kD + 2), 'c');
  EXPECT_EQ(b.Mem().Ler8(kD + 3), 0x55u) << "strncpy nao pode terminar quando enche";

  // n MAIOR que o src: copia, termina, e enche o resto de zeros.
  for (std::uint32_t i = 0; i < 10; ++i) b.Mem().Escrever8(kD + i, 0x55u);
  EXPECT_EQ(b.ChamaSaida(1570, kD, kS, 9), kD);
  EXPECT_EQ(b.Mem().Ler8(kD + 5), 'f');
  EXPECT_EQ(b.Mem().Ler8(kD + 6), 0u);
  EXPECT_EQ(b.Mem().Ler8(kD + 8), 0u) << "o resto tem de ser enchido com zeros";
  EXPECT_EQ(b.Mem().Ler8(kD + 9), 0x55u) << "e nao passar de n";
}

TEST(Ajudantes, OStrstrEstaEm0x0D8EDistingueMaiusculas) {
  // `char *strstr(const char *haystack, const char *needle)` -- 0x0D8.
  //
  // O OFFSET IMPORTA: 0x0E8 e o `stristr`, que NAO distingue maiusculas. O
  // zeebulator regista o strstr em 0x0E8 (`mod_runtime.cpp:21`) e essa troca e
  // silenciosa -- o jogo recebe uma resposta plausivel e errada. Este teste fixa
  // as DUAS coisas: que esta cablado no 0x0D8, e que distingue maiusculas (o que
  // prova que e mesmo o strstr e nao o vizinho).
  Bancada b;
  constexpr std::uint32_t kH = 0x80210400u, kN = 0x80210500u;
  auto poe = [&](std::uint32_t e, const char* t) {
    for (std::uint32_t i = 0;; ++i) { b.Mem().Escrever8(e + i, static_cast<std::uint8_t>(t[i])); if (!t[i]) break; }
  };
  poe(kH, "roms/neogeo/karnovr");
  poe(kN, "neogeo");
  EXPECT_EQ(b.ChamaSaida(1571, kH, kN), kH + 5) << "tem de achar no offset 5";

  poe(kN, "NeoGeo");
  EXPECT_EQ(b.ChamaSaida(1571, kH, kN), 0u)
      << "strstr DISTINGUE maiusculas -- se achasse, era o stristr de 0x0E8";

  poe(kN, "");
  EXPECT_EQ(b.ChamaSaida(1571, kH, kN), kH) << "agulha vazia devolve o palheiro";

  // E a cablagem, no offset do cabecalho e nao no do zeebulator.
  EXPECT_EQ(b.Mem().Ler32(kTabela + 0x0D8), b.S().Endereco(1571)) << "strstr vive em 0x0D8";
  EXPECT_EQ(b.Mem().Ler32(kTabela + 0x0C8), b.S().Endereco(1570)) << "strncpy vive em 0x0C8";
}

// ===========================================================================
// O CAMINHO DO DESENHO -- os quatro defeitos que mantinham os 10 titulos da
// familia `emulator_neo` em 640 pixels e 1 cor. Cada teste diz o que MEDIU.
// ===========================================================================

TEST(DrawRect, RectNuloComFillLimpaOEcraInteiro) {
  // `AEEIDisplay.h:1043-1045`: "If pRect is NULL and dwFlags contains
  // IDF_RECT_FILL, this function clears the entire destination bitmap (or
  // current clip rectangle if set) using clrFill."
  //
  // E o `IDisplay_ClearScreen` (`AEEIDisplay.h:380-383`) e exactamente essa
  // chamada. O despacho tinha um `if (prc != 0)` a envolver tudo: TODA a limpeza
  // de ecra do corpus era deitada fora sem nada a acusar.
  //
  // MEDIDO no `karnovr.mod` 0xe8ac-0xe8d4 (`mov r1,#0`, `mov r3,#2` -> `[sp]`).
  constexpr std::uint32_t kIdfRectFill = 0x02u;   // AEEIDisplay.h:41
  constexpr std::uint32_t kIdfRectFrame = 0x01u;  // AEEIDisplay.h:40
  constexpr std::uint32_t kRgbNone = 0xFFFFFFFFu; // AEERGBVAL.h:27
  {
    Bancada b;
    b.D().TelaRef().Limpar();
    b.ChamaSaida(1533, kObjDisplay, 0u, kRgbNone, kRgbNone, kIdfRectFill,
                 static_cast<std::uint64_t>(Tela::kLargura) * Tela::kAltura + 1000);
    EXPECT_EQ(b.D().TelaRef().Escritos(),
              static_cast<std::uint32_t>(Tela::kLargura) * Tela::kAltura)
        << "DrawRect(NULL, ..., IDF_RECT_FILL) tem de limpar o ecra inteiro";
  }
  {
    // SEM o FILL, um `pRect` nulo e um rectangulo VAZIO (mesma linha do SDK).
    Bancada b;
    b.D().TelaRef().Limpar();
    b.ChamaSaida(1533, kObjDisplay, 0u, kRgbNone, kRgbNone, kIdfRectFrame);
    EXPECT_EQ(b.D().TelaRef().Escritos(), 0u)
        << "pRect nulo SEM IDF_RECT_FILL e um rectangulo vazio";
  }
  {
    // "or current clip rectangle if set": com clip, limpa-se o CLIP.
    Bancada b;
    b.D().TelaRef().Limpar();
    constexpr std::uint32_t kRect = 0x80210000u;
    b.Mem().Escrever16(kRect + 0, 10);
    b.Mem().Escrever16(kRect + 2, 20);
    b.Mem().Escrever16(kRect + 4, 30);
    b.Mem().Escrever16(kRect + 6, 40);
    b.ChamaSaida(1536, kObjDisplay, kRect);  // SetClipRect
    b.ChamaSaida(1533, kObjDisplay, 0u, kRgbNone, kRgbNone, kIdfRectFill, 100000);
    EXPECT_EQ(b.D().TelaRef().Escritos(), 30u * 40u) << "o clip e que manda no tamanho";
  }
}

TEST(DrawText, NCharsMenosUmContaAStringENaoQuatroMilMilhoes) {
  // `AEEIDisplay.h:939-940`: "nChars ... If this is -1, the length will be
  // automatically computed by this function".
  //
  // `nchars` era lido como `uint32`: `0xFFFFFFFF * 8` dava um laco de quatro mil
  // milhoes de iteracoes que escrevia 640 pixels (a largura do ecra) -- e ERA
  // ESSA a medida "640 pixels" dos 10 titulos da familia `emulator_neo`.
  // Medido: ~4 s de uma corrida de 9,2 s do `karnovr` gastos neste unico laco.
  Bancada b;
  b.D().TelaRef().Limpar();
  constexpr std::uint32_t kTexto = 0x80211000u;
  const char* kAscii = "OLA";  // 3 caracteres
  for (std::uint32_t k = 0; k < 3; ++k) {
    b.Mem().Escrever16(kTexto + k * 2, static_cast<std::uint16_t>(kAscii[k]));
  }
  b.Mem().Escrever16(kTexto + 6, 0);  // AECHAR terminador
  // x, y e prcBackground vao na pilha (sp+0, sp+4, sp+8); o `ChamaSaida` so
  // escreve o sp+0, os outros dois escrevem-se aqui.
  b.Mem().Escrever32(0x80090004u, 0);  // y = 0
  b.Mem().Escrever32(0x80090008u, 0);  // prcBackground = NULL
  b.ChamaSaida(1532, kObjDisplay, 0x8000u /* AEE_FONT_NORMAL */, kTexto, 0xFFFFFFFFu,
               /*na_pilha = x =*/0u, 100000);
  EXPECT_EQ(b.D().TelaRef().Escritos(), 3u * 8u)
      << "com nChars = -1 conta-se a string: 3 caracteres x 8 px";
}

TEST(BitmapGetInfo, OSlot12RespondeOTamanhoDaTelaERespeitaONSize) {
  // `int GetInfo(IBitmap*, AEEBitmapInfo*, int nSize)` -- slot 12
  // (`AEEIBitmap.h:42-58`), com `AEEBitmapInfo` = {cx, cy, nDepth}
  // (`AEEIBitmap.h:34-38`).
  //
  // MEDIDO: os 10 titulos da familia `emulator_neo` pedem-no com `nSize = 0xc`
  // (`karnovr.mod` 0xfdd4) e guardam cx/cy como o tamanho do ecra. Sem resposta,
  // ficavam com lixo da pilha a fazer de largura e altura.
  Bancada b;
  constexpr std::uint32_t kInfo = 0x80212000u;
  constexpr std::uint32_t kGuarda = 0xDEADBEEFu;
  for (std::uint32_t k = 0; k < 4; ++k) b.Mem().Escrever32(kInfo + k * 4, kGuarda);
  b.ChamaSaida(1572, kObjDibBase + 0x300, kInfo, 12u);
  EXPECT_EQ(b.Mem().Ler32(kInfo + 0), static_cast<std::uint32_t>(Tela::kLargura));
  EXPECT_EQ(b.Mem().Ler32(kInfo + 4), static_cast<std::uint32_t>(Tela::kAltura));
  EXPECT_EQ(b.Mem().Ler32(kInfo + 8), 16u) << "RGB565 = 16 bits, o pixel da Tela";
  EXPECT_EQ(b.Mem().Ler32(kInfo + 12), kGuarda) << "escreveu para la do AEEBitmapInfo";

  // O `nSize` e o contrato (`AEEIBitmap.h:764`): com 8, o terceiro campo fica.
  for (std::uint32_t k = 0; k < 4; ++k) b.Mem().Escrever32(kInfo + k * 4, kGuarda);
  b.ChamaSaida(1572, kObjDibBase + 0x300, kInfo, 8u);
  EXPECT_EQ(b.Mem().Ler32(kInfo + 0), static_cast<std::uint32_t>(Tela::kLargura));
  EXPECT_EQ(b.Mem().Ler32(kInfo + 8), kGuarda) << "nSize = 8 nao autoriza o terceiro campo";
}

TEST(Qegl, ORetornoSaiPeloPonteiroFinalEOsArgumentosEstaoDeslocados) {
  // A MOLDURA DO QEGL: `int metodo(IQEGL *pMe, <args do EGL>, <tipo> *pSaida)`.
  //
  // MEDIDO no `karnovr.mod` (base 0, offset de ficheiro == endereco):
  //   0x102c0 `ldr pc,[r4,#0x14]` (slot 5 = eglInitialize) e logo a seguir
  //   0x102c4 `ldr r0,[sp,#4]` -- o titulo devolve *pSaida, NAO o r0.
  // O `InitGLSurface` (0xfba8) faz `cmp r0,#1` e desviava para o ecra de erro
  // "InitGLSurface failed" com o lixo da pilha que estava no lugar do EGL_TRUE.
  //
  // E o `pMe` DESLOCA os argumentos: sem o deslocamento, o `dpy` verdadeiro caia
  // no lugar do `major` e o modulo escrevia `1` EM CIMA do objecto IEGL.
  Bancada b;
  const std::uint32_t qegl = VtClasse(static_cast<std::uint32_t>(Classe::kQEGL));
  constexpr std::uint32_t kSaida = 0x80213000u;
  constexpr std::uint32_t kQeglObj = 0x8F005000u;  // ObjetoDaClasse(kQEGL)
  b.Mem().Escrever32(kSaida, 0x5A5A5A5Au);
  const std::uint32_t vtable_antes = b.Mem().Ler32(kObjIegl);
  // eglInitialize(pMe, dpy, major = NULL, minor = NULL, &saida)
  b.ChamaSaida(qegl + 5, kQeglObj, kObjIegl, 0u, 0u, kSaida);
  EXPECT_EQ(b.Mem().Ler32(kSaida), 1u) << "EGL_TRUE tem de sair pelo ponteiro final";
  EXPECT_EQ(b.Mem().Ler32(kObjIegl), vtable_antes)
      << "o dpy no lugar do `major` escrevia por cima da vtable do IEGL";
}
// ===========================================================================
// `GetAEEVersion`: a ASSINATURA e o VALOR.
//
// `uint32 GetAEEVersion(byte *pszFormatted, int nSize, uint16 wFlags)`
// (`AEEStdLib.h:115-116`). O `r1` e um TAMANHO, nao um ponteiro.
// ===========================================================================
TEST(GetAEEVersion, OR1EUmTamanhoENaoUmPonteiro) {
  // O DEFEITO MEDIDO: o codigo antigo fazia `Escrever32(r1, versao)`. Com um
  // `nSize` de 16 -- o valor natural para um buffer de "4.0.2.0" -- isso
  // escrevia quatro bytes em 0x00000010, dentro da imagem do titulo (a base do
  // modulo e ZERO, `tests/mod_base_test.cpp`), e o buffer do `r0` ficava por
  // tocar.
  Bancada b;
  constexpr std::uint32_t kSaidaGetAeeVersion = 1501;  // `despacho.cpp:60`
  constexpr std::uint32_t kBuf = 0x00091000u;
  constexpr std::uint32_t kTamanho = 16;
  b.Mem().Escrever32(kTamanho, 0xDEADBEEFu);  // o endereco que o defeito pisava
  for (std::uint32_t k = 0; k < 32; ++k) b.Mem().Escrever8(kBuf + k, 0xAAu);

  const std::uint32_t r = b.ChamaSaida(kSaidaGetAeeVersion, kBuf, kTamanho, 0x0001u);

  EXPECT_EQ(b.Mem().Ler32(kTamanho), 0xDEADBEEFu)
      << "o `nSize` foi usado como endereco de escrita";
  // GAV_LATIN1 (`AEEStdLib.h:35`): cadeia de um byte por letra, terminada.
  std::string s;
  b.Mem().LerCadeia(kBuf, &s, 32);
  EXPECT_EQ(s, "4.0.2.0");
  EXPECT_EQ(r, 0x04000200u);
}

TEST(GetAEEVersion, SemGavLatin1ACadeiaEAECHAR) {
  Bancada b;
  constexpr std::uint32_t kSaidaGetAeeVersion = 1501;
  constexpr std::uint32_t kBuf = 0x00091100u;
  for (std::uint32_t k = 0; k < 32; ++k) b.Mem().Escrever8(kBuf + k, 0xAAu);
  EXPECT_EQ(b.ChamaSaida(kSaidaGetAeeVersion, kBuf, 32, 0u), 0x04000200u);
  const char* esperado = "4.0.2.0";
  for (std::uint32_t k = 0; k < 7; ++k) {
    EXPECT_EQ(b.Mem().Ler16(kBuf + k * 2), static_cast<std::uint16_t>(esperado[k])) << "letra " << k;
  }
  EXPECT_EQ(b.Mem().Ler16(kBuf + 14), 0u) << "AECHAR terminado";
}

TEST(GetAEEVersion, OValorEOQueOSDKCodifica) {
  // `AEEStdLib.h:4948-4954`: byte alto da palavra alta = versao MAIOR. O valor
  // antigo, `0x00400002`, lia-se "0.64.0.2". Mesmo valor no zeebx
  // (`src/machine/helper.rs:695-705`) e no zeebulator (`mod_runtime.cpp:751`).
  Bancada b;
  constexpr std::uint32_t kSaidaGetAeeVersion = 1501;
  const std::uint32_t v = b.ChamaSaida(kSaidaGetAeeVersion, 0, 0, 0);
  EXPECT_EQ(v >> 24, 4u) << "versao maior";
  EXPECT_EQ((v >> 16) & 0xFFu, 0u) << "versao menor";
  EXPECT_EQ((v >> 8) & 0xFFu, 2u) << "sub-versao";
  EXPECT_EQ(v & 0xFFu, 0u) << "build";
  EXPECT_EQ(v, 0x04000200u);
}

TEST(GetAEEVersion, BufferPequenoDemaisNaoTransbordaEUmBufferNuloNaoEscreve) {
  Bancada b;
  constexpr std::uint32_t kSaidaGetAeeVersion = 1501;
  constexpr std::uint32_t kBuf = 0x00091200u;
  for (std::uint32_t k = 0; k < 16; ++k) b.Mem().Escrever8(kBuf + k, 0x55u);
  EXPECT_EQ(b.ChamaSaida(kSaidaGetAeeVersion, kBuf, 4, 0x0001u), 0x04000200u);
  std::string s;
  b.Mem().LerCadeia(kBuf, &s, 16);
  EXPECT_EQ(s, "4.0");  // 3 letras + terminador nos 4 bytes pedidos
  EXPECT_EQ(b.Mem().Ler8(kBuf + 4), 0x55u) << "escreveu para la do `nSize`";
  // `nSize` zero nao escreve nada, e o valor continua a ser devolvido.
  EXPECT_EQ(b.ChamaSaida(kSaidaGetAeeVersion, kBuf, 0, 0x0001u), 0x04000200u);
  EXPECT_EQ(b.Mem().Ler8(kBuf), static_cast<std::uint8_t>('4'));
}

// ===========================================================================
// `GetDeviceInfo`: o `wStructSize` e campo DE ENTRADA.
//
// `AEEIShell.h:116-120`: "In order to use the following fields, you MUST
// fill-in the wStructSize element of the structure before passing this to the
// GetDeviceInfo call."
// ===========================================================================
TEST(GetDeviceInfo, UmaStructCurtaNaoLevaEscritaNaCauda) {
  // O DEFEITO MEDIDO: escreviam-se sempre 64 bytes. Um titulo compilado contra a
  // struct curta (44 bytes, ate ao `dwLang`) levava 20 bytes por cima do que
  // estivesse a seguir -- e o `AEEDeviceInfo` do guest esta quase sempre na
  // PILHA, logo por cima das variaveis locais de quem chamou.
  Bancada b;
  // O MESMO endereco de saida que a `tools/bateria.cpp:633` cabla no slot 4 do
  // IShell (`kSlotIdGetDeviceInfo`, `despacho.cpp:75`).
  constexpr std::uint32_t kSaidaGetDeviceInfo = 1549;
  constexpr std::uint32_t kPi = 0x00092000u;
  for (std::uint32_t k = 0; k < 96; ++k) b.Mem().Escrever8(kPi + k, 0x5Au);
  // O chamador NAO declarou tamanho nenhum: o campo fica com o lixo que tinha.
  b.Mem().Escrever16(kPi + 44, 0);
  b.ChamaSaida(kSaidaGetDeviceInfo, 0x80020000u, kPi);

  EXPECT_NE(b.Mem().Ler16(kPi + 0), 0x5A5Au) << "a cabeca da struct tem de ser escrita";
  for (std::uint32_t k = 46; k < 64; ++k) {
    EXPECT_EQ(b.Mem().Ler8(kPi + k), 0x5Au)
        << "byte " << k << ": escrita para la do que o titulo declarou";
  }
  EXPECT_EQ(b.Mem().Ler16(kPi + 44), 0u) << "o proprio wStructSize e do chamador";
}

TEST(GetDeviceInfo, UmaStructCOMPLETAPedidaPeloTituloLevaACauda) {
  // O outro lado da mesma regra, e o titulo que o obrigou do lado do zeebx
  // (Bejeweled Twist, `src/machine/shell.rs:425-430`): quem declara 64 quer o
  // `wMaxPath`, e recebe-lo a zero seria "nenhum caminho de ficheiro cabe".
  Bancada b;
  // O MESMO endereco de saida que a `tools/bateria.cpp:633` cabla no slot 4 do
  // IShell (`kSlotIdGetDeviceInfo`, `despacho.cpp:75`).
  constexpr std::uint32_t kSaidaGetDeviceInfo = 1549;
  constexpr std::uint32_t kPi = 0x00092100u;
  for (std::uint32_t k = 0; k < 96; ++k) b.Mem().Escrever8(kPi + k, 0x5Au);
  b.Mem().Escrever16(kPi + 44, 64);
  b.ChamaSaida(kSaidaGetDeviceInfo, 0x80020000u, kPi);

  EXPECT_EQ(b.Mem().Ler16(kPi + 44), 64u);
  EXPECT_EQ(b.Mem().Ler16(kPi + 56), 256u) << "wMaxPath (+56)";
  EXPECT_EQ(b.Mem().Ler8(kPi + 64), 0x5Au) << "nem um byte para la da struct";
}

TEST(GetDeviceInfo, OValorDECLARADOFicaContado) {
  // A demanda mais alta do corpus (18 titulos) nao deixava rasto nenhum.
  Bancada b;
  // O MESMO endereco de saida que a `tools/bateria.cpp:633` cabla no slot 4 do
  // IShell (`kSlotIdGetDeviceInfo`, `despacho.cpp:75`).
  constexpr std::uint32_t kSaidaGetDeviceInfo = 1549;
  constexpr std::uint32_t kPi = 0x00092200u;
  b.Mem().Escrever16(kPi + 44, 64);
  b.ChamaSaida(kSaidaGetDeviceInfo, 0x80020000u, kPi);
  const auto& p = b.Tr().ContagemPressupostos();
  ASSERT_NE(p.find("IShell::GetDeviceInfo"), p.end())
      << "um valor declarado que nao se conta e um valor invisivel";
  EXPECT_EQ(p.at("IShell::GetDeviceInfo"), 1u);
}

// ===========================================================================
// `CreateDIBitmap`: o IDIB e uma STRUCT PUBLICA, e um sucesso com `pBmp = 0` e
// um sucesso a apontar para a base do modulo do proprio titulo.
// ===========================================================================
TEST(CreateDIBitmap, OBufferDePixelsExisteMesmoEOCabecalhoEODoSDK) {
  Bancada b;
  constexpr std::uint32_t kSaidaCreateDIBitmap = 1538;  // `despacho.cpp:69`
  constexpr std::uint32_t kPpIdib = 0x00093000u;
  b.Mem().Escrever32(kPpIdib, 0xDEADBEEFu);
  // `CreateDIBitmap(po, ppIDIB, colorDepth, cx, cy)` -- a altura vai na pilha.
  ASSERT_EQ(b.ChamaSaida(kSaidaCreateDIBitmap, kObjDisplay, kPpIdib, 16, 32, 8), 0u);
  const std::uint32_t dib = b.Mem().Ler32(kPpIdib);
  ASSERT_NE(dib, 0u);
  ASSERT_NE(dib, 0xDEADBEEFu);

  // `AEEIDIB.h:42-55`, campo a campo.
  EXPECT_EQ(b.Mem().Ler32(dib + 4), 0u) << "pPaletteMap: ponteiro publico, tem de ser nulo";
  const std::uint32_t pbmp = b.Mem().Ler32(dib + 8);
  EXPECT_NE(pbmp, 0u) << "pBmp a zero e o endereco 0, que aqui e a base do modulo";
  EXPECT_EQ(b.Mem().Ler16(dib + 20), 32u) << "cx (uint16, +20)";
  EXPECT_EQ(b.Mem().Ler16(dib + 22), 8u) << "cy (uint16, +22)";
  EXPECT_EQ(b.Mem().Ler16(dib + 24), 64u) << "nPitch (+24): 32 px x 2 bytes";
  EXPECT_EQ(b.Mem().Ler8(dib + 28), 16u) << "nDepth em BITS (+28)";
  EXPECT_EQ(b.Mem().Ler8(dib + 29), 16u) << "nColorScheme = IDIB_COLORSCHEME_565";

  // O buffer e MESMO do guest: escreve-se e le-se, e esta a zeros ao nascer.
  for (std::uint32_t k = 0; k < 64 * 8; ++k) {
    ASSERT_EQ(b.Mem().Ler8(pbmp + k), 0u) << "byte " << k << " do buffer novo";
  }
  b.Mem().Escrever16(pbmp + 2, 0xF800u);
  EXPECT_EQ(b.Mem().Ler16(pbmp + 2), 0xF800u);
}

TEST(CreateDIBitmap, UmaProfundidadeSemCaminhoRecusaComNome) {
  // O `BitBlt` deste despacho le a origem com `Ler16` seja qual for o
  // `colorDepth` pedido. Devolver SUCESSO para 8 bits daria um blit de lixo mais
  // tarde e noutro sitio -- a forma exacta da mentira silenciosa. O zeebx faz o
  // mesmo: recusa o formato que nao sabe tratar (`src/machine/bitmap.rs:126-131`).
  Bancada b;
  constexpr std::uint32_t kSaidaCreateDIBitmap = 1538;
  constexpr std::uint32_t kPpIdib = 0x00093100u;
  b.Mem().Escrever32(kPpIdib, 0xDEADBEEFu);
  EXPECT_EQ(b.ChamaSaida(kSaidaCreateDIBitmap, kObjDisplay, kPpIdib, 8, 32, 8), kAeeUnsupported);
  EXPECT_EQ(b.Mem().Ler32(kPpIdib), 0u) << "uma recusa nao pode deixar ponteiro nenhum de pe";
  EXPECT_EQ(b.Faltas("IDisplay::CreateDIBitmap"), 1u);
}

TEST(CreateDIBitmap, DoisBitmapsNaoPartilhamOMesmoBuffer) {
  Bancada b;
  constexpr std::uint32_t kSaidaCreateDIBitmap = 1538;
  constexpr std::uint32_t kA = 0x00093200u, kB = 0x00093300u;
  ASSERT_EQ(b.ChamaSaida(kSaidaCreateDIBitmap, kObjDisplay, kA, 16, 16, 16), 0u);
  ASSERT_EQ(b.ChamaSaida(kSaidaCreateDIBitmap, kObjDisplay, kB, 16, 16, 16), 0u);
  const std::uint32_t da = b.Mem().Ler32(kA), db = b.Mem().Ler32(kB);
  EXPECT_NE(da, db);
  EXPECT_NE(b.Mem().Ler32(da + 8), b.Mem().Ler32(db + 8));
}

TEST(BitmapDoEcra, OPBmpApontaParaOEcraDoGuestEJaNaoEUmaFalta) {
  // ESTE TESTE AFIRMAVA O CONTRARIO, e estava certo no dia em que foi escrito:
  // o `pBmp` ficava a ZERO e isso era uma falta com nome. Passou a falso quando
  // o ecra ganhou pagina no guest (`core/brew/ecra.h`, `kBaseDoEcraNoGuest`) --
  // e a falta desapareceu porque a capacidade EXISTE, nao porque se calou. O
  // comportamento novo tem testes proprios em `tests/ecra_guest_test.cpp`.
  Bancada b;
  const std::uint32_t saida = 0x80090100u;
  b.ChamaSaida(1550, kObjDisplay, saida);
  const std::uint32_t bmp = b.Mem().Ler32(saida);
  ASSERT_EQ(bmp, kObjDibBase + 0x300);
  EXPECT_EQ(b.Mem().Ler32(bmp + 8), kBaseDoEcraNoGuest) << "pBmp do ecra";
  EXPECT_EQ(b.Faltas("IDIB::pBmp do bitmap do ecra"), 0u);
  EXPECT_EQ(b.Mem().Ler16(bmp + 20), Tela::kLargura);
  EXPECT_EQ(b.Mem().Ler16(bmp + 22), Tela::kAltura);
  EXPECT_EQ(b.Mem().Ler8(bmp + 28), 16u);
}

// ===========================================================================
// AS RECUSAS MUDAS: `RmDir`, `IFile::Write`, `ISQLMgr::Open`.
//
// As tres devolviam o codigo certo e NAO registavam nada -- ao contrario do
// `MkDir`/`Remove` ao lado, que registam. Uma recusa que nao se conta nao
// aparece na corrida, e a lista do que falta diz que ninguem pediu.
// ===========================================================================
TEST(RecusasMudas, WriteESqlOpenPassamAContar) {
  Bancada b;
  constexpr std::uint32_t kSaidaWrite = 1559, kSaidaSqlOpen = 1553;
  constexpr std::uint32_t kNome = 0x00094000u;
  const char* dir = "brew/save";
  for (std::uint32_t k = 0; dir[k] != 0; ++k) b.Mem().Escrever8(kNome + k, static_cast<std::uint8_t>(dir[k]));
  b.Mem().Escrever8(kNome + 9, 0);

  // O `RmDir` SAIU DESTE TESTE, e nao por conveniencia: ele deixou de ser uma
  // recusa muda. O `Remove` (1509) e o `RmDir` (1547) passaram a SERVIR o
  // contrato sobre a VFS, e tem testes proprios (`FileMgrServido.*` abaixo).
  EXPECT_EQ(b.ChamaSaida(kSaidaWrite, 0x80060100u, 0x00094100u, 64), 0u);
  EXPECT_EQ(b.Faltas("IFile::Write"), 1u)
      << "o Write devolve BYTES ESCRITOS: zero e uma resposta legitima do "
         "contrato, e por isso e a recusa mais perigosa de calar";

  EXPECT_EQ(b.ChamaSaida(kSaidaSqlOpen, 0x80060200u, kNome, 0x00094200u, 0x00094300u),
            kAeeUnsupported);
  EXPECT_EQ(b.Faltas("ISQLMgr::Open"), 1u);
}

TEST(GetLastError, DevolveOErroDaUltimaOperacaoQueFalhou) {
  // Devolvia SEMPRE 0 -- "sem erro" logo a seguir a uma recusa.
  Bancada b;
  constexpr std::uint32_t kSaidaLastErr = 1512, kSaidaRmDir = 1547;
  constexpr std::uint32_t kNome = 0x00094400u;
  b.Mem().Escrever8(kNome, 0);
  EXPECT_EQ(b.ChamaSaida(kSaidaLastErr, 0x80060000u), 0u) << "sem operacao nenhuma, sem erro";
  // A bancada nao registou VFS nenhuma, logo o caminho nao existe e o `RmDir`
  // responde EFAILED -- e EFAILED e o que o `GetLastError` tem de dizer. Antes
  // disto a resposta era `kAeeUnsupported` (20), que NAO e resposta do contrato
  // do IFileMgr: `AEEFile.h` so declara SUCCESS e EFAILED para o `RmDir`.
  EXPECT_EQ(b.ChamaSaida(kSaidaRmDir, 0x80060000u, kNome), static_cast<std::uint32_t>(kAeeFailed));
  EXPECT_EQ(b.ChamaSaida(kSaidaLastErr, 0x80060000u), static_cast<std::uint32_t>(kAeeFailed));
}

// ===========================================================================
// O IFileMgr A SERVIR: `Remove` (slot 4), `RmDir` (slot 6) e `EnumNext`
// (slot 11), com o contrato do SDK sobre a VFS.
//
// A NUMERACAO E A DO CABECALHO, e nao a da cablagem da ferramenta: o primeiro
// teste le `tools/brew_slots.inc`, GERADO de `AEEFile.h` (a guarda
// `slots_do_sdk` regenera-o e compara). Chamar que chamasse so o ID interno
// nao provavam nada sobre o slot que o JOGO chama -- foi a classe de defeito
// que ja custou a cablagem do `SetTimer` nesta arvore.
//
// A VFS CONTINUA SO DE LEITURA, por decisao: estes jogos apagam saves, e apagar
// no disco do HOSPEDEIRO destruiria a reprodutibilidade. O que se serve e a
// RESPOSTA do contrato (SUCCESS/EFAILED, TRUE/FALSE) sobre o que a VFS conhece
// -- o resultado da operacao, sem a operacao no disco.
// ===========================================================================
TEST(FileMgrDoSDK, OsSlotsDoIFileMgrSaoOsDoCabecalho) {
  // `AEEFile.h`, `INHERIT_IFileMgr`: `INHERIT_IBase` = 2 slots (AddRef,
  // Release), depois OpenFile, GetInfo, Remove, MkDir, RmDir, Test,
  // GetFreeSpace, GetLastError, EnumInit, EnumNext, Rename, ...
  EXPECT_EQ(brew_slots::kFileMgr_OpenFile, 2u);
  EXPECT_EQ(brew_slots::kFileMgr_GetInfo, 3u);
  EXPECT_EQ(brew_slots::kFileMgr_Remove, 4u);
  EXPECT_EQ(brew_slots::kFileMgr_MkDir, 5u);
  EXPECT_EQ(brew_slots::kFileMgr_RmDir, 6u);
  EXPECT_EQ(brew_slots::kFileMgr_Test, 7u);
  EXPECT_EQ(brew_slots::kFileMgr_GetFreeSpace, 8u);
  EXPECT_EQ(brew_slots::kFileMgr_GetLastError, 9u);
  EXPECT_EQ(brew_slots::kFileMgr_EnumInit, 10u);
  EXPECT_EQ(brew_slots::kFileMgr_EnumNext, 11u);
  EXPECT_EQ(brew_slots::kFileMgr_Rename, 12u);
}

TEST(FileMgrServido, ORemoveRespondeOContratoSemApagarNada) {
  // MEDIDO (corrida do corte): 4 titulos pedem `Remove` (game, abd,
  // allstarcards, torkandkral). O contrato (`AEEFile.h`, `IFILEMGR_Remove`)
  // responde SUCCESS ou EFAILED; a VFS e so de leitura, logo a resposta e
  // "existe? SUCCESS : EFAILED", e o ficheiro do hospedeiro fica intacto.
  {
    Bancada b;
    constexpr std::uint32_t kSaidaRemove = 1509;
    constexpr std::uint32_t kNome = 0x00094700u;
    const char* ficheiro = "app.log";
    for (std::uint32_t k = 0; ficheiro[k] != 0; ++k)
      b.Mem().Escrever8(kNome + k, static_cast<std::uint8_t>(ficheiro[k]));
    b.Mem().Escrever8(kNome + 7, 0);
    // Nenhuma VFS registada: o ficheiro "nao existe" -> EFAILED, e NAO ha falta.
    EXPECT_EQ(b.ChamaSaida(kSaidaRemove, kObjFileMgr, kNome), static_cast<std::uint32_t>(kAeeFailed));
    EXPECT_EQ(b.Faltas("IFileMgr::Remove"), 0u);
    const auto& p = b.Tr().ContagemPressupostos();
    EXPECT_NE(p.find("IFileMgr::Remove"), p.end())
        << "a resposta servida tem de ficar DECLARADA, e nao muda";
  }
  {
    // Com a VFS registada e o ficheiro la dentro: SUCCESS, e o ficheiro do
    // hospedeiro CONTINUA no disco -- a VFS respondeu, nao apagou.
    const auto pasta = std::filesystem::temp_directory_path() / "zb2_teste_filemgr_remove";
    std::filesystem::create_directories(pasta);
    std::ofstream f(pasta / "app.log", std::ios::binary);
    f << "x";
    f.close();
    Bancada b;
    b.AcessoAVfs().Registar(pasta.string());
    constexpr std::uint32_t kSaidaRemove = 1509;
    constexpr std::uint32_t kNome = 0x00094800u;
    const char* ficheiro = "app.log";
    for (std::uint32_t k = 0; ficheiro[k] != 0; ++k)
      b.Mem().Escrever8(kNome + k, static_cast<std::uint8_t>(ficheiro[k]));
    b.Mem().Escrever8(kNome + 7, 0);
    EXPECT_EQ(b.ChamaSaida(kSaidaRemove, kObjFileMgr, kNome), static_cast<std::uint32_t>(kAeeSuccess));
    EXPECT_TRUE(std::filesystem::exists(pasta / "app.log"))
        << "a VFS e so de leitura: o ficheiro do hospedeiro nao pode sumir";
    std::filesystem::remove_all(pasta);
  }
}

TEST(FileMgrServido, ORmDirRespondeNosDoisEnderecosDoSlot) {
  // MEDIDO (corrida do corte): 6 titulos pedem `RmDir` (alpineracerex,
  // pacmania, tekken2, gof, allstarcards, pbc) -- apagam saves velhos antes de
  // gravar. O `RmDir` do SDK e o slot 6 (`kFileMgr_RmDir`); a cablagem da
  // ferramenta (a tabela unica `kWire` de `tools/bateria.cpp`) aponta-lhe o id
  // 1547. O servico responde nos DOIS enderecos: o que o SDK diz (7000+6) e o
  // que a cablagem produz (1547), para a proxima correccao da cablagem nao
  // poder partir sem se ver.
  Bancada b;
  constexpr std::uint32_t kNome = 0x00094900u;
  const char* dir = "udata/gof_save_options";
  for (std::uint32_t k = 0; dir[k] != 0; ++k)
    b.Mem().Escrever8(kNome + k, static_cast<std::uint8_t>(dir[k]));
  b.Mem().Escrever8(kNome + 22, 0);
  // Sem VFS registada o caminho nao existe: EFAILED nos dois enderecos.
  EXPECT_EQ(b.ChamaSaida(1547, kObjFileMgr, kNome), static_cast<std::uint32_t>(kAeeFailed));
  EXPECT_EQ(b.ChamaSaida(kVtableFileMgr + brew_slots::kFileMgr_RmDir, kObjFileMgr, kNome),
            static_cast<std::uint32_t>(kAeeFailed));
  EXPECT_EQ(b.Faltas("IFileMgr::RmDir"), 0u);
  EXPECT_EQ(b.Faltas("IFileMgr::slot6"), 0u);
}

TEST(FileMgrServido, OEnumNextRespondeFalsoSemEnumeracao) {
  // MEDIDO (corrida do corte): gof, rmp e pbc chamam o slot 11 LOGO NO
  // ARRANQUE, sem `EnumInit` -- e a sonda de saves ("ha entradas?"). O slot 11
  // do SDK e o `EnumNext` (`kFileMgr_EnumNext`; ver tambem a chamada em
  // `gof.mod` 0x3688c: `ldr r2,[r1,#44]` = vtable[11]). Sem estado de
  // enumeracao a resposta honesta e FALSE (iteracao vazia), e o `GetLastError`
  // passa a EFAILED -- o contrato (`AEEFile.h`, `IFILEMGR_EnumNext`: FALSE
  // seguido de GetLastError devolve EFAILED mesmo quando a enumeracao acabou
  // bem).
  Bancada b;
  constexpr std::uint32_t kSlot =
      kVtableFileMgr + brew_slots::kFileMgr_EnumNext;  // 7000 + 11 = 7011
  constexpr std::uint32_t kInfo = 0x00094a00u;
  EXPECT_EQ(kSlot, 7011u);
  // FALSE = 0, e o FileInfo nao e tocado.
  b.Mem().Escrever32(kInfo, 0xDEADBEEFu);
  EXPECT_EQ(b.ChamaSaida(kSlot, kObjFileMgr, kInfo), 0u);
  EXPECT_EQ(b.Mem().Ler32(kInfo), 0xDEADBEEFu);
  EXPECT_EQ(b.Faltas("IFileMgr::slot11"), 0u);
  constexpr std::uint32_t kSaidaLastErr = 1512;
  EXPECT_EQ(b.ChamaSaida(kSaidaLastErr, kObjFileMgr),
            static_cast<std::uint32_t>(kAeeFailed));
  const auto& p = b.Tr().ContagemPressupostos();
  EXPECT_NE(p.find("IFileMgr::EnumNext"), p.end());
}

TEST(GetFreeSpace, OTotalEODoGuiaEOLivreFicaDeclaradoEContado) {
  // `ZeeboDeveloperGuide0.97.md:794`: "The total file system size available on
  // Zeebo is 1GB." O total era 1 MiB inventado.
  Bancada b;
  constexpr std::uint32_t kSaidaFree = 1511;
  constexpr std::uint32_t kPTotal = 0x00094500u;
  const std::uint32_t livre = b.ChamaSaida(kSaidaFree, 0x80060000u, kPTotal);
  EXPECT_EQ(b.Mem().Ler32(kPTotal), 0x40000000u) << "1 GiB, do guia do fabricante";
  EXPECT_EQ(livre, 0x04000000u);
  EXPECT_GT(livre, 64u * 1024u) << "o guia pede 64 KiB para save (:796); menos que isso "
                                   "faria o titulo desistir de gravar";
  const auto& p = b.Tr().ContagemPressupostos();
  ASSERT_NE(p.find("IFileMgr::GetFreeSpace"), p.end())
      << "o comentario antigo PROMETIA registo e nao havia nenhum";
}

// ===========================================================================
// `CheckPrivLevel`: as regras estao escritas no cabecalho, e nao eram lidas.
// ===========================================================================
TEST(CheckPrivLevel, RespondeSimAoQueExisteENaoAoQueNaoExiste) {
  Bancada b;
  constexpr std::uint32_t kSaidaCheckPriv = 1564;
  // "Every application is a member of the group 0" (AEEIShell.h:4391).
  EXPECT_EQ(b.ChamaSaida(kSaidaCheckPriv, 0x80020000u, 0, 1), 1u);
  // PL_FILE (0x0001, AEEPLPrivs.bid:9): ha IFileMgr e ha ficheiros.
  EXPECT_EQ(b.ChamaSaida(kSaidaCheckPriv, 0x80020000u, 0x0001u, 1), 1u);
  // PL_NETWORK (0x0002): nao ha rede nenhuma neste emulador.
  EXPECT_EQ(b.ChamaSaida(kSaidaCheckPriv, 0x80020000u, 0x0002u, 1), 0u);
  // PL_SYSTEM (0xffff): a soma de todos os bits, logo tambem os que nao ha.
  EXPECT_EQ(b.ChamaSaida(kSaidaCheckPriv, 0x80020000u, 0xFFFFu, 1), 0u);
  // Uma mascara MISTA (ficheiro + rede) tambem nao passa: o pedido e conjunto.
  EXPECT_EQ(b.ChamaSaida(kSaidaCheckPriv, 0x80020000u, 0x0003u, 1), 0u);
  EXPECT_EQ(b.Faltas("IShell::CheckPrivLevel"), 3u) << "cada nao fica com o nome";
}

TEST(CheckPrivLevel, UmaClasseQueSabemosCriarPassaEUmaQueNaoNao) {
  Bancada b;
  constexpr std::uint32_t kSaidaCheckPriv = 1564;
  // "the group that is equal to the application's class ID" nao se pode provar
  // sem titulo carregado; prova-se o outro ramo, o do CreateInstance.
  EXPECT_EQ(b.ChamaSaida(kSaidaCheckPriv, 0x80020000u, kClsidHid, 1), 1u);
  EXPECT_EQ(b.ChamaSaida(kSaidaCheckPriv, 0x80020000u, 0x01DEAD00u, 1), 0u);
  EXPECT_EQ(b.Faltas("IShell::CheckPrivLevel"), 1u);
}

TEST(QueryClass, OUnzipStreamEOMemStreamPassamASerClassesServidas) {
  // `0x01001014` = `AEECLSID_UNZIPSTREAM` e `0x0100100c` = `AEECLSID_MEMASTREAM`
  // (`AEEClassIDs.h:75,82` do SDK 4.0.2 da consola). A frente io2 passou a
  // SERVI-LOS no `CreateInstance` (objectos com vtable de verdade), logo o
  // `QueryClass` tem de dizer TRUE -- a classe que nao se sabe criar diz FALSE.
  Bancada b;
  constexpr std::uint32_t kQueryClass = 1541;  // kSlotIdQueryClass
  EXPECT_NE(b.ChamaSaida(kQueryClass, kObjShell, 0x01001014u), 0u)
      << "AEECLSID_UNZIPSTREAM e servido desde a frente io2";
  EXPECT_NE(b.ChamaSaida(kQueryClass, kObjShell, 0x0100100cu), 0u)
      << "AEECLSID_MEMASTREAM e servido desde a frente io2";

  // Uma classe desconhecida de verdade continua a dizer FALSE,
  EXPECT_EQ(b.ChamaSaida(kQueryClass, kObjShell, 0x01DEAD00u), 0u);
  // E as que ja eram servidas continuam a dizer TRUE.
  EXPECT_NE(b.ChamaSaida(kQueryClass, kObjShell, 0x01001003u), 0u) << "AEECLSID_FILEMGR";
  EXPECT_NE(b.ChamaSaida(kQueryClass, kObjShell, 0x01001001u), 0u) << "AEECLSID_DISPLAY";
  EXPECT_NE(b.ChamaSaida(kQueryClass, kObjShell, 0x01001002u), 0u) << "AEECLSID_HEAP";
}


// ===========================================================================
// A FRENTE io2: IMemAStream (0x0100100c) e IUnzipAStream (0x01001014).
//
// O allstarcards (medido em corrida_fmg.json) cria um MEMASTREAM sobre o blob
// gzip de um recurso, entrega-o ao UNZIPSTREAM por `SetStream` e LE 310 KB em
// laco. Estes testes provam a CABLAGEM (leem a vtable do objecto que o
// CreateInstance devolveu e chamam o endereco que a tabela diz) e nao um id
// interno -- armadilha 3 da sessao.
// ===========================================================================

// O indice de saida a partir do endereco que a vtable guarda.
std::uint32_t IndiceDeSaida(const Saidas& s, std::uint32_t endereco) {
  return (endereco - s.base) / s.passo;
}

TEST(UnzipStream, OCreateInstanceServeOsDoisObjectosComVTablesCabladas) {
  Bancada b;
  constexpr std::uint32_t kPpo = 0x00094000u;
  // 0x01001014 = AEECLSID_UNZIPSTREAM (AEEClassIDs.h:82, AEECLSID_CORE+20).
  EXPECT_EQ(b.ChamaSaida(kBaseDoShell + 2, kObjShell, 0x01001014u, kPpo), kAeeSuccess);
  const std::uint32_t unzip = b.Mem().Ler32(kPpo);
  EXPECT_NE(unzip, 0u);
  // 0x0100100c = AEECLSID_MEMASTREAM (AEEClassIDs.h:75, AEECLSID_CORE+12).
  EXPECT_EQ(b.ChamaSaida(kBaseDoShell + 2, kObjShell, 0x0100100cu, kPpo), kAeeSuccess);
  const std::uint32_t mem = b.Mem().Ler32(kPpo);
  EXPECT_NE(mem, 0u);
  EXPECT_NE(unzip, mem);

  // A vtable do unzip: IBase (0/1) + Readable/Read/Cancel/SetStream (2..5);
  // todos os slots 2..5 APONTAM PARA A FAIXA DE SAIDA e sao distintos entre si.
  const std::uint32_t vt_u = b.Mem().Ler32(unzip);
  EXPECT_NE(vt_u, 0u);
  std::uint32_t anteriores = 0;
  for (std::uint32_t slot = 2; slot <= 5; ++slot) {
    const std::uint32_t entra = b.Mem().Ler32(vt_u + slot * 4);
    EXPECT_TRUE(b.S().Contem(entra)) << "slot " << slot << " aponta para a faixa";
    EXPECT_NE(entra, anteriores) << "slot " << slot << " e distinto do anterior";
    anteriores = entra;
  }
  // A vtable do memstream: IBase + Readable/Read/Cancel/Set/SetEx (2..6).
  const std::uint32_t vt_m = b.Mem().Ler32(mem);
  EXPECT_NE(vt_m, 0u);
  for (std::uint32_t slot = 2; slot <= 6; ++slot) {
    EXPECT_TRUE(b.S().Contem(b.Mem().Ler32(vt_m + slot * 4)))
        << "slot " << slot << " do memstream aponta para a faixa";
  }
}

TEST(UnzipStream, DescomprimeOGzipEntreguePorUmMemStream) {
  Bancada b;
  constexpr std::uint32_t kPpo = 0x00094000u;
  constexpr std::uint32_t kDados = 0x00095000u;
  constexpr std::uint32_t kDest = 0x00096000u;
  // O MESMO PADRAO do allstarcards: um AEEResBlob (offset 32 + mime
  // application/x-gzip-compressed) seguido do stream gzip.
  static const std::uint8_t kBlob[123] = {
      0x20, 0x00, 0x61, 0x70, 0x70, 0x6c, 0x69, 0x63, 0x61, 0x74, 0x69, 0x6f,
      0x6e, 0x2f, 0x78, 0x2d, 0x67, 0x7a, 0x69, 0x70, 0x2d, 0x63, 0x6f, 0x6d,
      0x70, 0x72, 0x65, 0x73, 0x73, 0x65, 0x64, 0x00, 0x1f, 0x8b, 0x08, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x02, 0x03, 0xcb, 0x2f, 0x4a, 0x54, 0x48, 0x4a,
      0xcd, 0xd5, 0x51, 0xc8, 0x2c, 0x2e, 0xc9, 0x57, 0x48, 0x55, 0x28, 0xcd,
      0x55, 0x48, 0x2e, 0x2d, 0x2a, 0xce, 0x2f, 0xf2, 0x4c, 0xc9, 0x49, 0xd5,
      0x4b, 0xca, 0x2d, 0x50, 0x48, 0x49, 0x2d, 0x4e, 0xce, 0xcf, 0x2d, 0x28,
      0xca, 0xcc, 0xcd, 0x4c, 0xc9, 0x57, 0x28, 0x48, 0xcd, 0xc9, 0x57, 0xf0,
      0x0c, 0xcd, 0xab, 0xca, 0x2c, 0x70, 0x0c, 0x2e, 0x29, 0x4a, 0x4d, 0xcc,
      0xe5, 0xca, 0x1f, 0x2e, 0x46, 0x00, 0x00, 0xae, 0x7a, 0xee, 0x9a, 0x0c,
      0x01, 0x00, 0x00,
  };
  for (std::size_t k = 0; k < sizeof(kBlob); ++k) {
    b.Mem().Escrever8(kDados + static_cast<std::uint32_t>(k), kBlob[k]);
  }
  const std::string esperado =
      "ora bem, isto e um cursorIdle.bmp descomprimido pelo IUnzipAStream\n"
      "ora bem, isto e um cursorIdle.bmp descomprimido pelo IUnzipAStream\n"
      "ora bem, isto e um cursorIdle.bmp descomprimido pelo IUnzipAStream\n"
      "ora bem, isto e um cursorIdle.bmp descomprimido pelo IUnzipAStream\n";

  // MEMASTREAM.Set(po, pBuf, dwSize, dwOffset, bSysMem) -- slot 5 da vtable.
  ASSERT_EQ(b.ChamaSaida(kBaseDoShell + 2, kObjShell, 0x0100100cu, kPpo), kAeeSuccess);
  const std::uint32_t mem = b.Mem().Ler32(kPpo);
  const std::uint32_t vt_m = b.Mem().Ler32(mem);
  const std::uint32_t set_saida = b.Mem().Ler32(vt_m + 5 * 4);
  b.ChamaSaida(IndiceDeSaida(b.S(), set_saida), mem, kDados,
               static_cast<std::uint32_t>(sizeof(kBlob)), 0, 1);

  // UNZIPSTREAM.SetStream(po, pIAStream) -- slot 5 da vtable do unzip.
  ASSERT_EQ(b.ChamaSaida(kBaseDoShell + 2, kObjShell, 0x01001014u, kPpo), kAeeSuccess);
  const std::uint32_t unzip = b.Mem().Ler32(kPpo);
  const std::uint32_t vt_u = b.Mem().Ler32(unzip);
  const std::uint32_t ss_saida = b.Mem().Ler32(vt_u + 5 * 4);
  b.ChamaSaida(IndiceDeSaida(b.S(), ss_saida), unzip, mem);

  // UNZIPSTREAM.Read(po, pDest, nWant) -- slot 3. Le em pedacos ate EOF, como
  // o allstarcards (ele le em pedacos de 20 e testa o retorno).
  const std::uint32_t read_saida = b.Mem().Ler32(vt_u + 3 * 4);
  const std::uint32_t idx_read = IndiceDeSaida(b.S(), read_saida);
  std::string lido;
  for (int volta = 0; volta < 40; ++volta) {
    const std::uint32_t n = b.ChamaSaida(idx_read, unzip, kDest, 64);
    if (n == 0) break;
    for (std::uint32_t k = 0; k < n; ++k) {
      lido.push_back(static_cast<char>(b.Mem().Ler8(kDest + k)));
    }
  }
  EXPECT_EQ(lido, esperado);
}

TEST(UnzipStream, ReadSemOrigemDevolveFimDoStreamERegistaAFalta) {
  Bancada b;
  constexpr std::uint32_t kPpo = 0x00094000u;
  constexpr std::uint32_t kDest = 0x00096000u;
  ASSERT_EQ(b.ChamaSaida(kBaseDoShell + 2, kObjShell, 0x01001014u, kPpo), kAeeSuccess);
  const std::uint32_t unzip = b.Mem().Ler32(kPpo);
  const std::uint32_t vt_u = b.Mem().Ler32(unzip);
  const std::uint32_t read_saida = b.Mem().Ler32(vt_u + 3 * 4);
  // Sem SetStream nao ha origem: EOF (0), e a razao fica no traco -- um stub
  // que devolve 0 em silencio esconderia o ficheiro em falta.
  EXPECT_EQ(b.ChamaSaida(IndiceDeSaida(b.S(), read_saida), unzip, kDest, 64), 0u);
  EXPECT_EQ(b.Faltas("IUnzipAStream::Read"), 1u);
}



// ===========================================================================
// 12. A THREAD COOPERATIVA, PELO DESPACHO (frente thrd).
//
// Os testes de `classes_test.cpp` provam o CONTRATO (`PrepararRetomadaDeThread`
// etc.) chamado a mao. Estes provam a CABLAGEM: que o `Despacho::Correr`
// retoma uma thread pendente numa fronteira entre chamadas de API, que o
// `IThread_Suspend` devolve o controlo ao hospedeiro sem perder o sitio, e que
// o `ISHELL_Resume` (slot 36) enfileira a thread pelo callback de retomada --
// o ciclo completo que os 22 titulos da bateria pedem (`Start`).
//
// Hoje (antes desta frente) o teste e VERMELHO: a thread fica pendente e o pfn
// nunca corre; os marcadores ficam com o valor de partida.
// ===========================================================================
TEST(EntradaNoDespacho, UmaThreadAgendadaVoltaACorrerOPfnNasFronteirasDeApi) {
  Bancada b;
  const std::uint32_t th = static_cast<std::uint32_t>(Classe::kThread);
  const std::uint32_t kPpObj = 0x80091000u;
  const std::uint32_t kM1 = 0x80092000u;  // prova da 1a corrida (pos-Suspend)
  const std::uint32_t kM2 = 0x80093000u;  // prova da 2a corrida (pos-Resume)
  const std::uint32_t kRotinaDaThread = 0x00000600u;
  const std::uint32_t kR1Inicial = 0x4000;  // 16 KiB, o pedido mais medido

  // 1. O objecto do IThread pelo caminho do titulo: CreateInstance (slot 2 do
  //    IShell) com o AEECLSID_THREAD (0x01001017).
  ASSERT_EQ(b.ChamaSaida(kBaseDoShell + 2, kObjShell, 0x01001017u, kPpObj), kAeeSuccess);
  const std::uint32_t obj = b.Mem().Ler32(kPpObj);
  ASSERT_NE(obj, 0u) << "o CreateInstance tem de devolver o objecto do IThread";
  // A CABLAGEM, lida da TABELA (armadilha 3): o slot 7 aponta para o ramo do
  // Start do despacho.
  const std::uint32_t vt = b.Mem().Ler32(obj);
  EXPECT_EQ(b.Mem().Ler32(vt + brew_slots::kThread_Start * 4),
            b.S().Endereco(VtClasse(th) + brew_slots::kThread_Start));

  // 2. A FUNCAO DA THREAD, ARM, em kRotinaDaThread: suspende-se na 1a corrida e,
  //    na retomada (pelo Resume), continua apos o Suspend, grava os dois
  //    marcadores e volta -- e a funcao de entrada que volta TERMINA a thread.
  //      600 e5901000  ldr r1, [r0]         ; r1 = vtable
  //      604 e591c028  ldr ip, [r1, #0x28]  ; slot 10 = Suspend
  //      608 e1a0e00f  mov lr, pc
  //      60c e12fff1c  bx ip                ; Suspend(this): cede a vez
  //      610 e59f201c  ldr r2, [pc, #0x1c]  ; &m1 (0x634)
  //      614 e59f301c  ldr r3, [pc, #0x1c]  ; 0x11111111 (0x638)
  //      618 e5823000  str r3, [r2]         ; m1 = 0x11111111 (2a corrida)
  //      61c e59f2018  ldr r2, [pc, #0x18]  ; &m2 (0x63c)
  //      620 e59f3018  ldr r3, [pc, #0x18]  ; 0x22222222 (0x640)
  //      624 e5823000  str r3, [r2]         ; m2 = 0x22222222
  //      628 e12fff1e  bx lr                ; lr = sentinela: a thread termina
  const std::uint32_t pfn[] = {
      0xe5901000u, 0xe591c028u, 0xe1a0e00fu, 0xe12fff1cu,
      0xe59f201cu, 0xe59f301cu, 0xe5823000u,
      0xe59f2018u, 0xe59f3018u, 0xe5823000u,
      0xe12fff1eu,  // bx lr -- a funcao de entrada volta; a thread TERMINA
      0xe1a00000u, 0xe1a00000u,
      kM1, 0x11111111u, kM2, 0x22222222u,
  };
  for (std::size_t k = 0; k < sizeof(pfn) / sizeof(pfn[0]); ++k) {
    b.Mem().Escrever32(kRotinaDaThread + static_cast<std::uint32_t>(k * 4), pfn[k]);
  }

  // 3. O GUEST, ARM, em kRotina (0x200): Start; GetResumeCBK; Resume pelo slot
  //    36; GetResumeCBK de novo (a "proxima fronteira"); volta a sentinela.
  //      200 e5904000  ldr r4, [r0]         ; r4 = vtable do IThread
  //      204 e594c01c  ldr ip, [r4, #0x1c]  ; slot 7 = Start
  //      208 e1a0e00f  mov lr, pc
  //      20c e12fff1c  bx ip                ; Start(this,0x4000,pfn,arg)
  //      210 e594c02c  ldr ip, [r4, #0x2c]  ; slot 11 = GetResumeCBK
  //      214 e1a0e00f  mov lr, pc
  //      218 e12fff1c  bx ip                ; r0 = cbk -- a 1a retomada corre AQUI
  //      21c e1a01000  mov r1, r0           ; r1 = pcb
  //      220 e59f002c  ldr r0, [pc, #0x2c]  ; r0 = po (0x254)
  //      224 e59fc02c  ldr ip, [pc, #0x2c]  ; ip = saida do slot 36 (0x258)
  //      228 e1a0e00f  mov lr, pc
  //      22c e12fff1c  bx ip                ; ISHELL_Resume(po, cbk): enfileira
  //      230 e594c02c  ldr ip, [r4, #0x2c]  ; GetResumeCBK de novo
  //      234 e1a0e00f  mov lr, pc
  //      238 e12fff1c  bx ip                ; a 2a retomada corre AQUI
  //      23c e3a00000  mov r0, #0
  //      240 e59fe018  ldr lr, [pc, #0x18]  ; lr = [0x260] = sentinela
  //      244 e12fff1e  bx lr
  const std::uint32_t saida_resume = b.S().Endereco(kBaseDoShell + brew_slots::kShell_Resume);
  const std::uint32_t principal[] = {
      0xe5904000u, 0xe594c01cu, 0xe1a0e00fu, 0xe12fff1cu,
      0xe594c02cu, 0xe1a0e00fu, 0xe12fff1cu,
      0xe1a01000u,
      0xe59f002cu, 0xe59fc02cu, 0xe1a0e00fu, 0xe12fff1cu,
      0xe594c02cu, 0xe1a0e00fu, 0xe12fff1cu,
      0xe3a00000u, 0xe59fe018u, 0xe12fff1eu,
      0xe1a00000u, 0xe1a00000u, 0xe1a00000u,
      kObjShell,        // 0x254
      saida_resume,     // 0x258
      0xe1a00000u,      // 0x25c
      kSentinela,       // 0x260
  };
  for (std::size_t k = 0; k < sizeof(principal) / sizeof(principal[0]); ++k) {
    b.Mem().Escrever32(kRotina + static_cast<std::uint32_t>(k * 4), principal[k]);
  }

  // 4. O CICLO COMPLETO por DENTRO de `Despacho::Correr`, de uma so vez.
  b.Mem().Escrever32(kM1, 0xDEADBEEFu);
  b.Mem().Escrever32(kM2, 0xDEADBEEFu);
  b.Cpu().Set(kR0, obj);
  b.Cpu().Set(kR1, kR1Inicial);         // nStackSz
  b.Cpu().Set(kR2, kRotinaDaThread);    // pfStart
  b.Cpu().Set(kR3, 0x80200048u);        // pvStart (o valor MEDIDO no zeeboids)
  b.Cpu().Set(kLR, kSentinela);
  b.Cpu().Set(kPC, kRotina);
  const ResultadoFase r = b.D().Correr(b.Cpu(), 100000, kArg0);
  EXPECT_EQ(r.motivo, "retornou");
  // O Pfn TEM DE TER CORRIDO: m1 e m2 mudaram. Hoje (sem a cablagem) a thread
  // fica pendente, os marcadores ficam com o valor de partida -- VERMELHO.
  EXPECT_EQ(b.Mem().Ler32(kM1), 0x11111111u)
      << "a thread tem de CORRER o pfn na fronteira entre chamadas de API";
  EXPECT_EQ(b.Mem().Ler32(kM2), 0x22222222u)
      << "a 2a corrida (ISHELL_Resume) tem de continuar apos o Suspend";
  EXPECT_EQ(b.Faltas("IThread::Start"), 0u);
  EXPECT_EQ(b.Faltas("IShell::Resume"), 0u);
}


}  // namespace zb2::brew
