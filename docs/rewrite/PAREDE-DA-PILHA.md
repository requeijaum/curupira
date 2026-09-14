# PAREDE DA PILHA: 21 dos 62 titulos saiam do modulo com o PC na PILHA

Branch `pilha`, worktree `/tmp/wt-pilha`, base `eb62459`. A arvore antiga
(`new_ez_ui`) fica intacta.

## O numero, antes -> depois, e o comando que o produziu

    ./build/zb2_bateria "$corpus" "$mods" /tmp/antes.json     # antes
    ./build/zb2_bateria "$corpus" "$mods" /tmp/depois.json    # depois
    ./build/zb2_comparar /tmp/antes.json /tmp/depois.json

| medicao | antes | depois |
|---|---|---|
| titulos cujo motivo contem `saiu_do_modulo_para_0x8007ff` (a PILHA) | **21** | **0** |
| `applet` (com o instrumento corrigido, ver abaixo) | **22** | **37** |
| `applet` (com o instrumento ANTIGO, o da referencia em `eb62459`) | 41 | 51 |
| regressoes no `zb2_comparar` | -- | **0** |

Contagem do numero da parede:

    python3 -c "import json;d=json.load(open('/tmp/depois.json'))['titulos'];print(len([x for x in d if '0x8007ff' in x['motivo']]))"

## A CAUSA, com a prova

### O que o anel diz (instrumento do commit `eb62459`, `core/brew/despacho.cpp`)

