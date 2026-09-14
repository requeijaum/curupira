#ifndef ZB2_CORE_CPU_ARM_INTERPRETER_H
#define ZB2_CORE_CPU_ARM_INTERPRETER_H

#include "core/cpu/cpu.h"

namespace zb2 {

// Interpretador ARM de referencia. Escrito para ser LIDO e conferido, nao para
// ser rapido: e o oraculo contra o qual um JIT futuro se compara.
//
// O que esta implementado esta dito no proprio codigo, e o que NAO esta e
// recusado com nome (principio P2) em vez de ignorado. Um interpretador que
// trata uma instrucao desconhecida como "nao fez nada" e a versao em codigo do
// stub silencioso que descartou 86 377 chamadas de GL no projeto antigo.
class ArmInterpreter : public ICpu {
 public:
  ArmInterpreter(Memoria& mem, Traco* traco = nullptr);

  Reg Get(int r) const override;
  void Set(int r, Reg v) override;
  std::uint32_t Cpsr() const override;
  void SetCpsr(std::uint32_t v) override;
  Memoria& Mem() override { return mem_; }

  std::uint64_t Passo() override;
  std::uint64_t Correr(std::uint64_t limite) override;
  void Repor(Reg pc, Reg sp) override;

  // --- consulta, para testes e diagnostico ---
  std::uint64_t InstruscoesRecusadas() const override { return recusadas_; }
  // A ultima instrucao recusada, em hexadecimal, para o diagnostico dizer O QUE
  // faltou em vez de "falhou".
  std::uint32_t UltimaRecusada() const { return ultima_recusada_; }
  std::uint32_t PcDaUltimaRecusada() const { return pc_da_recusada_; }

  // --- sonda do descodificador (P7) -------------------------------------
  // O NOME da instrucao que o executor ACABOU de tratar, escrito no ramo que a
  // tratou. Existe para o auditor diferencial
  // (`tools/auditar_descodificador.py`) comparar a NOSSA descodificacao com a do
  // `arm-none-eabi-objdump` palavra a palavra, sobre o corpus inteiro.
  //
  // PORQUE O NOME VEM DO EXECUTOR, e nao de uma segunda descodificacao: uma
  // segunda descodificacao (em Python, ou uma tabela escrita de memoria) pode
  // divergir da primeira e concordar consigo mesma -- que e exactamente o defeito
  // que esta sonda existe para encontrar. Aqui nao ha duas respostas possiveis:
  // o nome que sai e o ramo que correu.
  //
  // Vazio antes do primeiro passo. Com condicao FALSA vale `-`: nesse caso nada
  // correu, e dizer um nome seria mentir.
  const char* FamiliaDaUltima() const { return familia_; }
  // O motivo pelo qual o executor RECUSOU a ultima instrucao, ou `nullptr` quando
  // nao recusou. `FamiliaDaUltima()` diz O QUE era; isto diz o que faltou.
  const char* MotivoDaRecusa() const { return motivo_recusa_; }

  // Modo actual. Recusa-se a devolver um modo invalido: se o CPSR tiver lixo,
  // isto denuncia.
  Modo ModoAtual() const;

 private:
  // --- registradores ----------------------------------------------------
  // O banco tem os 16 registradores do modo corrente. Os registradores
  // sombreados (r8-r12 do FIQ, r13/r14 dos modos de excepcao, SPSR) existem
  // porque os titulos usam IRQ e FIQ; sem eles, uma interrupcao corromperia o
  // estado do jogo.
  Reg banco_[16] = {};
  Reg sombra_fiq_r8_r12_[5] = {};
  Reg sombra_irq_r13_r14_[2] = {};
  Reg sombra_svc_r13_r14_[2] = {};
  Reg sombra_abt_r13_r14_[2] = {};
  Reg sombra_und_r13_r14_[2] = {};
  mutable Reg spsr_[7] = {};
  mutable std::uint32_t modo_atual_ = 0;
  mutable bool modo_atual_valido_ = true;

  // --- bandeiras --------------------------------------------------------
  bool n_ = false, z_ = false, c_ = false, v_ = false;

  // --- execucao ---------------------------------------------------------
  std::uint32_t Buscar32(std::uint32_t end);
  std::uint32_t Buscar16(std::uint32_t end);
  void Recusar(std::uint32_t instr, std::uint32_t pc, const char* porque);

  bool CondicaoVerdadeira(std::uint32_t cond) const;

  // Deslocamento do operando 2 do ARM (bits 0-11 do campo de dados).
  Reg Deslocar(Reg valor, std::uint32_t tipo, uint32_t quantidade, bool carry_in,
               bool* carry_out) const;
  Reg OperandoDeslocado(std::uint32_t instr, std::uint32_t pc, bool usaImediato,
                        bool* carry_out) const;

  void ExecutarArm(std::uint32_t instr, std::uint32_t pc);
  void ExecutarThumb(std::uint16_t instr, std::uint32_t pc);

  // Familias do ARM.
  void DadosProcessados(std::uint32_t instr, std::uint32_t pc);
  // O grupo "extra load/store": LDRH/STRH/LDRSB/LDRSH/LDRD/STRD. Existe como
  // familia propria porque os bits 27-25 destas instrucoes sao 000 -- os mesmos
  // do grupo de dados processados.
  void TransferenciaExtra(std::uint32_t instr, std::uint32_t pc);
  // ARMv6: as instrucoes "media" -- extensao de sinal/zero (SXTB/UXTH e a
  // familia "A"), reversao de bytes (REV/REV16/REVSH). Vivem em bits 27-24 =
  // 0110, que o primeiro nivel le como TRANSFERENCIA SIMPLES: sem este ramo, um
  // `uxth r3, r5` e executado como `strb` e um `sxth` como `ldr`. Ver a medicao
  // em `tools/auditar_descodificador.py`.
  void MediaArmv6(std::uint32_t instr, std::uint32_t pc);
  // ARMv5TE: a aritmetica DSP (SMULxy/SMLAxy/SMULWy/SMLAWy/SMLALxy), as somas
  // com saturacao (QADD/QSUB/QDADD/QDSUB) e o CLZ. Vive em bits 27-24 = 0001,
  // que o primeiro nivel le como DADOS PROCESSADOS.
  void AritmeticaDsp(std::uint32_t instr, std::uint32_t pc);
  void TransferenciaSimples(std::uint32_t instr, std::uint32_t pc);
  void Bloco(std::uint32_t instr, std::uint32_t pc);
  void Bifurcar(std::uint32_t instr, std::uint32_t pc);
  void Multiplicar(std::uint32_t instr);
  void MultiplicarLongo(std::uint32_t instr);
  void TrocarEntreProcessadorEStatus(std::uint32_t instr);
  void Coprocessador(std::uint32_t instr, std::uint32_t pc);
  void SWI(std::uint32_t instr, std::uint32_t pc);

  Memoria& mem_;
  Traco* traco_ = nullptr;
  std::uint64_t recusadas_ = 0;
  std::uint32_t ultima_recusada_ = 0;
  std::uint32_t pc_da_recusada_ = 0;
  // Sonda do descodificador. Apontam para literais estaticos: nao ha alocacao, e
  // o custo por instrucao e uma escrita de ponteiro.
  const char* familia_ = "-";
  const char* motivo_recusa_ = nullptr;
};

}  // namespace zb2

#endif  // ZB2_CORE_CPU_ARM_INTERPRETER_H
