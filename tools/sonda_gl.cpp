// Sonda do IGL: as chamadas de GL de um titulo REAL a chegar ao modulo.
//
// A PERGUNTA QUE ESTA SONDA RESPONDE, com numero: **o `.mod` de um titulo traz o
// wrapper de GL compilado dentro, e quando esse wrapper chama a vtable o pedido
// chega ao nosso `Igl` com o nome e os argumentos certos?**
//
// PORQUE NAO E "correr o jogo". Chegar a um quadro desenhado precisa de mais tres
// coisas que NAO existem nesta etapa, e a sonda di-las em vez de as esconder:
// (a) `IShell::CreateInstance(AEECLSID_GL)` servido pelo despacho, (b) o IEGL/EGL
// inteiro, (c) o rasterizador. O que ESTA sonda mede e a peca que faltava por
// baixo de todas: o wrapper do TITULO a falar com o NOSSO objecto IGL.
//
// COMO FUNCIONA, em quatro passos:
//
//   1. Carrega o `.mod` (binario ARM plano) em 0x00100000, como o carregador.
//   2. Varre a imagem a procura dos THUNKS do wrapper `GLES_1x.c`:
//          ldr  rX, [pc, #k]      ; o literal ROPI
//          add  rX, pc, rX        ; -> &gpIGL (+0) ou &gpIEGL (+4)
//          ldr  rX, [rX]          ; o objecto
//          ldr  rX, [rX]          ; a vtable
//          ldr  rX, [rX, #off]    ; o slot (off/4)
//          bx   rX
//      Cada thunk da (endereco, bloco ROPI, deslocamento, slot). E a MESMA
//      medicao que o `tools/gerar_slots.py` faz no `conftest.elf` do SDK -- feita
//      no titulo, e escrita outra vez aqui DE PROPOSITO: se as duas divergirem,
//      uma das duas esta errada.
//   3. Escreve o endereco do NOSSO objecto IGL no `gpIGL` do modulo -- que e
//      exactamente o que o `GLES_Init` do jogo faria com o resultado do
//      `CreateInstance`.
//   4. Chama cada thunk encontrado pelo CPU, com argumentos plausiveis, e
//      despacha o pedido para o `Igl` como o `Despacho` faria.
//
// O relatorio diz: quantos thunks o titulo traz, que slots sao, o que o modulo
// fez com cada um, o estado acumulado, e a lista de recusas COM NOME.
//
// Uso: zb2_sonda_gl <ficheiro.mod> [destino.txt]

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "core/brew/igl.h"
#include "core/cpu/arm_interpreter.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"

using namespace zb2;

namespace {

constexpr std::uint32_t kBase = 0x00100000u;
constexpr std::uint32_t kPilha = 0x80080000u;
constexpr std::uint32_t kSentinela = 0xFFFFFFF0u;
constexpr std::uint32_t kBanda = 0x00090000u;  // onde a sonda escreve dados do guest

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
  std::uint32_t endereco = 0;      // onde o thunk COMECA (o `ldr rX,[pc,#k]`)
  std::uint32_t bloco_ropi = 0;    // o endereco que o `add pc` resolve
  std::uint32_t deslocamento = 0;  // 0 = gpIGL, 4 = gpIEGL
  std::uint32_t slot = 0;
};

