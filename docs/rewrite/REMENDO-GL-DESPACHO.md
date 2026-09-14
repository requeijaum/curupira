# REMENDO: a cablagem do GL no despacho (IGL + IEGL)

Este documento e o **remendo exacto** para os dois ficheiros partilhados
(`core/brew/despacho.h` e `core/brew/despacho.cpp`). O dono desses ficheiros aplica-o.
Tudo o resto da etapa ja esta na branch `egl`: `core/brew/egl.{h,cpp}` (o IEGL),
`tools/gerar_slots.py` (o IGL e o IEGL declarados uma vez, mais as constantes do EGL),
`tools/gl_slots.inc` (regenerado), `tools/sonda_gl.cpp` (reescrita) e
`tests/egl_test.cpp`.

## Como se aplica (e como se prova)

    python3 tools/remendo_gl.py <raiz_da_arvore>            # aplica
    python3 tools/remendo_gl.py <raiz_da_arvore> --reverter # desfaz

O script tem **sete alteracoes**, cada uma com `assert` antes (a ancora existe
exactamente uma vez?) e depois (ficou com a contagem de linhas esperada: 189 -> 214
no `.h`, 1000 -> 1068 no `.cpp`). Uma ancora em falta NAO escreve nada. Ele recusa
aplicar se os ficheiros ja nao tiverem as contagens de partida -- o que tambem quer
dizer que recusa quando alguem mexeu nos ficheiros entretanto, e isso e a resposta
certa.

Aplicado numa COPIA da arvore (`/tmp/prova-gl2`), com o `full-rewrite` em `96ede69`:

| prova | antes | depois |
|---|---|---|
| `zb2_tests` | 289 verdes, 1 saltado (a cablagem) | **293 verdes, 0 saltados** |
| `ctest` | 6/6 | 5/6 (ver "a regressao do pbc" abaixo) |
| `zb2_sonda_gl`, 4 titulos com wrapper | **0 de 232** chamadas chegam | **232 de 232** (0 engolidas) |

## As sete alteracoes

1. **`despacho.h`: os `#include` do GL e a guarda do teste.** Depois de
   `#include "core/brew/ajudantes.h"`: `#define ZB2_CABLAGEM_GL 1` mais os includes
   de `core/brew/egl.h` e `core/brew/igl.h`. O `#define` e o que faz o
   `tests/gl_cablagem_test.cpp` correr de verdade em vez de saltar.

2. **`despacho.h`: os acessores e o membro.** Antes de
   `void DefinirVtableBitmap(...)`: `InstalarGl`, `IglRef`/`EglRef`.
   No privado, depois de `Temporizador timer_;`: `Igl igl_;` e `Egl egl_;`.

3. **`despacho.cpp`: a lista de inicializacao.** `igl_(mem, traco), egl_(mem, traco)`
   acrescentados depois de `ihid_(mem, traco, sinais_, entrada_)`.

4. **`despacho.cpp`: o `InstalarGl`.** Devolve os slots cablados no total (0 =
   falhou), recusa se os dois objectos ficarem no mesmo endereco, e emite
   `GL_CABLADO` com os enderecos.

5. **`despacho.cpp`: a instalacao no `InstalarAjudantes`.** Uma chamada
   `InstalarGl(saidas)` logo depois do comentario do `IMedia` e antes do laco dos
   117 ajudantes; se devolver 0, registra `cablagem_do_GL` como falta.

6. **`despacho.cpp`: o `CreateInstance` do `AEECLSID_GL` (e do `EGL`).** No ramo
   `idx == kBaseDoShell + 2`, depois do `kIidFileMgr`:
   `else if (iid == kClsidIgl) devolver = igl_.Objeto();` e a mesma linha para o
   `kClsidIegl`. **O `AEECLSID_GL` esta MEDIDO no `ddragonz` (0x11d61c-0x11d634: o
   jogo guarda o resultado em `gpIGL`); o `AEECLSID_EGL` vem de `AEEGL.h` e nao foi
   pedido por nenhum dos 4 titulos com wrapper.**

7. **`despacho.cpp`: o RAMO NOVO, ANTES do `idx >= kBaseDoShell`.** Dois ramos na
   cadeia do `Correr`, imediatamente antes de `} else if (idx >= kBaseDoShell) {`:

       } else if (idx >= kVtableIgl && idx < kVtableIgl + gl_slots::kIglSlots) { ... }
       } else if (idx >= kVtableIegl && idx < kVtableIegl + gl_slots::kIeglSlots) { ... }
       } else if (idx >= kBaseDoShell) { ... }

   Cada um monta o `ArgumentosGl` (4 registos, `sp`, `lr`), chama
   `igl_`/`egl_.Executar(idx - kVtable..., av, &retorno)` e poe o `r0`.

