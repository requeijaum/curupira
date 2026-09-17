#include "core/brew/graficos.h"

#include <algorithm>
#include <cstdio>

#include "core/brew/widget.h"  // `Atendido` -- declarado em `graficos.h`, DEFINIDO aqui

// Ver `graficos.h` para a medicao que pediu este modulo, para as ordens dos
// slots (lidas do cabecalho por `tools/gerar_slots.py`), para a tela onde ele
// escreve, para o caminho que faltou (a `Tela` do rasterizador) e para a
// contradicao do `AEEGraphics.h` sobre o sentido do eixo Y.

namespace zb2::brew {

// Os numeros de slot vem de `brew_slots.inc` (gerado). Sem prefixo: a origem e
// UMA, e o nome fica legivel.
using namespace brew_slots;

namespace {

// `AEERect` do guest: quatro `int16` (`platform/ui/inc/AEERect.h:23-26`). E a
// MESMA struct que o `core/brew/widget.cpp` escreve ao servi-lo; ver o
// comentario de `LerRectDoGuest`/`EscreverRectDoGuest` em `graficos.h`.
constexpr std::uint32_t kRectX = 0;
constexpr std::uint32_t kRectY = 2;
constexpr std::uint32_t kRectDx = 4;
constexpr std::uint32_t kRectDy = 6;

// A PROFUNDIDADE DE COR DA TELA. O pixel e RGB565 (`core/brew/tela.h`), e os dois
// emuladores de referencia respondem o MESMO a este slot:
//   `zeebx` `src/machine/display.rs:431` -> `COLOR_DEPTH` = 16 (`mod.rs:1072`);
//   `zeemu` `brew/BrewGraphics.cpp:245`  -> `cpu.set_reg(REG_R0, 16)`.
// E a mesma verdade do EGL daqui (`egl.cpp:114`, `EGL_BUFFER_SIZE 16`).
constexpr std::uint32_t kProfundidadeDeCor = 16;

// `AEE_PAINT_COPY` e `AEE_PAINT_XOR` (`AEEGraphics.h:165-168`).
constexpr std::uint32_t kPinturaCopy = 0;
constexpr std::uint32_t kPinturaXor = 1;

// Um ponteiro do guest so se escreve se a pagina existir: o
// `Memoria::Escrever8` ALOCA a pagina que falta (`memoria.cpp:107`), logo escrever
// num ponteiro invalido fabricava memoria que ninguem pediu -- e o defeito
// apareceria noutro sitio, a semana seguinte.
bool PaginaDoGuest(const Memoria& mem, std::uint32_t onde, std::uint32_t quantos) {
  return onde != 0 && mem.Existe(onde) && mem.Existe(onde + quantos - 1);
}

}  // namespace

bool LerRectDoGuest(const Memoria& mem, std::uint32_t onde, int* x, int* y, int* largura,
                    int* altura) {
  if (!PaginaDoGuest(mem, onde, 8)) return false;
  *x = static_cast<std::int16_t>(mem.Ler16(onde + kRectX));
  *y = static_cast<std::int16_t>(mem.Ler16(onde + kRectY));
  *largura = static_cast<std::int16_t>(mem.Ler16(onde + kRectDx));
  *altura = static_cast<std::int16_t>(mem.Ler16(onde + kRectDy));
  return true;
}

void EscreverRectDoGuest(Memoria& mem, std::uint32_t onde, int x, int y, int largura,
                         int altura) {
  if (onde == 0) return;
  mem.Escrever16(onde + kRectX, static_cast<std::uint16_t>(x));
  mem.Escrever16(onde + kRectY, static_cast<std::uint16_t>(y));
  mem.Escrever16(onde + kRectDx, static_cast<std::uint16_t>(largura));
  mem.Escrever16(onde + kRectDy, static_cast<std::uint16_t>(altura));
}

Graficos::Graficos(Memoria& mem, Traco& traco) : mem_(mem), traco_(traco) {
  // O ESTADO INICIAL, com a proveniencia de cada valor:
  //
  //   `SetColor`: "The default foreground color is black" (`AEEGraphics.h`,
  //               documentacao do `SetColor`) -- e o `zeebx` concorda
  //               (`src/machine/mod.rs:1129`, `stroke: Rgb::BLACK`);
  //   `fill` e `background`: BRANCO no `zeebx` (`:1130-1131`). O cabecalho NAO
  //               diz a omissao destes dois; fica dito que a fonte e a
  //               referencia, e nao uma deducao.
  //   `fill_mode`: FALSE -- a omissao do `zeebx` (`:1132`) e o unico estado que
  //               nao acrescenta desenho a quem o nao pediu;
  //   `point_size`: 1 (`zeebx` `:1133`);
  //   O CLIP E O VIEWPORT por omissao SAO A JANELA: "The default clipping shape
  //               is CLIPPING_RECT and the display window is the default
  //               clipping region" (`AEEClipShape`), e "The default viewport is
  //               the screen without a frame" (`IGRAPHICS_SetViewport`).
  estado_.cor_de_frente = CorRgba{0, 0, 0, 255};
  estado_.cor_de_preenchimento = CorRgba{255, 255, 255, 255};
  estado_.cor_de_fundo = CorRgba{255, 255, 255, 255};
  estado_.preenche = false;
  estado_.tamanho_do_ponto = 1;
  estado_.modo_de_pintura = kPinturaCopy;
  estado_.clip_tipo = kClipNenhum;
  estado_.viewport_largura = kLarguraDoEcra;
  estado_.viewport_altura = kAlturaDoEcra;
  RecalcularCaixa();
}

std::uint32_t Graficos::ParaRgbval(const CorRgba& c, bool com_alfa) {
  // `MAKE_RGB`/`MAKE_RGBA` (`AEERGBVAL.h:19-20`), lidos e nao deduzidos:
  //   MAKE_RGBA(r,g,b,a) = (r<<8) + (g<<16) + (b<<24) + a
  // O BYTE BAIXO E O ALFA, e o vermelho esta no SEGUNDO byte -- a mesma ordem que
  // a `Tela::RgbvalPara565` ja usa (`tela.h`: "o vermelho esta no SEGUNDO byte,
  // nao no primeiro, e o byte baixo e alfa").
  const std::uint32_t rgb = (static_cast<std::uint32_t>(c.r) << 8) |
                            (static_cast<std::uint32_t>(c.g) << 16) |
                            (static_cast<std::uint32_t>(c.b) << 24);
  return com_alfa ? (rgb | c.a) : rgb;
}

std::uint16_t Graficos::ParaCor565(const CorRgba& c) {
  // A MESMA truncagem da `Tela::RgbvalPara565` e do rasterizador: descarta os
  // bits baixos, nao arredonda. Duas conversoes diferentes dariam dois pixeis
  // diferentes para a mesma cor pedida.
  const std::uint32_t r = (static_cast<std::uint32_t>(c.r) >> 3) & 0x1Fu;
  const std::uint32_t g = (static_cast<std::uint32_t>(c.g) >> 2) & 0x3Fu;
  const std::uint32_t b = (static_cast<std::uint32_t>(c.b) >> 3) & 0x1Fu;
  return static_cast<std::uint16_t>((r << 11) | (g << 5) | b);
}

// ---------------------------------------------------------------------------
// A CONSTRUCAO: guarda a faixa de saida e confere que ela cabe
// ---------------------------------------------------------------------------
bool Graficos::Construir(const Saidas& saidas, std::string* motivo) {
  if (pronto_) {
    if (motivo != nullptr) *motivo = "o IGraphics ja estava construido";
    return false;
  }
  // O ULTIMO INDICE DESTE MODULO: os 64 slots do objecto generico de indice 3.
  // Um indice fora da faixa da um endereco que o laco de execucao NUNCA
  // reconhece, e o pedido do titulo fica sem resposta EM SILENCIO -- a pior
  // especie. E a mesma guarda do `Widgets::Construir`, pela mesma razao.
  const std::uint32_t ultimo = VtGenerico(kIndiceDoGraphics) + kSlotsPorVtable - 1u;
  if (ultimo >= saidas.quantos) {
    if (motivo != nullptr) {
      *motivo = "a faixa de saida tem " + std::to_string(saidas.quantos) +
                " indices e o IGraphics precisa do indice " + std::to_string(ultimo);
    }
    return false;
  }
  // UMA COPIA, e nao um ponteiro: o modulo nunca guarda um endereco de fora.
  saidas_ = saidas;
  pronto_ = true;
  return true;
}

bool Graficos::EMeu(std::uint32_t indice) const {
  if (!pronto_) return false;
  return indice >= VtGenerico(kIndiceDoGraphics) &&
         indice < VtGenerico(kIndiceDoGraphics) + kSlotsPorVtable;
}

// A CABLAGEM, NO PRIMEIRO PEDIDO DE CADA SLOT.
//
// O objecto e o generico 3 (`ObjGenerico(3)` = 0x80060300) e quem o escreve e o
// `ConstruirObjeto` de quem dirige o titulo -- na bateria isso corre DEPOIS do
// `InstalarAjudantes` (que e quem constroi este modulo, `tools/bateria.cpp:633`
// contra `:769`), logo a cablagem NAO pode ser conferida na construcao. Confere-se
// aqui: a vtable do objecto tem de ter, no slot pedido, o endereco de saida que o
// despacho acabou de nos entregar. Sem esta leitura, um objecto trocado faria
// este modulo atender indices que o guest nunca usa -- e o defeito era mudo. (E a
// licao da cablagem do `SetTimer`, que custou uma corrida inteira.)
bool Graficos::ConferirSlot(std::uint32_t slot) {
  const std::uint32_t objeto = ObjGenerico(kIndiceDoGraphics);
  if (!mem_.Existe(objeto)) {
    RegistarRecusa("IGraphics sem objecto na memoria",
                   "obj=0x" + Hex(objeto) + " slot=" + std::to_string(slot));
    return false;
  }
  const std::uint32_t vtable = mem_.Ler32(objeto);
  if (vtable == 0 || !mem_.Existe(vtable + slot * 4)) {
    RegistarRecusa("IGraphics sem vtable",
                   "obj=0x" + Hex(objeto) + " slot=" + std::to_string(slot));
    return false;
  }
  const std::uint32_t lido = mem_.Ler32(vtable + slot * 4);
  const std::uint32_t esperado = saidas_.Endereco(VtGenerico(kIndiceDoGraphics) + slot);
  if (lido != esperado) {
    char det[128];
    std::snprintf(det, sizeof(det), "vtable=0x%08x slot=%u tem 0x%08x e esperava 0x%08x",
                  vtable, slot, lido, esperado);
    RegistarRecusa("IGraphics cablagem perdida", det);
    return false;
  }
  return true;
}

void Graficos::RegistarRecusa(const std::string& nome, const std::string& detalhe) {
  traco_.RegistarFalta(Area::Brew, nome, detalhe);
  recusas_.push_back(detalhe.empty() ? nome : nome + ": " + detalhe);
}

// ---------------------------------------------------------------------------
// A TELA
// ---------------------------------------------------------------------------
//
// ESCREVE-SE NO BUFFER DO ECRA NO GUEST (`kBaseDoEcraNoGuest`), que e a memoria
// onde os pixeis da consola vivem e que o titulo le pelo `pBmp`. O caminho
// preferido -- a `Tela`/`Superficie` do rasterizador -- NAO e alcancavel dos
// ficheiros que esta frente pode tocar; a razao esta no cabecalho, e o
// precedente tambem (o `glDrawTex*OES`, `classes.cpp:1091`).
//
// O QUE ISSO CUSTA, dito por inteiro: estes pixeis sao ABSORVIDOS pela `Tela` no
// `AbsorverEcraDoGuest` (`despacho.cpp:1970`) e ENTRAM em `pixels`/`cores` da
// bateria. O numero diz "houve desenho" (e houve: o titulo pediu-o); nao diz quem
// o fez. E a razao pela qual este modulo CONTA os proprios pixeis
// (`pixeis_escritos`) e os publica no resumo.
void Graficos::LigarATela() {
  if (tem_tela_) return;
  // O ECRA SO GANHA PAGINA NO GUEST QUANDO ALGUEM PEDE O BITMAP DO ECRA
  // (`Despacho::EscreverCabecalhoDoBitmapDoEcra`, que corre no
  // `IDisplay::GetDeviceBitmap`): "um titulo que nunca o peca nao paga os
  // 614 400 bytes nem as copias" (`despacho.h:631`). MEDIDO no `allstarcards`:
  // ele NUNCA o pede, e a pagina 0x82000000 nao existe nessa corrida -- logo o
  // `DrawRect` responde que nao tem destino (por nome), em vez de escrever num
  // terceiro sitio. A tentativa fica aqui, e nao em cada pixel: quando a pagina
  // aparece, o `tem_tela_` fica verdadeiro e nao ha mais nenhuma consulta.
  // SE A PAGINA NAO EXISTE, O DESTINO E CRIADO -- e nao recusado. O ecra existe
  // sempre; o que era condicional era a COPIA no guest, que so nascia a pedido do
  // titulo. Um titulo que desenhe 2D sem pedir o bitmap do ecra ficava sem destino
  // e a falta dizia "sem pBmp" -- honesta quanto ao sintoma e enganadora quanto a
  // causa (o ecra sempre existiu; faltava criar a pagina). MEDIDO no `allstarcards`:
  // 296 `DrawRect` por quadro, zero pedidos de bitmap do ecra, 296 recusas.
  if (!mem_.Existe(kBaseDoEcraNoGuest) && criar_ecra_) {
    criar_ecra_();
  }
  tem_tela_ = mem_.Existe(kBaseDoEcraNoGuest);
}

void Graficos::RecalcularCaixa() {
  // O clip EM VIGOR = viewport x clip x ecra. Calculado quando o estado muda (e
  // nao em cada pixel): o `Tela::Retangulo` ja pagou o defeito do limite
  // verificado tarde -- "um limite verificado so no destino nao limita o
  // trabalho" (`tela.h:44-49`).
  int x0 = static_cast<int>(estado_.viewport_x);
  int y0 = static_cast<int>(estado_.viewport_y);
  int x1 = x0 + static_cast<int>(estado_.viewport_largura);
  int y1 = y0 + static_cast<int>(estado_.viewport_altura);
  if (estado_.clip_tipo == kClipRectangulo) {
    x0 = std::max(x0, static_cast<int>(estado_.clip_x));
    y0 = std::max(y0, static_cast<int>(estado_.clip_y));
    x1 = std::min(x1, static_cast<int>(estado_.clip_x + estado_.clip_largura));
    y1 = std::min(y1, static_cast<int>(estado_.clip_y + estado_.clip_altura));
  }
  x0 = std::max(x0, 0);
  y0 = std::max(y0, 0);
  x1 = std::min(x1, static_cast<int>(kLarguraDoEcra));
  y1 = std::min(y1, static_cast<int>(kAlturaDoEcra));
  caixa_x0_ = x0;
  caixa_y0_ = y0;
  caixa_x1_ = x1;
  caixa_y1_ = y1;
}

bool Graficos::DentroDoClip(int x, int y) const {
  return x >= caixa_x0_ && y >= caixa_y0_ && x < caixa_x1_ && y < caixa_y1_;
}

void Graficos::EscreverPixel(int x, int y, std::uint16_t cor) {
  if (!DentroDoClip(x, y)) return;
  LigarATela();
  if (!tem_tela_) return;
  const std::uint32_t onde =
      kBaseDoEcraNoGuest +
      (static_cast<std::uint32_t>(y) * kLarguraDoEcra + static_cast<std::uint32_t>(x)) * 2u;
  if (estado_.modo_de_pintura == kPinturaXor) {
    // `AEE_PAINT_XOR`: "the color will be determined by the XOR binary operation
    // of the old and new colors", e o cabecalho diz para que serve -- "XOR the
    // same foreground color against the background to recover the background"
    // (`AEEGraphics.h`, documentacao do `SetPaintMode`).
    cor = static_cast<std::uint16_t>(mem_.Ler16(onde) ^ cor);
  }
  mem_.Escrever16(onde, cor);
  ++estado_.pixeis_escritos;
}

void Graficos::PreencherRect(int x, int y, int largura, int altura, std::uint16_t cor) {
  // O LIMITE ANTES DE PERCORRER: uma rect de 30000 pixeis vinda do guest nao pode
  // custar 900 milhoes de iteracoes (foi um defeito medido, mais de 900 s para um
  // titulo).
  const int x0 = std::max(x, caixa_x0_);
  const int y0 = std::max(y, caixa_y0_);
  const int x1 = std::min(x + largura, caixa_x1_);
  const int y1 = std::min(y + altura, caixa_y1_);
  if (x0 >= x1 || y0 >= y1) return;
  for (int j = y0; j < y1; ++j) {
    for (int i = x0; i < x1; ++i) EscreverPixel(i, j, cor);
  }
}

void Graficos::MolduraRect(int x, int y, int largura, int altura, std::uint16_t cor) {
  if (largura <= 0 || altura <= 0) return;
  for (int i = 0; i < largura; ++i) {
    EscreverPixel(x + i, y, cor);
    EscreverPixel(x + i, y + altura - 1, cor);
  }
  for (int j = 0; j < altura; ++j) {
    EscreverPixel(x, y + j, cor);
    EscreverPixel(x + largura - 1, y + j, cor);
  }
}

void Graficos::AplicarFlagsDoRect(int x, int y, int largura, int altura, std::uint32_t nflag) {
  // "If nFlag is set to AEE_GRAPHICS_FRAME, it draws the frame using the current
  // foreground color and the paint mode, while setting the clipping region. If
  // nFlag is set to AEE_GRAPHICS_CLEAR, it clears the interior ... If nFlag is
  // set to AEE_GRAPHICS_FILL, it fills the interior using the current fill color
  // and the current paint mode" (`AEEGraphics.h`, documentacao do `SetClip`).
  if ((nflag & kGraphicsFrame) != 0) {
    MolduraRect(x, y, largura, altura, ParaCor565(estado_.cor_de_frente));
  }
  if ((nflag & kGraphicsClear) != 0) {
    PreencherRect(x, y, largura, altura, ParaCor565(estado_.cor_de_fundo));
  }
  if ((nflag & kGraphicsFill) != 0) {
    PreencherRect(x, y, largura, altura, ParaCor565(estado_.cor_de_preenchimento));
  }
}

// ---------------------------------------------------------------------------
// OS GETS: ESCREVEM O QUE ESTA GUARDADO (e nao zero)
// ---------------------------------------------------------------------------
bool Graficos::EscreverByteDoGuest(std::uint32_t onde, std::uint8_t v, bool* recusado) {
  if (onde == 0) return false;  // um ponteiro NULO nao e um erro: o SDK aceita-o
  if (!mem_.Existe(onde)) {
    *recusado = true;
    return false;
  }
  mem_.Escrever8(onde, v);
  return true;
}

// O ALFA, que e o QUINTO argumento de `SetColor`/`SetFillColor`.
//
// O QUINTO ARGUMENTO DO AAPCS ESTA NA PILHA (`[sp+0]`), e NAO no r4 -- e o
// desmonte do `allstarcards` que o diz, no sitio de chamada (0x202b0-0x202e0):
//
//     202b0  and  r8, r1, #255     ; r8 = alfa (o byte baixo de um MAKE_RGBA)
//     202bc  str  r8, [sp]         ; <<< o quinto argumento, empilhado
//     202d8  ldr  ip, [r1, #16]    ; slot 4 = SetColor (16/4)
//     202e0  blx  ip               ; e o lr medido na recusa e 0x202e4
//
// MEDIDO: os 296 pedidos do `allstarcards` trazem `[sp+0] = 0`, e o cabecalho diz
// que o alfa "is just a placeholder that has no effect on the foreground color"
// -- fica guardado e devolvido, sem mentir sobre o que fez. O `zeemu` le-o do
// `REG_R4` (`BrewGraphics.cpp:155`) e essa e a divergencia dele, declarada.
std::uint8_t Graficos::AlfaDaPilha(ICpu& cpu) const {
  const std::uint32_t sp = cpu.Get(kSP);
  if (!mem_.Existe(sp)) return 0;
  return static_cast<std::uint8_t>(mem_.Ler32(sp) & 0xFFu);
}

// ---------------------------------------------------------------------------
// O ATENDIMENTO
// ---------------------------------------------------------------------------
Atendido Graficos::Atender(ICpu& cpu, std::uint32_t indice) {
  if (!EMeu(indice)) return Atendido::NaoEMeu;
  const std::uint32_t slot = indice - VtGenerico(kIndiceDoGraphics);
  const std::uint64_t bit = (slot < 64) ? (1ull << slot) : 0ull;
  if (bit != 0 && (slots_conferidos_ & bit) == 0) {
    if (!ConferirSlot(slot)) {
      cpu.Set(kR0, kAeeUnsupported);
      return Atendido::NaoImplementado;
    }
    slots_conferidos_ |= bit;
  }
  // O ESTADO (2..19) SERVIDO A SERIO, o `DrawRect` (22) a desenhar, e todo o
  // resto RECUSA COM O NOME (P2).
  if (slot >= kGraphics_SetBackground && slot <= kGraphics_GetColorDepth) {
    return AtenderEstado(cpu, slot);
  }
  if (slot == kGraphics_Translate) {
    // `void Translate(IGraphics *po, int16 x, int16 y)` (AEEGraphics.h:255).
    // Translada a origem do sistema de coordenadas do mundo em (x, y).
    estado_.origem_x = static_cast<std::int16_t>(cpu.Get(kR1));
    estado_.origem_y = static_cast<std::int16_t>(cpu.Get(kR2));
    cpu.Set(kR0, 0);
    return Atendido::Feito;
  }
  if (slot == kGraphics_DrawRect) return DesenharRect(cpu);
  return NaoImplementado(cpu, slot);
}

Atendido Graficos::NaoImplementado(ICpu& cpu, std::uint32_t slot) {
  // O NOME, e nao o numero: `NomeDeGraphics` vem do `.inc` GERADO, e uma recusa
  // que diga `slot23` obriga a ir ao cabecalho contar em cada ronda -- que e o
  // defeito que a tabela de nomes existe para tirar do caminho.
  char det[176];
  std::snprintf(det, sizeof(det),
                "slot=%u metodo=%s r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x lr=0x%08x", slot,
                NomeDeGraphics(slot), cpu.Get(kR0), cpu.Get(kR1), cpu.Get(kR2), cpu.Get(kR3),
                cpu.Get(kLR));
  RegistarRecusa(std::string("IGraphics::") + NomeDeGraphics(slot), det);
  // `EUNSUPPORTED` (20): nao ha implementacao. Nao se devolve SUCCESS com um
  // desenho que nao aconteceu -- seria o stub silencioso outra vez.
  cpu.Set(kR0, kAeeUnsupported);
  return Atendido::NaoImplementado;
}

Atendido Graficos::AtenderEstado(ICpu& cpu, std::uint32_t slot) {
  const std::uint32_t r1 = cpu.Get(kR1);
  const std::uint32_t r2 = cpu.Get(kR2);
  const std::uint32_t r3 = cpu.Get(kR3);
  bool recusado = false;
  switch (slot) {
    // --- as cores ----------------------------------------------------------
    case kGraphics_SetBackground: {
      // `RGBVAL SetBackground(po, r, g, b)`: "The updated RGB value for the
      // current background color". O alfa NAO existe nesta chamada e fica 255.
      estado_.cor_de_fundo =
          CorRgba{static_cast<std::uint8_t>(r1), static_cast<std::uint8_t>(r2),
                  static_cast<std::uint8_t>(r3), 255};
      cpu.Set(kR0, ParaRgbval(estado_.cor_de_fundo, false));
      return Atendido::Feito;
    }
    case kGraphics_GetBackground: {
      EscreverByteDoGuest(r1, estado_.cor_de_fundo.r, &recusado);
      EscreverByteDoGuest(r2, estado_.cor_de_fundo.g, &recusado);
      EscreverByteDoGuest(r3, estado_.cor_de_fundo.b, &recusado);
      cpu.Set(kR0, 0);
      if (recusado) {
        RegistarRecusa("IGraphics::GetBackground ponteiro fora do mapa",
                       "r1=0x" + Hex(r1) + " r2=0x" + Hex(r2) + " r3=0x" + Hex(r3));
        return Atendido::NaoImplementado;
      }
      return Atendido::Feito;
    }
    case kGraphics_SetColor: {
      estado_.cor_de_frente = CorRgba{static_cast<std::uint8_t>(r1),
                                      static_cast<std::uint8_t>(r2),
                                      static_cast<std::uint8_t>(r3), AlfaDaPilha(cpu)};
      cpu.Set(kR0, ParaRgbval(estado_.cor_de_frente, true));
      return Atendido::Feito;
    }
    case kGraphics_GetColor: {
      // O `alpha` de saida e o QUARTO ponteiro, e o quarto argumento de um metodo
      // de 5 argumentos esta na PILHA: `GetColor(po, &r, &g, &b, &a)` -> o `&a`
      // e o `[sp+0]`. A leitura confirma-se pelo `zeemu`, que o le do `REG_SP`
      // (`BrewGraphics.cpp:160`).
      const std::uint32_t sp = cpu.Get(kSP);
      const std::uint32_t palfa = mem_.Existe(sp) ? mem_.Ler32(sp) : 0u;
      EscreverByteDoGuest(r1, estado_.cor_de_frente.r, &recusado);
      EscreverByteDoGuest(r2, estado_.cor_de_frente.g, &recusado);
      EscreverByteDoGuest(r3, estado_.cor_de_frente.b, &recusado);
      EscreverByteDoGuest(palfa, estado_.cor_de_frente.a, &recusado);
      cpu.Set(kR0, 0);
      if (recusado) {
        RegistarRecusa("IGraphics::GetColor ponteiro fora do mapa",
                       "r1=0x" + Hex(r1) + " r2=0x" + Hex(r2) + " r3=0x" + Hex(r3) +
                           " sp0=0x" + Hex(palfa));
        return Atendido::NaoImplementado;
      }
      return Atendido::Feito;
    }
    // --- o modo de preenchimento -------------------------------------------
    case kGraphics_SetFillMode: {
      // "If an invalid value (other than TRUE or FALSE) is passed as input
      // parameter, the fill mode is set to FALSE by default" -- logo TRUE e o 1 e
      // todo o resto DESLIGA.
      const bool novo = (r1 == 1u);
      estado_.preenche = novo;
      cpu.Set(kR0, novo ? 1u : 0u);
      return Atendido::Feito;
    }
    case kGraphics_GetFillMode: {
      cpu.Set(kR0, estado_.preenche ? 1u : 0u);
      return Atendido::Feito;
    }
    case kGraphics_SetFillColor: {
      estado_.cor_de_preenchimento = CorRgba{static_cast<std::uint8_t>(r1),
                                             static_cast<std::uint8_t>(r2),
                                             static_cast<std::uint8_t>(r3), AlfaDaPilha(cpu)};
      cpu.Set(kR0, ParaRgbval(estado_.cor_de_preenchimento, true));
      return Atendido::Feito;
    }
    case kGraphics_GetFillColor: {
      const std::uint32_t sp = cpu.Get(kSP);
      const std::uint32_t palfa = mem_.Existe(sp) ? mem_.Ler32(sp) : 0u;
      EscreverByteDoGuest(r1, estado_.cor_de_preenchimento.r, &recusado);
      EscreverByteDoGuest(r2, estado_.cor_de_preenchimento.g, &recusado);
      EscreverByteDoGuest(r3, estado_.cor_de_preenchimento.b, &recusado);
      EscreverByteDoGuest(palfa, estado_.cor_de_preenchimento.a, &recusado);
      cpu.Set(kR0, 0);
      if (recusado) {
        RegistarRecusa("IGraphics::GetFillColor ponteiro fora do mapa",
                       "r1=0x" + Hex(r1) + " r2=0x" + Hex(r2) + " r3=0x" + Hex(r3) +
                           " sp0=0x" + Hex(palfa));
        return Atendido::NaoImplementado;
      }
      return Atendido::Feito;
    }
    // --- o tamanho do ponto ------------------------------------------------
    case kGraphics_SetPointSize: {
      estado_.tamanho_do_ponto = r1 & 0xFFu;
      cpu.Set(kR0, estado_.tamanho_do_ponto);
      return Atendido::Feito;
    }
    case kGraphics_GetPointSize: {
      cpu.Set(kR0, estado_.tamanho_do_ponto);
      return Atendido::Feito;
    }
    // --- o clip ------------------------------------------------------------
    case kGraphics_SetClip: {
      const std::uint32_t forma = r1;
      if (forma == 0) {
        // "If the pointer pShape is NULL, it resets the clipping region the
        // window." -- o clip volta a ser a JANELA.
        estado_.clip_tipo = kClipNenhum;
        RecalcularCaixa();
        cpu.Set(kR0, 1);
        return Atendido::Feito;
      }
      if (!PaginaDoGuest(mem_, forma, kTamanhoDoClip)) {
        RegistarRecusa("IGraphics::SetClip forma fora do mapa", "pShape=0x" + Hex(forma));
        cpu.Set(kR0, 0);
        return Atendido::NaoImplementado;
      }
      const std::uint32_t tipo = mem_.Ler8(forma + kClipTipo);
      if (tipo != kClipNenhum && tipo != kClipRectangulo) {
        // AS OUTRAS CINCO FORMAS RECUSAM COM O NOME (P2): nao se guarda um clip
        // que nao se aplica -- seria a mesma mentira do `SetColor` que devolve
        // sucesso sem guardar nada. O VALOR da forma vai no detalhe, para se
        // saber QUAL foi pedida.
        RegistarRecusa("IGraphics::SetClip forma nao implementada",
                       "tipo=" + std::to_string(tipo) +
                           " (so CLIPPING_NONE=0 e CLIPPING_RECT=1)");
        cpu.Set(kR0, 0);
        return Atendido::NaoImplementado;
      }
      int x = 0, y = 0, largura = 0, altura = 0;
      if (tipo == kClipRectangulo) {
        LerRectDoGuest(mem_, forma + kClipRect, &x, &y, &largura, &altura);
      }
      estado_.clip_tipo = tipo;
      estado_.clip_x = static_cast<std::uint32_t>(x);
      estado_.clip_y = static_cast<std::uint32_t>(y);
      estado_.clip_largura = static_cast<std::uint32_t>(largura);
      estado_.clip_altura = static_cast<std::uint32_t>(altura);
      // O CLIP E POSTO ANTES DE OS `nFlag` DESENHAREM: as duas ordens sao
      // defensaveis e o cabecalho nao escolhe. Escolhe-se esta porque o desenho
      // dos `nFlag` e do rectangulo DO CLIP, que fica dentro do proprio clip.
      RecalcularCaixa();
      AplicarFlagsDoRect(x, y, largura, altura, r2);
      cpu.Set(kR0, 1);
      return Atendido::Feito;
    }
    case kGraphics_GetClip: {
      const std::uint32_t destino = r1;
      cpu.Set(kR0, 0);
      if (destino == 0) return Atendido::Feito;  // um ponteiro NULO: nada a fazer
      if (!PaginaDoGuest(mem_, destino, kTamanhoDoClip)) {
        RegistarRecusa("IGraphics::GetClip destino fora do mapa", "pShape=0x" + Hex(destino));
        return Atendido::NaoImplementado;
      }
      mem_.Escrever8(destino + kClipTipo, static_cast<std::uint8_t>(estado_.clip_tipo));
      // O byte de enchimento entre o `type` (int8) e o `union` (alinhado a 2) fica
      // a ZERO: escrever-lhe lixo mudaria o que o guest le de volta.
      mem_.Escrever8(destino + kClipTipo + 1, 0);
      EscreverRectDoGuest(mem_, destino + kClipRect, static_cast<int>(estado_.clip_x),
                          static_cast<int>(estado_.clip_y),
                          static_cast<int>(estado_.clip_largura),
                          static_cast<int>(estado_.clip_altura));
      cpu.Set(kR0, 1);
      return Atendido::Feito;
    }
    // --- o viewport --------------------------------------------------------
    case kGraphics_SetViewport: {
      const std::uint32_t prect = r1;
      const std::uint32_t nflag = r2;
      if (prect == 0) {
        // "If pRect is NULL, this function resets the viewport to the default,
        // and it resets the clipping region to the default viewport."
        estado_.viewport_x = 0;
        estado_.viewport_y = 0;
        estado_.viewport_largura = kLarguraDoEcra;
        estado_.viewport_altura = kAlturaDoEcra;
        estado_.viewport_com_moldura = false;
        estado_.clip_tipo = kClipNenhum;
        RecalcularCaixa();
        cpu.Set(kR0, 1);
        return Atendido::Feito;
      }
      if (!PaginaDoGuest(mem_, prect, 8)) {
        RegistarRecusa("IGraphics::SetViewport rect fora do mapa", "pRect=0x" + Hex(prect));
        cpu.Set(kR0, 0);
        return Atendido::NaoImplementado;
      }
      int x = 0, y = 0, largura = 0, altura = 0;
      LerRectDoGuest(mem_, prect, &x, &y, &largura, &altura);
      const bool moldura = (nflag & kGraphicsFrame) != 0;
      // "It returns TRUE only if the displayable area is non-empty and the
      // viewport is completely contained within the physical screen. Otherwise,
      // it returns FALSE" -- e "The minimum value for dx, dy is 3 pixels" com a
      // moldura e 1 sem ela. UMA RECUSA DE CONTRATO NAO E UMA FALTA: devolve-se
      // FALSE (e o que o cabecalho manda) sem se registar falta nenhuma, e o
      // estado anterior fica intacto.
      const int minimo = moldura ? 3 : 1;
      const bool serve = largura >= minimo && altura >= minimo && x >= 0 && y >= 0 &&
                         x + largura <= static_cast<int>(kLarguraDoEcra) &&
                         y + altura <= static_cast<int>(kAlturaDoEcra);
      if (!serve) {
        ++estado_.viewports_recusados;
        cpu.Set(kR0, 0);
        return Atendido::Feito;
      }
      estado_.viewport_x = static_cast<std::uint32_t>(x);
      estado_.viewport_y = static_cast<std::uint32_t>(y);
      estado_.viewport_largura = static_cast<std::uint32_t>(largura);
      estado_.viewport_altura = static_cast<std::uint32_t>(altura);
      estado_.viewport_com_moldura = moldura;
      RecalcularCaixa();
      // "If AEE_GRAPHICS_FRAME is set, it draws the frame." / "If
      // AEE_GRAPHICS_CLEAR is set, it clears the viewport to the background
      // color." Estes dois `nFlag` desenham; o `FILL` NAO esta documentado para
      // o viewport (so para o `SetClip`) e por isso NAO se aplica aqui.
      if (moldura) {
        MolduraRect(x, y, largura, altura, ParaCor565(estado_.cor_de_frente));
      }
      if ((nflag & kGraphicsClear) != 0) {
        PreencherRect(x, y, largura, altura, ParaCor565(estado_.cor_de_fundo));
      }
      cpu.Set(kR0, 1);
      return Atendido::Feito;
    }
    case kGraphics_GetViewport: {
      const std::uint32_t destino = r1;
      const std::uint32_t pmoldura = r2;
      cpu.Set(kR0, 0);
      if (destino == 0) return Atendido::Feito;
      if (!PaginaDoGuest(mem_, destino, 8)) {
        RegistarRecusa("IGraphics::GetViewport rect fora do mapa", "pRect=0x" + Hex(destino));
        return Atendido::NaoImplementado;
      }
      EscreverRectDoGuest(mem_, destino, static_cast<int>(estado_.viewport_x),
                          static_cast<int>(estado_.viewport_y),
                          static_cast<int>(estado_.viewport_largura),
                          static_cast<int>(estado_.viewport_altura));
      if (pmoldura != 0 && mem_.Existe(pmoldura)) {
        mem_.Escrever8(pmoldura, estado_.viewport_com_moldura ? 1u : 0u);
      }
      cpu.Set(kR0, 1);
      return Atendido::Feito;
    }
    case kGraphics_ClearViewport: {
      // "void ClearViewport(po): clears the current viewport to the background
      // color."
      PreencherRect(static_cast<int>(estado_.viewport_x),
                    static_cast<int>(estado_.viewport_y),
                    static_cast<int>(estado_.viewport_largura),
                    static_cast<int>(estado_.viewport_altura),
                    ParaCor565(estado_.cor_de_fundo));
      cpu.Set(kR0, 0);
      return Atendido::Feito;
    }
    // --- o modo de pintura -------------------------------------------------
    case kGraphics_SetPaintMode: {
      // "In AEE_PAINT_COPY mode, the new graphics content overwrites the previous
      // one. In AEE_PAINT_XOR mode, the color will be determined by the XOR ..."
      // e "If an invalid integer (enum) value is passed as the input 'mode'
      // parameter, the paint mode is set to AEE_PAINT_COPY by default."
      const std::uint32_t pedido = r1 & 0xFFu;
      estado_.modo_de_pintura = (pedido == kPinturaXor) ? kPinturaXor : kPinturaCopy;
      cpu.Set(kR0, estado_.modo_de_pintura);
      return Atendido::Feito;
    }
    case kGraphics_GetPaintMode: {
      cpu.Set(kR0, estado_.modo_de_pintura);
      return Atendido::Feito;
    }
    // --- a profundidade de cor ---------------------------------------------
    case kGraphics_GetColorDepth: {
      cpu.Set(kR0, kProfundidadeDeCor);
      return Atendido::Feito;
    }
    default:
      break;
  }
  return NaoImplementado(cpu, slot);
}

// ---------------------------------------------------------------------------
// O `DrawRect` (slot 22): O PEDIDO QUE O TITULO FAZ 296 VEZES
// ---------------------------------------------------------------------------
Atendido Graficos::DesenharRect(ICpu& cpu) {
  const std::uint32_t prect = cpu.Get(kR1);
  if (prect == 0 || !mem_.Existe(prect)) {
    RegistarRecusa("IGraphics::DrawRect sem rect",
                   "pRect=0x" + Hex(prect) + " lr=0x" + Hex(cpu.Get(kLR)));
    cpu.Set(kR0, kAeeBadParm);  // EBADPARM: "if one of the input parameters is invalid"
    return Atendido::NaoImplementado;
  }
  int x = 0, y = 0, largura = 0, altura = 0;
  if (!LerRectDoGuest(mem_, prect, &x, &y, &largura, &altura)) {
    RegistarRecusa("IGraphics::DrawRect rect fora do mapa", "pRect=0x" + Hex(prect));
    cpu.Set(kR0, kAeeBadParm);
    return Atendido::NaoImplementado;
  }
  x += estado_.origem_x;
  y += estado_.origem_y;
  if (largura < 0 || altura < 0) {
    // `AEERect.dx`/`dy` sao LARGURA e ALTURA (`AEERect.h:35-40`): um valor
    // negativo nao e um rectangulo.
    char det[128];
    std::snprintf(det, sizeof(det), "rect=(%d,%d,%d,%d) lr=0x%08x", x, y, largura, altura,
                  cpu.Get(kLR));
    RegistarRecusa("IGraphics::DrawRect rect com tamanho negativo", det);
    cpu.Set(kR0, kAeeBadParm);
    return Atendido::NaoImplementado;
  }
  ++estado_.rectangulos;
  // OS PRIMEIROS OITO RECTS VAO PARA O TRACO, e depois o modulo cala-se: o
  // `allstarcards` pede 296 por QUADRO, e uma linha por rect enchia o traco de
  // 300 quadros com a mesma coisa. Estes oito sao a MEDICAO de ONDE o titulo
  // desenha (x, y, dx, dy) -- e a resposta a pergunta do sentido do eixo Y.
  if (rects_tracados_ < 8) {
    ++rects_tracados_;
    char det[160];
    std::snprintf(det, sizeof(det),
                  "rect=(%d,%d,%d,%d) preenche=%d cor_de_frente=0x%04x "
                  "cor_de_preenchimento=0x%04x lr=0x%08x",
                  x, y, largura, altura, estado_.preenche ? 1 : 0,
                  ParaCor565(estado_.cor_de_frente), ParaCor565(estado_.cor_de_preenchimento),
                  cpu.Get(kLR));
    traco_.Emitir(Area::Brew, Nivel::Depuracao, "IGRAPHICS_DRAWRECT", det);
  }
  if (largura == 0 || altura == 0) {
    // Um rect vazio nao e um erro do contrato: desenha zero pixeis e responde
    // SUCCESS. Contado, para o relatorio poder dizer quantos foram.
    ++estado_.rectangulos_vazios;
    cpu.Set(kR0, kAeeSuccess);
    return Atendido::Feito;
  }
  // OS PARAMETROS ESTAO BONS. FALTA O DESTINO, e ele e medido: sem a pagina do
  // ecra no guest nao ha onde escrever, e este modulo NAO inventa um terceiro
  // sitio (o segundo seria a `Tela` do rasterizador, que nao e alcancavel
  // destes ficheiros -- ver o cabecalho). A recusa e POR NOME e POR CHAMADA: o
  // numero que ela soma e a DEMANDA ("este titulo pediu desenho 2D N vezes e nao
  // ha destino"), que e o que a lista de faltas existe para dizer.
  LigarATela();
  if (!tem_tela_) {
    // O DETALHE TEM DE CABER (um `snprintf` truncado mente sobre o que disse, e o
    // compilador avisou): a razao longa fica no comentario acima, e aqui fica o
    // que identifica a chamada e o endereco que falta.
    char det2[160];
    std::snprintf(det2, sizeof(det2),
                  "rect=(%d,%d,%d,%d) preenche=%d frente=0x%04x preenchimento=0x%04x "
                  "lr=0x%08x -- sem pBmp em 0x%08x",
                  x, y, largura, altura, estado_.preenche ? 1 : 0,
                  ParaCor565(estado_.cor_de_frente), ParaCor565(estado_.cor_de_preenchimento),
                  cpu.Get(kLR), kBaseDoEcraNoGuest);
    RegistarRecusa("IGraphics::DrawRect sem destino", det2);
    cpu.Set(kR0, kAeeUnsupported);
    return Atendido::NaoImplementado;
  }
  // A ORDEM E A DOS DOIS EMULADORES DE REFERENCIA (`zeemu`
  // `brew/BrewGraphics.cpp:295-302`; `zeebx` `src/machine/display.rs:470-476`):
  // com o preenchimento ligado o interior leva a cor de PREENCHIMENTO, e a
  // moldura leva SEMPRE a cor de FRENTE -- "The color of the outline is the
  // current foreground color. If the fill-mode is turned on, the interior will be
  // filled with the current fill color" (`AEEGraphics.h`, documentacao do
  // `IGRAPHICS_DrawRect`). Como a moldura vem DEPOIS, a cor de frente fica na
  // borda mesmo quando o preenchimento esta ligado -- que e o que os dois fazem.
  if (estado_.preenche) {
    PreencherRect(x, y, largura, altura, ParaCor565(estado_.cor_de_preenchimento));
  }
  MolduraRect(x, y, largura, altura, ParaCor565(estado_.cor_de_frente));
  cpu.Set(kR0, kAeeSuccess);
  return Atendido::Feito;
}

}  // namespace zb2::brew
