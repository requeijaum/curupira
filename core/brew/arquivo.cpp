#include "core/brew/arquivo.h"

#include <fstream>
#include <iterator>

namespace zb2::brew {

namespace {
constexpr std::uint32_t kOfmLeitura = 0x0001u;
constexpr std::uint32_t kOfmEscrita = 0x000Eu;  // READWRITE | CREATE | APPEND
}  // namespace

bool Arquivos::ModoMudaOFicheiro(std::uint32_t modo) const {
  return (modo & kOfmEscrita) != 0;
}

std::uint32_t Arquivos::Abrir(const std::string& nome, std::uint32_t modo,
                              const std::string& pasta_do_titulo) {
  if (vfs_ == nullptr) return 0;
  if (ModoMudaOFicheiro(modo)) return 0;
  if ((modo & kOfmLeitura) == 0) return 0;
  const std::string caminho = vfs_->Normalizar(nome);
  if (caminho.empty()) return 0;

  std::ifstream f(pasta_do_titulo + "/" + caminho, std::ios::binary);
  if (!f) return 0;
  Aberto a;
  a.dados.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  if (a.dados.empty()) return 0;
  a.id = proximo_id_++;
  abertos_.push_back(a);
  return a.id;
}

Arquivos::Aberto* Arquivos::Procurar(std::uint32_t id) {
  for (auto& a : abertos_) {
    if (a.id == id) return &a;
  }
  return nullptr;
}

const Arquivos::Aberto* Arquivos::Procurar(std::uint32_t id) const {
  for (const auto& a : abertos_) {
    if (a.id == id) return &a;
  }
  return nullptr;
}

std::int32_t Arquivos::Ler(std::uint32_t id, Memoria& mem, Endereco pDestino,
                           std::uint32_t quer) {
  Aberto* a = Procurar(id);
  if (a == nullptr) return -1;
  const std::uint32_t resta = static_cast<std::uint32_t>(a->dados.size() - a->pos);
  const std::uint32_t n = quer < resta ? quer : resta;
  for (std::uint32_t i = 0; i < n; ++i) {
    mem.Escrever8(pDestino + i, a->dados[a->pos + i]);
  }
  a->pos += n;
  // Curto se chegar ao fim: e o contrato do `Read`. Devolver mais seria inventar.
  return static_cast<std::int32_t>(n);
}

std::int32_t Arquivos::Posicionar(std::uint32_t id, std::uint32_t tipo, std::int32_t posicao) {
  Aberto* a = Procurar(id);
  if (a == nullptr) return -1;
  const std::int64_t tamanho = static_cast<std::int64_t>(a->dados.size());
  std::int64_t novo = -1;
  if (tipo == 0) novo = posicao;                                              // SEEK_SET
  else if (tipo == 1) novo = static_cast<std::int64_t>(a->pos) + posicao;     // SEEK_CUR
  else if (tipo == 2) novo = tamanho + posicao;                               // SEEK_END
  if (novo < 0 || novo > tamanho) return -1;
  a->pos = static_cast<std::uint32_t>(novo);
  return static_cast<std::int32_t>(a->pos);
}

bool Arquivos::Informacao(std::uint32_t id, Memoria& mem, Endereco pInfo) const {
  const Aberto* a = Procurar(id);
  if (a == nullptr) return false;
  if (pInfo == 0) return true;
  // `FileInfo`: `char attrib; uint32 dwCreationDate; uint32 dwSize;
  //              char szName[AEE_MAX_FILE_NAME]`.
  mem.Escrever8(pInfo + 0, 0);  // AEE_FA_NORMAL
  mem.Escrever32(pInfo + 4, 0);
  mem.Escrever32(pInfo + 8, static_cast<std::uint32_t>(a->dados.size()));
  for (std::uint32_t i = 0; i < 64; ++i) mem.Escrever8(pInfo + 12 + i, 0);
  return true;
}

void Arquivos::Fechar(std::uint32_t id) {
  for (auto it = abertos_.begin(); it != abertos_.end(); ++it) {
    if (it->id == id) {
      abertos_.erase(it);
      return;
    }
  }
}

}  // namespace zb2::brew
