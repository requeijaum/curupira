#include "core/brew/ihiddevice.h"

#include <cstdio>

namespace zb2::brew {

namespace {

std::string Hex(std::uint32_t v) {
  char b[16];
  std::snprintf(b, sizeof(b), "0x%08x", v);
  return std::string(b);
}

constexpr std::uint32_t kUidDesconhecido = 0xFFFFFFFFu;  // "-1" = nao suportado

}  // namespace

// --- a tabela dos botoes ----------------------------------------------------
//
// Os 16 UIDs sao os do cabecalho do SDK (`AEEHIDDevice_Joystick.h`, linhas 24-39
// do `BrewMPSDK-7.12.5/SDKPro/1.0.4.601 Pro/platform/hardware/inc/`).
//
// A COLUNA DO INDICE TEM DUAS FONTES, e as duas cobrem coisas diferentes:
//
//  1. OS DOZE PRIMEIROS (0..11), do ARQUIVO DO PROPRIO CONSOLE: cada uma das
//     cinco entradas de controle do `hid_devices.cfg` declara
//     `BUTTON:<indice>:<UID>`, e todos os doze pares batem com esta tabela.
//     `research/sources/zeemu/rootfs/sys/hid_devices.cfg` (as cinco entradas) e,
//     pelo lado do SDK, `AEEHID.bid` -- o texto que documenta o FORMATO do
//     arquivo traz a mesma entrada "Generic Joystick" com os mesmos doze pares.
//     Nao vem dos jogos: vem do console e do SDK, que sao as duas fontes que
//     podem dizer qual e o `nButtonID`.
//     A UNICA discordancia medida nas cinco entradas: a do RumblePad2 traz
//     `BUTTON:9:0x0106c403` (`Back`) onde as outras quatro trazem `0x0106c402`
//     (`Start`). Quatro contra uma, e o `BUTTON:9` e o `Start` no `enum` do
//     sample: fica `0x0106c402` -- e a discordancia esta escrita aqui em vez de
//     escondida (a copia do SDK, `platform/simulation/hid_devices.cfg`, traz
//     `0x0106c402` nessa linha: o `0x403` e erro de transcricao de uma entrada).
//
//  2. OS QUATRO DO D-PAD (12..15), que o arquivo do console NAO lista ("Note that
//     the DPAD buttons don't need to be included in this file, as they are always
//     mapped to the expected values" -- exemplo do mesmo arquivo, no SDK). Estes
//     quatro vem das TABELAS DE SALTO DOS JOGOS: `Rolimaz.mod` 0x133d30..0x133d98
//     e `a3d.mod` 0x10422c..0x104288 (os dois com o mesmo par UID->indice), mais o
//     `enum eGamePadButtons` do sample do SDK (`GamepadMgr.h`: GAMEPAD_DPAD_UP=12,
//     LEFT=13, DOWN=14, RIGHT=15).
//
// NENHUMA ENTRADA FICA SEM FONTE -- kQuantosBotoesSemMedicao e zero, e o teste
// `Hid.OsTresIndicesQueFaltavam...` (em `tests/hid_test.cpp`) le a tabela do
// arquivo do console contra o codigo, para o numero nao depender de memoria.
const BotaoDoZeebo kBotoesDoZeebo[kQuantosBotoes] = {
    {0x0106c40au, "Button_1", 0, true},             // BUTTON:0   das cinco entradas
    {0x0106c40bu, "Button_2", 1, true},             // BUTTON:1
    {0x0106c40cu, "Button_3", 2, true},             // BUTTON:2
    {0x0106c40du, "Button_4", 3, true},             // BUTTON:3
    {0x0106c406u, "Left_Shoulder_Upper", 4, true},  // BUTTON:4
    {0x0106c408u, "Right_Shoulder_Upper", 5, true}, // BUTTON:5
    {0x0106c407u, "Left_Shoulder_Lower", 6, true},  // BUTTON:6
    {0x0106c409u, "Right_Shoulder_Lower", 7, true}, // BUTTON:7
    {0x0106c403u, "Back", 8, true},                 // BUTTON:8
    {0x0106c402u, "Start", 9, true},                // BUTTON:9 (ver a discordancia)
    {0x0106c404u, "Left_Thumbstick", 10, true},     // BUTTON:10
    {0x0106c405u, "Right_Thumbstick", 11, true},    // BUTTON:11
    {0x0106c3feu, "DPad_Up", 12, true},             // tabela de saltos do Rolimaz/a3d
    {0x0106c3ffu, "DPad_Left", 13, true},
    {0x0106c400u, "DPad_Down", 14, true},
    {0x0106c401u, "DPad_Right", 15, true},
};

// --- a tabela dos eixos -----------------------------------------------------
//
// A palavra vem do nome que o console da ao UID no seu proprio arquivo
// (hid_devices.cfg: AXIS:X:0x0106c4d0, AXIS:Y:0x0106c4d1, AXIS:Z:0x0106c4ce,
// AXIS:RZ:0x0106c4cf) cruzado com a ordem dos campos do `AEEHIDPositionInfo`
// (nX, nY, nZ, nRx, nRy, nRz, ... = palavras 1..6).
const EixoDoZeebo kEixosDoZeebo[kQuantosEixos] = {
    {0x0106c4d0u, "LeftThumb_X", 1},   // AXIS:X
    {0x0106c4d1u, "LeftThumb_Y", 2},   // AXIS:Y
    {0x0106c4ceu, "RightThumb_X", 3},  // AXIS:Z
    {0x0106c4cfu, "RightThumb_Y", 6},  // AXIS:RZ
};

// --- a tabela dos TIPOS DE DISPOSITIVO (`nDeviceType`) ----------------------
//
// O numero que o jogo passa no r1 do `GetConnectedDevices` e um UID de CLASSE de
// dispositivo. A lista que a documentacao do proprio metodo aceita tem TRES
// valores, e estes tres (todos em
// `BrewMPSDK-7.12.5/SDKPro/1.0.4.601 Pro/platform/hardware/inc/`):
//
//   AEEHIDDevice_Mouse.h:21     AEEUID_HID_Mouse_Device        0x0106c3fb
//   AEEHIDDevice_Keyboard.h:21  AEEUID_HID_Keyboard_Device     0x0106c3fc
//   AEEHIDDevice_Joystick.h:21  AEEUID_HID_Joystick_Device     0x0106c3fd
//
// ("The user can filter the results based on the nDevice type. Following values
//  can be used: Joystick - AEEUID_HID_Joystick_Device, Keyboard -
//  AEEUID_HID_Keyboard_Device, Mouse - AEEUID_HID_Mouse_Device" -- comentario do
//  `IHID_GetConnectedDevices` em `AEEIHID.h`, e a mesma pagina da documentacao.)
//
// O QUE FICA DE FORA, e por que: `AEEUID_HID_Unknown_DeviceType` (0x106c3fa,
// `AEEIHIDDevice.h:34`) NAO esta na tabela. Ele nao e uma classe para enumerar --
// e o marcador que o `GetDeviceInfo` devolve quando nao identifica o aparelho -- e
// nao aparece na lista que o metodo aceita. Um pedido com ele e um tipo nao
// suportado, e a resposta do contrato e `AEE_EBADPARM`.
//
// E O ZERO, que nao e um tipo: e "TODOS". Esta escrito na documentacao do proprio
// metodo (`AEEIHID.h` nesta arvore, e
// `.../documentation/API Reference/Hardware/HID/methods/IHID_GetConnectedDevices.htm`):
//   "The nDeviceType parameter should be set to either a UID for the type of
//    device that the user is interested in or 0 to return all attached devices."
// e a mesma pagina da o codigo da recusa:
//   "AEE_EBADPARM : if an unsupported device type is specified."
// (Nulla: o `zeemu` C++, que e o outro emulador desta familia, ja respondia
//  assim -- `r1 == 0` inclui joystick E teclado, e um tipo fora da lista devolve
//  `AEE_EBADPARM`; `research/sources/zeemu/brew/BrewHID.cpp:298-330`.)
//
// Os DOIS primeiros tipos existem e nao tem dispositivo NENHUM aqui (nem ha
// caminho de teclado ou de rato neste emulador); o terceiro e o unico que responde
// com um handle. Responder zero para um tipo declarado e a resposta verdadeira --
// e o que nao pode acontecer e responder em silencio.
const TipoDeDispositivo kTiposDeDispositivo[kQuantosTipos] = {
    {0x0106c3fbu, "Mouse_Device", false},           // AEEHIDDevice_Mouse.h:21
    {0x0106c3fcu, "Keyboard_Device", false},        // AEEHIDDevice_Keyboard.h:21
    {kUidJoystickDevice, "Joystick_Device", true},  // AEEHIDDevice_Joystick.h:21
};

namespace {

// O tipo pelo UID, ou nulo quando o SDK nao declara esse numero. A procura e
// pela TABELA (`kTiposDeDispositivo`, no cabecalho) -- e nao uma cadeia de `if`
// com os tres numeros escritos a mao: um segundo sitio com o mesmo numero e um
// sitio que diverge.
const TipoDeDispositivo* TipoPorUid(std::uint32_t uid) {
  for (std::uint32_t k = 0; k < kQuantosTipos; ++k) {
    if (kTiposDeDispositivo[k].uid == uid) return &kTiposDeDispositivo[k];
  }
  return nullptr;
}

}  // namespace

Ihid::Ihid(Memoria& mem, Traco& traco, Sinais& sinais, EntradaDoZeebo& entrada)
    : mem_(mem), traco_(traco), sinais_(sinais), entrada_(entrada) {
  for (std::uint32_t k = 0; k < kQuantosEixos; ++k) ultimo_eixo_[k] = kEixoCentro;
}

std::uint32_t Ihid::Recusar(ICpu& cpu, const char* o_que, const char* porque) {
  // A RECUSA E REGISTADA (P2). Este e o caminho que a arvore antiga NAO tinha:
  // la, o default de cada um dos 40 slots do `IHIDDevice` era `SUCCESS` mudo, e
  // foi por um desses que 86 377 chamadas de `glCullFace` desapareceram noutro
  // sitio do mesmo projeto.
  traco_.RegistarFalta(Area::Entrada, o_que, porque);
  cpu.Set(kR0, kAeeBadParm);
  return kAeeBadParm;
}

bool Ihid::Construir(const Saidas& saidas, std::uint32_t base_das_saidas) {
  base_ = base_das_saidas;
  if (!saidas.ativa) {
    traco_.RegistarFalta(Area::Entrada, "Ihid::Construir", "a faixa de saida nao esta activa");
    return false;
  }
  if (base_ + kSlotsNecessarios > saidas.quantos) {
    traco_.RegistarFalta(Area::Entrada, "Ihid::Construir",
                         "a faixa de saida so tem " + std::to_string(saidas.quantos) +
                             " indices; este modulo precisa ate ao " +
                             std::to_string(base_ + kSlotsNecessarios));
    return false;
  }
  ConstruirObjeto(mem_, saidas, kObjIhid, kVtableIhid, kSlotsPorVtable, base_ + kBaseDoIhid);
  ConstruirObjeto(mem_, saidas, kObjIhidDevice, kVtableIhidDevice, kSlotsPorVtable,
                  base_ + kBaseDoDispositivo);

  // A LEITURA DE VOLTA da cablagem, slot a slot. Foi a falta dela que deixou o
  // `SetTimer` escrito e a vtable a apontar para o stub que recusa -- quatro
  // minutos de corrida por uma linha perdida numa edicao.
  struct Checagem {
    std::uint32_t vtable;
    std::uint32_t base;
    std::uint32_t primeiro;
    std::uint32_t ultimo;
  };
  const Checagem checagens[2] = {
      {kVtableIhid, base_ + kBaseDoIhid, kIHID_CreateDevice, kIHID_GetConnectedDevices},
      {kVtableIhidDevice, base_ + kBaseDoDispositivo, brew_slots::kHIDDevice_GetDeviceInfo,
       brew_slots::kHIDDevice_GetRumbleStatus},
  };
  for (const auto& c : checagens) {
    for (std::uint32_t slot = c.primeiro; slot <= c.ultimo; ++slot) {
      const std::uint32_t esperado = saidas.Endereco(c.base + slot);
      const std::uint32_t lido = mem_.Ler32(c.vtable + slot * 4);
      if (lido != esperado) {
        traco_.RegistarFalta(Area::Entrada, "Ihid::Construir",
                             "cablagem perdida: vtable 0x" + Hex(c.vtable) + " slot " +
                                 std::to_string(slot));
        return false;
      }
    }
  }

  construido_ = true;
  conectados_ = 1;
  // OS VALORES QUE ESTE EMULADOR NAO MEDIU FICAM DITOS NUM TRACO. Um instrumento
  // so entra se puder ser verdadeiro (P7): estes numeros sao DECLARADOS, e quem
  // ler a corrida tem de o poder ver sem ler o codigo.
  // O QUE FICA AQUI E A INSTALACAO, e nao um valor entregue a um titulo: e por
  // isso que continua a ser um `Emitir`. O PRESSUPOSTO conta-se onde o valor sai
  // para o guest (`Ihid::GetDeviceInfo`), senao o numero mediria quantas vezes o
  // emulador arrancou -- e nao quantos titulos foram informados.
  traco_.Emitir(Area::Entrada, Nivel::Informacao, "HID_DECLARADO",
                "wProductID=0x0135 wVendorID=0x1eaa (MEDIDOS por terceiros: descritor USB do "
                "console + hid_devices.original.cfg, via zeebx) | 1 aparelho DECLARADO (o "
                "console tinha 3) | eixos: 4, centro=128 MEDIDO (funsoccer 0x1ed504), "
                "min=0/max=255 DECLARADOS (nenhuma fonte os da) | botoes: 16 canais, indice com "
                "medicao: " + std::to_string(kQuantosBotoes - kQuantosBotoesSemMedicao) + "/" +
                    std::to_string(kQuantosBotoes));
  return true;
}

bool Ihid::Atender(ICpu& cpu, std::uint32_t indice) {
  if (!construido_) return false;
  if (indice >= base_ + kBaseDoIhid && indice < base_ + kBaseDoIhid + 8) {
    return AtenderIhid(cpu, indice - (base_ + kBaseDoIhid));
  }
  if (indice >= base_ + kBaseDoDispositivo &&
      indice < base_ + kBaseDoDispositivo + brew_slots::kHIDDevice_GetRumbleStatus + 1) {
    return AtenderDispositivo(cpu, indice - (base_ + kBaseDoDispositivo));
  }
  return false;
}

bool Ihid::AtenderIhid(ICpu& cpu, std::uint32_t slot) {
  if (cpu.Get(kR0) != kObjIhid) {
    Recusar(cpu, "IHID::slot", "po nao e o objecto IHID deste emulador");
    return true;
  }
  switch (slot) {
    case kIHID_CreateDevice: {
      // `AEEResult CreateDevice(IHID*, int nDevHandle, IHIDDevice **ppiHidDevice)`
      const std::uint32_t handle = cpu.Get(kR1);
      const std::uint32_t ppi = cpu.Get(kR2);
      if (handle != kHandleDoDispositivo) {
        // RECUSA em vez de devolver um dispositivo que nao existe: quem pede um
        // handle que nunca foi reportado esta a usar um numero inventado.
        cpu.Set(kR0, kAeeNoSuch);
        traco_.RegistarFalta(Area::Entrada, "IHID::CreateDevice",
                             "handle " + std::to_string(handle) + " desconhecido");
        return true;
      }
      if (ppi == 0) {
        Recusar(cpu, "IHID::CreateDevice", "ppiHidDevice nulo");
        return true;
      }
      mem_.Escrever32(ppi, kObjIhidDevice);
      cpu.Set(kR0, kAeeSuccess);
      return true;
    }
    case kIHID_GetDeviceInfo: {
      // `AEEResult GetDeviceInfo(IHID*, int nDevHandle, AEEHIDDeviceInfo*)`
      const std::uint32_t handle = cpu.Get(kR1);
      if (handle != kHandleDoDispositivo) {
        cpu.Set(kR0, kAeeNoSuch);
        traco_.RegistarFalta(Area::Entrada, "IHID::GetDeviceInfo",
                             "handle " + std::to_string(handle) + " desconhecido");
        return true;
      }
      // O mesmo preenchimento do `IHIDDevice_GetDeviceInfo`, e a mesma struct --
      // por isso passa pelo mesmo codigo, com o r2 no lugar do r1.
      const std::uint32_t antes = cpu.Get(kR1);
      cpu.Set(kR1, cpu.Get(kR2));
      GetDeviceInfo(cpu);
      cpu.Set(kR1, antes);
      return true;
    }
    case kIHID_GetNextConnectEvent: {
      // `AEEResult GetNextConnectEvent(IHID*, int *pnDevHandle, int *pnStatus,
      //                                boolean *pbDroppedEvents)`
      //
      // AEE_ENOMORE: NAO HA EVENTO DE CONEXAO PENDENTE.
      //
      // O dispositivo esta ligado desde o inicio, logo nunca houve mudanca de
      // estado para reportar -- e o `AEE_ENOMORE` (47, "no more items
      // available") e a resposta verdadeira. A arvore antiga respondeu `0` aqui
      // primeiro e mediu o resultado: o sample le isto num laco `while(SUCCESS ==
      // ...)`, e o `abd` girou 20 mil milhoes de passos sem sair. **Um "sim"
      // generico nao e uma resposta neutra: e um laco infinito num sitio onde o
      // contrato pede "acabou".**
      cpu.Set(kR0, kAeeNoMore);
      return true;
    }
    case kIHID_RegisterForConnectEvents: {
      const std::uint32_t sinal = cpu.Get(kR1);
      if (!sinais_.Conhece(sinal)) {
        Recusar(cpu, "IHID::RegisterForConnectEvents", "o ISignal nao e um sinal nosso");
        return true;
      }
      sinal_de_conexao_ = sinal;
      // Registado, e NAO marcado: nao houve mudanca de estado para avisar. Marcar
      // aqui seria inventar um evento de conexao que nao aconteceu.
      cpu.Set(kR0, kAeeSuccess);
      return true;
    }
    case kIHID_GetConnectedDevices: {
      // `AEEResult GetConnectedDevices(IHID*, int nDeviceType, int *pnDevHandles,
      //                               int pnDevHandlesLen, int *pnDevHandlesLenReq)`
      const std::uint32_t tipo = cpu.Get(kR1);
      const std::uint32_t handles = cpu.Get(kR2);
      const std::uint32_t quantos_cabem = cpu.Get(kR3);
      const std::uint32_t preq = mem_.Ler32(cpu.Get(kSP));
      // OS TRES CASOS DO CONTRATO, e nenhum deles fica mudo:
      //
      //   `0`                    -> TODOS os dispositivos ligados. NAO e "nenhum":
      //                             era o achado B2 da auditoria
      //                             (`docs/rewrite/auditoria/aud-media-hid-widget.md:58`),
      //                             que respondia zero dispositivos com sucesso;
      //   UID que o SDK declara  -> os desse tipo (zero, quando nao ha nenhum);
      //   qualquer outro numero  -> `AEE_EBADPARM`, com o numero no NOME da falta.
      //
      // A pergunta do tipo vai a TABELA, e nao a uma cadeia de `if` com os numeros
      // escritos a mao: um segundo sitio com o mesmo numero e um sitio que diverge.
      const TipoDeDispositivo* t = TipoPorUid(tipo != 0 ? tipo : kUidJoystickDevice);
      if (t == nullptr) {
        if (preq != 0) mem_.Escrever32(preq, 0);
        // O NOME DA FALTA TEM O NUMERO. "tipo desconhecido" sem o numero obriga a
        // ir ao desmonte para saber QUAL pedido ficou por responder.
        traco_.RegistarFalta(Area::Entrada, "IHID::GetConnectedDevices tipo " + Hex(tipo),
                             "a lista que o metodo aceita e 0x0106c3fb Mouse, 0x0106c3fc "
                             "Keyboard e 0x0106c3fd Joystick (e 0 = todos); AEE_EBADPARM e o "
                             "codigo do contrato para um tipo nao suportado");
        cpu.Set(kR0, kAeeBadParm);
        return true;
      }
      if (!t->existe) {
        // TIPO DECLARADO, E SEM DISPOSITIVO NENHUM AQUI. "nao ha teclado nenhum
        // ligado" e a resposta VERDADEIRA (o console responde o mesmo com a porta
        // vazia), e o que nao se pode e responde-la em SILENCIO: o zero e um valor
        // que este emulador NAO mediu -- nao ha caminho de teclado nenhum aqui --
        // logo vai como PRESSUPOSTO, que e a metade que a bateria publica
        // (`Traco::RegistarPressuposto`). A auditoria mediu o custo do contrario:
        // `ausencia de falta != ausencia de chamada` (`aud-media-hid-widget.md`,
        // secao e), e a constante do teclado aparece no codigo de 11 dos 62
        // titulos (varredura de constantes de 32 bits, `:444`).
        if (preq != 0) mem_.Escrever32(preq, 0);
        traco_.RegistarPressuposto(Area::Entrada,
                                   "IHID::GetConnectedDevices sem " + std::string(t->nome),
                                   "o tipo 0x" + Hex(t->uid) +
                                       " e DECLARADO no SDK e este emulador nao tem nenhum "
                                       "dispositivo dele: responde zero");
        cpu.Set(kR0, kAeeSuccess);
        return true;
      }
      if (preq != 0) mem_.Escrever32(preq, 1);
      if (handles != 0 && quantos_cabem >= 1) mem_.Escrever32(handles, kHandleDoDispositivo);
      conectados_ = 1;
      cpu.Set(kR0, kAeeSuccess);
      traco_.Emitir(Area::Entrada, Nivel::Depuracao, "HID_DISPOSITIVOS",
                    (tipo == 0 ? std::string("todos (0) ") + t->nome : std::string(t->nome)) +
                        " handle=" + std::to_string(kHandleDoDispositivo) +
                        " cabem=" + std::to_string(quantos_cabem));
      return true;
    }
    default:
      Recusar(cpu, "IHID::slot", "slot sem implementacao");
      return true;
  }
}

bool Ihid::AtenderDispositivo(ICpu& cpu, std::uint32_t slot) {
  if (cpu.Get(kR0) != kObjIhidDevice) {
    Recusar(cpu, "IHIDDevice::slot", "po nao e o objecto IHIDDevice deste emulador");
    return true;
  }
  switch (slot) {
    case 2: QueryInterfaceIhidDevice(cpu); return true;
    case brew_slots::kHIDDevice_GetDeviceInfo: GetDeviceInfo(cpu); return true;
    case brew_slots::kHIDDevice_GetDeviceStatus: GetDeviceStatus(cpu); return true;
    case brew_slots::kHIDDevice_RegisterForStatusChange: RegisterForStatusChange(cpu); return true;
    case brew_slots::kHIDDevice_GetButtonInfo: GetButtonInfo(cpu); return true;
    case brew_slots::kHIDDevice_GetNumberOfButtons: GetNumberOfButtons(cpu); return true;
    case brew_slots::kHIDDevice_RegisterForButtonEvent: RegisterForButtonEvent(cpu); return true;
    case brew_slots::kHIDDevice_GetNextButtonEvent: GetNextButtonEvent(cpu); return true;
    case brew_slots::kHIDDevice_GetPositionState:
      EscreverPosicao(cpu, ModoDaPosicao::Estado);
      return true;
    case brew_slots::kHIDDevice_GetMinPositionInfo:
      EscreverPosicao(cpu, ModoDaPosicao::Minimo);
      return true;
    case brew_slots::kHIDDevice_GetMaxPositionInfo:
      EscreverPosicao(cpu, ModoDaPosicao::Maximo);
      return true;
    case brew_slots::kHIDDevice_GetAxesInfo:
      EscreverPosicao(cpu, ModoDaPosicao::Uids);
      return true;
    case brew_slots::kHIDDevice_RegisterForPositionChange:
      RegisterForPositionChange(cpu);
      return true;
    case brew_slots::kHIDDevice_SetExclusiveLevel: SetExclusiveLevel(cpu); return true;
    case brew_slots::kHIDDevice_GetExclusiveLevel: GetExclusiveLevel(cpu); return true;
    case brew_slots::kHIDDevice_Rumble: Rumble(cpu); return true;
    case brew_slots::kHIDDevice_GetRumbleStatus: GetRumbleStatus(cpu); return true;
    default:
      // QUALQUER SLOT NAO LISTADO RECUSA EM VOZ ALTA. Era aqui que a arvore
      // antiga devolvia `AEE_SUCCESS` mudo em 40 slots.
      Recusar(cpu, "IHIDDevice::slot", "slot sem implementacao");
      return true;
  }
}

void Ihid::QueryInterfaceIhidDevice(ICpu& cpu) {
  const std::uint32_t iid = cpu.Get(kR1);
  const std::uint32_t ppo = cpu.Get(kR2);
  if (ppo == 0) {
    Recusar(cpu, "IHIDDevice::QueryInterface", "ppo nulo");
    return;
  }
  if (iid == kIidIHIDDevice) {
    mem_.Escrever32(ppo, kObjIhidDevice);
    cpu.Set(kR0, kAeeSuccess);
    return;
  }
  // ECLASSNOTSUPPORT com o ponteiro a zero: recusar, e nao mentir com um objecto
  // que so tem slots a recusar.
  mem_.Escrever32(ppo, 0);
  cpu.Set(kR0, kAeeClassNotSupported);
}

void Ihid::GetDeviceInfo(ICpu& cpu) {
  // `AEEHIDDeviceInfo` (AEEIHIDDevice.h):
  //   +0 int nDeviceType; +4 uint16 wProductID; +6 uint16 wVendorID;
  //   +8 boolean bBluetoothDevice
  const std::uint32_t pi = cpu.Get(kR1);
  if (pi == 0) {
    Recusar(cpu, "IHIDDevice::GetDeviceInfo", "pDevInfo nulo");
    return;
  }
  mem_.Escrever32(pi + 0, 0x0106c3fdu);  // AEEUID_HID_Joystick_Device (linha 21 do cabecalho)
  // O PAR VID/PID ESTAVA MARCADO "DECLARADOS, sem medicao" E ESTA MEDIDO.
  //
  // Esta correccao e ao contrario das outras desta ronda: nao tiramos uma
  // afirmacao forte demais, damos credito a um numero que o estava a perder.
  // `0x1eaa/0x0135` vem do CONSOLE, por duas vias independentes:
  //   1. o descritor USB capturado de um Zeebo, "My Power / Usb Game Pad"
  //      (`docs/vendor/tripleoxygen/hardware/peripheral/joystick_descriptor.txt`,
  //      citado em zeebx `src/machine/mod.rs:338-342`);
  //   2. a entrada `VID:0x1EAA:PID:0x0135` do `hid_devices.original.cfg` do
  //      proprio console (zeebx `docs/implementacao/09-entrada.md:76`,
  //      `docs/07-inventario-vendor.md:112`), onde o aparelho aparece como
  //      "New Zeebo Game Controller" -- o Dragon
  //      (`docs/implementacao/20-boomerang-e-wii-remote.md:19,23`).
  //
  // O LIMITE DESTA AFIRMACAO, e fica escrito: **nao lemos o ficheiro original**,
  // que nao esta nesta arvore. O `hid_devices.cfg` que ESTA
  // (`research/sources/zeemu/rootfs/sys/hid_devices.cfg`) e outro ficheiro e
  // NAO tem este par -- so comandos de PC. Logo: MEDIDO no console, por
  // terceiros, com a fonte nomeada; nao medido por nos.
  mem_.Escrever16(pi + 4, 0x0135);
  mem_.Escrever16(pi + 6, 0x1eaa);
  mem_.Escrever8(pi + 8, 0);  // com fio: `bBluetoothDevice` = falso
  // CONTADO onde o valor SAI para o titulo: assim a corrida diz quantos titulos
  // foram informados destes numeros, e nao quantas vezes o modulo arrancou.
  //
  // O que aqui e PRESSUPOSTO ja nao e o par VID/PID: e o aparelho UNICO. O
  // console tinha pelo menos tres (Dragon `1eaa:0135`, Z-Pad `1a5c:3033` e o
  // receptor Boomerang `15a2:0003` -- zeebx `src/machine/mod.rs:338-352`), e
  // este emulador anuncia so o primeiro.
  traco_.RegistarPressuposto(Area::Entrada, "IHIDDevice::GetDeviceInfo",
                             "nDeviceType=0x0106c3fd wProductID=0x0135 wVendorID=0x1eaa "
                             "MEDIDOS no console (descritor USB + hid_devices.original.cfg, "
                             "via zeebx); DECLARADO e o aparelho UNICO -- o console tinha 3");
  cpu.Set(kR0, kAeeSuccess);
}

void Ihid::GetDeviceStatus(ICpu& cpu) {
  // `AEE_SUCCESS : The device is attached and operational` -- e o contrato do
  // `AEEIHIDDevice.h` para o VALOR do estado.
  const std::uint32_t pn = cpu.Get(kR1);
  if (pn == 0) {
    Recusar(cpu, "IHIDDevice::GetDeviceStatus", "pnStatus nulo");
    return;
  }
  mem_.Escrever32(pn, kAeeSuccess);
  cpu.Set(kR0, kAeeSuccess);
}

void Ihid::RegisterForStatusChange(ICpu& cpu) {
  const std::uint32_t sinal = cpu.Get(kR1);
  if (!sinais_.Conhece(sinal)) {
    Recusar(cpu, "IHIDDevice::RegisterForStatusChange", "o ISignal nao e um sinal nosso");
    return;
  }
  sinal_de_estado_ = sinal;
  // REGISTADO, e o dispositivo NUNCA MUDA DE ESTADO neste emulador: um unico
  // joystick, sempre ligado. O sinal fica registado e nunca e marcado, e isso
  // esta dito aqui -- porque um "registrado e nunca avisado" silencioso e
  // exatamente a forma do defeito que esta etapa existe para corrigir. A
  // diferenca e que o estado que ele espia nao muda mesmo.
  cpu.Set(kR0, kAeeSuccess);
}

int Ihid::IndiceDoBotao(std::uint32_t argumento) const {
  // O jogo pode pedir o botao pelo UID (o que os jogos embutem, medido) ou pelo
  // INDICE (`nButtonID`, que e o que o `AEEHIDButtonInfo` devolve e o que o
  // sample usa para indexar a tabela dele). Aceitam-se os dois, e a ambiguidade
  // fica dita: o cabecalho nao diz qual dos dois o argumento e --
  // "Button the user is interested in", nenhuma palavra sobre UID.
  for (std::uint32_t k = 0; k < kQuantosBotoes; ++k) {
    if (kBotoesDoZeebo[k].uid == argumento) return static_cast<int>(k);
    if (kBotoesDoZeebo[k].id == argumento) return static_cast<int>(k);
  }
  return -1;
}

void Ihid::GetButtonInfo(ICpu& cpu) {
  const std::uint32_t argumento = cpu.Get(kR1);
  const std::uint32_t pi = cpu.Get(kR2);
  const int k = IndiceDoBotao(argumento);
  if (k < 0) {
    // "AEE_ENOSUCH : if the specified button is not supported."
    cpu.Set(kR0, kAeeNoSuch);
    traco_.RegistarFalta(Area::Entrada, "IHIDDevice::GetButtonInfo",
                         "botao 0x" + Hex(argumento) + " desconhecido");
    return;
  }
  if (pi == 0) {
    Recusar(cpu, "IHIDDevice::GetButtonInfo", "pnButtonInfo nulo");
    return;
  }
  // `AEEHIDButtonInfo`: nButtonID, nState, nButtonUID, nButtonMin, nButtonMax.
  // `nButtonMin` 0 e `nButtonMax` 1 sao o que o cabecalho manda para um botao
  // digital simples.
  mem_.Escrever32(pi + 0, kBotoesDoZeebo[k].id);
  mem_.Escrever32(pi + 4, estado_botao_[k]);
  mem_.Escrever32(pi + 8, kBotoesDoZeebo[k].uid);
  mem_.Escrever32(pi + 12, 0);
  mem_.Escrever32(pi + 16, 1);
  cpu.Set(kR0, kAeeSuccess);
}

void Ihid::GetNumberOfButtons(ICpu& cpu) {
  const std::uint32_t pn = cpu.Get(kR1);
  if (pn == 0) {
    Recusar(cpu, "IHIDDevice::GetNumberOfButtons", "pnButtons nulo");
    return;
  }
  // DEZASSEIS, e nao catorze.
  //
  // O numero nao e uma escolha: e o tamanho da lista de UIDs de botao do
  // `AEEHIDDevice_Joystick.h` (0x0106c3fe..0x0106c40d, dezasseis entradas) e o
  // `NUM_OF_GAMEPAD_BUTTONS` do sample do SDK, que e 16
  // (`GAMEPAD_BUTTON_1..12` + os quatro do d-pad). Os "14" que o despacho partilhado
  // responde hoje vem de contar as entradas de um `hid_devices.cfg` -- que e o
  // mapeamento de um controle de PC DENTRO do simulador, e nao a lista de botoes
  // do aparelho.
  mem_.Escrever32(pn, kQuantosBotoes);
  cpu.Set(kR0, kAeeSuccess);
}

void Ihid::RegisterForButtonEvent(ICpu& cpu) {
  const std::uint32_t sinal = cpu.Get(kR1);
  if (!sinais_.Conhece(sinal)) {
    Recusar(cpu, "IHIDDevice::RegisterForButtonEvent", "o ISignal nao e um sinal nosso");
    return;
  }
  sinal_de_botao_ = sinal;
  cpu.Set(kR0, kAeeSuccess);
  traco_.Emitir(Area::Entrada, Nivel::Depuracao, "REGISTA_BOTAO", "sinal=0x" + Hex(sinal));
}

void Ihid::RegisterForPositionChange(ICpu& cpu) {
  const std::uint32_t sinal = cpu.Get(kR1);
  if (!sinais_.Conhece(sinal)) {
    Recusar(cpu, "IHIDDevice::RegisterForPositionChange", "o ISignal nao e um sinal nosso");
    return;
  }
  sinal_de_posicao_ = sinal;
  cpu.Set(kR0, kAeeSuccess);
  // O SLOT QUE A Z-WHEEL CHAMA UMA VEZ (medido com `ZEEB_LOG_HID_SLOT=1` na
  // arvore antiga: a unica linha que saiu foi `slot 14 caiu no default (sucesso
  // mudo)`). Aqui ele guarda o sinal de verdade, e o `Bombear` marca-o quando um
  // eixo muda.
  traco_.Emitir(Area::Entrada, Nivel::Depuracao, "REGISTA_POSICAO",
                "slot 14, sinal=0x" + Hex(sinal));
}

void Ihid::SetExclusiveLevel(ICpu& cpu) {
  nivel_exclusivo_ = static_cast<std::int32_t>(cpu.Get(kR1));
  cpu.Set(kR0, kAeeSuccess);
  // O CONTRATO (AEEIHIDDevice.h, SetExclusiveLevel): "Only the instances with the
  // highest level set on the device will be notified about events." Aqui ha UMA
  // instancia, logo guardar o nivel e suficiente para o contrato ser verdade. Se
  // um dia houver duas, isto tem de ser revisto -- e este e o comentario que o
  // diz.
  traco_.Emitir(Area::Entrada, Nivel::Depuracao, "NIVEL_EXCLUSIVO",
                std::to_string(nivel_exclusivo_));
}

void Ihid::GetExclusiveLevel(ICpu& cpu) {
  const std::uint32_t pn = cpu.Get(kR1);
  if (pn == 0) {
    Recusar(cpu, "IHIDDevice::GetExclusiveLevel", "pnLevel nulo");
    return;
  }
  mem_.Escrever32(pn, static_cast<std::uint32_t>(nivel_exclusivo_));
  cpu.Set(kR0, kAeeSuccess);
}

void Ihid::Rumble(ICpu& cpu) {
  // AEE_EUNSUPPORTED, e nao sucesso. O aparelho nao tem motor: o proprio
  // cabecalho do SDK preve este caso e da-lhe um codigo proprio
  // ("AEE_EUNSUPPORTED : if the device does not support rumble",
  // AEEIHIDDevice.h, IHIDDevice_Rumble). Devolver sucesso seria mentir sobre o
  // aparelho -- e a mentira so apareceria muito mais tarde, dentro do jogo.
  cpu.Set(kR0, kAeeUnsupported);
  traco_.RegistarFalta(Area::Entrada, "IHIDDevice::Rumble",
                       "sem motor de vibracao: AEE_EUNSUPPORTED (o codigo que o cabecalho define)");
}

void Ihid::GetRumbleStatus(ICpu& cpu) {
  cpu.Set(kR0, kAeeUnsupported);
  traco_.RegistarFalta(Area::Entrada, "IHIDDevice::GetRumbleStatus",
                       "sem motor de vibracao: AEE_EUNSUPPORTED");
}

std::int32_t Ihid::ValorDoEixo(std::uint32_t uid) const {
  bool nao_conhecido = false;
  const std::int32_t v = entrada_.ValorAgora(uid, &nao_conhecido);
  if (nao_conhecido) return kEixoCentro;  // o repouso MEDIDO
  return v;
}

void Ihid::EscreverPosicao(ICpu& cpu, ModoDaPosicao modo) {
  const std::uint32_t pi = cpu.Get(kR1);
  if (pi == 0) {
    Recusar(cpu, "IHIDDevice::PositionInfo", "ponteiro nulo");
    return;
  }
  // Todas as 25 palavras sao escritas, e nao so as quatro mapeadas: uma palavra
  // deixada com o valor anterior e um valor que nao veio de sitio nenhum.
  for (std::uint32_t k = 0; k < kPalavrasDaPosicao; ++k) {
    const auto* eixo = static_cast<const EixoDoZeebo*>(nullptr);
    for (std::uint32_t i = 0; i < kQuantosEixos; ++i) {
      if (kEixosDoZeebo[i].palavra == k) eixo = &kEixosDoZeebo[i];
    }
    std::uint32_t valor = 0;
    switch (modo) {
      case ModoDaPosicao::Estado:
        // A palavra 0 e o `bRelativeAxes`, e NAO um eixo. Os eixos do Zeebo sao
        // ABSOLUTOS (um manche com repouso num valor, e nao um deslocamento) --
        // e e isso que o centro 128 significa. Fica DECLARADO: o campo esta no
        // cabecalho e alguem tem de o preencher.
        valor = (k == 0) ? 0u : (eixo != nullptr ? static_cast<std::uint32_t>(ValorDoEixo(eixo->uid)) : 0u);
        break;
      case ModoDaPosicao::Minimo:
        // O cabecalho: "If the minimum and maximum values are both zero that
        // indicates that the axis is not supported." Logo o nao suportado e
        // min=0/max=0, e um eixo suportado comeca em 0.
        valor = 0;
        break;
      case ModoDaPosicao::Maximo:
        valor = (k != 0 && eixo != nullptr) ? static_cast<std::uint32_t>(kEixoMax) : 0u;
        break;
      case ModoDaPosicao::Uids:
        // "If the UID is 0 then the system has no specific information on what
        // the axis represents. If the UID is -1 that indicates that the axis is
        // not supported."
        valor = (k == 0) ? 0u : (eixo != nullptr ? eixo->uid : kUidDesconhecido);
        break;
    }
    mem_.Escrever32(pi + k * 4, valor);
  }
  cpu.Set(kR0, kAeeSuccess);
}

void Ihid::GetNextButtonEvent(ICpu& cpu) {
  const std::uint32_t pi = cpu.Get(kR1);
  const std::uint32_t pts = cpu.Get(kR2);
  const std::uint32_t pdropped = cpu.Get(kR3);
  if (fila_.empty()) {
    // "AEE_ENOMORE : No more events are pending" (AEEIHIDDevice.h).
    cpu.Set(kR0, kAeeNoMore);
    return;
  }
  if (pi == 0) {
    Recusar(cpu, "IHIDDevice::GetNextButtonEvent", "pnButtonInfo nulo");
    return;
  }
  const EventoDeBotao e = fila_.front();
  fila_.pop_front();
  mem_.Escrever32(pi + 0, e.id);
  mem_.Escrever32(pi + 4, e.estado);
  mem_.Escrever32(pi + 8, e.uid);
  mem_.Escrever32(pi + 12, 0);  // nButtonMin: botao digital
  mem_.Escrever32(pi + 16, 1);  // nButtonMax
  // OS DOIS ULTIMOS PONTEIROS PODEM SER NULOS, e o sample do SDK passa-os a
  // NULO: `IHIDDevice_GetNextButtonEvent(pIHIDDevice, &bi, NULL, NULL)`
  // (GamepadMgr.c, L_JoystickButtonCB). Escrever sem olhar para o ponteiro
  // matava o jogo no callback DELE.
  if (pts != 0) mem_.Escrever32(pts, e.quando_ms);  // o relogio INJECTADO (P4)
  if (pdropped != 0) mem_.Escrever32(pdropped, 0);
  ++eventos_entregues_;
  cpu.Set(kR0, kAeeSuccess);
}


bool Ihid::Bombear(ICpu& cpu) {
  // A INJECCAO. Tudo o que entra no controle passa por aqui, e nada mais entra:
  // nao ha leitura de teclado do hospedeiro em lado nenhum deste modulo (P4).
  //
  // O CAMINHO CURTO EXISTE POR CAUSA DO CUSTO: o despacho chama isto UMA VEZ POR
  // INSTRUCAO DO GUEST, e percorrer o guiao inteiro a cada instrucao seria caro
  // sem nada mudar. O instante do relogio injectado e o que decide: sem instante
  // novo nao ha eventos novos a aplicar.
  //
  // O QUE NAO SE ENCURTA: os callbacks PENDENTES. Uma chamada com o instante
  // parado ainda tem de entregar o que ficou por entregar -- senao um callback
  // marcado ficaria preso para sempre.
  const std::uint32_t agora = entrada_.Agora();
  if (ja_bombeou_ && agora == ultimo_instante_bombeado_) {
    return sinais_.PrepararProximoCallback(cpu);
  }
  bool eixo_mudou = false;
  bool botao_mudou = false;

  for (const auto& e : entrada_.Guiao()) {
    if (e.t_ms > agora) break;  // o guiao e nao decrescente em tempo
    if (ja_bombeou_ && e.t_ms <= ultimo_instante_bombeado_) continue;
    if (e.tipo == TipoNaEntrada::Eixo) {
      eixo_mudou = true;
      traco_.Emitir(Area::Entrada, Nivel::Depuracao, "INJETA_EIXO",
                    "t=" + std::to_string(e.t_ms) + " uid=0x" + Hex(e.uid) + " valor=" +
                        std::to_string(e.valor));
      continue;
    }
    const int k = IndiceDoBotao(e.uid);
    if (k < 0) {
      // Um UID de botao que nao esta na tabela NAO entra em silencio: fica
      // registado, e o evento nao e inventado.
      traco_.RegistarFalta(Area::Entrada, "injecao de botao",
                           "uid 0x" + Hex(e.uid) + " nao esta na tabela de botoes");
      continue;
    }
    const std::uint32_t novo = static_cast<std::uint32_t>(e.valor);
    if (estado_botao_[k] == novo) continue;  // sem mudanca, sem evento de borda
    estado_botao_[k] = novo;
    botao_mudou = true;
    EventoDeBotao ev;
    ev.id = kBotoesDoZeebo[k].id;
    ev.estado = novo;
    ev.uid = kBotoesDoZeebo[k].uid;
    ev.quando_ms = e.t_ms;
    fila_.push_back(ev);
    traco_.Emitir(Area::Entrada, Nivel::Depuracao, "INJETA_BOTAO",
                  "t=" + std::to_string(e.t_ms) + " " + kBotoesDoZeebo[k].nome + " (uid=0x" +
                      Hex(ev.uid) + ") estado=" + std::to_string(novo));
  }

  // O CONTRATO DO `RegisterForPositionChange` E "quando a posicao MUDA". Marcar o
  // sinal a cada instante bombeado seria uma tempestade de callbacks que nenhum
  // jogo espera; nao marcar nunca foi o defeito medido na Z-Wheel. A comparacao e
  // com o valor de agora, lido do guiao nesta chamada.
  for (std::uint32_t k = 0; k < kQuantosEixos; ++k) {
    const std::int32_t v = ValorDoEixo(kEixosDoZeebo[k].uid);
    if (v != ultimo_eixo_[k]) {
      ultimo_eixo_[k] = v;
      eixo_mudou = true;
    }
  }

  if (eixo_mudou && sinal_de_posicao_ != 0) {
    sinais_.Marcar(sinal_de_posicao_);
    ++sinalizacoes_posicao_;
  }
  if (botao_mudou && sinal_de_botao_ != 0) {
    sinais_.Marcar(sinal_de_botao_);
    ++sinalizacoes_botao_;
  }
  ultimo_instante_bombeado_ = agora;
  ja_bombeou_ = true;

  // O CALLBACK VAI PARA O GUEST, e quem o corre e quem chama isto: o modulo
  // poe-no no PC e devolve `true`, e o laco de eventos corre o titulo a partir
  // dai -- o mesmo que o despacho ja faz com os callbacks de temporizador.
  return sinais_.PrepararProximoCallback(cpu);
}

}  // namespace zb2::brew
