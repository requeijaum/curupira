#ifndef ZB2_CORE_BREW_IGL_H
#define ZB2_CORE_BREW_IGL_H

// O IGL: a interface de OpenGL ES 1.x do BREW.
//
// PORQUE ESTE FICHEIRO EXISTE. Na arvore antiga, **71 dos 84 handlers de GL nao
// tinham log nenhum**, e o `glCullFace` descartou **86 377 chamadas em silencio**
// por causa de um stub que devolvia sucesso e nao fazia nada. A reescrita existe
// em boa parte por causa disto: aqui nao ha caminho mudo. Cada chamada registra o
// NOME do metodo e os ARGUMENTOS, e cada caminho sem implementacao RECUSA.
//
// COMO O GUEST CHEGA AQUI, medido em `ddragonz.mod` (nao deduzido). O desmonte do
// modulo, com o ficheiro carregado em 0x00100000, mostra em 0x11d61c-0x11d634:
//
//     ldr  r0, [r5, #12]     ; o pIShell, dentro do contexto da funcao de init
//     ldr  r1, [r0]          ; a vtable do IShell
//     ldr  r3, [r1, #8]      ; slot 2 = CreateInstance (kShell_CreateInstance = 2)
//     ldr  r1, [pc, #824]    ; 0x11d970 = 0x01014bc3 = AEECLSID_GL (AEEGL.h)
//     bx   r3                ; CreateInstance(pIShell, GL, &pIGL)
//
// O jogo compila o wrapper do SDK (`GLES_1x.c`) DENTRO do proprio `.mod`, e o
// wrapper faz `IGL_glClear(GPIGL, mask)`, que e
// `AEEGETPVTBL(p,IGL)->glClear(mask)`. Logo: o objecto IGL vem de
// `IShell::CreateInstance(AEECLSID_GL)`, o endereco fica em `gpIGL` (dentro do
// modulo) e cada chamada `gl*` le a vtable desse objecto e salta para o slot.
//
// O `po` NAO E PASSADO AOS METODOS `gl*`. Medido em duas fontes que concordam: no
// desmonte de `conftest.elf` (o exemplo do proprio SDK), o `glCullFace` em
// 0x28500 faz `ldr r1,[pc,#16]` / `add r1,pc,r1` = 0x2a5fc (exactamente o
// endereco que `nm` da a `gpIGL`) / `ldr r1,[r1]` / `ldr r1,[r1]` /
// `ldr r1,[r1,#76]` / `bx r1` -- seis instrucoes que NAO tocam no r0, que ja leva
// o primeiro argumento a serio. So os slots da cabeca (AddRef, Release,
// QueryInterface) recebem o `po`.
//
// A TABELA DE SLOTS VEM DE `tools/gl_slots.inc`, GERADO de `AEEGL.h` por
// `tools/gerar_slots.py` -- e conferida contra os 102 thunks do `conftest.elf`
// pela guarda `tools/verificar_slots_gl.sh`. **Um numero de slot escrito a mao ja
// divergiu uma vez neste trabalho** (`INHERIT_IBase` tem DOIS membros e eu contava
// tres, o que deslocou todos os slots do IShell por um), e o custo foi uma ronda
// inteira a olhar para o sitio errado. Copiar a ordem de outro emulador -- que foi
// o que aconteceu com o mapa do IGLES11 na arvore antiga -- esta proibido POR
// TESTE, e nao por disciplina.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core/cpu/cpu.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"
#include "core/video/rasterizador.h"
#include "tools/gl_slots.inc"

