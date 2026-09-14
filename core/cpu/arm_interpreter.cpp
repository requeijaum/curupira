#include "core/cpu/arm_interpreter.h"

#include <cstdio>

namespace zb2 {

bool ModoValido(std::uint32_t cpsr) {
  switch (cpsr & Cpsr::kModo) {
    case static_cast<std::uint32_t>(Modo::Usuario):
    case static_cast<std::uint32_t>(Modo::FIQ):
    case static_cast<std::uint32_t>(Modo::IRQ):
    case static_cast<std::uint32_t>(Modo::Supervisor):
    case static_cast<std::uint32_t>(Modo::Abort):
    case static_cast<std::uint32_t>(Modo::Indefinido):
    case static_cast<std::uint32_t>(Modo::Sistema):
      return true;
    default:
      return false;
  }
}

namespace {

// Bandeiras de carry e overflow, num sitio so.
//
// MOTIVO de estarem isoladas: e onde os erros de bandeira nascem. Recalcular a
// mao em cada instrucao multiplica as ocasioes de errar, e um erro de bandeira
// nao aparece na tela -- aparece num desvio que o jogo toma a mais. No ARM,
// C=1 na subtracao significa "sem borrow", que e a inversao classica.
struct SomaFlags {
  bool c;
  bool v;
};

SomaFlags FlagsDaSoma(std::uint32_t a, std::uint32_t b, std::uint32_t carry_in, std::uint32_t r) {
  const std::uint64_t soma = static_cast<std::uint64_t>(a) + b + carry_in;
  SomaFlags f;
  f.c = soma > 0xFFFFFFFFull;
  const bool sa = (a >> 31) != 0;
  const bool sb = (b >> 31) != 0;
  const bool sr = (r >> 31) != 0;
  f.v = (sa == sb) && (sr != sa);
  return f;
}

SomaFlags FlagsDaSubtracao(std::uint32_t a, std::uint32_t b, std::uint32_t borrow_in,
                           std::uint32_t r) {
  const std::uint64_t sub = static_cast<std::uint64_t>(a) - b - borrow_in;
  SomaFlags f;
  f.c = sub <= 0xFFFFFFFFull;  // no ARM, C=1 significa "sem borrow"
  const bool sa = (a >> 31) != 0;
  const bool sb = (b >> 31) != 0;
  const bool sr = (r >> 31) != 0;
  f.v = (sa != sb) && (sr != sa);
  return f;
}

SomaFlags FlagsDaSub(std::uint32_t a, std::uint32_t b, std::uint32_t r) {
  return FlagsDaSubtracao(a, b, 0, r);
}

}  // namespace

namespace {
bool instrucao_bandeiras(std::uint32_t instr) { return (instr & (1u << 20)) != 0; }
}  // namespace

ArmInterpreter::ArmInterpreter(Memoria& mem, Traco* traco) : mem_(mem), traco_(traco) {
  // ARMADILHA MEDIDA, e a razao de este construtor nao ser trivial.
  //
  // O Zeebulator antigo fazia `cpsr_ = 0`. **Zero nao e modo nenhum**: os cinco
  // bits baixos de um CPSR valido sao 0x10 (Usuario), 0x11 (FIQ), 0x12 (IRQ),
  // 0x13 (Supervisor), 0x17 (Abort), 0x1B (Indefinido) ou 0x1F (Sistema).
  //
  // Encontrar isto custou uma investigacao: o `chessbots.mod`, em 0x0019a870,
  // faz `mrs r0, cpsr / tst r0, #0xf / bxeq lr`. Ler um CPSR com o modo a zero
  // mudava o caminho do jogo. E a divergencia so apareceu porque o teste de
  // lockstep interpretador-contra-JIT existia -- o sintoma na tela era o mesmo.
  modo_atual_ = static_cast<std::uint32_t>(Modo::Usuario) | Cpsr::kI | Cpsr::kF;
  spsr_[0] = modo_atual_;
}

Reg ArmInterpreter::Get(int r) const {
  if (r == kPC) return banco_[kPC];
  if (modo_atual_ == static_cast<std::uint32_t>(Modo::FIQ) && r >= 8 && r <= 12) {
    return sombra_fiq_r8_r12_[r - 8];
  }
  if (r == kSP || r == kLR) {
    switch (modo_atual_ & Cpsr::kModo) {
      case static_cast<std::uint32_t>(Modo::IRQ): return sombra_irq_r13_r14_[r - 13];
      case static_cast<std::uint32_t>(Modo::Supervisor): return sombra_svc_r13_r14_[r - 13];
      case static_cast<std::uint32_t>(Modo::Abort): return sombra_abt_r13_r14_[r - 13];
      case static_cast<std::uint32_t>(Modo::Indefinido): return sombra_und_r13_r14_[r - 13];
      default: break;
    }
  }
  return banco_[r];
}

void ArmInterpreter::Set(int r, Reg v) {
  if (r == kPC) { banco_[kPC] = v; return; }
  if (modo_atual_ == static_cast<std::uint32_t>(Modo::FIQ) && r >= 8 && r <= 12) {
    sombra_fiq_r8_r12_[r - 8] = v; return;
  }
  if (r == kSP || r == kLR) {
    switch (modo_atual_ & Cpsr::kModo) {
      case static_cast<std::uint32_t>(Modo::IRQ): sombra_irq_r13_r14_[r - 13] = v; return;
      case static_cast<std::uint32_t>(Modo::Supervisor): sombra_svc_r13_r14_[r - 13] = v; return;
      case static_cast<std::uint32_t>(Modo::Abort): sombra_abt_r13_r14_[r - 13] = v; return;
      case static_cast<std::uint32_t>(Modo::Indefinido): sombra_und_r13_r14_[r - 13] = v; return;
      default: break;
    }
  }
  banco_[r] = v;
}

std::uint32_t ArmInterpreter::Cpsr() const {
  std::uint32_t v = modo_atual_;
  if (n_) v |= Cpsr::kN;
  if (z_) v |= Cpsr::kZ;
  if (c_) v |= Cpsr::kC;
  if (v_) v |= Cpsr::kV;
  return v;
}

void ArmInterpreter::SetCpsr(std::uint32_t v) {
  n_ = (v & Cpsr::kN) != 0;
  z_ = (v & Cpsr::kZ) != 0;
  c_ = (v & Cpsr::kC) != 0;
  v_ = (v & Cpsr::kV) != 0;
  modo_atual_ = v;
  if (!ModoValido(v)) {
    // Nao se corrige em silencio: escrever um modo invalido e um defeito de
    // quem escreve, e engoli-lo foi exactamente o erro do projeto antigo.
    Recusar(0, banco_[kPC], "CPSR com modo invalido escrito");
  }
}

Modo ArmInterpreter::ModoAtual() const {
  return static_cast<Modo>(modo_atual_ & Cpsr::kModo);
}

void ArmInterpreter::Recusar(std::uint32_t instr, std::uint32_t pc, const char* porque) {
  ++recusadas_;
  ultima_recusada_ = instr;
  pc_da_recusada_ = pc;
  if (traco_ != nullptr) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "instr=0x%08x pc=0x%08x -- %s", instr, pc, porque);
    traco_->Emitir(Area::Cpu, Nivel::Erro, "INSTRUCAO_RECUSADA", buf);
  }
}