No `a3d`, a fase `create` corre **exactamente 10 instrucoes** e sai para a pilha:

    000054f4:e92d4010  push {r4, lr}
    000054f8:e590c00c  ldr  r12, [r0, #12]      <<< o ponteiro do metodo
    000054fc:e1a0e002  mov  lr, r2
    00005500:e1a02000  mov  r2, r0
    00005504:e35c0000  cmp  r12, #0
    00005508:e1a0000e  mov  r0, lr
    0000550c:08bd4010  popeq {r4, lr}           ; se r12 == 0, volta
    00005510:0afffb03  beq  0x4124              ; ... e chama o AEEClsCreateInstance
    00005514:18bd4010  popne {r4, lr}
    00005518:112fff1c  bxne r12                 <<< o salto

    create:saiu_do_modulo_para_0x8007ffcc lr=0xfffffff0 r0=0x01081970
                                          r1=0x80020000 r2=0x80200010 r3=0x00090010 sp=0x80080000

`r0` na saida e o `ClsId` e `r2` e o ponteiro do modulo, logo **entrou-se com
`r0 = modulo` e `r2 = ClsId`** -- a assinatura com que a bateria chama o
`IModule::CreateInstance`. O `ldr r12, [r0, #12]` le `modulo + 12`.

### Onde e que isso esta escrito

`research/docs/sdk-extract/.../platform/system/src/AEEModGen.c` da os dois factos:

1. `AEEMod_Load` (modulo dinamico) chama
   `AEEStaticMod_New(sizeof(AEEMod), pIShell, ph, ppMod, NULL, NULL)` -- os
   argumentos 5 e 6, `pfnMC` e `pfnMF`, vao a **ZERO**.
2. `AEEMod_CreateInstance` faz
   `if (pme->pfnModCrInst) nErr = pme->pfnModCrInst(ClsId, pIShell, pIModule, ppObj);
    else nErr = AEEClsCreateInstance(...)`.

E `platform/system/inc/AEEModGen.h` da o `struct AEEMod`: em ARM, `pvt` (+0),
`m_nRefs` (+4), `m_pIShell` (+8), **`pfnModCrInst` (+12)**, `pfnModFreeData` (+16).
O `+12` comparado com zero e o `pfnModCrInst` -- e para um modulo dinamico tem de
estar a **ZERO**, para o `bxne r12` nao acontecer e o caminho do `beq` (o
`AEEClsCreateInstance`) ser tomado.

### Quem escreveu o valor errado

Instrumento temporario `[DEBUG-pilha1]` no laco do `Correr` (espiao de escrita com
`ZB2_ESPIAO=0x8020001c`), uma linha de `fprintf` por cada mudanca:

    [DEBUG-pilha1] pc=0000563c alvo=8020001c 0x00000000 -> 0x8007ffcc
                   r0=00000001 r1=0000551c r2=00000000 r3=00090000 sp=8007ffcc lr=000055f0

`0x563C` e, no `a3d.mod`, o `stmib r4, {r0, r6, r8, sb}` do `AEEStaticMod_New`
compilado: escreve `m_nRefs` (+4), `m_pIShell` (+8), `pfnModCrInst` (+12) e
`pfnModFreeData` (+16). O valor de `+12` vem de **`r8`**, e `r8` vem de:

    000055b0:e92d47f0  push {r4, r5, r6, r7, r8, sb, sl, lr}
    000055c0:e1cd82d0  ldrd r8, sb, [sp, #0x20]

`0xE1CD82D0` -- conferido com `arm-none-eabi-objdump`, e nao lido de memoria:

    $ printf '\xd0\x82\xcd\xe1' > w.bin && arm-none-eabi-objdump -D -b binary -m arm w.bin
    0: e1cd82d0  ldrd r8, [sp, #32]

### A causa raiz: o grupo "extra load/store" nao existia no interpretador

Os bits 27-25 do `LDRD` (e do `STRD`, `LDRH`, `STRH`, `LDRSB`, `LDRSH`) sao **000**
-- os mesmos que o primeiro nivel de descodificacao do `ExecutarArm` usa para
"dados processados". Sem um ramo proprio, o `ldrd` caia no `DadosProcessados`, que
o lia como `BIC r8, sp, r0, LSR r2`: com o deslocamento a zero, o resultado e o
**proprio SP**. Medido: `0x8007FFCC`. E esse valor que ia parar ao `+12` do modulo,
e o `bxne r12` levava o PC para a pilha.

Reproduzido no teste `Cpu.ExtraLdrdNaoDevolveOEnderecoDaPilha` (15 testes em
`tests/cpu_test.cpp`).

## A CORRECCAO

### 1. `core/cpu/arm_interpreter.{h,cpp}` -- o grupo `TransferenciaExtra`

Ramo novo no `g == 0` do `ExecutarArm`, **antes** dos ramos genericos (a ordem e a
licao mais repetida desta arvore: do mais especifico para o mais generico), mais a
funcao `TransferenciaExtra`. A mascara que identifica o grupo:

    bits 27-25 = 000  e  bits 7-4 = `1 S H 1`  e  bits 6-5 != 00

Os bits 6-5 != 00 sao o que exclui o `MUL`/`UMULL`/`SWP` (bits 7-4 = 1001), que
vivem no mesmo espaco.

**A armadilha do grupo, e a razao de a verificacao ter sido feita no binutils:** na
palavra dupla o bit 20 (`L`) e **ZERO nas duas direccoes**. `ldrd r8, [sp, #32]` =
`0xE1CD82D0` e `strd r4, [r0, #8]` = `0xE1C040F8`: o que separa o `LDRD` do `STRD` e
o campo 7-4 (**1101** carrega, **1111** guarda), e nao o bit 20. Ler o bit 20 aqui
troca a leitura pela escrita -- e foi o que a primeira versao deste codigo fez, em
silêncio, **e o teste `ExtraLdrdCarregaDoisRegistradoresDaPilha` apanhou**.

O que nao esta implementado **RECUSA com o nome** (P2): a forma nao privilegiada
(`*T`), o `Rt` impar e o desalinhamento no `LDRD`/`STRD`, o `Rt`/`Rn` = PC, a
escrita na base com `Rd = Rn` e o offset de registrador com os bits 11-8 diferentes
de zero.

### 2. `tools/bateria.cpp` -- 1 linha, e e um defeito de INSTRUMENTO (P7)

    mem.Escrever32(kPPObj, 0);      // antes de `cpu.Repor(ci, kPilha)`

MEDIDO, com `[DEBUG-pilha4]` na fase de carga: **27 dos 62** titulos chegam a fase
`create` com o slot de saida `0x00090010` **ja com lixo** que a fase de CARGA la
deixou (fifa09 `0x15950248`, nfs `0xe59fa250`, cnk2 `0x05050604`, ... -- muitos sao
palavras de INSTRUCAO, `0xe58d0028`, `0xe1a00004`).

Consequencia: `e.create = mem.Ler32(kPPObj) != 0` media **"o slot tem lixo"** e nao
**"o `CreateInstance` criou o applet"**. O `AEEStaticMod_New` do SDK faz
`*ppMod = NULL;` na primeira linha; isto e o mesmo, do lado do instrumento.

Sem esta linha, o `zb2_comparar` acusa **duas regressoes falsas**: o `fifa09` e o
`nfs` aparecem como `applet true -> false`, quando o `true` era o lixo da carga.
Provado com o remendo aplicado e a correccao da CPU **revertida** (`git stash`):
os dois titulos mostram `_sem_applet`, ou seja, o `true` da referencia nunca foi um
applet. Com este remendo, o numero honesto de partida e **22**, e nao 41.

### 3. `tools/baseline/bateria.json` -- regravado (acto explicito)

`tools/regressao.sh --atualizar`, como o proprio script exige. O `git diff` deste
ficheiro e a lista do que mudou de estado. **Sem este passo o `ctest` falha com 16
"regressoes"** que sao o lixo do slot a desaparecer (ver acima).

### 4. `tools/provar_guardas_pilha.py` -- 11 violacoes deliberadas

    python3 tools/provar_guardas_pilha.py <raiz_da_arvore>

**11 de 11 VERMELHAS.** A V1 e a que reproduz a parede: com
`if (EhTransferenciaExtra(instr))` trocado por `if (false)`, o `LDRD` volta a ser
lido como `BIC` e os testes do `a3d` ficam vermelhos.

## O que fica por fazer, e o que CONTRADIZ o que se pensava

1. **O `kPPMod`/`kPPObj` (0x00090000 e 0x00090010) caem DENTRO da imagem do modulo
   em 29 dos 62 titulos** (tamanho > 0x90010: `cninja` 2829784, `nfs` 2627616,
   `quake2brew` 8705936, ...). O carregador escreve o ponteiro de modulo em cima do
   codigo do proprio modulo. Isto e anterior a esta frente, esta em
   `tools/bateria.cpp` (partilhado) e **nao foi corrigido**: a area de rascunho
   devia sair da imagem (o heap, ou uma pagina propria). E a explicacao mais
   provavel para boa parte do lixo do ponto 2.
2. **O `LDRH`/`STRH` estavam a ser mal executados em todo o corpus**, e nao so o
   `LDRD`. Medido no traco `[DEBUG-pilha3]` (execucao real, 62 titulos): 124
   palavras distintas do grupo, das quais `strh` 60, `ldrh` 21, `strd` 6, `ldrsb`,
   `ldrsh`, ... Todas iam para o `DadosProcessados`. **Isto contradiz a ideia de que
   a parede era so do `LDRD`** -- era uma familia inteira de instrucoes em falta.
3. **A `refusadas` subiu em 6 titulos** (`allstarcards` 0 -> 3999940, `rmp`
   0 -> 56173, `nfs` 0 -> 129026, `gof` 45 -> 28166, `zeeboids` 0 -> 48,
   `zeebovolley` 0 -> 1). Medido com `[DEBUG-pilha5]`, as palavras recusadas sao
   quase todas **dados a serem executados** (`0000e8bd`, `30b2e1b0`, `0108fab8`,
   `5000e0d4`), ou seja, titulos que ja andavam fora do modulo. Nenhuma recusa
   atingiu codigo valido dos titulos que criam applet.
4. **O Thumb continua sem `LDRH`/`STRH`/`LDRSB`/`LDRSH`** (`ExecutarThumb` tem 7
   formas). Nao foi tocado: nenhum titulo do corpus corre Thumb nesta fase.
5. **Os 6 dos 21 que nao ganharam applet**: `fifa09` e `nfs` passam a correr a fase
   toda e a esgotar o orcamento (deixaram de saltar para a pilha, mas ainda nao
   criam applet), `prey3d` sai agora para `0x0108d48e` (valor com cara de CLSID),
   `zeebotennis`, `Boiaz` e `allstarcards` saem para dentro do modulo ou continuam
   a orcamento.
6. **A tabela de `recusadas` do `bateria` nao diz QUAL instrucao**: com 4 milhoes
   de recusas num titulo, saber o nome da forma recusada exigiu o `[DEBUG-pilha5]`.
   Um campo por forma (LDRHT, Rt impar, desalinhado, ...) seria a medicao certa.

## Ficheiros partilhados que esta frente altera (para o merge)

| ficheiro | alteracao | porque |
|---|---|---|
| `core/cpu/arm_interpreter.h` | declaracao de `TransferenciaExtra` (4 linhas) | a familia nova |
| `core/cpu/arm_interpreter.cpp` | o grupo + o ramo no `g == 0` (~180 linhas) | a correccao |
| `tools/bateria.cpp` | `mem.Escrever32(kPPObj, 0);` (1 linha + comentario) | o instrumento do `applet` |
| `tools/baseline/bateria.json` | regravado com `--atualizar` | a referencia tem de ser honesta |

Nao tocados: `core/brew/despacho.cpp` (a sonda `[DEBUG-pilha1]`/`[DEBUG-pilha2]` foi
temporaria e foi revertida -- o anel do commit `eb62459` chegou para a causa, **nao
e preciso mais instrumento no despacho**).
