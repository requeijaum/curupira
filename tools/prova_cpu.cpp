
// Provas isoladas do interpretador ARM. Cada bloco e UMA hipotese.
// Compilar contra libzb2_core.a. Nao faz parte do build.
#include <cstdio>
#include <cstdint>
#include "core/cpu/arm_interpreter.h"
#include "core/memoria/memoria.h"

using namespace zb2;

namespace {
struct B {
  Memoria mem;
  ArmInterpreter cpu{mem};
  std::uint32_t base = 0x00100000, off = 0;
  B() { mem.EscritorUnico("prova"); cpu.Repor(base, 0x80080000); }
  void I(std::uint32_t v) { mem.Escrever32(base + off, v); off += 4; }
  void Fim() { I(0xEAFFFFFEu); }
  std::uint32_t R(int i) { return cpu.Get(i); }
  void R(int i, std::uint32_t v) { cpu.Set(i, v); }
  bool C() { return (cpu.Cpsr() & Cpsr::kC) != 0; }
  bool T() { return (cpu.Cpsr() & Cpsr::kT) != 0; }
};
int falhas = 0;
void EQ(const char* nome, std::uint32_t obtido, std::uint32_t esperado) {
  const bool ok = obtido == esperado;
  if (!ok) ++falhas;
  std::printf("%-46s %s  obtido=0x%08x esperado=0x%08x\n", nome, ok ? "OK  " : "FALHA", obtido, esperado);
}
}  // namespace

