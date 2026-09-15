// O TAMANHO DO ECRA. UM SO NUMERO, PROVADO DO LADO DO GUEST.
//
// PORQUE EXISTE ESTE FICHEIRO: a arvore teve DUAS resolucoes ao mesmo tempo.
//   `IShell::GetDeviceInfo`   dizia 320x240  (`despacho.cpp`, antes desta etapa)
//   `Tela`, EGL, rasterizador
//   e o `IBitmap` do ecra      diziam 640x480
// Nenhum dos dois era derivado do outro, e o comentario do `widget.h` chamava ao
// 320x240 "uma so verdade". Um jogo que PERGUNTA o tamanho ao shell e depois
// desenha no bitmap do ecra desenhava num quarto da area -- e nada na arvore
// ficava vermelho por causa disso.
//
// O QUE ESTE TESTE MEDE: as respostas que o GUEST recebe, pelas vtables, de cada
// publicador do tamanho do ecra. Ele falha se dois publicadores discordarem, e
// falha se o numero deixar de ser o do guia oficial do Zeebo:
//   ZeeboDeveloperGuide0.97.md:490  "Zeebo will only support VGA (640x480)
//                                    display configuration."
//   ZeeboDeveloperGuide0.97.md:241  "Video-Out resolution: VGA (640X480)"
//   ZeeboDeveloperGuide0.97.md:3371 "Games ... must be developed targeting a VGA
//                                    (640x480) screen size"
//
// NAO e um `static_assert` sozinho: um `static_assert` so ve as constantes que
// alguem se lembrou de ligar. Este teste ve o que sai pela vtable, que e o que o
// jogo ve.

#include <gtest/gtest.h>

#include <cstdint>

#include "core/brew/despacho.h"
#include "core/brew/ecra.h"
#include "core/brew/egl.h"
#include "core/brew/interface.h"
#include "core/brew/tela.h"
#include "core/cpu/arm_interpreter.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"
#include "core/video/rasterizador.h"
#include "tools/brew_slots.inc"

namespace zb2::brew {
namespace {

// AS COPIAS EM TEMPO DE COMPILACAO. Estas nao precisam de correr: se uma delas
// se separar do `ecra.h`, o ficheiro nem compila.
static_assert(static_cast<std::uint32_t>(Tela::kLargura) == kLarguraDoEcra,
              "tela.h: a largura da Tela deixou de ser a do ecra");
static_assert(static_cast<std::uint32_t>(Tela::kAltura) == kAlturaDoEcra,
              "tela.h: a altura da Tela deixou de ser a do ecra");
static_assert(kLarguraDaSuperficie == kLarguraDoEcra,
              "egl.h: a largura da superficie deixou de ser a do ecra");
static_assert(kAlturaDaSuperficie == kAlturaDoEcra,
              "egl.h: a altura da superficie deixou de ser a do ecra");

// O NUMERO DO GUIA, escrito a mao AQUI e SO aqui, no teste. Se o `ecra.h` mudar
// para outra coisa, este teste diz qual era a fonte.
constexpr std::uint32_t kVgaLargura = 640;
constexpr std::uint32_t kVgaAltura = 480;

constexpr std::uint32_t kPilha = 0x80080000u;
constexpr std::uint32_t kHeap = 0x80200000u;
constexpr std::uint32_t kHeapTam = 0x00C00000u;
constexpr std::uint32_t kTabela = 0x80010000u;
constexpr std::uint32_t kSentinela = 0xFFFFFFF0u;
constexpr std::uint32_t kArg0 = 0x80090000u;
// Os indices de saida da cablagem do motor (`tools/bateria.cpp:267-272`), que e
// onde as vtables do IShell e do IDisplay sao ligadas aos slots.
constexpr std::uint32_t kSaidaGetDest = 1545;
constexpr std::uint32_t kSaidaGetDeviceInfo = 1549;
constexpr std::uint32_t kSaidaGetDeviceBitmap = 1550;

// A bancada CABLA AS VTABLES como a bateria as cabla, e depois chama PELA
// VTABLE: ler o objecto, ler o slot, saltar. E o caminho do jogo.
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
    despacho_->InstalarAjudantes(saidas_, kTabela);
    ConstruirObjeto(mem_, saidas_, kObjShell, saidas_.Endereco(kVtableShell), 64, kVtableShell);
    ConstruirObjeto(mem_, saidas_, kObjDisplay, saidas_.Endereco(kVtableDisplay), 64,
                    kVtableDisplay);
    Cablar(kVtableShell, brew_slots::kShell_GetDeviceInfo, kSaidaGetDeviceInfo);
    Cablar(kVtableDisplay, brew_slots::kDisplay_GetDeviceBitmap, kSaidaGetDeviceBitmap);
    Cablar(kVtableDisplay, brew_slots::kDisplay_GetDestination, kSaidaGetDest);
    cpu_.Repor(0, kPilha);
  }
  ~Bancada() {
    delete despacho_;
    delete al_;
  }

