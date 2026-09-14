#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "core/brew/ajudantes.h"
#include "core/carga/mod.h"
#include "core/cpu/arm_interpreter.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"

using namespace zb2;

namespace {

constexpr std::uint32_t kTabela = 0x80010000u;
constexpr std::uint32_t kHeapInicio = 0x80200000u;
constexpr std::uint32_t kHeapTamanho = 0x00100000u;

// Uma bancada com CPU, memoria, alocador, tabela e faixa de saida -- o minimo
// para um modulo poder falar com o sistema.
struct Bancada {
  Tempo tempo;
  Traco traco{"teste", &tempo};
  DestinoMemoria dm;
  Memoria mem{&traco};
  ArmInterpreter cpu{mem, &traco};
  Alocador alocador{mem, kHeapInicio, kHeapTamanho, &traco};
  TabelaDeAjudantes tabela{mem, alocador, &traco};
  std::uint64_t malloc_chamado = 0;
  std::uint64_t free_chamado = 0;
  std::vector<std::uint32_t> tamanhos_pedidos;

  Bancada() {
    traco.JuntarDestino(&dm);
    mem.EscritorUnico("cpu");
    Saidas s;
    s.base = 0xF0000000u;
    s.passo = 4;
    s.quantos = 200;
    s.ativa = true;
    cpu.ConfigurarSaidas(s);
    // Os dois slots que o modulo pede primeiro. Os offsets vem do SDK
    // (`AEEStdLib.h`, 0x68 = malloc, 0x6c = free) e foram confirmados no
    // desmonte do `imicro3d.mod` em 0x00100724.
    tabela.Declarar(0x68, "malloc");
    tabela.Declarar(0x6c, "free");
    mem.Escrever32(kTabela + 0x68, cpu.GetSaidas().Endereco(0));
    mem.Escrever32(kTabela + 0x6c, cpu.GetSaidas().Endereco(1));
  }

  // Corre o modulo ate a saida `limite`, despachando as saidas.
  std::uint64_t Correr(std::uint64_t limite) {
    std::uint64_t total = 0;
    while (total < limite) {
      const std::uint32_t pc = cpu.Get(kPC);
      if (pc == kSentinela()) return total;
      std::uint32_t idx = 0;
      if (cpu.GetSaidas().Contem(pc, &idx)) {
        const std::uint32_t lr = cpu.Get(kLR);
        if (idx == 0) {
          ++malloc_chamado;
          tamanhos_pedidos.push_back(cpu.Get(kR0));
          cpu.Set(kR0, alocador.Malloc(cpu.Get(kR0)));
        } else if (idx == 1) {
          ++free_chamado;
          alocador.Free(cpu.Get(kR0));
          cpu.Set(kR0, kAeeSuccess);
        } else {
          cpu.Set(kR0, kAeeUnsupported);
        }
        cpu.Set(kPC, lr);
        continue;
      }
      cpu.Passo();
      ++total;
    }
    return total;
  }

