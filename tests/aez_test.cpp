// tests/aez_test.cpp
//
// O AEZ (`.aez`) -- os recursos dos titulos BREW 3D (gof, rmp, pbc), num
// recipiente de registos com caminho absoluto e payload gzip (ou cru).
//
// Este ficheiro prova quatro coisas, por ordem de dependencia:
//
//   (a) o RECIPIENTE sintetico montado AQUI (gzip de bloco stored, escrito a
//       mao como o `pack_test.cpp` escreve o zlib), com um registo GUARDADO e um
//       comprimido, e as GUARDAS do parser (ficheiro vazio, nome nao
//       imprimivel, geometria que sai do ficheiro, payload que nao e gzip);
//   (b) o `res.aez` REAL do gof (277380): 182 registos, os 182 extraem, os
//       tamanhos declarados batem, e registos escolhidos batem byte a byte com
//       o censo em Python (sha256);
//   (c) os DOZE `.aez` do corpus (1 390 registos, 278 guardados) parseiam;
//   (d) a VFS serve os CAMINHOS QUE O GUEST PEDE -- os 37 recusados do gof e os
//       71 do rmp medidos no traco da bateria --, e nao serve formas que
//       ninguem pede.
//
// As fixtures reais NAO estao no repositorio: os testes que as abrem SALTAM
// quando a midia nao esta nesta maquina (o codigo de `pakz_test.cpp`).

#include <gtest/gtest.h>

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
#include "core/carga/aez.h"
#include "core/carga/png.h"  // Crc32DePng: o CRC-32 do rodape do gzip

using namespace zb2;
using namespace zb2::brew;

namespace {

// --- o montador sintetico ------------------------------------------------

void Escrever32(std::vector<std::uint8_t>* v, std::size_t onde, std::uint32_t valor) {
  for (int i = 0; i < 4; ++i) {
    (*v)[onde + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((valor >> (8 * i)) & 0xffu);
  }
}

// Um stream GZIP (RFC1952) com UM bloco deflate stored (RFC1951 3.2.4): o
// cabecalho de 10, o bloco `01 LEN NLEN`, e o rodape CRC32 + ISIZE. Nao ha
// codificador de gzip nesta arvore e nao passa a haver; o CRC-32 e o mesmo do
// PNG (`Crc32DePng`), o que faz deste montador uma prova do rodape.
std::vector<std::uint8_t> GzipDeBlocoStored(const std::vector<std::uint8_t>& dados) {
  std::vector<std::uint8_t> g = {0x1f, 0x8b, 0x08, 0x00, 0, 0, 0, 0, 0x00, 0xff};
  g.push_back(0x01);  // BFINAL=1, BTYPE=00 (stored)
  const std::uint16_t n = static_cast<std::uint16_t>(dados.size());
  g.push_back(static_cast<std::uint8_t>(n & 0xffu));
  g.push_back(static_cast<std::uint8_t>((n >> 8) & 0xffu));
  g.push_back(static_cast<std::uint8_t>((~n) & 0xffu));
  g.push_back(static_cast<std::uint8_t>(((~n) >> 8) & 0xffu));
  g.insert(g.end(), dados.begin(), dados.end());
  const std::uint32_t crc = Crc32DePng(dados.data(), dados.size());
  const std::uint32_t isize = static_cast<std::uint32_t>(dados.size());
  for (int i = 0; i < 4; ++i) g.push_back(static_cast<std::uint8_t>((crc >> (8 * i)) & 0xffu));
  for (int i = 0; i < 4; ++i) g.push_back(static_cast<std::uint8_t>((isize >> (8 * i)) & 0xffu));
  return g;
}

struct EntradaSintetica {
  std::string nome;
  std::vector<std::uint8_t> dados;
  bool guardada = false;
};

// Monta um AEZ com a geometria medida (core/carga/aez.h): registos colados,
// `[u8 len][nome][u32 descomprimido][u32 comprimido|0xffffffff][payload]`.
std::vector<std::uint8_t> MontarAez(const std::vector<EntradaSintetica>& entradas) {
  std::vector<std::uint8_t> out;
  for (const EntradaSintetica& e : entradas) {
    const std::vector<std::uint8_t> payload = e.guardada ? e.dados : GzipDeBlocoStored(e.dados);
    out.push_back(static_cast<std::uint8_t>(e.nome.size()));
    out.insert(out.end(), e.nome.begin(), e.nome.end());
    const std::size_t cabeca = out.size();
    out.resize(cabeca + 8, 0);
    Escrever32(&out, cabeca, static_cast<std::uint32_t>(e.dados.size()));
    Escrever32(&out, cabeca + 4,
               e.guardada ? aez_campos::kGuardado : static_cast<std::uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
  }
  return out;
}

std::vector<std::uint8_t> Padrao(std::size_t n, std::uint8_t semente) {
  std::vector<std::uint8_t> v(n);
  for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<std::uint8_t>(semente + i * 7u);
  return v;
}

// --- onde esta a midia ---------------------------------------------------

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
    if (std::filesystem::exists(r + "/277380/res.aez", ec)) return r;
  }
  return {};
}

bool Existe(const std::string& caminho) {
  std::error_code ec;
  return std::filesystem::exists(caminho, ec);
}

std::vector<std::uint8_t> LerFicheiro(const std::string& caminho) {
  std::ifstream f(caminho, std::ios::binary);
  return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)),
                                   std::istreambuf_iterator<char>());
}

}  // namespace

