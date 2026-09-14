#ifndef ZB2_CORE_TEMPO_TEMPO_H
#define ZB2_CORE_TEMPO_TEMPO_H

// Fonte de tempo unica e INJECTADA.
//
// PRINCIPIO P4 (determinismo por construcao). Medicao que o sustenta: no
// Zeebulator antigo o mesmo titulo com a mesma entrada produzia corridas
// diferentes porque o tempo vinha do sistema, e o JIT mudava o entrelacamento
// dos temporizadores o suficiente para o `zenonia` virar OUTRO jogo -- nao um
// jogo mais lento. Com o tempo injectado, "mesma entrada, mesma saida" deixa de
// ser esperanca e passa a ser propriedade testavel.
//
// Nada aqui le o relogio do sistema. Nada aqui dorme. O tempo so avanca quando
// alguem o avanca.

#include <cstdint>

namespace zb2 {

// Nanossegundos. Inteiro, sem virgula flutuante: somas de tempo tem de ser
// exactas e reproduziveis, e um `double` acumulado nao e nenhuma das duas.
using Ns = std::int64_t;

class Tempo {
 public:
  // Os temporizadores do BREW contam em milissegundos; o Zeebo pede 16 ms por
  // quadro (um tick). Estas duas constantes sao a unica ponte entre unidades.
  static constexpr Ns kNada = 0;
  static constexpr Ns kMs = 1000000;
  static constexpr Ns kTickPadrao = 16 * kMs;

  Ns Agora() const { return agora_; }

  // Avanca o relogio. O unico caminho pelo qual o tempo muda.
  void Avancar(Ns quanto) {
    // Tempo nao anda para tras. Um avanco negativo e um defeito de quem chama,
    // e engoli-lo em silencio esconderia uma contagem de tempo invertida -- o
    // oposto do principio P2.
    if (quanto < 0) {
      ultimo_avanco_negativo_ = quanto;
      ++avancos_invalidos_;
      return;
    }
    agora_ += quanto;
  }

  // Quantos avancos invalidos foram tentados. Um teste le isto; o resto do
  // sistema nao deve encontrar nada aqui.
  std::uint64_t AvancosInvalidos() const { return avancos_invalidos_; }
  Ns UltimoAvancoNegativo() const { return ultimo_avanco_negativo_; }

  // Para o determinismo ser verificavel, o tempo tem de poder ser reposto --
  // duas corridas comparam-se a partir do mesmo zero.
  void Repor() {
    agora_ = 0;
    avancos_invalidos_ = 0;
    ultimo_avanco_negativo_ = 0;
  }

 private:
  Ns agora_ = 0;
  std::uint64_t avancos_invalidos_ = 0;
  Ns ultimo_avanco_negativo_ = 0;
};

}  // namespace zb2

#endif  // ZB2_CORE_TEMPO_TEMPO_H
