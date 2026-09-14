#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

#include "core/carga/mod.h"
#include "core/memoria/memoria.h"

namespace zb2 {
namespace {

// A BASE DO MODULO, DERIVADA DOS PROPRIOS BYTES DO MODULO.
//
// ==================== O DEFEITO ====================
//
// A base era `0x00100000`, e era uma CONVENCAO HERDADA -- nao uma medicao. O
// comentario do carregador da arvore antiga dizia ate "loadable at any base
// address", que e a afirmacao que o defeito torna falsa.
//
// **O modulo NAO e carregavel em qualquer base: os literais dele sao OFFSETS DO
// FICHEIRO, usados como ENDERECOS ABSOLUTOS.** Um `ldr r0,[pc,#imm]` que devia
// carregar o endereco de uma cadeia carrega o OFFSET dela no ficheiro, e o codigo
// le-o como ponteiro.
//
// Com a base a 0x00100000 esses literais caem FORA da imagem mapeada, e
// `Memoria::Ler8` devolve zero em endereco nao mapeado -- logo o jogo le zero em
// vez do dado. Silenciosamente.
//
// ==================== A PROVA ====================
//
// Se a base for ZERO, cada literal que aponta para texto tem de cair EXACTAMENTE
// no inicio de uma cadeia. Este teste conta quantos o fazem, com a base a zero e
// com a base antiga, e exige que a zero sejam muitos e com a antiga sejam ZERO.
//
// MEDIDO em `pacmania.mod` (148.400 bytes): 51 literais apontam para texto com a
// base a zero, e **0 com a base a 0x00100000** -- porque com essa base os literais
// nem estao dentro da imagem.
//
// ==================== O EFEITO, na bateria dos 62 ====================
//
//     base 0x00100000:   modulo 48 | applet 22
//     base 0x00001000:   modulo 62 | applet 41
//     base 0x00000000:   modulo 62 | applet 41
//
// 14 modulos e 19 applets que simplesmente nao apareciam.

// Um literal e "aponta para texto" quando o valor cai dentro da imagem E os bytes
// desse ponto sao texto imprimivel seguido de zero.
bool ApontaParaTexto(const std::vector<std::uint8_t>& dw, std::uint32_t base, std::uint32_t v,
                     std::string* texto) {
  if (v < base) return false;
  const std::uint64_t off = static_cast<std::uint64_t>(v) - base;
  if (off + 8 > dw.size()) return false;
  const std::uint8_t* p = dw.data() + off;
  if (p[0] < 'A' || p[0] > 'z') return false;
  std::size_t n = 0;
  while (n < 24 && p[n] != 0) {
    if (p[n] < 0x20 || p[n] > 0x7e) return false;
    ++n;
  }
  if (n < 4) return false;
  *texto = std::string(reinterpret_cast<const char*>(p), n);
  return true;
}

std::size_t ContarLiteraisParaTexto(const std::vector<std::uint8_t>& dw, std::uint32_t base) {
  std::size_t n = 0;
  for (std::size_t off = 0; off + 4 <= dw.size(); off += 4) {
    std::uint32_t v = 0;
    std::memcpy(&v, dw.data() + off, 4);
    std::string t;
    if (ApontaParaTexto(dw, base, v, &t)) ++n;
  }
  return n;
}

std::vector<std::uint8_t> Ler(const std::string& c, bool* ok) {
  std::ifstream f(c, std::ios::binary);
  if (!f) { *ok = false; return {}; }
  std::vector<std::uint8_t> v((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
  *ok = !v.empty();
  return v;
}

// Acha um `.mod` do corpus. Sem ele, o teste SALTA -- e diz que saltou.
std::string AcharMod(const std::string& nome) {
  const std::string raiz = std::string(ZB2_RAIZ_DO_REPO);
  for (const char* raiz_mod : {"/media/rafaelfrequiao/8C5F-19E51/zeebo/ROMs/debug_nand/mod",
                               "/home/rafaelfrequiao/projects/zeebo-lab/games/brew/mod"}) {
    if (!std::ifstream(std::string(raiz_mod)).good()) continue;
    (void)raiz;
    // Procura a pasta que tem este `.mod`, sem depender de saber o numero.
    std::system(("ls " + std::string(raiz_mod) + "/*/" + nome +
                 ".mod 2>/dev/null | head -1 > /tmp/zb2_mod.txt")
                    .c_str());
    std::ifstream f("/tmp/zb2_mod.txt");
    std::string caminho;
    std::getline(f, caminho);
    if (!caminho.empty()) return caminho;
  }
  return {};
}

TEST(BaseDoModulo, OsLiteraisSaoOffsetsDoFicheiro) {
  const std::string caminho = AcharMod("pacmania");
  if (caminho.empty()) {
    GTEST_SKIP() << "SALTADO: o pacmania.mod nao esta nesta maquina. A base NAO foi "
                    "verificada contra a midia real.";
  }
  bool ok = false;
  const std::vector<std::uint8_t> dw = Ler(caminho, &ok);
  ASSERT_TRUE(ok) << caminho;

  const std::size_t com_base_antiga = ContarLiteraisParaTexto(dw, 0x00100000u);
  const std::size_t com_base_zero = ContarLiteraisParaTexto(dw, 0u);
  const std::size_t com_base_1000 = ContarLiteraisParaTexto(dw, 0x00001000u);

  // Com a base antiga: ZERO. Os literais nem caem dentro da imagem.
  EXPECT_EQ(com_base_antiga, 0u)
      << "com a base 0x00100000 nenhum literal devia apontar para texto";
  // Com a base a zero: muitos, e cada um deles no INICIO de uma cadeia.
  EXPECT_GT(com_base_zero, 20u);
  // A base 0x1000 tambem "acerta" em alguns, mas por ACIDENTE: o literal cai
  // dentro da imagem desviado de 0x1000, e o que la esta pode ser texto por
  // coincidencia. A base zero e a unica em que o literal cai no OFFSET EXATO.
  EXPECT_LT(com_base_1000, com_base_zero)
      << "a base 0x1000 nao pode acertar mais do que a base zero, que e a exacta";
}

TEST(BaseDoModulo, AImagemCabeQuandoABaseEZero) {
  // A base zero tem de ser ACEITE pelo carregador: era ela que ele recusava.
  Memoria mem(nullptr);
  const std::vector<std::uint8_t> imagem{0x10, 0x20, 0x30, 0x40};
  const ResultadoDaCarga r = CarregarMod(mem, imagem, 0u, 0x80010000u, nullptr);
  EXPECT_TRUE(r.ok) << r.motivo;
  EXPECT_EQ(r.base, 0u);
  EXPECT_EQ(mem.Ler8(0), 0x10);
  EXPECT_EQ(mem.Ler8(3), 0x40);
  // O ponteiro ROPI vai para base-4, que com base zero e 0xFFFFFFFC. A aritmetica
  // do uint32_t faz o wrap-around e a memoria e esparsa: e uma pagina como outra
  // qualquer. A alternativa era PROIBIR a unica base que faz o modulo funcionar.
  EXPECT_EQ(mem.Ler32(0xFFFFFFFCu), 0x80010000u);
}

TEST(BaseDoModulo, UmaBaseDeslocadaNaoAcerta) {
  // A prova de que a base zero nao e so "menos ma": deslocar a base estraga os
  // literais. Com a base a 4, o literal `v` cai em `v - 4`, que e o ultimo byte do
  // que vem antes -- logo o padrao de texto tem de se perder.
  bool ok = false;
  const std::string caminho = AcharMod("pacmania");
  if (caminho.empty()) GTEST_SKIP() << "SALTADO: sem o pacmania.mod";
  const std::vector<std::uint8_t> dw = Ler(caminho, &ok);
  ASSERT_TRUE(ok);
  EXPECT_LT(ContarLiteraisParaTexto(dw, 4u), ContarLiteraisParaTexto(dw, 0u));
}

}  // namespace
}  // namespace zb2