// O descodificador. Devolve o endereco de INICIO da cadeia, e nao o endereco por
// onde a varredura passou: o primeiro escrito devolvia o endereco de varrimento,
// e com passos de 4 bytes o mesmo thunk aparecia 3 vezes (o deslocamento do
// encaixe dentro da janela de 32 instrucoes). A contagem saiu 319 onde ha 37 --
// **um numero que parece medido e conta a coisa errada e pior do que nenhum.**
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
  std::uint32_t ws[32];
  for (int k = 0; k < 32; ++k) ws[k] = ler32(vaddr + 4u * k);

  for (int k = 0; k < 26; ++k) {
    if (!ldr_imm(ws[k]) || ((ws[k] >> 16) & 0xF) != 15) continue;
    const std::uint32_t rd = (ws[k] >> 12) & 0xF;
    if (!add_pc(ws[k + 1], rd)) continue;
    std::uint32_t reg = rd, estagio = 0, kk = 0, slot = 0;
    bool achou = false;
    for (int j = k + 2; j < k + 20 && j < 31; ++j) {
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
          // O slot TEM de ser seguido do `bx`. Sem esta exigencia, qualquer
          // leitura de um campo a dois ponteiros de distancia entra como thunk.
          if (imm == 0 || imm % 4 != 0 || imm > 512) break;
          if (ws[j + 1] != (0xE12FFF10u | destino)) break;
          slot = imm / 4;
          achou = true;
          break;
        }
      } else if (((w >> 12) & 0xF) == reg && ((w >> 25) & 0x7) != 0x4) {
        break;
      }
    }
    if (!achou) continue;
    t->endereco = vaddr + 4u * static_cast<std::uint32_t>(k);
    t->bloco_ropi = (vaddr + 4u * static_cast<std::uint32_t>(k + 1) + 8) +
                    ler32(vaddr + 4u * static_cast<std::uint32_t>(k) + 8 + (ws[k] & 0xFFF));
    t->deslocamento = kk;
    t->slot = slot;
    return true;
  }
  return false;
}

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
  Traco traco("sonda_gl", &tempo);
  Memoria mem(&traco);
  mem.EscritorUnico("sonda_gl");
  ArmInterpreter cpu(mem, &traco);
  Saidas saidas;
  saidas.base = 0xF0000000u;
  saidas.passo = 4;
  saidas.quantos = 100000;
  saidas.ativa = true;
  cpu.ConfigurarSaidas(saidas);

  mem.EscreverBruto(kBase, imagem.data(), static_cast<std::uint32_t>(imagem.size()));

  zb2::brew::Igl igl(mem, traco);
  if (igl.Instalar(saidas) == 0) {
    std::printf("FALHA: nao consegui cablar a vtable do IGL\n");
    return 1;
  }

  // --- 1. varrer os thunks do wrapper -------------------------------------
  std::map<std::uint32_t, Thunk> thunks;          // endereco do thunk -> thunk
  std::map<std::uint32_t, Thunk> por_slot;        // slot do IGL -> thunk
  for (std::uint32_t a = kBase; a + 4 * 32 < kBase + imagem.size(); a += 4) {
    Thunk t;
    if (!DescodificarThunk(imagem, a, &t)) continue;
    thunks[t.endereco] = t;
    if (t.deslocamento == 0 && por_slot.find(t.slot) == por_slot.end()) por_slot[t.slot] = t;
  }
  std::uint32_t de_igl = 0, de_ieg = 0, base_igl = 0, base_ieg = 0;
  for (const auto& par : thunks) {
    if (par.second.deslocamento == 0) { ++de_igl; base_igl = par.second.bloco_ropi; }
    else { ++de_ieg; base_ieg = par.second.bloco_ropi; }
  }
  std::printf("== %s: %zu bytes ==\n", argv[1], imagem.size());
  std::printf("thunks do wrapper encontrados: %zu (IGL %u / IEGL %u)\n", thunks.size(), de_igl, de_ieg);
  if (thunks.empty()) {
    std::printf("NADA ENCONTRADO. Nao invento uma explicacao: o que se sabe e que\n"
                "este `.mod` nao tem o idioma ROPI `add rX,pc,rX` que o `ddragonz.mod` tem\n"
                "1164 vezes. Fica por medir.\n");
    return 1;
  }
  std::printf("bloco ROPI: gpIGL em 0x%08x | gpIEGL em 0x%08x\n", base_igl,
              base_ieg ? base_ieg : base_igl + 4);

  // O SLOT TEM DE EXISTIR NA TABELA. Um thunk a pedir um slot fora dela significa
  // que a tabela (do cabecalho) e o wrapper do titulo NAO sao a mesma ABI -- e
  // isso e a coisa mais importante que esta sonda pode dizer.
  std::uint32_t fora = 0;
  for (const auto& par : por_slot) {
    if (par.first >= gl_slots::kIglSlots) {
      std::printf("  !! slot %u pedido pelo titulo NAO existe na tabela (max %u)\n", par.first,
                  gl_slots::kIglSlots - 1);
      ++fora;
    }
  }
  if (fora == 0) {
    std::printf("todos os %zu slots pedidos existem em AEEGL.h (0..%u)\n\n", por_slot.size(),
                gl_slots::kIglSlots - 1);
  }

  // --- 2. escrever gpIGL (o que o `GLES_Init` do jogo faria) ---------------
  mem.Escrever32(base_igl, igl.Objeto());
  std::printf("gpIGL <- 0x%08x (o nosso objecto IGL)\n\n", igl.Objeto());

  // --- 3. chamar cada thunk, com argumentos plausiveis ---------------------
  const auto Fixo = [](float v) {
    return static_cast<std::uint32_t>(static_cast<std::int32_t>(v * 65536.0f));
  };
  const std::uint32_t vertices = kBanda;
  const std::uint32_t cores = kBanda + 0x80;
  const std::uint32_t coords = kBanda + 0x100;
  for (int k = 0; k < 9; ++k) mem.Escrever32(vertices + 4u * k, 0x3F800000u);
  for (int k = 0; k < 4; ++k) mem.Escrever32(cores + 4u * k, 0xFFFFFFFFu);
  for (int k = 0; k < 6; ++k) mem.Escrever32(coords + 4u * k, 0);
  const std::uint32_t destino_textura = kBanda + 0x300, destino_int = kBanda + 0x340;
  const std::uint32_t vetor_parametros = kBanda + 0x380;
  for (int k = 0; k < 4; ++k) mem.Escrever32(vetor_parametros + 4u * k, 0);

  // Os argumentos, por slot. Nao ha aqui nenhum numero de slot escrito a mao: a
  // chave e a constante GERADA do cabecalho, e e por isso que renomear um slot
  // parte isto em vez de o deixar silenciosamente errado.
  struct Args {
    std::uint32_t reg[4];
    std::vector<std::uint32_t> pilha;
  };
  const auto argumentos = [&](std::uint32_t slot) -> Args {
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
        return {{GL_TEXTURE_2D, 0, GL_RGBA, 64},
                {32, 0, GL_RGBA, GL_UNSIGNED_BYTE, vertices}};
      case kIgl_DrawArrays: return {{GL_TRIANGLES, 0, 3, 0}, {}};
      case kIgl_DrawElements: return {{GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, vertices}, {}};
      case kIgl_GetIntegerv: return {{GL_MAX_MODELVIEW_STACK_DEPTH, destino_int, 0, 0}, {}};
      // A unidade de textura existe no cabecalho (`GL_TEXTURE0`); passar zero
      // fazia o modulo recusar por um motivo que era da SONDA e nao do modulo.
      case kIgl_ActiveTexture:
      case kIgl_ClientActiveTexture: return {{GL_TEXTURE0, 0, 0, 0}, {}};
      // As familias que so acumulam parametros levam um vector A SERIO, lido da
      // memoria do guest. Com o ponteiro a zero, a recusa era "sem vector" -- e a
      // lista de recusas ficava a misturar as falhas da sonda com as do modulo.
      case kIgl_Fogxv:
      case kIgl_LightModelxv:
      case kIgl_Lightxv:
      case kIgl_Materialxv:
      case kIgl_TexEnvxv: return {{GL_FOG, vetor_parametros, 0, 0}, {}};
      case kIgl_PushMatrix: return {{0, 0, 0, 0}, {}};
      default: return {{0, 0, 0, 0}, {}};
    }
  };

  // A ORDEM E DE PROPOSITO, e nao a ordem dos slots.
  //
  // Um `glDrawArrays` (slot 26) antes do `glVertexPointer` (slot 78) recusa por
  // "desenho sem array de vertices ligado" -- e a recusa seria VERDADEIRA e a
  // medicao inutil: nao se aprende nada sobre o desenho. A primeira versao desta
  // sonda fez exatamente isso. Aqui o alinhamento vem primeiro, o desenho depois.
  const auto prioridade = [](std::uint32_t slot) {
    using namespace gl_slots;
    switch (slot) {
      case kIgl_EnableClientState:
      case kIgl_DisableClientState:
      case kIgl_VertexPointer:
      case kIgl_ColorPointer:
      case kIgl_TexCoordPointer:
      case kIgl_NormalPointer:
        return 0;
      // O `glPopMatrix` so tem sentido DEPOIS de um `glPushMatrix`: um pop numa
      // pilha vazia recusa, e a recusa seria verdadeira e inutil.
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
  std::vector<std::pair<int, std::uint32_t>> ordem;
  for (const auto& par : por_slot) ordem.push_back({prioridade(par.first), par.first});
  std::sort(ordem.begin(), ordem.end());

  int chamadas = 0, feitas = 0, recusadas = 0, nao_implementadas = 0;
  std::printf("%-4s %-24s %-10s %s\n", "slot", "metodo", "resultado", "detalhe");
  for (const auto& passo : ordem) {
    const Thunk& t = por_slot[passo.second];
    const Args a = argumentos(t.slot);
    std::uint32_t sp = kPilha;
    for (std::size_t k = 0; k < a.pilha.size(); ++k) {
      mem.Escrever32(sp + 4u * static_cast<std::uint32_t>(k), a.pilha[k]);
    }
    cpu.Repor(t.endereco, sp);
    for (int k = 0; k < 4; ++k) cpu.Set(kR0 + k, a.reg[k]);
    cpu.Set(kLR, kSentinela);

    std::string resultado = "sem_saida";
    std::string detalhe;
    for (int voltas = 0; voltas < 64; ++voltas) {
      cpu.Correr(1000000);
      const std::uint32_t pc = cpu.Get(kPC);
      std::uint32_t idx = 0;
      if (!cpu.GetSaidas().Contem(pc, &idx)) {
        resultado = "saiu_da_faixa";
        char buf[128];
        std::snprintf(buf, sizeof(buf), "pc=0x%08x lr=0x%08x (nao chegou a um slot)",
                      cpu.Get(kPC), cpu.Get(kLR));
        detalhe = buf;
        break;
      }
      if (idx == 3 || idx == 4) {  // a IBase: a convencao partilhada do despacho
        const std::uint32_t po = cpu.Get(kR0);
        const std::uint32_t n = mem.Ler32(po + 4);
        if (idx == 3) mem.Escrever32(po + 4, n + 1);
        cpu.Set(kR0, idx == 3 ? n + 1 : (n > 0 ? n - 1 : 0));
        cpu.Set(kPC, cpu.Get(kLR));
        if (cpu.Get(kLR) == kSentinela) break;
        continue;
      }
      zb2::brew::ArgumentosGl av;
      for (int k = 0; k < 4; ++k) av.reg[k] = cpu.Get(kR0 + k);
      av.sp = cpu.Get(kSP);
      av.lr = cpu.Get(kLR);
      std::uint32_t ret = 0;
      const zb2::brew::ResultadoGl r = igl.Executar(idx - zb2::brew::kVtableIgl, av, &ret);
      cpu.Set(kR0, ret);
      resultado = zb2::brew::Nome(r);
      detalhe = igl.Ultimas().back().motivo;
      ++chamadas;
      if (r == zb2::brew::ResultadoGl::Feito) ++feitas;
      else if (r == zb2::brew::ResultadoGl::Recusado) ++recusadas;
      else ++nao_implementadas;
      cpu.Set(kPC, cpu.Get(kLR));
      if (cpu.Get(kLR) == kSentinela) break;
    }
    if (t.slot == gl_slots::kIgl_GetIntegerv && !detalhe.empty()) {
      detalhe += " [-> " + std::to_string(mem.Ler32(destino_int)) + "]";
    }
    if (detalhe.size() > 66) detalhe = detalhe.substr(0, 66) + "...";
    std::printf("%-4u %-24s %-10s %s\n", t.slot, gl_slots::NomeIgl(t.slot), resultado.c_str(),
                detalhe.c_str());
  }

  // --- 4. o estado acumulado, e o que ficou por fazer ---------------------
  const float* m = igl.MatrizCorrente();
  std::printf("\n== estado acumulado ==\n");
  std::printf("chamadas ao IGL: %d (feitas %d | recusadas %d | nao implementadas %d)\n", chamadas,
              feitas, recusadas, nao_implementadas);
  std::printf("matriz de MODELVIEW (column-major, indice = coluna*4 + linha):\n");
  for (int r = 0; r < 4; ++r) {
    std::printf("  [%9.4f %9.4f %9.4f %9.4f]\n", m[r], m[4 + r], m[8 + r], m[12 + r]);
  }
  std::printf("cull face = 0x%04x | front face = 0x%04x | limpezas = %" PRIu64
              " | desenhos = %" PRIu64 " | vertices submetidos = %" PRIu64
              " | texturas geradas = %" PRIu64 "\n",
              igl.CullFace(), igl.FrontFace(), igl.Limpezas(), igl.Desenhos(), igl.Vertices(),
              igl.TexturasGeradas());
  const zb2::brew::ArrayDeVertices* v = igl.Array(gl_slots::GL_VERTEX_ARRAY);
  if (v != nullptr) {
    std::printf("array de vertices: tamanho=%d tipo=0x%04x passo=%u ponteiro=0x%08x\n", v->tamanho,
                v->tipo, v->passo, v->ponteiro);
  }
  std::printf("PIXELS: 0 -- nao ha rasterizador nesta etapa (o `glDrawArrays` recusa e di-lo)\n");

  std::printf("\n== recusas, por nome (o que a arvore antiga nao tinha em lado nenhum) ==\n");
  if (igl.Recusas().empty()) {
    std::printf("  nenhuma\n");
  } else {
    for (const auto& par : igl.Recusas()) {
      std::printf("  %-26s %" PRIu64 "x\n", par.first.c_str(), par.second);
    }
  }
  std::printf("\n== demanda medida: os slots que este titulo traz ==\n");
  for (const auto& par : por_slot) {
    std::printf("  %-3u %s\n", par.first, gl_slots::NomeIgl(par.first));
  }
  std::printf("\n%u thunks do IEGL existem no titulo; o IEGL NAO e servido nesta etapa,\n"
              "e por isso o `gpIEGL` do modulo fica a zero.\n", de_ieg);
  return 0;
}
