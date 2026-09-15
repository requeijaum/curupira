#ifndef ZB2_CORE_VIDEO_RASTERIZADOR_H
#define ZB2_CORE_VIDEO_RASTERIZADOR_H

// O RASTERIZADOR: o que transforma vertices em pixels. A parede que a bateria
// nomeia 124 vezes (etapa 3/6 do `docs/rewrite/PLAN.md`).
//
// PORQUE ESTE FICHEIRO EXISTE, e a medicao: o IGL tinha estado e o IEGL servia o
// config e as superficies, e `glDrawArrays`/`glDrawElements` RECUSAVAM com o
// numero de vertices no detalhe. Medido antes desta etapa:
//
//     ./build/zb2_bateria "$corpus" "$mods" /tmp/antes.json
//     == 62 titulos | PIXELS 0 em 62 de 62 | rasterizador_de_GL pedido 124x ==
//
// ESCRITA ONDE: na `core/brew/tela.h`, e NAO num buffer proprio. A medida
// `PIXELS`/`CORES` da bateria le exactamente essa tela (`tools/bateria.cpp:794`),
// e um rasterizador com buffer proprio daria um numero que nao mede o que o
// titulo escreveu na tela do emulador.
//
// O QUE ESTA ETAPA FAZ (e cada item tem teste com numeros escritos):
//   - `glClear` com a cor de limpeza;
//   - TRIANGLES / TRIANGLE_STRIP / TRIANGLE_FAN por varrimento de caixa
//     delimitadora com teste de aresta (funcoes de aresta e a regra
//     "top-left", que e o que impede dois triangulos vizinhos de escreverem o
//     mesmo pixel da aresta -- ou de deixarem uma fenda);
//   - interpolacao de cor (por vertice) e de coordenadas de textura;
//   - amostragem da textura ligada (GL_NEAREST, clamp);
//   - teste de profundidade, quando `GL_DEPTH_TEST` esta ligado;
//   - descarte de faces por orientacao (`glCullFace`/`glFrontFace`).
//
// O QUE FICOU DE FORA, ESCRITO AQUI PORQUE E A UNICA FORMA DE NAO SER LIDO COMO
// FEITO (P2). Cada linha nomeia o que nao existe, e nao "o que falta":
//
//   1. PERSPECTIVA: a interpolacao de textura e cor E CORRIGIDA POR PERSPECTIVA
//      (atributos divididos por w no rasterizador).
//   2. RECORTE DO PLANO PROXIMO: triangulos que cruzam o plano proximo
//      (clip.z + clip.w >= 0) sao recortados (Sutherland-Hodgman) gerando poligonos
//      de 3 ou 4 vertices com atributos interpolados. Triangulos totalmente atras sao
//      descartados (contados em `TriangulosDescartados()`). Os outros 5 planos do frustum
//      continuam tratados por descarte de caixa delimitadora no viewport.
//   3. SEM BLENDING (`GL_BLEND`), SEM ALPHA TEST, SEM STENCIL, SEM DITHER,
//      SEM POLYGON OFFSET, SEM SCISSOR, SEM COLOR MASK por canal: o pixel
//      escrito e a cor calculada, opaca. As capacidades que os titulos ligarem
//      vao para o traco, uma vez cada (`CapacidadesPorFazer()`), e nao em
//      silencio.
//   4. SEM ILUMINACAO e SEM NEVOA (`GL_LIGHTING`, `GL_FOG`): a cor vem do
//      `glColor4x` ou do array de cores, sem nenhuma contribuicao de luz.
//   5. SEM MIPMAPS e SEM FILTRAGEM BILINEAR: um texel por pixel, o do canto
//      inferior esquerdo da celula (`floor(u * largura)`), com CLAMP nas
//      bordas. O wrap `GL_REPEAT` nao existe: uma coordenada fora de [0,1] e
//      presa na borda, e isso fica registado no detalhe do desenho.
//   6. A TEXTURA NAO E COPIADA no `glTexImage2D`: o ponteiro aponta para a
//      MEMORIA DO GUEST, e a amostragem le de la a cada pixel. Um titulo que
//      reutilize o mesmo buffer para outra textura veria a textura errada. Ha
//      uma medicao parcial na arvore antiga (os quatro titulos que usam GL nao
//      correm ate ao desenho), e nenhuma medicao de reutilizacao de buffer:
//      fica dito, e nao inventado.
//   7. O MODO PLANO (`GL_FLAT`) usa a cor do ULTIMO vertice do triangulo. A
//      regra do "vertice provocador" das strips e fans nao foi implementada.
//   8. RGBA EM 8 BITS POR CANAL e convertido para RGB565 na ESCRITA (a tela e
//      RGB565: `tela.h:void CorAtual(std::uint32_t rgb565)`), e o alfa nao tem
//      onde ser guardado: `Alpha` e descartado.
//
// A SUPERFICIE. A escrita passa por uma interface pequena (`Superficie`) porque
// um teste tem de poder afirmar OS PIXELS EXACTOS -- coordenada e cor, um a um --
// e a `core/brew/tela.h` (ficheiro partilhado, com testes proprios) nao expoe
// nenhuma leitura de pixel. Em producao a implementacao e UMA (`DestinoTela`,
// que escreve mesmo na `Tela`); o `Superficie` NAO e um segundo framebuffer.

