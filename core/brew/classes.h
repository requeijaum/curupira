#ifndef ZB2_CORE_BREW_CLASSES_H
#define ZB2_CORE_BREW_CLASSES_H

// AS CLASSES QUE O CORPUS CRIA POR `IShell::CreateInstance` E QUE O MOTOR NAO
// CONHECIA.
//
// O QUE ISTO RESOLVE, medido antes de existir (bateria, commit `ecc97b0`):
//
//     IShell::CreateInstance CLSID desconhecido  pedido 3x
//         iid=0x0100104f ppo=0x8007ff74   1x     (tectoy, o Z-Wheel)
//         iid=0x01003109 ppo=0x80200380   1x     (zenonia)
//         iid=0x01028e3c ppo=0x8020339c   1x     (tectoy)
//
// Os tres nomes saem de `tools/clsids.inc` (GERADO dos cabecalhos do SDK):
//
//     0x0100104f = AEECLSID_AppHistory    AEEAppHistory.bid:9   -> IAppHistory
//     0x01003109 = AEECLSID_TEXTCTL       AEEClassIDs.h:209      -> ITextCtl
//     0x01028e3c = AEECLSID_VALUEMODEL_1  AEECLSID_VALUEMODEL_1.bid:31 -> IValueModel
//
// O QUE FOI MEDIDO DO USO QUE OS TITULOS FAZEM DELAS -- e nao deduzido.
//
// Uma sonda temporaria (objecto com uma vtable de 64 slots, um endereco de saida
// distinto por slot, `ZB2_TRACE=1`) mostrou QUE SLOT cada titulo pede. Depois
// cada pedido foi conferido no DESMONTE do proprio modulo, porque a vtable e a
// assinatura sao duas fontes e tem de concordar:
//
//   tectoy  -- vtable 2200, `[r1,#0x14]` = slot 5 e `[r1,#0x04]` = slot 1:
//       0x045990  ldr  r2, [r1, #0x18]   ; slot 6 -- ITextCtl (outro titulo)
//       0x0006c3e8  Top    (slot 5)      ; 0x6c3e8 -> `ldr r2,[r1,#0x14]`
//       0x0006c410  Release(slot 1)
//   zenonia -- seis slots, todos conferidos no codigo em 0x0459xx e 0x06630c:
//
//       0x45998  ldr r2,[r1,#0x18]  slot 6   SetRect        r1 = &rect na pilha
//       0x459b0  ldr r2,[r1,#0x20]  slot 8   SetProperties  r1 = 0x80010000
//       0x459c8  ldr r2,[r1,#0x48]  slot 18  SetInputMode   r1 = 3
//       0x459e0  ldr r2,[r1,#0x10]  slot 4   SetActive      r1 = 1
//       0x66330  ldr r1,[r1,#0x14]  slot 5   IsActive
//       0x6635c  ldr ip,[r1,#0x08]  slot 2   HandleEvent
//
//   E OS ARGUMENTOS CONFEREM COM O CABECALHO (`platform/deprecated/inc/AEEText.h`):
//   `TP_FRAME|TP_FIXSETRECT` = 0x00010000|0x80000000 = **0x80010000**, exactamente
//   o literal em 0x45a08; `AEE_TM_LETTERS` = **3** (AEEText.h:76); `SetActive`
//   recebe `boolean` e leva **1**. A ordem dos slots tambem: o gerador
//   (`tools/gerar_slots.py`) resolve `QINTERFACE(ITextCtl)` + `DECLARE_IBASE`(2) +
//   `DECLARE_ICONTROL`(9) e poe o `SetRect` no 6, o `SetProperties` no 8, o
//   `SetInputMode` no 18 e o `SetActive` no 4 -- os MESMOS numeros que o modulo
//   pede. **Duas fontes independentes do SDK, e elas concordam.**
//
// O DESENHO, e o que NAO se faz:
//
//   - a classe passa a existir e o objecto e entregue. O `CreateInstance` de um
//     `AEECLSID_TEXTCTL` no aparelho a serio DEVOLVE um objecto -- recusa-lo era
//     a mentira, nao o contrario;
//   - CADA METODO nao implementado RECUSA em voz alta e registra-se com o NOME do
//     metodo (`ITextCtl::SetInputMode`), que e o que faz a lista de demanda passar
//     a dizer o que falta em vez de repetir tres numeros;
//   - o que se implementa e SO o que a medicao justifica: o `IAppHistory::Top`.
//     Nada de `bool` a fingir sucesso (P2) -- nem um objecto que se diz completo.
//
// PORQUE ISTO E UM BLOCO DE ENDERECOS PROPRIO, E PORQUE E EM `0x8F000000`.
//
// O mapa dos enderecos de objecto esta em `core/brew/igl.h` (comentario "o mapa
// medido dos enderecos de objecto deste emulador"); a ultima linha escrita la e
// `0x800B0000 IGL | 0x800B1000 IEGL`. A faixa de SAIDAS usada aqui e 40000 (as da
// entrada sao 20000+, as do IGL 30000/31000): uma faixa que se sobreponha a outra
// apaga uma vtable em silencio -- foi o defeito medido que obrigou o IGL a mudar
// de faixa.
//
// **`0x800C0000` NAO ESTAVA LIVRE, E ISSO FOI MEDIDO.** A primeira versao desta
// frente pos os tres objectos em `0x800C0000` (a "proxima linha livre" do mapa) e
// dois titulos mudaram de comportamento:
//
//     ./build/zb2_bateria "$corpus" "$mods" /tmp/depois.json
//     ./build/zb2_comparar /tmp/antes.json /tmp/depois.json
//       gof (277380): passos_create 1601656 -> 4000000
//                     recusadas      45 -> 3733093
//                     motivo "create:saiu_do_modulo_para_0x4278252046" -> "create:orcamento_esgotado"
//       rmp (278282): passos_create 1601028 -> 4000000
//                     recusadas       0 -> 3733092
//
// Nenhum dos dois pede nenhuma das tres classes (o `faltas` deles nao muda): o que
// muda e a MEMORIA. Eles LEEM `0x800C0000`, que estava a zero, e passam a ler o
// ponteiro da vtable. **Um endereco "livre" no mapa nao e um endereco que o
// CORPUS nao usa** -- o mapa foi escrito por quem instalou os objectos, e nao por
// quem mediu os 62 titulos.
//
// `0x8F000000` e o endereco que fica: acima de tudo o que o corpus mapeia (as
// imagens tem a base ZERO e menos de 1 MB; o heap e `0x80200000`+12 MB; a pilha
// `0x80080000`; a entrada `0x81030000`) e abaixo da faixa de saida (`0xF0000000`).
// A prova e a bateria inteira: com este endereco, os 62 titulos dao o MESMO
// resultado que antes, fora dos tres CLSIDs desta frente.

