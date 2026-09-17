#ifndef ZB2_CORE_BREW_IMEDIA_H
#define ZB2_CORE_BREW_IMEDIA_H

// IMedia -- a interface de midia do BREW (etapa 5 do PLAN.md).
//
// TUDO AQUI VEM DE UM CABECALHO DO SDK OU DE UMA MEDICAO FEITA NESTA ARVORE, e
// cada numero diz de onde veio (P1). As tres fontes, por ordem de importancia:
//
// 1. A TABELA DE SLOTS, do cabecalho `platform/media/inc/AEEIMedia.h`:
//
//      #define INHERIT_IMedia(iname)
//         INHERIT_IQI(iname);
//         int (*RegisterNotify)(iname *po, PFNMEDIANOTIFY, void *);
//         int (*SetMediaParm)(iname *po, int nParamID, int32 p1, int32 p2);
//         int (*GetMediaParm)(iname *po, int nParamID, int32 *pP1, int32 *pP2);
//         int (*Play)(iname *po);            int (*Record)(iname *po);
//         int (*Stop)(iname *po);            int (*Seek)(iname *po, AEEMediaSeek, int32);
//         int (*Pause)(iname *po);           int (*Resume)(iname *po);
//         int (*GetTotalTime)(iname *po);    int (*GetState)(iname *po, boolean *);
//
//    A CABECA E `INHERIT_IQI`, e nao `INHERIT_IBase`: o IMedia TEM
//    `QueryInterface`, que fica no slot 2. Os 11 metodos proprios vao do 3 ao 13,
//    portanto a interface tem **14 slots**. NAO foi escrito a mao: sai do
//    `tools/gerar_slots.py`, que le os cabecalhos, e o `zb2_tests` compara as
//    constantes que este ficheiro usa com as geradas. Comando:
//
//      python3 tools/gerar_slots.py "$SDK" tools/brew_slots.inc
//
//    ARMADILHA DECLARADA: um dos dois primeiros slots ja esteve mal identificado
//    nesta sessao (a IBase foi contada com tres membros). Aqui o teste
//    `Media.ATabelaDeSlotsEADoSDK` falha se a cabeca deixar de ser a IQI.
//
// 2. A ORDEM DOS CAMPOS DE `AEEMediaCmdNotify`, MEDIDA NO PROPRIO CORPO DO
//    CORPUS, e nao copiada. O tratador real do `cnk2` (pasta 274214) esta em
//    `0x00101a80`; lendo as palavras de 32 bits do ficheiro `cnk2.mod` a partir
//    do deslocamento `0x1a80` (o modulo carrega em `0x00100000`, logo
//    deslocamento de ficheiro == endereco - 0x00100000):
//
//      0x101a88  e5913008   ldr r3, [r1, #8]    -> o nCmd esta em +8
//      0x101a8c  e3530004   cmp r3, #4          -> e igual a MM_CMD_PLAY (1+3)
//      0x101aa0  e5913010   ldr r3, [r1, #16]   -> o nStatus esta em +16
//      0x101aa4  e2433001   sub r3, r3, #1
//      0x101aa8  e353000a   cmp r3, #10         -> cobre os status 1..11
//      0x101aac  979ff103   ldrls pc, [pc, r3, lsl #2]   -> salto por tabela
//
//    E os dois destinos dessa tabela foram lidos tambem, palavra a palavra:
//
//      status 2 (MM_STATUS_DONE) -> 0x0010352c: um tratador a serio
//          (0x10352c e5c62001 strb r2,[r6,#1] ...)
//      status 3 (MM_STATUS_ABORT) -> 0x001034b8:
//          0x1034b8 e2833028 add r3, r3, #0x28
//          0x1034bc e2400001 sub r0, r0, #1
//          0x1034c0 e5813000 str r3, [r1]
//          0x1034c4 e12fff1e bx  lr          -> traduz o status e VOLTA
//
//    Ou seja: neste titulo o DONE processa e o ABORT sai imediatamente. E por
//    isso que o `Stop` avisa com DONE (MM_STATUS_DONE = 2) e nao com ABORT.
//
// 3. OS CODIGOS DE RETORNO, de `platform/system/inc/AEEStdErr.h`:
//    AEE_SUCCESS 0, AEE_EFAILED 1, AEE_ENOMEMORY 2, AEE_ECLASSNOTSUPPORT 3,
//    AEE_EBADSTATE 13, AEE_EBADPARM 14, AEE_EUNSUPPORTED 20.
//
// ARMADILHA DECLARADA (a que ja foi apanhada uma vez neste projeto): existem
// DOIS enums de midia no SDK, `MM_STATUS_*` (o resultado de um pedido) e
// `MMD_*` (o TIPO DE DADOS -- `MMD_FILE_NAME` 0, `MMD_BUFFER` 1,
// `MMD_ISOURCE` = AEEIID_ISource). Trocar um pelo outro da um `SetMediaParm`
// que aceita um status como se fosse dados. E `0x01005505` NAO e MPEG4: e
// `AEECLSID_MEDIAMIDIOUTMSG` (`AEEClassIDs.h`: MULTIMEDIA = 0x01005500,
// MIDIOUTMSG = MULTIMEDIA+5, MPEG4 = MULTIMEDIA+7).

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core/audio/misturador.h"
#include "core/brew/interface.h"
#include "core/brew/vfs.h"
#include "core/cpu/cpu.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"
#include "tools/brew_slots.inc"

