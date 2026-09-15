// Testes do RASTERIZADOR: os primeiros pixels da reescrita.
//
// O QUE ESTES TESTES PROTEGEM, e porque existem:
//
// 1. A GEOMETRIA. Um erro de rasterizacao nao da erro nenhum: da uma imagem
//    errada, e a bateria so conta pixels. Por isso os numeros estao ESCRITOS
//    aqui: um triangulo com vertices conhecidos tem de dar EXACTAMENTE um
//    conjunto de pixels, um a um, com a cor de cada um.
//
// 2. A ARESTA PARTILHADA. Dois triangulos vizinhos que reclamem o mesmo pixel da
//    aresta escrevem-no DUAS vezes (a contagem `PIXELS` mente sobre o trabalho);
//    se nenhum o reclamar, fica uma fenda de um pixel em toda a costura. Ha um
//    teste para cada metade disto, com a contagem de escritas por pixel.
//
// 3. O CAMINHO DE PRODUCAO. A escrita nao pode ir para um buffer do
//    rasterizador: a medida `PIXELS`/`CORES` da bateria le a `core/brew/tela.h`
//    (`tools/bateria.cpp:794`). Ha testes que chamam `Igl::Executar` e leem a
//    `Tela` -- a mesma tela que a bateria le.
//
// 4. A RECUSA. Sem tela ligada (`Igl::DefinirTela`) o desenho e a limpeza
//    RECUSAM com o motivo escrito. "Devolve sucesso e nao desenha" e o stub mudo
//    que descartou 86 377 `glCullFace` na arvore antiga (P2).

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "core/brew/igl.h"
#include "core/brew/tela.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"
#include "core/video/rasterizador.h"
#include "tools/gl_slots.inc"

namespace zb2::video {
namespace {

using namespace gl_slots;

// ---------------------------------------------------------------------------
// A BANCADA
// ---------------------------------------------------------------------------

// A SUPERFICIE DO TESTE. Ela existe por uma razao so: a `core/brew/tela.h` (um
// ficheiro partilhado, com testes proprios) NAO expoe nenhuma leitura de pixel --
// so contagens. Sem um gravador no lugar dela, o teste da geometria diria "6
// pixels" e nao conseguiria dizer QUAIS. O gravador observa a escrita no mesmo
// ponto onde a `Tela` a recebe, e nao por dentro do rasterizador.
struct Gravador final : public Superficie {
  int largura = 640, altura = 480;
  std::map<std::pair<int, int>, std::uint32_t> pixels;
  std::map<std::pair<int, int>, int> escritas;  // quantas vezes CADA pixel foi escrito
  std::uint64_t total = 0;
  std::uint32_t cor_da_limpeza = 0;
  bool limpou = false;

  int Largura() const override { return largura; }
  int Altura() const override { return altura; }
  void Escrever(int x, int y, std::uint32_t c) override {
    pixels[{x, y}] = c;
    ++escritas[{x, y}];
    ++total;
  }
  void Limpar(std::uint32_t c) override {
    limpou = true;
    cor_da_limpeza = c;
  }

