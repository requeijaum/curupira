#include "core/brew/interface.h"

#include <cstdio>
#include <string>
#include <vector>

#include "core/brew/tela.h"  // Tela::RgbvalPara565 (a conversao do IDisplay)

namespace zb2::brew {

// A ORDEM E A DA MEDICAO, e nao a alfabetica: e a lista de IIDs que o
// `IShell::CreateInstance` mais pediu no corpus. Foi assim que se percebeu, na
// arvore antiga, quais das interfaces eram as mesmas por dois nomes diferentes.
const Generico kGenericos[7] = {
    {0x01001002u, "IHeap"},
    // 0x01001014 NAO e o IFile: e o `AEECLSID_UNZIPSTREAM`
    // (`AEEClassIDs.h:82`, `AEECLSID_CORE + 20`). O nome errado ("IFile") vivia
    // AQUI e na copia de `tools/bateria.cpp`; a da ferramenta fica para outro
    // agente, e o ramo de nomes do despacho consulta a tabela certa.
    {0x01001014u, "IUnzipAStream"},
    {0x01001056u, "ISound"},
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

// Os nomes dos slots do `IUnzipAStream` (`AEEUnzipStream.h`): cabeca
// `INHERIT_IAStream` = IBase(2) + Readable/Read/Cancel + SetStream.
const char* NomeDeUnzipStream(unsigned slot) {
  static const char* const k[] = {
      "AddRef", "Release", "Readable", "Read", "Cancel", "SetStream",
  };
  return slot < 6 ? k[slot] : "slot_fora_da_tabela";
}

// Os nomes dos slots do `IMemAStream` (`AEE.h`, `INHERIT_IMemAStream`):
// IAStream(2..4) + Set + SetEx.
const char* NomeDeMemStream(unsigned slot) {
  static const char* const k[] = {
      "AddRef", "Release", "Readable", "Read", "Cancel", "Set", "SetEx",
  };
  return slot < 7 ? k[slot] : "slot_fora_da_tabela";
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

// ---------------------------------------------------------------------------
// A FAMILIA DO IBITMAP (frente ibitmap). Ver o bloco no `interface.h`: a razao
// de cada slot, o que os 6+4 titulos pedem depois do slot13 e onde o despacho
// liga este servidor. Tudo o que este servidor precisa le e escreve no
// CABECALHO PUBLICO do IDIB na memoria do guest (`AEEIDIB.h:42-55`,
// `CamposDoIdib`); nao ha um mapa de bitmaps no hospedeiro, e e de proposito --
// a mesma fonte que o jogo usa (o `pBmp`, o `cx`, o `cy`) e a unica que nao
// pode divergir.
// ---------------------------------------------------------------------------
namespace {

// `AEEIID_IDIB` (`AEEIDIB.h:37`) -- e o MESMO `kIidDib` do `despacho.cpp`. Esta
// e a segunda copia, e a regra da casa diz que e uma segunda chance de divergir
// em silencio; o que a protege e o teste do QI (slot 2) em
// `tests/ecra_guest_test.cpp`: se divergir, o QI deixa de responder ao IID que
// o `pacmania` pede (0x01001045) e o teste fica vermelho.
constexpr std::uint32_t kIidDibFamilia = 0x01001045u;
// Os IIDs ANTIGOS do DIB (zeebx `src/machine.rs:729-740`: o 2.0 e o pre-2.0).
// O `IDIB` e um IBitmap onde a struct comeca pela vtable; a resposta e sempre
// o proprio objecto.
//
// A ORIGEM DE CADA UM Esta no SDK, e nao na memoria de ninguem:
//   `0x01001045` = `AEECLSID_DIB`      (`AEEClassIDs.h:157`, `CORE+69`);
//   `0x0100102c` = `AEECLSID_DIB_20`   (`:114`, "value of AEECLSID_DIB in 2.0");
//   `0x01001029` = `CORE+41`, hoje `AEECLSID_TRANSFORM` (`:111`) -- o valor do
//                  DIB de uma versao ANTERIOR do BREW, reciclado depois.
// MEDIDO (corrida de 62, esta frente): o `fifa09` e o `zenonia` fazem
// `CreateCompatibleBitmap` e a seguir `QI(0x01001029)`; sao os DOIS titulos que
// pedem esse IID (falta `IBitmap::QueryInterface iid=0x01001029`, 2x).
constexpr std::uint32_t kIidDib20 = 0x0100102cu;
constexpr std::uint32_t kIidDibAntigo = 0x01001029u;

// `AEERasterOp`, os tres que este servidor conhece. O `7` (`AEE_RO_TRANSPARENT`)
// e o do zeebx (`src/machine.rs:68-71`): com ele, o `FillRect` e o `BltIn`
// saltam os pixels da cor transparente da origem.
constexpr std::uint32_t kRopXor = 1;
constexpr std::uint32_t kRopCopy = 2;
constexpr std::uint32_t kRopTransparente = 7;

// Os 16 nomes do IBitmap, na ordem do `INHERIT_IBitmap` (`AEEIBitmap.h`; o
// gerador `tools/gerar_slots.py` produz a mesma lista para `brew_slots.inc`).
const char* kNomeDoSlotBitmap[16] = {
    "AddRef", "Release", "QueryInterface", "RGBToNative", "NativeToRGB",
    "DrawPixel", "GetPixel", "SetPixels", "DrawHScanline", "FillRect",
    "BltIn", "BltOut", "GetInfo", "CreateCompatibleBitmap",
    "SetTransparencyColor", "GetTransparencyColor",
};

// O que este servidor le do objecto, campo a campo do `CamposDoIdib`. Um
// bitmap tem `buffer` so depois de o cabecalho ser escrito (o ecra quando o
// jogo pede o pBmp, os `CreateDIBitmap`, os criados pelo slot13); sem buffer
// nao ha desenho possivel, e a recusa diz isso em vez de fingir.
struct DadosDoBitmap {
  std::uint32_t pvt = 0;
  std::uint32_t pbmp = 0;
  std::uint32_t transparente = 0;
  int largura = 0;
  int altura = 0;
  int profundidade = 0;
  bool com_buffer = false;
};

DadosDoBitmap LerDadosDoBitmap(Memoria& mem, std::uint32_t obj) {
  using C = CamposDoIdib;
  DadosDoBitmap d;
  d.pvt = mem.Ler32(obj + C::kPvt);
  d.pbmp = mem.Ler32(obj + C::kPBmp);
  d.transparente = mem.Ler32(obj + C::kNcTransparent);
  d.largura = static_cast<int>(static_cast<std::uint16_t>(mem.Ler16(obj + C::kCx)));
  d.altura = static_cast<int>(static_cast<std::uint16_t>(mem.Ler16(obj + C::kCy)));
  d.profundidade = static_cast<int>(mem.Ler8(obj + C::kNDepth));
  d.com_buffer = d.pvt != 0 && d.pbmp != 0 && d.largura > 0 && d.altura > 0;
  return d;
}

// O endereco do pixel (x,y) no buffer do DIB, ou 0 quando o buffer nao existe
// ou o ponto cai fora. O passo e `largura*2` (RGB565), o mesmo `nPitch` que o
// cabecalho publica.
std::uint32_t EnderecoDoPixelDo(const DadosDoBitmap& d, int x, int y) {
  if (!d.com_buffer || x < 0 || y < 0 || x >= d.largura || y >= d.altura) return 0;
  return d.pbmp +
         static_cast<std::uint32_t>(y * d.largura + x) * 2u;
}

void EscreverPixel(Memoria& mem, const DadosDoBitmap& d, int x, int y,
                   std::uint32_t cor565, std::uint32_t rop) {
  const std::uint32_t p = EnderecoDoPixelDo(d, x, y);
  if (p == 0) return;
  if (rop == kRopXor) {
    mem.Escrever16(p, static_cast<std::uint16_t>(mem.Ler16(p) ^ cor565));
  } else {
    mem.Escrever16(p, static_cast<std::uint16_t>(cor565 & 0xFFFFu));
  }
}

// A recusa com o NOME do metodo (P2): nunca "sucesso sem efeito", e o nome e o
// que a lista de demanda mostra.
void RecusarBitmap(Traco& traco, unsigned slot, const std::string& porque) {
  const char* nome = slot < 16 ? kNomeDoSlotBitmap[slot] : "slot_fora_da_tabela";
  traco.RegistarFalta(Area::Brew, "IBitmap::" + std::string(nome), porque);
}

// O argumento `i` da chamada: 0..3 em registos, o resto na pilha do guest.
std::uint32_t ArgDe(const ICpu& cpu, Memoria& mem, std::uint32_t sp, std::uint32_t i) {
  return i < 4 ? cpu.Get(kR0 + i) : mem.Ler32(sp + (i - 4) * 4u);
}

// Le uma struct `AEERect` (4 x int16: x, y, dx, dy) da memoria do guest.
// `false` com ponteiro nulo.
bool LerRect(Memoria& mem, std::uint32_t prc, int* x, int* y, int* dx, int* dy) {
  if (prc == 0) return false;
  *x = static_cast<int>(static_cast<std::int16_t>(mem.Ler16(prc + 0)));
  *y = static_cast<int>(static_cast<std::int16_t>(mem.Ler16(prc + 2)));
  *dx = static_cast<int>(static_cast<std::int16_t>(mem.Ler16(prc + 4)));
  *dy = static_cast<int>(static_cast<std::int16_t>(mem.Ler16(prc + 6)));
  return true;
}

// Um objecto DIB livre na faixa dos bitmaps (`kObjDibBase .. +0x1000`): o
// `kObjDibBase+0x300` e o ecra, e os `CreateDIBitmap` preenchem de baixo para
// cima -- o servico comeca ACIMA do ecra, onde so os bitmaps deste slot13 vao
// parar (o `CreateDIBitmap` precisaria de 13+ produtos para la chegar, e uma
// guarda recusa na fronteira da faixa). Um espaco livre tem o `+0` sem a
// vtable do bitmap (pagina nunca escrita = zeros).
std::uint32_t ProcurarObjectoLivre(Memoria& mem, std::uint32_t vtable_do_bitmap) {
  for (std::uint32_t obj = kObjDibBase + 0x340u; obj < kFimDosDibCompativeis; obj += 0x40u) {
    if (mem.Ler32(obj) != vtable_do_bitmap) return obj;
  }
  return 0;
}

// Escreve o cabecalho PUBLICO do IDIB para um bitmap novo: o mesmo layout do
// `Despacho::EscreverCabecalhoDeIdib` (`AEEIDIB.h:42-55`), campo a campo. Nao
// mexe na contagem de referencias do despacho (o `refs_do_dib_` vive la; o
// primeiro `AddRef` do guest cria a entrada, e o caminho e o mesmo).
void EscreverCabecalhoDeDibNovo(Memoria& mem, std::uint32_t obj, std::uint32_t vtable,
                                std::uint32_t pbmp, std::uint32_t largura,
                                std::uint32_t altura) {
  using C = CamposDoIdib;
  mem.Escrever32(obj + C::kPvt, vtable);
  mem.Escrever32(obj + C::kPPaletteMap, 0);  // NUNCA a contagem: o IDIB_FlushPalette desreferencia
  mem.Escrever32(obj + C::kPBmp, pbmp);
  mem.Escrever32(obj + C::kPRGB, 0);  // RGB565 e directo: nao ha paleta
  mem.Escrever32(obj + C::kNcTransparent, 0);
  mem.Escrever16(obj + C::kCx, static_cast<std::uint16_t>(largura));
  mem.Escrever16(obj + C::kCy, static_cast<std::uint16_t>(altura));
  mem.Escrever16(obj + C::kNPitch, static_cast<std::uint16_t>(largura * 2u));
  mem.Escrever16(obj + C::kCntRGB, 0);
  mem.Escrever8(obj + C::kNDepth, 16);
  mem.Escrever8(obj + C::kNColorScheme, C::kEsquemaDeCor565);
  for (std::uint32_t k = C::kReservado; k < C::kTamanho; ++k) mem.Escrever8(obj + k, 0);
}

// Copia um rectangulo de um DIB para outro, pixel a pixel (RGB565, sem
// escalonamento). `origem`/`destino` podem ser o ecra ou um DIB criado; a
// cor transparente e da ORIGEM e so salta quando o rop a pede.
void BlitEntreDIBs(Memoria& mem, const DadosDoBitmap& origem, const DadosDoBitmap& destino,
                   int xd, int yd, int dx, int dy, int xs, int ys, std::uint32_t rop) {
  if (!origem.com_buffer || !destino.com_buffer) return;
  if (dx <= 0 || dy <= 0) return;
  for (int j = 0; j < dy; ++j) {
    const int sy = ys + j, ddy = yd + j;
    for (int i = 0; i < dx; ++i) {
      const std::uint32_t ps = EnderecoDoPixelDo(origem, xs + i, sy);
      const std::uint32_t pd = EnderecoDoPixelDo(destino, xd + i, ddy);
      if (ps == 0 || pd == 0) continue;
      const std::uint16_t cor =
          static_cast<std::uint16_t>(origem.transparente & 0xFFFFu);
      if (rop == kRopTransparente && mem.Ler16(ps) == cor) continue;
      mem.Escrever16(pd, mem.Ler16(ps));
    }
  }
}

}  // namespace

bool IidDeDib(std::uint32_t iid) {
  return iid == kIidDibFamilia || iid == kIidDib20 || iid == kIidDibAntigo;
}

bool AtenderBitmapDaFamilia(ICpu& cpu, Memoria& mem, Alocador& al, Traco& traco,
                            std::uint32_t indice) {
  if (indice < kVtableBitmap + 2 || indice >= kVtableBitmap + 16) return false;
  const unsigned slot = indice - kVtableBitmap;
  const std::uint32_t po = cpu.Get(kR0);
  const std::uint32_t sp = cpu.Get(kSP);
  const auto arg = [&](std::uint32_t i) { return ArgDe(cpu, mem, sp, i); };

  switch (slot) {
    case 2: {
      // `int QueryInterface(IBitmap*, AEEIID, void**)`. O `IDIB` e o proprio
      // IBitmap com campos publicos (`AEEIDIB.h:57-60`): quem o pede vai LER o
      // cabecalho, logo ele tem de estar escrito antes de o ponteiro sair.
      // MEDIDO no `pacmania` (lr=0x12d94), a seguir ao slot13.
      const std::uint32_t iid = arg(1), ppo = arg(2);
      if (ppo == 0) {
        RecusarBitmap(traco, 2, "ppObj nulo");
        cpu.Set(kR0, kAeeBadParm);
        return true;
      }
      if (IidDeDib(iid)) {
        mem.Escrever32(ppo, po);
        cpu.Set(kR0, kAeeSuccess);
        return true;
      }
      mem.Escrever32(ppo, 0);
      char det[64];
      std::snprintf(det, sizeof(det), "iid=0x%08x", iid);
      RecusarBitmap(traco, 2, det);
      cpu.Set(kR0, kAeeUnsupported);
      return true;
    }
    case 3: {
      // `NativeColor RGBToNative(IBitmap*, RGBVAL rgb)`: a MAQUINA guia e o
      // RGB565 da Tela (a mesma conversao do IDisplay, `Tela::RgbvalPara565`).
      // MEDIDO no `tekken2` (lr=0x1ba8c): o jogo converte a paleta do BMP do
      // .bar e desenha com o resultado.
      cpu.Set(kR0, Tela::RgbvalPara565(arg(1)));
      return true;
    }
    case 4: {
      // `RGBVAL NativeToRGB(IBitmap*, NativeColor clr)`: 565 -> RGBVAL
      // (`r<<8 | g<<16 | b<<24`, byte baixo = alfa, como o zeebx
      // `to_rgbval` e o `MAKE_RGB` de `AEERGBVAL.h`).
      const std::uint32_t c = arg(1) & 0xFFFFu;
      const std::uint32_t r = (c >> 11) & 0x1Fu, g = (c >> 5) & 0x3Fu, b = c & 0x1Fu;
      cpu.Set(kR0, (r << (8 + 3)) | (g << (16 + 2)) | (b << (24 + 3)));
      return true;
    }
    case 5: {
      // `int DrawPixel(IBitmap*, unsigned x, unsigned y, NativeColor color, AEERasterOp rop)`.
      const DadosDoBitmap d = LerDadosDoBitmap(mem, po);
      if (!d.com_buffer) {
        RecusarBitmap(traco, 5, "bitmap sem buffer de pixels (cabecalho nunca escrito)");
        cpu.Set(kR0, kAeeUnsupported);
        return true;
      }
      EscreverPixel(mem, d, static_cast<int>(arg(1)), static_cast<int>(arg(2)), arg(3),
                    arg(4));
      cpu.Set(kR0, kAeeSuccess);
      return true;
    }
    case 6: {
      // `int GetPixel(IBitmap*, unsigned x, unsigned y, NativeColor *pColor)`.
      const DadosDoBitmap d = LerDadosDoBitmap(mem, po);
      const std::uint32_t p = EnderecoDoPixelDo(d, static_cast<int>(arg(1)),
                                                static_cast<int>(arg(2)));
      const std::uint32_t pc = arg(3);
      if (pc != 0) mem.Escrever32(pc, p == 0 ? 0 : mem.Ler16(p));
      cpu.Set(kR0, d.com_buffer ? kAeeSuccess : kAeeUnsupported);
      if (!d.com_buffer) RecusarBitmap(traco, 6, "bitmap sem buffer de pixels");
      return true;
    }
    case 7: {
      // `int SetPixels(IBitmap*, unsigned cnt, AEEPoint *pPoint, NativeColor color,
      //                AEERasterOp rop)`. `AEEPoint` = (int16 x, int16 y).
      const DadosDoBitmap d = LerDadosDoBitmap(mem, po);
      if (!d.com_buffer) {
        RecusarBitmap(traco, 7, "bitmap sem buffer de pixels");
        cpu.Set(kR0, kAeeUnsupported);
        return true;
      }
      const std::uint32_t n = arg(1), lista = arg(2);
      const std::uint32_t cor = arg(3) & 0xFFFFu, rop = arg(4);
      for (std::uint32_t k = 0; k < n; ++k) {
        const std::uint32_t p = lista + k * 4u;
        const int x = static_cast<int>(static_cast<std::int16_t>(mem.Ler16(p)));
        const int y = static_cast<int>(static_cast<std::int16_t>(mem.Ler16(p + 2)));
        EscreverPixel(mem, d, x, y, cor, rop);
      }
      cpu.Set(kR0, kAeeSuccess);
      return true;
    }
    case 8: {
      // `int DrawHScanline(IBitmap*, unsigned y, unsigned xMin, unsigned xMax,
      //                    NativeColor color, AEERasterOp rop)`.
      const DadosDoBitmap d = LerDadosDoBitmap(mem, po);
      if (!d.com_buffer) {
        RecusarBitmap(traco, 8, "bitmap sem buffer de pixels");
        cpu.Set(kR0, kAeeUnsupported);
        return true;
      }
      const int y = static_cast<int>(arg(1));
      int xmin = static_cast<int>(arg(2)), xmax = static_cast<int>(arg(3));
      if (xmax < xmin) { const int t = xmin; xmin = xmax; xmax = t; }
      const std::uint32_t cor = arg(4) & 0xFFFFu, rop = arg(5);
      for (int x = xmin; x <= xmax; ++x) EscreverPixel(mem, d, x, y, cor, rop);
      cpu.Set(kR0, kAeeSuccess);
      return true;
    }
    case 9: {
      // `int FillRect(IBitmap*, const AEERect *prc, NativeColor color, AEERasterOp rop)`.
      // Com `AEE_RO_TRANSPARENT` e a cor igual a transparente, NAO escreve nada
      // (o `IBITMAP_Invalidate` do SDK e um alias disto; o zeebx
      // `src/machine.rs:6321-6340` mede o caso no Bejeweled).
      const DadosDoBitmap d = LerDadosDoBitmap(mem, po);
      if (!d.com_buffer) {
        RecusarBitmap(traco, 9, "bitmap sem buffer de pixels");
        cpu.Set(kR0, kAeeUnsupported);
        return true;
      }
      int x = 0, y = 0, dx = 0, dy = 0;
      if (!LerRect(mem, arg(1), &x, &y, &dx, &dy)) {
        RecusarBitmap(traco, 9, "rect nulo");
        cpu.Set(kR0, kAeeBadParm);
        return true;
      }
      const std::uint32_t cor = arg(2) & 0xFFFFu, rop = arg(3);
      if (rop == kRopTransparente && cor == (d.transparente & 0xFFFFu)) {
        cpu.Set(kR0, kAeeSuccess);
        return true;
      }
      for (int j = 0; j < dy; ++j)
        for (int i = 0; i < dx; ++i) EscreverPixel(mem, d, x + i, y + j, cor, rop);
      cpu.Set(kR0, kAeeSuccess);
      return true;
    }
    case 10:
    case 11: {
      // `int BltIn(IBitmap*, int xDst, int yDst, int dx, int dy, IBitmap *pSrc,
      //            int xSrc, int ySrc, AEERasterOp rop)` e o `BltOut` com o
      // `pDst` no lugar do `pSrc` (origem = po). O rectangulo leva o mesmo
      // corte de limites dos dois lados; pixels fora ficam de fora.
      const std::uint32_t outro = arg(5);
      if (outro == 0) {
        RecusarBitmap(traco, slot, "ponteiro de bitmap nulo");
        cpu.Set(kR0, kAeeBadParm);
        return true;
      }
      const DadosDoBitmap destino = LerDadosDoBitmap(mem, po);
      const DadosDoBitmap origem =
          slot == 10 ? LerDadosDoBitmap(mem, outro) : destino;
      const DadosDoBitmap alvo =
          slot == 10 ? destino : LerDadosDoBitmap(mem, outro);
      if (!origem.com_buffer || !alvo.com_buffer) {
        RecusarBitmap(traco, slot, "blit entre bitmaps sem buffer de pixels");
        cpu.Set(kR0, kAeeUnsupported);
        return true;
      }
      // O ROP E A COR TRANSPARENTE DA ORIGEM sao os dois dados que decidem se um
      // blit COPIA ou SALTA: com `AEE_RO_TRANSPARENT` (7) e o pixel da origem
      // igual ao `ncTransparent` DELA, o destino nao e tocado (SDK,
      // `IBitmap_SetTransparencyColor`: "used when this bitmap is the source
      // bitmap of a transparent bit blit operation"). Sem esta medida nao se
      // sabe se um magenta no ecra e o jogo a pedir uma copia ou o emulador a
      // saltar a cor errada -- e foi essa a duvida que obrigou a instrumentar.
      traco.Emitir(Area::Video, Nivel::Informacao,
                   slot == 10 ? "BITMAP_BLTIN" : "BITMAP_BLTOUT",
                   "rop=" + std::to_string(arg(8)) + " origem.transparente=0x" +
                       Hex(origem.transparente & 0xFFFFu) + " dx=" +
                       std::to_string(arg(3)) + " dy=" + std::to_string(arg(4)));
      BlitEntreDIBs(mem, origem, alvo, static_cast<int>(arg(1)), static_cast<int>(arg(2)),
                    static_cast<int>(arg(3)), static_cast<int>(arg(4)),
                    static_cast<int>(arg(6)), static_cast<int>(arg(7)), arg(8));
      cpu.Set(kR0, kAeeSuccess);
      return true;
    }
    case 12: {
      // `int GetInfo(IBitmap*, AEEBitmapInfo *pinfo, int nSize)`. Os TRES u32 do
      // `AEEBitmapInfo` (`AEEIBitmap.h:34-38`), lidos DO CABECALHO DO OBJECTO --
      // e nao do ecra: o `pacmania` pergunta a dimensao do DIB que acabou de
      // criar. O guest diz quantos bytes conhece e escreve-se so esses.
      const DadosDoBitmap d = LerDadosDoBitmap(mem, po);
      const std::uint32_t pinfo = arg(1), nsize = arg(2);
      if (pinfo == 0) {
        cpu.Set(kR0, kAeeBadParm);
        return true;
      }
      const std::uint32_t campos[3] = {
          static_cast<std::uint32_t>(d.largura), static_cast<std::uint32_t>(d.altura),
          static_cast<std::uint32_t>(d.profundidade > 0 ? d.profundidade : 16),
      };
      for (std::uint32_t k = 0; k < 3 && (k + 1) * 4u <= nsize; ++k) {
        mem.Escrever32(pinfo + k * 4u, campos[k]);
      }
      cpu.Set(kR0, kAeeSuccess);
      return true;
    }
    case 13: {
      // `int CreateCompatibleBitmap(IBitmap *po, IBitmap **ppIBitmap, uint16 w,
      //                             uint16 h)` -- o slot que os 6 titulos pedem.
      // Assinatura e retornos do zeebx (`src/machine.rs:6389-6411`): um objecto
      // bitmap novo, `out` em r1, SUCCESS, ou ENOMEMORY quando nao ha espaco.
      // MEDIDO: sempre sobre o bitmap do ecra (0x80050300), 640x480 e 320x240
      // (tectoy/zeebo_app/fifa09/zenonia), ou as dimensoes do recurso acabado de
      // carregar (tekken2 508x86/14x14/161x95/640x230, pacmania 78x78 e as
      // LOADING_STAGE_IMAGES).
      const std::uint32_t pp = arg(1);
      const std::uint32_t w = arg(2) & 0xFFFFu, h = arg(3) & 0xFFFFu;
      if (pp != 0) mem.Escrever32(pp, 0);
      const std::uint32_t vtable_do_po = mem.Ler32(po);
      if (vtable_do_po == 0) {
        RecusarBitmap(traco, 13, "po sem vtable de bitmap");
        cpu.Set(kR0, kAeeUnsupported);
        return true;
      }
      if (w == 0 || h == 0) {
        char det[96];
        std::snprintf(det, sizeof(det), "%ux%u -- so ha caminho para DIB RGB565 de tamanho>0",
                      static_cast<unsigned>(w), static_cast<unsigned>(h));
        RecusarBitmap(traco, 13, det);
        cpu.Set(kR0, kAeeUnsupported);
        return true;
      }
      const std::uint64_t bytes64 = static_cast<std::uint64_t>(w) * 2u * h;
      if (bytes64 > 0x40000000u) {
        char det[96];
        std::snprintf(det, sizeof(det), "%ux%u fora do alcance do heap do guest",
                      static_cast<unsigned>(w), static_cast<unsigned>(h));
        RecusarBitmap(traco, 13, det);
        cpu.Set(kR0, kAeeNoMemory);
        return true;
      }
      const std::uint32_t bytes = static_cast<std::uint32_t>(bytes64);
      const std::uint32_t pixels = al.Malloc(bytes);
      if (pixels == 0) {
        char det[96];
        std::snprintf(det, sizeof(det), "sem heap para %ux%u (%u bytes)",
                      static_cast<unsigned>(w), static_cast<unsigned>(h),
                      static_cast<unsigned>(bytes));
        RecusarBitmap(traco, 13, det);
        cpu.Set(kR0, kAeeNoMemory);
        return true;
      }
      // Buffer a ZEROS (P4, determinismo): um DIB novo com lixo dentro faria
      // duas corridas iguais desenharem coisas diferentes -- a mesma regra do
      // `IDisplay::CreateDIBitmap`.
      const std::vector<std::uint8_t> zeros(bytes, 0);
      mem.EscreverBloco(pixels, zeros.data(), bytes);
      const std::uint32_t obj = ProcurarObjectoLivre(mem, vtable_do_po);
      if (obj == 0) {
        al.Free(pixels);
        RecusarBitmap(traco, 13, "faixa de objectos de bitmap cheia");
        cpu.Set(kR0, kAeeNoMemory);
        return true;
      }
      EscreverCabecalhoDeDibNovo(mem, obj, vtable_do_po, pixels, w, h);
      if (pp != 0) mem.Escrever32(pp, obj);
      cpu.Set(kR0, kAeeSuccess);
      return true;
    }
    case 14: {
      // `int SetTransparencyColor(IBitmap*, NativeColor color)` -- publicada no
      // `ncTransparent` do IDIB, que o `FillRect`/`BltIn` leem com rop
      // TRANSPARENT.
      traco.Emitir(Area::Video, Nivel::Informacao, "BITMAP_SETA_TRANSPARENCIA",
                   "po=0x" + Hex(po) + " cor=0x" + Hex(arg(1) & 0xFFFFu));
      mem.Escrever32(po + CamposDoIdib::kNcTransparent, arg(1) & 0xFFFFu);
      cpu.Set(kR0, kAeeSuccess);
      return true;
    }
    case 15: {
      // `int GetTransparencyColor(IBitmap*, NativeColor *pColor)`.
      const std::uint32_t pc = arg(1);
      if (pc == 0) {
        cpu.Set(kR0, kAeeBadParm);
        return true;
      }
      mem.Escrever32(pc, mem.Ler32(po + CamposDoIdib::kNcTransparent));
      cpu.Set(kR0, kAeeSuccess);
      return true;
    }
    default:
      return false;
  }
}


}  // namespace zb2::brew
