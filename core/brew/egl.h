#ifndef ZB2_CORE_BREW_EGL_H
#define ZB2_CORE_BREW_EGL_H

// O IEGL: a interface de EGL 1.0 do BREW -- os 28 slots de `AEEGL.h`.
//
// PORQUE ISTO EXISTE, e nao e "por simetria com o IGL". O wrapper de GL que os
// jogos trazem compilado DENTRO do `.mod` (o `GLES_1x.c` do SDK) chama o EGL
// ANTES de qualquer `gl*`, e mediu-se isso no corpo do corpus:
//
//   `ddragonz.mod`, com o ficheiro carregado em 0x00100000 (o deslocamento de
//   ficheiro e o endereco menos 0x00100000). O `GLES_Init` do titulo esta em
//   0x11d6c4-0x11d890:
//
//     11d6c4  bl 0x123e58        ; eglGetDisplay      (slot 4)
//     11d6d4  bl 0x123e74        ; eglGetError        (slot 3)
//     11d6d8  cmp r0, #0x3000    ; EGL_SUCCESS
//     11d6ec  bl 0x123eac        ; eglInitialize      (slot 5)
//     11d740  bl 0x123dac        ; eglChooseConfig    (slot 10)
//     11d7a4  bl 0x123e04        ; eglCreateWindowSurface (slot 12)
//     11d7e8  bl 0x123de8        ; eglCreateContext   (slot 17)
//     11d814  bl 0x123ec8        ; eglMakeCurrent     (slot 19)
//
//   Logo: sem o IEGL, NENHUM `gl*` do jogo chega a acontecer. O IGL sozinho nao
//   serve para nada -- e foi essa a medicao que decidiu esta etapa.
//
// A TABELA DE SLOTS VEM DE `tools/gl_slots.inc`, GERADO de `AEEGL.h` por
// `tools/gerar_slots.py`, e conferida contra os 102 thunks `gl*`/`egl*`
// desmontados do `conftest.elf` do proprio SDK. Copiar a ordem de outro emulador
// esta proibido POR TESTE (`tools/verificar_slots_gl.sh`).
//
// AS CONSTANTES DO EGL (`EGL_SUCCESS`, `EGL_WINDOW_BIT`, ...) tambem SAO GERADAS,
// de `platform/ui/inc/EGL/egl.h`. Nao ha neste ficheiro um so numero da ABI
// escrito a mao -- so os CASTS do cabecalho (`EGL_NO_SURFACE` = `((EGLSurface)0)`
// e companhia), que o ARM carrega como zero e que ficam aqui com a linha citada.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core/brew/ecra.h"       // kLarguraDoEcra, kAlturaDoEcra
#include "core/brew/igl.h"        // ResultadoGl, ArgumentosGl, kVtableIegl, kObjIegl
#include "core/cpu/cpu.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"
#include "tools/gl_slots.inc"

