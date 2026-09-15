#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/brew/despacho.h"
#include "core/brew/ecra.h"
#include "core/brew/interface.h"
#include "core/brew/tela.h"
#include "core/cpu/arm_interpreter.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"
#include "tools/brew_slots.inc"

namespace zb2::brew {
namespace {

// ===========================================================================
// O ECRA VISTO PELO GUEST (`IDIB::pBmp`).
//
// O QUE ESTES TESTES PROVAM, e porque cada um existe:
//
//   1. O `pBmp` do bitmap do ecra aponta para memoria do GUEST, e o cabecalho
//      publico a volta dele (cx, cy, nPitch, nDepth) descreve essa memoria. Era
//      zero, e zero nesta arvore e a base do modulo do proprio titulo.
//   2. Um pixel escrito pelo guest NESSE endereco chega a `Tela` no
//      `IDisplay::Update` -- que e o instante em que o BREW mostra o quadro.
//   3. O desenho NOSSO (2D do IDisplay) fica visivel ao guest no mesmo buffer:
//      um titulo que componha por cima do ecra le o que la esta, e nao zeros.
//
// A CABLAGEM E LIDA DA TABELA, nunca chamada pelo indice interno. Chamar
// `ChamaSaida(kSlotIdGetDeviceBitmap)` entraria no ramo do despacho sem provar
// que a vtable do `IDisplay` aponta para la -- e a vtable e metade do que pode
// estar errado.
// ===========================================================================

constexpr std::uint32_t kBase = 0x00000000u;
constexpr std::uint32_t kPilha = 0x80080000u;
constexpr std::uint32_t kHeap = 0x80200000u;
constexpr std::uint32_t kHeapTam = 0x00C00000u;
constexpr std::uint32_t kTabela = 0x80010000u;
constexpr std::uint32_t kSentinela = 0xFFFFFFF0u;
constexpr std::uint32_t kArg0 = 0x80090000u;
constexpr std::uint32_t kTamanhoDoModulo = 0x00100000u;

class Bancada {
 public:
  Bancada() {
    saidas_.base = 0xF0000000u;
    saidas_.passo = 4;
    saidas_.quantos = 100000;
    saidas_.ativa = true;
    cpu_.ConfigurarSaidas(saidas_);
    al_ = new Alocador(mem_, kHeap, kHeapTam, nullptr);
    despacho_ = new Despacho(mem_, traco_, *al_, vfs_);
    despacho_->DefinirVtableBitmap(saidas_);
    despacho_->DefinirVtableFicheiro(saidas_.Endereco(kVtableFileObj));
    despacho_->InstalarAjudantes(saidas_, kTabela);
    despacho_->DefinirFaixaDoModulo(kBase, kTamanhoDoModulo);
    // O OBJECTO DO IDISPLAY, construido como a bateria o constroi: e dele que
    // sai a vtable que estes testes leem.
    ConstruirObjeto(mem_, saidas_, kObjDisplay, saidas_.Endereco(kVtableDisplay),
                    kSlotsPorVtable, kVtableDisplay);
    // A CABLAGEM DOS DOIS SLOTS QUE ESTES TESTES USAM.
    //
    // OS NUMEROS SAO COPIA de `tools/bateria.cpp` (a tabela `kWire`, linhas 658 e
    // 676, e as constantes `kSlotId*` da linha 293): a cablagem das vtables vive
    // na FERRAMENTA, nao no motor, e o motor nao a exporta. **Uma segunda copia
    // de um id de slot ja custou uma corrida inteira a este projecto.**
    //
    // O que protege esta copia e o proprio teste: se o id divergir do que o
    // `Despacho` serve, a saida cablada nao entra em ramo nenhum, o
    // `GetDeviceBitmap` devolve ZERO e os testes abaixo ficam VERMELHOS -- foi
    // assim que este ficheiro comecou.
    mem_.Escrever32(saidas_.Endereco(kVtableDisplay) + brew_slots::kDisplay_GetDeviceBitmap * 4,
                    saidas_.Endereco(1550));  // kSlotIdGetDeviceBitmap
    mem_.Escrever32(saidas_.Endereco(kVtableDisplay) + brew_slots::kDisplay_Update * 4,
                    saidas_.Endereco(1537));  // kSlotIdUpdate
    cpu_.Repor(kBase, kPilha);
  }
  ~Bancada() {
    delete despacho_;
    delete al_;
  }