namespace zb2::brew {

// --- codigos de retorno (AEEStdErr.h, AEEError.h) ---------------------------
constexpr std::int32_t kAeeSucesso = 0;
constexpr std::int32_t kAeeFalhou = 1;
constexpr std::int32_t kAeeSemMemoria = 2;
constexpr std::int32_t kAeeClasseNaoSuportada = 3;
constexpr std::int32_t kAeeEstadoErrado = 13;
constexpr std::int32_t kAeeParametroErrado = 14;
constexpr std::int32_t kAeeNaoSuportado = 20;

// --- as classes da familia de midia (AEEClassIDs.h) -------------------------
// `AEECLSID_MULTIMEDIA` = QVERSION + 0x5500 = 0x01005500 (o proprio AEEIMedia.h
// o confirma: "AEEIID_IMedia 0x01005500 // This is AEECLSID_MEDIA").
constexpr std::uint32_t kClasseMultimidia = 0x01005500u;
constexpr std::uint32_t kUltimaClasseMultimidia = 0x01005514u;  // MULTIMEDIA + 20
constexpr std::uint32_t kIidMedia = kClasseMultimidia;          // AEEIID_IMedia

// --- comandos (MM_CMD_*, AEEIMedia.h) --------------------------------------
constexpr std::int32_t kMmCmdBase = 1;
constexpr std::int32_t kMmCmdSetMediaParm = kMmCmdBase + 1;  // 2
constexpr std::int32_t kMmCmdGetMediaParm = kMmCmdBase + 2;  // 3
constexpr std::int32_t kMmCmdPlay = kMmCmdBase + 3;          // 4  <- medido
constexpr std::int32_t kMmCmdRecord = kMmCmdBase + 4;        // 5
constexpr std::int32_t kMmCmdGetTotalTime = kMmCmdBase + 5;  // 6

// --- estados (MM_STATUS_*, AEEIMedia.h) ------------------------------------
constexpr std::int32_t kMmStatusBase = 1;
constexpr std::int32_t kMmStatusStart = kMmStatusBase;        // 1
constexpr std::int32_t kMmStatusDone = kMmStatusBase + 1;     // 2  <- medido
constexpr std::int32_t kMmStatusAbort = kMmStatusBase + 2;    // 3  <- medido
constexpr std::int32_t kMmStatusMediaSpec = kMmStatusBase + 3;
constexpr std::int32_t kMmStatusTickUpdate = kMmStatusBase + 4;
constexpr std::int32_t kMmStatusSeek = kMmStatusBase + 6;
constexpr std::int32_t kMmStatusPause = kMmStatusBase + 8;
constexpr std::int32_t kMmStatusResume = kMmStatusBase + 10;

// O nome do status, para o registo dizer o numero E o nome. Um log so com o
// numero ja obrigou, nesta sessao, a procurar a tabela a mao.
const char* NomeDoStatusDeMidia(std::int32_t status);
const char* NomeDoComandoDeMidia(std::int32_t cmd);

// --- parametros (MM_PARM_*, AEEIMedia.h) -----------------------------------
constexpr std::int32_t kMmParmBase = 1;
constexpr std::int32_t kMmParmMediaData = kMmParmBase;        // 1
constexpr std::int32_t kMmParmAudioDevice = kMmParmBase + 1;  // 2
constexpr std::int32_t kMmParmAudioPath = kMmParmBase + 2;    // 3
constexpr std::int32_t kMmParmVolume = kMmParmBase + 3;       // 4
constexpr std::int32_t kMmParmMute = kMmParmBase + 4;         // 5
constexpr std::int32_t kMmParmTempo = kMmParmBase + 5;        // 6
constexpr std::int32_t kMmParmTune = kMmParmBase + 6;         // 7
constexpr std::int32_t kMmParmPan = kMmParmBase + 7;          // 8
constexpr std::int32_t kMmParmTickTime = kMmParmBase + 8;     // 9
constexpr std::int32_t kMmParmRect = kMmParmBase + 9;         // 10
constexpr std::int32_t kMmParmPlayRepeat = kMmParmBase + 10;  // 11
constexpr std::int32_t kMmParmPos = kMmParmBase + 11;         // 12
constexpr std::int32_t kMmParmClsid = kMmParmBase + 12;       // 13
constexpr std::int32_t kMmParmCaps = kMmParmBase + 13;        // 14
constexpr std::int32_t kMmParmEnable = kMmParmBase + 14;      // 15
constexpr std::int32_t kMmParmChannelShare = kMmParmBase + 15;  // 16
constexpr std::int32_t kMmParmFrame = kMmParmBase + 16;         // 17
constexpr std::int32_t kMmParmReserved1 = kMmParmBase + 17;     // 18
constexpr std::int32_t kMmParmReserved2 = kMmParmBase + 18;     // 19
constexpr std::int32_t kMmParmSeekCaps = kMmParmBase + 19;      // 20
constexpr std::int32_t kMmParmRate = kMmParmBase + 20;          // 21
constexpr std::int32_t kMmParmPlayType = kMmParmBase + 21;      // 22
constexpr std::int32_t kMmParmAudioSync = kMmParmBase + 22;     // 23
constexpr std::int32_t kMmParmNotes = kMmParmBase + 23;         // 24

// --- estados da maquina (MM_STATE_*, AEEIMedia.h) --------------------------
constexpr std::int32_t kMmEstadoBase = 1;
constexpr std::int32_t kMmEstadoOcioso = kMmEstadoBase;        // 1 IDLE
constexpr std::int32_t kMmEstadoPronto = kMmEstadoBase + 1;    // 2 READY
constexpr std::int32_t kMmEstadoTocando = kMmEstadoBase + 2;   // 3 PLAY
constexpr std::int32_t kMmEstadoPausado = kMmEstadoBase + 4;   // 5 PLAY_PAUSE
constexpr std::int32_t kMmEstadoGravando = kMmEstadoBase + 3;  // 4 RECORD

// --- caminho de audio, capacidades, dados, seek ----------------------------
constexpr std::int32_t kMmCaminhoLocal = 1;       // MM_APATH_LOCAL
constexpr std::int32_t kMmCaminhoRinger = 2;      // MM_APATH_LOCAL_RINGER
constexpr std::int32_t kMmCaminhoRemoto = 3;      // MM_APATH_REMOTE
constexpr std::int32_t kMmCaminhoAmbos = 4;       // MM_APATH_BOTH
constexpr std::int32_t kMmMaxPan = 128;           // MM_MAX_PAN
constexpr std::uint32_t kMmCapsAudio = 0x00000001u;  // MM_CAPS_AUDIO
constexpr std::uint32_t kMmdNomeDeFicheiro = 0;     // MMD_FILE_NAME
constexpr std::uint32_t kMmdBuffer = 1;             // MMD_BUFFER
// MMD_ISOURCE = AEEIID_ISource, e `platform/deprecated/inc/AEEISource.h:40` diz
// `#define AEEIID_ISource 0x01001012`.
constexpr std::uint32_t kMmdFonte = 0x01001012u;
constexpr std::int32_t kMmSeekInicio = 0;           // MM_SEEK_START
constexpr std::int32_t kMmSeekFim = 1;              // MM_SEEK_END
constexpr std::int32_t kMmSeekActual = 2;           // MM_SEEK_CURRENT
constexpr std::int32_t kMmSeekModoTempo = 0x00;     // MM_SEEK_MODE_TIME
constexpr std::int32_t kMmSeekModoQuadro = 0x10;    // MM_SEEK_MODE_FRAME
constexpr std::int32_t kMmSeekModoCapitulo = 0x20;  // MM_SEEK_MODE_CHAPTER
constexpr std::int32_t kMmTipoNormal = 1;           // MM_PLAY_TYPE_NORMAL
constexpr std::int32_t kMmTipoRinger = 2;           // MM_PLAY_TYPE_RINGER

// --- `AEEMediaData` (AEEIMedia.h) ------------------------------------------
//   typedef struct AEEMediaData { AEECLSID clsData; void *pData; uint32 dwSize; }
constexpr std::uint32_t kOffMidiaClsData = 0;
constexpr std::uint32_t kOffMidiaPData = 4;
constexpr std::uint32_t kOffMidiaDwSize = 8;

// --- `AEEMediaCmdNotify` (AEEIMedia.h) ------------------------------------
//   clsMedia 0, pIMedia 4, nCmd 8, nSubCmd 12, nStatus 16, pCmdData 20, dwSize 24
// Os deslocamentos 8 e 16 sao os MEDIDOS no corpo do `cnk2` (ver o cabecalho
// deste ficheiro); os restantes veem da ordem dos campos do proprio cabecalho.
constexpr std::uint32_t kOffAvisoClsMedia = 0;
constexpr std::uint32_t kOffAvisoPIMedia = 4;
constexpr std::uint32_t kOffAvisoCmd = 8;
constexpr std::uint32_t kOffAvisoSubCmd = 12;
constexpr std::uint32_t kOffAvisoStatus = 16;
constexpr std::uint32_t kOffAvisoDados = 20;
constexpr std::uint32_t kOffAvisoTamanho = 24;
constexpr std::uint32_t kTamanhoDoAviso = 28;  // 7 campos de 32 bits

// --- a faixa de saida deste modulo -----------------------------------------
//
// Cada slot TEM de ter um endereco de saida proprio: um stub so para todos foi
// o defeito que deixou 86 377 `glCullFace` sem nome na arvore antiga.
//
// Estes indices sao indices na faixa de saida (o `Saidas` da CPU), e nao
// enderecos. As faixas vizinhas, para a colisao ser verificavel por leitura:
// 1000..1116 sao os ajudantes do `AEEHelperFuncs`, 2000.. sao os slots do
// `IShell`, 3000 a vtable dele, 6000 o `IDisplay`, 7000 o `IFileMgr`, 8000 o
// bitmap, 9000.. as interfaces genericas, 9500 o ficheiro. 4000 e 4100 estao
// livres, e os `static_assert` abaixo provam-no no momento da compilacao.
constexpr std::uint32_t kBaseDoMedia = 4000;
constexpr std::uint32_t kVtableDoMedia = 4100;
constexpr std::uint32_t kSlotsDoMedia = 14;  // 3 da IQI + 11 metodos

static_assert(kBaseDoMedia >= 1117, "as saidas do media nao podem invadir os ajudantes");
static_assert(kBaseDoMedia + kSlotsDoMedia <= kVtableDoMedia,
              "as saidas do media nao podem invadir a propria vtable");
static_assert(kVtableDoMedia + kSlotsPorVtable <= 6000,
              "a vtable do media nao pode invadir o IDisplay");

// --- IMediaUtil: a SEGUNDA interface da familia `AEECLSID_MULTIMEDIA` -------
//
// O QUE ESTA AQUI E UMA MEDICAO, e nao uma leitura de cabecalho. O `allstarcards`
// (`mod/280173`) cria o `AEECLSID_MEDIAUTIL` (0x0100550d) pelo slot 2 do `IShell`
// e chama o SLOT 3 do objecto que recebe. Desmonte do proprio modulo (base 0):
//
//   0000b9cc  add  r2, r4, #4       ; r2 = &[r4+4], o `ppo` do CreateInstance
//   0000b9d0  ldr  r0, [r0, #12]    ; o objecto em [r4+0] + 12 = a IShell
//   0000b9d4  ldr  r1, [r0]         ; a vtable da IShell
//   0000b9d8  ldr  r3, [r1, #8]     ; slot 2 = IShell::CreateInstance
//   0000b9dc  ldr  r1, [pc, #216]   ; 0x0100550d = AEECLSID_MEDIAUTIL
//   0000b9e0  blx  r3
//   ...
//   0000f388  ldr  r0, [r8]         ; r8 = arg2 de 0xf2d0, o objecto criado acima
//   0000f38c  add  r2, r4, #28      ; <- o `ppm` QUE A CHAMADA TEM DE ESCREVER
//   0000f390  add  r1, sp, #4       ; o `AEEMediaData` da pilha (clsData = 1 = MMD_BUFFER)
//   0000f394  ldr  r3, [r0, #12]    ; o SLOT 3 da vtable DELE
//   0000f398  mov  r0, r8           ; this = o mesmo objecto
//   0000f39c  blx  r3
//   0000f3a0  cmp  r0, #0 / beq 0xf3e4   ; SUCCESS -> o jogo usa o `[r4+28]`
//
// O SLOT 3 DE UM `IMedia` E O `RegisterNotify` (`kMedia_RegisterNotify == 3`): era
// essa a vtable que o `Criar` dava ao `0x0100550d`, logo o slot 3 respondia
// SUCCESS e NAO escrevia o `ppm`. O `[r4+28]` ficava a ZERO, o jogo fazia
// `ldr r2,[r0]` com r0 = 0, tomava a PRIMEIRA PALAVRA DO PROPRIO MODULO
// (0xea00000e, uma instrucao ARM) por vtable e chamava `[r2,#12]` = 0xea00001a:
// **53x em pc=0xf3f4 + 53x em pc=0xf428 = 106 recusas de CPU por corrida, 2 por
// quadro** (`/tmp/pesquisa/setup.md`, que mediu a cadeia).
//
// OS SLOTS, de `AEEINTERFACE(IMediaUtil)` em `sdk/inc/AEEMediaUtil.h`:
//   AddRef 0 | Release 1 | QueryInterface 2 |
//   CreateMedia(po, AEEMediaData *pmd, IMedia **ppm)        3
//   EncodeMedia(po, AEEMediaEncodeResult *, AEECLSID, ...)  4
//   CreateMediaEx(po, AEEMediaCreateInfo *pcmi, IMedia **)  5
//
// ARMADILHA DECLARADA, e a razao de os numeros estarem escritos por extenso: o
// `EncodeMedia` ocupa o SLOT 4, logo o `CreateMediaEx` e o 5 e NAO o 4. Havia um
// pedido que dizia "4 slots: IQI + CreateMedia + CreateMediaEx (slot 4)"; o
// cabecalho do SDK diz 6 slots e o `zeebx` concorda com o cabecalho
// (`/tmp/zx-new/src/aee_slots.rs`: `MEDIAUTIL` = AddRef, Release, QueryInterface,
// CreateMedia, EncodeMedia, CreateMediaEx). Com o `CreateMediaEx` no 4, um jogo
// que chamasse o 4 receberia o `CreateMediaEx` no lugar do `EncodeMedia` -- e o
// NOME no registo de faltas mentiria sobre o que o jogo pediu.
// O UNICO CLSID DESTA FAMILIA cujo `MMD_BUFFER` traz AMOSTRAS. Todos os outros
// (`AEECLSID_MEDIAMP3` 0x01005502, `MEDIAADPCM` 0x0100550a, `MEDIAAAC`,
// `MEDIAMPEG4`, ...) trazem um FLUXO COMPRIMIDO, e nele um tamanho IMPAR de
// bytes e o caso normal -- nao um defeito do jogo. MEDIDO no `a3d`: o jogo abre
// `a3d_sound_bgm_00.mp3`, cria um `AEECLSID_MEDIAMP3` e entrega-lhe o ficheiro
// inteiro (150352 bytes; a segunda faixa, 85261). Ver `Media::DefinirDados`.
constexpr std::uint32_t kClsMediaPcm = 0x01005511u;  // AEECLSID_MEDIAPCM
static_assert(kClsMediaPcm == kClasseMultimidia + 17u,
              "AEECLSID_MEDIAPCM = AEECLSID_MULTIMEDIA + 17 (AEEClassIDs.h)");
constexpr std::uint32_t kClsMediaUtil = 0x0100550du;  // AEECLSID_MEDIAUTIL
static_assert(kClsMediaUtil == kClasseMultimidia + 13u,
              "AEECLSID_MEDIAUTIL = AEECLSID_MULTIMEDIA + 13 (AEEClassIDs.h:340)");
constexpr std::uint32_t kSlotsDoMediaUtil = 6;  // 3 da IQI + 3 metodos
constexpr std::uint32_t kMediaUtil_CreateMedia = 3;
constexpr std::uint32_t kMediaUtil_EncodeMedia = 4;
constexpr std::uint32_t kMediaUtil_CreateMediaEx = 5;
static_assert(kMediaUtil_CreateMediaEx == kSlotsDoMediaUtil - 1,
              "o CreateMediaEx e o ULTIMO dos seis slots, e nao o quarto");

// A FAIXA DE SAIDA do IMediaUtil, ao lado da do IMedia. O 4200 e o primeiro
// numero livre acima da pagina da vtable do IMedia (4100 + kSlotsPorVtable =
// 4164), e o 4300 e a pagina da vtable dele. O intervalo 4164..6000 estava
// LIVRE no mapa das saidas -- e por isso que se pode usar sem deslocar ninguem.
constexpr std::uint32_t kBaseDoMediaUtil = 4200;
constexpr std::uint32_t kVtableDoMediaUtil = 4300;
static_assert(kBaseDoMediaUtil >= kVtableDoMedia + kSlotsPorVtable,
              "as saidas do IMediaUtil nao podem cair na pagina da vtable do IMedia");
static_assert(kBaseDoMediaUtil + kSlotsDoMediaUtil <= kVtableDoMediaUtil,
              "as saidas do IMediaUtil nao podem invadir a propria vtable");
static_assert(kVtableDoMediaUtil + kSlotsPorVtable <= 6000,
              "a vtable do IMediaUtil nao pode invadir o IDisplay");

// --- `AEEMediaCreateInfo`, o argumento do `CreateMediaEx` -------------------
//   typedef struct AEEMediaCreateInfo {
//     uint32          dwStructSize;   // o tamanho desta struct (versionamento)
//     int             nState;         // MM_STATE_IDLE (1) ou MM_STATE_READY (2)
//     int             nCount;         // quantos elementos tem a pmdList
//     AEEMediaDataEx *pmdList;
//     int            *pnCapsList;
//   } (`AEEMediaUtil.h`)
//
// NAO SE COPIA A DOC: o bloco de documentacao do MESMO cabecalho escreve
// `int16 nState; int16 nCount;` e a STRUCT declara `int` nos dois (32 bits). Vale
// a struct -- e o unico dos dois que o compilador do SDK compila, e um campo de
// 16 bits lido como 32 daria um estado que o jogo nunca pediu.
//
// O PRIMEIRO ELEMENTO de um `AEEMediaDataEx` tem os MESMOS tres campos de um
// `AEEMediaData` (`clsData`, `pData`, `dwSize`; `AEEIMedia.h:287-291`), e e por
// isso que o `DefinirDados` o le SEM uma segunda tabela de deslocamentos -- duas
// tabelas que tem de concordar sao zero tabelas.
constexpr std::uint32_t kOffCmiTamanho = 0;
constexpr std::uint32_t kOffCmiEstado = 4;
constexpr std::uint32_t kOffCmiQuantos = 8;
constexpr std::uint32_t kOffCmiLista = 12;
constexpr std::uint32_t kOffCmiCaps = 16;
constexpr std::uint32_t kTamanhoDoCmi = 20;  // 5 campos de 32 bits

// --- A REGIAO DE MEMORIA DESTE MODULO, declarada ---------------------------
//
// Os objectos de midia. As bases usadas pelas outras interfaces vao de
// 0x80020000 a 0x80070000; a primeira livre e 0x80090000. O mapa dos enderecos
// de objecto deste emulador, por ordem (`core/brew/igl.h`, onde ele foi escrito
// ao decidir-se para onde ia o IGL): 0x80010000 ajudantes | 0x80020000 IShell |
// 0x80030000 IDisplay | 0x80040000 IFileMgr | 0x80050000 DIB | 0x80060000
// genericos | 0x80070000 ficheiros | 0x80080000 pilha | **0x80090000 IMedia** |
// **0x800A0000 avisos de midia** | 0x800B0000 IGL.
constexpr std::uint32_t kObjMediaBase = 0x80090000u;
constexpr std::uint32_t kPassoDoObjetoMedia = 0x40;

// REGIAO AUXILIAR, declarada: onde o aviso (`AEEMediaCmdNotify`) e os dados que
// ele aponta sao escritos na memoria do guest ANTES de o callback ser chamado.
// Nao se escreve na pilha do jogo: o `pCmdNotify` que o callback recebe e nosso.
//
// UM BLOCO DE AVISO POR OBJECTO, e por isso o numero de objectos manda no
// tamanho desta regiao. Com o teto de 1024 objectos (abaixo) sao
// 1024 * 28 = 28 672 bytes de avisos, e a seguir os dados (4 bytes por objecto).
// O `kAvisoDadosBase` SUBIU de 0x800A1000 para 0x800A8000 por isso mesmo, e o
// comentario do `igl.h` que cita o valor antigo passou a estar desactualizado:
// o `igl.h` NAO foi tocado (e de outra frente) e quem conta e o valor daqui. O
// que NAO mudou e o que importa a quem vem depois: o `kObjIgl` continua em
// 0x800B0000, e a regiao daqui acaba antes dele -- provado pelos tres
// `static_assert` no topo do `imedia.cpp`, e pelo teste
// `Media.ARegiaoDeMidiaNaoInvadeAQuemVemADepoisDela`.
constexpr std::uint32_t kAvisoBase = 0x800A0000u;
constexpr std::uint32_t kAvisoDadosBase = 0x800A8000u;

// O FIM DA REGIAO DESTE MODULO: onde nasce o modulo seguinte
// (`core/brew/igl.h`, `kObjIgl = 0x800B0000u`, "o primeiro bloco livre").
constexpr std::uint32_t kFimDaRegiaoDeMidia = 0x800B0000u;

// QUANTOS OBJECTOS DE MIDIA CABEM AQUI -- E, MAIS IMPORTANTE, DE ONDE VEM O
// NUMERO. Ele **nao** vem do SDK: o SDK nao limita quantos objectos de midia
// podem existir ao mesmo tempo. O que ele limita e a REPRODUCAO SIMULTANEA (as
// vozes), em `doc/AEEMedia.txt:785-791` ("IMedia - Simultaneous media
// playback"):
//
//   "The device capabilities enforce certain restrictions on number and type of
//    simultaneous media. / For example, Qualcomm MSM-based devices can typically
//    simultaneously play the following media sets:  * 1 MIDI / MMF / PMD(with
//    MIDI) + 4 QCP(fixed) / AMR / ADPCM (all 4 of same type) ... * [Using 6550
//    and above] 4 MIDI / MMF / PMD(with MIDI) + 4 QCP(fixed) / AMR / ADPCM"
//
// Isso e quantas midias TOCAM ao mesmo tempo. Nesta arvore nao ha descodificador
// nem vozes -- o `Avancar` soma as amostras no misturador e CONTA-as --, logo
// aquele numero nao decide nada aqui, e cita-lo como se decidisse seria inventar
// proveniencia.
//
// O que decide e a REGIAO: os objectos vivem em `[kObjMediaBase, kAvisoBase)` a
// `kPassoDoObjetoMedia`, e o aviso de cada um tem de caber entre `kAvisoBase` e
// `kAvisoDadosBase`. O conjunto CRESCE a pedido ate este maximo (era um conjunto
// FIXO de 16 lugares, e o 17.o pedido respondia "sem memoria": a medicao que
// obrigou a esta mudanca esta no `Criar`).
constexpr std::uint32_t kMaxObjetosDeMidia =
    (kAvisoBase - kObjMediaBase) / kPassoDoObjetoMedia;  // 1024
static_assert(kMaxObjetosDeMidia == 1024,
              "a regiao dos objectos da 1024 enderecos de 0x40 (0x80090000..0x800A0000)");

// --- o cabecalho do objecto (medido em bytes, 32) ---------------------------
constexpr std::uint32_t kOffObjVtable = 0;
constexpr std::uint32_t kOffObjRefs = 4;
constexpr std::uint32_t kOffObjEstado = 8;
constexpr std::uint32_t kOffObjClasse = 12;
constexpr std::uint32_t kOffObjAvisoFn = 16;
constexpr std::uint32_t kOffObjAvisoUsuario = 20;
constexpr std::uint32_t kOffObjAmostrasTotal = 24;
constexpr std::uint32_t kOffObjPosicao = 28;
constexpr std::uint32_t kTamanhoDoCabecalho = 32;

// O bit de "pronto" que a arvore ANTIGA mediu no `ddragonz` (`obj+0x25`, byte
// que o tratador do jogo exige nao nulo depois de um `Play` bem sucedido). NAO
// foi re-medido nesta arvore -- esta aqui marcado como PISTA, e o teste que o
// cobre diz isso. Custo se estiver errado: um byte escrito num objecto por
// ninguem ler.
constexpr std::uint32_t kOffObjProntoMedidoNoDdragonz = 0x25;

// Como o `SetMediaParm` trata cada parametro. E UMA tabela so, que serve o
// `Set`, o `Get` e o teste: duas listas que tem de concordar sao zero listas, e
// foi assim que uma implementacao ja desapareceu neste projeto.
enum class TratamentoDeParametro {
  // Muda o som ou a contagem: e aplicado de verdade.
  Aplicado,
  // Guardado e devolvido igual pelo `Get`, mas NAO aplicado ao som. Cada
  // aceitacao EMITE um evento de traco com o nome do parametro -- aceitar e
  // guardar em silencio seria o stub que o P2 proibe.
  Guardado,
  // O SDK declara este parametro so de leitura: um `Set` RECUSA em voz alta.
  SoDeLeitura,
  // Sem implementacao nesta arvore: recusa em voz alta e fica contado.
  Recusado,
};

// DUAS COLUNAS, porque o SDK trata os dois sentidos de forma diferente: ha
// parametros so de leitura (`MM_PARM_CLSID`, `MM_PARM_CAPS`,
// `MM_PARM_SEEK_CAPS`), e um `Set` neles tem de recusar.
struct ParametroDeMidia {
  std::int32_t id;
  const char* nome;
  TratamentoDeParametro no_set;
  TratamentoDeParametro no_get;
};

// A tabela, declarada uma vez.
extern const ParametroDeMidia kParametrosDeMidia[];
extern const std::size_t kQuantosParametrosDeMidia;

// O tratamento de um id de parametro; `nullptr` se o id nao existe no SDK.
const ParametroDeMidia* ParametroPorId(std::int32_t id);

// Uma linha da TABELA DECLARADA da vtable do IMedia: o slot e o nome que o SDK
// lhe da.
struct SlotDoMedia {
  std::uint32_t slot;
  const char* nome;
};

// A TABELA DECLARADA: uma so, que serve os nomes do registo, o que o `Instalar`
// escreve, e a conferencia de que todos os slots estao preenchidos.
extern const SlotDoMedia kTabelaDeSlotsDoMedia[];
extern const std::size_t kQuantosSlotsDoMediaDeclarados;

// A TABELA DECLARADA DO IMEDIAUTIL, a par da do IMedia e pela mesma razao: e UMA
// lista que serve os nomes do registo, o que o `Instalar` escreve, e a
// conferencia de que os seis slots estao preenchidos. Duas listas que tem de
// concordar sao zero listas.
extern const SlotDoMedia kTabelaDeSlotsDoMediaUtil[];
extern const std::size_t kQuantosSlotsDoMediaUtilDeclarados;

// A GUARDA, isolada do `Instalar` de proposito: assim o teste pode provar por
// VIOLACAO que uma tabela com um slot por preencher -- ou com um slot repetido
// -- e RECUSADA. Uma guarda que passa sem a mudanca nao e guarda.
ResultadoCablagem ConferirTabelaDeSlots(const SlotDoMedia* slots, std::size_t quantas,
                                        std::uint32_t quantos_slots);

// O CLSID pertence a familia `AEECLSID_MULTIMEDIA` (0x01005500..0x01005514)?
//
// E esta a pergunta que o despacho do motor tem de fazer ANTES de chamar
// `Media::Criar`: uma funcao so, para nao haver duas ideias de "o que e midia".
bool ClasseDeMidia(std::uint32_t cls);
const char* NomeDaClasseDeMidia(std::uint32_t cls);

// A interface IMedia, como o guest a ve: um objecto ROPI cuja vtable cabe na
// faixa de saida, mais o servico de avisos.
//
// QUEM CHAMA ISTO: o despacho do motor (`core/brew/despacho.cpp`), no caso em
// que o guest pede um CLSID da familia de midia ao `IShell::CreateInstance`, e
// no laco, para os indices de saida desta faixa. O `tools/sonda_media.cpp` faz
// o mesmo com o mesmo CPU, e e o comando que fica VERMELHO enquanto o motor nao
// conhecer o `AEECLSID_MEDIA`.
class Media {
 public:
  Media(Memoria& mem, Traco& traco, const Saidas& saidas,
        audio::Misturador& misturador, Vfs* vfs = nullptr);