// ---------------------------------------------------------------------------
// (a) O recipiente sintetico e as guardas
// ---------------------------------------------------------------------------

TEST(Aez, RecipienteSinteticoGuardadoEComprimido) {
  const std::vector<std::uint8_t> cru = Padrao(300, 0x21);
  const std::vector<std::uint8_t> comprimido = Padrao(600, 0x09);
  const std::vector<std::uint8_t> bytes = MontarAez({
      {"/data/meshes/asteroid_part_04.aem", cru, true},
      {"/data/textures/main_menu.aei", comprimido, false},
      {"/pt.lang", Padrao(40, 0x55), false},
  });

  Aez a;
  std::string motivo;
  ASSERT_TRUE(a.Parse(bytes, &motivo)) << motivo;
  ASSERT_EQ(a.NumeroDeEntradas(), 3u);
  EXPECT_EQ(a.RegistosGuardados(), 1u);
  EXPECT_EQ(a.NomesRepetidos(), 0u);
  EXPECT_EQ(a.ListaDeEntradas()[0].nome, "/data/meshes/asteroid_part_04.aem");
  EXPECT_TRUE(a.ListaDeEntradas()[0].guardado);
  EXPECT_EQ(a.ListaDeEntradas()[0].tamanho_no_ficheiro, cru.size());
  EXPECT_FALSE(a.ListaDeEntradas()[1].guardado);
  // O nome tem ESPACO nos dumps reais (`alien weapon.aem`) e o casamento nao
  // distingue a caixa, como no `.pakz`.
  EXPECT_NE(a.Procurar("/PT.LANG"), nullptr);

  std::vector<std::uint8_t> saida;
  ASSERT_TRUE(a.Extrair(0u, &saida, &motivo)) << motivo;
  EXPECT_EQ(saida, cru);
  ASSERT_TRUE(a.Extrair(1u, &saida, &motivo)) << motivo;
  EXPECT_EQ(saida, comprimido);
  ASSERT_TRUE(a.ExtrairPorNome("/pt.lang", &saida, &motivo)) << motivo;
  EXPECT_EQ(saida, Padrao(40, 0x55));
}

TEST(Aez, RecusaComMotivo) {
  Aez a;
  std::string motivo;

  // Ficheiro vazio: nao ha registo nenhum que diga que esta bom.
  EXPECT_FALSE(a.Parse({}, &motivo));
  EXPECT_NE(motivo.find("curto"), std::string::npos) << motivo;

  // Nome nao imprimivel: um byte de controle e um registo lido no sitio errado.
  {
    std::vector<std::uint8_t> bytes = MontarAez({{"/pt.lang", Padrao(10, 1), false}});
    bytes[5] = 0x01;
    EXPECT_FALSE(a.Parse(bytes, &motivo));
    EXPECT_NE(motivo.find("nao imprimivel"), std::string::npos) << motivo;
  }

  // Comprimento de caminho que passa do fim.
  {
    std::vector<std::uint8_t> bytes = MontarAez({{"/pt.lang", Padrao(10, 1), false}});
    bytes[0] = 0xff;
    EXPECT_FALSE(a.Parse(bytes, &motivo));
    EXPECT_NE(motivo.find("passam dos"), std::string::npos) << motivo;
  }

  // Tamanho descomprimido ZERO.
  {
    std::vector<std::uint8_t> bytes = MontarAez({{"/pt.lang", Padrao(10, 1), false}});
    Escrever32(&bytes, 9, 0u);
    EXPECT_FALSE(a.Parse(bytes, &motivo));
    EXPECT_NE(motivo.find("tamanho descomprimido 0"), std::string::npos) << motivo;
  }

  // Um tamanho COMPRIMIDO que passa do fim do ficheiro: o registo seguinte
  // (ou o fim do ficheiro) fica no sitio errado.
  {
    std::vector<std::uint8_t> bytes = MontarAez({{"/pt.lang", Padrao(10, 1), false}});
    Escrever32(&bytes, 13, 0xfffff0u);  // 1 do comprimento + 8 do nome + 4
    EXPECT_FALSE(a.Parse(bytes, &motivo));
    EXPECT_NE(motivo.find("passa do fim do ficheiro"), std::string::npos) << motivo;
  }

  // Payload comprimido que nao e gzip: o 2.o campo diz "comprimido" e o corpo
  // nao comeca em 1f 8b.
  {
    std::vector<std::uint8_t> bytes = MontarAez({{"/pt.lang", Padrao(10, 1), false}});
    const std::size_t inicio = 1u + std::string("/pt.lang").size() + 8u;
    bytes[inicio] = 0x78;
    bytes[inicio + 1] = 0x01;
    EXPECT_FALSE(a.Parse(bytes, &motivo));
    EXPECT_NE(motivo.find("1f 8b"), std::string::npos) << motivo;
  }
}

