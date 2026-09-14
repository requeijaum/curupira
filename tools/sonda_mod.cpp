// Sonda de arranque de um `.mod`.
//
// Existe para responder a UMA pergunta, com numero: **carregar o modulo basta
// para ele comecar a correr, e onde e que ele para?** O interpretador sabe
// quantas instrucoes correu, o PC de cada paragem e quantas recusou.
//
// A tabela de ajudantes fica a ZERO de proposito: com ela a zero, o modulo le
// zero de `base-4` e o que acontece a seguir diz exactamente onde ele precisa do
// sistema. E a medicao mais barata que ha, e e o "comando que fica vermelho" da
// Etapa 2 do plano.
//
// Uso: zb2_sonda_mod <ficheiro.mod> [limite_de_passos]

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "core/brew/ajudantes.h"
#include "core/carga/mod.h"
#include "core/cpu/arm_interpreter.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"

using namespace zb2;

namespace {

std::vector<std::uint8_t> LerFicheiro(const char* caminho, bool* ok) {
  std::vector<std::uint8_t> dados;
  std::FILE* f = std::fopen(caminho, "rb");
  if (f == nullptr) { *ok = false; return dados; }
  std::uint8_t buf[65536];
  std::size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) dados.insert(dados.end(), buf, buf + n);
  std::fclose(f);
  *ok = true;
  return dados;
}

