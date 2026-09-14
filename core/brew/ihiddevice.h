#ifndef ZB2_CORE_BREW_IHIDDEVICE_H
#define ZB2_CORE_BREW_IHIDDEVICE_H

// ETAPA 8 -- A ENTRADA: `IHID` + `IHIDDevice`, e os UIDs MEDIDOS.
//
// ---------------------------------------------------------------------------
// O QUE FALTAVA, medido antes de escrever codigo
// ---------------------------------------------------------------------------
// Rodando a arvore ANTIGA (binario ja construido, copiado para /tmp, sem tocar
// na arvore) com o instrumento que ela ja tinha:
//
//   ZEEB_LOG_HID_SLOT=1 ZEEB_LOG_HID=1 ./zeebulator_game_probe <tectoy.mod> - - 17237912
//
// o resultado foi UMA linha:
//
//   [hid_slot] slot 14 caiu no default (sucesso mudo)
//
// e mais nenhuma. Ou seja, no Z-Wheel:
//   * o UNICO slot do `IHIDDevice` que caiu no stub de sucesso mudo foi o 14;
//   * uma vez;
//   * e o `ZEEB_LOG_HID` (que imprime `RegisterForButtonEvent`, o slot 8) nao
//     imprimiu NADA -- o Z-Wheel nao registra eventos de BOTAO. Ele registra
//     MUDANCA DE POSICAO e le os eixos.
// Esse e o defeito: `RegisterForPositionChange` respondia SUCESSO e nunca
// sinalizava nada, e o menu ficava a espera para sempre.
//
// O NUMERO. O cabecalho diz 14, e nao 13:
//
//   INHERIT_IHIDDevice(iname) = INHERIT_IQI(iname) + 16 membros
//   INHERIT_IQI   = INHERIT_IBase (AddRef, Release) + QueryInterface = 3 slots
//
//   slot  3 GetDeviceInfo            slot 11 GetMinPositionInfo
//   slot  4 GetDeviceStatus          slot 12 GetMaxPositionInfo
//   slot  5 RegisterForStatusChange  slot 13 GetAxesInfo
//   slot  6 GetButtonInfo            slot 14 RegisterForPositionChange
//   slot  7 GetNumberOfButtons       slot 15 SetExclusiveLevel
//   slot  8 RegisterForButtonEvent   slot 16 GetExclusiveLevel
//   slot  9 GetNextButtonEvent       slot 17 Rumble
//   slot 10 GetPositionState         slot 18 GetRumbleStatus
//
// O ancora que fixa a contagem e o `GetNumberOfButtons`: quem afirma 7 tem de
// aceitar 3 slots de cabeca, e com 3 slots de cabeca o `RegisterForPositionChange`
// e o 14. `GetAxesInfo` e que e o 13. Foi conferido por duas vias:
//   1. `tools/gerar_slots.py` (o gerador que le os cabecalhos) escreve
//      `kHIDDevice_RegisterForPositionChange = 14` em `tools/brew_slots.inc`,
//      cujo conteudo e IDENTICO ao que o gerador produz agora do SDK
//      (`diff` vazio, "cabeca: INHERIT_IQI = 3 slots de cabeca, 16 metodos").
//   2. os `static_assert` abaixo, que fixam os numeros que eu transcrevi.
//
// ---------------------------------------------------------------------------
// OS UIDs: de ONDE VEM CADA UM
// ---------------------------------------------------------------------------
// A fonte primaria e o cabecalho do SDK que da NOME a cada UID:
//   "$SDK/platform/hardware/inc/AEEHIDDevice_Joystick.h" (linhas 21-46)
//
//   AEEUID_HID_Joystick_Device            0x0106c3fd   (linha 21)
//   AEEUID_HIDJoystick_DPad_Up            0x0106c3fe   (24)
//   AEEUID_HIDJoystick_DPad_Left          0x0106c3ff   (25)
//   AEEUID_HIDJoystick_DPad_Down          0x0106c400   (26)
//   AEEUID_HIDJoystick_DPad_Right         0x0106c401   (27)
//   AEEUID_HIDJoystick_Start              0x0106c402   (28)
//   AEEUID_HIDJoystick_Back               0x0106c403   (29)
//   AEEUID_HIDJoystick_Left_Thumbstick    0x0106c404   (30)
//   AEEUID_HIDJoystick_Right_Thumbstick   0x0106c405   (31)
//   AEEUID_HIDJoystick_Left_Shoulder_Upper 0x0106c406  (32)
//   AEEUID_HIDJoystick_Left_Shoulder_Lower  0x0106c407  (33)
//   AEEUID_HIDJoystick_Right_Shoulder_Upper 0x0106c408 (34)
//   AEEUID_HIDJoystick_Right_Shoulder_Lower 0x0106c409 (35)
//   AEEUID_HIDJoystick_Button_1           0x0106c40a   (36)
//   AEEUID_HIDJoystick_Button_2           0x0106c40b   (37)
//   AEEUID_HIDJoystick_Button_3           0x0106c40c   (38)
//   AEEUID_HIDJoystick_Button_4           0x0106c40d   (39)
//   AEEUID_HIDJoystick_RightThumb_X       0x0106c4ce   (42)
//   AEEUID_HIDJoystick_RightThumb_Y       0x0106c4cf   (43)
//   AEEUID_HIDJoystick_LeftThumb_X        0x0106c4d0   (44)
//   AEEUID_HIDJoystick_LeftThumb_Y        0x0106c4d1   (45)
//   AEEUID_HIDJoystick_Throttle           0x0106c4d3   (46)
//
// E confirmada, valor a valor, pela varredura dos 62 `.mod` do corpus (feita de
// novo nesta sessao, e nao citada): os eixos aparecem em 24 a 38 titulos cada, o
// `DPad_Up` em 23, o `AEEUID_HID_Joystick_Device` em 59. Numeros medidos com
// `/tmp` de trabalho -- a contagem esta no relatorio.
//
// ---------------------------------------------------------------------------
// O D-PAD NAO E UM CONTROLO UNICO -- e a medicao que o mostra
// ---------------------------------------------------------------------------
// `docs/PAREAMENTO-DE-UIDS-MEDIDO.md` conclui que "o d-pad parece ser UM controlo
// so, nao quatro botoes", porque nos 62 `.mod` o `DPad_Up` (0x0106c3fe) aparece em
// 23 titulos e o `DPad_Left`/`DPad_Right` em ZERO. A segunda metade e verdade (eu
// recontei: 0x3ff zero vezes, 0x401 zero vezes). A conclusao nao e.
//
// Os jogos NAO embutem os quatro UIDs porque os CALCULAM a partir do primeiro. O
// mesmo idioma aparece em 5 titulos independentes, e e este:
//
//   Rolimaz.mod      0x133cd4   ldr r3, [pc, #0xcc]   ; o pool (0x133da8) tem 0x0106c3fe
//                    0x133ce0   sub r2, r2, r3         ; r2 = uid - 0x0106c3fe
//                    0x133ce4   cmp r2, #0x10          ; DEZASSEIS valores
//                    0x133ce8   ldrlo pc, [pc, r2, lsl #2]
//   AirRacez.mod     0x143fa8   (mesmas quatro instrucoes)
//   zeebotennis.mod  0x129948   (idem)
//   funsoccer.mod    0x1b58c0   (idem)
//   a3d.mod          0x1041d4   ldr r2, [pc, #0xb8] / sub r1, r1, r2 / cmp r1, #0x10 /
//                               addlo pc, pc, r1, lsl #2
//
// Quem SUBTRAI o UID do `DPad_Up` e compara com 16 esta a tratar
// 0x0106c3fe..0x0106c40d como os DEZASSEIS botoes do joystick -- que e
// exatamente a lista do cabecalho acima. Um controlo unico com valor de direcao
// nao se le assim.
//
// E as INDICES. O Rolimaz mapeia UID->indice num despacho por tabela
// (`ldrlo pc, [pc, r2, lsl#2]` em 0x133ce8, tabela em 0x133cf0 com valores
// relativos 0x00033b30..0x00033b98, corpos em 0x133d30..0x133d98): `DPad_Up`->12,
// `DPad_Left`->13, `DPad_Down`->14, `DPad_Right`->15, `Back`->8,
// `Left_Thumbstick`->10, `Right_Thumbstick`->11, `Left_Shoulder_Upper`->4,
// `Right_Shoulder_Upper`->5, `Button_1..4`->0..3, e o resto nao mapeado.
// A correspondencia entre a tabela relativa e os corpos foi conferida pelos
// INTERVALOS: os 16 vaos entre destinos batem 1:1 com os 16 vaos entre corpos.
//
// A SEGUNDA FONTE NAO TEM ESSA AMBIGUIDADE, porque usa saltos RELATIVOS:
// `a3d.mod` 0x1041e4 `addlo pc, pc, r1, lsl #2`, com 16 `b` inline em
// 0x1041e8..0x104228 e os corpos em 0x10422c..0x104288:
//
//   0x3fe DPad_Up          -> 12    0x406 Left_Shoulder_Upper  -> 4
//   0x3ff DPad_Left        -> 13    0x408 Right_Shoulder_Upper -> 5
//   0x400 DPad_Down        -> 14    0x40a Button_1             -> 0
//   0x401 DPad_Right       -> 15    0x40b Button_2             -> 1
//   0x403 Back             ->  8    0x40c Button_3             -> 2
//                                   0x40d Button_4             -> 3
//     (0x402 Start, 0x404/0x405 os polegares, 0x407/0x409 os ombros de baixo e
//      0x40e..: NAO MAPEADOS -- o `a3d` devolve r0=1, "nao encontrado")
//
// OS DOZE PARES QUE O `a3d` MAPEIA BATEM 1:1 COM OS DO ROLIMAZ, e os tres UIDs
// que sobram (Start, Left_Shoulder_Lower, Right_Shoulder_Lower) sao exatamente os
// tres que ficam sem indice medido na tabela abaixo.
//
// A terceira fonte e o `enum eGamePadButtons` do sample do SDK
// (ZeeboSDKPackage-1.2.4/samples/.../conftest/GamepadMgr.h: GAMEPAD_DPAD_UP=12,
// GAMEPAD_DPAD_LEFT=13, GAMEPAD_DPAD_DOWN=14, GAMEPAD_DPAD_RIGHT=15), que da a
// mesma ordem para o d-pad.
//
// ---------------------------------------------------------------------------
// O VALOR DE UM EIXO: o que esta medido, e o que NAO esta
// ---------------------------------------------------------------------------
// MEDIDO -- o CENTRO e 128. `funsoccer.mod` (Zeebo F.C. Super League), 0x1ed504:
//
//   1ed504  mvn   r0, #0x7f      ; r0 = -128
//   1ed508  sxtah r4, r0, r4     ; (int16)r4 - 128
//   1ed50c  sxtah r3, r0, r3
//   1ed518  sxtah r3, r0, ip
//   1ed51c  sxtah r0, r0, r1
//   1ed510  strh  r4, [r2]       ; quatro eixos, convertidos para halfword
//   1ed514  strh  r3, [r2, #2]
//   1ed520  strh  r3, [r2, #4]
//   1ed524  strh  r0, [r2, #6]
//
// O jogo subtrai 128 para achar o centro: o repouso do aparelho e 128, e o valor
// do eixo cabe num halfword positivo (a extensao de sinal que ele aplica nao
// estraga um byte sem sinal).
//
// DECLARADO, e dito que e declarado -- os EXTREMOS 0 e 255. O que os sustenta:
//   * o centro medido e 128, e o campo e um byte sem sinal (`funsoccer` fecha em
//     `strh`, ou seja halfword, mas o valor vem de um byte: ver abaixo);
//   * o `hid_devices.cfg` do PROPRIO console declara os quatro eixos e nao lhes
//     da minimo nem maximo (research/sources/zeemu/rootfs/sys/hid_devices.cfg);
//   * a loja do console, no proprio NAND, usa um limiar sobre o eixo:
//     `mod/274755/tectoy.cfg:17  joystick_ignore_thresh=59` -- um limiar de 59
//     nao tem sentido numa escala de 16 bits, e tem-no numa escala centrada em
//     128.
// O QUE FALTA MEDIR, e fica escrito em vez de adivinhado: uma medicao DIRETA dos
// extremos seria um titulo que chamasse `GetMinPositionInfo`/`GetMaxPositionInfo`
// (slots 11 e 12) e normalizasse por `(max - min)`. Nenhum dos 62 foi medido a
// faze-lo. Este modulo publica min=0/max=255/centro=128 para os QUATRO eixos
// mapeados, e `0/0` (o marcador de "eixo nao suportado" do cabecalho) para os
// outros -- e o desvio, se a medicao aparecer, corrige-se AQUI e num so sitio.

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "core/brew/ihid_entrada.h"
#include "core/brew/interface.h"
#include "core/cpu/cpu.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"
#include "tools/brew_slots.inc"