  bool SoEstePixel(int x, int y, std::uint32_t cor) const {
    const auto it = pixels.find({x, y});
    return it != pixels.end() && it->second == cor;
  }
  int Vezes(int x, int y) const {
    const auto it = escritas.find({x, y});
    return it == escritas.end() ? 0 : it->second;
  }
};

std::uint32_t BitsDe(float v) {
  std::uint32_t b = 0;
  std::memcpy(&b, &v, sizeof(b));
  return b;
}

void EscreverFloat(Memoria& mem, Endereco onde, float v) { mem.Escrever32(onde, BitsDe(v)); }

// OS VERTICES VAO PARA A MEMORIA DO GUEST como GL_FLOAT, 3 por vertice (passo
// 12): e a mesma forma que o `glVertexPointer(3, GL_FLOAT, 12, p)` do jogo pede.
void PorVertices(Memoria& mem, Endereco onde, const std::vector<std::pair<float, float>>& v) {
  for (std::size_t k = 0; k < v.size(); ++k) {
    const Endereco base = onde + static_cast<Endereco>(k * 12);
    EscreverFloat(mem, base, v[k].first);
    EscreverFloat(mem, base + 4, v[k].second);
    EscreverFloat(mem, base + 8, 0.0f);
  }
}

// OS VERTICES COM z, ao contrario do `PorVertices` (que escreve z = 0): uma
// projeccao de PERSPECTIVA precisa do z, porque e ele que faz o `w` divergir.
void PorVerticesZ(Memoria& mem, Endereco onde, const std::vector<std::array<float, 3>>& v) {
  for (std::size_t k = 0; k < v.size(); ++k) {
    const Endereco base = onde + static_cast<Endereco>(k * 12);
    EscreverFloat(mem, base, v[k][0]);
    EscreverFloat(mem, base + 4, v[k][1]);
    EscreverFloat(mem, base + 8, v[k][2]);
  }
}

// AS COORDENADAS DE TEXTURA: 2 GL_FLOAT por vertice, passo 8.
void PorUv(Memoria& mem, Endereco onde, const std::vector<std::pair<float, float>>& uv) {
  for (std::size_t k = 0; k < uv.size(); ++k) {
    EscreverFloat(mem, onde + static_cast<Endereco>(k * 8), uv[k].first);
    EscreverFloat(mem, onde + static_cast<Endereco>(k * 8) + 4, uv[k].second);
  }
}

// UMA TEXTURA DE UMA LINHA EM QUE O TEXEL i VALE i: o canal VERDE leva i * 4, e o
// RGB565 descarta os dois bits mais baixos dele (`(i*4) >> 2 == i` ate i = 63),
// pelo que a COR do pixel escrito DIZ QUAL TEXEL foi amostrado. Sem isto o teste
// precisaria de uma tabela paralela de indices, que podia divergir da amostragem
// -- e um teste que concorda consigo mesmo nao prova a amostragem.
void PorTexturaDeUmaLinha(Memoria& mem, Endereco onde, int texels) {
  for (int i = 0; i < texels; ++i) {
    const Endereco p = onde + static_cast<Endereco>(i * 4);
    mem.Escrever8(p, 0);
    mem.Escrever8(p + 1, static_cast<std::uint8_t>(i * 4));
    mem.Escrever8(p + 2, 0);
    mem.Escrever8(p + 3, 255);
  }
}

// A PROJECCAO DE PERSPECTIVA DOS TESTES DA SECCAO 6: um frustum simetrico com o
// plano proximo em n = 1, o afastado em f = 100, o campo de visao de 90 graus
// (`f = cot(45) = 1`) e aspecto 1. Column-major, como o `igl.cpp` a guarda:
//
//   clip.x = x   clip.y = y   clip.z = (f+n)/(n-f) * z + 2fn/(n-f)   clip.w = -z
//
// com (f+n)/(n-f) = 101/-99 e 2fn/(n-f) = 200/-99.
//
// O `clip.w = -z` E O QUE FAZ ESTE TESTE VALER: sem ele (uma projeccao
// ortografica, `w = 1` em todo o lado) a interpolacao afim e a corrigida por
// perspectiva dao O MESMO resultado, e um teste de perspectiva nao provaria nada.
void PorFrustum(EstadoDeRasterizacao* e) {
  for (int k = 0; k < 16; ++k) e->projection[k] = 0.0f;
  e->projection[0] = 1.0f;                // (linha 0, coluna 0)
  e->projection[5] = 1.0f;                // (linha 1, coluna 1)
  e->projection[10] = -101.0f / 99.0f;    // (linha 2, coluna 2)
  e->projection[14] = -200.0f / 99.0f;    // (linha 2, coluna 3)
  e->projection[11] = -1.0f;              // (linha 3, coluna 2): o `clip.w = -z`
}

// O estado por omissao dos testes: uma janela de 8x8 pixels (para os numeros
// caberem na mao), cor vermelha, sem textura, sem profundidade e SEM descarte de
// faces -- cada teste liga o que quer testar.
//
// A JANELA DE 8x8 E UMA ESCOLHA: com 640x480 o primeiro teste teria centenas de
// pixels para conferir a mao, e um teste que ninguem consegue ler nao protege
// nada.
EstadoDeRasterizacao EstadoBase() {
  EstadoDeRasterizacao e;
  e.viewport[0] = 0;
  e.viewport[1] = 0;
  e.viewport[2] = 8;
  e.viewport[3] = 8;
  e.cor = Rgba{255, 0, 0, 255};  // vermelho opaco -> RGB565 0xF800
  return e;
}

// ---------------------------------------------------------------------------
// 1. A GEOMETRIA: os pixels EXACTOS
// ---------------------------------------------------------------------------

TEST(Rasterizador, TrianguloConhecidoDaExactamenteSeisPixels) {
  Memoria mem(nullptr);
  Gravador g;
  Rasterizador r(mem, g);
  EstadoDeRasterizacao e = EstadoBase();

  // OS VERTICES, e a conta que os poe onde eu quero:
  //   janela x = (x_ndc + 1) * largura / 2   |   janela y = (1 - y_ndc) * altura / 2
  // com a janela em (0,0,8,8):
  //   A = (-1, 0.75) -> (0, 1)      B = (-1, 0) -> (0, 4)      C = (0, 0) -> (4, 4)
  // A AREA: Aresta(A,B,C) = (0-0)*(4-1) - (4-1)*(4-0) = -12 -> o rasterizador
  // inverte a ordem (area positiva) e fica com area = 12.
  constexpr Endereco kVertices = 0x00100000;
  PorVertices(mem, kVertices, {{-1.0f, 0.75f}, {-1.0f, 0.0f}, {0.0f, 0.0f}});
  e.vertices.ligado = true;
  e.vertices.tamanho = 3;
  e.vertices.tipo = GL_FLOAT;
  e.vertices.passo = 12;
  e.vertices.ponteiro = kVertices;

  PedidoDeDesenho p;
  p.primitiva = GL_TRIANGLES;
  p.quantos = 3;
  std::string motivo;
  ASSERT_TRUE(r.Desenhar(e, p, &motivo)) << motivo;

  // A CONTA DOS SEIS PIXELS, feita a mao (e confirmada por um calculo
  // independente): o interior do triangulo e `y >= 1 + 0.75x`, amostrado no
  // centro de cada pixel. Coluna x=0 (centro 0.5): limite 1.375 -> os centros
  // 1.5, 2.5 e 3.5 entram; coluna x=1 (centro 1.5): limite 2.125 -> 2.5 e 3.5;
  // coluna x=2 (centro 2.5): limite 2.875 -> 3.5; coluna x=3 (centro 3.5):
  // limite 3.625 -> nenhum. Nenhum centro cai EM CIMA de uma aresta, logo este
  // teste nao depende da regra do canto.
  const std::vector<std::pair<int, int>> esperados = {
      {0, 1}, {0, 2}, {1, 2}, {0, 3}, {1, 3}, {2, 3}};
  EXPECT_EQ(g.escritas.size(), esperados.size());
  EXPECT_EQ(g.total, 6u);
  for (const auto& px : esperados) {
    EXPECT_EQ(g.Vezes(px.first, px.second), 1) << "pixel " << px.first << "," << px.second;
    EXPECT_TRUE(g.SoEstePixel(px.first, px.second, 0xF800u))  // 255,0,0 em RGB565
        << "pixel " << px.first << "," << px.second;
  }
  // E NENHUM OUTRO: o rasto negativo e o que um "a mais" nao mostra.
  EXPECT_EQ(g.Vezes(3, 0), 0);
  EXPECT_EQ(g.Vezes(0, 0), 0);
  EXPECT_EQ(g.Vezes(3, 3), 0);
  EXPECT_EQ(r.Pixels(), 6u);
  EXPECT_EQ(r.Triangulos(), 1u);
}

TEST(Rasterizador, ArestaPartilhadaNaoEscreveDuasVezesNemDeixaFenda) {
  Memoria mem(nullptr);
  Gravador g;
  Rasterizador r(mem, g);
  EstadoDeRasterizacao e = EstadoBase();

  // O QUADRADO 0..4 x 0..4 DA JANELA, em DOIS triangulos que partilham a
  // diagonal (0,0)-(4,4). A diagonal passa pelo CENTRO de quatro pixels
  // ((0.5,0.5), (1.5,1.5), (2.5,2.5), (3.5,3.5)): sao exactamente esses que a
  // regra do canto decide.
  constexpr Endereco kVertices = 0x00100000;
  PorVertices(mem, kVertices, {{-1.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f},   // T1
                               {-1.0f, 1.0f}, {0.0f, 0.0f}, {-1.0f, 0.0f}});  // T2
  e.vertices.ligado = true;
  e.vertices.tamanho = 3;
  e.vertices.tipo = GL_FLOAT;
  e.vertices.passo = 12;
  e.vertices.ponteiro = kVertices;
  PedidoDeDesenho p;
  p.primitiva = GL_TRIANGLES;
  p.quantos = 6;
  std::string motivo;
  ASSERT_TRUE(r.Desenhar(e, p, &motivo)) << motivo;

  // 4 x 4 = 16 pixels, e NENHUM escrito duas vezes: a area total dizer 16 nao
  // chega (com a aresta reclamada pelos dois, o total seria 20 e a area
  // continuaria 16 -- o defeito apareceria so na contagem de `PIXELS`).
  EXPECT_EQ(g.escritas.size(), 16u);
  EXPECT_EQ(g.total, 16u);
  for (const auto& par : g.escritas) EXPECT_EQ(par.second, 1) << "pixel escrito duas vezes";
  // E NENHUMA FENDA: os 16 pixels do quadrado estao todos escritos.
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      EXPECT_EQ(g.Vezes(x, y), 1) << "fenda em " << x << "," << y;
    }
  }
}

