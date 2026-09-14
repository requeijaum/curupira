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
constexpr std::uint32_t kSlotIdCreateDIBitmap = 1538;
constexpr std::uint32_t kObjDibBase = 0x80050000u;
// Os slots do IDisplay, na ordem que `platform/ui/inc/AEEIDisplay.h` declara em
// `INHERIT_IDisplay`. Lido campo a campo.
enum : std::uint32_t {
  kDisQueryInterface = 2,
  kDisGetFontMetrics = 3,
  kDisMeasureTextEx = 4,
  kDisDrawText = 5,
  kDisDrawRect = 6,
  kDisBitBlt = 7,
  kDisUpdate = 8,
  kDisSetColor = 11,
  kDisCreateDIBitmap = 14,
  kDisSetClipRect = 19,
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
std::uint32_t g_vtable_bitmap = 0;
// Os slots do IShell, na ordem que `platform/system/inc/AEEIShell.h` declara em
// `INHERIT_IShell`. Lido campo a campo, e nao copiado.
enum : std::uint32_t {
  kSheCreateInstance = 3,
  kSheSetTimer = 12,     // <<< o pedido de demanda mais alto depois do QI
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
  while (*passos < limite) {
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
        if (ppo != 0) mem_ref.Escrever32(ppo, devolver);
        cpu.Set(kR0, devolver != 0 ? kAeeSuccess : kAeeClassNotSupported);
        if (devolver == 0) {
          char det[96];
          std::snprintf(det, sizeof(det), "iid=0x%08x ppo=0x%08x", iid, ppo);
          traco.RegistarFalta(Area::Brew, "IShell::QueryInterface IID desconhecido", det);
        }
      } else if (idx >= kBaseDoShell) {
        // O NOME tem de dizer de QUE interface e o slot. Um so "IShell::slot"
        // para tudo dava `IShell::slot4004` para um metodo do IDisplay -- numero
        // sem nome outra vez, e ja foi esse o defeito que me fez perder uma
        // ronda inteira a olhar para a lista errada.
        char nome[64], det[128];
        const char* iface = "IShell";
        std::uint32_t slot = 0;
        if (idx >= kVtableFileMgr) { iface = "IFileMgr"; slot = idx - kVtableFileMgr; }
        else if (idx >= kVtableDisplay) { iface = "IDisplay"; slot = idx - kVtableDisplay; }
        else { iface = "IShell"; slot = idx - kBaseDoShell; }
        std::snprintf(nome, sizeof(nome), "%s::slot%u", iface, slot);
        // Os ARGUMENTOS no detalhe: para o QueryInterface (slot 2) o r1 e o IID
        // pedido, e sem ele nao se sabe o que responder. Foi assim que se
        // percebeu, na arvore antiga, quais das interfaces eram as mesmas por
        // dois nomes diferentes.
        std::snprintf(det, sizeof(det), "r0=0x%08x r1=0x%08x r2=0x%08x", r0, cpu.Get(kR1),
                      cpu.Get(kR2));
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
        // `void DrawRect(IDisplay *po, AEERect *prc)` -- CONTORNO, nao
        // preenchido. Quem preenche e o DrawRect do IGraphics/draw, que e outra
        // interface; confundir os dois e um erro classico de quem le o SDK por
        // alto.
        const std::uint32_t prc = cpu.Get(kR1);
        if (prc != 0) {
          g_fb.Retangulo(mem_ref.Ler32(prc), mem_ref.Ler32(prc + 4), mem_ref.Ler32(prc + 8),
                         mem_ref.Ler32(prc + 12), /*preencher=*/false);
        }
        cpu.Set(kR0, 0);
      } else if (idx == kSlotIdDrawText) {
        // `void DrawText(IDisplay *po, const AECHAR *pText, int nChars, AEERect *prc,
        //                uint32 flags)`.
        //
        // Aqui NAO se rasteriza texto: nao ha fonte carregada. O que se faz e
        // contar, e escrever uma barra com a cor actual na linha de base -- para
        // a medida "pixels escritos" nao ficar a zero por causa do texto. Fica
        // declarado como aproximacao.
        const std::uint32_t ptext = cpu.Get(kR1);
        const std::uint32_t prc = cpu.Get(kR3);
        std::uint32_t larg = 0;
        if (prc != 0) {
          const std::uint32_t x = mem_ref.Ler32(prc), y = mem_ref.Ler32(prc + 4);
          larg = mem_ref.Ler32(prc + 8);
          if (larg == 0) larg = 8;
          for (std::uint32_t i = 0; i < larg; ++i) g_fb.Ponto(static_cast<int>(x + i), static_cast<int>(y));
        }
        (void)ptext;
        ++g_textos;
        cpu.Set(kR0, 0);
      } else if (idx == kSlotIdBitBlt) {
        // `void BitBlt(IDisplay *po, IBitmap *pib, int xDst, int yDst, int dx,
        //              int dy, int nWidth, int nHeight)`.
        // Copia do bitmap de origem para o framebuffer. O bitmap tem cabecalho
        // nosso (ver CreateDIBitmap): largura, altura, e os pixels em RGB565.
        const std::uint32_t pib = cpu.Get(kR1);
        if (pib != 0) {
          const std::uint32_t origem = mem_ref.Ler32(pib + 8);
          const std::uint32_t bmp_larg = mem_ref.Ler32(pib + 12);
          const std::uint32_t bmp_alt = mem_ref.Ler32(pib + 16);
          const std::int32_t xd = static_cast<std::int32_t>(cpu.Get(kR2));
          const std::int32_t yd = static_cast<std::int32_t>(cpu.Get(kR3));
          const std::uint32_t dw = mem_ref.Ler32(cpu.Get(kSP) + 0);
          const std::uint32_t dh = mem_ref.Ler32(cpu.Get(kSP) + 4);
          const std::uint32_t nw = mem_ref.Ler32(cpu.Get(kSP) + 8);
          const std::uint32_t nh = mem_ref.Ler32(cpu.Get(kSP) + 12);
          if (origem != 0 && bmp_larg != 0 && bmp_alt != 0 && nw != 0 && nh != 0) {
            for (std::uint32_t j = 0; j < nh; ++j) {
              for (std::uint32_t i = 0; i < nw; ++i) {
                const std::uint32_t u = (dw + i) % bmp_larg, v = (dh + j) % bmp_alt;
                g_fb.cor_atual = mem_ref.Ler16(origem + (v * bmp_larg + u) * 2);
                g_fb.Ponto(static_cast<int>(xd + static_cast<std::int32_t>(i)),
                           static_cast<int>(yd + static_cast<std::int32_t>(j)));
              }
            }
            ++g_blits;
          }
        }
        cpu.Set(kR0, 0);
      } else if (idx == kSlotIdCreateDIBitmap) {
        // `IBitmap *CreateDIBitmap(IDisplay *po, const AEEBitmapInfo *pbi, void *pData)`.
        // Devolve um bitmap nosso: cabecalho `{vtable, ...}` + largura, altura e
        // o endereco dos pixels no guest. Os pixels ficam no formato do SDK e o
        // BitBlt le-os de la.
        const std::uint32_t pbi = cpu.Get(kR1);
        const std::uint32_t pdata = cpu.Get(kR2);
        const std::int32_t cx = static_cast<std::int16_t>(mem_ref.Ler16(pbi + 4));
        const std::int32_t cy = static_cast<std::int16_t>(mem_ref.Ler16(pbi + 6));
        const std::uint32_t obj = kObjDibBase + g_dibs * 0x40;
        ++g_dibs;
        mem_ref.Escrever32(obj + 0, g_vtable_bitmap);
        mem_ref.Escrever32(obj + 4, 1);
        mem_ref.Escrever32(obj + 8, pdata);
        mem_ref.Escrever32(obj + 12, static_cast<std::uint32_t>(cx > 0 ? cx : 1));
        mem_ref.Escrever32(obj + 16, static_cast<std::uint32_t>(cy > 0 ? cy : 1));
        cpu.Set(kR0, obj);
      } else if (idx == kSlotIdGetFontMetrics) {
        // `void GetFontMetrics(IDisplay *po, AEEFontMetrics *pfm)`. Valores
        // DECLARADOS de uma fonte de 12 px -- o jogo precisa de numeros para
        // calcular posicoes, e zero faria tudo colapsar numa linha.
        const std::uint32_t pfm = cpu.Get(kR1);
        if (pfm != 0) {
          mem_ref.Escrever32(pfm + 0, 0);   // nAscent
          mem_ref.Escrever32(pfm + 4, 0);   // nDescent
          mem_ref.Escrever16(pfm + 8, 12);  // cy
        }
        cpu.Set(kR0, 0);
      } else if (idx == kSlotIdMeasureText) {
        // `void MeasureTextEx(IDisplay *po, const AECHAR *pText, int nChars,
        //                     AEERect *prc)`. Devolve uma largura DECLARADA: 8 px
        // por caracter, que e a fonte de 12 px medida acima.
        const std::uint32_t prc = cpu.Get(kR3);
        const std::uint32_t n = cpu.Get(kR2);
        if (prc != 0) {
          mem_ref.Escrever32(prc + 8, (n > 0 ? n : 1) * 8);
          mem_ref.Escrever32(prc + 12, 12);
        }
        cpu.Set(kR0, 0);
      } else if (idx == kSlotIdUpdate) {
        ++g_updates;
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
  mem.Escrever32(kTabela + 0x68, s.Endereco(0));  // malloc
  mem.Escrever32(kTabela + 0x6c, s.Endereco(1));  // free
  // `dbgprintf` (0x09c) passa a ser SERVIDO: e o slot mais pedido depois do
  // malloc, medido (16 titulos). Le a cadeia de formato do guest e escreve-a.
  // Nao interpreta os `%` -- o texto cru ja diz de que titulo se trata.
  mem.Escrever32(kTabela + kSlotDbgPrintf, s.Endereco(kBaseDoSlot + 500));
  mem.Escrever32(kTabela + kSlotStrlen, s.Endereco(kSlotIdStrlen));
  mem.Escrever32(kTabela + kSlotMemset, s.Endereco(kSlotIdMemset));
  mem.Escrever32(kTabela + kSlotStrcpy, s.Endereco(kSlotIdStrcpy));
  mem.Escrever32(kTabela + kSlotStrcmp, s.Endereco(kSlotIdStrcmp));
  mem.Escrever32(kTabela + kSlotStrchr, s.Endereco(kSlotIdStrchr));
  mem.Escrever32(kTabela + kSlotMemmove, s.Endereco(kSlotIdMemmove));
  mem.Escrever32(kTabela + kSlotStrtowstr, s.Endereco(kSlotIdStrtowstr));
  mem.Escrever32(kTabela + kSlotGetAeeVersion, s.Endereco(kSlotIdGetAeeVersion));
  mem.Escrever32(kTabela + kSlotAeeGetRand, s.Endereco(kSlotIdAeeGetRand));
  // TODOS os outros slots da tabela recebem um endereco que RECUSA em voz alta,
  // em vez de ficarem a ZERO.
  //
  // MEDIDO, e foi a medicao que mudou o rumo: com os slots a zero, 61 dos 62
  // titulos saem do modulo com `saiu_do_modulo_para_0x0` -- o `bx` do modulo cai
  // em memoria nula e nao ha nada a aprender dali. Com um stub que recusa, o
  // pedido fica REGISTADO com o nome do slot, e a bateria diz o que cada titulo
  // precisa em vez de dizer que saltou para zero.
  //
  // E o principio P2 do desenho: stub silencioso e proibido. Aqui o "silencio"
  // era literalmente o endereco zero.
  // QUEM JA TEM IMPLEMENTACAO. O laco abaixo enche o resto com o stub que
  // recusa -- e tem de SALTAR estes, senao sobrescreve-os.
  //
  // Esta lista existe por causa do erro mais reincidente desta sessao, que
  // apareceu QUATRO vezes: um passo generico a atropelar trabalho especifico. As
  // tres primeiras foram na ordem dos testes de descodificacao; esta foi um laco
  // de preenchimento a apagar implementacoes ja escritas -- e o sintoma era a
  // bateria continuar a dizer "falta strlen" com o `strlen` escrito e a
  // funcionar. Com uma lista explicita, o erro passa a ser impossivel por
  // construcao, em vez de depender de a ordem estar certa.
  const std::uint32_t implementados[] = {
      kSlotMemmove, kSlotMemset, kSlotStrcpy, kSlotStrcmp, kSlotStrlen, kSlotStrchr,
      kSlotStrtowstr, kSlotGetAeeVersion, kSlotAeeGetRand, kSlotDbgPrintf,
  };
  const auto ja_tem = [&](std::uint32_t off) {
    for (std::uint32_t x : implementados) {
      if (x == off) return true;
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
  // Os slots do IFileMgr que o corpus pede, e que tem implementacao.
  mem.Escrever32(s.Endereco(kVtableFileMgr + kFmTest), s.Endereco(kSlotIdFmTest));
  mem.Escrever32(s.Endereco(kVtableFileMgr + kFmGetFreeSpace), s.Endereco(kSlotIdFmFree));
  mem.Escrever32(s.Endereco(kVtableFileMgr + kFmGetLastError), s.Endereco(kSlotIdFmLastErr));
  // Os slots do IDisplay que o corpus pede, e que tem implementacao.
  //
  // Escreve-se na MEMORIA DA VTABLE -- `mem[vtable + slot*4]` -- e nao no
  // endereco de saida. E o mesmo cuidado do SetTimer, e o mesmo erro que ja me
  // apanhou uma vez.
  //
  // Este bloco chegou a NAO SER APLICADO sem eu notar: a substituicao de texto
  // falhou em silencio e eu "verifiquei" com um `grep -c` que contava
  // `kVtableDisplay` -- que aparece nas linhas do `ConstruirShell` de qualquer
  // maneira. **Uma verificacao que passa sem a mudanca nao e verificacao.** Dai
  // o `assert` acima, e o assert de leitura abaixo.
  mem.Escrever32(s.Endereco(kVtableDisplay) + kDisGetFontMetrics * 4, s.Endereco(kSlotIdGetFontMetrics));
  mem.Escrever32(s.Endereco(kVtableDisplay) + kDisMeasureTextEx * 4, s.Endereco(kSlotIdMeasureText));
  mem.Escrever32(s.Endereco(kVtableDisplay) + kDisDrawText * 4, s.Endereco(kSlotIdDrawText));
  mem.Escrever32(s.Endereco(kVtableDisplay) + kDisDrawRect * 4, s.Endereco(kSlotIdDrawRect));
  mem.Escrever32(s.Endereco(kVtableDisplay) + kDisBitBlt * 4, s.Endereco(kSlotIdBitBlt));
  mem.Escrever32(s.Endereco(kVtableDisplay) + kDisSetColor * 4, s.Endereco(kSlotIdSetColor));
  mem.Escrever32(s.Endereco(kVtableDisplay) + kDisSetClipRect * 4, s.Endereco(kSlotIdSetClipRect));
  mem.Escrever32(s.Endereco(kVtableDisplay) + kDisUpdate * 4, s.Endereco(kSlotIdUpdate));
  mem.Escrever32(s.Endereco(kVtableDisplay) + kDisCreateDIBitmap * 4, s.Endereco(kSlotIdCreateDIBitmap));

  const auto carga = CarregarMod(mem, imagem, kBase, kTabela, &traco);
  if (!carga.ok) { e.motivo = "carga_recusada:" + carga.motivo; return e; }
  e.carga = true;

  cpu.Repor(kBase, kPilha);
  cpu.Set(kR0, kShell);                 // o IShell minimo mas real
  cpu.Set(kR2, kPPMod);
  cpu.Set(kLR, kSentinela);
  CorrerFase(cpu, al, mem, traco, kLimite, &e.passos_carga, &e.motivo, kPPMod);
  e.recusadas = cpu.InstruscoesRecusadas();

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
  std::string motivo_create;
  CorrerFase(cpu, al, mem, traco, kLimite, &e.passos_create, &motivo_create, kPPObj);
  e.recusadas = cpu.InstruscoesRecusadas();
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
            ",\"blits\":" + std::to_string(e.blits) + "},\n";
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

