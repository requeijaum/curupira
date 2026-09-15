// tests/pack_test.cpp
//
// O PACK (`.pkg`) E O INFLATE -- a familia Neo Geo.
//
// Este ficheiro prova quatro coisas, e a ordem e a de dependencia:
//
//   (a) o INFLATE das tres formas de bloco do RFC1951 (stored, fixed, dynamic),
//       sobre TRES streams do MESMO texto: se um caminho estiver errado, os tres
//       bytes nao batem;
//   (b) o PACK sintetico: um recipiente MONTADO no proprio teste, com duas
//       entradas, lido entrada a entrada;
//   (c) o `boot.pkg` REAL do corpus (SALTA se a midia nao estiver nesta maquina,
//       como os `RecursosDeVerdade` -- um teste que passa sem ter corrido e pior
//       do que um teste vermelho);
//   (d) o CAMINHO DO GUEST: `karnovr/boot.rom` resolvido pela VFS, 8192 bytes.
//
// ---------------------------------------------------------------------------
// AS FIXTURES
// ---------------------------------------------------------------------------
// Os tres streams descomprimem para os MESMOS 1158 bytes de texto (um paragrafo
// em portugues repetido tres vezes). O sha256 desse texto, medido com o `zlib` do
// Python sobre o mesmo paragrafo, e o criterio:
//
//   da10a318a84f862d33f042f4de198f121d2cc3110f3c1797cb72fba93cac75ff
//
// O stream do nivel 0 (stored) NAO foi usado como veio: o hexadecimal que o autor
// das fixtures passou estava TRUNCADO -- 784 bytes em vez de 1169, com o campo
// `LEN` a declarar 1158 e o adler32 cortado a 3 bytes (o `zlib` do Python recusa-o
// com "incomplete or truncated stream"). A fixture deste ficheiro foi RECONSTRUIDA
// a partir do mesmo paragrafo, com o `LEN`/`NLEN` certos e o adler32 BIG-ENDIAN
// que o RFC1950 exige (e o unico campo do stream que nao vai LSB primeiro -- o
// descompressor deste projeto recusou os 9 `.pkg` reais, todos bons, por o ler
// como os outros, e este teste e o que fixa a regra). O que se conservou foi o
// proposito da fixture: exercitar o caminho `stored` com um tamanho de 1158
// bytes, que atravessa a fronteira do bloco.
//
// ---------------------------------------------------------------------------
// O QUE JA ESTAVA MEDIDO ANTES DESTE TESTE EXISTIR
// ---------------------------------------------------------------------------
// Um censo independente, em Python com `zlib`, sobre os 9 `.pkg` reais do corpus
// (127 entradas) deu, por entrada: nome, tamanho comprimido, tamanho
// descomprimido, offset e o sha256 do descomprimido. As 127 linhas do censo e as
// 127 linhas do leitor C++ batem campo a campo. O que este ficheiro prova e o
// mesmo, mas DENTRO do `ctest`: nao depende de ninguem ter guardado o censo.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/brew/arquivo.h"
#include "core/brew/sha256.h"
#include "core/brew/vfs.h"
#include "core/carga/inflate.h"
#include "core/carga/pack.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"

using namespace zb2;
using namespace zb2::brew;

