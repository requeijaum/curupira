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

// O TEXTO, sem o escrever: o `dbgprintf` faz a mensagem aparecer no traco.
//
// O `Formatar` acima e a mesma formatacao MAIS a escrita no buffer do guest, e
// era tudo o que existia -- quem quisesse o texto do lado do HOSPEDOR tinha de
// arranjar um buffer emprestado dentro do espaco do guest. O `dbgprintf` nao
// escreve no guest: imprime no traco. Sem esta porta, a unica saida era escrever
// num buffer do guest para o ler de volta, que e o defeito a contornar-se.
//
// A MEDICAO, no traco do `alice` (frente `fmt`): as linhas
//     [brew] GUEST_DBGPRINTF *dbgprintf-%d* %s:%d
//     [brew] GUEST_DBGPRINTF %s
// sao a CADEIA DE FORMATO impressa tal e qual -- a mensagem do ASSERT do titulo
// nao aparece em sitio nenhum, em 7 titulos.
std::string FormatarParaTexto(Memoria& mem, Endereco pFormato, const std::uint32_t* argumentos,
                              int nArgumentos, std::uint32_t limite = 0);

// --- `AEEOldVaList`: o `va_list` do SDK, que e `int**` ---------------------
//
// `typedef int **AEEOldVaList;` -- `platform/system/inc/AEEOldVaList.h:37` (o ramo
// ARM; a linha 43 e `typedef int *` do "everybody else"). O que o modulo poe no
// registador NAO e a AREA de argumentos: e o endereco da VARIAVEL `va_list`. O
// proprio SDK constroi-o assim (`AEEStdLib.h:374,379`:
// `AEEOldVaList_From_va_list((const va_list *)&arg)`), e a area resolve-se com
// UMA indirecao: `area = mem.Ler32(pLista)`, e o argumento k e
// `mem.Ler32(area + 4*k)`.
//
// O DESMONTE DO GUEST diz o mesmo, `alice.mod` (base zero: deslocamento ==
// endereco), no prologo do ajudante do ASSERT:
//
//     3f01c: e92d000f  push {r0, r1, r2, r3}   @ guarda os quatro registadores
//     3f030: e28d0054  add  r0, sp, #84        @ = &primeiro argumento na pilha
//     3f034: e58d0004  str  r0, [sp, #4]       @ a VARIAVEL va_list vive em [sp+4]
//     3f03c: e59d2050  ldr  r2, [sp, #80]      @ formato = o r2 guardado
//     3f040: e28d3004  add  r3, sp, #4         @ r3 = &va_list  (e `int**`)
//     3f044: e590c140  ldr  ip, [r0, #320]     @ AEEHelperFuncs[0x140] = vsnprintf
//     3f048: e28d0008  add  r0, sp, #8         @ o buffer de destino
//     3f04c: e12fff3c  blx  ip                 @ vsnprintf(buf, 64, fmt, r3)
//
// SEM a indirecao -- ler em `pLista + 4*k` -- os argumentos saem do sitio errado
// e, no caso medido, saem dos BYTES DO PROPRIO FORMATO: com o formato em 0x3fa30
// ("  %d @ %s"), o argumento do `%s` era `mem.Ler32(0x3fa34)` = 0x25204020 = os
// ASCII " @ %" lidos como ENDERECO. O `Formatar` ia entao ler uma cadeia em
// 0x25204020, que nao existe: a recusa `Memoria::Ler fora de instrucao` com o PC
// 0x3f04c, 20 vezes no `alice` (frente `inst`, e a medicao desta frente).
//
// Devolve quantos argumentos ficaram resolvidos: 0 quando `pLista` e nulo ou
// quando a area de argumentos nao esta em memoria Mapeada (nesse caso `destino`
// fica a zeros e NAO se le a area -- a recusa e essa, e o texto sai vazio, que e
// o que a leitura fora de memoria ja devolvia).
int ArgumentosDoVaLists(Memoria& mem, Endereco pLista, std::uint32_t* destino, int quantos);

}  // namespace zb2::brew

#endif
