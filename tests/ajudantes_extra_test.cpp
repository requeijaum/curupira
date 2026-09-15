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

  // Escreve uma cadeia LARGA (AECHAR) a partir de texto ASCII, com o
  // terminador -- o que o `EscreverLarga` faz com uma lista de unidades.
  void EscreverLargaAscii(std::uint32_t onde, const std::string& s) {
    for (std::size_t i = 0; i < s.size(); ++i) {
      mem.Escrever16(onde + static_cast<std::uint32_t>(i) * 2,
                     static_cast<std::uint16_t>(static_cast<unsigned char>(s[i])));
    }
    mem.Escrever16(onde + static_cast<std::uint32_t>(s.size()) * 2, 0);
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

  // Le uma cadeia LARGA (AECHAR, 2 bytes por unidade) e devolve-a em UTF-8,
  // para os testes do WSTRLEN/WSTRNCOPYN poderem comparar com texto.
  std::string LerLarga(std::uint32_t onde, std::uint32_t max = 64) const {
    std::string s;
    for (std::uint32_t i = 0; i < max; ++i) {
      const std::uint16_t c = mem.Ler16(onde + i * 2);
      if (c == 0) break;
      s.push_back(static_cast<char>(c & 0xFFu));
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

TEST(AjudantesExtra, ImplementadosSaoDezoitoEATodosNoCatalogo) {
  // 7 da etapa anterior + os 6 da frente io2 (wstrlen, wstrncopyn, strtoul,
  // snprintf, strlcpy, strlcat) + o stricmp (0x0d0) da frente park + os 4 da
  // frente ajud2 (atoi, strends, aee_GetTimeMS, wsprintf).
  EXPECT_EQ(AjudantesExtra::Implementados(), 18u);
  for (std::uint32_t off : {0x0D8u, 0x044u, 0x050u, 0x0E8u, 0x138u, 0x0F4u, 0x0CCu, 0x0D0u,
                            0x090u, 0x0FCu, 0x0ACu, 0x03Cu}) {
    EXPECT_NE(DeclaracaoDoOffset(off), nullptr) << "0x" << std::hex << off;
  }
}

TEST(AjudantesExtra, StrdupCopiaComNulEDaPonteiroNovo) {
  Bancada b;
  b.EscreverCadeia(kTexto, "ola");
  b.cpu.Set(kR0, kTexto);
  EXPECT_EQ(b.Atender(0x0F4), Atendimento::Implementado);
  const std::uint32_t p = b.cpu.Get(kR0);
  EXPECT_NE(p, 0u);
  EXPECT_NE(p, kTexto);
  EXPECT_EQ(b.LerCadeia(p), "ola");
  EXPECT_EQ(b.mem.Ler8(p + 3), 0u);
  // Segunda chamada devolve endereco diferente com mesmo conteudo: e copia.
  b.cpu.Set(kR0, kTexto);
  b.Atender(0x0F4);
  const std::uint32_t p2 = b.cpu.Get(kR0);
  EXPECT_NE(p2, 0u);
  EXPECT_NE(p2, p);
  EXPECT_EQ(b.LerCadeia(p2), "ola");
}

TEST(AjudantesExtra, StrdupVaziaDaPonteiroValidoComNul) {
  Bancada b;
  b.EscreverCadeia(kTexto, "");
  b.cpu.Set(kR0, kTexto);
  EXPECT_EQ(b.Atender(0x0F4), Atendimento::Implementado);
  const std::uint32_t p = b.cpu.Get(kR0);
  EXPECT_NE(p, 0u);
  EXPECT_EQ(b.mem.Ler8(p), 0u);
}

TEST(AjudantesExtra, StrdupNulaRecusaERegista) {
  Bancada b;
  b.cpu.Set(kR0, 0);
  EXPECT_EQ(b.Atender(0x0F4), Atendimento::Implementado);
  EXPECT_EQ(b.cpu.Get(kR0), 0u);
  EXPECT_EQ(b.Faltas("AEEHelperFuncs[0x0f4] strdup"), 1u);
}

TEST(AjudantesExtra, StrdupSemMemoriaRecusaERegista) {
  Bancada b;
  b.EscreverCadeia(kTexto, "abc");
  // Enche o heap: aloca o maior bloco possivel (heap - cabecalho).
  // Depois disto nao cabe mais nada, como prova GetRamFreeOMaiorReportadoCabeMesmo.
  const std::uint32_t grande = b.alocador.Malloc(kHeapTamanho - 16);
  ASSERT_NE(grande, 0u);
  b.cpu.Set(kR0, kTexto);
  EXPECT_EQ(b.Atender(0x0F4), Atendimento::Implementado);
  EXPECT_EQ(b.cpu.Get(kR0), 0u);
  EXPECT_EQ(b.Faltas("AEEHelperFuncs[0x0f4] strdup"), 1u);
  EXPECT_NE(b.DetalheDaFalta("AEEHelperFuncs[0x0f4] strdup").find("sem memoria"),
            std::string::npos);
}

TEST(AjudantesExtra, StrncmpComparaAteNComNul) {
  Bancada b;
  b.EscreverCadeia(kTexto, "abc");
  b.EscreverCadeia(kTexto2, "abd");
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, kTexto2);
  b.cpu.Set(kR2, 2);
  EXPECT_EQ(b.Atender(0x0CC), Atendimento::Implementado);
  EXPECT_EQ(b.cpu.Get(kR0), 0u) << "2 primeiros iguais";
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, kTexto2);
  b.cpu.Set(kR2, 3);
  b.Atender(0x0CC);
  EXPECT_NE(b.cpu.Get(kR0), 0u) << "c != d no terceiro";
  EXPECT_LT(static_cast<std::int32_t>(b.cpu.Get(kR0)), 0) << "c < d";
  // n=0: iguais.
  b.cpu.Set(kR2, 0);
  b.Atender(0x0CC);
  EXPECT_EQ(b.cpu.Get(kR0), 0u);
  // NUL trava: "ab" vs "ab\0x01..." com n grande.
  b.EscreverCadeia(kTexto2, "ab");
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, kTexto2);
  b.cpu.Set(kR2, 16);
  b.Atender(0x0CC);
  EXPECT_NE(b.cpu.Get(kR0), 0u) << "abc vs ab: NUL != c";
}

