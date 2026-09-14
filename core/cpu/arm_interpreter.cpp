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
  // A sonda do descodificador le daqui: `FamiliaDaUltima()` diz O QUE era a
  // instrucao, e este campo diz O QUE FALTOU. Quem le a recusa na bateria ve um
  // numero; quem le isto ve a forma que falta.
  motivo_recusa_ = porque;
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
  familia_ = "-";
  motivo_recusa_ = nullptr;
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

  // NOME da forma, para a sonda. O nome vem dos bits, e nao do caminho que o
  // codigo toma: uma forma RECUSADA continua a ter nome (o `ldrh` com Rn = PC e
  // `ldrh`), e quem quer o motivo le `MotivoDaRecusa`. Sem isto, a recusa
  // apagaria a informacao de QUAL instrucao faltou.
  if (palavra_dupla) {
    familia_ = dupla_carrega ? "ldrd" : "strd";
  } else if (campo == 0xBu) {
    familia_ = carrega ? "ldrh" : "strh";
  } else {
    // O campo 7-4 e que separa: 1101 e o LDRSB, 1111 e o LDRSH. A primeira
    // versao desta sonda escrevia "ldrsb" nos DOIS, e foi o auditor, na
    // primeira corrida, que o apanhou (`e1d000f0` = `ldrsh r0, [r0]`).
    familia_ = (campo == 0xDu) ? "ldrsb" : "ldrsh";
  }

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

