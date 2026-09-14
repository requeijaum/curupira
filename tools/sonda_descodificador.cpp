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

}  // namespace

int main(int argc, char** argv) {
  Opcoes op;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--thumb") op.thumb = true;
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
