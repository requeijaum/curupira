// Contagem das instrucoes recusadas ao longo das FASES de um titulo.
//
// PORQUE EXISTE UM FICHEIRO PARA UM CONTADOR.
//
// O `tools/bateria.cpp` fazia `e.recusadas = cpu.InstruscoesRecusadas();` depois
// da carga e outra vez depois do `CreateInstance`. Duas coisas, medidas:
//
//   1. `ArmInterpreter::Repor` faz `recusadas_ = 0`
//      (`core/cpu/arm_interpreter.cpp:164`). O `cpu.Repor` que prepara o
//      `CreateInstance` APAGA a conta da carga, e a atribuicao seguinte (que e
//      `=`, nao `+=`) escreve por cima do que restava.
//   2. A fase do `EVT_APP_START` e o laco de quadro correm DEPOIS da ultima
//      leitura. Nunca eram lidos.
//
// O campo `recusadas` do JSON era, por isso, a conta do `CreateInstance` e so
// dela. E o mesmo defeito de instrumento (P7) que o sub-agente `widget` apanhou
// nas faltas, noutro campo.
//
// A conta vive aqui, e nao dentro da ferramenta, para poder ser TESTADA contra o
// traco -- que conta cada recusa uma vez, em `INSTRUCAO_RECUSADA`, e nao
// sabe de fases nenhumas. Ver `tests/recusas_test.cpp`.
#ifndef TOOLS_RECUSAS_H_
#define TOOLS_RECUSAS_H_

#include <cstdint>
#include <string>

#include "core/cpu/cpu.h"

namespace zb2 {
namespace tools {

// Soma as recusas por DELTA, e nao por leitura absoluta: assim sobrevive a
// qualquer `Repor` -- o de hoje e os que vierem.
class ContadorDeRecusas {
 public:
  // O valor no inicio da fase corrente. Chamar DEPOIS de cada `Repor` (o
  // contador da CPU acabou de ir a zero) para que o delta seguinte seja o
  // trabalho da fase nova, e nao a diferenca contra um valor que ja nao existe.
  void Rearmar(const ICpu& cpu) { base_ = cpu.InstruscoesRecusadas(); }

  // Fecha a fase: devolve a PARCELA dela e soma-a ao total.
  std::uint64_t Colher(const ICpu& cpu) {
    const std::uint64_t agora = cpu.InstruscoesRecusadas();
    // Depois de um `Repor` o contador anda para TRAS (vai a zero). Nesse caso a
    // parcela e o proprio valor actual: subtrair daria um numero enorme por
    // baixo de zero num `uint64`, e o total ficava absurdo em vez de curto.
    const std::uint64_t parcela = (agora >= base_) ? (agora - base_) : agora;
    base_ = agora;
    total_ += parcela;
    return parcela;
  }

  std::uint64_t Total() const { return total_; }

 private:
  std::uint64_t base_ = 0;
  std::uint64_t total_ = 0;
};

// AS PARCELAS PUBLICADAS, e o invariante que as liga ao total.
//
// O total sozinho nao se verifica de fora: quem le o JSON nao tem como saber se
// alguma fase ficou por somar -- que e exactamente o defeito que existiu. Com as
// parcelas ao lado, `recusadas == soma das parcelas` e uma conta que qualquer
// leitor faz, e o teste faz.
struct RecusasPorFase {
  std::uint64_t carga = 0;
  std::uint64_t create = 0;
  std::uint64_t start = 0;
  std::uint64_t quadros = 0;

  std::uint64_t Soma() const { return carga + create + start + quadros; }
};

}  // namespace tools
}  // namespace zb2

#endif  // TOOLS_RECUSAS_H_
