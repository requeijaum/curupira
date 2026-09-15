// AS RECUSAS SAO SOMADAS SOBRE AS TRES FASES -- e o teste prova o VERMELHO.
//
// A alegacao que este ficheiro mede: o campo `recusadas` da bateria reportava
// MENOS recusas do que o interpretador contava. Confirmada no codigo
// (`ArmInterpreter::Repor` zera o contador; a bateria lia-o em absoluto depois
// de duas das quatro fases) e confirmada aqui com numeros.
//
// A VERDADE INDEPENDENTE e o TRACO: `ArmInterpreter::Recusar` emite um
// `INSTRUCAO_RECUSADA` por cada recusa e NAO sabe de fases nenhumas. Contar
// esses eventos da o numero certo sem passar pelo contador que esta em causa --
// e por isso o teste nao pode passar por acaso ao copiar o metodo que testa.
#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "core/cpu/arm_interpreter.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"
#include "tools/recusas.h"

using zb2::ArmInterpreter;
using zb2::DestinoMemoria;
using zb2::Memoria;
using zb2::Traco;
using zb2::tools::ContadorDeRecusas;
using zb2::tools::RecusasPorFase;

namespace {

// UMA INSTRUCAO QUE O INTERPRETADOR RECUSA, e a escolha e medida.
//
// A primeira versao usava `0xE7F000F0`, o "permanently undefined" do ARM ARM
// (A3-28). **Este interpretador executa-o como `ldrb` e NAO o recusa** --
// conferido nesta bancada (`familia=ldrb`, `recusadas=0`). Nao serve para
// provar contagem de recusas, e fica DITO aqui porque e um defeito por si so
// (`EhMediaArmv6` nao cobre o espaco indefinido do grupo 011).
//
// `0xE1200070` e o `BKPT #0`, e o proprio interpretador recusa-o com todas as
// letras ("BKPT/HLT: o emulador nao tem depurador",
// `core/cpu/arm_interpreter.cpp:784`). Recusa uma vez por execucao e avanca o
// PC, que e exactamente o que uma fase com N recusas precisa.
constexpr std::uint32_t kRecusada = 0xE1200070u;  // BKPT #0
constexpr std::uint32_t kBase = 0x1000u;
constexpr std::uint32_t kPilha = 0x9000u;
constexpr std::uint32_t kSaida = 0xF0000000u;

// Quantas recusas o TRACO viu -- a conta que nao passa pelo contador da CPU.
std::uint64_t RecusasNoTraco(const DestinoMemoria& dm) {
  std::uint64_t n = 0;
  for (const auto& ev : dm.eventos) {
    if (ev.nome == "INSTRUCAO_RECUSADA") ++n;
  }
  return n;
}

// Uma fase: `quantas` instrucoes recusadas seguidas, corridas ate ao fim.
//
// NAO se usa aqui um salto para a faixa de saida para terminar a fase, e a
// razao e medida nesta mesma bancada: `mov pc, #imm` e `add pc, pc, rX` NAO
// saltam neste interpretador (o `ExecutarArm` escreve `pc + 4` por cima do PC
// que o `DadosProcessados` acabou de escrever). Um terminador que nao termina
// faria o teste medir outra coisa sem o dizer.
void Fase(Memoria& mem, ArmInterpreter& cpu, std::uint32_t inicio, int quantas) {
  for (int i = 0; i < quantas; ++i) {
    mem.Escrever32(inicio + static_cast<std::uint32_t>(i) * 4u, kRecusada);
  }
  cpu.Set(zb2::kPC, inicio);
  cpu.Correr(static_cast<std::uint64_t>(quantas));
}

struct Bancada {
  Traco traco{"recusas"};
  DestinoMemoria dm;
  Memoria mem{&traco};
  ArmInterpreter cpu{mem, &traco};