namespace zb2::brew {

// OS TRES VALORES DO DESENHO, COM O MESMO VOCABULARIO DA FRONTEIRA DE HLE.
//
// `ResultadoGl` ja diz "feito / recusado / nao implementado" e o `ArgumentosGl`
// ja transporta os quatro registos, o `sp` e o `lr`. O IEGL precisa da mesma
// coisa: e o mesmo despacho, com outra tabela. Um segundo enum de tres valores
// identicos seria uma segunda verdade sobre o que "recusado" quer dizer.
using ResultadoEgl = ResultadoGl;
using ChamadaEgl = ChamadaGl;

// --- os CASTS do cabecalho ------------------------------------------------
//
// `platform/ui/inc/EGL/egl.h`:
//     #define EGL_DEFAULT_DISPLAY  ((EGLNativeDisplayType)0)
//     #define EGL_NO_CONTEXT       ((EGLContext)0)
//     #define EGL_NO_DISPLAY       ((EGLDisplay)0)
//     #define EGL_NO_SURFACE       ((EGLSurface)0)
//     #define EGL_DONT_CARE        ((EGLint)-1)
//     #define EGL_UNKNOWN          ((EGLint)-1)
//
// O que o ARM carrega e 0, 0, 0, 0, -1 e -1. Sao os unicos valores do ficheiro
// que o gerador NAO pode ler por `re` (sao castes, e nao literais numericos), e
// por isso ficam aqui -- com a linha que os define.
constexpr std::uint32_t kEglSemDisplay = 0;
constexpr std::uint32_t kEglSemSuperficie = 0;
constexpr std::uint32_t kEglSemContexto = 0;
constexpr std::int32_t kEglTantoFaz = -1;
constexpr std::int32_t kEglDesconhecido = -1;

// --- os handles deste modulo ----------------------------------------------
//
// UM HANDLE DO GUEST POR OBJECTO NOSSO, como no resto deste emulador: um valor
// nao nulo, distinto, e sem memoria por baixo que o guest possa ler.
//
// O DISPLAY e o PROPRIO objecto IEGL. Nao e invencao: neste SDK quem cria o
// objecto e o `IShell::CreateInstance(AEECLSID_EGL)`, e o que o `GLES_Init` do
// jogo faz com o resultado e exactamente guarda-lo e passa-lo como `dpy` a todos
// os metodos (medido em `ddragonz`, 0x11d6c4-0x11d8f8: `str r0,[r4,#8]` uma vez, e
// depois `ldr r0,[r4,#8]` antes de cada chamada).
constexpr std::uint32_t kDisplayUnico = kObjIegl;
// O UNICO config. Medido: o `ddragonz` pede UM (`r3 = 1` no `eglChooseConfig` em
// 0x11d738) e usa o primeiro da lista que recebe.
constexpr std::uint32_t kConfigUnico = kObjIegl + 0x100u;
constexpr std::uint32_t kPassoDeObjeto = 0x10;
constexpr std::uint32_t kMaxSuperficies = 16;
constexpr std::uint32_t kMaxContextos = 8;
constexpr std::uint32_t kPrimeiraSuperficie = kObjIegl + 0x200u;
constexpr std::uint32_t kPrimeiroContexto = kObjIegl + 0x300u;
// As strings que o `eglQueryString` devolve ficam na memoria do guest, porque e
// um `const char*` que o jogo le. 64 bytes por string, tres strings.
constexpr std::uint32_t kZonaDeStrings = kObjIegl + 0x400u;
constexpr std::uint32_t kPassoDeString = 0x40;
constexpr std::uint32_t kQuantasStrings = 3;

// O TAMANHO DA SUPERFICIE E O DO ECRA (`core/brew/ecra.h`). Nao ha aqui numero
// escrito a mao: um 640 copiado e uma segunda verdade a espera de divergir, e
// foi exactamente assim que o `GetDeviceInfo` ficou a publicar 320x240 enquanto
// o resto do emulador desenhava em 640x480. O `egl_test.cpp` mantem o
// `static_assert(kLarguraDaSuperficie == Tela::kLargura)` como segunda guarda.
constexpr std::uint32_t kLarguraDaSuperficie = kLarguraDoEcra;  // ecra.h
constexpr std::uint32_t kAlturaDaSuperficie = kAlturaDoEcra;    // ecra.h

// O NOME DOS ATRIBUTOS, para a recusa ser legivel. Um `EGL_BAD_ATTRIBUTE` com o
// numero e uma recusa sem nome -- o defeito que a arvore antiga tinha em 71 dos
// 84 handlers de GL.
const char* NomeDoAtributoEgl(std::uint32_t atributo);

// --- os IIDs da familia GL que o QueryInterface responde --------------------
//
// O CONTRATO, medido no zeebx (`src/machine.rs` 9815-9860, `egl_query_interface`):
// o objecto `AEECLSID_QEGL` responde por VARIAS interfaces. Devolver `this`
// para tudo entregava a vtable do EGL a quem pediu a do GL -- a primeira
// chamada caia num slot que nao existe. Os valores sao os do SDK 4.0.2 (o
// ficheiro e a linha ao lado); os GLES10/11/11EXT ja vivem em `classes.h`
// (`kIidGles10/11/11Ext`), e este e o UNICO sitio onde os outros aparecem.
constexpr std::uint32_t kIidGles10Ext = 0x0103d8deu;         // AEEGLES10Ext.h:22
constexpr std::uint32_t kIidGles11ExtPak = 0x0103def1u;      // AEEGLES11ExtPak.h:22
constexpr std::uint32_t kIidEgl10 = 0x0103d8edu;             // AEEEGL10.h:22
constexpr std::uint32_t kIidEgl11 = 0x0103d8eeu;             // AEEEGL11.h:20
constexpr std::uint32_t kIidEglGetColorBuffer = 0x0103d8efu; // AEEEGLGetColorBuffer.h:20
constexpr std::uint32_t kIidEglGetPowerLevel = 0x0103d8f0u;  // AEEEGLGetPowerLevel.h:20
constexpr std::uint32_t kIidEglOesSwapInterval = 0x010426e3u;  // AEEEGLOESSwapInterval.h:22
constexpr std::uint32_t kIidEglSurfaceManipV1 = 0x010434ccu; // AEEEGLSurfaceManip.h:23
constexpr std::uint32_t kIidEglSurfaceManip = 0x01051834u;   // AEEEGLSurfaceManip.h:252
constexpr std::uint32_t kIidGlesImageonExtV1 = 0x010459b1u;  // AEEGLESImageonEXT.h:23
constexpr std::uint32_t kIidGlesImageonExt = 0x01058546u;    // AEEGLESImageonEXT.h:204

// O NOME de UM IID da familia, para a recusa dizer O QUE falta (P2): um
// "IID nao servido" sem nome obrigava a ir ao cabecalho contar numeros em
// cada ronda. `"iid_fora_da_tabela"` para um valor fora dela.
const char* NomeDoIidDaFamiliaGl(std::uint32_t iid);

// UMA LINHA DA TABELA DO CONFIG, com a ORIGEM do valor.
//
// ESTA TABELA E O CORACAO DA HONESTIDADE DESTE MODULO. O `eglChooseConfig` so
// pode responder o que o emulador CONSEGUE mesmo apresentar, e o emulador tem uma
// coisa para apresentar: a `core/brew/tela.h`, 640x480, com o pixel em RGB565
// (`Tela::CorAtual(std::uint32_t rgb565)`, tela.h linha 39). Logo:
//
//   R = 5, G = 6, B = 5, A = 0   -- o formato da tela, e nao um 8888 de folheto
//   depth = 0, stencil = 0       -- nao ha buffer de profundidade nem de stencil
//   SURFACE_TYPE = WINDOW_BIT    -- so superficies de janela existem aqui
//
// E o pedido do corpus CONFERE com isto. MEDIDO no `ddragonz.mod`: a lista de
// atributos do `eglChooseConfig` esta em 0x14fc10 (o literal esta em 0x11d978, e
// o `add pc` do codigo em 0x11d704 resolve-o), e o conteudo e:
//
//   [ 0] 0x00003033 SURFACE_TYPE = 4 = EGL_WINDOW_BIT
//   [ 4] 0x00003024 RED_SIZE     = 5
//   [ 8] 0x00003023 GREEN_SIZE   = 6
//   [12] 0x00003022 BLUE_SIZE    = 5
//   [16] 0x00003038 EGL_NONE     (fim da lista)
//
// **O jogo pede exactamente o que a tela tem.** Um config "plausivel" de 32 bits
// teria recusado este pedido -- e a recusa seria do emulador, nao do jogo.
struct AtributoDaConfig {
  std::uint32_t id;
  std::uint32_t valor;
  const char* origem;  // de onde vem o numero (P1)
};
extern const AtributoDaConfig kConfigDoZeebulator[];
extern const std::size_t kQuantosAtributosDoConfig;

// O IEGL, como o guest o ve.
class Egl {
 public:
  Egl(Memoria& mem, Traco& traco);

