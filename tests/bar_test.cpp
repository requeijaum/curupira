// Testes do leitor `.bar` (`core/carga/bar.{h,cpp}`).
//
// Duas familias:
//
//  1. `BarSintetico*` -- ficheiros `.bar` construidos AQUI, byte a byte, sem
//     depender de ROM nenhuma. Sao estes que provam as GUARDAS: cada um viola
//     uma invariante de proposito (ver o comentario de cada um, que diz o que
//     partir para o teste ficar VERMELHO).
//
//  2. `BarDeVerdade*` -- o `pacmania.bar` real (o que o jogo pede: id 5091, tipo
//     6) e o `aeecontrols.bar` do SDK (cujos ids o cabecalho do VENDEDOR
//     declara). Saltam com mensagem quando o ficheiro nao existe na maquina --
//     PULAR, e nao passar em silencio.
//
// Comandos que produzem os numeros citados:
//   python3 tools/medir_bar.py recurso "<mods>/276212/pacmania.bar" 6 5091
//   python3 tools/medir_bar.py censo "<SDK>"
//   /usr/bin/python3 tools/medir_bar.py desmontar "<mods>/276212/pacmania.mod" 0x110d8c 0x40

#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "core/carga/bar.h"
#include "core/brew/sha256.h"   // o resumo do buffer RGB565 inteiro
#include "core/carga/png.h"       // o descodificador PNG (frente imgdec)

namespace {

using zb2::ArquivoBar;
using zb2::BlobDoBar;
using zb2::RecursoDoBar;
using zb2::RegistroDoBar;

const char* kPacmaniaBarPadrao =
    "/media/rafaelfrequiao/8C5F-19E51/zeebo/ROMs/debug_nand/mod/276212/pacmania.bar";

// Devolve vazio quando o ficheiro nao existe. O caminho pode ser dado por
// variavel de ambiente, para nao prender o teste a uma maquina.
std::string CaminhoDoPacmania() {
  const char* env = std::getenv("ZB2_BAR_PACMANIA");
  if (env != nullptr && *env != '\0') {
    std::ifstream f(env, std::ios::binary);
    if (f) {
      return env;
    }
    return std::string();
  }
  std::ifstream f(kPacmaniaBarPadrao, std::ios::binary);
  return f ? std::string(kPacmaniaBarPadrao) : std::string();
}

// O SDK fica em <repo>/research/docs/sdk-extract/... . A raiz do repositorio
// vem do CMake (`ZB2_RAIZ_DO_REPO`), que a descobre pelo `git` para funcionar
// tambem a partir de uma worktree.
std::string CaminhoDaAeeControls(const char* locale) {
  std::vector<std::string> raizes;
  if (const char* env = std::getenv("ZB2_SDK_DIR")) {
    raizes.push_back(env);
  }
#ifdef ZB2_RAIZ_DO_REPO
  raizes.push_back(std::string(ZB2_RAIZ_DO_REPO) + "/research/docs/sdk-extract/" +
                    "BrewMPSDK-7.12.5/SDKPro/1.0.4.601 Pro");
#endif
  for (const std::string& raiz : raizes) {
    const std::string caminho =
        raiz + "/sck/platform/system/brewcore/src/BREWSim/components/" + locale + "/256Color/aeecontrols.bar";
    std::ifstream f(caminho, std::ios::binary);
    if (f) {
      return caminho;
    }
  }
  return std::string();
}

// ---------------------------------------------------------------------------
// Construtor de `.bar` sinteticos, com a mesma forma MEDIDA em `bar.h`.
// ---------------------------------------------------------------------------
struct RegistoDeTeste {
  std::uint16_t tipo;
  std::uint16_t primeiro_id;
  std::uint16_t delta;
  std::uint16_t primeiro_indice;
};

void Escrever16(std::vector<std::uint8_t>* b, std::size_t pos, std::uint16_t v) {
  (*b)[pos] = static_cast<std::uint8_t>(v & 0xff);
  (*b)[pos + 1] = static_cast<std::uint8_t>((v >> 8) & 0xff);
}

void Escrever32(std::vector<std::uint8_t>* b, std::size_t pos, std::uint32_t v) {
  for (int k = 0; k < 4; ++k) {
    (*b)[pos + k] = static_cast<std::uint8_t>((v >> (8 * k)) & 0xff);
  }
}

std::vector<std::uint8_t> MontarBar(const std::vector<RegistoDeTeste>& registos,
                                    const std::vector<std::vector<std::uint8_t>>& recursos) {
  std::uint32_t num_ids = 0;
  for (const RegistoDeTeste& r : registos) {
    num_ids += static_cast<std::uint32_t>(r.delta) + 1u;
  }
  const std::uint32_t n_registos = static_cast<std::uint32_t>(registos.size());
  const std::uint32_t off_registos = 32;
  const std::uint32_t tam_registos = 8 * n_registos;
  const std::uint32_t off_indices = off_registos + tam_registos;
  const std::uint32_t off_dados = off_indices + 4 * (num_ids + 1);

  std::vector<std::uint8_t> dados;
  std::vector<std::uint32_t> indices;
  indices.push_back(off_dados);
  for (const std::vector<std::uint8_t>& r : recursos) {
    dados.insert(dados.end(), r.begin(), r.end());
    indices.push_back(off_dados + static_cast<std::uint32_t>(dados.size()));
  }
  while (indices.size() < num_ids + 1) {
    indices.push_back(indices.back());
  }

  std::vector<std::uint8_t> b(off_dados + dados.size(), 0);
  Escrever16(&b, 0, 0x0011);
  Escrever16(&b, 2, 1);
  Escrever16(&b, 4, 1);
  Escrever16(&b, 6, static_cast<std::uint16_t>(n_registos));
  Escrever32(&b, 8, off_registos);
  Escrever32(&b, 12, tam_registos);
  Escrever32(&b, 16, off_indices);
  Escrever32(&b, 20, num_ids);
  Escrever32(&b, 24, off_dados);
  Escrever32(&b, 28, static_cast<std::uint32_t>(dados.size()));
  for (std::uint32_t k = 0; k < n_registos; ++k) {
    Escrever16(&b, off_registos + 8 * k + 0, registos[k].tipo);
    Escrever16(&b, off_registos + 8 * k + 2, registos[k].primeiro_id);
    Escrever16(&b, off_registos + 8 * k + 4, registos[k].delta);
    Escrever16(&b, off_registos + 8 * k + 6, registos[k].primeiro_indice);
  }
  for (std::size_t k = 0; k < indices.size(); ++k) {
    Escrever32(&b, off_indices + 4 * k, indices[k]);
  }
  for (std::size_t k = 0; k < dados.size(); ++k) {
    b[off_dados + k] = dados[k];
  }
  return b;
}

// Um blob AEEResBlob: `deslocamento`, zero, mime, um byte de enchimento (para o
// deslocamento ficar par, como no pacmania), e o dado.
std::vector<std::uint8_t> Blob(std::uint8_t deslocamento, const std::string& mime,
                               const std::vector<std::uint8_t>& dado) {
  std::vector<std::uint8_t> b;
  b.push_back(deslocamento);
  b.push_back(0);
  b.insert(b.end(), mime.begin(), mime.end());
  b.push_back(0);
  while (b.size() < deslocamento) {
    b.push_back(0);
  }
  b.insert(b.end(), dado.begin(), dado.end());
  return b;
}

const std::vector<std::uint8_t> kDadoA = {'A', 'A', 'A'};
const std::vector<std::uint8_t> kDadoB = {'B', 'B'};
const std::vector<std::uint8_t> kDadoC = {'C'};

// Tabela de dois tipos, como o `aeecontrols.bar`: tipo 6 nos indices 0..2 e
// tipo 1 nos indices 3..4.
std::vector<std::uint8_t> BarDeDoisTipos() {
  const std::uint8_t deslocamento = 12;
  return MontarBar({{6, 10, 2, 0}, {1, 100, 1, 3}},
                   {Blob(deslocamento, "image/x", kDadoA), Blob(deslocamento, "image/x", kDadoB),
                    Blob(deslocamento, "image/x", kDadoC), {'s', 't', 'r'},
                    {'o', 'u', 't', 'r', 'o'}});
}

ArquivoBar AbrirSintetico(std::vector<std::uint8_t> bytes) {
  std::string motivo;
  ArquivoBar a = ArquivoBar::AbrirDados(std::move(bytes), &motivo);
  return a;
}

}  // namespace

