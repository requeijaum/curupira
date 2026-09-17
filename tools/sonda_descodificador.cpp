// SONDA DO DESCODIFICADOR -- a metade C++ do auditor diferencial.
//
// O QUE ESTA FERRAMENTA FAZ, e o que ela NAO faz:
//
//   Le um ficheiro binario, trata-o como imagem de codigo, e para CADA palavra
//   alinhada pergunta ao interpretador `core/cpu/arm_interpreter.cpp` QUE
//   INSTRUCAO E AQUELA -- executando-a uma vez, num estado zerado, e lendo o
//   nome que o RAMO QUE CORREU escreveu (`FamiliaDaUltima`, `MotivoDaRecusa`).
//
//   Ela NAO decide se o resultado esta certo. Quem decide e o `objdump`, e quem
//   compara e o `tools/auditar_descodificador.py`. A divisao e de proposito: se
//   esta sonda tivesse a sua propria ideia de ARM, teriamos duas
//   descodificacoes a concordar consigo mesmas -- que e exactamente o defeito
//   que ela existe para encontrar (o `ldrd` do `a3d.mod`, lido como `BIC`
//   durante centenas de commits, em silencio).
//
// PORQUE A EXECUCAO E SEGURA: cada palavra e executada exactamente UMA vez
// (`Passo`, nunca `Correr`), com os 16 registradores zerados e a memoria a
// devolver zero nas paginas que nunca foram escritas. Nao ha caminho para o
// `SWI` chamar o HLE (o `SWI` sem tratador RECUSA, `arm_interpreter.cpp`), e o
// PC de saida nao e consultado porque `Passo()` nao olha para a faixa de saida.
// As paginas que as escritas alocam sao largadas periodicamente para a memoria
// nao crescer sem limite.
//
// A CONDICAO E SATISFEITA DE PROPOSITO. Metade das instrucoes tem condicao
// diferente de `AL`, e com as bandeiras a zero o interpretador reportaria
// `condicao_falsa` para todas elas -- o auditor veria metade do codigo. As
// bandeiras N/Z/C/V sao escolhidas, por condicao, para a instrucao CORRER.
//
// Uso:
//   zb2_sonda_descodificador <ficheiro> [--thumb] [--base=0x0] [--bin=saida.bin]
//                                      [--inicio=N] [--fim=N] [--verboso] [--resumo]
// Saida:
//   stdout: as linhas `#classe <id> <nome>` e `#palavras <n>`
//   --bin : um byte por palavra, o id da classe daquela palavra (ordem de
//           endereco, para alinhar com as linhas do objdump)
// Codigos de saida: 0 feito | 2 uso | 3 ficheiro ilegivel | 4 tabela de classes
//           a transbordar (o auditor nao pode comparar com ids truncados).
//
// MEDICAO QUE MOTIVOU A FERRAMENTA, no proprio ledger: o grupo "extra
// load/store" (LDRH/STRH/LDRSB/LDRSH/LDRD/STRD) nao tinha ramo no
// interpretador; os bits 27-25 dessas instrucoes sao 000, os mesmos do grupo de
// dados processados, e cada `ldrd r8, sb, [sp, #0x20]` corria como
// `BIC r8, sp, r0, LSR r2`. 21 dos 62 titulos saltavam para a PILHA.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/cpu/arm_interpreter.h"

