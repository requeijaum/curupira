#ifndef ZB2_CORE_CPU_CPU_H
#define ZB2_CORE_CPU_CPU_H

// Interface da CPU. Existe para haver SEMPRE duas implementacoes: um
// interpretador de referencia (legivel, obviamente correcto) e um JIT (rapido).
//
// PRINCIPIO P5: a incognita mais caro deste projeto nao e a linguagem ARM, e
// saber se o que corre esta certo. Com duas implementacoes independentes, a
// resposta a "isto esta certo?" e uma diferenca entre as duas, e nao uma
// opiniao. No Zeebulator antigo o interpretador era o oraculo, mas so foi usado
// como tal DEPOIS de o JIT ja ter divergido -- e uma dessas divergencias (o
// CPSR inicial) so apareceu porque o teste de lockstep foi escrito.

#include <cstdint>
#include <string>

#include "core/memoria/memoria.h"
#include "core/traco/traco.h"

namespace zb2 {

using Reg = std::uint32_t;

// Indices do banco de registradores, nomeados para o codigo nao ter numeros
// soltos: `c.Get(13)` nao diz nada a quem le.
enum : int {
  kR0 = 0, kR1, kR2, kR3, kR4, kR5, kR6, kR7,
  kR8, kR9, kR10, kR11, kR12,
  kSP = 13, kLR = 14, kPC = 15,
};

// Modos do ARM (os 5 bits baixos do CPSR). O Zeebo e ARM11.
enum class Modo : std::uint32_t {
  Usuario = 0x10,
  FIQ = 0x11,
  IRQ = 0x12,
  Supervisor = 0x13,
  Abort = 0x17,
  Indefinido = 0x1B,
  Sistema = 0x1F,
};

// Um modo e valido quando os 5 bits baixos correspondem a um dos 7 acima. O
// Zeebulator antigo comecava com `cpsr = 0`, e **0 nao e modo nenhum** -- foi
// uma armadilha medida: o `chessbots.mod` faz `mrs r0, cpsr / tst r0, #0xf /
// bxeq lr`, e a leitura de um modo invalido mudava o caminho do jogo.
bool ModoValido(std::uint32_t cpsr);

struct Cpsr {
  static constexpr std::uint32_t kN = 1u << 31;
  static constexpr std::uint32_t kZ = 1u << 30;
  static constexpr std::uint32_t kC = 1u << 29;
  static constexpr std::uint32_t kV = 1u << 28;
  static constexpr std::uint32_t kI = 1u << 7;  // IRQ desactivada
  static constexpr std::uint32_t kF = 1u << 6;  // FIQ desactivada
  static constexpr std::uint32_t kT = 1u << 5;  // Thumb
  static constexpr std::uint32_t kModo = 0x1Fu;
};

// A faixa de saida para C++.
//
// O modulo descobre o sistema por uma tabela de ponteiros de funcao; esses
// ponteiros apontam PARA AQUI. Cada slot implementado recebe um endereco proprio
// dentro desta faixa, e o laco de execucao PARA quando o PC entra nela,
// devolvendo o controle ao C++, que despacha pelo indice.
//
// E o mesmo desenho da arvore antiga (a "faixa de armadilhas" em 0xF0000000), e
// continua a ser o certo: um ponteiro de funcao do guest tem de ser um endereco
// EXECUTAVEL, senao o `bx` cai em memoria que nao existe.
struct Saidas {
  Reg base = 0;
  Reg passo = 4;
  Reg quantos = 0;
  bool ativa = false;

  Reg Endereco(std::uint32_t indice) const { return base + indice * passo; }
  bool Contem(Reg pc, std::uint32_t* indice = nullptr) const {
    if (!ativa || pc < base) return false;
    const Reg delta = pc - base;
    if (delta % passo != 0) return false;
    const Reg i = delta / passo;
    if (i >= quantos) return false;
    if (indice != nullptr) *indice = i;
    return true;
  }
};

class ICpu {
 public:
  virtual ~ICpu() = default;

  virtual Reg Get(int r) const = 0;
  virtual void Set(int r, Reg v) = 0;
  virtual std::uint32_t Cpsr() const = 0;
  virtual void SetCpsr(std::uint32_t v) = 0;
  virtual Memoria& Mem() = 0;

  // Executa UMA instrucao.
  virtual std::uint64_t Passo() = 0;

  // Corre ate o PC entrar na faixa de saida, ou ate ao limite. Devolve quantas
  // instrucoes correu.
  //
  // O limite existe para que um laco sem fim no guest NUNCA prenda o emulador em
  // silencio: foi um defeito medido (o `chessbots` passou 22 segundos dentro de
  // uma unica chamada, e o canal de diagnostico ficava mudo).
  virtual std::uint64_t Correr(std::uint64_t limite) = 0;

  virtual void Repor(Reg pc, Reg sp) = 0;

  // Quantas instrucoes o nucleo nao soube executar. Fica na INTERFACE, e nao no
  // interpretador, porque o despacho de HLE tem de poder ser conduzido pelos dois
  // nucleos -- e um contador de recusas que so um deles tem obriga o despacho a
  // depender do concreto.
  virtual std::uint64_t InstruscoesRecusadas() const { return 0; }

  void ConfigurarSaidas(const Saidas& s) { saidas_ = s; }
  const Saidas& GetSaidas() const { return saidas_; }

 protected:
  Saidas saidas_;
};

}  // namespace zb2

#endif  // ZB2_CORE_CPU_CPU_H
