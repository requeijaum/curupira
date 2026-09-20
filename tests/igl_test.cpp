// Testes do IGL: a tabela de slots, o estado acumulado e as guardas.
//
// O QUE ESTES TESTES PROTEGEM, e porque existem:
//
// 1. A TABELA DE SLOTS. Na arvore antiga o mapa do IGLES11 foi COPIADO de outro
//    emulador, e nao medido. Aqui a tabela vem de `AEEGL.h` (gerada por
//    `tools/gerar_slots.py`) e e conferida contra os 102 thunks do `conftest.elf`
//    pela guarda `tools/verificar_slots_gl.sh`. Este ficheiro fixa os dois
//    valores que foram medidos DUAS vezes por caminhos independentes: o
//    `glClear` no slot 7 e o `glCullFace` no slot 19.
//
// 2. O ESTADO. Um produto de matrizes errado nao da erro: da uma imagem errada.
//    Por isso os numeros estao escritos a mao aqui, com a conta no comentario.
//
// 3. NENHUM CAMINHO MUDO. Cada chamada tem de aparecer no traco, com nome, e
//    cada caminho sem implementacao tem de RECUSAR. Foi um stub que devolvia
//    sucesso e nao fazia nada que descartou 86 377 chamadas de `glCullFace`.

#include <gtest/gtest.h>

#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "core/brew/classes.h"
#include "core/brew/ecra.h"
#include "core/brew/igl.h"
#include "core/cpu/arm_interpreter.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"
#include "tools/igles_slots.inc"

namespace zb2::brew {
namespace {

using namespace gl_slots;

// Tudo o que o modulo precisa para existir: memoria do guest, o registador, e a
// faixa de saida (o espaco de enderecos que o despacho usa para identificar o
// slot chamado).
struct Banco {
  Tempo tempo;
  Traco traco;
  DestinoMemoria destino;
  Memoria mem;
  Saidas saidas;
  Igl igl;

