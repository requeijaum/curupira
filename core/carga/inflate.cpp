#include "core/carga/inflate.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace zb2 {
namespace {

// ---------------------------------------------------------------------------
// Adler32 (RFC1950, secao 9)
// ---------------------------------------------------------------------------
// `65521` e o maior primo abaixo de 65536, e e o modulo do RFC. A soma corre em
// blocos de 5552 bytes porque e o maior `n` para o qual `255*n*(n+1)/2 +
// (n+1)*65520` cabe num `uint32` -- acima disso o resto teria de ser tirado a
// cada byte, e um laço a mais por byte em 87 MB de ROM medida pesa.
constexpr std::uint32_t kAdlerBase = 65521u;
constexpr std::size_t kAdlerBloco = 5552u;

void Recusar(std::string* motivo, const std::string& porque) {
  if (motivo != nullptr) *motivo = porque;
}

std::string Dez(std::size_t v) { return std::to_string(v); }

// ---------------------------------------------------------------------------
// O leitor de bits do deflate (RFC1951, 3.1.1)
// ---------------------------------------------------------------------------
// Os campos do deflate empacotam-se a partir do BIT MENOS significativo do
// primeiro byte. Ler bytes e testar mascaras a mao, espalhado pelo codigo, e a
// forma mais segura de um descompressor ficar com um deslocamento errado num
// unico sitio -- aqui ha um so sitio, e todos os campos passam por ele.
class Bits {
 public:
  Bits(const std::uint8_t* dados, std::size_t n) : d_(dados), n_(n) {}

  // Le `quantos` bits (<= 16). Falso quando o stream acabou: e assim que "o
  // ficheiro acabou a meio" deixa de ser um indice fora do buffer.
  bool Ler(unsigned quantos, std::uint32_t* saida) {
    if (quantos == 0) {
      *saida = 0;
      return true;
    }
    while (bits_ < quantos) {
      if (pos_ >= n_) return false;
      buf_ |= static_cast<std::uint64_t>(d_[pos_]) << bits_;
      ++pos_;
      bits_ += 8;
    }
    *saida = static_cast<std::uint32_t>(buf_ & ((1ull << quantos) - 1u));
    buf_ >>= quantos;
    bits_ -= quantos;
    return true;
  }

  // Descarta os bits que sobram do ultimo simbolo ate ao proximo byte. O bloco
  // `stored` e o adler32 comecam em fronteira de byte.
  void AlinharAoByte() {
    // DESCARTAR `(8 - p%8) % 8` E NAO `p%8`: os bits que faltam ate ao proximo
    // byte. A primeira versao deste codigo descartava `p%8`, o que avanca a
    // posicao para o sitio ERRADO -- e o sintoma era o adler32 lido um byte
    // antes (o stream "nao batia" num ficheiro perfeitamente bom). Os dois
    // numeros so coincidem quando `p%8` e zero ou quatro.
    unsigned descartar = (8u - static_cast<unsigned>(PosicaoEmBits() % 8u)) % 8u;
    while (bits_ < descartar) {
      // O stream pode ter acabado exactamente aqui: nesse caso nao ha mais nada
      // a alinhar, e quem chama vai olhar para o consumo e recusar.
      if (pos_ >= n_) return;
      buf_ |= static_cast<std::uint64_t>(d_[pos_]) << bits_;
      ++pos_;
      bits_ += 8;
    }
    buf_ >>= descartar;
    bits_ -= descartar;
  }

  // Quantos bytes da entrada ja foram consumidos, arredondado ao byte.
  std::size_t BytesConsumidos() const { return PosicaoEmBits() / 8u; }

 private:
  std::size_t PosicaoEmBits() const { return pos_ * 8u - bits_; }

