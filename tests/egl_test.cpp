// Testes do IEGL -- e da CABLAGEM do GL no despacho.
//
// O QUE ESTES TESTES PROTEGEM, e porque existem:
//
// 1. A TABELA DE SLOTS. 28 slots, com os nomes do `AEEGL.h`, conferidos contra os
//    102 thunks do `conftest.elf` pela guarda `tools/verificar_slots_gl.sh`. Copiar
//    a ordem de outro emulador esta proibido por TESTE.
//
// 2. O CONFIG. O unico config deste emulador e o da `Tela` (640x480, RGB565). Um
//    config de folheto (8/8/8/8) recusaria o pedido do corpus -- MEDIDO no
//    `ddragonz.mod` 0x14fc10: SURFACE_TYPE=WINDOW_BIT, R=5, G=6, B=5, EGL_NONE.
//
// 3. NENHUM CAMINHO MUDO. Cada uma das 28 chamadas tem de aparecer no traco com
//    nome, e cada caminho sem implementacao tem de RECUSAR (P2). Foi um stub que
//    devolvia sucesso e nao fazia nada que descartou 86 377 `glCullFace`.
//
// 4. A CABLAGEM NO DESPACHO. A chamada tem de CHEGAR ao modulo pelo despacho do
//    motor, e nao por baixo dele. Enquanto o remendo nao estiver aplicado estes
//    testes SALTAM com o motivo escrito -- um teste que passa sem ter corrido e
//    pior do que um teste vermelho. Ver `docs/rewrite/REMENDO-GL-DESPACHO.md`.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/brew/despacho.h"
#include "core/cpu/arm_interpreter.h"
#include "core/brew/egl.h"
#include "core/brew/ihid_entrada.h"
#include "core/brew/ihiddevice.h"
#include "core/brew/imedia.h"
#include "core/brew/tela.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"

namespace zb2::brew {
namespace {

using namespace gl_slots;

// O TAMANHO DA SUPERFICIE TEM DE SER O DA TELA, e o `static_assert` e o que impede
// os dois de se separarem numa edicao futura. Um `640x480` escrito a mao aqui e o
// mesmo numero -- mas nao ha nada que o obrigue a continuar a ser.
static_assert(kLarguraDaSuperficie == static_cast<std::uint32_t>(Tela::kLargura),
              "egl.h: a largura da superficie deixou de ser a da Tela");
static_assert(kAlturaDaSuperficie == static_cast<std::uint32_t>(Tela::kAltura),
              "egl.h: a altura da superficie deixou de ser a da Tela");

struct Banco {
  Tempo tempo;
  Traco traco;
  DestinoMemoria destino;
  Memoria mem;
  Saidas saidas;
  Egl egl;

