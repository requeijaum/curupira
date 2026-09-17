#ifndef ZB2_CORE_BREW_DESPACHO_H
#define ZB2_CORE_BREW_DESPACHO_H

// O DESPACHO DE HLE: o que o emulador faz quando o guest chama o sistema.
//
// VIVE NO MOTOR, e nao na ferramenta. Isto era o maior pedaco de comportamento do
// emulador dentro de `tools/bateria.cpp` -- cerca de 860 linhas -- e enquanto la
// esteve, **so a ferramenta de medicao sabia correr um jogo**, e nenhum teste
// chegava a nada disto.
//
// O desenho: o modulo do guest descobre o sistema por uma tabela de ponteiros de
// funcao (`AEEHelperFuncs`) e por vtables de interface. Esses ponteiros apontam
// para a FAIXA DE SAIDA; o laco de execucao para quando o PC entra nela, e este
// despacho decide pelo INDICE. Um endereco unico por slot e o que faz o registo
// dizer QUAL metodo foi pedido, em vez de "algo do shell".

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/audio/misturador.h"
#include "core/brew/ajudantes.h"
#include "core/brew/cheats.h"
#include "core/brew/recursos.h"
#include "core/brew/sql.h"

// A CABLAGEM DO GL (etapa 6): o IGL e o IEGL. Ver docs/rewrite/REMENDO-GL-DESPACHO.md.
//
// ESTE `#define` E A GUARDA DO TESTE. O `tests/gl_cablagem_test.cpp` corre de
// verdade quando ele existe e SALTA com o motivo escrito quando nao existe -- e um
// teste que passa sem ter corrido e pior do que um teste vermelho.
#define ZB2_CABLAGEM_GL 1
#include "core/brew/egl.h"
#include "core/brew/igl.h"

// A CABLAGEM DO GL (etapa 6): o IGL e o IEGL. Ver docs/rewrite/REMENDO-GL-DESPACHO.md.
//
// ESTE `#define` E A GUARDA DO TESTE. O `tests/gl_cablagem_test.cpp` corre de
// verdade quando ele existe e SALTA com o motivo escrito quando nao existe -- e um
// teste que passa sem ter corrido e pior do que um teste vermelho.
#define ZB2_CABLAGEM_GL 1
#include "core/brew/egl.h"
#include "core/brew/igl.h"
#include "core/brew/ihiddevice.h"
#include "core/brew/arquivo.h"
#include "core/brew/imedia.h"
#include "core/brew/interface.h"
#include "core/brew/tela.h"
#include "core/brew/vfs.h"
#include "core/brew/widget.h"
#include "core/cpu/cpu.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"

