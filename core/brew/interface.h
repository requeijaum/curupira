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

#include "core/brew/ajudantes.h"  // Alocador + os kAee*
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

// A FRONTEIRA COM OS IDIBs DO PNG (frente imgdec).
//
// Os DIBs compativeis deste servico (slot13) sobem do FUNDO da pagina
// (`kObjDibBase + 0x340`) e os IDIBs que o descodificador PNG constroi DESCE do
// TOPO (`0x80050FC0`, `core/brew/classes.h` `kObjetosDibDoPng`, ate 28 objectos
// de 0x40 -> fundo em 0x800508C0). As duas pontas trabalham na MESMA pagina e
// sem esta fronteira cruzam-se depois de ~23 alocacoes de cada lado -- e um
// objecto partilhado por dois donos e um defeito que so aparece em producao.
constexpr std::uint32_t kFimDosDibCompativeis = kObjDibBase + 0x8C0u;
static_assert(kFimDosDibCompativeis <= 0x800508C0u,
              "a fronteira dos DIBs compativeis tem de ficar ABAIXO do fundo dos IDIBs do PNG");
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
// `IDIB`, TRANSCRITA de `platform/ui/inc/AEEIDIB.h:42-55`.
//
// O IDIB do BREW **e uma struct com membros publicos** -- o cabecalho diz-o em
// `:24-25` ("This is a struct with public members. It is also an interface...").
// O jogo le `po->pBmp`, `po->cx`, `po->nPitch` DIRECTAMENTE, sem passar por
// nenhum slot da vtable. Logo os offsets sao contrato, tal como os slots.
//
// O que ca estava era um objecto INVENTADO: `[0]` vtable, `[4]` contagem de
// referencias, `[8]` pData, `[12]` largura em u32, `[16]` altura em u32, `[20]`
// profundidade em u32. Coincidia num campo (o `pBmp`, em +8) e divergia em
// todos os outros: a largura de um DIB de 320 px lia-se em `cx` (+20) como o que
// la estivesse, e o `+4` -- que no SDK e o `pPaletteMap`, um PONTEIRO --
// levava o numero 1. `IDIB_FlushPalette` (`AEEIDIB.h:83-86`) faz
// `IQI_RELEASEIF(po->pPaletteMap)`: com 1 la dentro, isso e uma chamada
// indirecta pelo endereco 1.
struct CamposDoIdib {
  static constexpr std::uint32_t kPvt = 0;
  static constexpr std::uint32_t kPPaletteMap = 4;   // IQI* -- tem de ficar NULO
  static constexpr std::uint32_t kPBmp = 8;          // byte* para a primeira linha
  static constexpr std::uint32_t kPRGB = 12;         // uint32* da paleta
  static constexpr std::uint32_t kNcTransparent = 16;  // NativeColor (uint32, AEEIBitmap.h:32)
  static constexpr std::uint32_t kCx = 20;           // uint16
  static constexpr std::uint32_t kCy = 22;           // uint16
  static constexpr std::uint32_t kNPitch = 24;       // int16, bytes de uma linha para a seguinte
  static constexpr std::uint32_t kCntRGB = 26;       // uint16
  static constexpr std::uint32_t kNDepth = 28;       // uint8, BITS por pixel
  static constexpr std::uint32_t kNColorScheme = 29; // uint8, IDIB_COLORSCHEME_565 = 16
  static constexpr std::uint32_t kReservado = 30;    // 6 bytes, "initialize to 0"
  static constexpr std::uint32_t kTamanho = 36;
  // `IDIB_COLORSCHEME_565` (`AEEIDIB.h:32`): 5 bits R, 6 G, 5 B -- o pixel da Tela.
  static constexpr std::uint8_t kEsquemaDeCor565 = 16;
};

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

// O nome que o SDK da ao slot das interfaces de stream com nome proprio
// (frente io2): `IUnzipAStream` (AEEUnzipStream.h) e `IMemAStream` (AEE.h).
// Usados pelo ramo de nomes do despacho para nao dizer `slot<N>` de uma
// interface que tem nomes em cabecalho.
const char* NomeDeUnzipStream(unsigned slot);
const char* NomeDeMemStream(unsigned slot);