  Banco() : traco("teste-do-egl", &tempo), mem(&traco), egl(mem, traco) {
    traco.JuntarDestino(&destino);
    mem.EscritorUnico("teste");
    saidas.base = 0xF0000000u;
    saidas.passo = 4;
    saidas.quantos = 100000;
    saidas.ativa = true;
    egl.Instalar(saidas);
  }
};

ArgumentosGl Args(std::uint32_t a0 = 0, std::uint32_t a1 = 0, std::uint32_t a2 = 0,
                  std::uint32_t a3 = 0, std::uint32_t sp = 0) {
  ArgumentosGl a;
  a.reg[0] = a0;
  a.reg[1] = a1;
  a.reg[2] = a2;
  a.reg[3] = a3;
  a.sp = sp;
  return a;
}

// Escreve uma `attrib_list` do EGL na memoria do guest, terminada em EGL_NONE.
std::uint32_t EscreverLista(Banco& b, const std::vector<std::uint32_t>& pares, std::uint32_t onde) {
  std::uint32_t p = onde;
  for (const std::uint32_t v : pares) {
    b.mem.Escrever32(p, v);
    p += 4;
  }
  b.mem.Escrever32(p, EGL_NONE);
  return onde;
}

// O PEDIDO MEDIDO NO CORPUS: a lista do `eglChooseConfig` do `ddragonz.mod`, lida
// do ficheiro em 0x14fc10 (o literal esta em 0x11d978 e o `add pc` da instrucao
// 0x11d704 resolve-o). Nao e inventada: e o que um titulo pede mesmo.
const std::vector<std::uint32_t> kPedidoDoDdragonz = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                                                      EGL_RED_SIZE,     5,
                                                      EGL_GREEN_SIZE,   6,
                                                      EGL_BLUE_SIZE,    5};

// ---------------------------------------------------------------------------
// 1. A TABELA DE SLOTS
// ---------------------------------------------------------------------------

TEST(TabelaDoIegl, TamanhoVemDoCabecalho) {
  // `AEEGL.h`: INHERIT_IQueryInterface (3) + 25 metodos = 28.
  EXPECT_EQ(kIeglSlots, 28u);
  EXPECT_EQ(kIegl_AddRef, 0u);
  EXPECT_EQ(kIegl_Release, 1u);
  EXPECT_EQ(kIegl_QueryInterface, 2u);
  EXPECT_EQ(kIegl_GetError, 3u);
  // A cabeca NAO e a IBase (dois membros): e a IQI, e sao TRES. Contar dois
  // deslocaria todos os slots por um -- foi o erro que custou uma ronda inteira.
  EXPECT_EQ(kIegl_GetDisplay, 4u);
  EXPECT_EQ(kIegl_Initialize, 5u);
}

TEST(TabelaDoIegl, DoisSlotsMedidosNoConftest) {
  // `conftest.elf`: o `eglGetDisplay` faz `ldr r1,[r1,#16]` -> 16/4 = 4, e o
  // `eglSwapBuffers` faz `ldr r1,[r1,#104]` -> 104/4 = 26. O cabecalho concorda.
  EXPECT_EQ(kIegl_GetDisplay, 4u);
  EXPECT_EQ(kIegl_SwapBuffers, 26u);
}

TEST(TabelaDoIegl, NomesCompletosEUnicos) {
  std::vector<std::string> vistos;
  for (std::uint32_t s = 0; s < kIeglSlots; ++s) {
    const std::string nome = NomeIegl(s);
    EXPECT_FALSE(nome.empty()) << "slot " << s;
    for (const auto& outro : vistos) EXPECT_NE(nome, outro) << "nome repetido: " << nome;
    vistos.push_back(nome);
  }
  EXPECT_EQ(vistos.size(), 28u);
  // Os 25 metodos proprios comecam por `egl`; se a lista perdesse um nome, a
  // contagem cai e o teste di-lo.
  std::uint32_t quantos = 0;
  for (std::uint32_t s = kIegl_GetError; s < kIeglSlots; ++s) {
    if (std::string(NomeIegl(s)).rfind("egl", 0) == 0) ++quantos;
  }
  EXPECT_EQ(quantos, 25u);
  EXPECT_STREQ(NomeIegl(kIeglSlots), "slot_fora_da_tabela");
}

TEST(TabelaDoIegl, InstalarCablaEObjEConfere) {
  Banco b;
  EXPECT_EQ(b.mem.Ler32(b.egl.Objeto()), b.egl.Vtable());
  EXPECT_EQ(b.mem.Ler32(b.egl.Objeto() + 4), 1u);
  EXPECT_EQ(b.mem.Ler32(b.egl.Vtable() + 0), b.saidas.Endereco(3));
  EXPECT_EQ(b.mem.Ler32(b.egl.Vtable() + 4), b.saidas.Endereco(4));
  EXPECT_EQ(b.mem.Ler32(b.egl.Vtable() + kIegl_MakeCurrent * 4),
            b.saidas.Endereco(kVtableIegl + kIegl_MakeCurrent));
}

// ---------------------------------------------------------------------------
// 2. AS FAIXAS E A COLISAO (o defeito silencioso desta etapa)
// ---------------------------------------------------------------------------

TEST(CablagemGl, AsFaixasNaoSeSobrepoem) {
  // A ENTRADA ocupa 20000..20047 na bateria (`tools/bateria.cpp:65` instala-a em
  // 20000; `Sinais::kSlotsNecessarios` 16 + `Ihid::kSlotsNecessarios` 32).
  const std::uint32_t fim_da_entrada = 20000u + Sinais::kSlotsNecessarios + Ihid::kSlotsNecessarios;
  EXPECT_GE(kVtableIgl, fim_da_entrada)
      << "a faixa do IGL voltou a entrar na faixa da entrada: a vtable de um dos";
  EXPECT_GT(kVtableIegl, kVtableIgl + kIglSlots);
}

TEST(CablagemGl, OsEnderecosDosObjectosNaoSeSobrepoem) {
  // `imedia.h` usa esta zona para os avisos de midia. Duas frentes escolheram o
  // mesmo endereco (0x800A0000) -- o mapa medido esta em `igl.h`.
  EXPECT_NE(kObjIgl, kAvisoBase);
  EXPECT_NE(kObjIgl, kAvisoDadosBase);
  EXPECT_NE(kObjIegl, kAvisoBase);
  EXPECT_NE(kObjIegl, kAvisoDadosBase);
  EXPECT_NE(kObjIgl, kObjIegl);
  // A zona de strings do IEGL tambem nao pode cair em cima de outra coisa nossa.
  EXPECT_GT(kZonaDeStrings, kObjIegl);
}

TEST(CablagemGl, AEntradaEOGlNaoPartilhamNemFaixaNemArmazenamento) {
  // A FAIXA DA ENTRADA e 20000..20047 na bateria (`tools/bateria.cpp:65` instala-a
  // em 20000; `Sinais::kSlotsNecessarios` 16 + `Ihid::kSlotsNecessarios` 32), e o
  // `AtenderEntrada` corre ANTES de qualquer ramo do GL no `Correr`. Se as faixas
  // se sobrepusessem, os primeiros 48 slots do IGL ficariam SOMBREADOS pelos da
  // entrada -- a mesma classe do erro de ordem que apareceu sete vezes aqui.
  //
  // A MEDICAO DO ARMAZENAMENTO IMPORTA TANTO COMO A DA FAIXA, e foi por isso que
  // esta cena existe: a entrada escreve as vtables dela em 0x81030000/0x81040000
  // (`ihid_entrada.cpp`), enquanto o IGL e o IEGL escrevem as suas DENTRO da faixa
  // de saida (uma por slot). Duas frentes escolherem o mesmo sitio para a mesma
  // coisa aconteceu DUAS vezes nesta etapa (a faixa 20000 e o endereco 0x800A0000).
  Banco b;
  Memoria& mem = b.mem;
  Traco& traco = b.traco;
  Alocador al(mem, 0x80200000u, 0x00400000u, &traco);
  Vfs vfs;
  Despacho despacho(mem, traco, al, vfs);
  const std::uint32_t base_da_entrada = 20000;
  EXPECT_TRUE(despacho.InstalarEntrada(b.saidas, base_da_entrada));
  const std::uint32_t fim_da_entrada =
      base_da_entrada + Sinais::kSlotsNecessarios + Ihid::kSlotsNecessarios;
  EXPECT_FALSE(kVtableIgl < fim_da_entrada && kVtableIgl + kIglSlots > base_da_entrada)
      << "a faixa do IGL entrou na faixa da entrada: os primeiros slots do IGL ficariam "
         "sombreados por ela";
  EXPECT_FALSE(kVtableIegl < fim_da_entrada && kVtableIegl + kIeglSlots > base_da_entrada);
  // E o IGL instala-se DEPOIS da entrada sem perder a cablagem (a leitura de volta
  // dele passa, porque as duas nao partilham armazenamento nenhum).
  Igl igl(mem, traco);
  EXPECT_EQ(igl.Instalar(b.saidas), kIglSlots);
  EXPECT_EQ(mem.Ler32(igl.Vtable() + kIgl_Clear * 4),
            b.saidas.Endereco(kVtableIgl + kIgl_Clear));
  // A ancora das duas vtables: a do IGL vive na faixa de saida (uma por slot) e a
  // do IEGL tambem, sem se tocarem.
  EXPECT_EQ(igl.Vtable(), b.saidas.Endereco(kVtableIgl));
  EXPECT_GE(b.saidas.Endereco(kVtableIegl), b.saidas.Endereco(kVtableIgl) + kIglSlots * 4u);
}

// ---------------------------------------------------------------------------
// 3. O CONFIG: o que o emulador CONSEGUE apresentar
// ---------------------------------------------------------------------------

TEST(ConfigDoEgl, OsValoresSaoOsDaTela) {
  const auto valor = [](std::uint32_t id) {
    for (std::size_t k = 0; k < kQuantosAtributosDoConfig; ++k) {
      if (kConfigDoZeebulator[k].id == id) return kConfigDoZeebulator[k].valor;
    }
    return 0xFFFFFFFFu;
  };
  EXPECT_EQ(valor(EGL_RED_SIZE), 5u);
  EXPECT_EQ(valor(EGL_GREEN_SIZE), 6u);
  EXPECT_EQ(valor(EGL_BLUE_SIZE), 5u);
  EXPECT_EQ(valor(EGL_BUFFER_SIZE), 16u);
  EXPECT_EQ(valor(EGL_ALPHA_SIZE), 0u);
  // EGL_DEPTH_SIZE ERA 0 E CONTRADIZIA O PROPRIO EMULADOR: o rasterizador tem
  // buffer de profundidade (`core/video/rasterizador.h:210` `TemBufferDeProfundidade()`,
  // `:243` `std::vector<float> profundidade_`, `:166-168` teste e escrita). O
  // config negava uma coisa que o modulo ao lado tem.
  //
  // MEDIDO, e e o que o mudou: o `karnovr` pede `EGL_DEPTH_SIZE 16` no
  // `eglChooseConfig`; com 0, a comparacao de tamanhos (`egl.cpp`, "o config tem
  // de ter pelo menos o pedido") devolvia ZERO configs, o
  // `eglCreateWindowSurface` recebia config 0 e recusava, e os 10 titulos da
  // familia `emulator_neo` desenhavam "InitGLSurface failed".
  //
  // 16 e a profundidade do ecra do Zeebo (RGB565 + Z16), o mesmo que o zeebx
  // declara (`src/video/gles.rs:84`); o float do nosso buffer cobre essa precisao.
  EXPECT_EQ(valor(EGL_DEPTH_SIZE), 16u);
  EXPECT_EQ(valor(EGL_STENCIL_SIZE), 0u);
  EXPECT_EQ(valor(EGL_SURFACE_TYPE),
              static_cast<std::uint32_t>(EGL_WINDOW_BIT | EGL_PIXMAP_BIT));
  // NAO HA PBUFFER: o `eglCreatePbufferSurface` recusa, e o config tem de dizer o
  // mesmo que o metodo faz.
  EXPECT_EQ(valor(EGL_MAX_PBUFFER_PIXELS), 0u);
}

TEST(ConfigDoEgl, CadaLinhaDizDeOndeVemOValor) {
  // P1: uma tabela sem a origem escrita nao se pode auditar. O teste exige que a
  // origem exista e nao seja vazia, e que os ids nao se repitam.
  std::vector<std::uint32_t> vistos;
  for (std::size_t k = 0; k < kQuantosAtributosDoConfig; ++k) {
    EXPECT_NE(kConfigDoZeebulator[k].origem, nullptr);
    EXPECT_GT(std::string(kConfigDoZeebulator[k].origem).size(), 8u);
    for (const auto v : vistos) EXPECT_NE(v, kConfigDoZeebulator[k].id);
    vistos.push_back(kConfigDoZeebulator[k].id);
  }
}

TEST(ConfigDoEgl, ONomeDosAtributosVemDoCabecalho) {
  EXPECT_STREQ(NomeDoAtributoEgl(EGL_RED_SIZE), "EGL_RED_SIZE");
  EXPECT_STREQ(NomeDoAtributoEgl(EGL_SURFACE_TYPE), "EGL_SURFACE_TYPE");
  EXPECT_EQ(NomeDoAtributoEgl(0x9999u), nullptr);
}

// ---------------------------------------------------------------------------
// 4. O CICLO DE VIDA, com os pedidos do corpus
// ---------------------------------------------------------------------------

TEST(CicloDoEgl, OGetDisplayDevolveODisplayEDefaultEOServido) {
  Banco b;
  std::uint32_t r = 0;
  EXPECT_EQ(b.egl.Executar(kIegl_GetDisplay, Args(0), &r), ResultadoGl::Feito);
  EXPECT_EQ(r, kDisplayUnico);
  EXPECT_EQ(b.egl.Erro(), EGL_SUCCESS);
}

TEST(CicloDoEgl, OInitializeEscreveAVersaoDeclarada) {
  Banco b;
  const std::uint32_t pmaior = 0x00090000u, pmenor = 0x00090004u;
  b.mem.Escrever32(pmaior, 0xDEADBEEFu);
  b.mem.Escrever32(pmenor, 0xDEADBEEFu);
  std::uint32_t r = 0;
  EXPECT_EQ(b.egl.Executar(kIegl_Initialize, Args(kDisplayUnico, pmaior, pmenor), &r),
            ResultadoGl::Feito);
  EXPECT_EQ(r, static_cast<std::uint32_t>(EGL_TRUE));
  EXPECT_EQ(b.mem.Ler32(pmaior), 1u);
  EXPECT_EQ(b.mem.Ler32(pmenor), 0u);
  EXPECT_TRUE(b.egl.Iniciado());
}

TEST(CicloDoEgl, OInitializeRecusaUmDisplayQueNaoEDesteEmulador) {
  Banco b;
  std::uint32_t r = 0;
  EXPECT_EQ(b.egl.Executar(kIegl_Initialize, Args(0x80020000u, 0, 0), &r), ResultadoGl::Recusado);
  EXPECT_EQ(b.egl.Erro(), EGL_BAD_DISPLAY);
  EXPECT_FALSE(b.egl.Iniciado());
}

TEST(CicloDoEgl, OEscolheConfigComOPedidoMedidoNoCorpus) {
  Banco b;
  const std::uint32_t lista = EscreverLista(b, kPedidoDoDdragonz, 0x00090000u);
  const std::uint32_t saida = 0x00090400u, pnum = 0x00090410u;
  b.mem.Escrever32(pnum, 0xDEADBEEFu);
  b.mem.Escrever32(saida, 0xDEADBEEFu);
  // CINCO argumentos: o ultimo vem na pilha.
  const std::uint32_t pilha = 0x80080000u;
  b.mem.Escrever32(pilha, pnum);
  ArgumentosGl a = Args(kDisplayUnico, lista, saida, 1, pilha);
  std::uint32_t r = 0;
  EXPECT_EQ(b.egl.Executar(kIegl_ChooseConfig, a, &r), ResultadoGl::Feito);
  EXPECT_EQ(r, static_cast<std::uint32_t>(EGL_TRUE));
  EXPECT_EQ(b.mem.Ler32(pnum), 1u) << "o pedido do ddragonz (R5 G6 B5 + WINDOW_BIT) tem de casar";
  EXPECT_EQ(b.mem.Ler32(saida), kConfigUnico);
}

TEST(CicloDoEgl, OEscolheConfigRecusaUmPedidoMaiorDoQueATela) {
  Banco b;
  // R=8 nao cabe num pixel de 5 bits. O EGL diz que isso e um SUCESSO com ZERO
  // configs -- e nao uma recusa do emulador. O motivo fica escrito e com o nome.
  std::vector<std::uint32_t> pedido = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RED_SIZE, 8};
  const std::uint32_t lista = EscreverLista(b, pedido, 0x00090000u);
  const std::uint32_t pnum = 0x00090410u;
  b.mem.Escrever32(pnum, 0xDEADBEEFu);
  const std::uint32_t pilha = 0x80080000u;
  b.mem.Escrever32(pilha, pnum);
  std::uint32_t r = 0;
  EXPECT_EQ(b.egl.Executar(kIegl_ChooseConfig, Args(kDisplayUnico, lista, 0, 1, pilha), &r),
            ResultadoGl::Feito);
  EXPECT_EQ(b.mem.Ler32(pnum), 0u);
  ASSERT_FALSE(b.egl.Ultimas().empty());
  EXPECT_NE(b.egl.Ultimas().back().motivo.find("EGL_RED_SIZE"), std::string::npos);
}

