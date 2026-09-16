#include "core/video/rasterizador.h"

#include <algorithm>
#include <cmath>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

#include "tools/gl_slots.inc"

namespace zb2::video {

namespace {

// OS NUMEROS DO GL VEM DE `tools/gl_slots.inc`, GERADO de `AEEGL.h` por
// `tools/gerar_slots.py`. Nenhum valor de `GL_*` e escrito a mao aqui: um numero
// de GL escrito a mao ja divergiu uma vez neste trabalho (o mapa do IGLES11 na
// arvore antiga foi COPIADO de outro emulador), e copiar a ordem esta proibido
// por teste (`tools/verificar_slots_gl.sh`).
using namespace gl_slots;

// O tamanho, em bytes, de um elemento de um array de vertices. Devolve 0 para um
// tipo que este rasterizador NAO le -- e o chamador RECUSA com o nome do tipo, em
// vez de ler lixo (P2).
int BytesDoTipo(std::uint32_t tipo) {
  switch (tipo) {
    case GL_BYTE:
    case GL_UNSIGNED_BYTE: return 1;
    case GL_SHORT:
    case GL_UNSIGNED_SHORT: return 2;
    case GL_FIXED:
    case GL_FLOAT: return 4;
    default: return 0;
  }
}

const char* NomeDoTipo(std::uint32_t tipo) {
  switch (tipo) {
    case GL_BYTE: return "GL_BYTE";
    case GL_UNSIGNED_BYTE: return "GL_UNSIGNED_BYTE";
    case GL_SHORT: return "GL_SHORT";
    case GL_UNSIGNED_SHORT: return "GL_UNSIGNED_SHORT";
    case GL_FIXED: return "GL_FIXED";
    case GL_FLOAT: return "GL_FLOAT";
    default: return "tipo_desconhecido";
  }
}

const char* NomeDaPrimitiva(std::uint32_t modo) {
  switch (modo) {
    case GL_POINTS: return "GL_POINTS";
    case GL_LINES: return "GL_LINES";
    case GL_LINE_LOOP: return "GL_LINE_LOOP";
    case GL_LINE_STRIP: return "GL_LINE_STRIP";
    case GL_TRIANGLES: return "GL_TRIANGLES";
    case GL_TRIANGLE_STRIP: return "GL_TRIANGLE_STRIP";
    case GL_TRIANGLE_FAN: return "GL_TRIANGLE_FAN";
    default: return "primitiva_desconhecida";
  }
}

float LerFloat(const Memoria& mem, Endereco onde, std::uint32_t tipo) {
  switch (tipo) {
    case GL_FLOAT: {
      const std::uint32_t bits = mem.Ler32(onde);
      float f = 0.0f;
      std::memcpy(&f, &bits, sizeof(f));
      return f;
    }
    case GL_FIXED:
      return static_cast<float>(static_cast<std::int32_t>(mem.Ler32(onde))) / 65536.0f;
    default:
      return 0.0f;
  }
}

// `P * M * v`, com as matrizes COLUMN-MAJOR (o elemento (linha r, coluna c) esta
// em `m[c*4 + r]`), que e como o `igl.cpp` as guarda e como o GL as define.
void AplicarMatriz(const float* m, const float* v, float* saida) {
  for (int r = 0; r < 4; ++r) {
    saida[r] = m[0 * 4 + r] * v[0] + m[1 * 4 + r] * v[1] + m[2 * 4 + r] * v[2] +
               m[3 * 4 + r] * v[3];
  }
}

// A FUNCAO DE ARESTA. Positiva para os pontos do lado INTERIOR quando o
// triangulo foi ordenado de area positiva (ver `RasterizarTriangulo`), e zero
// em cima da aresta -- que e o caso em que a regra "top-left" decide.
double Aresta(double x0, double y0, double x1, double y1, double px, double py) {
  return (x1 - x0) * (py - y0) - (y1 - y0) * (px - x0);
}

// A REGRA DO CANTO: uma aresta de cima ou de esquerda PERTENCE ao triangulo.
//
// Sem ela, dois triangulos vizinhos reclamam os MESMOS pixels da aresta que
// partilham (o pixel e escrito duas vezes, e a contagem `PIXELS` mente sobre o
// trabalho) ou nao o reclama nenhum (uma fenda de um pixel em toda a costura).
// Os dois casos estao no teste `Rasterizador.ArestaPartilhadaNaoEscreveDuasVezes`.
//
// A DERIVACAO, e nao a citacao: as duas travessias da MESMA aresta por dois
// triangulos orientados de area positiva tem (dx, dy) de sinais opostos. Logo,
// exigir `dy < 0`, ou `dy == 0 && dx > 0`, inclui o pixel em EXACTAMENTE UM dos
// dois. A janela deste rasterizador tem o y para baixo (a `Tela` cresce para
// baixo), e por isso "cima" e o lado do y menor.
bool ArestaDeCanto(double dx, double dy) { return dy < 0.0 || (dy == 0.0 && dx > 0.0); }

// --- as constantes do GL QUE O `.inc` GERADO NAO TEM -------------------------
//
// `tools/gl_slots.inc` e GERADO de `AEEGL.h` e do `gles/gl.h`, e uma linha
// escrita a mao la faz a guarda `tools/verificar_slots_gl.sh` divergir do
// cabecalho. Os factores de mistura e o `glColorMask` NAO sao servidos por esse
// gerador (so os nomes de slot e um punhado de valores o sao), e por isso os
// numeros ficam aqui, com a linha EXACTA de onde vieram:
//
//   BREW-4.0.2-SP19/sdk/inc/gles/gles_1_0/gl.h:112-124
//     GL_SRC_COLOR 0x0300 | GL_ONE_MINUS_SRC_COLOR 0x0301 | GL_SRC_ALPHA 0x0302
//     GL_ONE_MINUS_SRC_ALPHA 0x0303 | GL_DST_ALPHA 0x0304
//     GL_ONE_MINUS_DST_ALPHA 0x0305 | GL_DST_COLOR 0x0306
//     GL_ONE_MINUS_DST_COLOR 0x0307 | GL_SRC_ALPHA_SATURATE 0x0308
constexpr std::uint32_t GL_SRC_COLOR = 0x0300u;
constexpr std::uint32_t GL_ONE_MINUS_SRC_COLOR = 0x0301u;
constexpr std::uint32_t GL_DST_ALPHA = 0x0304u;
constexpr std::uint32_t GL_ONE_MINUS_DST_ALPHA = 0x0305u;
constexpr std::uint32_t GL_DST_COLOR = 0x0306u;
constexpr std::uint32_t GL_ONE_MINUS_DST_COLOR = 0x0307u;

// --- a aritmetica da cor em [0,1] -------------------------------------------
//
// A MISTURA do GL e uma soma de produtos ponderados, e a ponderacao e um
// produto de cores: fazer as duas contas em inteiros de 8 bits perde os bits
// que decidem o resultado (o `GL_DST_COLOR` de um fundo quase preto, por
// exemplo). As cores do fragmento e do destino passam a `float` em [0,1], a
// soma e presa a [0,1] e so no fim volta a 8 bits -- como no zeebx
// (`src/rasterizer.rs`, `pack`/`unpack`).
float Apertar01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// O DESTINO, em [0,1]. A tela e RGB565 e NAO TEM ALFA: o alfa do destino e 1
// (o valor que a especificacao da a um framebuffer sem bits de alfa), e nao um
// zero. Um `GL_DST_ALPHA` de 0 por omissao apagaria o desenho em vez de o
// misturar. A escala e a do numero de bits de cada canal: 31 e 63, e nao 255.
void CorDoDestino(std::uint32_t rgb565, float* c) {
  c[0] = static_cast<float>((rgb565 >> 11) & 0x1Fu) / 31.0f;
  c[1] = static_cast<float>((rgb565 >> 5) & 0x3Fu) / 63.0f;
  c[2] = static_cast<float>(rgb565 & 0x1Fu) / 31.0f;
  c[3] = 1.0f;
}

// 8 bits por canal, com o arredondamento do `pack` do zeebx (`*255 + 0.5`).
Rgba DeBits(float* c) {
  Rgba r;
  const auto canal = [](float v) {
    return static_cast<std::uint8_t>(Apertar01(v) * 255.0f + 0.5f);
  };
  r.r = canal(c[0]);
  r.g = canal(c[1]);
  r.b = canal(c[2]);
  r.a = canal(c[3]);
  return r;
}

// Os nove factores do `glBlendFunc`, com o canal `c` (0..3). O resto vale 1.0,
// que e o `GL_ONE` -- e o que o zeebx faz (`factor`, `rasterizer.rs:2000`).
float FatorDeMistura(std::uint32_t tipo, const float origem[4], const float destino[4], int c) {
  switch (tipo) {
    case GL_ZERO: return 0.0f;
    case GL_SRC_COLOR: return origem[c];
    case GL_ONE_MINUS_SRC_COLOR: return 1.0f - origem[c];
    case GL_SRC_ALPHA: return origem[3];
    case GL_ONE_MINUS_SRC_ALPHA: return 1.0f - origem[3];
    case GL_DST_ALPHA: return destino[3];
    case GL_ONE_MINUS_DST_ALPHA: return 1.0f - destino[3];
    case GL_DST_COLOR: return destino[c];
    case GL_ONE_MINUS_DST_COLOR: return 1.0f - destino[c];
    default: return 1.0f;  // GL_ONE, e o que nao esta nos nove
  }
}

// --- os vectores da luz ------------------------------------------------------

double Comprimento3(const double v[3]) {
  return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

void Normalizar3(double v[3]) {
  const double n = Comprimento3(v);
  if (n <= 0.0) return;
  v[0] /= n;
  v[1] /= n;
  v[2] /= n;
}

double Ponto3(const double a[3], const double b[3]) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// O HOLOFOTE, com a formula do GL ES 1.x: o cosseno e medido entre a direccao do
// cone e a direccao DA LUZ PARA O VERTICE (o contrario de `para_a_luz`).
double Holofote(const EstadoDeRasterizacao::Luz& luz, const double para_a_luz[3]) {
  if (luz.corte_do_holofote >= 180.0f) return 1.0;  // sem cone: luz como as outras
  double direcao[3] = {static_cast<double>(luz.direcao_do_holofote[0]),
                       static_cast<double>(luz.direcao_do_holofote[1]),
                       static_cast<double>(luz.direcao_do_holofote[2])};
  Normalizar3(direcao);
  const double coseno = -(direcao[0] * para_a_luz[0] + direcao[1] * para_a_luz[1] +
                          direcao[2] * para_a_luz[2]);
  const double limite = std::cos(static_cast<double>(luz.corte_do_holofote) *
                                 3.14159265358979323846 / 180.0);
  if (coseno < limite) return 0.0;
  const double base = std::max(0.0, coseno);
  return std::pow(base, static_cast<double>(luz.exponente_do_holofote));
}

// UMA FUNCAO DE COMPARACAO DO GL. A mesma do teste de profundidade, e a mesma
// dos dois usos do alpha test (`glAlphaFuncx` e o `compare` do zeebx,
// `rasterizer.rs:1962`). O `default` e `true`: e o `GL_ALWAYS` da omissao.
bool Comparar(std::uint32_t funcao, float valor, float referencia) {
  switch (funcao) {
    case GL_NEVER: return false;
    case GL_LESS: return valor < referencia;
    case GL_EQUAL: return valor == referencia;
    case GL_LEQUAL: return valor <= referencia;
    case GL_GREATER: return valor > referencia;
    case GL_NOTEQUAL: return valor != referencia;
    case GL_GEQUAL: return valor >= referencia;
    default: return true;
  }
}

}  // namespace

// --- a cor ----------------------------------------------------------------

Rgba Desempacotar(std::uint32_t rgba) {
  Rgba c;
  c.r = static_cast<std::uint8_t>(rgba & 0xFFu);
  c.g = static_cast<std::uint8_t>((rgba >> 8) & 0xFFu);
  c.b = static_cast<std::uint8_t>((rgba >> 16) & 0xFFu);
  c.a = static_cast<std::uint8_t>((rgba >> 24) & 0xFFu);
  return c;
}

std::uint32_t Para565(Rgba c) {
  // A tela e RGB565 (`tela.h`: `CorAtual(std::uint32_t rgb565)`, 5+6+5) e o
  // `egl.cpp` serve o config a dizer o mesmo (EGL_RED_SIZE 5, GREEN 6, BLUE 5).
  // Os bits mais baixos sao DESCARTADOS, e nao arredondados: e o que uma
  // conversao de 8 para 5/6 bits faz por truncagem, e o teste fixa o resultado.
  return (static_cast<std::uint32_t>(c.r >> 3) << 11) |
         (static_cast<std::uint32_t>(c.g >> 2) << 5) | static_cast<std::uint32_t>(c.b >> 3);
}

// --- a superficie de producao ---------------------------------------------

void DestinoTela::Escrever(int x, int y, std::uint32_t rgb565) {
  tela_->CorAtual(rgb565);
  tela_->Ponto(x, y);
}

void DestinoTela::Limpar(std::uint32_t rgb565) {
  // O CLIP E POSTO DE LADO E REPOSTO: no GL o `glClear` nao e limitado pelo clip
  // (quem o limita e o `glScissor`, e o scissor nao esta implementado). O clip da
  // `Tela` e do `IDisplay` -- um `glClear` depois de um `DrawRect` com clip
  // deixaria a maior parte da tela por limpar.
  const std::uint32_t* guardado = tela_->ClipAtual();
  const std::uint32_t clip[4] = {guardado[0], guardado[1], guardado[2], guardado[3]};
  tela_->ClipLimpo();
  tela_->CorAtual(rgb565);
  tela_->Retangulo(0, 0, static_cast<std::uint32_t>(zb2::brew::Tela::kLargura),
                   static_cast<std::uint32_t>(zb2::brew::Tela::kAltura), true);
  tela_->Clip(clip[0], clip[1], clip[2], clip[3]);
}

// --- o rasterizador -------------------------------------------------------

Rasterizador::Rasterizador(Memoria& mem, Superficie& superficie)
    : mem_(mem), superficie_(superficie) {
  largura_ = superficie_.Largura();
  altura_ = superficie_.Altura();
}

void Rasterizador::PrepararProfundidade() {
  const int l = superficie_.Largura(), a = superficie_.Altura();
  if (l != largura_ || a != altura_) {
    largura_ = l;
    altura_ = a;
    profundidade_.clear();
  }
  if (largura_ <= 0 || altura_ <= 0) return;
  if (profundidade_.size() != static_cast<std::size_t>(largura_) * static_cast<std::size_t>(altura_)) {
    // O BUFFER DE PROFUNDIDADE NASCE A UM: nenhum desenho passa um `GL_LESS`
    // contra um buffer a zero, e um zero nao escrito seria "a profundidade mais
    // proxima" -- todos os pixels seguintes falhariam o teste em silencio.
    profundidade_.assign(static_cast<std::size_t>(largura_) * static_cast<std::size_t>(altura_), 1.0f);
  }
}

float Rasterizador::ProfundidadeEm(int x, int y) const {
  const int largura = superficie_.Largura(), altura = superficie_.Altura();
  if (x < 0 || y < 0 || x >= largura || y >= altura) return 1.0f;
  const std::size_t indice = static_cast<std::size_t>(y) * static_cast<std::size_t>(largura) +
                             static_cast<std::size_t>(x);
  if (indice >= profundidade_.size()) return 1.0f;
  return profundidade_[indice];
}

std::uint64_t Rasterizador::Limpar(const EstadoDeRasterizacao& estado, std::string* motivo) {
  if (superficie_.Largura() <= 0 || superficie_.Altura() <= 0) {
    if (motivo != nullptr) *motivo = "sem superficie onde escrever";
    return 0;
  }
  std::uint64_t escritos = 0;
  if ((estado.mascara_de_limpeza & GL_COLOR_BUFFER_BIT) != 0) {
    superficie_.Limpar(Para565(estado.cor_de_limpeza));
    escritos = static_cast<std::uint64_t>(superficie_.Largura()) *
               static_cast<std::uint64_t>(superficie_.Altura());
    pixels_ += escritos;
  }
  if ((estado.mascara_de_limpeza & GL_DEPTH_BUFFER_BIT) != 0) {
    PrepararProfundidade();
    if (!profundidade_.empty()) {
      std::fill(profundidade_.begin(), profundidade_.end(), estado.profundidade_de_limpeza);
    }
  }
  return escritos;
}

bool Rasterizador::LerVertice(const EstadoDeRasterizacao& e, std::uint32_t indice, Vertice* v,
                              std::string* motivo) const {
  const ArrayDoCliente& av = e.vertices;
  if (!av.ligado) {
    *motivo = "desenho sem array de vertices ligado";
    return false;
  }
  if (av.tamanho < 2 || av.tamanho > 4) {
    *motivo = "array de vertices com tamanho fora de 2..4";
    return false;
  }
  const int bytes = BytesDoTipo(av.tipo);
  if (bytes == 0 || (av.tipo != GL_FLOAT && av.tipo != GL_FIXED)) {
    *motivo = std::string("array de vertices do tipo ") + NomeDoTipo(av.tipo) +
              ": o rasterizador so le GL_FLOAT e GL_FIXED (e o `igl.cpp` aceita mais tipos)";
    return false;
  }
  const std::uint32_t passo = (av.passo != 0) ? av.passo
                                             : static_cast<std::uint32_t>(av.tamanho * bytes);
  const Endereco base = av.ponteiro + passo * indice;
  float p[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  for (int k = 0; k < av.tamanho; ++k) {
    p[k] = LerFloat(mem_, base + static_cast<Endereco>(k * bytes), av.tipo);
  }
  float clip[4];
  AplicarMatriz(e.modelview, p, clip);
  float clip2[4];
  AplicarMatriz(e.projection, clip, clip2);
  for (int k = 0; k < 4; ++k) v->clip[k] = clip2[k];
  // A POSICAO NO ESPACO DO OLHO e o resultado da modelview (o `clip` desta
  // linha), e nao uma segunda conta: a luz do GL vive em coordenadas de olho.
  for (int k = 0; k < 4; ++k) v->olho[k] = clip[k];

  // --- a cor do vertice ---------------------------------------------------
  v->cor = e.cor;
  if (e.cor_por_vertice) {
    const ArrayDoCliente& ac = e.cores;
    if (!ac.ligado) {
      *motivo = "cor por vertice ligada sem array de cores definido";
      return false;
    }
    const int bcor = BytesDoTipo(ac.tipo);
    if (bcor == 0) {
      *motivo = std::string("array de cores do tipo ") + NomeDoTipo(ac.tipo) + " sem caminho";
      return false;
    }
    const std::uint32_t passo_cor =
        (ac.passo != 0) ? ac.passo : static_cast<std::uint32_t>(ac.tamanho * bcor);
    const Endereco base_cor = ac.ponteiro + passo_cor * indice;
    std::uint8_t canais[4] = {255, 255, 255, 255};
    for (int k = 0; k < ac.tamanho && k < 4; ++k) {
      const Endereco onde = base_cor + static_cast<Endereco>(k * bcor);
      if (ac.tipo == GL_UNSIGNED_BYTE) {
        canais[k] = mem_.Ler8(onde);
      } else if (ac.tipo == GL_FIXED) {
        const float f = LerFloat(mem_, onde, GL_FIXED);
        canais[k] = static_cast<std::uint8_t>(std::min(255.0f, std::max(0.0f, f * 255.0f + 0.5f)));
      } else {
        *motivo = std::string("array de cores do tipo ") + NomeDoTipo(ac.tipo) +
                  ": so GL_UNSIGNED_BYTE e GL_FIXED tem caminho";
        return false;
      }
    }
    v->cor = Rgba{canais[0], canais[1], canais[2], canais[3]};
  }

  // --- a ILUMINACAO, que decide a cor do vertice ---------------------------
  //
  // A cor do vertice passa a ser calculada AQUI, e nao interpolada ate ao pixel
  // e depois corrigida: no GL ES 1.x a luz e uma conta POR VERTICE, e o
  // resultado e o que se interpola. Faze-la por fragmento daria outra imagem
  // (mais lisa) e outra despesa.
  v->normal[0] = 0.0f;
  v->normal[1] = 0.0f;
  v->normal[2] = 1.0f;  // a omissao do GL, quando nao ha array de normais
  if (e.iluminacao_ligada) {
    float normal_do_objeto[3] = {0.0f, 0.0f, 1.0f};
    if (e.normais.ligado) {
      if (!LerNormalDeObjeto(e, indice, normal_do_objeto, motivo)) return false;
    }
    TransformarNormal(e, normal_do_objeto, v->normal);
    v->cor = CorIluminada(e, v->olho, v->normal, v->cor);
  }

  // --- as coordenadas de textura ------------------------------------------
  v->u = 0.0f;
  v->v = 0.0f;
  if (e.textura_ligada) {
    const ArrayDoCliente& at = e.coordenadas_de_textura;
    if (!at.ligado) {
      *motivo = "textura ligada sem array de coordenadas de textura";
      return false;
    }
    const int bt = BytesDoTipo(at.tipo);
    if (bt == 0 || (at.tipo != GL_FLOAT && at.tipo != GL_FIXED)) {
      *motivo = std::string("array de coordenadas do tipo ") + NomeDoTipo(at.tipo) +
                ": o rasterizador so le GL_FLOAT e GL_FIXED";
      return false;
    }
    const std::uint32_t passo_t =
        (at.passo != 0) ? at.passo : static_cast<std::uint32_t>(at.tamanho * bt);
    const Endereco base_t = at.ponteiro + passo_t * indice;
    v->u = LerFloat(mem_, base_t, at.tipo);
    v->v = LerFloat(mem_, base_t + static_cast<Endereco>(bt), at.tipo);
  }
  return true;
}

void Rasterizador::TransformarNormal(const EstadoDeRasterizacao& e, const float n[3], float saida[3]) {
  // A MATRIZ DAS NORMAIS E A TRANSPOSTA DA INVERSA da parte 3x3 da modelview, e
  // NAO a modelview: com escala nao uniforme, uma normal transformada como se
  // fosse uma direccao deixa de ser perpendicular a superficie e a luz escorrega
  // pelo modelo (e o mesmo que o zeebx documenta em `matriz_de_normais`,
  // `src/rasterizer.rs:1917`).
  const float* m = e.modelview;
  // (linha, coluna) = m[coluna * 4 + linha]: as matrizes sao COLUMN-MAJOR.
  const double a[3][3] = {{m[0], m[4], m[8]}, {m[1], m[5], m[9]}, {m[2], m[6], m[10]}};
  const double det = a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) -
                     a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
                     a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
  double inversa[3][3];
  if (std::fabs(det) <= 1e-12) {
    // MATRIZ DEGENERADA: a parte 3x3 crua e o menos errado que se pode devolver
    // (o mesmo que o zeebx faz). Inventar uma inversa seria pior.
    for (int i = 0; i < 3; ++i) {
      for (int j = 0; j < 3; ++j) inversa[i][j] = a[i][j];
    }
  } else {
    for (int i = 0; i < 3; ++i) {
      for (int j = 0; j < 3; ++j) {
        // A adjunta e a transposta da matriz dos cofactores: `adj[i][j]` e o
        // cofactor de `a[j][i]`, e a inversa e `adj / det`.
        const int i1 = (i + 1) % 3, i2 = (i + 2) % 3;
        const int j1 = (j + 1) % 3, j2 = (j + 2) % 3;
        const double cofactor = a[j1][i1] * a[j2][i2] - a[j1][i2] * a[j2][i1];
        inversa[i][j] = cofactor / det;
      }
    }
  }
  // `N = transposta(inversa)`: com `inversa` em (linha, coluna), o elemento
  // (i, j) da transposta e `inversa[j][i]`.
  double v[3] = {static_cast<double>(n[0]), static_cast<double>(n[1]), static_cast<double>(n[2])};
  double r[3];
  for (int i = 0; i < 3; ++i) {
    r[i] = inversa[0][i] * v[0] + inversa[1][i] * v[1] + inversa[2][i] * v[2];
  }
  // O `GL_RESCALE_NORMAL`: a normal e escalada pelo inverso do comprimento da
  // TERCEIRA LINHA da inversa da modelview -- a formula do `_mesa_rescale_normal`
  // do Mesa, que usa `m[2], m[6], m[10]` da inversa (o que, em column-major, e a
  // terceira LINHA). E o que impede uma escala nao uniforme de apagar a luz.
  if (e.reescalar_normais) {
    const double linha[3] = {inversa[2][0], inversa[2][1], inversa[2][2]};
    const double n2 = Comprimento3(linha);
    if (n2 > 1e-12) {
      const double fator = 1.0 / n2;
      r[0] *= fator;
      r[1] *= fator;
      r[2] *= fator;
    }
  }
  if (e.normalizar_normais) Normalizar3(r);
  saida[0] = static_cast<float>(r[0]);
  saida[1] = static_cast<float>(r[1]);
  saida[2] = static_cast<float>(r[2]);
}

bool Rasterizador::LerNormalDeObjeto(const EstadoDeRasterizacao& e, std::uint32_t indice,
                                     float n[3], std::string* motivo) const {
  const ArrayDoCliente& an = e.normais;
  const int bytes = BytesDoTipo(an.tipo);
  if (bytes == 0) {
    *motivo = std::string("array de normais do tipo ") + NomeDoTipo(an.tipo) + " sem caminho";
    return false;
  }
  const std::uint32_t passo = (an.passo != 0) ? an.passo
                                             : static_cast<std::uint32_t>(an.tamanho * bytes);
  const Endereco base = an.ponteiro + passo * indice;
  for (int k = 0; k < 3 && k < an.tamanho; ++k) {
    const Endereco onde = base + static_cast<Endereco>(k * bytes);
    switch (an.tipo) {
      case GL_FLOAT:
      case GL_FIXED:
        n[k] = LerFloat(mem_, onde, an.tipo);
        break;
      case GL_BYTE:
        // AS CONVERSOES SAO AS DA ESPECIFICACAO: um componente guardado em N
        // bits e lido como `c / (2^(N-1) - 1)`, o que poe 127 em 1 e -128 em
        // -1.0078. E o que o Mesa faz (`_mesa_unpack_normal`), e sem isto uma
        // normal em bytes ficaria com valores 100 vezes maiores que 1.
        n[k] = static_cast<float>(static_cast<std::int8_t>(mem_.Ler8(onde))) / 127.0f;
        break;
      case GL_SHORT:
        n[k] = static_cast<float>(static_cast<std::int16_t>(mem_.Ler16(onde))) / 32767.0f;
        break;
      default:
        *motivo = std::string("array de normais do tipo ") + NomeDoTipo(an.tipo) + " sem caminho";
        return false;
    }
  }
  return true;
}

Rgba Rasterizador::CorIluminada(const EstadoDeRasterizacao& e, const float olho[4],
                                const float normal[3], const Rgba& cor_do_vertice) {
  const EstadoDeRasterizacao::Aparencia& material = e.material;
  // O `GL_COLOR_MATERIAL`: a cor do vertice TOMA O LUGAR da ambiente e da difusa
  // do material. E o unico caminho pelo qual um vector de cores continua a
  // valer com a luz ligada -- sem ele, um titulo que pinte os vertices e ligue
  // o `GL_LIGHTING` fica com a cor do material e o vector fica decorativo.
  float ambiente[4];
  float difusa[4];
  if (e.cor_do_material) {
    ambiente[0] = difusa[0] = static_cast<float>(cor_do_vertice.r) / 255.0f;
    ambiente[1] = difusa[1] = static_cast<float>(cor_do_vertice.g) / 255.0f;
    ambiente[2] = difusa[2] = static_cast<float>(cor_do_vertice.b) / 255.0f;
    ambiente[3] = difusa[3] = static_cast<float>(cor_do_vertice.a) / 255.0f;
  } else {
    for (int k = 0; k < 4; ++k) {
      ambiente[k] = material.ambiente[k];
      difusa[k] = material.difusa[k];
    }
  }

  //   cor = emissao + ambiente_do_material * ambiente_da_cena
  //       + SOMA( atenuacao * holofote * ( ambiente_do_material * ambiente_da_luz
  //                                     + difusa_do_material * difusa_da_luz * max(N.L, 0)
  //                                     + especular_do_material * especular_da_luz * max(N.H, 0)^brilho ) )
  // A NORMAL UNITARIA. A equacao do GL usa o vector unitario (`n̂`), e a normal
  // que chega aqui e a que saiu da transposta da inversa da modelview -- com um
  // comprimento que depende da escala do modelo. Normaliza-la AQUI e a escolha
  // do zeebx (a receita verificada: `let normal = normaliza(gira_normal(...))`,
  // `src/rasterizer.rs:1042`) e a razao pela qual o `GL_NORMALIZE` e o
  // `GL_RESCALE_NORMAL` nao mudam nenhuma cor nesta implementacao -- os dois
  // pedem uma normal unitaria a chegar a equacao, e ela chega sempre. Isso fica
  // DITO, e nao maquilhado com um efeito que nao existe.
  double n_unit[3] = {static_cast<double>(normal[0]), static_cast<double>(normal[1]),
                      static_cast<double>(normal[2])};
  Normalizar3(n_unit);

  float saida[3];
  for (int c = 0; c < 3; ++c) {
    saida[c] = material.emissao[c] + ambiente[c] * e.ambiente_da_cena[c];
  }
  // A DIRECCAO PARA O OBSERVADOR: o observador esta na ORIGEM do espaco do
  // olho, logo e o proprio ponto, negado e normalizado.
  double para_o_olho[3] = {-static_cast<double>(olho[0]), -static_cast<double>(olho[1]),
                           -static_cast<double>(olho[2])};
  Normalizar3(para_o_olho);

  for (const EstadoDeRasterizacao::Luz& luz : e.luzes) {
    if (!luz.ligada) continue;
    double para_a_luz[3];
    // UMA LUZ DIRECCIONAL (w = 0) nao tem distancia: a posicao e uma direccao, e
    // nao ha atenuacao que a multiplique.
    const bool direccional = (luz.posicao[3] == 0.0f);
    for (int c = 0; c < 3; ++c) {
      para_a_luz[c] = direccional ? static_cast<double>(luz.posicao[c])
                                  : static_cast<double>(luz.posicao[c]) - static_cast<double>(olho[c]);
    }
    const double distancia = Comprimento3(para_a_luz);
    Normalizar3(para_a_luz);
    double atenuacao = 1.0;
    if (!direccional) {
      const double divisor = static_cast<double>(luz.atenuacao[0]) +
                             static_cast<double>(luz.atenuacao[1]) * distancia +
                             static_cast<double>(luz.atenuacao[2]) * distancia * distancia;
      atenuacao = (divisor <= 0.0) ? 1.0 : (1.0 / divisor);
    }
    const double peso = atenuacao * Holofote(luz, para_a_luz);
    if (peso <= 0.0) continue;

    const double n_l = std::max(0.0, Ponto3(n_unit, para_a_luz));
    double brilho = 0.0;
    if (n_l > 0.0 && material.brilho > 0.0f) {
      // O MEIO-VECTOR DE BLINN, que e o que o GL ES 1.x usa no lugar da reflexao
      // de Phong.
      double meio[3] = {para_a_luz[0] + para_o_olho[0], para_a_luz[1] + para_o_olho[1],
                        para_a_luz[2] + para_o_olho[2]};
      Normalizar3(meio);
      brilho = std::pow(std::max(0.0, Ponto3(n_unit, meio)),
                        static_cast<double>(material.brilho));
    }
    for (int c = 0; c < 3; ++c) {
      saida[c] += static_cast<float>(
          peso * (static_cast<double>(ambiente[c]) * luz.ambiente[c] +
                  static_cast<double>(difusa[c]) * luz.difusa[c] * n_l +
                  static_cast<double>(material.especular[c]) * luz.especular[c] * brilho));
    }
  }

  Rgba r;
  const auto canal = [](float v) { return static_cast<std::uint8_t>(Apertar01(v) * 255.0f + 0.5f); };
  r.r = canal(saida[0]);
  r.g = canal(saida[1]);
  r.b = canal(saida[2]);
  // O ALFA VEM DA DIFUSA DO MATERIAL, e NAO da soma dos canais: todas as luzes
  // tem alfa 1, e somar alfa de luz deixa tudo opaco -- que e o contrario do que
  // um titulo que desenhe geometria iluminada com transparencia quer.
  r.a = canal(difusa[3]);
  return r;
}

Rasterizador::Vertice Rasterizador::InterpolarVertice(const Vertice& a, const Vertice& b, double t) {
  Vertice v;
  for (int k = 0; k < 4; ++k) {
    v.clip[k] = static_cast<float>(a.clip[k] + (b.clip[k] - a.clip[k]) * t);
  }
  v.u = static_cast<float>(a.u + (b.u - a.u) * t);
  v.v = static_cast<float>(a.v + (b.v - a.v) * t);
  auto canal = [&](std::uint8_t ca, std::uint8_t cb) -> std::uint8_t {
    const double val = static_cast<double>(ca) + (static_cast<double>(cb) - static_cast<double>(ca)) * t;
    return static_cast<std::uint8_t>(std::min(255.0, std::max(0.0, val + 0.5)));
  };
  v.cor = Rgba{canal(a.cor.r, b.cor.r), canal(a.cor.g, b.cor.g),
               canal(a.cor.b, b.cor.b), canal(a.cor.a, b.cor.a)};
  return v;
}

int Rasterizador::RecortarPlanoProximo(const Vertice* tri, Vertice* saida) const {
  // Sutherland-Hodgman contra o plano proximo: clip.z + clip.w >= 0.
  int n = 0;
  for (int i = 0; i < 3; ++i) {
    const Vertice& a = tri[i];
    const Vertice& b = tri[(i + 1) % 3];
    const double da = static_cast<double>(a.clip[2]) + static_cast<double>(a.clip[3]);
    const double db = static_cast<double>(b.clip[2]) + static_cast<double>(b.clip[3]);
    const bool a_dentro = da >= 0.0;
    const bool b_dentro = db >= 0.0;
    if (a_dentro) {
      saida[n++] = a;
    }
    if (a_dentro != b_dentro) {
      const double t = da / (da - db);
      saida[n++] = InterpolarVertice(a, b, t);
    }
  }
  return n;
}

bool Rasterizador::Projetar(const EstadoDeRasterizacao& e, Vertice* v) const {
  const float w = v->clip[3];
  if (!(w > 0.0f)) return false;  // atras da camara: sem recorte de frustum (ver o topo)
  const float x_ndc = v->clip[0] / w;
  const float y_ndc = v->clip[1] / w;
  const float z_ndc = v->clip[2] / w;
  const double vx = static_cast<double>(e.viewport[0]);
  const double vy = static_cast<double>(e.viewport[1]);
  const double vw = static_cast<double>(e.viewport[2]);
  const double vh = static_cast<double>(e.viewport[3]);
  // O Y E INVERTIDO: o GL conta a janela a partir do canto INFERIOR esquerdo e a
  // `Tela` cresce para baixo. Ver a declaracao no cabecalho.
  v->x = static_cast<float>(vx + (x_ndc + 1.0) * vw * 0.5);
  v->y = static_cast<float>(vy + (1.0 - y_ndc) * vh * 0.5);
  v->z = static_cast<float>(std::min(1.0, std::max(0.0, (z_ndc + 1.0) * 0.5)));
  return true;
}

Rgba Rasterizador::AmostrarTextura(const EstadoDeRasterizacao& e, float u, float v) const {
  const Textura& t = e.textura;
  if (!t.existe || t.largura == 0 || t.altura == 0) return e.cor;
  // CLAMP, e nao wrap: a coordenada fora de [0,1] e presa na borda (o ponto 5 do
  // cabecalho). O `floor` e o canto inferior esquerdo da celula de texel.
  int tx = static_cast<int>(std::floor(u * static_cast<float>(t.largura)));
  int ty = static_cast<int>(std::floor(v * static_cast<float>(t.altura)));
  tx = std::min(static_cast<int>(t.largura) - 1, std::max(0, tx));
  ty = std::min(static_cast<int>(t.altura) - 1, std::max(0, ty));
  const Endereco base = t.ponteiro + static_cast<Endereco>((ty * static_cast<int>(t.largura) + tx));
  if (t.formato == GL_RGBA && t.tipo == GL_UNSIGNED_BYTE) {
    const Endereco p = t.ponteiro + static_cast<Endereco>(
                                         (ty * static_cast<int>(t.largura) + tx) * 4);
    return Rgba{mem_.Ler8(p), mem_.Ler8(p + 1), mem_.Ler8(p + 2), mem_.Ler8(p + 3)};
  }
  if (t.formato == GL_RGB && t.tipo == GL_UNSIGNED_BYTE) {
    const Endereco p = t.ponteiro + static_cast<Endereco>(
                                         (ty * static_cast<int>(t.largura) + tx) * 3);
    return Rgba{mem_.Ler8(p), mem_.Ler8(p + 1), mem_.Ler8(p + 2), 255};
  }
  if (t.formato == GL_LUMINANCE && t.tipo == GL_UNSIGNED_BYTE) {
    const std::uint8_t l = mem_.Ler8(base);
    return Rgba{l, l, l, 255};
  }
  return e.cor;
}

void Rasterizador::EscreverPixel(const EstadoDeRasterizacao& e, int x, int y, float profundidade,
                                 Rgba cor) {
  // 1. O ALPHA TEST. Vem ANTES de tudo (como no zeebx): o fragmento que ele
  //    descarta nao escreve cor, nao escreve profundidade e nao chega a
  //    mistura. O alfa do fragmento entra em [0,1] porque a referencia do
  //    `glAlphaFuncx` e um GLfixed nessa faixa -- comparar um byte 0..255 com
  //    uma referencia 0..1 seria comparar duas escalas diferentes.
  if (e.teste_de_alfa) {
    const float alfa = static_cast<float>(cor.a) / 255.0f;
    if (!Comparar(e.funcao_de_alfa, alfa, e.alfa_de_referencia)) {
      ++descartados_alfa_;
      return;
    }
  }
  const int l = superficie_.Largura();
  const int a = superficie_.Altura();
  if (x < 0 || y < 0 || x >= l || y >= a) return;
  const std::size_t indice = static_cast<std::size_t>(y) * static_cast<std::size_t>(l) +
                             static_cast<std::size_t>(x);
  if (e.teste_de_profundidade && profundidade_.size() > indice) {
    const float guardada = profundidade_[indice];
    bool passa = false;
    switch (e.funcao_de_profundidade) {
      case GL_NEVER: passa = false; break;
      case GL_LESS: passa = profundidade < guardada; break;
      case GL_EQUAL: passa = profundidade == guardada; break;
      case GL_LEQUAL: passa = profundidade <= guardada; break;
      case GL_GREATER: passa = profundidade > guardada; break;
      case GL_NOTEQUAL: passa = profundidade != guardada; break;
      case GL_GEQUAL: passa = profundidade >= guardada; break;
      case GL_ALWAYS: passa = true; break;
      default: passa = true; break;
    }
    if (!passa) return;
    // A PROFUNDIDADE SO SE ESCREVE COM O TESTE LIGADO, que e o que o GL faz: com
    // o `GL_DEPTH_TEST` desligado um `glDepthMask(GL_TRUE)` nao escreve nada.
    if (e.escrever_profundidade) profundidade_[indice] = profundidade;
  }

  // 2. A MISTURA E A MASCARA DE COR, as duas contra o pixel que JA esta la.
  //    A leitura do destino so acontece quando uma das duas a pede: um titulo
  //    sem `GL_BLEND` e com a mascara por omissao escreve sem ler nada, e paga
  //    o mesmo que pagava antes desta capacidade existir.
  std::uint32_t escrito = Para565(cor);
  const bool mascara_limpa = (e.mascara_de_cor == 0x0000000Fu);
  if (e.mistura_ligada || !mascara_limpa) {
    const std::uint32_t destino565 = superficie_.Ler(x, y);
    if (e.mistura_ligada) {
      float origem[4] = {static_cast<float>(cor.r) / 255.0f, static_cast<float>(cor.g) / 255.0f,
                         static_cast<float>(cor.b) / 255.0f, static_cast<float>(cor.a) / 255.0f};
      float destino[4];
      CorDoDestino(destino565, destino);
      float misturado[4];
      for (int c = 0; c < 4; ++c) {
        const float f_origem = FatorDeMistura(e.mistura_fonte, origem, destino, c);
        const float f_destino = FatorDeMistura(e.mistura_destino, origem, destino, c);
        misturado[c] = Apertar01(origem[c] * f_origem + destino[c] * f_destino);
      }
      // O CANAL ALFA DO RESULTADO NAO TEM ONDE SER GUARDADO (a tela e RGB565):
      // ele fica calculado e e descartado na escrita, como o alfa do fragmento.
      // Todos os quatro canais entram na conta porque um factor `GL_DST_ALPHA`
      // le o alfa do DESTINO, e esse existe (vale 1).
      escrito = Para565(DeBits(misturado));
    }
    if (!mascara_limpa) {
      // A MASCARA DE COR. Os canais proibidos ficam com o que o destino ja
      // tinha -- o que uma passada anterior deixou ali. A mascara e aplicada em
      // BITS do RGB565, e nao num vaivem de 8 bits: um canal que fica tem de
      // ficar EXACTAMENTE como estava, e nao arredondado por uma ida e volta.
      std::uint32_t mascara = 0;
      if ((e.mascara_de_cor & 0x1u) != 0) mascara |= 0xF800u;  // vermelho
      if ((e.mascara_de_cor & 0x2u) != 0) mascara |= 0x07E0u;  // verde
      if ((e.mascara_de_cor & 0x4u) != 0) mascara |= 0x001Fu;  // azul
      escrito = (escrito & mascara) | (destino565 & ~mascara);
    }
  }
  superficie_.Escrever(x, y, escrito);
  ++pixels_;
}

void Rasterizador::RasterizarTriangulo(const EstadoDeRasterizacao& e, const Vertice& va,
                                       const Vertice& vb, const Vertice& vc) {
  // --- o descarte de faces, ANTES da inversao do y -------------------------
  //
  // A orientacao e decidida em NDC (y para CIMA), e nao na janela: a janela
  // deste rasterizador tem o y para baixo, e faze-la na janela inverteria
  // `GL_CCW` e `GL_CW` -- todos os titulos que descartam a face de tras
  // desenhariam a metade errada.
  const double area_ndc = static_cast<double>(vb.clip[0] / vb.clip[3] - va.clip[0] / va.clip[3]) *
                              static_cast<double>(vc.clip[1] / vc.clip[3] - va.clip[1] / va.clip[3]) -
                          static_cast<double>(vb.clip[1] / vb.clip[3] - va.clip[1] / va.clip[3]) *
                              static_cast<double>(vc.clip[0] / vc.clip[3] - va.clip[0] / va.clip[3]);
  const bool anti_horario = area_ndc > 0.0;
  const bool frente = (e.orientacao_da_frente == GL_CCW) ? anti_horario : !anti_horario;
  if (e.descartar_faces && (e.descartar_face == GL_FRONT_AND_BACK ||
                            (e.descartar_face == GL_FRONT && frente) ||
                            (e.descartar_face == GL_BACK && !frente))) {
    ++descartados_;
    return;
  }

  Vertice a = va, b = vb, c = vc;
  double area = Aresta(a.x, a.y, b.x, b.y, c.x, c.y);
  if (area == 0.0) {
    ++descartados_;  // triangulo degenerado: nenhum pixel lhe pertence
    return;
  }
  if (area < 0.0) {
    std::swap(b, c);
    area = -area;
  }

  const double minx = std::min(a.x, std::min(b.x, c.x));
  const double maxx = std::max(a.x, std::max(b.x, c.x));
  const double miny = std::min(a.y, std::min(b.y, c.y));
  const double maxy = std::max(a.y, std::max(b.y, c.y));

  // A CAIXA DELIMITADORA E CORTADA PELO VIEWPORT, e so depois se percorre: um
  // triangulo enorme nao pode custar `largura * altura` pixels de trabalho (o
  // limite verificado so no destino ja custou mais de 900 s para UM titulo, e
  // esta escrito no `tela.h`).
  const int x0 = std::max(static_cast<int>(e.viewport[0]), static_cast<int>(std::floor(minx)));
  const int y0 = std::max(static_cast<int>(e.viewport[1]), static_cast<int>(std::floor(miny)));
  const int vp_fim_x = std::min(superficie_.Largura(),
                                static_cast<int>(e.viewport[0]) + static_cast<int>(e.viewport[2]));
  const int vp_fim_y = std::min(superficie_.Altura(),
                                static_cast<int>(e.viewport[1]) + static_cast<int>(e.viewport[3]));
  const int x1 = std::min(vp_fim_x, static_cast<int>(std::ceil(maxx)));
  const int y1 = std::min(vp_fim_y, static_cast<int>(std::ceil(maxy)));
  if (x1 <= x0 || y1 <= y0) {
    ++descartados_;  // fora do viewport
    return;
  }

  // A regra do canto, por aresta. (a->b, b->c, c->a) na ordem de area positiva.
  const bool canto_ab = ArestaDeCanto(b.x - a.x, b.y - a.y);
  const bool canto_bc = ArestaDeCanto(c.x - b.x, c.y - b.y);
  const bool canto_ca = ArestaDeCanto(a.x - c.x, a.y - c.y);

  ++triangulos_;
  for (int y = y0; y < y1; ++y) {
    const double py = static_cast<double>(y) + 0.5;
    for (int x = x0; x < x1; ++x) {
      const double px = static_cast<double>(x) + 0.5;
      const double e_ab = Aresta(a.x, a.y, b.x, b.y, px, py);
      const double e_bc = Aresta(b.x, b.y, c.x, c.y, px, py);
      const double e_ca = Aresta(c.x, c.y, a.x, a.y, px, py);
      const bool dentro = (e_ab > 0.0 || (e_ab == 0.0 && canto_ab)) &&
                          (e_bc > 0.0 || (e_bc == 0.0 && canto_bc)) &&
                          (e_ca > 0.0 || (e_ca == 0.0 && canto_ca));
      if (!dentro) continue;
      // BARYCENTRICOS: cada peso e a funcao da aresta OPOSTA. `area` e a soma
      // das tres (e nao um quarto calculo).
      const double wc = e_ab / area;
      const double wa = e_bc / area;
      const double wb = e_ca / area;

      // CORRECAO DE PERSPECTIVA: os atributos (cor e textura) sao divididos por w
      // de cada vertice e reescalados pela soma dos pesos ponderados por 1/w.
      // O `z` de janela continua afim (linear em tela), como exige a especificacao OpenGL.
      const double inv_wa = 1.0 / static_cast<double>(a.clip[3]);
      const double inv_wb = 1.0 / static_cast<double>(b.clip[3]);
      const double inv_wc = 1.0 / static_cast<double>(c.clip[3]);
      const double den_persp = wa * inv_wa + wb * inv_wb + wc * inv_wc;
      const double inv_den = (den_persp > 0.0) ? (1.0 / den_persp) : 1.0;
      const double pwa = wa * inv_wa * inv_den;
      const double pwb = wb * inv_wb * inv_den;
      const double pwc = wc * inv_wc * inv_den;

      Rgba cor;
      if (e.sombreado_plano) {
        cor = c.cor;  // ponto 7 do cabecalho: a cor do ultimo vertice
      } else {
        cor.r = static_cast<std::uint8_t>(std::min(255.0, pwa * a.cor.r + pwb * b.cor.r + pwc * c.cor.r + 0.5));
        cor.g = static_cast<std::uint8_t>(std::min(255.0, pwa * a.cor.g + pwb * b.cor.g + pwc * c.cor.g + 0.5));
        cor.b = static_cast<std::uint8_t>(std::min(255.0, pwa * a.cor.b + pwb * b.cor.b + pwc * c.cor.b + 0.5));
        // O ALFA E INTERPOLADO, e nao fixo a 255: a tela nao tem buffer de
        // alfa (ponto 8 do cabecalho) e o pixel escrito continua opaco, mas a
        // MISTURA e o ALPHA TEST leem ESTE alfa. Com o 255 de antes, um
        // `GL_SRC_ALPHA` valia sempre 1 e o `GL_ALPHA_TEST` nunca descartava
        // nada -- duas capacidades ligadas e inertes.
        cor.a = static_cast<std::uint8_t>(std::min(255.0, pwa * a.cor.a + pwb * b.cor.a + pwc * c.cor.a + 0.5));
      }
      if (e.textura_ligada) {
        const float u = static_cast<float>(pwa * a.u + pwb * b.u + pwc * c.u);
        const float v = static_cast<float>(pwa * a.v + pwb * b.v + pwc * c.v);
        cor = AmostrarTextura(e, u, v);
      }
      const float z = static_cast<float>(wa * a.z + wb * b.z + wc * c.z);
      EscreverPixel(e, x, y, z, cor);
    }
  }
}

void Rasterizador::RasterizarSegmento(const EstadoDeRasterizacao& e, const Vertice& va,
                                      const Vertice& vb, bool incluir_fim) {
  // --- 1. O RECORTE DO PLANO PROXIMO, e a derivacao da formula --------------
  //
  // A CONDICAO DE DENTRO DO PLANO PROXIMO E `z + w >= 0` (as coordenadas sao as
  // de clip, depois de P * M * v). Para um SEGMENTO nao e preciso
  // Sutherland-Hodgman: a grandeza `d = z + w` e AFIM no parametro do segmento,
  // `P(t) = A + t*(B - A)`, logo
  //
  //     d(t) = d0 + t*(d1 - d0)          com d0 = z0 + w0 e d1 = z1 + w1
  //
  // e o ponto onde ela passa por zero e `d(t) = 0` -> `t = d0 / (d0 - d1)`, que e
  // a mesma conta escrita como `t = (-d0) / (d1 - d0)`.
  //
  // O CASO DEGENERADO (denominador 0) NAO PODE ACONTECER AQUI: o corte so e
  // feito quando os dois extremos caem de LADOS DIFERENTES do plano
  // (`a_dentro != b_dentro`), e nesse caso `d0` e `d1` tem sinais opostos --
  // `d0 - d1` nunca e zero. Se os dois estao dentro, nada se corta; se os dois
  // estao fora, o segmento e DESCARTADO (e nao cortado com um `t` inventado).
  Vertice a = va, b = vb;
  const double d0 = static_cast<double>(a.clip[2]) + static_cast<double>(a.clip[3]);
  const double d1 = static_cast<double>(b.clip[2]) + static_cast<double>(b.clip[3]);
  const bool a_dentro = d0 >= 0.0;
  const bool b_dentro = d1 >= 0.0;
  if (!a_dentro && !b_dentro) {
    ++segmentos_descartados_;  // todo atras do plano proximo
    return;
  }
  if (a_dentro != b_dentro) {
    const double t = d0 / (d0 - d1);
    const Vertice corte = InterpolarVertice(a, b, t);
    if (a_dentro) {
      b = corte;
    } else {
      a = corte;
    }
    ++segmentos_recortados_;
  }

  // --- 2. A PROJECCAO DOS DOIS EXTREMOS ------------------------------------
  if (!Projetar(e, &a) || !Projetar(e, &b)) {
    // Um ponto com `w <= 0` projetado da lixo (o `Projetar` recusa-o), e um
    // segmento com um extremo desses nao tem desenho possivel. E contado.
    ++segmentos_descartados_;
    return;
  }

  // --- 3. O LIMITE DA JANELA, antes de percorrer ---------------------------
  //
  // E o MESMO limite dos triangulos (`AJanelaLimitaORasto`): o `glViewport` nao
  // e so o mapa de NDC para a janela, e tambem o limite do que se desenha. O
  // teste e de CAIXA (nao ha aqui varrimento): um segmento cuja caixa esta toda
  // fora nao escreve nada.
  const int vp_x0 = static_cast<int>(e.viewport[0]);
  const int vp_y0 = static_cast<int>(e.viewport[1]);
  const int vp_fim_x = std::min(superficie_.Largura(), vp_x0 + static_cast<int>(e.viewport[2]));
  const int vp_fim_y = std::min(superficie_.Altura(), vp_y0 + static_cast<int>(e.viewport[3]));
  const double minx = std::min(a.x, b.x), maxx = std::max(a.x, b.x);
  const double miny = std::min(a.y, b.y), maxy = std::max(a.y, b.y);
  if (maxx < static_cast<double>(vp_x0) || minx >= static_cast<double>(vp_fim_x) ||
      maxy < static_cast<double>(vp_y0) || miny >= static_cast<double>(vp_fim_y)) {
    ++segmentos_descartados_;  // fora do viewport
    return;
  }

  // --- 4. O DDA NO EIXO MAIOR ----------------------------------------------
  //
  // OS PIXEIS: o pixel de um extremo e o que CONTEM o extremo (`floor`), que e o
  // que um DDA de extremos inteiros faz (o `floor` do x de janela, e nao um
  // arredondamento: o centro do pixel `k` esta em `k + 0.5`, e o extremo em
  // `k + 0.1` pertence-lhe).
  //
  // A PONTA FINAL ABERTA: o pixel do PONTO FINAL nao e escrito em nenhum
  // segmento -- excepto quando `incluir_fim` o pede (a ultima ponta de uma
  // `GL_LINE_STRIP`). E isso que faz um vertice partilhado por dois segmentos
  // escrever UM pixel, e nao dois (a mesma ideia da regra do canto dos
  // triangulos: `ArestaPartilhadaNaoEscreveDuasVezesNemDeixaFenda`).
  //
  // O DESVIO DECLARADO: a regra do OpenGL para linhas e a `diamond-exit`, e um
  // extremo que caia DENTRO do losango do pixel final escreve esse pixel. Aqui o
  // pixel final de cada segmento nao e escrito (com a excepcao da ultima ponta de
  // uma faixa): um segmento perde, no maximo, o ULTIMO pixel da sua ponta. Fica
  // dito, e o teste `DiagonalDeQuarentaECincoGraus` fixa-o.
  int x = static_cast<int>(std::floor(a.x));
  int y = static_cast<int>(std::floor(a.y));
  const int x_fim = static_cast<int>(std::floor(b.x));
  const int y_fim = static_cast<int>(std::floor(b.y));
  const int dx = x_fim - x, dy = y_fim - y;
  const int adx = std::abs(dx), ady = std::abs(dy);
  const int sx = (dx < 0) ? -1 : 1;
  const int sy = (dy < 0) ? -1 : 1;
  const bool maior_em_x = adx >= ady;
  const int passos = maior_em_x ? adx : ady;
  ++segmentos_;
  if (passos == 0) {
    // COMPRIMENTO ZERO (os dois extremos no mesmo pixel): escreve UM pixel, o do
    // vertice. Sem este caso, um segmento degenerado nao escreveria nada -- e um
    // ponto na tela e um desenho, nao um vazio.
    EscreverPixel(e, x, y, a.z, e.sombreado_plano ? b.cor : a.cor);
    return;
  }
  const auto dentro_da_janela = [&](int px, int py) {
    return px >= vp_x0 && py >= vp_y0 && px < vp_fim_x && py < vp_fim_y;
  };
  int erro = maior_em_x ? (2 * ady - adx) : (2 * adx - ady);
  for (int k = 0; k <= passos; ++k) {
    if (k == passos && !incluir_fim) break;
    const double t = static_cast<double>(k) / static_cast<double>(passos);
    // O `z` E A COR SAO LINEARES em `t` -- o parametro do eixo maior, que e o
    // mesmo com que o DDA anda. O `sombreado_plano` do estado continua a valer:
    // com o `GL_FLAT` a cor e a do ULTIMO vertice, como nos triangulos.
    const float z = static_cast<float>(static_cast<double>(a.z) +
                                       (static_cast<double>(b.z) - static_cast<double>(a.z)) * t);
    Rgba cor;
    if (e.sombreado_plano) {
      cor = b.cor;
    } else {
      const auto canal = [&](std::uint8_t ca, std::uint8_t cb) {
        const double val = static_cast<double>(ca) +
                           (static_cast<double>(cb) - static_cast<double>(ca)) * t;
        return static_cast<std::uint8_t>(std::min(255.0, std::max(0.0, val + 0.5)));
      };
      cor = Rgba{canal(a.cor.r, b.cor.r), canal(a.cor.g, b.cor.g), canal(a.cor.b, b.cor.b),
                 canal(a.cor.a, b.cor.a)};
    }
    if (e.textura_ligada) {
      // A TEXTURA E AMOSTRADA COM A COORDENADA INTERPOLADA AFIM em `t`, e nao
      // corrigida por perspectiva como nos triangulos: num segmento o `w` varia
      // ao longo dele, e a correccao exigiria a mesma divisao por `w` que o
      // `RasterizarTriangulo` faz. Fica dito -- nenhum dos dois titulos desta
      // demanda (heavyweaponbrew, pbc) desenha linhas com textura ligada, e a
      // escolha afim e a que nao inventa uma correccao por medir.
      const float u = static_cast<float>(static_cast<double>(a.u) +
                                        (static_cast<double>(b.u) - static_cast<double>(a.u)) * t);
      const float v = static_cast<float>(static_cast<double>(a.v) +
                                        (static_cast<double>(b.v) - static_cast<double>(a.v)) * t);
      cor = AmostrarTextura(e, u, v);
    }
    // O PIXEL PASSA PELO `EscreverPixel`, QUE JA FAZ o alpha test, o teste de
    // profundidade, a mistura e a mascara de cor contra o pixel que la esta. Nada
    // desta logica e duplicada aqui, e o `z` que lhe chega e o do ponto do
    // segmento (um `z` errado escreveria a profundidade errada no buffer).
    if (dentro_da_janela(x, y)) EscreverPixel(e, x, y, z, cor);
    if (maior_em_x) {
      x += sx;
      erro += 2 * ady;
      if (erro > 0) {
        y += sy;
        erro -= 2 * adx;
      }
    } else {
      y += sy;
      erro += 2 * adx;
      if (erro > 0) {
        x += sx;
        erro -= 2 * ady;
      }
    }
  }
}

bool Rasterizador::Desenhar(const EstadoDeRasterizacao& estado, const PedidoDeDesenho& pedido,
                            std::string* motivo) {
  if (superficie_.Largura() <= 0 || superficie_.Altura() <= 0) {
    ++recusadas_;
    *motivo = "sem superficie onde escrever";
    return false;
  }
  const std::uint32_t modo = pedido.primitiva;
  const bool triangulos = (modo == GL_TRIANGLES || modo == GL_TRIANGLE_STRIP ||
                           modo == GL_TRIANGLE_FAN);
  const bool linhas = (modo == GL_LINES || modo == GL_LINE_LOOP || modo == GL_LINE_STRIP);
  if (!triangulos && !linhas) {
    ++recusadas_;
    // P2: o caminho que nao existe RECUSA e REGISTA, com o NOME da primitiva. Um
    // "devolve sucesso e nao desenha" e o defeito do `glCullFace`, que descartou
    // 86 377 chamadas sem deixar rasto.
    //
    // O TEXTO LISTA AS PRIMITIVAS QUE EXISTEM, e nao as que existiam antes das
    // linhas: uma recusa a dizer "so TRIANGLES, TRIANGLE_STRIP e TRIANGLE_FAN"
    // com as linhas ja desenhadas seria uma linha de log que nao pode ser
    // verdadeira (P7). O `GL_POINTS` continua aqui, com o nome dele -- nenhum
    // titulo do corpus o pede, e nao se inventa um rasterizador de pontos sem
    // medicao que o peca.
    *motivo = std::string("primitiva ") + NomeDaPrimitiva(modo) +
              " sem rasterizador nesta etapa (so TRIANGLES, TRIANGLE_STRIP, TRIANGLE_FAN, "
              "LINES, LINE_LOOP e LINE_STRIP)";
    return false;
  }
  if (linhas && estado.largura_de_linha > 1.0f) {
    // A MENTIRA SILENCIOSA QUE ESTA GUARDA EVITA: uma linha de 2 px desenhada
    // com 1 px e um resultado errado que nenhuma contagem apanha -- sai um
    // desenho, so nao e o que o titulo pediu. O `kIgl_LineWidthx` ja guarda o
    // valor e di-lo; aqui o desenho RECUSA com o numero que o guest pediu.
    ++recusadas_;
    char d[160];
    std::snprintf(d, sizeof(d),
                  "largura de linha %g sem rasterizador de linha grossa nesta etapa",
                  static_cast<double>(estado.largura_de_linha));
    *motivo = d;
    return false;
  }
  if (pedido.quantos == 0) {
    ++recusadas_;
    *motivo = "desenho com zero vertices";
    return false;
  }
  if (estado.textura_ligada && estado.textura.comprimida) {
    ++recusadas_;
    *motivo = "textura comprimida: nao ha descodificador (ATITC/ETC) nesta arvore";
    return false;
  }
  if (estado.textura_ligada && estado.textura.existe) {
    const bool formato_conhecido =
        (estado.textura.formato == GL_RGBA || estado.textura.formato == GL_RGB ||
         estado.textura.formato == GL_LUMINANCE) &&
        estado.textura.tipo == GL_UNSIGNED_BYTE;
    if (!formato_conhecido) {
      ++recusadas_;
      char d[128];
      std::snprintf(d, sizeof(d), "textura com formato 0x%04x e tipo 0x%04x sem caminho de amostragem",
                    estado.textura.formato, estado.textura.tipo);
      *motivo = d;
      return false;
    }
  }
  // O BUFFER DE PROFUNDIDADE SO NASCE SE O ESTADO O PEDIR: 640x480 floats sao
  // 1,2 MB, e um titulo 2D que nunca ligue o `GL_DEPTH_TEST` nao os paga.
  if (estado.teste_de_profundidade) PrepararProfundidade();

  const std::uint64_t pixels_antes = pixels_;
  const std::uint64_t triangulos_antes = triangulos_;
  const std::uint64_t segmentos_antes = segmentos_;
  const std::uint64_t descartados_antes = descartados_;
  const std::uint64_t seg_descartados_antes = segmentos_descartados_;
  std::string recusa_do_vertice;
  std::vector<Vertice> vertices;
  vertices.reserve(pedido.quantos);
  for (std::uint32_t k = 0; k < pedido.quantos; ++k) {
    std::uint32_t indice = pedido.primeiro + k;
    if (pedido.por_indices) {
      const Endereco onde = pedido.endereco_dos_indices +
                            static_cast<Endereco>(k * ((pedido.tipo_do_indice == GL_UNSIGNED_BYTE) ? 1 : 2));
      indice = (pedido.tipo_do_indice == GL_UNSIGNED_BYTE) ? mem_.Ler8(onde) : mem_.Ler16(onde);
    }
    Vertice v;
    if (!LerVertice(estado, indice, &v, &recusa_do_vertice)) {
      ++recusadas_;
      *motivo = recusa_do_vertice;
      return false;
    }
    vertices.push_back(v);
  }
  const std::size_t n = vertices.size();
  const auto triangulo = [&](std::size_t i, std::size_t j, std::size_t k) {
    const Vertice trio[3] = {vertices[i], vertices[j], vertices[k]};
    Vertice poli[4];
    const int n_rec = RecortarPlanoProximo(trio, poli);
    if (n_rec < 3) {
      ++descartados_;
      return;
    }
    if (n_rec > 3) ++recortados_;
    for (int m = 0; m < n_rec; ++m) {
      if (!Projetar(estado, &poli[m])) {
        ++descartados_;
        return;
      }
    }
    RasterizarTriangulo(estado, poli[0], poli[1], poli[2]);
    if (n_rec == 4) {
      RasterizarTriangulo(estado, poli[0], poli[2], poli[3]);
    }
  };
  // O CAMINHO DAS LINHAS: os vertices sao os MESMOS (a leitura ja e generica --
  // expande indices e chama `LerVertice`), e o que muda e o que se faz com eles.
  //
  // A ULTIMA PONTA DE UMA FAIXA E FECHADA (`incluir_fim`), e as outras nao: numa
  // `GL_LINE_STRIP` o vertice final nao e ponta de nenhum outro segmento, e sem
  // esta excepcao o ultimo pixel da faixa nunca seria escrito. Na `GL_LINE_LOOP`
  // todos os segmentos ficam ABERTOS, porque o ultimo volta ao PRIMEIRO vertice e
  // esse pixel ja foi escrito pelo primeiro segmento.
  const auto segmento = [&](std::size_t i, std::size_t j, bool incluir_fim) {
    RasterizarSegmento(estado, vertices[i], vertices[j], incluir_fim);
  };
  if (modo == GL_LINES) {
    // PARES INDEPENDENTES: cada par e um segmento, e a ponta de cada um fica
    // aberta (a regra declarada -- uma linha de 6 px escreve 6 pixels).
    for (std::size_t i = 0; i + 1 < n; i += 2) segmento(i, i + 1, false);
  } else if (modo == GL_LINE_LOOP) {
    if (n >= 2) {
      for (std::size_t i = 0; i + 1 < n; ++i) segmento(i, i + 1, false);
      segmento(n - 1, 0, false);
    }
  } else if (modo == GL_LINE_STRIP) {
    for (std::size_t i = 0; i + 1 < n; ++i) segmento(i, i + 1, i + 2 == n);
  } else if (modo == GL_TRIANGLES) {
    for (std::size_t i = 0; i + 2 < n; i += 3) triangulo(i, i + 1, i + 2);
  } else if (modo == GL_TRIANGLE_STRIP) {
    // O GL inverte a ordem dos triangulos IMPARES para o descarte de faces: sem
    // isso, metade dos triangulos de uma faixa teria a frente trocada.
    for (std::size_t i = 0; i + 2 < n; ++i) {
      if (i % 2 == 0) triangulo(i, i + 1, i + 2);
      else triangulo(i + 1, i, i + 2);
    }
  } else {
    for (std::size_t i = 1; i + 1 < n; ++i) triangulo(0, i, i + 1);
  }

  const std::uint64_t escritos = pixels_ - pixels_antes;
  char detalhe[192];
  // O TRACO DO FIM DIZ QUAL DOS DOIS CONTADORES CONTA. Ate esta frente dizia
  // sempre "em K triangulos", e essa linha, com uma faixa de linhas desenhada,
  // seria falsa (P7): o teste `FaixaDeTresPontosColineares...` le-a e exige o
  // "segmentos" para as primitivas de linha.
  if (linhas) {
    std::snprintf(detalhe, sizeof(detalhe),
                  "%u vertices -> %" PRIu64 " pixels em %" PRIu64 " segmentos (%" PRIu64
                  " recortados, %" PRIu64 " descartados)",
                  pedido.quantos, escritos, segmentos_ - segmentos_antes,
                  segmentos_recortados_, segmentos_descartados_ - seg_descartados_antes);
  } else {
    std::snprintf(detalhe, sizeof(detalhe),
                  "%u vertices -> %" PRIu64 " pixels em %" PRIu64 " triangulos (%" PRIu64
                  " recortados, %" PRIu64 " descartados)",
                  pedido.quantos, escritos, triangulos_ - triangulos_antes,
                  recortados_, descartados_ - descartados_antes);
  }
  *motivo = detalhe;
  return true;
}

}  // namespace zb2::video
