// Sonda do WIDGET: mede o que UM titulo pede ao `IRootForm` na fase do
// `EVT_APP_START`, e se o pedido foi SERVIDO.
//
// PORQUE ESTA SONDA EXISTE, e o defeito de instrumento que a obriga.
//
// A bateria (`tools/bateria.cpp`) guarda a contagem de faltas A MEIO: faz
// `for (par : traco.ContagemFaltas()) e.faltas[par.first] = par.second;` DEPOIS
// do `create` e ANTES do `EVT_APP_START`. Tudo o que um titulo pede no arranque
// -- que e onde TODA a construcao de interface acontece -- nao entra nem na linha
// da tabela nem no JSON. **Um instrumento que nao regista o degrau que interessa
// mede o degrau que estava a jeito.**
//
// Foi assim que a lista de demanda do projecto ficou sem o `IRootForm`: os tres
// pedidos do `tectoy` (medidos com o traco, `IRootForm::slot3` x3) nunca
// chegaram a lista.
//
// O QUE ESTA SONDA FAZ, e o que NAO faz:
//   - corre UM titulo, com a MESMA sequencia da bateria (carga, CreateInstance,
//     EVT_APP_START);
//   - separa as faltas POR FASE, e imprime as da fase do arranque com o nome e os
//     argumentos;
//   - chama as faltas cujo nome comeca por `IRootForm` de "pedido de interface";
//   - SAI COM 1 quando ha um pedido de interface NAO SERVIDO, e com 0 quando nao
//     ha. E o comando que fica VERMELHO primeiro e VERDE depois.
//
// Uso: zb2_sonda_widget <corpus.json> <dir_dos_mods> <mod> [saida.json]
//
// O JSON de saida e OPCIONAL e tem o mesmo formato de uma ficha da bateria, para
// o `tools/comparar` o poder ler sem uma segunda gramatica.

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "core/brew/ajudantes.h"
#include "core/brew/despacho.h"
#include "core/brew/interface.h"
#include "core/cpu/arm_interpreter.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"
#include "core/carga/mod.h"

using namespace zb2;

