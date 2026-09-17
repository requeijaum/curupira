#ifndef ZB2_CORE_BREW_AJUDANTES_H
#define ZB2_CORE_BREW_AJUDANTES_H

// A tabela `AEEHelperFuncs`: o sistema que todo modulo BREW ve.
//
// COMO SE SABE A ORDEM DOS SLOTS: o `AEEHelperFuncs` e um `struct` declarado em
// `platform/system/inc/AEEStdLib.h` do SDK, com 117 campos na ordem de
// declaracao. Essa ordem E a ordem dos offsets. Na arvore antiga isto foi
// conferido campo a campo contra o cabecalho, e deu ZERO divergencias -- mas o
// mapa tinha sido primeiro copiado de outro emulador, e a copia estava errada em
// dois sitios (`kStrstrSlotOffset` guardava o offset do `stristr`). Dai a regra
// deste projeto (desenho, decisao estrutural 2): **a tabela e DECLARADA, e a
// instalacao recusa quando falta um slot**.
//
// COMO SE SABE QUE E EM `base - 4`: o `AEEMod_Load` do `imicro3d.mod` faz
// (medido, desmonte do nosso corpus em 0x00100710):
//     ldr r0, [pc, #120]     ; offset
//     add r0, r0, pc         ; endereco do bloco de globais (ROPI)
//     ldr r0, [r0, #-4]      ; <<< a tabela esta 4 bytes antes
//     ldr r1, [r0, #104]     ; e o slot 0x68 = malloc
//     add r0, r4, #16        ; nSize + sizeof(IModuleVtbl)
//     bx  r1
//
// PRINCIPIO P2: um slot por preencher NAO devolve sucesso em silencio. Devolve
// EUNSUPPORTED e regista o nome de quem faltou. No Zeebulator antigo havia um
// stub que devolvia sucesso e nao fazia nada, e 86 377 chamadas de `glCullFace`
// foram descartadas sem ninguem saber.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/cpu/cpu.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"

namespace zb2 {

// Codigos de erro do BREW que usamos. Os valores estao em
// `platform/system/inc/AEEStdErr.h`, com o numero da linha ao lado -- e este
// enum e a UNICA fonte deles.
//
// PORQUE ISTO ESTAVA A FAZER FALTA: o `despacho.cpp` tinha uma copia propria
// destes numeros no seu `namespace` anonimo, e a copia tinha
// `kAeeUnsupported = 0xE0000001`. Esse valor nao existe em cabecalho nenhum (o
// `AEE_EUNSUPPORTED` e 20, AEEStdErr.h:36), e a copia local ESCONDIA o enum --
// logo todo o "nao implementado" desta arvore respondia um codigo que jogo nenhum
// conhece. **Duas copias de um numero medido sao duas chances de ele divergir**,
// e esta divergiu.
enum : std::int32_t {
  kAeeSuccess = 0,             // AEEStdErr.h:16
  kAeeFailed = 1,              // AEEStdErr.h:17
  kAeeNoMemory = 2,            // AEEStdErr.h:18 ("insufficient RAM")
  kAeeClassNotSupported = 3,   // AEEStdErr.h:19
  kAeeBadParm = 14,            // AEEStdErr.h:30
  kAeeUnsupported = 20,        // AEEStdErr.h:36
  kAeeNoSuch = 39,             // AEEStdErr.h:55
  kAeeNoMore = 47,             // AEEStdErr.h:63
};

// Alocador do sistema. Vive aqui e nao no modulo porque e o sistema que o
// fornece -- e porque o modulo o pede antes de qualquer outra coisa.
//
// Simples e explicito: uma regiao de heap de tamanho fixo, blocos com cabecalho
// de tamanho, lista de livres encadeada por ordem de endereco. Nao tenta ser
// rapido; tenta ser conferivel.
class Alocador {
 public:
  Alocador(Memoria& mem, std::uint32_t inicio, std::uint32_t tamanho, Traco* traco = nullptr);

  std::uint32_t Malloc(std::uint32_t tamanho);
  void Free(std::uint32_t endereco);
  // `zerar` = a parte NOVA (quando cresce) fica a zero. E a regra do `malloc`
  // desta casa (`ALLOC_NO_ZMEM` a ZERO, `AEEStdLib.h:547`): o `malloc` zera por
  // omissao, e o `realloc` cresce pela mesma regra. A omissao e `true` -- quem
  // nao diz nada recebe a regra, e nao o contrario.
  std::uint32_t Realloc(std::uint32_t endereco, std::uint32_t tamanho, bool zerar = true);