void ArmInterpreter::Repor(Reg pc, Reg sp) {
  for (int i = 0; i < 16; ++i) banco_[i] = 0;
  banco_[kPC] = pc;
  banco_[kSP] = sp;
  n_ = z_ = c_ = v_ = false;
  modo_atual_ = static_cast<std::uint32_t>(Modo::Usuario) | Cpsr::kI | Cpsr::kF;
  recusadas_ = 0;
  ultima_recusada_ = 0;
  pc_da_recusada_ = 0;
}

std::uint32_t ArmInterpreter::Buscar32(std::uint32_t end) { return mem_.Ler32(end); }
std::uint32_t ArmInterpreter::Buscar16(std::uint32_t end) { return mem_.Ler16(end); }

bool ArmInterpreter::CondicaoVerdadeira(std::uint32_t cond) const {
  switch (cond) {
    case 0x0: return z_;
    case 0x1: return !z_;
    case 0x2: return c_;
    case 0x3: return !c_;
    case 0x4: return n_;
    case 0x5: return !n_;
    case 0x6: return v_;
    case 0x7: return !v_;
    case 0x8: return c_ && !z_;
    case 0x9: return !c_ || z_;
    case 0xA: return n_ == v_;
    case 0xB: return n_ != v_;
    case 0xC: return !z_ && (n_ == v_);
    case 0xD: return z_ || (n_ != v_);
    case 0xE: return true;
    case 0xF: return true;
  }
  return false;
}

Reg ArmInterpreter::Deslocar(Reg valor, std::uint32_t tipo, uint32_t quantidade, bool carry_in,
                             bool* carry_out) const {
  *carry_out = carry_in;
  if (quantidade == 0) return valor;
  switch (tipo) {
    case 0: {
      if (quantidade < 32) {
        *carry_out = ((valor >> (32 - quantidade)) & 1) != 0;
        return valor << quantidade;
      }
      if (quantidade == 32) {
        *carry_out = (valor & 1) != 0;
        return 0;
      }
      *carry_out = false;
      return 0;
    }
    case 1: {
      if (quantidade < 32) {
        *carry_out = ((valor >> (quantidade - 1)) & 1) != 0;
        return valor >> quantidade;
      }
      if (quantidade == 32) {
        *carry_out = (valor >> 31) != 0;
        return 0;
      }
      *carry_out = false;
      return 0;
    }
    case 2: {
      const auto s = static_cast<std::int32_t>(valor);
      if (quantidade < 32) {
        *carry_out = ((valor >> (quantidade - 1)) & 1) != 0;
        return static_cast<Reg>(s >> quantidade);
      }
      *carry_out = s < 0;
      return s < 0 ? 0xFFFFFFFFu : 0u;
    }
    case 3: {
      const uint32_t q = quantidade & 31;
      if (q == 0) {
        *carry_out = (valor >> 31) != 0;
        return valor;
      }
      *carry_out = ((valor >> (q - 1)) & 1) != 0;
      return (valor >> q) | (valor << (32 - q));
    }
  }
  return valor;
}

Reg ArmInterpreter::OperandoDeslocado(std::uint32_t instr, std::uint32_t pc, bool usaImediato,
                                      bool* carry_out) const {
  if (usaImediato) {
    const uint32_t imediato = instr & 0xFF;
    const uint32_t rotacao = ((instr >> 8) & 0xF) * 2;
    if (rotacao == 0) {
      *carry_out = c_;
      return imediato;
    }
    return Deslocar(imediato, 3, rotacao, c_, carry_out);
  }
  const uint32_t rm = instr & 0xF;
  Reg valor = Get(static_cast<int>(rm));
  if (rm == kPC) valor += 8;
  (void)pc;
  const uint32_t tipo = (instr >> 5) & 3;
  if ((instr & 0x10u) != 0) {
    const uint32_t rs = (instr >> 8) & 0xF;
    const uint32_t quantidade = Get(static_cast<int>(rs)) & 0xFF;
    return Deslocar(valor, tipo, quantidade, c_, carry_out);
  }
  uint32_t quantidade = (instr >> 7) & 0x1F;
  if (quantidade == 0 && tipo != 0) quantidade = 32;
  return Deslocar(valor, tipo, quantidade, c_, carry_out);
}

