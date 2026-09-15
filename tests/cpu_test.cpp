#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/cpu/arm_interpreter.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"

using zb2::ArmInterpreter;
using zb2::Area;
using zb2::Cpsr;
using zb2::DestinoMemoria;
using zb2::Memoria;
using zb2::Modo;
using zb2::ModoValido;
using zb2::Nivel;
using zb2::Traco;

namespace {

// ---------------------------------------------------------------------------
// Montador minimo para os testes.
//
// MOTIVO: a primeira versao destes testes escrevia as instrucoes como literais
// hexadecimais, e duas delas estavam erradas -- o `MLA` tinha os campos Rn/Rs
// trocados e o teste acusava o EMULADOR por um erro do TESTE. Construir a
// instrucao a partir dos campos nomeados tira essa classe de erro inteira: o
// que fica escrito e a intencao da instrucao, nao o resultado de a codificar a
// mao as tres da manha.
//
// Os formatos seguem o ARM ARM: `cond` nos bits 31-28, `opcode` em 24-21,
// registradores nos seus nibbles.
constexpr std::uint32_t kAl = 0xEu;

constexpr std::uint32_t DpImediato(std::uint32_t opcode, std::uint32_t rd, std::uint32_t rn,
                                   std::uint32_t imm, bool poe_bandeiras = false) {
  return (kAl << 28) | (1u << 25) | ((opcode & 0xF) << 21) | (poe_bandeiras ? (1u << 20) : 0u) |
         ((rn & 0xF) << 16) | ((rd & 0xF) << 12) | (imm & 0xFF);
}
constexpr std::uint32_t DpRegistrador(std::uint32_t opcode, std::uint32_t rd, std::uint32_t rn,
                                      std::uint32_t rm, bool poe_bandeiras = false) {
  return (kAl << 28) | ((opcode & 0xF) << 21) | (poe_bandeiras ? (1u << 20) : 0u) |
         ((rn & 0xF) << 16) | ((rd & 0xF) << 12) | (rm & 0xF);
}
constexpr std::uint32_t MovImediato(std::uint32_t rd, std::uint32_t imm, bool poe_bandeiras = false) {
  return DpImediato(0xD, rd, 0, imm, poe_bandeiras);
}
constexpr std::uint32_t CmpImediato(std::uint32_t rn, std::uint32_t imm) {
  return DpImediato(0xA, 0, rn, imm, true);
}
constexpr std::uint32_t CmpRegistrador(std::uint32_t rn, std::uint32_t rm) {
  return DpRegistrador(0xA, 0, rn, rm, true);
}
constexpr std::uint32_t SomaImediata(std::uint32_t rd, std::uint32_t rn, std::uint32_t imm,
                                     bool poe_bandeiras = false) {
  return DpImediato(0x4, rd, rn, imm, poe_bandeiras);
}
constexpr std::uint32_t SubImediata(std::uint32_t rd, std::uint32_t rn, std::uint32_t imm,
                                    bool poe_bandeiras = false) {
  return DpImediato(0x2, rd, rn, imm, poe_bandeiras);
}
constexpr std::uint32_t SomaRegistrador(std::uint32_t rd, std::uint32_t rn, std::uint32_t rm,
                                        bool poe_bandeiras = false) {
  return DpRegistrador(0x4, rd, rn, rm, poe_bandeiras);
}
constexpr std::uint32_t SubRegistrador(std::uint32_t rd, std::uint32_t rn, std::uint32_t rm,
                                       bool poe_bandeiras = false) {
  return DpRegistrador(0x2, rd, rn, rm, poe_bandeiras);
}
constexpr std::uint32_t EAnd(std::uint32_t rd, std::uint32_t rn, std::uint32_t rm) {
  return DpRegistrador(0x0, rd, rn, rm);
}
constexpr std::uint32_t EOrr(std::uint32_t rd, std::uint32_t rn, std::uint32_t rm) {
  return DpRegistrador(0xC, rd, rn, rm);
}
constexpr std::uint32_t EOrrLsl(std::uint32_t rd, std::uint32_t rn, std::uint32_t rm,
                                std::uint32_t deslocamento) {
  return EOrr(rd, rn, rm) | ((deslocamento & 0x1F) << 7);
}
constexpr std::uint32_t EEor(std::uint32_t rd, std::uint32_t rn, std::uint32_t rm) {
  return DpRegistrador(0x1, rd, rn, rm);
}
constexpr std::uint32_t MoveRegistrador(std::uint32_t rd, std::uint32_t rm, std::uint32_t tipo,
                                        std::uint32_t quantidade) {
  // tipo: 0=LSL 1=LSR 2=ASR 3=ROR
  return DpRegistrador(0xD, rd, 0, rm) | ((tipo & 3) << 5) | ((quantidade & 0x1F) << 7);
}
constexpr std::uint32_t Mul(std::uint32_t rd, std::uint32_t rm, std::uint32_t rs,
                            bool poe_bandeiras = false) {
  return (kAl << 28) | ((poe_bandeiras ? 1u : 0u) << 20) | ((rd & 0xF) << 16) | ((rs & 0xF) << 8) |
         0x90u | (rm & 0xF);
}
constexpr std::uint32_t Mla(std::uint32_t rd, std::uint32_t rm, std::uint32_t rs,
                            std::uint32_t rn, bool poe_bandeiras = false) {
  // ARM ARM A4.1.26: `MLA Rd, Rm, Rs, Rn` calcula Rd = (Rm * Rs) + Rn.
  //   Rd nos bits 19-16, **Rn (a PARCELA SOMADA) nos 15-12**, Rs nos 11-8 e
  //   Rm nos 3-0.
  //
  // A ORDEM DOS ARGUMENTOS DESTE CONSTRUTOR E A DO ARM ARM, e nao e um detalhe:
  // a versao anterior recebia `(rd, rn, rm, rs)` e dizia que o resultado era
  // `Rn * Rm + Rs`. Isso e falso, e o interpretador tinha a MESMA leitura
  // errada -- os dois concordavam um com o outro e discordavam do processador.
  // MEDIDO no binutils (`objdump -D -b binary -m arm`):
  //   0xE0203291 = `mla r0, r1, r2, r3`
  //   0xE0201392 = `mla r0, r2, r3, r1`
  return (kAl << 28) | (1u << 21) | ((poe_bandeiras ? 1u : 0u) << 20) | ((rd & 0xF) << 16) |
         ((rn & 0xF) << 12) | ((rs & 0xF) << 8) | 0x90u | (rm & 0xF);
}
constexpr std::uint32_t Umull(std::uint32_t rd_lo, std::uint32_t rd_hi, std::uint32_t rm,
                              std::uint32_t rs) {
  // ARM ARM: `UMULL RdLo, RdHi, Rm, Rs`.
  return (kAl << 28) | 0x00800090u | ((rd_hi & 0xF) << 16) | ((rd_lo & 0xF) << 12) |
         ((rs & 0xF) << 8) | (rm & 0xF);
}
constexpr std::uint32_t Mrc(std::uint32_t rd, std::uint32_t crn, std::uint32_t crm,
                            std::uint32_t opcode2) {
  return (kAl << 28) | 0x0E100000u | (1u << 20) | ((rd & 0xF) << 12) | (15u << 8) |
         ((crn & 0xF) << 16) | (crm & 0xF) | ((opcode2 & 7) << 5);
}
constexpr std::uint32_t MrsCpsr(std::uint32_t rd) { return (kAl << 28) | 0x010F0000u | ((rd & 0xF) << 12); }
constexpr std::uint32_t TstRegistradorImediato(std::uint32_t rn, std::uint32_t imm) {
  return DpImediato(0x8, 0, rn, imm, true);
}
constexpr std::uint32_t BxRegistrador(std::uint32_t rm) {
  return (kAl << 28) | 0x012FFF10u | (rm & 0xF);
}
constexpr std::uint32_t BL(std::int32_t deslocamento_palavras) {
  return (kAl << 28) | 0x0B000000u | (static_cast<std::uint32_t>(deslocamento_palavras) & 0x00FFFFFFu);
}
constexpr std::uint32_t BIncondicional(std::int32_t deslocamento_palavras) {
  return (kAl << 28) | 0x0A000000u | (static_cast<std::uint32_t>(deslocamento_palavras) & 0x00FFFFFFu);
}
constexpr std::uint32_t LdrImediato(std::uint32_t rd, std::uint32_t rn, std::uint32_t deslocamento) {
  return (kAl << 28) | 0x05900000u | ((rn & 0xF) << 16) | ((rd & 0xF) << 12) | (deslocamento & 0xFFF);
}
constexpr std::uint32_t StrImediato(std::uint32_t rd, std::uint32_t rn, std::uint32_t deslocamento) {
  return (kAl << 28) | 0x05800000u | ((rn & 0xF) << 16) | ((rd & 0xF) << 12) | (deslocamento & 0xFFF);
}
constexpr std::uint32_t StrbImediato(std::uint32_t rd, std::uint32_t rn, std::uint32_t deslocamento) {
  return (kAl << 28) | 0x05C00000u | ((rn & 0xF) << 16) | ((rd & 0xF) << 12) | (deslocamento & 0xFFF);
}
constexpr std::uint32_t LdmIaComWriteback(std::uint32_t rn, std::uint32_t lista) {
  return (kAl << 28) | 0x08B00000u | ((rn & 0xF) << 16) | (lista & 0xFFFF);
}
constexpr std::uint32_t StmFdComWriteback(std::uint32_t rn, std::uint32_t lista) {
  return (kAl << 28) | 0x092D0000u | ((rn & 0xF) << 16) | (lista & 0xFFFF);
}
constexpr std::uint32_t LdmFdComWriteback(std::uint32_t rn, std::uint32_t lista) {
  return (kAl << 28) | 0x08BD0000u | ((rn & 0xF) << 16) | (lista & 0xFFFF);
}
constexpr std::uint32_t Swi(std::uint32_t imediato) {
  return (kAl << 28) | 0x0F000000u | (imediato & 0x00FFFFFFu);
}


// --- grupo "extra load/store" -----------------------------------------------
//
// As palavras abaixo foram conferidas com o `arm-none-eabi-objdump` (binutils),
// e nao escritas de memoria. O teste `ExtraPalavraMedidaDoA3d` fixa a palavra
// REAL lida do corpus contra estes construtores -- se um campo mudar de sitio,
// o teste acusa o construtor e nao o emulador.
constexpr std::uint32_t ExtraL(  std::uint32_t campo, std::uint32_t rt, std::uint32_t rn,
                                 std::uint32_t deslocamento, bool carrega, bool escreve_na_base = false,
                                 bool subtrai = false, bool pre_indexado = true) {
  return (kAl << 28) | (pre_indexado ? (1u << 24) : 0u) | (subtrai ? 0u : (1u << 23)) |
         (1u << 22) | (escreve_na_base ? (1u << 21) : 0u) | (carrega ? (1u << 20) : 0u) |
         ((rn & 0xF) << 16) | ((rt & 0xF) << 12) | (((deslocamento >> 4) & 0xF) << 8) |
         ((campo & 0xF) << 4) | (deslocamento & 0xF);
}
constexpr std::uint32_t LdrdImediato(std::uint32_t rt, std::uint32_t rn, std::uint32_t deslocamento,
                                     bool escreve_na_base = false) {
  return ExtraL(0xD, rt, rn, deslocamento, false, escreve_na_base);
}
constexpr std::uint32_t StrdImediato(std::uint32_t rt, std::uint32_t rn, std::uint32_t deslocamento,
                                     bool escreve_na_base = false) {
  return ExtraL(0xF, rt, rn, deslocamento, false, escreve_na_base);
}
constexpr std::uint32_t LdrhImediato(std::uint32_t rt, std::uint32_t rn, std::uint32_t deslocamento,
                                     bool escreve_na_base = false) {
  return ExtraL(0xB, rt, rn, deslocamento, true, escreve_na_base);
}
constexpr std::uint32_t StrhImediato(std::uint32_t rt, std::uint32_t rn, std::uint32_t deslocamento,
                                     bool escreve_na_base = false) {
  return ExtraL(0xB, rt, rn, deslocamento, false, escreve_na_base);
}
constexpr std::uint32_t LdrsbImediato(std::uint32_t rt, std::uint32_t rn, std::uint32_t deslocamento) {
  return ExtraL(0xD, rt, rn, deslocamento, true);
}
constexpr std::uint32_t LdrshImediato(std::uint32_t rt, std::uint32_t rn, std::uint32_t deslocamento) {
  return ExtraL(0xF, rt, rn, deslocamento, true);
}
// A forma NAO PRIVILEGIADA (`LDRHT`/`STRHT`): P=0 e W=1. Nao esta implementada,
// e o teste desta guarda exige que ela RECUSE -- nao que ela faca "alguma coisa".
constexpr std::uint32_t StrhNaoPrivilegiado(std::uint32_t rt, std::uint32_t rn,
                                            std::uint32_t deslocamento) {
  return (kAl << 28) | (0u << 24) | (1u << 23) | (1u << 22) | (1u << 21) | (0u << 20) |
         ((rn & 0xF) << 16) | ((rt & 0xF) << 12) | (((deslocamento >> 4) & 0xF) << 8) | 0xB0u |
         (deslocamento & 0xF);
}

// Escreve instrucoes ARM a partir de um endereco e corre.
class Bancada {
 public:
  explicit Bancada(std::uint32_t base = 0x00100000, std::uint32_t pilha = 0x80080000)
      : cpu_(mem_, &traco_) {
    base_ = base;
    mem_.EscritorUnico("cpu");
    // `Repor` ja deixa o CPSR num modo VALIDO com IRQ e FIQ desactivadas. Nao
    // se chama `SetCpsr` aqui: faze-lo depois de `Repor` limpava as mascaras de
    // interrupcao, e um teste que as verificava falhava por causa do fixture e
    // nao do emulador.
    cpu_.Repor(base, pilha);
  }
  void Instrucao(std::uint32_t v) {
    mem_.Escrever32(base_ + deslocamento_, v);
    deslocamento_ += 4;
  }
  void Thumb(std::uint16_t v) {
    mem_.Escrever16(base_ + deslocamento_, v);
    deslocamento_ += 2;
  }
  void Terminar() { Instrucao(0xEAFFFFFEu); }  // B . -- um laco que nao avanca
  std::uint64_t Correr(std::uint64_t n) { return cpu_.Correr(n); }
  std::uint32_t R(int i) const { return cpu_.Get(i); }
  void R(int i, std::uint32_t v) { cpu_.Set(i, v); }
  std::uint32_t Bandeiras() const { return cpu_.Cpsr() & 0xF0000000u; }
  ArmInterpreter& Cpu() { return cpu_; }
  Memoria& Mem() { return mem_; }
  Traco& Tr() { return traco_; }

