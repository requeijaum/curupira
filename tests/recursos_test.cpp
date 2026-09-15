// Testes do servico de recursos (`core/brew/recursos.{h,cpp}`).
//
// Tres familias, e a divisao e a mesma do `bar_test.cpp`:
//
//  1. `Recursos.*` -- um `.bar` SINTETICO, construido aqui byte a byte, e um
//     alocador de verdade (`core/brew/ajudantes.h`). Sao estes que provam as
//     GUARDAS. Cada teste diz, no comentario, o que partir para ficar VERMELHO
//     (marca `V<n>`), porque uma guarda que passa sem a mudanca nao e guarda.
//
//  2. `RecursosDeVerdade.*` -- o `pacmania.bar` real (id 5091, tipo 6) e o
//     `aeecontrols.bar` do SDK (texto do locale `es`). SALTAM com mensagem
//     quando o ficheiro nao existe nesta maquina -- PULAR, e nao passar em
//     silencio.
//
//  3. `RecursosDependencia.*` -- o contrato do GUEST de que este servico depende:
//     as chamadas medidas ao `LoadResDataEx` passam `pBuf` e `pnBufSize` em
//     `strd r2, r3, [sp]` (pacmania.mod 0x16884 e 0x10d98). Se o `strd` do
//     interpretador nao escrever os dois registradores, os argumentos que chegam
//     ao servico sao LIXO DE FRAME ANTERIOR -- medido: a bateria imprimia
//     `sp0=0x80202d70 sp1=0x0000140b` onde o desmonte diz `[sp] = 0xFFFFFFFF` e
//     `[sp+4] = sp+8`.
//
// Comandos que produzem os numeros citados:
//   python3 tools/medir_bar.py recurso "<mods>/276212/pacmania.bar" 6 5091
//   python3 tools/medir_bar.py censo "<SDK>"
//   /usr/bin/python3 tools/medir_bar.py desmontar "<mods>/276212/pacmania.mod" 0x110d8c 0x40

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/brew/ajudantes.h"
#include "core/brew/recursos.h"
#include "core/cpu/arm_interpreter.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"

namespace {

using zb2::Alocador;
using zb2::ArmInterpreter;
using zb2::kPC;
using zb2::kR2;
using zb2::kR3;
using zb2::kSP;
using zb2::Memoria;
using zb2::RecursoDoBar;
using zb2::Traco;
using zb2::brew::FormaDoRecurso;
using zb2::brew::kSoOTamanho;
using zb2::brew::LeitorDeRecursos;
using zb2::brew::PedidoDeRecurso;
using zb2::brew::PedidoDeTexto;
using zb2::brew::Recursos;
using zb2::brew::ResultadoDoRecurso;
using zb2::brew::ResultadoDoTexto;

// --- um `.bar` sintetico, com a forma MEDIDA em `core/carga/bar.h` ---------

void Escrever16(std::vector<std::uint8_t>* b, std::size_t pos, std::uint16_t v) {
  (*b)[pos] = static_cast<std::uint8_t>(v & 0xff);
  (*b)[pos + 1] = static_cast<std::uint8_t>((v >> 8) & 0xff);
}

void Escrever32(std::vector<std::uint8_t>* b, std::size_t pos, std::uint32_t v) {
  for (int k = 0; k < 4; ++k) (*b)[pos + k] = static_cast<std::uint8_t>((v >> (8 * k)) & 0xff);
}

// O `AEEResBlob`: `bDataOffset`, zero, mime terminado em NUL, enchimento ate ao
// deslocamento, e o dado. O `pacmania` traz 0x1c (28) e
// "application/octet-stream" com 6 bytes de dado.
std::vector<std::uint8_t> Blob(std::uint8_t deslocamento, const std::string& mime,
                               const std::vector<std::uint8_t>& dado) {
  std::vector<std::uint8_t> b;
  b.push_back(deslocamento);
  b.push_back(0);
  b.insert(b.end(), mime.begin(), mime.end());
  b.push_back(0);
  while (b.size() < deslocamento) b.push_back(0);
  b.insert(b.end(), dado.begin(), dado.end());
  return b;
}

// Um recurso de TEXTO: a marca 0x03 e os caracteres de 8 bits.
std::vector<std::uint8_t> Texto(const std::string& caracteres, std::uint8_t marca = 0x03) {
  std::vector<std::uint8_t> b;
  b.push_back(marca);
  b.insert(b.end(), caracteres.begin(), caracteres.end());
  return b;
}

struct RegistoDeTeste {
  std::uint16_t tipo;
  std::uint16_t primeiro_id;
  std::uint16_t delta;
  std::uint16_t primeiro_indice;
};

struct BarSintetico {
  std::vector<std::uint8_t> bytes;
  std::vector<std::vector<std::uint8_t>> recursos;
};

BarSintetico MontarBar(const std::vector<RegistoDeTeste>& registos,
                       const std::vector<std::vector<std::uint8_t>>& recursos) {
  BarSintetico s;
  s.recursos = recursos;
  std::uint32_t num_ids = 0;
  for (const RegistoDeTeste& r : registos) num_ids += static_cast<std::uint32_t>(r.delta) + 1u;
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
  while (indices.size() < num_ids + 1) indices.push_back(indices.back());

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
  for (std::size_t k = 0; k < indices.size(); ++k) Escrever32(&b, off_indices + 4 * k, indices[k]);
  for (std::size_t k = 0; k < dados.size(); ++k) b[off_dados + k] = dados[k];
  s.bytes = std::move(b);
  return s;
}

// --- o banco de testes ------------------------------------------------------

constexpr std::uint32_t kEnderecoDaPilha = 0x8007ff00u;
constexpr std::uint32_t kEnderecoDoPonteiro = kEnderecoDaPilha + 0x40;  // onde vive o *pnBufSize
constexpr std::uint32_t kEnderecoDoBuffer = kEnderecoDaPilha + 0x100;   // buffer do chamador
constexpr std::uint32_t kEnderecoDoTexto = kEnderecoDaPilha + 0x200;    // AECHAR* do LoadResString

class Banco {
 public:
  Banco()
      : mem(&traco),
        alocador(mem, 0x80200000u, 0x00080000u),
        bar(MontarBar({{6, 5001, 1, 0}, {1, 700, 0, 2}, {1, 701, 0, 3}},
                      {Blob(28, "application/octet-stream", {0x01, 0x00, 0x14, 0x36, 0x1e, 0xf0}),
                       Blob(12, "image/bmp", {0xa1, 0xa2, 0xa3, 0xa4}),
                       Texto("ola"),
                       Texto("xy", 0xff)})) {
    // O `*pnBufSize` fica a zero, e os buffers do chamador ficam cheios de 0xAA:
    // um teste que diga "nada foi escrito" tem de ter onde o ver.
    for (std::uint32_t k = 0; k < 0x400; ++k) mem.Escrever8(kEnderecoDaPilha + k, 0xAA);
  }