TEST(Aez, GzipCorrompidoRecusaNaExtraccao) {
  std::vector<std::uint8_t> bytes = MontarAez({{"/x.aei", Padrao(500, 0x33), false}});
  Aez a;
  std::string motivo;
  ASSERT_TRUE(a.Parse(bytes, &motivo)) << motivo;
  // Estragar o CORPO (o parser nao olha para dentro do gzip; a prova e o rodape).
  const std::size_t inicio = 1u + std::string("/x.aei").size() + 8u;
  bytes[inicio + 14] = static_cast<std::uint8_t>(bytes[inicio + 14] ^ 0xffu);
  ASSERT_TRUE(a.Parse(bytes, &motivo)) << motivo;  // o recipiente continua bom
  std::vector<std::uint8_t> saida;
  EXPECT_FALSE(a.Extrair(0u, &saida, &motivo));
  EXPECT_NE(motivo.find("crc"), std::string::npos) << motivo;
}

// ---------------------------------------------------------------------------
// (b) O res.aez real do gof
// ---------------------------------------------------------------------------

TEST(Aez, ResAezDoGof182Registos) {
  const std::string raiz = RaizDosMods();
  if (raiz.empty()) {
    GTEST_SKIP() << "sem a pasta 277380 (gof) do corpus nesta maquina (ZB2_MODS) -- PULAR";
  }
  const std::string caminho = raiz + "/277380/res.aez";
  if (!Existe(caminho)) GTEST_SKIP() << "sem " << caminho << " -- PULAR";

  Aez a;
  std::string motivo;
  ASSERT_TRUE(a.Parse(LerFicheiro(caminho), &motivo)) << motivo;
  ASSERT_EQ(a.NumeroDeEntradas(), 182u);
  EXPECT_EQ(a.RegistosGuardados(), 55u);
  EXPECT_EQ(a.NomesRepetidos(), 0u);

  // Os 182 extraem, e o tamanho e o DECLARADO no registo.
  for (std::size_t i = 0; i < a.NumeroDeEntradas(); ++i) {
    std::vector<std::uint8_t> dados;
    ASSERT_TRUE(a.Extrair(i, &dados, &motivo)) << "registo " << i << ": " << motivo;
    EXPECT_EQ(dados.size(), a.ListaDeEntradas()[i].tamanho_descomprimido)
        << "registo " << a.ListaDeEntradas()[i].nome;
  }

  // Registos escolhidos, contra o censo em Python: o 1.o (nome com ESPACO), o
  // 1.o GUARDADO (1141 bytes crus), o `/pt.lang` (nome CRU, sem barra depois da
  // normalizacao) e o `main_menu.aei` (1 051 871 -- o caminho que o traco do gof
  // recusava 35x).
  const struct { const char* nome; std::size_t tamanho; const char* sha; } kAlvos[] = {
      {"/data/meshes/alien weapon.aem", 12883,
       "6434ef0e859fd087bd1b09528089dcee93a1e576bafe3ea0fda1f163c0a3039f"},
      {"/data/meshes/asteroid_part_04.aem", 1141,
       "4769255b09661e28336a82b36fececbe14137803dd20d65e27c437ab2c35a84f"},
      {"/pt.lang", 33140, "7b5e428f2347fc84f48466cf51e0c14970c03945b9f038bd6f72159e9d968433"},
      {"/data/textures/main_menu.aei", 1051871,
       "43232f5b0eaafd3830c2dc60eb86ab969bf4a25ec93b0161850fb1c1bc25d26f"},
  };
  for (const auto& alvo : kAlvos) {
    const EntradaDoAez* e = a.Procurar(alvo.nome);
    ASSERT_NE(e, nullptr) << "sem registo " << alvo.nome;
    std::vector<std::uint8_t> dados;
    ASSERT_TRUE(a.Extrair(*e, &dados, &motivo)) << alvo.nome << ": " << motivo;
    EXPECT_EQ(dados.size(), alvo.tamanho) << alvo.nome;
    EXPECT_EQ(Sha256Hex(dados.data(), dados.size()), std::string(alvo.sha)) << alvo.nome;
  }
}

