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
#include <cstring>
#include <string>
#include <vector>

#include "core/brew/ajudantes_extra.h"
#include "core/brew/despacho.h"
#include "core/brew/formato.h"
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

TEST(AjudantesExtra, Utf8ToWstrEncheODestinoAteAoFimENaoEscreveFora) {
  // O LIMITE, com a regra MEDIDA no proprio SDK. O destino tem 4 bytes (2
  // AECHAR) e a entrada tem tres caracteres: cabem DOIS, e a conversao diz
  // FALSE sem escrever um byte para la dos 38 (aqui, dos 4).
  //
  // PORQUE NAO SE RESERVA O TERMINADOR: o chamador medido (a Z-Wheel,
  // `tectoy` 0x7af24) faz `len = strlen(psz)`, `malloc((len+1)*2)` e chama
  // `utf8towstr(psz, len, buf, len*2)` -- o `nSize` e EXACTAMENTE o numero de
  // unidades dos caracteres, e a unidade do terminador fica FORA dele. Com a
  // reserva, "http://www.ats.com/" perdia o ultimo caracter.
  Bancada b;
  b.EscreverCadeia(kTexto, "abc");
  for (std::uint32_t i = 0; i < 8; ++i) b.mem.Escrever8(kTexto2 + i, 0xAA);
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, 3);
  b.cpu.Set(kR2, kTexto2);
  b.cpu.Set(kR3, 4);
  EXPECT_EQ(b.Atender(0x050), Atendimento::Implementado);
  EXPECT_EQ(b.cpu.Get(kR0), 0u) << "nao cabe tudo tem de ser FALSE";
  EXPECT_EQ(b.mem.Ler16(kTexto2), static_cast<std::uint16_t>('a'));
  EXPECT_EQ(b.mem.Ler16(kTexto2 + 2), static_cast<std::uint16_t>('b'))
      << "o destino enche-se ate ao fim, e nao se reserva a unidade do NUL";
  for (std::uint32_t i = 4; i < 8; ++i) {
    EXPECT_EQ(b.mem.Ler8(kTexto2 + i), 0xAAu) << "escreveu depois do fim do destino";
  }
  EXPECT_EQ(b.Faltas("AEEHelperFuncs[0x050] utf8towstr"), 1u);
}

TEST(AjudantesExtra, Utf8ToWstrDaOCasoMedidoDaZWheel) {
  // O caso REAL, com os numeros do traco: `tectoy` (274755), a Z-Wheel, pede
  // `utf8towstr(r0="http://www.ats.com/", r1=19, r2=buf, r3=38)`.
  //
  // `r1=19` e o `strlen` (SEM o terminador) e `r3=38 = 2*19` e o numero de
  // unidades dos caracteres; o `malloc` do guest pede `(19+1)*2 = 40` bytes, e
  // a unidade do terminador (bytes 38-39) fica FORA do `nSize`.
  //
  // Com a reserva do terminador, esta chamada -- medida -- perdia o ultimo
  // caracter e dizia FALSE. O resultado certo tem os 19 caracteres todos.
  Bancada b;
  const std::string url = "http://www.ats.com/";
  ASSERT_EQ(url.size(), 19u);
  b.EscreverCadeia(kTexto, url);
  for (std::uint32_t i = 0; i < 8; ++i) b.mem.Escrever8(kTexto2 + 38 + i, 0xAA);
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, 19);
  b.cpu.Set(kR2, kTexto2);
  b.cpu.Set(kR3, 38);
  EXPECT_EQ(b.Atender(0x050), Atendimento::Implementado);
  EXPECT_EQ(b.cpu.Get(kR0), 1u) << "coube tudo: a chamada da Z-Wheel tem de dar TRUE";
  EXPECT_EQ(b.Faltas("AEEHelperFuncs[0x050] utf8towstr"), 0u);
  for (std::uint32_t i = 0; i < 19; ++i) {
    EXPECT_EQ(b.mem.Ler16(kTexto2 + i * 2), static_cast<std::uint16_t>(url[i]))
        << "caracter " << i;
  }
  for (std::uint32_t i = 38; i < 46; ++i) {
    EXPECT_EQ(b.mem.Ler8(kTexto2 + i), 0xAAu)
        << "o terminador NAO cabe no nSize: escreve-lo e escrever memoria do guest";
  }
}

TEST(AjudantesExtra, Utf8ToWstrEscreveOTerminadorQuandoEleCabe) {
  // A outra metade da mesma regra: quando SOBRA a unidade do terminador, ele e
  // escrito -- e o que deixa a cadeia larga do guest terminada.
  Bancada b;
  b.EscreverCadeia(kTexto, "abc");
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, 3);
  b.cpu.Set(kR2, kTexto2);
  b.cpu.Set(kR3, 8);
  EXPECT_EQ(b.Atender(0x050), Atendimento::Implementado);
  EXPECT_EQ(b.cpu.Get(kR0), 1u);
  EXPECT_EQ(b.mem.Ler16(kTexto2), static_cast<std::uint16_t>('a'));
  EXPECT_EQ(b.mem.Ler16(kTexto2 + 2), static_cast<std::uint16_t>('b'));
  EXPECT_EQ(b.mem.Ler16(kTexto2 + 4), static_cast<std::uint16_t>('c'));
  EXPECT_EQ(b.mem.Ler16(kTexto2 + 6), 0u) << "o terminador cabe no nSize de 8 bytes";
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

// --- 3b. wstrtoutf8 (0x054) ------------------------------------------------
TEST(AjudantesExtra, WstrToUtf8ConverteAsciiEAcimaDe0x80) {
  // Contrato do cabecalho (`AEEStdLib.h:84-85` e a doc do `aee_WStrToUTF8`,
  // `AEEStdLib_static.h:494-510`): r0=wide, r1=nLen em AECHARs, r2=destino de
  // BYTES, r3=bytes do destino. "FALSE if fails ( if pSrc or pDst is NULL; if
  // nSize is zero or lesser )".
  //
  // O `nLen` e em AECHARs e o `nSize` em BYTES -- a assimetria e do cabecalho, e
  // e o contrario do `utf8towstr`, onde o `nLen` e em BYTES.
  Bancada b;
  b.EscreverLarga(kTexto, {'c', 0x00E7, 'o'});  // "c" + U+00E7 + "o"
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, 3);
  b.cpu.Set(kR2, kTexto2);
  b.cpu.Set(kR3, 16);
  EXPECT_EQ(b.Atender(0x054), Atendimento::Implementado);
  EXPECT_EQ(b.cpu.Get(kR0), 1u);
  EXPECT_EQ(b.mem.Ler8(kTexto2), static_cast<std::uint8_t>('c'));
  EXPECT_EQ(b.mem.Ler8(kTexto2 + 1), 0xC3u);
  EXPECT_EQ(b.mem.Ler8(kTexto2 + 2), 0xA7u);
  EXPECT_EQ(b.mem.Ler8(kTexto2 + 3), static_cast<std::uint8_t>('o'));
}

TEST(AjudantesExtra, WstrToUtf8EscreveTresBytesParaAcimaDe0x800) {
  // O medido no texto do `pbc`: U+2122 (0x2122) e U+00AE (0x00AE) aparecem no
  // texto de creditos. A regra do SDK (`BREWSim`, `aee_WStrToUTF8`): 1 byte
  // abaixo de 0x80, 2 abaixo de 0x800, 3 acima -- e NUNCA 4.
  Bancada b;
  b.EscreverLarga(kTexto, {0x2122, 0x00AE});
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, 2);
  b.cpu.Set(kR2, kTexto2);
  b.cpu.Set(kR3, 16);
  EXPECT_EQ(b.Atender(0x054), Atendimento::Implementado);
  EXPECT_EQ(b.cpu.Get(kR0), 1u);
  EXPECT_EQ(b.mem.Ler8(kTexto2), 0xE2u);
  EXPECT_EQ(b.mem.Ler8(kTexto2 + 1), 0x84u);
  EXPECT_EQ(b.mem.Ler8(kTexto2 + 2), 0xA2u);
  EXPECT_EQ(b.mem.Ler8(kTexto2 + 3), 0xC2u);
  EXPECT_EQ(b.mem.Ler8(kTexto2 + 4), 0xAEu);
}

TEST(AjudantesExtra, WstrToUtf8NaoEscreveTerminadorQueNaoCabe) {
  // O caso medido da Z-Wheel (`tectoy`, 0x7673c): `len = wstrlen(wide)`,
  // `malloc(len*10 + 1)` e `wstrtoutf8(wide, len, buf, len*10)` -- o `nLen` e em
  // AECHARs, SEM o terminador, e o destino nao tem obrigacao de o levar.
  Bancada b;
  b.EscreverLarga(kTexto, {'o', 'l', 'a'});
  for (std::uint32_t i = 0; i < 8; ++i) b.mem.Escrever8(kTexto2 + 3 + i, 0xAA);
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, 3);
  b.cpu.Set(kR2, kTexto2);
  b.cpu.Set(kR3, 3);
  EXPECT_EQ(b.Atender(0x054), Atendimento::Implementado);
  EXPECT_EQ(b.cpu.Get(kR0), 1u) << "coube: o SDK so diz FALSE com ponteiro nulo ou nSize<=0";
  EXPECT_EQ(b.LerCadeia(kTexto2, 3), "ola");
  for (std::uint32_t i = 3; i < 11; ++i) {
    EXPECT_EQ(b.mem.Ler8(kTexto2 + i), 0xAAu) << "escreveu depois do fim do destino";
  }
}

