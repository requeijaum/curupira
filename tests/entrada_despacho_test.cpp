#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/brew/ajudantes.h"
#include "core/brew/classes.h"
#include "core/brew/despacho.h"
// O PNG e o adler do teste da frente ishell2: um PNG de 2x2 e um contentor
// `.bar`/`.pod` sinteticos, montados com as funcoes do proprio motor
// (`Crc32DePng`, `Adler32`) -- nao ha codificador nesta arvore, e nao passa a
// haver.
#include "core/carga/inflate.h"
#include "core/carga/png.h"
#include "core/carga/bmp.h"
#include "core/brew/ihiddevice.h"
#include "core/cpu/arm_interpreter.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"
#include "tools/brew_slots.inc"
#include "tools/igles_slots.inc"

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
    traco_.JuntarDestino(&destino_);
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

  // UMA CHAMADA DO GUEST A UM ENDERECO -- e nao a um indice.
  //
  // E o que testa a CABLAGEM: o PC vai para a ENTRADA DA VTABLE que o objecto
  // tem, e nao para o endereco de saida que o teste "ja sabe". Um teste que chama
  // `ChamaSaida(<id interno>)` prova que o handler responde -- nao prova que o
  // objecto esta ligado a ele, e foi assim que um `SetTimer` cablado ao stub
  // errado passou uma corrida inteira sem sintoma.
  std::uint32_t ChamaEndereco(std::uint32_t endereco, std::uint32_t r0, std::uint32_t r1 = 0,
                              std::uint32_t r2 = 0, std::uint32_t r3 = 0,
                              std::uint64_t limite = 1000) {
    cpu_.Set(kR0, r0);
    cpu_.Set(kR1, r1);
    cpu_.Set(kR2, r2);
    cpu_.Set(kR3, r3);
    cpu_.Set(kSP, kArg0);
    mem_.Escrever32(kArg0, 0);
    cpu_.Set(kLR, kSentinela);
    cpu_.Set(kPC, endereco);
    const ResultadoFase r = despacho_->Correr(cpu_, limite, kArg0);
    (void)r;
    return cpu_.Get(kR0);
  }

  // A ENTRADA `slot` DA VTABLE DO OBJECTO -- a TABELA, e nao a resposta de quem a
  // escreveu.
  std::uint32_t EntradaDaVtable(std::uint32_t objeto, std::uint32_t slot) const {
    return mem_.Ler32(mem_.Ler32(objeto) + slot * 4);
  }

  Memoria& Mem() { return mem_; }
  Vfs& AcessoAVfs() { return vfs_; }
  Despacho& D() { return *despacho_; }
  ArmInterpreter& Cpu() { return cpu_; }
  const Saidas& S() const { return saidas_; }
  bool Instalada() const { return instalada_; }
  Traco& Tr() { return traco_; }
  const std::vector<Evento>& Eventos() const { return destino_.eventos; }

 private:
  Memoria mem_;
  Traco traco_{"teste_entrada_despacho", nullptr};
  DestinoMemoria destino_;
  Vfs vfs_;
  Alocador* al_ = nullptr;
  Despacho* despacho_ = nullptr;
  Saidas saidas_;
  ArmInterpreter cpu_{mem_, &traco_};
  bool instalada_ = false;
};

}  // namespace

TEST(PrefsDoShell, GuardaCopiaPorClasseESubstituiAVersao) {
  Bancada b;
  constexpr std::uint32_t kClasse = 0x0102f00du;
  constexpr std::uint16_t kVersao1 = 1u, kVersao2 = 2u;
  constexpr std::uint32_t kOrigem = 0x0020d000u, kDestino = 0x0020d100u;
  constexpr std::uint8_t kPrimeiro[] = {0x11u, 0x22u, 0x33u, 0x44u};
  constexpr std::uint8_t kSegundo[] = {0xaau, 0xbbu};

  // Sem registro, GetPrefs falha e nao toca no buffer do chamador.
  for (std::uint32_t i = 0; i < 4; ++i)
    b.Mem().Escrever8(kDestino + i, 0xe0u + i);
  EXPECT_EQ(b.ChamaSaida(kBaseDoShell + brew_slots::kShell_GetPrefs, kObjShell,
                         kClasse, kVersao1, kDestino, 4),
            static_cast<std::uint32_t>(kAeeFailed));
  for (std::uint32_t i = 0; i < 4; ++i)
    EXPECT_EQ(b.Mem().Ler8(kDestino + i), 0xe0u + i);

  for (std::uint32_t i = 0; i < sizeof(kPrimeiro); ++i)
    b.Mem().Escrever8(kOrigem + i, kPrimeiro[i]);
  EXPECT_EQ(b.ChamaSaida(kBaseDoShell + brew_slots::kShell_SetPrefs, kObjShell,
                         kClasse, kVersao1, kOrigem, sizeof(kPrimeiro)),
            static_cast<std::uint32_t>(kAeeSuccess));
  // A cache e dona dos bytes: o chamador pode reutilizar a origem.
  b.Mem().Escrever8(kOrigem, 0xffu);

  EXPECT_EQ(b.ChamaSaida(kBaseDoShell + brew_slots::kShell_GetPrefs, kObjShell,
                         kClasse, kVersao1, 0, 0),
            sizeof(kPrimeiro));
  for (std::uint32_t i = 0; i < 4; ++i)
    b.Mem().Escrever8(kDestino + i, 0xd0u + i);
  EXPECT_EQ(b.ChamaSaida(kBaseDoShell + brew_slots::kShell_GetPrefs, kObjShell,
                         kClasse, kVersao1, kDestino, 3),
            sizeof(kPrimeiro));
  for (std::uint32_t i = 0; i < 4; ++i)
    EXPECT_EQ(b.Mem().Ler8(kDestino + i), 0xd0u + i);
  EXPECT_EQ(b.ChamaSaida(kBaseDoShell + brew_slots::kShell_GetPrefs, kObjShell,
                         kClasse, kVersao1, kDestino, 4),
            static_cast<std::uint32_t>(kAeeSuccess));
  for (std::uint32_t i = 0; i < sizeof(kPrimeiro); ++i)
    EXPECT_EQ(b.Mem().Ler8(kDestino + i), kPrimeiro[i]);

  // Outra versao do mesmo CLSID substitui o registro anterior.
  for (std::uint32_t i = 0; i < sizeof(kSegundo); ++i)
    b.Mem().Escrever8(kOrigem + i, kSegundo[i]);
  EXPECT_EQ(b.ChamaSaida(kBaseDoShell + brew_slots::kShell_SetPrefs, kObjShell,
                         kClasse, kVersao2, kOrigem, sizeof(kSegundo)),
            static_cast<std::uint32_t>(kAeeSuccess));
  b.Mem().Escrever8(kDestino, 0x7eu);
  EXPECT_EQ(b.ChamaSaida(kBaseDoShell + brew_slots::kShell_GetPrefs, kObjShell,
                         kClasse, kVersao1, kDestino, 4),
            static_cast<std::uint32_t>(kAeeFailed));
  EXPECT_EQ(b.Mem().Ler8(kDestino), 0x7eu);
  EXPECT_EQ(b.ChamaSaida(kBaseDoShell + brew_slots::kShell_GetPrefs, kObjShell,
                         kClasse, kVersao2, kDestino, sizeof(kSegundo)),
            static_cast<std::uint32_t>(kAeeSuccess));
  for (std::uint32_t i = 0; i < sizeof(kSegundo); ++i)
    EXPECT_EQ(b.Mem().Ler8(kDestino + i), kSegundo[i]);
}

TEST(PrefsDoShell, ValidaBufferEExplicitaPedidoSincronoSemPersistir) {
  Bancada b;
  constexpr std::uint32_t kClasse = 0x0102f00eu;
  constexpr std::uint32_t kOrigem = 0x0020e000u, kFimDaPagina = 0x0020efffu;

  EXPECT_EQ(b.ChamaSaida(kBaseDoShell + brew_slots::kShell_SetPrefs, kObjShell,
                         kClasse, 1, 0, 1),
            static_cast<std::uint32_t>(kAeeBadParm));
  // So o ultimo byte da pagina existe; dois bytes ultrapassam a memoria do
  // guest.
  b.Mem().Escrever8(kFimDaPagina, 0x5au);
  EXPECT_EQ(b.ChamaSaida(kBaseDoShell + brew_slots::kShell_SetPrefs, kObjShell,
                         kClasse, 1, kFimDaPagina, 2),
            static_cast<std::uint32_t>(kAeeBadParm));

  b.Mem().Escrever8(kOrigem, 0x91u);
  b.Mem().Escrever8(kOrigem + 1, 0x92u);
  // O bit alto pede gravacao sincrona em NAND. A copia fica so neste processo;
  // a persistencia pedida e recusada explicitamente.
  EXPECT_EQ(b.ChamaSaida(kBaseDoShell + brew_slots::kShell_SetPrefs, kObjShell,
                         kClasse, 1, kOrigem, 0x8002u),
            static_cast<std::uint32_t>(kAeeUnsupported));
  EXPECT_EQ(b.ChamaSaida(kBaseDoShell + brew_slots::kShell_GetPrefs, kObjShell,
                         kClasse, 1, 0, 0),
            2u);
}

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

TEST(Ajudantes, OStrrchrDevolveAUltimaOcorrenciaInclusiveNoNul) {
  // `char *strrchr(const char *s1, int ch)` e AEEHelperFuncs[0x01C].
  // ConfTest/Dragon Vs Chicken usa-o para separar extensoes de media.
  Bancada b;
  constexpr std::uint32_t kTexto = 0x80210600u;
  const char* texto = "media/right.wav";
  for (std::uint32_t i = 0;; ++i) {
    b.Mem().Escrever8(kTexto + i, static_cast<std::uint8_t>(texto[i]));
    if (texto[i] == 0) break;
  }
  EXPECT_EQ(b.ChamaSaida(1582, kTexto, '.'), kTexto + 11);
  EXPECT_EQ(b.ChamaSaida(1582, kTexto, '/'), kTexto + 5);
  EXPECT_EQ(b.ChamaSaida(1582, kTexto, 'x'), 0u);
  // Como libc, procurar NUL devolve o endereco do terminador.
  EXPECT_EQ(b.ChamaSaida(1582, kTexto, 0), kTexto + 15);
  EXPECT_EQ(b.Mem().Ler32(kTabela + 0x01Cu),
            b.S().Endereco(1582));
  EXPECT_EQ(b.Faltas("AEEHelperFuncs[0x01c] strrchr"), 0u);
}

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

// ===========================================================================
// A FRENTE ibmap2: a resposta tem de ser sobre o OBJECT QUE O GUEST PERGUNTOU.
//
// Os dois ramos abaixo (`kSlotIdBitmapGetInfo` = 1572 e `kSlotIdBitmapQI` = 1565)
// sao os enderecos que a FERRAMENTA cabla nos slots 12 e 2 da vtable do IBitmap
// (`tools/bateria.cpp`, `kWire`). Chegam aqui com QUALQUER bitmap no r0.
//
// Ate o `CreateCompatibleBitmap` (slot 13) existir, o unico bitmap alcancavel era
// o do ECRA -- e por isso responder 640x480 e publicar o ecra valia sempre. Com a
// familia servida (frente ibmap2) o guest cria DIBs proprios e pergunta por ELES:
// um desenho composto no DIB errado e um desenho que nao aparece.
//
// MEDIDO no `pacmania` (lr=0x12d94): `CreateCompatibleBitmap` -> `QI(AEEIID_IDIB)`
// -> `GetInfo(nSize=12)`, e o jogo guarda esses numeros para escrever no `pBmp`.
// ===========================================================================
TEST(BitmapGetInfo, OGetInfoDeUmDibCriadoRespondeAsDimensoesDELE) {
  Bancada b;
  constexpr std::uint32_t kInfo = 0x80212000u;
  constexpr std::uint32_t kPpDib = 0x80212100u;
  constexpr std::uint32_t kGuarda = 0xDEADBEEFu;
  // `int CreateDIBitmap(IDisplay*, IDIB **ppIDIB, uint8 colorDepth, uint16 w,
  //                     uint16 h)` -- o `h` vai na pilha.
  b.ChamaSaida(1538, kObjDisplay, kPpDib, 16u, 78u, 64u);
  const std::uint32_t dib = b.Mem().Ler32(kPpDib);
  ASSERT_NE(dib, 0u) << "sem DIB criado nao ha o que perguntar";
  for (std::uint32_t k = 0; k < 4; ++k) b.Mem().Escrever32(kInfo + k * 4, kGuarda);
  b.ChamaSaida(1572, dib, kInfo, 12u);
  // VERMELHO NA BASE (antes da correccao): 640x480 e 16 -- o tamanho do ECRA.
  EXPECT_EQ(b.Mem().Ler32(kInfo + 0), 78u) << "a largura e a do DIB, nao a do ecra";
  EXPECT_EQ(b.Mem().Ler32(kInfo + 4), 64u) << "a altura e a do DIB, nao a do ecra";
  EXPECT_EQ(b.Mem().Ler32(kInfo + 8), 16u) << "RGB565 = 16 bits";
  // O CONTRATO DO ECRA NAO MUDA: o ecra responde com o tamanho da Tela.
  b.ChamaSaida(1572, kObjDibBase + 0x300, kInfo, 12u);
  EXPECT_EQ(b.Mem().Ler32(kInfo + 0), static_cast<std::uint32_t>(Tela::kLargura));
  EXPECT_EQ(b.Mem().Ler32(kInfo + 4), static_cast<std::uint32_t>(Tela::kAltura));
}