TEST(CicloDoEgl, OEscolheConfigRecusaUmaListaQueNaoSabeLer) {
  Banco b;
  // Um id que nao existe no cabecalho: ler pares de um ponteiro errado e chama-los
  // de config seria pior do que recusar.
  std::vector<std::uint32_t> lixo = {0x9999u, 1u};
  const std::uint32_t lista = EscreverLista(b, lixo, 0x00090000u);
  const std::uint32_t pilha = 0x80080000u;
  b.mem.Escrever32(pilha, 0x00090410u);
  std::uint32_t r = 0;
  EXPECT_EQ(b.egl.Executar(kIegl_ChooseConfig, Args(kDisplayUnico, lista, 0, 1, pilha), &r),
            ResultadoGl::Recusado);
  EXPECT_EQ(b.egl.Erro(), EGL_BAD_ATTRIBUTE);
}

TEST(CicloDoEgl, OGetConfigAttribServeOMedidoERecusaONaoMedido) {
  Banco b;
  const std::uint32_t saida = 0x00090400u;
  std::uint32_t r = 0;
  EXPECT_EQ(b.egl.Executar(kIegl_GetConfigAttrib,
                           Args(kDisplayUnico, kConfigUnico, EGL_RED_SIZE, saida), &r),
            ResultadoGl::Feito);
  EXPECT_EQ(b.mem.Ler32(saida), 5u);
  // `EGL_NATIVE_VISUAL_ID` descreve a JANELA NATIVA da maquina: nao ha medida
  // nenhuma do que o Zeebo responde, e por isso RECUSA -- com o nome do atributo.
  b.mem.Escrever32(saida, 0xDEADBEEFu);
  EXPECT_EQ(b.egl.Executar(kIegl_GetConfigAttrib,
                           Args(kDisplayUnico, kConfigUnico, EGL_NATIVE_VISUAL_ID, saida), &r),
            ResultadoGl::Recusado);
  EXPECT_EQ(b.mem.Ler32(saida), 0xDEADBEEFu) << "a recusa nao pode escrever no ponteiro de saida";
  EXPECT_EQ(b.egl.Erro(), EGL_BAD_ATTRIBUTE);
  ASSERT_FALSE(b.egl.Ultimas().empty());
  EXPECT_NE(b.egl.Ultimas().back().motivo.find("EGL_NATIVE_VISUAL_ID"), std::string::npos);
}

