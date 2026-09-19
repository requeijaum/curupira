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
//   - LINES / LINE_LOOP / LINE_STRIP por DDA no eixo MAIOR, com o ponto final
//     ABERTO em cada segmento (excepto na ultima ponta de uma faixa). A regra e
//     a versao para segmentos da mesma convencao: um vertice partilhado por dois
//     segmentos escreve UM pixel, e nenhum pixel se perde. O desvio declarado em
//     relacao a `diamond-exit` exacta do GL esta nos EXTREMOS do segmento (ver
//     `RasterizarSegmento` em rasterizador.cpp, e o teste
//     `Rasterizador.DiagonalDeQuarentaECincoGraus`);
//   - A LARGURA DA LINHA: `estado.largura_de_linha` (a omissao do GL e 1.0). A
//     largura <= 1 e uma linha de um pixel; a largura > 1 RECUSA o desenho com o
//     NOME ("largura de linha 2 sem rasterizador de linha grossa nesta etapa"),
//     porque desenhar 2 px com 1 px seria uma mentira silenciosa. O valor
//     continua a ficar guardado no `igl.cpp` (o pedido e observavel), e nao
//     aplicado;
//   - interpolacao de cor (por vertice) e de coordenadas de textura por unidade;
//   - amostragem de ate duas texturas (GL_NEAREST, clamp), composta em ordem com
//     GL_MODULATE ou GL_REPLACE; GL_COMBINE fica recusado porque fontes e
//     operandos ainda nao sao estado modelado;
//   - teste de profundidade, quando `GL_DEPTH_TEST` esta ligado;
//   - descarte de faces por orientacao (`glCullFace`/`glFrontFace`).
//
// O QUE A FRENTE rast2 ACRESCENTOU (e a demanda medida que a pediu):
//
//   - MISTURA (`GL_BLEND`), com os nove factores do `glBlendFunc` e a conta
//     `origem*f_origem + destino*f_destino` presa a [0,1] em `float`, contra o
//     pixel que JA esta na superficie (`Superficie::Ler`). Pedida por gof
//     (`glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)`, 294x), rmp (a mesma
//     897x, mais `(ONE, ONE)` 897x e `(ZERO, ONE_MINUS_SRC_COLOR)` 299x) e
//     pacmania;
//   - ALPHA TEST (`GL_ALPHA_TEST` + `glAlphaFuncx`): o fragmento reprovado nao
//     escreve cor nem profundidade, e o descarte e contado
//     (`FragmentosDescartados()`);
//   - MASCARA DE COR (`glColorMask`): os canais proibidos ficam com o que o
//     destino ja tinha -- escrever o valor calculado neles apagaria o que uma
//     passada anterior deixou;
//   - ILUMINACAO (`GL_LIGHTING`), por vertice, com a equacao do GL ES 1.x:
//     `emissao + ambiente_m*ambiente_da_cena + SOMA(atenuacao*holofote *
//     (ambiente_m*ambiente_l + difusa_m*difusa_l*max(N.L,0) +
//     especular_m*especular_l*max(N.H,0)^brilho))`. O material e um so (a face e
//     ignorada, como no ES 1.x), com o `GL_COLOR_MATERIAL` a pôr a cor do
//     vertice no lugar da ambiente e da difusa, e O ALFA A VIR DA DIFUSA DO
//     MATERIAL (somar alfa de luz deixa tudo opaco). A normal vem do
//     `GL_NORMAL_ARRAY` (ou da omissao do GL, `(0,0,1)`) e e transformada pela
//     TRANSPOSTA DA INVERSA da modelview; a posicao do vertice e guardada no
//     espaco do olho, que e onde a luz do GL vive.
//   - AS LUZES LIGAM-SE COM `glEnable(GL_LIGHT0 + i)`: o estado tem as oito, e
//     ate esta frente o `glEnable(GL_LIGHT0)` era RECUSADO por capacidade
//     desconhecida (299 recusas por corrida em rmp).
//
// O `GL_NORMALIZE` E O `GL_RESCALE_NORMAL` NAO MUDAM NENHUMA COR, e isso fica
// dito por inteiro: os dois pedem uma normal UNITARIA a chegar a equacao, e ela
// chega sempre (a equacao normaliza o vector, que e a receita verificada no
// zeebx). O pedido esta satisfeito -- o que nao existe e um efeito separado
// deles.
//
// O QUE FICOU DE FORA, ESCRITO AQUI PORQUE E A UNICA FORMA DE NAO SER LIDO COMO
// FEITO (P2). Cada linha nomeia o que nao existe, e nao "o que falta":
//
//   1. PERSPECTIVA: a interpolacao de textura e cor E CORRIGIDA POR PERSPECTIVA
//      (atributos divididos por w no rasterizador).
//   2. RECORTE DO PLANO PROXIMO: triangulos que cruzam o plano proximo
//      (clip.z + clip.w >= 0) sao recortados (Sutherland-Hodgman) gerando poligonos
//      de 3 ou 4 vertices com atributos interpolados. Triangulos totalmente atras sao
//      descartados (contados em `TriangulosDescartados()`). Um SEGMENTO que cruza o
//      plano proximo e cortado no ponto onde `z + w` passa por zero (a forma
//      parametrizada, com a conta em `RasterizarSegmento`) e um segmento todo atras
//      e descartado (`SegmentosDescartados()`). Os outros 5 planos do frustum
//      continuam tratados por descarte de caixa delimitadora no viewport.
//   3. SEM STENCIL, SEM DITHER, SEM POLYGON OFFSET e SEM SCISSOR: as quatro
//      ficam nomeadas no traco, uma vez cada (`CapacidadesPorFazer()`), e nao em
//      silencio. A MISTURA, O ALPHA TEST E A MASCARA DE COR SAIRAM desta lista
//      -- sao feitas (ver a frente rast2 acima).
//   4. SEM NEVOA (`GL_FOG`): a cor vem do `glColor4x`, do array de cores ou da
//      ILUMINACAO. O `GL_LIGHTING` SAIU desta lista -- e feito.
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
//      onde ser guardado: `Alpha` e DESCARTADO NA ESCRITA. Ele NAO e descartado
//      antes disso: e interpolado por vertice e e o alfa do fragmento que a
//      MISTURA e o ALFA TEST leem. Com o alfa fixo a 255 (o que se fazia antes
//      desta frente), um `GL_SRC_ALPHA` valia sempre 1 e o `GL_ALPHA_TEST`
//      nunca descartava nada -- duas capacidades ligadas e inertes.
//   9. O `glColorMaterial(face, modo)` NAO E SERVIDO e nao esta entre os slots do
//      `AEEGL.h`: com o `GL_COLOR_MATERIAL` ligado admite-se o MODO POR OMISSAO
//      do GL, `GL_AMBIENT_AND_DIFFUSE` (a cor do vertice toma o lugar da
//      ambiente E da difusa). Um titulo que peca outro modo fica com o
//      por-omissao, e nao com um modo inventado.
//
// A SUPERFICIE. A escrita passa por uma interface pequena (`Superficie`) porque
// um teste tem de poder afirmar OS PIXELS EXACTOS -- coordenada e cor, um a um --
// e a `core/brew/tela.h` (ficheiro partilhado, com testes proprios) nao expoe
// nenhuma leitura de pixel. Em producao a implementacao e UMA (`DestinoTela`,
// que escreve mesmo na `Tela`); o `Superficie` NAO e um segundo framebuffer.

