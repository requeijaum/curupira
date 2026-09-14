#ifndef ZB2_CORE_BREW_FORMATO_H
#define ZB2_CORE_BREW_FORMATO_H

// Formatacao de texto no ABI do BREW: `sprintf` e `vsprintf`.
//
// VIVE NO MOTOR, e nao na ferramenta. A bateria e o instrumento que mede; a
// formatacao e comportamento do sistema emulado, e qualquer frente (o GUI, um
// futuro `zeebulator2`) tem de a poder usar. Enquanto esteve dentro da
// ferramenta, so a ferramenta sabia formatar -- e nenhum teste lhe chegava.

#include <cstdint>
#include <string>

#include "core/memoria/memoria.h"

namespace zb2::brew {

// Le a cadeia de formato do guest e escreve o resultado em `pBuf`, terminado a
// zero. Devolve o numero de caracteres escritos (sem o terminador), como o `sprintf`.
//
// `argumentos` sao os argumentos JA RESOLVIDOS pela convencao do AAPCS: os quatro
// primeiros em r0..r3, o resto na pilha. Quem chama resolve-os, porque so quem
// chama sabe se veio de um `sprintf` (argumentos soltos) ou de um `vsprintf`
// (um ponteiro para os argumentos).
// `limite` e o numero maximo de CARACTERES a escrever (sem o terminador). Zero
// significa sem limite, que e o contrato do `sprintf`/`vsprintf`. O `vsnprintf` passa
// o valor do r1 -- e sem ele um `%s` comprido transborda o buffer do jogo.
std::uint32_t Formatar(Memoria& mem, Endereco pBuf, Endereco pFormato,
                       const std::uint32_t* argumentos, int nArgumentos,
                       std::uint32_t limite = 0);

}  // namespace zb2::brew

#endif