namespace {

// --- as tres fixtures do inflate ------------------------------------------

// Nivel 9: arvore de Huffman DINAMICA (BTYPE=10), 246 bytes.
const char* kZlibNivel9 =
    "78daed91d16dc3300c4457e100850768be8202dda00330126db3904497923c7f8f6d027487063060cae6dd3d5257623f"
    "cd85b25095ac890d651f4287dba70ca3c63dc98cffb332b96cceaf28495b1f3eab34f47423bc9da94337b3380aa7533c"
    "7316755be88d3353d1b633650b17edd00da9617cd84392acade28ade663864ddec858452a8572e831f9222273b81ceea"
    "0ffbd7c4f7e8b0b9d047a5063207554fae0862aa31980269934be0df1b8a02074fe29b242e3be21a83f7dd1462e40575"
    "2c05019426a027404764ae02e71ee17f57715f9335106b1b983e3cacecc03dd87f0dc358dc31d542d7e70d3c6fe09fdf"
    "c037604e9d1a";

// Nivel 1: arvore FIXA (BTYPE=01), 249 bytes.
const char* kZlibNivel1 =
    "7801ed93516ac4300c44af3207287b80f66b29f4063d806a2b898a6da5729cf377bcec42efd0854064a219bd91c91512"
    "a787222baa664be22cfba1d8c3bff57034e949c7fc3eaa20740d7965096bfd8851b5b1a73bf80e41a76e640d16815323"
    "4b560bbfe05db2a058db04d9a78b75ea0eadd378f78724795b348cbdcd913cdbea2f50a4a95ea41cf290143d25403aaf"
    "37f69f41abd9e1e382cf8a46b220554f611c24a833981169d5b7897f6f28461c3e49be3449d938ae09793fdc28e6bc49"
    "3d97c2014883d083d9b89eac8bd2b94ffebfabb8afc91b89ad1d4c3f3dbc6cc4dde57628cc2ed008a6bae0fabc81e70d"
    "fcf37fe017604e9d1a";

// Nivel 0: bloco STORED (BTYPE=00), 1169 bytes. Ver o cabecalho do ficheiro.
const char* kZlibNivel0 =
    "780101860479fb41206172766f7265206465206d65646963616f2064657374652070726f6a65746f206e617363657520"
    "646520756d612072656772613a20756d20696e737472756d656e746f20736f20656e7472612073652070756465722073"
    "6572207665726461646569726f2e2043616461206c696e686120646f207265676973746f2074656d20646520706f6465"
    "722073657220636f6e666572696461206e6f20636f6469676f2c206520636164612066616c74612074656d206465206c"
    "65766172206f206e6f6d65206465207175656d2066616c746f752e20556d206e756d65726f206573637269746f206120"
    "6d616f20646976657267653b20756d206e756d65726f206c69646f20646f206361626563616c686f2c206e616f2e2046"
    "6f6920657374612061206c6963616f2071756520637573746f752073657465206465666569746f7320646520696e7374"
    "72756d656e746f206520756d6120726f6e646120696e74656972612061206f6c68617220706172612061206c69737461"
    "206572726164612e2041206172766f7265206465206d65646963616f2064657374652070726f6a65746f206e61736365"
    "7520646520756d612072656772613a20756d20696e737472756d656e746f20736f20656e747261207365207075646572"
    "20736572207665726461646569726f2e2043616461206c696e686120646f207265676973746f2074656d20646520706f"
    "6465722073657220636f6e666572696461206e6f20636f6469676f2c206520636164612066616c74612074656d206465"
    "206c65766172206f206e6f6d65206465207175656d2066616c746f752e20556d206e756d65726f206573637269746f20"
    "61206d616f20646976657267653b20756d206e756d65726f206c69646f20646f206361626563616c686f2c206e616f2e"
    "20466f6920657374612061206c6963616f2071756520637573746f752073657465206465666569746f7320646520696e"
    "737472756d656e746f206520756d6120726f6e646120696e74656972612061206f6c68617220706172612061206c6973"
    "7461206572726164612e2041206172766f7265206465206d65646963616f2064657374652070726f6a65746f206e6173"
    "63657520646520756d612072656772613a20756d20696e737472756d656e746f20736f20656e74726120736520707564"
    "657220736572207665726461646569726f2e2043616461206c696e686120646f207265676973746f2074656d20646520"
    "706f6465722073657220636f6e666572696461206e6f20636f6469676f2c206520636164612066616c74612074656d20"
    "6465206c65766172206f206e6f6d65206465207175656d2066616c746f752e20556d206e756d65726f20657363726974"
    "6f2061206d616f20646976657267653b20756d206e756d65726f206c69646f20646f206361626563616c686f2c206e61"
    "6f2e20466f6920657374612061206c6963616f2071756520637573746f752073657465206465666569746f7320646520"
    "696e737472756d656e746f206520756d6120726f6e646120696e74656972612061206f6c68617220706172612061206c"
    "69737461206572726164612e20604e9d1a";

constexpr std::size_t kTamanhoDoTexto = 1158;
constexpr const char* kSha256DoTexto =
    "da10a318a84f862d33f042f4de198f121d2cc3110f3c1797cb72fba93cac75ff";

std::uint8_t ValorDoHex(char c) {
  if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0');
  if (c >= 'a' && c <= 'f') return static_cast<std::uint8_t>(c - 'a' + 10);
  return static_cast<std::uint8_t>(c - 'A' + 10);
}

std::vector<std::uint8_t> DeHex(const char* hex) {
  const std::string s(hex);
  EXPECT_EQ(s.size() % 2, 0u);
  std::vector<std::uint8_t> v;
  v.reserve(s.size() / 2);
  for (std::size_t i = 0; i + 1 < s.size(); i += 2) {
    v.push_back(static_cast<std::uint8_t>((ValorDoHex(s[i]) << 4) | ValorDoHex(s[i + 1])));
  }
  return v;
}

void Escrever32(std::vector<std::uint8_t>* v, std::size_t onde, std::uint32_t valor) {
  for (int i = 0; i < 4; ++i) {
    (*v)[onde + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((valor >> (8 * i)) & 0xffu);
  }
}

// Um stream zlib com UM bloco stored (RFC1950 + RFC1951 3.2.4). E o que monta o
// recipiente sintetico: assim o teste tem um codificador proprio, e nao depende
// de um `zlib` do hospedeiro para o qual nao ha dependencia nenhuma neste
// projeto.
std::vector<std::uint8_t> ZlibStored(const std::vector<std::uint8_t>& dados) {
  EXPECT_LE(dados.size(), 65535u);
  std::vector<std::uint8_t> v;
  v.push_back(0x78);  // CMF: CM=8, CINFO=7
  v.push_back(0x01);  // FLG: (0x78<<8|0x01) % 31 == 0
  v.push_back(0x01);  // BFINAL=1, BTYPE=00
  const std::uint16_t n = static_cast<std::uint16_t>(dados.size());
  v.push_back(static_cast<std::uint8_t>(n & 0xffu));
  v.push_back(static_cast<std::uint8_t>((n >> 8) & 0xffu));
  v.push_back(static_cast<std::uint8_t>((~n) & 0xffu));
  v.push_back(static_cast<std::uint8_t>(((~n) >> 8) & 0xffu));
  v.insert(v.end(), dados.begin(), dados.end());
  const std::uint32_t adler = Adler32(dados.data(), dados.size());
  // BIG-ENDIAN: e o unico campo do stream que nao vai LSB primeiro (RFC1950, 2.2).
  for (int i = 3; i >= 0; --i) v.push_back(static_cast<std::uint8_t>((adler >> (8 * i)) & 0xffu));
  return v;
}

struct EntradaParaMontar {
  std::string nome;
  std::vector<std::uint8_t> comprimido;  // um stream zlib
  std::uint32_t descomprimido = 0;
};

// Monta um PACK com as entradas dadas, com o layout da medicao: cabecalho de
// 268 bytes, registos de 20, dados contiguos, e a tabela de nomes no fim.
std::vector<std::uint8_t> MontarPack(const std::vector<EntradaParaMontar>& entradas) {
  const std::size_t count = entradas.size();
  const std::size_t inicio_dos_dados = pack_campos::kCabecalho + count * pack_campos::kRegisto;
  std::size_t total = inicio_dos_dados;
  for (const EntradaParaMontar& e : entradas) total += e.comprimido.size();

  std::vector<std::uint8_t> nomes;
  for (const EntradaParaMontar& e : entradas) {
    std::string n = e.nome;
    n.resize(pack_campos::kNome, '\0');
    for (char c : n) nomes.push_back(static_cast<std::uint8_t>(c));
  }
  const std::vector<std::uint8_t> tabela = ZlibStored(nomes);

  std::vector<std::uint8_t> v(total + tabela.size(), 0);
  v[0] = 'P'; v[1] = 'A'; v[2] = 'C'; v[3] = 'K';
  Escrever32(&v, 4, static_cast<std::uint32_t>(count));
  Escrever32(&v, 8, static_cast<std::uint32_t>(total));
  std::size_t onde = inicio_dos_dados;
  for (std::size_t i = 0; i < count; ++i) {
    const std::size_t r = pack_campos::kCabecalho + i * pack_campos::kRegisto;
    Escrever32(&v, r + 0, pack_campos::kConstante);
    Escrever32(&v, r + 4, 0x11223344u);  // hash: lido e nao usado
    Escrever32(&v, r + 8, static_cast<std::uint32_t>(entradas[i].comprimido.size()));
    Escrever32(&v, r + 12, entradas[i].descomprimido);
    Escrever32(&v, r + 16, static_cast<std::uint32_t>(onde));
    for (std::size_t b = 0; b < entradas[i].comprimido.size(); ++b) v[onde + b] = entradas[i].comprimido[b];
    onde += entradas[i].comprimido.size();
  }
  for (std::size_t b = 0; b < tabela.size(); ++b) v[total + b] = tabela[b];
  return v;
}

std::vector<std::uint8_t> Padrao(std::size_t n, std::uint8_t semente) {
  std::vector<std::uint8_t> v(n);
  for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<std::uint8_t>(semente + i * 7u);
  return v;
}

// --- onde esta a midia do corpus -----------------------------------------

// A midia do corpus nao esta no repositorio. O primeiro sitio onde os `.pkg`
// desta familia existem e a pasta de trabalho do projeto (`zeebo-lab`); o
// segundo e o disco externo do corpus completo. `ZB2_MODS` ganha aos dois --
// e e o que a corrida de regressao exporta.
std::string RaizDosMods() {
  // `ZB2_MODS` MANDA, quando esta posto: se o operador disse onde esta a midia e
  // ali nao ha os ficheiros, o teste SALTA -- e nao vai a um caminho por omissao
  // desta maquina buscar uns ficheiros que nao sao os que ele pediu. (E o que
  // torna o salto verificavel: `ZB2_MODS=/nao/existe` tem de dar SALTADO.)
  std::vector<std::string> raizes;
  if (const char* env = std::getenv("ZB2_MODS")) {
    if (*env != '\0') raizes.push_back(env);
  } else {
    raizes.push_back("/home/rafaelfrequiao/projects/zeebo-lab/games/brew/mod");
    raizes.push_back("/media/rafaelfrequiao/8C5F-19E51/zeebo/ROMs/debug_nand/mod");
  }
  for (const std::string& r : raizes) {
    std::error_code ec;
    if (std::filesystem::exists(r + "/279126/boot.pkg", ec)) return r;
  }
  return {};
}

// Vazio quando o ficheiro nao esta nesta maquina: e o SINAL de saltar. Devolver
// um caminho que nao existe faria o teste FALHAR em vez de saltar -- e um teste
// que falha por falta de midia e tao mau como um que passa por falta dela.
std::string CaminhoDoBootPkg() {
  const std::string raiz = RaizDosMods();
  if (raiz.empty()) return {};
  const std::string caminho = raiz + "/279126/boot.pkg";
  std::error_code ec;
  if (!std::filesystem::exists(caminho, ec)) return {};
  return caminho;
}

std::string PastaDoKarnovr() {
  const std::string raiz = RaizDosMods();
  if (raiz.empty()) return {};
  const std::string pasta = raiz + "/279126";
  std::error_code ec;
  if (!std::filesystem::exists(pasta + "/karnovr.pkg", ec)) return {};
  return pasta;
}

}  // namespace

// ---------------------------------------------------------------------------
// (a) O INFLATE: as tres formas de bloco
// ---------------------------------------------------------------------------

TEST(Pack, InflateDasTresFormasDeBlocoDaoOMesmoTexto) {
  const std::vector<std::uint8_t> nivel9 = DeHex(kZlibNivel9);
  const std::vector<std::uint8_t> nivel1 = DeHex(kZlibNivel1);
  const std::vector<std::uint8_t> nivel0 = DeHex(kZlibNivel0);
  // Os tres tamanhos foram medidos com o `zlib` do Python. Um deles trocado e um
  // teste que nao mede o que diz medir.
  ASSERT_EQ(nivel9.size(), 246u);
  ASSERT_EQ(nivel1.size(), 249u);
  ASSERT_EQ(nivel0.size(), 1169u);

  std::vector<std::uint8_t> a, b, c;
  std::string ma, mb, mc;
  ASSERT_TRUE(Inflar(nivel9.data(), nivel9.size(), &a, &ma)) << ma;
  ASSERT_TRUE(Inflar(nivel1.data(), nivel1.size(), &b, &mb)) << mb;
  ASSERT_TRUE(Inflar(nivel0.data(), nivel0.size(), &c, &mc)) << mc;

  EXPECT_EQ(a.size(), kTamanhoDoTexto);
  EXPECT_EQ(b.size(), kTamanhoDoTexto);
  EXPECT_EQ(c.size(), kTamanhoDoTexto);
  EXPECT_EQ(a, b) << "o bloco FIXO nao deu o mesmo texto que o DINAMICO";
  EXPECT_EQ(a, c) << "o bloco STORED nao deu o mesmo texto que o DINAMICO";
  EXPECT_EQ(brew::Sha256Hex(a.data(), a.size()), std::string(kSha256DoTexto));
  // As tres arvores diferentes dao bytes identicos: uma que estivesse errada
  // (um comprimento, uma distancia, a ordem dos comprimentos de codigo) daria
  // aqui, e nao num "descomprimiu".

  // E o `teto` recusa em vez de deixar o hospedeiro encher-se.
  std::vector<std::uint8_t> d;
  std::string motivo;
  EXPECT_FALSE(Inflar(nivel9.data(), nivel9.size(), &d, &motivo, kTamanhoDoTexto - 1));
  EXPECT_NE(motivo.find("teto"), std::string::npos) << motivo;
  // O consumo exacto: quem le de um recipiente maior confere com isto.
  std::size_t consumido = 0;
  ASSERT_TRUE(Inflar(nivel9.data(), nivel9.size(), &d, &motivo, 0, &consumido)) << motivo;
  EXPECT_EQ(consumido, nivel9.size());
  ASSERT_TRUE(Inflar(nivel0.data(), nivel0.size(), &d, &motivo, 0, &consumido)) << motivo;
  EXPECT_EQ(consumido, nivel0.size());
}

TEST(Pack, InflateRecusaComMotivo) {
  const std::vector<std::uint8_t> bom = DeHex(kZlibNivel9);
  std::vector<std::uint8_t> saida;
  std::string motivo;

  // Cabecalho curto.
  EXPECT_FALSE(Inflar(bom.data(), 1, &saida, &motivo));
  EXPECT_NE(motivo.find("2"), std::string::npos) << motivo;

  // CM errado: 0x79 tem (CMF & 0x0f) == 9.
  {
    std::vector<std::uint8_t> v = bom;
    v[0] = 0x79;
    v[1] = 0x01;
    EXPECT_FALSE(Inflar(v.data(), v.size(), &saida, &motivo));
    EXPECT_NE(motivo.find("CM="), std::string::npos) << motivo;
  }
  // O resto do cabecalho tem de ser multiplo de 31.
  {
    std::vector<std::uint8_t> v = bom;
    v[1] = 0x00;
    EXPECT_FALSE(Inflar(v.data(), v.size(), &saida, &motivo));
    EXPECT_NE(motivo.find("31"), std::string::npos) << motivo;
  }
  // FDICT ligado: nao ha dicionario em nenhum ficheiro medido, e inventar um
  // seria descomprimir dados errados.
  {
    std::vector<std::uint8_t> v = bom;
    // FDICT (bit 0x20) ligado E o resto do cabecalho ainda multiplo de 31:
    // 0x78<<8 | 0x20 = 30752 = 31*992. Um FLG que falhe o modulo de 31 seria
    // recusado pela guarda anterior, e o teste estaria a medir a guarda errada.
    v[1] = 0x20;
    EXPECT_FALSE(Inflar(v.data(), v.size(), &saida, &motivo));
    EXPECT_NE(motivo.find("FDICT"), std::string::npos) << motivo;
  }
  // BTYPE=11 e reservado. O primeiro byte apos o cabecalho tem os 3 bits do
  // bloco: 0x07 = BFINAL=1, BTYPE=3.
  {
    std::vector<std::uint8_t> v = bom;
    v[2] = 0x07;
    EXPECT_FALSE(Inflar(v.data(), v.size(), &saida, &motivo));
    EXPECT_NE(motivo.find("BTYPE=3"), std::string::npos) << motivo;
  }
  // Stream cortado a meio dos dados.
  EXPECT_FALSE(Inflar(bom.data(), bom.size() - 40, &saida, &motivo));
  EXPECT_FALSE(motivo.empty());

  // Adler32 errado: o stream esta bom, o ultimo byte do resumo nao.
  {
    std::vector<std::uint8_t> v = bom;
    v[v.size() - 1] = static_cast<std::uint8_t>(v[v.size() - 1] ^ 0xffu);
    EXPECT_FALSE(Inflar(v.data(), v.size(), &saida, &motivo));
    EXPECT_NE(motivo.find("adler32"), std::string::npos) << motivo;
  }
  // Bloco stored com LEN e NLEN que nao sao complementares.
  {
    std::vector<std::uint8_t> v = DeHex(kZlibNivel0);
    v[6] = static_cast<std::uint8_t>(v[6] ^ 0x01u);  // mexe no NLEN
    EXPECT_FALSE(Inflar(v.data(), v.size(), &saida, &motivo));
    EXPECT_NE(motivo.find("complementares"), std::string::npos) << motivo;
  }
  // E o adler32 tem de ser recalculado: o valor a big-endian da fixture stored.
  {
    std::vector<std::uint8_t> v = DeHex(kZlibNivel0);
    const std::size_t n = v.size();
    EXPECT_EQ(v[n - 4], 0x60u);  // ((texto) adler = 0x604e9d1a, MSB primeiro)
    EXPECT_EQ(v[n - 3], 0x4eu);
    EXPECT_EQ(v[n - 2], 0x9du);
    EXPECT_EQ(v[n - 1], 0x1au);
  }
}

// ---------------------------------------------------------------------------
// (b) O PACK sintetico
// ---------------------------------------------------------------------------

TEST(Pack, RecipienteSinteticoDeDuasEntradas) {
  const std::vector<std::uint8_t> rom = Padrao(4096, 0x11);
  const std::vector<std::uint8_t> texto = DeHex(kZlibNivel9);  // ja e um stream zlib
  std::vector<std::uint8_t> texto_desco(0);
  {
    std::string m;
    ASSERT_TRUE(Inflar(texto.data(), texto.size(), &texto_desco, &m)) << m;
  }

  std::vector<EntradaParaMontar> entradas;
  entradas.push_back({"066-p1.bin", ZlibStored(rom), static_cast<std::uint32_t>(rom.size())});
  entradas.push_back({"boot.rom", texto, static_cast<std::uint32_t>(texto_desco.size())});
  const std::vector<std::uint8_t> bytes = MontarPack(entradas);

  Pacote p;
  std::string motivo;
  ASSERT_TRUE(p.Parse(bytes, &motivo)) << motivo;
  ASSERT_EQ(p.NumeroDeEntradas(), 2u);
  EXPECT_EQ(p.ListaDeEntradas()[0].nome, "066-p1.bin");
  EXPECT_EQ(p.ListaDeEntradas()[1].nome, "boot.rom");
  EXPECT_EQ(p.ListaDeEntradas()[0].tamanho_descomprimido, 4096u);
  EXPECT_EQ(p.ListaDeEntradas()[1].tamanho_descomprimido, kTamanhoDoTexto);
  EXPECT_EQ(p.ListaDeEntradas()[0].offset, pack_campos::kCabecalho + 2 * pack_campos::kRegisto);
  EXPECT_EQ(p.ListaDeEntradas()[0].hash, 0x11223344u);  // lido, e nao usado como criterio
  EXPECT_EQ(p.ConstanteDivergente(), 0u);
  EXPECT_EQ(p.NomesRepetidos(), 0u);

  std::vector<std::uint8_t> lido;
  ASSERT_TRUE(p.Extrair(0, &lido, &motivo)) << motivo;
  EXPECT_EQ(lido, rom);
  ASSERT_TRUE(p.Extrair(p.ListaDeEntradas()[1], &lido, &motivo)) << motivo;
  EXPECT_EQ(lido, texto_desco);
  // Sem distinguir maiusculas: o guest monta o caminho a partir do nome.
  ASSERT_TRUE(p.ExtrairPorNome("BOOT.ROM", &lido, &motivo)) << motivo;
  EXPECT_EQ(lido, texto_desco);
  EXPECT_EQ(p.Procurar("066-P1.BIN"), &p.ListaDeEntradas()[0]);
  EXPECT_EQ(p.Procurar("nao_existe.bin"), nullptr);
  EXPECT_FALSE(p.ExtrairPorNome("nao_existe.bin", &lido, &motivo));
  EXPECT_NE(motivo.find("nao_existe.bin"), std::string::npos) << motivo;
  EXPECT_FALSE(p.Extrair(7, &lido, &motivo));
}

TEST(Pack, RecipienteRecusadoComMotivo) {
  const std::vector<std::uint8_t> dados = Padrao(64, 0x22);
  std::vector<EntradaParaMontar> entradas;
  entradas.push_back({"a.bin", ZlibStored(dados), 64});
  const std::vector<std::uint8_t> bom = MontarPack(entradas);

  std::string motivo;
  {
    Pacote p;
    std::vector<std::uint8_t> v = bom;
    v[0] = 'X';
    EXPECT_FALSE(p.Parse(v, &motivo));
    EXPECT_NE(motivo.find("PACK"), std::string::npos) << motivo;
  }
  {
    // Os dados comecam por um cabecalho gzip: e o erro que faz um descompressor
    // parecer funcionar num ficheiro e falhar no seguinte.
    Pacote p;
    std::vector<std::uint8_t> v = bom;
    v[pack_campos::kCabecalho + pack_campos::kRegisto] = 0x1f;
    v[pack_campos::kCabecalho + pack_campos::kRegisto + 1] = 0x8b;
    EXPECT_FALSE(p.Parse(v, &motivo));
    EXPECT_NE(motivo.find("zlib"), std::string::npos) << motivo;
    EXPECT_NE(motivo.find("1f 8b"), std::string::npos) << motivo;
  }
  {
    // A tabela de nomes aponta para dentro dos dados.
    Pacote p;
    std::vector<std::uint8_t> v = bom;
    Escrever32(&v, 8, 300);
    EXPECT_FALSE(p.Parse(v, &motivo));
    EXPECT_NE(motivo.find("tabela"), std::string::npos) << motivo;
  }
  {
    // Uma entrada cujos dados saem do ficheiro.
    Pacote p;
    std::vector<std::uint8_t> v = bom;
    Escrever32(&v, pack_campos::kCabecalho + 8, 100000u);
    EXPECT_FALSE(p.Parse(v, &motivo));
    EXPECT_NE(motivo.find("fim do ficheiro"), std::string::npos) << motivo;
  }
  {
    // A tabela de nomes cortada: nao descomprime.
    Pacote p;
    std::vector<std::uint8_t> v = bom;
    v.resize(v.size() - 10);
    p.Parse(v, &motivo);
    EXPECT_FALSE(p.Valido());
    EXPECT_NE(motivo.find("tabela de nomes"), std::string::npos) << motivo;
  }
  {
    // Sem assinatura nao ha pacote -- e o motivo diz o que la estava.
    Pacote p;
    std::vector<std::uint8_t> v(10, 0);
    EXPECT_FALSE(p.Parse(v, &motivo));
    EXPECT_FALSE(p.Valido());
    std::vector<std::uint8_t> saida;
    EXPECT_FALSE(p.Extrair(0, &saida, &motivo));
  }
}

// ---------------------------------------------------------------------------
// (b, cont.) A VFS: o pacote servido como DIRECTÓRIO
// ---------------------------------------------------------------------------
//
// Sem midia nenhuma: a pasta e montada no proprio teste, com um `.mod` (para o
// directorio da uniao ter o nome que o guest usa), o pacote do jogo e o
// `boot.pkg`.

class VfsComPacotes : public ::testing::Test {
 protected:
  void SetUp() override {
    pasta_ = std::filesystem::temp_directory_path() / "zb2_teste_pack_vfs";
    std::filesystem::remove_all(pasta_);
    std::filesystem::create_directories(pasta_);

    // O `.mod` so existe para dar o nome ao directorio; o conteudo nao interessa
    // (a VFS nao o le, e o carregador nao entra aqui).
    Escrever("karnovr.mod", Padrao(16, 0x01));

    std::vector<EntradaParaMontar> do_jogo;
    do_jogo.push_back({"066-p1.bin", ZlibStored(Padrao(2048, 0x30)), 2048});
    do_jogo.push_back({"sfix.sfx", ZlibStored(Padrao(512, 0x60)), 512});
    Escrever("karnovr.pkg", MontarPack(do_jogo));

    std::vector<EntradaParaMontar> do_bios;
    do_bios.push_back({"boot.rom", ZlibStored(Padrao(8192, 0x90)), 8192});
    Escrever("boot.pkg", MontarPack(do_bios));

    vfs_.Registar(pasta_.string());
  }
  void TearDown() override { std::filesystem::remove_all(pasta_); }