namespace {

// Bandeiras que tornam a condicao VERDADEIRA. Um caso por condicao, e nao uma
// formula: a tabela de condicoes do ARM e o sitio onde um erro fica invisivel
// (a condicao LE e `Z=1 ou N!=V`).
std::uint32_t FlagsParaCondicao(std::uint32_t cond) {
  using C = zb2::Cpsr;
  switch (cond) {
    case 0x0: return C::kZ;                        // EQ
    case 0x1: return 0;                            // NE
    case 0x2: return C::kC;                        // CS
    case 0x3: return 0;                            // CC
    case 0x4: return C::kN;                        // MI
    case 0x5: return 0;                            // PL
    case 0x6: return C::kV;                        // VS
    case 0x7: return 0;                            // VC
    case 0x8: return C::kC;                        // HI: C=1, Z=0
    case 0x9: return 0;                            // LS: C=0
    case 0xA: return 0;                            // GE: N==V (0==0)
    case 0xB: return C::kN;                        // LT: N!=V
    case 0xC: return 0;                            // GT: Z=0, N==V
    case 0xD: return C::kZ;                        // LE: Z=1
    default: return 0;                             // AL e NV
  }
}

struct Opcoes {
  std::string ficheiro;
  std::string bin;
  std::string pcs;
  bool efeito = false;
  bool thumb = false;
  bool verboso = false;
  bool resumo = false;
  std::uint32_t base = 0;
  std::uint64_t inicio = 0;
  std::uint64_t fim = UINT64_MAX;
};

// A tabela de classes: um id por par (familia, motivo). Uma so tabela, e nao
// duas, porque a recusa e uma CLASSIFICACAO como as outras: o que o auditor
// pergunta ao objdump e "o que era esta instrucao", e a resposta "era um `ldrh`
// que este interpretador recusa por Rn = PC" e uma resposta.
class TabelaDeClasses {
 public:
  int Id(const std::string& nome) {
    auto it = ids_.find(nome);
    if (it != ids_.end()) return it->second;
    if (nomes_.size() >= 255) return -1;
    const int id = static_cast<int>(nomes_.size());
    nomes_.push_back(nome);
    ids_[nome] = id;
    return id;
  }
  const std::vector<std::string>& Nomes() const { return nomes_; }
  bool Transbordou() const { return transbordou_; }
  void MarcarTransbordo() { transbordou_ = true; }