namespace zb2::brew {

// --- IShell::DetectType (slot 43): A ZONA DAS CADEIAS DE MIME ----------------
//
// O `DetectType` recebe um `const char **pcpszMIME` -- ou seja, DEVOLVE UM
// ENDERECO DE MEMORIA DO GUEST com a cadeia. A zona vive na pagina do objecto
// do shell, pela mesma razao e com a mesma forma do `Egl`
// (`core/brew/egl.h:99`, `kZonaDeStrings = kObjIegl + 0x400`): a pagina de um
// objecto nosso e nossa, e uma cadeia constante tem de estar num sitio que o
// guest possa ler. 32 bytes por cadeia, e o `+0x800` fica guardado para o
// rascunho do `pp` (4 bytes) que o `LoadResObject` precisa.
constexpr std::uint32_t kZonaDeMimesDoShell = kObjShell + 0x400u;
constexpr std::uint32_t kPassoDeMime = 0x20;
constexpr std::uint32_t kMaximoDeMimes = 16;
constexpr std::uint32_t kRascunhoDoShell = kObjShell + 0x800u;

// --- O SQL (ISQLMgr + ISQLDatabase) -----------------------------------------
//
// O MOTOR DE SQL DO CONSOLE ESTA EM `core/brew/sql.{h,cpp}`: a ponte sobre o
// SQLite de verdade (amalgamacao em `third_party/sqlite3/`). O que fica AQUI e a
// ENTREGA DA LINHA ao callback do jogo, que precisa do nucleo e da memoria do
// guest, e as zonas onde os textos dessa linha sao escritos.
//
// A ZONA DE UMA LINHA DE CONSULTA vive na pagina do objecto do banco
// (`kObjSqlDb` = 0x81080000), como as cadeias do `DetectType` vivem na do shell e
// pela mesma razao: uma cadeia que o guest le tem de estar num sitio que o guest
// ve. O `sqlite3_exec` chama o callback UMA VEZ POR LINHA, logo a zona so precisa
// de UMA linha de cada vez -- e nao de uma tabela.
//
// O TAMANHO NAO E ESCOLHA LIVRE: e MEDIDO no dialecto do proprio modulo.
//   - COLUNAS: a consulta mais larga que o `tectoy` manda e
//     `SELECT * FROM GAMEINFO, TITLETEXT ...` = 8 + 3 = ONZE colunas (o `ASSETS`
//     do catalogo tem NOVE, o `PREFSINFO` quatro). O tecto e 12 -- uma de folga, e
//     nao um numero redondo escolhido a sorte;
//   - TEXTO: o valor mais comprido dos quatro bancos REAIS da midia
//     (`tt_prefs.db`, `asset_cache`, `tt_game_info`, `tt_dlqueue.db`, 502 valores
//     de texto contados) tem 45 caracteres -- um URL em `PREFSINFO.strValue`. O
//     passo e 0x40 (63 caracteres mais o NUL).
//
// UM VALOR QUE NAO CABE NAO SE TRUNCA EM SILENCIO: e uma FALTA com o nome da
// coluna e o comprimento (ver `EntregarLinhaSql`). Truncar daria ao jogo um
// caminho ou um titulo errados, e o defeito so apareceria no ecra.
constexpr std::uint32_t kZonaDosNomesSql = kObjSqlDb + 0x100u;
constexpr std::uint32_t kZonaDosValoresSql = kObjSqlDb + 0x400u;
constexpr std::uint32_t kZonaDosVectoresSql = kObjSqlDb + 0x700u;
constexpr std::uint32_t kPassoDoTextoDaColunaSql = 0x40u;
constexpr std::uint32_t kMaximoDeColunasSql = 12u;
constexpr std::uint32_t kMaximoDeTextoDaColunaSql = kPassoDoTextoDaColunaSql - 1u;
// A ZONA TEM DE CABER ANTES DA VTABLE DO PROPRIO OBJECTO. Se as duas se
// cruzarem, escrever uma linha de consulta estragava o slot que o jogo chama, e o
// defeito apareceria mais tarde como "o `Exec` deixou de existir".
static_assert(kZonaDosVectoresSql + 2u * (kMaximoDeColunasSql + 1u) * 4u <=
                  kEnderecoDaVtableSqlDb,
              "a zona da linha de consulta nao pode invadir a vtable do banco");

// --- OS DOIS CLSIDs DO Z-WHEEL QUE FALTAVAM (frente zclsid) -----------------
//
// O `tectoy` (274755, o Z-Wheel) e o unico titulo do corpus que pede estas duas
// classes, e ate aqui recebia `ECLASSNOTSUPPORT` nas tres, com o jogo a DIZER o
// nome delas:
//
//   `Unable to create instance of IConfig in Tectoy_SetSystemLanguage, error 3`
//   `Unable to create instance of IDOWNLOAD in Tectoy_FixupTime`
//
// MEDIDO no proprio modulo (`ZB2_TRACE=1`, `ZB2_QUADROS=300 ZB2_EVT_START=1`):
// `AEECLSID_CONFIG` 2x (ppo na PILHA, 0x8007ff98 e 0x8007ffb0 -- os dois sitios de
// chamada da mesma funcao) e `AEECLSID_DOWNLOAD` 1x (ppo=0x802035c4, no HEAP: o
// jogo GUARDA o objecto).
//
// OS VALORES DOS CLSIDs NAO VEM DA MEDICAO, vem do SDK:
// `platform/system/inc/AEEClassIDs.h:109` (`AEECLSID_CONFIG = AEECLSID_CORE+39` =
// 0x01001027) e `:49` (`AEECLSID_DOWNLOAD = AEECLSID_PRIV = QVERSION` =
// 0x01000000). O `tools/clsids.inc` desta arvore concorda com os dois.
//
// O QUE O JOGO FAZ COM CADA OBJECTO -- lido no DESMONTE do `.mod` (base ZERO, os
// literais sao offsets do ficheiro), e nao adivinhado:
//
//   `Tectoy_SetSystemLanguage` (0x711c8), o sitio do `CreateInstance`:
//     711d8  ldr r1,[r0]        ; a vtable do IShell
//     711e0  ldr r3,[r1,#8]     ; slot 2 = CreateInstance
//     711e4  ldr r1,[pc,#0x114] ; 0x01001027  <- o literal, em 0x71300
//     711ec  bx  r3
//     711f0  movs r4,r0 ; beq 0x71240        ; 0 = criou -> caminho bom
//     71240  ldr r0,[sp]        ; o objecto IConfig
//     71248  ldr r1,[r0]        ; a vtable DELE
//     71250  ldr ip,[r1,#0xc]   ; slot 3
//     71254  mov r1,#0x3f       ; nItem = 63  (o idioma do sistema)
//     71258  bx  ip             ; SetItem(po, 0x3f, sp+8, 4)
//     712e0  bl  0x2f2a4        ; e no fim SOLTA o objecto (slot 1)
//
//   `Tectoy_FixupTime` (0x69b20), o sitio do `CreateInstance` -- e ELE que corre
//   nesta corrida (o `lr` do traco diz 0x69b64, o retorno do `bx`):
//     69b4c  ldr r3,[r1,#8]     ; IShell slot 2 = CreateInstance
//     69b54  add r2,r2,#0x17c   ; ppobj = pMe+0x357c -- o jogo GUARDA-o
//     69b58  mov r1,#0x1000000  ; 0x01000000 -- o literal e este `mov`, e nao uma
//                               ; palavra da pool: nao ha `ldr` dele no modulo
//     69b60  bx  r3             ; CreateInstance(po, DOWNLOAD, &m_pDownload)
//     69b64  cmp r0,#0 ; beq 0x69ba4         ; 0 = criou -> caminho bom
//     (o caminho de falha imprime "Unable to create instance of IDOWNLOAD in
//      Tectoy_FixupTime" -- 0x69c50 -- e foi o unico destes que o `tectoy` corria)
//     69ba4  ... IShell slot 45 = GetClassItemID(po, o clsid do titulo) -> o id
//     69bdc  ldr r0,[r4,#0x57c] ; e depois, com esse id:
//     69bf0  ldr ip,[r1,#0x54]  ; SLOT 21 (0x54 = 21*4)
//     69bf4  mov r1,r6          ; o id do item
//     69bec  add r2,pc,r2       ; um PONTEIRO DE FUNCAO do modulo (0x735f4)
//     69bfc  bx ip              ; slot21(po, id, callback, contexto)
//     69c1c  b 0x69b44          ; e o RETORNO do slot 21 nao e lido
//
//   A MESMA CLASSE, NUM SEGUNDO SITIO (que NAO correu no corpus de 300 quadros,
//   e por isso esta aqui como leitura estatica e nao como medida): o
//   `Gamelib_CheckForFailedDownload` de `GameLib_Form.c` (0x27bf8), cujas cadeias
//   o nomeiam -- "Gamelib_CheckForFailedDownload: dlitem = %d" (0x27f08) e
//   "fixing failed download %s, adding classID 0x%X" (0x27f4c):
//     27c5c  mov r1,#0x1000000  ; CreateInstance(DOWNLOAD, &obj)
//     27cb4  ldr r0,[sp]        ; o objecto IDownload
//     27cc0  ldr r2,[r1,#0xc]   ; slot 3
//     27cc4  mov r1,#0
//     27cc8  bx  r2             ; slot3(po, 0)
//     27ccc  cmp r0,#0 ; beq 0x27de4         ; 0 = "NAO ha downloads falhados"
//     27cd4  mov r5,r0 ; 27dd4: ldr r6,[r5]  ; senao: r0 e um PONTEIRO para uma
//     27dd8  cmp r6,#0 ; bne 0x27ce0         ; lista terminada em NULO de ids, e
//     27dd0  add r5,r5,#4                    ; por cada id ele chama o SLOT 4
//     27d34  ldr r2,[r1,#0x10] ; 27d38 mov r1,r6 ; 27d3c bx r2   ; slot4(po,id)
//     27d68  ldr r0,[r0,#0x20]  ; a cadeia que o `slot 4` devolveu, em +0x20
//
// OS SLOTS 2 DO IConfig E A ORDEM DO IConfig VEM DO `zeebx` NOVO
// (`src/aee_slots.rs:957`, `CONFIG = ["AddRef","Release","GetItem","SetItem"]`),
// que os leu do SDK e do proprio modulo; o `SetItem` no slot 3 esta confirmado
// pelo desmonte acima. `AEEIConfig.h`/`AEEIDownload.h` NAO EXISTEM na extracao do
// SDK que esta na maquina (conferido: nao ha `AEEIConfig.h` nem `AEEIDownload.h`
// em `sdk-extract/`, e o `AEEClassIDs.h` do 4.0.2 traz so as constantes) --
// **a contradicao com o enunciado desta frente, que os deu como fonte**.
// O que existe do `IDownload` sao as macros USADAS pela implementacao de
// referencia (`OATDownload.c`: `IDOWNLOAD_Release/OnStatus/GetItemInfo/Delete/
// Restore/Acquire/GetModInfo`), que dao os NOMES e nao a ordem dos slots.
constexpr std::uint32_t kIidConfig = 0x01001027u;
constexpr std::uint32_t kIidDownload = 0x01000000u;

// ONDE OS DOIS OBJECTOS VIVEM. Uma pagina por objecto, como o IHID (0x81010000 +
// 0x81011000) e a fabrica de sinais (0x81030000 + 0x81031000): a pagina da vtable
// e a do objecto, e nenhuma delas toca a faixa de SAIDA (0xF0000000), que NAO e
// nossa -- a frente `zwheel` pagou um titulo alheio por ter posto uma vtable la
// (o `reksio` le 0xF0009934).
constexpr std::uint32_t kVtableZclsidConfig = 0x81090000u;
constexpr std::uint32_t kObjConfig = 0x81091000u;
constexpr std::uint32_t kVtableZclsidDownload = 0x810A0000u;
constexpr std::uint32_t kObjDownload = 0x810A1000u;

// OS INDICES DE SAIDA, um por slot, como no SQL (9800) e nos widgets (60000+):
// 9900 e 10000 sao os primeiros blocos livres acima do fim do banco (9800 + 64 =
// 9864). O `tools/bateria.cpp` NAO precisa de saber disto: quem escreve estas
// vtables e o motor, no `InstalarZclsid`, e nao a ferramenta.
constexpr std::uint32_t kVtableConfig = 9900;
constexpr std::uint32_t kVtableDownload = 10000;
constexpr std::uint32_t kSlotConfigGetItem = kVtableConfig + 2;
constexpr std::uint32_t kSlotConfigSetItem = kVtableConfig + 3;
constexpr std::uint32_t kSlotDownloadFalhados = kVtableDownload + 3;
constexpr std::uint32_t kSlotDownloadItemInfo = kVtableDownload + 4;
// O SLOT 21: medido no `tectoy` (ver `AtenderDownload` no `.cpp`).
constexpr std::uint32_t kSlotDownloadInfoComCallback = kVtableDownload + 21;

// O TECTO DE UM ITEM DE CONFIGURACAO, e o unico valor que o jogo escreveu: 4
// bytes (`SetItem(0x3f, ptr, 4)`), medidos no desmonte. O tecto deixa passar o
// que o SDK declara para um item de configuracao (bytes, cores, um `ConfigTime`)
// e RECUSA o resto com o nome -- um item maior que isto nao tem contrato medido, e
// aceita-lo seria o stub silencioso com outra cara.
constexpr std::uint32_t kMaximoDoItemDeConfig = 256u;

// Um temporizador pedido pelo guest. UM so, porque e o que os titulos pedem: o
// laco de quadro, re-armado pelo proprio callback.
//
// O PAR E `(pfn, pUser)`, E NAO UM `AEECallback*`. Os argumentos veem SEPARADOS
// do guest, como o cabecalho manda:
//
//     int SetTimer(IShell *po, int32 dwMsecs, void (*pfn)(void *), void *pUser);
//     platform/system/inc/AEEIShell.h:299, em `INHERIT_IShell`
//
// MEDIDO no `asq` (`mod/280214/asq.mod`), no `EVT_APP_START`, em `0x8cf34`:
//
//     8cf34  ldr  r2, [r5, #12]     ; r2 = applet->m_pIShell
//     8cf38  mov  r3, r5            ; r3 = pMe        -- o CONTEXTO
//     8cf3c  mov  r0, r2            ; r0 = po
//     8cf44  mov  r1, #100          ; r1 = 100 ms     -- a DURACAO
//     8cf48  ldr  r2, [pc, #572]    ; r2 = *0x8d18c = 0x8c3e8  -- a FUNCAO
//     8cf4c  ldr  ip, [ip, #44]     ; slot 11 = SetTimer
//     8cf54  bx   ip
//
// Este codigo lia o r1 como ponteiro de `AEECallback` e o r2 como milissegundos
// -- os dois campos TROCADOS. O `PrepararCallbackDoTemporizador` lia depois
// `[100]` como par `(funcao, contexto)`, caia fora do modulo, e RECUSAVA. O
// sintoma era "nenhum titulo arma o laco de quadro" -- e sem laco de quadro
// nenhum titulo chega ao codigo que desenha.
struct Temporizador {
  bool ativo = false;
  std::uint32_t pfn = 0;    // void (*pfn)(void *pUser)
  std::uint32_t puser = 0;  // pUser
  std::int64_t vence_em_ms = 0;
};

struct ResultadoFase {
  std::uint64_t passos = 0;
  // PORQUE parou. `retornou` e uma fase concluida; `orcamento_de_tempo` e um
  // titulo que ainda estava a ANDAR, e isso muda o que se conclui dele.
  std::string motivo;
};

class Despacho {
 public:
  Despacho(Memoria& mem, Traco& traco, Alocador& alocador, Vfs& vfs);