  const std::uint8_t* d_ = nullptr;
  std::size_t n_ = 0;
  std::size_t pos_ = 0;      // bytes ja lidos para o buffer
  std::uint64_t buf_ = 0;    // bits ainda por consumir (os baixos sao os proximos)
  unsigned bits_ = 0;
};

// ---------------------------------------------------------------------------
// Codigos de Huffman (RFC1951, 3.2.2)
// ---------------------------------------------------------------------------
constexpr unsigned kMaxBits = 15;
constexpr std::size_t kMaxSimbolos = 288;  // o maior codigo: os literais/comprimentos

class Codigos {
 public:
  // Constroi o codigo canonico a partir dos comprimentos. `incompleta_ok` aceita
  // um codigo incompleto -- e o caso legitimo de um bloco com UM unico simbolo
  // (o RFC1951 3.2.7 permite-o, e um descompressor real produz-lo).
  bool Construir(const std::uint8_t* comprimentos, std::size_t quantos, bool incompleta_ok,
                 std::string* motivo) {
    contagem_.fill(0);
    quantos_ = quantos;
    maior_ = 0;
    for (std::size_t i = 0; i < quantos; ++i) {
      const unsigned c = comprimentos[i];
      if (c > kMaxBits) {
        Recusar(motivo, "comprimento de codigo " + Dez(c) + " no simbolo " + Dez(i) +
                            ": o deflate so vai ate 15 bits");
        return false;
      }
      ++contagem_[c];
      if (c > maior_) maior_ = c;
    }
    if (contagem_[0] == quantos) return true;  // arvore vazia: nenhum codigo

    // Sobre-subscricao e uma recusa; incompleta so onde o RFC o permite.
    int restante = 1;
    for (unsigned len = 1; len <= kMaxBits; ++len) {
      restante <<= 1;
      restante -= static_cast<int>(contagem_[len]);
      if (restante < 0) {
        Recusar(motivo, "codigo de Huffman sobre-subscrito: sobram " + Dez(static_cast<std::size_t>(-restante)) +
                            " codigos no comprimento " + Dez(len));
        return false;
      }
    }
    if (restante > 0 && !incompleta_ok) {
      Recusar(motivo, "codigo de Huffman incompleto: faltam " + Dez(static_cast<std::size_t>(restante)) +
                          " codigos (maior comprimento = " + Dez(maior_) + ")");
      return false;
    }

    // Ordem canonica: primeiro por comprimento, depois por simbolo.
    std::array<std::uint16_t, kMaxBits + 2> deslocamento{};
    for (unsigned len = 1; len <= kMaxBits; ++len) {
      deslocamento[len + 1] = static_cast<std::uint16_t>(deslocamento[len] + contagem_[len]);
    }
    for (std::size_t s = 0; s < quantos; ++s) {
      if (comprimentos[s] != 0) simbolo_[deslocamento[comprimentos[s]]++] = static_cast<std::uint16_t>(s);
    }
    return true;
  }

  bool Vazia() const { return contagem_[0] == quantos_; }

  // Descodifica um simbolo. Percorre os comprimentos como o `puff` do zlib: uma
  // tabela de consulta directa seria mais rapida e mais dificil de conferir a
  // mao -- e este descompressor existe para ser conferido (P1).
  bool Descodificar(Bits& b, std::uint32_t* simbolo, std::string* motivo) const {
    std::uint32_t codigo = 0;
    std::uint32_t primeiro = 0;
    std::uint32_t indice = 0;
    for (unsigned len = 1; len <= kMaxBits; ++len) {
      std::uint32_t bit = 0;
      if (!b.Ler(1, &bit)) {
        Recusar(motivo, "o stream acabou dentro de um codigo de Huffman");
        return false;
      }
      codigo |= bit;
      const std::uint32_t cont = contagem_[len];
      if (codigo - primeiro < cont) {
        *simbolo = simbolo_[indice + (codigo - primeiro)];
        return true;
      }
      indice += cont;
      primeiro = (primeiro + cont) << 1;
      codigo <<= 1;
    }
    Recusar(motivo, "codigo de Huffman com mais de 15 bits (codigo que a arvore nao declara)");
    return false;
  }