  // Uma chamada a um metodo LIDO DA VTABLE do objecto, como o guest faria:
  // `ldr r_vt,[po]` / `ldr pc,[r_vt, #slot*4]`.
  std::uint32_t ChamarMetodo(std::uint32_t po, unsigned slot, std::uint32_t r1 = 0,
                             std::uint32_t r2 = 0, std::uint32_t r3 = 0) {
    const std::uint32_t vt = mem_.Ler32(po);
    const std::uint32_t destino = mem_.Ler32(vt + slot * 4u);
    EXPECT_NE(destino, 0u) << "slot " << slot << " da vtable em " << vt << " esta vazio";
    cpu_.Set(kR0, po);
    cpu_.Set(kR1, r1);
    cpu_.Set(kR2, r2);
    cpu_.Set(kR3, r3);
    cpu_.Set(kSP, kArg0);
    cpu_.Set(kLR, kSentinela);
    cpu_.Set(kPC, destino);
    despacho_->Correr(cpu_, 1000, kArg0);
    return cpu_.Get(kR0);
  }

  // O bitmap do ecra, pelo caminho que os titulos medidos usam:
  // `IDISPLAY_GetDeviceBitmap` (slot 16) com um ponteiro de saida.
  std::uint32_t BitmapDoEcra() {
    const std::uint32_t pp = 0x80091000u;
    mem_.Escrever32(pp, 0);
    ChamarMetodo(kObjDisplay, brew_slots::kDisplay_GetDeviceBitmap, pp);
    return mem_.Ler32(pp);
  }

  std::size_t Faltas(const std::string& nome) const {
    const auto& f = traco_.ContagemFaltas();
    const auto it = f.find(nome);
    return it == f.end() ? 0 : static_cast<std::size_t>(it->second);
  }

  Memoria& Mem() { return mem_; }
  Despacho& D() { return *despacho_; }
  Tela& T() { return despacho_->TelaRef(); }

