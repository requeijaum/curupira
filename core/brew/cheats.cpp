#include "core/brew/cheats.h"

#include <cstdio>
#include <cstdlib>

#include "core/memoria/memoria.h"
#include "core/traco/traco.h"

namespace zb2::brew {

namespace {

// Le string "chave": "valor" em [de, fim). "" = nao achou AQUI.
std::string LerTexto(const std::string& s, const std::string& chave, std::size_t de,
                     std::size_t fim) {
  const std::size_t k = s.find('"' + chave + '"', de);
  if (k == std::string::npos || k >= fim) return "";
  const std::size_t v = s.find(':', k);
  if (v == std::string::npos || v >= fim) return "";
  const std::size_t a = s.find('"', v);
  if (a == std::string::npos || a >= fim) return "";
  const std::size_t b = s.find('"', a + 1);
  if (b == std::string::npos || b > fim) return "";
  return s.substr(a + 1, b - a - 1);
}

// Le numero "chave": 123 ou 0x... em [de, fim). `ok` diz se achou AQUI.
std::uint32_t LerNumero(const std::string& s, const std::string& chave, std::size_t de,
                        std::size_t fim, bool* ok) {
  *ok = false;
  const std::size_t k = s.find('"' + chave + '"', de);
  if (k == std::string::npos || k >= fim) return 0;
  const std::size_t v = s.find(':', k);
  if (v == std::string::npos || v >= fim) return 0;
  // O valor pode vir com aspas (`"0x1000"`) ou nu (`10`): pula espacos e UMA
  // aspa de abertura. Sem isto, todo endereco hexadecimal entre aspas falhava
  // e o cheat nascia com zero escritas -- foi o que 5 testes denunciaram.
  const char* ini = s.c_str() + v + 1;
  while (*ini == ' ' || *ini == '\t' || *ini == '"') ++ini;
  char* fim_num = nullptr;
  const unsigned long n = std::strtoul(ini, &fim_num, 0);
  if (fim_num == ini) return 0;
  *ok = true;
  return static_cast<std::uint32_t>(n);
}

bool FaseValida(const std::string& fase) {
  return fase == "carga" || fase == "create" || fase == "start" || fase == "quadros";
}

}  // namespace

Cheats::Cheats(Memoria& mem, Traco& traco) : mem_(mem), traco_(traco) {}

bool Cheats::LerConteudo(const std::string& json) {
  cheats_.clear();
  tem_pc_ = false;
  std::size_t p = 0;
  while ((p = json.find("\"nome\"", p)) != std::string::npos) {
    // Delimita a entrada pelo proximo `"nome"` (ou fim do texto): todos os
    // campos tem de estar DENTRO dela, senao pertencem a outra entrada.
    const std::size_t prox = json.find("\"nome\"", p + 6);
    const std::size_t fim = (prox == std::string::npos) ? json.size() : prox;
    const std::string nome = LerTexto(json, "nome", p, fim);
    const std::string titulo = LerTexto(json, "titulo", p, fim);
    const std::string fase = LerTexto(json, "fase", p, fim);
    bool tem_pc = false;
    const std::uint32_t pc = LerNumero(json, "pc", p, fim, &tem_pc);
    if (nome.empty() || (!tem_pc && !FaseValida(fase))) {
      p = fim;
      continue;
    }
    Cheat c;
    c.nome = nome;
    c.titulo = titulo;
    c.fase = fase;
    c.pc = pc;
    c.tem_pc = tem_pc;
    if (tem_pc) tem_pc_ = true;
    if (fase == "quadros") {
      bool ok = false;
      c.quadro = LerNumero(json, "quadro", p, fim, &ok);
      if (!ok) {
        p = fim;
        continue;
      }
    }
    // As escritas: cada `{"endereco": ..., "u8"|"u16"|"u32": ...}` no intervalo.
    // O tamanho sai da chave presente ENTRE este `endereco` e o proximo.
    std::size_t q = p;
    while ((q = json.find("\"endereco\"", q)) != std::string::npos && q < fim) {
      bool ok = false;
      const std::uint32_t end = LerNumero(json, "endereco", q, fim, &ok);
      if (!ok) break;
      const std::size_t po = json.find("\"endereco\"", q + 11);
      const std::size_t limite = (po == std::string::npos || po > fim) ? fim : po;
      const std::size_t k8 = json.find("\"u8\"", q);
      const std::size_t k16 = json.find("\"u16\"", q);
      const std::size_t k32 = json.find("\"u32\"", q);
      EscritaDeCheat e;
      e.endereco = end;
      if (k8 != std::string::npos && k8 < limite) {
        e.tamanho = 1;
        e.valor = LerNumero(json, "u8", q, limite, &ok);
      } else if (k16 != std::string::npos && k16 < limite) {
        e.tamanho = 2;
        e.valor = LerNumero(json, "u16", q, limite, &ok);
      } else if (k32 != std::string::npos && k32 < limite) {
        e.tamanho = 4;
        e.valor = LerNumero(json, "u32", q, limite, &ok);
      } else {
        break;
      }
      if (!ok) break;
      c.escritas.push_back(e);
      q = limite;
      if (q >= fim) break;
    }
    if (!c.escritas.empty()) cheats_.push_back(c);
    p = fim;
  }
  return true;
}

bool Cheats::LerFicheiro(const std::string& caminho) {
  std::FILE* f = std::fopen(caminho.c_str(), "rb");
  if (f == nullptr) return false;
  std::string texto;
  char buf[4096];
  std::size_t n = 0;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) texto.append(buf, n);
  std::fclose(f);
  return LerConteudo(texto);
}

void Cheats::ReporPorTitulo(const std::string& mod) {
  titulo_ = mod;
  for (auto& c : cheats_) c.aplicada = false;
}

void Cheats::Aplicar(Cheat& c) {
  for (const auto& e : c.escritas) {
    if (e.tamanho == 1) {
      mem_.Escrever8(e.endereco, static_cast<std::uint8_t>(e.valor & 0xFFu));
    } else if (e.tamanho == 2) {
      mem_.Escrever16(e.endereco, static_cast<std::uint16_t>(e.valor & 0xFFFFu));
    } else {
      mem_.Escrever32(e.endereco, e.valor);
    }
  }
  c.aplicada = true;
  char det[192];
  std::snprintf(det, sizeof(det), "%s: %u escrita(s) no guest", c.nome.c_str(),
                static_cast<unsigned>(c.escritas.size()));
  traco_.Emitir(Area::Brew, Nivel::Aviso, "CHEAT_APLICADO", det);
}

std::uint32_t Cheats::NaFase(const std::string& fase, std::uint32_t quadro) {
  std::uint32_t n = 0;
  for (auto& c : cheats_) {
    if (c.aplicada || c.tem_pc || c.fase != fase) continue;
    if (!c.titulo.empty() && c.titulo != titulo_) continue;
    if (fase == "quadros" && quadro < c.quadro) continue;
    Aplicar(c);
    ++n;
  }
  return n;
}

void Cheats::NoPasso(std::uint32_t pc) {
  if (!tem_pc_) return;
  for (auto& c : cheats_) {
    if (c.aplicada || !c.tem_pc || c.pc != pc) continue;
    if (!c.titulo.empty() && c.titulo != titulo_) continue;
    Aplicar(c);
  }
}

}  // namespace zb2::brew