// ---------------------------------------------------------------------------
// ARMv6: O GRUPO "MEDIA" (extensao de sinal/zero e reversao de bytes)
// ---------------------------------------------------------------------------
//
// PORQUE EXISTE ESTE RAMO, e a medicao que o obrigou (auditor diferencial,
// `tools/auditar_descodificador.py`, corpus dos 62 titulos):
//
//   uxth  17 295 palavras -> o interpretador executava `ldrb`
//   uxtb  12 764 palavras -> o interpretador executava `strb`
//   sxth   2 183 palavras -> o interpretador executava `ldr`
//   sxtb     781 palavras -> o interpretador executava `str`
//   sxtab  6 432 palavras -> o interpretador executava `str`
//
// Os bits 27-24 destas instrucoes sao 0110, que o primeiro nivel de
// descodificacao le como TRANSFERENCIA SIMPLES (bits 27-25 = 011) -- exactamente
// o mesmo defeito de classe do `ldrd` (bits 27-25 = 000 lidos como dados
// processados): uma instrucao que NAO recusa, corre outra coisa e da um
// resultado plausivel.
//
// O QUE SEPARA AS DUAS COISAS: numa transferencia com offset de registrador o
// BIT 4 e ZERO (bits 11-4 = deslocamento: bits 11-7 quantidade, bits 6-5 tipo,
// bit 4 = 0). Todas as formas "media" tem o bit 4 = 1. Logo
// `bits 27-24 = 0110 e bit 4 = 1` e a fronteira, e e ela que o despachante testa.
//
// A TABELA FOI DERIVADA DO BINUTILS, e nao escrita de memoria: varreu-se
// bits 27-20 x bits 19-16 x bits 11-4 (65 536 palavras) e leu-se o nome que o
// `arm-none-eabi-objdump -D -b binary -m armv6` da a cada uma; a mascara de cada
// forma e o conjunto de bits que NAO variam dentro de um mesmo nome. Zero
// ambiguidades: nenhum par de mascaras casa com a mesma palavra.
namespace {

enum class SemanticaMedia {
  Nenhuma,            // a forma existe e NAO esta implementada -> recusa com nome
  Extensao,           // SXTB/SXTH/UXTB/UXTH
  ExtensaoAcumulada,  // SXTAB/SXTAH/UXTAB/UXTAH
  Reverter,           // REV
  Reverter16,         // REV16
  ReverterSinal16,    // REVSH
};

struct FormaMedia {
  std::uint32_t mascara;
  std::uint32_t valor;
  const char* nome;
  SemanticaMedia semantica;
  bool com_sinal;   // a extensao e com sinal
  int largura;      // 8 ou 16 bits
};

const FormaMedia kFormasMedia[] = {
    // -- implementadas -------------------------------------------------------
    // A ordem importa: as formas SEM acumulacao (bits 19-16 = 1111) tem de ser
    // testadas antes das formas "A", cuja mascara deixa os bits 19-16 livres --
    // `0x06AF0070 & 0x0FF003F0` da `0x06A00070`, ou seja, o SXTB tambem casa com
    // a mascara do SXTAB. A primeira versao desta tabela nao tinha a ordem e o
    // SXTB era lido como SXTAB.
    {0x0FFF03F0u, 0x06AF0070u, "sxtb", SemanticaMedia::Extensao, true, 8},
    {0x0FFF03F0u, 0x06BF0070u, "sxth", SemanticaMedia::Extensao, true, 16},
    {0x0FFF03F0u, 0x06EF0070u, "uxtb", SemanticaMedia::Extensao, false, 8},
    {0x0FFF03F0u, 0x06FF0070u, "uxth", SemanticaMedia::Extensao, false, 16},
    {0x0FF003F0u, 0x06A00070u, "sxtab", SemanticaMedia::ExtensaoAcumulada, true, 8},
    {0x0FF003F0u, 0x06B00070u, "sxtah", SemanticaMedia::ExtensaoAcumulada, true, 16},
    {0x0FF003F0u, 0x06E00070u, "uxtab", SemanticaMedia::ExtensaoAcumulada, false, 8},
    {0x0FF003F0u, 0x06F00070u, "uxtah", SemanticaMedia::ExtensaoAcumulada, false, 16},
    {0x0FFF0FF0u, 0x06BF0F30u, "rev", SemanticaMedia::Reverter, false, 32},
    {0x0FFF0FF0u, 0x06BF0FB0u, "rev16", SemanticaMedia::Reverter16, false, 32},
    {0x0FFF0FF0u, 0x06FF0FB0u, "revsh", SemanticaMedia::ReverterSinal16, false, 32},
    // -- conhecidas e RECUSADAS com nome (P2) --------------------------------
    // A aritmetica paralela (SADD16...) tem de mexer nas bandeiras GE do CPSR,
    // que este interpretador nao emula; faze-la "a meias" daria resultados
    // errados EM SILENCIO, que e o defeito que esta arvore persegue. O mesmo
    // vale para o SEL, cujo resultado depende dessas bandeiras.
    {0x0FFF03F0u, 0x068F0070u, "sxtb16", SemanticaMedia::Nenhuma, false, 0},
    {0x0FFF03F0u, 0x06CF0070u, "uxtb16", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF003F0u, 0x06800070u, "sxtab16", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF003F0u, 0x06C00070u, "uxtab16", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00FF0u, 0x06800FB0u, "sel", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00070u, 0x06800010u, "pkhbt", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00070u, 0x06800050u, "pkhtb", SemanticaMedia::Nenhuma, false, 0},
    {0x0FE00030u, 0x06A00010u, "ssat", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00FF0u, 0x06A00F30u, "ssat16", SemanticaMedia::Nenhuma, false, 0},
    {0x0FE00030u, 0x06E00010u, "usat", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00FF0u, 0x06E00F30u, "usat16", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00FF0u, 0x06100F10u, "sadd16", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00FF0u, 0x06100F30u, "sasx", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00FF0u, 0x06100F50u, "ssax", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00FF0u, 0x06100F70u, "ssub16", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00FF0u, 0x06100F90u, "sadd8", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00FF0u, 0x06100FF0u, "ssub8", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00FF0u, 0x06200F10u, "qadd16", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00FF0u, 0x06200F30u, "qasx", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00FF0u, 0x06200F50u, "qsax", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00FF0u, 0x06200F70u, "qsub16", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00FF0u, 0x06200F90u, "qadd8", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00FF0u, 0x06200FF0u, "qsub8", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00FF0u, 0x06300F10u, "shadd16", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00FF0u, 0x06300F30u, "shasx", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00FF0u, 0x06300F50u, "shsax", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00FF0u, 0x06300F70u, "shsub16", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00FF0u, 0x06300F90u, "shadd8", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00FF0u, 0x06300FF0u, "shsub8", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00FF0u, 0x06500F10u, "uadd16", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00FF0u, 0x06500F30u, "uasx", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00FF0u, 0x06500F50u, "usax", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00FF0u, 0x06500F70u, "usub16", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00FF0u, 0x06500F90u, "uadd8", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00FF0u, 0x06500FF0u, "usub8", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00FF0u, 0x06600F10u, "uqadd16", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00FF0u, 0x06600F30u, "uqasx", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00FF0u, 0x06600F50u, "uqsax", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00FF0u, 0x06600F70u, "uqsub16", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00FF0u, 0x06600F90u, "uqadd8", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00FF0u, 0x06600FF0u, "uqsub8", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00FF0u, 0x06700F10u, "uhadd16", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00FF0u, 0x06700F30u, "uhasx", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00FF0u, 0x06700F50u, "uhsax", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00FF0u, 0x06700F70u, "uhsub16", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00FF0u, 0x06700F90u, "uhadd8", SemanticaMedia::Nenhuma, false, 0},
    {0x0FF00FF0u, 0x06700FF0u, "uhsub8", SemanticaMedia::Nenhuma, false, 0},
};

// A FRONTEIRA da transferencia simples com offset de registrador: bit 4 = 0.
bool EhMediaArmv6(std::uint32_t instr) {
  return (instr & 0x0F000010u) == 0x06000010u;
}

// Rotacao de bytes: `rotr` (bits 11-10 das extensoes) roda o registrador de
// origem em multiplos de 8 bits, e o ARM ARM manda rodar ANTES de extrair.
Reg RodarBytes(Reg valor, std::uint32_t quanto) {
  const std::uint32_t r = (quanto & 3u) * 8u;
  if (r == 0) return valor;
  return (valor >> r) | (valor << (32u - r));
}

}  // namespace

