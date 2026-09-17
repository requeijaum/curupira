#include "core/brew/ajudantes.h"

#include <algorithm>
#include <cstring>

namespace zb2 {

// ---------------------------------------------------------------------------
// Alocador
// ---------------------------------------------------------------------------
Alocador::Alocador(Memoria& mem, std::uint32_t inicio, std::uint32_t tamanho, Traco* traco)
    : mem_(mem), traco_(traco), inicio_(inicio), tamanho_(tamanho) {
  // Um bloco unico cobrindo tudo, livre. O heap nasce com UM bloco: e o estado
  // mais simples que representa "nao alocado nada".
  Cabecalho c;
  c.tamanho = tamanho;
  c.livre = 1;
  Escrever(inicio_, c);
  blocos_ = 1;
}

Alocador::Cabecalho Alocador::Ler(std::uint32_t endereco) const {
  Cabecalho c;
  c.tamanho = mem_.Ler32(endereco);
  c.livre = mem_.Ler32(endereco + 4);
  c.reservado1 = mem_.Ler32(endereco + 8);
  c.reservado2 = mem_.Ler32(endereco + 12);
  return c;
}

void Alocador::Escrever(std::uint32_t endereco, const Cabecalho& c) {
  mem_.Escrever32(endereco, c.tamanho);
  mem_.Escrever32(endereco + 4, c.livre);
  mem_.Escrever32(endereco + 8, c.reservado1);
  mem_.Escrever32(endereco + 12, c.reservado2);
}

std::uint32_t Alocador::Malloc(std::uint32_t tamanho) {
  if (tamanho == 0) {
    // `malloc(0)` devolve um ponteiro unico e valido. Devolver zero faria o
    // chamador tratar como falha -- e um modulo que pede zero bytes e legitimo.
    tamanho = kAlinhamento;
  }
  const std::uint32_t precisa = (tamanho + kCabecalho + kAlinhamento - 1) & ~(kAlinhamento - 1);

  // Primeiro bloco livre que serve. Percorre por endereco, o que mantem a lista
  // ordenada sem estrutura extra.
  std::uint32_t atual = inicio_;
  const std::uint32_t fim = inicio_ + tamanho_;
  while (atual + kCabecalho <= fim) {
    const Cabecalho c = Ler(atual);
    if (c.tamanho < kCabecalho || atual + c.tamanho > fim) break;  // cabecalho corrompido
    if (c.livre != 0 && c.tamanho >= precisa) {
      const std::uint32_t sobra = c.tamanho - precisa;
      // So parte o bloco se a sobra der para um cabecalho mais o minimo.
      if (sobra >= kCabecalho + kAlinhamento) {
        Cabecalho novo;
        novo.tamanho = precisa;
        novo.livre = 0;
        Escrever(atual, novo);
        Cabecalho resto;
        resto.tamanho = sobra;
        resto.livre = 1;
        Escrever(atual + precisa, resto);
      } else {
        Cabecalho ocupado = c;
        ocupado.livre = 0;
        Escrever(atual, ocupado);
      }
      alocado_ += precisa;
      // O PICO: o `Alocado()` do fim de uma corrida diz o que SOBROU, e nao o que
      // o titulo chegou a pedir. Para responder "houve estouro de memoria?" o
      // numero que interessa e o maximo, e e ele que a bateria escreve.
      if (alocado_ > pico_) pico_ = alocado_;
      return atual + kCabecalho;
    }
    atual += c.tamanho;
  }
  ++falhas_;
  // O MAIOR PEDIDO QUE NAO COUBE, e nao so o pico: um heap com 23 MiB usados de
  // 64 pode recusar um pedido grande, e era isso que ficava invisivel. Foi o que
  // o `zeebovolley`/`footparty`/`zeeboids` mostraram.
  if (tamanho > maior_falha_) maior_falha_ = tamanho;
  return 0;
}

bool Alocador::Caberia(std::uint32_t tamanho) const {
  if (tamanho == 0) tamanho = kAlinhamento;  // a mesma regra do `Malloc`
  // A soma em 64 bits: em 32 bits um pedido como 0xFFFFFFFF ENVOLVE
  // (vira ~22) e "caberia" em qualquer bloco -- o `Malloc` tem a mesma
  // aritmetica sem guarda (defeito conhecido, impacto nao medido, fora desta
  // frente). Aqui um pedido que nem cabe em 32 bits e FALSE sem passear.
  const std::uint64_t precisa64 =
      static_cast<std::uint64_t>(tamanho) + kCabecalho + kAlinhamento - 1;
  if (precisa64 > 0xFFFFFFFFu) return false;
  const std::uint32_t precisa = static_cast<std::uint32_t>(precisa64) & ~(kAlinhamento - 1);
  std::uint32_t atual = inicio_;
  const std::uint32_t fim = inicio_ + tamanho_;
  while (atual + kCabecalho <= fim) {
    const Cabecalho c = Ler(atual);
    if (c.tamanho < kCabecalho || atual + c.tamanho > fim) break;  // a mesma guarda do `Malloc`
    if (c.livre != 0 && c.tamanho >= precisa) return true;
    atual += c.tamanho;
  }
  return false;
}

std::uint32_t Alocador::TamanhoDoBloco(std::uint32_t endereco) const {
  if (endereco == 0) return 0;
  const std::uint32_t procurado = endereco - kCabecalho;
  if (procurado < inicio_) return 0;
  std::uint32_t atual = inicio_;
  const std::uint32_t fim = inicio_ + tamanho_;
  while (atual + kCabecalho <= fim) {
    const Cabecalho c = Ler(atual);
    if (c.tamanho < kCabecalho || atual + c.tamanho > fim) return 0;  // corrompido
    if (atual == procurado) {
      if (c.livre != 0) return 0;  // livre: nao e um bloco vivo
      return c.tamanho - kCabecalho;
    }
    if (atual > procurado) return 0;  // a lista anda por ordem de endereco
    atual += c.tamanho;
  }
  return 0;
}

void Alocador::Free(std::uint32_t endereco) {
  if (endereco == 0) return;  // `free(0)` e legal e nao faz nada
  const std::uint32_t bloco = endereco - kCabecalho;
  if (bloco < inicio_ || bloco >= inicio_ + tamanho_) {
    // Um `free` de endereco que nao e nosso e um defeito de quem chama, e
    // engoli-lo esconderia corrupcao de heap. Conta-se.
    ++falhas_;
    if (traco_ != nullptr) {
      char buf[64];
      std::snprintf(buf, sizeof(buf), "endereco=0x%08x fora do heap", endereco);
      traco_->Emitir(Area::Brew, Nivel::Aviso, "FREE_FORA_DO_HEAP", buf);
    }
    return;
  }
  Cabecalho c = Ler(bloco);
  c.livre = 1;
  Escrever(bloco, c);
  alocado_ -= std::min(alocado_, c.tamanho);

  // Junta com os vizinhos livres. Sem isto o heap fragmenta e um modulo que
  // aloca e liberta em ciclo acaba sem memoria -- que e o sintoma mais caro de
  // diagnosticar depois.
  std::uint32_t atual = inicio_;
  const std::uint32_t fim = inicio_ + tamanho_;
  while (atual + kCabecalho <= fim) {
    const Cabecalho a = Ler(atual);
    if (a.tamanho < kCabecalho || atual + a.tamanho >= fim) break;
    const Cabecalho b = Ler(atual + a.tamanho);
    if (a.livre != 0 && b.livre != 0 && b.tamanho >= kCabecalho) {
      Cabecalho junto;
      junto.tamanho = a.tamanho + b.tamanho;
      junto.livre = 1;
      Escrever(atual, junto);
      continue;  // tenta juntar outra vez no mesmo sitio
    }
    atual += a.tamanho;
  }
}

std::uint32_t Alocador::Realloc(std::uint32_t endereco, std::uint32_t tamanho, bool zerar) {
  // `realloc(0, n)` e um `malloc(n)`: a regra do zero e a do `malloc`.
  if (endereco == 0) {
    const std::uint32_t p = Malloc(tamanho);
    if (p != 0 && zerar) {
      for (std::uint32_t k = 0; k < tamanho; ++k) mem_.Escrever8(p + k, 0);
    }
    return p;
  }
  if (tamanho == 0) {
    Free(endereco);
    return 0;
  }
  const std::uint32_t bloco = endereco - kCabecalho;
  const Cabecalho c = Ler(bloco);
  const std::uint32_t capacidade = c.tamanho - kCabecalho;
  if (capacidade >= tamanho) return endereco;  // ja cabe
  const std::uint32_t novo = Malloc(tamanho);
  if (novo == 0) return 0;
  // Copia o que havia. LerBloco/EscreverBloco em vez de byte a byte: foi a
  // optimizacao que mais rendeu na arvore antiga.
  std::vector<std::uint8_t> copia(capacidade);
  mem_.LerBloco(endereco, copia.data(), capacidade);
  mem_.EscreverBloco(novo, copia.data(), capacidade);
  // A PARTE NOVA: do fim do que havia ate ao tamanho pedido. Sem isto o crescimento
  // entregava ao titulo os bytes do dono anterior do bloco -- e o `malloc` desta
  // casa zera (`ALLOC_NO_ZMEM` e a bandeira que o desliga).
  if (zerar) {
    for (std::uint32_t k = capacidade; k < tamanho; ++k) mem_.Escrever8(novo + k, 0);
  }
  Free(endereco);
  return novo;
}

// ---------------------------------------------------------------------------
// Tabela de ajudantes
// ---------------------------------------------------------------------------
TabelaDeAjudantes::TabelaDeAjudantes(Memoria& mem, Alocador& alocador, Traco* traco)
    : mem_(mem), alocador_(alocador), traco_(traco) {}

void TabelaDeAjudantes::Declarar(std::uint32_t offset, const char* nome, FuncaoDeAjudante funcao) {
  slots_.push_back({offset, nome, std::move(funcao)});
}

std::size_t TabelaDeAjudantes::Implementados() const {
  std::size_t n = 0;
  for (const SlotDeAjudante& s : slots_) {
    if (s.implementado()) ++n;
  }
  return n;
}

TabelaDeAjudantes::Instalacao TabelaDeAjudantes::Instalar(std::uint32_t endereco,
                                                         bool permitir_por_implementar) {
  Instalacao r;
  for (const SlotDeAjudante& s : slots_) {
    if (!s.implementado()) r.faltam.push_back(s.nome != nullptr ? s.nome : "(sem nome)");
  }
  // A recusa esta aqui, e nao num comentario: a instalacao FALHA quando falta um
  // slot, e nomeia-os. Foi um stub que devolvia sucesso e nao fazia nada que
  // escondeu 86 377 chamadas de `glCullFace` no projeto antigo.
  if (!r.faltam.empty() && !permitir_por_implementar) {
    r.ok = false;
    return r;
  }
  for (const SlotDeAjudante& s : slots_) {
    if (!s.implementado()) continue;
    mem_.Escrever32(endereco + s.offset, 0);
  }
  r.ok = true;
  r.endereco = endereco;
  return r;
}

}  // namespace zb2
