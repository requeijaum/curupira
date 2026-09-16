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

// AS DUAS CAPACIDADES QUE O `.inc` GERADO NAO TEM, e a razao e medida.
//
// `tools/gl_slots.inc` e GERADO de `AEEGL.h` e do cabecalho `gles/gl.h`, e e
// conferido por `tools/verificar_slots_gl.sh`: uma linha escrita a mao la faz a
// guarda DIVERGIR do cabecalho. Estas duas nao sao servidas por esse gerador, e
// o valor e o do cabecalho do SDK, com a linha exacta:
//
//   BREW-4.0.2-SP19/sdk/inc/gles/gles_1_0/gl.h:180   GL_RESCALE_NORMAL 0x803A
//   BREW-4.0.2-SP19/sdk/inc/gles/gles_1_0/gl.h:178   GL_COLOR_MATERIAL 0x0B57
//
// SAO AS UNICAS DUAS SEM NOME, e isso e medido, nao deduzido: no corpus de 62
// titulos (`ZB2_TRACE=1`, capacidade contada por titulo) ha exactamente duas
// capacidades recusadas por nao terem nome -- `glEnable 0x803A` em gof, rmp e
// pbc, e `glDisable 0x0B57` em tekken2.
//
// A CONTRADICAO QUE ISTO CORRIGE. O relatorio da frente glbloco escreveu que o
// `glDisable` do tekken2 era `GL_STENCIL_TEST` com o valor 0x0B57. Nao e:
// 0x0B57 e GL_COLOR_MATERIAL, e GL_STENCIL_TEST vale 0x0B90 (gl.h:165). A troca
// nao e cosmetica -- o nome errado manda quem o ler procurar um buffer de
// stencil que ninguem pediu, quando o pedido e "a cor corrente alimenta o
// material", que e o mesmo assunto dos dois `fv` desta frente. Nenhum dos 62
// titulos medidos liga GL_STENCIL_TEST.
constexpr std::uint32_t GL_RESCALE_NORMAL = 0x803Au;
constexpr std::uint32_t GL_COLOR_MATERIAL = 0x0B57u;

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
// A AMBIENTE DA CENA. Nao esta no `.inc` gerado, e o valor e o do cabecalho:
//   gles_1_0/gl.h:255  GL_LIGHT_MODEL_AMBIENT 0x0B53
constexpr std::uint32_t GL_LIGHT_MODEL_AMBIENT = 0x0B53u;
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

