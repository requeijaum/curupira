#ifndef ZB2_CORE_BREW_MD5CTX_H
#define ZB2_CORE_BREW_MD5CTX_H

#include <cstdint>

#include "core/brew/ajudantes.h"
#include "core/cpu/cpu.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"

namespace zb2::brew {

// AEECLSID_MD5Ctx / AEECLSID_CMD5Ctx: IHashCtx.  These are output indices, not
// guest addresses.  41200 starts after the current Qualcomm-extension ranges
// (the last one is 41150..41153).
constexpr std::uint32_t kVtableMd5Ctx = 41200;
constexpr std::uint32_t kObjetoMd5Ctx = 0x8f250000u;
constexpr std::uint32_t kMd5CtxBytes = 88;
constexpr std::uint32_t kMd5DigestBytes = 16;
constexpr std::uint32_t kAeeSecHashInvalidCtx = 0x601u;
constexpr std::uint32_t kAeeSecHashMoreData = 0x602u;
constexpr std::uint32_t kAeeSecCryptInvalidKey = 0x603u;

// Builds the one, stateless IHashCtx interface object.  Hash state never lives
// in this object: the application owns the 88-byte AEE_MD5_CTX buffer.
void ConstruirMd5Ctx(Memoria& mem, const Saidas& saidas, Traco& traco);

// True only for this interface's seven output entries.
bool AtenderMd5Ctx(ICpu& cpu, std::uint32_t indice, Traco& traco);

}  // namespace zb2::brew

#endif
