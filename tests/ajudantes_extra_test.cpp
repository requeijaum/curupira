// Testes dos ajudantes EXTRA do sistema (`AEEHelperFuncs`).
//
// O QUE ESTES TESTES TEM DE PROVAR, e nao so exercitar:
//
//  1. O CATALOGO E O DO CABECALHO. 117 slots, offsets de 4 em 4, e as quatro
//     que enganam no offset certo (`0x020` sprintf e nao `vsprintf`, `0x0d8`
//     strstr e nao `stristr`, `0x040` strtowstr e nao `wstrcompress`, `0x090`
//     atoi e nao `aee_GetRand`).
//  2. `strstr` E `stristr` SAO DIFERENTES, num caso em que ELAS DISCORDAM. Foi
//     este o erro que tres testes chamados `Strstr*` esconderam na arvore
//     antiga: todos testavam o `stristr`.
//  3. AS IMPLEMENTACOES FAZEM O QUE O SDK DIZ. Cada uma com o caso do caminho
//     principal e o caso do LIMITE (destino curto, ponteiro nulo, entrada
//     invalida, heap que nao soma).
//  4. AS GUARDAS RECUSAM. Cada guarda tem aqui a sua violacao deliberada: a
//     lista de blocos corrompida, o destino pequeno de mais, o ponteiro nulo.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/brew/ajudantes_extra.h"
#include "core/cpu/arm_interpreter.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"

using namespace zb2;
using namespace zb2::brew;

namespace {

constexpr std::uint32_t kHeapInicio = 0x80200000u;
constexpr std::uint32_t kHeapTamanho = 0x00100000u;
constexpr std::uint32_t kTexto = 0x80100000u;
constexpr std::uint32_t kTexto2 = 0x80101000u;

struct Bancada {
  Tempo tempo;
  Traco traco{"ajudantes_extra", &tempo};
  DestinoMemoria dm;
  Memoria mem{&traco};
  ArmInterpreter cpu{mem, &traco};
  Alocador alocador{mem, kHeapInicio, kHeapTamanho, &traco};
  AjudantesExtra extra{mem, alocador, traco};

  Bancada() {
    traco.JuntarDestino(&dm);
    mem.EscritorUnico("cpu");
  }

  void EscreverCadeia(std::uint32_t onde, const std::string& s) {
    for (std::size_t i = 0; i < s.size(); ++i) {
      mem.Escrever8(onde + static_cast<std::uint32_t>(i), static_cast<std::uint8_t>(s[i]));
    }
    mem.Escrever8(onde + static_cast<std::uint32_t>(s.size()), 0);
  }

  void EscreverLarga(std::uint32_t onde, const std::vector<std::uint16_t>& s) {
    for (std::size_t i = 0; i < s.size(); ++i) {
      mem.Escrever16(onde + static_cast<std::uint32_t>(i) * 2, s[i]);
    }
    mem.Escrever16(onde + static_cast<std::uint32_t>(s.size()) * 2, 0);
  }

  std::string LerCadeia(std::uint32_t onde, std::uint32_t max = 64) const {
    std::string s;
    for (std::uint32_t i = 0; i < max; ++i) {
      const std::uint8_t c = mem.Ler8(onde + i);
      if (c == 0) break;
      s.push_back(static_cast<char>(c));
    }
    return s;
  }

  // As faltas registadas com um dado nome. Devolve quantas vezes.
  std::uint64_t Faltas(const std::string& nome) const {
    const auto it = traco.ContagemFaltas().find(nome);
    return (it == traco.ContagemFaltas().end()) ? 0 : it->second;
  }

  // O detalhe da ULTIMA falta registada com aquele nome.
  //
  // `Traco::RegistarFalta` emite o evento com o nome `NAO_IMPLEMENTADO: <nome>`
  // (traco.cpp:122) e o motivo no detalhe: e por aqui que se verifica que a
  // recusa LEVA a razao, e nao so o nome.
  std::string DetalheDaFalta(const std::string& nome) const {
    std::string ultimo;
    for (const Evento& e : dm.eventos) {
      if (e.nome == "NAO_IMPLEMENTADO: " + nome) ultimo = e.detalhe;
    }
    return ultimo;
  }

