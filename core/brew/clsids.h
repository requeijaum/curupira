#ifndef ZB2_CORE_BREW_CLSIDS_H
#define ZB2_CORE_BREW_CLSIDS_H

// O NOME DE CADA CLSID/IID, PARA A LISTA DE DEMANDA PODER SER LIDA.
//
// PORQUE ISTO EXISTE, medido. A demanda da bateria dizia
//
//     IShell::CreateInstance CLSID desconhecido  pedido 3x
//         iid=0x0100104f ppo=0x8007ff74   1x
//         iid=0x01003109 ppo=0x80200380   1x
//         iid=0x01028e3c ppo=0x8020339c   1x
//
// -- tres numeros e nenhum nome. Uma lista que diz o numero obriga a ir ao
// cabecalho CONTAR em cada ronda; uma que diz o nome e uma medida. E este
// projecto ja pagou por ter numeros da ABI escritos a mao em dois sitios: duas
// copias de um numero medido sao duas chances de ele divergir.
//
// Os numeros vem de `tools/clsids.inc`, GERADO dos `*.bid` e `*.h` do SDK por
// `tools/gerar_clsids.py`, com uma guarda que regenera e compara
// (`tools/verificar_clsids.sh`, `ctest` nome `clsids_do_sdk`). Nao ha um CLSID
// escrito a mao em lado nenhum deste modulo.

#include <cstdint>
#include <string>

namespace zb2::brew {

// O nome que o SDK da a este valor (`AEECLSID_AppHistory`, `AEECLSID_TEXTCTL`,
// ...), ou `nullptr` quando o SDK nao o declara.
//
// 1764 valores tem nome; os tres que este trabalho foi buscar -- 0x0100104f,
// 0x01003109 e 0x01028e3c -- estao entre eles, cada um com a sua origem escrita
// no `.inc`.
const char* NomeDoClsid(std::uint32_t iid);

// `AEECLSID_AppHistory (0x0100104f)`, ou `desconhecido (0x01011810)`.
//
// A forma com o numero ATRAS do nome e deliberada: o nome e para ler, o numero e
// para conferir. Trocar um pelo outro foi, no `IEgl`, o que fez dois modulos
// disputarem as mesmas saídas em silencio.
std::string DescreverClsid(std::uint32_t iid);

}  // namespace zb2::brew

#endif  // ZB2_CORE_BREW_CLSIDS_H