  static constexpr std::uint32_t kSentinela() { return 0xFFFFFFF0u; }
};

// Um modulo sintetico que faz o que o modulo real faz na entrada, mas escrito
// por nos: le a tabela de `base-4`, pede `nSize + 16` ao malloc, guarda o
// ponteiro e retorna. E a forma minima da convencao ROPI.
//
// Codificado AQUI como palavras, com o comentario de cada uma -- nao ha
// montador externo, e o teste fica legivel sem ferramenta nenhuma.
std::vector<std::uint32_t> ModuloSintetico() {
  return {
      0xE92D4010u,  // push {r4, lr}
      0xE24DD008u,  // sub  sp, sp, #8
      0xE1A04002u,  // mov  r4, r2            ; r4 = ppMod
      // O `ldr` esta na 4.a palavra (base+12); o seu PC vale base+20, e o
      // literal esta na 17.a (base+64). Logo o offset e 64-20 = 44 = 0x2c.
      // Com 24 (a primeira versao) ele lia base+44 -- dentro do proprio codigo.
      0xE59F002Cu,  // ldr  r0, [pc, #44]     ; o literal no fim
      0xE08F0000u,  // add  r0, pc, r0
      0xE5100004u,  // ldr  r0, [r0, #-4]     ; <<< a tabela, em base-4
      0xE5901068u,  // ldr  r1, [r0, #104]    ; slot 0x68 = malloc
      0xE28FE008u,  // add  lr, pc, #8        ; retorno
      0xE3A00020u,  // mov  r0, #32           ; nSize + 16
      0xE12FFF31u,  // blx  r1                ; chama o sistema
      // A condicao estava INVERTIDA na primeira versao (`streq` em vez de
      // `strne`): com o malloc a devolver um ponteiro nao nulo, `streq` nao
      // corria e o ponteiro nunca era guardado.
      0xE3500000u,  // cmp   r0, #0
      0x15840000u,  // strne r0, [r4]      ; guarda o ponteiro, se houver
      0x13A00000u,  // movne r0, #0        ; sucesso
      0x03A00001u,  // moveq r0, #1        ; falha
      0xE28DD008u,  // add  sp, sp, #8
      0xE8BD8010u,  // pop  {r4, pc}
      // O literal tem de dar `r0 = base` depois do `add r0, pc, r0`, para que o
      // `ldr r0, [r0, #-4]` seguinte leia mesmo `base-4`. Calculo: o `add` esta
      // em base+16, logo o seu PC vale base+24; queremos r0 = base, entao o
      // literal e base - (base+24) = -24 = 0xFFFFFFE8. Errei este numero na
      // primeira escrita e o modulo sintetico nao chegava a chamar o sistema.
      0xFFFFFFE8u,  // literal: -24, para o `add r0, pc, r0` dar base
  };
}

std::vector<std::uint8_t> LerBytes(const std::string& caminho, bool* ok) {
  std::ifstream f(caminho, std::ios::binary);
  if (!f) { *ok = false; return {}; }
  std::vector<std::uint8_t> v((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
  *ok = !v.empty();
  return v;
}

}  // namespace

// ===========================================================================
// A convencao ROPI: a tabela esta em `base - 4`
// ===========================================================================

TEST(Carga, OCarregadorEscreveATabelaEmBaseMenosQuatro) {
  // Esta e a convencao que o `AEEMod_Load` do `imicro3d.mod` usa, medida por
  // desmonte em 0x00100710:
  //     ldr r0, [pc, #120] / add r0, pc, r0 / ldr r0, [r0, #-4]
  // Sem o ponteiro escrito em `base-4`, o modulo le zero e salta para zero.
  Memoria mem;
  const std::vector<std::uint8_t> imagem = {1, 2, 3, 4, 5, 6, 7, 8};
  const auto r = CarregarMod(mem, imagem, 0x00100000u, 0x80010000u, nullptr);
  ASSERT_TRUE(r.ok) << r.motivo;
  EXPECT_EQ(mem.Ler32(0x00100000u - 4), 0x80010000u);
  EXPECT_EQ(mem.Ler8(0x00100000u), 1);
  EXPECT_EQ(mem.Ler8(0x00100000u + 7), 8);
  EXPECT_EQ(r.ponto_de_entrada, 0x00100000u);
}

TEST(Carga, BaseDemasiadoBaixaERecusada) {
  // Com base < 4 nao ha sitio para o ponteiro da ROPI. Recusar e melhor do que
  // escrever em `0xffffffff` e seguir.
  Memoria mem;
  const std::vector<std::uint8_t> imagem = {1, 2, 3, 4};
  const auto r = CarregarMod(mem, imagem, 2, 0x80010000u, nullptr);
  EXPECT_FALSE(r.ok);
  EXPECT_FALSE(r.motivo.empty());
}

TEST(Carga, ImagemVaziaERecusada) {
  Memoria mem;
  const auto r = CarregarMod(mem, {}, 0x00100000u, 0, nullptr);
  EXPECT_FALSE(r.ok);
}

// ===========================================================================
// O modulo sintetico: a forma minima da convencao
// ===========================================================================

TEST(Carga, ModuloSinteticoLeATabelaEDevolvePonteiro) {
  Bancada b;
  std::vector<std::uint8_t> bytes;
  for (std::uint32_t p : ModuloSintetico()) {
    bytes.push_back(static_cast<std::uint8_t>(p & 0xFF));
    bytes.push_back(static_cast<std::uint8_t>((p >> 8) & 0xFF));
    bytes.push_back(static_cast<std::uint8_t>((p >> 16) & 0xFF));
    bytes.push_back(static_cast<std::uint8_t>((p >> 24) & 0xFF));
  }
  constexpr std::uint32_t kBase = 0x00100000u;
  constexpr std::uint32_t kPPMod = 0x00090000u;
  ASSERT_TRUE(CarregarMod(b.mem, bytes, kBase, kTabela, &b.traco).ok);
  b.cpu.Repor(kBase, 0x80080000u);
  b.cpu.Set(kR0, 0);
  b.cpu.Set(kR2, kPPMod);
  b.cpu.Set(kLR, Bancada::kSentinela());

  b.Correr(200);

  EXPECT_EQ(b.malloc_chamado, 1u) << "o modulo pediu exactamente um bloco";
  ASSERT_EQ(b.tamanhos_pedidos.size(), 1u);
  EXPECT_EQ(b.tamanhos_pedidos[0], 32u) << "nSize + sizeof(IModuleVtbl)";
  const std::uint32_t ponteiro = b.mem.Ler32(kPPMod);
  EXPECT_NE(ponteiro, 0u) << "o ponteiro do modulo e a saida da chamada";
  EXPECT_EQ(ponteiro, kHeapInicio + 16u) << "primeiro bloco: apos o cabecalho";
}

TEST(Carga, SemATabelaOModuloSaltaParaZero) {
  // O contraste que prova o mecanismo: com a tabela a zero, o `blx` do modulo
  // cai em 0. E o que a primeira medicao do `imicro3d` real mostrou.
  Bancada b;
  std::vector<std::uint8_t> bytes;
  for (std::uint32_t p : ModuloSintetico()) {
    bytes.push_back(static_cast<std::uint8_t>(p & 0xFF));
    bytes.push_back(static_cast<std::uint8_t>((p >> 8) & 0xFF));
    bytes.push_back(static_cast<std::uint8_t>((p >> 16) & 0xFF));
    bytes.push_back(static_cast<std::uint8_t>((p >> 24) & 0xFF));
  }
  constexpr std::uint32_t kBase = 0x00100000u;
  ASSERT_TRUE(CarregarMod(b.mem, bytes, kBase, /*tabela=*/0, &b.traco).ok);
  b.mem.Escrever32(kTabela + 0x68, 0);  // destabula o slot do malloc
  b.cpu.Repor(kBase, 0x80080000u);
  b.cpu.Set(kR2, 0x00090000u);
  b.cpu.Set(kLR, Bancada::kSentinela());
  for (int i = 0; i < 200; ++i) {
    if (b.cpu.Get(kPC) < 0x1000) break;  // saltou para perto de zero
    b.cpu.Passo();
  }
  EXPECT_LT(b.cpu.Get(kPC), 0x1000u) << "sem tabela, o blx cai em memoria nula";
}

// ===========================================================================
// A estrutura do modulo que o `AEEMod_Load` constroi
// ===========================================================================

TEST(CargaCorpus, AEEModLoadConstroiOModuloEOModuloTemVtable) {
  // MEDIDO no `imicro3d.mod` real, e o desmonte explica cada campo.
  //
  // `AEEStaticMod_New` (0x001006d4), que o `AEEMod_Load` chama, faz:
  //     10077c  stmib r0, {r1, r6, r7, r8}
  // ou seja escreve em module+4, +8, +12 e +16 os valores r1, r6, r7, r8.
  // r7 e r8 vem de `ldm r8, {r7, r8}` -- os argumentos 5 e 6 na PILHA do
  // chamador. E o proprio `AEEMod_Load` empurra zero para eles
  // (0x00100008: `mov r3,#0 / str r3,[sp] / str r3,[sp,#4]`).
  //
  // CONCLUSAO, e vale a pena escreve-la porque eu a li errada primeiro: um
  // `module+12` a zero DEPOIS do `AEEMod_Load` e o comportamento correcto, e
  // nao memoria por preencher. O `CreateInstance` do APPLET regista-se noutro
  // sitio, mais tarde. Foi preciso tracar as instrucoes para o ver: eu tinha
  // atribuido o zero a um argumento em falta da NOSSA chamada.
  const char* caminhos[] = {
      "/media/rafaelfrequiao/8C5F-19E51/zeebo/ROMs/debug_nand/mod/12875/imicro3d.mod",
      "/home/rafaelfrequiao/projects/zeebo-lab/games/brew/mod/12875/imicro3d.mod",
  };
  std::vector<std::uint8_t> imagem;
  bool ok = false;
  for (const char* c : caminhos) {
    imagem = LerBytes(c, &ok);
    if (ok) break;
  }
  if (!ok) GTEST_SKIP() << "corpus nao montado";

  Bancada b;
  constexpr std::uint32_t kBase = 0x00100000u;
  constexpr std::uint32_t kPPMod = 0x00090000u;
  constexpr std::uint32_t kShell = 0x81000000u;
  ASSERT_TRUE(CarregarMod(b.mem, imagem, kBase, kTabela, &b.traco).ok);
  b.cpu.Repor(kBase, 0x80080000u);
  b.cpu.Set(kR0, kShell);
  b.cpu.Set(kR2, kPPMod);
  b.cpu.Set(kLR, Bancada::kSentinela());
  b.Correr(10000);

  const std::uint32_t modulo = b.mem.Ler32(kPPMod);
  ASSERT_NE(modulo, 0u);
  const std::uint32_t vtable = b.mem.Ler32(modulo);
  EXPECT_NE(vtable, 0u) << "o primeiro campo do modulo e a vtable";
  EXPECT_EQ(b.mem.Ler32(modulo + 4), 1u) << "contagem de referencias comeca em 1";
  EXPECT_EQ(b.mem.Ler32(modulo + 8), kShell) << "o IShell que passamos foi guardado";
  EXPECT_EQ(b.mem.Ler32(modulo + 12), 0u)
      << "zero e o valor CERTO aqui: o proprio AEEMod_Load empurra zero para os "
         "argumentos 5 e 6, que sao os que alimentam +12 e +16";
  // A vtable do IModule tem 4 slots, e o slot 2 e `CreateInstance`.
  for (int i = 0; i < 4; ++i) {
    const std::uint32_t alvo = b.mem.Ler32(vtable + static_cast<std::uint32_t>(i) * 4);
    EXPECT_TRUE(alvo >= kBase && alvo < kBase + 90068u)
        << "slot " << i << " da vtable aponta para dentro do modulo";
  }
  EXPECT_EQ(b.cpu.InstruscoesRecusadas(), 0u);
}

// ===========================================================================
// O alocador
// ===========================================================================

TEST(Alocador, MallocDevolveBlocosDistintosEAlinhados) {
  Memoria mem;
  Alocador a(mem, kHeapInicio, kHeapTamanho, nullptr);
  const std::uint32_t p1 = a.Malloc(16);
  const std::uint32_t p2 = a.Malloc(16);
  EXPECT_NE(p1, 0u);
  EXPECT_NE(p2, 0u);
  EXPECT_NE(p1, p2);
  EXPECT_EQ(p1 & 7u, 0u) << "alinhado a 8";
  EXPECT_EQ(p2 & 7u, 0u);
  EXPECT_GE(p2, p1 + 16u) << "nao se sobrepoem";
}

TEST(Alocador, EscreverNumBlocoNaoTocaNoSeguinte) {
  Memoria mem;
  Alocador a(mem, kHeapInicio, kHeapTamanho, nullptr);
  const std::uint32_t p1 = a.Malloc(16);
  const std::uint32_t p2 = a.Malloc(16);
  mem.Escrever32(p2, 0xDEADBEEFu);
  for (int i = 0; i < 16; ++i) mem.Escrever8(p1 + static_cast<std::uint32_t>(i), 0xAA);
  EXPECT_EQ(mem.Ler32(p2), 0xDEADBEEFu) << "o bloco seguinte ficou intacto";
}

TEST(Alocador, FreeEJuntadoDevolvemOEspacoInteiro) {
  // Sem juntar vizinhos livres o heap fragmenta, e um modulo que aloca e
  // liberta em ciclo acaba sem memoria -- o sintoma mais caro de diagnosticar
  // depois.
  Memoria mem;
  Alocador a(mem, kHeapInicio, 0x1000u, nullptr);
  const std::uint32_t p1 = a.Malloc(0x200);
  const std::uint32_t p2 = a.Malloc(0x200);
  const std::uint32_t p3 = a.Malloc(0x200);
  ASSERT_NE(p3, 0u);
  a.Free(p1);
  a.Free(p2);
  a.Free(p3);
  const std::uint32_t grande = a.Malloc(0x800);
  EXPECT_NE(grande, 0u) << "depois de juntar, cabe um bloco grande";
}

TEST(Alocador, PedidoMaiorQueOHeapFalhaEConta) {
  Memoria mem;
  Alocador a(mem, kHeapInicio, 0x1000u, nullptr);
  EXPECT_EQ(a.Malloc(0x100000u), 0u);
  EXPECT_EQ(a.Falhas(), 1u);
}

TEST(Alocador, FreeDeEnderecoDeForaEContadoENaoCorrompe) {
  Memoria mem;
  Alocador a(mem, kHeapInicio, 0x1000u, nullptr);
  const std::uint32_t antes = a.Falhas();
  a.Free(0x12345678u);  // nao e nosso
  EXPECT_EQ(a.Falhas(), antes + 1) << "um free de fora e um defeito, e fica contado";
  EXPECT_EQ(a.Malloc(16), kHeapInicio + 16u) << "e o heap continua utilizavel";
}

TEST(Alocador, ReallocCresceECopiaOConteudo) {
  Memoria mem;
  Alocador a(mem, kHeapInicio, 0x10000u, nullptr);
  const std::uint32_t p = a.Malloc(16);
  for (int i = 0; i < 16; ++i) mem.Escrever8(p + static_cast<std::uint32_t>(i), static_cast<std::uint8_t>(i));
  const std::uint32_t maior = a.Realloc(p, 64);
  ASSERT_NE(maior, 0u);
  for (int i = 0; i < 16; ++i) {
    EXPECT_EQ(mem.Ler8(maior + static_cast<std::uint32_t>(i)), static_cast<std::uint8_t>(i))
        << "byte " << i << " sobreviveu ao realloc";
  }
}

// ===========================================================================
// A tabela recusa instalar-se incompleta -- principio P2
// ===========================================================================

TEST(Ajudantes, RecusaInstalarComSlotPorImplementar) {
  // Foi um stub que devolvia sucesso e nao fazia nada que escondeu 86 377
  // chamadas de `glCullFace` no projeto antigo. A tabela tem de RECUSAR.
  Memoria mem;
  Alocador a(mem, kHeapInicio, kHeapTamanho, nullptr);
  TabelaDeAjudantes t(mem, a, nullptr);
  t.Declarar(0x68, "malloc", [](ICpu& c) { c.Set(kR0, 0); });
  t.Declarar(0x6c, "free");  // declarado e NAO implementado
  EXPECT_EQ(t.Declarados(), 2u);
  EXPECT_EQ(t.Implementados(), 1u);
  const auto r = t.Instalar(kTabela);
  EXPECT_FALSE(r.ok) << "instalar incompleto tem de falhar";
  ASSERT_EQ(r.faltam.size(), 1u);
  EXPECT_EQ(r.faltam[0], "free");
}

TEST(Ajudantes, ComPermissaoInstalaEDizOQueFalta) {
  Memoria mem;
  Alocador a(mem, kHeapInicio, kHeapTamanho, nullptr);
  TabelaDeAjudantes t(mem, a, nullptr);
  t.Declarar(0x68, "malloc", [](ICpu& c) { c.Set(kR0, 42); });
  t.Declarar(0x6c, "free");
  const auto r = t.Instalar(kTabela, /*permitir_por_implementar=*/true);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(r.faltam.size(), 1u) << "mesmo aceitando, o que falta fica dito";
}

// ===========================================================================
// Integracao: o `imicro3d.mod` real -- salta quando o corpus nao esta montado
// ===========================================================================

TEST(CargaCorpus, IMicro3dCarregaEDevolvePonteiroDeModulo) {
  const char* caminhos[] = {
      "/media/rafaelfrequiao/8C5F-19E51/zeebo/ROMs/debug_nand/mod/12875/imicro3d.mod",
      "/home/rafaelfrequiao/projects/zeebo-lab/games/brew/mod/12875/imicro3d.mod",
  };
  std::vector<std::uint8_t> imagem;
  bool ok = false;
  for (const char* c : caminhos) {
    imagem = LerBytes(c, &ok);
    if (ok) break;
  }
  if (!ok) {
    GTEST_SKIP() << "corpus de 62 titulos nao esta montado nesta maquina";
  }

  Bancada b;
  constexpr std::uint32_t kBase = 0x00100000u;
  constexpr std::uint32_t kPPMod = 0x00090000u;
  ASSERT_TRUE(CarregarMod(b.mem, imagem, kBase, kTabela, &b.traco).ok);
  b.cpu.Repor(kBase, 0x80080000u);
  b.cpu.Set(kR0, 0x81000000u);  // o IShell -- ainda nao existe; o modulo so o guarda
  b.cpu.Set(kR2, kPPMod);
  b.cpu.Set(kLR, Bancada::kSentinela());

  const std::uint64_t passos = b.Correr(100000);

  // MEDIDO: 60 instrucoes, UMA chamada ao malloc de 36 bytes, ponteiro de
  // modulo nao nulo em 0x80200010. Os numeros ficam no teste para que uma
  // mudanca que os altere seja vista, e nao passada em claro.
  EXPECT_EQ(b.malloc_chamado, 1u) << "o modulo pede exactamente um bloco em AEEMod_Load";
  ASSERT_EQ(b.tamanhos_pedidos.size(), 1u);
  EXPECT_EQ(b.tamanhos_pedidos[0], 36u) << "nSize(20) + sizeof(IModuleVtbl)(16)";
  const std::uint32_t ponteiro = b.mem.Ler32(kPPMod);
  EXPECT_NE(ponteiro, 0u) << "o `AEEMod_Load` devolveu um ponteiro de modulo";
  EXPECT_EQ(b.cpu.InstruscoesRecusadas(), 0u)
      << "nenhuma instrucao recusada: opcode=0x" << std::hex << b.cpu.UltimaRecusada();
  EXPECT_LT(passos, 100u) << "e termina depressa, nao por esgotar o orcamento";
}
