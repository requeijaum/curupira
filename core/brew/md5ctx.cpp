#include "core/brew/md5ctx.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <string>

#include "core/brew/interface.h"
#include "tools/brew_slots.inc"

namespace zb2::brew {
namespace {

constexpr std::uint32_t kIidIqi = 0x01000001u;
constexpr std::uint32_t kIidHashCtx = 0x0102cb09u;

// This is the conventional RFC 1321 MD5_CTX representation.  It exactly fills
// AEECLSID_MD5Ctx_SIZE: state[4] (16), bit count (8), partial block (64).
// The SDK deliberately exposes AEE_MD5_CTX as opaque bytes; keeping all state
// here, rather than in a host map keyed by guest address, permits copied and
// independently allocated guest contexts.
struct Estado {
  std::uint32_t h[4];
  std::uint64_t bits;
  std::uint8_t bloco[64];
};
static_assert(sizeof(Estado) == kMd5CtxBytes);

constexpr std::uint32_t kK[64] = {
    0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu, 0xf57c0fafu, 0x4787c62au,
    0xa8304613u, 0xfd469501u, 0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu,
    0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u, 0xf61e2562u, 0xc040b340u,
    0x265e5a51u, 0xe9b6c7aau, 0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
    0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu, 0xa9e3e905u, 0xfcefa3f8u,
    0x676f02d9u, 0x8d2a4c8au, 0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu,
    0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u, 0x289b7ec6u, 0xeaa127fau,
    0xd4ef3085u, 0x04881d05u, 0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
    0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u, 0x655b59c3u, 0x8f0ccc92u,
    0xffeff47du, 0x85845dd1u, 0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u,
    0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u};
constexpr unsigned kS[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

constexpr std::uint32_t Rodar(std::uint32_t x, unsigned n) { return (x << n) | (x >> (32 - n)); }

bool ContextoValido(std::uint32_t pctx, std::uint32_t tamanho) {
  // nCTXSize is an int in the SDK.  A negative value must not become a giant
  // valid unsigned size after crossing the ARM ABI.
  return pctx != 0 && tamanho >= kMd5CtxBytes && tamanho < 0x80000000u;
}

Estado LerEstado(Memoria& mem, std::uint32_t p) {
  Estado e{};
  for (unsigned i = 0; i < 4; ++i) e.h[i] = mem.Ler32(p + i * 4u);
  e.bits = static_cast<std::uint64_t>(mem.Ler32(p + 16)) |
           (static_cast<std::uint64_t>(mem.Ler32(p + 20)) << 32);
  for (unsigned i = 0; i < 64; ++i) e.bloco[i] = mem.Ler8(p + 24 + i);
  return e;
}

void EscreverEstado(Memoria& mem, std::uint32_t p, const Estado& e) {
  for (unsigned i = 0; i < 4; ++i) mem.Escrever32(p + i * 4u, e.h[i]);
  mem.Escrever32(p + 16, static_cast<std::uint32_t>(e.bits));
  mem.Escrever32(p + 20, static_cast<std::uint32_t>(e.bits >> 32));
  for (unsigned i = 0; i < 64; ++i) mem.Escrever8(p + 24 + i, e.bloco[i]);
}

void Iniciar(Estado* e) {
  std::memset(e, 0, sizeof(*e));
  e->h[0] = 0x67452301u;
  e->h[1] = 0xefcdab89u;
  e->h[2] = 0x98badcfeu;
  e->h[3] = 0x10325476u;
}

void Comprimir(Estado* e, const std::uint8_t bloco[64]) {
  std::uint32_t m[16];
  for (unsigned i = 0; i < 16; ++i) {
    m[i] = static_cast<std::uint32_t>(bloco[i * 4]) |
           (static_cast<std::uint32_t>(bloco[i * 4 + 1]) << 8) |
           (static_cast<std::uint32_t>(bloco[i * 4 + 2]) << 16) |
           (static_cast<std::uint32_t>(bloco[i * 4 + 3]) << 24);
  }
  std::uint32_t a = e->h[0], b = e->h[1], c = e->h[2], d = e->h[3];
  for (unsigned i = 0; i < 64; ++i) {
    std::uint32_t f = 0, g = 0;
    if (i < 16) { f = (b & c) | (~b & d); g = i; }
    else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) & 15u; }
    else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) & 15u; }
    else { f = c ^ (b | ~d); g = (7 * i) & 15u; }
    const std::uint32_t proximo = b + Rodar(a + f + kK[i] + m[g], kS[i]);
    a = d; d = c; c = b; b = proximo;
  }
  e->h[0] += a; e->h[1] += b; e->h[2] += c; e->h[3] += d;
}

void AbsorverByte(Estado* e, std::uint8_t byte) {
  const unsigned pos = static_cast<unsigned>((e->bits >> 3) & 63u);
  e->bloco[pos] = byte;
  e->bits += 8;
  if (pos == 63) Comprimir(e, e->bloco);
}

void Atualizar(Memoria& mem, Estado* e, std::uint32_t dados, std::uint32_t tamanho) {
  for (std::uint32_t i = 0; i < tamanho; ++i) AbsorverByte(e, mem.Ler8(dados + i));
}

