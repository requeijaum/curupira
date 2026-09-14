#ifndef ZB2_CORE_BREW_VFS_H
#define ZB2_CORE_BREW_VFS_H

// A VFS do modulo: a pasta irma do `.mod`, SO DE LEITURA.
//
// VIVE NO MOTOR. A VFS e uma decisao do emulador (nao escrever no modulo do jogo
// preserva a reprodutibilidade), e nao um pormenor do instrumento.

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace zb2::brew {

class Vfs {
 public:
  // Regista o conteudo de uma pasta. Guarda so os NOMES normalizados: e o que o
  // `Test`, o `OpenFile` e o `EnumInit` precisam de saber.
  void Registar(const std::string& pasta);

  bool Existe(const std::string& caminho) const;
  std::size_t Quantos() const { return nomes_.size(); }
  const std::set<std::string>& Nomes() const { return nomes_; }

  // A normalizacao das rotas do BREW, num so sitio: barras invertidas viram
  // normais, barras repetidas colapsam, e `..` NAO sai da pasta do modulo.
  //
  // Devolve vazio quando o nome nao corresponde a nada. UMA SO REGRA, para nao
  // haver duas verdades sobre que ficheiro existe -- foi por isso que o
  // `OpenFile` e o `Test` passaram a partilhar esta funcao.
  std::string Normalizar(const std::string& bruto) const;

 private:
  std::set<std::string> nomes_;
};

}  // namespace zb2::brew

#endif
