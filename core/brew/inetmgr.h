#ifndef ZB2_CORE_BREW_INETMGR_H
#define ZB2_CORE_BREW_INETMGR_H

#include <cstdint>

#include "core/brew/interface.h"
#include "core/cpu/cpu.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"
#include "tools/clsids.inc"

namespace zb2::brew {

// AEECLSID_NET is the legacy INetMgr object requested by reksio.  Its head is
// IBase only: slot 2 is SetMask, not QueryInterface.
constexpr std::uint32_t kAeeClsidNet = brew_clsids::kClsid_NET;
static_assert(kAeeClsidNet == 0x0100102eu, "AEECLSID_NET e 0x0100102e");
constexpr std::uint32_t kVtableNetMgr = 41400;
constexpr std::uint32_t kObjetoNetMgr = 0x8f252000u;
constexpr std::uint32_t kSlotsNetMgr = 12;

const char* NomeDoSlotNetMgr(std::uint32_t slot);

// Installs only the object's ABI and lifetime head.  Network operations remain
// explicit refusals until a network backend is measured and implemented.
void ConstruirNetMgr(Memoria& mem, const Saidas& saidas, Traco& traco);

// Handles slots 2..11.  Slots 0 and 1 use the shared IBase AddRef/Release
// outputs installed by ConstruirObjeto.
bool AtenderNetMgr(ICpu& cpu, std::uint32_t indice, Traco& traco);

}  // namespace zb2::brew

#endif  // ZB2_CORE_BREW_INETMGR_H
