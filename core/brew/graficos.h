#ifndef ZB2_CORE_BREW_GRAFICOS_H
#define ZB2_CORE_BREW_GRAFICOS_H

// O `IGraphics`: a interface 2D do BREW 4.0 (`AEEGraphics.h`), o objecto que o
// `AEECLSID_GRAPHICS` (`AEEClassIDs.h:158`) devolve.
//
// PORQUE ISTO EXISTE, e a medicao. Na corrida de referencia deste ramo
// (`/tmp/corrida_sw.json`, 62 titulos, `ZB2_QUADROS=300 ZB2_EVT_START=1`) ha UM
// titulo que desenha por esta interface, e desenha 296 vezes em cada um destes
// quatro slots:
//
//     IGraphics::slot4   296x   r1=0x86 r2=0x69 r3=0xff      -> SetColor(r,g,b,alpha)
//     IGraphics::slot6   297x   r1=1                          -> SetFillMode(fFill)
//     IGraphics::slot8   297x   r1=0x86 r2=0x69 r3=0xff      -> SetFillColor(r,g,b,alpha)
//     IGraphics::slot22  296x   r1=0x8007fe60 (rect na pilha) -> DrawRect(po, AEERect *)
//
// (o titulo e o `allstarcards`, 280173; os outros 61 nao tocam nesta interface).
// O objecto e o generico de indice 3 (`kGenericos`, `core/brew/interface.cpp`),
// portanto 0x80060300, e ate esta frente o despacho respondia-lhe
// `kAeeUnsupported` em TODOS os slots -- a interface era uma fachada.
//
// ---------------------------------------------------------------------------
// AS ORDENS (lidas, nao escritas a mao)
// ---------------------------------------------------------------------------
//
// Os numeros de slot NAO estao aqui escritos: vem de `tools/brew_slots.inc`,
// que o `tools/gerar_slots.py` le do cabecalho. Este cabecalho usa a forma
// ANTIGA da interface (`QINTERFACE(IGraphics)` + `DECLARE_IBASE`), e o gerador
// ja sabia le-la pelo `ITextCtl`; a interface foi acrescentada a lista dele
// nesta frente, e o resultado esta no `.inc`:
//
//     AddRef 0, Release 1, SetBackground 2, GetBackground 3, SetColor 4,
//     GetColor 5, SetFillMode 6, GetFillMode 7, SetFillColor 8, GetFillColor 9,
//     SetPointSize 10, GetPointSize 11, SetClip 12, GetClip 13, SetViewport 14,
//     GetViewport 15, ClearViewport 16, SetPaintMode 17, GetPaintMode 18,
//     GetColorDepth 19, DrawPoint 20, DrawLine 21, DrawRect 22, DrawCircle 23,
//     ... 43 ao todo (`kGraphicsSlots`).
//
// **Um slot deslocado aqui nao da erro: da desenho errado.** E por isso que a
// tabela e gerada e que os 44 nomes entram no `.inc` (`NomeDeGraphics`), para a
// recusa poder dizer o NOME do que nao esta feito em vez de um numero.
//
// ---------------------------------------------------------------------------
// A TELA: ONDE OS PIXEIS SAO ESCRITOS (e o caminho que faltou)
// ---------------------------------------------------------------------------
//
// O rasterizador escreve na `Tela` do hospedeiro (`core/brew/tela.h`), que o
// `Despacho` tem por membro PRIVADO e liga ao IGL (`igl_.DefinirTela(&tela_)`).
// **Dos ficheiros que esta frente pode tocar (`core/brew/graficos.*`,
// `core/brew/widget.*`) a `Tela` nao e alcancavel**: nao ha acessor para ela no
// `Despacho` e o `igl.h` tambem nao esta aberto. O caminho que falta e uma
// linha -- `Despacho::InstalarWidgets` a passar a `Tela&`, ou um
// `Despacho::AtenderGraficos` na cadeia de atendimento -- e fica escrito aqui
// como DIVIDA, em vez de se inventar um segundo framebuffer.
//
// O QUE SE FAZ ENTRE TANTO, e nao e uma tela nova: escreve-se no BUFFER DO ECRA
// NO GUEST (`kBaseDoEcraNoGuest`, `core/brew/ecra.h:61`, RGB565, 640x480), que e
// a MESMA memoria onde o 3D acaba (o `ExporEcraAoGuest` copia a `Tela` para la) e
// a memoria que o titulo le pelo `pBmp` do ecra. O precedente esta na arvore: o
// `glDrawTex*OES` (`core/brew/classes.cpp:1091-1093`) escreve exactamente ali
// pelo mesmo motivo. O que fica dito, e medido: esses pixeis sao ABSORVIDOS pela
// `Tela` no `AbsorverEcraDoGuest` e contam como desenho (`pixels`/`cores` da
// bateria) -- logo o instrumento conta o desenho 2D como se o guest o tivesse
// feito. O numero nao mente sobre "houve desenho"; mente sobre QUEM desenhou.
//
// A JANELA E A MESMA DOS DOIS EMULADORES DE REFERENCIA: x da esquerda, y de
// CIMA. O `AEEGraphics.h` contradiz-se em duas linhas -- o comentario do
// `Translate` (`:250`) diz "the origin is at (0,0), which is at the lower left
// corner of the window", e a documentacao do mesmo `Translate` (`:48456`) diz
// "The window's origin is always its upper left corner". O que os dois
// emuladores fazem e o segundo: `zeebx` (`src/video/display.rs:150`,
// `pixels[(y*width)+x]`) e `zeemu` (`brew/BrewGraphics.cpp:281-302`, `y +=
// viewport_y + translate_y` e escrita directa na linha y). O y NAO se inverte, e
// a contradicao fica registada no relatorio.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/brew/ecra.h"
#include "core/brew/interface.h"
#include "core/cpu/cpu.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"
#include "tools/brew_slots.inc"

