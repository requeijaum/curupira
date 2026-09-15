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

#include "core/brew/despacho.h"
#include "core/cpu/arm_interpreter.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"
#include "tools/ajudantes_slots.inc"
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



// ===========================================================================
// 4. O DESPACHO (a frente "corte"): `saidas > 200` contava TODAS as saidas da
//    fase, e nao RECUSAS SEGUIDAS (PLAN.md, "A ordem a seguir", item 1).
//
//    O `Despacho::Correr` tinha um contador `saidas` que crescia a CADA saida
//    servida e a cada recusa, e a fase morria quando ele passava de 200.
//    MEDIDO na corrida de referencia (`corrida_hid_q.json`): fases mortas em
//    `parou_em_slot_nao_implementado` com `recusadas = 0` e so centenas de
//    passos -- o jogo fazia 200 chamadas LEGITIMAS e a proxima instrucao
//    matava a fase. A correccao conta as recusas SEGUIDAS.
//
//    O primeiro teste corre 260 saidas legitimas (`strlen`, cujo endereco e
//    LIDO DA TABELA que o despacho instalou -- a cablagem, e nao um id
//    interno), UMA recusa (`IShell::slot30`, o ramo generico), e mais uma
//    saida legitima. Com o detector antigo a fase morre aos ~200 despachos;
//    com recusas seguidas, a sequencia nunca passa de 1 e a fase RETORNA.
//
//    O segundo teste prova o contra-lado: um ciclo de 260 recusas SEGUIDAS
//    tem de continuar a abortar a fase. O detector nao e arrancado -- e
//    afinado.
// ===========================================================================

// O ficheiro vive fora de `namespace zb2::brew`: os nomes do despacho entram
// por `using`, como o topo do ficheiro faz para o interpretador.
using zb2::Alocador;
using zb2::brew::Despacho;
using zb2::brew::ResultadoFase;
using zb2::brew::Vfs;
using zb2::brew::kVtableFileObj;
using zb2::Saidas;

// O slot generico do IShell: 2000 + 30, sem ramo proprio no despacho -- cai no
// ramo generico, que REGISTA a falta com o nome e RECUSA.
constexpr std::uint32_t kDespSlotRecusado = 2030;
constexpr std::uint32_t kDespBase = 0x00000000u;
constexpr std::uint32_t kDespPilha = 0x80080000u;
constexpr std::uint32_t kDespHeap = 0x80200000u;
constexpr std::uint32_t kDespHeapTam = 0x00C00000u;
constexpr std::uint32_t kDespTabela = 0x80010000u;
constexpr std::uint32_t kDespSentinela = 0xFFFFFFF0u;
constexpr std::uint32_t kDespTamanhoDoModulo = 0x00100000u;
constexpr std::uint32_t kDespRotina = 0x00004000u;

class BancadaDeDespacho {
 public:
  BancadaDeDespacho() {
    saidas_.base = 0xF0000000u;
    saidas_.passo = 4;
    saidas_.quantos = 100000;
    saidas_.ativa = true;
    cpu_.ConfigurarSaidas(saidas_);
    al_ = new Alocador(mem_, kDespHeap, kDespHeapTam, nullptr);
    despacho_ = new Despacho(mem_, traco_, *al_, vfs_);
    despacho_->DefinirVtableBitmap(saidas_);
    despacho_->DefinirVtableFicheiro(saidas_.Endereco(kVtableFileObj));
    despacho_->InstalarAjudantes(saidas_, kDespTabela);
    despacho_->DefinirFaixaDoModulo(kDespBase, kDespTamanhoDoModulo);
  }
  ~BancadaDeDespacho() {
    delete despacho_;
    delete al_;
  }

  // O ENDERECO DO `strlen` LIDO DA TABELA que o despacho instalou -- a ordem
  // do guest a serio (`AEEHelperFuncs` -> endereco de saida). Nao e um id
  // interno escrito a mao: se a cablagem divergir da tabela, e o teste que
  // falha.
  std::uint32_t SaidaDoStrlen() const {
    // `kAjudante_strlen` JA E O OFFSET EM BYTES (`tools/ajudantes_slots.inc`,
    // gerado do `AEEStdLib.h`: strlen = 0x014, strchr = 0x018, ...). Multiplicar
    // por 4 lia QUATRO vezes a frente na tabela -- um endereco valido da mesma
    // familia, e por isso passava por acidente. Medido pela frente fmt.
    return mem_.Ler32(kDespTabela + brew_ajudantes::kAjudante_strlen);
  }

