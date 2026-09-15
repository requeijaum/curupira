#ifndef ZB2_CORE_BREW_CLASSES_H
#define ZB2_CORE_BREW_CLASSES_H

// AS CLASSES QUE O CORPUS CRIA POR `IShell::CreateInstance` E QUE O MOTOR NAO
// CONHECIA.
//
// O QUE ISTO RESOLVE, medido antes de existir (bateria, commit `ecc97b0`):
//
//     IShell::CreateInstance CLSID desconhecido  pedido 3x
//         iid=0x0100104f ppo=0x8007ff74   1x     (tectoy, o Z-Wheel)
//         iid=0x01003109 ppo=0x80200380   1x     (zenonia)
//         iid=0x01028e3c ppo=0x8020339c   1x     (tectoy)
//
// Os tres nomes saem de `tools/clsids.inc` (GERADO dos cabecalhos do SDK):
//
//     0x0100104f = AEECLSID_AppHistory    AEEAppHistory.bid:9   -> IAppHistory
//     0x01003109 = AEECLSID_TEXTCTL       AEEClassIDs.h:209      -> ITextCtl
//     0x01028e3c = AEECLSID_VALUEMODEL_1  AEECLSID_VALUEMODEL_1.bid:31 -> IValueModel
//
// O QUE FOI MEDIDO DO USO QUE OS TITULOS FAZEM DELAS -- e nao deduzido.
//
// Uma sonda temporaria (objecto com uma vtable de 64 slots, um endereco de saida
// distinto por slot, `ZB2_TRACE=1`) mostrou QUE SLOT cada titulo pede. Depois
// cada pedido foi conferido no DESMONTE do proprio modulo, porque a vtable e a
// assinatura sao duas fontes e tem de concordar:
//
//   tectoy  -- vtable 2200, `[r1,#0x14]` = slot 5 e `[r1,#0x04]` = slot 1:
//       0x045990  ldr  r2, [r1, #0x18]   ; slot 6 -- ITextCtl (outro titulo)
//       0x0006c3e8  Top    (slot 5)      ; 0x6c3e8 -> `ldr r2,[r1,#0x14]`
//       0x0006c410  Release(slot 1)
//   zenonia -- seis slots, todos conferidos no codigo em 0x0459xx e 0x06630c:
//
//       0x45998  ldr r2,[r1,#0x18]  slot 6   SetRect        r1 = &rect na pilha
//       0x459b0  ldr r2,[r1,#0x20]  slot 8   SetProperties  r1 = 0x80010000
//       0x459c8  ldr r2,[r1,#0x48]  slot 18  SetInputMode   r1 = 3
//       0x459e0  ldr r2,[r1,#0x10]  slot 4   SetActive      r1 = 1
//       0x66330  ldr r1,[r1,#0x14]  slot 5   IsActive
//       0x6635c  ldr ip,[r1,#0x08]  slot 2   HandleEvent
//
//   E OS ARGUMENTOS CONFEREM COM O CABECALHO (`platform/deprecated/inc/AEEText.h`):
//   `TP_FRAME|TP_FIXSETRECT` = 0x00010000|0x80000000 = **0x80010000**, exactamente
//   o literal em 0x45a08; `AEE_TM_LETTERS` = **3** (AEEText.h:76); `SetActive`
//   recebe `boolean` e leva **1**. A ordem dos slots tambem: o gerador
//   (`tools/gerar_slots.py`) resolve `QINTERFACE(ITextCtl)` + `DECLARE_IBASE`(2) +
//   `DECLARE_ICONTROL`(9) e poe o `SetRect` no 6, o `SetProperties` no 8, o
//   `SetInputMode` no 18 e o `SetActive` no 4 -- os MESMOS numeros que o modulo
//   pede. **Duas fontes independentes do SDK, e elas concordam.**
//
// O DESENHO, e o que NAO se faz:
//
//   - a classe passa a existir e o objecto e entregue. O `CreateInstance` de um
//     `AEECLSID_TEXTCTL` no aparelho a serio DEVOLVE um objecto -- recusa-lo era
//     a mentira, nao o contrario;
//   - CADA METODO nao implementado RECUSA em voz alta e registra-se com o NOME do
//     metodo (`ITextCtl::SetInputMode`), que e o que faz a lista de demanda passar
//     a dizer o que falta em vez de repetir tres numeros;
//   - o que se implementa e SO o que a medicao justifica: o `IAppHistory::Top`.
//     Nada de `bool` a fingir sucesso (P2) -- nem um objecto que se diz completo.
//
// PORQUE ISTO E UM BLOCO DE ENDERECOS PROPRIO, E PORQUE E EM `0x8F000000`.
//
// O mapa dos enderecos de objecto esta em `core/brew/igl.h` (comentario "o mapa
// medido dos enderecos de objecto deste emulador"); a ultima linha escrita la e
// `0x800B0000 IGL | 0x800B1000 IEGL`. A faixa de SAIDAS usada aqui e 40000 (as da
// entrada sao 20000+, as do IGL 30000/31000): uma faixa que se sobreponha a outra
// apaga uma vtable em silencio -- foi o defeito medido que obrigou o IGL a mudar
// de faixa.
//
// **`0x800C0000` NAO ESTAVA LIVRE, E ISSO FOI MEDIDO.** A primeira versao desta
// frente pos os tres objectos em `0x800C0000` (a "proxima linha livre" do mapa) e
// dois titulos mudaram de comportamento:
//
//     ./build/zb2_bateria "$corpus" "$mods" /tmp/depois.json
//     ./build/zb2_comparar /tmp/antes.json /tmp/depois.json
//       gof (277380): passos_create 1601656 -> 4000000
//                     recusadas      45 -> 3733093
//                     motivo "create:saiu_do_modulo_para_0x4278252046" -> "create:orcamento_esgotado"
//       rmp (278282): passos_create 1601028 -> 4000000
//                     recusadas       0 -> 3733092
//
// Nenhum dos dois pede nenhuma das tres classes (o `faltas` deles nao muda): o que
// muda e a MEMORIA. Eles LEEM `0x800C0000`, que estava a zero, e passam a ler o
// ponteiro da vtable. **Um endereco "livre" no mapa nao e um endereco que o
// CORPUS nao usa** -- o mapa foi escrito por quem instalou os objectos, e nao por
// quem mediu os 62 titulos.
//
// `0x8F000000` e o endereco que fica: acima de tudo o que o corpus mapeia (as
// imagens tem a base ZERO e menos de 1 MB; o heap e `0x80200000`+12 MB; a pilha
// `0x80080000`; a entrada `0x81030000`) e abaixo da faixa de saida (`0xF0000000`).
// A prova e a bateria inteira: com este endereco, os 62 titulos dao o MESMO
// resultado que antes, fora dos tres CLSIDs desta frente.

