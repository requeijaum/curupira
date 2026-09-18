#include "core/memoria/memoria.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace zb2 {

Memoria::Memoria(Traco* traco) : traco_(traco) {}

void Memoria::EscritorUnico(std::string nome) {
  if (!autor_.empty() && autor_ != nome && traco_ != nullptr) {
    traco_->Emitir(Area::Memoria, Nivel::Aviso, "SEGUNDO_ESCRITOR",
                   "ja havia '" + autor_ + "', agora '" + nome + "'");
  }
  autor_ = std::move(nome);
}

std::uint8_t* Memoria::Pagina(Endereco a, bool criar) {
  const std::uint32_t n = a >> kPaginaBits;
  auto it = paginas_.find(n);
  if (it != paginas_.end()) return it->second.get();
  if (!criar) return nullptr;
  auto nova = std::make_unique<std::uint8_t[]>(kPagina);
  std::memset(nova.get(), 0, kPagina);
  std::uint8_t* p = nova.get();
  paginas_.emplace(n, std::move(nova));
  const std::uint32_t patamar =
      static_cast<std::uint32_t>(paginas_.size() / kPaginasPorAvisoDeMemoria);
  if (patamar > paginas_avisadas_) {
    paginas_avisadas_ = patamar;
    if (traco_ != nullptr) {
      char buf[96];
      std::snprintf(buf, sizeof(buf), "%u paginas de 4 KiB (%u MiB) escritas pelo guest",
                    static_cast<unsigned>(paginas_.size()),
                    static_cast<unsigned>(paginas_.size() / 256u));
      traco_->Emitir(Area::Memoria, Nivel::Aviso, "PAGINAS_DO_HOSPEDE", buf);
    }
  }
  return p;
}

bool Memoria::Existe(Endereco a) const {
  return paginas_.find(a >> kPaginaBits) != paginas_.end();
}

std::uint8_t Memoria::Ler8(Endereco a) const {
  const std::uint32_t n = a >> kPaginaBits;
  auto it = paginas_.find(n);
  if (it == paginas_.end()) {
    // Ler nao aloca -- mas NAO fica em silencio: a leitura fica contada e
    // pendente, e o CPU recusa a instrucao com o endereco (ver o Passo).
    ++leituras_nao_mapeadas_;
    if (!leitura_nao_mapeada_pendente_) {
      leitura_nao_mapeada_pendente_ = true;
      endereco_da_leitura_nao_mapeada_pendente_ = a;
      // QUEM LEU, no proprio acto da leitura: o PC que o `Passo` declarou. Uma
      // leitura feita entre passos (o hospedeiro) fica com o PC da ultima
      // instrucao do guest a correr -- que e quem a provocou, no caso medido.
      pc_da_leitura_nao_mapeada_pendente_ = pc_;
      leitura_pendente_.leitor_host = leitor_host_;
      leitura_pendente_.r0 = leitor_r0_; leitura_pendente_.r1 = leitor_r1_; leitura_pendente_.lr = leitor_lr_;
    }
    return 0;
  }
  return it->second[a & kMascaraPagina];
}

std::uint16_t Memoria::Ler16(Endereco a) const {
  return static_cast<std::uint16_t>(Ler8(a)) |
         static_cast<std::uint16_t>(static_cast<std::uint16_t>(Ler8(a + 1)) << 8);
}

std::uint32_t Memoria::Ler32(Endereco a) const {
  // A SONDA DE LEITURA fica so aqui, no acesso de 32 bits: um ponteiro do guest
  // (`pBmp`, `pPaletteMap`, uma vtable) le-se sempre com `ldr`. Por em `Ler8`
  // custaria uma comparacao por byte em todo o emulador.
  if (sonda_fim_ != 0 && a >= sonda_inicio_ && a < sonda_fim_) {
    bool ja = false;
    for (const auto& par : leituras_sondadas_) {
      if (par.first == a && par.second == pc_) { ja = true; break; }
    }
    if (!ja) leituras_sondadas_.emplace_back(a, pc_);
  }
  return static_cast<std::uint32_t>(Ler8(a)) |
         (static_cast<std::uint32_t>(Ler8(a + 1)) << 8) |
         (static_cast<std::uint32_t>(Ler8(a + 2)) << 16) |
         (static_cast<std::uint32_t>(Ler8(a + 3)) << 24);
}

void Memoria::LerBloco(Endereco a, void* destino, std::uint32_t quantos) const {
  // Leitura em bloco, uma procura de pagina por trecho contiguo. Foi a
  // optimizacao que subiu o Zeebulator de 16,31 para 33,38 quadros por segundo
  // (conversao de 86% para 17-21%), e aqui nasce feita em vez de descoberta
  // depois.
  auto* d = static_cast<std::uint8_t*>(destino);
  std::uint32_t feito = 0;
  while (feito < quantos) {
    const Endereco atual = a + feito;
    const std::uint32_t n = atual >> kPaginaBits;
    const std::uint32_t dentro = atual & kMascaraPagina;
    const std::uint32_t neste = std::min(kPagina - dentro, quantos - feito);
    auto it = paginas_.find(n);
    if (it == paginas_.end()) {
      std::memset(d + feito, 0, neste);
    } else {
      std::memcpy(d + feito, it->second.get() + dentro, neste);
    }
    feito += neste;
  }
}