TEST(AjudantesExtra, StrncmpNulaRecusaERegista) {
  Bancada b;
  b.cpu.Set(kR0, 0);
  b.cpu.Set(kR1, kTexto);
  b.cpu.Set(kR2, 4);
  EXPECT_EQ(b.Atender(0x0CC), Atendimento::Implementado);
  EXPECT_EQ(b.cpu.Get(kR0), 0u);
  EXPECT_EQ(b.Faltas("AEEHelperFuncs[0x0cc] strncmp"), 1u);
}

// 0x0D0 -- stricmp, CASO-INSENSITIVO (o teste que o distingue do strcmp).
//
// O QUE O SDK DIZ: `int (*stricmp)(const char *a, const char *b)`
// (AEEStdLib.h linha 150, via `tools/ajudantes_slots.inc`). A ordem de
// comparacao e a do strcmp, com o alfabeto ASCII dobrado para minusculas
// (`A`..`Z` -> `a`..`z`) antes de comparar o byte. Devolve 0 quando as
// cadeias igualam sem caixa; <0 quando `a` vem antes; >0 quando vem depois.
TEST(AjudantesExtra, StricmpComparaSemCaixaEDevolveSinal) {
  Bancada b;
  b.EscreverCadeia(kTexto, "AbC");
  b.EscreverCadeia(kTexto2, "aBc");
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, kTexto2);
  EXPECT_EQ(b.Atender(0x0D0), Atendimento::Implementado);
  EXPECT_EQ(b.cpu.Get(kR0), 0u) << "AbC == aBc sem caixa";
  // A caixa e o QUE DISTINGUE o stricmp do strcmp: o strcmp daria != 0 aqui.
  b.EscreverCadeia(kTexto, "abc");
  b.EscreverCadeia(kTexto2, "abd");
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, kTexto2);
  b.Atender(0x0D0);
  EXPECT_LT(static_cast<std::int32_t>(b.cpu.Get(kR0)), 0) << "c < d";
  b.EscreverCadeia(kTexto, "abd");
  b.EscreverCadeia(kTexto2, "abc");
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, kTexto2);
  b.Atender(0x0D0);
  EXPECT_GT(static_cast<std::int32_t>(b.cpu.Get(kR0)), 0) << "d > c";
  // O NUL trava como no strcmp: "ab" nunca iguala "abc".
  b.EscreverCadeia(kTexto, "ab");
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, kTexto2);
  b.Atender(0x0D0);
  EXPECT_NE(b.cpu.Get(kR0), 0u) << "ab vs abc: NUL != c";
  // Igualdade exacta com terminador no mesmo byte.
  b.EscreverCadeia(kTexto2, "ab");
  b.cpu.Set(kR0, kTexto);  // o r0 anterior ficou com o -99 do caso acima
  b.Atender(0x0D0);
  EXPECT_EQ(b.cpu.Get(kR0), 0u) << "ab == ab";
}