 private:
  Memoria mem_;
  Traco traco_{"teste", nullptr};
  ArmInterpreter cpu_;
  std::uint32_t base_ = 0;
  std::uint32_t deslocamento_ = 0;
};

bool N(const Bancada& b) { return (b.Bandeiras() & Cpsr::kN) != 0; }
bool Z(const Bancada& b) { return (b.Bandeiras() & Cpsr::kZ) != 0; }
bool C(const Bancada& b) { return (b.Bandeiras() & Cpsr::kC) != 0; }
bool V(const Bancada& b) { return (b.Bandeiras() & Cpsr::kV) != 0; }

}  // namespace

// ===========================================================================
// A ARMADILHA MEDIDA: o CPSR inicial tem de ser um modo VALIDO
// ===========================================================================

TEST(Cpu, ZeroNaoEUmModoValido) {
  // Esta e a armadilha que custou uma investigacao inteira no Zeebulator
  // antigo, que comecava com `cpsr = 0`. Os cinco bits baixos de um CPSR valido
  // nunca sao zero. O teste fixa os SETE valores validos e afirma que zero nao
  // e nenhum deles -- se alguem "simplificar" o construtor outra vez, isto fica
  // vermelho.
  EXPECT_FALSE(ModoValido(0x00000000u));
  EXPECT_FALSE(ModoValido(0xF0000000u));
  EXPECT_FALSE(ModoValido(0x00000001u));
  for (std::uint32_t m : {0x10u, 0x11u, 0x12u, 0x13u, 0x17u, 0x1Bu, 0x1Fu}) {
    EXPECT_TRUE(ModoValido(m)) << "modo 0x" << std::hex << m << " devia ser valido";
    EXPECT_TRUE(ModoValido(0xF0000000u | m));
  }
}

TEST(Cpu, AposReporOCpsrEOModoUsuarioEFLAGSDeZero) {
  Bancada b;
  EXPECT_EQ(b.Cpu().ModoAtual(), Modo::Usuario);
  EXPECT_TRUE(ModoValido(b.Cpu().Cpsr()));
  EXPECT_FALSE(N(b));
  EXPECT_FALSE(Z(b));
  EXPECT_FALSE(C(b));
  EXPECT_FALSE(V(b));
  // IRQ e FIQ desactivadas na arranque: um emulador que aceite interrupcoes
  // antes de o guest as preparar corre codigo que o jogo nao escreveu.
  EXPECT_NE(b.Cpu().Cpsr() & Cpsr::kI, 0u);
  EXPECT_NE(b.Cpu().Cpsr() & Cpsr::kF, 0u);
}

TEST(Cpu, EscreverUmModoInvalidoERecusado) {
  Bancada b;
  const std::uint64_t antes = b.Cpu().InstruscoesRecusadas();
  b.Cpu().SetCpsr(0x00000003u);  // modo 3 nao existe
  EXPECT_EQ(b.Cpu().InstruscoesRecusadas(), antes + 1);
}

// ===========================================================================
// Leitura do CPSR pelo proprio guest (o que o `chessbots` faz)
// ===========================================================================

TEST(Cpu, MrsDevolveOCpsrEOTesteDeModoFunciona) {
  // O `chessbots.mod` faz, em 0x0019a870:
  //     mrs r0, cpsr
  //     tst r0, #0xf
  //     bxeq lr
  // Ou seja: se os bits de modo forem zero, ele sai por outro caminho. Aqui o
  // teste corre exactamente essa sequencia e afirma o que TEM de acontecer.
  Bancada b;
  b.Instrucao(MrsCpsr(0));
  b.Instrucao(TstRegistradorImediato(0, 0x0F));
  b.Terminar();
  b.Correr(2);
  EXPECT_EQ(b.R(0) & 0x1Fu, 0x10u) << "o modo tem de ser Usuario (0x10), nao zero";
  // Este teste tinha uma expectativa contraditoria escrita ao lado do proprio
  // comentario a dizer o contrario. Ficou a que corresponde a realidade do
  // hardware: 0x10 & 0x0f e zero, logo Z fica 1.
  EXPECT_TRUE(Z(b)) << "0x10 & 0x0f e zero, logo Z=1";
}

TEST(Cpu, TstComMascaraQueNaoTocaOsBitsDeModo) {
  Bancada b;
  b.Instrucao(MrsCpsr(0));
  b.Instrucao(TstRegistradorImediato(0, 0x0F));
  b.Terminar();
  b.Correr(2);
  // 0x10 & 0x0f == 0  ->  Z = 1.
  EXPECT_TRUE(Z(b)) << "(0x10 & 0x0f) e zero, logo Z tem de ficar 1";
}

// ===========================================================================
// Dados processados -- valores esperados calculados a mao
// ===========================================================================

TEST(Cpu, MovESomaDeImediatos) {
  // MOV r1, #7 ; ADD r2, r1, #0x23  ->  r2 = 0x2a
  Bancada b;
  b.Instrucao(MovImediato(1, 7));
  b.Instrucao(SomaImediata(2, 1, 0x23));
  b.Terminar();
  b.Correr(2);
  EXPECT_EQ(b.R(1), 7u);
  EXPECT_EQ(b.R(2), 0x2Au);
}

TEST(Cpu, SubtracaoPoeOCarryQuandoNaoHaBorrow) {
  // CMP r0, r1 com r0=5, r1=3 -> sem borrow, C=1, Z=0, N=0.
  Bancada b;
  b.R(0, 5);
  b.R(1, 3);
  b.Instrucao(CmpRegistrador(0, 1));
  b.Terminar();
  b.Correr(1);
  EXPECT_TRUE(C(b)) << "5-3 nao gera borrow, logo C=1";
  EXPECT_FALSE(Z(b));
  EXPECT_FALSE(N(b));
}

TEST(Cpu, SubtracaoPoeCarryAZeroQuandoHaBorrow) {
  // 3 - 5 gera borrow: C=0, N=1, V=0.
  Bancada b;
  b.R(0, 3);
  b.R(1, 5);
  b.Instrucao(CmpRegistrador(0, 1));
  b.Terminar();
  b.Correr(1);
  EXPECT_FALSE(C(b)) << "no ARM, C=0 significa que houve borrow";
  EXPECT_TRUE(N(b));
  EXPECT_FALSE(V(b));
}

TEST(Cpu, OverflowDeSomaSinalizada) {
  // 0x7fffffff + 1 = 0x80000000: V=1 (dois positivos dao negativo), N=1, C=0.
  Bancada b;
  b.R(0, 0x7FFFFFFFu);
  b.R(1, 1);
  b.Instrucao(SomaRegistrador(0, 0, 1, true));
  b.Terminar();
  b.Correr(1);
  EXPECT_TRUE(V(b)) << "positivo + positivo = negativo e overflow";
  EXPECT_TRUE(N(b));
  EXPECT_FALSE(C(b));
  EXPECT_EQ(b.R(0), 0x80000000u);
}

TEST(Cpu, SubtracaoDeNegativoGeraOverflowSemBorrow) {
  // 0x80000000 - 1 = 0x7fffffff.
  //
  // Como SINAL: negativo menos positivo da positivo -> V=1, N=0.
  // Como SEM SINAL: 2147483648 - 1 nao gera borrow -> C=1.
  //
  // A primeira versao deste teste esperava C=0. Estava errada, e escrevi-a
  // a pensar em sinal quando o carry e uma propriedade da aritmetica sem
  // sinal. Corrigir o teste (e nao o emulador) foi a leitura certa: o par
  // N/V descreve o lado com sinal, o C descreve o lado sem sinal, e os dois
  // convivem no mesmo resultado.
  Bancada b;
  b.R(0, 0x80000000u);
  b.R(1, 1);
  b.Instrucao(SubRegistrador(0, 0, 1, true));
  b.Terminar();
  b.Correr(1);
  EXPECT_TRUE(V(b)) << "negativo menos positivo, em sinal, transborda";
  EXPECT_FALSE(N(b));
  EXPECT_TRUE(C(b)) << "em SEM SINAL 0x80000000 e maior que 1, logo nao houve borrow";
  EXPECT_EQ(b.R(0), 0x7FFFFFFFu);
}

TEST(Cpu, DeslocamentosEsquerdaEDireita) {
  // MOV r1, #1 ; MOV r2, r1, LSL #4  ->  0x10
  // MOV r3, r2, LSR #2              ->  0x04
  Bancada b;
  b.Instrucao(MovImediato(1, 1));
  b.Instrucao(MoveRegistrador(2, 1, 0, 4));   // LSL #4
  b.Instrucao(MoveRegistrador(3, 2, 1, 2));   // LSR #2
  b.Terminar();
  b.Correr(3);
  EXPECT_EQ(b.R(2), 0x10u);
  EXPECT_EQ(b.R(3), 0x04u);
}

TEST(Cpu, DeslocamentoDireitoAritmeticoPreservaOSinal) {
  // MOV r1, #0x80000000 via MSR nao da; usar MOV r1, #-1 e ASR #1 -> continua -1
  Bancada b;
  b.Instrucao(DpImediato(0xF, 1, 0, 0));      // MVN r1, #0 -> 0xFFFFFFFF
  b.Instrucao(MoveRegistrador(1, 1, 2, 1));   // ASR #1
  b.Terminar();
  b.Correr(2);
  EXPECT_EQ(b.R(1), 0xFFFFFFFFu) << "-1 >> 1 aritmeticamente continua -1";
}

TEST(Cpu, OrrEAndEOrr) {
  // MOV r1, #0xF0 ; MOV r2, #0x0F
  // AND r3, r1, r2  -> 0
  // ORR r4, r1, r2  -> 0xFF
  // EOR r5, r1, r2  -> 0xFF
  Bancada b;
  b.Instrucao(MovImediato(1, 0xF0));
  b.Instrucao(MovImediato(2, 0x0F));
  b.Instrucao(EAnd(3, 1, 2));
  b.Instrucao(EOrr(4, 1, 2));
  b.Instrucao(EEor(5, 1, 2));
  b.Terminar();
  b.Correr(5);
  EXPECT_EQ(b.R(3), 0u);
  EXPECT_EQ(b.R(4), 0xFFu);
  EXPECT_EQ(b.R(5), 0xFFu);
}

TEST(Cpu, CondicionalNaoExecutaQuandoABandeiraNaoBate) {
  // Z=0, entao um MOVEQ nao pode tocar no registrador.
  Bancada b;
  b.R(0, 5);
  b.R(1, 3);
  b.Instrucao(CmpRegistrador(0, 1));
  b.Instrucao(0x03A0207Bu);  // MOVEQ r2, #0x7b
  b.Terminar();
  b.Correr(2);
  EXPECT_EQ(b.R(2), 0u) << "Z=0, logo MOVEQ nao corre";
}