namespace zb2::brew {

// Os dois AEECLSID, de `AEEGL.h`:
//     #define AEECLSID_GL    0x01014bc3   //| OpenGLES 1.0 Common-Lite spec
//     #define AEECLSID_EGL   0x01014bc4   //| EGL 1.0 spec
constexpr std::uint32_t kClsidIgl = 0x01014bc3u;
constexpr std::uint32_t kClsidIegl = 0x01014bc4u;

// A FAIXA DE SAIDA. Um endereco de saida por SLOT, como no resto deste emulador:
// com um stub unico, o registo diz "algo de GL" e o numero perde o nome.
//
// A faixa fica ACIMA de todas as que o `despacho.cpp` usa (2000 a 9472). Isso nao
// e estetica: o despacho testa do mais generico para o mais especifico em varios
// sitios, e um intervalo novo que ficasse ABAIXO seria engolido pelo ramo
// generico. **O erro de ordem (um passo generico a atropelar um especifico)
// apareceu SEIS vezes neste trabalho.**
//
// 30000 E NAO 20000, E A RAZAO FOI MEDIDA. Estas duas constantes nasceram a
// 20000/21000, escolhidas quando a unica coisa acima do despacho era a tabela de
// ajudantes. Depois disso, a etapa 8 escolheu -- num OUTRO ficheiro, e sem saber
// desta -- a mesma base para a ENTRADA:
//
//     tools/bateria.cpp:65   constexpr std::uint32_t kBaseDasEntradas = 20000;
//     core/brew/ihiddevice.h:319  Ihid::kSlotsNecessarios      = 32;
//     core/brew/ihid_entrada.h:217 Sinais::kSlotsNecessarios   = 16;
//
// 32 + 16 = 48 slots, de 20000 a 20047 -- exactamente onde a vtable deste modulo
// vive (20000 a 20079). As duas coisas escrevem em `Endereco(20000+i)`: sao a
// MESMA memoria, e quem instalar por ultimo apaga o outro. Nao ha sintoma nenhum
// no momento: a vtable fica escrita e o defeito aparece so quando o jogo salta
// para um slot e cai no metodo de outra interface.
//
// Quem detecta isso e a LEITURA DE VOLTA do `Instalar` (ver `igl.cpp`), e o
// teste `EglTest/CablagemGl.AInstalacaoDetectaAColisao` prova-o numa cena com as
// duas instalacoes na mesma base.
//
// Ruling: a faixa do IGL sobe para 30000/31000, e a da entrada fica onde esta.
// Custo se estiver errado: um numero diferente do que estava escrito no relatorio
// da etapa 6. Custo de as deixar sobrepostas: a vtable de um dos dois modulos
// apagada pelo outro, em silencio.
constexpr std::uint32_t kVtableIgl = 30000;   // 80 slots (AEEGL.h)
constexpr std::uint32_t kVtableIegl = 31000;  // 28 slots (AEEGL.h)
// OS ENDERECOS DOS OBJECTOS. Tambem mudaram, e pela MESMA razao da faixa acima:
// 0x800A0000 e 0x800A1000 estavam ocupados por outra frente.
//
//     core/brew/imedia.h:232  constexpr std::uint32_t kAvisoBase      = 0x800A0000u;
//     core/brew/imedia.h:233  constexpr std::uint32_t kAvisoDadosBase = 0x800A1000u;
//
// O mapa medido dos enderecos de objecto deste emulador, por ordem:
//     0x80010000 tabela de ajudantes | 0x80020000 IShell | 0x80030000 IDisplay
//     0x80040000 IFileMgr | 0x80050000 DIB | 0x80060000 genericos
//     0x80070000 ficheiros | 0x80080000 pilha | 0x80090000 IMedia
//     0x800A0000 avisos de midia | 0x800B0000 LIVRE
// `0x800B0000` e o primeiro bloco livre, e e o que este modulo passa a usar.
constexpr std::uint32_t kObjIgl = 0x800B0000u;
constexpr std::uint32_t kObjIegl = 0x800B1000u;

// --- OS TRES METODOS QUE SO O IGLES11 TEM ------------------------------------
//
// O `AEEGL.h` (a vtable de 80 slots do IGL) so declara as variantes `x` -- 16.16
// -- destes tres: `glLightxv` (slot 44), `glMaterialxv` (slot 50) e `glOrthox`
// (slot 57).  O `IGLES11` (`AEEGLES10.h`/`AEEGLES11.h`, a tabela de
// `tools/igles_slots.inc`) declara TAMBEM as variantes `f` -- float -- e sao
// essas que os titulos pedem, MEDIDO nos tres 3D do corpus:
//
//     IGLES11::Lightfv     6x em gof, pbc e rmp (light=0x4000, pname)
//     IGLES11::Materialfv  8x em gof, pbc e rmp (face=0x0408, pname)
//     IGLES11::Orthof      1x em abd, peggle e torkandkral
//
// ESTES IDS NAO ENTRAM EM `tools/gl_slots.inc`, E A RAZAO E MEDIDA: esse
// ficheiro e GERADO de `AEEGL.h` por `tools/gerar_slots.py` e conferido por
// `tools/verificar_slots_gl.sh`; uma linha escrita a mao la faz a guarda
// DIVERGIR do cabecalho -- e um numero de slot escrito a mao ja divergiu uma vez
// neste trabalho.  E o mais importante: o IGL de `AEEGL.h` NAO TEM estes slots,
// logo escreve-los na tabela de 80 seria uma afirmacao falsa sobre a vtable.
//
// Sao portanto identificadores INTERNOS do MOTOR, acima dos 80 da vtable, e so
// o `classes.cpp` os usa -- no mapa `SlotIglesNoIgl`.  `Igl::Instalar` continua a
// cablar 80 slots (`kIglSlots`): nenhum destes chega a memoria do guest.
constexpr std::uint32_t kIgl_Lightfv = gl_slots::kIglSlots + 0u;     // = 80
constexpr std::uint32_t kIgl_Materialfv = gl_slots::kIglSlots + 1u;  // = 81
constexpr std::uint32_t kIgl_Orthof = gl_slots::kIglSlots + 2u;      // = 82

// E O QUARTO DESTA FAMILIA, com a mesma razao e agora com a MEDIDA do rmp: o
// `glAlphaFunc(func, ref)` do IGLES11 (`AEEGLES10.h:27`, `AEEGLES11.h:84`), cuja
// referencia e `AEEGLclampf` -- um `float` de 32 bits. O `AEEGL.h` so declara o
// `glAlphaFuncx` (`:63`, `GLclampx`), logo `kIgl_AlphaFuncx` (o slot 4 da vtable)
// NAO serve este pedido: os dois tem a mesma grandeza em escalas DIFERENTES.
//
// MEDIDO no `rmp` (`ZB2_QUADROS=300 ZB2_EVT_START=1`): 299 chamadas, uma por
// quadro, com r1=0x00000204 (GL_GREATER) e r2=0x3f000000 -- e 0x3f000000 e o BIT
// PATTERN do `float` 0.5, nao o 1.0 em 16.16 (0x00010000). Por isso o `igl.cpp`
// le esta referencia com o `Real` e nunca com o `Fixo`: o `Fixo` sobre os mesmos
// bits daria 7.6294e-06, um alpha test que descarta tudo (ou nada) e nenhum
// sintoma.
constexpr std::uint32_t kIgl_AlphaFunc = gl_slots::kIglSlots + 3u;  // = 83

// O resultado de UM slot, com os tres valores que o desenho exige:
//   Feito           -- entendido, e o efeito de ESTADO aconteceu;
//   Recusado        -- o metodo existe, e o pedido nao pode ser servido (o
//                      motivo fica escrito);
//   NaoImplementado -- nao ha handler para este slot.
//
// **NUNCA "devolve sucesso e nao faz nada".** `Feito` quer dizer que o estado
// mudou mesmo; o que NAO existe (o rasterizador) diz-se no detalhe de cada
// chamada e nas recusas de `glDrawArrays`/`glDrawElements`.
enum class ResultadoGl { Feito, Recusado, NaoImplementado };

const char* Nome(ResultadoGl r);

// O que chegou ao slot. Os quatro registos porque nenhum metodo do IGL tem mais
// de quatro argumentos em registo; os restantes vao na PILHA, a partir de `sp`,
// e so sao lidos do guest quando o metodo os declara.
struct ArgumentosGl {
  std::uint32_t reg[4] = {0, 0, 0, 0};
  std::uint32_t sp = 0;
  std::uint32_t lr = 0;  // quem chamou: sem o LR nao se sabe de onde veio
};

// Um registo de chamada, para o teste e para a sonda mostrarem o que chegou.
struct ChamadaGl {
  std::uint32_t slot = 0;
  std::string nome;
  // DEZ, e nao oito: o metodo com mais argumentos do `AEEGL.h` e o
  // `glTexImage2D`, com NOVE (target, level, internalformat, width, height,
  // border, format, type, data). Com oito, o ultimo escrevia fora do array e o
  // teste do `glTexImage2D` apanhou-o com uma falha de segmentacao -- dentro do
  // `Memoria::Ler8`, que e o sitio errado para o defeito aparecer.
  std::uint32_t args[10] = {};
  std::size_t n_args = 0;
  std::uint32_t lr = 0;
  ResultadoGl resultado = ResultadoGl::NaoImplementado;
  std::string motivo;
};

// O que se guarda de uma textura. NAO ha pixels: o `glTexImage2D` aponta para a
// memoria do guest, e leva-los para o hospedeiro e trabalho do rasterizador, que
// NAO faz parte desta etapa. Fica o que permite MEDIR que a textura foi pedida,
// com que tamanho, com que formato e quantas vezes.
struct EstadoDaTextura {
  std::uint32_t largura = 0, altura = 0;
  // `formato` e o formato INTERNO (o 3.o argumento do `glTexImage2D`); o
  // `formato_do_pixel` e o 6.o (GL_RGBA, GL_RGB, GL_LUMINANCE) e o `tipo` e o
  // 8.o. O rasterizador precisa dos dois ultimos para ler os texels: sem eles
  // teria de adivinhar quantos bytes tem cada texel.
  std::uint32_t formato = 0, tipo = 0;
  std::uint32_t formato_do_pixel = 0;
  // O NONO ARGUMENTO: os texels, NA MEMORIA DO GUEST. Nao ha copia no acto do
  // `glTexImage2D` (o rasterizador le de la a cada amostragem) -- e isso esta
  // escrito no topo de `core/video/rasterizador.h`, com a consequencia.
  std::uint32_t ponteiro = 0;
  std::uint32_t uploade = 0;
  bool comprimida = false;
};

// Um array de vertices ligado por `glVertexPointer` e companhia. O ponteiro e um
// endereco EMULADO: guarda-se tal como veio, e so se leria no desenho.
struct ArrayDeVertices {
  bool definido = false;
  int tamanho = 0;
  std::uint32_t tipo = 0;
  std::uint32_t passo = 0;
  std::uint32_t ponteiro = 0;
};

// --- pilhas de matrizes -----------------------------------------------------
//
// A PROFUNDIDADE e 16/2/2, o minimo que o GL ES 1.x garante para
// MODELVIEW/PROJECTION/TEXTURE. Nao e um numero escrito a mao: **e o numero que a
// propria interface passa a devolver** -- `glGetIntegerv(GL_MAX_*_STACK_DEPTH)`
// serve estes valores, e o teste verifica-o. Assim a afirmacao nao fica no
// comentario: fica observavel pelo guest e por um teste.
constexpr int kFundosMax = 16;
constexpr int kFundoModelView = 16;
constexpr int kFundoProjection = 2;
constexpr int kFundoTexture = 2;
constexpr int kModoModelView = 0;
constexpr int kModoProjection = 1;
constexpr int kModoTexture = 2;

struct PilhaDeMatrizes {
  float m[kFundosMax][16];
  int fundo = kFundoModelView;
  int topo = 0;  // indice da matriz corrente
};

class Igl {
 public:
  Igl(Memoria& mem, Traco& traco);