// ---------------------------------------------------------------------------
// (c) Os doze .aez do corpus
// ---------------------------------------------------------------------------

TEST(Aez, OsDozeAezDoCorpusParseiam) {
  const std::string raiz = RaizDosMods();
  if (raiz.empty()) {
    GTEST_SKIP() << "sem a midia do corpus nesta maquina (ZB2_MODS) -- PULAR";
  }
  const struct { const char* rel; std::size_t registos; std::size_t guardados; } kAlvos[] = {
      {"277380/res.aez", 182, 55},        {"278282/cars.aez", 268, 4},
      {"278282/preview.aez", 38, 9},      {"278282/sky.aez", 12, 0},
      {"278282/sound.aez", 54, 54},       {"278282/ter.aez", 18, 0},
      {"278282/ter1.aez", 91, 0},         {"278282/ter2.aez", 164, 0},
      {"278282/ter3.aez", 113, 3},        {"278282/textures.aez", 30, 0},
      {"278282/tracks.aez", 103, 85},     {"280238/pbc_data.aez", 317, 68},
  };
  std::size_t total = 0;
  for (const auto& alvo : kAlvos) {
    const std::string caminho = raiz + "/" + alvo.rel;
    if (!Existe(caminho)) GTEST_SKIP() << "sem " << caminho << " -- PULAR";
    Aez a;
    std::string motivo;
    ASSERT_TRUE(a.Parse(LerFicheiro(caminho), &motivo)) << alvo.rel << ": " << motivo;
    EXPECT_EQ(a.NumeroDeEntradas(), alvo.registos) << alvo.rel;
    EXPECT_EQ(a.RegistosGuardados(), alvo.guardados) << alvo.rel;
    total += a.NumeroDeEntradas();
  }
  EXPECT_EQ(total, 1390u) << "os 12 recipientes do corpus tem 1390 registos (medido)";
}

// ---------------------------------------------------------------------------
// (d) A VFS serve o .aez
// ---------------------------------------------------------------------------