TEST(Cpu, CondicionalExecutaQuandoABandeiraBate) {
  Bancada b;
  b.R(0, 5);
  b.R(1, 5);
  b.Instrucao(CmpRegistrador(0, 1));
  b.Instrucao(0x03A0207Bu);  // MOVEQ r2, #0x7b
  b.Terminar();
  b.Correr(2);
  EXPECT_EQ(b.R(2), 0x7Bu) << "Z=1, logo MOVEQ corre";
}

// ===========================================================================
// Memoria
// ===========================================================================

TEST(Cpu, StoreDepoisLoadDevolveOMesmo) {
  // MOV r0, #0x4000 nao cabe no imediato; usa-se a soma de dois.
  Bancada b;
  b.R(0, 0x80090000u);
  b.R(1, 0xDEADBEEFu);
  b.Instrucao(StrImediato(1, 0, 0));
  b.Instrucao(LdrImediato(2, 0, 0));
  b.Terminar();
  b.Correr(2);
  EXPECT_EQ(b.R(2), 0xDEADBEEFu);
  EXPECT_EQ(b.Mem().Ler32(0x80090000), 0xDEADBEEFu);
}

TEST(Cpu, StoreDeByteNaoTocaNosVizinhos) {
  // ATENCAO a ordem, porque ja me enganou: escrever instrucoes com `Instrucao`
  // NAO as corre. Preparar estado entre duas chamadas a `Instrucao` nao tem
  // efeito nenhum -- as duas so correm depois, com o estado final. A primeira
  // versao deste teste punha `R(1, 0xAA)` entre o STR e o STRB e esperava que
  // o STR tivesse visto o valor antigo.
  Bancada b;
  b.R(0, 0x80091000u);
  b.R(1, 0x12345678u);
  b.Instrucao(StrImediato(1, 0, 0));    // STR  r1, [r0]
  b.Instrucao(MovImediato(1, 0xAA));    // r1 = 0xAA, como instrucao
  b.Instrucao(StrbImediato(1, 0, 1));   // STRB r1, [r0, #1]
  b.Terminar();
  b.Correr(3);
  EXPECT_EQ(b.Mem().Ler32(0x80091000), 0x1234AA78u)
      << "so o byte 1 muda; os outros tres continuam do STR";
}

TEST(Cpu, LdmESubDevolvemOsRegistradores) {
  // LDMIA r0!, {r1, r2} com r0 a apontar para uma tabela conhecida.
  Bancada b;
  b.Mem().Escrever32(0x80092000, 0x11111111u);
  b.Mem().Escrever32(0x80092004, 0x22222222u);
  b.R(0, 0x80092000u);
  b.Instrucao(LdmIaComWriteback(0, 0x0006));
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.R(1), 0x11111111u);
  EXPECT_EQ(b.R(2), 0x22222222u);
  EXPECT_EQ(b.R(0), 0x80092008u) << "o writeback avanca 8 bytes";
}

TEST(Cpu, StmComPcNaListaAvancaOPc) {
  // O prologo GCC `push {fp,ip,lr,pc}` (0xE92DD800): STM com PC na lista TEM
  // de avancar para a proxima instrucao, senao reexecuta para sempre (cluster
  // 0x002b: 13 titulos estouravam a carga em 4M passos).
  const std::uint32_t pilha = 0x800FF000u;
  Bancada b(0x00100000u, pilha);
  b.Instrucao(StmFdComWriteback(13, 0xD800));
  b.Instrucao(0xE1A00000u);  // nop de destino
  b.Terminar();
  b.Correr(2);
  EXPECT_EQ(b.R(15), 0x00100008u) << "STM com PC reexecutava a si mesmo";
}

TEST(Cpu, PushEPopUsamAPilha) {
  const std::uint32_t pilha = 0x800FF000u;
  Bancada b(0x00100000u, pilha);
  b.R(1, 0xCAFEBABEu);
  b.R(2, 0xFEEDFACEu);
  b.Instrucao(StmFdComWriteback(13, 0x0006));
  b.Instrucao(LdmFdComWriteback(13, 0x0006));
  b.Terminar();
  b.Correr(2);
  EXPECT_EQ(b.R(1), 0xCAFEBABEu);
  EXPECT_EQ(b.R(2), 0xFEEDFACEu);
  EXPECT_EQ(b.R(13), pilha) << "push seguido de pop devolve a pilha ao inicio";
}

// ===========================================================================
// Bifurcacoes
// ===========================================================================

TEST(Cpu, BlPoeOLrEVoltaComBx) {
  Bancada b;
  // O alvo de um BL e `pc + 8 + 4 * deslocamento`. Com deslocamento 0, o alvo
  // e a instrucao seguinte a seguir ao NOP (pc+8); o LR fica a apontar para o
  // NOP (pc+4), que e para onde o BX lr volta.
  b.Instrucao(BL(0));
  b.Instrucao(MoveRegistrador(0, 0, 0, 0));  // NOP, e o ponto de retorno
  b.Instrucao(MovImediato(1, 42));           // <- alvo do BL
  b.Instrucao(BxRegistrador(14));            // volta para o NOP
  b.Terminar();
  b.Correr(4);
  EXPECT_EQ(b.R(1), 42u) << "o corpo da funcao correu";
}

TEST(Cpu, BxParaEnderecoImparEntraEmThumb) {
  // BX com o bit 0 ligado muda para Thumb. Este caminho importa porque o
  // modulo do Zeebo tem codigo ARM e Thumb no mesmo ficheiro.
  Bancada b;
  // O literal tem de CONTER o endereco alvo, com o bit 0 ligado. Pôr o valor em
  // `r0` na preparacao nao serve: o LDR escreve-o por cima. Foi o erro da
  // primeira versao deste teste.
  const std::uint32_t alvo = 0x00100000u + 12;
  b.Instrucao(LdrImediato(0, 15, 0));       // LDR r0, [pc, #0] -> o literal seguinte
  b.Instrucao(BxRegistrador(0));
  b.Instrucao(alvo | 1u);                   // literal: alvo em Thumb
  b.Mem().Escrever16(alvo & ~1u, 0x2105u);  // MOV r1, #5  (Thumb)
  b.Correr(2);
  EXPECT_NE(b.Cpu().Cpsr() & Cpsr::kT, 0u) << "BX com bit 0 ligado entra em Thumb";
  b.Correr(1);
  EXPECT_EQ(b.R(1), 5u);
}

// ===========================================================================
// Multiplicacao
// ===========================================================================

TEST(Cpu, MulEMla) {
  Bancada b;
  b.R(1, 7);
  b.R(2, 6);
  b.Instrucao(Mul(0, 1, 2));            // r0 = r1 * r2 = 42
  b.R(3, 100);
  b.Instrucao(Mla(0, 1, 2, 3));          // r0 = (r1 * r2) + r3 = 142
  b.Terminar();
  b.Correr(2);
  EXPECT_EQ(b.R(0), 142u);
}

TEST(Cpu, UmullDaOProdutoDe64Bits) {
  // 0x10000 * 0x10000 = 0x100000000 -> metade baixa 0, metade alta 1.
  Bancada b;
  // `UMULL RdLo, RdHi, Rm, Rs` -> RdHi:RdLo = Rm * Rs. Com Rm = Rs = 0x10000,
  // o produto e 0x100000000: metade alta 1, metade baixa 0.
  b.R(0, 0x10000u);
  b.R(1, 0x10000u);
  b.Instrucao(Umull(0, 1, 0, 0));  // RdLo=r0, RdHi=r1, Rm=r0, Rs=r0
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.R(0), 0u);
  EXPECT_EQ(b.R(1), 1u);
}

// ===========================================================================
// Recusa ruidosa -- principio P2
// ===========================================================================

TEST(Cpu, InstrucaoDesconhecidaERecusadaComNomeENaoIgnorada) {
  // Um interpretador que trata o que nao conhece como "nao fez nada" e a versao
  // em codigo do stub silencioso que descartou 86 377 chamadas de GL no projeto
  // antigo. Aqui a recusa fica contada e identificada.
  Bancada b;
  b.Instrucao(0xF2000000u);  // fora de qualquer forma reconhecida
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.Cpu().InstruscoesRecusadas(), 1u);
  EXPECT_EQ(b.Cpu().UltimaRecusada(), 0xF2000000u);
}

TEST(Cpu, CorrerRespeitaOLimiteEVolta) {
  // O limite existe para um laco sem fim no guest nunca prender o emulador em
  // silencio -- foi um defeito medido (22 segundos sem uma linha de log).
  Bancada b;
  b.Terminar();  // B .
  const std::uint64_t n = b.Correr(5000);
  EXPECT_EQ(n, 5000u) << "Correr tem de VOLTAR e dizer quantas correu";
}

TEST(Cpu, SwiSemTratadorERecusada) {
  Bancada b;
  b.Instrucao(Swi(0));
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.Cpu().InstruscoesRecusadas(), 1u);
  EXPECT_EQ(b.Cpu().UltimaRecusada(), 0xEF000000u);
}

TEST(Cpu, ARecusaEscreveNoTracoComOPcEAsBandeiras) {
  // "Sem linha no log" era lido como "nao aconteceu". Uma recusa tem de deixar
  // rasto consultavel, com o suficiente para investigar sem repetir a corrida.
  Bancada b;
  DestinoMemoria dm;
  b.Tr().JuntarDestino(&dm);
  b.Instrucao(0xF2000000u);
  b.Terminar();
  b.Correr(1);
  ASSERT_GE(dm.eventos.size(), 1u);
  const auto& e = dm.eventos[0];
  EXPECT_EQ(e.area, Area::Cpu);
  EXPECT_EQ(e.nivel, Nivel::Erro);
  EXPECT_EQ(e.nome, "INSTRUCAO_RECUSADA");
  EXPECT_NE(e.detalhe.find("0xf2000000"), std::string::npos) << "o detalhe nomeia a instrucao";
}

// ===========================================================================
// O grupo "extra load/store" -- a parede dos 21 titulos que saltavam para a PILHA
// ===========================================================================
//
// MEDIDO, com o espiao de escrita em `0x8020001c` (o campo `+12` do objecto do
// modulo) e o traco `[DEBUG-pilha1]`:
//
//   pc=0000563c alvo=8020001c 0x00000000 -> 0x8007ffcc  sp=8007ffcc
//
// `0x563C` e o `stmib r4, {r0, r6, r8, sb}` do `AEEStaticMod_New`, que escreve
// `pMe->pfnModCrInst` em `+12` com o valor que veio do `ldrd r8, sb, [sp,#0x20]`
// em `0x55C0`. Devia ler os argumentos 5 e 6 (que o `AEEMod_Load` empurra como
// zero) e em vez disso o interpretador executava um `BIC r8, sp, r0, LSR r2`.

TEST(Cpu, ExtraPalavraMedidaDoA3d) {
  // A palavra REAL, lida do FICHEIRO `a3d.mod` no offset 0x55C0, descodificada
  // pelo `arm-none-eabi-objdump` como `ldrd r8, sb, [sp, #0x20]`. Este teste
  // existe para que um erro NOS CONSTRUTORES de campos abaixo acuse os
  // construtores, e nao o emulador -- foi o que aconteceu com o `MLA` na etapa 1.
  // As palavras a direita foram IMPRESSAS pelo binutils, e nao escritas a mao.
  // A primeira versao deste teste tinha cinco delas erradas -- e foram os
  // construtores a acusar o TESTE, que e o que se quer que aconteca (o
  // contrario foi o erro do `MLA`, na etapa 1: o teste acusava o emulador).
  EXPECT_EQ(LdrdImediato(8, 13, 0x20), 0xE1CD82D0u);   // ldrd  r8, [sp, #32]
  EXPECT_EQ(StrdImediato(4, 0, 0x8), 0xE1C040F8u);     // strd  r4, [r0, #8]
  EXPECT_EQ(LdrhImediato(1, 4, 0x20), 0xE1D412B0u);    // ldrh  r1, [r4, #32]
  EXPECT_EQ(StrhImediato(1, 4, 0x20), 0xE1C412B0u);    // strh  r1, [r4, #32]
  EXPECT_EQ(LdrsbImediato(1, 4, 0x20), 0xE1D412D0u);   // ldrsb r1, [r4, #32]
  EXPECT_EQ(LdrshImediato(1, 4, 0x20), 0xE1D412F0u);   // ldrsh r1, [r4, #32]
  EXPECT_EQ(LdrdImediato(8, 13, 0x20, true), 0xE1ED82D0u);  // ldrd r8, [sp, #32]!
}