namespace zb2::brew {

// O `Atendido` vive em `core/brew/widget.h` (e o resultado de um pedido de
// QUALQUER modulo desta familia). Aqui so se declara que existe: uma segunda
// copia do enum seria duas listas a ter de concordar, e esta arvore ja pagou
// esse defeito.
enum class Atendido;

// O `IGraphics` e o `kGenericos[kIndiceDoGraphics]` (`interface.cpp`), e o IID e o
// `AEECLSID_GRAPHICS` (`AEEClassIDs.h:158`, `0x01002001`). O `despacho.cpp` ja
// conhece a classe (`ClasseConhecida`) e ja devolve o objecto; o que faltava era
// alguem ATENDER os slots.
//
// O IID NAO se repete aqui: `despacho.cpp:72` e `tools/bateria.cpp:196` ja o tem
// cada um o seu, e um terceiro `kIidGraphics` neste cabecalho colidia com eles
// (`despacho.cpp` inclui este ficheiro pelo `widget.h`, e o compilador acusou
// "reference to `kIidGraphics` is ambiguous"). Quem precisa do numero le-o da
// TABELA (`kGenericos[kIndiceDoGraphics].iid`), que e a fonte que diz qual
// objecto o `CreateInstance` entrega -- e e o que o teste faz.
constexpr std::uint32_t kIndiceDoGraphics = 3;

// ---------------------------------------------------------------------------
// OS `nFlag` DO `SetClip`/`SetViewport` (`AEEGraphics.h:26-29`)
// ---------------------------------------------------------------------------
constexpr std::uint32_t kGraphicsNone = 0x00;
constexpr std::uint32_t kGraphicsFrame = 0x02;  // desenha a moldura
constexpr std::uint32_t kGraphicsClear = 0x04;  // limpa o interior
constexpr std::uint32_t kGraphicsFill = 0x08;   // preenche o interior

// Os dois tipos de clip que este modulo aplica (`AEEGraphics.h:36-45`):
// `CLIPPING_NONE` (0) usa a janela e `CLIPPING_RECT` (1) e um rectangulo. As
// outras cinco formas entram na tabela de recusa COM O NOME.
constexpr std::uint8_t kClipNenhum = 0;
constexpr std::uint8_t kClipRectangulo = 1;

// ---------------------------------------------------------------------------
// A `AEEClip` DO GUEST, CAMPO A CAMPO (`AEEGraphics.h:151-163`)
// ---------------------------------------------------------------------------
//
//     typedef struct _clipshape {
//        AEEClipShape   type;      // int8 num alvo ARM (`AEEGraphics.h:47`)
//        union { AEERect rect; ... } shape;
//     } AEEClip;
//
// O `union` comeca no +2 e nao no +1: TODOS os seus membros comecam por `int16`
// (`AEERect` = x,y,dx,dy; `AEECircle` = cx,cy,r; ...), logo o alinhamento dele e
// 2 e o compilador poe um byte de enchimento a seguir ao `type`. O tamanho e
// 2 + 8 = 10. **Um deslocamento escrito a mao ja custou uma ronda nesta arvore**
// (era o caso da `AEERect` com 16 bytes em vez de 8, `core/brew/widget.cpp`),
// e por isso o teste escreve uma SENTINELA depois dos 10 bytes: uma struct maior
// do que o que este modulo escreve apanha-se ali.
constexpr std::uint32_t kClipTipo = 0;
constexpr std::uint32_t kClipRect = 2;
constexpr std::uint32_t kTamanhoDoClip = 10;

// ---------------------------------------------------------------------------
// A `AEERect` DO GUEST: QUATRO `int16` (8 BYTES)
// ---------------------------------------------------------------------------
//
// A mesma verdade de `core/brew/widget.cpp` (onde o defeito dos 16 bytes se
// pagou) e agora UMA so funcao, usada pelos dois modulos: o `IRootForm` escreve
// a extensao de um widget e o `IGraphics` le o rect do `DrawRect`.
bool LerRectDoGuest(const Memoria& mem, std::uint32_t onde, int* x, int* y, int* largura,
                    int* altura);
void EscreverRectDoGuest(Memoria& mem, std::uint32_t onde, int x, int y, int largura,
                         int altura);

// Uma cor RGBA de 8 bits por canal, como o SDK a entrega (quatro `uint8` soltos,
// e nao um `RGBVAL`). O `RGBVAL` so existe na SAIDA: `MAKE_RGBA(r,g,b,a) =
// (r<<8)|(g<<16)|(b<<24)|a` (`AEERGBVAL.h:20`).
struct CorRgba {
  std::uint8_t r = 0, g = 0, b = 0, a = 0;
};

// O que este modulo guarda e o que ele ja desenhou. Os testes e o relatorio leem
// daqui em vez de espreitarem os privados.
struct ResumoDeGraficos {
  CorRgba cor_de_frente;
  CorRgba cor_de_preenchimento;
  CorRgba cor_de_fundo;
  bool preenche = false;
  std::uint32_t tamanho_do_ponto = 0;
  std::uint32_t modo_de_pintura = 0;
  // O clip em vigor: 0 = a janela (o ecra), 1 = um rectangulo.
  std::uint32_t clip_tipo = 0;
  std::uint32_t clip_x = 0, clip_y = 0, clip_largura = 0, clip_altura = 0;
  // O viewport em vigor (o ecra, por omissao) e se foi pedido com moldura.
  std::uint32_t viewport_x = 0, viewport_y = 0, viewport_largura = 0, viewport_altura = 0;
  bool viewport_com_moldura = false;
  std::uint32_t rectangulos = 0;      // `DrawRect` atendidos
  std::uint32_t rectangulos_vazios = 0;
  // `SetViewport` recusado pelo CONTRATO (fora do ecra, ou pequeno de mais para a
  // moldura). Nao e uma falta: o cabecalho manda devolver FALSE -- mas conta-se,
  // porque um numero que nao se ve e um numero que nao existe.
  std::uint32_t viewports_recusados = 0;
  std::uint32_t pixeis_escritos = 0;  // pixeis que ESTE modulo escreveu na tela
};

class Graficos {
 public:
  Graficos(Memoria& mem, Traco& traco);