  // Escreve o objecto e a vtable NA MEMORIA DO GUEST, com um endereco de saida
  // por slot. Usa `ConstruirObjeto`, do `core/brew/interface.h`, para nao haver
  // uma segunda forma de construir objectos BREW neste emulador. Devolve quantos
  // slots foram cablados.
  std::uint32_t Instalar(const Saidas& saidas);

  std::uint32_t Objeto() const { return objeto_; }
  std::uint32_t Vtable() const { return vtable_; }

  // --- O RASTERIZADOR (etapa 3/6) -----------------------------------------
  //
  // A SUPERFICIE ONDE OS PIXELS SAO ESCRITOS. E a `core/brew/tela.h` do motor,
  // ligada de FORA (pelo `despacho.cpp`, que e quem tem a tela): sem ela,
  // `glClear`/`glDrawArrays`/`glDrawElements` RECUSAM com o motivo escrito, e
  // nao escrevem para um buffer proprio -- uma segunda tela daria uma medida
  // (`PIXELS`/`CORES`) que nao mede o que o titulo escreveu na tela do emulador.
  void DefinirTela(Tela* tela) { destino_.ApontarPara(tela); }
  bool TemTela() const { return destino_.Pronto(); }
  const video::Rasterizador& RasterizadorRef() const { return rasterizador_; }