TEST(Cpu, ExtraLdrdCarregaDoisRegistradoresDaPilha) {
  // A cena do `a3d`, reduzida ao essencial: a pilha tem 0x11111111 e 0x22222222
  // nos argumentos 5 e 6, e o `ldrd r8, sb, [sp, #0x20]` tem de os carregar.
  // O valor de partida de r8 e NAO ZERO de proposito: com zero, uma guarda que
  // nao escrevesse nada passava verde.
  Bancada b;
  // O SP e o da cena medida: 0x8007FFCC e onde o `AEEStaticMod_New` fica depois
  // do proprio `push`, logo `[sp, #0x20]` e 0x8007FFEC -- a area dos argumentos
  // 5 e 6 que o chamador preencheu com zero.
  b.R(13, 0x8007FFCCu);
  b.R(8, 0xDEADBEEFu);
  b.R(9, 0xDEADBEEFu);
  b.Mem().Escrever32(0x8007FFECu, 0x11111111u);
  b.Mem().Escrever32(0x8007FFF0u, 0x22222222u);
  b.Instrucao(LdrdImediato(8, 13, 0x20));
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.R(8), 0x11111111u) << "o primeiro registrador vem da pilha";
  EXPECT_EQ(b.R(9), 0x22222222u) << "o segundo registrador vem da pilha";
}

TEST(Cpu, ExtraLdrdNaoDevolveOEnderecoDaPilha) {
  // A REGRESSAO, como teste proprio. Sem `TransferenciaExtra` o `LDRD` cai no
  // `DadosProcessados`, que o le como um `BIC`: o resultado passa a ser o bit a
  // bit do primeiro operando, e com o deslocamento a zero isso e o PROPRIO SP.
  // Medido: 0x8007FFCC. E este valor que fazia 21 titulos saltarem para a pilha.
  Bancada b;
  b.R(13, 0x8007FFCCu);
  b.R(8, 0u);
  b.R(0, 0u);   // o `BIC` errado usava o r0 como quantidade de deslocamento
  b.R(2, 0u);
  b.Mem().Escrever32(0x8007FFECu, 0x11111111u);
  b.Mem().Escrever32(0x8007FFF0u, 0x22222222u);
  b.Instrucao(LdrdImediato(8, 13, 0x20));
  b.Terminar();
  b.Correr(1);
  EXPECT_NE(b.R(8), 0x8007FFCCu) << "o SP nao e o valor guardado na pilha";
  EXPECT_NE(b.R(8), 0x8007FFECu) << "nem o endereco de onde se leu";
  EXPECT_EQ(b.R(8), 0x11111111u);
}

TEST(Cpu, ExtraLdrdComEscritaNaBaseAndaComOPonteiro) {
  // `ldrd r8, sb, [sp, #0x20]!` -- pre-indexado com escrita na base.
  Bancada b;
  b.R(13, 0x8007FFCCu);
  b.Mem().Escrever32(0x8007FFECu, 0xAAAA0001u);
  b.Mem().Escrever32(0x8007FFF0u, 0xAAAA0002u);
  b.Instrucao(LdrdImediato(8, 13, 0x20, /*escreve_na_base=*/true));
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.R(8), 0xAAAA0001u);
  EXPECT_EQ(b.R(9), 0xAAAA0002u);
  EXPECT_EQ(b.R(13), 0x8007FFECu) << "o `!` anda com a base";
}

TEST(Cpu, ExtraStrdEscreveDoisRegistradoresSeguidos) {
  Bancada b;
  b.R(4, 0x5A5A5A5Au);
  b.R(5, 0xA5A5A5A5u);
  b.Instrucao(StrdImediato(4, 0, 0x8));
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.Mem().Ler32(0x8u), 0x5A5A5A5Au);
  EXPECT_EQ(b.Mem().Ler32(0xCu), 0xA5A5A5A5u);
}

TEST(Cpu, ExtraLdrhZeraOsBitsDeCima) {
  // `LDRH` e sem sinal: 0xFFFFFFFF no destino tem de ficar 0x00001234.
  Bancada b;
  b.R(0, 0x00020000u);
  b.R(1, 0xFFFFFFFFu);
  b.Mem().Escrever16(0x00020010u, 0x1234u);
  b.Instrucao(LdrhImediato(1, 0, 0x10));
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.R(1), 0x00001234u) << "a meia-palavra entra nos 16 bits de baixo";
}

TEST(Cpu, ExtraStrhEscreveSoDoisBytes) {
  // O STRH nao pode escrever os 32 bits: se escrevesse, apagava o vizinho.
  Bancada b;
  b.R(0, 0x00020000u);
  b.R(1, 0x0000ABCDu);
  b.Mem().Escrever32(0x00020010u, 0xFFFFFFFFu);
  b.Instrucao(StrhImediato(1, 0, 0x10));
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.Mem().Ler32(0x00020010u), 0xFFFFABCDu) << "so os 16 bits de baixo mudam";
}

TEST(Cpu, ExtraLdrhPosIndexadoAndaComABase) {
  // P=0 e W=0: `ldrh r1, [r0], #0x10`.
  Bancada b;
  b.R(0, 0x00020000u);
  b.Mem().Escrever16(0x00020000u, 0x1234u);
  // `ldrh r1, [r0], #16` (conferido com o binutils): le na base e so DEPOIS
  // anda com a base. Uma versao anterior deste teste punha o valor no endereco
  // de destino e acusava o emulador por um erro do teste.
  constexpr std::uint32_t kLdrhPosIndexado = ExtraL(0xB, 1, 0, 0x10, true, false, false, false);
  EXPECT_EQ(kLdrhPosIndexado, 0xE0D011B0u);
  b.Instrucao(kLdrhPosIndexado);
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.R(1), 0x1234u);
  EXPECT_EQ(b.R(0), 0x00020010u) << "o pos-indexado anda com a base DEPOIS de ler";
}

TEST(Cpu, ExtraLdrsbEstendeOSinalDoByte) {
  // 0x80 e -128 em 8 bits: o resultado tem de ser 0xFFFFFF80.
  Bancada b;
  b.R(0, 0x00020000u);
  b.Mem().Escrever8(0x00020010u, 0x80u);
  b.Instrucao(LdrsbImediato(1, 0, 0x10));
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.R(1), 0xFFFFFF80u);
}

TEST(Cpu, ExtraLdrshEstendeOSinalDaMeiaPalavra) {
  Bancada b;
  b.R(0, 0x00020000u);
  b.Mem().Escrever16(0x00020010u, 0x8001u);
  b.Instrucao(LdrshImediato(1, 0, 0x10));
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.R(1), 0xFFFF8001u);
}

TEST(Cpu, ExtraFormaNaoPrivilegiadaERecusadaEmVozAlta) {
  // P2: o caminho nao implementado RECUSA e REGISTA. Aceitar o `STRHT` e
  // executa-lo como se fosse um `STRH` seria o stub silencioso outra vez.
  Bancada b;
  const std::uint64_t antes = b.Cpu().InstruscoesRecusadas();
  b.Instrucao(StrhNaoPrivilegiado(1, 0, 0x10));
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.Cpu().InstruscoesRecusadas(), antes + 1);
  EXPECT_EQ(b.Cpu().UltimaRecusada(), StrhNaoPrivilegiado(1, 0, 0x10));
  EXPECT_EQ(b.Mem().Ler32(0x10u), 0u) << "uma recusa nao escreve memoria";
}

TEST(Cpu, ExtraLdrdComRtImparERecusado) {
  // ARM ARM A8.8.72: `LDRD` com Rt impar e UNPREDICTABLE. Escolher um par de
  // registradores por conta propria seria inventar comportamento.
  Bancada b;
  const std::uint64_t antes = b.Cpu().InstruscoesRecusadas();
  b.Instrucao(LdrdImediato(9, 0, 0x8));
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.Cpu().InstruscoesRecusadas(), antes + 1);
}

TEST(Cpu, ExtraContinuaADescodificarOMulNoMesmoEspaco) {
  // A guarda de ORDEM: o `MUL` tem os bits 27-25 = 000 e os bits 7-4 = 1001, o
  // MESMO espaco do grupo extra load/store. Se o ramo novo engolir o `MUL`, o
  // sintoma e um registrador a ficar com lixo -- e foi assim que esta familia de
  // erro apareceu seis vezes nesta arvore.
  Bancada b;
  b.R(1, 7);
  b.R(2, 6);
  b.Instrucao(Mul(0, 1, 2));
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.R(0), 42u);
  EXPECT_EQ(b.Cpu().InstruscoesRecusadas(), 0u);
}

TEST(Cpu, ExtraLdrdDesalinhadoERecusadoEmVozAlta) {
  // O ARM exige alinhamento de 4 no LDRD/STRD. A nossa memoria e esparsa e
  // atenderia o pedido desalinhado sem dizer nada -- e um titulo que dependesse
  // disso nao teria sintoma nenhum.
  Bancada b;
  b.R(0, 0x00020002u);
  const std::uint64_t antes = b.Cpu().InstruscoesRecusadas();
  b.Instrucao(LdrdImediato(2, 0, 0));
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.Cpu().InstruscoesRecusadas(), antes + 1);
}

TEST(Cpu, ExtraComRtIgualAoPcERecusado) {
  // `ldrh pc, [r0]` e UNPREDICTABLE. Deixar cair no `DadosProcessados` era o
  // que acontecia antes; agora recusa com o nome.
  Bancada b;
  const std::uint64_t antes = b.Cpu().InstruscoesRecusadas();
  b.Instrucao(LdrhImediato(15, 0, 0));
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.Cpu().InstruscoesRecusadas(), antes + 1);
  EXPECT_EQ(b.Cpu().UltimaRecusada(), LdrhImediato(15, 0, 0));
}

// ===========================================================================
// O GRUPO "MEDIA" DO ARMv6 (SXTB/UXTH/REV) E A ARITMETICA DSP DO ARMv5TE
// ===========================================================================
//
// PORQUE ESTES TESTES EXISTEM: o auditor diferencial
// (`tools/auditar_descodificador.py`) mediu, no corpus dos 62 titulos, 46 000
// palavras em que a NOSSA descodificacao executava OUTRA INSTRUCAO sem recusar:
// 17 295 `uxth` corriam como `ldrb`, 12 764 `uxtb` como `strb`, 6 432 `sxtab`
// como `str`, 2 183 `sxth` como `ldr`, 1 105 `smulbb` como `cmn`, 1 013 `smlabb`
// como `tst`, 259 `clz` como `cmn`.
//
// AS PALAVRAS DOS TESTES SAO AS DO CORPUS, lidas dos `.mod` e conferidas com o
// `arm-none-eabi-objdump` -- nao foram escritas de memoria. O endereco vem no
// comentario para quem quiser repetir a leitura.

// --- construtores das formas "media" ----------------------------------------
//
// `SEM_ACUMULACAO` tem os bits 19-16 = 1111 DENTRO da constante (e por isso nao
// leva `Rn`); as formas "A" tem os bits 19-16 livres e levam `Rn`. A primeira
// versao do construtor somava os dois e produzia `uxtah` onde se pedia `uxth`.
constexpr std::uint32_t ExtensaoSemAcumulacao(std::uint32_t base, std::uint32_t rd,
                                              std::uint32_t rm, std::uint32_t rodagem = 0) {
  return (kAl << 28) | base | ((rd & 0xF) << 12) | ((rodagem & 3) << 10) | (rm & 0xF);
}
constexpr std::uint32_t Sxtb(std::uint32_t rd, std::uint32_t rm) {
  return ExtensaoSemAcumulacao(0x06AF0070u, rd, rm);
}
constexpr std::uint32_t Sxth(std::uint32_t rd, std::uint32_t rm, std::uint32_t rodagem = 0) {
  return ExtensaoSemAcumulacao(0x06BF0070u, rd, rm, rodagem);
}
constexpr std::uint32_t Uxtb(std::uint32_t rd, std::uint32_t rm) {
  return ExtensaoSemAcumulacao(0x06EF0070u, rd, rm);
}
constexpr std::uint32_t Uxth(std::uint32_t rd, std::uint32_t rm) {
  return ExtensaoSemAcumulacao(0x06FF0070u, rd, rm);
}
constexpr std::uint32_t Sxtab(std::uint32_t rd, std::uint32_t rn, std::uint32_t rm,
                              std::uint32_t rodagem = 0) {
  return (kAl << 28) | 0x06A00070u | ((rn & 0xF) << 16) | ((rd & 0xF) << 12) |
         ((rodagem & 3) << 10) | (rm & 0xF);
}
constexpr std::uint32_t Sxtah(std::uint32_t rd, std::uint32_t rn, std::uint32_t rm,
                              std::uint32_t rodagem = 0) {
  return (kAl << 28) | 0x06B00070u | ((rn & 0xF) << 16) | ((rd & 0xF) << 12) |
         ((rodagem & 3) << 10) | (rm & 0xF);
}
constexpr std::uint32_t Uxtah(std::uint32_t rd, std::uint32_t rn, std::uint32_t rm) {
  return (kAl << 28) | 0x06F00070u | ((rn & 0xF) << 16) | ((rd & 0xF) << 12) | (rm & 0xF);
}
constexpr std::uint32_t Rev(std::uint32_t rd, std::uint32_t rm) {
  return (kAl << 28) | 0x06BF0F30u | ((rd & 0xF) << 12) | (rm & 0xF);
}
constexpr std::uint32_t Rev16(std::uint32_t rd, std::uint32_t rm) {
  return (kAl << 28) | 0x06BF0FB0u | ((rd & 0xF) << 12) | (rm & 0xF);
}
constexpr std::uint32_t Revsh(std::uint32_t rd, std::uint32_t rm) {
  return (kAl << 28) | 0x06FF0FB0u | ((rd & 0xF) << 12) | (rm & 0xF);
}
// `SEL` e `SXTB16`/`PKH`/`SSAT`/a aritmetica paralela ESTAO RECUSADAS (P2): o
// `SEL` depende das bandeiras GE, que este interpretador nao emula.
constexpr std::uint32_t SelRecusado(void) { return 0xE6800FB0u; }
constexpr std::uint32_t Sxtb16Recusado(void) { return 0xE68F0070u; }  // `sxtb16 r0, r0`

