#ifndef ZB2_CORE_AUDIO_MISTURADOR_H
#define ZB2_CORE_AUDIO_MISTURADOR_H

// O misturador da etapa 5. Ele NAO toca som: ele CONTA.
//
// PORQUE ASSIM, e nao com uma saida de audio a serio: a etapa 5 tem um criterio
// que se mede -- "o misturador reporta amostras nao nulas" (PLAN.md). Uma saida
// de audio de verdade acrescenta um relogio do hospede, um dispositivo, e uma
// fila de blocos a um emulador cujo determinismo (P4) e a unica razao de as
// medicoes serem comparaveis. O que a etapa precisa do misturador e o NUMERO:
// quantas amostras entraram, quantas eram diferentes de zero, qual o pico, e
// quantas vezes saturou.
//
// P4 -- O TEMPO NAO E LIDO DE LADO NENHUM. Nao ha relogio aqui, nem thread, nem
// `time()`: quem decide quantas amostras passaram e o LACO, que chama `Avancar`
// com um numero INJECTADO. Duas corridas com os mesmos numeros dao o mesmo
// resultado, byte a byte.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace zb2::audio {

// AEE_MAX_VOLUME, transcrito de `platform/media/inc/AEEISound.h`:
//   #define AEE_MAX_VOLUME            100  // Volume from 0 (silence) to AEE_MAX_VOLUME
// O mesmo valor que `MM_PARM_VOLUME` documenta em `AEEIMedia.h`
// ("p1 = volume (0 - AEE_MAX_VOLUME)").
constexpr std::uint32_t kVolumeMaximo = 100;

// Abaixo disto uma soma de vozes passa do limite de 16 bits com sinal e tem de
// SATURAR. O numero 32767 nao vem do SDK: e o limite do formato que o `pData`
// de um `MMD_BUFFER` contem (PCM 16 bits com sinal), e por isso esta escrito
// aqui e nao deduzido de uma constante magica.
constexpr std::int32_t kLimitePcm16 = 32767;

class Misturador {
 public:
  // O que o misturador conta. Tudo em `uint64` porque a contagem e para ser
  // lida ao fim de uma corrida inteira, e nao por bloco.
  struct Medida {
    std::uint64_t amostras_recebidas = 0;
    std::uint64_t amostras_nao_nulas = 0;
    std::uint64_t saturacoes = 0;
    std::uint64_t escalas_limitadas = 0;  // volume acima de kVolumeMaximo, travado
    std::uint64_t blocos = 0;
    std::int32_t pico = 0;  // maior |amostra| JA MISTURADA num bloco fechado
  };

  // Soma `quantas` amostras PCM de 16 bits com sinal ao bloco em curso.
  //
  // `volume` segue a convencao do SDK (0 a AEE_MAX_VOLUME = 100). Um volume
  // acima disso NAO e aceite em silencio: e travado em 100 e CONTADO em
  // `escalas_limitadas`. `mudo` aplica MM_PARM_MUTE (1 = mudo, 0 = normal): as
  // amostras ZERAM, e portanto nao contam para `amostras_nao_nulas` -- que e o
  // que o criterio da etapa mede.
  void Misturar(const std::int16_t* amostras, std::size_t quantas, std::uint32_t volume,
                bool mudo);

  // Soma amostras numa POSICAO conhecida do bloco em curso. E o que permite
  // duas vozes sobrepostas: cada uma escreve no seu deslocamento, e o bloco soma
  // as duas antes de medir o pico. Sem isto, um misturador so somaria vozes que
  // comecassem no mesmo instante.
  void MisturarNoBloco(const std::int16_t* amostras, std::size_t quantas, std::uint32_t volume,
                       bool mudo, std::size_t deslocamento);

  // Fecha o bloco: conta-o, calcula o pico da soma, e limpa o acumulador.
  //
  // O bloco e o que um misturador a serio entregaria de uma vez ao dispositivo.
  // Aqui ele existe para o PICO ser uma medida de SOMA de vozes, e nao de uma
  // chamada isolada: sem ele, duas vozes que tocam juntas nunca mostrariam o
  // valor somado, e a saturacao seria invisivel.
  void FecharBloco();

  // Quantas amostras do bloco em curso ja foram pedidas. Um bloco vazio nao
  // pode ser fechado sem consequencia: fechar dois blocos vazios conta dois.
  std::size_t AmostrasNoBloco() const { return acumulador_.size(); }

  const Medida& MedidaAcumulada() const { return medida_; }
  void Repor();

  // Um bloco maior do que isto nao tem uso: 4096 amostras a 22050 Hz sao 186 ms
  // de som, e o laco de um jogo entrega quadros, nao segundos. O limite existe
  // para uma chamada com um numero absurdo nao alocar um vector do tamanho dele
  // -- foi um laco sem limite que ja custou 900 segundos num titulo. E publico
  // porque quem avanca o tempo por blocos precisa de saber o tamanho deles.
  static constexpr std::size_t kAmostrasPorBlocoMax = 4096;

 private:

  std::vector<std::int32_t> acumulador_;
  Medida medida_;
};

}  // namespace zb2::audio

#endif  // ZB2_CORE_AUDIO_MISTURADOR_H
