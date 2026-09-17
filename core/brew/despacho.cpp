#include "core/brew/despacho.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "core/audio/misturador.h"
#include "core/brew/ajudantes_extra.h"
#include "core/brew/classes.h"
#include "core/brew/clsids.h"
#include "core/brew/formato.h"
#include "core/brew/imedia.h"
// O `Inflar` (RFC1950) dos `.pkg` -- REUSADO, e nao copiado: core/carga e de
// outro agente, e so se le daqui. O gzip (RFC1952) dos `.bar` e tratado neste
// ficheiro, por cima dele.
#include "core/carga/inflate.h"
#include "core/carga/mod.h"
// O DESCODIFICADOR DE PNG (`DescodificarPng`), para o `LoadResObject`: o
// `LoadResDataEx` entrega o bloco CRU e o `LoadResObject` entrega um OBJECTO
// desenhavel, e o unico formato de imagem que esta arvore sabe virar pixels e
// este (`core/carga/png.h`). Nao se acrescenta descodificador nenhum a
// `core/carga`: quem nao for PNG e recusado COM O NOME.
#include "core/carga/png.h"

// ---------------------------------------------------------------------------
// ZB2_PC_HOT -- ONDE E QUE O GUEST GASTA O TEMPO. O instrumento que faltava.
// ---------------------------------------------------------------------------
//
// A tabela de faltas diz o que os titulos PEDEM a esta arvore. Quando ela fica
// VAZIA (41 dos 62, medido) e o titulo continua sem desenhar, ela nao tem mais
// nada para dizer: a parede passou a ser o que o guest FAZ com o estado que tem.
// Para esses, a pergunta e "onde e que ele esta a gastar os passos", e a resposta
// e um histograma de PCs.
//
// AMOSTRAGEM, e nao todos os passos: um `++` por instrucao num `std::map` custa
// mais do que a propria instrucao. `ZB2_PC_HOT=<n>` amostra um passo em cada n
// (4096 chega para ver onde o tempo esta) e imprime no fim de cada fase os 12 PCs
// mais quentes, com a PALAVRA que la estava -- a palavra e o que permite desmontar
// o sitio sem outra corrida.
// O TRECHO PENDENTE: o tecto de instrucoes DEVOLVE A VEZ, e nao perde o trabalho.
//
// A FONTE e o `zeebx` do Kaio (`1026fb7`, branch `development`): "*O Zeebo Extreme
// Rolima roda o carregamento inteiro dentro do `EVT_APP_START` sem ceder a vez: o
// corte descartava o trecho e a sessao terminava sozinha aos 3,8 s*". Ele guarda
// onde continuar e a volta seguinte RETOMA -- antes de qualquer outra coisa --
// com o MODO (o bit 0 do endereco diz Thumb) e os REGISTRADORES de entao (a volta
// ainda entrega sinais e callbacks, e entrar no guest para isso sobrescreve
// `r0`-`r3` e o `lr`).
//
// **O `Rolimaz` do nosso corpus e o mesmo caso** (pasta 276809): a `g2` destravou-o
// e ele bate no tecto de 16 M passos no `start`. Subir o tecto foi o que fizemos
// antes (a 16 M, medido); RETOMAR e a resposta certa -- o tecto deixa de ser um
// limite de trabalho e passa a ser so um pedido de vez.
//
// So o trecho de FORA e retomado: um trecho aninhado (um callback chamado de dentro
// do despacho de uma API) tem quem o espere do lado de ca, e esse quadro ja se foi.
// Por isso a marca e posta no corte do laco PRINCIPAL, e nao em qualquer saida.


std::map<std::uint32_t, std::uint64_t> g_pc_hist;
std::uint32_t g_pc_amostra = 0;
std::uint32_t g_pc_hist_lido = 0;
std::uint32_t LerPcHot() {
  if (g_pc_hist_lido == 0) {
    g_pc_hist_lido = 1;
    if (const char* e = std::getenv("ZB2_PC_HOT")) g_pc_amostra = std::strtoul(e, nullptr, 0);
  }
  return g_pc_amostra;
}
void DespejarPcHot(const char* fase) {
  if (g_pc_hist.empty()) return;
  std::vector<std::pair<std::uint64_t, std::uint32_t>> v;
  v.reserve(g_pc_hist.size());
  for (const auto& par : g_pc_hist) v.push_back({par.second, par.first});
  std::sort(v.begin(), v.end(), std::greater<std::pair<std::uint64_t, std::uint32_t>>());
  std::fprintf(stderr, "\n== ZB2_PC_HOT %s: %zu PCs distintos, amostra 1/%u ==\n", fase, v.size(),
               g_pc_amostra);
  for (std::size_t k = 0; k < v.size() && k < 12; ++k) {
    std::fprintf(stderr, "   pc=0x%08x  %llu amostras\n", v[k].second,
                 static_cast<unsigned long long>(v[k].first));
  }
  g_pc_hist.clear();
}

namespace zb2::brew {

// O TECTO DE SAIDAS SERVIDAS POR FASE.
//
// O 20000 nasceu para o laco de QUADRO (medido: ~19 saidas por quadro x 1051
// quadros ~= 20 000) e travava ciclos presos em saidas. MEDIDO na frente
// ibmap2: ha trabalho LEGITIMO que nao cabe la -- o `tekken2` descodifica SEIS
// imagens do `.bar` a `RGBToNative`+`DrawPixel` por byte, ~118 000 saidas so na
// primeira, e a fase morria a meio da descodificacao (pixels 322 M -> 307 200 e
// textos 2096 -> 0, o chamado "regressao da composicao" -- que era isto).
// O tecto sobe para 200 000: cabe a descodificacao medida com folga, e um ciclo
// preso continua a ser travado (e o tecto de PASSOS, 8 M, e o limite que
// sobrevive a tudo).
constexpr std::uint64_t kSaidasPorFase = 2000000;

namespace {

// Os IIDs que o corpus MEDIU como pedidos.
//
// OS VALORES VEM DO CABECALHO, e nao da memoria: `AEECLSID_DISPLAY` e
// `AEECLSID_CORE+1` = 0x01001001, e `AEECLSID_FILEMGR` = 0x01001003
// (`platform/system/inc/AEEClassIDs.h`).
//
// **Na migracao eu escrevi um valor errado para o Display, de memoria, e a bateria caiu
// de 22 applets para 1.** Os que faltam ficaram com objecto generico, e a
// diferenca so apareceu no numero final. Dai o teste que compara estes valores com
// a medicao estar em `tests/brew_test.cpp`.
constexpr std::uint32_t kIidDisplay = 0x01001001u;
constexpr std::uint32_t kIidDib = 0x01001045u;  // AEEIID_IDIB (AEEIDIB.h:37)
constexpr std::uint32_t kIidFileMgr = 0x01001003u;
constexpr std::uint32_t kIidHeap = 0x01001002u;
// 0x01001014 NAO E O IFile: e o `AEECLSID_UNZIPSTREAM` (`AEEClassIDs.h:82`,
// `AEECLSID_CORE + 20`, com `AEECLSID_CORE = QVERSION + 0x1000 = 0x01001000`).
//
// O IFile nao tem CLSID nenhum -- nao se cria por `CreateInstance`, nasce do
// `IFileMgr::OpenFile`. A constante foi posta aqui com o nome errado e entrou na
// lista do `QueryClass`, que passou a responder SIM a uma classe que o
// `CreateInstance` nao serve. **Dizer "suporto" e depois nao criar e pior do que
// recusar**: o titulo segue um caminho que assume ter descompressao.
//
// A constante aparece em NOVE dos 62 .mod (ddragonz, alpineracerex 6x,
// ridgeracer, tekken2, gof, rmp, zeeboids, allstarcards, pbc), logo nao e
// hipotetico. O `IUnzipAStream` (`AEEUnzipStream.h`) e a via do SDK para
// descomprimir, e nenhum de nos o serve ainda -- fica como frente propria, com
// nome certo.
constexpr std::uint32_t kClsidUnzipStream = 0x01001014u;
constexpr std::uint32_t kIidSound = 0x01001056u;
constexpr std::uint32_t kIidGraphics = 0x01002001u;
// `kIidRootForm` deixou de estar aqui: este ficheiro tinha uma copia local do
// mesmo numero que `core/brew/widget.h` declara, e **duas copias de um numero
// medido sao duas chances de ele divergir** -- foi assim que o `kAeeUnsupported`
// desta arvore passou a valer um codigo que nao existe em cabecalho nenhum. O
// valor passou a vir de um sitio so.
constexpr std::uint32_t kIidHid = 0x0106c411u;
constexpr std::uint32_t kIidSqlMgr = 0x0102c4e8u;
// As constantes que o despacho usa, todas derivadas dos cabecalhos gerados.
//
// OS CODIGOS DE ERRO JA NAO ESTAO AQUI. Estavam, com `kAeeUnsupported` a valer
// `0xE0000001` -- um valor que nao existe em cabecalho nenhum -- e a copia local
// ESCONDIA o enum de `ajudantes.h` (onde o `AEE_EUNSUPPORTED` e 20,
// AEEStdErr.h:36). Tirei a copia: agora o nome resolve para o enum, e ha um
// numero medido num sitio so.
constexpr std::uint32_t kSentinela = 0xFFFFFFF0u;
// O `EVTFLG_ASYNC` do `ISHELL_PostEvent`, MEDIDO em `AEEShell.h:48`. O
// `PostEvent` e O MESMO SLOT do `SendEvent` (`AEEShell.h:279`), e a unica coisa
// que os distingue e este bit.
constexpr std::uint32_t kEvtflgAsync = 0x0002u;
// O ORCAMENTO DE PASSOS DE UMA ENTREGA DE EVENTO. Nao e o da fase, para o motivo
// da saida distinguir "o tratador nao voltou" de "a fase esgotou"; mas os passos
// gastos SAO somados aos da fase (ver `EntregarEventoAoApplet`), porque a entrega
// corre codigo do titulo e nao pode ser tempo de graca. zeebx usa 10 000 000
// (`QSORT_BUDGET`, `src/machine/mod.rs:1549`).
constexpr std::uint64_t kLimiteDoEvento = 4000000ull;
// A base do modulo. MEDIDA: ver `tests/mod_base_test.cpp` e o `bateria.cpp`.
constexpr std::uint32_t kBase = 0x00000000u;
constexpr int kOrcamentoSegundos = 25;
// Quanto a MIDIA anda por milissegundo do relogio virtual. O valor e o do modulo
// de midia (`Media::kAmostrasPorMs`, 22 = 22050/1000 truncado); escreve-se por
// extenso aqui para a constante do motor e a do modulo nao poderem divergir sem
// alguem ler isto.
constexpr std::uint32_t kAmostrasDeMidiaPorMs = zb2::brew::Media::kAmostrasPorMs;
constexpr std::uint32_t kSlotIdStrlen = 1503, kSlotIdMemset = 1504, kSlotIdStrcpy = 1505;
// `strcat` (0x00C) e `sleep` (0x184) da `AEEHelperFuncs`. Sao as duas ajudantes
// que MAIS TITULOS pediam depois desta ronda: `strcat` em 10 dos 62 e `sleep` em
// 9. A medida que conta e TITULOS AFECTADOS, e nao pedidos -- o `IFile::Read`
// tinha 70 pedidos e era UM titulo so (`allstarcards`).
constexpr std::uint32_t kSlotIdStrcat = 1568, kSlotIdSleep = 1569;
// `strncpy` (0x0C8) e `strstr` (0x0D8). O zeebulator ja as tinha, e foi a
// comparacao com ele que as trouxe -- mas com DOIS OFFSETS TROCADOS do lado
// dele, ver o comentario no ramo do `strstr`.
constexpr std::uint32_t kSlotIdStrncpy = 1570, kSlotIdStrstr = 1571;
// AVISO A QUEM ACRESCENTAR O PROXIMO: estes numeros sao um espaco PARTILHADO e
// nao ha nada que impeca dois de colidirem. Ja aconteceu: duas frentes desta
// sessao escolheram 1568 (o `strcat` e o `IBitmap::GetInfo`), e o sintoma foi um
// teste de bitmap a chamar a implementacao do `strcat`. Confirma com
// `grep -n "= 15[0-9][0-9]" core/brew/despacho.cpp` antes de escolher.
constexpr std::uint32_t kSlotIdMemmove = 1506, kSlotIdStrcmp = 1507, kSlotIdStrchr = 1508;
constexpr std::uint32_t kSlotIdStrtowstr = 1500, kSlotIdGetAeeVersion = 1501,
                       kSlotIdAeeGetRand = 1502;
constexpr std::uint32_t kSlotIdGetUpTime = 1540, kSlotIdQueryClass = 1541,
                       kSlotIdGetAppInstance = 1543;
constexpr std::uint32_t kSlotIdFmTest = 1510, kSlotIdFmFree = 1511, kSlotIdFmLastErr = 1512;
constexpr std::uint32_t kSlotIdSetTimer = 1520;
constexpr std::uint32_t kSlotIdGetFontMetrics = 1530, kSlotIdMeasureText = 1531,
                       kSlotIdDrawText = 1532, kSlotIdDrawRect = 1533, kSlotIdBitBlt = 1534,
                       kSlotIdSetColor = 1535, kSlotIdSetClipRect = 1536, kSlotIdUpdate = 1537,
                       kSlotIdCreateDIBitmap = 1538, kSlotIdBacklight = 1542;
constexpr std::uint32_t kSlotIdMkDir = 1544,
                       kSlotIdBitmapQI = 1565, kSlotIdBitmapGetInfo = 1572,
                       kSlotIdRemove = 1509,
                       kSlotIdGetDest = 1545,
                       kSlotIdSetDest = 1546, kSlotIdRmDir = 1547, kSlotIdGetDeviceInfo = 1549,
                       kSlotIdGetDeviceBitmap = 1550, kSlotIdGetClipRect = 1551,
                       kSlotIdCancelTimer = 1552, kSlotIdSqlOpen = 1553, kSlotIdOpenFile = 1554,
                       // O `Exec` do `ISQLDatabase`: NAO e um id avulso na faixa 15xx.
                       // O objecto do banco e construido por `ConstruirObjeto` com a
                       // `base_dos_slots` IGUAL a propria vtable (`kVtableSqlDb`), logo
                       // o indice de saida do slot `k` e `kVtableSqlDb + k` -- UMA so
                       // fonte para a cablagem e para o despacho, e nenhum numero
                       // escrito a mao nos dois lados (a armadilha das duas copias).
                       kSlotIdSqlExec = kVtableSqlDb + 3,
                       kSlotIdFileRead = 1555, kSlotIdFileSeek = 1556, kSlotIdFileInfo = 1557,
                       kSlotIdFileRelease = 1558, kSlotIdFileWrite = 1559;
constexpr std::uint32_t kSlotIdSprintf = 1560, kSlotIdVsprintf = 1561, kSlotIdHeapLock = 1562,
                       kSlotIdVsnprintf = 1566, kSlotIdRealloc = 1567,
                       kSlotIdFreeResData = 1563, kSlotIdCheckPriv = 1564;
// 1581, e nao 1600+: o ramo-faixa do Unzip (`1599..1656`, linha ~3560) engole
// qualquer id ali antes dos ramos de igualdade -- medido, com este proprio
// `SetFont` a cair no `Unzip slot 8` em vez do seu ramo (nona ocorrencia do
// erro de ordem desta arvore). 1581 esta abaixo de todas as faixas.
constexpr std::uint32_t kSlotIdDisplaySetFont = 1581;  // IDisplay slot 17 (QW fontes: 3x no `ddragonz`)
constexpr std::uint32_t kSlotIdGetTimerExpiration = 1600;  // IShell slot 13 (QW4: 1 pedido no `zenonia`)
constexpr std::uint32_t kSlotIdHeapCheckAvail = 9006;      // IHeap slot 6 (QW4: 1 pedido no `bio4_brew`)
static_assert(kSlotIdHeapCheckAvail == zb2::brew::VtGenerico(0) + 6,
              "o CheckAvail e o slot 6 da vtable generica do IHeap (9000+6)");
// O `dbgprintf` (AEEHelperFuncs 0x09c). **MEDIDO: ele estava a correr o
// `strtowstr`, e a ESCREVER na memoria do titulo.**
//
// O id dele era `kBaseDoSlot + 500`. `kBaseDoSlot` e 1000, logo o id era 1500 --
// e `kSlotIdStrtowstr` TAMBEM e 1500. Dois offsets do `AEEHelperFuncs` (0x040 e
// 0x09c) apontavam para o MESMO endereco de saida, e o ramo do `strtowstr` vem
// primeiro no `if/else` do despacho: **toda a chamada a `dbgprintf` de todo o
// titulo corria o `strtowstr`**.
//
// O ESTRAGO, medido no `cninja` (sonda no `Despacho::Correr`, ver
// `/tmp/pesquisa/12-gl.md`): o titulo chama
//     dbgprintf(fmt, 4, "c:/my_code/emulator_neo/framework/ctordtor.cpp", 121)
// e o `strtowstr` le r0 como ORIGEM e **r1 como DESTINO**. r1 = 4. A mensagem
// `"eglGetProcAddress (NBI) - platform does not support EGLSurfaceManip
// interface"` foi escrita em UTF-16 por cima de `0x00000004..0x0000009c` -- que
// e o CODIGO do proprio modulo (a base e ZERO). Doze instrucoes depois um
// `ldr pc,[r3,#0xd8]` (o `strstr` da tabela, lida da zona destruida) saltou para
// o PC=0 e o titulo moeu dados ate ao fim do orcamento.
//
// 1580 e o primeiro id da faixa que NENHUM outro usa (conferido com
// `grep -n "= 15[0-9][0-9]" core/brew/despacho.cpp`), e a guarda
// `SemIdsRepetidos` abaixo passa a recusar a proxima colisao no arranque.
constexpr std::uint32_t kSlotIdDbgPrintf = 1580;
// A FRENTE io2 (etapa 12): IUnzipAStream e IMemAStream, servidos a serio.
//
// Os objectos e as vtables sao construidos no `InstalarAjudantes` (indices
// 15000/15010, objectos 0x80060700/0x80060800 -- a faixa dos genericos e de
// OUTRO agente). Os ids de saida desta frente estao em 1590+, conferidos livres
// por `grep "= 15[0-9][0-9]"` antes de escolher. Os slots NAO servidos das duas
// interfaces apontam para faixas proprias (1599+ e 1660+) que RECUSAM com o
// nome do SDK em vez de cairem no ramo dos ajudantes.
constexpr std::uint32_t kClsidMemAStream = 0x0100100cu;  // AEEClassIDs.h:75
// (o kClsidUnzipStream = 0x01001014u ja vive no topo deste ficheiro)
constexpr std::uint32_t kSlotIdUnzipReadable = 1590;
constexpr std::uint32_t kSlotIdUnzipRead = 1591;
constexpr std::uint32_t kSlotIdUnzipCancel = 1592;
constexpr std::uint32_t kSlotIdUnzipSetStream = 1593;
constexpr std::uint32_t kSlotIdMemStreamReadable = 1594;
constexpr std::uint32_t kSlotIdMemStreamRead = 1595;
constexpr std::uint32_t kSlotIdMemStreamCancel = 1596;
constexpr std::uint32_t kSlotIdMemStreamSet = 1597;
constexpr std::uint32_t kSlotIdMemStreamSetEx = 1598;
constexpr std::uint32_t kSlotIdUnzipSlots = 1599;  // 58 slots: 6..63 da vtable
constexpr std::uint32_t kSlotIdMemStreamSlots = 1660;  // 57 slots: 7..63
constexpr std::uint32_t kVtUnzip = 15000;
constexpr std::uint32_t kVtMemStream = 15010;
constexpr std::uint32_t kObjUnzip = 0x80060700u;
constexpr std::uint32_t kObjMemStream = 0x80060800u;
constexpr std::uint32_t kBaseDoSlot = 1000;
// A LARGURA DECLARADA DE UM CARACTERE no `DrawText` sem fonte carregada. Nao e
// uma medida de fonte nenhuma: e a aproximacao que este modulo assume, dita uma
// vez para os dois sitios que a usam (o comprimento e a barra).
constexpr std::uint32_t kLarguraDoCaractere = 8;
// Os itens da paleta do IDisplay (`AEEIDisplay.h:139-142,165`): 1 = CLR_USER_TEXT,
// 2 = CLR_USER_BACKGROUND, 3 = CLR_USER_LINE (= CLR_USER_FRAME).
constexpr std::uint32_t kClrUserBackground = 2, kClrUserLine = 3;

// Uma linha da tabela de ajudantes: o offset no `AEEHelperFuncs` e o endereco de
// saida da implementacao.
// OS OFFSETS DESTA TABELA VEM DO CABECALHO, e nao da mao.
//
// DOIS DELES ESTAVAM NO SITIO ERRADO, e nada acusou: `kSlotStrtowstr` valia
// 0x0a0 -- que e o `wstrcompress`; o `strtowstr` verdadeiro e 0x040 -- e
// `kSlotAeeGetRand` valia 0x090, que e o `atoi`; o `aee_GetRand` e 0x0a8. O
// despacho servia um `strtowstr` no sitio do `wstrcompress` e bytes aleatorios
// no sitio do `atoi`. Os numeros certos JA estavam escritos em
// `tools/bateria.cpp` -- a SEGUNDA copia deles divergiu desta, e nao havia nada
// a comparar as duas. **Duas copias de um numero medido sao duas chances de ele
// divergir.** (Medido em `platform/system/inc/AEEStdLib.h`, campos 10, 16, 36 e
// 42; ver `tools/ajudantes_slots.inc`, gerado.)
// O BIT ALTO DO TAMANHO DE UM `malloc`: "nao zeres" (`AEEStdLib.h:547`).
constexpr std::uint32_t kAllocNoZmem = 0x80000000u;
constexpr std::uint32_t kSlotDbgPrintf = brew_ajudantes::kAjudante_dbgprintf;
constexpr std::uint32_t kSlotStrlen = brew_ajudantes::kAjudante_strlen;
constexpr std::uint32_t kSlotMemset = brew_ajudantes::kAjudante_memset;
constexpr std::uint32_t kSlotStrcpy = brew_ajudantes::kAjudante_strcpy;
constexpr std::uint32_t kSlotStrcat = brew_ajudantes::kAjudante_strcat;
constexpr std::uint32_t kSlotStrncpy = brew_ajudantes::kAjudante_strncpy;
constexpr std::uint32_t kSlotStrstr = brew_ajudantes::kAjudante_strstr;
constexpr std::uint32_t kSlotSleep = brew_ajudantes::kAjudante_sleep;
constexpr std::uint32_t kSlotStrcmp = brew_ajudantes::kAjudante_strcmp;
constexpr std::uint32_t kSlotStrchr = brew_ajudantes::kAjudante_strchr;
constexpr std::uint32_t kSlotMemmove = brew_ajudantes::kAjudante_memmove;
constexpr std::uint32_t kSlotStrtowstr = brew_ajudantes::kAjudante_strtowstr;
constexpr std::uint32_t kSlotGetAeeVersion = brew_ajudantes::kAjudante_GetAEEVersion;
constexpr std::uint32_t kSlotAeeGetRand = brew_ajudantes::kAjudante_aee_GetRand;
static_assert(kSlotStrtowstr == 0x040, "0x040 e strtowstr; 0x0a0 e wstrcompress");
static_assert(kSlotAeeGetRand == 0x0a8, "0x0a8 e aee_GetRand; 0x090 e atoi");

struct LigacaoAjudante {
  std::uint32_t off;
  std::uint32_t saida;
};
}  // namespace

Despacho::Despacho(Memoria& mem, Traco& traco, Alocador& alocador, Vfs& vfs)
    : mem_(mem),
      traco_(traco),
      cheats_(mem_, traco_),
      al_(alocador),
      vfs_(vfs),
      arquivos_(&vfs),
      // A pasta do título só fica conhecida em `SituarTitulo`, depois do
      // construtor. A lambda lê dir_/pasta_ NO MOMENTO DO PEDIDO; não captura uma
      // cópia vazia agora. E usa a mesma VFS do OpenFile, uma só verdade sobre o
      // que existe no pacote.
      recursos_(mem, alocador,
                 [this](const std::string& ficheiro, std::vector<std::uint8_t>* bytes,
                        std::string* motivo) {
                   return LeitorDaPasta(dir_ + "/" + pasta_, &vfs_)(ficheiro, bytes, motivo);
                 },
                 &traco),
      sinais_(mem, traco),
      ihid_(mem, traco, sinais_, entrada_),
      // Ordem igual à DECLARAÇÃO em despacho.h: C++ constrói por declaração,
      // não pela ordem que parece aqui. O -Wreorder apanhou esta divergência.
      igl_(mem, traco),
      egl_(mem, traco),
      widgets_(mem, traco) {}

// A INSTALACAO DO GL. Devolve quantos slots foram cablados NO TOTAL (0 = falhou).
//
// AS FAIXAS SAO 30000 E 31000, e nao 20000/21000: a faixa 20000 esta ocupada pela
// ENTRADA (`tools/bateria.cpp` instala-a em 20000; 32 + 16 slots, de 20000 a 20047)
// e as duas escrevem nos MESMOS enderecos. Os numeros medidos estao no comentario
// das constantes em `core/brew/igl.h`.
std::uint32_t Despacho::InstalarGl(const Saidas& saidas) {
  const std::uint32_t a = igl_.Instalar(saidas);
  const std::uint32_t b = egl_.Instalar(saidas);
  if (a == 0 || b == 0) return 0;
  // DOIS OBJECTOS NO MESMO ENDERECO dariam uma vtable a servir as duas interfaces.
  // Cada `Instalar` confere a sua vtable; nada confere que os dois objectos sao
  // distintos, e um `if` custa menos do que uma ronda a olhar para o sitio errado.
  if (igl_.Objeto() == egl_.Objeto()) {
    traco_.RegistarFalta(Area::Video, "cablagem_do_GL",
                         "o IGL e o IEGL ficaram no mesmo endereco de objecto");
    return 0;
  }
  traco_.Emitir(Area::Video, Nivel::Informacao, "GL_CABLADO",
                "IGL em 0x800B0000 (faixa 30000) e IEGL em 0x800B1000 (faixa 31000)");
  // A TELA DO DESENHO. O rasterizador (`core/video/rasterizador.cpp`) escreve
  // AQUI, e nao num buffer proprio: a medida `PIXELS`/`CORES` do
  // `tools/bateria.cpp` le a `Tela` do `Despacho`, e uma tela propria dentro do
  // IGL daria um numero que nao mede o que o titulo escreveu no ecra.
  //
  // Sem esta linha o `glDrawArrays` RECUSA com o motivo escrito ("o IGL NAO TEM
  // TELA LIGADA"), e nenhum pixel aparece -- e o unico remendo que a etapa do
  // rasterizador precisa em ficheiro partilhado.
  igl_.DefinirTela(&tela_);
  return a + b;
}

bool Despacho::InstalarWidgets(const Saidas& saidas) {
  // IDEMPOTENTE: quem dirige o titulo pode chamar isto, e o `InstalarAjudantes`
  // ja o chamou. Uma segunda chamada nao e um erro -- mas tambem nao pode
  // REGISTAR uma falta, que seria um erro inventado pelo instrumento.
  if (widgets_prontos_) return true;
  std::string motivo;
  if (!widgets_.Construir(saidas, &motivo)) {
    // RECUSA RUIDOSA (P2): o `IRootForm` continua a existir, mas os slots dele
    // recusam com nome -- e nao devolvem sucesso sem fazer nada.
    traco_.RegistarFalta(Area::Brew, "os widgets nao foram construidos", motivo);
    return false;
  }
  // O DESTINO DO DESENHO 2D: quem sabe escrever o cabecalho do IDIB do ecra e o
  // proprio despacho (`EscreverCabecalhoDoBitmapDoEcra`, que ja era chamado pelo
  // `IDisplay::GetDestination` e pelo `GetDeviceBitmap`). O `IGraphics` do widget
  // desenha no buffer do ecra e a `Tela` absorve-o no `Update` -- a mesma via dos
  // outros, e nao um segundo framebuffer.
  widgets_.GraficosRef().DefinirCriadorDoEcra(
      [this]() -> std::uint32_t { return EscreverCabecalhoDoBitmapDoEcra(); });
  widgets_prontos_ = true;
  return true;
}

bool Despacho::AtenderWidgets(ICpu& cpu, std::uint32_t indice) {
  if (!widgets_prontos_) return false;
  if (!widgets_.EMeu(indice)) return false;
  // Os DOIS resultados contam como "atendido": `Feito` nao regista nada, e
  // `NaoImplementado` ja registou o nome do que falta. O que NAO pode acontecer
  // e cair no ramo generico e ser nomeado outra vez -- o registo diria
  // `IRootForm::slot3` quando o que faltou foi, por exemplo, o `Draw` de um
  // widget.
  (void)widgets_.Atender(cpu, indice);
  return true;
}

// ---------------------------------------------------------------------------
// O SQL: o `ISQLMgr` e o `ISQLDatabase` que ele devolve
// ---------------------------------------------------------------------------
//
// O QUE O Z-WHEEL FAZ, MEDIDO com o traco (`ZB2_TRACE=1`, `tectoy`, 274755) e
// conferido contra a sonda do `zeebx`
// (`zeebx-emu/docs/implementacao/13-classes-desconhecidas.md`):
//
//   OpenDatabase("tt_prefs.db", &pdb)                       -> o gestor devolve um banco
//   Exec(pdb, "PRAGMA integrity_check", cb, x)              -> o banco executa
//   Exec(pdb, "SELECT version, subversion FROM DBINFO", cb, x)
//
// Sao as duas assinaturas do `sqlite3_open`/`sqlite3_exec`, e o `Exec` leva um
// PONTEIRO DE FUNCAO (`r2`, dentro da faixa de codigo do modulo) e um contexto
// (`r3`): e o `sqlite3_exec` com o callback do jogo.
//
// A ORDEM DOS SLOTS NAO VEIO DE CABECALHO -- nao ha `AEEISQL.h` no SDK 4.0.2 nem
// no 7.12.5 (conferido). Veio das duas medicoes independentes, e as duas dizem o
// MESMO: os tres primeiros slots sao o `IQI` de sempre e o slot 3 e o `Open`
// (aqui) / o `Exec` (no banco).
//
// O QUE ESTE MODULO NAO E: uma base de dados. E o SUBCONJUNTO MEDIDO de SQL que o
// Z-Wheel pede, e tudo o que sai desse conjunto e RECUSADO COM O TEXTO DA
// INSTRUCAO no registo.
bool Despacho::InstalarSql(const Saidas& saidas) {
  if (sql_pronto_) return true;
  // Os 64 slots, e nao 4: um slot por cablar le-se como zero e um `blx 0` e o
  // que acontece a seguir (foi o defeito do IBitmap do ecra, `interface.h`).
  // O 4.o argumento e o ENDERECO da vtable e o 6.o e o INDICE de base das saidas
  // -- dois papeis do MESMO numero (`kVtableSqlDb`), e troca-los poe o valor 9800
  // como PONTEIRO de vtable: o `ldr r3,[r0]`/`ldr ip,[r3,#12]` do thunk do modulo
  // le entao a palavra de instrucao que estiver em 9800+12. Foi esse o defeito
  // desta instalacao na primeira corrida: o traco dizia
  // `saiu_do_modulo_para_0xe08f5004`, que e a propria instrucao `add r5,pc,r4` do
  // modulo usada como endereco.
  ConstruirObjeto(mem_, saidas, kObjSqlDb, kEnderecoDaVtableSqlDb, kSlotsPorVtable, kVtableSqlDb);
  // A LEITURA DE VOLTA: o slot 3 tem de apontar para o indice do `Exec`.
  const std::uint32_t lido = mem_.Ler32(kEnderecoDaVtableSqlDb + 3 * 4);
  if (lido != saidas.Endereco(kSlotIdSqlExec)) {
    traco_.RegistarFalta(Area::Brew, "cablagem_do_SQL",
                         "o slot 3 do ISQLDatabase nao aponta para o Exec");
    return false;
  }
  sql_pronto_ = true;
  traco_.Emitir(Area::Brew, Nivel::Informacao, "SQL_INSTALADO",
                "ISQLDatabase obj=0x" + Hex(kObjSqlDb) + " vtable=0x" +
                    Hex(kEnderecoDaVtableSqlDb) + " (indice de saida " +
                    std::to_string(kVtableSqlDb) + ") Exec=slot3");
  return true;
}

namespace {
// Uma cadeia NUL-terminada na memoria do guest. Nao ha `EscreverCadeia` no
// `Memoria`; o que ha e `Escrever8`, e escrever um byte a cada vez num interface
// que ja tem `LerCadeia` seria a assimetria que produz o proximo defeito.
void EscreverTexto(Memoria& mem, std::uint32_t onde, const std::string& s) {
  for (std::size_t k = 0; k < s.size(); ++k) {
    mem.Escrever8(onde + static_cast<std::uint32_t>(k), static_cast<std::uint8_t>(s[k]));
  }
  mem.Escrever8(onde + static_cast<std::uint32_t>(s.size()), 0);
}
}  // namespace

// A ENTREGA DE UMA LINHA AO CALLBACK DO JOGO, com a forma do `sqlite3_exec`:
// `int cb(void *ctx, int ncols, char **valores, char **nomes)`, e um retorno
// DIFERENTE DE ZERO pede paragem.
//
// QUEM DECIDE O QUE E UMA LINHA E O SQLITE (`core/brew/sql.h`): este caminho
// escreve os textos na zona do guest, aponta os dois vectores de `char *` para
// eles e corre o callback do modulo. Antes -- com o subconjunto a mao -- era este
// ficheiro que decidia o que era uma tabela, um `INSERT` e um `SELECT`, e decidia
// tudo o que sabia: o que nao sabia ficava recusado com o texto da instrucao.
//
// O VALOR NULO DO SQL E UM PONTEIRO NULO: o `sqlite3_exec` entrega `NULL` no
// vector, e o console entregava o mesmo. Escrever uma cadeia vazia no lugar dele
// seria dizer ao jogo que a coluna tem uma string -- e a diferenca entre "nao ha
// valor" e "ha um valor vazio" e usada pelo proprio catalogo (o `strValue` das
// preferencias e `''` de verdade).
//
// A CHAMADA AO GUEST E REENTRANTE, e por isso segue o mesmo cuidado do
// `ISHELL_SendEvent`: guardar os 16 registadores e o CPSR, um tecto de
// aninhamento, e repor tudo no fim. O callback corre pelo `Correr` (o mesmo
// caminho do evento) e nao por um laco de `Passo`, pela razao escrita la: um
// `Passo` entraria na faixa de saida e deslizaria ate ao limite.
bool Despacho::EntregarLinhaSql(ICpu& cpu, const PonteSqlite::Linha& linha, std::uint32_t cb,
                                std::uint32_t ctx, std::uint32_t pp_saida) {
  // SEM CALLBACK NAO HA ENTREGA, E A INSTRUCAO CONTINUA. E o contrato do
  // `sqlite3_exec` (uma instrucao sem callback executa e deita fora as linhas), e
  // era isto que o subconjunto a mao nao tinha: ele parava.
  if (cb == 0) return true;
  // A LINHA TEM DE CABER NA ZONA, e o tecto e medido (ver `despacho.h`). Uma linha
  // maior NAO SE TRUNCA: fica uma FALTA com o numero de colunas, porque entregar
  // metade de uma linha e uma mentira que so apareceria no ecra do jogo.
  if (linha.colunas < 0 ||
      static_cast<std::uint32_t>(linha.colunas) > kMaximoDeColunasSql) {
    traco_.RegistarFalta(Area::Brew, "ISQLDatabase::Exec linha mais larga que a zona",
                         std::to_string(linha.colunas) + " colunas (tecto " +
                             std::to_string(kMaximoDeColunasSql) + ")");
    return false;
  }
  const std::uint32_t n = static_cast<std::uint32_t>(linha.colunas);
  // O TEXTO TAMBEM TEM TECTO, e pelo mesmo motivo: o valor mais comprido do
  // dialecto medido tem 45 caracteres, e um que nao caiba e uma falta NOMEADA em
  // vez de um texto cortado.
  for (std::uint32_t i = 0; i < n; ++i) {
    const char* nome = (linha.nomes != nullptr) ? linha.nomes[i] : nullptr;
    const char* valor = (linha.valores != nullptr) ? linha.valores[i] : nullptr;
    const std::size_t t_nome = (nome != nullptr) ? std::strlen(nome) : 0;
    const std::size_t t_valor = (valor != nullptr) ? std::strlen(valor) : 0;
    if (t_nome > kMaximoDeTextoDaColunaSql || t_valor > kMaximoDeTextoDaColunaSql) {
      traco_.RegistarFalta(Area::Brew, "ISQLDatabase::Exec valor maior que a zona",
                           "coluna " + std::string(nome != nullptr ? nome : "?") + ": " +
                               std::to_string(t_valor) + " caracteres (tecto " +
                               std::to_string(kMaximoDeTextoDaColunaSql) + ")");
      return false;
    }
  }
  // O CALLBACK TEM DE ESTAR DENTRO DO MODULO DO TITULO -- a mesma guarda do
  // `IShell::SendEvent`. Sem ela, um ponteiro de funcao por inicializar punha o
  // PC num endereco de dados e o laco andava a executar zeros.
  const std::uint32_t fim_do_modulo = (faixa_fim_ > faixa_base_) ? faixa_fim_ : 0;
  if (fim_do_modulo == 0 || cb < faixa_base_ || cb >= fim_do_modulo) {
    char det[96];
    std::snprintf(det, sizeof(det), "cb=0x%08x fora do modulo [0x%08x,0x%08x)", cb, faixa_base_,
                  fim_do_modulo);
    traco_.RegistarFalta(Area::Brew, "ISQLDatabase::Exec callback fora do modulo", det);
    return false;
  }
  if (profundidade_de_evento_ >= kMaxProfundidadeDeEvento) {
    traco_.RegistarFalta(Area::Brew, "ISQLDatabase::Exec aninhamento",
                         "sem entrega de linhas por aninhamento profundo");
    return false;
  }

  // OS TEXTOS, E DEPOIS OS DOIS VECTORES DE `char *`. Os dois vectores vivem no
  // mesmo bloco, um a seguir ao outro, e cada um leva o seu `NULL` no fim.
  constexpr std::uint32_t kVectorDeNomes = (kMaximoDeColunasSql + 1u) * 4u;
  for (std::uint32_t i = 0; i < n; ++i) {
    const char* nome = (linha.nomes != nullptr && linha.nomes[i] != nullptr) ? linha.nomes[i] : "";
    const char* valor =
        (linha.valores != nullptr && linha.valores[i] != nullptr) ? linha.valores[i] : nullptr;
    const std::uint32_t onde_nome = kZonaDosNomesSql + i * kPassoDoTextoDaColunaSql;
    const std::uint32_t onde_valor = kZonaDosValoresSql + i * kPassoDoTextoDaColunaSql;
    EscreverTexto(mem_, onde_nome, nome);
    EscreverTexto(mem_, onde_valor, valor != nullptr ? valor : "");
    mem_.Escrever32(kZonaDosVectoresSql + i * 4, valor != nullptr ? onde_valor : 0u);
    mem_.Escrever32(kZonaDosVectoresSql + kVectorDeNomes + i * 4, onde_nome);
  }
  mem_.Escrever32(kZonaDosVectoresSql + n * 4, 0);
  mem_.Escrever32(kZonaDosVectoresSql + kVectorDeNomes + n * 4, 0);

  std::array<std::uint32_t, 16> guardados{};
  for (int r = 0; r < 16; ++r) guardados[static_cast<std::size_t>(r)] = cpu.Get(r);
  const std::uint32_t cpsr_guardado = cpu.Cpsr();
  cpu.Set(kR0, ctx);
  cpu.Set(kR1, n);
  cpu.Set(kR2, kZonaDosVectoresSql);
  cpu.Set(kR3, kZonaDosVectoresSql + kVectorDeNomes);
  cpu.Set(kLR, kSentinela);
  cpu.Set(kPC, cb);
  ++profundidade_de_evento_;
  const ResultadoFase r = Correr(cpu, kLimiteDoEvento, pp_saida);
  --profundidade_de_evento_;
  // O RETORNO DO CALLBACK LE-SE ANTES DE REPOR OS REGISTADORES, e isto foi um
  // DEFEITO MEDIDO, nao uma precaucao: as duas linhas seguintes poem no `r0` o
  // valor que o canal tinha ANTES da entrega (`kObjSqlDb`, o objecto do banco), e
  // a leitura feita DEPOIS delas dava sempre "diferente de zero" -- ou seja, o
  // `sqlite3_exec` via SEMPRE um pedido de paragem e abortava TODA a instrucao com
  // uma linha. O subconjunto a mao tinha o mesmo `Restaurar` antes da leitura do
  // `r0` (`if (cpu.Get(kR0) != 0) break;`), e nao se via porque ele nunca olhava
  // para o codigo de retorno do proprio `Exec`; o teste do ciclo das preferencias
  // deu com ele no primeiro dia.
  const std::uint32_t retorno_do_callback = cpu.Get(kR0);
  for (int reg = 0; reg < 16; ++reg) cpu.Set(reg, guardados[static_cast<std::size_t>(reg)]);
  cpu.SetCpsr(cpsr_guardado);
  if (r.motivo != "retornou") {
    traco_.RegistarFalta(Area::Brew, "ISQLDatabase::Exec callback nao voltou",
                         r.motivo + " | cb=0x" + Hex(cb));
    return false;
  }
  // "Callback que devolve diferente de zero manda parar": o contrato do
  // `sqlite3_exec`. Recusa-lo aqui (como o subconjunto fazia) deixaria o jogo sem
  // o unico jeito que ele tem de parar uma consulta comprida.
  return retorno_do_callback == 0;
}

bool Despacho::AtenderSql(ICpu& cpu, std::uint32_t indice, std::uint32_t pp_saida) {
  if (!sql_pronto_) return false;
  if (indice < kVtableSqlDb || indice >= kVtableSqlDb + kSlotsPorVtable) return false;
  const std::uint32_t slot = indice - kVtableSqlDb;
  if (slot != 3) {
    // Os outros slots do banco. Nenhum apareceu na medicao: um pedido aqui e
    // informacao NOVA, e por isso fica com nome proprio em vez de cair no ramo
    // generico e ser lido como `IFileMgr::slot2803`.
    char det[64];
    std::snprintf(det, sizeof(det), "slot=%u", slot);
    traco_.RegistarFalta(Area::Brew, "ISQLDatabase slot nao implementado", det);
    cpu.Set(kR0, kAeeUnsupported);
    return true;
  }

  std::string sql;
  mem_.LerCadeia(cpu.Get(kR1), &sql, 1024);
  const std::uint32_t cb = cpu.Get(kR2);
  const std::uint32_t ctx = cpu.Get(kR3);
  traco_.Emitir(Area::Brew, Nivel::Depuracao, "SQL_EXEC",
                "\"" + sql + "\" cb=0x" + Hex(cb) + " ctx=0x" + Hex(ctx));
  if (!sql_.Aberto()) {
    // A INSTRUCAO SEM BANCO. So acontece depois de um `Open` recusado, e fica com
    // o nome em vez de um codigo mudo: sem esta linha o jogo responderia "erro de
    // SQL" e ninguem saberia que o banco e que nao estava la.
    traco_.RegistarFalta(Area::Brew, "ISQLDatabase::Exec sem banco aberto", sql.substr(0, 96));
    cpu.Set(kR0, kAeeFailed);
    return true;
  }
  // O PRESSUPOSTO DECLARADO, e nao uma falta: o motor de SQL existe (e o SQLite,
  // dominio publico, o mesmo motor do console) e a resposta e dele. O que este
  // projecto conta como falta e uma capacidade NOSSA que nao existe; aqui a
  // capacidade existe, e o que se declara e que ela e o motor do console.
  traco_.RegistarPressuposto(Area::Brew, "ISQLDatabase::Exec (ponte SQLite)",
                             std::string("o motor de SQL e o SQLite ") + PonteSqlite::Versao());
  std::string motivo;
  const int r = sql_.Executar(
      sql,
      [&](const PonteSqlite::Linha& linha) {
        return EntregarLinhaSql(cpu, linha, cb, ctx, pp_saida);
      },
      &motivo);
  if (r != PonteSqlite::kBom) {
    // ERRO DO MOTOR, E NAO UMA RECUSA NOSSA: a instrucao chegou ao SQLite e o
    // SQLite disse que nao. O registo leva a INSTRUCAO e a MENSAGEM dele (e a
    // mensagem do SQLite nomeia a tabela ou a coluna que faltou), num evento de
    // erro -- e nao numa falta contada, que poria uma resposta do proprio console
    // na lista do que nos falta implementar.
    traco_.Emitir(Area::Brew, Nivel::Erro, "SQL_ERRO", motivo + " | " + sql.substr(0, 96));
  }
  cpu.Set(kR0, (r == PonteSqlite::kBom) ? kAeeSuccess : kAeeFailed);
  return true;
}

// ---------------------------------------------------------------------------
// OS DOIS CLSIDs DO Z-WHEEL: o `IConfig` e o `IDownload` (frente zclsid)
// ---------------------------------------------------------------------------
//
// As constantes e a MEDICAO inteira (o traco e o desmonte dos dois sitios de
// chamada) estao em `core/brew/despacho.h`, junto das dos outros objectos. Aqui
// esta o comportamento, e a regra e a da casa: **o que a medicao mostrou e
// servido; o que nao se sabe RECUSA COM O NOME** -- nunca sucesso silencioso.
bool Despacho::InstalarZclsid(const Saidas& saidas) {
  if (zclsid_pronto_) return true;
  // UM OBJECTO POR CLSID, cada um com a vtable da SUA interface (64 slots, como
  // todas as desta arvore: um slot por cablar le-se como zero, e o `blx 0` e o
  // defeito que o IBitmap do ecra ja pagou).
  ConstruirObjeto(mem_, saidas, kObjConfig, kVtableZclsidConfig, kSlotsPorVtable, kVtableConfig);
  ConstruirObjeto(mem_, saidas, kObjDownload, kVtableZclsidDownload, kSlotsPorVtable,
                  kVtableDownload);
  // A LEITURA DE VOLTA DOS SLOTS QUE A MEDICAO USOU -- e nao so a escrita: uma
  // cablagem ja se perdeu nesta arvore sem sintoma nenhum (`SetTimer`), e o
  // sintoma era a bateria dizer que faltava o metodo.
  const std::uint32_t lido_config =
      mem_.Ler32(kVtableZclsidConfig + (kSlotConfigSetItem - kVtableConfig) * 4);
  if (lido_config != saidas.Endereco(kSlotConfigSetItem)) {
    traco_.RegistarFalta(Area::Brew, "cablagem_do_IConfig",
                         "o slot 3 do IConfig nao aponta para o SetItem");
    return false;
  }
  const std::uint32_t lido_download =
      mem_.Ler32(kVtableZclsidDownload + (kSlotDownloadFalhados - kVtableDownload) * 4);
  if (lido_download != saidas.Endereco(kSlotDownloadFalhados)) {
    traco_.RegistarFalta(Area::Brew, "cablagem_do_IDownload",
                         "o slot 3 do IDownload nao aponta para a lista");
    return false;
  }
  zclsid_pronto_ = true;
  traco_.Emitir(Area::Brew, Nivel::Informacao, "ZCLSID_INSTALADOS",
                "IConfig obj=0x" + Hex(kObjConfig) + " vtable=0x" + Hex(kVtableZclsidConfig) +
                    " (SetItem no slot 3) | IDownload obj=0x" + Hex(kObjDownload) +
                    " vtable=0x" + Hex(kVtableZclsidDownload) + " (lista no slot 3)");
  return true;
}

bool Despacho::AtenderZclsid(ICpu& cpu, std::uint32_t indice, std::uint32_t pp_saida) {
  (void)pp_saida;
  if (!zclsid_pronto_) return false;
  if (indice >= kVtableConfig && indice < kVtableConfig + kSlotsPorVtable) {
    return AtenderConfig(cpu, indice - kVtableConfig);
  }
  if (indice >= kVtableDownload && indice < kVtableDownload + kSlotsPorVtable) {
    return AtenderDownload(cpu, indice - kVtableDownload);
  }
  return false;
}

// O `IConfig` (`AEECLSID_CONFIG`). Os quatro slots com nome vem do `zeebx` novo
// (`src/aee_slots.rs:957`) e o `SetItem` no slot 3 esta confirmado no desmonte do
// proprio `tectoy` (`0x71250`): `SetItem(po, 0x3f, sp+8, 4)` -- o item 63, o
// idioma do sistema, 4 bytes.
bool Despacho::AtenderConfig(ICpu& cpu, std::uint32_t slot) {
  if (slot == kSlotConfigGetItem - kVtableConfig) {
    const std::uint32_t item = cpu.Get(kR1);
    const std::uint32_t destino = cpu.Get(kR2);
    const std::uint32_t tamanho = cpu.Get(kR3);
    const auto it = itens_do_config_.find(item);
    // ITEM QUE O JOGO NUNCA ESCREVEU: RECUSA. Este emulador nao tem configuracao
    // de aparelho, e devolver ZERO seria devolver um valor -- e um valor zero e
    // legitimo (o `dwValue` das preferencias mede-se a zero). A recusa leva o
    // numero do item para o proximo poder saber QUAL item falta.
    if (it == itens_do_config_.end()) {
      char det[96];
      std::snprintf(det, sizeof(det), "item=0x%02x n=%u (nunca foi escrito pelo app)",
                    item, tamanho);
      traco_.RegistarFalta(Area::Brew, "IConfig::GetItem item nao definido", det);
      cpu.Set(kR0, kAeeFailed);
      return true;
    }
    // UM PEDIDO MAIOR QUE O GUARDADO NAO SE COMPLETA COM LIXO: se o jogo quer 8
    // bytes de um item de 4, o que esta depois nao existe.
    if (tamanho > it->second.size()) {
      char det[96];
      std::snprintf(det, sizeof(det), "item=0x%02x pede %u bytes, ha %zu", item, tamanho,
                    it->second.size());
      traco_.RegistarFalta(Area::Brew, "IConfig::GetItem pedido maior que o valor", det);
      cpu.Set(kR0, kAeeFailed);
      return true;
    }
    if (destino != 0) {
      for (std::uint32_t k = 0; k < tamanho; ++k) {
        mem_.Escrever8(destino + k, it->second[k]);
      }
    }
    ++config_lidas_;
    traco_.Emitir(Area::Brew, Nivel::Depuracao, "ICONFIG_GETITEM",
                  "item=0x" + Hex(item) + " n=" + std::to_string(tamanho) + " (gravado pelo app)");
    cpu.Set(kR0, kAeeSuccess);
    return true;
  }
  if (slot == kSlotConfigSetItem - kVtableConfig) {
    const std::uint32_t item = cpu.Get(kR1);
    const std::uint32_t origem = cpu.Get(kR2);
    const std::uint32_t tamanho = cpu.Get(kR3);
    if (tamanho == 0 || tamanho > kMaximoDoItemDeConfig) {
      char det[96];
      std::snprintf(det, sizeof(det), "item=0x%02x n=%u (tecto %u)", item, tamanho,
                    kMaximoDoItemDeConfig);
      traco_.RegistarFalta(Area::Brew, "IConfig::SetItem tamanho fora do contrato", det);
      cpu.Set(kR0, kAeeFailed);
      return true;
    }
    std::vector<std::uint8_t> valor(tamanho, 0);
    for (std::uint32_t k = 0; k < tamanho; ++k) {
      valor[k] = mem_.Ler8(origem + k);
    }
    itens_do_config_[item] = valor;
    ++config_escritas_;
    // O PRESSUPOSTO, DECLARADO (e nao uma falta): o item guarda-se, e o que o
    // `GetItem` devolve e o que o proprio jogo gravou nesta corrida. O aparelho
    // deste emulador NAO tem configuracao -- inventa-la seria dar ao jogo um
    // idioma que ninguem escolheu.
    traco_.RegistarPressuposto(Area::Brew, "IConfig::SetItem (o valor e do proprio app)",
                               "este emulador nao tem configuracao de aparelho; o que o app "
                               "grava e o que o app rele");
    traco_.Emitir(Area::Brew, Nivel::Depuracao, "ICONFIG_SETITEM",
                  "item=0x" + Hex(item) + " n=" + std::to_string(tamanho));
    cpu.Set(kR0, kAeeSuccess);
    return true;
  }
  // OS OUTROS SLOTS. Nenhum apareceu na medicao: um pedido aqui e informacao
  // NOVA, e fica com nome proprio em vez de cair no ramo generico e ser lido como
  // um slot do IFileMgr.
  char det[96];
  std::snprintf(det, sizeof(det), "slot=%u po=0x%08x lr=0x%08x", slot, cpu.Get(kR0), cpu.Get(kLR));
  traco_.RegistarFalta(Area::Brew, "IConfig slot nao implementado", det);
  cpu.Set(kR0, kAeeUnsupported);
  return true;
}

// O `IDownload` (`AEECLSID_DOWNLOAD` = 0x01000000). O SLOT 3 DEVOLVE A LISTA dos
// downloads falhados, ou ZERO quando nao ha nenhum -- e o desmonte mostra-o com
// todas as letras: com `r0 != 0` o jogo faz
//
//     27dd4  ldr r6,[r5] ; cmp r6,#0 ; bne 0x27ce0     ; r6 = um id da lista
//     27dd0  add r5,r5,#4                              ; e avanca quatro bytes
//
// ou seja `r0` e um PONTEIRO para uma lista terminada em NULO, e nao um codigo de
// erro (com um codigo, o `ldr r6,[20]` leria dentro do proprio modulo e o laco
// nunca parava).
//
// ESTE EMULADOR NAO TEM FILA DE DOWNLOADS: nao ha rede, nao ha `tt_dlqueue.db`
// aberto e nada foi descarregado, logo a lista de falhados e VAZIA -- e o `0` e a
// verdade sobre este sistema, e nao um stub. O que se declara e isso mesmo, com
// um pressuposto nomeado.
bool Despacho::AtenderDownload(ICpu& cpu, std::uint32_t slot) {
  if (slot == kSlotDownloadFalhados - kVtableDownload) {
    traco_.RegistarPressuposto(Area::Brew, "IDownload (a fila de downloads nao existe)",
                               "sem rede e sem fila nada foi descarregado; a lista de "
                               "falhados e vazia, e o 0 e o valor que o modulo medido "
                               "trata como nada a corrigir");
    traco_.Emitir(Area::Brew, Nivel::Depuracao, "IDOWNLOAD_FALHADOS",
                  "vazia -> 0 (nenhum download falhado)");
    cpu.Set(kR0, 0);
    return true;
  }
  if (slot == kSlotDownloadItemInfo - kVtableDownload) {
    // O `slot 4` SO E ALCANCAVEL POR UM ID DA LISTA (`27d38 mov r1,r6`), e a lista
    // volta vazia -- logo aqui nao ha nada a entregar. Fica com o NOME e contado,
    // para o dia em que um titulo chegar aqui com um id: a resposta seria o
    // `AppModInfo` daquele id, e isso exige a fila de downloads, que nao existe.
    char det[96];
    std::snprintf(det, sizeof(det), "id=0x%08x lr=0x%08x (a lista de falhados estava vazia)",
                  cpu.Get(kR1), cpu.Get(kLR));
    traco_.RegistarFalta(Area::Brew, "IDownload slot4 (info do item) sem lista", det);
    cpu.Set(kR0, 0);
    return true;
  }
  if (slot == kSlotDownloadInfoComCallback - kVtableDownload) {
    // O SLOT 21, E ELE QUE O `tectoy` CHAMA. MEDIDO (traco com o `lr` posto no
    // detalhe, e depois o desmonte do sitio): em `Tectoy_FixupTime`, logo a seguir
    // ao `IShell::GetClassItemID` (slot 45, `tools/brew_slots.inc:58`),
    //
    //     69bdc  ldr r0,[r4,#0x57c]   ; o objecto IDownload que ELE guardou
    //     69be4  ldr r1,[r0]          ; a vtable
    //     69bf0  ldr ip,[r1,#0x54]    ; slot 21 (0x54 = 21*4)
    //     69bf4  mov r1,r6            ; o id do item (o que o slot 45 devolveu)
    //     69bec  add r2,pc,r2         ; um PONTEIRO DE FUNCAO do modulo (0x735f4)
    //     69bf8  mov lr,pc ; bx ip    ; slot21(po, id, callback, contexto)
    //
    // A FORMA `(po, id, callback, contexto)` E A DO `IDOWNLOAD_GetItemInfo` da
    // implementacao de referencia do SDK (`OATDownload.c:589`:
    // `IDOWNLOAD_GetItemInfo(pme->m_pIDownload, pme->m_id, OATDownload_ItemInfoCB,
    // pme)`), e o `AEEIDownload.h` NAO existe nesta maquina para confirmar a ORDEM
    // dos slots -- ver a contradicao no cabecalho. O NOME fica o da forma medida
    // ("info do item com callback") e nao o do candidato.
    //
    // SEM FILA NAO HA ITEM: o id que o `GetClassItemID` devolveu nao corresponde a
    // nada descarregado, e nao ha `AppModInfo` para entregar. A resposta e
    // `AEE_EFAILED` -- "esse item nao existe" e a verdade deste sistema -- e o
    // callback NAO se chama: chama-lo com uma estrutura inventada seria dar ao
    // jogo um URL ou uma versao que ninguem mediu.
    char det[128];
    std::snprintf(det, sizeof(det),
                  "id=0x%08x cb=0x%08x ctx=0x%08x lr=0x%08x (nao ha fila de downloads)",
                  cpu.Get(kR1), cpu.Get(kR2), cpu.Get(kR3), cpu.Get(kLR));
    traco_.RegistarPressuposto(Area::Brew, "IDownload::GetItemInfo",
                               std::string(det) + "; serviu EFAILED sem callback");
    cpu.Set(kR0, kAeeFailed);
    return true;
  }
  char det[96];
  std::snprintf(det, sizeof(det), "slot=%u po=0x%08x lr=0x%08x", slot, cpu.Get(kR0), cpu.Get(kLR));
  traco_.RegistarFalta(Area::Brew, "IDownload slot nao implementado", det);
  cpu.Set(kR0, kAeeUnsupported);
  return true;
}

namespace {
// Le uma cadeia do guest, com limite. Sem limite, um ponteiro errado percorre o
// espaco todo antes de parar.
std::string LerTextoDe(const Memoria& mem, std::uint32_t p, std::size_t maximo) {
  std::string s;
  if (p == 0) return s;
  for (std::size_t k = 0; k < maximo; ++k) {
    const char ch = static_cast<char>(mem.Ler8(p + static_cast<std::uint32_t>(k)));
    if (ch == 0) break;
    s.push_back(ch);
  }
  return s;
}
// O identificador do ficheiro a partir do endereco do objecto: o objecto de
// indice `n` vive em `kObjFileBase + n*0x40`.
std::uint32_t IdentificadorDeFicheiro(std::uint32_t obj) {
  return (obj >= kObjFileBase) ? (obj - kObjFileBase) / 0x40 : 0;
}

// ---------------------------------------------------------------------------
// O DETECTOR DE TIPO (`IShell::DetectType`, slot 43).
//
// ===========================================================================
// 1) O CONTRATO, do cabecalho do SDK -- e nao adivinhado
// ===========================================================================
//
// `platform/system/inc/AEEIShell.h:275` e a macro em `:568`:
//
//   int (*DetectType)(iname *po, const void *cpBuf, uint32 *pdwSize,
//                     const char *cpszName, const char **pcpszMIME);
//
// Em AAPCS: `r0=po, r1=cpBuf, r2=pdwSize, r3=cpszName`, e `[sp+0]=pcpszMIME`.
// O SLOT E O 43: `AEEIShell.h:288` (`INHERIT_IShell`) conta 43 entradas antes
// dele, e o `tools/gerar_slots.py` gerou `kShell_DetectType = 43` em
// `tools/brew_slots.inc:43`. As duas contagens (a do cabecalho e a do gerador)
// concordam.
//
// O bloco de documentacao (`:4515-4570`) diz o que cada resposta significa:
//
//   pdwSize : [in/out] On input, the size of the data in pBuf; if cpBuf is
//             NULL, then this is ignored. On output, the number of additional
//             data bytes needed to perform type detection.
//   Return  : AEE_SUCCESS ... AEE_ENOTYPE ... AEE_ENEEDMORE  (need more data;
//             *pdwSize contains the required number of additional bytes)
//
// e traz o caso de uso ESCRITO no proprio cabecalho, que e o unico que o corpus
// faz:
//
//   if (ENEEDMORE == ISHELL_DetectType(ps, NULL, &dwReqSize, NULL, NULL))
//       // dwReqSize contains the max bytes needed for type detection.
//
// ===========================================================================
// 2) O QUE OS TITULOS PEDEM, medido no proprio guest
// ===========================================================================
//
// MEDIDO (`ZB2_TRACE=1`, corpus de 62 titulos, 46 chamadas em 4 titulos):
// TODAS as 46 chamadas tem `r1 = 0` (`cpBuf`), `r3 = 0` (`cpszName`) e
// `[sp+0] = 0` (`pcpszMIME`); so o `r2` muda. Sao a SONDA DE TAMANHO, e o
// `lr` diz de onde vem:
//
//   abd 35x lr=0x00001360  pacmania 3x lr=0x00000e9c
//   ridgeracer 25x lr=0x0000174c  torkandkral 18x lr=0x000014ac
//
// E o desmonte do `abd.mod` (base ZERO, ver `tests/mod_base_test.cpp`) fecha a
// questao -- o que o jogo faz com a resposta, instrucao a instrucao:
//
//   0000133c  ldr  r0, [r0]        ; r0 = o objecto IShell
//   00001348  mov  r3, r6          ; r3 = 0        (cpszName)
//   0000134c  ldr  ip, [r0, #0xac] ; 0xac = 43*4 -> slot 43
//   00001350  add  r2, sp, #0x30   ; r2 = &dwSize
//   00001354  mov  r1, r6          ; r1 = 0        (cpBuf)
//   00001358  mov  r0, r5          ; r0 = o shell
//   0000135c  blx  ip
//   00001360  cmp  r0, #0x23       ; 0x23 = 35 = AEE_ENEEDMORE
//   00001364  bne  #0x1374         ; != 35 -> desiste do objecto
//   00001368  ldr  r1, [sp, #0x30] ; *pdwSize
//   0000136c  cmp  r1, #0
//   00001370  bne  #0x1380         ; != 0 -> SEGUE
//   00001374  mov  r0, #1          ; desistiu: devolve 1 (objecto nao pronto)
//
// Ou seja: a condicao e LITERALMENTE `ret == AEE_ENEEDMORE && *pdwSize != 0`,
// e nao "nao-zero". **E o `35` nao e um numero magico: e o `ENEEDMORE` do
// SDK** -- o zeebulator mediu o mesmo `cmp r0, #35` (`core/brew/ishell.cpp`,
// o ramo do slot 43) e chamou-lhe um "contrato de estado" porque nao tinha o
// cabecalho `AEEStdErr.h` a mao: `AEE_ENEEDMORE = 35` (`AEEStdErr.h:51`; o
// `AEE_ENOTYPE` e o 34).
//
// A CONTRADICAO COM A REFERENCIA, e fica escrita: o zeebulator responde `35` na
// 1a chamada e `0` na 2a (um alternador por paridade de chamadas) porque
// observou um segundo sitio de chamada a esperar `0`. O `abd` faz 35 chamadas
// IDENTICAS (mesmo `lr`, mesmos argumentos, o mesmo `r2`) e o `0` na 2a
// deixaria 17 dos 35 objectos por inicializar. O que a 2a chamada do zeebulator
// descreve e OUTRO sitio (`abd.mod` 0x16a0, = o 0x1016a0 deles), que pede o
// MIME com `[sp] = &cpszMIME` -- e esse sitio le `AEE_SUCCESS`. Os dois sitios
// sao servidos pelo MESMO contrato do SDK: sonda -> `ENEEDMORE` + tamanho;
// dado -> `SUCCESS` + mime. Nao ha estado a manter, e nao ha alternador.
// ---------------------------------------------------------------------------

// `AEE_ENOTYPE` (34) e `AEE_ENEEDMORE` (35), de `AEEStdErr.h:50-51`. Ficam
// aqui, junto do contrato que os usa, e nao no `kAee*` de `ajudantes.h`: aquele
// enum tem os codigos que o despacho ja usava, e o nome do erro e do sitio que
// o devolve.
constexpr std::uint32_t kAeeNoType = 34;
constexpr std::uint32_t kAeeNeedMore = 35;

// DE QUANTOS BYTES PRECISA ESTE DETECTOR. A assinatura mais comprida da tabela
// abaixo tem 12 bytes (o `RIFF` + tamanho + `WAVE`), e o numero e o mesmo do
// zeebx (`src/machine/mod.rs:832`, `DETECT_TYPE_BYTES = 16`): cabe a mais
// comprida com folga, e um numero so serve para a resposta ao guest E para o
// corte da leitura.
constexpr std::uint32_t kBytesDoDetector = 16;

// AS CADEIAS DE MIME, NUMA LISTA SO. O indice devolvido pelo detector e o mesmo
// que endereça a cadeia na memoria do guest (`EscreverMimeNoGuest`): duas
// listas que tem de concordar sao zero listas.
const char* const kMimes[] = {
    "image/png", "image/jpeg", "image/gif", "image/bmp", "audio/mid",
    "audio/mpeg", "audio/wav", "audio/amr", "text/plain",
};
constexpr std::uint32_t kQuantosMimes = sizeof(kMimes) / sizeof(kMimes[0]);
static_assert(kQuantosMimes <= kMaximoDeMimes,
              "a zona de mimes do shell tem de ter espaco para a lista toda");
enum MimeConhecido : std::uint32_t {
  kMimePng = 0,
  kMimeJpeg,
  kMimeGif,
  kMimeBmp,
  kMimeMid,
  kMimeMpeg,
  kMimeWav,
  kMimeAmr,
  kMimeTexto,
  kMimeNenhum,  // a resposta "nao sei" -- nunca indexa a lista
};

// A ASSINATURA -> O MIME. As assinaturas sao as do zeebx (`src/machine/mod.rs`,
// `detect_mime`) e as que os recursos do corpus usam. Um ficheiro com menos
// bytes do que a assinatura nao a pode ter: e o `n`.
std::uint32_t MimeDaMagia(const std::uint8_t* b, std::size_t n) {
  if (b == nullptr) return kMimeNenhum;
  const auto comeca = [&](const char* magia, std::size_t k) {
    if (n < k) return false;
    for (std::size_t i = 0; i < k; ++i) {
      if (b[i] != static_cast<std::uint8_t>(magia[i])) return false;
    }
    return true;
  };
  // A assinatura do PNG sao OITO bytes (ISO/IEC 15948, 5.2), e nao quatro: o
  // `\r\n` no fim e o que distingue um PNG de um ficheiro que so comeca por
  // `89 50 4E 47`.
  if (comeca("\x89PNG\r\n\x1a\n", 8)) return kMimePng;
  if (comeca("\xff\xd8\xff", 3)) return kMimeJpeg;  // SOI + o primeiro marcador
  if (comeca("GIF87a", 6) || comeca("GIF89a", 6)) return kMimeGif;
  if (comeca("BM", 2)) return kMimeBmp;
  if (comeca("MThd", 4)) return kMimeMid;
  // O MP3 tem DUAS formas: com a etiqueta `ID3` a frente, ou com o
  // sincronismo `FF Ex` (o primeiro byte todo a um, os tres bits de cima do
  // segundo tambem).
  if (comeca("ID3", 3)) return kMimeMpeg;
  if (n >= 2 && b[0] == 0xFFu && (b[1] & 0xE0u) == 0xE0u) return kMimeMpeg;
  // O WAV e um `RIFF` com o tamanho a seguir e a marca `WAVE` no +8: o `RIFF`
  // sozinho tambem abre AVI e WebP, e o que decide e o `WAVE`.
  if (comeca("RIFF", 4) && n >= 12 && b[8] == 'W' && b[9] == 'A' && b[10] == 'V' && b[11] == 'E') {
    return kMimeWav;
  }
  if (comeca("#!AMR", 5)) return kMimeAmr;
  return kMimeNenhum;
}

// A EXTENSAO -> O MIME, para o caso em que o chamador da o NOME e nao os bytes.
// A extensao e a ultima depois do ultimo ponto DA ULTIMA COMPONENTE do caminho
// (um directoria com um ponto no nome nao conta). Tudo em minusculas: o cartao
// do Zeebo tem nomes em maiusculas (`MATERIAL.BAR`) e uma comparacao sensivel
// ao caso seria uma recusa dependente da ROM.
std::uint32_t MimeDaExtensao(const std::string& nome) {
  const std::size_t barra = nome.find_last_of("/\\");
  const std::string ficheiro = (barra == std::string::npos) ? nome : nome.substr(barra + 1);
  const std::size_t ponto = ficheiro.find_last_of('.');
  if (ponto == std::string::npos) return kMimeNenhum;
  std::string ext = ficheiro.substr(ponto + 1);
  for (char& c : ext) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  if (ext == "png") return kMimePng;
  if (ext == "jpg" || ext == "jpeg") return kMimeJpeg;
  if (ext == "gif") return kMimeGif;
  if (ext == "bmp") return kMimeBmp;
  if (ext == "mid" || ext == "midi") return kMimeMid;
  if (ext == "mp3") return kMimeMpeg;
  if (ext == "wav") return kMimeWav;
  if (ext == "amr") return kMimeAmr;
  if (ext == "txt") return kMimeTexto;
  return kMimeNenhum;
}

// ---------------------------------------------------------------------------
// A DESCOMPRESSAO DA FRENTE io2.
//
// O `IUnzipAStream` do SDK descomprime "o algoritmo deflate, o usado pelo
// gzip". Os recursos `.bar` de tipo imagem chegam num AEEResBlob cujo dado e
// um stream GZIP (o allstarcards, medido: mime=application/x-gzip-compressed e
// o 1f 8b logo a seguir ao blob); os `.pkg` usam zlib puro (RFC1950). O
// `Inflar` de core/carga cobre o zlib; o gzip precisa de tirar o envelope ANTES
// e de validar o RODAPE (`CRC32` + `ISIZE`, os 8 bytes finais do RFC1952) --
// que e o que este ficheiro faz, sem tocar em core/carga.
// ---------------------------------------------------------------------------

// CRC32 do RFC1952 (gzip), polinomio normal (0xEDB88320) da libz. Tabela fixa
// de 256 entradas, escrita UMA vez a pedido (a treliça da primeira chamada).
std::uint32_t Crc32(const std::uint8_t* dados, std::size_t n) {
  static std::uint32_t tabela[256];
  static bool pronta = false;
  if (!pronta) {
    for (std::uint32_t i = 0; i < 256; ++i) {
      std::uint32_t c = i;
      for (int k = 0; k < 8; ++k) {
        c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      }
      tabela[i] = c;
    }
    pronta = true;
  }
  std::uint32_t crc = 0xFFFFFFFFu;
  for (std::size_t k = 0; k < n; ++k) {
    crc = tabela[(crc ^ dados[k]) & 0xFFu] ^ (crc >> 8);
  }
  return ~crc;
}

// Descomprime o interior de um stream GZIP (RFC1952) usando o `Inflar` dos
// `.pkg` (RFC1950). O `Inflar` nao conhece o gzip: recebe o PAYLOAD deflate
// dentro de um cabecalho zlib sintetico com um adler32 falso no fim. Ele
// descomprime os blocos todos e recusa no adler -- com a saida inteira no
// vector. A VALIDACAO do gzip e o rodape dele: `CRC32(saida)` e `ISIZE`
// conferem, e nada disso depende do adler falsificado. Um ficheiro de
// verdade corrompido falha no CRC e recusa com o motivo do inflate.
bool InflarGzip(const std::vector<std::uint8_t>& entrada, std::vector<std::uint8_t>* saida,
                std::string* motivo) {
  if (entrada.size() < 18 || entrada[0] != 0x1Fu || entrada[1] != 0x8Bu) {
    if (motivo != nullptr) *motivo = "nao comeca em 1f 8b (gzip)";
    return false;
  }
  if (entrada[2] != 8u) {
    if (motivo != nullptr) *motivo = "CM != 8: o gzip so define deflate";
    return false;
  }
  const std::uint8_t flg = entrada[3];
  if ((flg & 0xE0u) != 0u) {
    if (motivo != nullptr) *motivo = "flags reservadas do cabecalho gzip";
    return false;
  }
  std::size_t p = 10;
  if ((flg & 0x04u) != 0u) {  // FEXTRA
    if (p + 2 > entrada.size()) return false;
    const std::size_t xlen = entrada[p] | (static_cast<std::size_t>(entrada[p + 1]) << 8);
    p += 2 + xlen;
  }
  for (std::uint32_t bit = 0; bit < 2; ++bit) {  // FNAME, FCOMMENT
    if ((flg & (bit == 0 ? 0x08u : 0x10u)) == 0u) continue;
    while (p < entrada.size() && entrada[p] != 0) ++p;
    ++p;
  }
  if ((flg & 0x02u) != 0u) p += 2;  // FHCRC
  if (p + 8 > entrada.size()) {
    if (motivo != nullptr) *motivo = "faltam os 8 bytes do rodape gzip";
    return false;
  }
  const std::size_t plen = entrada.size() - p - 8;
  const std::uint32_t crc_esperado = static_cast<std::uint32_t>(entrada[entrada.size() - 8]) |
                                     (static_cast<std::uint32_t>(entrada[entrada.size() - 7]) << 8) |
                                     (static_cast<std::uint32_t>(entrada[entrada.size() - 6]) << 16) |
                                     (static_cast<std::uint32_t>(entrada[entrada.size() - 5]) << 24);
  const std::uint32_t isize = static_cast<std::uint32_t>(entrada[entrada.size() - 4]) |
                              (static_cast<std::uint32_t>(entrada[entrada.size() - 3]) << 8) |
                              (static_cast<std::uint32_t>(entrada[entrada.size() - 2]) << 16) |
                              (static_cast<std::uint32_t>(entrada[entrada.size() - 1]) << 24);

  std::vector<std::uint8_t> zlibbuf;
  zlibbuf.reserve(2 + plen + 4);
  zlibbuf.push_back(0x78);
  zlibbuf.push_back(0x01);
  zlibbuf.insert(zlibbuf.end(), entrada.begin() + static_cast<std::ptrdiff_t>(p),
                 entrada.begin() + static_cast<std::ptrdiff_t>(p + plen));
  zlibbuf.push_back(0);
  zlibbuf.push_back(0);
  zlibbuf.push_back(0);
  zlibbuf.push_back(0);
  std::string motivo_inflate;
  const bool ok_inflate = zb2::Inflar(zlibbuf.data(), zlibbuf.size(), saida, &motivo_inflate,
                                      0u, nullptr);
  const std::uint32_t crc_calculado = Crc32(saida->data(), saida->size());
  const bool rodape_confere =
      crc_calculado == crc_esperado && saida->size() == static_cast<std::size_t>(isize);
  if (!ok_inflate && !rodape_confere) {
    if (motivo != nullptr) {
      *motivo = "inflate: " + motivo_inflate + " | crc gzip=0x" +
                Hex(crc_esperado) + " calculado=0x" + Hex(crc_calculado) +
                " isize=" + std::to_string(isize) + " saida=" + std::to_string(saida->size());
    }
    saida->clear();
    return false;
  }
  // ok_inflate a TRUE com rodape a falhar e um inflate que "passou" num stream
  // que nao e o nosso interior: recusa tambem, com o mesmo criterio do CRC.
  if (ok_inflate && !rodape_confere) {
    if (motivo != nullptr) {
      *motivo = "blocos decodificados mas o rodape gzip nao confere (crc/isize)";
    }
    saida->clear();
    return false;
  }
  return true;
}

// O que o IUnzipAStream descomprime: um stream zlib (RFC1950, o dos `.pkg`) ou
// um gzip (RFC1952, o dos `.bar`), ambos com o prefixo AEEResBlob possivel
// (`[0]` = bDataOffset, `[1]` = 0; o dado comeca em `entrada[entrada[0]]`).
// Um cabecalho zlib valido nunca tem o segundo byte a zero, por isso a
// heuristica do blob nao engana os streams crus.
bool InflarParaUnzip(const std::vector<std::uint8_t>& entrada, std::vector<std::uint8_t>* saida,
                     std::string* motivo) {
  std::size_t off = 0;
  if (entrada.size() >= 2 && entrada[1] == 0 && entrada[0] >= 0x10u &&
      entrada[0] < entrada.size()) {
    off = entrada[0];
  }
  if (off + 2 <= entrada.size() && entrada[off] == 0x1Fu && entrada[off + 1] == 0x8Bu) {
    std::vector<std::uint8_t> gzip(entrada.begin() + static_cast<std::ptrdiff_t>(off),
                                   entrada.end());
    return InflarGzip(gzip, saida, motivo);
  }
  std::string motivo_zlib;
  if (zb2::Inflar(entrada.data() + off, entrada.size() - off, saida, &motivo_zlib, 0u, nullptr)) {
    return true;
  }
  if (off != 0) {
    // O salto do blob nao deu num stream valido: tenta o comeco, por seguranca.
    return zb2::Inflar(entrada.data(), entrada.size(), saida, motivo, 0u, nullptr);
  }
  if (motivo != nullptr) *motivo = motivo_zlib;
  return false;
}

}  // namespace

bool Despacho::ExpandirUnzip(EstadoDoUnzip& e) {
  // A ORIGEM e um IAStream. O BREW aceita dois como tal: um IMemAStream (o caso
  // MEDIDO do allstarcards: o blob gzip de um recurso) e um IFile aberto (o
  // padrao que o zeebx documenta para o Double Dragon).
  std::vector<std::uint8_t> comprimido;
  const std::uint32_t origem = e.origem;
  if (origem == kObjMemStream) {
    auto it = memstreams_.find(origem);
    if (it == memstreams_.end()) {
      traco_.RegistarFalta(Area::Brew, "IUnzipAStream::Read",
                           "a origem e o IMemAStream que nao existe (objecto nunca criado)");
      return false;
    }
    auto& ms = it->second;
    while (ms.pos < ms.tamanho) {
      comprimido.push_back(mem_.Ler8(ms.base + ms.pos));
      ++ms.pos;
    }
  } else if (origem >= kObjFileBase) {
    // Um IFile como origem: le da posicao corrente ate ao fim, como o
    // `drain_stream` do zeebx.
    const std::uint32_t id = IdentificadorDeFicheiro(origem);
    const std::uint32_t info = al_.Malloc(76);  // FileInfo
    if (info == 0) {
      traco_.RegistarFalta(Area::Brew, "IUnzipAStream::Read", "o alocador recusou o FileInfo");
      return false;
    }
    if (!arquivos_.Informacao(id, mem_, info)) {
      al_.Free(info);
      traco_.RegistarFalta(Area::Brew, "IUnzipAStream::Read", "a origem IFile nao respondeu GetInfo");
      return false;
    }
    const std::uint32_t tamanho = mem_.Ler32(info + 8);  // FileInfo.dwSize
    al_.Free(info);
    if (tamanho > 0) {
      const std::uint32_t buf = al_.Malloc(tamanho);
      if (buf == 0) {
        traco_.RegistarFalta(Area::Brew, "IUnzipAStream::Read",
                             "o alocador recusou " + std::to_string(tamanho) + " bytes para ler a origem");
        return false;
      }
      const std::int32_t lidos = arquivos_.Ler(id, mem_, buf, tamanho);
      if (lidos != static_cast<std::int32_t>(tamanho)) {
        al_.Free(buf);
        traco_.RegistarFalta(Area::Brew, "IUnzipAStream::Read",
                             "leu " + std::to_string(lidos) + " dos " + std::to_string(tamanho) +
                                 " bytes da origem IFile");
        return false;
      }
      comprimido.resize(tamanho);
      for (std::uint32_t k = 0; k < tamanho; ++k) comprimido[k] = mem_.Ler8(buf + k);
      al_.Free(buf);
    }
  } else {
    traco_.RegistarFalta(Area::Brew, "IUnzipAStream::Read",
                         "a origem 0x" + Hex(origem) + " nao e IMemAStream nem IFile");
    return false;
  }

  // O BLOB do .bar pode ter o prefijo AEEResBlob; o `InflarParaUnzip` trata
  // dele, do zlib e do gzip.
  std::string motivo;
  if (!InflarParaUnzip(comprimido, &e.saida, &motivo)) {
    traco_.RegistarFalta(Area::Brew, "IUnzipAStream::Read",
                         "nao descomprimiu: " + motivo +
                             " (origem com " + std::to_string(comprimido.size()) + " bytes)");
    e.saida.clear();
    return false;
  }
  e.pos = 0;
  traco_.RegistarPressuposto(Area::Brew, "IUnzipAStream::Read",
                             "descomprimiu " + std::to_string(comprimido.size()) + " -> " +
                                 std::to_string(e.saida.size()) + " bytes");
  return true;
}


bool Despacho::InstalarEntrada(const Saidas& saidas, std::uint32_t base) {
  if (entrada_pronta_) {
    traco_.RegistarFalta(Area::Entrada, "InstalarEntrada", "a entrada ja estava instalada");
    return false;
  }

  // O GUIAO DA ENTRADA, e de onde ele vem.
  //
  // `ZB2_ENTRADA` e o TEXTO do guiao, com `;` a separar os eventos (o formato esta
  // em `core/brew/ihid_entrada.h`). Ler o guiao do AMBIENTE e aceitavel dentro do
  // P4 -- e CONFIGURACAO lida UMA vez no arranque, e nao tempo nem aleatoriedade
  // lidos a cada passo: a mesma linha de comando com a mesma variavel da a mesma
  // corrida. O que continua proibido (e nao existe) e ler o teclado do hospedeiro
  // ou o relogio do hospedeiro.
  //
  // Sem a variavel o controle fica em REPOUSO -- quatro eixos no centro medido
  // (128) e nenhum botao premido.
  if (const char* guiao = std::getenv("ZB2_ENTRADA")) {
    if (*guiao != '\0') {
      std::string texto(guiao);
      for (char& c : texto) {
        if (c == ';') c = '\n';
      }
      std::string motivo;
      if (!EntradaDoZeebo::Ler(texto, &entrada_, &motivo)) {
        // RECUSA RUIDOSA: um guiao invalido NAO e aplicado pela metade.
        traco_.RegistarFalta(Area::Entrada, "ZB2_ENTRADA", motivo);
        return false;
      }
    }
  }

  if (!sinais_.Construir(saidas, base)) return false;
  if (!ihid_.Construir(saidas, base + Sinais::kSlotsNecessarios)) return false;
  base_da_entrada_ = base;
  entrada_pronta_ = true;
  traco_.Emitir(Area::Entrada, Nivel::Informacao, "ENTRADA_INSTALADA",
                "base=" + Hex(base) + " eventos_do_guiao=" +
                    std::to_string(entrada_.Quantos()) + " ihid=" +
                    Hex(ihid_.EnderecoDoIhid()) + " fabrica=" + Hex(sinais_.EnderecoDaFabrica()));
  return true;
}

bool Despacho::AtenderEntrada(ICpu& cpu, std::uint32_t indice) {
  if (!entrada_pronta_) return false;
  return sinais_.Atender(cpu, indice) || ihid_.Atender(cpu, indice);
}

bool Despacho::ClasseConhecida(std::uint32_t cls) const {
  // O proprio titulo + as classes que o `CreateInstance` serve. O proprio app
  // esteve fora desta lista e o `QueryClass` respondia FALSE para ele, em
  // contradicao com o `CreateInstance` que o servia.
  const bool e_o_titulo = (tem_clsid_ && cls == clsid_titulo_);
  const bool e_classe_servida = (IndiceDaClasse(cls) < kQuantasClasses);
  return cls == kIidDisplay || cls == 0x010127d4u || cls == kIidFileMgr || cls == kIidHeap ||
         cls == kIidSound || cls == kIidGraphics || cls == kIidRootForm ||
         cls == kIidHid || cls == kIidSqlMgr || cls == kClsidMemAStream ||
         cls == kClsidUnzipStream ||
         (entrada_pronta_ && cls == kClsidSignalCBFactory) || e_o_titulo || e_classe_servida;
}

void Despacho::EscreverCabecalhoDeIdib(std::uint32_t obj, std::uint32_t pbmp,
                                       std::uint32_t largura, std::uint32_t altura) {
  // `AEEIDIB.h:42-55`, campo a campo. O `nPitch` e int16 e conta BYTES de uma
  // linha para a seguinte: com RGB565 sao dois por pixel.
  using C = zb2::brew::CamposDoIdib;
  // NASCE COM UMA REFERENCIA (a regra COM, e a que o `ConstruirObjeto` ja
  // seguia); as chamadas seguintes ao mesmo objecto nao a reiniciam.
  refs_do_dib_.emplace(obj, 1u);
  mem_.Escrever32(obj + C::kPvt, vtable_bitmap_);
  mem_.Escrever32(obj + C::kPPaletteMap, 0);  // ver `refs_do_dib_`: NAO e a contagem
  mem_.Escrever32(obj + C::kPBmp, pbmp);
  mem_.Escrever32(obj + C::kPRGB, 0);            // RGB565 e directo: nao ha paleta
  mem_.Escrever32(obj + C::kNcTransparent, 0);
  mem_.Escrever16(obj + C::kCx, static_cast<std::uint16_t>(largura));
  mem_.Escrever16(obj + C::kCy, static_cast<std::uint16_t>(altura));
  mem_.Escrever16(obj + C::kNPitch, static_cast<std::uint16_t>(largura * 2));
  mem_.Escrever16(obj + C::kCntRGB, 0);
  mem_.Escrever8(obj + C::kNDepth, 16);
  mem_.Escrever8(obj + C::kNColorScheme, C::kEsquemaDeCor565);
  // "initialize to 0 when constructing a DIB" (`AEEIDIB.h:53`).
  for (std::uint32_t k = C::kReservado; k < C::kTamanho; ++k) mem_.Escrever8(obj + k, 0);
}

std::uint32_t Despacho::EscreverCabecalhoDoBitmapDoEcra() {
  const std::uint32_t obj = zb2::brew::kObjDibBase + 0x300;
  // O ECRA TEM AGORA UM BUFFER NO GUEST (`core/brew/ecra.h`,
  // `kBaseDoEcraNoGuest`). A `Tela` continua a viver no hospedeiro e o guest ve
  // uma COPIA sincronizada: `ExporEcraAoGuest` num sentido,
  // `AbsorverEcraDoGuest` no outro, no `IDisplay::Update`.
  //
  // O QUE ISTO SUBSTITUI: o `pBmp` ficava a ZERO e registava-se a falta
  // `IDIB::pBmp do bitmap do ecra`. A falta era HONESTA quanto ao buffer e
  // MENTIROSA quanto a demanda -- era registada aqui, ao ESCREVER o cabecalho,
  // e por isso acusava os 25 titulos que pedem o bitmap do ecra por qualquer
  // motivo. Medido com a sonda de leitura (`Memoria::SondarLeitura`), so DOIS
  // leem o campo: `tekken2` e `zenonia`. Ver `core/brew/ecra.h`.
  EscreverCabecalhoDeIdib(obj, zb2::brew::kBaseDoEcraNoGuest, zb2::brew::Tela::kLargura,
                          zb2::brew::Tela::kAltura);
  if (!ecra_exposto_) {
    ecra_exposto_ = true;
    // A VIGIA DE SUJIDADE arma-se com o buffer: e ela que distingue "o guest
    // escreveu pixels" de "nos desenhamos".
    mem_.VigiarSujidade(zb2::brew::kBaseDoEcraNoGuest,
                        zb2::brew::kBaseDoEcraNoGuest + zb2::brew::kBytesDoEcra);
    traco_.RegistarPressuposto(Area::Brew, "ecra do guest sincronizado no Update",
                               "pBmp=" + Hex(zb2::brew::kBaseDoEcraNoGuest) + " " +
                                   std::to_string(zb2::brew::kBytesDoEcra) + " bytes");
  }
  // O CONTEUDO TEM DE ESTAR LA ANTES DE O PONTEIRO SAIR DAQUI: um titulo que
  // leia o ecra para o compor (alpha, scroll) leria zeros sobre desenho nosso.
  ExporEcraAoGuest();
  return obj;
}

// ---------------------------------------------------------------------------
// `IShell::DetectType` (slot 43). O contrato esta medido no comentario da tabela
// de mimes, acima; aqui esta so a traducao dele para a ABI.
// ---------------------------------------------------------------------------
bool Despacho::AtenderDetectType(ICpu& cpu) {
  const std::uint32_t sp = cpu.Get(kSP);
  const std::uint32_t cp_buf = cpu.Get(kR1);
  const std::uint32_t p_tamanho = cpu.Get(kR2);
  const std::uint32_t p_nome = cpu.Get(kR3);
  const std::uint32_t pp_mime = mem_.Ler32(sp);

  // 1. SEM DADOS E SEM NOME: "de quantos bytes precisas?". E o caso das 46
  //    chamadas medidas, e a resposta e a documentada -- `ENEEDMORE` com o
  //    numero de bytes que este detector precisa de ver para decidir.
  //
  //    O `pdwSize` NAO pode ser nulo aqui: sem sitio para escrever o numero, o
  //    `ENEEDMORE` seria um pedido que o chamador nao consegue cumprir (ele
  //    repetiria `NULL` para sempre). Nesse caso a resposta honesta e
  //    "nao sei o tipo", e a falta fica com o nome.
  if (cp_buf == 0 && p_nome == 0) {
    if (p_tamanho == 0) {
      traco_.RegistarFalta(Area::Brew, "IShell::DetectType",
                           "sem cpBuf, sem cpszName e com pdwSize nulo: nao ha resposta possivel");
      cpu.Set(kR0, kAeeNoType);
      return true;
    }
    mem_.Escrever32(p_tamanho, kBytesDoDetector);
    cpu.Set(kR0, kAeeNeedMore);
    traco_.Emitir(Area::Brew, Nivel::Depuracao, "ISHELL_DETECTTYPE",
                  "so-tamanho -> " + std::to_string(kBytesDoDetector) + " bytes (ENEEDMORE) lr=0x" + Hex(cpu.Get(kLR)));
    return true;
  }

  // 2. HA DADOS OU UM NOME: le-se o que ha e responde-se o MIME. O `*pdwSize` e
  //    a ENTRADA (quantos bytes o chamador tem) e o corte e o que o detector
  //    precisa -- nunca se le mais do que o chamador diz ter.
  const std::uint32_t disponiveis = (p_tamanho != 0) ? mem_.Ler32(p_tamanho) : 0;
  std::vector<std::uint8_t> bytes;
  if (cp_buf != 0) {
    const std::uint32_t quantos = std::min(disponiveis, kBytesDoDetector);
    bytes.reserve(quantos);
    for (std::uint32_t k = 0; k < quantos; ++k) {
      bytes.push_back(mem_.Ler8(cp_buf + k));
    }
  }
  const std::string nome = LerTextoDe(mem_, p_nome, 512);
  std::uint32_t mime = MimeDaMagia(bytes.empty() ? nullptr : bytes.data(), bytes.size());
  if (mime == kMimeNenhum) mime = MimeDaExtensao(nome);
  if (mime == kMimeNenhum) {
    // O QUE NAO SE SABE DIZ-SE. Nao se inventa um mime, e nao se devolve
    // sucesso: o contrato tem resposta para isto (`AEE_ENOTYPE`).
    cpu.Set(kR0, kAeeNoType);
    char det_sem_tipo[192];
    std::snprintf(det_sem_tipo, sizeof(det_sem_tipo),
                  "sem tipo: %u bytes de %u no cpBuf, cpszName=%s, cpBuf=0x%08x lr=0x%08x",
                  static_cast<unsigned>(bytes.size()), static_cast<unsigned>(disponiveis),
                  nome.empty() ? "-" : nome.c_str(), cp_buf, cpu.Get(kLR));
    traco_.Emitir(Area::Brew, Nivel::Depuracao, "ISHELL_DETECTTYPE", det_sem_tipo);
    return true;
  }
  const std::uint32_t endereco = EscreverMimeNoGuest(mime);
  if (pp_mime != 0) mem_.Escrever32(pp_mime, endereco);
  // Nenhum byte ADICIONAL faz falta depois de o tipo estar identificado.
  if (p_tamanho != 0) mem_.Escrever32(p_tamanho, 0);
  cpu.Set(kR0, kAeeSuccess);
  traco_.Emitir(Area::Brew, Nivel::Depuracao, "ISHELL_DETECTTYPE",
                std::string(kMimes[mime]) + " (SUCCESS, " + std::to_string(bytes.size()) +
                    " bytes lidos" + (pp_mime == 0 ? ", pcpszMIME nulo" : "") + ", lr=0x" +
                    Hex(cpu.Get(kLR)) + ")");
  return true;
}

std::uint32_t Despacho::EscreverMimeNoGuest(std::uint32_t indice) {
  const std::uint32_t p = kZonaDeMimesDoShell + indice * kPassoDeMime;
  const std::string texto = kMimes[indice];
  for (std::size_t k = 0; k < texto.size(); ++k) {
    mem_.Escrever8(p + static_cast<std::uint32_t>(k), static_cast<std::uint8_t>(texto[k]));
  }
  mem_.Escrever8(p + static_cast<std::uint32_t>(texto.size()), 0);
  return p;
}

// ---------------------------------------------------------------------------
// `IShell::LoadResObject` (slot 19). O irmao do `LoadResDataEx` (slot 41): le o
// MESMO contentor, com a diferenca de devolver um objecto em vez do bloco cru.
// ---------------------------------------------------------------------------
//
// A ASSINATURA, de `AEEIShell.h:251` e da macro em `:433`:
//
//   IBase *(*LoadResObject)(iname *po, const char *pszResFile, uint16 nResID,
//                           AEECLSID cls);
//
// O 4o argumento esta DOCUMENTADO como `AEEHandlerType hType` (`:2315`) e os
// valores do SDK sao `HTYPE_VIEWER = AEECLSID_VIEW = 0x01004000` e
// `HTYPE_SOUND = AEECLSID_SOUNDPLAYER = 0x01002000` (`AEEClassIDs.h:28` e `:157`,
// citados em `AEEIShell.h:3720`). E a documentacao do `IResFile` diz o que a
// variante deste SDK aceita no mesmo lugar (`AEEIResFile.h:750`): "clsid: Class
// ID of the handler ... Or IID of the interface to be retrieved".
//
// MEDIDO (2 titulos, 3 chamadas, `ZB2_TRACE=1`):
//
//   quake       r1=0x0000a0c0 ("fs:/~/../id1/splash_title.png") id=0 cls=0
//   toyraidzeebo r1=0x00039984 ("toyraidzeebo.pod")             id=1   cls=0x01001021
//   toyraidzeebo o mesmo nome                                   id=0x20 cls=0x01001021
//
// Os dois casos sao os DOIS ramos que o zeebx documenta (`src/machine/shell.rs`,
// `shell_load_res_object`): com `nResID == 0` o ficheiro inteiro E o recurso
// (o caso do quake), e com `nResID != 0` o que o nome indica e um contentor e a
// entrada e a daquele id. O `cls = 0x01001021` e o `AEECLSID_BITMAP`
// (`AEEClassIDs.h:94`, o mesmo valor do `AEEIID_IBitmap`): o que se pede e um
// BITMAP, e nao um `IImage`.
//
// O NOME E UM CAMINHO, e nao o nome de um ficheiro da pasta: o `fs:/~/../id1/`
// do quake e resolvido pela MESMA VFS que serve o `OpenFile` (uma so regra
// sobre o que existe).
bool Despacho::AtenderLoadResObject(ICpu& cpu) {
  const std::uint32_t p_nome = cpu.Get(kR1);
  const std::uint16_t id = static_cast<std::uint16_t>(cpu.Get(kR2));
  const std::uint32_t cls = cpu.Get(kR3);
  const std::string nome = LerTextoDe(mem_, p_nome, 512);
  // O CONTRATO DE FALHA E `NULL`, e escreve-se ANTES de qualquer trabalho: um
  // caminho de recusa que deixe o r0 com o valor do guest entregava-lhe um
  // ponteiro que ele acredita ser um objecto.
  cpu.Set(kR0, 0);
  if (nome.empty()) {
    traco_.RegistarFalta(Area::Brew, "IShell::LoadResObject", "pszResFile nulo ou vazio");
    return true;
  }

  // 1. OS BYTES. O leitor e o MESMO do `LoadResDataEx` (a pasta do titulo pela
  //    VFS), para nao haver duas regras sobre que ficheiro existe.
  std::vector<std::uint8_t> bytes;
  std::string motivo;
  const LeitorDeRecursos leitor = LeitorDaPasta(dir_ + "/" + pasta_, &vfs_);
  if (!leitor(nome, &bytes, &motivo)) {
    traco_.RegistarFalta(Area::Brew, "IShell::LoadResObject",
                         nome + " id=" + std::to_string(id) + ": " + motivo);
    return true;
  }

  const std::uint8_t* dados = bytes.data();
  std::size_t n = bytes.size();
  std::string mime;
  // O CONTENTOR TEM DE VIVER ATE AO FIM DA FUNCAO. `dados`/`n` passam a apontar
  // para dentro de `contentor.bytes_`: com o contentor declarado DENTRO do `if`
  // abaixo, era destruido no fim do bloco e a `DescodificarPng` recebia um
  // ponteiro PENDENTE.
  //
  // MEDIDO (frente fora, `toyraidzeebo` medido SOZINHO): SIGSEGV em
  // `zb2::DescodificarPng` (`core/carga/png.cpp:126`), com
  // `dados=0x7ffff61e0178` ja NAO mapeado e `tamanho=10676`. O `bytes_` de um
  // contentor grande vive num `mmap` que a destruicao DEVOLVE ao sistema; nos
  // titulos em que o bloco libertado fica na arena do `malloc` (o caso da
  // corrida dos 62) o defeito nao rebenta -- le-se como lixo, e o `toyraidzeebo`
  // so aparecia na corrida completa por isso.
  ArquivoBar contentor;
  if (id != 0) {
    // 1b. O `pszResFile` e um CONTENTOR e o id escolhe a entrada. O `.pod` do
    //     `toyraidzeebo` foi medido com o MESMO formato do `.bar` (`0x11 0x00`,
    //     registos de 8 bytes, tabela de deslocamentos: `tools/medir_bar.py
    //     censo` diz "1 de 1 ficheiros com o formato medido"), logo o leitor e o
    //     mesmo -- e nao um segundo leitor a espera de divergir.
    contentor = ArquivoBar::AbrirDados(bytes, &motivo);
    if (!contentor.Valido()) {
      traco_.RegistarFalta(Area::Brew, "IShell::LoadResObject",
                           nome + " id=" + std::to_string(id) + ": " + motivo);
      return true;
    }
    const RecursoDoBar recurso = contentor.Ler(id, kTipoImagem);
    if (!recurso.ok) {
      traco_.RegistarFalta(Area::Brew, "IShell::LoadResObject",
                           nome + " id=" + std::to_string(id) + " tipo=" +
                               std::to_string(kTipoImagem) + ": " + recurso.motivo);
      return true;
    }
    // O `AEEResBlob` E NOSSO PARA SALTAR: quem pediu foi um OBJECT de imagem, e
    // nao o bloco cru com o mime impresso que o `LoadResDataEx` entrega. Se o
    // recurso nao tiver a forma de blob, os bytes do recurso sao o dado.
    const BlobDoBar blob = ArquivoBar::LerBlob(recurso);
    dados = blob.ok ? blob.dados : recurso.dados;
    n = blob.ok ? blob.tamanho : recurso.tamanho;
    if (blob.ok) mime = blob.mime;
  }

  // 2. A IMAGEM. So o PNG tem descodificador nesta arvore (`core/carga/png.h`);
  //    o resto e recusado COM O NOME do que se viu, e nao com um "falhou".
  zb2::ImagemPng img;
  std::string porque;
  if (!zb2::DescodificarPng(dados, n, &img, &porque)) {
    traco_.RegistarFalta(Area::Brew, "IShell::LoadResObject",
                         nome + " id=" + std::to_string(id) + " (" +
                             std::to_string(n) + " bytes" +
                             (mime.empty() ? "" : ", mime=" + mime) +
                             "): nao descodifica como PNG: " + porque);
    return true;
  }

  // 3. O OBJECT. Os pixels vivem no heap do GUEST (e o jogo que os le), e o
  //    cabecalho publico do IDIB e escrito pela MESMA funcao que o escreve para
  //    o ecra e para os `IDisplay::CreateDIBitmap`.
  const std::uint32_t obj = IdibLivre();
  if (obj == 0) {
    traco_.RegistarFalta(Area::Brew, "IShell::LoadResObject",
                         nome + " id=" + std::to_string(id) +
                             ": a banda dos bitmaps compativeis esta cheia");
    return true;
  }
  const std::uint32_t bytes_dos_pixels = img.largura * 2u * img.altura;
  const std::uint32_t pixels = al_.Malloc(bytes_dos_pixels);
  if (pixels == 0) {
    char det[96];
    std::snprintf(det, sizeof(det), "%ux%u pede %u bytes e o heap do guest nao os deu",
                  static_cast<unsigned>(img.largura), static_cast<unsigned>(img.altura),
                  static_cast<unsigned>(bytes_dos_pixels));
    traco_.RegistarFalta(Area::Brew, "IShell::LoadResObject", det);
    return true;
  }
  for (std::uint32_t k = 0; k < img.pixels.size(); ++k) {
    mem_.Escrever16(pixels + k * 2u, img.pixels[k]);
  }
  EscreverCabecalhoDeIdib(obj, pixels, img.largura, img.altura);
  // A LEITURA DE VOLTA, e a mesma guarda do `CriarDibDoPng`: um objecto com o
  // `+0` a zero e um `blx 0` no primeiro metodo que o jogo lhe chamar.
  if (mem_.Ler32(obj + zb2::brew::CamposDoIdib::kPvt) != vtable_bitmap_ ||
      mem_.Ler32(obj + zb2::brew::CamposDoIdib::kPBmp) != pixels) {
    traco_.RegistarFalta(Area::Brew, "IShell::LoadResObject",
                         "o cabecalho do IDIB novo nao ficou escrito em " + Hex(obj));
    return true;
  }
  cpu.Set(kR0, obj);
  traco_.Emitir(Area::Brew, Nivel::Depuracao, "ISHELL_LOADRESOBJECT",
                nome + " id=" + std::to_string(id) + " cls=" + Hex(cls) + " -> IBitmap " +
                    Hex(obj) + " " + std::to_string(img.largura) + "x" +
                    std::to_string(img.altura) + (img.tem_alpha ? " com alfa" : ""));
  return true;
}

// ---------------------------------------------------------------------------
// O REGISTO DE HANDLERS (`IShell::GetHandler`, slot 32).
//
// ===========================================================================
// 1) O CONTRATO, do cabecalho do SDK -- e nao adivinhado
// ===========================================================================
//
// `platform/system/inc/AEEIShell.h:800` (a lista `INHERIT_IShell`, a MESMA que
// da o `kShell_DetectType = 43` e o `kShell_LoadResObject = 19`) e a macro em
// `:876`:
//
//   AEECLSID (*GetHandler)(iname *po, AEECLSID cls, const char *pszIn);
//   #define ISHELL_GetHandler(p,t,psz)  GET_PVTBL(p,IShell)->GetHandler(p,t,psz)
//
// Em AAPCS: `r0=po`, `r1=clsBase`, `r2=pszIn`. O SLOT E O 32, e nao um numero
// contado a mao: `tools/gerar_slots.py` gerou `kShell_GetHandler = 32` em
// `tools/brew_slots.inc:45` a partir desta lista, e o zeebulator conta o mesmo
// (`core/brew/ishell.cpp:227`, "// 32 GetHandler").
//
// O bloco de documentacao (`AEEIShell.h:6261-6288`) diz o que cada argumento e
// e o que se devolve:
//
//   clsBase : Handler type (HTYPE_VIEWER, HTYPE_SOUND) or an AEECLSID base
//             class for the handler to meet.
//   pszIn   : Input string.
//   Return  : AEECLSID of the associated handler class.
//             0 (zero), if otherwise.
//
// O USO E O DO PROPRIO SDK, escrito TRES vezes e sempre com a mesma forma:
//
//   AEEIShell.h:7116 (o exemplo da doc do `DetectType`)
//       clsHandler = ISHELL_GetHandler(pIShell, AEECLSID_MEDIA, cpszMIME);
//       if (clsHandler) ISHELL_CreateInstance(pIShell, clsHandler, ...);
//   doc/AEEMedia.txt:281 ("first determine the media MIME type using the
//       ISHELL_DetectType() API. Next, obtain the handler IMedia ClassID for
//       the MIME type using ISHELL_GetHandler() and the handler type
//       AEECLSID_MEDIA")
//   doc/AEEMedia.txt:327 (o exemplo, `cls = ISHELL_GetHandler(ps,
//       AEECLSID_MEDIA, cpszMIME); if (cls) *pCls = cls;`)
//
// ===========================================================================
// 2) O QUE OS TITULOS PEDEM, MEDIDO (`ZB2_TRACE=1`, 95 chamadas)
// ===========================================================================
//
// Em TODAS as 95 chamadas: `r1 = 0x01005500` (`AEECLSID_MEDIA`) e o `r2` e uma
// cadeia de MIME na memoria do guest (`0x800204a0` / `0x800204c0`, a mesma do
// `DetectType`). Nenhuma outra coisa muda.
//
//   mime             chamadas  onde (o `lr` de cada sitio de chamada)
//   audio/wav              87  abd 0x16e4 (34+1) | ridgeracer 0x1b18 (19)
//                              torkandkral 0x1830 (17) | toyraidzeebo 0x22cc (14)
//                              pacmania 0x1268 (3)
//   audio/mpeg              8  ridgeracer 0x1b18 (6) | toyraidzeebo 0x22cc (1)
//                              abd 0x16e4 (1)
//
// Os 95 pedidos vem dos 5 titulos que a frente `ishell2` deixou nomeados:
// abd 35, ridgeracer 25, torkandkral 17, toyraidzeebo 15, pacmania 3.
//
// ===========================================================================
// 3) O QUE O GUEST FAZ COM ISTO, desmontado (base ZERO, `abd.mod`)
// ===========================================================================
//
// O sitio de chamada do `abd` e o exemplo do `AEEMedia.txt` traduzido para
// ARM, instrucao a instrucao -- a sonda de tamanho do `DetectType` esta em
// 0x134c e a segunda chamada, com dados, em 0x16b8:
//
//   000016a0  add  r3, sp, #0x0c     ; r3 = &algo
//   000016a4  str  r3, [sp]          ; [sp+0] = &cpszMIME  (o 5o argumento)
//   000016a8  ldr  r0, [r5]          ; r0 = o objecto IShell
//   000016ac  mov  r3, #0            ; r3 = 0 (cpszName)
//   000016b0  add  r2, sp, #0x30     ; r2 = &dwSize
//   000016b4  mov  r1, r6            ; r1 = o buffer com os bytes lidos
//   000016b8  ldr  ip, [r0, #0xac]   ; 0xac = 43*4  -> DetectType
//   000016c0  blx  ip
//   000016c4  movs r7, r0            ; r7 = o retorno do DetectType
//   000016c8  bne  #0x16ec           ; != SUCCESS -> nao ha handler a pedir
//   000016cc  ldr  r0, [r5]          ; r0 = o IShell
//   000016d0  ldr  r2, [sp, #0x0c]   ; r2 = o MIME que o DetectType devolveu
//   000016d4  ldr  r1, [pc, #0x40]   ; r1 = *0x171c = 0x01005500 = AEECLSID_MEDIA
//   000016d8  ldr  r3, [r0, #0x80]   ; 0x80 = 32*4  -> GetHandler
//   000016dc  mov  r0, r5
//   000016e0  blx  r3
//   000016e4  cmp  r0, #0            ; <<< o `lr` medido
//   000016e8  strne r0, [sb]         ; if (cls) *pCls = cls;   -- o exemplo do SDK
//   00001708  mov  r0, r7
//   0000170c  b    #0x1378           ; volta ao chamador com o CLSID em r0
//
// E o literal foi lido do proprio ficheiro: `abd.mod[0x171c] = 0x01005500`.
// Logo o `0` e o "nao ha handler", e quem o recebe NAO chama o CreateInstance
// com uma classe inventada -- o `strne` so escreve quando o retorno nao e zero.
//
// Os outros quatro titulos tem o MESMO codigo no seu sitio de chamada (o
// `ldr r3, [r0, #0x80]` a seguir ao `DetectType` e o `strne r0, [sb]`):
// `pacmania.mod` 0x1264, `torkandkral.mod` 0x182c, e a mesma forma no
// `ridgeracer` 0x1b14 e no `toyraidzeebo` 0x22c8.
//
// E o que se segue, medido na corrida: cada `GetHandler` e seguido de um
// `IShell::CreateInstance` com EXACTAMENTE o valor que este slot devolveu -- as
// 95 recusas `iid=0x00000014` eram o `kAeeUnsupported` (20) que o ramo generico
// escrevia no r0. Ou seja: **a recusa deste slot virava um pedido de criacao de
// uma classe que nao existe**, e o jogo ficava sem o objecto de midia.
//
// ===========================================================================
// 4) A TABELA, e a CONTRADICAO escrita com a referencia
// ===========================================================================
//
// O que o slot devolve e o AEECLSID do handler REGISTADO. No aparelho o registo
// e montado no arranque pelos proprios handlers (`ISHELL_RegisterHandler`); o
// que se pode citar e o mapa que o SDK publica em
// `platform/system/inc/AEEMimeTypes.h`, onde o NOME da macro diz QUAL e a
// classe do handler. Duas linhas importam aqui, e as duas foram medidas:
//
//   :71  #define MT_AUDIO_ADPCM  "audio/wav"     <-- o WAV e do handler ADPCM
//   :37  #define ADPCM_EXTENSION "wav"
//   :64  #define MT_AUDIO_MP3    "audio/mp3"
//
// **CONTRADICAO, e ela e da referencia, nao da medicao**: o zeebx mapeia
// `audio/wav` -> `AEECLSID_MEDIAPCM` (`src/machine.rs:250`, `handler_for`).
// O SDK diz o contrario em dois sitios independentes: `MT_AUDIO_ADPCM` chama-se
// ADPCM e vale "audio/wav" (`AEEMimeTypes.h:71`, e o `ADPCM_EXTENSION` e "wav"),
// e `doc/AEEMedia.txt:807-810` toca TRES ficheiros `a1.wav`/`a2.wav`/`a3.wav`
// com `AEECLSID_MEDIAADPCM`. O `AEECLSID_MEDIAPCM` e outra coisa: o exemplo de
// streaming do `AEEMedia.txt:1146` usa-o para um `sample.raw` -- PCM linear CRU
// com `AEEMediaWaveSpec`, e nao um contentor WAV. O zeemu concorda com o SDK
// (`brew/BrewShell.cpp:218`, `audio/wav` -> `0x0100550a`). Serve-se o SDK.
//
// A tabela so tem entradas cuja classe PERTENCE a familia que esta arvore crIA
// (`zb2::brew::ClasseDeMidia`, `core/brew/imedia.cpp`: 0x01005500..0x01005514).
// Um MIME que o SDK nomeia e que nao tem classe nesta familia (`audio/wma`,
// `video/wmv`) NAO entra: seria uma resposta que nao se pode cumprir.
struct LinhaDoRegisto {
  std::uint32_t base;     // o `clsBase` pedido
  const char* mime;       // a cadeia do `pszIn`
  std::uint32_t handler;  // o AEECLSID do handler
  const char* origem;     // onde este par saiu (cabecalho:linha, ou referencia)
};

// `AEECLSID_MEDIA`: `AEEClassIDs.h:278` (`AEECLSID_MULTIMEDIA = QVERSION+0x5500`
// = 0x01005500), e o `AEEIMedia.h:27` escreve-o por extenso no comentario:
// "AEEIID_IMedia 0x01005500 // This is AEECLSID_MEDIA". E o base class que o
// `doc/AEEMedia.txt:281` manda usar para pedir um handler de midia.
constexpr std::uint32_t kClsBaseMedia = 0x01005500u;

const LinhaDoRegisto kRegistoDeHandlers[] = {
    // O PAR MEDIDO nesta arvore (87+8 chamadas): e o unico que o corpus pede.
    {kClsBaseMedia, "audio/wav", 0x0100550au, "AEEMimeTypes.h:71 MT_AUDIO_ADPCM"},
    {kClsBaseMedia, "audio/mpeg", 0x01005502u, "AEEMimeTypes.h:64 MT_AUDIO_MP3"},
    // Os nomes alternativos do MESMO par, aceites pelas referencias (zeemu
    // `BrewShell.cpp:215-218`) e escritos no proprio `AEEMimeTypes.h`.
    {kClsBaseMedia, "audio/x-wav", 0x0100550au, "zeemu BrewShell.cpp:218"},
    {kClsBaseMedia, "audio/wave", 0x0100550au, "zeemu BrewShell.cpp:218"},
    {kClsBaseMedia, "snd/wav", 0x0100550au, "AEEMimeTypes.h:52 MT_ADPCM"},
    {kClsBaseMedia, "audio/mp3", 0x01005502u, "AEEMimeTypes.h:64"},
    {kClsBaseMedia, "snd/mp3", 0x01005502u, "AEEMimeTypes.h:45 MT_MP3"},
    // O resto do registo do SDK, cada linha com a sua macro. `MT_*` -> a classe
    // com o mesmo nome (`AEEClassIDs.h:328-345`).
    {kClsBaseMedia, "audio/mid", 0x01005501u, "AEEMimeTypes.h:63 MT_AUDIO_MIDI"},
    {kClsBaseMedia, "audio/midi", 0x01005501u, "zeemu BrewShell.cpp:215"},
    {kClsBaseMedia, "snd/midi", 0x01005501u, "AEEMimeTypes.h:44"},
    {kClsBaseMedia, "audio/qcp", 0x01005503u, "AEEMimeTypes.h:65 MT_AUDIO_QCP"},
    {kClsBaseMedia, "audio/vnd.qcelp", 0x01005503u, "AEEMimeTypes.h:66"},
    {kClsBaseMedia, "snd/qcp", 0x01005503u, "AEEMimeTypes.h:46"},
    {kClsBaseMedia, "snd/vnd.qcelp", 0x01005503u, "AEEMimeTypes.h:47 MT_VNDQCP"},
    {kClsBaseMedia, "video/pmd", 0x01005504u, "AEEMimeTypes.h:83 MT_VIDEO_PMD"},
    {kClsBaseMedia, "audio/qcf", 0x01005506u, "AEEMimeTypes.h:67 MT_AUDIO_QCF"},
    {kClsBaseMedia, "snd/qcf", 0x01005506u, "AEEMimeTypes.h:48 MT_QCF"},
    {kClsBaseMedia, "video/mp4", 0x01005507u, "AEEMimeTypes.h:84 MT_VIDEO_MPEG4"},
    {kClsBaseMedia, "audio/mmf", 0x01005508u, "AEEMimeTypes.h:68 MT_AUDIO_MMF"},
    {kClsBaseMedia, "snd/mmf", 0x01005508u, "AEEMimeTypes.h:49"},
    {kClsBaseMedia, "audio/spf", 0x01005509u, "AEEMimeTypes.h:69 MT_AUDIO_PHR"},
    {kClsBaseMedia, "snd/spf", 0x01005509u, "AEEMimeTypes.h:50 MT_PHR"},
    {kClsBaseMedia, "audio/aac", 0x0100550bu, "AEEMimeTypes.h:72 MT_AUDIO_AAC"},
    {kClsBaseMedia, "snd/aac", 0x0100550bu, "AEEMimeTypes.h:53"},
    {kClsBaseMedia, "audio/imy", 0x0100550cu, "AEEMimeTypes.h:70 MT_AUDIO_IMELODY"},
    {kClsBaseMedia, "snd/imy", 0x0100550cu, "AEEMimeTypes.h:51"},
    {kClsBaseMedia, "audio/amr", 0x0100550eu, "AEEMimeTypes.h:73 MT_AUDIO_AMR"},
    {kClsBaseMedia, "snd/amr", 0x0100550eu, "AEEMimeTypes.h:54"},
    {kClsBaseMedia, "audio/hvs", 0x0100550fu, "AEEMimeTypes.h:75 MT_AUDIO_HVS"},
    {kClsBaseMedia, "audio/saf", 0x01005510u, "AEEMimeTypes.h:76 MT_AUDIO_SAF"},
    {kClsBaseMedia, "audio/xmf", 0x01005512u, "AEEMimeTypes.h:77 MT_AUDIO_XMF"},
    {kClsBaseMedia, "audio/mxmf", 0x01005512u, "AEEMimeTypes.h:78"},
    {kClsBaseMedia, "audio/xmf0", 0x01005512u, "AEEMimeTypes.h:79"},
    {kClsBaseMedia, "audio/xmf1", 0x01005512u, "AEEMimeTypes.h:80"},
    {kClsBaseMedia, "audio/dls", 0x01005513u, "AEEMimeTypes.h:81 MT_AUDIO_DLS"},
    {kClsBaseMedia, "video/svg", 0x01005514u, "AEEMimeTypes.h:86 MT_VIDEO_SVG"},
    {kClsBaseMedia, "video/svgz", 0x01005514u, "AEEMimeTypes.h:87"},
};
constexpr std::size_t kQuantasLinhasDoRegisto =
    sizeof(kRegistoDeHandlers) / sizeof(kRegistoDeHandlers[0]);

// O NOME DOS TRES TIPOS DE HANDLER DEPRECADOS (`AEEIShell.h:582-584`), para o
// detalhe da recusa nao dizer so um numero. Fora dessa faixa a resposta e vazia:
// o `clsBase` e um AEECLSID e o nome dele vem do `DescreverClsid`.
const char* NomeDoTipoDeHandler(std::uint32_t base) {
  switch (base) {
    case 0: return " (HTYPE_VIEWER)";
    case 1: return " (HTYPE_SOUND)";
    case 2: return " (HTYPE_BROWSE)";
    default: return "";
  }
}

const LinhaDoRegisto* ProcurarNoRegisto(std::uint32_t base, const std::string& mime) {
  for (std::size_t k = 0; k < kQuantasLinhasDoRegisto; ++k) {
    if (kRegistoDeHandlers[k].base == base && mime == kRegistoDeHandlers[k].mime) {
      return &kRegistoDeHandlers[k];
    }
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// `IShell::GetHandler` (slot 32) -- a ABI, e a resposta
// ---------------------------------------------------------------------------
bool Despacho::AtenderGetHandler(ICpu& cpu) {
  const std::uint32_t base = cpu.Get(kR1);
  const std::uint32_t p_entrada = cpu.Get(kR2);
  // A CADEIA SO SE LE SE O PONTEIRO ESTIVER NA JANELA DO GUEST. Um `pszIn` que
  // nao esteja nao pode provocar uma leitura a mais: uma leitura num endereco
  // nao mapeado ficaria registada com o PC da instrucao seguinte e mudaria o que
  // a bateria mede (ver a memoria
  // `a_leitura_de_dados_em_endereco_nao_mapeado_do_curupira`).
  const bool tem_ponteiro = (p_entrada >= 0x00100000u && p_entrada < 0x81000000u);
  const std::string entrada = tem_ponteiro ? LerTextoDe(mem_, p_entrada, 64) : std::string();

  // A ABI CRUA, SO COM `ZB2_TRACE=1`: foi esta linha que mediu o `clsBase`
  // (0x01005500 nas 95 chamadas) e a cadeia do `pszIn` -- o `txt` do detalhe da
  // falta le o `r1`, e neste slot o `r1` e um CLSID, nao texto.
  traco_.Emitir(Area::Brew, Nivel::Depuracao, "ISHELL_GETHANDLER_ABI",
                "clsBase=0x" + Hex(base) + " pszIn=\"" +
                    (entrada.empty() ? (tem_ponteiro ? "<vazia>" : "<ponteiro fora do guest>")
                                     : entrada) +
                    "\" lr=0x" + Hex(cpu.Get(kLR)));

  const LinhaDoRegisto* linha =
      entrada.empty() ? nullptr : ProcurarNoRegisto(base, entrada);
  std::uint32_t resposta = (linha != nullptr) ? linha->handler : 0;

  // NAO SE ENTREGA UMA CLASSE QUE NAO SE CRIA. Se a linha do registo apontar
  // para uma classe que o `CreateInstance` desta arvore recusaria, o jogo
  // ficaria com um AEECLSID que nao da em nada -- e o pedido seguinte, esse
  // sim, e que apareceria como "CLSID desconhecido". Aqui ve-se antes.
  if (resposta != 0 && (media_ == nullptr || !zb2::brew::ClasseDeMidia(resposta))) {
    traco_.RegistarFalta(Area::Brew, "IShell::GetHandler handler nao criavel",
                         std::string(linha->origem) + ": 0x" + Hex(resposta) + " para '" +
                             entrada + "' nao e classe desta arvore");
    resposta = 0;
  }

  // O QUE NAO SE SERVE DIZ-SE COM O NOME -- e o `0` e a resposta que o SDK
  // preve ("0 (zero), if otherwise"), nao um erro inventado. As tres razoes
  // ficam separadas porque exigem correcoes diferentes: um `pszIn` que nao
  // chegou, um `clsBase` cujo registo nao temos, e um MIME que este registo
  // nao conhece.
  if (resposta == 0) {
    char det[224];
    if (entrada.empty()) {
      std::snprintf(det, sizeof(det),
                    "pszIn %s (0x%08x) com clsBase=0x%08x lr=0x%08x",
                    tem_ponteiro ? "vazio" : "nulo ou fora do guest", p_entrada, base,
                    cpu.Get(kLR));
      traco_.RegistarFalta(Area::Brew, "IShell::GetHandler pszIn nulo ou vazio", det);
    } else if (linha == nullptr && base != kClsBaseMedia) {
      // O REGISTO QUE NAO TEMOS. O `clsBase` tem DUAS formas no cabecalho
      // (`AEEIShell.h:6280`): "Handler type (HTYPE_VIEWER, HTYPE_SOUND) or an
      // AEECLSID base class". A primeira e o enum DEPRECADO -- `HTYPE_VIEWER`
      // e **0**, `HTYPE_SOUND` **1**, `HTYPE_BROWSE` **2** (`:582-584`; o
      // cabecalho di-lo por extenso em `:568`: "***deprecated****"), e ela
      // pede um IViewer/ISoundPlayer que esta arvore nao sabe criar; a segunda
      // e um AEECLSID (o 0x01004000 e o `AEECLSID_VIEW`, `AEEClassIDs.h:28`).
      // O nome entra no detalhe para o pedido seguinte se contar sem ir ao
      // cabecalho.
      std::snprintf(det, sizeof(det), "clsBase=0x%08x%s (%s) mime='%s' lr=0x%08x", base,
                    NomeDoTipoDeHandler(base), zb2::brew::DescreverClsid(base).c_str(),
                    entrada.c_str(), cpu.Get(kLR));
      traco_.RegistarFalta(Area::Brew,
                           "IShell::GetHandler sem registo para o clsBase", det);
    } else {
      std::snprintf(det, sizeof(det), "mime='%s' clsBase=0x%08x lr=0x%08x", entrada.c_str(),
                    base, cpu.Get(kLR));
      traco_.RegistarFalta(Area::Brew, "IShell::GetHandler sem handler para o MIME", det);
    }
  } else {
    traco_.Emitir(Area::Brew, Nivel::Depuracao, "ISHELL_GETHANDLER",
                  "'" + entrada + "' -> 0x" + Hex(resposta) + " (" +
                      zb2::brew::DescreverClsid(resposta) + ", " + linha->origem +
                      ") lr=0x" + Hex(cpu.Get(kLR)));
  }
  cpu.Set(kR0, resposta);
  return true;
}

// ---------------------------------------------------------------------------
// O `GetClassItemID` -- o SLOT 45 do IShell (`tools/brew_slots.inc:58`).
//
// O CONTRATO, do SDK (`AEEShell.h:813`; ficha em `:7239`):
//
//     uint32 GetClassItemID(iname *po, AEECLSID cls);
//     "This method returns a 32-bit unique identifier associated with the
//      owning module for the specified class ID."
//     "Return Value: 0 - Class not found or module is static"   (`:7255`)
//
// O QUE O TITULO FAZ COM O QUE ELE DEVOLVE -- desmonte do `tectoy` (`274755`),
// base ZERO (os literais do `.mod` sao offsets do ficheiro), no `Tectoy_FixupTime`
// (0x69b20) do `Tectoy.c`:
//
//     69b4c  ldr r3,[r1,#8]      ; IShell slot 2 = CreateInstance
//     69b58  mov r1,#0x1000000   ; AEECLSID_DOWNLOAD (o literal e este `mov`)
//     69b60  bx  r3              ; CreateInstance(po, DOWNLOAD, &m_pDownload)
//     69b64  cmp r0,#0 ; beq 0x69ba4        ; 0 = criou -> caminho bom
//     69ba4  ldr r0,[r5,#0x20]   ; o IShell
//     69ba8  ldr r7,[pc,#0xdc]   ; 0x01070798 -- lido em 0x69c8c: o CLSID DO PROPRIO TITULO
//     69bb4  ldr r2,[r1,#0xb4]   ; slot 45 (0xb4 = 45*4) = GetClassItemID
//     69bb8  mov r1,r7
//     69bbc  bx  r2              ; GetClassItemID(po, 0x01070798)
//     69bc0  movs r6,r0
//     69bc4  bne 0x69bdc         ; id != 0 -> USA este id
//     69bc8  ldr r0,[r4,#0x57c]  ; id == 0 -> VIA ALTERNATIVA: o IDownload que criou
//     69bd0  bl  0x59f58         ; que chama o slot 3 dele (lista de falhados) e, por
//                                ; cada id, o slot 4 (o AppModInfo), a procura de um
//                                ; classID igual a 0x01070798 (`59fd8 cmp r2,r7`)
//     69bd8  beq 0x69b44         ; nao achou -> DESISTE e retorna
//     69bf0  ldr ip,[r1,#0x54]   ; slot 21 (0x54 = 21*4) do IDownload
//     69bf4  mov r1,r6           ; com o id que veio do slot 45
//
// Duas conclusoes, as duas medidas:
//
//  1. **o valor do slot 45 E um id de item.** E o mesmo valor que o `IDownload`
//     recebe no slot 21, que tem a forma de `IDOWNLOAD_GetItemInfo(po, id, cb,
//     ctx)` (`OATDownload.c:589`). Nao e um codigo de erro nem um booleano.
//  2. **o zero NAO e neutro aqui:** e a porta da via alternativa -- varrer a fila
//     de downloads falhados. Nesta maquina essa fila nao existe, logo um zero
//     manda o titulo DESISTIR (0x69bd8) por um caminho que nao e o dele.
//
// O NUMERO QUE SE DEVOLVE. O id de item de um modulo e o nome da PASTA dele na
// NAND -- `mod/274755/tectoy.mod` --, e a pasta e numerica nos 65 `.mod` do
// corpus. O `SituarTitulo` ja entrega esse nome ao Despacho (`pasta_`), que e o
// mesmo valor que serve o `LoadResString` (`dir_ + "/../mif/" + pasta_ + ".mif"`).
// Segunda prova no mesmo lugar: o `.mif` do titulo chama-se `274755.mif`, tambem
// com o numero no NOME.
//
// CONTRADICAO DECLARADA: o `zeebx` (`src/machine/shell.rs:401`) escreve que o
// numero "e a mesma numeracao que aparece no `.mif`". MEDIDO: **nao aparece.** O
// `mif/274755.mif` (1480 bytes) nao contem `274755` em ASCII, nem em 32 bits
// little-endian nem big-endian. O que existe e o NOME do ficheiro. A conclusao
// (id = numero da pasta) nao muda -- mas a prova e a pasta, e nao o conteudo.
bool Despacho::AtenderGetClassItemID(ICpu& cpu) {
  const std::uint32_t cls = cpu.Get(kR1);
  // A ABI CRUA, so com `ZB2_TRACE=1`: e ela que confirma que o `r1` e o CLSID do
  // titulo, e nao um ponteiro de texto (a armadilha do ramo generico, que lia
  // "texto" de um `0x01070798`).
  traco_.Emitir(Area::Brew, Nivel::Depuracao, "ISHELL_GETCLASSITEMID_ABI",
                "cls=" + Hex(cls) + " lr=" + Hex(cpu.Get(kLR)));

  // A CLASSE DE OUTRO MODULO (ou um titulo sem CLSID conhecido): o SDK manda
  // devolver 0 -- "Class not found or module is static". Fica DECLARADO, porque
  // este caminho nao foi medido em nenhum titulo do corpus: e uma resposta dada,
  // nao uma leitura.
  if (!tem_clsid_ || cls != clsid_titulo_) {
    char det[160];
    std::snprintf(det, sizeof(det), "cls=0x%08x; o clsid deste modulo e %s lr=0x%08x", cls,
                  tem_clsid_ ? Hex(clsid_titulo_).c_str() : "desconhecido", cpu.Get(kLR));
    traco_.RegistarPressuposto(Area::Brew, "IShell::GetClassItemID de classe alheia (-> 0)",
                               det);
    cpu.Set(kR0, 0);
    return true;
  }

  // O ID: o nome da pasta do titulo, lido como numero decimal. Vem da CORRIDA
  // (o `dir` que a bateria abriu), e nao de uma tabela nossa.
  std::uint32_t id = 0;
  bool numerica = !pasta_.empty() && pasta_.size() <= 9u;
  for (char c : pasta_) {
    if (c < '0' || c > '9') { numerica = false; break; }
    id = id * 10u + static_cast<std::uint32_t>(c - '0');
  }
  // SEM NUMERO, NADA SE INVENTA. Uma pasta que nao seja um numero quer dizer que
  // o id de item nao e conhecido: um id inventado punha o titulo a pedir o
  // `AppModInfo` de um item ALHEIO no slot 21. O `0` e o valor que o SDK preve
  // para "nao encontrado", e e o unico canal que este metodo tem -- o retorno E
  // o id, e nao ha codigo de erro. Por isso a recusa fica registada com o NOME.
  if (!numerica || id == 0) {
    traco_.RegistarFalta(Area::Brew, "IShell::GetClassItemID sem id de item",
                         "pasta='" + pasta_ + "' (o id de item e o numero da pasta do modulo, "
                         "medido em mod/274755/tectoy.mod)");
    cpu.Set(kR0, 0);
    return true;
  }
  traco_.Emitir(Area::Brew, Nivel::Depuracao, "ISHELL_GETCLASSITEMID",
                "cls=" + Hex(cls) + " -> id=" + Hex(id) + " (" + pasta_ + ") lr=" +
                    Hex(cpu.Get(kLR)));
  cpu.Set(kR0, id);
  return true;
}

std::uint32_t Despacho::IdibLivre() const {
  for (std::uint32_t obj = zb2::brew::kObjDibBase + 0x340u;
       obj < zb2::brew::kFimDosDibCompativeis; obj += 0x40u) {
    if (mem_.Ler32(obj) != vtable_bitmap_) return obj;
  }
  return 0;
}

void Despacho::ExporEcraAoGuest() {
  if (!ecra_exposto_) return;
  std::vector<std::uint16_t> quadro(static_cast<std::size_t>(zb2::brew::Tela::kLargura) *
                                    zb2::brew::Tela::kAltura);
  tela_.ExportarPara565(quadro.data());
  // `EscreverBruto`, e nao `EscreverBloco`: e o hospedeiro a repor a SUA copia.
  // Com `EscreverBloco` a faixa ficava suja por nossa causa e o `Absorver`
  // seguinte lia de volta o que acabamos de escrever.
  mem_.EscreverBruto(zb2::brew::kBaseDoEcraNoGuest, quadro.data(), zb2::brew::kBytesDoEcra);
  mem_.LimparSujidade();
}

std::uint32_t Despacho::AbsorverEcraDoGuest() {
  if (!ecra_exposto_) return 0;
  if (!mem_.Sujo()) {
    // O guest nao tocou no ecra. Mesmo assim EXPORTA-SE: o 2D que desenhamos
    // desde a ultima vez tem de ficar visivel a quem le o buffer.
    ExporEcraAoGuest();
    return 0;
  }
  // SO A FAIXA QUE O GUEST SUJOU. Ler e comparar o ecra inteiro nao seria so
  // lento: "diferente da Tela" nao e "escrito pelo guest" -- os pixels que NOS
  // desenhamos desde a ultima exportacao tambem diferem, e absorve-los apagaria
  // o nosso proprio desenho.
  const std::uint32_t inicio_byte = mem_.SujoInicio() & ~1u;         // alinhado ao pixel
  const std::uint32_t fim_byte = (mem_.SujoFim() + 1u) & ~1u;
  const std::size_t primeiro = (inicio_byte - zb2::brew::kBaseDoEcraNoGuest) / 2u;
  const std::size_t quantos = (fim_byte - inicio_byte) / 2u;
  std::vector<std::uint16_t> buffer(quantos);
  mem_.LerBloco(inicio_byte, buffer.data(), static_cast<std::uint32_t>(quantos * 2u));
  const std::uint32_t vindos = tela_.AbsorverDe565(buffer.data(), primeiro, quantos);
  pixels_do_guest_ += vindos;
  // Os dois lados voltam a ser iguais, e a faixa suja recomeca vazia.
  ExporEcraAoGuest();
  return vindos;
}

void Despacho::InstalarAjudantes(const Saidas& saidas, Endereco tabela) {
  // A FAIXA DE SAIDA DO IMEDIA, e a vtable dele: escrita UMA vez, aqui, antes
  // do primeiro `Criar`. O `Media` guarda uma COPIA da faixa, logo ela tem de
  // estar configurada neste momento -- o `Instalar` recusa se nao estiver.
  media_ = std::make_unique<Media>(mem_, traco_, saidas, misturador_, &vfs_);
  const auto instalacao = media_->Instalar();
  if (!instalacao.ok) {
    traco_.Emitir(Area::Audio, Nivel::Erro, "IMEDIA_NAO_INSTALADO", instalacao.motivo);
  }
  // A TABELA UNICA: offset no `AEEHelperFuncs` x endereco de saida da
  // implementacao.
  //
  // E uma so lista de proposito. Havia duas -- as escritas individuais e uma
  // lista de "quem ja tem implementacao" que o laco de preenchimento consultava
  // -- e eu esqueci-me de acrescentar a segunda UMA vez. O sintoma foi identico
  // ao do erro que essa lista existia para evitar: a bateria a dizer "falta
  // aee_GetUpTimeMS" com o `aee_GetUpTimeMS` escrito e a funcionar.
  //
  // **Duas listas que tem de concordar sao zero listas.** Com uma so, e
  // impossivel acrescentar uma implementacao sem que o laco a respeite.
    const LigacaoAjudante kLigados[] = {
      {0x68, 0},  // malloc -- tratado a parte, pelo alocador
      {0x6c, 1},  // free
      {kSlotDbgPrintf, kSlotIdDbgPrintf},
      {kSlotStrlen, kSlotIdStrlen},
      {kSlotMemset, kSlotIdMemset},
      {kSlotStrcpy, kSlotIdStrcpy},
      {kSlotStrcat, kSlotIdStrcat},
      {kSlotStrncpy, kSlotIdStrncpy},
      {kSlotStrstr, kSlotIdStrstr},
      {kSlotSleep, kSlotIdSleep},
      {kSlotStrcmp, kSlotIdStrcmp},
      {kSlotStrchr, kSlotIdStrchr},
      {kSlotMemmove, kSlotIdMemmove},
      {kSlotStrtowstr, kSlotIdStrtowstr},
      {kSlotGetAeeVersion, kSlotIdGetAeeVersion},
      {kSlotAeeGetRand, kSlotIdAeeGetRand},
      // `aee_GetUpTimeMS`: o relogio do sistema, pedido por 11 titulos. Devolve
      // o tempo VIRTUAL, e nao o do sistema -- e o que mantem o determinismo.
      {0x0b0, kSlotIdGetUpTime},   // aee_GetUpTimeMS (derivado da struct, 0x0b0)
      {0x0c0, kSlotIdGetAppInstance},
      {0x020, kSlotIdSprintf},
      {0x13c, kSlotIdVsprintf},
      // `vsnprintf` (0x140) e o PEDIDO MAIS ALTO do corpus: **116 vezes, em 4 titulos**
      // (alice 65, zeeboids 49). E o irmao `vsprintf` (0x13c) JA ESTAVA implementado --
      // **uma vitoria de demanda a meio caminho**, sem engenharia reversa nenhuma.
      // Achado pela auditoria de stubs.
      {brew_ajudantes::kAjudante_vsnprintf, kSlotIdVsnprintf},
      // `realloc` (0x074), 9 vezes em 3 titulos. O `Alocador::Realloc` estava ESCRITO e
      // TESTADO e **nao ligado ao slot** -- o mesmo caso.
      {brew_ajudantes::kAjudante_realloc, kSlotIdRealloc},
  };
  // A GUARDA DOS IDs REPETIDOS. **Escrita depois de um id repetido custar uma
  // frente inteira**: o `dbgprintf` tinha `kBaseDoSlot + 500` = 1500, que e o
  // `kSlotIdStrtowstr`, e por isso TODA a chamada a `dbgprintf` corria o
  // `strtowstr` e escrevia na memoria do titulo (ver o comentario do
  // `kSlotIdDbgPrintf`). A faixa dos ids e um espaco PARTILHADO e nao tinha
  // guarda nenhuma; agora tem, e ela fala no ARRANQUE em vez de o defeito
  // aparecer como um titulo que morre oito milhoes de passos mais tarde, num
  // sitio sem ligacao visivel a causa.
  //
  // A GUARDA FOI PROVADA, e nao so escrita: ligar dois offsets ao mesmo id
  // (`strcat` -> `kSlotIdStrcpy`) fa-la falar no arranque, com os dois offsets
  // no texto. Uma guarda que nunca se viu disparar e uma guarda por provar.
  //
  // Duas condicoes, e as duas ja morderam:
  //   1. dois offsets diferentes com o MESMO id de saida;
  //   2. um id dentro da faixa generica [kBaseDoSlot, kBaseDoSlot+117), que o
  //      laco do fim desta funcao usa para os offsets sem implementacao.
  for (std::size_t a = 0; a < sizeof(kLigados) / sizeof(kLigados[0]); ++a) {
    if (kLigados[a].saida >= kBaseDoSlot && kLigados[a].saida < kBaseDoSlot + 117 &&
        kLigados[a].saida != 0 && kLigados[a].saida != 1) {
      char det[128];
      std::snprintf(det, sizeof(det),
                    "o offset 0x%03x usa o id %u, que esta na faixa generica "
                    "[%u,%u) dos offsets sem implementacao",
                    kLigados[a].off, kLigados[a].saida, kBaseDoSlot, kBaseDoSlot + 117);
      traco_.RegistarFalta(Area::Brew, "ajudantes_id_na_faixa_generica", det);
    }
    for (std::size_t b = a + 1; b < sizeof(kLigados) / sizeof(kLigados[0]); ++b) {
      if (kLigados[a].saida != kLigados[b].saida) continue;
      char det[128];
      std::snprintf(det, sizeof(det), "os offsets 0x%03x e 0x%03x partilham o id %u",
                    kLigados[a].off, kLigados[b].off, kLigados[a].saida);
      traco_.RegistarFalta(Area::Brew, "ajudantes_id_repetido", det);
    }
  }
  for (const auto& lig : kLigados) {
    mem_.Escrever32(tabela + lig.off, saidas.Endereco(lig.saida));
  }

  // A FRENTE io2: os objectos IUnzipAStream e IMemAStream. Construidos AQUI --
  // no mesmo passo de construcao do sistema -- porque `tools/bateria.cpp` e
  // partilhado e esta frente nao o altera, e os objectos genericos (9000+)
  // sao de outra frente. As vtables vivem nos indices 15000/15010 da faixa de
  // saida (livres: a entrada usa 20000+, o GL 30000+, os widgets 60000+).
  //
  // CABLAGEM COM LEITURA DE VOLTA, como a da ferramenta: uma vtable que se
  // perde em silencio ja custou uma ronda nesta arvore.
  {
    struct LigacaoDeStream {
      std::uint32_t slot;
      std::uint32_t saida;
    };
    const LigacaoDeStream kUnzip[] = {
        {0, 3}, {1, 4}, {2, kSlotIdUnzipReadable}, {3, kSlotIdUnzipRead},
        {4, kSlotIdUnzipCancel}, {5, kSlotIdUnzipSetStream},
    };
    const LigacaoDeStream kMem[] = {
        {0, 3}, {1, 4},
        {2, kSlotIdMemStreamReadable}, {3, kSlotIdMemStreamRead},
        {4, kSlotIdMemStreamCancel}, {5, kSlotIdMemStreamSet},
        {6, kSlotIdMemStreamSetEx},
    };
    const auto ligar = [&](std::uint32_t vt, std::uint32_t slot, std::uint32_t saida_id) {
      mem_.Escrever32(saidas.Endereco(vt) + slot * 4, saidas.Endereco(saida_id));
    };
    for (const LigacaoDeStream& l : kUnzip) ligar(kVtUnzip, l.slot, l.saida);
    for (std::uint32_t slot = 6; slot < 64; ++slot) {
      ligar(kVtUnzip, slot, kSlotIdUnzipSlots + (slot - 6));
    }
    for (const LigacaoDeStream& l : kMem) ligar(kVtMemStream, l.slot, l.saida);
    for (std::uint32_t slot = 7; slot < 64; ++slot) {
      ligar(kVtMemStream, slot, kSlotIdMemStreamSlots + (slot - 7));
    }
    mem_.Escrever32(kObjUnzip, saidas.Endereco(kVtUnzip));
    mem_.Escrever32(kObjUnzip + 4, 1);  // contagem da convencao desta arvore
    mem_.Escrever32(kObjMemStream, saidas.Endereco(kVtMemStream));
    mem_.Escrever32(kObjMemStream + 4, 1);
    // A LEITURA DE VOLTA.
    bool ok = true;
    for (const LigacaoDeStream& l : kUnzip) {
      ok = ok && mem_.Ler32(saidas.Endereco(kVtUnzip) + l.slot * 4) ==
                     saidas.Endereco(l.saida);
    }
    for (const LigacaoDeStream& l : kMem) {
      ok = ok && mem_.Ler32(saidas.Endereco(kVtMemStream) + l.slot * 4) ==
                     saidas.Endereco(l.saida);
    }
    ok = ok && mem_.Ler32(kObjUnzip) == saidas.Endereco(kVtUnzip) &&
         mem_.Ler32(kObjMemStream) == saidas.Endereco(kVtMemStream);
    if (!ok) {
      traco_.RegistarFalta(Area::Brew, "cablagem_dos_streams",
                           "a vtable do IUnzipAStream/IMemAStream divergiu da instalacao");
    }
    traco_.Emitir(Area::Brew, Nivel::Informacao, "STREAMS_INSTALADOS",
                  "IUnzipAStream obj=0x" + Hex(kObjUnzip) + " vtable=" + Hex(saidas.Endereco(kVtUnzip)) +
                      " | IMemAStream obj=0x" + Hex(kObjMemStream) +
                      " vtable=" + Hex(saidas.Endereco(kVtMemStream)));
  }

  // O WIDGET, no fim da instalacao dos ajudantes. Fica AQUI -- e nao num sitio
  // que a bateria tenha de chamar -- porque esta frente nao pode obrigar a mudar
  // a ferramenta: `tools/bateria.cpp` e partilhado. O `InstalarWidgets` e
  // idempotente, logo quem o quiser chamar explicitamente tambem pode.
  (void)InstalarWidgets(saidas);
  // O SQL, pela MESMA razao: a ferramenta e partilhada e esta frente nao obriga a
  // muda-la. Ver `InstalarSql`.
  (void)InstalarSql(saidas);
  // OS DOIS CLSIDs DO Z-WHEEL, pela mesma razao das duas linhas acima.
  (void)InstalarZclsid(saidas);

  const auto ja_tem = [&](std::uint32_t off) {
    for (const auto& lig : kLigados) {
      if (lig.off == off) return true;
    }
    return false;
  };
  // O GL entra AQUI, junto da tabela de ajudantes: e o mesmo passo de construcao
  // do sistema, e nao um segundo caminho que alguem tem de lembrar de chamar.
  const std::uint32_t slots_gl = InstalarGl(saidas);
  if (slots_gl == 0) {
    traco_.RegistarFalta(Area::Video, "cablagem_do_GL",
                         "o IGL e/ou o IEGL nao cablaram; os pedidos de GL vao recusar");
  }

  // AS TRES CLASSES DO ARRANQUE (`core/brew/classes.h`): `AEECLSID_AppHistory`,
  // `AEECLSID_VALUEMODEL_1` e `AEECLSID_TEXTCTL`. Ficam AQUI, no mesmo passo de
  // construcao do sistema, e nao num sitio que a bateria tenha de chamar --
  // `tools/bateria.cpp` e partilhado e esta frente nao o altera.
  ConstruirClasses(mem_, saidas, traco_);

  // A TELA DO MOTOR DO IGLES11 (frente tela). O `ConstruirIgles` -- chamado
  // por `ConstruirClasses` acima -- RECONSTROI o motor do IGLES11 por corrida
  // (uma vez por titulo), e o `InstalarGl`, feito ANTES em cima, so liga a
  // Tela ao IGL de 30000. O IGLES11 (40300+, o objecto que os 8 titulos 3D
  // usam) ficava sem ela: o `glClear` desse objecto RECUSAVA a escrita com "o
  // IGL NAO TEM TELA LIGADA" (medido no ridgeracer: IGLES11::Clear 1x,
  // pixels=0) -- o fio que as frentes glbloco e qualcomm deixaram encostado.
  // O acesso e const porque o `EstadoDoIgles11` e a janela de leitura dos
  // testes; definir a tela e o unico remendo desta frente em ficheiro
  // partilhado, e fica AQUI para a ligacao acompanhar a instalacao.
  if (const Igl* igles11 = EstadoDoIgles11()) {
    const_cast<Igl*>(igles11)->DefinirTela(&tela_);
  }

  for (std::uint32_t off = 0; off < 117 * 4; off += 4) {
    if (off == 0x68 || off == 0x6c || ja_tem(off)) continue;
    // UM endereco de saida POR OFFSET, e nao um stub generico para todos.
    //
    // MOTIVO, medido: com um stub so, 44 titulos pediam algo e o registo dizia
    // "slot_de_saida_2" 44 vezes -- um numero sem nome. Com um endereco por
    // offset, a bateria diz QUAL funcao do sistema cada titulo pediu, e a lista
    // do que falta passa a ser ordenada por demanda em vez de por intuicao. E o
    // mesmo metodo que nomeou os slots de GL na arvore antiga.
    mem_.Escrever32(tabela + off, saidas.Endereco(kBaseDoSlot + off / 4));
  }
}

bool Despacho::PrepararCallbackDoTemporizador(ICpu& cpu) {
  if (!timer_.ativo || timer_.pfn == 0) return false;
  // O PAR `(funcao, contexto)` VEM DO PROPRIO `SetTimer`, e nao de um
  // `AEECallback` lido da memoria: o cabecalho passa-os em dois argumentos.
  const std::uint32_t fn = timer_.pfn;
  const std::uint32_t ctx = timer_.puser;
  timer_.ativo = false;
  // A FAIXA DO MODULO VEM DE FORA (`DefinirFaixaDoModulo`), porque o TAMANHO e do
  // titulo que esta carregado. Base zero e o valor medido; sem tamanho definido,
  // nenhum callback e aceite -- que e a resposta certa para "nao sei onde esta o
  // codigo do titulo".
  // O TAMANHO vem do `Sinais`, que e quem o guarda (`DefinirFaixaDoModulo`): dois
  // sitios a guardar a mesma faixa seriam dois sitios a divergir.
  if ((fn & ~1u) < sinais_.BaseDoModulo() ||
      (fn & ~1u) >= sinais_.BaseDoModulo() + sinais_.TamanhoDoModulo()) {
    traco_.RegistarFalta(Area::Guarda, "callback_de_temporizador",
                         "funcao " + Hex(fn) + " fora do modulo");
    return false;
  }
  cpu.Set(kLR, kSentinela);  // o retorno do callback volta para ca
  // O CALLBACK E CHAMADO COMO UM `bx`, e nao com `Set(kPC, fn)`: o modo tem de
  // vir do bit 0 da FUNCAO, e nao do que o titulo deixou no CPSR. O mesmo defeito
  // custou 186 486 543 passos na entrega do `EVT_APP_START` (`tools/bateria.cpp`):
  // com `Set`, o modo da fase anterior mandava, e um callback Thumb corria como
  // ARM (ou o contrario). Aqui, o caminho equivalente -- um `bx r3` do proprio
  // modulo -- ja respeitava o bit 0.
  cpu.Bx(fn);
  cpu.Set(kR0, ctx);
  return true;
}

bool Despacho::EntregarEventoAoApplet(ICpu& cpu, std::uint32_t clsapp, std::uint32_t evt,
                                      std::uint16_t wp, std::uint32_t dwp,
                                      std::uint32_t pp_saida, std::uint32_t* devolveu,
                                      std::uint64_t* passos_gastos) {
  if (devolveu != nullptr) *devolveu = 0;
  if (passos_gastos != nullptr) *passos_gastos = 0;

  // 1. QUEM. `clsApp == 0` e "o applet activo" (`AEEIShell.h:3031`, e a macro
  //    `ISHELL_HandleEvent`, `AEEShell.h:281`); com um so applet e o mesmo
  //    destino. Uma classe que nao e a do titulo e um evento para alguem que
  //    aqui nao existe, e a resposta certa e FALSE -- nao ha para quem
  //    encaminhar. A guarda so aperta quando o CLSID do titulo e CONHECIDO.
  char det[160];
  if (clsapp != 0 && tem_clsid_ && clsapp != clsid_titulo_) {
    std::snprintf(det, sizeof(det), "clsApp=0x%08x nao e o titulo (0x%08x); evt=0x%04x", clsapp,
                  clsid_titulo_, evt);
    traco_.RegistarFalta(Area::Brew, "IShell::SendEvent", det);
    return false;
  }

  // 2. O APPLET. Quando ainda nao esta registado le-se o `ppObj` do
  //    `CreateInstance`, porque o `AEEApplet_New` do guest ja la escreveu o
  //    ponteiro: para o shell, o applet existe quando e CRIADO, e nao quando e
  //    iniciado (zeebulator `ishell.cpp:200-207`, zeebx `signal.rs:232-241`).
  std::uint32_t applet = applet_;
  if (applet == 0 && pp_saida != 0) applet = mem_.Ler32(pp_saida);
  if (applet == 0) {
    std::snprintf(det, sizeof(det), "sem applet (ppObj=0x%08x); evt=0x%04x wp=%u", pp_saida, evt,
                  static_cast<unsigned>(wp));
    traco_.RegistarFalta(Area::Brew, "IShell::SendEvent", det);
    return false;
  }

  // 3. O `HandleEvent` e o SLOT 2 da vtable do applet (AddRef=0, Release=1,
  //    HandleEvent=2 -- a ordem do `AEEAppGen.c`, a mesma que a bateria ja usa
  //    para entregar o `EVT_APP_START`).
  const std::uint32_t vtable = mem_.Ler32(applet);
  const std::uint32_t handle_event = mem_.Ler32(vtable + 2 * 4);
  // O DESTINO TEM DE ESTAR DENTRO DO MODULO DO TITULO -- a mesma guarda que a
  // entrada ja aplica aos callbacks. Sem ela, uma vtable por inicializar punha
  // o PC num endereco de dados e o laco andava a executar zeros.
  const std::uint32_t fim_do_modulo = (faixa_fim_ > faixa_base_) ? faixa_fim_ : 0;
  if (handle_event == 0 || fim_do_modulo == 0 || handle_event < faixa_base_ ||
      handle_event >= fim_do_modulo) {
    std::snprintf(det, sizeof(det), "HandleEvent=0x%08x fora do modulo [0x%08x,0x%08x); evt=0x%04x",
                  handle_event, faixa_base_, fim_do_modulo, evt);
    traco_.RegistarFalta(Area::Brew, "IShell::SendEvent", det);
    return false;
  }

  // 4. O TECTO. `HandleEvent` -> `SendEvent` -> `HandleEvent` e uma cadeia que
  //    so para com um limite declarado.
  if (profundidade_de_evento_ >= kMaxProfundidadeDeEvento) {
    std::snprintf(det, sizeof(det), "aninhamento %d atingiu o tecto %d; evt=0x%04x",
                  profundidade_de_evento_, kMaxProfundidadeDeEvento, evt);
    traco_.RegistarFalta(Area::Brew, "IShell::SendEvent(aninhamento)", det);
    return false;
  }

  // 5. GUARDAR os 16 registadores e o CPSR: isto corre por cima de um guest com
  //    registadores VIVOS (o mesmo cuidado de `core/brew/imedia.cpp:397-403`).
  std::array<std::uint32_t, 16> guardados{};
  for (int r = 0; r < 16; ++r) guardados[static_cast<std::size_t>(r)] = cpu.Get(r);
  const std::uint32_t cpsr_guardado = cpu.Cpsr();

  // 6. CHAMAR com a ABI do `IApplet::HandleEvent(pApplet, evt, wParam, dwParam)`.
  cpu.Set(kR0, applet);
  cpu.Set(kR1, evt);
  cpu.Set(kR2, wp);
  cpu.Set(kR3, dwp);
  cpu.Set(kLR, kSentinela);
  cpu.Set(kPC, handle_event);

  // 7. CORRER PELO PROPRIO `Correr`, e NAO por um laco de `cpu.Passo()`: o
  //    `HandleEvent` de um applet real chama o sistema, e o `Passo` NAO para na
  //    faixa de saida -- quem para e o `Correr` do interpretador
  //    (`core/cpu/arm_interpreter.cpp:1527`). Um laco de `Passo` entraria na
  //    faixa de saida, leria zeros da memoria esparsa e deslizaria ate ao
  //    limite. (E o defeito latente de `core/brew/imedia.cpp:412`.)
  ++profundidade_de_evento_;
  const ResultadoFase r = Correr(cpu, kLimiteDoEvento, pp_saida);
  --profundidade_de_evento_;
  if (passos_gastos != nullptr) *passos_gastos = r.passos;

  const bool voltou = (r.motivo == "retornou");
  const std::uint32_t resposta = voltou ? cpu.Get(kR0) : 0;
  if (!voltou) {
    // P2: o caminho que nao concluiu REGISTA. Um tratador que se perde nao
    // derruba quem mandou o evento -- para ele, ninguem tratou.
    std::snprintf(det, sizeof(det), "HandleEvent=0x%08x evt=0x%04x nao voltou: %s", handle_event,
                  evt, r.motivo.c_str());
    traco_.RegistarFalta(Area::Brew, "IShell::SendEvent(nao_voltou)", det);
  }

  // 8. REPOR. O r15 E o PC, logo isto repoe tambem o PC do laco de fora; quem
  //    chama poe o `kR0` DEPOIS desta reposicao, que e o retorno do metodo.
  for (int r2 = 0; r2 < 16; ++r2) cpu.Set(r2, guardados[static_cast<std::size_t>(r2)]);
  cpu.SetCpsr(cpsr_guardado);

  // O QUE O APPLET RESPONDEU, no traco de depuracao (`ZB2_TRACE=1`). A resposta
  // util NAO e o `boolean`: e o que o applet escreveu no `*dwParam` -- e e isso
  // que o chamador le a seguir (`tectoy.mod:0x6a3a0`). Sem esta linha, uma
  // entrega que corre e responde ZERO e indistinguivel de uma que corre e
  // responde o objecto pedido.
  {
    char det_ok[192];
    std::snprintf(det_ok, sizeof(det_ok),
                  "cls=0x%08x evt=0x%04x wp=%u dwp=0x%08x -> HandleEvent=0x%08x devolveu=%u "
                  "resposta=0x%08x passos=%llu%s",
                  clsapp, evt, static_cast<unsigned>(wp), dwp, handle_event, resposta,
                  dwp != 0 ? mem_.Ler32(dwp) : 0,
                  static_cast<unsigned long long>(r.passos), voltou ? "" : " (NAO VOLTOU)");
    traco_.Emitir(Area::Brew, Nivel::Depuracao, "ISHELL_SENDEVENT", det_ok);
  }
  if (devolveu != nullptr) *devolveu = resposta;
  return voltou;
}

// ---------------------------------------------------------------------------
// A EXTENSAO QUE O TITULO TROUXE
// ---------------------------------------------------------------------------
//
// O `a3d` pede `0x010292c3` ao `IShell::CreateInstance`; a classe nao esta em
// cabecalho nenhum deste SDK e nao existe em particao nenhuma da consola,
// porque ela viaja no proprio pacote do titulo -- e o `imicro3d.mod`, um modulo
// ARM de 90 068 bytes. O mecanismo e o do console:
//
//   1. `AEEMod_Load(shell, helpers, &saida)` NA PRIMEIRA VEZ, e a extensao
//      entrega o `IModule*` dela;
//   2. `IModule::CreateInstance(modulo, shell, clsid, &saida)` -- o SLOT 2 do
//      vtable do `IModule`, o mesmo que o applet usa (`tools/bateria.cpp` faz
//      os dois passos para o titulo: `vtable = Ler32(modulo)` e
//      `ci = Ler32(vtable + 8)`).
//
// O objecto que volta e implementado em ARM PELA EXTENSAO. Daqui em diante o
// jogo fala directamente com ele e nos NAO precisamos de saber que interface e:
// nem o `IMICRO3D` nem o motor da Superscape foram implementados aqui -- eles
// rodam. Fingir um servico completo era o stub silencioso com outra cara (P2);
// isto e o contrario: entregar o modulo a serio que o titulo trouxe.
//
// O DESVIO DE REGRA: isto acontece DENTRO do despacho de uma chamada de API, que
// e onde entrar no guest era proibido -- quem esta a despachar vai ler o `lr`
// depois para saber onde retomar o jogo, e perde-lo manda a execucao para o
// endereco zero. Por isso a chamada e aninhada A SERIO, com o contexto todo
// salvo e devolvido (`ChamarNoGuest`). A pilha nao precisa de cuidado: a
// chamada aninhada empilha abaixo do `sp` corrente, que e espaco que ninguem
// esta a usar -- a mesma garantia que uma interrupcao tem.

bool Despacho::OferecerExtensao(std::uint32_t classe, std::uint32_t base, std::uint32_t tabela,
                                const std::vector<std::uint8_t>& imagem) {
  for (const ExtensaoDoTitulo& e : extensoes_) {
    if (e.classe == classe) {
      // DUAS extensoes para a mesma classe seria uma escolha, e uma escolha sem
      // medida nao se faz (P2): diz-se qual ja la estava.
      traco_.RegistarFalta(Area::Brew, "extensao_repetida",
                           "classe " + Hex(classe) + " ja oferecida em " + Hex(e.base));
      return false;
    }
  }
  const ResultadoDaCarga carga = CarregarMod(mem_, imagem, base, tabela, &traco_);
  if (!carga.ok) {
    traco_.RegistarFalta(Area::Brew, "extensao_nao_carregada",
                         "classe " + Hex(classe) + ": " + carga.motivo);
    return false;
  }
  ExtensaoDoTitulo e;
  e.classe = classe;
  e.base = carga.base;
  e.tamanho = carga.tamanho;
  e.tabela = tabela;
  extensoes_.push_back(e);
  char det[160];
  std::snprintf(det, sizeof(det), "classe=%s base=0x%08x tamanho=%u (mapeada, sem AEEMod_Load)",
                Hex(classe).c_str(), carga.base, carga.tamanho);
  traco_.Emitir(Area::Brew, Nivel::Informacao, "EXTENSAO_OFERECIDA", det);
  return true;
}

std::uint32_t Despacho::ChamarNoGuest(ICpu& cpu, std::uint32_t funcao, const std::uint32_t args[4],
                                      bool* voltou, ResultadoFase* fase) {
  // O `lr` E O QUE NAO SE PODE PERDER, mas guarda-se o contexto todo (r0..r15 e o
  // CPSR): o jogo pode ter a chamada a meio de uma expressao, e o despacho vai
  // ler estes registos a seguir.
  std::uint32_t guardados[16] = {};
  for (int r = 0; r < 16; ++r) guardados[r] = cpu.Get(static_cast<std::uint32_t>(r));
  const std::uint32_t cpsr_guardado = cpu.Cpsr();

  cpu.Set(kR0, args[0]);
  cpu.Set(kR1, args[1]);
  cpu.Set(kR2, args[2]);
  cpu.Set(kR3, args[3]);
  cpu.Set(kLR, kSentinela);  // o retorno da extensao volta para ca
  cpu.Set(kPC, funcao);

  // CORRER PELO PROPRIO `Correr`, e NAO por um laco de `cpu.Passo()`: a extensao
  // chama o sistema (ja medido: o `AEEMod_Load` do `imicro3d.mod` comeca por
  // `ldr r0,[r0,#-4]` e salta para o slot 0x68, o `malloc`), e quem para na faixa
  // de saida e o `Correr`. O tecto de profundidade e o mesmo dos eventos: uma
  // cadeia so para com um tecto.
  if (profundidade_de_evento_ >= kMaxProfundidadeDeEvento) {
    traco_.RegistarFalta(Area::Brew, "extensao_profunda_demais",
                         "funcao " + Hex(funcao) + " com profundidade " +
                             std::to_string(profundidade_de_evento_));
    for (int r = 0; r < 16; ++r) cpu.Set(static_cast<std::uint32_t>(r), guardados[r]);
    cpu.SetCpsr(cpsr_guardado);
    if (voltou != nullptr) *voltou = false;
    return 0;
  }
  ++profundidade_de_evento_;
  const ResultadoFase r = Correr(cpu, kLimiteDoEvento, 0);
  --profundidade_de_evento_;

  const std::uint32_t resposta = cpu.Get(kR0);
  for (int r2 = 0; r2 < 16; ++r2) cpu.Set(static_cast<std::uint32_t>(r2), guardados[r2]);
  cpu.SetCpsr(cpsr_guardado);
  if (voltou != nullptr) *voltou = (r.motivo == "retornou");
  if (fase != nullptr) *fase = r;
  return resposta;
}

std::uint32_t Despacho::CriarInstanciaDaExtensao(ICpu& cpu, std::uint32_t shell,
                                                 std::uint32_t classe) {
  for (ExtensaoDoTitulo& e : extensoes_) {
    if (e.classe != classe) continue;
    // O BLOCO DE APOIO DAS DUAS SAIDAS. Fica DENTRO da faixa da extensao (que e
    // nossa) e nao na pilha do jogo: e um endereco fixo, reproduzivel, e o jogo
    // nao escreve la. A faixa da extensao e reservada por quem a oferece.
    const std::uint32_t p_modulo = e.base + e.tamanho + 64u;
    const std::uint32_t p_objeto = e.base + e.tamanho + 128u;

    if (!e.carregou) {
      // 1. `AEEMod_Load(pIShell, pHelpers, &pIModule)` -- UMA vez, e mesmo que
      //    falhe: uma extensao partida nao se recarrega a cada pedido.
      e.carregou = true;
      mem_.Escrever32(p_modulo, 0);
      const std::uint32_t args[4] = {shell, e.tabela, p_modulo, 0};
      bool voltou = false;
      ResultadoFase fase;
      ChamarNoGuest(cpu, e.base, args, &voltou, &fase);
      e.modulo = mem_.Ler32(p_modulo);
      if (!voltou || e.modulo == 0) {
        traco_.RegistarFalta(Area::Brew, "AEEMod_Load",
                             "classe " + Hex(classe) + " base " + Hex(e.base) +
                                 (voltou ? ": devolveu modulo ZERO"
                                         : ": NAO voltou a sentinela -- " + fase.motivo +
                                               " com " + std::to_string(fase.passos) + " passos"));
        return 0;
      }
      char det[160];
      std::snprintf(det, sizeof(det), "classe=%s base=0x%08x pIModule=0x%08x", Hex(classe).c_str(),
                    e.base, e.modulo);
      traco_.Emitir(Area::Brew, Nivel::Informacao, "EXTENSAO_CARREGADA", det);
    }

    // 2. `IModule::CreateInstance(po, pIShell, ClsId, ppApplet)` -- o SLOT 2.
    //    O `IModule*` aponta para o vtable, e o slot 2 esta em +8 (medido em
    //    `tools/bateria.cpp:942` para o modulo do titulo: `Ler32(vtable + 8)`).
    const std::uint32_t vtable = mem_.Ler32(e.modulo);
    const std::uint32_t criar = mem_.Ler32(vtable + 8u);
    if (criar == 0) {
      traco_.RegistarFalta(Area::Brew, "IModule::CreateInstance",
                           "classe " + Hex(classe) + ": o slot 2 do vtable em " + Hex(vtable) +
                               " esta a ZERO");
      return 0;
    }
    mem_.Escrever32(p_objeto, 0);
    const std::uint32_t args[4] = {e.modulo, shell, classe, p_objeto};
    bool voltou = false;
    ChamarNoGuest(cpu, criar, args, &voltou);
    const std::uint32_t objeto = mem_.Ler32(p_objeto);
    char det[192];
    std::snprintf(det, sizeof(det),
                  "classe=%s IModule=0x%08x slot2=0x%08x objeto=0x%08x%s", Hex(classe).c_str(),
                  e.modulo, criar, objeto, voltou ? "" : " (NAO VOLTOU A SENTINELA)");
    traco_.Emitir(Area::Brew, Nivel::Informacao, "EXTENSAO_CRIAR_INSTANCIA", det);
    if (objeto == 0) {
      traco_.RegistarFalta(Area::Brew, "IShell::CreateInstance IMicro3D (0x010292c3)",
                           "a extensao respondeu com objecto ZERO (modulo " + Hex(e.modulo) + ")");
    }
    return objeto;
  }
  return 0;
}

ResultadoFase Despacho::Correr(ICpu& cpu, std::uint64_t limite, std::uint32_t pp_saida) {
  ResultadoFase resultado;
  // DOIS CONTADORES (item 1 do PLAN, docs/rewrite/PLAN.md):
  //
  // `recusas_seguidas` conta RECUSAS CONSECUTIVAS: cresce a cada saida
  // RECUSADA e volta a zero quando uma saida e SERVIDA. E a medida de "um
  // jogo que insiste num metodo que recusa, em vez de andar em ciclo".
  //
  // `saidas` e o TOTAL de saidas da fase, e so marca o ciclo de saidas sem fim
  // (o limite de 20000). NAO julga recusa nenhuma -- qualquer saida ja o fazia
  // crescer, e foi assim que o antigo `saidas > 200` matou fases LEGITIMAS.
  // MEDIDO na corrida de referencia (`corrida_hid_q.json`): 13 fases mortas em
  // `parou_em_slot_nao_implementado` com `recusadas = 0` e so centenas de
  // passos -- 200 chamadas legitimas + a proxima instrucao, e a fase morria.
  std::uint32_t recusas_seguidas = 0;
  std::uint32_t saidas = 0;
  bool continuar_no_laco = false;
  // A THREAD COOPERATIVA (frente thrd), corrida POR ESTA INVOCACAO:
  //
  //   `tinha_pendente`  -- uma thread JA estava pendente antes da chamada de
  //                        API que esta a ser servida? (a fronteira posterior
  //                        ao Start e a segunda regra de retomada; a primeira,
  //                        para a thread recem-criada, usa o pfn capturado)
  //   `thread_a_correr` -- esta invocacao retomou uma thread; quando o pc cair
  //                        na sentinela, e ela que voltou, e nao o guest
  //   `hospedeiro`      -- o contexto do guest (r0..r15) salvo no retomar, para
  //                        repor quando a thread ceder ou terminar
  bool tinha_pendente = TemThreadPendente();
  bool thread_a_correr = false;
  // O pfn do `Start` acabado de despachar nesta mesma iteracao (0 = nenhum).
  // Capturado no ramo do despacho, lido e reposto no epilogo.
  std::uint32_t pfn_do_ultimo_start = 0;
  std::uint32_t hospedeiro[16] = {};
  // A RETOMADA DO TRECHO CORTADO, ANTES de qualquer outra coisa: entregar um timer
  // por cima de um quadro pela metade seria entrega-lo fora de hora (a regra do
  // `1026fb7`). O PC leva o bit 0 quando o guest estava em Thumb -- e assim que o
  // ARM diz "continue em Thumb" -- e os registradores voltam ao que eram.
  if (trecho_.valido) {
    for (int k = 0; k < 16; ++k) cpu.Set(k, trecho_.regs[k]);
    cpu.Set(kPC, trecho_.pc);
    // O MODO vai junto: o bit `T` do CPSR e o que diz ao interpretador que o PC
    // retomado e Thumb. Sem ele um guest Thumb volta decodificado como ARM -- o
    // modo em que o proprio `zeebx` mediu o salto para o endereco zero.
    std::uint32_t cpsr = cpu.Cpsr();
    if (trecho_.thumb) cpsr |= Cpsr::kT; else cpsr &= ~Cpsr::kT;
    cpu.SetCpsr(cpsr);
    trecho_.valido = false;
    traco_.Emitir(Area::Brew, Nivel::Depuracao, "TRECHO_RETOMADO",
                  "pc=0x" + Hex(trecho_.pc) + (trecho_.thumb ? " (Thumb)" : " (ARM)"));
  }
  while (resultado.passos < limite) {
    // O ORCAMENTO DE TEMPO, verificado a cada 65536 passos.
    //
    // A cada passo seria caro; a cada 65536 o erro maximo e de um bloco, e o
    // custo e nulo. O motivo fica REGISTADO: um titulo que bate no orcamento nao
    // e um titulo que falhou -- e um titulo que ainda estava a andar, e isso
    // muda o que se conclui dele.
    // O ORCAMENTO DE FASE PASSA A SER EM PASSOS, e nao em relogio do hospedeiro.
    //
    // Era `std::chrono::steady_clock`, e isso e uma violacao do P4
    // (determinismo por construcao): o emulador passava a medir a CARGA DA
    // MAQUINA. Ficou LATENTE enquanto o limite de passos dominou -- os 36 titulos
    // com `orcamento_esgotado` param exactamente em 4000000 passos, nenhum pelo
    // relogio. Mas no dia em que um titulo ficasse mais lento por passo, a
    // bateria acusaria regressoes de JOGABILIDADE que eram regressoes de
    // CARGA -- e o `tools/comparar` (etapa 9) acreditaria nelas.
    //
    // Achado pelo sub-agente `regressoes`, na revisao da etapa 9. **O agente que
    // constroi o juiz foi quem viu que o juiz ia julgar a coisa errada.**
    const std::uint32_t pc = cpu.Get(kPC);
    if (pc == kSentinela) {
      // A sentinela tem dois significados: o retorno da chamada de entrada, ou
      // o retorno de um callback de temporizador. Distinguir os dois e o que
      // permite o laco de eventos -- sem isto, o primeiro callback do jogo
      // seria lido como "o modulo retornou".
      //
      // TEM UM TERCEIRO, desta frente (thrd): a THREAD que esta invocacao
      // retomou voltou a sentinela -- por `Suspend` (cede a vez), por `Exit`
      // (encerra) ou porque a funcao de entrada voltou sem passar por nenhum
      // (e o rv e o r0; quem o decide e o `ConcluirRetomadaDeThread`). Fecha a
      // corrida e REPOE O GUEST onde ficou, a meio da fronteira entre chamadas
      // de API.
      if (thread_a_correr) {
        ConcluirRetomadaDeThread(cpu, traco_);
        thread_a_correr = false;
        for (std::uint32_t k = 0; k <= kPC; ++k) cpu.Set(k, hospedeiro[k]);
        continue;
      }
      if (continuar_no_laco) { continuar_no_laco = false; continue; }
      resultado.motivo = "retornou";
      DespejarPcHot("retornou");
      return resultado;
    }
    std::uint32_t idx = 0;
    if (cpu.GetSaidas().Contem(pc, &idx)) {
      const std::uint32_t lr = cpu.Get(kLR);
      // O RETORNO DE UMA SAIDA VAI SEM O BIT 0 DO `lr`.
      //
      // MEDIDO (frente g5, `reksio`): um `bl` do Thumb poe `lr = proxima | 1`. O
      // regresso escrevia esse valor CRU no PC, e o PC ficava IMPAR -- a busca
      // seguinte lia a meia-palavra a partir do byte 1 e o modulo ia para fora em
      // duas instrucoes. O registo do titulo mostrou-o no proprio anel:
      // `00000232:47204718` (a veneira `bx r3`), `00036189:011c3268` -- o PC
      // impar 0x36189, com a palavra DESLOCADA (`0x1c3268e0` e a palavra certa em
      // 0x36188, `ldr r0,[r4,#0xc]`).
      //
      // E O BIT 0 DIZ O MODO DO CHAMADOR: o regresso e um `bx lr`, e nao um
      // `Set(kPC, lr)`. MEDIDO um degrau acima, e e o mesmo defeito que ja
      // custou 186 486 543 passos no `EVT_APP_START` (ver o comentario do
      // callback, `EntregarEventoAoApplet`): o guest chama o ajudante com um
      // `bx r3` para um endereco PAR da faixa de saida, o que poe a CPU em ARM;
      // a mascara sozinha devolvia o PC ao Thumb com o CPSR ainda em ARM, e o
      // modulo passava a ler codigo Thumb como ARM -- foi o que o `reksio`
      // mostrou: 91 recusas a partir de `pc=0x36188 instr=0x1c3268e0`
      // (`ldr r0,[r4,#0xc]` lido a deslocado) e a deriva para fora do modulo.
      const std::uint32_t kernel_retorno_ = lr;
      const std::uint32_t r0 = cpu.Get(kR0);
      // Esta saida RECUSOU? Os ramos de recusa marcam-no; o epilogo do bloco
      // usa-o para decidir se a sequencia de recusas recomeca.
      bool recusou_agora = false;
      // FRENTE thrd: captura o pfn ANTES de o `AtenderClasse` servir o Start
      // (r2 ainda e o terceiro argumento). E o que permite a fronteira decidir
      // se a thread recem-criada corre ali mesmo, sem desenhar o interior da
      // classe neste ficheiro. Um pfn fora da faixa do modulo nao e uma thread
      // do titulo (os testes do contrato passam `Start` com pfn de LIXO, de
      // proposito) -- essa fica pendente como dantes.
      if (idx == VtClasse(static_cast<std::uint32_t>(Classe::kThread)) +
                  brew_slots::kThread_Start) {
        pfn_do_ultimo_start = cpu.Get(kR2);
      }
      // O PARK DA ESPERA (frente park): classifica a saida ANTES de a servir --
      // ler o relogio cresce a contagem, trabalho zera. Ver o zeebx
      // `note_spin` (ramo fix-fp-threading): e aqui, e nao no fim, que a
      // contagem se decide; o fim so usa o numero.
      NotarEspera(cpu, idx);
      if (idx == 0) {
        // O `malloc` DO BREW TEM DUAS REGRAS, e as duas sao do cabecalho:
        //
        // 1. O BIT ALTO DO TAMANHO E UMA BANDEIRA, nao um tamanho. O
        //    `AEEStdLib.h:547` define `#define ALLOC_NO_ZMEM (0x80000000L)` e o
        //    `MALLOCREC_EX` (`:556`) usa-o como MASCARA (`(n) & ~ALLOC_NO_ZMEM`).
        //    Um pedido com o bit posto pede o tamanho SEM ele.
        //    MEDIDO no `quake2brew`: o codigo do titulo faz `orr r0, r5,
        //    #0x80000000` (`0x000b0650`) antes de chamar o `malloc` do helper, e
        //    o NOSSO servidor lia aquilo como 2 GiB -- **339 "MALLOC ERROR" do
        //    proprio Quake** (`qcommon\cmd.cpp:733`, `cvar.cpp:199-201`) e as
        //    `cvars` todas a NULL (e dai os 99 `atoi(NULL)`). Com o heap VAZIO
        //    (`alocado=0,0 MiB`): nao era falta de memoria, era um tamanho mal
        //    lido.
        // 2. `malloc` ZERA POR OMISSAO (e o que o `NO_ZMEM` desliga). O nosso
        //    `Alocador` devolve memoria de bloco novo -- que num heap folha ja e
        //    zero -- mas um bloco REUTILIZADO traria os bytes do dono anterior.
        //    A regra serve os dois casos.
        const std::uint32_t pedido = r0;
        const std::uint32_t tamanho = pedido & ~kAllocNoZmem;
        const std::uint32_t bloco = al_.Malloc(tamanho);
        if (bloco != 0 && (pedido & kAllocNoZmem) == 0) {
          for (std::uint32_t k = 0; k < tamanho; ++k) mem_.Escrever8(bloco + k, 0);
        }
        cpu.Set(kR0, bloco);
      } else if (idx == 1) {
        al_.Free(r0);
        cpu.Set(kR0, kAeeSuccess);
      } else if (idx == 3) {
        // IShell::AddRef -- devolve a contagem de referencias, que e o que a
        // interface do SDK promete.
        //
        // O `+4` DE UM IDIB NAO E A CONTAGEM: e o `pPaletteMap` (`AEEIDIB.h:44`),
        // um ponteiro publico que o `IDIB_FlushPalette` desreferencia. Para os
        // objectos da faixa dos bitmaps a contagem vive do lado de ca.
        if (EUmObjectoDeBitmap(r0)) {
          cpu.Set(kR0, ++refs_do_dib_[r0]);
        } else {
          const std::uint32_t n = mem_.Ler32(r0 + 4) + 1;
          mem_.Escrever32(r0 + 4, n);
          cpu.Set(kR0, n);
        }
      } else if (idx == 4) {
        // IShell::Release
        if (EUmObjectoDeBitmap(r0)) {
          std::uint32_t& n = refs_do_dib_[r0];
          if (n > 0) --n;
          cpu.Set(kR0, n);
        } else {
          const std::uint32_t n = mem_.Ler32(r0 + 4);
          if (n > 0) mem_.Escrever32(r0 + 4, n - 1);
          cpu.Set(kR0, n > 0 ? n - 1 : 0);
        }
      } else if (AtenderEntrada(cpu, idx)) {
        // A ENTRADA (etapa 8): IHID, IHIDDevice e os sinais do BREW.
        //
        // ESTE RAMO VEM ANTES DOS OUTROS, e nao por gosto: os indices desta faixa
        // sao 20000+, e o ramo `idx >= kBaseDoShell` (2000) mais abaixo apanhava-os
        // e dava-lhes o NOME de um metodo do IShell. Foi por um nome errado num
        // ramo generico que o `SetTimer` ja se perdeu uma vez nesta arvore.
      } else if (idx == VtClasse(static_cast<std::uint32_t>(Classe::kAppHistory)) +
                               brew_slots::kAppHistory_GetClass) {
        // `int GetClass(po, AEECLSID *pcls)` (AEEIAppHistory.h): devolve o CLSID
        // da entrada corrente -- que e o titulo, lista de 1 (premissa do Top).
        // Honesto agora que o motor guarda o CLSID (SituarTitulo): sem CLSID,
        // EFAILED; pcls nulo, EBADPARM.
        const std::uint32_t pcls = cpu.Get(kR1);
        if (pcls == 0) {
          cpu.Set(kR0, kAeeBadParm);
        } else if (!tem_clsid_) {
          cpu.Set(kR0, kAeeFailed);
        } else {
          mem_.Escrever32(pcls, clsid_titulo_);
          cpu.Set(kR0, kAeeSuccess);
        }
        traco_.Emitir(Area::Brew, Nivel::Depuracao, "APPHISTORY_GETCLASS",
                      tem_clsid_ ? "clsid do titulo" : "sem clsid");
      } else if (idx >= VtClasse(static_cast<std::uint32_t>(Classe::kQEGL)) &&
                 idx < VtClasse(static_cast<std::uint32_t>(Classe::kQEGL)) + kQeglSlots &&
                 idx != VtClasse(static_cast<std::uint32_t>(Classe::kQEGL)) + 2) {
        // O QEGL, ANTES DO AtenderClasse: a faixa e 40000+ e o ramo das classes
        // apanhava estes slots e devolvia `QEGL::?` sem comportamento (nona
        // ocorrencia do erro de ordem). O slot 2 (QI) fica no AtenderClasse.
        const std::uint32_t qegl_base = VtClasse(static_cast<std::uint32_t>(Classe::kQEGL));
        const std::uint32_t q_slot = idx - qegl_base;
        const std::uint32_t slot_iegl = QeglParaIegl(q_slot);
        // A MOLDURA DE CHAMADA DO QEGL TEM MAIS DOIS ARGUMENTOS QUE A DO IEGL, e
        // isso estava por tratar nos dois lados:
        //
        //   QEGL:  int metodo(IQEGL *pMe, <os argumentos do EGL>, <tipo> *pSaida)
        //   IEGL:  <tipo> metodo(<os argumentos do EGL>)   -- como este modulo o serve
        //
        // 1. O `pMe` DESLOCA TODOS OS ARGUMENTOS UM LUGAR. Sem o deslocamento, o
        //    `eglInitialize` do `karnovr` chegava aqui com `dpy = pMe` (0x8F005000)
        //    -- que o `display_ok` ACEITA -- e com o `dpy` verdadeiro
        //    (0x800B1000) no lugar do `major`. O modulo escrevia entao `1` em
        //    0x800B1000, que e o PROPRIO OBJECTO IEGL: a palavra escrita por cima
        //    e o ponteiro da vtable. Uma chamada bem sucedida destruia a interface.
        //
        // 2. O VALOR DE RETORNO SAI PELO PONTEIRO FINAL, e nao pelo r0. MEDIDO em
        //    tres pontos do `karnovr.mod` (o `.mod` carrega na base 0, logo o
        //    offset do ficheiro e o endereco):
        //      0x102c0 `ldr pc,[r4,#0x14]` (slot 5, eglInitialize) e logo a seguir
        //              0x102c4 `ldr r0,[sp,#4]`  -- devolve *pSaida, nao o r0;
        //      0x104a4 `ldr pc,[r4,#0x24]` (slot 9, eglChooseConfig) e
        //              0x104a8 `ldr r0,[sp,#0xc]`;
        //      (e o `abd` 0x14688/0x146c4 no GetDisplay, que ja estava tratado.)
        //    O `karnovr` compara `cmp r0,#1` em 0x102b8 (dentro de `InitGLSurface`,
        //    0xfba8) e, com lixo da pilha no lugar do EGL_TRUE, desviava para o
        //    ecra de erro "InitGLSurface failed". **Era este o tecto dos 10
        //    titulos que escreviam pixels.**
        //
        // O INDICE DO PONTEIRO DE SAIDA NAO E ADIVINHADO: e o numero de argumentos
        // que o proprio `Egl::Executar` declara ter consumido (`ChamadaEgl::n_args`),
        // que e o numero de parametros do EGL. Confere com os tres pontos medidos:
        // GetDisplay 1 -> r2; Initialize 3 -> sp[0]; ChooseConfig 5 -> sp[8].
        const bool qegl_ibase = (q_slot == 0 || q_slot == 1);  // AddRef/Release levam `pMe`
        ArgumentosGl avq;
        if (qegl_ibase) {
          for (int k2 = 0; k2 < 4; ++k2) avq.reg[k2] = cpu.Get(kR0 + k2);
          avq.sp = cpu.Get(kSP);
        } else {
          avq.reg[0] = cpu.Get(kR1);
          avq.reg[1] = cpu.Get(kR2);
          avq.reg[2] = cpu.Get(kR3);
          avq.reg[3] = mem_.Ler32(cpu.Get(kSP));
          avq.sp = cpu.Get(kSP) + 4;
        }
        avq.lr = cpu.Get(kLR);
        std::uint32_t retornoq = 0;
        const ResultadoGl rq = egl_.Executar(slot_iegl, avq, &retornoq);
        cpu.Set(kR0, retornoq);
        if (!qegl_ibase && rq != ResultadoGl::NaoImplementado && !egl_.Ultimas().empty()) {
          const std::size_t n = egl_.Ultimas().back().n_args;
          const std::uint32_t out = (n < 4) ? avq.reg[n] : mem_.Ler32(avq.sp + (n - 4) * 4);
          if (out != 0) mem_.Escrever32(out, retornoq);
        }
      } else if (AtenderClasse(cpu, idx, traco_)) {
        // AS CLASSES CONHECIDAS (`core/brew/classes.h`). ESTE RAMO VEM ANTES DO
        // `idx >= kBaseDoShell`, e nao e gosto: os indices desta faixa sao 40000+
        // e o ramo generico (2000) apanhava-os e dava-lhes o NOME de um metodo do
        // IShell. **E o erro de ORDEM, que ja apareceu oito vezes nesta arvore.**
        //
        // Os metodos nao implementados RECUSAM COM O NOME DO METODO
        // (`ITextCtl::SetInputMode`) e o registo fica no Traco. Aqui nao se
        // distingue o atendido do recusado (o `AtenderClasse` devolve true nas
        // duas), logo este ramo conta como SERVIDO: a sequencia de recusas
        // recomeca, e um ciclo preso num metodo de classe e travado pelo
        // `laco_de_saidas` (20000) como qualquer outro ciclo de saidas.
      } else if (idx == kBaseDoShell + 2) {
        // IShell::CreateInstance(po, ClsId, ppobj) -- IShell slot 2.
        //
        // NAO e `QueryInterface`: o `INHERIT_IBase` deste SDK tem DOIS membros
        // (`AddRef`, `Release`) e nao ha `QueryInterface` nele; o slot 2 do IShell
        // e o `CreateInstance` (`AEEIShell.h`). A semantica do codigo ja era essa
        // (r1 = ClsId, r2 = &ppobj) -- **era o NOME que estava errado**, e este
        // comentario andou a mentir durante varias rondas. Apontado por dois
        // sub-agentes independentes (`imedia` e `hid-entrada`).
        //
        // Os dois IIDs que o corpus pede
        // sao conhecidos por medicao; o que nao for conhecido devolve
        // ECLASSNOTSUPPORT com o ponteiro a zero -- recusar, nao mentir.
        const std::uint32_t iid = cpu.Get(kR1);
        const std::uint32_t ppo = cpu.Get(kR2);
        // O `IShell` DA CHAMADA, APANHADO AQUI. Os ramos abaixo escrevem no
        // `r0` (o do `IMedia` chega a fazer `continue`), e a extensao precisa
        // dele para o `AEEMod_Load` e para o `IModule::CreateInstance` -- e o
        // mesmo objecto que o applet recebeu no arranque. Ler o `r0` em baixo
        // seria ler o que o ramo anterior lhe tivesse deixado.
        const std::uint32_t shell = cpu.Get(kR0);
        std::uint32_t devolver = 0;
        if (media_ && zb2::brew::ClasseDeMidia(iid)) {
          // A FAMILIA AEECLSID_MULTIMEDIA (0x01005500): o objecto de midia, a
          // tabela do IMedia e o ciclo de vida vivem em core/brew/imedia.
          if (ppo != 0) mem_.Escrever32(ppo, 0);
          cpu.Set(kR0, static_cast<std::uint32_t>(media_->Criar(iid, ppo)));
          cpu.Bx(kernel_retorno_);
          continue;
        }
        // DISPLAY1 (0x010127d4, "display 1") e o segundo display; num aparelho
        // de um so ecra e o mesmo objeto do DISPLAY (precedente: zeebulator
        // brew_platform.cpp:54; SDK AEEDisp.h:49). 3x no corpus.
        constexpr std::uint32_t kIidDisplay1 = 0x010127d4u;
        if (iid == kIidDisplay || iid == kIidDisplay1) devolver = zb2::brew::kObjDisplay;
        else if (iid == kIidFileMgr) devolver = zb2::brew::kObjFileMgr;
        // O GL E O EGL (AEEGL.h). O `ddragonz` cria um objecto com o AEECLSID_GL
        // (0x01014bc3) e passa o resultado como `gpIGL` (0x11d61c-0x11d634).
        // O AEECLSID_EGL (0x01014bc4) NAO aparece em nenhum dos 4 titulos com
        // wrapper -- esta linha vem do cabecalho e nao de uma medicao do corpus.
        else if (iid == zb2::brew::kClsidIgl) devolver = igl_.Objeto();
        else if (iid == zb2::brew::kClsidIegl) devolver = egl_.Objeto();
        // A ENTRADA. O `AEECLSID_HID` deixou de ser um objecto GENERICO: existe um
        // IHID a serio. E a fabrica de sinais tambem (0x01041207, pedida por 37
        // dos 62 titulos -- medido), porque sem ela o jogo nao tem como pedir o
        // par (funcao, contexto) que o `RegisterForPositionChange` recebe.
        else if (entrada_pronta_ && iid == kIidHid) devolver = ihid_.EnderecoDoIhid();
        else if (entrada_pronta_ && iid == kClsidSignalCBFactory) {
          devolver = sinais_.EnderecoDaFabrica();
        }
        // A FRENTE io2: IUnzipAStream e IMemAStream sao objectos A SERIO (vtable
        // e comportamento proprios), e nao genericos. O `0x01001014` ESTAVA em
        // `kGenericos` com o nome "IFile" -- e o objecto generico recusava o
        // `Read`/`SetStream` que o allstarcards pede em laco (310 KB).
        else if (iid == kClsidUnzipStream) devolver = kObjUnzip;
        else if (iid == kClsidMemAStream) devolver = kObjMemStream;
        // OS DOIS CLSIDs DO Z-WHEEL (frente zclsid): cada um recebe o objecto da
        // SUA interface. Este ramo vem ANTES do dos genericos, e nao e gosto: o
        // `0x01000000` e tambem o `AEECLSID_PRIV` (a base de toda a familia), e um
        // titulo que o peca espera a classe -- deixar o ramo generico apanha-lo
        // daria uma interface sem comportamento nenhum em vez de recusa.
        else if (zclsid_pronto_ && iid == kIidConfig) {
          devolver = kObjConfig;
          // O `lr` no traco e a MEDICAO do sitio de chamada, e sem ele nao ha como
          // ir ao desmonte (foi assim que se leu o `Tectoy_SetSystemLanguage`).
          traco_.Emitir(Area::Brew, Nivel::Depuracao, "ZCLSID_CRIADO",
                        "AEECLSID_CONFIG -> IConfig obj=0x" + Hex(kObjConfig) + " ppo=0x" +
                            Hex(ppo) + " lr=0x" + Hex(cpu.Get(kLR)));
        } else if (zclsid_pronto_ && iid == kIidDownload) {
          devolver = kObjDownload;
          traco_.Emitir(Area::Brew, Nivel::Depuracao, "ZCLSID_CRIADO",
                        "AEECLSID_DOWNLOAD -> IDownload obj=0x" + Hex(kObjDownload) + " ppo=0x" +
                            Hex(ppo) + " lr=0x" + Hex(cpu.Get(kLR)));
        }
        // Os que tem objecto generico: o jogo fica com uma interface cujos
        // metodos recusam, e a bateria aprende quais sao.
        else {
          for (std::uint32_t k = 0; k < kNGenericos; ++k) {
            if (iid == kGenericos[k].iid) devolver = zb2::brew::ObjGenerico(k);
          }
          // E, por fim, AS TRES CLASSES DO ARRANQUE (`core/brew/classes.h`). O
          // objecto existe e os metodos que nao estejam implementados RECUSAM COM
          // O NOME -- o `CreateInstance` de um `AEECLSID_TEXTCTL` no aparelho a
          // serio TAMBEM devolve um objecto. O que nao se faz e devolver sucesso
          // com um objecto que se diz completo (P2).
          if (devolver == 0) devolver = zb2::brew::ObjetoDoClsid(iid);
          // AS FONTES STANDARD: um objecto por CLSID com a metrica do seu
          // tamanho nominal (frente fontes). Vem depois das classes e antes
          // da extensao: e nossa, e nao e recusa.
          if (devolver == 0) devolver = zb2::brew::ObjetoDaFonte(iid);
        }
        // E, POR FIM, A CLASSE QUE VIAJA NO PACOTE DO PROPRIO TITULO. Vem DEPOIS
        // de tudo o que e nosso e ANTES da recusa: a extensao e a ultima a
        // tentar, mas nao e uma recusa -- quando ela responde, a criacao foi
        // SERVIDA. Sem isto o `a3d` fica preso em
        // `IShell::CreateInstance IMicro3D (0x010292c3)` e nao chega a desenhar
        // um pixel.
        if (devolver == 0) devolver = CriarInstanciaDaExtensao(cpu, shell, iid);
        if (ppo != 0) mem_.Escrever32(ppo, devolver);
        cpu.Set(kR0, devolver != 0 ? kAeeSuccess : kAeeClassNotSupported);
        if (devolver == 0) {
          char det[128];
          // O NOME, QUANDO O SDK O DECLARA (`tools/clsids.inc`, gerado dos
          // `*.bid`/`*.h`). Uma lista de demanda que diz o numero obriga a ir ao
          // cabecalho contar em cada ronda; uma que diz o nome e uma medida.
          std::snprintf(det, sizeof(det), "iid=0x%08x ppo=0x%08x %s", iid, ppo,
                        zb2::brew::DescreverClsid(iid).c_str());
          // O NOME ENTRA NA CHAVE DA FALTA, e nao so no detalhe. Medido na
          // corrida do corte: 4 titulos recebem "CLSID desconhecido" (chessbots
          // 0x0100100f, allstarcards 0x0100100c, pbc 0x01001039, a3d
          // 0x010292c3) -- e a lista de demanda dizia UM numero sem nome.
          //
          // 0x010292c3 NAO esta em cabecalho nenhum deste SDK (`grep -rn` na
          // arvore extraida nao o encontra): e o servico privado de 3D da HI
          // Corporation, fornecido pelo `imicro3d.mod` -- o unico `.mif` do
          // corpus sem applet (pasta 12875, clsid 0x010292c3), e o `a3d` (o
          // titulo que o pede) emite "IMICRO3D failed creation" quando a
          // criacao falha. Zeemu nomeia-o assim: "Private HI Corporation 3D
          // service from imicro3d.mod" (`BrewShell.cpp:2737`,
          // `BrewMicro3D.cpp:539`).
          //
          // O que se recusa continua a recusar (ECLASSNOTSUPPORT, ponteiro a
          // zero) -- o que muda e o NOME com que a recusa fica contada. Nao se
          // serve um IMicro3D, um LICENSE ou um MD5 sem implementacao: fingir
          // um servico completo e o stub silencioso com outra cara (P2).
          std::string chave_da_falta;
          if (iid == 0x010292c3u) {
            chave_da_falta = "IShell::CreateInstance IMicro3D (0x010292c3)";
          } else if (const char* nome_do_clsid = zb2::brew::NomeDoClsid(iid)) {
            chave_da_falta = std::string("IShell::CreateInstance ") + nome_do_clsid;
          } else {
            // Desconhecido de verdade: mantem a chave antiga, para os futuros
            // desconhecidos continuarem a somar no mesmo sitio.
            chave_da_falta = "IShell::CreateInstance CLSID desconhecido";
          }
          traco_.RegistarFalta(Area::Brew, chave_da_falta, det);
        }
      } else if (media_ && media_->Atender(idx, cpu)) {
        // O IMedia (core/brew/imedia) atendeu este indice de saida.
      } else if (AtenderSql(cpu, idx, pp_saida)) {
        // O SQL. Antes do ramo generico pelo mesmo motivo do widget: a faixa
        // 9800+ cai no `idx >= kVtableFileMgr` do fim da cadeia e um `Exec`
        // apareceria nomeado como `IFileMgr::slot2803`.
      } else if (AtenderZclsid(cpu, idx, pp_saida)) {
        // OS DOIS CLSIDs DO Z-WHEEL, ao lado do SQL e pela MESMA razao: as faixas
        // 9900 (IConfig) e 10000 (IDownload) caiam no `idx >= kBaseDoShell` do fim
        // da cadeia, e um `SetItem` apareceria nomeado como um slot do IFileMgr.
      } else if (AtenderWidgets(cpu, idx)) {
        // O WIDGET: `IRootForm`, `IForm`, `IHandler` e `IWidget`.
        //
        // ESTE RAMO VEM ANTES DO `idx >= kBaseDoShell`, e a razao e a classe de
        // erro que ja apareceu duas vezes nesta arvore: os indices da raiz do
        // `IRootForm` CAEM DENTRO da faixa generica (9000 + 4*64), e o ramo
        // generico dava-lhes o nome certo mas o comportamento errado -- recusava
        // e dizia `<interface>::slotN`. Com o ramo do widget a frente, o pedido
        // e ATENDIDO e o nome so aparece quando o que faltou e mesmo um metodo
        // que nao existe.
      } else if (idx >= kVtableIgl && idx < kVtableIgl + gl_slots::kIglSlots) {
        // O GL. ESTE RAMO VEM ANTES DO `idx >= kBaseDoShell`, e por isso e que ele
        // esta escrito AQUI e nao no fim da cadeia: a faixa e 30000+ e o ramo
        // generico (2000) engole-a e da-lhe o nome de um metodo do IFileMgr.
        // **Do mais especifico para o mais generico -- oito casos neste trabalho.**
        //
        // O `po` NAO E PASSADO: medido no `conftest.elf` (gli.h documenta as seis
        // instrucoes do `glCullFace`), o wrapper carrega um argumento por registo e
        // nao toca no r0. So os slots da cabeca (AddRef/Release/QueryInterface) o
        // recebem, e esses vao em `a.reg[0]`.
        ArgumentosGl av;
        for (int k2 = 0; k2 < 4; ++k2) av.reg[k2] = cpu.Get(kR0 + k2);
        av.sp = cpu.Get(kSP);
        av.lr = cpu.Get(kLR);
        std::uint32_t retorno = 0;
        igl_.Executar(idx - kVtableIgl, av, &retorno);
        cpu.Set(kR0, retorno);
      } else if (idx >= kVtableIegl && idx < kVtableIegl + gl_slots::kIeglSlots) {
        // O IEGL, pela mesma razao e com a mesma forma. Sem ele NENHUM `gl*` do
        // jogo acontece: o wrapper do SDK chama `eglInitialize`/`eglChooseConfig`/
        // `eglCreateWindowSurface`/`eglMakeCurrent` ANTES do primeiro `gl*`
        // (medido no `ddragonz.mod`, 0x11d6c4-0x11d890).
        ArgumentosGl av;
        for (int k2 = 0; k2 < 4; ++k2) av.reg[k2] = cpu.Get(kR0 + k2);
        av.sp = cpu.Get(kSP);
        av.lr = cpu.Get(kLR);
        std::uint32_t retorno = 0;
        egl_.Executar(idx - kVtableIegl, av, &retorno);
        cpu.Set(kR0, retorno);
      } else if (idx == kBaseDoShell + brew_slots::kShell_DetectType) {
        // `int DetectType(IShell*, const void *cpBuf, uint32 *pdwSize,
        //                 const char *cpszName, const char **pcpszMIME)` -- o
        // slot 43, que estava no ramo generico (recusava e dizia
        // `IShell::slot43`). O contrato MEDIDO e a implementacao estao no
        // comentario do detector, acima.
        (void)AtenderDetectType(cpu);
      } else if (idx == kBaseDoShell + brew_slots::kShell_GetHandler) {
        // `AEECLSID GetHandler(IShell*, AEECLSID clsBase, const char *pszIn)` --
        // o slot 32, que estava no ramo generico (recusava e dizia
        // `IShell::slot32`). O argumento `r1` NAO e texto: e o `clsBase`. O
        // contrato, a medicao das 95 chamadas e a tabela do registo estao no
        // comentario do `AtenderGetHandler`, acima.
        (void)AtenderGetHandler(cpu);
      } else if (idx == kBaseDoShell + brew_slots::kShell_LoadResData) {
        // `void *LoadResData(IShell*, const char *pszResFile, uint16 id, ResType type)`
        // (AEEIShell.h:250): a variante antiga de LoadResDataEx sempre aloca o
        // blob. O a3d pede `font.bar`, id=5001, type=6 e depois passa o retorno
        // a FreeResData; cair no slot generico devolvia 0x14 como se fosse ponteiro.
        PedidoDeRecurso pedido;
        pedido.ficheiro = LerTextoDe(mem_, cpu.Get(kR1), 512);
        pedido.id = static_cast<std::uint16_t>(cpu.Get(kR2));
        pedido.tipo = static_cast<std::uint16_t>(cpu.Get(kR3));
        pedido.buffer = 0;  // a API legacy nao recebe pBuf: Recursos aloca.
        pedido.pn_tamanho = 0;
        pedido.tem_pn_tamanho = false;
        const ResultadoDoRecurso r = recursos_.Atender(pedido);
        cpu.Set(kR0, r.ponteiro);  // zero se o recurso nao existe.
      } else if (idx == kBaseDoShell + brew_slots::kShell_LoadResObject) {
        // `IBase *LoadResObject(IShell*, const char*, uint16 nResID, AEECLSID)`
        // -- o slot 19, que estava no ramo generico. O comentario da
        // implementacao tem os tres casos medidos.
        (void)AtenderLoadResObject(cpu);
      } else if (idx == kBaseDoShell + brew_slots::kShell_LoadResDataEx) {
        // `void *LoadResDataEx(IShell*, const char *pszResFile, uint16 id,
        //                      ResType type, void *pBuf, uint32 *pnBufSize)`.
        // A ABI AAPCS põe pBuf/pnBufSize na pilha. A semântica das três formas
        // vive em `Recursos`, onde é testada contra o contrato do SDK:
        //  pBuf=-1 -> tamanho + retorno -1; pBuf=0 -> alocar; outro -> copiar.
        const std::uint32_t sp = cpu.Get(kSP);
        PedidoDeRecurso pedido;
        pedido.ficheiro = LerTextoDe(mem_, cpu.Get(kR1), 512);
        pedido.id = static_cast<std::uint16_t>(cpu.Get(kR2));
        pedido.tipo = static_cast<std::uint16_t>(cpu.Get(kR3));
        pedido.buffer = mem_.Ler32(sp);
        pedido.pn_tamanho = mem_.Ler32(sp + 4);
        // A ABI CRUA NO TRACO, e nao so o motivo da recusa: a recusa diz o
        // `id`/`tipo` que se resolveram, mas nao diz QUEM chamou nem com que
        // `pBuf`. Sem o `lr` e sem os dois argumentos da pilha, a pergunta
        // "porque e que este titulo traz um buffer de 131 bytes" nao tem
        // resposta -- e foi essa a pergunta que custou a sonda da frente
        // `ishell2`. (So com `ZB2_TRACE=1`; nao mexe em contagem nenhuma.)
        // O `*pnBufSize` SO SE LE NA FORMA COPIAR, que e a unica que o le: uma
        // leitura a mais num endereco que o guest nao mapeou ficaria registada
        // como leitura nao mapeada e seria atribuida a instrucao seguinte (ver
        // a memoria `a_leitura_de_dados_em_endereco_nao_mapeado_do_curupira`).
        // Uma sonda que muda o que mede nao e uma sonda.
        const bool forma_copiar = (pedido.buffer != 0 && pedido.buffer != kSoOTamanho);
        char abi[192];
        std::snprintf(abi, sizeof(abi),
                      "ficheiro=%s id=%u tipo=%u pBuf=0x%08x %s lr=0x%08x",
                      pedido.ficheiro.c_str(), static_cast<unsigned>(pedido.id),
                      static_cast<unsigned>(pedido.tipo), pedido.buffer,
                      forma_copiar ? ("*pnBufSize=" + Hex(mem_.Ler32(pedido.pn_tamanho))).c_str()
                                   : (pedido.buffer == 0 ? "alocacao" : "so-tamanho"),
                      cpu.Get(kLR));
        traco_.Emitir(Area::Brew, Nivel::Depuracao, "ISHELL_LOADRESDATAEX_ABI", abi);
        const ResultadoDoRecurso r = recursos_.Atender(pedido);
        cpu.Set(kR0, r.ponteiro);  // 0 em recusa, como o SDK exige.
      } else if (idx == kBaseDoShell + brew_slots::kShell_LoadResString) {
        // `int LoadResString(IShell*, const char*, uint16, AECHAR*, int)`.
        const std::uint32_t sp = cpu.Get(kSP);
        PedidoDeTexto pedido;
        pedido.ficheiro = LerTextoDe(mem_, cpu.Get(kR1), 512);
        pedido.base_nula = (cpu.Get(kR1) == 0);
        // A BASE NULA seleciona a cadeia do PROPRIO MODULO, e o `.mif` mora NA
        // PASTA IRMA da pasta do modulo: `<pai de dir_>/mif/<pasta_>.mif`
        // (medido em 62 ficheiros do corpus, um por titulo). O caminho vai no
        // `pedido.ficheiro`; o `Recursos::ServirTexto` le-o directamente.
        if (pedido.base_nula) pedido.ficheiro = dir_ + "/../mif/" + pasta_ + ".mif";
        pedido.id = static_cast<std::uint16_t>(cpu.Get(kR2));
        pedido.destino = cpu.Get(kR3);
        pedido.n_bytes = mem_.Ler32(sp);
        const ResultadoDoTexto r = recursos_.ServirTexto(pedido);
        cpu.Set(kR0, r.ok ? r.caracteres : static_cast<std::uint32_t>(kAeeUnsupported));
      } else if (idx == kSlotIdFreeResData) {
        // Esta vtable usa endereço específico (1563), não `kBaseDoShell+20`.
        // Só `Recursos` sabe quais ponteiros ele próprio alocou; passar outro ao
        // alocador corromperia o heap silenciosamente.
        (void)recursos_.Libertar(cpu.Get(kR1));
        cpu.Set(kR0, kAeeSuccess);  // método void; a recusa fica no Traco.
      } else if (idx == kBaseDoShell + brew_slots::kShell_GetDeviceInfoEx) {
        // `int GetDeviceInfoEx(IShell*, AEEDeviceItem, void*, int*)` (AEEIShell.h).
        // *pnSize e in/out: entrada = bytes do buffer, saida = bytes necessarios.
        //
        // OS ITENS MEDIDOS OU DEMANDADOS:
        //   nItem=0x29 (MODEL_NAME) -- a demanda antiga (recklessracing, rt2);
        //   nItem=0x1c (IMEI) -- o allstarcards (1x, corrida_fmg);
        //   nItem=0x02 (MOBILE_ID) e 0x01 (CHIP_ID) -- o chessbots (1x cada).
        // Valores de `AEEDeviceItems.h` do SDK MP (a extracao 4.0.2 nao traz o
        // cabecalho; os numeros sao os do BrewMPSDK-7.12.5) e nomes por extenso
        // em `AEEDeviceItems.h:31-41`.
        const std::uint32_t item = cpu.Get(kR1);
        const std::uint32_t p_buf = cpu.Get(kR2);
        const std::uint32_t p_tam = cpu.Get(kR3);
        struct Item {
          std::uint32_t id;
          const char* nome;
          // AECHAR (UTF-16LE) quando nao nulo; ASCII quando `larga` e nulo.
          const std::uint16_t* larga;
          const char* ascii;
          std::uint32_t bytes;
        };
        // u"MSM7201A" -- o fabricante do Zeebo, do guia oficial
        // (ZeeboDeveloperGuide0.97.md:224, "o MSM7201A").
        static const std::uint16_t kModelo[] = {'Z', 'e', 'e', 'b', 'o', 0};
        static const std::uint16_t kChipDoZeebo[] = {'M', 'S', 'M', '7', '2', '0', '1', 'A', 0};
        // O IMEI e o do zeebx: sintetico, com os 15 digitos e o digito de Luhn
        // certo (quem pede um IMEI costuma confere-lo). O MOBILE_ID de uma
        // consola sem rede e a cadeia vazia -- honesto.
        static const Item kItens[] = {
            {0x01u, "CHIP_ID", kChipDoZeebo, nullptr, sizeof(kChipDoZeebo)},
            {0x02u, "MOBILE_ID", nullptr, "", 1},
            {0x1cu, "IMEI", nullptr, "350000000000006", 16},
            {0x29u, "MODEL_NAME", kModelo, nullptr, sizeof(kModelo)},
        };
        const Item* item_servido = nullptr;
        for (const Item& it : kItens) {
          if (it.id == item) item_servido = &it;
        }
        if (p_tam == 0) {
          cpu.Set(kR0, kAeeBadParm);
        } else if (item_servido == nullptr) {
          char det[64];
          std::snprintf(det, sizeof(det), "nItem=0x%08x sem suporte", item);
          traco_.RegistarFalta(Area::Brew, "IShell::GetDeviceInfoEx", det);
          cpu.Set(kR0, kAeeUnsupported);
        } else if (p_buf == 0) {
          mem_.Escrever32(p_tam, item_servido->bytes);
          cpu.Set(kR0, kAeeSuccess);
          traco_.Emitir(Area::Brew, Nivel::Depuracao, "ISHELL_GETDEVICEINFOEX",
                        std::string(item_servido->nome) + " so-tamanho -> " +
                            std::to_string(item_servido->bytes));
        } else {
          const std::uint32_t cabem = mem_.Ler32(p_tam);
          if (item_servido->larga != nullptr) {
            const std::uint32_t unidades = item_servido->bytes / 2;
            if (cabem >= item_servido->bytes) {
              for (std::uint32_t i = 0; i < unidades; ++i) {
                mem_.Escrever16(p_buf + i * 2, item_servido->larga[i]);
              }
            } else {
              // Preenchimento parcial com NUL final garantido (unidades de 2).
              const std::uint32_t que_cabem = cabem / 2;
              for (std::uint32_t i = 0; i < que_cabem; ++i) {
                const std::uint16_t c =
                    (i + 1 == que_cabem) ? 0 : item_servido->larga[i];
                mem_.Escrever16(p_buf + i * 2, c);
              }
            }
          } else {
            const std::uint32_t n = item_servido->bytes - 1;  // sem o NUL
            if (cabem > 0) {
              const std::uint32_t copiar = std::min(cabem - 1, n);
              for (std::uint32_t i = 0; i < copiar; ++i) {
                mem_.Escrever8(p_buf + i, static_cast<std::uint8_t>(item_servido->ascii[i]));
              }
              mem_.Escrever8(p_buf + copiar, 0);
            }
          }
          mem_.Escrever32(p_tam, item_servido->bytes);
          cpu.Set(kR0, kAeeSuccess);
          char det[96];
          std::snprintf(det, sizeof(det), "%s cabem=%u -> %u", item_servido->nome, cabem,
                        item_servido->bytes);
          traco_.Emitir(Area::Brew, Nivel::Depuracao, "ISHELL_GETDEVICEINFOEX", det);
        }
      } else if (idx == kBaseDoShell + brew_slots::kShell_GetClassItemID) {
        // `uint32 GetClassItemID(IShell*, AEECLSID cls)` -- o SLOT 45, que estava
        // no ramo generico: recusava com `IShell::slot45` e deixava no `r0` o
        // `kAeeUnsupported` (20 = 0x14), que o `tectoy` guardava e passava ao
        // `IDownload::slot21` COMO SE FOSSE um id de item (ver o comentario do
        // `AtenderGetClassItemID`). O `r1` deste slot e um CLSID, e nao texto.
        (void)AtenderGetClassItemID(cpu);
      } else if (idx == kBaseDoShell + brew_slots::kShell_Resume) {
        // ISHELL_Resume -- IShell slot 36. `int Resume(IShell*, AEECallback* pcb)`
        // (AEEIShell.h, INHERIT_IShell). E O MECANISMO das threads cooperativas:
        // o jogo pede a retomada por aqui, e so entao a thread suspensa tem como
        // voltar (`classes.h`, `EnfileirarThreadPeloCallbackDeRetomada`; zeebx
        // `shell_resume`). Quando o `pcb` e o `GetResumeCBK` de uma thread, a
        // thread e ENFILEIRADA e o despacho retoma-a na proxima fronteira entre
        // chamadas de API. Um `pcb` que nao seja de thread nao tem fila de
        // callbacks genericos neste despacho -- RECUSA COM NOME, em vez de
        // prometer um callback que nunca corre (P2).
        //
        // O VALOR devolvido ao guest e SUCCESS (o zeebx devolve-o); o cabecalho
        // nao documenta outro.
        const std::uint32_t pcb = cpu.Get(kR1);
        if (EnfileirarThreadPeloCallbackDeRetomada(pcb)) {
          cpu.Set(kR0, kAeeSuccess);
          char det[64];
          std::snprintf(det, sizeof(det), "pcb=0x%08x enfileirada", pcb);
          traco_.Emitir(Area::Brew, Nivel::Depuracao, "ISHELL_RESUME", det);
        } else {
          char det[64];
          std::snprintf(det, sizeof(det), "pcb=0x%08x nao e de thread", pcb);
          traco_.RegistarFalta(Area::Brew, "IShell::Resume", det);
          cpu.Set(kR0, kAeeUnsupported);
        }
      } else if (idx == kBaseDoShell + brew_slots::kShell_SendEvent) {
        // ISHELL_SendEvent -- IShell slot 21. ESTE RAMO VEM ANTES DO
        // `idx >= kBaseDoShell`, que e o ramo generico: la, este pedido era
        // registado como `IShell::slot21` e respondido com `kAeeUnsupported`
        // (20). E `SendEvent` devolve **boolean** -- 20 e TRUE. O `tectoy`
        // (274755) faz `cmp r0,#0 / ldrne r0,[sp,#8]` (0x6a39c-0x6a3a0): com 20
        // ele seguia o ramo do SUCESSO, lia o zero que ele proprio pos no
        // `dwParam`, e nem chegava a imprimir `SendEvent to get PrefsDB failed`.
        // Mentiamos e apagavamos o diagnostico ao mesmo tempo.
        //
        //   r1=wFlags  r2=clsApp  r3=evt  [sp+0]=wParam  [sp+4]=dwParam
        //   (AEEIShell.h:309 -- SEIS argumentos; a macro de cinco poe wFlags=0)
        const std::uint32_t sp = cpu.Get(kSP);
        const std::uint32_t flags = cpu.Get(kR1);
        const std::uint32_t cls = cpu.Get(kR2);
        const std::uint32_t evt = cpu.Get(kR3);
        const std::uint16_t wp = static_cast<std::uint16_t>(mem_.Ler32(sp));
        const std::uint32_t dwp = mem_.Ler32(sp + 4);
        std::uint32_t devolveu = 0;
        std::uint64_t gastos = 0;
        if ((flags & kEvtflgAsync) != 0) {
          // O `ISHELL_PostEvent` (`AEEShell.h:279`) pede o ADIAMENTO para a
          // volta seguinte do laco de eventos (`AEEIShell.h:2894`). Aqui nao ha
          // fila de eventos de applet -- e entrega-lo como se fosse sincrono
          // mudava a ordem que o titulo pediu. RECUSA COM NOME, e nao um
          // silencio: e assim que ele aparece na lista de demanda se algum
          // titulo o usar. (zeebx e zeebulator ignoram os wFlags sem o dizer.)
          char det_ev[160];
          std::snprintf(det_ev, sizeof(det_ev),
                        "wFlags=0x%04x cls=0x%08x evt=0x%04x wp=%u dwp=0x%08x lr=0x%08x", flags,
                        cls, evt, static_cast<unsigned>(wp), dwp, lr);
          traco_.RegistarFalta(Area::Brew, "IShell::SendEvent(EVTFLG_ASYNC)", det_ev);
          cpu.Set(kR0, 0);  // FALSE
        } else {
          const bool entregue =
              EntregarEventoAoApplet(cpu, cls, evt, wp, dwp, pp_saida, &devolveu, &gastos);
          // OS PASSOS DA ENTREGA SAEM DO ORCAMENTO DA FASE. A entrega corre
          // codigo do titulo; nao os contar aqui era dar tempo de graca e fazer
          // a coluna `passos_start` da bateria mentir.
          resultado.passos += gastos;
          cpu.Set(kR0, entregue ? devolveu : 0);  // FALSE = ninguem tratou
        }
      } else if (idx == kVtableFileMgr + brew_slots::kFileMgr_MkDir) {
        // O `MkDir` NO ENDERECO DO SDK (slot 5 = 7000+5), e tambem no id 1544.
        // A VFS e somente de leitura para reprodutibilidade. Servido com SUCCESS
        // e pressuposto declarado (padrao do Remove e RmDir).
        std::string nome_mkdir;
        mem_.LerCadeia(cpu.Get(kR1), &nome_mkdir, 512);
        ultimo_erro_do_fm_ = kAeeSuccess;
        traco_.RegistarPressuposto(Area::Brew, "IFileMgr::MkDir",
                                   "serviu SUCCESS no slot 5 (SDK): " + nome_mkdir +
                                       "; diretorio NAO criado no hospedeiro (VFS so de leitura)");
        traco_.Emitir(Area::Brew, Nivel::Depuracao, "FM_MKDIR",
                      nome_mkdir + " -> OK (nada criado)");
        cpu.Set(kR0, static_cast<std::uint32_t>(ultimo_erro_do_fm_));
      } else if (idx == kVtableFileMgr + brew_slots::kFileMgr_RmDir) {
        // O `RmDir` NO ENDERECO QUE O SDK DIZ (slot 6 = 7000+6), e nao so no id
        // que a cablagem da ferramenta produz (1547, acima--o servico responde
        // nos dois). Este ramo vem ANTES do generico `idx >= kBaseDoShell` de
        // proposito: a faixa 7000+ e a vtable do IFileMgr, e o ramo generico
        // dava a estes dois enderecos o nome "IFileMgr::slotN" sem os atender.
        // O teste `FileMgrServido.ORmDirRespondeNosDoisEnderecosDoSlot` cobre
        // os dois enderecos.
        std::string nome_end;
        mem_.LerCadeia(cpu.Get(kR1), &nome_end, 512);
        const bool existe_end = vfs_.Existe(nome_end);
        ultimo_erro_do_fm_ = existe_end ? kAeeSuccess : kAeeFailed;
        traco_.RegistarPressuposto(Area::Brew, "IFileMgr::RmDir",
                                   existe_end ? "serviu SUCCESS no slot 6 (SDK): " + nome_end
                                              : "serviu EFAILED no slot 6 (SDK): " + nome_end);
        cpu.Set(kR0, static_cast<std::uint32_t>(ultimo_erro_do_fm_));
      } else if (idx == kVtableFileMgr + brew_slots::kFileMgr_GetInfo) {
        // `int GetInfo(IFileMgr *po, const char *pszName, FileInfo *pInfo)` --
        // IFileMgr slot 3 (`AEEFile.h:213`). MEDIDO: o `ridgeracer` pede-o 2x e a
        // recusa dava-lhe o nome GENERICO ("IFileMgr::slot3") -- um numero, e nao o
        // nome da operacao, que e o que a regra P2 desta casa exige.
        //
        // O `FileInfo` e a struct do `AEEFile.h:73-79`:
        //   `char attrib; uint32 dwCreationDate; uint32 dwSize; char szName[64]`
        // (76 bytes, e o `attrib` deixa 3 de enchimento antes da data). O `0` do
        // `attrib` e o `AEE_FA_NORMAL` do proprio cabecalho.
        std::string nome_info;
        mem_.LerCadeia(cpu.Get(kR1), &nome_info, 512);
        const std::uint32_t p_info = cpu.Get(kR2);
        std::vector<std::uint8_t> bytes_info;
        std::string motivo_info;
        if (!vfs_.Existe(nome_info) || !vfs_.Ler(nome_info, &bytes_info, &motivo_info)) {
          // NAO EXISTE -> `EFAILD`, e o `szName` NAO se escreve (nao ha nome a dar).
          ultimo_erro_do_fm_ = kAeeFailed;
          traco_.Emitir(Area::Brew, Nivel::Aviso, "IFILEMGR_GETINFO",
                        nome_info + " -> EFAILED (nao existe na VFS)");
          cpu.Set(kR0, static_cast<std::uint32_t>(kAeeFailed));
          // NAO `continue` AQUI. O `++resultado.passos` do laco desta fase esta no
          // FIM do corpo (`:4936`, sob o `while (resultado.passos < limite)` de
          // `:2477`): um `continue` SALTA-O, o tecto nunca fecha e a fase nunca
          // acaba. Foi o que aconteceu -- o ramo de erro do `GetInfo` prendeu o
          // teste `FileMgrServido.OGetInfo...` para sempre (o teste leva a VFS
          // VAZIA, logo cai sempre aqui). No titulo nao se via: as 2 chamadas do
          // `ridgeracer` acertam no ficheiro e seguem pelo caminho bom.
          //
          // A FORMA CERTA e a dos ramos vizinhos (o `RmDir`, acima): servir a
          // chamada e DEIXAR CAIR no fecho comum, que faz o retorno e conta o
          // passo. Este e o mesmo defeito que matou a varredura do `audit` (um
          // `continue` antes do `++resultado.passos`, 20 h a 99% de CPU).
          //
          // E O `else` NAO E ORNAMENTO: sem ele o fluxo do ERRO cai dentro do
          // caminho de SUCESSO (logo abaixo), que escreve `kAeeSuccess` por cima
          // do `EFAILD` -- a chamada respondia 0 a um ficheiro que nao existe. O
          // teste apanhou-o na primeira assercao.
        } else {
        if (p_info != 0) {
          mem_.Escrever8(p_info + 0, 0);                                        // AEE_FA_NORMAL
          mem_.Escrever32(p_info + 4, 0);                                       // dwCreationDate
          mem_.Escrever32(p_info + 8, static_cast<std::uint32_t>(bytes_info.size()));
          for (std::uint32_t k = 0; k < 64u; ++k) mem_.Escrever8(p_info + 12u + k, 0);
          // O NOME: e o que esta chamada tem de dar e o `EnumNext` nao da (o nosso
          // `Informacao` deixa-o a zero). Cabem 63 caracteres e o terminador.
          const std::size_t quantos = std::min<std::size_t>(nome_info.size(), 63u);
          for (std::size_t k = 0; k < quantos; ++k) {
            mem_.Escrever8(p_info + 12u + static_cast<std::uint32_t>(k),
                           static_cast<std::uint8_t>(nome_info[k]));
          }
        }
        ultimo_erro_do_fm_ = kAeeSuccess;
        traco_.Emitir(Area::Brew, Nivel::Depuracao, "IFILEMGR_GETINFO",
                      nome_info + " -> " + std::to_string(bytes_info.size()) + " bytes");
        cpu.Set(kR0, static_cast<std::uint32_t>(kAeeSuccess));
        }
      } else if (idx == kVtableFileMgr + brew_slots::kFileMgr_EnumNext) {
        // `boolean EnumNext(IFileMgr *po, FileInfo *pInfo)` -- IFileMgr slot 11
        // (`kFileMgr_EnumNext`; `AEEFile.h`, `IFILEMGR_EnumNext`).
        //
        // MEDIDO (corrida do corte): gof, rmp e pbc chamam o slot 11 LOGO NO
        // ARRANQUE, sem `EnumInit` antes -- e a sonda de saves "ha entradas?".
        // A demonstracao esta no proprio guest: `gof.mod` 0x3688c le a vtable e
        // `ldr r2,[r1,#44]` (vtable[11]) e usa o retorno como boolean
        // (`cmp r0,#1` em 0x36898). Sem estado de enumeracao a resposta
        // honesta e FALSE (iteracao vazia), e o `GetLastError` passa a EFAILED
        // -- o contrato do SDK manda exactamente isto: FALSE seguido de
        // GetLastError devolve EFAILED mesmo quando a enumeracao terminou bem.
        traco_.RegistarPressuposto(Area::Brew, "IFileMgr::EnumNext",
                                   "sem EnumInit servido: iteracao vazia, devolve FALSE (nao ha entradas)");
        ultimo_erro_do_fm_ = kAeeFailed;
        cpu.Set(kR0, 0);  // FALSE
      } else if (idx >= zb2::brew::kVtableBitmap + 2 &&
                 idx < zb2::brew::kVtableBitmap + 16 &&
                 AtenderBitmapDaFamilia(cpu, mem_, al_, traco_, idx)) {
        // A FAMILIA DO IBITMAP, servida sobre o OBJECT a que o guest chamou
        // (frente ibmap2). ESTE RAMO TEM DE FICAR ANTES DO RAMO GENERICO
        // `idx >= kBaseDoShell`: a faixa 8002..8015 e >= 2000, e a cadeia e de
        // `else if` -- um ramo posto DEPOIS do generico NUNCA corre para estes
        // indices. MEDIDO: com o ramo depois, o trace de uma corrida de 3
        // titulos e BYTE A BYTE o mesmo da base (o gancho era codigo morto, e
        // a medida dizia "sem regressao" sobre um gancho que nao existia).
      } else if (idx == kSlotIdHeapCheckAvail) {
        // `boolean CheckAvail(IHeap *po, uint32 dwSize)` -- IHeap slot 6
        // (`AEEHeap.h`). Responde se um `Malloc(dwSize)` caberia AGORA, com a
        // MESMA conta do `Malloc` (`Alocador::Caberia`): TRUE com espaco e
        // FALSE sem -- sem pressuposto e sem adivinha.
        cpu.Set(kR0, al_.Caberia(cpu.Get(kR1)) ? 1u : 0u);
      } else if (idx == kSlotIdGetTimerExpiration) {
        // `uint32 GetTimerExpiration(IShell *po, void (*pfn)(void *), void *pUser)`
        // -- IShell slot 13 (`AEEIShell.h`). Devolve o restante em ms do
        // temporizador armado para esse (pfn, pUser), ou 0 se nao houver.
        // Devolve NUMERO, nao codigo: 0 e "sem temporizador", e nao erro.
        const std::uint32_t pfn = cpu.Get(kR1), puser = cpu.Get(kR2);
        std::uint32_t restante = 0;
        if (timer_.ativo && timer_.pfn == pfn && timer_.puser == puser &&
            timer_.vence_em_ms > agora_ms_) {
          restante = static_cast<std::uint32_t>(timer_.vence_em_ms - agora_ms_);
        }
        cpu.Set(kR0, restante);
      } else if (idx >= kBaseDoShell) {
        // O NOME tem de dizer de QUE interface e o slot. Um so "IShell::slot"
        // para tudo dava `IShell::slot4004` para um metodo do IDisplay -- numero
        // sem nome outra vez, e ja foi esse o defeito que me fez perder uma
        // ronda inteira a olhar para a lista errada.
        char nome[64], det[128];
        const char* iface = "IShell";
        std::uint32_t slot = 0;
        // Os objectos GENERICOS tem vtable propria por interface: sem isto o
        // `slot 7` de um `ISound` aparecia nomeado como `IFileMgr`, e a lista de
        // demanda mentia sobre o que os titulos pedem.
        const std::uint32_t kgen = (idx >= zb2::brew::kVtableGenericoBase)
                                       ? (idx - zb2::brew::kVtableGenericoBase) / 64u
                                       : kNGenericos;
        if (kgen < kNGenericos) {
          iface = kGenericos[kgen].nome;
          slot = idx - zb2::brew::VtGenerico(kgen);
        } else if (idx >= zb2::brew::kVtableBitmap &&
                   idx < zb2::brew::kVtableBitmap + zb2::brew::kSlotsPorVtable) {
          // A vtable do IBitmap do ecra (8000). Sem este ramo, o slot 13 dela
          // aparecia como `IFileMgr::slot1013` -- o nome errado da interface
          // errada (7000 + 1013 = 8013).
          iface = "IBitmap"; slot = idx - zb2::brew::kVtableBitmap;
        } else if (idx >= zb2::brew::kVtableFileMgr) { iface = "IFileMgr"; slot = idx - zb2::brew::kVtableFileMgr; }
        else if (idx >= zb2::brew::kVtableDisplay) { iface = "IDisplay"; slot = idx - zb2::brew::kVtableDisplay; }
        else { iface = "IShell"; slot = idx - kBaseDoShell; }
        std::snprintf(nome, sizeof(nome), "%s::slot%u", iface, slot);
        // O RAMO DE NOMES CONSULTA A TABELA (frente io2): para o objecto
        // IUnzipAStream (kGenericos[1]) o nome do SDK diz QUAL metodo foi
        // pedido -- `Read`, `SetStream` -- e nao um numero. O `IFile` do nome
        // antigo era o erro de dois nomes que esta frente corrigiu.
        if (kgen == 1 && slot < 6) {
          std::snprintf(nome, sizeof(nome), "%s::%s", iface, NomeDeUnzipStream(slot));
        }
        // Os ARGUMENTOS no detalhe: para o CreateInstance (slot 2) o r1 e o ClsId
        // pedido, e sem ele nao se sabe o que responder. Foi assim que se
        // percebeu, na arvore antiga, quais das interfaces eram as mesmas por
        // dois nomes diferentes.
        // O LR entra no detalhe porque sem ele nao se sabe QUEM chama.
        //
        // Foi a falta dele que me deixou a olhar para um `IDisplay::slot2` com uma
        // FONTE (`AEE_FONT_NORMAL = 0x8000`) no r1 -- argumento que nenhum metodo
        // daquele slot aceita -- sem forma de saber de onde vinha a chamada. Com o
        // LR, vai-se ao sitio e le-se a instrucao.
        // r3 E OS ARGUMENTOS NA PILHA entram tambem: ha metodos com SEIS
        // argumentos (`LoadResDataEx`, `MeasureTextEx`), e sem eles nao se sabe
        // o que o pedido quer -- so se sabe que existe.
        const std::uint32_t sp = cpu.Get(kSP);
        // SE O r1 FOR UM PONTEIRO PARA TEXTO, LE-SE O TEXTO.
        //
        // Um nome de ficheiro de recurso diz mais do que o numero do ponteiro --
        // e foi assim que se descobriu que o `pacmania` pede um recurso de um
        // ficheiro concreto. Sem isto ficava-se a olhar para 0x80202a70.
        char txt[48] = {0};
        const std::uint32_t possivel = cpu.Get(kR1);
        // E A PAGINA TEM DE ESTAR MAPEADA, e a guarda de FAIXA nao chega: MEDIDO
        // nesta frente, o `IShell::slot45` (`GetClassItemID`) recebe o CLSID do
        // titulo em r1 (0x01070798) -- dentro da faixa, mas NAO e memoria. Ler o
        // "texto" ali registava uma leitura nao mapeada que era NOSSA, e o
        // `pc_de_quem_leu` apontava para a instrucao do guest seguinte (a mesma
        // armadilha que a frente `ropi2` escreveu). A leitura do instrumento
        // ainda por cima deixa o estado pendente do `Memoria` -- que a CPU usa
        // para julgar a INSTRUCAO.
        if (possivel >= 0x00100000u && possivel < 0x81000000u && mem_.Existe(possivel)) {
          bool imprimivel = true;
          for (int k = 0; k < 40; ++k) {
            const std::uint8_t ch = mem_.Ler8(possivel + static_cast<std::uint32_t>(k));
            if (ch == 0) break;
            if (ch < 0x20 || ch > 0x7e) { imprimivel = false; break; }
            txt[k] = static_cast<char>(ch);
          }
          if (!imprimivel) txt[0] = 0;
        }
        std::snprintf(det, sizeof(det),
                      "r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x sp0=0x%08x sp1=0x%08x lr=0x%08x txt=%s",
                      r0, cpu.Get(kR1), cpu.Get(kR2), cpu.Get(kR3), mem_.Ler32(sp),
                      mem_.Ler32(sp + 4), cpu.Get(kLR), txt);
        traco_.RegistarFalta(Area::Brew, nome, det);
        cpu.Set(kR0, kAeeUnsupported);
        recusou_agora = true;
        if (++recusas_seguidas > 200) { resultado.motivo = "parou_em_slot_nao_implementado"; return resultado; }
      } else if (idx == kSlotIdUnzipSetStream) {
        // `void SetStream(IUnzipAStream *po, IAStream *pIAStream)` -- slot 5
        // (`AEEUnzipStream.h`). Guarda a origem e RESETA o estado: uma origem
        // nova anula a expansao anterior (o ALLSTARCARDS entrega o IMemAStream
        // que criou sobre o blob gzip de um recurso).
        EstadoDoUnzip& e = unzips_[r0];
        e = EstadoDoUnzip{};
        e.origem = cpu.Get(kR1);
        traco_.Emitir(Area::Brew, Nivel::Depuracao, "UNZIP_SETSTREAM",
                      "obj=0x" + Hex(r0) + " origem=0x" + Hex(e.origem));
      } else if (idx == kSlotIdUnzipReadable) {
        // `boolean Readable(po, pfn, pUser)` -- slot 2. A resposta honesta e
        // FALSE enquanto nao ha bytes por ler (sem SetStream, ou antes da
        // primeira leitura); TRUE depois, enquanto a posicao esta no meio. O
        // zeebx devolve o mesmo por "o conteudo ja esta inteiro na memoria".
        const auto it = unzips_.find(r0);
        const bool tem = it != unzips_.end() && it->second.expandido &&
                         it->second.pos < it->second.saida.size();
        cpu.Set(kR0, tem ? 1u : 0u);
      } else if (idx == kSlotIdUnzipRead) {
        // `int32 Read(po, pDest, nWant)` -- slot 3. A descompressao corre de
        // UMA VEZ na primeira leitura (como o zeebx, `UnzipState`), e as
        // leituras seguintes saem do buffer. No fim, 0 -- o contrato do
        // IAStream. O allstarcards le 310 KB em pedacos e testa o retorno.
        EstadoDoUnzip& e = unzips_[r0];
        if (!e.expandido) {
          e.expandido = true;
          if (e.origem == 0) {
            // Sem origem (o SetStream recebeu NULL): nao ha o que descomprimir.
            // EOF, com a razao no traco -- um 0 em silencio esconderia o
            // ficheiro em falta (P2).
            if (!e.origem_desconhecida) {
              e.origem_desconhecida = true;
              traco_.RegistarFalta(Area::Brew, "IUnzipAStream::Read",
                                   "SetStream nunca recebeu origem (NULL): fim do stream");
            }
          } else if (!ExpandirUnzip(e)) {
            // A recusa ja foi registada pelo ExpandirUnzip; a leitura devolve
            // EOF para o jogo nao ficar em laco de saidas.
          }
        }
        const std::uint32_t p_dest = cpu.Get(kR1);
        const std::uint32_t pedido = cpu.Get(kR2);
        const std::uint32_t disponivel =
            e.pos < e.saida.size() ? static_cast<std::uint32_t>(e.saida.size() - e.pos) : 0u;
        const std::uint32_t n = std::min(pedido, disponivel);
        for (std::uint32_t i = 0; i < n; ++i) {
          mem_.Escrever8(p_dest + i, e.saida[e.pos + i]);
        }
        e.pos += n;
        cpu.Set(kR0, n);
      } else if (idx == kSlotIdUnzipCancel) {
        // `void Cancel(po, pfn, pUser)` -- slot 4. Nao ha leitura assincrona
        // aqui: cancelar nao tem trabalho, e o metodo e void.
        cpu.Set(kR0, kAeeSuccess);
      } else if (idx >= kSlotIdUnzipSlots && idx < kSlotIdUnzipSlots + 58) {
        // Os slots 6..63 do IUnzipAStream (nenhum titulo os pede, medido): a
        // recusa leva o NOME do SDK e nao cai no ramo dos ajudantes, que os
        // nomearia como offsets de uma tabela que nao e a deles.
        const unsigned slot = 6 + (idx - kSlotIdUnzipSlots);
        traco_.RegistarFalta(Area::Brew, std::string("IUnzipAStream::") + NomeDeUnzipStream(slot),
                             "sem implementacao nesta etapa");
        cpu.Set(kR0, kAeeUnsupported);
        recusou_agora = true;
        if (++recusas_seguidas > 200) { resultado.motivo = "parou_em_slot_nao_implementado"; return resultado; }
      } else if (idx == kSlotIdMemStreamSet || idx == kSlotIdMemStreamSetEx) {
        // `void Set(po, pBuff, dwSize, dwOffset, bSysMem)` e `SetEx` -- slots
        // 5/6 do IMemAStream (AEE.h, IMEMASTREAM_Set). A janela de leitura
        // comeca em `pBuff + dwOffset` (o SDK diz-o por extenso) e tem `dwSize`
        // bytes. O `SetEx` traz funcoes de libertao que nao se aplicam: o
        // buffer e do guest e a VFS nao o possui -- declarado no pressuposto.
        auto& m = memstreams_[r0];
        m.base = cpu.Get(kR1) + cpu.Get(kR3);
        m.tamanho = cpu.Get(kR2);
        m.pos = 0;
        if (idx == kSlotIdMemStreamSetEx) {
          traco_.RegistarPressuposto(Area::Brew, "IMemAStream::SetEx",
                                     "pUserFreeFn/pUserFeeData nao aplicados: o buffer e do guest");
        }
      } else if (idx == kSlotIdMemStreamReadable) {
        const auto it = memstreams_.find(r0);
        const bool tem = it != memstreams_.end() && it->second.pos < it->second.tamanho;
        cpu.Set(kR0, tem ? 1u : 0u);
      } else if (idx == kSlotIdMemStreamRead) {
        // `int32 Read(po, pDest, nWant)` -- slot 3. Le do bloco que o `Set`
        // declarou, da posicao corrente ate ao fim.
        auto& m = memstreams_[r0];
        const std::uint32_t p_dest = cpu.Get(kR1);
        const std::uint32_t pedido = cpu.Get(kR2);
        const std::uint32_t resta = m.pos < m.tamanho ? m.tamanho - m.pos : 0u;
        const std::uint32_t n = std::min(pedido, resta);
        for (std::uint32_t i = 0; i < n; ++i) {
          mem_.Escrever8(p_dest + i, mem_.Ler8(m.base + m.pos + i));
        }
        m.pos += n;
        cpu.Set(kR0, n);
      } else if (idx == kSlotIdMemStreamCancel) {
        cpu.Set(kR0, kAeeSuccess);
      } else if (idx >= kSlotIdMemStreamSlots && idx < kSlotIdMemStreamSlots + 57) {
        const unsigned slot = 7 + (idx - kSlotIdMemStreamSlots);
        traco_.RegistarFalta(Area::Brew, std::string("IMemAStream::") + NomeDeMemStream(slot),
                             "sem implementacao nesta etapa");
        cpu.Set(kR0, kAeeUnsupported);
        recusou_agora = true;
        if (++recusas_seguidas > 200) { resultado.motivo = "parou_em_slot_nao_implementado"; return resultado; }
      } else if (idx == kSlotIdStrlen) {
        // size_t strlen(const char *s) -- conta ate ao NUL, sem limite
        // artificial: a memoria do guest responde zero onde nao ha nada.
        std::uint32_t n = 0;
        while (mem_.Ler8(r0 + n) != 0) ++n;
        cpu.Set(kR0, n);
      } else if (idx == kSlotIdMemset) {
        // void *memset(void *d, int c, size_t n) -- devolve o destino
        const std::uint32_t n = cpu.Get(kR2);
        for (std::uint32_t i = 0; i < n; ++i) mem_.Escrever8(r0 + i, static_cast<std::uint8_t>(cpu.Get(kR1)));
        cpu.Set(kR0, r0);
      } else if (idx == kSlotIdStrcpy) {
        const std::uint32_t src = cpu.Get(kR1);
        std::uint32_t i = 0;
        for (;;) {
          const std::uint8_t b = mem_.Ler8(src + i);
          mem_.Escrever8(r0 + i, b);
          if (b == 0) break;
          ++i;
        }
        cpu.Set(kR0, r0);
      } else if (idx == kSlotIdStrcat) {
        // `char *strcat(char *dst, const char *src)` -- AEEHelperFuncs 0x00C
        // (`tools/ajudantes_slots.inc:157`). Devolve o `dst`, como a libc.
        //
        // Pedido por DEZ dos 62 titulos -- a ajudante em falta que mais titulos
        // afectava. Nao e uma funcao "avancada": e `strcat`, e faltava.
        const std::uint32_t src = cpu.Get(kR1);
        std::uint32_t fim = 0;
        while (mem_.Ler8(r0 + fim) != 0) ++fim;  // o NUL do destino
        std::uint32_t i = 0;
        for (;;) {
          const std::uint8_t b = mem_.Ler8(src + i);
          mem_.Escrever8(r0 + fim + i, b);
          if (b == 0) break;
          ++i;
        }
        cpu.Set(kR0, r0);
      } else if (idx == kSlotIdStrncpy) {
        // `char *strncpy(char *dst, const char *src, size_t n)` -- 0x0C8.
        // A SEMANTICA DA LIBC, com as duas pontas que enganam: NAO termina em
        // NUL se o `src` tiver n ou mais bytes, e ENCHE o resto com zeros se
        // tiver menos. Copiar so ate ao NUL deixaria lixo no fim do buffer, e
        // terminar sempre escreveria um byte a mais do que o jogo reservou.
        const std::uint32_t src = cpu.Get(kR1);
        const std::uint32_t n = cpu.Get(kR2);
        std::uint32_t i = 0;
        for (; i < n; ++i) {
          const std::uint8_t b = mem_.Ler8(src + i);
          mem_.Escrever8(r0 + i, b);
          if (b == 0) break;
        }
        for (; i < n; ++i) mem_.Escrever8(r0 + i, 0);  // o enchimento da libc
        cpu.Set(kR0, r0);
      } else if (idx == kSlotIdStrstr) {
        // `char *strstr(const char *haystack, const char *needle)` -- 0x0D8.
        // Devolve o ponteiro para a primeira ocorrencia, ou 0.
        //
        // O OFFSET E 0x0D8 E NAO 0x0E8. O `0x0E8` e o `stristr`, que NAO
        // distingue maiusculas (`AEEStdLib.h`, campos 152 e 156 da
        // `struct AEEHelperFuncs`). O zeebulator regista o `strstr` em 0x0E8
        // (`core/brew/mod_runtime.cpp:21`), e isso e uma troca silenciosa: o
        // jogo recebe uma resposta PLAUSIVEL e errada -- encontra onde nao devia
        // ou nao encontra o que existe -- e nada falha nem fica registado.
        // O topo do `tools/ajudantes_slots.inc` ja avisava deste par, em "AS
        // QUATRO QUE ENGANAM"; o proprio ficheiro e gerado do cabecalho.
        const std::uint32_t agulha = cpu.Get(kR1);
        if (mem_.Ler8(agulha) == 0) {  // agulha vazia: devolve o palheiro
          cpu.Set(kR0, r0);
        } else {
          std::uint32_t achou = 0;
          for (std::uint32_t i = 0; mem_.Ler8(r0 + i) != 0; ++i) {
            std::uint32_t j = 0;
            for (;; ++j) {
              const std::uint8_t a2 = mem_.Ler8(agulha + j);
              if (a2 == 0) { achou = r0 + i; break; }
              if (mem_.Ler8(r0 + i + j) != a2) break;
            }
            if (achou != 0) break;
          }
          cpu.Set(kR0, achou);
        }
      } else if (idx == kSlotIdSleep) {
        // `void sleep(uint32 msecs)` -- AEEHelperFuncs 0x184
        // (`tools/ajudantes_slots.inc:251`). Pedido por NOVE dos 62.
        //
        // NAO DORME, E ISSO E DE PROPOSITO. Dormir de verdade seria parar o
        // relogio do anfitriao dentro de uma medicao -- viola o P4 pelo mesmo
        // motivo que o orcamento por relogio violava (ver `despacho.cpp` no
        // `Correr`, e o `kOrcamentoSegundos` que foi apagado). O tempo do
        // emulador e o VIRTUAL, e quem o avanca e o laco de eventos.
        //
        // O que se faz e AVANCAR O RELOGIO VIRTUAL pelos milissegundos pedidos,
        // que e o que o guest observa a seguir se chamar `GetUpTimeMS`. Assim um
        // `sleep(100)` num laco de espera termina, em vez de rodar para sempre.
        // Um tecto para o avanco: um `sleep` com um valor absurdo (lixo num
        // registador) nao pode empurrar o relogio virtual para o fim do mundo e
        // fazer vencer todos os temporizadores de uma vez.
        constexpr std::uint32_t kTectoMs = 60000;  // um minuto virtual
        agora_ms_ += (r0 > kTectoMs) ? kTectoMs : r0;
        cpu.Set(kR0, 0);
      } else if (idx == kSlotIdStrcmp) {
        const std::uint32_t a2 = r0, b2 = cpu.Get(kR1);
        std::uint32_t i = 0;
        for (;;) {
          const std::uint8_t ca = mem_.Ler8(a2 + i), cb2 = mem_.Ler8(b2 + i);
          if (ca != cb2 || ca == 0 || cb2 == 0) {
            cpu.Set(kR0, static_cast<std::uint32_t>(static_cast<std::int32_t>(ca) -
                                                    static_cast<std::int32_t>(cb2)));
            break;
          }
          ++i;
        }
      } else if (idx == kSlotIdStrchr) {
        const std::uint8_t c2 = static_cast<std::uint8_t>(cpu.Get(kR1));
        std::uint32_t i = 0, achou = 0;
        for (;;) {
          const std::uint8_t b = mem_.Ler8(r0 + i);
          if (b == c2) { achou = r0 + i; break; }
          if (b == 0) break;
          ++i;
        }
        cpu.Set(kR0, achou);
      } else if (idx == kSlotIdMemmove) {
        const std::uint32_t src = cpu.Get(kR1), n = cpu.Get(kR2);
        constexpr std::uint32_t kLimiteDaCopia = 0x04000000u;  // 64 MiB
        if (n > kLimiteDaCopia) {
          char det[160];
          std::snprintf(det, sizeof(det),
                        "n=0x%08x (limite 0x%08x) dest=0x%08x src=0x%08x lr=0x%08x -- copia RECUSADA",
                        n, kLimiteDaCopia, r0, src, lr);
          traco_.RegistarFalta(Area::Brew, "AEEHelperFuncs[0x000] memmove (tamanho absurdo)", det);
          cpu.Set(kR0, r0);
        } else {
        std::vector<std::uint8_t> copia(n);   // copia intermediaria: o C permite sobreposicao
        mem_.LerBloco(src, copia.data(), n);
        mem_.EscreverBloco(r0, copia.data(), n);
        cpu.Set(kR0, r0);
        }
      } else if (idx == kSlotIdStrtowstr) {
        // AECHAR *strtowstr(const char *pszIn, AECHAR *pDest, int nSize).
        // AECHAR e UTF-16; nSize e em CARACTERES, e a funcao termina o destino.
        const std::uint32_t destino = cpu.Get(kR1);
        const std::uint32_t tam = cpu.Get(kR2);
        std::uint32_t i = 0;
        for (; static_cast<int>(i) + 1 < static_cast<int>(tam); ++i) {
          const std::uint8_t c2 = mem_.Ler8(r0 + i);
          mem_.Escrever16(destino + i * 2, c2);
          if (c2 == 0) break;
        }
        if (static_cast<int>(i) + 1 >= static_cast<int>(tam) && destino != 0) {
          mem_.Escrever16(destino + (tam - 1) * 2, 0);
        }
        cpu.Set(kR0, destino);
      } else if (idx == kSlotIdGetAeeVersion) {
        // `uint32 GetAEEVersion(byte *pszFormatted, int nSize, uint16 wFlags)`
        // -- `AEEStdLib.h:115-116`, descrito em `:4908-4954`.
        //
        // DOIS DEFEITOS, os dois medidos contra o cabecalho do SDK:
        //
        // 1. O `r1` era tratado como `uint32 *pVer` e levava um `Escrever32`.
        //    O `r1` e um INTEIRO -- o TAMANHO do buffer. Escrevia-se quatro
        //    bytes no endereco que por acaso fosse igual ao tamanho pedido
        //    (um `nSize` de 16 escrevia em 0x00000010, dentro da imagem do
        //    titulo), e o buffer do `r0` -- o unico que o chamador vai ler --
        //    nunca era tocado. Um jogo que imprima a versao lia o que la
        //    estivesse.
        //
        // 2. O valor era `0x00400002`. A regra esta em `AEEStdLib.h:4948-4954`:
        //    byte alto da palavra alta = versao MAIOR, byte baixo da palavra
        //    alta = menor, byte alto da palavra baixa = sub, byte baixo =
        //    build. `0x00400002` le-se "0.64.0.2", que nao e versao nenhuma.
        //    O Zeebo corre BREW 4.0.2: `0x04000200`. E o valor do zeebx
        //    (`src/machine/helper.rs:695-705`, `AEE_VERSION`) e do zeebulator
        //    (`core/brew/mod_runtime.cpp:747-775`), os dois independentes.
        //
        // `GAV_LATIN1` (0x0001, `AEEStdLib.h:35`) pede a cadeia em BYTES; sem
        // ele e AECHAR (UTF-16). O `nSize` e em BYTES nos dois casos
        // (`:4930`), e por isso o ramo AECHAR divide por dois.
        constexpr std::uint32_t kGavLatin1 = 0x0001u;
        constexpr std::uint32_t kAeeVersao = 0x04000200u;
        static const char kAeeVersaoTexto[] = "4.0.2.0";
        const std::uint32_t buf = r0;
        const std::int32_t tam = static_cast<std::int32_t>(cpu.Get(kR1));
        const std::uint32_t flags = cpu.Get(kR2) & 0xFFFFu;
        const std::size_t letras = sizeof(kAeeVersaoTexto) - 1;
        if (buf != 0 && tam > 0) {
          if ((flags & kGavLatin1) != 0) {
            const std::size_t n = std::min(static_cast<std::size_t>(tam - 1), letras);
            for (std::size_t k = 0; k < n; ++k) {
              mem_.Escrever8(buf + static_cast<std::uint32_t>(k),
                             static_cast<std::uint8_t>(kAeeVersaoTexto[k]));
            }
            mem_.Escrever8(buf + static_cast<std::uint32_t>(n), 0);
          } else if (tam >= 2) {
            const std::size_t cabem = static_cast<std::size_t>(tam) / 2;
            const std::size_t n = std::min(cabem - 1, letras);
            for (std::size_t k = 0; k < n; ++k) {
              mem_.Escrever16(buf + static_cast<std::uint32_t>(k) * 2,
                              static_cast<std::uint16_t>(kAeeVersaoTexto[k]));
            }
            mem_.Escrever16(buf + static_cast<std::uint32_t>(n) * 2, 0);
          }
        }
        cpu.Set(kR0, kAeeVersao);
      } else if (idx == kSlotIdAeeGetRand) {
        // `aee_GetRand` -- gerador DETERMINISTA (principio P4). Um gerador do
        // sistema tornaria duas corridas diferentes, e o emulador deixaria de
        // ser reproduzivel -- que e o que sustenta todas as medicoes.
        static std::uint32_t semente = 0x12345678u;
        semente = semente * 1103515245u + 12345u;
        cpu.Set(kR0, (semente >> 16) & 0x7FFFu);
      } else if (idx == kSlotIdSetColor) {
        // `RGBVAL SetColor(IDisplay *po, AEEClrItem clr, RGBVAL rgb)` -- TRES
        // argumentos, e devolve a cor ANTERIOR do item (`AEEIDisplay.h:232` e
        // :297; a descricao esta em :1297-1330).
        //
        // ESTAVA ERRADO DE TRES MANEIRAS, e as tres juntas escondiam-se:
        //   1. `r1` e o ITEM (`AEEClrItem`, 1..16 -- `AEEIDisplay.h:139-156`), e
        //      nao a cor. Usava-se o NUMERO DO ITEM como cor: um item entre 1 e
        //      16 dava sempre um pixel quase preto.
        //   2. a cor e `r2`, em RGBVAL (`MAKE_RGB` = `r<<8 | g<<16 | b<<24`,
        //      `AEERGBVAL.h:24`), e a tela guarda RGB565. Truncar com `& 0xFFFF`
        //      guardava os bits errados: `RGB_WHITE` (0xFFFFFF00) e
        //      `MAKE_RGB(255,0,0)` (0x0000FF00) davam AMBOS 0xFF00 -- a mesma cor.
        //   3. devolvia-se 0. O SDK devolve a cor anterior, e o idioma do
        //      cabecalho (:134-136) e guardar esse valor para o repor a seguir:
        //      um jogo que o faca repunha PRETO por cima do que tinha.
        //
        // `RGB_NONE` (0xFFFFFFFF, `AEERGBVAL.h:27`) LE sem escrever -- e a forma
        // documentada de perguntar a cor de um item.
        //
        // O zeebx tem os tres pontos certos (`machine/display.rs:18-25`,
        // `video/display.rs:25-36`): foi a comparacao com ele que deu por isto.
        const std::uint32_t item = cpu.Get(kR1);
        const std::uint32_t rgb = cpu.Get(kR2);
        const std::uint32_t anterior = tela_.CorDoItem(item);
        if (rgb != 0xFFFFFFFFu) {
          tela_.DefinirCorDoItem(item, rgb);
          tela_.CorAtual(Tela::RgbvalPara565(rgb));
        }
        cpu.Set(kR0, anterior);
      } else if (idx == kSlotIdSetClipRect) {
        // `void SetClipRect(IDisplay *po, AEERect *prc)` -- prc nulo limpa o clip.
        const std::uint32_t prc = cpu.Get(kR1);
        if (prc == 0) {
          tela_.ClipLimpo();
        } else {
          // AEERect sao 4x int16 (x,y,dx,dy), nao 4x u32: ler u32 punha lixo
          // nas coords e o clip calava o desenho sem sintoma.
          const int x = static_cast<std::int16_t>(mem_.Ler16(prc));
          const int y = static_cast<std::int16_t>(mem_.Ler16(prc + 2));
          const int w = static_cast<std::int16_t>(mem_.Ler16(prc + 4));
          const int h = static_cast<std::int16_t>(mem_.Ler16(prc + 6));
          tela_.Clip(x < 0 ? 0 : static_cast<std::uint32_t>(x),
                      y < 0 ? 0 : static_cast<std::uint32_t>(y),
                      w < 0 ? 0 : static_cast<std::uint32_t>(w),
                      h < 0 ? 0 : static_cast<std::uint32_t>(h));
        }
        cpu.Set(kR0, 0);
      } else if (idx == kSlotIdDrawRect) {
        // `void DrawRect(IDisplay *po, const AEERect *pRect, RGBVAL clrFrame,
        //                RGBVAL clrFill, uint32 dwFlags)`.
        //
        // ASSINATURA CORRIGIDA. Eu tinha escrito a versao do BREW 4.x, em que o
        // r1 era a rect e nao havia cores. Neste SDK o r1 e a RECT, o r2 e a cor
        // do contorno e o r3 a do preenchimento -- e o bit `DW_RECT_DRAW` do
        // dwFlags e que diz se e contorno ou cheio.
        // `pRect` NULO COM `IDF_RECT_FILL` LIMPA O ECRA INTEIRO, e nao "nao faz
        // nada" -- `AEEIDisplay.h:1043-1045`: "If pRect is NULL and dwFlags
        // contains IDF_RECT_FILL, this function clears the entire destination
        // bitmap (or current clip rectangle if set) using clrFill. If pRect is
        // NULL without IDF_RECT_FILL flag, this function treats pRect as an empty
        // rectangle."
        //
        // E O IDIOMA MAIS COMUM DO SDK: `IDisplay_ClearScreen`
        // (`AEEIDisplay.h:380-383`) e exactamente `DrawRect(p, NULL, RGB_NONE,
        // RGB_NONE, IDF_RECT_FILL)`. Com o `if (prc != 0)` a envolver tudo, **toda
        // a limpeza de ecra do corpus era deitada fora em silencio**.
        //
        // MEDIDO no `karnovr.mod` 0xe8ac-0xe8d4: `mov r1,#0` (pRect), `mov r3,#2`
        // -> `str r3,[sp]` (IDF_RECT_FILL), `ldr pc,[r4,#0x14]`.
        const std::uint32_t prc = cpu.Get(kR1);
        // `RGB_NONE` (0xFFFFFFFF, `AEERGBVAL.h:27`) NAO E UMA COR: e "sem cor
        // dada". Truncado para 565 dava BRANCO por acidente da truncagem.
        //
        // O que o SDK diz que se usa nesse caso esta escrito para o `DrawText`
        // (`AEEIDisplay.h:963-965`): "using the CLR_USER_BACKGROUND as the fill
        // color and CLR_USER_FRAME as the frame color". E o mesmo par de itens
        // que o `DrawRect` preenche e contorna, e `CLR_USER_FRAME` e
        // `CLR_USER_LINE` (`AEEIDisplay.h:165`).
        //
        // O `IDisplay_ClearScreen` (`AEEIDisplay.h:380-383`) passa `RGB_NONE` nas
        // DUAS cores -- sem esta regra, limpar o ecra pintava-o de branco.
        //
        // DECLARADO, e nao medido: o valor inicial dos itens e 0 (preto). O que a
        // maquina real poe num item que nunca foi escrito nao esta medido.
        const auto cor_ou_item = [&](std::uint32_t rgb, std::uint32_t item) {
          return rgb == 0xFFFFFFFFu ? tela_.CorDoItem(item) : rgb;
        };
        const std::uint32_t clrframe = cor_ou_item(cpu.Get(kR2), kClrUserLine);
        const std::uint32_t clrfill = cor_ou_item(cpu.Get(kR3), kClrUserBackground);
        const std::uint32_t flags = mem_.Ler32(cpu.Get(kSP) + 0);
        // Os bits do `AEERectFlags` (`AEEIDisplay.h:39-43`): FRAME = contorno (1),
        // FILL = cheio (2).
        const bool contorno = (flags & 0x01u) != 0, cheio = (flags & 0x02u) != 0;
        if (prc == 0) {
          if (cheio) {
            // "or current clip rectangle if set" -- o clip E o rectangulo.
            const std::uint32_t* c = tela_.ClipAtual();
            tela_.CorAtual(Tela::RgbvalPara565(clrfill));
            tela_.Retangulo(c[0], c[1], c[2], c[3], true);
          }
          // Sem FILL, um `pRect` nulo e um rectangulo VAZIO: nada a desenhar.
        } else {
          const int x = static_cast<std::int16_t>(mem_.Ler16(prc));
          const int y = static_cast<std::int16_t>(mem_.Ler16(prc + 2));
          const int w = static_cast<std::int16_t>(mem_.Ler16(prc + 4));
          const int h = static_cast<std::int16_t>(mem_.Ler16(prc + 6));
          if (cheio || contorno) {
            tela_.CorAtual(Tela::RgbvalPara565(cheio ? clrfill : clrframe));
            tela_.Retangulo(x < 0 ? 0 : static_cast<std::uint32_t>(x),
                             y < 0 ? 0 : static_cast<std::uint32_t>(y),
                             w < 0 ? 0 : static_cast<std::uint32_t>(w),
                             h < 0 ? 0 : static_cast<std::uint32_t>(h), cheio);
          }
        }
        cpu.Set(kR0, 0);
      } else if (idx == kSlotIdDrawText) {
        // `int DrawText(IDisplay *po, AEEFont nFont, const AECHAR *pcText,
        //              int nChars, int x, int y, const AEERect *prcBackground,
        //              uint32 dwFlags)`.
        //
        // ASSINATURA CORRIGIDA: os argumentos 5 e 6 sao COORDENADAS, nao uma
        // rect. A minha versao lia o r3 como ponteiro de rect e desenhava a barra
        // no sitio errado -- e um ponteiro de rect interpretado como x daria uma
        // barra numa linha absurda, ou fora do ecra, sem nada a acusar.
        //
        // Nao ha fonte carregada, logo NAO se rasteriza texto: desenha-se uma
        // barra com a cor actual, com a largura declarada de 8 px por caracter.
        // Fica declarado como aproximacao, e a bateria conta `textos`.
        const std::uint32_t nchars = cpu.Get(kR3);
        const std::uint32_t x = mem_.Ler32(cpu.Get(kSP) + 0);
        const std::uint32_t y = mem_.Ler32(cpu.Get(kSP) + 4);
        const std::uint32_t prcfundo = mem_.Ler32(cpu.Get(kSP) + 8);
        if (prcfundo != 0) {
          // O fundo e pedido explicitamente: pinta-se com a cor actual antes.
          // AEERect = 4x int16 (ver SetClipRect/DrawRect).
          const int fx = static_cast<std::int16_t>(mem_.Ler16(prcfundo));
          const int fy = static_cast<std::int16_t>(mem_.Ler16(prcfundo + 2));
          const int fw = static_cast<std::int16_t>(mem_.Ler16(prcfundo + 4));
          const int fh = static_cast<std::int16_t>(mem_.Ler16(prcfundo + 6));
          tela_.Retangulo(fx < 0 ? 0 : static_cast<std::uint32_t>(fx),
                           fy < 0 ? 0 : static_cast<std::uint32_t>(fy),
                           fw < 0 ? 0 : static_cast<std::uint32_t>(fw),
                           fh < 0 ? 0 : static_cast<std::uint32_t>(fh), true);
        }
        // `nChars == -1` QUER DIZER "conta tu", e nao "4.294.967.295 caracteres":
        // `AEEIDisplay.h:939-940` -- "If this is -1, the length will be
        // automatically computed by this function".
        //
        // O QUE ESTAVA AQUI ERA PIOR DO QUE UM COMPRIMENTO ERRADO: `nchars` e
        // `uint32`, logo `0xFFFFFFFF * 8` dava `0xFFFFFFF8` e o laco corria QUATRO
        // MIL MILHOES de vezes por chamada -- medido, ~4 s de uma corrida de 9 s
        // num so `DrawText` do `karnovr` -- para escrever 640 pixels (a largura do
        // ecra), que era exactamente a medida "640 pixels" dos 10 titulos da
        // familia `emulator_neo`. E a mesma licao do `Tela::Retangulo`
        // (`core/brew/tela.h`): limitar ANTES de percorrer.
        std::uint32_t quantos = nchars;
        if (static_cast<std::int32_t>(nchars) < 0) {
          quantos = 0;
          const std::uint32_t ptexto = cpu.Get(kR2);
          // AECHAR e uint16 (`AEE.h`), terminado em 0. O limite e a largura do
          // ecra em caracteres: mais do que isso nao cabe na `Tela` de qualquer
          // maneira, e um ponteiro invalido nao pode custar um laco sem fim.
          const std::uint32_t kMaximo = Tela::kLargura / kLarguraDoCaractere + 1;
          if (ptexto != 0) {
            while (quantos < kMaximo && mem_.Ler16(ptexto + quantos * 2) != 0) ++quantos;
          }
        }
        const std::uint32_t larg = (quantos > 0 ? quantos : 1) * kLarguraDoCaractere;
        for (std::uint32_t i = 0; i < larg; ++i) tela_.Ponto(static_cast<int>(x + i), static_cast<int>(y));
        ++textos_;
        cpu.Set(kR0, static_cast<std::uint32_t>(larg));
      } else if (idx == kSlotIdBitBlt) {
        // `void BitBlt(IDisplay *po, int xDest, int yDest, int cxDest, int cyDest,
        //              const void *pbmSource, int xSrc, int ySrc, AEERasterOp dwRopCode)`.
        //
        // ASSINATURA CORRIGIDA, e a correccao e grande: a origem NAO e um
        // `IBitmap` com cabecalho -- e um bloco CRU de pixels, sem largura nem
        // altura. Quem sabe as dimensoes e quem chamou. A largura da origem vem
        // do proprio passo: assume-se que a origem tem a largura pedida
        // (`cxDest`), que e a convencao do BREW para blits sem escalonamento.
        const std::int32_t xd = static_cast<std::int32_t>(cpu.Get(kR1));
        const std::int32_t yd = static_cast<std::int32_t>(cpu.Get(kR2));
        const std::int32_t cx = static_cast<std::int32_t>(cpu.Get(kR3));
        const std::int32_t cy = static_cast<std::int32_t>(mem_.Ler32(cpu.Get(kSP) + 0));
        const std::uint32_t origem = mem_.Ler32(cpu.Get(kSP) + 4);
        const std::int32_t xs = static_cast<std::int32_t>(mem_.Ler32(cpu.Get(kSP) + 8));
        const std::int32_t ys = static_cast<std::int32_t>(mem_.Ler32(cpu.Get(kSP) + 12));
        // O `pbmSource` E UM IDIB*, E NAO UM BLOCO CRU DE PIXELS.
        //
        // MEDIDO (frente fora, `toyraidzeebo`): o guest passa `0x80050340` e
        // `0x80050380` -- enderecos DENTRO da banda dos IDIB (`kObjDibBase`) --
        // com `cx=195 cy=203` e `xs=ys=0`. Lendo os pixels a partir do
        // ENDERECO DO OBJECTO, o laco (39 585 pixels, 79 170 bytes) atravessa o
        // fim da pagina dos IDIB e entra na pagina SEGUINTE, que nao esta
        // mapeada: 1041 leituras nao mapeadas em `0x80051000` (a fronteira
        // `kObjDibBase + 0x1000`), com o PC do guest `0x319c` -- e 1041 dos 1066
        // pedidos da corrida de referencia de `/tmp/corrida_memcmp.json`.
        //
        // O `pBmp`/`cx`/`nPitch` do cabecalho PUBLICO do IDIB (`AEEIDIB.h`) sao
        // o que diz onde os pixels estao; a origem de um `BitBlt` de um IDIB e o
        // buffer, nao o cabecalho. Um ponteiro FORA da banda (um bloco cru, a
        // convencao que este ramo seguia) continua a ser lido como pixels.
        using C = zb2::brew::CamposDoIdib;
        std::uint32_t fonte = origem;
        std::uint32_t passo = static_cast<std::uint32_t>(cx) * 2u;
        if (EUmObjectoDeBitmap(origem)) {
          const std::uint32_t pbmp = mem_.Ler32(origem + C::kPBmp);
          const std::uint32_t n_pitch =
              static_cast<std::uint16_t>(mem_.Ler16(origem + C::kNPitch));
          if (pbmp != 0 && n_pitch != 0) {
            fonte = pbmp;
            passo = n_pitch;
          }
        }
        if (origem != 0 && cx > 0 && cy > 0) {
          for (std::int32_t j = 0; j < cy; ++j) {
            for (std::int32_t i = 0; i < cx; ++i) {
              const std::uint32_t u = static_cast<std::uint32_t>(xs + i);
              const std::uint32_t v = static_cast<std::uint32_t>(ys + j);
              tela_.CorAtual(mem_.Ler16(fonte + v * passo + u * 2u));
              tela_.Ponto(xd + i, yd + j);
            }
          }
          ++blits_;
        }
        cpu.Set(kR0, 0);
      } else if (idx == kSlotIdCreateDIBitmap) {
        // `int CreateDIBitmap(IDisplay *po, IDIB **ppIDIB, uint8 colorDepth,
        //                     uint16 w, uint16 h)`.
        //
        // ASSINATURA CORRIGIDA. O ponteiro de saida e o SEGUNDO argumento, e o
        // que se devolve no r0 e um codigo (0 = SUCCESS). A minha versao
        // devolvia o objecto no r0 e ignorava o `ppIDIB` -- o chamador ficava com
        // o ponteiro por preencher e o objecto perdido.
        //
        // DUAS MENTIRAS AQUI, e as duas em silencio:
        //
        // 1. Devolvia-se `AEE_SUCCESS` com o `pBmp` a ZERO. O jogo recebia um
        //    IDIB valido cujo buffer de pixels e o endereco 0: escreve la, e o
        //    que se estraga e a pagina zero -- que nesta arvore e a BASE DO
        //    MODULO do titulo (`tests/mod_base_test.cpp`). Sucesso a apontar
        //    para o codigo do proprio jogo.
        // 2. O cabecalho era inventado (ver `CamposDoIdib`): a struct do IDIB e
        //    PUBLICA e o jogo le `cx`/`cy`/`nPitch` directamente.
        //
        // Agora aloca-se mesmo, no heap do guest, e o `nPitch` diz a verdade. Se
        // nao houver memoria, RECUSA COM NOME -- a regra da casa (P2), e a mesma
        // que o zeebx aplica ao formato que nao sabe tratar
        // (`src/machine/bitmap.rs:126-131`).
        const std::uint32_t ppidib = cpu.Get(kR1);
        const std::uint32_t prof = cpu.Get(kR2) & 0xFFu;
        const std::uint32_t w = cpu.Get(kR3) & 0xFFFFu;
        const std::uint32_t h = mem_.Ler32(cpu.Get(kSP) + 0) & 0xFFFFu;
        if (ppidib != 0) mem_.Escrever32(ppidib, 0);
        if (prof != 16 || w == 0 || h == 0) {
          // A PROFUNDIDADE QUE NAO SEJA 16 NAO TEM CAMINHO: o `BitBlt` deste
          // despacho le a origem com `Ler16` (RGB565) sem olhar ao `colorDepth`
          // (ver o ramo do `kSlotIdBitBlt`). Fingir que se criou um DIB de 8
          // bits daria um blit de lixo mais tarde e noutro sitio.
          char det[96];
          std::snprintf(det, sizeof(det), "colorDepth=%u %ux%u -- so ha caminho para RGB565",
                        static_cast<unsigned>(prof), static_cast<unsigned>(w),
                        static_cast<unsigned>(h));
          traco_.RegistarFalta(Area::Brew, "IDisplay::CreateDIBitmap", det);
          cpu.Set(kR0, kAeeUnsupported);
        } else {
          const std::uint32_t passo = w * 2;
          const std::uint32_t bytes = passo * h;
          const std::uint32_t pixels = al_.Malloc(bytes);
          if (pixels == 0) {
            char det[96];
            std::snprintf(det, sizeof(det), "sem heap para %ux%u (%u bytes)",
                          static_cast<unsigned>(w), static_cast<unsigned>(h),
                          static_cast<unsigned>(bytes));
            traco_.RegistarFalta(Area::Brew, "IDisplay::CreateDIBitmap", det);
            cpu.Set(kR0, kAeeNoMemory);
          } else {
            // Buffer a ZEROS: um DIB novo com lixo dentro faria duas corridas
            // iguais desenharem coisas diferentes (P4, determinismo).
            const std::vector<std::uint8_t> zeros(bytes, 0);
            mem_.EscreverBloco(pixels, zeros.data(), bytes);
            const std::uint32_t obj = zb2::brew::kObjDibBase + dibs_ * 0x40;
            ++dibs_;
            EscreverCabecalhoDeIdib(obj, pixels, w, h);
            if (ppidib != 0) mem_.Escrever32(ppidib, obj);
            cpu.Set(kR0, 0);
          }
        }
            } else if (idx == kSlotIdGetFontMetrics) {
        // `int GetFontMetrics(IDisplay *po, AEEFont nFont, int *pnAscent,
        //                     int *pnDescent)`.
        //
        // ASSINATURA CORRIGIDA: o r1 e a FONTE e o r2/r3 sao os dois ponteiros de
        // saida. Eu tinha escrito a versao do BREW 4.x, com uma struct de
        // metricas no r1 -- que aqui seria lido como um `AEEFont` e a escrita
        // ia para o sitio errado. **Este era um defeito silencioso de verdade.**
        const std::uint32_t pascent = cpu.Get(kR2), pdescent = cpu.Get(kR3);
        const int asc = -10, desc = 2;  // fonte de 12 px, valores DECLARADOS
        if (pascent != 0) mem_.Escrever32(pascent, static_cast<std::uint32_t>(asc));
        if (pdescent != 0) mem_.Escrever32(pdescent, static_cast<std::uint32_t>(desc));
        cpu.Set(kR0, 12);
      } else if (idx == kSlotIdMeasureText) {
        // `int MeasureTextEx(IDisplay *po, AEEFont nFont, const AECHAR *pcText,
        //                    int nChars, int nMaxWidth, int *pnFits)`.
        //
        // ASSINATURA CORRIGIDA: `pnFits` e o 6.o argumento, na pilha, e recebe a
        // largura que CABE. Largura DECLARADA de 8 px por caracter.
        const std::uint32_t n = cpu.Get(kR3);
        const std::uint32_t nmax = mem_.Ler32(cpu.Get(kSP) + 0);
        const std::uint32_t pfits = mem_.Ler32(cpu.Get(kSP) + 4);
        const std::uint32_t larg = (n > 0 ? n : 1) * 8;
        if (pfits != 0) {
          mem_.Escrever32(pfits, nmax == 0 ? larg : (larg < nmax ? larg : nmax));
        }
        cpu.Set(kR0, static_cast<std::uint32_t>(larg));
      } else if (idx == kSlotIdUpdate) {
        // `void Update(IDisplay *po)`: e ESTE o instante em que o BREW mostra o
        // que foi desenhado. E por isso e aqui que os pixels escritos
        // directamente no `pBmp` entram na Tela.
        ++updates_;
        AbsorverEcraDoGuest();
        cpu.Set(kR0, 0);
      } else if (idx == kSlotIdBacklight) {
        // `void Backlight(IDisplay *po, boolean bOn)`. Sem ecra fisico: conta.
        ++backlights_;
        cpu.Set(kR0, 0);
      } else if (idx == kSlotIdSetTimer) {
        // `int SetTimer(IShell *po, int32 dwMsecs, void (*pfn)(void *), void *pUser)`
        //
        // ASSINATURA CORRIGIDA, e a correccao tira o laco de quadro de todos os
        // titulos: o r1 e a DURACAO e o r2 e a FUNCAO. A versao anterior lia
        // `r1` como ponteiro de `AEECallback` e `r2` como milissegundos.
        //
        // A prova sao DUAS fontes independentes, como o projecto exige:
        //   1. o cabecalho, `platform/system/inc/AEEIShell.h:299`
        //      (`INHERIT_IShell`): `int (*SetTimer)(iname *po, int32 dwMsecs,
        //      void (*pfn)(void *), void * pUser)` -- e o inline, linha 403;
        //   2. o codigo do guest, `mod/280214/asq.mod` em `0x8cf34`:
        //      `mov r1,#100` (100 ms) e `ldr r2,[pc,#572]` (= `*0x8d18c` =
        //      `0x8c3e8`, um endereco de CODIGO) com `mov r3,r5` (o contexto).
        //      Um ponteiro de funcao lido de um literal, no r2 -- nao um periodo.
        //
        // CONSEQUENCIA MEDIDA da versao errada: `PrepararCallbackDoTemporizador`
        // fazia `Ler32(100)` e recusava ("fora do modulo"). NENHUM titulo do
        // corpus armava um temporizador, e sem laco de quadro nenhum chega ao
        // codigo que desenha. E o pedido de demanda mais alto do IShell.
        timer_.ativo = true;
        timer_.pfn = cpu.Get(kR2);
        timer_.puser = cpu.Get(kR3);
        timer_.vence_em_ms = agora_ms_ + static_cast<std::int64_t>(cpu.Get(kR1));
        traco_.Emitir(Area::Guarda, Nivel::Depuracao, "SET_TIMER",
                     "pfn=" + Hex(timer_.pfn) + " puser=" + Hex(timer_.puser) + " em " +
                         std::to_string(cpu.Get(kR1)) + " ms");
        cpu.Set(kR0, kAeeSuccess);
      } else if (idx == kSlotIdGetUpTime) {
        cpu.Set(kR0, static_cast<std::uint32_t>(agora_ms_));
      } else if (idx == kSlotIdGetDest) {
        // `IBitmap *GetDestination(IDisplay *po)` -- IDisplay slot 16.
        // Devolve o bitmap que esta a receber o desenho. O jogo usa-o para saber
        // o TAMANHO da tela (via IBitmap::GetInfo) antes de calcular posicoes.
        const std::uint32_t obj = EscreverCabecalhoDoBitmapDoEcra();
        destino_ = obj;
        cpu.Set(kR0, obj);
      } else if (idx == kSlotIdSetDest) {
        // `int SetDestination(IDisplay *po, IBitmap *pDst)` -- IDisplay slot 15.
        // So se ACEITA um bitmap nosso: aceitar um ponteiro qualquer poria o
        // desenho num sitio que nao existe.
        const std::uint32_t pdst = cpu.Get(kR1);
        if (pdst >= zb2::brew::kObjDibBase && pdst < zb2::brew::kObjDibBase + 0x1000) {
          destino_ = pdst;
          cpu.Set(kR0, 0);  // SUCCESS
        } else {
          cpu.Set(kR0, kAeeUnsupported);
        }
      } else if (idx == kSlotIdDisplaySetFont) {
        // `IFont *SetFont(IDisplay *po, AEEFont nFont, IFont *piFont)` --
        // IDisplay slot 17. Guarda a anterior e devolve-a. So se ACEITA uma
        // fonte nossa (os 3 objectos) ou o zero; outro ponteiro poria na
        // medida texto que nao existe.
        const std::uint32_t pif = cpu.Get(kR2);
        if (pif != 0 && pif != zb2::brew::kObjetoFonte11 &&
            pif != zb2::brew::kObjetoFonte15 && pif != zb2::brew::kObjetoFonte36) {
          traco_.RegistarFalta(Area::Brew, "IDisplay::SetFont",
                               "fonte 0x" + Hex(pif) + " nao e objecto desta arvore");
          cpu.Set(kR0, kAeeUnsupported);
        } else {
          const std::uint32_t anterior = fonte_do_display_;
          fonte_do_display_ = pif;
          cpu.Set(kR0, anterior);
        }
      } else if (idx == kSlotIdBitmapGetInfo) {
        // `int GetInfo(IBitmap *po, AEEBitmapInfo *pinfo, int nSize)` -- IBitmap
        // slot 12 (`AEEIBitmap.h:42-58`: `INHERIT_IQI` gasta 0,1,2 e o `GetInfo`
        // e o decimo terceiro campo do macro).
        //
        // `AEEBitmapInfo` sao TRES uint32 (`AEEIBitmap.h:34-38`): cx, cy, nDepth.
        // O guest DIZ quantos bytes conhece no `nSize`, e escreve-se so esses --
        // e o contrato do proprio cabecalho (`AEEIBitmap.h:764`: "The size of
        // AEEBitmapInfo in the current version").
        //
        // PORQUE E QUE ISTO ESTAVA A FALTAR CUSTA PIXELS: o `karnovr` (e os outros
        // nove da familia `emulator_neo`) le daqui o TAMANHO DO ECRA antes de
        // montar a superficie GL -- `karnovr.mod` 0xfda4 `GetDeviceBitmap`, 0xfdd4
        // `ldr pc,[r3,#0x30]` (slot 12) com `r2 = 0xc`, e a seguir 0xfddc/0xfde0
        // `ldr r3,[sp]` / `ldr r2,[sp,#4]` -> guarda em `[r5,#0x38]` e `[r5,#0x3c]`.
        // Sem resposta, o titulo fica com o lixo da pilha a fazer de largura e
        // altura do ecra.
        const std::uint32_t pinfo = cpu.Get(kR1);
        const std::uint32_t nsize = cpu.Get(kR2);
        // DE QUE BITMAP E QUE O GUEST PERGUNTA? Nao e sempre o ecra.
        //
        // Este ramo responde pelo `idx` que a FERRAMENTA cabla (`kWire`:
        // `{kVtableBitmap, 12, kSlotIdBitmapGetInfo}`), e por isso a pergunta
        // chega aqui com QUALQUER bitmap no r0 -- e o `CreateCompatibleBitmap`
        // passou a existir (frente ibmap2), logo o r0 pode ser um DIB NOVO, com
        // as dimensoes DELE. Responder 640x480 a quem pergunta pelo DIB que
        // acabou de criar e o mesmo defeito que esta frente veio corrigir, um
        // nivel acima.
        //
        // MEDIDO no `pacmania` (lr=0x12d94): `CreateCompatibleBitmap` ->
        // `QI(AEEIID_IDIB)` -> `GetInfo(nSize=12)`, e o jogo guarda os dois
        // numeros para escrever no `pBmp`.
        //
        // O ECRA continua a responder com o tamanho da `Tela`: o objecto do
        // ecra e o unico cujo cabecalho publico pode NAO estar escrito ainda
        // (so ganha pagina quando alguem o pede), e o tamanho dele E o da Tela.
        const std::uint32_t po = cpu.Get(kR0);
        const bool e_o_ecra = (po == zb2::brew::kObjDibBase + 0x300);
        using C = zb2::brew::CamposDoIdib;
        const bool e_outro_dib = !e_o_ecra && EUmObjectoDeBitmap(po);
        if (pinfo == 0) {
          cpu.Set(kR0, kAeeBadParm);
        } else {
          std::uint32_t campos[3] = {static_cast<std::uint32_t>(zb2::brew::Tela::kLargura),
                                     static_cast<std::uint32_t>(zb2::brew::Tela::kAltura),
                                     16u};  // RGB565, o pixel da `Tela`
          if (e_outro_dib) {
            campos[0] = mem_.Ler16(po + C::kCx);
            campos[1] = mem_.Ler16(po + C::kCy);
            const std::uint32_t prof = mem_.Ler8(po + C::kNDepth);
            campos[2] = prof > 0 ? prof : 16u;
          }
          for (std::uint32_t k2 = 0; k2 < 3 && (k2 + 1) * 4 <= nsize; ++k2) {
            mem_.Escrever32(pinfo + k2 * 4, campos[k2]);
          }
          cpu.Set(kR0, kAeeSuccess);
        }
      } else if (idx == kSlotIdBitmapQI) {
        // `int QueryInterface(IBitmap*, AEEIID, void**)` no bitmap do ecra.
        // Regra COM: pedir uma interface que o objeto JA implementa devolve o
        // proprio objeto (o motor pede IID_DIB no IDIB: cluster WERV).
        const std::uint32_t iid = cpu.Get(kR1);
        const std::uint32_t ppo = cpu.Get(kR2);
        if (ppo == 0) {
          cpu.Set(kR0, kAeeBadParm);
        } else if (zb2::brew::IidDeDib(iid)) {
          // A LISTA DOS IIDs DE DIB E UMA SO (`zb2::brew::IidDeDib`), e nao
          // duas: o servidor da familia responde ao mesmo pedido quando a
          // vtable nao tem este endereco cablado (`ConstruirVtableDoBitmap`
          // poe `8002` no slot 2; a ferramenta poe este). Com a lista partida,
          // o mesmo `QI(0x0100102c)` respondia de duas maneiras diferentes
          // conforme quem cablou a vtable.
          // O IDIB e a MESMA struct (`AEEIDIB.h:57-60`, `IDIB_to_IBitmap` e um
          // cast): quem pede IID_DIB vai LER os campos publicos, logo o
          // cabecalho tem de estar escrito antes de o ponteiro sair daqui.
          //
          // O PONTEIRO QUE SAI E O DO OBJECT CODE O GUEST PERGUNTOU, e nao o do
          // ecra: era isso que este ramo fazia, e publicar o ecra a quem pediu o
          // DIB que acabou de criar poe o desenho do titulo no ecra por engano
          // (ou o contrario: o titulo escreve no ecra a pensar que escreve no
          // bitmap dele). Para os DIBs criados (`CreateDIBitmap`, e o
          // `CreateCompatibleBitmap` do slot13) o cabecalho ja foi escrito na
          // criacao.
          const std::uint32_t po = cpu.Get(kR0);
          const bool e_o_ecra = (po == zb2::brew::kObjDibBase + 0x300);
          const std::uint32_t obj =
              e_o_ecra ? EscreverCabecalhoDoBitmapDoEcra()
                       : (EUmObjectoDeBitmap(po) ? po : 0u);
          if (obj == 0) {
            // Um ponteiro fora da faixa dos bitmaps nao tem IDIB para publicar:
            // RECUSA COM NOME, em vez de publicar o ecra (que e o que este ramo
            // fazia, e mentia sobre o objecto).
            mem_.Escrever32(ppo, 0);
            char det[64];
            std::snprintf(det, sizeof(det), "po=0x%08x nao e um bitmap da maquina", po);
            traco_.RegistarFalta(Area::Brew, "IBitmap::QueryInterface", det);
            cpu.Set(kR0, kAeeUnsupported);
          } else {
            mem_.Escrever32(ppo, obj);
            cpu.Set(kR0, kAeeSuccess);
            traco_.Emitir(Area::Brew, Nivel::Depuracao, "IBITMAP_QUERYINTERFACE",
                          e_o_ecra ? "IID_DIB -> o bitmap do ecra"
                                   : "IID_DIB -> o proprio objeto");
          }
        } else {
          mem_.Escrever32(ppo, 0);
          char det[64];
          std::snprintf(det, sizeof(det), "iid=0x%08x", iid);
          traco_.RegistarFalta(Area::Brew, "IBitmap::QueryInterface", det);
          cpu.Set(kR0, kAeeUnsupported);
        }
      } else if (idx == kSlotIdGetDeviceBitmap) {
        // `int GetDeviceBitmap(IDisplay *po, IBitmap **ppIBitmap)` -- IDisplay
        // slot 16. O jogo quer o bitmap do ECRA para desenhar por cima dele.
        // E o mesmo objecto que o `GetDestination` devolve.
        const std::uint32_t pp = cpu.Get(kR1);
        if (pp != 0) {
          // O header do objeto, igual ao GetDestination: sem ele o QI do motor
          // lia vtable de lixo (cluster WERV: [[0x80050300]+8] = "BREW").
          mem_.Escrever32(pp, EscreverCabecalhoDoBitmapDoEcra());
          cpu.Set(kR0, 0);  // SUCCESS
        } else {
          cpu.Set(kR0, kAeeUnsupported);
        }
      } else if (idx == kSlotIdGetClipRect) {
        // `void GetClipRect(IDisplay *po, AEERect *pRect)` -- IDisplay slot 19.
        // Devolve o clip ACTUAL, que o `SetClipRect` guardou.
        const std::uint32_t prc = cpu.Get(kR1);
        if (prc != 0) {
          // AEERect sao 4x int16 = OITO bytes (`AEERect.h:21-24`), e nao 4x u32.
          //
          // Escrevia-se DEZASSEIS: os oito a mais caiam por cima do que estivesse
          // a seguir ao `AEERect` -- e o `AEERect` do guest esta quase sempre na
          // PILHA, logo por cima das suas proprias variaveis locais. Corrupcao
          // silenciosa: nada falha aqui, falha mais tarde e noutro sitio.
          //
          // O `SetClipRect`, oito linhas acima neste mesmo ficheiro, JA lia int16
          // desde que isso foi medido. Ficou a metade do par por corrigir.
          const std::uint32_t* c = tela_.ClipAtual();
          for (int k = 0; k < 4; ++k) {
            const std::int32_t v = static_cast<std::int32_t>(c[k]);
            const std::int16_t cortado = static_cast<std::int16_t>(
                v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
            mem_.Escrever16(prc + static_cast<std::uint32_t>(k * 2),
                            static_cast<std::uint16_t>(cortado));
          }
        }
        cpu.Set(kR0, 0);
      } else if (idx == kSlotIdCancelTimer) {
        // `int CancelTimer(IShell *po, void (*pfn)(void *), void *pUser)` --
        // IShell slot 12 (`AEEIShell.h:300`, e o inline na linha 408). Os campos
        // veem SEPARADOS aqui tambem, pela mesma razao do `SetTimer`.
        //
        // So se desarma se for o MESMO callback: cancelar um temporizador alheio
        // pararia o laco de quadro de outra coisa.
        if (timer_.ativo && timer_.pfn == cpu.Get(kR1)) timer_.ativo = false;
        cpu.Set(kR0, kAeeSuccess);
      } else if (idx == kSlotIdRealloc) {
        // `void *realloc(void *pSrc, uint32 dwSize)` -- o alocador do guest, que ja
        // tinha o `Realloc` escrito e testado. Ate agora um jogo que o chamasse
        // recebia `EUNSUPPORTED` e **ficava com o ponteiro antigo sem saber**.
        //
        // A BANDEIRA VALE AQUI TAMBEM (`ALLOC_NO_ZMEM`, `AEEStdLib.h:547`): um
        // `realloc(p, n | 0x80000000)` pedia 2 GiB a este ramo e recebia zero -- o
        // mesmo defeito do `malloc`, no vizinho. E a parte NOVA cresce com a mesma
        // regra (zerada, salvo com a bandeira), que e a leitura consistente do
        // contrato -- e esta escrita aqui para ser discutida se aparecer a fonte
        // que fixe o contrario.
        const std::uint32_t pedido_realloc = cpu.Get(kR1);
        cpu.Set(kR0, al_.Realloc(cpu.Get(kR0), pedido_realloc & ~kAllocNoZmem,
                                 (pedido_realloc & kAllocNoZmem) == 0));
      } else if (idx == kSlotIdHeapLock || idx == kSlotIdHeapLock + 0) {
        // `int Lock(IHeap1 *po)` -- IHeap1 slot 7. Bloqueia o heap para uso
        // exclusivo.
        //
        // NAO ha nada a bloquear: o principio P6 do desenho e UM ESCRITOR para a
        // memoria do guest, e nao ha threads de subsistema. Devolver sucesso e a
        // resposta CORRECTA, e nao um stub: o contrato e "a partir daqui es o
        // unico a mexer", e isso ja e verdade.
        cpu.Set(kR0, 0);
      } else if (idx == kSlotIdCheckPriv) {
        // `boolean CheckPrivLevel(IShell *po, AEECLSID clsIDWant,
        //                         boolean bQueryOnly)` -- IShell slot 39,
        // `AEEIShell.h:327`. TRES argumentos: o comentario antigo dizia
        // `uint32 dwPriv` e dois, e o `bQueryOnly` nem era lido.
        //
        // Respondia-se TRUE A TUDO, sem sequer ler o argumento. As regras estao
        // escritas no cabecalho (`AEEIShell.h:4355-4392`) e sao estas:
        //   - "Every application is a member of the group 0" -> cls 0 e TRUE;
        //   - "Every application is a member of its own group: the group that
        //     is equal to the application's class ID" -> o proprio titulo;
        //   - "If the high-order word of clsIDWant is 0, the value is treated
        //     as a bit-mask of the old-style privilege bits" (PL_FILE 0x0001,
        //     PL_NETWORK 0x0002, ... PL_SYSTEM 0xffff -- `AEEPLPrivs.bid:9-19`);
        //   - caso contrario e um AEECLSID, e a pergunta e "pertenco ao grupo
        //     dessa classe", que aqui e "sei criar essa classe".
        //
        // O QUE CONCEDEMOS, e porque: `PL_FILE` (0x0001) -- ha IFileMgr e ha
        // ficheiros, ainda que so de leitura. Tudo o resto (rede, TAPI, web,
        // download, agenda, localizacao, e o `PL_SYSTEM` que e a soma de todos)
        // nao existe neste emulador, e dizer que sim seria prometer o que nao
        // ha.
        const std::uint32_t cls_pedida = cpu.Get(kR1);
        constexpr std::uint32_t kPlFile = 0x0001u;  // AEEPLPrivs.bid:9
        bool tem = false;
        const char* porque = "";
        if (cls_pedida == 0) {
          tem = true;  // grupo 0: todos pertencem
          porque = "grupo 0";
        } else if ((cls_pedida >> 16) == 0) {
          // Mascara antiga: so passa se TODOS os bits pedidos forem concedidos.
          tem = (cls_pedida & ~kPlFile) == 0;
          porque = "mascara PL_*";
        } else if (tem_clsid_ && cls_pedida == clsid_titulo_) {
          tem = true;
          porque = "o proprio titulo";
        } else {
          // "Adding a class ID to the module's Dependencies adds the module to
          // the group denoted by that class ID" (`AEEIShell.h:4385-4386`). Aqui
          // a pergunta responde-se com a MESMA lista do `QueryClass`: pertenco
          // ao grupo das classes que sei servir.
          tem = ClasseConhecida(cls_pedida);
          porque = "classe servida pelo CreateInstance";
        }
        if (!tem) {
          char det[96];
          std::snprintf(det, sizeof(det), "cls=0x%08x (%s) bQueryOnly=%u",
                        cls_pedida, porque, static_cast<unsigned>(cpu.Get(kR2)));
          traco_.RegistarFalta(Area::Brew, "IShell::CheckPrivLevel", det);
        }
        cpu.Set(kR0, tem ? 1u : 0u);
      } else if (idx == kSlotIdSprintf || idx == kSlotIdVsprintf || idx == kSlotIdVsnprintf) {
        // `int sprintf(char *pBuf, const char *pFmt, ...)` -- AEEHelperFuncs
        // 0x020; `int vsprintf(char*, const char*, va_list)` -- 0x13c.
        //
        // VARARGS no AAPCS: os quatro primeiros argumentos vao em r0..r3 e o
        // resto na PILHA a partir do `sp`. O `vsprintf` recebe um PONTEIRO para os
        // argumentos; o `sprintf` recebe-os soltos. O trabalho esta em `Formato`,
        // que tem testes contra o `snprintf` DO SISTEMA.
        std::uint32_t args[8];
        int n = 0;
        if (idx == kSlotIdVsprintf || idx == kSlotIdVsnprintf) {
          // A LISTA, e nao os argumentos: `AEEOldVaList` e `int**`
          // (`AEEOldVaList.h:37`), logo o registador traz o endereco da VARIAVEL
          // `va_list`. E o registador do `vsnprintf` e o **r3**: no `vsnprintf`
          // o r2 e o FORMATO.
          //
          // MEDIDO no `alice` (esta frente): ler a lista no r2 fazia os
          // argumentos sairem dos BYTES DO PROPRIO FORMATO -- o `%s` de
          // "  %d @ %s" recebia `mem.Ler32(formato + 4)` = 0x25204020, os ASCII
          // " @ %" lidos como endereco, e o `Formatar` ia ler uma cadeia em
          // 0x25204020 (20 recusas do hospedeiro, pc 0x3f04c). O desmonte e os
          // numeros estao em `core/brew/formato.h`.
          const std::uint32_t plista = (idx == kSlotIdVsnprintf) ? cpu.Get(kR3) : cpu.Get(kR2);
          ArgumentosDoVaLists(mem_, plista, args, 8);
          n = 8;
        } else {
          args[n++] = cpu.Get(kR2);
          args[n++] = cpu.Get(kR3);
          for (int i = 0; i < 6; ++i) {
            args[n++] = mem_.Ler32(cpu.Get(kSP) + static_cast<std::uint32_t>(i) * 4);
          }
        }
        // `vsnprintf(char *buf, uint32 f, const char *format, AEEOldVaList list)`: o
        // r1 e o TAMANHO, e o formato esta no r2. O `sprintf`/`vsprintf` nao tem esse
        // argumento -- e o `Formatar` tem de receber o limite, senao um `%s` comprido
        // transborda o buffer do jogo.
        const std::uint32_t pbuf = cpu.Get(kR0);
        const bool com_limite = (idx == kSlotIdVsnprintf);
        const std::uint32_t pfmt = com_limite ? cpu.Get(kR2) : cpu.Get(kR1);
        const std::uint32_t limite = com_limite ? cpu.Get(kR1) : 0;
        if (pbuf == 0 || pfmt == 0) {
          cpu.Set(kR0, 0);
        } else {
          cpu.Set(kR0, Formatar(mem_, pbuf, pfmt, args, n, limite));
        }
      } else if (idx == kSlotIdOpenFile) {
        // `IFile *OpenFile(IFileMgr *po, const char *pszFile, OpenFileMode mode)`
        // -- IFileMgr slot 2. O trabalho esta em `Arquivos`, que TEM TESTES.
        //
        // Isto era uma reimplementacao propria dentro da ferramenta: a mesma
        // regra escrita duas vezes, e so uma delas testada. E o defeito que a
        // migracao existe para corrigir.
        const std::string nome = LerTextoDe(mem_, cpu.Get(kR1), 512);
        const std::uint32_t id = arquivos_.Abrir(nome, cpu.Get(kR2), dir_ + "/" + pasta_);
        if (id == 0) {
          // A RECUSA FICA DITA. Este ramo nao escrevia nada no traco, e o
          // efeito medido foi um instrumento cego: 10 titulos andaram 742 mil
          // passos no arranque sem que se pudesse ver QUE ficheiro pediam, nem
          // que o pedido tinha sido recusado. Com o nome e o motivo, a pergunta
          // "o que e que falta a este jogo?" passa a ter resposta no log.
          traco_.Emitir(Area::Brew, Nivel::Depuracao, "OPENFILE_RECUSADO",
                        "\"" + nome + "\" modo=" + Hex(cpu.Get(kR2)) + " | " +
                            arquivos_.UltimoMotivo());
          cpu.Set(kR0, 0);  // NULL -- nao ha IFile
        } else {
          traco_.Emitir(Area::Brew, Nivel::Depuracao, "OPENFILE",
                        "\"" + nome + "\" -> " + arquivos_.UltimoCaminho() +
                            (arquivos_.UltimoVeioDePacote() ? " (entrada de .pkg)"
                                                            : " (ficheiro solto)"));
          const std::uint32_t obj = kObjFileBase + id * 0x40;
          mem_.Escrever32(obj + 0, vtable_ficheiro_);
          mem_.Escrever32(obj + 4, 1);
          cpu.Set(kR0, obj);
        }
      } else if (idx == kSlotIdFileRelease) {
        arquivos_.Fechar(IdentificadorDeFicheiro(cpu.Get(kR0)));
        cpu.Set(kR0, 0);
      } else if (idx == kSlotIdFileRead) {
        // `int32 Read(IFile *po, void *pDest, uint32 nWant)` -- IAStream slot 3.
        const std::int32_t n = arquivos_.Ler(IdentificadorDeFicheiro(cpu.Get(kR0)), mem_,
                                            cpu.Get(kR1), cpu.Get(kR2));
        cpu.Set(kR0, static_cast<std::uint32_t>(n));
      } else if (idx == kSlotIdFileSeek) {
        // `int32 Seek(IFile *po, FileSeekType seek, int32 position)` -- slot 7.
        const std::int32_t r = arquivos_.Posicionar(IdentificadorDeFicheiro(cpu.Get(kR0)),
                                                    cpu.Get(kR1),
                                                    static_cast<std::int32_t>(cpu.Get(kR2)));
        cpu.Set(kR0, static_cast<std::uint32_t>(r));
      } else if (idx == kSlotIdFileInfo) {
        // `int GetInfo(IFile *po, FileInfo *pInfo)` -- slot 6.
        const bool ok = arquivos_.Informacao(IdentificadorDeFicheiro(cpu.Get(kR0)), mem_,
                                             cpu.Get(kR1));
        cpu.Set(kR0, ok ? kAeeSuccess : kAeeUnsupported);
      } else if (idx == kSlotIdFileWrite) {
        // `uint32 Write(IFile*, const void *p, uint32 n)` -- slot 5.
        // A VFS e SO DE LEITURA por DECISAO. Recusa declarada, zero bytes.
        //
        // "Declarada" onde? Nao havia registo nenhum. E este e o pior dos tres
        // silencios desta familia: o `Write` devolve o NUMERO DE BYTES
        // ESCRITOS, logo zero e uma resposta legitima do contrato -- um jogo que
        // nao confira o retorno continua como se tivesse gravado.
        char det_w[64];
        std::snprintf(det_w, sizeof(det_w), "%u bytes pedidos",
                      static_cast<unsigned>(cpu.Get(kR2)));
        traco_.RegistarFalta(Area::Brew, "IFile::Write", det_w);
        ultimo_erro_do_fm_ = kAeeUnsupported;
        cpu.Set(kR0, 0);
      } else if (idx == kSlotIdSqlOpen) {
        // `int OpenDatabase(ISQLMgr *po, const char *pszName, ISQLDatabase **ppDB)`
        // -- ISQLMgr slot 3.
        //
        // O PONTEIRO DE SAIDA E O `r2`, E AQUI ESTAVA O `r3`. MEDIDO duas vezes,
        // por dois caminhos independentes:
        //   - a sonda do `zeebx` imprime a chamada inteira --
        //     `slot[ 3] ("tt_prefs.db", 0x10002f28, 0x0)`
        //     (`zeebx-emu/docs/implementacao/13-classes-desconhecidas.md`);
        //   - o `zeebx` escreve o banco em `a2` e le o nome em `a1`
        //     (`zeebx-emu/src/machine/sql.rs`, `OpenDatabase`).
        // O `r3` e o TERCEIRO argumento (zero no Z-Wheel). O que aqui estava
        // escrevia o NULO em cima do que o chamador tivesse no `r3`, e deixava o
        // `ppDB` do chamador com o lixo que ele ja tinha -- um NULO no sitio
        // errado. Nao se via porque a resposta era `AEE_UNSUPPORTED` e o jogo
        // desistia na linha seguinte: o defeito ficava escondido atras de outro.
        std::string nome_sql;
        mem_.LerCadeia(cpu.Get(kR1), &nome_sql, 512);
        const std::uint32_t ppdb = cpu.Get(kR2);
        // O FICHEIRO DO JOGO, LIDO PELA VFS. A VFS desta arvore e SO DE LEITURA
        // por decisao, e a pasta do titulo e a ROM do utilizador: o banco e aberto
        // numa COPIA, num scratch nosso, e quem escreve (o `INSERT` do jogo, o
        // `journal` do proprio SQLite) escreve na copia. A decisao inteira, com a
        // alternativa recusada e a razao, esta em `core/brew/sql.h`.
        std::vector<std::uint8_t> semente;
        std::string motivo_vfs;
        const bool tem_semente = vfs_.Ler(nome_sql, &semente, &motivo_vfs);
        std::string motivo;
        if (sql_.Abrir(nome_sql, tem_semente ? &semente : nullptr, &motivo)) {
          if (ppdb != 0) mem_.Escrever32(ppdb, kObjSqlDb);
          ++sql_abertos_;
          traco_.RegistarPressuposto(
              Area::Brew, "ISQLMgr::Open (ponte SQLite)",
              "o banco do jogo e aberto numa copia da midia, em scratch proprio");
          traco_.Emitir(Area::Brew, Nivel::Depuracao, "SQLMGR_OPEN",
                        "\"" + nome_sql + "\" -> 0x" + Hex(kObjSqlDb) + " ppDB=0x" + Hex(ppdb) +
                            " aberto#" + std::to_string(sql_abertos_) + " ficheiro=" +
                            sql_.Caminho() +
                            (tem_semente ? " (semeado da midia)" : " (novo, nao existe na midia)"));
          cpu.Set(kR0, kAeeSuccess);
        } else {
          // O BANCO NAO ABRIU, e a razao vai no registo com o NOME que o jogo deu:
          // "nao abriu" sem o nome do ficheiro nem o motivo e a recusa muda que
          // esta arvore conta como defeito.
          traco_.RegistarFalta(Area::Brew, "ISQLMgr::Open (o banco nao abriu)",
                               "\"" + nome_sql + "\": " + motivo);
          cpu.Set(kR0, kAeeFailed);
        }
      } else if (idx == kSlotIdGetDeviceInfo) {
        // `void GetDeviceInfo(IShell *po, AEEDeviceInfo *pi)` -- IShell slot 4,
        // e a demanda MAIS ALTA do corpus. MEDIDO com uma sonda temporaria no
        // proprio handler (bateria com `ZB2_TRACE=1`, corpus62): **37 dos 62
        // titulos chamam-no**, e sete deles duas vezes. O numero "18" que aqui
        // estava era de uma contagem antiga e nao voltou a ser medido.
        //
        // O jogo le daqui o TAMANHO DO ECRA e a profundidade de cor, para calcular
        // posicoes e para decidir que superficies pode criar. Sem isto, 18 titulos
        // pediam-no e nao recebiam nada.
        //
        // O TAMANHO VEM DE `core/brew/ecra.h`: 640x480 (VGA), 16 bits.
        //
        // AQUI DIZIA 320x240, E ERA A SEGUNDA RESOLUCAO DA ARVORE. A `Tela`, o
        // EGL, o rasterizador e o `IBitmap` deste mesmo ficheiro (linhas 1028 e
        // 1076) ja diziam 640x480. Um jogo que pergunta o tamanho aqui e depois
        // desenha no bitmap do ecra desenhava num quarto da area. O guia oficial
        // (`ZeeboDeveloperGuide0.97.md:490`) diz "Zeebo will only support VGA
        // (640x480) display configuration".
        // 320x240 e 16 bits: os valores do ZEEBO, DECLARADOS como tal.
        //
        // O `wStructSize` E CAMPO DE ENTRADA, e nao de saida. O cabecalho diz-o
        // com todas as letras (`AEEIShell.h:116-120`): "In order to use the
        // following fields, you MUST fill-in the wStructSize element of the
        // structure before passing this to the GetDeviceInfo call."
        //
        // Ou seja: a cauda (`wStructSize`, `dwNetLinger`, `dwSleepDefer`,
        // `wMaxPath`, `dwPlatformID`) so pode ser escrita se o CHAMADOR tiver
        // declarado que a struct dele chega la. Escrevia-se sempre 64 bytes --
        // e um titulo compilado contra a struct curta (44 bytes, ate ao
        // `dwLang`) levava VINTE bytes por cima do que estivesse a seguir, que
        // na esmagadora maioria dos casos e a pilha do proprio chamador.
        //
        // MEDIDO nesta arvore: `sizeof(AeeDeviceInfo)` = 64,
        // `offsetof(w_struct_size)` = 44, `offsetof(dw_net_linger)` = 48
        // (compilado e impresso). O zeebx tem os MESMOS offsets, escritos a mao
        // e chegados la por outro caminho (`src/machine/shell.rs:425-455`), e
        // diz o titulo que o obrigou: o Bejeweled Twist manda 64 e, enquanto so
        // recebia os 44 primeiros bytes, lia `wMaxPath = 0` -- nenhum caminho de
        // ficheiro lhe cabia.
        const std::uint32_t pi = cpu.Get(kR1);
        if (pi != 0) {
          // LE-SE ANTES DE ESCREVER. O campo pertence ao chamador.
          const std::uint32_t pedido = mem_.Ler16(pi + kOffsetDeWStructSize);
          const bool quer_a_cauda = pedido >= sizeof(AeeDeviceInfo);
          AeeDeviceInfo di{};
          di.cx_screen = static_cast<std::uint16_t>(zb2::brew::kLarguraDoEcra);
          di.cy_screen = static_cast<std::uint16_t>(zb2::brew::kAlturaDoEcra);
          // `cxAltScreen`/`cyAltScreen` a ZERO: o Zeebo NAO TEM segunda tela.
          //
          // Estavam iguais ao ecra principal, o que declara um segundo ecra com o
          // mesmo tamanho -- e um titulo que teste `bAltDisplay`/`cxAltScreen`
          // para decidir se ha tampa (o caso dos telemoveis de concha, para que
          // este campo existe) recebia um sim. O `bAltDisplay` ja dizia 0 aqui
          // ao lado: os dois discordavam um do outro.
          // O zeebx poe zero pela mesma razao (`machine/shell.rs:437-438`).
          di.cx_alt_screen = 0;
          di.cy_alt_screen = 0;
          di.cx_scroll_bar = 10;
          di.w_encoding = 0;          // AEE_ENC_UNICODE
          di.w_menu_text_scroll = 30;
          di.n_color_depth = 16;
          di.unused2 = 0;
          di.w_menu_image_delay = 100;
          // `dwRAM` -- o cabecalho chama-lhe deprecated, mas ha FONTE para o
          // valor e um titulo que o leia merece o numero certo em vez de zero:
          // o guia do fabricante diz "Heap size is limited to 32MB"
          // (`ZeeboDeveloperGuide0.97.md:796`), e o aparelho tem "128 MBytes DDR
          // SDRAM + 32Mbyte stacked" (:236). O que interessa ao titulo e o HEAP.
          // O zeebx devolve o mesmo (`machine/shell.rs:451`, o `HEAP_SIZE` dele).
          //
          // Zero nao e "nao sei": zero e "nao ha RAM", e um titulo que divida
          // por ele ou que decida a qualidade das texturas por ele apanha o pior
          // caminho possivel.
          // `dwRAM` E O HEAP QUE DAMOS, e nao o do guia.
          //
          // O campo e o "Initial size of BREW heap" (`AEEShell.h:425`). O valor era o
          // do guia ("Heap size is limited to 32MB", `ZeeboDeveloperGuide0.97.md:796`)
          // -- verdadeiro na consola, falso aqui desde que o heap passou a 64 MiB
          // (`d3af3ec`). Declarar 32 MiB sobre um heap de 64 e a mesma familia de
          // mentira que esta sessao ja corrigiu varias vezes, e `al_.Tamanho()` e a
          // fonte unica: e o MESMO alocador que serve o `malloc` do guest.
          //
          // MEDIDO e DITO: isto **nao** mexeu em nenhum dos 62 titulos (0 regressoes,
          // 0 melhorias, 0 neutros). Tinha sido escrito para explicar o pool do motor
          // TTD e a medicao desmentiu-o -- o `footparty` pede os mesmos 24 117 248
          // bytes com 8, 32 ou 64 MiB declarados (ver o `docs/rewrite/PLANO`). Fica
          // porque e verdade, nao porque resolve.
          di.dw_ram = al_.Tamanho();
          di.b_alt_display = 0; di.b_flip = 0; di.b_vibrator = 0; di.b_ext_speaker = 0;
          di.b_vr = 0; di.b_pos_loc = 0; di.b_midi = 1; di.b_cmx = 0; di.b_pen = 1;
          di.dw_prompt_props = 0;
          di.w_key_close_app = 0; di.w_key_close_all_apps = 0;
          di.dw_lang = 0;             // AEE_LNG_ENGLISH
          di.w_struct_size = static_cast<std::uint16_t>(sizeof(AeeDeviceInfo));
          di.dw_net_linger = 0; di.dw_sleep_defer = 0;
          di.w_max_path = 256;
          di.dw_platform_id = 0;
          const auto* b = reinterpret_cast<const std::uint8_t*>(&di);
          const std::size_t quantos = quer_a_cauda ? sizeof(AeeDeviceInfo) : kOffsetDeWStructSize;
          for (std::size_t k = 0; k < quantos; ++k) {
            mem_.Escrever8(pi + static_cast<std::uint32_t>(k), b[k]);
          }
          // A DEMANDA MAIS ALTA DO CORPUS (18 titulos) nao deixava rasto nenhum.
          // Os valores sao DECLARADOS: ficam contados, com o tamanho que o
          // titulo pediu -- e assim a proxima pessoa nao tem de adivinhar quais
          // dos 62 pedem a cauda.
          char det[128];
          std::snprintf(det, sizeof(det),
                        "%ux%u, %u bits, wStructSize pedido=%u -> escritos %u bytes",
                        static_cast<unsigned>(di.cx_screen), static_cast<unsigned>(di.cy_screen),
                        static_cast<unsigned>(di.n_color_depth), static_cast<unsigned>(pedido),
                        static_cast<unsigned>(quantos));
          traco_.RegistarPressuposto(Area::Brew, "IShell::GetDeviceInfo", det);
        }
        cpu.Set(kR0, 0);
      } else if (idx == kSlotIdMkDir) {
        // `int MkDir(IFileMgr *po, const char *pszDir)` -- IFileMgr slot 5.
        // Mesma decisao do RmDir/Remove: VFS so de leitura, servido com SUCCESS
        // e pressuposto declarado (nada criado no hospedeiro).
        std::string nome;
        mem_.LerCadeia(cpu.Get(kR1), &nome, 512);
        ultimo_erro_do_fm_ = kAeeSuccess;
        traco_.RegistarPressuposto(Area::Brew, "IFileMgr::MkDir",
                                   "serviu SUCCESS: " + nome +
                                       "; diretorio NAO criado no hospedeiro (VFS so de leitura)");
        traco_.Emitir(Area::Brew, Nivel::Depuracao, "FM_MKDIR",
                      nome + " -> OK (nada criado)");
        cpu.Set(kR0, static_cast<std::uint32_t>(ultimo_erro_do_fm_));
      } else if (idx == kSlotIdRemove) {
        // `int Remove(IFileMgr *po, const char *pszFile)` -- IFileMgr slot 4
        // (`tools/brew_slots.inc`, gerado de `AEEFile.h`).
        //
        // SERVIDO, e nao recusado: a demanda mediu 4 titulos (game, abd,
        // allstarcards, torkandkral) a apagar saves. O contrato
        // (`IFILEMGR_Remove`, AEEFile.h) responde SUCCESS ou EFAILED -- e e
        // isso que se serve, sobre a VFS. A VFS continua SO DE LEITURA: apagar
        // no disco do hospedeiro destruiria a reprodutibilidade, e estes jogos
        // mexem em save dirs que nao sao assets do modulo. "Remove" responde
        // entao "existe? -> SUCCESS : EFAILED", e a operacao no disco NAO
        // acontece -- declarado no pressuposto, para a resposta servida ficar
        // contada e nao muda.
        std::string nome;
        mem_.LerCadeia(cpu.Get(kR1), &nome, 512);
        const bool existe = vfs_.Existe(nome);
        ultimo_erro_do_fm_ = existe ? kAeeSuccess : kAeeFailed;
        traco_.RegistarPressuposto(Area::Brew, "IFileMgr::Remove",
                                   existe ? "serviu SUCCESS: " + nome +
                                                " existe na VFS; nada foi apagado no hospedeiro"
                                          : "serviu EFAILED: " + nome +
                                                " nao existe na VFS; nada foi apagado no hospedeiro");
        traco_.Emitir(Area::Brew, Nivel::Depuracao, "FM_REMOVE",
                      nome + (existe ? " -> OK (nada apagado)" : " -> MISS"));
        cpu.Set(kR0, static_cast<std::uint32_t>(ultimo_erro_do_fm_));
      } else if (idx == kSlotIdRmDir) {
        // `int RmDir(IFileMgr *po, const char *pszDir)`.
        //
        // O SLOT, MEDIDO NA NUMERACAO DA CABLAGEM: a tabela unica `kWire` de
        // `tools/bateria.cpp` aponta este id ao slot 7 da vtable, e o slot 7 do
        // SDK (`kFileMgr_Test`) e o `Test` -- responde "existe?". O `RmDir` do
        // SDK e o slot 6 (`kFileMgr_RmDir`), e o servico responde TAMBEM nesse
        // endereco (7000+6, ramo abaixo) -- para a correccao da cablagem nao
        // poder partir sem se ver. A regra servida e a mesma para os dois
        // enderecos, e e a regra que os dois contratos partilham: o caminho
        // existe na VFS? (no `RmDir` do SDK, a operacao so pode ter sucesso se
        // o directorio existir e estiver vazio; a VFS nao consegue provar
        // vazio e NAO apaga nada -- a decisao fica declarada no pressuposto).
        //
        // A DEMANDA, medida: 6 titulos (alpineracerex, pacmania, tekken2, gof,
        // allstarcards, pbc) apagam saves velhos antes de gravar -- e o guia do
        // fabricante pede exactamente este padrao: "If there are files or
        // directories elements beneath the directory to be removed, they should
        // be removed prior to calling this function" (AEEFile.h, RmDir).
        std::string nome_rm;
        mem_.LerCadeia(cpu.Get(kR1), &nome_rm, 512);
        const bool existe_rm = vfs_.Existe(nome_rm);
        ultimo_erro_do_fm_ = existe_rm ? kAeeSuccess : kAeeFailed;
        traco_.RegistarPressuposto(Area::Brew, "IFileMgr::RmDir",
                                   existe_rm ? "serviu SUCCESS: " + nome_rm +
                                                   " existe na VFS; diretorio NAO removido no hospedeiro"
                                             : "serviu EFAILED: " + nome_rm +
                                                   " nao existe na VFS; diretorio NAO removido no hospedeiro");
        traco_.Emitir(Area::Brew, Nivel::Depuracao, "FM_RMDIR",
                      nome_rm + (existe_rm ? " -> OK (nada removido)" : " -> MISS"));
        cpu.Set(kR0, static_cast<std::uint32_t>(ultimo_erro_do_fm_));
      } else if (idx == kSlotIdGetAppInstance) {
        // `void *GetAppInstance(void)` -- o ponteiro do applet, para o codigo que
        // nao tem o `po` a mao. Nao tem argumentos: os registos que a bateria
        // imprime sao RESIDUAIS, e foi por isso que quase persegui uma "fuga de
        // enderecos de saida para o guest" que nao existia.
        //
        // **DENTRO DO PROPRIO `CreateInstance` a resposta NAO E `applet_`**, e
        // isto esta MEDIDO no `Rolimaz` (276809): o motor chama o ajudante 0x0c0
        // em `pc=0x000047b8` (durante o create) e usa a resposta como OBJECT0
        // -- `ldr r0,[r4,#0xc]` em `0x0000480c`. Com a resposta a ZERO, essa
        // leitura cai no CABECALHO DO PROPRIO MODULO (`[0x0c]` = 1), o `1` passa
        // a ser tratado como ponteiro, a leitura de `[1]` devolve `0x000fea00`,
        // o `blx` seguinte salta para 0 e a entrada do modulo corre uma SEGUNDA
        // vez (`ENTRADA_DO_MODULO_REPETIDA lr=0x000023dc`).
        //
        // MEDIDO na corrida dos 62 (0 regressoes): os 5 titulos do motor Tectoy
        // (`Rolimaz`, `AirRacez`, `Bajaz`, `Boiaz`, `JetBoardz` -- 5,5,5,5,5
        // re-entradas -> 0) e o `ddragonz` (1 -> 0, 0 -> 120 972 000 px e
        // 0 -> 1500 textos). **O `footparty`, o `cnk2` e o `zumar` NAO vem por
        // aqui**: esta correccao nao lhes toca UM campo (0 diferencas em 62), e o
        // mecanismo deles esta medido em `/tmp/pesquisa/g2.md` (seccao 5).
        //
        // A REGRA E A QUE O `SendEvent` JA USA (acima, `EntregarEventoAoApplet`):
        // o applet existe quando e CRIADO, e nao quando e INICIADO -- o
        // `AEEApplet_New` do guest ja escreveu o ponteiro no `ppObj`
        // (zeebulator `core/brew/ishell.cpp:200-207`; zeebx
        // `src/machine/helper.rs`, caso "GetAppInstance": `current_applet == 0`
        // -> `read_u32(out_module + 4)`). UM LUGAR, UMA REGRA: `applet_` so
        // ganha quando o `DefinirApplet` corre (depois do create).
        std::uint32_t applet = applet_;
        if (applet == 0 && pp_saida != 0) applet = mem_.Ler32(pp_saida);
        cpu.Set(kR0, applet);
      } else if (idx == kSlotIdQueryClass) {
        // `boolean QueryClass(IShell *po, AEECLSID cls, AEEAppInfo *pai)`.
        //
        // Responde se a classe existe, e preenche o `AEEAppInfo` quando ha
        // ponteiro. As classes que SEI criar sao as que o `CreateInstance` e o
        // `CreateInstance` ja servem; o resto devolve FALSE -- recusar, nao
        // mentir. Um `AEEAppInfo` a zeros com `TRUE` seria a versao em dados do
        // stub silencioso.
        const std::uint32_t cls = cpu.Get(kR1);
        const std::uint32_t pai = cpu.Get(kR2);
        // A LISTA E UMA SO (`ClasseConhecida`), e e partilhada com o
        // `CheckPrivLevel`: duas listas que tem de concordar sao zero listas --
        // e esta arvore ja pagou essa licao mais do que uma vez.
        const bool conhecida = ClasseConhecida(cls);
        if (pai != 0) {
          // AEEAppInfo: cls(0), pszName(4), pszIcon(8), dwIconSize(12), ...
          mem_.Escrever32(pai + 0, cls);
          mem_.Escrever32(pai + 4, 0);
          mem_.Escrever32(pai + 8, 0);
          mem_.Escrever32(pai + 12, 0);
          mem_.Escrever32(pai + 16, 0);
        }
        cpu.Set(kR0, conhecida ? 1u : 0u);
      } else if (idx == kSlotIdFmTest) {
        // `int Test(IFileMgr *po, const char *pszName)` -- devolve AEE_SUCCESS
        // se o ficheiro existe no sistema de ficheiros virtual.
        std::string nome;
        mem_.LerCadeia(cpu.Get(kR1), &nome, 512);
        const std::uint32_t existe = vfs_.Existe(nome) ? kAeeSuccess : kAeeFailed;
        traco_.Emitir(Area::Brew, Nivel::Depuracao, "FM_TEST",
                                        nome + (existe == 0 ? " -> OK" : " -> MISS"));
        cpu.Set(kR0, existe);
      } else if (idx == kSlotIdFmFree) {
        // `uint32 GetFreeSpace(IFileMgr *po, uint32 *pdwTotal)`.
        //
        // O TOTAL DEIXA DE SER INVENTADO: o guia oficial do fabricante diz
        // "The total file system size available on Zeebo is 1GB"
        // (`ZeeboDeveloperGuide0.97.md:794`). 1 GiB, MEDIDO no documento.
        //
        // O LIVRE continua DECLARADO -- e partilhado com tudo o que esteja
        // instalado, e nenhum documento o fixa. O numero escolhido tem uma
        // razao escrita: o mesmo guia (`:796-797`) diz que um titulo pode usar
        // "at most 64KB for save game data", logo qualquer valor muito acima de
        // 64 KiB responde a pergunta que o jogo faz ("cabe o meu save?") sem
        // fingir um disco vazio. 64 MiB.
        //
        // O comentario antigo PROMETIA "fica registado como valor declarado" e
        // nao havia registo nenhum. Agora ha.
        constexpr std::uint32_t kTotalDoFs = 0x40000000u;  // 1 GiB, guia :794
        constexpr std::uint32_t kLivreDeclarado = 0x04000000u;  // 64 MiB
        if (cpu.Get(kR1) != 0) mem_.Escrever32(cpu.Get(kR1), kTotalDoFs);
        traco_.RegistarPressuposto(Area::Brew, "IFileMgr::GetFreeSpace",
                                   "total=1 GiB MEDIDO (ZeeboDeveloperGuide0.97.md:794); "
                                   "livre=64 MiB DECLARADO (o guia so fixa os 64 KiB de "
                                   "save, :796)");
        cpu.Set(kR0, kLivreDeclarado);
      } else if (idx == kSlotIdFmLastErr) {
        // `int GetLastError(IFileMgr *po)` -- o erro da ULTIMA operacao que
        // falhou. Devolvia SEMPRE 0, ou seja "correu tudo bem" logo a seguir a
        // uma recusa: um jogo que faca `if (IFILEMGR_GetLastError(pfm) ==
        // EFILEEXISTS)` para decidir o que fazer a seguir decide ao contrario.
        cpu.Set(kR0, static_cast<std::uint32_t>(ultimo_erro_do_fm_));
      } else if (idx == kSlotIdDbgPrintf) {
        // `void dbgprintf(const char *psz, ...)` -- AEEHelperFuncs 0x09c
        // (`AEEStdLib.h:83`). O FORMATO esta no r0 e os VARIADICOS comecam no r1
        // (AAPCS), logo a mensagem tem de ser FORMATA. Imprimir a cadeia de
        // formato tal e qual -- `GUEST_DBGPRINTF %s`, `...*dbgprintf-%d* %s:%d` --
        // e o que deixava a mensagem do ASSERT invisivel em 7 titulos.
        std::uint32_t arg_dbg[8];
        arg_dbg[0] = cpu.Get(kR1);
        arg_dbg[1] = cpu.Get(kR2);
        arg_dbg[2] = cpu.Get(kR3);
        for (int i = 3; i < 8; ++i) {
          const std::uint32_t onde = cpu.Get(kSP) + static_cast<std::uint32_t>(i - 3) * 4;
          arg_dbg[i] = mem_.Existe(onde) ? mem_.Ler32(onde) : 0u;  // nao se le o que nao existe
        }
        const std::string msg = FormatarParaTexto(mem_, cpu.Get(kR0), arg_dbg, 8, 512);
        traco_.Emitir(Area::Brew, Nivel::Depuracao, "GUEST_DBGPRINTF", msg);
        cpu.Set(kR0, 0);
      } else if (idx >= kBaseDoSlot &&
                 AtenderAjudanteExtra(cpu, mem_, al_, traco_, (idx - kBaseDoSlot) * 4)) {
        // A TABELA DOS AJUDANTES EXTRA, e este ramo vem ANTES do ramo generico
        // dos 117 slots. **A ORDEM E O DEFEITO**: com a condicao invertida
        // (`!Atender...`) o caso ATENDIDO cai no ramo generico, que escreve
        // AEE_EUNSUPPORTED por cima do resultado e regista um
        // `servico_sem_nome_idx<idx>`. Foi o que a primeira versao deste gancho
        // fez, e a lista de demanda mostrou-o na ronda seguinte: os tres offsets
        // tratados apareceram como `servico_sem_nome_idx1017/1020/1078`. E a
        // setima vez que esta classe de erro aparece nesta arvore.
        //
        // R0 ja esta escrito (implementacao, ou recusa com o nome e a
        // assinatura). O `saidas` continua a contar no fim do laco.
      } else if (idx >= kBaseDoSlot) {
        const std::uint32_t off = (idx - kBaseDoSlot) * 4;
        const char* conhecido = zb2::brew::NomeDoAjudante(off);
        char nome[64];
        if (conhecido != nullptr) {
          std::snprintf(nome, sizeof(nome), "AEEHelperFuncs[0x%03x] %s", off, conhecido);
        } else {
          std::snprintf(nome, sizeof(nome), "AEEHelperFuncs[0x%03x]", off);
        }
        char det2[128];
        std::snprintf(det2, sizeof(det2), "r0=0x%08x r1=0x%08x r2=0x%08x", r0, cpu.Get(kR1),
                      cpu.Get(kR2));
        traco_.RegistarFalta(Area::Brew, nome, det2);
        cpu.Set(kR0, kAeeUnsupported);
        recusou_agora = true;
        if (++recusas_seguidas > 200) { resultado.motivo = "parou_em_slot_nao_implementado"; return resultado; }
      } else {
        traco_.RegistarFalta(Area::Brew, "servico_sem_nome_idx" + std::to_string(idx),
                            "chamado com r0=" + Hex(r0));
        cpu.Set(kR0, kAeeUnsupported);
        recusou_agora = true;
        if (++recusas_seguidas > 200) { resultado.motivo = "parou_em_slot_nao_implementado"; return resultado; }
      }
      cpu.Bx(kernel_retorno_);
      // Esta saida foi SERVIDA (nao passou por ramo de recusa): a sequencia de
      // recusas recomeca. E o "zera quando corre sem recusa" do item 1 do PLAN
      // -- a zero a cada INSTRUCAO do guest (em vez de a cada saida servida),
      // um ciclo preso em recusas nunca acumulava 201: entre recusas ha sempre
      // instrucoes do proprio laco, e o detector morreria em silencio.
      if (!recusou_agora) recusas_seguidas = 0;
      if (++saidas > kSaidasPorFase) { resultado.motivo = "laco_de_saidas"; return resultado; }
      // O PARK DA ESPERA (frente park), na MESMA fronteira da thread. Os dois
      // usam o mesmo facto: aqui o guest esta num ponto onde o estado vivo cabe
      // nos registadores, e trocar de contexto nao interrompe nada pela metade.
      //
      // ESTACIONAR: uma thread que so espera (contagem no limiar, ver
      // `NotarEspera`) e tirada da CPU como se tivesse chamado Suspend, e o
      // hospedeiro volta. O laco de eventos (temporizadores, entrada, midia)
      // fica parado durante a volta da thread -- foi isso que mediu o zeebx:
      // sem estacionar, uma thread que nunca cede trava o quadro. `lr_real_`
      // exclui o `Suspend` de DENTRO da thread (o lr ja e a sentinela: ela
      // cedeu por iniciativa propria, e a classes vai fecha-la).
      const bool lr_real_ = cpu.Get(kLR) != kSentinela;
      if (thread_a_correr && profundidade_de_evento_ == 0 && lr_real_ &&
          espera_ms_ >= kParkMs) {
        for (std::uint32_t k = 0; k <= kPC; ++k) estacionada_contexto_[k] = cpu.Get(k);
        estacionada_ = true;
        for (std::uint32_t k = 0; k <= kPC; ++k) cpu.Set(k, hospedeiro[k]);
        thread_a_correr = false;
        espera_polls_ = 0;
        espera_ms_ = 0;
        continue;
      }
      // RETOMAR: a estacionada volta na proxima fronteira, ANTES da thread
      // pendente por outras vias (ela nao esta em `g_pendentes` da classes).
      // O laco de eventos correu pelo menos uma vez entre o estacionar e aqui.
      if (estacionada_ && !thread_a_correr && profundidade_de_evento_ == 0 &&
          lr_real_) {
        for (std::uint32_t k = 0; k <= kPC; ++k) hospedeiro[k] = cpu.Get(k);
        for (std::uint32_t k = 0; k <= kPC; ++k) cpu.Set(k, estacionada_contexto_[k]);
        thread_a_correr = true;
        estacionada_ = false;
        continue;
      }
      // A FRONTEIRA ENTRE CHAMADAS DE API, e a THREAD COOPERATIVA (frente
      // thrd). E aqui -- com o guest num ponto onde o estado vivo cabe nos
      // registadores mais o pc de retorno -- que a thread pendente volta a
      // correr o seu pfn (`PrepararRetomadaDeThread`, o `run_pending_threads`
      // do zeebx). O `tinha_pendente` e a regra de fronteira: a chamada que
      // CRIOU a thread nao e a fronteira onde ela corre, a PROXIMA e.
      //
      // FORA DO DESPACHO DE UM EVENTO (`profundidade_de_evento_ == 0`): dentro
      // de um `SendEvent` o guest esta no meio de um tratador, e retomar a
      // thread ai era reentrancia -- o problema que este laco evita noutros
      // sitios (o relogio virtual parado durante a entrega).
      //
      // A EXCECAO MEDIDA (frente thrq): quando o Start vem com lr == sentinela,
      // ele e um TAIL CALL do guest -- o Start e a ULTIMA chamada do turno, e
      // depois dele nao ha mais nenhuma fronteira com lr real (medido na
      // bateria: 10 titulos criam e iniciam a thread no fim do turno e ela fica
      // pendente para sempre, retomadas = 0). Nesse caso o proprio Start e a
      // fronteira -- DESDE QUE o guest tenha CORRIDO ate la (`resultado.passos
      // > 0` nesta invocacao). E a guarda que mantem os testes do CONTRATO de
      // classes_test.cpp verdes: eles chamam o Start por `ChamaSaida` A FRIO
      // (pc no proprio endereco de saida, passos == 0) e exigem que com
      // lr == sentinela a thread fique PENDENTE.
      const bool criou_agora = (pfn_do_ultimo_start != 0);
      const bool lr_real = cpu.Get(kLR) != kSentinela;
      const bool guest_correu_ate_ao_start = resultado.passos > 0;
      bool pode_retomar = false;
      if (!thread_a_correr && profundidade_de_evento_ == 0 && TemThreadPendente()) {
        if (criou_agora) {
          const std::uint32_t fim = (faixa_fim_ > faixa_base_) ? faixa_fim_
                                                               : (kBase + 0x01000000u);
          pode_retomar = pfn_do_ultimo_start >= kBase && pfn_do_ultimo_start < fim &&
                         (lr_real || guest_correu_ate_ao_start);
        } else {
          pode_retomar = tinha_pendente && lr_real;
        }
      }
      if (pode_retomar) {
        for (std::uint32_t k = 0; k <= kPC; ++k) hospedeiro[k] = cpu.Get(k);
        if (PrepararRetomadaDeThread(cpu, traco_)) thread_a_correr = true;
      }
      pfn_do_ultimo_start = 0;
      tinha_pendente = TemThreadPendente();
      continue;
    }
    // O LIMITE E O TAMANHO DA IMAGEM, quando ele e conhecido. Sem ele, cai-se no
    // `kBase + 16 MB` de antes -- que e um limite generoso de mais, mas melhor do
    // que nenhum.
    const std::uint32_t fim = (faixa_fim_ > faixa_base_) ? faixa_fim_ : (kBase + 0x01000000u);
    if (pc < kBase || pc >= fim) {
      // O ANEL DAS ULTIMAS INSTRUCOES -- para responder a "como e que chegamos aqui".
      //
      // MEDIDO: 21 dos 62 titulos saem do modulo com o PC na PILHA (0x8007ffc0..
      // 0x8007ffcc). Isso e um endereco de pilha a ser usado como endereco de
      // CODIGO, e as causas candidatas sao sempre as mesmas: um `bx lr` com o LR
      // estragado, um ponteiro de funcao lido do sitio errado, ou um argumento
      // passado no registo errado. **Sem saber a instrucao que saltou, as tres sao
      // indistinguiveis** -- e foi assim que se perdeu tempo na arvore antiga.
      //
      // O anel guarda as ultimas 16 instrucoes (PC + palavra) e e impresso no
      // motivo. E barato e responde a pergunta no proprio registo da bateria.
      char anel[16 * 22 + 1];
      std::size_t usado = 0;
      anel[0] = 0;
      const std::uint32_t quantas = (ultimas_ < 16) ? ultimas_ : 16;
      for (std::uint32_t k = 0; k < quantas; ++k) {
        const std::uint32_t i = (ultimas_ + k) % 16;
        const int n = std::snprintf(anel + usado, sizeof(anel) - usado, " %08x:%08x",
                                    anel_pc_[i], anel_instr_[i]);
        if (n <= 0 || usado + static_cast<std::size_t>(n) >= sizeof(anel) - 1) break;
        usado += static_cast<std::size_t>(n);
      }
      // OS REGISTOS entram tambem: o anel diz a INSTRUCAO, e os registos dizem de
      // ONDE veio o valor. Medido no `a3d`: a ultima instrucao e `bxne r12` e o
      // `r12` veio de `ldr r12,[r0,#12]` -- um despacho virtual cujo campo `+12` do
      // objecto tem um endereco de PILHA. Sem os registos nao se sabe se o objecto
      // era o errado ou se o campo e que estava por inicializar.
      resultado.motivo = "saiu_do_modulo_para_" + Hex(pc) + " lr=" + Hex(cpu.Get(kLR)) +
                         " r0=" + Hex(cpu.Get(kR0)) + " r1=" + Hex(cpu.Get(kR1)) +
                         " r2=" + Hex(cpu.Get(kR2)) + " r3=" + Hex(cpu.Get(kR3)) +
                         " sp=" + Hex(cpu.Get(kSP)) + " ultimas:" + anel;
      (void)pp_saida;
      return resultado;
    }
    // O LACO DE EVENTOS, em tempo VIRTUAL (principio P4): a cada passo avanca-se
    // 1 ms de tempo emulado, e um temporizador vencido e cumprido aqui.
    //
    // O callback de um `AEECallback` e um par `(funcao, contexto)` nos dois
    // primeiros campos. Chama-se com o contexto no r0, como o SDK define, e o
    // proprio callback re-arma o temporizador -- que e como um laco de quadro
    // se sustenta em BREW.
    // O LACO DE QUADRO PARA ENQUANTO UM EVENTO ESTA A SER ENTREGUE.
    //
    // O `SendEvent` (slot 21) corre o `HandleEvent` do applet por DENTRO deste
    // laco, com um `Correr` aninhado. Se o relogio virtual continuasse a andar
    // la dentro, um temporizador de quadro ou um sinal de entrada podia disparar
    // NO MEIO do tratador -- reentrancia que o BREW nunca faz, e que poria dois
    // callbacks do titulo a partilhar os mesmos registadores.
    //
    // O `agora_ms_` tambem nao avanca: a entrega e instantanea para o guest
    // (o chamador le a resposta na instrucao seguinte, `tectoy.mod:0x6a3a0`).
    // A VOLTA DA THREAD E ATOMICA PARA OS EVENTOS (frente thrd): enquanto uma
    // thread retomada corre, o relogio virtual e os callbacks ficam parados,
    // como durante a entrega de um evento. Um temporizador a disparar no MEIO
    // do turno da thread poria o callback a partilhar registadores com ela.
    if (profundidade_de_evento_ == 0 && !thread_a_correr) {
      ++agora_ms_;
      // O RELOGIO VIRTUAL E UM SO, e a entrada le-o daqui.
      //
      // A `EntradaDoZeebo` tem o seu proprio contador (e e ele que decide que
      // eventos do guiao ja valem), mas quem o avanca e o laco, com o MESMO relogio
      // que faz vencer os temporizadores. Dois relogios dentro da mesma corrida
      // seriam duas fontes de tempo -- e o P4 existe para haver uma.
      if (entrada_pronta_) entrada_.Repor(static_cast<std::uint32_t>(agora_ms_));
      if (timer_.ativo && agora_ms_ >= timer_.vence_em_ms && timer_.pfn != 0) {
        // UMA SO IMPLEMENTACAO do disparo do callback: a mesma que quem dirige o
        // titulo de fora usa. Duas copias disto divergiriam -- e a copia que aqui
        // estava guardava um `pc_salvo`/`lr_salvo` que nunca serviu para nada.
        if (PrepararCallbackDoTemporizador(cpu)) {
          ++resultado.passos;
          continuar_no_laco = true;
        }
      }

      // A ENTRADA, no mesmo lugar do temporizador e pela mesma razao: o laco de
      // eventos e o unico sitio onde o tempo VIRTUAL avanca (P4).
      if (entrada_pronta_ && ihid_.Bombear(cpu)) {
        continuar_no_laco = true;
        continue;
      }

      // A MIDIA ANDA COM O RELOGIO VIRTUAL DO LACO (P4), e o aviso e entregue aqui.
      //
      // As duas coisas juntas, e NAO dentro de um handler: a entrega reentra no
      // codigo do guest, e o `EntregarAviso` guarda e repoe os 16 registradores, o
      // CPSR e o PC -- o que so e seguro no passo, onde o guest esta numa fronteira
      // de instrucao.
      //
      // Sem o avanco, o `Play` de um titulo nunca chega ao fim e o aviso DONE -- o
      // que o `cnk2` conta para so tocar a musica da pista -- nunca nasce.
      if (media_ != nullptr) {
        media_->Avancar(kAmostrasDeMidiaPorMs);
        media_->EntregarAviso(cpu, kSentinela, 200000);
      }
    }

    // Cheats com gatilho de PC: um `if` quando a lista esta vazia (o caso de
    // todas as corridas sem `ZB2_CHEATS`), varredura curta quando nao esta.
    cheats_.NoPasso(pc);
    if (const char* w = std::getenv("ZB2_SONDA_WATCH")) {
      const std::uint32_t alvo_w = static_cast<std::uint32_t>(std::strtoul(w, nullptr, 0));
      static std::uint32_t ant = 0;
      static bool liga = false;
      const std::uint32_t v = mem_.Ler32(alvo_w);
      if (!liga) { liga = true; ant = v; }
      else if (v != ant) {
        std::fprintf(stderr,
                     "WATCH %08x %08x -> %08x pela instrucao pc=%08x=%08x r0=%08x r1=%08x r2=%08x r3=%08x r4=%08x r5=%08x r6=%08x r7=%08x lr=%08x sp=%08x\n",
                     alvo_w, ant, v, pc, mem_.Ler32(pc), cpu.Get(kR0), cpu.Get(kR1), cpu.Get(kR2),
                     cpu.Get(kR3), cpu.Get(kR4), cpu.Get(kR5), cpu.Get(kR6), cpu.Get(kR7),
                     cpu.Get(kLR), cpu.Get(kSP));
        ant = v;
      }
    }
    // PROPOSTA (frente ropi2), NAO APLICADA NA ENTREGA.
    //
    // A SEGUNDA ENTRADA NO MODULO E UMA SENTENCA DE MORTE para os 6 titulos da
    // familia A (`cninja`, `karnovr`, `spinmast`, `strhoop`, `supbtime`,
    // `wizdfire`), e hoje ela nao se ve em lado nenhum: o registo do titulo diz
    // apenas `saiu_do_modulo_para_0xfe3bc25c`. MEDIDO no `cninja` real: a
    // primeira entrada em 0 e a do sistema (lr = sentinela, r2 = 0x90000); a
    // SEGUNDA vem de DENTRO da thread (sp = 0x8f033e60, lr = 0x00011014, r0 = 0)
    // e a veneira da ROPI volta a correr sobre a lista JA ZERADA -- 82 475
    // iteracoes de `[0x9c] += 0x9c`, que destroem a primeira instrucao do
    // proprio modulo (`0x0a8ef06e` = `beq 0xfe3bc1c0`, e o PC de saida do
    // titulo e `0xfe3bc25c` = o alvo mais 0x9c). Ver
    // `tests/carga_test.cpp`, `CargaRopi.ASegundaPassagemDaVeneiraDestroiAEntradaDoModulo`.
    if (pc == faixa_base_) {
      const bool ja = entrada_ja_correu_;
      entrada_ja_correu_ = true;
      if (ja) traco_.Emitir(Area::Brew, Nivel::Erro, "ENTRADA_DO_MODULO_REPETIDA",
                    "pc=0x" + Hex(pc) + " lr=0x" + Hex(cpu.Get(kLR)) + " r0=0x" +
                        Hex(cpu.Get(kR0)) + " sp=0x" + Hex(cpu.Get(kSP)) +
                        " -- a veneira da ROPI vai correr outra vez sobre a lista zerada");
    }
    if (std::getenv("ZB2_SONDA_ZERO") != nullptr) {
      static bool ja = false;
      if (!ja && pc >= 0x1000u && mem_.Ler32(pc) == 0u) {
        ja = true;
        char a3[16 * 22 + 1];
        std::size_t u = 0;
        a3[0] = 0;
        const std::uint32_t q = (ultimas_ < 16) ? ultimas_ : 16;
        for (std::uint32_t k = 0; k < q; ++k) {
          const std::uint32_t ii = (ultimas_ + k) % 16;
          const int nn = std::snprintf(a3 + u, sizeof(a3) - u, " %08x:%08x", anel_pc_[ii], anel_instr_[ii]);
          if (nn <= 0 || u + static_cast<std::size_t>(nn) >= sizeof(a3) - 1) break;
          u += static_cast<std::size_t>(nn);
        }
        std::fprintf(stderr, "PRIMEIRO_ZERO pc=%08x r0=%08x r1=%08x r2=%08x r3=%08x r4=%08x r5=%08x r7=%08x lr=%08x sp=%08x anel:%s\n",
                     pc, cpu.Get(kR0), cpu.Get(kR1), cpu.Get(kR2), cpu.Get(kR3), cpu.Get(kR4),
                     cpu.Get(kR5), cpu.Get(kR7), cpu.Get(kLR), cpu.Get(kSP), a3);
      }
    }
    // O ANEL: guarda o PC e a PALAVRA da instrucao antes de a executar. A palavra
    // serve para ver QUAL era a instrucao, e nao so onde estava.
    anel_pc_[ultimas_ % 16] = pc;
    anel_instr_[ultimas_ % 16] = mem_.Ler32(pc);
    const std::uint32_t amostra_pc = LerPcHot();
    if (amostra_pc != 0 && (resultado.passos % amostra_pc) == 0) ++g_pc_hist[pc];
    ++ultimas_;
    cpu.Passo();
    ++resultado.passos;
  }
  resultado.motivo = "orcamento_esgotado";
  DespejarPcHot("orcamento_esgotado");
  // O CORTE GUARDA ONDE CONTINUAR. Sem isto o trecho e PERDIDO: o `Rolimaz` carrega
  // dentro do `EVT_APP_START`, o laco de eventos nao encontra timer nenhum e a
  // sessao acaba como se o jogo tivesse terminado (medido no `zeebx` do Kaio).
  trecho_.valido = true;
  trecho_.pc = cpu.Get(kPC);
  for (int k = 0; k < 16; ++k) trecho_.regs[k] = cpu.Get(k);
  trecho_.thumb = (cpu.Cpsr() & Cpsr::kT) != 0;
  // O `return` EXPLICITO nao e estilo: cair fora do fim de uma funcao que devolve
  // por valor e COMPORTAMENTO INDEFINIDO, e o `ResultadoFase` tem um
  // `std::string` dentro -- o resultado medido foi `free(): double free detected`
  // no segundo titulo, com a pilha a apontar para o `append` de uma cadeia
  // VIZINHA. O `append` era a vitima; o culpado era este `return` em falta.
  return resultado;
}

// ---------------------------------------------------------------------------
// O PARK DA ESPERA (frente park). Detalhes em `despacho.h`; aqui so a conta.
// ---------------------------------------------------------------------------

// Classifica a saida que esta a ser servida. Leitura de relogio CRESCE a
// contagem; ceder e perguntar sem mudar nada nao a desfazem; QUALQUER outra
// chamada e trabalho, e zera (regra medida do zeebx, `note_spin` em
// `fix-fp-threading`).
void Despacho::NotarEspera(ICpu& cpu, std::uint32_t indice) {
  const bool le_relogio = indice == kSlotIdGetUpTime ||
                          (indice >= kBaseDoSlot &&
                           (((indice - kBaseDoSlot) * 4) == 0x0AC ||
                            ((indice - kBaseDoSlot) * 4) == 0x0B0 ||
                            ((indice - kBaseDoSlot) * 4) == 0x0B4));
  if (le_relogio) {
    // Cada leitura vale 1 ms de espera: o relogio virtual desta arvore avanca
    // 1 ms por instrucao, e cada volta do laco de espera do Rolimaz lê o
    // relogio uma vez. O valor nao e uma medida -- e a escala do contador.
    ++espera_polls_;
    espera_ms_ += 1;
    return;
  }
  if (EhCedencia(indice)) return;
  if (EhPerguntaInocua(cpu, indice)) return;
  espera_polls_ = 0;
  espera_ms_ = 0;
}

bool Despacho::EhCedencia(std::uint32_t indice) const {
  const std::uint32_t th = VtClasse(static_cast<std::uint32_t>(Classe::kThread));
  // Ceder a vez nao e trabalho nem espera: nao conta, mas tambem nao desfaz a
  // contagem (zeebx: "é como o laço dá a volta").
  return indice == th + brew_slots::kThread_Suspend ||
         indice == th + brew_slots::kThread_GetResumeCBK ||
         indice == kBaseDoShell + brew_slots::kShell_Resume;
}

bool Despacho::EhPerguntaInocua(ICpu& cpu, std::uint32_t indice) {
  // O CONTROLE COM A FILA VAZIA: a resposta e SEMPRE a mesma, volta apos volta
  // -- nada progride, so o tempo. Com um guiao de entrada, a resposta MUDA e
  // e trabalho (zeebx: "com evento na fila a resposta muda o jogo").
  const bool sem_guiao = entrada_.Quantos() == 0;
  const std::uint32_t b = base_da_entrada_ + Sinais::kSlotsNecessarios;
  if (sem_guiao && entrada_pronta_) {
    const std::uint32_t off = indice - b;
    const bool e_ihid = indice >= b && indice < b + Ihid::kSlotsNecessarios;
    if (e_ihid &&
        (off == brew_slots::kIHID_GetNextConnectEvent ||
         off == Ihid::kBaseDoDispositivo + brew_slots::kHIDDevice_GetDeviceInfo ||
         off == Ihid::kBaseDoDispositivo + brew_slots::kHIDDevice_GetDeviceStatus ||
         off == Ihid::kBaseDoDispositivo + brew_slots::kHIDDevice_GetButtonInfo ||
         off == Ihid::kBaseDoDispositivo + brew_slots::kHIDDevice_GetNumberOfButtons ||
         off == Ihid::kBaseDoDispositivo + brew_slots::kHIDDevice_GetNextButtonEvent ||
         off == Ihid::kBaseDoDispositivo + brew_slots::kHIDDevice_GetPositionState ||
         off == Ihid::kBaseDoDispositivo + brew_slots::kHIDDevice_GetMinPositionInfo ||
         off == Ihid::kBaseDoDispositivo + brew_slots::kHIDDevice_GetMaxPositionInfo ||
         off == Ihid::kBaseDoDispositivo + brew_slots::kHIDDevice_GetAxesInfo)) {
      return true;
    }
  }
  // O `memset` pequeno (<= 16 bytes, o AEEHIDButtonInfo do Rolimaz) e a
  // limpeza antes da pergunta, nao trabalho.
  if (indice == kSlotIdMemset && cpu.Get(kR2) <= kMemsetDaEspera) return true;
  return false;
}

}  // namespace zb2::brew