// --- construtores do grupo DSP ----------------------------------------------
constexpr std::uint32_t XyDe(const char* xy) {
  // O sufixo `<x><y>`: <x> escolhe a metade de Rm (bit 5) e <y> a de Rs (bit 6).
  // A ordem foi conferida no objdump: `smlabt` = 0x...C0 e `smlatb` = 0x...A0.
  return (xy[0] == 't' ? 0x20u : 0x0u) | (xy[1] == 't' ? 0x40u : 0x0u);
}
constexpr std::uint32_t SmulXy(const char* xy, std::uint32_t rd, std::uint32_t rs, std::uint32_t rm) {
  return (kAl << 28) | 0x01600080u | XyDe(xy) | ((rd & 0xF) << 16) | ((rs & 0xF) << 8) | (rm & 0xF);
}
constexpr std::uint32_t SmlaXy(const char* xy, std::uint32_t rd, std::uint32_t ra, std::uint32_t rs,
                               std::uint32_t rm) {
  return (kAl << 28) | 0x01000080u | XyDe(xy) | ((rd & 0xF) << 16) | ((ra & 0xF) << 12) |
         ((rs & 0xF) << 8) | (rm & 0xF);
}
constexpr std::uint32_t Clz(std::uint32_t rd, std::uint32_t rm) {
  return (kAl << 28) | 0x016F0F10u | ((rd & 0xF) << 12) | (rm & 0xF);
}
constexpr std::uint32_t Qdadd(std::uint32_t rd, std::uint32_t rn, std::uint32_t rm) {
  return (kAl << 28) | 0x01400050u | ((rn & 0xF) << 16) | ((rd & 0xF) << 12) | (rm & 0xF);
}
constexpr std::uint32_t Qsub(std::uint32_t rd, std::uint32_t rn, std::uint32_t rm) {
  return (kAl << 28) | 0x01200050u | ((rn & 0xF) << 16) | ((rd & 0xF) << 12) | (rm & 0xF);
}
// Condicao 1111: `pld` e o `blx <rotulo>`.
constexpr std::uint32_t PldImediato(std::uint32_t rn, std::uint32_t deslocamento) {
  return 0xF550F000u | ((rn & 0xF) << 16) | (deslocamento & 0xFFF);
}
constexpr std::uint32_t PldRegistrador(std::uint32_t rn, std::uint32_t rm) {
  return 0xF750F000u | ((rn & 0xF) << 16) | (rm & 0xF);
}
constexpr std::uint32_t BlxImediato(std::int32_t deslocamento_palavras) {
  return 0xFA000000u | (static_cast<std::uint32_t>(deslocamento_palavras) & 0x00FFFFFFu);
}
// Thumb: os formatos 5 (registrador) e 8/9 (imediato).
constexpr std::uint16_t ThumbF5(std::uint32_t op, std::uint32_t rm, std::uint32_t rn, std::uint32_t rd) {
  return static_cast<std::uint16_t>(0x5000u | ((op & 7u) << 9) | ((rm & 7u) << 6) | ((rn & 7u) << 3) | (rd & 7u));
}
constexpr std::uint16_t ThumbMemImediato(bool carrega, bool meia, bool byte, std::uint32_t imm5,
                                         std::uint32_t rn, std::uint32_t rd) {
  const std::uint32_t base = meia ? 0x8000u : (byte ? 0x7000u : 0x6000u);
  return static_cast<std::uint16_t>(base | (carrega ? 0x0800u : 0u) | ((imm5 & 0x1Fu) << 6) |
                                    ((rn & 7u) << 3) | (rd & 7u));
}

TEST(Cpu, MediaPalavrasMedidasDoCorpus) {
  // As palavras REAIS, cada uma com o ficheiro e o offset de onde foi lida.
  EXPECT_EQ(Uxth(3, 5), 0xE6FF3075u);        // a3d.mod      +0xad00  `uxth r3, r5`
  EXPECT_EQ(Uxtb(1, 1), 0xE6EF1071u);        // cninja.mod   +0x480   `uxtb r1, r1`
  EXPECT_EQ(Sxth(6, 1), 0xE6BF6071u);        // a3d.mod      +0x12d4  `sxth r6, r1`
  EXPECT_EQ(Sxtb(0, 0), 0xE6AF0070u);        // a3d.mod      +0x23bc  `sxtb r0, r0`
  EXPECT_EQ(Sxtab(3, 3, 0), 0xE6A33070u);    // cninja.mod   +0x46620 `sxtab r3, r3, r0`
  EXPECT_EQ(Sxtah(0, 0, 8, 2), 0xE6B00878u); // fifa09.mod   +0xc4fd8 `sxtah r0, r0, r8, ror #16`
  EXPECT_EQ(Uxtah(4, 0, 4), 0xE6F04074u);    // zeebotennis  +0x3c32c `uxtah r4, r0, r4`
  EXPECT_EQ(Rev16(0, 0), 0xE6BF0FB0u);       // a3d.mod      +0x11d80 `rev16 r0, r0`
  EXPECT_EQ(Clz(6, 2), 0xE16F6F12u);         // chessbots    +0xc30   `clz r6, r2`
  EXPECT_EQ(SmulXy("bb", 1, 14, 1), 0xE1610E81u);  // chessbots +0x13370 `smulbb r1, r1, lr`
  EXPECT_EQ(SmlaXy("bt", 4, 1, 3, 3), 0xE10413C3u);  // chessbots +0x4db8 `smlabt r4, r3, r3, r1`
}

TEST(Cpu, MediaUxthNaoEscreveMemoriaNemLeByte) {
  // O DEFEITO, como teste proprio: com os bits 27-25 = 011 e sem o ramo do grupo
  // media, o `uxth r3, r5` cai na transferencia simples e corre como `ldrb r3,
  // [r5]`. O r5 e um endereco VALIDO de proposito: o teste tem de distinguir
  // "extraiu a meia-palavra" de "leu um byte do endereco".
  Bancada b;
  b.R(5, 0x00110000u);
  b.R(3, 0xDEADBEEFu);
  b.Mem().Escrever8(0x00110000u, 0x7Fu);  // se corresse como ldrb, o r3 ficava 0x7F
  b.Instrucao(Uxth(3, 5));
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.R(3), 0x00000000u) << "a metade de baixo do r5 e zero";
}

TEST(Cpu, MediaUxthExtraiAMeiaPalavraSemSinal) {
  Bancada b;
  b.R(5, 0xFFFF1234u);
  b.R(3, 0xDEADBEEFu);
  b.Instrucao(Uxth(3, 5));
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.R(3), 0x00001234u);
}

TEST(Cpu, MediaSxthEstendeOSinalDaMeiaPalavra) {
  Bancada b;
  b.R(1, 0x12348000u);
  b.R(6, 0xDEADBEEFu);
  b.Instrucao(Sxth(6, 1));
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.R(6), 0xFFFF8000u) << "0x8000 com sinal e 0xFFFF8000";
}

TEST(Cpu, MediaSxtbEstendeOSinalDoByte) {
  Bancada b;
  b.R(0, 0x00000080u);
  b.Instrucao(Sxtb(0, 0));
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.R(0), 0xFFFFFF80u);
}

TEST(Cpu, MediaUxtbZeraOsBitsDeCima) {
  Bancada b;
  b.R(1, 0xFFFFFF80u);
  b.Instrucao(Uxtb(1, 1));
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.R(1), 0x00000080u);
}

TEST(Cpu, MediaRodagemDeBytesVemAntesDaExtensao) {
  // `sxtah r0, r0, r8, ror #16`: a rodagem de 16 bits ja poe a metade de baixo
  // em 0x8000, e a extensao de sinal tem de dar 0xFFFF8000. Uma versao que
  // estendesse ANTES de rodar daria outro valor -- e o valor errado parece
  // plausivel, que e o que torna esta classe de erro caro.
  Bancada b;
  b.R(0, 0x00000000u);
  // A rodagem em `sxtah ..., ror #16` RODA O r8 e extrai a metade de BAIXO do
  // valor rodado: 0x80000000 rodado 16 bits da 0x00008000, cuja metade de baixo
  // com sinal e 0xFFFF8000.
  b.R(8, 0x80000000u);
  b.Instrucao(Sxtah(0, 0, 8, 2));
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.R(0), 0xFFFF8000u);
}

TEST(Cpu, MediaSxtabSomaAoRegistradorDaBase) {
  Bancada b;
  b.R(3, 0x00000100u);
  b.R(0, 0x000000FFu);  // 0xFF com sinal = -1
  b.Instrucao(Sxtab(3, 3, 0));
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.R(3), 0x000000FFu) << "0x100 + (-1)";
}

TEST(Cpu, MediaRevInverteOsQuatroBytes) {
  Bancada b;
  b.R(0, 0x11223344u);
  b.Instrucao(Rev(1, 0));
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.R(1), 0x44332211u);
}

TEST(Cpu, MediaRev16InverteEmCadaMeiaPalavra) {
  Bancada b;
  b.R(2, 0x11223344u);
  b.Instrucao(Rev16(3, 2));
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.R(3), 0x22114433u);
}

TEST(Cpu, MediaRevshInverteEEstendeOSinal) {
  Bancada b;
  b.R(0, 0x000080FFu);
  b.Instrucao(Revsh(1, 0));
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.R(1), 0xFFFFFF80u) << "0x80FF trocado em 16 bits e 0xFF80";
}

TEST(Cpu, MediaNaoEngoleATransferenciaComDeslocamentoDeRegistrador) {
  // A FRONTEIRA: uma transferencia com offset de registrador tem bits 27-24 =
  // 0110 e o BIT 4 = 0. Um teste que so olhasse os bits 27-24 partia o
  // `str r0, [r0, -r2]` -- e este teste existe para o apanhar.
  Bancada b;
  b.R(0, 0x00110000u);
  b.R(2, 0x00000004u);
  // `str r0, [r0, -r2]` = 0xE6000002 (post-indexado: guarda na BASE e so depois
  // anda com ela). O que o teste prova e que GUARDA -- a alternativa era o ramo
  // novo o ler como uma extensao de sinal.
  b.Instrucao(0xE6000002u);
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.Mem().Ler32(0x00110000u), 0x00110000u) << "tem de GUARDAR, e nao extrair";
  EXPECT_EQ(b.R(0), 0x0010FFFCu) << "e a base anda no fim";
}

TEST(Cpu, MediaSxtb16ConhecidaERecusadaComNome) {
  // P2: o que nao esta implementado RECUSA. O `sxtb16` esta identificado na
  // tabela e recusado com o nome dele -- nao executado como outra coisa.
  Bancada b;
  b.R(0, 0xDEADBEEFu);
  b.Instrucao(Sxtb16Recusado());
  b.Terminar();
  const std::uint64_t antes = b.Cpu().InstruscoesRecusadas();
  b.Correr(1);
  EXPECT_EQ(b.Cpu().InstruscoesRecusadas(), antes + 1);
  EXPECT_EQ(b.Cpu().FamiliaDaUltima(), std::string("sxtb16"));
  EXPECT_EQ(b.R(0) & 0xFFFF0000u, 0xDEAD0000u) << "e nao mexe no registrador";
}

TEST(Cpu, DspClzContaOsZerosDaEsquerda) {
  Bancada b;
  b.R(2, 0x0000FFFFu);
  b.Instrucao(Clz(6, 2));
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.R(6), 16u);
}

