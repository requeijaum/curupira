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
    despacho_->DefinirVtableBitmap(saidas_.Endereco(kVtableBitmap));
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

}  // namespace zb2::brew