namespace zb2::brew {

// --- os slots, e as ancoras -------------------------------------------------
//
// Os numeros do `IHIDDevice` vem do `brew_slots.inc` (GERADO dos cabecalhos). Os
// `static_assert` fixam o que eu TRANSCREVI do cabecalho a mao. Servem para as
// duas coisas nao poderem divergir em silencio: se alguem "corrigir" o `.inc`, o
// build para aqui.
static_assert(brew_slots::kHIDDevice_GetDeviceInfo == 3, "INHERIT_IQI = 3 slots");
static_assert(brew_slots::kHIDDevice_GetDeviceStatus == 4, "do cabecalho, em ordem");
static_assert(brew_slots::kHIDDevice_RegisterForStatusChange == 5, "do cabecalho");
static_assert(brew_slots::kHIDDevice_GetButtonInfo == 6, "do cabecalho");
static_assert(brew_slots::kHIDDevice_GetNumberOfButtons == 7, "a ancora da contagem");
static_assert(brew_slots::kHIDDevice_RegisterForButtonEvent == 8, "o slot do sample");
static_assert(brew_slots::kHIDDevice_GetNextButtonEvent == 9, "o slot do sample");
static_assert(brew_slots::kHIDDevice_GetPositionState == 10, "familia da posicao");
static_assert(brew_slots::kHIDDevice_GetMinPositionInfo == 11, "familia da posicao");
static_assert(brew_slots::kHIDDevice_GetMaxPositionInfo == 12, "familia da posicao");
static_assert(brew_slots::kHIDDevice_GetAxesInfo == 13, "NAO e o RegisterForPositionChange");
static_assert(brew_slots::kHIDDevice_RegisterForPositionChange == 14,
              "o slot que a Z-Wheel chama UMA vez, medido com ZEEB_LOG_HID_SLOT=1");
static_assert(brew_slots::kHIDDevice_SetExclusiveLevel == 15, "do cabecalho");
static_assert(brew_slots::kHIDDevice_GetExclusiveLevel == 16, "do cabecalho");
static_assert(brew_slots::kHIDDevice_Rumble == 17, "o aparelho nao tem motor");
static_assert(brew_slots::kHIDDevice_GetRumbleStatus == 18, "o ultimo");

// --- o IHID, que entrega o IHIDDevice ---------------------------------------
//
// Transcrito de "$SDK/platform/hardware/inc/AEEIHID.h", `INHERIT_IHID`:
//     INHERIT_IQI(iname) + CreateDevice + GetDeviceInfo + GetNextConnectEvent +
//     RegisterForConnectEvents + GetConnectedDevices
//
// O `tools/gerar_slots.py` da arvore NAO cobre este cabecalho (a lista de
// interfaces dele tem Shell/Display/FileMgr/HIDDevice/Heap1/SQLMgr/IAStream/IFile).
// Conferi os cinco numeros com o MESMO gerador, numa copia em /tmp com `IHID` na
// lista, e ele escreve `kIHID_CreateDevice = 3 ... kIHID_GetConnectedDevices = 7`
// -- `INHERIT_IQI = 3 slots de cabeca, 5 metodos`. Ver o relatorio: acrescentar
// `IHID` (e `ISignal*`) aquela lista e a proxima limpeza.
constexpr std::uint32_t kIHID_CreateDevice = 3;
constexpr std::uint32_t kIHID_GetDeviceInfo = 4;
constexpr std::uint32_t kIHID_GetNextConnectEvent = 5;
constexpr std::uint32_t kIHID_RegisterForConnectEvents = 6;
constexpr std::uint32_t kIHID_GetConnectedDevices = 7;

constexpr std::uint32_t kIidIHID = 0x0106c38du;        // AEEIHID.h
constexpr std::uint32_t kIidIHIDDevice = 0x0106c38eu;  // AEEIHIDDevice.h
constexpr std::uint32_t kClsidHid = 0x0106c411u;       // AEECLSID_HID, ja usado no despacho

// --- o pareamento MEDIDO ----------------------------------------------------
//
// O UID, o nome do SDK, e o INDICE (`nButtonID`) que o jogo usa. O indice NAO e
// invencao: vem do `enum eGamePadButtons` do sample do SDK e da tabela de saltos
// do Rolimaz, que concordam nos quatro que ambos cobrem (12,13,14,15).
//
// TRES ENTRADAS ESTAO MARCADAS COMO NAO MEDIDAS. O Rolimaz mapeia 13 dos 16 UIDs;
// sobram tres indices (6, 7 e 9) e tres UIDs sem indice medido (Start,
// Left_Shoulder_Lower, Right_Shoulder_Lower). A ordem deles aqui vem POR
// ELIMINACAO, e esta dito que vem -- e o sitio onde uma medicao futura corrige.
struct BotaoDoZeebo {
  std::uint32_t uid;
  const char* nome;      // o nome do SDK, sem o prefixo AEEUID_HIDJoystick_
  std::uint32_t id;      // `nButtonID`
  bool medido;           // o par (uid, id) tem medicao? ver acima
};

constexpr std::uint32_t kQuantosBotoes = 16;
constexpr std::uint32_t kQuantosBotoesSemMedicao = 3;
extern const BotaoDoZeebo kBotoesDoZeebo[kQuantosBotoes];

// Os EIXOS. A `palavra` e o indice da palavra em `AEEHIDPositionInfo` (a palavra
// 0 e o `bRelativeAxes`, e NAO um eixo). A atribuicao vem do arquivo do PROPRIO
// console, que declara o nome de cada eixo e o seu UID:
//
//   research/sources/zeemu/rootfs/sys/hid_devices.cfg
//     AXIS:X:0x0106c4d0   AXIS:Y:0x0106c4d1   AXIS:Z:0x0106c4ce   AXIS:RZ:0x0106c4cf
//
// e os nomes de CAMPO do `AEEHIDPositionInfo` (AEEIHIDDevice.h) sao
// nX(1) nY(2) nZ(3) nRx(4) nRy(5) nRz(6). X->nX, Y->nY, Z->nZ, RZ->nRz.
// O `Throttle` (0x0106c4d3) existe no cabecalho e NAO aparece em nenhum arquivo
// do console: fica FORA da tabela, e o `GetAxesInfo` publica -1 (o "nao
// suportado" do proprio cabecalho) em vez de lhe inventar uma palavra.
struct EixoDoZeebo {
  std::uint32_t uid;
  const char* nome;
  std::uint32_t palavra;
};
constexpr std::uint32_t kQuantosEixos = 4;
extern const EixoDoZeebo kEixosDoZeebo[kQuantosEixos];

// `AEEHIDPositionInfo` tem 25 palavras (1 + 24 eixos). O `bRelativeAxes` e a
// palavra 0.
constexpr std::uint32_t kPalavrasDaPosicao = 25;

constexpr std::int32_t kEixoMin = EntradaDoZeebo::kValorMin;
constexpr std::int32_t kEixoCentro = EntradaDoZeebo::kValorCentro;
constexpr std::int32_t kEixoMax = EntradaDoZeebo::kValorMax;

// --- os enderecos dos nossos objectos ---------------------------------------
//
// DECLARADOS, com a razao: fora da faixa do modulo (0x00100000..0x01100000, que o
// despacho usa como "e codigo do titulo"), acima do heap (0x80200000) e distantes
// das faixas que a `interface.h` ja ocupa (0x8001/2/3/4/5/6/7xxxx).
constexpr std::uint32_t kVtableIhid = 0x81010000u;
constexpr std::uint32_t kObjIhid = 0x81011000u;
constexpr std::uint32_t kVtableIhidDevice = 0x81020000u;
constexpr std::uint32_t kObjIhidDevice = 0x81021000u;
// (A fabrica de sinais, os objectos de sinal e o limite deles ficam em
// `ihid_entrada.h`: sao da alçada do modulo `Sinais`, e nao do `IHID`.)

// O `handle` do unico dispositivo que este emulador reporta.
//
// DECLARADO: nao ha hardware para o numerar. O sample do SDK usa o que o
// `IHID_GetConnectedDevices` lhe deu, logo qualquer valor nao nulo serve -- e o
// valor esta aqui, num sitio so, porque o `CreateDevice` e o `GetDeviceInfo`
// tem de concordar sobre ele.
constexpr std::uint32_t kHandleDoDispositivo = 1;

// ---------------------------------------------------------------------------
// Ihid -- o IHID e o IHIDDevice na memoria do guest
// ---------------------------------------------------------------------------
class Ihid {
 public:
  Ihid(Memoria& mem, Traco& traco, Sinais& sinais, EntradaDoZeebo& entrada);

