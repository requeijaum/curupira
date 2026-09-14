# Etapa 5 -- midia e audio (o `IMedia`)

Branch `etapa5-media` (worktree `/tmp/wt-etapa5-media`), commit **bd551d0**.
Tudo o que esta aqui foi medido nesta arvore; os comandos estao escritos.

## O numero

| o que | antes | depois | comando |
|---|---|---|---|
| slots do `IMedia` implementados | 0 | **14** (3 da `INHERIT_IQI` + 11 do cabecalho) | `./build/zb2_sonda_media` (imprime `Instalar() OK`) |
| o motor entrega um `IMedia`? | **NAO**: `r0=0x00000003` (`AEE_ECLASSNOTSUPPORT`), `*ppobj=0`, falta `IShell::CreateInstance CLSID desconhecido` x1, saida **1** |  **SIM** na copia com o remendo: `r0=0`, `*ppobj=0x80090000`, saida **0** | `./build/zb2_sonda_media` |
| testes | 87 | **130** | `./build/zb2_tests` |
| amostras no misturador, 1 s de midia | 0 | recebidas **22050**, nao nulas **11025**, pico **2400**, 23 blocos | `./build/zb2_sonda_media` |

O misturador NAO toca som: ele CONTA. O criterio da etapa ("o misturador reporta
amostras nao nulas") mede-se, e o numero sai da sonda.

## O comando que fica VERMELHO

    ./build/zb2_sonda_media ; echo $?     # 1 enquanto o motor recusar o AEECLSID_MEDIA

Saida medida hoje (arvore sem o remendo):

    MOTOR: IShell::CreateInstance(0x01005500) -> r0=0x00000003 (AEE_SUCCESS=0), *ppobj=0x00000000
    MOTOR: passos=7 motivo=retornou
    MOTOR: falta "IShell::CreateInstance CLSID desconhecido" x1
    MOTOR: VERMELHO -- o motor NAO conhece o AEECLSID_MEDIA.

O pedido de midia esta escrito em codigo ARM: a sonda constroi o objecto do
`IShell` como a bateria o constroi, escreve um trampolim que faz `blx` pelo slot 2
(`CreateInstance`) da vtable, e corre o laco do motor de verdade.

**O verde foi provado numa COPIA da arvore** (`/tmp/wt-prova-despacho`, ~25 linhas
em `despacho.{h,cpp}`, diff guardado em `/tmp/remendo_despacho.diff`), porque
`core/brew/despacho.cpp` e partilhado e nao foi tocado nesta branch. Com o remendo,
o MESMO comando imprime:

    MOTOR: IShell::CreateInstance(0x01005500) -> r0=0x00000000 (AEE_SUCCESS=0), *ppobj=0x80090000
    MOTOR: VERDE -- o motor entregou um objecto de midia (0x80090000)
    CODIGO=0

## A ABI, e de onde vem

- **Slots**: `platform/media/inc/AEEIMedia.h`, macro `INHERIT_IMedia`. A cabeca e
  `INHERIT_IQI` (AddRef, Release, QueryInterface = slots 0,1,2), e nao
  `INHERIT_IBase`: por isso os 11 metodos proprios vao do 3 ao 13. Gerado por
  `tools/gerar_slots.py` (a interface `Media` foi acrescentada a lista), e o teste
  compara as constantes geradas com as usadas. Um erro de um slot ja custou uma
  ronda inteira neste trabalho.
- **`AEEMediaCmdNotify`**, medido no corpo do corpus e nao copiado: o tratador
  real do `cnk2` (pasta 274214) esta em `0x00101a80`, e as palavras de 32 bits do
  `cnk2.mod` a partir do deslocamento `0x1a80` sao

      0x101a88  e5913008   ldr r3, [r1, #8]    -> nCmd em +8
      0x101a8c  e3530004   cmp r3, #4          -> MM_CMD_PLAY (MM_CMD_BASE 1 + 3)
      0x101aa0  e5913010   ldr r3, [r1, #16]   -> nStatus em +16
      0x101aa4  e2433001   sub r3, r3, #1
      0x101aa8  e353000a   cmp r3, #10         -> cobre os status 1..11
      0x101aac  979ff103   ldrls pc, [pc, r3, lsl #2]

  e os dois destinos da tabela:

      status 2 (MM_STATUS_DONE)  -> 0x0010352c  tratador a serio (strb r2,[r6,#1] ...)
      status 3 (MM_STATUS_ABORT) -> 0x001034b8  0x1034b8 e2833028 add r3,r3,#0x28
                                                0x1034bc e2400001 sub r0,r0,#1
                                                0x1034c0 e5813000 str r3,[r1]
                                                0x1034c4 e12fff1e bx  lr

  Ou seja: neste titulo o DONE processa e o ABORT sai imediatamente. Dai o `Stop`
  avisar com **DONE (2)**.
- **Codigos**: `AEEStdErr.h` (`AEE_SUCCESS` 0, `AEE_ECLASSNOTSUPPORT` 3,
  `AEE_EBADSTATE` 13, `AEE_EBADPARM` 14, `AEE_EUNSUPPORTED` 20).
- **Armadilha**: `MM_STATUS_*` (resultado de um pedido) e `MMD_*` (tipo de dados) sao
  enums diferentes; `MMD_ISOURCE` = `AEEIID_ISource` = `0x01001012`
  (`platform/deprecated/inc/AEEISource.h:40`). E `0x01005505` NAO e MPEG4: e
  `AEECLSID_MEDIAMIDIOUTMSG` (MPEG4 = +7). Ha teste para os dois nomes.

## O que os testes contam

`tests/media_test.cpp` (33) corre **codigo ARM de verdade** numa bancada: um
trampolim que le a vtable do objecto e faz `blx` pelo slot, e um tratador que le os
MESMOS campos que o tratador do `cnk2` (`+8` o nCmd, `+16` o nStatus) e CONTA as
chamadas. Os dois `static_assert` do montador confrontam-no com `ldr r3,[r1,#8]` e
`add r3,r3,#1`, palavras lidas de um modulo real.

O que a bancada mede: um `Play` que chega ao fim da midia avisa **uma** vez com
`nCmd=4`/`nStatus=2`; um segundo `Play` no mesmo objecto avisa o pedido antigo com
`nStatus=3`; um `Stop` sem reproducao em curso **nao avisa**; `Stop` a meio avisa
uma vez e um `Stop` repetido nao avisa outra vez; 20 pedidos dao exactamente 20
avisos; uma midia em repeticao infinita nunca avisa.

`tests/audio_test.cpp` (10) mede o misturador: contagem, amostras nao nulas, pico
com volume, mudo, saturacao, soma de vozes no mesmo bloco, bloco vazio.

## Guardas provadas por VIOLACAO DELIBERADA

Cada uma foi quebrada de proposito, e o teste que a cobre ficou VERMELHO:

1. tabela declarada sem o slot 13 -> 2 testes vermelhos
2. sem a guarda da faixa de saida configurada -> 1
3. um SEGUNDO aviso no fim da midia -> 3
4. `Stop` a avisar sempre -> 2
5. volume fora da faixa aceito -> 1
6. `MMD_FILE_NAME` aceito em silencio -> 1
7. zeros contados como amostras nao nulas -> 1
8. bloco vazio contado como bloco -> 1
9. duas vozes encostadas em vez de somadas -> 2

Depois de repor: 130 testes verdes. O guiao e `/tmp/provar_guardas.py`.

## O remendo que falta (ficheiros PARTILHADOS, nao tocados)

`core/brew/despacho.h`: `<memory>`, `#include "core/audio/misturador.h"` e
`"core/brew/imedia.h"`, e dois membros:

    audio::Misturador misturador_;
    std::unique_ptr<Media> media_;   // o Media tem referencias: nao e atribuivel

`core/brew/despacho.cpp`: (1) no inicio de `InstalarAjudantes` (a faixa de saida
tem de estar configurada antes), `media_ = std::make_unique<Media>(mem_, traco_,
saidas, misturador_, &vfs_);` + `Instalar()`; (2) no caso
`idx == kBaseDoShell + 2`, antes da tabela de IIDs conhecidos:

    if (media_ && zb2::brew::ClasseDeMidia(iid)) {
      if (ppo != 0) mem_.Escrever32(ppo, 0);
      cpu.Set(kR0, static_cast<std::uint32_t>(media_->Criar(iid, ppo)));
      cpu.Set(kPC, lr);
      continue;
    }

e (3) o atendimento dos indices de saida, antes do ramo `idx >= kBaseDoShell`:
`} else if (media_ && media_->Atender(idx, cpu)) { ... }`.

**Falta ainda a ENTREGA DOS AVISOS no laco.** O aviso nasce em fila e quem o leva
ao guest e o laco: os 16 registradores e o CPSR tem de ser guardados e repostos (no
momento da entrega o guest tem registradores vivos -- a arvore antiga tem esta
primitiva medida, `HleRuntime::CallArmFunctionPreservingContext`), e o PC tem de
ser REPOSTO depois do callback. Sem repor o PC, o laco volta a ler a sentinela e
encerra a fase: foi o que aconteceu na primeira sonda, que usou `0xEEEE0000` como
sentinela e o laco reportou `saiu_do_modulo` (a sentinela do despacho e
`0xFFFFFFF0`).

## O que ficou por fazer, e o que contradiz o que se pensava

- **`MMD_FILE_NAME` (som por nome de ficheiro) e RECUSADO**: nao ha descodificador
  de audio nesta arvore. A recusa diz qual das duas causas e ("esta no VFS, mas
  nao ha descodificador" / "nao esta no VFS"), porque juntas no log sao
  indistinguiveis. `MMD_ISOURCE` e `Record` tambem recusam, com falta registada.
- **Parametros aceitos e GUARDADOS mas nao aplicados ao som**: `PAN`, `TEMPO`,
  `TUNE`, `TICK_TIME`, `RECT`, `POS`, `CHANNEL_SHARE`, `ENABLE`, `PLAY_TYPE`,
  `RATE`, `NOTES`, `AUDIOSYNC`, `AUDIO_DEVICE`, `AUDIO_PATH`. Cada aceitacao
  deixa um evento `IMEDIA_PARM_GUARDADO` no traco com o nome e o valor -- aceitar
  em silencio seria o stub que o P2 proibe. `MM_PARM_FRAME`, os `RESERVED_*` e os
  so de leitura (`CLSID`, `CAPS`, `SEEK_CAPS`) recusam em voz alta.
- **`GetTotalTime` usa a taxa DECLARADA de 22050 Hz**: nao ha descodificador, logo
  o tempo nao sai de um cabecalho lido. O numero esta no codigo, com este aviso.
- **`Pause`/`Resume`/`Seek` nao notificam** (decisao registada). O callback
  medido do `cnk2` so age em DONE/ABORT.
- **Contradicoes medidas**:
  1. "os titulos nao pedem IMedia porque a `CreateInstance` do `AEECLSID_MEDIA`
     nao existe" -- a `CreateInstance` de facto nao existe (medido acima), mas nos
     QUATRO titulos que tocam som na arvore antiga (`a3d`, `cnk2`, `ddragonz`,
     `allstarcards`) a bateria regista **ZERO faltas de qualquer tipo** e uma cor
     so: eles nao chegam a pedir nada. O portao esta antes do IMedia.
  2. O slot 2 do `IShell` NAO e `QueryInterface`: e **`CreateInstance`**
     (`AEEIShell.h`: `INHERIT_IBase` + `CreateInstance` no slot 2; o `IShell` nao
     tem `QueryInterface`). O comentario do despacho e a linguagem do ledger
     chamam-lhe QueryInterface -- a semantica do codigo (`r1` = ClsId, `r2` =
     `&ppobj`) e a de CreateInstance.
  3. O tratador do `cnk2` cobre os status **1..11** (`cmp r3, #10`), e nao 1..4.
- **Ficheiro gerado partilhado**: `tools/gerar_slots.py` ganhou a interface
  `Media` e `tools/brew_slots.inc` foi regenerado (bloco aditivo de 15 linhas; a
  guarda `verificar_slots.sh` continua verde). Se outro ramo mexer na mesma lista,
  o conflito e na lista `INTERFACES`.
- **`tools/bateria.cpp` nao precisa de nada**: a criacao do objecto de midia passa
  pelo caso do `CreateInstance`. Se quiserem o misturador no relatorio da bateria,
  e la que entra (nao foi pedido).