TEST(Rasterizador, CorInterpoladaEntreVertices) {
  Memoria mem(nullptr);
  Gravador g;
  Rasterizador r(mem, g);
  EstadoDeRasterizacao e = EstadoBase();

  // O MESMO TRIANGULO DO PRIMEIRO TESTE, agora com COR POR VERTICE -- e as TRES
  // cores sao DIFERENTES, porque com duas iguais um erro nos pesos passava
  // despercebido (foi o que a violacao V3 de `tools/provar_guardas_raster.py`
  // mostrou na primeira versao deste teste: o teste ficou VERDE com os pesos
  // trocados). V0 a preto, V1 a azul, V2 a vermelho:
  //   V0 = A = (-1, 0.75) -> janela (0, 1)   V1 = B = (-1, 0) -> (0, 4)
  //   V2 = C = (0, 0)     -> janela (4, 4)
  // No pixel (0,1) os pesos baricentricos (as funcoes das arestas OPOSTAS,
  // calculadas a mao; a area e 12 depois da inversao da ordem por ser negativa)
  //   w(V0) = 10/12 = 0.8333 | w(V1) = 1.5/12 = 0.125 | w(V2) = 0.5/12 = 0.041666
  // Logo a cor e (0.125*255, 0, 0.041666*255) = (31.875, 0, 10.625) ->
  // (32, 0, 11) com o +0.5 da conversao, e em RGB565
  //   (32>>3)<<11 | (0>>2)<<5 | (11>>3) = 4<<11 | 1 = 0x2001.
  // Com os pesos trocados (a violacao) o azul de V1 entra com 0.8333 e o
  // resultado e (32, 0, 212) -> 0x201A: VERMELHO no teste.
  constexpr Endereco kVertices = 0x00100000, kCores = 0x00101000;
  PorVertices(mem, kVertices, {{-1.0f, 0.75f}, {-1.0f, 0.0f}, {0.0f, 0.0f}});
  const std::uint8_t cores[12] = {0, 0, 0, 255, 0, 0, 255, 255, 255, 0, 0, 255};
  for (std::size_t k = 0; k < 12; ++k) mem.Escrever8(kCores + static_cast<Endereco>(k), cores[k]);
  e.vertices = {true, 3, GL_FLOAT, 12, kVertices};
  e.cor_por_vertice = true;
  e.cores = {true, 4, GL_UNSIGNED_BYTE, 4, kCores};
  PedidoDeDesenho p;
  p.primitiva = GL_TRIANGLES;
  p.quantos = 3;
  std::string motivo;
  ASSERT_TRUE(r.Desenhar(e, p, &motivo)) << motivo;
  EXPECT_EQ(g.escritas.size(), 6u);
  EXPECT_TRUE(g.SoEstePixel(0, 1, 0x2001u));
  // O VERTICE C, no pixel que lhe pertence: o pixel (2,3) tem w(C) grande. O
  // canto inferior direito do triangulo toca o vertice C em (4,4), e o pixel
  // (2,3) tem centro (2.5,3.5): e o pixel mais proximo de C dos seis.
  const std::uint32_t cor_do_canto = g.pixels.at({2, 3});
  EXPECT_GT(static_cast<int>(cor_do_canto >> 11), 10);  // muito mais claro que 32,32,32
}

// ---------------------------------------------------------------------------
// 2. A TELA DE PRODUCAO: o caminho que a bateria mede
// ---------------------------------------------------------------------------

// `GLfixed` e 16.16 (`igl.cpp`, `Igl::Fixo`).
std::uint32_t Fixo(float v) {
  return static_cast<std::uint32_t>(static_cast<std::int32_t>(v * 65536.0f));
}

// A BANCADA COM O MOTOR A SERIO: o `Igl` de verdade, a `Tela` de verdade (a
// mesma que o `tools/bateria.cpp:794` le para publicar `PIXELS`/`CORES`), e o
// registador para as faltas.
struct Bancada {
  zb2::Tempo tempo;
  zb2::Traco traco;
  zb2::DestinoMemoria destino;
  zb2::Memoria mem;
  zb2::Saidas saidas;
  zb2::brew::Igl igl;
  zb2::brew::Tela tela;

  Bancada() : traco("teste-do-rasterizador", &tempo), mem(&traco), igl(mem, traco) {
    traco.JuntarDestino(&destino);
    mem.EscritorUnico("teste");
    saidas.base = 0xF0000000u;
    saidas.passo = 4;
    saidas.quantos = 100000;
    saidas.ativa = true;
  }

  zb2::brew::ResultadoGl Ch(std::uint32_t slot, std::uint32_t a0 = 0, std::uint32_t a1 = 0,
                            std::uint32_t a2 = 0, std::uint32_t a3 = 0) {
    zb2::brew::ArgumentosGl a;
    a.reg[0] = a0;
    a.reg[1] = a1;
    a.reg[2] = a2;
    a.reg[3] = a3;
    std::uint32_t retorno = 0;
    return igl.Executar(slot, a, &retorno);
  }

  // O TRIANGULO DO PRIMEIRO TESTE, montado pelos slots do IGL como um titulo
  // faria: o array de vertices, o estado de cliente e a cor.
  void PrepararTriangulo(Endereco vertices) {
    Ch(kIgl_Viewport, 0, 0, 8, 8);
    PorVertices(mem, vertices, {{-1.0f, 0.75f}, {-1.0f, 0.0f}, {0.0f, 0.0f}});
    EXPECT_EQ(Ch(kIgl_VertexPointer, 3, GL_FLOAT, 12, vertices), zb2::brew::ResultadoGl::Feito);
    EXPECT_EQ(Ch(kIgl_EnableClientState, GL_VERTEX_ARRAY), zb2::brew::ResultadoGl::Feito);
    EXPECT_EQ(Ch(kIgl_Color4x, Fixo(1.0f), Fixo(0.0f), Fixo(0.0f), Fixo(1.0f)),
              zb2::brew::ResultadoGl::Feito);
  }
};