  // Uma chamada COMO O JOGO A FAZ: `(*po)->slot(po, ...)`.
  std::uint32_t ChamaPelaVtable(std::uint32_t objeto, std::uint32_t slot, std::uint32_t r1 = 0) {
    const std::uint32_t vt = mem_.Ler32(objeto);
    const std::uint32_t pfn = mem_.Ler32(vt + slot * 4);
    cpu_.Set(kR0, objeto);
    cpu_.Set(kR1, r1);
    cpu_.Set(kR2, 0);
    cpu_.Set(kR3, 0);
    cpu_.Set(kSP, kArg0);
    cpu_.Set(kLR, kSentinela);
    cpu_.Set(kPC, pfn);
    despacho_->Correr(cpu_, 1000, kArg0);
    return cpu_.Get(kR0);
  }

  Memoria& Mem() { return mem_; }

 private:
  void Cablar(std::uint32_t vt, std::uint32_t slot, std::uint32_t saida) {
    mem_.Escrever32(saidas_.Endereco(vt) + slot * 4, saidas_.Endereco(saida));
  }

  Memoria mem_;
  Traco traco_{"teste_do_ecra", nullptr};
  Vfs vfs_;
  Alocador* al_ = nullptr;
  Despacho* despacho_ = nullptr;
  Saidas saidas_;
  ArmInterpreter cpu_{mem_, &traco_};
};

}  // namespace

// ---------------------------------------------------------------------------
// 1. O QUE O `IShell::GetDeviceInfo` PUBLICA
// ---------------------------------------------------------------------------
TEST(Ecra, OGetDeviceInfoPublicaOEcraDoGuiaEmVga) {
  Bancada b;
  constexpr std::uint32_t kInfo = 0x80091000u;
  for (std::uint32_t k = 0; k < 64; k += 4) b.Mem().Escrever32(kInfo + k, 0xCDCDCDCDu);
  b.ChamaPelaVtable(kObjShell, brew_slots::kShell_GetDeviceInfo, kInfo);
  // `AEEDeviceInfo`: cxScreen@0, cyScreen@2, cxAltScreen@4, cyAltScreen@6 --
  // quatro `uint16` (AEEShell.h, `AEEDeviceInfo`).
  EXPECT_EQ(b.Mem().Ler16(kInfo + 0), static_cast<std::uint16_t>(kVgaLargura)) << "cxScreen";
  EXPECT_EQ(b.Mem().Ler16(kInfo + 2), static_cast<std::uint16_t>(kVgaAltura)) << "cyScreen";
  EXPECT_EQ(b.Mem().Ler16(kInfo + 0), static_cast<std::uint16_t>(kLarguraDoEcra));
  EXPECT_EQ(b.Mem().Ler16(kInfo + 2), static_cast<std::uint16_t>(kAlturaDoEcra));
}

