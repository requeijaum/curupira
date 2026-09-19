#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/brew/classes.h"   // AtenderClasse + ConstruirIgles (o TexEnvx do IGLES11)
#include "core/brew/despacho.h"
#include "core/brew/ecra.h"
#include "core/brew/interface.h"
#include "core/brew/tela.h"
#include "core/cpu/arm_interpreter.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"
#include "tools/brew_slots.inc"
#include "tools/igles_slots.inc"  // kIgles_TexEnvx (AEEGLES10.h + AEEGLES11.h)

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
  Alocador& Al() { return *al_; }
  Traco& Tr() { return traco_; }
  ArmInterpreter& Cpu() { return cpu_; }
  const Saidas& S() const { return saidas_; }

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


// ===========================================================================
// A FAMILIA DO IBITMAP (frente ibitmap): o slot13 (`CreateCompatibleBitmap`) e
// o que os titulos fazem depois dele. Ver /tmp/pesquisa/ibitmap.md.
//
// O PRIMEIRO teste e VERMELHO NA BASE (prova da tarefa): o despacho ainda nao
// tem o ramo da familia, e o slot13 do bitmap do ecra recusa com nome. Fica
// VERDE quando o dono do `despacho.cpp` aceitar o ramo de duas condicoes que o
// relatorio propoe (`AtenderBitmapDaFamilia` na cadeia de indices 8002..8015).
//
// OS OUTROS provam o SERVIDOR (o que vai dentro desse ramo) ao nivel da ABI,
// chamando `AtenderBitmapDaFamilia` com registos postos a mao -- sem depender
// de nenhum ficheiro de outra frente. E o que torna o patch aplicavel, legivel
// e testavel antes de o despacho o ligar.
// ===========================================================================

// Os tres `AEERasterOp` que o servidor conhece. Sao valores do SDK
// (`AEEGraphics.h`; o zeebx `src/machine.rs:68-71`); se divergirem do servidor,
// o teste do `FillRect` transparente fica vermelho -- e e isso que ele existe
// para apanhar.
constexpr std::uint32_t kRopXorTeste = 1;
constexpr std::uint32_t kRopCopyTeste = 2;
constexpr std::uint32_t kRopTransparenteTeste = 7;

TEST(BitmapCompativel, OSlot13RefusaHojeECriaOBitmapQuandoCablar) {
  Bancada b;
  const std::uint32_t bmp = b.BitmapDoEcra();
  const std::uint32_t pp = 0x80091010u;
  b.Mem().Escrever32(pp, 0xDEADBEEFu);
  // O caminho do guest: `ldr pc,[vt,#52]` (slot 13) com out em r1, w em r2,
  // h em r3. MEDIDO no `tekken2` (lr=0x1ba8c) e no `pacmania` (lr=0x12d94).
  const std::uint32_t rc = b.ChamarMetodo(bmp, 13, pp, 320, 240);
  // VERMELHO NA BASE: o despacho recusa com `kAeeUnsupported` (20) e
  // `IBitmap::slot13` na lista de faltas. VERDE quando o ramo da familia entrar.
  EXPECT_EQ(rc, kAeeSuccess) << "hoje o slot13 recusa (o commit da frente ibitmap"
                                " liga o ramo de AtenderBitmapDaFamilia no despacho)";
  const std::uint32_t obj = b.Mem().Ler32(pp);
  EXPECT_NE(obj, 0xDEADBEEFu) << "o pp tem de ser escrito com o objecto novo";
  EXPECT_NE(obj, 0u);
  using C = CamposDoIdib;
  EXPECT_EQ(b.Mem().Ler16(obj + C::kCx), 320u);
  EXPECT_EQ(b.Mem().Ler16(obj + C::kCy), 240u);
  EXPECT_EQ(b.Mem().Ler16(obj + C::kNPitch), 640u);
  EXPECT_EQ(b.Mem().Ler8(obj + C::kNDepth), 16u);
}

