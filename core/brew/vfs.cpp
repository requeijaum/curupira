#include "core/brew/vfs.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "core/traco/traco.h"

namespace zb2::brew {
namespace {

std::string Minusculas(const std::string& s) {
  std::string r = s;
  for (char& c : r) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return r;
}

bool TerminaComIgnorandoCaixa(const std::string& texto, const std::string& sufixo) {
  if (texto.size() < sufixo.size()) return false;
  return Minusculas(texto.substr(texto.size() - sufixo.size())) == sufixo;
}

// O nome sem a extensao (o "stem"): `karnovr.mod` -> `karnovr`.
std::string SemExtensao(const std::string& nome) {
  const std::size_t ponto = nome.rfind('.');
  if (ponto == std::string::npos || ponto == 0) return nome;
  return nome.substr(0, ponto);
}

std::string Chave(const std::string& dir, const std::string& nome) {
  return Minusculas(dir) + "/" + Minusculas(nome);
}

std::string Dez(std::size_t v) { return std::to_string(v); }

// A lista de directorios, numa linha, por ordem (o `std::set` ja ordena).
std::string Lista(const std::set<std::string>& v) {
  std::string s;
  for (const std::string& x : v) {
    if (!s.empty()) s += ", ";
    s += x;
  }
  return s;
}

}  // namespace

void Vfs::Registar(const std::string& pasta) {
  pasta_ = pasta;
  nomes_.clear();
  nomes_por_caixa_.clear();
  pacotes_.clear();
  conteudo_.clear();
  conteudo_pakz_.clear();
  conteudo_aez_.clear();
  alias_.clear();
  nomes_de_entrada_.clear();
  diretorios_.clear();

  // A LISTA E ORDENADA ANTES DE SER USADA: a ordem de uma `directory_iterator`
  // e a ordem do sistema de ficheiros, e o P4 (determinismo por construcao) nao
  // admite duas corridas com indices de pacotes trocados.
  std::vector<std::string> ficheiros;
  std::error_code ec;
  for (const auto& entrada : std::filesystem::directory_iterator(pasta, ec)) {
    if (!entrada.is_regular_file(ec)) continue;
    const std::string nome = entrada.path().filename().string();
    nomes_.insert(nome);
    // O mapa da caixa ignorada: o PRIMEIRO na ordem do `std::set` fica (a
    // insercao de uma chave que ja existe nao faz nada).
    nomes_por_caixa_.insert({Minusculas(nome), nome});
    ficheiros.push_back(nome);
  }
  std::sort(ficheiros.begin(), ficheiros.end());

  // O DIRECTORIO DA UNIAO E O STEM DO `.mod` -- e nao o nome da pasta: e o
  // mesmo numero (a pasta `279126` tem o `karnovr.mod`), mas quem manda no nome
  // que o guest pede e o modulo. Uma pasta com dois `.mod` nao decide nada: fica
  // so o directorio de cada pacote.
  std::size_t quantos_mod = 0;
  for (const std::string& f : ficheiros) {
    if (TerminaComIgnorandoCaixa(f, ".mod")) {
      diretorios_.insert(SemExtensao(f));
      ++quantos_mod;
    }
  }
  if (quantos_mod > 1) {
    // Nao se escolhe um: retiram-se todos. Um diretorio escolhido a sorte seria
    // uma ligacao que so se ve no dia em que falha.
    for (const std::string& f : ficheiros) {
      if (TerminaComIgnorandoCaixa(f, ".mod")) diretorios_.erase(SemExtensao(f));
    }
  }

  for (const std::string& f : ficheiros) {
    if (!TerminaComIgnorandoCaixa(f, ".pkg")) continue;
    PacoteRegistado registo;
    registo.ficheiro = f;

    std::ifstream entrada(pasta + "/" + f, std::ios::binary);
    if (!entrada) {
      registo.motivo = "nao consegui abrir o ficheiro";
      pacotes_.push_back(registo);
      continue;
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(entrada)),
                                    std::istreambuf_iterator<char>());
    Pacote pacote;
    std::string porque;
    if (!pacote.Parse(std::move(bytes), &porque)) {
      registo.motivo = porque;
      pacotes_.push_back(registo);
      continue;
    }

    // O directorio de cada pacote entra (o stem do proprio ficheiro), e e o que
    // faz `boot/boot.rom` resolver sem se saber nada do `.mod`.
    diretorios_.insert(SemExtensao(f));

    registo.entradas = pacote.NumeroDeEntradas();
    registo.indice_no_conteudo = conteudo_.size();
    pacotes_.push_back(registo);
    conteudo_.push_back(std::move(pacote));
  }