 private:
  std::array<std::uint16_t, kMaxBits + 1> contagem_{};
  std::array<std::uint16_t, kMaxSimbolos> simbolo_{};
  std::size_t quantos_ = 0;
  unsigned maior_ = 0;
};

bool SoCodigosCurtos(const std::uint8_t* comprimentos, std::size_t quantos) {
  for (std::size_t i = 0; i < quantos; ++i) {
    if (comprimentos[i] > 1) return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// As tabelas do RFC1951, 3.2.5
// ---------------------------------------------------------------------------
struct BaseEExtra {
  std::uint16_t base;
  std::uint8_t extra;
};

// Codigo 257..285 -> comprimento. O 285 (258 bytes) e o unico com zero bits
// extra no fim da tabela, e e o que um codificador usa para copias longas.
constexpr BaseEExtra kComprimentos[29] = {
    {3, 0},   {4, 0},   {5, 0},   {6, 0},   {7, 0},   {8, 0},   {9, 0},   {10, 0},
    {11, 1},  {13, 1},  {15, 1},  {17, 1},  {19, 2},  {23, 2},  {27, 2},  {31, 2},
    {35, 3},  {43, 3},  {51, 3},  {59, 3},  {67, 4},  {83, 4},  {99, 4},  {115, 4},
    {131, 5}, {163, 5}, {195, 5}, {227, 5}, {258, 0}};

// Codigo 0..29 -> distancia. O RFC1951 3.2.6 declara 32 codigos de 5 bits na
// arvore fixa das distancias, e diz que os valores 30 e 31 "nunca ocorrem": eles
// existem na ARVORE (para o codigo ser completo) e usar um deles e recusa.
constexpr BaseEExtra kDistancias[30] = {
    {1, 0},     {2, 0},     {3, 0},     {4, 0},     {5, 1},     {7, 1},     {9, 2},     {13, 2},
    {17, 3},    {25, 3},    {33, 4},    {49, 4},    {65, 5},    {97, 5},    {129, 6},   {193, 6},
    {257, 7},   {385, 7},   {513, 8},   {769, 8},   {1025, 9},  {1537, 9},  {2049, 10}, {3073, 10},
    {4097, 11}, {6145, 11}, {8193, 12}, {12289, 12}, {16385, 13}, {24577, 13}};

constexpr std::size_t kCodigosDeDistancia = 30;
constexpr std::size_t kCodigosDeDistanciaNaArvore = 32;

// ---------------------------------------------------------------------------
// O laco dos simbolos: e o mesmo para o bloco fixo e para o dinamico
// ---------------------------------------------------------------------------
bool DescomprimirBloco(Bits& b, const Codigos& literais, const Codigos& distancias,
                       std::vector<std::uint8_t>* saida, std::uint32_t teto, std::string* motivo) {
  for (;;) {
    std::uint32_t simbolo = 0;
    if (!literais.Descodificar(b, &simbolo, motivo)) return false;
    if (simbolo < 256) {
      if (teto != 0 && saida->size() >= teto) {
        Recusar(motivo, "a saida passaria do teto declarado de " + Dez(teto) + " bytes");
        return false;
      }
      saida->push_back(static_cast<std::uint8_t>(simbolo));
      continue;
    }
    if (simbolo == 256) return true;  // fim do bloco
    if (simbolo > 285) {
      Recusar(motivo, "codigo de comprimento invalido: " + Dez(simbolo) + " (o deflate declara 257..285)");
      return false;
    }
    const BaseEExtra& c = kComprimentos[simbolo - 257];
    std::uint32_t extra = 0;
    if (c.extra != 0 && !b.Ler(c.extra, &extra)) {
      Recusar(motivo, "o stream acabou nos bits extra do comprimento (codigo " + Dez(simbolo) + ")");
      return false;
    }
    const std::size_t comprimento = static_cast<std::size_t>(c.base) + extra;

    if (distancias.Vazia()) {
      Recusar(motivo,
              "o bloco usa uma copia mas a arvore das distancias nao declara codigo nenhum "
              "(comprimento pedido = " + Dez(comprimento) + ")");
      return false;
    }
    std::uint32_t codigo_da_distancia = 0;
    if (!distancias.Descodificar(b, &codigo_da_distancia, motivo)) return false;
    if (codigo_da_distancia >= kCodigosDeDistancia) {
      Recusar(motivo, "codigo de distancia " + Dez(codigo_da_distancia) +
                          ": o RFC1951 3.2.6 diz que 30 e 31 nunca ocorrem");
      return false;
    }
    const BaseEExtra& d = kDistancias[codigo_da_distancia];
    std::uint32_t extra_da_distancia = 0;
    if (d.extra != 0 && !b.Ler(d.extra, &extra_da_distancia)) {
      Recusar(motivo, "o stream acabou nos bits extra da distancia (codigo " + Dez(codigo_da_distancia) + ")");
      return false;
    }
    const std::size_t distancia = static_cast<std::size_t>(d.base) + extra_da_distancia;
    if (distancia > saida->size()) {
      Recusar(motivo, "distancia de " + Dez(distancia) + " bytes com apenas " + Dez(saida->size()) +
                          " bytes ja descomprimidos");
      return false;
    }
    if (teto != 0 && saida->size() + comprimento > teto) {
      Recusar(motivo, "a saida passaria do teto declarado de " + Dez(teto) + " bytes");
      return false;
    }
    const std::size_t inicio = saida->size() - distancia;
    // Byte a byte, e nao com uma copia de bloco: a fonte e o proprio destino (uma
    // copia pode sobrepor-se a si mesma, e e assim que o deflate escreve
    // repeticao). Um `memcpy` aqui produziria dados errados em silencio.
    for (std::size_t i = 0; i < comprimento; ++i) {
      const std::uint8_t byte = (*saida)[inicio + i];
      saida->push_back(byte);
    }
  }
}

bool ArvoresFixas(Codigos* literais, Codigos* distancias, std::string* motivo) {
  // RFC1951 3.2.6.
  std::array<std::uint8_t, 288> comprimentos{};
  std::size_t i = 0;
  for (; i < 144; ++i) comprimentos[i] = 8;
  for (; i < 256; ++i) comprimentos[i] = 9;
  for (; i < 280; ++i) comprimentos[i] = 7;
  for (; i < 288; ++i) comprimentos[i] = 8;
  if (!literais->Construir(comprimentos.data(), comprimentos.size(), false, motivo)) return false;

  std::array<std::uint8_t, kCodigosDeDistanciaNaArvore> distancias_fixas{};
  distancias_fixas.fill(5);
  return distancias->Construir(distancias_fixas.data(), distancias_fixas.size(), false, motivo);
}

bool ArvoresDinamicas(Bits& b, Codigos* literais, Codigos* distancias, std::string* motivo) {
  // RFC1951 3.2.7.
  std::uint32_t hlit = 0, hdist = 0, hclen = 0;
  if (!b.Ler(5, &hlit) || !b.Ler(5, &hdist) || !b.Ler(4, &hclen)) {
    Recusar(motivo, "o stream acabou no cabecalho das arvores dinamicas (HLIT/HDIST/HCLEN)");
    return false;
  }
  const std::size_t n_literais = hlit + 257;
  const std::size_t n_distancias = hdist + 1;

  // A ORDEM e a do RFC1951, e nao a numerica: os comprimentos dos codigos
  // 0..18 escrevem-se pela ordem 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3,
  // 13, 2, 14, 1, 15. Trocar dois destes numeros desloca a arvore inteira e o
  // resultado ainda "descomprime" -- com o texto errado.
  constexpr unsigned kOrdem[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
  std::array<std::uint8_t, 19> comprimento_do_comprimento{};
  for (std::uint32_t i = 0; i < hclen + 4; ++i) {
    std::uint32_t v = 0;
    if (!b.Ler(3, &v)) {
      Recusar(motivo, "o stream acabou na tabela dos comprimentos de codigo (indice " + Dez(i) + ")");
      return false;
    }
    comprimento_do_comprimento[kOrdem[i]] = static_cast<std::uint8_t>(v);
  }
  Codigos arvore_dos_comprimentos;
  if (!arvore_dos_comprimentos.Construir(comprimento_do_comprimento.data(), comprimento_do_comprimento.size(),
                                         false, motivo)) {
    return false;
  }

  std::vector<std::uint8_t> comprimentos(n_literais + n_distancias, 0);
  std::size_t i = 0;
  while (i < comprimentos.size()) {
    std::uint32_t simbolo = 0;
    if (!arvore_dos_comprimentos.Descodificar(b, &simbolo, motivo)) return false;
    if (simbolo < 16) {
      comprimentos[i++] = static_cast<std::uint8_t>(simbolo);
      continue;
    }
    std::uint32_t repetir = 0;
    std::uint8_t valor = 0;
    if (simbolo == 16) {
      if (i == 0) {
        Recusar(motivo, "o codigo 16 repete o comprimento anterior, e nao ha anterior");
        return false;
      }
      if (!b.Ler(2, &repetir)) {
        Recusar(motivo, "o stream acabou nos bits de repeticao do codigo 16");
        return false;
      }
      repetir += 3;
      valor = comprimentos[i - 1];
    } else if (simbolo == 17) {
      if (!b.Ler(3, &repetir)) {
        Recusar(motivo, "o stream acabou nos bits de repeticao do codigo 17");
        return false;
      }
      repetir += 3;
    } else if (simbolo == 18) {
      if (!b.Ler(7, &repetir)) {
        Recusar(motivo, "o stream acabou nos bits de repeticao do codigo 18");
        return false;
      }
      repetir += 11;
    } else {
      Recusar(motivo, "simbolo " + Dez(simbolo) + " nao existe na arvore dos comprimentos de codigo");
      return false;
    }
    if (i + repetir > comprimentos.size()) {
      Recusar(motivo, "a repeticao do codigo " + Dez(simbolo) + " escreveria " + Dez(repetir) +
                          " comprimentos, e so faltam " + Dez(comprimentos.size() - i));
      return false;
    }
    for (std::uint32_t r = 0; r < repetir; ++r) comprimentos[i++] = valor;
  }

  // Sem o codigo 256 nao ha fim de bloco declarado: o bloco so acabaria por
  // recusa, e mais vale dize-lo aqui, com o numero.
  if (comprimentos[256] == 0) {
    Recusar(motivo, "a arvore dos literais nao declara o codigo 256 (fim de bloco)");
    return false;
  }
  if (!literais->Construir(comprimentos.data(), n_literais, SoCodigosCurtos(comprimentos.data(), n_literais),
                           motivo)) {
    return false;
  }
  // A arvore das distancias pode ficar VAZIA (nenhum codigo) num bloco que nao
  // tem nenhuma copia -- o RFC1951 3.2.7 permite-o, e a recusa fica para o
  // momento em que uma copia a pedir (com o comprimento no motivo).
  const std::uint8_t* cd = comprimentos.data() + n_literais;
  return distancias->Construir(cd, n_distancias, SoCodigosCurtos(cd, n_distancias), motivo);
}

}  // namespace

std::uint32_t Adler32(const std::uint8_t* dados, std::size_t n, std::uint32_t semente) {
  std::uint32_t a = semente & 0xffffu;
  std::uint32_t b = (semente >> 16) & 0xffffu;
  std::size_t i = 0;
  while (i < n) {
    const std::size_t fim = std::min(i + kAdlerBloco, n);
    for (; i < fim; ++i) {
      a += dados[i];
      b += a;
    }
    a %= kAdlerBase;
    b %= kAdlerBase;
  }
  return (b << 16) | a;
}

bool Inflar(const std::uint8_t* entrada, std::size_t tamanho, std::vector<std::uint8_t>* saida,
            std::string* motivo, std::uint32_t teto, std::size_t* consumido) {
  if (motivo != nullptr) motivo->clear();
  if (consumido != nullptr) *consumido = 0;
  if (saida == nullptr) {
    Recusar(motivo, "saida nula");
    return false;
  }
  saida->clear();
  if (entrada == nullptr) {
    Recusar(motivo, "entrada nula");
    return false;
  }

  // --- o cabecalho zlib (RFC1950, secao 2.2) ---
  if (tamanho < 2) {
    Recusar(motivo, "stream zlib com " + Dez(tamanho) + " bytes: o cabecalho do RFC1950 tem 2");
    return false;
  }
  const std::uint8_t cmf = entrada[0];
  const std::uint8_t flg = entrada[1];
  if ((cmf & 0x0fu) != 8u) {
    char b[64];
    std::snprintf(b, sizeof(b), "CM=%u: o unico metodo do RFC1950 e 8 (deflate)", cmf & 0x0fu);
    Recusar(motivo, b);
    return false;
  }
  if ((cmf >> 4) > 7u) {
    char b[80];
    std::snprintf(b, sizeof(b), "CINFO=%u: janela maior do que os 32 KiB do RFC1950", cmf >> 4);
    Recusar(motivo, b);
    return false;
  }
  if (((static_cast<unsigned>(cmf) << 8) | flg) % 31u != 0u) {
    char b[96];
    std::snprintf(b, sizeof(b), "CMF=0x%02x FLG=0x%02x: (CMF<<8|FLG) nao e multiplo de 31", cmf, flg);
    Recusar(motivo, b);
    return false;
  }
  if ((flg & 0x20u) != 0u) {
    Recusar(motivo, "FDICT ligado: dicionario predefinido, que nenhum ficheiro medido usa");
    return false;
  }

  // O corpo comeca depois do cabecalho de 2 bytes: o deslocamento do adler32 e
  // contado a partir do INICIO do stream, e nao do corpo.
  const std::uint8_t* corpo = entrada + 2;
  const std::size_t n_corpo = tamanho - 2;
  Bits bits(corpo, n_corpo);

  bool ultimo = false;
  while (!ultimo) {
    std::uint32_t b_final = 0, tipo = 0;
    if (!bits.Ler(1, &b_final) || !bits.Ler(2, &tipo)) {
      Recusar(motivo, "o stream acabou antes do bloco final (BFINAL/BTYPE)");
      return false;
    }
    ultimo = b_final != 0;

    if (tipo == 0) {
      // Bloco stored: LEN, o complemento NLEN, e os bytes em claro.
      bits.AlinharAoByte();
      std::uint32_t len = 0, nlen = 0;
      if (!bits.Ler(16, &len) || !bits.Ler(16, &nlen)) {
        Recusar(motivo, "bloco stored truncado: faltam LEN/NLEN");
        return false;
      }
      if ((len ^ 0xffffu) != nlen) {
        char b[128];
        std::snprintf(b, sizeof(b), "bloco stored com LEN=%u e NLEN=%u, que nao sao complementares", len, nlen);
        Recusar(motivo, b);
        return false;
      }
      if (teto != 0 && saida->size() + len > teto) {
        Recusar(motivo, "a saida passaria do teto declarado de " + Dez(teto) + " bytes");
        return false;
      }
      for (std::uint32_t i = 0; i < len; ++i) {
        std::uint32_t byte = 0;
        if (!bits.Ler(8, &byte)) {
          Recusar(motivo, "bloco stored truncado: faltam " + Dez(len - i) + " bytes dos " + Dez(len) +
                              " declarados em LEN");
          return false;
        }
        saida->push_back(static_cast<std::uint8_t>(byte));
      }
      continue;
    }

    if (tipo == 3) {
      Recusar(motivo, "BTYPE=3 reservado pelo RFC1951 (os valores definidos sao 0, 1 e 2)");
      return false;
    }

    Codigos literais, distancias;
    if (tipo == 1) {
      if (!ArvoresFixas(&literais, &distancias, motivo)) return false;
    } else {
      if (!ArvoresDinamicas(bits, &literais, &distancias, motivo)) return false;
    }
    if (!DescomprimirBloco(bits, literais, distancias, saida, teto, motivo)) return false;
  }

  // --- o adler32 (RFC1950, secao 2.2) ---
  bits.AlinharAoByte();
  const std::size_t fim = 2 + bits.BytesConsumidos();
  if (fim + 4 > tamanho) {
    Recusar(motivo, "faltam os 4 bytes do adler32: o stream tem " + Dez(tamanho) + " bytes e os blocos acabam em " +
                        Dez(fim));
    return false;
  }
  // O ADLER32 E O UNICO CAMPO DO STREAM QUE VAI EM BIG-ENDIAN (RFC1950, 2.2:
  // "the Adler-32 checksum ... MSB first"); tudo o resto no deflate e LSB
  // primeiro. Ler estes quatro bytes como os outros tres campos do cabecalho da
  // exatamente o valor com os bytes ao contrario -- e foi assim que este codigo
  // recusou os 9 ficheiros reais, todos bons, na primeira corrida.
  const std::uint32_t no_stream = (static_cast<std::uint32_t>(entrada[fim]) << 24) |
                                 (static_cast<std::uint32_t>(entrada[fim + 1]) << 16) |
                                 (static_cast<std::uint32_t>(entrada[fim + 2]) << 8) |
                                 static_cast<std::uint32_t>(entrada[fim + 3]);
  const std::uint32_t calculado =
      Adler32(saida->data(), saida->size());
  if (no_stream != calculado) {
    char b[160];
    std::snprintf(b, sizeof(b),
                  "adler32 do stream = 0x%08x, calculado sobre os %zu bytes descomprimidos = 0x%08x",
                  no_stream, saida->size(), calculado);
    Recusar(motivo, b);
    return false;
  }
  if (consumido != nullptr) *consumido = fim + 4;
  return true;
}

}  // namespace zb2
