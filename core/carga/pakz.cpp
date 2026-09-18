#include "core/carga/pakz.h"

#include <lzma.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

namespace zb2 {
namespace {

std::uint32_t Ler32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint64_t Ler64(const std::uint8_t* p) {
  return static_cast<std::uint64_t>(p[0]) | (static_cast<std::uint64_t>(p[1]) << 8) |
         (static_cast<std::uint64_t>(p[2]) << 16) | (static_cast<std::uint64_t>(p[3]) << 24) |
         (static_cast<std::uint64_t>(p[4]) << 32) | (static_cast<std::uint64_t>(p[5]) << 40) |
         (static_cast<std::uint64_t>(p[6]) << 48) | (static_cast<std::uint64_t>(p[7]) << 56);
}

void Recusar(std::string* motivo, const std::string& porque) {
  if (motivo != nullptr) *motivo = porque;
}

std::string Minusculas(const std::string& s) {
  std::string r = s;
  for (char& c : r) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return r;
}

}  // namespace

bool Pakz::Parse(std::vector<std::uint8_t> bytes, std::string* motivo) {
  if (motivo != nullptr) motivo->clear();
  if (ficheiro_.is_open()) ficheiro_.close();
  indexado_de_ficheiro_ = false;
  tamanho_do_ficheiro_ = 0;
  bytes_ = std::move(bytes);
  entradas_.clear();
  nomes_repetidos_ = 0;
  nomes_no_campo_inteiro_ = 0;
  valido_ = false;
  motivo_.clear();

  const std::size_t n = bytes_.size();
  if (n < pakz_campos::kCabecalho) {
    motivo_ = "ficheiro com " + std::to_string(n) + " bytes: o cabecalho do PAKZ ocupa " +
              std::to_string(pakz_campos::kCabecalho);
    Recusar(motivo, motivo_);
    return false;
  }
  if (std::memcmp(bytes_.data(), pakz_campos::kAssinatura, 4) != 0) {
    char b[96];
    std::snprintf(b, sizeof(b), "assinatura \"%c%c%c%c\" em vez de \"PACK\"", bytes_[0], bytes_[1],
                  bytes_[2], bytes_[3]);
    motivo_ = b;
    Recusar(motivo, motivo_);
    return false;
  }

  const std::uint32_t offset_da_tabela = Ler32(&bytes_[4]);
  const std::uint32_t tamanho_da_tabela = Ler32(&bytes_[8]);
  if (tamanho_da_tabela == 0 || tamanho_da_tabela % pakz_campos::kRegisto != 0) {
    motivo_ = "tabela com " + std::to_string(tamanho_da_tabela) + " bytes: tem de ser multiplo dos " +
              std::to_string(pakz_campos::kRegisto) + " do registo (e maior que zero)";
    Recusar(motivo, motivo_);
    return false;
  }
  const std::uint64_t fim_da_tabela =
      static_cast<std::uint64_t>(offset_da_tabela) + tamanho_da_tabela;
  if (fim_da_tabela > n) {
    motivo_ = "tabela no offset " + std::to_string(offset_da_tabela) + " com " +
              std::to_string(tamanho_da_tabela) + " bytes acaba em " + std::to_string(fim_da_tabela) +
              " e o ficheiro tem " + std::to_string(n);
    Recusar(motivo, motivo_);
    return false;
  }
  if (offset_da_tabela < pakz_campos::kCabecalho) {
    motivo_ = "tabela no offset " + std::to_string(offset_da_tabela) +
              ", antes dos dados (que comecam em 12)";
    Recusar(motivo, motivo_);
    return false;
  }

  // --- os registos ---
  const std::uint32_t count = tamanho_da_tabela / pakz_campos::kRegisto;
  entradas_.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    const std::uint8_t* r =
        &bytes_[offset_da_tabela + static_cast<std::size_t>(i) * pakz_campos::kRegisto];

    // O NOME vive nos 56 primeiros bytes e acaba no primeiro NUL -- e nao no
    // byte 40: os 10 ficheiros do corpus tem nomes de ate 43 caracteres, e o
    // rabo do nome vive nos bytes 40..55 (o zeebulator-upstream le so 40 e
    // chama aos bytes 40..55 campos "nao confirmados").
    std::size_t comprimento = 0;
    while (comprimento < pakz_campos::kCampoDoNome && r[comprimento] != 0) ++comprimento;
    if (comprimento == pakz_campos::kCampoDoNome) {
      motivo_ = "nome da entrada " + std::to_string(i) + " sem NUL nos " +
                std::to_string(pakz_campos::kCampoDoNome) + " bytes";
      Recusar(motivo, motivo_);
      return false;
    }
    if (comprimento == 0) {
      motivo_ = "nome da entrada " + std::to_string(i) + " vazio";
      Recusar(motivo, motivo_);
      return false;
    }
    if (comprimento >= 40u) ++nomes_no_campo_inteiro_;

    std::string nome(reinterpret_cast<const char*>(r), comprimento);
    // Os nomes medidos sao todos ASCII imprimivel (7 487 de 7 487). Um byte de
    // controle aqui e um sinal de tabela lida no sitio errado; um nome com lixo
    // dentro nunca casaria com o que o guest pede, em silencio.
    for (char c : nome) {
      if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7e) {
        char b[128];
        std::snprintf(b, sizeof(b), "nome da entrada %u com byte 0x%02x (nao imprimivel)", i,
                      static_cast<unsigned char>(c));
        motivo_ = b;
        Recusar(motivo, motivo_);
        return false;
      }
    }

