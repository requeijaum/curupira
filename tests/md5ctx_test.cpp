#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>

#include "core/brew/ajudantes.h"
#include "core/brew/despacho.h"
#include "core/brew/interface.h"
#include "core/brew/md5ctx.h"
#include "core/brew/vfs.h"
#include "core/cpu/arm_interpreter.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"
#include "tools/brew_slots.inc"
#include "tools/clsids.inc"

namespace zb2::brew {
namespace {

constexpr std::uint32_t kHeap = 0x80200000u;
constexpr std::uint32_t kHeapTam = 0x00c00000u;
constexpr std::uint32_t kPilha = 0x80080000u;
constexpr std::uint32_t kPpObj = 0x80091000u;
constexpr std::uint32_t kCtx = 0x80210000u;
constexpr std::uint32_t kDados = 0x80211000u;
constexpr std::uint32_t kDigest = 0x80212000u;
constexpr std::uint32_t kComprimento = 0x80213000u;
constexpr std::uint32_t kSentinela = 0xfffffff0u;

class BancadaMd5 {
 public:
  BancadaMd5() : cpu_(mem_, &traco_), al_(mem_, kHeap, kHeapTam, nullptr), despacho_(mem_, traco_, al_, vfs_) {
    saidas_.base = 0xf0000000u;
    saidas_.passo = 4;
    saidas_.quantos = 100000;
    saidas_.ativa = true;
    cpu_.ConfigurarSaidas(saidas_);
    despacho_.DefinirVtableBitmap(saidas_);
    despacho_.DefinirVtableFicheiro(saidas_.Endereco(kVtableFileObj));
    despacho_.InstalarAjudantes(saidas_, 0x80010000u);
    despacho_.DefinirFaixaDoModulo(0, 0x00100000u);
    cpu_.Repor(0, kPilha);
  }

  void Chamar(std::uint32_t indice, std::uint32_t r0, std::uint32_t r1 = 0,
              std::uint32_t r2 = 0, std::uint32_t r3 = 0, std::uint32_t pilha = 0) {
    cpu_.Set(kR0, r0);
    cpu_.Set(kR1, r1);
    cpu_.Set(kR2, r2);
    cpu_.Set(kR3, r3);
    mem_.Escrever32(kPilha, pilha);
    cpu_.Set(kLR, kSentinela);
    cpu_.Set(kPC, saidas_.Endereco(indice));
    const ResultadoFase fase = despacho_.Correr(cpu_, 1000, kPpObj);
    EXPECT_EQ(fase.motivo, "retornou");
  }

  Memoria& M() { return mem_; }
  ArmInterpreter& Cpu() { return cpu_; }
  const Saidas& S() const { return saidas_; }