// ---------------------------------------------------------------------------
// Guardas: ficheiros sinteticos, cada um com UMA invariante partida
// ---------------------------------------------------------------------------

TEST(BarSintetico, LeOPrimeiroEUltimoIdDeCadaRegisto) {
  ArquivoBar a = AbrirSintetico(BarDeDoisTipos());
  ASSERT_TRUE(a.Valido()) << a.Motivo();
  EXPECT_EQ(a.NumeroDeIds(), 5u);
  EXPECT_EQ(a.NumeroDeRecursos(), 5u);

  const RecursoDoBar primeiro = a.Ler(10, 6);
  ASSERT_TRUE(primeiro.ok) << primeiro.motivo;
  EXPECT_EQ(primeiro.tamanho, 15u);  // 12 + 3
  EXPECT_EQ(primeiro.dados[12], 'A');

  // O ULTIMO id do intervalo do registo (primeiro_id + delta) e o limite.
  const RecursoDoBar ultimo = a.Ler(12, 6);
  ASSERT_TRUE(ultimo.ok) << ultimo.motivo;
  EXPECT_EQ(ultimo.dados[12], 'C');

  const RecursoDoBar segundo_tipo = a.Ler(101, 1);
  ASSERT_TRUE(segundo_tipo.ok) << segundo_tipo.motivo;
  EXPECT_EQ(segundo_tipo.tamanho, 5u);
  EXPECT_EQ(segundo_tipo.dados[0], 'o');
}

// VIOLACAO: `Procurar` desiste do intervalo em vez de o percorrer.
TEST(BarSintetico, RecusaIdForaDeQualquerRegisto) {
  ArquivoBar a = AbrirSintetico(BarDeDoisTipos());
  ASSERT_TRUE(a.Valido()) << a.Motivo();
  const RecursoDoBar antes = a.Ler(9, 6);
  EXPECT_FALSE(antes.ok);
  EXPECT_NE(antes.motivo.find("nao ha registo"), std::string::npos) << antes.motivo;
  // 13 fica no BURACO entre o fim do registo do tipo 6 (12) e o do tipo 1 (100).
  const RecursoDoBar buraco = a.Ler(13, 6);
  EXPECT_FALSE(buraco.ok);
  const RecursoDoBar depois = a.Ler(102, 1);
  EXPECT_FALSE(depois.ok);
}