TEST(Cpu, DspSmulbbMultiplicaAsMeiasPalavrasDeBaixo) {
  // `smulbb r1, r1, lr`: Rd = bits 19-16 = 1, Rs = bits 11-8 = 14, Rm = bits 3-0
  // = 1. O resultado e (int16)r1 * (int16)r14. Valores NEGATIVOS de proposito:
  // com positivos, uma multiplicacao sem sinal dava o mesmo.
  Bancada b;
  b.R(1, 0x0000FFFEu);    // -2
  b.R(14, 0x00000003u);   //  3
  b.Instrucao(SmulXy("bb", 1, 14, 1));
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.R(1), 0xFFFFFFFAu) << "-2 * 3";
}

TEST(Cpu, DspSmulbtUsaAMeiaDeCimaDeRs) {
  // A variante MISTA e a que separa os bits 6 e 5: com as duas metades trocadas
  // o resultado e outro, e o valor errado parece plausivel.
  Bancada b;
  b.R(4, 0x00000002u);    // Rm: metade de baixo = 2
  b.R(3, 0x00070000u);    // Rs: metade de cima = 7
  b.Instrucao(SmulXy("bt", 1, 3, 4));
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.R(1), 14u) << "2 * 7";
  // E O NOME, que e o que o auditor compara com o objdump: `bt` = metade de
  // baixo de Rm e de cima de Rs. Trocar os dois bits (6 e 5) da o mesmo numero
  // de metades escolhidas, mas o nome errado -- e um nome errado aqui significa
  // uma instrucao lida ao contrario do que o binutils diz.
  EXPECT_EQ(b.Cpu().FamiliaDaUltima(), std::string("smulbt"));

  Bancada b2;
  b2.R(4, 0x00070002u);   // Rm: baixo = 2, cima = 7
  b2.R(3, 0x00000009u);   // Rs: baixo = 9
  b2.Instrucao(SmulXy("tb", 1, 3, 4));  // metade de CIMA de Rm x a de BAIXO de Rs
  b2.Terminar();
  b2.Correr(1);
  EXPECT_EQ(b2.R(1), 63u) << "7 * 9";
  EXPECT_EQ(b2.Cpu().FamiliaDaUltima(), std::string("smultb"));
}

TEST(Cpu, DspSmlaXySomaORegistradorDeAcumulacao) {
  // `smlabt r4, r3, r3, r1` (a palavra medida do chessbots): Rd = 4, Rs = 3,
  // Ra = 1, Rm = 3. Rd = (int16)Rm_lo * (int16)Rs_hi + Ra. Mede a ORDEM dos
  // operandos: trocar Rs por Ra da outro numero.
  Bancada b;
  b.R(3, 0x00090002u);   // Rs: cima = 9; Rm: baixo = 2
  b.R(1, 100u);          // acumulador
  b.R(4, 0u);
  b.Instrucao(SmlaXy("bt", 4, 1, 3, 3));
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.R(4), 118u) << "2 * 9 + 100";
}

TEST(Cpu, DspSmulxyNaoAceitaBits15a12DiferentesDeZero) {
  // ARMADILHA MEDIDA: os bits 15-12 do `SMULxy` sao reservados e tem de ser
  // ZERO. `0xE1641382` (bits 15-12 = 1) NAO e `smulbb` -- o objdump chama-lhe
  // `cmn r4, r2, lsl #3`. Uma mascara que os deixasse livres transformava uma
  // instrucao de dados processados numa multiplicacao, em silencio.
  Bancada b;
  b.R(4, 0x11111111u);
  b.R(2, 0x22222222u);
  b.Instrucao(0xE1641382u);
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.Cpu().FamiliaDaUltima(), std::string("cmn"));
  EXPECT_EQ(b.R(4), 0x11111111u) << "o cmn nao escreve no Rd";
}

TEST(Cpu, DspQdaddSaturaEDevolveOMaximo) {
  // `qdadd` soma o DOBRO de Rm e satura. Com 0x7FFFFFFF em Rn e 1 em Rm, o
  // dobro e 2 e a soma satura: a resposta e 0x7FFFFFFF e a bandeira Q fica
  // posta (bit 27 do CPSR).
  Bancada b;
  b.R(0, 0x7FFFFFFFu);
  b.R(4, 1u);
  b.Instrucao(Qdadd(0, 0, 4));
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.R(0), 0x7FFFFFFFu);
  EXPECT_NE(b.Cpu().Cpsr() & (1u << 27), 0u) << "a bandeira Q";
}

TEST(Cpu, DspQsubSaturaNoNegativo) {
  Bancada b;
  b.R(0, 0x80000000u);  // o menor inteiro
  b.R(4, 1u);
  b.Instrucao(Qsub(0, 0, 4));
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.R(0), 0x80000000u);
  EXPECT_NE(b.Cpu().Cpsr() & (1u << 27), 0u);
}

TEST(Cpu, DspNaoTocaNoSwpNemNoBkpt) {
  // As tres formas que partilham os bits 27-24 = 0001: o SWP (bits 7-4 = 1001) e
  // o BKPT (bits 7-4 = 0111) NAO sao do grupo DSP. Sem esta distincao o
  // despachante passava a testar uma coisa e a executar outra.
  Bancada b;
  b.R(0, 0x00110000u);
  b.R(2, 0x11223344u);
  b.Mem().Escrever32(0x00110000u, 0x55667788u);
  b.Instrucao(0xE1002092u);  // swp r2, r2, [r0]
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.R(2), 0x55667788u) << "o swp leu o valor antigo para o Rd";
  EXPECT_EQ(b.Mem().Ler32(0x00110000u), 0x11223344u) << "e escreveu o Rm do sitio";

  Bancada b2;
  b2.Instrucao(0xE1200070u);  // bkpt 0
  b2.Terminar();
  const std::uint64_t antes = b2.Cpu().InstruscoesRecusadas();
  b2.Correr(1);
  EXPECT_EQ(b2.Cpu().InstruscoesRecusadas(), antes + 1) << "o BKPT recusa: nao ha depurador";
  EXPECT_EQ(b2.Cpu().FamiliaDaUltima(), std::string("bkpt"));
}

TEST(Cpu, PldNaoTocaNaMemoriaNemNosRegistradores) {
  // O PLD e uma DICA e a semantica dele e nao fazer nada. Trata-lo como "stub
  // silencioso" seria um erro: nao ha caminho por implementar. O que o teste
  // prova e que ele NAO tem o efeito da transferencia simples que o substituia
  // (com os bits 27-25 = 101 e o bit 24 = 1, o PLD caia em "condicao NV" e era
  // RECUSADO -- 4 630 palavras no corpus).
  Bancada b;
  b.R(0, 0x00110000u);
  b.R(5, 0x11223344u);
  b.Instrucao(PldImediato(0, 0x20));
  b.Instrucao(PldRegistrador(0, 5));
  b.Terminar();
  const std::uint64_t antes = b.Cpu().InstruscoesRecusadas();
  b.Correr(2);
  EXPECT_EQ(b.Cpu().InstruscoesRecusadas(), antes) << "o pld nao recusa: e uma dica";
  EXPECT_EQ(b.Cpu().FamiliaDaUltima(), std::string("pld"));
  EXPECT_EQ(b.R(5), 0x11223344u);
  EXPECT_EQ(b.R(0), 0x00110000u) << "e nao escreve no registrador da base";
}

TEST(Cpu, BlxImediatoTrocaParaThumbEOGuardaOLr) {
  Bancada b;
  b.Instrucao(BlxImediato(1));  // para pc + 8 + 4
  b.Terminar();
  b.Correr(1);
  EXPECT_NE(b.Cpu().Cpsr() & Cpsr::kT, 0u) << "entra em Thumb";
  EXPECT_EQ(b.R(15), 0x00100000u + 8u + 4u);
  EXPECT_EQ(b.R(14), 0x00100004u) << "o LR e a instrucao seguinte";
}

TEST(Cpu, McrNaoVaiParaOSwiEDepoisDoArranjoVaiParaOCoprocessador) {
  // Os bits 27-25 do CDP/MCR/MRC sao 111, os MESMOS do SWI -- o que os separa e
  // o bit 24 (SWI = `cond 1111 imm24`). O interpretador mandava os dois para o
  // `SWI`, e o efeito era que o `mrc p15` (a leitura do tipo de cache) nunca
  // chegava ao `Coprocessador`.
  Bancada b;
  b.Instrucao(0xEE110F10u);  // mrc p15, 0, r0, c1, c0, 0
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.Cpu().FamiliaDaUltima(), std::string("mrc"));
  EXPECT_EQ(b.R(0), 0x410FB760u) << "o valor de cache declarado pelo Coprocessador";

  Bancada b2;
  b2.Instrucao(Swi(0x123456u));
  b2.Terminar();
  const std::uint64_t antes = b2.Cpu().InstruscoesRecusadas();
  b2.Correr(1);
  EXPECT_EQ(b2.Cpu().FamiliaDaUltima(), std::string("swi"));
  EXPECT_EQ(b2.Cpu().InstruscoesRecusadas(), antes + 1);
}

TEST(Cpu, MrsComBitsBaixosDiferentesDeZeroNaoEMrs) {
  // A MASCARA DO MRS VAI ATE AO BIT 0. Com a mascara antiga (que deixava os doze
  // bits baixos livres e testava o MRS ANTES do SWP), `0xE10F0090` era lido como
  // MRS -- e o objdump diz `swp r0, r0, [pc]`.
  Bancada b;
  b.R(0, 0x00110000u);
  b.R(2, 0x11223344u);
  b.Mem().Escrever32(0x00110000u, 0x55667788u);
  b.Instrucao(0xE10F2090u);  // swp r2, r2, [pc] -- a base e o PC, so para o nome
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.Cpu().FamiliaDaUltima(), std::string("swp"));
}

TEST(Cpu, ThumbLdrhEStrhDoFormato8UsamDoisBytes) {
  // O `meia` estava MORTO na versao anterior (`(instr & 0xF000) == 0x8000` E
  // `(instr & 0x1000) != 0` nunca e verdade), e o STRH/LDRH corriam como
  // STR/LDR de 32 bits -- que escrevem quatro bytes onde o jogo escreve dois.
  Bancada b;
  b.R(2, 0x00110000u);
  b.R(1, 0x11223344u);
  b.R(7, 0x00100005u);  // entra em Thumb: `bx r7` com o bit 0 posto
  b.Mem().Escrever32(0x00110000u, 0xAAAAAAAAu);
  b.Instrucao(0xE12FFF17u);                                // bx r7 -> Thumb
  b.Thumb(ThumbMemImediato(false, true, false, 0, 2, 1));  // strh r1, [r2, #0]
  b.Thumb(ThumbMemImediato(true, true, false, 0, 2, 3));   // ldrh r3, [r2, #0]
  b.Terminar();
  b.Correr(3);
  EXPECT_EQ(b.Mem().Ler32(0x00110000u), 0xAAAA3344u) << "so os dois bytes de baixo mudaram";
  EXPECT_EQ(b.R(3), 0x00003344u) << "e o ldrh zero-extende";
}

TEST(Cpu, ThumbStrbEStrDePalavraGuardamENaoCarregam) {
  // DEFEITO MEDIDO NO ESPACO THUMB INTEIRO (65 536 meias-palavras, auditor):
  // o `L` destas formas e o BIT 11 em todas elas, e o codigo lia os bits 12-11
  // (`(instr >> 11) & 3`), que dao 0 no 0x6000 mas **2 no 0x7000 e no 0x9000**.
  // Como o teste era `op == 0`, o `strb` e o `str` de palavra eram executados
  // como LEITURA -- 2 048 + 2 048 meias-palavras do espaco, em silencio.
  Bancada b;
  b.R(2, 0x00110010u);
  b.R(1, 0x11223344u);
  b.R(7, 0x00100005u);
  b.Mem().Escrever8(0x00110010u, 0x00u);
  b.Instrucao(0xE12FFF17u);                                 // bx r7 -> Thumb
  b.Thumb(ThumbMemImediato(false, false, true, 0, 2, 1));   // strb r1, [r2, #0]
  b.Thumb(ThumbMemImediato(false, false, false, 0, 2, 1));  // str  r1, [r2, #0]
  b.Terminar();
  b.Correr(3);
  EXPECT_EQ(b.Mem().Ler8(0x00110010u), 0x44u) << "o strb guardou o byte de baixo";
  EXPECT_EQ(b.Mem().Ler32(0x00110010u), 0x11223344u) << "e o str guardou a palavra";
}

