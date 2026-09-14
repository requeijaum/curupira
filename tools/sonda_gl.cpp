// Sonda do GL: as chamadas de GL/EGL de um titulo REAL a chegar ao modulo --
// ATRAVES DO DESPACHO DO MOTOR, e nao por baixo dele.
//
// PORQUE ESTA SONDA FOI REESCRITA. A versao anterior chamava o `Igl::Executar`
// directamente, e por isso media a coisa errada: dizia "25 chamadas chegaram"
// com a cablagem do despacho INEXISTENTE. **Um instrumento que mede o modulo em
// vez da cablagem mente, e foi um instrumento que mente que custou a semana dos
// 86 377 `glCullFace` descartados** (P7: um log so entra se puder ser verdadeiro).
//
// Agora a sonda faz o que o motor faz: monta o `Despacho` do motor
// (`core/brew/despacho.h`), instala os ajudantes, escreve o ponteiro do NOSSO
// objecto no `gpIGL`/`gpIEGL` do modulo (que e o que o `GLES_Init` do jogo faz com
// o resultado do `CreateInstance`), e depois CHAMA OS THUNKS DO WRAPPER. O thunk
// le a vtable do objecto, salta para um endereco da faixa de saida, e quem decide
// o que acontece a seguir e o `Despacho::Correr` -- o MESMO codigo que corre na
// bateria. Se a cablagem nao existir, o pedido e engolido por um ramo generico e a
// sonda diz com que NOME errado isso aconteceu.
//
// O QUE ESTA SONDA NAO E: um rasterizador. `PIXELS` continua 0, e o `glDrawArrays`
// recusa em voz alta a dizer por que.
//
// COMO ACHAR OS THUNKS. Varre-se a imagem a procura do idioma do `GLES_1x.c`:
//
//     ldr  rX, [pc, #k]      ; o literal ROPI
//     add  rX, pc, rX        ; -> &gpIGL (+0) ou &gpIEGL (+4)
//     ldr  rX, [rX]          ; o objecto
//     ldr  rX, [rX]          ; a vtable
//     ldr  rY, [rX, #off]    ; o slot (off/4) -- Y pode ser outro registo
//     bx   rY                ; ... e o `bx` pode nao vir na instrucao seguinte
//
// AS DUAS ULTIMAS LINHAS SAO A CORRECCAO DESTA RONDA, e as duas custaram uma
// contagem errada: o thunk de 5 argumentos (o `eglChooseConfig` do `ddragonz` em
// 0x123dac) carrega o slot para o r12 e mete um `mov r3, lr` e um `mov lr, pc`
// antes do `bx ip`. Exigir `bx` na instrucao seguinte e o mesmo registo perdia
// esses thunks: MEDIDO, o `ddragonz` aparecia com 37 thunks em vez de 41, e o unico
// titulo que pede um config de EGL nao mostrava esse pedido.
//
// Uso: zb2_sonda_gl <ficheiro.mod> [--directo]

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "core/brew/despacho.h"
#include "core/brew/egl.h"
#include "core/brew/igl.h"
#include "core/cpu/arm_interpreter.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"

using namespace zb2;

namespace {

constexpr std::uint32_t kBase = 0x00100000u;   // o modulo carrega aqui, como o `igl.h` documenta
constexpr std::uint32_t kPilha = 0x80080000u;
constexpr std::uint32_t kSentinela = 0xFFFFFFF0u;
constexpr std::uint32_t kTabela = 0x80010000u;
constexpr std::uint32_t kHeap = 0x80200000u;
constexpr std::uint32_t kHeapTam = 0x00C00000u;
constexpr std::uint32_t kBanda = 0x00090000u;  // dados que a sonda escreve no guest
constexpr std::uint64_t kLimite = 2000000ull;

// Quantas instrucoes podem separar o `ldr` do slot do `bx` que salta para ele.
// 4 cobre o prologo de 5 argumentos do wrapper (medido em 0x123dac).
constexpr int kJanelaDoBx = 4;

std::vector<std::uint8_t> LerFicheiro(const char* caminho, bool* ok) {
  std::vector<std::uint8_t> dados;
  std::FILE* f = std::fopen(caminho, "rb");
  if (f == nullptr) { *ok = false; return dados; }
  std::uint8_t buf[65536];
  std::size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) dados.insert(dados.end(), buf, buf + n);
  std::fclose(f);
  *ok = true;
  return dados;
}

struct Thunk {
  std::uint32_t endereco = 0;      // onde a CADEIA ROPI comeca
  std::uint32_t entrada = 0;       // onde a FUNCAO comeca (o `push`); e por aqui que se chama
  std::uint32_t bloco_ropi = 0;    // o endereco que o `add pc` resolve
  std::uint32_t deslocamento = 0;  // 0 = gpIGL, 4 = gpIEGL
  std::uint32_t slot = 0;
};