  // O TAMANHO UTIL DE UM BLOCO VIVO deste alocador, pelo cabecalho, ou 0.
  //
  // EXISTE POR UMA MEDICAO, e nao por simetria: o `IShell::LoadResDataEx` recebe
  // do chamador um `*pnBufSize` que nem sempre e a verdade -- o `peggle` declara
  // 6 bytes e o bloco que ele proprio alocou (com o tamanho que a consulta
  // anterior devolveu) tem 64 629; o `torkandkral` declara 131 e os blocos tem de
  // 32 KiB a 1 MiB; o `heavyweaponbrew` declara 1097 para 12 336. Sem esta
  // medida, a unica leitura possivel era o numero declarado, e os tres titulos
  // ficavam com o buffer por servir.
  //
  // ZERO QUER DIZER "NAO SEI", e nao "cabem zero": nem todo ponteiro do guest e
  // um bloco deste alocador (a pilha, um DIB, um objecto). Quem chama tem de
  // tratar o zero como ausencia de medida.
  //
  // A LISTA E PERCORRIDA, e o cabecalho NAO se le num endereco de confianca: um
  // `endereco - kCabecalho` que caia no meio de um bloco daria um cabecalho
  // inventado, e um tamanho inventado a servir de guarda de memoria e pior do
  // que nao ter guarda nenhuma.
  std::uint32_t TamanhoDoBloco(std::uint32_t endereco) const;

  std::uint32_t Inicio() const { return inicio_; }
  std::uint32_t Tamanho() const { return tamanho_; }
  std::uint32_t Alocado() const { return alocado_; }
  std::uint32_t Pico() const { return pico_; }
  std::uint32_t Blocos() const { return blocos_; }
  std::uint32_t Falhas() const { return falhas_; }

 private:
  // Cabecalho de bloco: 16 bytes, alinhado a 8. Guarda o tamanho do bloco
  // (cabecalho incluido) e se esta livre. Os outros 8 bytes ficam de reserva
  // para o `realloc` poder crescer sem mover dados quando o vizinho esta livre.
  struct Cabecalho {
    std::uint32_t tamanho = 0;   // do cabecalho ate ao fim do bloco
    std::uint32_t livre = 0;
    std::uint32_t reservado1 = 0;
    std::uint32_t reservado2 = 0;
  };
  static constexpr std::uint32_t kCabecalho = 16;
  static constexpr std::uint32_t kAlinhamento = 8;

  Cabecalho Ler(std::uint32_t endereco) const;
  void Escrever(std::uint32_t endereco, const Cabecalho& c);

  Memoria& mem_;
  Traco* traco_ = nullptr;
  std::uint32_t inicio_ = 0;
  std::uint32_t tamanho_ = 0;
  std::uint32_t alocado_ = 0;
  // O maximo que o `alocado_` atingiu (ver o `Malloc`).
  std::uint32_t pico_ = 0;
  std::uint32_t blocos_ = 0;
  std::uint32_t falhas_ = 0;
};

// Uma funcao da tabela. Recebe o nucleo e escreve o resultado em R0.
using FuncaoDeAjudante = std::function<void(ICpu&)>;

struct SlotDeAjudante {
  std::uint32_t offset = 0;
  const char* nome = nullptr;
  FuncaoDeAjudante funcao;  // vazio = nao implementado
  bool implementado() const { return static_cast<bool>(funcao); }
};

class TabelaDeAjudantes {
 public:
  TabelaDeAjudantes(Memoria& mem, Alocador& alocador, Traco* traco = nullptr);

  // Declara um slot. `nome` vem do SDK; `funcao` vazia significa "declarado e
  // ainda nao implementado", que e um estado legitimo e VISIVEL.
  void Declarar(std::uint32_t offset, const char* nome, FuncaoDeAjudante funcao = {});

  // Escreve a tabela no espaco do guest e devolve o endereco.
  //
  // RECUSA a instalar quando algum slot declarado esta por preencher, e diz
  // quais -- a menos que `permitir_por_implementar` seja verdade, que existe so
  // para a sonda poder medir o modulo com o que ja ha.
  struct Instalacao {
    bool ok = false;
    std::uint32_t endereco = 0;
    std::vector<std::string> faltam;
  };
  Instalacao Instalar(std::uint32_t endereco, bool permitir_por_implementar = false);

  const std::vector<SlotDeAjudante>& Slots() const { return slots_; }
  std::size_t Implementados() const;
  std::size_t Declarados() const { return slots_.size(); }

 private:
  Memoria& mem_;
  Alocador& alocador_;
  Traco* traco_;
  std::vector<SlotDeAjudante> slots_;
};

}  // namespace zb2

#endif  // ZB2_CORE_BREW_AJUDANTES_H