  Banco()
      : traco("teste-do-igl", &tempo),
        mem(&traco),
        igl(mem, traco) {
    traco.JuntarDestino(&destino);
    mem.EscritorUnico("teste");
    saidas.base = 0xF0000000u;
    saidas.passo = 4;
    saidas.quantos = 100000;
    saidas.ativa = true;
  }
};

// `GLfixed` e 16.16. Escrever 1.0f como 65536 e a metade de um teste que apanha
// quem confunda fracao com inteiro.
std::uint32_t Fixo(float v) {
  return static_cast<std::uint32_t>(static_cast<std::int32_t>(v * 65536.0f));
}

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


ResultadoGl PedirReadPixels(Banco& b, int x, int y, int largura, int altura,
                            std::uint32_t formato, std::uint32_t tipo, std::uint32_t pixels) {
  constexpr std::uint32_t kPilha = 0x0002F000u;
  b.mem.Escrever32(kPilha + 0u, formato);
  b.mem.Escrever32(kPilha + 4u, tipo);
  b.mem.Escrever32(kPilha + 8u, pixels);
  return b.igl.Executar(kIgl_ReadPixels,
                        Args(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                             static_cast<std::uint32_t>(largura), static_cast<std::uint32_t>(altura),
                             kPilha),
                        nullptr);
}

// ---------------------------------------------------------------------------
// 1. A TABELA DE SLOTS
// ---------------------------------------------------------------------------

TEST(TabelaDoIgl, TamanhoVemDoCabecalho) {
  // `AEEGL.h`: INHERIT_IQueryInterface (3 slots) + 77 metodos = 80.
  EXPECT_EQ(kIglSlots, 80u);
  EXPECT_EQ(kIeglSlots, 28u);
}

TEST(TabelaDoIgl, CabecaEAddRefReleaseQueryInterface) {
  // `INHERIT_IQueryInterface` de `AEEIQI.h`, nesta ordem. Foi confundir esta
  // cabeca com `INHERIT_IBase` (dois membros) que deslocou TODOS os slots do
  // IShell por um, e o custo foi uma ronda.
  EXPECT_EQ(kIgl_AddRef, 0u);
  EXPECT_EQ(kIgl_Release, 1u);
  EXPECT_EQ(kIgl_QueryInterface, 2u);
  EXPECT_EQ(kIgl_ActiveTexture, 3u);  // o primeiro metodo proprio
}

TEST(TabelaDoIgl, DoisSlotsMedidosDuasVezes) {
  // Medicao 1 -- `conftest.elf` (o `.mod` de exemplo do proprio SDK), desmonte
  // do `glClear` em 0x282b8: `ldr r1,[r1,#28]` -> 28/4 = 7.
  EXPECT_EQ(kIgl_Clear, 7u);
  // Medicao 2 -- o mesmo ficheiro, `glCullFace` em 0x28500: `ldr r1,[r1,#76]`
  // -> 76/4 = 19. O cabecalho `AEEGL.h` poe o `glCullFace` na posicao 16 depois
  // da cabeca de 3 -> 19. As duas fontes concordam.
  EXPECT_EQ(kIgl_CullFace, 19u);
}

TEST(TabelaDoIgl, NomesCompletosEUnicos) {
  std::vector<std::string> vistos;
  for (std::uint32_t s = 0; s < kIglSlots; ++s) {
    const std::string nome = NomeIgl(s);
    EXPECT_FALSE(nome.empty()) << "slot " << s;
    for (const auto& outro : vistos) EXPECT_NE(nome, outro) << "nome repetido: " << nome;
    vistos.push_back(nome);
  }
  EXPECT_EQ(vistos.size(), 80u);
}

TEST(TabelaDoIgl, TodoOMetodoProprioEUmGl) {
  // A convencao do cabecalho: os 77 metodos proprios do IGL comecam por `gl`.
  // Se a lista perdesse um nome, a contagem cai e o teste di-lo -- e uma tabela
  // copiada de outro emulador nao tem esta forma.
  std::uint32_t quantos_gl = 0;
  for (std::uint32_t s = kIgl_ActiveTexture; s < kIglSlots; ++s) {
    const std::string nome = NomeIgl(s);
    if (nome.rfind("gl", 0) == 0) ++quantos_gl;
  }
  EXPECT_EQ(quantos_gl, 77u);
}

TEST(TabelaDoIgl, ForaDaTabelaNaoLeMemoriaEstranha) {
  // Um slot fora do intervalo NAO pode indexar a lista de nomes.
  EXPECT_STREQ(NomeIgl(kIglSlots), "slot_fora_da_tabela");
  EXPECT_STREQ(NomeIgl(9999), "slot_fora_da_tabela");
}

TEST(TabelaDoIgl, InstalarCablaEObjEConfere) {
  Banco b;
  EXPECT_EQ(b.igl.Instalar(b.saidas), kIglSlots);
  // O objecto: `[0]` = vtable, `[4]` = contagem de referencias (do
  // `ConstruirObjeto` do `core/brew/interface.h`).
  EXPECT_EQ(b.mem.Ler32(b.igl.Objeto()), b.igl.Vtable());
  EXPECT_EQ(b.mem.Ler32(b.igl.Objeto() + 4), 1u);
  // A IBase fica com os enderecos 3 e 4, que e a convencao do despacho.
  EXPECT_EQ(b.mem.Ler32(b.igl.Vtable() + 0), b.saidas.Endereco(3));
  EXPECT_EQ(b.mem.Ler32(b.igl.Vtable() + 4), b.saidas.Endereco(4));
  // E cada slot do `glCullFace` aponta para o endereco de saida DO SEU slot.
  EXPECT_EQ(b.mem.Ler32(b.igl.Vtable() + kIgl_CullFace * 4),
            b.saidas.Endereco(kVtableIgl + kIgl_CullFace));
}

// ---------------------------------------------------------------------------
// 2. O ESTADO: AS MATRIZES
// ---------------------------------------------------------------------------

// Le a matriz corrente; a identidade e o ponto de partida.
const float* M(Banco& b) { return b.igl.MatrizCorrente(); }

TEST(EstadoGl, IdentityDepoisTranslateTemOsNumerosCertos) {
  Banco b;
  EXPECT_EQ(b.igl.Executar(kIgl_LoadIdentity, Args(), nullptr), ResultadoGl::Feito);
  EXPECT_EQ(b.igl.Executar(kIgl_Translatex, Args(Fixo(1.0f), Fixo(2.0f), Fixo(3.0f)), nullptr),
            ResultadoGl::Feito);
  const float* m = M(b);
  // A CONTA, escrita: M := I * T. Em column-major (indice = coluna*4 + linha) a
  // translacao cai em m[12], m[13], m[14]. Diagonal 1, resto 0.
  EXPECT_FLOAT_EQ(m[12], 1.0f);
  EXPECT_FLOAT_EQ(m[13], 2.0f);
  EXPECT_FLOAT_EQ(m[14], 3.0f);
  for (int k = 0; k < 4; ++k) EXPECT_FLOAT_EQ(m[k * 4 + k], 1.0f);
  EXPECT_FLOAT_EQ(m[0], 1.0f);
  EXPECT_FLOAT_EQ(m[1], 0.0f);
  EXPECT_FLOAT_EQ(m[3], 0.0f);
  EXPECT_FLOAT_EQ(m[15], 1.0f);
}

TEST(EstadoGl, OProdutoEAPosMultiplicacaoDaEsquerda) {
  Banco b;
  // M := I * T(1,0,0) * S(2,2,2). A translacao NAO e afectada pela escala (o GL
  // pos-multiplica: a translacao fica na ultima coluna de M, e o S so mexe nas
  // linhas 0..2). Se a ordem estivesse trocada, m[12] seria 2.
  b.igl.Executar(kIgl_LoadIdentity, Args(), nullptr);
  b.igl.Executar(kIgl_Translatex, Args(Fixo(1.0f), Fixo(0.0f), Fixo(0.0f)), nullptr);
  b.igl.Executar(kIgl_Scalex, Args(Fixo(2.0f), Fixo(2.0f), Fixo(2.0f)), nullptr);
  const float* m = M(b);
  EXPECT_FLOAT_EQ(m[0], 2.0f);
  EXPECT_FLOAT_EQ(m[5], 2.0f);
  EXPECT_FLOAT_EQ(m[10], 2.0f);
  EXPECT_FLOAT_EQ(m[12], 1.0f);
  // Agora T2(0,1,0): a translacao NOVA e `M[linha][1] * 1 + M[linha][3]`.
  // Para a linha 1: M[1][1] = 2, M[1][3] = 0 -> m[13] = 2.
  b.igl.Executar(kIgl_Translatex, Args(Fixo(0.0f), Fixo(1.0f), Fixo(0.0f)), nullptr);
  EXPECT_FLOAT_EQ(m[13], 2.0f);
  EXPECT_FLOAT_EQ(m[12], 1.0f);
}

TEST(EstadoGl, RotacaoDeNoventaGrausEmZ) {
  Banco b;
  b.igl.Executar(kIgl_LoadIdentity, Args(), nullptr);
  // 90 graus em torno de (0,0,1): cos=0, sin=1. A matriz de rotacao tem
  // R[0][1] = -1 e R[1][0] = 1, o que em column-major da m[4] = -1 e m[1] = 1.
  b.igl.Executar(kIgl_Rotatex, Args(Fixo(90.0f), Fixo(0.0f), Fixo(0.0f), Fixo(1.0f)), nullptr);
  const float* m = M(b);
  EXPECT_NEAR(m[4], -1.0f, 1e-5f);
  EXPECT_NEAR(m[1], 1.0f, 1e-5f);
  EXPECT_NEAR(m[0], 0.0f, 1e-6f);
  EXPECT_NEAR(m[5], 0.0f, 1e-6f);
  EXPECT_NEAR(m[10], 1.0f, 1e-6f);
}

TEST(EstadoGl, FrustumComOsNumerosDaFormula) {
  Banco b;
  b.igl.Executar(kIgl_MatrixMode, Args(GL_PROJECTION), nullptr);
  b.igl.Executar(kIgl_LoadIdentity, Args(), nullptr);
  // frustum(-1, 1, -1, 1, 1, 2). Os dois ultimos argumentos (near e far) vao na
  // PILHA, e nao nos registos: sao seis argumentos.
  //
  // A CONTA, com o indice em column-major (indice = coluna*4 + linha):
  //   m[0]  = (linha 0, coluna 0) = 2n/(r-l)     = 2*1/2 = 1
  //   m[5]  = (linha 1, coluna 1) = 2n/(t-b)     = 1
  //   m[10] = (linha 2, coluna 2) = -(f+n)/(f-n) = -3/1  = -3
  //   m[11] = (linha 3, coluna 2) = -1
  //   m[14] = (linha 2, coluna 3) = -(2fn)/(f-n) = -4/1  = -4
  const std::uint32_t pilha = 0x00020000u;
  b.mem.Escrever32(pilha, Fixo(1.0f));      // arg 4: near
  b.mem.Escrever32(pilha + 4, Fixo(2.0f));  // arg 5: far
  const ArgumentosGl a = Args(Fixo(-1.0f), Fixo(1.0f), Fixo(-1.0f), Fixo(1.0f), pilha);
  EXPECT_EQ(b.igl.Executar(kIgl_Frustumx, a, nullptr), ResultadoGl::Feito);
  const float* m = M(b);
  EXPECT_FLOAT_EQ(m[0], 1.0f);
  EXPECT_FLOAT_EQ(m[5], 1.0f);
  EXPECT_FLOAT_EQ(m[10], -3.0f);
  EXPECT_FLOAT_EQ(m[11], -1.0f);
  EXPECT_FLOAT_EQ(m[14], -4.0f);
}

TEST(EstadoGl, PushEPopVoltamAoQueEstava) {
  Banco b;
  b.igl.Executar(kIgl_LoadIdentity, Args(), nullptr);
  b.igl.Executar(kIgl_PushMatrix, Args(), nullptr);
  b.igl.Executar(kIgl_Translatex, Args(Fixo(7.0f), Fixo(0.0f), Fixo(0.0f)), nullptr);
  EXPECT_FLOAT_EQ(M(b)[12], 7.0f);
  b.igl.Executar(kIgl_PopMatrix, Args(), nullptr);
  EXPECT_FLOAT_EQ(M(b)[12], 0.0f);
  EXPECT_EQ(b.igl.TopoDaPilha(kModoModelView), 0);
}

TEST(EstadoGl, PopEmPilhaVaziaRecusa) {
  Banco b;
  const auto r = b.igl.Executar(kIgl_PopMatrix, Args(), nullptr);
  EXPECT_EQ(r, ResultadoGl::Recusado);
  EXPECT_EQ(b.igl.Recusas().count("glPopMatrix"), 1u);
}

TEST(EstadoGl, PilhasTemAProfundidadeQueOGuestVe) {
  Banco b;
  // A afirmacao "as pilhas tem 16/2/2" NAO fica no comentario: e o numero que o
  // proprio `glGetIntegerv` devolve, e o teste le-o do guest.
  const std::uint32_t destino = 0x00030000u;
  b.igl.Executar(kIgl_GetIntegerv, Args(GL_MAX_MODELVIEW_STACK_DEPTH, destino), nullptr);
  EXPECT_EQ(b.mem.Ler32(destino), static_cast<std::uint32_t>(kFundoModelView));
  b.igl.Executar(kIgl_GetIntegerv, Args(GL_MAX_PROJECTION_STACK_DEPTH, destino), nullptr);
  EXPECT_EQ(b.mem.Ler32(destino), static_cast<std::uint32_t>(kFundoProjection));
  b.igl.Executar(kIgl_GetIntegerv, Args(GL_MAX_TEXTURE_STACK_DEPTH, destino), nullptr);
  EXPECT_EQ(b.mem.Ler32(destino), static_cast<std::uint32_t>(kFundoTexture));

  // E o LIMITE e mesmo esse: 15 pushes (a matriz inicial + 15 = 16) passam, o
  // 16.o recusa.
  int feitos = 0;
  for (int k = 0; k < 40; ++k) {
    if (b.igl.Executar(kIgl_PushMatrix, Args(), nullptr) == ResultadoGl::Feito) ++feitos;
  }
  EXPECT_EQ(feitos, kFundoModelView - 1);
  EXPECT_EQ(b.igl.Recusas().count("glPushMatrix"), 1u);
}

TEST(EstadoGl, PilhaDaProjecaoEAMaisCurta) {
  Banco b;
  b.igl.Executar(kIgl_MatrixMode, Args(GL_PROJECTION), nullptr);
  EXPECT_EQ(b.igl.Executar(kIgl_PushMatrix, Args(), nullptr), ResultadoGl::Feito);
  EXPECT_EQ(b.igl.Executar(kIgl_PushMatrix, Args(), nullptr), ResultadoGl::Recusado);
}

TEST(EstadoGl, ModoDeMatrizInvalidoRecusaENaoMudaNada) {
  Banco b;
  b.igl.Executar(kIgl_MatrixMode, Args(GL_MODELVIEW), nullptr);
  const auto r = b.igl.Executar(kIgl_MatrixMode, Args(0x1234u), nullptr);
  EXPECT_EQ(r, ResultadoGl::Recusado);
  EXPECT_EQ(b.igl.ModoDeMatriz(), static_cast<std::uint32_t>(kModoModelView));
}

// ---------------------------------------------------------------------------
// 3. ESTADO: COR, TEXTURAS, ARRAYS
// ---------------------------------------------------------------------------

TEST(EstadoGl, CorEm16_16ViraRGBA8) {
  Banco b;
  // (1.0, 0.5, 0.0, 1.0) -> R=255, G=128, B=0, A=255, empacotado R nos bits 0-7.
  b.igl.Executar(kIgl_Color4x,
                 Args(Fixo(1.0f), Fixo(0.5f), Fixo(0.0f), Fixo(1.0f)), nullptr);
  EXPECT_EQ(b.igl.Cor(), 0xFF0080FFu);
}

TEST(EstadoGl, CullFaceAcumulaERecusaOModoDesconhecido) {
  Banco b;
  EXPECT_EQ(b.igl.Executar(kIgl_CullFace, Args(GL_FRONT), nullptr), ResultadoGl::Feito);
  EXPECT_EQ(b.igl.CullFace(), GL_FRONT);
  EXPECT_EQ(b.igl.Executar(kIgl_CullFace, Args(0x9999u), nullptr), ResultadoGl::Recusado);
  // O estado NAO muda quando a chamada recusa.
  EXPECT_EQ(b.igl.CullFace(), GL_FRONT);
  EXPECT_EQ(b.igl.Recusas().count("glCullFace"), 1u);
}

TEST(EstadoGl, BindTextureSoAceitaTextura2D) {
  Banco b;
  EXPECT_EQ(b.igl.Executar(kIgl_Enable, Args(GL_TEXTURE_2D), nullptr), ResultadoGl::Feito);
  EXPECT_EQ(b.igl.Executar(kIgl_BindTexture, Args(GL_TEXTURE_2D, 3u), nullptr),
            ResultadoGl::Feito);
  EXPECT_EQ(b.igl.TexturaLigada(), 3u);
  EXPECT_EQ(b.igl.Executar(kIgl_BindTexture, Args(0x0DE0u, 4u), nullptr), ResultadoGl::Recusado);
  EXPECT_EQ(b.igl.TexturaLigada(), 3u);
}

TEST(EstadoGl, GenTexturesEscreveOsIdentificadoresNoGuest) {
  Banco b;
  const std::uint32_t lista = 0x00040000u;
  EXPECT_EQ(b.igl.Executar(kIgl_GenTextures, Args(3, lista), nullptr), ResultadoGl::Feito);
  EXPECT_EQ(b.igl.TexturasGeradas(), 3u);
  EXPECT_EQ(b.mem.Ler32(lista), 1u);
  EXPECT_EQ(b.mem.Ler32(lista + 4), 2u);
  EXPECT_EQ(b.mem.Ler32(lista + 8), 3u);
  EXPECT_EQ(b.igl.Executar(kIgl_GenTextures, Args(0, lista), nullptr), ResultadoGl::Recusado);
}

TEST(EstadoGl, SegundaUnidadeTemBindECoordenadasProprios) {
  Banco b;
  constexpr std::uint32_t kTextura1 = 0x84c1u;
  ASSERT_EQ(b.igl.Executar(kIgl_BindTexture, Args(GL_TEXTURE_2D, 7u), nullptr), ResultadoGl::Feito);
  ASSERT_EQ(b.igl.Executar(kIgl_ActiveTexture, Args(kTextura1), nullptr), ResultadoGl::Feito);
  ASSERT_EQ(b.igl.Executar(kIgl_BindTexture, Args(GL_TEXTURE_2D, 9u), nullptr), ResultadoGl::Feito);
  EXPECT_EQ(b.igl.TexturaLigada(), 7u);  // observador antigo continua a ser unidade 0
  EXPECT_EQ(b.igl.TexturaLigadaNaUnidade(kTextura1), 9u);
  // Upload na unidade 1 escreve a textura ligada nela; antes este ramo devolvia
  // sucesso e largava os pixels.
  constexpr std::uint32_t kPilha = 0x00103000u;
  const std::uint32_t resto[5] = {1u, 0u, GL_RGBA, GL_UNSIGNED_BYTE, 0x00104000u};
  for (int k = 0; k < 5; ++k) b.mem.Escrever32(kPilha + 4u * k, resto[k]);
  ArgumentosGl imagem = Args(GL_TEXTURE_2D, 0u, GL_RGBA, 1u);
  imagem.sp = kPilha;
  ASSERT_EQ(b.igl.Executar(kIgl_TexImage2D, imagem, nullptr), ResultadoGl::Feito);
  ASSERT_NE(b.igl.Textura(9u), nullptr);
  EXPECT_EQ(b.igl.Textura(9u)->ponteiro, 0x00104000u);
  ASSERT_EQ(b.igl.Executar(kIgl_Enable, Args(GL_TEXTURE_2D), nullptr), ResultadoGl::Feito);
  ASSERT_EQ(b.igl.Executar(kIgl_ClientActiveTexture, Args(kTextura1), nullptr), ResultadoGl::Feito);
  ASSERT_EQ(b.igl.Executar(kIgl_EnableClientState, Args(GL_TEXTURE_COORD_ARRAY), nullptr),
            ResultadoGl::Feito);
  ASSERT_EQ(b.igl.Executar(kIgl_TexCoordPointer, Args(2u, GL_FLOAT, 8u, 0x00102000u), nullptr),
            ResultadoGl::Feito);
  EXPECT_TRUE(b.igl.CoordenadasDeTexturaLigadas(kTextura1));
  ASSERT_NE(b.igl.CoordenadasDeTextura(kTextura1), nullptr);
  EXPECT_EQ(b.igl.CoordenadasDeTextura(kTextura1)->ponteiro, 0x00102000u);

  ASSERT_EQ(b.igl.Executar(kIgl_ActiveTexture, Args(GL_TEXTURE0), nullptr), ResultadoGl::Feito);
  ASSERT_EQ(b.igl.Executar(kIgl_BindTexture, Args(GL_TEXTURE_2D, 11u), nullptr), ResultadoGl::Feito);
  ASSERT_EQ(b.igl.Executar(kIgl_ClientActiveTexture, Args(GL_TEXTURE0), nullptr), ResultadoGl::Feito);
  ASSERT_EQ(b.igl.Executar(kIgl_TexCoordPointer, Args(2u, GL_FLOAT, 8u, 0x00101000u), nullptr),
            ResultadoGl::Feito);
  EXPECT_EQ(b.igl.TexturaLigada(), 11u);
  EXPECT_EQ(b.igl.TexturaLigadaNaUnidade(kTextura1), 9u);
  ASSERT_NE(b.igl.Array(GL_TEXTURE_COORD_ARRAY), nullptr);
  EXPECT_EQ(b.igl.Array(GL_TEXTURE_COORD_ARRAY)->ponteiro, 0x00101000u);
}

TEST(EstadoGl, TexEnvGuardaCombineRgbBasicoSoNaUnidade1) {
  Banco b;
  constexpr std::uint32_t kTextura1 = 0x84c1u;
  ASSERT_EQ(b.igl.Executar(kIgl_ActiveTexture, Args(kTextura1), nullptr), ResultadoGl::Feito);
  EXPECT_EQ(b.igl.Executar(kIgl_TexEnvx, Args(kTextureEnv, kTextureEnvMode, kCombine), nullptr),
            ResultadoGl::Feito);
  EXPECT_EQ(b.igl.AmbienteDeTextura(kTextura1), kCombine);
  EXPECT_EQ(b.igl.Executar(kIgl_TexEnvx, Args(kTextureEnv, kCombineRgb, kAdd), nullptr),
            ResultadoGl::Feito);
  EXPECT_EQ(b.igl.CombineRgb(kTextura1), kAdd);
  EXPECT_EQ(b.igl.Executar(kIgl_TexEnvx, Args(kTextureEnv, kSource0Rgb, kPrevious), nullptr),
            ResultadoGl::Feito);
  EXPECT_EQ(b.igl.Source0Rgb(kTextura1), kPrevious);
  constexpr std::uint32_t kVet = 0x00040000u;
  b.mem.Escrever32(kVet, kTexture);
  EXPECT_EQ(b.igl.Executar(kIgl_TexEnvxv, Args(kTextureEnv, kSource1Rgb, kVet), nullptr),
            ResultadoGl::Feito);
  EXPECT_EQ(b.igl.Source1Rgb(kTextura1), kTexture);
  EXPECT_EQ(b.igl.Executar(kIgl_TexEnvx,
                           Args(kTextureEnv, kOperand0Rgb, kOneMinusSrcColor), nullptr),
            ResultadoGl::Feito);
  EXPECT_EQ(b.igl.Operand0Rgb(kTextura1), kOneMinusSrcColor);
  EXPECT_EQ(b.igl.Executar(kIgl_TexEnvx, Args(kTextureEnv, kOperand1Rgb, kSrcColor), nullptr),
            ResultadoGl::Feito);
  EXPECT_EQ(b.igl.Operand1Rgb(kTextura1), kSrcColor);

  // Alpha, source2, scales and other combine functions stay named refusals.
  EXPECT_EQ(b.igl.Executar(kIgl_TexEnvx, Args(kTextureEnv, kCombineAlpha, kModulate), nullptr),
            ResultadoGl::Recusado);
  EXPECT_EQ(b.igl.Executar(kIgl_TexEnvx, Args(kTextureEnv, kSource2Rgb, kTexture), nullptr),
            ResultadoGl::Recusado);
  EXPECT_EQ(b.igl.Executar(kIgl_TexEnvx, Args(kTextureEnv, kRgbScale, 2u), nullptr),
            ResultadoGl::Recusado);
  EXPECT_EQ(b.igl.Executar(kIgl_TexEnvx, Args(kTextureEnv, kCombineRgb, kSubtract), nullptr),
            ResultadoGl::Recusado);
  EXPECT_EQ(b.igl.CombineRgb(kTextura1), kAdd);

  // Combine is deliberately unit1-only. Unit0 keeps its legacy paths.
  ASSERT_EQ(b.igl.Executar(kIgl_ActiveTexture, Args(GL_TEXTURE0), nullptr), ResultadoGl::Feito);
  EXPECT_EQ(b.igl.Executar(kIgl_TexEnvx, Args(kTextureEnv, kTextureEnvMode, kCombine), nullptr),
            ResultadoGl::Recusado);
  EXPECT_EQ(b.igl.AmbienteDeTextura(GL_TEXTURE0), kReplace);
}

TEST(EstadoGl, TexImage2DRegistraAsDimensoesSemCopiarPixeis) {
  Banco b;
  const std::uint32_t pilha = 0x00050000u;
  // glTexImage2D(alvo, level, formato interno, largura, altura, border, formato,
  //              tipo, dados): o ALVO, o LEVEL, o FORMATO INTERNO e a LARGURA vao
  // em r0..r3; os outros cinco vao na PILHA, a partir do `sp`.
  const std::uint32_t resto[5] = {32u, 0u, GL_RGBA, GL_UNSIGNED_BYTE, 0x00060000u};
  for (int k = 0; k < 5; ++k) b.mem.Escrever32(pilha + 4u * k, resto[k]);
  ArgumentosGl a = Args(GL_TEXTURE_2D, 0, GL_RGBA, 64);
  a.sp = pilha;
  b.igl.Executar(kIgl_BindTexture, Args(GL_TEXTURE_2D, 9u), nullptr);
  EXPECT_EQ(b.igl.Executar(kIgl_TexImage2D, a, nullptr), ResultadoGl::Feito);
  const EstadoDaTextura* t = b.igl.Textura(9);
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(t->largura, 64u);
  EXPECT_EQ(t->altura, 32u);
  EXPECT_EQ(t->uploade, 1u);
  EXPECT_FALSE(t->comprimida);
}

TEST(EstadoGl, CompressedTexImage2DDecodificaAtitcParaTexturaDoHost) {
  Banco b;
  constexpr std::uint32_t kPilha = 0x00050000u, kDados = 0x00051000u;
  // ATITC RGB 4x4: vermelho/verde e seletores 0..3 repetidos.
  const std::uint8_t bruto[8] = {0x00, 0x7c, 0xe0, 0x07, 0xe4, 0xe4, 0xe4, 0xe4};
  for (std::uint32_t i = 0; i < 8; ++i) b.mem.Escrever8(kDados + i, bruto[i]);
  // Args 4..7: altura, border, imageSize, data.
  const std::uint32_t resto[4] = {4u, 0u, 8u, kDados};
  for (int i = 0; i < 4; ++i) b.mem.Escrever32(kPilha + 4u * i, resto[i]);
  ArgumentosGl a = Args(GL_TEXTURE_2D, 0u, 0x8c92u, 4u);
  a.sp = kPilha;
  ASSERT_EQ(b.igl.Executar(kIgl_BindTexture, Args(GL_TEXTURE_2D, 33u), nullptr), ResultadoGl::Feito);
  ASSERT_EQ(b.igl.Executar(kIgl_CompressedTexImage2D, a, nullptr), ResultadoGl::Feito);
  const EstadoDaTextura* t = b.igl.Textura(33u);
  ASSERT_NE(t, nullptr);
  ASSERT_TRUE(t->comprimida);
  ASSERT_NE(t->texels_descodificados, nullptr);
  ASSERT_EQ(t->texels_descodificados->size(), 16u);
  EXPECT_EQ((*t->texels_descodificados)[0].r, 255);
  EXPECT_EQ((*t->texels_descodificados)[1].r, 170);
  EXPECT_EQ((*t->texels_descodificados)[2].g, 159);
  EXPECT_EQ((*t->texels_descodificados)[3].g, 255);
}

TEST(EstadoGl, TexImage2DSemSpValidoRecusa) {
  Banco b;
  // Sem argumentos na pilha, o `Arg` leria o endereco 0. Recusar aqui e o que
  // impede uma recusa no sitio errado.
  ArgumentosGl a = Args(GL_TEXTURE_2D, 0, GL_RGBA, 64);
  a.reg[3] = 32;
  a.sp = 0;
  EXPECT_EQ(b.igl.Executar(kIgl_TexImage2D, a, nullptr), ResultadoGl::Recusado);
}

TEST(EstadoGl, ArraysDeVerticesGuardamOTipoEOPasso) {
  Banco b;
  EXPECT_EQ(b.igl.Executar(kIgl_VertexPointer,
                           Args(3, GL_FLOAT, 12, 0x00200000u), nullptr),
            ResultadoGl::Feito);
  const ArrayDeVertices* a = b.igl.Array(GL_VERTEX_ARRAY);
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(a->tamanho, 3);
  EXPECT_EQ(a->tipo, GL_FLOAT);
  EXPECT_EQ(a->passo, 12u);
  EXPECT_EQ(a->ponteiro, 0x00200000u);
  // Um tamanho fora de 2..4 recusa.
  EXPECT_EQ(b.igl.Executar(kIgl_VertexPointer, Args(9, GL_FLOAT, 0, 0x00200000u), nullptr),
            ResultadoGl::Recusado);
}

TEST(EstadoGl, PonteiroDeVerticesNuloRecusa) {
  Banco b;
  EXPECT_EQ(b.igl.Executar(kIgl_VertexPointer, Args(3, GL_FLOAT, 0, 0), nullptr),
            ResultadoGl::Recusado);
}

// ---------------------------------------------------------------------------
// 4. O DESENHO: A RECUSA E A MEDICAO
// ---------------------------------------------------------------------------

TEST(EstadoGl, DrawArraysSemArrayLigadoRecusaENaoContaVertices) {
  Banco b;
  const auto r = b.igl.Executar(kIgl_DrawArrays, Args(GL_TRIANGLES, 0, 30), nullptr);
  EXPECT_EQ(r, ResultadoGl::Recusado);
  EXPECT_EQ(b.igl.Vertices(), 0u);
  EXPECT_EQ(b.igl.Desenhos(), 0u);
}

TEST(EstadoGl, DrawArraysSemTelaRecusaMasContaAGeometria) {
  // ESTE TESTE MUDOU DE NOME E DE ASSERT NO COMMIT DO RASTERIZADOR, e a mudanca
  // e a informacao: o que faltava a 13/09 era o rasterizador, e o que falta
  // agora a este `Banco` e a SUPERFICIE -- o `Banco` do teste nao tem `Tela`
  // nenhuma, e o `Igl` so desenha para uma tela ligada por
  // `Igl::DefinirTela`. Sem ela a recusa continua a ser recusa, e nao "sucesso
  // sem pixel" (P2). Os pixels a serio estao em `tests/rasterizador_test.cpp`.
  Banco b;
  b.igl.Executar(kIgl_EnableClientState, Args(GL_VERTEX_ARRAY), nullptr);
  b.igl.Executar(kIgl_VertexPointer, Args(3, GL_FLOAT, 0, 0x00200000u), nullptr);
  EXPECT_FALSE(b.igl.TemTela());
  const auto r = b.igl.Executar(kIgl_DrawArrays, Args(GL_TRIANGLES, 0, 30), nullptr);
  // RECUSA, e nao "sucesso": sem tela nenhum pixel fica escrito.
  EXPECT_EQ(r, ResultadoGl::Recusado);
  // Mas a geometria foi mesmo submetida, e isso mede-se.
  EXPECT_EQ(b.igl.Desenhos(), 1u);
  EXPECT_EQ(b.igl.Vertices(), 30u);
  const std::string motivo = b.igl.Ultimas().back().motivo;
  EXPECT_NE(motivo.find("TELA LIGADA"), std::string::npos) << motivo;
}

TEST(EstadoGl, DrawArraysComPrimitivaInvalidaRecusa) {
  Banco b;
  b.igl.Executar(kIgl_EnableClientState, Args(GL_VERTEX_ARRAY), nullptr);
  b.igl.Executar(kIgl_VertexPointer, Args(3, GL_FLOAT, 0, 0x00200000u), nullptr);
  EXPECT_EQ(b.igl.Executar(kIgl_DrawArrays, Args(0x7777u, 0, 30), nullptr),
            ResultadoGl::Recusado);
  EXPECT_EQ(b.igl.Vertices(), 0u);
}

TEST(EstadoGl, EnableDeTexturaNaoLigaOArrayDeVertices) {
  Banco b;
  // Um `glEnable(GL_TEXTURE_2D)` NAO e um `glEnableClientState(GL_VERTEX_ARRAY)`.
  // Misturar os dois faria um desenho passar a precondicao sem ter vertices.
  b.igl.Executar(kIgl_Enable, Args(GL_TEXTURE_2D), nullptr);
  EXPECT_FALSE(b.igl.ArrayDeClienteLigado(GL_VERTEX_ARRAY));
  b.igl.Executar(kIgl_EnableClientState, Args(GL_VERTEX_ARRAY), nullptr);
  EXPECT_TRUE(b.igl.ArrayDeClienteLigado(GL_VERTEX_ARRAY));
  // E o `glEnable(GL_VERTEX_ARRAY)` liga a MESMA coisa (o GL ES 1.x tem os dois
  // caminhos para a mesma capacidade).
  Banco c;
  c.igl.Executar(kIgl_Enable, Args(GL_VERTEX_ARRAY), nullptr);
  EXPECT_TRUE(c.igl.ArrayDeClienteLigado(GL_VERTEX_ARRAY));
}

// ---------------------------------------------------------------------------
// 5. NENHUM CAMINHO MUDO
// ---------------------------------------------------------------------------

TEST(RegistoGl, CadaChamadaEntraNoTracoComONome) {
  Banco b;
  const std::uint64_t antes = b.traco.TotalEmitidos();
  b.igl.Executar(kIgl_CullFace, Args(GL_FRONT), nullptr);
  b.igl.Executar(kIgl_MatrixMode, Args(GL_MODELVIEW), nullptr);
  b.igl.Executar(kIgl_LoadIdentity, Args(), nullptr);
  EXPECT_EQ(b.traco.TotalEmitidos(), antes + 3);
  EXPECT_EQ(b.igl.Chamadas(), 3u);
  // E o nome no traco e o do METODO, e nao "slot 19".
  EXPECT_EQ(b.destino.QuantosComNome("GL_glCullFace"), 1u);
  EXPECT_EQ(b.destino.QuantosComNome("GL_glMatrixMode"), 1u);
}

TEST(RegistoGl, SlotSemHandlerNaoFicaMudo) {
  Banco b;
  // `glCopyTexImage2D` existe no cabecalho e nao tem implementacao nesta etapa:
  // tem de RECUSAR E REGISTAR, e nao devolver um sucesso vazio.
  EXPECT_EQ(b.igl.Executar(kIgl_CopyTexImage2D, Args(), nullptr), ResultadoGl::NaoImplementado);
  EXPECT_EQ(b.igl.Recusas().count("glCopyTexImage2D"), 1u);
  EXPECT_EQ(b.destino.QuantosComNome("GL_glCopyTexImage2D"), 1u);
}

TEST(RegistoGl, SlotForaDaTabelaRecusaComNomeLegivel) {
  Banco b;
  EXPECT_EQ(b.igl.Executar(900, Args(), nullptr), ResultadoGl::NaoImplementado);
  EXPECT_EQ(b.igl.Recusas().count("slot_fora_da_tabela"), 1u);
}

TEST(RegistoGl, AContagemPorSlotDizQuemPedeOQue) {
  Banco b;
  for (int k = 0; k < 5; ++k) b.igl.Executar(kIgl_CullFace, Args(GL_BACK), nullptr);
  b.igl.Executar(kIgl_Clear, Args(GL_COLOR_BUFFER_BIT), nullptr);
  EXPECT_EQ(b.igl.ChamadasDoSlot(kIgl_CullFace), 5u);
  EXPECT_EQ(b.igl.ChamadasDoSlot(kIgl_Clear), 1u);
  EXPECT_EQ(b.igl.ChamadasDoSlot(kIgl_CullFace) + b.igl.ChamadasDoSlot(kIgl_Clear),
            b.igl.Chamadas());
}

TEST(RegistoGl, ClearAcumulaODizESemTelaRecusa) {
  // MESMA MUDANCA DO TESTE ACIMA, e pelo mesmo motivo: o que faltava era a
  // superfice. O ESTADO continua a ser acumulado -- e o que este teste protege --
  // mas "feito" sem um pixel escrito seria o stub mudo (P2).
  Banco b;
  b.igl.Executar(kIgl_ClearColorx, Args(Fixo(1.0f), Fixo(1.0f), Fixo(1.0f), Fixo(1.0f)), nullptr);
  EXPECT_EQ(b.igl.Executar(kIgl_Clear, Args(GL_COLOR_BUFFER_BIT), nullptr), ResultadoGl::Recusado);
  EXPECT_EQ(b.igl.Limpezas(), 1u);
  EXPECT_EQ(b.igl.MascaraDeLimpeza(), GL_COLOR_BUFFER_BIT);
  EXPECT_EQ(b.igl.CorDeLimpeza(), 0xFFFFFFFFu);
  // Zero sem buffer conhecido recusa.
  EXPECT_EQ(b.igl.Executar(kIgl_Clear, Args(0), nullptr), ResultadoGl::Recusado);
}

TEST(RegistoGl, GetStringDescreveOMotorSemInventarOHardware) {
  Banco b;
  // Vendor/renderer/version sao definidos pela IMPLEMENTACAO. Eles descrevem o
  // Curupira, nao fingem a resposta de uma Adreno real. Extensoes vazias tambem
  // sao uma lista valida: esta arvore nao anuncia capacidade ausente.
  auto ler = [&](std::uint32_t p) {
    std::string texto;
    for (;; ++p) { const std::uint8_t c = b.mem.Ler8(p); if (c == 0) return texto; texto += char(c); }
  };
  std::uint32_t ret = 0;
  EXPECT_EQ(b.igl.Executar(kIgl_GetString, Args(GL_VENDOR), &ret), ResultadoGl::Feito);
  EXPECT_NE(ret, 0u);
  EXPECT_EQ(ler(ret), "Curupira");
  EXPECT_EQ(b.igl.Executar(kIgl_GetString, Args(GL_RENDERER), &ret), ResultadoGl::Feito);
  EXPECT_EQ(ler(ret), "Curupira software rasterizer");
  EXPECT_EQ(b.igl.Executar(kIgl_GetString, Args(GL_VERSION), &ret), ResultadoGl::Feito);
  EXPECT_EQ(ler(ret), "OpenGL ES-CM 1.0");
  EXPECT_EQ(b.igl.Executar(kIgl_GetString, Args(GL_EXTENSIONS), &ret), ResultadoGl::Feito);
  EXPECT_EQ(ler(ret), "");
}

TEST(RegistoGl, GetErrorDevolveZeroEDiLo) {
  Banco b;
  std::uint32_t ret = 0xDEAD;
  EXPECT_EQ(b.igl.Executar(kIgl_GetError, Args(), &ret), ResultadoGl::Feito);
  EXPECT_EQ(ret, GL_NO_ERROR);
  EXPECT_NE(b.igl.Ultimas().back().motivo.find("recusas"), std::string::npos);
}

TEST(RegistoGl, ParametroAcumuladoEObservavel) {
  Banco b;
  // `glFogx` nao tem consumidor (nao ha rasterizador), mas o valor NAO pode
  // desaparecer: fica guardado e o teste le-o.
  EXPECT_EQ(b.igl.Executar(kIgl_Fogx, Args(0x0B65u, Fixo(0.25f)), nullptr), ResultadoGl::Feito);
  const std::vector<std::uint32_t>* v = b.igl.Parametro(kIgl_Fogx, 0x0B65u);
  ASSERT_NE(v, nullptr);
  ASSERT_EQ(v->size(), 1u);
  EXPECT_EQ((*v)[0], Fixo(0.25f));
}

TEST(RegistoGl, PixelStoreiAceitaPackAlignmentMedidoNoDdragonz) {
  Banco b;
  EXPECT_EQ(b.igl.Executar(kIgl_PixelStorei, Args(GL_PACK_ALIGNMENT, 2), nullptr), ResultadoGl::Feito);
  const auto* valor = b.igl.Parametro(kIgl_PixelStorei, GL_PACK_ALIGNMENT);
  ASSERT_NE(valor, nullptr);
  ASSERT_EQ(valor->size(), 1u);
  EXPECT_EQ((*valor)[0], 2u);
}

TEST(RegistoGl, PixelStoreiMantemAlinhamentoValidoQuandoRecusaValor) {
  Banco b;
  EXPECT_EQ(b.igl.Executar(kIgl_PixelStorei, Args(GL_UNPACK_ALIGNMENT, 2), nullptr), ResultadoGl::Feito);
  EXPECT_EQ(b.igl.Executar(kIgl_PixelStorei, Args(GL_PACK_ALIGNMENT, 3), nullptr), ResultadoGl::Recusado);
  const auto* valor = b.igl.Parametro(kIgl_PixelStorei, GL_UNPACK_ALIGNMENT);
  ASSERT_NE(valor, nullptr);
  EXPECT_EQ((*valor)[0], 2u);
  EXPECT_EQ(b.igl.Executar(kIgl_PixelStorei, Args(0xdead, 2), nullptr), ResultadoGl::Recusado);
}

// `Tela` e top-down, mas a API GL e bottom-up. O primeiro `uint16_t` do
// destino tem de ser y, nao a linha que esta visualmente no topo.
TEST(RegistoGl, ReadPixelsRgb565ViraYEAlineiaCadaLinhaNoBufferDoGuest) {
  Banco b;
  Tela tela;
  b.igl.DefinirTela(&tela);
  constexpr int kX = 20;
  // GL y=7 e a linha Tela 472; GL y=8 e a linha Tela 471.
  const std::uint16_t de_baixo[5] = {0x1001u, 0x1002u, 0x1003u, 0x1004u, 0x1005u};
  const std::uint16_t de_cima[5] = {0x2001u, 0x2002u, 0x2003u, 0x2004u, 0x2005u};
  for (int i = 0; i < 5; ++i) {
    tela.CorAtual(de_baixo[i]);
    tela.Ponto(kX + i, 472);
    tela.CorAtual(de_cima[i]);
    tela.Ponto(kX + i, 471);
  }
  // 5 RGB565 = 10 bytes; PACK=8 pede passo 16, e nao os 12 da omissao PACK=4.
  ASSERT_EQ(b.igl.Executar(kIgl_PixelStorei, Args(GL_PACK_ALIGNMENT, 8), nullptr),
            ResultadoGl::Feito);
  constexpr std::uint32_t kDestino = 0x00031000u;
  for (std::uint32_t i = 0; i < 32; ++i) b.mem.Escrever8(kDestino + i, 0xA5u);
  ASSERT_EQ(PedirReadPixels(b, kX, 7, 5, 2, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, kDestino),
            ResultadoGl::Feito);
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(b.mem.Ler16(kDestino + static_cast<std::uint32_t>(i * 2)), de_baixo[i]);
    EXPECT_EQ(b.mem.Ler16(kDestino + 16u + static_cast<std::uint32_t>(i * 2)), de_cima[i]);
  }
  // O padding e separacao, nao e um sexto pixel que o GL possa escrever.
  for (std::uint32_t i = 10; i < 16; ++i) EXPECT_EQ(b.mem.Ler8(kDestino + i), 0xA5u);
}

TEST(RegistoGl, ReadPixelsRecortaNaTelaSemEncolherOBufferPedido) {
  Banco b;
  Tela tela;
  b.igl.DefinirTela(&tela);
  tela.CorAtual(0x7E0u);
  tela.Ponto(0, Tela::kAltura - 1);  // GL (0,0).
  constexpr std::uint32_t kDestino = 0x00032000u;
  for (std::uint32_t i = 0; i < 8; ++i) b.mem.Escrever8(kDestino + i, 0xA5u);
  // Rect GL [-1,-1]..[0,0]: a primeira linha fica toda fora; a segunda tem so
  // o pixel (0,0). Dois RGB565 ocupam exactamente o passo default de 4 bytes.
  ASSERT_EQ(PedirReadPixels(b, -1, -1, 2, 2, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, kDestino),
            ResultadoGl::Feito);
  EXPECT_EQ(b.mem.Ler16(kDestino + 0u), 0u);
  EXPECT_EQ(b.mem.Ler16(kDestino + 2u), 0u);
  EXPECT_EQ(b.mem.Ler16(kDestino + 4u), 0u);
  EXPECT_EQ(b.mem.Ler16(kDestino + 6u), 0x07E0u);
}

TEST(RegistoGl, ReadPixelsRecusaFormatoTipoEDimensaoNaoSuportadosSemEscrever) {
  Banco b;
  Tela tela;
  b.igl.DefinirTela(&tela);
  constexpr std::uint32_t kDestino = 0x00033000u;
  b.mem.Escrever16(kDestino, 0xA5A5u);
  EXPECT_EQ(PedirReadPixels(b, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_SHORT_5_6_5, kDestino),
            ResultadoGl::Recusado);
  EXPECT_EQ(b.mem.Ler16(kDestino), 0xA5A5u);
  EXPECT_EQ(PedirReadPixels(b, 0, 0, -1, 1, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, kDestino),
            ResultadoGl::Recusado);
  EXPECT_EQ(b.mem.Ler16(kDestino), 0xA5A5u);
  // A guarda e ANTES do laco: o pedido nao pode virar milhoes de escritas
  // pretas so por estar quase todo fora da Tela.
  EXPECT_EQ(PedirReadPixels(b, 0, 0, 1, Tela::kLargura * Tela::kAltura + 1, GL_RGB,
                            GL_UNSIGNED_SHORT_5_6_5, kDestino),
            ResultadoGl::Recusado);
  EXPECT_EQ(b.mem.Ler16(kDestino), 0xA5A5u);
}

TEST(RegistoGl, ALarguraDeLinhaDizOQueFazENaoPrometeOLinAlem) {
  Banco b;
  // O DEFEITO QUE ESTE TESTE APANHA (o padrao P2): o `kIgl_LineWidthx` guardava o
  // valor e respondia "feito", e nenhum rasterizador o lia -- o pedido ficava
  // guardado num mapa e NAO era aplicado, sem o dizer. O valor continua a ficar
  // guardado (o pedido e observavel), e a mensagem passa a dizer AS DUAS METADES:
  // <= 1 desenha um pixel, > 1 recusa o desenho.
  EXPECT_EQ(b.igl.Executar(kIgl_LineWidthx, Args(Fixo(2.0f)), nullptr), ResultadoGl::Feito);
  const std::vector<std::uint32_t>* v = b.igl.Parametro(kIgl_LineWidthx, 0);
  ASSERT_NE(v, nullptr);
  ASSERT_EQ(v->size(), 1u);
  EXPECT_EQ((*v)[0], Fixo(2.0f));
  const std::string motivo = b.igl.Ultimas().back().motivo;
  EXPECT_NE(motivo.find("aplicada"), std::string::npos) << motivo;
  EXPECT_NE(motivo.find("RECUSA"), std::string::npos) << motivo;
  // A medicao que a mensagem tem de reflectir: o rasterizador so tem linha de um
  // pixel (ver `RasterizarSegmento` e o teste
  // `Rasterizador.LarguraDeLinhaMaiorQueUmRecusaComONome`).
  EXPECT_NE(motivo.find("linha grossa"), std::string::npos) << motivo;
  // E A LARGURA NAO POSITIVA CONTINUA A RECUSAR NO PROPRIO SLOT.
  EXPECT_EQ(b.igl.Executar(kIgl_LineWidthx, Args(0u), nullptr), ResultadoGl::Recusado);
}

TEST(RegistoGl, QueryInterfaceDevolveOProprioObjeto) {
  Banco b;
  b.igl.Instalar(b.saidas);
  const std::uint32_t ppo = 0x00070000u;
  std::uint32_t ret = 0xDEAD;
  EXPECT_EQ(b.igl.Executar(kIgl_QueryInterface, Args(b.igl.Objeto(), kClsidIgl, ppo), &ret),
            ResultadoGl::Feito);
  EXPECT_EQ(b.mem.Ler32(ppo), b.igl.Objeto());
  EXPECT_EQ(ret, 0u);
  // Um IID que este objecto nao serve: ponteiro a zero e ECLASSNOTSUPPORT.
  EXPECT_EQ(b.igl.Executar(kIgl_QueryInterface, Args(b.igl.Objeto(), 0x01001001u, ppo), &ret),
            ResultadoGl::Recusado);
  EXPECT_EQ(b.mem.Ler32(ppo), 0u);
  EXPECT_EQ(ret, 0xE0000001u);
}

TEST(RegistoGl, AddRefEReleaseMexemNaContagem) {
  Banco b;
  b.igl.Instalar(b.saidas);
  std::uint32_t ret = 0;
  // O `ConstruirObjeto` escreveu 1 no campo da contagem.
  EXPECT_EQ(b.igl.Executar(kIgl_AddRef, Args(b.igl.Objeto()), &ret), ResultadoGl::Feito);
  EXPECT_EQ(ret, 2u);
  EXPECT_EQ(b.igl.Executar(kIgl_Release, Args(b.igl.Objeto()), &ret), ResultadoGl::Feito);
  EXPECT_EQ(ret, 1u);
}

// ---------------------------------------------------------------------------
// 5. A FRENTE gloe: `GL_OES_draw_texture` + `glDrawTexivOES` JUNTOS
// ---------------------------------------------------------------------------
//
// O que estes testes provam e a REGRA MEDIDA do relatorio 12-gl.md: anunciar a
// extensao SEM servir o `glDrawTexivOES` da 10 regressoes (pixels 307384 -> 0)
// nos dez titulos `emulator_neo`. O anuncio (a string de extensoes do
// `IGLES11::GetString`) e o servico (o blit dos `IGLES11Ext::DrawTex*OES`)
// habitam os DOIS em `core/brew/classes.cpp` -- o `Igl` (`core/brew/igl.cpp`)
// nao participa no caminho dos dez titulos (medido: zero faltas IGL no corpus
// inteiro). Estes testes correm `AtenderClasse` com uma CPU real, como os de
// `classes_test.cpp`, para exigirem o MESMO caminho que o guest usa.

// O banco das classes: objectos construidos + CPU real + saidas, porque e a
// cablagem que o `despacho` usa (o `Despacho::Correr` chama `AtenderClasse`).
struct BancoClasses {
  Tempo tempo;
  Traco traco;
  DestinoMemoria destino;
  Memoria mem;
  Saidas saidas;
  ArmInterpreter cpu;