// O ENDERECO DE ENTRADA DO THUNK, e nao o endereco da cadeia ROPI.
//
// PORQUE ISTO NAO E DETALHE. O wrapper do SDK nao gera uma funcao por metodo com
// a mesma forma: os metodos com CINCO OU MAIS argumentos tem um PROLOGO que salva
// os registos e copia para a pilha os argumentos que nao cabem em quatro. Medido
// no `ddragonz.mod`, no `glCompressedTexImage2D` (slot 15):
//
//     124188  push {r4, r5, r6, lr}     ; <<< a ENTRADA da funcao
//     12418c  sub  sp, sp, #16
//     124190  mov  r6, r3               ; guarda o 4.o argumento
//     124194  add  r3, sp, #32
//     124198  mov  lr, r0
//     12419c  mov  r5, r2
//     1241a0  mov  r4, r1
//     1241a4  ldm  r3, {r0, r1, r2, r3} ; os argumentos 5..8 que vieram na pilha
//     1241a8  stm  sp, {r0, r1, r2, r3}
//     1241ac  ldr  r0, [pc, #48]        ; <<< aqui comeca a cadeia ROPI
//     ...
//     1241d8  add  sp, sp, #16
//     1241dc  pop  {r4, r5, r6, lr}
//     1241e0  bx   lr
//
// A CADEIA NAO E A ENTRADA. Chamar o thunk a partir de 0x1241ac (o que a primeira
// versao desta sonda fazia) corre o corpo com o r4/r5/r6/lr que estivessem no CPU e
// termina no epilogo a tirar da pilha o que nunca la foi posto -- MEDIDO: o PC saiu
// do modulo para 0x400010 e a sonda acusou `saiu_do_modulo` em cinco thunks, com a
// culpa a parecer do emulador.
//
// A REGRA: andar para tras a partir da cadeia ate ao `push {..., lr}` mais proximo,
// sem atravessar nenhum salto e sem andar mais de 16 instrucoes. Sem `push` (o caso
// dos quatro argumentos, como o `eglMakeCurrent` em 0x123ec8) a entrada E a cadeia.
std::uint32_t EntradaDoThunk(const std::vector<std::uint8_t>& img, const auto& ler32,
                             std::uint32_t cadeia) {
  for (int k = 1; k <= 16; ++k) {
    const std::uint32_t a = cadeia - 4u * static_cast<std::uint32_t>(k);
    if (a < kBase) break;
    const std::uint32_t w = ler32(a);
    // `push {..., lr}` = STMDB sp!, {reglist com o bit 14}
    if ((w & 0x0FFF0000u) == 0x092D0000u && ((w >> 14) & 1) == 1) return a;
    // Um salto antes do `push` significa que a funcao comeca depois dele: nao ha
    // prologo para tras, e a entrada e a propria cadeia.
    if (((w >> 25) & 0x7) == 0x5) break;
    // Um `pop` ou um `push` sem o LR tambem marcam o fim do que se procura.
    if ((w & 0x0FFF0000u) == 0x08BD0000u) break;
  }
  return cadeia;
}

bool DescodificarThunk(const std::vector<std::uint8_t>& img, std::uint32_t vaddr, Thunk* t) {
  const auto ler32 = [&](std::uint32_t a) -> std::uint32_t {
    if (a < kBase) return 0;
    const std::size_t o = a - kBase;
    if (o + 4 > img.size()) return 0;
    return static_cast<std::uint32_t>(img[o]) | (static_cast<std::uint32_t>(img[o + 1]) << 8) |
           (static_cast<std::uint32_t>(img[o + 2]) << 16) |
           (static_cast<std::uint32_t>(img[o + 3]) << 24);
  };
  const auto ldr_imm = [](std::uint32_t w) { return (w & 0x0F700000u) == 0x05100000u; };
  const auto add_pc = [](std::uint32_t w, std::uint32_t rd) {
    return ((w >> 12) & 0xF) == rd && ((w >> 16) & 0xF) == 15 && (w & 0xF) == rd &&
           ((w >> 21) & 0xF) == 0x4;
  };
  // UM `str` NAO ESCREVE NO CAMPO Rd. Tratar um `str r3, [sp]` do prologo como
  // escrita do r3 quebrava a cadeia -- ver `tools/gerar_slots.py`.
  const auto escreve_em = [](std::uint32_t w, std::uint32_t reg) {
    if (((w >> 12) & 0xF) != reg) return false;
    if (w == (0xE12FFF10u | reg)) return false;
    const std::uint32_t op = (w >> 25) & 0x7;
    if (op == 0 || op == 1) return !(((w >> 20) & 1) && !((w >> 21) & 1));
    if (op == 2 || op == 3) return ((w >> 20) & 1) == 1;
    if (op == 5) return ((w >> 24) & 1) == 1;
    return false;
  };
  std::uint32_t ws[40];
  for (int k = 0; k < 40; ++k) ws[k] = ler32(vaddr + 4u * k);

  for (int k = 0; k < 32; ++k) {
    if (!ldr_imm(ws[k]) || ((ws[k] >> 16) & 0xF) != 15) continue;
    const std::uint32_t rd = (ws[k] >> 12) & 0xF;
    if (!add_pc(ws[k + 1], rd)) continue;
    std::uint32_t reg = rd, estagio = 0, kk = 0, slot = 0;
    bool achou = false;
    for (int j = k + 2; j < k + 24 && j < 39; ++j) {
      const std::uint32_t w = ws[j];
      if (w == (0xE12FFF10u | reg)) break;  // bx reg: a cadeia acabou sem slot
      if (ldr_imm(w) && ((w >> 16) & 0xF) == reg) {
        const std::uint32_t destino = (w >> 12) & 0xF, imm = w & 0xFFF;
        if (estagio == 0) {
          if (imm != 0 && imm != 4) break;
          kk = imm;
          reg = destino;
          estagio = 1;
        } else if (estagio == 1) {
          if (imm != 0) break;
          reg = destino;
          estagio = 2;
        } else {
          if (imm == 0 || imm % 4 != 0 || imm > 512) break;
          // `bx` E `blx` saltam os dois para o registo: o `conftest.elf` do SDK
          // fecha o `glCompressedTexImage2D` com `blx ip`.
          for (int j2 = j + 1; j2 <= j + kJanelaDoBx && j2 < 39; ++j2) {
            if (ws[j2] == (0xE12FFF10u | destino) || ws[j2] == (0xE12FFF30u | destino)) {
              slot = imm / 4;
              achou = true;
            }
            if (achou) break;
            if (escreve_em(ws[j2], destino)) break;
          }
          break;
        }
      } else if (escreve_em(w, reg)) {
        break;
      }
    }
    if (!achou) continue;
    t->endereco = vaddr + 4u * static_cast<std::uint32_t>(k);
    t->entrada = EntradaDoThunk(img, ler32, t->endereco);
    t->bloco_ropi = (vaddr + 4u * static_cast<std::uint32_t>(k + 1) + 8) +
                    ler32(vaddr + 4u * static_cast<std::uint32_t>(k) + 8 + (ws[k] & 0xFFF));
    t->deslocamento = kk;
    t->slot = slot;
    return true;
  }
  return false;
}