  Atendimento Atender(std::uint32_t offset) { return extra.Atender(cpu, offset); }
};

// --- 1. o catalogo ---------------------------------------------------------
TEST(AjudantesExtra, CatalogoTemOs117DoCabecalho) {
  std::size_t quantos = 0;
  const Declaracao* c = CatalogoDosAjudantes(&quantos);
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(quantos, 117u);
  EXPECT_EQ(brew_ajudantes::kQuantos, 117u);
  for (std::size_t i = 0; i < quantos; ++i) {
    EXPECT_EQ(c[i].offset, 4u * i) << "slot " << i << " fora da ordem do cabecalho";
    ASSERT_NE(c[i].nome, nullptr);
    ASSERT_NE(c[i].assinatura, nullptr);
    EXPECT_GT(std::string(c[i].assinatura).size(), 8u) << c[i].nome;
    // A assinatura TEM de nomear o campo: e o texto da declaracao do cabecalho,
    // e nao uma etiqueta qualquer.
    EXPECT_NE(std::string(c[i].assinatura).find(c[i].nome), std::string::npos)
        << "assinatura de " << c[i].nome << ": " << c[i].assinatura;
    EXPECT_GT(c[i].linha, 0u);
  }
  EXPECT_EQ(c[0].offset, 0u);
  EXPECT_STREQ(c[0].nome, "memmove");
  EXPECT_EQ(c[116].offset, 0x1D0u);
  EXPECT_STREQ(c[116].nome, "GetALSContext");
}

TEST(AjudantesExtra, AsQuatroQueEnganamEstaoNoOffsetCerto) {
  // Cada linha: nome, offset MEDIDO no cabecalho, e o nome que NAO e ele.
  struct Caso {
    const char* nome;
    std::uint32_t offset;
    const char* nao_e;
  };
  const Caso casos[] = {
      {"sprintf", 0x020, "vsprintf"},          // o vsprintf e 0x13c
      {"vsprintf", 0x13C, "sprintf"},
      {"strstr", 0x0D8, "stristr"},            // o stristr e 0x0e8
      {"stristr", 0x0E8, "strstr"},
      {"strtowstr", 0x040, "wstrcompress"},    // o wstrcompress e 0x0a0
      {"wstrcompress", 0x0A0, "strtowstr"},
      {"atoi", 0x090, "aee_GetRand"},          // o aee_GetRand e 0x0a8
      {"aee_GetRand", 0x0A8, "atoi"},
  };
  for (const Caso& c : casos) {
    const char* nome = NomeDoAjudanteDoSdk(c.offset);
    ASSERT_NE(nome, nullptr) << "offset 0x" << std::hex << c.offset;
    EXPECT_STREQ(nome, c.nome) << "offset 0x" << std::hex << c.offset << " devia ser "
                               << c.nome << " e nao " << c.nao_e;
    EXPECT_STRNE(nome, c.nao_e);
  }
}

TEST(AjudantesExtra, OsTresDaDemandaTemNome) {
  // Os tres offsets que a lista de demanda da bateria pedia sem nome.
  EXPECT_STREQ(NomeDoAjudanteDoSdk(0x138), "GetRAMFree");
  EXPECT_STREQ(NomeDoAjudanteDoSdk(0x044), "wstrtostr");
  EXPECT_STREQ(NomeDoAjudanteDoSdk(0x050), "utf8towstr");
  // A assinatura vem do cabecalho, com a linha ao lado.
  const Declaracao* d = DeclaracaoDoOffset(0x138);
  ASSERT_NE(d, nullptr);
  EXPECT_EQ(d->linha, 179u);
  EXPECT_NE(std::string(d->assinatura).find("pdwTotal"), std::string::npos);
  EXPECT_NE(std::string(d->assinatura).find("pdwLargest"), std::string::npos);
}

TEST(AjudantesExtra, ForaDaTabelaNaoTemNome) {
  EXPECT_EQ(NomeDoAjudanteDoSdk(0x1D4), nullptr);  // o 118.o slot nao existe
  EXPECT_EQ(NomeDoAjudanteDoSdk(0x1D8), nullptr);
  EXPECT_EQ(NomeDoAjudanteDoSdk(0x13A), nullptr);  // nao alinhado a 4
  EXPECT_EQ(AssinaturaDoAjudante(0x1D4), nullptr);
}

// --- 2. wstrtostr (0x044) --------------------------------------------------
TEST(AjudantesExtra, WstrToStrConverteEAcabaEmNul) {
  Bancada b;
  b.EscreverLarga(kTexto, {'o', 'l', 'a'});
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, kTexto2);
  b.cpu.Set(kR2, 8);
  EXPECT_EQ(b.Atender(0x044), Atendimento::Implementado);
  EXPECT_EQ(b.cpu.Get(kR0), kTexto2);  // devolve o destino, como o SDK
  EXPECT_EQ(b.LerCadeia(kTexto2), "ola");
}