  LeitorDeRecursos Leitor() {
    return [this](const std::string& nome, std::vector<std::uint8_t>* bytes, std::string* motivo) {
      ++contador_de_leituras;
      if (nome != "teste.bar") {
        *motivo = "nao foi possivel abrir /falso/" + nome;
        return false;
      }
      *bytes = bar.bytes;
      return true;
    };
  }

  Recursos Servico() { return Recursos(mem, alocador, Leitor(), &traco); }

  std::uint32_t Ler32(std::uint32_t a) { return mem.Ler32(a); }
  std::uint8_t Ler8(std::uint32_t a) { return mem.Ler8(a); }
  std::uint16_t Ler16(std::uint32_t a) { return mem.Ler16(a); }
  std::uint32_t TamanhoDoRecurso(std::size_t indice) const {
    return static_cast<std::uint32_t>(bar.recursos[indice].size());
  }

  Traco traco;
  Memoria mem;
  Alocador alocador;
  BarSintetico bar;
  int contador_de_leituras = 0;
};

PedidoDeRecurso Pedido(std::uint16_t id, std::uint32_t buffer) {
  PedidoDeRecurso p;
  p.ficheiro = "teste.bar";
  p.id = id;
  p.tipo = zb2::brew::kTipoImagem;
  p.buffer = buffer;
  p.pn_tamanho = kEnderecoDoPonteiro;
  return p;
}

// --- V1: a forma "so o tamanho" escreve o tamanho e devolve -1 --------------
TEST(Recursos, SoOTamanhoDevolveMenosUmEEscreveOTamanho) {
  Banco b;
  {
    Recursos r = b.Servico();
    const std::uint32_t tamanho = b.TamanhoDoRecurso(0);
    const ResultadoDoRecurso s = r.Atender(Pedido(5001, kSoOTamanho));
    ASSERT_TRUE(s.ok) << s.motivo;
    EXPECT_EQ(s.forma, FormaDoRecurso::Tamanho);
    // V1a: devolver `tamanho` no r0 em vez de `-1` (o "obvio" errado) -- o SDK
    // diz explicitamente "-1 (0xffffffff)" em AEEIShell.h:2457.
    EXPECT_EQ(s.ponteiro, kSoOTamanho);
    // V1b: escrever no `*pnBufSize` o tamanho do DADO em vez do recurso inteiro.
    EXPECT_EQ(b.Ler32(kEnderecoDoPonteiro), tamanho);
    EXPECT_EQ(s.bytes, tamanho);
    EXPECT_GT(tamanho, 6u);  // o recurso inteiro e maior do que os 6 bytes de dado
    EXPECT_EQ(r.Contagem().de_tamanho, 1u);
    EXPECT_EQ(r.Contagem().alocados, 0u);
    EXPECT_EQ(r.Contagem().copiados, 0u);
  }
}

// --- V2: a forma "copiar" copia byte a byte e nao toca em mais nada ---------
TEST(Recursos, CopiarCopiaORecursoEUsaOTamanhoComoEntrada) {
  Banco b;
  {
    Recursos r = b.Servico();
    const std::uint32_t tamanho = b.TamanhoDoRecurso(0);
    b.mem.Escrever32(kEnderecoDoPonteiro, tamanho);  // *pnBufSize de ENTRADA
    const ResultadoDoRecurso s = r.Atender(Pedido(5001, kEnderecoDoBuffer));
    ASSERT_TRUE(s.ok) << s.motivo;
    EXPECT_EQ(s.forma, FormaDoRecurso::Copia);
    EXPECT_EQ(s.ponteiro, kEnderecoDoBuffer);  // devolve o `pBuf`, e nao outro
    EXPECT_EQ(b.Ler32(kEnderecoDoPonteiro), tamanho);
    for (std::uint32_t k = 0; k < tamanho; ++k) {
      EXPECT_EQ(b.Ler8(kEnderecoDoBuffer + k), b.bar.recursos[0][k]) << "byte " << k;
    }
    EXPECT_EQ(b.Ler8(kEnderecoDoBuffer + tamanho), 0xAA);
    EXPECT_EQ(r.Contagem().copiados, 1u);
  }
}

// --- V3: um buffer pequeno RECUSA, e nao escreve meio recurso ---------------
TEST(Recursos, BufferPequenoRecusaENaoEscreveNada) {
  Banco b;
  {
    Recursos r = b.Servico();
    const std::uint32_t tamanho = b.TamanhoDoRecurso(0);
    b.mem.Escrever32(kEnderecoDoPonteiro, tamanho - 1u);
    const ResultadoDoRecurso s = r.Atender(Pedido(5001, kEnderecoDoBuffer));
    // V3: tirar a comparacao `cabem < tamanho` -- o buffer ficava com 33 dos 34
    // bytes e o teste seguinte encontrava um byte escrito.
    EXPECT_FALSE(s.ok);
    EXPECT_EQ(s.forma, FormaDoRecurso::Recusado);
    EXPECT_NE(s.motivo.find("buffer do chamador"), std::string::npos) << s.motivo;
    for (std::uint32_t k = 0; k < 0x40; ++k) {
      EXPECT_EQ(b.Ler8(kEnderecoDoBuffer + k), 0xAA) << "byte " << k;
    }
    EXPECT_EQ(b.Ler32(kEnderecoDoPonteiro), tamanho - 1u);  // nao se reescreve
    EXPECT_EQ(r.Contagem().recusados, 1u);
    EXPECT_EQ(r.Contagem().servidos, 0u);
  }
}

// --- V4: a forma "alocar" aloca no heap do GUEST ---------------------------
TEST(Recursos, AlocarUsaOAlocadorDoGuestEDevolveORecursoInteiro) {
  Banco b;
  {
    Recursos r = b.Servico();
    const std::uint32_t tamanho = b.TamanhoDoRecurso(0);
    const std::uint32_t antes = b.alocador.Alocado();
    const ResultadoDoRecurso s = r.Atender(Pedido(5001, 0));
    ASSERT_TRUE(s.ok) << s.motivo;
    EXPECT_EQ(s.forma, FormaDoRecurso::Alocacao);
    EXPECT_NE(s.ponteiro, 0u);
    // V4a: alocar com `malloc` do hospedeiro -- o ponteiro sairia da faixa do
    // heap do guest e o jogo escreveria em memoria que o emulador nao mapeia.
    EXPECT_GE(s.ponteiro, b.alocador.Inicio());
    EXPECT_LT(s.ponteiro, b.alocador.Inicio() + b.alocador.Tamanho());
    EXPECT_GE(b.alocador.Alocado(), antes + tamanho);
    EXPECT_EQ(b.Ler32(kEnderecoDoPonteiro), tamanho);
    EXPECT_EQ(r.Alocacoes().size(), 1u);
    ASSERT_EQ(r.Alocacoes().count(s.ponteiro), 1u);
    EXPECT_EQ(r.Alocacoes().at(s.ponteiro), tamanho);
    // O primeiro byte no destino e o `bDataOffset` do blob (28), e o byte 1 e
    // zero -- e a forma do `AEEResBlob`, e nao o dado.
    EXPECT_EQ(b.Ler8(s.ponteiro), 28);
    EXPECT_EQ(b.Ler8(s.ponteiro + 1), 0);
    EXPECT_EQ(r.Contagem().alocados, 1u);
  }
}

// --- V5: um id que nao existe RECUSA com o id e o tipo no motivo ------------
TEST(Recursos, IdInexistenteRecusaEmVozAlta) {
  Banco b;
  {
    Recursos r = b.Servico();
    // O caso do corpus: o `pacmania` chama DUAS vezes com `r2 = 0` e o
    // `pacmania.bar` nao tem id 0 (registo medido: 5001..5118, 5120, 5121..5126,
    // 5127, 5128..5144).
    const ResultadoDoRecurso s = r.Atender(Pedido(0, 0));
    EXPECT_FALSE(s.ok);
    EXPECT_NE(s.motivo.find("0x00000000"), std::string::npos) << s.motivo;
    EXPECT_FALSE(r.Recusas().empty());
    EXPECT_EQ(r.Contagem().recusados, 1u);
    EXPECT_TRUE(r.Alocacoes().empty());  // nada foi alocado por causa da recusa
  }
}

// --- V6: um ficheiro que nao existe RECUSA, e nao se tenta abrir duas vezes -
TEST(Recursos, FicheiroInexistenteRecusaEMemoriza) {
  Banco b;
  {
    Recursos r = b.Servico();
    PedidoDeRecurso p = Pedido(5001, 0);
    p.ficheiro = "nao_existe.bar";
    const ResultadoDoRecurso s = r.Atender(p);
    EXPECT_FALSE(s.ok);
    EXPECT_NE(s.motivo.find("nao_existe.bar"), std::string::npos) << s.motivo;
    const ResultadoDoRecurso s2 = r.Atender(p);
    EXPECT_FALSE(s2.ok);
    // V6: tirar a memorizacao da falha -- o leitor seria chamado duas vezes e o
    // contador acusava 2 num ficheiro que ja se sabe que nao existe.
    EXPECT_EQ(b.contador_de_leituras, 1);
    EXPECT_EQ(r.FalhasAoAbrir().count("nao_existe.bar"), 1u);
    EXPECT_EQ(r.FicheirosAbertos(), 0u);
  }
}

// --- V7: o `pnBufSize` nulo recusa (o SDK diz "Cannot be NULL") ------------
TEST(Recursos, PnBufSizeNuloRecusa) {
  Banco b;
  {
    Recursos r = b.Servico();
    PedidoDeRecurso p = Pedido(5001, kSoOTamanho);
    p.pn_tamanho = 0;
    const ResultadoDoRecurso s = r.Atender(p);
    EXPECT_FALSE(s.ok);
    EXPECT_NE(s.motivo.find("pnBufSize"), std::string::npos) << s.motivo;
    EXPECT_EQ(r.FicheirosAbertos(), 0u);  // nem se abre o ficheiro
  }
}

// --- V8: o `FreeResData` so liberta o que o `LoadResData` deu --------------
TEST(Recursos, LibertarSoLibertaOQueFoiAlocadoPorEle) {
  Banco b;
  {
    Recursos r = b.Servico();
    const ResultadoDoRecurso s = r.Atender(Pedido(5001, 0));
    ASSERT_TRUE(s.ok) << s.motivo;
    const std::uint32_t com_o_bloco = b.alocador.Alocado();
    EXPECT_TRUE(r.Libertar(s.ponteiro));
    EXPECT_LT(b.alocador.Alocado(), com_o_bloco);
    EXPECT_TRUE(r.Alocacoes().empty());
    EXPECT_EQ(r.Contagem().libertacoes, 1u);
    EXPECT_EQ(r.Contagem().bytes_libertados, b.TamanhoDoRecurso(0));
    // V8a: libertar outra vez. Sem a lista de alocacoes, o endereco ia ao
    // alocador uma segunda vez -- corrupcao de heap silenciosa.
    EXPECT_FALSE(r.Libertar(s.ponteiro));
    // V8b: libertar um ponteiro que o servico nunca deu.
    const std::uint32_t alheio = b.alocador.Malloc(32);
    ASSERT_NE(alheio, 0u);
    const std::uint32_t antes = b.alocador.Alocado();
    EXPECT_FALSE(r.Libertar(alheio));
    EXPECT_EQ(b.alocador.Alocado(), antes);
    // V8c: `FreeResData(po, NULL)` e "nao ha nada para libertar", e o contrato
    // do metodo e `void`: conta-se e nao se recusa.
    EXPECT_TRUE(r.Libertar(0));
    EXPECT_EQ(r.Contagem().libertacoes_de_nulo, 1u);
    b.alocador.Free(alheio);
  }
}

// --- o motivo de cada recusa fica agrupado, para a lista de demanda --------
TEST(Recursos, AsRecusasFicamAgrupadasPorMotivo) {
  Banco b;
  {
    Recursos r = b.Servico();
    for (int k = 0; k < 3; ++k) {
      const ResultadoDoRecurso s = r.Atender(Pedido(0, 0));
      EXPECT_FALSE(s.ok);
    }
    ASSERT_EQ(r.Recusas().size(), 1u);
    EXPECT_EQ(r.Recusas().begin()->second, 3u);
    EXPECT_EQ(r.Contagem().pedidos, 3u);
    EXPECT_EQ(r.Contagem().recusados, 3u);
    // E o motivo NOMEIA o metodo do SDK, e nao "recusado": e o que faz a lista
    // de demanda dizer de que interface se trata (P2).
    EXPECT_NE(b.traco.ContagemFaltas().count("IShell::LoadResDataEx"), 0u);
  }
}

// --- o `.bar` e lido UMA vez, e a tabela de recursos diz o que se serviu ----
TEST(Recursos, OFicheiroEABertoUmaSoVezEATabelaDizOMesmo) {
  Banco b;
  {
    Recursos r = b.Servico();
    ASSERT_TRUE(r.Atender(Pedido(5001, 0)).ok);
    ASSERT_TRUE(r.Atender(Pedido(5002, 0)).ok);
    EXPECT_EQ(b.contador_de_leituras, 1);
    EXPECT_EQ(r.FicheirosAbertos(), 1u);
    ASSERT_NE(r.Arquivo("teste.bar"), nullptr);
    EXPECT_EQ(r.Arquivo("teste.bar")->NumeroDeRecursos(), 4u);
    // A tabela de recursos: `(id, tipo) -> bytes`, e o que a tabela diz e o que
    // o servico serviu, byte a byte (e a MESMA leitura, e por isso tem de
    // coincidir).
    const RecursoDoBar da_tabela = r.Recurso("teste.bar", 5001, zb2::brew::kTipoImagem);
    ASSERT_TRUE(da_tabela.ok) << da_tabela.motivo;
    EXPECT_EQ(da_tabela.tamanho, b.TamanhoDoRecurso(0));
    for (std::uint32_t k = 0; k < da_tabela.tamanho; ++k) {
      EXPECT_EQ(da_tabela.dados[k], b.bar.recursos[0][k]) << k;
    }
    EXPECT_FALSE(r.Recurso("teste.bar", 4999, zb2::brew::kTipoImagem).ok);
    EXPECT_FALSE(r.Recurso("outro.bar", 5001, zb2::brew::kTipoImagem).ok);
  }
}

// --- o mime do blob, que e o que diz ao jogo o que ele vai descodificar ----
TEST(Recursos, OMimeDoBlobSaiNoResultado) {
  Banco b;
  {
    Recursos r = b.Servico();
    const ResultadoDoRecurso s = r.Atender(Pedido(5001, kSoOTamanho));
    ASSERT_TRUE(s.ok) << s.motivo;
    EXPECT_EQ(s.mime, "application/octet-stream");
    const ResultadoDoRecurso s2 = r.Atender(Pedido(5002, kSoOTamanho));
    ASSERT_TRUE(s2.ok) << s2.motivo;
    EXPECT_EQ(s2.mime, "image/bmp");
    EXPECT_EQ(s2.bytes, b.TamanhoDoRecurso(1));
  }
}

// --- o `LoadResString` (slot 17) -------------------------------------------

PedidoDeTexto PedidoDeTextoDeTeste(std::uint16_t id, std::uint32_t n_bytes) {
  PedidoDeTexto p;
  p.ficheiro = "teste.bar";
  p.id = id;
  p.destino = kEnderecoDoTexto;
  p.n_bytes = n_bytes;
  return p;
}

// --- V9: o texto de 8 bits vira AECHAR, com terminador ---------------------
TEST(Recursos, TextoDe8BitsViraUtf16ComTerminador) {
  Banco b;
  {
    Recursos r = b.Servico();
    const ResultadoDoTexto t = r.ServirTexto(PedidoDeTextoDeTeste(700, 32));
    ASSERT_TRUE(t.ok) << t.motivo;
    EXPECT_EQ(t.marca, 0x03);
    EXPECT_EQ(t.caracteres, 3u);
    EXPECT_EQ(t.bytes_do_recurso, 4u);  // a marca + "ola"
    EXPECT_EQ(b.Ler16(kEnderecoDoTexto + 0), 0x006fu);  // 'o'
    EXPECT_EQ(b.Ler16(kEnderecoDoTexto + 2), 0x006cu);  // 'l'
    EXPECT_EQ(b.Ler16(kEnderecoDoTexto + 4), 0x0061u);  // 'a'
    // V9: tirar o terminador -- o byte seguinte volta a ser 0xAA.
    EXPECT_EQ(b.Ler16(kEnderecoDoTexto + 6), 0x0000u);
    EXPECT_EQ(b.Ler8(kEnderecoDoTexto + 8), 0xAA);
    EXPECT_EQ(r.Contagem().textos, 1u);
  }
}

// --- V10: um buffer curto para o texto RECUSA e nao escreve metade --------
TEST(Recursos, TextoComBufferCurtoRecusa) {
  Banco b;
  {
    Recursos r = b.Servico();
    const ResultadoDoTexto t = r.ServirTexto(PedidoDeTextoDeTeste(700, 6));  // 3 chars pedem 8 bytes
    EXPECT_FALSE(t.ok);
    EXPECT_NE(t.motivo.find("buffer de destino"), std::string::npos) << t.motivo;
    for (std::uint32_t k = 0; k < 0x20; ++k) {
      EXPECT_EQ(b.Ler8(kEnderecoDoTexto + k), 0xAA) << "byte " << k;
    }
  }
}

// --- V11: uma marca de texto que nao esta medida RECUSA com o NOME dela ---
TEST(Recursos, TextoComMarcaDe16BitsRecusaPeloNome) {
  Banco b;
  {
    Recursos r = b.Servico();
    const ResultadoDoTexto t = r.ServirTexto(PedidoDeTextoDeTeste(701, 32));
    EXPECT_FALSE(t.ok);
    // Os `aeecontrols.bar` do `he`, do `ja` e do `ko` comecam por 0xFF/0xFD/0xFE.
    EXPECT_NE(t.motivo.find("0xff"), std::string::npos) << t.motivo;
    EXPECT_EQ(t.marca, 0xff);
    for (std::uint32_t k = 0; k < 0x10; ++k) EXPECT_EQ(b.Ler8(kEnderecoDoTexto + k), 0xAA);
  }
}

// --- V12: um id de texto que nao existe RECUSA ---------------------------
TEST(Recursos, TextoComIdInexistenteRecusa) {
  Banco b;
  {
    Recursos r = b.Servico();
    const ResultadoDoTexto t = r.ServirTexto(PedidoDeTextoDeTeste(702, 32));
    EXPECT_FALSE(t.ok);
    EXPECT_NE(t.motivo.find("0x000002be"), std::string::npos) << t.motivo;  // 702
    // E pedir um TEXTO com o id de uma IMAGEM tambem recusa: o tipo faz parte da
    // chave do directorio do `.bar`.
    const ResultadoDoTexto t2 = r.ServirTexto(PedidoDeTextoDeTeste(5001, 32));
    EXPECT_FALSE(t2.ok);
  }
}

// --- o MIF do proprio modulo (base NULA) ----------------------------------
//
// No SDK a base NULA e legal e seleciona a cadeia do PROPRIO modulo (MIF):
// `ISHELL_GetAppAuthor(ps,buf,tam) = LoadResString(ps,NULL,IDS_MIF_COMPANY=6,...)`
// (`AEEShell.h:919-921`; `IDS_MIF_*` em `AEEShell.h:129-131`: 6, 7, 8). O `.mif`
// e o MESMO contentor do `.bar` (magic 0x0011 -- o `MontarBar` acima serve para
// o construir) e mora na pasta IRMA da pasta do modulo:
// `<pai de dir_>/mif/<pasta_>.mif` (medido: 62 ficheiros no corpus, um por
// titulo). O despacho passa esse caminho no `pedido.ficheiro` quando a base e
// nula -- a linha vive no patch separado `rec-despacho.patch`, porque o
// `despacho.cpp` esta ocupado por outra frente.

std::string EscreverMifTemp(const std::string& nome, const std::vector<std::uint8_t>& bytes) {
  const std::filesystem::path pasta =
      std::filesystem::temp_directory_path() / "zb2_recursos_mif";
  std::filesystem::create_directories(pasta);
  const std::filesystem::path caminho = pasta / nome;
  std::ofstream f(caminho, std::ios::binary);
  f.write(reinterpret_cast<const char*>(bytes.data()),
          static_cast<std::streamsize>(bytes.size()));
  f.close();
  return caminho.string();
}

PedidoDeTexto PedidoDeMifDoModulo(const std::string& caminho, std::uint16_t id,
                                  std::uint32_t n_bytes) {
  PedidoDeTexto p;
  p.ficheiro = caminho;
  p.base_nula = true;
  p.id = id;
  p.destino = kEnderecoDoTexto;
  p.n_bytes = n_bytes;
  return p;
}

// --- V13 (novo): a base nula le a cadeia do proprio modulo no `.mif` -------
TEST(Recursos, TextoComBaseNulaLeAVersaoDoMif) {
  // id 8 = IDS_MIF_VERSION; o conteudo MEDIDO do 280173.mif e `03 "1.0.20"`.
  const std::string caminho =
      EscreverMifTemp("versao.mif", MontarBar({{1, 8, 0, 0}}, {Texto("1.0.20")}).bytes);
  Banco b;
  {
    Recursos r = b.Servico();
    const ResultadoDoTexto t = r.ServirTexto(PedidoDeMifDoModulo(caminho, 8, 32));
    ASSERT_TRUE(t.ok) << t.motivo;
    EXPECT_EQ(t.caracteres, 6u);
    EXPECT_EQ(t.marca, 0x03);
    const std::uint16_t esperado[6] = {'1', '.', '0', '.', '2', '0'};
    for (int k = 0; k < 6; ++k) {
      EXPECT_EQ(b.Ler16(kEnderecoDoTexto + k * 2), esperado[k]) << "byte " << k;
    }
    // V13a: o terminador, como no ramo do `.bar`.
    EXPECT_EQ(b.Ler16(kEnderecoDoTexto + 12), 0x0000u);
    EXPECT_EQ(b.Ler8(kEnderecoDoTexto + 14), 0xAA);
    EXPECT_EQ(r.Contagem().textos, 1u);
    EXPECT_EQ(r.Contagem().servidos, 1u);
    EXPECT_TRUE(r.Recusas().empty());
  }
}

// --- V14: o BOM UTF-16 menor-primeiro (0xFF 0xFE) do `.mif` ----------------
// Medido em 135 dos recursos de texto dos 62 `.mif` (ex.: 12875.mif id 6).
TEST(Recursos, TextoDoMifComBomUtf16MenorPrimeiro) {
  const std::vector<std::uint8_t> recurso = {0xff, 0xfe, '1', 0, '.', 0,
                                             '0',  0,    '.', 0, '2', 0,
                                             '0',  0};
  const std::string caminho =
      EscreverMifTemp("bom_le.mif", MontarBar({{1, 8, 0, 0}}, {recurso}).bytes);
  Banco b;
  {
    Recursos r = b.Servico();
    const ResultadoDoTexto t = r.ServirTexto(PedidoDeMifDoModulo(caminho, 8, 32));
    ASSERT_TRUE(t.ok) << t.motivo;
    EXPECT_EQ(t.caracteres, 6u);
    EXPECT_EQ(b.Ler16(kEnderecoDoTexto + 0), '1');
    EXPECT_EQ(b.Ler16(kEnderecoDoTexto + 8), '2');
    EXPECT_EQ(b.Ler16(kEnderecoDoTexto + 12), 0x0000u);  // o terminador
  }
}

// --- V15: o BOM UTF-16 maior-primeiro (0xFE 0xFF) --------------------------
TEST(Recursos, TextoDoMifComBomUtf16MaiorPrimeiro) {
  const std::vector<std::uint8_t> recurso = {0xfe, 0xff, 0, '1', 0, '.', 0, '0'};
  const std::string caminho =
      EscreverMifTemp("bom_be.mif", MontarBar({{1, 8, 0, 0}}, {recurso}).bytes);
  Banco b;
  {
    Recursos r = b.Servico();
    const ResultadoDoTexto t = r.ServirTexto(PedidoDeMifDoModulo(caminho, 8, 32));
    ASSERT_TRUE(t.ok) << t.motivo;
    EXPECT_EQ(t.caracteres, 3u);  // "1.0", com o BOM fora da conta
    EXPECT_EQ(b.Ler16(kEnderecoDoTexto + 0), '1');
    EXPECT_EQ(b.Ler16(kEnderecoDoTexto + 2), '.');
    EXPECT_EQ(b.Ler16(kEnderecoDoTexto + 4), '0');
    EXPECT_EQ(b.Ler16(kEnderecoDoTexto + 6), 0x0000u);
  }
}

// --- V16: um rodape depois do ultimo deslocamento --------------------------
// 12 de 62 `.mif` do corpus trazem 20 bytes a mais, DEPOIS do ultimo valor da
// tabela de deslocamentos (medido: 274791.mif, 274803.mif, ...). O conteudo dos
// recursos termina no ultimo deslocamento; servir nao pode depender disso.
TEST(Recursos, TextoDoMifComRodapeDeVinteBytes) {
  BarSintetico m = MontarBar({{1, 8, 0, 0}}, {Texto("1.0.20")});
  m.bytes.insert(m.bytes.end(), 20, 0xAB);  // o rodape de 20 bytes
  const std::string caminho = EscreverMifTemp("rodape.mif", m.bytes);
  Banco b;
  {
    Recursos r = b.Servico();
    const ResultadoDoTexto t = r.ServirTexto(PedidoDeMifDoModulo(caminho, 8, 32));
    ASSERT_TRUE(t.ok) << t.motivo;
    EXPECT_EQ(t.caracteres, 6u);
  }
}

// --- V17: uma marca que nao esta medida recusa pelo NOME dela --------------
TEST(Recursos, TextoDoMifComMarcaDesconhecidaRecusaPeloNome) {
  BarSintetico m = MontarBar({{1, 8, 0, 0}}, {{0xfd, 0xfe, 'x', 0}});
  const std::string caminho = EscreverMifTemp("marca_bad.mif", m.bytes);
  Banco b;
  {
    Recursos r = b.Servico();
    const ResultadoDoTexto t = r.ServirTexto(PedidoDeMifDoModulo(caminho, 8, 32));
    EXPECT_FALSE(t.ok);
    EXPECT_NE(t.motivo.find("0xfd"), std::string::npos) << t.motivo;
    for (std::uint32_t k = 0; k < 0x20; ++k) {
      EXPECT_EQ(b.Ler8(kEnderecoDoTexto + k), 0xAA) << "byte " << k;
    }
    EXPECT_EQ(r.Contagem().recusados, 1u);
  }
}

// --- V18: um id que nao existe no `.mif` recusa com o id -------------------
TEST(Recursos, TextoDoMifComIdInexistenteRecusa) {
  const std::string caminho =
      EscreverMifTemp("sem_id.mif", MontarBar({{1, 6, 0, 0}}, {Texto("x")}).bytes);
  Banco b;
  {
    Recursos r = b.Servico();
    const ResultadoDoTexto t = r.ServirTexto(PedidoDeMifDoModulo(caminho, 8, 32));
    EXPECT_FALSE(t.ok);
    EXPECT_NE(t.motivo.find("0x00000008"), std::string::npos) << t.motivo;
    EXPECT_EQ(r.Contagem().recusados, 1u);
  }
}

// --- V19: um `.mif` que nao existe recusa com o caminho --------------------
TEST(Recursos, TextoDoMifInexistenteRecusa) {
  const std::string caminho =
      std::filesystem::temp_directory_path().string() + "/zb2_nao_existe.mif";
  Banco b;
  {
    Recursos r = b.Servico();
    const ResultadoDoTexto t = r.ServirTexto(PedidoDeMifDoModulo(caminho, 8, 32));
    EXPECT_FALSE(t.ok);
    EXPECT_NE(t.motivo.find("nao foi possivel abrir"), std::string::npos) << t.motivo;
    EXPECT_NE(t.motivo.find(caminho), std::string::npos) << t.motivo;
    EXPECT_EQ(r.Contagem().recusados, 1u);
  }
}

// --- V20: sem localizacao (despacho sem o patch) recusa com o id ----------
// O `.mif` NAO mora dentro da pasta do titulo, e quem sabe o caminho e o
// despacho (patch separado). Sem ele, a recusa continua a dizer o id -- e a
// registar a falta, que e o que a demanda da bateria mostra.
TEST(Recursos, TextoComBaseNulaSemLocalizacaoRecusaComOId) {
  Banco b;
  {
    Recursos r = b.Servico();
    PedidoDeTexto p;
    p.ficheiro = "";
    p.base_nula = true;
    p.id = 6;  // IDS_MIF_COMPANY
    p.destino = kEnderecoDoTexto;
    p.n_bytes = 32;
    const ResultadoDoTexto t = r.ServirTexto(p);
    EXPECT_FALSE(t.ok);
    EXPECT_NE(t.motivo.find(".mif"), std::string::npos) << t.motivo;
    EXPECT_NE(t.motivo.find("0x00000006"), std::string::npos) << t.motivo;
    for (std::uint32_t k = 0; k < 0x20; ++k) {
      EXPECT_EQ(b.Ler8(kEnderecoDoTexto + k), 0xAA) << "byte " << k;
    }
    EXPECT_NE(b.traco.ContagemFaltas().count("IShell::LoadResString"), 0u);
    EXPECT_EQ(r.Contagem().textos, 1u);
    EXPECT_EQ(r.Contagem().recusados, 1u);
  }
}


// --- com os ficheiros de verdade ------------------------------------------

std::string CaminhoDoPacmania() {
  const char* env = std::getenv("ZB2_BAR_PACMANIA");
  if (env != nullptr && *env != '\0') return env;
  const std::string padrao =
      "/media/rafaelfrequiao/8C5F-19E51/zeebo/ROMs/debug_nand/mod/276212/pacmania.bar";
  std::ifstream f(padrao, std::ios::binary);
  return f ? padrao : std::string();
}

std::string CaminhoDaAeeControls(const char* locale) {
  std::vector<std::string> raizes;
  if (const char* env = std::getenv("ZB2_SDK_DIR")) raizes.push_back(env);
#ifdef ZB2_RAIZ_DO_REPO
  raizes.push_back(std::string(ZB2_RAIZ_DO_REPO) + "/research/docs/sdk-extract/" +
                   "BrewMPSDK-7.12.5/SDKPro/1.0.4.601 Pro");
#endif
  for (const std::string& raiz : raizes) {
    const std::string caminho =
        raiz + "/sck/platform/system/brewcore/src/BREWSim/components/" + locale +
        "/256Color/aeecontrols.bar";
    std::ifstream f(caminho, std::ios::binary);
    if (f) return caminho;
  }
  return std::string();
}

TEST(RecursosDeVerdade, OPacmaniaServeOId5091EOBlobMedido) {
  const std::string caminho = CaminhoDoPacmania();
  if (caminho.empty()) {
    GTEST_SKIP() << "pacmania.bar nao existe nesta maquina -- PULAR, e nao passar";
  }
  Traco traco;
  Memoria mem(&traco);
  Alocador alocador(mem, 0x80200000u, 0x00080000u);
  std::ifstream f(caminho, std::ios::binary);
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                  std::istreambuf_iterator<char>());
  ASSERT_FALSE(bytes.empty());
  const LeitorDeRecursos leitor =
      [&bytes](const std::string& nome, std::vector<std::uint8_t>* destino, std::string* motivo) {
        if (nome != "pacmania.bar") {
          *motivo = "so o pacmania.bar esta registado neste teste";
          return false;
        }
        *destino = bytes;
        return true;
      };
  Recursos r(mem, alocador, leitor, &traco);