  // Escreve o endereco de cada slot do `AEEHelperFuncs` que tem implementacao.
  //
  // UMA TABELA SO, de onde saem as escritas E a lista de saltos do preenchimento.
  // Havia duas -- as escritas e uma lista de "quem ja tem implementacao" que o
  // laco consultava -- e eu esquecime de acrescentar a segunda UMA vez, e o laco
  // apagou a implementacao. **Duas listas que tem de concordar sao zero listas.**
  void InstalarAjudantes(const Saidas& saidas, Endereco tabela);

  // Corre o guest ate a sentinela, o limite de passos, ou o orcamento de tempo.
  ResultadoFase Correr(ICpu& cpu, std::uint64_t limite, std::uint32_t pp_saida);

  // --- o estado que a ferramenta observa ---------------------------------
  Tela& TelaRef() { return tela_; }
  const Tela& TelaRef() const { return tela_; }

  // Cheats declarados (`ZB2_CHEATS`): a bateria carrega o ficheiro e fixa o
  // titulo; as fronteiras de fase chamam `NaFase` e o laco chama `NoPasso`.
  Cheats& RefCheats() { return cheats_; }


  // `Faltas()`, `VfsRef()`, `ArquivosRef()`, `Blits()` e `Backlights()` SAIRAM.
  //
  // Eram acessores que ninguem chamava -- e o `Faltas()` era pior do que inutil:
  // devolvia `faltas_`, um mapa que **nunca e escrito em lado nenhum**. Quem o
  // chamasse lia um mapa VAZIO e concluia "nao falta nada".
  //
  // **E a terceira vez que esta classe de defeito aparece nesta arvore:**
  // o `e.tamanho` esteve a zero e fazia o degrau `vtable` parecer falso; o
  // `Despacho::Faltas()` foi encontrado pelo sub-agente do GL, que o leu e imprimiu
  // "sem_nome" nas 41 chamadas; e o `e.create` media lixo no slot de saida --
  // **o `applet 41` estava inflado e o honesto era 22.**
  //
  // A licao e sempre a mesma: **um campo que nao mede e pior do que um campo ausente,
  // porque um ausente nao se le.** Uma API que devolve um valor plausivel e errado
  // faz o instrumento mentir sem avisar.
  std::uint32_t Textos() const { return textos_; }

