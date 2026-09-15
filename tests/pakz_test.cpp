// tests/pakz_test.cpp
//
// O PAKZ (`.pakz`) -- os assets dos titulos TTD, em LZMA_ALONE.
//
// Este ficheiro prova cinco coisas, por ordem de dependencia:
//
//   (a) o RECIPIENTE sintetico (montado no proprio teste com o liblzma real):
//       duas entradas, nomes com barra e nomes que transbordam os 40 bytes,
//       lidas entrada a entrada;
//   (b) as GUARDAS: assinatura, tabela que nao e multiplo de 64, dados que
//       saem do ficheiro, dados nao contiguos, stream LZMA truncado;
//   (c) o `resources.pakz` REAL da Alice (279386/280386): 793 entradas,
//       as 793 descomprimem, e entradas escolhidas batem byte a byte com o
//       censo em Python (sha256) -- incluindo os DOIS nomes de mais de 40
//       caracteres, que o zeebulator-upstream le truncados aos 40;
//   (d) os DEZ `.pakz` do corpus parseiam (contagem de entradas medida);
//   (e) a VFS serve o `.pakz` da Alice: `ani/touxiang1` directo, e debaixo
//       dos directorios servidos -- a regra declarada em `core/brew/vfs.h`.
//
// As fixtures reais NAO estao no repositorio: os testes que as abrem SALTAM
// quando a midia nao esta nesta maquina (o codigo de `pack_test.cpp`).

#include <gtest/gtest.h>

#include <lzma.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/brew/sha256.h"
#include "core/brew/vfs.h"
#include "core/carga/pakz.h"

using namespace zb2;
using namespace zb2::brew;