 private:
  Memoria mem_;
  Traco traco_{"teste_md5ctx", nullptr};
  Vfs vfs_;
  ArmInterpreter cpu_;
  Alocador al_;
  Despacho despacho_;
  Saidas saidas_;
};

TEST(Md5Ctx, AbiArmEResumoAbcPelaVtable) {
  BancadaMd5 b;

  // IShell::CreateInstance(this, CLSID, &po): object and vtable are real.
  b.M().Escrever32(kPpObj, 0xdeadbeefu);
  b.Chamar(kBaseDoShell + brew_slots::kShell_CreateInstance, kObjShell,
           brew_clsids::kClsid_MD5Ctx, kPpObj);
  ASSERT_EQ(b.Cpu().Get(kR0), kAeeSuccess);
  const std::uint32_t po = b.M().Ler32(kPpObj);
  ASSERT_EQ(po, kObjetoMd5Ctx);
  const std::uint32_t vt = b.M().Ler32(po);
  ASSERT_EQ(vt, b.S().Endereco(kVtableMd5Ctx));
  EXPECT_EQ(b.M().Ler32(vt + brew_slots::kHashCtx_Init * 4),
            b.S().Endereco(kVtableMd5Ctx + brew_slots::kHashCtx_Init));
  EXPECT_EQ(b.M().Ler32(vt + brew_slots::kHashCtx_Update * 4),
            b.S().Endereco(kVtableMd5Ctx + brew_slots::kHashCtx_Update));
  EXPECT_EQ(b.M().Ler32(vt + brew_slots::kHashCtx_Final * 4),
            b.S().Endereco(kVtableMd5Ctx + brew_slots::kHashCtx_Final));

  // ARM AAPCS: Init r1/r2; Update data r3, len at [sp]; Final digest r3, &len at [sp].
  for (std::uint32_t i = 0; i < kMd5CtxBytes; ++i) b.M().Escrever8(kCtx + i, 0xa5);
  b.Chamar(kVtableMd5Ctx + brew_slots::kHashCtx_Init, po, kCtx, kMd5CtxBytes);
  b.M().Escrever8(kDados + 0, 'a');
  b.Chamar(kVtableMd5Ctx + brew_slots::kHashCtx_Update, po, kCtx, kMd5CtxBytes, kDados, 1);
  b.M().Escrever8(kDados + 0, 'b');
  b.M().Escrever8(kDados + 1, 'c');
  b.Chamar(kVtableMd5Ctx + brew_slots::kHashCtx_Update, po, kCtx, kMd5CtxBytes, kDados, 2);
  b.M().Escrever32(kComprimento, kMd5DigestBytes);
  b.Chamar(kVtableMd5Ctx + brew_slots::kHashCtx_Final, po, kCtx, kMd5CtxBytes, kDigest,
           kComprimento);
  EXPECT_EQ(b.Cpu().Get(kR0), kAeeSuccess);
  EXPECT_EQ(b.M().Ler32(kComprimento), kMd5DigestBytes);
  const std::array<std::uint8_t, 16> esperado = {0x90, 0x01, 0x50, 0x98, 0x3c, 0xd2,
                                                   0x4f, 0xb0, 0xd6, 0x96, 0x3f, 0x7d,
                                                   0x28, 0xe1, 0x7f, 0x72};
  for (std::uint32_t i = 0; i < esperado.size(); ++i) EXPECT_EQ(b.M().Ler8(kDigest + i), esperado[i]);
}

TEST(Md5Ctx, AtualizacaoDeBlocoCheioMantemOComprimentoOriginal) {
  BancadaMd5 b;
  b.Chamar(kBaseDoShell + brew_slots::kShell_CreateInstance, kObjShell,
           brew_clsids::kClsid_MD5Ctx, kPpObj);
  const std::uint32_t po = b.M().Ler32(kPpObj);
  b.Chamar(kVtableMd5Ctx + brew_slots::kHashCtx_Init, po, kCtx, kMd5CtxBytes);
  for (std::uint32_t i = 0; i < 64; ++i) b.M().Escrever8(kDados + i, 'a');
  b.Chamar(kVtableMd5Ctx + brew_slots::kHashCtx_Update, po, kCtx, kMd5CtxBytes, kDados, 64);
  b.M().Escrever32(kComprimento, kMd5DigestBytes);
  b.Chamar(kVtableMd5Ctx + brew_slots::kHashCtx_Final, po, kCtx, kMd5CtxBytes, kDigest,
           kComprimento);
  const std::array<std::uint8_t, 16> esperado = {0x01, 0x48, 0x42, 0xd4, 0x80, 0xb5,
                                                   0x71, 0x49, 0x5a, 0x4a, 0x03, 0x63,
                                                   0x79, 0x3f, 0x73, 0x67};
  for (std::uint32_t i = 0; i < esperado.size(); ++i) EXPECT_EQ(b.M().Ler8(kDigest + i), esperado[i]);
}

TEST(Md5Ctx, ContextoCurtoFalhaComoSDKEDigestPequenoPede16) {
  BancadaMd5 b;
  b.Chamar(kBaseDoShell + brew_slots::kShell_CreateInstance, kObjShell,
           brew_clsids::kClsid_MD5Ctx, kPpObj);
  const std::uint32_t po = b.M().Ler32(kPpObj);
  b.M().Escrever32(kPpObj, 0xdeadbeefu);
  b.Chamar(kVtableMd5Ctx + brew_slots::kHashCtx_QueryInterface, po, 0x0102cb09u, kPpObj);
  EXPECT_EQ(b.Cpu().Get(kR0), kAeeSuccess);
  EXPECT_EQ(b.M().Ler32(kPpObj), po);
  EXPECT_EQ(b.M().Ler32(po + 4), 2u);
  b.Chamar(kVtableMd5Ctx + brew_slots::kHashCtx_QueryInterface, po, 0xdeadbeefu, kPpObj);
  EXPECT_EQ(b.Cpu().Get(kR0), kAeeClassNotSupported);
  EXPECT_EQ(b.M().Ler32(kPpObj), 0u);

  for (std::uint32_t i = 0; i < 87; ++i) b.M().Escrever8(kCtx + i, 0xa5);
  b.Chamar(kVtableMd5Ctx + brew_slots::kHashCtx_Init, po, kCtx, 87);
  for (std::uint32_t i = 0; i < 87; ++i) EXPECT_EQ(b.M().Ler8(kCtx + i), 0xa5);
  b.M().Escrever32(kComprimento, 16);
  b.Chamar(kVtableMd5Ctx + brew_slots::kHashCtx_Final, po, kCtx, 87, kDigest, kComprimento);
  EXPECT_EQ(b.Cpu().Get(kR0), kAeeSecHashInvalidCtx);

  b.Chamar(kVtableMd5Ctx + brew_slots::kHashCtx_Init, po, kCtx, kMd5CtxBytes);
  b.M().Escrever32(kComprimento, 15);
  b.M().Escrever8(kDigest, 0xa5);
  b.Chamar(kVtableMd5Ctx + brew_slots::kHashCtx_Final, po, kCtx, kMd5CtxBytes, kDigest,
           kComprimento);
  EXPECT_EQ(b.Cpu().Get(kR0), kAeeSecHashMoreData);
  EXPECT_EQ(b.M().Ler32(kComprimento), kMd5DigestBytes);
  EXPECT_EQ(b.M().Ler8(kDigest), 0xa5);

  b.Chamar(kVtableMd5Ctx + brew_slots::kHashCtx_SetKey, po, kCtx, kMd5CtxBytes, kDados, 1);
  EXPECT_EQ(b.Cpu().Get(kR0), kAeeSecCryptInvalidKey);
}

}  // namespace
}  // namespace zb2::brew