TEST(CicloDoEgl, AOrdemDoWrapperDoJogoFunciona) {
  Banco b;
  std::uint32_t r = 0;
  // A ordem MEDIDA no `ddragonz.mod` (0x11d6c4-0x11d890). Sem ela, nenhum `gl*`
  // do jogo acontece: e o wrapper do SDK que chama o EGL antes do GL.
  EXPECT_EQ(b.egl.Executar(kIegl_GetDisplay, Args(0), &r), ResultadoGl::Feito);
  EXPECT_EQ(b.egl.Executar(kIegl_Initialize, Args(kDisplayUnico, 0, 0), &r), ResultadoGl::Feito);
  const std::uint32_t lista = EscreverLista(b, kPedidoDoDdragonz, 0x00090000u);
  const std::uint32_t pnum = 0x00090410u, saida = 0x00090400u, pilha = 0x80080000u;
  b.mem.Escrever32(pilha, pnum);
  EXPECT_EQ(b.egl.Executar(kIegl_ChooseConfig, Args(kDisplayUnico, lista, saida, 1, pilha), &r),
            ResultadoGl::Feito);
  EXPECT_EQ(b.mem.Ler32(pnum), 1u);
  const std::uint32_t cfg = b.mem.Ler32(saida);
  EXPECT_EQ(b.egl.Executar(kIegl_CreateWindowSurface, Args(kDisplayUnico, cfg, 0, 0), &r),
            ResultadoGl::Feito);
  EXPECT_NE(r, 0u);
  const std::uint32_t sup = r;
  EXPECT_EQ(b.egl.Executar(kIegl_CreateContext, Args(kDisplayUnico, cfg, 0, 0), &r),
            ResultadoGl::Feito);
  EXPECT_NE(r, 0u);
  const std::uint32_t ctx = r;
  EXPECT_EQ(b.egl.Executar(kIegl_MakeCurrent, Args(kDisplayUnico, sup, sup, ctx), &r),
            ResultadoGl::Feito);
  EXPECT_EQ(b.egl.ContextoCorrente(), ctx);
  EXPECT_EQ(b.egl.SuperficieCorrente(), sup);
  EXPECT_EQ(b.egl.Executar(kIegl_SwapBuffers, Args(kDisplayUnico, sup), &r), ResultadoGl::Feito);
  EXPECT_EQ(b.egl.Trocas(), 1u);
  EXPECT_EQ(b.egl.Erro(), EGL_SUCCESS);
}