TEST(AjudantesExtra, WstrToStrTruncaComoTodoOSdk) {
  // A semantica e a do proprio SDK (OEMBREWSettings.c:363, OEMPDPSettings_common.h:592):
  // `nSize > 1` no laco, NUL no fim. Com nSize=2 cabe UM caracter.
  Bancada b;
  b.EscreverLarga(kTexto, {'a', 'b', 'c'});
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, kTexto2);
  b.cpu.Set(kR2, 2);
  b.mem.Escrever8(kTexto2 + 4, 0xAA);
  b.Atender(0x044);
  EXPECT_EQ(b.LerCadeia(kTexto2), "a");
  EXPECT_EQ(b.mem.Ler8(kTexto2 + 1), 0u);
  EXPECT_EQ(b.mem.Ler8(kTexto2 + 4), 0xAAu) << "escreveu fora do nSize";
}

TEST(AjudantesExtra, WstrToStrComNSizeZeroNaoConverte) {
  // Cabecalho: "If this is 0, this function does not do any conversion, but
  // returns pszDest unchanged". E nao e falta: e o contrato.
  Bancada b;
  b.EscreverLarga(kTexto, {'a'});
  b.mem.Escrever8(kTexto2, 0x7A);
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, kTexto2);
  b.cpu.Set(kR2, 0);
  EXPECT_EQ(b.Atender(0x044), Atendimento::Implementado);
  EXPECT_EQ(b.cpu.Get(kR0), kTexto2);
  EXPECT_EQ(b.mem.Ler8(kTexto2), 0x7Au) << "nSize=0 nao pode escrever no destino";
  EXPECT_EQ(b.Faltas("AEEHelperFuncs[0x044] wstrtostr"), 0u);
}

TEST(AjudantesExtra, WstrToStrComPonteiroNuloRecusaERegista) {
  Bancada b;
  b.cpu.Set(kR0, 0);  // pIn nulo
  b.cpu.Set(kR1, kTexto2);
  b.cpu.Set(kR2, 8);
  EXPECT_EQ(b.Atender(0x044), Atendimento::Implementado);
  EXPECT_EQ(b.cpu.Get(kR0), 0u);
  EXPECT_EQ(b.Faltas("AEEHelperFuncs[0x044] wstrtostr"), 1u);
}

