#ifndef ZB2_CORE_BREW_INTERFACE_H
#define ZB2_CORE_BREW_INTERFACE_H

// A construcao de objectos BREW e a cablagem das vtables.
//
// VIVE NO MOTOR. Isto e o que faz um `IShell`, um `IDisplay` ou um `IFile`
// existirem na memoria do guest -- e qualquer frente precisa disso, nao so a
// ferramenta que mede.
//
// AS CONSTANTES DE SLOT VEM DE `brew_slots.inc`, GERADO DOS CABECALHOS.
// Estiveram escritas a mao e estavam TODAS erradas por um, porque `INHERIT_IBase`
// tem DOIS membros (`AddRef`, `Release`) e eu contava TRES -- a procura de um
// `QueryInterface` que nao existe em `INHERIT_IBase`.

#include <cstdint>
#include <vector>

#include "tools/brew_slots.inc"
#include "core/cpu/cpu.h"
#include "core/memoria/memoria.h"

namespace zb2::brew {

// --- a faixa de saida -------------------------------------------------------
//
// Um endereco unico por SLOT: e o que faz o registo de faltas dizer QUAL metodo
// cada titulo pediu, em vez de "algo do shell". Com um stub so para todos, 27
// titulos pediam "algo do shell" e o numero nao tinha nome.
constexpr std::uint32_t kBaseAjudantes = 1000;
constexpr std::uint32_t kBaseDoShell = 2000;
constexpr std::uint32_t kVtableShell = 3000;
constexpr std::uint32_t kVtableDisplay = 6000;
constexpr std::uint32_t kVtableFileMgr = 7000;
constexpr std::uint32_t kVtableBitmap = 8000;
constexpr std::uint32_t kVtableFileObj = 9500;
constexpr std::uint32_t kVtableGenericoBase = 9000;
constexpr std::uint32_t kPassoGenerico = 64;
// Quantos slots tem cada vtable construida. Uma so constante, para o limite da
// cablagem e a construcao nao poderem divergir.
constexpr std::uint32_t kSlotsPorVtable = 64;

constexpr std::uint32_t kObjShell = 0x80020000u;
constexpr std::uint32_t kObjDisplay = 0x80030000u;
constexpr std::uint32_t kObjFileMgr = 0x80040000u;
constexpr std::uint32_t kObjDibBase = 0x80050000u;
constexpr std::uint32_t kObjGenericoBase = 0x80060000u;
constexpr std::uint32_t kObjFileBase = 0x80070000u;
constexpr std::uint32_t kPassoGenericoObj = 0x100;

// As interfaces que o corpus MEDIU como pedidas, com o nome do SDK.
// A lista vem da medicao, e nao de uma leitura de cabecalho: e a ordem por
// demanda que diz o que vale implementar.
struct Generico {
  std::uint32_t iid;
  const char* nome;
};
extern const Generico kGenericos[7];
constexpr std::uint32_t kNGenericos = 7;

constexpr std::uint32_t ObjGenerico(std::uint32_t k) {
  return kObjGenericoBase + k * kPassoGenericoObj;
}
constexpr std::uint32_t VtGenerico(std::uint32_t k) {
  return kVtableGenericoBase + k * kPassoGenerico;
}

// --- construcao -------------------------------------------------------------

// Escreve um objecto ROPI: `[0]` = a vtable, `[4]` = a contagem de referencias, e
// um endereco de saida distinto por slot.
//
// OS SLOTS 0 E 1 SAO A IBASE, E SAO ESCRITOS UMA VEZ, explicitamente, e o laco
// comeca no 2. Antes, o laco escrevia os 64 slots e havia um par de escritas a
// seguir a corrigir os dois primeiros: funcionava, mas **dependia da ordem** --
// e a ordem e a classe de erro que apareceu OITO vezes neste trabalho.
void ConstruirObjeto(Memoria& mem, const Saidas& saidas, std::uint32_t objeto,
                     std::uint32_t vtable, std::uint32_t quantos_slots,
                     std::uint32_t base_dos_slots);

// Uma linha da cablagem: que endereco de saida fica no slot `slot` da vtable `vt`.
struct Ligacao {
  std::uint32_t vt;
  std::uint32_t slot;
  std::uint32_t saida;
};

// O motivo pelo qual a cablagem pode ser recusada. `ok` a false significa que a
// ligacao foi REJEITADA e o objecto ficou como estava -- um abortar seria pior
// num produto, e uma recusa silenciosa pior ainda.
struct ResultadoCablagem {
  bool ok = true;
  std::string motivo;
};

// Escreve a cablagem na memoria e CONFIRMA-A com uma leitura de volta.
//
// A LEITURA DE VOLTA NAO E CERIMONIA. Uma cablagem ja se perdeu sem sintoma
// visivel: o `SetTimer` estava escrito e a funcionar, mas a entrada da vtable
// apontava para o stub que recusa, e o sintoma era a bateria dizer "falta
// SetTimer" -- o que exigiu uma corrida inteira de 4 minutos para descobrir.
ResultadoCablagem Cablar(Memoria& mem, const Saidas& saidas, const Ligacao* ligacoes,
                         std::size_t quantas);

// O nome que o SDK da ao offset na tabela de ajudantes, ou `nullptr`.
const char* NomeDoAjudante(std::uint32_t offset);

}  // namespace zb2::brew

#endif