  // Escreve o objecto e a vtable NA MEMORIA DO GUEST, com um endereco de saida
  // por slot, e CONFIRMA com leitura de volta (uma cablagem ja se perdeu em
  // silencio neste trabalho). Devolve quantos slots foram cablados, ou 0.
  std::uint32_t Instalar(const Saidas& saidas);

  std::uint32_t Objeto() const { return objeto_; }
  std::uint32_t Vtable() const { return vtable_; }

  // O DESPACHO: chamado quando o PC entra no endereco de saida do slot.
  ResultadoEgl Executar(std::uint32_t slot, const ArgumentosGl& a, std::uint32_t* retorno);

  // --- o estado que a sonda e o teste observam -----------------------------
  bool Iniciado() const { return iniciado_; }
  std::uint32_t Erro() const { return erro_; }
  std::uint32_t SuperficiesCriadas() const { return superficies_criadas_; }
  std::uint32_t ContextosCriados() const { return contextos_criados_; }
  std::size_t SuperficiesVivas() const { return superficies_vivas_.size(); }
  std::size_t ContextosVivos() const { return contextos_vivos_.size(); }
  std::uint32_t SuperficieCorrente() const { return corrente_draw_; }
  std::uint32_t ContextoCorrente() const { return corrente_ctx_; }
  std::uint64_t Trocas() const { return trocas_; }
  std::uint64_t Chamadas() const { return chamadas_; }
  std::uint64_t ChamadasDoSlot(std::uint32_t slot) const;
  // O ultimo `attrib_list` lido do guest, em pares (id, valor), tal como chegou.
  // Nao e uma interpretacao: e o que estava na memoria do guest.
  const std::vector<std::uint32_t>& UltimosAtributos() const { return ultimos_atributos_; }
  std::uint32_t UltimoConfigPedido() const { return config_do_pedido_; }
  // A lista do que falta, por nome (P2/P7: o que nao tem implementacao fica dito).
  const std::map<std::string, std::uint64_t>& Recusas() const { return recusas_; }
  const std::vector<ChamadaEgl>& Ultimas() const { return ultimas_; }
  // O texto que ficou na memoria do guest para uma das strings servidas.
  std::string StringServida(std::uint32_t indice) const;