    EntradaDoPakz e;
    e.nome = std::move(nome);
    e.offset = Ler32(r + pakz_campos::kOffsetDoOffset);
    e.tamanho_comprimido = Ler32(r + pakz_campos::kOffsetDoTamanho);
    if (static_cast<std::uint64_t>(e.offset) + e.tamanho_comprimido > n) {
      motivo_ = "entrada " + std::to_string(i) + " (\"" + e.nome + "\"): dados de " +
                std::to_string(e.tamanho_comprimido) + " bytes no offset " +
                std::to_string(e.offset) + " passam do fim do ficheiro (" + std::to_string(n) + ")";
      Recusar(motivo, motivo_);
      return false;
    }
    if (e.tamanho_comprimido < pakz_campos::kPropsDoLzma + pakz_campos::kTamanhoDeclaradoDoLzma) {
      motivo_ = "entrada " + std::to_string(i) + " (\"" + e.nome + "\"): " +
                std::to_string(e.tamanho_comprimido) + " bytes de dados, e um stream LZMA_ALONE tem " +
                std::to_string(pakz_campos::kPropsDoLzma + pakz_campos::kTamanhoDeclaradoDoLzma) +
                " so de cabecalho";
      Recusar(motivo, motivo_);
      return false;
    }
    entradas_.push_back(std::move(e));
  }

  // A CONTIGUIDADE: medido em 7 487 entradas de 10 ficheiros, os dados comecam
  // em 12, cada entrada comeca onde a anterior acabou, e a ultima acaba no
  // offset da tabela. Quem le um registo com outra geometria le o ficheiro no
  // sitio errado -- e a soma dos tamanhos e a forma de o dizer sem descomprimir.
  std::uint64_t soma = 0;
  for (const EntradaDoPakz& e : entradas_) {
    if (e.offset != pakz_campos::kCabecalho + soma) {
      motivo_ = "dados nao contiguos: \"" + e.nome + "\" no offset " + std::to_string(e.offset) +
                " e o fim do anterior e " + std::to_string(pakz_campos::kCabecalho + soma);
      Recusar(motivo, motivo_);
      return false;
    }
    soma += e.tamanho_comprimido;
  }
  if (soma != static_cast<std::uint64_t>(offset_da_tabela) - pakz_campos::kCabecalho) {
    motivo_ = "a soma dos dados comprimidos e " + std::to_string(soma) + " e a tabela fica em " +
              std::to_string(offset_da_tabela) + " (dados: " +
              std::to_string(static_cast<std::uint64_t>(offset_da_tabela) - pakz_campos::kCabecalho) +
              ")";
    Recusar(motivo, motivo_);
    return false;
  }

  // O TAMANHO DESCOMPRIMIDO vem do proprio stream (bytes 5..12), e nao de um
  // registo: e o campo que o censo confirmou bater com o produzido em 7 486
  // de 7 486 entradas. LE-SE DEPOIS da contiguidade, e so depois: a leitura
  // so tem sentido com os offsets ja confirmados (uma geometria mentirosa
  // daria aqui lixo, e a recusa diria a doenca errada). Os 7 487 do corpus
  // declaram o tamanho; um stream com o "desconhecido" (0xffffffffffffffff)
  // fica com o campo a zero, e a Extrair cresce com teto -- e o liblzma do
  // proprio teste monta streams assim, logo o caminho e de verdade e nao so
  // defesa.
  for (EntradaDoPakz& e : entradas_) {
    const std::uint64_t declarado = Ler64(&bytes_[e.offset + pakz_campos::kPropsDoLzma]);
    if (declarado != pakz_campos::kTamanhoDesconhecidoDoLzma) {
      if (declarado > 0xffffffffull) {
        motivo_ = "entrada \"" + e.nome + "\": o stream declara " + std::to_string(declarado) +
                  " bytes descomprimidos, mais do que os 4 GiB do campo";
        Recusar(motivo, motivo_);
        return false;
      }
      e.tamanho_descomprimido = static_cast<std::uint32_t>(declarado);
    }
  }

  // A REPETICAO DE NOMES nao e erro -- e um facto do ficheiro, contado. Quem
  // resolve um nome (a VFS) escolhe o primeiro; quem le isto fica a saber.
  std::vector<std::string> vistos;
  vistos.reserve(entradas_.size());
  for (const EntradaDoPakz& e : entradas_) vistos.push_back(Minusculas(e.nome));
  std::sort(vistos.begin(), vistos.end());
  for (std::size_t i = 1; i < vistos.size(); ++i) {
    if (vistos[i] == vistos[i - 1]) ++nomes_repetidos_;
  }

  valido_ = true;
  return true;
}