#include <cstdint>

#include "core/brew/ajudantes.h"
#include "core/brew/interface.h"
#include "core/cpu/cpu.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"

namespace zb2::brew {

// As tres classes conhecidas. A ORDEM e a ordem em que a bateria as encontrou.
enum class Classe : std::uint32_t {
  kAppHistory = 0,
  kValueModel_1 = 1,
  kTextCtl = 2,
  kThread = 3,
  kPNGDecoderBREW = 4,
  kQEGL = 5,
  kCM = 6,
  kQuantas = 7,
};

constexpr std::uint32_t kQuantasClasses = static_cast<std::uint32_t>(Classe::kQuantas);
// 32 slots por objecto: nenhuma das tres interfaces passa de 28 (`ITextCtl`,
// `AEEText.h`). O limite e o mesmo para as tres, para o calculo do indice ser uma
// divisao e nao uma tabela.
constexpr std::uint32_t kSlotsDaClasse = 32;
// A FAIXA DE SAIDAS destas classes. 40000 nao colide com 1000 (ajudantes), 2000
// (IShell), 6000/7000/8000/9500 (IDisplay/IFileMgr/DIB/ficheiro), 9000 (genericos),
// 1500-1564 (metodos), 20000 (entrada) nem 30000/31000 (IGL/IEGL).
//
// O IGLES11 vive em faixa propria (40300+, 148 slots): nao cabe nos 32 por
// classe, e alargar o passo partia a aritmetica de todas as outras.
constexpr std::uint32_t kVtableClasseBase = 40000;
// QEGL = IEGL sem GetProcAddress (slot 8): tudo a partir de 8 desloca -1
// (zeemu BrewEGL.cpp setup_vtables; 27 slots + Fn).
constexpr std::uint32_t kQeglSlots = 27;
constexpr std::uint32_t QeglParaIegl(std::uint32_t q) { return q >= 8 ? q + 1 : q; }
constexpr std::uint32_t kVtableIgles = 40300;
constexpr std::uint32_t kIglesSlots = 148;
constexpr std::uint32_t kObjetoIgles = 0x8F010000u;
// A ZONA DE STRINGS DO IGLES11 (`glGetString`). Um bloco de 4 KB a seguir ao
// objecto, DENTRO da mesma faixa `0x8F000000` que a bateria inteira ja provou
// que o corpus nao toca (ver o comentario do `0x800C0000` acima: um endereco
// "livre" no mapa nao e um endereco que o CORPUS nao usa, e isso foi MEDIDO).
//
// O desenho copia o do `core/brew/egl.h` (`kZonaDeStrings`): um endereco fixo
// por consulta, escrito quando a consulta acontece. **Nunca se devolve NULO** --
// ha um caso medido na arvore antiga (`ddragonz` mete o resultado do
// `eglQueryString` num `strstr` sem testar o nulo).
constexpr std::uint32_t kZonaDeStringsIgles = kObjetoIgles + 0x1000u;
constexpr std::uint32_t kPassoDeStringIgles = 0x100;

// O `IGLES11Ext` -- OUTRA interface, OUTRO objecto (AEEGLES11Ext.h,
// AEEIID_GLES11EXT = 0x0103d8eb): 3 + 12 = 15 slots.
//
// PORQUE EXISTE, MEDIDO (sonda: `cninja` com `GL_OES_draw_texture` anunciado no
// `glGetString`): o titulo passa a pedir `QEGL::QueryInterface` com OITO IIDs
// seguidos, e este e o que lhe da o `glDrawTexivOES` que a extensao promete.
// Os oito, todos identificados nos cabecalhos do SDK 4.0.2:
//     0x010426e3 EGLOESSWAPINTERVAL   0x0103d8ef EGLGETCOLORBUFFER
//     0x0103d8f0 EGLGETPOWERLEVEL     0x01051834 EGLSURFACEMANIP
//     0x0103d8de GLES10EXT            0x0103d8eb GLES11EXT   <-- este
//     0x0103def1 GLES11EXTPAK         0x01058546 GLESIMAGEONEXT
// As strings do `.mod` (`framework/GLES_ext.c`) dizem que o titulo TOLERA a
// ausencia dos outros ("platform does not support ... interface").
constexpr std::uint32_t kVtableIglesExt = 40500;
constexpr std::uint32_t kIglesExtSlots = 15;
constexpr std::uint32_t kObjetoIglesExt = 0x8F020000u;
constexpr std::uint32_t kIidGles11Ext = 0x0103d8ebu;
// IIDs de interface (AEEGLES10/11.h via 3 refs; sem .h no SDK extract).
constexpr std::uint32_t kIidGles10 = 0x0103d8ddu;
constexpr std::uint32_t kIidGles11 = 0x0103d8eau;
constexpr std::uint32_t VtClasse(std::uint32_t k) {
  return kVtableClasseBase + k * kSlotsDaClasse;
}
// Os enderecos dos objectos no espaco do guest. `0x800C0000 + k*0x1000`, um bloco
// de 4 KB por classe -- o primeiro livre depois do IGL/EGL em `0x800B0000`.
constexpr std::uint32_t ObjetoDaClasse(std::uint32_t k) { return 0x8F000000u + k * 0x1000u; }

// O indice da classe deste CLSID, ou `kQuantasClasses` quando nao e nenhuma
// delas. E o unico sitio onde os tres numeros aparecem.
std::uint32_t IndiceDaClasse(std::uint32_t clsid);

// O nome do CLSID (`AEECLSID_AppHistory`), o nome da interface que a classe
// entrega (`IAppHistory`) e o nome de UM slot dessa interface (`Top`).
const char* NomeDaClasse(std::uint32_t k);
const char* NomeDaInterface(std::uint32_t k);
const char* NomeDoSlotDaClasse(std::uint32_t k, std::uint32_t slot);

// O objecto desta classe, ou 0. Pura: o endereco nao depende de estado nenhum.
std::uint32_t ObjetoDoClsid(std::uint32_t clsid);

// `true` = este slot tem implementacao (e so o `IAppHistory::Top` o tem hoje).
bool SlotDaClasseImplementado(std::uint32_t k, std::uint32_t slot);

// Escreve os tres objectos e as tres vtables na memoria do guest, uma vez, com a
// faixa de saida ja montada. LE A CABLAGEM DE VOLTA e diz (no traco) se um slot
// ficou por cablar -- uma cablagem perdida numa edicao ja custou uma corrida
// inteira neste trabalho.
void ConstruirClasses(Memoria& mem, const Saidas& saidas, Traco& traco);

// O objeto IGLES11 (faixa propria): constroi vtable+objeto uma vez.
void ConstruirIgles(Memoria& mem, const Saidas& saidas, Traco& traco);
// Nome do slot IGLES11 e do slot IGLES11Ext, das tabelas GERADAS dos cabecalhos
// (`tools/igles_slots.inc`).
const char* NomeDoSlotIgles(std::uint32_t slot);
const char* NomeDoSlotIglesExt(std::uint32_t slot);

// Repoem o estado do ITextCtl (ativo/props/modo) no arranque. Sem isto, dois
// testes na mesma Bancada veriam o estado um do outro -- statics partilhados.
void ReporEstadoTextCtl();

// Atende um pedido desta faixa. `false` = o indice nao e destas classes (e o
// chamador segue a cadeia de ramos). `true` = foi atendido, com sucesso OU com
// recusa REGISTADA.
bool AtenderClasse(ICpu& cpu, std::uint32_t indice, Traco& traco);

}  // namespace zb2::brew

#endif  // ZB2_CORE_BREW_CLASSES_H