  // O DESTINO DO DESENHO 2D. O modulo escreve no buffer do ecra do GUEST
  // (`kBaseDoEcraNoGuest`) e a `Tela` do hospedeiro absorve-o no `Update`. Essa
  // pagina so nasce quando alguem pede o bitmap do ecra -- e ha titulos que
  // DESENHAM sem nunca o pedir (medido: `allstarcards`, 296 `DrawRect` por
  // quadro e zero pedidos). Sem este criador o desenho nao teria destino e o
  // modulo recusaria por nome; com ele, o ecra existe quando o titulo o usa.
  // E o `Despacho` que o liga (e ele que sabe escrever o cabecalho do IDIB do
  // ecra); aqui fica so a porta.
  void DefinirCriadorDoEcra(std::function<std::uint32_t()> f) { criar_ecra_ = std::move(f); }

  // Guarda a faixa de saida (uma COPIA: a `Saidas` que a bateria usa vive no
  // ambito do titulo, e um ponteiro guardado de um titulo para o outro apontaria
  // para memoria morta -- foi um defeito medido nesta arvore) e confere que ela
  // tem espaco para os 64 slots do objecto. NAO confere o objecto: quem constroi
  // os genericos e a ferramenta, e no `InstalarAjudantes` (que corre ANTES desse
  // laco, `tools/bateria.cpp:633` contra `:769`) ele ainda nao existe. A
  // cablagem e conferida no PRIMEIRO pedido -- que e quando ela tem de estar
  // certa.
  bool Construir(const Saidas& saidas, std::string* motivo);

  // O indice pertence a este modulo? (a vtable do generico 3 sao 64 slots)
  bool EMeu(std::uint32_t indice) const;

  Atendido Atender(ICpu& cpu, std::uint32_t indice);