TEST(AjudantesExtra, StricmpDistinguidoDoStrcmpPelaCaixa) {
  // O strcmp (0x010, servido no despacho) e CASE-SENSITIVE; o stricmp nao.
  // O caso em que os dois DISCORDAM e o teste do stricmp.
  Bancada b;
  b.EscreverCadeia(kTexto, "Level");
  b.EscreverCadeia(kTexto2, "level");
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, kTexto2);
  EXPECT_EQ(b.Atender(0x0D0), Atendimento::Implementado);
  EXPECT_EQ(b.cpu.Get(kR0), 0u) << "stricmp iguala Level com level";
}

TEST(AjudantesExtra, StricmpNulaRecusaERegista) {
  Bancada b;
  b.cpu.Set(kR0, 0);
  b.cpu.Set(kR1, kTexto);
  EXPECT_EQ(b.Atender(0x0D0), Atendimento::Implementado);
  EXPECT_EQ(b.cpu.Get(kR0), 0u);
  EXPECT_EQ(b.Faltas("AEEHelperFuncs[0x0d0] stricmp"), 1u);
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


// ---------------------------------------------------------------------------
// A FRENTE io2, os seis ajudantes demandados (corrida_fmg.json): wstrlen
// (0x030, allstarcards), wstrncopyn (0x080, chessbots), strtoul (0x0c4,
// alpineracerex 392x), snprintf (0x144), strlcpy (0x14c) e strlcat (0x150) --
// estes tres ultimos no allstarcards.
//
// As assinaturas vem do `tools/ajudantes_slots.inc` (gerado do AEEStdLib.h):
//   0x030  int  (*wstrlen)(const AECHAR *p)
//   0x080  int  (*wstrncopyn)(AECHAR *pszDest, int cbDest, const AECHAR *pszSource, int lenSource)
//   0x0c4  uint32 (*strtoul)(const char *nptr, char **endptr, int base)
//   0x144  int32 (*snprintf)(char *buf, uint32 f, const char *format, ...)
//   0x14c  size_t (*strlcpy)(char *dst, const char *src, size_t nSize)
//   0x150  size_t (*strlcat)(char *dst, const char *src, size_t nSize)
// ---------------------------------------------------------------------------

TEST(AjudantesExtra, WstrlenContaAECHARsENaoBytes) {
  Bancada b;
  // 6 AECHARs = 12 bytes (a latina ocupa 2 bytes). Um byte a mais nao pode
  // entrar na conta: o SDK diz "the number of AECHAR characters".
  b.EscreverLarga(kTexto, std::vector<std::uint16_t>{'Z', 'e', 'e', 'b', 'o', 0x00e9});
  b.cpu.Set(kR0, kTexto);
  EXPECT_EQ(b.Atender(0x030), Atendimento::Implementado);
  EXPECT_EQ(b.cpu.Get(kR0), 6u);
  // Vazia (so o NUL): 0.
  b.EscreverLarga(kTexto, {});
  b.cpu.Set(kR0, kTexto);
  b.Atender(0x030);
  EXPECT_EQ(b.cpu.Get(kR0), 0u);
}

TEST(AjudantesExtra, WstrncopynCopiaAteLenSourceETerminaSempre) {
  Bancada b;
  b.EscreverLarga(kTexto2, std::vector<std::uint16_t>{'0', '1', '2', '3', '4', '5', '6', '7', '8', '9'});
  // lenSource < comprimento: copia 4 e termina.
  b.cpu.Set(kR0, kTexto);   // pDest
  b.cpu.Set(kR1, 8);        // cbDest (AECHARs de capacidade)
  b.cpu.Set(kR2, kTexto2);  // pSrc
  b.cpu.Set(kR3, 4);        // lenSource
  EXPECT_EQ(b.Atender(0x080), Atendimento::Implementado);
  EXPECT_EQ(b.cpu.Get(kR0), 4u) << "4 AECHAR copiados";
  EXPECT_EQ(b.LerLarga(kTexto, 8), "0123");
  // lenSource = -1: copia a cadeia inteira (o SDK diz exactamente isto),
  // com o destino a dar espaco (cbDest=16).
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, 16);
  b.cpu.Set(kR3, -1);
  b.Atender(0x080);
  EXPECT_EQ(b.LerLarga(kTexto, 16), "0123456789");
  EXPECT_EQ(b.cpu.Get(kR0), 10u);
  // cbDest curto: nunca transborda -- a terminação NUL cabe SEMPRE no destino.
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, 3);
  b.cpu.Set(kR3, -1);
  b.Atender(0x080);
  EXPECT_EQ(b.LerLarga(kTexto, 3), "01");
}