TEST(BitmapCompativel, OServidorCriaUmDibComCabecalhoPublico) {
  Bancada b;
  const std::uint32_t bmp = b.BitmapDoEcra();
  const std::uint32_t pp = 0x80091010u;
  b.Mem().Escrever32(pp, 0);
  auto& cpu = b.Cpu();
  cpu.Set(kR0, bmp);
  cpu.Set(kR1, pp);
  cpu.Set(kR2, 320);
  cpu.Set(kR3, 240);
  ASSERT_TRUE(AtenderBitmapDaFamilia(cpu, b.Mem(), b.Al(), b.Tr(), kVtableBitmap + 13));
  EXPECT_EQ(cpu.Get(kR0), kAeeSuccess);
  const std::uint32_t obj = b.Mem().Ler32(pp);
  ASSERT_NE(obj, 0u);
  // O CABECALHO PUBLICO DO IDIB (`AEEIDIB.h:42-55`) campo a campo, como o do
  // ecra: o jogo le `cx`/`cy`/`nPitch`/`nDepth` directamente (o `pacmania` faz
  // GetInfo + QI(IDIB) logo a seguir ao slot13).
  using C = CamposDoIdib;
  const std::uint32_t pbmp = b.Mem().Ler32(obj + C::kPBmp);
  ASSERT_NE(pbmp, 0u) << "zero e a base do modulo do titulo";
  EXPECT_EQ(b.Mem().Ler32(obj), b.Mem().Ler32(bmp)) << "a MESMA vtable do bitmap";
  EXPECT_EQ(b.Mem().Ler32(obj + C::kPPaletteMap), 0u);
  EXPECT_EQ(b.Mem().Ler32(obj + C::kPRGB), 0u);
  EXPECT_EQ(b.Mem().Ler16(obj + C::kCx), 320u);
  EXPECT_EQ(b.Mem().Ler16(obj + C::kCy), 240u);
  EXPECT_EQ(b.Mem().Ler16(obj + C::kNPitch), 640u);
  EXPECT_EQ(b.Mem().Ler16(obj + C::kCntRGB), 0u);
  EXPECT_EQ(b.Mem().Ler8(obj + C::kNDepth), 16u);
  EXPECT_EQ(b.Mem().Ler8(obj + C::kNColorScheme), C::kEsquemaDeCor565);
  // Determinismo (P4): um DIB novo nasce a ZEROS, nao com lixo do heap.
  EXPECT_EQ(b.Mem().Ler16(pbmp), 0u);
  // A faixa de objectos: acima do ecra (+0x300), e nunca o proprio ecra.
  EXPECT_NE(obj, bmp);
  EXPECT_GE(obj, kObjDibBase + 0x340u);
  EXPECT_LT(obj, kObjDibBase + 0x1000u);
}