bool DentroDoMod(std::uint32_t pc, std::uint32_t base, std::uint32_t tamanho) {
  return pc >= base && pc < base + tamanho;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "uso: %s <ficheiro.mod> [limite]\n", argv[0]);
    return 2;
  }
  const std::uint64_t limite = argc > 2 ? std::strtoull(argv[2], nullptr, 0) : 200000;

  bool ok = false;
  const std::vector<std::uint8_t> imagem = LerFicheiro(argv[1], &ok);
  if (!ok || imagem.empty()) {
    std::fprintf(stderr, "nao consegui ler '%s'\n", argv[1]);
    return 2;
  }

  constexpr std::uint32_t kBase = 0x00100000;
  constexpr std::uint32_t kPilha = 0x80080000;
  constexpr std::uint32_t kSentinela = 0xFFFFFFF0u;

  Tempo tempo;
  Traco traco("sonda", &tempo);
  DestinoMemoria dm;
  traco.JuntarDestino(&dm);
  Memoria mem(&traco);
  mem.EscritorUnico("cpu");
  ArmInterpreter cpu(mem, &traco);

  // A faixa de saida e a tabela de ajudantes.
  Saidas saidas;
  saidas.base = 0xF0000000u;
  saidas.passo = 4;
  saidas.quantos = 200;
  saidas.ativa = true;
  cpu.ConfigurarSaidas(saidas);

  Alocador alocador(mem, 0x80200000u, 0x00400000u, &traco);
  TabelaDeAjudantes tabela(mem, alocador, &traco);
  const std::uint32_t kSlotMalloc = 0x68;
  const std::uint32_t kSlotFree = 0x6c;
  // Os dois slots que o modulo pede primeiro, medidos no desmonte de
  // 0x00100724: `ldr r1, [r0, #104]` = offset 0x68 = malloc.
  tabela.Declarar(kSlotMalloc, "malloc");
  tabela.Declarar(kSlotFree, "free");
  const std::uint32_t kTabela = 0x80010000u;
  {
    // Instala com permissao para slots por implementar: a sonda existe para
    // MEDIR, e recusar aqui esconderia o resultado.
    auto inst = tabela.Instalar(kTabela, /*permitir_por_implementar=*/true);
    (void)inst;
  }
  // Escreve, para os slots que sabemos, os enderecos de saida correspondentes.
  mem.Escrever32(kTabela + kSlotMalloc, cpu.GetSaidas().Endereco(0));
  mem.Escrever32(kTabela + kSlotFree, cpu.GetSaidas().Endereco(1));

  const ResultadoDaCarga carga = CarregarMod(mem, imagem, kBase, kTabela, &traco);
  if (!carga.ok) {
    std::printf("CARGA FALHOU: %s\n", carga.motivo.c_str());
    return 1;
  }
  cpu.Repor(kBase, kPilha);
  // A entrada do modulo e chamada COMO FUNCAO pelo sistema: o LR aponta para um
  // sentinela e o `bx lr` do fim significa "a chamada terminou", nao "salta para
  // o que estiver no LR". Sem isto, o LR a zero faz a primeira medicao parar em
  // 0x00000000 e a leitura obvia -- "leu um ponteiro nulo da tabela" -- estava
  // ERRADA: era o retorno normal da funcao.
  cpu.Set(kLR, kSentinela);
  // O `AEEMod_Load` recebe argumentos, e passar tudo a zero faz o modulo desistir
  // em 23 instrucoes -- medido, e foi a razao de a primeira sonda parecer que o
  // modulo "retornava". A arvore antiga chama-o com r0 = shell e r2 = ponteiro
  // para onde escrever o ponteiro do modulo.
  constexpr std::uint32_t kPpMod = 0x00090000;   // endereco de saida, fora do modulo
  constexpr std::uint32_t kShellFalso = 0x81000000;  // nao ha IShell ainda
  cpu.Set(kR0, kShellFalso);
  cpu.Set(kR2, kPpMod);

  std::printf("mod: %s\n", argv[1]);
  std::printf("  base=0x%08x tamanho=%u bytes  PC=0x%08x SP=0x%08x\n", carga.base, carga.tamanho,
              cpu.Get(kPC), cpu.Get(kSP));
  std::printf("  CPSR=0x%08x (modo valido: %s)\n", cpu.Cpsr(),
              ModoValido(cpu.Cpsr()) ? "sim" : "NAO");

  std::uint64_t passos = 0;
  std::uint32_t pc_anterior = cpu.Get(kPC);
  int eventos = 0;
  while (passos < limite) {
    const std::uint32_t pc = cpu.Get(kPC);
    if (pc == kSentinela) {
      std::printf("  A CHAMADA RETORNOU apos %" PRIu64 " instrucoes\n", passos);
      break;
    }
    if (!DentroDoMod(pc, kBase, carga.tamanho)) {
      // Saiu do modulo. Um salto para fora e a assinatura de "leu um ponteiro
      // que nao existia" -- tipicamente o da tabela de ajudantes.
      if (eventos < 8) {
        std::printf("  [saida %d] pc=0x%08x (fora do modulo) apos %" PRIu64
                    " instrucoes; pc anterior=0x%08x\n",
                    eventos + 1, pc, passos, pc_anterior);
      }
      ++eventos;
      if (pc < 0x1000) {
        if (eventos <= 9) {
          std::printf("               leu um ponteiro NULO e saltou para 0x%08x\n", pc);
        }
        break;
      }
    }
    std::uint32_t indice_saida = 0;
    if (cpu.GetSaidas().Contem(pc, &indice_saida)) {
      // O guest chamou o sistema. Despacha-se pelo indice, com os argumentos
      // nos registradores, e devolve-se o controle ao modulo.
      const std::uint32_t lr = cpu.Get(kLR);
      if (indice_saida == 0) {
        const std::uint32_t pedido = cpu.Get(kR0);
        const std::uint32_t bloco = alocador.Malloc(pedido);
        std::printf("  [saida %d] MALLOC(%u) -> 0x%08x   (apos %" PRIu64 " instrucoes)\n",
                    indice_saida, pedido, bloco, passos);
        cpu.Set(kR0, bloco);
      } else if (indice_saida == 1) {
        std::printf("  [saida %d] FREE(0x%08x)   (apos %" PRIu64 " instrucoes)\n",
                    indice_saida, cpu.Get(kR0), passos);
        alocador.Free(cpu.Get(kR0));
        cpu.Set(kR0, 0);
      } else {
        std::printf("  [saida %d] slot NAO TRATADO   (apos %" PRIu64 " instrucoes)\n",
                    indice_saida, passos);
        cpu.Set(kR0, kAeeUnsupported);
      }
      cpu.Set(kPC, lr);
      if (++eventos > 20) { std::printf("  (demasiadas saidas; paro)\n"); break; }
    }
    pc_anterior = pc;
    cpu.Passo();
    ++passos;
  }
  std::printf("  heap: alocado=%u bytes, falhas=%u\n", alocador.Alocado(), alocador.Falhas());

  std::printf("  resumo: %" PRIu64 " instrucoes, %" PRIu64 " recusadas, PC final=0x%08x\n",
              passos, cpu.InstruscoesRecusadas(), cpu.Get(kPC));
  std::printf("  ponteiro do modulo (em 0x%08x) = 0x%08x\n", kPpMod, mem.Ler32(kPpMod));
  if (cpu.InstruscoesRecusadas() > 0) {
    std::printf("  ultima recusada: opcode=0x%08x em pc=0x%08x\n", cpu.UltimaRecusada(),
                cpu.PcDaUltimaRecusada());
  }
  return 0;
}