  // COMO OS INDICES DA FAIXA DE SAIDA FICAM: o slot `i` de uma vtable aponta
  // para o endereco de saida `base_das_saidas + base_da_interface + i`. Dois
  // blocos, um por interface.
  static constexpr std::uint32_t kBaseDoIhid = 0;
  static constexpr std::uint32_t kBaseDoDispositivo = 8;

  // Quantos indices da faixa de saida este modulo ocupa.
  static constexpr std::uint32_t kSlotsNecessarios = 32;

  // Constroi as vtables, os objectos e a cablagem, e CONFIRMA a cablagem com
  // uma leitura de volta (o desenho da `interface.h`: uma cablagem ja se perdeu
  // numa edicao de texto sem nada acusar).
  bool Construir(const Saidas& saidas, std::uint32_t base_das_saidas);

  // Atende um pedido da faixa deste modulo. Devolve false quando o indice nao e
  // deste modulo (o chamador segue para o resto da cadeia).
  bool Atender(ICpu& cpu, std::uint32_t indice);

  // Aplica a entrada ate ao instante corrente, enfileira os eventos de botao e
  // MARCA os sinais registados. Devolve true quando pos um callback no guest
  // (PC = funcao do titulo, R0 = contexto, LR = sentinela): quem chama tem de
  // o deixar correr, como o despacho ja faz com os callbacks de temporizador.
  bool Bombear(ICpu& cpu);