TEST(AjudantesExtra, StrtoulConverteEDevolveOndeParou) {
  Bancada b;
  constexpr std::uint32_t kEnd = 0x80103000u;
  b.EscreverCadeia(kTexto, "123abc");
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, kEnd);
  b.cpu.Set(kR2, 10);
  EXPECT_EQ(b.Atender(0x0C4), Atendimento::Implementado);
  EXPECT_EQ(b.cpu.Get(kR0), 123u);
  EXPECT_EQ(b.mem.Ler32(kEnd), kTexto + 3) << "endptr aponta para o 'a'";
  // Base 16 com prefixo 0x, base 0 a detectar o prefixo.
  b.EscreverCadeia(kTexto, "0x1Fff");
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, 0);  // endptr nulo
  b.cpu.Set(kR2, 0);
  b.Atender(0x0C4);
  EXPECT_EQ(b.cpu.Get(kR0), 0x1FFFu);
  // Sem conversao: 0 e endptr = nptr (o SDK diz exactamente isto).
  b.EscreverCadeia(kTexto, "xyz");
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, kEnd);
  b.cpu.Set(kR2, 10);
  b.Atender(0x0C4);
  EXPECT_EQ(b.cpu.Get(kR0), 0u);
  EXPECT_EQ(b.mem.Ler32(kEnd), kTexto);
}

TEST(AjudantesExtra, SnprintfFormataComLimiteEVarargs) {
  Bancada b;
  // O MEDIDO no allstarcards: snprintf(buf, 32, "udata/settings%d.dat", n).
  b.EscreverCadeia(kTexto2, "udata/settings%d.dat");
  b.cpu.Set(kR0, kTexto);   // buf
  b.cpu.Set(kR1, 32);       // f
  b.cpu.Set(kR2, kTexto2);  // format
  b.cpu.Set(kR3, 3);        // primeiro vararg
  EXPECT_EQ(b.Atender(0x144), Atendimento::Implementado);
  EXPECT_EQ(b.LerCadeia(kTexto), "udata/settings3.dat");
  // O limite corta e termina: f=8 escreve 8 caracteres + NUL (o contrato
  // de `limite` do Formatar: "sem o terminador").
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, 8);
  b.cpu.Set(kR2, kTexto2);
  b.cpu.Set(kR3, 3);
  b.Atender(0x144);
  EXPECT_EQ(b.LerCadeia(kTexto), "udata/se");
}

