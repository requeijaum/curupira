#include "core/brew/arquivo.h"

#include <iterator>
#include <utility>

#include "core/traco/traco.h"  // Hex()

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
  // A PASTA DEIXOU DE SER LIDA AQUI, e o argumento fica na assinatura por uma
  // razao so: quem a sabe e a VFS, que foi registada com ela (`Vfs::Registar`).
  // Duas fontes para "onde esta o ficheiro" e a forma de ler duas pastas
  // diferentes conforme quem pergunta -- e o argumento continua a ser passado
  // pelas chamadas que ja existiam (`tests/brew_test.cpp`).
  (void)pasta_do_titulo;
  ultimo_motivo_.clear();
  ultimo_caminho_.clear();
  ultimo_de_pacote_ = false;
  if (vfs_ == nullptr) {
    ultimo_motivo_ = "sem VFS";
    return 0;
  }
  if (ModoMudaOFicheiro(modo)) {
    ultimo_motivo_ = "o modo 0x" + Hex(modo) + " escreve: a VFS e SO DE LEITURA, por decisao";
    return 0;
  }
  if ((modo & kOfmLeitura) == 0) {
    ultimo_motivo_ = "o modo 0x" + Hex(modo) + " nao pede leitura";
    return 0;
  }

  // A LEITURA PASSA TODA PELA VFS -- ficheiro solto na pasta do titulo, ou
  // entrada de um `.pkg` descomprimida. E o mesmo caminho para os dois casos, e
  // e por isso que o `Test` do `IFileMgr` e o `OpenFile` nao podem divergir
  // sobre o que existe.
  Aberto a;
  std::string porque;
  if (!vfs_->Ler(nome, &a.dados, &porque)) {
    ultimo_motivo_ = porque;
    return 0;
  }
  if (a.dados.empty()) {
    ultimo_motivo_ = "ficheiro vazio: " + vfs_->Normalizar(nome);
    return 0;
  }
  ultimo_caminho_ = vfs_->Normalizar(nome);
  ultimo_de_pacote_ = ultimo_caminho_.find('/') != std::string::npos;
  a.id = proximo_id_++;
  abertos_.push_back(std::move(a));
  return abertos_.back().id;
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
  // OS TRES VALORES SAO OS DO SDK, e nao os do `SEEK_SET/CUR/END` do `stdio`.
  //
  // `AEEFile.h:363-374` (SDK 4.0.2 SP19, o da consola) e explicito:
  //
  //     typedef enum { _SEEK_START, _SEEK_END, _SEEK_CURRENT } FileSeekType;
  //
  // ou seja `_SEEK_START = 0`, `_SEEK_END = 1`, `_SEEK_CURRENT = 2` (a lista de
  // valores esta tambem em `AEEFile.h:39-41`). A ordem do `stdio` do C e OUTRA
  // (`SET = 0`, `CUR = 1`, `END = 2`) e usar essa aqui troca DOIS dos tres.
  //
  // MEDIDO no gof (`ZB2_QUADROS=3`, sonda no `Ler`/`Posicionar`), e e o defeito
  // que apagava TODA a leitura de ficheiros do emulador: o jogo faz
  // `OpenFile` -> `Seek(2, 0)` -> `Read(8)`. O 2 e `_SEEK_CURRENT` ("onde
  // estou?", a posicao continua onde estava, 0); lido como o `END` do stdio, a
  // posicao saltava para o FIM do ficheiro (`pos=1051871 tam=1051871`) e o
  // `Read` devolvia 0 bytes -- o jogo imprimia "Failed to load" para o
  // `main_menu.aei` e "Data could not be read from file" para 32 `.wav`, com o
  // FICHEIRO TODA INTEIRO no `Arquivos` (o mesmo sintoma com o ficheiro solto na
  // pasta, e nao so dentro de um recipiente: medido).
  switch (tipo) {
    case 0: novo = posicao; break;                                      // _SEEK_START
    case 1: novo = tamanho + posicao; break;                            // _SEEK_END
    case 2: novo = static_cast<std::int64_t>(a->pos) + posicao; break;  // _SEEK_CURRENT
    default: return -1;
  }
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