  BancoClasses()
      : traco("teste-da-frente-gloe", &tempo), mem(&traco), cpu(mem, &traco) {
    traco.JuntarDestino(&destino);
    saidas.base = 0xF0000000u;
    saidas.passo = 4;
    saidas.quantos = 100000;
    saidas.ativa = true;
    cpu.ConfigurarSaidas(saidas);
    ConstruirClasses(mem, saidas, traco);
  }

  std::size_t Faltas(const std::string& nome) const {
    const auto& f = traco.ContagemFaltas();
    const auto it = f.find(nome);
    return it == f.end() ? 0 : static_cast<std::size_t>(it->second);
  }
  std::uint16_t Pixel(int x, int y) const {
    return static_cast<std::uint16_t>(mem.Ler16(
        zb2::brew::kBaseDoEcraNoGuest + static_cast<std::uint32_t>(y * 640 + x) * 2u));
  }
  // O detalhe da ULTIMA falta com este nome (traco.cpp:122 prefixa
  // `NAO_IMPLEMENTADO: `). E o que a lista de demanda mostra.
  std::string Detalhe(const std::string& nome) const {
    for (const auto& ev : destino.eventos) {
      if (ev.nome == "NAO_IMPLEMENTADO: " + nome) return ev.detalhe;
    }
    return "";
  }
};

TEST(FrenteGloe, OAnuncioEServidoNoMesmoCommitDoBlit) {
  // A REGRA em forma de teste: a lista de extensoes contem `GL_OES_draw_texture`
  // (o blit serve-o, nos testes abaixo) e NAO contem os nomes que o zeebx
  // anuncia e esta arvore nao serve (`GL_ATI_*`, `GL_ARB_vertex_buffer_object`):
  // anunciar o que nao existe faz o titulo saltar para uma funcao que nao esta
  // la (o caso MEDIDO em 12-gl.md C.4).
  BancoClasses b;
  const std::uint32_t pret = 0x80100000u;
  b.mem.Escrever32(pret, 0xdeadbeefu);
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, gl_slots::GL_EXTENSIONS);
  b.cpu.Set(kR2, pret);
  EXPECT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_GetString, b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), kAeeSuccess);
  const std::uint32_t p = b.mem.Ler32(pret);
  ASSERT_NE(p, 0u) << "NUNCA NULO: ha um strstr medido sobre este resultado";
  std::string s;
  b.mem.LerCadeia(p, &s, 0x100);
  EXPECT_NE(s.find("GL_OES_draw_texture"), std::string::npos) << s;
  EXPECT_EQ(s.find("GL_ATI_"), std::string::npos) << "atitc/imageon nao sao servidos: " << s;
  EXPECT_EQ(s.find("GL_ARB_"), std::string::npos) << "vertex_buffer_object nao e servido: " << s;
}

TEST(FrenteGloe, OEstadoDeTexturaMinimoEServidoComSucesso) {
  // O blit desenha a textura LIGADA; o titulo liga-a com `glGenTextures`,
  // `glBindTexture` e `glTexImage2D` no IGLES11, e os tres tem de responder
  // antes de qualquer rect. (VERMELHO antes da frente: recusavam todos.)
  BancoClasses b;
  const std::uint32_t texels = 0x0002C000u;
  const std::uint32_t pilha = 0x0002D000u;
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, 1u);            // n = 1
  b.cpu.Set(kR2, 0x0002B000u);   // lista de ids
  EXPECT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_GenTextures, b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), kAeeSuccess);
  EXPECT_NE(b.mem.Ler32(0x0002B000u), 0u);

  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, gl_slots::GL_TEXTURE_2D);
  b.cpu.Set(kR2, b.mem.Ler32(0x0002B000u));
  EXPECT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_BindTexture, b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), kAeeSuccess);

  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, gl_slots::GL_TEXTURE_2D);
  b.cpu.Set(kR2, 0u);                                   // level
  b.cpu.Set(kR3, gl_slots::GL_RGBA);                    // internalformat
  b.cpu.Set(kSP, pilha);
  b.mem.Escrever32(pilha + 0u, 2u);                     // width
  b.mem.Escrever32(pilha + 4u, 2u);                     // height
  b.mem.Escrever32(pilha + 8u, 0u);                     // border
  b.mem.Escrever32(pilha + 12u, gl_slots::GL_RGBA);     // format (o 6.o arg)
  b.mem.Escrever32(pilha + 16u, gl_slots::GL_UNSIGNED_BYTE);  // type
  b.mem.Escrever32(pilha + 20u, texels);                // pixels
  EXPECT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_TexImage2D, b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), kAeeSuccess);
}

TEST(FrenteGloe, OBlitEscreveOPedacoDaTexturaNoEcraDoGuest) {
  // Uma textura 2x2 RGBA com quatro texels de cores distintas, desenhada num
  // rect 4x4 em coordenadas de JANELA (8, 8). O rect ocupa as linhas 468..471 e
  // as colunas 8..11 do ecra do guest, e cada canto mostra o texel certo:
  //
  //   linha 468 (topo do rect, v=1)  -> fila 1 dos dados: azul | branco
  //   linha 471 (fundo do rect, v=0) -> fila 0 dos dados: vermelho | verde
  //
  // (as colunas 8..11 mapeiam u 0..1: azul/vermelho em 8,9; branco/verde em 10,11)
  BancoClasses b;
  const std::uint32_t texels = 0x0002C000u;
  const std::uint32_t pilha = 0x0002D000u;
  const std::uint32_t coords = 0x0002E000u;
  // fila 0 dos dados: vermelho, verde  | fila 1: azul, branco (RGBA8, LE)
  b.mem.Escrever32(texels + 0u, 0xFF0000FFu);
  b.mem.Escrever32(texels + 4u, 0xFF00FF00u);
  b.mem.Escrever32(texels + 8u, 0xFFFF0000u);
  b.mem.Escrever32(texels + 12u, 0xFFFFFFFFu);

  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, gl_slots::GL_TEXTURE_2D);
  b.cpu.Set(kR2, 7u);
  EXPECT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_BindTexture, b.traco));
  ASSERT_EQ(b.cpu.Get(kR0), kAeeSuccess);

  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, gl_slots::GL_TEXTURE_2D);
  b.cpu.Set(kR2, 0u);
  b.cpu.Set(kR3, gl_slots::GL_RGBA);
  b.cpu.Set(kSP, pilha);
  b.mem.Escrever32(pilha + 0u, 2u);
  b.mem.Escrever32(pilha + 4u, 2u);
  b.mem.Escrever32(pilha + 8u, 0u);
  b.mem.Escrever32(pilha + 12u, gl_slots::GL_RGBA);
  b.mem.Escrever32(pilha + 16u, gl_slots::GL_UNSIGNED_BYTE);
  b.mem.Escrever32(pilha + 20u, texels);
  EXPECT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_TexImage2D, b.traco));
  ASSERT_EQ(b.cpu.Get(kR0), kAeeSuccess);

  // `glDrawTexivOES(pMe, const GLint *coords)` -- coords = x, y, z, w, h.
  b.mem.Escrever32(coords + 0u, 8u);
  b.mem.Escrever32(coords + 4u, 8u);
  b.mem.Escrever32(coords + 8u, 0u);
  b.mem.Escrever32(coords + 12u, 4u);
  b.mem.Escrever32(coords + 16u, 4u);
  b.cpu.Set(kR0, kObjetoIglesExt);
  b.cpu.Set(kR1, coords);
  EXPECT_TRUE(
      AtenderClasse(b.cpu, kVtableIglesExt + igles_ext_slots::kIglesExt_DrawTexivOES, b.traco));
  ASSERT_EQ(b.cpu.Get(kR0), kAeeSuccess) << "o rect recusou; ver a falta no traco";

  // Os quatro cantos do rect, em RGB565: vermelho 0xF800, verde 0x07E0,
  // azul 0x001F, branco 0xFFFF.
  EXPECT_EQ(b.Pixel(8, 468), 0x001Fu);    // topo esquerda -> azul
  EXPECT_EQ(b.Pixel(10, 468), 0xFFFFu);   // topo direita -> branco
  EXPECT_EQ(b.Pixel(8, 471), 0xF800u);    // fundo esquerda -> vermelho
  EXPECT_EQ(b.Pixel(10, 471), 0x07E0u);   // fundo direita -> verde
  // E o rect NAO vazou fora do sitio: um pixel a 20px de distancia continua
  // por escrever.
  EXPECT_EQ(b.Pixel(30, 30), 0u);
}

TEST(FrenteGloe, ALarguraNegativaEspelhaOEixo) {
  // O sinal da largura espelha: com w = -4 o rect passa a ocupar as colunas
  // 4..7, e o texel da DIREITA da textura (branco) fica no lado ESQUERDO do
  // rect. MEDIDO no zeebx (`rasterizer.rs::draw_texture`): "largura ou altura
  // negativa ali espelha o eixo -- e assim que a extensao vira a imagem".
  BancoClasses b;
  const std::uint32_t texels = 0x0002C000u;
  const std::uint32_t pilha = 0x0002D000u;
  const std::uint32_t coords = 0x0002E000u;
  b.mem.Escrever32(texels + 0u, 0xFF0000FFu);
  b.mem.Escrever32(texels + 4u, 0xFF00FF00u);
  b.mem.Escrever32(texels + 8u, 0xFFFF0000u);
  b.mem.Escrever32(texels + 12u, 0xFFFFFFFFu);
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, gl_slots::GL_TEXTURE_2D);
  b.cpu.Set(kR2, 7u);
  AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_BindTexture, b.traco);
  ASSERT_EQ(b.cpu.Get(kR0), kAeeSuccess);
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, gl_slots::GL_TEXTURE_2D);
  b.cpu.Set(kR2, 0u);
  b.cpu.Set(kR3, gl_slots::GL_RGBA);
  b.cpu.Set(kSP, pilha);
  b.mem.Escrever32(pilha + 0u, 2u);
  b.mem.Escrever32(pilha + 4u, 2u);
  b.mem.Escrever32(pilha + 8u, 0u);
  b.mem.Escrever32(pilha + 12u, gl_slots::GL_RGBA);
  b.mem.Escrever32(pilha + 16u, gl_slots::GL_UNSIGNED_BYTE);
  b.mem.Escrever32(pilha + 20u, texels);
  AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_TexImage2D, b.traco);
  ASSERT_EQ(b.cpu.Get(kR0), kAeeSuccess);
  // x=8, w=-4 -> rect nas colunas 4..7 (8 nao pertence ao rect).
  b.mem.Escrever32(coords + 0u, 8u);
  b.mem.Escrever32(coords + 4u, 8u);
  b.mem.Escrever32(coords + 8u, 0u);
  b.mem.Escrever32(coords + 12u, static_cast<std::uint32_t>(-4));
  b.mem.Escrever32(coords + 16u, 4u);
  b.cpu.Set(kR0, kObjetoIglesExt);
  b.cpu.Set(kR1, coords);
  EXPECT_TRUE(
      AtenderClasse(b.cpu, kVtableIglesExt + igles_ext_slots::kIglesExt_DrawTexivOES, b.traco));
  ASSERT_EQ(b.cpu.Get(kR0), kAeeSuccess);
  // Topo do rect (linha 468, fila 1): o lado esquerdo do rect mostra o texel da
  // DIREITA da textura (branco), e o direito mostra o azul.
  EXPECT_EQ(b.Pixel(4, 468), 0xFFFFu);
  EXPECT_EQ(b.Pixel(7, 468), 0x001Fu);
  // Fora do rect (coluna 8) nada foi escrito por este rect.
  EXPECT_EQ(b.Pixel(8, 468), 0u);
}

TEST(FrenteGloe, OBlitSemTexturaRecusaComNome) {
  // Sem `glBindTexture` o blit NAO pode desenhar: "devolver sucesso e nao
  // desenhar" e o defeito do stub do `glCullFace`. A recusa tem de se ler.
  BancoClasses b;
  const std::uint32_t coords = 0x0002E000u;
  b.mem.Escrever32(coords + 0u, 0u);
  b.mem.Escrever32(coords + 4u, 0u);
  b.mem.Escrever32(coords + 8u, 0u);
  b.mem.Escrever32(coords + 12u, 4u);
  b.mem.Escrever32(coords + 16u, 4u);
  b.cpu.Set(kR0, kObjetoIglesExt);
  b.cpu.Set(kR1, coords);
  EXPECT_TRUE(
      AtenderClasse(b.cpu, kVtableIglesExt + igles_ext_slots::kIglesExt_DrawTexivOES, b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), kAeeUnsupported);
  EXPECT_EQ(b.Faltas("IGLES11Ext::DrawTexivOES"), 1u);
  EXPECT_EQ(b.Pixel(10, 10), 0u);
}

TEST(FrenteGloe, ACabecaDoIglesExtViveNoObjecto) {
  // O wrapper (`GLES_ext.c`) chama `IGLES11EXT_QueryInterface` para se servir a
  // si proprio e `IGLES11EXT_Release` no fim (`ReleaseNBI`): a contagem tem de
  // viver no objecto, como nas outras interfaces.
  BancoClasses b;
  const std::uint32_t ppo = 0x80100010u;
  b.cpu.Set(kR0, kObjetoIglesExt);
  b.cpu.Set(kR1, kIidGles11Ext);
  b.cpu.Set(kR2, ppo);
  EXPECT_TRUE(AtenderClasse(b.cpu, kVtableIglesExt + igles_ext_slots::kIglesExt_QueryInterface,
                            b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), kAeeSuccess);
  EXPECT_EQ(b.mem.Ler32(ppo), kObjetoIglesExt);

  b.cpu.Set(kR0, kObjetoIglesExt);
  EXPECT_TRUE(AtenderClasse(b.cpu, kVtableIglesExt + igles_ext_slots::kIglesExt_AddRef, b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), 2u);
  EXPECT_TRUE(AtenderClasse(b.cpu, kVtableIglesExt + igles_ext_slots::kIglesExt_Release,
                            b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), 1u);
}

// ---------------------------------------------------------------------------
// 5. A FRENTE GLBLOCO: OS DOZE SLOTS NOMEADOS DO IGLES11
// ---------------------------------------------------------------------------
//
// OITO titulos (`abd`, `gof`, `pacmania`, `pbc`, `ridgeracer`, `rmp`,
// `tekken2`, `torkandkral`) pedem hoje estes metodos do IGLES11 e recebem
// recusa com nome. A demanda medida (bateria, ZB2_QUADROS=300 ZB2_EVT_START=1):
//
//   Disable 8 | MatrixMode 8 | Viewport 8 | LoadIdentity 7 | Hint 6 |
//   ShadeModel 6 | CullFace 5 | EnableClientState 5 | TexParameterx 4 |
//   Clear 3 | ClearColorx 3
//
// O teste VERMELHO desta frente: cada slot tem de responder SERVINDO ESTADO
// (r0 = AEE_SUCCESS e nenhuma falta `IGLES11::<nome>`), em vez da recusa
// generica. E o teste NAO chama `ChamaSaida`: le a TABELA pelo indice
// `kVtableIgles + slot`, a mesma leitura do despacho.
//
// O CAMINHO E O DO IGL DE 80 SLOTS, e a razao esta medida: o `core/brew/igl.cpp`
// ja implementa estes DOZE metodos com o estado exacto que o rasterizador
// consome no desenho (`MontarEstado` -> `EstadoDeRasterizacao`). Copiar esse
// estado para um segundo sitio (rasterizador, classes) criaria DUAS verdades
// paralelas -- a armadilha 2 desta casa ("segundas copias de kSlotId"). O
// IGLES11 passa a ter o SEU proprio motor do mesmo tipo, construido em
// `ConstruirIgles`, e o `AtenderClasse` desloca os argumentos pelo `po` (o
// IGLES11 leva `iname *pMe` em r0; o IGL de 80 slots nao leva nada) antes de
// chamar `Igl::Executar`. So o `Clear` fica a dever a ESCRITA: sem superficie
// ligada a este objecto, a limpeza e uma recusa NOMEADA com o motivo, e nao um
// "sucesso" sem pixel (a regra dos 86 377 glCullFace).

constexpr std::uint32_t kPilhaDoTeste = 0x0002D000u;  // sp[0] = o 4.o argumento real

struct PedidoIgles {
  std::uint32_t slot;
  const char* nome;
  std::uint32_t r1, r2, r3;
  std::uint32_t sp0;  // usado so quando `quatro` e true
  bool quatro;
};

