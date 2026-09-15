#include "core/carga/pack.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "core/carga/inflate.h"

namespace zb2 {
namespace {

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

bool CabecalhoZlib(const std::uint8_t* p) {
  // RFC1950 2.2: CM=8, CINFO<=7, e o resto do cabecalho faz (CMF<<8|FLG)
  // multiplo de 31. Um `78 da` (o que os 127 medidos trazem) passa; um `1f 8b`
  // (gzip) NAO passa -- e e exatamente isso que se quer apanhar aqui.
  if ((p[0] & 0x0fu) != 8u) return false;
  if ((p[0] >> 4) > 7u) return false;
  if (((static_cast<unsigned>(p[0]) << 8) | p[1]) % 31u != 0u) return false;
  return true;
}

}  // namespace

bool Pacote::Parse(std::vector<std::uint8_t> bytes, std::string* motivo) {
  if (motivo != nullptr) motivo->clear();
  bytes_ = std::move(bytes);
  entradas_.clear();
  nomes_repetidos_ = 0;
  constante_divergente_ = 0;
  valido_ = false;
  motivo_.clear();

  const std::size_t n = bytes_.size();
  if (n < pack_campos::kCabecalho) {
    motivo_ = "ficheiro com " + std::to_string(n) + " bytes: o cabecalho do PACK ocupa " +
              std::to_string(pack_campos::kCabecalho);
    Recusar(motivo, motivo_);
    return false;
  }
  if (std::memcmp(bytes_.data(), pack_campos::kAssinatura, 4) != 0) {
    char b[96];
    std::snprintf(b, sizeof(b), "assinatura \"%c%c%c%c\" em vez de \"PACK\"", bytes_[0], bytes_[1],
                  bytes_[2], bytes_[3]);
    motivo_ = b;
    Recusar(motivo, motivo_);
    return false;
  }

  const std::uint32_t count = Ler32(&bytes_[4]);
  const std::uint32_t offset_da_tabela = Ler32(&bytes_[8]);
  if (count == 0) {
    motivo_ = "PACK com 0 entradas";
    Recusar(motivo, motivo_);
    return false;
  }
  const std::uint64_t fim_dos_registos =
      static_cast<std::uint64_t>(pack_campos::kCabecalho) + static_cast<std::uint64_t>(count) * pack_campos::kRegisto;
  if (fim_dos_registos > n) {
    motivo_ = "PACK declara " + std::to_string(count) + " entradas de " +
              std::to_string(pack_campos::kRegisto) + " bytes, que acabam em " +
              std::to_string(fim_dos_registos) + " e o ficheiro tem " + std::to_string(n);
    Recusar(motivo, motivo_);
    return false;
  }
  if (offset_da_tabela < fim_dos_registos || offset_da_tabela >= n) {
    motivo_ = "tabela de nomes no offset " + std::to_string(offset_da_tabela) + ": tem de estar entre " +
              std::to_string(fim_dos_registos) + " (fim dos registos) e " + std::to_string(n);
    Recusar(motivo, motivo_);
    return false;
  }

  // --- os registos ---
  entradas_.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    const std::uint8_t* r = &bytes_[pack_campos::kCabecalho + static_cast<std::size_t>(i) * pack_campos::kRegisto];
    const std::uint32_t constante = Ler32(r + 0);
    EntradaDoPack e;
    e.hash = Ler32(r + 4);
    e.tamanho_comprimido = Ler32(r + 8);
    e.tamanho_descomprimido = Ler32(r + 12);
    e.offset = Ler32(r + 16);
    if (constante != pack_campos::kConstante) ++constante_divergente_;
    if (static_cast<std::uint64_t>(e.offset) + e.tamanho_comprimido > n) {
      motivo_ = "entrada " + std::to_string(i) + ": dados de " + std::to_string(e.tamanho_comprimido) +
                " bytes no offset " + std::to_string(e.offset) + " passam do fim do ficheiro (" +
                std::to_string(n) + ")";
      Recusar(motivo, motivo_);
      return false;
    }
    if (e.tamanho_comprimido < 2) {
      motivo_ = "entrada " + std::to_string(i) + ": " + std::to_string(e.tamanho_comprimido) +
                " bytes de dados, e um stream zlib tem no minimo um cabecalho de 2";
      Recusar(motivo, motivo_);
      return false;
    }
    if (!CabecalhoZlib(&bytes_[e.offset])) {
      char b[160];
      std::snprintf(b, sizeof(b),
                    "entrada %u (%s): os dados comecam em %02x %02x, que nao e um cabecalho zlib (CM=8, "
                    "CINFO<=7, (CMF<<8|FLG)%%31==0) -- gzip comeca em 1f 8b e nao serve",
                    i, e.nome.c_str(), bytes_[e.offset], bytes_[e.offset + 1]);
      motivo_ = b;
      Recusar(motivo, motivo_);
      return false;
    }
    entradas_.push_back(std::move(e));
  }