  // Escreve a vtable do IMedia na faixa de saida e CONFIRMA-A com leitura de
  // volta. Uma tabela com um slot por preencher RECUSA e diz qual (P2): uma
  // vtable incompleta instalada em silencio foi exatamente o defeito dos 86 377
  // `glCullFace` descartados.
  ResultadoCablagem Instalar();

  // `IShell::CreateInstance(po, ClsId, void **ppobj)` para a familia de midia.
  // Escreve `*ppobj` e devolve o codigo do SDK. Um CLSID fora da familia NAO
  // escreve nada e devolve `AEE_ECLASSNOTSUPPORT` (recusar, nao mentir).
  std::int32_t Criar(std::uint32_t cls, std::uint32_t pponovo);

  // O ponto por onde o laco entrega os indices de saida desta faixa. Devolve
  // `false` quando o indice NAO e do IMedia -- e nesse caso nada foi tocado.
  bool Atender(std::uint32_t indice, ICpu& cpu);

  // O MESMO ponto para a faixa do IMediaUtil (indices `kBaseDoMediaUtil..`).
  //
  // E um metodo proprio e nao um ramo escondido dentro do `Atender` porque o
  // INDICE ja diz a interface E o slot: 4003 e `IMedia::RegisterNotify` e 4203 e
  // `IMediaUtil::CreateMedia`, embora os dois sejam "o slot 3". Quem le o codigo
  // ve as duas faixas; quem le o registo ve o nome certo.
  bool AtenderMediaUtil(std::uint32_t indice, ICpu& cpu);

