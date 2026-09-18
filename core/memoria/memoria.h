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

  // --- ATRIBUICAO DA LEITURA (frente inst) ------------------------------
  // A pendencia de cima diz QUE endereco, e nao diz DE QUEM. Quem consumia era
  // o fim da instrucao do passo -- e a pendencia sobrevivia ao passo, logo uma
  // leitura feita pelo HOSPEDEIRO (o nosso C++ a ler a memoria do guest) era
  // acusada no PC da instrucao que corresse A SEGUIR.
  //
  // MEDIDO no `alice` (frente ropi2, seccao 3): em 30 visitas ao PC 0x3f050 o
  // `r4` foi SEMPRE 0x200 e `[0x1fc]` estava mapeado; 21 das 22 recusas
  // atribuidas a esse PC eram o NOSSO `Formatar` a ler o argumento `%s` do
  // `ASSERT` (o ASCII "  %s" = 0x73252020), e a instrucao acusada era a que
  // estava no `lr` do ajudante. A evidencia "0x3f050 le 0x73252020" era FALSA.
  //
  // QUEM LEU SABE-SE PELO PC DECLARADO A MEMORIA (`PcAtual`, que o `Passo` poe
  // no inicio de cada instrucao): a leitura nasce com o PC de quem a fez.
  struct LeituraNaoMapeada {
    Endereco endereco = 0;
    Endereco pc = 0;
    std::string leitor_host;
    Endereco r0 = 0, r1 = 0, lr = 0;
  };
  // Contexto observacional da chamada C++ que le memoria guest.
  void ContextoLeitorHost(std::string leitor, Endereco r0, Endereco r1, Endereco lr) {
    leitor_host_ = std::move(leitor); leitor_r0_ = r0; leitor_r1_ = r1; leitor_lr_ = lr;
  }

  // Consome a pendencia, mas SO se ela for da instrucao `pc` (o mesmo PC que o
  // `Passo` acabou de declarar). Uma pendencia de outro dono fica por consumir:
  // quem a recolhe e o `RecolherLeituraNaoMapeadaForaDeInstrucao`, no passo
  // seguinte. A atribuicao fica garantida por construcao, e nao por convencao.
  // ANOTA UMA LEITURA NAO MAPEADA SEM A FAZER (o mesmo registo do `Ler8`, e so
  // se o endereco nao existir). Existe porque quem PERGUNTA nem sempre e quem LE:
  //
  // Um `LDR` de palavra DESALINHADO le a palavra ALINHADA e roda-a -- e o que o
  // ARM faz -- mas o endereco que interessa a quem depura o guest e o que ele
  // CALCULOU. Sem isto, o motivo da recusa nomeava `0xea000094` quando o guest
  // tinha pedido `0xea000097`, e o teste
  // `Cpu.LeituraDeDadosEmEnderecoNaoMapeadoERecusadaComOEndereco` (medido no
  // `cnk2`) exige o endereco do guest.
  void AnotarLeituraNaoMapeada(Endereco a) const {
    if (Existe(a)) return;
    ++leituras_nao_mapeadas_;
    if (!leitura_nao_mapeada_pendente_) {
      leitura_nao_mapeada_pendente_ = true;
      endereco_da_leitura_nao_mapeada_pendente_ = a;
      pc_da_leitura_nao_mapeada_pendente_ = pc_;
      leitura_pendente_.leitor_host = leitor_host_;
      leitura_pendente_.r0 = leitor_r0_; leitura_pendente_.r1 = leitor_r1_; leitura_pendente_.lr = leitor_lr_;
    }
  }

  bool ConsumirLeituraNaoMapeadaPendenteDaInstrucao(Endereco pc, Endereco* primeira) const {
    if (!leitura_nao_mapeada_pendente_) return false;
    if (pc_da_leitura_nao_mapeada_pendente_ != pc) return false;
    *primeira = endereco_da_leitura_nao_mapeada_pendente_;
    leitura_nao_mapeada_pendente_ = false;
    return true;
  }

  // FECHA A CONTA DA INSTRUCAO ANTERIOR. Uma leitura nao mapeada que chegou ate
  // aqui NAO foi consumida por instrucao nenhuma: quem leu foi o HOSPEDEIRO.
  // Devolve-a (com o PC de quem a fez) e limpa-a -- o que fica por contar e
  // contado, e nao atribuido a uma instrucao inocente.
  //
  // CONTA EVENTOS, e nao leituras: a pendencia e unica, logo varias leituras do
  // hospedeiro seguidas contam uma vez. O total de leituras (de todos os
  // autores) esta em `LeiturasNaoMapeadas()`.
  bool RecolherLeituraNaoMapeadaForaDeInstrucao(LeituraNaoMapeada* lida) const {
    if (!leitura_nao_mapeada_pendente_) return false;
    lida->endereco = endereco_da_leitura_nao_mapeada_pendente_;
    lida->pc = pc_da_leitura_nao_mapeada_pendente_;
    lida->leitor_host = leitura_pendente_.leitor_host;
    lida->r0 = leitura_pendente_.r0; lida->r1 = leitura_pendente_.r1; lida->lr = leitura_pendente_.lr;
    leitura_nao_mapeada_pendente_ = false;
    ++leituras_nao_mapeadas_fora_de_instrucao_;
    ultima_leitura_fora_de_instrucao_ = *lida;
    return true;
  }
  std::uint64_t LeiturasNaoMapeadasForaDeInstrucao() const {
    return leituras_nao_mapeadas_fora_de_instrucao_;
  }
  // A ultima leitura recolhida fora de instrucao. Existe para o teste poder
  // afirmar o PC de QUEM LEU sem depender do texto do traco.
  const LeituraNaoMapeada& UltimaLeituraForaDeInstrucao() const {
    return ultima_leitura_fora_de_instrucao_;
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
  // Patamar do aviso de consumo do hospedeiro: 65536 paginas de 4 KiB = 256 MiB.
  // Nao e um teto (escrever continua a funcionar) -- e um marco para que um
  // OOM futuro tenha nome e numero no traco em vez de ser um misterio. Viu-se
  // nesta auditoria que nada no corpus chega perto; o dia em que algo chegar,
  // o aviso diz onde.
  static constexpr std::uint32_t kPaginasPorAvisoDeMemoria = 65536;
  std::uint32_t paginas_avisadas_ = 0;
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
  // O PC declarado a memoria no instante da leitura pendente: e o que diz DE
  // QUEM e a leitura (frente inst).
  mutable Endereco pc_da_leitura_nao_mapeada_pendente_ = 0;
  mutable std::uint64_t leituras_nao_mapeadas_fora_de_instrucao_ = 0;
  mutable LeituraNaoMapeada ultima_leitura_fora_de_instrucao_;
  std::string leitor_host_;
  Endereco leitor_r0_ = 0, leitor_r1_ = 0, leitor_lr_ = 0;
  mutable LeituraNaoMapeada leitura_pendente_;
};

}  // namespace zb2

#endif  // ZB2_CORE_MEMORIA_MEMORIA_H
