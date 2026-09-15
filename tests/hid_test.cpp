#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "core/brew/ihid_entrada.h"
#include "core/brew/ihiddevice.h"
#include "core/cpu/arm_interpreter.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"
#include "tools/brew_slots.inc"

namespace zb2::brew {
namespace {

// ===========================================================================
// A BANCADA
//
// O `ICpu` e o INTERPRETADOR DE VERDADE (`core/cpu/arm_interpreter.h`), e nao um
// duble. Um callback de sinal tem de CHEGAR AO GUEST, e a unica prova de que
// chega e o codigo do guest correr e escrever: com um duble, o teste afirmaria
// que nos chamamos aquilo que nos proprios escrevemos no duble.
// ===========================================================================
constexpr std::uint32_t kBaseDasSaidas = 500;   // indice, nao endereco
constexpr std::uint32_t kPilha = 0x00200000u;
constexpr std::uint32_t kArg0 = 0x00201000u;    // argumentos de pilha / saidas
// A BASE DO MODULO E ZERO (medida): o codigo do titulo comeca no inicio da
// imagem. A rotina de teste vive em 0x200, dentro dos 1 MiB que a bancada declara.
constexpr std::uint32_t kRotina = 0x00000200u;  // "codigo do titulo"
constexpr std::uint32_t kMarcador = 0x00000300u;
constexpr std::uint32_t kContexto = 0xDEADBEEFu;

class Bancada {
 public:
  explicit Bancada(const std::string& guiao = "") {
    saidas_.base = 0x90000000u;
    saidas_.passo = 4;
    saidas_.quantos = kBaseDasSaidas + 64;
    saidas_.ativa = true;
    cpu_.ConfigurarSaidas(saidas_);
    cpu_.Repor(kRotina, kPilha);
    traco_.JuntarDestino(&destino_);
    if (!guiao.empty()) {
      std::string motivo;
      guiao_ok_ = EntradaDoZeebo::Ler(guiao, &entrada_, &motivo);
      guiao_motivo_ = motivo;
    }
    // A FAIXA DO MODULO DO TITULO: base ZERO (medida) e um tamanho declarado.
    // A rotina de teste vive em 0x200, dentro da faixa. Sem isto o modulo da
    // entrada recusa TODO o callback -- que e a resposta certa quando nao se sabe
    // onde esta o codigo, e nao um salto para o desconhecido.
    sinais_.DefinirFaixaDoModulo(0, 0x00100000u);
    sinais_ok_ = sinais_.Construir(saidas_, kBaseDasSaidas);
    ihid_ok_ = ihid_.Construir(saidas_, kBaseDasSaidas);
  }

  // Uma chamada do guest a um slot: os quatro primeiros argumentos nos registos
  // (AAPCS) e o resto na pilha.
  std::uint32_t ChamaDispositivo(std::uint32_t slot, std::uint32_t r0, std::uint32_t r1 = 0,
                                 std::uint32_t r2 = 0, std::uint32_t r3 = 0,
                                 std::uint32_t na_pilha = 0) {
    return Chama(ihid_, Ihid::kBaseDoDispositivo, slot, r0, r1, r2, r3, na_pilha);
  }
  std::uint32_t ChamaPai(std::uint32_t slot, std::uint32_t r0, std::uint32_t r1 = 0,
                         std::uint32_t r2 = 0, std::uint32_t r3 = 0,
                         std::uint32_t na_pilha = 0) {
    return Chama(ihid_, Ihid::kBaseDoIhid, slot, r0, r1, r2, r3, na_pilha);
  }
  std::uint32_t ChamaFabrica(std::uint32_t slot, std::uint32_t r0, std::uint32_t r1 = 0,
                             std::uint32_t r2 = 0, std::uint32_t r3 = 0,
                             std::uint32_t na_pilha = 0) {
    return ChamaSinais(Sinais::kBaseDaFabrica, slot, r0, r1, r2, r3, na_pilha);
  }
  std::uint32_t ChamaSinal(std::uint32_t slot, std::uint32_t r0, std::uint32_t r1 = 0,
                           std::uint32_t r2 = 0) {
    return ChamaSinais(Sinais::kBaseDoSinal, slot, r0, r1, r2, 0, 0);
  }

  // Cria um sinal como o sample do SDK o cria: funcao, contexto, e os DOIS
  // ponteiros de saida -- o `ppiSig` no r3 e o `ppiSigCtl` na pilha.
  std::uint32_t CriaSinal(std::uint32_t funcao, std::uint32_t contexto) {
    constexpr std::uint32_t kSaida = 0x00202000u;
    mem_.Escrever32(kSaida, 0);
    mem_.Escrever32(kSaida + 4, 0);
    const std::uint32_t r = ChamaFabrica(kISignalCBFactory_CreateSignal,
                                         sinais_.EnderecoDaFabrica(), funcao, contexto, kSaida,
                                         kSaida + 4);
    if (r != kAeeSuccess) return 0;
    return mem_.Ler32(kSaida + 4);  // o ISignalCtl e o objecto que o jogo guarda
  }

  // O codigo do "titulo": guarda o contexto que recebeu e volta.
  //
  //   00100200  ldr r1, [pc, #4]   ; -> 00100210
  //   00100204  str r0, [r1]
  //   00100208  bx  lr
  //   00100210  .word marcador
  void EscreverRotina() {
    mem_.Escrever32(kRotina + 0, 0xE59F1004u);
    mem_.Escrever32(kRotina + 4, 0xE5810000u);
    mem_.Escrever32(kRotina + 8, 0xE12FFF1Eu);
    mem_.Escrever32(kRotina + 12, kMarcador);
  }

  std::size_t QuantasFaltas(const std::string& nome) const {
    const auto& f = traco_.ContagemFaltas();
    const auto it = f.find(nome);
    return it == f.end() ? 0 : static_cast<std::size_t>(it->second);
  }

  // Os PRESSUPOSTOS sao a metade que a bateria publica e o `faltas` NAO cobre:
  // um valor respondido sem medicao. A `auditoria` mediu que `ausencia de falta
  // != ausencia de chamada` (`aud-media-hid-widget.md`, secao e), logo o zero
  // teclados tem de ter contador proprio.
  std::size_t QuantosPressupostos(const std::string& nome) const {
    const auto& p = traco_.ContagemPressupostos();
    const auto it = p.find(nome);
    return it == p.end() ? 0 : static_cast<std::size_t>(it->second);
  }

  Memoria& Mem() { return mem_; }
  Traco& Tr() { return traco_; }
  DestinoMemoria& Destino() { return destino_; }
  ArmInterpreter& Cpu() { return cpu_; }
  EntradaDoZeebo& Entrada() { return entrada_; }
  Sinais& Sinal() { return sinais_; }
  Ihid& Dispositivo() { return ihid_; }
  bool GuiaoOk() const { return guiao_ok_; }
  const std::string& MotivoDoGuiao() const { return guiao_motivo_; }
  const Saidas& SaidasDaBancada() const { return saidas_; }