  // O DESPACHO. Chamado quando o PC entra no endereco de saida do slot.
  // `retorno`, quando nao nulo, recebe o r0 da funcao.
  ResultadoGl Executar(std::uint32_t slot, const ArgumentosGl& a, std::uint32_t* retorno);

  // --- o estado que a sonda e o teste observam ----------------------------
  const float* MatrizCorrente() const;
  std::uint32_t ModoDeMatriz() const { return modo_; }
  int TopoDaPilha(int modo) const;
  std::uint32_t Cor() const { return cor_; }
  std::uint32_t CorDeLimpeza() const { return cor_limpeza_; }
  std::uint32_t MascaraDeLimpeza() const { return mascara_limpeza_; }
  std::uint32_t TexturaLigada() const { return textura_ligada_; }
  std::uint32_t TexturaActiva() const { return textura_activa_; }
  std::uint64_t TexturasGeradas() const { return texturas_geradas_; }
  const EstadoDaTextura* Textura(std::uint32_t id) const;
  bool InterruptorLigado(std::uint32_t cap) const;
  bool ArrayDeClienteLigado(std::uint32_t array) const;
  const ArrayDeVertices* Array(std::uint32_t array) const;
  std::uint32_t Viewport(int indice) const;
  // A MISTURA E O ALPHA TEST, como o `glBlendFunc`/`glAlphaFuncx` os deixaram.
  // Sao observaveis porque um estado que so existe dentro do retrato do
  // desenho nao se pode testar sem desenhar.
  std::uint32_t MisturaFonte() const { return mistura_fonte_; }
  std::uint32_t MisturaDestino() const { return mistura_destino_; }
  std::uint32_t FuncaoDeAlfa() const { return funcao_de_alfa_; }
  float AlfaDeReferencia() const { return alfa_de_referencia_; }
  std::uint32_t CullFace() const { return cull_face_; }
  std::uint32_t FrontFace() const { return front_face_; }
  std::uint32_t ShadeModel() const { return shade_model_; }
  // O estado generico dos metodos que so acumulam parametros (`glFogx`,
  // `glLightxv`, `glMaterialx`, ...). A chave e (slot, pname) e o valor sao os
  // argumentos que chegaram: acumular e o que impede "devolver sucesso e nao
  // fazer nada", e e observavel pelo teste.
  const std::vector<std::uint32_t>* Parametro(std::uint32_t slot, std::uint32_t pname) const;
  // O ESTADO DE LUZ E DE MATERIAL, em float. Estes dois acumulam por
  // (luz / face, pname), e nao por slot como o `Parametro` acima: a chave de um
  // `glLightfv(GL_LIGHT0, GL_AMBIENT, ...)` e o INDICE DA LUZ, e o mesmo slot
  // serve as oito luzes do GL -- uma chave so por slot guardaria a ultima luz e
  // perderia as outras sete em silencio. Os valores ficam em `float`, tal como o
  // SDK os entrega: converter para 16.16 era inventar precisao que o pedido nao
  // tem (o `Fixo` faz o contrario, e por isso nao se usa aqui).
  const std::vector<float>* ParametroDeLuz(std::uint32_t luz, std::uint32_t pname) const;
  // A POSICAO (e a direccao do holofote) EM COORDENADAS DE OLHO, que e o que o
  // `glLightfv(GL_POSITION)` guarda: o vector passa pela modelview do INSTANTE
  // da chamada. Guardar as coordenadas de objecto e transformar na hora do
  // desenho parece igual e nao e -- a luz andaria junto com cada modelo.
  const std::vector<float>* ParametroDeLuzEmOlho(std::uint32_t luz, std::uint32_t pname) const;
  const std::vector<float>* ParametroDeMaterial(std::uint32_t face, std::uint32_t pname) const;
  std::uint64_t Limpezas() const { return limpezas_; }
  std::uint64_t Desenhos() const { return desenhos_; }
  std::uint64_t Vertices() const { return vertices_; }
  std::uint64_t Chamadas() const { return chamadas_; }
  std::uint64_t ChamadasDoSlot(std::uint32_t slot) const;
  // A LISTA DO QUE FALTA, por nome. A arvore antiga nao tinha isto em lado
  // nenhum, e por isso nao havia como saber o que era fachada.
  const std::map<std::string, std::uint64_t>& Recusas() const { return recusas_; }
  const std::vector<ChamadaGl>& Ultimas() const { return ultimas_; }