TEST(FrenteGlbloco, OsOnzeSlotsDeEstadoRespondemComSucessoESemFalta) {
  const PedidoIgles pedidos[] = {
      // slot IGLES11, nome, r1, r2, r3, sp[0], usa-sp
      {igles_slots::kIgles_Enable, "Enable", GL_CULL_FACE, 0, 0, 0, false},
      {igles_slots::kIgles_Disable, "Disable", GL_CULL_FACE, 0, 0, 0, false},
      {igles_slots::kIgles_MatrixMode, "MatrixMode", GL_MODELVIEW, 0, 0, 0, false},
      {igles_slots::kIgles_LoadIdentity, "LoadIdentity", 0, 0, 0, 0, false},
      {igles_slots::kIgles_Viewport, "Viewport", 0, 0, 640, 480, true},
      // `GL_PERSPECTIVE_CORRECTION_HINT` e 0x0C02 (gles/gl.h nao o gera neste
      // `.inc`); o `Hint` acumula o par (alvo, modo) sem o interpretar.
      {igles_slots::kIgles_Hint, "Hint", 0x0C02u, GL_FASTEST, 0, 0, false},
      {igles_slots::kIgles_ShadeModel, "ShadeModel", GL_SMOOTH, 0, 0, 0, false},
      {igles_slots::kIgles_CullFace, "CullFace", GL_BACK, 0, 0, 0, false},
      {igles_slots::kIgles_EnableClientState, "EnableClientState", GL_VERTEX_ARRAY, 0, 0, 0,
       false},
      {igles_slots::kIgles_DisableClientState, "DisableClientState", GL_VERTEX_ARRAY, 0, 0, 0,
       false},
      {igles_slots::kIgles_TexParameterx, "TexParameterx", GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
       GL_NEAREST, 0, false},
      // (1, 0, 0, 1) em GLfixed 16.16: o quarto argumento vai NA PILHA.
      {igles_slots::kIgles_ClearColorx, "ClearColorx", 0x00010000u, 0, 0, 0x00010000u, true},
  };
  BancoClasses b;
  for (const auto& p : pedidos) {
    SCOPED_TRACE(p.nome);
    b.mem.Escrever32(kPilhaDoTeste, p.sp0);
    b.cpu.Set(kR0, kObjetoIgles);
    b.cpu.Set(kR1, p.r1);
    b.cpu.Set(kR2, p.r2);
    b.cpu.Set(kR3, p.r3);
    b.cpu.Set(kSP, kPilhaDoTeste);
    EXPECT_TRUE(AtenderClasse(b.cpu, kVtableIgles + p.slot, b.traco));
    EXPECT_EQ(b.cpu.Get(kR0), kAeeSuccess) << p.nome;
    EXPECT_EQ(b.Faltas(std::string("IGLES11::") + p.nome), 0u) << p.nome;
  }
}

TEST(FrenteGlbloco, OEstadoServeSeNoMotorDoIglEObservavel) {
  // A ARMADILHA 3 desta casa: um teste que so ve "nao ha falta" nao prova que
  // o estado MUDOU. Este le o estado do motor (o mesmo `Igl` que o
  // rasterizador consome no desenho) apos CADA servico.
  BancoClasses b;
  ASSERT_NE(EstadoDoIgles11(), nullptr);

  // Enable/Disable -> o interruptor de capacidade.
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, GL_CULL_FACE);
  AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_Enable, b.traco);
  EXPECT_TRUE(EstadoDoIgles11()->InterruptorLigado(GL_CULL_FACE));
  AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_Disable, b.traco);
  EXPECT_FALSE(EstadoDoIgles11()->InterruptorLigado(GL_CULL_FACE));

  // MatrixMode + LoadIdentity -> a matriz da MODE corrente e a identidade.
  b.cpu.Set(kR1, GL_PROJECTION);
  AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_MatrixMode, b.traco);
  EXPECT_EQ(EstadoDoIgles11()->ModoDeMatriz(), kModoProjection);
  AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_LoadIdentity, b.traco);
  const float* m = EstadoDoIgles11()->MatrizCorrente();
  for (int k = 0; k < 4; ++k) EXPECT_FLOAT_EQ(m[k * 4 + k], 1.0f);

  // Viewport, com o quarto argumento NA PILHA (o deslocamento do `pMe`).
  b.mem.Escrever32(kPilhaDoTeste, 240u);
  b.cpu.Set(kR1, 10u);
  b.cpu.Set(kR2, 20u);
  b.cpu.Set(kR3, 320u);
  b.cpu.Set(kSP, kPilhaDoTeste);
  AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_Viewport, b.traco);
  EXPECT_EQ(EstadoDoIgles11()->Viewport(0), 10u);
  EXPECT_EQ(EstadoDoIgles11()->Viewport(1), 20u);
  EXPECT_EQ(EstadoDoIgles11()->Viewport(2), 320u);
  EXPECT_EQ(EstadoDoIgles11()->Viewport(3), 240u);

  // CullFace, ShadeModel, EnableClientState.
  b.cpu.Set(kR1, GL_BACK);
  AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_CullFace, b.traco);
  EXPECT_EQ(EstadoDoIgles11()->CullFace(), GL_BACK);
  b.cpu.Set(kR1, GL_FLAT);
  AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_ShadeModel, b.traco);
  EXPECT_EQ(EstadoDoIgles11()->ShadeModel(), GL_FLAT);
  b.cpu.Set(kR1, GL_VERTEX_ARRAY);
  AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_EnableClientState, b.traco);
  EXPECT_TRUE(EstadoDoIgles11()->ArrayDeClienteLigado(GL_VERTEX_ARRAY));

  // Hint: acumula o par (alvo, modo).
  b.cpu.Set(kR1, 0x0C02u);
  b.cpu.Set(kR2, GL_FASTEST);
  AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_Hint, b.traco);
  const auto* hint = EstadoDoIgles11()->Parametro(kIgl_Hint, 0u);
  ASSERT_NE(hint, nullptr);
  ASSERT_EQ(hint->size(), 4u);
  EXPECT_EQ((*hint)[1], GL_FASTEST);

  // TexParameterx: o valor fica no parametro do slot IGL correspondente.
  b.cpu.Set(kR1, GL_TEXTURE_2D);
  b.cpu.Set(kR2, GL_TEXTURE_MIN_FILTER);
  b.cpu.Set(kR3, GL_NEAREST);
  AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_TexParameterx, b.traco);
  const auto* p = EstadoDoIgles11()->Parametro(kIgl_TexParameterx, GL_TEXTURE_MIN_FILTER);
  ASSERT_NE(p, nullptr);
  ASSERT_EQ(p->size(), 1u);
  EXPECT_EQ((*p)[0], GL_NEAREST);

  // ClearColorx: RGBA8 com o byte 0 a ser o VERMELHO (o mesmo desempacotamento
  // do `rasterizador.h`); o quarto argumento vem da pilha.
  b.mem.Escrever32(kPilhaDoTeste, 0x00010000u);  // alpha = 1.0
  b.cpu.Set(kR1, 0x00010000u);                    // vermelho = 1.0
  b.cpu.Set(kR2, 0u);
  b.cpu.Set(kR3, 0u);
  b.cpu.Set(kSP, kPilhaDoTeste);
  AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_ClearColorx, b.traco);
  EXPECT_EQ(EstadoDoIgles11()->CorDeLimpeza(), 0xFF0000FFu);
}

TEST(FrenteGlbloco, OEstadoNaoVazaEntreCorridas) {
  // O estado vive num motor RECONSTRUIDO por `ConstruirIgles` (uma vez por
  // titulo na bateria): a segunda Bancada nao pode ver o que a primeira
  // ligou. Sem isto, o segundo titulo da bateria veria o GL do primeiro.
  BancoClasses b1;
  b1.cpu.Set(kR0, kObjetoIgles);
  b1.cpu.Set(kR1, GL_CULL_FACE);
  AtenderClasse(b1.cpu, kVtableIgles + igles_slots::kIgles_Enable, b1.traco);
  EXPECT_TRUE(EstadoDoIgles11()->InterruptorLigado(GL_CULL_FACE));
  {
    BancoClasses b2;
    ASSERT_NE(EstadoDoIgles11(), nullptr);
    EXPECT_FALSE(EstadoDoIgles11()->InterruptorLigado(GL_CULL_FACE));
    EXPECT_EQ(EstadoDoIgles11()->Chamadas(), 0u);
  }
}

TEST(FrenteGlbloco, OClearAcumulaAMascaraERecusaAEscritaSemSuperficie) {
  // O `glClear` nao pode "dar certo" aqui: o objecto IGLES11 nao tem
  // superficie ligada neste incremento, e a regra desta casa e RECUSAR a
  // limpeza com o motivo, nunca responder sucesso sem escrever um pixel. O que
  // a frente exige dele e o ESTADO: a mascara acumulada, e o detalhe da recusa
  // a dizer que e a TELA que falta (e nao "slot sem implementacao").
  BancoClasses b;
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, GL_COLOR_BUFFER_BIT);
  EXPECT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_Clear, b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), kAeeUnsupported);
  EXPECT_EQ(b.Faltas("IGLES11::Clear"), 1u);
  EXPECT_NE(b.Detalhe("IGLES11::Clear").find("TELA LIGADA"), std::string::npos)
      << b.Detalhe("IGLES11::Clear");
}

// ---------------------------------------------------------------------------
// 6. A FRENTE tela: A TELA DO MOTOR GL
// ---------------------------------------------------------------------------
//
// O IGLES11 tem um motor do MESMO tipo do IGL de 30000 (`Igl`), reconstruido
// por `ConstruirIgles` (uma vez por corrida). O IGL de 30000 recebe a Tela na
// assembleia (`Despacho::InstalarGl`); o do IGLES11 ficou SEM ela -- o
// `glClear` desse objecto recusava a ESCRITA ("o IGL NAO TEM TELA LIGADA",
// medido no ridgeracer: IGLES11::Clear 1x, pixels=0). O teste abaixo prova o
// CONTRATO do motor (definir a Tela -> a limpeza escreve); o teste da
// assembleia (que e quem liga o fio) vive em `entrada_despacho_test.cpp`.

TEST(FrenteTela, OClearDoIgles11EscreveNaTelaDepoisDeDefinirTela) {
  // O MESMO ACTO do despacho (`Igl::DefinirTela` sobre o motor do IGLES11)
  // faz o `Clear` desse objecto escrever 640x480 pixels na Tela, com sucesso e
  // sem falta -- em vez de acumular a mascara e recusar (o teste
  // `OClearAcumula...` fixa o outro lado do mesmo cabo).
  BancoClasses b;
  Tela tela;
  ASSERT_NE(EstadoDoIgles11(), nullptr);
  const_cast<Igl*>(EstadoDoIgles11())->DefinirTela(&tela);
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, GL_COLOR_BUFFER_BIT);
  EXPECT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_Clear, b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), kAeeSuccess);
  EXPECT_EQ(b.Faltas("IGLES11::Clear"), 0u);
  EXPECT_EQ(tela.Escritos(), static_cast<std::uint32_t>(Tela::kLargura * Tela::kAltura));
  EXPECT_EQ(tela.CoresDistintas(), 1u);
}


// ---------------------------------------------------------------------------
// 6. A FRENTE IGL2: OS `fv` DO IGLES11, O `Orthof` E AS CAPACIDADES SEM NOME
// ---------------------------------------------------------------------------
//
// O QUE FALTAVA, e isto e a corrida de referencia lida titulo a titulo
// (`/tmp/corrida_egl2.json`, `ZB2_QUADROS=300 ZB2_EVT_START=1`, 62 titulos):
//
//   IGLES11::Lightfv     6x em gof, pbc e rmp (light=GL_LIGHT0)
//   IGLES11::Materialfv  8x em gof, pbc e rmp (face=GL_FRONT_AND_BACK)
//   IGLES11::Orthof      1x em abd, peggle e torkandkral
//   glEnable 0x803A      1x em gof, rmp e pbc  -> GL_RESCALE_NORMAL
//   glDisable 0x0B57     1x em tekken2         -> GL_COLOR_MATERIAL
//
// Os dois ultimos sao as UNICAS capacidades sem nome de todo o corpus, e o
// segundo CORRIGE o relatorio da frente glbloco, que o chamou GL_STENCIL_TEST
// (`GL_STENCIL_TEST` vale 0x0B90, `gles_1_0/gl.h:165`; 0x0B57 e
// `GL_COLOR_MATERIAL`, `:178`).
//
// COMO ESTES TESTES ENTRAM: pela TABELA (`kVtableIgles + slot`), e nao por
// `ChamaSaida`. Um teste que chamasse o metodo interno diria que a funcao serve
// e nao diria que a chamada CHEGA -- a armadilha 3 desta casa. E todos conferem
// o ESTADO, porque "nao ha falta" tambem e o que se le de uma recusa nomeada.

// Os valores que o `.inc` gerado NAO tem, com a linha do cabecalho do SDK (o
// bloco que os justifica esta em `core/brew/igl.cpp`):
//   gles_1_0/gl.h:180 GL_RESCALE_NORMAL 0x803A | :178 GL_COLOR_MATERIAL 0x0B57
//   :259 GL_AMBIENT 0x1200 | :260 GL_DIFFUSE 0x1201 | :261 GL_SPECULAR 0x1202
//   :301 GL_SHININESS 0x1601 | :302 GL_AMBIENT_AND_DIFFUSE 0x1602
//   :461 GL_LIGHT0 0x4000
constexpr std::uint32_t kGlRescaleNormal = 0x803Au;
constexpr std::uint32_t kGlColorMaterial = 0x0B57u;
constexpr std::uint32_t kGlAmbient = 0x1200u;
constexpr std::uint32_t kGlDiffuse = 0x1201u;
constexpr std::uint32_t kGlShininess = 0x1601u;
constexpr std::uint32_t kGlLight0 = 0x4000u;

// `AEEGLfloat` (32 bits): os bits da palavra SAO o `float`.
std::uint32_t Real(float v) {
  std::uint32_t u = 0;
  std::memcpy(&u, &v, sizeof(u));
  return u;
}

TEST(FrenteIgl2, OsTresFvDoIgles11RespondemPelaTabelaEChegamAoMotor) {
  BancoClasses b;
  ASSERT_NE(EstadoDoIgles11(), nullptr);
  const std::uint32_t vector = 0x0002E100u;
  for (int k = 0; k < 4; ++k) b.mem.Escrever32(vector + 4u * k, Real(0.25f * (k + 1)));

  // Lightfv(GL_LIGHT0, GL_AMBIENT, vector) -- tres argumentos reais em r1..r3.
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, kGlLight0);
  b.cpu.Set(kR2, kGlAmbient);
  b.cpu.Set(kR3, vector);
  EXPECT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_Lightfv, b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), kAeeSuccess);
  EXPECT_EQ(b.Faltas("IGLES11::Lightfv"), 0u);
  const auto* luz = EstadoDoIgles11()->ParametroDeLuz(kGlLight0, kGlAmbient);
  ASSERT_NE(luz, nullptr);
  ASSERT_EQ(luz->size(), 4u);
  EXPECT_FLOAT_EQ((*luz)[0], 0.25f);
  EXPECT_FLOAT_EQ((*luz)[3], 1.0f);

  // Materialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, vector): quatro componentes.
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, GL_FRONT_AND_BACK);
  b.cpu.Set(kR2, kGlDiffuse);
  b.cpu.Set(kR3, vector);
  EXPECT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_Materialfv, b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), kAeeSuccess);
  EXPECT_EQ(b.Faltas("IGLES11::Materialfv"), 0u);
  const auto* material =
      EstadoDoIgles11()->ParametroDeMaterial(GL_FRONT_AND_BACK, kGlDiffuse);
  ASSERT_NE(material, nullptr);
  EXPECT_EQ(material->size(), 4u);
  EXPECT_FLOAT_EQ((*material)[1], 0.5f);
}

TEST(FrenteIgl2, OShininessLeUMValorENaoQuatro) {
  // O TANTO DE COMPONENTES VEM DO `pname`: o `GL_SHININESS` e UM escalar, e o
  // gof/pbc pedem-no com o escalar em 0x8007fefc-0x8007feec. Ler sempre quatro
  // valores (o que o `kIgl_Lightxv` faz hoje) lia 12 bytes que o titulo nunca
  // escreveu. O teste poe um valor no primeiro lugar e um VENENO nos tres
  // seguintes: se o motor lesse quatro, o veneno aparecia no estado.
  BancoClasses b;
  ASSERT_NE(EstadoDoIgles11(), nullptr);
  const std::uint32_t escalar = 0x0002E200u;
  b.mem.Escrever32(escalar, Real(12.0f));
  for (int k = 1; k < 4; ++k) b.mem.Escrever32(escalar + 4u * k, Real(999.0f));

  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, GL_FRONT_AND_BACK);
  b.cpu.Set(kR2, kGlShininess);
  b.cpu.Set(kR3, escalar);
  EXPECT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_Materialfv, b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), kAeeSuccess);
  const auto* sh = EstadoDoIgles11()->ParametroDeMaterial(GL_FRONT_AND_BACK, kGlShininess);
  ASSERT_NE(sh, nullptr);
  ASSERT_EQ(sh->size(), 1u);
  EXPECT_FLOAT_EQ((*sh)[0], 12.0f);
}

TEST(FrenteIgl2, OOrthofLeOsTresUltimosNaPilhaDoIgles11) {
  // A MOLDURA: `Orthof(iname *pMe, l, r, b, t, n, f)`. O `pMe` em r0 desloca
  // tudo: l, r, b em r1..r3 e t, n, f em [sp], [sp+4], [sp+8]. Uma moldura
  // errada daria uma matriz PLAUSIVEL e um desenho errado sem sintoma, por isso
  // o teste fixa os seis numeros e a matriz que eles produzem.
  BancoClasses b;
  ASSERT_NE(EstadoDoIgles11(), nullptr);
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, GL_PROJECTION);
  AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_MatrixMode, b.traco);
  AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_LoadIdentity, b.traco);
  EXPECT_EQ(EstadoDoIgles11()->ModoDeMatriz(), kModoProjection);

  b.mem.Escrever32(kPilhaDoTeste, Real(240.0f));      // top
  b.mem.Escrever32(kPilhaDoTeste + 4u, Real(0.0f));   // near
  b.mem.Escrever32(kPilhaDoTeste + 8u, Real(1.0f));   // far
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, Real(0.0f));    // left
  b.cpu.Set(kR2, Real(320.0f));  // right
  b.cpu.Set(kR3, Real(0.0f));    // bottom
  b.cpu.Set(kSP, kPilhaDoTeste);
  EXPECT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_Orthof, b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), kAeeSuccess);
  EXPECT_EQ(b.Faltas("IGLES11::Orthof"), 0u);

  // A matriz do `glOrtho(0, 320, 0, 240, 0, 1)`, column-major, na conta do GL:
  //   2/(r-l) = 2/320 | 2/(t-b) = 2/240 | -2/(f-n) = -2
  //   -(r+l)/(r-l) = -1 | -(t+b)/(t-b) = -1 | -(f+n)/(f-n) = -1
  const float* m = EstadoDoIgles11()->MatrizCorrente();
  EXPECT_FLOAT_EQ(m[0], 2.0f / 320.0f);
  EXPECT_FLOAT_EQ(m[5], 2.0f / 240.0f);
  EXPECT_FLOAT_EQ(m[10], -2.0f);
  EXPECT_FLOAT_EQ(m[12], -1.0f);
  EXPECT_FLOAT_EQ(m[13], -1.0f);
  EXPECT_FLOAT_EQ(m[14], -1.0f);
  EXPECT_FLOAT_EQ(m[15], 1.0f);
  // E o resto e zero: uma matriz plausivel com lixo nas outras celulas tambem
  // desenharia "quase" bem.
  EXPECT_FLOAT_EQ(m[1], 0.0f);
  EXPECT_FLOAT_EQ(m[4], 0.0f);
  EXPECT_FLOAT_EQ(m[11], 0.0f);
}

TEST(FrenteIgl2, UmPnameForaDaTabelaRecusaComOValorDeleERecusaEscrever) {
  BancoClasses b;
  ASSERT_NE(EstadoDoIgles11(), nullptr);
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, kGlLight0);
  b.cpu.Set(kR2, 0x00007777u);  // nao esta no cabecalho do SDK
  b.cpu.Set(kR3, 0x0002E100u);
  EXPECT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_Lightfv, b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), kAeeUnsupported);
  EXPECT_EQ(b.Faltas("IGLES11::Lightfv"), 1u);
  // A recusa DIZ o valor que nao soube servir: sem isso nao se sabe o que fazer.
  EXPECT_NE(b.Detalhe("IGLES11::Lightfv").find("0x00007777"), std::string::npos);
  // E nao escreveu estado nenhum (um "quase" seria pior do que a recusa).
  EXPECT_EQ(EstadoDoIgles11()->ParametroDeLuz(kGlLight0, 0x00007777u), nullptr);

  // O vector de parametros nulo tambem recusa (e nao le a memoria do endereco 0).
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, kGlLight0);
  b.cpu.Set(kR2, kGlAmbient);
  b.cpu.Set(kR3, 0u);
  EXPECT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_Lightfv, b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), kAeeUnsupported);
  EXPECT_EQ(b.Faltas("IGLES11::Lightfv"), 2u);
}

TEST(FrenteIgl2, AsDuasCapacidadesSemNomePassamATerNome) {
  BancoClasses b;
  ASSERT_NE(EstadoDoIgles11(), nullptr);

  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, kGlRescaleNormal);
  EXPECT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_Enable, b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), kAeeSuccess);
  EXPECT_EQ(b.Faltas("IGLES11::Enable"), 0u);
  EXPECT_TRUE(EstadoDoIgles11()->InterruptorLigado(kGlRescaleNormal));

  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, kGlColorMaterial);
  EXPECT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_Disable, b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), kAeeSuccess);
  EXPECT_EQ(b.Faltas("IGLES11::Disable"), 0u);
  EXPECT_FALSE(EstadoDoIgles11()->InterruptorLigado(kGlColorMaterial));

  // A TABELA NAO VIROU UM "ACEITA TUDO": o que nao tem nome continua a recusar,
  // e a recusa tem nome de metodo.
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, 0x00001234u);
  EXPECT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_Enable, b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), kAeeUnsupported);
  EXPECT_EQ(b.Faltas("IGLES11::Enable"), 1u);
}