  // RESOLVE UM NOME DE FUNCAO GL PARA UM ENDERECO DE TRAMPOLIM DO IGL.
  // Devolve o endereco do trampolim (na faixa de saidas do IGL) quando o nome
  // existe na tabela do IGL, ou 0 quando nao existe. O nome e SEM o sufixo ARB
  // ou OES (o `eglGetProcAddress` tira-o antes de chamar esta funcao).
  // E publico porque o teste o chama directamente: um ponteiro de funcao que
  // o teste nao pode construir so (P1).
  std::uint32_t ResolverGlProc(const std::string& nome) const;

 private:
  ResultadoEgl Recusar(const std::string& motivo, std::uint32_t erro, ChamadaEgl& c);
  ResultadoEgl NaoTem(ChamadaEgl& c);
  void Registar(const ChamadaEgl& c);
  std::uint32_t Arg(std::size_t i, const ArgumentosGl& a) const;
  bool LerListaDeAtributos(std::uint32_t ponteiro, std::vector<std::uint32_t>* destino,
                           std::string* porque);
  void EscreverString(std::uint32_t indice, const std::string& texto);
  const AtributoDaConfig* Atributo(std::uint32_t id) const;
  std::uint32_t CriarSuperficie();
  std::uint32_t CriarContexto();
  bool SuperficieValida(std::uint32_t s) const;
  bool ContextoValido(std::uint32_t c) const;

  Memoria& mem_;
  Traco& traco_;
  std::uint32_t objeto_ = 0, vtable_ = 0;
  Saidas saidas_;

  bool iniciado_ = false;
  std::uint32_t erro_ = gl_slots::EGL_SUCCESS;
  std::uint32_t superficies_criadas_ = 0;  // quantas superficies foram criadas
  std::uint32_t contextos_criados_ = 0;
  std::vector<std::uint32_t> superficies_vivas_;
  std::vector<std::uint32_t> contextos_vivos_;
  std::uint32_t corrente_draw_ = 0, corrente_read_ = 0, corrente_ctx_ = 0;
  std::uint64_t trocas_ = 0;
  std::uint64_t chamadas_ = 0;
  std::map<std::uint32_t, std::uint64_t> por_slot_;
  std::map<std::string, std::uint64_t> recusas_;
  std::vector<ChamadaEgl> ultimas_;
  std::vector<std::uint32_t> ultimos_atributos_;
  std::uint32_t config_do_pedido_ = 0;
  std::map<std::uint32_t, std::string> strings_;
};

}  // namespace zb2::brew

#endif  // ZB2_CORE_BREW_EGL_H