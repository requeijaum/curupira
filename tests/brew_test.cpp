#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/brew/arquivo.h"
#include "core/brew/formato.h"
#include "core/brew/interface.h"
#include "core/brew/sha256.h"
#include "core/brew/tela.h"
#include "core/brew/vfs.h"
#include "core/memoria/memoria.h"

namespace zb2::brew {
namespace {

// Escreve uma cadeia no guest. Sem isto, cada teste teria o seu laco.
void PorCadeia(Memoria& mem, Endereco onde, const std::string& s) {
  for (std::size_t k = 0; k < s.size(); ++k) {
    mem.Escrever8(onde + static_cast<Endereco>(k), static_cast<std::uint8_t>(s[k]));
  }
  mem.Escrever8(onde + static_cast<Endereco>(s.size()), 0);
}

std::string LerCadeia(Memoria& mem, Endereco onde) {
  std::string s;
  for (int k = 0; k < 512; ++k) {
    const char ch = static_cast<char>(mem.Ler8(onde + static_cast<Endereco>(k)));
    if (ch == 0) break;
    s.push_back(ch);
  }
  return s;
}

// ---------------------------------------------------------------------------
// FORMATO. Os testes comparam com o `snprintf` DO SISTEMA: e a unica referencia
// que nao fui eu que escrevi, e foi escrita para isto.
// ---------------------------------------------------------------------------

std::string Formata(const std::string& formato, const std::vector<std::uint32_t>& args) {
  Memoria mem(nullptr);
  constexpr Endereco kBuf = 0x00100000;
  constexpr Endereco kFmt = 0x00101000;
  PorCadeia(mem, kFmt, formato);
  Formatar(mem, kBuf, kFmt, args.data(), static_cast<int>(args.size()));
  return LerCadeia(mem, kBuf);
}

TEST(Formato, InteiroComSinal) {
  char esperado[64];
  std::snprintf(esperado, sizeof(esperado), "%d", -12345);
  EXPECT_EQ(Formata("%d", {static_cast<std::uint32_t>(-12345)}), esperado);
}

TEST(Formato, HexadecimalComLargura) {
  // `%08x` e o que o BREW usa para identificadores. Sem a largura, o jogo le um
  // numero mais curto do que espera.
  char esperado[64];
  std::snprintf(esperado, sizeof(esperado), "%08x", 0x1a2bu);
  EXPECT_EQ(Formata("%08x", {0x1a2bu}), esperado);
}

TEST(Formato, CadeiaLiteraleEUmInteiro) {
  Memoria mem(nullptr);
  constexpr Endereco kBuf = 0x00100000, kFmt = 0x00101000, kArg = 0x00102000;
  PorCadeia(mem, kFmt, "n=%s/%d");
  PorCadeia(mem, kArg, "oi");
  const std::uint32_t args[2] = {kArg, 7};
  Formatar(mem, kBuf, kFmt, args, 2);
  EXPECT_EQ(LerCadeia(mem, kBuf), "n=oi/7");
}

TEST(Formato, PorCentoDuploNaoConsomeArgumento) {
  EXPECT_EQ(Formata("100%%", {}), "100%");
}

TEST(Formato, EspecificadorDesconhecidoSaiComoEstava) {
  // Um especificador que nao se conhece NAO pode virar lixo silencioso: sai como
  // veio, para o defeito ser visivel no texto.
  EXPECT_EQ(Formata("%q", {}), "%q");
}

TEST(Formato, NaoPassaDaMemoriaEscrita) {
  // Um `%s` apontado a memoria virgem tem de parar. Sem limite, percorreria o
  // espaco todo.
  const std::string s = Formata("%s", {0x7F000000u});
  EXPECT_LT(s.size(), 16u);
}

// ---------------------------------------------------------------------------
// TELA. A medida VISUAL, e o limite que ela ja deixou passar uma vez.
// ---------------------------------------------------------------------------

TEST(Tela, RetanguloCheioContaOsPixels) {
  Tela t;
  t.Retangulo(0, 0, 10, 5, true);
  EXPECT_EQ(t.Escritos(), 50u);
}

TEST(Tela, RetanguloEnormeNaoPercorreOEspacoTodo) {
  // MEDIDO, e foi a causa de mais de 900 segundos para UM titulo: o laco corria
  // `w*h` vezes porque o limite estava so no `Ponto`. Este teste falha por
  // DEMORA se o limite voltar para o sitio errado.
  Tela t;
  t.Retangulo(0, 0, 0xFFFFFFF0u, 0xFFFFFFF0u, true);
  EXPECT_LE(t.Escritos(), static_cast<std::uint32_t>(Tela::kLargura * Tela::kAltura));
}

TEST(Tela, ClipLimitaAEscrita) {
  Tela t;
  t.Clip(10, 10, 4, 4);
  t.Retangulo(0, 0, 100, 100, true);
  EXPECT_EQ(t.Escritos(), 16u);
}

TEST(Tela, CorDistintaConta) {
  Tela t;
  t.CorAtual(0x1234);
  t.Retangulo(0, 0, 4, 4, true);
  t.CorAtual(0x5678);
  t.Retangulo(4, 0, 4, 4, true);
  EXPECT_EQ(t.CoresDistintas(), 3u);  // as duas cores e o fundo
}

// ---------------------------------------------------------------------------
// VFS. A normalizacao e UMA SO, e por isso e que se testa.
// ---------------------------------------------------------------------------

TEST(Vfs, BarrasEEspacosColapsam) {
  Vfs v;
  const std::string n = v.Normalizar("\\pasta//sub\\x.dat");
  EXPECT_EQ(n, "");  // nao registado -> vazio, mas sem rebentar
}

TEST(Vfs, PontoPontoNaoSaiDaPasta) {
  Vfs v;
  // O `..` e RETIRADO, e nao resolvido para o pai: um titulo que peca `../../x`
  // fica com `x` dentro da sua propria pasta.
  EXPECT_EQ(v.Normalizar("../../x"), "");
  EXPECT_EQ(v.Normalizar("../x"), "");
}

// ---------------------------------------------------------------------------
// FICHEIROS. Um ficheiro de verdade, num directoria temporaria.
// ---------------------------------------------------------------------------

class ArquivosTeste : public ::testing::Test {
 protected:
  void SetUp() override {
    pasta_ = std::filesystem::temp_directory_path() / "zb2_teste_arquivos";
    std::filesystem::create_directories(pasta_);
    std::ofstream f(pasta_ / "dados.bin", std::ios::binary);
    for (int k = 0; k < 256; ++k) f.put(static_cast<char>(k));
    f.close();
    vfs_.Registar(pasta_.string());
  }
  void TearDown() override { std::filesystem::remove_all(pasta_); }