TEST(Cpu, ThumbFormato5FazAsSeteFormasDeMemoriaComRegistrador) {
  // As sete formas que faltavam (LDRH/STRH/LDRSB/LDRSH e companhia) vivem no
  // formato 5. O STRH tem de escrever DOIS bytes e o LDRSH tem de estender o
  // SINAL -- as duas coisas que uma implementacao apressada troca.
  Bancada b;
  b.R(2, 0x00110020u);
  b.R(1, 0x00008080u);
  b.R(4, 0x00000000u);
  b.R(7, 0x00100005u);
  b.Instrucao(0xE12FFF17u);      // bx r7 -> Thumb
  b.Thumb(ThumbF5(1, 4, 2, 1));  // strh r1, [r2, r4]
  b.Thumb(ThumbF5(7, 4, 2, 3));  // ldrsh r3, [r2, r4]
  b.Terminar();
  b.Correr(3);
  EXPECT_EQ(b.Mem().Ler32(0x00110020u), 0x00008080u) << "so dois bytes escritos";
  EXPECT_EQ(b.R(3), 0xFFFF8080u) << "0x8080 com sinal";
}

TEST(Cpu, ThumbFormato5ContinuaARecusarOQueNaoConhece) {
  // O espaco Thumb tem formas que NAO estao implementadas. Elas RECUSAM com o
  // nome da forma (formato 1 = deslocamento imediato), e nao executam outra
  // coisa: medido, 22 692 das 65 536 meias-palavras do espaco.
  Bancada b;
  b.R(7, 0x00100005u);
  b.Instrucao(0xE12FFF17u);  // bx r7 -> Thumb
  b.Thumb(0x0000u);          // lsls r0, r0, #0 -- formato 1
  b.Terminar();
  const std::uint64_t antes = b.Cpu().InstruscoesRecusadas();
  b.Correr(2);
  EXPECT_EQ(b.Cpu().InstruscoesRecusadas(), antes + 1);
  EXPECT_EQ(b.Cpu().FamiliaDaUltima(), std::string("thumb:formato1_deslocamento_imediato"));
}

// ===========================================================================
// OS DOIS DEFEITOS DO `emulator_neo`: ambos SILENCIOSOS, ambos com `recusadas = 0`
// ===========================================================================

TEST(Cpu, LdrParaOPcSaltaEODespachoNaoEscrevePcMaisQuatroPorCima) {
  // `ldr pc,[rn,#imm]` -- a UNICA forma como a familia do `emulator_neo` chama o
  // sistema (`mov lr,pc` + `ldr pc,[tabela,#slot]`), 5240 vezes so no
  // `karnovr.mod`. O despacho escrevia `pc + 4` DEPOIS da transferencia e
  // anulava o salto: o modulo seguia em frente como se a chamada nao existisse.
  //
  // NAO ERA UMA RECUSA: `InstruscoesRecusadas()` fica a ZERO nos dois casos.
  // Era uma instrucao executada e desfeita -- por isso 10 titulos morriam sem
  // deixar rasto nas faltas.
  Bancada b;
  constexpr std::uint32_t kTabela = 0x00120000u;
  constexpr std::uint32_t kAlvo = 0x00130000u;
  b.Mem().Escrever32(kTabela + 8, kAlvo);  // o slot 2 da tabela
  b.R(4, kTabela);
  const std::uint64_t recusadas_antes = b.Cpu().InstruscoesRecusadas();
  b.Instrucao(LdrImediato(15, 4, 8));  // ldr pc,[r4,#8]
  b.Terminar();
  b.Correr(1);

  EXPECT_EQ(b.R(15), kAlvo) << "o PC tem de ser o que a tabela diz";
  EXPECT_NE(b.R(15), 0x00100004u) << "e NAO a instrucao seguinte";
  EXPECT_EQ(b.Cpu().InstruscoesRecusadas(), recusadas_antes)
      << "o defeito era silencioso: nada era recusado";
}

TEST(Cpu, LdrParaOPcComCondicaoFalsaAvancaComoQualquerOutra) {
  // A outra metade da guarda: se a condicao NAO passa, a transferencia nao
  // acontece e o PC tem de avancar normalmente. Sem esta metade, a correccao
  // acima transformaria um `ldrne pc,...` nao tomado num laco parado.
  Bancada b;
  constexpr std::uint32_t kTabela = 0x00120000u;
  b.Mem().Escrever32(kTabela + 8, 0x00130000u);
  b.R(4, kTabela);
  b.R(1, 5);
  b.Instrucao(CmpImediato(1, 5));                        // Z = 1
  b.Instrucao(LdrImediato(15, 4, 8) & ~(0xFu << 28) | (0x1u << 28));  // ldrNE pc,[r4,#8]
  b.Terminar();
  b.Correr(2);
  EXPECT_EQ(b.R(15), 0x00100008u) << "condicao falsa: avanca para a seguinte";
}

TEST(Cpu, AritmeticaSemBitSNaoMexeEmCarryNemOverflow) {
  // `add r0,r0,#16` SEM o bit S nao pode tocar no carry. As seis aritmeticas
  // (SUB/RSB/ADD/ADC/SBC/RSC) escreviam C e V sempre; as de logica nao, e por
  // isso o defeito passou despercebido durante toda a etapa 1.
  //
  // MEDIDO no laco de entrada do `emulator_neo`: o `add` apagava o carry do
  // `cmp` anterior e o `bcc` ficava SEMPRE tomado -- laco infinito, ainda vivo
  // aos 60 milhoes de passos. Um `add` que mexe em bandeiras nao da erro: da
  // uma decisao errada muito mais tarde, noutro sitio.
  Bancada b;
  b.R(1, 0x00000020u);
  b.Instrucao(CmpImediato(1, 0x10));      // 0x20 - 0x10: sem emprestimo -> C = 1
  b.Instrucao(SomaImediata(0, 0, 0x10));  // add r0,r0,#16 -- SEM bit S
  b.Terminar();
  b.Correr(2);
  EXPECT_TRUE(C(b)) << "o carry do `cmp` tem de sobreviver ao `add` sem S";

  // E o mesmo com uma soma que TRANSBORDA: sem bit S, o transbordo nao se ve.
  Bancada c;
  c.R(1, 0u);
  c.R(0, 0xFFFFFFFFu);
  c.Instrucao(CmpImediato(1, 0x10));      // 0 - 0x10: com emprestimo -> C = 0
  c.Instrucao(SomaImediata(0, 0, 1));     // 0xFFFFFFFF + 1 -- daria C = 1 com S
  c.Terminar();
  c.Correr(2);
  EXPECT_FALSE(C(c)) << "sem bit S, o transbordo do `add` nao chega as bandeiras";

  // A PROVA DE QUE A CORRECCAO NAO MATOU O CAMINHO NORMAL: com bit S, escreve.
  Bancada d;
  d.R(0, 0xFFFFFFFFu);
  d.Instrucao(SomaImediata(0, 0, 1, true));  // adds r0,r0,#1
  d.Terminar();
  d.Correr(1);
  EXPECT_TRUE(C(d)) << "com bit S, o carry TEM de ser escrito";
  EXPECT_TRUE(Z(d)) << "0xFFFFFFFF + 1 = 0";
}


// ===========================================================================
// CACADA DE DEFEITOS SILENCIOSOS -- a segunda ronda (ver docs `10-cpu-cacada`)
// ===========================================================================
//
// O padrao caçado e sempre o mesmo: a instrucao E EXECUTADA, `recusadas` fica a
// ZERO, nao ha falta nenhuma para registar -- e o efeito esta errado. Cada teste
// abaixo nomeia a palavra REAL (conferida no `arm-none-eabi-objdump`) e quantas
// vezes ela correu na bateria dos 62 titulos.

TEST(Cpu, MlaSomaAParcelaDosBits15a12ENaoAMultiplicaPorEla) {
  // `mla r0, r1, r2, r3` == 0xE0203291 (MEDIDO no binutils). ARM ARM A4.1.26:
  //   Rd(19-16) = Rm(3-0) * Rs(11-8) + Rn(15-12)
  // O campo 15-12 e a PARCELA SOMADA. O interpretador lia-o como primeiro
  // FACTOR e somava o campo 11-8 -- ou seja, calculava `Rn*Rm + Rs`.
  //
  // Com 7 * 6 + 100 o certo e 142; o defeito dava 706 (= 100*7 + 6). Nao ha
  // recusa: e uma multiplicacao valida de numeros errados.
  // MEDIDO na bateria dos 62 titulos: 15 585 `mla` executados.
  Bancada b;
  b.R(1, 7);
  b.R(2, 6);
  b.R(3, 100);
  const std::uint64_t recusadas_antes = b.Cpu().InstruscoesRecusadas();
  b.Instrucao(Mla(0, 1, 2, 3));  // mla r0, r1, r2, r3 -- r1*r2 + r3
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.R(0), 142u) << "Rm*Rs + Rn";
  EXPECT_NE(b.R(0), 706u) << "e nao Rn*Rm + Rs";
  EXPECT_EQ(b.Cpu().InstruscoesRecusadas(), recusadas_antes) << "o defeito era silencioso";
}

TEST(Cpu, MlaComACodificacaoRealDoObjdump) {
  // A MESMA AFIRMACAO, feita com a PALAVRA e nao com o montador: se o montador
  // dos testes tiver os campos trocados (ja teve), este teste continua a dizer a
  // verdade. 0xE0201392 = `mla r0, r2, r3, r1` -> r0 = r2*r3 + r1.
  Bancada b;
  b.R(1, 7);
  b.R(2, 6);
  b.R(3, 100);
  b.Instrucao(0xE0201392u);
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.R(0), 607u) << "6 * 100 + 7";
}

TEST(Cpu, RrxRodaUmBitPeloCarryENaoEIdentidade) {
  // `rrx r0, r1` == 0xE1A00061 (MEDIDO): tipo ROR com o campo de quantidade a
  // ZERO. O interpretador convertia esse zero em 32 -- e ROR #32 e a
  // IDENTIDADE. O registrador saia intacto, sem recusa nenhuma.
  // MEDIDO na bateria: 7 execucoes no operando 2 e 1 no offset de um LDR/STR.
  Bancada b;
  b.R(1, 3u);
  b.R(4, 0u);
  b.Instrucao(CmpImediato(4, 0));  // 0 - 0: sem emprestimo -> C = 1
  b.Instrucao(0xE1A00061u);        // rrx r0, r1
  b.Terminar();
  b.Correr(2);
  EXPECT_EQ(b.R(0), 0x80000001u) << "(3 >> 1) | (C << 31)";
  EXPECT_NE(b.R(0), 3u) << "e NAO o valor intacto do ROR #32";
}

TEST(Cpu, RrxComBitSEscreveOCarryDeSaida) {
  // `movs r0, r1, rrx` == 0xE1B00061: o carry de saida do RRX e o BIT 0 do
  // valor de entrada. E o par do teste acima -- sem ele, um RRX que devolvesse
  // o valor certo com o carry errado passaria.
  Bancada b;
  b.R(1, 3u);
  b.R(4, 0u);
  b.Instrucao(CmpImediato(4, 0));  // C = 1
  b.Instrucao(0xE1B00061u);        // movs r0, r1, rrx
  b.Terminar();
  b.Correr(2);
  EXPECT_EQ(b.R(0), 0x80000001u);
  EXPECT_TRUE(C(b)) << "o carry de saida e o bit 0 da entrada";
}

TEST(Cpu, StmComPcNaListaGravaPcMaisOito) {
  // `stmfd sp!, {fp, ip, lr, pc}` == 0xE92DD800 -- o prologo APCS do GCC.
  // O PC LIDO COMO FONTE vale `endereco_da_instrucao + 8`; o `Passo` nao adianta
  // o PC, logo `Get(kPC)` dentro do executor vale `pc` e a palavra gravada saia
  // oito bytes atras. Nao ha recusa: o STM corre inteiro e grava um valor errado.
  // MEDIDO na bateria: 168 `stm` com o PC na lista.
  Bancada b;
  b.R(13, 0x80070000u);
  const std::uint64_t recusadas_antes = b.Cpu().InstruscoesRecusadas();
  b.Instrucao(0xE92DD800u);
  b.Terminar();
  b.Correr(1);
  // ordem crescente de registrador em enderecos crescentes: fp, ip, lr, pc.
  EXPECT_EQ(b.Mem().Ler32(b.R(13) + 12), 0x00100008u) << "pc + 8";
  EXPECT_NE(b.Mem().Ler32(b.R(13) + 12), 0x00100000u) << "e NAO o endereco da propria instrucao";
  EXPECT_EQ(b.Cpu().InstruscoesRecusadas(), recusadas_antes);
}

