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
#include <filesystem>
#include <set>

#include "core/carga/mod.h"
#include "core/cpu/arm_interpreter.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"

using namespace zb2;

namespace {

constexpr std::uint32_t kBase = 0x00100000u;
constexpr std::uint32_t kPilha = 0x80080000u;
constexpr std::uint32_t kHeap = 0x80200000u;
constexpr std::uint32_t kHeapTam = 0x00C00000u;
constexpr std::uint32_t kTabela = 0x80010000u;
constexpr std::uint32_t kPPMod = 0x00090000u;
constexpr std::uint32_t kPPObj = 0x00090010u;
constexpr std::uint32_t kSentinela = 0xFFFFFFF0u;
constexpr std::uint64_t kLimite = 4000000ull;

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
constexpr int kOrcamentoSegundos = 25;
// Os slots da tabela comecam neste indice da faixa de saida. Abaixo dele ficam
// os servicos tratados (malloc, free, AddRef, Release).
constexpr std::uint32_t kBaseDoSlot = 1000;
// Os slots da vtable do IShell comecam aqui, para o mesmo efeito: saber QUAL
// metodo da interface cada titulo chama, e nao so que chamou algum.
constexpr std::uint32_t kBaseDoShell = 2000;
constexpr std::uint32_t kVtableShell = 1000;
constexpr std::uint32_t kVtableBitmap = 8000;
constexpr std::uint32_t kVtableDisplay = 6000;
constexpr std::uint32_t kVtableFileMgr = 7000;

// Vtables das interfaces que o shell entrega. Cada uma tem slots com endereco
// proprio, para o pedido seguinte ficar nomeado.
constexpr std::uint32_t kBaseDoDisplay = 3000;
constexpr std::uint32_t kBaseDoFileMgr = 4000;
constexpr std::uint32_t kObjDisplay = 0x80030000u;
constexpr std::uint32_t kObjFileMgr = 0x80040000u;
constexpr std::uint32_t kIidDisplay = 0x01001001u;
constexpr std::uint32_t kIidFileMgr = 0x01001003u;
// Os IIDs que a bateria MEDIU como pedidos ao QueryInterface, com o nome do SDK
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
constexpr std::uint32_t kObjGenericoBase = 0x80060000u;
constexpr std::uint32_t kVtableGenericoBase = 9000;
constexpr std::uint32_t kPassoGenerico = 0x100;
// A ordem desta tabela e a ordem em que os objectos sao construidos: o indice e
// o que liga o IID ao objecto.
struct GenericIfc { std::uint32_t iid; const char* nome; };
const GenericIfc kGenericos[] = {
    {0x01001002u, "IHeap"},    {0x01001014u, "IFile"},  {0x01001056u, "ISound"},
    {0x01002001u, "IGraphics"}, {0x01028e51u, "IRootForm"},
    {0x0106c411u, "IHID"},     {0x0102c4e8u, "ISQLMgr"},
};
constexpr std::uint32_t kNGenericos = sizeof(kGenericos) / sizeof(kGenericos[0]);
constexpr std::uint32_t ObjGenerico(std::uint32_t k) { return kObjGenericoBase + k * kPassoGenerico; }
constexpr std::uint32_t VtGenerico(std::uint32_t k) { return kVtableGenericoBase + k * 64; }
constexpr std::uint32_t kObjGenerico = 0x80060000u;  // mantido para o resto
// Os slots do IFileMgr, na ordem que `platform/deprecated/inc/AEEFile.h` declara
// em `INHERIT_IFileMgr`. A ORDEM E A DO SDK, lida campo a campo -- e nao
// copiada de outro emulador, que foi o erro que a arvore antiga cometeu com os
// slots do IGLES11.
enum : std::uint32_t {
  kFmQueryInterface = 2,
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
constexpr std::uint32_t kSlotMemmove = 0x000;
constexpr std::uint32_t kSlotMemset = 0x004;
constexpr std::uint32_t kSlotStrcpy = 0x008;
constexpr std::uint32_t kSlotStrcmp = 0x010;
constexpr std::uint32_t kSlotStrlen = 0x014;
constexpr std::uint32_t kSlotStrchr = 0x018;
constexpr std::uint32_t kSlotStrtowstr = 0x040;
constexpr std::uint32_t kSlotAeeGetRand = 0x0a8;
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
constexpr std::uint32_t kSlotIdGetNumButtons = 1544;
constexpr std::uint32_t kSlotIdGetDest = 1545;
constexpr std::uint32_t kSlotIdSetDest = 1546;
constexpr std::uint32_t kSlotIdRmDir = 1547;
constexpr std::uint32_t kSlotIdGetFontMetricsAlias = 1548;
constexpr std::uint32_t kSlotIdGetDeviceInfo = 1549;

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
constexpr std::uint32_t kObjDibBase = 0x80050000u;
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
// FRAMEBUFFER DE SOFTWARE.
//
// Existe para haver uma medida VISUAL que nao dependa de capturar ecra: quantos
// pixels distintos cada titulo escreveu, e de que cor. E a versao honesta de
// "o jogo desenha" -- e foi um censo a medir imagem, e nao jogabilidade, que
// deu veredictos errados na arvore antiga.
constexpr int kLargura = 640;
constexpr int kAltura = 480;
struct Framebuffer {
  std::vector<std::uint32_t> cores{kLargura * kAltura, 0};
  std::uint32_t cor_atual = 0;
  std::uint32_t escritos = 0;
  std::uint32_t clip[4] = {0, 0, kLargura, kAltura};
  void Ponto(int x, int y) {
    if (x < clip[0] || y < clip[1] || x >= clip[0] + clip[2] || y >= clip[1] + clip[3]) return;
    if (x < 0 || y < 0 || x >= kLargura || y >= kAltura) return;
    cores[static_cast<size_t>(y) * kLargura + x] = cor_atual;
    ++escritos;
  }
  void Retangulo(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h,
                 bool preencher) {
    // LIMITE ANTES DE PERCORRER, e nao so dentro do `Ponto`.
    //
    // O `Ponto` ja recusa o que sai do ecra, mas o LACO corria na mesma `w*h`
    // vezes. Com uma rect grande vinda do guest isso sao milhares de milhoes de
    // iteracoes: o `pacmania` passou a levar mais de 900 s e a bateria inteira
    // deixou de acabar.
    //
    // **Um limite verificado so no destino nao limita o trabalho.** O trabalho
    // tem de ser limitado ANTES de comecar.
    if (x >= static_cast<std::uint32_t>(kLargura) || y >= static_cast<std::uint32_t>(kAltura)) return;
    if (w > static_cast<std::uint32_t>(kLargura) - x) w = static_cast<std::uint32_t>(kLargura) - x;
    if (h > static_cast<std::uint32_t>(kAltura) - y) h = static_cast<std::uint32_t>(kAltura) - y;
    if (preencher) {
      for (std::uint32_t j = 0; j < h; ++j) {
        for (std::uint32_t i = 0; i < w; ++i) Ponto(static_cast<int>(x + i), static_cast<int>(y + j));
      }
    } else {
      for (std::uint32_t i = 0; i < w; ++i) {
        Ponto(static_cast<int>(x + i), static_cast<int>(y));
        Ponto(static_cast<int>(x + i), static_cast<int>(y + h - 1));
      }
      for (std::uint32_t j = 0; j < h; ++j) {
        Ponto(static_cast<int>(x), static_cast<int>(y + j));
        Ponto(static_cast<int>(x + w - 1), static_cast<int>(y + j));
      }
    }
  }
  std::uint32_t CoresDistintas() const {
    std::set<std::uint32_t> s;
    for (std::uint32_t v : cores) s.insert(v);
    return static_cast<std::uint32_t>(s.size());
  }
};
Framebuffer g_fb;
std::uint32_t g_textos = 0;
std::uint32_t g_blits = 0;
std::uint32_t g_updates = 0;
std::uint32_t g_dibs = 0;
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
constexpr std::uint32_t kSlotGetAeeVersion = 0x08c;

struct Titulo {
  std::string pasta;
  std::string mod;
  std::string clsid;
};

std::vector<Evento> dm_eventos;  // eventos do ultimo titulo, para os detalhes

struct Estado {
  bool carga = false;
  bool modulo = false;
  bool vtable = false;
  bool create = false;
  std::uint64_t passos_carga = 0;
  std::uint64_t passos_create = 0;
  std::uint64_t recusadas = 0;
  std::uint32_t pixels = 0;
  std::uint32_t cores = 0;
  std::uint32_t textos = 0;
  std::uint32_t blits = 0;
  std::uint32_t tamanho = 0;
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
void ConstruirShell(Memoria& mem, const Saidas& s, std::uint32_t objeto, std::uint32_t vtable,
                    std::uint32_t quantos_slots, std::uint32_t base_dos_slots = kBaseDoShell) {
  mem.Escrever32(objeto, vtable);           // *(pishell) = vtable
  mem.Escrever32(objeto + 4, 1);            // contagem de referencias
  for (std::uint32_t i = 0; i < quantos_slots; ++i) {
    // UM endereco por slot, para o registo dizer QUAL metodo do IShell foi
    // chamado. Com um stub so para todos, 27 titulos pediam "algo do shell" e o
    // numero nao tinha nome.
    mem.Escrever32(vtable + i * 4, s.Endereco(base_dos_slots + i));
  }
  mem.Escrever32(vtable + 0, s.Endereco(3));   // AddRef
  mem.Escrever32(vtable + 4, s.Endereco(4));   // Release
}

// Traduz um offset da tabela de ajudantes para o nome que o SDK lhe da. So os
// que ja foram medidos; o resto fica com o offset, que ja e util porque ordena
// a demanda.
const char* NomeDoSlot(std::uint32_t off) {
  switch (off) {
    case 0x000: return "memmove";
    case 0x004: return "memset";
    case 0x008: return "strcpy";
    case 0x00c: return "strcat";
    case 0x010: return "strcmp";
    case 0x014: return "strlen";
    case 0x018: return "strchr";
    case 0x01c: return "strrchr";
    case 0x020: return "sprintf";
    case 0x068: return "malloc";
    case 0x06c: return "free";
    case 0x088: return "OEMStrSize";
    case 0x08c: return "GetAEEVersion";
    case 0x09c: return "dbgprintf";
    default: return nullptr;
  }
}

// Sistema de ficheiros virtual MINIMO: os ficheiros que estao ao lado do
// modulo, no disco do hospedeiro. Nao ha escrita, e uma tentativa de escrita
// falha -- principio do desenho: nao escrever nas midias do utilizador.
std::set<std::string>* g_vfs = nullptr;
std::int64_t g_agora_ms = 0;
Temporizador g_timer;
bool g_vfs_registado = false;
bool vfs_tem(const std::string& nome) {
  if (g_vfs == nullptr) return false;
  std::string limpo = nome;
  // Normaliza como as rotas do BREW fazem: barras invertidas viram normais,
  // barras repetidas colapsam, e uma barra no inicio ancora no modulo.
  for (char& ch : limpo) {
    if (ch == '\\') ch = '/';
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  while (limpo.find("//") != std::string::npos) limpo.replace(limpo.find("//"), 2, "/");
  while (!limpo.empty() && limpo.front() == '/') limpo.erase(0, 1);
  while (limpo.rfind("./", 0) == 0) limpo.erase(0, 2);
  // `..` so sobe ate a raiz do modulo -- a mesma regra que o zeebx mediu nos
  // Zeebo Extreme, mas aqui o que importa e nao sair da pasta.
  while (limpo.rfind("../", 0) == 0) limpo.erase(0, 3);
  if (g_vfs->count(limpo) != 0) return true;
  // Se sobrar uma pasta a frente, tenta o nome base: os jogos montam
  // "pasta/ficheiro" para recursos que estao soltos na pasta do titulo.
  const size_t barra = limpo.rfind('/');
  if (barra != std::string::npos) return g_vfs->count(limpo.substr(barra + 1)) != 0;
  return false;
}

void CorrerFase(ArmInterpreter& cpu, Alocador& al, Memoria& mem_ref, Traco& traco,
                std::uint64_t limite, std::uint64_t* passos, std::string* motivo,
                std::uint32_t pp_out) {
  *passos = 0;
  std::uint32_t saidas = 0;
  bool continuar_no_laco = false;
  const auto inicio = std::chrono::steady_clock::now();
  while (*passos < limite) {
    // O ORCAMENTO DE TEMPO, verificado a cada 65536 passos.
    //
    // A cada passo seria caro; a cada 65536 o erro maximo e de um bloco, e o
    // custo e nulo. O motivo fica REGISTADO: um titulo que bate no orcamento nao
    // e um titulo que falhou -- e um titulo que ainda estava a andar, e isso
    // muda o que se conclui dele.
    if ((*passos & 0xFFFFull) == 0) {
      const auto agora = std::chrono::steady_clock::now();
      const auto s = std::chrono::duration_cast<std::chrono::seconds>(agora - inicio).count();
      if (s >= kOrcamentoSegundos) {
        *motivo = "orcamento_de_tempo";
        return;
      }
    }
    const std::uint32_t pc = cpu.Get(kPC);
    if (pc == kSentinela) {
      // A sentinela tem dois significados: o retorno da chamada de entrada, ou
      // o retorno de um callback de temporizador. Distinguir os dois e o que
      // permite o laco de eventos -- sem isto, o primeiro callback do jogo
      // seria lido como "o modulo retornou".
      if (continuar_no_laco) { continuar_no_laco = false; continue; }
      *motivo = "retornou";
      return;
    }
    std::uint32_t idx = 0;
    if (cpu.GetSaidas().Contem(pc, &idx)) {
      const std::uint32_t lr = cpu.Get(kLR);
      const std::uint32_t r0 = cpu.Get(kR0);
      if (idx == 0) {
        cpu.Set(kR0, al.Malloc(r0));
      } else if (idx == 1) {
        al.Free(r0);
        cpu.Set(kR0, kAeeSuccess);
      } else if (idx == 3) {
        // IShell::AddRef -- devolve a contagem de referencias, que e o que a
        // interface do SDK promete.
        const std::uint32_t n = mem_ref.Ler32(r0 + 4) + 1;
        mem_ref.Escrever32(r0 + 4, n);
        cpu.Set(kR0, n);
      } else if (idx == 4) {
        // IShell::Release
        const std::uint32_t n = mem_ref.Ler32(r0 + 4);
        if (n > 0) mem_ref.Escrever32(r0 + 4, n - 1);
        cpu.Set(kR0, n > 0 ? n - 1 : 0);
      } else if (idx == kBaseDoShell + 2) {
        // IShell::QueryInterface(po, iid, ppo). Os dois IIDs que o corpus pede
        // sao conhecidos por medicao; o que nao for conhecido devolve
        // ECLASSNOTSUPPORT com o ponteiro a zero -- recusar, nao mentir.
        const std::uint32_t iid = cpu.Get(kR1);
        const std::uint32_t ppo = cpu.Get(kR2);
        std::uint32_t devolver = 0;
        if (iid == kIidDisplay) devolver = kObjDisplay;
        else if (iid == kIidFileMgr) devolver = kObjFileMgr;
        // Os que tem objecto generico: o jogo fica com uma interface cujos
        // metodos recusam, e a bateria aprende quais sao.
        else {
          for (std::uint32_t k = 0; k < kNGenericos; ++k) {
            if (iid == kGenericos[k].iid) devolver = ObjGenerico(k);
          }
        }
        if (ppo != 0) mem_ref.Escrever32(ppo, devolver);
        cpu.Set(kR0, devolver != 0 ? kAeeSuccess : kAeeClassNotSupported);
        if (devolver == 0) {
          char det[96];
          std::snprintf(det, sizeof(det), "iid=0x%08x ppo=0x%08x", iid, ppo);
          traco.RegistarFalta(Area::Brew, "IShell::CreateInstance CLSID desconhecido", det);
        }
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
        const std::uint32_t kgen = (idx >= kVtableGenericoBase)
                                       ? (idx - kVtableGenericoBase) / 64u
                                       : kNGenericos;
        if (kgen < kNGenericos) {
          iface = kGenericos[kgen].nome;
          slot = idx - VtGenerico(kgen);
        } else if (idx >= kVtableFileMgr) { iface = "IFileMgr"; slot = idx - kVtableFileMgr; }
        else if (idx >= kVtableDisplay) { iface = "IDisplay"; slot = idx - kVtableDisplay; }
        else { iface = "IShell"; slot = idx - kBaseDoShell; }
        std::snprintf(nome, sizeof(nome), "%s::slot%u", iface, slot);
        // Os ARGUMENTOS no detalhe: para o QueryInterface (slot 2) o r1 e o IID
        // pedido, e sem ele nao se sabe o que responder. Foi assim que se
        // percebeu, na arvore antiga, quais das interfaces eram as mesmas por
        // dois nomes diferentes.
        // O LR entra no detalhe porque sem ele nao se sabe QUEM chama.
        //
        // Foi a falta dele que me deixou a olhar para um `IDisplay::slot2` com uma
        // FONTE (`AEE_FONT_NORMAL = 0x8000`) no r1 -- argumento que nenhum metodo
        // daquele slot aceita -- sem forma de saber de onde vinha a chamada. Com o
        // LR, vai-se ao sitio e le-se a instrucao.
        std::snprintf(det, sizeof(det), "r0=0x%08x r1=0x%08x r2=0x%08x lr=0x%08x", r0, cpu.Get(kR1),
                      cpu.Get(kR2), cpu.Get(kLR));
        traco.RegistarFalta(Area::Brew, nome, det);
        cpu.Set(kR0, kAeeUnsupported);
        if (++saidas > 200) { *motivo = "parou_em_slot_nao_implementado"; return; }
      } else if (idx == kSlotIdStrlen) {
        // size_t strlen(const char *s) -- conta ate ao NUL, sem limite
        // artificial: a memoria do guest responde zero onde nao ha nada.
        std::uint32_t n = 0;
        while (mem_ref.Ler8(r0 + n) != 0) ++n;
        cpu.Set(kR0, n);
      } else if (idx == kSlotIdMemset) {
        // void *memset(void *d, int c, size_t n) -- devolve o destino
        const std::uint32_t n = cpu.Get(kR2);
        for (std::uint32_t i = 0; i < n; ++i) mem_ref.Escrever8(r0 + i, static_cast<std::uint8_t>(cpu.Get(kR1)));
        cpu.Set(kR0, r0);
      } else if (idx == kSlotIdStrcpy) {
        const std::uint32_t src = cpu.Get(kR1);
        std::uint32_t i = 0;
        for (;;) {
          const std::uint8_t b = mem_ref.Ler8(src + i);
          mem_ref.Escrever8(r0 + i, b);
          if (b == 0) break;
          ++i;
        }
        cpu.Set(kR0, r0);
      } else if (idx == kSlotIdStrcmp) {
        const std::uint32_t a2 = r0, b2 = cpu.Get(kR1);
        std::uint32_t i = 0;
        for (;;) {
          const std::uint8_t ca = mem_ref.Ler8(a2 + i), cb2 = mem_ref.Ler8(b2 + i);
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
          const std::uint8_t b = mem_ref.Ler8(r0 + i);
          if (b == c2) { achou = r0 + i; break; }
          if (b == 0) break;
          ++i;
        }
        cpu.Set(kR0, achou);
      } else if (idx == kSlotIdMemmove) {
        const std::uint32_t src = cpu.Get(kR1), n = cpu.Get(kR2);
        std::vector<std::uint8_t> copia(n);   // copia intermediaria: o C permite sobreposicao
        mem_ref.LerBloco(src, copia.data(), n);
        mem_ref.EscreverBloco(r0, copia.data(), n);
        cpu.Set(kR0, r0);
      } else if (idx == kSlotIdStrtowstr) {
        // AECHAR *strtowstr(const char *pszIn, AECHAR *pDest, int nSize).
        // AECHAR e UTF-16; nSize e em CARACTERES, e a funcao termina o destino.
        const std::uint32_t destino = cpu.Get(kR1);
        const std::uint32_t tam = cpu.Get(kR2);
        std::uint32_t i = 0;
        for (; static_cast<int>(i) + 1 < static_cast<int>(tam); ++i) {
          const std::uint8_t c2 = mem_ref.Ler8(r0 + i);
          mem_ref.Escrever16(destino + i * 2, c2);
          if (c2 == 0) break;
        }
        if (static_cast<int>(i) + 1 >= static_cast<int>(tam) && destino != 0) {
          mem_ref.Escrever16(destino + (tam - 1) * 2, 0);
        }
        cpu.Set(kR0, destino);
      } else if (idx == kSlotIdGetAeeVersion) {
        // Devolve a versao, e escreve-a em *pVer quando ha ponteiro. 4.0.2
        // codificada como o SDK a codifica: AEE_VER(4,0,2).
        const std::uint32_t pver = cpu.Get(kR1);
        const std::uint32_t ver = 0x00400002u;
        if (pver != 0) mem_ref.Escrever32(pver, ver);
        cpu.Set(kR0, ver);
      } else if (idx == kSlotIdAeeGetRand) {
        // `aee_GetRand` -- gerador DETERMINISTA (principio P4). Um gerador do
        // sistema tornaria duas corridas diferentes, e o emulador deixaria de
        // ser reproduzivel -- que e o que sustenta todas as medicoes.
        static std::uint32_t semente = 0x12345678u;
        semente = semente * 1103515245u + 12345u;
        cpu.Set(kR0, (semente >> 16) & 0x7FFFu);
      } else if (idx == kSlotIdSetColor) {
        // `void SetColor(IDisplay *po, RGBVAL rgb)`. O Zeebo usa RGB565.
        g_fb.cor_atual = cpu.Get(kR1) & 0xFFFFu;
        cpu.Set(kR0, 0);
      } else if (idx == kSlotIdSetClipRect) {
        // `void SetClipRect(IDisplay *po, AEERect *prc)` -- prc nulo limpa o clip.
        const std::uint32_t prc = cpu.Get(kR1);
        if (prc == 0) {
          g_fb.clip[0] = 0; g_fb.clip[1] = 0; g_fb.clip[2] = kLargura; g_fb.clip[3] = kAltura;
        } else {
          g_fb.clip[0] = static_cast<std::uint32_t>(static_cast<std::int32_t>(mem_ref.Ler32(prc)));
          g_fb.clip[1] = static_cast<std::uint32_t>(static_cast<std::int32_t>(mem_ref.Ler32(prc + 4)));
          g_fb.clip[2] = mem_ref.Ler32(prc + 8);
          g_fb.clip[3] = mem_ref.Ler32(prc + 12);
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
        const std::uint32_t prc = cpu.Get(kR1);
        if (prc != 0) {
          const std::uint32_t x = mem_ref.Ler32(prc), y = mem_ref.Ler32(prc + 4);
          const std::uint32_t w = mem_ref.Ler32(prc + 8), h = mem_ref.Ler32(prc + 12);
          const std::uint32_t clrframe = cpu.Get(kR2), clrfill = cpu.Get(kR3);
          const std::uint32_t flags = mem_ref.Ler32(cpu.Get(kSP) + 0);
          // Os bits do `AEERectFlags`: DRAW = contorno, FILL = cheio.
          const bool contorno = (flags & 0x01u) != 0, cheio = (flags & 0x02u) != 0;
          if (cheio || contorno) {
            g_fb.cor_atual = cheio ? clrfill : clrframe;
            g_fb.Retangulo(x, y, w, h, cheio);
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
        const std::uint32_t x = mem_ref.Ler32(cpu.Get(kSP) + 0);
        const std::uint32_t y = mem_ref.Ler32(cpu.Get(kSP) + 4);
        const std::uint32_t prcfundo = mem_ref.Ler32(cpu.Get(kSP) + 8);
        if (prcfundo != 0) {
          // O fundo e pedido explicitamente: pinta-se com a cor actual antes.
          g_fb.Retangulo(mem_ref.Ler32(prcfundo), mem_ref.Ler32(prcfundo + 4),
                         mem_ref.Ler32(prcfundo + 8), mem_ref.Ler32(prcfundo + 12), true);
        }
        const std::uint32_t larg = (nchars > 0 ? nchars : 1) * 8;
        for (std::uint32_t i = 0; i < larg; ++i) g_fb.Ponto(static_cast<int>(x + i), static_cast<int>(y));
        ++g_textos;
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
        const std::int32_t cy = static_cast<std::int32_t>(mem_ref.Ler32(cpu.Get(kSP) + 0));
        const std::uint32_t origem = mem_ref.Ler32(cpu.Get(kSP) + 4);
        const std::int32_t xs = static_cast<std::int32_t>(mem_ref.Ler32(cpu.Get(kSP) + 8));
        const std::int32_t ys = static_cast<std::int32_t>(mem_ref.Ler32(cpu.Get(kSP) + 12));
        if (origem != 0 && cx > 0 && cy > 0) {
          for (std::int32_t j = 0; j < cy; ++j) {
            for (std::int32_t i = 0; i < cx; ++i) {
              const std::uint32_t u = static_cast<std::uint32_t>(xs + i);
              const std::uint32_t v = static_cast<std::uint32_t>(ys + j);
              g_fb.cor_atual =
                  mem_ref.Ler16(origem + (v * static_cast<std::uint32_t>(cx) + u) * 2);
              g_fb.Ponto(xd + i, yd + j);
            }
          }
          ++g_blits;
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
        const std::uint32_t ppidib = cpu.Get(kR1);
        const std::uint32_t prof = cpu.Get(kR2) & 0xFFu;
        const std::uint32_t w = cpu.Get(kR3) & 0xFFFFu;
        const std::uint32_t h = mem_ref.Ler32(cpu.Get(kSP) + 0) & 0xFFFFu;
        const std::uint32_t obj = kObjDibBase + g_dibs * 0x40;
        ++g_dibs;
        // O IDIB tem cabecalho proprio: dimensoes, profundidade, e o PASSAPORTE
        // de acesso aos pixels (`pData`), que o `IDIB_GetBuffer` devolve.
        mem_ref.Escrever32(obj + 0, g_vtable_bitmap);
        mem_ref.Escrever32(obj + 4, 1);
        mem_ref.Escrever32(obj + 8, 0);  // pData -- por atribuir
        mem_ref.Escrever32(obj + 12, w);
        mem_ref.Escrever32(obj + 16, h);
        mem_ref.Escrever32(obj + 20, prof);
        if (ppidib != 0) mem_ref.Escrever32(ppidib, obj);
        cpu.Set(kR0, 0);
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
        if (pascent != 0) mem_ref.Escrever32(pascent, static_cast<std::uint32_t>(asc));
        if (pdescent != 0) mem_ref.Escrever32(pdescent, static_cast<std::uint32_t>(desc));
        cpu.Set(kR0, 12);
      } else if (idx == kSlotIdMeasureText) {
        // `int MeasureTextEx(IDisplay *po, AEEFont nFont, const AECHAR *pcText,
        //                    int nChars, int nMaxWidth, int *pnFits)`.
        //
        // ASSINATURA CORRIGIDA: `pnFits` e o 6.o argumento, na pilha, e recebe a
        // largura que CABE. Largura DECLARADA de 8 px por caracter.
        const std::uint32_t n = cpu.Get(kR3);
        const std::uint32_t nmax = mem_ref.Ler32(cpu.Get(kSP) + 0);
        const std::uint32_t pfits = mem_ref.Ler32(cpu.Get(kSP) + 4);
        const std::uint32_t larg = (n > 0 ? n : 1) * 8;
        if (pfits != 0) {
          mem_ref.Escrever32(pfits, nmax == 0 ? larg : (larg < nmax ? larg : nmax));
        }
        cpu.Set(kR0, static_cast<std::uint32_t>(larg));
      } else if (idx == kSlotIdUpdate) {
        ++g_updates;
        cpu.Set(kR0, 0);
      } else if (idx == kSlotIdBacklight) {
        // `void Backlight(IDisplay *po, boolean bOn)`. Sem ecra fisico: conta.
        ++g_backlights;
        cpu.Set(kR0, 0);
      } else if (idx == kSlotIdSetTimer) {
        // `int SetTimer(IShell *po, AEECallback *pcb, int msecs)`. Guarda o
        // pedido; quem o cumpre e o laco, adiante.
        //
        // E o pedido de demanda mais alto do IShell, e faz sentido: em BREW o
        // laco de quadro do jogo vive AQUI -- o app arma um temporizador e o
        // proprio callback re-arma o seguinte. Sem isto nenhum jogo anda.
        g_timer.ativo = true;
        g_timer.callback = cpu.Get(kR1);
        g_timer.vence_em_ms = g_agora_ms + static_cast<std::int64_t>(cpu.Get(kR2));
        traco.Emitir(Area::Guarda, Nivel::Depuracao, "SET_TIMER",
                     "cb=0x" + std::to_string(g_timer.callback) + " em " +
                         std::to_string(cpu.Get(kR2)) + " ms");
        cpu.Set(kR0, kAeeSuccess);
      } else if (idx == kSlotIdGetUpTime) {
        cpu.Set(kR0, static_cast<std::uint32_t>(g_agora_ms));
      } else if (idx == kSlotIdGetNumButtons) {
        // `int GetNumberOfButtons(IHIDDevice *po)` -- IHIDDevice slot 7.
        //
        // O valor e DECLARADO, e diz-se que e declarado. A contagem real foi
        // medida na arvore antiga (docs/PAREAMENTO-DE-UIDS-MEDIDO.md) e o d-pad e
        // um controlo UNICO com UID proprio; o numero de BOTOES e a contagem que
        // o `hid_devices.cfg` do zeemu declara.
        // Nao tem argumentos: os registos que a bateria imprime sao residuais.
        cpu.Set(kR0, 14);
      } else if (idx == kSlotIdGetDest) {
        // `IBitmap *GetDestination(IDisplay *po)` -- IDisplay slot 16.
        // Devolve o bitmap que esta a receber o desenho. O jogo usa-o para saber
        // o TAMANHO da tela (via IBitmap::GetInfo) antes de calcular posicoes.
        const std::uint32_t obj = kObjDibBase + 0x300;
        mem_ref.Escrever32(obj + 0, g_vtable_bitmap);
        mem_ref.Escrever32(obj + 4, 1);
        mem_ref.Escrever32(obj + 8, 0);
        mem_ref.Escrever32(obj + 12, kLargura);
        mem_ref.Escrever32(obj + 16, kAltura);
        mem_ref.Escrever32(obj + 20, 16);
        g_destino = obj;
        cpu.Set(kR0, obj);
      } else if (idx == kSlotIdSetDest) {
        // `int SetDestination(IDisplay *po, IBitmap *pDst)` -- IDisplay slot 15.
        // So se ACEITA um bitmap nosso: aceitar um ponteiro qualquer poria o
        // desenho num sitio que nao existe.
        const std::uint32_t pdst = cpu.Get(kR1);
        if (pdst >= kObjDibBase && pdst < kObjDibBase + 0x1000) {
          g_destino = pdst;
          cpu.Set(kR0, 0);  // SUCCESS
        } else {
          cpu.Set(kR0, kAeeUnsupported);
        }
      } else if (idx == kSlotIdGetDeviceInfo) {
        // `void GetDeviceInfo(IShell *po, AEEDeviceInfo *pi)` -- IShell slot 4,
        // e a demanda MAIS ALTA do corpus: 18 titulos.
        //
        // O jogo le daqui o TAMANHO DO ECRA e a profundidade de cor, para calcular
        // posicoes e para decidir que superficies pode criar. Sem isto, 18 titulos
        // pediam-no e nao recebiam nada.
        //
        // 320x240 e 16 bits: os valores do ZEEBO, DECLARADOS como tal.
        const std::uint32_t pi = cpu.Get(kR1);
        if (pi != 0) {
          brew::AeeDeviceInfo di{};
          di.cx_screen = 320; di.cy_screen = 240;
          di.cx_alt_screen = 320; di.cy_alt_screen = 240;
          di.cx_scroll_bar = 10;
          di.w_encoding = 0;          // AEE_ENC_UNICODE
          di.w_menu_text_scroll = 30;
          di.n_color_depth = 16;
          di.unused2 = 0;
          di.w_menu_image_delay = 100;
          di.dw_ram = 0;              // deprecated no cabecalho
          di.b_alt_display = 0; di.b_flip = 0; di.b_vibrator = 0; di.b_ext_speaker = 0;
          di.b_vr = 0; di.b_pos_loc = 0; di.b_midi = 1; di.b_cmx = 0; di.b_pen = 1;
          di.dw_prompt_props = 0;
          di.w_key_close_app = 0; di.w_key_close_all_apps = 0;
          di.dw_lang = 0;             // AEE_LNG_ENGLISH
          di.w_struct_size = static_cast<std::uint16_t>(sizeof(brew::AeeDeviceInfo));
          di.dw_net_linger = 0; di.dw_sleep_defer = 0;
          di.w_max_path = 256;
          di.dw_platform_id = 0;
          const auto* b = reinterpret_cast<const std::uint8_t*>(&di);
          for (std::size_t k = 0; k < sizeof(brew::AeeDeviceInfo); ++k) {
            mem_ref.Escrever8(pi + static_cast<std::uint32_t>(k), b[k]);
          }
        }
        cpu.Set(kR0, 0);
      } else if (idx == kSlotIdRmDir) {
        // `int RmDir(IFileMgr *po, const char *pszDir)` -- IFileMgr slot 7.
        //
        // A VFS desta etapa e SO DE LEITURA, e e deliberado: um jogo que apague
        // um ficheiro do modulo destroi a reprodutibilidade. Recusa-se em voz
        // alta (principio P2) em vez de mentir com um sucesso que nao aconteceu.
        cpu.Set(kR0, kAeeUnsupported);
      } else if (idx == kSlotIdGetAppInstance) {
        // `void *GetAppInstance(void)` -- o ponteiro do applet, para o codigo que
        // nao tem o `po` a mao. Nao tem argumentos: os registos que a bateria
        // imprime sao RESIDUAIS, e foi por isso que quase persegui uma "fuga de
        // enderecos de saida para o guest" que nao existia.
        cpu.Set(kR0, g_applet);
      } else if (idx == kSlotIdQueryClass) {
        // `boolean QueryClass(IShell *po, AEECLSID cls, AEEAppInfo *pai)`.
        //
        // Responde se a classe existe, e preenche o `AEEAppInfo` quando ha
        // ponteiro. As classes que SEI criar sao as que o `QueryInterface` e o
        // `CreateInstance` ja servem; o resto devolve FALSE -- recusar, nao
        // mentir. Um `AEEAppInfo` a zeros com `TRUE` seria a versao em dados do
        // stub silencioso.
        const std::uint32_t cls = cpu.Get(kR1);
        const std::uint32_t pai = cpu.Get(kR2);
        const bool conhecida = (cls == kIidDisplay || cls == kIidFileMgr ||
                                cls == kIidHeap || cls == kIidFile || cls == kIidSound ||
                                cls == kIidGraphics || cls == kIidRootForm ||
                                cls == kIidHid || cls == kIidSqlMgr);
        if (pai != 0) {
          // AEEAppInfo: cls(0), pszName(4), pszIcon(8), dwIconSize(12), ...
          mem_ref.Escrever32(pai + 0, cls);
          mem_ref.Escrever32(pai + 4, 0);
          mem_ref.Escrever32(pai + 8, 0);
          mem_ref.Escrever32(pai + 12, 0);
          mem_ref.Escrever32(pai + 16, 0);
        }
        cpu.Set(kR0, conhecida ? 1u : 0u);
      } else if (idx == kSlotIdFmTest) {
        // `int Test(IFileMgr *po, const char *pszName)` -- devolve AEE_SUCCESS
        // se o ficheiro existe no sistema de ficheiros virtual.
        std::string nome;
        mem_ref.LerCadeia(cpu.Get(kR1), &nome, 512);
        const std::uint32_t existe = vfs_tem(nome) ? kAeeSuccess : kAeeFailed;
        traco.Emitir(Area::Brew, Nivel::Depuracao, "FM_TEST",
                                        nome + (existe == 0 ? " -> OK" : " -> MISS"));
        cpu.Set(kR0, existe);
      } else if (idx == kSlotIdFmFree) {
        // `uint32 GetFreeSpace(IFileMgr *po, uint32 *pdwTotal)`. Valor
        // DECLARADO: nao ha disco neste emulador, e inventar um espaco
        // plausivel e melhor do que devolver zero -- zero faria um jogo recusar
        // gravar. Fica registado como valor declarado.
        if (cpu.Get(kR1) != 0) mem_ref.Escrever32(cpu.Get(kR1), 0x00100000u);
        cpu.Set(kR0, 0x00080000u);
      } else if (idx == kSlotIdFmLastErr) {
        cpu.Set(kR0, 0);
      } else if (idx == kBaseDoSlot + 500) {
        // dbgprintf
        std::string msg;
        mem_ref.LerCadeia(r0, &msg, 512);
        traco.Emitir(Area::Brew, Nivel::Depuracao, "GUEST_DBGPRINTF", msg);
        cpu.Set(kR0, 0);
      } else if (idx >= kBaseDoSlot) {
        const std::uint32_t off = (idx - kBaseDoSlot) * 4;
        const char* conhecido = NomeDoSlot(off);
        char nome[64];
        if (conhecido != nullptr) {
          std::snprintf(nome, sizeof(nome), "AEEHelperFuncs[0x%03x] %s", off, conhecido);
        } else {
          std::snprintf(nome, sizeof(nome), "AEEHelperFuncs[0x%03x]", off);
        }
        char det2[128];
        std::snprintf(det2, sizeof(det2), "r0=0x%08x r1=0x%08x r2=0x%08x", r0, cpu.Get(kR1),
                      cpu.Get(kR2));
        traco.RegistarFalta(Area::Brew, nome, det2);
        cpu.Set(kR0, kAeeUnsupported);
        if (++saidas > 200) { *motivo = "parou_em_slot_nao_implementado"; return; }
      } else {
        traco.RegistarFalta(Area::Brew, "servico_sem_nome_idx" + std::to_string(idx),
                            "chamado com r0=0x" + std::to_string(r0));
        cpu.Set(kR0, kAeeUnsupported);
        if (++saidas > 200) { *motivo = "parou_em_slot_nao_implementado"; return; }
      }
      cpu.Set(kPC, lr);
      if (++saidas > 20000) { *motivo = "laco_de_saidas"; return; }
      continue;
    }
    if (pc < kBase || pc >= kBase + 0x01000000u) {
      *motivo = "saiu_do_modulo_para_0x" + std::to_string(pc);
      (void)pp_out;
      return;
    }
    // O LACO DE EVENTOS, em tempo VIRTUAL (principio P4): a cada passo avanca-se
    // 1 ms de tempo emulado, e um temporizador vencido e cumprido aqui.
    //
    // O callback de um `AEECallback` e um par `(funcao, contexto)` nos dois
    // primeiros campos. Chama-se com o contexto no r0, como o SDK define, e o
    // proprio callback re-arma o temporizador -- que e como um laco de quadro
    // se sustenta em BREW.
    ++g_agora_ms;
    if (g_timer.ativo && g_agora_ms >= g_timer.vence_em_ms && g_timer.callback != 0) {
      const std::uint32_t fn = mem_ref.Ler32(g_timer.callback);
      const std::uint32_t ctx = mem_ref.Ler32(g_timer.callback + 4);
      g_timer.ativo = false;
      if (fn >= kBase && fn < kBase + 0x01000000u) {
        ++*passos;
        const std::uint32_t pc_salvo = cpu.Get(kPC);
        const std::uint32_t lr_salvo = cpu.Get(kLR);
        cpu.Set(kLR, kSentinela);       // o retorno do callback volta para ca
        cpu.Set(kPC, fn);
        cpu.Set(kR0, ctx);
        continuar_no_laco = true;
        (void)pc_salvo; (void)lr_salvo;
      }
    }

    if (saidas > 200) { *motivo = "parou_em_slot_nao_implementado"; return; }
    cpu.Passo();
    ++*passos;
  }
  *motivo = "orcamento_esgotado";
}

Estado Medir(const Titulo& t, const std::string& dir) {
  Estado e;
  bool ok = false;
  const std::vector<std::uint8_t> imagem = Ler(dir + "/" + t.pasta + "/" + t.mod + ".mod", &ok);
  if (!ok) { e.motivo = "mod_ausente"; return e; }
  // O framebuffer e POR TITULO: um estado que passa de um titulo para o outro
  // tornaria a medida incomparavel -- que e o defeito de metodo mais repetido
  // desta sessao.
  g_fb = Framebuffer{};
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
    g_vfs = &guardado;
    if (!g_vfs_registado) {
      traco.Emitir(Area::Carga, Nivel::Informacao, "VFS",
                   std::to_string(ficheiros.size()) + " ficheiros do titulo registados");
      g_vfs_registado = true;
    }
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
  g_vtable_bitmap = s.Endereco(kVtableBitmap);
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
  const struct { std::uint32_t off; std::uint32_t saida; } kAjudantesLigados[] = {
      {0x68, 0},  // malloc -- tratado a parte, pelo alocador
      {0x6c, 1},  // free
      {kSlotDbgPrintf, kBaseDoSlot + 500},
      {kSlotStrlen, kSlotIdStrlen},
      {kSlotMemset, kSlotIdMemset},
      {kSlotStrcpy, kSlotIdStrcpy},
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
  };
  for (const auto& lig : kAjudantesLigados) {
    mem.Escrever32(kTabela + lig.off, s.Endereco(lig.saida));
  }
  const auto ja_tem = [&](std::uint32_t off) {
    for (const auto& lig : kAjudantesLigados) {
      if (lig.off == off) return true;
    }
    return false;
  };
  for (std::uint32_t off = 0; off < 117 * 4; off += 4) {
    if (off == 0x68 || off == 0x6c || ja_tem(off)) continue;
    // UM endereco de saida POR OFFSET, e nao um stub generico para todos.
    //
    // MOTIVO, medido: com um stub so, 44 titulos pediam algo e o registo dizia
    // "slot_de_saida_2" 44 vezes -- um numero sem nome. Com um endereco por
    // offset, a bateria diz QUAL funcao do sistema cada titulo pediu, e a lista
    // do que falta passa a ser ordenada por demanda em vez de por intuicao. E o
    // mesmo metodo que nomeou os slots de GL na arvore antiga.
    mem.Escrever32(kTabela + off, s.Endereco(kBaseDoSlot + off / 4));
  }

  // As vtables das interfaces ficam ACIMA da tabela de ajudantes, dentro da
  // mesma faixa de saida. Enderecos distintos por interface.
  const std::uint32_t kShell = 0x80020000u;
  ConstruirShell(mem, s, kShell, s.Endereco(kVtableShell), 64);

  // As interfaces que o shell entrega por QueryInterface.
  //
  // MEDIDO, e e o que decidiu a ordem desta etapa: 22 titulos pedem
  // `AEECLSID_DISPLAY` e 4 pedem `AEECLSID_FILEMGR`, ambos pelo slot 2 do IShell
  // (QueryInterface) com o IID no r1 e o ponteiro de saida no r2.
  ConstruirShell(mem, s, kObjDisplay, s.Endereco(kVtableDisplay), 64, kVtableDisplay);
  ConstruirShell(mem, s, kObjFileMgr, s.Endereco(kVtableFileMgr), 64, kVtableFileMgr);

  // Um objecto generico para as interfaces que ainda nao tem implementacao.
  //
  // MOTIVO, e e uma decisao de honestidade: quando o `QueryInterface` recusa, o
  // jogo desiste e a bateria nao aprende nada sobre ele. Quando devolve um
  // objecto cujos slots RECUSAM em voz alta, a bateria aprende QUAL metodo
  // daquela interface o jogo quer -- e a lista de demanda cresce em vez de
  // parar. O que NAO se faz e devolver sucesso com um objecto que finge
  // funcionar (principio P2).
  for (std::uint32_t k = 0; k < kNGenericos; ++k) {
    ConstruirShell(mem, s, ObjGenerico(k), s.Endereco(VtGenerico(k)), 64, VtGenerico(k));
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
      {kVtableShell, brew_slots::kShell_SetTimer, kSlotIdSetTimer},
      {kVtableShell, brew_slots::kShell_QueryClass, kSlotIdQueryClass},
      {kVtableShell, brew_slots::kShell_GetDeviceInfo, kSlotIdGetDeviceInfo},
      // IHIDDevice: slot 7 = GetNumberOfButtons
      {VtGenerico(5), brew_slots::kHIDDevice_GetNumberOfButtons, kSlotIdGetNumButtons},
      // IDisplay
      {kVtableDisplay, kDisGetFontMetrics, kSlotIdGetFontMetrics},
      {kVtableDisplay, kDisMeasureTextEx, kSlotIdMeasureText},
      {kVtableDisplay, kDisDrawText, kSlotIdDrawText},
      {kVtableDisplay, kDisDrawRect, kSlotIdDrawRect},
      {kVtableDisplay, kDisBitBlt, kSlotIdBitBlt},
      {kVtableDisplay, kDisSetColor, kSlotIdSetColor},
      {kVtableDisplay, kDisSetClipRect, kSlotIdSetClipRect},
      {kVtableDisplay, kDisUpdate, kSlotIdUpdate},
      {kVtableDisplay, brew_slots::kDisplay_Backlight, kSlotIdBacklight},
      {kVtableDisplay, kDisCreateDIBitmap, kSlotIdCreateDIBitmap},
      {kVtableDisplay, brew_slots::kDisplay_SetDestination, kSlotIdSetDest},
      {kVtableDisplay, brew_slots::kDisplay_GetDestination, kSlotIdGetDest},
      // IFileMgr
      {kVtableFileMgr, kFmTest, kSlotIdFmTest},
      {kVtableFileMgr, kFmGetFreeSpace, kSlotIdFmFree},
      {kVtableFileMgr, kFmGetLastError, kSlotIdFmLastErr},
      {kVtableFileMgr, 7, kSlotIdRmDir},
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
    if (w.slot < 2) {
      std::fprintf(stderr, "CABLAGEM RECUSADA: slot %u do objecto 0x%08x e da IBase\n",
                   w.slot, w.vt);
      std::abort();
    }
    // O FIM da vtable deste objecto, e nao a base da PRIMEIRA vtable generica.
    // A guarda disparou na primeira versao porque comparava com `kVtableGenericoBase`
    // -- e cada vtable generica tem 64 slots, logo o fim de uma e o inicio da
    // seguinte, nao a base da serie. **A guarda estava certa no proposito e
    // errada na conta.**
    const std::uint32_t fim = (w.vt >= kVtableGenericoBase)
                                  ? VtGenerico((w.vt - kVtableGenericoBase) / 64u + 1u)
                                  : kVtableGenericoBase;
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

  cpu.Repor(kBase, kPilha);
  cpu.Set(kR0, kShell);                 // o IShell minimo mas real
  cpu.Set(kR2, kPPMod);
  cpu.Set(kLR, kSentinela);
  CorrerFase(cpu, al, mem, traco, kLimite, &e.passos_carga, &e.motivo, kPPMod);
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
  cpu.Set(kR2, static_cast<std::uint32_t>(std::strtoul(t.clsid.c_str(), nullptr, 0)));
  cpu.Set(kR3, kPPObj);
  cpu.Set(kLR, kSentinela);
  // O orcamento de tempo comeca a contar AQUI, e cobre a fase de criacao, que e
  // a que passou a ser cara.
  const auto t0 = std::chrono::steady_clock::now();
  std::string motivo_create;
  CorrerFase(cpu, al, mem, traco, kLimite, &e.passos_create, &motivo_create, kPPObj);
  e.recusadas = cpu.InstruscoesRecusadas();
  g_applet = mem.Ler32(kPPObj);  // para o `GetAppInstance`
  e.create = mem.Ler32(kPPObj) != 0;
  e.motivo += " | create:" + motivo_create;
  if (!e.create) e.motivo += "_sem_applet";
  for (const auto& par : traco.ContagemFaltas()) e.faltas[par.first] = par.second;
  e.pixels = g_fb.escritos;
  e.cores = g_fb.CoresDistintas();
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
              "modulo", "vtable", "carga_p", "cria_p", "PIXELS", "CORES", "motivo");
  int carregam = 0, com_modulo = 0, com_applet = 0;
  // Nome -> conjunto de detalhes distintos vistos (para a lista final dizer os
  // ARGUMENTOS, e nao so a contagem).
  std::map<std::string, std::uint64_t> faltas_totais;
  std::map<std::string, std::map<std::string, std::uint64_t>> faltas_detalhe;
  std::string json = "[\n";
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
    std::printf("%-16s %-8u %-6s %-6s %-6s %8" PRIu64 " %8" PRIu64 " %8u %5u  %s\n", t.mod.c_str(),
                e.tamanho, e.carga ? "sim" : "NAO", e.modulo ? "sim" : "NAO",
                e.vtable ? "sim" : "NAO", e.passos_carga, e.passos_create, e.pixels, e.cores,
                e.motivo.c_str());
    json += "  {\"mod\":\"" + t.mod + "\",\"pasta\":\"" + t.pasta + "\",\"tamanho\":" +
            std::to_string(e.tamanho) + ",\"carga\":" + (e.carga ? "true" : "false") +
            ",\"modulo\":" + (e.modulo ? "true" : "false") +
            ",\"vtable\":" + (e.vtable ? "true" : "false") +
            ",\"applet\":" + (e.create ? "true" : "false") +
            ",\"passos_carga\":" + std::to_string(e.passos_carga) +
            ",\"passos_create\":" + std::to_string(e.passos_create) +
            ",\"recusadas\":" + std::to_string(e.recusadas) +
            ",\"motivo\":\"" + e.motivo + "\"" +
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
  json += "]\n";

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

