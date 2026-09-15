#ifndef ZB2_CORE_BREW_INTERFACE_H
#define ZB2_CORE_BREW_INTERFACE_H

// A construcao de objectos BREW e a cablagem das vtables.
//
// VIVE NO MOTOR. Isto e o que faz um `IShell`, um `IDisplay` ou um `IFile`
// existirem na memoria do guest -- e qualquer frente precisa disso, nao so a
// ferramenta que mede.
//
// AS CONSTANTES DE SLOT VEM DE `brew_slots.inc`, GERADO DOS CABECALHOS.
// Estiveram escritas a mao e estavam TODAS erradas por um, porque `INHERIT_IBase`
// tem DOIS membros (`AddRef`, `Release`) e eu contava TRES -- a procura de um
// `QueryInterface` que nao existe em `INHERIT_IBase`.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "tools/brew_slots.inc"
#include "core/cpu/cpu.h"
#include "core/memoria/memoria.h"

namespace zb2::brew {

// --- a faixa de saida -------------------------------------------------------
//
// Um endereco unico por SLOT: e o que faz o registo de faltas dizer QUAL metodo
// cada titulo pediu, em vez de "algo do shell". Com um stub so para todos, 27
// titulos pediam "algo do shell" e o numero nao tinha nome.
constexpr std::uint32_t kBaseAjudantes = 1000;
constexpr std::uint32_t kBaseDoShell = 2000;
constexpr std::uint32_t kVtableShell = 3000;
constexpr std::uint32_t kVtableDisplay = 6000;
constexpr std::uint32_t kVtableFileMgr = 7000;
constexpr std::uint32_t kVtableBitmap = 8000;
constexpr std::uint32_t kVtableFileObj = 9500;
constexpr std::uint32_t kVtableGenericoBase = 9000;
constexpr std::uint32_t kPassoGenerico = 64;
// Quantos slots tem cada vtable construida. Uma so constante, para o limite da
// cablagem e a construcao nao poderem divergir.
constexpr std::uint32_t kSlotsPorVtable = 64;

constexpr std::uint32_t kObjShell = 0x80020000u;
constexpr std::uint32_t kObjDisplay = 0x80030000u;
constexpr std::uint32_t kObjFileMgr = 0x80040000u;
constexpr std::uint32_t kObjDibBase = 0x80050000u;
constexpr std::uint32_t kObjGenericoBase = 0x80060000u;
constexpr std::uint32_t kObjFileBase = 0x80070000u;
constexpr std::uint32_t kPassoGenericoObj = 0x100;

// As interfaces que o corpus MEDIU como pedidas, com o nome do SDK.
// A lista vem da medicao, e nao de uma leitura de cabecalho: e a ordem por
// demanda que diz o que vale implementar.
struct Generico {
  std::uint32_t iid;
  const char* nome;
};
extern const Generico kGenericos[7];
constexpr std::uint32_t kNGenericos = 7;

constexpr std::uint32_t ObjGenerico(std::uint32_t k) {
  return kObjGenericoBase + k * kPassoGenericoObj;
}
constexpr std::uint32_t VtGenerico(std::uint32_t k) {
  return kVtableGenericoBase + k * kPassoGenerico;
}

// `AEEDeviceInfo`, TRANSCRITA de `platform/system/inc/AEEIShell.h`.
//
// Os campos escrevem-se por NOME e o `sizeof` vem do compilador, em vez de
// numeros de offset escritos a mao. **Um offset escrito a mao ja divergiu uma
// vez neste trabalho** -- os slots do IDisplay -- e o custo foi uma ronda.
//
// `EmptyEnum` e `unsigned` (AEEIShell.h linha 83). No ARM AAPCS o `uint32` alinha
// a 4 e as bitfields `unsigned : 1` empacotam no mesmo `unsigned`, que e o mesmo
// que o x86-64 faz aqui.
struct AeeDeviceInfo {
  std::uint16_t cx_screen, cy_screen, cx_alt_screen, cy_alt_screen, cx_scroll_bar;
  std::uint16_t w_encoding, w_menu_text_scroll, n_color_depth;
  unsigned unused2;
  std::uint32_t w_menu_image_delay, dw_ram;
  unsigned b_alt_display : 1, b_flip : 1, b_vibrator : 1, b_ext_speaker : 1, b_vr : 1,
      b_pos_loc : 1, b_midi : 1, b_cmx : 1, b_pen : 1;
  std::uint32_t dw_prompt_props;
  std::uint16_t w_key_close_app, w_key_close_all_apps;
  std::uint32_t dw_lang;
  std::uint16_t w_struct_size;
  std::uint32_t dw_net_linger, dw_sleep_defer;
  std::uint16_t w_max_path;
  std::uint32_t dw_platform_id;
};

// O CORTE DA STRUCT: tudo o que vem a partir daqui so existe se o CHAMADOR o
// pedir, enchendo o `wStructSize` ANTES da chamada (`AEEIShell.h:116-120`).
//
// Os dois numeros sao MEDIDOS pelo compilador, e nao escritos a mao: a auditoria
// que deu por isto teve de os DEDUZIR do layout, e uma deducao de offset ja
// custou uma ronda nesta arvore. Um `static_assert` transforma a deducao em
// prova, e um cabecalho do SDK que mude parte o build em vez de partir a pilha
// do guest.
constexpr std::size_t kOffsetDeWStructSize = offsetof(AeeDeviceInfo, w_struct_size);
static_assert(kOffsetDeWStructSize == 44, "o corte da AEEDeviceInfo e o +44 do wStructSize");
static_assert(sizeof(AeeDeviceInfo) == 64, "a AEEDeviceInfo completa sao 64 bytes");
static_assert(offsetof(AeeDeviceInfo, dw_lang) == 40,
              "o dwLang e o ultimo campo antes do corte (AEEIShell.h:115)");


// --- construcao -------------------------------------------------------------

// Escreve um objecto ROPI: `[0]` = a vtable, `[4]` = a contagem de referencias, e
// um endereco de saida distinto por slot.
//
// OS SLOTS 0 E 1 SAO A IBASE, E SAO ESCRITOS UMA VEZ, explicitamente, e o laco
// comeca no 2. Antes, o laco escrevia os 64 slots e havia um par de escritas a
// seguir a corrigir os dois primeiros: funcionava, mas **dependia da ordem** --
// e a ordem e a classe de erro que apareceu OITO vezes neste trabalho.
void ConstruirObjeto(Memoria& mem, const Saidas& saidas, std::uint32_t objeto,
                     std::uint32_t vtable, std::uint32_t quantos_slots,
                     std::uint32_t base_dos_slots);

// A VTABLE DO IBitmap DO ECRA, construida como a do IDisplay e a do IFileMgr.
//
// PORQUE EXISTE: ate aqui so o slot 2 (`QueryInterface`) era cablado, e os slots
// 0 e 1 (`AddRef`/`Release` da IBase) ficavam A ZERO. Um jogo que faz
// `IBITMAP_Release(pbmp)` le `[[pbmp] + 4]`, apanha zero e faz `blx 0` -- passa a
// executar o cabecalho do proprio .mod como codigo e derrama a pilha. MEDIDO no
// `abd` (279369) em 0x1469c e no `torkandkral` (280463) em 0x135d4: era isso, e
// nao o EGL, que produzia o `dpy = 0xF0027390` que o `eglInitialize` recusava.
void ConstruirVtableDoBitmap(Memoria& mem, const Saidas& saidas);

// Uma linha da cablagem: que endereco de saida fica no slot `slot` da vtable `vt`.
struct Ligacao {
  std::uint32_t vt;
  std::uint32_t slot;
  std::uint32_t saida;
};

// O motivo pelo qual a cablagem pode ser recusada. `ok` a false significa que a
// ligacao foi REJEITADA e o objecto ficou como estava -- um abortar seria pior
// num produto, e uma recusa silenciosa pior ainda.
struct ResultadoCablagem {
  bool ok = true;
  std::string motivo;
};

// Escreve a cablagem na memoria e CONFIRMA-A com uma leitura de volta.
//
// A LEITURA DE VOLTA NAO E CERIMONIA. Uma cablagem ja se perdeu sem sintoma
// visivel: o `SetTimer` estava escrito e a funcionar, mas a entrada da vtable
// apontava para o stub que recusa, e o sintoma era a bateria dizer "falta
// SetTimer" -- o que exigiu uma corrida inteira de 4 minutos para descobrir.
ResultadoCablagem Cablar(Memoria& mem, const Saidas& saidas, const Ligacao* ligacoes,
                         std::size_t quantas);

// O nome que o SDK da ao offset na tabela de ajudantes, ou `nullptr`.
const char* NomeDoAjudante(std::uint32_t offset);

}  // namespace zb2::brew

#endif
