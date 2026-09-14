#include "core/carga/mod.h"

namespace zb2 {

ResultadoDaCarga CarregarMod(Memoria& mem, const std::vector<std::uint8_t>& imagem,
                             std::uint32_t base, std::uint32_t tabela_de_ajudantes, Traco* traco) {
  ResultadoDaCarga r;
  r.base = base;
  r.tamanho = static_cast<std::uint32_t>(imagem.size());
  r.ponto_de_entrada = base;
  if (imagem.empty()) {
    r.motivo = "imagem vazia";
    return r;
  }
  // O endereco tem de deixar espaco para o ponteiro da ROPI em `base - 4`.
  //
  // BASE ZERO E ACEITE, e o ponteiro vai para 0xFFFFFFFC. A versao anterior
  // recusava `base < 4`, e isso PROIBIA o unico valor que faz os literais do
  // modulo estarem certos (ver `tests/mod_base_test.cpp`). A aritmetica do
  // `uint32_t` faz o wrap-around e a memoria e esparsa: 0xFFFFFFFC e uma pagina
  // como outra qualquer.
  const std::uint32_t endereco_ropi = base - 4u;
  if (static_cast<std::uint64_t>(base) + imagem.size() > 0x100000000ull) {
    r.motivo = "imagem nao cabe no espaco de enderecos a partir desta base";
    return r;
  }
  mem.EscreverBruto(base, imagem.data(), r.tamanho);
  // A convencao ROPI. Escreve-se mesmo quando o valor e zero: deixar o sitio por
  // escrever faria o modulo ler o que la estivesse, e um zero acidental e
  // indistinguivel de um zero propositado no log.
  mem.Escrever32(endereco_ropi, tabela_de_ajudantes);
  if (traco != nullptr) {
    char buf[160];
    std::snprintf(buf, sizeof(buf), "base=0x%08x tamanho=%u tabela=0x%08x (em base-4)",
                  base, r.tamanho, tabela_de_ajudantes);
    traco->Emitir(Area::Carga, Nivel::Informacao, "MOD_CARREGADO", buf);
  }
  r.ok = true;
  return r;
}

}  // namespace zb2