  // --- consulta, para os testes e para o relatorio ------------------------
  std::uint32_t DispositivosConectados() const { return conectados_; }
  std::uint32_t EventosEntregues() const { return eventos_entregues_; }
  std::uint32_t SinalizacoesDePosicao() const { return sinalizacoes_posicao_; }
  std::uint32_t SinalizacoesDeBotao() const { return sinalizacoes_botao_; }
  std::uint32_t QuantosEixosMapeados() const { return kQuantosEixos; }
  std::uint32_t EnderecoDoDispositivo() const { return kObjIhidDevice; }
  std::uint32_t EnderecoDoIhid() const { return kObjIhid; }

 private:
  bool AtenderIhid(ICpu& cpu, std::uint32_t slot);
  bool AtenderDispositivo(ICpu& cpu, std::uint32_t slot);

  // Os handlers, um por slot com comportamento.
  void GetDeviceInfo(ICpu& cpu);
  void GetDeviceStatus(ICpu& cpu);
  void RegisterForStatusChange(ICpu& cpu);
  void GetButtonInfo(ICpu& cpu);
  void GetNumberOfButtons(ICpu& cpu);
  void RegisterForButtonEvent(ICpu& cpu);
  void GetNextButtonEvent(ICpu& cpu);
  void GetPositionState(ICpu& cpu);
  void GetMinPositionInfo(ICpu& cpu);
  void GetMaxPositionInfo(ICpu& cpu);
  void GetAxesInfo(ICpu& cpu);
  void RegisterForPositionChange(ICpu& cpu);
  void SetExclusiveLevel(ICpu& cpu);
  void GetExclusiveLevel(ICpu& cpu);
  void Rumble(ICpu& cpu);
  void GetRumbleStatus(ICpu& cpu);
  void QueryInterfaceIhidDevice(ICpu& cpu);

