#include "core/brew/recursos.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <utility>

namespace zb2::brew {

namespace {

std::string Hex(std::uint32_t v) {
  char b[16];
  std::snprintf(b, sizeof(b), "0x%08x", v);
  return b;
}

}  // namespace

const char* Nome(FormaDoRecurso f) {
  switch (f) {
    case FormaDoRecurso::Tamanho: return "tamanho";
    case FormaDoRecurso::Copia: return "copia";
    case FormaDoRecurso::Alocacao: return "alocacao";
    case FormaDoRecurso::Recusado: return "recusado";
  }
  return "?";
}

LeitorDeRecursos LeitorDaPasta(const std::string& pasta_do_titulo, const Vfs* vfs) {
  return [pasta_do_titulo, vfs](const std::string& ficheiro, std::vector<std::uint8_t>* bytes,
                                std::string* motivo) -> bool {
    if (bytes == nullptr) {
      if (motivo != nullptr) *motivo = "sem destino para os bytes";
      return false;
    }
    // A MESMA normalizacao da VFS. Sem VFS, aceita-se o nome tal como veio: o
    // teste que servir um `.bar` sintetico nao tem de registar pastas.
    std::string nome = ficheiro;
    if (vfs != nullptr) {
      nome = vfs->Normalizar(ficheiro);
      if (nome.empty()) {
        if (motivo != nullptr) *motivo = "a VFS do titulo nao tem " + ficheiro;
        return false;
      }
    }
    std::ifstream f(pasta_do_titulo + "/" + nome, std::ios::binary);
    if (!f) {
      if (motivo != nullptr) *motivo = "nao foi possivel abrir " + pasta_do_titulo + "/" + nome;
      return false;
    }
    bytes->assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    if (bytes->empty()) {
      if (motivo != nullptr) *motivo = "ficheiro vazio: " + nome;
      return false;
    }
    return true;
  };
}

Recursos::Recursos(Memoria& mem, Alocador& alocador, LeitorDeRecursos leitor, Traco* traco)
    : mem_(mem), al_(alocador), leitor_(std::move(leitor)), traco_(traco) {}

void Recursos::Recusar(const std::string& motivo, ResultadoDoRecurso* r) {
  r->ok = false;
  r->forma = FormaDoRecurso::Recusado;
  r->motivo = motivo;
  r->ponteiro = 0;
  r->bytes = 0;
  ++medicao_.recusados;
  ++recusas_[motivo];
  // P2: o caminho que nao serve REGISTA-SE, com o nome do metodo do SDK. Sem
  // isto, um recurso recusado seria indistinguivel de um recurso nunca pedido --
  // que e exactamente o defeito que o ledger chama "os descartes eram
  // invisiveis" (86 377 `glCullFace` descartados em silencio).
  if (traco_ != nullptr) traco_->RegistarFalta(Area::Brew, "IShell::LoadResDataEx", motivo);
}

const ArquivoBar* Recursos::Abrir(const std::string& nome, std::string* motivo) {
  const auto ja = abertos_.find(nome);
  if (ja != abertos_.end()) return &ja->second;
  const auto falhou = falha_ao_abrir_.find(nome);
  if (falhou != falha_ao_abrir_.end()) {
    // A falha tambem se memoriza: sem isto, um titulo que peca 20 recursos de um
    // ficheiro inexistente tentava abri-lo 20 vezes, e o registo ficava com 20
    // linhas iguais -- que e a definicao de ruido.
    *motivo = falhou->second;
    return nullptr;
  }
  if (!leitor_) {
    *motivo = "sem leitor de recursos configurado";
    falha_ao_abrir_[nome] = *motivo;
    return nullptr;
  }
  std::vector<std::uint8_t> bytes;
  std::string porque;
  if (!leitor_(nome, &bytes, &porque)) {
    *motivo = porque.empty() ? ("nao foi possivel ler " + nome) : porque;
    falha_ao_abrir_[nome] = *motivo;
    return nullptr;
  }
  std::string porque_bar;
  ArquivoBar bar = ArquivoBar::AbrirDados(std::move(bytes), &porque_bar);
  if (!bar.Valido()) {
    *motivo = nome + ": " + porque_bar;
    falha_ao_abrir_[nome] = *motivo;
    return nullptr;
  }
  // GUARDA-SE o `.bar` inteiro, e nao se abre por pedido: o `pacmania.bar` tem
  // 7.455.569 bytes e o `pacmania` pede recursos TRES vezes (medido). A tabela
  // vive o tempo do `Recursos`, que e UM titulo (o `Despacho` e construido por
  // titulo na bateria).
  const auto posto = abertos_.emplace(nome, std::move(bar));
  return &posto.first->second;
}

const ArquivoBar* Recursos::Arquivo(const std::string& nome) const {
  const auto it = abertos_.find(nome);
  return it == abertos_.end() ? nullptr : &it->second;
}

RecursoDoBar Recursos::Recurso(const std::string& ficheiro, std::uint16_t id,
                                std::uint16_t tipo) const {
  const ArquivoBar* bar = Arquivo(ficheiro);
  if (bar == nullptr) {
    RecursoDoBar r;
    r.motivo = "o ficheiro de recursos " + ficheiro + " nao esta aberto";
    return r;
  }
  return bar->Ler(id, tipo);
}

std::size_t Recursos::BytesNasAlocacoes() const {
  std::size_t total = 0;
  for (const auto& par : alocados_) total += par.second;
  return total;
}

ResultadoDoRecurso Recursos::Atender(const PedidoDeRecurso& pedido) {
  ResultadoDoRecurso r;
  ++medicao_.pedidos;

  // 1. Os dois argumentos que o contrato exige, conferidos ANTES de tocar em
  //    memoria. `pnBufSize` "Cannot be NULL" (AEEIShell.h:2447): escrever o
  //    tamanho num nulo seria escrever no endereco 0 do guest, que e codigo.
  if (pedido.ficheiro.empty()) {
    Recusar("pszResFile vazio (o SDK espera o NOME do ficheiro, ex. \"pacmania.bar\")", &r);
    return r;
  }
  if (pedido.pn_tamanho == 0) {
    Recusar("pnBufSize nulo, e o cabecalho do SDK diz \"Cannot be NULL\"", &r);
    return r;
  }

  // 2. O ficheiro. Um `.bar` que nao existe, ou que nao bate certo com o formato
  //    medido, recusa-se com o motivo do leitor (P2) -- e o `core/carga/bar.cpp`
  //    ja diz QUAL campo nao bateu.
  std::string porque;
  const ArquivoBar* bar = Abrir(pedido.ficheiro, &porque);
  if (bar == nullptr) {
    Recusar(porque, &r);
    return r;
  }

  // 3. O recurso. O id 0 e o caso que se ve no corpus: DUAS das tres chamadas do
  //    `pacmania` tem `r2 = 0` (medido), e o `pacmania.bar` nao tem id 0 em
  //    registo nenhum (os registos cobrem 5001..5118, 5120..5120, 5121..5126,
  //    5127..5127 e 5128..5144). A recusa diz-lo com o id e o tipo.
  const RecursoDoBar recurso = bar->Ler(pedido.id, pedido.tipo);
  if (!recurso.ok) {
    Recusar(pedido.ficheiro + ": " + recurso.motivo, &r);
    return r;
  }

  // 4. O TAMANHO E O DO RECURSO INTEIRO. A prova e do VENDEDOR
  //    (`UTResFile.c:142-177`): o tamanho da forma "so o tamanho" e igual ao
  //    numero de bytes que a alocacao devolve, e os dois buffers comparam-se
  //    iguais byte a byte. Para um recurso de imagem, o recurso inteiro e o
  //    `AEEResBlob`, e o dado comeca em `blob + bDataOffset` -- quem soma e o
  //    JOGO (`pacmania.mod:0x110dd4`, `add r1, r5, r0`).
  const std::uint32_t tamanho = recurso.tamanho;
  r.bytes = tamanho;
  if (pedido.tipo == kTipoImagem) {
    const BlobDoBar blob = ArquivoBar::LerBlob(recurso);
    if (blob.ok) r.mime = blob.mime;
  }

  if (pedido.buffer == kSoOTamanho) {
    // FORMA 1: so o tamanho. O RETORNO E -1, e nao o tamanho (AEEIShell.h:2457:
    // "If pBuf was -1, ... the function returns -1 (0xffffffff)"). Devolver o
    // tamanho no r0 seria o "obvio" e estaria errado: o `pacmania` compara o
    // r0 com o literal do id e o -1 e o que ele procura.
    mem_.Escrever32(pedido.pn_tamanho, tamanho);
    r.ok = true;
    r.forma = FormaDoRecurso::Tamanho;
    r.ponteiro = kSoOTamanho;
    ++medicao_.de_tamanho;
  } else if (pedido.buffer != 0) {
    // FORMA 2: copiar para o buffer do chamador. Na entrada, `*pnBufSize` e o
    // TAMANHO DESSE BUFFER (AEEIShell.h:2449) e, se nao couber, a funcao devolve
    // NULL sem escrever nada. Nao se escreve meio recurso: isso seria entregar
    // ao jogo um buffer com lixo no fim, sem nada a acusar.
    const std::uint32_t cabem = mem_.Ler32(pedido.pn_tamanho);
    if (cabem < tamanho) {
      Recusar(pedido.ficheiro + ": buffer do chamador com " + Hex(cabem) + " bytes para um recurso de " +
                  Hex(tamanho) + " (o SDK devolve NULL e nao escreve)", &r);
      return r;
    }
    mem_.EscreverBloco(pedido.buffer, recurso.dados, tamanho);
    mem_.Escrever32(pedido.pn_tamanho, tamanho);
    r.ok = true;
    r.forma = FormaDoRecurso::Copia;
    r.ponteiro = pedido.buffer;
    ++medicao_.copiados;
  } else {
    // FORMA 3: alocar. Pelo ALOCADOR DO GUEST, e nao pelo `malloc` do
    // hospedeiro: o ponteiro vai para dentro do jogo, que o passa ao
    // `FreeResData` e o guarda na sua propria contabilidade -- um endereco do
    // hospedeiro ali seria um endereco que nao existe na memoria do guest.
    const std::uint32_t novo = al_.Malloc(tamanho);
    if (novo == 0) {
      Recusar(pedido.ficheiro + ": o alocador do guest recusou " + Hex(tamanho) + " bytes", &r);
      return r;
    }
    mem_.EscreverBloco(novo, recurso.dados, tamanho);
    mem_.Escrever32(pedido.pn_tamanho, tamanho);
    alocados_[novo] = tamanho;
    r.ok = true;
    r.forma = FormaDoRecurso::Alocacao;
    r.ponteiro = novo;
    ++medicao_.alocados;
  }

  ++medicao_.servidos;
  medicao_.bytes_servidos += tamanho;
  if (traco_ != nullptr) {
    traco_->Emitir(Area::Brew, Nivel::Depuracao, "RES_DADOS",
                   pedido.ficheiro + " id=" + Hex(pedido.id) + " mime=" + (r.mime.empty() ? "-" : r.mime) +
                       " bytes=" + std::to_string(tamanho) + " forma=" + Nome(r.forma) + " r0=" +
                       Hex(r.ponteiro));
  }
  return r;
}

ResultadoDoTexto Recursos::ServirTexto(const PedidoDeTexto& pedido) {
  ResultadoDoTexto r;
  ++medicao_.textos;
  if (pedido.ficheiro.empty()) {
    r.motivo = "pszBaseFile vazio";
    ++medicao_.recusados;
    ++recusas_[r.motivo];
    if (traco_ != nullptr) traco_->RegistarFalta(Area::Brew, "IShell::LoadResString", r.motivo);
    return r;
  }
  std::string porque;
  const ArquivoBar* bar = Abrir(pedido.ficheiro, &porque);
  if (bar == nullptr) {
    r.motivo = porque;
    ++medicao_.recusados;
    ++recusas_[r.motivo];
    if (traco_ != nullptr) traco_->RegistarFalta(Area::Brew, "IShell::LoadResString", r.motivo);
    return r;
  }
  const RecursoDoBar recurso = bar->Ler(pedido.id, kTipoTexto);
  if (!recurso.ok) {
    r.motivo = pedido.ficheiro + " (texto): " + recurso.motivo;
    ++medicao_.recusados;
    ++recusas_[r.motivo];
    if (traco_ != nullptr) traco_->RegistarFalta(Area::Brew, "IShell::LoadResString", r.motivo);
    return r;
  }
  r.bytes_do_recurso = recurso.tamanho;
  if (recurso.tamanho < 2u) {
    r.motivo = "recurso de texto com " + Hex(recurso.tamanho) + " bytes: so cabe a marca";
    ++medicao_.recusados;
    ++recusas_[r.motivo];
    if (traco_ != nullptr) traco_->RegistarFalta(Area::Brew, "IShell::LoadResString", r.motivo);
    return r;
  }
  r.marca = recurso.dados[0];
  if (r.marca != kMarcaDoTextoDe8Bits) {
    // RECUSA PELO NOME DA MARCA (P2): 0xFF/0xFD/0xFE sao os `aeecontrols.bar` do
    // `he`, do `ja` e do `ko` -- textos de 16 bits, que NAO estao descodificados
    // aqui. Devolver os bytes crus num buffer de AECHAR seria entregar ao jogo
    // uma cadeia que ele nao sabe ler.
    r.motivo = "texto com marca 0x" + Hex(r.marca).substr(8, 2) +
               " (so a marca 0x03 dos textos de 8 bits esta medida)";
    ++medicao_.recusados;
    ++recusas_[r.motivo];
    if (traco_ != nullptr) traco_->RegistarFalta(Area::Brew, "IShell::LoadResString", r.motivo);
    return r;
  }
  const std::uint32_t caracteres = recurso.tamanho - 1u;
  // O destino tem de levar os caracteres em UTF-16 e, se couber, o terminador.
  const std::uint32_t precisos = (caracteres + 1u) * 2u;
  if (pedido.destino == 0 || pedido.n_bytes < precisos) {
    r.motivo = "buffer de destino com " + Hex(pedido.n_bytes) + " bytes para " +
               std::to_string(caracteres) + " caracteres (UTF-16, mais o terminador)";
    ++medicao_.recusados;
    ++recusas_[r.motivo];
    if (traco_ != nullptr) traco_->RegistarFalta(Area::Brew, "IShell::LoadResString", r.motivo);
    return r;
  }
  for (std::uint32_t k = 0; k < caracteres; ++k) {
    mem_.Escrever16(pedido.destino + k * 2u, recurso.dados[1u + k]);
  }
  mem_.Escrever16(pedido.destino + caracteres * 2u, 0);
  r.ok = true;
  r.caracteres = caracteres;
  ++medicao_.servidos;
  if (traco_ != nullptr) {
    traco_->Emitir(Area::Brew, Nivel::Depuracao, "RES_TEXTO",
                   pedido.ficheiro + " id=" + Hex(pedido.id) + " caracteres=" +
                       std::to_string(caracteres));
  }
  return r;
}

bool Recursos::Libertar(std::uint32_t endereco) {
  if (endereco == 0) {
    // `FreeResData(po, NULL)` nao e um erro: e "nao ha nada para libertar", e o
    // contrato do metodo e `void`. Conta-se, porque um jogo que o faca sempre e
    // um facto sobre o jogo.
    ++medicao_.libertacoes_de_nulo;
    return true;
  }
  const auto it = alocados_.find(endereco);
  if (it == alocados_.end()) {
    const std::string motivo = "FreeResData de " + Hex(endereco) +
                               ", que este servico nao alocou (ou ja foi libertado)";
    ++recusas_[motivo];
    if (traco_ != nullptr) traco_->RegistarFalta(Area::Brew, "IShell::FreeResData", motivo);
    return false;
  }
  const std::uint32_t bytes = it->second;
  al_.Free(endereco);
  alocados_.erase(it);
  medicao_.bytes_libertados += bytes;
  ++medicao_.libertacoes;
  if (traco_ != nullptr) {
    traco_->Emitir(Area::Brew, Nivel::Depuracao, "RES_LIVRE",
                   Hex(endereco) + " bytes=" + std::to_string(bytes));
  }
  return true;
}

}  // namespace zb2::brew
