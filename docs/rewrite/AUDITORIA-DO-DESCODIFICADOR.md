# A AUDITORIA DIFERENCIAL DO DESCODIFICADOR ARM (objdump contra o interpretador)

Branch `audit-desc`, worktree `/tmp/wt-audit`, base `0286921` (a parede da PILHA).
A arvore antiga (`new_ez_ui`) fica intacta.

## O comando que fica VERMELHO primeiro, e o numero

    python3 tools/auditar_descodificador.py "$mods"                    # nao existia
    python3 tools/auditar_descodificador.py "$corpus" --mods "$mods"   # a ferramenta

O comando que responde a pergunta -- "quantas palavras o interpretador le como
OUTRA INSTRUCAO, sem recusar" -- e este, sobre as 13 063 614 palavras alinhadas
dos 62 `.mod` do corpus:

    # ANTES (commit 0286921)
    # concorda: 12199439 | executamos_outra (SILENCIOSO): 66376 | recusamos: 109260

    # DEPOIS
    # concorda: 12271307 | executamos_outra (SILENCIOSO): 2838 | recusamos falta a forma: 16969

A COLUNA QUE INTERESSA E A DO MEIO: **66 376 -> 2 838 palavras em que o
interpretador executa outra instrucao, em silencio** (-95,7%). E a mesma classe de
defeito do `ldrd` da parede anterior -- **a mais caro do projeto**: uma instrucao
mal descodificada NAO recusa, da um resultado plausivel e errado.

As 2 838 que restam NAO sao codigo mal descodificado:
  * 413 sao `mrs` que o binutils descodifica a mais (bits 19-16 diferentes de
    1111, SBO violado: o ARM ARM nao define essa palavra) -- aqui a NOSSA leitura
    e a certa e o oraculo e que e permissivo;
  * o resto sao as instrucoes DSP/SIMD do ARMv6 (SMLAD/SMLSLD/SMMUL/USAD8) e as
    de exclusao mutua (STREX), que continuam a ser lidas como dados processados,
    e que na sua maioria caem em DADOS do modulo (ver "o que ficou por fazer").

## Como a ferramenta funciona, e porque nao tem uma segunda descodificacao

    objdump -D -b binary -m armv6 <ficheiro>           (o oraculo, binutils 2.44)
    zb2_sonda_descodificador <ficheiro> --bin=...      (o NOSSO ramo, palavra a palavra)

A sonda C++ NAO descodifica nada por sua conta: executa a palavra UMA vez num
estado zerado e le o NOME que o ramo de execucao escreveu (`FamiliaDaUltima`,
`MotivoDaRecusa`, novos em `core/cpu/arm_interpreter.h`). Uma segunda
descodificacao escrita em Python poderia divergir da primeira e concordar consigo
mesma -- que e o defeito a encontrar, e nao o metodo.

Tres classes de divergencia, por ordem de gravidade:

| classe | o que e | antes | depois |
|---|---|---|---|
| (b) `executamos_outra` | o SILENCIOSO: corremos outra instrucao | 66 376 | **2 838** |
| (a) `recusamos` | falta a forma, e a recusa tem nome (P2) | 109 260 | **16 969** |
| (c) `nome_diferente` | o mesmo efeito com dois nomes | 44 282 | 44 115 |
| `objdump_nao_sabe` | o objdump diz `.word`/`UNDEF`: sao DADOS | 326 865 | 326 865 |

As recusas "com o nome certo" (401 520) sao o que RECUSA por nao estar
implementado a EXECUCAO: coprocessador (`stc`/`ldc`/`cdp`/`mcr`/`mrc`), `SWI` sem
tratador, `LDRD`/`STRD` nas formas UNPREDICTABLE, `and` com escrita no PC e S,
`stmdb`/`ldmdb` com o `^`. Sao honestas (P2), e nao descodificacao.