#include <cstdint>
#include <string>
#include <vector>

#include "core/brew/tela.h"
#include "core/memoria/memoria.h"

namespace zb2::video {

// --- a superficie ---------------------------------------------------------

class Superficie {
 public:
  virtual ~Superficie() = default;
  virtual int Largura() const = 0;
  virtual int Altura() const = 0;
  virtual void Escrever(int x, int y, std::uint32_t rgb565) = 0;
  // O `glClear` cobre a superficie TODA, e nao o clip corrente: no GL o
  // `glClear` nao e limitado pelo clip (`glScissor` e que o limita, e o scissor
  // nao esta implementado -- ver o ponto 3 acima).
  virtual void Limpar(std::uint32_t rgb565) = 0;
};

// A superficie de PRODUCAO: a `Tela` do motor.
class DestinoTela final : public Superficie {
 public:
  void ApontarPara(zb2::brew::Tela* tela) { tela_ = tela; }
  bool Pronto() const { return tela_ != nullptr; }
  // SEM TELA APONTADA A SUPERFICIE TEM TAMANHO ZERO, e nao 640x480. Nao e
  // estetica: e o que faz o `Rasterizador` RECUSAR em vez de ESCREVER num
  // ponteiro nulo -- a recusa e uma resposta, uma falha de segmentacao nao e.
  int Largura() const override { return tela_ == nullptr ? 0 : zb2::brew::Tela::kLargura; }
  int Altura() const override { return tela_ == nullptr ? 0 : zb2::brew::Tela::kAltura; }
  void Escrever(int x, int y, std::uint32_t rgb565) override;
  void Limpar(std::uint32_t rgb565) override;