TEST(Rasterizador, ODesenhoEscreveNaTelaQueABateriaLe) {
  Bancada b;
  b.igl.DefinirTela(&b.tela);
  constexpr Endereco kV = 0x00100000;
  b.PrepararTriangulo(kV);
  EXPECT_EQ(b.tela.Escritos(), 0u);
  EXPECT_EQ(b.Ch(kIgl_DrawArrays, GL_TRIANGLES, 0, 3), zb2::brew::ResultadoGl::Feito);
  // OS MESMOS SEIS PIXELS do teste da geometria, agora contados PELA TELA -- a
  // medida que o `tools/bateria.cpp` publica como `PIXELS`.
  EXPECT_EQ(b.tela.Escritos(), 6u);
  // Duas cores: o vermelho do desenho e o fundo preto que a `Tela` ja tinha.
  EXPECT_EQ(b.tela.CoresDistintas(), 2u);
  // A regiao do triangulo tem as duas cores; longe dele, so o fundo.
  EXPECT_EQ(b.tela.CoresEm(0, 1, 3, 3), 2u);
  EXPECT_EQ(b.tela.CoresEm(5, 5, 3, 3), 1u);
  // E O RASTERIZADOR CONTA O MESMO (dois contadores independentes).
  EXPECT_EQ(b.igl.RasterizadorRef().Pixels(), 6u);
  EXPECT_EQ(b.igl.RasterizadorRef().Triangulos(), 1u);
}

TEST(Rasterizador, OGlDrawElementsLeOsIndices) {
  Bancada b;
  b.igl.DefinirTela(&b.tela);
  constexpr Endereco kV = 0x00100000, kI = 0x00101000, kI8 = 0x00102000;
  b.PrepararTriangulo(kV);
  // Os indices 0,1,2 como GL_UNSIGNED_SHORT e como GL_UNSIGNED_BYTE: os dois
  // caminhos de leitura do `glDrawElements`.
  b.mem.Escrever16(kI, 0);
  b.mem.Escrever16(kI + 2, 1);
  b.mem.Escrever16(kI + 4, 2);
  b.mem.Escrever8(kI8, 0);
  b.mem.Escrever8(kI8 + 1, 1);
  b.mem.Escrever8(kI8 + 2, 2);
  EXPECT_EQ(b.Ch(kIgl_DrawElements, GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, kI),
            zb2::brew::ResultadoGl::Feito);
  EXPECT_EQ(b.tela.Escritos(), 6u);
  EXPECT_EQ(b.Ch(kIgl_DrawElements, GL_TRIANGLES, 3, GL_UNSIGNED_BYTE, kI8),
            zb2::brew::ResultadoGl::Feito);
  EXPECT_EQ(b.tela.Escritos(), 12u);  // o MESMO triangulo outra vez, desenhado duas vezes
  // Os indices fora do array de vertices nao sao um caso desta etapa: um indice e
  // um indice, e o `glVertexPointer` nao tem tamanho. Fica dito aqui, e nao
  // inventado um limite que o guest nao pode ver.
  EXPECT_EQ(b.mem.Ler16(kI + 2), 1u);
}

TEST(Rasterizador, OGlClearPintaATelaTodaEIgnoraOClip) {
  Bancada b;
  b.igl.DefinirTela(&b.tela);
  EXPECT_EQ(b.Ch(kIgl_ClearColorx, Fixo(1.0f), Fixo(0.0f), Fixo(0.0f), Fixo(1.0f)),
            zb2::brew::ResultadoGl::Feito);
  // UM CLIP DO IDisplay NAO LIMITA O `glClear` (quem o limita no GL e o
  // `glScissor`, que o rasterizador nao implementa -- e o `glScissor` registra
  // essa diferenca no detalhe da chamada).
  b.tela.Clip(100, 100, 10, 10);
  EXPECT_EQ(b.Ch(kIgl_Clear, GL_COLOR_BUFFER_BIT), zb2::brew::ResultadoGl::Feito);
  EXPECT_EQ(b.tela.Escritos(),
            static_cast<std::uint32_t>(zb2::brew::Tela::kLargura) *
                static_cast<std::uint32_t>(zb2::brew::Tela::kAltura));
  EXPECT_EQ(b.tela.CoresDistintas(), 1u);
  // E O CLIP FICA COMO ESTAVA: o `glClear` poe-o de lado e repoe-o.
  const std::uint32_t* clip = b.tela.ClipAtual();
  EXPECT_EQ(clip[0], 100u);
  EXPECT_EQ(clip[1], 100u);
  EXPECT_EQ(clip[2], 10u);
  EXPECT_EQ(clip[3], 10u);
}

TEST(Rasterizador, OClipDoIDisplayLimitaOglDrawArrays) {
  Bancada b;
  b.igl.DefinirTela(&b.tela);
  constexpr Endereco kV = 0x00100000;
  b.PrepararTriangulo(kV);
  // Os seis pixels do triangulo estao em (0,1) (0,2) (1,2) (0,3) (1,3) (2,3).
  // Com o clip em (0,2)-(2,4) sobram quatro deles.
  b.tela.Clip(0, 2, 2, 2);
  EXPECT_EQ(b.Ch(kIgl_DrawArrays, GL_TRIANGLES, 0, 3), zb2::brew::ResultadoGl::Feito);
  EXPECT_EQ(b.tela.Escritos(), 4u);
}

TEST(Rasterizador, AJanelaPequenaMudaAProjeccao) {
  Memoria mem(nullptr);
  Gravador g;
  Rasterizador r(mem, g);
  EstadoDeRasterizacao e = EstadoBase();
  // A JANELA E (0,0,8,2). O `glViewport` faz DUAS coisas: e o mapa afim de NDC
  // para a janela E o limite do que se desenha. Com a altura a 2, os vertices vao
  // para (0, 0.25), (0, 1) e (4, 1) -- o triangulo fica deitado nas duas
  // primeiras linhas -- e sobra UM pixel, o (0,0), cujo centro (0.5, 0.5) esta
  // dentro (0.5 >= 0.25 + 0.1875*0.5 = 0.34375). ESTE TESTE E O DO MAPA; o do
  // LIMITE e o seguinte.
  e.viewport[3] = 2;
  constexpr Endereco kV = 0x00100000;
  PorVertices(mem, kV, {{-1.0f, 0.75f}, {-1.0f, 0.0f}, {0.0f, 0.0f}});
  e.vertices = {true, 3, GL_FLOAT, 12, kV};
  PedidoDeDesenho p;
  p.primitiva = GL_TRIANGLES;
  p.quantos = 3;
  std::string motivo;
  ASSERT_TRUE(r.Desenhar(e, p, &motivo)) << motivo;
  EXPECT_EQ(g.escritas.size(), 1u);
  EXPECT_EQ(g.Vezes(0, 0), 1);
  EXPECT_EQ(g.Vezes(0, 1), 0);  // a linha 1 fica FORA da janela de altura 2
}