  // --- a tabela de nomes: UM stream zlib com `count` nomes de 256 bytes ---
  const std::uint32_t bytes_da_tabela = static_cast<std::uint32_t>(n - offset_da_tabela);
  std::vector<std::uint8_t> nomes;
  std::string porque;
  std::size_t consumido = 0;
  if (!Inflar(&bytes_[offset_da_tabela], bytes_da_tabela, &nomes, &porque,
              count * pack_campos::kNome, &consumido)) {
    motivo_ = "tabela de nomes no offset " + std::to_string(offset_da_tabela) + ": " + porque;
    Recusar(motivo, motivo_);
    return false;
  }
  if (consumido != bytes_da_tabela) {
    motivo_ = "tabela de nomes: o stream zlib ocupa " + std::to_string(consumido) + " bytes e vao " +
              std::to_string(bytes_da_tabela) + " ate ao fim do ficheiro";
    Recusar(motivo, motivo_);
    return false;
  }
  if (nomes.size() != static_cast<std::size_t>(count) * pack_campos::kNome) {
    motivo_ = "tabela de nomes descomprimiu para " + std::to_string(nomes.size()) + " bytes e " +
              std::to_string(count) + " entradas x " + std::to_string(pack_campos::kNome) + " bytes sao " +
              std::to_string(static_cast<std::size_t>(count) * pack_campos::kNome);
    Recusar(motivo, motivo_);
    return false;
  }

  for (std::uint32_t i = 0; i < count; ++i) {
    const std::uint8_t* p = nomes.data() + static_cast<std::size_t>(i) * pack_campos::kNome;
    std::size_t comprimento = 0;
    while (comprimento < pack_campos::kNome && p[comprimento] != 0) ++comprimento;
    if (comprimento == pack_campos::kNome) {
      motivo_ = "nome da entrada " + std::to_string(i) + " sem NUL nos " +
                std::to_string(pack_campos::kNome) + " bytes";
      Recusar(motivo, motivo_);
      return false;
    }
    if (comprimento == 0) {
      motivo_ = "nome da entrada " + std::to_string(i) + " vazio";
      Recusar(motivo, motivo_);
      return false;
    }
    std::string nome(reinterpret_cast<const char*>(p), comprimento);
    // Os nomes medidos sao todos ASCII imprimivel. Um byte de controle aqui e um
    // sinal de que a tabela foi lida no sitio errado -- e um nome com lixo dentro
    // nunca casaria com o que o guest pede, em silencio.
    for (char c : nome) {
      if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7e) {
        char b[128];
        std::snprintf(b, sizeof(b), "nome da entrada %u com byte 0x%02x (nao imprimivel)",
                      i, static_cast<unsigned char>(c));
        motivo_ = b;
        Recusar(motivo, motivo_);
        return false;
      }
    }
    entradas_[i].nome = std::move(nome);
  }

  // A REPETICAO DE NOMES nao e erro -- e um facto do ficheiro, contado. Quem
  // resolve um nome (a VFS) escolhe o primeiro; quem le isto fica a saber que o
  // ficheiro tinha dois.
  std::vector<std::string> vistos;
  vistos.reserve(entradas_.size());
  for (const EntradaDoPack& e : entradas_) vistos.push_back(Minusculas(e.nome));
  std::sort(vistos.begin(), vistos.end());
  for (std::size_t i = 1; i < vistos.size(); ++i) {
    if (vistos[i] == vistos[i - 1]) ++nomes_repetidos_;
  }

  valido_ = true;
  return true;
}

const EntradaDoPack* Pacote::Procurar(const std::string& nome) const {
  const std::string alvo = Minusculas(nome);
  for (const EntradaDoPack& e : entradas_) {
    if (Minusculas(e.nome) == alvo) return &e;
  }
  return nullptr;
}

bool Pacote::Extrair(const EntradaDoPack& entrada, std::vector<std::uint8_t>* saida,
                     std::string* motivo) const {
  if (motivo != nullptr) motivo->clear();
  if (!valido_) {
    Recusar(motivo, "pacote invalido: " + motivo_);
    return false;
  }
  const std::string prefixo = "entrada \"" + entrada.nome + "\": ";
  if (static_cast<std::uint64_t>(entrada.offset) + entrada.tamanho_comprimido > bytes_.size()) {
    Recusar(motivo, prefixo + "os dados saem do ficheiro");
    return false;
  }
  std::string porque;
  std::size_t consumido = 0;
  if (!Inflar(&bytes_[entrada.offset], entrada.tamanho_comprimido, saida, &porque,
              entrada.tamanho_descomprimido, &consumido)) {
    Recusar(motivo, prefixo + porque);
    return false;
  }
  if (consumido != entrada.tamanho_comprimido) {
    Recusar(motivo, prefixo + "o stream zlib ocupa " + std::to_string(consumido) + " bytes e o registo declara " +
                        std::to_string(entrada.tamanho_comprimido));
    saida->clear();
    return false;
  }
  if (saida->size() != entrada.tamanho_descomprimido) {
    Recusar(motivo, prefixo + "descomprimiu para " + std::to_string(saida->size()) +
                        " bytes e o registo declara " + std::to_string(entrada.tamanho_descomprimido));
    saida->clear();
    return false;
  }
  return true;
}

bool Pacote::Extrair(std::size_t indice, std::vector<std::uint8_t>* saida, std::string* motivo) const {
  if (indice >= entradas_.size()) {
    Recusar(motivo, "entrada " + std::to_string(indice) + " nao existe: o pacote tem " +
                        std::to_string(entradas_.size()));
    return false;
  }
  return Extrair(entradas_[indice], saida, motivo);
}

bool Pacote::ExtrairPorNome(const std::string& nome, std::vector<std::uint8_t>* saida,
                            std::string* motivo) const {
  const EntradaDoPack* e = Procurar(nome);
  if (e == nullptr) {
    Recusar(motivo, "o pacote nao tem entrada chamada \"" + nome + "\"");
    return false;
  }
  return Extrair(*e, saida, motivo);
}

}  // namespace zb2