No ramo (c) a maior parte e vocabulario: `strb ~ strbt` 20 394, `ldrb ~ ldrbt`
12 682, `str ~ strt` 8 388, `ldr ~ ldrt` 2 557 -- as formas nao privilegiadas
(`*T`). Este emulador corre em modo UTILIZADOR e sem MMU, onde as duas formas sao
a MESMA operacao; executa-las como a forma normal esta certo.

## As classes que estavam EM FALTA, e o que cada uma custava

O auditor encontrou cinco familias inteiras a correr outra coisa, mais tres
formas pontuais:

| classe | palavras no corpus | o que o interpretador fazia | onde |
|---|---|---|---|
| ARMv6 `UXTH` | 17 295 | `ldrb` | bits 27-24 = 0110, que o primeiro nivel le como transferencia |
| ARMv6 `UXTB` | 12 764 | `strb` | idem |
| ARMv6 `SXTAB` | 6 432 | `str` | idem |
| ARMv6 `SXTH` | 2 183 | `ldr` | idem |
| ARMv6 `SXTB` | 781 | `str` | idem |
| ARMv5TE `SMULxy`/`SMLAxy` | 1 105 + 1 013 | `cmn`/`tst` | bits 27-24 = 0001, que o primeiro nivel le como dados processados |
| ARMv6 `CLZ` | 259 | `cmn` | idem |
| `PLD` | 4 630 | recusado como "condicao NV" | o PLD TEM condicao 1111 na codificacao |
| `MSR` de registrador | -- | `teq` (nao escrevia o CPSR) | forma que faltava no grupo 000 |
| `MCR`/`MRC` | -- | `SWI` sem tratador | o bit 24 e que separa o SWI do coprocessador |
| `BLX <rotulo>` | -- | recusado como "condicao NV" | forma imediata, condicao 1111 |
| `MRS` | -- | engolia o `SWP` (0xE10F0090) | a mascara nao ia ate ao bit 0 |
| `BKPT`/`HLT` | -- | `teq`, em silencio | entra em modo de depuracao: RECUSA |

E a ARMADILHA do `SMULxy`: os bits 15-12 sao reservados e TEM de ser zero --
medido, `0xE1641382` (bits 15-12 = 1) nao e `smulbb`, e `cmn r4, r2, lsl #3`. Uma
mascara que os deixasse livres trocava uma instrucao de dados processados por uma
multiplicacao, em silencio.

E a fronteira do grupo media: numa transferencia com offset de registrador o BIT
4 e ZERO (bits 11-4 = deslocamento: bits 11-7 quantidade, 6-5 tipo, bit 4 = 0) e
todas as formas media tem o bit 4 = 1. Logo `bits 27-24 = 0110 e bit 4 = 1` e a
fronteira. Sem ela, o `str r0, [r0, -r2]` era lido como uma extensao de sinal.

## O lado do THUMB: a mesma classe, coberta por INTEIRO

O espaco Thumb inteiro sao 65 536 meias-palavras, e ele cabe todo numa corrida do
auditor (`--thumb`, `-M force-thumb`). Resultado depois das correccoes:

    # executamos_outra (SILENCIOSO): 0
    # nome_diferente: 0
    # recusamos falta a forma: 22 692

O que se corrigiu, e o que se mediu:

1. **O `L` do formato 8/9 era lido nos bits 12-11.** O `L` e o BIT 11 em todas
   estas formas (0x6000/0x6800 palavra, 0x7000/0x7800 byte, 0x8000/0x8800
   meia-palavra, 0x9000/0x9800 palavra); ler os bits 12-11 da 0 no 0x6000 mas
   **2 no 0x7000 e no 0x9000**, e como o teste era `op == 0`, o `strb` e o `str`
   de palavra do Thumb eram executados como LEITURA -- 2 048 + 2 048 meias-palavras
   do espaco. Este defeito estava ESCONDIDO pela tabela de classes do proprio
   auditor: com `ldr` e `str` na mesma classe, ele aparecia como
   "nome_diferente". A tabela foi partida em CARGA e GUARDA por causa disso.