TEST(FrenteIgl2, OMapaNovoServeOsSlotsQueFaltavamEOLerDaPilhaContinuaCerto) {
  // A OUTRA METADE da frente: os slots do IGLES11 que faltavam no mapa e que o
  // motor JA servia. Sem eles a recusa era `IGLES11::Scalex` / `Scissor` /
  // `GetIntegerv` / `BlendFunc` / `Color4x` -- os pedidos MEDIDOS em gof, pbc,
  // rmp, pacmania, ridgeracer e peggle.
  BancoClasses b;
  ASSERT_NE(EstadoDoIgles11(), nullptr);

  // Scalex(1, 1, 1) em GLfixed, depois da identidade: a diagonal fica em 1.
  b.cpu.Set(kR0, kObjetoIgles);
  AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_LoadIdentity, b.traco);
  b.cpu.Set(kR1, Fixo(1.0f));
  b.cpu.Set(kR2, Fixo(1.0f));
  b.cpu.Set(kR3, Fixo(1.0f));
  EXPECT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_Scalex, b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), kAeeSuccess);
  EXPECT_EQ(b.Faltas("IGLES11::Scalex"), 0u);
  EXPECT_FLOAT_EQ(EstadoDoIgles11()->MatrizCorrente()[0], 1.0f);

  // Scissor(1, 2, 3, 4): o QUARTO argumento real vem da pilha.
  b.mem.Escrever32(kPilhaDoTeste, 4u);
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, 1u);
  b.cpu.Set(kR2, 2u);
  b.cpu.Set(kR3, 3u);
  b.cpu.Set(kSP, kPilhaDoTeste);
  EXPECT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_Scissor, b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), kAeeSuccess);
  EXPECT_EQ(b.Faltas("IGLES11::Scissor"), 0u);
  const auto* scissor = EstadoDoIgles11()->Parametro(kIgl_Scissor, 0u);
  ASSERT_NE(scissor, nullptr);
  ASSERT_EQ(scissor->size(), 4u);
  EXPECT_EQ((*scissor)[3], 4u);

  // Color4x(1, 0, 0, 1): tambem quatro argumentos reais, o quarto na pilha.
  b.mem.Escrever32(kPilhaDoTeste, Fixo(1.0f));
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, Fixo(1.0f));
  b.cpu.Set(kR2, Fixo(0.0f));
  b.cpu.Set(kR3, Fixo(0.0f));
  b.cpu.Set(kSP, kPilhaDoTeste);
  EXPECT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_Color4x, b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), kAeeSuccess);
  EXPECT_EQ(EstadoDoIgles11()->Cor(), 0xFF0000FFu);  // RGBA8: R=FF A=FF

  // VertexPointer(3, GL_FIXED, 0, ponteiro): o quarto (o ponteiro) na pilha.
  b.mem.Escrever32(kPilhaDoTeste, 0x0002F000u);
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, 3u);
  b.cpu.Set(kR2, GL_FIXED);
  b.cpu.Set(kR3, 0u);
  b.cpu.Set(kSP, kPilhaDoTeste);
  EXPECT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_VertexPointer, b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), kAeeSuccess);
  const ArrayDeVertices* a = EstadoDoIgles11()->Array(GL_VERTEX_ARRAY);
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(a->ponteiro, 0x0002F000u);

  // GetIntegerv(GL_MAX_MODELVIEW_STACK_DEPTH) e BlendFunc: dois argumentos.
  const std::uint32_t destino = 0x0002E300u;
  b.mem.Escrever32(destino, 0u);
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, GL_MAX_MODELVIEW_STACK_DEPTH);
  b.cpu.Set(kR2, destino);
  EXPECT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_GetIntegerv, b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), kAeeSuccess);
  EXPECT_EQ(b.mem.Ler32(destino), kFundoModelView);

  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, GL_SRC_ALPHA);
  b.cpu.Set(kR2, GL_ONE_MINUS_SRC_ALPHA);
  EXPECT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_BlendFunc, b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), kAeeSuccess);
  // O `glBlendFunc` DEIXOU DE SER "SO ACUMULADO": a frente rast2 deu-lhe um
  // consumidor (o rasterizador mistura), e o que este teste tem de afirmar
  // passou a ser o ESTADO, e nao um mapa que ninguem le. Um teste que
  // continuasse a ler o `Parametro` passava a provar o caminho antigo.
  EXPECT_EQ(EstadoDoIgles11()->MisturaFonte(), GL_SRC_ALPHA);
  EXPECT_EQ(EstadoDoIgles11()->MisturaDestino(), GL_ONE_MINUS_SRC_ALPHA);
}
// ---------------------------------------------------------------------------
// 8. O `IGLES11::DrawArrays` -- a mesma falta de mapa, e a que DESENHA
// ---------------------------------------------------------------------------
//
// MEDIDO na corrida de referencia (`ZB2_QUADROS=300 ZB2_EVT_START=1`, 62
// titulos): o `IGLES11::DrawArrays` (slot 54 de `tools/igles_slots.inc`) e
// pedido 3914x pelo `abd` e 2117x pelo `torkandkral`, e nos DOIS ele e recusado
// pelo mapa (`SlotIglesNoIgl` nao tem a entrada). Os dois titulos ficam com
// ZERO pixels em toda a corrida. E exactamente o mesmo defeito do `AlphaFunc`:
// um metodo que existe no motor do IGL de 80 slots (`kIgl_DrawArrays`, 26) e
// que nao tem caminho nenhum a partir do IGLES11.
//
// MEDICAO ISOLADA, so com esta linha do mapa (1 linha + este teste):
//
//     REGRESSOES 0 | abd pixels 0 -> 107 750 948 | torkandkral 0 -> 107 740 737
//     faltas IGLES11::DrawArrays 3914 -> 0 e 2117 -> 0, e NADA mais mudou
//
// O TESTE ENTRA PELA TABELA (o endereco do slot 54 lido da vtable escrita na
// memoria do guest), e exige as duas metades: o metodo SERVIDO e os seis pixels
// na Tela -- "nao ha falta" tambem e o que se le de uma recusa por outro motivo.

TEST(FrenteMapaDoIgles, ODrawArraysDoIglesServeEDesenhaPelaTabela) {
  BancoClasses b;
  Tela tela;
  ASSERT_NE(EstadoDoIgles11(), nullptr);
  const_cast<Igl*>(EstadoDoIgles11())->DefinirTela(&tela);

  const std::uint32_t entrada =
      b.saidas.Endereco(kVtableIgles) + 4u * igles_slots::kIgles_DrawArrays;
  const std::uint32_t alvo = b.mem.Ler32(entrada);
  std::uint32_t indice = 0;
  ASSERT_TRUE(b.saidas.Contem(alvo, &indice)) << "entrada 0x" << std::hex << entrada;
  EXPECT_EQ(indice, kVtableIgles + igles_slots::kIgles_DrawArrays);

  constexpr std::uint32_t kV = 0x0002F100u;
  b.mem.Escrever32(kPilhaDoTeste, 8u);  // Viewport(0, 0, 8, 8): a altura na pilha
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, 0u);
  b.cpu.Set(kR2, 0u);
  b.cpu.Set(kR3, 8u);
  b.cpu.Set(kSP, kPilhaDoTeste);
  ASSERT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_Viewport, b.traco));

  const float v[3][2] = {{-1.0f, 0.75f}, {-1.0f, 0.0f}, {0.0f, 0.0f}};
  for (std::uint32_t k = 0; k < 3u; ++k) {
    b.mem.Escrever32(kV + 8u * k, Real(v[k][0]));
    b.mem.Escrever32(kV + 8u * k + 4u, Real(v[k][1]));
  }
  b.mem.Escrever32(kPilhaDoTeste, kV);
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, 2u);
  b.cpu.Set(kR2, GL_FLOAT);
  b.cpu.Set(kR3, 8u);
  b.cpu.Set(kSP, kPilhaDoTeste);
  ASSERT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_VertexPointer, b.traco));
  ASSERT_EQ(b.cpu.Get(kR0), kAeeSuccess);

  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, GL_VERTEX_ARRAY);
  ASSERT_TRUE(
      AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_EnableClientState, b.traco));
  ASSERT_EQ(b.cpu.Get(kR0), kAeeSuccess);

  b.mem.Escrever32(kPilhaDoTeste, Fixo(1.0f));
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, Fixo(1.0f));
  b.cpu.Set(kR2, Fixo(0.0f));
  b.cpu.Set(kR3, Fixo(0.0f));
  b.cpu.Set(kSP, kPilhaDoTeste);
  ASSERT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_Color4x, b.traco));
  ASSERT_EQ(b.cpu.Get(kR0), kAeeSuccess);

  // DrawArrays(GL_TRIANGLES, 0, 3): TRES argumentos reais, nenhum na pilha.
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, GL_TRIANGLES);
  b.cpu.Set(kR2, 0u);
  b.cpu.Set(kR3, 3u);
  ASSERT_TRUE(AtenderClasse(b.cpu, indice, b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), kAeeSuccess) << b.Detalhe("IGLES11::DrawArrays");
  EXPECT_EQ(b.Faltas("IGLES11::DrawArrays"), 0u) << b.Detalhe("IGLES11::DrawArrays");
  EXPECT_EQ(EstadoDoIgles11()->Desenhos(), 1u);
  EXPECT_EQ(EstadoDoIgles11()->Vertices(), 3u);
  EXPECT_EQ(tela.Escritos(), 6u) << "os seis fragmentos do triangulo foram escritos";
  EXPECT_EQ(EstadoDoIgles11()->RasterizadorRef().FragmentosDescartados(), 0u);
}

// ---------------------------------------------------------------------------
// 7. A FRENTE alphafunc: o `IGLES11::AlphaFunc`, a variante `float`
// ---------------------------------------------------------------------------
//
// MEDIDO na corrida de referencia (`/tmp/corrida_fmt.json`, `ZB2_QUADROS=300
// ZB2_EVT_START=1`, 62 titulos): no `rmp` o `IGLES11::AlphaFunc` (slot 3 de
// `tools/igles_slots.inc`) e pedido **299 vezes**, uma por quadro, e e a UNICA
// falta daquele titulo nos quadros (`recusadas_quadros = 299` de 304 recusas).
// Os dois argumentos reais, medidos no traco do proprio `rmp`
// (`/tmp/pesquisa/inst_bateria.out:77`):
//
//     r1 = 0x00000204   GL_GREATER (`gles_1_0/gl.h:99`, "AlphaFunction")
//     r2 = 0x3f000000   o BIT PATTERN do `float` 0.5 -- e nao 0x00010000, que e
//                       o 1.0 em GLfixed 16.16
//
// O `AEEGL.h` (a vtable de 80 slots do IGL) so tem o `glAlphaFuncx`
// (`AEEGL.h:63`, `GLclampx`); o `AEEGLES10.h:27` e o `AEEGLES11.h:84` tem as
// DUAS variantes. O id do motor e portanto INTERNO (como os tres `f`/`fv` de
// cima) e a leitura da referencia e `Real`, nunca `Fixo`: ler 0x3f000000 com o
// `Fixo` daria 0.0000076294, e um `GL_ALPHA_TEST` com essa referencia descarta
// tudo (ou nada) sem nenhum sintoma.
//
// COMO ESTES TESTES ENTRAM: pela TABELA. O endereco de saida do slot 3 e lido da
// VTABLE ESCRITA NA MEMORIA DO GUEST -- a mesma que o wrapper do SDK le pelo
// ponteiro do objecto -- e o indice de despacho sai desse endereco. Um teste que
// chamasse o id interno do motor provava o motor e nao a cablagem (armadilha 3).

TEST(FrenteAlphafunc, OAlphaFuncDoIglesServePelaTabelaEOMotorGuardaOEstado) {
  BancoClasses b;
  ASSERT_NE(EstadoDoIgles11(), nullptr);

  // A LEITURA DA TABELA, e nao um indice escrito a mao.
  const std::uint32_t entrada =
      b.saidas.Endereco(kVtableIgles) + 4u * igles_slots::kIgles_AlphaFunc;
  const std::uint32_t alvo = b.mem.Ler32(entrada);
  std::uint32_t indice = 0;
  ASSERT_TRUE(b.saidas.Contem(alvo, &indice)) << "entrada 0x" << std::hex << entrada;
  EXPECT_EQ(indice, kVtableIgles + igles_slots::kIgles_AlphaFunc);

  // O PEDIDO MEDIDO NO `rmp`: (GL_GREATER, 0.5f), 299x.
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, GL_GREATER);
  b.cpu.Set(kR2, Real(0.5f));
  b.cpu.Set(kR3, 0u);
  ASSERT_TRUE(AtenderClasse(b.cpu, indice, b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), kAeeSuccess) << b.Detalhe("IGLES11::AlphaFunc");
  EXPECT_EQ(b.Faltas("IGLES11::AlphaFunc"), 0u) << b.Detalhe("IGLES11::AlphaFunc");
  EXPECT_EQ(EstadoDoIgles11()->FuncaoDeAlfa(), GL_GREATER);
  // A ESCALA E A METADE QUE APANHA O ERRO: se o `AlphaFunc` fosse servido pelo
  // mesmo ramo do `AlphaFuncx` (o `Fixo`), este valor seria 7.6294e-06.
  EXPECT_FLOAT_EQ(EstadoDoIgles11()->AlfaDeReferencia(), 0.5f);

  // A OUTRA DIRECCAO: a mesma referencia na variante `x` (GLfixed 16.16) tem de
  // deixar o MESMO `float` no motor. As duas entradas, um so estado.
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, GL_GREATER);
  b.cpu.Set(kR2, Fixo(0.5f));
  ASSERT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_AlphaFuncx, b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), kAeeSuccess);
  EXPECT_FLOAT_EQ(EstadoDoIgles11()->AlfaDeReferencia(), 0.5f);
}

TEST(FrenteAlphafunc, OFragmentoEDescartadoPelaReferenciaEmFloatDoAlphaFunc) {
  // O ESTADO OBSERVAVEL NAO CHEGA: o que o `GL_ALPHA_TEST` faz e NAO ESCREVER, e
  // o contador de descartes e a unica forma de distinguir "descartou" de "nao
  // havia geometria" (`rasterizador.h:352`).
  BancoClasses b;
  Tela tela;
  ASSERT_NE(EstadoDoIgles11(), nullptr);
  // A TELA E LIGADA AO MOTOR DO IGLES11 pelo mesmo acto do despacho
  // (`Despacho::InstalarGl` chama `Igl::DefinirTela` sobre ele).
  const_cast<Igl*>(EstadoDoIgles11())->DefinirTela(&tela);
  Igl* motor = const_cast<Igl*>(EstadoDoIgles11());

  constexpr std::uint32_t kV = 0x0002F100u;  // os vertices (2 floats, passo 8)
  constexpr std::uint32_t kI = 0x0002F200u;  // os indices (GL_UNSIGNED_SHORT)

  // Viewport(0, 480-8, 8, 8): o quarto argumento real (a altura) vem da PILHA, e o
  // `y` conta de BAIXO (o `glViewport`), logo a janela de 8x8 no canto de cima de um
  // ecra de 480 tem `y = 480 - 8`. Estava `y = 0` -- a convencao que o `zeebx` do
  // Kaio corrigiu (`89dcd0f`).
  b.mem.Escrever32(kPilhaDoTeste, 8u);
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, 0u);
  b.cpu.Set(kR2, 480u - 8u);
  b.cpu.Set(kR3, 8u);
  b.cpu.Set(kSP, kPilhaDoTeste);
  ASSERT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_Viewport, b.traco));
  ASSERT_EQ(b.cpu.Get(kR0), kAeeSuccess);

  // O MESMO triangulo do teste da cablagem do rasterizador: tres vertices em NDC.
  const float v[3][2] = {{-1.0f, 0.75f}, {-1.0f, 0.0f}, {0.0f, 0.0f}};
  for (std::uint32_t k = 0; k < 3u; ++k) {
    b.mem.Escrever32(kV + 8u * k, Real(v[k][0]));
    b.mem.Escrever32(kV + 8u * k + 4u, Real(v[k][1]));
    b.mem.Escrever16(kI + 2u * k, static_cast<std::uint16_t>(k));
  }

  // VertexPointer(2, GL_FLOAT, 8, kV): o quarto argumento real e o ponteiro.
  b.mem.Escrever32(kPilhaDoTeste, kV);
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, 2u);
  b.cpu.Set(kR2, GL_FLOAT);
  b.cpu.Set(kR3, 8u);
  b.cpu.Set(kSP, kPilhaDoTeste);
  ASSERT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_VertexPointer, b.traco));
  ASSERT_EQ(b.cpu.Get(kR0), kAeeSuccess);

  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, GL_VERTEX_ARRAY);
  ASSERT_TRUE(
      AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_EnableClientState, b.traco));
  ASSERT_EQ(b.cpu.Get(kR0), kAeeSuccess);

  // A COR DA ORIGEM COM ALFA 0.25 -- 64/255 = 0.2509804, abaixo da referencia.
  const auto cor = [&](float alfa) {
    b.mem.Escrever32(kPilhaDoTeste, Fixo(alfa));
    b.cpu.Set(kR0, kObjetoIgles);
    b.cpu.Set(kR1, Fixo(1.0f));
    b.cpu.Set(kR2, Fixo(0.0f));
    b.cpu.Set(kR3, Fixo(0.0f));
    b.cpu.Set(kSP, kPilhaDoTeste);
    return AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_Color4x, b.traco);
  };
  const auto desenhar = [&]() {
    b.mem.Escrever32(kPilhaDoTeste, kI);
    b.cpu.Set(kR0, kObjetoIgles);
    b.cpu.Set(kR1, GL_TRIANGLES);
    b.cpu.Set(kR2, 3u);
    b.cpu.Set(kR3, GL_UNSIGNED_SHORT);
    b.cpu.Set(kSP, kPilhaDoTeste);
    return AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_DrawElements, b.traco);
  };

  ASSERT_TRUE(cor(0.25f));

  // O ALPHA TEST LIGADO E A REFERENCIA EM `float`, pelos DOIS slots do IGLES11.
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, GL_ALPHA_TEST);
  ASSERT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_Enable, b.traco));
  ASSERT_EQ(b.cpu.Get(kR0), kAeeSuccess);
  ASSERT_EQ(b.Faltas("IGLES11::Enable"), 0u);
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, GL_GREATER);
  b.cpu.Set(kR2, Real(0.5f));
  ASSERT_TRUE(
      AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_AlphaFunc, b.traco));
  ASSERT_EQ(b.cpu.Get(kR0), kAeeSuccess) << b.Detalhe("IGLES11::AlphaFunc");

  ASSERT_TRUE(desenhar());
  EXPECT_EQ(b.cpu.Get(kR0), kAeeSuccess) << b.Detalhe("IGLES11::DrawElements");
  EXPECT_EQ(tela.Escritos(), 0u) << "o fragmento reprovado nao escreve cor nem profundidade";
  EXPECT_EQ(motor->RasterizadorRef().FragmentosDescartados(), 6u)
      << "os seis fragmentos do triangulo foram reprovados pelo alpha test";

  // A OUTRA DIRECCAO, com a MESMA referencia: o alfa a 1 passa e escreve os seis.
  // Sem esta metade, "nao escreveu nada" seria tambem o que se le de um desenho
  // que nunca chegou a tela.
  ASSERT_TRUE(cor(1.0f));
  ASSERT_TRUE(desenhar());
  EXPECT_EQ(tela.Escritos(), 6u);
  EXPECT_EQ(motor->RasterizadorRef().FragmentosDescartados(), 6u)
      << "o segundo desenho nao descarta nenhum";
}

// ---------------------------------------------------------------------------
// 9. A FRENTE color4f: o `IGLES11::Color4f`, a variante `float`, e a COR
// ---------------------------------------------------------------------------
//
// MEDIDO na corrida de base (`/tmp/corrida_slot32.json`, `ZB2_QUADROS=300
// ZB2_EVT_START=1`, 62 titulos): o `IGLES11::Color4f` (slot 6 de
// `tools/igles_slots.inc`, `int (*Color4f) (iname *pMe, AEEGLfloat red,
// AEEGLfloat green, AEEGLfloat blue, AEEGLfloat alpha)`, `AEEGLES10.h:30`) e
// pedido **299 vezes em CADA UM dos dois titulos que o chamam** -- `abd` e
// `torkandkral`, uma por quadro, e e o UNICO pedido dos dois que o mapa
// `SlotIglesNoIgl` nao tem depois do `DrawArrays` ter entrado. Sao os DOIS
// titulos que passaram a desenhar com o `DrawArrays` (0 -> 107 750 948 e
// 107 740 737 px) e os dois com UMA SO COR: a cor do desenho fica no valor por
// omissao do motor (branco) porque nenhum pedido de cor chega la.
//
// O `AEEGL.h` (a vtable de 80 slots do IGL) so tem o `glColor4x`
// (`AEEGL.h:71`, `GLfixed`); as DUAS variantes estao no `AEEGLES10.h:30`
// (float) e `:64` (fixed), e a `x` (slot 40) ja esta no mapa. O id do motor e
// portanto INTERNO (como os quatro de cima) e a leitura dos QUATRO argumentos e
// `Real`, nunca `Fixo`: o `Fixo` sobre os bits de um `float` -- 0x3E800000 para
// 0.25 -- daria 16256.0, que o `Apertar` levava a 1.0, isto e, 0xFF em vez de
// 0x40, sem nenhum aviso.
//
// OS QUATRO ARGUMENTOS: o quarto (o alfa) vai na PILHA por causa do `pMe` em r0
// (`classes.cpp`, `SlotIglesTemQuartoNaPilha`).
//
// COMO ESTES TESTES ENTRAM: pela TABELA. O endereco do slot 6 e lido da vtable
// escrita na memoria do guest, e o indice de despacho sai desse endereco -- um
// teste que chamasse o id interno do motor provava o motor e nao a cablagem.

TEST(FrenteColor4f, OColor4fDoIglesServePelaTabelaEOEstadoGuardaACor) {
  BancoClasses b;
  ASSERT_NE(EstadoDoIgles11(), nullptr);

  const std::uint32_t entrada =
      b.saidas.Endereco(kVtableIgles) + 4u * igles_slots::kIgles_Color4f;
  const std::uint32_t alvo = b.mem.Ler32(entrada);
  std::uint32_t indice = 0;
  ASSERT_TRUE(b.saidas.Contem(alvo, &indice)) << "entrada 0x" << std::hex << entrada;
  EXPECT_EQ(indice, kVtableIgles + igles_slots::kIgles_Color4f);

  // (0.25, 0.5, 0.75, 0.5) em `float`; o QUARTO argumento vem da pilha.
  b.mem.Escrever32(kPilhaDoTeste, Real(0.5f));  // alfa
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, Real(0.25f));  // vermelho
  b.cpu.Set(kR2, Real(0.5f));   // verde
  b.cpu.Set(kR3, Real(0.75f));  // azul
  b.cpu.Set(kSP, kPilhaDoTeste);
  ASSERT_TRUE(AtenderClasse(b.cpu, indice, b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), kAeeSuccess) << b.Detalhe("IGLES11::Color4f");
  EXPECT_EQ(b.Faltas("IGLES11::Color4f"), 0u) << b.Detalhe("IGLES11::Color4f");
  // 64, 128, 191, 128 em 8 bits (`v * 255 + 0.5`), com o vermelho no BYTE 0.
  EXPECT_EQ(EstadoDoIgles11()->Cor(), 0x80BF8040u);

  // A OUTRA DIRECCAO: os MESMOS valores pela variante `x` (GLfixed 16.16) tem de
  // deixar o MESMO RGBA8. As duas escalas, um so estado -- e e esta metade que
  // falha se a `f` for apontada para o ramo da `x`.
  b.mem.Escrever32(kPilhaDoTeste, Fixo(0.5f));
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, Fixo(0.25f));
  b.cpu.Set(kR2, Fixo(0.5f));
  b.cpu.Set(kR3, Fixo(0.75f));
  b.cpu.Set(kSP, kPilhaDoTeste);
  ASSERT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_Color4x, b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), kAeeSuccess);
  EXPECT_EQ(EstadoDoIgles11()->Cor(), 0x80BF8040u);
}