bool Pakz::IndexarFicheiro(const std::string& caminho, std::string* motivo) {
  if (motivo != nullptr) motivo->clear();
  if (ficheiro_.is_open()) ficheiro_.close();
  bytes_.clear();
  entradas_.clear();
  nomes_repetidos_ = 0;
  nomes_no_campo_inteiro_ = 0;
  valido_ = false;
  motivo_.clear();
  indexado_de_ficheiro_ = false;
  tamanho_do_ficheiro_ = 0;

  ficheiro_.open(caminho, std::ios::binary);
  if (!ficheiro_) {
    motivo_ = "nao consegui abrir para indice: " + caminho;
    Recusar(motivo, motivo_);
    return false;
  }
  ficheiro_.seekg(0, std::ios::end);
  const std::streampos fim = ficheiro_.tellg();
  if (fim < 0) {
    motivo_ = "nao consegui medir o ficheiro";
    Recusar(motivo, motivo_);
    ficheiro_.close();
    return false;
  }
  tamanho_do_ficheiro_ = static_cast<std::uint64_t>(fim);
  if (tamanho_do_ficheiro_ < pakz_campos::kCabecalho) {
    motivo_ = "ficheiro com " + std::to_string(tamanho_do_ficheiro_) +
              " bytes: o cabecalho do PAKZ ocupa " + std::to_string(pakz_campos::kCabecalho);
    Recusar(motivo, motivo_);
    ficheiro_.close();
    return false;
  }

  std::uint8_t cabecalho[pakz_campos::kCabecalho];
  ficheiro_.seekg(0, std::ios::beg);
  ficheiro_.read(reinterpret_cast<char*>(cabecalho), sizeof(cabecalho));
  if (ficheiro_.gcount() != static_cast<std::streamsize>(sizeof(cabecalho))) {
    motivo_ = "nao consegui ler o cabecalho inteiro";
    Recusar(motivo, motivo_);
    ficheiro_.close();
    return false;
  }
  if (std::memcmp(cabecalho, pakz_campos::kAssinatura, 4) != 0) {
    char b[96];
    std::snprintf(b, sizeof(b), "assinatura \"%c%c%c%c\" em vez de \"PACK\"", cabecalho[0],
                  cabecalho[1], cabecalho[2], cabecalho[3]);
    motivo_ = b;
    Recusar(motivo, motivo_);
    ficheiro_.close();
    return false;
  }
  const std::uint32_t offset_da_tabela = Ler32(cabecalho + 4);
  const std::uint32_t tamanho_da_tabela = Ler32(cabecalho + 8);
  if (tamanho_da_tabela == 0 || tamanho_da_tabela % pakz_campos::kRegisto != 0) {
    motivo_ = "tabela com " + std::to_string(tamanho_da_tabela) +
              " bytes: tem de ser multiplo dos " + std::to_string(pakz_campos::kRegisto) +
              " do registo (e maior que zero)";
    Recusar(motivo, motivo_);
    ficheiro_.close();
    return false;
  }
  const std::uint64_t fim_da_tabela = static_cast<std::uint64_t>(offset_da_tabela) + tamanho_da_tabela;
  if (offset_da_tabela < pakz_campos::kCabecalho || fim_da_tabela > tamanho_do_ficheiro_) {
    motivo_ = "tabela fora do ficheiro";
    Recusar(motivo, motivo_);
    ficheiro_.close();
    return false;
  }

  std::vector<std::uint8_t> tabela(tamanho_da_tabela);
  ficheiro_.seekg(offset_da_tabela, std::ios::beg);
  ficheiro_.read(reinterpret_cast<char*>(tabela.data()), static_cast<std::streamsize>(tabela.size()));
  if (ficheiro_.gcount() != static_cast<std::streamsize>(tabela.size())) {
    motivo_ = "nao consegui ler a tabela inteira";
    Recusar(motivo, motivo_);
    ficheiro_.close();
    return false;
  }
  const std::uint32_t count = tamanho_da_tabela / pakz_campos::kRegisto;
  entradas_.reserve(count);
  std::uint64_t soma = 0;
  for (std::uint32_t i = 0; i < count; ++i) {
    const std::uint8_t* r = &tabela[static_cast<std::size_t>(i) * pakz_campos::kRegisto];
    std::size_t comprimento = 0;
    while (comprimento < pakz_campos::kCampoDoNome && r[comprimento] != 0) ++comprimento;
    if (comprimento == 0 || comprimento == pakz_campos::kCampoDoNome) {
      motivo_ = "nome invalido da entrada " + std::to_string(i);
      Recusar(motivo, motivo_);
      ficheiro_.close();
      return false;
    }
    if (comprimento >= 40u) ++nomes_no_campo_inteiro_;
    std::string nome(reinterpret_cast<const char*>(r), comprimento);
    for (char c : nome) {
      if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7e) {
        motivo_ = "nome da entrada " + std::to_string(i) + " com byte nao imprimivel";
        Recusar(motivo, motivo_);
        ficheiro_.close();
        return false;
      }
    }
    EntradaDoPakz e;
    e.nome = std::move(nome);
    e.offset = Ler32(r + pakz_campos::kOffsetDoOffset);
    e.tamanho_comprimido = Ler32(r + pakz_campos::kOffsetDoTamanho);
    if (e.tamanho_comprimido < pakz_campos::kPropsDoLzma + pakz_campos::kTamanhoDeclaradoDoLzma ||
        static_cast<std::uint64_t>(e.offset) + e.tamanho_comprimido > tamanho_do_ficheiro_ ||
        e.offset != pakz_campos::kCabecalho + soma) {
      motivo_ = "geometria invalida da entrada " + std::to_string(i) + " (\"" + e.nome + "\")";
      Recusar(motivo, motivo_);
      ficheiro_.close();
      return false;
    }
    soma += e.tamanho_comprimido;
    entradas_.push_back(std::move(e));
  }
  if (soma != static_cast<std::uint64_t>(offset_da_tabela) - pakz_campos::kCabecalho) {
    motivo_ = "dados comprimidos nao acabam no inicio da tabela";
    Recusar(motivo, motivo_);
    ficheiro_.close();
    return false;
  }
  for (EntradaDoPakz& e : entradas_) {
    std::uint8_t tamanho_lzma[pakz_campos::kTamanhoDeclaradoDoLzma];
    ficheiro_.seekg(static_cast<std::uint64_t>(e.offset) + pakz_campos::kPropsDoLzma, std::ios::beg);
    ficheiro_.read(reinterpret_cast<char*>(tamanho_lzma), sizeof(tamanho_lzma));
    if (ficheiro_.gcount() != static_cast<std::streamsize>(sizeof(tamanho_lzma))) {
      motivo_ = "nao consegui ler o tamanho LZMA de \"" + e.nome + "\"";
      Recusar(motivo, motivo_);
      ficheiro_.close();
      return false;
    }
    const std::uint64_t declarado = Ler64(tamanho_lzma);
    if (declarado != pakz_campos::kTamanhoDesconhecidoDoLzma) {
      if (declarado > 0xffffffffull) {
        motivo_ = "entrada \"" + e.nome + "\": tamanho descomprimido acima de 4 GiB";
        Recusar(motivo, motivo_);
        ficheiro_.close();
        return false;
      }
      e.tamanho_descomprimido = static_cast<std::uint32_t>(declarado);
    }
  }
  std::vector<std::string> vistos;
  vistos.reserve(entradas_.size());
  for (const EntradaDoPakz& e : entradas_) vistos.push_back(Minusculas(e.nome));
  std::sort(vistos.begin(), vistos.end());
  for (std::size_t i = 1; i < vistos.size(); ++i) {
    if (vistos[i] == vistos[i - 1]) ++nomes_repetidos_;
  }
  ficheiro_.clear();
  ficheiro_.seekg(0, std::ios::beg);
  indexado_de_ficheiro_ = true;
  valido_ = true;
  return true;
}

