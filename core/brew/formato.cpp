#include "core/brew/formato.h"

#include <cstdio>

#include "core/traco/traco.h"

namespace zb2::brew {

namespace {

// Le uma cadeia do guest, com limite. Sem limite, um `%s` apontado a memoria
// errada percorre o espaco todo antes de parar.
std::string LerCadeia(Memoria& mem, Endereco p, std::size_t maximo) {
  std::string s;
  for (std::size_t k = 0; k < maximo; ++k) {
    const char ch = static_cast<char>(mem.Ler8(p + static_cast<Endereco>(k)));
    if (ch == 0) break;
    s.push_back(ch);
  }
  return s;
}

}  // namespace

std::uint32_t Formatar(Memoria& mem, Endereco pBuf, Endereco pFormato,
                       const std::uint32_t* argumentos, int nArgumentos, std::uint32_t limite) {
  // O `Traco` do `Ler8` nao e usado aqui: uma formatacao em falta e registada
  // pelo DESPACHO, com o nome do slot. Registar de novo aqui duplicaria.
  std::string saida;
  int proximo = 0;
  const auto argumento = [&](void) -> std::uint32_t {
    return proximo < nArgumentos ? argumentos[proximo++] : 0u;
  };

  for (Endereco i = 0; i < 4096; ++i) {
    const char ch = static_cast<char>(mem.Ler8(pFormato + i));
    if (ch == 0) break;
    if (ch != '%') {
      saida.push_back(ch);
      continue;
    }
    ++i;
    char espec = static_cast<char>(mem.Ler8(pFormato + i));
    if (espec == 0) break;

    // Largura e zero a esquerda. `%04x` e comum e sem isto o jogo le um numero
    // errado -- e um numero errado num ficheiro de recursos e uma parede.
    int largura = 0;
    bool zero_a_esquerda = false;
    if (espec == '0') {
      zero_a_esquerda = true;
      ++i;
      espec = static_cast<char>(mem.Ler8(pFormato + i));
    }
    while (espec >= '0' && espec <= '9') {
      largura = largura * 10 + (espec - '0');
      ++i;
      espec = static_cast<char>(mem.Ler8(pFormato + i));
    }
    // Modificadores de comprimento: no AAPCS um `long` e um `int`, logo sao
    // absorvidos sem mudar nada.
    while (espec == 'l' || espec == 'h' || espec == 'z') {
      ++i;
      espec = static_cast<char>(mem.Ler8(pFormato + i));
    }

    char tmp[64];
    tmp[0] = 0;
    switch (espec) {
      case 'd':
      case 'i':
        std::snprintf(tmp, sizeof(tmp), "%d", static_cast<std::int32_t>(argumento()));
        break;
      case 'u':
        std::snprintf(tmp, sizeof(tmp), "%u", argumento());
        break;
      case 'x':
        if (largura > 0 && zero_a_esquerda) std::snprintf(tmp, sizeof(tmp), "%0*x", largura, argumento());
        else if (largura > 0) std::snprintf(tmp, sizeof(tmp), "%*x", largura, argumento());
        else std::snprintf(tmp, sizeof(tmp), "%x", argumento());
        break;
      case 'X': {
        const std::uint32_t v = argumento();
        if (largura > 0 && zero_a_esquerda) std::snprintf(tmp, sizeof(tmp), "%0*X", largura, v);
        else if (largura > 0) std::snprintf(tmp, sizeof(tmp), "%*X", largura, v);
        else std::snprintf(tmp, sizeof(tmp), "%X", v);
        break;
      }
      case 'p':
        std::snprintf(tmp, sizeof(tmp), "0x%08x", argumento());
        break;
      case 'c':
        tmp[0] = static_cast<char>(argumento() & 0xFFu);
        tmp[1] = 0;
        break;
      case 's': {
        const std::string s = LerCadeia(mem, argumento(), 4096);
        std::snprintf(tmp, sizeof(tmp), "%s", s.c_str());
        break;
      }
      case '%':
        tmp[0] = '%';
        tmp[1] = 0;
        break;
      default:
        tmp[0] = '%';
        tmp[1] = espec;
        tmp[2] = 0;
        break;
    }
    saida += tmp;
  }

  // O LIMITE do `vsnprintf`. Sem ele, um `%s` comprido escreve fora do buffer do
  // jogo -- e o jogo nao tem como saber.
  if (limite > 0 && saida.size() > limite) saida.resize(limite);
  for (std::size_t k = 0; k < saida.size(); ++k) {
    mem.Escrever8(pBuf + static_cast<Endereco>(k), static_cast<std::uint8_t>(saida[k]));
  }
  mem.Escrever8(pBuf + static_cast<Endereco>(saida.size()), 0);
  return static_cast<std::uint32_t>(saida.size());
}

}  // namespace zb2::brew