// O NOME DE UM `pname` (o do cabecalho), ou nulo quando nao esta na tabela: e o
// que falta para a recusa poder dizer QUAL `pname` nao foi servido.
const char* NomeDoPname(std::uint32_t pname, bool e_luz) {
  const Pname* p = e_luz ? AcharPname(kPnamesDeLuz, pname) : AcharPname(kPnamesDeMaterial, pname);
  return p == nullptr ? nullptr : p->nome;
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
    // As duas MEDIDAS no corpus (ver o bloco acima, com a linha do cabecalho).
    {GL_RESCALE_NORMAL, "GL_RESCALE_NORMAL"},
    {GL_COLOR_MATERIAL, "GL_COLOR_MATERIAL"},
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
  if (InterruptorLigado(GL_TEXTURE_2D) && textura_ligada_ != 0) {
    const EstadoDaTextura* t = Textura(textura_ligada_);
    if (t != nullptr) {
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

  e.sombreado_plano = (shade_model_ == GL_FLAT);
  e.teste_de_profundidade = InterruptorLigado(GL_DEPTH_TEST);
  e.escrever_profundidade = depth_mask_;
  e.funcao_de_profundidade = depth_func_;
  e.descartar_faces = InterruptorLigado(GL_CULL_FACE);
  e.descartar_face = cull_face_;
  e.orientacao_da_frente = front_face_;

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
  // A AMBIENTE DA CENA (`glLightModelxv(GL_LIGHT_MODEL_AMBIENT, ...)`, valores
  // em GLfixed). Sem pedido fica a omissao do GL que o retrato ja tem.
  //
  // AS VARIANTES `x`/`xv` DE LUZ E MATERIAL (`glLightxv`, `glMaterialxv`) NAO
  // ALIMENTAM ESTE RETRATO, e isso fica dito: elas guardam palavras CRUAS do
  // guest num mapa diferente, e nenhum dos titulos medidos deste corpus as
  // chama (o traco de gof, rmp, pbc, pacmania e tekken2 nao tem uma linha
  // `glLightxv`/`glMaterialxv`). Implementa-las as cegas seria adivinhar a
  // escala dos valores.
  if (const std::vector<std::uint32_t>* v = Parametro(kIgl_LightModelxv, GL_LIGHT_MODEL_AMBIENT);
      v != nullptr) {
    for (std::size_t k = 0; k < v->size() && k < 4; ++k) {
      e.ambiente_da_cena[k] = RealDoFixo((*v)[k]);
    }
  }

  // O QUE OS TITULOS LIGARAM E O RASTERIZADOR NAO FAZ. Cada nome vai para o
  // traco (uma vez, no `RegistarRessalvas`), com o nome do que falta -- e nao em
  // silencio, que foi o que o `glCullFace` fez 86 377 vezes na arvore antiga.
  // A TABELA ENCOLHEU (frente rast2): `GL_BLEND`, `GL_ALPHA_TEST` e a mascara de
  // cor, e depois `GL_LIGHTING`, `GL_NORMALIZE`, `GL_RESCALE_NORMAL` e
  // `GL_COLOR_MATERIAL`, SAIRAM dela porque o rasterizador passou a faze-las.
  // Uma capacidade que deixa de faltar TEM de sair desta lista: uma linha a
  // dizer que falta, com o codigo a faze-la, e a mentira simetrica do stub mudo.
  const struct { std::uint32_t cap; const char* nome; } por_fazer[] = {
      {GL_FOG, "nevoa_de_GL_sem_rasterizador"},
      {GL_POLYGON_OFFSET_FILL, "polygon_offset_sem_rasterizador"},
      {GL_SCISSOR_TEST, "scissor_sem_rasterizador"},
      {GL_DITHER, "dithering_sem_rasterizador"},
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
      profundidade_limpeza_ = Apertar(Fixo(0, a), 0.0f, 1.0f);
      return feito(1);
    case kIgl_ClearStencil:
      // Guarda-se o valor. Nao ha buffer de stencil onde o aplicar, e o detalhe
      // di-lo -- mas o estado existe e e observavel, em vez de "sucesso e nada".
      parametros_[ChaveDeParametro(slot, 0)] = {a.reg[0]};
      return feito_com(1, "valor de limpeza de stencil guardado; sem buffer de stencil");
    case kIgl_Clear: {
      if ((a.reg[0] & (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)) == 0) {
        return recusa("mascara de Clear sem nenhum buffer conhecido");
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
      return feito_com(1, "glClear escreveu " + std::to_string(escritos) + " pixels na Tela");
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
      if (a.reg[3] == 0) return recusa("ponteiro de coordenadas nulo");
      arrays_[GL_TEXTURE_COORD_ARRAY] = {true, static_cast<int>(a.reg[0]), a.reg[1], a.reg[2],
                                         a.reg[3]};
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
      if (a.reg[0] != GL_TEXTURE0) return recusa("so a unidade GL_TEXTURE0 existe aqui");
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
      return feito_com(9, "os pixels ficam na memoria do guest e sao lidos na amostragem "
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
      // O valor fica guardado, e o rasterizador NAO o aplica (o `glClear` cobre a
      // tela toda e o desenho nao e limitado por ele). Dizer "feito" aqui e
      // aceitavel porque o ESTADO mudou mesmo; nao dizer a diferenca nao era.
      parametros_[ChaveDeParametro(slot, 0)] = {a.reg[0], a.reg[1], a.reg[2], a.reg[3]};
      return feito_com(4, "o rectangulo ficou guardado; o rasterizador nao aplica o scissor");
    case kIgl_LineWidthx: {
      if (Fixo(0, a) <= 0.0f) return recusa("largura de linha nao positiva");
      parametros_[ChaveDeParametro(slot, 0)] = {a.reg[0]};
      return feito(1);
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
    case kIgl_DepthRangex:
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
    case kIgl_Fogx:
    case kIgl_LightModelx:
    case kIgl_Lightx:
    case kIgl_Materialx:
    case kIgl_TexEnvx:
      parametros_[ChaveDeParametro(slot, a.reg[0])] = {a.reg[1]};
      return feito_com(2, "parametro acumulado; sem rasterizador que o use");
    case kIgl_Fogxv:
    case kIgl_LightModelxv:
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
      std::uint32_t valor = 0;
      if (pname == GL_MAX_MODELVIEW_STACK_DEPTH) valor = kFundoModelView;
      else if (pname == GL_MAX_PROJECTION_STACK_DEPTH) valor = kFundoProjection;
      else if (pname == GL_MAX_TEXTURE_STACK_DEPTH) valor = kFundoTexture;
      else if (pname == GL_MAX_TEXTURE_SIZE) {
        return recusa("a memoria de texturas nao existe nesta etapa");
      } else {
        return recusa("pname nao servido (sem medida do que a maquina responde)");
      }
      mem_.Escrever32(destino, valor);
      return feito(2);
    }
    case kIgl_GetString:
      // A STRING E UMA AFIRMACAO SOBRE O HARDWARE. Devolver "ATI" ou uma lista de
      // extensoes que nao foram medidas seria inventar -- e ha um caso medido na
      // arvore antiga (o `ddragonz` mete o resultado do `eglQueryString` num
      // `strstr` sem testar o nulo) que torna isto uma armadilha. Recusa-se.
      return recusa("sem medida do que a maquina responde (glGetString)");
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