void ArmInterpreter::MediaArmv6(std::uint32_t instr, std::uint32_t pc) {
  const std::uint32_t rd = (instr >> 12) & 0xFu;
  const std::uint32_t rn = (instr >> 16) & 0xFu;
  const std::uint32_t rm = instr & 0xFu;
  const std::uint32_t rot = (instr >> 10) & 3u;

  const FormaMedia* forma = nullptr;
  for (const FormaMedia& f : kFormasMedia) {
    if ((instr & f.mascara) == f.valor) { forma = &f; break; }
  }
  if (forma == nullptr) {
    // O espaco media tem palavras que o binutils tambem nao descodifica: o
    // caminho correcto e RECUSAR, e nao inventar uma forma.
    familia_ = "media_armv6_desconhecida";
    Recusar(instr, pc, "forma do grupo media do ARMv6 nao implementada");
    return;
  }
  familia_ = forma->nome;

  // A RODAGEM SO EXISTE NAS FORMAS DE EXTENSAO. Nas de reversao os bits 11-10
  // fazem parte do proprio opcode (0xF3 no REV, 0xFB no REV16/REVSH) e rodar por
  // eles da um resultado errado que parece plausivel -- foi um teste que o
  // apanhou (`rev` de 0x11223344 dava 0x11442222 em vez de 0x44332211).
  const Reg origem = (forma->semantica == SemanticaMedia::Extensao ||
                      forma->semantica == SemanticaMedia::ExtensaoAcumulada)
                         ? RodarBytes(Get(static_cast<int>(rm)), rot)
                         : Get(static_cast<int>(rm));
  switch (forma->semantica) {
    case SemanticaMedia::Extensao:
    case SemanticaMedia::ExtensaoAcumulada: {
      Reg valor = 0;
      if (forma->largura == 8) {
        valor = forma->com_sinal ? static_cast<Reg>(static_cast<std::int32_t>(static_cast<std::int8_t>(origem & 0xFFu)))
                                 : static_cast<Reg>(origem & 0xFFu);
      } else {
        valor = forma->com_sinal ? static_cast<Reg>(static_cast<std::int32_t>(static_cast<std::int16_t>(origem & 0xFFFFu)))
                                 : static_cast<Reg>(origem & 0xFFFFu);
      }
      if (forma->semantica == SemanticaMedia::ExtensaoAcumulada) valor += Get(static_cast<int>(rn));
      Set(static_cast<int>(rd), valor);
      return;
    }
    case SemanticaMedia::Reverter:
      Set(static_cast<int>(rd), ((origem & 0xFFu) << 24) | ((origem & 0xFF00u) << 8) |
                                ((origem >> 8) & 0xFF00u) | ((origem >> 24) & 0xFFu));
      return;
    case SemanticaMedia::Reverter16:
      Set(static_cast<int>(rd), ((origem & 0xFF00FF00u) >> 8) | ((origem & 0x00FF00FFu) << 8));
      return;
    case SemanticaMedia::ReverterSinal16: {
      const Reg trocado = ((origem & 0xFF00u) >> 8) | ((origem & 0xFFu) << 8);
      Set(static_cast<int>(rd), static_cast<Reg>(static_cast<std::int32_t>(static_cast<std::int16_t>(trocado & 0xFFFFu))));
      return;
    }
    case SemanticaMedia::Nenhuma:
    default:
      Recusar(instr, pc, "forma do grupo media do ARMv6 conhecida e NAO implementada");
      return;
  }
}

// ---------------------------------------------------------------------------
// ARMv5TE: A ARITMETICA DSP (multiplicacoes de meia-palavra) e o CLZ
// ---------------------------------------------------------------------------
//
// PORQUE EXISTE, e o que o auditor mediu no corpus dos 62 titulos:
//   smulbb 1 105 palavras, smlabb 1 013, clz 259, smulwy 215 -- todas elas
//   executadas como `cmn`/`tst`/`teq` (o opcode do grupo de dados processados
//   que partilha os bits 27-25 = 000).
//
// A FRONTEIRA: `bits 27-24 = 0001` com os campos 7-4 em {1yx0, 1y10, 0101}. Do
// mesmo espaco fazem parte o SWP (bits 7-4 = 1001) e o BKPT/HLT (bits 7-4 =
// 0111), que ficam FORA desta funcao e RECUSAM no sitio deles: o BKPT entra em
// modo de depuracao, que este emulador nao tem, e o SWP ja tem implementacao.
//
// AS MASCARAS FORAM DERIVADAS DO BINUTILS (ver o comentario da tabela media) e
// NAO da documentacao: no `SMULxy` os bits 15-12 sao reservados e TEM de ser
// zero -- medido, `0xE1641382` (bits 15-12 = 1) nao e `smulbb`, e `cmn`. Uma
// mascara que os deixasse livres trocava uma instrucao de dados processados por
// uma multiplicacao, EM SILENCIO.
namespace {

struct FormaDsp {
  std::uint32_t mascara;
  std::uint32_t valor;
  const char* nome;
};

const FormaDsp kFormasDsp[] = {
    {0x0FFF0FF0u, 0x016F0F10u, "clz"},
    {0x0FF00FF0u, 0x01000050u, "qadd"},
    {0x0FF00FF0u, 0x01200050u, "qsub"},
    {0x0FF00FF0u, 0x01400050u, "qdadd"},
    {0x0FF00FF0u, 0x01600050u, "qdsub"},
    // AS MASCARAS DAS FORMAS `xy` TEM DE DEIXAR OS BITS 6-5 LIVRES: sao eles que
    // escolhem as metades (BB/BT/TB/TT). A primeira versao desta tabela usava
    // `0x0FF000F0` (o campo 7-4 inteiro fixo) e so a variante `BB` era
    // reconhecida -- as outras tres caiam no grupo de dados processados e davam
    // `teq`/`cmn`/`tst`/`cmp`. Medido pelo auditor sobre a varredura do espaco:
    // 2 862 palavras divergentes. Os bits 15-12 ficam livres quando ha acumulador
    // (Ra) e sao ZERO obrigatorio no `SMULxy`/`SMULWy` (medido: `0xE1641382` com
    // bits 15-12 = 1 nao e `smulbb`, e `cmn`).
    {0x0FF0F090u, 0x01600080u, "smulxy"},
    {0x0FF00090u, 0x01000080u, "smlaxy"},
    {0x0FF00090u, 0x01400080u, "smlalxy"},
    {0x0FF0F0B0u, 0x012000A0u, "smulwy"},
    {0x0FF000B0u, 0x01200080u, "smlawy"},
    {0x0FF000F0u, 0x01000070u, "hlt"},
    {0x0FF000F0u, 0x01200070u, "bkpt"},
};

// A FRONTEIRA da aritmetica DSP: e EXACTAMENTE o conjunto da tabela acima, e nao
// uma segunda expressao dos mesmos bits escrita a mao. Duas expressoes dos mesmos
// bits divergem, e o despachante passa a testar uma coisa e a executar outra --
// que e a classe de defeito que este auditor persegue.
bool EhAritmeticaDsp(std::uint32_t instr) {
  for (const FormaDsp& f : kFormasDsp) {
    if ((instr & f.mascara) == f.valor) return true;
  }
  return false;
}

std::int32_t MeiaPalavra(Reg v, bool alto) {
  return alto ? static_cast<std::int32_t>(static_cast<std::int16_t>(v >> 16))
              : static_cast<std::int32_t>(static_cast<std::int16_t>(v & 0xFFFFu));
}

// Satura para 32 bits com sinal e diz se saturou (a bandeira Q do CPSR depende
// disso).
Reg Saturar(std::int64_t v, bool* saturou) {
  if (v > 2147483647LL) { *saturou = true; return 0x7FFFFFFFu; }
  if (v < -2147483648LL) { *saturou = true; return 0x80000000u; }
  return static_cast<Reg>(static_cast<std::int32_t>(v));
}

}  // namespace