// ---------------------------------------------------------------------------
// O GRUPO "EXTRA LOAD/STORE" (ARM ARM A5.3.4): LDRH, STRH, LDRSB, LDRSH,
// LDRD e STRD.
// ---------------------------------------------------------------------------
//
// PORQUE ESTE GRUPO TEM DE SER UMA FAMILIA PROPRIA, e a medicao que o obrigou:
//
// `ldrd r8, sb, [sp, #0x20]` -- palavra 0xE1CD82D0, no `a3d.mod` em 0x55C0 --
// tem os bits 27-25 = 000, que e o MESMO campo que o primeiro nivel de
// descodificacao usa para "dados processados". Sem o ramo abaixo ele cai no
// `DadosProcessados`, que o le como `BIC r8, sp, r0, LSR r2` e escreve em r8 o
// valor `sp & ~0` = o proprio SP.
//
// O efeito medido, com o espiao de escrita no descarregador de `ESPIAO=0x8020001c`:
//
//   [DEBUG-pilha1] pc=0000563c alvo=8020001c 0x00000000 -> 0x8007ffcc
//                  r0=00000001 r1=0000551c sp=8007ffcc lr=000055f0
//
// 0x563C e o `stmib r4, {r0, r6, r8, sb}` do `AEEStaticMod_New` compilado, que
// escreve `pMe->pfnModCrInst` em `+12`. O valor vem de r8, r8 vem do `ldrd`, e
// o `ldrd` devia ler os argumentos 5 e 6 do `AEEStaticMod_New` -- que o
// `AEEMod_Load` empurra como ZERO (`platform/system/src/AEEModGen.c`:
// `AEEStaticMod_New(sizeof(AEEMod), pIShell, ph, ppMod, NULL, NULL)`).
//
// Com `+12` a zero, o `AEEMod_CreateInstance` do modulo toma o caminho `beq`
// (o `else` do `if (pme->pfnModCrInst)`) e chama o `AEEClsCreateInstance`. Com
// `+12` a apontar para a PILHA, o modulo faz `bxne ip` para um endereco de
// dados -- e **21 dos 62 titulos saiam do modulo com o PC na pilha**.
//
// AS FORMAS ABAIXO FORAM DESCODIFICADAS, e nao escritas de memoria. O oraculo e
// o `arm-none-eabi-objdump` (binutils), e o mapa campo-a-campo esta fixado pelo
// teste `ExtraPalavraMedidaDoA3d` em `tests/cpu_test.cpp`. As familias:
//
//   bits 7-4 = 1011 (0xB) -> meia-palavra      L=1 LDRH   / L=0 STRH
//   bits 7-4 = 1101 (0xD) -> L=0 LDRD          L=1 LDRSB
//   bits 7-4 = 1111 (0xF) -> L=0 STRD          L=1 LDRSH
//
// Repare-se que no caso da palavra dupla o bit 20 (o `L` das outras) e ZERO nas
// DUAS direccoes: quem separa LDRD de STRD e o campo 7-4 (D contra F). Foi essa
// a leitura que enganou a primeira versao desta nota.
//
// O que NAO esta implementado RECUSA com o nome (P2): as formas nao
// privilegiadas (`*T`), o `Rt` impar no LDRD/STRD, o `Rt`/`Rn` = PC, o
// desalinhamento no LDRD/STRD e o offset de registrador com os bits 11-8
// diferentes de zero.
namespace {
bool EhTransferenciaExtra(std::uint32_t instr) {
  // bits 27-25 = 000 (o grupo), bit 7 = 1 e bit 4 = 1 (o campo `1 S H 1`) e
  // bits 6-5 diferentes de 00. O campo 6-5 = 00 e o que exclui o
  // `MUL`/`UMULL`/`SWP` (bits 7-4 = 1001), que vivem no mesmo espaco; o bit 7 = 0
  // e o que exclui os dados processados com registrador de deslocamento
  // (`<op> Rd, Rn, Rm, <shift> Rs`, bits 7-4 = 0_shift_1).
  return (instr & 0x0E000000u) == 0u && (instr & 0x90u) == 0x90u && (instr & 0x60u) != 0u;
}
}  // namespace