// --- 3. utf8towstr (0x050) -------------------------------------------------
TEST(AjudantesExtra, Utf8ToWstrConverteAsciiEMultibyte) {
  Bancada b;
  // "c" + U+00E7 (0xC3 0xA7) + "o" -- o multibyte de 2 bytes.
  const std::string entrada = std::string("c") + "\xC3\xA7" + "o";
  b.EscreverCadeia(kTexto, entrada);
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, static_cast<std::uint32_t>(entrada.size()));
  b.cpu.Set(kR2, kTexto2);
  b.cpu.Set(kR3, 16);
  EXPECT_EQ(b.Atender(0x050), Atendimento::Implementado);
  EXPECT_EQ(b.cpu.Get(kR0), 1u);
  EXPECT_EQ(b.mem.Ler16(kTexto2), static_cast<std::uint16_t>('c'));
  EXPECT_EQ(b.mem.Ler16(kTexto2 + 2), 0x00E7u);
  EXPECT_EQ(b.mem.Ler16(kTexto2 + 4), static_cast<std::uint16_t>('o'));
  EXPECT_EQ(b.mem.Ler16(kTexto2 + 6), 0u);
}

TEST(AjudantesExtra, Utf8ToWstrEscreveParDeSubstitutos) {
  // U+1F600 (0xF0 0x9F 0x98 0x80) -> D83D DE00 em UTF-16.
  Bancada b;
  const std::string entrada = "\xF0\x9F\x98\x80";
  b.EscreverCadeia(kTexto, entrada);
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, static_cast<std::uint32_t>(entrada.size()));
  b.cpu.Set(kR2, kTexto2);
  b.cpu.Set(kR3, 8);
  b.Atender(0x050);
  EXPECT_EQ(b.cpu.Get(kR0), 1u);
  EXPECT_EQ(b.mem.Ler16(kTexto2), 0xD83Du);
  EXPECT_EQ(b.mem.Ler16(kTexto2 + 2), 0xDE00u);
  EXPECT_EQ(b.mem.Ler16(kTexto2 + 4), 0u);
}

TEST(AjudantesExtra, Utf8ToWstrRecusaQuandoNaoCabeENaoEscreveFora) {
  // A guarda: nao cabe -> FALSE e REGISTO. E a violacao deliberada do limite --
  // o destino tem 4 bytes (2 AECHAR: um caracter e o NUL) e a entrada tem tres.
  Bancada b;
  b.EscreverCadeia(kTexto, "abc");
  for (std::uint32_t i = 0; i < 8; ++i) b.mem.Escrever8(kTexto2 + i, 0xAA);
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, 3);
  b.cpu.Set(kR2, kTexto2);
  b.cpu.Set(kR3, 4);
  EXPECT_EQ(b.Atender(0x050), Atendimento::Implementado);
  EXPECT_EQ(b.cpu.Get(kR0), 0u) << "nao cabe tem de ser FALSE";
  EXPECT_EQ(b.mem.Ler16(kTexto2), static_cast<std::uint16_t>('a'));
  EXPECT_EQ(b.mem.Ler16(kTexto2 + 2), 0u) << "o que coube fica terminado em NUL";
  for (std::uint32_t i = 4; i < 8; ++i) {
    EXPECT_EQ(b.mem.Ler8(kTexto2 + i), 0xAAu) << "escreveu depois do fim do destino";
  }
  EXPECT_EQ(b.Faltas("AEEHelperFuncs[0x050] utf8towstr"), 1u);
}

TEST(AjudantesExtra, Utf8ToWstrRecusaEntradaInvalida) {
  // 0x80 sozinho nao e UTF-8 valido (e um byte de continuacao sem cabeca).
  Bancada b;
  b.mem.Escrever8(kTexto, 0x80);
  b.mem.Escrever8(kTexto + 1, 0);
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, 1);
  b.cpu.Set(kR2, kTexto2);
  b.cpu.Set(kR3, 16);
  EXPECT_EQ(b.Atender(0x050), Atendimento::Implementado);
  EXPECT_EQ(b.cpu.Get(kR0), 0u);
  EXPECT_EQ(b.Faltas("AEEHelperFuncs[0x050] utf8towstr"), 1u);
  // CURTO DEMAIS tambem e invalido: "C0 80" seria NUL em UTF-8 sem normalizacao,
  // e aceita-lo esconderia o fim da cadeia.
  b.EscreverCadeia(kTexto, std::string("\xC0\x80"));
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, 2);
  EXPECT_EQ(b.Atender(0x050), Atendimento::Implementado);
  EXPECT_EQ(b.cpu.Get(kR0), 0u);
  EXPECT_EQ(b.Faltas("AEEHelperFuncs[0x050] utf8towstr"), 2u);
}