void ArmInterpreter::AritmeticaDsp(std::uint32_t instr, std::uint32_t pc) {
  const uint32_t campo_27_20 = (instr >> 20) & 0xFFu;
  const uint32_t campo_7_4 = (instr >> 4) & 0xFu;
  const uint32_t rd = (instr >> 12) & 0xFu;
  const uint32_t rn = (instr >> 16) & 0xFu;
  const uint32_t rs = (instr >> 8) & 0xFu;
  const uint32_t rm = instr & 0xFu;
  const bool x_alto = (instr & 0x20u) != 0;  // bit 5: metade de Rm
  const bool y_alto = (instr & 0x40u) != 0;  // bit 6: metade de Rs

  const FormaDsp* forma = nullptr;
  for (const FormaDsp& f : kFormasDsp) {
    if ((instr & f.mascara) == f.valor) { forma = &f; break; }
  }
  if (forma == nullptr) {
    familia_ = "dsp_desconhecida";
    Recusar(instr, pc, "instrucao do grupo DSP/QADD do ARMv5TE nao implementada");
    return;
  }

  const std::string nome = forma->nome;
  // O nome da sonda e o do objdump, com as letras das metades. As cadeias sao
  // literais ESTATICOS: o `familia_` e um `const char*` que a sonda le depois do
  // passo, e uma cadeia temporaria seria um ponteiro para memoria morta.
  const int xy = (x_alto ? 2 : 0) | (y_alto ? 1 : 0);
  static const char* const kNomeXy[3][4] = {
      {"smulbb", "smulbt", "smultb", "smultt"},
      {"smlabb", "smlabt", "smlatb", "smlatt"},
      {"smlalbb", "smlalbt", "smlaltb", "smlaltt"},
  };
  static const char* const kNomeWy[2][2] = {{"smulwb", "smulwt"}, {"smlawb", "smlawt"}};
  if (nome == "smulxy") familia_ = kNomeXy[0][xy];
  else if (nome == "smlaxy") familia_ = kNomeXy[1][xy];
  else if (nome == "smlalxy") familia_ = kNomeXy[2][xy];
  else if (nome == "smulwy") familia_ = kNomeWy[0][y_alto ? 1 : 0];
  else if (nome == "smlawy") familia_ = kNomeWy[1][y_alto ? 1 : 0];
  else familia_ = forma->nome;

  if (nome == "bkpt" || nome == "hlt") {
    // Nao e uma classe em falta: o BKPT provoca uma excepcao de depuracao, e
    // este emulador nao tem depurador. Recusar e a resposta honesta.
    Recusar(instr, pc, "BKPT/HLT: o emulador nao tem depurador");
    return;
  }
  if (nome == "clz") {
    Reg v = Get(static_cast<int>(rm));
    Reg n = 0;
    while (n < 32 && (v & 0x80000000u) == 0) { v <<= 1; ++n; }
    Set(static_cast<int>(rd), n);
    return;
  }
  if (campo_7_4 == 0x5u && (campo_27_20 & 0xF9u) == 0x10u) {
    // QADD/QSUB/QDADD/QDSUB: `campo_27_20` = 0x10/0x12/0x14/0x16 -> os dois bits
    // do meio escolhem a operacao. O ARM ARM: Rd = Rn +/- Rm, com o dobro de Rm
    // nas formas D.
    const Reg valor_rn = Get(static_cast<int>(rn));
    const Reg valor_rm = Get(static_cast<int>(rm));
    const bool dobrar = (campo_27_20 & 0x4u) != 0;
    const bool subtrair = (campo_27_20 & 0x2u) != 0;
    bool saturou = false;
    Reg parcela = valor_rm;
    if (dobrar) {
      // O dobro satura ANTES da soma (e a primeira saturacao que pode por o Q).
      parcela = Saturar(2LL * static_cast<std::int64_t>(static_cast<std::int32_t>(valor_rm)), &saturou);
    }
    const std::int64_t a = static_cast<std::int64_t>(static_cast<std::int32_t>(valor_rn));
    const std::int64_t b = static_cast<std::int64_t>(static_cast<std::int32_t>(parcela));
    const Reg resultado = Saturar(subtrair ? a - b : a + b, &saturou);
    if (saturou) modo_atual_ |= (1u << 27);  // a bandeira Q do CPSR
    Set(static_cast<int>(rd), resultado);
    return;
  }

  const std::int32_t a = MeiaPalavra(Get(static_cast<int>(rm)), x_alto);
  const std::int32_t b = MeiaPalavra(Get(static_cast<int>(rs)), y_alto);
  const std::int64_t produto = static_cast<std::int64_t>(a) * static_cast<std::int64_t>(b);

  if (nome == "smulxy") {
    Set(static_cast<int>(rn), static_cast<Reg>(static_cast<std::int32_t>(produto)));
    return;
  }
  if (nome == "smlaxy") {
    const Reg resultado = static_cast<Reg>(static_cast<std::int32_t>(
        produto + static_cast<std::int64_t>(static_cast<std::int32_t>(Get(static_cast<int>(rd))))));
    Set(static_cast<int>(rn), resultado);
    return;
  }
  if (nome == "smlalxy") {
    // Duas metades de destino: RdHi = bits 19-16, RdLo = bits 15-12.
    const std::uint64_t antigo = (static_cast<std::uint64_t>(Get(static_cast<int>(rn))) << 32) |
                                 Get(static_cast<int>(rd));
    const std::uint64_t novo = antigo + static_cast<std::uint64_t>(produto);
    Set(static_cast<int>(rn), static_cast<Reg>(novo >> 32));
    Set(static_cast<int>(rd), static_cast<Reg>(novo & 0xFFFFFFFFu));
    return;
  }
  // smulwy / smlawy: o `W` diz que o PRIMEIRO operando e a palavra inteira (e
  // nao uma metade), o `B`/`T` qual a metade de Rs, e o resultado e a metade
  // alta do produto. E o que a instrucao faz numa multiplicacao de ponto fixo
  // 16.16 por uma fraccao Q15.
  const std::int64_t bruto = static_cast<std::int64_t>(static_cast<std::int32_t>(Get(static_cast<int>(rm)))) *
                             static_cast<std::int64_t>(b);
  const Reg meio = static_cast<Reg>(static_cast<std::uint64_t>(bruto) >> 16);
  if (nome == "smlawy") {
    Set(static_cast<int>(rn), static_cast<Reg>(meio + Get(static_cast<int>(rd))));
    return;
  }
  Set(static_cast<int>(rn), meio);
}