  // 260 saidas LEGITIMAS, uma RECUSA no meio, e mais uma saida legitima.
  // Termina devolvendo o controlo (`bx r7`, r7 = sentinela -> "retornou").
  //   4000  e1a0e00f  mov lr,pc    ; lr = 0x4008
  //   4004  e12fff18  bx r8        ; saida legitima (r8 = SaidaDoStrlen)
  //   4008  e2555001  subs r5,r5,#1
  //   400c  1afffffb  bne 4000
  //   4010  e1a0e00f  mov lr,pc    ; lr = 0x4018
  //   4014  e12fff19  bx r9        ; RECUSA (r9 = endereco do slot 2030)
  //   4018  e1a0e00f  mov lr,pc    ; lr = 0x4020
  //   401c  e12fff18  bx r8        ; outra saida legitima, a seguir `a recusa
  //   4020  e12fff17  bx r7        ; retorna
  void MontarMuitasLegitimasComUmaRecusa(std::uint32_t quantas) {
    mem_.Escrever32(kDespRotina + 0x00, 0xe1a0e00fu);
    mem_.Escrever32(kDespRotina + 0x04, 0xe12fff18u);
    mem_.Escrever32(kDespRotina + 0x08, 0xe2555001u);
    mem_.Escrever32(kDespRotina + 0x0c, 0x1afffffb);
    mem_.Escrever32(kDespRotina + 0x10, 0xe1a0e00fu);
    mem_.Escrever32(kDespRotina + 0x14, 0xe12fff19u);
    mem_.Escrever32(kDespRotina + 0x18, 0xe1a0e00fu);
    mem_.Escrever32(kDespRotina + 0x1c, 0xe12fff18u);
    mem_.Escrever32(kDespRotina + 0x20, 0xe12fff17u);
    cpu_.Repor(kDespBase, kDespPilha);
    cpu_.Set(zb2::kPC, kDespRotina);
    cpu_.Set(zb2::kLR, kDespSentinela);
    cpu_.Set(zb2::kR0, 0);  // argumento do strlen: a memoria esparsa responde NUL
    cpu_.Set(zb2::kR5, quantas);
    cpu_.Set(zb2::kR7, kDespSentinela);
    cpu_.Set(zb2::kR8, SaidaDoStrlen());
    cpu_.Set(zb2::kR9, saidas_.Endereco(kDespSlotRecusado));
  }

  // 260 RECUSAS SEGUIDAS. O detector tem de continuar a parar isto.
  //   4400  e1a0e00f  mov lr,pc    ; lr = 0x4408
  //   4404  e12fff19  bx r9        ; RECUSA
  //   4408  e2555001  subs r5,r5,#1
  //   440c  1afffffb  bne 4400
  //   4410  e12fff17  bx r7
  void MontarCicloDeRecusas(std::uint32_t quantas) {
    mem_.Escrever32(kDespRotina + 0x400, 0xe1a0e00fu);
    mem_.Escrever32(kDespRotina + 0x404, 0xe12fff19u);
    mem_.Escrever32(kDespRotina + 0x408, 0xe2555001u);
    mem_.Escrever32(kDespRotina + 0x40c, 0x1afffffb);
    mem_.Escrever32(kDespRotina + 0x410, 0xe12fff17u);
    cpu_.Repor(kDespBase, kDespPilha);
    cpu_.Set(zb2::kPC, kDespRotina + 0x400);
    cpu_.Set(zb2::kLR, kDespSentinela);
    cpu_.Set(zb2::kR5, quantas);
    cpu_.Set(zb2::kR7, kDespSentinela);
    cpu_.Set(zb2::kR9, saidas_.Endereco(kDespSlotRecusado));
  }

  ResultadoFase Correr(std::uint64_t limite) { return despacho_->Correr(cpu_, limite, 0); }
  std::size_t Faltas(const std::string& nome) const {
    const auto& f = traco_.ContagemFaltas();
    const auto it = f.find(nome);
    return it == f.end() ? 0 : static_cast<std::size_t>(it->second);
  }

 private:
  Memoria mem_;
  Traco traco_{"despacho_recusas"};
  Vfs vfs_;
  Alocador* al_ = nullptr;
  Despacho* despacho_ = nullptr;
  Saidas saidas_;
  ArmInterpreter cpu_{mem_, &traco_};
};

// 4a. O VERMELHO (antes da correccao): o `saidas > 200` conta TODAS as saidas,
//     e a fase morre aos ~200 despachos -- no meio das 260 legitimas. Depois da
//     correccao, a fase passa o ciclo, atende a recusa, atende mais uma saida e
//     RETORNA.
TEST(RecusasDoDespacho, MuitasSaidasLegitimasMaisUmaRecusaNaoMatamAFase) {
  BancadaDeDespacho b;
  b.MontarMuitasLegitimasComUmaRecusa(260);

  const ResultadoFase r = b.Correr(200000);

  EXPECT_NE(r.motivo, "parou_em_slot_nao_implementado")
      << "261 saidas (260 legitimas + 1 recusa) nao sao um jogo preso: "
         "o detector conta recusas SEGUIDAS, e a sequencia aqui nunca passa de 1";
  EXPECT_EQ(r.motivo, "retornou");
  EXPECT_GE(r.passos, static_cast<std::uint64_t>(260u * 4u))
      << "a fase correu o ciclo inteiro antes de retornar";
  EXPECT_EQ(b.Faltas("IShell::slot30"), 1u)
      << "a recusa foi registada com o nome -- o teste le a TABELA, nao um id";
}

// 4b. O contra-lado: um ciclo de recusas SEGUIDAS continua a abortar a fase.
TEST(RecusasDoDespacho, UmCicloDeRecusasSeguidasContinuaAPararAFase) {
  BancadaDeDespacho b;
  b.MontarCicloDeRecusas(260);

  const ResultadoFase r = b.Correr(200000);

  EXPECT_EQ(r.motivo, "parou_em_slot_nao_implementado")
      << "201 recusas seguidas sao um ciclo preso, e a fase tem de parar";
  EXPECT_LT(r.passos, static_cast<std::uint64_t>(201u * 4u + 8u))
      << "parou na sequencia de recusas, e nao no orcamento";
}

}  // namespace
