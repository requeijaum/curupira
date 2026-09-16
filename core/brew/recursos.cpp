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

std::uint16_t Ler16(const std::uint8_t* p) {
  return static_cast<std::uint16_t>(p[0] | (static_cast<std::uint16_t>(p[1]) << 8));
}

std::uint32_t Ler32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

// O ultimo valor da tabela de deslocamentos de um contentor `.bar`/`.mif`, com
// a forma do cabecalho conferida: magic 0x0011, indices a seguir aos registos,
// e o valor no intervalo [inicio dos dados, tamanho do ficheiro]. `false` sem
// tocar em `*fim` quando o cabecalho nao tem a forma do contentor.
bool UltimoDeslocamento(const std::vector<std::uint8_t>& bytes, std::uint32_t* fim) {
  if (bytes.size() < bar_campos::kCabecalho) return false;
  const std::uint8_t* p = bytes.data();
  if (Ler16(p) != bar_campos::kVersao || Ler16(p + 2) != bar_campos::kCampo2 ||
      Ler16(p + 4) != bar_campos::kCampo4) {
    return false;
  }
  const std::uint32_t off_indices = Ler32(p + 16);
  const std::uint32_t num_ids = Ler32(p + 20);
  const std::uint32_t off_dados = Ler32(p + 24);
  if (num_ids == 0u || off_indices + 4u * num_ids + 4u > bytes.size()) return false;
  const std::uint32_t ultimo = Ler32(p + off_indices + 4u * num_ids);
  if (ultimo < off_dados || ultimo > bytes.size()) return false;
  if (fim != nullptr) *fim = ultimo;
  return true;
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
    // FORMA 2: copiar para o buffer do chamador. O cabecalho diz que `*pnBufSize`
    // de entrada e o TAMANHO DESSE BUFFER (AEEIShell.h:2449) e que, se nao
    // couber, a funcao devolve NULL sem escrever nada. Nao se escreve meio
    // recurso: isso seria entregar ao jogo um buffer com lixo no fim.
    //
    // **E O NUMERO DECLARADO NAO E SEMPRE A VERDADE.** MEDIDO (base `97086eb`, 62
    // titulos): TRES titulos trazem um `*pnBufSize` menor que o recurso E menor
    // que o BLOCO que eles proprios alocaram --
    //   peggle           resources.bar id=5000 tipo=6    declara 0x6   para 0xfc75 (64 629 B)
    //   torkandkral      data.bar (19 ids)               declara 0x83  para 0x8020..0x100020
    //   heavyweaponbrew  heavyweapon.bar id=9317         declara 0x449 para 0x3030
    // O `peggle` e o caso claro: perguntou o tamanho (forma 1), alocou os 64 629
    // bytes que a resposta deu, e chamou de novo com o `*pnBufSize` a valer 6 --
    // o numero do TIPO, que ficou na variavel. Medir o BLOCO e a leitura honesta:
    // nao e o numero que o jogo disse, e o que ele de facto reservou, e a copia
    // continua limitada ao que existe.
    //
    // ISTO CONTRADIZ O TEXTO LITERAL DO CABECALHO, que manda julgar pelo numero
    // declarado. A contradicao fica escrita de proposito: o que sustenta o ramo e
    // a medicao dos tres titulos, e cada servico por esta via regista o
    // pressuposto para o relatorio o mostrar -- se um titulo piorar na bateria, a
    // leitura literal volta.
    const std::uint32_t declarado = mem_.Ler32(pedido.pn_tamanho);
    const std::uint32_t bloco = al_.TamanhoDoBloco(pedido.buffer);
    const std::uint32_t capacidade = declarado > bloco ? declarado : bloco;
    if (capacidade < tamanho) {
      Recusar(pedido.ficheiro + " id=" + std::to_string(pedido.id) + " tipo=" +
                  std::to_string(pedido.tipo) + ": buffer do chamador com " + Hex(declarado) +
                  " bytes (bloco " + Hex(bloco) + ") para um recurso de " + Hex(tamanho) +
                  " (o SDK devolve NULL e nao escreve)",
              &r);
      return r;
    }
    if (bloco > declarado && traco_ != nullptr) {
      traco_->RegistarPressuposto(
          Area::Brew, "IShell::LoadResDataEx serviu pelo BLOCO, nao pelo *pnBufSize declarado",
          pedido.ficheiro + " id=" + std::to_string(pedido.id) + " declara " + Hex(declarado) +
              " e o bloco tem " + Hex(bloco) + ", recurso de " + Hex(tamanho));
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
  if (pedido.base_nula) {
    // A BASE NULA SELECIONA O PROPRIO MODULO, e nao um ficheiro: o SDK le
    // autor/direitos/versao com `LoadResString(ps,NULL,IDS_MIF_*,...)`
    // (`AEEShell.h:919-921`, `ISHELL_GetAppAuthor/Copyright/Version`; ids 6/7/8
    // em `AEEShell.h:129-131`). O conteudo vem do `.mif`, que fica na pasta
    // IRMA da pasta do modulo -- `<pai de dir_>/mif/<pasta_>.mif`, medido em 62
    // ficheiros do corpus (um por titulo, ao lado da `mod/` que a bateria
    // recebe) --, e quem sabe esse caminho e o despacho: uma linha (patch
    // separado `rec-despacho.patch`) que o poe no `pedido.ficheiro`. Sem essa
    // linha, recusa-se com o id (P2), e a demanda continua a dizer QUAL cadeia
    // do modulo o titulo pediu (o `alpineracerex` pediu 1x com base nula).
    if (pedido.ficheiro.empty()) {
      char detalhe[256];
      std::snprintf(detalhe, sizeof(detalhe),
                    "pszBaseFile nulo com id=%s: cadeia do proprio modulo, mas sem a "
                    "localizacao do .mif (o despacho ainda nao aplicou o rec-despacho.patch)",
                    Hex(pedido.id).c_str());
      r.motivo = detalhe;
      ++medicao_.recusados;
      ++recusas_[r.motivo];
      if (traco_ != nullptr) traco_->RegistarFalta(Area::Brew, "IShell::LoadResString", r.motivo);
      return r;
    }

    // O `.mif` le-se pelo CAMINHO, e nao pelo leitor do `.bar`: ele vive FORA
    // da pasta do titulo (o dominio da VFS), e a VFS RETIRA o `..` em vez de o
    // resolver (medido em `core/brew/vfs.cpp`). Por isso o `LeitorDaPasta` nao
    // chega la; quem chega e o caminho directo, posto pelo despacho.
    std::vector<std::uint8_t> bytes;
    {
      std::ifstream f(pedido.ficheiro, std::ios::binary);
      if (!f) {
        r.motivo = "nao foi possivel abrir o MIF do proprio modulo: " + pedido.ficheiro;
        ++medicao_.recusados;
        ++recusas_[r.motivo];
        if (traco_ != nullptr) traco_->RegistarFalta(Area::Brew, "IShell::LoadResString", r.motivo);
        return r;
      }
      bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
      if (bytes.empty()) {
        r.motivo = "MIF vazio: " + pedido.ficheiro;
        ++medicao_.recusados;
        ++recusas_[r.motivo];
        if (traco_ != nullptr) traco_->RegistarFalta(Area::Brew, "IShell::LoadResString", r.motivo);
        return r;
      }
    }

    // O `.mif` e o MESMO contentor do `.bar` (magic 0x0011, registos de 8
    // bytes, tabela de deslocamentos). 50 de 62 terminam exactamente no ultimo
    // valor da tabela; os outros 12 trazem 20 bytes a mais, DEPOIS dele
    // (medido: 274791.mif, 274803.mif, ...) -- o conteudo dos recursos termina
    // no ultimo deslocamento, e o rodape nao e recurso nenhum. Se a validacao
    // estrita do `ArquivoBar` recusar por isso, corta-se no ultimo deslocamento
    // e valida-se outra vez.
    std::string porque_mif;
    ArquivoBar mif = ArquivoBar::AbrirDados(bytes, &porque_mif);
    if (!mif.Valido()) {
      std::uint32_t fim = 0;
      if (UltimoDeslocamento(bytes, &fim) && fim < bytes.size()) {
        bytes.resize(fim);
        mif = ArquivoBar::AbrirDados(bytes, &porque_mif);
      }
    }
    if (!mif.Valido()) {
      r.motivo = pedido.ficheiro + " (MIF): " + porque_mif;
      ++medicao_.recusados;
      ++recusas_[r.motivo];
      if (traco_ != nullptr) traco_->RegistarFalta(Area::Brew, "IShell::LoadResString", r.motivo);
      return r;
    }

    const RecursoDoBar recurso = mif.Ler(pedido.id, kTipoTexto);
    if (!recurso.ok) {
      r.motivo = pedido.ficheiro + " (MIF): " + recurso.motivo;
      ++medicao_.recusados;
      ++recusas_[r.motivo];
      if (traco_ != nullptr) traco_->RegistarFalta(Area::Brew, "IShell::LoadResString", r.motivo);
      return r;
    }
    r.bytes_do_recurso = recurso.tamanho;
    r.marca = recurso.dados[0];  // a marca que abre o recurso, como no ramo do `.bar`

    // A MARCA do texto, como no `.bar`: 0x03 = um byte por caractere. Os `.mif`
    // trazem tambem UTF-16 com BOM -- `0xFF 0xFE` (menor primeiro) em 135 dos
    // recursos de texto dos 62 `.mif` medidos, e o `0xFE 0xFF` (maior primeiro)
    // e a outra metade do BOM; e o que o SDK chama "UNICODE (UCS2 encoding)"
    // (`AEEIShell.h`). Qualquer outra marca recusa-se pelo NOME dela (P2), como
    // no ramo do `.bar`.
    std::vector<std::uint16_t> codepoints;
    const std::uint8_t* dados = recurso.dados;
    const std::uint32_t n = recurso.tamanho;
    if (n >= 2u && dados[0] == 0xffu && dados[1] == 0xfeu) {
      for (std::uint32_t k = 2u; k + 1u < n; k += 2u) {
        const std::uint16_t ch =
            static_cast<std::uint16_t>(dados[k] | (static_cast<std::uint16_t>(dados[k + 1u]) << 8));
        if (ch == 0) break;
        codepoints.push_back(ch);
      }
    } else if (n >= 2u && dados[0] == 0xfeu && dados[1] == 0xffu) {
      for (std::uint32_t k = 2u; k + 1u < n; k += 2u) {
        const std::uint16_t ch =
            static_cast<std::uint16_t>((static_cast<std::uint16_t>(dados[k]) << 8) | dados[k + 1u]);
        if (ch == 0) break;
        codepoints.push_back(ch);
      }
    } else if (n >= 1u && dados[0] == 0x03u) {
      for (std::uint32_t k = 1u; k < n; ++k) {
        if (dados[k] == 0) break;
        codepoints.push_back(dados[k]);
      }
    } else {
      const std::string marca =
          n >= 2u ? Hex(dados[0]).substr(8, 2) + " " + Hex(dados[1]).substr(8, 2)
                  : Hex(dados[0]).substr(8, 2);
      r.motivo = "texto do MIF com marca 0x" + marca +
                 " (so 0x03 e o BOM UTF-16 0xFF 0xFE / 0xFE 0xFF estao medidas)";
      ++medicao_.recusados;
      ++recusas_[r.motivo];
      if (traco_ != nullptr) traco_->RegistarFalta(Area::Brew, "IShell::LoadResString", r.motivo);
      return r;
    }
    if (codepoints.empty()) {
      r.motivo = pedido.ficheiro + " id=" + Hex(pedido.id) + ": texto sem caracteres apos a marca";
      ++medicao_.recusados;
      ++recusas_[r.motivo];
      if (traco_ != nullptr) traco_->RegistarFalta(Area::Brew, "IShell::LoadResString", r.motivo);
      return r;
    }
    const std::uint32_t caracteres = static_cast<std::uint32_t>(codepoints.size());
    // O destino tem de levar os caracteres em UTF-16 e, se couber, o terminador
    // -- a MESMA regra do ramo do `.bar`.
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
      mem_.Escrever16(pedido.destino + k * 2u, codepoints[k]);
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
