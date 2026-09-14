#include "core/carga/bar.h"

#include <cstdio>
#include <fstream>
#include <utility>

// O formato lido aqui esta MEDIDO, campo a campo, no cabecalho `bar.h`. Cada
// verificacao abaixo recusa EM VOZ ALTA (P2): nunca devolve sucesso a fingir.
// Um `.bar` malformado no corpus e um facto a registar, nao um detalhe a
// esconder.

namespace zb2 {
namespace {

std::uint16_t Ler16(const std::uint8_t* p) {
  return static_cast<std::uint16_t>(p[0] | (static_cast<std::uint16_t>(p[1]) << 8));
}

std::uint32_t Ler32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::string Hex(std::uint32_t v) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "0x%08x", v);
  return buf;
}

}  // namespace

ArquivoBar ArquivoBar::AbrirDados(std::vector<std::uint8_t> bytes, std::string* motivo) {
  ArquivoBar a;
  a.bytes_ = std::move(bytes);
  if (!a.Validar()) {
    if (motivo != nullptr) {
      *motivo = a.motivo_;
    }
    return a;
  }
  if (motivo != nullptr) {
    motivo->clear();
  }
  return a;
}

ArquivoBar ArquivoBar::AbrirFicheiro(const std::string& caminho, std::string* motivo) {
  std::ifstream f(caminho, std::ios::binary);
  if (!f) {
    if (motivo != nullptr) {
      *motivo = "nao foi possivel abrir o ficheiro: " + caminho;
    }
    return ArquivoBar();
  }
  // Ler de uma vez so: o `pacmania.bar` tem 7.455.569 bytes, e ler por blocos
  // nao muda nada o resultado.
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                  std::istreambuf_iterator<char>());
  if (bytes.empty()) {
    if (motivo != nullptr) {
      *motivo = "ficheiro vazio: " + caminho;
    }
    return ArquivoBar();
  }
  return AbrirDados(std::move(bytes), motivo);
}