// VIOLACAO: comparar so o tipo, sem comparar o id.
TEST(BarSintetico, RecusaTipoDiferente) {
  ArquivoBar a = AbrirSintetico(BarDeDoisTipos());
  ASSERT_TRUE(a.Valido()) << a.Motivo();
  const RecursoDoBar r = a.Ler(10, 1);  // id 10 existe no tipo 6, nao no tipo 1
  EXPECT_FALSE(r.ok);
  EXPECT_NE(r.motivo.find("nao ha registo"), std::string::npos) << r.motivo;
}

// VIOLACAO: aceitar o ficheiro sem olhar para a assinatura (trocar o teste de
// `versao != 0x0011` por `false`).
TEST(BarSintetico, RecusaAssinaturaErrada) {
  std::vector<std::uint8_t> bytes = BarDeDoisTipos();
  Escrever16(&bytes, 0, 0x1234);
  ArquivoBar a = AbrirSintetico(bytes);
  EXPECT_FALSE(a.Valido());
  EXPECT_NE(a.Motivo().find("assinatura"), std::string::npos) << a.Motivo();
}

// VIOLACAO: tirar a verificacao `n < 32` do inicio de `Validar`.
TEST(BarSintetico, RecusaFicheiroMaisCurtoQueOCabecalho) {
  ArquivoBar a = AbrirSintetico({0x11, 0x00, 0x01});
  EXPECT_FALSE(a.Valido());
  EXPECT_NE(a.Motivo().find("abaixo dos 32"), std::string::npos) << a.Motivo();
}

// VIOLACAO: tirar a comparacao `tam_registos % 8 != 0`.
TEST(BarSintetico, RecusaTamanhoDosRegistosNaoMultiploDe8) {
  std::vector<std::uint8_t> bytes = BarDeDoisTipos();
  Escrever32(&bytes, 12, 15);  // 15 bytes de registos: nao da registos inteiros
  ArquivoBar a = AbrirSintetico(bytes);
  EXPECT_FALSE(a.Valido());
  EXPECT_NE(a.Motivo().find("nao e multiplo de 8"), std::string::npos) << a.Motivo();
}

// VIOLACAO: tirar a comparacao `tam_registos / 8 != num_registos`.
TEST(BarSintetico, RecusaContagemDeRegistosIncoerente) {
  std::vector<std::uint8_t> bytes = BarDeDoisTipos();
  Escrever16(&bytes, 6, 3);  // diz que ha 3 registos, e a tabela tem 2
  ArquivoBar a = AbrirSintetico(bytes);
  EXPECT_FALSE(a.Valido());
  // A FRAGMENTO TEM DE SER SO DESTA GUARDA: "registos" sozinho tambem aparece
  // na mensagem da soma dos ids, e o teste passava sem a guarda estar la.
  EXPECT_NE(a.Motivo().find("tamanho dos registos"), std::string::npos) << a.Motivo();
}

// VIOLACAO: tirar a soma `delta + 1` de `Validar`.
TEST(BarSintetico, RecusaSomaDeIdsDiferenteDaContagem) {
  std::vector<std::uint8_t> bytes = BarDeDoisTipos();
  Escrever32(&bytes, 20, 6);  // num_ids = 6, e os registos cobrem 5
  ArquivoBar a = AbrirSintetico(bytes);
  EXPECT_FALSE(a.Valido());
  // "ids" sozinho nao serve: a mensagem da tabela de indices tambem o diz.
  EXPECT_NE(a.Motivo().find("os registos cobrem"), std::string::npos) << a.Motivo();
}

// VIOLACAO: tirar a comparacao `off_registos != 32`.
TEST(BarSintetico, RecusaDeslocamentoDosRegistosDiferenteDe32) {
  std::vector<std::uint8_t> bytes = BarDeDoisTipos();
  Escrever32(&bytes, 8, 40);  // os registos passariam a comecar 8 bytes depois
  ArquivoBar a = AbrirSintetico(bytes);
  EXPECT_FALSE(a.Valido());
  EXPECT_NE(a.Motivo().find("deslocamento dos registos"), std::string::npos) << a.Motivo();
}

// VIOLACAO: tirar a comparacao `off_indices != off_registos + tam_registos`.
TEST(BarSintetico, RecusaDeslocamentoDosIndicesErrado) {
  std::vector<std::uint8_t> bytes = BarDeDoisTipos();
  Escrever32(&bytes, 16, 40);  // a tabela de indices nao fica a seguir aos registos
  ArquivoBar a = AbrirSintetico(bytes);
  EXPECT_FALSE(a.Valido());
  EXPECT_NE(a.Motivo().find("tabela de indices em"), std::string::npos) << a.Motivo();
}

// VIOLACAO: tirar a comparacao `bytes_dos_indices / 4 != num_ids + 1`.
TEST(BarSintetico, RecusaTabelaDeIndicesComONumeroErradoDeValores) {
  std::vector<std::uint8_t> bytes = BarDeDoisTipos();
  // A tabela fica entre 48 e off_dados. Encolher off_dados para 48 + 12 deixa
  // 3 valores onde o cabecalho manda ter num_ids + 1 = 6.
  Escrever32(&bytes, 24, 48 + 12);
  ArquivoBar a = AbrirSintetico(bytes);
  EXPECT_FALSE(a.Valido());
  EXPECT_NE(a.Motivo().find("tabela de indices com"), std::string::npos) << a.Motivo();
}