2. **A guarda da meia-palavra estava MORTA**: `(instr & 0xF000) == 0x8000` E
   `(instr & 0x1000) != 0` nunca sao verdadeiras ao mesmo tempo (a primeira diz
   que o bit 12 e ZERO). O `strh`/`ldrh` do formato 8 corriam como `str`/`ldr` de
   32 bits -- escrevem quatro bytes onde o jogo escreve dois.
3. **O formato 5 (0x5000) nao existia** -- era recusado. E onde vivem as sete
   formas de memoria com registrador, incluindo `ldrh`/`strh`/`ldrsb`/`ldrsh`
   (as que o agente anterior tinha medido em falta).

FORMAS QUE CONTINUAM A FALTAR, com o nome e o numero (o auditor da o nome do ARM
ARM A6.2 em cada recusa):

| formato Thumb | meias-palavras | o que tem la |
|---|---|---|
| 1 deslocamento imediato (LSL/LSR/ASR) | 4 096 | `lsls`/`lsrs`/`asrs` |
| 2 add/sub registrador | 2 048 | `adds`/`subs` (registrador) |
| 4 operacoes ALU | 1 936 | `and`/`eor`/`adc`/`sbc`/`ror`/`mul`/`neg`... |
| 10 add rd, pc/sp | 4 096 | `add r0, pc, #n` |
| 11 add/sub sp (e push/pop/bkpt) | 3 348 | `add sp, #n`, `push`, `pop` |
| 12 stmia/ldmia | 4 096 | `stmia r0!, {...}` / `ldmia` |
| 19 bl/blx (metade de prefixo/sufixo) | 3 072 | metades de instrucoes de 32 bits |

Nenhum titulo do corpus corre Thumb nesta fase: e LATENTE, e agora esta medido.

## O numero da bateria: antes -> depois, e o que CONTRADIZ o que se pensava

    ./build/zb2_bateria "$corpus" "$mods" <saida>.json
    ./build/zb2_comparar curupira/tools/baseline/bateria.json <saida>.json

| medicao | antes | depois |
|---|---|---|
| carga / modulo | 62 / 62 | 62 / 62 |
| vtable | 48 | 48 |
| **applet** | **37** | **37** |
| titulos que saem do modulo | 41 | 41 |
| `recusadas` (soma dos 62) | 4 213 388 | |
| `PIXELS` | 0 | 0 |

**A CORRECCAO NAO MOVEU A PAREDE, E ISSO E UM RESULTADO.** As classes corrigidas
CORREM na bateria: com um contador temporario (`[DEBUG-desc1]`, ja removido do
codigo) mediu-se, na corrida dos 62 titulos:

    [DEBUG-desc1] media_armv6=1010 dsp_armv5te=6

e 953 delas no `heavyweaponbrew`. Antes desta frente, cada uma dessas
1 010 execucoes corria OUTRA instrucao. E o efeito medido foi:

* `heavyweaponbrew`: `passos_create` 7 328 -> 7 338 e
  `faltas.IShell::slot41` 14 -> 15 (pede o `LoadResDataEx` uma vez mais);
* `gof`: `recusadas` 28 166 -> 28 161 e o PC de saida mudou de `0xff00f20e` para
  `0xff00f13a`;
* os outros 60 titulos: nenhuma mudanca.

Ou seja: **os 41 titulos que saem do modulo NAO saem por causa de uma instrucao
mal descodificada** -- nem do `ldrd`/`ldrh` (frente anterior), nem das familias
desta. Nas duas frentes a parede tem a mesma forma: o guest executa poucas
instrucoes (o `a3d` faz 10 na fase `create`) e sai com um valor que tem cara de
DADOS lidos do sitio errado. Isso esta no ESTADO com que o titulo entra, e nao na
descodificacao.

## Ficheiros