bool ArquivoBar::Validar() {
  valido_ = false;
  registros_.clear();
  deslocamentos_.clear();
  numero_de_recursos_ = 0;
  numero_de_ids_ = 0;

  const std::uint32_t n = static_cast<std::uint32_t>(bytes_.size());
  if (n < bar_campos::kCabecalho) {
    motivo_ = "ficheiro com " + Hex(n) + " bytes, abaixo dos 32 do cabecalho";
    return false;
  }
  const std::uint8_t* p = bytes_.data();

  const std::uint16_t versao = Ler16(p + 0);
  const std::uint16_t campo2 = Ler16(p + 2);
  const std::uint16_t campo4 = Ler16(p + 4);
  if (versao != bar_campos::kVersao || campo2 != bar_campos::kCampo2 || campo4 != bar_campos::kCampo4) {
    motivo_ = "assinatura nao bate certo: versao=" + Hex(versao) + " campo2=" + Hex(campo2) +
              " campo4=" + Hex(campo4) + " (esperado versao=0x00000011 campo2=0x00000001 campo4=0x00000001)";
    return false;
  }
  const std::uint16_t num_registos = Ler16(p + 6);
  const std::uint32_t off_registos = Ler32(p + 8);
  const std::uint32_t tam_registos = Ler32(p + 12);
  const std::uint32_t off_indices = Ler32(p + 16);
  const std::uint32_t num_ids = Ler32(p + 20);
  const std::uint32_t off_dados = Ler32(p + 24);

  if (off_registos != bar_campos::kDeslocamentoDosRegistos) {
    motivo_ = "deslocamento dos registos em " + Hex(off_registos) + ", medido como 0x00000020";
    return false;
  }
  if (tam_registos % 8u != 0u) {
    motivo_ = "tamanho dos registos " + Hex(tam_registos) + " nao e multiplo de 8";
    return false;
  }
  if (tam_registos / 8u != num_registos) {
    motivo_ = "tamanho dos registos " + Hex(tam_registos) + " da " + Hex(tam_registos / 8u) +
              " registos, e o cabecalho declara " + Hex(num_registos);
    return false;
  }
  if (off_indices != off_registos + tam_registos) {
    motivo_ = "tabela de indices em " + Hex(off_indices) + ", esperado " +
              Hex(off_registos + tam_registos) + " (fim dos registos)";
    return false;
  }
  if (off_indices > n) {
    motivo_ = "tabela de indices em " + Hex(off_indices) + ", para la do fim do ficheiro (" + Hex(n) + ")";
    return false;
  }
  const std::uint32_t bytes_dos_indices = off_dados - off_indices;
  if (off_dados < off_indices || bytes_dos_indices % 4u != 0u) {
    motivo_ = "zona da tabela de indices inconsistente: " + Hex(off_indices) + " .. " + Hex(off_dados);
    return false;
  }
  if (off_dados > n) {
    motivo_ = "inicio dos dados em " + Hex(off_dados) + ", para la do fim do ficheiro (" + Hex(n) + ")";
    return false;
  }

  // Os registos primeiro, a tabela depois. A ORDEM importa para o diagnostico:
  // se a soma dos ids for conferida depois da contagem de valores da tabela,
  // um `num_ids` errado acusa sempre a tabela, e a guarda da soma nunca chega a
  // ser exercitada -- foi o que aconteceu na primeira versao destes testes.
  registros_.reserve(num_registos);
  std::uint32_t soma = 0;
  for (std::uint32_t k = 0; k < num_registos; ++k) {
    const std::uint8_t* r = p + off_registos + 8u * k;
    RegistroDoBar reg;
    reg.tipo = Ler16(r + 0);
    reg.primeiro_id = Ler16(r + 2);
    reg.delta = Ler16(r + 4);
    reg.primeiro_indice = Ler16(r + 6);
    registros_.push_back(reg);
    soma += static_cast<std::uint32_t>(reg.delta) + 1u;
  }
  if (soma != num_ids) {
    motivo_ = "os registos cobrem " + Hex(soma) + " ids, e o cabecalho declara " + Hex(num_ids);
    return false;
  }

  // A tabela tem num_ids + 1 valores: um por id resolvivel, mais o fim do
  // ultimo recurso. Medido em 320 de 320 `.bar` do SDK.
  if (bytes_dos_indices / 4u != num_ids + 1u) {
    motivo_ = "tabela de indices com " + Hex(bytes_dos_indices / 4u) + " valores, e o cabecalho declara " +
              Hex(num_ids) + " ids (esperado num_ids + 1)";
    return false;
  }

  const std::uint32_t n_valores = num_ids + 1u;
  deslocamentos_.reserve(n_valores);
  for (std::uint32_t k = 0; k < n_valores; ++k) {
    deslocamentos_.push_back(Ler32(p + off_indices + 4u * k));
  }
  numero_de_recursos_ = n_valores - 1u;
  numero_de_ids_ = num_ids;

  if (deslocamentos_.front() != off_dados) {
    motivo_ = "primeiro deslocamento " + Hex(deslocamentos_.front()) + " difere do inicio dos dados " +
              Hex(off_dados);
    return false;
  }
  for (std::uint32_t k = 0; k < n_valores; ++k) {
    if (deslocamentos_[k] > n) {
      motivo_ = "deslocamento[" + Hex(k) + "] = " + Hex(deslocamentos_[k]) + " sai do ficheiro (" + Hex(n) + ")";
      return false;
    }
    if (k > 0 && deslocamentos_[k] < deslocamentos_[k - 1]) {
      motivo_ = "deslocamentos nao monotonos: [" + Hex(k - 1) + "]=" + Hex(deslocamentos_[k - 1]) + " > [" +
                Hex(k) + "]=" + Hex(deslocamentos_[k]);
      return false;
    }
  }
  // O ULTIMO valor da tabela e o tamanho do ficheiro. Medido em 320 de 320
  // `.bar` do SDK; e o unico limite de fim que o formato oferece, porque o
  // campo 28 do cabecalho nao serve para isso (ver `bar.h` seccao 1).
  if (deslocamentos_.back() != n) {
    motivo_ = "ultimo deslocamento " + Hex(deslocamentos_.back()) + " difere do tamanho do ficheiro " + Hex(n);
    return false;
  }

  // GUARDA: um registo que aponte para fora da tabela. Um `primeiro_indice` que
  // apanhe o valor de fim (== tamanho do ficheiro) daria um recurso de tamanho
  // negativo -- que e o mesmo que ler memoria que nao e do recurso.
  for (std::size_t k = 0; k < registros_.size(); ++k) {
    const RegistroDoBar& reg = registros_[k];
    const std::uint32_t ultimo = static_cast<std::uint32_t>(reg.primeiro_indice) + reg.delta;
    if (ultimo >= numero_de_recursos_) {
      motivo_ = "registo " + Hex(k) + " (tipo " + Hex(reg.tipo) + ", id " + Hex(reg.primeiro_id) +
                ") cobre indices ate " + Hex(ultimo) + ", e ha " + Hex(numero_de_recursos_) + " recursos";
      return false;
    }
  }

  valido_ = true;
  motivo_.clear();
  return true;
}

bool ArquivoBar::Procurar(std::uint16_t id, std::uint16_t tipo, std::uint32_t* indice,
                          std::string* motivo) const {
  if (!valido_) {
    if (motivo != nullptr) {
      *motivo = motivo_.empty() ? "arquivo invalido" : motivo_;
    }
    return false;
  }
  for (const RegistroDoBar& reg : registros_) {
    if (reg.tipo != tipo) {
      continue;
    }
    // Intervalo contiguo [primeiro_id, primeiro_id + delta]. A comparacao e
    // feita em 32 bits: `primeiro_id + delta` pode passar dos 65535 do u16.
    const std::uint32_t primeiro = reg.primeiro_id;
    const std::uint32_t ultimo = primeiro + reg.delta;
    if (id < primeiro || id > ultimo) {
      continue;
    }
    const std::uint32_t i = static_cast<std::uint32_t>(reg.primeiro_indice) + (id - primeiro);
    if (i >= numero_de_recursos_) {
      // GUARDA DEFENSIVA, e medido que e: com `Validar` a recusar os registos
      // que apontam para fora, partir esta guarda NAO deixa nenhum teste
      // vermelho (violacoes.py, M23). Fica porque `Procurar` e publica e nao
      // pode devolver um indice que sai da tabela.
      if (motivo != nullptr) {
        *motivo = "indice " + Hex(i) + " sai da tabela de " + Hex(numero_de_recursos_) + " recursos";
      }
      return false;
    }
    if (indice != nullptr) {
      *indice = i;
    }
    if (motivo != nullptr) {
      motivo->clear();
    }
    return true;
  }
  if (motivo != nullptr) {
    *motivo = "nao ha registo do tipo " + Hex(tipo) + " que cubra o id " + Hex(id);
  }
  return false;
}