  // Quantos IMediaUtil estao vivos AGORA. E o MESMO conjunto dos IMedia (um lugar
  // por objecto, servido pelo `Criar` e devolvido pelo `Release`), logo este
  // numero e do conjunto e nao de um segundo registo.
  std::uint32_t ObjetosDeMediaUtilVivos() const;

  // O `AEECLSID` com que o objecto foi criado (0 se nao existe). E por ele que
  // cada handler sabe se o `this` que recebeu e um IMedia ou um IMediaUtil: os
  // dois vivem na mesma regiao e a mesma vtable os distinguiria mal.
  std::int32_t ClasseDe(std::uint32_t objeto) const;

  // O laco avanca o tempo EM AMOSTRAS (injectadas, P4). As amostras que cada
  // voz consome passam pelo misturador, que as CONTA, e uma voz que chega ao
  // fim avisa UMA vez.
  void Avancar(std::uint32_t amostras);

  // --- os avisos ----------------------------------------------------------
  //
  // Avisos sao ENTREGUES, nunca chamados de dentro do handler. Chamar o callback
  // do guest no meio de uma chamada do guest reentra no codigo do jogo com o
  // quadro dele ainda por fechar -- foi uma medicao da arvore antiga
  // (`MediaHle::Tick`, comentario do `pending_notifications_`), e a razao de a
  // entrega ser ADIADA para uma fila.
  //
  // ENTRE O NASCIMENTO E A ENTREGA O GUEST CONTINUA A CORRER, e por isso o aviso
  // tem de levar a IDENTIDADE do objecto, e nao so o endereco. As duas metades da
  // regra vem de FORA desta arvore, uma de cada vez:
  //
  // - O AVISO NAO SOME COM O `Release`. O `zeebx` mediu-o no Zeebo F.C. Super
  //   League (`src/machine/media.rs:556`): o jogo para o som, solta o objecto e
  //   espera o `DONE` desse som -- descartado junto com o objecto, a abertura
  //   parava na tela de aviso. E a razao de o `fn` e o `pUser` serem congelados
  //   no NASCIMENTO do aviso, e nao lidos do objecto na altura da entrega.
  // - O AVISO NAO E ENTREGUE COM O ENDERECO DE OUTRO OBJECTO. O proprio SDK diz
  //   que o jogo correla o aviso pelo `IMedia *` que vem dentro dele
  //   (`AEEIMedia.h`, "Callback Events": "You can correlate using either the
  //   IMedia pointer or class ID returned in the callback data"). O endereco de
  //   um objecto soltado VOLTA a ser entregue (e o que o recolhedor faz aqui, e
  //   o que a arvore antiga fazia com a `generation` do `MediaHle`), logo um
  //   aviso que chegue depois disso faria o jogo ler o `DONE` de um som como se
  //   fosse de outro. Nesse caso o aviso e DESCARTADO e a recusa fica REGISTADA
  //   com o nome (P2) -- nunca entregue com a identidade errada.
  struct Aviso {
    std::uint32_t objeto = 0;   // IMedia *  (o `pIMedia` do aviso)
    // A IDENTIDADE do objecto quando o aviso NASCEU. Nao e o endereco outra vez:
    // o endereco diz ONDE o objecto esta, e a serie diz QUEM ele e -- e entre o
    // nascimento e a entrega o endereco pode mudar de dono (ver a regra 4 da
    // entrega, abaixo).
    std::uint32_t serie = 0;
    std::int32_t comando = 0;
    std::int32_t sub_comando = 0;
    std::int32_t status = 0;
    std::uint32_t dados = 0;    // `pCmdData` (endereco no guest)
    std::uint32_t tamanho = 0;  // `dwSize`
    std::uint32_t fn = 0;       // `PFNMEDIANOTIFY` registado pelo jogo
    std::uint32_t usuario = 0;  // o `pUser` do jogo
    std::uint32_t endereco = 0; // onde o aviso ja esta escrito na memoria
  };