TEST(AjudantesExtra, Utf8ToWstrRecusaPonteiroNuloENSizeZero) {
  Bancada b;
  b.cpu.Set(kR0, 0);
  b.cpu.Set(kR1, 4);
  b.cpu.Set(kR2, kTexto2);
  b.cpu.Set(kR3, 16);
  b.Atender(0x050);
  EXPECT_EQ(b.cpu.Get(kR0), 0u);
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR2, kTexto2);
  b.cpu.Set(kR3, 0);
  b.Atender(0x050);
  EXPECT_EQ(b.cpu.Get(kR0), 0u);
  EXPECT_EQ(b.Faltas("AEEHelperFuncs[0x050] utf8towstr"), 2u);
}

// --- 4. GetRAMFree (0x138) -------------------------------------------------
TEST(AjudantesExtra, GetRamFreeNumHeapNovoDaOsTresNumeros) {
  Bancada b;
  b.cpu.Set(kR0, kTexto);    // pdwTotal
  b.cpu.Set(kR1, kTexto2);   // pdwLargest
  EXPECT_EQ(b.Atender(0x138), Atendimento::Implementado);
  // O heap nasce com UM bloco livre de `tamanho` bytes, com 16 de cabecalho.
  EXPECT_EQ(b.mem.Ler32(kTexto), kHeapTamanho);
  EXPECT_EQ(b.mem.Ler32(kTexto2), kHeapTamanho - 16);
  EXPECT_EQ(b.cpu.Get(kR0), kHeapTamanho - 16);
}

TEST(AjudantesExtra, GetRamFreeOMaiorReportadoCabeMesmo) {
  // A PROVA de que o numero medido e o maior PEDIDO que o `malloc` aceita: um
  // relatorio que nao se pode verificar por uma alocacao a seguir seria um
  // numero bonito e sem consequencia.
  Bancada b;
  b.cpu.Set(kR0, 0);
  b.cpu.Set(kR1, 0);
  b.Atender(0x138);
  const std::uint32_t maior = b.cpu.Get(kR0);
  EXPECT_NE(b.alocador.Malloc(maior), 0u) << "o maior reportado tinha de caber";
  EXPECT_EQ(b.alocador.Malloc(1), 0u) << "depois de encher, nao pode caber mais nada";
}

TEST(AjudantesExtra, GetRamFreeDepoisDeAlocarDesceNoTamanhoCerto) {
  Bancada b;
  // O `malloc` de 100 bytes ocupa `precisa` = (100 + 16 + 7) & ~7 = 120 bytes de
  // BLOCO. O numero 120 nao foi inventado aqui: e o que esta em
  // `Alocador::Malloc` (`precisa`). O bloco livre que sobra tem os seus proprios
  // 16 bytes de cabecalho, logo o que sobra para o `malloc` ENTREGAR e
  // `tamanho - 120 - 16 = tamanho - 136`. **Este 16 e a medida que o teste
  // descobriu**, e nao uma expectativa de quem escreveu a implementacao.
  const std::uint32_t p = b.alocador.Malloc(100);
  ASSERT_NE(p, 0u);
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, kTexto2);
  b.Atender(0x138);
  EXPECT_EQ(b.mem.Ler32(kTexto), kHeapTamanho);
  EXPECT_EQ(b.mem.Ler32(kTexto2), kHeapTamanho - 136);
  EXPECT_EQ(b.cpu.Get(kR0), kHeapTamanho - 136);
  b.alocador.Free(p);
  b.Atender(0x138);
  EXPECT_EQ(b.cpu.Get(kR0), kHeapTamanho - 16) << "depois do free volta ao inicio";
}