  // O MESMO PASSO para os `.pakz`. O recipiente e outro (`Pakz`, LZMA_ALONE) e
  // o indice vai para o vector proprio -- os dois indices de `PacoteRegistado`
  // existem porque nao sao o mesmo numero.
  for (const std::string& f : ficheiros) {
    if (!TerminaComIgnorandoCaixa(f, ".pakz")) continue;
    PacoteRegistado registo;
    registo.ficheiro = f;
    registo.pakz = true;

    std::ifstream entrada(pasta + "/" + f, std::ios::binary);
    if (!entrada) {
      registo.motivo = "nao consegui abrir o ficheiro";
      pacotes_.push_back(registo);
      continue;
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(entrada)),
                                    std::istreambuf_iterator<char>());
    Pakz recipiente;
    std::string porque;
    if (!recipiente.Parse(std::move(bytes), &porque)) {
      registo.motivo = porque;
      pacotes_.push_back(registo);
      continue;
    }

    diretorios_.insert(SemExtensao(f));
    registo.entradas = recipiente.NumeroDeEntradas();
    registo.indice_no_conteudo_pakz = conteudo_pakz_.size();
    pacotes_.push_back(registo);
    conteudo_pakz_.push_back(std::move(recipiente));
  }

  // O MESMO PASSO para os `.aez`. O recipiente e o terceiro (`Aez`, registos
  // com caminho absoluto e payload gzip ou cru) e o indice vai para o vector
  // proprio, pela mesma razao que os dois anteriores: os indices nao sao o
  // mesmo numero.
  for (const std::string& f : ficheiros) {
    if (!TerminaComIgnorandoCaixa(f, ".aez")) continue;
    PacoteRegistado registo;
    registo.ficheiro = f;
    registo.aez = true;

    std::ifstream entrada(pasta + "/" + f, std::ios::binary);
    if (!entrada) {
      registo.motivo = "nao consegui abrir o ficheiro";
      pacotes_.push_back(registo);
      continue;
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(entrada)),
                                    std::istreambuf_iterator<char>());
    Aez recipiente;
    std::string porque;
    if (!recipiente.Parse(std::move(bytes), &porque)) {
      registo.motivo = porque;
      pacotes_.push_back(registo);
      continue;
    }

    // O directorio do proprio ficheiro entra (o stem, como nos outros dois): e
    // o que faz `res/res.aez` e `tracks/tracks.aez` existirem como nomes. As
    // ENTRADAS nao se servem debaixo dele -- o caminho delas ja e absoluto e e
    // esse que o guest pede (ver `core/brew/vfs.h`).
    diretorios_.insert(SemExtensao(f));
    registo.entradas = recipiente.NumeroDeEntradas();
    registo.indice_no_conteudo_aez = conteudo_aez_.size();
    pacotes_.push_back(registo);
    conteudo_aez_.push_back(std::move(recipiente));
  }

  // A UNIAO SO DEPOIS DE TODOS OS PACOTES LIDOS -- e nao durante a leitura de
  // cada um.
  //
  // A razao e a REGRA: todos os directorios servidos (o stem do `.mod` E o stem
  // de cada pacote) servem a uniao INTEIRA. Inserir a uniao enquanto se le o
  // pacote `i` deixaria os pacotes lidos ANTES dele sem acesso a estas entradas
  // atraves do directorio do pacote `i` -- e a ordem de leitura e alfabetica,
  // logo o defeito dependeria do nome dos ficheiros, que e a pior especie de
  // defeito: intermitente e invisivel.
  //
  // E OS DOIS INDICES NAO SAO O MESMO NUMERO: `pacotes_` inclui os pacotes
  // recusados e `conteudo_` nao. Aqui percorrem-se os VALIDOS, com o indice de
  // `conteudo_` no campo proprio.
  // Os TRES recipientes nao tem o mesmo tipo de entrada, logo o numero de
  // entradas e o nome da entrada `i` saem de duas funcoes com o `if` num so
  // sitio -- e nao de ternarios espalhados, que foi como a primeira versao
  // deste passo ficou a ler o `conteudo_` errado.
  const auto quantas_entradas = [this](std::size_t p) -> std::size_t {
    const PacoteRegistado& r = pacotes_[p];
    if (r.aez) return conteudo_aez_[r.indice_no_conteudo_aez].NumeroDeEntradas();
    if (r.pakz) return conteudo_pakz_[r.indice_no_conteudo_pakz].NumeroDeEntradas();
    return conteudo_[r.indice_no_conteudo].NumeroDeEntradas();
  };
  const auto nome_da_entrada = [this](std::size_t p, std::size_t i) -> const std::string& {
    const PacoteRegistado& r = pacotes_[p];
    if (r.aez) return conteudo_aez_[r.indice_no_conteudo_aez].ListaDeEntradas()[i].nome;
    if (r.pakz) return conteudo_pakz_[r.indice_no_conteudo_pakz].ListaDeEntradas()[i].nome;
    return conteudo_[r.indice_no_conteudo].ListaDeEntradas()[i].nome;
  };

  for (std::size_t p = 0; p < pacotes_.size(); ++p) {
    if (!pacotes_[p].motivo.empty()) continue;
    const std::size_t n = quantas_entradas(p);
    for (std::size_t i = 0; i < n; ++i) {
      const std::string nome = nome_da_entrada(p, i);
      nomes_de_entrada_.insert(nome);
      if (pacotes_[p].aez) {
        // O CAMINHO DO PROPRIO REGISTO e o que o guest pede, e ele traz a barra
        // inicial (`/data/tracks/t1.w3t`). A normalizacao do BREW tira essa
        // barra antes de casar (`Vfs::Normalizar`), logo a chave e o caminho
        // SEM ela. Um nome sem barra nenhuma (`pt.lang`) entra CRU, e nao
        // debaixo de um directorio: e assim que o `gof` o pede.
        const std::string sem_barra =
            !nome.empty() && nome.front() == '/' ? nome.substr(1) : nome;
        const std::string chave = Minusculas(sem_barra);
        if (!chave.empty() && alias_.count(chave) == 0) {
          alias_[chave] = {p, 0, 0, pacotes_[p].indice_no_conteudo_aez, false, true, i};
        }
        continue;
      }
      for (const std::string& dir : diretorios_) {
        const std::string chave = Chave(dir, nome);
        // O PRIMEIRO GANHA, e a ordem e a dos nomes dos ficheiros: um nome que
        // apareca em dois pacotes da a mesma resposta em todas as corridas.
        if (alias_.count(chave) == 0) {
          alias_[chave] = {p, pacotes_[p].pakz ? 0 : pacotes_[p].indice_no_conteudo,
                           pacotes_[p].indice_no_conteudo_pakz, 0, pacotes_[p].pakz, false, i};
        }
      }
      // O CAMINHO DA ENTRADA DO `.pakz` E O CAMINHO que o motor TTD pede:
      // `ani/touxiang1`, `xui/textures/ball.atitc`. A entrada entra tambem na
      // uniao pelo nome dela INTEIRO (e nao so debaixo de um directorio
      // servido) -- sem isto, um pedido do guest `ani/touxiang1` nao resolveria,
      // porque `ani` nao e stem de nenhum mod nem de nenhum pacote. Um nome
      // solto da tabela (`z1.lua`, sem barra) nao entra aqui: ficou so com o
      // directorio servido a frente, como uma entrada do `.pkg`.
      if (pacotes_[p].pakz && nome.find('/') != std::string::npos) {
        const std::string chave = Minusculas(nome);
        if (alias_.count(chave) == 0) {
          alias_[chave] = {p, 0, pacotes_[p].indice_no_conteudo_pakz, 0, true, false, i};
        }
      }
    }
  }
}

