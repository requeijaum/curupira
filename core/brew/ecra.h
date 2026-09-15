#ifndef ZB2_CORE_BREW_ECRA_H
#define ZB2_CORE_BREW_ECRA_H

// O TAMANHO DO ECRA DO ZEEBO. UM SO NUMERO, UM SO SITIO.
//
// PORQUE EXISTE ESTE FICHEIRO: a arvore tinha DUAS resolucoes ao mesmo tempo.
// O `IShell::GetDeviceInfo` publicava 320x240 (QVGA) e a `Tela`, o EGL, o
// rasterizador e o `IBitmap` do ecra usavam 640x480 (VGA). Um jogo que pergunta
// o tamanho ao shell e depois desenha na tela desenhava num quarto do ecra --
// ou fora dele. Nenhum dos dois numeros era deduzido do outro: eram duas
// constantes independentes que ninguem obrigava a concordar.
//
// QUAL DOS DOIS E O CERTO: 640x480. MEDIDO, no guia oficial do Zeebo
// (`ZeeboDeveloperGuide0.97.md`):
//   :241  "Video-Out resolution: VGA (640X480) - 4:3 aspect ratio"
//   :490  "Zeebo will only support VGA (640x480) display configuration."
//   :526  "Zeebo provides VGA support (640x480)"
//   :1640 "The initial dimensions of the EGL surface will match the target
//          display, which is VGA (640x480) for Zeebo."
//   :3371 "Games for the Zeebo console must be developed targeting a VGA
//          (640x480) screen size"
// O 320x240 aparece no guia, mas SO como regiao de origem a ampliar para VGA
// (`:1671`, `:1686` `EGLSurfaceScaleRect src_rect = {0, 0, 320, 240}`): e uma
// escolha do JOGO dentro da superficie, nunca o tamanho do ecra.
//
// E o que as tres referencias fazem, cada uma com UMA constante:
//   zeebx-emu    `src/machine/mod.rs:1069-1070`  SCREEN_WIDTH/HEIGHT = 640/480,
//                usadas pelo `shell_get_device_info` (`src/machine/shell.rs:435-436`)
//   zeemu        `brew/BrewShell.h:232`          display_width_ = 640 (altura 480),
//                escrito em `cxScreen` por `brew/BrewShell.cpp:1418-1419`
//   zeebulator   `frontends/standalone/main.cpp:45-46`  kWidth/kHeight = 640/480
//
// REGRA: quem precisar do tamanho do ecra inclui ESTE ficheiro. Nao ha um
// segundo 640, nem um segundo 480, escrito a mao noutro sitio. O teste
// `tests/ecra_test.cpp` falha se alguem voltar a por dois numeros na arvore.

#include <cstdint>

namespace zb2::brew {

constexpr std::uint32_t kLarguraDoEcra = 640;
constexpr std::uint32_t kAlturaDoEcra = 480;

// O ECRA VISTO PELO GUEST: os mesmos pixels, com endereco no espaco do guest.
//
// PORQUE EXISTE: o IDIB do BREW e uma struct PUBLICA e o campo `pBmp`
// (`AEEIDIB.h:45`) e o ponteiro para a primeira linha de pixels. Um titulo que
// pede `IDisplay::GetDeviceBitmap` + `QueryInterface(AEEIID_DIB)` guarda esse
// ponteiro e escreve pixels DIRECTAMENTE. Com o `pBmp` a zero -- que era o que
// esta arvore fazia -- esse titulo escreve no endereco 0, que aqui e a BASE DO
// MODULO dele proprio (`tools/bateria.cpp:55`): corrompe o seu proprio codigo.
//
// MEDIDO (sonda de leitura, corpus de 62): **DOIS** titulos leem o campo,
// `tekken2` (0x34ad8, `ldr r0,[r0,#8]` -> guarda em `[r4,#0x98]`) e `zenonia`
// (0xb644, `ldr r0,[r6,#8]` -> guarda em `[r5,#4]`). Os outros 23 que a falta
// acusava leem so o offset 0 -- a vtable -- porque chamam metodos (GetInfo,
// QueryInterface, Release) e nunca tocam nos pixels.
//
// ONDE: acima do heap (`0x80200000`+`0xC00000` = `0x80E00000`) e acima dos
// objectos de widget (`0x81070000`). 0x82000000 esta livre.
constexpr std::uint32_t kBaseDoEcraNoGuest = 0x82000000u;
constexpr std::uint32_t kBytesPorPixelDoEcra = 2;  // RGB565
constexpr std::uint32_t kBytesDoEcra =
    kLarguraDoEcra * kAlturaDoEcra * kBytesPorPixelDoEcra;  // 614 400

}  // namespace zb2::brew

#endif  // ZB2_CORE_BREW_ECRA_H