  // Tira UM aviso da fila, SEM o entregar. Existe para quem queira ver o pedido
  // sem correr codigo do guest.
  bool RetirarAviso(Aviso* saida);
  std::size_t AvisosPendentes() const { return fila_.size(); }
  std::uint64_t AvisosEmitidos() const { return avisos_emitidos_; }
  // Os avisos que morreram na fila porque o endereco ja era de OUTRO objecto
  // (regra 4 da entrega). Fecha a conta, e por isso um aviso que desaparece
  // nunca se confunde com um aviso que nunca nasceu:
  //   emitidos == entregues + nao entregues + descartados
  std::uint64_t AvisosDescartados() const { return avisos_descartados_; }

  // --- a ENTREGA do aviso ao guest ---------------------------------------
  //
  // Leva UMA notificacao pendente ao callback do jogo, correndo-o. E a operacao
  // mais delicada do modulo, e cada regra tem uma medicao por tras:
  //
  // 1. OS 16 REGISTRADORES E O CPSR SAO GUARDADOS E REPOSTOS.
  //    O callback e codigo do guest chamado a partir do laco, e no momento da
  //    entrega o guest tem registradores VIVOS -- pode estar a meio de um
  //    quadro. A arvore antiga tem esta primitiva medida
  //    (`HleRuntime::CallArmFunctionPreservingContext`, core/brew/hle_runtime.cpp):
  //    guarda `array<uint32_t,16>` + CPSR, chama com o LR na sentinela, corre ate
  //    o PC voltar a sentinela, e repoe os dois.
  //
  // 2. O PC E REPOSTO A MAO. Sem isso, o laco seguinte volta a ler a sentinela e
  //    encerra a fase: foi o que aconteceu na primeira sonda desta etapa, que
  //    usou `0xEEEE0000` como sentinela e o laco respondeu `saiu_do_modulo`
  //    (a sentinela do despacho e `0xFFFFFFF0`). Com o PC reposto, a sentinela
  //    tem UM significado so para quem chama.
  //
  // 3. UM CALLBACK QUE NAO VOLTA E REGISTADO, e nao dado como bom (P2). Se o
  //    callback nao regressar dentro de `limite_de_passos`, fica a falta com o
  //    endereco do callback e o pedido que a provocou -- e o emulador CONTINUA,
  //    em vez de girar para sempre.
  //
  // 4. O AVISO LEVA A IDENTIDADE QUE TINHA QUANDO NASCEU (`Aviso::serie`), e a
  //    entrega compara-a com o dono do endereco AGORA. Os tres casos, e os tres
  //    sao diferentes:
  //      - o objecto ainda e o mesmo                    -> ENTREGA;
  //      - foi soltado e o endereco ainda nao tem dono   -> ENTREGA (ver a regra
  //        do `zeebx` no comentario de `Aviso`: o jogo pode estar a espera deste
  //        `DONE` depois de parar e soltar);
  //      - o endereco JA E DE OUTRO objecto              -> DESCARTA e REGISTA o
  //        motivo, com a serie dos dois. Entregar aqui seria dizer ao jogo que o
  //        evento de um som era de outro.
  //
  // `sentinela` e o endereco de retorno do laco do motor (`Despacho`: 0xFFFFFFF0).
  // Devolve `true` se tirou um aviso da fila (mesmo que a entrega tenha falhado).
  bool EntregarAviso(ICpu& cpu, std::uint32_t sentinela, std::uint64_t limite_de_passos);

