#include "core/video/rasterizador.h"

#include <algorithm>
#include <cmath>
#include <cinttypes>
#include <cstdio>
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
  superficie_.Escrever(x, y, Para565(cor));
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
        cor.a = 255;
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
  if (!triangulos) {
    ++recusadas_;
    // P2: o caminho que nao existe RECUSA e REGISTA, com o NOME da primitiva. Um
    // "devolve sucesso e nao desenha" e o defeito do `glCullFace`, que descartou
    // 86 377 chamadas sem deixar rasto.
    *motivo = std::string("primitiva ") + NomeDaPrimitiva(modo) +
              " sem rasterizador nesta etapa (so TRIANGLES, TRIANGLE_STRIP e TRIANGLE_FAN)";
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
  const std::uint64_t descartados_antes = descartados_;
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
  for (Vertice& v : vertices) {
    if (!Projetar(estado, &v)) {
      // UM VERTICE ATRAS DA CAMARA INVALIDA O TRIANGULO EM QUE ENTRA, e nao o
      // desenho todo: o recorte de frustum nao existe (ponto 2 do cabecalho), e
      // descartar so aquele triangulo e a versao honesta do que se faz aqui.
      v.clip[3] = -1.0f;
    }
  }

  const std::size_t n = vertices.size();
  const auto triangulo = [&](std::size_t i, std::size_t j, std::size_t k) {
    if (vertices[i].clip[3] <= 0.0f || vertices[j].clip[3] <= 0.0f || vertices[k].clip[3] <= 0.0f) {
      ++descartados_;
      return;
    }
    RasterizarTriangulo(estado, vertices[i], vertices[j], vertices[k]);
  };
  if (modo == GL_TRIANGLES) {
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
  std::snprintf(detalhe, sizeof(detalhe),
                "%u vertices -> %" PRIu64 " pixels em %" PRIu64 " triangulos (%" PRIu64
                " descartados)",
                pedido.quantos, escritos, triangulos_ - triangulos_antes,
                descartados_ - descartados_antes);
  *motivo = detalhe;
  return true;
}

}  // namespace zb2::video
