#include "core/brew/inetmgr.h"

#include <cstdio>
#include <string>

namespace zb2::brew {
namespace {

constexpr const char* kNomesDosSlots[kSlotsNetMgr] = {
    "AddRef", "Release", "SetMask", "GetHostByName", "GetLastError", "OpenSocket",
    "NetStatus", "GetMyIPAddr", "SetLinger", "OnEvent", "SetOpt", "GetOpt",
};

static_assert(kSlotsNetMgr == 12, "INetMgr tem 12 slots: IBase + 10 metodos legados");

}  // namespace

const char* NomeDoSlotNetMgr(std::uint32_t slot) {
  return slot < kSlotsNetMgr ? kNomesDosSlots[slot] : "slot_fora_da_tabela";
}

void ConstruirNetMgr(Memoria& mem, const Saidas& saidas, Traco& traco) {
  ConstruirObjeto(mem, saidas, kObjetoNetMgr, saidas.Endereco(kVtableNetMgr), kSlotsNetMgr,
                  kVtableNetMgr);

  bool ok = mem.Ler32(kObjetoNetMgr) == saidas.Endereco(kVtableNetMgr) &&
            mem.Ler32(kObjetoNetMgr + 4u) == 1;
  for (std::uint32_t slot = 0; slot < kSlotsNetMgr && ok; ++slot) {
    const std::uint32_t esperado =
        slot == 0 ? saidas.Endereco(3) : slot == 1 ? saidas.Endereco(4)
                                               : saidas.Endereco(kVtableNetMgr + slot);
    ok = mem.Ler32(saidas.Endereco(kVtableNetMgr) + slot * 4u) == esperado;
  }
  if (!ok) {
    traco.RegistarFalta(Area::Brew, "inetmgr_cablagem_perdida",
                        "INetMgr sem objecto ou vtable cablada");
  }
}

bool AtenderNetMgr(ICpu& cpu, std::uint32_t indice, Traco& traco) {
  if (indice < kVtableNetMgr + 2 || indice >= kVtableNetMgr + kSlotsNetMgr) return false;
  const std::uint32_t slot = indice - kVtableNetMgr;
  char detalhe[160];
  std::snprintf(detalhe, sizeof(detalhe),
                "r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x lr=0x%08x; sem backend de rede",
                cpu.Get(kR0), cpu.Get(kR1), cpu.Get(kR2), cpu.Get(kR3), cpu.Get(kLR));
  traco.RegistarFalta(Area::Brew, std::string("INetMgr::") + NomeDoSlotNetMgr(slot), detalhe);
  cpu.Set(kR0, kAeeUnsupported);
  return true;
}

}  // namespace zb2::brew