  // O sitio medido: `pacmania.mod:0x110db8`, formando o pedido com a ABI do SDK.
  PedidoDeRecurso p;
  p.ficheiro = "pacmania.bar";
  p.id = 0x13e3;  // 5091 -- o literal em mem[0x10e8c]
  p.tipo = 6;     // RESTYPE_IMAGE
  p.buffer = 0;   // pBuf NULL: alocar
  p.pn_tamanho = kEnderecoDoPonteiro;
  const ResultadoDoRecurso s = r.Atender(p);
  ASSERT_TRUE(s.ok) << s.motivo;

  // Os numeros do `core/carga/bar.h` (seccao 4), medidos no recurso 90:
  // 34 bytes de recurso, `bDataOffset` 0x1c, mime "application/octet-stream",
  // e 6 bytes de dado `01 00 14 36 1e f0`.
  EXPECT_EQ(s.bytes, 34u);
  EXPECT_EQ(s.mime, "application/octet-stream");
  EXPECT_EQ(mem.Ler32(kEnderecoDoPonteiro), 34u);
  EXPECT_EQ(mem.Ler8(s.ponteiro), 28u);
  EXPECT_EQ(mem.Ler8(s.ponteiro + 1), 0u);
  const std::uint32_t dado = s.ponteiro + 28;
  const std::uint8_t esperado[6] = {0x01, 0x00, 0x14, 0x36, 0x1e, 0xf0};
  for (int k = 0; k < 6; ++k) {
    EXPECT_EQ(mem.Ler8(dado + static_cast<std::uint32_t>(k)), esperado[k]) << k;
  }