// VIOLACAO: tirar a comparacao do ULTIMO deslocamento com o tamanho do ficheiro.
TEST(BarSintetico, RecusaUltimoDeslocamentoDiferenteDoTamanhoDoFicheiro) {
  std::vector<std::uint8_t> bytes = BarDeDoisTipos();
  const std::uint32_t off_indices = 32 + 8 * 2;
  const std::uint32_t ultimo = off_indices + 4 * 5;  // valores[5] = 6.o valor
  Escrever32(&bytes, ultimo, static_cast<std::uint32_t>(bytes.size()) - 1);
  ArquivoBar a = AbrirSintetico(bytes);
  EXPECT_FALSE(a.Valido());
  EXPECT_NE(a.Motivo().find("tamanho do ficheiro"), std::string::npos) << a.Motivo();
}

// VIOLACAO: tirar a guarda `ultimo >= numero_de_recursos_` sobre os registos.
TEST(BarSintetico, RecusaRegistoQueApontaParaForaDaTabela) {
  // O segundo registo (tipo 1, id 100, delta 2) comeca no indice 3 e cobriria
  // os indices 3, 4 e 5. A tabela tem 5 recursos (indices 0..4), portanto o
  // indice 5 sai fora. Sem a guarda, `Ler(101,1)` leria `deslocamentos_[5]` --
  // memoria que nao e do recurso.
  std::vector<std::uint8_t> bytes =
      MontarBar({{6, 10, 1, 0}, {1, 100, 2, 3}}, {Blob(12, "image/x", kDadoA), Blob(12, "image/x", kDadoB),
                                                  {'a'}, {'b'}});
  ArquivoBar a = AbrirSintetico(bytes);
  EXPECT_FALSE(a.Valido());
  EXPECT_NE(a.Motivo().find("cobre indices"), std::string::npos) << a.Motivo();
}


// VIOLACAO: tirar a comparacao `deslocamentos_[k] < deslocamentos_[k-1]`.
TEST(BarSintetico, RecusaDeslocamentosNaoMonotonos) {
  std::vector<std::uint8_t> bytes = BarDeDoisTipos();
  const std::uint32_t off_indices = 32 + 8 * 2;
  Escrever32(&bytes, off_indices + 8, 4);  // valores[2] volta para tras
  ArquivoBar a = AbrirSintetico(bytes);
  EXPECT_FALSE(a.Valido());
  EXPECT_NE(a.Motivo().find("monotonos"), std::string::npos) << a.Motivo();
}

// VIOLACAO: tirar a comparacao `deslocamentos_.front() != off_dados`.
TEST(BarSintetico, RecusaPrimeiroDeslocamentoDiferenteDoInicioDosDados) {
  std::vector<std::uint8_t> bytes = BarDeDoisTipos();
  const std::uint32_t off_indices = 32 + 8 * 2;
  const std::uint32_t off_dados = 32 + 8 * 2 + 4 * 5;
  // valores[0] = off_dados - 1: continua a ser menor que valores[1] (portanto
  // nao apanha a guarda dos monotonos) e nao e o inicio dos dados.
  Escrever32(&bytes, off_indices, off_dados - 1);
  ArquivoBar a = AbrirSintetico(bytes);
  EXPECT_FALSE(a.Valido());
  EXPECT_NE(a.Motivo().find("primeiro deslocamento"), std::string::npos) << a.Motivo();
}

// VIOLACAO: tirar a guarda `indice >= numero_de_recursos_` de `LerPorIndice`.
TEST(BarSintetico, RecusaLerPorIndiceForaDaTabela) {
  ArquivoBar a = AbrirSintetico(BarDeDoisTipos());
  ASSERT_TRUE(a.Valido()) << a.Motivo();
  const RecursoDoBar r = a.LerPorIndice(5);
  EXPECT_FALSE(r.ok);
  EXPECT_NE(r.motivo.find("sai da tabela"), std::string::npos) << r.motivo;
  EXPECT_TRUE(a.LerPorIndice(4).ok);
}

// VIOLACAO: tirar a comparacao `recurso.dados[1] != 0` de `LerBlob`.
TEST(BlobSintetico, LeODeslocamentoEOMime) {
  ArquivoBar a = AbrirSintetico(BarDeDoisTipos());
  ASSERT_TRUE(a.Valido()) << a.Motivo();
  const RecursoDoBar r = a.Ler(11, 6);
  ASSERT_TRUE(r.ok) << r.motivo;
  const BlobDoBar b = ArquivoBar::LerBlob(r);
  ASSERT_TRUE(b.ok) << b.motivo;
  EXPECT_EQ(b.deslocamento, 12);
  EXPECT_EQ(b.mime, "image/x");
  EXPECT_EQ(b.tamanho, 2u);
  EXPECT_EQ(b.dados[0], 'B');
}

// VIOLACAO: tirar a comparacao `deslocamento >= recurso.tamanho`.
TEST(BlobSintetico, RecusaDeslocamentoParaLaDoRecurso) {
  const std::vector<std::uint8_t> blob = Blob(12, "image/x", kDadoC);
  std::vector<std::uint8_t> bytes = MontarBar({{6, 10, 0, 0}}, {blob});
  // Estraga SO o primeiro byte do blob (bDataOffset = 0xff). O blob comeca no
  // fim do ficheiro, porque o construtor poe os dados por ultimo.
  bytes[bytes.size() - blob.size()] = 0xff;
  ArquivoBar a = AbrirSintetico(bytes);
  ASSERT_TRUE(a.Valido()) << a.Motivo();
  const RecursoDoBar r = a.Ler(10, 6);
  ASSERT_TRUE(r.ok) << r.motivo;
  const BlobDoBar b = ArquivoBar::LerBlob(r);
  EXPECT_FALSE(b.ok);
  EXPECT_NE(b.motivo.find("bDataOffset"), std::string::npos) << b.motivo;
}