void ArmInterpreter::TransferenciaExtra(std::uint32_t instr, std::uint32_t pc) {
  const std::uint32_t campo = (instr >> 4) & 0xFu;  // 1 S H 1
  const bool p = ((instr >> 24) & 1u) != 0;
  const bool u = ((instr >> 23) & 1u) != 0;
  const bool com_imediato = ((instr >> 22) & 1u) != 0;
  const bool w = ((instr >> 21) & 1u) != 0;
  const bool carrega = ((instr >> 20) & 1u) != 0;
  const std::uint32_t rn = (instr >> 16) & 0xFu;
  const std::uint32_t rt = (instr >> 12) & 0xFu;
  const std::uint32_t rm = instr & 0xFu;
  const std::uint32_t imm4h = (instr >> 8) & 0xFu;
  const std::uint32_t imm4l = instr & 0xFu;

  // A ARMADILHA DESTE GRUPO, e a razao de a verificacao ter sido feita no
  // binutils: no caso da PALAVRA DUPLA o bit 20 (`L`) e ZERO nas DUAS direccoes
  // (`ldrd r8, [sp, #32]` = 0xE1CD82D0 e `strd r4, [r0, #8]` = 0xE1C040F8).
  // Quem separa LDRD de STRD e o campo 7-4: 1101 (D) carrega, 1111 (F) guarda.
  // Ler o bit 20 aqui troca a leitura pela escrita -- e a primeira versao deste
  // codigo faze-lo-ia em silencio se o teste `ExtraLdrd*` nao existisse.
  const bool palavra_dupla = !carrega && (campo == 0xDu || campo == 0xFu);
  const bool dupla_carrega = (campo == 0xDu);

  if (rn == 15 || rt == 15) {
    Recusar(instr, pc, "extra load/store com Rn ou Rt = PC e UNPREDICTABLE no ARM");
    return;
  }
  if (!p && w) {
    Recusar(instr, pc, "forma nao privilegiada do extra load/store (LDRHT/STRHT/LDRSBT/LDRSHT) nao implementada");
    return;
  }
  if (palavra_dupla && (rt & 1u) != 0) {
    Recusar(instr, pc, "LDRD/STRD com Rt impar e UNPREDICTABLE no ARM");
    return;
  }
  if (w && rn == rt) {
    Recusar(instr, pc, "extra load/store com escrita na base e Rd = Rn e UNPREDICTABLE no ARM");
    return;
  }

  Reg deslocamento = 0;
  if (com_imediato) {
    deslocamento = (imm4h << 4) | imm4l;
  } else {
    if (imm4h != 0) {
      Recusar(instr, pc, "offset de registrador com os bits 11-8 diferentes de zero nao implementado");
      return;
    }
    if (palavra_dupla) {
      if (!u) {  // P=0, U=0 no LDRD/STRD e UNPREDICTABLE (conferido no binutils)
        Recusar(instr, pc, "LDRD/STRD com offset de registrador, P=0 e U=0 nao e forma valida");
        return;
      }
    } else if (!(p && !w && u)) {
      // A forma de registrador da meia-palavra e do sinalizado e so uma:
      // `[Rn, Rm]` -- P=1, W=0, U=1. As outras nao existem (o binutils
      // descodifica-as como UNDEFINED).
      Recusar(instr, pc, "offset de registrador nesta forma de extra load/store nao e valido no ARM");
      return;
    }
    deslocamento = Get(static_cast<int>(rm));
  }

  // OS VALORES DE ORIGEM SAO LIDOS ANTES DE A BASE ANDAR. No pos-indexado o
  // `Rn` muda durante a instrucao e o `Rt` pode ser o mesmo registrador: ler
  // primeiro tira a duvida de ordem. O caso com escrita na base e `Rn == Rt` ja
  // foi RECUSADO acima, porque no ARM e UNPREDICTABLE.
  const Reg valor_lo = Get(static_cast<int>(rt));
  const Reg valor_hi = palavra_dupla ? Get(static_cast<int>(rt + 1)) : 0;

  const Reg base = Get(static_cast<int>(rn));
  Reg endereco = base;
  if (p) {
    endereco = u ? base + deslocamento : base - deslocamento;
  } else if (u) {
    Set(static_cast<int>(rn), base + deslocamento);  // pos-indexado
  } else {
    Set(static_cast<int>(rn), base - deslocamento);  // pos-indexado a descer
  }
  if (p && w) Set(static_cast<int>(rn), endereco);

  if (palavra_dupla) {
    if ((endereco & 3u) != 0u) {
      Recusar(instr, pc, "LDRD/STRD em endereco desalinhado (o ARM exige alinhamento de 4)");
      return;
    }
    if (dupla_carrega) {
      Set(static_cast<int>(rt), mem_.Ler32(endereco));
      Set(static_cast<int>(rt + 1), mem_.Ler32(endereco + 4u));
    } else {
      mem_.Escrever32(endereco, valor_lo);
      mem_.Escrever32(endereco + 4u, valor_hi);
    }
    return;
  }

  if (campo == 0xBu) {  // meia-palavra
    if (carrega) {
      Set(static_cast<int>(rt), mem_.Ler16(endereco));  // zero-extendido
    } else {
      mem_.Escrever16(endereco, static_cast<std::uint16_t>(valor_lo & 0xFFFFu));
    }
    return;
  }

  if (campo == 0xDu) {  // LDRSB
    const auto b = static_cast<std::int8_t>(mem_.Ler8(endereco));
    Set(static_cast<int>(rt), static_cast<Reg>(static_cast<std::int32_t>(b)));
    return;
  }

  // campo == 0xF: LDRSH
  const auto h = static_cast<std::int16_t>(mem_.Ler16(endereco));
  Set(static_cast<int>(rt), static_cast<Reg>(static_cast<std::int32_t>(h)));
}

void ArmInterpreter::DadosProcessados(std::uint32_t instr, std::uint32_t pc) {
  const std::uint32_t opcode = (instr >> 21) & 0xF;
  const uint32_t rn = (instr >> 16) & 0xF;
  const uint32_t rd = (instr >> 12) & 0xF;
  const bool s = (instr & (1u << 20)) != 0;
  bool carry = c_;
  const Reg op2 = OperandoDeslocado(instr, pc, (instr & 0x02000000u) != 0, &carry);
  Reg a = Get(static_cast<int>(rn));
  if (rn == kPC) a += 8;

  Reg resultado = 0;
  bool escreve = true;
  bool carry_ja_posto = false;
  switch (opcode) {
    case 0x0: resultado = a & op2; break;
    case 0x1: resultado = a ^ op2; break;
    case 0x2: { resultado = a - op2; auto f = FlagsDaSubtracao(a, op2, 0, resultado); c_ = f.c; v_ = f.v; carry_ja_posto = true; break; }
    case 0x3: { resultado = op2 - a; auto f = FlagsDaSubtracao(op2, a, 0, resultado); c_ = f.c; v_ = f.v; carry_ja_posto = true; break; }
    case 0x4: { resultado = a + op2; auto f = FlagsDaSoma(a, op2, 0, resultado); c_ = f.c; v_ = f.v; carry_ja_posto = true; break; }
    case 0x5: { resultado = a + op2 + (c_ ? 1 : 0); auto f = FlagsDaSoma(a, op2, c_ ? 1 : 0, resultado); c_ = f.c; v_ = f.v; carry_ja_posto = true; break; }
    case 0x6: { const Reg b = op2 + (c_ ? 0 : 1); resultado = a - b; auto f = FlagsDaSubtracao(a, b, 0, resultado); c_ = f.c; v_ = f.v; carry_ja_posto = true; break; }
    case 0x7: { const Reg b = op2 + (c_ ? 0 : 1); resultado = b - a; auto f = FlagsDaSubtracao(b, a, 0, resultado); c_ = f.c; v_ = f.v; carry_ja_posto = true; break; }
    case 0x8: resultado = a & op2; escreve = false; n_ = (resultado >> 31) != 0; z_ = resultado == 0; c_ = carry; break;
    case 0x9: resultado = a ^ op2; escreve = false; n_ = (resultado >> 31) != 0; z_ = resultado == 0; c_ = carry; break;
    case 0xA: { resultado = a - op2; auto f = FlagsDaSubtracao(a, op2, 0, resultado); c_ = f.c; v_ = f.v; n_ = (resultado >> 31) != 0; z_ = resultado == 0; } escreve = false; carry_ja_posto = true; break;
    case 0xB: { resultado = a + op2; auto f = FlagsDaSoma(a, op2, 0, resultado); c_ = f.c; v_ = f.v; n_ = (resultado >> 31) != 0; z_ = resultado == 0; } escreve = false; carry_ja_posto = true; break;
    case 0xC: resultado = a | op2; break;
    case 0xD: resultado = op2; break;
    case 0xE: resultado = a & ~op2; break;
    case 0xF: resultado = ~op2; break;
  }

  // As bandeiras tem DUAS origens, e misturar as duas foi um erro real deste
  // codigo na primeira versao: um `carry_ja_posto` que devia dizer apenas
  // "o C e o V ja vieram da soma" acabou tambem a suprimir o N e o Z. O
  // sintoma era `0x7fffffff + 1 == 0x80000000` ficar com N=0.
  //
  //   A) aritmetica (ADD/SUB/RSB/ADC/SBC/RSC): C e V vem do calculo; N e Z vem
  //      do resultado.
  //   B) logica (AND/EOR/ORR/MOV/BIC/MVN) e deslocamentos: N e Z vem do
  //      resultado, C vem do ultimo bit deslocado para fora.
  //   C) TST/TEQ/CMP/CMN: ja poem as quatro no proprio ramo.
  if (s && escreve) {
    n_ = (resultado >> 31) != 0;
    z_ = resultado == 0;
    if (!carry_ja_posto) c_ = carry;
  }

  if (!escreve) return;
  if (rd == kPC) {
    if (s) {
      Recusar(instr, pc, "escrita no PC com S (restauro de SPSR) nao implementada");
      return;
    }
    Set(kPC, resultado);
    return;
  }
  Set(static_cast<int>(rd), resultado);
}