 private:
  std::uint32_t Chama(Ihid& modulo, std::uint32_t base, std::uint32_t slot, std::uint32_t r0,
                      std::uint32_t r1, std::uint32_t r2, std::uint32_t r3,
                      std::uint32_t na_pilha) {
    cpu_.Set(kR0, r0);
    cpu_.Set(kR1, r1);
    cpu_.Set(kR2, r2);
    cpu_.Set(kR3, r3);
    cpu_.Set(kSP, kArg0);
    mem_.Escrever32(kArg0, na_pilha);
    const bool atendido = modulo.Atender(cpu_, kBaseDasSaidas + base + slot);
    EXPECT_TRUE(atendido) << "o slot " << slot << " nao era deste modulo";
    return cpu_.Get(kR0);
  }

  std::uint32_t ChamaSinais(std::uint32_t base, std::uint32_t slot, std::uint32_t r0,
                            std::uint32_t r1, std::uint32_t r2, std::uint32_t r3,
                            std::uint32_t na_pilha) {
    cpu_.Set(kR0, r0);
    cpu_.Set(kR1, r1);
    cpu_.Set(kR2, r2);
    cpu_.Set(kR3, r3);
    cpu_.Set(kSP, kArg0);
    mem_.Escrever32(kArg0, na_pilha);
    const bool atendido = sinais_.Atender(cpu_, kBaseDasSaidas + base + slot);
    EXPECT_TRUE(atendido) << "o slot " << slot << " nao era dos sinais";
    return cpu_.Get(kR0);
  }

