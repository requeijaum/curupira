#ifndef ZB2_CORE_CARGA_MOD_H
#define ZB2_CORE_CARGA_MOD_H

// Carregador de `.mod`.
//
// CONHECIMENTO HERDADO, e a frase abaixo e a medicao que o sustenta, feita na
// arvore antiga: o `.mod` e um **binario ARM plano, sem cabecalho, posicionavel
// em qualquer endereco, sem tabela de relocacao**. A prova foi carregar um
// `ddragonz.mod` real num endereco arbitrario e o executar passo a passo pelo
// interpretador -- 23 instrucoes reais, incluindo duas chamadas aninhadas com
// prologo, epilogo e retorno por BX LR correctos, sem qualquer analise de
// cabecalho.
//
// Isso NAO significa que carregar basta. Ha uma convencao por cima, a ROPI
// (Read-Only Position Independent) do compilador ARM: o codigo do modulo
// descobre o proprio endereco por PC relativo e le um ponteiro de **4 bytes
// ANTES da propria base** para chegar a tabela de funcoes do sistema
// (`AEEHelperFuncs`). Sem esse ponteiro, o modulo le zero e salta para zero.
//
// A medicao que fixou o deslocamento -4 tambem veio da arvore antiga: o
// `AEEModGen.c` de todo modulo chama `AEEStaticMod_New`, que faz
// `MALLOC(nSize + sizeof(IModuleVtbl))` -- e o desmonte do modulo mostra
// exactamente `r0 = nSize + 16` imediatamente antes de uma chamada indirecta
// cujo ponteiro veio de `[base - 4]` indexado.

#include <cstdint>
#include <string>
#include <vector>

#include "core/cpu/cpu.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"

namespace zb2 {

struct ResultadoDaCarga {
  bool ok = false;
  std::string motivo;       // vazio quando ok
  std::uint32_t base = 0;
  std::uint32_t tamanho = 0;
  std::uint32_t ponto_de_entrada = 0;
};

// Carrega `imagem` em `base` e aponta o PC para la.
//
// `tabela_de_ajudantes` e o endereco da `AEEHelperFuncs`, e vai para `base - 4`
// pela convencao ROPI descrita acima. Passar zero e aceite -- serve para MEDIR o
// que o modulo faz sem sistema, que e como se descobre quais slots ele precisa.
ResultadoDaCarga CarregarMod(Memoria& mem, const std::vector<std::uint8_t>& imagem,
                             std::uint32_t base, std::uint32_t tabela_de_ajudantes,
                             Traco* traco = nullptr);

}  // namespace zb2

#endif  // ZB2_CORE_CARGA_MOD_H
