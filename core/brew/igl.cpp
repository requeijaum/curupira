#include "core/brew/igl.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/brew/interface.h"

namespace zb2::brew {

namespace {

using namespace gl_slots;

// --- a matematica das matrizes ----------------------------------------------
//
// COLUMN-MAJOR, que e como o GL guarda as matrizes: o elemento (linha r, coluna
// c) esta em `m[c*4 + r]`. Isto nao e um detalhe: se fosse row-major, o produto
// `M * T` do `glTranslatex` punha a translacao na diagonal errada e o teste com
// numeros escritos a mao (1,2,3 em trans[12],trans[13],trans[14]) nao passaria.
//
// As matrizes sao `float` e nao `GLfixed` porque o produto de duas 16.16 estoura
// os 32 bits. A entrada e saida continua a ser 16.16 -- a conversao esta no
// `Fixo` e no `glLoadMatrixx`.

void Identidade(float* m) {
  for (int i = 0; i < 16; ++i) m[i] = 0.0f;
  m[0] = m[5] = m[10] = m[15] = 1.0f;
}

// saida = a * b. A `saida` pode ser a propria `a` (o caso do `glTranslatex` que
// pos-multiplica a matriz corrente), por isso o produto vai para um temporario.
void Multiplicar(float* saida, const float* a, const float* b) {
  float t[16];
  for (int c = 0; c < 4; ++c) {
    for (int r = 0; r < 4; ++r) {
      float s = 0.0f;
      for (int k = 0; k < 4; ++k) s += a[k * 4 + r] * b[c * 4 + k];
      t[c * 4 + r] = s;
    }
  }
  for (int i = 0; i < 16; ++i) saida[i] = t[i];
}

// As tres matrizes de transformacao do gl*. Cada uma e o `M := M * T` do GL.
void Translacao(float* m, float x, float y, float z) {
  float t[16];
  Identidade(t);
  t[12] = x;
  t[13] = y;
  t[14] = z;
  Multiplicar(m, m, t);
}

void Escala(float* m, float x, float y, float z) {
  float t[16];
  Identidade(t);
  t[0] = x;
  t[5] = y;
  t[10] = z;
  Multiplicar(m, m, t);
}

// Rotacao de `graus` em torno do eixo (x,y,z). A matriz e a do GL: R[r][c] como
// em qualquer tabela de rotacao de Rodrigues, e depois guardada em column-major.
void Rotacao(float* m, float graus, float x, float y, float z) {
  const double rad = static_cast<double>(graus) * 3.14159265358979323846 / 180.0;
  double n = std::sqrt(static_cast<double>(x) * x + static_cast<double>(y) * y +
                       static_cast<double>(z) * z);
  if (n == 0.0) return;
  const double nx = x / n, ny = y / n, nz = z / n;
  const double c = std::cos(rad), s = std::sin(rad);
  const double o = 1.0 - c;
  const double r[3][3] = {
      {nx * nx * o + c, nx * ny * o - nz * s, nx * nz * o + ny * s},
      {ny * nx * o + nz * s, ny * ny * o + c, ny * nz * o - nx * s},
      {nz * nx * o - ny * s, nz * ny * o + nx * s, nz * nz * o + c}};
  float t[16];
  Identidade(t);
  for (int linha = 0; linha < 3; ++linha) {
    for (int coluna = 0; coluna < 3; ++coluna) {
      t[coluna * 4 + linha] = static_cast<float>(r[linha][coluna]);
    }
  }
  Multiplicar(m, m, t);
}

float Apertar(float v, float minimo, float maximo) {
  return v < minimo ? minimo : (v > maximo ? maximo : v);
}

// A TABELA DAS CAPACIDADES DO GL, COMPLETA -- e a razao pela qual ela passa a
// ser completa.
//
// `tools/gl_slots.inc` e GERADO de `AEEGL.h` e do cabecalho `gles/gl.h`, e e
// conferido por `tools/verificar_slots_gl.sh`: uma linha escrita a mao la faz a
// guarda DIVERGIR do cabecalho. O cabecalho que esse gerador le e o
// `gles_1_0/gl.h` -- o perfil Common-Lite --, e por isso NEM TODA a `EnableCap`
// do GL ES 1.1 esta no `.inc`. As que faltavam ficam aqui, com a linha exacta
// do cabecalho do SDK:
//
//   BREW-4.0.2-SP19/sdk/inc/gles/gles_1_0/gl.h:180   GL_RESCALE_NORMAL 0x803A
//   BREW-4.0.2-SP19/sdk/inc/gles/gles_1_0/gl.h:178   GL_COLOR_MATERIAL 0x0B57
//   BREW-4.0.2-SP19/sdk/inc/gles/gles_1_1/gl.h:195   GL_COLOR_LOGIC_OP 0x0BF2
//   BREW-4.0.2-SP19/sdk/inc/gles/gles_1_1/gl.h:197   GL_STENCIL_TEST 0x0B90
//   BREW-4.0.2-SP19/sdk/inc/gles/gles_1_1/gl.h:207   GL_POINT_SMOOTH 0x0B10
//   BREW-4.0.2-SP19/sdk/inc/gles/gles_1_1/gl.h:208   GL_LINE_SMOOTH 0x0B20
//   BREW-4.0.2-SP19/sdk/inc/gles/gles_1_1/gl.h:218   GL_MULTISAMPLE 0x809D
//   BREW-4.0.2-SP19/sdk/inc/gles/gles_1_1/gl.h:219   GL_SAMPLE_ALPHA_TO_COVERAGE 0x809E
//   BREW-4.0.2-SP19/sdk/inc/gles/gles_1_1/gl.h:220   GL_SAMPLE_ALPHA_TO_ONE 0x809F
//   BREW-4.0.2-SP19/sdk/inc/gles/gles_1_1/gl.h:221   GL_SAMPLE_COVERAGE 0x80A0
//
// O QUE ISTO CONSERTA, medido. Antes desta frente havia exactamente DUAS
// capacidades recusadas por nao terem nome no corpus de 62 (`zbEnable 0x803A`
// em gof, rmp e pbc, e `glDisable 0x0B57` em tekken2); com o heap a 64 MiB
// (`d3af3ec`) a familia TTD passou a correr e apareceu uma TERCEIRA, nove vezes
// por corrida: `glDisable(GL_STENCIL_TEST)`, com o valor 0x0B90. A recusa dizia
// "capacidade desconhecida (sem nome no cabecalho deste modulo)" -- e o defeito
// era NOSSO, da tabela, e nao do guest: 0x0B90 e o `GL_STENCIL_TEST` do
// cabecalho do SDK.
//
// A CONTRADICAO QUE ISTO CORRIGE, outra vez, e agora com a medida do lado. O
// relatorio da frente glbloco escreveu que o `glDisable` do tekken2 era
// `GL_STENCIL_TEST` com o valor 0x0B57. Nao e: 0x0B57 e GL_COLOR_MATERIAL, e
// GL_STENCIL_TEST vale 0x0B90. A troca nao e cosmetica -- o nome errado manda
// quem o ler procurar um buffer de stencil que ninguem pediu, quando o pedido
// era "a cor corrente alimenta o material" (o teken2 nao pede stencil nenhum).
// E o stencil verdadeiro (0x0B90) so apareceu OITO COMMITS depois, quando os
// nove titulos TTD passaram a correr.
constexpr std::uint32_t GL_RESCALE_NORMAL = 0x803Au;
constexpr std::uint32_t GL_COLOR_MATERIAL = 0x0B57u;
constexpr std::uint32_t GL_COLOR_LOGIC_OP = 0x0BF2u;
constexpr std::uint32_t GL_STENCIL_TEST = 0x0B90u;
constexpr std::uint32_t GL_POINT_SMOOTH = 0x0B10u;
constexpr std::uint32_t GL_LINE_SMOOTH = 0x0B20u;
constexpr std::uint32_t GL_MULTISAMPLE = 0x809Du;
constexpr std::uint32_t GL_SAMPLE_ALPHA_TO_COVERAGE = 0x809Eu;
constexpr std::uint32_t GL_SAMPLE_ALPHA_TO_ONE = 0x809Fu;
constexpr std::uint32_t GL_SAMPLE_COVERAGE = 0x80A0u;

// OS `pname` DO `glGetIntegerv`, e para que servem aqui: SO PARA A RECUSA SER
// LEGIVEL. Nenhum deles muda o que e servido -- um `pname` que o emulador nao
// saiba responder continua a RECUSAR (P2), mas a recusa passa a dizer o NOME em
// vez de so o numero, que e a diferenca entre uma linha que se pode ler e uma
// que se tem de ir procurar ao cabecalho.
//
// O QUE NAO ESTA NO `.inc` GERADO, e a razao e a mesma do `GL_RESCALE_NORMAL`
// acima: `tools/gl_slots.inc` e gerado de `AEEGL.h` e de `gles_1_0/gl.h` (o
// perfil Common-Lite, que NAO tem estes nomes). Os valores abaixo sao de
// `sdk/inc/gles/gles_1_1/gl.h` -- o cabecalho do IGLES11, que e a interface por
// onde o `IGLES11::GetIntegerv` chega -- com a linha exacta ao lado:
//
//   gles_1_1/gl.h:249   GL_CURRENT_COLOR      0x0B00
//   gles_1_1/gl.h:277   GL_MATRIX_MODE        0x0BA0
//   gles_1_1/gl.h:278   GL_VIEWPORT           0x0BA2
//   gles_1_1/gl.h:282   GL_MODELVIEW_MATRIX   0x0BA6
//   gles_1_1/gl.h:302   GL_MAX_VIEWPORT_DIMS  0x0D3A
//   gles_1_1/gl.h:316   GL_TEXTURE_BINDING_2D 0x8069
//   gles_1_1/gl.h:269   GL_STENCIL_CLEAR_VALUE  0x0B91
constexpr std::uint32_t GL_CURRENT_COLOR = 0x0B00u;
constexpr std::uint32_t GL_STENCIL_CLEAR_VALUE = 0x0B91u;
constexpr std::uint32_t GL_MATRIX_MODE = 0x0BA0u;
constexpr std::uint32_t GL_VIEWPORT = 0x0BA2u;
constexpr std::uint32_t GL_MODELVIEW_MATRIX = 0x0BA6u;
constexpr std::uint32_t GL_MAX_VIEWPORT_DIMS = 0x0D3Au;
constexpr std::uint32_t GL_TEXTURE_BINDING_2D = 0x8069u;

// O NOME DO `pname`, quando o cabecalho do SDK o da. Devolve `nullptr` para os
// que nao estao aqui: inventar um nome seria pior do que nao dizer nenhum.
const char* NomeDoPnameDeConsulta(std::uint32_t pname) {
  switch (pname) {
    case GL_CURRENT_COLOR: return "GL_CURRENT_COLOR";
    case GL_MATRIX_MODE: return "GL_MATRIX_MODE";
    case GL_VIEWPORT: return "GL_VIEWPORT";
    case GL_MODELVIEW_MATRIX: return "GL_MODELVIEW_MATRIX";
    case GL_MAX_VIEWPORT_DIMS: return "GL_MAX_VIEWPORT_DIMS";
    case GL_TEXTURE_BINDING_2D: return "GL_TEXTURE_BINDING_2D";
    case GL_STENCIL_CLEAR_VALUE: return "GL_STENCIL_CLEAR_VALUE";
    case GL_STENCIL_BITS: return "GL_STENCIL_BITS";
    case GL_MAX_TEXTURE_SIZE: return "GL_MAX_TEXTURE_SIZE";
    case GL_MAX_MODELVIEW_STACK_DEPTH: return "GL_MAX_MODELVIEW_STACK_DEPTH";
    case GL_MAX_PROJECTION_STACK_DEPTH: return "GL_MAX_PROJECTION_STACK_DEPTH";
    case GL_MAX_TEXTURE_STACK_DEPTH: return "GL_MAX_TEXTURE_STACK_DEPTH";
    default: return nullptr;
  }
}

// OS `pname` DA LUZ E DO MATERIAL, com o numero de componentes de cada um.
//
// O `glLightfv`/`glMaterialfv` do GL ES 1.x leva um tanto de componentes que
// DEPENDE do `pname`: GL_AMBIENT/GL_DIFFUSE/GL_SPECULAR sao 4, GL_SPOT_DIRECTION
// e 3, GL_SHININESS e GL_SPOT_CUTOFF sao 1. Ler sempre quatro valores (que e o
// que o `kIgl_Lightxv` desta casa faz hoje, e o que se poderia copiar) leria, no
// `GL_SHININESS` do gof, 12 bytes a seguir ao escalar que o titulo escreveu em
// 0x8007feec -- e acumularia lixo do quadro de pilha como se fosse estado. Os
// valores e os numeros de componentes sao do cabecalho, com a linha:
//
//   gles_1_0/gl.h:259 GL_AMBIENT 0x1200 | :260 GL_DIFFUSE 0x1201
//   :261 GL_SPECULAR 0x1202 | :262 GL_POSITION 0x1203
//   :263 GL_SPOT_DIRECTION 0x1204 (3) | :264 GL_SPOT_EXPONENT 0x1205 (1)
//   :265 GL_SPOT_CUTOFF 0x1206 (1) | :266 GL_CONSTANT_ATTENUATION 0x1207 (1)
//   :267 GL_LINEAR_ATTENUATION 0x1208 (1) | :268 GL_QUADRATIC_ATTENUATION 0x1209 (1)
//   :300 GL_EMISSION 0x1600 | :301 GL_SHININESS 0x1601 (1) | :302 GL_AMBIENT_AND_DIFFUSE 0x1602
//
// Um `pname` fora desta tabela RECUSA com o valor dele escrito (P2): nao ha
// medida do que a maquina faria com um `pname` que o cabecalho do SDK nao tem.
constexpr std::uint32_t GL_AMBIENT = 0x1200u;
constexpr std::uint32_t GL_DIFFUSE = 0x1201u;
constexpr std::uint32_t GL_SPECULAR = 0x1202u;
constexpr std::uint32_t GL_POSITION = 0x1203u;
constexpr std::uint32_t GL_SPOT_DIRECTION = 0x1204u;
constexpr std::uint32_t GL_SPOT_EXPONENT = 0x1205u;
constexpr std::uint32_t GL_SPOT_CUTOFF = 0x1206u;
constexpr std::uint32_t GL_CONSTANT_ATTENUATION = 0x1207u;
constexpr std::uint32_t GL_LINEAR_ATTENUATION = 0x1208u;
constexpr std::uint32_t GL_QUADRATIC_ATTENUATION = 0x1209u;
constexpr std::uint32_t GL_EMISSION = 0x1600u;
constexpr std::uint32_t GL_SHININESS = 0x1601u;
constexpr std::uint32_t GL_AMBIENT_AND_DIFFUSE = 0x1602u;
constexpr std::uint32_t GL_LIGHT0 = 0x4000u;
// O MODELO DE LUZ. Nao estao no `.inc` gerado, e os valores sao os do
// cabecalho do SDK:
//   gles_1_0/gl.h:255 / gles_1_1/gl.h:362  GL_LIGHT_MODEL_AMBIENT 0x0B53
//   gles_1_1/gl.h:363                       GL_LIGHT_MODEL_TWO_SIDE 0x0B52
constexpr std::uint32_t GL_LIGHT_MODEL_AMBIENT = 0x0B53u;
constexpr std::uint32_t GL_LIGHT_MODEL_TWO_SIDE = 0x0B52u;
// O GL ES 1.x garante OITO luzes (`GL_MAX_LIGHTS`, gl.h:223, e 8). O valor da
// MAQUINA nao foi medido: o limite aqui e o que a especificacao garante, e um
// indice acima dele recusa com essa razao escrita, em vez de inventar um numero.
constexpr std::uint32_t kLuzesDoGlEs = 8u;

struct Pname {
  std::uint32_t valor;
  const char* nome;
  int componentes;
};
const Pname kPnamesDeLuz[] = {
    {GL_AMBIENT, "GL_AMBIENT", 4},
    {GL_DIFFUSE, "GL_DIFFUSE", 4},
    {GL_SPECULAR, "GL_SPECULAR", 4},
    {GL_POSITION, "GL_POSITION", 4},
    {GL_SPOT_DIRECTION, "GL_SPOT_DIRECTION", 3},
    {GL_SPOT_EXPONENT, "GL_SPOT_EXPONENT", 1},
    {GL_SPOT_CUTOFF, "GL_SPOT_CUTOFF", 1},
    {GL_CONSTANT_ATTENUATION, "GL_CONSTANT_ATTENUATION", 1},
    {GL_LINEAR_ATTENUATION, "GL_LINEAR_ATTENUATION", 1},
    {GL_QUADRATIC_ATTENUATION, "GL_QUADRATIC_ATTENUATION", 1},
};
const Pname kPnamesDeMaterial[] = {
    {GL_AMBIENT, "GL_AMBIENT", 4},
    {GL_DIFFUSE, "GL_DIFFUSE", 4},
    {GL_AMBIENT_AND_DIFFUSE, "GL_AMBIENT_AND_DIFFUSE", 4},
    {GL_SPECULAR, "GL_SPECULAR", 4},
    {GL_EMISSION, "GL_EMISSION", 4},
    {GL_SHININESS, "GL_SHININESS", 1},
};
template <std::size_t N>
const Pname* AcharPname(const Pname (&tabela)[N], std::uint32_t pname) {
  for (const Pname& p : tabela) {
    if (p.valor == pname) return &p;
  }
  return nullptr;
}

// UM VECTOR DE 4 PASSADO PELA MODELVIEW CORRENTE. E o que o `glLightfv` faz com
// a POSICAO da luz (e com a direccao do holofote, com w = 0): a luz passa a
// viver em coordenadas de olho, onde o rasterizador a sabe usar.
void AplicarNaMatriz(const float* m, const float* v, float* saida) {
  for (int r = 0; r < 4; ++r) {
    saida[r] = m[0 * 4 + r] * v[0] + m[1 * 4 + r] * v[1] + m[2 * 4 + r] * v[2] + m[3 * 4 + r] * v[3];
  }
}

// Um valor guardado como GLfixed (16.16) para `float`. Os `xv` desta casa
// guardam as palavras cruas do guest, e um deles e a unica fonte da ambiente da
// cena (`glLightModelxv`).
float RealDoFixo(std::uint32_t bits) {
  return static_cast<float>(static_cast<std::int32_t>(bits)) / 65536.0f;
}

// A chave do estado de luz / material: (luz ou face) e o `pname`.
std::uint64_t ChaveDoAlvo(std::uint32_t alvo, std::uint32_t pname) {
  return (static_cast<std::uint64_t>(alvo) << 32) | pname;
}

// As capacidades que o estado deste modulo sabe acumular, com o nome no SDK.
struct Capacidade {
  std::uint32_t cap;
  const char* nome;
};
const Capacidade kCapacidades[] = {
    {GL_CULL_FACE, "GL_CULL_FACE"},
    {GL_LIGHTING, "GL_LIGHTING"},
    {GL_TEXTURE_2D, "GL_TEXTURE_2D"},
    {GL_NORMALIZE, "GL_NORMALIZE"},
    {GL_ALPHA_TEST, "GL_ALPHA_TEST"},
    {GL_BLEND, "GL_BLEND"},
    {GL_DITHER, "GL_DITHER"},
    {GL_FOG, "GL_FOG"},
    {GL_DEPTH_TEST, "GL_DEPTH_TEST"},
    {GL_SCISSOR_TEST, "GL_SCISSOR_TEST"},
    {GL_POLYGON_OFFSET_FILL, "GL_POLYGON_OFFSET_FILL"},
    // As que o `.inc` gerado NAO tem, todas do cabecalho do SDK (o bloco acima
    // tem a linha de cada uma). A lista e a `/* EnableCap */` inteira do
    // `gles_1_1/gl.h:189-221`: **uma capacidade do GL ES 1.1 nao pode voltar a
    // ser recusada por nao ter nome**. O que ela NAO promete e um efeito -- a
    // lista `por_fazer` do `MontarEstado` diz, uma por uma, o que esta arvore
    // nao faz com o interruptor ligado.
    {GL_RESCALE_NORMAL, "GL_RESCALE_NORMAL"},
    {GL_COLOR_MATERIAL, "GL_COLOR_MATERIAL"},
    {GL_COLOR_LOGIC_OP, "GL_COLOR_LOGIC_OP"},
    {GL_STENCIL_TEST, "GL_STENCIL_TEST"},
    {GL_POINT_SMOOTH, "GL_POINT_SMOOTH"},
    {GL_LINE_SMOOTH, "GL_LINE_SMOOTH"},
    {GL_MULTISAMPLE, "GL_MULTISAMPLE"},
    {GL_SAMPLE_ALPHA_TO_COVERAGE, "GL_SAMPLE_ALPHA_TO_COVERAGE"},
    {GL_SAMPLE_ALPHA_TO_ONE, "GL_SAMPLE_ALPHA_TO_ONE"},
    {GL_SAMPLE_COVERAGE, "GL_SAMPLE_COVERAGE"},
    // AS OITO LUZES. `glEnable(GL_LIGHT0)` NAO e uma capacidade desconhecida: e
    // a luz 0, e sem estes nomes ela era RECUSADA -- medido, 299 recusas por
    // corrida em rmp (`slot=28 args=[00004000 ...]`), que e uma luz que o
    // titulo liga e o emulador diz nao conhecer.
    //   gles/gles_1_0/gl.h:461-468  GL_LIGHT0 0x4000 ... GL_LIGHT7 0x4007
    {GL_LIGHT0 + 0u, "GL_LIGHT0"},
    {GL_LIGHT0 + 1u, "GL_LIGHT1"},
    {GL_LIGHT0 + 2u, "GL_LIGHT2"},
    {GL_LIGHT0 + 3u, "GL_LIGHT3"},
    {GL_LIGHT0 + 4u, "GL_LIGHT4"},
    {GL_LIGHT0 + 5u, "GL_LIGHT5"},
    {GL_LIGHT0 + 6u, "GL_LIGHT6"},
    {GL_LIGHT0 + 7u, "GL_LIGHT7"},
};
const char* NomeDaCapacidade(std::uint32_t cap) {
  for (const auto& c : kCapacidades) {
    if (c.cap == cap) return c.nome;
  }
  return nullptr;
}

const char* NomeDoArray(std::uint32_t a) {
  switch (a) {
    case GL_VERTEX_ARRAY: return "GL_VERTEX_ARRAY";
    case GL_NORMAL_ARRAY: return "GL_NORMAL_ARRAY";
    case GL_COLOR_ARRAY: return "GL_COLOR_ARRAY";
    case GL_TEXTURE_COORD_ARRAY: return "GL_TEXTURE_COORD_ARRAY";
    default: return nullptr;
  }
}

const char* NomeDaPrimitiva(std::uint32_t m) {
  switch (m) {
    case GL_POINTS: return "GL_POINTS";
    case GL_LINES: return "GL_LINES";
    case GL_LINE_LOOP: return "GL_LINE_LOOP";
    case GL_LINE_STRIP: return "GL_LINE_STRIP";
    case GL_TRIANGLES: return "GL_TRIANGLES";
    case GL_TRIANGLE_STRIP: return "GL_TRIANGLE_STRIP";
    case GL_TRIANGLE_FAN: return "GL_TRIANGLE_FAN";
    default: return nullptr;
  }
}

bool TipoDeVerticeValido(std::uint32_t t, bool com_fixo) {
  if (t == GL_BYTE || t == GL_UNSIGNED_BYTE || t == GL_SHORT || t == GL_UNSIGNED_SHORT) return true;
  if (com_fixo && t == GL_FIXED) return true;
  return t == GL_FLOAT;
}

// A MASCARA DO `glClear`, POR NOME. Um `glClear(0x00004100)` no detalhe obriga
// quem o le a ir procurar os bits ao cabecalho; com os nomes ao lado, o que o
// titulo pediu le-se. O valor cru fica tambem: um bit que a tabela nao nomeie
// aparece no numero, e nao desaparece.
std::string NomeDaMascaraDeLimpeza(std::uint32_t mascara) {
  std::string s;
  const auto juntar = [&](std::uint32_t bit, const char* nome) {
    if ((mascara & bit) == 0) return;
    if (!s.empty()) s += "|";
    s += nome;
  };
  // Os tres valores sao do `.inc` gerado (`gles/gl.h`): GL_COLOR_BUFFER_BIT
  // 0x4000, GL_DEPTH_BUFFER_BIT 0x0100, GL_STENCIL_BUFFER_BIT 0x0400.
  juntar(GL_COLOR_BUFFER_BIT, "GL_COLOR_BUFFER_BIT");
  juntar(GL_DEPTH_BUFFER_BIT, "GL_DEPTH_BUFFER_BIT");
  juntar(GL_STENCIL_BUFFER_BIT, "GL_STENCIL_BUFFER_BIT");
  char hex[24];
  std::snprintf(hex, sizeof(hex), "0x%08x", mascara);
  return s.empty() ? std::string(hex) : (s + " [" + hex + "]");
}

std::uint64_t ChaveDeParametro(std::uint32_t slot, std::uint32_t pname) {
  return (static_cast<std::uint64_t>(slot) << 32) | pname;
}

// O NOME DE CADA SLOT DO MOTOR. Os 80 do `AEEGL.h` vem da tabela gerada; os CINCO
// internos do IGLES11 (`kIgl_Lightfv`, `kIgl_Materialfv`, `kIgl_Orthof`,
// `kIgl_AlphaFunc` e o `kIgl_Color4f`) NAO estao la -- e sem este mapa o traco
// escrevia `slot_fora_da_tabela`, que e uma recusa que nao se pode ler. O nome e
// o do metodo do GL, porque e esse o metodo que foi chamado.
const char* NomeDoSlotDoMotor(std::uint32_t slot) {
  switch (slot) {
    case kIgl_Lightfv: return "glLightfv";
    case kIgl_Materialfv: return "glMaterialfv";
    case kIgl_Orthof: return "glOrthof";
    case kIgl_AlphaFunc: return "glAlphaFunc";
    case kIgl_Color4f: return "glColor4f";
    default: return NomeIgl(slot);
  }
}

}  // namespace

const char* Nome(ResultadoGl r) {
  switch (r) {
    case ResultadoGl::Feito: return "feito";
    case ResultadoGl::Recusado: return "recusado";
    case ResultadoGl::NaoImplementado: return "nao_implementado";
  }
  return "?";
}

Igl::Igl(Memoria& mem, Traco& traco)
    : mem_(mem), traco_(traco), rasterizador_(mem, destino_) {
  Identidade(mv_.m[0]);
  Identidade(proj_.m[0]);
  Identidade(tex_.m[0]);
  mv_.fundo = kFundoModelView;
  proj_.fundo = kFundoProjection;
  tex_.fundo = kFundoTexture;
}

std::uint32_t Igl::Instalar(const Saidas& saidas) {
  objeto_ = kObjIgl;
  vtable_ = saidas.Endereco(kVtableIgl);
  ConstruirObjeto(mem_, saidas, objeto_, vtable_, kIglSlots, kVtableIgl);

  // A LEITURA DE VOLTA. Uma cablagem que nao se confirma a si propria perde-se em
  // silencio -- ja aconteceu uma vez neste trabalho, e o sintoma foi a bateria a
  // dizer "falta SetTimer" com o SetTimer a funcionar.
  for (std::uint32_t i = 0; i < kIglSlots; ++i) {
    const std::uint32_t lido = mem_.Ler32(vtable_ + i * 4);
    std::uint32_t esperado = saidas.Endereco(kVtableIgl + i);
    if (i == 0) esperado = saidas.Endereco(3);  // AddRef, do `ConstruirObjeto`
    if (i == 1) esperado = saidas.Endereco(4);  // Release
    if (lido != esperado) {
      char det[160];
      std::snprintf(det, sizeof(det),
                    "slot %u da vtable do IGL tem 0x%08x, devia ter 0x%08x", i, lido, esperado);
      traco_.RegistarFalta(Area::Video, "cablagem_do_igl_perdida", det);
      return 0;
    }
  }

  // O QUE O RASTERIZADOR FAZ, E O QUE NAO FAZ, REGISTADO UMA VEZ E COM NOME.
  //
  // ESTE REGISTO SUBSTITUI UMA FALTA, e a substituicao e o ponto: aqui estava
  //
  //     traco_.RegistarFalta(Area::Video, "rasterizador_de_GL",
  //                          "o estado e a interface existem; nenhum pixel e escrito. "
  //                          "Medido: 0 pixels em 62 titulos antes desta etapa.");
  //
  // que era verdade antes de `core/video/rasterizador.cpp` existir e passaria a
  // ser MENTIRA no minuto seguinte (P7: um log so entra se puder ser verdadeiro).
  // O que fica escrito e o que o rasterizador faz, e o que ficou de fora -- com o
  // ficheiro onde a lista completa esta.
  traco_.Emitir(Area::Video, Nivel::Informacao, "RASTERIZADOR",
                "TRIANGLES/STRIP/FAN, cor por vertice, textura GL_NEAREST, teste de "
                "profundidade e descarte de faces; AFIM e sem blending/stencil/mipmaps "
                "(lista completa no topo de core/video/rasterizador.h). Escreve na Tela "
                "quando o despacho a liga (Igl::DefinirTela)");
  traco_.Emitir(Area::Video, Nivel::Informacao, "IGL_INSTALADO",
                "80 slots (AEEGL.h), objecto em 0x800B0000, vtable cablada e conferida");
  return kIglSlots;
}

// --- O RETRATO DO ESTADO PARA O RASTERIZADOR ---------------------------------

video::EstadoDeRasterizacao Igl::MontarEstado() const {
  using namespace gl_slots;
  video::EstadoDeRasterizacao e;
  for (int k = 0; k < 16; ++k) {
    e.modelview[k] = mv_.m[mv_.topo][k];
    e.projection[k] = proj_.m[proj_.topo][k];
  }
  for (int k = 0; k < 4; ++k) e.viewport[k] = viewport_[k];
  e.cor = video::Desempacotar(cor_);
  e.cor_de_limpeza = video::Desempacotar(cor_limpeza_);
  e.mascara_de_limpeza = mascara_limpeza_;
  e.profundidade_de_limpeza = profundidade_limpeza_;
  e.profundidade_perto = profundidade_perto_;
  e.profundidade_longe = profundidade_longe_;
  for (int k = 0; k < 4; ++k) e.scissor[k] = scissor_[k];
  e.teste_de_scissor = InterruptorLigado(gl_slots::GL_SCISSOR_TEST);

  // UM ARRAY SO ESTA LIGADO SE AS DUAS COISAS FOREM VERDADE: o
  // `glEnableClientState` e o `gl*Pointer` que o definiu. O `igl.cpp` ja guarda
  // as duas separadas de proposito (o `glEnable(GL_TEXTURE_2D)` NAO pode ligar o
  // array de coordenadas), e o rasterizador ve o resultado dessa separacao.
  const auto copiar = [&](std::uint32_t alvo, video::ArrayDoCliente* destino) {
    const ArrayDeVertices* a = Array(alvo);
    if (a == nullptr) return;
    destino->tamanho = a->tamanho;
    destino->tipo = a->tipo;
    destino->passo = a->passo;
    destino->ponteiro = a->ponteiro;
    destino->ligado = ArrayDeClienteLigado(alvo);
  };
  copiar(GL_VERTEX_ARRAY, &e.vertices);
  copiar(GL_COLOR_ARRAY, &e.cores);
  copiar(GL_TEXTURE_COORD_ARRAY, &e.coordenadas_de_textura);
  e.cor_por_vertice = e.cores.ligado;

  // A TEXTURA LIGADA SO CONTA COM O `GL_TEXTURE_2D` LIGADO: e o que o GL faz. Uma
  // textura com o alvo desligado nao e amostrada, e o desenho usa a cor.
  //
  // E SO ENTRA NO RETRATO SE O RASTERIZADOR A SOUBER AMOSTRAR. Cada uma das tres
  // razoes para nao entrar fica NOMEADA em `capacidades_por_fazer` (registada uma
  // vez, com o nome, por `RegistarRessalvas`) e o desenho segue com a cor do
  // vertice -- em vez de RECUSAR o desenho inteiro. A diferenca e medida: o
  // `Desenhar` recusa uma textura comprimida ou de formato desconhecido, e uma
  // recusa dessas apagaria os 307 200 px do `ridgeracer` (a reserva do ponto 2) e
  // os 91 852 828 px do `pbc` (ATC, sem descodificador nesta arvore) -- numeros
  // que a corrida de referencia TEM. Perder um desenho inteiro por causa de uma
  // textura e pior do que desenhar sem ela e dize-lo.
  if (InterruptorLigado(GL_TEXTURE_2D) && textura_ligada_ != 0) {
    const EstadoDaTextura* t = Textura(textura_ligada_);
    if (t != nullptr) {
      if (t->comprimida) {
        e.capacidades_por_fazer.push_back("textura_comprimida_sem_descodificador");
      } else if (t->ponteiro == 0) {
        // A RESERVA: `glTexImage2D(..., pixels = NULL)` registou dimensoes e
        // formato, e os texels ainda nao chegaram (`glTexSubImage2D`).
        e.capacidades_por_fazer.push_back("textura_reservada_sem_texels");
      } else if (!video::TexturaAmostravel(t->formato_do_pixel, t->tipo)) {
        e.capacidades_por_fazer.push_back("formato_de_textura_sem_caminho_de_amostragem");
      } else {
        e.textura_ligada = true;
        e.textura.existe = true;
        e.textura.comprimida = t->comprimida;
        e.textura.largura = t->largura;
        e.textura.altura = t->altura;
        e.textura.formato = t->formato_do_pixel;
        e.textura.tipo = t->tipo;
        e.textura.ponteiro = t->ponteiro;
      }
    }
  }

  e.sombreado_plano = (shade_model_ == GL_FLAT);
  e.teste_de_profundidade = InterruptorLigado(GL_DEPTH_TEST);
  e.escrever_profundidade = depth_mask_;
  e.funcao_de_profundidade = depth_func_;
  e.descartar_faces = InterruptorLigado(GL_CULL_FACE);
  e.descartar_face = cull_face_;
  e.orientacao_da_frente = front_face_;

  // A LARGURA DA LINHA. O `kIgl_LineWidthx` guarda o valor em `parametros_` desde
  // sempre e respondia "feito"; ate esta frente o rasterizador nao tinha onde o
  // ler, e o pedido estava guardado e NAO era aplicado. Agora ele chega ao
  // estado: <= 1 desenha uma linha de um pixel, > 1 RECUSA o desenho com o nome
  // (uma linha de 2 px desenhada com 1 px seria a mentira silenciosa).
  if (const std::vector<std::uint32_t>* largura = Parametro(kIgl_LineWidthx, 0);
      largura != nullptr && !largura->empty()) {
    e.largura_de_linha = RealDoFixo((*largura)[0]);
  }

  // A MISTURA, O ALPHA TEST E A MASCARA DE COR. Os tres vao para o retrato
  // porque os tres decidem pixel: a mistura soma contra o destino, o teste
  // descarta o fragmento, a mascara preserva os canais que proibe.
  e.mistura_ligada = InterruptorLigado(GL_BLEND);
  e.mistura_fonte = mistura_fonte_;
  e.mistura_destino = mistura_destino_;
  e.teste_de_alfa = InterruptorLigado(GL_ALPHA_TEST);
  e.funcao_de_alfa = funcao_de_alfa_;
  e.alfa_de_referencia = alfa_de_referencia_;
  e.mascara_de_cor = color_mask_;

  // --- A LUZ, O MATERIAL E AS NORMAIS -------------------------------------
  //
  // O MATERIAL E UM SO: no GL ES 1.x nao ha iluminacao de dois lados, e a face
  // do `glMaterialfv` nao muda a conta (guarda-se, e o pedido fica observavel em
  // `ParametroDeMaterial`). As OITO luzes vao todas: um titulo que ligue a luz 3
  // e nao a 0 tem de a ter ligada no retrato.
  e.iluminacao_ligada = InterruptorLigado(GL_LIGHTING);
  e.cor_do_material = InterruptorLigado(GL_COLOR_MATERIAL);
  e.normalizar_normais = InterruptorLigado(GL_NORMALIZE);
  e.reescalar_normais = InterruptorLigado(GL_RESCALE_NORMAL);
  copiar(GL_NORMAL_ARRAY, &e.normais);
  {
    // A ORDEM DE PROCURA E `FRONT_AND_BACK`, `FRONT`, `BACK`: o GL escreve o
    // material dos dois lados com o primeiro, e com material unico os tres sao o
    // mesmo objecto.
    const std::uint32_t faces[3] = {GL_FRONT_AND_BACK, GL_FRONT, GL_BACK};
    const auto do_material = [&](std::uint32_t pname, float* destino) {
      for (const std::uint32_t face : faces) {
        const std::vector<float>* v = ParametroDeMaterial(face, pname);
        if (v == nullptr) continue;
        for (std::size_t k = 0; k < v->size() && k < 4; ++k) destino[k] = (*v)[k];
        return;
      }
    };
    // O `GL_AMBIENT_AND_DIFFUSE` escreve nos DOIS, e e aplicado PRIMEIRO: a
    // ordem entre ele e os dois `pname` separados nao e guardada (o mapa guarda
    // um valor por `pname`), e o valor explicito a ganhar e a leitura mais
    // conservadora.
    do_material(GL_AMBIENT_AND_DIFFUSE, e.material.ambiente);
    do_material(GL_AMBIENT_AND_DIFFUSE, e.material.difusa);
    do_material(GL_AMBIENT, e.material.ambiente);
    do_material(GL_DIFFUSE, e.material.difusa);
    do_material(GL_SPECULAR, e.material.especular);
    do_material(GL_EMISSION, e.material.emissao);
    do_material(GL_SHININESS, &e.material.brilho);
  }
  for (std::uint32_t i = 0; i < kLuzesDoGlEs; ++i) {
    const std::uint32_t nome = GL_LIGHT0 + i;
    video::EstadoDeRasterizacao::Luz& luz = e.luzes[i];
    luz.ligada = InterruptorLigado(nome);
    const auto valor = [&](std::uint32_t pname, float* destino, int quantos) {
      // A POSICAO VEM DA COPIA EM COORDENADAS DE OLHO quando existe; as outras
      // grandezas sao cores e numeros, e valem iguais nos dois espacos.
      const std::vector<float>* em_olho = ParametroDeLuzEmOlho(nome, pname);
      const std::vector<float>* v = (em_olho != nullptr) ? em_olho : ParametroDeLuz(nome, pname);
      if (v == nullptr) return;  // sem pedido: fica a omissao do GL
      for (int k = 0; k < quantos && k < static_cast<int>(v->size()); ++k) {
        destino[k] = (*v)[static_cast<std::size_t>(k)];
      }
    };
    valor(GL_AMBIENT, luz.ambiente, 4);
    valor(GL_DIFFUSE, luz.difusa, 4);
    valor(GL_SPECULAR, luz.especular, 4);
    valor(GL_POSITION, luz.posicao, 4);
    valor(GL_SPOT_DIRECTION, luz.direcao_do_holofote, 3);
    valor(GL_SPOT_EXPONENT, &luz.exponente_do_holofote, 1);
    valor(GL_SPOT_CUTOFF, &luz.corte_do_holofote, 1);
    valor(GL_CONSTANT_ATTENUATION, &luz.atenuacao[0], 1);
    valor(GL_LINEAR_ATTENUATION, &luz.atenuacao[1], 1);
    valor(GL_QUADRATIC_ATTENUATION, &luz.atenuacao[2], 1);
  }
  // A AMBIENTE DA CENA (`glLightModelxv(GL_LIGHT_MODEL_AMBIENT, ...)`). O valor
  // e lido do ESTADO DO MOTOR, onde o caso do `kIgl_LightModelxv` o guarda em
  // `float` e ja em [0,1] -- e nao de `parametros_`, que so guardava as palavras
  // cruas: um mapa que ninguem decodifica e indistinguivel de um pedido perdido
  // (P2). Sem nenhum pedido fica a omissao do GL que o retrato ja tem.
  for (int k = 0; k < 4; ++k) e.ambiente_da_cena[k] = ambiente_da_cena_[k];
  //
  // AS VARIANTES `x`/`xv` DE LUZ E MATERIAL (`glLightxv`, `glMaterialxv`) NAO
  // ALIMENTAM ESTE RETRATO, e isso fica dito: elas guardam palavras CRUAS do
  // guest num mapa diferente, e nenhum dos titulos medidos deste corpus as
  // chama (o traco de gof, rmp, pbc, pacmania e tekken2 nao tem uma linha
  // `glLightxv`/`glMaterialxv`). Implementa-las as cegas seria adivinhar a
  // escala dos valores.

  // O QUE OS TITULOS LIGARAM E O RASTERIZADOR NAO FAZ. Cada nome vai para o
  // traco (uma vez, no `RegistarRessalvas`), com o nome do que falta -- e nao em
  // silencio, que foi o que o `glCullFace` fez 86 377 vezes na arvore antiga.
  // A TABELA ENCOLHEU (frente rast2): `GL_BLEND`, `GL_ALPHA_TEST` e a mascara de
  // cor, e depois `GL_LIGHTING`, `GL_NORMALIZE`, `GL_RESCALE_NORMAL` e
  // `GL_COLOR_MATERIAL`, SAIRAM dela porque o rasterizador passou a faze-las.
  // Uma capacidade que deixa de faltar TEM de sair desta lista: uma linha a
  // dizer que falta, com o codigo a faze-la, e a mentira simetrica do stub mudo.
  //
  // A LISTA CRESCEU (frente igl9) com as capacidades do GL ES 1.1 que a tabela
  // de constantes passou a CONHECER. Uma capacidade conhecida que nao muda
  // nenhum pixel TEM de estar aqui, e nao em silencio: a alternativa era
  // recusa-la por "capacidade desconhecida", que e uma mentira sobre o
  // cabecalho do SDK, ou aceita-la sem mais, que e o stub mudo.
  const struct { std::uint32_t cap; const char* nome; } por_fazer[] = {
      {GL_FOG, "nevoa_de_GL_sem_rasterizador"},
      {GL_POLYGON_OFFSET_FILL, "polygon_offset_sem_rasterizador"},
      {GL_DITHER, "dithering_sem_rasterizador"},
      // O STENCIL: o pedido MEDIDO 9x nos nove titulos TTD
      // (`glDisable(GL_STENCIL_TEST)`), e nao ha buffer de stencil nenhum nesta
      // arvore (`core/brew/egl.cpp:151`, EGL_STENCIL_SIZE 0). O interruptor
      // fica guardado (o `IsEnabled` nao mente) e o limite fica DITO.
      {GL_STENCIL_TEST, "teste_de_stencil_sem_buffer_de_stencil"},
      {GL_COLOR_LOGIC_OP, "operacao_logica_de_cor_sem_rasterizador"},
      {GL_POINT_SMOOTH, "suavizacao_de_ponto_sem_rasterizador"},
      {GL_LINE_SMOOTH, "suavizacao_de_linha_sem_rasterizador"},
      {GL_MULTISAMPLE, "multiamostragem_sem_rasterizador"},
      {GL_SAMPLE_ALPHA_TO_COVERAGE, "multiamostragem_sem_rasterizador"},
      {GL_SAMPLE_ALPHA_TO_ONE, "multiamostragem_sem_rasterizador"},
      {GL_SAMPLE_COVERAGE, "multiamostragem_sem_rasterizador"},
  };
  for (const auto& f : por_fazer) {
    if (InterruptorLigado(f.cap)) e.capacidades_por_fazer.push_back(f.nome);
  }
  // A MASCARA DE COR SAIU DA LISTA DAS FALTAS: o rasterizador aplica-a contra o
  // pixel que ja esta la (`EscreverPixel`), e por isso um titulo que proiba um
  // canal ja nao precisa de ser avisado de que o pedido dele nao vale nada.

  // O FILTRO DA TEXTURA. O rasterizador amostra sempre o texel mais proximo; um
  // titulo que peca GL_LINEAR fica com essa diferenca escrita, e nao silenciosa.
  for (const std::uint32_t pname : {GL_TEXTURE_MIN_FILTER, GL_TEXTURE_MAG_FILTER}) {
    const std::vector<std::uint32_t>* v = Parametro(kIgl_TexParameterx, pname);
    if (v != nullptr && !v->empty() && (*v)[0] != GL_NEAREST) {
      e.capacidades_por_fazer.push_back("filtro_de_textura_alem_de_GL_NEAREST");
      break;
    }
  }
  return e;
}

void Igl::RegistarRessalvas(const video::EstadoDeRasterizacao& e) {
  for (const std::string& nome : e.capacidades_por_fazer) {
    if (recusas_.find(nome) != recusas_.end()) continue;
    recusas_[nome] = 1;
    if (nome == "filtro_de_textura_alem_de_GL_NEAREST") {
      // Precedente do stencil: capacidade que esta arvore nao tem vira
      // PRESSUPOSTO com nome e razao, e nao falta. O rasterizador amostra o
      // texel mais proximo; um titulo que peca GL_LINEAR desenha por inteiro
      // com diferenca sub-texel -- nao ha ausencia para recusar, ha uma
      // aproximacao para declarar. MEDIDO em 7 titulos, 1 pedido cada.
      traco_.RegistarPressuposto(Area::Video, nome,
                                 "filtro LINEAR pedido; rasterizador amostra NEAREST "
                                 "(texel mais proximo). Titulo desenha por inteiro.");
      continue;
    }
    traco_.RegistarFalta(Area::Video, nome,
                         "capacidade ligada pelo titulo e NAO implementada no rasterizador "
                         "(lista no topo de core/video/rasterizador.h)");
  }
}

// --- leitura dos argumentos --------------------------------------------------

std::uint32_t Igl::Arg(std::size_t i, const ArgumentosGl& a) const {
  if (i < 4) return a.reg[i];
  return mem_.Ler32(a.sp + static_cast<std::uint32_t>((i - 4) * 4));
}

float Igl::Fixo(std::size_t i, const ArgumentosGl& a) const {
  // GLfixed: 16 bits inteiros e 16 fraccionarios.
  return static_cast<float>(static_cast<std::int32_t>(Arg(i, a))) / 65536.0f;
}

float Igl::Real(std::size_t i, const ArgumentosGl& a) const {
  // `AEEGLfloat` de 32 bits: a palavra que veio no registo (ou na pilha) E o
  // `float`, e nao ha conversao nenhuma a fazer. Reinterpretar os bits com
  // `memcpy` e o que evita o comportamento indefinido de um `*reinterpret_cast`
  // num tipo alinhado a 4 que veio de um registo.
  const std::uint32_t bits = Arg(i, a);
  float v = 0.0f;
  std::memcpy(&v, &bits, sizeof(v));
  return v;
}

// O MESMO, mas lendo da MEMORIA DO GUEST: `const AEEGLfloat *params`.
float Igl::RealDaMemoria(std::uint32_t endereco) const {
  const std::uint32_t bits = mem_.Ler32(endereco);
  float v = 0.0f;
  std::memcpy(&v, &bits, sizeof(v));
  return v;
}

void Igl::LerMatriz(std::size_t i, const ArgumentosGl& a, float* saida) const {
  const std::uint32_t p = Arg(i, a);
  for (int k = 0; k < 16; ++k) {
    saida[k] = static_cast<float>(static_cast<std::int32_t>(mem_.Ler32(p + 4u * k))) / 65536.0f;
  }
}

PilhaDeMatrizes& Igl::PilhaDoModo() {
  if (modo_ == kModoProjection) return proj_;
  if (modo_ == kModoTexture) return tex_;
  return mv_;
}

const float* Igl::MatrizCorrente() const {
  if (modo_ == kModoProjection) return proj_.m[proj_.topo];
  if (modo_ == kModoTexture) return tex_.m[tex_.topo];
  return mv_.m[mv_.topo];
}

int Igl::TopoDaPilha(int modo) const {
  if (modo == kModoProjection) return proj_.topo;
  if (modo == kModoTexture) return tex_.topo;
  return mv_.topo;
}

const EstadoDaTextura* Igl::Textura(std::uint32_t id) const {
  const auto it = texturas_.find(id);
  return it == texturas_.end() ? nullptr : &it->second;
}

bool Igl::InterruptorLigado(std::uint32_t cap) const {
  const auto it = interruptores_.find(cap);
  return it != interruptores_.end() && it->second != 0;
}

bool Igl::ArrayDeClienteLigado(std::uint32_t array) const {
  const auto it = arrays_de_cliente_.find(array);
  if (it != arrays_de_cliente_.end() && it->second) return true;
  // `glEnable(GL_VERTEX_ARRAY)` e `glEnableClientState(GL_VERTEX_ARRAY)` ligam a
  // MESMA coisa. O GL ES 1.x define os dois caminhos para a mesma capacidade, e
  // um teste que so olhasse para um deles passaria com o jogo a usar o outro.
  const auto c = interruptores_.find(array);
  return c != interruptores_.end() && c->second != 0;
}

const ArrayDeVertices* Igl::Array(std::uint32_t array) const {
  const auto it = arrays_.find(array);
  if (it == arrays_.end() || !it->second.definido) return nullptr;
  return &it->second;
}

std::uint32_t Igl::Viewport(int i) const {
  return (i >= 0 && i < 4) ? viewport_[i] : 0;
}

const std::vector<std::uint32_t>* Igl::Parametro(std::uint32_t slot, std::uint32_t pname) const {
  const auto it = parametros_.find(ChaveDeParametro(slot, pname));
  return it == parametros_.end() ? nullptr : &it->second;
}

const std::vector<float>* Igl::ParametroDeLuz(std::uint32_t luz, std::uint32_t pname) const {
  const auto it = luzes_.find(ChaveDoAlvo(luz, pname));
  return it == luzes_.end() ? nullptr : &it->second;
}

const std::vector<float>* Igl::ParametroDeLuzEmOlho(std::uint32_t luz, std::uint32_t pname) const {
  const auto it = luzes_em_olho_.find(ChaveDoAlvo(luz, pname));
  return it == luzes_em_olho_.end() ? nullptr : &it->second;
}

const std::vector<float>* Igl::ParametroDeMaterial(std::uint32_t face, std::uint32_t pname) const {
  const auto it = materiais_.find(ChaveDoAlvo(face, pname));
  return it == materiais_.end() ? nullptr : &it->second;
}

std::uint64_t Igl::ChamadasDoSlot(std::uint32_t slot) const {
  const auto it = por_slot_.find(slot);
  return it == por_slot_.end() ? 0 : it->second;
}

// --- registo ----------------------------------------------------------------

ResultadoGl Igl::Recusar(const std::string& motivo, ChamadaGl& c) {
  c.motivo = motivo;
  c.resultado = ResultadoGl::Recusado;
  return ResultadoGl::Recusado;
}

ResultadoGl Igl::NaoTem(ChamadaGl& c) {
  c.motivo = "slot sem implementacao nesta etapa";
  c.resultado = ResultadoGl::NaoImplementado;
  return ResultadoGl::NaoImplementado;
}

void Igl::Registar(const ChamadaGl& c) {
  // UM SO PONTO DE EMISSAO. E a regra 2 do `core/traco`: uma linha duplicada de
  // log (o `GlBindTexture` da arvore antiga) nasce de haver dois sitios a emitir.
  char det[256];
  std::snprintf(det, sizeof(det), "slot=%u args=[%08x %08x %08x %08x] lr=0x%08x -> %s%s%s",
                c.slot, c.args[0], c.args[1], c.args[2], c.args[3], c.lr, Nome(c.resultado),
                c.motivo.empty() ? "" : " | ", c.motivo.c_str());
  traco_.Emitir(Area::Video, c.resultado == ResultadoGl::Feito ? Nivel::Informacao : Nivel::Aviso,
                "GL_" + c.nome, det);
  if (c.resultado != ResultadoGl::Feito) {
    ++recusas_[c.nome];
    traco_.RegistarFalta(Area::Video, c.nome, c.motivo + " | " + det);
  }
  ultimas_.push_back(c);
  constexpr std::size_t kGuarda = 64;
  if (ultimas_.size() > kGuarda) ultimas_.erase(ultimas_.begin());
}

ResultadoGl Igl::Executar(std::uint32_t slot, const ArgumentosGl& a, std::uint32_t* retorno) {
  // Os nomes dos slots e as constantes do GL vivem em `gl_slots` (gerado). O
  // `using` fica DENTRO da funcao: o namespace do emulador nao fica poluido com
  // 240 nomes de constantes do GL.
  using namespace gl_slots;
  if (retorno != nullptr) *retorno = 0;
  ++chamadas_;
  ++por_slot_[slot];
  ChamadaGl c;
  c.slot = slot;
  c.nome = NomeDoSlotDoMotor(slot);
  c.lr = a.lr;
  c.n_args = 4;
  for (int i = 0; i < 4; ++i) c.args[i] = a.reg[i];

  // Assim que um argumento vem da PILHA, o `sp` tem de ser um endereco a serio.
  // Com `sp` a zero, `Arg` leria a memoria do endereco 0 e a recusa apareceria no
  // sitio errado.
  const auto esp = [&](std::size_t ate) -> bool {
    if (ate <= 4) return true;
    if (a.sp < 0x00010000u) return false;
    for (std::size_t i = 4; i < ate; ++i) c.args[i] = Arg(i, a);
    return true;
  };

  // TRES SAIDAS, E TODAS PASSAM PELO `Registar`. Um caminho que devolvesse sem
  // registar seria o defeito dos 71 handlers sem log, outra vez.
  const auto feito = [&](std::size_t usados) {
    c.n_args = usados;
    c.resultado = ResultadoGl::Feito;
    Registar(c);
    return ResultadoGl::Feito;
  };
  // Feito, mas com uma ressalva escrita no detalhe (por exemplo: o estado foi
  // acumulado e nenhum pixel foi escrito).
  const auto feito_com = [&](std::size_t usados, const std::string& motivo) {
    c.n_args = usados;
    c.motivo = motivo;
    c.resultado = ResultadoGl::Feito;
    Registar(c);
    return ResultadoGl::Feito;
  };
  const auto recusa = [&](const std::string& motivo) {
    Recusar(motivo, c);
    Registar(c);
    return ResultadoGl::Recusado;
  };
  // O metodo existe, o pedido foi compreendido e a geometria foi mesmo
  // submetida -- mas nao ha rasterizador. E uma RECUSA, e nao um sucesso.
  const auto recusa_com = [&](std::size_t usados, const std::string& motivo) {
    c.n_args = usados;
    Recusar(motivo, c);
    Registar(c);
    return ResultadoGl::Recusado;
  };
  const auto sem = [&]() {
    NaoTem(c);
    Registar(c);
    return ResultadoGl::NaoImplementado;
  };

  switch (slot) {
    // --- a cabeca: IBase + QueryInterface (AEEGL.h) -------------------------
    case kIgl_AddRef: {
      const std::uint32_t n = mem_.Ler32(a.reg[0] + 4) + 1;
      mem_.Escrever32(a.reg[0] + 4, n);
      if (retorno != nullptr) *retorno = n;
      return feito(1);
    }
    case kIgl_Release: {
      const std::uint32_t n = mem_.Ler32(a.reg[0] + 4);
      if (n == 0) return recusa("Release de um objecto com contagem zero");
      mem_.Escrever32(a.reg[0] + 4, n - 1);
      if (retorno != nullptr) *retorno = n - 1;
      return feito(1);
    }
    case kIgl_QueryInterface: {
      // `ISHELL`/`IGL` so se devolvem a si proprios: nao ha outra interface
      // dentro deste objecto. Recusar com o ponteiro a zero e a resposta certa.
      const std::uint32_t iid = a.reg[1], ppo = a.reg[2];
      if (ppo == 0) return recusa("ppObj nulo");
      if (iid == kClsidIgl) {
        mem_.Escrever32(ppo, objeto_);
        if (retorno != nullptr) *retorno = 0;  // SUCCESS
        return feito(3);
      }
      mem_.Escrever32(ppo, 0);
      if (retorno != nullptr) *retorno = 0xE0000001u;  // ECLASSNOTSUPPORT
      return recusa("IID nao servido por este objecto");
    }

    // --- matrizes -----------------------------------------------------------
    case kIgl_MatrixMode: {
      const std::uint32_t m = a.reg[0];
      if (m == GL_MODELVIEW) modo_ = kModoModelView;
      else if (m == GL_PROJECTION) modo_ = kModoProjection;
      else if (m == GL_TEXTURE) modo_ = kModoTexture;
      else return recusa("modo de matriz desconhecido");
      return feito(1);
    }
    case kIgl_LoadIdentity:
      Identidade(PilhaDoModo().m[PilhaDoModo().topo]);
      return feito(0);
    case kIgl_LoadMatrixx:
      LerMatriz(0, a, PilhaDoModo().m[PilhaDoModo().topo]);
      return feito(1);
    case kIgl_MultMatrixx: {
      float outra[16];
      LerMatriz(0, a, outra);
      Multiplicar(PilhaDoModo().m[PilhaDoModo().topo], PilhaDoModo().m[PilhaDoModo().topo], outra);
      return feito(1);
    }
    case kIgl_PushMatrix: {
      PilhaDeMatrizes& p = PilhaDoModo();
      if (p.topo + 1 >= p.fundo) return recusa("pilha de matrizes cheia");
      for (int i = 0; i < 16; ++i) p.m[p.topo + 1][i] = p.m[p.topo][i];
      ++p.topo;
      return feito(0);
    }
    case kIgl_PopMatrix: {
      PilhaDeMatrizes& p = PilhaDoModo();
      if (p.topo == 0) return recusa("pilha de matrizes vazia");
      --p.topo;
      return feito(0);
    }
    case kIgl_Translatex:
      Translacao(PilhaDoModo().m[PilhaDoModo().topo], Fixo(0, a), Fixo(1, a), Fixo(2, a));
      return feito(3);
    case kIgl_Scalex:
      Escala(PilhaDoModo().m[PilhaDoModo().topo], Fixo(0, a), Fixo(1, a), Fixo(2, a));
      return feito(3);
    case kIgl_Rotatex:
      Rotacao(PilhaDoModo().m[PilhaDoModo().topo], Fixo(0, a), Fixo(1, a), Fixo(2, a), Fixo(3, a));
      return feito(4);
    case kIgl_Frustumx: {
      if (!esp(6)) return recusa("argumentos na pilha sem sp valido");
      const float l = Fixo(0, a), r = Fixo(1, a), b = Fixo(2, a), t = Fixo(3, a);
      const float n = Fixo(4, a), f = Fixo(5, a);
      if (r - l == 0.0f || t - b == 0.0f || f - n == 0.0f || n <= 0.0f) {
        return recusa("frustum degenerado");
      }
      float m[16];
      for (int i = 0; i < 16; ++i) m[i] = 0.0f;
      m[0] = 2.0f * n / (r - l);       // (linha 0, coluna 0)
      m[4 * 1 + 1] = 2.0f * n / (t - b);
      m[4 * 2 + 0] = (r + l) / (r - l);
      m[4 * 2 + 1] = (t + b) / (t - b);
      m[4 * 2 + 2] = -(f + n) / (f - n);
      m[4 * 2 + 3] = -1.0f;
      m[4 * 3 + 2] = -(2.0f * f * n) / (f - n);
      Multiplicar(PilhaDoModo().m[PilhaDoModo().topo], PilhaDoModo().m[PilhaDoModo().topo], m);
      return feito(6);
    }
    case kIgl_Orthox: {
      if (!esp(6)) return recusa("argumentos na pilha sem sp valido");
      const float l = Fixo(0, a), r = Fixo(1, a), b = Fixo(2, a), t = Fixo(3, a);
      const float n = Fixo(4, a), f = Fixo(5, a);
      if (r - l == 0.0f || t - b == 0.0f || f - n == 0.0f) return recusa("ortho degenerado");
      float m[16];
      for (int i = 0; i < 16; ++i) m[i] = 0.0f;
      m[0] = 2.0f / (r - l);
      m[4 * 1 + 1] = 2.0f / (t - b);
      m[4 * 2 + 2] = -2.0f / (f - n);
      m[4 * 3 + 0] = -(r + l) / (r - l);
      m[4 * 3 + 1] = -(t + b) / (t - b);
      m[4 * 3 + 2] = -(f + n) / (f - n);
      m[15] = 1.0f;
      Multiplicar(PilhaDoModo().m[PilhaDoModo().topo], PilhaDoModo().m[PilhaDoModo().topo], m);
      return feito(6);
    }
    case kIgl_Orthof: {
      // O `Orthof` DO IGLES11 (slot 22 de `AEEGLES10.h`), que o IGL de
      // `AEEGL.h` NAO tem: la so existe o `glOrthox` de 16.16, logo este id e
      // interno do motor (ver `igl.h`). A CONTA E A MESMA do `kIgl_Orthox`
      // acima e o pedido e o mesmo -- o que muda e a entrada: seis `float`
      // (`AEEGLfloat`), e nao seis GLfixed. A escrita e na matriz CORRENTE,
      // como no `x`: o GL define `glOrtho` como `M := M * O` e o uso normal e o
      // PROJECTION, mas quem escolhe o modo e o titulo (`glMatrixMode`), e o
      // motor nao o muda por conta propria.
      if (!esp(6)) return recusa("argumentos na pilha sem sp valido");
      const float l = Real(0, a), r = Real(1, a), b2 = Real(2, a), t2 = Real(3, a);
      const float n = Real(4, a), f = Real(5, a);
      if (r - l == 0.0f || t2 - b2 == 0.0f || f - n == 0.0f) return recusa("ortho degenerado");
      float m[16];
      for (int i = 0; i < 16; ++i) m[i] = 0.0f;
      m[0] = 2.0f / (r - l);
      m[4 * 1 + 1] = 2.0f / (t2 - b2);
      m[4 * 2 + 2] = -2.0f / (f - n);
      m[4 * 3 + 0] = -(r + l) / (r - l);
      m[4 * 3 + 1] = -(t2 + b2) / (t2 - b2);
      m[4 * 3 + 2] = -(f + n) / (f - n);
      m[15] = 1.0f;
      Multiplicar(PilhaDoModo().m[PilhaDoModo().topo], PilhaDoModo().m[PilhaDoModo().topo], m);
      // OS SEIS VALORES VAO NO DETALHE, e nao e enfeite: e a unica forma de VER
      // no traco que a moldura de chamada do IGLES11 foi lida no sitio certo (os
      // tres primeiros nos registos r1..r3, os outros tres NA PILHA, depois do
      // `pMe`). Uma moldura errada daria uma matriz plausivel e um desenho
      // errado sem sintoma.
      char det[192];
      std::snprintf(det, sizeof(det),
                    "ortho(%g, %g, %g, %g, %g, %g) [l,r,b,t,n,f] aplicado a matriz corrente "
                    "(modo %u)",
                    l, r, b2, t2, n, f, modo_);
      return feito_com(6, det);
    }

    // --- cor e limpeza ------------------------------------------------------
    case kIgl_Color4x: {
      std::uint32_t rgba = 0;
      for (int k = 0; k < 4; ++k) {
        const float v = Apertar(Fixo(k, a), 0.0f, 1.0f);
        rgba |= static_cast<std::uint32_t>(v * 255.0f + 0.5f) << (8u * k);
      }
      cor_ = rgba;
      return feito(4);
    }
    case kIgl_Color4f: {
      // O MESMO METODO NA OUTRA ESCALA: o `glColor4f` do IGLES11
      // (`AEEGLES10.h:30`, `AEEGLES11.h:104`) leva QUATRO `AEEGLfloat`, e o IGL
      // de `AEEGL.h` NAO o tem -- so o `glColor4x` (`:71`). O id e INTERNO ao
      // motor (ver `igl.h`) e a leitura e o `Real`.
      //
      // O QUARTO ARGUMENTO VEM DA PILHA: a moldura leva `pMe` em r0, e o
      // `AtenderClasse` le [sp] para `av.reg[3]` nos slots que a
      // `SlotIglesTemQuartoNaPilha` lista -- este passou a estar la. Sem isso o
      // alfa seria o `r3` (o azul) e a cor sairia com o alfa errado.
      //
      // MEDIDO na corrida de base: 299 pedidos no `abd` e 299 no `torkandkral`,
      // uma por quadro, os dois titulos que ja desenham e que ficavam com UMA SO
      // COR (o branco por omissao do motor). O `Fixo` sobre os bits de um
      // `float` daria 16256.0 para 0.25 -- o `Apertar` levava-o a 1.0 e a cor
      // sairia saturada, sem nenhum aviso.
      std::uint32_t rgba = 0;
      for (int k = 0; k < 4; ++k) {
        const float v = Apertar(Real(static_cast<std::size_t>(k), a), 0.0f, 1.0f);
        rgba |= static_cast<std::uint32_t>(v * 255.0f + 0.5f) << (8u * k);
      }
      cor_ = rgba;
      // OS QUATRO VALORES VAO NO DETALHE: e a unica forma de VER no traco que a
      // cor pedida pelo titulo chegou inteira (os tres primeiros argumentos em
      // r1..r3 e o quarto na pilha). Um deslocamento errado daria uma cor
      // plausivel e nenhum sintoma.
      char det[160];
      std::snprintf(det, sizeof(det),
                    "glColor4f(%g, %g, %g, %g) -> RGBA8 0x%08x",
                    static_cast<double>(Real(0, a)), static_cast<double>(Real(1, a)),
                    static_cast<double>(Real(2, a)), static_cast<double>(Real(3, a)), rgba);
      return feito_com(4, det);
    }
    case kIgl_ColorMask: {
      std::uint32_t mascara = 0;
      for (int k = 0; k < 4; ++k) {
        if (a.reg[k] != 0) mascara |= 1u << k;
      }
      color_mask_ = mascara;
      return feito(4);
    }
    case kIgl_ClearColorx:
      cor_limpeza_ = 0;
      for (int k = 0; k < 4; ++k) {
        const float v = Apertar(Fixo(k, a), 0.0f, 1.0f);
        cor_limpeza_ |= static_cast<std::uint32_t>(v * 255.0f + 0.5f) << (8u * k);
      }
      return feito(4);
    case kIgl_ClearDepthx:
      // O VALOR VAI AO RASTERIZADOR, e nao para um mapa que ninguem le: o
      // `MontarEstado` leva-o em `EstadoDeRasterizacao::profundidade_de_limpeza`
      // e o `Rasterizador::Limpar` enche o buffer de profundidade com ele
      // (`rasterizador.cpp:322`). O pedido MEDIDO no traco dos nove titulos TTD
      // e `r1=0x0000ffff` = 65535/65536 = 1.0 em GLfixed (`IGLES11::ClearDepthx`
      // 12x) -- o mesmo 1.0 da omissao do GL, e por isso o efeito dele so se ve
      // num titulo que peca OUTRO valor.
      profundidade_limpeza_ = Apertar(Fixo(0, a), 0.0f, 1.0f);
      return feito(1);
    case kIgl_ClearStencil: {
      // ESTA ARVORE NAO TEM BUFFER DE STENCIL, E ISSO PASSA A ESTAR DITO. O
      // valor fica guardado (o estado existe, e um `glGetIntegerv` pode ter de o
      // devolver -- ver o `GL_STENCIL_CLEAR_VALUE` abaixo), e o traco leva um
      // PRESSUPOSTO com a razao, que e a mesma do config do EGL:
      //
      //   core/brew/egl.cpp:151  {EGL_STENCIL_SIZE, 0, "nao ha buffer de stencil"}
      //
      // A DIFERENCA QUE ISTO FAZ NAO E NO DESENHO (nao ha nada para limpar): e
      // na RECUSA. Antes deste caminho o slot nao estava no mapa e a resposta era
      // "IGLES11::ClearStencil r0=.. r1=.." -- um pedido sem nome e sem razao.
      // Era, no traco dos nove titulos, 5 recusas por corrida.
      stencil_limpeza_ = a.reg[0];
      traco_.RegistarPressuposto(
          Area::Video, "glClearStencil",
          "esta arvore NAO tem buffer de stencil (core/brew/egl.cpp:151, EGL_STENCIL_SIZE 0, "
          "'nao ha buffer de stencil'); o valor fica guardado e observavel, nada e limpo");
      char det[128];
      std::snprintf(det, sizeof(det),
                    "valor de limpeza de stencil 0x%08x guardado; esta arvore nao tem buffer de "
                    "stencil (EGL_STENCIL_SIZE 0) e nada foi limpo",
                    stencil_limpeza_);
      return feito_com(1, det);
    }
    case kIgl_Clear: {
      if ((a.reg[0] & (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)) == 0) {
        return recusa("mascara de Clear sem nenhum buffer conhecido");
      }
      // O BIT DO STENCIL E ACEITE E NAO LIMPA NADA, e isso e dito uma vez por
      // corrida (presupposto, com a razao): um sucesso silencioso sobre um bit
      // que nao existe seria o stub mudo.
      if ((a.reg[0] & GL_STENCIL_BUFFER_BIT) != 0) {
        traco_.RegistarPressuposto(
            Area::Video, "glClear(GL_STENCIL_BUFFER_BIT)",
            "esta arvore NAO tem buffer de stencil (core/brew/egl.cpp:151, EGL_STENCIL_SIZE 0); "
            "o bit e aceite na mascara e nenhum pixel de stencil e limpo");
      }
      mascara_limpeza_ = a.reg[0];
      ++limpezas_;
      // SEM TELA, O `glClear` RECUSA. A mascara e a cor FICAM acumuladas (o
      // estado existe e e observavel), mas "feito" sem um pixel escrito seria o
      // stub mudo que esta reescrita existe para nao repetir.
      if (!destino_.Pronto()) {
        return recusa_com(1,
                          "mascara e cor de limpeza acumuladas, mas o IGL NAO TEM TELA LIGADA "
                          "(Igl::DefinirTela) e nenhum pixel foi escrito");
      }
      const video::EstadoDeRasterizacao estado = MontarEstado();
      RegistarRessalvas(estado);
      std::string motivo;
      const std::uint64_t escritos = rasterizador_.Limpar(estado, &motivo);
      if (escritos == 0 && !motivo.empty()) return recusa_com(1, motivo);
      return feito_com(1, "glClear(" + NomeDaMascaraDeLimpeza(mascara_limpeza_) +
                              ") escreveu " + std::to_string(escritos) + " pixels na Tela");
    }

    // --- interruptores ------------------------------------------------------
    case kIgl_Enable:
    case kIgl_Disable: {
      const std::uint32_t cap = a.reg[0];
      if (NomeDaCapacidade(cap) == nullptr && NomeDoArray(cap) == nullptr) {
        return recusa("capacidade desconhecida (sem nome no cabecalho deste modulo)");
      }
      interruptores_[cap] = (slot == kIgl_Enable) ? 1u : 0u;
      // SO os arrays de cliente entram em `arrays_de_cliente_`. O
      // `glEnable(GL_TEXTURE_2D)` NAO liga o array de coordenadas: misturar os
      // dois faria um `glDrawArrays` passar a precondicao sem ter vertices.
      if (NomeDoArray(cap) != nullptr) arrays_de_cliente_[cap] = (slot == kIgl_Enable);
      return feito(1);
    }
    case kIgl_EnableClientState:
    case kIgl_DisableClientState: {
      const std::uint32_t alvo = a.reg[0];
      if (NomeDoArray(alvo) == nullptr) return recusa("array de cliente desconhecido");
      arrays_de_cliente_[alvo] = (slot == kIgl_EnableClientState);
      return feito(1);
    }

    // --- arrays de vertices -------------------------------------------------
    case kIgl_VertexPointer: {
      if (a.reg[0] < 2 || a.reg[0] > 4) return recusa("tamanho de vertice fora de 2..4");
      if (!TipoDeVerticeValido(a.reg[1], true)) return recusa("tipo de vertice nao suportado");
      if (a.reg[3] == 0) return recusa("ponteiro de vertices nulo");
      arrays_[GL_VERTEX_ARRAY] = {true, static_cast<int>(a.reg[0]), a.reg[1], a.reg[2], a.reg[3]};
      return feito(4);
    }
    case kIgl_ColorPointer: {
      if (a.reg[0] != 4) return recusa("o GL ES 1.x so aceita 4 componentes de cor");
      if (!TipoDeVerticeValido(a.reg[1], true)) return recusa("tipo de cor nao suportado");
      if (a.reg[3] == 0) return recusa("ponteiro de cores nulo");
      arrays_[GL_COLOR_ARRAY] = {true, 4, a.reg[1], a.reg[2], a.reg[3]};
      return feito(4);
    }
    case kIgl_NormalPointer: {
      if (!TipoDeVerticeValido(a.reg[0], true)) return recusa("tipo de normal nao suportado");
      if (a.reg[2] == 0) return recusa("ponteiro de normais nulo");
      arrays_[GL_NORMAL_ARRAY] = {true, 3, a.reg[0], a.reg[1], a.reg[2]};
      return feito(3);
    }
    case kIgl_TexCoordPointer: {
      if (a.reg[0] < 2 || a.reg[0] > 4) return recusa("tamanho de coordenada fora de 2..4");
      if (!TipoDeVerticeValido(a.reg[1], true)) return recusa("tipo de coordenada nao suportado");
      // O ponteiro nulo e valido quando o titulo desvincula o array ou usa buffers
      // (ex: ridgeracer passa pointer=NULL). Se o array for desenhado sem dados,
      // a recusa ocorre no DrawArrays/DrawElements quando o cliente esta habilitado.
      arrays_[GL_TEXTURE_COORD_ARRAY] = {a.reg[3] != 0, static_cast<int>(a.reg[0]), a.reg[1],
                                         a.reg[2], a.reg[3]};
      return feito(4);
    }

    // --- texturas -----------------------------------------------------------
    case kIgl_GenTextures: {
      const std::uint32_t n = a.reg[0], destino = a.reg[1];
      if (n == 0) return recusa("glGenTextures com zero texturas");
      if (n > 4096) return recusa("glGenTextures com um numero absurdo de texturas");
      if (destino == 0) return recusa("lista de saida nula");
      for (std::uint32_t k = 0; k < n; ++k) {
        const std::uint32_t id = static_cast<std::uint32_t>(++texturas_geradas_);
        mem_.Escrever32(destino + 4u * k, id);
      }
      return feito(2);
    }
    case kIgl_DeleteTextures: {
      const std::uint32_t n = a.reg[0], lista = a.reg[1];
      if (n == 0 || lista == 0) return recusa("glDeleteTextures sem lista");
      for (std::uint32_t k = 0; k < n; ++k) {
        const std::uint32_t id = mem_.Ler32(lista + 4u * k);
        texturas_.erase(id);
        if (textura_ligada_ == id) textura_ligada_ = 0;
      }
      return feito(2);
    }
    case kIgl_BindTexture: {
      if (a.reg[0] != GL_TEXTURE_2D) return recusa("alvo diferente de GL_TEXTURE_2D");
      textura_ligada_ = a.reg[1];
      return feito(2);
    }
    case kIgl_ActiveTexture:
    case kIgl_ClientActiveTexture: {
      // Uma so unidade de textura: o GL ES 1.x garante pelo menos uma, e e a
      // unica que este modulo acumula. `GL_TEXTURE0` vem gerado de `gles/gl.h`.
      //
      // O MOTIVO DIZ O VALOR PEDIDO. `GL_TEXTURE1`+ e MULTITEXTURE (mais de uma
      // unidade, `GL_MAX_TEXTURE_UNITS`), que no GL ES 1.x e EXTENSAO e nao
      // existe nesta arvore: um "so a unidade GL_TEXTURE0 existe aqui" obrigava
      // quem le o traco a ir buscar o numero a outro sitio para saber o que o
      // titulo pediu, e o valor e a unica forma de distinguir `glActiveTexture`
      // de uma chamada lida no sitio errado. A recusa continua a ser recusa: nada
      // muda de estado (P2, nunca "sucesso" sem efeito).
      if (a.reg[0] != GL_TEXTURE0) {
        char det[192];
        std::snprintf(det, sizeof(det),
                      "unidade 0x%08x: GL_TEXTURE1+ e multitexture (extensao) e esta arvore "
                      "tem UMA unidade de textura, GL_TEXTURE0 (0x%08x)",
                      a.reg[0], static_cast<std::uint32_t>(GL_TEXTURE0));
        return recusa(det);
      }
      if (slot == kIgl_ActiveTexture) textura_activa_ = a.reg[0];
      return feito(1);
    }
    case kIgl_TexParameterx: {
      if (a.reg[0] != GL_TEXTURE_2D) return recusa("alvo diferente de GL_TEXTURE_2D");
      parametros_[ChaveDeParametro(slot, a.reg[1])] = {a.reg[2]};
      return feito(3);
    }
    case kIgl_TexImage2D: {
      if (!esp(9)) return recusa("argumentos na pilha sem sp valido");
      // NOVE argumentos: a partir do quinto vem da pilha. `a.reg[4]` NAO existe
      // (o array tem quatro) e o compilador avisa com `-Warray-bounds` -- foi
      // assim que se apanhou um `alt` a ler o `sp` da struct.
      const std::uint32_t alvo = a.reg[0], formato = a.reg[2];
      const std::uint32_t larg = Arg(3, a), alt = Arg(4, a);
      if (alvo != GL_TEXTURE_2D) return recusa("alvo diferente de GL_TEXTURE_2D");
      if (larg == 0 || alt == 0) return recusa("textura com largura ou altura zero");
      if (larg > 4096 || alt > 4096) return recusa("textura maior do que o maximo suportado");
      EstadoDaTextura& t = texturas_[textura_ligada_];
      t.largura = larg;
      t.altura = alt;
      t.formato = formato;
      // OS DOIS CAMPOS QUE O RASTERIZADOR PRECISA PARA LER OS TEXELS: o formato
      // do pixel (6.o argumento) e o tipo do elemento (8.o). Sem eles o
      // rasterizador teria de adivinhar quantos bytes tem cada texel, e um palpite
      // errado daria uma imagem errada SEM sintoma.
      t.formato_do_pixel = c.args[6];
      t.tipo = c.args[7];
      t.ponteiro = c.args[8];
      ++t.uploade;
      // `pixels == 0` E RESERVA, e nao recusa: as dimensoes e o formato ficam
      // registados e os texels chegam depois pelo `glTexSubImage2D` (e o par que
      // o `ridgeracer` faz). Enquanto o ponteiro for 0, o `MontarEstado` di-lo
      // pelo nome (`textura_reservada_sem_texels`) e o desenho segue com a cor do
      // vertice -- nunca uma amostragem do endereco zero.
      return feito_com(9, t.ponteiro == 0
                              ? "reserva de textura (glTexImage2D com pixels NULL): dimensoes e "
                                "formato registados, sem texels ate um glTexSubImage2D"
                              : "os pixels ficam na memoria do guest e sao lidos na amostragem "
                                "(nao ha copia no acto do glTexImage2D)");
    }
    case kIgl_TexSubImage2D: {
      if (!esp(9)) return recusa("argumentos na pilha sem sp valido");
      if (textura_ligada_ == 0) return recusa("sem textura ligada");
      // Os argumentos sao (alvo, nivel, x, y, larg, alt, formato, tipo, pixels).
      // Guarda-se o ponteiro tal como veio: a amostragem le os texels dessa
      // memoria, como se a textura tivesse sido enviada inteira de uma vez. A
      // consequencia (um buffer reutilizado para outra textura) esta escrita no
      // topo de `core/video/rasterizador.h`.
      EstadoDaTextura& t = texturas_[textura_ligada_];
      t.formato_do_pixel = c.args[6];
      t.tipo = c.args[7];
      t.ponteiro = c.args[8];
      ++t.uploade;
      return feito_com(9, "os pixels ficam na memoria do guest e sao lidos na amostragem");
    }
    case kIgl_CompressedTexImage2D: {
      if (!esp(8)) return recusa("argumentos na pilha sem sp valido");
      if (a.reg[0] != GL_TEXTURE_2D) return recusa("alvo diferente de GL_TEXTURE_2D");
      // A ALTURA VEM DA PILHA: os oito argumentos nao cabem em quatro registos.
      // Ler `a.reg[4]` seria ler o campo seguinte da struct (o `sp`) -- e o
      // compilador avisou com `-Warray-bounds`, que e a razao de o `-Wall
      // -Wextra` estar ligado nesta arvore.
      const std::uint32_t largura = a.reg[3], altura = Arg(4, a);
      if (largura == 0 || altura == 0) return recusa("textura comprimida com dimensao zero");
      EstadoDaTextura& t = texturas_[textura_ligada_];
      t.largura = largura;
      t.altura = altura;
      t.formato = a.reg[2];
      t.comprimida = true;
      ++t.uploade;
      return feito_com(8, "formato comprimido registado, sem descodificacao nesta etapa");
    }
    case kIgl_CompressedTexSubImage2D:
    case kIgl_CopyTexImage2D:
    case kIgl_CopyTexSubImage2D:
      return sem();  // precisam do rasterizador (leem pixels de algum sitio)

    // --- estado de rasterizacao --------------------------------------------
    case kIgl_CullFace: {
      const std::uint32_t m = a.reg[0];
      if (m != GL_FRONT && m != GL_BACK && m != GL_FRONT_AND_BACK) {
        return recusa("modo de cull desconhecido");
      }
      cull_face_ = m;
      return feito(1);
    }
    case kIgl_FrontFace: {
      const std::uint32_t m = a.reg[0];
      if (m != GL_CW && m != GL_CCW) return recusa("orientacao de face desconhecida");
      front_face_ = m;
      return feito(1);
    }
    case kIgl_ShadeModel: {
      const std::uint32_t m = a.reg[0];
      if (m != GL_SMOOTH && m != GL_FLAT) return recusa("modo de sombreado desconhecido");
      shade_model_ = m;
      return feito(1);
    }
    case kIgl_DepthFunc: {
      const std::uint32_t f2 = a.reg[0];
      if (f2 < GL_NEVER || f2 > GL_ALWAYS) return recusa("funcao de profundidade desconhecida");
      depth_func_ = f2;
      return feito(1);
    }
    case kIgl_DepthMask:
      depth_mask_ = a.reg[0] != 0;
      return feito(1);
    case kIgl_Viewport:
      viewport_[0] = a.reg[0];
      viewport_[1] = a.reg[1];
      viewport_[2] = a.reg[2];
      viewport_[3] = a.reg[3];
      return feito(4);
    case kIgl_Scissor:
      // O RECTANGULO E APLICADO, e nao so guardado. Ate agora dizia-se "o
      // rasterizador nao aplica o scissor" -- e a justificacao que o `zeebx` do
      // Kaio derrubou por medicao (`c278514`, no PEGGLE): ignorar o recorte nao e
      // "desenhar a mais", e desenhar ERRADO quando o titulo conta com ele. O
      // Peggle desenha a folha de fontes inteira e aperta a tesoura em volta de uma
      // letra; sem a tesoura, cada letra punha o alfabeto todo no ecra.
      //
      // O `y` conta de BAIXO (a mesma convencao do `glViewport`); a conversao vive
      // no rasterizador, num sitio so.
      for (int k = 0; k < 4; ++k) scissor_[k] = a.reg[k];
      parametros_[ChaveDeParametro(slot, 0)] = {a.reg[0], a.reg[1], a.reg[2], a.reg[3]};
      return feito_com(4, "o rectangulo vai ao rasterizador (aperta a caixa do desenho)");
    case kIgl_LineWidthx: {
      if (Fixo(0, a) <= 0.0f) return recusa("largura de linha nao positiva");
      parametros_[ChaveDeParametro(slot, 0)] = {a.reg[0]};
      // O VALOR E APLICADO, e o texto di-lo por inteiro. O `feito(1)` de antes
      // prometia o que nao acontecia: o valor ficava guardado e nenhum rasterizador
      // o lia (o padrao P2 -- o pedido guardado num mapa que ninguem le e
      // indistinguivel de um pedido perdido). Agora o valor vai para o retrato do
      // estado (`MontarEstado`), onde a largura <= 1 desenha uma linha de um pixel
      // e a largura > 1 RECUSA o desenho com o nome.
      return feito_com(1,
                       "largura de linha guardada e aplicada: <= 1 desenha 1 px, > 1 RECUSA o "
                       "desenho (nao ha rasterizador de linha grossa nesta etapa)");
    }
    case kIgl_PointSizex: {
      if (Fixo(0, a) <= 0.0f) return recusa("tamanho de ponto nao positivo");
      parametros_[ChaveDeParametro(slot, 0)] = {a.reg[0]};
      return feito(1);
    }
    case kIgl_PixelStorei: {
      if (a.reg[0] != GL_UNPACK_ALIGNMENT) return recusa("pname de PixelStore desconhecido");
      if (a.reg[1] != 1 && a.reg[1] != 2 && a.reg[1] != 4 && a.reg[1] != 8) {
        return recusa("alinhamento de desempacotamento invalido");
      }
      parametros_[ChaveDeParametro(slot, a.reg[0])] = {a.reg[1]};
      return feito(2);
    }
    // O `glBlendFunc(fonte, destino)` e o `glAlphaFuncx(funcao, referencia)`.
    // Estes DOIS nao ficam so acumulados: o rasterizador tem caminho para os
    // dois (a mistura e o alpha test), e um pedido guardado num mapa que
    // ninguem le e indistinguivel de um pedido perdido.
    case kIgl_BlendFunc: {
      mistura_fonte_ = a.reg[0];
      mistura_destino_ = a.reg[1];
      return feito(2);
    }
    case kIgl_AlphaFuncx: {
      // A REFERENCIA E GLfixed (16.16) e entra em [0,1]: e a faixa em que o
      // alfa do fragmento vive, e era por comparar duas escalas diferentes que
      // o teste do alpha test passava verde com a guarda arrancada.
      funcao_de_alfa_ = a.reg[0];
      alfa_de_referencia_ = Apertar(Fixo(1, a), 0.0f, 1.0f);
      return feito(2);
    }
    case kIgl_AlphaFunc: {
      // O MESMO METODO NA OUTRA ESCALA: o `glAlphaFunc` do IGLES11
      // (`AEEGLES10.h:27`) leva `AEEGLclampf`, um `float` de 32 bits, e o IGL de
      // `AEEGL.h` NAO o tem -- so o `glAlphaFuncx` (`:63`). O id e INTERNO ao
      // motor (ver `igl.h`) e a leitura e o `Real`.
      //
      // MEDIDO no `rmp`: 299 chamadas com r2=0x3f000000 = 0.5f. O `Fixo` sobre
      // esses mesmos bits daria 7.6294e-06: o fragmento reprovava em TODAS as
      // comparacoes com o `>` e o desenho desaparecia (ou o alpha test nunca
      // descartava), sem nenhum aviso em nenhum dos dois casos.
      //
      // O ESTADO E O MESMO dos dois lados -- `funcao_de_alfa_` e
      // `alfa_de_referencia_` --, e e ele que o `MontarEstado` leva ao
      // rasterizador no desenho (`e.teste_de_alfa`, `e.funcao_de_alfa`,
      // `e.alfa_de_referencia`, o que a frente rast2 deixou pronto).
      funcao_de_alfa_ = a.reg[0];
      alfa_de_referencia_ = Apertar(Real(1, a), 0.0f, 1.0f);
      return feito(2);
    }
    case kIgl_DepthRangex: {
      // `void DepthRangex(GLfixed zNear, GLfixed zFar)` -- os dois valores vao ao
      // rasterizador. **ESTE RAMO FALTOU no commit `8a46233`**, que dizia que a
      // faixa era aplicada: os campos existiam, o consumo existia, e o pedido do
      // guest nao tinha por onde entrar (uma celula de edicao falhou a meio e o
      // ficheiro nao foi gravado). Um commit que diz o que nao fez e o defeito que
      // esta casa persegue -- fica registado assim.
      //
      // A FONTE e o `zeebx` do Kaio (`89dcd0f`): o Crash Nitro Kart ALTERNA faixas
      // para por o brilho do kart por cima do cenario.
      profundidade_perto_ = Apertar(Real(0, a), 0.0f, 1.0f);
      profundidade_longe_ = Apertar(Real(1, a), 0.0f, 1.0f);
      parametros_[ChaveDeParametro(slot, 0)] = {a.reg[0], a.reg[1]};
      return feito_com(2, "a faixa vai ao rasterizador (profundidade_perto/_longe)");
    }
    case kIgl_Hint:
    case kIgl_PolygonOffsetx:
    case kIgl_SampleCoveragex:
    case kIgl_LogicOp:
    case kIgl_StencilFunc:
    case kIgl_StencilMask:
    case kIgl_StencilOp:
      // So acumulam parametros: guardam-se TODOS, para nao haver um caminho que
      // "devolve sucesso e nao faz nada". O consumidor (o rasterizador) nao
      // existe, e o detalhe da chamada di-lo.
      parametros_[ChaveDeParametro(slot, 0)] = {a.reg[0], a.reg[1], a.reg[2], a.reg[3]};
      return feito_com(4, "parametro acumulado; sem rasterizador que o use");
    // --- O MODELO DE LUZ -----------------------------------------------------
    //
    // A DEMANDA, MEDIDA no traco dos nove titulos TTD (`ZB2_QUADROS=300
    // ZB2_EVT_START=1`, `/tmp/pesquisa/igl9-9trace.txt`): `IGLES11::LightModelxv`
    // 5x, sempre com `r1=0x00000b53` = `GL_LIGHT_MODEL_AMBIENT` (`gles_1_1/gl.h:362`)
    // e `r2` a apontar para QUATRO valores em GLfixed. E o termo CONSTANTE da
    // equacao de luz do GL ES 1.x
    //
    //     cor = emissao + ambiente_do_material * ambiente_da_cena + SOMA(luzes)
    //
    // -- o rasterizador desta arvore ja o soma (`rasterizador.cpp:569`, frente
    // iluminacao da frente rast2). O que faltava era o CAMINHO: o slot nao
    // estava no mapa `SlotIglesNoIgl`, e o pedido nunca chegava aqui; e ele
    // guardava as palavras cruas em `parametros_`, que so o `MontarEstado`
    // decodificava. Agora o pedido SERVE o termo constante do ambiente.
    case kIgl_LightModelx:
    case kIgl_LightModelxv: {
      const std::uint32_t pname = a.reg[0];
      if (pname == GL_LIGHT_MODEL_AMBIENT) {
        // A FORMA ESCALAR NAO TEM ESTE `pname`: o `glLightModelx(GLenum, GLfixed)`
        // do ES 1.1 so serve o `GL_LIGHT_MODEL_TWO_SIDE`; um ambiente de cena tem
        // QUATRO componentes, e ler so uma seria servir uma cor que o titulo nao
        // pediu.
        if (slot == kIgl_LightModelx) {
          return recusa(
              "glLightModelx(GL_LIGHT_MODEL_AMBIENT) com UM valor: o ambiente da cena tem "
              "quatro componentes (gles_1_1/gl.h:362) e a forma escalar so serve o "
              "GL_LIGHT_MODEL_TWO_SIDE");
        }
        const std::uint32_t p = a.reg[1];
        if (p == 0) return recusa("vector de parametros nulo");
        for (int k = 0; k < 4; ++k) {
          ambiente_da_cena_[k] =
              Apertar(RealDoFixo(mem_.Ler32(p + 4u * static_cast<std::uint32_t>(k))), 0.0f, 1.0f);
        }
        char det[192];
        std::snprintf(det, sizeof(det),
                      "glLightModelxv(GL_LIGHT_MODEL_AMBIENT) = (%g, %g, %g, %g); e o termo "
                      "constante da luz (rasterizador.cpp:569)",
                      static_cast<double>(ambiente_da_cena_[0]), static_cast<double>(ambiente_da_cena_[1]),
                      static_cast<double>(ambiente_da_cena_[2]), static_cast<double>(ambiente_da_cena_[3]));
        return feito_com(2, det);
      }
      if (pname == GL_LIGHT_MODEL_TWO_SIDE) {
        // O QUE NAO SE IMPLEMENTA, DITO PELO NOME. A iluminacao das DUAS faces
        // pede um material por face; o rasterizador desta arvore tem UM (a face
        // e ignorada, como no ES 1.x) e nao ha medida do que o vendor do Zeebo
        // faria com a face de tras. Aceitar e nao fazer nada seria o stub mudo.
        return recusa_com(
            2,
            "GL_LIGHT_MODEL_TWO_SIDE (gles_1_1/gl.h:363) nao implementado: a iluminacao desta "
            "arvore tem UM material (a face e ignorada, como no ES 1.x) e nao ha medida da face "
            "de tras");
      }
      char det[160];
      std::snprintf(det, sizeof(det),
                    "pname 0x%08x fora da tabela do modelo de luz deste modulo (o cabecalho do "
                    "SDK declara GL_LIGHT_MODEL_AMBIENT 0x0B53 e GL_LIGHT_MODEL_TWO_SIDE 0x0B52)",
                    pname);
      return recusa(det);
    }
    case kIgl_Fogx:
    case kIgl_Lightx:
    case kIgl_Materialx:
    case kIgl_TexEnvx:
      parametros_[ChaveDeParametro(slot, a.reg[0])] = {a.reg[1]};
      return feito_com(2, "parametro acumulado; sem rasterizador que o use");
    case kIgl_Fogxv:
    case kIgl_Lightxv:
    case kIgl_Materialxv:
    case kIgl_TexEnvxv: {
      const std::uint32_t pname = a.reg[0], p = a.reg[1];
      if (p == 0) return recusa("vector de parametros nulo");
      std::vector<std::uint32_t> valores;
      for (int k = 0; k < 4; ++k) valores.push_back(mem_.Ler32(p + 4u * k));
      parametros_[ChaveDeParametro(slot, pname)] = valores;
      return feito_com(2, "parametro acumulado (4 valores lidos do guest); sem rasterizador");
    }
    // --- luz e material na forma `fv` do IGLES11 -----------------------------
    //
    // OS DOIS UNICOS METODOS DESTA FAMILIA QUE O IGL DE `AEEGL.h` NAO TEM. O
    // pedido e medido: 6 `Lightfv` e 8 `Materialfv` em gof, pbc e rmp, sempre
    // com light=GL_LIGHT0 / face=GL_FRONT_AND_BACK e os `pname` de
    // AMBIENT/DIFFUSE/SPECULAR (+ SHININESS no material).
    case kIgl_Lightfv:
    case kIgl_Materialfv: {
      // A MOLDURA DE CHAMADA DO IGLES11: `iname *pMe` em r0, logo o alvo (a luz
      // ou a face) esta em r1, o `pname` em r2 e o vector de `params` em r3. Os
      // tres valores caem em `a.reg[0..2]` porque o `classes.cpp` desloca os
      // registos antes de chamar este motor.
      const bool e_luz = (slot == kIgl_Lightfv);
      const std::uint32_t alvo = a.reg[0];
      const std::uint32_t pname = a.reg[1];
      const std::uint32_t params = a.reg[2];
      if (e_luz && (alvo < GL_LIGHT0 || alvo >= GL_LIGHT0 + kLuzesDoGlEs)) {
        char det[128];
        std::snprintf(det, sizeof(det),
                      "indice de luz 0x%08x fora das %u luzes que o GL ES 1.x garante "
                      "(GL_MAX_LIGHTS, gl.h:223); o valor da maquina nao foi medido",
                      alvo, kLuzesDoGlEs);
        return recusa(det);
      }
      if (!e_luz && alvo != GL_FRONT && alvo != GL_BACK && alvo != GL_FRONT_AND_BACK) {
        return recusa("face desconhecida (nem GL_FRONT, nem GL_BACK, nem GL_FRONT_AND_BACK)");
      }
      const Pname* pn =
          e_luz ? AcharPname(kPnamesDeLuz, pname) : AcharPname(kPnamesDeMaterial, pname);
      if (pn == nullptr) {
        // NAO SE ADIVINHA O TANTO DE COMPONENTES de um `pname` desconhecido: ler
        // quatro valores seria ler memoria que o titulo nao escreveu. RECUSA com
        // o valor, que e a informacao que falta para o servir.
        char det[160];
        std::snprintf(det, sizeof(det),
                      "%s com pname 0x%08x, que nao esta na tabela deste modulo",
                      e_luz ? "glLightfv" : "glMaterialfv", pname);
        return recusa(det);
      }
      if (params == 0) return recusa("vector de parametros nulo");
      // O TANTO DE COMPONENTES E O DO `pname` (ver `kPnamesDeLuz`): quatro para
      // AMBIENT/DIFFUSE/SPECULAR, UM para o GL_SHININESS -- que e o caso MEDIDO
      // em gof e pbc, com o escalar em 0x8007feec e 0x8007fecc.
      std::vector<float> valores;
      valores.reserve(static_cast<std::size_t>(pn->componentes));
      for (int k = 0; k < pn->componentes; ++k) {
        valores.push_back(RealDaMemoria(params + 4u * static_cast<std::uint32_t>(k)));
      }
      (e_luz ? luzes_ : materiais_)[ChaveDoAlvo(alvo, pname)] = valores;
      if (e_luz && valores.size() >= 3) {
        // A POSICAO E A DIRECCAO DO HOLOFOTE SAO GUARDADAS EM COORDENADAS DE
        // OLHO, e nao na coordenada em que o titulo as escreveu: e a definicao
        // do `glLight` (a transformacao e feita pela modelview do instante da
        // chamada), e e a razao por que uma luz de cena fica parada enquanto o
        // carro anda. So estes dois `pname` a levam; os outros sao cores.
        if (pname == GL_POSITION || pname == GL_SPOT_DIRECTION) {
          const float w = (pname == GL_POSITION && valores.size() >= 4) ? valores[3] : 0.0f;
          const float v[4] = {valores[0], valores[1], valores[2], w};
          float em_olho[4];
          AplicarNaMatriz(mv_.m[mv_.topo], v, em_olho);
          luzes_em_olho_[ChaveDoAlvo(alvo, pname)] =
              std::vector<float>(em_olho, em_olho + 4);
        }
      }
      std::string texto;
      for (std::size_t k = 0; k < valores.size(); ++k) {
        char v[32];
        std::snprintf(v, sizeof(v), "%s%g", k == 0 ? "" : ", ",
                      static_cast<double>(valores[k]));
        texto += v;
      }
      char det[256];
      std::snprintf(det, sizeof(det),
                    "%s 0x%08x = (%s) acumulado como estado; o rasterizador NAO faz iluminacao "
                    "(core/video/rasterizador.h, ponto 4)",
                    pn->nome, alvo, texto.c_str());
      return feito_com(3, det);
    }
    case kIgl_Normal3x:
    case kIgl_MultiTexCoord4x: {
      const std::size_t quantos = (slot == kIgl_MultiTexCoord4x) ? 5 : 3;
      if (!esp(quantos)) return recusa("argumentos na pila sem sp valido");
      std::vector<std::uint32_t> valores;
      for (std::size_t k = 0; k < quantos; ++k) valores.push_back(Arg(k, a));
      parametros_[ChaveDeParametro(slot, 0)] = valores;
      return feito(quantos);
    }

    // --- desenho ------------------------------------------------------------
    case kIgl_DrawArrays: {
      const std::uint32_t modo = a.reg[0], primeiro = a.reg[1], quantos = a.reg[2];
      if (NomeDaPrimitiva(modo) == nullptr) return recusa("primitiva desconhecida");
      if (!ArrayDeClienteLigado(GL_VERTEX_ARRAY)) {
        return recusa("desenho sem array de vertices ligado");
      }
      if (Array(GL_VERTEX_ARRAY) == nullptr) {
        return recusa("array de vertices ligado mas nunca definido por glVertexPointer");
      }
      if (quantos == 0) return recusa("desenho com zero vertices");
      ++desenhos_;
      vertices_ += quantos;
      if (!destino_.Pronto()) {
        return recusa_com(3, "geometria submetida (" + std::to_string(quantos) +
                                 " vertices, primeiro " + std::to_string(primeiro) +
                                 "), mas o IGL NAO TEM TELA LIGADA (Igl::DefinirTela): "
                                 "nenhum pixel escrito");
      }
      const video::EstadoDeRasterizacao estado = MontarEstado();
      RegistarRessalvas(estado);
      video::PedidoDeDesenho pedido;
      pedido.primitiva = modo;
      pedido.primeiro = primeiro;
      pedido.quantos = quantos;
      std::string motivo;
      if (!rasterizador_.Desenhar(estado, pedido, &motivo)) return recusa_com(3, motivo);
      return feito_com(3, motivo);
    }
    case kIgl_DrawElements: {
      const std::uint32_t modo = a.reg[0], quantos = a.reg[1], tipo = a.reg[2], indices = a.reg[3];
      if (NomeDaPrimitiva(modo) == nullptr) return recusa("primitiva desconhecida");
      if (tipo != GL_UNSIGNED_BYTE && tipo != GL_UNSIGNED_SHORT) {
        return recusa("tipo de indice nao suportado");
      }
      if (indices == 0) return recusa("lista de indices nula");
      if (!ArrayDeClienteLigado(GL_VERTEX_ARRAY)) {
        return recusa("desenho sem array de vertices ligado");
      }
      if (quantos == 0) return recusa("desenho com zero indices");
      ++desenhos_;
      vertices_ += quantos;
      if (!destino_.Pronto()) {
        return recusa_com(4, "geometria submetida (" + std::to_string(quantos) +
                                 " indices), mas o IGL NAO TEM TELA LIGADA: nenhum pixel escrito");
      }
      const video::EstadoDeRasterizacao estado = MontarEstado();
      RegistarRessalvas(estado);
      video::PedidoDeDesenho pedido;
      pedido.primitiva = modo;
      pedido.quantos = quantos;
      pedido.por_indices = true;
      pedido.tipo_do_indice = tipo;
      pedido.endereco_dos_indices = indices;
      std::string motivo;
      if (!rasterizador_.Desenhar(estado, pedido, &motivo)) return recusa_com(4, motivo);
      return feito_com(4, motivo);
    }
    case kIgl_Finish:
    case kIgl_Flush:
      // O desenho deste rasterizador e IMEDIATO: `glDrawArrays` ja escreveu os
      // pixels quando volta. Nao ha fila de comandos para esvaziar.
      return feito_com(0, "nada esta pendente: o rasterizador desenha no proprio glDrawArrays");

    // --- consultas ----------------------------------------------------------
    case kIgl_GetError:
      // Devolve sempre GL_NO_ERROR, e di-lo: nao ha estado de erro implementado,
      // e INVENTAR um codigo seria pior do que o zero. As recusas nao ficam
      // invisiveis -- cada uma vai para o traco com o nome do metodo e para a
      // lista de faltas.
      if (retorno != nullptr) *retorno = GL_NO_ERROR;
      return feito_com(0, "sem estado de erro: as recusas estao no traco, com nome");
    case kIgl_GetIntegerv: {
      const std::uint32_t pname = a.reg[0], destino = a.reg[1];
      if (destino == 0) return recusa("destino de glGetIntegerv nulo");
      // CADA LINHA DESTA TABELA E UMA AFIRMACAO SOBRE A MAQUINA, e por isso cada
      // uma tem a fonte. O que nao se sabe responder RECUSA com o `pname` dito
      // pelo nome (P2) -- uma resposta inventada aqui nao da erro nenhum: da um
      // titulo a dimensionar texturas, ou um viewport, com numeros que a maquina
      // nunca teve.
      std::uint32_t valor = 0;
      if (pname == GL_MAX_MODELVIEW_STACK_DEPTH) {
        valor = kFundoModelView;  // a pilha do motor (igl.h: kFundoModelView)
      } else if (pname == GL_MAX_PROJECTION_STACK_DEPTH) {
        valor = kFundoProjection;
      } else if (pname == GL_MAX_TEXTURE_STACK_DEPTH) {
        valor = kFundoTexture;
      } else if (pname == GL_STENCIL_BITS) {
        // ZERO BITS DE STENCIL, e o valor nao e um palpite: e o MESMO que o
        // config do EGL declara (`core/brew/egl.cpp:151`, `EGL_STENCIL_SIZE 0
        // "nao ha buffer de stencil"`). Um titulo que pergunte quantos bits de
        // stencil existem recebe a verdade desta arvore, em vez de uma recusa
        // que o mandaria adivinhar.
        valor = 0;
      } else if (pname == GL_STENCIL_CLEAR_VALUE) {
        // O VALOR QUE O `glClearStencil` GUARDOU (`igl.h`, `stencil_limpeza_`).
        // O estado existe mesmo que o buffer nao: e a diferenca entre "nao ha
        // buffer" e "nao ha estado nenhum".
        valor = stencil_limpeza_;
      } else if (pname == GL_MAX_TEXTURE_SIZE) {
        // O UNICO `pname` QUE OS TITULOS PEDIAM no corpus original, e o numero
        // e o do console, com a fonte: `kTexturaMaxima` (`igl.h`).
        valor = kTexturaMaxima;
      } else if (pname == GL_MAX_TEXTURE_UNITS) {
        // Dragon Vs Chicken (ConfTest da SDK) consulta isto antes de carregar
        // QX. Este e estado do NOSSO motor, nao uma alegacao sobre GPU Zeebo:
        // ActiveTexture/ClientActiveTexture aceitam somente GL_TEXTURE0 e so
        // existe uma textura ligada/array de texcoords. Declarar 2 (o hardware
        // suporta duas) faria QX usar GL_TEXTURE1, que este motor recusa.
        valor = kUnidadesDeTextura;
      } else {
        char det[192];
        const char* nome = NomeDoPnameDeConsulta(pname);
        if (nome != nullptr) {
          std::snprintf(det, sizeof(det),
                        "pname 0x%08x (%s) nao servido: nao ha valor medido nesta maquina",
                        pname, nome);
        } else {
          std::snprintf(det, sizeof(det),
                        "pname 0x%08x nao servido: nao ha valor medido nesta maquina",
                        pname);
        }
        return recusa(det);
      }
      mem_.Escrever32(destino, valor);
      return feito_com(2, "valor do estado do motor escrito na memoria do guest");
    }
    case kIgl_GetString: {
      // `glGetString` descreve A IMPLEMENTACAO actual. Nao fingimos a Adreno do
      // aparelho: estas strings identificam o Curupira e a lista de extensoes e
      // vazia porque nao anunciamos uma funcao que o rasterizador nao tenha.
      // O retorno precisa permanecer legivel pelo guest apos a chamada, por isso
      // vive num trecho reservado do objecto IGL, abaixo do IEGL (0x800b1000).
      constexpr std::uint32_t kBaseDasStrings = kObjIgl + 0x200u;
      const char* texto = nullptr;
      std::uint32_t destino = 0;
      switch (a.reg[0]) {
        case GL_VENDOR:     texto = "Curupira";                     destino = kBaseDasStrings; break;
        case GL_RENDERER:   texto = "Curupira software rasterizer"; destino = kBaseDasStrings + 0x40u; break;
        case GL_VERSION:    texto = "OpenGL ES-CM 1.0";             destino = kBaseDasStrings + 0x80u; break;
        case GL_EXTENSIONS: texto = "";                             destino = kBaseDasStrings + 0xc0u; break;
        default: return recusa("nome de glGetString nao reconhecido");
      }
      for (std::uint32_t i = 0;; ++i) {
        const std::uint8_t byte = static_cast<std::uint8_t>(texto[i]);
        mem_.Escrever8(destino + i, byte);
        if (byte == 0) break;
      }
      if (retorno != nullptr) *retorno = destino;
      return feito_com(1, "string estatica da implementacao escrita no guest");
    }
    case kIgl_ReadPixels:
      // O framebuffer agora EXISTE (a Tela), mas a `core/brew/tela.h` nao expoe
      // nenhuma leitura de pixel: `Escritos()`, `CoresDistintas()` e
      // `CoresEm(x,y,w,h)` contam, e nao devolvem a cor de um pixel. Ler seria
      // preciso e ajuda a dizer onde a recusa esta.
      return recusa("a Tela nao tem leitura de pixel (tela.h nao expoe nenhum `Ler`)");

    default:
      return sem();
  }
}

}  // namespace zb2::brew