  // E VOLTARAM, porque a quarta vez foi a mesma classe de defeito AO CONTRARIO:
  // o motor contava (`++blits_` em `despacho.cpp`, `++updates_`, `++dibs_`) e a
  // FERRAMENTA lia variaveis globais (`g_blits`, `g_updates`, `g_dibs` em
  // `tools/bateria.cpp`) que **nunca eram incrementadas em lado nenhum**. As
  // colunas `textos` e `blits` do JSON escreviam ZERO em todas as corridas desde
  // que existem, e ninguem reparou porque zero e um numero plausivel para um
  // emulador que ainda nao desenha.
  //
  // Tirar os acessores nao resolveu: mudou o defeito de "ninguem le" para
  // "le-se a coisa errada". Agora ha UMA fonte, e e esta.
  std::uint32_t Blits() const { return blits_; }

  // O RELOGIO VIRTUAL, para quem precisa de afirmar que ele NAO andou.
  // (`tests/sendevent_test.cpp`: a entrega de um evento tem de ser instantanea
  // para o guest.)
  std::uint32_t AgoraMs() const { return static_cast<std::uint32_t>(agora_ms_); }
  std::uint32_t Updates() const { return updates_; }
  std::uint32_t Dibs() const { return dibs_; }



  // --- A ENTRADA (etapa 8) -------------------------------------------------
  //
  // O `IHID`, o `IHIDDevice` e os sinais do BREW vivem num modulo proprio
  // (`core/brew/ihiddevice.{h,cpp}` + `core/brew/ihid_entrada.{h,cpp}`); aqui
  // esta so a CABLAGEM deles na faixa de saida.
  //
  // `base` e o primeiro indice desta faixa. O modulo NAO impoe um numero: quem
  // chama escolhe-o, e a `Saidas` tem de ter espaco para
  // `base + Sinais::kSlotsNecessarios + Ihid::kSlotsNecessarios`. Se nao tiver,
  // os dois modulos RECUSAM e dizem qual o indice que falta.
  bool InstalarEntrada(const Saidas& saidas, std::uint32_t base);

  // Atende um pedido desta faixa. `false` = o indice nao e da entrada.
  bool AtenderEntrada(ICpu& cpu, std::uint32_t indice);

  // --- O WIDGET (IRootForm + IForm + IHandler + IWidget) -------------------
  //
  // O `IShell::CreateInstance` do corpus pede `AEECLSID_CRootForm`
  // (`0x01028e51`) e recebe o objecto GENERICO de indice `kIndiceDoRootForm`; os
  // slots desse objecto ja apontam para a faixa de saida, e este modulo passa a
  // ATENDE-LOS em vez de os recusar. Ver `core/brew/widget.h` para a medicao
  // (o `tectoy`, slot 3 com `EVT_WDG_GETPROPERTY`/`WID_FORM`/`WID_SOFTKEYS`).
  //
  // CHAMADO AUTOMATICAMENTE NO FIM DO `InstalarAjudantes`, e idempotente: nao
  // faz mal nenhum que quem dirige o titulo o chame tambem, e assim esta frente
  // nao obriga a mudar `tools/bateria.cpp`.
  bool InstalarWidgets(const Saidas& saidas);

  // --- O SQL (ISQLMgr + ISQLDatabase) --------------------------------------
  //
  // O `tectoy` (274755, o Z-Wheel) e o UNICO titulo do corpus que toca o SQL:
  // quatro `ISQLMgr::Open` -- `tt_prefs.db` 2x, `asset_cache` e `tt_game_info`.
  // Ver `InstalarSql` no `.cpp` para a medicao.
  bool InstalarSql(const Saidas& saidas);
  bool AtenderSql(ICpu& cpu, std::uint32_t indice, std::uint32_t pp_saida);

  // --- OS DOIS CLSIDs DO Z-WHEEL (frente zclsid) ----------------------------
  //
  // Os dois objectos que o `AEECLSID_CONFIG` e o `AEECLSID_DOWNLOAD` recebem, e
  // os slots da interface de CADA um. Constroi-se um objecto por CLSID, com a
  // vtable da SUA interface -- a licao da frente `mediautil` (`imedia.cpp`,
  // `Criar`): servir duas classes com o mesmo objecto e o defeito que deu um
  // `IMediaUtil` a quem pediu um `ECLASSID_MULTIMEDIA`.
  //
  // O que NAO se sabe recusa COM O NOME, e nao devolve sucesso: e a mesma regra
  // do `AtenderSql` para os slots que a medicao nao mostrou.
  bool InstalarZclsid(const Saidas& saidas);
  bool AtenderZclsid(ICpu& cpu, std::uint32_t indice, std::uint32_t pp_saida);
  Widgets& WidgetsRef() { return widgets_; }
  const Widgets& WidgetsRef() const { return widgets_; }
  // `true` = o indice era do widget e ja foi atendido (com sucesso OU com recusa
  // registada). `false` = nao e desta faixa.
  bool AtenderWidgets(ICpu& cpu, std::uint32_t indice);

  // Aplica a entrada ate ao instante actual e marca os sinais registados.
  // `true` = ha um callback do titulo posto no PC (PC = funcao, R0 = contexto,
  // LR = sentinela); quem chama tem de o deixar correr.
  bool BombearEntrada(ICpu& cpu) { return ihid_.Bombear(cpu); }

  // A FAIXA DO MODULO DO TITULO (base e tamanho), para o modulo da entrada poder
  // recusar um callback que aponte para fora dela. A base e ZERO, e e medida.
  // O FIM DA FAIXA tambem vai para o despacho, e nao so para os modulos.
  //
  // MEDIDO, apontado pelo sub-agente `hid-entrada`: o `saiu_do_modulo` do laco usava
  // `pc >= kBase + 0x01000000`, 16 MB acima da base. Com a base a ZERO isso e
  // `0x01000000` -- e a bateria mostrava 12 titulos a saltar para la e a serem dados
  // como "fora do modulo", quando o limite real e o TAMANHO da imagem. **Um guest
  // que se perca entre o fim da imagem e os 16 MB continua a andar em silencio.**
  void DefinirFaixaDoModulo(std::uint32_t base, std::uint32_t tamanho) {
    base_do_modulo_ = base;
    faixa_base_ = base;
    faixa_fim_ = base + tamanho;
    sinais_.DefinirFaixaDoModulo(base, tamanho);
  }

  // --- o LACO DE QUADRO ----------------------------------------------------
  //
  // Em BREW o laco de quadro do jogo vive no `IShell::SetTimer`: o applet arma um
  // temporizador e o proprio callback re-arma o seguinte. O laco do despacho ja o
  // cumpre; isto existe para quem dirige o titulo de FORA poder correr quadros
  // depois do arranque -- que e o que a bateria (2 fases: carga e create) nao
  // faz, e sem o qual um menu nao chega a andar.
  bool TemporizadorArmado() const { return timer_.ativo; }
  std::uint32_t CallbackDoTemporizador() const { return timer_.pfn; }
  std::uint32_t ContextoDoTemporizador() const { return timer_.puser; }
  bool PrepararCallbackDoTemporizador(ICpu& cpu);

