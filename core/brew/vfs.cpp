#include "core/brew/vfs.h"

#include <filesystem>

namespace zb2::brew {

void Vfs::Registar(const std::string& pasta) {
  nomes_.clear();
  std::error_code ec;
  for (const auto& entrada : std::filesystem::directory_iterator(pasta, ec)) {
    if (!entrada.is_regular_file(ec)) continue;
    nomes_.insert(entrada.path().filename().string());
  }
}

bool Vfs::Existe(const std::string& caminho) const {
  return !Normalizar(caminho).empty();
}

std::string Vfs::Normalizar(const std::string& bruto) const {
  std::string limpo = bruto;
  for (char& ch : limpo) {
    if (ch == '\\') ch = '/';
  }
  while (limpo.find("//") != std::string::npos) limpo.replace(limpo.find("//"), 2, "/");
  while (!limpo.empty() && limpo.front() == '/') limpo.erase(0, 1);
  while (limpo.rfind("./", 0) == 0) limpo.erase(0, 2);
  // `..` NAO sai da pasta do modulo: e retirado, e nao resolvido para o pai.
  // Um titulo que peca `../../x` fica com `x` dentro da sua propria pasta.
  while (limpo.rfind("../", 0) == 0) limpo.erase(0, 3);

  const std::size_t barra = limpo.rfind('/');
  if (barra == std::string::npos) {
    return nomes_.count(limpo) != 0 ? limpo : std::string();
  }
  // "pasta/ficheiro" para recursos soltos na pasta do titulo.
  if (nomes_.count(limpo) != 0) return limpo;
  const std::string so_nome = limpo.substr(barra + 1);
  return nomes_.count(so_nome) != 0 ? so_nome : std::string();
}

}  // namespace zb2::brew