// ---------------------------------------------------------------------------
// A FAMILIA DO IBITMAP (frente ibmap2) -- SERVIDA, e CABLADA no despacho.
//
// O `IBitmap` tem 16 slots (`AEEIBitmap.h`, `INHERIT_IBitmap`): 3 de cabeca
// (AddRef, Release, QueryInterface) + RGBToNative(3), NativeToRGB(4), DrawPixel(5),
// GetPixel(6), SetPixels(7), DrawHScanline(8), FillRect(9), BltIn(10), BltOut(11),
// GetInfo(12), CreateCompatibleBitmap(13), SetTransparencyColor(14),
// GetTransparencyColor(15).
//
// MEDIDO (corrida de referencia /tmp/corrida_thrd.json, titulos do corpus):
//   `IBitmap::slot13` em 6 titulos (fifa09, pacmania, tectoy, tekken2, zeebo_app,
//   zenonia) e `torkandkral` com 17 pedidos; `IImageDecoder::GetBitmap` em 4
//   (abd, heavyweaponbrew, peggle, torkandkral); `IGLES11::TexEnvx` em 4 (abd,
//   pacmania, peggle, torkandkral). Ver /tmp/pesquisa/ibitmap.md.
//
// O que os titulos fazem DEPOIS do slot13 (traco + desmonte, ver o relatorio):
//   - tekken2 (lr=0x1ba8c): `CreateCompatibleBitmap` sobre o bitmap do ecra
//     (0x80050300), e a seguir `RGBToNative` (slot 3) + `DrawPixel` (slot 5)
//     por pixel, desenhando recursos `image/bmp` do .bar PARA o DIB novo;
//   - pacmania (lr=0x12d94): slot13 por imagem carregada (LOADING_STAGE_IMAGES),
//     e a seguir `QueryInterface(0x01001045 = AEEIID_IDIB)` (slot 2) +
//     `GetInfo` (slot 12), com escrita DIRECTA no `pBmp`.
//
// ESTE FICHEIRO SERVE A FAMILIA TODA (slots 2..15 da faixa 8000) por cima do
// CABECALHO PUBLICO DO IDIB (`AEEIDIB.h:42-55`, `CamposDoIdib`): as dimensoes,
// o passo e o buffer de cada bitmap vivem na memoria do guest, e o servidor
// le-os de la -- a mesma fonte que o jogo usa, e a unica que nao pode divergir.
//
// COMO ESTA LIGADO (o ramo vive em `core/brew/despacho.cpp`, ANTES do ramo
// generico `idx >= kBaseDoShell` -- ver a nota la, o lugar e contrato):
//
//     } else if (idx >= zb2::brew::kVtableBitmap + 2 &&
//                idx < zb2::brew::kVtableBitmap + 16 &&
//                AtenderBitmapDaFamilia(cpu, mem_, al_, traco_, idx)) {
//
// A vtable de TODOS os objectos DIB (o ecra em `kObjDibBase+0x300`, os dos
// `IDisplay::CreateDIBitmap` e os criados por este slot13) ja aponta para a
// faixa 8000 (`ConstruirVtableDoBitmap`), logo o ramo serve a familia sem
// tocar em cablagem nenhuma. Devolve `true` quando o indice e desta faixa e foi
// atendido (com sucesso OU com recusa registada); `false` para o despacho
// seguir a cadeia.
bool AtenderBitmapDaFamilia(ICpu& cpu, Memoria& mem, Alocador& al, Traco& traco,
                            std::uint32_t indice);

// Os IIDs que o `IDIB` responde (frente ibmap2): UMA lista, usada nos DOIS
// caminhos que respondem ao `QueryInterface` do bitmap -- o servidor da familia
// (slots 8002..8015) e o ramo do despacho no endereco que a ferramenta cabla no
// slot 2 (`tools/bateria.cpp`, `kWire`). Duas listas que tem de concordar sao
// zero listas: foi uma divergencia dessas que fez a bateria cablar o slot 12 do
// IBitmap para o `strcat` (ver o aviso em `tools/bateria.cpp:268-289`).
//
// `0x01001045` e o `AEECLSID_DIB` (`AEEClassIDs.h:157`), `0x0100102c` o
// `AEECLSID_DIB_20` (`:114`) e `0x01001029` (`CORE+41`, hoje `AEECLSID_TRANSFORM`,
// `:111`) e o valor do DIB de uma versao ANTERIOR do BREW -- MEDIDO: o `fifa09` e
// o `zenonia` pedem exactamente esse IID ao bitmap que acabaram de criar.
bool IidDeDib(std::uint32_t iid);

}  // namespace zb2::brew

#endif