TEST(AjudantesExtra, WstrToUtf8ParaQuandoODestinoNaoTemEspacoERegista) {
  // O SDK (`BREWSim`, `aee_WStrToUTF8`) PARA no caracter que nao cabe e devolve
  // TRUE -- o valor de retorno e o que o guest espera. A truncagem nao fica
  // muda (P2): vai para o registo da corrida.
  Bancada b;
  b.EscreverLarga(kTexto, {'a', 0x00E7, 'b'});
  for (std::uint32_t i = 0; i < 8; ++i) b.mem.Escrever8(kTexto2 + i, 0xAA);
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, 3);
  b.cpu.Set(kR2, kTexto2);
  b.cpu.Set(kR3, 1);
  EXPECT_EQ(b.Atender(0x054), Atendimento::Implementado);
  EXPECT_EQ(b.cpu.Get(kR0), 1u);
  EXPECT_EQ(b.mem.Ler8(kTexto2), static_cast<std::uint8_t>('a'));
  for (std::uint32_t i = 1; i < 8; ++i) {
    EXPECT_EQ(b.mem.Ler8(kTexto2 + i), 0xAAu) << "o caracter de 2 bytes nao cabe: nao se escreve";
  }
  EXPECT_EQ(b.Faltas("AEEHelperFuncs[0x054] wstrtoutf8"), 1u);
}

TEST(AjudantesExtra, WstrToUtf8RecusaPonteiroNuloENSizeZero) {
  Bancada b;
  b.EscreverLarga(kTexto, {'a'});
  b.cpu.Set(kR0, 0);
  b.cpu.Set(kR1, 1);
  b.cpu.Set(kR2, kTexto2);
  b.cpu.Set(kR3, 8);
  b.Atender(0x054);
  EXPECT_EQ(b.cpu.Get(kR0), 0u);
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR2, 0);
  b.Atender(0x054);
  EXPECT_EQ(b.cpu.Get(kR0), 0u);
  b.cpu.Set(kR2, kTexto2);
  b.cpu.Set(kR3, 0);
  b.Atender(0x054);
  EXPECT_EQ(b.cpu.Get(kR0), 0u);
  EXPECT_EQ(b.Faltas("AEEHelperFuncs[0x054] wstrtoutf8"), 3u);
}