namespace {

// --- o montador sintetico ------------------------------------------------

std::vector<std::uint8_t> CodificarLzmaAlone(const std::vector<std::uint8_t>& conteudo) {
  lzma_options_lzma opcoes;
  if (lzma_lzma_preset(&opcoes, LZMA_PRESET_DEFAULT) != LZMA_OK) {
    throw std::runtime_error("lzma_lzma_preset falhou");
  }
  lzma_stream strm = LZMA_STREAM_INIT;
  if (lzma_alone_encoder(&strm, &opcoes) != LZMA_OK) {
    throw std::runtime_error("lzma_alone_encoder nao arrancou");
  }
  strm.next_in = conteudo.data();
  strm.avail_in = conteudo.size();
  std::vector<std::uint8_t> fora(conteudo.size() + 4096);
  strm.next_out = fora.data();
  strm.avail_out = fora.size();
  const lzma_ret ret = lzma_code(&strm, LZMA_FINISH);
  lzma_end(&strm);
  if (ret != LZMA_STREAM_END) throw std::runtime_error("lzma_code nao acabou");
  fora.resize(fora.size() - strm.avail_out);
  return fora;
}

void Escrever32(std::vector<std::uint8_t>* v, std::size_t onde, std::uint32_t valor) {
  for (int i = 0; i < 4; ++i) {
    (*v)[onde + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((valor >> (8 * i)) & 0xffu);
  }
}

struct FicheiroSintetico {
  std::string nome;
  std::vector<std::uint8_t> conteudo;
};

// Monta um PAKZ sintetico com a geometria medida (core/carga/pakz.h): cabecalho
// de 12, dados contiguos desde 12, tabela de registos de 64 no fim.
std::vector<std::uint8_t> MontarPakz(const std::vector<FicheiroSintetico>& ficheiros) {
  std::vector<std::vector<std::uint8_t>> streams;
  for (const auto& f : ficheiros) streams.push_back(CodificarLzmaAlone(f.conteudo));

  std::uint32_t cursor = pakz_campos::kCabecalho;
  std::vector<std::uint32_t> offsets;
  for (const auto& s : streams) {
    offsets.push_back(cursor);
    cursor += static_cast<std::uint32_t>(s.size());
  }
  const std::uint32_t tabela = cursor;
  const std::uint32_t tamanho_da_tabela = static_cast<std::uint32_t>(ficheiros.size()) * pakz_campos::kRegisto;

  std::vector<std::uint8_t> out(tabela + tamanho_da_tabela, 0);
  std::memcpy(out.data(), pakz_campos::kAssinatura, 4);
  Escrever32(&out, 4, tabela);
  Escrever32(&out, 8, tamanho_da_tabela);
  for (std::size_t i = 0; i < streams.size(); ++i) {
    std::memcpy(&out[offsets[i]], streams[i].data(), streams[i].size());
  }
  for (std::size_t i = 0; i < ficheiros.size(); ++i) {
    const std::size_t r = tabela + i * pakz_campos::kRegisto;
    std::memcpy(&out[r], ficheiros[i].nome.data(), ficheiros[i].nome.size());
    Escrever32(&out, r + pakz_campos::kOffsetDoOffset, offsets[i]);
    Escrever32(&out, r + pakz_campos::kOffsetDoTamanho, static_cast<std::uint32_t>(streams[i].size()));
  }
  return out;
}

std::vector<std::uint8_t> Padrao(std::size_t n, std::uint8_t semente) {
  std::vector<std::uint8_t> v(n);
  for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<std::uint8_t>(semente + i * 7u);
  return v;
}

// --- onde esta a midia ---------------------------------------------------

// A midia do corpus nao esta no repositorio. `ZB2_MODS` manda; os dois
// caminhos por omissao sao os das maquinas onde o corpus costuma viver (a pasta
// de trabalho do projeto e o disco externo).
std::string RaizDosMods() {
  std::vector<std::string> raizes;
  if (const char* env = std::getenv("ZB2_MODS")) {
    if (*env != '\0') raizes.push_back(env);
  } else {
    raizes.push_back("/home/rafaelfrequiao/projects/zeebo-lab/games/brew/mod");
    raizes.push_back("/media/rafaelfrequiao/8C5F-19E51/zeebo/ROMs/debug_nand/mod");
  }
  for (const std::string& r : raizes) {
    std::error_code ec;
    if (std::filesystem::exists(r + "/280386/resources.pakz", ec)) return r;
  }
  return {};
}

std::string CaminhoDoAlicePakz() {
  const std::string raiz = RaizDosMods();
  if (raiz.empty()) return {};
  const std::string caminho = raiz + "/280386/resources.pakz";
  std::error_code ec;
  if (!std::filesystem::exists(caminho, ec)) return {};
  return caminho;
}

std::string PastaDaAlice() {
  const std::string raiz = RaizDosMods();
  if (raiz.empty()) return {};
  const std::string pasta = raiz + "/280386";
  std::error_code ec;
  if (!std::filesystem::exists(pasta + "/alice.mod", ec)) return {};
  return pasta;
}

}  // namespace

// ---------------------------------------------------------------------------
// (a) O recipiente sintetico
// ---------------------------------------------------------------------------

TEST(Pakz, RecipienteSinteticoDeTresEntradas) {
  const std::vector<std::uint8_t> dados1 = Padrao(2048, 0x11);
  const std::vector<std::uint8_t> dados2 = {1, 2, 3, 4, 5};
  // O terceiro nome transborda os 40 bytes do campo (43 caracteres): o NUL e
  // que marca o fim, nao o byte 40 -- medido no alice/resources.pakz.
  const std::string nome_longo = "audio/tulgeyedgechoirgreensection_rough.mp3";

  const std::vector<std::uint8_t> bytes = MontarPakz(
      {{"ani/touxiang1", dados2}, {"xui/textures/ball.atitc", dados1}, {nome_longo, dados1}});

  Pakz p;
  std::string motivo;
  ASSERT_TRUE(p.Parse(bytes, &motivo)) << motivo;
  ASSERT_EQ(p.NumeroDeEntradas(), 3u);
  EXPECT_EQ(p.ListaDeEntradas()[0].nome, "ani/touxiang1");
  EXPECT_EQ(p.ListaDeEntradas()[1].nome, "xui/textures/ball.atitc");
  EXPECT_EQ(p.ListaDeEntradas()[2].nome, nome_longo);
  EXPECT_EQ(p.NomesNoCampoInteiro(), 1u);
  EXPECT_EQ(p.NomesRepetidos(), 0u);

  std::vector<std::uint8_t> saida;
  ASSERT_TRUE(p.Extrair(0u, &saida, &motivo)) << motivo;
  EXPECT_EQ(saida, dados2);
  ASSERT_TRUE(p.ExtrairPorNome("XUI/TEXTURES/BALL.ATITC", &saida, &motivo)) << motivo;
  EXPECT_EQ(saida, dados1);
  ASSERT_TRUE(p.ExtrairPorNome(nome_longo, &saida, &motivo)) << motivo;
  EXPECT_EQ(saida, dados1);

  const EntradaDoPakz* e = p.Procurar("ani/touxiang1");
  ASSERT_NE(e, nullptr);
  // O liblzma monta streams com tamanho DECLARADO como desconhecido: o campo
  // fica a zero e a Extrair cresce com teto (a volta acima ja conferiu o
  // conteudo inteiro). Os 7 487 reais declaram o tamanho e o campo vem cheio.
  EXPECT_EQ(e->tamanho_descomprimido, 0u);
}

TEST(Pakz, RecipienteSinteticoComNomesRepetidosConta) {
  const std::vector<std::uint8_t> bytes = MontarPakz(
      {{"a.bin", Padrao(100, 1)}, {"A.BIN", Padrao(100, 2)}, {"b.bin", Padrao(100, 3)}});
  Pakz p;
  std::string motivo;
  ASSERT_TRUE(p.Parse(bytes, &motivo)) << motivo;
  EXPECT_EQ(p.NomesRepetidos(), 1u);
  EXPECT_EQ(p.Procurar("a.bin")->offset, p.ListaDeEntradas()[0].offset) << "o PRIMEIRO ganha";
}

// ---------------------------------------------------------------------------
// (b) As guardas
// ---------------------------------------------------------------------------

TEST(Pakz, RecusaComMotivo) {
  Pakz p;
  std::string motivo;

  // Ficheiro mais pequeno que o cabecalho.
  std::vector<std::uint8_t> curto = {'P', 'A'};
  EXPECT_FALSE(p.Parse(curto, &motivo));
  EXPECT_NE(motivo.find("12"), std::string::npos) << motivo;

  // Assinatura errada.
  std::vector<std::uint8_t> bytes = MontarPakz({{"x", {1, 2, 3}}});
  bytes[0] = 'Q';
  EXPECT_FALSE(p.Parse(bytes, &motivo));
  EXPECT_NE(motivo.find("PACK"), std::string::npos) << motivo;

  // Tabela que nao e multiplo de 64.
  bytes = MontarPakz({{"x", {1, 2, 3}}});
  Escrever32(&bytes, 8, 65u);
  EXPECT_FALSE(p.Parse(bytes, &motivo));
  EXPECT_NE(motivo.find("64"), std::string::npos) << motivo;

  // Tabela que passa do fim do ficheiro.
  bytes = MontarPakz({{"x", {1, 2, 3}}});
  Escrever32(&bytes, 4, static_cast<std::uint32_t>(bytes.size() - 32));
  EXPECT_FALSE(p.Parse(bytes, &motivo));
  EXPECT_NE(motivo.find("acaba em"), std::string::npos) << motivo;

  // Dados nao contiguos: um buraco de 4 bytes entre a primeira e a segunda
  // entrada. O tamanho do registo continua certo; a geometria e que mentiu.
  bytes = MontarPakz({{"a.bin", Padrao(200, 1)}, {"b.bin", Padrao(200, 2)}});
  {
    const std::uint32_t tabela = [&] { std::uint32_t v; std::memcpy(&v, &bytes[4], 4); return v; }();
    std::uint32_t off_b = 0;
    std::memcpy(&off_b, &bytes[tabela + 1 * pakz_campos::kRegisto + pakz_campos::kOffsetDoOffset], 4);
    Escrever32(&bytes, tabela + 1 * pakz_campos::kRegisto + pakz_campos::kOffsetDoOffset, off_b + 4);
  }
  EXPECT_FALSE(p.Parse(bytes, &motivo));
  EXPECT_NE(motivo.find("contiguos"), std::string::npos) << motivo;

  // Entrada cujos dados passam do fim do ficheiro.
  bytes = MontarPakz({{"x", {1, 2, 3}}});
  const std::uint32_t tab = [&] { std::uint32_t v; std::memcpy(&v, &bytes[4], 4); return v; }();
  Escrever32(&bytes, tab + pakz_campos::kOffsetDoTamanho, 0xFFFFFF00u);
  EXPECT_FALSE(p.Parse(bytes, &motivo));
  EXPECT_NE(motivo.find("passam do fim"), std::string::npos) << motivo;

  // Nome sem NUL nos 56 bytes.
  bytes = MontarPakz({{"x", {1, 2, 3}}});
  const std::uint32_t tab3 = [&] { std::uint32_t v; std::memcpy(&v, &bytes[4], 4); return v; }();
  for (std::size_t i = 0; i < pakz_campos::kCampoDoNome; ++i) bytes[tab3 + i] = 'a';
  EXPECT_FALSE(p.Parse(bytes, &motivo));
  EXPECT_NE(motivo.find("NUL"), std::string::npos) << motivo;
}

TEST(Pakz, StreamLzmaTruncadoRecusaNaExtraccao) {
  std::vector<std::uint8_t> bytes = MontarPakz({{"x.bin", Padrao(5000, 7)}});
  Pakz p;
  std::string motivo;
  ASSERT_TRUE(p.Parse(bytes, &motivo)) << motivo;
  // Cortar o corpo do stream mantendo o tamanho declarado no registo: so a
  // extraccao pode apanhar isto.
  const std::uint32_t tab = [&] { std::uint32_t v; std::memcpy(&v, &bytes[4], 4); return v; }();
  std::uint32_t off = 0, tam = 0;
  std::memcpy(&off, &bytes[tab + pakz_campos::kOffsetDoOffset], 4);
  std::memcpy(&tam, &bytes[tab + pakz_campos::kOffsetDoTamanho], 4);
  for (std::uint32_t i = off + tam / 2; i < off + tam; ++i) bytes[i] = 0;
  ASSERT_TRUE(p.Parse(bytes, &motivo)) << motivo;  // o recipiente continua bom
  std::vector<std::uint8_t> saida;
  EXPECT_FALSE(p.Extrair(0u, &saida, &motivo));
  EXPECT_NE(motivo.find("LZMA"), std::string::npos) << motivo;
}

// ---------------------------------------------------------------------------
// (c) O alice/resources.pakz real
// ---------------------------------------------------------------------------

TEST(Pakz, AliceReal793EntradasTodasDescomprimem) {
  const std::string caminho = CaminhoDoAlicePakz();
  if (caminho.empty()) {
    GTEST_SKIP() << "sem pasta 280386 (alice) do corpus nesta maquina (ZB2_MODS) -- PULAR";
  }
  std::ifstream f(caminho, std::ios::binary);
  ASSERT_TRUE(f) << caminho;
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

  Pakz p;
  std::string motivo;
  ASSERT_TRUE(p.Parse(bytes, &motivo)) << motivo;
  ASSERT_EQ(p.NumeroDeEntradas(), 793u);
  EXPECT_EQ(p.NomesRepetidos(), 0u);
  // Os 2 nomes que transbordam os 40 bytes: contados, nao criterio.
  EXPECT_EQ(p.NomesNoCampoInteiro(), 2u);

  for (std::size_t i = 0; i < p.NumeroDeEntradas(); ++i) {
    std::vector<std::uint8_t> dados;
    ASSERT_TRUE(p.Extrair(i, &dados, &motivo)) << "entrada " << i << ": " << motivo;
    const EntradaDoPakz& e = p.ListaDeEntradas()[i];
    EXPECT_EQ(dados.size(), e.tamanho_descomprimido) << "entrada " << e.nome;
  }
}

TEST(Pakz, AliceRealEntradasEscolhidasBatemComOCenso) {
  const std::string caminho = CaminhoDoAlicePakz();
  if (caminho.empty()) {
    GTEST_SKIP() << "sem pasta 280386 (alice) do corpus nesta maquina (ZB2_MODS) -- PULAR";
  }
  std::ifstream f(caminho, std::ios::binary);
  ASSERT_TRUE(f) << caminho;
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

  Pakz p;
  std::string motivo;
  ASSERT_TRUE(p.Parse(bytes, &motivo)) << motivo;

  const struct { const char* nome; std::size_t tamanho; const char* sha; } kAlvos[] = {
      {"z1.lua", 10923, "e2f67499a606ed245a7d1ab236b5e8f9d6108db76e4d240240277c38cf2f14e6"},
      {"lua/config", 11800, "553d6a1e95a02b42e84039dd3b725ff30fe43beae9882a12d825297dfdec5410"},
      {"ani/touxiang1", 19, "dbbc4e8d11f8b6de6f7f3b50941d68c7310ddb8532ebf2b4feab5b76d867db30"},
      {"xui/mainmenu.xui", 3140, "412eae650dff50cf27817ca8c4c2808acf446efb6b03df9e484a027d082e1fbc"},
      {"img/tele", 2245, "3afd9a64c912657c3ba47792598442996ef1b6e2a54c7bb12963ab3c2c76b6e2"},
      // Os DOIS nomes que o zeebulator-upstream le truncados aos 40 bytes.
      // Medido: o NUL vive no byte 41/43, o nome inteiro casa com o pedido do
      // proprio .mod (strings de 280386__alice.mod.txt).
      {"audio/menu_outgame_maintheme_roughmix.mp3", 365612,
       "442652a2081e979691ec9c1561ed636da7453db9829710006ecba027a91588f8"},
      {"audio/tulgeyedgechoirgreensection_rough.mp3", 407617,
       "90bb2173ede44f293f8fe5d45d532b7cc0e98f1b0461ddd63909ab868ef78f5a"},
  };
  for (const auto& alvo : kAlvos) {
    const EntradaDoPakz* e = p.Procurar(alvo.nome);
    ASSERT_NE(e, nullptr) << "sem entrada " << alvo.nome;
    std::vector<std::uint8_t> dados;
    ASSERT_TRUE(p.Extrair(*e, &dados, &motivo)) << alvo.nome << ": " << motivo;
    EXPECT_EQ(dados.size(), alvo.tamanho) << alvo.nome;
    EXPECT_EQ(Sha256Hex(dados.data(), dados.size()), std::string(alvo.sha)) << alvo.nome;
  }
}

// ---------------------------------------------------------------------------
// (d) Os dez .pakz do corpus parseiam
// ---------------------------------------------------------------------------

TEST(Pakz, OsDezPakzDoCorpusParseiam) {
  const std::string raiz = RaizDosMods();
  if (raiz.empty()) {
    GTEST_SKIP() << "sem a midia do corpus nesta maquina (ZB2_MODS) -- PULAR";
  }
  const struct { const char* rel; std::size_t entradas; } kAlvos[] = {
      {"277534/resources.pakz", 298},  {"278212/resources.pakz", 323},
      {"278283/resources.pakz", 1117}, {"278738/resources.pakz", 368},
      {"279159/resources.pakz", 401},  {"279380/resources.pakz", 474},
      {"279382/resources.pakz", 1048}, {"280386/resources.pakz", 793},
      {"280634/resources.pakz", 1373}, {"280647/resources.pakz", 1292}};
  std::size_t total = 0;
  for (const auto& alvo : kAlvos) {
    const std::string caminho = raiz + "/" + alvo.rel;
    std::ifstream f(caminho, std::ios::binary);
    ASSERT_TRUE(f) << caminho;
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    Pakz p;
    std::string motivo;
    ASSERT_TRUE(p.Parse(bytes, &motivo)) << alvo.rel << ": " << motivo;
    EXPECT_EQ(p.NumeroDeEntradas(), alvo.entradas) << alvo.rel;
    total += p.NumeroDeEntradas();
  }
  EXPECT_EQ(total, 7487u) << "os 10 pacotes do corpus tem 7487 entradas (medido)";
}

// ---------------------------------------------------------------------------
// (e) A VFS serve o .pakz da Alice
// ---------------------------------------------------------------------------

TEST(Pakz, VfsServeOAlicePakz) {
  const std::string pasta = PastaDaAlice();
  if (pasta.empty()) {
    GTEST_SKIP() << "sem pasta 280386 (alice) do corpus nesta maquina (ZB2_MODS) -- PULAR";
  }
  Vfs vfs;
  vfs.Registar(pasta);
  ASSERT_GE(vfs.Pacotes().size(), 1u);
  for (const Vfs::PacoteRegistado& p : vfs.Pacotes()) {
    EXPECT_TRUE(p.motivo.empty()) << p.ficheiro << ": " << p.motivo;
    EXPECT_TRUE(p.pakz) << p.ficheiro << ": esperava .pakz na pasta da Alice";
  }
  EXPECT_TRUE(vfs.Existe("resources.pakz"));  // o ficheiro solto continua a abrir

  // A regra do .pakz: o caminho da entrada directo (como o motor TTD pede),
  // e debaixo dos directorios servidos (o stem do .mod e o stem do pacote).
  EXPECT_TRUE(vfs.Existe("ani\\touxiang1"));
  EXPECT_TRUE(vfs.Existe("alice/ani/touxiang1"));
  EXPECT_TRUE(vfs.Existe("resources/ani/touxiang1"));
  EXPECT_TRUE(vfs.Existe("lua/config"));
  EXPECT_TRUE(vfs.Existe("xui/mainmenu.xui"));
  // Um nome solto da tabela so resolve com o directorio servido a frente.
  EXPECT_FALSE(vfs.Existe("z1.lua"));
  EXPECT_TRUE(vfs.Existe("alice/z1.lua"));
  // Quem nao esta no indice nao existe -- o teste do "nao existe".
  EXPECT_FALSE(vfs.Existe("ani/nao_existe"));
  EXPECT_FALSE(vfs.Existe("alice/nao_existe"));

  std::vector<std::uint8_t> dados;
  std::string motivo;
  ASSERT_TRUE(vfs.Ler("ani/touxiang1", &dados, &motivo)) << motivo;
  EXPECT_EQ(dados.size(), 19u);
  EXPECT_EQ(Sha256Hex(dados.data(), dados.size()),
            "dbbc4e8d11f8b6de6f7f3b50941d68c7310ddb8532ebf2b4feab5b76d867db30");

  ASSERT_TRUE(vfs.Ler("alice/lua/config", &dados, &motivo)) << motivo;
  EXPECT_EQ(dados.size(), 11800u);

  // E a origem declarada: pacote 0, e o pacote diz que e .pakz.
  std::size_t pacote = 0, entrada = 0;
  ASSERT_TRUE(vfs.OrigemDe("ani/touxiang1", &pacote, &entrada));
  EXPECT_EQ(vfs.Pacotes()[pacote].ficheiro, "resources.pakz");
  EXPECT_TRUE(vfs.Pacotes()[pacote].pakz);
}