 private:
  std::unordered_map<std::string, int> ids_;
  std::vector<std::string> nomes_;
  bool transbordou_ = false;
};


// ---------------------------------------------------------------------------
// O MODO DE EFEITO (`--efeito`): o auditor passa a comparar VALORES.
// ---------------------------------------------------------------------------
//
// PORQUE EXISTE. O modo de cima compara NOMES: o `bl` de 32 bits do Thumb com os
// campos trocados chama-se `bl` nos dois lados e faz o guest saltar para o meio
// de outra funcao; o `ldr r0,[sp,#4]` do Thumb 9xxx chamava-se `ldr` nos dois
// lados e lia o registador errado. Centenas de commits passaram por cima disso.
// Um nome NAO e um efeito.
//
// COMO SE LE O EFEITO, sem uma segunda descodificacao aqui: a instrucao e
// executada UMA vez numa CAIXA DE AREIA (um endereco proprio, com todo o espaco
// em volta preenchido por um PADRAO invertivel), e o que se le de volta e o
// estado: PC depois, registadores mudados, palavras de memoria mudadas.
//
//   - o PADRAO e `valor(a) = a * 0x9E3779B1 ^ 0xA5A5A5A5`, uma BIJECCAO: o valor
//     que um `ldr` deixa no registador diz, sem ambiguidade, DE QUE ENDERECO ele
//     leu. E assim que o endereco de um carregamento PC-relativo se compara em
//     NUMEROS, e nao por "o nome bate".
//   - a JANELA VIGIADA e lida de volta depois de cada passo e o que mudou sai no
//     relatorio: e assim que a LISTA de um `stm`/`push` -- que nao escreve
//     registador nenhum -- se compara.
//   - os sentinelas dos registadores sao ENDERECOS dentro da janela (0x8800+4i),
//     e os seus oito bits de baixo dao as contagens de deslocamento 0, 4, ... 48
//     quando o registador e usado como quantidade (0 e 32 incluidos).
//
// A EXECUCAO E NA CAIXA, e nao no endereco do ficheiro, porque o padrao precisa
// de rodear a instrucao. O endereco REAL de cada palavra vai no relatorio (e o
// denominador continua a ser o ficheiro), e o que se compara e o efeito relativo
// a `pc`.
constexpr std::uint32_t kCaixa = 0x00008000u;
constexpr std::uint32_t kPadraoInicio = 0x00000000u;
constexpr std::uint32_t kPadraoFim = 0x00010000u;
// A JANELA E A REGIAO DO PADRAO INTEIRA. Uma janela estreita deixava de fora
// as escritas cujo endereco vem da ARITMETICA dos sentinelas (medido:
// `str r0,[r0,r0]` do formato 5 do Thumb escreve em 0x11000, fora de 0x6000-0xA000)
// -- e um deposito que nao se ve nao se compara.
constexpr std::uint32_t kVigiaInicio = 0x00000000u;
constexpr std::uint32_t kVigiaFim = 0x00010000u;
constexpr std::uint32_t kPadraoM = 0x9E3779B1u;
constexpr std::uint32_t kPadraoX = 0xA5A5A5A5u;
// Os sentinelas vivem BAIXOS, para que a aritmetica entre eles (somas, indices
// de registador) fique toda dentro da janela.
constexpr std::uint32_t kSentinelas = 0x00002000u;
constexpr std::uint32_t kSPDaCaixa = 0x00002400u;
constexpr std::uint32_t kLRDaCaixa = 0x00002440u;

inline std::uint32_t ValorDoPadrao(std::uint32_t a) { return (a * kPadraoM) ^ kPadraoX; }
inline std::uint32_t SentinelaDoRegistador(int r) {
  return kSentinelas + 4u * static_cast<std::uint32_t>(r);
}

// Le a lista de enderecos (um por linha, hexadecimal ou decimal) que o modo
// `--pcs` audita. A lista vem do que o CORPUS EXECUTA, e nao do espaco inteiro.
std::vector<std::uint64_t> LerEnderecos(const std::string& caminho, std::size_t tamanho) {
  std::vector<std::uint64_t> saida;
  std::ifstream f(caminho);
  if (!f) {
    std::fprintf(stderr, "nao abri a lista de enderecos %s\n", caminho.c_str());
    return saida;
  }
  std::string linha;
  while (std::getline(f, linha)) {
    if (linha.empty() || linha[0] == '#') continue;
    const char* c = linha.c_str();
    char* fim = nullptr;
    const unsigned long v = std::strtoul(c, &fim, 0);
    if (fim == c) continue;
    saida.push_back(static_cast<std::uint64_t>(v) / tamanho);
  }
  return saida;
}

int CorrerEfeito(const Opcoes& op, const std::vector<std::uint8_t>& dados) {
  const std::size_t tamanho = op.thumb ? 2 : 4;
  const std::uint64_t palavras = dados.size() / tamanho;
  std::unique_ptr<zb2::Memoria> memoria(new zb2::Memoria(nullptr));
  memoria->EscritorUnico("sonda_descodificador_efeito");
  std::unique_ptr<zb2::ArmInterpreter> cpu(new zb2::ArmInterpreter(*memoria, nullptr));

  // O PADRAO EM VOLTA DA CAIXA. E escrito uma so vez: cada passo que escreve em
  // memoria e desfeito (a janela e reposta a partir da linha de base).
  std::vector<std::uint8_t> padrao(kPadraoFim - kPadraoInicio);
  for (std::uint32_t a = kPadraoInicio; a < kPadraoFim; a += 4) {
    const std::uint32_t v = ValorDoPadrao(a);
    const std::size_t i = a - kPadraoInicio;
    padrao[i + 0] = static_cast<std::uint8_t>(v & 0xFFu);
    padrao[i + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
    padrao[i + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
    padrao[i + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
  }
  memoria->EscreverBruto(kPadraoInicio, padrao.data(), static_cast<std::uint32_t>(padrao.size()));

  const std::size_t janela = kVigiaFim - kVigiaInicio;
  std::vector<std::uint8_t> base(janela);
  std::vector<std::uint8_t> agora(janela);
  memoria->LerBloco(kVigiaInicio, base.data(), static_cast<std::uint32_t>(janela));

  // O CABECALHO: tudo o que o auditor precisa de saber para calcular o que
  // ESPERA. Os numeros saem daqui, e nao escritos a mao do outro lado.
  std::printf("#efeito_sentinela caixa=0x%08x sp=0x%08x lr=0x%08x", kCaixa, kSPDaCaixa, kLRDaCaixa);
  for (int r = 0; r < 13; ++r) std::printf(" r%d=0x%08x", r, SentinelaDoRegistador(r));
  std::printf(" padrao_m=0x%08x padrao_x=0x%08x padrao_ini=0x%08x padrao_fim=0x%08x"
              " vigia_ini=0x%08x vigia_fim=0x%08x base=0x%08x tamanho=%zu modo=%s\n",
              kPadraoM, kPadraoX, kPadraoInicio, kPadraoFim, kVigiaInicio, kVigiaFim, op.base,
              tamanho, op.thumb ? "thumb" : "arm");

  std::vector<std::uint64_t> indices;
  if (!op.pcs.empty()) {
    indices = LerEnderecos(op.pcs, tamanho);
  } else {
    for (std::uint64_t i = op.inicio; i < palavras && i < op.fim; ++i) indices.push_back(i);
  }

  for (std::size_t k = 0; k < indices.size(); ++k) {
    const std::uint64_t i = indices[k];
    if (i >= palavras) continue;
    std::uint32_t palavra = 0;
    for (std::size_t b = 0; b < tamanho; ++b) palavra |= static_cast<std::uint32_t>(dados[i * tamanho + b]) << (8 * b);
    const std::uint32_t endereco = op.base + static_cast<std::uint32_t>(i * tamanho);

    // A INSTRUCAO NA CAIXA, e a linha de base posta em dia com ela (os quatro
    // bytes da caixa sao os unicos que diferem da base por construcao).
    std::uint8_t bytes[4] = {0, 0, 0, 0};
    for (std::size_t b = 0; b < tamanho; ++b) bytes[b] = dados[i * tamanho + b];
    memoria->EscreverBruto(kCaixa, bytes, static_cast<std::uint32_t>(tamanho));
    for (std::size_t b = 0; b < tamanho; ++b) base[kCaixa - kVigiaInicio + b] = bytes[b];

    // ESTADO INICIAL: os treze sentinelas, sp, lr e a condicao satisfeita.
    cpu->Repor(kCaixa, kSPDaCaixa);
    for (int r = 0; r < 13; ++r) cpu->Set(r, SentinelaDoRegistador(r));
    cpu->Set(zb2::kLR, kLRDaCaixa);
    std::uint32_t cpsr = static_cast<std::uint32_t>(zb2::Modo::Usuario) | zb2::Cpsr::kI | zb2::Cpsr::kF;
    if (op.thumb) {
      cpsr |= zb2::Cpsr::kT;
      if ((palavra & 0xF000u) == 0xD000u) cpsr |= FlagsParaCondicao((palavra >> 8) & 0xFu);
    } else {
      cpsr |= FlagsParaCondicao(palavra >> 28);
    }
    cpu->SetCpsr(cpsr);
    const std::uint32_t cpsr_antes = cpu->Cpsr();
    cpu->Passo();
    const bool recusou = cpu->InstruscoesRecusadas() != 0;
    const std::uint32_t cpsr_depois = cpu->Cpsr();
    const std::uint32_t pc_depois = cpu->Get(zb2::kPC);

    std::printf("#efeito %08x %08x %08x %08x %08x %d regs:", endereco, palavra, cpsr_antes,
                pc_depois, cpsr_depois, recusou ? 1 : 0);
    for (int r = 0; r < 16; ++r) {
      const std::uint32_t inicial = (r == zb2::kPC) ? kCaixa : ((r == zb2::kSP) ? kSPDaCaixa : ((r == zb2::kLR) ? kLRDaCaixa : SentinelaDoRegistador(r)));
      const std::uint32_t v = cpu->Get(r);
      if (v != inicial) std::printf("%d=%08x ", r, v);
    }
    std::printf(" mem:");
    memoria->LerBloco(kVigiaInicio, agora.data(), static_cast<std::uint32_t>(janela));
    if (std::memcmp(agora.data(), base.data(), janela) != 0) {
      for (std::size_t o = 0; o < janela; o += 4) {
        if (std::memcmp(&agora[o], &base[o], 4) == 0) continue;
        std::uint32_t v = 0;
        for (std::size_t b = 0; b < 4; ++b) v |= static_cast<std::uint32_t>(agora[o + b]) << (8 * b);
        std::printf("%08x=%08x ", static_cast<std::uint32_t>(kVigiaInicio + o), v);
      }
      // A JANELA E REPOSTA: uma escrita de uma instrucao nao pode contaminar a
      // leitura da seguinte (foi assim que o `strd` da sonda clobberou a imagem).
      memoria->EscreverBruto(kVigiaInicio, base.data(), static_cast<std::uint32_t>(janela));
    }
    std::printf(" nome:%s\n", cpu->FamiliaDaUltima());
    if (recusou) std::printf("#efeito_motivo %08x %s\n", endereco, cpu->MotivoDaRecusa() != nullptr ? cpu->MotivoDaRecusa() : "recusa sem motivo escrito");
    std::fflush(stdout);
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  Opcoes op;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--thumb") op.thumb = true;
    else if (a == "--efeito") op.efeito = true;
    else if (a.rfind("--pcs=", 0) == 0) op.pcs = a.substr(6);
    else if (a == "--verboso") op.verboso = true;
    else if (a == "--resumo") op.resumo = true;
    else if (a.rfind("--bin=", 0) == 0) op.bin = a.substr(6);
    else if (a.rfind("--base=", 0) == 0) op.base = static_cast<std::uint32_t>(std::strtoul(a.c_str() + 7, nullptr, 0));
    else if (a.rfind("--inicio=", 0) == 0) op.inicio = std::strtoull(a.c_str() + 9, nullptr, 0);
    else if (a.rfind("--fim=", 0) == 0) op.fim = std::strtoull(a.c_str() + 6, nullptr, 0);
    else if (!a.empty() && a[0] == '-') { std::fprintf(stderr, "opcao desconhecida: %s\n", a.c_str()); return 2; }
    else op.ficheiro = a;
  }
  if (op.ficheiro.empty()) {
    std::fprintf(stderr,
                 "uso: zb2_sonda_descodificador <ficheiro> [--thumb] [--base=0x0] [--bin=f]\n"
                 "                              [--inicio=N] [--fim=N] [--verboso] [--resumo]\n");
    return 2;
  }

  std::ifstream entrada(op.ficheiro, std::ios::binary);
  if (!entrada) { std::fprintf(stderr, "nao abri %s\n", op.ficheiro.c_str()); return 3; }
  std::vector<std::uint8_t> dados((std::istreambuf_iterator<char>(entrada)),
                                  std::istreambuf_iterator<char>());
  // O MODO DE EFEITO nao classifica: ele EXECUTA numa caixa de areia e le o
  // estado. Sai daqui, antes de se carregar a imagem na memoria.
  if (op.efeito) return CorrerEfeito(op, dados);
  const std::size_t tamanho = op.thumb ? 2 : 4;
  const std::uint64_t palavras = dados.size() / tamanho;

  // A IMAGEM E CARREGADA NA MEMORIA DA SONDA. Sem isto o `Passo` lia
  // `mem.Ler32(pc)` -- memoria vazia, palavra `0x00000000` -- e classificava
  // TODAS as palavras como `condicao_falsa`. Foi o primeiro resultado desta
  // sonda, e estava errado: um instrumento que le do sitio errado da uma
  // resposta plausivel e uniforme, que e o modo mais facil de nao se dar por
  // ele.
  //
  // `ArmInterpreter` guarda uma REFERENCIA para a memoria, logo nao e
  // atribuivel: os dois vivem num `unique_ptr` e sao recriados juntos.
  std::unique_ptr<zb2::Memoria> memoria;
  std::unique_ptr<zb2::ArmInterpreter> cpu;
  const auto carregar = [&]() {
    memoria.reset(new zb2::Memoria(nullptr));
    memoria->EscritorUnico("sonda_descodificador");
    memoria->EscreverBruto(op.base, dados.data(), static_cast<std::uint32_t>(dados.size()));
    cpu.reset(new zb2::ArmInterpreter(*memoria, nullptr));
  };
  carregar();
  TabelaDeClasses tabela;
  std::vector<std::uint8_t> classes;
  if (!op.bin.empty()) classes.reserve(static_cast<std::size_t>(palavras));
  std::vector<std::uint64_t> contagens;

  const std::uint32_t sp = 0x80080000u;  // dentro do espaco do Zeebo, e uma area
                                         // propria: o guest usa a pilha em
                                         // 0x8007ffc0..0x8007ffcc (medido no
                                         // ledger), e nenhuma escrita aqui
                                         // contamina a leitura das palavras.

  for (std::uint64_t i = 0; i < palavras; ++i) {
    if (i < op.inicio || i >= op.fim) continue;
    std::uint32_t palavra = 0;
    for (std::size_t b = 0; b < tamanho; ++b) palavra |= static_cast<std::uint32_t>(dados[i * tamanho + b]) << (8 * b);
    const std::uint32_t pc = op.base + static_cast<std::uint32_t>(i * tamanho);

    // Estado zerado, com a condicao satisfeita. `Repor` zera o banco e as
    // bandeiras; o CPSR e posto a seguir, porque a bandeira T nao pode vir do
    // `Repor` (o modo tem de dizer se a palavra e ARM ou Thumb).
    cpu->Repor(pc, sp);
    std::uint32_t cpsr = static_cast<std::uint32_t>(zb2::Modo::Usuario) | zb2::Cpsr::kI | zb2::Cpsr::kF;
    if (op.thumb) {
      cpsr |= zb2::Cpsr::kT;
      // No Thumb a condicao SO existe no bloco 0xD000-0xDFFF (bits 11-8). Sem
      // isto, metade dos ramos condicionais aparecia como `condicao_falsa` e o
      // auditor perdia metade da forma 13.
      if ((palavra & 0xF000u) == 0xD000u) cpsr |= FlagsParaCondicao((palavra >> 8) & 0xFu);
    } else {
      cpsr |= FlagsParaCondicao(palavra >> 28);
    }
    // A PALAVRA A AUDITAR E REPOSTA ANTES DE CADA PASSO. Medido, e foi um
    // defeito de instrumento: um `strd`/`str` da sonda escreve em memoria, e a
    // memoria E a imagem -- o `strd` de 0x04 escreveu em 0x08 e 0x0C, e as
    // palavras seguintes foram classificadas JA CLOBBERED (lidas como
    // `0x00000000`, isto e, `condicao_falsa`). A classificacao passa a ser
    // sempre da palavra do FICHEIRO.
    for (std::size_t b = 0; b < tamanho; ++b) {
      const std::uint8_t byte = static_cast<std::uint8_t>((palavra >> (8 * b)) & 0xFFu);
      memoria->EscreverBruto(pc + static_cast<std::uint32_t>(b), &byte, 1);
    }
    cpu->SetCpsr(cpsr);
    const std::uint64_t recusadas_antes = cpu->InstruscoesRecusadas();
    cpu->Passo();
    const bool recusou = cpu->InstruscoesRecusadas() != recusadas_antes;

    std::string nome = cpu->FamiliaDaUltima();
    if (recusou) {
      nome += "|";
      nome += cpu->MotivoDaRecusa() != nullptr ? cpu->MotivoDaRecusa() : "recusa sem motivo escrito";
    }
    const int id = tabela.Id(nome);
    if (id < 0) { tabela.MarcarTransbordo(); std::fprintf(stderr, "tabela de classes transbordou\n"); return 4; }

    if (static_cast<std::size_t>(id) >= contagens.size()) contagens.resize(static_cast<std::size_t>(id) + 1, 0);
    ++contagens[static_cast<std::size_t>(id)];
    if (!op.bin.empty()) classes.push_back(static_cast<std::uint8_t>(id));
    if (op.verboso) std::printf("%08x %08x %s\n", pc, palavra, nome.c_str());

    // A memoria do guest e esparsa, e uma escrita num endereco arbitrario cria
    // uma pagina. Sem esta limpeza, uma varredura de milhoes de palavras enchia
    // a RAM do hospedeiro. O estado do CPU nao interessa entre palavras (e
    // reposto em cada uma).
    // O limite e medido em PAGINAS porque a memoria do guest e esparsa: uma
    // escrita num endereco arbitrario cria uma pagina de 4 KiB. A imagem mais
    // pequena do corpus tem 22 paginas e a maior 2129, logo 40 000 paginas
    // (160 MiB) so se atinge se as escritas da sonda se espalharem -- e nesse
    // caso a memoria e refeita e a imagem recarregada, em vez de a sonda
    // continuar com a RAM do hospedeiro a crescer.
    if ((i & 0xFFFFu) == 0 && memoria->PaginasAlocadas() > 40000u) carregar();
  }

  if (!op.bin.empty()) {
    std::ofstream saida(op.bin, std::ios::binary);
    if (!saida) { std::fprintf(stderr, "nao escrevi %s\n", op.bin.c_str()); return 3; }
    saida.write(reinterpret_cast<const char*>(classes.data()), static_cast<std::streamsize>(classes.size()));
  }
  const std::vector<std::string>& nomes = tabela.Nomes();
  for (std::size_t i = 0; i < nomes.size(); ++i) std::printf("#classe %zu %s\n", i, nomes[i].c_str());
  std::printf("#palavras %zu\n", classes.empty() ? static_cast<std::size_t>(palavras) : classes.size());
  if (op.resumo) {
    for (std::size_t i = 0; i < nomes.size(); ++i) {
      if (i < contagens.size() && contagens[i] != 0) std::printf("#conta %zu %llu\n", i, static_cast<unsigned long long>(contagens[i]));
    }
  }
  return 0;
}