TEST(CicloDoEgl, ACriacaoDeSuperficieRecusaSemInitialize) {
  Banco b;
  std::uint32_t r = 0;
  EXPECT_EQ(b.egl.Executar(kIegl_CreateWindowSurface, Args(kDisplayUnico, kConfigUnico, 0, 0), &r),
            ResultadoGl::Recusado);
  EXPECT_EQ(b.egl.Erro(), EGL_NOT_INITIALIZED);
  EXPECT_EQ(b.egl.SuperficiesVivas(), 0u);
}

TEST(CicloDoEgl, OGetErrorConsomeOErro) {
  Banco b;
  std::uint32_t r = 0;
  EXPECT_EQ(b.egl.Executar(kIegl_Initialize, Args(0x80020000u, 0, 0), &r), ResultadoGl::Recusado);
  EXPECT_EQ(b.egl.Erro(), EGL_BAD_DISPLAY);
  EXPECT_EQ(b.egl.Executar(kIegl_GetError, Args(), &r), ResultadoGl::Feito);
  EXPECT_EQ(r, static_cast<std::uint32_t>(EGL_BAD_DISPLAY));
  // A SEGUNDA leitura devolve SUCCESS: o EGL define que a leitura limpa o erro.
  // Sem isto, o primeiro erro ficava para sempre e o jogo tomava todas as decisoes
  // seguintes por ele.
  EXPECT_EQ(b.egl.Executar(kIegl_GetError, Args(), &r), ResultadoGl::Feito);
  EXPECT_EQ(r, static_cast<std::uint32_t>(EGL_SUCCESS));
}

TEST(CicloDoEgl, OQueryStringNuncaDevolveNulo) {
  Banco b;
  std::uint32_t r = 0;
  const std::uint32_t consultas[] = {EGL_VERSION, EGL_EXTENSIONS, EGL_CLIENT_APIS, EGL_VENDOR,
                                     0x30AAu};
  for (const std::uint32_t qual : consultas) {
    EXPECT_EQ(b.egl.Executar(kIegl_Initialize, Args(kDisplayUnico, 0, 0), &r), ResultadoGl::Feito);
    r = 0;
    b.egl.Executar(kIegl_QueryString, Args(kDisplayUnico, qual), &r);
    EXPECT_NE(r, 0u) << "consulta 0x" << std::hex << qual;
    // O que ficou na memoria tem de ser uma cadeia terminada: um nulo aqui seria
    // o `strstr` do `ddragonz` sobre o nulo (medicao da arvore antiga).
    bool terminou = false;
    for (std::uint32_t k = 0; k < kPassoDeString; ++k) {
      if (b.mem.Ler8(r + k) == 0) { terminou = true; break; }
    }
    EXPECT_TRUE(terminou);
  }
  b.egl.Executar(kIegl_QueryString, Args(kDisplayUnico, EGL_VERSION), &r);
  EXPECT_EQ(b.egl.StringServida(0), "1.0");
  b.egl.Executar(kIegl_QueryString, Args(kDisplayUnico, EGL_EXTENSIONS), &r);
  EXPECT_EQ(b.egl.StringServida(1), "");
  b.egl.Executar(kIegl_QueryString, Args(kDisplayUnico, EGL_VENDOR), &r);
  EXPECT_EQ(b.egl.StringServida(1), "") << "o vendor da maquina nao foi medido: string vazia";
}