TEST(BitmapCompativel, ODesenhoDaFamiliaEscreveNoBufferDoGuest) {
  Bancada b;
  const std::uint32_t bmp = b.BitmapDoEcra();
  const std::uint32_t pp = 0x80091010u;
  b.Mem().Escrever32(pp, 0);
  auto& cpu = b.Cpu();
  cpu.Set(kR0, bmp);
  cpu.Set(kR1, pp);
  cpu.Set(kR2, 8);
  cpu.Set(kR3, 8);
  ASSERT_TRUE(AtenderBitmapDaFamilia(cpu, b.Mem(), b.Al(), b.Tr(), kVtableBitmap + 13));
  const std::uint32_t obj = b.Mem().Ler32(pp);
  ASSERT_NE(obj, 0u);
  const std::uint32_t pbmp = b.Mem().Ler32(obj + CamposDoIdib::kPBmp);
  ASSERT_NE(pbmp, 0u);
  const std::uint32_t sp = 0x80092000u;

  // DrawPixel (slot 5): r1=x r2=y r3=cor565 rop=[sp]. O mesmo padrao MEDIDO no
  // tekken2 a seguir ao slot13 (RGBToNative + DrawPixel por pixel do BMP).
  cpu.Set(kR0, obj);
  cpu.Set(kR1, 3);
  cpu.Set(kR2, 4);
  cpu.Set(kR3, 0xF800u);
  b.Mem().Escrever32(sp, kRopCopyTeste);
  cpu.Set(kSP, sp);
  ASSERT_TRUE(AtenderBitmapDaFamilia(cpu, b.Mem(), b.Al(), b.Tr(), kVtableBitmap + 5));
  EXPECT_EQ(cpu.Get(kR0), kAeeSuccess) << "o DrawPixel escreve no DIB novo";
  EXPECT_EQ(b.Mem().Ler16(pbmp + (4 * 8 + 3) * 2u), 0xF800u);
  EXPECT_EQ(b.Mem().Ler16(pbmp), 0u) << "o resto continua a zeros";

  // GetPixel (slot 6): r3 = ponteiro de saida.
  const std::uint32_t pc = 0x80092010u;
  cpu.Set(kR0, obj);
  cpu.Set(kR1, 3);
  cpu.Set(kR2, 4);
  cpu.Set(kR3, pc);
  ASSERT_TRUE(AtenderBitmapDaFamilia(cpu, b.Mem(), b.Al(), b.Tr(), kVtableBitmap + 6));
  EXPECT_EQ(b.Mem().Ler32(pc), 0xF800u);

  // RGBToNative (slot 3): RGB_WHITE (0xFFFFFF00) -> branco 565 (0xFFFF).
  cpu.Set(kR0, obj);
  cpu.Set(kR1, 0xFFFFFF00u);
  ASSERT_TRUE(AtenderBitmapDaFamilia(cpu, b.Mem(), b.Al(), b.Tr(), kVtableBitmap + 3));
  EXPECT_EQ(cpu.Get(kR0), 0xFFFFu);

  // GetInfo (slot 12): o AEEBitmapInfo sao tres u32 (cx, cy, nDepth).
  const std::uint32_t pinfo = 0x80092020u;
  cpu.Set(kR0, obj);
  cpu.Set(kR1, pinfo);
  cpu.Set(kR2, 12);
  ASSERT_TRUE(AtenderBitmapDaFamilia(cpu, b.Mem(), b.Al(), b.Tr(), kVtableBitmap + 12));
  EXPECT_EQ(b.Mem().Ler32(pinfo), 8u);
  EXPECT_EQ(b.Mem().Ler32(pinfo + 4), 8u);
  EXPECT_EQ(b.Mem().Ler32(pinfo + 8), 16u);

  // QueryInterface IDIB (slot 2): o proprio objecto -- e o que o pacmania pede
  // com o IID 0x01001045 logo a seguir ao slot13.
  const std::uint32_t ppo = 0x80092030u;
  cpu.Set(kR0, obj);
  cpu.Set(kR1, 0x01001045u);  // AEEIID_IDIB (AEEIDIB.h:37)
  cpu.Set(kR2, ppo);
  ASSERT_TRUE(AtenderBitmapDaFamilia(cpu, b.Mem(), b.Al(), b.Tr(), kVtableBitmap + 2));
  EXPECT_EQ(cpu.Get(kR0), kAeeSuccess);
  EXPECT_EQ(b.Mem().Ler32(ppo), obj);
}

TEST(BitmapCompativel, OFillRectEOTransparenteSeguemORop) {
  Bancada b;
  const std::uint32_t bmp = b.BitmapDoEcra();
  const std::uint32_t pp = 0x80091010u;
  b.Mem().Escrever32(pp, 0);
  auto& cpu = b.Cpu();
  cpu.Set(kR0, bmp);
  cpu.Set(kR1, pp);
  cpu.Set(kR2, 16);
  cpu.Set(kR3, 16);
  ASSERT_TRUE(AtenderBitmapDaFamilia(cpu, b.Mem(), b.Al(), b.Tr(), kVtableBitmap + 13));
  const std::uint32_t obj = b.Mem().Ler32(pp);
  ASSERT_NE(obj, 0u);
  const std::uint32_t pbmp = b.Mem().Ler32(obj + CamposDoIdib::kPBmp);

  // FillRect (slot 9): r1=rect r2=cor r3=rop. O rect e 4 x int16 (x,y,dx,dy).
  const std::uint32_t prc = 0x80092040u;
  b.Mem().Escrever16(prc + 0, 1);
  b.Mem().Escrever16(prc + 2, 2);
  b.Mem().Escrever16(prc + 4, 5);
  b.Mem().Escrever16(prc + 6, 4);
  cpu.Set(kR0, obj);
  cpu.Set(kR1, prc);
  cpu.Set(kR2, 0x07E0u);
  cpu.Set(kR3, kRopCopyTeste);
  ASSERT_TRUE(AtenderBitmapDaFamilia(cpu, b.Mem(), b.Al(), b.Tr(), kVtableBitmap + 9));
  EXPECT_EQ(b.Mem().Ler16(pbmp + (3 * 16 + 2) * 2u), 0x07E0u);
  EXPECT_EQ(b.Mem().Ler16(pbmp), 0u) << "fora do rect nao pinta";

  // A cor transparente: `SetTransparencyColor` (slot 14) + FillRect com
  // `AEE_RO_TRANSPARENT` e a MESMA cor -> NAO escreve (o `IBITMAP_Invalidate`
  // do SDK e isto; o zeebx mede o caso no Bejeweled).
  cpu.Set(kR0, obj);
  cpu.Set(kR1, 0x001Fu);
  ASSERT_TRUE(AtenderBitmapDaFamilia(cpu, b.Mem(), b.Al(), b.Tr(), kVtableBitmap + 14));
  const std::uint32_t prc2 = 0x80092050u;
  b.Mem().Escrever16(prc2 + 0, 0);
  b.Mem().Escrever16(prc2 + 2, 0);
  b.Mem().Escrever16(prc2 + 4, 16);
  b.Mem().Escrever16(prc2 + 6, 16);
  cpu.Set(kR0, obj);
  cpu.Set(kR1, prc2);
  cpu.Set(kR2, 0x001Fu);
  cpu.Set(kR3, kRopTransparenteTeste);
  ASSERT_TRUE(AtenderBitmapDaFamilia(cpu, b.Mem(), b.Al(), b.Tr(), kVtableBitmap + 9));
  EXPECT_EQ(cpu.Get(kR0), kAeeSuccess);
  EXPECT_EQ(b.Mem().Ler16(pbmp + (3 * 16 + 2) * 2u), 0x07E0u) << "transparente nao apaga";
  // GetTransparencyColor (slot 15) devolve o que o 14 guardou.
  const std::uint32_t pc = 0x80092060u;
  cpu.Set(kR0, obj);
  cpu.Set(kR1, pc);
  ASSERT_TRUE(AtenderBitmapDaFamilia(cpu, b.Mem(), b.Al(), b.Tr(), kVtableBitmap + 15));
  EXPECT_EQ(b.Mem().Ler32(pc), 0x001Fu);
}

