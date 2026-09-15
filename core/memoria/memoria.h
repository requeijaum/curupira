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

  // --- faixa suja -------------------------------------------------------
  // "O guest mexeu nestes bytes desde a ultima vez que eu limpei?" -- min e max,
  // e nada mais. A `Vigia` guarda o PRIMEIRO escritor de CADA endereco e faz uma
  // busca linear por escrita: com uma faixa de 614 400 bytes (o ecra) isso e
  // quadratico e impagavel. Esta responde a pergunta que o ecra faz, em duas
  // comparacoes por byte.
  //
  // QUEM SUJA: `Escrever*` e `EscreverBloco`, que sao o guest (ou um servico a
  // pedido dele). `EscreverBruto` NAO suja -- e o hospedeiro a repor a sua
  // propria copia, e marca-la levaria o motor a ler de volta o que acabou de
  // escrever e a chamar-lhe desenho do guest.
  void VigiarSujidade(Endereco inicio, Endereco fim) {
    faixa_inicio_ = inicio;
    faixa_fim_ = fim;
    LimparSujidade();
  }
  void LimparSujidade() {
    sujo_min_ = faixa_fim_;
    sujo_max_ = faixa_inicio_;
  }
  bool Sujo() const { return sujo_min_ < sujo_max_; }
  Endereco SujoInicio() const { return sujo_min_; }
  Endereco SujoFim() const { return sujo_max_; }  // exclusivo

  // --- sonda de LEITURA -------------------------------------------------
  // A vigia responde "quem escreveu aqui?". Esta responde a pergunta simetrica,
  // "quem LEU daqui?", e existe por uma medicao concreta: a falta
  // `IDIB::pBmp do bitmap do ecra` era registada quando se CONSTRUIA o
  // cabecalho do bitmap do ecra, nao quando o guest lia o campo. Sem saber quem
  // le, nao se sabe quantos titulos a falta bloqueia mesmo.
  //
  // Faixa unica, e desligada por omissao (`fim == 0`): o custo no caminho
  // quente e uma comparacao.
  void SondarLeitura(Endereco inicio, Endereco fim) {
    sonda_inicio_ = inicio;
    sonda_fim_ = fim;
    leituras_sondadas_.clear();
  }
  void PararDeSondar() { sonda_fim_ = 0; leituras_sondadas_.clear(); }
  // Pares (endereco, pc de quem leu), por ordem de primeira leitura.
  const std::vector<std::pair<Endereco, Endereco>>& LeiturasSondadas() const {
    return leituras_sondadas_;
  }

  // --- leitura nao mapeada ---------------------------------------------
  // A leitura de uma pagina que nunca foi escrita CONTINUA a devolver zero e a
  // nao alocar (o contrato de cima nao muda) -- MAS fica contada e PENDENTE, e
  // o CPU consome a pendencia no fim da instrucao e RECUSA com o endereco.
  // Sem isto, um `ldr` de um ponteiro nulo devolvia 0 em silencio e o erro nao
  // se propagava: medido no `cnk2`, `[0xea000097]` devolveu 0 e o `bx` seguinte
  // saltou para 0 (frente 16). A pendencia e unica (o primeiro endereco): uma
  // instrucao com varias leituras nao mapeadas recusa-se UMA vez.
  std::uint64_t LeiturasNaoMapeadas() const { return leituras_nao_mapeadas_; }
  bool ConsumirLeituraNaoMapeadaPendente(Endereco* primeira) const {
    if (!leitura_nao_mapeada_pendente_) return false;
    *primeira = endereco_da_leitura_nao_mapeada_pendente_;
    leitura_nao_mapeada_pendente_ = false;
    return true;
  }

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
  Endereco sonda_inicio_ = 0;
  Endereco sonda_fim_ = 0;  // exclusivo; zero = sonda desligada
  Endereco faixa_inicio_ = 0;
  Endereco faixa_fim_ = 0;  // exclusivo; zero = sem faixa suja
  Endereco sujo_min_ = 0;
  Endereco sujo_max_ = 0;
  mutable std::vector<std::pair<Endereco, Endereco>> leituras_sondadas_;
  // Leituras de paginas inexistentes (o contador serve aos testes e ao
  // diagnostico; a pendencia serve ao CPU para recusar com o endereco).
  mutable std::uint64_t leituras_nao_mapeadas_ = 0;
  mutable bool leitura_nao_mapeada_pendente_ = false;
  mutable Endereco endereco_da_leitura_nao_mapeada_pendente_ = 0;
};

}  // namespace zb2

#endif  // ZB2_CORE_MEMORIA_MEMORIA_H