  // Quantos callbacks do jogo correram de verdade, e quantos NAO voltaram.
  std::uint64_t AvisosEntregues() const { return avisos_entregues_; }
  std::uint64_t AvisosNaoEntregues() const { return avisos_nao_entregues_; }

  // --- o que a medicao le ------------------------------------------------
  const Saidas& SaidasDaBancada() const { return saidas_; }
  std::uint32_t ObjetosVivos() const;
  std::uint32_t PedidosAceitos() const { return pedidos_aceitos_; }
  std::uint32_t PedidosRecusados() const { return pedidos_recusados_; }
  std::int32_t EstadoDe(std::uint32_t objeto) const;
  std::uint32_t TamanhoDoFluxoGuardado(std::uint32_t objeto) const;
  const std::map<std::uint32_t, std::uint32_t>& ClassesPedidas() const { return classes_pedidas_; }
  const std::string& UltimoMotivoDeRecusa() const { return ultimo_motivo_; }

  // A taxa DECLARADA: nao ha descodificador nesta arvore, logo o tempo total de
  // uma midia sai de um numero declarado, e nao de um cabecalho lido. O valor
  // esta aqui, com este comentario, para nao parecer medido.
  static constexpr std::uint32_t kTaxaDeclarada = 22050;
  // Quantas amostras o laco avanca por milissegundo do relogio VIRTUAL (P4): a
  // taxa declarada dividida por 1000, TRUNCADA. O truncamento (22,05 -> 22) anda
  // 0,2% devagar, e isso esta DITO: um numero redondo inventado seria pior do que
  // um numero truncado e explicado.
  static constexpr std::uint32_t kAmostrasPorMs = kTaxaDeclarada / 1000;

