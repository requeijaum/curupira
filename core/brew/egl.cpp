#include "core/brew/egl.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "core/brew/interface.h"

namespace zb2::brew {

namespace {

using namespace gl_slots;

// ---------------------------------------------------------------------------
// A TABELA DO CONFIG
// ---------------------------------------------------------------------------
//
// Cada valor diz DE ONDE VEM, e a distincao entre "medido" e "declarado" esta
// escrita: `core/brew/tela.h` e o unico sitio deste emulador com pixels, e e
// portanto a unica coisa que um config pode prometer.


struct NomeDeAtributo {
  std::uint32_t id;
  const char* nome;
};
const NomeDeAtributo kAtributos[] = {
    {EGL_BUFFER_SIZE, "EGL_BUFFER_SIZE"}, {EGL_ALPHA_SIZE, "EGL_ALPHA_SIZE"},
    {EGL_BLUE_SIZE, "EGL_BLUE_SIZE"}, {EGL_GREEN_SIZE, "EGL_GREEN_SIZE"},
    {EGL_RED_SIZE, "EGL_RED_SIZE"}, {EGL_DEPTH_SIZE, "EGL_DEPTH_SIZE"},
    {EGL_STENCIL_SIZE, "EGL_STENCIL_SIZE"}, {EGL_CONFIG_CAVEAT, "EGL_CONFIG_CAVEAT"},
    {EGL_CONFIG_ID, "EGL_CONFIG_ID"}, {EGL_LEVEL, "EGL_LEVEL"},
    {EGL_MAX_PBUFFER_HEIGHT, "EGL_MAX_PBUFFER_HEIGHT"},
    {EGL_MAX_PBUFFER_PIXELS, "EGL_MAX_PBUFFER_PIXELS"},
    {EGL_MAX_PBUFFER_WIDTH, "EGL_MAX_PBUFFER_WIDTH"},
    {EGL_NATIVE_RENDERABLE, "EGL_NATIVE_RENDERABLE"},
    {EGL_NATIVE_VISUAL_ID, "EGL_NATIVE_VISUAL_ID"},
    {EGL_NATIVE_VISUAL_TYPE, "EGL_NATIVE_VISUAL_TYPE"},
    {EGL_SAMPLES, "EGL_SAMPLES"}, {EGL_SAMPLE_BUFFERS, "EGL_SAMPLE_BUFFERS"},
    {EGL_SURFACE_TYPE, "EGL_SURFACE_TYPE"}, {EGL_TRANSPARENT_TYPE, "EGL_TRANSPARENT_TYPE"},
    {EGL_NONE, "EGL_NONE"}, {EGL_BIND_TO_TEXTURE_RGB, "EGL_BIND_TO_TEXTURE_RGB"},
    {EGL_BIND_TO_TEXTURE_RGBA, "EGL_BIND_TO_TEXTURE_RGBA"},
    {EGL_MIN_SWAP_INTERVAL, "EGL_MIN_SWAP_INTERVAL"},
    {EGL_MAX_SWAP_INTERVAL, "EGL_MAX_SWAP_INTERVAL"},
    {EGL_LUMINANCE_SIZE, "EGL_LUMINANCE_SIZE"}, {EGL_ALPHA_MASK_SIZE, "EGL_ALPHA_MASK_SIZE"},
    {EGL_COLOR_BUFFER_TYPE, "EGL_COLOR_BUFFER_TYPE"},
    {EGL_RENDERABLE_TYPE, "EGL_RENDERABLE_TYPE"},
    {EGL_MATCH_NATIVE_PIXMAP, "EGL_MATCH_NATIVE_PIXMAP"}, {EGL_CONFORMANT, "EGL_CONFORMANT"},
    {EGL_VENDOR, "EGL_VENDOR"}, {EGL_VERSION, "EGL_VERSION"},
    {EGL_EXTENSIONS, "EGL_EXTENSIONS"}, {EGL_CLIENT_APIS, "EGL_CLIENT_APIS"},
    {EGL_HEIGHT, "EGL_HEIGHT"}, {EGL_WIDTH, "EGL_WIDTH"},
    {EGL_LARGEST_PBUFFER, "EGL_LARGEST_PBUFFER"},
    {EGL_TEXTURE_FORMAT, "EGL_TEXTURE_FORMAT"}, {EGL_TEXTURE_TARGET, "EGL_TEXTURE_TARGET"},
    {EGL_MIPMAP_TEXTURE, "EGL_MIPMAP_TEXTURE"}, {EGL_MIPMAP_LEVEL, "EGL_MIPMAP_LEVEL"},
    {EGL_RENDER_BUFFER, "EGL_RENDER_BUFFER"}, {EGL_SWAP_BEHAVIOR, "EGL_SWAP_BEHAVIOR"},
    {EGL_CONTEXT_CLIENT_TYPE, "EGL_CONTEXT_CLIENT_TYPE"},
    {EGL_CONTEXT_CLIENT_VERSION, "EGL_CONTEXT_CLIENT_VERSION"},
    {EGL_OPENGL_ES_API, "EGL_OPENGL_ES_API"}, {EGL_DRAW, "EGL_DRAW"}, {EGL_READ, "EGL_READ"},
    {EGL_CORE_NATIVE_ENGINE, "EGL_CORE_NATIVE_ENGINE"},
};

const char* NomeDoErro(std::uint32_t erro) {
  switch (erro) {
    case EGL_SUCCESS: return "EGL_SUCCESS";
    case EGL_NOT_INITIALIZED: return "EGL_NOT_INITIALIZED";
    case EGL_BAD_ACCESS: return "EGL_BAD_ACCESS";
    case EGL_BAD_ALLOC: return "EGL_BAD_ALLOC";
    case EGL_BAD_ATTRIBUTE: return "EGL_BAD_ATTRIBUTE";
    case EGL_BAD_CONFIG: return "EGL_BAD_CONFIG";
    case EGL_BAD_CONTEXT: return "EGL_BAD_CONTEXT";
    case EGL_BAD_CURRENT_SURFACE: return "EGL_BAD_CURRENT_SURFACE";
    case EGL_BAD_DISPLAY: return "EGL_BAD_DISPLAY";
    case EGL_BAD_MATCH: return "EGL_BAD_MATCH";
    case EGL_BAD_NATIVE_PIXMAP: return "EGL_BAD_NATIVE_PIXMAP";
    case EGL_BAD_NATIVE_WINDOW: return "EGL_BAD_NATIVE_WINDOW";
    case EGL_BAD_PARAMETER: return "EGL_BAD_PARAMETER";
    case EGL_BAD_SURFACE: return "EGL_BAD_SURFACE";
    default: return "erro_egl_sem_nome";
  }
}

}  // namespace

// A TABELA E PUBLICA (esta fora do namespace anonimo) porque o TESTE tem de
// poder comparar cada valor com a origem escrita ao lado -- uma tabela que so o
// proprio modulo ve nao se pode auditar (P1).
const AtributoDaConfig kConfigDoZeebulator[] = {
    // O TAMANHO E O FORMATO DO PIXEL -- da TELA, e nao de um folheto.
    // `tela.h`: `void CorAtual(std::uint32_t rgb565)`; 5+6+5 = 16.
    {EGL_BUFFER_SIZE, 16, "tela.h: o pixel da Tela e RGB565 (5+6+5)"},
    {EGL_RED_SIZE, 5, "tela.h: CorAtual(rgb565) -- 5 bits de vermelho"},
    {EGL_GREEN_SIZE, 6, "tela.h: CorAtual(rgb565) -- 6 bits de verde"},
    {EGL_BLUE_SIZE, 5, "tela.h: CorAtual(rgb565) -- 5 bits de azul"},
    // E o pedido do corpus confere: `ddragonz.mod` 0x14fc10 pede R=5, G=6, B=5.
    {EGL_ALPHA_SIZE, 0, "nao ha canal alfa em nenhum buffer deste emulador"},
    // HA BUFFER DE PROFUNDIDADE, e a linha anterior dizia o contrario: o
    // rasterizador deste emulador guarda um `std::vector<float> profundidade_`
    // (`core/video/rasterizador.h:243`), com `TemBufferDeProfundidade()` a dize-lo
    // (:210) e teste/escrita de profundidade (:166-168). **O config negava uma
    // coisa que o proprio modulo tem.**
    //
    // 16 e a profundidade do ecra do Zeebo (RGB565 + Z16); o zeebx declara o mesmo
    // (`src/video/gles.rs:84`), e o float do nosso buffer cobre essa precisao.
    //
    // CONSEQUENCIA MEDIDA de estar a 0: o `karnovr` pede `EGL_DEPTH_SIZE 16`, o
    // `eglChooseConfig` devolve 0 configs (a comparacao de tamanhos e
    // `pedido <= oferecido`, egl.cpp:560), o `eglCreateWindowSurface` recebe
    // config 0 e recusa, e o titulo desenha "InitGLSurface failed".
    {EGL_DEPTH_SIZE, 16, "rasterizador.h:210,243 tem buffer de profundidade; Z16 do ecra (zeebx src/video/gles.rs:84)"},
    // O BUFFER DE PROFUNDIDADE EXISTE DESDE QUE O RASTERIZADOR NASCEU.
    //
    // Este zero envelheceu: era verdade quando so havia a `Tela`, e deixou de
    // ser quando `core/video/rasterizador.cpp:558` passou a criar o buffer de
    // profundidade a pedido do estado (`PrepararProfundidade`, com o teste em
    // `:381-398`). Um valor que FOI medido e nao se reviu e uma mentira com
    // data -- e esta tinha consequencia: o `eglChooseConfig` compara "o config
    // tem PELO MENOS o pedido", logo um titulo que peca `EGL_DEPTH_SIZE 16`
    // -- o que qualquer jogo 3D pede -- recebia ZERO configs.
    //
    // 16 e o numero do HARDWARE, e nao um palpite: o guia oficial diz que o GPU
    // do Zeebo faz "Z buffer depth testing with 16-bit Z"
    // (`ZeeboDeveloperGuide0.97.md:1460-1463`). O zeebx (`gles.rs:128-138`) e o
    // zeebulator (`gl_hle.cpp:409-414`) dizem 16 tambem. O nosso buffer guarda
    // `float` (`rasterizador.h:243`), logo tem pelo menos essa precisao.
    {EGL_DEPTH_SIZE, 16,
     "rasterizador.cpp:558 cria o buffer de profundidade; 16 bits e o Z do "
     "hardware (ZeeboDeveloperGuide0.97.md:1460)"},
    {EGL_STENCIL_SIZE, 0, "nao ha buffer de stencil"},
    {EGL_LUMINANCE_SIZE, 0, "nao ha config de luminancia"},
    {EGL_ALPHA_MASK_SIZE, 0, "nao ha mascara de alfa"},
    {EGL_SAMPLES, 0, "nao ha multisampling"},
    {EGL_SAMPLE_BUFFERS, 0, "nao ha multisampling"},
    {EGL_LEVEL, 0, "nao ha configs em niveis de framebuffer (EGL_LEVEL 0)"},
    {EGL_CONFIG_ID, 1, "identificador desta unica config, escolhido por este modulo"},
    // JANELA E PIXMAP: o `eglCreatePbufferSurface` RECUSA, e o config tem de dizer
    // o mesmo que os
    // metodos fazem -- um config que prometesse PBUFFER_BIT seria uma promessa
    // que o modulo nao cumpre.
    {EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PIXMAP_BIT,
     "janela + pixmap (pixmap guardada sem raster, como a janela)"},
    // O IGL deste SDK e o do `AEEGL.h`: "OpenGLES 1.0 Common-Lite spec".
    {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES_BIT, "AEEGL.h: OpenGLES 1.0 Common-Lite spec"},
    {EGL_CONFORMANT, EGL_OPENGL_ES_BIT, "AEEGL.h: OpenGLES 1.0 Common-Lite spec"},
    {EGL_COLOR_BUFFER_TYPE, EGL_RGB_BUFFER, "tela.h: o pixel e RGB, nao luminancia"},
    {EGL_CONFIG_CAVEAT, EGL_NONE, "sem advert a config (EGL_NONE)"},
    // NAO HA RENDERIZACAO NATIVA: quem desenha (quando houver rasterizador) e o
    // emulador, sobre a `Tela`, e nao uma API nativa da maquina.
    {EGL_NATIVE_RENDERABLE, EGL_FALSE, "nao ha motor de desenho nativo"},
    {EGL_TRANSPARENT_TYPE, EGL_NONE, "nao ha transparencia de config"},
    {EGL_BIND_TO_TEXTURE_RGB, EGL_FALSE, "nao ha superficie ligada a textura"},
    {EGL_BIND_TO_TEXTURE_RGBA, EGL_FALSE, "nao ha superficie ligada a textura"},
    {EGL_MIN_SWAP_INTERVAL, 0, "nao ha controlo de intervalo de troca"},
    {EGL_MAX_SWAP_INTERVAL, 0, "nao ha controlo de intervalo de troca"},
    // O MAXIMO DE PBUFFER E ZERO, e e a verdade: nao ha memoria de pbuffer.
    {EGL_MAX_PBUFFER_WIDTH, 0, "nao ha memoria de pbuffer"},
    {EGL_MAX_PBUFFER_HEIGHT, 0, "nao ha memoria de pbuffer"},
    {EGL_MAX_PBUFFER_PIXELS, 0, "nao ha memoria de pbuffer"},
    // NAO ESTAO AQUI, de proposito, e a consulta RECUSA com o nome:
    //   EGL_NATIVE_VISUAL_ID / EGL_NATIVE_VISUAL_TYPE -- descrevem a janela
    //     nativa da MAQUINA, e nao ha medida nenhuma do que o Zeebo responde.
    //   EGL_MATCH_NATIVE_PIXMAP -- pseudo-atributo que so se pode responder
    //     quando existe uma pixmap nativa para comparar.
};

const std::size_t kQuantosAtributosDoConfig =
    sizeof(kConfigDoZeebulator) / sizeof(kConfigDoZeebulator[0]);

const char* NomeDoAtributoEgl(std::uint32_t atributo) {
  for (const auto& a : kAtributos) {
    if (a.id == atributo) return a.nome;
  }
  return nullptr;
}

Egl::Egl(Memoria& mem, Traco& traco) : mem_(mem), traco_(traco) {}

std::uint32_t Egl::Instalar(const Saidas& saidas) {
  objeto_ = kObjIegl;
  vtable_ = saidas.Endereco(kVtableIegl);
  ConstruirObjeto(mem_, saidas, objeto_, vtable_, kIeglSlots, kVtableIegl);
  // A LEITURA DE VOLTA, como no IGL: uma cablagem que nao se confirma a si
  // propria perde-se em silencio, e o sintoma aparece noutro sitio.
  for (std::uint32_t i = 0; i < kIeglSlots; ++i) {
    const std::uint32_t lido = mem_.Ler32(vtable_ + i * 4);
    std::uint32_t esperado = saidas.Endereco(kVtableIegl + i);
    if (i == 0) esperado = saidas.Endereco(3);
    if (i == 1) esperado = saidas.Endereco(4);
    if (lido != esperado) {
      char det[160];
      std::snprintf(det, sizeof(det),
                    "slot %u da vtable do IEGL tem 0x%08x, devia ter 0x%08x", i, lido, esperado);
      traco_.RegistarFalta(Area::Video, "cablagem_do_iegL_perdida", det);
      return 0;
    }
  }
  // O RASTERIZADOR EXISTE, E NAO E AQUI (`core/video/rasterizador.cpp`).
  //
  // AQUI ESTAVA UMA FALTA -- `rasterizador_de_GL`, "nenhum pixel e escrito (nao ha
  // rasterizador). Medido: 0 pixels em 62 titulos." -- e ela era verdadeira. Com o
  // rasterizador escrito, a mesma linha passaria a ser MENTIRA no minuto seguinte
  // (P7: um log so entra se puder ser verdadeiro). O que fica dito e o que este
  // modulo continua a ser: o config e as superficies. Quem desenha e o IGL, na
  // Tela que o despacho lhe liga (`igl_.DefinirTela`).
  traco_.Emitir(Area::Video, Nivel::Informacao, "IEGL_SEM_DESENHO_PROPRIO",
                "o IEGL serve o config e as superficies; os pixels sao escritos pelo IGL "
                "em core/video/rasterizador.cpp");
  traco_.Emitir(Area::Video, Nivel::Informacao, "IEGL_INSTALADO",
                "28 slots (AEEGL.h), objecto em 0x800B1000, vtable cablada e conferida");
  return kIeglSlots;
}

// --- leitura -----------------------------------------------------------------

std::uint32_t Egl::Arg(std::size_t i, const ArgumentosGl& a) const {
  if (i < 4) return a.reg[i];
  return mem_.Ler32(a.sp + static_cast<std::uint32_t>((i - 4) * 4));
}

const AtributoDaConfig* Egl::Atributo(std::uint32_t id) const {
  for (const auto& a : kConfigDoZeebulator) {
    if (a.id == id) return &a;
  }
  return nullptr;
}

// Le a `attrib_list` do guest ATE AO `EGL_NONE`. Nao interpreta nada aqui: o que
// esta na memoria fica na memoria (fica em `ultimos_atributos_`).
bool Egl::LerListaDeAtributos(std::uint32_t ponteiro, std::vector<std::uint32_t>* destino,
                              std::string* porque) {
  destino->clear();
  if (ponteiro == 0) return true;  // lista ausente: sao os valores por omissao
  constexpr std::uint32_t kMaxPares = 32;
  for (std::uint32_t k = 0; k < kMaxPares; ++k) {
    const std::uint32_t id = mem_.Ler32(ponteiro + 8 * k);
    if (id == EGL_NONE) return true;
    // UM ATRIBUTO DESCONHECIDO RECUSA. Aceitar pares de um ponteiro errado seria
    // ler lixo e chama-lo de config -- e a lista tem uma forma que se verifica.
    if (NomeDoAtributoEgl(id) == nullptr) {
      char det[96];
      std::snprintf(det, sizeof(det), "atributo 0x%08x em %u", id, k);
      *porque = std::string("atributo de EGL desconhecido no cabecalho deste modulo: ") + det;
      return false;
    }
    destino->push_back(id);
    destino->push_back(mem_.Ler32(ponteiro + 8 * k + 4));
  }
  *porque = "lista de atributos sem EGL_NONE depois de 32 pares";
  return false;
}

void Egl::EscreverString(std::uint32_t indice, const std::string& texto) {
  const std::uint32_t p = kZonaDeStrings + indice * kPassoDeString;
  for (std::size_t k = 0; k < texto.size() && k < kPassoDeString - 1; ++k) {
    mem_.Escrever8(p + static_cast<std::uint32_t>(k), static_cast<std::uint8_t>(texto[k]));
  }
  mem_.Escrever8(p + static_cast<std::uint32_t>(texto.size()), 0);
  strings_[indice] = texto;
}

std::string Egl::StringServida(std::uint32_t indice) const {
  const std::uint32_t p = kZonaDeStrings + indice * kPassoDeString;
  std::string s;
  for (std::uint32_t k = 0; k < kPassoDeString; ++k) {
    const std::uint8_t c = mem_.Ler8(p + k);
    if (c == 0) break;
    s.push_back(static_cast<char>(c));
  }
  return s;
}

// --- registo -----------------------------------------------------------------

ResultadoEgl Egl::Recusar(const std::string& motivo, std::uint32_t erro, ChamadaEgl& c) {
  c.motivo = motivo;
  c.resultado = ResultadoGl::Recusado;
  // O ESTADO DE ERRO DO EGL EXISTE E E MANTIDO, e nao "sempre zero". O IGL
  // recusa-se a inventar um codigo de `glGetError` (ver `igl.cpp`); aqui o codigo
  // nao e inventado -- e o que o proprio cabecalho define para a situacao, e o
  // jogo le-o com o `eglGetError` (medido: `ddragonz` 0x11d6f8 e 0x11d7f8).
  erro_ = erro;
  return ResultadoGl::Recusado;
}

ResultadoEgl Egl::NaoTem(ChamadaEgl& c) {
  c.motivo = "slot sem implementacao nesta etapa";
  c.resultado = ResultadoGl::NaoImplementado;
  return ResultadoGl::NaoImplementado;
}

void Egl::Registar(const ChamadaEgl& c) {
  char det[256];
  std::snprintf(det, sizeof(det), "slot=%u args=[%08x %08x %08x %08x] lr=0x%08x -> %s%s%s",
                c.slot, c.args[0], c.args[1], c.args[2], c.args[3], c.lr, Nome(c.resultado),
                c.motivo.empty() ? "" : " | ", c.motivo.c_str());
  traco_.Emitir(Area::Video, c.resultado == ResultadoGl::Feito ? Nivel::Informacao : Nivel::Aviso,
                "EGL_" + c.nome, det);
  if (c.resultado != ResultadoGl::Feito) {
    ++recusas_[c.nome];
    traco_.RegistarFalta(Area::Video, c.nome, c.motivo);
  }
  ultimas_.push_back(c);
  constexpr std::size_t kGuarda = 64;
  if (ultimas_.size() > kGuarda) ultimas_.erase(ultimas_.begin());
}

std::uint64_t Egl::ChamadasDoSlot(std::uint32_t slot) const {
  const auto it = por_slot_.find(slot);
  return it == por_slot_.end() ? 0 : it->second;
}

bool Egl::SuperficieValida(std::uint32_t s) const {
  for (const auto v : superficies_vivas_) {
    if (v == s) return true;
  }
  return false;
}

bool Egl::ContextoValido(std::uint32_t c) const {
  for (const auto v : contextos_vivos_) {
    if (v == c) return true;
  }
  return false;
}

std::uint32_t Egl::CriarSuperficie() {
  if (superficies_vivas_.size() >= kMaxSuperficies) return 0;
  const std::uint32_t s = kPrimeiraSuperficie + superficies_criadas_ * kPassoDeObjeto;
  ++superficies_criadas_;
  superficies_vivas_.push_back(s);
  return s;
}

std::uint32_t Egl::CriarContexto() {
  if (contextos_vivos_.size() >= kMaxContextos) return 0;
  const std::uint32_t c = kPrimeiroContexto + contextos_criados_ * kPassoDeObjeto;
  ++contextos_criados_;
  contextos_vivos_.push_back(c);
  return c;
}

// --- o despacho --------------------------------------------------------------

ResultadoEgl Egl::Executar(std::uint32_t slot, const ArgumentosGl& a, std::uint32_t* retorno) {
  using namespace gl_slots;
  if (retorno != nullptr) *retorno = 0;
  ++chamadas_;
  ++por_slot_[slot];
  ChamadaEgl c;
  c.slot = slot;
  c.nome = NomeIegl(slot);
  c.lr = a.lr;
  c.n_args = 4;
  for (int i = 0; i < 4; ++i) c.args[i] = a.reg[i];

  // O `sp` so interessa a partir do quinto argumento (`eglChooseConfig` tem
  // cinco), e um `sp` a zero leria o endereco zero e a recusa apareceria no sitio
  // errado -- o mesmo cuidado que o `igl.cpp` tem.
  const auto tem_pilha = [&](std::size_t ate) -> bool {
    if (ate <= 4) return true;
    if (a.sp < 0x00010000u) return false;
    for (std::size_t i = 4; i < ate; ++i) c.args[i] = Arg(i, a);
    return true;
  };

  const auto feito = [&](std::size_t usados, std::uint32_t r0) {
    c.n_args = usados;
    c.resultado = ResultadoGl::Feito;
    Registar(c);
    if (retorno != nullptr) *retorno = r0;
    return ResultadoGl::Feito;
  };
  const auto feito_com = [&](std::size_t usados, std::uint32_t r0, const std::string& motivo) {
    c.n_args = usados;
    c.motivo = motivo;
    c.resultado = ResultadoGl::Feito;
    Registar(c);
    if (retorno != nullptr) *retorno = r0;
    return ResultadoGl::Feito;
  };
  const auto recusa = [&](std::size_t usados, const std::string& motivo, std::uint32_t erro) {
    c.n_args = usados;
    Recusar(motivo, erro, c);
    Registar(c);
    return ResultadoGl::Recusado;
  };
  const auto sem = [&]() {
    NaoTem(c);
    Registar(c);
    return ResultadoGl::NaoImplementado;
  };
  // O display tem de ser o nosso. Aceitar um ponteiro qualquer poria o estado do
  // EGL num objecto que nao existe. O objeto QEGL (0x8F005000) tambem vale: o
  // QEGL delega no mesmo estado unico (titulos inicializam pelo QEGL).
  const auto display_ok = [&](std::uint32_t dpy, std::size_t usados) -> bool {
    if (dpy == kDisplayUnico || dpy == 0x8F005000u) return true;
    char det[64];
    std::snprintf(det, sizeof(det), "dpy=0x%08x nao e display deste emulador", dpy);
    recusa(usados, det, EGL_BAD_DISPLAY);
    return false;
  };
  const auto iniciado = [&](std::size_t usados) -> bool {
    if (iniciado_) return true;
    recusa(usados, "display nao inicializado (eglInitialize nao correu)", EGL_NOT_INITIALIZED);
    return false;
  };

  switch (slot) {
    // --- a cabeca: IBase + QueryInterface (AEEGL.h) -------------------------
    case kIegl_AddRef: {
      const std::uint32_t n = mem_.Ler32(a.reg[0] + 4) + 1;
      mem_.Escrever32(a.reg[0] + 4, n);
      return feito(1, n);
    }
    case kIegl_Release: {
      const std::uint32_t n = mem_.Ler32(a.reg[0] + 4);
      if (n == 0) return recusa(1, "Release de um objecto com contagem zero", EGL_BAD_DISPLAY);
      mem_.Escrever32(a.reg[0] + 4, n - 1);
      return feito(1, n - 1);
    }
    case kIegl_QueryInterface: {
      const std::uint32_t iid = a.reg[1], ppo = a.reg[2];
      if (ppo == 0) return recusa(3, "ppObj nulo", EGL_BAD_PARAMETER);
      if (iid == kClsidIegl) {
        mem_.Escrever32(ppo, objeto_);
        return feito(3, 0);  // SUCCESS
      }
      mem_.Escrever32(ppo, 0);
      return recusa(3, "IID nao servido por este objecto", EGL_BAD_PARAMETER);
    }

    // --- as consultas basicas ----------------------------------------------
    case kIegl_GetError: {
      const std::uint32_t e = erro_;
      erro_ = EGL_SUCCESS;  // a leitura CONSOME o erro, como o EGL define
      if (e == EGL_SUCCESS) return feito(0, e);
      return feito_com(0, e, std::string("devolve e limpa ") + NomeDoErro(e));
    }
    case kIegl_GetDisplay: {
      const std::uint32_t nativo = a.reg[0];
      if (nativo == kEglSemDisplay) {
        return feito_com(1, kDisplayUnico,
                         "EGL_DEFAULT_DISPLAY -> o proprio objecto IEGL (o unico display)");
      }
      // Um display nativo concreto existe no guest (o `ddragonz` passa
      // `[r5, #20]`), mas este emulador nao tem dois displays: o valor e
      // guardado no registo e nao interpretado, e o detalhe di-lo.
      return feito_com(1, kDisplayUnico,
                       "display nativo " + Hex(nativo) +
                           " nao interpretado: so existe um display (o objecto IEGL)");
    }
    case kIegl_Initialize: {
      const std::uint32_t dpy = a.reg[0];
      if (!display_ok(dpy, 3)) return ResultadoGl::Recusado;
      // A VERSAO E A DA INTERFACE, E NAO UMA INVENCAO. `AEEGL.h` declara o
      // AEECLSID_EGL como "EGL 1.0 spec"; o que a MAQUINA responderia nao foi
      // medido, e por isso o valor servido e 1.0 com esta nota. Um "1.4" saido do
      // cabecalho do SDK (`EGL/egl.h` e o cabecalho de referencia da 1.4) seria
      // uma afirmacao sobre o hardware.
      if (a.reg[1] != 0) mem_.Escrever32(a.reg[1], 1u);
      if (a.reg[2] != 0) mem_.Escrever32(a.reg[2], 0u);
      const bool ja = iniciado_;
      iniciado_ = true;
      return feito_com(3, EGL_TRUE,
                       ja ? "ja estava inicializado; versao 1.0 declarada (AEEGL.h)"
                          : "versao 1.0 declarada (AEEGL.h: 'EGL 1.0 spec'); a da maquina nao "
                            "foi medida");
    }
    case kIegl_Terminate: {
      if (!display_ok(a.reg[0], 1)) return ResultadoGl::Recusado;
      if (!iniciado_) return recusa(1, "display nao inicializado", EGL_NOT_INITIALIZED);
      iniciado_ = false;
      corrente_draw_ = corrente_read_ = corrente_ctx_ = 0;
      return feito(1, EGL_TRUE);
    }
    case kIegl_QueryString: {
      // NUNCA DEVOLVE NULO, e isto e uma decisao com medicao por tras: na arvore
      // antiga mediu-se um `strstr` sobre o resultado do `eglQueryString` sem
      // teste de nulo. Aqui a resposta e sempre um endereco valido na memoria do
      // guest; o que nao tem resposta verdadeira devolve a string VAZIA e fica
      // registado como falta, com o nome da consulta.
      if (!display_ok(a.reg[0], 2)) return ResultadoGl::Recusado;
      const std::uint32_t qual = a.reg[1];
      if (qual == EGL_VERSION) {
        EscreverString(0, "1.0");
        return feito_com(2, kZonaDeStrings + 0 * kPassoDeString,
                         "EGL_VERSION = \"1.0\" DECLARADO (AEEGL.h: 'EGL 1.0 spec'); a versao "
                         "da maquina nao foi medida");
      }
      if (qual == EGL_EXTENSIONS) {
        // A STRING VAZIA E A VERDADE, e nao uma recusa: este modulo nao implementa
        // extensao nenhuma (`glGetProcAddress`/`eglGetProcAddress` devolvem zero).
        EscreverString(1, "");
        return feito_com(2, kZonaDeStrings + 1 * kPassoDeString,
                         "EGL_EXTENSIONS = \"\": nenhuma extensao implementada nesta arvore");
      }
      if (qual == EGL_CLIENT_APIS) {
        EscreverString(2, "OpenGL_ES");
        return feito_com(2, kZonaDeStrings + 2 * kPassoDeString,
                         "EGL_CLIENT_APIS = \"OpenGL_ES\": a unica API servida (IGL)");
      }
      // O VENDOR E UMA AFIRMACAO SOBRE O FABRICANTE, e nao foi medido. A string
      // vazia mantem o guest vivo (um `strstr` sobre ela devolve nulo, e nao
      // rebenta) e a falta fica com o nome.
      if (qual == EGL_VENDOR) {
        EscreverString(1, "");
        return feito_com(2, kZonaDeStrings + 1 * kPassoDeString,
                         "EGL_VENDOR = \"\": sem medida do que a maquina responde");
      }
      EscreverString(1, "");
      char det[96];
      std::snprintf(det, sizeof(det), "consulta 0x%08x sem medida", qual);
      erro_ = EGL_BAD_PARAMETER;
      c.resultado = ResultadoGl::Recusado;
      c.n_args = 2;
      c.motivo = det;
      Registar(c);
      if (retorno != nullptr) *retorno = kZonaDeStrings + 1 * kPassoDeString;
      return ResultadoGl::Recusado;
    }
    case kIegl_GetProcAddress: {
      // ZERO E A RESPOSTA CERTA, e nao uma recusa: o EGL define que um nome
      // desconhecido devolve nulo, e as extensoes sao mesmo zero. O PEDIDO FICA
      // REGISTADO com o nome que o jogo pediu -- e assim que se aprende o que os
      // titulos procuram, em vez de "algo devolveu nulo".
      const std::uint32_t pnome = a.reg[0];
      std::string nome;
      if (pnome != 0) mem_.LerCadeia(pnome, &nome, 128);
      return feito_com(1, 0, "EGL_NO_PROC para \"" + nome + "\": nenhuma extensao implementada");
    }

    // --- configs -----------------------------------------------------------
    case kIegl_GetConfigs: {
      const std::uint32_t dpy = a.reg[0], lista = a.reg[1], tamanho = a.reg[2], pnum = a.reg[3];
      if (!display_ok(dpy, 4)) return ResultadoGl::Recusado;
      if (pnum == 0) return recusa(4, "num_config nulo", EGL_BAD_PARAMETER);
      const std::uint32_t quantos = (tamanho == 0 || lista == 0) ? 0 : 1;
      if (quantos == 1) mem_.Escrever32(lista, kConfigUnico);
      mem_.Escrever32(pnum, quantos);
      return feito(4, EGL_TRUE);
    }
    case kIegl_ChooseConfig: {
      if (!tem_pilha(5)) return recusa(4, "num_config na pilha sem sp valido", EGL_BAD_PARAMETER);
      const std::uint32_t dpy = a.reg[0], lista = a.reg[1], saida = a.reg[2];
      const std::uint32_t tamanho = a.reg[3], pnum = Arg(4, a);
      if (!display_ok(dpy, 5)) return ResultadoGl::Recusado;
      if (pnum == 0) return recusa(5, "num_config nulo", EGL_BAD_PARAMETER);
      std::string porque;
      if (!LerListaDeAtributos(lista, &ultimos_atributos_, &porque)) {
        return recusa(5, porque, EGL_BAD_ATTRIBUTE);
      }
      config_do_pedido_ = 0;
      // A COMPARACAO E A DO EGL, E NAO "ACEITA TUDO":
      //   mascara (SURFACE_TYPE, RENDERABLE_TYPE, CONFORMANT): o pedido tem de
      //     ser um SUBCONJUNTO do que o config oferece;
      //   tamanhos: o config tem de ter PELO MENOS o pedido;
      //   identidade (CONFIG_ID, LEVEL, COR): iguais.
      // Um pedido que o config nao serve devolve ZERO configs -- e o EGL diz que
      // isso e um sucesso, com o numero a zero. Nao e uma recusa do emulador: e o
      // emulador a dizer a verdade sobre o que tem.
      bool serve = true;
      std::string primeiro_que_falhou;
      for (std::size_t k = 0; k + 1 < ultimos_atributos_.size(); k += 2) {
        const std::uint32_t id = ultimos_atributos_[k];
        const std::uint32_t valor = ultimos_atributos_[k + 1];
        config_do_pedido_ = id;
        if (id == EGL_MATCH_NATIVE_PIXMAP) {
          serve = false;
          primeiro_que_falhou = "EGL_MATCH_NATIVE_PIXMAP: nao ha pixmap nativa para comparar";
          break;
        }
        const AtributoDaConfig* cfg = Atributo(id);
        if (cfg == nullptr) {
          serve = false;
          primeiro_que_falhou = std::string(NomeDoAtributoEgl(id)) + ": sem medida neste modulo";
          break;
        }
        bool casa = false;
        if (id == EGL_SURFACE_TYPE || id == EGL_RENDERABLE_TYPE || id == EGL_CONFORMANT) {
          casa = (valor & ~cfg->valor) == 0;  // o pedido tem de caber no oferecido
        } else if (id == EGL_CONFIG_ID || id == EGL_LEVEL || id == EGL_COLOR_BUFFER_TYPE ||
                   id == EGL_TRANSPARENT_TYPE || id == EGL_CONFIG_CAVEAT ||
                   id == EGL_NATIVE_RENDERABLE || id == EGL_BIND_TO_TEXTURE_RGB ||
                   id == EGL_BIND_TO_TEXTURE_RGBA) {
          casa = (valor == cfg->valor);
        } else if (id == EGL_MIN_SWAP_INTERVAL) {
          casa = (valor <= cfg->valor);
        } else if (id == EGL_MAX_SWAP_INTERVAL) {
          casa = (valor >= cfg->valor);
        } else {
          casa = (valor <= cfg->valor);  // tamanhos: o config tem de ter pelo menos
        }
        if (!casa) {
          serve = false;
          char det[128];
          std::snprintf(det, sizeof(det), "%s pedido %u, este config tem %u",
                        NomeDoAtributoEgl(id), valor, cfg->valor);
          primeiro_que_falhou = det;
          break;
        }
      }
      const std::uint32_t quantos = serve ? 1u : 0u;
      if (serve && saida != 0 && tamanho > 0) mem_.Escrever32(saida, kConfigUnico);
      mem_.Escrever32(pnum, quantos);
      if (!serve) {
        return feito_com(5, EGL_TRUE,
                         "0 configs: " + primeiro_que_falhou +
                             " (o config deste emulador esta em core/brew/egl.cpp)");
      }
      return feito(5, EGL_TRUE);
    }
    case kIegl_GetConfigAttrib: {
      const std::uint32_t dpy = a.reg[0], cfg = a.reg[1], id = a.reg[2], pvalor = a.reg[3];
      if (!display_ok(dpy, 4)) return ResultadoGl::Recusado;
      if (cfg != kConfigUnico) return recusa(4, "config nao e o deste emulador", EGL_BAD_CONFIG);
      if (pvalor == 0) return recusa(4, "ponteiro de saida nulo", EGL_BAD_PARAMETER);
      const AtributoDaConfig* v = Atributo(id);
      if (v == nullptr) {
        const char* nome = NomeDoAtributoEgl(id);
        return recusa(4,
                      nome != nullptr ? std::string(nome) + ": sem medida do que a maquina responde"
                                      : "atributo desconhecido no cabecalho deste modulo",
                      EGL_BAD_ATTRIBUTE);
      }
      mem_.Escrever32(pvalor, v->valor);
      return feito_com(4, EGL_TRUE, std::string("valor de ") + NomeDoAtributoEgl(id) + " (" +
                                        v->origem + ")");
    }

    // --- superficies -------------------------------------------------------
    case kIegl_CreateWindowSurface: {
      const std::uint32_t dpy = a.reg[0], cfg = a.reg[1], janela = a.reg[2];
      const std::uint32_t lista = a.reg[3];
      if (!display_ok(dpy, 4)) return ResultadoGl::Recusado;
      if (!iniciado(4)) return ResultadoGl::Recusado;
      if (cfg != kConfigUnico) return recusa(4, "config nao e o deste emulador", EGL_BAD_CONFIG);
      std::string porque;
      if (!LerListaDeAtributos(lista, &ultimos_atributos_, &porque)) {
        return recusa(4, porque, EGL_BAD_ATTRIBUTE);
      }
      const std::uint32_t s = CriarSuperficie();
      if (s == 0) return recusa(4, "limite de superficies deste modulo atingido", EGL_BAD_ALLOC);
      // A JANELA E UM OBJECTO DO GUEST (no `ddragonz` e um IDIB criado com o
      // AEECLSID 0x01001045), e nao ha janela nativa nenhuma neste emulador:
      // guarda-se o ponteiro e diz-se que nao e interpretado. Zero e aceite -- o
      // EGL permite-o, e o jogo que ainda nao tem IDIB chega aqui.
      return feito_com(4, s,
                       "superficie de janela criada; janela " + Hex(janela) +
                           " guardada e NAO interpretada (nao ha janela nativa); atributos: " +
                           std::to_string(ultimos_atributos_.size() / 2) +
                           " par(es) guardado(s) sem interpretacao");
    }
    case kIegl_CreatePixmapSurface: {
      // A pixmap e um IDIB do guest (fluxo OpenVG: GetDeviceBitmap + QI DIB).
      // Espelha a window: valida, cria handle, guarda o ponteiro sem interpretar.
      const std::uint32_t dpy = a.reg[0], cfg = a.reg[1], pixmap = a.reg[2];
      const std::uint32_t lista = a.reg[3];
      if (!display_ok(dpy, 4)) return ResultadoGl::Recusado;
      if (!iniciado(4)) return ResultadoGl::Recusado;
      if (cfg != kConfigUnico) return recusa(4, "config nao e o deste emulador", EGL_BAD_CONFIG);
      std::string porque;
      if (!LerListaDeAtributos(lista, &ultimos_atributos_, &porque)) {
        return recusa(4, porque, EGL_BAD_ATTRIBUTE);
      }
      const std::uint32_t s = CriarSuperficie();
      if (s == 0) return recusa(4, "limite de superficies deste modulo atingido", EGL_BAD_ALLOC);
      return feito_com(4, s,
                       "superficie de pixmap criada; pixmap " + Hex(pixmap) +
                           " guardada e NAO interpretada (sem rasterizador); atributos: " +
                           std::to_string(ultimos_atributos_.size() / 2) +
                           " par(es) guardado(s) sem interpretacao");
    }
    case kIegl_CreatePbufferSurface:
      return recusa(3, "nao ha memoria de pbuffer (o config tem EGL_MAX_PBUFFER_* a zero)",
                    EGL_BAD_MATCH);
    case kIegl_DestroySurface: {
      const std::uint32_t dpy = a.reg[0], s = a.reg[1];
      if (!display_ok(dpy, 2)) return ResultadoGl::Recusado;
      if (!SuperficieValida(s)) return recusa(2, "superficie nao e deste emulador", EGL_BAD_SURFACE);
      superficies_vivas_.erase(std::remove(superficies_vivas_.begin(), superficies_vivas_.end(), s),
                           superficies_vivas_.end());
      if (corrente_draw_ == s) corrente_draw_ = 0;
      if (corrente_read_ == s) corrente_read_ = 0;
      return feito(2, EGL_TRUE);
    }
    case kIegl_QuerySurface: {
      const std::uint32_t dpy = a.reg[0], s = a.reg[1], id = a.reg[2], pvalor = a.reg[3];
      if (!display_ok(dpy, 4)) return ResultadoGl::Recusado;
      if (!SuperficieValida(s)) return recusa(4, "superficie nao e deste emulador", EGL_BAD_SURFACE);
      if (pvalor == 0) return recusa(4, "ponteiro de saida nulo", EGL_BAD_PARAMETER);
      if (id == EGL_WIDTH) {
        mem_.Escrever32(pvalor, kLarguraDaSuperficie);
        return feito_com(4, EGL_TRUE,
                         "EGL_WIDTH = " + std::to_string(kLarguraDaSuperficie) +
                             ", do ecra (ecra.h: kLarguraDoEcra) -- sem rasterizador, "
                             "esta e a largura do unico framebuffer que existe");
      }
      if (id == EGL_HEIGHT) {
        mem_.Escrever32(pvalor, kAlturaDaSuperficie);
        return feito_com(4, EGL_TRUE,
                         "EGL_HEIGHT = " + std::to_string(kAlturaDaSuperficie) +
                             ", do ecra (ecra.h: kAlturaDoEcra) -- sem rasterizador");
      }
      if (id == EGL_CONFIG_ID) {
        mem_.Escrever32(pvalor, 1u);
        return feito(4, EGL_TRUE);
      }
      const char* nome = NomeDoAtributoEgl(id);
      return recusa(4,
                    nome != nullptr ? std::string(nome) + ": sem medida do que a maquina responde"
                                    : "atributo de superficie desconhecido",
                    EGL_BAD_ATTRIBUTE);
    }

    // --- contextos ---------------------------------------------------------
    case kIegl_CreateContext: {
      const std::uint32_t dpy = a.reg[0], cfg = a.reg[1], partilha = a.reg[2], lista = a.reg[3];
      if (!display_ok(dpy, 4)) return ResultadoGl::Recusado;
      if (!iniciado(4)) return ResultadoGl::Recusado;
      if (cfg != kConfigUnico) return recusa(4, "config nao e o deste emulador", EGL_BAD_CONFIG);
      if (partilha != kEglSemContexto && !ContextoValido(partilha)) {
        return recusa(4, "contexto de partilha nao e deste emulador", EGL_BAD_CONTEXT);
      }
      std::string porque;
      if (!LerListaDeAtributos(lista, &ultimos_atributos_, &porque)) {
        return recusa(4, porque, EGL_BAD_ATTRIBUTE);
      }
      for (std::size_t k = 0; k + 1 < ultimos_atributos_.size(); k += 2) {
        const std::uint32_t id = ultimos_atributos_[k], valor = ultimos_atributos_[k + 1];
        if (id == EGL_CONTEXT_CLIENT_VERSION && valor != 1) {
          // O UNICO CLIENTE E O GL ES 1.x (o IGL do AEEGL.h e o Common-Lite). Um
          // pedido de versao 2 seria servido por uma interface que nao existe
          // aqui, e aceita-lo seria mentir ao jogo sobre o que ele vai encontrar.
          char det[96];
          std::snprintf(det, sizeof(det), "EGL_CONTEXT_CLIENT_VERSION = %u", valor);
          return recusa(4, det, EGL_BAD_ATTRIBUTE);
        }
        if (id == EGL_CONTEXT_CLIENT_TYPE && valor != EGL_OPENGL_ES_API) {
          return recusa(4, "EGL_CONTEXT_CLIENT_TYPE diferente de EGL_OPENGL_ES_API",
                        EGL_BAD_ATTRIBUTE);
        }
      }
      const std::uint32_t ctx = CriarContexto();
      if (ctx == 0) return recusa(4, "limite de contextos deste modulo atingido", EGL_BAD_ALLOC);
      return feito_com(4, ctx,
                       "contexto criado; os atributos estao guardados sem interpretacao (o IGL "
                       "deste emulador tem UM estado, e nao um por contexto)");
    }
    case kIegl_DestroyContext: {
      const std::uint32_t dpy = a.reg[0], ctx = a.reg[1];
      if (!display_ok(dpy, 2)) return ResultadoGl::Recusado;
      if (!ContextoValido(ctx)) return recusa(2, "contexto nao e deste emulador", EGL_BAD_CONTEXT);
      contextos_vivos_.erase(std::remove(contextos_vivos_.begin(), contextos_vivos_.end(), ctx),
                             contextos_vivos_.end());
      if (corrente_ctx_ == ctx) corrente_ctx_ = 0;
      return feito(2, EGL_TRUE);
    }
    case kIegl_MakeCurrent: {
      const std::uint32_t dpy = a.reg[0], draw = a.reg[1], read = a.reg[2], ctx = a.reg[3];
      if (!display_ok(dpy, 4)) return ResultadoGl::Recusado;
      if (!iniciado(4)) return ResultadoGl::Recusado;
      if (ctx == kEglSemContexto) {
        // A libertação do contexto corrente, como o EGL define: os dois
        // superficies tem de ser EGL_NO_SURFACE.
        if (draw != kEglSemSuperficie || read != kEglSemSuperficie) {
          return recusa(4, "contexto nulo com superficie nao nula", EGL_BAD_MATCH);
        }
        corrente_draw_ = corrente_read_ = corrente_ctx_ = 0;
        return feito(4, EGL_TRUE);
      }
      if (!ContextoValido(ctx)) return recusa(4, "contexto nao e deste emulador", EGL_BAD_CONTEXT);
      if (!SuperficieValida(draw) || !SuperficieValida(read)) {
        return recusa(4, "superficie nao e deste emulador", EGL_BAD_SURFACE);
      }
      corrente_draw_ = draw;
      corrente_read_ = read;
      corrente_ctx_ = ctx;
      return feito_com(4, EGL_TRUE,
                       "contexto corrente posto; o IGL nao tem estado por contexto (um so objecto "
                       "GL, sem rasterizador)");
    }
    case kIegl_GetCurrentContext:
      return feito(0, corrente_ctx_);
    case kIegl_GetCurrentSurface: {
      const std::uint32_t qual = a.reg[0];
      if (qual == EGL_DRAW) return feito(1, corrente_draw_);
      if (qual == EGL_READ) return feito(1, corrente_read_);
      return recusa(1, "leitura diferente de EGL_DRAW/EGL_READ", EGL_BAD_PARAMETER);
    }
    case kIegl_GetCurrentDisplay:
      return feito(0, iniciado_ ? kDisplayUnico : kEglSemDisplay);
    case kIegl_QueryContext: {
      const std::uint32_t dpy = a.reg[0], ctx = a.reg[1], id = a.reg[2], pvalor = a.reg[3];
      if (!display_ok(dpy, 4)) return ResultadoGl::Recusado;
      if (!ContextoValido(ctx)) return recusa(4, "contexto nao e deste emulador", EGL_BAD_CONTEXT);
      if (pvalor == 0) return recusa(4, "ponteiro de saida nulo", EGL_BAD_PARAMETER);
      if (id == EGL_CONTEXT_CLIENT_VERSION) {
        mem_.Escrever32(pvalor, 1u);
        return feito_com(4, EGL_TRUE, "EGL_CONTEXT_CLIENT_VERSION = 1 (o IGL deste SDK)");
      }
      if (id == EGL_CONTEXT_CLIENT_TYPE) {
        mem_.Escrever32(pvalor, EGL_OPENGL_ES_API);
        return feito_com(4, EGL_TRUE, "EGL_CONTEXT_CLIENT_TYPE = EGL_OPENGL_ES_API");
      }
      const char* nome = NomeDoAtributoEgl(id);
      return recusa(4,
                    nome != nullptr ? std::string(nome) + ": sem medida do que a maquina responde"
                                    : "atributo de contexto desconhecido",
                    EGL_BAD_ATTRIBUTE);
    }

    // --- apresentacao ------------------------------------------------------
    case kIegl_WaitGL:
      return feito_com(0, EGL_TRUE, "nada pendente: nao ha comandos de GL em fila");
    case kIegl_WaitNative: {
      const std::uint32_t motor = a.reg[0];
      if (motor != EGL_CORE_NATIVE_ENGINE) {
        return recusa(1, "motor nativo desconhecido", EGL_BAD_PARAMETER);
      }
      return feito_com(1, EGL_TRUE, "nada a esperar: nao ha motor de desenho nativo");
    }
    case kIegl_SwapBuffers: {
      const std::uint32_t dpy = a.reg[0], s = a.reg[1];
      if (!display_ok(dpy, 2)) return ResultadoGl::Recusado;
      if (!SuperficieValida(s)) return recusa(2, "superficie nao e deste emulador", EGL_BAD_SURFACE);
      ++trocas_;
      // A TROCA E FEITA (o contador sobe, o jogo continua o seu laco) E A VERDADE
      // FICA DITA: nada foi desenhado, porque nao ha rasterizador. Recusar aqui
      // pararia o laco de quadro do jogo por um motivo que nao e dele.
      return feito_com(2, EGL_TRUE,
                       "troca contada; nada a apresentar -- nenhum rasterizador escreveu pixels");
    }
    case kIegl_CopyBuffers:
      return recusa(3, "nao ha pixmap de destino nem framebuffer de onde copiar", EGL_BAD_NATIVE_PIXMAP);

    default:
      return sem();
  }
}

}  // namespace zb2::brew