void ArmInterpreter::TransferenciaSimples(std::uint32_t instr, std::uint32_t pc) {
  const bool i = (instr & (1u << 25)) != 0;
  const bool p = (instr & (1u << 24)) != 0;
  const bool u = (instr & (1u << 23)) != 0;
  const bool b = (instr & (1u << 22)) != 0;
  const bool w = (instr & (1u << 21)) != 0;
  const bool l = (instr & (1u << 20)) != 0;
  const uint32_t rn = (instr >> 16) & 0xF;
  const uint32_t rd = (instr >> 12) & 0xF;
  (void)pc;

  Reg deslocamento = instr & 0xFFF;
  if (i) {
    const uint32_t rm = instr & 0xF;
    bool lixo = false;
    deslocamento = Deslocar(Get(static_cast<int>(rm)), (instr >> 5) & 3, (instr >> 7) & 0x1F, c_, &lixo);
  }
  // `rn == 15` significa enderecamento relativo ao PC, e no ARM o PC vale
  // `endereco_da_instrucao + 8`. Esquecer o +8 le o sitio errado -- e o sitio
  // errado aqui e a PROPRIA instrucao, o que da um resultado que parece
  // plausivel e nao e. Um teste de `LDR r0, [pc, #0]` apanhou isto.
  Reg base = Get(static_cast<int>(rn));
  if (rn == kPC) base += 8;
  const Reg deslocado = u ? base + deslocamento : base - deslocamento;
  const Reg endereco = p ? deslocado : base;

  if (l) {
    Reg valor;
    if (b) {
      valor = mem_.Ler8(endereco);
    } else {
      valor = mem_.Ler32(endereco);
      const uint32_t desal = endereco & 3;
      if (desal != 0) valor = (valor >> (desal * 8)) | (valor << (32 - desal * 8));
    }
    Set(static_cast<int>(rd), valor);
  } else {
    const Reg valor = Get(static_cast<int>(rd));
    if (b) {
      mem_.Escrever8(endereco, static_cast<std::uint8_t>(valor & 0xFF));
    } else {
      mem_.Escrever32(endereco, valor);
    }
  }
  if (!p || w) Set(static_cast<int>(rn), deslocado);
}

void ArmInterpreter::Bloco(std::uint32_t instr, std::uint32_t pc) {
  const bool p = (instr & (1u << 24)) != 0;
  const bool u = (instr & (1u << 23)) != 0;
  const bool s = (instr & (1u << 22)) != 0;
  const bool w = (instr & (1u << 21)) != 0;
  const bool l = (instr & (1u << 20)) != 0;
  const uint32_t rn = (instr >> 16) & 0xF;
  const uint32_t lista = instr & 0xFFFF;
  if (lista == 0) { Recusar(instr, pc, "LDM/STM com lista de registradores vazia"); return; }
  int quantos = 0;
  for (int i = 0; i < 16; ++i) if ((lista & (1u << i)) != 0) ++quantos;
  const Reg base = Get(static_cast<int>(rn));
  const uint32_t passo = 4;
  Reg endereco = u ? base + (p ? passo : 0) : base - static_cast<Reg>(quantos) * passo + (p ? 0 : passo);

  for (int i = 0; i < 16; ++i) {
    if ((lista & (1u << i)) == 0) continue;
    if (l) {
      Set(i, mem_.Ler32(endereco));
    } else {
      mem_.Escrever32(endereco, Get(i));
    }
    endereco += passo;
  }
  if (w) Set(static_cast<int>(rn), u ? base + static_cast<Reg>(quantos) * passo
                                     : base - static_cast<Reg>(quantos) * passo);
  if (s) Recusar(instr, pc, "LDM/STM com S (banco de usuario) nao implementado");

  // O PC avanca AQUI, e nao no despachante -- e so aqui se sabe se a lista de
  // registradores inclui o PC. Esquecer isto fez o `STM` correr duas vezes
  // seguidas no primeiro teste de push/pop: o despachante nao avancava o PC
  // depois dos blocos, e o passo seguinte reexecutava a mesma instrucao.
  if ((lista & (1u << kPC)) == 0) Set(kPC, pc + 4);
}

void ArmInterpreter::Bifurcar(std::uint32_t instr, std::uint32_t pc) {
  int32_t deslocamento = static_cast<int32_t>(instr & 0x00FFFFFFu);
  if ((deslocamento & 0x00800000) != 0) deslocamento |= static_cast<int32_t>(0xFF000000u);
  const Reg alvo = pc + 8 + static_cast<Reg>(deslocamento << 2);
  if ((instr & (1u << 24)) != 0) Set(kLR, pc + 4);
  Set(kPC, alvo);
}