const EntradaDoPakz* Pakz::Procurar(const std::string& nome) const {
  const std::string alvo = Minusculas(nome);
  for (const EntradaDoPakz& e : entradas_) {
    if (Minusculas(e.nome) == alvo) return &e;
  }
  return nullptr;
}

bool Pakz::Extrair(const EntradaDoPakz& entrada, std::vector<std::uint8_t>* saida,
                   std::string* motivo) const {
  if (motivo != nullptr) motivo->clear();
  if (saida == nullptr) {
    Recusar(motivo, "destino nulo");
    return false;
  }
  saida->clear();
  if (!valido_) {
    Recusar(motivo, "recipiente invalido: " + motivo_);
    return false;
  }
  const std::string prefixo = "entrada \"" + entrada.nome + "\": ";
  std::vector<std::uint8_t> bloco_do_ficheiro;
  const std::uint8_t* stream = nullptr;
  const std::uint64_t fim_da_entrada = static_cast<std::uint64_t>(entrada.offset) + entrada.tamanho_comprimido;
  if (indexado_de_ficheiro_) {
    if (!ficheiro_.is_open() || fim_da_entrada > tamanho_do_ficheiro_) {
      Recusar(motivo, prefixo + "os dados saem do ficheiro indexado");
      return false;
    }
    bloco_do_ficheiro.resize(entrada.tamanho_comprimido);
    ficheiro_.clear();
    ficheiro_.seekg(entrada.offset, std::ios::beg);
    ficheiro_.read(reinterpret_cast<char*>(bloco_do_ficheiro.data()),
                   static_cast<std::streamsize>(bloco_do_ficheiro.size()));
    if (ficheiro_.gcount() != static_cast<std::streamsize>(bloco_do_ficheiro.size())) {
      Recusar(motivo, prefixo + "nao consegui ler o bloco comprimido");
      return false;
    }
    stream = bloco_do_ficheiro.data();
  } else {
    if (fim_da_entrada > bytes_.size()) {
      Recusar(motivo, prefixo + "os dados saem do ficheiro");
      return false;
    }
    stream = &bytes_[entrada.offset];
  }

  // O cabecalho do proprio stream diz o tamanho descomprimido (bytes 5..12).
  // Medido: bate com o que o liblzma produz em 7 486 de 7 486. Pre-alocar esse
  // tamanho e a mesma regra do `teto` do Inflar: sem ele, um registo com o
  // tamanho certo e um corpo mentiroso enchia o hospedeiro.
  const std::uint64_t declarado = Ler64(stream + pakz_campos::kPropsDoLzma);

  lzma_stream strm = LZMA_STREAM_INIT;
  const lzma_ret init = lzma_alone_decoder(&strm, UINT64_MAX);
  if (init != LZMA_OK) {
    Recusar(motivo, prefixo + "lzma_alone_decoder nao arrancou (liblzma " + std::to_string(init) + ")");
    return false;
  }
  strm.next_in = stream;
  strm.avail_in = entrada.tamanho_comprimido;

  std::vector<std::uint8_t> destino;
  if (declarado != pakz_campos::kTamanhoDesconhecidoDoLzma) {
    if (declarado > std::numeric_limits<std::size_t>::max()) {
      lzma_end(&strm);
      Recusar(motivo, prefixo + "declara " + std::to_string(declarado) +
                          " bytes descomprimidos, mais do que um vector desta maquina");
      return false;
    }
    if (declarado > pakz_campos::kTetoSemTamanhoDeclarado) {
      lzma_end(&strm);
      Recusar(motivo, prefixo + "declara " + std::to_string(declarado) +
                          " bytes descomprimidos, acima do teto de " +
                          std::to_string(pakz_campos::kTetoSemTamanhoDeclarado));
      return false;
    }
    destino.resize(static_cast<std::size_t>(declarado));
    strm.next_out = destino.data();
    strm.avail_out = destino.size();
    const lzma_ret ret = lzma_code(&strm, LZMA_FINISH);
    lzma_end(&strm);
    if (ret != LZMA_STREAM_END) {
      Recusar(motivo, prefixo + "o LZMA_ALONE nao chegou ao fim (liblzma " + std::to_string(ret) +
                          ")");
      saida->clear();
      return false;
    }
    if (strm.avail_in != 0) {
      Recusar(motivo, prefixo + "o stream ocupa " +
                          std::to_string(entrada.tamanho_comprimido - strm.avail_in) +
                          " bytes e o registo declara " + std::to_string(entrada.tamanho_comprimido));
      saida->clear();
      return false;
    }
    const std::size_t produzido = destino.size() - strm.avail_out;
    if (static_cast<std::uint64_t>(produzido) != declarado) {
      Recusar(motivo, prefixo + "descomprimiu para " + std::to_string(produzido) +
                          " bytes e o cabecalho do stream declara " + std::to_string(declarado));
      saida->clear();
      return false;
    }
    destino.resize(produzido);
    *saida = std::move(destino);
    return true;
  }

  // Tamanho declarado como desconhecido (0xffffffffffffffff): cresce-se, com
  // um teto declarado -- o maior descomprimido medido no corpus e 1 048 973
  // bytes, e crescer sem teto e deixar o hospedeiro encher-se. E o caminho que
  // os streams sinteticos do teste (gerados pelo liblzma) usam.
  std::size_t produzido = 0;
  std::vector<std::uint8_t> cresce(entrada.tamanho_comprimido * 8u + 4096u);
  lzma_ret ret = LZMA_OK;
  while (ret == LZMA_OK) {
    if (cresce.size() > pakz_campos::kTetoSemTamanhoDeclarado) {
      lzma_end(&strm);
      Recusar(motivo, prefixo + "sem tamanho declarado no stream, ultrapassou o teto de " +
                          std::to_string(pakz_campos::kTetoSemTamanhoDeclarado) + " bytes");
      saida->clear();
      return false;
    }
    strm.next_out = cresce.data() + produzido;
    strm.avail_out = cresce.size() - produzido;
    ret = lzma_code(&strm, LZMA_FINISH);
    produzido = cresce.size() - strm.avail_out;
    if (ret == LZMA_STREAM_END) break;
    if (ret != LZMA_OK) {
      lzma_end(&strm);
      Recusar(motivo, prefixo + "o LZMA_ALONE nao chegou ao fim (liblzma " + std::to_string(ret) +
                          ")");
      saida->clear();
      return false;
    }
    if (strm.avail_out == 0) cresce.resize(cresce.size() * 2u);
  }
  lzma_end(&strm);
  if (strm.avail_in != 0) {
    Recusar(motivo, prefixo + "o stream ocupa " +
                        std::to_string(entrada.tamanho_comprimido - strm.avail_in) +
                        " bytes e o registo declara " + std::to_string(entrada.tamanho_comprimido));
    saida->clear();
    return false;
  }
  cresce.resize(produzido);
  *saida = std::move(cresce);
  return true;
}

bool Pakz::Extrair(std::size_t indice, std::vector<std::uint8_t>* saida, std::string* motivo) const {
  if (indice >= entradas_.size()) {
    Recusar(motivo, "entrada " + std::to_string(indice) + " nao existe: o recipiente tem " +
                        std::to_string(entradas_.size()));
    return false;
  }
  return Extrair(entradas_[indice], saida, motivo);
}

bool Pakz::ExtrairPorNome(const std::string& nome, std::vector<std::uint8_t>* saida,
                          std::string* motivo) const {
  const EntradaDoPakz* e = Procurar(nome);
  if (e == nullptr) {
    Recusar(motivo, "o recipiente nao tem entrada chamada \"" + nome + "\"");
    return false;
  }
  return Extrair(*e, saida, motivo);
}

}  // namespace zb2