std::string Vfs::CaminhoDePacote(const std::string& limpo) const {
  if (alias_.empty()) return {};
  std::vector<std::string> candidatos;
  candidatos.push_back(limpo);
  // Os DOIS prefixos que o guest usa. Sao retirados, e o casamento fica pelo
  // sufixo `<dir>/<nome>` -- uma regra so, e nao tres tabelas.
  if (limpo.rfind("roms/neogeo/", 0) == 0) candidatos.push_back(limpo.substr(12));
  if (limpo.rfind("roms/", 0) == 0) candidatos.push_back(limpo.substr(5));

  // As entradas do `.aez` estao na uniao pelo CAMINHO DO PROPRIO REGISTO (sem a
  // barra inicial), e nao por `<dir>/<nome>` -- ver `core/brew/vfs.h`.
  //
  // A UNIAO E UMA LISTA DE CAMINHOS COMPLETOS: cada directorio servido x cada
  // entrada (e as entradas do `.pakz` trazem o caminho inteiro delas, que pode
  // ter mais do que uma barra -- 4 470 das 7 487 medidas tem duas, como
  // `xui/textures/ball.atitc`). O casamento e por MEMBRESIA EXACTA no indice:
  // quem nao esta na uniao nao existe, e o teste de "nao existe" prova-o. Os
  // prefixos `roms/` e `roms/neogeo/` sao retirados antes do casamento.
  for (const std::string& c : candidatos) {
    if (alias_.count(Minusculas(c)) != 0) return c;
  }
  return {};
}