  // E o id 0, que e o que o corpus pede duas vezes, RECUSA-SE em voz alta.
  PedidoDeRecurso zero = p;
  zero.id = 0;  // as duas chamadas medidas com `r2 = 0` (o id dinamico)
  const ResultadoDoRecurso sz = r.Atender(zero);
  EXPECT_FALSE(sz.ok);
  EXPECT_NE(sz.motivo.find("0x00000000"), std::string::npos) << sz.motivo;
}

TEST(RecursosDeVerdade, OAeeControlsDoSdkServeJaneiroEmEspanhol) {
  const std::string caminho = CaminhoDaAeeControls("es");
  if (caminho.empty()) {
    GTEST_SKIP() << "aeecontrols.bar do SDK nao existe nesta maquina -- PULAR, e nao passar";
  }
  Traco traco;
  Memoria mem(&traco);
  Alocador alocador(mem, 0x80200000u, 0x00080000u);
  std::ifstream f(caminho, std::ios::binary);
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                  std::istreambuf_iterator<char>());
  const LeitorDeRecursos leitor =
      [&bytes](const std::string&, std::vector<std::uint8_t>* destino, std::string*) {
        *destino = bytes;
        return true;
      };
  Recursos r(mem, alocador, leitor, &traco);
  // `AEEControls_res.h` -- "AUTO-GENERATED BY BREW Resource Editor" -- diz
  // `AEE_IDS_JANUARY = 1`, e o recurso medido e `03 "enero"`.
  const ResultadoDoTexto t = r.ServirTexto(PedidoDeTextoDeTeste(1, 64));
  ASSERT_TRUE(t.ok) << t.motivo;
  EXPECT_EQ(t.caracteres, 5u);
  const std::uint16_t esperado[5] = {'e', 'n', 'e', 'r', 'o'};
  for (int k = 0; k < 5; ++k) EXPECT_EQ(mem.Ler16(kEnderecoDoTexto + k * 2), esperado[k]) << k;
}