void ArmInterpreter::Multiplicar(std::uint32_t instr) {
  const uint32_t rd = (instr >> 16) & 0xF;
  const uint32_t rn = (instr >> 12) & 0xF;
  const uint32_t rs = (instr >> 8) & 0xF;
  const uint32_t rm = instr & 0xF;
  const bool acumula = (instr & (1u << 21)) != 0;
  const bool s = (instrucao_bandeiras(instr));
  // ARM ARM, e importa ler a ordem com cuidado:
  //   MUL  Rd, Rm, Rs      ->  Rd = Rm * Rs        (o campo Rn nao e operando)
  //   MLA  Rd, Rn, Rm, Rs  ->  Rd = Rn * Rm + Rs   (Rn e o primeiro factor E a
  //                                                 parcela somada)
  // A primeira versao deste codigo fazia `Rm * Rs + Rn` para o MLA, que troca
  // os papeis de Rn e Rs. Num teste com 7 * 6 + 100 isso da 1607 em vez de 142
  // -- um erro que so aparece com valores diferentes nos tres campos.
  std::uint32_t r = acumula ? (Get(static_cast<int>(rn)) * Get(static_cast<int>(rm)))
                            : (Get(static_cast<int>(rm)) * Get(static_cast<int>(rs)));
  if (acumula) r += Get(static_cast<int>(rs));
  Set(static_cast<int>(rd), r);
  if (s) { n_ = (r >> 31) != 0; z_ = r == 0; }
}

void ArmInterpreter::MultiplicarLongo(std::uint32_t instr) {
  const uint32_t rd_hi = (instr >> 16) & 0xF;
  const uint32_t rd_lo = (instr >> 12) & 0xF;
  const uint32_t rs = (instr >> 8) & 0xF;
  const uint32_t rm = instr & 0xF;
  const bool com_sinal = (instr & (1u << 22)) != 0;
  const bool acumula = (instr & (1u << 21)) != 0;
  const bool s = (instr & (1u << 20)) != 0;
  if (com_sinal) {
    std::int64_t r = static_cast<std::int64_t>(static_cast<std::int32_t>(Get(static_cast<int>(rm)))) *
                     static_cast<std::int64_t>(static_cast<std::int32_t>(Get(static_cast<int>(rs))));
    if (acumula) {
      r += static_cast<std::int64_t>((static_cast<std::uint64_t>(Get(static_cast<int>(rd_hi))) << 32) |
                                     Get(static_cast<int>(rd_lo)));
    }
    const auto u = static_cast<std::uint64_t>(r);
    Set(static_cast<int>(rd_lo), static_cast<Reg>(u & 0xFFFFFFFFu));
    Set(static_cast<int>(rd_hi), static_cast<Reg>(u >> 32));
    if (s) { n_ = r < 0; z_ = r == 0; }
  } else {
    std::uint64_t r = static_cast<std::uint64_t>(Get(static_cast<int>(rm))) *
                      static_cast<std::uint64_t>(Get(static_cast<int>(rs)));
    if (acumula) {
      r += (static_cast<std::uint64_t>(Get(static_cast<int>(rd_hi))) << 32) |
           Get(static_cast<int>(rd_lo));
    }
    Set(static_cast<int>(rd_lo), static_cast<Reg>(r & 0xFFFFFFFFu));
    Set(static_cast<int>(rd_hi), static_cast<Reg>(r >> 32));
    if (s) { n_ = (r >> 63) != 0; z_ = r == 0; }
  }
}

void ArmInterpreter::TrocarEntreProcessadorEStatus(std::uint32_t instr) {
  if ((instr & (1u << 21)) == 0) {  // MRS
    const uint32_t rd = (instr >> 12) & 0xF;
    const bool spsr = (instr & (1u << 22)) != 0;
    Set(static_cast<int>(rd), spsr ? spsr_[0] : Cpsr());
    return;
  }
  const bool imediato = (instr & (1u << 25)) != 0;
  Reg valor;
  if (imediato) {
    const uint32_t imm = instr & 0xFF;
    const uint32_t rot = ((instr >> 8) & 0xF) * 2;
    valor = rot == 0 ? imm : ((imm >> rot) | (imm << (32 - rot)));
  } else {
    valor = Get(static_cast<int>(instr & 0xF));
  }
  const std::uint32_t mascara_campos = (instr >> 16) & 0xF;
  std::uint32_t novo = Cpsr();
  if ((mascara_campos & 1) != 0) novo = (novo & 0xFFFFFF00u) | (valor & 0xFFu);
  if ((mascara_campos & 8) != 0) novo = (novo & 0x00FFFFFFu) | (valor & 0xFF000000u);
  SetCpsr(novo);
}

void ArmInterpreter::Coprocessador(std::uint32_t instr, std::uint32_t pc) {
  (void)pc;
  const uint32_t cp = (instr >> 8) & 0xF;
  if (cp == 15 && (instr & (1u << 20)) != 0) {  // MRC p15
    // O unico uso medido no corpus e ler o registrador de tipo de cache. Este
    // projeto nao emula cache, e em vez de devolver zero em silencio devolve um
    // valor declarado e registra o que falta.
    const uint32_t rd = (instr >> 12) & 0xF;
    Set(static_cast<int>(rd), 0x410FB760u);
    if (traco_ != nullptr) {
      traco_->RegistarFalta(Area::Cpu, "coprocessador p15 leitura real",
                            "devolvido um valor declarado de cache type");
    }
    return;
  }
  Recusar(instr, pc, "coprocessador nao implementado");
}

void ArmInterpreter::SWI(std::uint32_t instr, std::uint32_t pc) {
  Recusar(instr, pc, "SWI sem tratador registado");
}