  bool EntradaPronta() const { return entrada_pronta_; }
  EntradaDoZeebo& Entrada() { return entrada_; }
  Sinais& SinaisRef() { return sinais_; }
  Ihid& IhidRef() { return ihid_; }
  std::uint32_t BaseDaEntrada() const { return base_da_entrada_; }
  std::uint32_t BaseDosSinais() const { return base_da_entrada_; }
  std::uint32_t BaseDoIhid() const { return ihid_.BaseDasSaidas(); }

  // O que o despacho precisa de saber do titulo actual. Posto UMA vez por titulo,
  // no inicio -- e nao um estado que passa de um titulo para o outro.
  void SituarTitulo(const std::string& dir, const std::string& pasta,
                    std::uint32_t clsid = 0) {
    dir_ = dir;
    pasta_ = pasta;
    clsid_titulo_ = clsid;
    tem_clsid_ = (clsid != 0);
    // A REGRA QUE LIGOU OS PACOTES `.pkg` AO SISTEMA DE FICHEIROS FICA DITA.
    //
    // A `Vfs` foi registada antes deste objecto existir (a ferramenta chama
    // `Registar` antes de construir o `Despacho`), pelo que a declaracao e
    // emitida agora, uma so vez por titulo, e nao no momento do registo: sem
    // isto, `karnovr/boot.rom` resolver para uma entrada de `boot.pkg` seria uma
    // ligacao que ninguem ve -- e uma ligacao que ninguem ve perde-se em
    // silencio, que e o defeito que ja custou a cablagem do `SetTimer`.
    vfs_.DeclararNoTraco(&traco_);
    // O PARK DA ESPERA (frente park): o estado nao passa de titulo para titulo.
    estacionada_ = false;
    // Frente ropi2: uma vez por titulo, e nao uma vez por processo.
    entrada_ja_correu_ = false;
  }
  std::uint32_t ClsidDoTitulo() const { return clsid_titulo_; }
  bool TemClsidDoTitulo() const { return tem_clsid_; }

  // --- A EXTENSAO QUE O TITULO TROUXE (o `IMicro3D` do `a3d`) ---------------
  //
  // DOIS titulos do corpus -- o `a3d` (Action Hero 3D) e o Kingdom Hearts --
  // pedem ao `IShell::CreateInstance` uma classe que NAO esta em cabecalho nenhum
  // do SDK e nao existe em particao nenhuma da consola: `0x010292c3` e
  // `0x0102bbfc`. Nao sao classes da consola, sao do PROPRIO PACOTE do jogo: um
  // modulo de extensao em ARM que viaja ao lado do titulo.
  //
  // MEDIDO (nos 65 `.mif` do corpus, com o leitor desta arvore): a extensao
  // declara a classe num registo de 8 bytes, `<u32 ClassID> <u32 zero>` -- a
  // MESMA forma que o manifesto do jogo usa para declarar a dependencia. So DOIS
  // `.mif` sao extensoes: o `12875.mif` (`0x010292c3`, o servico de 3D da HI que
  // o `a3d` pede) e o `12876.mif` (`0x0102bbfc`, o motor Superscape do Kingdom
  // Hearts). O que os separa de um titulo e nao terem seccao de applet -- e por
  // isso que a busca do fornecedor (`ProcurarFornecedorDaClasse`) e pela PASTA do
  // modulo, que e a pareacao do BREW.
  //
  // `OferecerExtensao` MAPEIA o `.mod` na carga (o `CarregarMod` num endereco
  // nosso); a hora do pedido fica com os dois passos do console, que sao duas
  // chamadas ANINHADAS ao guest: o `AEEMod_Load` uma vez, e depois o
  // `IModule::CreateInstance` (slot 2) sempre que o jogo pede a classe.
  bool OferecerExtensao(std::uint32_t classe, std::uint32_t base, std::uint32_t tabela,
                        const std::vector<std::uint8_t>& imagem);
  std::uint32_t ExtensoesOferecidas() const {
    return static_cast<std::uint32_t>(extensoes_.size());
  }
  // Serve a classe: devolve o objecto, ou ZERO e regista a falta COM O MOTIVO.
  // O `shell` e o `r0` da chamada do jogo -- o mesmo IShell que o applet recebeu.
  std::uint32_t CriarInstanciaDaExtensao(ICpu& cpu, std::uint32_t shell, std::uint32_t classe);
  // --- O GL (etapa 6) ------------------------------------------------------
  //
  // O `Igl` e o `Egl` vivem AQUI, e nao na ferramenta: a cablagem e do motor.
  // `InstalarGl` escreve as duas vtables na faixa de saida e CONFIRMA com leitura
  // de volta (cada modulo ja faz a sua); e o `Correr` que entrega os pedidos.
  std::uint32_t InstalarGl(const Saidas& saidas);
  Igl& IglRef() { return igl_; }
  const Igl& IglRef() const { return igl_; }
  Egl& EglRef() { return egl_; }
  const Egl& EglRef() const { return egl_; }

  // --- O GL (etapa 6) ------------------------------------------------------
  //
  // O `Igl` e o `Egl` vivem AQUI, e nao na ferramenta: a cablagem e do motor.

  // A VTABLE DO IBitmap: GUARDA O ENDERECO **E CONSTROI A TABELA**.
  //
  // As duas coisas juntas de proposito. Enquanto foram duas, a ferramenta
  // guardava o endereco e ninguem construia a tabela: os slots 0 e 1 (`AddRef` e
  // `Release` da IBase) ficavam a ZERO e o `IBITMAP_Release` do guest fazia
  // `blx 0`. MEDIDO no `abd` (279369, 0x1469c) e no `torkandkral` (280463,
  // 0x135d4) -- ver `tests/entrada_despacho_test.cpp`, TEST(VtableDoBitmap, ...).
  void DefinirVtableBitmap(const Saidas& s) {
    vtable_bitmap_ = s.Endereco(kVtableBitmap);
    ConstruirVtableDoBitmap(mem_, s);
  }
  void DefinirVtableFicheiro(std::uint32_t v) { vtable_ficheiro_ = v; }
  void DefinirApplet(std::uint32_t v) { applet_ = v; }

