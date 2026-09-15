#include "core/brew/ajudantes_extra.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "core/brew/formato.h"

namespace zb2::brew {

namespace {

// O cabecalho de bloco do NOSSO alocador: 16 bytes, `tamanho` (cabecalho
// incluido) no offset 0 e `livre` no offset 4. A fonte e
// `core/brew/ajudantes.h`, na declaracao `Alocador::Cabecalho` -- e este numero
// NAO foi transcrito de memoria: e o que la esta, escrito ao lado da struct.
//
// Um leitor que percorra a lista tem de a percorrer ate ao FIM EXACTO do heap.
// O `GetRAMFree` confere isso (ver `FazerGetRamFree`): se a soma dos blocos nao
// der o tamanho do heap, ele NAO devolve um numero -- um numero que nao foi
// medido e pior do que nenhum.
constexpr std::uint32_t kCabecalhoDeBloco = 16;

// Limites de sanidade. Nao sao estilo: o guest pode passar um `nSize` absurdo
// (registos residuais) e um laco que confie nele trava o emulador. O limite
// existe e a sua violacao e REGISTADA.
constexpr std::uint32_t kLimiteDeCaracteres = 1u << 20;
constexpr std::uint32_t kLimiteDeCadeia = 1u << 16;
// Uma lista de blocos maior do que isto e uma lista que nao fecha. O limite
// existe para uma travessia de um heap corrompido nao correr sem fim.
constexpr std::uint32_t kLimiteDeBlocos = 1u << 20;

// Uma falta registada com o NOME e a ASSINATURA do cabecalho. Nao existe
// recusa muda neste ficheiro: a razao vai sempre para o registo (P2).
void RegistarRecusa(Traco& traco, std::uint32_t offset, const char* porque,
                    const std::string& detalhe = "") {
  const Declaracao* d = DeclaracaoDoOffset(offset);
  char nome[128];
  if (d != nullptr) {
    std::snprintf(nome, sizeof(nome), "AEEHelperFuncs[0x%03x] %s", offset, d->nome);
  } else {
    std::snprintf(nome, sizeof(nome), "AEEHelperFuncs[0x%03x]", offset);
  }
  std::string texto = porque;
  if (d != nullptr) {
    texto += " | assinatura: ";
    texto += d->assinatura;
  }
  if (!detalhe.empty()) {
    texto += " | ";
    texto += detalhe;
  }
  traco.RegistarFalta(Area::Brew, nome, texto);
}

// O detalhe dos registos. E O MESMO TEXTO que o despacho usa no caminho
// generico (`core/brew/despacho.cpp`), de proposito: a lista de demanda agrupa
// por este par (nome, detalhe), e mudar o formato aqui mudava a lista toda.
std::string DetalheDosRegistos(ICpu& cpu) {
  char det[128];
  std::snprintf(det, sizeof(det), "r0=0x%08x r1=0x%08x r2=0x%08x", cpu.Get(kR0),
                cpu.Get(kR1), cpu.Get(kR2));
  return std::string(det);
}

void EmitirChamada(Traco& traco, std::uint32_t offset, const std::string& detalhe) {
  char nome[64];
  std::snprintf(nome, sizeof(nome), "AJUDANTE_EXTRA_0x%03x", offset);
  traco.Emitir(Area::Brew, Nivel::Depuracao, nome, detalhe);
}

// ---------------------------------------------------------------------------
// 0x044 -- `char *(*wstrtostr)(const AECHAR *pIn, char *pszDest, int nSize)`
// ---------------------------------------------------------------------------
//
// ASSINATURA, argumento a argumento, de `AEEStdLib.h` linha 75 (a linha vem do
// `.inc` gerado, e `tools/verificar_ajudantes.sh` falha se o cabecalho mudar):
//   r0 = pIn     : AECHAR* (uint16*, terminado em 0) -- `AECHAR` e `uint16`
//                  (`platform/system/inc/AEEStdDef.h` linha 114)
//   r1 = pszDest : char* -- destino
//   r2 = nSize   : int   -- bytes do destino
//   devolve: pszDest
//
// SEMANTICA, medida em DOIS sitios independentes do proprio SDK, que fazem o
// mesmo: `sck/.../oem/ishellbased/sim/OEMBREWSettings.c` (funcao
// `my_wstrtostr`, linha 363) e `sck/.../oem/OEMNet/msm/OEMPDPSettings_common.h`
// (funcao `wstrtostr_PDP`, linha 592). Nas duas: um AECHAR por byte, com
// `(char)ch`, enquanto o destino nao chegar ao fim (`nSize > 1`), e NUL no fim.
//
// `nSize <= 0` NAO converte e devolve o destino (e o que o cabecalho diz:
// "If this is 0, this function does not do any conversion, but returns pszDest").
// O `nSize` negativo e invalido no cabecalho; aqui cai no mesmo ramo, e o regista.
void FazerWstrToStr(Memoria& mem, Alocador&, ICpu& cpu, Traco& traco) {
  const std::uint32_t p_dest = cpu.Get(kR1);
  std::uint32_t p_in = cpu.Get(kR0);
  const std::int32_t n_size = static_cast<std::int32_t>(cpu.Get(kR2));

  if (p_in == 0 || p_dest == 0) {
    RegistarRecusa(traco, brew_ajudantes::kAjudante_wstrtostr,
                   "ponteiro nulo", DetalheDosRegistos(cpu));
    cpu.Set(kR0, 0);
    return;
  }
  if (n_size <= 0) {
    if (n_size < 0) {
      RegistarRecusa(traco, brew_ajudantes::kAjudante_wstrtostr,
                     "nSize negativo (o cabecalho so define 0)", DetalheDosRegistos(cpu));
    }
    cpu.Set(kR0, p_dest);
    return;
  }

  std::uint32_t p = p_dest;
  std::int32_t restante = n_size;
  std::uint32_t lidos = 0;
  while (restante > 1) {
    const std::uint16_t w = mem.Ler16(p_in);
    if (w == 0) break;
    mem.Escrever8(p, static_cast<std::uint8_t>(w & 0xFFu));
    p_in += 2;
    p += 1;
    restante -= 1;
    if (++lidos >= kLimiteDeCaracteres) {
      RegistarRecusa(traco, brew_ajudantes::kAjudante_wstrtostr,
                     "cadeia de entrada sem fim (limite de caracteres)", "");
      break;
    }
  }
  mem.Escrever8(p, 0);
  cpu.Set(kR0, p_dest);
  char det[96];
  std::snprintf(det, sizeof(det), "nSize=%d caracteres=%u", n_size, lidos);
  EmitirChamada(traco, brew_ajudantes::kAjudante_wstrtostr, det);
}

// ---------------------------------------------------------------------------
// 0x050 -- `boolean (*utf8towstr)(const byte *pszIn, int nLen, AECHAR *pszDest, int nSizeDestBytes)`
// ---------------------------------------------------------------------------
//
// ASSINATURA, de `AEEStdLib.h` linhas 80-81:
//   r0 = pszIn          : byte*   -- UTF-8, terminado em 0
//   r1 = nLen           : int     -- comprimento do que entra, em BYTES
//   r2 = pszDest        : AECHAR* -- destino
//   r3 = nSizeDestBytes : int     -- bytes do destino
//   devolve: TRUE (1) ou FALSE (0)
//
// O CABECALHO DEFINE O FRACASSO: `platform/system/inc/OEM/AEEStdLib_static.h`
// linhas 466-482 (doc do `aee_UTF8ToWStr`): "FALSE if fails ( if pSrc or pDst is
// NULL; if nSize is zero or lesser )".
//
// O QUE O CABECALHO NAO DEFINE, e a decisao fica escrita: o que fazer quando a
// conversao NAO CABE no destino. Nao truncar em silencio e a regra desta arvore
// (P2): devolve FALSE, termina em NUL o que ja estava convertido (quando ha
// espaco para o NUL) e REGISTA. Custo se estiver errado: um titulo que conte com
// truncagem perde a conversao inteira -- e o registo diz qual e o tamanho que
// faltou, que e o que permite corrigir sem adivinhar.
//
// CESU-8: um par de substitutos escrito como dois grupos de 3 bytes passa por
// dois AECHAR, que e exactamente o que o destino espera. Nao se recusa.
void FazerUtf8ToWstr(Memoria& mem, Alocador&, ICpu& cpu, Traco& traco) {
  const std::uint32_t p_in = cpu.Get(kR0);
  const std::int32_t n_len = static_cast<std::int32_t>(cpu.Get(kR1));
  const std::uint32_t p_dest = cpu.Get(kR2);
  const std::int32_t n_size = static_cast<std::int32_t>(cpu.Get(kR3));

  if (p_in == 0 || p_dest == 0 || n_size <= 0 || n_len < 0) {
    RegistarRecusa(traco, brew_ajudantes::kAjudante_utf8towstr,
                   "argumento invalido (ponteiro nulo ou nSize < 1)", DetalheDosRegistos(cpu));
    cpu.Set(kR0, 0);
    return;
  }

  const std::uint32_t max_bytes = std::min<std::uint32_t>(
      static_cast<std::uint32_t>(n_len), kLimiteDeCadeia);
  std::uint32_t escritos = 0;  // AECHAR escritos
  std::uint32_t i = 0;
  bool caber = true;
  bool invalido = false;

  while (i < max_bytes) {
    const std::uint8_t b0 = mem.Ler8(p_in + i);
    if (b0 == 0) break;
    std::uint32_t cp = 0;
    std::uint32_t quantos = 1;
    if (b0 < 0x80u) {
      cp = b0;
    } else if ((b0 & 0xE0u) == 0xC0u) {
      cp = b0 & 0x1Fu;
      quantos = 2;
    } else if ((b0 & 0xF0u) == 0xE0u) {
      cp = b0 & 0x0Fu;
      quantos = 3;
    } else if ((b0 & 0xF8u) == 0xF0u) {
      cp = b0 & 0x07u;
      quantos = 4;
    } else {
      invalido = true;
      break;
    }
    if (i + quantos > max_bytes) {  // sequencia cortada pelo fim do que entra
      invalido = true;
      break;
    }
    bool ok = true;
    for (std::uint32_t k = 1; k < quantos; ++k) {
      const std::uint8_t bk = mem.Ler8(p_in + i + k);
      if ((bk & 0xC0u) != 0x80u) {
        ok = false;
        break;
      }
      cp = (cp << 6) | (bk & 0x3Fu);
    }
    // CURTO DEMAIS E RECUSADO: um grupo de 2 bytes so pode valer >= 0x80, um de
    // 3 >= 0x800, um de 4 >= 0x10000. Sem isto, "C0 80" passaria por NUL e
    // esconderia o fim da cadeia.
    const std::uint32_t minimo = (quantos == 2) ? 0x80u : (quantos == 3) ? 0x800u : 0x10000u;
    if (!ok || (quantos > 1 && cp < minimo) || (quantos == 4 && cp > 0x10FFFFu)) {
      invalido = true;
      break;
    }
    i += quantos;

    // Codificacao em UTF-16: 1 AECHAR abaixo de 0x10000, 2 acima (substitutos).
    std::uint16_t saida[2];
    std::uint32_t quantos_saida = 1;
    if (cp < 0x10000u) {
      saida[0] = static_cast<std::uint16_t>(cp);
    } else {
      const std::uint32_t v = cp - 0x10000u;
      saida[0] = static_cast<std::uint16_t>(0xD800u + (v >> 10));
      saida[1] = static_cast<std::uint16_t>(0xDC00u + (v & 0x3FFu));
      quantos_saida = 2;
    }
    // O NUL tambem conta para o que cabe: e preciso 2 bytes para ele.
    if ((escritos + quantos_saida + 1) * 2 > static_cast<std::uint32_t>(n_size)) {
      caber = false;
      break;
    }
    for (std::uint32_t k = 0; k < quantos_saida; ++k) {
      mem.Escrever16(p_dest + (escritos + k) * 2, saida[k]);
    }
    escritos += quantos_saida;
    if (escritos >= kLimiteDeCaracteres) {
      RegistarRecusa(traco, brew_ajudantes::kAjudante_utf8towstr,
                     "cadeia de entrada sem fim (limite de caracteres)", "");
      break;
    }
  }

  if (escritos * 2 + 2 <= static_cast<std::uint32_t>(n_size)) {
    mem.Escrever16(p_dest + escritos * 2, 0);
  }
  char det[160];
  if (!caber) {
    std::snprintf(det, sizeof(det), "nao cabe: nSize=%d, convertidos=%u AECHAR", n_size,
                  escritos);
    RegistarRecusa(traco, brew_ajudantes::kAjudante_utf8towstr, "destino pequeno de mais", det);
    cpu.Set(kR0, 0);
    return;
  }
  if (invalido) {
    std::snprintf(det, sizeof(det), "UTF-8 invalido no byte %u (nLen=%d)", i, n_len);
    RegistarRecusa(traco, brew_ajudantes::kAjudante_utf8towstr, "entrada invalida", det);
    cpu.Set(kR0, 0);
    return;
  }
  std::snprintf(det, sizeof(det), "nLen=%d convertidos=%u AECHAR", n_len, escritos);
  EmitirChamada(traco, brew_ajudantes::kAjudante_utf8towstr, det);
  cpu.Set(kR0, 1);
}

// ---------------------------------------------------------------------------
// 0x138 -- `uint32 (*GetRAMFree)(uint32 *pdwTotal, uint32 *pdwLargest)`
// ---------------------------------------------------------------------------
//
// ASSINATURA, de `AEEStdLib.h` linha 179:
//   r0 = pdwTotal   : uint32* -- recebe o TAMANHO TOTAL do heap (pode ser 0)
//   r1 = pdwLargest : uint32* -- recebe o maior BLOCO LIVRE (pode ser 0)
//   devolve: memoria disponivel para o `malloc`
//
// CONTRATO do sistema, de `platform/system/inc/OEM/AEE_OEMHeap.h` linhas 484-500
// (doc do `AEEHeap_GetRAMFree`): "pdwTotal: total size of the heap. pdwMax: size
// of the largest free node. Return: Total memory available to malloc".
//
// O QUE E "A MEMORIA DO SISTEMA" NUM EMULADOR: o heap que NOS damos ao modulo --
// `Alocador`, o mesmo que serve o `malloc` (0x68) e o `free` (0x6c). Devolver um
// numero inventado aqui seria a mentira mais cara deste ficheiro: um titulo
// decide o que aloca por este valor, e a partir do valor errado o `malloc`
// comeca a falhar num sitio que nao tem nada a ver com a causa.
//
// A recusa: se a lista de blocos NAO somar exactamente o tamanho do heap, a
// medicao falhou. Nesse caso escreve-se 0 nos dois ponteiros, devolve-se 0
// ("nao ha memoria disponivel", que faz o chamador recusar em vez de assumir) e
// REGISTA-SE. **Um numero que nao foi medido nao se devolve.**
//
// A TRAVESSIA E UM PONTO SO DE DECISAO, e isso foi medido: a primeira versao
// tinha tres `if` soltos (bloco impossivel, bloco fora do heap, soma que nao
// fecha), e arrancar o da soma final NAO fez teste nenhum ficar vermelho -- a
// verificacao por bloco apanhava o mesmo caso primeiro, e a "guarda" nao era
// guarda nenhuma. **Uma guarda que se pode arrancar sem um teste ficar vermelho
// nao esta provada.** Aqui o resultado da travessia inteira e ESTE booleano, e a
// violacao deliberada que o forca a `true` poe o teste vermelho.
bool PercorrerHeap(const Memoria& mem, const Alocador& al, std::uint32_t* livre,
                   std::uint32_t* maior) {
  std::uint32_t andado = 0;
  std::uint32_t soma_livre = 0;
  std::uint32_t o_maior = 0;
  std::uint32_t blocos = 0;
  while (andado < al.Tamanho()) {
    const std::uint32_t inicio = al.Inicio() + andado;
    const std::uint32_t tamanho = mem.Ler32(inicio);
    // Um bloco que comeca antes do fim tem de ACABAR dentro do heap e de ter
    // espaco para o proprio cabecalho.
    if (tamanho < kCabecalhoDeBloco || andado + tamanho > al.Tamanho()) return false;
    if (mem.Ler32(inicio + 4) != 0) {
      const std::uint32_t util = tamanho - kCabecalhoDeBloco;
      soma_livre += util;
      if (util > o_maior) o_maior = util;
    }
    andado += tamanho;
    if (++blocos > kLimiteDeBlocos) return false;
  }
  if (andado != al.Tamanho()) return false;
  *livre = soma_livre;
  *maior = o_maior;
  return true;
}

void FazerGetRamFree(Memoria& mem, Alocador& al, ICpu& cpu, Traco& traco) {
  const std::uint32_t p_total = cpu.Get(kR0);
  const std::uint32_t p_maior = cpu.Get(kR1);

  std::uint32_t livre = 0;
  std::uint32_t maior = 0;
  const bool ok = PercorrerHeap(mem, al, &livre, &maior);

  char det[160];
  if (!ok) {
    std::snprintf(det, sizeof(det), "a lista de blocos do heap NAO fecha (tamanho=%u)",
                  al.Tamanho());
    RegistarRecusa(traco, brew_ajudantes::kAjudante_GetRAMFree,
                   "medicao do heap falhou", det);
    if (p_total != 0) mem.Escrever32(p_total, 0);
    if (p_maior != 0) mem.Escrever32(p_maior, 0);
    cpu.Set(kR0, 0);
    return;
  }
  if (p_total != 0) mem.Escrever32(p_total, al.Tamanho());
  if (p_maior != 0) mem.Escrever32(p_maior, maior);
  cpu.Set(kR0, livre);
  std::snprintf(det, sizeof(det), "livre=%u total=%u maior=%u", livre, al.Tamanho(),
                maior);
  EmitirChamada(traco, brew_ajudantes::kAjudante_GetRAMFree, det);
}

// ---------------------------------------------------------------------------
// 0x0d8 `strstr` e 0x0e8 `stristr` -- AS DUAS QUE JA SE TROCARAM AQUI
// ---------------------------------------------------------------------------
//
// ASSINATURAS, de `AEEStdLib.h` linhas 153 e 157:
//   char *(*strstr)(const char *haystack, const char *needle)             0x0d8
//   char *(*stristr)(const char *cpszHaystack, const char *cpszNeedle)    0x0e8
//
// O que distingue as duas e SO a caixa: `strstr` e sensivel, `stristr` nao.
// Nesta arvore elas ja estiveram trocadas -- `kStrstrSlotOffset` guardava o
// offset do `stristr`, e tres testes chamados `Strstr*` testavam o `stristr`
// (um deles passava por acidente). Por isso as duas implementacoes e os testes
// das duas vivem juntos e separados: um teste que compara as duas num caso onde
// ELAS DISCORDAM e a unica prova de que nao estao trocadas.
//
// Estas duas NAO estavam implementadas nesta arvore: caíam no ramo generico e
// devolviam AEE_EUNSUPPORTED, o que faz um `strstr` do jogo devolver NULL e
// seguir por um caminho falso sem sintoma.
std::uint32_t ProcurarSubcadeia(const Memoria& mem, std::uint32_t p_palheiro,
                                std::uint32_t p_agulha, bool sem_caixa) {
  if (p_palheiro == 0 || p_agulha == 0) return 0;
  std::string palheiro, agulha;
  mem.LerCadeia(p_palheiro, &palheiro, kLimiteDeCadeia);
  mem.LerCadeia(p_agulha, &agulha, kLimiteDeCadeia);
  if (agulha.empty()) return p_palheiro;  // como a libc: agulha vazia casa em todo o lado
  if (palheiro.size() < agulha.size()) return 0;
  // ASCII apenas. NAO se usa a locale do hospedeiro: `tolower` do C depende da
  // locale, e a locale e ambiente do PROCESSO -- uma segunda fonte de variacao
  // que o P4 proibe (o mesmo titulo com a mesma entrada tem de dar o mesmo).
  const auto igual = [sem_caixa](char a, char b) {
    if (!sem_caixa) return a == b;
    const auto baixa = [](char c) {
      return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    };
    return baixa(a) == baixa(b);
  };
  for (std::size_t i = 0; i + agulha.size() <= palheiro.size(); ++i) {
    std::size_t k = 0;
    while (k < agulha.size() && igual(palheiro[i + k], agulha[k])) ++k;
    if (k == agulha.size()) return p_palheiro + static_cast<std::uint32_t>(i);
  }
  return 0;
}

void FazerStrstr(Memoria& mem, Alocador&, ICpu& cpu, Traco& traco) {
  const std::uint32_t palheiro = cpu.Get(kR0);
  const std::uint32_t agulha = cpu.Get(kR1);
  const std::uint32_t r = ProcurarSubcadeia(mem, palheiro, agulha, false);
  cpu.Set(kR0, r);
  char det[96];
  std::snprintf(det, sizeof(det), "palheiro=0x%08x agulha=0x%08x -> 0x%08x", palheiro,
                agulha, r);
  EmitirChamada(traco, brew_ajudantes::kAjudante_strstr, det);
}

void FazerStristr(Memoria& mem, Alocador&, ICpu& cpu, Traco& traco) {
  const std::uint32_t r = ProcurarSubcadeia(mem, cpu.Get(kR0), cpu.Get(kR1), true);
  cpu.Set(kR0, r);
  char det[96];
  std::snprintf(det, sizeof(det), "sem caixa, -> 0x%08x", r);
  EmitirChamada(traco, brew_ajudantes::kAjudante_stristr, det);
}

// ---------------------------------------------------------------------------
// 0x0F4 -- `char *(*strdup)(const char *psz)` (AEEStdLib.h linha 159)
// ---------------------------------------------------------------------------
//
// r0 = psz; devolve copia no heap do Alocador, ou NULL.
// Semantica libc: malloc(strlen+1), copia com NUL.
void FazerStrdup(Memoria& mem, Alocador& al, ICpu& cpu, Traco& traco) {
  const std::uint32_t p_in = cpu.Get(kR0);
  if (p_in == 0) {
    RegistarRecusa(traco, brew_ajudantes::kAjudante_strdup, "ponteiro nulo",
                   DetalheDosRegistos(cpu));
    cpu.Set(kR0, 0);
    return;
  }
  std::string s;
  const std::size_t n = mem.LerCadeia(p_in, &s, kLimiteDeCadeia);
  if (n == kLimiteDeCadeia && mem.Ler8(p_in + static_cast<std::uint32_t>(n)) != 0) {
    RegistarRecusa(traco, brew_ajudantes::kAjudante_strdup,
                   "cadeia de entrada sem fim (limite de caracteres)",
                   DetalheDosRegistos(cpu));
    cpu.Set(kR0, 0);
    return;
  }
  const std::uint32_t saida = al.Malloc(static_cast<std::uint32_t>(n + 1));
  if (saida == 0) {
    char det[96];
    std::snprintf(det, sizeof(det), "sem memoria: n=%zu", n);
    RegistarRecusa(traco, brew_ajudantes::kAjudante_strdup, "sem memoria", det);
    cpu.Set(kR0, 0);
    return;
  }
  if (!s.empty()) mem.EscreverBloco(saida, s.data(), static_cast<std::uint32_t>(n));
  mem.Escrever8(saida + static_cast<std::uint32_t>(n), 0);
  cpu.Set(kR0, saida);
  char det[96];
  std::snprintf(det, sizeof(det), "n=%zu -> 0x%08x", n, saida);
  EmitirChamada(traco, brew_ajudantes::kAjudante_strdup, det);
}

// ---------------------------------------------------------------------------
// A LISTA DAS IMPLEMENTACOES
// ---------------------------------------------------------------------------
//
// UMA lista, e o `Atender` percorre-a: nao ha um segundo sitio a dizer quem esta
// implementado. **Duas listas que tem de concordar sao zero listas** -- nesta
// arvore isso ja custou uma ronda, quando o laco de preenchimento dos 117 slots
// apagou um `aee_GetUpTimeMS` que estava escrito e a funcionar.
struct Implementacao {
  std::uint32_t offset;
  const char* nome;
  void (*funcao)(Memoria&, Alocador&, ICpu&, Traco&);
};

// 0x0CC -- `int (*strncmp)(const char *a, const char *b, size_t length)`
// (AEEStdLib.h). r0=a, r1=b, r2=n. Compara ate n ou ao primeiro NUL.
void FazerStrncmp(Memoria& mem, Alocador&, ICpu& cpu, Traco& traco) {
  const std::uint32_t a = cpu.Get(kR0);
  const std::uint32_t b = cpu.Get(kR1);
  const std::uint32_t n = cpu.Get(kR2);
  std::int32_t r = 0;
  if (a == 0 || b == 0) {
    RegistarRecusa(traco, brew_ajudantes::kAjudante_strncmp, "ponteiro nulo",
                   DetalheDosRegistos(cpu));
    cpu.Set(kR0, 0);
    return;
  }
  for (std::uint32_t i = 0; i < n; ++i) {
    const std::uint8_t ca = mem.Ler8(a + i);
    const std::uint8_t cb = mem.Ler8(b + i);
    if (ca != cb) {
      r = static_cast<std::int32_t>(ca) - static_cast<std::int32_t>(cb);
      break;
    }
    if (ca == 0) break;
  }
  cpu.Set(kR0, static_cast<std::uint32_t>(r));
  char det[96];
  std::snprintf(det, sizeof(det), "n=%u -> %d", n, r);
  EmitirChamada(traco, brew_ajudantes::kAjudante_strncmp, det);
}


// ---------------------------------------------------------------------------
// A FRENTE io2 (etapa 12): os seis ajudantes que a corrida fmg mediu como os
// mais pedidos depois da ronda anterior. Assinaturas em `AEEStdLib.h` e nomes
// em `tools/ajudantes_slots.inc` (gerado, ancoras conferidas no fim).
// ---------------------------------------------------------------------------

// 0x030 -- `int (*wstrlen)(const AECHAR *p)`. Conta AECHAR (unidades de 16
// bits) ate ao NUL; um byte a mais nao pode entrar na conta (SDK: "the number
// of AECHAR characters in string, excluding the terminal NULL").
void FazerWstrlen(Memoria& mem, Alocador&, ICpu& cpu, Traco& traco) {
  const std::uint32_t p_in = cpu.Get(kR0);
  if (p_in == 0) {
    RegistarRecusa(traco, brew_ajudantes::kAjudante_wstrlen, "ponteiro nulo",
                   DetalheDosRegistos(cpu));
    cpu.Set(kR0, 0);
    return;
  }
  std::uint32_t n = 0;
  while (mem.Ler16(p_in + n * 2) != 0) {
    if (++n >= kLimiteDeCaracteres) {
      RegistarRecusa(traco, brew_ajudantes::kAjudante_wstrlen,
                     "cadeia larga sem fim (limite de caracteres)", "");
      cpu.Set(kR0, 0);
      return;
    }
  }
  cpu.Set(kR0, n);
  char det[48];
  std::snprintf(det, sizeof(det), "AECHAR=%u", n);
  EmitirChamada(traco, brew_ajudantes::kAjudante_wstrlen, det);
}

// 0x080 -- `int (*wstrncopyn)(AECHAR *pszDest, int cbDest, const AECHAR *pszSource, int lenSource)`.
//
// O SDK declara por extenso: `cbDest` e a capacidade do destino EM AECHAR; o
// destino e SEMPRE terminado a NUL; `lenSource` nao inclui o NUL, e -1 copia a
// origem inteira; devolve o numero de AECHAR copiados. Medido no chessbots
// (1x): destino na pilha (0x802001c6), cbDest=0x200=512, origem numa cadeia
// larga de zeros dentro do modulo.
void FazerWstrncopyn(Memoria& mem, Alocador&, ICpu& cpu, Traco& traco) {
  const std::uint32_t p_dest = cpu.Get(kR0);
  const std::int32_t cb_dest = static_cast<std::int32_t>(cpu.Get(kR1));
  const std::uint32_t p_src = cpu.Get(kR2);
  const std::int32_t len_source = static_cast<std::int32_t>(cpu.Get(kR3));
  if (p_dest == 0 || p_src == 0 || cb_dest <= 0) {
    RegistarRecusa(traco, brew_ajudantes::kAjudante_wstrncopyn,
                   "ponteiro nulo ou cbDest < 1", DetalheDosRegistos(cpu));
    cpu.Set(kR0, 0);
    return;
  }
  const std::uint32_t limite = static_cast<std::uint32_t>(cb_dest) - 1u;  // o NUL final
  std::uint32_t copiados = 0;
  while ((len_source < 0 || static_cast<std::int32_t>(copiados) < len_source) &&
         copiados < limite) {
    const std::uint16_t w = mem.Ler16(p_src + copiados * 2);
    if (w == 0) break;
    mem.Escrever16(p_dest + copiados * 2, w);
    ++copiados;
    if (copiados >= kLimiteDeCaracteres) {
      RegistarRecusa(traco, brew_ajudantes::kAjudante_wstrncopyn,
                     "cadeia de entrada sem fim (limite de caracteres)", "");
      cpu.Set(kR0, 0);
      return;
    }
  }
  mem.Escrever16(p_dest + copiados * 2, 0);
  cpu.Set(kR0, copiados);
  char det[96];
  std::snprintf(det, sizeof(det), "cbDest=%d lenSource=%d AECHAR=%u", cb_dest, len_source,
                copiados);
  EmitirChamada(traco, brew_ajudantes::kAjudante_wstrncopyn, det);
}

// 0x0C4 -- `uint32 (*strtoul)(const char *nptr, char **endptr, int base)`.
//
// O SDK diz que e "a wrapper around the standard C library function strtoul()",
// logo as regras sao as da libc: espacos iniciais, sinal, prefixo 0x/0 quando
// base 0, paragem no primeiro caracter que nao e do numero, endptr = onde
// parou (nptr quando nada converteu), 0 sem conversao.
void FazerStrtoul(Memoria& mem, Alocador&, ICpu& cpu, Traco& traco) {
  const std::uint32_t p_in = cpu.Get(kR0);
  const std::uint32_t p_end = cpu.Get(kR1);
  const std::int32_t base = static_cast<std::int32_t>(cpu.Get(kR2));
  if (p_in == 0) {
    RegistarRecusa(traco, brew_ajudantes::kAjudante_strtoul, "ponteiro nulo",
                   DetalheDosRegistos(cpu));
    cpu.Set(kR0, 0);
    return;
  }
  const auto byte = [&](std::uint32_t pos) -> std::uint8_t {
    return mem.Ler8(p_in + pos);
  };
  std::uint32_t i = 0;
  while (byte(i) == ' ' || byte(i) == '\t' || byte(i) == '\n' || byte(i) == '\v' ||
         byte(i) == '\f' || byte(i) == '\r') {
    ++i;
  }
  bool negativo = false;
  if (byte(i) == '+' || byte(i) == '-') {
    negativo = (byte(i) == '-');
    ++i;
  }
  int b = base;
  if (b == 0) {
    if (byte(i) == '0' && (byte(i + 1) == 'x' || byte(i + 1) == 'X')) {
      b = 16;
      i += 2;
    } else if (byte(i) == '0') {
      b = 8;
    } else {
      b = 10;
    }
  } else if (b == 16 && byte(i) == '0' && (byte(i + 1) == 'x' || byte(i + 1) == 'X')) {
    i += 2;
  }
  if (b < 2 || b > 36) {
    b = 0;  // "invalid base": nada converte (a libc devolve 0 e endptr=nptr)
    i = 0;
  }
  std::uint64_t valor = 0;
  bool algum_digito = false;
  for (;;) {
    const std::uint8_t c = byte(i);
    std::uint32_t d = 0;
    if (c >= '0' && c <= '9') d = c - '0';
    else if (c >= 'a' && c <= 'z') d = c - 'a' + 10;
    else if (c >= 'A' && c <= 'Z') d = c - 'A' + 10;
    else break;
    if (d >= static_cast<std::uint32_t>(b)) break;
    algum_digito = true;
    valor = valor * static_cast<std::uint64_t>(b) + d;
    if (valor > 0xFFFFFFFFull) valor = 0xFFFFFFFFull;  // satura como ULONG_MAX
    ++i;
    if (i >= kLimiteDeCadeia) {
      RegistarRecusa(traco, brew_ajudantes::kAjudante_strtoul,
                     "numero sem fim (limite de caracteres)", "");
      cpu.Set(kR0, 0);
      return;
    }
  }
  if (!algum_digito) i = 0;  // endptr = nptr, e nada se converteu
  if (p_end != 0) mem.Escrever32(p_end, p_in + i);
  std::uint32_t r = static_cast<std::uint32_t>(valor);
  if (negativo) r = static_cast<std::uint32_t>(-static_cast<std::int64_t>(r));
  cpu.Set(kR0, r);
  char det[96];
  std::snprintf(det, sizeof(det), "base=%d valor=%u", base, r);
  EmitirChamada(traco, brew_ajudantes::kAjudante_strtoul, det);
}

// 0x144 -- `int32 (*snprintf)(char *buf, uint32 f, const char *format, ...)`.
// Igual ao `sprintf` (0x020) com o limite `f` no r1 e o formato no r2; os
// varargs comecam no r3 (AAPCS). O MEDIDO no allstarcards: formato
// "udata/settings%d.dat" com limite 32.
void FazerSnprintf(Memoria& mem, Alocador&, ICpu& cpu, Traco& traco) {
  const std::uint32_t pbuf = cpu.Get(kR0);
  const std::uint32_t limite = cpu.Get(kR1);
  const std::uint32_t pfmt = cpu.Get(kR2);
  if (pbuf == 0 || pfmt == 0) {
    RegistarRecusa(traco, brew_ajudantes::kAjudante_snprintf, "ponteiro nulo",
                   DetalheDosRegistos(cpu));
    cpu.Set(kR0, 0);
    return;
  }
  const std::uint32_t sp = cpu.Get(kSP);
  std::uint32_t args[8];
  int n = 0;
  args[n++] = cpu.Get(kR3);
  for (int i = 0; i < 7; ++i) {
    args[n++] = mem.Ler32(sp + static_cast<std::uint32_t>(i) * 4);
  }
  cpu.Set(kR0, Formatar(mem, pbuf, pfmt, args, n, limite));
  char det[96];
  std::snprintf(det, sizeof(det), "buf=0x%08x f=%u fmt=0x%08x", pbuf, limite, pfmt);
  EmitirChamada(traco, brew_ajudantes::kAjudante_snprintf, det);
}

// 0x14C -- `size_t (*strlcpy)(char *dst, const char *src, size_t nSize)`.
// O SDK: "guarantee that it is NULL terminated"; devolve o comprimento da
// cadeia que se TENTOU copiar (para se saber se truncou); (size_t)-1 para erro.
void FazerStrlcpy(Memoria& mem, Alocador&, ICpu& cpu, Traco& traco) {
  const std::uint32_t p_dst = cpu.Get(kR0);
  const std::uint32_t p_src = cpu.Get(kR1);
  const std::uint32_t n_size = cpu.Get(kR2);
  if (p_dst == 0 || p_src == 0) {
    RegistarRecusa(traco, brew_ajudantes::kAjudante_strlcpy, "ponteiro nulo",
                   DetalheDosRegistos(cpu));
    cpu.Set(kR0, 0xFFFFFFFFu);  // (size_t)-1, o erro que o SDK declara
    return;
  }
  std::uint32_t comprimento = 0;
  while (mem.Ler8(p_src + comprimento) != 0) {
    if (++comprimento >= kLimiteDeCadeia) {
      RegistarRecusa(traco, brew_ajudantes::kAjudante_strlcpy,
                     "origem sem fim (limite de caracteres)", "");
      cpu.Set(kR0, 0xFFFFFFFFu);
      return;
    }
  }
  if (n_size > 0) {
    const std::uint32_t copiar = (comprimento < n_size - 1) ? comprimento : (n_size - 1);
    for (std::uint32_t k = 0; k < copiar; ++k) {
      mem.Escrever8(p_dst + k, mem.Ler8(p_src + k));
    }
    mem.Escrever8(p_dst + copiar, 0);
  }
  cpu.Set(kR0, comprimento);
  char det[96];
  std::snprintf(det, sizeof(det), "nSize=%u comprimento=%u", n_size, comprimento);
  EmitirChamada(traco, brew_ajudantes::kAjudante_strlcpy, det);
}

// 0x150 -- `size_t (*strlcat)(char *dst, const char *src, size_t nSize)`.
// Devolve o comprimento que se TENTOU criar (dst + src); (size_t)-1 para erro.
void FazerStrlcat(Memoria& mem, Alocador&, ICpu& cpu, Traco& traco) {
  const std::uint32_t p_dst = cpu.Get(kR0);
  const std::uint32_t p_src = cpu.Get(kR1);
  const std::uint32_t n_size = cpu.Get(kR2);
  if (p_dst == 0 || p_src == 0) {
    RegistarRecusa(traco, brew_ajudantes::kAjudante_strlcat, "ponteiro nulo",
                   DetalheDosRegistos(cpu));
    cpu.Set(kR0, 0xFFFFFFFFu);
    return;
  }
  std::uint32_t fim = 0;
  while (fim < n_size && mem.Ler8(p_dst + fim) != 0) {
    ++fim;
    if (fim > kLimiteDeCadeia) {
      RegistarRecusa(traco, brew_ajudantes::kAjudante_strlcat,
                     "destino sem fim (limite de caracteres)", "");
      cpu.Set(kR0, 0xFFFFFFFFu);
      return;
    }
  }
  if (fim >= n_size) {
    // O destino ja encheu o buffer: nada se acrescenta, e o comprimento
    // TENTADO continua a ser fim + strlen(src).
    std::uint32_t resto = 0;
    while (mem.Ler8(p_src + resto) != 0) {
      if (++resto >= kLimiteDeCadeia) break;
    }
    cpu.Set(kR0, fim + resto);
    return;
  }
  // Copia o que couber de src a partir de `fim`, termina, e conta o total.
  std::uint32_t lidos = 0;
  const std::uint32_t espaco = n_size - fim;  // inclui o NUL final
  while (lidos + 1 < espaco) {
    const std::uint8_t c = mem.Ler8(p_src + lidos);
    if (c == 0) break;
    mem.Escrever8(p_dst + fim + lidos, c);
    ++lidos;
    if (lidos >= kLimiteDeCadeia) {
      RegistarRecusa(traco, brew_ajudantes::kAjudante_strlcat,
                     "origem sem fim (limite de caracteres)", "");
      cpu.Set(kR0, 0xFFFFFFFFu);
      return;
    }
  }
  mem.Escrever8(p_dst + fim + lidos, 0);
  std::uint32_t total = fim + lidos;
  // O comprimento TENTADO inclui o resto da origem que nao coube.
  while (mem.Ler8(p_src + lidos) != 0) {
    ++lidos;
    if (lidos > kLimiteDeCadeia) break;
  }
  total = fim + lidos;
  cpu.Set(kR0, total);
  char det[96];
  std::snprintf(det, sizeof(det), "nSize=%u total=%u", n_size, total);
  EmitirChamada(traco, brew_ajudantes::kAjudante_strlcat, det);
}

constexpr Implementacao kImplementados[] = {
    {brew_ajudantes::kAjudante_strstr, "strstr", FazerStrstr},
    {brew_ajudantes::kAjudante_wstrlen, "wstrlen", FazerWstrlen},
    {brew_ajudantes::kAjudante_wstrncopyn, "wstrncopyn", FazerWstrncopyn},
    {brew_ajudantes::kAjudante_strtoul, "strtoul", FazerStrtoul},
    {brew_ajudantes::kAjudante_snprintf, "snprintf", FazerSnprintf},
    {brew_ajudantes::kAjudante_strlcpy, "strlcpy", FazerStrlcpy},
    {brew_ajudantes::kAjudante_strlcat, "strlcat", FazerStrlcat},
    {brew_ajudantes::kAjudante_wstrtostr, "wstrtostr", FazerWstrToStr},
    {brew_ajudantes::kAjudante_utf8towstr, "utf8towstr", FazerUtf8ToWstr},
    {brew_ajudantes::kAjudante_stristr, "stristr", FazerStristr},
    {brew_ajudantes::kAjudante_GetRAMFree, "GetRAMFree", FazerGetRamFree},
    {brew_ajudantes::kAjudante_strdup, "strdup", FazerStrdup},
    {brew_ajudantes::kAjudante_strncmp, "strncmp", FazerStrncmp},
};

// AS ANCORAS DA LISTA, verificadas em tempo de COMPILACAO. Cada uma e um offset
// que tem de valer o que o cabecalho diz: se alguem reordenar o `.inc`, isto
// deixa de compilar -- e nao se descobre com uma bateria de 25 segundos por
// titulo.
static_assert(brew_ajudantes::kAjudante_strstr == 0x0D8, "0x0d8 e strstr, nao stristr");
static_assert(brew_ajudantes::kAjudante_stristr == 0x0E8, "0x0e8 e stristr");
static_assert(brew_ajudantes::kAjudante_strtowstr == 0x040, "0x040 e strtowstr");
static_assert(brew_ajudantes::kAjudante_wstrcompress == 0x0A0, "0x0a0 e wstrcompress");
static_assert(brew_ajudantes::kAjudante_atoi == 0x090, "0x090 e atoi");
static_assert(brew_ajudantes::kAjudante_aee_GetRand == 0x0A8, "0x0a8 e aee_GetRand");
static_assert(brew_ajudantes::kAjudante_sprintf == 0x020, "0x020 e sprintf");
static_assert(brew_ajudantes::kAjudante_vsprintf == 0x13C, "0x13c e vsprintf");
static_assert(brew_ajudantes::kAjudante_wstrtostr == 0x044, "0x044 e wstrtostr");
static_assert(brew_ajudantes::kAjudante_utf8towstr == 0x050, "0x050 e utf8towstr");
static_assert(brew_ajudantes::kAjudante_GetRAMFree == 0x138, "0x138 e GetRAMFree");
static_assert(brew_ajudantes::kAjudante_strdup == 0x0F4, "0x0f4 e strdup");
static_assert(brew_ajudantes::kAjudante_strncmp == 0x0CC, "0x0cc e strncmp");
static_assert(brew_ajudantes::kAjudante_wstrlen == 0x030, "0x030 e wstrlen");
static_assert(brew_ajudantes::kAjudante_wstrncopyn == 0x080, "0x080 e wstrncopyn, nao strncpy");
static_assert(brew_ajudantes::kAjudante_strtoul == 0x0C4, "0x0c4 e strtoul");
static_assert(brew_ajudantes::kAjudante_snprintf == 0x144, "0x144 e snprintf");
static_assert(brew_ajudantes::kAjudante_strlcpy == 0x14C, "0x14c e strlcpy");
static_assert(brew_ajudantes::kAjudante_strlcat == 0x150, "0x150 e strlcat");
static_assert(
    sizeof(kImplementados) / sizeof(kImplementados[0]) ==
        13,
    "a lista das implementacoes mudou: actualiza o numero e o teste");

}  // namespace

const Declaracao* CatalogoDosAjudantes(std::size_t* quantos) {
  if (quantos != nullptr) *quantos = brew_ajudantes::kQuantos;
  return brew_ajudantes::kDeclaracoes;
}

const Declaracao* DeclaracaoDoOffset(std::uint32_t offset) {
  if (offset % brew_ajudantes::kPassoBytes != 0) return nullptr;
  const std::uint32_t i = offset / brew_ajudantes::kPassoBytes;
  if (i >= brew_ajudantes::kQuantos) return nullptr;
  return &brew_ajudantes::kDeclaracoes[i];
}

const char* NomeDoAjudanteDoSdk(std::uint32_t offset) {
  const Declaracao* d = DeclaracaoDoOffset(offset);
  return (d == nullptr) ? nullptr : d->nome;
}

const char* AssinaturaDoAjudante(std::uint32_t offset) {
  const Declaracao* d = DeclaracaoDoOffset(offset);
  return (d == nullptr) ? nullptr : d->assinatura;
}

AjudantesExtra::AjudantesExtra(Memoria& mem, Alocador& alocador, Traco& traco)
    : mem_(mem), al_(alocador), traco_(traco) {}

std::size_t AjudantesExtra::Implementados() {
  return sizeof(kImplementados) / sizeof(kImplementados[0]);
}

Atendimento AjudantesExtra::Atender(ICpu& cpu, std::uint32_t offset) {
  for (const Implementacao& impl : kImplementados) {
    if (impl.offset != offset) continue;
    impl.funcao(mem_, al_, cpu, traco_);
    return Atendimento::Implementado;
  }
  // NAO ESTA IMPLEMENTADO. Duas coisas diferentes, e a diferenca importa:
  if (DeclaracaoDoOffset(offset) == nullptr) {
    return Atendimento::Fora_Da_Tabela;  // nem e um slot do sistema
  }
  // E um slot do sistema, e recusa-se em voz alta (P2), com o nome e a
  // assinatura do cabecalho, e com o MESMO detalhe de registos do despacho.
  RegistarRecusa(traco_, offset, "por implementar nesta etapa", DetalheDosRegistos(cpu));
  cpu.Set(kR0, kAeeUnsupported);
  ++recusas_;
  return Atendimento::Recusado;
}

bool AtenderAjudanteExtra(ICpu& cpu, Memoria& mem, Alocador& alocador, Traco& traco,
                          std::uint32_t offset) {
  AjudantesExtra extra(mem, alocador, traco);
  return extra.Atender(cpu, offset) != Atendimento::Fora_Da_Tabela;
}

}  // namespace zb2::brew
