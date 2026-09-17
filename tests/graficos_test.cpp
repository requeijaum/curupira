#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/brew/ajudantes.h"
#include "core/brew/despacho.h"
#include "core/brew/ecra.h"
#include "core/brew/graficos.h"
#include "core/brew/interface.h"
#include "core/brew/vfs.h"
#include "core/brew/widget.h"
#include "core/cpu/arm_interpreter.h"
#include "core/cpu/cpu.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"
#include "tools/brew_slots.inc"

namespace zb2::brew {
namespace {

// Os nomes dos slots vem do `.inc` GERADO (o mesmo ficheiro que o motor usa):
// `kGraphics_*`, `kGraphicsSlots`.
using namespace brew_slots;

// ===========================================================================
// O `IGraphics`: a interface 2D do BREW 4.0, e o que o `allstarcards` pede 296
// vezes por slot em cada um destes quatro:
//
//     slot 4  SetColor      296x   r1=0x86 r2=0x69 r3=0xff   (alpha no [sp+0])
//     slot 6  SetFillMode   297x   r1=1
//     slot 8  SetFillColor  297x   r1=0x86 r2=0x69 r3=0xff   (alpha no [sp+0])
//     slot 22 DrawRect      296x   r1=0x8007fe60 (um rect na PILHA)
//
// (medido na corrida de referencia `/tmp/corrida_sw.json`; o detalhe cru esta na
// tabela de faltas da bateria, que imprime `r0..r3`, `sp0`/`sp1` e o `lr`).
//
// A PORTA DE ENTRADA DESTES TESTES E A TABELA, e nao um indice interno: o
// `ChamaPelaTabela` LE o ponteiro de funcao da vtable do objecto na memoria do
// guest e salta para la -- que e o que o titulo faz com o `blx ip`. Um teste que
// chamasse o endereco de saida directamente provava a ARITMETICA do modulo e
// nao a CABLAGEM dela. (Armadilha ja paga nesta arvore.)
// ===========================================================================

constexpr std::uint32_t kBase = 0x00000000u;
constexpr std::uint32_t kPilha = 0x80080000u;
constexpr std::uint32_t kHeap = 0x80200000u;
constexpr std::uint32_t kHeapTam = 0x00C00000u;
constexpr std::uint32_t kTabela = 0x80010000u;
constexpr std::uint32_t kBaseDasEntradas = 20000;
constexpr std::uint32_t kSentinela = 0xFFFFFFF0u;
constexpr std::uint32_t kArg0 = 0x80090000u;
constexpr std::uint32_t kShell = 0x80020000u;
// O endereco de saida do `GetDeviceBitmap`, COPIADO de `tools/bateria.cpp` (a
// tabela `kWire`) -- ver a nota na `Bancada`.
constexpr std::uint32_t kSaidaGetDeviceBitmap = 1550;
// Uma zona onde o teste escreve as structs do guest (o rect do `DrawRect`, a
// forma do `SetClip`, os ponteiros de saida dos `Get*`).
constexpr std::uint32_t kZona = 0x800A0000u;

// As cores do teste, escolhidas para os 565 serem DISTINTOS e reconheciveis:
//   azul  (0,0,255)   -> 0x001F
//   vermelho (255,0,0) -> 0xF800
//   verde (0,255,0)   -> 0x07E0
constexpr std::uint16_t kAzul565 = 0x001Fu;
constexpr std::uint16_t kVermelho565 = 0xF800u;
constexpr std::uint16_t kVerde565 = 0x07E0u;

class Bancada {
 public:
  // `com_ecra` = o titulo PEDIU o bitmap do ecra (e por isso o guest tem a
  // pagina 0x82000000). A omissao e o caminho dos titulos que desenham; o
  // `false` e o caminho MEDIDO do `allstarcards`, que nunca pede o bitmap.
  explicit Bancada(bool com_ecra = true) {
    saidas_.base = 0xF0000000u;
    saidas_.passo = 4;
    saidas_.quantos = 100000;  // o mesmo da bateria
    saidas_.ativa = true;
    cpu_.ConfigurarSaidas(saidas_);
    al_ = new Alocador(mem_, kHeap, kHeapTam, nullptr);
    despacho_ = new Despacho(mem_, traco_, *al_, vfs_);
    despacho_->DefinirVtableBitmap(saidas_);
    despacho_->DefinirVtableFicheiro(saidas_.Endereco(kVtableFileObj));
    // O `InstalarAjudantes` tambem constroi os widgets E o IGraphics.
    despacho_->InstalarAjudantes(saidas_, kTabela);
    despacho_->DefinirFaixaDoModulo(kBase, 0x00100000u);
    ConstruirObjeto(mem_, saidas_, kShell, saidas_.Endereco(kVtableShell),
                    kSlotsPorVtable, kBaseDoShell);
    // OS GENERICOS, escritos como a bateria os escreve
    // (`tools/bateria.cpp:768-770`): e o `ConstruirObjeto` do generico 3 que poe
    // a vtable que o `IGraphics` confere no primeiro pedido.
    for (std::uint32_t k = 0; k < kNGenericos; ++k) {
      ConstruirObjeto(mem_, saidas_, ObjGenerico(k), saidas_.Endereco(VtGenerico(k)),
                      kSlotsPorVtable, VtGenerico(k));
    }
    // O OBJECTO DO IDISPLAY: e por ele que o ecra ganha pagina no guest.
    ConstruirObjeto(mem_, saidas_, kObjDisplay, saidas_.Endereco(kVtableDisplay),
                    kSlotsPorVtable, kVtableDisplay);
    // A CABLAGEM DO `GetDeviceBitmap` -- um numero COPIADO da bateria
    // (`tools/bateria.cpp`, `kSlotIdGetDeviceBitmap = 1550`), pela mesma razao
    // que o `tests/ecra_guest_test.cpp` declara: a cablagem das vtables vive na
    // ferramenta, e o motor nao a exporta. Se ele divergir do que o `Despacho`
    // serve, o `BitmapDoEcra` devolve ZERO e estes testes ficam VERMELHOS.
    mem_.Escrever32(saidas_.Endereco(kVtableDisplay) +
                        brew_slots::kDisplay_GetDeviceBitmap * 4,
                    saidas_.Endereco(kSaidaGetDeviceBitmap));
    // O ECRA TEM DE EXISTIR ANTES DOS TESTES: e a memoria onde este modulo
    // escreve, e sem ela ele RECUSA-SE a desenhar (com o nome). Pedido pelo
    // caminho dos titulos, e nao por um atalho.
    if (com_ecra) {
      EXPECT_NE(BitmapDoEcra(), 0u) << "o ecra do guest nao ficou exposto";
      EXPECT_TRUE(mem_.Existe(kBaseDoEcraNoGuest));
    } else {
      EXPECT_FALSE(mem_.Existe(kBaseDoEcraNoGuest))
          << "sem ninguem pedir o bitmap do ecra nao ha pagina para o desenho 2D";
    }
    // A ZONA DE TESTE (o rect, a forma do clip, os ponteiros de saida): uma
    // pagina alocada por uma escrita, para um `Get*` nao cair em "ponteiro fora
    // do mapa" por uma razao que nao e a do teste.
    mem_.Escrever8(kZona, 0);
    cpu_.Repor(kBase, kPilha);
  }
  ~Bancada() {
    delete despacho_;
    delete al_;
  }