// VIOLACAO: tirar a comparacao `recurso.dados[1] != 0` de `LerBlob`.
// VIOLACAO: tirar a comparacao `deslocamento < fim_do_mime + 1u` (o dado
// comecaria DENTRO do mime).
TEST(BlobSintetico, RecusaDeslocamentoDentroDoMime) {
  ArquivoBar a = AbrirSintetico(MontarBar({{6, 10, 0, 0}}, {Blob(4, "image/x", kDadoC)}));
  ASSERT_TRUE(a.Valido()) << a.Motivo();
  const RecursoDoBar r = a.Ler(10, 6);
  ASSERT_TRUE(r.ok) << r.motivo;
  const BlobDoBar b = ArquivoBar::LerBlob(r);
  EXPECT_FALSE(b.ok);
  EXPECT_NE(b.motivo.find("bDataOffset"), std::string::npos) << b.motivo;
}

TEST(BlobSintetico, RecusaBlobComOSegundoByteNaoNulo) {
  std::vector<std::uint8_t> m = Blob(12, "image/x", kDadoC);
  m[1] = 7;  // o SDK diz que este byte e sempre zero
  ArquivoBar a = AbrirSintetico(MontarBar({{6, 10, 0, 0}}, {m}));
  ASSERT_TRUE(a.Valido()) << a.Motivo();
  const RecursoDoBar r = a.Ler(10, 6);
  ASSERT_TRUE(r.ok) << r.motivo;
  const BlobDoBar b = ArquivoBar::LerBlob(r);
  EXPECT_FALSE(b.ok);
  EXPECT_NE(b.motivo.find("byte 1 do blob"), std::string::npos) << b.motivo;
}

// VIOLACAO: tirar a recusa do mime VAZIO (`fim_do_mime == 2`).
TEST(BlobSintetico, RecusaMimeVazio) {
  ArquivoBar a = AbrirSintetico(MontarBar({{6, 10, 0, 0}}, {Blob(4, "", kDadoC)}));
  ASSERT_TRUE(a.Valido()) << a.Motivo();
  const RecursoDoBar r = a.Ler(10, 6);
  ASSERT_TRUE(r.ok) << r.motivo;
  const BlobDoBar b = ArquivoBar::LerBlob(r);
  EXPECT_FALSE(b.ok);
  EXPECT_NE(b.motivo.find("mime vazio"), std::string::npos) << b.motivo;
}

// VIOLACAO: tirar a procura do terminador NUL do mime.
TEST(BlobSintetico, RecusaMimeSemTerminador) {
  std::vector<std::uint8_t> m = Blob(12, "image/x", kDadoC);
  // O NUL do mime esta no byte 9 e o enchimento ocupa os bytes 10 e 11: os tres
  // passam a ter letras, e o mime fica sem terminador dentro do recurso.
  m[9] = 'y';
  m[10] = 'z';
  m[11] = 'w';
  ArquivoBar a = AbrirSintetico(MontarBar({{6, 10, 0, 0}}, {m}));
  ASSERT_TRUE(a.Valido()) << a.Motivo();
  const RecursoDoBar r = a.Ler(10, 6);
  ASSERT_TRUE(r.ok) << r.motivo;
  const BlobDoBar b = ArquivoBar::LerBlob(r);
  EXPECT_FALSE(b.ok);
  EXPECT_NE(b.motivo.find("mime"), std::string::npos) << b.motivo;
}

// VIOLACAO: tirar a comparacao `recurso.tamanho < 4` de `LerBlob`.
TEST(BlobSintetico, RecusaRecursoPequenoDemaisParaSerBlob) {
  ArquivoBar a = AbrirSintetico(MontarBar({{6, 10, 0, 0}}, {{1, 0, 2}}));
  ASSERT_TRUE(a.Valido()) << a.Motivo();
  const RecursoDoBar r = a.Ler(10, 6);
  ASSERT_TRUE(r.ok) << r.motivo;
  const BlobDoBar b = ArquivoBar::LerBlob(r);
  EXPECT_FALSE(b.ok);
  EXPECT_NE(b.motivo.find("pequeno demais"), std::string::npos) << b.motivo;
}

// ---------------------------------------------------------------------------
// O `pacmania.bar` DE VERDADE
// ---------------------------------------------------------------------------