TEST(BitmapCompativel, OBltCopiaPixelsEntreCompativeis) {
  Bancada b;
  const std::uint32_t bmp = b.BitmapDoEcra();
  auto& cpu = b.Cpu();
  const auto criar = [&](std::uint32_t pp, std::uint32_t w, std::uint32_t h) {
    b.Mem().Escrever32(pp, 0);
    cpu.Set(kR0, bmp);
    cpu.Set(kR1, pp);
    cpu.Set(kR2, w);
    cpu.Set(kR3, h);
    EXPECT_TRUE(AtenderBitmapDaFamilia(cpu, b.Mem(), b.Al(), b.Tr(), kVtableBitmap + 13));
    return b.Mem().Ler32(pp);
  };
  const std::uint32_t origem = criar(0x80091010u, 8, 8);
  const std::uint32_t destino = criar(0x80091020u, 12, 12);
  ASSERT_NE(origem, 0u);
  ASSERT_NE(destino, 0u);
  const std::uint32_t pbmp_o = b.Mem().Ler32(origem + CamposDoIdib::kPBmp);
  const std::uint32_t pbmp_d = b.Mem().Ler32(destino + CamposDoIdib::kPBmp);
  const std::uint32_t sp = 0x80092070u;

  // Um pixel na origem (4,5) e outro na cor transparente (0,0).
  cpu.Set(kR0, origem);
  cpu.Set(kR1, 4);
  cpu.Set(kR2, 5);
  cpu.Set(kR3, 0x1234u);
  b.Mem().Escrever32(sp, kRopCopyTeste);
  cpu.Set(kSP, sp);
  ASSERT_TRUE(AtenderBitmapDaFamilia(cpu, b.Mem(), b.Al(), b.Tr(), kVtableBitmap + 5));

  // BltIn (slot 10): r1=xd r2=yd r3=dx [sp]=dy,src,xs,ys,rop.
  //  (po, xDst, yDst, dx, dy, pSrc, xSrc, ySrc, rop)
  cpu.Set(kR0, destino);
  cpu.Set(kR1, 2);
  cpu.Set(kR2, 3);
  cpu.Set(kR3, 8);
  b.Mem().Escrever32(sp + 0, 8);   // dy
  b.Mem().Escrever32(sp + 4, origem);
  b.Mem().Escrever32(sp + 8, 0);   // xs
  b.Mem().Escrever32(sp + 12, 0);  // ys
  b.Mem().Escrever32(sp + 16, kRopCopyTeste);
  ASSERT_TRUE(AtenderBitmapDaFamilia(cpu, b.Mem(), b.Al(), b.Tr(), kVtableBitmap + 10));
  EXPECT_EQ(b.Mem().Ler16(pbmp_d + (8 * 12 + 6) * 2u), 0x1234u)
      << "o pixel (4,5) da origem chegou a (2+4, 3+5) do destino";
  EXPECT_EQ(b.Mem().Ler16(pbmp_d + (3 * 12 + 2) * 2u), 0u)
      << "o resto do destino continua a zeros";

  // BltOut (slot 11): inverte os papeis -- destino do outro lado.
  const std::uint32_t destino2 = criar(0x80091030u, 16, 16);
  ASSERT_NE(destino2, 0u);
  const std::uint32_t pbmp_d2 = b.Mem().Ler32(destino2 + CamposDoIdib::kPBmp);
  cpu.Set(kR0, origem);  // a FONTE
  cpu.Set(kR1, 1);
  cpu.Set(kR2, 1);
  cpu.Set(kR3, 4);
  b.Mem().Escrever32(sp + 0, 4);
  b.Mem().Escrever32(sp + 4, destino2);
  b.Mem().Escrever32(sp + 8, 4);
  b.Mem().Escrever32(sp + 12, 5);
  b.Mem().Escrever32(sp + 16, kRopCopyTeste);
  ASSERT_TRUE(AtenderBitmapDaFamilia(cpu, b.Mem(), b.Al(), b.Tr(), kVtableBitmap + 11));
  EXPECT_EQ(b.Mem().Ler16(pbmp_d2 + (1 * 16 + 1) * 2u), 0x1234u)
      << "o (4,5) da origem chegou a (1,1) do destino2";
}