  // A VTABLE DO OBJECT0, lida da memoria (e nao um numero escrito aqui).
  std::uint32_t VtableDoIgfx() const { return mem_.Ler32(ObjGenerico(kIndiceDoGraphics)); }

  // O bitmap do ecra, PELO CAMINHO DOS TITULOS:
  // `IDISPLAY_GetDeviceBitmap` (slot 16) com um ponteiro de saida. E este pedido
  // que da pagina no guest ao buffer do ecra.
  std::uint32_t BitmapDoEcra() {
    const std::uint32_t pp = kZona + 0x200;
    mem_.Escrever32(pp, 0);
    const std::uint32_t vt = mem_.Ler32(kObjDisplay);
    const std::uint32_t destino = mem_.Ler32(vt + brew_slots::kDisplay_GetDeviceBitmap * 4u);
    cpu_.Set(kR0, kObjDisplay);
    cpu_.Set(kR1, pp);
    cpu_.Set(kR2, 0);
    cpu_.Set(kR3, 0);
    cpu_.Set(kSP, kArg0);
    cpu_.Set(kLR, kSentinela);
    cpu_.Set(kPC, destino);
    const ResultadoFase r = despacho_->Correr(cpu_, 1000, kArg0);
    (void)r;
    return mem_.Ler32(pp);
  }

  // A CHAMADA PELA TABELA: o PC vai para o que a vtable diz, como o `blx ip` do
  // titulo. `no_pilha` e o QUINTO argumento (o alfa de `SetColor`).
  std::uint32_t ChamaPelaTabela(std::uint32_t slot, std::uint32_t r0, std::uint32_t r1 = 0,
                                std::uint32_t r2 = 0, std::uint32_t r3 = 0,
                                std::uint32_t no_pilha = 0, std::uint64_t limite = 2000) {
    const std::uint32_t vt = VtableDoIgfx();
    const std::uint32_t alvo = mem_.Ler32(vt + slot * 4);
    mem_.Escrever32(kArg0, no_pilha);
    cpu_.Set(kR0, r0);
    cpu_.Set(kR1, r1);
    cpu_.Set(kR2, r2);
    cpu_.Set(kR3, r3);
    cpu_.Set(kSP, kArg0);
    cpu_.Set(kLR, kSentinela);
    cpu_.Set(kPC, alvo);
    const ResultadoFase r = despacho_->Correr(cpu_, limite, kArg0);
    (void)r;
    return cpu_.Get(kR0);
  }

  // Um pedido pelo objecto: `po` e sempre o generico 3.
  std::uint32_t Pede(std::uint32_t slot, std::uint32_t r1 = 0, std::uint32_t r2 = 0,
                     std::uint32_t r3 = 0, std::uint32_t no_pilha = 0) {
    return ChamaPelaTabela(slot, ObjGenerico(kIndiceDoGraphics), r1, r2, r3, no_pilha);
  }

  void EscreverRect(std::uint32_t onde, int x, int y, int largura, int altura) {
    EscreverRectDoGuest(mem_, onde, x, y, largura, altura);
  }

  // O pixel do ecra DO GUEST, no formato em que o titulo o leria pelo `pBmp`.
  std::uint16_t Pixel(int x, int y) const {
    return mem_.Ler16(kBaseDoEcraNoGuest +
                      (static_cast<std::uint32_t>(y) * kLarguraDoEcra +
                       static_cast<std::uint32_t>(x)) * 2u);
  }
  void PintarEcra(int x, int y, std::uint16_t cor) {
    mem_.Escrever16(kBaseDoEcraNoGuest +
                        (static_cast<std::uint32_t>(y) * kLarguraDoEcra +
                         static_cast<std::uint32_t>(x)) * 2u,
                    cor);
  }