std::string Vfs::NomeReal(const std::string& limpo) const {
  if (nomes_.count(limpo) != 0) return limpo;  // a caixa EXATA ganha sempre
  const auto it = nomes_por_caixa_.find(Minusculas(limpo));
  return it == nomes_por_caixa_.end() ? std::string() : it->second;
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
    const std::string solto = NomeReal(limpo);
    if (!solto.empty()) return solto;
    // UM NOME SEM BARRA NENHUMA (`pt.lang`, `gb.lang`): o ficheiro SOLTO manda
    // (a linha acima), e so depois a UNIAO -- que so tem nomes crus quando um
    // `.aez` os traz (`/pt.lang` e um registo do `res.aez` do gof, e e assim,
    // cru, que o jogo o pede). Nos `.pkg`/`.pakz` a uniao e `<dir>/<nome>` e
    // isto continua a nao resolver nada: e o que o `Pakz.VfsServeOAlicePakz`
    // prova do outro lado (`z1.lua` so existe como `alice/z1.lua`).
    return CaminhoDePacote(limpo);
  }
  // "pasta/ficheiro" para recursos soltos na pasta do titulo. O CAMINHO
  // INTEIRO nao existe no disco (a pasta do titulo e plana), logo o casamento
  // util e pelo nome final -- e com a caixa ignorada, como a consola.
  const std::string inteiro = NomeReal(limpo);
  if (!inteiro.empty()) return inteiro;
  const std::string so_nome = NomeReal(limpo.substr(barra + 1));
  if (!so_nome.empty()) return so_nome;
  // E, por fim, a UNIAO DOS PACOTES: `<dir>/<nome>`, com os prefixos `roms\` e
  // `roms\neogeo\` retirados.
  return CaminhoDePacote(limpo);
}

bool Vfs::Existe(const std::string& caminho) const { return !Normalizar(caminho).empty(); }