void ArmInterpreter::ExecutarArm(std::uint32_t instr, std::uint32_t pc) {
  const std::uint32_t cond = instr >> 28;
  if (cond == 0xF) {
    Recusar(instr, pc, "instrucao com condicao NV");
    Set(kPC, pc + 4);
    return;
  }
  if (!CondicaoVerdadeira(cond)) {
    Set(kPC, pc + 4);
    return;
  }

  // A descodificacao segue a tabela do ARM, e os campos 27-25 sao o primeiro
  // nivel. Isto importa: `MOV r1, #7` e um load/store partilham os bits 27-26
  // (00); o que os separa e o BIT 25. Uma versao anterior deste codigo testava
  // os bits 27-26 primeiro e mandava oito testes de dados processados para o
  // caminho de memoria -- o sintoma era um registrador a ficar a zero.
  //
  //   000 -> dados processados (registrador), multiplicacao, misc
  //   001 -> dados processados com imediato, MSR imediato
  //   010/011 -> transferencia simples (LDR/STR)
  //   100 -> transferencia de bloco (LDM/STM)
  //   101 -> bifurcacao
  //   110/111 -> coprocessador e SWI
  const std::uint32_t g = (instr >> 25) & 7;

  if (g == 0) {
    // O GRUPO "EXTRA LOAD/STORE" VEM PRIMEIRO, e a ordem e a licao mais
    // repetida desta arvore: **do mais especifico para o mais generico**.
    //
    // Sem este ramo, o `LDRD`/`STRD`/`LDRH`/`STRH`/`LDRSB`/`LDRSH` cai no
    // `DadosProcessados` -- que os le como `BIC`/`TEQ` e escreve registradores
    // com lixo. Ver `TransferenciaExtra`.
    if (EhTransferenciaExtra(instr)) {
      TransferenciaExtra(instr, pc);
      Set(kPC, pc + 4);
      return;
    }
    // Grupo das multiplicacoes e do misc, com formas especificas.
    // BX e BLX (forma de registrador). Partilham quase tudo; o que os separa
    // sao os bits 7-4: 0001 para BX, 0011 para BLX -- que alem de saltar guarda
    // o retorno no LR.
    //
    // Faltava o BLX, e o sintoma foi silencioso e preciso: o `blx r1` do
    // primeiro modulo caia no grupo de dados processados, nao saltava, e o
    // modulo seguia como se a chamada ao sistema tivesse acontecido. Um teste
    // com modulo sintetico apanhou-o.
    if ((instr & 0x0FFFFF30u) == 0x012FFF10u || (instr & 0x0FFFFF30u) == 0x012FFF30u) {
      const bool com_retorno = (instr & 0x30u) == 0x30u;
      const Reg alvo = Get(static_cast<int>(instr & 0xF));
      if (com_retorno) Set(kLR, pc + 4);
      if ((alvo & 1) != 0) modo_atual_ |= Cpsr::kT; else modo_atual_ &= ~Cpsr::kT;
      Set(kPC, alvo & ~1u);
      return;
    }
    if ((instr & 0x0FBF0F00u) == 0x010F0000u) {  // MRS
      TrocarEntreProcessadorEStatus(instr);
      Set(kPC, pc + 4);
      return;
    }
    // ORDEM, e as mascaras, importam: o `MUL` e o `UMULL` partilham os bits
    // 7-4 = 1001 e o campo 27-22. O que os separa e o BIT 23 -- ligado nas
    // multiplicacoes longas. Uma mascara que nao o exclua faz o `UMULL` cair no
    // `MUL` e o produto de 64 bits sai truncado a 32 (medido: a metade baixa
    // ficava certa no registrador errado).
    if ((instr & 0x0F8000F0u) == 0x00800090u) { MultiplicarLongo(instr); Set(kPC, pc + 4); return; }
    if ((instr & 0x0FC000F0u) == 0x00000090u) { Multiplicar(instr); Set(kPC, pc + 4); return; }

    if ((instr & 0x0FB00FF0u) == 0x01000090u) {  // SWP/SWPB
      const bool byte = (instr & (1u << 22)) != 0;
      const uint32_t rn = (instr >> 16) & 0xF;
      const uint32_t rd = (instr >> 12) & 0xF;
      const uint32_t rm = instr & 0xF;
      const Reg end = Get(static_cast<int>(rn));
      const Reg antigo = byte ? mem_.Ler8(end) : mem_.Ler32(end);
      if (byte) mem_.Escrever8(end, static_cast<std::uint8_t>(Get(static_cast<int>(rm)) & 0xFF));
      else mem_.Escrever32(end, Get(static_cast<int>(rm)));
      Set(static_cast<int>(rd), antigo);
      Set(kPC, pc + 4);
      return;
    }
    DadosProcessados(instr, pc);
    Set(kPC, pc + 4);
    return;
  }
  if (g == 1) {
    // 001: dados processados com imediato -- EXCEPTO o MSR com imediato, que
    // ocupa o mesmo espaco e se distingue pelo campo de opcode.
    if ((instr & 0x0FB0F000u) == 0x0320F000u) {  // MSR imediato
      TrocarEntreProcessadorEStatus(instr);
      Set(kPC, pc + 4);
      return;
    }
    DadosProcessados(instr, pc);
    Set(kPC, pc + 4);
    return;
  }
  if (g == 2 || g == 3) { TransferenciaSimples(instr, pc); Set(kPC, pc + 4); return; }
  if (g == 4) { Bloco(instr, pc); return; }  // o PC e tratado dentro
  if (g == 5) { Bifurcar(instr, pc); return; }
  if (g == 6) { Coprocessador(instr, pc); Set(kPC, pc + 4); return; }
  // 111: SWI
  SWI(instr, pc);
  Set(kPC, pc + 4);
}