  Vfs vfs_;
  std::filesystem::path pasta_;
  Memoria mem_{nullptr};
};

TEST_F(ArquivosTeste, LeOPedidoEEncurtaNoFim) {
  Arquivos a(&vfs_);
  const std::uint32_t id = a.Abrir("dados.bin", 0x0001u, pasta_.string());
  ASSERT_NE(id, 0u);
  constexpr Endereco kDest = 0x00100000;
  EXPECT_EQ(a.Ler(id, mem_, kDest, 10), 10);
  EXPECT_EQ(mem_.Ler8(kDest), 0);
  EXPECT_EQ(mem_.Ler8(kDest + 9), 9);
  // Pede mais do que resta: curto, e nao inventado.
  EXPECT_EQ(a.Ler(id, mem_, kDest, 1000), 246);
  EXPECT_EQ(a.Ler(id, mem_, kDest, 10), 0);
}

TEST_F(ArquivosTeste, PosicionaEmRelacaoAoInicioEaoFim) {
  Arquivos a(&vfs_);
  const std::uint32_t id = a.Abrir("dados.bin", 0x0001u, pasta_.string());
  ASSERT_NE(id, 0u);
  EXPECT_EQ(a.Posicionar(id, 0, 10), 10);
  EXPECT_EQ(a.Posicionar(id, 1, 5), 15);
  EXPECT_EQ(a.Posicionar(id, 2, 0), 256);
  // Fora do ficheiro: -1, e a posicao NAO muda.
  EXPECT_EQ(a.Posicionar(id, 0, 999), -1);
  EXPECT_EQ(a.Posicionar(id, 0, 256), 256);
}

TEST_F(ArquivosTeste, InformacaoTrazOTamanho) {
  Arquivos a(&vfs_);
  const std::uint32_t id = a.Abrir("dados.bin", 0x0001u, pasta_.string());
  ASSERT_NE(id, 0u);
  constexpr Endereco kInfo = 0x00100000;
  EXPECT_TRUE(a.Informacao(id, mem_, kInfo));
  EXPECT_EQ(mem_.Ler32(kInfo + 8), 256u);
}

TEST_F(ArquivosTeste, ModoQueMudaOFicheiroERecusado) {
  // `_OFM_CREATE` (4) e `_OFM_APPEND` (8) mudam o ficheiro. A VFS e so de leitura
  // por DECISAO, e a recusa e em voz alta: um objecto nulo, e nao um ficheiro que
  // finge aceitar escrita.
  Arquivos a(&vfs_);
  EXPECT_EQ(a.Abrir("novo.bin", 0x0004u, pasta_.string()), 0u);
  EXPECT_EQ(a.Abrir("dados.bin", 0x0008u, pasta_.string()), 0u);
  EXPECT_EQ(a.Abrir("dados.bin", 0x0002u, pasta_.string()), 0u);
  EXPECT_TRUE(a.ModoMudaOFicheiro(0x0004u | 0x0001u));
  EXPECT_FALSE(a.ModoMudaOFicheiro(0x0001u));
}

TEST_F(ArquivosTeste, FicheiroInexistenteNaoAbre) {
  Arquivos a(&vfs_);
  EXPECT_EQ(a.Abrir("nao_existe.dat", 0x0001u, pasta_.string()), 0u);
}

// ---------------------------------------------------------------------------
// INTERFACE. A construcao dos objectos e a cablagem das vtables.
// ---------------------------------------------------------------------------

TEST(Interface, OObjetoApontaParaAVtableEAIBaseEstaNosSlotsZeroEUm) {
  Memoria mem(nullptr);
  Saidas s;  // a faixa de saida, com os enderecos derivados do indice
  ConstruirObjeto(mem, s, kObjShell, s.Endereco(kVtableShell), 64, kBaseDoShell);
  EXPECT_EQ(mem.Ler32(kObjShell), s.Endereco(kVtableShell));
  EXPECT_EQ(mem.Ler32(kObjShell + 4), 1u);
  EXPECT_EQ(mem.Ler32(s.Endereco(kVtableShell) + 0), s.Endereco(3));  // AddRef
  EXPECT_EQ(mem.Ler32(s.Endereco(kVtableShell) + 4), s.Endereco(4));  // Release
}

TEST(Interface, CadaSlotTemUmEnderecoDiferente) {
  // Um stub so para todos dava "algo do shell" sem nome. Um endereco por slot e o
  // que faz o registo de faltas dizer QUAL metodo foi pedido.
  Memoria mem(nullptr);
  Saidas s;
  ConstruirObjeto(mem, s, kObjDisplay, s.Endereco(kVtableDisplay), 64, kVtableDisplay);
  std::set<std::uint32_t> vistos;
  for (std::uint32_t i = 2; i < 32; ++i) {
    vistos.insert(mem.Ler32(s.Endereco(kVtableDisplay) + i * 4));
  }
  EXPECT_EQ(vistos.size(), 30u);
}

TEST(Interface, CablarEscreveEConfirma) {
  Memoria mem(nullptr);
  Saidas s;
  ConstruirObjeto(mem, s, kObjDisplay, s.Endereco(kVtableDisplay), 64, kVtableDisplay);
  const brew::Ligacao l[] = {
      {kVtableDisplay, brew_slots::kDisplay_DrawText, 7777},
      {kVtableDisplay, brew_slots::kDisplay_GetFontMetrics, 7778},
  };
  const auto r = Cablar(mem, s, l, 2);
  EXPECT_TRUE(r.ok) << r.motivo;
  EXPECT_EQ(mem.Ler32(s.Endereco(kVtableDisplay) + brew_slots::kDisplay_DrawText * 4),
            s.Endereco(7777));
}

TEST(Interface, CablarRecusaOSlotDaIBase) {
  // GUARDA PROVADA POR VIOLACAO: sem esta recusa, escrever no slot 2 (que num
  // SDK em que a IBase tivesse tres membros seria `Release`) destruiria a IBase
  // inteira sem nada a acusar.
  Memoria mem(nullptr);
  Saidas s;
  ConstruirObjeto(mem, s, kObjDisplay, s.Endereco(kVtableDisplay), 64, kVtableDisplay);
  const brew::Ligacao l[] = {{kVtableDisplay, 1, 1234}};
  const auto r = Cablar(mem, s, l, 1);
  EXPECT_FALSE(r.ok);
  EXPECT_NE(r.motivo.find("IBase"), std::string::npos);
  // E O SLOT NAO FOI TOCADO.
  EXPECT_EQ(mem.Ler32(s.Endereco(kVtableDisplay) + 4), s.Endereco(4));
}

TEST(Interface, CablarAceitaOSlotUmDoObjetoFicheiro) {
  // A UNICA EXCECAO, e ela e declarada: o objecto ficheiro nao tem `Release`, e o
  // jogo fecha ficheiros com `IFILE_Release` (slot 1).
  Memoria mem(nullptr);
  Saidas s;
  const brew::Ligacao l[] = {{kVtableFileObj, 1, 4242}};
  const auto r = Cablar(mem, s, l, 1);
  EXPECT_TRUE(r.ok) << r.motivo;
}

TEST(Interface, CablarRecusaUmSlotForaDaVtable) {
  Memoria mem(nullptr);
  Saidas s;
  ConstruirObjeto(mem, s, kObjDisplay, s.Endereco(kVtableDisplay), 64, kVtableDisplay);
  const brew::Ligacao l[] = {{kVtableDisplay, 999, 1234}};
  const auto r = Cablar(mem, s, l, 1);
  EXPECT_FALSE(r.ok);
}

TEST(Interface, CablarReescreveUmaEntradaAlteradaPorBaixo) {
  // ESTE TESTE CHAMAVA-SE `CablarDetetaUmaCablagemPerdida`, E O NOME MENTIA.
  //
  // Achado pela auditoria do plano, com prova por mutacao: **o teste fica VERDE
  // quando a LEITURA DE VOLTA e arrancada do `Cablar`.** Ou seja, ele nao mede a
  // deteccao -- mede a REESCRITA, que acontece antes da leitura.
  //
  // O `Cablar` escreve, e depois confirma. Neste teste o valor foi alterado ANTES
  // da chamada, logo a escrita corrige-o -- e a confirmacao passa por construcao.
  // **A deteccao nao esta a ser exercitada por nada.**
  //
  // A licao, e e a terceira vez nesta arvore: **um teste cujo nome e cujo comentario
  // afirmam o que ele NAO faz.** Ficam aqui as duas partes separadas, para o nome
  // nao voltar a prometer o que o corpo nao cumpre.
  //
  // (O que a deteccao apanharia, de verdade: uma escrita que chegasse DEPOIS da
  // leitura de volta. O `Cablar` nao tem esse caminho: escreve, le, e devolve. A
  // deteccao existe para o caso de OUTRO escritor aparecer pelo meio, e esse caso
  // nao e reproduzivel num teste de unidade sem um escritor concorrente -- o que
  // esta arvore proibe por desenho, P6.)
  Memoria mem(nullptr);
  Saidas s;
  ConstruirObjeto(mem, s, kObjShell, s.Endereco(kVtableShell), 64, kBaseDoShell);
  const brew::Ligacao l[] = {{kVtableShell, brew_slots::kShell_SetTimer, 5555}};
  // Simula a perda: alguem escreveu outro valor por cima DEPOIS da cablagem.
  mem.Escrever32(s.Endereco(kVtableShell) + brew_slots::kShell_SetTimer * 4, s.Endereco(1));
  const auto r = Cablar(mem, s, l, 1);
  EXPECT_TRUE(r.ok) << "a cablagem deve ser reescrita e confirmada";
  EXPECT_EQ(mem.Ler32(s.Endereco(kVtableShell) + brew_slots::kShell_SetTimer * 4),
            s.Endereco(5555));
}

TEST(Interface, AsConstantesDeSlotSaoAsDoCabecalho) {
  // Guardas de sanidade sobre a geracao. Se alguem voltar a contar TRES membros
  // na IBase, estes numeros mudam e o teste FICA VERMELHO.
  EXPECT_EQ(brew_slots::kShell_CreateInstance, 2u);
  EXPECT_EQ(brew_slots::kShell_QueryClass, 3u);
  EXPECT_EQ(brew_slots::kShell_SetTimer, 11u);
  EXPECT_EQ(brew_slots::kShell_CancelTimer, 12u);
  EXPECT_EQ(brew_slots::kDisplay_GetFontMetrics, 2u);
  EXPECT_EQ(brew_slots::kDisplay_DrawText, 4u);
  EXPECT_EQ(brew_slots::kFileMgr_OpenFile, 2u);
  EXPECT_EQ(brew_slots::kIFile_Seek, 7u);
}

// ---------------------------------------------------------------------------
// SHA-256. VECTORES CONHECIDOS (FIPS 180-4), e nao o que eu acho que devia dar.
// ---------------------------------------------------------------------------

TEST(Sha256, VectoresConhecidos) {
  // Os tres vectores do FIPS 180-4, mais o das mil repeticoes de 'a'. Se a
  // implementacao estiver errada, qualquer um deles falha -- e um resumo de
  // ficheiro errado faz o comparador comparar entradas diferentes como se fossem
  // a mesma.
  EXPECT_EQ(Sha256Hex(std::string("")),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  EXPECT_EQ(Sha256Hex(std::string("abc")),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  EXPECT_EQ(Sha256Hex(std::string("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")),
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  EXPECT_EQ(Sha256Hex(std::string(1000000, 'a')),
            "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(Sha256, UmByteDiferenteDaOutroResumo) {
  // O ponto todo: dois corpus que difiram num byte tem de dar resumos diferentes.
  EXPECT_NE(Sha256Hex(std::string("corpus A")), Sha256Hex(std::string("corpus B")));
}

TEST(Sha256, BlocosDeTamanhosDiferentes) {
  // 55, 56, 64 e 65 bytes: os tamanhos a volta da fronteira do preenchimento, que
  // e onde uma implementacao errada acerta por acaso.
  for (int n : {54, 55, 56, 63, 64, 65, 119, 120}) {
    const std::string s(static_cast<std::size_t>(n), 'x');
    EXPECT_EQ(Sha256Hex(s).size(), 64u) << n;
    EXPECT_NE(Sha256Hex(s), Sha256Hex(s + "x")) << n;
  }
}

}  // namespace
}  // namespace zb2::brew
