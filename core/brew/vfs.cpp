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
  pacotes_.clear();
  conteudo_.clear();
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
  for (std::size_t p = 0; p < pacotes_.size(); ++p) {
    if (!pacotes_[p].motivo.empty()) continue;
    const std::size_t conteudo = pacotes_[p].indice_no_conteudo;
    for (std::size_t i = 0; i < conteudo_[conteudo].NumeroDeEntradas(); ++i) {
      const std::string& nome = conteudo_[conteudo].ListaDeEntradas()[i].nome;
      nomes_de_entrada_.insert(nome);
      for (const std::string& dir : diretorios_) {
        const std::string chave = Chave(dir, nome);
        // O PRIMEIRO GANHA, e a ordem e a dos nomes dos ficheiros: um nome que
        // apareca em dois pacotes da a mesma resposta em todas as corridas.
        if (alias_.count(chave) == 0) alias_[chave] = {p, conteudo, i};
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

  for (const std::string& c : candidatos) {
    const std::size_t barra = c.rfind('/');
    if (barra == std::string::npos || barra == 0 || barra + 1 >= c.size()) continue;
    const std::string dir = c.substr(0, barra);
    const std::string nome = c.substr(barra + 1);
    if (diretorios_.count(Minusculas(dir)) == 0) continue;
    if (alias_.find(Chave(dir, nome)) == alias_.end()) continue;
    return dir + "/" + nome;
  }
  return {};
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
  if (nomes_.count(so_nome) != 0) return so_nome;
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
  if (barra == std::string::npos) return false;
  const auto it = alias_.find(Chave(canonico.substr(0, barra), canonico.substr(barra + 1)));
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
      traco->Emitir(Area::Brew, Nivel::Informacao, "VFS_PACOTE",
                    p.ficheiro + ": " + Dez(p.entradas) + " entradas no indice");
    } else {
      // Um pacote que NAO entrou fica dito, e nao em silencio: sem isto, "o
      // jogo nao encontra o ficheiro" e "o pacote foi recusado" davam o mesmo
      // sintoma -- nada.
      traco->Emitir(Area::Brew, Nivel::Erro, "VFS_PACOTE_RECUSADO", p.ficheiro + ": " + p.motivo);
    }
  }
  traco->Emitir(Area::Brew, Nivel::Informacao, "VFS_ALIAS",
                "<dir>/<nome> servido da UNIAO dos pacotes; dirs aceites: " + Lista(diretorios_) +
                    "; prefixos aceitos antes do casamento: roms/, roms/neogeo/; nome sem distinguir "
                    "maiusculas");
}

}  // namespace zb2::brew
