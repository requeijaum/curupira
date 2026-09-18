#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "core/brew/ajudantes.h"
#include "core/brew/despacho.h"
#include "core/brew/interface.h"
#include "core/brew/menuctl.h"
#include "core/brew/vfs.h"
#include "core/cpu/arm_interpreter.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"

namespace zb2::brew {
namespace {

constexpr std::uint32_t kHeap = 0x80200000u;
constexpr std::uint32_t kHeapTam = 0x00c00000u;
constexpr std::uint32_t kShell = 0x80020000u;
constexpr std::uint32_t kTabela = 0x80010000u;
constexpr std::uint32_t kPilha = 0x80080000u;
constexpr std::uint32_t kArgs = 0x80090000u;
constexpr std::uint32_t kSentinela = 0xfffffff0u;

class BancadaMenu {
 public:
  BancadaMenu() : al_(mem_, kHeap, kHeapTam, nullptr), despacho_(mem_, traco_, al_, vfs_), cpu_(mem_, &traco_) {
    saidas_.base = 0xf0000000u;
    saidas_.passo = 4;
    saidas_.quantos = 100000;
    saidas_.ativa = true;
    cpu_.ConfigurarSaidas(saidas_);
    despacho_.DefinirVtableBitmap(saidas_);
    despacho_.DefinirVtableFicheiro(saidas_.Endereco(kVtableFileObj));
    despacho_.InstalarAjudantes(saidas_, kTabela);
    despacho_.DefinirFaixaDoModulo(0, 0x00100000u);
    ConstruirObjeto(mem_, saidas_, kShell, saidas_.Endereco(kVtableShell), kSlotsPorVtable, kBaseDoShell);
    cpu_.Repor(0, kPilha);
  }

  std::uint32_t ChamarIndice(std::uint32_t indice, std::uint32_t r0, std::uint32_t r1 = 0,
                             std::uint32_t r2 = 0, std::uint32_t r3 = 0) {
    cpu_.Set(kR0, r0); cpu_.Set(kR1, r1); cpu_.Set(kR2, r2); cpu_.Set(kR3, r3);
    cpu_.Set(kSP, kArgs); cpu_.Set(kLR, kSentinela); cpu_.Set(kPC, saidas_.Endereco(indice));
    (void)despacho_.Correr(cpu_, 1000, kArgs);
    return cpu_.Get(kR0);
  }

  std::uint32_t Criar() {
    constexpr std::uint32_t out = kArgs + 0x80;
    mem_.Escrever32(out, 0);
    EXPECT_EQ(ChamarIndice(kBaseDoShell + 2, kShell, kClsidSoftKeyCtl, out), kAeeSuccess);
    return mem_.Ler32(out);
  }

  std::uint32_t ChamarSlot(std::uint32_t objeto, std::uint32_t slot, std::uint32_t r1 = 0,
                           std::uint32_t r2 = 0, std::uint32_t r3 = 0) {
    const std::uint32_t funcao = mem_.Ler32(mem_.Ler32(objeto) + slot * 4u);
    EXPECT_TRUE(saidas_.Contem(funcao, nullptr));
    return ChamarIndice((funcao - saidas_.base) / saidas_.passo, objeto, r1, r2, r3);
  }

  Memoria& M() { return mem_; }
  std::size_t Faltas(const std::string& nome) const {
    const auto it = traco_.ContagemFaltas().find(nome);
    return it == traco_.ContagemFaltas().end() ? 0u : static_cast<std::size_t>(it->second);
  }

 private:
  Memoria mem_;
  Traco traco_{"teste_menu", nullptr};
  Vfs vfs_;
  Alocador al_;
  Despacho despacho_;
  ArmInterpreter cpu_;
  Saidas saidas_;
};

TEST(MenuCtl, FactoryCreatesIndependentSoftKeyControls) {
  BancadaMenu b;
  const std::uint32_t a = b.Criar();
  const std::uint32_t c = b.Criar();
  ASSERT_NE(a, 0u);
  ASSERT_NE(c, 0u);
  EXPECT_NE(a, c);
  EXPECT_EQ(b.M().Ler32(a), b.M().Ler32(c));
  EXPECT_EQ(b.M().Ler32(a + 4u), 1u);
  EXPECT_EQ(b.ChamarSlot(a, 0), 2u);  // AddRef through the vtable
  EXPECT_EQ(b.ChamarSlot(a, 1), 1u);  // Release through the vtable
}

TEST(MenuCtl, RocketwebChainGetsRectAndStoresOnlyValidItems) {
  BancadaMenu b;
  const std::uint32_t menu = b.Criar();
  ASSERT_NE(menu, 0u);
  constexpr std::uint32_t rect = kArgs + 0x100;
  for (std::uint32_t n = 0; n < 8; ++n) b.M().Escrever8(rect + n, 0xff);
  const std::uint32_t untouched = b.ChamarSlot(menu, 7, rect);
  EXPECT_EQ(untouched, menu);  // void: no invented return value
  EXPECT_EQ(b.M().Ler16(rect + 0), 0u);
  EXPECT_EQ(b.M().Ler16(rect + 2), 216u);
  EXPECT_EQ(b.M().Ler16(rect + 4), 320u);
  EXPECT_EQ(b.M().Ler16(rect + 6), 24u);

  constexpr std::uint32_t text = kArgs + 0x140;
  b.M().Escrever16(text + 0, 'O'); b.M().Escrever16(text + 2, 'K'); b.M().Escrever16(text + 4, 0);
  b.M().Escrever32(kArgs, text);
  b.M().Escrever32(kArgs + 4, 0xcafebabeu);
  EXPECT_EQ(b.ChamarSlot(menu, 12, 0, 77, 9), 1u);
  EXPECT_EQ(b.ChamarSlot(menu, 12, 0, 77, 9), 0u);  // duplicate proves first item was stored
  b.M().Escrever32(kArgs, 0);
  EXPECT_EQ(b.ChamarSlot(menu, 12, 0, 77, 10), 0u);
}

TEST(MenuCtl, VoidStateSlotsDoNotInventResultsAndUnknownSlotsRefuse) {
  BancadaMenu b;
  const std::uint32_t menu = b.Criar();
  ASSERT_NE(menu, 0u);
  EXPECT_EQ(b.ChamarSlot(menu, 19, 0), menu);
  constexpr std::uint32_t colors = kArgs + 0x200;
  for (std::uint32_t n = 0; n < 40; ++n) b.M().Escrever8(colors + n, static_cast<std::uint8_t>(n));
  EXPECT_EQ(b.ChamarSlot(menu, 24, colors), menu);
  EXPECT_EQ(b.ChamarSlot(menu, 2), kAeeUnsupported);
  EXPECT_EQ(b.Faltas("IMenuCtl::HandleEvent"), 1u);
}

}  // namespace
}  // namespace zb2::brew