  // Escreve as 25 palavras de `AEEHIDPositionInfo` com o modo pedido.
  enum class ModoDaPosicao { Estado, Minimo, Maximo, Uids };
  void EscreverPosicao(ICpu& cpu, ModoDaPosicao modo);

  // O valor de um eixo, do guiao. Devolve `centro` para um UID que o guiao nao
  // conhece.
  std::int32_t ValorDoEixo(std::uint32_t uid) const;
  // O indice em `kBotoesDoZeebo` para um UID ou para um indice, ou -1.
  int IndiceDoBotao(std::uint32_t argumento) const;

  // Recusa em voz alta (P2): regista a falta e devolve o codigo.
  std::uint32_t Recusar(ICpu& cpu, const char* o_que, const char* porque);

  Memoria& mem_;
  Traco& traco_;
  Sinais& sinais_;
  EntradaDoZeebo& entrada_;

  std::uint32_t base_ = 0;
  bool construido_ = false;

  // Os sinais que o jogo registrou. Zero = nao registado.
  //
  // LIDO do sample do SDK: o `RegisterForButtonEvent` e o
  // `RegisterForPositionChange` recebem um `ISignal *` -- e o Z-Wheel so usa o
  // segundo (medido). Como nao ha sinal registado, nao ha callback a marcar: o
  // estado por omissao e o silencio, e nao um sinal inventado.
  std::uint32_t sinal_de_botao_ = 0;
  std::uint32_t sinal_de_posicao_ = 0;
  std::uint32_t sinal_de_estado_ = 0;
  // O sinal do `IHID_RegisterForConnectEvents`. Guardado e NUNCA marcado: o
  // dispositivo esta ligado desde o inicio, logo nao ha mudanca para avisar.
  std::uint32_t sinal_de_conexao_ = 0;