TEST(Rasterizador, AJanelaLimitaORasto) {
  Memoria mem(nullptr);
  Gravador g;
  Rasterizador r(mem, g);
  EstadoDeRasterizacao e = EstadoBase();
  // A GEOMETRIA SAI DA JANELA, de proposito. A janela e (0,0,8,8) e o triangulo
  // em NDC e (-1,1), (-1,-3), (3,-3): pela conta do `Projetar`
  //   janela x = (x_ndc + 1) * 8 / 2  |  janela y = (1 - y_ndc) * 8 / 2
  // isso da (0,0), (0,16) e (16,16) -- o dobro da janela nos dois eixos. O
  // interior e `y >= x` (o centro do triangulo e (5.33, 10.67)), e so contam os
  // centros com x < 8 e y < 8: sao 8+7+6+5+4+3+2+1 = 36 candidatos, MENOS os 8
  // que caem EM CIMA da hipotenusa `y = x` (o centro de um pixel esta em
  // (k+0.5, k+0.5), e essa aresta NAO e de canto -- ver a regra). 36 - 8 = 28.
  constexpr Endereco kV = 0x00100000;
  PorVertices(mem, kV, {{-1.0f, 1.0f}, {-1.0f, -3.0f}, {3.0f, -3.0f}});
  e.vertices = {true, 3, GL_FLOAT, 12, kV};
  PedidoDeDesenho p;
  p.primitiva = GL_TRIANGLES;
  p.quantos = 3;
  std::string motivo;
  ASSERT_TRUE(r.Desenhar(e, p, &motivo)) << motivo;
  EXPECT_EQ(g.escritas.size(), 28u);
  EXPECT_EQ(g.total, 28u);
  // E NENHUM PIXEL FORA DA JANELA. Sem o corte pela janela seriam 92: e o numero
  // que a violacao deliberada (V7 de `tools/provar_guardas_raster.py`) produz.
  EXPECT_EQ(g.Vezes(0, 8), 0);
  EXPECT_EQ(g.Vezes(7, 8), 0);
  EXPECT_EQ(g.Vezes(0, 15), 0);
  for (const auto& par : g.escritas) {
    EXPECT_LT(par.first.first, 8);
    EXPECT_LT(par.first.second, 8);
  }
}

// ---------------------------------------------------------------------------
// 3. A TEXTURA
// ---------------------------------------------------------------------------