TEST(FrenteColor4f, ACorDoColor4fChegaATelaNoDesenho) {
  // A ARMADILHA 3 outra vez: "nao ha falta" prova o estado, nao prova que a COR
  // sai do motor. Este desenha um triangulo depois do `Color4f` e le o PIXEL da
  // tela -- a cor tem de ser a pedida, e nao o branco por omissao.
  BancoClasses b;
  Tela tela;
  ASSERT_NE(EstadoDoIgles11(), nullptr);
  const_cast<Igl*>(EstadoDoIgles11())->DefinirTela(&tela);

  constexpr std::uint32_t kV = 0x0002F100u;  // os vertices (2 floats, passo 8)
  constexpr std::uint32_t kI = 0x0002F200u;  // os indices (GL_UNSIGNED_SHORT)

  // Viewport(0, 480-8, 8, 8): o `y` conta de BAIXO (o `glViewport`), logo a janela
  // de 8x8 no canto de cima de um ecra de 480 tem `y = 480 - 8` (a convencao do
  // `89dcd0f` do `zeebx`). O quarto argumento real (a altura) vem da PILHA.
  b.mem.Escrever32(kPilhaDoTeste, 8u);
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, 0u);
  b.cpu.Set(kR2, 480u - 8u);
  b.cpu.Set(kR3, 8u);
  b.cpu.Set(kSP, kPilhaDoTeste);
  ASSERT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_Viewport, b.traco));
  ASSERT_EQ(b.cpu.Get(kR0), kAeeSuccess);

  // O MESMO triangulo de seis fragmentos dos testes de cablagem do rasterizador.
  const float v[3][2] = {{-1.0f, 0.75f}, {-1.0f, 0.0f}, {0.0f, 0.0f}};
  for (std::uint32_t k = 0; k < 3u; ++k) {
    b.mem.Escrever32(kV + 8u * k, Real(v[k][0]));
    b.mem.Escrever32(kV + 8u * k + 4u, Real(v[k][1]));
    b.mem.Escrever16(kI + 2u * k, static_cast<std::uint16_t>(k));
  }

  b.mem.Escrever32(kPilhaDoTeste, kV);
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, 2u);
  b.cpu.Set(kR2, GL_FLOAT);
  b.cpu.Set(kR3, 8u);
  b.cpu.Set(kSP, kPilhaDoTeste);
  ASSERT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_VertexPointer, b.traco));
  ASSERT_EQ(b.cpu.Get(kR0), kAeeSuccess);

  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, GL_VERTEX_ARRAY);
  ASSERT_TRUE(
      AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_EnableClientState, b.traco));
  ASSERT_EQ(b.cpu.Get(kR0), kAeeSuccess);

  // A COR VERMELHA OPACA, PELO SLOT 6 (e nao pelo id interno): (1, 0, 0, 1).
  b.mem.Escrever32(kPilhaDoTeste, Real(1.0f));
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, Real(1.0f));
  b.cpu.Set(kR2, Real(0.0f));
  b.cpu.Set(kR3, Real(0.0f));
  b.cpu.Set(kSP, kPilhaDoTeste);
  ASSERT_TRUE(
      AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_Color4f, b.traco));
  ASSERT_EQ(b.cpu.Get(kR0), kAeeSuccess) << b.Detalhe("IGLES11::Color4f");
  ASSERT_EQ(EstadoDoIgles11()->Cor(), 0xFF0000FFu);

  // DrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, kI) -- os quatro
  // argumentos, os tres primeiros em r1..r3 e o quarto (os indices) na pilha.
  b.mem.Escrever32(kPilhaDoTeste, kI);
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, GL_TRIANGLES);
  b.cpu.Set(kR2, 3u);
  b.cpu.Set(kR3, GL_UNSIGNED_SHORT);
  b.cpu.Set(kSP, kPilhaDoTeste);
  ASSERT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_DrawElements, b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), kAeeSuccess) << b.Detalhe("IGLES11::DrawElements");

  // TODOS os pixels escritos dentro do viewport sao VERMELHOS (RGB565 0xF800), e
  // ha mais de zero. O vermelho da tela e o mesmo desempacotamento do
  // `rasterizador.cpp` (byte 0 = vermelho).
  std::uint32_t vermelhos = 0, outras = 0;
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 8; ++x) {
      const std::uint32_t p = tela.PixelEm(x, y);
      if (p == 0xF800u) {
        ++vermelhos;
      } else if (p != 0u) {
        ++outras;
      }
    }
  }
  EXPECT_EQ(outras, 0u) << "dentro do viewport so pode haver pixels vermelhos";
  EXPECT_GT(vermelhos, 0u) << "sem pixel nenhum, 'nao ha falta' era o unico sinal";
  EXPECT_EQ(vermelhos, tela.Escritos()) << "todos os escritos sao vermelhos";
}

// ---------------------------------------------------------------------------
// 10. A FRENTE getint: o `glGetIntegerv(GL_MAX_TEXTURE_SIZE)`
// ---------------------------------------------------------------------------
//
// MEDIDO ANTES DE MEXER (frente getint, base `dc132e7`). Um titulo por corrida,
// `ZB2_QUADROS=300 ZB2_EVT_START=1 ZB2_TRACE=1`, traco das tres corridas em
// `/tmp/getint_tr_{gof,pbc,rmp}.txt`:
//
//   [video] GL_glGetIntegerv slot=38 args=[00000d33 802000a8 ...] lr=0x00022b68
//           -> recusado | a memoria de texturas nao existe nesta etapa
//
// `args[0]` e o `pname` e o valor e 0x0D33 (GL_MAX_TEXTURE_SIZE, `gles_1_0/gl.h:224`),
// e e o UNICO `pname` que os 62 titulos pedem a este metodo: uma chamada no gof,
// uma no pbc e uma no rmp, sempre com o mesmo pname. Os tres levam o mesmo
// desfecho: `recusado`.
//
// DUAS FALTAS POR UMA CHAMADA, e isso e o desenho, nao um defeito: o wrapper do
// SDK (`sdk/src/GL.c`, `glGetIntegerv` -> `IGL_glGetIntegerv(GPIGL, pname, params)`)
// entra pelo IGL de 80 slots (o motor regista `glGetIntegerv`), e o objecto
// IGLES11 entra pelo slot 66 mapeado para o MESMO motor (o `classes.cpp` regista
// `IGLES11::GetIntegerv`). Uma correccao no motor serve os dois nomes -- e o
// teste verifica os DOIS contadores de faltas, porque e isso que a bateria conta.
//
// COMO ESTES TESTES ENTRAM: pela TABELA. O endereco de saida do slot vem da
// VTABLE ESCRITA NA MEMORIA DO GUEST -- a mesma que o wrapper do SDK le pelo
// ponteiro do objecto -- e o indice de despacho sai desse endereco. Um teste que
// chamasse o id interno do motor provava o motor e nao a cablagem (armadilha 3).

TEST(FrenteGetint, OMaxTextureSizeDoIgles11EhServidoPelaTabela) {
  BancoClasses b;
  ASSERT_NE(EstadoDoIgles11(), nullptr);

  // A TABELA, lida da memoria do guest no endereco que o objecto IGLES11 aponta.
  const std::uint32_t entrada =
      b.saidas.Endereco(kVtableIgles) + 4u * igles_slots::kIgles_GetIntegerv;
  const std::uint32_t alvo = b.mem.Ler32(entrada);
  std::uint32_t indice = 0;
  ASSERT_TRUE(b.saidas.Contem(alvo, &indice)) << "entrada 0x" << std::hex << entrada;
  EXPECT_EQ(indice, kVtableIgles + igles_slots::kIgles_GetIntegerv);

  // O PEDIDO MEDIDO NO gof, pbc E rmp, com o destino que os tres usam.
  constexpr std::uint32_t kDestino = 0x0002E300u;
  b.mem.Escrever32(kDestino, 0xDEADBEEFu);  // VENENO: apanhado se nao escrever
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, GL_MAX_TEXTURE_SIZE);
  b.cpu.Set(kR2, kDestino);
  ASSERT_TRUE(AtenderClasse(b.cpu, indice, b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), kAeeSuccess) << b.Detalhe("IGLES11::GetIntegerv");
  // OS DOIS NOMES DA MESMA CHAMADA, e nenhum deles com falta.
  EXPECT_EQ(b.Faltas("IGLES11::GetIntegerv"), 0u) << b.Detalhe("IGLES11::GetIntegerv");
  EXPECT_EQ(b.Faltas("glGetIntegerv"), 0u);

  // O NUMERO, escrito a mao, com a fonte: o guia do console diz, na linha 1254,
  // "Zeebo supports texture sizes of up to 1024 x 1024". O valor tem de estar na
  // memoria do guest -- e o titulo le-o de la.
  EXPECT_EQ(b.mem.Ler32(kDestino), 1024u);

  // O `GL_MAX_TEXTURE_SIZE` DEIXOU DE SER O UNICO: as tres profundidades de pilha
  // continuam servidas pelo mesmo caminho, com os mesmos numeros do motor.
  const std::uint32_t casos[4][2] = {{GL_MAX_MODELVIEW_STACK_DEPTH, 16u},
                                     {GL_MAX_PROJECTION_STACK_DEPTH, 2u},
                                     {GL_MAX_TEXTURE_STACK_DEPTH, 2u},
                                     {GL_MAX_TEXTURE_UNITS, kUnidadesDeTextura}};
  for (const auto& caso : casos) {
    b.mem.Escrever32(kDestino, 0xDEADBEEFu);
    b.cpu.Set(kR0, kObjetoIgles);
    b.cpu.Set(kR1, caso[0]);
    b.cpu.Set(kR2, kDestino);
    ASSERT_TRUE(AtenderClasse(b.cpu, indice, b.traco));
    EXPECT_EQ(b.cpu.Get(kR0), kAeeSuccess);
    EXPECT_EQ(b.mem.Ler32(kDestino), caso[1]) << "pname 0x" << std::hex << caso[0];
  }
}

TEST(FrenteGetint, OMaxTextureSizeTambemServeOPercursoDoIGLDe80Slots) {
  // A OUTRA PORTA: o wrapper do SDK (`GL.c: glGetIntegerv` ->
  // `IGL_glGetIntegerv(GPIGL, ...)`) chega pelo motor do IGL de 80 slots. Sem
  // esta metade, "o pedido foi servido" so estaria provado para o objecto
  // IGLES11 -- e o `glGetIntegerv` do SDK nao passa por la.
  Banco b;
  ASSERT_EQ(b.igl.Instalar(b.saidas), kIglSlots);

  // A TABELA, e nao um id interno: o slot 38 do IGL tem de apontar para o
  // endereco de saida DO SEU slot.
  EXPECT_EQ(b.mem.Ler32(b.igl.Vtable() + 4u * kIgl_GetIntegerv),
            b.saidas.Endereco(kVtableIgl + kIgl_GetIntegerv));

  constexpr std::uint32_t kDestino = 0x0002E400u;
  b.mem.Escrever32(kDestino, 0xDEADBEEFu);
  EXPECT_EQ(b.igl.Executar(kIgl_GetIntegerv, Args(GL_MAX_TEXTURE_SIZE, kDestino), nullptr),
            ResultadoGl::Feito);
  EXPECT_EQ(b.mem.Ler32(kDestino), 1024u);
  EXPECT_EQ(b.igl.Recusas().count("glGetIntegerv"), 0u);
}

TEST(FrenteGetint, OPnameQueNaoSeSabeResponderRecusaComONomeEDeixaODestinoIntacto) {
  // O OUTRO LADO DA REGRA (P2): um `pname` sem valor medido NAO pode receber um
  // numero inventado -- um viewport ou uma matriz a zero fariam um titulo
  // dimensionar texturas (ou recortar o ecra) com numeros que a maquina nao tem,
  // e nada avisava. Recusa, diz o pname, e NAO escreve.
  BancoClasses b;
  ASSERT_NE(EstadoDoIgles11(), nullptr);
  const std::uint32_t entrada =
      b.saidas.Endereco(kVtableIgles) + 4u * igles_slots::kIgles_GetIntegerv;
  std::uint32_t indice = 0;
  ASSERT_TRUE(b.saidas.Contem(b.mem.Ler32(entrada), &indice));

  constexpr std::uint32_t kDestino = 0x0002E500u;
  // `GL_VIEWPORT` (0x0BA2, `gles_1_1/gl.h:278`) esta no cabecalho do SDK, logo a
  // recusa diz o NOME. O numero vai escrito aqui porque `tools/gl_slots.inc` e
  // gerado do perfil Common-Lite e nao tem este nome (a mesma razao do `igl.cpp`).
  b.mem.Escrever32(kDestino, 0xDEADBEEFu);
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, 0x00000BA2u);  // GL_VIEWPORT
  b.cpu.Set(kR2, kDestino);
  ASSERT_TRUE(AtenderClasse(b.cpu, indice, b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), kAeeUnsupported);
  EXPECT_EQ(b.Faltas("IGLES11::GetIntegerv"), 1u);
  EXPECT_NE(b.Detalhe("IGLES11::GetIntegerv").find("0x00000ba2"), std::string::npos);
  EXPECT_NE(b.Detalhe("IGLES11::GetIntegerv").find("GL_VIEWPORT"), std::string::npos);
  EXPECT_EQ(b.mem.Ler32(kDestino), 0xDEADBEEFu) << "a recusa nao escreve no destino";
}

TEST(FrenteGetint, OPnameForaDoCabecalhoRecusaComONumeroDele) {
  // O mesmo, com um `pname` que nem o cabecalho do SDK tem: a recusa fica com o
  // NUMERO, que e a unica informacao que existe sobre ele. O banco e NOVO porque
  // o `Detalhe` de um nome devolve o PRIMEIRO evento com esse nome, e nesta
  // corrida o primeiro ja tem de ser este.
  BancoClasses b;
  ASSERT_NE(EstadoDoIgles11(), nullptr);
  const std::uint32_t entrada =
      b.saidas.Endereco(kVtableIgles) + 4u * igles_slots::kIgles_GetIntegerv;
  std::uint32_t indice = 0;
  ASSERT_TRUE(b.saidas.Contem(b.mem.Ler32(entrada), &indice));

  constexpr std::uint32_t kDestino = 0x0002E500u;
  b.mem.Escrever32(kDestino, 0xDEADBEEFu);
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, 0x0000ABCDu);
  b.cpu.Set(kR2, kDestino);
  ASSERT_TRUE(AtenderClasse(b.cpu, indice, b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), kAeeUnsupported);
  EXPECT_EQ(b.Faltas("IGLES11::GetIntegerv"), 1u);
  EXPECT_NE(b.Detalhe("IGLES11::GetIntegerv").find("0x0000abcd"), std::string::npos);
  EXPECT_EQ(b.mem.Ler32(kDestino), 0xDEADBEEFu) << "a recusa nao escreve no destino";
}

// ---------------------------------------------------------------------------
// 7. A FRENTE IGL9: O BLOCO `IGLES11` QUE NOVE TITULOS PEDEM
// ---------------------------------------------------------------------------
//
// A DEMANDA, MEDIDA no traco dos nove titulos da familia TTD --
// activitycenter, alice, dodgeball, footparty, funsoccer, zeeboids,
// zeebopeteca, zeebotennis, zeebovolley -- com `ZB2_QUADROS=300
// ZB2_EVT_START=1` (`/tmp/pesquisa/igl9-9trace.txt`, e as faltas por titulo em
// `/tmp/corrida_ram.json`). Os nove escrevem 307 200 px (UM `glClear`) e param:
//
//   IGLES11::ClearDepthx    12x   r1=0x0000ffff (1.0 em GLfixed)
//   IGLES11::ClearStencil    5x   r1=0x000000ff
//   IGLES11::LightModelxv    5x   r1=0x00000b53 (GL_LIGHT_MODEL_AMBIENT)
//   IGLES11::Disable         9x   r1=0x00000b90 (GL_STENCIL_TEST)
//   glDisable (o IGL de 80)  9x   o MESMO pedido, pelo outro objecto
//
// TRES DESTES QUATRO SLOTS NAO TINHAM CAMINHO NENHUM no mapa `SlotIglesNoIgl`
// (`core/brew/classes.cpp`): o `ClearDepthx`, o `ClearStencil` e o
// `LightModelxv` morriam no mapa e recebiam a recusa generica com r0..r3 -- o
// mesmo caso que o relatorio da frente `linhas` apanhou no `kIgles_LineWidthx`.
//
// E DOIS DEFEITOS DA TABELA DE CONSTANTES. `GL_STENCIL_TEST` (0x0B90) e o
// `GL_LIGHT_MODEL_TWO_SIDE` (0x0B52) estao no cabecalho `gles_1_1/gl.h`
// (`:197` e `:363`) e NAO no `tools/gl_slots.inc` gerado -- esse ficheiro le
// `gles_1_0/gl.h`, o perfil Common-Lite, que nao tem esses nomes. Sem eles a
// recusa dizia "capacidade desconhecida (sem nome no cabecalho deste modulo)":
// uma constante do GL ES 1.1 tratada como invencao do guest.

// Os valores do cabecalho do SDK que o `.inc` gerado nao tem:
//   gles_1_1/gl.h:197 GL_STENCIL_TEST 0x0B90
//   gles_1_1/gl.h:362 GL_LIGHT_MODEL_AMBIENT 0x0B53
//   gles_1_1/gl.h:363 GL_LIGHT_MODEL_TWO_SIDE 0x0B52
constexpr std::uint32_t kGlStencilTest = 0x0B90u;
constexpr std::uint32_t kGlLightModelAmbient = 0x0B53u;
constexpr std::uint32_t kGlLightModelTwoSide = 0x0B52u;

// O VALOR DE LIMPEZA DE PROFUNDIDADE E O QUE VAI AO RASTERIZADOR. A omissao do
// GL e 1.0, e um motor que a fixasse nunca deixaria um titulo escolher outra --
// o `glClearDepthx` e o UNICO caminho para a escolher.
TEST(FrenteIgl9, OClearDepthxDaTabelaDecideOValorQueFicaNoBuffer) {
  BancoClasses b;
  Tela tela;
  ASSERT_NE(EstadoDoIgles11(), nullptr);
  const_cast<Igl*>(EstadoDoIgles11())->DefinirTela(&tela);

  // A OMISSAO, medida primeiro: sem nenhum `ClearDepthx` o buffer nasce a 1.0
  // (`rasterizador.cpp`, `PrepararProfundidade`).
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, GL_DEPTH_BUFFER_BIT);
  ASSERT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_Clear, b.traco));
  ASSERT_EQ(b.cpu.Get(kR0), kAeeSuccess);
  ASSERT_TRUE(EstadoDoIgles11()->RasterizadorRef().TemBufferDeProfundidade());
  EXPECT_FLOAT_EQ(EstadoDoIgles11()->RasterizadorRef().ProfundidadeEm(0, 0), 1.0f);

  // O PEDIDO MEDIDO NO TRACO: `r1=0x0000ffff` = 65535/65536 = 1.0 em GLfixed.
  // O 0.5 do teste e o que distingue "o pedido foi usado" de "o motor tinha
  // 1.0 por omissao".
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, 0x00008000u);  // 0.5 em GLfixed
  EXPECT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_ClearDepthx, b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), kAeeSuccess);
  EXPECT_EQ(b.Faltas("IGLES11::ClearDepthx"), 0u) << b.Detalhe("IGLES11::ClearDepthx");
  EXPECT_FLOAT_EQ(EstadoDoIgles11()->ProfundidadeDeLimpeza(), 0.5f);

  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, GL_DEPTH_BUFFER_BIT);
  ASSERT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_Clear, b.traco));
  EXPECT_FLOAT_EQ(EstadoDoIgles11()->RasterizadorRef().ProfundidadeEm(0, 0), 0.5f);
  EXPECT_FLOAT_EQ(EstadoDoIgles11()->RasterizadorRef().ProfundidadeEm(639, 479), 0.5f);

  // E UM SEGUNDO PEDIDO MUDA O BUFFER OUTRA VEZ: um valor fixo no codigo
  // passaria as alineas de cima e falharia esta.
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, 0x00004000u);  // 0.25
  EXPECT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_ClearDepthx, b.traco));
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, GL_DEPTH_BUFFER_BIT);
  ASSERT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_Clear, b.traco));
  EXPECT_FLOAT_EQ(EstadoDoIgles11()->RasterizadorRef().ProfundidadeEm(0, 0), 0.25f);
}

// O STENCIL NAO EXISTE NESTA ARVORE, E ISSO PASSA A ESTAR DITO.
//
// O valor de limpeza FICA guardado (o estado existe, e um `glGetIntegerv` pode
// ter de o devolver), e o traco leva um PRESSUPOSTO: `EGL_STENCIL_SIZE 0`
// ("nao ha buffer de stencil", `core/brew/egl.cpp:151`). O que muda nao e o
// desenho -- e a recusa, que deixa de dizer "capacidade desconhecida" e passa a
// dizer o NOME da capacidade e a razao.
TEST(FrenteIgl9, OClearStencilGuardaOValorEDeclaraQueNaoHaBuffer) {
  BancoClasses b;
  ASSERT_NE(EstadoDoIgles11(), nullptr);
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, 0x000000FFu);  // o valor medido no traco
  EXPECT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_ClearStencil, b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), kAeeSuccess);
  EXPECT_EQ(b.Faltas("IGLES11::ClearStencil"), 0u) << b.Detalhe("IGLES11::ClearStencil");
  EXPECT_EQ(EstadoDoIgles11()->StencilDeLimpeza(), 0xFFu);

  // O PRESSUPOSTO, e nao uma falta: nao ha nada por fazer aqui -- ha uma coisa
  // que esta arvore nao tem, e quem le a corrida tem de a poder ver.
  const auto& p = b.traco.ContagemPressupostos();
  const auto it = p.find("glClearStencil");
  ASSERT_NE(it, p.end()) << "o pressuposto do stencil nao foi declarado";
  EXPECT_EQ(it->second, 1u) << "um pressuposto por chamada";
}

// A CAPACIDADE SEM NOME NAO PODE VOLTAR A ACONTECER. `GL_STENCIL_TEST` e o
// pedido MEDIDO 9x nos nove titulos -- pelo `IGLES11::Disable` E pelo
// `glDisable` do IGL de 80 slots, que partilham esta tabela (duas recusas por
// chamada, medidas no traco).
TEST(FrenteIgl9, OStencilTestTemNomeEEstadoEDeclaraOSeuLimite) {
  BancoClasses b;
  ASSERT_NE(EstadoDoIgles11(), nullptr);

  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, kGlStencilTest);
  EXPECT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_Disable, b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), kAeeSuccess);
  EXPECT_EQ(b.Faltas("IGLES11::Disable"), 0u) << b.Detalhe("IGLES11::Disable");
  EXPECT_FALSE(EstadoDoIgles11()->InterruptorLigado(kGlStencilTest));

  // E o `Enable` liga-o mesmo: o estado existe, para o `IsEnabled` nao mentir.
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, kGlStencilTest);
  EXPECT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_Enable, b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), kAeeSuccess);
  EXPECT_TRUE(EstadoDoIgles11()->InterruptorLigado(kGlStencilTest));

  // LIGADO, entra na lista do que o rasterizador NAO faz (o mesmo caminho do
  // scissor): o titulo que o liga ve uma falta com o NOME do limite, uma vez.
  Tela tela;
  const_cast<Igl*>(EstadoDoIgles11())->DefinirTela(&tela);
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, GL_COLOR_BUFFER_BIT);
  ASSERT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_Clear, b.traco));
  EXPECT_EQ(b.Faltas("teste_de_stencil_sem_buffer_de_stencil"), 1u);
}