#include <cstdint>

#include "core/brew/ajudantes.h"
#include "core/brew/interface.h"
#include "core/cpu/cpu.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"

namespace zb2::brew {

// O motor de estado do IGL de 80 slots (`core/brew/igl.h`), que a frente
// glbloco passa a servir ao objecto IGLES11 (ver `EstadoDoIgles11`). So se
// usa como ponteiro opaco aqui; a definicao completa fica no `.cpp`.
class Igl;

// As tres classes conhecidas. A ORDEM e a ordem em que a bateria as encontrou.
enum class Classe : std::uint32_t {
  kAppHistory = 0,
  kValueModel_1 = 1,
  kTextCtl = 2,
  kThread = 3,
  kPNGDecoderBREW = 4,
  kQEGL = 5,
  kCM = 6,
  kLicense = 7,
  kVectorModel_1 = 8,
  kSourceUtil = 9,
  kQuantas = 10,
};

constexpr std::uint32_t kQuantasClasses = static_cast<std::uint32_t>(Classe::kQuantas);
// 32 slots por objecto: nenhuma das tres interfaces passa de 28 (`ITextCtl`,
// `AEEText.h`). O limite e o mesmo para as tres, para o calculo do indice ser uma
// divisao e nao uma tabela.
constexpr std::uint32_t kSlotsDaClasse = 32;
// A FAIXA DE SAIDAS destas classes. 40000 nao colide com 1000 (ajudantes), 2000
// (IShell), 6000/7000/8000/9500 (IDisplay/IFileMgr/DIB/ficheiro), 9000 (genericos),
// 1500-1564 (metodos), 20000 (entrada) nem 30000/31000 (IGL/IEGL).
//
// O IGLES11 vive em faixa propria (40300+, 148 slots): nao cabe nos 32 por
// classe, e alargar o passo partia a aritmetica de todas as outras.
constexpr std::uint32_t kVtableClasseBase = 40000;
// QEGL = IEGL sem GetProcAddress (slot 8): tudo a partir de 8 desloca -1
// (zeemu BrewEGL.cpp setup_vtables; 27 slots + Fn).
constexpr std::uint32_t kQeglSlots = 27;
constexpr std::uint32_t QeglParaIegl(std::uint32_t q) { return q >= 8 ? q + 1 : q; }
constexpr std::uint32_t kVtableIgles = 40300;
constexpr std::uint32_t kIglesSlots = 148;
constexpr std::uint32_t kObjetoIgles = 0x8F010000u;
// A ZONA DE STRINGS DO IGLES11 (`glGetString`). Um bloco de 4 KB a seguir ao
// objecto, DENTRO da mesma faixa `0x8F000000` que a bateria inteira ja provou
// que o corpus nao toca (ver o comentario do `0x800C0000` acima: um endereco
// "livre" no mapa nao e um endereco que o CORPUS nao usa, e isso foi MEDIDO).
//
// O desenho copia o do `core/brew/egl.h` (`kZonaDeStrings`): um endereco fixo
// por consulta, escrito quando a consulta acontece. **Nunca se devolve NULO** --
// ha um caso medido na arvore antiga (`ddragonz` mete o resultado do
// `eglQueryString` num `strstr` sem testar o nulo).
constexpr std::uint32_t kZonaDeStringsIgles = kObjetoIgles + 0x1000u;
constexpr std::uint32_t kPassoDeStringIgles = 0x100;

// O `IGLES11Ext` -- OUTRA interface, OUTRO objecto (AEEGLES11Ext.h,
// AEEIID_GLES11EXT = 0x0103d8eb): 3 + 12 = 15 slots.
//
// PORQUE EXISTE, MEDIDO (sonda: `cninja` com `GL_OES_draw_texture` anunciado no
// `glGetString`): o titulo passa a pedir `QEGL::QueryInterface` com OITO IIDs
// seguidos, e este e o que lhe da o `glDrawTexivOES` que a extensao promete.
// Os oito, todos identificados nos cabecalhos do SDK 4.0.2:
//     0x010426e3 EGLOESSWAPINTERVAL   0x0103d8ef EGLGETCOLORBUFFER
//     0x0103d8f0 EGLGETPOWERLEVEL     0x01051834 EGLSURFACEMANIP
//     0x0103d8de GLES10EXT            0x0103d8eb GLES11EXT   <-- este
//     0x0103def1 GLES11EXTPAK         0x01058546 GLESIMAGEONEXT
// As strings do `.mod` (`framework/GLES_ext.c`) dizem que o titulo TOLERA a
// ausencia dos outros ("platform does not support ... interface").
constexpr std::uint32_t kVtableIglesExt = 40500;
constexpr std::uint32_t kIglesExtSlots = 15;
constexpr std::uint32_t kObjetoIglesExt = 0x8F020000u;
constexpr std::uint32_t kIidGles11Ext = 0x0103d8ebu;

// --- AS SETE EXTENSOES QUALCOMM DO QEGL (frente qualcomm) ------------------
//
// MEDIDO na bateria (corrida /tmp/corrida_thrd.json, 14 titulos): cninja,
// spinmast, strhoop, supbtime, karnovr, wizdfire, magdrop3, darkseal,
// baddudes, hbarrel, gof, pbc, ridgeracer e rmp pedem, no `QueryInterface` do
// objecto da CLASSE QEGL (slot 2), sete IIDs de extensao -- 1x cada. O wrapper
// do SDK dentro do `.mod` (`GLES_ext.c`) guarda o objecto que receber e chama
// depois os SLOTS dele; servir o IID com um objecto cuja vtable tem o tamanho
// REAL do SDK e o minimo que nao faz a primeira chamada cair num slot que nao
// existe (o aviso do zeebx, machine.rs 9840-9860).
//
// A CONTAGEM DE SLOTS VEM DOS CABECALHOS DO SDK 4.0.2 (a linha ao lado de cada
// nome, no `.cpp`). As V2 sao superconjunto das V1 com o MESMO prefixo de
// vtable (zeebx): um so objecto atende as duas IIDs de cada par.
//
// AS FAIXAS de saida: 40000-40223 sao as classes, 40300-40447 o IGLES11,
// 40500-40514 o IGLES11Ext. Nada mais vive a seguir; estas sete comecam em
// 40600, cada uma na sua base, e o teste prova que o slot ALEM da ultima
// tabela nao e atendido (uma vtable maior do que a real e o defeito que o
// zeebx avisou).
//
// OS OBJECTOS ficam depois do pool das pilhas das threads (0x8F030000 +
// 0x200000 = 0x8F230000), dentro da faixa 0x8F000000 que a bateria inteira
// ja provou que o corpus nao toca (o comentario do 0x800C0000 no topo).
constexpr std::uint32_t kVtableEglGetColorBuffer = 40600;   // 4 (AEEEGLGetColorBuffer.h)
constexpr std::uint32_t kEglGetColorBufferSlots = 4;
constexpr std::uint32_t kVtableEglSurfaceManip = 40700;     // 27 (AEEEGLSurfaceManip.h, V2)
constexpr std::uint32_t kEglSurfaceManipSlots = 27;
constexpr std::uint32_t kVtableGlesImageonExt = 40800;      // 27 (AEEGLESImageonEXT.h, V2)
constexpr std::uint32_t kGlesImageonExtSlots = 27;
constexpr std::uint32_t kVtableGles10Ext = 40900;           // 4 (AEEGLES10Ext.h)
constexpr std::uint32_t kGles10ExtSlots = 4;
constexpr std::uint32_t kVtableGles11ExtPak = 41000;        // 30 (AEEGLES11ExtPak.h)
constexpr std::uint32_t kGles11ExtPakSlots = 30;
constexpr std::uint32_t kVtableEglOesSwapInterval = 41100;  // 5 (AEEEGLOESSwapInterval.h)
constexpr std::uint32_t kEglOesSwapIntervalSlots = 5;
constexpr std::uint32_t kVtableEglGetPowerLevel = 41150;    // 4 (AEEEGLGetPowerLevel.h)
constexpr std::uint32_t kEglGetPowerLevelSlots = 4;

constexpr std::uint32_t kObjetoEglGetColorBuffer = 0x8F240000u;
constexpr std::uint32_t kObjetoEglSurfaceManip = 0x8F241000u;
constexpr std::uint32_t kObjetoGlesImageonExt = 0x8F242000u;
constexpr std::uint32_t kObjetoGles10Ext = 0x8F243000u;
constexpr std::uint32_t kObjetoGles11ExtPak = 0x8F244000u;
constexpr std::uint32_t kObjetoEglOesSwapInterval = 0x8F245000u;
constexpr std::uint32_t kObjetoEglGetPowerLevel = 0x8F246000u;

// --- O DESCODIFICADOR PNG: `IImageDecoder` + `IForceFeed` (frente imgdec) ------
//
// O `AEECLSID_PNGDECODER_BREW` (0x01030766) ja era criado aqui, mas so a CABECA
// era servida: os cinco slots do `IImageDecoder` recusavam todos com o nome. Os
// QUATRO titulos que queimam os 8 M passos num laco de conversao de imagem
// (`abd` 279369, `peggle` 278962, `torkandkral` 280463, `heavyweaponbrew`
// 278200) param exactamente ai:
//
//   CreateInstance(0x01030766)
//   QI(AEEIID_IForceFeed 0x0101eb0b)          <- falta 1x por titulo
//   Write(dados)
//   GetBitmap(&pBitmap)                       <- falta 1x por titulo; pBitmap fica
//                                               a NULL e o jogo converte os pixels
//                                               de um bitmap que nao existe
//
// MEDIDO em `/tmp/pesquisa/memo.md` (seccao 2): com o descritor a NULL o jogo le
// a CABECA DO PROPRIO MODULO como se fosse o cabecalho do IDIB (`[0+8]`,
// `[0+0x14]`, `[0+0x16]`) e percorre 0x1c34e1a0 = 473 227 680 pixels de paginas
// virgens ate gastar o orcamento.
//
// OS DOIS IIDs vem dos cabecalhos do SDK e os slots do `.inc` GERADO
// (`brew_slots.inc`, de `AEEIImageDecoder.h` e `AEEIForceFeed.h`): o
// `INHERIT_IQI` da os tres primeiros e a posicao do metodo o resto.
constexpr std::uint32_t kIidImageDecoder = 0x01026e20u;  // AEEIImageDecoder.h:30
constexpr std::uint32_t kIidForceFeed = 0x0101eb0b;      // AEEIForceFeed.h:27

// A SEGUNDA VTABLE DO MESMO OBJECTO. Um objecto BREW que exporta duas interfaces
// entrega um PONTEIRO por interface, cada um com a vtable dele no `+0`: e por
// isso que o `QueryInterface` devolve outro endereco, e nao o proprio.
//
// 40520 esta LIVRE no mapa das faixas: 40000-40223 sao as classes, 40300-40447 o
// IGLES11, 40500-40514 o IGLES11Ext e 40600+ as sete extensoes QUALCOMM.
constexpr std::uint32_t kVtableForceFeed = 40520;
// AS FONTES STANDARD: UMA vtable partilhada de 6 slots em 40448 (faixa livre
// 40448-40499, antes do IglesExt), com UM objecto por CLSID para a metrica
// levar o tamanho nominal certo. Nao entram na aritmetica das classes
// (`VtClasse(k)`): os 96 saidas de tres classes 10/11/12 cairiam DENTRO do
// IGLES11 (40300-40447) e a cablagem de uma apagava a vtable do outro --
// medido, com 6 testes a falhar. Precedente: o `IForceFeed`, tambem fora da
// faixa, tambem com ramo explicito.
constexpr std::uint32_t kVtableFonte = 40448;
constexpr std::uint32_t kFonteSlots = 6;
constexpr std::uint32_t kObjetoFonte11 = 0x8F00A000u;
constexpr std::uint32_t kObjetoFonte15 = 0x8F00B000u;
constexpr std::uint32_t kObjetoFonte36 = 0x8F00C000u;
constexpr std::uint32_t kIidFonte = 0x01001022u;  // AEEIID_IFont (AEEIFont.h)
static_assert(kVtableFonte + kFonteSlots <= 40500,
              "a vtable das fontes tem de caber antes do IGLES11Ext");
constexpr std::uint32_t kForceFeedSlots = brew_slots::kForceFeedSlots;
static_assert(brew_slots::kForceFeedSlots == 5, "o IForceFeed tem 5 slots (IQI 3 + Write + Reset)");
//
// O ENDERECO DO OBJECTO e 0x8F010100: o RESTO DA PAGINA DO OBJECTO DO IGLES11
// (o objecto em +0, a zona de strings dos `glGetString` em +0x1000). Foi MEDIDO
// que isto importa: a memoria do guest e esparsa POR PAGINAS, e uma pagina nova
// muda o numero de leituras recusadas de um titulo que varre paginas virgens --
// com o objecto em 0x8F008000 (pagina nova) o `zeebopeteca` passava de 532 503
// para 532 502 leituras recusadas, e com este endereco volta a 532 503. Uma
// pagina que a corrida ja escreve nao acrescenta nada ao mapa.
constexpr std::uint32_t kObjetoForceFeed = 0x8F010100u;
// JPEG usa objecto e vtable distintos do PNG: duas instancias podem coexistir,
// e um Write num formato nao pode contaminar o fluxo do outro.
constexpr std::uint32_t kObjetoForceFeedJpeg = 0x8F010200u;
constexpr std::uint32_t kVtableForceFeedJpeg = 40530u;
constexpr std::uint32_t kVtableJpegDecoder = 40540u;
constexpr std::uint32_t kObjetoJpegDecoder = 0x8F010300u;

// IWeb (AEECLSID_Web, 0x01005000): a tabela medida tem 13 slots. Ela fica
// fora da faixa das classes (40000..40223) e depois do IHashCtx (41200..41206),
// pois aumentar kQuantasClasses invadiria a vtable do IGLES11 em 40300.
constexpr std::uint32_t kVtableWeb = 41300u;
constexpr std::uint32_t kWebSlots = 13u;
constexpr std::uint32_t kObjetoWeb = 0x8F251000u;

// A BANDA DOS PIXELS DESCODIFICADOS. `0x8F300000`: dentro da faixa 0x8F000000 que
// a bateria inteira ja provou que o corpus nao toca (o comentario do 0x800C0000
// no topo deste ficheiro), ACIMA do pool das pilhas das threads (0x8F030000 +
// 2 MiB = 0x8F230000) e dos objectos QUALCOMM (0x8F240000..0x8F247000). Os 4 MiB
// cobrem 1 Mi-pixel em RGB565 (o teto do descodificador) com folga para varias
// imagens do mesmo titulo; esgotada a banda, a recusa diz os numeros.
constexpr std::uint32_t kBandaDosPixelsDoPng = 0x8F300000u;
constexpr std::uint32_t kBytesDaBandaDoPng = 0x00400000u;

// OS OBJECTOS IDIB DO DESCODIFICADOR. Tem de ficar dentro da pagina que o
// despacho reconhece como "um objecto de bitmap" (`despacho.h`,
// `EUmObjectoDeBitmap`: 0x80050000..0x80051000) -- e isso que faz o
// `AddRef`/`Release` deles contar do lado do despacho em vez de escrever no `+4`,
// que num IDIB e o `pPaletteMap` PUBLICO (`AEEIDIB.h:44`) e nao uma contagem.
//
// A CONSTRUCAO DESCE DO TOPO (0x80050FC0, passo 0x40, ate 0x80050900): o
// `IDisplay::CreateDIBitmap` sobe do FUNDO da mesma pagina (`despacho.cpp`,
// `kObjDibBase + dibs_ * 0x40`) e o bitmap do ecra esta em +0x300. As duas pontas
// so se cruzam se um titulo alocar 60 DIBs pelo `CreateDIBitmap`.
constexpr std::uint32_t kObjetosDibDoPng = 0x80050FC0u;
constexpr std::uint32_t kPassoDoObjetoDib = 0x40u;
constexpr std::uint32_t kMaximoDeObjetosDibDoPng = 28u;

// O NOME de UM slot destas sete interfaces, das listas dos `INHERIT_*` do SDK
// (os ficheiros e as linhas estao no `.cpp`). "?" fora da tabela.
const char* NomeDoSlotEglGetColorBuffer(std::uint32_t slot);
const char* NomeDoSlotEglSurfaceManip(std::uint32_t slot);
const char* NomeDoSlotGlesImageonExt(std::uint32_t slot);
const char* NomeDoSlotGles10Ext(std::uint32_t slot);
const char* NomeDoSlotGles11ExtPak(std::uint32_t slot);
const char* NomeDoSlotEglOesSwapInterval(std::uint32_t slot);
const char* NomeDoSlotEglGetPowerLevel(std::uint32_t slot);
// IIDs de interface (AEEGLES10/11.h via 3 refs; sem .h no SDK extract).
constexpr std::uint32_t kIidGles10 = 0x0103d8ddu;
constexpr std::uint32_t kIidGles11 = 0x0103d8eau;
constexpr std::uint32_t VtClasse(std::uint32_t k) {
  return kVtableClasseBase + k * kSlotsDaClasse;
}
// Os enderecos dos objectos no espaco do guest. `0x800C0000 + k*0x1000`, um bloco
// de 4 KB por classe -- o primeiro livre depois do IGL/EGL em `0x800B0000`.
constexpr std::uint32_t ObjetoDaClasse(std::uint32_t k) { return 0x8F000000u + k * 0x1000u; }

// O indice da classe deste CLSID, ou `kQuantasClasses` quando nao e nenhuma
// delas. E o unico sitio onde os tres numeros aparecem.
std::uint32_t IndiceDaClasse(std::uint32_t clsid);

// O nome do CLSID (`AEECLSID_AppHistory`), o nome da interface que a classe
// entrega (`IAppHistory`) e o nome de UM slot dessa interface (`Top`).
const char* NomeDaClasse(std::uint32_t k);
const char* NomeDaInterface(std::uint32_t k);
const char* NomeDoSlotDaClasse(std::uint32_t k, std::uint32_t slot);

// O objecto desta classe, ou 0. Pura: o endereco nao depende de estado nenhum.
std::uint32_t ObjetoDoClsid(std::uint32_t clsid);
// O objecto da fonte pedida (0 se nao e fonte). Fora da aritmetica das classes.
std::uint32_t ObjetoDaFonte(std::uint32_t clsid);

// `true` = este slot tem implementacao (e so o `IAppHistory::Top` o tem hoje).
bool SlotDaClasseImplementado(std::uint32_t k, std::uint32_t slot);

// Escreve os tres objectos e as tres vtables na memoria do guest, uma vez, com a
// faixa de saida ja montada. LE A CABLAGEM DE VOLTA e diz (no traco) se um slot
// ficou por cablar -- uma cablagem perdida numa edicao ja custou uma corrida
// inteira neste trabalho.
void ConstruirClasses(Memoria& mem, const Saidas& saidas, Traco& traco);

// O objeto IGLES11 (faixa propria): constroi vtable+objeto uma vez.
void ConstruirIgles(Memoria& mem, const Saidas& saidas, Traco& traco);
// Nome do slot IGLES11 e do slot IGLES11Ext, das tabelas GERADAS dos cabecalhos
// (`tools/igles_slots.inc`).
const char* NomeDoSlotIgles(std::uint32_t slot);
const char* NomeDoSlotIglesExt(std::uint32_t slot);

// Repoem o estado do ITextCtl (ativo/props/modo) no arranque. Sem isto, dois
// testes na mesma Bancada veriam o estado um do outro -- statics partilhados.
void ReporEstadoTextCtl();

// Repoe o estado do descodificador PNG (fluxo escrito, imagem descodificada, banda
// de pixels e busca dos objectos IDIB) no arranque de cada corrida. Chamado pelo
// `ConstruirClasses`, como o `ReporEstadoTextCtl`: sem isto, o fluxo de um titulo
// seria a imagem do titulo seguinte.
void ReporEstadoDoPng();
void ReporEstadoDoJpeg();

// --- O IThread cooperativo ---------------------------------------------------
//
// A thread do BREW e cooperativa e apoia-se em callbacks: corre ate chamar
// `ITHREAD_Suspend` e volta quando o `AEECallback` de `ITHREAD_GetResumeCBK` e
// disparado -- tipicamente pelo jogo, via `ISHELL_Resume` (`AEEThread.h`, o
// guia `ZeeboDeveloperGuide0.97.md:758-764`). A referencia de comportamento e
// o zeebx (`src/machine/thread.rs`, e o `IThread` novo em `/tmp/zx-new`):
// o `Start` aloca a pilha, guarda o `resume_pc`, poe `r0=this`, `r1=arg`,
// `sp=topo` e AGENDA a thread; o `Exit` termina e devolve o controlo; o
// `Suspend` e o ponto onde a thread devolve o controlo. MEDIDO na bateria
// (62 titulos, ZB2_QUADROS=300 ZB2_EVT_START=1): 22 titulos pedem `Start` uma
// vez, e nenhum pede mais nenhum metodo do IThread.
//
// O LADO DA CORRIDA E DO DESPACHO, e nao deste ficheiro: o `despacho.cpp` tem
// o laco de eventos, e e la que a thread pendente e retomada na fronteira do
// laco (o `run_pending_threads` do zeebx, uma volta por quadro). Este ficheiro
// declara o CONTRATO dessa ligacao e prova-o por testes:
//   - `TemThreadPendente`: ha uma thread iniciada e ainda nao retomada;
//   - `PrepararRetomadaDeThread`: restaura o contexto (r0..r12, sp), poe o
//     lr na sentinela e entrega o controlo ao guest na `retomar_pc` -- o laco
//     do despacho corre a partir daqui ate ao proximo `Suspend`/`Exit`;
//   - `ConcluirRetomadaDeThread`: a fechar a corrida, termina a thread quando
//     a funcao de entrada voltou sem passar por `Suspend`/`Exit` (zeebx
//     `resume_thread`), com o rv = r0 do guest;
//   - `EnfileirarThreadPeloCallbackDeRetomada`: para o `ISHELL_Resume` do
//     despacho -- quando `pcb` e o `GetResumeCBK` de uma thread, enfileira-a.
//
// A SENTINELA e 0xFFFFFFF0, o MESMO `kSentinela` do `despacho.cpp` (o "retorno
// para o sistema" desta arvore). Uma segunda copia do numero, e fica AQUI com
// este aviso: unificar as duas exige tirar o `kSentinela` do anonimo do
// despacho, um ficheiro que esta fora do alcance desta frente.
constexpr std::uint32_t kSentinelaDoHospedeiro = 0xFFFFFFF0u;
// `AEE_EALREADY = 26` (`AEEStdErr.h:26`). Ausente do enum de `ajudantes.h`
// (que so tem os codigos que o despacho usa); o `Start` e o `Exit` do IThread
// devolvem-no (`AEEThread.h`: "An _Start() may only be called once" e
// "EALREADY: if the IThread is already stopped").
constexpr std::int32_t kAeeAlready = 26;
// O POOL DAS PILHAS das threads. 0x8F030000, logo a seguir ao IGLES11Ext
// (0x8F020000) e DENTRO da faixa 0x8F000000 que a bateria provou que o corpus
// nao toca (o comentario no topo deste ficheiro: 0x800C0000 NAO estava livre,
// e isso foi MEDIDO). 2 MiB cobrem a maior pilha medida na bateria (0x100000 =
// 1 MiB, um titulo) com folga para mais um bloco. O pool e um `bump` simples,
// reposto por corrida em `ConstruirClasses`.
constexpr std::uint32_t kPilhasDeThreadInicio = 0x8F030000u;
constexpr std::uint32_t kPilhasDeThreadTamanho = 0x00200000u;

bool TemThreadPendente();
bool PrepararRetomadaDeThread(ICpu& cpu, Traco& traco);
void ConcluirRetomadaDeThread(ICpu& cpu, Traco& traco);
bool EnfileirarThreadPeloCallbackDeRetomada(std::uint32_t pcb);

// Atende um pedido desta faixa. `false` = o indice nao e destas classes (e o
// chamador segue a cadeia de ramos). `true` = foi atendido, com sucesso OU com
// recusa REGISTADA.
bool AtenderClasse(ICpu& cpu, std::uint32_t indice, Traco& traco);

// O ESTADO DO OBJECTO IGLES11 (frente glbloco, 8 titulos). Um motor do MESMO
// tipo do IGL de 80 slots, reconstruido por `ConstruirIgles` (uma vez por
// corrida, como o resto do estado das classes) e observavel pelo teste: e o
// mesmo estado que o rasterizador consome no desenho. `nullptr` antes de
// `ConstruirIgles`.
const Igl* EstadoDoIgles11();

}  // namespace zb2::brew

#endif  // ZB2_CORE_BREW_CLASSES_H