TEST(BitmapQI, OQueryInterfaceDeUmDibCriadoDevolveOProprioObjeto) {
  Bancada b;
  constexpr std::uint32_t kPpDib = 0x80212200u;
  constexpr std::uint32_t kPpo = 0x80212300u;
  b.ChamaSaida(1538, kObjDisplay, kPpDib, 16u, 320u, 240u);
  const std::uint32_t dib = b.Mem().Ler32(kPpDib);
  ASSERT_NE(dib, 0u);
  b.Mem().Escrever32(kPpo, 0xDEADBEEFu);
  // `int QueryInterface(IBitmap*, AEEIID, void**)` com `AEEIID_IDIB` (0x01001045).
  b.ChamaSaida(1565, dib, 0x01001045u, kPpo);
  // VERMELHO NA BASE (antes da correccao): sai o objecto do ECRA. Quem recebe
  // esse ponteiro escreve os pixels no ecra em vez de no bitmap dele.
  EXPECT_EQ(b.Mem().Ler32(kPpo), dib) << "o IDIB devolvido e o mesmo objecto pedido";
  // O ECRA continua a devolver o proprio objecto do ecra.
  b.Mem().Escrever32(kPpo, 0u);
  b.ChamaSaida(1565, kObjDibBase + 0x300, 0x01001045u, kPpo);
  EXPECT_EQ(b.Mem().Ler32(kPpo), kObjDibBase + 0x300u);

  // OS IIDs ANTIGOS DO DIB SAO A MESMA LISTA (`IidDeDib`): `0x0100102c` e o
  // `AEECLSID_DIB_20` (`AEEClassIDs.h:114`) e `0x01001029` (`CORE+41`) e o valor
  // do DIB de uma versao anterior do BREW -- MEDIDO: o `fifa09` e o `zenonia`
  // pedem exactamente esse IID ao bitmap que acabaram de criar
  // (`CreateCompatibleBitmap` -> `QI(0x01001029)`), e o IID nao se serve so
  // porque a ferramenta cablou este endereco no slot 2 em vez do da familia.
  for (const std::uint32_t iid : {0x0100102cu, 0x01001029u}) {
    b.Mem().Escrever32(kPpo, 0u);
    b.ChamaSaida(1565, dib, iid, kPpo);
    EXPECT_EQ(b.Mem().Ler32(kPpo), dib) << "iid=0x" << std::hex << iid;
  }
  // Um IID de OUTRA interface continua a ser recusado com o nome, e nao com o
  // bitmap (o `0x01001001` e o `AEECLSID_DISPLAY`).
  b.Mem().Escrever32(kPpo, 0xDEADBEEFu);
  EXPECT_EQ(b.ChamaSaida(1565, dib, 0x01001001u, kPpo), kAeeUnsupported);
  EXPECT_EQ(b.Mem().Ler32(kPpo), 0u) << "recusa nao deixa o ponteiro por limpar";
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
TEST(RecusasMudas, OWritePassaAContar) {
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

  // O `ISQLMgr::Open` SAIU DESTE TESTE: ele deixou de ser recusa. Passou a ser
  // SERVIDO -- devolve um banco no `r2` e `AEE_SUCCESS` --, e o que ele faz tem
  // testes proprios em `SqlDoZWheel.*` (o `Open`, o `Exec`, a tabela do `DBINFO`).
  // Deixa-lo aqui a espera de `AEE_UNSUPPORTED` seria o teste a guardar o defeito.
  EXPECT_EQ(b.ChamaSaida(kSaidaSqlOpen, 0x80060200u, kNome, 0x00094200u, 0x00094300u),
            kAeeSuccess);
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
// (slots 10/11), com o contrato do SDK sobre a VFS.
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

TEST(RecursosNoShell, LoadResDataDoSlot18EAtendidoComONome) {
  Bancada b;
  constexpr std::uint32_t kNome = 0x000949C0u;
  const char* ficheiro = "ausente.bar";
  for (std::uint32_t k = 0; ficheiro[k] != 0; ++k)
    b.Mem().Escrever8(kNome + k, static_cast<std::uint8_t>(ficheiro[k]));
  b.Mem().Escrever8(kNome + 10, 0);
  // Falha de recurso retorna ponteiro nulo, não EUNSUPPORTED do slot genérico.
  EXPECT_EQ(b.ChamaSaida(kBaseDoShell + brew_slots::kShell_LoadResData, 0x80020000u,
                         kNome, 5001u, 6u), 0u);
  EXPECT_EQ(b.Faltas("IShell::slot18"), 0u);
  EXPECT_EQ(b.Faltas("IShell::LoadResData"), 1u);
  EXPECT_EQ(b.Faltas("IShell::LoadResDataEx"), 0u);
}

TEST(FileMgrServido, OMkDirRespondeSuccessNosDoisEnderecosDoSlot) {
  Bancada b;
  constexpr std::uint32_t kNome = 0x00094980u;
  const char* dir = "udata";
  for (std::uint32_t k = 0; dir[k] != 0; ++k)
    b.Mem().Escrever8(kNome + k, static_cast<std::uint8_t>(dir[k]));
  b.Mem().Escrever8(kNome + 5, 0);

  EXPECT_EQ(b.ChamaSaida(1544, kObjFileMgr, kNome), static_cast<std::uint32_t>(kAeeSuccess));
  EXPECT_EQ(b.ChamaSaida(kVtableFileMgr + brew_slots::kFileMgr_MkDir, kObjFileMgr, kNome),
            static_cast<std::uint32_t>(kAeeSuccess));
  EXPECT_EQ(b.Faltas("IFileMgr::MkDir"), 0u);
  EXPECT_EQ(b.Faltas("IFileMgr::slot5"), 0u);
  const auto& p = b.Tr().ContagemPressupostos();
  EXPECT_NE(p.find("IFileMgr::MkDir"), p.end());
}

TEST(FileMgrServido, OGetInfoRespondeComAStructDoCabecalhoENomeiaOslot) {
  // `int GetInfo(IFileMgr *po, const char *pszName, FileInfo *pInfo)` -- slot 3
  // (`AEEFile.h:213`). MEDIDO: o `ridgeracer` pede-o 2x, e a recusa dava-lhe o
  // nome GENERICO ("IFileMgr::slot3"): um numero, e nao a operacao.
  //
  // Sem VFS registada o ficheiro nao existe -> `EFAILD`, e a falta com o nome
  // generico tem de DESAPARECER (o ramo atende o slot).
  Bancada b;
  constexpr std::uint32_t kNome = 0x00094A00u;
  const char* ficheiro = "data/ridgeracer.pak";
  for (std::uint32_t k = 0; ficheiro[k] != 0; ++k)
    b.Mem().Escrever8(kNome + k, static_cast<std::uint8_t>(ficheiro[k]));
  b.Mem().Escrever8(kNome + 20, 0);
  EXPECT_EQ(b.ChamaSaida(kVtableFileMgr + brew_slots::kFileMgr_GetInfo, kObjFileMgr, kNome, 0u),
            static_cast<std::uint32_t>(kAeeFailed));
  EXPECT_EQ(b.Faltas("IFileMgr::slot3"), 0u)
      << "o slot 3 passou a ser atendido pelo nome: a falta generica nao pode ficar";
}

TEST(FileMgrServido, OEnumInitDaRaizDeclaraEnumeracaoVazia) {
  Bancada b;
  constexpr std::uint32_t kSlot = kVtableFileMgr + brew_slots::kFileMgr_EnumInit;
  constexpr std::uint32_t kSaidaLastErr = 1512;
  EXPECT_EQ(kSlot, 7010u);
  // NULL e cadeia vazia significam a raiz. A VFS ainda nao expoe lista de
  // assets: inicia-se uma enumeracao vazia, mas a API existe e responde sucesso.
  EXPECT_EQ(b.ChamaSaida(kSlot, kObjFileMgr, 0, 0), static_cast<std::uint32_t>(kAeeSuccess));
  EXPECT_EQ(b.ChamaSaida(kSaidaLastErr, kObjFileMgr), static_cast<std::uint32_t>(kAeeSuccess));
  constexpr std::uint32_t kRaiz = 0x00094A80u;
  b.Mem().Escrever8(kRaiz, 0);
  EXPECT_EQ(b.ChamaSaida(kSlot, kObjFileMgr, kRaiz, 1), static_cast<std::uint32_t>(kAeeSuccess));
  constexpr std::uint32_t kFsRaiz = 0x00094AC0u;
  const char* fs_raiz = "fs:/~/";
  for (std::uint32_t i = 0; fs_raiz[i] != 0; ++i)
    b.Mem().Escrever8(kFsRaiz + i, static_cast<std::uint8_t>(fs_raiz[i]));
  b.Mem().Escrever8(kFsRaiz + 6, 0);
  EXPECT_EQ(b.ChamaSaida(kSlot, kObjFileMgr, kFsRaiz, 0), static_cast<std::uint32_t>(kAeeSuccess));
  EXPECT_EQ(b.Faltas("IFileMgr::slot10"), 0u);
}

TEST(FileMgrServido, OEnumNextRespondeFalsoDepoisDoEnumInitVazio) {
  // MEDIDO no gof/rmp/pbc apos a cablagem correta: slot 10 (`EnumInit`) recebe
  // cadeia vazia e bDirs=FALSE, devolve SUCCESS; em seguida slot 11 (`EnumNext`)
  // e a sonda de saves "ha entradas?". A raiz vazia tem iteracao vazia: FALSE
  // sem tocar FileInfo e GetLastError=EFAILED, como AEEFile.h documenta para o
  // fim normal da enumeracao.
  Bancada b;
  constexpr std::uint32_t kSlot =
      kVtableFileMgr + brew_slots::kFileMgr_EnumNext;  // 7000 + 11 = 7011
  constexpr std::uint32_t kInfo = 0x00094a00u;
  EXPECT_EQ(kSlot, 7011u);
  ASSERT_EQ(b.ChamaSaida(kVtableFileMgr + brew_slots::kFileMgr_EnumInit,
                          kObjFileMgr, 0, 0),
            static_cast<std::uint32_t>(kAeeSuccess));
  // FALSE = 0, e o FileInfo nao e tocado.
  b.Mem().Escrever32(kInfo, 0xDEADBEEFu);
  EXPECT_EQ(b.ChamaSaida(kSlot, kObjFileMgr, kInfo), 0u);
  EXPECT_EQ(b.Mem().Ler32(kInfo), 0xDEADBEEFu);
  EXPECT_EQ(b.Faltas("IFileMgr::slot11"), 0u);
  constexpr std::uint32_t kSaidaLastErr = 1512;
  EXPECT_EQ(b.ChamaSaida(kSaidaLastErr, kObjFileMgr),
            static_cast<std::uint32_t>(kAeeFailed));
  const auto& pressupostos = b.Tr().ContagemPressupostos();
  EXPECT_NE(pressupostos.find("IFileMgr::EnumNext"), pressupostos.end());
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

// ===========================================================================
// O PARK DA ESPERA (frente park): uma thread que SO ESPERA prende o laco de
// eventos, e a fronteira entre chamadas de API e o ponto onde a estacionar
// (como se tivesse chamado Suspend) e devolver a vez ao temporizador.
//
// VERMELHO HOJE (sem o park): a thread roda e nunca cede; o `agora_ms_` fica
// CONGELADO durante a volta dela (o temporizador nao dispara), e o marcador
// fica com o valor de partida.
// ===========================================================================
TEST(EntradaNoDespacho, UmaThreadQueSoEsperaEParkadaENaoPrendeOLacoDeEventos) {
  Bancada b;
  // Os ids de saida do despacho (despacho.cpp): `kSlotIdSetTimer` = 1520.
  // O relogio vai pela CABLAGEM (armadilha 3): a tabela AEEHelperFuncs em
  // `kTabela`, no offset 0x0b0 = aee_GetUpTimeMS.
  const std::uint32_t kSetTimer = 1520;
  const std::uint32_t kPpObj = 0x80091000u;
  const std::uint32_t kMarcadorDoTemporizador = 0x00000310u;
  const std::uint32_t kRotinaDaThread = 0x00000600u;
  const std::uint32_t kCallbackDoTemporizador = 0x00000500u;

  // 1. O objecto do IThread (CreateInstance) -- e a PILHA da thread, para o
  //    pfn poder estar dentro da faixa do modulo (0x600) no Start.
  ASSERT_EQ(b.ChamaSaida(kBaseDoShell + 2, kObjShell, 0x01001017u, kPpObj), kAeeSuccess);
  const std::uint32_t obj = b.Mem().Ler32(kPpObj);
  ASSERT_NE(obj, 0u);

  // 2. O CALLBACK DO TEMPORIZADOR em kCallbackDoTemporizador: escreve o
  //    marcador e volta. E a prova de que o laco de eventos CORREU enquanto a
  //    thread esperava.
  //      500 e3a02c03  mov r2, #0x310      (kMarcadorDoTemporizador)
  //      504 e59f3008  ldr r3, [pc, #8]    ; @0x514 = 0x33333333
  //      508 e5823000  str r3, [r2]        ; marcador = 0x33333333
  //      50c e12fff1e  bx lr
  const std::uint32_t cbk[] = {0xe3a02e31u, 0xe59f3008u, 0xe5823000u, 0xe12fff1eu,
                               0xe1a00000u, 0x33333333u};
  for (std::size_t k = 0; k < sizeof(cbk) / sizeof(cbk[0]); ++k) {
    b.Mem().Escrever32(kCallbackDoTemporizador + static_cast<std::uint32_t>(k * 4), cbk[k]);
  }

  // 3. A THREAD QUE SO ESPERA em kRotinaDaThread: laco que so le o relogio
  //    virtual (aee_GetUpTimeMS pela TABELA -- o valor em kTabela+0x0b0), sem
  //    mudar nada e sem nunca ceder.
  //      600 e59f300c  ldr r3, [pc, #12]   ; @0x614 = kTabela+0x0b0
  //      604 e5930000  ldr r0, [r3]        ; r0 = tabela[0x0b0] = saida do relogio
  //      608 e1a0e00f  mov lr, pc
  //      60c e12fff10  bx r0               ; aee_GetUpTimeMS()
  //      610 e1a00000  (pad)
  //      614 <kTabela+0x0b0>
  //      618 eafffff8  b 0x600
  const std::uint32_t pfn[] = {0xe59f300cu, 0xe5930000u, 0xe1a0e00fu, 0xe12fff10u,
                               0xe1a00000u, kTabela + 0x0b0u, 0xeafffff8u};
  for (std::size_t k = 0; k < sizeof(pfn) / sizeof(pfn[0]); ++k) {
    b.Mem().Escrever32(kRotinaDaThread + static_cast<std::uint32_t>(k * 4), pfn[k]);
  }

  // 4. O GUEST, ARM, em kRotina (0x200): arma o temporizador, inicia a thread
  //    e fica num laco (o tempo virtual avanca fora da thread).
  //      200 e5900038  ldr r0, [pc, #0x38] ; @0x240 = saida do SetTimer
  //      204 e3a0100a  mov r1, #10         ; 10 ms
  //      208 e5902034  ldr r2, [pc, #0x34] ; @0x244 = 0x500 (callback)
  //      20c e3a03099  mov r3, #0x99
  //      210 e1a0e00f  mov lr, pc
  //      214 e12fff10  bx r0               ; SetTimer(po, 10, 0x500, 0x99)
  //      218 e5904028  ldr r4, [pc, #0x28] ; @0x248 = obj
  //      21c e1a00004  mov r0, r4          ; this
  //      220 e3a01a04  mov r1, #0x4000     ; 16 KiB
  //      224 e5902020  ldr r2, [pc, #0x20] ; @0x24c = 0x600 (pfn da thread)
  //      228 e3a03007  mov r3, #0x7        ; pvStart
  //      22c e5944000  ldr r4, [r4]        ; vtable
  //      230 e594c01c  ldr ip, [r4, #0x1c] ; slot 7 = Start
  //      234 e1a0e00f  mov lr, pc
  //      238 e12fff1c  bx ip               ; Start(obj, 0x4000, 0x600, 7)
  //      23c eafffffe  b 0x23c             ; o hospedeiro fica no laco
  const std::uint32_t saida_set_timer = b.S().Endereco(kSetTimer);
  const std::uint32_t principal[] = {
      0xe59f0038u, 0xe3a0100au, 0xe59f2034u, 0xe3a03099u, 0xe1a0e00fu, 0xe12fff10u,
      0xe59f4028u, 0xe1a00004u, 0xe3a01901u, 0xe59f2020u, 0xe3a03007u, 0xe5944000u,
      0xe594c01cu, 0xe1a0e00fu, 0xe12fff1cu, 0xeafffffeu,
      saida_set_timer,  // 0x240
      kCallbackDoTemporizador,  // 0x244
      obj,             // 0x248
      kRotinaDaThread, // 0x24c
  };
  for (std::size_t k = 0; k < sizeof(principal) / sizeof(principal[0]); ++k) {
    b.Mem().Escrever32(kRotina + static_cast<std::uint32_t>(k * 4), principal[k]);
  }

  // 5. O CICLO por DENTRO de `Despacho::Correr`, de uma so vez.
  b.Mem().Escrever32(kMarcadorDoTemporizador, 0xDEADBEEFu);
  // Os registadores de entrada nao importam: o guest carrega tudo de literais.
  b.Cpu().Set(kLR, kSentinela);
  b.Cpu().Set(kPC, kRotina);
  const ResultadoFase r = b.D().Correr(b.Cpu(), 100000, kArg0);
  // O PARK TEM DE TER DEVOLVIDO A VEZ AO LACO DE EVENTOS: o temporizador de 10
  // ms disparou e o marcador mudou. Hoje (sem o park) a thread nunca cede, o
  // relogio virtual fica congelado durante a volta dela e o marcador fica com
  // o valor de partida -- VERMELHO.
  EXPECT_EQ(b.Mem().Ler32(kMarcadorDoTemporizador), 0x33333333u)
      << "o laco de eventos tem de CORRER durante a espera da thread";
  (void)r;
}

// ===========================================================================
// A FRENTE tela: A TELA DO MOTOR DO IGLES11 (o IGL de 40300+)
// ===========================================================================
//
// O IGL de 30000 recebe a Tela dentro do `InstalarGl` (frente raster). O
// IGLES11 -- o objecto que os 8 titulos 3D usam -- tem um motor proprio
// (`classes.cpp`, `ConstruirIgles`) e a assembleia NAO o ligava a Tela: o
// `Clear` desse objecto recusava a ESCRITA ("o IGL NAO TEM TELA LIGADA",
// medido no ridgeracer: IGLES11::Clear 1x, pixels=0 -- o fio que as frentes
// glbloco e qualcomm deixaram encostado).
//
// A ARMADILHA 3 desta casa: um teste que chamasse `AtenderClasse` a um motor
// construido a mao nao testaria a CABLAGEM. Este corre a MESMA assembleia que
// a bateria usa (`InstalarAjudantes` -> `ConstruirClasses` -> `ConstruirIgles`)
// e pede o `Clear` pelo endereco de saida da vtable do IGLES11, com o pc do
// guest a entrar la dentro -- o caminho real. VERMELHO antes da frente, verde
// depois.

TEST(FrenteTela, AAssembleiaLigaATelaAoMotorDoIgles11) {
  Bancada b;
  b.D().TelaRef().Limpar();
  const std::uint32_t r0 = b.ChamaSaida(kVtableIgles + igles_slots::kIgles_Clear,
                                        kObjetoIgles, gl_slots::GL_COLOR_BUFFER_BIT);
  EXPECT_EQ(r0, kAeeSuccess);
  EXPECT_EQ(b.Faltas("IGLES11::Clear"), 0u);
  EXPECT_EQ(b.D().TelaRef().Escritos(),
            static_cast<std::uint32_t>(Tela::kLargura * Tela::kAltura))
      << "o Clear do IGLES11 tem de escrever na TELA DO DESPACHO, e nao recusar";
  EXPECT_EQ(b.D().TelaRef().CoresDistintas(), 1u);
}

// ===========================================================================
// A FRENTE ishell2: `IShell::DetectType` (slot 43) e `IShell::LoadResObject`
// (slot 19) -- os dois servidos pelo despacho, e nao pelo ramo generico.
// ===========================================================================
//
// A ARMADILHA 3 desta casa esta aqui tratada: os testes NAO chamam um id
// interno. Leem o SLOT da vtable do objecto do shell (que e o que o guest le) e
// entram no despacho PELO ENDERECO que la estiver -- se a cablagem desaparecer,
// o teste entra no stub que recusa e fica vermelho no `Faltas(...)`.
namespace {

constexpr std::uint32_t kNomeNoGuest = 0x80091000u;    // uma cadeia escrita pelo teste
constexpr std::uint32_t kBytesNoGuest = 0x80092000u;   // o buffer do `cpBuf`
constexpr std::uint32_t kPalavraNoGuest = 0x80093000u;  // o `pdwSize`/`pnBufSize`
constexpr std::uint32_t kPpNoGuest = 0x80093010u;       // o `pcpszMIME`

void EscreverCadeia(Memoria& m, std::uint32_t p, const std::string& s) {
  for (std::size_t k = 0; k < s.size(); ++k) {
    m.Escrever8(p + static_cast<std::uint32_t>(k), static_cast<std::uint8_t>(s[k]));
  }
  m.Escrever8(p + static_cast<std::uint32_t>(s.size()), 0);
}

std::string LerCadeia(Memoria& m, std::uint32_t p) {
  std::string s;
  for (std::uint32_t k = 0; k < 64; ++k) {
    const char c = static_cast<char>(m.Ler8(p + k));
    if (c == 0) break;
    s.push_back(c);
  }
  return s;
}

// UM PNG DE 2x2 RGBA, com as regras do formato e um bloco deflate STORED: as
// mesmas funcoes do motor escrevem o CRC e o adler (`tests/classes_test.cpp`,
// `PngDoTeste`). O canto superior esquerdo e VERMELHO puro (RGB565 = 0xF800) e o
// ultimo pixel e transparente -- ha um pixel e um `tem_alpha` para conferir.
std::vector<std::uint8_t> PngDoIshell2() {
  const std::vector<std::uint8_t> cru = {0,   255, 0, 0,   255, 0, 255, 0,
                                         255, 0,   0, 0,   255, 255, 0, 0, 0, 0};
  std::vector<std::uint8_t> zlib_stream = {0x78u, 0x01u, 0x01u};
  const std::uint16_t n = static_cast<std::uint16_t>(cru.size());
  zlib_stream.push_back(static_cast<std::uint8_t>(n & 0xffu));
  zlib_stream.push_back(static_cast<std::uint8_t>((n >> 8) & 0xffu));
  zlib_stream.push_back(static_cast<std::uint8_t>((~n) & 0xffu));
  zlib_stream.push_back(static_cast<std::uint8_t>(((~n) >> 8) & 0xffu));
  zlib_stream.insert(zlib_stream.end(), cru.begin(), cru.end());
  const std::uint32_t adler = Adler32(cru.data(), cru.size());
  for (int i = 3; i >= 0; --i) {
    zlib_stream.push_back(static_cast<std::uint8_t>((adler >> (8 * i)) & 0xffu));
  }

  std::vector<std::uint8_t> v = {0x89u, 0x50u, 0x4eu, 0x47u, 0x0du, 0x0au, 0x1au, 0x0au};
  auto chunk = [&](const char* tipo, const std::vector<std::uint8_t>& dados) {
    const std::uint32_t tam = static_cast<std::uint32_t>(dados.size());
    for (int i = 3; i >= 0; --i) {
      v.push_back(static_cast<std::uint8_t>((tam >> (8 * i)) & 0xffu));
    }
    std::vector<std::uint8_t> com_tipo(tipo, tipo + 4);
    com_tipo.insert(com_tipo.end(), dados.begin(), dados.end());
    v.insert(v.end(), com_tipo.begin(), com_tipo.end());
    const std::uint32_t crc = Crc32DePng(com_tipo.data(), com_tipo.size());
    for (int i = 3; i >= 0; --i) {
      v.push_back(static_cast<std::uint8_t>((crc >> (8 * i)) & 0xffu));
    }
  };
  const std::vector<std::uint8_t> ihdr = {0, 0, 0, 2, 0, 0, 0, 2, 8, 6, 0, 0, 0};
  chunk("IHDR", ihdr);
  chunk("IDAT", zlib_stream);
  chunk("IEND", {});
  return v;
}

// BMP BI_RGB 8 bpp 2x2 completo, com paleta BGR0. A primeira linha visual e
// vermelho/verde; o ficheiro positivo guarda-a por ultimo. Os pixels permitem
// provar que o LoadResObject nao devolveu um IDIB vazio ou inventado.
std::vector<std::uint8_t> BmpDoIshell2() {
  // BITMAPINFOHEADER + paleta BGR0 de quatro cores + 2 linhas 8bpp. O BMP
  // positivo guarda a linha visual superior (vermelho/verde) por ultimo.
  return {0x42, 0x4d, 78, 0, 0, 0, 0, 0, 0, 0, 70, 0, 0, 0,
          40, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 1, 0, 8, 0,
          0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0,
          0, 0, 0, 0,  // biClrImportant
          0, 0, 0, 0,  // paleta 0: preto
          0, 0, 255, 0,  // paleta 1: vermelho
          0, 255, 0, 0,  // paleta 2: verde
          255, 0, 0, 0,  // paleta 3: azul
          3, 0, 0, 0,  // fundo: azul, preto, padding
          1, 2, 0, 0};  // topo: vermelho, verde, padding
}

// O CONTENTOR DO `.bar`/`.pod`, montado com as regras medidas (`tests/bar_test.cpp`:
// cabecalho de 32 bytes, registos de 8 e a tabela de deslocamentos; o ultimo
// deslocamento E o tamanho do ficheiro).
struct RegistoDoContentor {
  std::uint16_t tipo, primeiro_id, delta, primeiro_indice;
};

void Escrever16Em(std::vector<std::uint8_t>* b, std::size_t pos, std::uint16_t v) {
  (*b)[pos] = static_cast<std::uint8_t>(v & 0xff);
  (*b)[pos + 1] = static_cast<std::uint8_t>((v >> 8) & 0xff);
}

void Escrever32Em(std::vector<std::uint8_t>* b, std::size_t pos, std::uint32_t v) {
  for (int k = 0; k < 4; ++k) {
    (*b)[pos + static_cast<std::size_t>(k)] =
        static_cast<std::uint8_t>((v >> (8 * k)) & 0xffu);
  }
}

// O `AEEResBlob` do tipo 6: deslocamento, zero, mime terminado em NUL, e o dado.
std::vector<std::uint8_t> BlobDoIshell2(std::uint8_t deslocamento, const std::string& mime,
                                        const std::vector<std::uint8_t>& dado) {
  std::vector<std::uint8_t> b;
  b.push_back(deslocamento);
  b.push_back(0);
  b.insert(b.end(), mime.begin(), mime.end());
  b.push_back(0);
  while (b.size() < deslocamento) b.push_back(0);
  b.insert(b.end(), dado.begin(), dado.end());
  return b;
}

std::vector<std::uint8_t> ContentorDoIshell2(const std::vector<RegistoDoContentor>& registos,
                                             const std::vector<std::vector<std::uint8_t>>& recursos) {
  std::uint32_t num_ids = 0;
  for (const RegistoDoContentor& r : registos) num_ids += static_cast<std::uint32_t>(r.delta) + 1u;
  const std::uint32_t n_registos = static_cast<std::uint32_t>(registos.size());
  const std::uint32_t off_registos = 32;
  const std::uint32_t tam_registos = 8 * n_registos;
  const std::uint32_t off_indices = off_registos + tam_registos;
  const std::uint32_t off_dados = off_indices + 4 * (num_ids + 1);

  std::vector<std::uint8_t> dados;
  std::vector<std::uint32_t> indices;
  indices.push_back(off_dados);
  for (const std::vector<std::uint8_t>& r : recursos) {
    dados.insert(dados.end(), r.begin(), r.end());
    indices.push_back(off_dados + static_cast<std::uint32_t>(dados.size()));
  }
  while (indices.size() < num_ids + 1) indices.push_back(indices.back());

  std::vector<std::uint8_t> b(off_dados + dados.size(), 0);
  Escrever16Em(&b, 0, 0x0011);
  Escrever16Em(&b, 2, 1);
  Escrever16Em(&b, 4, 1);
  Escrever16Em(&b, 6, static_cast<std::uint16_t>(n_registos));
  Escrever32Em(&b, 8, off_registos);
  Escrever32Em(&b, 12, tam_registos);
  Escrever32Em(&b, 16, off_indices);
  Escrever32Em(&b, 20, num_ids);
  Escrever32Em(&b, 24, off_dados);
  Escrever32Em(&b, 28, static_cast<std::uint32_t>(dados.size()));
  for (std::uint32_t k = 0; k < n_registos; ++k) {
    Escrever16Em(&b, off_registos + 8 * k + 0, registos[k].tipo);
    Escrever16Em(&b, off_registos + 8 * k + 2, registos[k].primeiro_id);
    Escrever16Em(&b, off_registos + 8 * k + 4, registos[k].delta);
    Escrever16Em(&b, off_registos + 8 * k + 6, registos[k].primeiro_indice);
  }
  for (std::size_t k = 0; k < indices.size(); ++k) Escrever32Em(&b, off_indices + 4 * k, indices[k]);
  for (std::size_t k = 0; k < dados.size(); ++k) b[off_dados + k] = dados[k];
  return b;
}

// A PASTA DO TITULO: `<tmp>/zb2_ishell2_pasta/<titulo>/`, com o que cada teste
// la escrever. A VFS e registada DEPOIS de os ficheiros existirem (o `Registar`
// enumera a pasta), como a bateria faz.
class PastaDoTitulo {
 public:
  PastaDoTitulo() {
    raiz_ = std::filesystem::temp_directory_path() / "zb2_ishell2_pasta";
    pasta_ = raiz_ / "titulo";
    std::error_code ec;
    std::filesystem::remove_all(raiz_, ec);
    std::filesystem::create_directories(pasta_);
  }
  ~PastaDoTitulo() {
    std::error_code ec;
    std::filesystem::remove_all(raiz_, ec);
  }
  std::string Raiz() const { return raiz_.string(); }
  std::string Nome() const { return pasta_.filename().string(); }
  std::string Caminho() const { return pasta_.string(); }
  void Escrever(const std::string& nome, const std::vector<std::uint8_t>& bytes) const {
    std::ofstream f(pasta_ / nome, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  }

 private:
  std::filesystem::path raiz_, pasta_;
};

// A MESMA ASSEMBLEIA DA BATERIA para o objecto do shell: e dele que sai a
// vtable que o guest le.
void ConstruirOShell(Bancada& b) {
  ConstruirObjeto(b.Mem(), b.S(), kObjShell, b.S().Endereco(kVtableShell), kSlotsPorVtable,
                  kBaseDoShell);
}

}  // namespace

// O CONTRATO DO SLOT 43, medido no `abd.mod` (35 chamadas identicas, todas do
// mesmo `lr`, com `cpBuf=0`, `cpszName=0` e `pcpszMIME=0`):
//
//   0x1360  cmp  r0, #0x23   ; 0x23 = 35 = AEE_ENEEDMORE
//   0x1364  bne  #0x1374     ; != 35 -> desiste
//   0x1368  ldr  r1, [sp,#0x30]
//   0x136c  cmp  r1, #0
//   0x1370  bne  #0x1380     ; *pdwSize != 0 -> segue
//
// A segunda chamada e IDENTICA e tem de responder 35 TAMBEM: o `abd` faz 35
// sequencias de init e um alternador por paridade deixaria metade dos objectos
// por inicializar (o zeebulator responde 0 na 2a -- ver a contradicao escrita em
// `core/brew/despacho.cpp`).
TEST(FrenteIshell2, OSlot43DaVtableRespondeENEEDMOREComOTamanhoNaoNulo) {
  Bancada b;
  ConstruirOShell(b);
  const std::uint32_t vtable = b.Mem().Ler32(kObjShell);
  ASSERT_EQ(vtable, b.S().Endereco(kVtableShell));
  EXPECT_EQ(b.Mem().Ler32(vtable + 4u * brew_slots::kShell_DetectType),
            b.S().Endereco(kBaseDoShell + brew_slots::kShell_DetectType))
      << "o slot 43 da vtable do shell tem de apontar para o detector";

  for (int chamada = 0; chamada < 2; ++chamada) {
    b.Mem().Escrever32(kPalavraNoGuest, 0);
    const std::uint32_t r = b.ChamaSaida(kBaseDoShell + brew_slots::kShell_DetectType, kObjShell,
                                         0, kPalavraNoGuest, 0, 0);
    EXPECT_EQ(r, 35u) << "chamada " << chamada << ": tem de ser AEE_ENEEDMORE";
    EXPECT_NE(b.Mem().Ler32(kPalavraNoGuest), 0u)
        << "chamada " << chamada << ": a 2a condicao do `abd` e *pdwSize != 0";
  }
  EXPECT_EQ(b.Faltas("IShell::slot43"), 0u) << "o slot deixou de ser servido pelo ramo generico";
}

// O `DetectType` com bytes OU com um nome: o MIME sai e vai para a memoria DO
// GUEST (`const char **`), e o que nao se sabe e `AEE_ENOTYPE` (34) -- nao um
// `SUCCESS` a fingir.
TEST(FrenteIshell2, ODetectTypeRespondeOMimeEOMimeVaiParaAMemoriaDoGuest) {
  Bancada b;
  ConstruirOShell(b);
  const std::vector<std::uint8_t> png = PngDoIshell2();
  for (std::size_t k = 0; k < png.size() && k < 32; ++k) {
    b.Mem().Escrever8(kBytesNoGuest + static_cast<std::uint32_t>(k), png[k]);
  }

  b.Mem().Escrever32(kPalavraNoGuest, 16);
  b.Mem().Escrever32(kPpNoGuest, 0);
  std::uint32_t r = b.ChamaSaida(kBaseDoShell + brew_slots::kShell_DetectType, kObjShell,
                                 kBytesNoGuest, kPalavraNoGuest, 0, kPpNoGuest);
  EXPECT_EQ(r, 0u) << "image/png pelo conteudo tem de ser SUCCESS";
  const std::uint32_t p = b.Mem().Ler32(kPpNoGuest);
  EXPECT_EQ(p, kZonaDeMimesDoShell) << "o mime tem de estar na zona do shell";
  EXPECT_EQ(LerCadeia(b.Mem(), p), "image/png");

  // PELO NOME, e em MAIUSCULAS: o cartao do Zeebo tem nomes assim.
  EscreverCadeia(b.Mem(), kNomeNoGuest, "MATERIAL.MID");
  b.Mem().Escrever32(kPalavraNoGuest, 0);
  r = b.ChamaSaida(kBaseDoShell + brew_slots::kShell_DetectType, kObjShell, 0, kPalavraNoGuest,
                   kNomeNoGuest, kPpNoGuest);
  EXPECT_EQ(r, 0u);
  EXPECT_EQ(LerCadeia(b.Mem(), b.Mem().Ler32(kPpNoGuest)), "audio/mid");

  // O QUE NAO SE SABE DIZ-SE: bytes irreconheciveis e sem nome.
  for (std::uint32_t k = 0; k < 16; ++k) b.Mem().Escrever8(kBytesNoGuest + k, 0x11u);
  b.Mem().Escrever32(kPalavraNoGuest, 16);
  r = b.ChamaSaida(kBaseDoShell + brew_slots::kShell_DetectType, kObjShell, kBytesNoGuest,
                   kPalavraNoGuest, 0, kPpNoGuest);
  EXPECT_EQ(r, 34u) << "AEE_ENOTYPE, e nao um mime inventado";
  EXPECT_EQ(b.Faltas("IShell::DetectType"), 0u);
}

// O SLOT 19: o ficheiro INTEIRO e o recurso quando `nResID == 0` (o caso do
// quake), e o `.bar`/`.pod` com a entrada `nResID` quando nao e (o caso do
// toyraidzeebo, cujo `.pod` foi medido com o formato do `.bar`). O que sai e um
// IDIB -- um bitmap, que e o que o `cls = 0x01001021` (AEECLSID_BITMAP) pede.
TEST(FrenteIshell2, OLoadResObjectServeUmIdibDoFicheiroEDoContentor) {
  PastaDoTitulo pasta;
  pasta.Escrever("splash.png", PngDoIshell2());
  pasta.Escrever("jogo.pod", ContentorDoIshell2(
                                 {{6, 1, 0, 0}},
                                 {BlobDoIshell2(12, "image/png", PngDoIshell2())}));
  Bancada b;
  b.AcessoAVfs().Registar(pasta.Caminho());
  b.D().SituarTitulo(pasta.Raiz(), pasta.Nome());
  ConstruirOShell(b);
  const std::uint32_t vtable = b.Mem().Ler32(kObjShell);
  EXPECT_EQ(b.Mem().Ler32(vtable + 4u * brew_slots::kShell_LoadResObject),
            b.S().Endereco(kBaseDoShell + brew_slots::kShell_LoadResObject))
      << "o slot 19 da vtable do shell tem de apontar para o LoadResObject";

  // 1. O FICHEIRO INTEIRO (`nResID = 0`, como o quake).
  EscreverCadeia(b.Mem(), kNomeNoGuest, "splash.png");
  const std::uint32_t obj = b.ChamaSaida(kBaseDoShell + brew_slots::kShell_LoadResObject, kObjShell,
                                         kNomeNoGuest, 0, 0);
  ASSERT_NE(obj, 0u) << "o PNG esta la: tem de sair um objecto";
  EXPECT_EQ(b.Mem().Ler32(obj), b.S().Endereco(kVtableBitmap))
      << "o objecto devolvido tem de ser um bitmap com vtable";
  EXPECT_EQ(b.Mem().Ler16(obj + CamposDoIdib::kCx), 2u);
  EXPECT_EQ(b.Mem().Ler16(obj + CamposDoIdib::kCy), 2u);
  const std::uint32_t pbmp = b.Mem().Ler32(obj + CamposDoIdib::kPBmp);
  ASSERT_NE(pbmp, 0u);
  EXPECT_EQ(b.Mem().Ler16(pbmp), 0xF800u) << "o canto superior esquerdo e vermelho puro (565)";
  EXPECT_EQ(b.Faltas("IShell::LoadResObject"), 0u);

  // 2. A ENTRADA DO CONTENTOR (`nResID = 1`, como o toyraidzeebo): o blob do
  //    tipo 6 e saltado (o mime fica para nos, o dado e do PNG).
  EscreverCadeia(b.Mem(), kNomeNoGuest, "jogo.pod");
  const std::uint32_t obj2 = b.ChamaSaida(kBaseDoShell + brew_slots::kShell_LoadResObject, kObjShell,
                                          kNomeNoGuest, 1, 0x01001021u);
  ASSERT_NE(obj2, 0u) << "a entrada 1 do contentor tem de ser servida";
  EXPECT_NE(obj2, obj) << "e um objecto NOVO, e nao o mesmo";
  EXPECT_EQ(b.Mem().Ler32(obj2), b.S().Endereco(kVtableBitmap));
  EXPECT_EQ(b.Mem().Ler16(obj2 + CamposDoIdib::kCx), 2u);

  // 3. O QUE NAO EXISTE: `NULL`, e a falta com o NOME do que se procurou.
  EscreverCadeia(b.Mem(), kNomeNoGuest, "nao_existe.png");
  const std::uint32_t nulo = b.ChamaSaida(kBaseDoShell + brew_slots::kShell_LoadResObject,
                                          kObjShell, kNomeNoGuest, 0, 0);
  EXPECT_EQ(nulo, 0u);
  EXPECT_EQ(b.Faltas("IShell::LoadResObject"), 1u);
}

// `rocketweb` pede `app.bar`, recurso 5008, como `image/bmp`. A entrada
// devolve um IDIB com os pixels que o BMP declarou, nao o bitmap generico nem
// um raster sintetico. Um BMP invalido conserva o contrato NULL e diz a razao.
TEST(FrenteIshell2, OLoadResObjectServeOBmpDoAppBarDoRocketwebComMotivoNaRecusa) {
  PastaDoTitulo pasta;
  pasta.Escrever("app.bar", ContentorDoIshell2(
                                {{6, 5008, 0, 0}},
                                {BlobDoIshell2(12, "image/bmp", BmpDoIshell2())}));
  pasta.Escrever("app-invalido.bar", ContentorDoIshell2(
                                         {{6, 5008, 0, 0}},
                                         {BlobDoIshell2(12, "image/bmp", {0x42, 0x4d})}));
  Bancada b;
  b.AcessoAVfs().Registar(pasta.Caminho());
  b.D().SituarTitulo(pasta.Raiz(), pasta.Nome());
  ConstruirOShell(b);

  EscreverCadeia(b.Mem(), kNomeNoGuest, "app.bar");
  const std::uint32_t obj = b.ChamaSaida(kBaseDoShell + brew_slots::kShell_LoadResObject,
                                         kObjShell, kNomeNoGuest, 5008, 0x01001021u);
  ASSERT_NE(obj, 0u);
  EXPECT_EQ(b.Mem().Ler32(obj), b.S().Endereco(kVtableBitmap));
  EXPECT_EQ(b.Mem().Ler16(obj + CamposDoIdib::kCx), 2u);
  EXPECT_EQ(b.Mem().Ler16(obj + CamposDoIdib::kCy), 2u);
  const std::uint32_t pixels = b.Mem().Ler32(obj + CamposDoIdib::kPBmp);
  ASSERT_NE(pixels, 0u);
  EXPECT_EQ(b.Mem().Ler16(pixels), ImagemBmp::Rgb565(255, 0, 0));
  EXPECT_EQ(b.Mem().Ler16(pixels + 2), ImagemBmp::Rgb565(0, 255, 0));
  EXPECT_EQ(b.Mem().Ler16(pixels + 4), ImagemBmp::Rgb565(0, 0, 255));
  EXPECT_EQ(b.Faltas("IShell::LoadResObject"), 0u);

  EscreverCadeia(b.Mem(), kNomeNoGuest, "app-invalido.bar");
  EXPECT_EQ(b.ChamaSaida(kBaseDoShell + brew_slots::kShell_LoadResObject,
                         kObjShell, kNomeNoGuest, 5008, 0x01001021u), 0u);
  EXPECT_EQ(b.Faltas("IShell::LoadResObject"), 1u);
  ASSERT_FALSE(b.Eventos().empty());
  const Evento& recusa = b.Eventos().back();
  EXPECT_EQ(recusa.nome, "NAO_IMPLEMENTADO: IShell::LoadResObject");
  EXPECT_NE(recusa.detalhe.find("nao descodifica como BMP"), std::string::npos);
  EXPECT_NE(recusa.detalhe.find("stream BMP menor que o cabecalho"), std::string::npos);
}

TEST(FrenteIshell2, OLoadResObjectResolveFsHomeParentSemSairDaRaizDosMods) {
  PastaDoTitulo pasta;
  const std::filesystem::path id1 = std::filesystem::path(pasta.Raiz()) / "id1";
  std::filesystem::create_directories(id1);
  {
    std::ofstream f(id1 / "splash_title.png", std::ios::binary);
    const auto png = PngDoIshell2();
    f.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
  }
  Bancada b;
  b.AcessoAVfs().Registar(pasta.Caminho());
  b.D().SituarTitulo(pasta.Raiz(), pasta.Nome());
  ConstruirOShell(b);
  EscreverCadeia(b.Mem(), kNomeNoGuest, "fs:/~/../id1/splash_title.png");
  const std::uint32_t obj = b.ChamaSaida(kBaseDoShell + brew_slots::kShell_LoadResObject,
                                         kObjShell, kNomeNoGuest, 0, 0);
  ASSERT_NE(obj, 0u);
  EXPECT_EQ(b.Mem().Ler16(obj + CamposDoIdib::kCx), 2u);
  EXPECT_EQ(b.Faltas("IShell::LoadResObject"), 0u);
}

// ===========================================================================
// A FRENTE slot32: `IShell::GetHandler` (o slot 32 da vtable do shell).
//
// O CONTRATO, de `AEEIShell.h:800` e da doc em `:6261-6288`:
//
//   AEECLSID GetHandler(IShell *po, AEECLSID clsBase, const char *pszIn)
//     -> r0=po  r1=clsBase  r2=pszIn
//   Return: AEECLSID of the associated handler class. 0 (zero), if otherwise.
//
// O USO E O DOS TITULOS, e o do proprio SDK (`AEEIShell.h:7116`,
// `doc/AEEMedia.txt:281/327`), e foi medido com `ZB2_TRACE=1`:
//
//   95 chamadas em 5 titulos (abd 35, ridgeracer 25, torkandkral 17,
//   toyraidzeebo 15, pacmania 3), TODAS com `r1 = 0x01005500` (AEECLSID_MEDIA) e
//   `r2` = a cadeia do MIME que o `DetectType` acabou de devolver:
//   "audio/wav" 87x e "audio/mpeg" 8x.
//
// E o `abd.mod` faz com o retorno o que o exemplo do SDK faz (`strne r0, [sb]`
// = `if (cls) *pCls = cls;`), logo o que sai daqui vai DIRETO para um
// `IShell::CreateInstance`.
//
// Os testes entram pelo ENDERECO DA VTABLE (armadilha 3): um teste que chamasse
// um id interno nao provava a cablagem.
// ===========================================================================

TEST(FrenteSlot32, OSlot32DaVtableRespondeOHandlerDoMimeMedido) {
  Bancada b;
  ConstruirOShell(b);
  const std::uint32_t vtable = b.Mem().Ler32(kObjShell);
  ASSERT_EQ(vtable, b.S().Endereco(kVtableShell));
  EXPECT_EQ(b.Mem().Ler32(vtable + 4u * brew_slots::kShell_GetHandler),
            b.S().Endereco(kBaseDoShell + brew_slots::kShell_GetHandler))
      << "o slot 32 da vtable do shell tem de apontar para o GetHandler";

  // `audio/wav` -> `AEECLSID_MEDIAADPCM` (0x0100550a), e NAO o `MEDIAPCM`:
  // `AEEMimeTypes.h:71` chama a este MIME `MT_AUDIO_ADPCM`, e o
  // `doc/AEEMedia.txt:809` toca `a1.wav` com `AEECLSID_MEDIAADPCM`. O
  // `MEDIAPCM` (0x01005511) e o PCM CRU do `AEEMedia.txt:1146` (`sample.raw`).
  // O zeebx mapeia este MIME para o MEDIAPCM -- e a contradicao que fica escrita
  // no `despacho.cpp`.
  EscreverCadeia(b.Mem(), kNomeNoGuest, "audio/wav");
  std::uint32_t r = b.ChamaSaida(kBaseDoShell + brew_slots::kShell_GetHandler, kObjShell,
                                 0x01005500u, kNomeNoGuest);
  EXPECT_EQ(r, 0x0100550au) << "AEECLSID_MEDIAADPCM";
  EXPECT_NE(r, 0x01005511u) << "AEECLSID_MEDIAPCM e o PCM cru, nao o WAV";

  // `audio/mpeg` -> `AEECLSID_MEDIAMP3` (0x01005502). O MIME e o que o NOSSO
  // `DetectType` devolve para um MP3 (o `MT_AUDIO_MP3` do SDK escreve-se
  // "audio/mp3"), e as duas referencias concordam neste par.
  EscreverCadeia(b.Mem(), kNomeNoGuest, "audio/mpeg");
  r = b.ChamaSaida(kBaseDoShell + brew_slots::kShell_GetHandler, kObjShell, 0x01005500u,
                   kNomeNoGuest);
  EXPECT_EQ(r, 0x01005502u) << "AEECLSID_MEDIAMP3";

  // E o nome alternativo do mesmo par (`zeemu BrewShell.cpp:218`). A cadeia e
  // em minusculas porque e ASSIM que a medicao a viu: o `pszIn` do guest e a
  // cadeia que o nosso `DetectType` lhe devolveu, e as constantes do
  // `AEEMimeTypes.h` sao todas minusculas.
  EscreverCadeia(b.Mem(), kNomeNoGuest, "audio/x-wav");
  r = b.ChamaSaida(kBaseDoShell + brew_slots::kShell_GetHandler, kObjShell, 0x01005500u,
                   kNomeNoGuest);
  EXPECT_EQ(r, 0x0100550au);

  EXPECT_EQ(b.Faltas("IShell::slot32"), 0u)
      << "o slot deixou de ser servido pelo ramo generico";
  EXPECT_EQ(b.Faltas("IShell::GetHandler sem handler para o MIME"), 0u);
}

// O QUE SAI DAQUI TEM DE SER CRIAVEL. O `abd.mod` mete o retorno do `GetHandler`
// direto num `CreateInstance` (0x16e8 `strne r0,[sb]`, e o pedido seguinte no
// codigo do titulo): uma classe que o `CreateInstance` recusasse deixaria o jogo
// sem objecto de midia e a recusa mudava so de nome.
std::vector<std::uint8_t> Bmp24DoA3d() {
  // BITMAPFILEHEADER + BITMAPINFOHEADER + 2 linhas de 2 pixels (stride 8).
  // A primeira linha visual e vermelho/verde; BMP positivo guarda-a por ultimo.
  return {0x42, 0x4d, 0x46, 0, 0, 0, 0, 0, 0, 0, 0x36, 0, 0, 0,
          0x28, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 1, 0, 24, 0,
          0, 0, 0, 0, 16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
          0, 0, 0, 0,
          255, 0, 0, 0, 0, 0, 0, 0,  // fundo: azul, preto, padding
          0, 0, 255, 0, 255, 0, 0, 0};  // topo: vermelho, verde, padding
}

// Cadeia que o a3d usa para BMP: GetHandler(HTYPE_VIEWER=0,"image/bmp")
// devolve AEECLSID_BMPDECODER, que entra no CreateInstance. Cada salto abaixo
// lê a entrada da vtable do guest; não chama índice interno.
TEST(FrenteA3dBmp, GetHandlerCriaDecoderAlimentaEDevolveBitmap) {
  Bancada b;
  ConstruirOShell(b);
  constexpr std::uint32_t kPpoDecoder = 0x80094000u;
  constexpr std::uint32_t kPpoFeed = 0x80094004u;
  constexpr std::uint32_t kPpoBitmap = 0x80094008u;
  constexpr std::uint32_t kBmpNoGuest = 0x80095000u;
  EscreverCadeia(b.Mem(), kNomeNoGuest, "image/bmp");

  const std::uint32_t cls = b.ChamaEndereco(
      b.EntradaDaVtable(kObjShell, brew_slots::kShell_GetHandler), kObjShell, 0u, kNomeNoGuest);
  ASSERT_EQ(cls, 0x01026e21u) << "AEECLSID_BMPDECODER";
  ASSERT_EQ(b.ChamaEndereco(b.EntradaDaVtable(kObjShell, 2), kObjShell, cls, kPpoDecoder),
            static_cast<std::uint32_t>(kAeeSuccess));
  const std::uint32_t decoder = b.Mem().Ler32(kPpoDecoder);
  ASSERT_EQ(decoder, kObjetoBmpDecoder);

  ASSERT_EQ(b.ChamaEndereco(b.EntradaDaVtable(decoder, brew_slots::kImageDecoder_QueryInterface),
                            decoder, kIidForceFeed, kPpoFeed),
            static_cast<std::uint32_t>(kAeeSuccess));
  const std::uint32_t feed = b.Mem().Ler32(kPpoFeed);
  ASSERT_EQ(feed, kObjetoForceFeedBmp);
  const std::vector<std::uint8_t> bmp = Bmp24DoA3d();
  b.Mem().EscreverBloco(kBmpNoGuest, bmp.data(), static_cast<std::uint32_t>(bmp.size()));
  ASSERT_EQ(b.ChamaEndereco(b.EntradaDaVtable(feed, brew_slots::kForceFeed_Write), feed,
                            kBmpNoGuest, static_cast<std::uint32_t>(bmp.size())),
            static_cast<std::uint32_t>(kAeeSuccess));
  ASSERT_EQ(b.ChamaEndereco(b.EntradaDaVtable(decoder, brew_slots::kImageDecoder_GetBitmap),
                            decoder, kPpoBitmap),
            static_cast<std::uint32_t>(kAeeSuccess));
  const std::uint32_t bitmap = b.Mem().Ler32(kPpoBitmap);
  ASSERT_NE(bitmap, 0u);
  EXPECT_EQ(b.Mem().Ler16(bitmap + CamposDoIdib::kCx), 2u);
  EXPECT_EQ(b.Mem().Ler16(bitmap + CamposDoIdib::kCy), 2u);
  const std::uint32_t pixels = b.Mem().Ler32(bitmap + CamposDoIdib::kPBmp);
  EXPECT_EQ(b.Mem().Ler16(pixels), ImagemBmp::Rgb565(255, 0, 0));
  EXPECT_EQ(b.Mem().Ler16(pixels + 2), ImagemBmp::Rgb565(0, 255, 0));
  EXPECT_EQ(b.Mem().Ler16(pixels + 4), ImagemBmp::Rgb565(0, 0, 255));
  EXPECT_EQ(b.Faltas("IShell::GetHandler sem registo para o clsBase"), 0u);
  EXPECT_EQ(b.Faltas("IShell::CreateInstance CLSID desconhecido"), 0u);
}

TEST(FrenteSlot32, OHandlerQueSaiDoSlot32ECriadoPeloCreateInstance) {
  Bancada b;
  ConstruirOShell(b);
  constexpr std::uint32_t kPpo = 0x00094000u;
  EscreverCadeia(b.Mem(), kNomeNoGuest, "audio/wav");
  const std::uint32_t cls = b.ChamaSaida(kBaseDoShell + brew_slots::kShell_GetHandler,
                                         kObjShell, 0x01005500u, kNomeNoGuest);
  ASSERT_EQ(cls, 0x0100550au);
  b.Mem().Escrever32(kPpo, 0);
  EXPECT_EQ(b.ChamaSaida(kBaseDoShell + 2, kObjShell, cls, kPpo), kAeeSuccess)
      << "o handler do registo tem de ser uma classe que esta arvore cria";
  const std::uint32_t obj = b.Mem().Ler32(kPpo);
  ASSERT_NE(obj, 0u) << "o CreateInstance tem de devolver o objecto de midia";
  EXPECT_TRUE(b.S().Contem(b.Mem().Ler32(obj)))
      << "e um objecto com vtable na faixa de saida";
  EXPECT_EQ(b.Faltas("IShell::GetHandler handler nao criavel"), 0u);
}

// O `0` E A RESPOSTA DO CONTRATO ("0 (zero), if otherwise"), e NAO UM ERRO
// INVENTADO -- mas o que falta fica DITO, com o nome, em cada um dos tres casos.
TEST(FrenteSlot32, ORegistoDizNaoAoQueNaoSeServeEDizPorque) {
  Bancada b;
  ConstruirOShell(b);

  // 1. UM `clsBase` cujo registo esta arvore nao tem: o `0x01004000` e o
  //    `AEECLSID_VIEW` (`AEEClassIDs.h:28`, a interface que os viewers de imagem
  //    implementam), e o `AEEMimeTypes.h`/`AEEMedia.txt` usam ESTA forma (um
  //    AEECLSID base) nas chamadas ao `GetHandler`. O MIME e o que o nosso
  //    `DetectType` devolve para um PNG -- e a resposta honesta e NAO, porque um
  //    `AEECLSID_PNG` (0x01004004) nao se cria aqui.
  //
  //    (A outra forma do argumento e o enum DEPRECADO: `HTYPE_VIEWER` e 0,
  //    `HTYPE_SOUND` e 1 e `HTYPE_BROWSE` e 2, `AEEIShell.h:582-584` -- e nao
  //    os CLSID. O cabecalho marca-os "***deprecated****" em `:568`.)
  EscreverCadeia(b.Mem(), kNomeNoGuest, "image/png");
  EXPECT_EQ(b.ChamaSaida(kBaseDoShell + brew_slots::kShell_GetHandler, kObjShell, 0x01004000u,
                         kNomeNoGuest),
            0u);
  EXPECT_EQ(b.Faltas("IShell::GetHandler sem registo para o clsBase"), 1u)
      << "o pedido fica contado com o nome do clsBase";

  // 2. Um MIME que o SDK nomeia mas sem classe NESTA familia (`MT_AUDIO_WMA`,
  //    `AEEMimeTypes.h:74`): nao se inventa um handler.
  EscreverCadeia(b.Mem(), kNomeNoGuest, "audio/wma");
  EXPECT_EQ(b.ChamaSaida(kBaseDoShell + brew_slots::kShell_GetHandler, kObjShell, 0x01005500u,
                         kNomeNoGuest),
            0u);
  EXPECT_EQ(b.Faltas("IShell::GetHandler sem handler para o MIME"), 1u);

  // 3. Um `pszIn` que nao chegou: nulo, e vazio.
  EXPECT_EQ(b.ChamaSaida(kBaseDoShell + brew_slots::kShell_GetHandler, kObjShell, 0x01005500u, 0),
            0u);
  EXPECT_EQ(b.Faltas("IShell::GetHandler pszIn nulo ou vazio"), 1u);
  EscreverCadeia(b.Mem(), kNomeNoGuest, "");
  EXPECT_EQ(b.ChamaSaida(kBaseDoShell + brew_slots::kShell_GetHandler, kObjShell, 0x01005500u,
                         kNomeNoGuest),
            0u);
  EXPECT_EQ(b.Faltas("IShell::GetHandler pszIn nulo ou vazio"), 2u);

  // E NENHUM destes casos gastou a resposta de um MIME que existe.
  EscreverCadeia(b.Mem(), kNomeNoGuest, "audio/mid");
  EXPECT_EQ(b.ChamaSaida(kBaseDoShell + brew_slots::kShell_GetHandler, kObjShell, 0x01005500u,
                         kNomeNoGuest),
            0x01005501u);
}


// ===========================================================================
// O SQL DO Z-WHEEL (`tectoy`, 274755): `ISQLMgr::Open` e o `ISQLDatabase::Exec`
// ===========================================================================
//
// A MEDICAO que pediu este bloco (`ZB2_TRACE=1`, tectoy): o applet abre quatro
// bancos (`tt_prefs.db` 2x, `asset_cache`, `tt_game_info`) e o unico titulo do
// corpus que toca o SQL e ele. Antes disto o `Open` respondia `AEE_UNSUPPORTED` e
// o applet imprimia `Failed to init Preferences database: 20` e desistia -- nao
// chegava a pedir `Exec` nenhum, e a lista de demanda nao dizia o que ele QUERIA.
//
// A ORDEM DOS ARGUMENTOS vem de duas medicoes independentes (a sonda do `zeebx`,
// `13-classes-desconhecidas.md`, e o `zeebx-emu/src/machine/sql.rs`): o nome no
// `r1` e o PONTEIRO DE SAIDA no `r2`. O `r3` e o terceiro argumento, e o que aqui
// estava escrevia o NULO nele.
constexpr std::uint32_t kSaidaSqlOpen = 1553;
constexpr std::uint32_t kSaidaSqlExec = kVtableSqlDb + 3;
constexpr std::uint32_t kNomeDoBanco = 0x00094300u;
constexpr std::uint32_t kSqlNoGuest = 0x00094400u;
constexpr std::uint32_t kPPDb = 0x00094500u;
constexpr std::uint32_t kMarcadorDoSql = 0x00000320u;
// A ROTINA DO CALLBACK, ARM, em `kRotina` (0x200): grava o `r1` (o numero de
// colunas), o `r2` (o vector de valores) e o `r3` (o vector dos nomes) no marcador,
// e devolve ZERO -- que e o que diz ao `sqlite3_exec` para continuar.
//
// OS TRES REGISTADORES SAO GUARDADOS. O vector dos nomes vem no `r3`, e uma rotina
// que so guardasse o `r2` deixaria o teste a ler a zona da memoria por CONSTANTE em
// vez de ler o que o callback recebeu -- um teste que prova que NOS escrevemos ali,
// e nao o que o JOGO ve.
void EscreverCallbackDoSql(Memoria& mem) {
  mem.Escrever32(kRotina + 0x00, 0xE59FC010u);  // ldr ip, [pc, #0x10] -> 0x218
  mem.Escrever32(kRotina + 0x04, 0xE58C1000u);  // str r1, [ip]      (ncols)
  mem.Escrever32(kRotina + 0x08, 0xE58C2004u);  // str r2, [ip, #4]  (valores)
  mem.Escrever32(kRotina + 0x0C, 0xE58C3008u);  // str r3, [ip, #8]  (nomes)
  mem.Escrever32(kRotina + 0x10, 0xE3A00000u);  // mov r0, #0
  mem.Escrever32(kRotina + 0x14, 0xE12FFF1Eu);  // bx lr
  mem.Escrever32(kRotina + 0x18, kMarcadorDoSql);
  mem.Escrever32(kMarcadorDoSql, 0);
  mem.Escrever32(kMarcadorDoSql + 4, 0);
  mem.Escrever32(kMarcadorDoSql + 8, 0);
}

// O QUE O CALLBACK DO JOGO RECEBEU, lido dos PROPRIOS vectores que ele recebeu
// (`char **`, com o `NULL` final do contrato do `sqlite3_exec`).
struct LinhaRecebida {
  std::uint32_t colunas = 0;
  std::vector<std::string> valores;
  std::vector<bool> nulos;
  std::vector<std::string> nomes;
  bool valores_terminados_em_nulo = false;
  bool nomes_terminados_em_nulo = false;
};

LinhaRecebida LerLinhaRecebida(Bancada& b) {
  LinhaRecebida r;
  r.colunas = b.Mem().Ler32(kMarcadorDoSql);
  const std::uint32_t pv = b.Mem().Ler32(kMarcadorDoSql + 4);
  const std::uint32_t pn = b.Mem().Ler32(kMarcadorDoSql + 8);
  for (std::uint32_t i = 0; i < r.colunas; ++i) {
    const std::uint32_t v = b.Mem().Ler32(pv + i * 4);
    r.nulos.push_back(v == 0);
    std::string s;
    if (v != 0) b.Mem().LerCadeia(v, &s, 256);
    r.valores.push_back(s);
    std::string n;
    const std::uint32_t p = b.Mem().Ler32(pn + i * 4);
    if (p != 0) b.Mem().LerCadeia(p, &n, 256);
    r.nomes.push_back(n);
  }
  r.valores_terminados_em_nulo = (pv != 0) && (b.Mem().Ler32(pv + r.colunas * 4) == 0);
  r.nomes_terminados_em_nulo = (pn != 0) && (b.Mem().Ler32(pn + r.colunas * 4) == 0);
  return r;
}

void EscreverTexto(Memoria& mem, std::uint32_t onde, const std::string& s) {
  for (std::size_t k = 0; k < s.size(); ++k)
    mem.Escrever8(onde + static_cast<std::uint32_t>(k), static_cast<std::uint8_t>(s[k]));
  mem.Escrever8(onde + static_cast<std::uint32_t>(s.size()), 0);
}

// Uma chamada ao `Exec` do banco, com a assinatura medida.
std::uint32_t ChamaExec(Bancada& b, const std::string& sql, std::uint32_t cb = 0,
                        std::uint32_t ctx = 0) {
  EscreverTexto(b.Mem(), kSqlNoGuest, sql);
  return b.ChamaSaida(kSaidaSqlExec, kObjSqlDb, kSqlNoGuest, cb, ctx);
}

// ABRE O BANCO DE TESTE pelo slot do `ISQLMgr` -- o mesmo caminho que o jogo usa.
//
// E OBRIGATORIO em todo o teste que chame o `Exec`: a PONTE TEM ESTADO (o banco
// aberto), e o subconjunto a mao nao tinha nenhum -- era por isso que o `Exec` dele
// respondia sem abertura nenhuma. A bancada nao registou VFS nenhuma, logo o banco
// nasce VAZIO (e o caso de um console sem nada descarregado).
std::uint32_t AbreOBanco(Bancada& b, const char* nome = "tt_prefs.db") {
  EscreverTexto(b.Mem(), kNomeDoBanco, nome);
  return b.ChamaSaida(kSaidaSqlOpen, 0x80060600u, kNomeDoBanco, kPPDb, 0u);
}

// O VALOR E O NOME DA COLUNA `i`, com o lugar vazio DITO em vez de um acesso fora
// dos limites: um teste que le `valores[0]` de uma linha que nao chegou nao falha,
// **estoura** -- e um estouro no meio de uma bateria de testes esconde os que
// vinham a seguir.
std::string ValorDaColuna(const LinhaRecebida& r, std::size_t i) {
  return i < r.valores.size() ? r.valores[i] : std::string("(sem coluna)");
}
std::string NomeDaColuna(const LinhaRecebida& r, std::size_t i) {
  return i < r.nomes.size() ? r.nomes[i] : std::string("(sem coluna)");
}

TEST(SqlDoZWheel, ACablagemDoBancoApontaParaOExecNaPropriaTabela) {
  // ESTE TESTE LE A TABELA (a vtable que o motor construiu), e nao o id interno:
  // chamar o id provaria so que o ramo do id existe -- foi essa a classe de
  // defeito que ja custou a cablagem do `SetTimer` nesta arvore.
  Bancada b;
  const std::uint32_t vt = kEnderecoDaVtableSqlDb;
  EXPECT_EQ(b.Mem().Ler32(vt + 3 * 4), b.S().Endereco(kSaidaSqlExec))
      << "o slot 3 do ISQLDatabase tem de ser o Exec";
  // O endereco de saida TEM de cair dentro da faixa de saida: um endereco fora
  // dela nunca e reconhecido pelo laco, e o `Exec` seria um stub mudo.
  const std::uint32_t inicio = b.S().base;
  const std::uint32_t fim = b.S().base + b.S().quantos * b.S().passo;
  EXPECT_GE(b.Mem().Ler32(vt + 3 * 4), inicio);
  EXPECT_LT(b.Mem().Ler32(vt + 3 * 4), fim);
  // E A VTABLE NAO PODE VIVER DENTRO DA FAIXA DE SAIDAS.
  //
  // MEDIDO, e o custo foi um titulo ALHEIO: com a vtable na faixa (indice 9800 ->
  // endereco 0xF0009920), o `reksio` (277495) -- que LE a memoria dessa faixa
  // (`ldr r0,[r4,#0x34]` em `0x35ffa`, com `r4=0xF0009900`) -- passou de
  // `passos_start=36` com `retornou` para `28` com `saiu_do_modulo`. A memoria da
  // faixa devolvia zero e passou a devolver um endereco dela propria.
  EXPECT_LT(vt, inicio) << "a vtable do banco tem de viver FORA da faixa de saidas";
  // E ELA MORA NA PAGINA DO PROPRIO OBJECTO -- e o que a mantem fora da faixa sem
  // depender de um segundo endereco escolhido a mao.
  EXPECT_GE(vt, kObjSqlDb);
  EXPECT_LT(vt, kObjSqlDb + 0x1000u);
  EXPECT_EQ(b.Mem().Ler32(kObjSqlDb), vt) << "o objecto aponta para a vtable dele";
}

TEST(SqlDoZWheel, OOpenDevolveOBancoNoR2EInformaOSucesso) {
  Bancada b;
  EscreverTexto(b.Mem(), kNomeDoBanco, "tt_prefs.db");
  b.Mem().Escrever32(kPPDb, 0xDEADBEEFu);
  // A assinatura medida: `OpenDatabase(po, pszName, ISQLDatabase **ppDB)`.
  EXPECT_EQ(b.ChamaSaida(kSaidaSqlOpen, 0x80060600u, kNomeDoBanco, kPPDb, 0u), kAeeSuccess);
  EXPECT_EQ(b.Mem().Ler32(kPPDb), kObjSqlDb) << "o banco vai no r2, e nao no r3";
  EXPECT_EQ(b.Faltas("ISQLMgr::Open"), 0u) << "servido nao e falta";
  // O PRESSUposto DECLARADO: o banco do jogo e aberto numa COPIA, num scratch
  // nosso, e nao na pasta da ROM do utilizador -- a VFS desta arvore e so de
  // leitura por decisao (`core/brew/vfs.h`, `core/brew/sql.h`).
  const auto& p = b.Tr().ContagemPressupostos();
  EXPECT_NE(p.find("ISQLMgr::Open (ponte SQLite)"), p.end())
      << "abrir numa copia e um pressuposto declarado, e nao silencioso";
}

TEST(SqlDoZWheel, OsPragmasEOsMarcadoresDeTransaccaoSaoServidos) {
  Bancada b;
  ASSERT_EQ(AbreOBanco(b), kAeeSuccess);
  // Os `PRAGMA` que so AJUSTAM e os marcadores de transaccao que envolvem a
  // criacao das tabelas: medidos no tectoy, e nenhum deles devolve linha.
  for (const char* s : {"PRAGMA main.journal_mode = PERSIST;", "PRAGMA main.locking_mode = EXCLUSIVE;",
                        "PRAGMA main.synchronous = FULL;", "PRAGMA legacy_file_format = OFF;",
                        "PRAGMA encoding = \"UTF-16\";", "BEGIN TRANSACTION;", "END TRANSACTION;"}) {
    EXPECT_EQ(ChamaExec(b, s), kAeeSuccess) << s;
  }
  // O `ROLLBACK` DE DENTRO de uma transaccao e SERVIDO -- e agora pelo MOTOR, e
  // nao por uma recusa nossa. O subconjunto a mao recusava-o com o nome porque
  // "prometia desfazer" e nao havia nada para desfazer; o SQLite desfaz mesmo.
  EXPECT_EQ(ChamaExec(b, "BEGIN TRANSACTION;"), kAeeSuccess);
  EXPECT_EQ(ChamaExec(b, "ROLLBACK TRANSACTION;"), kAeeSuccess);
  // E o `ROLLBACK` SEM transaccao aberta e um erro -- do SQLITE, com a mensagem
  // dele (`cannot rollback - no transaction is active`), que e exactamente o que o
  // console respondia. Antes era a NOSSA recusa, com o nome no registo.
  EXPECT_EQ(ChamaExec(b, "ROLLBACK;"), kAeeFailed);
  EXPECT_EQ(b.Faltas("ISQLDatabase::Exec instrucao nao servida"), 0u)
      << "nao ha aqui subconjunto nenhum: a instrucao CHEGA ao motor";
}

TEST(SqlDoZWheel, ODbinfoNasceVazioERecebeAVersaoQueOJogoGrava) {
  // A SEQUENCIA MEDIDA no tectoy, passo a passo:
  //   SELECT version, subversion FROM DBINFO   -> falha (nao ha tabela)
  //   CREATE TABLE DBINFO(...)                 -> cria
  //   INSERT OR REPLACE INTO DBINFO values (1, 0)
  //   SELECT version, subversion FROM DBINFO   -> uma linha
  Bancada b;
  ASSERT_EQ(AbreOBanco(b), kAeeSuccess);
  EXPECT_EQ(ChamaExec(b, "SELECT version, subversion FROM DBINFO"), kAeeFailed)
      << "sem a tabela, o SQLite diz 'no such table' -- e e assim que o jogo sabe "
         "que tem de criar";
  EXPECT_EQ(ChamaExec(b, "CREATE TABLE DBINFO(version INTEGER DEFAULT DB_VERSION, "
                         "subversion INTEGER DEFAULT DB_SUBVERSION)"),
            kAeeSuccess);
  EXPECT_EQ(ChamaExec(b, "CREATE TABLE DBINFO(version INTEGER, subversion INTEGER)"), kAeeFailed)
      << "um CREATE de uma tabela que ja existe e recusado pelo SQLite";
  EXPECT_EQ(ChamaExec(b, "INSERT OR REPLACE INTO DBINFO values (1, 0)"), kAeeSuccess);

  EscreverCallbackDoSql(b.Mem());
  EXPECT_EQ(ChamaExec(b, "SELECT version, subversion FROM DBINFO", kRotina, kContexto),
            kAeeSuccess);
  // A LINHA FOI ENTREGUE AO CALLBACK: ele correu e o numero de colunas e DOIS.
  // O TESTE LE OS VECTORES QUE O CALLBACK RECEBEU (`r2`/`r3`), e nao a zona da
  // memoria por constante: ler a zona provaria que NOS escrevemos ali, e o que
  // interessa e o que o JOGO ve.
  const LinhaRecebida r = LerLinhaRecebida(b);
  EXPECT_EQ(r.colunas, 2u);
  EXPECT_EQ(r.nomes, (std::vector<std::string>{"version", "subversion"}));
  EXPECT_EQ(r.valores, (std::vector<std::string>{"1", "0"}));
  EXPECT_TRUE(r.valores_terminados_em_nulo) << "o contrato do `sqlite3_exec` pede o NULL final";
  EXPECT_TRUE(r.nomes_terminados_em_nulo);
}

TEST(SqlDoZWheel, OPragmaIntegrityCheckEntregaOKAoCallback) {
  Bancada b;
  ASSERT_EQ(AbreOBanco(b), kAeeSuccess);
  EscreverCallbackDoSql(b.Mem());
  EXPECT_EQ(ChamaExec(b, "PRAGMA integrity_check", kRotina, kContexto), kAeeSuccess);
  const LinhaRecebida r = LerLinhaRecebida(b);
  EXPECT_EQ(r.colunas, 1u);
  EXPECT_EQ(ValorDaColuna(r, 0), "ok") << "o motor responde `ok` a um banco integro";
}

TEST(SqlDoZWheel, OPrefsGravaTodoOTextoLidoDeVoltaComQuatroColunas) {
  // A PECA QUE A FRENTE `zwheel` NOMEOU, palavra por palavra: *"INSERT sem
  // armazenamento PREFSINFO 8x -- o app grava e volta a ler, e precisa de um
  // `SELECT *` de 4 colunas com chave TEXT"*.
  //
  // Com o subconjunto a mao o `INSERT` era ACEITE e o conteudo NAO ficava: o
  // `SELECT` seguinte lia ZERO linhas de onde o jogo escreveu uma, e a preferencia
  // do utilizador perdia-se em silencio. Com a ponte quem guarda e o SQLite, e a
  // chave TEXT e dele.
  Bancada b;
  ASSERT_EQ(AbreOBanco(b), kAeeSuccess);
  EXPECT_EQ(ChamaExec(b, "INSERT OR REPLACE INTO PREFSINFO values ('Initialized', '', 1, 2)"),
            kAeeFailed) << "sem a tabela, e erro -- o SQLite nao a inventa";
  ASSERT_EQ(ChamaExec(b, "CREATE TABLE PREFSINFO(name TEXT PRIMARY KEY, strValue TEXT, "
                         "dwValue INTEGER, flags INTEGER)"),
            kAeeSuccess);
  ASSERT_EQ(ChamaExec(b, "INSERT OR REPLACE INTO PREFSINFO values ('Initialized', '', 1, 2)"),
            kAeeSuccess);
  EXPECT_EQ(b.Faltas("ISQLDatabase::Exec INSERT sem armazenamento"), 0u)
      << "o INSERT deixou de ser uma falta declarada: ele GUARDA";

  EscreverCallbackDoSql(b.Mem());
  ASSERT_EQ(ChamaExec(b, "SELECT * FROM PREFSINFO", kRotina, kContexto), kAeeSuccess);
  const LinhaRecebida r = LerLinhaRecebida(b);
  EXPECT_EQ(r.colunas, 4u) << "quatro colunas, como o esquema que o proprio modulo manda";
  EXPECT_EQ(r.nomes, (std::vector<std::string>{"name", "strValue", "dwValue", "flags"}));
  EXPECT_EQ(r.valores, (std::vector<std::string>{"Initialized", "", "1", "2"}));
  EXPECT_TRUE(r.valores_terminados_em_nulo);
  EXPECT_TRUE(r.nomes_terminados_em_nulo);
}

TEST(SqlDoZWheel, OCatalogoDeNoveColunasChegaAoCallbackInteiro) {
  // A LINHA MAIS LARGA do dialecto medido: o `ASSETS` do catalogo tem NOVE colunas
  // (`INSERT OR REPLACE INTO ASSETS values (%d, %d, %d, %d, '%s', %d, '%s', %d, %d)`).
  // O subconjunto a mao entregava QUATRO; nove era "linha com forma estranha".
  Bancada b;
  ASSERT_EQ(AbreOBanco(b), kAeeSuccess);
  ASSERT_EQ(ChamaExec(b, "CREATE TABLE ASSETS(owner INTEGER, dslid INTEGER PRIMARY KEY, type "
                         "INTEGER, version INTEGER, path TEXT, language INTEGER, title TEXT, "
                         "startdate INTEGER, enddate INTEGER)"),
            kAeeSuccess);
  ASSERT_EQ(ChamaExec(b, "INSERT OR REPLACE INTO ASSETS values (0, 10001, 6, 1, "
                         "'./assets/faq/en/setup.html', 538996325, 'Setup', 0, 0)"),
            kAeeSuccess);
  EscreverCallbackDoSql(b.Mem());
  ASSERT_EQ(ChamaExec(b, "SELECT * FROM ASSETS", kRotina, kContexto), kAeeSuccess);
  const LinhaRecebida r = LerLinhaRecebida(b);
  EXPECT_EQ(r.colunas, 9u);
  EXPECT_EQ(ValorDaColuna(r, 1), "10001");
  EXPECT_EQ(ValorDaColuna(r, 4), "./assets/faq/en/setup.html");
  EXPECT_EQ(ValorDaColuna(r, 6), "Setup");
  EXPECT_EQ(NomeDaColuna(r, 4), "path");
}

TEST(SqlDoZWheel, OUmaTabelaQueNinguemCriouERespostaPeloMotor) {
  // NAO HA AQUI SUBCONJUNTO NENHUM: a instrucao CHEGA ao SQLite e a resposta e a
  // dele, com a mensagem dele no registo. O que este projecto conta como FALTA e
  // uma capacidade nossa que nao existe -- e o motor existe. Contar a resposta do
  // console como falta nossa poria o motor inteiro na lista do que falta fazer.
  Bancada b;
  DestinoMemoria dm;
  b.Tr().JuntarDestino(&dm);
  ASSERT_EQ(AbreOBanco(b), kAeeSuccess);
  EXPECT_EQ(ChamaExec(b, "SELECT * FROM NAO_EXISTE"), kAeeFailed);
  EXPECT_EQ(b.Faltas("ISQLDatabase::Exec SELECT de tabela ausente"), 0u);
  EXPECT_EQ(b.Faltas("ISQLDatabase::Exec instrucao nao servida"), 0u);
  bool disse = false;
  for (const auto& ev : dm.eventos) {
    if (ev.nome == "SQL_ERRO" && ev.detalhe.find("NAO_EXISTE") != std::string::npos) disse = true;
  }
  EXPECT_TRUE(disse) << "o motivo do motor tem de ficar no registo, com a tabela nomeada";
}

TEST(SqlDoZWheel, OCallbackForaDoModuloNaoSeChama) {
  // A MESMA GUARDA do `IShell::SendEvent` e do temporizador: um ponteiro de
  // funcao fora da faixa do modulo poria o PC num endereco de dados. Sem poder
  // entregar a linha, a INSTRUCAO ABORTA -- e o contrato do `sqlite3_exec` (um
  // callback que devolve diferente de zero aborta), nao uma invencao nossa.
  Bancada b;
  ASSERT_EQ(AbreOBanco(b), kAeeSuccess);
  EXPECT_EQ(ChamaExec(b, "PRAGMA integrity_check", 0x50000000u, kContexto), kAeeFailed);
  EXPECT_EQ(b.Faltas("ISQLDatabase::Exec callback fora do modulo"), 1u);
}

TEST(SqlDoZWheel, OExecDeclaraOPressupostoDaPonteENaoUmaFalta) {
  // A REGRA DE FRONTEIRA do `Traco`: recusar e `RegistarFalta`; responder o que
  // nao se mediu e `RegistarPressuposto`. A ponte responde com o motor do console
  // -- capacidade que EXISTE -- e isso fica DECLARADO e contado, em vez de
  // silencioso.
  Bancada b;
  ASSERT_EQ(AbreOBanco(b), kAeeSuccess);
  ASSERT_EQ(ChamaExec(b, "PRAGMA integrity_check"), kAeeSuccess);
  const auto& p = b.Tr().ContagemPressupostos();
  EXPECT_NE(p.find("ISQLDatabase::Exec (ponte SQLite)"), p.end())
      << "o motor de SQL usado tem de ficar declarado, e nao implicito";
}

// ===========================================================================
// OS DOIS CLSIDs DO Z-WHEEL: o `IConfig` e o `IDownload` (frente zclsid).
//
// A DEMANDA, medida na corrida do `tectoy` (`ZB2_TRACE=1`): `AEECLSID_CONFIG` 2x,
// `AEECLSID_DOWNLOAD` 1x, e as tres a receberem `ECLASSNOTSUPPORT`. Estes testes
// provam que o `CreateInstance` os serve -- e prova-o pela TABELA e pela ENTRADA
// DA VTABLE, e nao pelo id interno de saida.
// ===========================================================================
TEST(ZclsidDoZWheel, OConfigVemDoCreateInstanceComASuaVtableCablada) {
  Bancada b;
  constexpr std::uint32_t kPPo = 0x00090020u;
  b.Mem().Escrever32(kPPo, 0);
  // A PORTA E A MESMA DA MEDICAO: o slot 2 do IShell.
  ASSERT_EQ(b.ChamaSaida(2000 + brew_slots::kShell_CreateInstance, 0x80020000u, kIidConfig,
                         kPPo),
            kAeeSuccess);
  const std::uint32_t obj = b.Mem().Ler32(kPPo);
  ASSERT_EQ(obj, kObjConfig) << "o AEECLSID_CONFIG tem de dar o NOSSO IConfig";
  // A TABELA: o slot 3 do objecto tem de ser o `SetItem`, e o 2 o `GetItem`.
  EXPECT_EQ(b.EntradaDaVtable(obj, 3), b.S().Endereco(kSlotConfigSetItem));
  EXPECT_EQ(b.EntradaDaVtable(obj, 2), b.S().Endereco(kSlotConfigGetItem));
}

TEST(ZclsidDoZWheel, OSetItemDoConfigGuardaEOGetItemDevolveOGravado) {
  // OS ARGUMENTOS SAO OS DO JOGO: `SetItem(po, 0x3f, ptr, 4)` -- o item 63, quatro
  // bytes (`tectoy` 0x71250, `mov r1,#0x3f`; `mov r3,#4`).
  Bancada b;
  constexpr std::uint32_t kPPo = 0x00090020u;
  constexpr std::uint32_t kValor = 0x000A0000u, kDestino = 0x000A0010u;
  b.Mem().Escrever32(kPPo, 0);
  ASSERT_EQ(b.ChamaSaida(2000 + brew_slots::kShell_CreateInstance, 0x80020000u, kIidConfig,
                         kPPo),
            kAeeSuccess);
  const std::uint32_t obj = b.Mem().Ler32(kPPo);
  b.Mem().Escrever32(kValor, 0x00000003u);
  b.Mem().Escrever32(kDestino, 0xDEADBEEFu);
  // PELA ENTRADA DA VTABLE: e o caminho que o jogo faz.
  EXPECT_EQ(b.ChamaEndereco(b.EntradaDaVtable(obj, 3), obj, 0x3fu, kValor, 4u), kAeeSuccess);
  EXPECT_EQ(b.ChamaEndereco(b.EntradaDaVtable(obj, 2), obj, 0x3fu, kDestino, 4u), kAeeSuccess);
  EXPECT_EQ(b.Mem().Ler32(kDestino), 3u) << "quem grava rele o que gravou";
  // E O ITEM QUE NINGUEM ESCREVEU RECUSA -- nao devolve zero, que e um valor
  // legitimo e mentiria sobre o aparelho.
  EXPECT_EQ(b.ChamaEndereco(b.EntradaDaVtable(obj, 2), obj, 0x10u, kDestino, 4u), kAeeFailed);
  EXPECT_EQ(b.Faltas("IConfig::GetItem item nao definido"), 1u);
}

TEST(ZclsidDoZWheel, ODownloadVemDoCreateInstanceEOSeuSlot21RecusaComONome) {
  Bancada b;
  constexpr std::uint32_t kPPo = 0x00090020u;
  b.Mem().Escrever32(kPPo, 0);
  ASSERT_EQ(b.ChamaSaida(2000 + brew_slots::kShell_CreateInstance, 0x80020000u, kIidDownload,
                         kPPo),
            kAeeSuccess);
  const std::uint32_t obj = b.Mem().Ler32(kPPo);
  ASSERT_EQ(obj, kObjDownload);
  // O SLOT 21, com os argumentos medidos em `Tectoy_FixupTime`: (po, id, callback
  // do modulo, contexto). Ele NAO e lido pelo jogo -- a resposta contratual e
  // EFAILED porque nao ha fila de downloads; isso e pressuposto declarado, nao falta.
  //
  // O `id` E O QUE O SLOT 45 DEVOLVE (frente `slot45`): 274755, o numero da pasta
  // do modulo. O `0x14` que aqui esteve era o `kAeeUnsupported` do ramo generico
  // -- um codigo de erro a passar por id de item, e nao uma medida.
  EXPECT_EQ(b.EntradaDaVtable(obj, 21), b.S().Endereco(kSlotDownloadInfoComCallback));
  EXPECT_EQ(b.ChamaEndereco(b.EntradaDaVtable(obj, 21), obj, 274755u, 0x000735f4u, 0x80200048u),
            kAeeFailed);
  EXPECT_EQ(b.Faltas("IDownload slot21 (info do item, id/cb/ctx)"), 0u);
  const auto& pressupostos_download = b.Tr().ContagemPressupostos();
  EXPECT_NE(pressupostos_download.find("IDownload::GetItemInfo"), pressupostos_download.end());
  // A LISTA DE DOWNLOADS FALHADOS (o slot 3 do segundo sitio medido, o
  // `Gamelib_CheckForFailedDownload`) responde ZERO: nao ha fila, logo nao ha
  // falhados -- e o zero e o valor que o modulo trata como "nada a corrigir".
  EXPECT_EQ(b.ChamaEndereco(b.EntradaDaVtable(obj, 3), obj, 0u), 0u);
  // E UM SLOT QUE NINGUEM MEDIU RECUSA COM O NUMERO DELE, em vez de responder
  // zero a fingir de contrato.
  EXPECT_EQ(b.ChamaEndereco(b.EntradaDaVtable(obj, 7), obj, 0u), kAeeUnsupported);
  EXPECT_EQ(b.Faltas("IDownload slot nao implementado"), 1u);
}

// ===========================================================================
// O SLOT 45 DO IShell: o `GetClassItemID` (frente `slot45`).
//
// A DEMANDA, medida no `tectoy` (274755): depois de criar o `IDownload`, o
// `Tectoy_FixupTime` chama `GetClassItemID(po, 0x01070798)` e passa o que ele
// devolver ao slot 21 do `IDownload`. Com o slot 45 no ramo generico, o `r0` era
// o `kAeeUnsupported` (20 = 0x14) -- um codigo de erro a viajar como id de item.
//
// Estes testes entram pela ENTRADA DA VTABLE do IShell (a TABELA, e nao o id
// interno de saida): uma chamada a `ChamaSaida(2045)` provaria a semantica, e nao
// a cablagem.
// ===========================================================================
TEST(FrenteSlot45, OGetClassItemIDEstaNaVtableEDevolveOIdDeItemDaPastaDoTitulo) {
  Bancada b;
  ConstruirOShell(b);
  // A TABELA: o slot 45 da vtable do IShell tem de apontar para o handler (o
  // indice de saida `kBaseDoShell + 45`, como o `ConstruirObjeto` da bateria).
  EXPECT_EQ(b.EntradaDaVtable(kObjShell, brew_slots::kShell_GetClassItemID),
            b.S().Endereco(kBaseDoShell + brew_slots::kShell_GetClassItemID))
      << "o slot 45 da vtable do shell tem de apontar para o GetClassItemID";
  // O TITULO MEDIDO: pasta 274755, clsid 0x01070798 (`corpus62.json` e o `.mif`).
  b.D().SituarTitulo("/nao/existe", "274755", 0x01070798u);
  const std::uint32_t id = b.ChamaEndereco(
      b.EntradaDaVtable(kObjShell, brew_slots::kShell_GetClassItemID), kObjShell, 0x01070798u);
  EXPECT_EQ(id, 274755u) << "o id de item e o numero da PASTA do modulo (mod/274755/tectoy.mod)";
  EXPECT_EQ(b.Faltas("IShell::slot45"), 0u)
      << "o ramo generico nao pode voltar a apanhar este slot";
  EXPECT_EQ(b.Faltas("IShell::GetClassItemID sem id de item"), 0u);
}

TEST(FrenteSlot45, UmaClasseDeOutroModuloRespondeZero) {
  Bancada b;
  ConstruirOShell(b);
  b.D().SituarTitulo("/nao/existe", "274755", 0x01070798u);
  // `0x01000000` e o `AEECLSID_DOWNLOAD`: uma classe que NAO e deste modulo. O SDK
  // manda devolver 0 ("Class not found or module is static") -- e o 0 fica
  // DECLARADO como pressuposto, porque este caminho nao foi medido.
  EXPECT_EQ(b.ChamaEndereco(b.EntradaDaVtable(kObjShell, brew_slots::kShell_GetClassItemID),
                            kObjShell, 0x01000000u),
            0u);
  const auto& p = b.Tr().ContagemPressupostos();
  EXPECT_NE(p.find("IShell::GetClassItemID de classe alheia (-> 0)"), p.end());
  // SEM `SituarTitulo` tambem: nao ha titulo, logo nao ha id de item a dar.
  Bancada c;
  ConstruirOShell(c);
  EXPECT_EQ(c.ChamaEndereco(c.EntradaDaVtable(kObjShell, brew_slots::kShell_GetClassItemID),
                            kObjShell, 0x01070798u),
            0u);
}

TEST(FrenteSlot45, SemNumeroDePastaRecusaComONome) {
  // UMA PASTA QUE NAO E UM NUMERO: o id de item nao e conhecido. Devolver um
  // numero inventado punha o titulo a pedir o `AppModInfo` de um item ALHEIO no
  // slot 21 do `IDownload` -- por isso a falta fica com o nome (P2), e o 0 e o
  // unico canal que ha (o retorno do metodo E o id).
  Bancada b;
  ConstruirOShell(b);
  b.D().SituarTitulo("/nao/existe", "pasta_sem_numero", 0x01070798u);
  EXPECT_EQ(b.ChamaEndereco(b.EntradaDaVtable(kObjShell, brew_slots::kShell_GetClassItemID),
                            kObjShell, 0x01070798u),
            0u);
  EXPECT_EQ(b.Faltas("IShell::GetClassItemID sem id de item"), 1u);
}

TEST(RetornoDeSaida, ORegressoVoltaAoModoDoChamador) {
  // MEDIDO (frente g5, `reksio` 0x36184): o modulo chama o ajudante pela veneira
  // do proprio ficheiro (`bl 0x232` + `bx r3`, com o endereco do ajudante na
  // tabela do modulo). O `bx` vai para um endereco PAR da faixa de saida, e o
  // bit 0 de um `bx` escolhe o MODO -- logo a chamada entra em ARM.
  //
  // O `bl` do Thumb tinha posto `lr = proxima | 1`. O regresso escrevia esse `lr`
  // CRU no PC: o PC ficava IMPAR (a busca seguinte lia a meia-palavra a partir do
  // byte 1) e o CPSR ficava em ARM, o que fazia o modulo ler codigo THUMB como
  // ARM. Foi isso que se viu no `reksio`: 91 recusas a partir de
  // `pc=0x36188 instr=0x1c3268e0` (a palavra deslocada de `ldr r0,[r4,#0xc]`) e,
  // antes disso, os tres `saiu_do_modulo_para_0x...` da frente.
  //
  // E o MESMO defeito que o callback ja tinha pago (ver `EntregarEventoAoApplet`:
  // "o modo tem de vir do bit 0 da FUNCAO"), agora do lado do RETORNO.
  Bancada b;
  b.Mem().Escrever8(0x80210000u, 0);  // strcat com destino vazio
  b.Mem().Escrever8(0x80210100u, 0);  // e fonte vazia
  // Um retorno de Thumb FORA do modulo (o modulo e 0..0x00100000): assim a
  // paragem e imediata e o estado fica intacto para se poder afirmar.
  constexpr std::uint32_t kRetorno = 0x00100011u;
  b.Cpu().SetCpsr(b.Cpu().Cpsr() | Cpsr::kT);
  b.Cpu().Set(kR0, 0x80210000u);
  b.Cpu().Set(kR1, 0x80210100u);
  b.Cpu().Set(kLR, kRetorno);
  b.Cpu().Bx(b.S().Endereco(1568));  // AEEHelperFuncs[0x00C] = strcat
  EXPECT_EQ(b.Cpu().Cpsr() & Cpsr::kT, 0u) << "a chamada entra em ARM (bit 0 do `bx`)";
  const ResultadoFase r = b.D().Correr(b.Cpu(), 100, 0);
  EXPECT_EQ(r.motivo.rfind("saiu_do_modulo", 0), 0u)
      << "a saida servida e o modulo sai no retorno: " << r.motivo;
  EXPECT_EQ(b.Cpu().Get(kPC), kRetorno & ~1u) << "o PC volta sem o bit 0";
  EXPECT_EQ(b.Cpu().Cpsr() & Cpsr::kT, Cpsr::kT) << "e o CPSR volta a Thumb (bit 0 do `lr`)";
}

// ===========================================================================
// QW4: dois slots de 1 pedido que hoje recusam com o numero.
//
// `IShell::slot13` e `GetTimerExpiration` (`AEEIShell.h`: `uint32
// (*GetTimerExpiration)(iname *po, void (*pfn)(void *), void *pUser)`), medido
// 1x no `zenonia`. `IHeap::slot6` e `CheckAvail` (`AEEHeap.h`: `boolean
// (*CheckAvail)(IHeap *pIHeap, uint32 dwSize)`), medido 1x no `bio4_brew`.
// ===========================================================================
TEST(EntradaNoDespacho, GetTimerExpirationDevolveORestanteDoTemporizadorArmado) {
  Bancada b;
  constexpr std::uint32_t kPfn = 0x500u, kPUser = 0x99u;
  // Sem temporizador: zero exacto, sem depender do relogio.
  EXPECT_EQ(b.ChamaSaida(1600, kObjShell, kPfn, kPUser), 0u);
  // Arma 100 ms e pergunta pelo MESMO callback: restante em (0, 100].
  ASSERT_EQ(b.ChamaSaida(1520, kObjShell, 100u, kPfn, kPUser), kAeeSuccess);
  const std::uint32_t restante = b.ChamaSaida(1600, kObjShell, kPfn, kPUser);
  EXPECT_GT(restante, 0u) << "o temporizador de 100 ms nao venceu em poucas instrucoes";
  EXPECT_LE(restante, 100u) << "o restante nao passa do que se armou";
  // Outro callback: zero exacto (nao e o temporizador dele).
  EXPECT_EQ(b.ChamaSaida(1600, kObjShell, 0x501u, kPUser), 0u);
}

TEST(EntradaNoDespacho, HeapCheckAvailRespondePeloMaiorBlocoLivre) {
  Bancada b;
  // Heap de teste com 12 MiB livres no arranque: 1 MiB cabe, 4 GiB nao.
  EXPECT_EQ(b.ChamaSaida(9006, 0x80060000u, 1048576u), 1u);
  EXPECT_EQ(b.ChamaSaida(9006, 0x80060000u, 0xFFFFFFFFu), 0u);
}

// `IDisplay::SetFont` (slot 17): o `ddragonz` adota as tres fontes que acabou
// de criar (3x, uma por objeto). Guarda a anterior e devolve-a, como o SDK
// manda (`IFont *(*SetFont)(iname *po, AEEFont nFont, IFont *piFont)`).
TEST(EntradaNoDespacho, DisplaySetFontGuardaEDevolveAAnterior) {
  Bancada b;
  constexpr std::uint32_t kFonte = 0x8F00B000u;  // kObjetoFonte15
  constexpr std::uint32_t kSaidaSetFont = 1581;
  // Sem fonte: anterior e zero, e a nova fica guardada.
  EXPECT_EQ(b.ChamaSaida(kSaidaSetFont, kObjDisplay, 0u, kFonte), 0u);
  // Segunda troca devolve a primeira.
  EXPECT_EQ(b.ChamaSaida(kSaidaSetFont, kObjDisplay, 0u, 0x8F00C000u), kFonte);
  // Ponteiro que nao e fonte nossa: recusa COM O NOME, sem trocar.
  EXPECT_EQ(b.ChamaSaida(kSaidaSetFont, kObjDisplay, 0u, 0x10000000u), kAeeUnsupported);
  EXPECT_EQ(b.ChamaSaida(kSaidaSetFont, kObjDisplay, 0u, kFonte), 0x8F00C000u);
}


// ===========================================================================
// ROCKETWEB: a cadeia medida no create e GetDeviceInfoEx(KEY_SUPPORT) seguido
// de RegisterNotify. A estrutura KeySupportType tem quatro bytes no ARM:
// `AVKType key` (uint16, entrada) e `boolean supported` no byte +2.
// ===========================================================================
TEST(RocketwebShell, GetDeviceInfoExKeySupportRespeitaAbiENSize) {
  Bancada b;
  ConstruirOShell(b);
  constexpr std::uint32_t kBuffer = 0x00218000u, kNSize = 0x00218100u;
  const std::uint32_t slot = b.EntradaDaVtable(kObjShell, brew_slots::kShell_GetDeviceInfoEx);
  EXPECT_EQ(slot, b.S().Endereco(kBaseDoShell + brew_slots::kShell_GetDeviceInfoEx));

  // `key` chega do guest; so `supported` e saida. O byte +3 e padding ARM e
  // nao pertence ao campo boolean.
  b.Mem().Escrever16(kBuffer, kAvkA);
  b.Mem().Escrever8(kBuffer + 2, 0xaau);
  b.Mem().Escrever8(kBuffer + 3, 0xbbu);
  b.Mem().Escrever32(kNSize, kKeySupportTypeBytes);
  EXPECT_EQ(b.ChamaEndereco(slot, kObjShell, kAeeDeviceItemKeySupport, kBuffer, kNSize), kAeeSuccess);
  EXPECT_EQ(b.Mem().Ler16(kBuffer), kAvkA);
  EXPECT_EQ(b.Mem().Ler8(kBuffer + 2), 1u);
  EXPECT_EQ(b.Mem().Ler8(kBuffer + 3), 0xbbu);
  EXPECT_EQ(b.Mem().Ler32(kNSize), kKeySupportTypeBytes);

  // Menos que a estrutura completa nao pode escrever alem da capacidade. A
  // chamada ainda publica o tamanho requerido, como o contrato do IShell diz.
  b.Mem().Escrever16(kBuffer, kAvkA);
  b.Mem().Escrever8(kBuffer + 2, 0xccu);
  b.Mem().Escrever32(kNSize, 2u);
  EXPECT_EQ(b.ChamaEndereco(slot, kObjShell, kAeeDeviceItemKeySupport, kBuffer, kNSize), kAeeSuccess);
  EXPECT_EQ(b.Mem().Ler8(kBuffer + 2), 0xccu);
  EXPECT_EQ(b.Mem().Ler32(kNSize), kKeySupportTypeBytes);

  // Consulta de tamanho: pBuff pode ser nulo e o tamanho de entrada nao e lido.
  b.Mem().Escrever32(kNSize, 0xfeedfaceu);
  EXPECT_EQ(b.ChamaEndereco(slot, kObjShell, kAeeDeviceItemKeySupport, 0, kNSize), kAeeSuccess);
  EXPECT_EQ(b.Mem().Ler32(kNSize), kKeySupportTypeBytes);
}

TEST(RocketwebShell, CadeiaGetDeviceInfoExDepoisRegisterNotifySoGuardaORegisto) {
  Bancada b;
  ConstruirOShell(b);
  constexpr std::uint32_t kBuffer = 0x00218200u, kNSize = 0x00218300u;
  constexpr std::uint32_t kClsNotify = 0x0102a001u;
  constexpr std::uint32_t kClsType = 0x0102a002u;
  constexpr std::uint32_t kMask = 0x00000040u;
  b.Mem().Escrever16(kBuffer, kAvkA);
  b.Mem().Escrever32(kNSize, kKeySupportTypeBytes);
  EXPECT_EQ(b.ChamaEndereco(b.EntradaDaVtable(kObjShell, brew_slots::kShell_GetDeviceInfoEx),
                            kObjShell, kAeeDeviceItemKeySupport, kBuffer, kNSize),
            kAeeSuccess);

  const std::uint32_t slot = b.EntradaDaVtable(kObjShell, brew_slots::kShell_RegisterNotify);
  EXPECT_EQ(slot, b.S().Endereco(kBaseDoShell + brew_slots::kShell_RegisterNotify));
  EXPECT_EQ(b.ChamaEndereco(slot, kObjShell, kClsNotify, kClsType, kMask), kAeeSuccess);
  ASSERT_EQ(b.D().RegistosDeNotificacaoDoShell().size(), 1u);
  const auto& reg = b.D().RegistosDeNotificacaoDoShell().front();
  EXPECT_EQ(reg.classe_notificadora, kClsNotify);
  EXPECT_EQ(reg.classe_de_tipo, kClsType);
  EXPECT_EQ(reg.mascara, kMask);
  // RegisterNotify recebe CLSIDs, nao um ponteiro de callback: o registo nao
  // executa guest code nem inventa uma notificacao.
  EXPECT_EQ(b.Cpu().Get(kPC), kSentinela);
}

}  // namespace zb2::brew
