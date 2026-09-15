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

TEST(RegistoGl, GetStringRecusaEmVezDeInventar) {
  Banco b;
  // A string de vendor/renderer e uma AFIRMACAO SOBRE O HARDWARE. Sem uma
  // medicao do que a maquina responde, inventa-la seria uma linha de log que nao
  // pode ser verdadeira (P7).
  EXPECT_EQ(b.igl.Executar(kIgl_GetString, Args(GL_VENDOR), nullptr), ResultadoGl::Recusado);
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

  // Viewport(0, 0, 8, 8): o quarto argumento real (a altura) vem da PILHA.
  b.mem.Escrever32(kPilhaDoTeste, 8u);
  b.cpu.Set(kR0, kObjetoIgles);
  b.cpu.Set(kR1, 0u);
  b.cpu.Set(kR2, 0u);
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

}  // namespace
}  // namespace zb2::brew