TEST(AjudantesExtra, WstrToUtf8EUtf8ToWstrFazemIdaEVolta) {
  // A ida e a volta com o par de substitutos: o `wstrtoutf8` escreve 3 bytes por
  // unidade (o par fica em CESU-8, que e o que o SDK escreve) e o `utf8towstr`
  // le cada grupo de 3 bytes como uma unidade -- o par volta igual.
  Bancada b;
  b.EscreverLarga(kTexto, {0xD83D, 0xDE00});
  b.cpu.Set(kR0, kTexto);
  b.cpu.Set(kR1, 2);
  b.cpu.Set(kR2, kTexto2);
  b.cpu.Set(kR3, 16);
  b.Atender(0x054);
  EXPECT_EQ(b.mem.Ler8(kTexto2), 0xEDu);
  EXPECT_EQ(b.mem.Ler8(kTexto2 + 3), 0xEDu);
  b.cpu.Set(kR0, kTexto2);
  b.cpu.Set(kR1, 6);
  b.cpu.Set(kR2, kTexto + 0x100);
  b.cpu.Set(kR3, 8);
  b.Atender(0x050);
  EXPECT_EQ(b.cpu.Get(kR0), 1u);
  EXPECT_EQ(b.mem.Ler16(kTexto + 0x100), 0xD83Du);
  EXPECT_EQ(b.mem.Ler16(kTexto + 0x102), 0xDE00u);
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

TEST(AjudantesExtra, ImplementadosSaoVinteESeteEATodosNoCatalogo) {
  // 7 da etapa anterior + os 6 da frente io2 (wstrlen, wstrncopyn, strtoul,
  // snprintf, strlcpy, strlcat) + o stricmp (0x0d0) da frente park + os 4 da
  // frente ajud2 (atoi, strends, aee_GetTimeMS, wsprintf) + o memcmp (0x0dc),
  // que 8 titulos pediam e que estava na tabela do cabecalho SEM implementacao
  // + o SetupNativeImage (0x064) da frente setup, que e o `CONVERTBMP`
  // + os 3 da frente zhelp (wstrtoutf8 0x054, aee_GetSeconds 0x0b4,
  // aee_GetJulianDate 0x0b8). O `utf8towstr` (0x050) ja estava nesta lista: o
  // que a frente zhelp lhe mudou foi a REGRA do destino cheio, nao o offset.
  // + o `sysfree` (0x0bc), que o `allstarcards` pedia 3x e que estava na tabela
  // do cabecalho SEM implementacao (e o par do heap: liberta o buffer que o
  // `SetupNativeImage` entrega).
  // + o `swaps` (0x130), que o `gof` e o `pbc` pediam 1646 vezes e que estava na
  // tabela do cabecalho SEM implementacao. O nome engana: troca BYTES, nao valores.
  // + o `strlower` (0x114), que o `quake2brew` pede -- e que so ficou alcancavel
  // depois de a bandeira do `malloc` ser servida (`fe1eaad`).
  // + o `strexpand` (0x0e4), que o `ddragonz` pede 1500 vezes.
  // + o `memstr` (0x0ec), que o `peggle` pede 303 vezes -- era a falta MAIS
  // PEDIDA da corrida dos 62, e o mesmo titulo tinha a segunda
  // (`IImageDecoder::GetBitmap`, 242).
  EXPECT_EQ(AjudantesExtra::Implementados(), 28u);
  for (std::uint32_t off : {0x0D8u, 0x044u, 0x050u, 0x054u, 0x0E8u, 0x138u, 0x0F4u, 0x0CCu,
                            0x0D0u, 0x090u, 0x0FCu, 0x0ACu, 0x0B4u, 0x0B8u, 0x03Cu, 0x0DCu,
                            0x0BCu, 0x130u, 0x114u, 0x0E4u, 0x0ECu}) {
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

// --- 3c. aee_GetSeconds (0x0b4) e aee_GetJulianDate (0x0b8) -----------------
TEST(AjudantesExtra, AeeGetSecondsDeclaraAEpocaDe1980) {
  // `uint32 (*aee_GetSeconds)(void)` (`AEEStdLib.h:137`). Doc (`AEEStdLib.h:4853`):
  // "seconds since 1980/01/06 00:00:00 UTC, incluindo os ajustes de segundo
  // intercalar e AJUSTADO ao fuso local e a hora de verao".
  //
  // E o MESMO relogio de calendario do `aee_GetTimeMS` (0x0ac), que esta tabela
  // ja serve: aquela frente devolve 0 ms como valor DECLARADO, porque o aparelho
  // emulado nunca adquiriu hora de sistema. A mesma regra, aplicada a mesma
  // grandeza: 0 segundos depois das 00:00:00 locais de 1980/01/06 -- o que faz
  // `GetTimeMS()/1000 == GetSeconds() % 86400`, e as duas contas nunca se
  // contradizem.
  Bancada b;
  EXPECT_EQ(b.Atender(0x0B4), Atendimento::Implementado);
  EXPECT_EQ(b.cpu.Get(kR0), 0u);
  EXPECT_EQ(b.Faltas("AEEHelperFuncs[0x0b4] aee_GetSeconds"), 0u);
  const auto& p = b.traco.ContagemPressupostos();
  const auto it = p.find("aee_GetSeconds_sem_relogio");
  ASSERT_NE(it, p.end()) << "um valor nao medido tem de aparecer no registo";
  EXPECT_EQ(it->second, 1u);
}

TEST(AjudantesExtra, JulianDateDoInstanteBaseE1980) {
  // Os valores do teste do PROPRIO SDK (`OATStdLib_Time.c`,
  // `OATStdLib_JULIANTOSECONDS_GETJULIANDATE`): "00:00:00 1/6/1980 <--> 0" com
  // `wWeekDay = 6`. E o que fixa a convencao do dia da semana: 0=segunda ...
  // 6=domingo (`AEEShell.h:2954`), e nao o domingo a zero.
  Bancada b;
  for (std::uint32_t i = 0; i < 20; ++i) b.mem.Escrever8(kTexto2 + i, 0xAA);
  b.cpu.Set(kR0, 0);
  b.cpu.Set(kR1, kTexto2);
  EXPECT_EQ(b.Atender(0x0B8), Atendimento::Implementado);
  EXPECT_EQ(b.mem.Ler16(kTexto2 + 0), 1980u);
  EXPECT_EQ(b.mem.Ler16(kTexto2 + 2), 1u);
  EXPECT_EQ(b.mem.Ler16(kTexto2 + 4), 6u);
  EXPECT_EQ(b.mem.Ler16(kTexto2 + 6), 0u);
  EXPECT_EQ(b.mem.Ler16(kTexto2 + 8), 0u);
  EXPECT_EQ(b.mem.Ler16(kTexto2 + 10), 0u);
  EXPECT_EQ(b.mem.Ler16(kTexto2 + 12), 6u) << "1980/01/06 foi um domingo";
  for (std::uint32_t i = 14; i < 20; ++i) {
    EXPECT_EQ(b.mem.Ler8(kTexto2 + i), 0xAAu) << "escreveu depois dos 7 uint16";
  }
  EXPECT_EQ(b.Faltas("AEEHelperFuncs[0x0b8] aee_GetJulianDate"), 0u);
}

TEST(AjudantesExtra, JulianDateDoAnoBissextoE1981) {
  // "00:00:00 1/6/1981 <--> 366*24*60*60" com `wWeekDay = 1` (terca) -- o mesmo
  // teste do SDK, e a prova de que 1980 conta 366 dias.
  Bancada b;
  b.cpu.Set(kR0, 366u * 86400u);
  b.cpu.Set(kR1, kTexto2);
  EXPECT_EQ(b.Atender(0x0B8), Atendimento::Implementado);
  EXPECT_EQ(b.mem.Ler16(kTexto2 + 0), 1981u);
  EXPECT_EQ(b.mem.Ler16(kTexto2 + 2), 1u);
  EXPECT_EQ(b.mem.Ler16(kTexto2 + 4), 6u);
  EXPECT_EQ(b.mem.Ler16(kTexto2 + 12), 1u);
}

TEST(AjudantesExtra, JulianDateComHoraEMes) {
  // 31 dias + 01:01:01 -> 1980/02/06 01:01:01, quarta-feira (o terceiro valor do
  // teste do SDK: "00:00:00 2/6/1980 <--> 31*24*60*60", `wWeekDay = 2`).
  Bancada b;
  b.cpu.Set(kR0, 31u * 86400u + 3661u);
  b.cpu.Set(kR1, kTexto2);
  b.Atender(0x0B8);
  EXPECT_EQ(b.mem.Ler16(kTexto2 + 0), 1980u);
  EXPECT_EQ(b.mem.Ler16(kTexto2 + 2), 2u);
  EXPECT_EQ(b.mem.Ler16(kTexto2 + 4), 6u);
  EXPECT_EQ(b.mem.Ler16(kTexto2 + 6), 1u);
  EXPECT_EQ(b.mem.Ler16(kTexto2 + 8), 1u);
  EXPECT_EQ(b.mem.Ler16(kTexto2 + 10), 1u);
  EXPECT_EQ(b.mem.Ler16(kTexto2 + 12), 2u);
}

TEST(AjudantesExtra, JulianDateComDwSecsZeroUsaORelogioDeclarado) {
  // Doc (`AEEStdLib.h:4938-4941`): "If the input value is 0, GETTIMESECONDS()
  // is used." O "agora" desta arvore e o instante declarado -- e o caminho passa
  // a mesma vez pelo registo do pressuposto, e nao por uma falta.
  Bancada b;
  b.cpu.Set(kR0, 0);
  b.cpu.Set(kR1, kTexto2);
  EXPECT_EQ(b.Atender(0x0B8), Atendimento::Implementado);
  EXPECT_EQ(b.mem.Ler16(kTexto2 + 0), 1980u);
  EXPECT_EQ(b.mem.Ler16(kTexto2 + 4), 6u);
  const auto& p = b.traco.ContagemPressupostos();
  EXPECT_EQ(p.at("aee_GetSeconds_sem_relogio"), 1u);
  EXPECT_EQ(b.Faltas("AEEHelperFuncs[0x0b8] aee_GetJulianDate"), 0u);
}

TEST(AjudantesExtra, JulianDateComPonteiroNuloRecusa) {
  // A funcao e `void`: nao ha valor de erro para devolver. Recusa-se em voz alta
  // (P2) e NAO se escreve nada. O `Atendimento` e `Implementado` -- a TABELA
  // tomou conta do offset, como em qualquer funcao implementada que recusa
  // dentro; o que diz o que se passou e o registo, com a razao.
  Bancada b;
  b.cpu.Set(kR0, 100u);
  b.cpu.Set(kR1, 0);
  EXPECT_EQ(b.Atender(0x0B8), Atendimento::Implementado);
  EXPECT_EQ(b.Faltas("AEEHelperFuncs[0x0b8] aee_GetJulianDate"), 1u);
  EXPECT_NE(b.DetalheDaFalta("AEEHelperFuncs[0x0b8] aee_GetJulianDate").find("pDate nulo"),
            std::string::npos);
}

TEST(AjudantesExtra, JulianDateNaoEstouraComOSegundoMaximo) {
  // `dwSecs` e `uint32` e a conta e feita em 64 bits: o valor maximo tem de dar
  // uma data de calendario valida, e nao lixo. (O proprio teste do SDK regista
  // que o valor -1 NAO volta a 1980/01/05 -- ver o relatorio.)
  Bancada b;
  b.cpu.Set(kR0, 0xFFFFFFFFu);
  b.cpu.Set(kR1, kTexto2);
  EXPECT_EQ(b.Atender(0x0B8), Atendimento::Implementado);
  const std::uint16_t ano = b.mem.Ler16(kTexto2 + 0);
  const std::uint16_t mes = b.mem.Ler16(kTexto2 + 2);
  const std::uint16_t dia = b.mem.Ler16(kTexto2 + 4);
  const std::uint16_t semana = b.mem.Ler16(kTexto2 + 12);
  EXPECT_GE(ano, 1980u);
  EXPECT_LE(ano, 2116u);
  EXPECT_GE(mes, 1u);
  EXPECT_LE(mes, 12u);
  EXPECT_GE(dia, 1u);
  EXPECT_LE(dia, 31u);
  EXPECT_LE(semana, 6u);
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

// ===========================================================================
// O `SetupNativeImage` (0x064) -- o `CONVERTBMP`, frente `setup`.
//
// O que estes testes fixam, e por que cada um:
//   1. O QUE SAI e um BLOCO CRU de pixels RGB565, top-down, largura x altura,
//      sem cabecalho nenhum -- e assim que o `BitBlt` do emulador o le
//      (`core/brew/despacho.cpp`, `kSlotIdBitBlt`), e foi o que o desmonte do
//      `allstarcards.mod` mostrou ser o destino do ponteiro.
//   2. `pii` fica ESCRITO (cx, cy, nColors, bAnimated, cxFrame): o jogo le
//      `cx` e `cy` sem confirmar nada.
//   3. `*pbRealloc = TRUE`: com FALSE o jogo copiava o tamanho COMPRIMIDO.
//   4. A RECUSA nao escreve `pii` e leva o NOME (P2) e a razao.
// ===========================================================================

constexpr std::uint32_t kBmp = 0x80110000u;
constexpr std::uint32_t kPii = 0x80120000u;
constexpr std::uint32_t kSinalizador = 0x80130000u;
constexpr std::uint32_t kSentinela = 0xCDCDCDCDu;

// UM BMP A SERIO, byte a byte, com o tamanho declarado no cabecalho do
// ficheiro -- e o ajudante le-o de la, porque a assinatura NAO leva tamanho.
// `paleta` e uma lista de cores RGB (a ordem do ficheiro e B, G, R, 0).
std::uint32_t EscreverBmp(Bancada& b, std::uint32_t onde, std::int32_t largura,
                          std::int32_t altura, std::uint16_t bits, std::uint32_t compressao,
                          const std::vector<std::uint32_t>& paleta,
                          const std::vector<std::uint8_t>& dados) {
  const std::uint32_t inicio =
      14u + 40u + static_cast<std::uint32_t>(paleta.size()) * 4u;
  const std::uint32_t total = inicio + static_cast<std::uint32_t>(dados.size());
  const auto p8 = [&](std::uint32_t off, std::uint8_t v) { b.mem.Escrever8(onde + off, v); };
  const auto p16 = [&](std::uint32_t off, std::uint16_t v) { b.mem.Escrever16(onde + off, v); };
  const auto p32 = [&](std::uint32_t off, std::uint32_t v) { b.mem.Escrever32(onde + off, v); };
  p8(0, 'B');
  p8(1, 'M');
  p32(2, total);
  p32(10, inicio);
  p32(14, 40);  // BITMAPINFOHEADER
  p32(18, static_cast<std::uint32_t>(largura));
  p32(22, static_cast<std::uint32_t>(altura));
  p16(26, 1);
  p16(28, bits);
  p32(30, compressao);
  p32(34, static_cast<std::uint32_t>(dados.size()));
  p32(46, static_cast<std::uint32_t>(paleta.size()));
  for (std::size_t k = 0; k < paleta.size(); ++k) {
    // A cor entra como 0xRRGGBB e o ficheiro guarda os tres bytes ao
    // contrario (B, G, R) e um quarto que fica a zero.
    const std::uint32_t cor = paleta[k];
    const std::uint32_t q = 54u + static_cast<std::uint32_t>(k) * 4u;
    p8(q + 0, static_cast<std::uint8_t>((cor >> 16) & 0xFFu));
    p8(q + 1, static_cast<std::uint8_t>((cor >> 8) & 0xFFu));
    p8(q + 2, static_cast<std::uint8_t>(cor & 0xFFu));
    p8(q + 3, 0);
  }
  for (std::size_t k = 0; k < dados.size(); ++k) {
    p8(inicio + static_cast<std::uint32_t>(k), dados[k]);
  }
  return total;
}

// As cores do teste, dadas em RGB (o BMP guarda-as ao contrario).
constexpr std::uint32_t kRgbVermelho = 0x0000FFu;
constexpr std::uint32_t kRgbVerde = 0x00FF00u;
constexpr std::uint32_t kRgbAzul = 0xFF0000u;
constexpr std::uint32_t kRgbBranco = 0xFFFFFFu;
constexpr std::uint16_t k565Vermelho = 0xF800u;
constexpr std::uint16_t k565Verde = 0x07E0u;
constexpr std::uint16_t k565Azul = 0x001Fu;
constexpr std::uint16_t k565Branco = 0xFFFFu;

// Arma a bancada para uma chamada ao ajudante: clsid, buffer, pii e o byte do
// `pbRealloc`, com uma sentinela para se poder provar que ele NAO foi mexido.
void ArmarSetup(Bancada& b, std::uint32_t p_buffer, std::uint32_t cls = 0x01004001u) {
  b.cpu.Set(kR0, cls);
  b.cpu.Set(kR1, p_buffer);
  b.cpu.Set(kR2, kPii);
  b.cpu.Set(kR3, kSinalizador);
  for (std::uint32_t k = 0; k < 10; ++k) b.mem.Escrever8(kPii + k, 0xCD);
  b.mem.Escrever8(kSinalizador, 0xCD);
}

TEST(AjudantesExtra, SetupNativeImageEDescodificadoPorEstaTabela) {
  // O offset, o nome e a assinatura do cabecalho (`AEEStdLib.h:88`), escritos
  // por extenso para a proxima frente nao o tomar por um `atoi` maior.
  EXPECT_EQ(brew_ajudantes::kAjudante_SetupNativeImage, 0x064u);
  ASSERT_NE(DeclaracaoDoOffset(0x064), nullptr);
  EXPECT_STREQ(DeclaracaoDoOffset(0x064)->nome, "SetupNativeImage");
  EXPECT_STREQ(DeclaracaoDoOffset(0x064)->assinatura,
               "void *(*SetupNativeImage)(AEECLSID cls, void *pBuffer, AEEImageInfo *pii, "
               "boolean *pbRealloc)");
  EXPECT_EQ(AjudantesExtra::Implementados(), 28u) << "o 0x064 e o memstr entraram na tabela";
}

TEST(AjudantesExtra, SetupNativeImageDescodificaBmpDe8BitsComPaleta) {
  // 2x2 a 8 bits com paleta: o BI_RGB guarda o ficheiro de BAIXO para cima, e o
  // que sai daqui tem de estar em CIMA para baixo (e a ordem que o `BitBlt` le).
  // Imagem:   A B      ficheiro: C D (primeira linha) e depois A B
  //           C D
  Bancada b;
  const std::vector<std::uint32_t> paleta = {kRgbVermelho, kRgbVerde, kRgbAzul, kRgbBranco};
  const std::vector<std::uint8_t> dados = {2, 3, 0, 0,  // linha de baixo: C D + enchimento
                                           0, 1, 0, 0}; // linha de cima:  A B
  EscreverBmp(b, kBmp, 2, 2, 8, 0, paleta, dados);
  ArmarSetup(b, kBmp);

  EXPECT_EQ(b.Atender(0x064), Atendimento::Implementado);
  const std::uint32_t p = b.cpu.Get(kR0);
  EXPECT_NE(p, 0u);
  EXPECT_EQ(b.Faltas("AEEHelperFuncs[0x064] SetupNativeImage"), 0u);
  EXPECT_EQ(b.mem.Ler16(p + 0), k565Vermelho);
  EXPECT_EQ(b.mem.Ler16(p + 2), k565Verde);
  EXPECT_EQ(b.mem.Ler16(p + 4), k565Azul);
  EXPECT_EQ(b.mem.Ler16(p + 6), k565Branco);
  // O `pii` (`AEEImageInfo.h:17-23`): cx, cy, nColors, bAnimated, cxFrame.
  EXPECT_EQ(b.mem.Ler16(kPii + 0), 2u) << "cx";
  EXPECT_EQ(b.mem.Ler16(kPii + 2), 2u) << "cy";
  EXPECT_EQ(b.mem.Ler16(kPii + 4), 0u) << "nColors";
  EXPECT_EQ(b.mem.Ler8(kPii + 6), 0u) << "bAnimated";
  EXPECT_EQ(b.mem.Ler16(kPii + 8), 2u) << "cxFrame = a largura do unico quadro";
  // E O SINALIZADOR. Com FALSE o jogo copiava o tamanho COMPRIMIDO (221 para um
  // BMP de 286 bytes, medido) e desenhava a copia truncada.
  EXPECT_EQ(b.mem.Ler8(kSinalizador), 1u) << "pbRealloc TRUE: o buffer e nosso";
}

TEST(AjudantesExtra, SetupNativeImageDescodificaBmpDe24BitsSemPaleta) {
  Bancada b;
  const std::vector<std::uint8_t> dados = {0, 0, 255, 255, 0, 0, 0, 0};  // BGR, BGR + enchimento
  EscreverBmp(b, kBmp, 2, 1, 24, 0, {}, dados);
  ArmarSetup(b, kBmp);
  EXPECT_EQ(b.Atender(0x064), Atendimento::Implementado);
  const std::uint32_t p = b.cpu.Get(kR0);
  EXPECT_EQ(b.mem.Ler16(p + 0), k565Vermelho);
  EXPECT_EQ(b.mem.Ler16(p + 2), k565Azul);
  EXPECT_EQ(b.mem.Ler16(kPii + 0), 2u);
  EXPECT_EQ(b.mem.Ler16(kPii + 2), 1u);
}

TEST(AjudantesExtra, SetupNativeImageDescodificaBmpDe4BitsEOSinalizador) {
  // 2x2 a 4 bits BI_RGB: dois pixels por byte, o da ESQUERDA no nibble de cima.
  Bancada b;
  const std::vector<std::uint32_t> paleta = {kRgbVermelho, kRgbVerde, kRgbAzul, kRgbBranco};
  // Linha de baixo (azul, branco) = 0x23, linha de cima (vermelho, verde) = 0x01.
  const std::vector<std::uint8_t> dados = {0x23, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00};
  EscreverBmp(b, kBmp, 2, 2, 4, 0, paleta, dados);
  ArmarSetup(b, kBmp);
  EXPECT_EQ(b.Atender(0x064), Atendimento::Implementado);
  const std::uint32_t p = b.cpu.Get(kR0);
  EXPECT_EQ(b.mem.Ler16(p + 0), k565Vermelho) << "nibble de cima = pixel da esquerda";
  EXPECT_EQ(b.mem.Ler16(p + 2), k565Verde);
  EXPECT_EQ(b.mem.Ler16(p + 4), k565Azul);
  EXPECT_EQ(b.mem.Ler16(p + 6), k565Branco);
}

TEST(AjudantesExtra, SetupNativeImageDescodificaRle8DeBaixoParaCima) {
  // 4x4 a 8 bits BI_RLE8, quatro linhas absolutas IGUAIS em conteudo e
  // diferentes entre si, para se ver a ORDEM: a primeira linha do ficheiro e a
  // de BAIXO. As corridas absolutas pautam-se a 2 bytes (a de 3 valores leva um
  // enchimento), e e isso que o teste do `0x00,0x03` mede.
  Bancada b;
  const std::vector<std::uint32_t> paleta = {kRgbVermelho, kRgbVerde, kRgbAzul, kRgbBranco};
  const std::vector<std::uint8_t> dados = {
      0x00, 0x04, 0, 0, 0, 0, 0x00, 0x00,  // linha de baixo: 0 0 0 0
      0x00, 0x04, 1, 1, 1, 1, 0x00, 0x00,  //                  1 1 1 1
      // A corrida absoluta de 3 valores e IMPAR: leva um byte de enchimento,
      // e o que vem a seguir (uma corrida normal de 1) tem de ser lido no sitio
      // certo. E este o teste do alinhamento.
      0x00, 0x03, 2, 2, 2, 0x00, 0x01, 0x03, 0x00, 0x00,  //  2 2 2 3
      0x00, 0x04, 3, 3, 3, 3, 0x00, 0x00,  // linha de cima:   3 3 3 3
      0x00, 0x01};                         // fim da imagem
  EscreverBmp(b, kBmp, 4, 4, 8, 1, paleta, dados);
  ArmarSetup(b, kBmp);
  EXPECT_EQ(b.Atender(0x064), Atendimento::Implementado);
  const std::uint32_t p = b.cpu.Get(kR0);
  const std::uint16_t esperado[4][4] = {{k565Branco, k565Branco, k565Branco, k565Branco},
                                        {k565Azul, k565Azul, k565Azul, k565Branco},
                                        {k565Verde, k565Verde, k565Verde, k565Verde},
                                        {k565Vermelho, k565Vermelho, k565Vermelho, k565Vermelho}};
  for (std::uint32_t y = 0; y < 4; ++y) {
    for (std::uint32_t x = 0; x < 4; ++x) {
      EXPECT_EQ(b.mem.Ler16(p + (y * 4u + x) * 2u), esperado[y][x])
          << "pixel (" << x << "," << y << ")";
    }
  }
  EXPECT_EQ(b.mem.Ler16(kPii + 0), 4u);
  EXPECT_EQ(b.mem.Ler16(kPii + 2), 4u);
}

TEST(AjudantesExtra, SetupNativeImageDescodificaRle4) {
  // 4x2 a 4 bits BI_RLE4: cada byte leva dois nibbles, o primeiro e o pixel da
  // ESQUERDA. A corrida absoluta de 3 nibbles leva um byte (2 nibbles) e o
  // bloco inteiro pauta-se a 2 bytes.
  Bancada b;
  const std::vector<std::uint32_t> paleta = {kRgbVermelho, kRgbVerde, kRgbAzul, kRgbBranco};
  const std::vector<std::uint8_t> dados = {
      0x00, 0x04, 0x01, 0x23, 0x00, 0x00,  // linha de baixo: 0 1 2 3
      0x00, 0x04, 0x32, 0x10, 0x00, 0x00,  // linha de cima:  3 2 1 0
      0x00, 0x01};
  EscreverBmp(b, kBmp, 4, 2, 4, 2, paleta, dados);
  ArmarSetup(b, kBmp);
  EXPECT_EQ(b.Atender(0x064), Atendimento::Implementado);
  const std::uint32_t p = b.cpu.Get(kR0);
  EXPECT_EQ(b.mem.Ler16(p + 0), k565Branco) << "linha de cima, pixel 0";
  EXPECT_EQ(b.mem.Ler16(p + 2), k565Azul);
  EXPECT_EQ(b.mem.Ler16(p + 4), k565Verde);
  EXPECT_EQ(b.mem.Ler16(p + 6), k565Vermelho);
  EXPECT_EQ(b.mem.Ler16(p + 8), k565Vermelho) << "linha de baixo, pixel 0";
  EXPECT_EQ(b.mem.Ler16(p + 10), k565Verde);
  EXPECT_EQ(b.mem.Ler16(p + 12), k565Azul);
  EXPECT_EQ(b.mem.Ler16(p + 14), k565Branco);
}

TEST(AjudantesExtra, SetupNativeImageRecusaClsQueNaoEOWinBmp) {
  // `AEECLSID_WINBMP` (0x01004002) NAO e o que o `CONVERTBMP` passa: o
  // `#define` do SDK passa `AEECLSID_WinBMP` = `AEECLSID_NATIVEBMP` = 0x01004001
  // (`AEEStdLib.h:384`, `tools/clsids.inc`).
  Bancada b;
  ArmarSetup(b, kBmp, 0x01004002u);
  // O ajudante ESTA na tabela (logo `Implementado`), e a recusa dele fica
  // registada com o nome -- e a convencao das outras recusas desta tabela.
  EXPECT_EQ(b.Atender(0x064), Atendimento::Implementado);
  EXPECT_EQ(b.cpu.Get(kR0), kAeeUnsupported);
  EXPECT_EQ(b.Faltas("AEEHelperFuncs[0x064] SetupNativeImage"), 1u);
  EXPECT_NE(b.DetalheDaFalta("AEEHelperFuncs[0x064] SetupNativeImage").find("0x01004002"),
            std::string::npos);
}

TEST(AjudantesExtra, SetupNativeImageRecusaOQueNaoEDescodificavelENaoEscreveOPii) {
  Bancada b;
  // Assinatura que nao e "BM" (4 bytes escritos por cima do cabecalho).
  for (std::uint32_t k = 0; k < 64; ++k) b.mem.Escrever8(kBmp + k, 0);
  b.mem.Escrever8(kBmp + 0, 'P');
  b.mem.Escrever8(kBmp + 1, 'N');
  b.mem.Escrever8(kBmp + 2, 'G');
  ArmarSetup(b, kBmp);
  EXPECT_EQ(b.Atender(0x064), Atendimento::Implementado);
  EXPECT_EQ(b.cpu.Get(kR0), kAeeUnsupported);
  EXPECT_EQ(b.Faltas("AEEHelperFuncs[0x064] SetupNativeImage"), 1u);
  EXPECT_NE(b.DetalheDaFalta("AEEHelperFuncs[0x064] SetupNativeImage").find("nao BM"),
            std::string::npos);
  // O `pii` fica como estava: o SDK so o escreve quando devolve imagem.
  for (std::uint32_t k = 0; k < 10; ++k) {
    EXPECT_EQ(b.mem.Ler8(kPii + k), 0xCDu) << "pii mexido no byte " << k;
  }
  EXPECT_EQ(b.mem.Ler8(kSinalizador), 0xCDu) << "o sinalizador tambem nao se mexe";
}

TEST(AjudantesExtra, SetupNativeImageRecusaProfundidadeQueNaoSabeLer) {
  Bancada b;
  EscreverBmp(b, kBmp, 2, 2, 16, 0, {}, std::vector<std::uint8_t>(16, 0));
  ArmarSetup(b, kBmp);
  EXPECT_EQ(b.Atender(0x064), Atendimento::Implementado);
  EXPECT_EQ(b.Faltas("AEEHelperFuncs[0x064] SetupNativeImage"), 1u);
  EXPECT_NE(b.DetalheDaFalta("AEEHelperFuncs[0x064] SetupNativeImage").find("profundidade"),
            std::string::npos);
}

// ===========================================================================
// O `AEEOldVaList` (frente `fmt`). O `vsprintf`/`vsnprintf` NAO recebem os
// argumentos: recebem o ENDERECO da VARIAVEL `va_list` (`typedef int **
// AEEOldVaList`, `AEEOldVaList.h:37`), e a area de argumentos esta no valor
// dela. O desmonte do `alice.mod` e a medicao estao em `core/brew/formato.h`.
// ===========================================================================

constexpr std::uint32_t kAreaDeArgumentos = 0x80102000u;
constexpr std::uint32_t kListaDeArgumentos = 0x80102400u;
constexpr std::uint32_t kFormatoDoAssert = 0x80101000u;

TEST(AjudantesExtra, ArgumentosDoVaListsSaemDoValorDaVariavel) {
  // O layout do `alice`, com os numeros medidos: o registador traz o ENDERECO da
  // VARIAVEL `va_list` (no traco real, 0x8f0afd7c), a variavel aponta para a
  // area, e a area tem os argumentos do formato "  %d @ %s" -- a cadeia do
  // proprio ASSERT do titulo (0x3fa30 no `.mod`).
  Bancada b;
  constexpr std::uint32_t kTextoDoAssert = 0x80103000u;
  b.EscreverCadeia(kFormatoDoAssert, "  %d @ %s");
  b.EscreverCadeia(kTextoDoAssert, "alice.c");
  b.mem.Escrever32(kAreaDeArgumentos + 0, 16);
  b.mem.Escrever32(kAreaDeArgumentos + 4, kTextoDoAssert);
  b.mem.Escrever32(kListaDeArgumentos, kAreaDeArgumentos);  // *va_list = a area
  std::uint32_t args[8];
  EXPECT_EQ(ArgumentosDoVaLists(b.mem, kListaDeArgumentos, args, 8), 8);
  EXPECT_EQ(args[0], 16u);
  EXPECT_EQ(args[1], kTextoDoAssert);
  EXPECT_EQ(FormatarParaTexto(b.mem, kFormatoDoAssert, args, 8), "  16 @ alice.c");
  // SEM LEITURA FORA DO QUE EXISTE: nem a variavel nem a area nem as palavras
  // que passam do fim. O valor seria zero de qualquer maneira -- o que muda e que
  // uma leitura nao mapeada NOSSA deixaria de aparecer como evidencia contra o
  // guest (e a `Memoria` conta-as todas).
  b.mem.PararDeSondar();
  const std::uint64_t lidas = b.mem.LeiturasNaoMapeadas();
  std::uint32_t outros[8];
  EXPECT_EQ(ArgumentosDoVaLists(b.mem, 0x7F000000u, outros, 8), 0)
      << "a VARIAVEL nao esta em memoria: nao se le nada";
  EXPECT_EQ(ArgumentosDoVaLists(b.mem, kListaDeArgumentos, outros, 8), 8);
  EXPECT_EQ(b.mem.LeiturasNaoMapeadas(), lidas)
      << "nenhuma leitura saiu fora da memoria mapeada";
}

TEST(AjudantesExtra, SemAIndirecaoOsArgumentosSaoOsBytesDoFormato) {
  // A CONTRADICAO, com os numeros medidos, e fica escrita para a proxima frente
  // nao ter de a redescobrir: SEM a indirecao -- a ler em `pLista + 4*k`, que e
  // o que o despacho fazia -- o argumento do `%s` de "  %d @ %s" e
  // `mem.Ler32(formato + 4)` = 0x25204020 = os ASCII " @ %" lidos como ENDERECO.
  // A "cadeia" nesse endereco nao existe: e a falta
  // `Memoria::Ler fora de instrucao`, que a frente `inst` mediu 20 vezes no
  // `alice` com o pc da chamada (0x3f04c).
  Bancada b;
  b.EscreverCadeia(kFormatoDoAssert, "  %d @ %s");
  std::uint32_t errado[2];
  errado[0] = b.mem.Ler32(kFormatoDoAssert);
  errado[1] = b.mem.Ler32(kFormatoDoAssert + 4);
  EXPECT_EQ(errado[0], 0x64252020u) << "os ASCII \"  %d\" como argumento";
  EXPECT_EQ(errado[1], 0x25204020u) << "os ASCII \" @ %\" -- o endereco do defeito";
  const std::uint64_t antes = b.mem.LeiturasNaoMapeadas();
  EXPECT_EQ(FormatarParaTexto(b.mem, kFormatoDoAssert, errado, 2), "  1680154656 @ ");
  EXPECT_GT(b.mem.LeiturasNaoMapeadas(), antes)
      << "o `%s` foi LER os bytes do formato como se fossem um endereco";
}

TEST(AjudantesExtra, FlutuanteConsomeDoisArgumentosENaoDeslocaOsSeguintes) {
  // O formato medido no `quake2brew` e `"%4.2f %s %s %s"`, e a cadeia esta no
  // `.mod` ao lado do proprio double 3.2 (0x299d0 e 0x299ce). Sem consumir os
  // DOIS argumentos do `double`, o `%s` seguinte recebia 0x7ae147ae -- a METADE
  // BAIXA do 3.2 -- e a leitura dessa "cadeia" era uma falta NOVA
  // (`Memoria::Ler fora de instrucao` 0 -> 1 nesse titulo, medido).
  Bancada b;
  constexpr std::uint32_t kFmt = 0x80101000u;
  constexpr std::uint32_t kTxt = 0x80103000u;
  b.EscreverCadeia(kFmt, "%4.2f %s");
  b.EscreverCadeia(kTxt, "alice.c");
  // As duas palavras medidas no `.mod` (0x299ce): sao o double 3.21.
  const std::uint32_t args[3] = {0x7ae147aeu, 0x4009ae14u, kTxt};
  EXPECT_EQ(FormatarParaTexto(b.mem, kFmt, args, 3), "3.21 alice.c");
  EXPECT_EQ(b.mem.LeiturasNaoMapeadas(), 0u) << "nenhuma leitura fora da memoria";
  // A precisao e do C: o `%f` sai pelo `snprintf` DO SISTEMA, com o texto da
  // especificacao que o guest escreveu.
  b.EscreverCadeia(kFmt, "%.3f|");
  EXPECT_EQ(FormatarParaTexto(b.mem, kFmt, args, 3), "3.210|");
}

// ===========================================================================
// A CABLAGEM DO `vsnprintf` (0x140) E DO `vsprintf` (0x13c): a chamada VAI PELA
// TABELA.
//
// ARMADILHA JA PAGA nesta arvore: um teste que chama um `<id interno>` NAO testa
// a cablagem. Aqui o endereco do ajudante e LIDO da `AEEHelperFuncs` que o
// proprio despacho instalou, e quem chama e uma rotina do GUEST que salta para
// ele -- como o `alice` faz (`alice.mod` 0x3f044 `ldr ip,[r0,#0x140]` + `blx
// ip` em 0x3f04c). Se a tabela e o despacho divergirem, e este teste que falha.
// ===========================================================================

constexpr std::uint32_t kBtTabela = 0x80010000u;
constexpr std::uint32_t kBtRotina = 0x00004000u;
constexpr std::uint32_t kBtPilha = 0x80080000u;
constexpr std::uint32_t kBtDados = 0x80090000u;
constexpr std::uint32_t kBtSentinela = 0xFFFFFFF0u;
constexpr std::uint32_t kBtHeap = 0x80200000u;
constexpr std::uint32_t kBtHeapTam = 0x00100000u;

void CadeiaEm(Memoria& mem, std::uint32_t onde, const std::string& s) {
  for (std::size_t k = 0; k < s.size(); ++k) {
    mem.Escrever8(onde + static_cast<std::uint32_t>(k), static_cast<std::uint8_t>(s[k]));
  }
  mem.Escrever8(onde + static_cast<std::uint32_t>(s.size()), 0);
}

std::string LerEm(const Memoria& mem, std::uint32_t onde) {
  std::string s;
  for (std::uint32_t k = 0; k < 128; ++k) {
    const char ch = static_cast<char>(mem.Ler8(onde + k));
    if (ch == 0) break;
    s.push_back(ch);
  }
  return s;
}

class BancadaDaTabela {
 public:
  BancadaDaTabela() {
    saidas_.base = 0xF0000000u;
    saidas_.passo = 4;
    saidas_.quantos = 100000;
    saidas_.ativa = true;
    cpu_.ConfigurarSaidas(saidas_);
    al_ = new Alocador(mem_, kBtHeap, kBtHeapTam, nullptr);
    despacho_ = new Despacho(mem_, traco_, *al_, vfs_);
    despacho_->DefinirVtableBitmap(saidas_);
    despacho_->DefinirVtableFicheiro(saidas_.Endereco(kVtableFileObj));
    despacho_->InstalarAjudantes(saidas_, kBtTabela);
    despacho_->DefinirFaixaDoModulo(0, 0x00100000u);
  }
  ~BancadaDaTabela() {
    delete despacho_;
    delete al_;
  }

  // O ENDERECO DO AJUDANTE, LIDO DA TABELA (`AEEHelperFuncs`): nao e um id
  // interno escrito a mao. O `offset` e o do SDK, que e o BYTE -- `0x140` e a
  // 81a entrada (`despacho.cpp:801`: `Escrever32(tabela + lig.off, ...)`).
  std::uint32_t SaidaDoAjudante(std::uint32_t offset) const {
    return mem_.Ler32(kBtTabela + offset);
  }

  // A chamada do guest, em tres instrucoes:
  //   4000  e1a0e00f  mov lr, pc
  //   4004  e12fff18  bx  r8   @ o ajudante
  //   4008  e12fff17  bx  r7   @ a sentinela: a fase RETORNA
  ResultadoFase Chamar(std::uint32_t saida, std::uint32_t r0, std::uint32_t r1, std::uint32_t r2,
                       std::uint32_t r3) {
    mem_.Escrever32(kBtRotina + 0, 0xe1a0e00fu);
    mem_.Escrever32(kBtRotina + 4, 0xe12fff18u);
    mem_.Escrever32(kBtRotina + 8, 0xe12fff17u);
    cpu_.Repor(0, kBtPilha);
    cpu_.Set(kPC, kBtRotina);
    cpu_.Set(kLR, kBtSentinela);
    cpu_.Set(kR7, kBtSentinela);
    cpu_.Set(kR8, saida);
    cpu_.Set(kR0, r0);
    cpu_.Set(kR1, r1);
    cpu_.Set(kR2, r2);
    cpu_.Set(kR3, r3);
    return despacho_->Correr(cpu_, 20000, 0);
  }

  Memoria mem_;
  Traco traco_{"vsnprintf_da_tabela"};
  Vfs vfs_;
  Alocador* al_ = nullptr;
  Despacho* despacho_ = nullptr;
  Saidas saidas_;
  ArmInterpreter cpu_{mem_, &traco_};
};

// O bloco de argumentos, montado como o guest o monta: a VARIAVEL `va_list` na
// `kLista`, ela a apontar para a `kArea`, e a area com (16, "alice.c") para o
// formato do ASSERT "  %d @ %s".
void MontarOAssertDoAlice(BancadaDaTabela& b, std::uint32_t kFmt, std::uint32_t kTxt,
                          std::uint32_t kArea, std::uint32_t kLista) {
  CadeiaEm(b.mem_, kFmt, "  %d @ %s");
  CadeiaEm(b.mem_, kTxt, "alice.c");
  b.mem_.Escrever32(kArea + 0, 16);
  b.mem_.Escrever32(kArea + 4, kTxt);
  b.mem_.Escrever32(kLista, kArea);
}

TEST(AjudantesExtra, VsnprintfDaTabelaFormataComOvaListDoSDK) {
  BancadaDaTabela b;
  constexpr std::uint32_t kFmt = kBtDados + 0x000u;
  constexpr std::uint32_t kBuf = kBtDados + 0x100u;
  constexpr std::uint32_t kTxt = kBtDados + 0x200u;
  constexpr std::uint32_t kArea = kBtDados + 0x300u;
  constexpr std::uint32_t kLista = kBtDados + 0x400u;
  MontarOAssertDoAlice(b, kFmt, kTxt, kArea, kLista);
  const ResultadoFase r = b.Chamar(b.SaidaDoAjudante(brew_ajudantes::kAjudante_vsnprintf), kBuf,
                                   64, kFmt, kLista);
  EXPECT_EQ(r.motivo, "retornou") << "a rotina do guest correu ate ao fim";
  EXPECT_EQ(LerEm(b.mem_, kBuf), "  16 @ alice.c");
  EXPECT_EQ(b.cpu_.Get(kR0), 14u) << "o comprimento escrito, como o `sprintf`";
  EXPECT_EQ(b.mem_.LeiturasNaoMapeadas(), 0u)
      << "com o `va_list` do SDK nenhuma leitura sai fora da memoria";
}

TEST(AjudantesExtra, VsprintfDaTabelaUsaOMesmoVaLists) {
  BancadaDaTabela b;
  constexpr std::uint32_t kFmt = kBtDados + 0x000u;
  constexpr std::uint32_t kBuf = kBtDados + 0x100u;
  constexpr std::uint32_t kTxt = kBtDados + 0x200u;
  constexpr std::uint32_t kArea = kBtDados + 0x300u;
  constexpr std::uint32_t kLista = kBtDados + 0x400u;
  CadeiaEm(b.mem_, kFmt, "n=%s/%d");
  CadeiaEm(b.mem_, kTxt, "alice.c");
  b.mem_.Escrever32(kArea + 0, kTxt);
  b.mem_.Escrever32(kArea + 4, 7);
  b.mem_.Escrever32(kLista, kArea);
  // `vsprintf(buf, fmt, va_list)` -- o formato esta no r1 e a lista no r2.
  const ResultadoFase r = b.Chamar(b.SaidaDoAjudante(brew_ajudantes::kAjudante_vsprintf), kBuf, kFmt,
                                   0, 0);
  EXPECT_EQ(r.motivo, "retornou");
  EXPECT_EQ(LerEm(b.mem_, kBuf), "n=/0")
      << "sem lista (r2) nao ha argumentos: o `%s` sai vazio e o `%d` sai 0";
  const ResultadoFase r2 = b.Chamar(b.SaidaDoAjudante(brew_ajudantes::kAjudante_vsprintf), kBuf, kFmt,
                                    kLista, 0);
  EXPECT_EQ(r2.motivo, "retornou");
  EXPECT_EQ(LerEm(b.mem_, kBuf), "n=alice.c/7");
}

}  // namespace

// ---------------------------------------------------------------------------
// OS TESTES DO HEAP DO KAIOTEC -- portados do `zeebx` dele (`src/brew/heap.rs`)
// ---------------------------------------------------------------------------
//
// A HISTORIA, contada por ele: "*eu tive um BO lascado com estouro de memoria no
// treino cerebral, a rom em si funcionava PERFEITAMENTE, mas conforme tu avancava
// ela comecava a morrer aos poucos ate crashar e nao era nada do jogo em si -- era
// full gerenciamento de memoria que tava esgotando a ram*".
//
// A CAUSA no lado dele: cada bloco devolvido virava um buraco ISOLADO na lista de
// livres, e numa sessao longa (o Treino Cerebral troca de tela centenas de vezes) o
// heap despedaca -- o pedido de um mega da fase seguinte nao acha onde caber com
// dezenas de megas livres, o jogo nao confere o ponteiro nulo e morre chamando um
// metodo em zero.
//
// O NOSSO `Alocador` JA FUNDE (esta escrito no `Free`), e estes tres testes existem
// para o PROVAR em vez de o afirmar -- e para o prender se alguem mexer ali.
TEST(AjudantesExtra, OHeapFundeOsBlocosLivresVizinhos) {
  Bancada b;
  const std::uint32_t a = b.alocador.Malloc(32);
  const std::uint32_t c = b.alocador.Malloc(32);
  const std::uint32_t d = b.alocador.Malloc(32);
  const std::uint32_t depois = b.alocador.Malloc(8);
  ASSERT_NE(a, 0u);
  ASSERT_NE(c, 0u);
  ASSERT_NE(d, 0u);
  ASSERT_NE(depois, 0u);
  // Soltos FORA de ordem: e a vizinhanca que tem de ser juntada, nao a ordem.
  b.alocador.Free(c);
  b.alocador.Free(a);
  b.alocador.Free(d);
  // O pedido de 96 TEM de caber no buraco que os tres formam juntos. Sem a fusao
  // ele ia para o topo do heap -- e numa sessao longa o topo acaba.
  EXPECT_EQ(b.alocador.Malloc(96), a) << "os tres blocos deviam ter virado um so";
  b.alocador.Free(depois);
}

TEST(AjudantesExtra, OBlocoDoTopoDevolveOEspacoAoHeap) {
  Bancada b;
  const std::uint32_t antes = b.alocador.Alocado();
  const std::uint32_t a = b.alocador.Malloc(32);
  ASSERT_NE(a, 0u);
  EXPECT_GT(b.alocador.Alocado(), antes);
  b.alocador.Free(a);
  EXPECT_EQ(b.alocador.Alocado(), antes) << "nada pode continuar entregue";
}

TEST(AjudantesExtra, SobEstresseNenhumBlocoVivoSeSobrepoe) {
  // O TESTE QUE O KAIOTEC ESCREVEU DEPOIS DO BO: a fusao mexe em VIZINHANCA, e um
  // erro ali entrega o mesmo endereco duas vezes -- o jogo escreve por cima do que
  // era dele, e o sintoma aparece longe da causa. Aqui correm 4000 passos de
  // alocacoes e libertacoes sorteadas com os blocos VIVOS conferidos a cada passo.
  Bancada b;
  std::vector<std::pair<std::uint32_t, std::uint32_t>> vivos;
  std::uint32_t semente = 12345u;
  const auto sorteia = [&semente]() {
    semente = semente * 1103515245u + 12345u;
    return semente >> 8;
  };
  for (int passo = 0; passo < 4000; ++passo) {
    const bool solta = !vivos.empty() && (passo % 3 == 0 || vivos.size() > 40);
    if (solta) {
      const std::size_t qual = sorteia() % vivos.size();
      b.alocador.Free(vivos[qual].first);
      vivos.erase(vivos.begin() + static_cast<std::ptrdiff_t>(qual));
      continue;
    }
    const std::uint32_t tamanho = 1u + sorteia() % 600u;
    const std::uint32_t endereco = b.alocador.Malloc(tamanho);
    if (endereco == 0) continue;
    const std::uint32_t fim = endereco + tamanho;
    for (const auto& outro : vivos) {
      const bool afastado = fim <= outro.first || endereco >= outro.first + outro.second;
      ASSERT_TRUE(afastado) << "bloco 0x" << std::hex << endereco << "+" << tamanho
                            << " encosta em 0x" << outro.first << "+" << outro.second;
    }
    vivos.push_back({endereco, tamanho});
  }
}

TEST(AjudantesExtra, OStrexpandAlargaBytesParaAecharsComTerminador) {
  // `void strexpand(const byte *pSrc, int nCount, AECHAR *pDest, int nSize)`:
  // cada BYTE vira uma unidade de 16 bits. O `ddragonz` pede-o 1500 vezes com um
  // buffer de pilha reaproveitado -- e o terminador TEM de la estar, senao a
  // cadeia que ele desenha a seguir le o que ficou do texto anterior.
  Bancada b;
  const std::uint32_t origem = 0x0004d000u, destino = 0x0004e000u;
  const std::string texto = "Kaiotec";
  for (std::size_t k = 0; k < texto.size(); ++k) {
    b.mem.Escrever8(origem + static_cast<std::uint32_t>(k), static_cast<std::uint8_t>(texto[k]));
  }
  // Antes de escrever, o destino tem LIXO -- e o terminador que o apaga.
  for (std::uint32_t k = 0; k < 32u; ++k) b.mem.Escrever16(destino + 2u * k, 0xFFFFu);
  b.cpu.Set(kR0, origem);
  b.cpu.Set(kR1, static_cast<std::uint32_t>(texto.size()));
  b.cpu.Set(kR2, destino);
  b.cpu.Set(kR3, 32u);
  EXPECT_EQ(b.Atender(brew_ajudantes::kAjudante_strexpand), Atendimento::Implementado);
  for (std::size_t k = 0; k < texto.size(); ++k) {
    EXPECT_EQ(b.mem.Ler16(destino + static_cast<std::uint32_t>(2 * k)),
              static_cast<std::uint16_t>(texto[k]))
        << "unidade " << k;
  }
  EXPECT_EQ(b.mem.Ler16(destino + static_cast<std::uint32_t>(2 * texto.size())), 0u)
      << "o terminador tem de estar la";
  // E com `nSize` mais curto que o texto, corta e fecha: nao escreve fora.
  for (std::uint32_t k = 0; k < 32u; ++k) b.mem.Escrever16(destino + 2u * k, 0xFFFFu);
  b.cpu.Set(kR3, 4u);
  EXPECT_EQ(b.Atender(brew_ajudantes::kAjudante_strexpand), Atendimento::Implementado);
  EXPECT_EQ(b.mem.Ler16(destino + 0u), static_cast<std::uint16_t>('K'));
  EXPECT_EQ(b.mem.Ler16(destino + 2u), static_cast<std::uint16_t>('a'));
  EXPECT_EQ(b.mem.Ler16(destino + 4u), static_cast<std::uint16_t>('i'));
  EXPECT_EQ(b.mem.Ler16(destino + 6u), 0u) << "tres unidades + o terminador (nSize=4)";
  EXPECT_EQ(b.mem.Ler16(destino + 8u), 0xFFFFu) << "nada foi escrito depois do terminador";
}

TEST(AjudantesExtra, OStrlowerPoeEmMinusculasNoSitioEDevolveOMesmoPonteiro) {
  // `char *(*strlower)(char *psz)`: altera o argumento E devolve-o. Um teste que so
  // olhasse para o valor de retorno nao apanhava um `strlower` que devolvesse uma
  // COPIA -- e um chamador que compare ponteiros (ou que escreva no retorno) ficaria
  // a alterar a cadeia errada.
  Bancada b;
  const std::uint32_t onde = 0x80010000u;
  const std::string texto = "AbC-123-Zz";
  for (std::size_t k = 0; k < texto.size(); ++k) {
    b.mem.Escrever8(onde + k, static_cast<std::uint8_t>(texto[k]));
  }
  b.mem.Escrever8(onde + texto.size(), 0);
  b.cpu.Set(kR0, onde);
  EXPECT_EQ(b.Atender(brew_ajudantes::kAjudante_strlower), Atendimento::Implementado);
  EXPECT_EQ(b.cpu.Get(kR0), onde) << "devolve o MESMO ponteiro";
  std::string saida;
  for (std::size_t k = 0; k < texto.size(); ++k) {
    saida.push_back(static_cast<char>(b.mem.Ler8(onde + k)));
  }
  EXPECT_EQ(saida, std::string("abc-123-zz")) << "so A-Z; os digitos e o hifen ficam";
  // O NULO tem de continuar a ser RECUSADO, e nao devolver sucesso.
  const std::uint64_t faltas_antes = b.Faltas("AEEHelperFuncs[0x114] strlower");
  b.cpu.Set(kR0, 0);
  b.Atender(brew_ajudantes::kAjudante_strlower);
  EXPECT_EQ(b.Faltas("AEEHelperFuncs[0x114] strlower"), faltas_antes + 1)
      << "ponteiro nulo recusa COM O NOME";
}

TEST(AjudantesExtra, OSwapsTrocaOsBytesEOLswaplNaoEstaServido) {
  // `swaps` (0x130) NAO troca dois valores: troca os BYTES de um. Num guest little-endian
  // isso e o que le dados gravados em big-endian -- e eram 1646 chamadas (gof 1308, pbc 338)
  // sem resposta.
  Bancada b;
  for (const auto par : {std::pair<std::uint32_t, std::uint32_t>{0x1234, 0x3412},
                         {0x00FF, 0xFF00}, {0xFF00, 0x00FF}, {0x0000, 0x0000},
                         {0xABCD, 0xCDAB}}) {
    b.cpu.Set(kR0, par.first);
    EXPECT_EQ(b.Atender(brew_ajudantes::kAjudante_swaps), Atendimento::Implementado);
    EXPECT_EQ(b.cpu.Get(kR0), par.second) << "swaps(0x" << std::hex << par.first << ")";
  }
  // O `+0` do `swaps` e o `+4` de um `swapl` sao offsets DIFERENTES: o cabecalho
  // (`AEEStdLib.h:172-173`) lista `swapl` em 0x12c e `swaps` em 0x130. Um teste que so
  // olhasse para o nome nao apanhava uma troca entre os dois -- e por isso este fixa o offset.
  EXPECT_EQ(brew_ajudantes::kAjudante_swaps, 0x130u);
  EXPECT_EQ(DeclaracaoDoOffset(0x130)->nome, std::string("swaps"));
  EXPECT_EQ(DeclaracaoDoOffset(0x12C)->nome, std::string("swapl"));
}

TEST(AjudantesExtra, OSysfreeLibertaOBlocoDoHeapDoGuest) {
  // O `sysfree` (0x0BC) e o par do alocador do SISTEMA -- e nesta arvore esse alocador e
  // o MESMO do heap do guest. E por isso que o `SetupNativeImage` entrega o buffer pelo
  // heap: o jogo liberta-o por aqui (medido: 3 chamadas no `allstarcards`).
  Bancada b;
  const std::uint32_t p = b.alocador.Malloc(64);
  ASSERT_NE(p, 0u);
  const std::uint32_t antes = b.alocador.Alocado();
  b.cpu.Set(kR0, p);
  EXPECT_EQ(b.Atender(brew_ajudantes::kAjudante_sysfree), Atendimento::Implementado);
  EXPECT_LT(b.alocador.Alocado(), antes) << "o bloco voltou ao heap";
  EXPECT_EQ(b.alocador.Falhas(), 0u) << "era um endereco NOSSO";
  // `sysfree(0)` e legal e nao faz nada (como o `free(0)` do C).
  b.cpu.Set(kR0, 0);
  EXPECT_EQ(b.Atender(brew_ajudantes::kAjudante_sysfree), Atendimento::Implementado);
  EXPECT_EQ(b.alocador.Falhas(), 0u);
  // E um endereco que NAO e do heap nao se engole: conta.
  b.cpu.Set(kR0, 0x12345678u);
  b.Atender(brew_ajudantes::kAjudante_sysfree);
  EXPECT_EQ(b.alocador.Falhas(), 1u) << "um free de fora do heap tem de ficar visivel";
}

TEST(AjudantesExtra, OMemcmpNaoTerminaNoNulo) {
  // A DIFERENCA PARA O `strncmp` E O QUE ESTE TESTE MEDE: o `memcmp` compara os n
  // BYTES e **o NUL nao termina**. Com o `strncmp` no lugar dele um bloco com um NUL
  // no meio dava igualdade cedo demais -- e um jogo que compara magias de ficheiro
  // aceitaria o que devia recusar.
  Bancada b;
  const std::uint32_t a = kTexto, c = kTexto2;
  const std::uint8_t pa[5] = {'O', 'I', 0, 'z', 'z'};
  const std::uint8_t pc[5] = {'O', 'I', 0, 'z', 'y'};
  for (int i = 0; i < 5; ++i) {
    b.mem.Escrever8(a + static_cast<std::uint32_t>(i), pa[i]);
    b.mem.Escrever8(c + static_cast<std::uint32_t>(i), pc[i]);
  }
  b.cpu.Set(kR0, a);
  b.cpu.Set(kR1, c);
  b.cpu.Set(kR2, 5);
  EXPECT_EQ(b.Atender(brew_ajudantes::kAjudante_memcmp), Atendimento::Implementado);
  EXPECT_GT(b.cpu.Get(kR0), 0u) << "o 5.o byte difere ('z' contra 'y'): positivo";
  // Com 4 bytes (NUL incluido) os dois sao iguais -- e o `strncmp` pararia aqui.
  b.cpu.Set(kR0, a);
  b.cpu.Set(kR1, c);
  b.cpu.Set(kR2, 4);
  b.Atender(brew_ajudantes::kAjudante_memcmp);
  EXPECT_EQ(b.cpu.Get(kR0), 0u) << "nos primeiros 4 (com o NUL) sao iguais";
  // E o `strncmp` sobre os MESMOS 5 bytes para no NUL e diz igual -- a prova de que
  // os dois NAO sao a mesma coisa.
  b.cpu.Set(kR0, a);
  b.cpu.Set(kR1, c);
  b.cpu.Set(kR2, 5);
  b.Atender(brew_ajudantes::kAjudante_strncmp);
  EXPECT_EQ(b.cpu.Get(kR0), 0u) << "o strncmp para no NUL: e a diferenca medida";
}

// ---------------------------------------------------------------------------
// 0x0EC -- `char *memstr(const char *cpHaystack, const char *cpszNeedle, size_t nHaystackLen)`
// ---------------------------------------------------------------------------
TEST(AjudantesExtra, OMemstrAcabaNoLimiteENaoNoNulo) {
  // `AEEStdLib.h:156`. MEDIDO: o `peggle` pede-o **303 vezes** -- a falta mais
  // pedida da corrida dos 62, e o mesmo titulo tinha a segunda
  // (`IImageDecoder::GetBitmap`, 242).
  //
  // A DIFERENCA PARA O `strstr` (0x0E8) E O `n`: o palheiro NAO tem de ter zero
  // nenhum, e a busca acaba no limite que o TITULO declara. O teste prova-o dos
  // dois lados, e e por isso que o palheiro tem um zero A MEIO: a agulha esta
  // DEPOIS desse zero, onde o `strstr` parava.
  Bancada b;
  const std::uint32_t palheiro = kTexto, agulha = kTexto2;
  const std::uint8_t pb[8] = {'a', 'b', 0, 'c', 'd', 0, 'e', 'f'};
  const char* ag = "cd";
  for (int i = 0; i < 8; ++i) b.mem.Escrever8(palheiro + static_cast<std::uint32_t>(i), pb[i]);
  for (int i = 0; ag[i] != 0; ++i)
    b.mem.Escrever8(agulha + static_cast<std::uint32_t>(i), static_cast<std::uint8_t>(ag[i]));
  b.mem.Escrever8(agulha + 2, 0);

  // (1) DENTRO do limite: acha-a, mesmo com o zero pelo meio.
  b.cpu.Set(kR0, palheiro);
  b.cpu.Set(kR1, agulha);
  b.cpu.Set(kR2, 8);
  EXPECT_EQ(b.Atender(brew_ajudantes::kAjudante_memstr), Atendimento::Implementado);
  EXPECT_EQ(b.cpu.Get(kR0), palheiro + 3u) << "a agulha esta em 3..4, DEPOIS do zero em 2";

  // (2) O MESMO palheiro, com o limite a acabar antes da agulha: nao ha.
  b.cpu.Set(kR0, palheiro);
  b.cpu.Set(kR1, agulha);
  b.cpu.Set(kR2, 4);
  b.Atender(brew_ajudantes::kAjudante_memstr);
  EXPECT_EQ(b.cpu.Get(kR0), 0u) << "de 3 sobram 1 byte para uma agulha de 2: nao cabe";

  // (3) A AGULHA VAZIA casa no inicio -- a escolha declarada (como a libc, e a
  // mesma que a `ProcurarSubcadeia` do `strstr` ja faz).
  b.mem.Escrever8(agulha, 0);
  b.cpu.Set(kR0, palheiro);
  b.cpu.Set(kR1, agulha);
  b.cpu.Set(kR2, 8);
  b.Atender(brew_ajudantes::kAjudante_memstr);
  EXPECT_EQ(b.cpu.Get(kR0), palheiro);

  // (4) PONTEIRO NULO: recusa COM NOME (a regra P2), e zero no r0.
  b.cpu.Set(kR0, 0);
  b.cpu.Set(kR1, agulha);
  b.cpu.Set(kR2, 8);
  b.Atender(brew_ajudantes::kAjudante_memstr);
  EXPECT_EQ(b.cpu.Get(kR0), 0u);
  EXPECT_EQ(b.traco.ContagemFaltas().count("AEEHelperFuncs[0x0ec] memstr"), 1u)
      << "a recusa tem de dizer o NOME e o offset";
}
