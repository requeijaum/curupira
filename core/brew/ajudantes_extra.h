#ifndef ZB2_CORE_BREW_AJUDANTES_EXTRA_H
#define ZB2_CORE_BREW_AJUDANTES_EXTRA_H

// OS AJUDANTES DO SISTEMA QUE AINDA NAO TINHAM IMPLEMENTACAO, UM SLOT POR
// FUNCAO, COM A ASSINATURA LIDA DO CABECALHO.
//
// A TABELA `AEEHelperFuncs` E O SISTEMA. Todo o modulo BREW a ve: 117 ponteiros
// de funcao, 4 bytes cada, na ordem do `struct AEEHelperFuncs` de
// `platform/system/inc/AEEStdLib.h`. O offset de cada um e `4 * posicao` --
// medido, e o mesmo em qualquer mod do corpus (o mod le a tabela em `base - 4`:
// ver `core/brew/ajudantes.h`).
//
// PORQUE ESTE MODULO EXISTE. A lista de demanda da bateria dizia
// `AEEHelperFuncs[0x138]`, `[0x044]`, `[0x050]` -- numeros crus. Para saber o
// que cada um pedia, era preciso ir ao cabecalho contar campos a mao, em cada
// ronda. E contar a mao ja divergiu DUAS VEZES nesta tabela:
//
//   `kSlotStrtowstr` ficou em 0x0a0 (que e `wstrcompress`; `strtowstr` e 0x040)
//   `kSlotAeeGetRand` ficou em 0x090 (que e `atoi`; `aee_GetRand` e 0x0a8)
//
// As duas estao em `core/brew/despacho.cpp`, e as duas ficaram mudas: o
// `despacho` servia `strtowstr` no sitio do `wstrcompress` e um gerador
// aleatorio no sitio do `atoi`. Nada acusou, porque nao havia nada que
// comparasse o offset USADO com o offset MEDIDO.
//
// Aqui os offsets NAO se escrevem: vem de `tools/ajudantes_slots.inc`, gerado do
// cabecalho por `tools/nomear_ajudantes.py`, com ancoras conferidas dentro do
// proprio gerador e uma guarda no CTest (`tools/verificar_ajudantes.sh`).
//
// PRINCIPIO P2: onde nao ha implementacao honesta, NAO se devolve sucesso. A
// recusa e RUIDOSA e leva o NOME e a ASSINATURA do cabecalho, para o registo da
// bateria dizer que funcao do sistema o titulo pediu e nao um offset.

#include <cstddef>
#include <cstdint>
#include <string>

#include "core/brew/ajudantes.h"
#include "core/cpu/cpu.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"
#include "tools/ajudantes_slots.inc"

namespace zb2::brew {

// O catalogo dos 117 slots, com o nome e a assinatura lidos do cabecalho.
using Declaracao = brew_ajudantes::Declaracao;
const Declaracao* CatalogoDosAjudantes(std::size_t* quantos);
const Declaracao* DeclaracaoDoOffset(std::uint32_t offset);

// O nome que o SDK da ao offset, ou `nullptr` se o offset nao for de nenhum
// slot (nao alinhado a 4, ou acima dos 117).
//
// E o que tira os 14 nomes escritos a mao de `core/brew/interface.cpp`: eles
// estavam certos, mas eram uma SEGUNDA lista, e a segunda lista de uma coisa e
// a que fica para tras.
const char* NomeDoAjudanteDoSdk(std::uint32_t offset);

// A assinatura do cabecalho (texto da declaracao), ou `nullptr`.
const char* AssinaturaDoAjudante(std::uint32_t offset);

// O QUE ACONTECEU a um offset.
enum class Atendimento {
  // O offset nao e de nenhum dos 117 slots. O despacho trata-o como antes.
  Fora_Da_Tabela,
  // Havia implementacao honesta, e ela correu. R0 esta escrito.
  Implementado,
  // Esta no catalogo e NAO tem implementacao honesta: recusou-se em voz alta
  // (R0 = AEE_EUNSUPPORTED) e registou-se o nome e a assinatura (P2).
  Recusado,
};

class AjudantesExtra {
 public:
  AjudantesExtra(Memoria& mem, Alocador& alocador, Traco& traco);

  Atendimento Atender(ICpu& cpu, std::uint32_t offset);

  // Quantos offsets do catalogo TEM implementacao aqui. E um numero, e nao uma
  // afirmacao: a bateria e o teste leem-no.
  static std::size_t Implementados();

  std::uint64_t RecusasRegistadas() const { return recusas_; }

 private:
  Memoria& mem_;
  Alocador& al_;
  Traco& traco_;
  std::uint64_t recusas_ = 0;
};

// O GANCHO DO DESPACHO -- a unica linha que liga isto ao motor.
//
// `true` quando a tabela tomou conta do offset (implementou, ou recusou com o
// nome do SDK). `false` quando o offset nao e de nenhum dos 117: o despacho
// segue como antes, o que mantem UM so ponto de recusa para o resto.
bool AtenderAjudanteExtra(ICpu& cpu, Memoria& mem, Alocador& alocador, Traco& traco,
                          std::uint32_t offset);

}  // namespace zb2::brew

#endif  // ZB2_CORE_BREW_AJUDANTES_EXTRA_H