// --- V21: os 3 titulos da demanda, com os `.mif` de verdade -----------------
// A demanda medida: chessbots (263019), alpineracerex (276151) e allstarcards
// (280173) pedem o slot 17 1x cada, base NULA, id 8 (IDS_MIF_VERSION). O `.mif`
// de cada um vive na pasta IRMA da `mod/` da bateria (na NAND de debug:
// `debug_nand/mif/<pasta>.mif`).

std::string CaminhoDoMif(const char* pasta) {
  const std::vector<std::string> raizes = {
      "/media/rafaelfrequiao/8C5F-19E51/zeebo/ROMs/debug_nand/mif",
      "/home/rafaelfrequiao/projects/zeebo-lab/games/brew/mif",
  };
  for (const std::string& raiz : raizes) {
    const std::string caminho = raiz + "/" + pasta + ".mif";
    std::ifstream f(caminho, std::ios::binary);
    if (f) return caminho;
  }
  return std::string();
}

TEST(RecursosDeVerdade, OsTresTitulosDaDemandaServemAVersaoDoMif) {
  struct Titulo {
    const char* pasta;
    const char* mod;
    const char* versao;
  };
  const Titulo titulos[] = {
      {"263019", "chessbots", "1.2.29"},
      {"276151", "alpineracerex", "1.0.1"},
      {"280173", "allstarcards", "1.0.20"},
  };
  std::vector<std::string> caminhos;
  for (const Titulo& t : titulos) caminhos.push_back(CaminhoDoMif(t.pasta));
  for (const std::string& c : caminhos) {
    if (c.empty()) {
      GTEST_SKIP() << "os `.mif` do corpus nao existem nesta maquina -- PULAR, e nao passar";
    }
  }
  Traco traco;
  Memoria mem(&traco);
  Alocador alocador(mem, 0x80200000u, 0x00080000u);
  const LeitorDeRecursos leitor = [](const std::string&, std::vector<std::uint8_t>*, std::string*) {
    return false;  // o MIF le-se pelo caminho, nao pelo leitor
  };
  Recursos r(mem, alocador, leitor, &traco);
  for (std::size_t i = 0; i < std::size(titulos); ++i) {
    const Titulo& t = titulos[i];
    const ResultadoDoTexto txt = r.ServirTexto(PedidoDeMifDoModulo(caminhos[i], 8, 64));
    ASSERT_TRUE(txt.ok) << t.mod << " (" << t.pasta << "): " << txt.motivo;
    EXPECT_EQ(txt.caracteres, std::strlen(t.versao)) << t.mod;
    for (std::size_t k = 0; k < std::strlen(t.versao); ++k) {
      EXPECT_EQ(mem.Ler16(kEnderecoDoTexto + static_cast<std::uint32_t>(k) * 2),
                static_cast<std::uint16_t>(t.versao[k]))
          << t.mod << " byte " << k;
    }
    const std::uint32_t fim = static_cast<std::uint32_t>(std::strlen(t.versao)) * 2u;
    EXPECT_EQ(mem.Ler16(kEnderecoDoTexto + fim), 0x0000u) << t.mod;
  }
}

