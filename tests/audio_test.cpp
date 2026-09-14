#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "core/audio/misturador.h"

using zb2::audio::kLimitePcm16;
using zb2::audio::kVolumeMaximo;
using zb2::audio::Misturador;

namespace {

// Uma senoide de inteiros pequenos e determinista: nao ha `rand()` do hospede
// em teste nenhum (P4), e um valor declarado e comparavel entre corridas.
std::vector<std::int16_t> Onda(std::size_t quantas, std::int16_t amplitude) {
  std::vector<std::int16_t> v(quantas);
  for (std::size_t k = 0; k < quantas; ++k) {
    v[k] = ((k % 4) < 2) ? amplitude : static_cast<std::int16_t>(-amplitude);
  }
  return v;
}

TEST(Misturador, ContaAsAmostrasEAsNaoNulas) {
  Misturador m;
  const auto onda = Onda(100, 1000);
  m.Misturar(onda.data(), onda.size(), kVolumeMaximo, false);
  m.FecharBloco();
  EXPECT_EQ(m.MedidaAcumulada().amostras_recebidas, 100u);
  EXPECT_EQ(m.MedidaAcumulada().amostras_nao_nulas, 100u);
  EXPECT_EQ(m.MedidaAcumulada().blocos, 1u);
  EXPECT_EQ(m.MedidaAcumulada().pico, 1000);
}

TEST(Misturador, SilencioNaoContaComoAmostraNaoNula) {
  // O criterio da etapa 5 e "o misturador reporta amostras NAO NULAS". Se zeros
  // contassem, o criterio passaria com um buffer todo a zero -- e um numero que
  // passa por engano e pior do que nenhum.
  Misturador m;
  const std::vector<std::int16_t> zeros(64, 0);
  m.Misturar(zeros.data(), zeros.size(), kVolumeMaximo, false);
  m.FecharBloco();
  EXPECT_EQ(m.MedidaAcumulada().amostras_recebidas, 64u);
  EXPECT_EQ(m.MedidaAcumulada().amostras_nao_nulas, 0u);
  EXPECT_EQ(m.MedidaAcumulada().pico, 0);
}

TEST(Misturador, VolumeMetadeBaixaOPicoAMetade) {
  Misturador m;
  const auto onda = Onda(16, 2000);
  m.Misturar(onda.data(), onda.size(), 50, false);
  m.FecharBloco();
  EXPECT_EQ(m.MedidaAcumulada().pico, 1000);
}

TEST(Misturador, MudoZeraOPicoSemDescartarAContagem) {
  // MUTE (MM_PARM_MUTE) e um pedido ACEITO: as amostras continuam a passar e a
  // ser contadas, mas o que sai e zero. As duas contagens dizem coisas
  // diferentes de proposito.
  Misturador m;
  const auto onda = Onda(32, 3000);
  m.Misturar(onda.data(), onda.size(), kVolumeMaximo, true);
  m.FecharBloco();
  EXPECT_EQ(m.MedidaAcumulada().amostras_recebidas, 32u);
  EXPECT_EQ(m.MedidaAcumulada().amostras_nao_nulas, 32u);
  EXPECT_EQ(m.MedidaAcumulada().pico, 0);
}

TEST(Misturador, VolumeAcimaDoMaximoETravadoEContado) {
  // Travar em silencio seria um valor errado sem sintoma; recusar a voz inteira
  // deixaria o jogo mudo. Trava e CONTA.
  Misturador m;
  const auto onda = Onda(8, 1000);
  m.Misturar(onda.data(), onda.size(), 200, false);
  m.FecharBloco();
  EXPECT_EQ(m.MedidaAcumulada().escalas_limitadas, 1u);
  EXPECT_EQ(m.MedidaAcumulada().pico, 1000);  // o mesmo que volume 100
}

TEST(Misturador, DuasVozesSomamNoMesmoBloco) {
  // O pico e do BLOCO, e nao da chamada: e o que torna a soma de vozes visivel.
  Misturador m;
  // Duas vozes que comecam no MESMO instante do bloco: e a posicao que as soma.
  const auto onda = Onda(16, 500);
  m.MisturarNoBloco(onda.data(), onda.size(), kVolumeMaximo, false, 0);
  m.MisturarNoBloco(onda.data(), onda.size(), kVolumeMaximo, false, 0);
  m.FecharBloco();
  EXPECT_EQ(m.MedidaAcumulada().pico, 1000);
  EXPECT_EQ(m.MedidaAcumulada().blocos, 1u);
  EXPECT_EQ(m.MedidaAcumulada().amostras_recebidas, 32u);
}

TEST(Misturador, SomaQuePassaDe16BitsSaturaEConta) {
  Misturador m;
  const std::vector<std::int16_t> alto(8, kLimitePcm16);
  // Oito amostras no maximo, duas vezes, no mesmo instante do bloco: a soma
  // passa o limite e SATURA -- e a saturacao e CONTADA.
  m.MisturarNoBloco(alto.data(), alto.size(), kVolumeMaximo, false, 0);
  m.MisturarNoBloco(alto.data(), alto.size(), kVolumeMaximo, false, 0);
  m.FecharBloco();
  EXPECT_EQ(m.MedidaAcumulada().pico, kLimitePcm16);
  EXPECT_EQ(m.MedidaAcumulada().saturacoes, 8u);
}

TEST(Misturador, FecharBlocoVazioNaoContaBloco) {
  // GUARDA CONTRA UMA CONTAGEM A MAIS: um bloco vazio nao e um bloco. Sem isto,
  // um laco que fecha o bloco a cada passo inflacionava a contagem sem que
  // nenhuma amostra tivesse passado.
  Misturador m;
  m.FecharBloco();
  m.FecharBloco();
  EXPECT_EQ(m.MedidaAcumulada().blocos, 0u);
  EXPECT_EQ(m.MedidaAcumulada().amostras_recebidas, 0u);
}

TEST(Misturador, BlocoGrandeNaoAlocaMaisDoQueOLimite) {
  // Um pedido maior do que o bloco e DIVIDIDO, nao descartado nem alocado.
  Misturador m;
  const auto onda = Onda(20000, 100);
  m.Misturar(onda.data(), onda.size(), kVolumeMaximo, false);
  m.FecharBloco();
  EXPECT_EQ(m.MedidaAcumulada().amostras_recebidas, 20000u);
  EXPECT_GT(m.MedidaAcumulada().blocos, 1u);
}

TEST(Misturador, ReporLimpaAContagem) {
  Misturador m;
  const auto onda = Onda(4, 100);
  m.Misturar(onda.data(), onda.size(), kVolumeMaximo, false);
  m.FecharBloco();
  m.Repor();
  EXPECT_EQ(m.MedidaAcumulada().amostras_recebidas, 0u);
  EXPECT_EQ(m.AmostrasNoBloco(), 0u);
}

}  // namespace
