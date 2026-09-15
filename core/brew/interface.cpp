#include "core/brew/interface.h"

namespace zb2::brew {

// A ORDEM E A DA MEDICAO, e nao a alfabetica: e a lista de IIDs que o
// `IShell::CreateInstance` mais pediu no corpus. Foi assim que se percebeu, na
// arvore antiga, quais das interfaces eram as mesmas por dois nomes diferentes.
const Generico kGenericos[7] = {
    {0x01001002u, "IHeap"},    {0x01001014u, "IFile"},  {0x01001056u, "ISound"},
    {0x01002001u, "IGraphics"}, {0x01028e51u, "IRootForm"},
    {0x0106c411u, "IHID"},     {0x0102c4e8u, "ISQLMgr"},
};

void ConstruirObjeto(Memoria& mem, const Saidas& saidas, std::uint32_t objeto,
                     std::uint32_t vtable, std::uint32_t quantos_slots,
                     std::uint32_t base_dos_slots) {
  mem.Escrever32(objeto, vtable);  // *(pobj) = vtable
  mem.Escrever32(objeto + 4, 1);   // contagem de referencias

  // A IBASE PRIMEIRO, e uma vez so. Os enderecos 3 e 4 sao os do despacho para
  // `AddRef` e `Release`.
  mem.Escrever32(vtable + 0, saidas.Endereco(3));
  mem.Escrever32(vtable + 4, saidas.Endereco(4));
  for (std::uint32_t i = 2; i < quantos_slots; ++i) {
    mem.Escrever32(vtable + i * 4, saidas.Endereco(base_dos_slots + i));
  }
}

void ConstruirVtableDoBitmap(Memoria& mem, const Saidas& saidas) {
  ConstruirObjeto(mem, saidas, kObjDibBase + 0x300, saidas.Endereco(kVtableBitmap),
                  kSlotsPorVtable, kVtableBitmap);
  // O `+4` DESTE OBJECTO NAO E UMA CONTAGEM: um IDIB e uma struct PUBLICA e o
  // `+4` dela e o `pPaletteMap` (`AEEIDIB.h:44`), que o `IDIB_FlushPalette`
  // (`:83-86`) desreferencia. O `ConstruirObjeto` acabou de la pôr o `1` da
  // convencao da casa; aqui repoe-se o ponteiro NULO, e a contagem passa a viver
  // no `Despacho` (`refs_do_dib_`).
  mem.Escrever32(kObjDibBase + 0x300 + CamposDoIdib::kPPaletteMap, 0);
}

ResultadoCablagem Cablar(Memoria& mem, const Saidas& saidas, const Ligacao* ligacoes,
                         std::size_t quantas) {
  for (std::size_t k = 0; k < quantas; ++k) {
    const Ligacao& l = ligacoes[k];
    // O FIM desta vtable, e nao a base da PRIMEIRA vtable generica: cada vtable
    // generica tem 64 slots, logo o fim de uma e o inicio da seguinte. Uma
    // guarda que comparava com a base disparou sobre uma cablagem CERTA.
    // TODAS as vtables tem `kSlotsPorVtable` slots -- e o `ConstruirObjeto` e
    // sempre chamado com esse numero. As genericas ficam encostadas umas as
    // outras, logo o fim de uma e o inicio da seguinte.
    //
    // A primeira versao comparava com `kVtableGenericoBase`, e por isso NAO
    // recusava um slot 999 do `IDisplay`: 6000 + 999 continua abaixo de 9000. O
    // teste apanhou-a. **Um limite que compara com a base errada nao limita
    // nada** -- a segunda vez nesta sessao que uma guarda falha na CONTA, e nao
    // no proposito.
    const std::uint32_t fim = (l.vt >= kVtableGenericoBase)
                                  ? VtGenerico((l.vt - kVtableGenericoBase) / kPassoGenerico + 1u)
                                  : l.vt + kSlotsPorVtable;
    if (l.vt + l.slot >= fim) {
      return {false, "slot " + std::to_string(l.slot) + " de " + Hex(l.vt) +
                         " sai da vtable"};
    }
    // A IBASE NAO SE CABLA -- com UMA excepcao, e ela e declarada.
    //
    // Para os objectos do `ConstruirObjeto`, os slots 0 e 1 ja tem `AddRef` e
    // `Release`. O objecto FICHEIRO nasce a parte e NAO tem `Release`, e o jogo
    // fecha ficheiros com `IFILE_Release(p)`, que e o slot 1.
    if (l.slot < 2 && l.vt != kVtableFileObj) {
      return {false, "slot " + std::to_string(l.slot) + " de " + Hex(l.vt) +
                         " e da IBase"};
    }
    mem.Escrever32(saidas.Endereco(l.vt) + l.slot * 4, saidas.Endereco(l.saida));
  }
  // A LEITURA DE VOLTA, e devolve o motivo em vez de abortar.
  for (std::size_t k = 0; k < quantas; ++k) {
    const Ligacao& l = ligacoes[k];
    const std::uint32_t lido = mem.Ler32(saidas.Endereco(l.vt) + l.slot * 4);
    if (lido != saidas.Endereco(l.saida)) {
      return {false, "cablagem perdida: " + Hex(l.vt) + " slot " +
                         std::to_string(l.slot)};
    }
  }
  return {true, ""};
}

const char* NomeDoAjudante(std::uint32_t offset) {
  switch (offset) {
    case 0x000: return "memmove";
    case 0x004: return "memset";
    case 0x008: return "strcpy";
    case 0x00c: return "strcat";
    case 0x010: return "strcmp";
    case 0x014: return "strlen";
    case 0x018: return "strchr";
    case 0x01c: return "strrchr";
    case 0x020: return "sprintf";
    case 0x068: return "malloc";
    case 0x06c: return "free";
    case 0x088: return "OEMStrSize";
    case 0x08c: return "GetAEEVersion";
    case 0x09c: return "dbgprintf";
    default: return nullptr;
  }
}

}  // namespace zb2::brew
