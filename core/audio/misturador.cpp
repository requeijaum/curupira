#include "core/audio/misturador.h"

namespace zb2::audio {

namespace {
// Escala uma amostra pelo volume (0..100). Inteira, e nao em ponto flutuante:
// o resultado tem de ser o MESMO em qualquer maquina, e a divisao inteira e
// reproduzivel.
std::int32_t Escalar(std::int16_t amostra, std::uint32_t volume) {
  return static_cast<std::int32_t>(amostra * static_cast<std::int32_t>(volume)) /
         static_cast<std::int32_t>(kVolumeMaximo);
}
}  // namespace

void Misturador::Misturar(const std::int16_t* amostras, std::size_t quantas,
                          std::uint32_t volume, bool mudo) {
  if (mudo) volume = 0;
  if (volume > kVolumeMaximo) {
    // TRAVADO, e CONTADO. Aceitar 200 em silencio daria uma saturacao que
    // ninguem sabe de onde veio; recusar a chamada inteira daria uma voz muda
    // sem sintoma. O numero `escalas_limitadas` e o que torna a travagem
    // visivel.
    volume = kVolumeMaximo;
    ++medida_.escalas_limitadas;
  }
  if (amostras == nullptr || quantas == 0) return;

  std::size_t ja = 0;
  while (ja < quantas) {
    if (acumulador_.size() >= kAmostrasPorBlocoMax) FecharBloco();
    const std::size_t cabem = kAmostrasPorBlocoMax - acumulador_.size();
    const std::size_t usar = ((quantas - ja) < cabem) ? (quantas - ja) : cabem;
    for (std::size_t k = 0; k < usar; ++k) {
      const std::int32_t bruto = amostras[ja + k];
      acumulador_.push_back(Escalar(amostras[ja + k], volume));
      ++medida_.amostras_recebidas;
      if (bruto != 0) ++medida_.amostras_nao_nulas;
    }
    ja += usar;
  }
}

void Misturador::MisturarNoBloco(const std::int16_t* amostras, std::size_t quantas,
                                 std::uint32_t volume, bool mudo, std::size_t deslocamento) {
  if (mudo) volume = 0;
  if (volume > kVolumeMaximo) {
    volume = kVolumeMaximo;
    ++medida_.escalas_limitadas;
  }
  if (amostras == nullptr) return;
  for (std::size_t k = 0; k < quantas; ++k) {
    const std::size_t alvo = deslocamento + k;
    if (alvo >= kAmostrasPorBlocoMax) {
      // O que nao cabe no bloco NAO se perde em silencio: fecha-se o bloco e a
      // amostra seguinte comeca o proximo, com o deslocamento a zero.
      FecharBloco();
      MisturarNoBloco(amostras + k, quantas - k, volume, mudo, 0);
      return;
    }
    while (acumulador_.size() <= alvo) acumulador_.push_back(0);
    const std::int32_t bruto = amostras[k];
    // A SOMA ACONTECE AQUI, no MESMO instante do bloco: e o que faz de duas
    // vozes sobrepostas uma soma, e nao duas listas encostadas. Sem isto, o pico
    // de um bloco nunca passaria do pico de uma voz, e a saturacao seria
    // invisivel.
    acumulador_[alvo] += Escalar(amostras[k], volume);
    ++medida_.amostras_recebidas;
    if (bruto != 0) ++medida_.amostras_nao_nulas;
  }
}

void Misturador::FecharBloco() {
  if (acumulador_.empty()) return;
  for (const std::int32_t v : acumulador_) {
    std::int32_t s = v;
    if (s > kLimitePcm16) {
      s = kLimitePcm16;
      ++medida_.saturacoes;
    } else if (s < -kLimitePcm16) {
      s = -kLimitePcm16;
      ++medida_.saturacoes;
    }
    const std::int32_t abs_v = (s < 0) ? -s : s;
    if (abs_v > medida_.pico) medida_.pico = abs_v;
  }
  ++medida_.blocos;
  acumulador_.clear();
}

void Misturador::Repor() {
  acumulador_.clear();
  medida_ = Medida{};
}

}  // namespace zb2::audio