TEST(CicloDoEgl, OGetProcAddressDevolveZeroERegistaOPedido) {
  Banco b;
  const std::uint32_t nome = 0x00090000u;
  const char* pedido = "glMapBufferOES";
  for (std::uint32_t k = 0; k < 14; ++k) b.mem.Escrever8(nome + k, static_cast<std::uint8_t>(pedido[k]));
  b.mem.Escrever8(nome + 14, 0);
  std::uint32_t r = 0xDEADBEEFu;
  EXPECT_EQ(b.egl.Executar(kIegl_GetProcAddress, Args(nome), &r), ResultadoGl::Feito);
  EXPECT_EQ(r, 0u) << "zero e a resposta certa: nenhuma extensao implementada";
  // O PEDIDO FICA REGISTADO COM NOME -- e assim que se aprende o que os titulos
  // procuram, em vez de "algo devolveu nulo".
  ASSERT_FALSE(b.egl.Ultimas().empty());
  EXPECT_NE(b.egl.Ultimas().back().motivo.find("glMapBufferOES"), std::string::npos);
}

TEST(CicloDoEgl, OPixmapServeComoHandleEPbufferECopyBuffersRecusam) {
  Banco b;
  std::uint32_t r = 0;
  EXPECT_EQ(b.egl.Executar(kIegl_Initialize, Args(kDisplayUnico, 0, 0), &r), ResultadoGl::Feito);
  // A pixmap e guardada sem raster, como a janela (fluxo OpenVG: IDIB do guest).
  EXPECT_EQ(b.egl.Executar(kIegl_CreatePixmapSurface, Args(kDisplayUnico, kConfigUnico, 0, 0), &r),
            ResultadoGl::Feito);
  EXPECT_NE(r, 0u);
  EXPECT_EQ(b.egl.Erro(), EGL_SUCCESS);
  EXPECT_EQ(b.egl.Executar(kIegl_CreatePbufferSurface, Args(kDisplayUnico, kConfigUnico, 0), &r),
            ResultadoGl::Recusado);
  EXPECT_EQ(b.egl.Executar(kIegl_CopyBuffers, Args(kDisplayUnico, kPrimeiraSuperficie, 0), &r),
            ResultadoGl::Recusado);
  EXPECT_EQ(b.egl.Ultimas().back().resultado, ResultadoGl::Recusado);
}

TEST(CicloDoEgl, OMakeCurrentValidaOsHandles) {
  Banco b;
  std::uint32_t r = 0;
  b.egl.Executar(kIegl_Initialize, Args(kDisplayUnico, 0, 0), &r);
  // Handles que nao sao deste emulador.
  EXPECT_EQ(b.egl.Executar(kIegl_MakeCurrent, Args(kDisplayUnico, 0x1234u, 0x1234u, 0x5678u), &r),
            ResultadoGl::Recusado);
  // Contexto nulo com superficie nao nula: o EGL chama a isso EGL_BAD_MATCH.
  EXPECT_EQ(b.egl.Executar(kIegl_MakeCurrent, Args(kDisplayUnico, 0x1234u, 0x1234u, 0), &r),
            ResultadoGl::Recusado);
  EXPECT_EQ(b.egl.Erro(), EGL_BAD_MATCH);
  // Contexto nulo e superficie nula: libertar o contexto corrente e valido.
  EXPECT_EQ(b.egl.Executar(kIegl_MakeCurrent, Args(kDisplayUnico, 0, 0, 0), &r), ResultadoGl::Feito);
  EXPECT_EQ(b.egl.ContextoCorrente(), 0u);
}

TEST(CicloDoEgl, OCriaContextoRecusaOVersionamento2) {
  Banco b;
  std::uint32_t r = 0;
  b.egl.Executar(kIegl_Initialize, Args(kDisplayUnico, 0, 0), &r);
  const std::vector<std::uint32_t> pedido = {EGL_CONTEXT_CLIENT_VERSION, 2};
  const std::uint32_t lista = EscreverLista(b, pedido, 0x00090000u);
  EXPECT_EQ(b.egl.Executar(kIegl_CreateContext, Args(kDisplayUnico, kConfigUnico, 0, lista), &r),
            ResultadoGl::Recusado);
  EXPECT_EQ(b.egl.ContextosVivos(), 0u);
  // O GL ES 2 seria servido por uma interface que nao existe aqui: aceitar o
  // pedido seria mentir ao jogo sobre o que ele vai encontrar.
  ASSERT_FALSE(b.egl.Ultimas().empty());
  EXPECT_NE(b.egl.Ultimas().back().motivo.find("EGL_CONTEXT_CLIENT_VERSION"), std::string::npos);
}

TEST(CicloDoEgl, OQuerySurfaceDaOTamanhoDaTela) {
  Banco b;
  std::uint32_t r = 0;
  b.egl.Executar(kIegl_Initialize, Args(kDisplayUnico, 0, 0), &r);
  b.egl.Executar(kIegl_CreateWindowSurface, Args(kDisplayUnico, kConfigUnico, 0, 0), &r);
  const std::uint32_t sup = r;
  const std::uint32_t saida = 0x00090400u;
  EXPECT_EQ(b.egl.Executar(kIegl_QuerySurface, Args(kDisplayUnico, sup, EGL_WIDTH, saida), &r),
            ResultadoGl::Feito);
  EXPECT_EQ(b.mem.Ler32(saida), static_cast<std::uint32_t>(Tela::kLargura));
  EXPECT_EQ(b.egl.Executar(kIegl_QuerySurface, Args(kDisplayUnico, sup, EGL_HEIGHT, saida), &r),
            ResultadoGl::Feito);
  EXPECT_EQ(b.mem.Ler32(saida), static_cast<std::uint32_t>(Tela::kAltura));
  EXPECT_EQ(b.egl.Executar(kIegl_QuerySurface, Args(kDisplayUnico, sup, EGL_LARGEST_PBUFFER, saida),
                           &r),
            ResultadoGl::Recusado);
}

