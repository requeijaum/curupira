// Cheats declarados: memory patching on-the-fly, auditavel.
//
// Um cheat e uma lista de escritas (u8/u16/u32) na memoria do guest que dispara
// UMA vez, ou numa fronteira de fase (`carga`, `create`, `start`, `quadros` +
// numero) ou quando o PC do guest passa por um endereco. Cada disparo vai para
// o traco (`CHEAT_APLICADO` com o nome) -- um patch silencioso e o stub mudo
// com outro nome, e esta casa nao os escreve.
//
// Formato (JSON lido a mao, como o corpus da bateria -- sem dependencia nova):
//   {"cheats": [
//     {"nome": "bandeira", "titulo": "a3d", "fase": "start",
//      "escreve": [{"endereco": "0x1000", "u32": 1}]},
//     {"nome": "codigo", "pc": "0x58c",
//      "escreve": [{"endereco": "0x2000", "u8": 255}]}
//   ]}
// `titulo` omitido = todos os titulos. `fase` omitida + `pc` presente = gatilho
// de passo. Entrada sem `nome`, sem `escreve`, com `fase` desconhecida ou sem
// `fase` nem `pc` e IGNORADA (nao derruba as validas).
//
// Usos: destravar funcionamento bobo (bandeira que o jogo nunca liga, retorno
// forcado via byte de codigo) e remover bugs (NOP sobre chamada quebrada).
// Nao faz: registradores (so memoria), condicoes sobre valores (fogo unico),
// escrita por quadro (um disparo por cheat por titulo).
#ifndef ZB2_CORE_BREW_CHEATS_H
#define ZB2_CORE_BREW_CHEATS_H

#include <cstdint>
#include <string>
#include <vector>

namespace zb2 {
class Memoria;
class Traco;
namespace brew {

struct EscritaDeCheat {
  std::uint32_t endereco = 0;
  std::uint32_t tamanho = 4;  // 1, 2 ou 4
  std::uint32_t valor = 0;
};

struct Cheat {
  std::string nome;
  std::string titulo;  // "" = todos
  std::string fase;    // "carga"|"create"|"start"|"quadros"|""
  std::uint32_t quadro = 0;
  std::uint32_t pc = 0;
  bool tem_pc = false;
  std::vector<EscritaDeCheat> escritas;
  bool aplicada = false;
};

class Cheats {
 public:
  Cheats(Memoria& mem, Traco& traco);

  // Le de texto (testes) ou de ficheiro (bateria). Devolve falso so se o
  // ficheiro nao abrir; entradas invalidas sao ignoradas, nunca erro fatal.
  bool LerConteudo(const std::string& json);
  bool LerFicheiro(const std::string& caminho);
  std::size_t Quantos() const { return cheats_.size(); }

  // Troca de titulo: limpa os disparos e fixa o filtro.
  void ReporPorTitulo(const std::string& mod);

  // Fronteira de fase (bateria chama com fase e numero do quadro corrente).
  // Devolve quantos dispararam.
  std::uint32_t NaFase(const std::string& fase, std::uint32_t quadro);

  // Passo do guest (despacho chama com o PC). Sem cheats com `pc` o custo e
  // um `if` -- nao se taxa o laco por uma lista vazia.
  void NoPasso(std::uint32_t pc);

 private:
  bool TemPc() const { return tem_pc_; }
  void Aplicar(Cheat& c);

  Memoria& mem_;
  Traco& traco_;
  std::vector<Cheat> cheats_;
  std::string titulo_;
  bool tem_pc_ = false;
};

}  // namespace brew
}  // namespace zb2

#endif  // ZB2_CORE_BREW_CHEATS_H