bool Vfs::Ler(const std::string& caminho, std::vector<std::uint8_t>* bytes, std::string* motivo) const {
  if (motivo != nullptr) motivo->clear();
  if (bytes == nullptr) {
    if (motivo != nullptr) *motivo = "destino nulo";
    return false;
  }
  bytes->clear();
  const std::string canonico = Normalizar(caminho);
  if (canonico.empty()) {
    if (motivo != nullptr) *motivo = "nao existe na VFS: " + caminho;
    return false;
  }
  const std::size_t barra = canonico.rfind('/');
  if (barra != std::string::npos) {
    const auto it = alias_.find(Chave(canonico.substr(0, barra), canonico.substr(barra + 1)));
    if (it != alias_.end()) {
      if (it->second.aez) {
        return conteudo_aez_[it->second.conteudo_aez].Extrair(it->second.entrada, bytes, motivo);
      }
      if (it->second.pakz) {
        return conteudo_pakz_[it->second.conteudo_pakz].Extrair(it->second.entrada, bytes, motivo);
      }
      return conteudo_[it->second.conteudo].Extrair(it->second.entrada, bytes, motivo);
    }
  } else if (nomes_.count(canonico) == 0) {
    // UM NOME SEM BARRA NENHUMA (`pt.lang`, `gb.lang`) nao chega aqui pelo
    // `Chave(dir, nome)` -- nao ha directorio. So o `.aez` serve entradas
    // assim, e o guest pede-as CRUAS (`/pt.lang`, medido no gof). O ficheiro
    // SOLTO tem prioridade: e a mesma ordem que o `Normalizar` ja segue, e
    // quem esta no disco da pasta nao passa a vir de dentro de um recipiente
    // por causa desta linha.
    const auto it = alias_.find(Minusculas(canonico));
    if (it != alias_.end()) {
      if (it->second.aez) {
        return conteudo_aez_[it->second.conteudo_aez].Extrair(it->second.entrada, bytes, motivo);
      }
      if (it->second.pakz) {
        return conteudo_pakz_[it->second.conteudo_pakz].Extrair(it->second.entrada, bytes, motivo);
      }
      return conteudo_[it->second.conteudo].Extrair(it->second.entrada, bytes, motivo);
    }
  }
  std::ifstream f(pasta_ + "/" + canonico, std::ios::binary);
  if (!f) {
    if (motivo != nullptr) *motivo = "nao consegui abrir " + pasta_ + "/" + canonico;
    return false;
  }
  bytes->assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  return true;
}

bool Vfs::OrigemDe(const std::string& caminho, std::size_t* pacote, std::size_t* entrada) const {
  const std::string canonico = Normalizar(caminho);
  const std::size_t barra = canonico.rfind('/');
  // O nome CRU (sem barra) e o caso do `.aez` -- ver `Ler`.
  const auto it = barra == std::string::npos
                      ? alias_.find(Minusculas(canonico))
                      : alias_.find(Chave(canonico.substr(0, barra), canonico.substr(barra + 1)));
  if (it == alias_.end()) return false;
  if (pacote != nullptr) *pacote = it->second.pacote;
  if (entrada != nullptr) *entrada = it->second.entrada;
  return true;
}

void Vfs::DeclararNoTraco(Traco* traco) {
  if (traco == nullptr || declarado_ || pasta_.empty()) return;
  declarado_ = true;

  std::size_t entradas = 0;
  for (const PacoteRegistado& p : pacotes_) entradas += p.entradas;
  traco->Emitir(Area::Brew, Nivel::Informacao, "VFS_REGISTADA",
                "pasta=" + pasta_ + " soltos=" + Dez(nomes_.size()) + " pacotes=" + Dez(pacotes_.size()) +
                    " entradas=" + Dez(entradas) + " caminhos=" + Dez(alias_.size()));
  for (const PacoteRegistado& p : pacotes_) {
    if (p.motivo.empty()) {
      const char* tipo = p.aez ? " (aez/gzip)" : (p.pakz ? " (pakz/LZMA)" : " (pkg/zlib)");
      traco->Emitir(Area::Brew, Nivel::Informacao, "VFS_PACOTE",
                    p.ficheiro + ": " + Dez(p.entradas) + " entradas no indice" + tipo);
    } else {
      // Um pacote que NAO entrou fica dito, e nao em silencio: sem isto, "o
      // jogo nao encontra o ficheiro" e "o pacote foi recusado" davam o mesmo
      // sintoma -- nada.
      traco->Emitir(Area::Brew, Nivel::Erro, "VFS_PACOTE_RECUSADO", p.ficheiro + ": " + p.motivo);
    }
  }
  traco->Emitir(Area::Brew, Nivel::Informacao, "VFS_ALIAS",
                "<dir>/<nome> servido da UNIAO dos pacotes; dirs aceites: " + Lista(diretorios_) +
                    "; entradas do pakz servem tambem pelo caminho inteiro delas; entradas do aez "
                    "servem pelo caminho do REGISTO sem a barra inicial (e o nome cru quando nao tem "
                    "barra nenhuma); prefixos aceitos antes do casamento: roms/, roms/neogeo/; nome "
                    "sem distinguir maiusculas, nas entradas E nos ficheiros soltos (a caixa exata "
                    "ganha; a ignorada e a segunda tentativa)");
}

}  // namespace zb2::brew