  // --- ISHELL_SendEvent (IShell slot 21) -----------------------------------
  //
  // A VTABLE TEM SEIS ARGUMENTOS (MEDIDO, `AEEIShell.h:309`):
  //
  //   boolean SendEvent(IShell *po, uint16 wFlags, AEECLSID clsApp,
  //                     AEEEvent evt, uint16 wParam, uint32 dwParam)
  //     r0=po  r1=wFlags  r2=clsApp  r3=evt  [sp+0]=wParam  [sp+4]=dwParam
  //
  // O `ISHELL_SendEvent` de CINCO argumentos e so a macro que poe `wFlags=0`
  // (`AEEShell.h:278`), e o `ISHELL_PostEvent` e O MESMO SLOT com
  // `EVTFLG_ASYNC|EVTFLG_UNIQUE` (`AEEShell.h:279`).
  //
  // A ENTREGA E SINCRONA porque o chamador le a resposta na instrucao SEGUINTE
  // ao retorno -- MEDIDO no `tectoy.mod` (274755), `0x6a3a0: ldrne r0,[sp,#8]`,
  // e declarado no SDK (`AEEIShell.h:2851`, "sends events synchronously").
  // Uma fila drenada no quadro seguinte entrega DEPOIS de o chamador ja ter
  // lido zero -- ou seja, nao entrega nada.
  //
  // `pp_saida` e o `ppObj` do `IModule::CreateInstance`: o applet manda um
  // evento a si proprio DE DENTRO do create, antes de `applet_` existir
  // (`tools/bateria.cpp:785` so o define depois). E o que as duas fontes
  // fazem: zeebulator `core/brew/ishell.cpp:200-207`, zeebx
  // `src/machine/signal.rs:232-241`.
  //
  // Devolve `true` quando o `HandleEvent` do applet CORREU e VOLTOU; nesse caso
  // `*devolveu` traz o `boolean` do applet. `*passos_gastos` traz os passos que
  // a entrega consumiu -- saem do MESMO orcamento da fase, e nao de um
  // orcamento escondido.
  bool EntregarEventoAoApplet(ICpu& cpu, std::uint32_t clsapp, std::uint32_t evt,
                              std::uint16_t wp, std::uint32_t dwp, std::uint32_t pp_saida,
                              std::uint32_t* devolveu, std::uint64_t* passos_gastos);

  // O FIM DA FAIXA DO MODULO, e nao `kBase + 16 MB`.

 private:
  // --- O PARK DA ESPERA (frente park) --------------------------------------
  //
  // O zeebx mediu-o (ramo fix-fp-threading, commit 4038f15): uma thread que so
  // espera -- ler o relogio, perguntar ao controle com a fila vazia -- prende o
  // laco de quadros, porque a thread cooperativa nunca cede a vez. O atalho
  // tem duas metades:
  //
  //  1. `NotarEspera`: no inicio de cada saida. Leitura de relogio CRESCE a
  //     contagem; ceder (Suspend/GetResumeCBK/Resume) e perguntar sem mudar
  //     nada (controle com a fila vazia, memset pequeno) nao desfazem a
  //     contagem; QUALQUER outra chamada e trabalho, e zera.
  //  2. Na fronteira entre duas chamadas de API, com a contagem no limiar, a
  //     thread actual e ESTACIONADA como se tivesse chamado Suspend: o
  //     contexto fica em `estacionada_contexto_`, o do hospedeiro volta, e o
  //     laco de eventos (temporizadores, entrada, midia) ganha a vez. A
  //     passada seguinte devolve a thread no mesmo ponto.
  //
  // PORQUE O CONTADOR E DE TODO O MODULO E NAO SO DA THREAD: o hospedeiro que
  // trabalha entre retomadas zera tudo, e a thread so acumula quando e ela a
  // unica a correr -- que e exactamente o caso que prende o laco.
  void NotarEspera(ICpu& cpu, std::uint32_t indice);
  bool EhCedencia(std::uint32_t indice) const;
  bool EhPerguntaInocua(ICpu& cpu, std::uint32_t indice);
  std::uint32_t espera_polls_ = 0;
  std::uint32_t espera_ms_ = 0;
  bool estacionada_ = false;
  std::uint32_t estacionada_contexto_[16] = {};
  // O LIMIAR, do zeebx com o tempo em ms: `VSYNC_PERIOD_US` (16,7 ms) la;
  // aqui o relogio e em ms e cada leitura conta 1 ms -- 16 leituras.
  static constexpr std::uint32_t kParkMs = 16;
  // O `memset` pequeno (o Rolimaz limpa um AEEHIDButtonInfo de 16 bytes antes
  // de cada GetNextButtonEvent) e parte da pergunta, nao trabalho.
  static constexpr std::uint32_t kMemsetDaEspera = 16;

 private:
  // O ANEL DAS ULTIMAS INSTRUCOES (ver `Correr`): guarda o PC e a palavra de cada uma
  // das ultimas 16, para o motivo da saida dizer COMO se chegou la.
  std::uint32_t anel_pc_[16] = {0};
  std::uint32_t anel_instr_[16] = {0};
  std::uint32_t ultimas_ = 0;

  // O CABECALHO PUBLICO DO IDIB (`AEEIDIB.h:42-55`), num so sitio.
  //
  // Havia tres copias a escrever offsets a mao (`CreateDIBitmap`,
  // `GetDestination`, `GetDeviceBitmap`) e as tres estavam erradas do mesmo
  // modo. Tres copias de um layout medido sao tres chances de ele divergir.
  void EscreverCabecalhoDeIdib(std::uint32_t obj, std::uint32_t pbmp, std::uint32_t largura,
                               std::uint32_t altura);

  // --- IShell::DetectType (slot 43) e IShell::LoadResObject (slot 19) --------
  //
  // Os dois servidos AQUI, e nao em `classes.*`: nenhum deles e uma classe. O
  // `DetectType` e um metodo do IShell cujo contrato foi MEDIDO (ver o
  // comentario do ramo, em `despacho.cpp`), e o `LoadResObject` e o irmao do
  // `LoadResDataEx` -- le o mesmo `.bar`, com a diferenca de devolver um
  // OBJECT desenhável em vez do bloco cru.
  //
  // `int DetectType(IShell*, const void *cpBuf, uint32 *pdwSize,
  //                 const char *cpszName, const char **pcpszMIME)`
  //   -> r0=po  r1=cpBuf  r2=pdwSize  r3=cpszName  [sp+0]=pcpszMIME
  //      (`AEEIShell.h:275`, `IShell_DetectType` `:568`)
  // `false` = o contrato nao pode ser cumprido (falta registada com o nome);
  // `true` = respondido, com sucesso ou com a recusa que o SDK preve.
  bool AtenderDetectType(ICpu& cpu);
  // Escreve a cadeia de mime numero `indice` na zona acima e devolve o endereco
  // dela na memoria do GUEST. Idempotente: escrever a mesma cadeia duas vezes
  // deixa-a igual.
  std::uint32_t EscreverMimeNoGuest(std::uint32_t indice);

  // `IBase *LoadResObject(IShell*, const char *pszResFile, uint16 nResID,
  //                       AEECLSID cls)`
  //   -> r0=po  r1=pszResFile  r2=nResID  r3=cls (`AEEIShell.h:251`)
  bool AtenderLoadResObject(ICpu& cpu);

  // `AEECLSID GetHandler(IShell*, AEECLSID clsBase, const char *pszIn)`
  //   -> r0=po  r1=clsBase  r2=pszIn  (`AEEIShell.h:800`, o slot 32)
  //
  // Devolve o AEECLSID do handler REGISTADO para aquele MIME, ou 0 quando nao
  // ha nenhum -- que e a resposta que a documentacao preve ("0 (zero), if
  // otherwise", `AEEIShell.h:6289`). O contrato medido e a tabela do registo
  // estao no comentario do `AtenderGetHandler`, em `despacho.cpp`.
  bool AtenderGetHandler(ICpu& cpu);