TEST(BarDeVerdade, PacmaniaRecurso5091Tipo6) {
  const std::string caminho = CaminhoDoPacmania();
  if (caminho.empty()) {
    GTEST_SKIP() << "sem pacmania.bar nesta maquina: procurei $" << "ZB2_BAR_PACMANIA e "
                 << kPacmaniaBarPadrao << ". O teste NAO passou: nao correu.";
  }
  std::string motivo;
  ArquivoBar a = ArquivoBar::AbrirFicheiro(caminho, &motivo);
  ASSERT_TRUE(a.Valido()) << motivo;
  EXPECT_EQ(a.Tamanho(), 7455569u);
  EXPECT_EQ(a.NumeroDeIds(), 143u);
  EXPECT_EQ(a.NumeroDeRecursos(), 143u);
  ASSERT_EQ(a.Registros().size(), 5u);

  // Os 5 registos, medidos com `python3 tools/medir_bar.py recurso ... 6 5091`.
  EXPECT_EQ(a.Registros()[0].tipo, 6u);
  EXPECT_EQ(a.Registros()[0].primeiro_id, 5001u);
  EXPECT_EQ(a.Registros()[0].delta, 117u);
  EXPECT_EQ(a.Registros()[0].primeiro_indice, 0u);
  EXPECT_EQ(a.Registros()[1].primeiro_id, 5120u);
  EXPECT_EQ(a.Registros()[1].primeiro_indice, 76u);
  EXPECT_EQ(a.Registros()[2].primeiro_id, 5121u);
  EXPECT_EQ(a.Registros()[2].delta, 5u);
  EXPECT_EQ(a.Registros()[2].primeiro_indice, 119u);
  EXPECT_EQ(a.Registros()[3].primeiro_id, 5127u);
  EXPECT_EQ(a.Registros()[3].primeiro_indice, 10u);
  EXPECT_EQ(a.Registros()[4].primeiro_id, 5128u);
  EXPECT_EQ(a.Registros()[4].delta, 16u);
  EXPECT_EQ(a.Registros()[4].primeiro_indice, 126u);

  // O pedido medido na bateria: r2 = 0x000013e3, r3 = 0x00000006,
  // `txt=pacmania.bar`, `lr=0x00110dbc`.
  //   `./build/zb2_bateria "$corpus" "$mods" /tmp/x.json`
  const RecursoDoBar r = a.Ler(0x13e3, 6);
  ASSERT_TRUE(r.ok) << r.motivo;
  EXPECT_EQ(r.id, 5091u);
  EXPECT_EQ(r.tipo, 6u);
  EXPECT_EQ(r.indice, 90u);  // 5091 - 5001 + 0
  EXPECT_EQ(r.tamanho, 34u);

  // Os primeiros bytes do recurso, byte a byte, contra a medicao. O bloco esta
  // em ficheiro[6234497 .. 6234531).
  const std::vector<std::uint8_t> medido = {
      0x1c, 0x00, 0x61, 0x70, 0x70, 0x6c, 0x69, 0x63, 0x61, 0x74, 0x69, 0x6f, 0x6e,
      0x2f, 0x6f, 0x63, 0x74, 0x65, 0x74, 0x2d, 0x73, 0x74, 0x72, 0x65, 0x61, 0x6d,
      0x00, 0x00, 0x01, 0x00, 0x14, 0x36, 0x1e, 0xf0};
  ASSERT_EQ(medido.size(), r.tamanho);
  for (std::size_t k = 0; k < medido.size(); ++k) {
    EXPECT_EQ(r.dados[k], medido[k]) << "byte " << k;
  }

  const BlobDoBar b = ArquivoBar::LerBlob(r);
  ASSERT_TRUE(b.ok) << b.motivo;
  EXPECT_EQ(b.deslocamento, 28);
  EXPECT_EQ(b.mime, "application/octet-stream");
  // O jogo le o dado em [0], [1], [2], [3] e [5] (pacmania.mod, 0x110dd8..0x110df4).
  // O dado tem, medido, 6 bytes: os cinco deslocamentos que ele le existem.
  ASSERT_EQ(b.tamanho, 6u);
  EXPECT_EQ(b.dados[0], 0x01);
  EXPECT_EQ(b.dados[1], 0x00);
  EXPECT_EQ(b.dados[2], 0x14);
  EXPECT_EQ(b.dados[3], 0x36);
  EXPECT_EQ(b.dados[5], 0xf0);
}

TEST(BarDeVerdade, PacmaniaTodosOsIdsQueOJogoPedeTemTamanhoMedido) {
  const std::string caminho = CaminhoDoPacmania();
  if (caminho.empty()) {
    GTEST_SKIP() << "sem pacmania.bar nesta maquina (ver ZB2_BAR_PACMANIA)";
  }
  std::string motivo;
  ArquivoBar a = ArquivoBar::AbrirFicheiro(caminho, &motivo);
  ASSERT_TRUE(a.Valido()) << motivo;

  // Os 7 pedidos de `LoadResDataEx` com id literal no `pacmania.mod`, com o
  // endereco da chamada e o tamanho MEDIDO do recurso que cada um resolve:
  //   /usr/bin/python3 tools/medir_bar.py desmontar "<mods>/276212/pacmania.mod" 0x102d8c 0x40
  // (o id esta no literal carregado para r2 imediatamente antes do `blx ip`)
  const std::vector<std::pair<std::uint16_t, std::uint32_t>> esperado = {
      {5017, 982984}, {5067, 15588}, {5068, 232}, {5069, 4212},
      {5070, 660},    {5071, 724},   {5091, 34}};
  for (const auto& par : esperado) {
    const RecursoDoBar r = a.Ler(par.first, 6);
    ASSERT_TRUE(r.ok) << par.first << ": " << r.motivo;
    EXPECT_EQ(r.tamanho, par.second) << "id " << par.first;
    EXPECT_EQ(r.indice, par.first - 5001) << "id " << par.first;
  }
  // O bloco 5068 (232 bytes) e o que o jogo le como 25 pares de u32 a partir
  // do byte 4 do dado: 4 + 25*8 = 204 = 232 - 28 do cabecalho do blob. E o
  // ajuste do desmonte em 0x10ec78 (`cmp r2, #0x19`).
  const RecursoDoBar bloco = a.Ler(5068, 6);
  ASSERT_TRUE(bloco.ok) << bloco.motivo;
  const BlobDoBar b = ArquivoBar::LerBlob(bloco);
  ASSERT_TRUE(b.ok) << b.motivo;
  EXPECT_EQ(b.tamanho, 204u);
  EXPECT_EQ(4u + 25u * 8u, b.tamanho);

  // O id 5091 nao existe no tipo 1 (RESTYPE_STRING), e o 5000 esta antes do
  // primeiro registo.
  EXPECT_FALSE(a.Ler(5091, 1).ok);
  EXPECT_FALSE(a.Ler(5000, 6).ok);
  EXPECT_FALSE(a.Ler(65535, 6).ok);
  // 5118 e o ULTIMO id do primeiro registo (5001 + 117) e tem de existir: e o
  // que separa "delta" de "contagem" (com contagem, o ultimo id seria 5117).
  EXPECT_TRUE(a.Ler(5118, 6).ok);
  // 5119 esta no buraco antes do registo seguinte (5120) e nao existe.
  EXPECT_FALSE(a.Ler(5119, 6).ok);
  // 5144 e o ultimo id do ultimo registo (5128 + 16); 5145 ja nao existe.
  EXPECT_TRUE(a.Ler(5144, 6).ok);
  EXPECT_FALSE(a.Ler(5145, 6).ok);
}

