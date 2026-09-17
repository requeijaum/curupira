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
  // A LEITURA DO DESTINO. Devolve o que foi escrito (e zero, preto, onde nada
  // foi): e o que a mistura e a mascara de cor leem. Sem ela nao haveria como
  // afirmar o pixel MISTURADO, e um teste do `GL_DST_COLOR` sobre um destino
  // preto nao distingue "leu o destino" de "nao leu nada".
  std::uint32_t Ler(int x, int y) const override {
    const auto it = pixels.find({x, y});
    return it == pixels.end() ? 0u : it->second;
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
  // A JANELA E O `Gravador` (640x480) e o `glViewport` CONTA O Y DE BAIXO PARA
  // CIMA: uma janela de 8x8 no canto de baixo esquerdo e `y = 480 - 8`. Estava
  // `y = 0`, que e a MESMA coisa enquanto se le o y de cima -- a convencao errada
  // que o `zeebx` do Kaio corrigiu (`89dcd0f`, vista no Crash Nitro Kart: o
  // trecho do portal saia espelhado). O que este ficheiro MEDE (os pixeis exactos)
  // nao muda: muda so onde o retangulo da janela esta.
  e.viewport[0] = 0;
  e.viewport[1] = 480 - 8;
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
  // faria: o array de vertices, o estado de cliente e a cor. O alfa e um
  // argumento com omissao OPACA porque e ele que o alpha test e a mistura leem:
  // um titulo que o mude tem de o poder dizer por este mesmo caminho.
  void PrepararTriangulo(Endereco vertices, float alfa = 1.0f) {
    // A JANELA NO TOPO, PELA CONVENCAO DO GL: o `y` conta de BAIXO, logo um
    // retangulo de 8x8 no canto de cima e `y = 480 - 8`. Estava `y = 0`, que e o
    // que os dois rasterizadores do `zeebx` faziam antes do `89dcd0f`.
    Ch(kIgl_Viewport, 0, 480 - 8, 8, 8);
    PorVertices(mem, vertices, {{-1.0f, 0.75f}, {-1.0f, 0.0f}, {0.0f, 0.0f}});
    EXPECT_EQ(Ch(kIgl_VertexPointer, 3, GL_FLOAT, 12, vertices), zb2::brew::ResultadoGl::Feito);
    EXPECT_EQ(Ch(kIgl_EnableClientState, GL_VERTEX_ARRAY), zb2::brew::ResultadoGl::Feito);
    EXPECT_EQ(Ch(kIgl_Color4x, Fixo(1.0f), Fixo(0.0f), Fixo(0.0f), Fixo(alfa)),
              zb2::brew::ResultadoGl::Feito);
  }

  // As faltas registadas no traco. Uma capacidade que passa a ser feita TEM de
  // sair da lista: "nao ha falta" e a outra metade do teste, e a metade que
  // apanha a tabela `por_fazer` a mentir.
  std::size_t Faltas(const std::string& nome) const {
    const auto& f = traco.ContagemFaltas();
    const auto it = f.find(nome);
    return it == f.end() ? 0 : static_cast<std::size_t>(it->second);
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
  e.viewport[1] = 480 - 2;  // o `y` do GL conta de baixo (ver o `EstadoBase`)
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
  // A SUPERFICIE DESTE TESTE E 8x8 (o `Gravador` acima), e nao 640x480: a janela
  // do `EstadoBase` e o retangulo do GL contado de baixo (`480 - 8`), e num ecra de
  // 8 linhas isso cai FORA. Numa superficie de 8x8 a janela `y = 0` cobre-a toda.
  e.viewport[1] = 0;
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
  // A PRIMITIVA DESTE TESTE PASSOU A SER O `GL_POINTS`: o `GL_LINE_LOOP` era a
  // primitiva sem caminho ate a frente das linhas, e depois dela passa a ser
  // DESENHADA (o teste do caminho das linhas e o
  // `Rasterizador.OLoopVoltaAoPrimeiroPontoSemEscreverDuasVezes`). Um teste que
  // continuasse a pedir a recusa de um `GL_LINE_LOOP` passaria a provar o
  // contrario do que diz. O `GL_POINTS` continua sem rasterizador: nenhum titulo
  // do corpus o pede (medido: e o unico motivo de recusa de primitiva e o
  // `GL_LINE_STRIP`), e nao se inventa um caso de uso para ele.
  p.primitiva = GL_POINTS;
  p.quantos = 3;
  std::string motivo;
  EXPECT_FALSE(r.Desenhar(e, p, &motivo));
  EXPECT_NE(motivo.find("GL_POINTS"), std::string::npos) << motivo;
  EXPECT_EQ(g.total, 0u);
  EXPECT_EQ(r.PrimitivasRecusadas(), 1u);
}

TEST(Rasterizador, CapacidadeLigadaSemCaminhoEntraNasFaltasUmaVezSo) {
  Bancada b;
  b.igl.DefinirTela(&b.tela);
  constexpr Endereco kV = 0x00100000;
  b.PrepararTriangulo(kV);
  // A CAPACIDADE DESTE TESTE PASSOU A SER O `GL_FOG`: ele continua por fazer,
  // e um teste que continuasse a ligar o `GL_BLEND` passaria a provar o
  // contrario do que diz -- a lista `por_fazer` do `igl.cpp` ja nao tem o
  // blending, porque o rasterizador passou a misturar (frente rast2).
  EXPECT_EQ(b.Ch(kIgl_Enable, GL_FOG), zb2::brew::ResultadoGl::Feito);
  EXPECT_EQ(b.Ch(kIgl_DrawArrays, GL_TRIANGLES, 0, 3), zb2::brew::ResultadoGl::Feito);
  EXPECT_EQ(b.Ch(kIgl_DrawArrays, GL_TRIANGLES, 0, 3), zb2::brew::ResultadoGl::Feito);
  // UMA VEZ, e nao uma por desenho: um titulo de 60 quadros escreveria a mesma
  // linha 60 vezes. Mas UMA vez, e nao zero: a nevoa nao existe, e isso tem de
  // aparecer.
  EXPECT_EQ(b.traco.ContagemFaltas().at("nevoa_de_GL_sem_rasterizador"), 1u);
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

// ---------------------------------------------------------------------------
// 7. A FRENTE rast2: A MISTURA, O ALPHA TEST E A MASCARA DE COR
// ---------------------------------------------------------------------------
//
// A DEMANDA MEDIDA (`/tmp/corrida_igl2.json`, ZB2_QUADROS=300
// ZB2_EVT_START=1, 62 titulos): `blending_de_GL_sem_rasterizador` em gof,
// pacmania e rmp. Os tres DESENHAM (gof 90 316 800 pixels, rmp 91 852 800,
// pacmania 1 454 944 807) e a Tela tem UMA cor: o rasterizador escrevia a cor
// calculada e mais nada, e um jogo que desenha tudo por cima de um fundo com
// `GL_SRC_ALPHA` sai chapado.
//
// O QUE OS TITULOS PEDEM, contado no traco (ZB2_TRACE=1, um titulo por vez):
//
//   rmp: glEnable(GL_BLEND) 1 196x, glBlendFunc(SRC_ALPHA, ONE_MINUS_SRC_ALPHA)
//        897x, glBlendFunc(ONE, ONE) 897x, glBlendFunc(ZERO,
//        ONE_MINUS_SRC_COLOR) 299x
//   gof: glEnable(GL_BLEND) 294x, glBlendFunc(SRC_ALPHA,
//        ONE_MINUS_SRC_ALPHA) 294x
//
// Os pedidos CHEGAVAM (a chamada respondia `feito`) e nao mudavam pixel nenhum:
// o estado ia para um mapa que o rasterizador nao le. **Um estado acumulado que
// ninguem consome e indistinguivel de um pedido perdido.**

// O `gl_slots.inc` GERADO nao tem os factores de mistura: o bloco que os
// justifica esta em `core/video/rasterizador.cpp`, com a linha do cabecalho do
// SDK (`gles/gles_1_0/gl.h:112-124`). Aqui fica so o que estes testes usam.
constexpr std::uint32_t kGlOneMinusSrcColor = 0x0301u;  // gl.h:113

// O TRIANGULO DE SEIS PIXELS, o das outras secoes: (0,1) (0,2) (1,2) (0,3)
// (1,3) (2,3) da janela 8x8. A COR DA ORIGEM e um argumento porque cada teste
// mistura uma cor diferente, e a conta esta escrita em cada um.
void DesenharSeisPixels(Rasterizador* r, Memoria& mem, const EstadoDeRasterizacao& e) {
  constexpr Endereco kV = 0x00100000;
  PorVertices(mem, kV, {{-1.0f, 0.75f}, {-1.0f, 0.0f}, {0.0f, 0.0f}});
  EstadoDeRasterizacao est = e;
  est.vertices = {true, 3, GL_FLOAT, 12, kV};
  PedidoDeDesenho p;
  p.primitiva = GL_TRIANGLES;
  p.quantos = 3;
  std::string motivo;
  ASSERT_TRUE(r->Desenhar(est, p, &motivo)) << motivo;
}

TEST(Rasterizador, AMisturaSomaOrigemEDestinoComOsFactoresDoGL) {
  Memoria mem(nullptr);
  Gravador g;
  Rasterizador r(mem, g);
  EstadoDeRasterizacao e = EstadoBase();

  // O DESTINO JA ESCRITO: o pixel (0,1) e VERDE (0x07E0 -> (0, 1, 0)); os
  // outros cinco continuam a preto. Um destino so num pixel e de proposito: se
  // a mistura lesse sempre o mesmo valor (ou zero), os dois casos davam o mesmo
  // numero e o teste nao dizia onde.
  g.pixels[{0, 1}] = 0x07E0u;
  // A ORIGEM: vermelho com alfa 128 -- 128/255 = 0.5019608.
  e.cor = Rgba{255, 0, 0, 128};
  e.mistura_ligada = true;
  e.mistura_fonte = GL_SRC_ALPHA;              // 0x0302
  e.mistura_destino = GL_ONE_MINUS_SRC_ALPHA;  // 0x0303
  DesenharSeisPixels(&r, mem, e);

  // A CONTA DO PIXEL (0,1), a mao, com origem (1,0,0,0.5019608) e destino
  // (0,1,0,1):
  //   f_origem  = alfa da origem = 0.5019608
  //   f_destino = 1 - alfa       = 0.4980392
  //   vermelho = 1 * 0.5019608 + 0 * 0.4980392 = 0.5019608
  //              -> 0.5019608 * 255 + 0.5 = 128.5 -> 128 -> 128 >> 3 = 16
  //              -> 16 << 11 = 0x8000
  //   verde    = 0 * 0.5019608 + 1 * 0.4980392 = 0.4980392
  //              -> 0.4980392 * 255 + 0.5 = 127.5 -> 127 -> 127 >> 2 = 31
  //              -> 31 << 5 = 0x03E0
  //   azul     = 0
  //   = 0x83E0. (Sem a mistura, o pixel seria 0xF800 -- vermelho chapado -- e a
  //   comparacao com o verde do destino desaparecia.)
  EXPECT_EQ(g.pixels.at({0, 1}), 0x83E0u) << "o pixel misturado do destino verde";
  // SOBRE PRETO sobra a origem, com o mesmo alfa:
  //   vermelho = 0.5019608 -> 128 -> 0x8000 | verde = 0 | azul = 0
  EXPECT_EQ(g.pixels.at({0, 2}), 0x8000u);
  EXPECT_EQ(g.pixels.at({1, 2}), 0x8000u);
  EXPECT_EQ(g.pixels.at({0, 3}), 0x8000u);
  EXPECT_EQ(g.pixels.at({1, 3}), 0x8000u);
  EXPECT_EQ(g.pixels.at({2, 3}), 0x8000u);
  // E os seis pixels foram escritos UMA vez cada: a mistura nao inventa pixels.
  EXPECT_EQ(g.escritas.size(), 6u);
  EXPECT_EQ(g.total, 6u);
}

TEST(Rasterizador, OFactorOneMinusSrcColorMultiplicaODestino) {
  // O TERCEIRO `glBlendFunc` medido em rmp: `(GL_ZERO, GL_ONE_MINUS_SRC_COLOR)`.
  // E o caso que prova que o DESTINO e mesmo lido: a origem nao contribui nada
  // (factor zero) e o resultado e o destino pesado por `(1 - cor da origem)`.
  Memoria mem(nullptr);
  Gravador g;
  Rasterizador r(mem, g);
  EstadoDeRasterizacao e = EstadoBase();

  g.pixels[{0, 1}] = 0xFFFFu;  // destino BRANCO (1, 1, 1)
  e.cor = Rgba{0, 255, 0, 255};  // origem VERDE opaco
  e.mistura_ligada = true;
  e.mistura_fonte = GL_ZERO;  // 0x0000
  e.mistura_destino = kGlOneMinusSrcColor;
  DesenharSeisPixels(&r, mem, e);

  //   vermelho = 0 + 1 * (1 - 0) = 1 -> 255 -> 31 << 11 = 0xF800
  //   verde    = 0 + 1 * (1 - 1) = 0
  //   azul     = 0 + 1 * (1 - 0) = 1 -> 31        -> 0x001F
  //   = 0xF81F (o verde do destino foi APAGADO pelo verde da origem).
  EXPECT_EQ(g.pixels.at({0, 1}), 0xF81Fu);
  // Sobre preto o destino vale (0,0,0) e o resultado e preto.
  EXPECT_EQ(g.pixels.at({0, 2}), 0x0000u);
  // O pixel escrito sobre preto continua a contar como escrita: a mistura nao
  // se recusa a escrever quando a conta da zero.
  EXPECT_EQ(g.escritas.size(), 6u);
}

TEST(Rasterizador, OAlphaTestDescartaOFragmentoAntesDeEscrever) {
  Memoria mem(nullptr);
  Gravador g;
  Rasterizador r(mem, g);
  EstadoDeRasterizacao e = EstadoBase();

  // O ALFA DA ORIGEM E 64: 64/255 = 0.2509804, e o teste e `GL_GREATER` com a
  // referencia em 0.5. O fragmento NAO passa:
  e.cor = Rgba{255, 0, 0, 64};
  e.teste_de_alfa = true;
  e.funcao_de_alfa = GL_GREATER;  // 0x0204
  e.alfa_de_referencia = 0.5f;
  DesenharSeisPixels(&r, mem, e);
  EXPECT_EQ(g.escritas.size(), 0u) << "o alpha test tem de descartar os seis";
  EXPECT_EQ(g.total, 0u);
  EXPECT_EQ(r.Pixels(), 0u);
  // O DESCARTE E CONTADO, e nao confundido com "nao havia geometria": e a unica
  // forma de distinguir um alpha test a funcionar de um alpha test inerte.
  EXPECT_EQ(r.FragmentosDescartados(), 6u);

  // A OUTRA DIRECCAO, com a MESMA funcao e a MESMA referencia: o alfa a 255
  // passa, escreve os seis, e nao descarta mais nenhum. Sem esta metade, um
  // rasterizador que nao escrevesse nada por outra razao passava no teste.
  EstadoDeRasterizacao passa = e;
  passa.cor = Rgba{255, 0, 0, 255};
  DesenharSeisPixels(&r, mem, passa);
  EXPECT_EQ(g.escritas.size(), 6u);
  EXPECT_EQ(g.total, 6u);
  EXPECT_EQ(r.FragmentosDescartados(), 6u) << "o segundo desenho nao descarta nada";
}

TEST(Rasterizador, AMascaraDeCorPreservaOCanalQueProibe) {
  Memoria mem(nullptr);
  Gravador g;
  Rasterizador r(mem, g);
  EstadoDeRasterizacao e = EstadoBase();

  // O destino do pixel (0,1) e VERDE; a origem e VERMELHO opaco; a mascara
  // proibe o canal VERDE (bit 1 a zero: `glColorMask(1, 0, 1, 1)`).
  g.pixels[{0, 1}] = 0x07E0u;
  e.cor = Rgba{255, 0, 0, 255};
  e.mascara_de_cor = 0x5u;  // vermelho e azul sim, verde nao
  DesenharSeisPixels(&r, mem, e);

  // O CANAL PROIBIDO FICA COM O QUE LA ESTAVA: o verde do destino (0x07E0 &
  // 0x07E0) junta-se ao vermelho que a origem escreveu (0xF800) e da 0xFFE0.
  // Sem a mascara o pixel seria 0xF800 -- o verde de uma passada anterior
  // apagado por um desenho que so queria mexer no vermelho.
  EXPECT_EQ(g.pixels.at({0, 1}), 0xFFE0u);
  // Nos outros cinco o destino e preto: a mascara nao muda nada.
  EXPECT_EQ(g.pixels.at({0, 2}), 0xF800u);
  EXPECT_EQ(g.pixels.at({2, 3}), 0xF800u);

  // A MASCARA POR OMISSAO E A OUTRA METADE DA PROVA: o mesmo desenho com
  // `glColorMask(1,1,1,1)` escreve 0xF800 no pixel do destino verde.
  Gravador g2;
  Rasterizador r2(mem, g2);
  g2.pixels[{0, 1}] = 0x07E0u;
  EstadoDeRasterizacao sem_mascara = e;
  sem_mascara.mascara_de_cor = 0x0000000Fu;
  DesenharSeisPixels(&r2, mem, sem_mascara);
  EXPECT_EQ(g2.pixels.at({0, 1}), 0xF800u);
}

// ---------------------------------------------------------------------------
// 8. A ILUMINACAO (`GL_LIGHTING`) E AS DUAS REGRAS QUE A DECIDEM
// ---------------------------------------------------------------------------
//
// A DEMANDA MEDIDA (`/tmp/corrida_igl2.json`): `rescaling_de_normais_sem_iluminacao`
// em gof e rmp, e um `glEnable(GL_LIGHT0)` RECUSADO 299 vezes por corrida em rmp
// ("capacidade desconhecida") -- uma luz que o titulo liga e o emulador diz nao
// conhecer. O rmp faz o setup completo (GL_LIGHTING, GL_LIGHT0, a posicao da
// luz, o material com SHININESS 128 e a cor especular a 0.6) e o rasterizador
// ignorava tudo: a cor saia do `glColor4x`, sem uma contribuicao de luz.

// A NORMAIS DE UM TRIANGULO: 3 GL_FLOAT por vertice, passo 12. A normal
// (1, 1, 0) e a mesma nos tres vertices; ela nao e normalizada no array (o
// `GL_NORMALIZE` e que trata disso), e o comprimento de sqrt(2) fica dito.
void PorNormais(Memoria& mem, Endereco onde, float x, float y, float z, int quantos) {
  for (int k = 0; k < quantos; ++k) {
    const Endereco base = onde + static_cast<Endereco>(k * 12);
    EscreverFloat(mem, base, x);
    EscreverFloat(mem, base + 4, y);
    EscreverFloat(mem, base + 8, z);
  }
}

// O ESTADO DA ILUMINACAO dos testes: UMA luz direccional (w = 0) ao longo de
// +y, branca; material com a difusa branca e o resto a zero; a ambiente da cena
// a zero. Com estes numeros a cor iluminada de um vertice e `N.L` em cada canal
// -- e o teste diz o valor que isso da em RGB565.
void LuzDireccionalEmY(EstadoDeRasterizacao* e) {
  e->iluminacao_ligada = true;
  e->ambiente_da_cena[0] = e->ambiente_da_cena[1] = e->ambiente_da_cena[2] = 0.0f;
  EstadoDeRasterizacao::Aparencia& m = e->material;
  m.ambiente[0] = m.ambiente[1] = m.ambiente[2] = 0.0f;
  m.difusa[0] = m.difusa[1] = m.difusa[2] = 1.0f;
  m.difusa[3] = 1.0f;
  m.especular[0] = m.especular[1] = m.especular[2] = 0.0f;
  m.emissao[0] = m.emissao[1] = m.emissao[2] = 0.0f;
  m.brilho = 0.0f;
  EstadoDeRasterizacao::Luz& l0 = e->luzes[0];
  l0.ligada = true;
  l0.ambiente[0] = l0.ambiente[1] = l0.ambiente[2] = 0.0f;
  l0.difusa[0] = l0.difusa[1] = l0.difusa[2] = 1.0f;
  l0.especular[0] = l0.especular[1] = l0.especular[2] = 0.0f;
  l0.posicao[0] = 0.0f;
  l0.posicao[1] = 1.0f;
  l0.posicao[2] = 0.0f;
  l0.posicao[3] = 0.0f;  // DIRECCIONAL: a posicao e uma direccao
}

TEST(Rasterizador, ALuzDecideACorDoVerticePeloCossenoDaNormal) {
  Memoria mem(nullptr);
  Gravador g;
  Rasterizador r(mem, g);
  EstadoDeRasterizacao e = EstadoBase();  // a cor do estado e VERMELHO opaco

  // A NORMAL E (1, 1, 0), e o teste usa-a SEM a normalizar (o `GL_NORMALIZE`
  // esta DESLIGADO neste teste): o cosseno tem de ser calculado com o vector
  // como ele esta -- e por isso a normal e normalizada pela propria equacao da
  // luz, que e o que o GL faz com o vector transformado.
  constexpr Endereco kV = 0x00100000, kNormais = 0x00102000;
  PorVertices(mem, kV, {{-1.0f, 0.75f}, {-1.0f, 0.0f}, {0.0f, 0.0f}});
  PorNormais(mem, kNormais, 1.0f, 1.0f, 0.0f, 3);
  e.vertices = {true, 3, GL_FLOAT, 12, kV};
  e.normais = {true, 3, GL_FLOAT, 12, kNormais};
  LuzDireccionalEmY(&e);
  DesenharSeisPixels(&r, mem, e);

  // A CONTA, a mao. A luz e direccional ao longo de +y, logo
  // `para_a_luz = (0, 1, 0)`. A normal do vertice e (1, 1, 0), e a equacao
  // normaliza os dois vectores ANTES do produto interno:
  //   N = (1, 1, 0) / sqrt(2) = (0.70710678, 0.70710678, 0)
  //   N . L = 0.70710678 * 0 + 0.70710678 * 1 + 0 = 0.70710678
  //   cor   = emissao(0) + ambiente(0)*cena(0) + 1 * (0 + difusa(1) * difusa_da_luz(1) * 0.70710678)
  //         = 0.70710678 em cada canal
  //   8 bits: 0.70710678 * 255 + 0.5 = 180.81 -> 180 (0xB4)
  //   565: 180 >> 3 = 22 -> 0xB000 | 180 >> 2 = 45 -> 0x05A0 | 22 -> 0x0016
  //   = 0xB5B6
  //
  // SEM ILUMINACAO o mesmo desenho da 0xF800 (o vermelho do `glColor4x`): e a
  // diferenca que este teste fixa. Com a normal A ZERO (a omissao do GL,
  // (0,0,1), contra esta luz) a cor seria 0x0000 -- preto.
  for (int y = 1; y <= 3; ++y) {
    for (int x = 0; x < 3; ++x) {
      const auto it = g.pixels.find({x, y});
      if (it == g.pixels.end()) continue;
      EXPECT_EQ(it->second, 0xB5B6u) << "pixel " << x << "," << y;
    }
  }
  EXPECT_EQ(g.escritas.size(), 6u);
}

TEST(Rasterizador, OAlfaIluminadoVemDaDifusaDoMaterial) {
  // A PRIMEIRA REGRA: o alfa NAO vem da soma das luzes. Todas as luzes tem alfa
  // 1, e somar alfa de luz deixa tudo opaco -- um titulo que desenhe geometria
  // iluminada com transparencia perde-a toda. O alfa e o da DIFUSA DO MATERIAL.
  //
  // A PROVA E PELO ALPHA TEST, e nao pelo canal: o alfa do fragmento nao tem
  // onde ser guardado (a tela e RGB565), e o que se pode afirmar e o que ele
  // DECIDE. A cor do vertice tem alfa ZERO (o `glColor4x` com o quarto
  // argumento a zero, que e o que rmp e pacmania fazem), e o material tem a
  // difusa com alfa 1:
  //   - com o alfa da difusa do material, 1.0 > 0.5 -> o fragmento PASSA;
  //   - com o alfa do vertice (0), o fragmento era descartado.
  Memoria mem(nullptr);
  Gravador g;
  Rasterizador r(mem, g);
  EstadoDeRasterizacao e = EstadoBase();
  e.cor = Rgba{255, 255, 255, 0};  // branco com alfa ZERO
  LuzDireccionalEmY(&e);
  e.material.difusa[3] = 1.0f;
  e.teste_de_alfa = true;
  e.funcao_de_alfa = GL_GREATER;
  e.alfa_de_referencia = 0.5f;
  DesenharSeisPixels(&r, mem, e);
  EXPECT_EQ(r.FragmentosDescartados(), 0u) << "o alfa da difusa do material e 1: nao descarta";
  EXPECT_EQ(g.escritas.size(), 6u);

  // E A OUTRA METADE: com a difusa do material TRANSPARENTE (alfa 0.25), o
  // mesmo desenho e descartado. Sem esta metade, um rasterizador que nunca
  // descartasse passava no teste de cima.
  Gravador g2;
  Rasterizador r2(mem, g2);
  EstadoDeRasterizacao opaco = e;
  opaco.material.difusa[3] = 0.25f;
  DesenharSeisPixels(&r2, mem, opaco);
  EXPECT_EQ(r2.FragmentosDescartados(), 6u);
  EXPECT_EQ(g2.escritas.size(), 0u);
}

TEST(Rasterizador, OColorMaterialPoeACorDoVerticeNoLugarDaAmbienteEDaDifusa) {
  // A SEGUNDA REGRA: com `GL_COLOR_MATERIAL` ligado, a cor do vertice toma o
  // lugar da ambiente e da difusa do material. E o unico caminho pelo qual um
  // vector de cores continua a valer com a luz ligada.
  Memoria mem(nullptr);
  Gravador g;
  Rasterizador r(mem, g);
  EstadoDeRasterizacao e = EstadoBase();
  e.cor = Rgba{255, 0, 0, 255};  // VERMELHO
  e.vertices = {true, 3, GL_FLOAT, 12, 0x00100000};
  LuzDireccionalEmY(&e);  // material difusa BRANCA
  // A luz passa a ser paralela a NORMAL (a omissao do GL, (0,0,1)): assim o
  // cosseno e 1 e o que resta na cor e o MATERIAL, e mais nada.
  e.luzes[0].posicao[0] = 0.0f;
  e.luzes[0].posicao[1] = 0.0f;
  e.luzes[0].posicao[2] = 1.0f;
  EstadoDeRasterizacao sem_cor_do_material = e;
  sem_cor_do_material.cor_do_material = false;
  DesenharSeisPixels(&r, mem, sem_cor_do_material);
  // SEM `GL_COLOR_MATERIAL`: manda a difusa do material, que e BRANCA ->
  // 0.8? NAO: a difusa do material deste teste e (1, 1, 1), logo 1.0 em cada
  // canal -> 0xFFFF.
  EXPECT_EQ(g.pixels.at({0, 1}), 0xFFFFu);

  // COM `GL_COLOR_MATERIAL`: manda a cor do vertice, VERMELHA -> 0xF800. O
  // material branco deixa de decidir nada.
  Gravador g2;
  Rasterizador r2(mem, g2);
  EstadoDeRasterizacao com = e;
  com.cor_do_material = true;
  DesenharSeisPixels(&r2, mem, com);
  EXPECT_EQ(g2.pixels.at({0, 1}), 0xF800u);
}

TEST(Rasterizador, ONormalizarEReescalonarNaoMudamACorPorqueAEquacaoNormaliza) {
  // O QUE ESTE TESTE PROVA, e o que ele NAO prova, dito por inteiro: o
  // `GL_NORMALIZE` e o `GL_RESCALE_NORMAL` pedem que a normal que chega a
  // equacao da luz seja UNITARIA. Nesta implementacao ela e sempre -- a equacao
  // normaliza o vector (a receita verificada do zeebx), e por isso os dois
  // interruptores NAO MUDAM NENHUMA COR. O que os torna observaveis e o estado
  // (o retrato leva-os) e a SAIDA DELES DA TABELA `por_fazer` do `igl.cpp`: a
  // falta existia porque o rasterizador nao tinha transformacao de normais
  // nenhuma, e isso acabou.
  //
  // A PROVA E COM UMA NORMAL OBLIQUA E UM MODELO ESCALADO, que e o caso em que
  // o GL define as duas correccoes:
  //   modelview = diag(2, 2, 2) -> inversa = diag(0.5, 0.5, 0.5)
  //   a terceira linha da inversa e (0, 0, 0.5) -> o factor de reescalonamento
  //   e 1 / 0.5 = 2, e a normal (1, 1, 0) chega a equacao como (1, 0.5, 0) * 2
  //   -> normalizada outra vez -> (0.7071, 0.7071, 0) -> N.L = 0.7071 com a luz
  //   em +y, o mesmo valor do teste da iluminacao -> 0xB5B6.
  //
  // (Os vertices entram a METADE do tamanho porque a modelview os multiplica por
  // dois: e a mesma janela do resto da seccao, e o que se quer medir e a cor.)
  Memoria mem(nullptr);
  Gravador sem_correccao;
  Rasterizador r_sem(mem, sem_correccao);
  EstadoDeRasterizacao e = EstadoBase();
  e.cor = Rgba{255, 255, 255, 255};
  for (int k = 0; k < 16; ++k) e.modelview[k] = 0.0f;
  e.modelview[0] = 2.0f;
  e.modelview[5] = 2.0f;
  e.modelview[10] = 2.0f;
  e.modelview[15] = 1.0f;
  constexpr Endereco kV = 0x00100000, kN = 0x00102000;
  PorVertices(mem, kV, {{-0.5f, 0.375f}, {-0.5f, 0.0f}, {0.0f, 0.0f}});
  PorNormais(mem, kN, 1.0f, 1.0f, 0.0f, 3);
  e.vertices = {true, 3, GL_FLOAT, 12, kV};
  e.normais = {true, 3, GL_FLOAT, 12, kN};
  LuzDireccionalEmY(&e);  // luz ao longo de +y, difusa e material brancos
  DesenharSeisPixels(&r_sem, mem, e);
  EXPECT_EQ(sem_correccao.pixels.at({0, 1}), 0xB5B6u);
  EXPECT_EQ(sem_correccao.escritas.size(), 6u);

  Gravador com_correccao;
  Rasterizador r_com(mem, com_correccao);
  EstadoDeRasterizacao com = e;
  com.normalizar_normais = true;
  com.reescalar_normais = true;
  DesenharSeisPixels(&r_com, mem, com);
  EXPECT_EQ(com_correccao.pixels.at({0, 1}), 0xB5B6u);
  EXPECT_EQ(com_correccao.pixels.at({2, 3}), 0xB5B6u);
  EXPECT_EQ(com_correccao.escritas.size(), 6u);
}

// --- a cablagem: do estado do IGL ate a Tela que a bateria le ---------------

TEST(Rasterizador, OMisturaChegaATelaPelaCablagemDoIglesSemFalta) {
  Bancada b;
  b.igl.DefinirTela(&b.tela);
  // O DESTINO E VERDE NO ECRA INTEIRO, pelo caminho do guest (`glClearColorx` +
  // `glClear`): e o estado real em que um sprite transparente e desenhado.
  ASSERT_EQ(b.Ch(kIgl_ClearColorx, Fixo(0.0f), Fixo(1.0f), Fixo(0.0f), Fixo(1.0f)),
            zb2::brew::ResultadoGl::Feito);
  ASSERT_EQ(b.Ch(kIgl_Clear, GL_COLOR_BUFFER_BIT), zb2::brew::ResultadoGl::Feito);

  // O PEDIDO DO TITULO: ligar a mistura e escolher os factores que rmp e gof
  // escolhem.
  EXPECT_EQ(b.Ch(kIgl_Enable, GL_BLEND), zb2::brew::ResultadoGl::Feito);
  EXPECT_EQ(b.Ch(kIgl_BlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA),
            zb2::brew::ResultadoGl::Feito);

  constexpr Endereco kV = 0x00100000;
  b.PrepararTriangulo(kV, 128.0f / 255.0f);  // alfa 128 -> 0.5019608
  EXPECT_EQ(b.Ch(kIgl_DrawArrays, GL_TRIANGLES, 0, 3), zb2::brew::ResultadoGl::Feito);

  // O PIXEL NA TELA, com a conta do teste da mistura: origem vermelha a 0.5019608
  // sobre o verde do fundo -> 0x83E0. E o valor que um rasterizador sem mistura
  // NAO pode dar (daria 0xF800).
  EXPECT_EQ(b.tela.PixelEm(0, 1), 0x83E0u);
  EXPECT_EQ(b.tela.PixelEm(2, 3), 0x83E0u);
  // E A CAPACIDADE SAIU DA LISTA DAS FALTAS: a tabela `por_fazer` do `igl.cpp`
  // nao pode continuar a dizer que falta o que ja se faz.
  EXPECT_EQ(b.Faltas("blending_de_GL_sem_rasterizador"), 0u);
}

// Os valores que o `.inc` gerado NAO tem, com a linha do cabecalho do SDK (os
// blocos que os justificam estao em `core/brew/igl.cpp` e em
// `core/video/rasterizador.cpp`):
//   gles_1_0/gl.h:180 GL_RESCALE_NORMAL 0x803A | :461 GL_LIGHT0 0x4000
//   :259 GL_POSITION 0x1203
constexpr std::uint32_t kGlRescaleNormal = 0x803Au;
constexpr std::uint32_t kGlLight0DoTeste = 0x4000u;
constexpr std::uint32_t kGlPosition = 0x1203u;
// `AEEGLfloat` (32 bits): os bits da palavra SAO o `float`.
std::uint32_t Real2(float v) {
  std::uint32_t u = 0;
  std::memcpy(&u, &v, sizeof(u));
  return u;
}

TEST(Rasterizador, ALuzChegaAoMotorPelaCablagemEAPosicaoVaiParaOEspacoDoOlho) {
  Bancada b;
  b.igl.DefinirTela(&b.tela);
  constexpr Endereco kVector = 0x00103000;

  // 1. A LUZ 0 PASSA A SER UMA CAPACIDADE CONHECIDA. Antes desta frente o
  //    `glEnable(GL_LIGHT0)` era RECUSADO -- medido, 299 vezes por corrida em
  //    rmp, com `slot=28 args=[00004000 ...]`: uma luz que o titulo liga e o
  //    emulador diz nao conhecer.
  EXPECT_EQ(b.Ch(kIgl_Enable, kGlLight0DoTeste), zb2::brew::ResultadoGl::Feito);
  EXPECT_TRUE(b.igl.InterruptorLigado(kGlLight0DoTeste));
  EXPECT_EQ(b.Faltas("glEnable"), 0u);

  // 2. A POSICAO DA LUZ E GUARDADA EM COORDENADAS DE OLHO. Com a modelview
  //    transladada para (0, 0, -5), o `glLightfv(GL_LIGHT0, GL_POSITION,
  //    (0, 0, 1, 1))` fica em (0, 0, -4, 1). As DUAS leituras dizem coisas
  //    diferentes de proposito: o valor cru e o que o titulo escreveu, o de
  //    olho e o que a luz usa -- e um emulador que guardasse so o primeiro
  //    deixaria a luz a andar com cada modelo.
  EXPECT_EQ(b.Ch(kIgl_Translatex, 0, 0, Fixo(-5.0f)), zb2::brew::ResultadoGl::Feito);
  b.mem.Escrever32(kVector + 0u, Real2(0.0f));
  b.mem.Escrever32(kVector + 4u, Real2(0.0f));
  b.mem.Escrever32(kVector + 8u, Real2(1.0f));
  b.mem.Escrever32(kVector + 12u, Real2(1.0f));
  EXPECT_EQ(b.Ch(zb2::brew::kIgl_Lightfv, kGlLight0DoTeste, kGlPosition, kVector),
            zb2::brew::ResultadoGl::Feito);
  const auto* cru = b.igl.ParametroDeLuz(kGlLight0DoTeste, kGlPosition);
  ASSERT_NE(cru, nullptr);
  EXPECT_FLOAT_EQ((*cru)[2], 1.0f);
  EXPECT_FLOAT_EQ((*cru)[3], 1.0f);
  const auto* em_olho = b.igl.ParametroDeLuzEmOlho(kGlLight0DoTeste, kGlPosition);
  ASSERT_NE(em_olho, nullptr) << "a posicao nao foi levada para o espaco do olho";
  EXPECT_FLOAT_EQ((*em_olho)[2], -4.0f);
  EXPECT_FLOAT_EQ((*em_olho)[3], 1.0f) << "o w decide se a luz e posicional";
}

TEST(Rasterizador, OQueSeImplementouSaiuDaTabelaDasFaltas) {
  // O SINAL HONESTO: uma capacidade que passa a ser feita TEM de sair da lista
  // `por_fazer` do `igl.cpp`. Um teste que so afirmasse o pixel passava com a
  // tabela a dizer que falta o que ja se faz -- a mentira simetrica do stub
  // mudo, e a razao deste teste existir.
  Bancada b;
  b.igl.DefinirTela(&b.tela);
  constexpr Endereco kV = 0x00100000;
  b.PrepararTriangulo(kV);
  for (const std::uint32_t cap : {GL_BLEND, GL_ALPHA_TEST, GL_LIGHTING, GL_NORMALIZE,
                                  kGlRescaleNormal, 0x0B57u /* GL_COLOR_MATERIAL */}) {
    EXPECT_EQ(b.Ch(kIgl_Enable, cap), zb2::brew::ResultadoGl::Feito) << "cap 0x" << std::hex << cap;
  }
  b.Ch(kIgl_ColorMask, 1, 1, 1, 1);
  EXPECT_EQ(b.Ch(kIgl_DrawArrays, GL_TRIANGLES, 0, 3), zb2::brew::ResultadoGl::Feito);
  for (const char* nome :
       {"blending_de_GL_sem_rasterizador", "alpha_test_de_GL_sem_rasterizador",
        "iluminacao_de_GL_sem_rasterizador", "normalizacao_de_normais_sem_rasterizador",
        "rescaling_de_normais_sem_iluminacao", "cor_do_material_sem_iluminacao",
        "glColorMask_de_GL_sem_rasterizador"}) {
    EXPECT_EQ(b.Faltas(nome), 0u) << nome;
  }
  // E O DESENHO CONTINUA A ACONTECER (com a luz ligada e sem normais definidas,
  // a normal e a omissao do GL, (0, 0, 1)): seis pixels na Tela.
  EXPECT_EQ(b.tela.Escritos(), 6u);
}

TEST(Rasterizador, OAlphaTestEAMascaraDeCorChegamAoTelaPelaCablagem) {
  Bancada b;
  b.igl.DefinirTela(&b.tela);
  constexpr Endereco kV = 0x00100000;
  // A TELA E LIMPA PRIMEIRO, para o "nao escreveu" ser observavel: com a tela a
  // zero, `Escritos() == 0` tambem seria o resultado de um desenho que nao
  // tivesse chegado la.
  ASSERT_EQ(b.Ch(kIgl_ClearColorx, Fixo(0.0f), Fixo(1.0f), Fixo(0.0f), Fixo(1.0f)),
            zb2::brew::ResultadoGl::Feito);
  ASSERT_EQ(b.Ch(kIgl_Clear, GL_COLOR_BUFFER_BIT), zb2::brew::ResultadoGl::Feito);
  const std::uint64_t limpos = b.tela.Escritos();

  // O ALPHA TEST: `GL_GREATER` com a referencia em 0.5 (GLfixed 16.16) e um
  // alfa de origem a 0.25 -> os seis fragmentos sao descartados.
  EXPECT_EQ(b.Ch(kIgl_Enable, GL_ALPHA_TEST), zb2::brew::ResultadoGl::Feito);
  EXPECT_EQ(b.Ch(kIgl_AlphaFuncx, GL_GREATER, Fixo(0.5f)), zb2::brew::ResultadoGl::Feito);
  b.PrepararTriangulo(kV, 0.25f);
  EXPECT_EQ(b.Ch(kIgl_DrawArrays, GL_TRIANGLES, 0, 3), zb2::brew::ResultadoGl::Feito);
  EXPECT_EQ(b.tela.Escritos(), limpos) << "o alpha test descartou os seis, e nada foi escrito";
  EXPECT_EQ(b.igl.RasterizadorRef().FragmentosDescartados(), 6u);

  // A MASCARA DE COR, no mesmo caminho: `glColorMask(1, 0, 1, 1)` e um pedido
  // observavel pelo estado, e nao uma falta.
  EXPECT_EQ(b.Ch(kIgl_ColorMask, 1, 0, 1, 1), zb2::brew::ResultadoGl::Feito);
  EXPECT_EQ(b.Faltas("glColorMask_de_GL_sem_rasterizador"), 0u);
  EXPECT_EQ(b.Faltas("alpha_test_de_GL_sem_rasterizador"), 0u);
}



// ---------------------------------------------------------------------------
// 9. AS LINHAS (`GL_LINES`, `GL_LINE_LOOP` e `GL_LINE_STRIP`)
// ---------------------------------------------------------------------------
//
// A DEMANDA MEDIDA, e o numero que a pede (corrida com `ZB2_QUADROS=300
// ZB2_EVT_START=1`): `heavyweaponbrew` recusa 304 desenhos e o `pbc` 7, todos
// com o mesmo motivo -- `GL_LINE_STRIP` sem rasterizador. E o UNICO motivo de
// recusa de primitiva no corpus, e o `heavyweaponbrew` e o titulo que mais
// desenha (116,8 M px). O `GL_POINTS` NAO e pedido por titulo nenhum e continua
// recusado COM O NOME.
//
// A REGRA DA LINHA, e o desvio DECLARADO (a convencao da casa e a
// `ArestaPartilhadaNaoEscreveDuasVezesNemDeixaFenda`, e esta e a versao dela
// para segmentos):
//
//   - o tracado e um DDA no eixo MAIOR, com o ponto inicial INCLUIDO e o ponto
//     final ABERTO em cada segmento. E o que faz um vertice partilhado por dois
//     segmentos escrever UM pixel -- e nao dois;
//   - a ULTIMA ponta de uma `GL_LINE_STRIP` e FECHADA (o vertice final escreve o
//     seu pixel, e nenhum outro segmento o escreveria);
//   - a `GL_LINES` mantem ABERTA a ponta de cada segmento (a mesma regra dos
//     outros): uma linha de 6 px escreve 6 pixels, e nao 7;
//   - o desvio em relacao a `diamond-exit` EXACTA do OpenGL esta nos EXTREMOS:
//     um segmento cujo ponto final caia dentro do losango do pixel final nao
//     escreve esse pixel. Fica dito AQUI, e o teste
//     `DiagonalDeQuarentaECincoGraus` fixa-o.

TEST(Rasterizador, LinhaHorizontalDeSeisPixelsEscreveSeisPixels) {
  Memoria mem(nullptr);
  Gravador g;
  Rasterizador r(mem, g);
  EstadoDeRasterizacao e = EstadoBase();  // janela (0,0,8,8), vermelho opaco

  // OS DOIS VERTICES, com a conta que os poe na janela:
  //   janela x = (x_ndc + 1) * 8 / 2   |   janela y = (1 - y_ndc) * 8 / 2
  //   A = (-0.75, 0.25) -> (1, 3)      B = (0.75, 0.25) -> (7, 3)
  // O segmento tem 6 px de comprimento entre os dois vertices, e a regra acima
  // (ponta final ABERTA) escreve 6 pixels: x = 1..6 na linha y = 3.
  constexpr Endereco kV = 0x00100000;
  PorVertices(mem, kV, {{-0.75f, 0.25f}, {0.75f, 0.25f}});
  e.vertices = {true, 3, GL_FLOAT, 12, kV};
  PedidoDeDesenho p;
  p.primitiva = GL_LINES;
  p.quantos = 2;
  std::string motivo;
  ASSERT_TRUE(r.Desenhar(e, p, &motivo)) << motivo;

  EXPECT_EQ(g.total, 6u);
  EXPECT_EQ(g.escritas.size(), 6u);
  for (int x = 1; x <= 6; ++x) {
    EXPECT_EQ(g.Vezes(x, 3), 1) << "pixel " << x << ",3";
    EXPECT_TRUE(g.SoEstePixel(x, 3, 0xF800u)) << "pixel " << x << ",3";
  }
  // A PONTA ABERTA, e o rasto negativo que o prova: o pixel do vertice final
  // (7,3) NAO e escrito, e nenhum pixel fora da linha o e.
  EXPECT_EQ(g.Vezes(7, 3), 0);
  EXPECT_EQ(g.Vezes(1, 2), 0);
  EXPECT_EQ(g.Vezes(1, 4), 0);
  EXPECT_EQ(r.Pixels(), 6u);
  EXPECT_EQ(r.Segmentos(), 1u);
  EXPECT_EQ(r.PrimitivasRecusadas(), 0u);
}

TEST(Rasterizador, FaixaDeTresPontosColinearesNaoContaOPixelDuasVezes) {
  Memoria mem(nullptr);
  Gravador g;
  Rasterizador r(mem, g);
  EstadoDeRasterizacao e = EstadoBase();

  // TRES PONTOS COLINEARES, e a conta que os poe na janela:
  //   A = (-1, 0)    -> (0, 4)
  //   B = (-0.5, 0)  -> (2, 4)
  //   C = (0.25, 0)  -> (5, 4)
  // Dois segmentos: A..B (pixels 0 e 1, ponta aberta) e B..C (pixels 2, 3 e 4),
  // mais a ponta FECHADA da faixa (pixel 5). Sao 6 pixels, e o pixel 2 -- o
  // vertice B, partilhado pelos dois segmentos -- e escrito UMA vez.
  constexpr Endereco kV = 0x00100000;
  PorVertices(mem, kV, {{-1.0f, 0.0f}, {-0.5f, 0.0f}, {0.25f, 0.0f}});
  e.vertices = {true, 3, GL_FLOAT, 12, kV};
  PedidoDeDesenho p;
  p.primitiva = GL_LINE_STRIP;
  p.quantos = 3;
  std::string motivo;
  ASSERT_TRUE(r.Desenhar(e, p, &motivo)) << motivo;

  EXPECT_EQ(g.total, 6u);
  EXPECT_EQ(g.escritas.size(), 6u);
  for (int x = 0; x <= 5; ++x) {
    EXPECT_EQ(g.Vezes(x, 4), 1) << "pixel " << x << ",4";
  }
  EXPECT_EQ(g.Vezes(6, 4), 0) << "a faixa nao chega ao pixel 6";
  EXPECT_EQ(r.Pixels(), 6u);
  EXPECT_EQ(r.Segmentos(), 2u);
  // O TRACO DO FIM conta SEGMENTOS, e nao triangulos: um traco que dissesse
  // "triangulos" com uma linha desenhada seria a contabilidade a mentir.
  EXPECT_NE(motivo.find("2 segmentos"), std::string::npos) << motivo;
  EXPECT_EQ(motivo.find("triangulos"), std::string::npos) << motivo;
}

TEST(Rasterizador, OLoopVoltaAoPrimeiroPontoSemEscreverDuasVezes) {
  Memoria mem(nullptr);
  Gravador g;
  Rasterizador r(mem, g);
  EstadoDeRasterizacao e = EstadoBase();

  // UM TRIANGULO FECHADO EM LINHAS: (0,4) -> (4,4) -> (0,0) -> (0,4). O ULTIMO
  // segmento volta ao PRIMEIRO vertice, cujo pixel ja foi escrito pelo primeiro
  // segmento: com a ponta aberta em todos os segmentos de um `LINE_LOOP`, esse
  // pixel nao e escrito duas vezes.
  constexpr Endereco kV = 0x00100000;
  PorVertices(mem, kV, {{-1.0f, 0.0f}, {0.0f, 0.0f}, {-1.0f, 1.0f}});
  e.vertices = {true, 3, GL_FLOAT, 12, kV};
  PedidoDeDesenho p;
  p.primitiva = GL_LINE_LOOP;
  p.quantos = 3;
  std::string motivo;
  ASSERT_TRUE(r.Desenhar(e, p, &motivo)) << motivo;
  EXPECT_EQ(r.Segmentos(), 3u);
  // O pixel do vertice (0,4) escreveu-se UMA vez -- e nao uma por cada um dos
  // dois segmentos que o tem por ponta.
  EXPECT_EQ(g.Vezes(0, 4), 1);
  for (const auto& par : g.escritas) EXPECT_EQ(par.second, 1) << "pixel escrito duas vezes";
}

TEST(Rasterizador, DiagonalDeQuarentaECincoGraus) {
  Memoria mem(nullptr);
  Gravador g;
  Rasterizador r(mem, g);
  EstadoDeRasterizacao e = EstadoBase();

  // A DIAGONAL (0,0) -> (4,4), ou seja A = (-1, 1) e B = (0, 0) em NDC. Com o
  // passo igual nos dois eixos, o DDA no eixo maior tem de andar EM DIAGONAL:
  // os pixels sao (0,0), (1,1), (2,2) e (3,3) -- e o (4,4) fica de fora pela
  // ponta ABERTA (o desvio declarado).
  constexpr Endereco kV = 0x00100000;
  PorVertices(mem, kV, {{-1.0f, 1.0f}, {0.0f, 0.0f}});
  e.vertices = {true, 3, GL_FLOAT, 12, kV};
  PedidoDeDesenho p;
  p.primitiva = GL_LINES;
  p.quantos = 2;
  std::string motivo;
  ASSERT_TRUE(r.Desenhar(e, p, &motivo)) << motivo;
  EXPECT_EQ(g.total, 4u);
  for (int k = 0; k < 4; ++k) EXPECT_EQ(g.Vezes(k, k), 1) << "pixel " << k << "," << k;
  EXPECT_EQ(g.Vezes(4, 4), 0);
  EXPECT_EQ(g.Vezes(1, 0), 0);
  EXPECT_EQ(g.Vezes(0, 1), 0);
}

TEST(Rasterizador, SegmentoTodoAtrasDoPlanoProximoEDescartado) {
  Memoria mem(nullptr);
  Gravador g;
  Rasterizador r(mem, g);
  EstadoDeRasterizacao e = EstadoBase();  // janela (0,0,8,8)
  PorFrustum(&e);

  // OS DOIS VERTICES ATRAS DA CAMARA: com `clip.w = -z` (ver o `PorFrustum`),
  // z = +2 da w = -2 e `clip.z + clip.w` = -4.06 - 2 = -6.06 < 0 nos DOIS. Um
  // ponto atras do plano proximo projetado DA lixo (o `Projetar` recusa-o), e
  // por isso o segmento e DESCARTADO -- contado, e nao desenhado as cegas.
  constexpr Endereco kV = 0x00100000;
  PorVerticesZ(mem, kV, {{0.0f, 0.0f, 2.0f}, {2.0f, 0.0f, 2.0f}});
  e.vertices = {true, 3, GL_FLOAT, 12, kV};
  PedidoDeDesenho p;
  p.primitiva = GL_LINES;
  p.quantos = 2;
  std::string motivo;
  EXPECT_TRUE(r.Desenhar(e, p, &motivo)) << motivo;
  EXPECT_EQ(g.total, 0u);
  EXPECT_EQ(r.Pixels(), 0u);
  EXPECT_EQ(r.Segmentos(), 0u);
  EXPECT_EQ(r.SegmentosDescartados(), 1u);
  EXPECT_EQ(r.SegmentosRecortados(), 0u);
  // E O SEGMENTO QUE CRUZA O PLANO e RECORTADO, e nao descartado: os dois
  // vertices com o mesmo (x, y) mas um a z = -2 (a frente) e outro a z = +2
  // (atras) dao um segmento que sobrevive ao recorte.
  PorVerticesZ(mem, kV, {{0.0f, 0.0f, -2.0f}, {0.0f, 0.0f, 2.0f}});
  EXPECT_TRUE(r.Desenhar(e, p, &motivo)) << motivo;
  EXPECT_EQ(r.SegmentosRecortados(), 1u);
  EXPECT_EQ(r.SegmentosDescartados(), 1u);
  EXPECT_GT(g.total, 0u);
}

TEST(Rasterizador, LarguraDeLinhaMaiorQueUmRecusaComONome) {
  Memoria mem(nullptr);
  Gravador g;
  Rasterizador r(mem, g);
  EstadoDeRasterizacao e = EstadoBase();
  constexpr Endereco kV = 0x00100000;
  PorVertices(mem, kV, {{-0.75f, 0.25f}, {0.75f, 0.25f}});
  e.vertices = {true, 3, GL_FLOAT, 12, kV};
  PedidoDeDesenho p;
  p.primitiva = GL_LINES;
  p.quantos = 2;
  std::string motivo;

  // A OMISSAO DO GL E 1: a largura 1 e uma linha de um pixel, e desenha.
  EXPECT_FLOAT_EQ(e.largura_de_linha, 1.0f) << "a omissao do estado tem de ser 1";
  ASSERT_TRUE(r.Desenhar(e, p, &motivo)) << motivo;
  EXPECT_EQ(g.total, 6u);

  // A LARGURA 2 RECUSA COM O NOME. Desenhar 2 px com 1 px seria uma mentira
  // silenciosa -- e o valor continua a ficar guardado no estado (o pedido nao
  // se perde: `kIgl_LineWidthx` entra em `parametros_`).
  e.largura_de_linha = 2.0f;
  const std::uint64_t antes = g.total;
  EXPECT_FALSE(r.Desenhar(e, p, &motivo));
  EXPECT_NE(motivo.find("largura de linha 2"), std::string::npos) << motivo;
  EXPECT_NE(motivo.find("linha grossa"), std::string::npos) << motivo;
  EXPECT_EQ(g.total, antes) << "recusou e escreveu na mesma";
  EXPECT_EQ(r.PrimitivasRecusadas(), 1u);
}

TEST(Rasterizador, OPointsContinuaRecusadoEOsNomeadosPassamAListarAsLinhas) {
  Memoria mem(nullptr);
  Gravador g;
  Rasterizador r(mem, g);
  EstadoDeRasterizacao e = EstadoBase();
  constexpr Endereco kV = 0x00100000;
  PorVertices(mem, kV, {{-0.75f, 0.25f}, {0.75f, 0.25f}});
  e.vertices = {true, 3, GL_FLOAT, 12, kV};
  PedidoDeDesenho p;
  p.primitiva = GL_POINTS;
  p.quantos = 2;
  std::string motivo;
  EXPECT_FALSE(r.Desenhar(e, p, &motivo));
  EXPECT_NE(motivo.find("GL_POINTS"), std::string::npos) << motivo;
  EXPECT_EQ(g.total, 0u);
  EXPECT_EQ(r.PrimitivasRecusadas(), 1u);
  // O TEXTO DA RECUSA LISTA O QUE EXISTE, e nao o que existia antes das linhas:
  // um texto a dizer "so TRIANGLES, TRIANGLE_STRIP e TRIANGLE_FAN" com as linhas
  // ja desenhadas seria uma linha de log que nao pode ser verdadeira (P7).
  EXPECT_NE(motivo.find("LINES"), std::string::npos) << motivo;
  EXPECT_NE(motivo.find("LINE_LOOP"), std::string::npos) << motivo;
  EXPECT_NE(motivo.find("LINE_STRIP"), std::string::npos) << motivo;
  EXPECT_EQ(motivo.find("so TRIANGLES, TRIANGLE_STRIP e TRIANGLE_FAN"), std::string::npos) << motivo;
}

TEST(Rasterizador, AsLinhasChegamATelaPelaCablagemDoIglEALarguraEGuardada) {
  Bancada b;
  b.igl.DefinirTela(&b.tela);
  constexpr Endereco kV = 0x00100000;
  EXPECT_EQ(b.Ch(kIgl_Viewport, 0, 0, 8, 8), zb2::brew::ResultadoGl::Feito);
  PorVertices(b.mem, kV, {{-0.75f, 0.25f}, {0.75f, 0.25f}});
  EXPECT_EQ(b.Ch(kIgl_VertexPointer, 3, GL_FLOAT, 12, kV), zb2::brew::ResultadoGl::Feito);
  EXPECT_EQ(b.Ch(kIgl_EnableClientState, GL_VERTEX_ARRAY), zb2::brew::ResultadoGl::Feito);
  EXPECT_EQ(b.Ch(kIgl_Color4x, Fixo(1.0f), Fixo(0.0f), Fixo(0.0f), Fixo(1.0f)),
            zb2::brew::ResultadoGl::Feito);

  // A LARGURA 1 PEDIDA PELO SLOT, e a linha desenhada pelo slot: os seis pixels
  // da Tela -- a mesma tela que a bateria le.
  EXPECT_EQ(b.Ch(kIgl_LineWidthx, Fixo(1.0f)), zb2::brew::ResultadoGl::Feito);
  EXPECT_EQ(b.Ch(kIgl_DrawArrays, GL_LINES, 0, 2), zb2::brew::ResultadoGl::Feito);
  EXPECT_EQ(b.tela.Escritos(), 6u);
  EXPECT_EQ(b.igl.RasterizadorRef().Segmentos(), 1u);

  // A LARGURA 2: o valor FICA GUARDADO (o pedido e observavel) e o DESENHO
  // RECUSA com o nome -- o defeito que esta frente corrige era o contrario:
  // guardar o valor e nao o aplicar, sem o dizer.
  EXPECT_EQ(b.Ch(kIgl_LineWidthx, Fixo(2.0f)), zb2::brew::ResultadoGl::Feito);
  const std::vector<std::uint32_t>* largura = b.igl.Parametro(kIgl_LineWidthx, 0);
  ASSERT_NE(largura, nullptr);
  ASSERT_EQ(largura->size(), 1u);
  EXPECT_EQ((*largura)[0], Fixo(2.0f));
  const std::uint64_t antes = b.tela.Escritos();
  EXPECT_EQ(b.Ch(kIgl_DrawArrays, GL_LINES, 0, 2), zb2::brew::ResultadoGl::Recusado);
  EXPECT_EQ(b.tela.Escritos(), antes);
  EXPECT_NE(b.igl.Ultimas().back().motivo.find("largura de linha 2"), std::string::npos)
      << b.igl.Ultimas().back().motivo;

  // E A LARGURA NAO POSITIVA CONTINUA A RECUSAR NO SLOT (nao e um estado).
  EXPECT_EQ(b.Ch(kIgl_LineWidthx, 0u), zb2::brew::ResultadoGl::Recusado);
}
// ---------------------------------------------------------------------------
// 10. A FRENTE IGL9: A LIMPEZA DE PROFUNDIDADE E O AMBIENTE DO MODELO
// ---------------------------------------------------------------------------
//
// As duas metades que o `igl.cpp` guarda e o rasterizador usa, provadas pela
// CABLAGEM (os slots do IGL, como um titulo os chama), e nao pelo estado posto
// a mao: um pedido guardado num mapa que o retrato do desenho nao le e
// indistinguivel de um pedido perdido.

TEST(Rasterizador, OGlClearDepthxDecideOValorQueFicaNoBuffer) {
  Bancada b;
  b.igl.DefinirTela(&b.tela);

  // A OMISSAO DO GL e 1.0, e ela esta no primeiro `glClear`.
  EXPECT_EQ(b.Ch(kIgl_Clear, GL_DEPTH_BUFFER_BIT), zb2::brew::ResultadoGl::Feito);
  ASSERT_TRUE(b.igl.RasterizadorRef().TemBufferDeProfundidade());
  EXPECT_FLOAT_EQ(b.igl.RasterizadorRef().ProfundidadeEm(0, 0), 1.0f);

  // `glClearDepthx(0.25)` e o `glClear` seguinte: o buffer fica a 0.25 em TODA
  // a superficie. Um `glClear` que usasse 1.0 fixo daria 1.0 aqui, e o
  // `glClearDepthx` seria um pedido guardado que ninguem le.
  EXPECT_EQ(b.Ch(kIgl_ClearDepthx, Fixo(0.25f)), zb2::brew::ResultadoGl::Feito);
  EXPECT_EQ(b.Ch(kIgl_Clear, GL_DEPTH_BUFFER_BIT), zb2::brew::ResultadoGl::Feito);
  EXPECT_FLOAT_EQ(b.igl.RasterizadorRef().ProfundidadeEm(0, 0), 0.25f);
  EXPECT_FLOAT_EQ(b.igl.RasterizadorRef().ProfundidadeEm(639, 479), 0.25f);
}

TEST(Rasterizador, OAmbienteDoModeloDaCablagemEntraNaCorIluminada) {
  // A EQUACAO, com UM termo so: sem nenhuma luz ligada,
  //   cor = emissao + ambiente_do_material * ambiente_da_cena
  // O material e branco (1,1,1) e a emissao fica a zero, logo a cor E o
  // ambiente da cena. Os dois numeros estao escritos:
  //   0.5  -> canal = 0.5*255+0.5 = 128 -> (128>>3)<<11|(128>>2)<<5|(128>>3) = 0x8410
  //   0.25 -> canal =  64            -> (8<<11)|(16<<5)|8                   = 0x4208
  constexpr Endereco kVertices = 0x00100000, kAmbiente = 0x00104000;
  constexpr std::uint32_t kGlAmbient = 0x1200u;      // gles_1_0/gl.h:259
  constexpr std::uint32_t kGlAmbientDaCena = 0x0B53u;  // gles_1_1/gl.h:362
  const auto cor_do_desenho = [&](float ambiente) {
    Bancada b;
    b.igl.DefinirTela(&b.tela);
    b.PrepararTriangulo(kVertices);
    // O AMBIENTE DO MODELO, pela cablagem do IGLES11/IGL: quatro GLfixed.
    // O MATERIAL E `float` (`AEEGLfloat`, 32 bits): o `glMaterialfv` do IGLES11
    // le os BITS de um `float`, e nao um 16.16 -- escrever `Fixo(1.0f)` aqui
    // punha 0x00010000, que como `float` e um denormal de 9.2e-41, e o material
    // sairia PRETO sem nenhum aviso. E a armadilha que a frente igl2 fechou.
    for (int k = 0; k < 4; ++k) {
      b.mem.Escrever32(kAmbiente + 4u * static_cast<std::uint32_t>(k), Real2(1.0f));
    }
    EXPECT_EQ(b.Ch(zb2::brew::kIgl_Materialfv, GL_FRONT_AND_BACK, kGlAmbient, kAmbiente),
              zb2::brew::ResultadoGl::Feito);
    // E o AMBIENTE DA CENA pelo `glLightModelxv(GL_LIGHT_MODEL_AMBIENT, ...)`.
    const std::uint32_t cena[4] = {Fixo(ambiente), Fixo(ambiente), Fixo(ambiente), Fixo(1.0f)};
    for (int k = 0; k < 4; ++k) {
      b.mem.Escrever32(kAmbiente + 16u + 4u * static_cast<std::uint32_t>(k), cena[k]);
    }
    EXPECT_EQ(b.Ch(kIgl_LightModelxv, kGlAmbientDaCena, kAmbiente + 16u),
              zb2::brew::ResultadoGl::Feito);
    EXPECT_EQ(b.Ch(kIgl_Enable, GL_LIGHTING), zb2::brew::ResultadoGl::Feito);
    EXPECT_EQ(b.Ch(kIgl_DrawArrays, GL_TRIANGLES, 0, 3), zb2::brew::ResultadoGl::Feito);
    // A COR DO DESENHO, no primeiro pixel escrito.
    std::uint32_t cor = 0;
    for (int y = 0; y < 8 && cor == 0; ++y) {
      for (int x = 0; x < 8 && cor == 0; ++x) {
        const std::uint32_t p = b.tela.PixelEm(x, y);
        if (p != 0) cor = p;
      }
    }
    return cor;
  };
  EXPECT_EQ(cor_do_desenho(0.5f), 0x8410u);
  EXPECT_EQ(cor_do_desenho(0.25f), 0x4208u);
}




}  // namespace
}  // namespace zb2::video