// ---------------------------------------------------------------------------
// A DEPENDENCIA DO GUEST: o `strd` que alimenta as chamadas medidas.
// ---------------------------------------------------------------------------

// `strd rT, rT2, [sp]` -- ARM ARM, "extra load/store": bits 7-4 = 1111 para o
// STRD (1101 seria o LDRD), `Rt2` e o registrador a seguir ao `Rt`. Montado a
// partir dos campos, e nao escrito a mao: o valor 0xe1cd20f0 (o do
// `pacmania.mod:0x16884`) sai daqui, e ha um teste que o confere.
constexpr std::uint32_t kAl = 0xEu;
constexpr std::uint32_t StrdImediato(std::uint32_t rn, std::uint32_t rt, std::uint32_t imediato) {
  return (kAl << 28) | (1u << 24) | (1u << 23) | (1u << 22) | ((rn & 0xF) << 16) |
         ((rt & 0xF) << 12) | ((imediato & 0xF0) << 4) | 0xF0u | (imediato & 0xF);
}

TEST(RecursosDependencia, AStrDoPacmaniaSaiDoMontadorDoTeste) {
  // Se esta conta falhar, o teste seguinte nao esta a executar a instrucao que
  // diz executar -- e um teste que prova a coisa errada e pior do que nenhum.
  EXPECT_EQ(StrdImediato(13, 2, 0), 0xe1cd20f0u);
}