RecursoDoBar ArquivoBar::LerPorIndice(std::uint32_t indice) const {
  RecursoDoBar r;
  r.indice = indice;
  if (!valido_) {
    r.motivo = motivo_.empty() ? "arquivo invalido" : motivo_;
    return r;
  }
  if (indice >= numero_de_recursos_) {
    r.motivo = "indice " + Hex(indice) + " sai da tabela de " + Hex(numero_de_recursos_) + " recursos";
    return r;
  }
  const std::uint32_t inicio = deslocamentos_[indice];
  const std::uint32_t fim = deslocamentos_[indice + 1u];
  if (fim < inicio || fim > bytes_.size()) {
    // GUARDA DEFENSIVA (violacoes.py M24): a monotonia e o ultimo deslocamento
    // igual ao tamanho do ficheiro, ja conferidos em `Validar`, tornam este
    // caso inalcancavel. Fica pela mesma razao do `Procurar`: o metodo e
    // publico e nao pode ler fora do recurso.
    r.motivo = "recurso " + Hex(indice) + " fora do ficheiro: " + Hex(inicio) + " .. " + Hex(fim);
    return r;
  }
  r.ok = true;
  r.dados = bytes_.data() + inicio;
  r.tamanho = fim - inicio;
  return r;
}

RecursoDoBar ArquivoBar::Ler(std::uint16_t id, std::uint16_t tipo) const {
  RecursoDoBar r;
  r.id = id;
  r.tipo = tipo;
  std::uint32_t indice = 0;
  std::string m;
  if (!Procurar(id, tipo, &indice, &m)) {
    r.motivo = m;
    return r;
  }
  RecursoDoBar por_indice = LerPorIndice(indice);
  por_indice.id = id;
  por_indice.tipo = tipo;
  return por_indice;
}

BlobDoBar ArquivoBar::LerBlob(const RecursoDoBar& recurso) {
  BlobDoBar b;
  if (!recurso.ok || recurso.dados == nullptr) {
    b.motivo = recurso.motivo.empty() ? "recurso nao lido" : recurso.motivo;
    return b;
  }
  if (recurso.tamanho < 4u) {
    b.motivo = "recurso de " + Hex(recurso.tamanho) + " bytes: pequeno demais para um AEEResBlob";
    return b;
  }
  const std::uint8_t deslocamento = recurso.dados[0];
  if (recurso.dados[1] != 0u) {
    b.motivo = "byte 1 do blob e " + Hex(recurso.dados[1]) + ", e o SDK diz que e sempre zero";
    return b;
  }
  // O mime tem de estar terminado em NUL DENTRO do recurso.
  std::uint32_t fim_do_mime = 0;
  for (std::uint32_t k = 2; k < recurso.tamanho; ++k) {
    if (recurso.dados[k] == 0u) {
      fim_do_mime = k;
      break;
    }
  }
  if (fim_do_mime < 2u) {
    b.motivo = "mime sem terminador NUL dentro dos " + Hex(recurso.tamanho) + " bytes do recurso";
    return b;
  }
  if (fim_do_mime == 2u) {
    b.motivo = "mime vazio";
    return b;
  }
  // A conta e feita com o limite: sem ele, um `fim_do_mime` a zero daria
  // 0xFFFFFFFE e o `assign` a seguir tentava copiar 4 GB.
  const std::uint32_t tam_do_mime = fim_do_mime >= 2u ? fim_do_mime - 2u : 0u;
  if (deslocamento < fim_do_mime + 1u || deslocamento >= recurso.tamanho) {
    // O dado comeca ANTES do fim do mime, ou depois do recurso: nos dois casos
    // nao ha dado nenhum para devolver.
    b.motivo = "bDataOffset " + Hex(deslocamento) + " incompativel com o mime (acaba em " + Hex(fim_do_mime) +
               ") e com o recurso de " + Hex(recurso.tamanho) + " bytes";
    return b;
  }
  b.ok = true;
  b.deslocamento = deslocamento;
  b.mime.assign(reinterpret_cast<const char*>(recurso.dados + 2), tam_do_mime);
  b.dados = recurso.dados + deslocamento;
  b.tamanho = recurso.tamanho - deslocamento;
  return b;
}

}  // namespace zb2