  void Escrever(const std::string& nome, const std::vector<std::uint8_t>& bytes) {
    std::ofstream f(pasta_ / nome, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }

  Vfs vfs_;
  std::filesystem::path pasta_;
};

TEST_F(VfsComPacotes, OSolitosEOsPacotesSaoOSDoisLidos) {
  // Os ficheiros soltos continuam a existir (o `.mod` e os `.pkg`), e o
  // directorio do titulo traz a UNIAO dos dois pacotes.
  EXPECT_TRUE(vfs_.Existe("karnovr.mod"));
  EXPECT_TRUE(vfs_.Existe("karnovr.pkg"));
  EXPECT_TRUE(vfs_.Existe("karnovr/boot.rom"));
  EXPECT_TRUE(vfs_.Existe("karnovr/066-p1.bin"));
  EXPECT_TRUE(vfs_.Existe("karnovr/sfix.sfx"));
  // O directorio de CADA pacote tambem serve (o `boot.pkg` da `boot/boot.rom`).
  EXPECT_TRUE(vfs_.Existe("boot/boot.rom"));
  EXPECT_FALSE(vfs_.Existe("karnovr/nao_existe.bin"));
  EXPECT_FALSE(vfs_.Existe("outro/boot.rom"));
  EXPECT_FALSE(vfs_.Existe("boot.rom"));  // sem directorio nao ha uniao

  EXPECT_EQ(vfs_.Pacotes().size(), 2u);
  for (const Vfs::PacoteRegistado& p : vfs_.Pacotes()) {
    EXPECT_TRUE(p.motivo.empty()) << p.ficheiro << ": " << p.motivo;
  }
  EXPECT_EQ(vfs_.NomesNosPacotes(), 3u);
  EXPECT_EQ(vfs_.DiretoriosServidos().count("karnovr"), 1u);
  EXPECT_EQ(vfs_.DiretoriosServidos().count("boot"), 1u);
}

TEST_F(VfsComPacotes, OsPrefixosDoGuestSaoAceites) {
  // Os tres caminhos que o guest monta (o mesmo ficheiro por tres nomes).
  EXPECT_EQ(vfs_.Normalizar(".\\karnovr\\boot.rom"), "karnovr/boot.rom");
  EXPECT_EQ(vfs_.Normalizar("roms\\karnovr\\boot.rom"), "karnovr/boot.rom");
  EXPECT_EQ(vfs_.Normalizar("roms\\neogeo\\karnovr\\boot.rom"), "karnovr/boot.rom");
  EXPECT_EQ(vfs_.Normalizar("roms/neogeo/karnovr/066-p1.bin"), "karnovr/066-p1.bin");
  // Um directorio que nao e nenhum dos servidos nao casa, mesmo com o nome certo
  // dentro: a regra e do directorio, e nao do sufixo solto.
  EXPECT_EQ(vfs_.Normalizar("qualquer/karnovr/boot.rom"), "");
}

TEST_F(VfsComPacotes, LerDescomprimeAEntrada) {
  std::vector<std::uint8_t> bytes;
  std::string motivo;
  ASSERT_TRUE(vfs_.Ler("karnovr/boot.rom", &bytes, &motivo)) << motivo;
  EXPECT_EQ(bytes, Padrao(8192, 0x90));
  ASSERT_TRUE(vfs_.Ler("roms\\karnovr\\sfix.sfx", &bytes, &motivo)) << motivo;
  EXPECT_EQ(bytes, Padrao(512, 0x60));
  ASSERT_TRUE(vfs_.Ler("karnovr.mod", &bytes, &motivo)) << motivo;
  EXPECT_EQ(bytes, Padrao(16, 0x01));
  EXPECT_FALSE(vfs_.Ler("karnovr/nao_existe.bin", &bytes, &motivo));
  EXPECT_NE(motivo.find("nao_existe.bin"), std::string::npos) << motivo;

  std::size_t pacote = 0, entrada = 0;
  ASSERT_TRUE(vfs_.OrigemDe("karnovr/boot.rom", &pacote, &entrada));
  EXPECT_EQ(vfs_.Pacotes()[pacote].ficheiro, "boot.pkg");
  EXPECT_EQ(entrada, 0u);
  EXPECT_FALSE(vfs_.OrigemDe("karnovr.mod", &pacote, &entrada));
}

TEST_F(VfsComPacotes, OIFileDoGuestLeOPacote) {
  // O caminho do guest INTEIRO: o `IFileMgr::OpenFile` e o `IFile::Read`.
  Memoria mem_(nullptr);
  Arquivos a(&vfs_);
  const std::uint32_t id = a.Abrir(".\\karnovr\\boot.rom", 0x0001u, pasta_.string());
  ASSERT_NE(id, 0u) << a.UltimoMotivo();
  EXPECT_TRUE(a.UltimoVeioDePacote());
  EXPECT_EQ(a.UltimoCaminho(), "karnovr/boot.rom");

  constexpr Endereco kDest = 0x00100000;
  EXPECT_EQ(a.Ler(id, mem_, kDest, 32), 32);
  const std::vector<std::uint8_t> inicio = Padrao(8192, 0x90);
  for (std::uint32_t i = 0; i < 32; ++i) EXPECT_EQ(mem_.Ler8(kDest + i), inicio[i]) << "byte " << i;
  // O tamanho que o jogo pergunta (o `pcmania`/neo geo le o tamanho antes de ler).
  constexpr Endereco kInfo = 0x00100200;
  ASSERT_TRUE(a.Informacao(id, mem_, kInfo));
  EXPECT_EQ(mem_.Ler32(kInfo + 8), 8192u);
  EXPECT_EQ(a.Posicionar(id, 2, 0), 8192);
  a.Fechar(id);
  EXPECT_EQ(a.Abertos(), 0u);

  // Um caminho que nao existe NAO abre -- e agora diz por que.
  EXPECT_EQ(a.Abrir("karnovr/nao_existe.bin", 0x0001u, pasta_.string()), 0u);
  EXPECT_NE(a.UltimoMotivo().find("nao_existe.bin"), std::string::npos) << a.UltimoMotivo();
}

TEST_F(VfsComPacotes, ARegraDosPacotesEstaNoTraco) {
  // "nada entra em silencio": a ligacao entre os `.pkg` e o sistema de ficheiros
  // fica DITA, com os numeros e os directorios aceites.
  Traco traco("teste");
  DestinoMemoria dm;
  traco.JuntarDestino(&dm);
  vfs_.DeclararNoTraco(&traco);
  EXPECT_EQ(dm.QuantosComNome("VFS_REGISTADA"), 1u);
  EXPECT_EQ(dm.QuantosComNome("VFS_PACOTE"), 2u);
  EXPECT_EQ(dm.QuantosComNome("VFS_PACOTE_RECUSADO"), 0u);
  EXPECT_EQ(dm.QuantosComNome("VFS_ALIAS"), 1u);
  for (const Evento& e : dm.eventos) {
    if (e.nome != "VFS_ALIAS") continue;
    EXPECT_NE(e.detalhe.find("karnovr"), std::string::npos) << e.detalhe;
    EXPECT_NE(e.detalhe.find("boot"), std::string::npos) << e.detalhe;
    EXPECT_NE(e.detalhe.find("roms/neogeo/"), std::string::npos) << e.detalhe;
  }
  // Idempotente: uma declaracao por corrida, e nao uma por titulo.
  vfs_.DeclararNoTraco(&traco);
  EXPECT_EQ(dm.QuantosComNome("VFS_ALIAS"), 1u);
}

TEST_F(VfsComPacotes, UmPacoteRecusadoFicaDitoENaoSomeDoIndice) {
  // Um `.pkg` corrompido na pasta: os outros continuam a servir, e o que ficou
  // de fora aparece no traco como ERRO com o motivo.
  std::vector<std::uint8_t> lixo = Padrao(700, 0x00);
  lixo[0] = 'P';
  Escrever("estranho.pkg", lixo);
  Vfs outro;
  outro.Registar(pasta_.string());
  bool achou = false;
  for (const Vfs::PacoteRegistado& p : outro.Pacotes()) {
    if (p.ficheiro != "estranho.pkg") continue;
    achou = true;
    EXPECT_FALSE(p.motivo.empty());
    EXPECT_EQ(p.entradas, 0u);
  }
  EXPECT_TRUE(achou);
  EXPECT_TRUE(outro.Existe("karnovr/boot.rom"));
  Traco traco("teste");
  DestinoMemoria dm;
  traco.JuntarDestino(&dm);
  outro.DeclararNoTraco(&traco);
  EXPECT_EQ(dm.QuantosComNome("VFS_PACOTE_RECUSADO"), 1u);
}

// ---------------------------------------------------------------------------
// (c) O `boot.pkg` de VERDADE do corpus
// ---------------------------------------------------------------------------

TEST(Pack, OBootPkgDeVerdade) {
  const std::string caminho = CaminhoDoBootPkg();
  if (caminho.empty()) {
    GTEST_SKIP() << "sem o boot.pkg do corpus nesta maquina (ZB2_MODS aponta a pasta dos mods) -- "
                    "PULAR, e nao passar";
  }
  std::ifstream f(caminho, std::ios::binary);
  ASSERT_TRUE(f) << caminho;
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  // Medido: 1816 bytes, UMA entrada, 1509 comprimidos, 8192 descomprimidos.
  ASSERT_EQ(bytes.size(), 1816u);

  Pacote p;
  std::string motivo;
  ASSERT_TRUE(p.Parse(bytes, &motivo)) << caminho << ": " << motivo;
  ASSERT_EQ(p.NumeroDeEntradas(), 1u) << "o boot.pkg traz UMA entrada";
  const EntradaDoPack& e = p.ListaDeEntradas()[0];
  EXPECT_EQ(e.nome, "boot.rom");
  EXPECT_EQ(e.tamanho_comprimido, 1509u);
  EXPECT_EQ(e.tamanho_descomprimido, 8192u);
  EXPECT_EQ(e.offset, pack_campos::kCabecalho + pack_campos::kRegisto);  // 288
  EXPECT_TRUE(p.Motivo().empty());
  EXPECT_EQ(p.ConstanteDivergente(), 0u);

  std::vector<std::uint8_t> rom;
  ASSERT_TRUE(p.Extrair(e, &rom, &motivo)) << motivo;
  ASSERT_EQ(rom.size(), 8192u);
  // O resumo do BIOS, medido com o `zlib` do Python sobre o mesmo ficheiro: e o
  // que fixa que os 8192 bytes sao os DESCOMPRIMIDOS, e nao os outros.
  EXPECT_EQ(brew::Sha256Hex(rom.data(), rom.size()),
            "be2d4441bace462f8dbd506c98b073b4b71ae652ac87a15757c70a6ec861277d");
  // Os oito primeiros bytes, medidos no mesmo ficheiro (o resumo e o criterio;
  // isto e a prova de que o CONTEUDO comeca onde deve, e nao um deslocamento de
  // um byte que o resumo nao distingue de um ficheiro parecido).
  const std::uint8_t kInicio[8] = {0x10, 0x00, 0x00, 0xf3, 0xc0, 0x00, 0xa0, 0x07};
  for (std::size_t i = 0; i < 8; ++i) EXPECT_EQ(rom[i], kInicio[i]) << "byte " << i;
  EXPECT_EQ(p.ExtrairPorNome("nao_existe", &rom, &motivo), false);
}

// ---------------------------------------------------------------------------
// (d) O CAMINHO DO GUEST: `karnovr/boot.rom`
// ---------------------------------------------------------------------------

TEST(Pack, OCaminhoDoGuestKarnovrBootRom) {
  const std::string pasta = PastaDoKarnovr();
  if (pasta.empty()) {
    GTEST_SKIP() << "sem a pasta 279126 (karnovr) do corpus nesta maquina (ZB2_MODS) -- PULAR";
  }
  Vfs vfs;
  vfs.Registar(pasta);
  ASSERT_GE(vfs.Pacotes().size(), 2u);
  for (const Vfs::PacoteRegistado& p : vfs.Pacotes()) {
    EXPECT_TRUE(p.motivo.empty()) << p.ficheiro << ": " << p.motivo;
  }

  // Os quatro nomes que o guest usa. O primeiro (`.\karnovr\boot.rom`) e o que o
  // desmonte do `karnovr.mod` monta (`%s\%s\%s` com ".", "karnovr", "boot.rom"),
  // e o BIOS vem do `boot.pkg` -- que e o ponto todo da uniao.
  EXPECT_TRUE(vfs.Existe(".\\karnovr\\boot.rom"));
  EXPECT_TRUE(vfs.Existe("roms\\karnovr\\boot.rom"));
  EXPECT_TRUE(vfs.Existe("roms\\neogeo\\karnovr\\boot.rom"));
  EXPECT_TRUE(vfs.Existe("karnovr\\066-p1.bin"));
  EXPECT_FALSE(vfs.Existe("karnovr\\nao_existe.bin"));

  std::vector<std::uint8_t> rom;
  std::string motivo;
  ASSERT_TRUE(vfs.Ler(".\\karnovr\\boot.rom", &rom, &motivo)) << motivo;
  ASSERT_EQ(rom.size(), 8192u);
  EXPECT_EQ(brew::Sha256Hex(rom.data(), rom.size()),
            "be2d4441bace462f8dbd506c98b073b4b71ae652ac87a15757c70a6ec861277d");

  std::size_t pacote = 0, entrada = 0;
  ASSERT_TRUE(vfs.OrigemDe("karnovr/boot.rom", &pacote, &entrada));
  EXPECT_EQ(vfs.Pacotes()[pacote].ficheiro, "boot.pkg");

  // A ROM do jogo vem do outro pacote, pelo mesmo directorio.
  std::vector<std::uint8_t> rom_do_jogo;
  ASSERT_TRUE(vfs.Ler("karnovr/066-p1.bin", &rom_do_jogo, &motivo)) << motivo;
  EXPECT_EQ(rom_do_jogo.size(), 1048576u);  // 066-p1.bin medido: 1 MiB
  ASSERT_TRUE(vfs.OrigemDe("karnovr/066-p1.bin", &pacote, &entrada));
  EXPECT_EQ(vfs.Pacotes()[pacote].ficheiro, "karnovr.pkg");

  // E o caminho do guest inteiro, pelo `IFileMgr`: abrir, ler 8192 bytes e
  // conferir o tamanho reportado ao jogo.
  Memoria mem_(nullptr);
  Arquivos a(&vfs);
  const std::uint32_t id = a.Abrir(".\\karnovr\\boot.rom", 0x0001u, pasta);
  ASSERT_NE(id, 0u) << a.UltimoMotivo();
  constexpr Endereco kDest = 0x00100000;
  EXPECT_EQ(a.Ler(id, mem_, kDest, 8192), 8192);
  EXPECT_EQ(mem_.Ler8(kDest), 0x10u);
  EXPECT_EQ(mem_.Ler8(kDest + 1), 0x00u);
  constexpr Endereco kInfo = 0x00104000;
  ASSERT_TRUE(a.Informacao(id, mem_, kInfo));
  EXPECT_EQ(mem_.Ler32(kInfo + 8), 8192u);
}

TEST(Pack, OsNovePacotesDoCorpusPassamTodasAsGuardas) {
  const std::string raiz = RaizDosMods();
  if (raiz.empty()) {
    GTEST_SKIP() << "sem a midia do corpus nesta maquina (ZB2_MODS) -- PULAR";
  }
  // Os 9 `.pkg` medidos, com o numero de entradas de cada um. Le-los todos aqui
  // custa alguns segundos -- e o preco de ter, dentro do `ctest`, a prova que o
  // formato vale para o corpus inteiro e nao para o `boot.pkg` so.
  const struct { const char* rel; std::size_t entradas; } kAlvos[] = {
      {"278988/strhoop.pkg", 10}, {"278986/cninja.pkg", 19},  {"279173/wizdfire.pkg", 18},
      {"278987/spinmast.pkg", 14}, {"279233/darkseal.pkg", 26}, {"279125/supbtime.pkg", 7},
      {"279126/karnovr.pkg", 22},  {"279126/boot.pkg", 1},    {"279200/magdrop3.pkg", 10}};
  std::size_t total = 0;
  for (const auto& alvo : kAlvos) {
    const std::string caminho = raiz + "/" + alvo.rel;
    std::ifstream f(caminho, std::ios::binary);
    ASSERT_TRUE(f) << caminho;
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    Pacote p;
    std::string motivo;
    ASSERT_TRUE(p.Parse(bytes, &motivo)) << alvo.rel << ": " << motivo;
    EXPECT_EQ(p.NumeroDeEntradas(), alvo.entradas) << alvo.rel;
    EXPECT_EQ(p.ConstanteDivergente(), 0u) << alvo.rel;
    EXPECT_EQ(p.NomesRepetidos(), 0u) << alvo.rel;
    for (std::size_t i = 0; i < p.NumeroDeEntradas(); ++i) {
      const EntradaDoPack& e = p.ListaDeEntradas()[i];
      std::vector<std::uint8_t> dados;
      ASSERT_TRUE(p.Extrair(e, &dados, &motivo)) << alvo.rel << " entrada " << i << ": " << motivo;
      EXPECT_EQ(dados.size(), e.tamanho_descomprimido) << alvo.rel << " " << e.nome;
      ++total;
    }
  }
  EXPECT_EQ(total, 127u) << "os 9 pacotes do corpus tem 127 entradas (medido)";
}