void ArmInterpreter::DadosProcessados(std::uint32_t instr, std::uint32_t pc) {
  const std::uint32_t opcode = (instr >> 21) & 0xF;
  // Nome do mnemonico, para a sonda do descodificador. A tabela e a do ARM ARM
  // A5.2.1, pela ordem do campo opcode -- e o mesmo campo que o `switch` abaixo
  // consome, logo nao ha duas listas para divergirem.
  static const char* const kNomes[16] = {"and", "eor", "sub", "rsb", "add", "adc", "sbc", "rsc",
                                         "tst", "teq", "cmp", "cmn", "orr", "mov", "bic", "mvn"};
  familia_ = kNomes[opcode];
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
  familia_ = l ? (b ? "ldrb" : "ldr") : (b ? "strb" : "str");

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
  familia_ = l ? (u ? (p ? "ldmib" : "ldmia") : (p ? "ldmdb" : "ldmda"))
               : (u ? (p ? "stmib" : "stmia") : (p ? "stmdb" : "stmda"));
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
  familia_ = ((instr & (1u << 24)) != 0) ? "bl" : "b";
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
  familia_ = acumula ? "mla" : "mul";
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
  familia_ = com_sinal ? (acumula ? "smlal" : "smull") : (acumula ? "umlal" : "umull");
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
  familia_ = ((instr & (1u << 21)) == 0) ? "mrs" : "msr";
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
  {
    const bool carrega = (instr & (1u << 20)) != 0;
    // bits 27-25 = 110 (bit 25 = 0) e o LDC/STC; bits 27-25 = 111 (bit 25 = 1) e
    // o CDP/MCR/MRC. A primeira versao desta sonda tinha o bit trocado e chamava
    // `ldc` ao `mcr` -- apanhado pelo auditor na primeira corrida.
    const bool para_memoria = (instr & (1u << 25)) == 0;
    if (para_memoria && (instr & 0xF0u) == 0x50u) {
      // bits 7-4 = 0101: e a transferencia de DOIS registradores de
      // coprocessador (MCRR/MRRC), e nao um LDC/STC. O nome e do objdump;
      // antes desta correccao o auditor chamava-lhe `ldc` (94 palavras no
      // corpus) -- o nome errado num instrumento e o mesmo defeito de P7.
      familia_ = carrega ? "mrrc" : "mcrr";
    } else if (para_memoria) {
      familia_ = carrega ? "ldc" : "stc";
    } else if ((instr & (1u << 4)) != 0) {
      familia_ = carrega ? "mrc" : "mcr";
    } else {
      familia_ = "cdp";
    }
  }
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
  familia_ = "swi";
  Recusar(instr, pc, "SWI sem tratador registado");
}