TEST(Aez, VfsServeOsCaminhosQueOGuestPede) {
  const std::string raiz = RaizDosMods();
  if (raiz.empty()) {
    GTEST_SKIP() << "sem a midia do corpus nesta maquina (ZB2_MODS) -- PULAR";
  }
  if (!Existe(raiz + "/277380/res.aez")) GTEST_SKIP() << "sem 277380/res.aez -- PULAR";

  Vfs vfs;
  vfs.Registar(raiz + "/277380");
  ASSERT_GE(vfs.Pacotes().size(), 1u);
  for (const Vfs::PacoteRegistado& p : vfs.Pacotes()) {
    if (p.ficheiro != "res.aez") continue;
    EXPECT_TRUE(p.motivo.empty()) << p.motivo;
    EXPECT_TRUE(p.aez);
    EXPECT_FALSE(p.pakz);
    EXPECT_EQ(p.entradas, 182u);
  }
  // O ficheiro SOLTO continua a abrir-se (o `.aez` nao deixa de existir).
  EXPECT_TRUE(vfs.Existe("res.aez"));
  // O ficheiro solto REAL da pasta, e o nome pedido com a CAIXA TROCADA (a
  // consola e FAT; o disco desta maquina nao). Medido no gof: 7 pedidos
  // recusados so por isto.
  EXPECT_TRUE(vfs.Existe("/galaxyonfire1_won.mp3"));
  EXPECT_TRUE(vfs.Existe("/GalaxyOnFire1_Won.mp3"));

  // OS CAMINHOS DO TRACO, um a um: os 30 caminhos que o gof pediu e que estao
  // num registo do `res.aez` (o censo em Python casou-os; os outros 7 sao os
  // `.mp3` soltos, com a caixa trocada).
  const char* kPedidos[] = {
      "/data/textures/main_menu.aei", "/data/textures/pre_game.aei", "/data/txt/stations.txt",
      "/data/txt/ships.txt",          "/data/txt/items.txt",          "/pt.lang",
      "/gb.lang",                     "/fx_boost_01.wav",             "/fx_pass_01.wav",
      "/fx_wpnemp_02.wav",            "/wpn_laser_05.wav",            "/wpn_rocket_04.wav",
      "/jet_interior06_30.wav",       "/fx_message_05.wav",           "/fx_hit_shield_02.wav",
      "/data/meshes/alien weapon.aem",
  };
  for (const char* p : kPedidos) EXPECT_TRUE(vfs.Existe(p)) << p;

  // As DUAS normalizacoes do BREW (barra invertida e barra inicial dupla).
  EXPECT_TRUE(vfs.Existe("\\data\\textures\\main_menu.aei"));
  EXPECT_TRUE(vfs.Existe("//data/textures/main_menu.aei"));

  std::vector<std::uint8_t> dados;
  std::string motivo;
  ASSERT_TRUE(vfs.Ler("/data/textures/main_menu.aei", &dados, &motivo)) << motivo;
  EXPECT_EQ(dados.size(), 1051871u);
  EXPECT_EQ(Sha256Hex(dados.data(), dados.size()),
            "43232f5b0eaafd3830c2dc60eb86ab969bf4a25ec93b0161850fb1c1bc25d26f");
  // O NOME CRU (sem barra) e o caso do `/pt.lang`: nao ha `<dir>/<nome>` para
  // ele, e e assim que o gof o pede.
  ASSERT_TRUE(vfs.Ler("/pt.lang", &dados, &motivo)) << motivo;
  EXPECT_EQ(dados.size(), 33140u);

  // A ORIGEM declarada: o pacote `res.aez`, e ele diz que e `.aez`.
  std::size_t pacote = 0, entrada = 0;
  ASSERT_TRUE(vfs.OrigemDe("/data/textures/main_menu.aei", &pacote, &entrada));
  EXPECT_EQ(vfs.Pacotes()[pacote].ficheiro, "res.aez");
  EXPECT_TRUE(vfs.Pacotes()[pacote].aez);

  // E AS FORMAS QUE NINGUEM PEDE NAO SE INVENTAM: o `<dir>/<caminho>` do
  // `.pakz` nao existe para o `.aez` (nenhum dos dois titulos medidos o pede).
  EXPECT_FALSE(vfs.Existe("res/data/tracks/t1.w3t"));
  EXPECT_FALSE(vfs.Existe("res/nao_existe.aei"));
  EXPECT_FALSE(vfs.Existe("/data/nao_existe.aei"));
}

TEST(Aez, VfsServeOTracksAEzDoRmp) {
  const std::string raiz = RaizDosMods();
  if (raiz.empty()) {
    GTEST_SKIP() << "sem a midia do corpus nesta maquina (ZB2_MODS) -- PULAR";
  }
  if (!Existe(raiz + "/278282/tracks.aez")) GTEST_SKIP() << "sem 278282 -- PULAR";

  Vfs vfs;
  vfs.Registar(raiz + "/278282");
  // Os 10 `.aez` da pasta entram todos, e nenhum e recusado.
  std::size_t aez = 0, registos = 0;
  for (const Vfs::PacoteRegistado& p : vfs.Pacotes()) {
    EXPECT_TRUE(p.motivo.empty()) << p.ficheiro << ": " << p.motivo;
    if (!p.aez) continue;
    ++aez;
    registos += p.entradas;
  }
  EXPECT_EQ(aez, 10u);
  EXPECT_EQ(registos, 891u);

  // OS 18 `.w3t` e os `.aei` que o traco do rmp pediu e recusou 71 vezes.
  for (int i = 1; i <= 18; ++i) {
    const std::string p = "/data/tracks/t" + std::to_string(i) + ".w3t";
    EXPECT_TRUE(vfs.Existe(p)) << p;
  }
  EXPECT_TRUE(vfs.Existe("/data/textures/tex_2d_shared.aei"));
  EXPECT_TRUE(vfs.Existe("/data/textures/tex_2d_mainmenu.aei"));
  EXPECT_TRUE(vfs.Existe("/data/textures/tex_2d_splash.aei"));
  EXPECT_TRUE(vfs.Existe("/data/sound/speaker/speech_checkpoint.wav"));
  EXPECT_FALSE(vfs.Existe("/data/tracks/t19.w3t"));

  std::vector<std::uint8_t> dados;
  std::string motivo;
  ASSERT_TRUE(vfs.Ler("/data/tracks/t1.w3t", &dados, &motivo)) << motivo;
  EXPECT_EQ(dados.size(), 8005u);
  EXPECT_EQ(Sha256Hex(dados.data(), dados.size()),
            "c7a2fefe8c7a6b416080b97a56613b5a204ab7d72cdd62eaa36c76cb6fd565d9");
}