namespace {

// A BASE DO MODULO E ZERO -- MEDIDA (`tests/mod_base_test.cpp`).
constexpr std::uint32_t kBase = 0x00000000u;
constexpr std::uint32_t kPilha = 0x80080000u;
constexpr std::uint32_t kHeap = 0x80200000u;
constexpr std::uint32_t kHeapTam = 0x00C00000u;
constexpr std::uint32_t kTabela = 0x80010000u;
constexpr std::uint32_t kPPMod = 0x00090000u;
constexpr std::uint32_t kPPObj = 0x00090010u;
constexpr std::uint32_t kSentinela = 0xFFFFFFF0u;
constexpr std::uint32_t kBaseDasEntradas = 20000;
constexpr std::uint32_t kAppStart = 0x000A0000u + 0x1000u;
constexpr std::uint64_t kLimite = 4000000ull;

struct Titulo {
  std::string pasta, mod, clsid;
};

std::vector<Titulo> LerCorpus(const std::string& caminho) {
  std::vector<Titulo> out;
  std::ifstream f(caminho);
  if (!f) return out;
  std::stringstream ss;
  ss << f.rdbuf();
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

std::uint64_t TotalDeFaltas(const Traco& t) {
  std::uint64_t n = 0;
  for (const auto& par : t.ContagemFaltas()) n += par.second;
  return n;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) {
    std::fprintf(stderr, "uso: %s <corpus.json> <dir_dos_mods> <mod> [saida.json]\n", argv[0]);
    return 2;
  }
  const std::vector<Titulo> todos = LerCorpus(argv[1]);
  Titulo t;
  bool achou = false;
  for (const auto& x : todos) {
    if (x.mod == argv[3]) { t = x; achou = true; break; }
  }
  if (!achou) {
    std::fprintf(stderr, "titulo '%s' nao esta no corpus %s\n", argv[3], argv[1]);
    return 2;
  }

  const std::string dir = argv[2];
  std::ifstream f(dir + "/" + t.pasta + "/" + t.mod + ".mod", std::ios::binary);
  if (!f) {
    std::fprintf(stderr, "nao consegui ler %s/%s/%s.mod\n", argv[2], t.pasta.c_str(),
                 t.mod.c_str());
    return 2;
  }
  const std::vector<std::uint8_t> imagem((std::istreambuf_iterator<char>(f)),
                                         std::istreambuf_iterator<char>());

  Tempo tempo;
  Traco traco("sonda_widget", &tempo);
  DestinoMemoria dm;
  traco.JuntarDestino(&dm);
  Memoria mem(&traco);
  mem.EscritorUnico("cpu");
  ArmInterpreter cpu(mem, &traco);
  Alocador al(mem, kHeap, kHeapTam, &traco);

  Saidas s;
  s.base = 0xF0000000u;
  s.passo = 4;
  s.quantos = 100000;
  s.ativa = true;
  cpu.ConfigurarSaidas(s);

  zb2::brew::Vfs vfs;
  vfs.Registar(dir + "/" + t.pasta);
  zb2::brew::Despacho despacho(mem, traco, al, vfs);
  despacho.InstalarAjudantes(s, kTabela);
  (void)despacho.InstalarEntrada(s, kBaseDasEntradas);
  despacho.DefinirFaixaDoModulo(kBase, static_cast<std::uint32_t>(imagem.size()));
  despacho.TelaRef().Limpar();
  despacho.SituarTitulo(dir, t.pasta);
  despacho.DefinirVtableBitmap(s.Endereco(zb2::brew::kVtableBitmap));
  despacho.DefinirVtableFicheiro(s.Endereco(zb2::brew::kVtableFileObj));

  // OS OBJECTOS DO SHELL, e a cablagem. E a MESMA tabela da bateria
  // (`tools/bateria.cpp`, `kWire`): um objecto por interface, e um endereco de
  // saida por slot.
  zb2::brew::ConstruirObjeto(mem, s, 0x80020000u, s.Endereco(zb2::brew::kVtableShell),
                             zb2::brew::kSlotsPorVtable, zb2::brew::kBaseDoShell);
  zb2::brew::ConstruirObjeto(mem, s, zb2::brew::kObjDisplay,
                             s.Endereco(zb2::brew::kVtableDisplay), 64,
                             zb2::brew::kVtableDisplay);
  zb2::brew::ConstruirObjeto(mem, s, zb2::brew::kObjFileMgr,
                             s.Endereco(zb2::brew::kVtableFileMgr), 64,
                             zb2::brew::kVtableFileMgr);
  for (std::uint32_t k = 0; k < zb2::brew::kNGenericos; ++k) {
    zb2::brew::ConstruirObjeto(mem, s, zb2::brew::ObjGenerico(k),
                               s.Endereco(zb2::brew::VtGenerico(k)), 64,
                               zb2::brew::VtGenerico(k));
  }
  // A CABLAGEM DOS SLOTS IMPLEMENTADOS. E a MESMA tabela de `tools/bateria.cpp`
  // (`kWire`), e ela tem de estar aqui: sem ela a sonda reportaria como "falta" um
  // metodo que o despacho ATENDE -- um pedido servido pela bateria e nao pela
  // sonda. **Um instrumento que nao reproduz a cablagem da ferramenta mede a
  // ferramenta errada.** Os indices de saida sao os mesmos numeros; a duplicacao
  // e conhecida e esta declarada no relatorio.
  const struct { std::uint32_t vt; std::uint32_t slot; std::uint32_t saida; } kWire[] = {
      {zb2::brew::kVtableShell, brew_slots::kShell_SetTimer, 1520},
      {zb2::brew::kVtableShell, brew_slots::kShell_QueryClass, 1541},
      {zb2::brew::kVtableShell, brew_slots::kShell_GetDeviceInfo, 1549},
      {zb2::brew::kVtableShell, brew_slots::kShell_CancelTimer, 1552},
      {zb2::brew::kVtableShell, brew_slots::kShell_FreeResData, 1563},
      {zb2::brew::kVtableShell, brew_slots::kShell_CheckPrivLevel, 1564},
      {zb2::brew::kVtableDisplay, brew_slots::kDisplay_GetDeviceBitmap, 1550},
      {zb2::brew::kVtableDisplay, brew_slots::kDisplay_GetClipRect, 1551},
      {zb2::brew::kVtableDisplay, brew_slots::kDisplay_GetFontMetrics, 1530},
      {zb2::brew::kVtableDisplay, brew_slots::kDisplay_MeasureTextEx, 1531},
      {zb2::brew::kVtableDisplay, brew_slots::kDisplay_DrawText, 1532},
      {zb2::brew::kVtableDisplay, brew_slots::kDisplay_DrawRect, 1533},
      {zb2::brew::kVtableDisplay, brew_slots::kDisplay_BitBlt, 1534},
      {zb2::brew::kVtableDisplay, brew_slots::kDisplay_SetColor, 1535},
      {zb2::brew::kVtableDisplay, brew_slots::kDisplay_SetClipRect, 1536},
      {zb2::brew::kVtableDisplay, brew_slots::kDisplay_Update, 1537},
      {zb2::brew::kVtableDisplay, brew_slots::kDisplay_Backlight, 1542},
      {zb2::brew::kVtableDisplay, brew_slots::kDisplay_CreateDIBitmap, 1538},
      {zb2::brew::kVtableDisplay, brew_slots::kDisplay_SetDestination, 1546},
      {zb2::brew::kVtableDisplay, brew_slots::kDisplay_GetDestination, 1545},
      {zb2::brew::VtGenerico(6), brew_slots::kSQLMgr_Open, 1553},
      {zb2::brew::VtGenerico(0), brew_slots::kHeap1_Lock, 1562},
      {zb2::brew::kVtableFileMgr, brew_slots::kFileMgr_OpenFile, 1554},
      {zb2::brew::kVtableFileObj, brew_slots::kIAStream_Read, 1555},
      {zb2::brew::kVtableFileObj, brew_slots::kIFile_Seek, 1556},
      {zb2::brew::kVtableFileObj, brew_slots::kIFile_GetInfo, 1557},
      {zb2::brew::kVtableFileObj, brew_slots::kIFile_Write, 1559},
  };
  for (const auto& w : kWire) {
    mem.Escrever32(s.Endereco(w.vt) + w.slot * 4, s.Endereco(w.saida));
  }

  const zb2::ResultadoDaCarga carga = CarregarMod(mem, imagem, kBase, kTabela, &traco);
  if (!carga.ok) {
    std::fprintf(stderr, "carga recusada: %s\n", carga.motivo.c_str());
    return 2;
  }
  despacho.DefinirFaixaDoModulo(kBase, carga.tamanho);
  cpu.Repor(kBase, kPilha);
  cpu.Set(kR0, 0x80020000u);
  cpu.Set(kR2, kPPMod);
  cpu.Set(kLR, kSentinela);
  const zb2::brew::ResultadoFase rc = despacho.Correr(cpu, kLimite, kPPMod);
  const std::uint32_t modulo = mem.Ler32(kPPMod);

  // ---- A FRONTEIRA: o que foi pedido ATE AQUI, e o que for pedido no arranque.
  const std::map<std::string, std::uint64_t> faltas_antes = traco.ContagemFaltas();
  const std::uint64_t total_antes = TotalDeFaltas(traco);

  std::uint64_t passos_start = 0;
  std::string motivo_start = "sem_applet";
  std::uint32_t applet = 0;
  if (modulo != 0) {
    const std::uint32_t vtable = mem.Ler32(modulo);
    const std::uint32_t ci = mem.Ler32(vtable + 8);
    cpu.Repor(ci, kPilha);
    cpu.Set(kR0, modulo);
    cpu.Set(kR1, 0x80020000u);
    cpu.Set(kR2, static_cast<std::uint32_t>(std::strtoul(t.clsid.c_str(), nullptr, 0)));
    cpu.Set(kR3, kPPObj);
    cpu.Set(kLR, kSentinela);
    (void)despacho.Correr(cpu, kLimite, kPPObj);
    applet = mem.Ler32(kPPObj);
    if (applet != 0) {
      // `AEEAppStart`, transcrita de `platform/system/inc/AEEAppStart.h`: vai no
      // `dwParam` (r3), com a `pDisplay` em +8 e o `rc` em +12.
      for (std::uint32_t k = 0; k < 32; ++k) mem.Escrever8(kAppStart + k, 0);
      mem.Escrever32(kAppStart + 4,
                     static_cast<std::uint32_t>(std::strtoul(t.clsid.c_str(), nullptr, 0)));
      mem.Escrever32(kAppStart + 8, zb2::brew::kObjDisplay);
      mem.Escrever32(kAppStart + 20, 320);
      mem.Escrever32(kAppStart + 24, 240);
      const std::uint32_t he = mem.Ler32(mem.Ler32(applet) + 8);
      cpu.Set(kR0, applet);
      cpu.Set(kR1, 0);           // EVT_APP_START
      cpu.Set(kR2, 0);
      cpu.Set(kR3, kAppStart);   // AEEAppStart*
      cpu.Set(kLR, kSentinela);
      cpu.Set(kPC, he);
      const zb2::brew::ResultadoFase rs = despacho.Correr(cpu, kLimite, kPPObj);
      passos_start = rs.passos;
      motivo_start = rs.motivo;
    }
  }

  // As faltas DA FASE DO ARRANQUE: a diferenca entre as duas contagens. E assim
  // que se separa o que o titulo pede ao ARRANCAR do que pediu a carregar.
  std::map<std::string, std::uint64_t> novas;
  for (const auto& par : traco.ContagemFaltas()) {
    const auto it = faltas_antes.find(par.first);
    const std::uint64_t antes = (it == faltas_antes.end()) ? 0 : it->second;
    if (par.second > antes) novas[par.first] = par.second - antes;
  }

  std::printf("titulo=%s  tamanho=%u  carga=%s  applet=%s  (%s)\n", t.mod.c_str(), carga.tamanho,
              carga.ok ? "sim" : "NAO", applet != 0 ? "sim" : "NAO", rc.motivo.c_str());
  std::printf("start: %" PRIu64 " passos (%s), r0=0x%08x -- o contrato do `EVT_APP_START` diz\n"
              "       que 0 ABORTA o arranque\n", passos_start, motivo_start.c_str(), cpu.Get(kR0));
  std::printf("quadros=%d pixels=%u cores=%u\n", 0, despacho.TelaRef().Escritos(),
              despacho.TelaRef().CoresDistintas());
  std::printf("faltas antes do arranque=%" PRIu64 "  faltas DURANTE o arranque=%zu nome(s)\n",
              total_antes, novas.size());
  std::uint64_t de_interface = 0;
  for (const auto& par : novas) {
    std::printf("   %-46s %" PRIu64 "x\n", par.first.c_str(), par.second);
    if (par.first.rfind("IRootForm", 0) == 0 || par.first.rfind("IForm", 0) == 0 ||
        par.first.rfind("IWidget", 0) == 0) {
      de_interface += par.second;
    }
  }
  // OS ARGUMENTOS, e nao so o nome. Uma falta sem os argumentos obriga a correr
  // a sonda outra vez com o traco ligado para saber O QUE foi pedido -- e um
  // instrumento que obriga a dois passos para dar uma resposta e meio instrumento.
  for (const auto& ev : dm.eventos) {
    if (ev.nome.rfind("NAO_IMPLEMENTADO: ", 0) == 0) {
      std::printf("   [detalhe] %s  %s\n", ev.nome.substr(18).c_str(), ev.detalhe.c_str());
    }
  }

  const zb2::brew::Widgets& w = despacho.WidgetsRef();
  std::printf("widget: propriedades_lidas=%u escritas=%u widgets_entregues=%u recusas=%zu\n",
              w.PropriedadesLidas(), w.PropriedadesEscritas(), w.WidgetsEntregues(),
              w.Recusas().size());

  if (argc > 4) {
    std::string json = "{\n  \"config\": {\"corpus_sha256\": \"\", \"titulos\": 1, \"build\": \"";
    const char* b = std::getenv("ZB2_BUILD");
    json += (b != nullptr && b[0] != 0) ? b : "desconhecido";
    json += "\"},\n  \"titulos\": [\n  {\"mod\":\"" + t.mod + "\",\"pasta\":\"" + t.pasta +
            "\",\"tamanho\":" + std::to_string(carga.tamanho) + ",\"carga\":true,\"modulo\":" +
            (modulo != 0 ? "true" : "false") + ",\"vtable\":true,\"applet\":" +
            (applet != 0 ? "true" : "false") + ",\"passos_carga\":" + std::to_string(rc.passos) +
            ",\"passos_create\":0,\"passos_start\":" + std::to_string(passos_start) +
            ",\"recusadas\":0,\"motivo\":\"" + motivo_start + "\",\"pixels\":" +
            std::to_string(despacho.TelaRef().Escritos()) + ",\"cores\":" +
            std::to_string(despacho.TelaRef().CoresDistintas()) + ",\"textos\":0,\"blits\":0,\"faltas\":{";
    bool primeiro = true;
    for (const auto& par : novas) {
      json += (primeiro ? "" : ",");
      json += "\"" + par.first + "\":" + std::to_string(par.second);
      primeiro = false;
    }
    json += "}}\n  ]\n}\n";
    std::ofstream out(argv[4]);
    out << json;
  }

  if (de_interface != 0) {
    std::printf("\nVERMELHO: %" PRIu64 " pedido(s) de interface NAO SERVIDO(S)\n", de_interface);
    return 1;
  }
  std::printf("\nVERDE: nenhum pedido de interface ficou por servir no arranque\n");
  return 0;
}
