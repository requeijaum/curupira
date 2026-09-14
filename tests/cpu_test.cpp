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
constexpr std::uint32_t Mla(std::uint32_t rd, std::uint32_t rn, std::uint32_t rm, std::uint32_t rs,
                            bool poe_bandeiras = false) {
  // ARM ARM: `MLA Rd, Rn, Rm, Rs` calcula Rd = (Rn * Rm) + Rs. O campo Rn vai
  // nos bits 15-12, o Rm nos 3-0 e o Rs nos 11-8.
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