## O erro de ORDEM: a OITAVA vez, e agora com o numero

**A faixa do GL e 30000/31000, e o ramo generico `idx >= kBaseDoShell` e 2000.**
Um ramo escrito depois do generico nunca corre: a chamada e atendida como se fosse
do `IFileMgr`. Medido com o ramo ausente, 4 titulos do corpus:

    chessbots  74 chamadas ->  0 chegam ("IFileMgr::slot23003" ... "IFileMgr::slot23xxx")
    ddragonz   41 chamadas ->  0 chegam ("IFileMgr::slot24019" para o `eglMakeCurrent`)
    tectoy     61 chamadas ->  0 chegam
    nfs        56 chamadas ->  0 chegam
    ------------------------------------------------------
    TOTAL     232 chamadas ->  0 chegam | com o ramo: 232 de 232

A guarda que apanha isto e `CablagemGl.AORDEMDoRamoEAGuardaDoOitavoCaso`: ela exige
que o pedido tenha deixado no traco o NOME do metodo de GL e que NENHUMA falta
tenha o nome de outra interface. Provada por violacao: com o ramo do GL tornado
generico (`false && idx >= ...`), esse teste e o
`ODespachoEntregaAChamadaAoIgl` ficam VERMELHOS.

## Dois numeros que este remendo NAO usa, e por que

**A faixa e 30000/31000, e nao 20000/21000.** As constantes nasceram a 20000 no
`igl.h`, e a etapa 8 escolheu -- noutro ficheiro, sem saber desta -- a MESMA base
para a ENTRADA (`tools/bateria.cpp:65` instala-a em 20000; 32 + 16 slots, de 20000
a 20047). Os indices 20000..20047 ficariam SOMBREADOS, porque
`AtenderEntrada(cpu, idx)` corre antes de qualquer ramo do GL. Ruling: o IGL sobe.
Custo se estiver errado: um numero diferente do que estava escrito no relatorio da
etapa 6.

**Os enderecos dos objectos sao 0x800B0000/0x800B1000, e nao 0x800A0000/0x800A1000.**
A `imedia.h` (232-233) usa os mesmos dois para os avisos de midia. O mapa medido dos
enderecos esta em `igl.h`.

## A regressao do `pbc`, dita por inteiro

Com o remendo aplicado, o juiz do projeto (`tools/regressao.sh`, 62 titulos) falha:

    REGRESSOES (1) por campo: passos_start
      pbc (280238): passos_start 4000000 -> 375   [maior melhor]
    NEUTROS QUE MUDARAM: faltas 62 (rasterizador_de_GL 0 -> 2 em todos -- e a
    instalacao dos dois modulos), build 1, motivo 1 (pbc: create "saiu_do_modulo"
    -> "laco_de_saidas"), passos_create 1

**Nao atualizei o baseline.** A regra do projeto e explicita: um instrumento que se
auto-atualiza quando falha transforma toda a regressao num "sem regressoes".

O que se mediu para isolar a causa, e o que continua aberto:

| experimento | `pbc` create / start |
|---|---|
| sem o remendo (branch `egl`, `ctest` 6/6) | 1966143 passos / `start:orcamento_esgotado` (0,4 M: 4000000) |
| com o remendo | 245793 passos, `create:laco_de_saidas` / `start:saiu_do_modulo_para_0x10239052`, 375 passos |
| com o remendo SEM a alteracao 6 (o `CreateInstance` do GL/EGL desligado) | **identico** ao anterior |

Logo a alteracao 6 esta REFUTADA como causa (medida, nao suposta). Sobram duas:

- **(b) os dois ramos novos do despacho**: um indice que o `pbc` use na faixa
  30000+ deixa de receber `AEE_EUNSUPPORTED` e passa a receber o que o IGL/IEGL
  devolvem. Um pedido servido muda o caminho do jogo.
- **(c) a instalacao dos dois objectos** no `InstalarAjudantes` (escreve duas
  vtables na faixa de saida -- enderecos que antes nao eram escritos).

O comando que separa (b) de (c): desligar so o ramo do IGL (o `false &&` da
violacao V1 de `tools/provar_guardas_gl.py`) e repetir a bateria do `pbc`:

    ./build/zb2_bateria /tmp/pbc.json "$mods" /tmp/pbc.json

**Nenhuma linha `GL_*`/`EGL_*` aparece no traco do `pbc`**, e isso torna (b) menos
provavel: nao se viu o `pbc` a entrar na faixa do GL. Nao tenho a resposta, e
prefiro dize-lo a inventar uma.