  Memoria mem_;
  Traco traco_{"teste_hid", nullptr};
  DestinoMemoria destino_;
  Saidas saidas_;
  ArmInterpreter cpu_{mem_, &traco_};
  EntradaDoZeebo entrada_;
  Sinais sinais_{mem_, traco_};
  Ihid ihid_{mem_, traco_, sinais_, entrada_};
  bool guiao_ok_ = true;
  std::string guiao_motivo_;
  bool sinais_ok_ = false;
  bool ihid_ok_ = false;
};

// ===========================================================================
// A TABELA, INDEPENDENTE DO MODULO
//
// Escrita aqui a partir do CABECALHO (`AEEHIDDevice_Joystick.h`, linhas 24-39) e
// do `enum eGamePadButtons` do sample (`GamepadMgr.h`), e do que o Rolimaz mede
// na tabela de saltos dele (0x133cf0). Se o modulo e esta tabela divergirem, um
// dos dois esta errado -- e isso e que se quer saber.
// ===========================================================================
struct UidDoCabecalho {
  std::uint32_t uid;
  const char* nome;
  std::uint32_t id;
};
const UidDoCabecalho kDoCabecalho[16] = {
    {0x0106c40au, "Button_1", 0},
    {0x0106c40bu, "Button_2", 1},
    {0x0106c40cu, "Button_3", 2},
    {0x0106c40du, "Button_4", 3},
    {0x0106c406u, "Left_Shoulder_Upper", 4},
    {0x0106c408u, "Right_Shoulder_Upper", 5},
    {0x0106c407u, "Left_Shoulder_Lower", 6},
    {0x0106c409u, "Right_Shoulder_Lower", 7},
    {0x0106c403u, "Back", 8},
    {0x0106c402u, "Start", 9},
    {0x0106c404u, "Left_Thumbstick", 10},
    {0x0106c405u, "Right_Thumbstick", 11},
    {0x0106c3feu, "DPad_Up", 12},
    {0x0106c3ffu, "DPad_Left", 13},
    {0x0106c400u, "DPad_Down", 14},
    {0x0106c401u, "DPad_Right", 15},
};

}  // namespace

// ===========================================================================
// 1. A TABELA CONFERE COM O CABECALHO
// ===========================================================================
TEST(Hid, TabelaDeBotoesConfereComOCabecalho) {
  ASSERT_EQ(kQuantosBotoes, 16u);
  for (std::uint32_t k = 0; k < kQuantosBotoes; ++k) {
    EXPECT_EQ(kBotoesDoZeebo[k].uid, kDoCabecalho[k].uid) << "uid na posicao " << k;
    EXPECT_STREQ(kBotoesDoZeebo[k].nome, kDoCabecalho[k].nome) << "nome na posicao " << k;
    EXPECT_EQ(kBotoesDoZeebo[k].id, kDoCabecalho[k].id) << "indice na posicao " << k;
  }
  // Os dezasseis UIDs sao CONSECUTIVOS -- nao pela ordem da tabela (que e a dos
  // indices), mas como CONJUNTO: e o que torna possivel o idioma medido em 5
  // titulos (`sub rX, uid, 0x0106c3fe; cmp rX, #0x10`).
  std::vector<std::uint32_t> uids;
  for (std::uint32_t k = 0; k < 16; ++k) uids.push_back(kDoCabecalho[k].uid);
  std::sort(uids.begin(), uids.end());
  for (std::uint32_t k = 0; k < 16; ++k) {
    EXPECT_EQ(uids[k], 0x0106c3feu + k) << "o conjunto dos UIDs tem de ser 0x0106c3fe..0x0106c40d";
  }
  // O D-PAD NAO E UM CONTROLO UNICO: quatro UIDs proprios, nas posicoes 12 a 15.
  EXPECT_EQ(kBotoesDoZeebo[12].uid, 0x0106c3feu);
  EXPECT_EQ(kBotoesDoZeebo[13].uid, 0x0106c3ffu);
  EXPECT_EQ(kBotoesDoZeebo[14].uid, 0x0106c400u);
  EXPECT_EQ(kBotoesDoZeebo[15].uid, 0x0106c401u);
  EXPECT_EQ(kBotoesDoZeebo[12].id, 12u);
  EXPECT_EQ(kBotoesDoZeebo[15].id, 15u);
  // NENHUMA ENTRADA SEM FONTE (era 3: os indices 6, 7 e 9). O contador fica -- e o
  // instrumento que acende se alguem acrescentar uma entrada sem fonte.
  std::uint32_t sem_medicao = 0;
  for (std::uint32_t k = 0; k < kQuantosBotoes; ++k) {
    if (!kBotoesDoZeebo[k].medido) ++sem_medicao;
  }
  EXPECT_EQ(sem_medicao, kQuantosBotoesSemMedicao);
  EXPECT_EQ(kQuantosBotoesSemMedicao, 0u);
}

TEST(Hid, OsDozeIndicesDoArquivoDoConsoleEstaoNaTabela) {
  // A FONTE, linha a linha: `research/sources/zeemu/rootfs/sys/hid_devices.cfg`,
  // entrada "Logitech Dual Action" (as cinco entradas do ficheiro trazem os MESMOS
  // doze pares -- so a do RumblePad2 diverge no `BUTTON:9`, ver o cabecalho do
  // `ihiddevice.cpp`). O primeiro campo e o `nButtonID`, o segundo o UID.
  const std::uint32_t kDoArquivo[12] = {
      0x0106c40a,  // BUTTON:0
      0x0106c40b,  // BUTTON:1
      0x0106c40c,  // BUTTON:2
      0x0106c40d,  // BUTTON:3
      0x0106c406,  // BUTTON:4
      0x0106c408,  // BUTTON:5
      0x0106c407,  // BUTTON:6  <- era "indice NAO medido"
      0x0106c409,  // BUTTON:7  <- era "indice NAO medido"
      0x0106c403,  // BUTTON:8
      0x0106c402,  // BUTTON:9  <- era "indice NAO medido"
      0x0106c404,  // BUTTON:10
      0x0106c405,  // BUTTON:11
  };
  for (std::uint32_t k = 0; k < 12; ++k) {
    EXPECT_EQ(kBotoesDoZeebo[k].uid, kDoArquivo[k])
        << "BUTTON:" << k << " do hid_devices.cfg do console";
    EXPECT_EQ(kBotoesDoZeebo[k].id, k) << "o indice do arquivo e o nButtonID";
    EXPECT_TRUE(kBotoesDoZeebo[k].medido) << "a entrada " << k << " tem fonte";
  }
  // E OS TRES QUE FORAM PROMOVIDOS, pelo nome, para o teste nao passar por engano
  // se a ordem da tabela mudar.
  EXPECT_EQ(kBotoesDoZeebo[6].uid, 0x0106c407u);
  EXPECT_STREQ(kBotoesDoZeebo[6].nome, "Left_Shoulder_Lower");
  EXPECT_EQ(kBotoesDoZeebo[7].uid, 0x0106c409u);
  EXPECT_STREQ(kBotoesDoZeebo[7].nome, "Right_Shoulder_Lower");
  EXPECT_EQ(kBotoesDoZeebo[9].uid, 0x0106c402u);
  EXPECT_STREQ(kBotoesDoZeebo[9].nome, "Start");
}

TEST(Hid, TabelaDeEixosConfereComOArquivoDoConsole) {
  // research/sources/zeemu/rootfs/sys/hid_devices.cfg:
  //   AXIS:X:0x0106c4d0  AXIS:Y:0x0106c4d1  AXIS:Z:0x0106c4ce  AXIS:RZ:0x0106c4cf
  // e `AEEHIDPositionInfo`: a palavra 0 e o `bRelativeAxes`, e os eixos comecam
  // na 1 (nX=1, nY=2, nZ=3, nRx=4, nRy=5, nRz=6).
  ASSERT_EQ(kQuantosEixos, 4u);
  EXPECT_EQ(kEixosDoZeebo[0].uid, 0x0106c4d0u);
  EXPECT_EQ(kEixosDoZeebo[1].uid, 0x0106c4d1u);
  EXPECT_EQ(kEixosDoZeebo[2].uid, 0x0106c4ceu);
  EXPECT_EQ(kEixosDoZeebo[3].uid, 0x0106c4cfu);
  EXPECT_EQ(kEixosDoZeebo[0].palavra, 1u);
  EXPECT_EQ(kEixosDoZeebo[1].palavra, 2u);
  EXPECT_EQ(kEixosDoZeebo[2].palavra, 3u);
  EXPECT_EQ(kEixosDoZeebo[3].palavra, 6u);
  for (std::uint32_t k = 0; k < kQuantosEixos; ++k) {
    EXPECT_NE(kEixosDoZeebo[k].palavra, 0u) << "a palavra 0 e o bRelativeAxes";
    EXPECT_LT(kEixosDoZeebo[k].palavra, kPalavrasDaPosicao);
  }
  // As 25 palavras: 1 + 24 eixos (X,Y,Z,Rx,Ry,Rz, VX..VRz, AX..ARz, FX..FRz).
  EXPECT_EQ(kPalavrasDaPosicao, 25u);
}

TEST(Hid, TabelaDeSlotsConfereComOCabecalho) {
  // O gerador dos cabecalhos e a minha transcricao tem de dar o mesmo numero.
  EXPECT_EQ(brew_slots::kHIDDevice_GetDeviceInfo, 3u);
  EXPECT_EQ(brew_slots::kHIDDevice_GetNumberOfButtons, 7u);
  EXPECT_EQ(brew_slots::kHIDDevice_GetAxesInfo, 13u);
  EXPECT_EQ(brew_slots::kHIDDevice_RegisterForPositionChange, 14u);
  EXPECT_EQ(brew_slots::kHIDDevice_GetRumbleStatus, 18u);
  // 3 slots de cabeca (INHERIT_IQI) + 16 membros = 19 slots.
  EXPECT_EQ(kIHID_CreateDevice, 3u);
  EXPECT_EQ(kIHID_GetConnectedDevices, 7u);
  EXPECT_EQ(kISignal_Set, 3u);
  EXPECT_EQ(kISignalCtl_Detach, 4u);
  EXPECT_EQ(kISignalCtl_Enable, 5u);
  EXPECT_EQ(kISignalCBFactory_CreateSignal, 3u);
}

// ===========================================================================
// 2. UM EVENTO DE BOTAO CHEGA AO CALLBACK REGISTADO -- ate ao fim, no guest
// ===========================================================================
TEST(Hid, BotaoChegaAoCallbackRegistadoEOCodigoDoGuestCorre) {
  // O primeiro evento do guiao esta aos 10 ms DE PROPOSITO: assim da para
  // afirmar que, no instante 0, NAO ha nada a fazer -- um guiao cujo primeiro
  // evento e aos 0 ms aplica-se na primeira bombagem, e isso e o que se quer
  // (o instante do guiao e o instante da corrida).
  Bancada b("10 botao 0x0106c3fe 1\n30 botao 0x0106c3fe 0\n");
  ASSERT_TRUE(b.GuiaoOk()) << b.MotivoDoGuiao();
  b.EscreverRotina();

  const std::uint32_t sinal = b.CriaSinal(kRotina, kContexto);
  ASSERT_NE(sinal, 0u) << "a fabrica devia ter criado um sinal";
  EXPECT_EQ(b.ChamaDispositivo(brew_slots::kHIDDevice_RegisterForButtonEvent,
                               b.Dispositivo().EnderecoDoDispositivo(), sinal),
            kAeeSuccess);

  // Antes de haver mudanca, NAO ha callback.
  EXPECT_FALSE(b.Dispositivo().Bombear(b.Cpu()));
  EXPECT_EQ(b.Mem().Ler32(kMarcador), 0u);

  // O botao e premido aos 10 ms: o `Bombear` tem de por o callback no guest.
  b.Entrada().Avancar(10);
  ASSERT_TRUE(b.Dispositivo().Bombear(b.Cpu()));
  EXPECT_EQ(b.Cpu().Get(kPC), kRotina) << "o PC tem de apontar para a funcao do titulo";
  EXPECT_EQ(b.Cpu().Get(kR0), kContexto) << "o r0 e o CONTEXTO do AEECallback";
  EXPECT_EQ(b.Cpu().Get(kLR), kSentinelaPadrao) << "o retorno volta para a sentinela";

  // E o codigo do titulo corre de verdade.
  b.Cpu().Passo();
  b.Cpu().Passo();
  b.Cpu().Passo();
  EXPECT_EQ(b.Mem().Ler32(kMarcador), kContexto)
      << "o callback registado nao recebeu o par (funcao, contexto)";

  // O EVENTO ficou na fila, e o jogo le-o com o UID MEDIDO.
  constexpr std::uint32_t kInfo = 0x00203000u;
  EXPECT_EQ(b.ChamaDispositivo(brew_slots::kHIDDevice_GetNextButtonEvent,
                               b.Dispositivo().EnderecoDoDispositivo(), kInfo, 0, 0),
            kAeeSuccess);
  EXPECT_EQ(b.Mem().Ler32(kInfo + 0), kBotoesDoZeebo[12].id) << "nButtonID do DPad_Up";
  EXPECT_EQ(b.Mem().Ler32(kInfo + 4), 1u) << "nState";
  EXPECT_EQ(b.Mem().Ler32(kInfo + 8), 0x0106c3feu) << "nButtonUID";
  EXPECT_EQ(b.Mem().Ler32(kInfo + 12), 0u) << "nButtonMin de um botao digital";
  EXPECT_EQ(b.Mem().Ler32(kInfo + 16), 1u) << "nButtonMax de um botao digital";
}

// ===========================================================================
// 3. O CAMINHO DA Z-WHEEL: registar so a POSICAO (slot 14) e o eixo mexer
// ===========================================================================
TEST(Hid, MudancaDePosicaoChegaAoSinalRegistadoNoSlotQuatorze) {
  Bancada b("0 eixo 0x0106c4d0 0\n40 eixo 0x0106c4d0 255\n80 eixo 0x0106c4d0 128\n");
  ASSERT_TRUE(b.GuiaoOk()) << b.MotivoDoGuiao();
  b.EscreverRotina();
  const std::uint32_t sinal = b.CriaSinal(kRotina, kContexto);
  ASSERT_NE(sinal, 0u);

  // O slot 14 e o `RegisterForPositionChange` (medido na Z-Wheel com
  // ZEEB_LOG_HID_SLOT=1). O teste chama-o PELO NUMERO do cabecalho.
  EXPECT_EQ(b.ChamaDispositivo(brew_slots::kHIDDevice_RegisterForPositionChange,
                               b.Dispositivo().EnderecoDoDispositivo(), sinal),
            kAeeSuccess);

  // O guiao ja poe o eixo em 0 no instante 0: a primeira injecao e uma mudanca,
  // porque o repouso e 128.
  ASSERT_TRUE(b.Dispositivo().Bombear(b.Cpu()));
  EXPECT_EQ(b.Cpu().Get(kPC), kRotina);
  EXPECT_EQ(b.Dispositivo().SinalizacoesDePosicao(), 1u);
  b.Cpu().Passo();
  b.Cpu().Passo();
  b.Cpu().Passo();

  // SEM mudanca, NAO ha callback: o contrato e "quando a posicao muda".
  b.Entrada().Avancar(10);  // ainda antes dos 40 ms
  EXPECT_FALSE(b.Dispositivo().Bombear(b.Cpu()));
  EXPECT_EQ(b.Dispositivo().SinalizacoesDePosicao(), 1u);

  b.Entrada().Avancar(30);  // 40 ms: o eixo vai a 255
  EXPECT_TRUE(b.Dispositivo().Bombear(b.Cpu()));
  EXPECT_EQ(b.Dispositivo().SinalizacoesDePosicao(), 2u);
}

TEST(Hid, SemSinalRegistadoNaoHaCallbackNemMudancaDeEstado) {
  // O estado por omissao tem de ser o SILENCIO: um sinal marcado sem ninguem
  // registado seria um callback para o nada.
  Bancada b("0 eixo 0x0106c4d0 0\n");
  ASSERT_TRUE(b.GuiaoOk());
  EXPECT_FALSE(b.Dispositivo().Bombear(b.Cpu()));
  EXPECT_EQ(b.Dispositivo().SinalizacoesDePosicao(), 0u);
  EXPECT_EQ(b.Dispositivo().SinalizacoesDeBotao(), 0u);
}

TEST(Hid, RegistarComPonteiroQueNaoEsSinalERecusadoERegistado) {
  Bancada b;
  EXPECT_EQ(b.ChamaDispositivo(brew_slots::kHIDDevice_RegisterForPositionChange,
                               b.Dispositivo().EnderecoDoDispositivo(), 0x12345678u),
            kAeeBadParm);
  EXPECT_EQ(b.QuantasFaltas("IHIDDevice::RegisterForPositionChange"), 1u);
}

TEST(Hid, CallbackParaForaDoModuloNaoSeChama) {
  // 0x50000000 nao e codigo do titulo. Saltar para la levava o emulador a
  // "saiu_do_modulo", um sintoma que nao diz nada sobre a causa.
  Bancada b("0 botao 0x0106c3fe 1\n");
  ASSERT_TRUE(b.GuiaoOk());
  const std::uint32_t sinal = b.CriaSinal(0x50000000u, kContexto);
  ASSERT_NE(sinal, 0u);
  ASSERT_EQ(b.ChamaDispositivo(brew_slots::kHIDDevice_RegisterForButtonEvent,
                               b.Dispositivo().EnderecoDoDispositivo(), sinal),
            kAeeSuccess);
  EXPECT_FALSE(b.Dispositivo().Bombear(b.Cpu()));
  EXPECT_EQ(b.QuantasFaltas("callback_de_sinal"), 1u);
  EXPECT_EQ(b.Cpu().Get(kPC), kRotina) << "o PC nao pode ter sido mexido";
}

// ===========================================================================
// 4. O EIXO: INTERVALO E CENTRO, COM OS NUMEROS NO TESTE
// ===========================================================================
TEST(Hid, EixoNoRepousoEhCentroEAsPalavrasNaoMapeadasSaoNaoSuportadas) {
  Bancada b;  // sem guiao: nenhum eixo foi injectado
  constexpr std::uint32_t kInfo = 0x00204000u;
  ASSERT_EQ(b.ChamaDispositivo(brew_slots::kHIDDevice_GetPositionState,
                               b.Dispositivo().EnderecoDoDispositivo(), kInfo),
            kAeeSuccess);
  EXPECT_EQ(b.Mem().Ler32(kInfo + 0), 0u) << "palavra 0 = bRelativeAxes (absoluto)";
  EXPECT_EQ(b.Mem().Ler32(kInfo + 1 * 4), 128u) << "repouso = centro MEDIDO (funsoccer 0x1ed504)";
  EXPECT_EQ(b.Mem().Ler32(kInfo + 2 * 4), 128u);
  EXPECT_EQ(b.Mem().Ler32(kInfo + 3 * 4), 128u);
  EXPECT_EQ(b.Mem().Ler32(kInfo + 6 * 4), 128u);
  // A palavra 4 e a 5 (nRx, nRy) nao tem eixo mapeado: o cabecalho diz que
  // min==max==0 significa "nao suportado", e e isso que se publica.
  EXPECT_EQ(b.Mem().Ler32(kInfo + 4 * 4), 0u);
  EXPECT_EQ(b.Mem().Ler32(kInfo + 5 * 4), 0u);
  EXPECT_EQ(b.Mem().Ler32(kInfo + 24 * 4), 0u) << "a ultima palavra tambem tem de ser escrita";

  ASSERT_EQ(b.ChamaDispositivo(brew_slots::kHIDDevice_GetMinPositionInfo,
                               b.Dispositivo().EnderecoDoDispositivo(), kInfo),
            kAeeSuccess);
  for (std::uint32_t k = 1; k < kPalavrasDaPosicao; ++k) {
    EXPECT_EQ(b.Mem().Ler32(kInfo + k * 4), 0u) << "minimo da palavra " << k;
  }
  ASSERT_EQ(b.ChamaDispositivo(brew_slots::kHIDDevice_GetMaxPositionInfo,
                               b.Dispositivo().EnderecoDoDispositivo(), kInfo),
            kAeeSuccess);
  EXPECT_EQ(b.Mem().Ler32(kInfo + 1 * 4), 255u);
  EXPECT_EQ(b.Mem().Ler32(kInfo + 2 * 4), 255u);
  EXPECT_EQ(b.Mem().Ler32(kInfo + 3 * 4), 255u);
  EXPECT_EQ(b.Mem().Ler32(kInfo + 6 * 4), 255u);
  EXPECT_EQ(b.Mem().Ler32(kInfo + 4 * 4), 0u) << "min==max==0 = eixo nao suportado";
  EXPECT_EQ(kEixoMin, 0);
  EXPECT_EQ(kEixoCentro, 128);
  EXPECT_EQ(kEixoMax, 255);

  ASSERT_EQ(b.ChamaDispositivo(brew_slots::kHIDDevice_GetAxesInfo,
                               b.Dispositivo().EnderecoDoDispositivo(), kInfo),
            kAeeSuccess);
  EXPECT_EQ(b.Mem().Ler32(kInfo + 0), 0u);
  EXPECT_EQ(b.Mem().Ler32(kInfo + 1 * 4), 0x0106c4d0u);
  EXPECT_EQ(b.Mem().Ler32(kInfo + 2 * 4), 0x0106c4d1u);
  EXPECT_EQ(b.Mem().Ler32(kInfo + 3 * 4), 0x0106c4ceu);
  EXPECT_EQ(b.Mem().Ler32(kInfo + 6 * 4), 0x0106c4cfu);
  // "If the UID is -1 that indicates that the axis is not supported."
  EXPECT_EQ(b.Mem().Ler32(kInfo + 4 * 4), 0xFFFFFFFFu);
  EXPECT_EQ(b.Mem().Ler32(kInfo + 24 * 4), 0xFFFFFFFFu);
}

TEST(Hid, OEixoInjectionadoApareceNoEstado) {
  Bancada b("0 eixo 0x0106c4d1 255\n");
  ASSERT_TRUE(b.GuiaoOk());
  b.Entrada().Avancar(1);
  constexpr std::uint32_t kInfo = 0x00205000u;
  ASSERT_EQ(b.ChamaDispositivo(brew_slots::kHIDDevice_GetPositionState,
                               b.Dispositivo().EnderecoDoDispositivo(), kInfo),
            kAeeSuccess);
  EXPECT_EQ(b.Mem().Ler32(kInfo + 2 * 4), 255u) << "o eixo Y vai para a palavra nY (2)";
  EXPECT_EQ(b.Mem().Ler32(kInfo + 1 * 4), 128u) << "o X fica no repouso";
}

// ===========================================================================
// 5. O EVENTO: PONTEIROS NULOS, FIM DA FILA, E O RELOGIO INJECTADO
// ===========================================================================
TEST(Hid, GetNextButtonEventAceitaPonteirosNulosEDepoisDizQueAcabou) {
  // O sample do SDK chama isto com os DOIS ultimos argumentos a NULO:
  //   IHIDDevice_GetNextButtonEvent(pIHIDDevice, &bi, NULL, NULL)
  // (GamepadMgr.c, L_JoystickButtonCB). Escrever num ponteiro nulo matava o jogo.
  Bancada b("0 botao 0x0106c3ff 1\n");
  ASSERT_TRUE(b.GuiaoOk());
  b.Entrada().Avancar(1);
  b.Dispositivo().Bombear(b.Cpu());
  constexpr std::uint32_t kInfo = 0x00206000u;
  ASSERT_EQ(b.ChamaDispositivo(brew_slots::kHIDDevice_GetNextButtonEvent,
                               b.Dispositivo().EnderecoDoDispositivo(), kInfo, 0, 0),
            kAeeSuccess);
  EXPECT_EQ(b.Mem().Ler32(kInfo + 8), 0x0106c3ffu) << "o DPad_Left, pelo UID do cabecalho";
  // A fila esta vazia: "AEE_ENOMORE : No more events are pending".
  EXPECT_EQ(b.ChamaDispositivo(brew_slots::kHIDDevice_GetNextButtonEvent,
                               b.Dispositivo().EnderecoDoDispositivo(), kInfo, 0, 0),
            kAeeNoMore);
  EXPECT_EQ(kAeeNoMore, 47u) << "o numero tem de ser o do AEEStdErr.h:63";
}

TEST(Hid, OTimestampDoEventoVemDoRelogioInjectado) {
  // P4: o `pdwTimestamp` e o instante do GUIAO, e nao o relogio do hospedeiro.
  // A arvore antiga escrevia `SDL_GetTicks()` aqui (game_probe.cpp,
  // GetNextButtonEvent) -- e duas corridas do mesmo titulo deixavam de ser
  // comparaveis. Este teste ficaria VERMELHO se alguem voltasse a ler o relogio
  // do sistema: o guiao premio o botao aos 1234 ms.
  Bancada b("0 botao 0x0106c402 1\n1234 botao 0x0106c402 0\n");
  ASSERT_TRUE(b.GuiaoOk());
  b.Entrada().Avancar(2000);
  b.Dispositivo().Bombear(b.Cpu());
  constexpr std::uint32_t kInfo = 0x00207000u, kTs = 0x00207020u, kDrop = 0x00207024u;
  b.Mem().Escrever32(kTs, 0xFFFFFFFFu);
  b.Mem().Escrever32(kDrop, 0xFFFFFFFFu);
  ASSERT_EQ(b.ChamaDispositivo(brew_slots::kHIDDevice_GetNextButtonEvent,
                               b.Dispositivo().EnderecoDoDispositivo(), kInfo, kTs, kDrop),
            kAeeSuccess);
  EXPECT_EQ(b.Mem().Ler32(kTs), 0u) << "o instante do primeiro evento";
  ASSERT_EQ(b.ChamaDispositivo(brew_slots::kHIDDevice_GetNextButtonEvent,
                               b.Dispositivo().EnderecoDoDispositivo(), kInfo, kTs, kDrop),
            kAeeSuccess);
  EXPECT_EQ(b.Mem().Ler32(kTs), 1234u) << "o relogio injectado, e nao o do hospedeiro";
  EXPECT_EQ(b.Mem().Ler32(kDrop), 0u);
}

TEST(Hid, NaoEscreveNoEnderecoZeroQuandoOsPonteirosSaoNulos) {
  // A guarda do ponteiro nulo tem de ser MESMO uma guarda.
  //
  // A memoria do guest e ESPARSA: escrever no endereco 0 ALOCA a pagina 0, logo
  // `Memoria::Existe(0)` denuncia a escrita SEJA QUAL FOR O VALOR escrito. Isto
  // importa porque a primeira versao deste teste so olhava para o VALOR, e um
  // evento com o instante 0 escrevia zero no endereco zero e o teste passava com
  // a guarda arrancada -- foi assim que a violacao deliberada desta guarda deu
  // VERDE. O instante do evento aqui e 7, e nao 0, para o valor tambem servir de
  // denuncia.
  Bancada b("7 botao 0x0106c3fe 1\n");
  ASSERT_TRUE(b.GuiaoOk());
  ASSERT_FALSE(b.Mem().Existe(0)) << "nada pode ter tocado na pagina 0 antes do teste";
  b.Entrada().Avancar(10);
  b.Dispositivo().Bombear(b.Cpu());
  constexpr std::uint32_t kInfo = 0x00208000u;
  EXPECT_EQ(b.Mem().Ler32(0), 0u);
  ASSERT_EQ(b.ChamaDispositivo(brew_slots::kHIDDevice_GetNextButtonEvent,
                               b.Dispositivo().EnderecoDoDispositivo(), kInfo, 0, 0),
            kAeeSuccess);
  EXPECT_FALSE(b.Mem().Existe(0)) << "escreveu na pagina 0: a guarda do ponteiro nulo caiu";
  EXPECT_EQ(b.Mem().Ler32(0), 0u);
}

// ===========================================================================
// 6. CONTAGEM DE BOTOES E O `IHID`
// ===========================================================================
TEST(Hid, GetNumberOfButtonsEhDezesseis) {
  Bancada b;
  constexpr std::uint32_t kSaida = 0x00209000u;
  ASSERT_EQ(b.ChamaDispositivo(brew_slots::kHIDDevice_GetNumberOfButtons,
                               b.Dispositivo().EnderecoDoDispositivo(), kSaida),
            kAeeSuccess);
  // Dezasseis: o tamanho da lista de UIDs de botao do cabecalho e o
  // `NUM_OF_GAMEPAD_BUTTONS` do sample do SDK.
  EXPECT_EQ(b.Mem().Ler32(kSaida), 16u);
}

TEST(Hid, OIhidEntregaUmDispositivoDoTipoJoystick) {
  Bancada b;
  constexpr std::uint32_t kHandles = 0x0020a000u, kReq = 0x0020a100u, kInfo = 0x0020a200u;
  b.Mem().Escrever32(kReq, 0xFFFFFFFFu);
  ASSERT_EQ(b.ChamaPai(kIHID_GetConnectedDevices, b.Dispositivo().EnderecoDoIhid(), 0x0106c3fdu,
                       kHandles, 2, kReq),
            kAeeSuccess);
  EXPECT_EQ(b.Mem().Ler32(kReq), 1u);
  const std::uint32_t handle = b.Mem().Ler32(kHandles);
  EXPECT_EQ(handle, kHandleDoDispositivo);

  // Um tipo de dispositivo que NAO existe aqui (o teclado, 0x0106c3fc, pedido por
  // 11 dos 62 titulos): zero dispositivos, e dito.
  b.Mem().Escrever32(kReq, 0xFFFFFFFFu);
  ASSERT_EQ(b.ChamaPai(kIHID_GetConnectedDevices, b.Dispositivo().EnderecoDoIhid(),
                       0x0106c3fcu, kHandles, 2, kReq),
            kAeeSuccess);
  EXPECT_EQ(b.Mem().Ler32(kReq), 0u);

  // O `CreateDevice` do handle reportado devolve o IHIDDevice.
  constexpr std::uint32_t kPp = 0x0020a300u;
  ASSERT_EQ(b.ChamaPai(kIHID_CreateDevice, b.Dispositivo().EnderecoDoIhid(), handle, kPp),
            kAeeSuccess);
  EXPECT_EQ(b.Mem().Ler32(kPp), b.Dispositivo().EnderecoDoDispositivo());

  // E o `GetDeviceInfo` do IHID diz o tipo que o sample compara com
  // `AEEUID_HID_Joystick_Device`.
  ASSERT_EQ(b.ChamaPai(kIHID_GetDeviceInfo, b.Dispositivo().EnderecoDoIhid(), handle, kInfo),
            kAeeSuccess);
  EXPECT_EQ(b.Mem().Ler32(kInfo + 0), 0x0106c3fdu);
  EXPECT_EQ(b.Mem().Ler32(kInfo + 8) & 0xFFu, 0u) << "bBluetoothDevice";
}

TEST(Hid, GetNextConnectEventDizQueNaoHaNada) {
  // A arvore antiga respondia `0` aqui e mediu o resultado: o `abd` girou 20 mil
  // milhoes de passos num laco `while(SUCCESS == ...)`. "Nao ha evento" tem de ser
  // dito com o codigo que significa isso.
  Bancada b;
  constexpr std::uint32_t kA = 0x0020b000u;
  EXPECT_EQ(b.ChamaPai(kIHID_GetNextConnectEvent, b.Dispositivo().EnderecoDoIhid(), kA, kA + 4,
                       kA + 8),
            kAeeNoMore);
  EXPECT_EQ(kAeeNoMore, 47u);
}

// ===========================================================================
// 7. AS RECUSAS: SLOTS SEM IMPLEMENTACAO, RUMBLE, E A FAIXA CURTA
// ===========================================================================
TEST(Hid, RumbleRecusaComEunsupportedERegistado) {
  Bancada b;
  // "AEE_EUNSUPPORTED : if the device does not support rumble" (AEEIHIDDevice.h).
  EXPECT_EQ(b.ChamaDispositivo(brew_slots::kHIDDevice_Rumble,
                               b.Dispositivo().EnderecoDoDispositivo(), 65535, 65535),
            kAeeUnsupported);
  EXPECT_EQ(kAeeUnsupported, 20u);
  EXPECT_EQ(b.QuantasFaltas("IHIDDevice::Rumble"), 1u);
  EXPECT_EQ(b.ChamaDispositivo(brew_slots::kHIDDevice_GetRumbleStatus,
                               b.Dispositivo().EnderecoDoDispositivo(), 0x0020c000u),
            kAeeUnsupported);
  EXPECT_EQ(b.QuantasFaltas("IHIDDevice::GetRumbleStatus"), 1u);
}

TEST(Hid, BotaoDesconhecidoRecusaComEnosuch) {
  Bancada b;
  constexpr std::uint32_t kInfo = 0x0020d000u;
  EXPECT_EQ(b.ChamaDispositivo(brew_slots::kHIDDevice_GetButtonInfo,
                               b.Dispositivo().EnderecoDoDispositivo(), 0xDEADBEEFu, kInfo),
            kAeeNoSuch);
  EXPECT_EQ(b.QuantasFaltas("IHIDDevice::GetButtonInfo"), 1u);
  // O mesmo botao, pedido pelo UID E pelo indice, da a MESMA resposta.
  ASSERT_EQ(b.ChamaDispositivo(brew_slots::kHIDDevice_GetButtonInfo,
                               b.Dispositivo().EnderecoDoDispositivo(), 0x0106c3feu, kInfo),
            kAeeSuccess);
  const std::uint32_t id = b.Mem().Ler32(kInfo + 0);
  const std::uint32_t uid = b.Mem().Ler32(kInfo + 8);
  EXPECT_EQ(id, 12u);
  EXPECT_EQ(uid, 0x0106c3feu);
  ASSERT_EQ(b.ChamaDispositivo(brew_slots::kHIDDevice_GetButtonInfo,
                               b.Dispositivo().EnderecoDoDispositivo(), 12u, kInfo),
            kAeeSuccess);
  EXPECT_EQ(b.Mem().Ler32(kInfo + 0), id);
  EXPECT_EQ(b.Mem().Ler32(kInfo + 8), uid);
}

TEST(Hid, ConstruirRecusaQuandoAFaixaDeSaidaEhCurta) {
  // A guarda da faixa de saida, provada por violacao: com poucos indices, o
  // modulo RECUSA e diz qual falta, em vez de escrever fora da faixa.
  Memoria mem;
  Traco traco("teste_hid", nullptr);
  DestinoMemoria destino;
  traco.JuntarDestino(&destino);
  EntradaDoZeebo entrada;
  Sinais sinais(mem, traco);
  Ihid ihid(mem, traco, sinais, entrada);
  Saidas curtas;
  curtas.base = 0x90000000u;
  curtas.passo = 4;
  curtas.quantos = Ihid::kSlotsNecessarios - 1;  // falta um indice
  curtas.ativa = true;
  EXPECT_FALSE(ihid.Construir(curtas, 0));
  EXPECT_EQ(static_cast<std::size_t>(traco.ContagemFaltas().count("Ihid::Construir")), 1u);
  // A MESMA guarda, no outro modulo: com 15 indices a fabrica de sinais tambem
  // recusa. Um limite que so um dos dois modulos verifica nao e um limite.
  Saidas muito_curtas;
  muito_curtas.base = 0x90000000u;
  muito_curtas.passo = 4;
  muito_curtas.quantos = Sinais::kSlotsNecessarios - 1;
  muito_curtas.ativa = true;
  EXPECT_FALSE(sinais.Construir(muito_curtas, 0));
  EXPECT_EQ(static_cast<std::size_t>(traco.ContagemFaltas().count("Sinais::Construir")), 1u);

  Saidas ok;
  ok.base = 0x90000000u;
  ok.passo = 4;
  ok.quantos = Ihid::kSlotsNecessarios + Sinais::kSlotsNecessarios;
  ok.ativa = true;
  EXPECT_TRUE(sinais.Construir(ok, 0));
  EXPECT_TRUE(ihid.Construir(ok, Sinais::kSlotsNecessarios));
}

// ===========================================================================
// 8. O GUIAO: ELE RECUSA O QUE NAO PODE SER, E NAO O APLICA PELA METADE
// ===========================================================================
TEST(Hid, GuiaoInvalidoEhRecusadoInteiro) {
  EntradaDoZeebo e;
  std::string motivo;
  // Um eixo fora de 0..255.
  EXPECT_FALSE(EntradaDoZeebo::Ler("0 eixo 0x0106c4d0 300\n", &e, &motivo));
  EXPECT_NE(motivo.find("fora de"), std::string::npos) << motivo;
  // Um botao com um estado que nao e 0 nem 1.
  EXPECT_FALSE(EntradaDoZeebo::Ler("0 botao 0x0106c3fe 5\n", &e, &motivo));
  EXPECT_NE(motivo.find("nState"), std::string::npos) << motivo;
  // Fora de ordem.
  EXPECT_FALSE(EntradaDoZeebo::Ler("10 eixo 0x0106c4d0 0\n5 eixo 0x0106c4d0 1\n", &e, &motivo));
  EXPECT_NE(motivo.find("fora de ordem"), std::string::npos) << motivo;
  // Tipo desconhecido.
  EXPECT_FALSE(EntradaDoZeebo::Ler("0 roda 0x0106c4d0 0\n", &e, &motivo));
  EXPECT_NE(motivo.find("desconhecido"), std::string::npos) << motivo;
  // Linha mal formada.
  EXPECT_FALSE(EntradaDoZeebo::Ler("0 eixo 0x0106c4d0\n", &e, &motivo));
  // E, o mais importante: um guiao com uma linha ma NAO fica aplicado pela
  // metade -- o que sai de `Ler` com `false` nao chegou a mexer no destino.
  EntradaDoZeebo destino;
  std::string antes = "0 eixo 0x0106c4d0 7\n";
  ASSERT_TRUE(EntradaDoZeebo::Ler(antes, &destino, &motivo)) << motivo;
  const std::size_t n = destino.Quantos();
  EXPECT_FALSE(EntradaDoZeebo::Ler("0 eixo 0x0106c4d0 1\n20 eixo 0x0106c4d0 900\n", &destino,
                                   &motivo));
  EXPECT_EQ(destino.Quantos(), n);
}

TEST(Hid, GuiaoValidoEhAceitoEComentariosSaoIgnorados) {
  EntradaDoZeebo e;
  std::string motivo;
  ASSERT_TRUE(EntradaDoZeebo::Ler(
      "# o menu da Z-Wheel a andar para cima\n"
      "0 eixo 0x0106c4d0 128   # repouso\n"
      "100 eixo 0x0106c4d0 255\n"
      "200 eixo 0x0106c4d0 128\n",
      &e, &motivo))
      << motivo;
  ASSERT_EQ(e.Quantos(), 3u);
  EXPECT_EQ(e.Guiao()[1].t_ms, 100u);
  bool desconhecido = false;
  EXPECT_EQ(e.ValorEm(150, 0x0106c4d0u, &desconhecido), 255);
  EXPECT_FALSE(desconhecido);
  EXPECT_EQ(e.ValorEm(50, 0x0106c4d1u, &desconhecido), 0);
  EXPECT_TRUE(desconhecido) << "o guiao nao fala deste eixo";
}

// ===========================================================================
// 9. P4: DUAS CORRIDAS DO MESMO GUIAO SAO IDENTICAS
// ===========================================================================
namespace {

// Corre um guiao e devolve o que se observa: os eventos entregues (uid, estado,
// instante) e as linhas de traco da area de Entrada.
std::vector<std::string> Corrida(const std::string& guiao) {
  Bancada b(guiao);
  EXPECT_TRUE(b.GuiaoOk()) << b.MotivoDoGuiao();
  const std::uint32_t sinal = b.CriaSinal(kRotina, kContexto);
  b.ChamaDispositivo(brew_slots::kHIDDevice_RegisterForButtonEvent,
                     b.Dispositivo().EnderecoDoDispositivo(), sinal);
  b.ChamaDispositivo(brew_slots::kHIDDevice_RegisterForPositionChange,
                     b.Dispositivo().EnderecoDoDispositivo(), sinal);
  std::vector<std::string> visto;
  constexpr std::uint32_t kInfo = 0x0020e000u, kTs = 0x0020e020u;
  for (int t = 0; t <= 60; ++t) {
    b.Entrada().Avancar(5);
    b.Dispositivo().Bombear(b.Cpu());
    // Le tudo o que a fila tiver, como o callback do sample faz.
    while (b.ChamaDispositivo(brew_slots::kHIDDevice_GetNextButtonEvent,
                              b.Dispositivo().EnderecoDoDispositivo(), kInfo, kTs, 0) ==
           kAeeSuccess) {
      visto.push_back("botao uid=" + std::to_string(b.Mem().Ler32(kInfo + 8)) +
                      " estado=" + std::to_string(b.Mem().Ler32(kInfo + 4)) +
                      " t=" + std::to_string(b.Mem().Ler32(kTs)));
    }
    if (b.Sinal().QuantosMarcados() != 0) {
      visto.push_back("posicao palavra1=" + std::to_string(
                                                [&] {
                                                  std::uint32_t v = 0;
                                                  b.ChamaDispositivo(
                                                      brew_slots::kHIDDevice_GetPositionState,
                                                      b.Dispositivo().EnderecoDoDispositivo(),
                                                      kInfo);
                                                  v = b.Mem().Ler32(kInfo + 4);
                                                  return v;
                                                }()));
    }
  }
  for (const auto& e : b.Destino().eventos) {
    if (e.area == Area::Entrada) visto.push_back("traco " + e.nome + " " + e.detalhe);
  }
  return visto;
}

}  // namespace

TEST(Hid, DuasCorridasDoMesmoGuiaoDaoOMesmoResultado) {
  const std::string guiao =
      "0 eixo 0x0106c4d0 128\n"
      "50 botao 0x0106c3fe 1\n"
      "100 botao 0x0106c3fe 0\n"
      "150 eixo 0x0106c4d0 255\n"
      "250 eixo 0x0106c4d0 128\n"
      "300 botao 0x0106c401 1\n";
  const auto a = Corrida(guiao);
  const auto c = Corrida(guiao);
  EXPECT_FALSE(a.empty());
  EXPECT_EQ(a, c) << "duas corridas do mesmo guiao tem de ser identicas (P4)";

  // E o teste nao e vazio: um guiao DIFERENTE da um resultado diferente.
  const auto d = Corrida(
      "0 eixo 0x0106c4d0 128\n"
      "50 botao 0x0106c3ff 1\n"
      "100 botao 0x0106c3ff 0\n"
      "150 eixo 0x0106c4d0 255\n"
      "250 eixo 0x0106c4d0 128\n"
      "300 botao 0x0106c401 1\n");
  EXPECT_NE(a, d);
}

// ===========================================================================
// 10. O `nDeviceType` DO `IHID::GetConnectedDevices`: ZERO, TIPO DECLARADO, LIXO
//
// A citacao que governa os tres casos e do proprio metodo (`AEEIHID.h`, copia
// local, e `BrewMPSDK-7.12.5/.../documentation/API Reference/Hardware/HID/
// methods/IHID_GetConnectedDevices.htm:69,109`):
//
//   "The nDeviceType parameter should be set to either a UID for the type of
//    device that the user is interested in or 0 to return all attached devices."
//   "AEE_EBADPARM : if an unsupported device type is specified."
//
// O primeiro caso e o achado **B2** da auditoria
// (`docs/rewrite/auditoria/aud-media-hid-widget.md:58`): `0` respondia zero
// dispositivos. Nao era um caso de laboratorio -- `0` e o valor documentado
// para "todos".
// ===========================================================================
TEST(Hid, GetConnectedDevicesComZeroDevolveTodosOsDispositivos) {
  Bancada b;
  constexpr std::uint32_t kHandles = 0x0020b800u, kReq = 0x0020b900u;
  b.Mem().Escrever32(kReq, 0xFFFFFFFFu);
  ASSERT_EQ(b.ChamaPai(kIHID_GetConnectedDevices, b.Dispositivo().EnderecoDoIhid(), 0, kHandles, 2,
                       kReq),
            kAeeSuccess);
  EXPECT_EQ(b.Mem().Ler32(kReq), 1u) << "0 = TODOS os dispositivos ligados, e ha um";
  EXPECT_EQ(b.Mem().Ler32(kHandles), kHandleDoDispositivo);
}

TEST(Hid, UmTipoDeclaradoSemDispositivoRespondeZeroEConta) {
  Bancada b;
  constexpr std::uint32_t kHandles = 0x0020ba00u, kReq = 0x0020bb00u;
  // A lista comeca com um valor que NAO e handle nenhum: se o caminho do "tipo
  // declarado sem dispositivo" escrevesse na lista, o teste ve-lo-ia.
  b.Mem().Escrever32(kHandles, 0xA5A5A5A5u);
  // O teclado (0x0106c3fc, `AEEHIDDevice_Keyboard.h:21`) e o rato (0x0106c3fb,
  // `AEEHIDDevice_Mouse.h:21`) sao tipos DECLARADOS. Zero dispositivos e a
  // resposta certa -- e NAO pode ser muda: e um valor que este emulador NAO
  // mediu (nao ha caminho de teclado nenhum aqui), logo vai como PRESSUPOSTO,
  // que e a metade que a bateria publica.
  ASSERT_EQ(b.ChamaPai(kIHID_GetConnectedDevices, b.Dispositivo().EnderecoDoIhid(), 0x0106c3fcu,
                       kHandles, 2, kReq),
            kAeeSuccess);
  EXPECT_EQ(b.Mem().Ler32(kReq), 0u);
  EXPECT_EQ(b.QuantosPressupostos("IHID::GetConnectedDevices sem Keyboard_Device"), 1u);
  ASSERT_EQ(b.ChamaPai(kIHID_GetConnectedDevices, b.Dispositivo().EnderecoDoIhid(), 0x0106c3fbu,
                       kHandles, 2, kReq),
            kAeeSuccess);
  EXPECT_EQ(b.QuantosPressupostos("IHID::GetConnectedDevices sem Mouse_Device"), 1u);
  // Um tipo declarado e sem dispositivo NAO inventa handle nenhum: a lista fica
  // como estava (nao se escreve lixo no array do jogo).
  EXPECT_EQ(b.Mem().Ler32(kHandles), 0xA5A5A5A5u) << "nada foi escrito pelo teclado nem pelo rato";
}

TEST(Hid, UmTipoQueOSdkNaoDeclaraERecusadoComONome) {
  Bancada b;
  constexpr std::uint32_t kHandles = 0x0020bc00u, kReq = 0x0020bd00u;
  b.Mem().Escrever32(kReq, 0xFFFFFFFFu);
  // `AEE_EBADPARM` = 14 (`AEEStdErr.h:30`) e o codigo que o metodo declara para
  // "unsupported device type". Lixo NAO passa em silencio, e o numero do tipo
  // entra no NOME da falta: uma falta sem o numero obriga a ir ao desmonte.
  EXPECT_EQ(b.ChamaPai(kIHID_GetConnectedDevices, b.Dispositivo().EnderecoDoIhid(), 0xDEADBEEFu,
                       kHandles, 2, kReq),
            kAeeBadParm);
  EXPECT_EQ(kAeeBadParm, 14u);
  EXPECT_EQ(b.QuantasFaltas("IHID::GetConnectedDevices tipo 0xdeadbeef"), 1u);
  // E o `Unknown_DeviceType` (0x106c3fa, `AEEIHIDDevice.h:34`) tambem nao e um
  // tipo para ENUMERAR: a lista que a documentacao do metodo aceita e joystick,
  // teclado e rato (e zero = todos).
  EXPECT_EQ(b.ChamaPai(kIHID_GetConnectedDevices, b.Dispositivo().EnderecoDoIhid(), 0x0106c3fau,
                       kHandles, 2, kReq),
            kAeeBadParm);
  EXPECT_EQ(b.QuantasFaltas("IHID::GetConnectedDevices tipo 0x0106c3fa"), 1u);
  // O `AEEUID_HID_Unknown_DeviceType` e o marcador do `GetDeviceInfo` para um
  // aparelho nao identificado (`AEEIHIDDevice.h:34`), NAO uma classe para
  // enumerar: nao esta na tabela, e um pedido com ele e um tipo nao suportado.
  for (std::uint32_t k = 0; k < kQuantosTipos; ++k) {
    EXPECT_NE(kTiposDeDispositivo[k].uid, 0x0106c3fau);
  }
  EXPECT_EQ(kQuantosTipos, 3u);
  // Recusar nao escreve handle nenhum na lista, e o comprimento sai ZERO em vez do
  // 0xFFFFFFFF que o teste la pos: um jogo que leia o comprimento sem olhar para o
  // codigo de erro ve "nenhum dispositivo", e nao um numero que nao existe.
  EXPECT_EQ(b.Mem().Ler32(kReq), 0u);
  EXPECT_EQ(b.Mem().Ler32(kHandles), 0u);
}

}  // namespace zb2::brew