TEST(AjudantesExtra, StrlcpyCopiaTerminaEdevolveOComprimentoTentado) {
  Bancada b;
  b.EscreverCadeia(kTexto2, "abcdefghij");
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, kTexto2);
  b.cpu.Set(kR2, 5);
  EXPECT_EQ(b.Atender(0x14C), Atendimento::Implementado);
  EXPECT_EQ(b.cpu.Get(kR0), 10u) << "o comprimento TENTADO (10)";
  EXPECT_EQ(b.LerCadeia(kTexto), "abcd") << "5 bytes: 4 + NUL";
  // Destino curto de mais nao transborda nunca.
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR2, 3);
  b.Atender(0x14C);
  EXPECT_EQ(b.LerCadeia(kTexto), "ab");
}

TEST(AjudantesExtra, StrlcatConcatenaComLimite) {
  Bancada b;
  b.EscreverCadeia(kTexto, "ab");
  b.EscreverCadeia(kTexto2, "cdef");
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, kTexto2);
  b.cpu.Set(kR2, 8);
  EXPECT_EQ(b.Atender(0x150), Atendimento::Implementado);
  EXPECT_EQ(b.cpu.Get(kR0), 6u) << "2 + 4 = 6 (o comprimento TENTADO)";
  EXPECT_EQ(b.LerCadeia(kTexto), "abcdef");
  // Limite que nao sobra para tudo: "ab" + "cdef" com nSize=5 => "abcd".
  b.EscreverCadeia(kTexto, "ab");
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR2, 5);
  b.Atender(0x150);
  EXPECT_EQ(b.LerCadeia(kTexto), "abcd");
}


// ---------------------------------------------------------------------------
// FRENTE ajud2 -- os ajudantes que faltavam por DEMANDA MEDIDA.
//
// Cada um destes testes existe por uma razao dita: o `atoi` porque a libc tem
// regras que se escrevem mal de cabeca; o `strends` porque a ORDEM dos
// argumentos e ao contrario do que o nome sugere; o `wsprintf` porque `nSize` e
// em BYTES e `%s` come um AECHAR*; o `aee_GetTimeMS` porque a resposta e um
// valor DECLARADO e tem de aparecer como tal.
// ---------------------------------------------------------------------------

TEST(AjudantesExtra, AtoiConverteComoALibcEIgnoraOResto) {
  // O cabecalho: "a wrapper around the atoi() function provided by the standard
  // C library. Its behavior is identical to that of atoi()" (AEEStdLib.h:2390).
  Bancada b;
  b.EscreverCadeia(kTexto, "  -42xyz");
  b.cpu.Set(kR0, kTexto);
  EXPECT_EQ(b.Atender(0x090), Atendimento::Implementado);
  EXPECT_EQ(static_cast<std::int32_t>(b.cpu.Get(kR0)), -42) << "espacos, sinal, e para no 'x'";
  EXPECT_EQ(b.Faltas("AEEHelperFuncs[0x090] atoi"), 0u);

  // Sem digitos nenhuns: ZERO e o ponteiro NAO se move (nao ha endptr aqui).
  b.EscreverCadeia(kTexto, "xyz");
  b.cpu.Set(kR0, kTexto);
  b.Atender(0x090);
  EXPECT_EQ(b.cpu.Get(kR0), 0u);

  // Os dois extremos exactos da representacao.
  b.EscreverCadeia(kTexto, "2147483647");
  b.cpu.Set(kR0, kTexto);
  b.Atender(0x090);
  EXPECT_EQ(static_cast<std::int32_t>(b.cpu.Get(kR0)), 2147483647);
  b.EscreverCadeia(kTexto, "-2147483648");
  b.cpu.Set(kR0, kTexto);
  b.Atender(0x090);
  EXPECT_EQ(static_cast<std::int32_t>(b.cpu.Get(kR0)), INT32_MIN)
      << "o tecto depende do sinal: um tecto unico saturado este valor";
}