int main() {
  // ---------------------------------------------------------------- MLA
  // MEDIDO com arm-none-eabi-as: `mla r0, r1, r2, r3` == 0xE0203291.
  // ARM ARM A4.1.26: Rd = (Rm * Rs) + Rn, com Rn nos bits 15-12 (o ADDEND),
  // Rs nos 11-8 e Rm nos 3-0.  r1*r2 + r3 = 7*6 + 100 = 142.
  {
    B b; b.R(1, 7); b.R(2, 6); b.R(3, 100);
    b.I(0xE0203291u); b.Fim(); b.cpu.Correr(1);
    EQ("MLA r0,r1,r2,r3 (7*6+100)", b.R(0), 142u);
    std::printf("    recusadas=%llu\n", (unsigned long long)b.cpu.InstruscoesRecusadas());
  }
  // ---------------------------------------------------------------- RRX
  // MEDIDO: `rrx r0, r1` == 0xE1A00061 (tipo ROR, quantidade imediata 0).
  // RRX = (valor >> 1) | (C << 31); o carry de saida e o bit 0 do valor.
  {
    B b; b.R(1, 3u);
    b.I(0xE3A0E000u | 0);        // (nada) -- placeholder para nao mexer no C
    b.off -= 4;
    // por o C a 1 com `cmp r4,#0` (0 - 0 = sem emprestimo -> C = 1)
    b.I(0xE3540000u);            // cmp r4,#0
    b.I(0xE1A00061u);            // rrx r0, r1
    b.Fim(); b.cpu.Correr(2);
    EQ("RRX r0,r1 (r1=3, C=1)", b.R(0), 0x80000001u);
  }
  {
    // `movs r0, r1, rrx` == 0xE1B00061: o carry de saida tem de ser o bit 0.
    B b; b.R(1, 3u);
    b.I(0xE3540000u);            // cmp r4,#0  -> C = 1
    b.I(0xE1B00061u);            // movs r0, r1, rrx
    b.Fim(); b.cpu.Correr(2);
    EQ("MOVS rrx: C de saida = bit 0 (=1)", b.C() ? 1u : 0u, 1u);
  }
  // ------------------------------------------------- STM com o PC na lista
  // ARM ARM: o PC LIDO como fonte vale `endereco_da_instrucao + 8`.
  // `stmfd sp!, {fp, ip, lr, pc}` == 0xE92DD800 -- o prologo APCS do GCC.
  {
    B b; b.R(13, 0x80070000u);
    b.I(0xE92DD800u); b.Fim(); b.cpu.Correr(1);
    // ordem crescente: fp(11) ip(12) lr(14) pc(15) em sp_novo+0,4,8,12
    EQ("STM {..,pc}: guarda pc+8", b.mem.Ler32(b.R(13) + 12), 0x00100008u);
  }
  {
    // `str pc, [r4]` == 0xE584F000
    B b; b.R(4, 0x00120000u);
    b.I(0xE584F000u); b.Fim(); b.cpu.Correr(1);
    EQ("STR pc,[r4]: guarda pc+8", b.mem.Ler32(0x00120000u), 0x00100008u);
  }
  // ------------------------------------------- LDM com PC: bit 0 -> Thumb
  // ARMv5T+: o LDM com o PC na lista e uma escrita do tipo BX -- o bit 0 do
  // valor carregado escolhe Thumb e o PC fica alinhado.
  {
    B b; b.R(13, 0x80070000u);
    b.mem.Escrever32(0x80070000u, 0x00200000u);
    b.mem.Escrever32(0x80070004u, 0x00300001u);   // bit 0 ligado -> Thumb
    b.I(0xE8BD8010u);            // ldmfd sp!, {r4, pc}
    b.cpu.Correr(1);
    EQ("LDM {..,pc} com bit 0: PC alinhado", b.R(15), 0x00300000u);
    EQ("LDM {..,pc} com bit 0: entra em Thumb", b.T() ? 1u : 0u, 1u);
  }
  {
    // `ldr pc, [r4]` com bit 0 ligado -- a mesma regra.
    B b; b.R(4, 0x00120000u);
    b.mem.Escrever32(0x00120000u, 0x00300001u);
    b.I(0xE594F000u);
    b.cpu.Correr(1);
    EQ("LDR pc,[r4] com bit 0: PC alinhado", b.R(15), 0x00300000u);
    EQ("LDR pc,[r4] com bit 0: entra em Thumb", b.T() ? 1u : 0u, 1u);
  }
  // ------------------------------------------------------------- MSR campos
  // `msr CPSR_cxsf, r0` == 0xE12FF000: os QUATRO campos. Os bits 8-23 tem de
  // chegar ao CPSR (ou a instrucao tem de RECUSAR, e nao engolir em silencio).
  {
    B b; b.R(0, 0x000F0000u);   // bits 16-19: o campo `s`
    const std::uint64_t antes = b.cpu.InstruscoesRecusadas();
    b.I(0xE12FF000u); b.Fim(); b.cpu.Correr(1);
    std::printf("%-46s cpsr=0x%08x recusadas_delta=%llu\n", "MSR CPSR_cxsf com bits 16-19",
                b.cpu.Cpsr(), (unsigned long long)(b.cpu.InstruscoesRecusadas() - antes));
  }
  // ------------------------------------------------------------- SBC carry
  // `sbcs r0, r1, r2` == 0xE0D10002 com r2 = 0xFFFFFFFF e C = 0:
  // r0 = r1 - 0xFFFFFFFF - 1 = r1 - 0x100000000 -> ha emprestimo, C = 0.
  {
    B b; b.R(1, 5u); b.R(2, 0xFFFFFFFFu); b.R(4, 1u);
    b.I(0xE3540002u);            // cmp r4,#2  -> 1-2 com emprestimo -> C = 0
    b.I(0xE0D10002u);            // sbcs r0, r1, r2
    b.Fim(); b.cpu.Correr(2);
    EQ("SBCS r1-0xFFFFFFFF-1: resultado", b.R(0), 5u);
    EQ("SBCS r1-0xFFFFFFFF-1: C = 0", b.C() ? 1u : 0u, 0u);
  }
  // ----------------------------------------------------- Thumb BLX registrador
  // `blx r3` (Thumb) == 0x4798: escreve o LR com `(pc + 2) | 1`.
  {
    B b;
    b.R(3, 0x00200000u);         // alvo em ARM (bit 0 = 0)
    b.cpu.SetCpsr(b.cpu.Cpsr() | Cpsr::kT);
    b.mem.Escrever16(b.base, 0x4798u);
    b.cpu.Set(15, b.base);
    b.cpu.Correr(1);
    EQ("Thumb BLX r3: LR = (pc+2)|1", b.R(14), 0x00100003u);
    EQ("Thumb BLX r3: PC = alvo", b.R(15), 0x00200000u);
  }
  // ---------------------------------------------- LDM com a base na lista
  // `ldmia r4!, {r4, r5}` == 0xE8B40030. Com o Rn na lista, o valor CARREGADO
  // manda -- o writeback nao o pode apagar.
  {
    B b; b.R(4, 0x00120000u);
    b.mem.Escrever32(0x00120000u, 0xAAAA0000u);
    b.mem.Escrever32(0x00120004u, 0xBBBB0000u);
    b.I(0xE8B40030u); b.Fim(); b.cpu.Correr(1);
    EQ("LDM r4!,{r4,r5}: r4 = valor carregado", b.R(4), 0xAAAA0000u);
  }
  std::printf("\nFALHAS: %d\n", falhas);
  return 0;
}