TEST(RecursosDependencia, StrdEscreveOsDoisRegistradoresNaPilha) {
  Traco traco;
  Memoria mem(&traco);
  ArmInterpreter cpu(mem, &traco);
  const std::uint32_t pilha = 0x8007ff78u;
  cpu.Repor(0x00010000u, pilha);
  cpu.Set(kR2, 0xFFFFFFFFu);
  cpu.Set(kR3, pilha + 8);
  mem.Escrever32(pilha, 0xDEADBEEFu);
  mem.Escrever32(pilha + 4, 0xDEADBEEFu);
  mem.Escrever32(0x00010000u, StrdImediato(13, 2, 0));  // strd r2, r3, [sp]
  cpu.Passo();
  EXPECT_EQ(cpu.InstruscoesRecusadas(), 0u)
      << "o STRD foi recusado em vez de executado (o que seria melhor do que agora)";
  // As duas palavras que o `LoadResDataEx` le do `[sp]` e do `[sp+4]`.
  EXPECT_EQ(mem.Ler32(pilha), 0xFFFFFFFFu);
  EXPECT_EQ(mem.Ler32(pilha + 4), pilha + 8);
  // E o `r2` NAO pode ser tocado: e o `nResID` da chamada, e no pacmania ele e
  // reposto logo a seguir (`uxth r2, r8`), mas noutro sitio qualquer isso nao
  // aconteceria. Um `strd` executado como um MOV deixava aqui outra coisa.
  EXPECT_EQ(cpu.Get(kR2), 0xFFFFFFFFu);
  EXPECT_EQ(cpu.Get(kSP), pilha);

  // O segundo sitio medido: `strd r2, r3, [sp]` com `r2` a ZERO
  // (`pacmania.mod:0x10d98`), que e o `pBuf = NULL` da forma de alocacao.
  cpu.Set(kR2, 0u);
  cpu.Set(kR3, pilha + 8);
  cpu.Set(kPC, 0x00010000u);
  cpu.Set(kSP, pilha);
  mem.Escrever32(pilha, 0xDEADBEEFu);
  cpu.Passo();
  EXPECT_EQ(mem.Ler32(pilha), 0u);
  EXPECT_EQ(mem.Ler32(pilha + 4), pilha + 8);
}