 private:
  struct Objeto {
    bool vivo = false;
    std::uint32_t endereco = 0;
    // A IDENTIDADE DO OBJECTO, e nao o endereco: um numero que so sobe, dado no
    // `Criar`. Existe porque o endereco de um objecto soltado volta a ser
    // entregue a outro, e um aviso guardado tem de poder ser validado contra o
    // dono do endereco na altura da entrega (`Aviso::serie`).
    std::uint32_t serie = 0;
    std::int32_t estado = kMmEstadoOcioso;
    std::uint32_t fn = 0;
    std::uint32_t usuario = 0;
    std::vector<std::int16_t> amostras;  // o PCM entregue por MM_PARM_MEDIA_DATA
    // O FLUXO COMPRIMIDO lido do VFS (`MMD_FILE_NAME`) ou entregue pelo guest
    // (`MMD_BUFFER` de descodificador): bytes opacos, SEM descodificador nesta
    // arvore. Existe para o `GetMediaParm` os poder devolver um dia; hoje o que
    // ele prova e que o pedido foi ACEITE (e nao apenas recusado em voz alta).
    std::vector<std::uint8_t> fluxo;
    bool tem_dados = false;
    // O ESTADO E O UNICO GUARDA DA REPRODUCAO EM CURSO. Nao ha um `tocando` ao
    // lado dele: duas variaveis que tem de concordar sao zero variaveis, e neste
    // projeto ja se perderam duas listas que tinham de concordar.
    std::size_t posicao = 0;
    std::uint32_t volume = audio::kVolumeMaximo;
    std::uint32_t pan = kMmMaxPan / 2;  // "Balanced - default" (AEEIMedia.h)
    bool mudo = false;
    std::int32_t repetir = 1;  // MM_PARM_PLAY_REPEAT: 1 = uma vez (por omissao)
    std::int32_t cls_data = 0; // o `clsData` do ultimo AEEMediaData
    std::uint32_t p_data = 0;
    std::uint32_t tam_data = 0;
    // Os parametros "guardados", por id: o `Get` devolve o que o `Set` guardou.
    std::map<std::int32_t, std::int32_t> guardados;
    std::uint32_t classe = 0;  // o AEECLSID com que o `Criar` o criou
  };

