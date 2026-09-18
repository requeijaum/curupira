#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "core/brew/ajudantes.h"
#include "core/brew/despacho.h"
#include "core/brew/inetmgr.h"
#include "core/brew/interface.h"
#include "core/brew/vfs.h"
#include "core/cpu/arm_interpreter.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"

namespace zb2::brew {
namespace {

constexpr std::uint32_t kHeap = 0x80200000u;
constexpr std::uint32_t kHeapTam = 0x00C00000u;
constexpr std::uint32_t kPilha = 0x80080000u;
constexpr std::uint32_t kTabela = 0x80010000u;
constexpr std::uint32_t kPpObj = 0x80091000u;
constexpr std::uint32_t kSentinela = 0xFFFFFFF0u;

class BancadaNet final {
 public:
  BancadaNet() : al_(mem_, kHeap, kHeapTam, nullptr), despacho_(mem_, traco_, al_, vfs_) {
    saidas_.base = 0xF0000000u;
    saidas_.passo = 4;
    saidas_.quantos = 100000;
    saidas_.ativa = true;
    cpu_.ConfigurarSaidas(saidas_);
    despacho_.DefinirVtableBitmap(saidas_);
    despacho_.DefinirVtableFicheiro(saidas_.Endereco(kVtableFileObj));
    despacho_.InstalarAjudantes(saidas_, kTabela);
    despacho_.DefinirFaixaDoModulo(0, 0x00100000u);
    cpu_.Repor(0, kPilha);
  }

  ResultadoFase ChamaSaida(std::uint32_t indice, std::uint32_t r0, std::uint32_t r1 = 0,
                           std::uint32_t r2 = 0, std::uint32_t r3 = 0) {
    cpu_.Set(kR0, r0); cpu_.Set(kR1, r1); cpu_.Set(kR2, r2); cpu_.Set(kR3, r3);
    cpu_.Set(kLR, kSentinela); cpu_.Set(kPC, saidas_.Endereco(indice));
    return despacho_.Correr(cpu_, 1000, kPpObj);
  }

  ResultadoFase ChamaMetodo(std::uint32_t objeto, std::uint32_t slot, std::uint32_t r1 = 0,
                            std::uint32_t r2 = 0, std::uint32_t r3 = 0) {
    const std::uint32_t vt = mem_.Ler32(objeto);
    cpu_.Set(kR0, objeto); cpu_.Set(kR1, r1); cpu_.Set(kR2, r2); cpu_.Set(kR3, r3);
    cpu_.Set(kLR, kSentinela); cpu_.Set(kPC, mem_.Ler32(vt + slot * 4u));
    return despacho_.Correr(cpu_, 1000, kPpObj);
  }

  std::size_t Faltas(const std::string& nome) const {
    const auto it = traco_.ContagemFaltas().find(nome);
    return it == traco_.ContagemFaltas().end() ? 0u : static_cast<std::size_t>(it->second);
  }
  Memoria& M() { return mem_; }
  ArmInterpreter& Cpu() { return cpu_; }
  const Saidas& S() const { return saidas_; }

 private:
  Memoria mem_;
  Traco traco_{"teste_inetmgr", nullptr};
  Vfs vfs_;
  Alocador al_;
  Despacho despacho_;
  Saidas saidas_;
  ArmInterpreter cpu_{mem_, &traco_};
};

TEST(INetMgr, FabricaDoShellEntregaObjetoELiberaPelaVtable) {
  BancadaNet b;
  b.M().Escrever32(kPpObj, 0xDEADBEEFu);

  b.ChamaSaida(kBaseDoShell + 2, kObjShell, kAeeClsidNet, kPpObj);
  const std::uint32_t obj = b.M().Ler32(kPpObj);
  ASSERT_EQ(b.Cpu().Get(kR0), kAeeSuccess);
  ASSERT_EQ(obj, kObjetoNetMgr);
  const std::uint32_t vt = b.M().Ler32(obj);
  ASSERT_EQ(vt, b.S().Endereco(kVtableNetMgr));
  EXPECT_EQ(b.M().Ler32(vt + 0 * 4u), b.S().Endereco(3));
  EXPECT_EQ(b.M().Ler32(vt + 1 * 4u), b.S().Endereco(4));
  EXPECT_EQ(b.M().Ler32(vt + 2 * 4u), b.S().Endereco(kVtableNetMgr + 2));

  // A chamada usa o ponteiro que o guest leu da vtable, nao AtenderNetMgr direto.
  b.ChamaMetodo(obj, 1);
  EXPECT_EQ(b.Cpu().Get(kR0), 0u);
  EXPECT_EQ(b.M().Ler32(obj + 4), 0u);
}

TEST(INetMgr, SlotDoisESetMaskNaoQueryInterfaceESemBackend) {
  BancadaNet b;
  b.ChamaSaida(kBaseDoShell + 2, kObjShell, kAeeClsidNet, kPpObj);
  const std::uint32_t obj = b.M().Ler32(kPpObj);
  constexpr std::uint32_t kSaida = 0x80092000u;
  b.M().Escrever32(kSaida, 0xA5A5A5A5u);

  // Um QI deslocado escreveria em r2. O slot 2 real e SetMask e recusa.
  b.ChamaMetodo(obj, 2, 0x01000001u, kSaida);
  EXPECT_EQ(b.Cpu().Get(kR0), kAeeUnsupported);
  EXPECT_EQ(b.M().Ler32(kSaida), 0xA5A5A5A5u);
  EXPECT_EQ(b.Faltas("INetMgr::SetMask"), 1u);

  const char* const nomes[] = {"GetHostByName", "GetLastError", "OpenSocket", "NetStatus",
                               "GetMyIPAddr", "SetLinger", "OnEvent", "SetOpt", "GetOpt"};
  for (std::uint32_t slot = 3; slot < kSlotsNetMgr; ++slot) {
    b.ChamaMetodo(obj, slot, 0x11111111u, kSaida, 0x33333333u);
    EXPECT_EQ(b.Cpu().Get(kR0), kAeeUnsupported) << NomeDoSlotNetMgr(slot);
    EXPECT_EQ(b.Faltas(std::string("INetMgr::") + nomes[slot - 3]), 1u);
  }
}

}  // namespace
}  // namespace zb2::brew