#include <cstdint>
#include <memory>
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
  // LER O PIXEL QUE JA LA ESTA. Existe por causa de DUAS capacidades que o GL
  // define contra o destino, e nao contra um valor calculado:
  //
  //   - a MISTURA (`GL_BLEND`): os factores `GL_DST_*` sao o destino, e a soma
  //     `origem*f_origem + destino*f_destino` precisa dele;
  //   - a MASCARA DE COR (`glColorMask`): os canais proibidos ficam com o que
  //     uma passada ANTERIOR deixou ali -- escrever o valor calculado neles
  //     apagaria o desenho que se queria preservar.
  //
  // Sem uma leitura, a alternativa era um segundo framebuffer dentro do
  // rasterizador -- e esse diverge da Tela no primeiro pixel que o guest
  // escreva (`Tela::AbsorverDe565`), o que daria uma medida e um desenho
  // diferentes da tela que a bateria le.
  virtual std::uint32_t Ler(int x, int y) const = 0;
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
  // A LEITURA DO PIXEL E A PROPRIA `Tela` (`Tela::PixelEm`): uma segunda copia
  // do estado dos pixels seria uma segunda verdade (ver `Ler` na `Superficie`).
  std::uint32_t Ler(int x, int y) const override {
    return tela_ == nullptr ? 0u : tela_->PixelEm(x, y);
  }

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