 private:
  zb2::brew::Tela* tela_ = nullptr;
};

// --- a cor ----------------------------------------------------------------

// RGBA, 8 bits por canal, na ORDEM DOS BYTES em que o `igl.cpp` a guarda: o
// `glColor4x` e o `glClearColorx` escrevem `v << (8 * k)` para k = 0..3, logo o
// byte 0 e o vermelho. `0xFF0000FF` (hex) e vermelho OPACO, e nao azul.
struct Rgba {
  std::uint8_t r = 0, g = 0, b = 0, a = 0;
};

Rgba Desempacotar(std::uint32_t rgba);
std::uint32_t Para565(Rgba c);

// --- o estado que o rasterizador consome ----------------------------------

// Um array de vertices ligado por `glVertexPointer` e companhia. Os quatro
// campos sao os quatro argumentos do `gl*Pointer`, tal como o guest os mandou.
struct ArrayDoCliente {
  bool ligado = false;
  int tamanho = 0;
  std::uint32_t tipo = 0;
  std::uint32_t passo = 0;
  std::uint32_t ponteiro = 0;
};

// A textura ligada. O `ponteiro` e o nono argumento do `glTexImage2D`: OS PIXELS
// NA MEMORIA DO GUEST (ver o ponto 6 no topo do ficheiro).
struct Textura {
  bool existe = false;
  bool comprimida = false;
  std::uint32_t largura = 0, altura = 0;
  std::uint32_t formato = 0;   // o 6.o argumento: o formato do texel (GL_RGBA, GL_RGB, ...)
  std::uint32_t tipo = 0;      // o 8.o argumento: o tipo do elemento (GL_UNSIGNED_BYTE, ...)
  std::uint32_t ponteiro = 0;  // o 9.o argumento: os texels, no espaco do guest
};

// O estado do pipeline NO INSTANTE do desenho. E um retrato, e nao uma
// referencia ao `Igl`: o rasterizador nao precisa de conhecer o IGL para ser
// testavel em isolamento, e uma leitura a meio do desenho nao pode ver um estado
// que mude por baixo.
struct EstadoDeRasterizacao {
  float modelview[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  float projection[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  // O `glViewport(x, y, largura, altura)` do GL tem o canto INFERIOR esquerdo em
  // (x, y); aqui (x, y) e o canto SUPERIOR esquerdo, porque a `Tela` cresce para
  // baixo. Sem medicao da orientacao do framebuffer do Zeebo, esta e uma escolha
  // DECLARADA, e o teste `Rasterizador.VerticesConhecidosDaoPixelsExactos` fixa-a.
  std::uint32_t viewport[4] = {0, 0, 640, 480};

  Rgba cor = {255, 255, 255, 255};
  bool cor_por_vertice = false;
  // O `glClear`: a cor e a mascara de limpeza que o `igl.cpp` acumulou.
  Rgba cor_de_limpeza = {0, 0, 0, 0};
  std::uint32_t mascara_de_limpeza = 0;
  ArrayDoCliente vertices, cores, coordenadas_de_textura;
  bool textura_ligada = false;
  Textura textura;
  bool sombreado_plano = false;
  bool teste_de_profundidade = false;
  bool escrever_profundidade = true;
  std::uint32_t funcao_de_profundidade = 0x0201u;  // GL_LESS (gl_slots.inc)
  // O DESCARTE DE FACES so acontece com `GL_CULL_FACE` LIGADO: `glCullFace` e
  // so um parametro. O rasterizador nao adivinha -- quem sabe se o interruptor
  // esta ligado e o `InterruptorLigado(GL_CULL_FACE)` do IGL.
  bool descartar_faces = false;
  std::uint32_t descartar_face = 0x0405u;          // GL_BACK
  std::uint32_t orientacao_da_frente = 0x0901u;    // GL_CCW
  float profundidade_de_limpeza = 1.0f;

  // As capacidades que os titulos LIGARAM e que este rasterizador nao faz (o
  // ponto 3 do topo). Cada nome entra uma vez no traco: e o oposto do stub mudo
  // do `glCullFace`, que descartou 86 377 chamadas sem deixar rasto.
  std::vector<std::string> capacidades_por_fazer;
};

// O pedido de desenho, tal como chegou ao slot.
struct PedidoDeDesenho {
  std::uint32_t primitiva = 0;
  std::uint32_t primeiro = 0;
  std::uint32_t quantos = 0;
  bool por_indices = false;
  std::uint32_t tipo_do_indice = 0;
  std::uint32_t endereco_dos_indices = 0;
};

// --- o rasterizador --------------------------------------------------------

class Rasterizador {
 public:
  Rasterizador(Memoria& mem, Superficie& superficie);

  // Quantos pixels a limpeza escreveu. Devolve 0 e escreve o motivo em
  // `motivo` quando nao ha superficie onde escrever.
  std::uint64_t Limpar(const EstadoDeRasterizacao& estado, std::string* motivo);

  // `true` = a geometria foi rasterizada. `false` = RECUSADA, com o motivo
  // escrito: primitiva sem caminho, tipo de array sem caminho, textura
  // comprimida, sem superficie. **Nunca "devolve sucesso e nao desenha".**
  bool Desenhar(const EstadoDeRasterizacao& estado, const PedidoDeDesenho& pedido,
                std::string* motivo);

  // O estado que o rasterizador guardou, depois da limpeza: a profundidade.
  bool TemBufferDeProfundidade() const { return !profundidade_.empty(); }
  float ProfundidadeEm(int x, int y) const;

  // Contadores do PROPRIO rasterizador, para o traco e para os testes. Nao sao
  // a medida do titulo: essa e a `Tela` (`Escritos()`/`CoresDistintas()`).
  std::uint64_t Pixels() const { return pixels_; }
  std::uint64_t Triangulos() const { return triangulos_; }
  std::uint64_t TriangulosDescartados() const { return descartados_; }
  std::uint64_t TriangulosRecortados() const { return recortados_; }
  std::uint64_t PrimitivasRecusadas() const { return recusadas_; }

 private:
  struct Vertice {
    float clip[4] = {0, 0, 0, 1};  // depois de P * M * v
    float x = 0, y = 0, z = 0;     // em coordenadas de janela
    Rgba cor;
    float u = 0, v = 0;
  };

  bool LerVertice(const EstadoDeRasterizacao& e, std::uint32_t indice, Vertice* v,
                  std::string* motivo) const;
  bool Projetar(const EstadoDeRasterizacao& e, Vertice* v) const;
  void RasterizarTriangulo(const EstadoDeRasterizacao& e, const Vertice& a, const Vertice& b,
                           const Vertice& c);
  void EscreverPixel(const EstadoDeRasterizacao& e, int x, int y, float profundidade, Rgba cor);
  Rgba AmostrarTextura(const EstadoDeRasterizacao& e, float u, float v) const;
  void PrepararProfundidade();

  static Vertice InterpolarVertice(const Vertice& a, const Vertice& b, double t);
  int RecortarPlanoProximo(const Vertice* entrada, Vertice* saida) const;

  Memoria& mem_;
  Superficie& superficie_;
  std::vector<float> profundidade_;
  int largura_ = 0, altura_ = 0;
  std::uint64_t pixels_ = 0, triangulos_ = 0, descartados_ = 0, recortados_ = 0, recusadas_ = 0;
};

}  // namespace zb2::video

#endif  // ZB2_CORE_VIDEO_RASTERIZADOR_H