TEST(AjudantesExtra, AtoiSaturaEDeclaraOPressuposto) {
  // O transbordo nao tem definicao no SDK (na libc e indefinido). O que NAO se
  // pode e inventar um numero em silencio: satura-se E declara-se.
  Bancada b;
  b.EscreverCadeia(kTexto, "99999999999999999999");
  b.cpu.Set(kR0, kTexto);
  EXPECT_EQ(b.Atender(0x090), Atendimento::Implementado);
  EXPECT_EQ(static_cast<std::int32_t>(b.cpu.Get(kR0)), INT32_MAX);
  const auto& p = b.traco.ContagemPressupostos();
  const auto it = p.find("atoi_saturado");
  ASSERT_NE(it, p.end()) << "a saturacao tem de ficar declarada";
  EXPECT_EQ(it->second, 1u);
}

TEST(AjudantesExtra, AtoiComPonteiroNuloRecusa) {
  Bancada b;
  b.cpu.Set(kR0, 0);
  EXPECT_EQ(b.Atender(0x090), Atendimento::Implementado);
  EXPECT_EQ(b.cpu.Get(kR0), 0u);
  EXPECT_EQ(b.Faltas("AEEHelperFuncs[0x090] atoi"), 1u);
}

TEST(AjudantesExtra, StrendsLeOSufixoPrimeiro) {
  // `boolean (*strends)(const char *cpszSuffic, const char *psz)` -- o pedaco
  // vem PRIMEIRO (AEEStdLib.h:160 e :2798). Trocar a ordem e o erro facil.
  Bancada b;
  b.EscreverCadeia(kTexto, "dat");           // sufixo
  b.EscreverCadeia(kTexto2, "settings.dat"); // cadeia
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, kTexto2);
  EXPECT_EQ(b.Atender(0x0FC), Atendimento::Implementado);
  EXPECT_EQ(b.cpu.Get(kR0), 1u) << "settings.dat acaba em dat";

  // A ORDEM TROCADA da FALSE: e este o caso que fixa a assinatura.
  b.cpu.Set(kR0, kTexto2);
  b.cpu.Set(kR1, kTexto);
  b.Atender(0x0FC);
  EXPECT_EQ(b.cpu.Get(kR0), 0u) << "'dat' nao acaba em 'settings.dat'";

  // Um sufixo mais comprido do que a cadeia nunca cabe no fim dela.
  b.EscreverCadeia(kTexto, "settings.dat");
  b.EscreverCadeia(kTexto2, "dat");
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, kTexto2);
  b.Atender(0x0FC);
  EXPECT_EQ(b.cpu.Get(kR0), 0u);

  // Maiusculas contam: o SDK so tem variante insensivel para o strbegins
  // (STRIBEGINS, AEEStdLib.h:333), e nao para o strends.
  b.EscreverCadeia(kTexto, "DAT");
  b.EscreverCadeia(kTexto2, "settings.dat");
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, kTexto2);
  b.Atender(0x0FC);
  EXPECT_EQ(b.cpu.Get(kR0), 0u);
  EXPECT_EQ(b.Faltas("AEEHelperFuncs[0x0fc] strends"), 0u) << "nada disto e falta";
}

TEST(AjudantesExtra, StrendsComSufixoVazioENulo) {
  Bancada b;
  // O sufixo vazio e o fim de qualquer cadeia (e o que `ends_with("")` faz).
  b.EscreverCadeia(kTexto, "");
  b.EscreverCadeia(kTexto2, "abc");
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, kTexto2);
  b.Atender(0x0FC);
  EXPECT_EQ(b.cpu.Get(kR0), 1u);
  // Duas cadeias vazias: uma acaba na outra.
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, kTexto);
  b.Atender(0x0FC);
  EXPECT_EQ(b.cpu.Get(kR0), 1u);
  // Ponteiro nulo: FALSE e a falta registada, com o nome do slot.
  b.cpu.Set(kR0, 0);
  b.Atender(0x0FC);
  EXPECT_EQ(b.cpu.Get(kR0), 0u);
  EXPECT_EQ(b.Faltas("AEEHelperFuncs[0x0fc] strends"), 1u);
}