TEST(AjudantesExtra, GetRamFreeRecusaQuandoOHeapNaoSoma) {
  // A VIOLACAO DELIBERADA da guarda da travessia: um cabecalho de bloco
  // corrompido. A soma dos blocos deixa de dar o tamanho do heap, e a medicao
  // falha. O que se espera NAO e um numero: e ZERO e um registo.
  Bancada b;
  b.mem.Escrever32(kHeapInicio, kHeapTamanho - 8);  // o ultimo bloco fica a faltar
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, kTexto2);
  EXPECT_EQ(b.Atender(0x138), Atendimento::Implementado);
  EXPECT_EQ(b.cpu.Get(kR0), 0u) << "uma travessia que nao soma nao pode dar um numero";
  EXPECT_EQ(b.mem.Ler32(kTexto), 0u);
  EXPECT_EQ(b.mem.Ler32(kTexto2), 0u);
  EXPECT_EQ(b.Faltas("AEEHelperFuncs[0x138] GetRAMFree"), 1u);
  EXPECT_NE(b.DetalheDaFalta("AEEHelperFuncs[0x138] GetRAMFree").find("NAO fecha"),
            std::string::npos);
}

TEST(AjudantesExtra, GetRamFreeComPonteirosNulosSoDevolveONumero) {
  // E assim que os DOIS pedidos do corpus aparecem: r0=0 e r1=0. O contrato
  // permite os dois nulos, e nao pode escrever-se em 0.
  Bancada b;
  b.cpu.Set(kR0, 0);
  b.cpu.Set(kR1, 0);
  b.Atender(0x138);
  EXPECT_EQ(b.cpu.Get(kR0), kHeapTamanho - 16);
  EXPECT_EQ(b.Faltas("AEEHelperFuncs[0x138] GetRAMFree"), 0u);
}

// --- 5. strstr (0x0d8) e stristr (0x0e8) -----------------------------------
TEST(AjudantesExtra, StrstrEStristrDiscordamNumCasoDeCaixa) {
  // ESTE TESTE E O QUE IMPEDE A TROCA. Na arvore antiga
  // `kStrstrSlotOffset` guardava o offset do `stristr`, e tres testes chamados
  // `Strstr*` testavam o `stristr` -- nenhum deles podia falhar por isso.
  Bancada b;
  // "abc" com a agulha "B": as duas TEM de discordar (o 'B' maiusculo nao esta
  // em "abc", mas o 'b' esta).
  b.EscreverCadeia(kTexto, "abc");
  b.EscreverCadeia(kTexto2, "B");

  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, kTexto2);
  b.Atender(0x0D8);  // strstr: sensivel a caixa -> NULL
  EXPECT_EQ(b.cpu.Get(kR0), 0u) << "strstr tem de ser sensivel a caixa";

  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, kTexto2);
  b.Atender(0x0E8);  // stristr: insensivel -> encontra o 'b' no indice 1
  EXPECT_EQ(b.cpu.Get(kR0), kTexto + 1) << "stristr tem de ser insensivel a caixa";

  // E com a caixa certa as duas concordam, o que prova que a diferenca acima nao
  // era "uma delas nao encontra nada".
  b.EscreverCadeia(kTexto2, "b");
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, kTexto2);
  b.Atender(0x0D8);
  EXPECT_EQ(b.cpu.Get(kR0), kTexto + 1);
}