 private:
  // O RETRATO DO ESTADO no instante do desenho, para o rasterizador. E um
  // retrato (valores), e nao uma referencia a este objecto: o rasterizador nao
  // precisa de conhecer o IGL para ser testavel em isolamento.
  video::EstadoDeRasterizacao MontarEstado() const;
  // As capacidades que o titulo ligou e que o rasterizador nao faz entram UMA vez
  // cada nas faltas: um aviso por desenho encheria o traco de um titulo de 60
  // quadros com a mesma linha, e um aviso nenhum seria o stub mudo outra vez.
  void RegistarRessalvas(const video::EstadoDeRasterizacao& e);
  ResultadoGl Recusar(const std::string& motivo, ChamadaGl& c);
  ResultadoGl NaoTem(ChamadaGl& c);
  void Registar(const ChamadaGl& c);
  std::uint32_t Arg(std::size_t i, const ArgumentosGl& a) const;
  float Fixo(std::size_t i, const ArgumentosGl& a) const;   // GLfixed 16.16
  float Real(std::size_t i, const ArgumentosGl& a) const;   // AEEGLfloat (32 bits)
  float RealDaMemoria(std::uint32_t endereco) const;        // idem, lido do guest
  void LerMatriz(std::size_t i, const ArgumentosGl& a, float* saida) const;
  PilhaDeMatrizes& PilhaDoModo();

