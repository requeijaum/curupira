// Bateria: corre os titulos do corpus e regista UM estado medido por titulo.
//
// PRINCIPIO P3 do desenho: "o corpus e a especificacao". Um titulo que piora tem
// de FALHAR a bateria, e o progresso tem de ser um numero por titulo, versionado
// -- nao uma opiniao sobre uma captura de ecra. Na arvore antiga o censo media a
// IMAGEM, e por isso dizia "sem desenho" sobre titulos que desenhavam.
//
// O que se mede aqui, por titulo, sem depender de o jogo desenhar nada:
//   carga    -- leu e mapeou a imagem?
//   module   -- o `AEEMod_Load` deu um ponteiro de modulo nao nulo?
//   vtable   -- a vtable do modulo tem 4 slots dentro do modulo?
//   create   -- o `IModule::CreateInstance` devolveu um applet nao nulo?
//   recusadas-- instrucoes que o interpretador nao soube executar
//   passos   -- instrucoes corridas em cada fase
//   faltas   -- slots do sistema que ficaram por implementar (por nome)
//
// Uso: zb2_bateria <corpus.json> <diretorio_dos_mods> [saida.json]

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <algorithm>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "core/brew/ajudantes.h"
#include "core/brew/despacho.h"
#include "ajudantes_slots.inc"
#include "core/brew/sha256.h"
#include "core/brew/interface.h"
#include "core/brew/tela.h"
#include <filesystem>
#include <set>

#include "core/carga/mod.h"
#include "core/cpu/arm_interpreter.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"

using namespace zb2;