TEST(AjudantesExtra, StrstrAgulhaVaziaECadeiaNaoEncontrada) {
  Bancada b;
  b.EscreverCadeia(kTexto, "abc");
  b.EscreverCadeia(kTexto2, "");
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, kTexto2);
  b.Atender(0x0D8);
  EXPECT_EQ(b.cpu.Get(kR0), kTexto) << "agulha vazia casa no inicio, como na libc";

  b.EscreverCadeia(kTexto2, "zz");
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, kTexto2);
  b.Atender(0x0D8);
  EXPECT_EQ(b.cpu.Get(kR0), 0u) << "nao encontrado devolve NULL";

  b.cpu.Set(kR1, 0);  // agulha nula
  b.Atender(0x0D8);
  EXPECT_EQ(b.cpu.Get(kR0), 0u);
}

// --- 6. a recusa (P2) ------------------------------------------------------
TEST(AjudantesExtra, OffsetDoCatalogoSemImplementacaoRecusaERegista) {
  Bancada b;
  // 0x12c = swapl, declarado no catalogo e sem implementacao honesta aqui.
  ASSERT_STREQ(NomeDoAjudanteDoSdk(0x12C), "swapl");
  b.cpu.Set(kR0, 0x12345678u);
  b.cpu.Set(kR1, 0);
  b.cpu.Set(kR2, 0);
  EXPECT_EQ(b.Atender(0x12C), Atendimento::Recusado);
  EXPECT_EQ(b.cpu.Get(kR0), static_cast<std::uint32_t>(kAeeUnsupported));
  EXPECT_EQ(b.Faltas("AEEHelperFuncs[0x12c] swapl"), 1u);
  EXPECT_EQ(b.extra.RecusasRegistadas(), 1u);
  // O detalhe dos registos e o MESMO texto do despacho: a lista de demanda
  // agrupa por ele, e mudar o formato aqui mudava a lista toda.
  EXPECT_NE(b.DetalheDaFalta("AEEHelperFuncs[0x12c] swapl").find("r0=0x12345678"), std::string::npos);
  EXPECT_NE(b.DetalheDaFalta("AEEHelperFuncs[0x12c] swapl").find("r1=0x00000000"), std::string::npos);
  EXPECT_NE(b.DetalheDaFalta("AEEHelperFuncs[0x12c] swapl").find("r2=0x00000000"), std::string::npos);
}

TEST(AjudantesExtra, OffsetForaDaTabelaNaoERecusadoAqui) {
  // Fora do catalogo, o despacho e que decide -- UM so ponto de recusa para o
  // que nao e desta tabela.
  Bancada b;
  EXPECT_EQ(b.Atender(0x1D4), Atendimento::Fora_Da_Tabela);
  EXPECT_EQ(b.Atender(0x13A), Atendimento::Fora_Da_Tabela);
  EXPECT_TRUE(b.traco.ContagemFaltas().empty());
}

TEST(AjudantesExtra, ImplementadosSaoCincoEATodosNoCatalogo) {
  EXPECT_EQ(AjudantesExtra::Implementados(), 5u);
  for (std::uint32_t off : {0x0D8u, 0x044u, 0x050u, 0x0E8u, 0x138u}) {
    EXPECT_NE(DeclaracaoDoOffset(off), nullptr) << "0x" << std::hex << off;
  }
}

TEST(AjudantesExtra, OContratoDoGanchoEDeUmaLinha) {
  // `AtenderAjudanteExtra` e o que o despacho chama. Devolve FALSE so quando o
  // offset nao e de nenhum dos 117 slot.
  Bancada b;
  b.EscreverCadeia(kTexto, "abc");
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, kTexto);
  b.cpu.Set(kR2, 0);
  EXPECT_TRUE(AtenderAjudanteExtra(b.cpu, b.mem, b.alocador, b.traco, 0x0D8));
  EXPECT_TRUE(AtenderAjudanteExtra(b.cpu, b.mem, b.alocador, b.traco, 0x12C));
  EXPECT_FALSE(AtenderAjudanteExtra(b.cpu, b.mem, b.alocador, b.traco, 0x1D4));
}

}  // namespace