// A TABELA DE CONSTANTES COMPLETA: TODA A `EnableCap` DO CABECALHO TEM NOME.
//
// A lista e a do `gles_1_1/gl.h`, na seccao `/* EnableCap */` (`:189-221`),
// mais as oito luzes (que o cabecalho deixa comentadas porque sao
// `GL_LIGHT0 + i`). O teste pergunta ao MOTOR, pela tabela, como o guest
// pergunta: uma constante sem nome RECUSA com "capacidade desconhecida", e o
// teste diz qual.
TEST(FrenteIgl9, TodaACapacidadeDoCabecalhoTemNomeEEstado) {
  constexpr std::uint32_t kEnableCaps[] = {
      0x0B60u,  // GL_FOG                      gles_1_1/gl.h:189
      0x0B50u,  // GL_LIGHTING                            :190
      0x0DE1u,  // GL_TEXTURE_2D                          :191
      0x0B44u,  // GL_CULL_FACE                           :192
      0x0BC0u,  // GL_ALPHA_TEST                          :193
      0x0BE2u,  // GL_BLEND                               :194
      0x0BF2u,  // GL_COLOR_LOGIC_OP                      :195
      0x0BD0u,  // GL_DITHER                              :196
      0x0B90u,  // GL_STENCIL_TEST                        :197
      0x0B71u,  // GL_DEPTH_TEST                          :198
      0x4000u, 0x4001u, 0x4002u, 0x4003u,  0x4004u, 0x4005u, 0x4006u, 0x4007u,
      0x0B10u,  // GL_POINT_SMOOTH                        :207
      0x0B20u,  // GL_LINE_SMOOTH                         :208
      0x0C11u,  // GL_SCISSOR_TEST                        :209
      0x0B57u,  // GL_COLOR_MATERIAL                      :210
      0x0BA1u,  // GL_NORMALIZE                           :211
      0x803Au,  // GL_RESCALE_NORMAL                      :212
      0x8037u,  // GL_POLYGON_OFFSET_FILL                 :213
      0x8074u,  // GL_VERTEX_ARRAY                        :214
      0x8075u,  // GL_NORMAL_ARRAY                        :215
      0x8076u,  // GL_COLOR_ARRAY                         :216
      0x8078u,  // GL_TEXTURE_COORD_ARRAY                 :217
      0x809Du,  // GL_MULTISAMPLE                         :218
      0x809Eu,  // GL_SAMPLE_ALPHA_TO_COVERAGE            :219
      0x809Fu,  // GL_SAMPLE_ALPHA_TO_ONE                 :220
      0x80A0u,  // GL_SAMPLE_COVERAGE                     :221
  };
  BancoClasses b;
  ASSERT_NE(EstadoDoIgles11(), nullptr);
  for (const std::uint32_t cap : kEnableCaps) {
    for (const std::uint32_t slot : {igles_slots::kIgles_Enable,
                                     igles_slots::kIgles_Disable}) {
      const std::size_t antes = b.Faltas("IGLES11::Enable") + b.Faltas("IGLES11::Disable");
      b.cpu.Set(kR0, kObjetoIgles);
      b.cpu.Set(kR1, cap);
      ASSERT_TRUE(AtenderClasse(b.cpu, kVtableIgles + slot, b.traco));
      EXPECT_EQ(b.cpu.Get(kR0), kAeeSuccess) << "capacidade 0x" << std::hex << cap;
      EXPECT_EQ(b.Faltas("IGLES11::Enable") + b.Faltas("IGLES11::Disable"), antes)
          << "capacidade 0x" << std::hex << cap << ": " << b.Detalhe("IGLES11::Disable")
          << b.Detalhe("IGLES11::Enable");
      EXPECT_EQ(EstadoDoIgles11()->InterruptorLigado(cap),
                slot == igles_slots::kIgles_Enable)
          << "capacidade 0x" << std::hex << cap;
    }
  }
}

// O AMBIENTE DO MODELO: UMA CAPACIDADE REAL DO GL, SERVIDA A SERIO.
//
// `glLightModelxv(GL_LIGHT_MODEL_AMBIENT, params)` com QUATRO valores em
// GLfixed: e o termo CONSTANTE da equacao de luz do GL ES 1.x
// (`cor = emissao + ambiente_do_material * ambiente_da_cena + ...`), e o
// rasterizador desta arvore ja o soma (`rasterizador.cpp:569`). O que faltava
// era o CAMINHO: o slot nao estava no mapa, e o pedido MEDIDO 5x nos nove
// titulos nunca chegava a este motor.
TEST(FrenteIgl9, OLightModelxvServeAAmbienteDaCenaPorGLfixed) {
  BancoClasses b;
  ASSERT_NE(EstadoDoIgles11(), nullptr);
  constexpr std::uint32_t kParams = 0x0002E100u;
  // OS VALORES SAO EXACTOS em 16.16 (0.5 = 0x8000, 0.25 = 0x4000, 1.0 =
  // 0x10000). O 0.2 NAO e (0x3333 = 0.19999695), e usa-lo aqui punha a
  // imprecisao da escala na expectativa em vez de na conversao.
  const std::uint32_t fixos[4] = {0x00008000u, 0x00004000u, 0u, 0x00010000u};  // 0.5, 0.25, 0, 1
  for (int k = 0; k < 4; ++k) b.mem.Escrever32(kParams + 4u * static_cast<std::uint32_t>(k), fixos[k]);

  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, kGlLightModelAmbient);
  b.cpu.Set(kR2, kParams);
  EXPECT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_LightModelxv, b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), kAeeSuccess);
  EXPECT_EQ(b.Faltas("IGLES11::LightModelxv"), 0u) << b.Detalhe("IGLES11::LightModelxv");

  const float* a = EstadoDoIgles11()->AmbienteDaCena();
  ASSERT_NE(a, nullptr);
  EXPECT_FLOAT_EQ(a[0], 0.5f);
  EXPECT_FLOAT_EQ(a[1], 0.25f);
  EXPECT_FLOAT_EQ(a[2], 0.0f);
  EXPECT_FLOAT_EQ(a[3], 1.0f);
}

// A METADE QUE NAO SE IMPLEMENTA, DITA PELO NOME. O `GL_LIGHT_MODEL_TWO_SIDE`
// (`gles_1_1/gl.h:363`) pede a iluminacao das DUAS faces, com um material por
// face; o rasterizador desta arvore tem UM material (a face e ignorada, como no
// ES 1.x) e nao ha medida do que o vendor do Zeebo faria. Recusa com o NOME, em
// vez de aceitar e nao fazer nada (P2).
TEST(FrenteIgl9, OLightModelxvComTwoSideRecusaComONome) {
  BancoClasses b;
  ASSERT_NE(EstadoDoIgles11(), nullptr);
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, kGlLightModelTwoSide);
  b.cpu.Set(kR2, 0x0002E200u);
  EXPECT_TRUE(AtenderClasse(b.cpu, kVtableIgles + igles_slots::kIgles_LightModelxv, b.traco));
  EXPECT_EQ(b.cpu.Get(kR0), kAeeUnsupported);
  EXPECT_NE(b.Detalhe("IGLES11::LightModelxv").find("GL_LIGHT_MODEL_TWO_SIDE"),
            std::string::npos)
      << b.Detalhe("IGLES11::LightModelxv");
  // O ESTADO DO AMBIENTE NAO FOI TOCADO por um pedido que nao e o dele.
  EXPECT_FLOAT_EQ(EstadoDoIgles11()->AmbienteDaCena()[0], 0.2f);
}

// ---------------------------------------------------------------------------
// 10. A FRENTE MATRIZ: OS CINCO SLOTS DO IGLES11 DA FAMILIA TECTOTY
// ---------------------------------------------------------------------------
//
// MEDIDO (corrida de referencia `/tmp/corrida_g5.json`, `ZB2_QUADROS=300
// ZB2_EVT_START=1`, 62 titulos): os CINCO titulos da familia TecToy -- `AirRacez`
// 277285, `Bajaz` 277727, `Boiaz` 278285, `JetBoardz` 278283 e `Rolimaz` 276809 --
// pedem cinco metodos do IGLES11 e recebem todos a recusa generica, porque os
// cinco MORREM no mapa `SlotIglesNoIgl` do `classes.cpp`. E a mesma morte em
// silencio que ja apanhou o `LineWidthx` e o `ClearDepthx`/`ClearStencil`/
// `LightModelxv`: um metodo que EXISTE no motor do IGL de 80 slots e nao tem
// caminho nenhum a partir do IGLES11.
//
//   IGLES11::PushMatrix    (89) | IGLES11::PopMatrix    (88)
//   IGLES11::Rotatex       (91) | IGLES11::FrontFace    (62)
//   IGLES11::ActiveTexture (31)
//
// OS NUMEROS DOS SLOTS sao os de `tools/igles_slots.inc` (gerado de
// `AEEGLES10.h`/`AEEGLES11.h`); o lado direito do mapa e o slot do IGL de
// `AEEGL.h` com o MESMO NOME -- a correspondencia e por nome, nunca por numero.
//
// OS ARGUMENTOS, lidos no traco do `Rolimaz` (`ZB2_TRACE=1`, o detalhe da recusa
// nomeada e que os traz):
//
//   IGLES11::PushMatrix    r1=0xf0027714 r2=0xf00276ec   (sem argumentos)
//   IGLES11::FrontFace     r1=0x00000900                  <- GL_CW
//   IGLES11::FrontFace     r1=0x00000901                  <- GL_CCW
//   IGLES11::ActiveTexture r1=0x000084c0                  <- GL_TEXTURE0
//   IGLES11::Rotatex       r1=0x00000000 r2=0 r3=0        <- angulo 0, eixo (0,0,?)
//
// O `FrontFace` e pedido DUAS vezes e com os DOIS valores: `0x0900` e **GL_CW**
// (`gles_1_0/gl.h:213`), e nao `GL_CCW`; `0x0901` (`:214`) e o `GL_CCW`. Quem
// escreveu `0x0900 = GL_CCW` leu o valor ao contrario, e a diferenca nao e
// decorativa: com o descarte de faces ligado os dois valores poem o MESMO
// triangulo em lados opostos.
//
// A PILHA DE MATRIZES JA ESTAVA CERTA, e isso e uma MEDICAO: o
// `kIgl_PushMatrix` COPIA a matriz corrente para o fundo seguinte e o
// `kIgl_PopMatrix` desce o indice, logo o `Pop` repoe mesmo o valor que la estava
// (o teste `EstadoGl.PushEPopVoltamAoQueEstava` fixa-o desde a etapa 6). O que
// faltava era o CAMINHO: sem o mapa, nem o `Push` nem o `Pop` chegavam ao motor.
// O teste abaixo (empilhar, MUDAR, repor, comparar) passa a ser o teste DAQUELE
// par pelo caminho do titulo -- e um `Pop` que so contasse, ou que repusesse a
// identidade, faz falhar pela diferenca entre 7 e 0.
//
// O QUARTO ARGUMENTO DO `Rotatex` VAI NA PILHA. O `glRotatex` tem QUATRO
// argumentos reais (`angle, x, y, z`) e a moldura do IGLES11 leva `iname *pMe` em
// r0: os tres primeiros caem em r1..r3 e o EIXO Z fica em `[sp]`. Sem a entrada
// na lista `SlotIglesTemQuartoNaPilha`, o z era lido do r3 (que nesta corrida vale
// 0), o eixo era o vector nulo e a `Rotacao` voltava sem tocar na matriz -- uma
// rotacao que "passa" e nao roda, sem sintoma nenhum. Nesta corrida o angulo e 0
// e a rotacao E a identidade: o que o teste prova e a LEITURA, com um eixo que
// nao e zero.
//
// COMO ESTES TESTES ENTRAM: pela TABELA, pelo indice `kVtableIgles + slot`, que e
// a mesma leitura do despacho. Um teste que chamasse o id interno do motor
// provava o motor e nao a cablagem (a armadilha 3 desta casa).

// `GL_TEXTURE1` = 0x84C1 (`gles_1_0/gl.h:406`). Nao esta em `gl_slots.inc` (o
// gerador so le o que o Toolset gera); o valor fica escrito com a linha ao lado.
constexpr std::uint32_t kGLTexture1 = 0x84C1u;

// Um pedido do IGLES11 pela TABELA. Escreve `sp0` no primeiro lugar da pilha (onde
// a moldura do IGLES11 poe o quarto argumento real) e devolve o r0.
std::uint32_t PedirNaTabelaDoIgles(BancoClasses& b, std::uint32_t slot, std::uint32_t r1 = 0,
                                   std::uint32_t r2 = 0, std::uint32_t r3 = 0,
                                   std::uint32_t sp0 = 0) {
  b.mem.Escrever32(kPilhaDoTeste, sp0);
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, r1);
  b.cpu.Set(kR2, r2);
  b.cpu.Set(kR3, r3);
  b.cpu.Set(kSP, kPilhaDoTeste);
  if (!AtenderClasse(b.cpu, kVtableIgles + slot, b.traco)) return 0xDEADBEEFu;
  return b.cpu.Get(kR0);
}

TEST(FrenteMatriz, OsCincoSlotsDaFamiliaTectoyRespondemPelaTabela) {
  BancoClasses b;
  ASSERT_NE(EstadoDoIgles11(), nullptr);
  // A ORDEM IMPORTA: o `PushMatrix` vem ANTES do `PopMatrix` (um `Pop` em pilha
  // vazia RECUSA, e isso e um teste proprio -- `EstadoGl.PopEmPilhaVaziaRecusa`).
  const std::uint32_t slots[] = {igles_slots::kIgles_PushMatrix, igles_slots::kIgles_PopMatrix,
                                 igles_slots::kIgles_Rotatex, igles_slots::kIgles_FrontFace,
                                 igles_slots::kIgles_ActiveTexture};
  const char* nomes[] = {"PushMatrix", "PopMatrix", "Rotatex", "FrontFace", "ActiveTexture"};
  for (std::size_t k = 0; k < 5u; ++k) {
    SCOPED_TRACE(nomes[k]);
    std::uint32_t r1 = 0, sp0 = 0;
    if (slots[k] == igles_slots::kIgles_Rotatex) {
      r1 = Fixo(90.0f);       // o angulo, em GLfixed 16.16
      sp0 = Fixo(1.0f);       // o eixo Z, o QUARTO argumento real: na PILHA
    }
    if (slots[k] == igles_slots::kIgles_FrontFace) r1 = GL_CCW;
    if (slots[k] == igles_slots::kIgles_ActiveTexture) r1 = GL_TEXTURE0;
    const std::string nome = std::string("IGLES11::") + nomes[k];
    EXPECT_EQ(PedirNaTabelaDoIgles(b, slots[k], r1, 0u, 0u, sp0), kAeeSuccess) << b.Detalhe(nome);
    EXPECT_EQ(b.Faltas(nome), 0u) << b.Detalhe(nome);
  }
  // O PAR CANCELOU: o `Pop` desceu o que o `Push` subiu.
  EXPECT_EQ(EstadoDoIgles11()->TopoDaPilha(kModoModelView), 0);

  // E O VALOR PEDIDO CHEGA AO ESTADO, e nao so "nao ha falta". O `FrontFace` e
  // pedido com os DOIS valores medidos no traco, e o primeiro deles e o GL_CW.
  EXPECT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_FrontFace, GL_CW), kAeeSuccess);
  EXPECT_EQ(EstadoDoIgles11()->FrontFace(), GL_CW);
  EXPECT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_FrontFace, GL_CCW), kAeeSuccess);
  EXPECT_EQ(EstadoDoIgles11()->FrontFace(), GL_CCW);
  // Um valor que nao e nenhum dos dois RECUSA com o nome, e o estado fica como
  // estava (P2: nunca "sucesso" sem efeito).
  EXPECT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_FrontFace, 0x1234u), kAeeUnsupported);
  EXPECT_EQ(b.Faltas("IGLES11::FrontFace"), 1u);
  EXPECT_EQ(EstadoDoIgles11()->FrontFace(), GL_CCW);

  // ActiveTexture: esta arvore tem UMA unidade de textura. `GL_TEXTURE0` e o
  // valor certo e E O QUE O TITULO PEDE (0x84c0 no traco); `GL_TEXTURE1`+ e
  // multitexture, uma EXTENSAO, e RECUSA COM O VALOR NOMEADO no motivo.
  EXPECT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_ActiveTexture, GL_TEXTURE0), kAeeSuccess);
  EXPECT_EQ(EstadoDoIgles11()->TexturaActiva(), GL_TEXTURE0);
  // O jogo pode montar uma segunda camada. Ela e isolada explicitamente: o
  // rasterizador so amostra a unidade 0, mas bind/upload da unidade 1 nao pode
  // destruir a textura base.
  EXPECT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_ActiveTexture, kGLTexture1),
            kAeeSuccess);
  EXPECT_EQ(b.Faltas("IGLES11::ActiveTexture"), 0u);
  EXPECT_EQ(EstadoDoIgles11()->TexturaActiva(), kGLTexture1);
}

TEST(FrenteMatriz, APilhaDeMatrizesEmpilhaMudaERepoePeloCaminhoDoTitulo) {
  BancoClasses b;
  ASSERT_NE(EstadoDoIgles11(), nullptr);
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_LoadIdentity), kAeeSuccess);
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_Translatex, Fixo(7.0f), 0u, 0u),
            kAeeSuccess);
  ASSERT_FLOAT_EQ(EstadoDoIgles11()->MatrizCorrente()[12], 7.0f);

  // 1) EMPILHA e 2) MUDA: a translacao nova escreve na COPIA, e o fundo de baixo
  // fica com os 7.
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_PushMatrix), kAeeSuccess);
  EXPECT_EQ(EstadoDoIgles11()->TopoDaPilha(kModoModelView), 1);
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_Translatex, Fixo(3.0f), 0u, 0u),
            kAeeSuccess);
  EXPECT_FLOAT_EQ(EstadoDoIgles11()->MatrizCorrente()[12], 10.0f);

  // 3) REPOE e 4) COMPARA. O `Pop` REPOE a matriz: o valor que la estava e 7, e
  // nao a identidade. Um `Pop` que so contasse (ou que repusesse a identidade)
  // da 0, e o teste falha -- e a razao de o valor ser 7 e nao 0.
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_PopMatrix), kAeeSuccess);
  EXPECT_FLOAT_EQ(EstadoDoIgles11()->MatrizCorrente()[12], 7.0f);
  EXPECT_EQ(EstadoDoIgles11()->TopoDaPilha(kModoModelView), 0);
  EXPECT_EQ(b.Faltas("IGLES11::PushMatrix") + b.Faltas("IGLES11::PopMatrix"), 0u);

  // 5) A PROFUNDIDADE E A QUE O GL ES 1.1 GARANTE: 16 niveis de MODELVIEW (a
  // matriz inicial + 15 `Push`), e o 16.o `Push` RECUSA com o motivo do motor
  // (nao ha "sucesso" sem efeito).
  int feitos = 0;
  for (int k = 0; k < 40; ++k) {
    if (PedirNaTabelaDoIgles(b, igles_slots::kIgles_PushMatrix) == kAeeSuccess) ++feitos;
  }
  EXPECT_EQ(feitos, kFundoModelView - 1);
  // E CADA UM DOS QUE NAO CABE RECUSA (40 - 15), com o motivo do motor -- e nao
  // um "sucesso" que deixa a matriz por guardar (P2).
  EXPECT_EQ(b.Faltas("IGLES11::PushMatrix"), 40u - static_cast<std::size_t>(feitos));
  EXPECT_NE(b.Detalhe("IGLES11::PushMatrix").find("cheia"), std::string::npos)
      << b.Detalhe("IGLES11::PushMatrix");
}

TEST(FrenteMatriz, ORotatexDoIgles11PosMultiplicaEUsaOEixoDaPilha) {
  BancoClasses b;
  ASSERT_NE(EstadoDoIgles11(), nullptr);
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_LoadIdentity), kAeeSuccess);
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_Translatex, Fixo(1.0f), 0u, 0u),
            kAeeSuccess);
  // `Rotatex(90, 0, 0, 1)` em GLfixed, na ordem do cabecalho: angulo em r1, eixo
  // em r2/r3 e o Z na PILHA.
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_Rotatex, Fixo(90.0f), Fixo(0.0f),
                                 Fixo(0.0f), Fixo(1.0f)),
            kAeeSuccess);
  ASSERT_EQ(b.Faltas("IGLES11::Rotatex"), 0u) << b.Detalhe("IGLES11::Rotatex");
  const float* m = EstadoDoIgles11()->MatrizCorrente();
  // O EIXO VEM DA PILHA. Com o quarto argumento a ler o r3 (que vale 0) o eixo
  // era o vector nulo, a `Rotacao` voltava sem escrever nada e o bloco 3x3 ficava
  // na identidade -- m[1] = 0 e m[4] = 0. Cos 90 = 0, sen 90 = 1: em
  // column-major, R[0][1] = -1 e m[4], e R[1][0] = 1 e m[1].
  EXPECT_NEAR(m[1], 1.0f, 1e-5f);
  EXPECT_NEAR(m[4], -1.0f, 1e-5f);
  EXPECT_NEAR(m[0], 0.0f, 1e-6f);
  EXPECT_NEAR(m[5], 0.0f, 1e-6f);
  EXPECT_NEAR(m[10], 1.0f, 1e-6f);
  // E A ORDEM E A DO GL: `M := M * R` (pos-multiplicacao), e nao `R * M`. Com a
  // ordem trocada a TRANSLACAO era rodada: T(1,0,0) por 90 graus em Z daria
  // (0,1,0), isto e m[12] = 0 e m[13] = 1. Na ordem do GL a ultima coluna e a
  // translacao, intacta pelo R que so mexe nas linhas 0..2.
  EXPECT_NEAR(m[12], 1.0f, 1e-5f);
  EXPECT_NEAR(m[13], 0.0f, 1e-5f);
}

TEST(FrenteMatriz, OFrontFaceChegaAoRasterizadorEPoeOMesmoTrianguloEmLadosOpostos) {
  // A CABLAGEM, e nao o estado: o `MontarEstado` (igl.cpp) leva o `front_face_` ao
  // `orientacao_da_frente` do rasterizador (`rasterizador.cpp:805`), e o que se
  // mede aqui e o EFEITO -- o MESMO triangulo, com a MESMA ordem de vertices, e
  // desenhado ou DESCARTADO conforme a orientacao que o TITULO declarou. Um teste
  // que so lesse `FrontFace()` provava o estado e nao o caminho ate aos pixels.
  BancoClasses b;
  Tela tela;
  ASSERT_NE(EstadoDoIgles11(), nullptr);
  const_cast<Igl*>(EstadoDoIgles11())->DefinirTela(&tela);
  Igl* motor = const_cast<Igl*>(EstadoDoIgles11());

  constexpr std::uint32_t kV = 0x0002F500u;
  constexpr std::uint32_t kI = 0x0002F600u;
  // A mesma geometria do teste do rasterizador (`ODescarteDeFacesUsaAOrientacaoEmNDC`):
  // a ordem (A, B, C) tem area POSITIVA em NDC, ou seja e a face da frente com
  // `GL_CCW`.
  const float v[3][2] = {{-1.0f, 0.75f}, {-1.0f, 0.0f}, {0.0f, 0.0f}};
  for (std::uint32_t k = 0; k < 3u; ++k) {
    b.mem.Escrever32(kV + 8u * k, Real(v[k][0]));
    b.mem.Escrever32(kV + 8u * k + 4u, Real(v[k][1]));
    b.mem.Escrever16(kI + 2u * k, static_cast<std::uint16_t>(k));
  }
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_Viewport, 0u, 480u - 8u, 8u, 8u), kAeeSuccess);
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_VertexPointer, 2u, GL_FLOAT, 8u, kV),
            kAeeSuccess);
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_EnableClientState, GL_VERTEX_ARRAY),
            kAeeSuccess);
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_Enable, GL_CULL_FACE), kAeeSuccess);
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_CullFace, GL_BACK), kAeeSuccess);

  const auto desenhar = [&]() {
    return PedirNaTabelaDoIgles(b, igles_slots::kIgles_DrawElements, GL_TRIANGLES, 3u,
                                GL_UNSIGNED_SHORT, kI);
  };

  // `GL_CCW` (0x0901, o SEGUNDO valor do traco): a face da frente e esta e o
  // triangulo escreve os seis fragmentos.
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_FrontFace, GL_CCW), kAeeSuccess);
  ASSERT_EQ(desenhar(), kAeeSuccess) << b.Detalhe("IGLES11::DrawElements");
  EXPECT_EQ(tela.Escritos(), 6u);
  EXPECT_EQ(motor->RasterizadorRef().TriangulosDescartados(), 0u);

  // `GL_CW` (0x0900, o PRIMEIRO valor do traco): a frente passa a ser a outra face
  // e o mesmo triangulo e DESCARTADO -- nenhum pixel novo. Se o `FrontFace` nao
  // chegasse ao rasterizador, esta metade escrevia os mesmos seis e nao se via.
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_FrontFace, GL_CW), kAeeSuccess);
  ASSERT_EQ(desenhar(), kAeeSuccess) << b.Detalhe("IGLES11::DrawElements");
  EXPECT_EQ(tela.Escritos(), 6u) << "nenhum pixel NOVO: o triangulo foi descartado";
  EXPECT_EQ(motor->RasterizadorRef().TriangulosDescartados(), 1u);
}