TEST(Rasterizador, ATexturaAmostraOCantoCerto) {
  Memoria mem(nullptr);
  Gravador g;
  g.largura = 8;
  g.altura = 8;
  Rasterizador r(mem, g);
  EstadoDeRasterizacao e = EstadoBase();
  e.viewport[2] = 8;
  e.viewport[3] = 8;

  // A TEXTURA 2x2, em RGBA de 8 bits, NA MEMORIA DO GUEST (nao ha copia: o
  // `glTexImage2D` guarda o ponteiro). Cada texel tem uma cor distinta, para uma
  // troca de coordenada ser VISIVEL e nao "quase igual".
  constexpr Endereco kV = 0x00100000, kT = 0x00101000, kUV = 0x00102000;
  const std::uint8_t texels[16] = {255, 0,   0,   255,  // (0,0) vermelho
                                   0,   255, 0,   255,  // (1,0) verde
                                   0,   0,   255, 255,  // (0,1) azul
                                   255, 255, 255, 255}; // (1,1) branco
  for (std::size_t k = 0; k < 16; ++k) mem.Escrever8(kT + static_cast<Endereco>(k), texels[k]);
  // O QUADRADO QUE COBRE A JANELA TODA: (0,0)->(-1,1) ... e as coordenadas de
  // textura acompanham, para cada quarto da janela pedir um texel.
  PorVertices(mem, kV, {{-1.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, -1.0f},   // canto sup.esq.
                        {-1.0f, 1.0f}, {1.0f, -1.0f}, {-1.0f, -1.0f}});
  const std::vector<std::pair<float, float>> uv = {
      {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
  for (std::size_t k = 0; k < uv.size(); ++k) {
    EscreverFloat(mem, kUV + static_cast<Endereco>(k * 8), uv[k].first);
    EscreverFloat(mem, kUV + static_cast<Endereco>(k * 8) + 4, uv[k].second);
  }
  e.vertices = {true, 3, GL_FLOAT, 12, kV};
  e.textura_ligada = true;
  e.textura.existe = true;
  e.textura.largura = 2;
  e.textura.altura = 2;
  e.textura.formato = GL_RGBA;
  e.textura.tipo = GL_UNSIGNED_BYTE;
  e.textura.ponteiro = kT;
  e.coordenadas_de_textura = {true, 2, GL_FLOAT, 8, kUV};

  PedidoDeDesenho p;
  p.primitiva = GL_TRIANGLES;
  p.quantos = 6;
  std::string motivo;
  ASSERT_TRUE(r.Desenhar(e, p, &motivo)) << motivo;

  // A CONTA: a janela tem 8x8 pixels e cada texel ocupa um quarto dela, logo 16
  // pixels com cada cor. A amostragem e `floor(u * largura)`, com o CENTRO do
  // pixel: o pixel (0,0) tem centro (0.5,0.5) -> u = v = 0.0625 -> o texel (0,0).
  EXPECT_EQ(g.escritas.size(), 64u);
  EXPECT_TRUE(g.SoEstePixel(0, 0, 0xF800u)) << "canto superior esquerdo devia ser o texel (0,0)";
  EXPECT_TRUE(g.SoEstePixel(7, 0, 0x07E0u)) << "devia ser o texel (1,0), verde";
  EXPECT_TRUE(g.SoEstePixel(0, 7, 0x001Fu)) << "devia ser o texel (0,1), azul";
  EXPECT_TRUE(g.SoEstePixel(7, 7, 0xFFFFu)) << "devia ser o texel (1,1), branco";
  std::map<std::uint32_t, int> por_cor;
  for (const auto& par : g.pixels) ++por_cor[par.second];
  EXPECT_EQ(por_cor.size(), 4u);
  for (const auto& par : por_cor) EXPECT_EQ(par.second, 16) << "cor 0x" << std::hex << par.first;
}

TEST(Rasterizador, TexturaComFormatoSemCaminhoRecusaComONome) {
  Memoria mem(nullptr);
  Gravador g;
  Rasterizador r(mem, g);
  EstadoDeRasterizacao e = EstadoBase();
  constexpr Endereco kV = 0x00100000, kUV = 0x00102000;
  PorVertices(mem, kV, {{-1.0f, 1.0f}, {-1.0f, -1.0f}, {1.0f, -1.0f}});
  for (int k = 0; k < 3; ++k) {
    EscreverFloat(mem, kUV + static_cast<Endereco>(k * 8), 0.0f);
    EscreverFloat(mem, kUV + static_cast<Endereco>(k * 8) + 4, 0.0f);
  }
  e.vertices = {true, 3, GL_FLOAT, 12, kV};
  e.coordenadas_de_textura = {true, 2, GL_FLOAT, 8, kUV};
  e.textura_ligada = true;
  e.textura.existe = true;
  e.textura.largura = 4;
  e.textura.altura = 4;
  e.textura.formato = GL_LUMINANCE_ALPHA;  // sem caminho de amostragem
  e.textura.tipo = GL_UNSIGNED_BYTE;
  e.textura.ponteiro = kV;
  PedidoDeDesenho p;
  p.primitiva = GL_TRIANGLES;
  p.quantos = 3;
  std::string motivo;
  EXPECT_FALSE(r.Desenhar(e, p, &motivo));
  EXPECT_NE(motivo.find("sem caminho de amostragem"), std::string::npos) << motivo;
  EXPECT_EQ(g.total, 0u);
  EXPECT_EQ(r.PrimitivasRecusadas(), 1u);
}

// ---------------------------------------------------------------------------
// 4. A PROFUNDIDADE E O DESCARTE DE FACES
// ---------------------------------------------------------------------------

TEST(Rasterizador, OTesteDeProfundidadeDecideQuemFica) {
  Memoria mem(nullptr);
  Gravador g;
  Rasterizador r(mem, g);
  EstadoDeRasterizacao e = EstadoBase();
  e.teste_de_profundidade = true;
  e.funcao_de_profundidade = GL_LESS;
  constexpr Endereco kV = 0x00100000;
  // O z do NDC vai para a janela como (z+1)/2: z = +0.5 -> 0.75 e z = -0.9 -> 0.05.
  PorVertices(mem, kV, {{-1.0f, 0.75f}, {-1.0f, 0.0f}, {0.0f, 0.0f}});
  e.vertices = {true, 3, GL_FLOAT, 12, kV};
  // O MESMO TRIANGULO, com z por vertice: um LONGEPARA desenhar primeiro.
  constexpr Endereco kVL = 0x00102000;
  e.vertices.ponteiro = kVL;
  PedidoDeDesenho p;
  p.primitiva = GL_TRIANGLES;
  p.quantos = 3;
  std::string motivo;
  // 1. Um triangulo LONGE (z = 0.5), vermelho.
  const float z_long = 0.5f;
  for (int k = 0; k < 3; ++k) {
    EscreverFloat(mem, kVL + static_cast<Endereco>(k * 12), k == 0 ? -1.0f : (k == 1 ? -1.0f : 0.0f));
    EscreverFloat(mem, kVL + static_cast<Endereco>(k * 12) + 4,
                  k == 0 ? 0.75f : (k == 1 ? 0.0f : 0.0f));
    EscreverFloat(mem, kVL + static_cast<Endereco>(k * 12) + 8, z_long);
  }
  ASSERT_TRUE(r.Desenhar(e, p, &motivo)) << motivo;
  EXPECT_EQ(r.Pixels(), 6u);
  EXPECT_TRUE(g.SoEstePixel(0, 1, 0xF800u));
  // 2. O MESMO sitio, MAIS PERTO (z = -0.9), azul: passa o GL_LESS e sobrepoe-se.
  e.cor = Rgba{0, 0, 255, 255};  // azul -> 0x001F
  for (int k = 0; k < 3; ++k) {
    EscreverFloat(mem, kVL + static_cast<Endereco>(k * 12) + 8, -0.9f);
  }
  ASSERT_TRUE(r.Desenhar(e, p, &motivo)) << motivo;
  EXPECT_EQ(r.Pixels(), 12u);
  EXPECT_TRUE(g.SoEstePixel(0, 1, 0x001Fu));
  // 3. Outro ainda MAIS LONGE (z = 0.9): o teste REPROVA-O e nada muda.
  e.cor = Rgba{0, 255, 0, 255};
  for (int k = 0; k < 3; ++k) {
    EscreverFloat(mem, kVL + static_cast<Endereco>(k * 12) + 8, 0.9f);
  }
  ASSERT_TRUE(r.Desenhar(e, p, &motivo)) << motivo;
  EXPECT_EQ(g.Vezes(0, 1), 2) << "o pixel mais proximo foi reescrito por um mais longe";
  EXPECT_TRUE(g.SoEstePixel(0, 1, 0x001Fu));
}

TEST(Rasterizador, SemGLDepthTestNaoHaTesteNemEscritaDeProfundidade) {
  Memoria mem(nullptr);
  Gravador g;
  Rasterizador r(mem, g);
  EstadoDeRasterizacao e = EstadoBase();  // teste_de_profundidade = false
  constexpr Endereco kV = 0x00100000;
  PorVertices(mem, kV, {{-1.0f, 0.75f}, {-1.0f, 0.0f}, {0.0f, 0.0f}});
  e.vertices = {true, 3, GL_FLOAT, 12, kV};
  PedidoDeDesenho p;
  p.primitiva = GL_TRIANGLES;
  p.quantos = 3;
  std::string motivo;
  ASSERT_TRUE(r.Desenhar(e, p, &motivo)) << motivo;
  // O buffer NAO foi criado: sem `GL_DEPTH_TEST` o rasterizador nao paga 640x480
  // floats por um teste que o jogo nao pediu.
  EXPECT_FALSE(r.TemBufferDeProfundidade());
}

TEST(Rasterizador, ODescarteDeFacesUsaAOrientacaoEmNDC) {
  Memoria mem(nullptr);
  Gravador g;
  Rasterizador r(mem, g);
  EstadoDeRasterizacao e = EstadoBase();
  e.descartar_faces = true;
  e.descartar_face = GL_BACK;
  e.orientacao_da_frente = GL_CCW;
  constexpr Endereco kV = 0x00100000;
  // A ORDEM (A, B, C) tem area POSITIVA em NDC -> face da frente (GL_CCW) e fica.
  PorVertices(mem, kV, {{-1.0f, 0.75f}, {-1.0f, 0.0f}, {0.0f, 0.0f}});
  e.vertices = {true, 3, GL_FLOAT, 12, kV};
  PedidoDeDesenho p;
  p.primitiva = GL_TRIANGLES;
  p.quantos = 3;
  std::string motivo;
  ASSERT_TRUE(r.Desenhar(e, p, &motivo)) << motivo;
  EXPECT_EQ(g.total, 6u);
  EXPECT_EQ(r.TriangulosDescartados(), 0u);
  // A ORDEM INVERSA tem area negativa -> face de tras -> DESCARTADA.
  PorVertices(mem, kV, {{0.0f, 0.0f}, {-1.0f, 0.0f}, {-1.0f, 0.75f}});
  const std::size_t antes = g.escritas.size();
  ASSERT_TRUE(r.Desenhar(e, p, &motivo)) << motivo;
  EXPECT_EQ(g.escritas.size(), antes);
  EXPECT_EQ(r.TriangulosDescartados(), 1u);
  // E COM O DESCARTE DESLIGADO o mesmo triangulo e desenhado: e o
  // `GL_CULL_FACE` que decide, e nao o `glCullFace` sozinho.
  e.descartar_faces = false;
  ASSERT_TRUE(r.Desenhar(e, p, &motivo)) << motivo;
  EXPECT_EQ(g.total, 12u);
}

// ---------------------------------------------------------------------------
// 5. A RECUSA (P2): nada de "sucesso e nenhum pixel"
// ---------------------------------------------------------------------------

TEST(Rasterizador, SemTelaODesenhoERecusadoEComOMotivoEscrito) {
  Bancada b;  // SEM `DefinirTela` -- que e o estado em que a arvore fica se o
              // despacho nao ligar a tela ao IGL.
  constexpr Endereco kV = 0x00100000;
  b.PrepararTriangulo(kV);
  EXPECT_FALSE(b.igl.TemTela());
  EXPECT_EQ(b.Ch(kIgl_DrawArrays, GL_TRIANGLES, 0, 3), zb2::brew::ResultadoGl::Recusado);
  EXPECT_EQ(b.tela.Escritos(), 0u);
  EXPECT_EQ(b.Ch(kIgl_Clear, GL_COLOR_BUFFER_BIT), zb2::brew::ResultadoGl::Recusado);
  // O motivo tem de ficar REGISTADO com o nome do metodo: uma recusa sem nome
  // foi o defeito dos 71 handlers de GL sem log.
  EXPECT_EQ(b.traco.ContagemFaltas().count("glDrawArrays"), 1u);
  EXPECT_EQ(b.traco.ContagemFaltas().count("glClear"), 1u);
  bool achou = false;
  for (const auto& ev : b.destino.eventos) {
    if (ev.nome == "GL_glDrawArrays" && ev.detalhe.find("TELA LIGADA") != std::string::npos) {
      achou = true;
    }
  }
  EXPECT_TRUE(achou) << "a recusa nao ficou no traco, com o motivo";
}

TEST(Rasterizador, PrimitivaSemCaminhoRecusaComONome) {
  Memoria mem(nullptr);
  Gravador g;
  Rasterizador r(mem, g);
  EstadoDeRasterizacao e = EstadoBase();
  constexpr Endereco kV = 0x00100000;
  PorVertices(mem, kV, {{-1.0f, 1.0f}, {-1.0f, -1.0f}, {1.0f, -1.0f}});
  e.vertices = {true, 3, GL_FLOAT, 12, kV};
  PedidoDeDesenho p;
  p.primitiva = GL_LINE_LOOP;
  p.quantos = 3;
  std::string motivo;
  EXPECT_FALSE(r.Desenhar(e, p, &motivo));
  EXPECT_NE(motivo.find("GL_LINE_LOOP"), std::string::npos) << motivo;
  EXPECT_EQ(g.total, 0u);
  EXPECT_EQ(r.PrimitivasRecusadas(), 1u);
}

TEST(Rasterizador, CapacidadeLigadaSemCaminhoEntraNasFaltasUmaVezSo) {
  Bancada b;
  b.igl.DefinirTela(&b.tela);
  constexpr Endereco kV = 0x00100000;
  b.PrepararTriangulo(kV);
  EXPECT_EQ(b.Ch(kIgl_Enable, GL_BLEND), zb2::brew::ResultadoGl::Feito);
  EXPECT_EQ(b.Ch(kIgl_BlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA),
            zb2::brew::ResultadoGl::Feito);
  EXPECT_EQ(b.Ch(kIgl_DrawArrays, GL_TRIANGLES, 0, 3), zb2::brew::ResultadoGl::Feito);
  EXPECT_EQ(b.Ch(kIgl_DrawArrays, GL_TRIANGLES, 0, 3), zb2::brew::ResultadoGl::Feito);
  // UMA VEZ, e nao uma por desenho: um titulo de 60 quadros escreveria a mesma
  // linha 60 vezes. Mas UMA vez, e nao zero: o blending nao existe, e isso tem
  // de aparecer.
  EXPECT_EQ(b.traco.ContagemFaltas().at("blending_de_GL_sem_rasterizador"), 1u);
  // E o desenho ACONTECE na mesma: a cor sai opaca no lugar de nada.
  EXPECT_EQ(b.tela.Escritos(), 12u);
}

// ---------------------------------------------------------------------------
// 6. A PERSPECTIVA NA INTERPOLACAO (w = -z)
// ---------------------------------------------------------------------------

TEST(Rasterizador, ATexturaEInterpoladaComPerspectiva) {
  Memoria mem(nullptr);
  Gravador g;
  Rasterizador r(mem, g);
  EstadoDeRasterizacao e = EstadoBase();  // janela (0,0,8,8), vermelho opaco
  PorFrustum(&e);

  // OS TRES VERTICES, e a conta que os poe na janela (w = -z):
  //   janela x = (x/w + 1) * 8 / 2   |   janela y = (1 - y/w) * 8 / 2
  //   A = (0, 0,    -2  ) -> w = 2   -> (4, 4)
  //   B = (4, 0,    -4  ) -> w = 4   -> (8, 4)
  //   C = (0, 2.4, -2.4) -> w = 2.4 -> (4, 0)
  // Os w DIVERGEM (2, 4 e 2.4), e e isso que separa a interpolacao afim da
  // corrigida por perspectiva.
  constexpr Endereco kV = 0x00100000, kUV = 0x00102000, kT = 0x00103000;
  PorVerticesZ(mem, kV, {{0.0f, 0.0f, -2.0f}, {4.0f, 0.0f, -4.0f}, {0.0f, 2.4f, -2.4f}});
  // A textura de 37 texels (o verde do texel i vale i * 4, logo a cor escrita diz
  // o INDICE amostrado: 37 * 4 = 148 < 256 e (i*4) >> 2 == i).
  PorTexturaDeUmaLinha(mem, kT, 37);
  PorUv(mem, kUV, {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}});
  e.vertices = {true, 3, GL_FLOAT, 12, kV};
  e.textura_ligada = true;
  e.textura.existe = true;
  e.textura.largura = 37;
  e.textura.altura = 1;
  e.textura.formato = GL_RGBA;
  e.textura.tipo = GL_UNSIGNED_BYTE;
  e.textura.ponteiro = kT;
  e.coordenadas_de_textura = {true, 2, GL_FLOAT, 8, kUV};
  PedidoDeDesenho p;
  p.primitiva = GL_TRIANGLES;
  p.quantos = 3;
  std::string motivo;
  ASSERT_TRUE(r.Desenhar(e, p, &motivo)) << motivo;

  // OS SEIS PIXELS, calculados com correcao de perspectiva:
  //   (4,1) -> texel 2 (u=0.0750)
  //   (4,2) -> texel 2 (u=0.0714)
  //   (5,2) -> texel 9 (u=0.2500)
  //   (4,3) -> texel 2 (u=0.0682)
  //   (5,3) -> texel 8 (u=0.2368)
  //   (6,3) -> texel 17 (u=0.4687)
  const std::map<std::pair<int, int>, std::uint32_t> esperado = {
      {{4, 1}, 2u << 5}, {{4, 2}, 2u << 5}, {{4, 3}, 2u << 5},
      {{5, 2}, 9u << 5}, {{5, 3}, 8u << 5}, {{6, 3}, 17u << 5}};
  EXPECT_EQ(g.escritas.size(), esperado.size());
  EXPECT_EQ(g.total, esperado.size());
  for (const auto& par : esperado) {
    EXPECT_TRUE(g.SoEstePixel(par.first.first, par.first.second, par.second))
        << "pixel " << par.first.first << "," << par.first.second;
    EXPECT_EQ(g.Vezes(par.first.first, par.first.second), 1);
  }

  // E NENHUM OUTRO: os centros em cima da diagonal ficam de fora pela regra de aresta.
  EXPECT_EQ(g.Vezes(4, 0), 0);
  EXPECT_EQ(g.Vezes(5, 1), 0);
  EXPECT_EQ(g.Vezes(7, 3), 0);
  EXPECT_EQ(g.Vezes(0, 0), 0);

  // A PROVA DE QUE O TESTE CONSEGUE FALHAR:
  // A interpolacao AFIM pura (sem dividir por w) daria texels 4, 4, 13, 4, 13, 23.
  // Todos os 6 pixels seriam DIFERENTES dos valores esperados!
  EXPECT_NE(g.pixels.at({6, 3}), static_cast<std::uint32_t>(23u << 5));
  EXPECT_NE(g.pixels.at({5, 2}), static_cast<std::uint32_t>(13u << 5));
  EXPECT_NE(g.pixels.at({5, 3}), static_cast<std::uint32_t>(13u << 5));
}

TEST(Rasterizador, OTrianguloQueAtravessaOPlanoProximoERecortadoEmVezDeDesaparecer) {
  Memoria mem(nullptr);
  Gravador g;
  Rasterizador r(mem, g);
  EstadoDeRasterizacao e = EstadoBase();
  PorFrustum(&e);

  // A E B EM FRENTE, C ATRAS DA CAMARA:
  //   A = (-2, 0, -2) -> w = 2 -> janela (0, 4)
  //   B = ( 2, 0, -2) -> w = 2 -> janela (8, 4)
  //   C = ( 0, 4,  2) -> w = -2 < 0 (atras da camera), e clip.z + clip.w < 0.
  //
  // ANTES DESTA ETAPA: o triangulo INTEIRO era descartado (0 pixels).
  // COM O RECORTE DO PLANO PROXIMO: o poligono recortado tem 4 vertices e cobre
  // a metade superior da janela (32 pixels em 8x8), sem descartar.
  constexpr Endereco kV = 0x00100000;
  PorVerticesZ(mem, kV, {{-2.0f, 0.0f, -2.0f}, {2.0f, 0.0f, -2.0f}, {0.0f, 4.0f, 2.0f}});
  e.vertices = {true, 3, GL_FLOAT, 12, kV};
  PedidoDeDesenho p;
  p.primitiva = GL_TRIANGLES;
  p.quantos = 3;
  std::string motivo;
  ASSERT_TRUE(r.Desenhar(e, p, &motivo)) << motivo;

  // A metade superior da janela de 8x8 pixels: 32 pixels (y de 0 a 3, x de 0 a 7).
  EXPECT_EQ(g.escritas.size(), 32u);
  EXPECT_EQ(g.total, 32u);
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 8; ++x) {
      EXPECT_TRUE(g.SoEstePixel(x, y, 0xF800u)) << "pixel " << x << "," << y;
      EXPECT_EQ(g.Vezes(x, y), 1);
    }
  }

  // Estatisticas do recorte:
  EXPECT_EQ(r.TriangulosRecortados(), 1u);
  EXPECT_EQ(r.TriangulosDescartados(), 0u);
  EXPECT_EQ(r.Triangulos(), 2u);  // 4 vertices divididos em 2 triangulos
  EXPECT_NE(motivo.find("recortados"), std::string::npos) << motivo;
}

TEST(Rasterizador, OTrianguloTotalmenteAtrasDoPlanoProximoEDescartado) {
  Memoria mem(nullptr);
  Gravador g;
  Rasterizador r(mem, g);
  EstadoDeRasterizacao e = EstadoBase();
  PorFrustum(&e);

  // Tres vertices atras da camera (z > 0, logo clip.z + clip.w < 0)
  constexpr Endereco kV = 0x00100000;
  PorVerticesZ(mem, kV, {{0.0f, 0.0f, 2.0f}, {1.0f, 0.0f, 3.0f}, {0.0f, 1.0f, 4.0f}});
  e.vertices = {true, 3, GL_FLOAT, 12, kV};
  PedidoDeDesenho p;
  p.primitiva = GL_TRIANGLES;
  p.quantos = 3;
  std::string motivo;
  ASSERT_TRUE(r.Desenhar(e, p, &motivo)) << motivo;

  EXPECT_EQ(g.escritas.size(), 0u);
  EXPECT_EQ(r.Pixels(), 0u);
  EXPECT_EQ(r.TriangulosRecortados(), 0u);
  EXPECT_EQ(r.TriangulosDescartados(), 1u);
}


}  // namespace
}  // namespace zb2::video
