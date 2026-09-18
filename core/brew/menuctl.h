#ifndef ZB2_CORE_BREW_MENUCTL_H
#define ZB2_CORE_BREW_MENUCTL_H

#include <array>
#include <cstdint>
#include <map>
#include <vector>

#include "core/brew/interface.h"
#include "core/cpu/cpu.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"

namespace zb2::brew {

// IMenuCtl, created for AEECLSID_SOFTKEYCTL.  Each factory call receives a
// separate guest object and separate host-side state.
constexpr std::uint32_t kClsidSoftKeyCtl = 0x01003101u;
constexpr std::uint32_t kVtableMenuCtl = 41500u;
constexpr std::uint32_t kMenuCtlSlots = 32u;
constexpr std::uint32_t kObjetoMenuCtlInicio = 0x8F270000u;
constexpr std::uint32_t kPassoDoObjetoMenuCtl = 0x100u;
constexpr std::uint32_t kMaximoDeObjetosMenuCtl = 256u;

class MenuCtl {
 public:
  MenuCtl(Memoria& mem, Traco& traco) : mem_(mem), traco_(traco) {}

  void Instalar(const Saidas& saidas);
  std::uint32_t Criar();
  bool Atender(ICpu& cpu, std::uint32_t indice);

 private:
  struct Item {
    std::uint16_t recurso = 0;
    std::uint16_t id = 0;
    std::uint32_t dado = 0;
    std::vector<std::uint16_t> texto;
  };
  struct Estado {
    std::array<std::int16_t, 4> rect{{0, 216, 320, 24}};
    bool comando_ativo = true;
    std::array<std::uint8_t, 40> cores{};
    std::vector<Item> itens;
  };

  bool CopiarTexto(std::uint32_t endereco, std::vector<std::uint16_t>* texto) const;
  void Recusar(ICpu& cpu, std::uint32_t slot);

  Memoria& mem_;
  Traco& traco_;
  Saidas saidas_;
  bool instalado_ = false;
  std::uint32_t proximo_ = kObjetoMenuCtlInicio;
  std::map<std::uint32_t, Estado> estados_;
};

}  // namespace zb2::brew
#endif  // ZB2_CORE_BREW_MENUCTL_H