  // --- o que se mediu (P7) -------------------------------------------------
  const ResumoDeGraficos& Resumo() const { return estado_; }
  std::uint32_t PixeisEscritos() const { return estado_.pixeis_escritos; }
  std::uint32_t Rectangulos() const { return estado_.rectangulos; }
  // Tudo o que ESTE modulo recusou, por nome (o `Traco` ja conta a global).
  const std::vector<std::string>& Recusas() const { return recusas_; }
  // A tela do guest esta alcancavel? (sem ela nao ha desenho possivel)
  bool TemTela() const { return tem_tela_; }

 private:
  // --- as cores ------------------------------------------------------------
  static std::uint32_t ParaRgbval(const CorRgba& c, bool com_alfa);
  static std::uint16_t ParaCor565(const CorRgba& c);
  // `0x0106c3fe`-style: o `AEEGraphics` so tem 8 bits por canal e a tela RGB565.
  // A truncagem e a MESMA da `Tela::RgbvalPara565` e do rasterizador: descarta os
  // bits baixos, nao arredonda. Duas conversoes diferentes davam dois pixeis
  // diferentes para a mesma cor pedida.

  // --- a cablagem do objecto ----------------------------------------------
  // A vtable do objecto TEM de apontar para o indice que esta a ser atendido.
  // Um pedido que chegue aqui por um slot que a vtable nao liga a nos e um
  // defeito de cablagem, e o silencio dele seria "o modulo atende indices que o
  // guest nunca usa". Conferido UMA vez por slot.
  bool ConferirSlot(std::uint32_t slot);

  // --- a tela --------------------------------------------------------------
  // Um `bool` decidido UMA vez (e nao um `mem_.Existe` por pixel).
  void LigarATela();
  void RecalcularCaixa();
  void EscreverPixel(int x, int y, std::uint16_t cor);
  void PreencherRect(int x, int y, int largura, int altura, std::uint16_t cor);
  void MolduraRect(int x, int y, int largura, int altura, std::uint16_t cor);
  // O clip em vigor: viewport x clip x ecra. Devolve falso se o pixel fica fora.
  bool DentroDoClip(int x, int y) const;

  // --- os ramos ------------------------------------------------------------
  Atendido AtenderEstado(ICpu& cpu, std::uint32_t slot);
  Atendido DesenharRect(ICpu& cpu);
  Atendido NaoImplementado(ICpu& cpu, std::uint32_t slot);
  // Escreve um campo de 8 bits do guest, se o ponteiro o permitir. Um ponteiro
  // fora do mapa NAO se escreve (o `Memoria::Escrever8` ALOCA a pagina, logo
  // escrever num ponteiro invalido fabricava memoria que ninguem pediu).
  bool EscreverByteDoGuest(std::uint32_t onde, std::uint8_t v, bool* recusado);
  // O alfa de `SetColor`/`SetFillColor` -- o QUINTO argumento, que o AAPCS poe na
  // PILHA. Ver o comentario longo no `.cpp`, com o desmonte do sitio de chamada.
  std::uint8_t AlfaDaPilha(ICpu& cpu) const;
  void RegistarRecusa(const std::string& nome, const std::string& detalhe);

  // Os `nFlag` de um `SetClip`: desenha enquanto muda o estado. Um so sitio faz
  // as tres coisas, para o `SetClip` e o `SetViewport` nao divergirem.
  void AplicarFlagsDoRect(int x, int y, int largura, int altura, std::uint32_t nflag);

  Memoria& mem_;
  Traco& traco_;
  Saidas saidas_;
  bool pronto_ = false;
  bool tem_tela_ = false;
  std::function<std::uint32_t()> criar_ecra_;
  // Uma mascara: o bit `slot` esta posto quando a cablagem desse slot ja foi
  // conferida. 64 slots = um `uint64_t`.
  std::uint64_t slots_conferidos_ = 0;
  // A CAIXA EM VIGOR (viewport x clip x ecra), recalculada quando o estado muda
  // e usada em cada pixel. Um limite calculado por pixel seria o defeito que a
  // `Tela::Retangulo` ja pagou.
  int caixa_x0_ = 0, caixa_y0_ = 0, caixa_x1_ = 0, caixa_y1_ = 0;
  ResumoDeGraficos estado_;
  std::vector<std::string> recusas_;
  // O `DrawRect` do `allstarcards` chega 296 vezes por quadro: o traco guarda os
  // primeiros oito rects (com os valores) e cala-se, para a medicao poder dizer
  // ONDE o titulo desenha sem encher o traco de 300 quadros.
  std::uint32_t rects_tracados_ = 0;
};

}  // namespace zb2::brew

#endif  // ZB2_CORE_BREW_GRAFICOS_H
