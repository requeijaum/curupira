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
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "core/brew/ajudantes.h"
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

struct Titulo {
  std::string pasta;
  std::string mod;
  std::string clsid;
};

struct Estado {
  bool carga = false;
  bool modulo = false;
  bool vtable = false;
  bool create = false;
  std::uint64_t passos_carga = 0;
  std::uint64_t passos_create = 0;
  std::uint64_t recusadas = 0;
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
                    std::uint32_t quantos_slots) {
  mem.Escrever32(objeto, vtable);           // *(pishell) = vtable
  mem.Escrever32(objeto + 4, 1);            // contagem de referencias
  for (std::uint32_t i = 0; i < quantos_slots; ++i) {
    // Cada slot leva um endereco de saida proprio; quem nao estiver tratado
    // RECUSA (principio P2) em vez de saltar para zero.
    mem.Escrever32(vtable + i * 4, s.Endereco(2));
  }
  mem.Escrever32(vtable + 0, s.Endereco(3));   // AddRef
  mem.Escrever32(vtable + 4, s.Endereco(4));   // Release
}

void CorrerFase(ArmInterpreter& cpu, Alocador& al, Memoria& mem_ref, Traco& traco,
                std::uint64_t limite, std::uint64_t* passos, std::string* motivo,
                std::uint32_t pp_out) {
  *passos = 0;
  std::uint32_t saidas = 0;
  while (*passos < limite) {
    const std::uint32_t pc = cpu.Get(kPC);
    if (pc == kSentinela) { *motivo = "retornou"; return; }
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
      } else {
        // `idx` identifica o slot; o endereco de retorno diz QUEM chamou.
        traco.RegistarFalta(Area::Brew, "slot_de_saida_" + std::to_string(idx),
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
  e.tamanho = static_cast<std::uint32_t>(imagem.size());

  Tempo tempo;
  Traco traco("bateria", &tempo);
  Memoria mem(&traco);
  mem.EscritorUnico("cpu");
  ArmInterpreter cpu(mem, &traco);
  Alocador al(mem, kHeap, kHeapTam, &traco);

  Saidas s;
  s.base = 0xF0000000u;
  s.quantos = 100000;   // espaco para a tabela de ajudantes E para as vtables
  s.passo = 4;
  s.ativa = true;
  cpu.ConfigurarSaidas(s);
  mem.Escrever32(kTabela + 0x68, s.Endereco(0));  // malloc
  mem.Escrever32(kTabela + 0x6c, s.Endereco(1));  // free
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
  for (std::uint32_t off = 0; off < 117 * 4; off += 4) {
    if (off == 0x68 || off == 0x6c) continue;
    mem.Escrever32(kTabela + off, s.Endereco(2));
  }

  // As vtables das interfaces ficam ACIMA da tabela de ajudantes, dentro da
  // mesma faixa de saida. Enderecos distintos por interface.
  const std::uint32_t kShell = 0x80020000u;
  const std::uint32_t kShellVtable = s.Endereco(1000);
  ConstruirShell(mem, s, kShell, kShellVtable, 64);

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
  cpu.Set(kR0, modulo);
  cpu.Set(kR1, static_cast<std::uint32_t>(std::strtoul(t.clsid.c_str(), nullptr, 0)));
  cpu.Set(kR2, kPPObj);
  cpu.Set(kLR, kSentinela);
  std::string motivo_create;
  CorrerFase(cpu, al, mem, traco, kLimite, &e.passos_create, &motivo_create, kPPObj);
  e.recusadas = cpu.InstruscoesRecusadas();
  e.create = mem.Ler32(kPPObj) != 0;
  e.motivo += " | create:" + motivo_create;
  if (!e.create) e.motivo += "_sem_applet";
  for (const auto& par : traco.ContagemFaltas()) e.faltas[par.first] = par.second;
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

  std::printf("%-16s %-8s %-6s %-6s %-6s %8s %8s  %s\n", "titulo", "tamanho", "carga", "modulo",
              "vtable", "carga_p", "cria_p", "motivo");
  int carregam = 0, com_modulo = 0, com_applet = 0;
  std::map<std::string, std::uint64_t> faltas_totais;
  std::string json = "[\n";
  for (const Titulo& t : titulos) {
    const Estado e = Medir(t, argv[2]);
    if (e.carga) ++carregam;
    if (e.modulo) ++com_modulo;
    if (e.create) ++com_applet;
    for (const auto& par : e.faltas) faltas_totais[par.first] += par.second;
    std::printf("%-16s %-8u %-6s %-6s %-6s %8" PRIu64 " %8" PRIu64 "  %s\n", t.mod.c_str(),
                e.tamanho, e.carga ? "sim" : "NAO", e.modulo ? "sim" : "NAO",
                e.vtable ? "sim" : "NAO", e.passos_carga, e.passos_create, e.motivo.c_str());
    json += "  {\"mod\":\"" + t.mod + "\",\"pasta\":\"" + t.pasta + "\",\"tamanho\":" +
            std::to_string(e.tamanho) + ",\"carga\":" + (e.carga ? "true" : "false") +
            ",\"modulo\":" + (e.modulo ? "true" : "false") +
            ",\"vtable\":" + (e.vtable ? "true" : "false") +
            ",\"applet\":" + (e.create ? "true" : "false") +
            ",\"passos_carga\":" + std::to_string(e.passos_carga) +
            ",\"passos_create\":" + std::to_string(e.passos_create) +
            ",\"recusadas\":" + std::to_string(e.recusadas) + "},\n";
  }
  json += "]\n";

  std::printf("\n== %d titulos | carga %d | ponteiro de modulo %d | applet %d ==\n", (int)titulos.size(),
              carregam, com_modulo, com_applet);
  std::printf("== slots do sistema que ficaram por implementar ==\n");
  for (const auto& par : faltas_totais) {
    std::printf("   %-40s %" PRIu64 "\n", par.first.c_str(), par.second);
  }
  if (argc > 3) {
    std::ofstream out(argv[3]);
    out << json;
    std::printf("\nestado escrito em %s\n", argv[3]);
  }
  return 0;
}