// Prova INDEPENDENTE do formato, com ids que o VENDEDOR declara.
//
// O `aeecontrols.bar` do SDK foi gerado pelo "BREW Resource Editor", e o
// cabecalho que o acompanha
// (`platform/deprecated/inc/AEEControls_res.h`) declara os ids: 16 bitmaps
// (`AEE_IDB_*`, 1..16, tipo 6 = RESTYPE_IMAGE) e 51 textos (`AEE_IDS_*`, 1..51,
// tipo 1 = RESTYPE_STRING). O ficheiro do locale `es` tem os registos
// (6, 1, 15, 0) e (1, 1, 50, 16) e 67 ids -- exactamente 16 + 51.
TEST(BarDeVerdade, AeeControlsDoSDKConfereComOCabecalhoDoVendedor) {
  const std::string caminho = CaminhoDaAeeControls("es");
  if (caminho.empty()) {
    GTEST_SKIP() << "sem aeecontrols.bar do SDK nesta maquina (ver ZB2_SDK_DIR)";
  }
  std::string motivo;
  ArquivoBar a = ArquivoBar::AbrirFicheiro(caminho, &motivo);
  ASSERT_TRUE(a.Valido()) << motivo;
  ASSERT_EQ(a.Registros().size(), 2u);
  EXPECT_EQ(a.NumeroDeIds(), 67u);
  EXPECT_EQ(a.Registros()[0].tipo, 6u);
  EXPECT_EQ(a.Registros()[0].delta, 15u);  // 16 bitmaps: AEE_IDB_* 1..16
  EXPECT_EQ(a.Registros()[1].tipo, 1u);
  EXPECT_EQ(a.Registros()[1].primeiro_id, 1u);
  EXPECT_EQ(a.Registros()[1].delta, 50u);  // 51 textos: AEE_IDS_* 1..51
  EXPECT_EQ(a.Registros()[1].primeiro_indice, 16u);

  // id 1, tipo 6 (AEE_IDB_SMALLFONTS) -> bitmap, e o dado comeca por "BM".
  const RecursoDoBar imagem = a.Ler(1, 6);
  ASSERT_TRUE(imagem.ok) << imagem.motivo;
  const BlobDoBar blob = ArquivoBar::LerBlob(imagem);
  ASSERT_TRUE(blob.ok) << blob.motivo;
  EXPECT_EQ(blob.mime, "image/bmp");
  ASSERT_GE(blob.tamanho, 2u);
  EXPECT_EQ(blob.dados[0], 'B');
  EXPECT_EQ(blob.dados[1], 'M');
  // O ultimo id do intervalo (16) tambem existe, e o 17 nao.
  EXPECT_TRUE(a.Ler(16, 6).ok);
  EXPECT_FALSE(a.Ler(17, 6).ok);

  // id 1, tipo 1 (AEE_IDS_JANUARY) -> "enero" em espanhol, precedido do byte
  // 0x03 de formato. Medido com `medir_bar.py recurso <ficheiro> 1 1`.
  const RecursoDoBar texto = a.Ler(1, 1);
  ASSERT_TRUE(texto.ok) << texto.motivo;
  ASSERT_EQ(texto.tamanho, 6u);
  EXPECT_EQ(texto.dados[0], 0x03);
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(texto.dados + 1), 5), "enero");
  // id 51 (AEE_IDS_BREW_SUSPENDED) existe; 52 nao.
  EXPECT_TRUE(a.Ler(51, 1).ok);
  EXPECT_FALSE(a.Ler(52, 1).ok);
}

// ---------------------------------------------------------------------------
// OS QUATRO PNGs QUE OS TITULOS DA FRENTE `imgdec` ENTREGAM AO DESCODIFICADOR
// ---------------------------------------------------------------------------
//
// Os quatro titulos que queimam os 8 M passos num laco de conversao de imagem
// (`abd` 279369, `peggle` 278962, `torkandkral` 280463, `heavyweaponbrew`
// 278200) pedem `AEECLSID_PNGDECODER_BREW` ao `CreateInstance`, perguntam-lhe
// `AEEIID_IForceFeed` (0x0101eb0b), escrevem-lhe um recurso e pedem-lhe o
// bitmap. Este teste abre o `.bar` de cada um, resolve o par (tipo, id) que a
// bateria mediu, descodifica o recurso e compara o SHA256 DO BUFFER RGB565
// INTEIRO -- um numero por ficheiro real.
//
// O ORACULO E INDEPENDENTE: os quatro resumos foram medidos com o `zlib` do
// Python (inflate + os cinco filtros por linha da secao 9 do PNG), e nao com
// este descodificador. Os tres que pedem com `tipo=20480` recebem o PNG cru; o
// `peggle` pede com `tipo=6` e recebe um `AEEResBlob` (mime `image/png` impresso
// no recurso, dado em `bDataOffset`) -- e o teste usa o MESMO caminho do blob
// que o `LerBlob` ja serve, para a montagem do recurso tambem ser provada.
std::string RaizDosModsDosPngs() {
  std::vector<std::string> raizes;
  if (const char* env = std::getenv("ZB2_MODS")) {
    if (*env != '\0') raizes.push_back(env);
  } else {
    raizes.push_back("/media/rafaelfrequiao/8C5F-19E51/zeebo/ROMs/debug_nand/mod");
  }
  for (const std::string& r : raizes) {
    std::ifstream f(r + "/279369/data.bar", std::ios::binary);
    if (f) return r;
  }
  return {};
}