  Memoria& mem_;
  Traco& traco_;
  // A ORDEM DE DECLARACAO E A ORDEM DE CONSTRUCAO: o `destino_` nasce antes do
  // `rasterizador_`, que guarda uma referencia a ele.
  video::DestinoTela destino_;
  video::Rasterizador rasterizador_;
  std::uint32_t objeto_ = 0, vtable_ = 0;

  std::uint32_t modo_ = kModoModelView;
  PilhaDeMatrizes mv_, proj_, tex_;
  std::uint32_t cor_ = 0xFFFFFFFFu;  // RGBA8
  std::uint32_t cor_limpeza_ = 0;
  float profundidade_limpeza_ = 1.0f;
  std::uint32_t mascara_limpeza_ = 0;
  std::uint32_t viewport_[4] = {0, 0, 0, 0};
  std::uint32_t cull_face_ = gl_slots::GL_BACK;
  std::uint32_t front_face_ = gl_slots::GL_CCW;
  std::uint32_t shade_model_ = gl_slots::GL_SMOOTH;
  std::uint32_t depth_func_ = gl_slots::GL_LESS;
  bool depth_mask_ = true;
  std::uint32_t color_mask_ = 0x0000000Fu;
  // A MISTURA E O ALPHA TEST. As omissoes sao as do GL: `glBlendFunc(GL_ONE,
  // GL_ZERO)` (copiar) e `glAlphaFuncx(GL_ALWAYS, 0)` (nao descartar). Sem
  // estas duas linhas, um titulo que ligue o `GL_BLEND` e chame o
  // `glBlendFunc` ficava com o pedido acumulado num `parametros_` que o
  // rasterizador nao le -- foi o estado em que rmp e gof estavam: o pedido
  // chegava, era aceite, e nenhum pixel mudava.
  std::uint32_t mistura_fonte_ = gl_slots::GL_ONE;
  std::uint32_t mistura_destino_ = gl_slots::GL_ZERO;
  std::uint32_t funcao_de_alfa_ = gl_slots::GL_ALWAYS;
  float alfa_de_referencia_ = 0.0f;
  std::map<std::uint32_t, std::uint32_t> interruptores_;
  std::map<std::uint32_t, bool> arrays_de_cliente_;
  std::map<std::uint32_t, ArrayDeVertices> arrays_;
  std::uint32_t textura_ligada_ = 0;
  std::uint32_t textura_activa_ = 0;
  std::uint64_t texturas_geradas_ = 0;
  std::map<std::uint32_t, EstadoDaTextura> texturas_;
  std::map<std::uint64_t, std::vector<std::uint32_t>> parametros_;
  // (luz << 32) | pname e (face << 32) | pname. A chave e de 64 bits porque as
  // duas grandezas sao `AEEGLenum` de 32.
  std::map<std::uint64_t, std::vector<float>> luzes_;
  std::map<std::uint64_t, std::vector<float>> luzes_em_olho_;
  std::map<std::uint64_t, std::vector<float>> materiais_;
  std::uint64_t limpezas_ = 0, desenhos_ = 0, vertices_ = 0;
  std::uint64_t chamadas_ = 0;
  std::map<std::uint32_t, std::uint64_t> por_slot_;
  std::map<std::string, std::uint64_t> recusas_;
  std::vector<ChamadaGl> ultimas_;
};

}  // namespace zb2::brew

#endif  // ZB2_CORE_BREW_IGL_H