 private:
  Memoria mem_;
  Traco traco_{"teste_ecra_do_guest", nullptr};
  Vfs vfs_;
  Alocador* al_ = nullptr;
  Despacho* despacho_ = nullptr;
  Saidas saidas_;
  ArmInterpreter cpu_{mem_, &traco_};
};

// O endereco de um pixel no buffer do guest, pela regra que o cabecalho publica.
std::uint32_t EnderecoDoPixel(int x, int y) {
  return kBaseDoEcraNoGuest +
         (static_cast<std::uint32_t>(y) * kLarguraDoEcra + static_cast<std::uint32_t>(x)) *
             kBytesPorPixelDoEcra;
}

}  // namespace

TEST(EcraDoGuest, OPBmpApontaParaMemoriaDoGuestEOCabecalhoDescreveA) {
  Bancada b;
  const std::uint32_t bmp = b.BitmapDoEcra();
  ASSERT_NE(bmp, 0u);
  using C = CamposDoIdib;
  EXPECT_EQ(b.Mem().Ler32(bmp + C::kPBmp), kBaseDoEcraNoGuest);
  EXPECT_NE(b.Mem().Ler32(bmp + C::kPBmp), 0u) << "zero e a base do modulo do titulo";
  EXPECT_EQ(b.Mem().Ler16(bmp + C::kCx), kLarguraDoEcra);
  EXPECT_EQ(b.Mem().Ler16(bmp + C::kCy), kAlturaDoEcra);
  // `nPitch` conta BYTES de uma linha para a seguinte (`AEEIDIB.h:48`).
  EXPECT_EQ(b.Mem().Ler16(bmp + C::kNPitch), kLarguraDoEcra * kBytesPorPixelDoEcra);
  EXPECT_EQ(b.Mem().Ler8(bmp + C::kNDepth), 16u);
  // A FALTA DEIXOU DE EXISTIR porque o buffer existe -- e nao porque se calou.
  EXPECT_EQ(b.Faltas("IDIB::pBmp do bitmap do ecra"), 0u);
}

TEST(EcraDoGuest, OBufferCobreOEcraInteiroDentroDoEspacoDoGuest) {
  Bancada b;
  b.BitmapDoEcra();
  // A ULTIMA LINHA TEM DE EXISTIR. Uma faixa curta daria uma escrita do guest
  // em memoria que ninguem le -- silenciosa, e so no fundo do ecra.
  EXPECT_TRUE(b.Mem().Existe(EnderecoDoPixel(kLarguraDoEcra - 1, kAlturaDoEcra - 1)));
  EXPECT_EQ(kBytesDoEcra, kLarguraDoEcra * kAlturaDoEcra * kBytesPorPixelDoEcra);
  // E NAO PODE PISAR O HEAP (`0x80200000` + `0xC00000`) nem os objectos.
  EXPECT_GE(kBaseDoEcraNoGuest, kHeap + kHeapTam);
}

TEST(EcraDoGuest, UmPixelEscritoPeloGuestChegaATelaNoUpdate) {
  Bancada b;
  b.BitmapDoEcra();
  const std::uint32_t antes = b.T().Escritos();
  const std::uint16_t kCor = 0xF81Fu;  // magenta em RGB565, nao e o preto de fundo
  b.Mem().Escrever16(EnderecoDoPixel(10, 20), kCor);
  EXPECT_EQ(b.T().PixelEm(10, 20), 0u) << "antes do Update a Tela nao mudou";
  b.ChamarMetodo(kObjDisplay, brew_slots::kDisplay_Update);
  EXPECT_EQ(b.T().PixelEm(10, 20), kCor);
  EXPECT_EQ(b.T().Escritos(), antes + 1) << "um pixel do guest conta como UM pixel desenhado";
  EXPECT_EQ(b.D().PixelsVindosDoGuest(), 1u);
}

TEST(EcraDoGuest, OClipNaoSeAplicaAoQueOGuestEscreveDirectamente) {
  Bancada b;
  b.BitmapDoEcra();
  // O guest escreveu nos pixels DELE, sem passar pelo nosso `IDisplay`. Aplicar
  // o clip aqui apagaria desenho que existe.
  b.T().Clip(0, 0, 4, 4);
  b.Mem().Escrever16(EnderecoDoPixel(100, 100), 0x07E0u);
  b.ChamarMetodo(kObjDisplay, brew_slots::kDisplay_Update);
  EXPECT_EQ(b.T().PixelEm(100, 100), 0x07E0u);
}

TEST(EcraDoGuest, ODesenhoNossoFicaVisivelAoGuest) {
  Bancada b;
  // Desenho do lado do hospedeiro ANTES de o ponteiro sair: um titulo que
  // componha por cima do ecra tem de ler o que la esta.
  b.T().CorAtual(0x001Fu);
  b.T().Ponto(5, 6);
  b.BitmapDoEcra();
  EXPECT_EQ(b.Mem().Ler16(EnderecoDoPixel(5, 6)), 0x001Fu);
  // E o que se desenha DEPOIS chega ao guest no `Update`.
  b.T().CorAtual(0x07E0u);
  b.T().Ponto(7, 8);
  EXPECT_EQ(b.Mem().Ler16(EnderecoDoPixel(7, 8)), 0u);
  b.ChamarMetodo(kObjDisplay, brew_slots::kDisplay_Update);
  EXPECT_EQ(b.Mem().Ler16(EnderecoDoPixel(7, 8)), 0x07E0u);
}

TEST(EcraDoGuest, SemPedirOBitmapDoEcraNaoSePagaNadaDisto) {
  Bancada b;
  // 23 dos 25 titulos que a falta acusava nunca leem o `pBmp`; e 37 do corpus
  // nem pedem o bitmap. Nenhum deles deve pagar 614 400 bytes nem uma copia.
  EXPECT_FALSE(b.D().EcraExpostoAoGuest());
  EXPECT_FALSE(b.Mem().Existe(kBaseDoEcraNoGuest));
  b.ChamarMetodo(kObjDisplay, brew_slots::kDisplay_Update);
  EXPECT_FALSE(b.Mem().Existe(kBaseDoEcraNoGuest));
  EXPECT_EQ(b.D().PixelsVindosDoGuest(), 0u);
}

TEST(SondaDeLeitura, DizQuemLeuOCampoEDeQuePc) {
  // A sonda que MEDIU a demanda real do `pBmp`: 23 dos 25 titulos acusados nunca
  // leem o campo. Sem ela, a falta era registada ao ESCREVER o cabecalho e
  // contava titulos que so chamam metodos do bitmap.
  Traco traco("teste_sonda", nullptr);
  Memoria mem(&traco);
  mem.EscritorUnico("teste");
  mem.Escrever32(0x1000, 0xAABBCCDDu);
  mem.SondarLeitura(0x1000, 0x1004);
  mem.PcAtual(0x1234);
  EXPECT_EQ(mem.Ler32(0x1000), 0xAABBCCDDu);
  ASSERT_EQ(mem.LeiturasSondadas().size(), 1u);
  EXPECT_EQ(mem.LeiturasSondadas()[0].first, 0x1000u);
  EXPECT_EQ(mem.LeiturasSondadas()[0].second, 0x1234u);
  // Fora da faixa nao entra; a mesma leitura do mesmo PC nao entra duas vezes.
  mem.Ler32(0x2000);
  mem.Ler32(0x1000);
  EXPECT_EQ(mem.LeiturasSondadas().size(), 1u);
  // De outro PC, entra: a pergunta e "quem le", e sao dois sitios.
  mem.PcAtual(0x5678);
  mem.Ler32(0x1000);
  EXPECT_EQ(mem.LeiturasSondadas().size(), 2u);
}

}  // namespace zb2::brew