TEST(BarDeVerdade, OsQuatroPngsDosTitulosDoImgdecDescodificam) {
  const std::string raiz = RaizDosModsDosPngs();
  if (raiz.empty()) {
    GTEST_SKIP() << "sem a midia do corpus nesta maquina (ZB2_MODS): o teste NAO passou, nao correu";
  }
  struct Alvo {
    const char* rel;      // o ficheiro de recursos, relativo a `mod/`
    std::uint16_t tipo;   // o par medido na corrida
    std::uint16_t id;
    std::uint32_t largura, altura;
    const char* sha_do_rgb565;
    bool tem_alpha;
  };
  const Alvo alvos[] = {
      {"278200/heavyweapon.bar", 20480u, 9346u, 943u, 44u,
       "fe0571c4ac81f20c50c20c14e1d55baad5d18e43c427dd960cfb33620892024c", true},
      // OS OUTROS TRES QUE O `heavyweaponbrew` CARREGA no mesmo `CreateInstance`
      // (medidos no traco: os ids 0x2465..0x2482 e o 0x23ad/0x23b0/0x23c2). O
      // 9136 e a razao de este descodificador servir 4 bits por amostra: e um PNG
      // de PALETA DE 4 BITS, e sem ele o `GetBitmap` recusava e o titulo voltava ao
      // laco de 8 M passos.
      {"278200/heavyweapon.bar", 20480u, 9154u, 60u, 90u,
       "54d9a47d7c9617f8d1f406564a1cc2387567b5b27f05144f3af53bdcaa34dd79", true},
      {"278200/heavyweapon.bar", 20480u, 9133u, 63u, 30u,
       "a69c48033e3f29bdb9e8a7e8f345f1994c027bf657699ab32a65503965e5536d", true},
      {"278200/heavyweapon.bar", 20480u, 9136u, 21u, 20u,
       "553004476d3491743ed4a4788bf64eac5877a1a00aecee4097b88de805fad3fb", false},
      {"278962/resources.bar", 6u, 5000u, 252u, 252u,
       "a1c59a64a66f11ac05052b9644e6f475fee9a84fc2d85ea616e91671dc1e5601", true},
      {"279369/data.bar", 20480u, 9073u, 291u, 125u,
       "8e27ec23c21446bf030d1f85dee70b33aa63b6440b2660aa043a20016a889ca8", true},
      // O `torkandkral` entrega a MESMA imagem do `abd` (os dois `.bar` sao
      // ficheiros diferentes): o mesmo resumo, e isso confirma-o.
      {"280463/data.bar", 20480u, 9008u, 291u, 125u,
       "8e27ec23c21446bf030d1f85dee70b33aa63b6440b2660aa043a20016a889ca8", true},
  };
  for (const Alvo& alvo : alvos) {
    std::string motivo;
    ArquivoBar a = ArquivoBar::AbrirFicheiro(raiz + "/" + alvo.rel, &motivo);
    ASSERT_TRUE(a.Valido()) << alvo.rel << ": " << motivo;
    const RecursoDoBar r = a.Ler(alvo.id, alvo.tipo);
    ASSERT_TRUE(r.ok) << alvo.rel << " id=" << alvo.id << " tipo=" << alvo.tipo << ": " << r.motivo;
    const std::uint8_t* dados = r.dados;
    std::size_t tamanho = r.tamanho;
    if (alvo.tipo == 6u) {
      const BlobDoBar blob = ArquivoBar::LerBlob(r);
      ASSERT_TRUE(blob.ok) << alvo.rel << ": " << blob.motivo;
      EXPECT_EQ(blob.mime, "image/png") << alvo.rel;
      dados = blob.dados;
      tamanho = blob.tamanho;
    }
    zb2::ImagemPng img;
    std::string porque;
    ASSERT_TRUE(zb2::DescodificarPng(dados, tamanho, &img, &porque)) << alvo.rel << ": " << porque;
    EXPECT_EQ(img.largura, alvo.largura) << alvo.rel;
    EXPECT_EQ(img.altura, alvo.altura) << alvo.rel;
    EXPECT_EQ(img.tem_alpha, alvo.tem_alpha) << alvo.rel;
    std::vector<std::uint8_t> bytes(img.pixels.size() * 2u, 0u);
    for (std::size_t k = 0; k < img.pixels.size(); ++k) {
      bytes[k * 2u] = static_cast<std::uint8_t>(img.pixels[k] & 0xFFu);
      bytes[k * 2u + 1u] = static_cast<std::uint8_t>(img.pixels[k] >> 8);
    }
    EXPECT_EQ(zb2::brew::Sha256Hex(bytes.data(), bytes.size()), std::string(alvo.sha_do_rgb565))
        << alvo.rel;
  }
}