// ---------------------------------------------------------------------------
// A FRENTE textura: O BIND/TEXIMAGE DO IGLES11 TEM DE CHEGAR AO MOTOR DO IGL
// ---------------------------------------------------------------------------
//
// O DEFEITO (medido em `/tmp/pesquisa/estudo-video.md`): o `IGLES11::BindTexture`
// e o `IGLES11::TexImage2D` escreviam o bloco lateral `kEstadoIgles` e faziam
// `return` ANTES do mapa `SlotIglesNoIgl` (`classes.cpp`). O motor do Igl -- o
// que o `MontarEstado` leva ao rasterizador -- ficava com `textura_ligada_ == 0`
// e com nenhuma textura; a guarda `igl.cpp:517` nunca punha textura no retrato
// do desenho. Consequencia medida nos 62 titulos: **59 222 desenhos em 10
// titulos, ZERO com textura ligada**, e esses 10 somam 1 882 689 500 px com 1 a
// 3 cores.
//
// OS DOIS TESTES ENTRAM PELA TABELA (`kVtableIgles + slot`), que e a leitura do
// despacho -- a armadilha 3 desta casa e um teste que chama o id interno do
// motor e prova o motor e nao a cablagem.

// O BLOCO LATERAL DO `glDrawTex*OES`. As constantes vivem no namespace anonimo
// de `classes.cpp`; os numeros ficam escritos aqui com a linha ao lado, e sao
// eles que o `glDrawTex*OES` le no guest.
constexpr std::uint32_t kEstadoIgles = 0x8F030000u;
constexpr std::uint32_t kIglesTexLigada = kEstadoIgles + 0u;
constexpr std::uint32_t kIglesTexPonteiro = kEstadoIgles + 20u;

// Um pedido do IGLES11 pela TABELA com a PILHA escrita a mao. O `glTexImage2D`
// tem nove argumentos reais e a moldura do IGLES11 leva o `po` em r0: o quarto
// argumento real e o primeiro lugar da pilha (`classes.cpp`,
// `SlotIglesTemQuartoNaPilha`), e os nove caem em `sp+0..sp+20`.
std::uint32_t PedirComPilhaNaTabela(BancoClasses& b, std::uint32_t slot, std::uint32_t r1,
                                    std::uint32_t r2, std::uint32_t r3,
                                    const std::uint32_t* pilha, std::size_t quantos) {
  for (std::size_t k = 0; k < quantos; ++k) {
    b.mem.Escrever32(kPilhaDoTeste + 4u * static_cast<std::uint32_t>(k), pilha[k]);
  }
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, r1);
  b.cpu.Set(kR2, r2);
  b.cpu.Set(kR3, r3);
  b.cpu.Set(kSP, kPilhaDoTeste);
  if (!AtenderClasse(b.cpu, kVtableIgles + slot, b.traco)) return 0xDEADBEEFu;
  return b.cpu.Get(kR0);
}

TEST(FrenteTextura, OBindTextureEOTexImage2DChegamAoMotorDoIgl) {
  BancoClasses b;
  ASSERT_NE(EstadoDoIgles11(), nullptr);
  constexpr std::uint32_t kTexels = 0x0002C000u;
  b.mem.Escrever32(kTexels, 0xFF00FF00u);  // um texel verde (RGBA8, LE)
  // width, height, border, format, type, pixels
  const std::uint32_t pilha[6] = {2u, 2u, 0u, GL_RGBA, GL_UNSIGNED_BYTE, kTexels};

  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_BindTexture, GL_TEXTURE_2D, 5u),
            kAeeSuccess)
      << b.Detalhe("IGLES11::BindTexture");
  ASSERT_EQ(PedirComPilhaNaTabela(b, igles_slots::kIgles_TexImage2D, GL_TEXTURE_2D, 0u, GL_RGBA,
                                  pilha, 6u),
            kAeeSuccess)
      << b.Detalhe("IGLES11::TexImage2D");

  // 1. O BLOCO LATERAL CONTINUA ESCRITO, e nao pode desaparecer com esta
  // correccao: e dele que o `glDrawTex*OES` (`DesenharRectTexturaIgles`) le a
  // textura que desenha.
  EXPECT_EQ(b.mem.Ler32(kIglesTexLigada), 5u);
  EXPECT_EQ(b.mem.Ler32(kIglesTexPonteiro), kTexels);

  // 2. O MOTOR DO IGL. E este estado que o `MontarEstado` leva ao
  // `EstadoDeRasterizacao` do desenho. Antes da correccao o `return` deixava-o
  // a ZERO -- e nenhuma textura chegava a rasterizacao em titulo nenhum.
  EXPECT_EQ(EstadoDoIgles11()->TexturaLigada(), 5u)
      << "o BindTexture do IGLES11 nao chegou ao motor do Igl";
  const EstadoDaTextura* t = EstadoDoIgles11()->Textura(5u);
  ASSERT_NE(t, nullptr) << "o TexImage2D do IGLES11 nao chegou ao motor do Igl";
  EXPECT_EQ(t->largura, 2u);
  EXPECT_EQ(t->altura, 2u);
  EXPECT_EQ(t->formato_do_pixel, GL_RGBA);
  EXPECT_EQ(t->tipo, GL_UNSIGNED_BYTE);
  EXPECT_EQ(t->ponteiro, kTexels);
  EXPECT_EQ(t->uploade, 1u);
  EXPECT_EQ(b.Faltas("IGLES11::BindTexture") + b.Faltas("IGLES11::TexImage2D"), 0u)
      << b.Detalhe("IGLES11::BindTexture") << b.Detalhe("IGLES11::TexImage2D");
}

TEST(FrenteTextura, ODesenhoAmostraATexturaLigadaNoEcra) {
  // A CABLAGEM ATE AO PIXEL, e nao o estado: um quadrilatero que ocupa a janela
  // inteira (8x8), uma textura 2x2 com quatro cores distintas e a cor do vertice
  // a VERMELHO. Com o caminho de textura morto o quadrilatero fica com UMA cor
  // (a do `glColor4x`); com ele ligado os quatro quadrantes mostram os quatro
  // texels. A medida e a mesma da bateria: CORES.
  BancoClasses b;
  Tela tela;
  ASSERT_NE(EstadoDoIgles11(), nullptr);
  const_cast<Igl*>(EstadoDoIgles11())->DefinirTela(&tela);

  constexpr std::uint32_t kTexels = 0x0002C000u;
  constexpr std::uint32_t kV = 0x0002F500u;
  constexpr std::uint32_t kI = 0x0002F600u;
  constexpr std::uint32_t kT = 0x0002F700u;
  // A textura 2x2, em RGBA8: fila 0 = vermelho, verde | fila 1 = azul, branco.
  b.mem.Escrever32(kTexels + 0u, 0xFF0000FFu);
  b.mem.Escrever32(kTexels + 4u, 0xFF00FF00u);
  b.mem.Escrever32(kTexels + 8u, 0xFFFF0000u);
  b.mem.Escrever32(kTexels + 12u, 0xFFFFFFFFu);
  // O quadrilatero -1..1 em NDC cobre a janela 0..8: (x=-1,y=-1) -> (0,8).
  const float v[4][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};
  const float uv[4][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
  const std::uint16_t idx[6] = {0u, 1u, 2u, 0u, 2u, 3u};
  for (std::uint32_t k = 0; k < 4u; ++k) {
    b.mem.Escrever32(kV + 8u * k, Real(v[k][0]));
    b.mem.Escrever32(kV + 8u * k + 4u, Real(v[k][1]));
    b.mem.Escrever32(kT + 8u * k, Real(uv[k][0]));
    b.mem.Escrever32(kT + 8u * k + 4u, Real(uv[k][1]));
  }
  for (std::uint32_t k = 0; k < 6u; ++k) b.mem.Escrever16(kI + 2u * k, idx[k]);

  const std::uint32_t pilha[6] = {2u, 2u, 0u, GL_RGBA, GL_UNSIGNED_BYTE, kTexels};
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_Viewport, 0u, 480u - 8u, 8u, 8u), kAeeSuccess);
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_BindTexture, GL_TEXTURE_2D, 5u),
            kAeeSuccess);
  ASSERT_EQ(PedirComPilhaNaTabela(b, igles_slots::kIgles_TexImage2D, GL_TEXTURE_2D, 0u, GL_RGBA,
                                  pilha, 6u),
            kAeeSuccess)
      << b.Detalhe("IGLES11::TexImage2D");
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_Enable, GL_TEXTURE_2D), kAeeSuccess);
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_EnableClientState, GL_VERTEX_ARRAY),
            kAeeSuccess);
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_VertexPointer, 2u, GL_FLOAT, 8u, kV),
            kAeeSuccess);
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_EnableClientState, GL_TEXTURE_COORD_ARRAY),
            kAeeSuccess);
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_TexCoordPointer, 2u, GL_FLOAT, 8u, kT),
            kAeeSuccess);
  // A COR DO VERTICE: vermelho opaco (`glColor4x(1, 0, 0, 1)`), o unico dado de
  // cor que existe quando a textura nao chega ao rasterizador.
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_Color4x, Fixo(1.0f), 0u, 0u, Fixo(1.0f)),
            kAeeSuccess);
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_DrawElements, GL_TRIANGLES, 6u,
                                 GL_UNSIGNED_SHORT, kI),
            kAeeSuccess)
      << b.Detalhe("IGLES11::DrawElements");
  ASSERT_EQ(tela.Escritos(), 64u) << "o quadrilatero nao cobriu a janela 8x8";

  // A PROVA: os quatro texels aparecem, e nao uma so cor. Sem o caminho de
  // textura a janela fica com o vermelho do `glColor4x` (0xF800) e mais nada.
  std::set<std::uint32_t> cores;
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 8; ++x) cores.insert(tela.PixelEm(x, y));
  }
  EXPECT_EQ(tela.CoresEm(0, 0, 8, 8), 4u) << "uma so cor = a textura nao foi amostrada";
  EXPECT_TRUE(cores.count(0xF800u) == 1u) << "falta o vermelho da textura";
  EXPECT_TRUE(cores.count(0x07E0u) == 1u) << "falta o verde da textura";
  EXPECT_TRUE(cores.count(0x001Fu) == 1u) << "falta o azul da textura";
  EXPECT_TRUE(cores.count(0xFFFFu) == 1u) << "falta o branco da textura";
}

TEST(FrenteTextura, AReservaDeTexturaServeOSDesenhoENaoORecusa) {
  // O PONTO 2: `glTexImage2D(..., pixels = NULL)` e RESERVA, e nao recusa. E o
  // par que o `ridgeracer` e o `pacmania` fazem (medido no traco: 2 reservas e 4
  // `glTexSubImage2D` cada um, e as faltas `IGLES11::TexImage2D` e
  // `IGLES11::TexSubImage2D` passam de 2 a 0 por titulo).
  //
  // DUAS METADES, e a segunda e a que interessa:
  //   (a) com a reserva e SEM texels, o desenho NAO recusa -- segue com a cor do
  //       vertice e a reserva fica nomeada (`textura_reservada_sem_texels`). Uma
  //       recusa aqui apagaria os 307 200 px do `ridgeracer` e os 1,45 G px do
  //       `pacmania`, que a corrida de referencia TEM;
  //   (b) com os texels a chegar pelo `glTexSubImage2D`, a MESMA textura passa a
  //       ser amostrada -- as quatro cores aparecem.
  BancoClasses b;
  Tela tela;
  ASSERT_NE(EstadoDoIgles11(), nullptr);
  const_cast<Igl*>(EstadoDoIgles11())->DefinirTela(&tela);

  constexpr std::uint32_t kTexels = 0x0002C000u;
  constexpr std::uint32_t kV = 0x0002F500u;
  constexpr std::uint32_t kI = 0x0002F600u;
  constexpr std::uint32_t kT = 0x0002F700u;
  b.mem.Escrever32(kTexels + 0u, 0xFF0000FFu);
  b.mem.Escrever32(kTexels + 4u, 0xFF00FF00u);
  b.mem.Escrever32(kTexels + 8u, 0xFFFF0000u);
  b.mem.Escrever32(kTexels + 12u, 0xFFFFFFFFu);
  const float v[4][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};
  const float uv[4][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
  const std::uint16_t idx[6] = {0u, 1u, 2u, 0u, 2u, 3u};
  for (std::uint32_t k = 0; k < 4u; ++k) {
    b.mem.Escrever32(kV + 8u * k, Real(v[k][0]));
    b.mem.Escrever32(kV + 8u * k + 4u, Real(v[k][1]));
    b.mem.Escrever32(kT + 8u * k, Real(uv[k][0]));
    b.mem.Escrever32(kT + 8u * k + 4u, Real(uv[k][1]));
  }
  for (std::uint32_t k = 0; k < 6u; ++k) b.mem.Escrever16(kI + 2u * k, idx[k]);

  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_Viewport, 0u, 480u - 8u, 8u, 8u), kAeeSuccess);
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_BindTexture, GL_TEXTURE_2D, 5u),
            kAeeSuccess);
  // A RESERVA: duas por duas, interior RGBA, pixels NULL.
  const std::uint32_t reserva[6] = {2u, 2u, 0u, GL_RGBA, GL_UNSIGNED_BYTE, 0u};
  ASSERT_EQ(PedirComPilhaNaTabela(b, igles_slots::kIgles_TexImage2D, GL_TEXTURE_2D, 0u, GL_RGBA,
                                  reserva, 6u),
            kAeeSuccess)
      << b.Detalhe("IGLES11::TexImage2D");
  ASSERT_EQ(b.Faltas("IGLES11::TexImage2D"), 0u)
      << "a reserva voltou a ser recusa: " << b.Detalhe("IGLES11::TexImage2D");
  const EstadoDaTextura* t = EstadoDoIgles11()->Textura(5u);
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(t->largura, 2u);
  EXPECT_EQ(t->ponteiro, 0u) << "a reserva nao tem texels ate um glTexSubImage2D";

  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_Enable, GL_TEXTURE_2D), kAeeSuccess);
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_EnableClientState, GL_VERTEX_ARRAY),
            kAeeSuccess);
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_VertexPointer, 2u, GL_FLOAT, 8u, kV),
            kAeeSuccess);
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_EnableClientState, GL_TEXTURE_COORD_ARRAY),
            kAeeSuccess);
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_TexCoordPointer, 2u, GL_FLOAT, 8u, kT),
            kAeeSuccess);
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_Color4x, Fixo(1.0f), 0u, 0u, Fixo(1.0f)),
            kAeeSuccess);

  // (a) O DESENHO NAO E RECUSADO pela reserva.
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_DrawElements, GL_TRIANGLES, 6u,
                                 GL_UNSIGNED_SHORT, kI),
            kAeeSuccess)
      << b.Detalhe("IGLES11::DrawElements");
  EXPECT_EQ(tela.Escritos(), 64u) << "um desenho RECUSADO por causa da reserva";
  EXPECT_EQ(tela.CoresEm(0, 0, 8, 8), 1u) << "sem texels nao ha textura para amostrar";
  EXPECT_EQ(tela.PixelEm(0, 0), 0xF800u) << "a cor do vertice, e nao o endereco zero";
  EXPECT_EQ(b.Faltas("textura_reservada_sem_texels"), 1u)
      << "a reserva tem de ficar NOMEADA (uma vez, e nao uma por desenho)";

  // (b) OS TEXELS CHEGAM pelo `glTexSubImage2D` e a MESMA textura passa a ser
  // amostrada. A assinatura do slot: (alvo, nivel, x, y, larg, alt, formato,
  // tipo, pixels) -- o quinto argumento real (o `y`) e o primeiro da pilha.
  const std::uint32_t sub[6] = {0u, 2u, 2u, GL_RGBA, GL_UNSIGNED_BYTE, kTexels};
  ASSERT_EQ(PedirComPilhaNaTabela(b, igles_slots::kIgles_TexSubImage2D, GL_TEXTURE_2D, 0u, 0u,
                                  sub, 6u),
            kAeeSuccess)
      << b.Detalhe("IGLES11::TexSubImage2D");
  tela.Limpar();
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_DrawElements, GL_TRIANGLES, 6u,
                                 GL_UNSIGNED_SHORT, kI),
            kAeeSuccess)
      << b.Detalhe("IGLES11::DrawElements");
  EXPECT_EQ(tela.CoresEm(0, 0, 8, 8), 4u) << "a textura reservada e depois preenchida nao foi amostrada";
}

// O FILTRO LINEAR E SERVIDO COMO NEAREST, E ISSO FICA DECLARADO.
//
// Precedente do stencil (`OClearStencilGuardaOValorEDeclaraQueNaoHaBuffer`):
// capacidade que esta arvore nao tem vira PRESSUPOSTO com nome e razao, e nao
// falta. O rasterizador amostra o texel mais proximo; um titulo que peca
// GL_LINEAR desenha por inteiro com diferenca sub-texel -- nao ha ausencia
// para recusar, ha uma aproximacao para declarar. MEDIDO em 7 titulos
// (`Rolimaz`, `AirRacez`, `gof`, `Bajaz`, `Boiaz`, `pbc`, `tekken2`), 1 pedido
// cada: com a falta, a lista de demanda escondia os pedidos reais deles.
TEST(FrenteIgl9, FiltroLinearEServidoComoNearestEDeclarado) {
  BancoClasses b;
  Tela tela;
  ASSERT_NE(EstadoDoIgles11(), nullptr);
  const_cast<Igl*>(EstadoDoIgles11())->DefinirTela(&tela);

  constexpr std::uint32_t kTexels = 0x0002C000u;
  constexpr std::uint32_t kV = 0x0002F500u;
  constexpr std::uint32_t kI = 0x0002F600u;
  constexpr std::uint32_t kT = 0x0002F700u;
  b.mem.Escrever32(kTexels + 0u, 0xFF0000FFu);
  b.mem.Escrever32(kTexels + 4u, 0xFF00FF00u);
  b.mem.Escrever32(kTexels + 8u, 0xFFFF0000u);
  b.mem.Escrever32(kTexels + 12u, 0xFFFFFFFFu);
  const float v[4][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};
  const float uv[4][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
  const std::uint16_t idx[6] = {0u, 1u, 2u, 0u, 2u, 3u};
  for (std::uint32_t k = 0; k < 4u; ++k) {
    b.mem.Escrever32(kV + 8u * k, Real(v[k][0]));
    b.mem.Escrever32(kV + 8u * k + 4u, Real(v[k][1]));
    b.mem.Escrever32(kT + 8u * k, Real(uv[k][0]));
    b.mem.Escrever32(kT + 8u * k + 4u, Real(uv[k][1]));
  }
  for (std::uint32_t k = 0; k < 6u; ++k) b.mem.Escrever16(kI + 2u * k, idx[k]);

  // O titulo pede LINEAR nos dois filtros (e o que os 7 fazem).
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_TexParameterx, GL_TEXTURE_2D,
                                 GL_TEXTURE_MIN_FILTER, GL_LINEAR),
            kAeeSuccess);
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_TexParameterx, GL_TEXTURE_2D,
                                 GL_TEXTURE_MAG_FILTER, GL_LINEAR),
            kAeeSuccess);
  const std::uint32_t pilha[6] = {2u, 2u, 0u, GL_RGBA, GL_UNSIGNED_BYTE, kTexels};
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_Viewport, 0u, 480u - 8u, 8u, 8u),
            kAeeSuccess);
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_BindTexture, GL_TEXTURE_2D, 5u),
            kAeeSuccess);
  ASSERT_EQ(PedirComPilhaNaTabela(b, igles_slots::kIgles_TexImage2D, GL_TEXTURE_2D, 0u, GL_RGBA,
                                  pilha, 6u),
            kAeeSuccess);
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_Enable, GL_TEXTURE_2D), kAeeSuccess);
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_EnableClientState, GL_VERTEX_ARRAY),
            kAeeSuccess);
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_VertexPointer, 2u, GL_FLOAT, 8u, kV),
            kAeeSuccess);
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_EnableClientState, GL_TEXTURE_COORD_ARRAY),
            kAeeSuccess);
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_TexCoordPointer, 2u, GL_FLOAT, 8u, kT),
            kAeeSuccess);
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_Color4x, Fixo(1.0f), 0u, 0u, Fixo(1.0f)),
            kAeeSuccess);
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_DrawElements, GL_TRIANGLES, 6u,
                                 GL_UNSIGNED_SHORT, kI),
            kAeeSuccess)
      << b.Detalhe("IGLES11::DrawElements");
  EXPECT_GT(tela.Escritos(), 0u) << "o desenho com filtro LINEAR tem de escrever pixels";
  // Sem falta (era `filtro_de_textura_alem_de_GL_NEAREST`), com pressuposto nomeado.
  EXPECT_EQ(b.Faltas("filtro_de_textura_alem_de_GL_NEAREST"), 0u);
  const auto& p = b.traco.ContagemPressupostos();
  const auto it = p.find("filtro_de_textura_alem_de_GL_NEAREST");
  ASSERT_NE(it, p.end()) << "o filtro LINEAR tem de ficar DECLARADO como NEAREST";
  EXPECT_EQ(it->second, 1u) << "um pressuposto por corrida, nao um por desenho";
}

TEST(FrenteIgl9, TexCoordPointerComPonteiroNuloAceiteParaDesvinculacao) {
  BancoClasses b;
  // Chamada com ponteiro nulo (ridgeracer passa 2, GL_FLOAT, 0, 0 para desvincular coordenadas).
  ASSERT_EQ(PedirNaTabelaDoIgles(b, igles_slots::kIgles_TexCoordPointer, 2u, GL_FLOAT, 0u, 0u),
            kAeeSuccess)
      << b.Detalhe("IGLES11::TexCoordPointer");
}

}  // namespace
}  // namespace zb2::brew