// Os argumentos plausiveis, por slot. Nao ha aqui nenhum numero de slot escrito a
// mao: a chave e a constante GERADA do cabecalho, e e por isso que renomear um
// slot parte isto em vez de o deixar silenciosamente errado.
struct Args {
  std::uint32_t reg[4];
  std::vector<std::uint32_t> pilha;
};

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "uso: %s <ficheiro.mod>\n", argv[0]);
    return 2;
  }
  bool ok = false;
  const std::vector<std::uint8_t> imagem = LerFicheiro(argv[1], &ok);
  if (!ok || imagem.empty()) {
    std::fprintf(stderr, "nao consegui ler '%s'\n", argv[1]);
    return 2;
  }

  Tempo tempo;
  Traco traco("sonda_gl");
  Memoria mem(&traco);
  mem.EscritorUnico("sonda_gl");
  ArmInterpreter cpu(mem, &traco);
  zb2::Alocador al(mem, kHeap, kHeapTam, &traco);

  Saidas saidas;
  saidas.base = 0xF0000000u;
  saidas.passo = 4;
  saidas.quantos = 100000;
  saidas.ativa = true;
  cpu.ConfigurarSaidas(saidas);

  mem.EscreverBruto(kBase, imagem.data(), static_cast<std::uint32_t>(imagem.size()));

  // --- a bancada instala os OBJECTOS (o motor e que decide o despacho) -------
  zb2::brew::Igl igl(mem, traco);
  zb2::brew::Egl egl(mem, traco);
  const std::uint32_t slots_igl = igl.Instalar(saidas);
  const std::uint32_t slots_egl = egl.Instalar(saidas);
  if (slots_igl == 0 || slots_egl == 0) {
    std::printf("FALHA: nao consegui cablar as vtables (IGL %u, IEGL %u)\n", slots_igl, slots_egl);
    return 2;
  }

  zb2::brew::Vfs vfs;
  zb2::brew::Despacho despacho(mem, traco, al, vfs);
  despacho.InstalarAjudantes(saidas, kTabela);
  despacho.DefinirFaixaDoModulo(kBase, static_cast<std::uint32_t>(imagem.size()));

  // QUEM ATENDE OS PEDIDOS: O DESPACHO, QUANDO A CABLAGEM EXISTE.
  //
  // A bancada instala sempre as duas vtables (sem isso, os thunks saltariam para
  // uma vtable por escrever, e o VERMELHO nao seria medivel). Mas quem RESPONDE aos
  // pedidos e o motor quando o remendo esta aplicado -- e o motor tem o seu proprio
  // `Igl`/`Egl`, criados no `Despacho`. Ler os contadores do objecto da bancada
  // nessa situacao daria ZERO com o motor a funcionar: **o mesmo sintoma de "a
  // cablagem nao existe", com a culpa na sonda outra vez.** O `#define` que o
  // remendo acrescenta ao `despacho.h` e o que escolhe a fonte do estado.
#ifdef ZB2_CABLAGEM_GL
  zb2::brew::Igl* estado_igl = &despacho.IglRef();
  zb2::brew::Egl* estado_egl = &despacho.EglRef();
  std::printf("cablagem do despacho: PRESENTE (o motor atende o GL)\n");