| ficheiro | o que e |
|---|---|
| `tools/auditar_descodificador.py` | NOVO. O auditor (ferramenta versionada) |
| `tools/sonda_descodificador.cpp` | NOVO. A metade C++: le o ramo que correu |
| `tools/provar_guardas_descodificador.py` | NOVO. **20 violacoes deliberadas, 20 VERMELHAS** |
| `docs/rewrite/AUDITORIA-DO-DESCODIFICADOR.md` | este documento |
| `core/cpu/arm_interpreter.{h,cpp}` | a sonda + as familias novas (PARTILHADO) |
| `tests/cpu_test.cpp` | +29 testes (PARTILHADO) |
| `CMakeLists.txt` | o alvo da sonda (1 alvo) |

A sonda (`FamiliaDaUltima`, `MotivoDaRecusa`) e INSTRUMENTO, e nao emulacao: ela
nao muda um unico comportamento do interpretador, e existe para o auditor poder
comparar sem uma segunda descodificacao. Foi ela que apanhou dois erros MEUS na
primeira corrida (o `ldrsb`/`ldrsh` trocados no nome, e o `ldc` chamado `mcr`).

## As guardas, provadas por violacao

    python3 tools/provar_guardas_descodificador.py <raiz_da_arvore>   # ZB2_DIR=<build>
    # 20 violacoes deliberadas, 20 VERMELHAS, 0 que nao apanharam

Cada violacao quebra UMA coisa (o ramo que desaparece, a mascara que alarga, a
rodagem que nao acontece, o `L` que muda de bit, o `strh` que escreve quatro
bytes) e o veredito e o do teste: se ficar verde, a guarda NAO e guarda. Duas
delas ficaram verdes na primeira corrida -- o teste do `SMULxy` comparava so o
NUMERO (e trocar os bits 6 e 5 da as mesmas metades escolhidas; o que muda e o
NOME, que e o que o auditor compara com o objdump), e o do `L` do Thumb so
exercitava as formas de meia-palavra, onde os bits 12-11 e o bit 11 dao o mesmo.
Ambas passaram a exercitar o que a guarda protege.

E no `ctest`, uma guarda que corre sempre:

    add_test(NAME descodificador_espaco_thumb ...)   # tools/guarda_descodificador.sh

O espaco Thumb INTEIRO (65 536 meias-palavras, cobertura completa e nao uma
amostra) em 0,5 s, com o criterio "ZERO palavras executadas como outra
instrucao". Provada por violacao: com o `L` do Thumb de volta nos bits 12-11, o
teste fica VERMELHO com `4096`. O corpus inteiro (13 milhoes de palavras, ~3
min) fica fora do ctest pelo tempo, e corre-se a mao.

## O que ficou por fazer

1. **ARMv6 DSP/SIMD**: `SMLAD`/`SMLSD`/`SMLALD`/`SMLSLD`/`SMUAD`/`SMUSD`/`USAD8`/
   `USADA8`/`SMMUL` e `LDREX`/`STREX` continuam a ser lidos como dados
   processados. O auditor nomeia-as (o binutils sabe-as); as mascaras saem do
   mesmo metodo (varredura do espaco + derivacao do nome). Contadas, a maior
   parte cai em DADOS do modulo (`0x474f5250` = "PROG" descodifica como
   `smlsldmi`), e a separacao entre codigo e dados precisa de um TRACO DE PCs
   executados, que esta frente nao tem.
2. **O `SEL` e a aritmetica paralela (`SADD16`...)** RECUSAM com nome: dependem
   das bandeiras GE do CPSR, que o interpretador nao emula. Recusar e o que o P2
   manda; implementa-las pede primeiro as GE.
3. **O binutils e MAIS PERMISSIVO do que o ARM ARM no `MRS`**: descodifica como
   `mrs` palavras com os bits 19-16 diferentes de 1111 (SBO violado), que o ARM
   ARM nao define. Sao 413 das divergencias residuais, e a NOSSA leitura e a
   certa -- mas e preciso saber que o oraculo nao e infalivel.
4. **O filtro codigo/dados.** A maior parte das divergencias residuais esta em
   palavras de DADOS (o interpretador executa-as quando o PC lhes chega). Sem o
   conjunto de PCs executados, os numeros desta auditoria sao um LIMITE SUPERIOR.
