#ifndef ZB2_CORE_BREW_ARQUIVO_H
#define ZB2_CORE_BREW_ARQUIVO_H

// Os ficheiros abertos pelo jogo, sobre a VFS.
//
// VIVE NO MOTOR. O `IFileMgr::OpenFile` e o `IFile::Read/Seek/GetInfo` sao
// comportamento do sistema emulado -- e o jogo passa por aqui para ler os
// assets. Enquanto isto esteve dentro da ferramenta, nenhum teste lhe chegava.

#include <cstdint>
#include <string>
#include <vector>

#include "core/brew/vfs.h"
#include "core/memoria/memoria.h"

namespace zb2::brew {

class Arquivos {
 public:
  explicit Arquivos(const Vfs* vfs) : vfs_(vfs) {}

  // Abre SO PARA LEITURA. `modo` e o `OpenFileMode` do BREW: `_OFM_READ` = 1,
  // `_OFM_READWRITE` = 2, `_OFM_CREATE` = 4, `_OFM_APPEND` = 8.
  //
  // Os tres ultimos MUDAM o ficheiro e sao RECUSADOS: a VFS e so de leitura por
  // DECISAO (um jogo que escreva no modulo destroi a reprodutibilidade), nao por
  // falta. Recusar em voz alta e o principio P2; fingir um sucesso seria pior.
  //
  // Devolve o identificador do ficheiro aberto, ou 0.
  std::uint32_t Abrir(const std::string& nome, std::uint32_t modo, const std::string& pasta_do_titulo);

  // `longo` sai a true quando o modo pedido muda o ficheiro -- para o despacho
  // poder dizer PORQUE recusou, e nao so que recusou.
  bool ModoMudaOFicheiro(std::uint32_t modo) const;

  std::int32_t Ler(std::uint32_t id, Memoria& mem, Endereco pDestino, std::uint32_t quer);
  std::int32_t Posicionar(std::uint32_t id, std::uint32_t tipo, std::int32_t posicao);
  bool Informacao(std::uint32_t id, Memoria& mem, Endereco pInfo) const;
  void Fechar(std::uint32_t id);

  std::size_t Abertos() const { return abertos_.size(); }

  // O que aconteceu na ultima chamada a `Abrir`, para o despacho poder DIZER
  // porque e que o `OpenFile` recusou -- em vez de zero e nada.
  //
  // Existe porque os pedidos de ficheiro do guest eram INVISIVEIS: o `OpenFile`
  // nao emite nada, e um titulo que pede um ficheiro que a VFS nao serve ficava a
  // pedir em silencio. `UltimoCaminho` e o nome canonico (o caminho por que a
  // entrada do pacote foi encontrada, quando foi).
  const std::string& UltimoMotivo() const { return ultimo_motivo_; }
  const std::string& UltimoCaminho() const { return ultimo_caminho_; }
  bool UltimoVeioDePacote() const { return ultimo_de_pacote_; }

 private:
  struct Aberto {
    std::uint32_t id = 0;
    std::vector<std::uint8_t> dados;
    std::uint32_t pos = 0;
  };
  Aberto* Procurar(std::uint32_t id);
  const Aberto* Procurar(std::uint32_t id) const;

  const Vfs* vfs_;
  std::vector<Aberto> abertos_;
  std::uint32_t proximo_id_ = 1;
  std::string ultimo_motivo_;
  std::string ultimo_caminho_;
  bool ultimo_de_pacote_ = false;
};

}  // namespace zb2::brew

#endif