  std::size_t Faltas(const std::string& nome) const {
    const auto& f = traco_.ContagemFaltas();
    const auto it = f.find(nome);
    return it == f.end() ? 0 : static_cast<std::size_t>(it->second);
  }
  std::size_t TotalDeFaltas() const {
    std::size_t n = 0;
    for (const auto& par : traco_.ContagemFaltas()) n += par.second;
    return n;
  }

  Memoria& Mem() { return mem_; }
  Despacho& D() { return *despacho_; }
  Graficos& G() { return despacho_->WidgetsRef().GraficosRef(); }
  const Saidas& S() const { return saidas_; }

 private:
  Memoria mem_;
  Traco traco_{"teste_graficos", nullptr};
  Vfs vfs_;
  Alocador* al_ = nullptr;
  Despacho* despacho_ = nullptr;
  Saidas saidas_;
  ArmInterpreter cpu_{mem_, &traco_};
};

// ---------------------------------------------------------------------------
// 1. O objecto e o IID, e o indice nao e um numero escolhido aqui.
// ---------------------------------------------------------------------------
TEST(Graficos, OIndiceEBatidoComAEECLSID_GRAPHICS) {
  // `AEEClassIDs.h:158`: `#define AEECLSID_GRAPHICS 0x01002001`. O numero vem da
  // TABELA (`kGenericos`), que e quem diz qual objecto o `CreateInstance` entrega
  // -- e nao de uma constante escrita neste ficheiro.
  ASSERT_LT(kIndiceDoGraphics, kNGenericos);
  EXPECT_EQ(kGenericos[kIndiceDoGraphics].iid, 0x01002001u);
  EXPECT_STREQ(kGenericos[kIndiceDoGraphics].nome, "IGraphics");
}

// ---------------------------------------------------------------------------
// 2. A TABELA: os 44 slots da interface apontam para a faixa de saida deste
//    modulo. Sem esta leitura, o modulo podia estar a atender indices que o
//    guest nunca usa.
// ---------------------------------------------------------------------------
TEST(Graficos, AVtableDoObjectoApontaParaAFaixaDeSaida) {
  Bancada b;
  const std::uint32_t objeto = ObjGenerico(kIndiceDoGraphics);
  EXPECT_EQ(objeto, 0x80060300u) << "o generico 3 e o objecto medido do IGraphics";
  const std::uint32_t vtable = b.VtableDoIgfx();
  EXPECT_EQ(vtable, b.S().Endereco(VtGenerico(kIndiceDoGraphics)));
  EXPECT_EQ(b.Mem().Ler32(vtable + 0), b.S().Endereco(3)) << "AddRef e do despacho";
  EXPECT_EQ(b.Mem().Ler32(vtable + 4), b.S().Endereco(4)) << "Release e do despacho";
  for (std::uint32_t slot = 2; slot < kGraphicsSlots; ++slot) {
    EXPECT_EQ(b.Mem().Ler32(vtable + slot * 4),
              b.S().Endereco(VtGenerico(kIndiceDoGraphics) + slot))
        << "slot " << slot << " (" << NomeDeGraphics(slot) << ")";
  }
  // E os dois getters do estado estao DENTRO da faixa que este modulo reclama.
  EXPECT_TRUE(b.D().WidgetsRef().GraficosRef().EMeu(VtGenerico(kIndiceDoGraphics) +
                                                    kGraphics_SetColor));
}

// ---------------------------------------------------------------------------
// 3. OS QUATRO PEDIDOS DO `allstarcards`, PELA TABELA, e sem faltas.
// ---------------------------------------------------------------------------
TEST(Graficos, OsQuatroPedidosDoAllstarcardsSaoServidosSemFalta) {
  Bancada b;
  const std::size_t antes = b.TotalDeFaltas();
  // Os argumentos sao os MEDIDOS (a cor 0x86/0x69/0xff e o r1=1 do modo de
  // preenchimento); o rect vai para a zona do teste.
  b.EscreverRect(kZona, 10, 20, 20, 10);
  EXPECT_EQ(b.Pede(kGraphics_SetColor, 0x86, 0x69, 0xff), 0xFF698600u)
      << "o retorno e MAKE_RGBA(r,g,b,a) = (r<<8)+(g<<16)+(b<<24)+a (AEERGBVAL.h:20), "
         "com o alfa MEDIDO a 0 no [sp+0]";
  EXPECT_EQ(b.Pede(kGraphics_SetFillMode, 1), 1u);
  EXPECT_EQ(b.Pede(kGraphics_SetFillColor, 0x86, 0x69, 0xff), 0xFF698600u);
  EXPECT_EQ(b.Pede(kGraphics_DrawRect, kZona), kAeeSuccess);
  // NENHUMA das quatro faltas com que a corrida de referencia conta.
  EXPECT_EQ(b.Faltas("IGraphics::slot4"), 0u);
  EXPECT_EQ(b.Faltas("IGraphics::slot6"), 0u);
  EXPECT_EQ(b.Faltas("IGraphics::slot8"), 0u);
  EXPECT_EQ(b.Faltas("IGraphics::slot22"), 0u);
  // E nenhuma falta com o nome do SDK: o pedido foi SERVIDO.
  EXPECT_EQ(b.Faltas("IGraphics::SetColor"), 0u);
  EXPECT_EQ(b.Faltas("IGraphics::SetFillMode"), 0u);
  EXPECT_EQ(b.Faltas("IGraphics::SetFillColor"), 0u);
  EXPECT_EQ(b.Faltas("IGraphics::DrawRect"), 0u);
  EXPECT_EQ(b.TotalDeFaltas(), antes) << "a bancada inteira devia estar calada";
}

// ---------------------------------------------------------------------------
// 4. O ESTADO: um `Set` que devolve SUCCESS sem guardar nada seria a mentira.
//    Cada `Set` devolve o valor posto, e cada `Get` devolve o GUARDADO.
// ---------------------------------------------------------------------------
TEST(Graficos, SetColorGuardaEGetColorDevolveOGuardado) {
  Bancada b;
  // O alfa e o QUINTO argumento, e o AAPCS poe-no na pilha (`ChamaPelaTabela`).
  // MAKE_RGBA(0x12,0x34,0x56,0xAB) = 0x563412AB -- o VERMELHO no segundo byte e o
  // alfa no byte baixo (AEERGBVAL.h:19-20). Escrito como literal de proposito: um
  // teste que reconstruisse a expressao nao apanhava uma troca de ordem.
  EXPECT_EQ(b.Pede(kGraphics_SetColor, 0x12, 0x34, 0x56, /*no_pilha=*/0xAB),
            0x563412ABu);
  // Os quatro ponteiros de saida do `GetColor`.
  const std::uint32_t pr = kZona + 0x40, pg = kZona + 0x44, pb = kZona + 0x48,
                     pa = kZona + 0x4C;
  for (std::uint32_t p : {pr, pg, pb, pa}) b.Mem().Escrever8(p, 0xEE);
  EXPECT_EQ(b.ChamaPelaTabela(kGraphics_GetColor, ObjGenerico(kIndiceDoGraphics), pr, pg, pb,
                              pa),
            0u);
  EXPECT_EQ(b.Mem().Ler8(pr), 0x12);
  EXPECT_EQ(b.Mem().Ler8(pg), 0x34);
  EXPECT_EQ(b.Mem().Ler8(pb), 0x56);
  EXPECT_EQ(b.Mem().Ler8(pa), 0xAB);
  EXPECT_EQ(b.TotalDeFaltas(), 0u);
}

TEST(Graficos, SetFillColorEGetFillColor) {
  Bancada b;
  EXPECT_EQ(b.Pede(kGraphics_SetFillColor, 0x01, 0x02, 0x03, /*no_pilha=*/0x04),
            0x03020104u);
  const std::uint32_t pr = kZona + 0x50, pg = kZona + 0x54, pb = kZona + 0x58,
                     pa = kZona + 0x5C;
  for (std::uint32_t p : {pr, pg, pb, pa}) b.Mem().Escrever8(p, 0xEE);
  EXPECT_EQ(b.ChamaPelaTabela(kGraphics_GetFillColor, ObjGenerico(kIndiceDoGraphics), pr, pg,
                              pb, pa),
            0u);
  EXPECT_EQ(b.Mem().Ler8(pr), 0x01);
  EXPECT_EQ(b.Mem().Ler8(pg), 0x02);
  EXPECT_EQ(b.Mem().Ler8(pb), 0x03);
  EXPECT_EQ(b.Mem().Ler8(pa), 0x04);
}

TEST(Graficos, SetBackgroundEGuardado) {
  Bancada b;
  EXPECT_EQ(b.Pede(kGraphics_SetBackground, 0x11, 0x22, 0x33), 0x33221100u)
      << "MAKE_RGB (sem alfa)";
  const std::uint32_t pr = kZona + 0x60, pg = kZona + 0x64, pb = kZona + 0x68;
  EXPECT_EQ(b.Pede(kGraphics_GetBackground, pr, pg, pb), 0u);
  EXPECT_EQ(b.Mem().Ler8(pr), 0x11);
  EXPECT_EQ(b.Mem().Ler8(pg), 0x22);
  EXPECT_EQ(b.Mem().Ler8(pb), 0x33);
}

TEST(Graficos, SetFillModeDesligaComValorInvalido) {
  Bancada b;
  EXPECT_EQ(b.Pede(kGraphics_SetFillMode, 1), 1u);
  EXPECT_EQ(b.Pede(kGraphics_GetFillMode), 1u);
  // "If an invalid value (other than TRUE or FALSE) is passed as input
  // parameter, the fill mode is set to FALSE by default" (AEEGraphics.h).
  EXPECT_EQ(b.Pede(kGraphics_SetFillMode, 7), 0u);
  EXPECT_EQ(b.Pede(kGraphics_GetFillMode), 0u);
}

TEST(Graficos, SetPointSizeEGetPointSize) {
  Bancada b;
  EXPECT_EQ(b.Pede(kGraphics_SetPointSize, 5), 5u);
  EXPECT_EQ(b.Pede(kGraphics_GetPointSize), 5u);
}

TEST(Graficos, SetPaintModeInvalidoViraCopy) {
  Bancada b;
  EXPECT_EQ(b.Pede(kGraphics_SetPaintMode, 1), 1u) << "AEE_PAINT_XOR";
  EXPECT_EQ(b.Pede(kGraphics_GetPaintMode), 1u);
  EXPECT_EQ(b.Pede(kGraphics_SetPaintMode, 9), 0u) << "invalido -> AEE_PAINT_COPY";
  EXPECT_EQ(b.Pede(kGraphics_GetPaintMode), 0u);
}

TEST(Graficos, GetColorDepthEDezasseis) {
  // RGB565: o `zeebx` (`display.rs:431`, `COLOR_DEPTH` = 16) e o `zeemu`
  // (`BrewGraphics.cpp:245`) respondem 16, e o EGL daqui declara o mesmo.
  Bancada b;
  EXPECT_EQ(b.Pede(kGraphics_GetColorDepth), 16u);
}

// ---------------------------------------------------------------------------
// 5. O `DrawRect`: o desenho a serio, com a cor de frente e a de preenchimento.
// ---------------------------------------------------------------------------
TEST(Graficos, DrawRectPreencheComACorDePreenchimentoEMolduraComACorDeFrente) {
  Bancada b;
  // O FUNDO DO TESTE: cinzento reconhecivel, para um pixel nao escrito se
  // distinguir de um escrito por acidente.
  for (int j = 0; j < 40; ++j) {
    for (int i = 0; i < 40; ++i) b.PintarEcra(i, j, 0x1234u);
  }
  b.Pede(kGraphics_SetColor, 0, 0, 255, 0);      // azul
  b.Pede(kGraphics_SetFillColor, 255, 0, 0, 0);  // vermelho
  b.Pede(kGraphics_SetFillMode, 1);
  b.EscreverRect(kZona, 10, 20, 20, 10);
  EXPECT_EQ(b.Pede(kGraphics_DrawRect, kZona), kAeeSuccess);
  // A MOLDURA tem a cor de FRENTE (azul)...
  EXPECT_EQ(b.Pixel(10, 20), kAzul565) << "canto superior esquerdo";
  EXPECT_EQ(b.Pixel(29, 20), kAzul565) << "canto superior direito";
  EXPECT_EQ(b.Pixel(10, 29), kAzul565) << "canto inferior esquerdo";
  // ...e o INTERIOR tem a de PREENCHIMENTO (vermelho).
  EXPECT_EQ(b.Pixel(20, 25), kVermelho565);
  EXPECT_EQ(b.Pixel(11, 21), kVermelho565) << "o interior comeca a seguir a moldura";
  // O QUE FICA FORA NAO FOI TOCADO.
  EXPECT_EQ(b.Pixel(9, 20), 0x1234u);
  EXPECT_EQ(b.Pixel(30, 20), 0x1234u);
  EXPECT_EQ(b.Pixel(10, 30), 0x1234u);
  EXPECT_EQ(b.G().Resumo().rectangulos, 1u);
}

TEST(Graficos, DrawRectSemPreenchimentoSoDesenhaAMoldura) {
  Bancada b;
  for (int j = 0; j < 40; ++j) {
    for (int i = 0; i < 40; ++i) b.PintarEcra(i, j, 0x1234u);
  }
  b.Pede(kGraphics_SetColor, 0, 0, 255, 0);      // azul
  b.Pede(kGraphics_SetFillColor, 255, 0, 0, 0);  // vermelho (nao deve aparecer)
  b.Pede(kGraphics_SetFillMode, 0);
  b.EscreverRect(kZona, 10, 20, 20, 10);
  EXPECT_EQ(b.Pede(kGraphics_DrawRect, kZona), kAeeSuccess);
  EXPECT_EQ(b.Pixel(10, 20), kAzul565);
  EXPECT_EQ(b.Pixel(20, 25), 0x1234u) << "o interior fica com o que la estava";
  EXPECT_EQ(b.Pixel(20, 25), 0x1234u) << "e NAO com a cor de preenchimento";
}

TEST(Graficos, ODesenhoVaiParaOMesmoBufferDoEcraQueO3DUsa) {
  // O pixel tem de ficar no buffer do ecra do guest (`kBaseDoEcraNoGuest`), que
  // e a memoria que o `pBmp` do IDIB do ecra aponta: e assim que o 2D fica
  // visivel a quem le o ecra, e e o mesmo sitio onde o `ExporEcraAoGuest` poe o
  // que o 3D desenhou.
  Bancada b;
  b.Pede(kGraphics_SetColor, 0, 255, 0, 0);  // verde
  b.EscreverRect(kZona, 3, 4, 2, 2);
  b.Pede(kGraphics_DrawRect, kZona);
  const std::uint32_t onde = kBaseDoEcraNoGuest + (4u * kLarguraDoEcra + 3u) * 2u;
  EXPECT_EQ(b.Mem().Ler16(onde), kVerde565);
  EXPECT_GT(b.G().PixeisEscritos(), 0u);
  EXPECT_TRUE(b.G().TemTela());
  // A moldura de um rect 2x2 mexe 4 pixeis -- e faz 8 ESCRITAS: as duas passagens
  // do `draw_frame` escrevem os quatro cantos duas vezes, que e o que o `zeebx`
  // (`src/video/display.rs:179-193`) e o `zeemu` tambem fazem. O contador conta
  // ESCRITAS, e nao pixeis distintos.
  EXPECT_EQ(b.G().PixeisEscritos(), 8u);
}

// ---------------------------------------------------------------------------
// 6. O clip, o viewport e o modo de pintura sao APLICADOS (e nao so guardados)
// ---------------------------------------------------------------------------
TEST(Graficos, OClipRectangularLimitaODesenho) {
  Bancada b;
  for (int j = 0; j < 40; ++j) {
    for (int i = 0; i < 40; ++i) b.PintarEcra(i, j, 0x1234u);
  }
  // `AEEClip`: `type` (int8) no +0 e o `union` no +2 (ver `graficos.h`).
  const std::uint32_t forma = kZona + 0x100;
  for (std::uint32_t k = 0; k < kTamanhoDoClip + 8; ++k) b.Mem().Escrever8(forma + k, 0x77);
  b.Mem().Escrever8(forma + kClipTipo, kClipRectangulo);
  // O CLIP APANHA A MOLDURA E O INTERIOR: x 10..15, y 20..23.
  b.EscreverRect(forma + kClipRect, 10, 20, 6, 4);
  EXPECT_EQ(b.Pede(kGraphics_SetClip, forma, 0), 1u);
  b.Pede(kGraphics_SetFillMode, 1);
  b.Pede(kGraphics_SetColor, 0, 0, 255, 0);
  b.Pede(kGraphics_SetFillColor, 255, 0, 0, 0);
  b.EscreverRect(kZona, 10, 20, 20, 10);  // maior do que o clip
  b.Pede(kGraphics_DrawRect, kZona);
  // Dentro do clip: desenhado. Fora: intacto.
  EXPECT_EQ(b.Pixel(10, 20), kAzul565) << "a moldura, dentro do clip";
  EXPECT_EQ(b.Pixel(12, 22), kVermelho565) << "o interior, dentro do clip";
  EXPECT_EQ(b.Pixel(9, 20), 0x1234u) << "a esquerda do clip";
  EXPECT_EQ(b.Pixel(16, 20), 0x1234u) << "a direita do clip (10+6=16)";
  EXPECT_EQ(b.Pixel(12, 24), 0x1234u) << "abaixo do clip (20+4=24)";
  EXPECT_EQ(b.Pixel(10, 29), 0x1234u)
      << "a moldura de baixo do rect caiu FORA do clip -- e por isso nao foi escrita";
}

TEST(Graficos, OGetClipDevolveOClipGuardadoEnaoTocaNoVizinho) {
  Bancada b;
  const std::uint32_t forma = kZona + 0x140;
  for (std::uint32_t k = 0; k < kTamanhoDoClip + 8; ++k) b.Mem().Escrever8(forma + k, 0x77);
  b.Mem().Escrever8(forma + kClipTipo, kClipRectangulo);
  b.EscreverRect(forma + kClipRect, 7, 8, 9, 10);
  b.Pede(kGraphics_SetClip, forma, 0);
  // O GET escreve noutro sitio, tambem com sentinela.
  const std::uint32_t destino = kZona + 0x180;
  for (std::uint32_t k = 0; k < kTamanhoDoClip + 8; ++k) b.Mem().Escrever8(destino + k, 0x77);
  EXPECT_EQ(b.Pede(kGraphics_GetClip, destino), 1u);
  EXPECT_EQ(b.Mem().Ler8(destino + kClipTipo), kClipRectangulo);
  int x = 0, y = 0, largura = 0, altura = 0;
  ASSERT_TRUE(LerRectDoGuest(b.Mem(), destino + kClipRect, &x, &y, &largura, &altura));
  EXPECT_EQ(x, 7);
  EXPECT_EQ(y, 8);
  EXPECT_EQ(largura, 9);
  EXPECT_EQ(altura, 10);
  // A SENTINELA: o que este modulo escreve sao 10 bytes, e nao mais.
  EXPECT_EQ(b.Mem().Ler8(destino + kTamanhoDoClip), 0x77)
      << "escreveu para la dos 10 bytes da AEEClip";
}

TEST(Graficos, OViewportForaDoEcraRecusaFalsoESemRegistarFalta) {
  Bancada b;
  b.EscreverRect(kZona, 600, 400, 100, 100);  // 700x500 nao cabe em 640x480
  EXPECT_EQ(b.Pede(kGraphics_SetViewport, kZona, 0), 0u)
      << "o cabecalho manda FALSE e as definicoes anteriores ficam";
  EXPECT_EQ(b.G().Resumo().viewports_recusados, 1u);
  // UMA RECUSA DE CONTRATO NAO E UMA FALTA: registar falta aqui seria acusar o
  // modulo de algo que o proprio cabecalho declara.
  EXPECT_EQ(b.TotalDeFaltas(), 0u);
  // E o viewport anterior (a janela) continua a valer.
  EXPECT_EQ(b.G().Resumo().viewport_largura, kLarguraDoEcra);
  // Um viewport que caiba e aceite.
  b.EscreverRect(kZona, 10, 10, 100, 100);
  EXPECT_EQ(b.Pede(kGraphics_SetViewport, kZona, 0), 1u);
  EXPECT_EQ(b.G().Resumo().viewport_largura, 100u);
}

TEST(Graficos, OModoDePinturaXorFazXorComOPixelQueLaEsta) {
  Bancada b;
  for (int j = 0; j < 16; ++j) {
    for (int i = 0; i < 16; ++i) b.PintarEcra(i, j, 0x0F0Fu);
  }
  b.Pede(kGraphics_SetPaintMode, 1);  // AEE_PAINT_XOR
  b.Pede(kGraphics_SetFillMode, 0);   // so a moldura, para a contagem ser clara
  b.Pede(kGraphics_SetColor, 0, 0, 255, 0);
  b.EscreverRect(kZona, 5, 5, 3, 3);
  b.Pede(kGraphics_DrawRect, kZona);
  // UM PIXEL DA ARESTA leva UMA escrita: novo = antigo XOR pedido.
  EXPECT_EQ(b.Pixel(6, 5), static_cast<std::uint16_t>(0x0F0Fu ^ kAzul565))
      << "novo = antigo XOR pedido (AEEGraphics.h, SetPaintMode)";
  // UM CANTO leva DUAS (a moldura passa duas vezes por ele, como o `draw_frame`
  // do `zeebx`): o XOR de dois pedidos iguais ANULA-SE, e o pixel fica como
  // estava. Fica medido -- e dito -- porque nenhum dos dois emuladores de
  // referencia implementa o modo XOR e nao ha a quem o ir buscar.
  EXPECT_EQ(b.Pixel(5, 5), 0x0F0Fu)
      << "os cantos levam duas escritas do mesmo valor: o XOR anula-se";
  // E em COPY o pixel e simplesmente substituido (uma vez ou duas, da o mesmo).
  b.Pede(kGraphics_SetPaintMode, 0);
  b.EscreverRect(kZona, 5, 5, 3, 3);
  b.Pede(kGraphics_DrawRect, kZona);
  EXPECT_EQ(b.Pixel(6, 5), kAzul565);
  EXPECT_EQ(b.Pixel(5, 5), kAzul565) << "sem XOR, os cantos ficam com a cor pedida";
}

// ---------------------------------------------------------------------------
// 7. O QUE NAO SE IMPLEMENTA RECUSA COM O NOME (P2)
// ---------------------------------------------------------------------------
TEST(Graficos, AsFormasNaoImplementadasRecusamComONome) {
  Bancada b;
  b.EscreverRect(kZona, 1, 2, 3, 4);
  EXPECT_EQ(b.Pede(kGraphics_DrawPoint, kZona), kAeeUnsupported);
  EXPECT_EQ(b.Pede(kGraphics_DrawLine, kZona), kAeeUnsupported);
  EXPECT_EQ(b.Pede(kGraphics_DrawCircle, kZona), kAeeUnsupported);
  EXPECT_EQ(b.Pede(kGraphics_DrawEllipse, kZona), kAeeUnsupported);
  EXPECT_EQ(b.Pede(kGraphics_ClearRect, kZona), kAeeUnsupported);
  EXPECT_EQ(b.Pede(kGraphics_StretchBlt), kAeeUnsupported);
  // O NOME, e nao o numero do slot: uma lista de demanda que diga
  // `IGraphics::slot23` obriga a ir ao cabecalho contar em cada ronda.
  EXPECT_EQ(b.Faltas("IGraphics::DrawCircle"), 1u);
  EXPECT_EQ(b.Faltas("IGraphics::DrawLine"), 1u);
  EXPECT_EQ(b.Faltas("IGraphics::DrawPoint"), 1u);
  EXPECT_EQ(b.Faltas("IGraphics::ClearRect"), 1u);
  EXPECT_EQ(b.Faltas("IGraphics::slot23"), 0u) << "a recusa tem de ter NOME";
  // E o `DrawRect` continua a ser servido (nao caiu na tabela de recusa).
  EXPECT_EQ(b.Faltas("IGraphics::DrawRect"), 0u);
}

TEST(Graficos, UmaFormaDeClipNaoImplementadaRecusaComONome) {
  Bancada b;
  const std::uint32_t forma = kZona + 0x1C0;
  b.Mem().Escrever8(forma + kClipTipo, 2);  // CLIPPING_CIRCLE
  EXPECT_EQ(b.Pede(kGraphics_SetClip, forma, 0), 0u)
      << "nao se guarda um clip que nao se aplica";
  EXPECT_EQ(b.Faltas("IGraphics::SetClip forma nao implementada"), 1u);
}

TEST(Graficos, TranslateGuardaOrigemETransladaODesenho) {
  Bancada b;
  // Translate define a origem em (10, 20)
  EXPECT_EQ(b.Pede(kGraphics_Translate, 10, 20), 0u);
  EXPECT_EQ(b.G().Resumo().origem_x, 10);
  EXPECT_EQ(b.G().Resumo().origem_y, 20);
  EXPECT_EQ(b.Faltas("IGraphics::Translate"), 0u);

  // Desenhar rect em (5, 5, 2, 2) deve desenhar em (15, 25, 2, 2)
  b.Pede(kGraphics_SetColor, 0, 0, 0xFF, 0xFF);  // azul
  b.EscreverRect(kZona, 5, 5, 2, 2);
  EXPECT_EQ(b.Pede(kGraphics_DrawRect, kZona), kAeeSuccess);
  EXPECT_EQ(b.Pixel(15, 25), kAzul565);
  EXPECT_EQ(b.Pixel(5, 5), 0x0000u) << "sem translate a posicao original nao foi afetada";
}

TEST(Graficos, SetDestinationEGetDestinationGuardamODestino) {
  Bancada b;
  constexpr std::uint32_t kNovoDestino = 0x80050340u;

  EXPECT_EQ(b.Pede(kGraphics_SetDestination, kNovoDestino), kAeeSuccess);
  EXPECT_EQ(b.Pede(kGraphics_GetDestination), kNovoDestino);
  EXPECT_EQ(b.Faltas("IGraphics::SetDestination"), 0u);
}

TEST(Graficos, OPonteiroDeSaidaForaDoMapaNaoAlocaMemoria) {
  Bancada b;
  b.Pede(kGraphics_SetColor, 1, 2, 3, 0);
  // 0x40000000 nao esta mapeado. Um `Escrever8` cego ALOCAVA a pagina ali.
  EXPECT_FALSE(b.Mem().Existe(0x40000000u));
  // O `GetColor` e `void` na ABI: devolve 0 e o que interessa e NAO ter escrito.
  EXPECT_EQ(b.Pede(kGraphics_GetColor, 0x40000000u, 0x40000004u, 0x40000008u, 0x4000000Cu),
            0u);
  EXPECT_FALSE(b.Mem().Existe(0x40000000u))
      << "o GetColor ALOCAVA memoria do guest num ponteiro invalido";
  EXPECT_EQ(b.Faltas("IGraphics::GetColor ponteiro fora do mapa"), 1u);
}

// ---------------------------------------------------------------------------
// 8. Uma diferenca de UM byte: a `AEEClip` tem 10 bytes e a `AEERect` 8
// ---------------------------------------------------------------------------
TEST(Graficos, ATabelaDosNomesVemDoGerador) {
  // Os quatro slots MEDIDOS do `allstarcards`, pelo nome que o `.inc` gerado
  // lhes da. Se um dia o cabecalho mudar de ordem, e aqui que se ve.
  EXPECT_EQ(kGraphics_SetColor, 4u);
  EXPECT_EQ(kGraphics_SetFillMode, 6u);
  EXPECT_EQ(kGraphics_SetFillColor, 8u);
  EXPECT_EQ(kGraphics_DrawRect, 22u);
  EXPECT_STREQ(NomeDeGraphics(kGraphics_SetColor), "SetColor");
  EXPECT_STREQ(NomeDeGraphics(kGraphics_SetFillMode), "SetFillMode");
  EXPECT_STREQ(NomeDeGraphics(kGraphics_SetFillColor), "SetFillColor");
  EXPECT_STREQ(NomeDeGraphics(kGraphics_DrawRect), "DrawRect");
  EXPECT_EQ(kGraphicsSlots, 44u);
  EXPECT_EQ(kTamanhoDoClip, 10u) << "AEEClip = int8 type + union(AEERect) alinhado a 2";
}

}  // namespace

// ---------------------------------------------------------------------------
// 9. SEM DESTINO: e ESTE o caso medido no corpus (o `allstarcards` nunca pede o
//    bitmap do ecra, portanto a pagina 0x82000000 nao existe). O pedido deixa de
//    ser um `slot22` anonimo e passa a ter NOME -- e o nome diz o que falta.
// ---------------------------------------------------------------------------
TEST(Graficos, SemDestinoODrawRectRecusaComONomeEOsSlotsDeEstadoSaoServidos) {
  Bancada b(/*com_ecra=*/false);
  // A CABLAGEM DO DESPACHO CRIA O DESTINO A PEDIDO (`DefinirCriadorDoEcra`, ligado
  // no `InstalarWidgets`), e e isso que faz o caminho do `allstarcards` -- 296
  // `DrawRect` e zero pedidos de bitmap do ecra -- desenhar em vez de recusar. Este
  // teste mede o OUTRO contrato: um modulo SEM via para criar o ecra. Tira-se-lhe a
  // via e ele tem de RECUSAR COM O NOME (e nao escrever num sitio nenhum).
  b.G().DefinirCriadorDoEcra(nullptr);
  ASSERT_FALSE(b.G().TemTela());
  const std::size_t antes = b.TotalDeFaltas();
  // OS SLOTS DE ESTADO NAO DEPENDEM DE DESTINO NENHUM: sao servidos na mesma.
  EXPECT_EQ(b.Pede(kGraphics_SetColor, 0x86, 0x69, 0xff), 0xFF698600u);
  EXPECT_EQ(b.Pede(kGraphics_SetFillMode, 1), 1u);
  EXPECT_EQ(b.Pede(kGraphics_SetFillColor, 0x86, 0x69, 0xff), 0xFF698600u);
  EXPECT_EQ(b.TotalDeFaltas(), antes) << "o estado nao tem de custar falta nenhuma";
  // O DESENHO NAO TEM ONDE SER FEITO, e di-lo pelo NOME.
  b.EscreverRect(kZona, 272, 355, 206, 60);
  EXPECT_EQ(b.Pede(kGraphics_DrawRect, kZona), kAeeUnsupported);
  EXPECT_EQ(b.Faltas("IGraphics::DrawRect sem destino"), 1u);
  EXPECT_EQ(b.Faltas("IGraphics::slot22"), 0u)
      << "o slot 22 ja e reclamado pelo modulo: a falta agora tem nome";
  EXPECT_EQ(b.G().PixeisEscritos(), 0u) << "sem destino nao se escreve pixel nenhum";
  EXPECT_EQ(b.G().Resumo().rectangulos, 1u) << "o pedido foi CONTADO";
  // E o rect fica no registo (e a medicao do sitio onde o titulo desenha).
  EXPECT_EQ(b.Mem().Ler16(kZona + 4), 206u);
}


TEST(Graficos, ComACablagemODrawRectCriaOEcraDoGuestEDesenha) {
  // O CAMINHO QUE FALTAVA. O `allstarcards` desenha 296 rectangulos por quadro e
  // nunca pede o bitmap do ecra; a pagina 0x82000000 so nascia a pedido, logo o
  // desenho 2D nao tinha destino. Com o criador ligado (o `Despacho`), o ecra
  // existe quando o titulo o usa -- e o destino e o MESMO dos outros (o guest
  // escreve no buffer do ecra e a `Tela` absorve-o no `Update`), nao um segundo
  // framebuffer.
  Bancada b(/*com_ecra=*/false);
  // O `tem_tela_` e decidido no PRIMEIRO desenho (e nao no construtor): antes disso
  // a pergunta ainda nao foi feita, e afirma-lo aqui seria testar a omissao.
  b.Pede(kGraphics_SetColor, 0xff, 0x00, 0x00);
  b.Pede(kGraphics_SetFillColor, 0x00, 0xff, 0x00);
  b.Pede(kGraphics_SetFillMode, 1);
  b.EscreverRect(kZona, 100, 200, 40, 20);
  EXPECT_EQ(b.Pede(kGraphics_DrawRect, kZona), kAeeSuccess);
  EXPECT_EQ(b.Faltas("IGraphics::DrawRect sem destino"), 0u)
      << "com destino nao ha falta nenhuma";
  EXPECT_TRUE(b.G().TemTela()) << "a cablagem do despacho cria o ecra a pedido";
  EXPECT_GT(b.G().PixeisEscritos(), 0u) << "o rect tem de escrever pixeis";
  EXPECT_EQ(b.G().Resumo().rectangulos, 1u);
}

}  // namespace zb2::brew