// ---------------------------------------------------------------------------
// 2. O QUE O BITMAP DO ECRA DIZ TEM DE SER O MESMO
//
// E AQUI QUE O DEFEITO APARECIA: o jogo pergunta ao shell (320x240 antes desta
// etapa) e desenha no bitmap (640x480). Duas respostas, do mesmo emulador, ao
// mesmo objecto fisico.
// ---------------------------------------------------------------------------
TEST(Ecra, OBitmapDoEcraDizOMesmoQueOGetDeviceInfo) {
  Bancada b;
  constexpr std::uint32_t kInfo = 0x80091000u;
  constexpr std::uint32_t kPp = 0x80091100u;
  b.ChamaPelaVtable(kObjShell, brew_slots::kShell_GetDeviceInfo, kInfo);
  const std::uint32_t cx = b.Mem().Ler16(kInfo + 0);
  const std::uint32_t cy = b.Mem().Ler16(kInfo + 2);

  // `GetDeviceBitmap(IDisplay*, IBitmap**)`. O objecto e um AEEIDIB e o cx/cy
  // sao `uint16` em +20/+22 (`AEEIDIB.h:42-55`) -- este teste lia u32 em +12/+16,
  // que eram os offsets da struct INVENTADA que o commit do IDIB corrigiu.
  b.ChamaPelaVtable(kObjDisplay, brew_slots::kDisplay_GetDeviceBitmap, kPp);
  const std::uint32_t bmp = b.Mem().Ler32(kPp);
  ASSERT_NE(bmp, 0u) << "o GetDeviceBitmap nao devolveu bitmap nenhum";
  EXPECT_EQ(b.Mem().Ler16(bmp + 20), cx) << "o bitmap do ecra e o GetDeviceInfo discordam na largura";
  EXPECT_EQ(b.Mem().Ler16(bmp + 22), cy) << "o bitmap do ecra e o GetDeviceInfo discordam na altura";

  // E o `GetDestination`, que e o outro caminho para o mesmo bitmap.
  const std::uint32_t dest = b.ChamaPelaVtable(kObjDisplay, brew_slots::kDisplay_GetDestination);
  ASSERT_NE(dest, 0u);
  EXPECT_EQ(b.Mem().Ler16(dest + 20), cx) << "o GetDestination discorda do GetDeviceInfo";
  EXPECT_EQ(b.Mem().Ler16(dest + 22), cy) << "o GetDestination discorda do GetDeviceInfo";
}

// ---------------------------------------------------------------------------
// 3. A TELA E O RASTERIZADOR
//
// A `Tela` e onde os pixels acabam, e o viewport omisso do GL e o ecra inteiro.
// Se o shell publicasse outro tamanho, o jogo calcularia posicoes para uma area
// que nao e esta.
// ---------------------------------------------------------------------------
TEST(Ecra, ATelaEOViewportOmissoSaoOEcraInteiro) {
  Bancada b;
  constexpr std::uint32_t kInfo = 0x80091000u;
  b.ChamaPelaVtable(kObjShell, brew_slots::kShell_GetDeviceInfo, kInfo);
  EXPECT_EQ(static_cast<std::uint32_t>(Tela::kLargura), b.Mem().Ler16(kInfo + 0));
  EXPECT_EQ(static_cast<std::uint32_t>(Tela::kAltura), b.Mem().Ler16(kInfo + 2));

  // A tela conta os pixels que aceita: um ponto fora dela nao e escrito. O canto
  // (largura-1, altura-1) tem de estar DENTRO, e (largura, altura) FORA.
  Tela t;
  t.CorAtual(0x1234u);
  t.Ponto(Tela::kLargura - 1, Tela::kAltura - 1);
  EXPECT_EQ(t.Escritos(), 1u) << "o ultimo pixel do ecra nao cabe na Tela";
  t.Ponto(Tela::kLargura, Tela::kAltura);
  EXPECT_EQ(t.Escritos(), 1u) << "a Tela aceitou um pixel fora do ecra";

  const zb2::video::EstadoDeRasterizacao e;
  EXPECT_EQ(e.viewport[2], kLarguraDoEcra) << "o viewport omisso do GL nao e o ecra";
  EXPECT_EQ(e.viewport[3], kAlturaDoEcra) << "o viewport omisso do GL nao e o ecra";
}

}  // namespace zb2::brew
