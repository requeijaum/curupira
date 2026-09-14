#include "core/brew/clsids.h"

#include <cstdio>

#include "tools/clsids.inc"

namespace zb2::brew {

namespace {

// A TABELA E ORDENADA POR VALOR (`tools/clsids.inc`), logo a procura e binaria e
// nao um `for` de 1764 iteracoes. Se um dia deixar de estar ordenada, o
// `static_assert` abaixo e o teste apanham-no antes de a procura mentir.
constexpr bool Ordenada() {
  for (std::size_t k = 1; k < brew_clsids::kQuantosValores; ++k) {
    if (brew_clsids::kPorValor[k - 1].valor >= brew_clsids::kPorValor[k].valor) return false;
  }
  return true;
}
static_assert(Ordenada(), "tools/clsids.inc tem de estar ordenado por valor");

}  // namespace

const char* NomeDoClsid(std::uint32_t iid) {
  std::size_t ini = 0, fim = brew_clsids::kQuantosValores;
  while (ini < fim) {
    const std::size_t meio = ini + (fim - ini) / 2;
    const std::uint32_t v = brew_clsids::kPorValor[meio].valor;
    if (v == iid) return brew_clsids::kPorValor[meio].nome;
    if (v < iid) {
      ini = meio + 1;
    } else {
      fim = meio;
    }
  }
  return nullptr;
}

std::string DescreverClsid(std::uint32_t iid) {
  char num[16];
  std::snprintf(num, sizeof(num), "0x%08x", iid);
  const char* nome = NomeDoClsid(iid);
  return nome == nullptr ? std::string("desconhecido (") + num + ")"
                         : std::string(nome) + " (" + num + ")";
}

}  // namespace zb2::brew
