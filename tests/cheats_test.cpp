#include <gtest/gtest.h>

#include <string>

#include "core/brew/cheats.h"
#include "core/memoria/memoria.h"
#include "core/tempo/tempo.h"
#include "core/traco/traco.h"

using zb2::DestinoMemoria;
using zb2::Memoria;
using zb2::Tempo;
using zb2::Traco;
using zb2::brew::Cheats;

namespace {

struct Bancada {
  Tempo tempo;
  Traco traco{"cheats", &tempo};
  DestinoMemoria dm;
  Memoria mem{&traco};
  Cheats cheats{mem, traco};

  Bancada() {
    traco.JuntarDestino(&dm);
    mem.EscritorUnico("cpu");
  }

  std::size_t Aplicados() const { return dm.QuantosComNome("CHEAT_APLICADO"); }
};

const char* kDoisCheats = R"({"cheats": [
  {"nome": "bandeira", "titulo": "a3d", "fase": "start",
   "escreve": [{"endereco": "0x1000", "u32": 1}]},
  {"nome": "codigo", "pc": "0x58c",
   "escreve": [{"endereco": "0x2000", "u8": 255}, {"endereco": "0x2004", "u16": 4660}]}
]})";

TEST(Cheats, FicheiroValidoCarregaCheatsComCampos) {
  Bancada b;
  ASSERT_TRUE(b.cheats.LerConteudo(kDoisCheats));
  EXPECT_EQ(b.cheats.Quantos(), 2u);
}

TEST(Cheats, EntradasInvalidasSaoIgnoradasSemDerrubarAsValidas) {
  Bancada b;
  ASSERT_TRUE(b.cheats.LerConteudo(R"({"cheats": [
    {"nome": "", "fase": "start", "escreve": [{"endereco": "0x1000", "u32": 1}]},
    {"nome": "fase-ruim", "fase": "meio", "escreve": [{"endereco": "0x1000", "u32": 1}]},
    {"nome": "sem-escritas", "fase": "start", "escreve": []},
    {"nome": "tam-ruim", "fase": "start", "escreve": [{"endereco": "0x1000", "u64": 1}]},
    {"nome": "ok", "fase": "start", "escreve": [{"endereco": "0x1000", "u32": 7}]}
  ]})"));
  EXPECT_EQ(b.cheats.Quantos(), 1u);
}

TEST(Cheats, NaFaseAplicaUmaVezETraca) {
  Bancada b;
  ASSERT_TRUE(b.cheats.LerConteudo(kDoisCheats));
  b.cheats.ReporPorTitulo("a3d");
  EXPECT_EQ(b.cheats.NaFase("start", 0), 1u);
  EXPECT_EQ(b.mem.Ler32(0x1000), 1u);
  EXPECT_EQ(b.Aplicados(), 1u);
  // Dispara uma vez: a segunda chamada nao escreve nem traca.
  EXPECT_EQ(b.cheats.NaFase("start", 0), 0u);
  EXPECT_EQ(b.Aplicados(), 1u);
  // Fase errada nao dispara.
  EXPECT_EQ(b.cheats.NaFase("carga", 0), 0u);
}

TEST(Cheats, TituloFiltra) {
  Bancada b;
  ASSERT_TRUE(b.cheats.LerConteudo(kDoisCheats));
  b.cheats.ReporPorTitulo("peggle");
  EXPECT_EQ(b.cheats.NaFase("start", 0), 0u);
  EXPECT_EQ(b.mem.Ler32(0x1000), 0u);
  EXPECT_EQ(b.Aplicados(), 0u);
}

TEST(Cheats, PcDisparaNoPassoUmaVez) {
  Bancada b;
  ASSERT_TRUE(b.cheats.LerConteudo(kDoisCheats));
  b.cheats.ReporPorTitulo("a3d");
  b.cheats.NoPasso(0x500);
  EXPECT_EQ(b.mem.Ler8(0x2000), 0u);
  b.cheats.NoPasso(0x58c);
  EXPECT_EQ(b.mem.Ler8(0x2000), 255u);
  EXPECT_EQ(b.mem.Ler16(0x2004), 4660u);
  EXPECT_EQ(b.Aplicados(), 1u);
  b.cheats.NoPasso(0x58c);
  EXPECT_EQ(b.Aplicados(), 1u);
}

TEST(Cheats, QuadroDisparaQuandoAlcancado) {
  Bancada b;
  ASSERT_TRUE(b.cheats.LerConteudo(R"({"cheats": [
    {"nome": "tardio", "fase": "quadros", "quadro": 10,
     "escreve": [{"endereco": "0x3000", "u32": 9}]}
  ]})"));
  b.cheats.ReporPorTitulo("");
  EXPECT_EQ(b.cheats.NaFase("quadros", 9), 0u);
  EXPECT_EQ(b.cheats.NaFase("quadros", 10), 1u);
  EXPECT_EQ(b.mem.Ler32(0x3000), 9u);
}

}  // namespace
