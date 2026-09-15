#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <string>

#include "core/brew/ajudantes.h"
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
  EXPECT_EQ(b.Mem().Ler32(bmp + 4), 1u) << "a contagem de referencias nasce a 1";
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
  EXPECT_EQ(b.Mem().Ler32(bmp + 4), 0u)
      << "o Release da IBase tinha de baixar a contagem de 1 para 0";
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

}  // namespace zb2::brew