void ArmInterpreter::ExecutarThumb(std::uint16_t instr, std::uint32_t pc) {

  if ((instr & 0xF800u) == 0x1800u) {  // ADD/SUB, registrador ou imediato de 3 bits
    const uint32_t op = (instr >> 9) & 3;
    const uint32_t rm = (instr >> 6) & 7;
    const uint32_t rn = (instr >> 3) & 7;
    const uint32_t rd = instr & 7;
    const Reg b = Get(static_cast<int>(rn));
    const uint32_t terceiro = (instr & 0x0400u) != 0 ? rm : Get(static_cast<int>(rm));
    Reg r = (op == 1 || op == 3) ? b - terceiro : b + terceiro;
    if (op == 1 || op == 3) { auto f = FlagsDaSub(b, terceiro, r); c_ = f.c; v_ = f.v; }
    else { auto f = FlagsDaSoma(b, terceiro, 0, r); c_ = f.c; v_ = f.v; }
    n_ = (r >> 31) != 0; z_ = r == 0;
    Set(static_cast<int>(rd), r);
    Set(kPC, pc + 2);
    return;
  }
  if ((instr & 0xE000u) == 0x2000u) {  // MOV/CMP/ADD/SUB imediato de 8 bits
    const uint32_t op = (instr >> 11) & 3;
    const uint32_t rd = (instr >> 8) & 7;
    const Reg imm = instr & 0xFF;
    const Reg b = Get(static_cast<int>(rd));
    Reg r = 0;
    switch (op) {
      case 0: r = imm; n_ = (r >> 31) != 0; z_ = r == 0; break;
      case 1: r = b - imm; { auto f = FlagsDaSub(b, imm, r); c_ = f.c; v_ = f.v; n_ = (r >> 31) != 0; z_ = r == 0; } break;
      case 2: r = b + imm; { auto f = FlagsDaSoma(b, imm, 0, r); c_ = f.c; v_ = f.v; n_ = (r >> 31) != 0; z_ = r == 0; } break;
      case 3: r = b - imm; { auto f = FlagsDaSub(b, imm, r); c_ = f.c; v_ = f.v; n_ = (r >> 31) != 0; z_ = r == 0; } break;
    }
    Set(static_cast<int>(rd), r);
    Set(kPC, pc + 2);
    return;
  }
  if ((instr & 0xF800u) == 0x4800u) {  // LDR literal
    const uint32_t rd = (instr >> 8) & 7;
    const Reg end = ((pc + 4) & ~3u) + ((instr & 0xFF) << 2);
    Set(static_cast<int>(rd), mem_.Ler32(end));
    Set(kPC, pc + 2);
    return;
  }
  if ((instr & 0xE000u) == 0x6000u || (instr & 0xE000u) == 0x7000u ||
      (instr & 0xE000u) == 0x8000u) {  // LDR/STR, LDRB/STRB, LDRH/STRH
    const uint32_t op = (instr >> 11) & 3;
    const uint32_t imm5 = (instr >> 6) & 0x1F;
    const uint32_t rn = (instr >> 3) & 7;
    const uint32_t rd = instr & 7;
    const Reg base = Get(static_cast<int>(rn));
    const bool meia = (instr & 0xF000u) == 0x8000u && (instr & 0x1000u) != 0;
    const bool byte = (instr & 0xF000u) == 0x7000u;
    if (meia) {
      const Reg end = base + (imm5 << 1);
      if (op == 1) mem_.Escrever16(end, static_cast<std::uint16_t>(Get(static_cast<int>(rd)) & 0xFFFF));
      else Set(static_cast<int>(rd), mem_.Ler16(end));
      Set(kPC, pc + 2);
      return;
    }
    const Reg end = base + (byte ? imm5 : imm5 << 2);
    if (op == 0) {
      if (byte) mem_.Escrever8(end, static_cast<std::uint8_t>(Get(static_cast<int>(rd)) & 0xFF));
      else mem_.Escrever32(end, Get(static_cast<int>(rd)));
    } else {
      if (byte) Set(static_cast<int>(rd), mem_.Ler8(end));
      else Set(static_cast<int>(rd), mem_.Ler32(end));
    }
    Set(kPC, pc + 2);
    return;
  }
  if ((instr & 0xF800u) == 0xE000u) {  // B incondicional
    int32_t d = instr & 0x7FF;
    if ((d & 0x400) != 0) d |= static_cast<int32_t>(0xFFFFF800u);
    Set(kPC, pc + 4 + static_cast<Reg>(d << 1));
    return;
  }
  if ((instr & 0xFF00u) == 0xDF00u) { SWI(instr, pc); Set(kPC, pc + 2); return; }
  if ((instr & 0xFF87u) == 0x4700u) {  // BX/BLX registrador
    const Reg alvo = Get(static_cast<int>((instr >> 3) & 0xF));
    if ((alvo & 1) == 0) modo_atual_ &= ~Cpsr::kT;
    Set(kPC, alvo & ~1u);
    return;
  }
  if ((instr & 0xF000u) == 0xD000u) {  // B condicional
    if (CondicaoVerdadeira((instr >> 8) & 0xF)) {
      int32_t d = instr & 0xFF;
      if ((d & 0x80) != 0) d |= static_cast<int32_t>(0xFFFFFF00u);
      Set(kPC, pc + 4 + static_cast<Reg>(d << 1));
    } else {
      Set(kPC, pc + 2);
    }
    return;
  }
  Recusar(instr, pc, "forma Thumb NAO implementada");
  Set(kPC, pc + 2);
}

std::uint64_t ArmInterpreter::Passo() {
  // O PC NAO avanca aqui. Cada executor decide o proximo PC, e a convencao do
  // ARM de que o PC esta 8 bytes a frente (4 em Thumb) e aplicada onde os
  // operandos a usam.
  //
  // MOTIVO de estar assim: uma versao anterior avancava o PC no `Passo` e
  // descontava 8 no executor, ficando 4 bytes atras -- o executor escrevia
  // `pc + 4` no fim e o PC voltava a instrucao de origem. O sintoma era o
  // programa preso no primeiro endereco, com os registradores a meio.
  const std::uint32_t pc = Get(kPC);
  if ((Cpsr() & Cpsr::kT) != 0) {
    const std::uint16_t instr = static_cast<std::uint16_t>(Buscar16(pc));
    ExecutarThumb(instr, pc);
  } else {
    const std::uint32_t instr = Buscar32(pc);
    ExecutarArm(instr, pc);
  }
  return 1;
}

std::uint64_t ArmInterpreter::Correr(std::uint64_t limite) {
  std::uint64_t n = 0;
  while (n < limite) {
    // Parar ANTES de executar: o PC dentro da faixa de saida e uma chamada ao
    // C++, nao codigo do guest. Quem chama `Correr` despacha e retoma.
    if (saidas_.Contem(Get(kPC))) return n;
    // O limite existe para que um laco sem fim no guest NUNCA prenda o emulador
    // em silencio. Foi um defeito medido: o `chessbots` passou 22 segundos
    // dentro de uma unica chamada, e o diagnostico ficava mudo porque dependia
    // de o laco principal voltar a iterar.
    Passo();
    ++n;
  }
  return n;
}

}  // namespace zb2