void Memoria::Escrever8(Endereco a, std::uint8_t v) {
  if (!autor_.empty()) {
    // Ha um escritor declarado; escrever a partir de outro sitio e registado.
    // Nao ha aqui como saber "de que thread" sem custo, por isso a marcacao e
    // explicita: quem escreve sem se ter declarado usa `EscreverBruto`.
    ++vigiados_ja_vistos_;  // contador de escritas, usado pelos testes
  } else {
    ++escritas_outro_autor_;
  }
  std::uint8_t* p = Pagina(a, true);
  p[a & kMascaraPagina] = v;

  if (faixa_fim_ != 0 && a >= faixa_inicio_ && a < faixa_fim_) {
    if (a < sujo_min_) sujo_min_ = a;
    if (a + 1 > sujo_max_) sujo_max_ = a + 1;
  }

  for (const Vigia& g : vigias_) {
    if (a < g.inicio || a >= g.fim) continue;
    // Primeira escrita por endereco apenas: a vigia responde a pergunta
    // "alguem escreveu aqui?", e a resposta nao muda com o numero de vezes.
    bool ja = false;
    for (const auto& par : escritas_vigiadas_) {
      if (par.first == a) { ja = true; break; }
    }
    if (!ja) {
      escritas_vigiadas_.emplace_back(a, pc_);
      if (traco_ != nullptr) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "endereco=0x%08x pc=0x%08x", a, pc_);
        traco_->Emitir(Area::Memoria, Nivel::Depuracao, "VIGIA: " + g.rotulo, buf);
      }
    }
  }
}

void Memoria::Escrever16(Endereco a, std::uint16_t v) {
  Escrever8(a, static_cast<std::uint8_t>(v & 0xFF));
  Escrever8(a + 1, static_cast<std::uint8_t>((v >> 8) & 0xFF));
}

void Memoria::Escrever32(Endereco a, std::uint32_t v) {
  Escrever8(a, static_cast<std::uint8_t>(v & 0xFF));
  Escrever8(a + 1, static_cast<std::uint8_t>((v >> 8) & 0xFF));
  Escrever8(a + 2, static_cast<std::uint8_t>((v >> 16) & 0xFF));
  Escrever8(a + 3, static_cast<std::uint8_t>((v >> 24) & 0xFF));
}

void Memoria::EscreverBloco(Endereco a, const void* origem, std::uint32_t quantos) {
  // A FAIXA SUJA MARCA-SE AQUI TAMBEM. Um `memcpy` do guest (o ajudante 0x06c e
  // irmaos, `despacho.cpp`) copia sprites para o framebuffer por este caminho:
  // se so `Escrever8` sujasse, uma linha inteira copiada de uma vez passaria por
  // "o guest nao mexeu" e o desenho desaparecia em silencio.
  if (faixa_fim_ != 0 && quantos != 0 && a < faixa_fim_ && a + quantos > faixa_inicio_) {
    const Endereco i = a > faixa_inicio_ ? a : faixa_inicio_;
    const Endereco f = (a + quantos) < faixa_fim_ ? (a + quantos) : faixa_fim_;
    if (i < sujo_min_) sujo_min_ = i;
    if (f > sujo_max_) sujo_max_ = f;
  }
  const auto* o = static_cast<const std::uint8_t*>(origem);
  std::uint32_t feito = 0;
  while (feito < quantos) {
    const Endereco atual = a + feito;
    const std::uint32_t n = atual >> kPaginaBits;
    const std::uint32_t dentro = atual & kMascaraPagina;
    const std::uint32_t neste = std::min(kPagina - dentro, quantos - feito);
    std::uint8_t* p = Pagina(n << kPaginaBits, true);
    std::memcpy(p + dentro, o + feito, neste);
    feito += neste;
  }
}

void Memoria::EscreverBruto(Endereco a, const void* origem, std::uint32_t quantos) {
  // SEM MARCAR A FAIXA SUJA: e o hospedeiro a escrever a sua propria copia (o
  // carregador, a reposicao de estado, a exportacao da Tela para o ecra do
  // guest). Marcar aqui faria o motor ler de volta o que ele proprio escreveu e
  // contar isso como desenho do guest.
  const auto* o = static_cast<const std::uint8_t*>(origem);
  std::uint32_t feito = 0;
  while (feito < quantos) {
    const Endereco atual = a + feito;
    const std::uint32_t n = atual >> kPaginaBits;
    const std::uint32_t dentro = atual & kMascaraPagina;
    const std::uint32_t neste = std::min(kPagina - dentro, quantos - feito);
    std::uint8_t* p = Pagina(n << kPaginaBits, true);
    std::memcpy(p + dentro, o + feito, neste);
    feito += neste;
  }
}

void Memoria::Vigiar(const Vigia& v) { vigias_.push_back(v); }

void Memoria::PararDeVigiar() {
  vigias_.clear();
  escritas_vigiadas_.clear();
}

std::size_t Memoria::LerCadeia(Endereco a, std::string* saida, std::uint32_t limite) const {
  saida->clear();
  for (std::uint32_t i = 0; i < limite; ++i) {
    const std::uint8_t c = Ler8(a + i);
    if (c == 0) return i;
    saida->push_back(static_cast<char>(c));
  }
  return limite;
}

}  // namespace zb2