  // `uint32 GetClassItemID(IShell*, AEECLSID cls)` -- `AEEIShell.h:813`, o SLOT
  // 45 do IShell (`tools/brew_slots.inc:58`).
  //   -> r0=po  r1=cls
  // Devolve o id de ITEM do modulo dono daquela classe, ou 0 quando a classe nao
  // e deste modulo -- que e o que o SDK preve ("0 - Class not found or module is
  // static", ficha em `AEEShell.h:7255`). O contrato, o desmonte do `tectoy` que
  // o consome e a prova do numero estao no comentario do
  // `AtenderGetClassItemID`, em `despacho.cpp`.
  bool AtenderGetClassItemID(ICpu& cpu);
  // O primeiro IDIB LIVRE da banda dos bitmaps compativeis, ACIMA do ecra
  // (`kObjDibBase + 0x300`), que e a mesma escolha do servico da familia
  // (`core/brew/interface.cpp`, `ProcurarObjectoLivre`) e a razao que la esta
  // escrita: abaixo do ecra estao os DIBs do `CreateDIBitmap`. 0 = banda cheia.
  std::uint32_t IdibLivre() const;
  // Para os testes poderem ver a contagem que saiu do `+4` do objecto.
 public:
  std::uint32_t ReferenciasDoBitmap(std::uint32_t obj) const {
    const auto it = refs_do_dib_.find(obj);
    return it == refs_do_dib_.end() ? 0 : it->second;
  }

 private:
  // "Sei servir esta classe?" -- a lista partilhada pelo `QueryClass` e pelo
  // `CheckPrivLevel`.
  bool ClasseConhecida(std::uint32_t cls) const;
  // O IDIB do ECRA. Devolve o endereco do objecto.
  std::uint32_t EscreverCabecalhoDoBitmapDoEcra();
  // A TELA -> O BUFFER DO GUEST. Depois disto, os dois lados sao iguais, e por
  // isso qualquer diferenca futura e do guest.
  void ExporEcraAoGuest();

 public:
  // O BUFFER DO GUEST -> A TELA. Corre no `IDisplay::Update` (que e quando o
  // BREW mostra o que foi desenhado) e uma vez no fim da medicao, para o caso
  // de um titulo que escreve pixels e nunca chama `Update`. Devolve quantos
  // pixels vieram do guest.
  std::uint32_t AbsorverEcraDoGuest();
  bool EcraExpostoAoGuest() const { return ecra_exposto_; }
  std::uint32_t PixelsVindosDoGuest() const { return pixels_do_guest_; }

 private:
  // O ecra so ganha pagina no guest quando ALGUEM pede o bitmap do ecra. Um
  // titulo que nunca o peca nao paga os 614 400 bytes nem as copias.
  bool ecra_exposto_ = false;
  std::uint32_t pixels_do_guest_ = 0;

  // A faixa dos objectos de bitmap (a mesma que o `SetDestination` ja aceita).
  static bool EUmObjectoDeBitmap(std::uint32_t p) {
    return p >= zb2::brew::kObjDibBase && p < zb2::brew::kObjDibBase + 0x1000;
  }
  // O ERRO DA ULTIMA OPERACAO DE FICHEIRO QUE FALHOU (`IFILEMGR_GetLastError`).
  //
  // Devolvia-se sempre 0 -- "sem erro" logo a seguir a uma recusa. Um jogo que
  // decida pelo codigo de erro decidia ao contrario.
  std::int32_t ultimo_erro_do_fm_ = kAeeSuccess;

  // A contagem de referencias dos IDIB vive AQUI, e nao no `+4` do objecto.
  //
  // O `+4` de um IDIB e o `pPaletteMap` (`AEEIDIB.h:44`), um PONTEIRO publico, e
  // a IBase generica desta arvore guarda a contagem nesse offset
  // (`core/brew/interface.cpp:18`, e os ramos `idx == 3/4` do `Atender`). Um
  // `AddRef` no bitmap do ecra punha `1` -- e depois `2`, `3` -- num campo que o
  // `IDIB_FlushPalette` (`AEEIDIB.h:83-86`) desreferencia.
  std::map<std::uint32_t, std::uint32_t> refs_do_dib_;

  Memoria& mem_;
  Traco& traco_;
  Cheats cheats_;
  Alocador& al_;
  Vfs& vfs_;
  Arquivos arquivos_;
  // Recursos do `.bar`: conhece a VFS e o alocador do GUEST. O despacho fica só
  // com a ABI (registros/pilha); as três formas de LoadResDataEx têm testes em
  // `tests/recursos_test.cpp`.
  Recursos recursos_;
  // O TRECHO CORTADO PELO TECTO, pronto a retomar (ver o `Correr`, no `.cpp`).
  //
  // E MEMBRO, e nao um estatico de ficheiro: um estatico sobrevive de um titulo
  // para o outro (e de um TESTE para o outro) e o proximo `Correr` retomava um PC
  // que ja nao existe -- medido, com tres testes da frente `igfx` a falhar.
  struct TrechoPendente {
    bool valido = false;
    std::uint32_t pc = 0;
    std::uint32_t regs[16] = {};
    bool thumb = false;
  };
  TrechoPendente trecho_;

  Tela tela_;
  // O MISTURADOR e o IMedia. O `Media` tem referencias dentro, logo NAO e
  // atribuivel: nasce no `InstalarAjudantes`, quando a faixa de saida ja esta
  // configurada (ele guarda uma COPIA dela).
  audio::Misturador misturador_;
  std::unique_ptr<Media> media_;

  // A ENTRADA. `sinais_` antes de `ihid_`, porque o `Ihid` guarda a referencia
  // ao `Sinais` -- e a ordem de declaracao e a ordem de construcao.
  EntradaDoZeebo entrada_;
  Sinais sinais_;
  Ihid ihid_;
  std::uint32_t base_da_entrada_ = 0;
  std::uint32_t base_do_modulo_ = 0;
  bool entrada_pronta_ = false;
  bool widgets_prontos_ = false;

  std::int64_t agora_ms_ = 0;
  Temporizador timer_;

  // O GL e o EGL. Depois do que eles nao usam e antes de nada que os use: a ordem
  // de declaracao e a ordem de construcao.
  Igl igl_;
  Egl egl_;

  // O GL e o EGL. Depois do que eles nao usam e antes de nada que os use: a ordem
  // de declaracao e a ordem de construcao.
  // O WIDGET. Depois de `tela_` e dos objectos do shell, porque e construido por
  // `InstalarAjudantes` e nao no construtor.
  Widgets widgets_;

  // A PROFUNDIDADE DA ENTREGA DE EVENTOS (guarda de reentrancia): o
  // `HandleEvent` do applet pode mandar outro evento, e isso e uma cadeia que
  // so para com um tecto. `4` e o numero do zeebx (`src/machine/mod.rs:1541`,
  // `MAX_NESTING`); o zeebulator usa 1 (`core/brew/ishell.h:303`).
  //
  // ELA TAMBEM DESLIGA O LACO DE QUADRO enquanto a entrega corre: um
  // temporizador ou um sinal de entrada disparado de DENTRO do `HandleEvent`
  // seria reentrancia que o BREW nunca faz.
  int profundidade_de_evento_ = 0;
  static constexpr int kMaxProfundidadeDeEvento = 4;