void Finalizar(Estado* e, std::uint8_t digest[kMd5DigestBytes]) {
  const std::uint64_t bits_originais = e->bits;
  AbsorverByte(e, 0x80);
  while (((e->bits >> 3) & 63u) != 56u) AbsorverByte(e, 0);
  for (unsigned i = 0; i < 8; ++i) AbsorverByte(e, static_cast<std::uint8_t>(bits_originais >> (8 * i)));
  for (unsigned i = 0; i < 4; ++i) {
    digest[i * 4] = static_cast<std::uint8_t>(e->h[i]);
    digest[i * 4 + 1] = static_cast<std::uint8_t>(e->h[i] >> 8);
    digest[i * 4 + 2] = static_cast<std::uint8_t>(e->h[i] >> 16);
    digest[i * 4 + 3] = static_cast<std::uint8_t>(e->h[i] >> 24);
  }
}

}  // namespace

void ConstruirMd5Ctx(Memoria& mem, const Saidas& saidas, Traco& traco) {
  static_assert(brew_slots::kHashCtxSlots == 7, "IHashCtx: IQI + Init/Update/Final/SetKey");
  static_assert(brew_slots::kHashCtx_Init == 3 && brew_slots::kHashCtx_Update == 4 &&
                    brew_slots::kHashCtx_Final == 5 && brew_slots::kHashCtx_SetKey == 6,
                "IHashCtx slot order is SDK ABI");
  ConstruirObjeto(mem, saidas, kObjetoMd5Ctx, saidas.Endereco(kVtableMd5Ctx),
                  brew_slots::kHashCtxSlots, kVtableMd5Ctx);
  bool ok = mem.Ler32(kObjetoMd5Ctx) == saidas.Endereco(kVtableMd5Ctx);
  for (std::uint32_t slot = 2; slot < brew_slots::kHashCtxSlots && ok; ++slot) {
    ok = mem.Ler32(saidas.Endereco(kVtableMd5Ctx) + slot * 4) ==
         saidas.Endereco(kVtableMd5Ctx + slot);
  }
  if (!ok) traco.RegistarFalta(Area::Brew, "md5ctx_cablagem_perdida", "IHashCtx sem vtable cablada");
}

bool AtenderMd5Ctx(ICpu& cpu, std::uint32_t indice, Traco&) {
  if (indice < kVtableMd5Ctx || indice >= kVtableMd5Ctx + brew_slots::kHashCtxSlots) return false;
  const std::uint32_t slot = indice - kVtableMd5Ctx;
  Memoria& mem = cpu.Mem();
  const std::uint32_t pctx = cpu.Get(kR1);
  const std::uint32_t tamanho_ctx = cpu.Get(kR2);

  if (slot == brew_slots::kHashCtx_QueryInterface) {
    const std::uint32_t iid = pctx, ppo = tamanho_ctx;
    if (ppo == 0) { cpu.Set(kR0, kAeeBadParm); return true; }
    if (iid == kIidIqi || iid == kIidHashCtx) {
      mem.Escrever32(ppo, kObjetoMd5Ctx);
      const std::uint32_t refs = mem.Ler32(kObjetoMd5Ctx + 4) + 1;
      mem.Escrever32(kObjetoMd5Ctx + 4, refs);
      cpu.Set(kR0, kAeeSuccess);
    } else {
      mem.Escrever32(ppo, 0);
      cpu.Set(kR0, kAeeClassNotSupported);
    }
    return true;
  }
  if (slot == brew_slots::kHashCtx_Init) {
    if (ContextoValido(pctx, tamanho_ctx)) { Estado e; Iniciar(&e); EscreverEstado(mem, pctx, e); }
    return true;  // void: a too-small context fails silently by SDK contract.
  }
  if (slot == brew_slots::kHashCtx_Update) {
    const std::uint32_t dados = cpu.Get(kR3);
    const std::uint32_t tamanho = mem.Ler32(cpu.Get(kSP));
    if (ContextoValido(pctx, tamanho_ctx) && tamanho < 0x80000000u && (tamanho == 0 || dados != 0)) {
      Estado e = LerEstado(mem, pctx);
      Atualizar(mem, &e, dados, tamanho);
      EscreverEstado(mem, pctx, e);
    }
    return true;  // void
  }
  if (slot == brew_slots::kHashCtx_Final) {
    if (!ContextoValido(pctx, tamanho_ctx)) { cpu.Set(kR0, kAeeSecHashInvalidCtx); return true; }
    const std::uint32_t destino = cpu.Get(kR3);
    const std::uint32_t ptamanho = mem.Ler32(cpu.Get(kSP));
    if (destino == 0 || ptamanho == 0) { cpu.Set(kR0, kAeeBadParm); return true; }
    if (mem.Ler32(ptamanho) < kMd5DigestBytes) {
      mem.Escrever32(ptamanho, kMd5DigestBytes);
      cpu.Set(kR0, kAeeSecHashMoreData);
      return true;
    }
    Estado e = LerEstado(mem, pctx);  // Final is non-destructive: no host state and no stale context.
    std::uint8_t digest[kMd5DigestBytes];
    Finalizar(&e, digest);
    for (std::uint32_t i = 0; i < kMd5DigestBytes; ++i) mem.Escrever8(destino + i, digest[i]);
    mem.Escrever32(ptamanho, kMd5DigestBytes);
    cpu.Set(kR0, kAeeSuccess);
    return true;
  }
  if (slot == brew_slots::kHashCtx_SetKey) {
    cpu.Set(kR0, ContextoValido(pctx, tamanho_ctx) ? kAeeSecCryptInvalidKey : kAeeSecHashInvalidCtx);
    return true;
  }
  return false;
}

}  // namespace zb2::brew