// O PAR (formato do texel, tipo do elemento) QUE O `AmostrarTextura` SABE LER.
//
// UMA LISTA, E NAO DUAS: o `Rasterizador::Desenhar` usa-a como guarda (uma
// textura que ele nao saiba ler RECUSA o desenho, e nao o desenha com a cor do
// vertice em silencio) e o `Igl::MontarEstado` usa-a para decidir se a textura
// entra no retrato do desenho. Duas copias desta condicao divergiriam em
// silencio, e o sintoma seria um titulo sem textura (ou com a textura errada)
// sem nada a apontar para a causa.
bool TexturaAmostravel(std::uint32_t formato, std::uint32_t tipo);

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
  // Textura comprimida ja decodificada, de posse do host. Texturas comuns
  // continuam lendo o ponteiro do guest para preservar sua semantica atual.
  std::shared_ptr<const std::vector<Rgba>> texels_descodificados;
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
  // O viewport omisso e o ECRA INTEIRO (`ecra.h`), como o GL manda: nao ha aqui
  // um 640x480 escrito a mao.
  std::uint32_t viewport[4] = {0, 0, zb2::brew::kLarguraDoEcra, zb2::brew::kAlturaDoEcra};

  Rgba cor = {255, 255, 255, 255};
  bool cor_por_vertice = false;
  // O `glClear`: a cor e a mascara de limpeza que o `igl.cpp` acumulou.
  Rgba cor_de_limpeza = {0, 0, 0, 0};
  std::uint32_t mascara_de_limpeza = 0;
  ArrayDoCliente vertices, cores, coordenadas_de_textura;
  // Unidade 0 mantem estes nomes para os testes e clientes existentes. Unidade
  // 1 tem estado proprio; a composicao aplica 0 e depois 1, como o pipeline ES.
  bool textura_ligada = false;
  Textura textura;
  std::uint32_t ambiente_de_textura = 0x1E01u;  // GL_REPLACE (legado unit0)
  bool textura1_ligada = false;
  Textura textura1;
  ArrayDoCliente coordenadas_de_textura1;
  std::uint32_t ambiente_de_textura1 = 0x1E01u;  // GL_REPLACE
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

  // --- A FAIXA DE PROFUNDIDADE (`glDepthRange`) -----------------------------
  //
  // A OMISSAO DO GL E (0, 1). O `z` de janela que sai da projeccao vai de 0 a 1 e
  // e' ESTA faixa que o mapeia para o que o buffer de profundidade guarda. Estava
  // guardada no `igl.cpp` e **nao aplicada** (no grupo dos "so acumulam
  // parametros", com o comentario "o consumidor nao existe") -- um pedido do
  // guest respondido com "feito" e sem efeito, que e o defeito P2 desta casa.
  //
  // A FONTE e o `zeebx` do Kaio (`89dcd0f`): ele implementou-a porque o Crash
  // Nitro Kart ALTERNA faixas para por o brilho do kart por cima do cenario --
  // sem ela, o brilho fica escondido ou tapa o que devia estar a frente.
  float profundidade_perto = 0.0f;
  float profundidade_longe = 1.0f;

  // --- O SCISSOR (`glScissor` + `GL_SCISSOR_TEST`) ---------------------------
  //
  // A tesoura do desenho. O `y` conta de BAIXO, como o `glViewport`. Estava
  // "guardado e nao aplicado", com a justificacao de que ignorar o recorte e
  // "desenhar a mais" -- e o `zeebx` do Kaio mediu que nao e (`c278514`): no PEGGLE
  // o jogo desenha a FOLHA DE FONTES INTEIRA e conta com a tesoura para aparecer uma
  // letra so, e ignorada "cada letra punha a folha inteira na tela".
  std::uint32_t scissor[4] = {0, 0, 0, 0};
  bool teste_de_scissor = false;

  // --- A LARGURA DA LINHA (`glLineWidthx`) ---------------------------------
  //
  // A OMISSAO DO GL E 1.0, e o campo existe porque o pedido do guest TEM de
  // chegar ao desenho: ate esta frente o `kIgl_LineWidthx` guardava o valor em
  // `parametros_` e respondia "feito", e o rasterizador nao tinha onde o ler -- o
  // pedido estava guardado e NAO era aplicado, sem o dizer (o padrao P2).
  //
  // LARGURA <= 1 -> linha de um pixel. LARGURA > 1 -> o desenho RECUSA com o
  // nome: uma linha de 2 px desenhada com 1 px seria uma mentira silenciosa.
  float largura_de_linha = 1.0f;

  // --- A MISTURA (`GL_BLEND`) ----------------------------------------------
  //
  // A OMISSAO DO GL E A COPIA: `glBlendFunc(GL_ONE, GL_ZERO)`. Um titulo que
  // ligue o `GL_BLEND` sem chamar o `glBlendFunc` fica com o pixel da origem, e
  // e isso que um rasterizador que so sabe copiar ja fazia.
  bool mistura_ligada = false;
  std::uint32_t mistura_fonte = 0x0001u;    // GL_ONE
  std::uint32_t mistura_destino = 0x0000u;  // GL_ZERO

  // --- O ALPHA TEST --------------------------------------------------------
  //
  // A omissao do GL e `GL_ALWAYS`, com a referencia a zero: um titulo que ligue
  // o `GL_ALPHA_TEST` sem chamar o `glAlphaFunc` nao descarta nada -- e o teste
  // que prova o descarte tem de por a funcao e a referencia, ou passaria com a
  // guarda arrancada (foi o caso medido do `GetNextButtonEvent`).
  bool teste_de_alfa = false;
  std::uint32_t funcao_de_alfa = 0x0207u;  // GL_ALWAYS
  float alfa_de_referencia = 0.0f;         // [0,1], como o `glAlphaFuncx` o da

  // --- A MASCARA DE COR ----------------------------------------------------
  //
  // Um bit por canal, na ordem dos argumentos do `glColorMask(red, green,
  // blue, alpha)`: bit 0 vermelho, 1 verde, 2 azul, 3 alfa. A omissao e `0xF`
  // (escreve tudo). O bit do ALFA nao tem onde ser guardado (a tela e RGB565 e
  // nao ha buffer de alfa) e por isso nao muda nenhum pixel -- fica dito.
  std::uint32_t mascara_de_cor = 0x0000000Fu;

  // --- A ILUMINACAO --------------------------------------------------------
  //
  // O material e a luz ficam AQUI, e nao uma referencia ao `Igl`: o retrato tem
  // de ser imune a uma mudanca de estado a meio do desenho, como o resto.
  struct Aparencia {
    float ambiente[4] = {0.2f, 0.2f, 0.2f, 1.0f};
    float difusa[4] = {0.8f, 0.8f, 0.8f, 1.0f};
    float especular[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float emissao[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float brilho = 0.0f;  // GL_SHININESS
  };
  struct Luz {
    bool ligada = false;  // `glEnable(GL_LIGHT0 + i)`
    float ambiente[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float difusa[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float especular[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    // EM COORDENADAS DE OLHO: e assim que o `glLightfv(GL_POSITION)` as guarda
    // (a posicao passa pela modelview do instante da chamada). O w diz se a luz
    // e direccional (0) ou posicional (1).
    float posicao[4] = {0.0f, 0.0f, 1.0f, 0.0f};
    float direcao_do_holofote[3] = {0.0f, 0.0f, -1.0f};
    float exponente_do_holofote = 0.0f;
    float corte_do_holofote = 180.0f;
    float atenuacao[3] = {1.0f, 0.0f, 0.0f};  // constante, linear, quadratica
  };
  bool iluminacao_ligada = false;
  bool cor_do_material = false;     // GL_COLOR_MATERIAL
  bool normalizar_normais = false;  // GL_NORMALIZE
  bool reescalar_normais = false;   // GL_RESCALE_NORMAL
  // A luz DO MATERIAL (uma so: a face e ignorada, como no ES 1.x) e as OITO
  // luzes que a especificacao garante. A omissao da ambiente da cena e a do GL.
  Aparencia material;
  Luz luzes[8];
  float ambiente_da_cena[4] = {0.2f, 0.2f, 0.2f, 1.0f};
  // O ARRAY DE NORMAIS. Sem ele o GL usa o vector (0, 0, 1) por vertice -- e
  // essa a omissao, e nao um valor inventado (medido: gof e rmp ligam o
  // `GL_LIGHTING` e NUNCA chamam o `glNormalPointer` nem o `glNormal3x`).
  ArrayDoCliente normais;

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
  // OS SEGMENTOS TEM CONTADORES PROPRIOS, e nao os dos triangulos: um contador
  // chamado `Triangulos()` a contar linhas seria a contabilidade a mentir (o
  // traco do fim diz qual dos dois conta, e o teste le-o).
  std::uint64_t Segmentos() const { return segmentos_; }
  std::uint64_t SegmentosDescartados() const { return segmentos_descartados_; }
  std::uint64_t SegmentosRecortados() const { return segmentos_recortados_; }
  std::uint64_t PrimitivasRecusadas() const { return recusadas_; }
  // Quantos fragmentos o ALPHA TEST descartou. E um contador proprio porque o
  // efeito dele e a AUSENCIA de escrita: sem ele, um alpha test que descarta
  // tudo e um alpha test que nao existe dao o mesmo `Pixels()`.
  std::uint64_t FragmentosDescartados() const { return descartados_alfa_; }

 private:
  struct Vertice {
    float clip[4] = {0, 0, 0, 1};  // depois de P * M * v
    float x = 0, y = 0, z = 0;     // em coordenadas de janela
    // A POSICAO NO ESPACO DO OLHO (M * v). E la que a luz do GL vive: as
    // posicoes das luzes sao guardadas em coordenadas de olho, o observador
    // esta na origem, e a atenuacao e o holofote medem-se daqui. Sem este
    // vector nao ha iluminacao possivel -- so uma cor de fantasia.
    float olho[4] = {0, 0, 0, 1};
    Rgba cor;
    // A NORMAL, ja transformada para o espaco do olho (e normalizada, se o
    // `GL_NORMALIZE` estiver ligado). A omissao do GL e (0, 0, 1).
    float normal[3] = {0.0f, 0.0f, 1.0f};
    float u[2] = {0, 0}, v[2] = {0, 0};
  };

  bool LerVertice(const EstadoDeRasterizacao& e, std::uint32_t indice, Vertice* v,
                  std::string* motivo) const;
  bool Projetar(const EstadoDeRasterizacao& e, Vertice* v) const;
  void RasterizarTriangulo(const EstadoDeRasterizacao& e, const Vertice& a, const Vertice& b,
                           const Vertice& c);
  // UM SEGMENTO: recorte do plano proximo, projeccao dos dois extremos e o DDA
  // no eixo maior. `incluir_fim` diz se o pixel do PONTO FINAL e escrito: e falso
  // em todos os segmentos menos na ultima ponta de uma `GL_LINE_STRIP` (ver o
  // comentario da funcao, com a derivacao do recorte e o desvio declarado).
  void RasterizarSegmento(const EstadoDeRasterizacao& e, const Vertice& a, const Vertice& b,
                          bool incluir_fim);
  void EscreverPixel(const EstadoDeRasterizacao& e, int x, int y, float profundidade, Rgba cor);
  // A cor de um vertice com o `GL_LIGHTING` ligado, na equacao do GL ES 1.x.
  static Rgba CorIluminada(const EstadoDeRasterizacao& e, const float olho[4],
                           const float normal[3], const Rgba& cor_do_vertice);
  // A normal em coordenadas de OLHO: `transposta_da_inversa(M3x3) * n`, com o
  // reescalonamento (`GL_RESCALE_NORMAL`) e a normalizacao (`GL_NORMALIZE`)
  // aplicados na ordem do GL.
  static void TransformarNormal(const EstadoDeRasterizacao& e, const float n[3], float saida[3]);
  // A normal do vertice, EM COORDENADAS DE OBJECTO: do array ligado, ou a
  // omissao do GL (0, 0, 1) quando nao ha array. Um array com um tipo sem
  // caminho RECUSA com o nome -- nao se inventa o valor de uma normal.
  bool LerNormalDeObjeto(const EstadoDeRasterizacao& e, std::uint32_t indice, float n[3],
                         std::string* motivo) const;
  Rgba AmostrarTextura(const Textura& textura, float u, float v) const;
  Rgba ComporTexturas(const EstadoDeRasterizacao& e, Rgba cor, float u0, float v0,
                      float u1, float v1) const;
  void PrepararProfundidade();

  static Vertice InterpolarVertice(const Vertice& a, const Vertice& b, double t);
  int RecortarPlanoProximo(const Vertice* entrada, Vertice* saida) const;

  Memoria& mem_;
  Superficie& superficie_;
  std::vector<float> profundidade_;
  int largura_ = 0, altura_ = 0;
  std::uint64_t pixels_ = 0, triangulos_ = 0, descartados_ = 0, recortados_ = 0, recusadas_ = 0;
  std::uint64_t descartados_alfa_ = 0;
  std::uint64_t segmentos_ = 0, segmentos_descartados_ = 0, segmentos_recortados_ = 0;
};

}  // namespace zb2::video

#endif  // ZB2_CORE_VIDEO_RASTERIZADOR_H