namespace {

// A BASE DO MODULO, MEDIDA -- e nao a convencao `0x00100000` que eu tinha
// herdado.
//
// Os literais de um `.mod` sao OFFSETS DO FICHEIRO usados como ENDERECOS
// ABSOLUTOS: com a base a zero, 51 literais do `pacmania.mod` caem exactamente em
// cima de cadeias reais; com a base 0x00100000, ZERO. Ver `tests/mod_base_test.cpp`.
//
// O efeito nos 62 titulos: `modulo 48 -> 62` e `applet 22 -> 41`.
constexpr std::uint32_t kBase = 0x00000000u;
constexpr std::uint32_t kPilha = 0x80080000u;
constexpr std::uint32_t kHeap = 0x80200000u;
constexpr std::uint32_t kHeapTam = 0x00C00000u;
constexpr std::uint32_t kTabela = 0x80010000u;
// A AREA DE RASCUNHO CAI DENTRO DA IMAGEM DO MODULO EM 29 DOS 62 TITULOS.
//
// Com a base do modulo a ZERO, a imagem comeca em 0 e vai ate ao tamanho do ficheiro --
// **29 dos 62 tem imagem maior que 0x90010** (o maior e o `quake2brew`, 0x84D790). O
// carregador escreve o ponteiro do modulo EM CIMA DO CODIGO do proprio modulo, e
// **nada acusa.**
//
// MEDIDO, e e a hipotese mais forte para a parede que resta: a auditoria do
// descodificador fechou **95,7% das divergencias silenciosas** de descodificacao
// (66.376 -> 2.838) e **NENHUM titulo mudou de estado**. A parede nao esta na
// descodificacao: esta no ESTADO com que o titulo entra -- e "dados lidos do sitio
// errado" e o que isto produz, sem precisar de mais nenhuma classe.
//
// **E NAO MEXI NESTES DOIS NUMEROS, depois de duas tentativas medidas.**
//
//   0x00090000  (o original)   -> modulo 62 | applet 37   (mas dentro da imagem em 29)
//   0x81000000                 -> modulo 48 | applet 38
//   heap do alocador           -> modulo 48 | applet 38
//
// **Qualquer endereco que nao seja o antigo perde 14 titulos**, e a causa nao esta
// determinada. A leitura que os dados sustentam: **o endereco do rascunho nao e nosso
// para escolher -- o carregador do GUEST decide alguma coisa com ele**, e o valor antigo
// coincide com o que ele espera. Escolher outro sitio por tentativa trocaria um defeito
// silencioso por uma regressao medida, e **nao se escolhe endereco pelo mapa**: foi o
// que o `0x800C0000` ja ensinou, quando dois titulos mudaram de comportamento por
// LEREM o endereco.
//
// O que falta medir, com precisao: **o que o GUEST escreve e le em 0x90000.** Esta
// registado como pedido no relatorio, e e uma frente propria.
constexpr std::uint32_t kPPMod = 0x00090000u;
constexpr std::uint32_t kPPObj = 0x00090010u;
constexpr std::uint32_t kSentinela = 0xFFFFFFF0u;
// O primeiro indice da faixa da ENTRADA (etapa 8). Os indices desta faixa tem de
// caber em `Saidas::quantos` (100000, acima): um endereco de saida fora da faixa
// nunca e reconhecido, e o modulo atenderia zero chamadas em silencio.
constexpr std::uint32_t kBaseDasEntradas = 20000;
// O TECTO PREDEFINIDO, quando o corpus nao declara `max_steps` para o titulo.
//
// MEDIDO porque 4 000 000 nao chegavam: o `quake2brew` precisa de 5 932 075 passos
// so para a CARGA (a zeragem da ZI/BSS do proprio modulo sao 1 947 556 iteracoes
// de tres instrucoes, campo +0x24 do cabecalho = 0x0076de90 bytes). Com 4M ficava
// `vtable: false` e parecia um defeito de carregador -- e era orcamento curto.
// Comprovado por segunda ferramenta: `zb2_sonda_mod <mod> 50000000` diz
// "MALLOC(36) apos 5932046 instrucoes". As referencias dao muito mais folga
// (zeebulator 64M em `game_probe.cpp:418`, zeemu 500M).
constexpr std::uint64_t kLimite = 8000000ull;

// QUANTOS QUADROS DO LACO DE EVENTO CORRER DEPOIS DO `CreateInstance`.
//
// MEDIDO, e foi o que faltava para a etapa 8: sem isto a bateria mede o ARRANQUE.
// Em BREW o laco de quadro do jogo so comeca quando o applet arma o temporizador e
// o sistema o chama. Um menu que navega precisa de MUITOS quadros depois disso.
//
// O valor por omissao e ZERO, e e deliberado: sem `ZB2_QUADROS` a bateria da
// EXACTAMENTE os numeros que dava antes desta mudanca.
constexpr int kQuadrosPorOmissao = 0;

// ENTREGAR O `EVT_APP_START` AO APPLET.
//
// MEDIDO: o `IModule::CreateInstance` de um applet BREW constroi o objecto e
// VOLTA. O trabalho a serio -- carregar recursos, armar o temporizador do laco de
// quadro -- acontece quando o SHELL entrega `EVT_APP_START` (0, `AEEEvent.h:23`)
// ao `HandleEvent` do applet, que e o SLOT 2 da vtable DELE (AddRef=0, Release=1,
// HandleEvent=2). Sem isto a Z-Wheel faz 255 passos no `create`, arma ZERO
// temporizadores e desenha 0 pixels.
//
// Por omissao LIGADO, e a decisao e deliberada.
//
// Houve aqui um interruptor a DESLIGAR isto por omissao, com o argumento certo de
// que, sem ele, "os numeros sao os de sempre". Mas esse argumento tem um preco que
// nao se ve: **um instrumento que decide se o emulador faz o seu trabalho esta a
// medir-se a si proprio.**
//
// `EVT_APP_START` nao e uma opcao de instrumentacao -- e o que a plataforma faz. A
// bateria que media "o applet foi criado" e nunca arrancava o app **nao estava a
// medir a aplicacao**: media o `CreateInstance`.
//
// Mudar uma medicao faz-se RE-MEDINDO e regravando a referencia no mesmo commit, e
// nao deixando o interruptor desligado. Foi o que se fez: ver o `tools/baseline/`.
constexpr int kEventosPorOmissao = 1;

// ORCAMENTO DE TEMPO POR FASE, em segundos.
//
// MEDIDO: com os slots certos, o `pacmania` passou a correr mais de 900 s sozinho
// -- contra 4 minutos da bateria INTEIRA dos 62 titulos antes. Quem paga nao e a
// emulacao: e o INSTRUMENTO, que grava em texto cada escrita a memoria do guest.
//
// Um limite de PASSOS nao chega como orcamento, porque o custo por passo depende
// do titulo. Sem isto, um titulo que acorda faz a bateria deixar de servir para
// medir os outros 61 -- e um instrumento que deixa de se poder correr e um
// instrumento morto.
// NAO HA ORCAMENTO POR RELOGIO, de proposito. A constante que aqui estava
// (`kOrcamentoSegundos = 25`) nunca foi usada -- `grep` so a encontrava a si
// propria. Reintroduzi-la seria violar o P4 (determinismo por construcao): o
// mesmo binario com a mesma entrada tem de dar o mesmo resultado numa maquina
// carregada e numa maquina vazia. O orcamento e em PASSOS, por titulo
// (`Titulo::max_steps`) ou pelo `kLimite` acima. Ver `despacho.cpp:393`.
// Os slots da tabela comecam neste indice da faixa de saida. Abaixo dele ficam
// os servicos tratados (malloc, free, AddRef, Release).
constexpr std::uint32_t kBaseDoSlot = 1000;
// Os slots da vtable do IShell comecam aqui, para o mesmo efeito: saber QUAL
// metodo da interface cada titulo chama, e nao so que chamou algum.
constexpr std::uint32_t kBaseDoShell = 2000;

// Vtables das interfaces que o shell entrega. Cada uma tem slots com endereco
// proprio, para o pedido seguinte ficar nomeado.
constexpr std::uint32_t kBaseDoDisplay = 3000;
constexpr std::uint32_t kBaseDoFileMgr = 4000;
constexpr std::uint32_t kIidDisplay = 0x01001001u;
constexpr std::uint32_t kIidFileMgr = 0x01001003u;
// Os IIDs que a bateria MEDIU como pedidos ao `CreateInstance`, com o nome do SDK
// (`platform/system/inc/AEEClassIDs.h`). A lista vem da medicao, nao de uma
// leitura do cabecalho: e a ordem por demanda que diz o que vale implementar.
constexpr std::uint32_t kIidHeap = 0x01001002u;
constexpr std::uint32_t kIidFile = 0x01001014u;
constexpr std::uint32_t kIidSound = 0x01001056u;
constexpr std::uint32_t kIidGraphics = 0x01002001u;
constexpr std::uint32_t kIidRootForm = 0x01028e51u;
constexpr std::uint32_t kIidHid = 0x0106c411u;
constexpr std::uint32_t kIidSqlMgr = 0x0102c4e8u;
// UM objecto e UMA vtable por interface, e nao um objecto so para todas.
//
// MEDIDO, e foi um defeito meu: com um objecto unico, o `slot 7` quer dizer
// `IHeap::slot7` OU `IFile::slot7` OU `ISound::slot7` -- conforme quem chamou --
// e a bateria nomeava tudo como `IFileMgr::slot2007`, porque o nome sai do
// intervalo da vtable. **A demanda ficava desonesta**: nao se sabia que metodo
// cada titulo quer, que e o unico proposito desta lista.
// A ordem desta tabela e a ordem em que os objectos sao construidos: o indice e
// o que liga o IID ao objecto.
struct GenericIfc { std::uint32_t iid; const char* nome; };
const GenericIfc kGenericos[] = {
    {0x01001002u, "IHeap"},    {0x01001014u, "IFile"},  {0x01001056u, "ISound"},
    {0x01002001u, "IGraphics"}, {0x01028e51u, "IRootForm"},
    {0x0106c411u, "IHID"},     {0x0102c4e8u, "ISQLMgr"},
};
constexpr std::uint32_t kNGenericos = sizeof(kGenericos) / sizeof(kGenericos[0]);
// Os slots do IFileMgr, na ordem que `platform/deprecated/inc/AEEFile.h` declara
// em `INHERIT_IFileMgr`. A ORDEM E A DO SDK, lida campo a campo -- e nao
// copiada de outro emulador, que foi o erro que a arvore antiga cometeu com os
// slots do IGLES11.
enum : std::uint32_t {
  kFmQueryInterface = 2,  // nome historico; no IShell este slot e o CreateInstance
  kFmOpenFile = 3,
  kFmGetInfo = 4,
  kFmTest = 8,
  kFmGetFreeSpace = 9,
  kFmGetLastError = 10,
};
constexpr std::uint32_t kSlotDbgPrintf = 0x09c;
// Os helpers mais basicos. Sao funcoes PURAS, sem estado e sem interface: a
// semantica vem do C e do SDK, e um teste pode compara-las com a libc do
// hospedeiro -- que e como a arvore antiga fechou a duvida sobre `strcmp` e
// `strstr`. Aqui implementam-se porque a bateria os pediu POR DEMANDA.
// OS OFFSETS VEM DO CABECALHO GERADO, e NAO escritos aqui.
//
// Havia DUAS copias destes numeros -- esta e a do despacho -- e elas DIVERGIRAM:
// o despacho tinha 0x0a0 para o `strtowstr` (que e 0x040) e 0x090 para o
// `aee_GetRand` (que e 0x0a8). O efeito medido: um guest que chamasse
// `wstrcompress` recebia a conversao do `strtowstr`, um que chamasse `atoi`
// recebia BYTES ALEATORIOS, e os outros dois recebiam EUNSUPPORTED.
//
// Nada comparava as duas copias. **Duas listas que tem de concordar sao zero
// listas** -- e esta e a sexta vez que este erro aparece neste trabalho, agora com
// consequencia no valor de retorno.
//
// Achado pelo sub-agente `helpers`, que gerou `tools/ajudantes_slots.inc` a partir
// de `AEEStdLib.h` e provou que o despacho divergia.
constexpr std::uint32_t kSlotMemmove = brew_ajudantes::kAjudante_memmove;
constexpr std::uint32_t kSlotMemset = brew_ajudantes::kAjudante_memset;
constexpr std::uint32_t kSlotStrcpy = brew_ajudantes::kAjudante_strcpy;
constexpr std::uint32_t kSlotStrcmp = brew_ajudantes::kAjudante_strcmp;
constexpr std::uint32_t kSlotStrlen = brew_ajudantes::kAjudante_strlen;
constexpr std::uint32_t kSlotStrchr = brew_ajudantes::kAjudante_strchr;
constexpr std::uint32_t kSlotStrtowstr = brew_ajudantes::kAjudante_strtowstr;
constexpr std::uint32_t kSlotAeeGetRand = brew_ajudantes::kAjudante_aee_GetRand;
// A CONFERENCIA, no sitio onde o erro aconteceu. Se o `.inc` mudar, isto nao
// compila -- e um numero errado nao chega a correr.
static_assert(kSlotStrtowstr == 0x040, "0x040 e strtowstr; 0x0a0 e wstrcompress");
static_assert(kSlotAeeGetRand == 0x0a8, "0x0a8 e aee_GetRand; 0x090 e atoi");
constexpr std::uint32_t kSlotIdStrtowstr = 1500;
constexpr std::uint32_t kSlotIdGetAeeVersion = 1501;
constexpr std::uint32_t kSlotIdAeeGetRand = 1502;
constexpr std::uint32_t kSlotIdStrlen = 1503;
constexpr std::uint32_t kSlotIdMemset = 1504;
constexpr std::uint32_t kSlotIdStrcpy = 1505;
constexpr std::uint32_t kSlotIdMemmove = 1506;
constexpr std::uint32_t kSlotIdStrcmp = 1507;
constexpr std::uint32_t kSlotIdStrchr = 1508;
constexpr std::uint32_t kSlotIdGetUpTime = 1540;
constexpr std::uint32_t kSlotIdQueryClass = 1541;
constexpr std::uint32_t kSlotIdGetAppInstance = 1543;
constexpr std::uint32_t kSlotIdFmTest = 1510;
constexpr std::uint32_t kSlotIdFmFree = 1511;
constexpr std::uint32_t kSlotIdFmLastErr = 1512;
constexpr std::uint32_t kSlotIdSetTimer = 1520;
constexpr std::uint32_t kSlotIdGetFontMetrics = 1530;
constexpr std::uint32_t kSlotIdMeasureText = 1531;
constexpr std::uint32_t kSlotIdDrawText = 1532;
constexpr std::uint32_t kSlotIdDrawRect = 1533;
constexpr std::uint32_t kSlotIdBitBlt = 1534;
constexpr std::uint32_t kSlotIdSetColor = 1535;
constexpr std::uint32_t kSlotIdSetClipRect = 1536;
constexpr std::uint32_t kSlotIdUpdate = 1537;
constexpr std::uint32_t kSlotIdBacklight = 1542;
constexpr std::uint32_t kSlotIdMkDir = 1544;
constexpr std::uint32_t kSlotIdRemove = 1509;
constexpr std::uint32_t kSlotIdBitmapQI = 1565;
// **SEGUNDA COPIA DE UM NUMERO**, como o `kSlotIdBitmapQI` acima: o valor tem de
// ser o mesmo que `core/brew/despacho.cpp` usa (1572). Fica aqui porque a tabela
// de cablagem vive nesta ferramenta; quem mudar um lado tem de mudar o outro.
//
// E ISTO JA FALHOU, UMA HORA DEPOIS DE O AVISO ACIMA SER ESCRITO: o numero era
// 1568, colidiu com o `strcat` de outra frente, mudei-o no `despacho.cpp` e NAO
// aqui. A bateria passou a cablar o slot 12 do IBitmap para a implementacao do
// `strcat`, e a medicao deu "10 regressoes, pixels 640 -> 0" -- um numero que
// parecia legitimo e nao era. Duas copias do mesmo numero em ficheiros
// diferentes nao se defendem com um comentario; defendem-se com a leitura de
// volta que o `kWire` faz logo abaixo, e que aqui ainda nao cobre este slot.
constexpr std::uint32_t kSlotIdBitmapGetInfo = 1572;
constexpr std::uint32_t kSlotIdGetDest = 1545;
constexpr std::uint32_t kSlotIdSetDest = 1546;
constexpr std::uint32_t kSlotIdRmDir = 1547;
constexpr std::uint32_t kSlotIdGetFontMetricsAlias = 1548;
constexpr std::uint32_t kSlotIdGetDeviceInfo = 1549;
constexpr std::uint32_t kSlotIdGetDeviceBitmap = 1550;
constexpr std::uint32_t kSlotIdGetClipRect = 1551;
constexpr std::uint32_t kSlotIdCancelTimer = 1552;
constexpr std::uint32_t kSlotIdSqlOpen = 1553;
constexpr std::uint32_t kSlotIdOpenFile = 1554;
constexpr std::uint32_t kSlotIdFileRead = 1555;
constexpr std::uint32_t kSlotIdFileSeek = 1556;
constexpr std::uint32_t kSlotIdFileInfo = 1557;
constexpr std::uint32_t kSlotIdFileRelease = 1558;
constexpr std::uint32_t kSlotIdSprintf = 1560;
constexpr std::uint32_t kSlotIdVsprintf = 1561;
constexpr std::uint32_t kSlotIdHeapLock = 1562;
constexpr std::uint32_t kSlotIdFreeResData = 1563;
constexpr std::uint32_t kSlotIdCheckPriv = 1564;
constexpr std::uint32_t kSlotIdFileWrite = 1559;

// OS NUMEROS DE SLOT VEM DO CABECALHO, GERADOS.
//
// Estavam escritos a mao e estavam TODOS errados por um, porque `INHERIT_IBase`
// tem DOIS membros (`AddRef`, `Release`) e eu contava TRES, a procura de um
// `QueryInterface` que nao existe em `INHERIT_IBase`.
//
// O sintoma que obrigou a descobri-lo: o `pacmania` (`mod/276212/pacmania.mod`)
// faz
//     00109d5c  ldr r1, [r0]        ; a vtable
//     00109d60  ldr ip, [r1, #8]    ; <<< INDICE 2
//     00109d64  mov r1, r6          ; AEE_FONT_BOLD
//     00109d68  blx ip              ; (po, fonte, &asc, &desc)
// -- a assinatura EXATA do `GetFontMetrics`, e eu recusava-o porque tinha o 2
// como `Release`. **O jogo estava certo e eu errado.**
#include "brew_slots.inc"

// `AEEDeviceInfo`, TRANSCRITA de `platform/system/inc/AEEIShell.h`.
//
// Os campos escrevem-se por NOME com `offsetof`-equivalente (a struct e o layout
// do compilador), e nao por numero de offset escrito a mao. **Um offset escrito a
// mao ja divergiu uma vez nesta sessao** -- os slots do IDisplay -- e o custo foi
// uma ronda inteira.
//
// `EmptyEnum` e `unsigned` (AEEIShell.h linha 83). No ARM AAPCS o `uint32` alinha
// a 4 e as bitfields `unsigned : 1` empacotam no mesmo `unsigned`, que e o mesmo
// que o x86-64 faz aqui.
namespace brew {
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
}  // namespace brew

constexpr std::uint32_t kSlotIdCreateDIBitmap = 1538;
// Os slots do IDisplay, na ordem que `platform/ui/inc/AEEIDisplay.h` declara em
// `INHERIT_IDisplay`. Lido campo a campo.
enum : std::uint32_t {
  kDisGetFontMetrics = brew_slots::kDisplay_GetFontMetrics,
  kDisMeasureTextEx = brew_slots::kDisplay_MeasureTextEx,
  kDisDrawText = brew_slots::kDisplay_DrawText,
  kDisDrawRect = brew_slots::kDisplay_DrawRect,
  kDisBitBlt = brew_slots::kDisplay_BitBlt,
  kDisUpdate = brew_slots::kDisplay_Update,
  kDisSetColor = brew_slots::kDisplay_SetColor,
  kDisCreateDIBitmap = brew_slots::kDisplay_CreateDIBitmap,
  kDisSetClipRect = brew_slots::kDisplay_SetClipRect,
};

// A TELA VEM DO MOTOR (`core/brew/tela.h`). Aqui havia uma copia, e era a
// segunda verdade sobre o mesmo assunto: a ferramenta desenhava numa tela que
// nenhuma frente podia ver, e nenhum teste lhe chegava.
// O ESTADO VEM DO MOTOR. A ferramenta passa a ser: ler o corpus, criar um
// `Despacho`, mandar correr, e escrever o que se mediu.
zb2::brew::Despacho* g_despacho = nullptr;

std::uint32_t g_textos = 0;
std::uint32_t g_blits = 0;
std::uint32_t g_updates = 0;
std::uint32_t g_dibs = 0;
// O CAMINHO DO TITULO ACTUAL e a vtable dos ficheiros: o despacho vive em
// `CorrerFase`, que nao recebe `dir` nem o titulo. Globais explicitas, postas no
// inicio de cada `Medir` -- e nao um estado que passa de um titulo para o outro.
std::string g_dir_actual;
std::string g_pasta_actual;
std::uint32_t g_vtable_ficheiro = 0;
std::string NormalizarCaminho(const std::string& bruto);
std::uint32_t g_backlights = 0;
std::uint32_t g_applet = 0;
bool g_alias_fontmetrics = false;
std::uint32_t g_destino = 0;
std::uint32_t g_vtable_bitmap = 0;
// Os slots do IShell, na ordem que `platform/system/inc/AEEIShell.h` declara em
// `INHERIT_IShell`. Lido campo a campo, e nao copiado.
enum : std::uint32_t {
  kSheCreateInstance = 3,
  kSheSetTimer = brew_slots::kShell_SetTimer,     // <<< o pedido de demanda mais alto depois do QI
  kSheCancelTimer = 14,
  kSheSendEvent = 25,
  kSheForceExit = 41,
};
// Um temporizador pedido pelo guest. Um so, porque e o que os titulos pedem:
// o laco de quadro, re-armado pelo proprio callback.
struct Temporizador {
  bool ativo = false;
  std::uint32_t callback = 0;   // AEECallback*
  std::int64_t vence_em_ms = 0;
};
constexpr std::uint32_t kSlotGetAeeVersion = brew_ajudantes::kAjudante_GetAEEVersion;

struct Titulo {
  std::string pasta;
  std::string mod;
  std::string clsid;
  // O TECTO DE PASSOS QUE O CORPUS DECLARA PARA ESTE TITULO, quando o declara.
  //
  // Zero = usar o `kLimite` da bateria. O campo existia no `corpus62.json` desde
  // sempre (o `cnk2` pede 186 486 543 e o `fifa09` 483 295 456) e a ferramenta
  // IGNORAVA-O: o `cnk2` recebia 2,1% do que pedia. Um tecto por titulo mantem o
  // determinismo (e passos, nao relogio -- ver o P4 em `despacho.cpp:393`) e
  // deixa de fazer a carga de um titulo grande parecer um defeito de carregador.
  std::uint64_t max_steps = 0;
};

std::vector<Evento> dm_eventos;  // eventos do ultimo titulo, para os detalhes

struct Estado {
  bool carga = false;
  bool modulo = false;
  bool vtable = false;
  bool create = false;
  std::uint64_t passos_carga = 0;
  std::uint64_t passos_create = 0;
  std::uint64_t passos_start = 0;  // a fase do `EVT_APP_START`
  std::uint64_t recusadas = 0;
  std::uint32_t pixels = 0;
  std::uint32_t cores = 0;
  std::uint32_t textos = 0;
  std::uint32_t blits = 0;
  std::uint32_t tamanho = 0;
  // Quantos quadros do laco de evento correram (so com `ZB2_QUADROS`), e se o
  // `EVT_APP_START` foi entregue (so com `ZB2_EVT_START`).
  int quadros = 0;
  int eventos = 0;
  std::string motivo;   // porque parou, quando parou
  std::map<std::string, std::uint64_t> faltas;
};

std::vector<std::uint8_t> Ler(const std::string& c, bool* ok) {
  std::ifstream f(c, std::ios::binary);
  if (!f) { *ok = false; return {}; }
  std::vector<std::uint8_t> v((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  *ok = !v.empty();
  return v;
}

// Le o corpus62.json sem dependencia de JSON: o ficheiro tem uma lista de
// objectos com "folder", "mod" e "clsid_hex", e uma leitura por campos chega.
std::vector<Titulo> LerCorpus(const std::string& caminho) {
  std::vector<Titulo> out;
  std::ifstream f(caminho);
  if (!f) return out;
  std::stringstream ss; ss << f.rdbuf();
  const std::string s = ss.str();
  size_t p = 0;
  while ((p = s.find("\"folder\"", p)) != std::string::npos) {
    Titulo t;
    auto valor = [&](const char* chave, size_t de) -> std::string {
      const size_t k = s.find(chave, de);
      if (k == std::string::npos) return {};
      const size_t a = s.find('"', s.find(':', k) + 1);
      const size_t b = s.find('"', a + 1);
      return s.substr(a + 1, b - a - 1);
    };
    t.pasta = valor("\"folder\"", p);
    t.mod = valor("\"mod\"", p);
    t.clsid = valor("\"clsid_hex\"", p);
    // `max_steps` e um NUMERO, e nao uma cadeia: nao passa pelo `valor` acima,
    // que procura aspas. Le-se so dentro deste objecto -- o `fim` e a chaveta
    // que fecha, senao um titulo sem campo apanhava o do titulo seguinte.
    {
      const size_t fim = s.find('}', p);
      const size_t k = s.find("\"max_steps\"", p);
      if (k != std::string::npos && (fim == std::string::npos || k < fim)) {
        const size_t d = s.find(':', k);
        if (d != std::string::npos) t.max_steps = std::strtoull(s.c_str() + d + 1, nullptr, 10);
      }
    }
    if (!t.pasta.empty() && !t.mod.empty()) out.push_back(t);
    p += 8;
  }
  return out;
}

// Um "sistema" minimo: o que a bateria consegue oferecer sem implementar nada
// a mais. Tudo o que NAO tem implementacao fica registado como falta, com o
// nome -- principio P2.
// Um IShell minimo, mas REAL: um objecto cujo primeiro campo e a vtable.
//
// MEDIDO, e foi o que destravou a bateria: depois do `malloc`, o primeiro que
// TODO modulo faz e `shell->AddRef()`:
//     10278c  ldr r0, [r6]      ; r0 = *(pishell)  -- a vtable do IShell
//     102794  ldr r1, [r0]      ; r1 = vtable[0]   -- AddRef
//     10279c  bx  r1
// Com `pishell` a apontar para memoria sem vtable, r1 sai 0 e o `bx r1` salta
// para zero -- que era, literalmente, o `saiu_do_modulo_para_0x0` que 61 dos 62
// titulos davam.
// Quantos quadros do laco de evento correr por titulo, e se o `EVT_APP_START` e
// entregue. Zero = os numeros de sempre.
int g_quadros = kQuadrosPorOmissao;
int g_eventos = kEventosPorOmissao;
bool g_trace = false;

Estado Medir(const Titulo& t, const std::string& dir) {
  // O TECTO DESTE TITULO: o que o corpus declara, ou o predefinido. Uma so
  // variavel para as quatro fases (carga, create, evento, quadros), porque um
  // tecto diferente por fase faria o mesmo titulo ter dois orcamentos e ninguem
  // saberia qual deles esgotou.
  const std::uint64_t limite = (t.max_steps > 0) ? t.max_steps : kLimite;
  Estado e;
  bool ok = false;
  const std::vector<std::uint8_t> imagem = Ler(dir + "/" + t.pasta + "/" + t.mod + ".mod", &ok);
  if (!ok) { e.motivo = "mod_ausente"; return e; }
  // O TAMANHO DA IMAGEM. Este campo ficou a ZERO durante varias rondas, porque a
  // linha foi removida no commit `d75281d` e nada acusou.
  //
  // A CONSEQUENCIA NAO ERA SO UM CAMPO BONITO A ZERO: o `vtable` compara
  // `alvo >= kBase && alvo < kBase + e.tamanho`, e com o tamanho a zero isso e
  // sempre FALSO. **Um dos quatro degraus do arranque estava morto**, e a bateria
  // imprimia `vtable NAO` nos 62 titulos -- que eu lia como "os modulos nao tem
  // vtable" em vez de "o campo nao mede nada".
  //
  // **Um campo que nao mede e pior do que um campo ausente: um ausente nao se le.**
  e.tamanho = static_cast<std::uint32_t>(imagem.size());
  // O framebuffer e POR TITULO: um estado que passa de um titulo para o outro
  // tornaria a medida incomparavel -- que e o defeito de metodo mais repetido
  // desta sessao.
  g_despacho = nullptr;  // o ponteiro so vale dentro de `Medir`
  g_textos = g_blits = g_updates = g_dibs = 0;

  Tempo tempo;
  Traco traco("bateria", &tempo);
  DestinoMemoria dm;            // para a lista final poder dizer os ARGUMENTOS
  traco.JuntarDestino(&dm);
  Memoria mem(&traco);
  // Registar os ficheiros irmaos do modulo no VFS, UMA vez por titulo.
  {
    std::set<std::string> ficheiros;
    std::error_code ec;
    for (const auto& entrada : std::filesystem::directory_iterator(
             dir + "/" + t.pasta, std::filesystem::directory_options::skip_permission_denied, ec)) {
      if (!entrada.is_regular_file(ec)) continue;
      std::string n = entrada.path().filename().string();
      for (char& ch : n) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
      ficheiros.insert(n);
    }
    static std::set<std::string> guardado;
    guardado = ficheiros;
  }
  mem.EscritorUnico("cpu");
  ArmInterpreter cpu(mem, &traco);
  Alocador al(mem, kHeap, kHeapTam, &traco);

  Saidas s;
  s.base = 0xF0000000u;
  s.quantos = 100000;   // espaco para a tabela de ajudantes E para as vtables
  s.passo = 4;
  s.ativa = true;
  cpu.ConfigurarSaidas(s);
  g_vtable_bitmap = s.Endereco(zb2::brew::kVtableBitmap);
  g_dir_actual = dir;
  g_pasta_actual = t.pasta;
  g_vtable_ficheiro = s.Endereco(zb2::brew::kVtableFileObj);
    // O DESPACHO DO MOTOR. A tabela de ajudantes e as implementacoes vivem em
  // `core/brew/despacho`, que TEM TESTES -- antes viviam aqui, e so a ferramenta
  // sabia correr um jogo.
  zb2::brew::Vfs vfs_do_titulo;
  vfs_do_titulo.Registar(dir + "/" + t.pasta);
  zb2::brew::Despacho despacho(mem, traco, al, vfs_do_titulo);
  despacho.InstalarAjudantes(s, kTabela);
  if (const char* qu = std::getenv("ZB2_QUADROS")) g_quadros = std::atoi(qu);
  if (const char* ev = std::getenv("ZB2_EVT_START")) g_eventos = std::atoi(ev);
  if (const char* tr = std::getenv("ZB2_TRACE")) g_trace = std::atoi(tr) != 0;
  // A ENTRADA (etapa 8). Sem guiao (`ZB2_ENTRADA`) o controle fica em repouso; com
  // um guiao invalido a instalacao RECUSA e diz por que, e a corrida segue sem
  // entrada -- e nao com meia entrada.
  if (!despacho.InstalarEntrada(s, kBaseDasEntradas)) {
    std::fprintf(stderr, "ENTRADA NAO INSTALADA -- ver as faltas\n");
  }
  g_despacho = &despacho;
  despacho.DefinirFaixaDoModulo(kBase, static_cast<std::uint32_t>(imagem.size()));
  // A TELA E LIMPA AQUI, e nao no inicio do `Medir`: o `Despacho` vive no
  // ambito desta funcao, e um ponteiro guardado de um titulo para o outro
  // apontaria para memoria morta. A primeira versao fazia isso e o resultado era
  // uma falha de segmentacao no SEGUNDO titulo.
  despacho.TelaRef().Limpar();
  const std::uint32_t clsid_do_titulo =
      static_cast<std::uint32_t>(std::strtoul(t.clsid.c_str(), nullptr, 0));
  despacho.SituarTitulo(dir, t.pasta, clsid_do_titulo);
  despacho.DefinirVtableBitmap(s);
  despacho.DefinirVtableFicheiro(s.Endereco(zb2::brew::kVtableFileObj));

  // As vtables das interfaces ficam ACIMA da tabela de ajudantes, dentro da
  // mesma faixa de saida. Enderecos distintos por interface.
  const std::uint32_t kShell = 0x80020000u;
  zb2::brew::ConstruirObjeto(mem, s, kShell, s.Endereco(zb2::brew::kVtableShell),
                             zb2::brew::kSlotsPorVtable, zb2::brew::kBaseDoShell);

  // As interfaces que o shell entrega pelo `CreateInstance` (slot 2).
  //
  // MEDIDO, e e o que decidiu a ordem desta etapa: 22 titulos pedem
  // `AEECLSID_DISPLAY` e 4 pedem `AEECLSID_FILEMGR`, ambos pelo slot 2 do IShell
  // (CreateInstance) com o ClsId no r1 e o ponteiro de saida no r2.
  zb2::brew::ConstruirObjeto(mem, s, zb2::brew::kObjDisplay, s.Endereco(zb2::brew::kVtableDisplay), 64, zb2::brew::kVtableDisplay);
  zb2::brew::ConstruirObjeto(mem, s, zb2::brew::kObjFileMgr, s.Endereco(zb2::brew::kVtableFileMgr), 64, zb2::brew::kVtableFileMgr);

  // Um objecto generico para as interfaces que ainda nao tem implementacao.
  //
  // MOTIVO, e e uma decisao de honestidade: quando o `QueryInterface` recusa, o
  // jogo desiste e a bateria nao aprende nada sobre ele. Quando devolve um
  // objecto cujos slots RECUSAM em voz alta, a bateria aprende QUAL metodo
  // daquela interface o jogo quer -- e a lista de demanda cresce em vez de
  // parar. O que NAO se faz e devolver sucesso com um objecto que finge
  // funcionar (principio P2).
  for (std::uint32_t k = 0; k < kNGenericos; ++k) {
    zb2::brew::ConstruirObjeto(mem, s, zb2::brew::ObjGenerico(k), s.Endereco(zb2::brew::VtGenerico(k)), 64, zb2::brew::VtGenerico(k));
  }
  // Os slots do IFileMgr que o corpus pede, e que tem implementacao.
  // A TABELA UNICA DA CABLAGEM DAS VTABLES: objecto, slot, endereco de saida.
  //
  // E uma so lista de proposito, pela mesma razao da tabela dos ajudantes. Antes
  // disto eram escritas soltas espalhadas por cem linhas, e UMA DELAS
  // DESAPARECEU numa edicao de texto sem eu notar -- o `SetTimer` (o slot 12 do
  // IShell) ficou escrito em todo o lado menos na vtable, e a bateria voltou a
  // dizer "falta SetTimer" com o SetTimer a funcionar. **E o mesmo sintoma da
  // quarta e da oitava ocorrencia, por uma terceira causa: agora a causa e a
  // edicao, nao a ordem.**
  //
  // Com uma so tabela, e impossivel acrescentar uma implementacao sem a cablar:
  // a cablagem e o unico sitio onde se declara o que existe.
  const struct { std::uint32_t vt; std::uint32_t slot; std::uint32_t saida; } kWire[] = {
      // IShell
      {zb2::brew::kVtableShell, brew_slots::kShell_SetTimer, kSlotIdSetTimer},
      {zb2::brew::kVtableShell, brew_slots::kShell_QueryClass, kSlotIdQueryClass},
      {zb2::brew::kVtableShell, brew_slots::kShell_GetDeviceInfo, kSlotIdGetDeviceInfo},
      {zb2::brew::kVtableShell, brew_slots::kShell_CancelTimer, kSlotIdCancelTimer},
      {zb2::brew::kVtableShell, brew_slots::kShell_FreeResData, kSlotIdFreeResData},
      {zb2::brew::kVtableShell, brew_slots::kShell_CheckPrivLevel, kSlotIdCheckPriv},
      {zb2::brew::kVtableDisplay, brew_slots::kDisplay_GetDeviceBitmap, kSlotIdGetDeviceBitmap},
      {zb2::brew::kVtableDisplay, brew_slots::kDisplay_GetClipRect, kSlotIdGetClipRect},
      {zb2::brew::VtGenerico(6), brew_slots::kSQLMgr_Open, kSlotIdSqlOpen},
      {zb2::brew::VtGenerico(0), brew_slots::kHeap1_Lock, kSlotIdHeapLock},
      {zb2::brew::kVtableFileMgr, brew_slots::kFileMgr_OpenFile, kSlotIdOpenFile},
      {zb2::brew::kVtableFileObj, brew_slots::kIAStream_Read, kSlotIdFileRead},
      {zb2::brew::kVtableFileObj, brew_slots::kIFile_Seek, kSlotIdFileSeek},
      {zb2::brew::kVtableFileObj, brew_slots::kIFile_GetInfo, kSlotIdFileInfo},
      {zb2::brew::kVtableFileObj, brew_slots::kIFile_Write, kSlotIdFileWrite},
      {zb2::brew::kVtableFileObj, 1, kSlotIdFileRelease},
      // IDisplay
      {zb2::brew::kVtableDisplay, kDisGetFontMetrics, kSlotIdGetFontMetrics},
      {zb2::brew::kVtableDisplay, kDisMeasureTextEx, kSlotIdMeasureText},
      {zb2::brew::kVtableDisplay, kDisDrawText, kSlotIdDrawText},
      {zb2::brew::kVtableDisplay, kDisDrawRect, kSlotIdDrawRect},
      {zb2::brew::kVtableDisplay, kDisBitBlt, kSlotIdBitBlt},
      {zb2::brew::kVtableDisplay, kDisSetColor, kSlotIdSetColor},
      {zb2::brew::kVtableDisplay, kDisSetClipRect, kSlotIdSetClipRect},
      {zb2::brew::kVtableDisplay, kDisUpdate, kSlotIdUpdate},
      {zb2::brew::kVtableDisplay, brew_slots::kDisplay_Backlight, kSlotIdBacklight},
      {zb2::brew::kVtableDisplay, kDisCreateDIBitmap, kSlotIdCreateDIBitmap},
      {zb2::brew::kVtableDisplay, brew_slots::kDisplay_SetDestination, kSlotIdSetDest},
      {zb2::brew::kVtableDisplay, brew_slots::kDisplay_GetDestination, kSlotIdGetDest},
      // IFileMgr
      {zb2::brew::kVtableFileMgr, kFmTest, kSlotIdFmTest},
      {zb2::brew::kVtableFileMgr, kFmGetFreeSpace, kSlotIdFmFree},
      {zb2::brew::kVtableFileMgr, kFmGetLastError, kSlotIdFmLastErr},
      {zb2::brew::kVtableFileMgr, 7, kSlotIdRmDir},
      {zb2::brew::kVtableFileMgr, brew_slots::kFileMgr_MkDir, kSlotIdMkDir},
      {zb2::brew::kVtableFileMgr, brew_slots::kFileMgr_Remove, kSlotIdRemove},
      {zb2::brew::kVtableBitmap, 2, kSlotIdBitmapQI},
      // IBitmap::GetInfo -- slot 12 (`AEEIBitmap.h:42-58`). MEDIDO como pedido
      // pelos 10 titulos da familia `emulator_neo` (`karnovr.mod` 0xfdd4, com
      // `nSize = 0xc = sizeof(AEEBitmapInfo)`).
      {zb2::brew::kVtableBitmap, 12, kSlotIdBitmapGetInfo},
  };
  for (const auto& w : kWire) {
    // A GUARDA: um slot 0 num objecto ROPI e o `QueryInterface` da IBase, e a
    // cablagem por scan ja o poe la. Cablar slot 0 ou 1 por cima destruiria a
    // IBase de uma interface inteira sem nada a acusar.
    // A IBase ocupa os slots 0 e 1 -- `AddRef` e `Release`, e SO esses dois.
    //
    // Aqui esteve `< 3`, e a premissa errada mandou-me recusar o slot 2 do
    // IDisplay, que e o `GetFontMetrics`. **Uma guarda construida sobre um
    // numero errado recusa o que esta certo** -- e o custo foi uma bateria.
    // A UNICA EXCECAO, e ela e explicita.
    //
    // Para os objectos que o `ConstruirShell` construiu, os slots 0 e 1 ja tem o
    // `AddRef`/`Release` da IBase, e escrever por cima destroi-os. Mas o objecto
    // FICHEIRO nasce aqui e NAO tem `Release` nenhum -- e o jogo fecha ficheiros
    // com `IFILE_Release(p)`, que e o slot 1. Sem esta excepcao, o fecho nao
    // existia e o `Release` da IBase (que faz `AddRef`/`Release` de objectos ROPI)
    // era chamado com um ponteiro de ficheiro.
    const bool e_o_ficheiro = (w.vt == zb2::brew::kVtableFileObj);
    if (w.slot < 2 && !e_o_ficheiro) {
      std::fprintf(stderr, "CABLAGEM RECUSADA: slot %u do objecto 0x%08x e da IBase\n",
                   w.slot, w.vt);
      std::abort();
    }
    // O FIM da vtable deste objecto, e nao a base da PRIMEIRA vtable generica.
    // A guarda disparou na primeira versao porque comparava com `zb2::brew::kVtableGenericoBase`
    // -- e cada vtable generica tem 64 slots, logo o fim de uma e o inicio da
    // seguinte, nao a base da serie. **A guarda estava certa no proposito e
    // errada na conta.**
    const std::uint32_t fim = (w.vt >= zb2::brew::kVtableGenericoBase)
                                  ? zb2::brew::VtGenerico((w.vt - zb2::brew::kVtableGenericoBase) / 64u + 1u)
                                  : zb2::brew::kVtableGenericoBase;
    if (w.vt + w.slot >= fim) {
      std::fprintf(stderr, "CABLAGEM RECUSADA: slot %u de 0x%08x sai da vtable (fim 0x%08x)\n",
                   w.slot, w.vt, fim);
      std::abort();
    }
    mem.Escrever32(s.Endereco(w.vt) + w.slot * 4, s.Endereco(w.saida));
  }
  // LEITURA DE VOLTA, e aborta se nao bater certo.
  //
  // Existe porque a cablagem JA se perdeu uma vez sem sintoma visivel: o sintoma
  // era a bateria a dizer "falta SetTimer" com o SetTimer a funcionar, e isso
  // exigiu uma bateria inteira (4 minutos) e uma ida ao codigo para descobrir.
  // **Uma cablagem que nao se confirma a si propria e uma cablagem que se perde
  // em silencio.** Agora perde-se com estrondo, no arranque, em 1 segundo.
  for (const auto& w : kWire) {
    const std::uint32_t lido = mem.Ler32(s.Endereco(w.vt) + w.slot * 4);
    if (lido != s.Endereco(w.saida)) {
      std::fprintf(stderr,
                   "CABLAGEM PERDIDA: objecto 0x%08x slot %u tem 0x%08x, devia ter 0x%08x\n",
                   w.vt, w.slot, lido, s.Endereco(w.saida));
      std::abort();
    }
  }

  const auto carga = CarregarMod(mem, imagem, kBase, kTabela, &traco);
  if (!carga.ok) { e.motivo = "carga_recusada:" + carga.motivo; return e; }
  e.carga = true;

  // A FAIXA DO MODULO DO TITULO, para o modulo da entrada poder recusar um
  // callback que aponte para fora dela. A base e ZERO (medida); o TAMANHO vem do
  // carregador.
  despacho.DefinirFaixaDoModulo(kBase, carga.tamanho);
  cpu.Repor(kBase, kPilha);
  cpu.Set(kR0, kShell);                 // o IShell minimo mas real
  cpu.Set(kR2, kPPMod);
  cpu.Set(kLR, kSentinela);
  {
    const zb2::brew::ResultadoFase r = g_despacho->Correr(cpu, limite, kPPMod);
    e.passos_carga = r.passos;
    e.motivo = r.motivo;
  }
  e.recusadas = cpu.InstruscoesRecusadas();
  g_applet = mem.Ler32(kPPObj);  // para o `GetAppInstance`

  const std::uint32_t modulo = mem.Ler32(kPPMod);
  e.modulo = modulo != 0;
  if (!e.modulo) {
    e.motivo += " | sem_ponteiro_de_modulo";
    for (const auto& par : traco.ContagemFaltas()) e.faltas[par.first] = par.second;
    dm_eventos = dm.eventos;
    return e;
  }

  const std::uint32_t vtable = mem.Ler32(modulo);
  int dentro = 0;
  for (int i = 0; i < 4; ++i) {
    const std::uint32_t alvo = mem.Ler32(vtable + static_cast<std::uint32_t>(i) * 4);
    if (alvo >= kBase && alvo < kBase + e.tamanho) ++dentro;
  }
  e.vtable = dentro == 4;

  const std::uint32_t ci = mem.Ler32(vtable + 8);
  // O SLOT DE SAIDA E LIMPO ANTES DA FASE, como a plataforma faz.
  //
  // MEDIDO (fifa09 0x15950248, nfs 0xe59fa250): sem isto, o slot fica com o que
  // a fase de CARGA la deixou, e `e.create = mem.Ler32(kPPObj) != 0` passa a
  // medir "o slot tem lixo", nao "o `CreateInstance` criou o applet". O
  // `AEEStaticMod_New` do SDK faz `*ppMod = NULL;` na primeira linha; isto e o
  // mesmo, do lado do instrumento.
  mem.Escrever32(kPPObj, 0);
  cpu.Repor(ci, kPilha);
  // ASSINATURA MEDIDA, por desmonte e por traco de registradores:
  //   r0 = po (o modulo), r1 = pIShell, r2 = ClsId, r3 = ppApplet
  //
  // Como se sabe, e vale escreve-lo porque custou varias hipoteses erradas:
  // `0x100644` faz `mov lr,r2 / mov r2,r0` e salta para `0x1005f4` com
  // `(r0=lr, r1, r2=po, r3)`. O `0x1005f4` compara r0 com o CLSID e chama
  // `0x104980(16, r1, r2, r3)` -- que e o `AEEApplet_New(dwSize, pIShell,
  // pIModule, ppApplet)` classico, com `movs r8,r3` a guardar o ppApplet e
  // `cmpne r6,#0` a EXIGIR r1 (o pIShell) diferente de zero.
  //
  // Eu passava r1 = 0 e o `AEEApplet_New` devolvia 1 em tres instrucoes. Nao era
  // o CLSID nem a ordem dos argumentos: era o SHELL em falta.
  cpu.Set(kR0, modulo);
  cpu.Set(kR1, kShell);
  cpu.Set(kR2, clsid_do_titulo);
  cpu.Set(kR3, kPPObj);
  cpu.Set(kLR, kSentinela);
  std::string motivo_create;
  {
    const zb2::brew::ResultadoFase r = g_despacho->Correr(cpu, limite, kPPObj);
    e.passos_create = r.passos;
    motivo_create = r.motivo;
  }
  e.recusadas = cpu.InstruscoesRecusadas();
  g_applet = mem.Ler32(kPPObj);  // para o `GetAppInstance`
  g_despacho->DefinirApplet(g_applet);
  e.create = mem.Ler32(kPPObj) != 0;

  e.motivo += " | create:" + motivo_create;
  if (!e.create) e.motivo += "_sem_applet";
  // A CONTAGEM DE FALTAS FICA PARA O FIM, e nao aqui.
  //
  // MEDIDO, e foi o sub-agente `widget` que o viu: aqui a contagem era guardada
  // ANTES da fase do `EVT_APP_START`. **Tudo o que um titulo pede ao ARRANCAR -- que
  // e onde toda a construcao de interface acontece -- nao entrava na linha nem no
  // JSON.** O `IRootForm::slot3` nunca apareceu na bateria por isto, e so se viu no
  // traco. E um defeito de instrumento (P7), e o pior tipo: fazia parecer que os
  // titulos nao pediam nada ao arrancar.
  // O EVENTO DE ARRANQUE (ver `kEventosPorOmissao`).
  int eventos_dados = 0;
  if (g_eventos != 0 && g_applet != 0) {
    const std::uint32_t vtable = mem.Ler32(g_applet);
    const std::uint32_t handle_event = mem.Ler32(vtable + 8);  // slot 2
    if (handle_event >= kBase && handle_event < kBase + e.tamanho) {
      // `AEEAppStart`, lida de `platform/system/inc/AEEAppStart.h`:
      //
      //     typedef struct {
      //        int         error;      // +0
      //        AEECLSID    clsApp;     // +4
      //        IDisplay *  pDisplay;   // +8
      //        AEERect     rc;         // +12  (x, y, dx, dy)
      //        const char *pszArgs;    // +28
      //     } AEEAppStart;
      //
      // **E VAI NO `dwp`, QUE E O `r3` -- nao no `wp`.** O cabecalho e explicito:
      // "A pointer to this structure is passed to applications in the `dwParam`
      // field, upon `EVT_APP_START`". A versao anterior punha-o no `r2` (`wp`, um
      // `uint16`): um ponteiro num campo de 16 bits, e o `r3` a zero.
      //
      // O `wp` e a "Bitmask of start codes" (`AEE_START_OEM`/`RESTART`/`SSAVER`) e
      // vai a ZERO: um arranque normal nao tem nenhum desses bits.
      //
      // Os valores do `AEEAppStart` sao os DECLARADOS em todo o lado: `pDisplay` e o
      // objecto do `IDisplay` e `rc` e o ECRA INTEIRO (`core/brew/ecra.h`), o mesmo
      // que o `IShell::GetDeviceInfo` publica. Onde nao ha medicao, esta dito.
      constexpr std::uint32_t kAppStart = 0x000A0000u + 0x1000u;
      for (std::uint32_t k = 0; k < 32; ++k) mem.Escrever8(kAppStart + k, 0);
      mem.Escrever32(kAppStart + 0, 0);  // error
      mem.Escrever32(kAppStart + 4, clsid_do_titulo);
      mem.Escrever32(kAppStart + 8, zb2::brew::kObjDisplay);
      mem.Escrever32(kAppStart + 12, 0);
      mem.Escrever32(kAppStart + 16, 0);
      mem.Escrever32(kAppStart + 20, zb2::brew::kLarguraDoEcra);
      mem.Escrever32(kAppStart + 24, zb2::brew::kAlturaDoEcra);
      mem.Escrever32(kAppStart + 28, 0);  // pszArgs
      cpu.Set(kR0, g_applet);
      cpu.Set(kR1, 0);          // EVT_APP_START
      cpu.Set(kR2, 0);          // wp = 0 bits de start code
      cpu.Set(kR3, kAppStart);  // dwp = AEEAppStart*
      cpu.Set(kLR, kSentinela);
      cpu.Set(kPC, handle_event);
      const zb2::brew::ResultadoFase re = g_despacho->Correr(cpu, limite, kPPObj);
      e.passos_start = re.passos;  // a fase do arranque, contada
      e.motivo += " | start:" + re.motivo;
      ++eventos_dados;
    } else {
      e.motivo += " | sem_handle_event";
    }
  }
  e.eventos = eventos_dados;

  // O LACO DE QUADRO, quando pedido. Cada volta poe o callback de temporizador
  // armado pelo titulo no PC e corre-o ate ele voltar a sentinela -- e o proprio
  // callback re-arma o seguinte, que e como um laco de quadro se sustenta em BREW.
  int quadros_corridos = 0;
  for (int q = 0; q < g_quadros; ++q) {
    if (!g_despacho->PrepararCallbackDoTemporizador(cpu)) break;  // o laco acabou
    const zb2::brew::ResultadoFase rq = g_despacho->Correr(cpu, limite, kPPObj);
    ++quadros_corridos;
    if (rq.motivo != "retornou") {
      e.motivo += " | quadro:" + rq.motivo;
      break;
    }
  }
  e.quadros = quadros_corridos;

  // O TRACO CRU, quando pedido (`ZB2_TRACE=1`). Existe para a pergunta que a
  // tabela NAO responde: um titulo que volta do arranque sem pedir nada que falte
  // -- nao se ve, pela tabela, o que ele ANDOU a fazer.
  if (g_trace) {
    std::fprintf(stderr, "== traco de %s (%zu eventos) ==\n", t.mod.c_str(), dm_eventos.size());
    for (const auto& ev : dm.eventos) {
      std::fprintf(stderr, "  [%s] %s %s\n", Nome(ev.area), ev.nome.c_str(), ev.detalhe.c_str());
    }
  }

  for (const auto& par : traco.ContagemFaltas()) e.faltas[par.first] = par.second;
  e.pixels = g_despacho->TelaRef().Escritos();
  e.cores = g_despacho->TelaRef().CoresDistintas();
  e.textos = g_textos;
  e.blits = g_blits;
  dm_eventos = dm.eventos;
  return e;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "uso: %s <corpus.json> <dir_dos_mods> [saida.json]\n", argv[0]);
    return 2;
  }
  const std::vector<Titulo> titulos = LerCorpus(argv[1]);
  if (titulos.empty()) { std::fprintf(stderr, "corpus vazio ou ilegivel\n"); return 2; }

  std::printf("%-16s %-8s %-6s %-6s %-6s %8s %8s %8s %5s  %s\n", "titulo", "tamanho", "carga",
              "modulo", "vtable", "carga_p", "cria_p", "QUADROS", "PIXELS", "CORES", "motivo");
  int carregam = 0, com_modulo = 0, com_applet = 0;
  // Nome -> conjunto de detalhes distintos vistos (para a lista final dizer os
  // ARGUMENTOS, e nao so a contagem).
  std::map<std::string, std::uint64_t> faltas_totais;
  std::map<std::string, std::map<std::string, std::uint64_t>> faltas_detalhe;
  // O CABECALHO DE PROVENIENCIA, e nao so a lista de fichas.
  //
  // Vem de um achado do sub-agente `regressoes`: o comparador deriva a
  // configuracao da lista de `pasta/mod`, logo **dois corpus diferentes que
  // mantenham os mesmos 62 pasta/mod sao indistinguiveis**. Duas dumps da mesma
  // ROM dao o mesmo corpus e bytes diferentes.
  //
  // O `build` vem do AMBIENTE e nao de um `git` invocado daqui: o instrumento nao
  // le o sistema por sua conta (P4 e P7). Se ninguem o disser, fica
  // `desconhecido`, e isso e uma resposta honesta.
  bool ok_corpus = false;
  const std::vector<std::uint8_t> bytes_corpus = Ler(argv[1], &ok_corpus);
  const char* ambiente_build = std::getenv("ZB2_BUILD");
  const std::string build = (ambiente_build != nullptr && ambiente_build[0] != 0)
                                ? ambiente_build
                                : "desconhecido";
  // O HASH DO BINARIO -- porque o NOME DO COMMIT MENTE.
  //
  // MEDIDO pelo sub-agente da auditoria: a referencia da bateria dizia
  // `build=eb62459` e os dados so podiam vir do codigo de `0286921`. A corrida foi
  // feita com a ARVORE SUJA e o campo guardou o commit do checkout, nao o codigo que
  // produziu o binario. **O comparador nao apanha isto, porque `build` e neutro por
  // desenho -- e um campo que nao identifica o que mediu nao serve como
  // proveniencia.**
  //
  // O hash do executavel identifica exactamente o que produziu os numeros: um
  // binario diferente da outro hash, e o mesmo binario da sempre o mesmo. E vem do
  // ambiente pela mesma razao que o `build`: **o instrumento nao le o sistema por
  // sua conta.**
  const char* ambiente_binario = std::getenv("ZB2_BINARIO");
  const std::string binario = (ambiente_binario != nullptr && ambiente_binario[0] != 0)
                                  ? ambiente_binario
                                  : "desconhecido";
  std::string json = "{\n  \"config\": {\"corpus_sha256\": \"" +
                     (ok_corpus ? zb2::brew::Sha256Hex(bytes_corpus.data(), bytes_corpus.size())
                                : std::string("desconhecido")) +
                     "\", \"titulos\": " + std::to_string(titulos.size()) + ", \"build\": \"" +
                     build + "\", \"binario_sha256\": \"" + binario + "\"},\n  \"titulos\": [\n";
  for (const Titulo& t : titulos) {
    dm_eventos.clear();
  const Estado e = Medir(t, argv[2]);
    if (e.carga) ++carregam;
    if (e.modulo) ++com_modulo;
    if (e.create) ++com_applet;
    for (const auto& par : e.faltas) faltas_totais[par.first] += par.second;
    for (const auto& ev : dm_eventos) {
      if (ev.nome.rfind("NAO_IMPLEMENTADO: ", 0) == 0) {
        faltas_detalhe[ev.nome.substr(18)][ev.detalhe]++;
      }
    }
    std::printf("%-16s %-8u %-6s %-6s %-6s %8" PRIu64 " %8" PRIu64 " %6d %8u %5u  %s\n",
                t.mod.c_str(), e.tamanho, e.carga ? "sim" : "NAO", e.modulo ? "sim" : "NAO",
                e.vtable ? "sim" : "NAO", e.passos_carga, e.passos_create, e.quadros, e.pixels,
                e.cores, e.motivo.c_str());
    json += "  {\"mod\":\"" + t.mod + "\",\"pasta\":\"" + t.pasta + "\",\"tamanho\":" +
            std::to_string(e.tamanho) + ",\"carga\":" + (e.carga ? "true" : "false") +
            ",\"modulo\":" + (e.modulo ? "true" : "false") +
            ",\"vtable\":" + (e.vtable ? "true" : "false") +
            ",\"applet\":" + (e.create ? "true" : "false") +
            ",\"passos_carga\":" + std::to_string(e.passos_carga) +
            ",\"passos_create\":" + std::to_string(e.passos_create) +
            ",\"recusadas\":" + std::to_string(e.recusadas) +
            ",\"passos_start\":" + std::to_string(e.passos_start) + ",\"motivo\":\"" + e.motivo + "\"" +
            ",\"pixels\":" + std::to_string(e.pixels) +
            ",\"cores\":" + std::to_string(e.cores) +
            ",\"textos\":" + std::to_string(e.textos) +
            ",\"blits\":" + std::to_string(e.blits) +
            // AS FALTAS POR TITULO, e nao so o total agregado.
            //
            // Foi a falta disto que me obrigou a adivinhar quais dos 62 titulos
            // fazia uma chamada -- e a adivinhacao custou uma ronda. A lista
            // agregada diz O QUE falta; so a lista por titulo diz QUEM pede.
            ",\"faltas\":{" + [&] {
              std::string s;
              bool primeiro = true;
              for (const auto& par : e.faltas) {
                s += (primeiro ? "" : ",");
                s += "\"" + par.first + "\":" + std::to_string(par.second);
                primeiro = false;
              }
              return s;
            }() + "}},\n";
  }
  // O JSON tem de ser VALIDO: uma virgula a mais no fim torna-o ilegivel para
  // quem o for ler, e ele existe exactamente para ser comparado entre corridas.
  // O texto termina em "},\n", logo a virgula esta em `size-2` e nao em
  // `size-3` -- errei o indice a primeira vez e o JSON continuou invalido.
  if (json.size() > 3 && json[json.size() - 2] == ',') json.erase(json.size() - 2, 1);
  json += "  ]\n}\n";

  std::printf("\n== %d titulos | carga %d | ponteiro de modulo %d | applet %d ==\n", (int)titulos.size(),
              carregam, com_modulo, com_applet);
  std::printf("== o que falta, por DEMANDA (o que os titulos pedem, por ordem) ==\n");
  std::vector<std::pair<std::uint64_t, std::string>> ordenado;
  for (const auto& par : faltas_totais) ordenado.push_back({par.second, par.first});
  std::sort(ordenado.begin(), ordenado.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });
  for (const auto& par : ordenado) {
    std::printf("   %-34s pedido %" PRIu64 "x\n", par.second.c_str(), par.first);
    for (const auto& d : faltas_detalhe[par.second]) {
      std::printf("        %-46s %" PRIu64 "x\n", d.first.c_str(), d.second);
    }
  }
  if (argc > 3) {
    std::ofstream out(argv[3]);
    out << json;
    std::printf("\nestado escrito em %s\n", argv[3]);
  }
  return 0;
}