TEST(AjudantesExtra, AeeGetTimeMSDeclaraAMeiaNoite) {
  // `GETTIMEMS` e um relogio de CALENDARIO (AEEStdLib.h:4783). O aparelho
  // emulado nao adquiriu hora, logo o valor e DECLARADO, e nao medido -- e o
  // caminho do `Traco` para isso e o pressuposto, nao a falta.
  Bancada b;
  EXPECT_EQ(b.Atender(0x0AC), Atendimento::Implementado);
  EXPECT_EQ(b.cpu.Get(kR0), 0u);
  EXPECT_EQ(b.Faltas("AEEHelperFuncs[0x0ac] aee_GetTimeMS"), 0u);
  const auto& p = b.traco.ContagemPressupostos();
  const auto it = p.find("aee_GetTimeMS_meia_noite");
  ASSERT_NE(it, p.end()) << "um valor nao medido tem de aparecer no registo";
  EXPECT_EQ(it->second, 1u);
}

TEST(AjudantesExtra, WsprintfFormataLargoComLimiteEmBytes) {
  // O medido no tekken2: r0=dest, r1=0x40 (64 BYTES = 32 AECHARs), r2=formato.
  // `%s` come um AECHAR* (o exemplo do SDK, c_SystemTaskApp.c:3555+3643, passa
  // `AECHAR[32]` a um formato com %s).
  Bancada b;
  constexpr std::uint32_t kPilha = 0x80104000u;
  constexpr std::uint32_t kLargo = 0x80105000u;
  b.EscreverLargaAscii(kLargo, "abc");
  b.EscreverLargaAscii(kTexto2, "%s-%d!");
  b.cpu.Set(kSP, kPilha);
  b.cpu.Set(kR0, kTexto);   // pDest
  b.cpu.Set(kR1, 64);       // nSize, em BYTES
  b.cpu.Set(kR2, kTexto2);  // pFormat (AECHAR)
  b.cpu.Set(kR3, kLargo);   // primeiro vararg
  b.mem.Escrever32(kPilha, 7);
  EXPECT_EQ(b.Atender(0x03C), Atendimento::Implementado);
  EXPECT_EQ(b.LerLarga(kTexto, 32), "abc-7!");
  EXPECT_EQ(b.mem.Ler16(kTexto + 6 * 2), 0u) << "terminador";
  EXPECT_EQ(b.Faltas("AEEHelperFuncs[0x03c] wsprintf"), 0u);
}

TEST(AjudantesExtra, WsprintfTruncaDentroDoBufferENaoEscreveFora) {
  Bancada b;
  constexpr std::uint32_t kPilha = 0x80104000u;
  constexpr std::uint32_t kLargo = 0x80105000u;
  // O buffer todo com 0x4141 antes: o que nao for escrito fica provado.
  for (std::uint32_t k = 0; k < 8; ++k) b.mem.Escrever16(kTexto + k * 2, 0x4141);
  b.EscreverLargaAscii(kLargo, "abcdef");
  b.EscreverLargaAscii(kTexto2, "%s");
  b.cpu.Set(kSP, kPilha);
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, 8);  // 8 BYTES = 4 AECHARs => 3 caracteres + terminador
  b.cpu.Set(kR2, kTexto2);
  b.cpu.Set(kR3, kLargo);
  b.Atender(0x03C);
  EXPECT_EQ(b.LerLarga(kTexto, 8), "abc");
  EXPECT_EQ(b.mem.Ler16(kTexto + 3 * 2), 0u) << "o terminador cabe DENTRO dos 8 bytes";
  EXPECT_EQ(b.mem.Ler16(kTexto + 4 * 2), 0x4141u) << "nada foi escrito fora do limite";
}