  // O estado dos botoes: um por entrada da tabela.
  std::uint32_t estado_botao_[kQuantosBotoes] = {};

  // A fila de eventos de botao, no formato que o jogo le
  // (`AEEHIDButtonInfo` de 20 bytes). Uma fila, e nao uma chamada directa: e o
  // contrato do `GetNextButtonEvent` ("retrieves the next button event").
  struct EventoDeBotao {
    std::uint32_t id;
    std::uint32_t estado;
    std::uint32_t uid;
    std::uint32_t quando_ms;
  };
  std::deque<EventoDeBotao> fila_;

  std::uint32_t conectados_ = 0;
  std::uint32_t eventos_entregues_ = 0;
  std::uint32_t sinalizacoes_posicao_ = 0;
  std::uint32_t sinalizacoes_botao_ = 0;
  std::int32_t nivel_exclusivo_ = 0;
  std::uint32_t ultimo_instante_bombeado_ = 0;
  bool ja_bombeou_ = false;

  // Os valores de eixo da ultima injecao, para saber quando MUDOU (o contrato do
  // `RegisterForPositionChange` e "quando a posicao muda").
  std::int32_t ultimo_eixo_[kQuantosEixos] = {};
  bool primeira_injecao_ = true;
};

}  // namespace zb2::brew

#endif  // ZB2_CORE_BREW_IHIDDEVICE_H