void ArmInterpreter::ExecutarArm(std::uint32_t instr, std::uint32_t pc) {
  // A sonda do descodificador comeca em "nada correu": cada ramo abaixo escreve o
  // NOME do que correu (ou da forma em falta), e `Recusar` acrescenta o motivo.
  familia_ = "nenhuma";
  motivo_recusa_ = nullptr;
  const std::uint32_t cond = instr >> 28;
  if (cond == 0xF) {
    // TRES FORMAS TEM CONDICAO 1111 POR CONSTRUCAO, e recusa-las e recusar
    // instrucoes validas. Medido no corpus: 4 630 `pld` contados como recusa.
    //
    //   `pld` (dica de pre-carga): `1111 0101 U101 Rn 1111 imm12` e a forma de
    //   registrador `1111 0111 U101 Rn 1111 0000 00 shift Rm`. O PLD NAO tem
    //   efeito nenhum sobre a memoria nem sobre os registradores (ARM ARM, "hint
    //   instructions"), logo executa-lo como nada NAO e um stub silencioso: e a
    //   semantica inteira dele. Conferido com o objdump: 0xF5D0F000 = `pld [r0]`,
    //   0xF550F000 = `pld [r0, #-0]`, 0xF7D0F001 = `pld [r0, r1]`.
    if ((instr & 0xFF70F000u) == 0xF550F000u || (instr & 0xFF70F000u) == 0xF750F000u) {
      familia_ = "pld";
      Set(kPC, pc + 4);
      return;
    }
    //   `blx <rotulo>`: `1111 101H imm24`. Muda para Thumb, e o `H` (bit 24) e a
    //   metade baixa do deslocamento. Medido no corpus: 0xfa000000 contado como
    //   "instrucao com condicao NV".
    if ((instr & 0xFE000000u) == 0xFA000000u) {
      familia_ = "blx_imediato";
      int32_t deslocamento = static_cast<int32_t>(instr & 0x00FFFFFFu);
      if ((deslocamento & 0x00800000) != 0) deslocamento |= static_cast<int32_t>(0xFF000000u);
      const Reg alvo = pc + 8 + static_cast<Reg>(deslocamento << 2) + (((instr >> 24) & 1u) << 1);
      Set(kLR, pc + 4);
      modo_atual_ |= Cpsr::kT;
      Set(kPC, alvo & ~1u);
      return;
    }
    familia_ = "nv";
    Recusar(instr, pc, "instrucao com condicao NV");
    Set(kPC, pc + 4);
    return;
  }
  if (!CondicaoVerdadeira(cond)) {
    // Com a condicao FALSA nao ha instrucao executada. Reportar aqui um nome
    // seria a sonda a mentir: o `ldrd` que motivou este auditor vivia
    // precisamente de um ramo que dizia uma coisa e fazia outra.
    familia_ = "condicao_falsa";
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
    // A ARITMETICA DSP DO ARMv5TE (SMULxy/SMLAWy/QADD/CLZ) vive nos mesmos bits
    // 27-25 = 000, e sem este ramo cada `smulbb` corre como `cmn` (medido: 1 105
    // `smulbb` no corpus, 1 013 `smlabb`, 259 `clz`).
    if (EhAritmeticaDsp(instr)) { AritmeticaDsp(instr, pc); Set(kPC, pc + 4); return; }
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
      familia_ = com_retorno ? "blx" : "bx";
      const Reg alvo = Get(static_cast<int>(instr & 0xF));
      if (com_retorno) Set(kLR, pc + 4);
      if ((alvo & 1) != 0) modo_atual_ |= Cpsr::kT; else modo_atual_ &= ~Cpsr::kT;
      Set(kPC, alvo & ~1u);
      return;
    }
    // A MASCARA DO MRS VAI ATE AO BIT 0, e a razao e medida: a codificacao do
    // MRS e `cond 00010 R 00 1111 Rd 0000 0000 0000` -- os doze bits baixos sao
    // ZERO, e com a mascara antiga (`0x0FBF0F00`, que os deixava livres) o
    // `swp r0, r0, [pc]` (0xE10F0090) era descodificado como MRS, porque o teste
    // do MRS vem antes do SWP. Conferido no objdump: 0xE10F0090 = `swp`,
    // 0xE10F0010 = `tst`.
    if ((instr & 0x0FBF0FFFu) == 0x010F0000u) {  // MRS
      TrocarEntreProcessadorEStatus(instr);
      Set(kPC, pc + 4);
      return;
    }
    // MSR com REGISTRADOR. Faltava, e o sintoma era silencioso: `msr CPSR_f, r0`
    // (0xE128F000) caia no grupo de dados processados como `teq` -- lia os
    // campos como um TEQ e NAO escrevia o CPSR. Medido no `a3d.mod` (+0x2d2d0,
    // 0x012dfda4 = `msreq CPSR_fsc, r4, lsr #27`).
    if ((instr & 0x0FB0FFF0u) == 0x0120F000u) {  // MSR registrador
      TrocarEntreProcessadorEStatus(instr);
      Set(kPC, pc + 4);
      return;
    }
    // BKPT/HLT: entram em modo de depuracao, que este emulador NAO tem. Uma
    // palavra destas num fluxo de codigo e um ponto de quebra, e executa-la como
    // `teq` (o que acontecia) escondia-o.
    if ((instr & 0x0FF000F0u) == 0x01200070u || (instr & 0x0FF000F0u) == 0x01000070u) {
      familia_ = ((instr & 0x0FF000F0u) == 0x01200070u) ? "bkpt" : "hlt";
      Recusar(instr, pc, "BKPT/HLT: o emulador nao tem depurador");
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
      familia_ = byte ? "swpb" : "swp";
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
  if (g == 2 || g == 3) {
    // ANTES da transferencia simples: o grupo "media" do ARMv6 (SXTB/UXTH/REV)
    // partilha estes bits 27-25 = 011. O bit 4 e o que separa -- ver
    // `EhMediaArmv6`. Medido no corpus: 17 295 `uxth` executados como `ldrb`.
    if (EhMediaArmv6(instr)) { MediaArmv6(instr, pc); Set(kPC, pc + 4); return; }
    TransferenciaSimples(instr, pc);
    Set(kPC, pc + 4);
    return;
  }
  if (g == 4) { Bloco(instr, pc); return; }  // o PC e tratado dentro
  if (g == 5) { Bifurcar(instr, pc); return; }
  if (g == 6) { Coprocessador(instr, pc); Set(kPC, pc + 4); return; }
  // 111: com o bit 24 a UM e o `SWI` (a codificacao e `cond 1111 imm24`, e o
  // bit 24 = 1 vem do `1111`); com o bit 24 a ZERO e o grupo de COPROCESSADOR
  // (CDP/MCR/MRC, que se escrevem `cond 1110 ...`). O interpretador mandava os DOIS para o `SWI`, e
  // o efeito era medivel: `mrc p15, 0, r0, c1, c0, 0` (a leitura do tipo de
  // cache, que o `Coprocessador` implementa) NUNCA chegava la, e o `cdp`/`mcr`
  // apareciam como "SWI sem tratador registado".
  if ((instr & (1u << 24)) == 0) {
    Coprocessador(instr, pc);
    Set(kPC, pc + 4);
    return;
  }
  SWI(instr, pc);
  Set(kPC, pc + 4);
}

namespace {

// O NOME DA FORMA DE PRIMEIRO NIVEL DO THUMB, do ARM ARM (A6.2), para a RECUSA
// dizer QUAL falta. As 16 formas dos bits 15-12; as que este interpretador
// implementa escrevem o mnemonico do objdump e nao param aqui.
const char* NomeDoFormatoThumb(std::uint16_t instr) {
  switch (instr >> 12) {
    case 0x0: return "thumb:formato1_deslocamento_imediato";
    case 0x1: return "thumb:formato2_add_sub_registrador";
    case 0x2:
    case 0x3: return "thumb:formato3_mov_cmp_add_sub_imediato";
    case 0x4: return "thumb:formato4_operacoes_alu";
    case 0x5:
    case 0x6: return "thumb:formato5_6_memoria_com_registrador";
    case 0x7:
    case 0x8: return "thumb:formato7_8_memoria_com_imediato";
    case 0xA: return "thumb:formato10_add_rd_pc_sp";
    case 0xB: return "thumb:formato11_add_sub_sp";
    case 0xC: return "thumb:formato12_stmia_ldmia";
    case 0xE: return "thumb:formato19_sufixo_blx";
    case 0xF: return "thumb:formato19_bl_blx";
    default: return "thumb:formato_desconhecido";
  }
}

}  // namespace

void ArmInterpreter::ExecutarThumb(std::uint16_t instr, std::uint32_t pc) {
  // A sonda do descodificador, do lado do Thumb. O Thumb tem 19 formas de
  // primeiro nivel, e as que FALTAM RECUSAM com nome (`forma Thumb NAO
  // implementada`); o auditor compara este nome com o do objdump.
  familia_ = "nenhuma";
  motivo_recusa_ = nullptr;
  if ((instr & 0xF800u) == 0x1800u) {  // ADD/SUB, registrador ou imediato de 3 bits
    familia_ = (((instr >> 9) & 3) == 1 || ((instr >> 9) & 3) == 3) ? "sub" : "add";
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
    familia_ = (const char*[]){"mov", "cmp", "add", "sub"}[(instr >> 11) & 3];
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
    familia_ = "ldr";
    const uint32_t rd = (instr >> 8) & 7;
    const Reg end = ((pc + 4) & ~3u) + ((instr & 0xFF) << 2);
    Set(static_cast<int>(rd), mem_.Ler32(end));
    Set(kPC, pc + 2);
    return;
  }
  if ((instr & 0xE000u) == 0x6000u || (instr & 0xE000u) == 0x7000u ||
      (instr & 0xE000u) == 0x8000u) {  // LDR/STR, LDRB/STRB, LDRH/STRH
    // O `L` DESTAS FORMAS E O BIT 11, em todas elas: 0x6000/0x6800 (palavra),
    // 0x7000/0x7800 (byte), 0x8000/0x8800 (meia-palavra) e 0x9000/0x9800
    // (palavra). Ler os bits 12-11 (`(instr >> 11) & 3`) da 0 no 0x6000 mas da
    // **2 no 0x7000 e no 0x9000** -- e como o teste de guarda era `op == 0`, o
    // `strb` (0x7000) e o `str` de palavra (0x9000) eram executados como
    // LEITURA, em silencio. Medido com o auditor: 2 048 + 2 048 metades de
    // palavra do espaco Thumb. O bit 12 nao faz parte do `L`: ele e que separa
    // a meia-palavra (0x8000) da palavra (0x9000).
    const bool carrega = ((instr >> 11) & 1u) != 0;
    const uint32_t imm5 = (instr >> 6) & 0x1F;
    const uint32_t rn = (instr >> 3) & 7;
    const uint32_t rd = instr & 7;
    const Reg base = Get(static_cast<int>(rn));
    // A GUARDA DA MEIA-PALAVRA ESTAVA MORTA, e e um defeito medido: a condicao
    // era `(instr & 0xF000) == 0x8000` E `(instr & 0x1000) != 0` -- as duas
    // juntas nunca sao verdadeiras, porque `(instr & 0xF000) == 0x8000` diz que
    // o bit 12 e ZERO. Resultado: `1000 imm5 Rn Rd` (STRH, 0x8000) e
    // `1000 1 imm5 Rn Rd` (LDRH, 0x8800) corriam como STR/LDR de 32 bits, EM
    // SILENCIO. Os valores do objdump (`-M force-thumb`): 0x8000 = `strh`,
    // 0x8800 = `ldrh`, 0x9000 = `str` (palavra), 0x9800 = `ldr` -- o bit 11 e o
    // `L` do LDRH e o bit 12 e o que separa a palavra (0x9) da meia-palavra
    // (0x8), logo o teste certo e so `(instr & 0xF000) == 0x8000`.
    const bool meia = (instr & 0xF000u) == 0x8000u;
    // O NOME da forma, para a sonda: a mesma familia de instrucoes com tres
    // larguras diferentes nao pode ter um nome so, senao a comparacao com o
    // objdump acusa divergencia onde nao ha nenhuma.
    familia_ = meia ? (carrega ? "ldrh" : "strh")
                    : ((instr & 0xF000u) == 0x7000u ? (carrega ? "ldrb" : "strb")
                                                    : (carrega ? "ldr" : "str"));
    const bool byte = (instr & 0xF000u) == 0x7000u;
    if (meia) {
      const Reg end = base + (imm5 << 1);
      if (carrega) Set(static_cast<int>(rd), mem_.Ler16(end));
      else mem_.Escrever16(end, static_cast<std::uint16_t>(Get(static_cast<int>(rd)) & 0xFFFF));
      Set(kPC, pc + 2);
      return;
    }
    const Reg end = base + (byte ? imm5 : imm5 << 2);
    if (carrega) {
      if (byte) Set(static_cast<int>(rd), mem_.Ler8(end));
      else Set(static_cast<int>(rd), mem_.Ler32(end));
    } else {
      if (byte) mem_.Escrever8(end, static_cast<std::uint8_t>(Get(static_cast<int>(rd)) & 0xFF));
      else mem_.Escrever32(end, Get(static_cast<int>(rd)));
    }
    Set(kPC, pc + 2);
    return;
  }
  // FORMATO 5 DO THUMB (0x5000-0x5FFF): acesso a memoria com offset de
  // REGISTRADOR nas oito variantes. FALTAVA INTEIRO -- era recusado ("forma
  // Thumb NAO implementada"), e as sete formas que o agente anterior mediu em
  // falta (LDRH/STRH/LDRSB/LDRSH e as suas companheiras) vivem aqui e no formato
  // 8/9 acima. Os nomes e a ordem dos campos sao do objdump
  // (`-M force-thumb`): `0101 op Rm Rn Rd` com `op` nos bits 11-9 --
  // 0 STR, 1 STRH, 2 STRB, 3 LDRSB, 4 LDR, 5 LDRH, 6 LDRB, 7 LDRSH.
  if ((instr & 0xF000u) == 0x5000u) {
    const uint32_t op = (instr >> 9) & 7u;
    const uint32_t rm = (instr >> 6) & 7u;
    const uint32_t rn = (instr >> 3) & 7u;
    const uint32_t rd = instr & 7u;
    static const char* const kNomes[8] = {"str",   "strh", "strb", "ldrsb",
                                          "ldr",   "ldrh", "ldrb", "ldrsh"};
    familia_ = kNomes[op];
    const Reg end = Get(static_cast<int>(rn)) + Get(static_cast<int>(rm));
    switch (op) {
      case 0: mem_.Escrever32(end, Get(static_cast<int>(rd))); break;
      case 1: mem_.Escrever16(end, static_cast<std::uint16_t>(Get(static_cast<int>(rd)) & 0xFFFFu)); break;
      case 2: mem_.Escrever8(end, static_cast<std::uint8_t>(Get(static_cast<int>(rd)) & 0xFFu)); break;
      case 3: Set(static_cast<int>(rd), static_cast<Reg>(static_cast<std::int32_t>(static_cast<std::int8_t>(mem_.Ler8(end))))); break;
      case 4: Set(static_cast<int>(rd), mem_.Ler32(end)); break;
      case 5: Set(static_cast<int>(rd), mem_.Ler16(end)); break;  // zero-extendido
      case 6: Set(static_cast<int>(rd), mem_.Ler8(end)); break;
      default: Set(static_cast<int>(rd), static_cast<Reg>(static_cast<std::int32_t>(static_cast<std::int16_t>(mem_.Ler16(end))))); break;
    }
    Set(kPC, pc + 2);
    return;
  }
  if ((instr & 0xF800u) == 0xE000u) {  // B incondicional
    familia_ = "b";
    int32_t d = instr & 0x7FF;
    if ((d & 0x400) != 0) d |= static_cast<int32_t>(0xFFFFF800u);
    Set(kPC, pc + 4 + static_cast<Reg>(d << 1));
    return;
  }
  if ((instr & 0xFF00u) == 0xDF00u) { familia_ = "swi"; SWI(instr, pc); Set(kPC, pc + 2); return; }
  if ((instr & 0xFF87u) == 0x4700u) {  // BX/BLX registrador
    familia_ = ((instr & 0x0080u) != 0) ? "blx" : "bx";
    const Reg alvo = Get(static_cast<int>((instr >> 3) & 0xF));
    if ((alvo & 1) == 0) modo_atual_ &= ~Cpsr::kT;
    Set(kPC, alvo & ~1u);
    return;
  }
  if ((instr & 0xF000u) == 0xD000u) {  // B condicional
    familia_ = "b";
    if (CondicaoVerdadeira((instr >> 8) & 0xF)) {
      int32_t d = instr & 0xFF;
      if ((d & 0x80) != 0) d |= static_cast<int32_t>(0xFFFFFF00u);
      Set(kPC, pc + 4 + static_cast<Reg>(d << 1));
    } else {
      Set(kPC, pc + 2);
    }
    return;
  }
  familia_ = NomeDoFormatoThumb(instr);
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