TEST(AjudantesExtra, WsprintfComPontoFlutuanteNaoEscreveNada) {
  // Regra do cabecalho (AEEStdLib.h:3324): "If %f is found anywhere within the
  // format string, this function returns immediately without doing any
  // processing". Nem o que vinha ANTES do %f se escreve.
  Bancada b;
  for (std::uint32_t k = 0; k < 8; ++k) b.mem.Escrever16(kTexto + k * 2, 0x4141);
  b.EscreverLargaAscii(kTexto2, "X%fY");
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, 64);
  b.cpu.Set(kR2, kTexto2);
  EXPECT_EQ(b.Atender(0x03C), Atendimento::Implementado);
  for (std::uint32_t k = 0; k < 8; ++k) {
    EXPECT_EQ(b.mem.Ler16(kTexto + k * 2), 0x4141u) << "unidade " << k << " mexida";
  }
  EXPECT_EQ(b.Faltas("AEEHelperFuncs[0x03c] wsprintf"), 0u)
      << "nao e falta: e o que o cabecalho manda fazer";
}

TEST(AjudantesExtra, WsprintfEspecificadorDesconhecidoEscreveEregistra) {
  Bancada b;
  b.EscreverLargaAscii(kTexto2, "a%qb");
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, 64);
  b.cpu.Set(kR2, kTexto2);
  EXPECT_EQ(b.Atender(0x03C), Atendimento::Implementado);
  EXPECT_EQ(b.LerLarga(kTexto, 8), "a%qb") << "escreve-se como veio";
  EXPECT_EQ(b.Faltas("AEEHelperFuncs[0x03c] wsprintf"), 1u) << "e registado (P2)";
  EXPECT_NE(b.DetalheDaFalta("AEEHelperFuncs[0x03c] wsprintf").find("nao suportado"),
            std::string::npos);
}

TEST(AjudantesExtra, WsprintfComNSizeZeroRecusaESenaoEscreve) {
  Bancada b;
  b.mem.Escrever16(kTexto, 0x4141);
  b.EscreverLargaAscii(kTexto2, "abc");
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, 1);  // 1 byte: nem um AECHAR inteiro cabe
  b.cpu.Set(kR2, kTexto2);
  EXPECT_EQ(b.Atender(0x03C), Atendimento::Implementado);
  EXPECT_EQ(b.mem.Ler16(kTexto), 0x4141u);
  EXPECT_EQ(b.Faltas("AEEHelperFuncs[0x03c] wsprintf"), 1u);
}

TEST(AjudantesExtra, SetupNativeImageNaoEAtendidoPorEstaTabela) {
  // 0x064 e `SetupNativeImage` -- o `CONVERTBMP` do SDK (AEEStdLib.h:88 e :384)
  // e NAO um ajudante de texto: devolve um buffer de imagem, e o que ele
  // precisa (o descodificador e um bitmap na faixa de saida) esta FORA desta
  // tabela. Este teste existe para que a proxima frente nao o tome por um
  // `atoi` maior, e para que a recusa continue NOMEADA.
  EXPECT_EQ(brew_ajudantes::kAjudante_SetupNativeImage, 0x064u);
  ASSERT_NE(DeclaracaoDoOffset(0x064), nullptr);
  EXPECT_STREQ(DeclaracaoDoOffset(0x064)->nome, "SetupNativeImage");
  EXPECT_STREQ(DeclaracaoDoOffset(0x064)->assinatura,
               "void *(*SetupNativeImage)(AEECLSID cls, void *pBuffer, AEEImageInfo *pii, "
               "boolean *pbRealloc)");
  Bancada b;
  b.cpu.Set(kR0, 0x01004001u);  // AEECLSID_WINBMP, o que o CONVERTBMP passa
  EXPECT_EQ(b.Atender(0x064), Atendimento::Recusado);
  EXPECT_EQ(b.Faltas("AEEHelperFuncs[0x064] SetupNativeImage"), 1u);
  EXPECT_EQ(b.cpu.Get(kR0), kAeeUnsupported);
}


}  // namespace
