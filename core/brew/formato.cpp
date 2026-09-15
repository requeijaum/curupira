#include "core/brew/formato.h"

#include <cstdio>
#include <cstring>

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

std::string FormatarParaTexto(Memoria& mem, Endereco pFormato,
                              const std::uint32_t* argumentos, int nArgumentos,
                              std::uint32_t limite) {
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
    // ONDE COMECA A ESPECIFICACAO (`%` excluido): o `%f` volta a ler este texto
    // para o passar ao `snprintf` do sistema, tal e qual o guest o escreveu.
    const Endereco inicio_da_especificacao = i;
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
    // O PONTO E A PRECISAO. Nao mudam a largura nem o argumento dos inteiros,
    // mas TEM de ser consumidos: sem isto o `%4.2f` sai como o texto "%.2f " e,
    // pior, NAO CONSOME O ARGUMENTO -- e todos os argumentos seguintes passam a
    // ser os do sitio errado. MEDIDO no `quake2brew`, cujo formato e
    // `"%4.2f %s %s %s"` (a cadeia esta no `.mod` ao lado do proprio double 3.2,
    // em 0x299d0/0x299ce): o `%s` seguinte recebia 0x7ae147ae, a METADE BAIXA do
    // 3.2, e a leitura dessa "cadeia" era uma falta nova (`Memoria::Ler fora de
    // instrucao` 0 -> 1 nesse titulo).
    if (espec == '.') {
      ++i;
      espec = static_cast<char>(mem.Ler8(pFormato + i));
      while (espec >= '0' && espec <= '9') {
        ++i;
        espec = static_cast<char>(mem.Ler8(pFormato + i));
      }
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
      // UM `double` OCUPA DOIS ARGUMENTOS de 32 bits na AAPCS (o par de
      // registadores alinhado, ou duas palavras alinhadas na pilha), e o par vem
      // na ordem baixo-alto. Consumir UM so desloca tudo o que vem a seguir --
      // ver o comentario da precisao, com a medicao do `quake2brew`.
      //
      // O texto da especificacao e passado ao `snprintf` DO SISTEMA tal como o
      // guest o escreveu: a precisao, a largura e os sinalizadores do `%f` sao os
      // do C, e reescreve-los a mao aqui seria a terceira copia da mesma regra.
      case 'f':
      case 'F':
      case 'e':
      case 'E':
      case 'g':
      case 'G': {
        const std::uint32_t meio_baixo = argumento();
        const std::uint32_t meio_alto = argumento();
        const std::uint64_t bits =
            (static_cast<std::uint64_t>(meio_alto) << 32) | static_cast<std::uint64_t>(meio_baixo);
        double v = 0.0;
        std::memcpy(&v, &bits, sizeof(v));
        char formato_do_host[24];
        int m = 0;
        formato_do_host[m++] = '%';
        for (Endereco k = inicio_da_especificacao; k <= i && m < 22; ++k) {
          formato_do_host[m++] = static_cast<char>(mem.Ler8(pFormato + k));
        }
        formato_do_host[m] = 0;
        std::snprintf(tmp, sizeof(tmp), formato_do_host, v);
        break;
      }
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
  return saida;
}

// O `Formatar` do contrato do `sprintf`: formata, e ESCREVE no buffer do guest.
// O trabalho todo esta no `FormatarParaTexto` -- o mesmo texto, sem a escrita.
std::uint32_t Formatar(Memoria& mem, Endereco pBuf, Endereco pFormato,
                       const std::uint32_t* argumentos, int nArgumentos, std::uint32_t limite) {
  const std::string saida = FormatarParaTexto(mem, pFormato, argumentos, nArgumentos, limite);
  for (std::size_t k = 0; k < saida.size(); ++k) {
    mem.Escrever8(pBuf + static_cast<Endereco>(k), static_cast<std::uint8_t>(saida[k]));
  }
  mem.Escrever8(pBuf + static_cast<Endereco>(saida.size()), 0);
  return static_cast<std::uint32_t>(saida.size());
}

// O `AEEOldVaList` do SDK e `int**` (`AEEOldVaList.h:37`): o registador traz o
// endereco da VARIAVEL `va_list`, e a area de argumentos esta no valor dela. Ver
// o cabecalho para o desmonte do `alice.mod` que o fixa.
int ArgumentosDoVaLists(Memoria& mem, Endereco pLista, std::uint32_t* destino, int quantos) {
  for (int k = 0; k < quantos; ++k) destino[k] = 0;
  if (pLista == 0 || quantos <= 0) return 0;
  if (!mem.Existe(pLista)) return 0;  // nem a variavel existe: nao se le
  // UMA indirecao. Sem ela o argumento k sai de `pLista + 4*k`, que e o sitio da
  // VARIAVEL e nao o dos argumentos -- e no caso medido sao os BYTES do formato.
  const Endereco area = mem.Ler32(pLista);
  if (area == 0) return 0;
  // NAO SE LE O QUE NAO EXISTE. Uma palavra que cai numa pagina por mapear
  // devolveria zero -- e deixava uma leitura NAO MAPEADA pendente, que a frente
  // `inst` atribui a quem a fez. Seria o NOSSO defeito a aparecer como evidencia
  // contra o guest, que e o que essa frente inteira existe para nao acontecer
  // (e o `quake2brew` e o `zenonia` ganharam uma falta assim quando o `dbgprintf`
  // passou a ler a pilha a toda a hora: esta medido). O valor e o mesmo -- zero.
  for (int k = 0; k < quantos; ++k) {
    const Endereco onde = area + 4u * static_cast<Endereco>(k);
    destino[k] = mem.Existe(onde) ? mem.Ler32(onde) : 0u;
  }
  return quantos;
}

}  // namespace zb2::brew