// ---------------------------------------------------------------------------
// 5. NENHUM CAMINHO MUDO (P2/P7)
// ---------------------------------------------------------------------------

TEST(NenhumCaminhoMudo, AsVinteEOitoChamadasFicamNoTraco) {
  Banco b;
  std::uint32_t r = 0;
  // Um estado a meio caminho: o display iniciado e uma superficie e um contexto
  // criados, para as chamadas de consulta terem o que responder.
  b.egl.Executar(kIegl_GetDisplay, Args(0), &r);
  b.egl.Executar(kIegl_Initialize, Args(kDisplayUnico, 0, 0), &r);
  b.egl.Executar(kIegl_CreateWindowSurface, Args(kDisplayUnico, kConfigUnico, 0, 0), &r);
  b.egl.Executar(kIegl_CreateContext, Args(kDisplayUnico, kConfigUnico, 0, 0), &r);
  b.egl.Executar(kIegl_MakeCurrent,
                 Args(kDisplayUnico, kPrimeiraSuperficie, kPrimeiraSuperficie, kPrimeiroContexto), &r);
  for (std::uint32_t s = 3; s < kIeglSlots; ++s) {
    const std::size_t antes = b.destino.eventos.size();
    const ArgumentosGl a = Args(kDisplayUnico, kConfigUnico, kPrimeiraSuperficie, kPrimeiroContexto,
                                0x80080000u);
    const ResultadoGl res = b.egl.Executar(s, a, &r);
    // UM RESULTADO ENTRE OS TRES DO DESENHO, e nao um `bool` a fingir sucesso.
    EXPECT_TRUE(res == ResultadoGl::Feito || res == ResultadoGl::Recusado ||
                res == ResultadoGl::NaoImplementado)
        << "slot " << s;
    // E UMA LINHA NO TRACO, com o nome do metodo. Nao ha chamada anonima.
    //
    // NAO se conta "uma linha por chamada": uma RECUSA emite duas (a chamada, com
    // `EGL_<metodo>`, e a falta, com o nome em maiusculas). Contar linhas aqui
    // seria fixar o numero de linhas de OUTRO modulo como se fosse contrato --
    // o que se exige e que a chamada esteja LA, com o nome dela.
    EXPECT_GT(b.destino.eventos.size(), antes) << "slot " << s << " (" << NomeIegl(s) << ")";
    bool achou_nome = false;
    for (std::size_t k = antes; k < b.destino.eventos.size(); ++k) {
      if (b.destino.eventos[k].nome == std::string("EGL_") + NomeIegl(s)) achou_nome = true;
    }
    EXPECT_TRUE(achou_nome) << "slot " << s << " (" << NomeIegl(s) << ") nao deixou linha";
  }
}

TEST(NenhumCaminhoMudo, ARecusaFicaNaListaDeFaltas) {
  Banco b;
  std::uint32_t r = 0;
  b.egl.Executar(kIegl_CreatePbufferSurface, Args(kDisplayUnico, kConfigUnico, 0), &r);
  bool achou = false;
  for (const auto& par : b.egl.Recusas()) {
    if (par.first == "eglCreatePbufferSurface" && par.second > 0) achou = true;
  }
  // A LISTA DO QUE FALTA, por nome: a arvore antiga nao tinha isto em lado nenhum,
  // e por isso nao havia como saber o que era fachada.
  EXPECT_TRUE(achou);
  for (const auto& par : b.traco.ContagemFaltas()) {
    if (par.first == "eglCreatePbufferSurface") achou = true;
  }
  EXPECT_TRUE(achou);
}

// ---------------------------------------------------------------------------
// 6. A CABLAGEM NO DESPACHO (salta enquanto o remendo nao estiver aplicado)
// ---------------------------------------------------------------------------

#ifdef ZB2_CABLAGEM_GL

// A CENA: um modulo falso com um thunk do wrapper, e a chamada a passar pelo
// `Despacho` do motor -- que e o codigo que corre na bateria.
struct CenaDoDespacho {
  Tempo tempo;
  Traco traco;
  DestinoMemoria destino;
  Memoria mem;
  Alocador al;
  Vfs vfs;
  Saidas saidas;
  ArmInterpreter cpu;
  Despacho despacho;

  CenaDoDespacho()
      : traco("teste-da-cablagem", &tempo),
        mem(&traco),
        al(mem, 0x80200000u, 0x00400000u, &traco),
        cpu(mem, &traco),
        despacho(mem, traco, al, vfs) {
    traco.JuntarDestino(&destino);
    mem.EscritorUnico("teste");
    saidas.base = 0xF0000000u;
    saidas.passo = 4;
    saidas.quantos = 100000;
    saidas.ativa = true;
    cpu.ConfigurarSaidas(saidas);
    despacho.InstalarAjudantes(saidas, 0x80010000u);
  }
};

// Entra no despacho pelo endereco de saida do slot `idx`, como o guest entra: o PC
// fica no endereco da faixa e os registos levam os argumentos. Devolve o r0.
// A sentinela do laco do motor (a mesma do `despacho.cpp`): o PC aqui significa
// "a chamada retornou", e nao "o guest saltou para o nada".
constexpr std::uint32_t kSentinelaDoLaco = 0xFFFFFFF0u;