TEST(Cpu, StrDoPcGravaPcMaisOito) {
  // `str pc, [r4]` == 0xE584F000 -- a mesma regra, na transferencia simples.
  Bancada b;
  b.R(4, 0x00120000u);
  b.Instrucao(0xE584F000u);
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.Mem().Ler32(0x00120000u), 0x00100008u);
}

TEST(Cpu, LdmComPcHonraOBitZeroEEntraEmThumb) {
  // `ldmfd sp!, {r4, pc}` == 0xE8BD8010. No ARMv5T e acima, o LDM com o PC na
  // lista e uma escrita do tipo BX: o bit 0 do valor escolhe o estado.
  // Guardar o bit 0 dentro do PC poe o buscador a ler numa morada IMPAR.
  //
  // MEDIDO na bateria: 534 496 `ldm` com o PC na lista, NENHUM com o bit 0
  // ligado. Esta correccao nao explica nenhum sintoma actual -- fecha um caminho.
  Bancada b;
  b.R(13, 0x80070000u);
  b.Mem().Escrever32(0x80070000u, 0x00200000u);
  b.Mem().Escrever32(0x80070004u, 0x00300001u);  // bit 0 ligado
  b.Instrucao(0xE8BD8010u);
  b.Correr(1);
  EXPECT_EQ(b.R(15), 0x00300000u) << "o PC fica sem o bit 0";
  EXPECT_NE(b.R(15) & 1u, 1u);
  EXPECT_EQ(b.Cpu().Cpsr() & Cpsr::kT, Cpsr::kT) << "e o estado passa a Thumb";
}

TEST(Cpu, LdrParaOPcHonraOBitZeroEEntraEmThumb) {
  // `ldr pc, [r4]` == 0xE594F000 -- a mesma regra do LDM.
  // MEDIDO na bateria: 748 `ldr pc`, nenhum com o bit 0 ligado.
  Bancada b;
  b.R(4, 0x00120000u);
  b.Mem().Escrever32(0x00120000u, 0x00300001u);
  b.Instrucao(0xE594F000u);
  b.Correr(1);
  EXPECT_EQ(b.R(15), 0x00300000u);
  EXPECT_EQ(b.Cpu().Cpsr() & Cpsr::kT, Cpsr::kT);
}

TEST(Cpu, BxDoHospedeiroHonraOBitZeroENaoHerdaOModoDaFaseAnterior) {
  // O SALTO DO HOSPEDEIRO. Ha dois sitios em que NAO e o guest que salta, e sim
  // o despacho a chamar codigo do modulo: a entrega do `EVT_APP_START` ao
  // `HandleEvent` do applet (`tools/bateria.cpp`) e o callback do temporizador
  // (`Despacho::PrepararCallbackDoTemporizador`). Nos dois, a chamada tem a
  // convencao de um `bx`: o BIT 0 do endereco escolhe ARM ou Thumb e nao faz
  // parte do PC.
  //
  // Com `Set(kPC, alvo)` -- o que estava escrito -- o PC era o endereco COM o bit
  // 0 e o MODO era o que a fase anterior deixou. E foi o que se mediu no `cnk2`,
  // na arvore de 15/09: a fase do `CreateInstance` terminou num `bx r3` com o bit
  // 0 ligado (0x808e0899), o nucleo ficou em Thumb, e a fase do `EVT_APP_START`
  // correu 186 486 543 passos (o tecto declarado no corpus) a ler codigo ARM como
  // Thumb, com 14 345 145 recusas da MESMA instrucao. Com o bit 0 respeitado, a
  // mesma fase gasta 155 342 passos.
  for (bool impar : {false, true}) {
    Bancada b;
    // A fase anterior deixa o CPSR no estado CONTRARIO ao que o alvo pede: e este
    // o caso que o defeito nao apanhava.
    std::uint32_t cpsr = b.Cpu().Cpsr();
    if (impar) {
      cpsr &= ~Cpsr::kT;
    } else {
      cpsr |= Cpsr::kT;
    }
    b.Cpu().SetCpsr(cpsr);
    const std::uint32_t alvo = 0x00123456u | (impar ? 1u : 0u);
    b.Cpu().Bx(alvo);
    EXPECT_EQ(b.R(15), 0x00123456u) << "o bit 0 nao faz parte do PC";
    EXPECT_EQ((b.Cpu().Cpsr() & Cpsr::kT) != 0, impar)
        << "quem manda no modo e o bit 0 do alvo, e nao o CPSR que la estava";
  }
  {
    // E o que acontece a seguir, que e o que interessa: duas palavras Thumb
    // escritas no alvo tem de ser lidas como DUAS instrucoes Thumb.
    Bancada b;
    b.Cpu().Bx(0x00100001u);  // o alvo e a propria base, com o bit 0 ligado
    b.Thumb(0x2001u);         // movs r0, #1
    b.Thumb(0x2102u);         // movs r1, #2
    b.Correr(2);
    EXPECT_EQ(b.Cpu().Cpsr() & Cpsr::kT, Cpsr::kT);
    EXPECT_EQ(b.R(0), 1u) << "a primeira palavra Thumb executou";
    EXPECT_EQ(b.R(1), 2u) << "e o PC avancou de DOIS em dois, nao de quatro em quatro";
  }
}

TEST(Cpu, LdmComABaseNaListaNaoApagaOValorCarregado) {
  // `ldmia r4!, {r4, r5}` == 0xE8B40030. Com o registador base DENTRO da lista,
  // quem manda e o valor carregado -- a escrita na base vinha depois do laco e
  // apagava-o. MEDIDO na bateria: 11 ocorrencias.
  Bancada b;
  b.R(4, 0x00120000u);
  b.Mem().Escrever32(0x00120000u, 0xAAAA0000u);
  b.Mem().Escrever32(0x00120004u, 0xBBBB0000u);
  b.Instrucao(0xE8B40030u);
  b.Terminar();
  b.Correr(1);
  EXPECT_EQ(b.R(4), 0xAAAA0000u) << "o valor lido da memoria";
  EXPECT_NE(b.R(4), 0x00120008u) << "e NAO o endereco final da escrita na base";
  EXPECT_EQ(b.R(5), 0xBBBB0000u);
}

TEST(Cpu, SbcsTrataOEmprestimoComoTerceiroOperando) {
  // `sbcs r0, r1, r2` == 0xE0D10002 com r2 = 0xFFFFFFFF e C = 0.
  // O codigo antigo fazia `b = op2 + 1`, que DA A VOLTA a zero: a conta passava
  // a `a - 0`, o resultado saia certo por acaso e o carry saia trocado.
  // MEDIDO na bateria: ZERO ocorrencias deste caso. Fica pela correccao.
  Bancada b;
  b.R(1, 5u);
  b.R(2, 0xFFFFFFFFu);
  b.R(4, 1u);
  b.Instrucao(CmpImediato(4, 2));  // 1 - 2: com emprestimo -> C = 0
  b.Instrucao(0xE0D10002u);        // sbcs r0, r1, r2
  b.Terminar();
  b.Correr(2);
  EXPECT_EQ(b.R(0), 5u) << "5 - 0xFFFFFFFF - 1 = 5 (mod 2^32)";
  EXPECT_FALSE(C(b)) << "ha emprestimo: C = 0";
}

TEST(Cpu, ThumbLdrRelativoAoSpNaoEOFormato9) {
  // FORMATO 11 DO THUMB: `1001 L Rd(10-8) Word8` -- relativo ao SP.
  // 0x9801 = `ldr r0, [sp, #4]` (MEDIDO no binutils, `-mthumb`).
  //
  // A guarda das transferencias abria em `(instr & 0xE000) == 0x8000`, que
  // apanha 0x8000-0x9FFF, e tratava o 0x9xxx como FORMATO 9 -- `ldr r1,[r0,#0]`.
  // Registador errado, base errada, deslocamento errado, ZERO recusas, e a sonda
  // do descodificador a dizer "ldr", que e o mesmo nome que o objdump da: por
  // isso o auditor diferencial tambem nao o via.
  Bancada b;
  b.R(13, 0x80070000u);
  b.Mem().Escrever32(0x80070004u, 0x1234ABCDu);
  b.R(0, 0x00990000u);  // se o formato 9 correr, le daqui e escreve em r1
  const std::uint64_t recusadas_antes = b.Cpu().InstruscoesRecusadas();
  b.Cpu().SetCpsr(b.Cpu().Cpsr() | Cpsr::kT);
  b.Thumb(0x9801u);  // ldr r0, [sp, #4]
  b.Correr(1);
  EXPECT_EQ(b.R(0), 0x1234ABCDu) << "o destino e r0 (bits 10-8) e a base e o SP";
  EXPECT_EQ(b.R(1), 0u) << "o formato 9 escreveria em r1";
  EXPECT_EQ(b.Cpu().InstruscoesRecusadas(), recusadas_antes) << "era silencioso";
}

TEST(Cpu, ThumbStrRelativoAoSpEscreveNaPilha) {
  // 0x9302 = `str r3, [sp, #8]` (MEDIDO no binutils).
  Bancada b;
  b.R(13, 0x80070000u);
  b.R(3, 0xCAFEBABEu);
  b.Cpu().SetCpsr(b.Cpu().Cpsr() | Cpsr::kT);
  b.Thumb(0x9302u);
  b.Correr(1);
  EXPECT_EQ(b.Mem().Ler32(0x80070008u), 0xCAFEBABEu);
}

TEST(Cpu, ThumbBlxRegistradorEscreveOLr) {
  // 0x4798 = `blx r3` (MEDIDO no binutils). A mascara do ramo era 0xFF87, que
  // exige o BIT 7 A ZERO -- e o bit 7 e exactamente o que separa o BLX do BX.
  // O ramo do `blx` era CODIGO MORTO e a instrucao caia na recusa final.
  Bancada b;
  b.R(3, 0x00200000u);  // alvo em ARM
  b.Cpu().SetCpsr(b.Cpu().Cpsr() | Cpsr::kT);
  b.Thumb(0x4798u);
  b.Correr(1);
  EXPECT_EQ(b.R(14), 0x00100003u) << "LR = (pc + 2) | 1";
  EXPECT_EQ(b.R(15), 0x00200000u);
  EXPECT_EQ(b.Cpu().Cpsr() & Cpsr::kT, 0u) << "o alvo tem o bit 0 a zero: volta a ARM";
}

TEST(Cpu, MovPcLrRetornaEAddPcFazTabelaDeSaltos) {
  // `mov pc,lr` e o retorno de funcao mais comum do ARM: 2 727 ocorrencias em 43
  // dos 62 modulos do corpus. `add pc,pc,rX,lsl#2` e uma tabela de saltos: 29
  // em 27 modulos. Nos dois, o `DadosProcessados` escrevia o PC e o despacho
  // escrevia `pc + 4` por cima -- o salto era anulado em SILENCIO.
  //
  // E o MESMO defeito do `ldr pc,[rn,#imm]` (commit d0f1146). Apareceu duas
  // vezes porque a correccao anterior tratou o SITIO e nao o PADRAO.
  {
    Bancada b;
    b.R(14, 0x00130000u);  // lr
    const std::uint64_t antes = b.Cpu().InstruscoesRecusadas();
    b.Instrucao(MoveRegistrador(15, 14, 0, 0));  // mov pc, lr
    b.Terminar();
    b.Correr(1);
    EXPECT_EQ(b.R(15), 0x00130000u) << "mov pc,lr tem de saltar para o lr";
    EXPECT_NE(b.R(15), 0x00100004u) << "e nao para a instrucao seguinte";
    EXPECT_EQ(b.Cpu().InstruscoesRecusadas(), antes) << "o defeito era silencioso";
  }
  {
    // `add pc,pc,r1` -- o PC lido como fonte vale pc + 8 (ARM ARM).
    Bancada b;
    b.R(1, 0x40u);
    b.Instrucao(SomaRegistrador(15, 15, 1));  // add pc, pc, r1
    b.Terminar();
    b.Correr(1);
    EXPECT_EQ(b.R(15), 0x00100000u + 8u + 0x40u) << "pc + 8 + r1";
  }
  {
    // E a outra metade da guarda: com a condicao FALSA, avanca como qualquer
    // outra. Sem isto, a correccao transformaria um `moveq pc,lr` nao tomado
    // num laco parado.
    Bancada b;
    b.R(14, 0x00130000u);
    b.R(1, 5);
    b.Instrucao(CmpImediato(1, 7));  // 5 != 7 -> Z = 0
    const std::uint32_t moveq = (MoveRegistrador(15, 14, 0, 0) & ~(0xFu << 28)) | (0x0u << 28);
    b.Instrucao(moveq);  // moveq pc, lr -- NAO tomado
    b.Terminar();
    b.Correr(2);
    EXPECT_EQ(b.R(15), 0x00100008u) << "condicao falsa: avanca para a seguinte";
  }
}