  // UMA EXTENSAO JA MAPEADA. O `modulo` e o `IModule*` que o `AEEMod_Load`
  // devolveu; `carregou` separa "ainda nao se pediu" de "pediu-se e falhou", para
  // uma extensao partida nao ser recarregada a cada pedido.
  struct ExtensaoDoTitulo {
    std::uint32_t classe = 0;
    std::uint32_t base = 0;
    std::uint32_t tamanho = 0;
    std::uint32_t tabela = 0;
    std::uint32_t modulo = 0;
    bool carregou = false;
  };
  std::vector<ExtensaoDoTitulo> extensoes_;
  // Uma chamada ANINHADA ao guest: salva r0..r15 e o CPSR, corre ate a sentinela
  // e repoe tudo. E o mesmo idioma do `EntregarEventoAoApplet` e da thread
  // cooperativa -- `hospedeiro`/`kSentinela` existem nesta arvore por causa deles.
  // `fase`, quando nao e nulo, leva o resultado da corrida aninhada -- o motivo
  // e os passos. Uma chamada que NAO volta tem de dizer PORQUE nao voltou: sem
  // isso a recusa e um "nao voltou" que nao se pode perseguir (P2).
  std::uint32_t ChamarNoGuest(ICpu& cpu, std::uint32_t funcao, const std::uint32_t args[4],
                              bool* voltou, ResultadoFase* fase = nullptr);

  std::uint32_t textos_ = 0;
  std::uint32_t blits_ = 0;
  std::uint32_t backlights_ = 0;
  std::uint32_t updates_ = 0;
  std::uint32_t dibs_ = 0;
  std::uint32_t applet_ = 0;
  std::uint32_t destino_ = 0;
  std::uint32_t vtable_bitmap_ = 0;
  std::uint32_t vtable_ficheiro_ = 0;
  // O `ISQLDatabase` so tem UM objecto: medido, o Z-Wheel abre um banco de cada
  // vez (abre, usa, fecha, e so depois abre o seguinte). Com uma fila de um lugar
  // a segunda abertura SUBSTITUI a primeira -- e um titulo que tivesse dois
  // abertos em simultaneo perderia o primeiro; fica DECLARADO aqui em vez de
  // silencioso.
  bool sql_pronto_ = false;
  std::uint32_t sql_abertos_ = 0;
  // A PONTE SOBRE O SQLITE DE VERDADE. O ESTADO DO BANCO NAO ESTA AQUI, e esse e
  // o ponto: quem guarda as tabelas, os tipos e as transaccoes e o motor do
  // console (`third_party/sqlite3/`, dominio publico), e nao uma tabela de nomes
  // em C++ que crescia uma instrucao por vez.
  PonteSqlite sql_;
  // Entrega UMA linha ao callback do jogo, com a forma do `sqlite3_exec`
  // (`int cb(void *ctx, int ncols, char **valores, char **nomes)`).
  //
  // Devolve `false` quando o callback pediu PARAGEM (contrato do `sqlite3_exec`:
  // um retorno diferente de zero aborta a instrucao) ou quando a linha nao pode
  // ser entregue -- e nesse caso a falta ja ficou registada com o nome.
  bool EntregarLinhaSql(ICpu& cpu, const PonteSqlite::Linha& linha, std::uint32_t cb,
                        std::uint32_t ctx, std::uint32_t pp_saida);

  // --- OS DOIS CLSIDs DO Z-WHEEL (frente zclsid) ----------------------------
  //
  // `AtenderConfig` e `AtenderDownload` recebem o SLOT (0..63), e nao o indice da
  // faixa: o `AtenderZclsid` e que sabe de que objecto se trata, e um slot que
  // chegue aqui fora do que foi medido RECUSA com o numero dele.
  bool AtenderConfig(ICpu& cpu, std::uint32_t slot);
  bool AtenderDownload(ICpu& cpu, std::uint32_t slot);
  // Os dois objectos construidos e a vtable conferida por leitura de volta.
  bool zclsid_pronto_ = false;
  // O QUE O JOGO ESCREVEU NOS ITENS DE CONFIGURACAO, por numero de item.
  //
  // O VALOR VEM DO PROPRIO APP (`IConfig::SetItem`), e nao de um aparelho: este
  // emulador NAO tem configuracao de aparelho nenhuma, e inventar um idioma ou um
  // brilho seria a resposta que ninguem mediu. O que fica guardado e o que o jogo
  // gravou, e o `GetItem` devolve o que existe -- quem grava rele os seus valores,
  // e o que o jogo nunca escreveu RECUSA (em vez de devolver zero, que e um valor
  // legitimo e mentiria).
  std::map<std::uint32_t, std::vector<std::uint8_t>> itens_do_config_;
  std::uint32_t config_escritas_ = 0;
  std::uint32_t config_lidas_ = 0;

  // A FRENTE io2: o estado dos objectos IUnzipAStream e IMemAStream, por
  // endereco de objecto. Os objectos nascem no `InstalarAjudantes` (kObjUnzip /
  // kObjMemStream); o estado do unzip guarda a origem (o IAStream do SetStream)
  // e o RESULTADO da descompressao, feito de uma vez na primeira leitura --
  // o zeebx faz o mesmo (`UnzipState`, src/machine.rs:1241+).
  struct EstadoDoUnzip {
    std::uint32_t origem = 0;
    std::vector<std::uint8_t> saida;
    std::uint32_t pos = 0;
    bool expandido = false;
    bool origem_desconhecida = false;
  };
  struct EstadoDoMemStream {
    std::uint32_t base = 0;  // pBuff + dwOffset (o dado comeca aqui)
    std::uint32_t tamanho = 0;
    std::uint32_t pos = 0;
  };
  std::map<std::uint32_t, EstadoDoUnzip> unzips_;
  std::map<std::uint32_t, EstadoDoMemStream> memstreams_;

  // Le a origem ate ao fim e descomprime tudo de uma vez. `false` = recusa ja
  // registada no traco, com a razao; a leitura devolve EOF (0), para o jogo nao
  // ficar em laco de saidas.
  bool ExpandirUnzip(EstadoDoUnzip& e);

  std::string dir_;
  std::string pasta_;
  std::uint32_t clsid_titulo_ = 0;
  bool tem_clsid_ = false;
  std::uint32_t faixa_base_ = 0;
  std::uint32_t faixa_fim_ = 0;
  // PROPOSTA (frente ropi2): o modulo ja arrancou uma vez nesta corrida. Serve
  // para a SEGUNDA entrada em `base` deixar de ser invisivel -- ver o ramo
  // `ENTRADA_DO_MODULO_REPETIDA` em `despacho.cpp`.
  bool entrada_ja_correu_ = false;
};

}  // namespace zb2::brew

#endif