  // Escreve UMA vtable a partir da tabela declarada e CONFIRMA-A com leitura de
  // volta. Os slots 0 e 1 sao a IBase, que e do MOTOR (saidas 3 e 4), e nao
  // metodos desta interface. Devolve o motivo quando a leitura nao bate com o
  // que se acabou de escrever -- uma cablagem conferida contra si propria ja
  // passou verde uma vez nesta arvore e nao servia para nada.
  ResultadoCablagem EscreverVtable(const SlotDoMedia* tabela, std::size_t quantas,
                                   std::uint32_t quantos_slots, std::uint32_t base,
                                   std::uint32_t vt);

  std::int32_t HandlerDeSlot(std::uint32_t slot, std::uint32_t objeto, ICpu& cpu);
  std::int32_t HandlerDoSlotDoMediaUtil(std::uint32_t slot, std::uint32_t objeto, ICpu& cpu);

  // `IMediaUtil::CreateMedia(po, AEEMediaData *pmd, IMedia **ppm)`: cria um
  // `IMedia` NOVO no conjunto deste modulo, LE o `AEEMediaData` para o estado
  // dele e ESCREVE-o no `ppm`. E a receita que o `zeebx` mediu
  // (`/tmp/zx-new/src/machine.rs:7257`, `media_call`): cria, le os dados,
  // `if out != 0 { write_u32(out, media) }`, SUCCESS.
  //
  // O `ppm` E O QUE ESTAVA EM FALTA NO NOSSO EMULADOR, e nao um detalhe: sem ele
  // o jogo fica com o ponteiro NULL e chama pela primeira palavra do proprio
  // modulo (ver o cabecalho, a medicao das 106 recusas).
  std::int32_t CreateMediaPeloUtil(std::uint32_t util, std::uint32_t pmd, std::uint32_t ppm);

  // `IMediaUtil::CreateMediaEx(po, AEEMediaCreateInfo *pcmi, IMedia **ppm)`.
  // Le a `AEEMediaCreateInfo` do guest: o `dwStructSize`, o `nState`, o `nCount`
  // e a lista. UM so elemento e implementado (o `AEEMediaDataEx` tem os mesmos
  // tres campos de cabeca de um `AEEMediaData`); mais do que um RECUSA em voz
  // alta, porque misturar fontes nao esta implementado nesta arvore.
  std::int32_t CreateMediaExPeloUtil(std::uint32_t util, std::uint32_t pcmi, std::uint32_t ppm);

  // O lugar do conjunto onde o `endereco` vive, so se ele for um IMediaUtil vivo.
  // `nullptr` caso contrario -- e quem chama RECUSA com o nome.
  Objeto* UtilPorEndereco(std::uint32_t objeto);

  // O CAMINHO COMUM do `CreateMedia` e do `CreateMediaEx`: cria um `IMedia` novo,
  // escreve-o no `ppm` (ou poe o `ppm` a ZERO, em qualquer falha) e, quando
  // `definir_dados`, le o `AEEMediaData`/`AEEMediaDataEx` apontado por `pmd` para
  // o estado dele. UM so sitio escreve o `ppm`: foi um ponteiro por escrever que
  // custou as 106 recusas, e um segundo caminho a faze-lo e a maneira de isso
  // voltar.
  std::int32_t CriarMediaComDados(std::uint32_t pmd, std::uint32_t ppm, bool definir_dados,
                                  const char* quem_chama);
  // O endereco do BLOCO DE AVISO do objecto e o dos dados que ele aponta
  // (`pCmdData`, 4 bytes por objecto). UMA conta, para os dois sitios que dela
  // dependem (`EmitirAviso` e `GetTotalTime`).
  //
  // Devolve `false` -- sem escrever nada -- quando o objecto nao esta na regiao
  // de midia: um indice fora dela escreveria o aviso de um objecto em cima do
  // bloco de outro, e o jogo leria o `DONE` de um som como se fosse de outro.
  bool EnderecoDoAviso(const Objeto& o, std::uint32_t* aviso, std::uint32_t* dados);
  // `MM_PARM_MEDIA_DATA`: le o `AEEMediaData` do guest e decide. Aceita bufer de
  // PCM; RECUSA o nome de ficheiro (nao ha descodificador nesta arvore) e a
  // fonte `MMD_ISOURCE`, os dois com o motivo escrito.
  std::int32_t DefinirDados(Objeto& o, std::int32_t p1, std::int32_t p2);
  // Faixa de cada parametro, do SDK: volume 0..100 (`AEE_MAX_VOLUME`), pan
  // 0..128 (`MM_MAX_PAN`), tune na lista do cabecalho, mute 0/1. Um valor fora
  // da faixa RECUSA com `AEE_EBADPARM`, e nao e guardado.
  static bool FaixaValida(std::int32_t id, std::int32_t p1);
  Objeto* PorEndereco(std::uint32_t objeto);
  const Objeto* PorEndereco(std::uint32_t objeto) const;
  void EmitirAviso(Objeto& o, std::int32_t comando, std::int32_t sub, std::int32_t status,
                   std::uint32_t dados, std::uint32_t tamanho);
  void RecolherLibertados();
  void Recusar(const std::string& o_que, const std::string& porque);

  Memoria& mem_;
  Traco& traco_;
  Saidas saidas_;
  audio::Misturador& misturador_;
  Vfs* vfs_;
  // O CONJUNTO DE OBJECTOS. Cresce a pedido, ate `kMaxObjetosDeMidia`, e um lugar
  // devolvido pelo `Release` (`RecolherLibertados`) volta a ser servido com o
  // MESMO endereco -- o lugar e do indice, e nao um objecto que anda de lugar.
  std::vector<Objeto> objetos_;
  std::vector<Aviso> fila_;
  std::map<std::uint32_t, std::uint32_t> classes_pedidas_;
  std::uint64_t avisos_emitidos_ = 0;
  std::uint64_t avisos_entregues_ = 0;
  std::uint64_t avisos_nao_entregues_ = 0;
  std::uint64_t avisos_descartados_ = 0;
  // A serie do proximo objecto criado. Comeca em 1 para o ZERO significar
  // "nenhum" -- um aviso com serie zero e um aviso que ninguem gerou.
  std::uint32_t proxima_serie_ = 1;
  std::uint32_t pedidos_aceitos_ = 0;
  std::uint32_t pedidos_recusados_ = 0;
  std::string ultimo_motivo_;
  bool instalada_ = false;
};

}  // namespace zb2::brew

#endif  // ZB2_CORE_BREW_IMEDIA_H
