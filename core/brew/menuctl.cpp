#include "core/brew/menuctl.h"

#include <cstdio>
#include <string>

namespace zb2::brew {
namespace {

constexpr const char* kNomes[kMenuCtlSlots] = {
    "AddRef", "Release", "HandleEvent", "Redraw", "SetActive", "IsActive", "SetRect", "GetRect",
    "SetProperties", "GetProperties", "Reset", "SetTitle", "AddItem", "AddItemEx", "GetItemData", "DeleteItem",
    "DeleteAll", "SetSel", "GetSel", "EnableCommand", "SetItemText", "SetItemTime", "GetItemTime", "SetStyle",
    "SetColors", "MoveItem", "GetItemCount", "GetItemID", "GetItem", "SetItem", "Sort", "SetSelEx",
};
constexpr std::uint32_t kMaximoTexto = 128;
constexpr std::uint32_t kTamanhoCores = 40;  // AEEMenuColors: uint16 mask + alignment + 9 RGBVALs.

}  // namespace

void MenuCtl::Instalar(const Saidas& saidas) {
  saidas_ = saidas;
  instalado_ = true;
  proximo_ = kObjetoMenuCtlInicio;
  estados_.clear();
  const std::uint32_t vt = saidas_.Endereco(kVtableMenuCtl);
  mem_.Escrever32(vt, saidas_.Endereco(3));
  mem_.Escrever32(vt + 4u, saidas_.Endereco(4));
  for (std::uint32_t slot = 2; slot < kMenuCtlSlots; ++slot) {
    mem_.Escrever32(vt + slot * 4u, saidas_.Endereco(kVtableMenuCtl + slot));
  }
  bool ok = mem_.Ler32(vt) == saidas_.Endereco(3) &&
            mem_.Ler32(vt + 4u) == saidas_.Endereco(4);
  for (std::uint32_t slot = 2; slot < kMenuCtlSlots && ok; ++slot) {
    ok = mem_.Ler32(vt + slot * 4u) == saidas_.Endereco(kVtableMenuCtl + slot);
  }
  if (!ok) traco_.RegistarFalta(Area::Brew, "menuctl_cablagem_perdida", "IMenuCtl sem vtable cablada");
}

std::uint32_t MenuCtl::Criar() {
  if (!instalado_ || estados_.size() >= kMaximoDeObjetosMenuCtl) return 0;
  const std::uint32_t objeto = proximo_;
  proximo_ += kPassoDoObjetoMenuCtl;
  mem_.Escrever32(objeto, saidas_.Endereco(kVtableMenuCtl));
  mem_.Escrever32(objeto + 4u, 1u);
  estados_.emplace(objeto, Estado{});
  return objeto;
}

bool MenuCtl::CopiarTexto(std::uint32_t endereco, std::vector<std::uint16_t>* texto) const {
  if (endereco == 0) return false;
  texto->clear();
  for (std::uint32_t n = 0; n <= kMaximoTexto; ++n) {
    const std::uint32_t onde = endereco + n * 2u;
    if (!mem_.Existe(onde) || !mem_.Existe(onde + 1u)) return false;
    const std::uint16_t c = mem_.Ler16(onde);
    if (c == 0) return n != 0;
    if (n == kMaximoTexto) return false;
    texto->push_back(c);
  }
  return false;
}

void MenuCtl::Recusar(ICpu& cpu, std::uint32_t slot) {
  char detalhe[160];
  std::snprintf(detalhe, sizeof(detalhe), "r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x lr=0x%08x",
                cpu.Get(kR0), cpu.Get(kR1), cpu.Get(kR2), cpu.Get(kR3), cpu.Get(kLR));
  const char* nome = slot < kMenuCtlSlots ? kNomes[slot] : "slot_fora_da_tabela";
  traco_.RegistarFalta(Area::Brew, std::string("IMenuCtl::") + nome, detalhe);
  cpu.Set(kR0, kAeeUnsupported);
}

bool MenuCtl::Atender(ICpu& cpu, std::uint32_t indice) {
  if (indice < kVtableMenuCtl || indice >= kVtableMenuCtl + kMenuCtlSlots) return false;
  const std::uint32_t slot = indice - kVtableMenuCtl;
  const std::uint32_t objeto = cpu.Get(kR0);
  auto it = estados_.find(objeto);
  if (it == estados_.end()) {
    Recusar(cpu, slot);
    return true;
  }
  Estado& estado = it->second;
  switch (slot) {
    case 0: {
      std::uint32_t refs = mem_.Ler32(objeto + 4u);
      if (refs != 0xffffffffu) ++refs;
      mem_.Escrever32(objeto + 4u, refs);
      cpu.Set(kR0, refs);
      return true;
    }
    case 1: {
      std::uint32_t refs = mem_.Ler32(objeto + 4u);
      if (refs != 0) --refs;
      mem_.Escrever32(objeto + 4u, refs);
      cpu.Set(kR0, refs);
      if (refs == 0) estados_.erase(it);
      return true;
    }
    case 7: {  // void GetRect(IMenuCtl*, AEERect*)
      const std::uint32_t out = cpu.Get(kR1);
      if (out == 0 || !mem_.Existe(out) || !mem_.Existe(out + 7u)) {
        traco_.RegistarFalta(Area::Brew, "IMenuCtl::GetRect", "AEERect de saida nulo ou nao mapeado");
        return true;
      }
      for (std::uint32_t n = 0; n < 4; ++n) mem_.Escrever16(out + n * 2u, static_cast<std::uint16_t>(estado.rect[n]));
      return true;
    }
    case 12: {  // boolean AddItem(this, resFile, resID, itemID, text, data)
      std::vector<std::uint16_t> texto;
      const std::uint32_t texto_endereco = mem_.Ler32(cpu.Get(kSP));
      if (!CopiarTexto(texto_endereco, &texto)) {
        cpu.Set(kR0, 0);
        return true;
      }
      const std::uint16_t id = static_cast<std::uint16_t>(cpu.Get(kR3));
      for (const Item& item : estado.itens) {
        if (item.id == id) {
          cpu.Set(kR0, 0);
          return true;
        }
      }
      Item item;
      item.recurso = static_cast<std::uint16_t>(cpu.Get(kR2));
      item.id = id;
      item.dado = mem_.Ler32(cpu.Get(kSP) + 4u);
      item.texto = std::move(texto);
      estado.itens.push_back(std::move(item));
      cpu.Set(kR0, 1);
      return true;
    }
    case 19:  // void EnableCommand(IMenuCtl*, boolean)
      estado.comando_ativo = cpu.Get(kR1) != 0;
      return true;
    case 24: {  // void SetColors(IMenuCtl*, AEEMenuColors*)
      const std::uint32_t cores = cpu.Get(kR1);
      if (cores == 0 || !mem_.Existe(cores) || !mem_.Existe(cores + kTamanhoCores - 1u)) {
        traco_.RegistarFalta(Area::Brew, "IMenuCtl::SetColors", "AEEMenuColors nulo ou nao mapeado");
        return true;
      }
      for (std::uint32_t n = 0; n < kTamanhoCores; ++n) estado.cores[n] = mem_.Ler8(cores + n);
      return true;
    }
    default:
      Recusar(cpu, slot);
      return true;
  }
}

}  // namespace zb2::brew