TEST(BitmapCompativel, OServidorRecusaComNomeForaDaFaixaEComObjectoInvalido) {
  Bancada b;
  auto& cpu = b.Cpu();
  // Fora da faixa da familia: nao e nosso, o despacho segue a cadeia.
  EXPECT_FALSE(AtenderBitmapDaFamilia(cpu, b.Mem(), b.Al(), b.Tr(), kVtableBitmap + 16));
  EXPECT_FALSE(AtenderBitmapDaFamilia(cpu, b.Mem(), b.Al(), b.Tr(), kVtableBitmap + 1));
  // Objecto sem cabecalho (o pp a zeros): DrawPixel recusa COM NOME e nao mente.
  cpu.Set(kR0, 0x80050F00u);
  cpu.Set(kR1, 0);
  cpu.Set(kR2, 0);
  cpu.Set(kR3, 0);
  cpu.Set(kSP, 0x80092000u);
  b.Mem().Escrever32(0x80092000u, kRopCopyTeste);
  EXPECT_TRUE(AtenderBitmapDaFamilia(cpu, b.Mem(), b.Al(), b.Tr(), kVtableBitmap + 5));
  EXPECT_EQ(cpu.Get(kR0), kAeeUnsupported);
  EXPECT_EQ(b.Faltas("IBitmap::DrawPixel"), 1u);
}


TEST(BitmapCompativel, OBltInComOrigemNulaDevolveBadParmSemFalta) {
  Bancada b;
  auto& cpu = b.Cpu();
  constexpr std::uint32_t sp = 0x80092000u;
  cpu.Set(kR0, b.BitmapDoEcra());
  cpu.Set(kR1, 0); cpu.Set(kR2, 0); cpu.Set(kR3, 1);
  cpu.Set(kSP, sp);
  b.Mem().Escrever32(sp + 0, 1);  // dy
  b.Mem().Escrever32(sp + 4, 0);  // pSrc invalido por contrato
  b.Mem().Escrever32(sp + 8, 0); b.Mem().Escrever32(sp + 12, 0);
  b.Mem().Escrever32(sp + 16, kRopCopyTeste);
  ASSERT_TRUE(AtenderBitmapDaFamilia(cpu, b.Mem(), b.Al(), b.Tr(), kVtableBitmap + 10));
  EXPECT_EQ(cpu.Get(kR0), kAeeBadParm);
  EXPECT_EQ(b.Faltas("IBitmap::BltIn"), 0u);
}