// A codificacao das extensoes de 16 bits: cond 0110 1111 (bits 27-20), `Rd` nos
// bits 15-12, `rotate` nos bits 11-8, `op` nos bits 7-4 (0x2 SXTB, 0x3 UXTB,
// 0x6 SXTH, 0x7 UXTH) e `Rm` nos bits 3-0.
// A codificacao real das extensoes ARMv6:
// sxtb=0x06AF0070, sxth=0x06BF0070, uxtb=0x06EF0070, uxth=0x06FF0070
constexpr std::uint32_t Extende(std::uint32_t op, std::uint32_t rd, std::uint32_t rm,
                                std::uint32_t rotate = 0) {
  std::uint32_t base_op = 0x06FF0070u;
  if (op == 0x2) base_op = 0x06AF0070u;      // sxtb
  else if (op == 0x6) base_op = 0x06BF0070u; // sxth
  else if (op == 0x3) base_op = 0x06EF0070u; // uxtb
  else if (op == 0x7) base_op = 0x06FF0070u; // uxth
  return (kAl << 28) | base_op | ((rd & 0xF) << 12) | ((rotate & 0xF) << 8) | (rm & 0xF);
}

TEST(RecursosDependencia, AUxthDoPacmaniaSaiDoMontadorDoTeste) {
  // `0011688c  uxth r2, r8` -- a instrucao que, executada como `ldrb r2, [pc]`,
  // punha o id do recurso a ZERO.
  EXPECT_EQ(Extende(0x7, 2, 8), 0xe6ff2078u);
}

TEST(RecursosDependencia, UxthPreservaOIdELimpaOsBitsDeCima) {
  Traco traco;
  Memoria mem(&traco);
  ArmInterpreter cpu(mem, &traco);
  cpu.Repor(0x00010000u, 0x8007ff78u);
  mem.Escrever32(0x00010000u, Extende(0x7, 2, 8));  // uxth r2, r8
  cpu.Set(8, 0x0000140bu);  // 5131, o id que o corpus pede
  cpu.Passo();
  EXPECT_EQ(cpu.InstruscoesRecusadas(), 0u);
  EXPECT_EQ(cpu.Get(kR2), 0x0000140bu) << "o id do recurso tem de CHEGAR ao servico";
  // E o `uxth` limpa os bits de cima: com lixo em cima, o id e o mesmo.
  cpu.Set(8, 0xffff140bu);
  cpu.Set(kPC, 0x00010000u);
  cpu.Passo();
  EXPECT_EQ(cpu.Get(kR2), 0x0000140bu);
  // As outras tres: `uxtb`, `sxth` e `sxtb`.
  mem.Escrever32(0x00010000u, Extende(0x3, 2, 8));  // uxtb
  cpu.Set(8, 0x00000140u);
  cpu.Set(kPC, 0x00010000u);
  cpu.Passo();
  EXPECT_EQ(cpu.Get(kR2), 0x40u);
  mem.Escrever32(0x00010000u, Extende(0x6, 2, 8));  // sxth
  cpu.Set(8, 0x0000fff0u);
  cpu.Set(kPC, 0x00010000u);
  cpu.Passo();
  EXPECT_EQ(cpu.Get(kR2), 0xfffffff0u);
  mem.Escrever32(0x00010000u, Extende(0x2, 2, 8));  // sxtb
  cpu.Set(8, 0x00000080u);
  cpu.Set(kPC, 0x00010000u);
  cpu.Passo();
  EXPECT_EQ(cpu.Get(kR2), 0xffffff80u);
  // O `rotate` NAO esta medido (zero em 17296 de 17296 no corpus): recusa-se.
  mem.Escrever32(0x00010000u, Extende(0x7, 2, 8, 1));
  cpu.Set(kPC, 0x00010000u);
  cpu.Passo();
  EXPECT_EQ(cpu.InstruscoesRecusadas(), 1u);
}

}  // namespace
