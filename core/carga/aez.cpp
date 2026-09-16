#include "core/carga/aez.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "core/carga/inflate.h"
#include "core/carga/png.h"  // Crc32DePng: o MESMO CRC-32 do rodape do gzip

namespace zb2 {
namespace {

std::uint16_t Ler16(const std::uint8_t* p) {
  return static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
}

std::uint32_t Ler32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

void Recusar(std::string* motivo, const std::string& porque) {
  if (motivo != nullptr) *motivo = porque;
}

std::string Minusculas(const std::string& s) {
  std::string r = s;
  for (char& c : r) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return r;
}

// O GZIP (RFC1952) dun payload, com o RODAPE como prova.
//
// O CRC-32 do rodape e o MESMO polinomio do PNG: usa-se o `Crc32DePng` do
// `core/carga/png.h`, que existe exposto exactamente para nao haver uma segunda
// copia da regra nesta arvore (o `despacho.cpp` tem a sua, no gzip do `.bar`, e
// essa e de outra frente).
//
// O `Inflar` da arvore descomprime o zlib (RFC1950) dos `.pkg` e confere o
// adler32 DELE; aqui o corpo deflate e envolvido num cabecalho zlib sintetico
// com os 4 bytes de adler a ZERO, logo o `Inflar` acaba de descomprimir e recusa
// no adler (sem limpar a saida -- o `despacho.cpp` conta com isso e este modulo
// tambem). A PROVA do gzip e o rodape: `CRC32(saida)` e `ISIZE`, e ainda o
// tamanho DESCOMPRIMIDO declarado no registo. Nada disto depende do adler
// falsificado, e um payload de verdade corrompido falha no CRC.
//
// `esperado` = o tamanho descomprimido do registo (4.o campo). `teto` limita a
// saida pelo mesmo valor; um `esperado` a zero nao pode desligar o teto, porque
// no `Inflar` `teto == 0` significa "sem teto".
bool DescomprimirGzip(const std::uint8_t* entrada, std::size_t tamanho, std::uint32_t esperado,
                      std::vector<std::uint8_t>* saida, std::string* motivo) {
  if (tamanho < aez_campos::kMinimoDoGzip) {
    Recusar(motivo, "payload de " + std::to_string(tamanho) + " bytes: o gzip tem " +
                        std::to_string(aez_campos::kMinimoDoGzip) +
                        " so de cabecalho (10) e rodape (8)");
    return false;
  }
  if (entrada[0] != aez_campos::kGzipId1 || entrada[1] != aez_campos::kGzipId2) {
    char b[96];
    std::snprintf(b, sizeof(b), "payload comeca em %02x %02x e nao no 1f 8b do RFC1952", entrada[0],
                  entrada[1]);
    Recusar(motivo, b);
    return false;
  }
  if (entrada[2] != 8u) {
    Recusar(motivo, "CM=" + std::to_string(entrada[2]) + ": o gzip so define 8 (deflate)");
    return false;
  }
  const std::uint8_t flg = entrada[3];
  if ((flg & 0xE0u) != 0u) {
    char b[80];
    std::snprintf(b, sizeof(b), "FLG=0x%02x: os bits reservados do RFC1952 estao a zero", flg);
    Recusar(motivo, b);
    return false;
  }

  // O cabecalho tem 10 bytes fixos mais os campos OPCIONAIS que o FLG pede.
  std::size_t p = 10;
  if ((flg & 0x04u) != 0u) {  // FEXTRA
    if (p + 2 > tamanho) {
      Recusar(motivo, "FEXTRA ligado e faltam os 2 bytes do XLEN");
      return false;
    }
    const std::size_t xlen = Ler16(entrada + p);
    p += 2 + xlen;
  }
  for (int campo = 0; campo < 2; ++campo) {  // FNAME (0x08) e FCOMMENT (0x10)
    const std::uint8_t mascara = (campo == 0) ? 0x08u : 0x10u;
    if ((flg & mascara) == 0u) continue;
    while (p < tamanho && entrada[p] != 0) ++p;
    if (p >= tamanho) {
      Recusar(motivo, campo == 0 ? "FNAME ligado e o nome nao acaba em NUL"
                                 : "FCOMMENT ligado e o comentario nao acaba em NUL");
      return false;
    }
    ++p;  // o NUL faz parte do campo
  }
  if ((flg & 0x02u) != 0u) p += 2;  // FHCRC
  if (p + 8 > tamanho) {
    Recusar(motivo, "o cabecalho do gzip ocupa " + std::to_string(p) + " bytes e o rodape do RFC1952 tem 8, " +
                        "e o payload so tem " + std::to_string(tamanho));
    return false;
  }

  const std::size_t plen = tamanho - p - 8;
  const std::uint32_t crc_esperado = Ler32(entrada + tamanho - 8);
  const std::uint32_t isize = Ler32(entrada + tamanho - 4);

  // O corpo deflate dentro de um zlib sintetico: cabecalho de 2 e 4 bytes de
  // adler a zero (o `Inflar` confere-o e recusa -- de proposito).
  std::vector<std::uint8_t> zlibbuf(2u + plen + 4u, 0u);
  zlibbuf[0] = 0x78;
  zlibbuf[1] = 0x01;
  if (plen != 0) std::memcpy(&zlibbuf[2], entrada + p, plen);

  std::string motivo_inflate;
  const std::uint32_t teto =
      esperado != 0u ? esperado : static_cast<std::uint32_t>(aez_campos::kTetoDescomprimido);
  const bool ok_inflate =
      Inflar(zlibbuf.data(), zlibbuf.size(), saida, &motivo_inflate, teto, nullptr);

  // A PROVA: o rodape do gzip E o tamanho declarado no registo. `ok_inflate`
  // nao entra na decisao -- o adler sintetico faz dele falso sempre que o adler
  // verdadeiro nao for zero, e aceitar a saida so por causa do CRC e exactamente
  // o que o `despacho.cpp` ja faz no gzip do `.bar`.
  const std::uint32_t crc_calculado = Crc32DePng(saida->data(), saida->size());
  if (crc_calculado == crc_esperado && saida->size() == static_cast<std::size_t>(isize) &&
      saida->size() == static_cast<std::size_t>(esperado)) {
    return true;
  }
  char b[256];
  std::snprintf(b, sizeof(b), "o rodape do gzip nao confere: crc=0x%08x calculado=0x%08x isize=%u saida=%zu "
                              "declarado=%u%s%s",
                crc_esperado, crc_calculado, isize, saida->size(), esperado,
                ok_inflate ? "" : " | inflate: ", ok_inflate ? "" : motivo_inflate.c_str());
  Recusar(motivo, b);
  saida->clear();
  return false;
}

}  // namespace

bool Aez::Parse(std::vector<std::uint8_t> bytes, std::string* motivo) {
  if (motivo != nullptr) motivo->clear();
  bytes_ = std::move(bytes);
  entradas_.clear();
  nomes_repetidos_ = 0;
  registos_guardados_ = 0;
  valido_ = false;
  motivo_.clear();

  const std::size_t n = bytes_.size();
  // O menor ficheiro com um registo: comprimento (1) + 1 byte de nome + 8. Um
  // ficheiro VAZIO nao e um recipiente sem entradas -- nao ha registo nenhum que
  // o diga, e aceita-lo em silencio seria dizer que um ficheiro truncado esta
  // bom.
  if (n < aez_campos::kMinimoDoRegisto + 1u) {
    motivo_ = "ficheiro com " + std::to_string(n) + " bytes: o registo mais curto do AEZ ocupa " +
              std::to_string(aez_campos::kMinimoDoRegisto + 1u);
    Recusar(motivo, motivo_);
    return false;
  }

  std::size_t off = 0;
  while (off < n) {
    const std::size_t comprimento = bytes_[off];
    if (comprimento == 0) {
      motivo_ = "registo " + std::to_string(entradas_.size()) + " no offset " + std::to_string(off) +
                " com comprimento de caminho 0";
      Recusar(motivo, motivo_);
      return false;
    }
    if (off + 1u + comprimento + 8u > n) {
      motivo_ = "registo " + std::to_string(entradas_.size()) + " no offset " + std::to_string(off) +
                ": o caminho de " + std::to_string(comprimento) + " bytes e os dois campos de 4 " +
                "passam dos " + std::to_string(n) + " do ficheiro";
      Recusar(motivo, motivo_);
      return false;
    }
    std::string nome(reinterpret_cast<const char*>(&bytes_[off + 1]), comprimento);
    // Os 1 390 nomes medidos sao ASCII imprimivel (0x20..0x7e), com ESPACOS e
    // barras. Um byte de controle aqui e um registo lido no sitio errado; um
    // nome com lixo dentro nunca casaria com o que o guest pede, em silencio.
    for (char c : nome) {
      const unsigned char u = static_cast<unsigned char>(c);
      if (u < 0x20u || u > 0x7eu) {
        char b[128];
        std::snprintf(b, sizeof(b), "registo %zu com byte 0x%02x (nao imprimivel) no caminho",
                      entradas_.size(), u);
        motivo_ = b;
        Recusar(motivo, motivo_);
        return false;
      }
    }

    EntradaDoAez e;
    e.nome = std::move(nome);
    e.tamanho_descomprimido = Ler32(&bytes_[off + 1 + comprimento]);
    const std::uint32_t segundo = Ler32(&bytes_[off + 5 + comprimento]);
    e.guardado = (segundo == aez_campos::kGuardado);
    e.tamanho_no_ficheiro = e.guardado ? e.tamanho_descomprimido : segundo;
    e.offset = static_cast<std::uint32_t>(off + aez_campos::kMinimoDoRegisto + comprimento);
    if (e.tamanho_descomprimido == 0) {
      motivo_ = "registo " + std::to_string(entradas_.size()) + " (\"" + e.nome +
                "\"): tamanho descomprimido 0 (nenhum dos 1 390 registos medidos e vazio)";
      Recusar(motivo, motivo_);
      return false;
    }
    if (static_cast<std::uint64_t>(e.offset) + e.tamanho_no_ficheiro > n) {
      motivo_ = "registo " + std::to_string(entradas_.size()) + " (\"" + e.nome + "\"): payload de " +
                std::to_string(e.tamanho_no_ficheiro) + " bytes no offset " + std::to_string(e.offset) +
                " passa do fim do ficheiro (" + std::to_string(n) + ")";
      Recusar(motivo, motivo_);
      return false;
    }
    // A FORMA do payload, conferida no PARSER e nao so na extraccao: um registo
    // comprimido comeca no 1f 8b do RFC1952 (1 112 de 1 112 medidos) e um
    // GUARDADO nao pode comecar la (0 de 278 medidos). Uma troca dos dois campos
    // muda a forma do payload, e e essa a doenca que isto apanha antes de
    // descomprimir nada.
    if (!e.guardado) {
      if (e.tamanho_no_ficheiro < aez_campos::kMinimoDoGzip) {
        motivo_ = "registo " + std::to_string(entradas_.size()) + " (\"" + e.nome + "\"): " +
                  std::to_string(e.tamanho_no_ficheiro) + " bytes de payload comprimido, e o gzip tem " +
                  std::to_string(aez_campos::kMinimoDoGzip) + " so de cabecalho e rodape";
        Recusar(motivo, motivo_);
        return false;
      }
      if (bytes_[e.offset] != aez_campos::kGzipId1 || bytes_[e.offset + 1] != aez_campos::kGzipId2) {
        char b[160];
        std::snprintf(b, sizeof(b),
                      "registo %zu (\"%s\"): payload comprimido comeca em %02x %02x e nao no 1f 8b",
                      entradas_.size(), e.nome.c_str(), bytes_[e.offset], bytes_[e.offset + 1]);
        motivo_ = b;
        Recusar(motivo, motivo_);
        return false;
      }
    }
    off = static_cast<std::size_t>(e.offset) + e.tamanho_no_ficheiro;
    entradas_.push_back(std::move(e));
  }

  // A REPETICAO DE NOMES nao e erro -- e um facto do ficheiro, contado (3 dos
  // 1 390 medidos, em ficheiros diferentes). Quem resolve um nome escolhe o
  // primeiro; quem le isto fica a saber.
  std::vector<std::string> vistos;
  vistos.reserve(entradas_.size());
  for (const EntradaDoAez& e : entradas_) vistos.push_back(Minusculas(e.nome));
  std::sort(vistos.begin(), vistos.end());
  for (std::size_t i = 1; i < vistos.size(); ++i) {
    if (vistos[i] == vistos[i - 1]) ++nomes_repetidos_;
  }
  for (const EntradaDoAez& e : entradas_) {
    if (e.guardado) ++registos_guardados_;
  }

  valido_ = true;
  return true;
}

const EntradaDoAez* Aez::Procurar(const std::string& nome) const {
  const std::string alvo = Minusculas(nome);
  for (const EntradaDoAez& e : entradas_) {
    if (Minusculas(e.nome) == alvo) return &e;
  }
  return nullptr;
}

bool Aez::Extrair(const EntradaDoAez& entrada, std::vector<std::uint8_t>* saida,
                  std::string* motivo) const {
  if (motivo != nullptr) motivo->clear();
  if (saida == nullptr) {
    Recusar(motivo, "destino nulo");
    return false;
  }
  saida->clear();
  if (!valido_) {
    Recusar(motivo, "recipiente invalido: " + motivo_);
    return false;
  }
  const std::string prefixo = "entrada \"" + entrada.nome + "\": ";
  if (static_cast<std::uint64_t>(entrada.offset) + entrada.tamanho_no_ficheiro > bytes_.size()) {
    Recusar(motivo, prefixo + "os dados saem do ficheiro");
    return false;
  }
  if (entrada.guardado) {
    // GUARDADA: o payload esta em claro, e o tamanho dele E o declarado.
    saida->assign(bytes_.begin() + static_cast<std::ptrdiff_t>(entrada.offset),
                  bytes_.begin() + static_cast<std::ptrdiff_t>(entrada.offset) +
                      entrada.tamanho_no_ficheiro);
    if (saida->size() != entrada.tamanho_descomprimido) {
      Recusar(motivo, prefixo + "guardada com " + std::to_string(saida->size()) +
                          " bytes e o registo declara " + std::to_string(entrada.tamanho_descomprimido));
      saida->clear();
      return false;
    }
    return true;
  }
  if (!DescomprimirGzip(&bytes_[entrada.offset], entrada.tamanho_no_ficheiro,
                        entrada.tamanho_descomprimido, saida, motivo)) {
    Recusar(motivo, prefixo + (motivo != nullptr ? *motivo : std::string("gzip recusado")));
    saida->clear();
    return false;
  }
  return true;
}

bool Aez::Extrair(std::size_t indice, std::vector<std::uint8_t>* saida, std::string* motivo) const {
  if (indice >= entradas_.size()) {
    Recusar(motivo, "indice " + std::to_string(indice) + " fora das " + std::to_string(entradas_.size()) +
                        " entradas");
    return false;
  }
  return Extrair(entradas_[indice], saida, motivo);
}

bool Aez::ExtrairPorNome(const std::string& nome, std::vector<std::uint8_t>* saida,
                         std::string* motivo) const {
  const EntradaDoAez* e = Procurar(nome);
  if (e == nullptr) {
    Recusar(motivo, "sem entrada \"" + nome + "\"");
    return false;
  }
  return Extrair(*e, saida, motivo);
}

}  // namespace zb2