#else
  zb2::brew::Igl* estado_igl = &igl;
  zb2::brew::Egl* estado_egl = &egl;
  std::printf("cablagem do despacho: AUSENTE (a bancada instala as vtables, mas quem\n"
              "                      despacha e o ramo generico do motor)\n");
#endif

  // --- 1. varrer os thunks -------------------------------------------------
  std::map<std::uint32_t, Thunk> thunks;
  for (std::uint32_t a = kBase; a + 4 * 40 < kBase + imagem.size(); a += 4) {
    Thunk t;
    if (!DescodificarThunk(imagem, a, &t)) continue;
    thunks[t.endereco] = t;
  }
  // O BLOCO ROPI E O MESMO PARA AS DUAS INTERFACES, E O `kk` E QUE ESCOLHE.
  //
  //     ldr  rX, [pc, #k]      ; o literal, que aponta para o BLOCO
  //     add  rX, pc, rX        ; -> o bloco (constante, igual nos dois casos)
  //     ldr  rX, [rX, #kk]     ; kk = 0 -> gpIGL | kk = 4 -> gpIEGL
  //
  // Escrever o objecto do IEGL em `bloco` (sem o `+4`) APAGA o `gpIGL` -- e foi o
  // que a primeira versao desta sonda fez: os thunks do IGL passaram a ler o
  // objecto do IEGL, e nenhuma chamada de `gl*` chegou ao modulo. O sintoma era o
  // mesmo de "a cablagem nao existe", com a causa na SONDA. Por isso a escrita e
  // CONFERIDA com leitura de volta, como toda a cablagem deste emulador.
  std::map<std::uint32_t, Thunk> por_slot_igl, por_slot_egl;
  std::uint32_t bloco_igl = 0, kk_igl = 0, bloco_ieg = 0, kk_ieg = 0;
  bool tem_igl = false, tem_ieg = false;
  for (const auto& par : thunks) {
    const Thunk& t = par.second;
    if (t.deslocamento == 0) {
      bloco_igl = t.bloco_ropi;
      kk_igl = t.deslocamento;
      tem_igl = true;
      if (por_slot_igl.find(t.slot) == por_slot_igl.end()) por_slot_igl[t.slot] = t;
    } else {
      bloco_ieg = t.bloco_ropi;
      kk_ieg = t.deslocamento;
      tem_ieg = true;
      if (por_slot_egl.find(t.slot) == por_slot_egl.end()) por_slot_egl[t.slot] = t;
    }
  }
  // Todos os thunks de uma banda tem de partilhar o bloco e o `kk`: se nao,
  // escrever o ponteiro num sitio so deixaria os outros a apontar para o nada.
  for (const auto& par : thunks) {
    const Thunk& t = par.second;
    if (t.deslocamento == 0 && (t.bloco_ropi != bloco_igl || kk_igl != 0)) {
      std::printf("FALHA: os thunks do IGL nao partilham o mesmo bloco ROPI\n");
      return 2;
    }
    if (t.deslocamento == 4 && (t.bloco_ropi != bloco_ieg || kk_ieg != 4)) {
      std::printf("FALHA: os thunks do IEGL nao partilham o mesmo bloco ROPI\n");
      return 2;
    }
  }
  if (tem_igl && tem_ieg && bloco_igl != bloco_ieg) {
    std::printf("FALHA: gpIGL e gpIEGL nao estao no mesmo bloco (0x%08x / 0x%08x)\n", bloco_igl,
                bloco_ieg);
    return 2;
  }
  std::printf("== %s: %zu bytes ==\n", argv[1], imagem.size());
  std::printf("thunks do wrapper: %zu (IGL %zu / IEGL %zu)\n", thunks.size(), por_slot_igl.size(),
              por_slot_egl.size());
  if (thunks.empty()) {
    std::printf("NADA ENCONTRADO. Nao invento uma explicacao: este `.mod` nao tem o idioma ROPI\n"
                "`add rX,pc,rX` que o `ddragonz.mod` tem 1164 vezes. Fica por medir.\n");
    return 2;
  }
  std::printf("bloco ROPI: gpIGL em 0x%08x (kk=%u) | gpIEGL em 0x%08x (kk=%u)\n", bloco_igl + kk_igl,
              kk_igl, bloco_ieg + kk_ieg, kk_ieg);
  std::uint32_t fora_igl = 0, fora_egl = 0;
  for (const auto& par : por_slot_igl) {
    if (par.first >= gl_slots::kIglSlots) ++fora_igl;
  }
  for (const auto& par : por_slot_egl) {
    if (par.first >= gl_slots::kIeglSlots) ++fora_egl;
  }
  std::printf("slots fora da tabela de AEEGL.h: IGL %u (max %u) | IEGL %u (max %u)\n", fora_igl,
              gl_slots::kIglSlots - 1, fora_egl, gl_slots::kIeglSlots - 1);
  std::uint32_t com_prologo = 0;
  for (const auto& par : thunks) {
    if (par.second.entrada != par.second.endereco) ++com_prologo;
  }
  std::printf("thunks com PROLOGO (entrada antes da cadeia): %u de %zu -- sao os de 5+ "
              "argumentos\n", com_prologo, thunks.size());

  // --- 2. escrever gpIGL/gpIEGL (o que o `GLES_Init` do jogo faria) --------
  mem.Escrever32(bloco_igl + kk_igl, igl.Objeto());
  mem.Escrever32(bloco_ieg + kk_ieg, egl.Objeto());
  // A LEITURA DE VOLTA: duas escritas que caem no mesmo endereco nao se veem de
  // outra forma, e o `kk` e precisamente o que as separa.
  if (mem.Ler32(bloco_igl + kk_igl) != igl.Objeto() ||
      mem.Ler32(bloco_ieg + kk_ieg) != egl.Objeto()) {
    std::printf("FALHA: os ponteiros gpIGL/gpIEGL nao ficaram escritos no modulo\n");
    return 2;
  }
  std::printf("gpIGL <- 0x%08x | gpIEGL <- 0x%08x (os nossos objectos)\n\n", igl.Objeto(),
              egl.Objeto());

  // Os dados que a sonda aponta nos argumentos.
  const auto Fixo = [](float v) {
    return static_cast<std::uint32_t>(static_cast<std::int32_t>(v * 65536.0f));
  };
  const std::uint32_t vertices = kBanda, cores = kBanda + 0x80, coords = kBanda + 0x100;
  for (int k = 0; k < 9; ++k) mem.Escrever32(vertices + 4u * k, 0x3F800000u);
  for (int k = 0; k < 4; ++k) mem.Escrever32(cores + 4u * k, 0xFFFFFFFFu);
  for (int k = 0; k < 6; ++k) mem.Escrever32(coords + 4u * k, 0);
  const std::uint32_t destino_textura = kBanda + 0x300, destino_int = kBanda + 0x340;
  const std::uint32_t vetor_parametros = kBanda + 0x380;
  for (int k = 0; k < 4; ++k) mem.Escrever32(vetor_parametros + 4u * k, 0);
  // A LISTA DE ATRIBUTOS DO EGL: os que o `ddragonz.mod` pede mesmo, lidos do
  // proprio ficheiro (0x14fc10, com o `add pc` da instrucao 0x11d704). Um
  // `attrib_list` inventado mediria a sonda, e nao o titulo.
  const std::uint32_t lista_egl = kBanda + 0x400;
  const std::uint32_t atributos_do_ddragonz[] = {0x3033u, 0x0004u, 0x3024u, 5u,
                                                 0x3023u, 6u,      0x3022u, 5u, 0x3038u};
  for (std::size_t k = 0; k < sizeof(atributos_do_ddragonz) / sizeof(std::uint32_t); ++k) {
    mem.Escrever32(lista_egl + 4u * static_cast<std::uint32_t>(k), atributos_do_ddragonz[k]);
  }

  const auto argumentos_igl = [&](std::uint32_t slot) -> Args {
    using namespace gl_slots;
    switch (slot) {
      case kIgl_MatrixMode: return {{GL_MODELVIEW, 0, 0, 0}, {}};
      case kIgl_Clear: return {{GL_COLOR_BUFFER_BIT, 0, 0, 0}, {}};
      case kIgl_ClearColorx: return {{Fixo(0.1f), Fixo(0.1f), Fixo(0.2f), Fixo(1.0f)}, {}};
      case kIgl_Color4x: return {{Fixo(1.0f), Fixo(1.0f), Fixo(1.0f), Fixo(1.0f)}, {}};
      case kIgl_CullFace: return {{GL_BACK, 0, 0, 0}, {}};
      case kIgl_FrontFace: return {{GL_CCW, 0, 0, 0}, {}};
      case kIgl_ShadeModel: return {{GL_SMOOTH, 0, 0, 0}, {}};
      case kIgl_DepthFunc: return {{GL_LESS, 0, 0, 0}, {}};
      case kIgl_BlendFunc: return {{GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, 0, 0}, {}};
      case kIgl_AlphaFuncx: return {{GL_ALWAYS, Fixo(0.0f), 0, 0}, {}};
      case kIgl_Hint: return {{GL_DONT_CARE, GL_FASTEST, 0, 0}, {}};
      case kIgl_PixelStorei: return {{GL_UNPACK_ALIGNMENT, 1, 0, 0}, {}};
      case kIgl_Viewport: return {{0, 0, 640, 480}, {}};
      case kIgl_Enable: return {{GL_CULL_FACE, 0, 0, 0}, {}};
      case kIgl_Disable: return {{GL_DITHER, 0, 0, 0}, {}};
      case kIgl_EnableClientState: return {{GL_VERTEX_ARRAY, 0, 0, 0}, {}};
      case kIgl_DisableClientState: return {{GL_TEXTURE_COORD_ARRAY, 0, 0, 0}, {}};
      case kIgl_VertexPointer: return {{3, GL_FLOAT, 12, vertices}, {}};
      case kIgl_ColorPointer: return {{4, GL_UNSIGNED_BYTE, 4, cores}, {}};
      case kIgl_TexCoordPointer: return {{2, GL_FLOAT, 8, coords}, {}};
      case kIgl_NormalPointer: return {{GL_FLOAT, 12, vertices}, {}};
      case kIgl_GenTextures: return {{1, destino_textura, 0, 0}, {}};
      case kIgl_DeleteTextures: return {{1, destino_textura, 0, 0}, {}};
      case kIgl_BindTexture: return {{GL_TEXTURE_2D, 1, 0, 0}, {}};
      case kIgl_TexParameterx: return {{GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR, 0}, {}};
      case kIgl_TexImage2D:
        return {{GL_TEXTURE_2D, 0, GL_RGBA, 64}, {32, 0, GL_RGBA, GL_UNSIGNED_BYTE, vertices}};
      case kIgl_DrawArrays: return {{GL_TRIANGLES, 0, 3, 0}, {}};
      case kIgl_DrawElements: return {{GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, vertices}, {}};
      case kIgl_GetIntegerv: return {{GL_MAX_MODELVIEW_STACK_DEPTH, destino_int, 0, 0}, {}};
      case kIgl_ActiveTexture:
      case kIgl_ClientActiveTexture: return {{GL_TEXTURE0, 0, 0, 0}, {}};
      case kIgl_Fogxv:
      case kIgl_LightModelxv:
      case kIgl_Lightxv:
      case kIgl_Materialxv:
      case kIgl_TexEnvxv: return {{GL_FOG, vetor_parametros, 0, 0}, {}};
      default: return {{0, 0, 0, 0}, {}};
    }
  };
  const auto argumentos_egl = [&](std::uint32_t slot) -> Args {
    using namespace gl_slots;
    switch (slot) {
      case kIegl_GetDisplay: return {{0, 0, 0, 0}, {}};  // EGL_DEFAULT_DISPLAY
      case kIegl_Initialize: return {{zb2::brew::kDisplayUnico, 0, 0, 0}, {}};
      case kIegl_Terminate: return {{zb2::brew::kDisplayUnico, 0, 0, 0}, {}};
      case kIegl_QueryString: return {{zb2::brew::kDisplayUnico, EGL_EXTENSIONS, 0, 0}, {}};
      case kIegl_GetProcAddress: return {{0, 0, 0, 0}, {}};
      case kIegl_GetConfigs:
        return {{zb2::brew::kDisplayUnico, destino_int, 1, destino_textura}, {}};
      case kIegl_ChooseConfig:
        // CINCO argumentos: o quinto vem da pilha, e um `sp` a zero faria a sonda
        // ler o endereco zero.
        return {{zb2::brew::kDisplayUnico, lista_egl, destino_textura, 1},
                {destino_int}};
      case kIegl_GetConfigAttrib:
        return {{zb2::brew::kDisplayUnico, zb2::brew::kConfigUnico, EGL_RED_SIZE, destino_int}, {}};
      case kIegl_CreateWindowSurface:
        return {{zb2::brew::kDisplayUnico, zb2::brew::kConfigUnico, 0, lista_egl}, {}};
      case kIegl_DestroySurface:
        return {{zb2::brew::kDisplayUnico, zb2::brew::kPrimeiraSuperficie, 0, 0}, {}};
      case kIegl_QuerySurface:
        return {{zb2::brew::kDisplayUnico, zb2::brew::kPrimeiraSuperficie, EGL_WIDTH, destino_int},
                {}};
      case kIegl_CreateContext:
        return {{zb2::brew::kDisplayUnico, zb2::brew::kConfigUnico, 0, 0}, {}};
      case kIegl_DestroyContext:
        return {{zb2::brew::kDisplayUnico, zb2::brew::kPrimeiroContexto, 0, 0}, {}};
      case kIegl_MakeCurrent:
        return {{zb2::brew::kDisplayUnico, zb2::brew::kPrimeiraSuperficie,
                 zb2::brew::kPrimeiraSuperficie, zb2::brew::kPrimeiroContexto}, {}};
      case kIegl_GetCurrentSurface: return {{EGL_DRAW, 0, 0, 0}, {}};
      case kIegl_QueryContext:
        return {{zb2::brew::kDisplayUnico, zb2::brew::kPrimeiroContexto,
                 EGL_CONTEXT_CLIENT_VERSION, destino_int}, {}};
      case kIegl_WaitNative: return {{EGL_CORE_NATIVE_ENGINE, 0, 0, 0}, {}};
      case kIegl_SwapBuffers:
        return {{zb2::brew::kDisplayUnico, zb2::brew::kPrimeiraSuperficie, 0, 0}, {}};
      default: return {{0, 0, 0, 0}, {}};
    }
  };

  // A ORDEM IMPORTA, e e a do wrapper do jogo: o EGL primeiro (o `GLES_Init`),
  // depois o GL. E, no GL, o alinhamento dos arrays antes do DESENHO -- um
  // `glDrawArrays` antes do `glVertexPointer` recusa por uma razao VERDADEIRA e
  // INUTIL (o primeiro a correr assim mediu nada sobre desenho).
  const auto prioridade = [](std::uint32_t slot) {
    using namespace gl_slots;
    switch (slot) {
      case kIgl_EnableClientState:
      case kIgl_DisableClientState:
      case kIgl_VertexPointer:
      case kIgl_ColorPointer:
      case kIgl_TexCoordPointer:
      case kIgl_NormalPointer:
      case kIgl_PushMatrix:
        return 0;
      case kIgl_DrawArrays:
      case kIgl_DrawElements:
        return 2;
      case kIgl_PopMatrix:
        return 3;
      default:
        return 1;
    }
  };

  // A ORDEM DOS SLOTS DO EGL NAO E A DOS NUMEROS, e e a mesma verdade da ordem do
  // GL: um `eglCreateWindowSurface` antes de um `eglInitialize` recusa por
  // "display nao inicializado" -- recusa VERDADEIRA e medida NADA sobre a criacao
  // de superficies. O `eglTerminate` vai por ULTIMO, senao fecha o display e tudo o
  // que vier depois recusa por um motivo que e da bancada.
  const auto prioridade_egl = [](std::uint32_t slot) {
    using namespace gl_slots;
    switch (slot) {
      case kIegl_GetDisplay:
      case kIegl_Initialize:
      case kIegl_GetConfigs:
      case kIegl_ChooseConfig:
      case kIegl_GetConfigAttrib:
      case kIegl_GetError:
        return 0;
      case kIegl_CreateWindowSurface:
      case kIegl_CreateContext:
      case kIegl_MakeCurrent:
      case kIegl_QuerySurface:
      case kIegl_QueryContext:
      case kIegl_GetCurrentContext:
      case kIegl_GetCurrentSurface:
      case kIegl_GetCurrentDisplay:
        return 1;
      case kIegl_QueryString:
      case kIegl_GetProcAddress:
      case kIegl_WaitGL:
      case kIegl_WaitNative:
      case kIegl_SwapBuffers:
        return 2;
      case kIegl_CreatePixmapSurface:
      case kIegl_CreatePbufferSurface:
      case kIegl_CopyBuffers:
      case kIegl_DestroySurface:
      case kIegl_DestroyContext:
        return 3;
      case kIegl_Terminate:
        return 4;
      default:
        return 1;
    }
  };
  struct Passo {
    int fase;  // 0 = EGL, 1 = IGL
    int ordem;
    Thunk t;
  };
  std::vector<Passo> guiao;
  for (const auto& par : por_slot_egl) {
    guiao.push_back({0, prioridade_egl(par.first) * 1000 + static_cast<int>(par.first), par.second});
  }
  for (const auto& par : por_slot_igl) {
    guiao.push_back({1, prioridade(par.first) * 1000 + static_cast<int>(par.first), par.second});
  }
  std::sort(guiao.begin(), guiao.end(), [](const Passo& a, const Passo& b) {
    if (a.fase != b.fase) return a.fase < b.fase;
    return a.ordem < b.ordem;
  });

  std::printf("%-4s %-26s %-14s %s\n", "slot", "metodo", "chegou?", "detalhe");
  std::uint32_t chegaram = 0, engolidas = 0;
  std::map<std::string, std::uint64_t> engolidas_por_nome;
  for (const auto& passo : guiao) {
    const Thunk& t = passo.t;
    const Args a =
        (passo.fase == 0) ? argumentos_egl(t.slot) : argumentos_igl(t.slot);
    std::uint32_t sp = kPilha;
    for (std::size_t k = 0; k < a.pilha.size(); ++k) {
      mem.Escrever32(sp + 4u * static_cast<std::uint32_t>(k), a.pilha[k]);
    }
    cpu.Repor(t.entrada, sp);
    for (int k = 0; k < 4; ++k) cpu.Set(kR0 + k, a.reg[k]);
    cpu.Set(kLR, kSentinela);

    const std::uint64_t antes = estado_igl->Chamadas() + estado_egl->Chamadas();
    // AS FALTAS VIVEM NO TRACO, e nao no `Despacho::Faltas()`. MEDIDO nesta
    // ronda: o mapa do despacho NUNCA e escrito (o `faltas_` nao aparece uma so vez
    // a ser preenchido em `despacho.cpp`), e um campo que nao mede e pior do que um
    // campo ausente -- foi assim que o `tamanho` do `.mod` mentiu na bateria.
    // `traco.ContagemFaltas()` e o caminho unico (P2/P7) e o mesmo que a bateria le.
    std::map<std::string, std::uint64_t> faltas_antes = traco.ContagemFaltas();
    std::string motivo;
    for (int voltas = 0; voltas < 64; ++voltas) {
      const zb2::brew::ResultadoFase r = despacho.Correr(cpu, kLimite, 0);
      if (cpu.Get(kLR) == kSentinela && cpu.Get(kPC) == kSentinela) break;
      if (r.motivo != "retornou") {
        motivo = r.motivo;
        break;
      }
    }
    const std::uint64_t depois = estado_igl->Chamadas() + estado_egl->Chamadas();
    const char* metodo =
        (passo.fase == 0) ? gl_slots::NomeIegl(t.slot) : gl_slots::NomeIgl(t.slot);
    if (depois > antes) {
      ++chegaram;
      std::string detalhe =
          (passo.fase == 0) ? estado_egl->Ultimas().back().motivo
                            : estado_igl->Ultimas().back().motivo;
      const char* resultado = (passo.fase == 0)
                                  ? zb2::brew::Nome(estado_egl->Ultimas().back().resultado)
                                  : zb2::brew::Nome(estado_igl->Ultimas().back().resultado);
      if (detalhe.size() > 58) detalhe = detalhe.substr(0, 58) + "...";
      std::printf("%-4u %-26s %-14s %s: %s\n", t.slot, metodo, "CHEGOU", resultado,
                  detalhe.c_str());
    } else {
      ++engolidas;
      // QUEM ENGOLIU, COM O NOME. O `Despacho` regista as faltas com o nome do
      // metodo; o delta mostra qual delas nasceu desta chamada.
      std::string nome = "sem_nome";
      for (const auto& par : traco.ContagemFaltas()) {
        const auto it = faltas_antes.find(par.first);
        const std::uint64_t antes_deste = (it == faltas_antes.end()) ? 0 : it->second;
        if (par.second > antes_deste) {
          nome = par.first;
          ++engolidas_por_nome[par.first];
          break;
        }
      }
      std::printf("%-4u %-26s %-14s o motor nomeou isto \"%s\"%s%s\n", t.slot, metodo,
                  "NAO CHEGOU", nome.c_str(), motivo.empty() ? "" : " | ",
                  motivo.c_str());
    }
  }

  // --- 3. o estado acumulado ----------------------------------------------
  const float* m = estado_igl->MatrizCorrente();
  std::printf("\n== estado acumulado (o que o MOTOR entregou ao modulo) ==\n");
  std::printf("chamadas ao IGL: %" PRIu64 " | ao IEGL: %" PRIu64 "\n", estado_igl->Chamadas(),
              estado_egl->Chamadas());
  std::printf("matriz de MODELVIEW (column-major, indice = coluna*4 + linha):\n");
  for (int r = 0; r < 4; ++r) {
    std::printf("  [%9.4f %9.4f %9.4f %9.4f]\n", m[r], m[4 + r], m[8 + r], m[12 + r]);
  }
  std::printf("cull face = 0x%04x | limpezas = %" PRIu64 " | desenhos = %" PRIu64
              " | vertices = %" PRIu64 " | texturas geradas = %" PRIu64 "\n",
              estado_igl->CullFace(), estado_igl->Limpezas(), estado_igl->Desenhos(),
              estado_igl->Vertices(), estado_igl->TexturasGeradas());
  std::printf("EGL: iniciado=%s | superficies criadas=%u (vivas %zu) | contextos=%u (vivos %zu) "
              "| trocas=%" PRIu64 " | erro=0x%04x\n",
              estado_egl->Iniciado() ? "sim" : "NAO", estado_egl->SuperficiesCriadas(),
              estado_egl->SuperficiesVivas(), estado_egl->ContextosCriados(),
              estado_egl->ContextosVivos(), estado_egl->Trocas(), estado_egl->Erro());
  std::printf("PIXELS: 0 -- nao ha rasterizador nesta etapa (o `glDrawArrays` recusa e di-lo)\n");

  std::printf("\n== recusas, por nome (o que a arvore antiga nao tinha em lado nenhum) ==\n");
  bool alguma = false;
  for (const auto& par : estado_igl->Recusas()) {
    std::printf("  IGL  %-24s %" PRIu64 "x\n", par.first.c_str(), par.second);
    alguma = true;
  }
  for (const auto& par : estado_egl->Recusas()) {
    std::printf("  IEGL %-24s %" PRIu64 "x\n", par.first.c_str(), par.second);
    alguma = true;
  }
  if (!alguma) std::printf("  nenhuma\n");

  std::printf("\n== demanda medida: os slots que este titulo traz ==\n");
  for (const auto& par : por_slot_egl) {
    std::printf("  IEGL %-3u %s\n", par.first, gl_slots::NomeIegl(par.first));
  }
  for (const auto& par : por_slot_igl) {
    std::printf("  IGL  %-3u %s\n", par.first, gl_slots::NomeIgl(par.first));
  }

  std::printf("\n== VEREDITO ==\n");
  std::printf("thunks do wrapper: %zu | chegaram ao modulo: %u | engolidos: %u\n",
              thunks.size(), chegaram, engolidas);
  for (const auto& par : engolidas_por_nome) {
    std::printf("  engolido por \"%s\" x%" PRIu64 "\n", par.first.c_str(), par.second);
  }
  if (engolidas == 0) {
    std::printf("VERDE -- todos os pedidos de GL/EGL chegaram aos modulos, com o nome certo.\n");
    return 0;
  }
  std::printf(
      "VERMELHO -- os modulos do IGL e do IEGL estao implementados e medidos, mas o MOTOR\n"
      "            ainda nao os alcanca: %u de %zu pedidos foram engolidos por um ramo\n"
      "            generico do despacho, com o nome de outra interface.\n"
      "            O remendo exacto (despacho.{h,cpp}) esta em\n"
      "            docs/rewrite/REMENDO-GL-DESPACHO.md, e foi provado numa COPIA da arvore.\n",
      engolidas, thunks.size());
  return 1;
}