std::uint32_t EntrarPeloSlot(CenaDoDespacho& c, std::uint32_t idx, std::uint32_t r0,
                             std::uint32_t r1 = 0) {
  c.cpu.Repor(c.saidas.Endereco(idx), 0x80080000u);
  c.cpu.Set(kR0, r0);
  c.cpu.Set(kR1, r1);
  c.cpu.Set(kLR, kSentinelaDoLaco);
  c.despacho.Correr(c.cpu, 10000, 0);
  return c.cpu.Get(kR0);
}

TEST(CablagemGl, ODespachoEntregaAChamadaAoIgl) {
  CenaDoDespacho c;
  const std::uint64_t antes = c.despacho.IglRef().Chamadas();
  EntrarPeloSlot(c, kVtableIgl + gl_slots::kIgl_Clear, gl_slots::GL_COLOR_BUFFER_BIT);
  EXPECT_EQ(c.despacho.IglRef().Chamadas(), antes + 1)
      << "a chamada nao chegou ao IGL pelo despacho";
  EXPECT_EQ(c.despacho.IglRef().ChamadasDoSlot(gl_slots::kIgl_Clear), 1u);
  EXPECT_EQ(c.despacho.IglRef().MascaraDeLimpeza(), static_cast<std::uint32_t>(gl_slots::GL_COLOR_BUFFER_BIT));
}

TEST(CablagemGl, ODespachoEntregaAChamadaAoIegl) {
  CenaDoDespacho c;
  const std::uint64_t antes = c.despacho.EglRef().Chamadas();
  EntrarPeloSlot(c, kVtableIegl + gl_slots::kIegl_GetDisplay, 0);
  EXPECT_EQ(c.despacho.EglRef().Chamadas(), antes + 1);
  EXPECT_EQ(c.despacho.EglRef().ChamadasDoSlot(gl_slots::kIegl_GetDisplay), 1u);
}

TEST(CablagemGl, AORDEMDoRamoEAGuardaDoOitavoCaso) {
  // ESTE E O TESTE DA ORDEM, e sem ele o defeito volta em silencio.
  //
  // O ramo generico `idx >= kBaseDoShell` (2000) vem ANTES, no codigo do
  // `Correr`, de tudo o que esta abaixo dele. A faixa do GL e 30000/31000, e um
  // ramo novo escrito DEPOIS do generico nunca corre: a chamada e atendida como
  // `IFileMgr::slot23007` (medido na sonda antes do remendo: 41 de 41 chamadas do
  // `ddragonz` engolidas, com o nome de outra interface).
  //
  // **O erro de ordem apareceu SETE vezes neste trabalho.** Aqui ele e apanhado
  // pelo NOME que o despacho registou: se o pedido chegou ao IGL, o traco tem o
  // nome do GL; se foi engolido, tem o nome de outra interface.
  CenaDoDespacho c;
  EntrarPeloSlot(c, kVtableIgl + gl_slots::kIgl_Clear, gl_slots::GL_COLOR_BUFFER_BIT);
  std::size_t faltas_com_nome_de_outra_iface = 0;
  for (const auto& par : c.traco.ContagemFaltas()) {
    if (par.first.find("IFileMgr::") != std::string::npos ||
        par.first.find("IDisplay::") != std::string::npos ||
        par.first.find("IShell::") != std::string::npos) {
      faltas_com_nome_de_outra_iface += par.second;
    }
  }
  EXPECT_EQ(faltas_com_nome_de_outra_iface, 0u)
      << "um pedido da faixa do GL foi atendido por um ramo generico e ficou com o NOME de outra "
         "interface -- e a OITAVA vez deste erro de ordem";
  // E O NOME DO METODO TEM DE ESTAR NO TRACO. `glClear` e SERVIDO (o estado e
  // acumulado) e o detalhe diz que nenhum pixel foi escrito; o que se exige aqui e
  // que a chamada tenha deixado uma linha com o NOME dela, e nao "algo de GL".
  std::size_t linhas_com_o_nome = 0;
  for (const auto& e : c.destino.eventos) {
    if (e.nome == "GL_glClear") ++linhas_com_o_nome;
  }
  EXPECT_EQ(linhas_com_o_nome, 1u)
      << "o `glClear` nao ficou registado com o NOME dele (um caminho mudo, e nao um log)";
}

TEST(CablagemGl, OsDoisObjectsSaoDistintos) {
  CenaDoDespacho c;
  EXPECT_NE(c.despacho.IglRef().Objeto(), c.despacho.EglRef().Objeto());
  // As duas vtables tem de estar em enderecos diferentes e cada objecto apontar
  // para a sua.
  EXPECT_NE(c.despacho.IglRef().Vtable(), c.despacho.EglRef().Vtable());
  EXPECT_EQ(c.mem.Ler32(c.despacho.IglRef().Objeto()), c.despacho.IglRef().Vtable());
  EXPECT_EQ(c.mem.Ler32(c.despacho.EglRef().Objeto()), c.despacho.EglRef().Vtable());
}

#else

TEST(CablagemGl, ORemendoNaoEstaAplicado) {
  GTEST_SKIP()
      << "a cablagem do GL no despacho NAO esta aplicada: `core/brew/despacho.h` nao "
         "define `ZB2_CABLAGEM_GL`. Aplica-a com\n"
         "    python3 tools/remendo_gl.py <raiz_da_arvore>\n"
         "e o remendo esta explicado em docs/rewrite/REMENDO-GL-DESPACHO.md.\n"
         "Com a cablagem ausente, medido: 0 de 41 chamadas do `ddragonz` chegam ao IGL\n"
         "(`./build/zb2_sonda_gl <mods>/274754/ddragonz.mod` sai com 1).";
}

#endif

}  // namespace
}  // namespace zb2::brew