  Bancada() {
    traco.JuntarDestino(&dm);
    zb2::Saidas s;
    s.base = kSaida;
    s.quantos = 16;
    s.passo = 4;
    s.ativa = true;
    cpu.ConfigurarSaidas(s);
  }
};

// -------------------------------------------------------------------------
// 1. O VERMELHO: o metodo ANTIGO (leitura absoluta depois de duas fases) perde
//    recusas, e perde-as em duas maneiras diferentes.
// -------------------------------------------------------------------------
TEST(RecusasDaBateria, OMetodoAntigoPerdeAsFasesCargaEStart) {
  Bancada b;
  // As quatro fases, com contagens distintas para que nenhuma se confunda com
  // outra por coincidencia de numero.
  constexpr int kCarga = 7;
  constexpr int kCreate = 3;
  constexpr int kStart = 11;
  constexpr int kQuadros = 5;

  b.cpu.Repor(kBase, kPilha);
  Fase(b.mem, b.cpu, kBase, kCarga);
  // O QUE A BATERIA FAZIA: ler o contador em absoluto.
  const std::uint64_t antigo_depois_da_carga = b.cpu.InstruscoesRecusadas();

  b.cpu.Repor(kBase + 0x200u, kPilha);  // o `Repor` do `CreateInstance`
  Fase(b.mem, b.cpu, kBase + 0x200u, kCreate);
  const std::uint64_t antigo_depois_do_create = b.cpu.InstruscoesRecusadas();

  // As duas fases que corriam DEPOIS da ultima leitura.
  Fase(b.mem, b.cpu, kBase + 0x400u, kStart);
  Fase(b.mem, b.cpu, kBase + 0x600u, kQuadros);

  const std::uint64_t verdade = RecusasNoTraco(b.dm);
  EXPECT_EQ(verdade, static_cast<std::uint64_t>(kCarga + kCreate + kStart + kQuadros));

  // 1. O `Repor` apagou a carga: a segunda leitura nao a contem.
  EXPECT_EQ(antigo_depois_da_carga, static_cast<std::uint64_t>(kCarga));
  EXPECT_EQ(antigo_depois_do_create, static_cast<std::uint64_t>(kCreate));
  // 2. E o valor publicado (o ultimo `=`, nao `+=`) era SO o do create.
  EXPECT_LT(antigo_depois_do_create, verdade);
  EXPECT_EQ(verdade - antigo_depois_do_create,
            static_cast<std::uint64_t>(kCarga + kStart + kQuadros));
}

// -------------------------------------------------------------------------
// 2. O VERDE: o contador por delta bate com o traco, e as parcelas somam o
//    total. E o invariante que a bateria verifica antes de escrever o JSON.
// -------------------------------------------------------------------------
TEST(RecusasDaBateria, ODeltaSomaAsQuatroFasesEBateComOTraco) {
  Bancada b;
  constexpr int kCarga = 7;
  constexpr int kCreate = 3;
  constexpr int kStart = 11;
  constexpr int kQuadros = 5;

  ContadorDeRecusas rec;
  RecusasPorFase p;

  b.cpu.Repor(kBase, kPilha);
  rec.Rearmar(b.cpu);
  Fase(b.mem, b.cpu, kBase, kCarga);
  p.carga = rec.Colher(b.cpu);

  b.cpu.Repor(kBase + 0x200u, kPilha);
  rec.Rearmar(b.cpu);
  Fase(b.mem, b.cpu, kBase + 0x200u, kCreate);
  p.create = rec.Colher(b.cpu);

  Fase(b.mem, b.cpu, kBase + 0x400u, kStart);
  p.start = rec.Colher(b.cpu);

  Fase(b.mem, b.cpu, kBase + 0x600u, kQuadros);
  p.quadros = rec.Colher(b.cpu);

  EXPECT_EQ(p.carga, static_cast<std::uint64_t>(kCarga));
  EXPECT_EQ(p.create, static_cast<std::uint64_t>(kCreate));
  EXPECT_EQ(p.start, static_cast<std::uint64_t>(kStart));
  EXPECT_EQ(p.quadros, static_cast<std::uint64_t>(kQuadros));
  EXPECT_EQ(p.Soma(), rec.Total());
  // A PROVA: o total publicado e o que o interpretador contou, sem fase perdida.
  EXPECT_EQ(rec.Total(), RecusasNoTraco(b.dm));
}

// -------------------------------------------------------------------------
// 3. O `Rearmar` ESQUECIDO nao inventa numeros.
//
//    Se alguem acrescentar um `Repor` e esquecer o `Rearmar`, o delta seria
//    `agora - base` com `agora` MENOR do que `base`. Num `uint64` isso daria um
//    numero astronomico -- um total absurdo e pior do que um total curto, porque
//    parece medicao. A guarda toma o valor actual como parcela.
// -------------------------------------------------------------------------
TEST(RecusasDaBateria, UmReporSemRearmarNaoProduzUmNumeroAbsurdo) {
  Bancada b;
  ContadorDeRecusas rec;
  b.cpu.Repor(kBase, kPilha);
  rec.Rearmar(b.cpu);
  Fase(b.mem, b.cpu, kBase, 9);
  EXPECT_EQ(rec.Colher(b.cpu), 9u);

  b.cpu.Repor(kBase + 0x200u, kPilha);  // sem `Rearmar` -- de proposito
  Fase(b.mem, b.cpu, kBase + 0x200u, 2);
  const std::uint64_t parcela = rec.Colher(b.cpu);
  EXPECT_EQ(parcela, 2u);
  EXPECT_EQ(rec.Total(), 11u);
  // Sem a guarda, o total seria 9 + (2 - 9) em `uint64`.
  EXPECT_LT(rec.Total(), 1000u);
}


}  // namespace
