#ifndef ZB2_CORE_MEMORIA_MEMORIA_H
#define ZB2_CORE_MEMORIA_MEMORIA_H

// Espaco de enderecos do guest: paginas esparsas, um escritor, vigia de escrita.
//
// PRINCIPIO P6 (um escritor por memoria). Medicao que o sustenta: no Zeebulator
// antigo nenhum dos travamentos tinha subsistema nosso envolvido -- o
// diagnostico `alive` mostrava `hle_depth = 0` e `hle_calls` congelado, ou seja
// o guest girava em codigo proprio e nenhum handler de HLE corria. Migrar
// subsistemas para threads proprias nao resolveria isso, e trocaria um bug
// visivel por corridas de dados silenciosas: a memoria do guest e partilhada
// pelo heap, pelo framebuffer e pelos buffers de midia.
//
// Esta classe nao tem mutex. Nao e esquecimento: e a decisao. O escritor
// identifica-se uma vez, e escrever sem se ter identificado e um erro registado.

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/traco/traco.h"

namespace zb2 {

using Endereco = std::uint32_t;

// O Zeebo tem 32 bits de espaco, mas nenhum titulo do corpus usa mais de uns
// poucos MB. Paginas de 4 KiB cobrem o que existe e mantem a tabela pequena.
class Memoria {
 public:
  static constexpr std::uint32_t kPaginaBits = 12;
  static constexpr std::uint32_t kPagina = 1u << kPaginaBits;  // 4096
  static constexpr std::uint32_t kMascaraPagina = kPagina - 1;

  explicit Memoria(Traco* traco = nullptr);

  // --- o unico escritor -------------------------------------------------
  // Identifica quem escreve. A partir daqui, escritas de outro autor sao
  // registadas como falta. Um segundo escritor nao e proibido a forca -- e
  // denunciado, porque no Zeebulator antigo essa era a diferenca entre um bug
  // visivel e um silencioso.
  void EscritorUnico(std::string nome);
  const std::string& Autor() const { return autor_; }
  std::uint64_t EscritasDeOutroAutor() const { return escritas_outro_autor_; }

  // --- leitura ----------------------------------------------------------
  // Ler de uma pagina que nunca foi escrita devolve zero e NAO aloca. Isto
  // importa: no Zeebulator antigo a memoria do guest era um mapa de objectos
  // esparsos e algumas rotinas liam "para ver o que ha", criando paginas sem
  // querer.
  std::uint8_t Ler8(Endereco a) const;
  std::uint16_t Ler16(Endereco a) const;
  std::uint32_t Ler32(Endereco a) const;
  void LerBloco(Endereco a, void* destino, std::uint32_t quantos) const;

  // --- escrita ----------------------------------------------------------
  void Escrever8(Endereco a, std::uint8_t v);
  void Escrever16(Endereco a, std::uint16_t v);
  void Escrever32(Endereco a, std::uint32_t v);
  void EscreverBloco(Endereco a, const void* origem, std::uint32_t quantos);

  // Escreve sem passar pela vigia. Reservado a carregar o modulo e a repor
  // estado -- operacoes que NAO sao "o guest a mexer na memoria".
  void EscreverBruto(Endereco a, const void* origem, std::uint32_t quantos);

  // --- vigia de escrita -------------------------------------------------
  // Uma faixa vigiada regista o PRIMEIRO escritor de cada endereco. Foi assim
  // que se descobriu, na sessao anterior, que a faixa de pilha do `Boiaz` era
  // escrita pelo proprio jogo -- o que derrubou a hipotese do buffer vazio sem
  // custar uma unica corrida a mais.
  struct Vigia {
    Endereco inicio = 0;
    Endereco fim = 0;  // exclusivo
    std::string rotulo;
  };
  void Vigiar(const Vigia& v);
  void PararDeVigiar();
  const std::vector<Vigia>& Vigias() const { return vigias_; }
  // Pares (endereco, pc de quem escreveu), por ordem de primeira escrita.
  const std::vector<std::pair<Endereco, Endereco>>& EscritasVigiadas() const {
    return escritas_vigiadas_;
  }
  // O chamador declara em que PC esta antes de escrever. A vigia guarda-o.
  void PcAtual(Endereco pc) { pc_ = pc; }

  // --- utilitarios ------------------------------------------------------
  std::uint32_t PaginasAlocadas() const { return static_cast<std::uint32_t>(paginas_.size()); }
  bool Existe(Endereco a) const;
  // Le uma cadeia terminada em NUL, limitada. Devolve o comprimento.
  std::size_t LerCadeia(Endereco a, std::string* saida, std::uint32_t limite = 4096) const;

 private:
  std::uint8_t* Pagina(Endereco a, bool criar);

  Traco* traco_;
  std::string autor_;
  std::unordered_map<std::uint32_t, std::unique_ptr<std::uint8_t[]>> paginas_;
  std::uint64_t escritas_outro_autor_ = 0;
  std::vector<Vigia> vigias_;
  std::vector<std::pair<Endereco, Endereco>> escritas_vigiadas_;
  std::uint64_t vigiados_ja_vistos_ = 0;
  Endereco pc_ = 0;
};

}  // namespace zb2

#endif  // ZB2_CORE_MEMORIA_MEMORIA_H