// ---------------------------------------------------------------------------
// O IGLES11::TexEnvx (mesma frente, tema imagem/estado de textura).
//
// MEDIDO em 4 titulos (abd, pacmania, peggle, torkandkral), sempre com o MESMO
// par logo a seguir ao `glEnable(GL_TEXTURE_2D)`:
//
//     r1=0x2300 (GL_TEXTURE_ENV_MODE) r2=0x2200 (GL_MODULATE)
//
// O motor do IGL de 80 slots (`igl.cpp`) JA implementa o `kIgl_TexEnvx` -- so
// falta a entrada do mapa `SlotIglesNoIgl` em `classes.cpp` (uma linha:
// `case kIgles_TexEnvx: return kIgl_TexEnvx;`). O teste abaixo fica VERMELHO
// enquanto essa linha nao existe, e VERDE com ela. O ficheiro `classes.cpp`
// esta ocupado pela frente qualcomm; por isso este teste vive AQUI, onde nao
// pisa ninguem, e a entrada do mapa fica no relatorio como PROPOSTA.
// ===========================================================================
TEST(FrenteIbitmap, OTexEnvxDoIglesRecusaHojeEServeOModoQuandoMapeado) {
  Bancada b;
  ConstruirIgles(b.Mem(), b.S(), b.Tr());
  auto& cpu = b.Cpu();
  cpu.Set(kR0, kObjetoIgles);
  cpu.Set(kR1, 0x2300u);  // GL_TEXTURE_ENV
  cpu.Set(kR2, 0x2200u);  // GL_TEXTURE_ENV_MODE
  cpu.Set(kR3, 0x2100u);  // GL_MODULATE
  ASSERT_TRUE(AtenderClasse(cpu, kVtableIgles + igles_slots::kIgles_TexEnvx, b.Tr()));
  // VERMELHO NA BASE: a recusa generica com nome. VERDE com a linha do mapa
  // `SlotIglesNoIgl(kIgles_TexEnvx) -> kIgl_TexEnvx` em classes.cpp.
  EXPECT_EQ(cpu.Get(kR0), kAeeSuccess) << "hoje o IGLES11::TexEnvx recusa com nome";
  EXPECT_EQ(b.Faltas("IGLES11::TexEnvx"), 0u) << "com o mapa, o motor do IGL serve o modo";
}

// O IGLES11::TexEnvfv (mesma frente, forma vetorial): 40 pedidos em 4 titulos
// (`zeebotennis`, `dodgeball`, `zeebopeteca`, `alice`), sempre
// r1=0x2300 (GL_TEXTURE_ENV) r2=0x2201 (GL_TEXTURE_ENV_MODE) r3=ponteiro.
// O motor do IGL JA implementa o `kIgl_TexEnvxv` (acumula os 4 valores) -- so
// falta a entrada do mapa, como foi com o `TexEnvx` acima.
TEST(FrenteIbitmap, OTexEnvfvDoIglesRecusaHojeEServeOModoQuandoMapeado) {
  Bancada b;
  ConstruirIgles(b.Mem(), b.S(), b.Tr());
  auto& cpu = b.Cpu();
  constexpr std::uint32_t kParams = 0x80093000u;
  b.Mem().Escrever32(kParams, 0x00002100u);  // GL_MODULATE como float-vetor de 1
  b.Mem().Escrever32(kParams + 4u, 0u);
  b.Mem().Escrever32(kParams + 8u, 0u);
  b.Mem().Escrever32(kParams + 12u, 0u);
  cpu.Set(kR0, kObjetoIgles);
  cpu.Set(kR1, 0x2300u);  // GL_TEXTURE_ENV (medido nos 4 titulos)
  cpu.Set(kR2, 0x2200u);  // GL_TEXTURE_ENV_MODE
  cpu.Set(kR3, kParams);
  ASSERT_TRUE(AtenderClasse(cpu, kVtableIgles + igles_slots::kIgles_TexEnvfv, b.Tr()));
  // VERMELHO NA BASE: a recusa generica com nome. VERDE com a linha do mapa
  // `SlotIglesNoIgl(kIgles_TexEnvfv) -> kIgl_TexEnvxv` em classes.cpp.
  EXPECT_EQ(cpu.Get(kR0), kAeeSuccess) << "hoje o IGLES11::TexEnvfv recusa com nome";
  EXPECT_EQ(b.Faltas("IGLES11::TexEnvfv"), 0u) << "com o mapa, o motor do IGL serve o modo";
}

}  // namespace zb2::brew
