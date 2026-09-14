# AUDITORIA -- O PLANO CONTRA O CODIGO, E A HONESTIDADE DOS TESTES

Auditoria da arvore nova (`curupira/`) contra `research/sources/zeebulator/docs/rewrite/PLAN.md`
(10 etapas, cada uma com um criterio que corre num comando e um titulo do corpus
que a prova) e contra os proprios testes (376 testes compilados).

Branch `auditoria-testes`, worktree `/tmp/wt-auditoria-testes`, base **`0286921`**
("A parede da PILHA: o grupo extra load/store do ARM nao existia (21 -> 0 titulos)").

**Regra de leitura deste documento:** cada numero tem, ao lado, o comando que o
produziu. Onde a medicao contradiz o que se pensava, esta escrito na seccao
"O que CONTRADIZ o que se pensava".

---

## 0. Como se mediu

    cd <worktree>/curupira
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j4

Comandos, todos corridos nesta arvore nesta auditoria:

| o que mede | comando |
|---|---|
| os testes unitarios | `./build/zb2_tests` |
| as guardas de construcao | `ctest --test-dir build` (8 testes) |
| o corpus inteiro | `./build/zb2_bateria "$corpus" "$mods" /tmp/wt-auditoria-testes/bateria_atual.json` |
| um titulo | `./build/zb2_bateria <corpus_de_uma_entrada> "$mods" <saida.json>` |
| as tabelas do SDK | `ZB2_SDK_DIR="<SDK>" ./tools/verificar_{slots,slots_gl,clsids,ajudantes}.sh` |
| a bancada da midia | `./build/zb2_sonda_media` |
| a bancada do GL | `./build/zb2_sonda_gl <mods>/274754/ddragonz.mod` |
| a sonda do modulo | `./build/zb2_sonda_mod <mods>/12875/imicro3d.mod` |
| a regressao | `./tools/regressao.sh` (bateria + `zb2_comparar` contra `tools/baseline/bateria.json`) |
| o laco de quadro | `ZB2_QUADROS=60 ./build/zb2_bateria ...` |
| a entrada (etapa 8) | `ZB2_ENTRADA="0 eixo 0x0106c4d0 0;200 botao 0x0106c3fe 1" ./build/zb2_bateria ...` |

E, para os testes, **o unico metodo que vale**: aplicar uma mudanca deliberada na
implementacao, reconstruir, correr o teste e ver se ele fica VERMELHO. O
instrumento desta auditoria e `tools/auditoria/runner_mutantes.py` (45 mutantes, ~3 s cada,
com `git checkout` do ficheiro e conferencia do `sha256` no fim de cada um).

### O estado medido, agora

    == 62 titulos | carga 62 | ponteiro de modulo 62 | applet 37 ==
    PIXELS 0 em 62 de 62 | CORES maximo 1 | QUADROS 0 | textos 0 | blits 0

    ./build/zb2_tests       -> 376 testes, 376 PASSED, 0 SKIPPED
    ctest --test-dir build  -> 8/8 (inclui `regressao`, 50,9 s)

O que o corpus pede e nao recebe, por demanda (do fim do log da bateria):

| falta | vezes |
|---|---|
| `AEEHelperFuncs[0x140] vsnprintf` | 116 |
| `IShell::slot41` (`LoadResDataEx`) | 21 |
| `IShell::CreateInstance` CLSID desconhecido (THREAD 9, PNGDECODER_BREW 3, 0x0103d8ec 3, 0x01011810 1) | 16 |
| `AEEHelperFuncs[0x074] realloc` | 9 |
| `IShell::slot44` | 2 |
| `IAppHistory::Back` / `GetClass`, `IRootForm HandleEvent`, `ITextCtl::*` (6 metodos), `IFileMgr::slot5` | 1 cada |

**`rasterizador_de_GL` NAO esta nesta lista, e nao esta porque deixou de existir.**
Ver a seccao de contradicoes.

---

## PARTE A -- O PLANO CONTRA O CODIGO

Resumo. "Entregavel" = os ficheiros que o plano nomeia existem; "criterio" = o
comando do plano passa; "titulo" = o titulo nomeado prova o criterio.

| etapa | entregavel | criterio | titulo prova | veredicto |
|---|---|---|---|---|
| 0 fundacao de medicao | existe | passa (1 ressalva) | nenhum | **CUMPRIDA COM RESSALVA** |
| 1 CPU de referencia | existe | passa | nenhum | **CUMPRIDA COM RESSALVA** |
| 2 carregador de MOD | existe | passa | `imicro3d` | **CUMPRIDA** (com um instrumento a contradize-la) |
| 3 IShell, IDisplay e 2D | **parcial** (`core/brew/idisplay` nao existe) | **NAO passa** | `zumar`/`pacmania` | **NAO CUMPRIDA** |
| 4 ficheiros, VFS, 62 titulos | parcial (GGZ/PAKZ ausente) | passa | os 62 | **CUMPRIDA** |
| 5 midia e audio | existe | **NAO passa num titulo** | `a3d` | **NAO CUMPRIDA** |
| 6 GL e as duas familias 3D | existe | **NAO passa** | `ddragonz`/`cnk2` | **NAO CUMPRIDA** |
| 7 tabelas verificadas contra o SDK | existe | passa | nenhum | **CUMPRIDA** |
| 8 entrada | existe | **NAO passa num titulo** | `tectoy` | **PARCIAL** |
| 9 regressoes automaticas | existe | passa (e esta provado por violacao) | os 62 | **CUMPRIDA** |

### Etapa 0 -- a fundacao de medicao

Entregaveis, presentes: `core/tempo/tempo.h`, `core/traco/traco.{h,cpp}`,
`core/memoria/memoria.{h,cpp}`, `tests/fundacao_test.cpp` (20 testes).

Criterio, parte 1: "`ctest` verde".

    ctest --test-dir build   ->  8/8, 100% tests passed   (o `fundacao` = zb2_tests, 376/376)

Criterio, parte 2: "um teste que **prova** que dois registadores com o mesmo
argumento produzem a mesma linha".

**Nao existe.** Procurado em `tests/` por `Comparar(`, `QuantosComNome`,
`linha`, `format`, `determin` e `MesmoArgumento`: o que existe e
`Traco.CompararAceitaMesmaConfiguracaoEDaODiferenca` (duas etiquetas iguais dao
um numero) e `Traco.CompararRecusaConfiguracoesDiferentes` (etiquetas diferentes
RECUSAM). Nenhum teste compara as LINHAS produzidas por dois registadores. A
determinacao de que a mesma entrada produz a mesma linha esta afirmada no
`DESIGN.md` (P4) e no comentario do `tempo.h`; nao esta medida.

Criterio, parte 3: "escrever num endereco vigiado dispara o aviso **uma vez por
escritor**".

O teste `Memoria.VigiaRegistaOPrimeiroEscritorDeCadaEndereco` prova **uma vez por
ENDERECO** (3 escritas em 2 enderecos -> 2 registos) e prova que a escrita fora da
faixa e ignorada. "Por escritor" nao e exercitado: o teste declara um so escritor
(`EscritorUnico("cpu")`). Veredicto da etapa: **CUMPRIDA COM RESSALVA** -- a
substancia existe e esta provada por violacao, o texto do criterio nao esta
coberto palavra a palavra.

Provado por violacao (VERMELHO com a guarda arrancada): a recusa de caminho
relativo, a recusa de comparar configuracoes diferentes, o contador de faltas, as
marcas `[DEBUG-`, o "primeira escrita por endereco", o limite da faixa da vigia,
o contador de escritas de outro autor e o limite da cadeia terminada em NUL.

### Etapa 1 -- CPU de referencia

Entregaveis: `core/cpu/arm_interpreter.{h,cpp}` (922 linhas),
`tests/cpu_test.cpp` (42 testes, inclui o grupo extra load/store que fechou a
parede da PILHA).

Criterio, parte 1: "`mrs/cpsr` devolve um modo valido".

    ./build/zb2_tests --gtest_filter='Cpu.ZeroNaoEUmModoValido:Cpu.AposRepor*:Cpu.MrsDevolve*'
    -> 3 testes, 3 PASSED
    ./build/zb2_sonda_mod <mods>/12875/imicro3d.mod  ->  "CPSR=0x000000d0 (modo valido: sim)"

Provado por violacao: `ModoValido` a devolver `true` no `default` -> VERMELHO em
`Cpu.ZeroNaoEUmModoValido`; `SetCpsr` a nao recusar -> VERMELHO em
`Cpu.EscreverUmModoInvalidoERecusado`; a ordem dos operandos do `MLA` trocada
(aquela que foi um bug real) -> VERMELHO em `Cpu.MulEMla`.

Criterio, parte 2: "um teste diferencial contra um conjunto de instrucoes com
resultado conhecido". Existe: 42 testes com valores calculados a mao, com as
instrucoes ARM construidas por um **montador minimo de campos nomeados**
(`tests/cpu_test.cpp:113` e seguintes) e nao por literais hexadecimais. Nota de
metodo: este diferencial e contra o resultado que o teste **escreve**, e nao
contra um orquestrador externo -- e o oposto do `formato`, que se compara com o
`snprintf` DO SISTEMA. Nao ha aqui um `assembler` ou `qemu` como referencia.

**Lacuna encontrada por violacao:** o `cpsr` do CONSTRUTOR
(`Arminterpreter` a nascer) pode ser posto a ZERO e **nenhum dos 42 testes fica
vermelho** (mutante 9, filtro `Cpu.AposRepor*,Cpu.MrsDevolve*`; e mutante 39,
filtro `Cpu.*`). O caminho coberto e o `Repor`, que escreve o valor correcto numa
linha propria. **O bug que o ledger diz ter custado uma investigacao inteira (o
Zeebulator antigo a comecar com `cpsr=0`) nao tem teste no ponto onde ele
acontecia.** Veredicto: **CUMPRIDA COM RESSALVA**.

### Etapa 2 -- carregador de MOD e o primeiro titulo

Entregaveis: `core/carga/mod.{h,cpp}`, `core/carga/bar.{h,cpp}`,
`tests/carga_test.cpp` (15), `tests/bar_test.cpp` (26), `tests/mod_base_test.cpp`
(3 -- a base do modulo E ZERO, derivada dos literais do `pacmania.mod`: 51
literais apontam para texto com a base a zero, 0 com a base antiga), e
`tools/sonda_mod.cpp`.

Criterio: "o `imicro3d` carrega, o applet pointer e nao nulo, e o
`HandleEvent(EVT_APP_START)` retorna".

    ./build/zb2_bateria /tmp/aud/corpus_imicro3d.json "$mods" /tmp/aud/bat_imicro3d.json
    imicro3d  90068  sim sim sim   60  340  0  0  1   retornou | create:retornou | start:retornou
    == 1 titulos | carga 1 | ponteiro de modulo 1 | applet 1 ==

Isto e: carrega, o `CreateInstance` escreveu um ponteiro de applet nao nulo, e a
fase do `EVT_APP_START` correu 10 passos e **retornou**. Criterio cumprido.

Provado por violacao: nao escrever o ponteiro da tabela de ajudantes em `base-4`
(ROPI) -> VERMELHO em `Carga.OCarregadorEscreveATabelaEmBaseMenosQuatro`; o mesmo
para `ponto_de_entrada`.

**CONTRADICAO (dois instrumentos, o mesmo titulo):**

    ./build/zb2_sonda_mod <mods>/12875/imicro3d.mod
    CreateInstance RETORNOU apos 18 instrucoes
    applet (em 0x00090010) = 0x00000000      <<< ZERO
    ponteiro do modulo (em 0x00090000) = 0x80200010

A bateria diz applet nao nulo; a sonda diz zero. A causa e a CONVENCAO DE CHAMADA:
a sonda (`tools/sonda_mod.cpp:205-208`) chama `CreateInstance(po, ClsId, ppObj)`
com tres argumentos (r0=modulo, r1=cls_id, r2=kPPObj), e a bateria chama com
QUATRO (`bateria.cpp:686`: `r0=po, r1=pIShell, r2=ClsId, r3=ppApplet`). Nenhuma
das duas e datada no ficheiro. A sonda e o instrumento que o ledger cita para a
etapa 2 ("a sonda `tools/sonda_mod` mede cada passo"): **para este titulo, ela
imprime um zero que a bateria contradiz, e nada na arvore obriga as duas a
concordarem.** Veredicto: **CUMPRIDA** pela bateria, com o instrumento antigo a
divergir.

### Etapa 3 -- IShell, IDisplay e desenho 2D

Entregaveis que o plano nomeia: `core/brew/ishell`, `core/brew/idisplay`,
`core/video/rasterizador`. Na arvore real **nao ha `core/brew/ishell` nem
`core/brew/idisplay`**: o `IShell` e o `IDisplay` vivem dentro de
`core/brew/despacho.cpp` (1238 linhas), `core/brew/interface.cpp` e
`core/brew/tela.cpp`. O rasterizador existe: `core/video/rasterizador.{h,cpp}`
(572 linhas) e `tests/rasterizador_test.cpp` (17 testes). **Entregavel: PARCIAL**
(o nome dos ficheiros nao bate certo; o conteudo existe noutro sitio).

Criterio: "um titulo 2D desenha e o ecra tem mais de 10 cores. Verificado na
bateria, nao numa captura a olho. Titulo: `zumar` ou `pacmania`."

    ./build/zb2_bateria /tmp/aud/corpus_pacmania.json "$mods" /tmp/aud/bat_pacmania.json
    pacmania  148400  sim sim sim   3555  8559   0   0   1
    ./build/zb2_bateria /tmp/aud/corpus_zumar.json "$mods" /tmp/aud/bat_zumar.json
    zumar     620848  sim sim sim    668   276   0   0   1
    (nos 62 titulos: PIXELS 0 em 62 de 62, CORES maximo 1)

E, com o laco de quadro ligado (o que a bateria por omissao NAO corre):

    ZB2_QUADROS=60 ...  zumar: 0 pixels;  pacmania: 0 pixels

**Criterio NAO cumprido: nenhum pixel, uma so cor.** O rasterizador existe e
funciona na bancada (`zb2_sonda_gl`: 3 vertices -> 38 400 pixels); o que nao
acontece e um titulo a chegar la. Veredicto: **NAO CUMPRIDA**.

### Etapa 4 -- ficheiros, VFS e os 62 titulos a correr

Entregaveis: `core/brew/vfs.{h,cpp}`, `core/brew/arquivo.{h,cpp}`,
`core/brew/formato.{h,cpp}`, `tools/bateria.cpp`. Do plano falta
`core/carga/ggz` e `core/carga/pakz` -- `core/carga/` tem `mod` e `bar`, e mais
nada (`ls core/carga`). 5 titulos do corpus tem `assets: ggz` e varios `pakz`.
**Entregavel: PARCIAL.**

Criterio: "os 62 titulos arrancam e a bateria regista um estado por titulo".

    ./build/zb2_bateria "$corpus" "$mods" /tmp/wt-auditoria-testes/bateria_atual.json
    == 62 titulos | carga 62 | ponteiro de modulo 62 | applet 37 ==
    (o JSON tem 62 fichas, cada uma com motivo, passos de cada fase, pixels e faltas)

**Criterio cumprido** (o criterio diz explicitamente que nao precisam de
desenhar). A versao honesta do numero de `applet` esta discutida na seccao de
contradicoes. Veredicto: **CUMPRIDA**.

### Etapa 5 -- midia e audio

Entregaveis: `core/brew/imedia.{h,cpp}` (927 linhas),
`core/audio/misturador.{h,cpp}`, `tests/media_test.cpp` (36),
`tests/audio_test.cpp` (10), `tools/sonda_media.cpp`.

Criterio: "um titulo toca som e o misturador reporta amostras nao nulas. Titulo:
`a3d`".

    ./build/zb2_bateria /tmp/aud/corpus_a3d.json "$mods" /tmp/aud/bat_a3d.json
    a3d  511088  sim sim sim  4000000  133967  0  0  1
        orcamento_esgotado | create:parou_em_slot_nao_implementado | start:retornou
    ZB2_QUADROS=60:  ... start:retornou | quadro:orcamento_esgotado   (0 pixels)

A bateria **nao tem coluna de audio** -- nao existe, hoje, um numero de som por
titulo. E a lista de demanda nao tem um unico CLSID de midia: as 16 recusas de
`CreateInstance` sao THREAD (9), PNGDECODER_BREW (3), 0x0103d8ec (3) e
0x01011810 (1). **O `a3d` nao chega a pedir midia nas fases que a bateria corre.**

O que EXISTE e uma bancada que prova a camada:

    ./build/zb2_sonda_media
    MOTOR: VERDE -- o motor entrega o IMedia (0x80090000) E o aviso de fim
    MODULO: misturador: recebidas=22050 nao_nulas=11025 pico=2400 blocos=23
    RESUMO: VERDE -- o motor entrega o IMedia e o modulo toca o ciclo de vida.

A bancada e escrita pelo proprio emulador (um pedido de midia em codigo ARM
montado no teste), e nao por um titulo. Veredicto: **NAO CUMPRIDA** (o criterio
nomeia um TITULO); a camada esta provada na bancada.

### Etapa 6 -- GL e as duas familias 3D

Entregaveis: `core/brew/igl.cpp` (1010), `core/brew/egl.cpp` (789),
`core/video/rasterizador.{h,cpp}`, `tests/igl_test.cpp` (38),
`tests/egl_test.cpp` (33), `tools/sonda_gl.cpp`. Existe tudo.

Criterio: "um titulo 3D desenha geometria com textura. Titulo: `ddragonz` ou
`cnk2`".

    ./build/zb2_bateria /tmp/aud/corpus_ddragonz.json "$mods" /tmp/aud/bat_ddragonz.json
    ddragonz  462748  sim sim sim  69  177  0 0 1   create:saiu_do_modulo_para_0xe3a03000 | start:retornou
    ZB2_QUADROS=60: ... QUADROS 1  PIXELS 0
    (o `cnk2` na corrida dos 62: 39 passos no create e saiu do modulo)

A cablagem CHEGA: `./build/zb2_sonda_gl <mods>/274754/ddragonz.mod` da
"thunks do wrapper: 41 | chegaram ao modulo: 41 | engolidos: 0", e a bancada
pede um triangulo a serio e obtem pixels. O que nao acontece e o guest desenhar.
Veredicto: **NAO CUMPRIDA**.

### Etapa 7 -- a tabela de interfaces verificada contra o SDK

Entregaveis: `tools/gerar_slots.py`, `tools/gerar_clsids.py`,
`tools/nomear_ajudantes.py`, os `.inc` gerados (`brew_slots`, `gl_slots`,
`clsids`, `ajudantes_slots`) e as quatro guardas `tools/verificar_*.sh`.

Criterio: "as tabelas do `AEEHelperFuncs` (117), do `IGL`, do `IGLES11` e do
`IHIDDevice` conferem com o SDK, com zero divergencias".

    ZB2_SDK_DIR="<SDK>" ./tools/verificar_slots.sh       -> slots=0
    ZB2_SDK_DIR="<SDK>" ./tools/verificar_slots_gl.sh    -> slots_gl=0
    ZB2_SDK_DIR="<SDK>" ./tools/verificar_clsids.sh      -> clsids=0
    ZB2_SDK_DIR="<SDK>" ./tools/verificar_ajudantes.sh   -> ajudantes=0
    (as quatro dizem "OK: ... corresponde aos cabecalhos")

O `verificar_clsids.sh` imprime ainda, honestamente, 10 AVISOS (nomes declarados
com textos diferentes em dois cabecalhos, fica a primeira declaracao) e 8 nomes
NAO resolvidos -- sao avisos, e nao divergencias, mas ficam registados.

O criterio pede tambem "um teste que falha se uma tabela instalada tiver slot por
preencher". Provado por violacao: instalar passando a aceitar slots em falta ->
VERMELHO em `Ajudantes.RecusaInstalarComSlotPorImplementar` (mutante 42).
Veredicto: **CUMPRIDA**, com a ressalva do paragrafo seguinte.

**E a guarda que devia detetar uma cablagem perdida nao a deteta.**
`Interface.CablarDetetaUmaCablagemPerdida` diz no proprio comentario "ESTE E O
TESTE QUE FALTAVA ... a perda e detetada em microssegundos". Arrancando a leitura
de volta (`core/brew/interface.cpp:67`, `if (lido != saidas.Endereco(l.saida))`)
o teste **continua VERDE** (mutante 43): o que ele verifica e que a cablagem e
REESCRITA (`EXPECT_TRUE(r.ok)` + o valor no slot), e nao que a perda e DETETADA.
Ver a Parte B.

### Etapa 8 -- entrada

Entregaveis: `core/brew/ihiddevice.{h,cpp}` (642),
`core/brew/ihid_entrada.{h,cpp}` (407), `tests/hid_test.cpp` (22),
`tests/entrada_despacho_test.cpp` (7).

Criterio: "o d-pad responde. O formato do valor ainda esta por determinar -- a
medicao que falta esta descrita em `docs/PAREAMENTO-DE-UIDS-MEDIDO.md`. Titulo:
`tectoy`".

O que esta medido e provado: as tabelas de botoes, eixos e slots contra o
cabecalho e o ficheiro do console (`Hid.TabelaDe*`); o guiao que recusa inteiro
(VERMELHO com a guarda do `nState` e a da ordem arrancadas -- mutantes 29, 30);
o botao desconhecido a recusar com `E_NO_SUCH` (mutante 34).

O que NAO esta medido: **nenhum titulo consome a entrada**. Medicao:

    ZB2_ENTRADA="0 eixo 0x0106c4d0 0;100 eixo 0x0106c4d0 200;200 botao 0x0106c3fe 1;300 botao 0x0106c3fe 0" \
        ./build/zb2_bateria /tmp/aud/corpus_tectoy.json "$mods" /tmp/aud/bat_tectoy_guiao.json
    tectoy  590048  sim sim sim  400  255  0  0  1  retornou | create:retornou | start:retornou

comparado com a mesma corrida sem guiao: **todos os campos medidos sao iguais**
(passos, quadros, pixels, cores, motivo, faltas). O guiao e ACEITE (nao ha
`ZB2_ENTRADA` nas faltas e o `ENTRADA NAO INSTALADA` nao aparece) e nao muda nada.
Veredicto: **PARCIAL** -- o mecanismo existe e esta testado; "o d-pad responde"
nao tem, hoje, um numero por titulo.

### Etapa 9 -- regressoes automaticas

Entregaveis: `tools/bateria.cpp`, `tools/comparar.{h,cpp}`,
`tools/comparar_main.cpp`, `tools/regressao.sh`,
`tools/baseline/bateria.json` (versionado) e `tests/comparar_test.cpp` (29).

Criterio: "mudar o emulador de forma a piorar um titulo **falha** a bateria, com o
numero que piorou".

Estado limpo:

    ctest --test-dir build  ->  `regressao` PASSED (50,93 s); 8/8
    ./tools/regressao.sh    ->  comparador: 0 regressoes
    (conferido por mim, ficha a ficha: 0 diferencas de modulo/applet/pixels/cores
     entre a corrida de agora e `tools/baseline/bateria.json`)

Violacao deliberada, com o comando e os numeros:

    python3 -c "..."   # core/brew/despacho.cpp: kIidDisplay 0x01001001 -> 0x01003003
    cmake --build build -j4 && ./tools/regressao.sh
    -> == 62 titulos | carga 62 | ponteiro de modulo 62 | applet 1 ==
    -> == degraus medidos (referencia -> corrida): applet 37 -> 1
    -> REGRESSOES (36)  por campo: applet 36:  a3d (274259): applet true -> false ...
    -> == regressao: FALHA -- um titulo piorou. O numero esta acima.
    -> codigo de saida 1

    git checkout -- core/brew/despacho.cpp && cmake --build build -j4 && ./tools/regressao.sh
    -> == regressao: SEM REGRESSOES      (codigo 0)

O `tools/regressao.sh` nomeia o titulo E o campo que piorou, e recusa-se a
comparar corridas de corpus diferente. Veredicto: **CUMPRIDA**, e provada por
violacao (nao e um teste que passa: e o juiz a falhar quando devia).

---

## PARTE B -- A HONESTIDADE DOS TESTES

### Inventario

| o que | numero | como |
|---|---|---|
| macros `TEST`/`TEST_F` nos ficheiros | **377** | `grep -c '\bTEST(_F)?('` nos 17 ficheiros de `tests/` |
| testes COMPILADOS e corridos | **376** | `./build/zb2_tests` -> "376 tests from 35 test suites ran" |
| testes que passaram | **376** | "376 PASSED" (3 corridas: antes, no meio e depois do rebuild limpo) |
| testes SALTADOS nesta maquina | **0** | o log nao tem uma unica linha `[  SKIPPED  ]` |
| assercoes `EXPECT_*`/`ASSERT_*` | **1540** | analisador proprio sobre os 17 ficheiros |
| sitios `GTEST_SKIP` | **8** | ver abaixo |
| mutantes aplicados (violacao deliberada) | **45** | `tools/auditoria/runner_mutantes.py` |

### As sete categorias que a tarefa pede, uma a uma

**(1) Assercao tautologica -- NAO ENCONTRADA.** Analisadas as 1540 assercoes: zero
com os dois lados textualmente iguais depois de normalizar espacos
(`EXPECT_EQ(x, x)`), zero `EXPECT_TRUE(true)`/`EXPECT_TRUE(1)`, zero
`EXPECT_GE(n, 0)` com `n` sem sinal (as 17 ocorrencias de `EXPECT_GE`/`EXPECT_GT`
tem todas o lado direito a `1u` ou a `0` com lado esquerdo **com** sinal).

**(2) Assercao generica de mais -- ENCONTRADAS TRES.** O metodo nao foi a leitura:
foi substituir o diagnostico pelo diagnostico ERRADO (o vizinho) e ver se o teste
continua verde. Tambem medi, para cada `find("...")` dos testes (53 no total),
quantos literais DISTINTOS da arvore contem a mesma agulha:

| teste | agulha | literais distintos que a contem | prova |
|---|---|---|---|
| `BlobSintetico.RecusaMimeSemTerminador` | `"mime"` | **3** (`mime vazio`, `mime sem terminador NUL dentro dos`, `incompativel com o mime`) | mutante 20: a recusa do "sem terminador" passa a reportar `"mime vazio"` (diagnostico FALSO) e o teste fica **VERDE** |
| `Media.UmCallbackQueNaoVoltaERegistadoENaoDaSucesso` | `"nao voltou"` | 2 | mutante 37: a mensagem reformatada para `"callback nao voltou: 0x... passos=N"` -- o teste fica **VERDE** (a agulha nao fixa nada) |
| `Hid.GuiaoInvalidoEhRecusadoInteiro` | `"fora de"` / `"desconhecido"` | 6 / **26** | aqui a agulha e generica mas o teste TEM dentes: `EXPECT_FALSE` + a agulha do vizinho sao incompatíveis. Mutantes 29/30 arrancam as guardas do `nState` e da ordem -> **VERMELHO** |

Nota: `find("mime")` e o caso grave, porque a agulha e satisfeita por tres
diagnosticos diferentes e o teste **nomeia** um deles. E a terceira vez que esta
classe de erro aparece nesta arvore (o ledger registra duas).

**(3) Teste que passa com o codigo apagado -- ENCONTRADOS CINCO.**

| teste | codigo arrancado | resultado |
|---|---|---|
| `Interface.CablarDetetaUmaCablagemPerdida` | a leitura de volta da cablagem (`core/brew/interface.cpp:67`) | **VERDE** (mutante 43). O teste verifica que a cablagem e REESCRITA, nao que a perda e DETETADA -- apesar do nome e do comentario "ESTE E O TESTE QUE FALTAVA" |
| todos os `Cpu.*` (42 testes) | o `cpsr` do CONSTRUTOR (`arm_interpreter.cpp:79`) | **VERDE** (mutantes 9 e 39). So o `Repor` esta coberto |
| todos os `Rasterizador.*` (17 testes) | o limite por pixel da janela (`rasterizador.cpp:341`) | **VERDE** (mutante 17) |
| todos os `Formato.*` (6 testes) | o `%x` sem largura (`formato.cpp:80`), que passa a `%d` | **VERDE** (mutante 40) |
| todos os `Hid.*` (22 testes) | a recusa de um handle de dispositivo desconhecido (`ihiddevice.cpp:157`) | **VERDE** (mutantes 44 e 45) |

**(4) Teste que codifica comportamento antigo -- NAO ENCONTRADO.** Nao tenho, nesta
auditoria, uma verificacao externa do emulador (um `qemu`, um `gdb`, um titulo
gravado) contra a qual dizer "o teste esta errado e o emulador certo". O que
encontrei foi o oposto, duas vezes, e ambas a favor do emulador: o comentario do
`Cpu.MrsDevolveOCpsr...` regista uma expectativa contraditoria que foi corrigida a
favor do hardware, e o `AEEIShell.h` deu razao ao `pacmania` (o jogo estava certo).
Declaro esta categoria **nao auditada**, e nao "limpa".

**(5) `GTEST_SKIP` que salta sempre.** 8 sitios de `GTEST_SKIP`; **7 compilados**
(um esta no ramo `#else` de `egl_test.cpp:692` e **nunca e compilado**: e o
`CablagemGl.ORemendoNaoEstaAplicado`, que existe so para descrever o estado sem a
cablagem -- e o 377.o macro). Dos 7 compilados, **0 saltaram nesta maquina**: as
condicoes sao a presenca de `pacmania.bar` (2), do `aeecontrols.bar` do SDK (1),
do corpus (2) e do `pacmania.mod` (2). Numa maquina sem a midia do corpus, **7
testes passam a nao correr** -- e o `ctest` mostra-los-ia a verde.

E o mesmo vale para as guardas de construcao: `slots_do_sdk`, `slots_do_sdk_gl`,
`clsids_do_sdk`, `slots_do_sdk_ajudantes` e `regressao` usam
`SKIP_RETURN_CODE 77`, ou seja, **um `ctest` "8/8 verde" pode ser 3/8** se faltar
o SDK ou a midia. Nesta maquina correm todos (o `regressao` demora 50,9 s).

**(6) Teste que compara com a propria implementacao -- NAO ENCONTRADO por analise
estatica**: zero assercoes com a MESMA funcao dos dois lados. E ha um bom exemplo
a seguir em `Formato.*`, que compara com o `snprintf` DO SISTEMA (e o
`BarDeVerdade.AeeControlsDoSDKConfereComOCabecalhoDoVendedor`, que abre o `.bar`
do SDK). O pior caso que encontrei e o oposto e esta declarado: o `mod_base_test`
prova que a base e zero com os literais do `pacmania.mod` REAL, e nao com um
fixture sintetico.

**(7) Teste que nunca corre por nome errado -- verificado:** os 376 testes
compilados correspondem a 376 dos 377 macros; a diferenca e o `#else` acima. Nao
ha teste declarado e nao listado.

### A provacao por violacao, um a um (45 mutantes)

Reproduzir: `python3 tools/auditoria/runner_mutantes.py --raiz <curupira> --build build
--mutantes tools/auditoria/mutantes.json --ids 1,2,... --saida /tmp/res.jsonl`
(a lista completa esta em `tools/auditoria/mutantes.json`; o resultado cru, com o
teste que ficou vermelho em cada um, em `tools/auditoria/resultados.jsonl`).

`esperado` e o que eu previ ANTES de correr; `obtido` e o que saiu; um `obtido`
diferente do `esperado` e um achado, e nao um erro a esconder.

| # | ficheiro | o que a mudanca quebra | filtro | esperado | obtido | teste que ficou vermelho |
|---|---|---|---|---|---|---|

As linhas da tabela estao em `tools/auditoria/mutantes-resultado.md`, gerada do resultado cru (`tools/auditoria/resultados.jsonl`). O resumo:

| resultado | quantos |
|---|---|
| mutante aplicado e teste **VERMELHO** (a guarda/teste tem dentes) | **32** |
| mutante aplicado e teste **VERDE** (achado: nao distingue) | **13** |
| destes, **equivalentes** (a mudanca nao muda o comportamento sob a configuracao do teste) | 1 (o 19; corrigido pelo 35, que ficou VERMELHO) |
| testes DISTINTOS provados vermelhos por violacao | **36** |

Os 32 VERMELHOS cobrem: os 4 guardrails do `Traco`; os 4 da `Memoria` (vigia por
endereco, faixa, escritor unico, cadeia com limite); o `ModoValido`, o `SetCpsr`
que recusa e o `MLA`; a tabela de ajudantes em `base-4` e o ponto de entrada do
loader; a inversao do y, a regra do canto, o teste de profundidade e o descarte de
faces do rasterizador; o `mime` vazio do `.bar`; o desenho sem tela do IGL (e o
tamanho do vertice); o versionamento do contexto EGL e o valor servido pelo
`GetConfigAttrib`; a escala do volume no misturador; o `nState` e a ordem do
guiao; o botao desconhecido do HID; o nome do metodo na recusa das classes; o
campo desconhecido do comparador; a instalacao de uma tabela com slot por
preencher; a recusa do slot da IBase; e a assinatura do `SetTimer` (a duracao no
r1 e a funcao no r2, nos dois sentidos).

### O que CONTRADIZ o que se pensava

**(1) A demanda nº1 nao existe mais: `rasterizador_de_GL` = 0.** O pedido que
abria esta tarefa ("rasterizador_de_GL pedido 124x, a parede que resta") **nao
aparece** na corrida de `0286921`: `grep rasterizador_de_GL` no log da bateria da
zero ocorrencias. A razao esta no git: o rasterizador foi cablado em `ed258de` e
`a0c444b`, e a `core/brew/egl.cpp` passou a registar a falta como evento de
informacao (era verdade e ia passar a mentira, P7). A demanda de topo agora e
`AEEHelperFuncs[0x140] vsnprintf` (116x), seguida de `IShell::slot41` (21x) e das
16 recusas de `CreateInstance`. **Trabalhar a lista antiga seria trabalhar no que
ja esta feito.**

**(2) Os numeros do estado actual estavam velhos.** O pedido dizia "commit
`ecc97b0`, 335 testes, ctest 7/7, applet 41". Medido em `0286921` (9 commits a
frente): **376 testes** (nao 335 nem os 376 "declarados" -- sao 377 macros, 376
compilados), **ctest 8/8** (nao 7), e **applet 37**. E `EVT_APP_START` chega aos
mesmos 37 titulos (37 fichas com `passos_start > 0`).

**(3) O `applet` honesto e 37, e nao 22 -- mas a inflacao existe, e vale 51.**
O pedido dizia que "o `applet 41` estava inflado, e o numero honesto era 22". A
direcao esta certa e a magnitude nao se reproduz. Medido, removendo a limpeza do
slot de saida (`mem.Escrever32(kPPObj, 0)` antes da fase `create`,
`tools/bateria.cpp:683`, que **entrou em `0286921`**):

    COM a limpeza (HEAD, 0286921):  == 62 titulos | ... | applet 37 ==
    SEM a limpeza (mutante local):  == 62 titulos | ... | applet 51 ==
    14 titulos mudam de applet com a limpeza (cnk2, cninja, game, darkseal, ...)

O numero honesto deste commit e **37**; o inflado seria **51**; o "22" do pedido e
de uma base anterior a duas mudancas (o anel da pilha e a limpeza do slot). E o numero
REAGE ao emulador como deve: arrancando o `IShell::QueryInterface` do
`AEECLSID_DISPLAY` (o `kIidDisplay` a `0x01003003`, o erro que o ledger registra),
a bateria vai a **1** e o comparador nomeia 36 regressoes. Ou seja, o `applet` mede
uma coisa real. O "22" do pedido e de uma base anterior a duas mudancas (o anel da
pilha e a limpeza do slot), e nao o valor honesto deste commit.

**(4) A referencia da bateria carrega um rotulo que a contradiz, e isto esta
provado.** O cabecalho de `tools/baseline/bateria.json` diz `build=eb62459`, e os
dados sao **identicos** aos da minha corrida em `0286921`, ficha a ficha (0
diferencas de modulo, applet, pixels e cores nas 62). So que:

    git show eb62459:curupira/tools/bateria.cpp | grep -c "mem.Escrever32(kPPObj, 0);"  -> 0
    git show 0286921:curupira/tools/bateria.cpp | grep -c "mem.Escrever32(kPPObj, 0);"  -> 1
    git log -1 --format=%h -- curupira/tools/baseline/bateria.json                      -> 0286921

Com o codigo de `eb62459` (sem a limpeza) o numero seria 51; a referencia diz 37.
**A corrida de referencia foi feita com a arvore suja** (a mudanca aplicada, o
commit ainda no pai) e o campo `build` guardou o commit do checkout, que nao era o
codigo medido. O comparador nao pode apanhar isto -- `Comparar.BuildDiferenteNaoFalha`,
campo NEUTRO por desenho -- e por isso `build` **nao serve como proveniencia**.
A propria corrida restaurada o mostra: "NEUTROS QUE MUDARAM (1): cabecalho.build
eb62459 -> 0286921". Falta um `sha256` dos FONTES medidos (dos dois lados).

**(5) Dois instrumentos, o mesmo titulo, respostas opostas (o applet do
`imicro3d`).** Ver a etapa 2: a sonda diz `0x00000000`, a bateria diz nao nulo. As
convencoes de chamada sao diferentes e **nada as obriga a concordar**.

**(6) Um teste cujo nome e comentario afirmam uma coisa que ele nao faz:**
`Interface.CablarDetetaUmaCablagemPerdida` nao deteta nada (mutante 43).

**(7) O instrumento desta auditoria tambem mentiu, e eu registo-o.**
`tools/auditoria/runner_mutantes.py` restaura os FONTES em cada mutante, mas **nao reconstroi
depois de restaurar**: ao fim do primeiro lote, o `build/zb2_tests` em disco ainda
continha o mutante 34. O `ctest` que corri a seguir deu `fundacao ... ***Failed`
-- e o defeito era do MEU instrumento, nao da arvore. Depois do rebuild limpo:
376/376 e 8/8. **Restaurar um ficheiro nao e restaurar o binario.**

### A contagem final pedida

| pergunta | resposta |
|---|---|
| quantos testes foram verificados por violacao | **36 testes distintos** (45 mutantes: 32 vermelhos, 1 equivalente, 12 verdes) |
| quantos sao considerados FRACOS | **5** provados por mutante que fica verde (`Interface.CablarDetetaUmaCablagemPerdida`, o `cpsr` do construtor da CPU, o limite por pixel do rasterizador, o `%x` sem largura do `Formato`, o handle desconhecido do HID) + **3** com assercao de mensagem generica (o `mime`, o `nao voltou`, e -- em menor grau -- o `desconhecido` do guiao) = **8** |
| quantos saltam sempre | **0** (8 sitios de `GTEST_SKIP`, dos quais 1 nunca e compilado; os outros 7 correram nesta maquina) -- mas **5 testes do `ctest` podem sair 77/"saltado"** sem SDK nem midia, e um `ctest` verde esconde isso |
| testes que nao conseguem falhar por nao terem assercao | **0** (todos os 376 tem pelo menos uma) |
| assercoes tautologicas | **0** |

### Defeitos encontrados no EMULADOR (registados, NAO corrigidos)

1. `tools/sonda_mod` chama `CreateInstance` com a convencao antiga (3 argumentos)
   e imprime `applet = 0` para o `imicro3d`. **Instrumento**, nao emulador -- mas
   imprime um numero que a bateria contradiz, e o ledger cita-o.
2. `core/brew/interface.cpp:64-71`: a leitura de volta nao tem teste (e, com esta
   `Memoria`, parece nao poder falhar).
3. `core/video/rasterizador.cpp:341`: o limite por pixel e redundante com o corte
   da caixa delimitadora (408-415) para a configuracao testada; nenhum teste o
   distingue.
4. `tools/baseline/bateria.json`: proveniencia nao verificada (o `build` mente).
5. `core/brew/imedia.cpp:529-537`: o contador `nao_nulas` (que vai para o traco)
   nao e afirmado por nenhum teste.
6. Nenhum titulo chega ao desenho (`PIXELS 0` em 62/62) nem a entrada (o guiao do
   d-pad nao muda um unico campo do `tectoy`). E a parede real deste commit, e
   esta fora do ambito desta tarefa (ha outra frente para isso).

### Ficheiros partilhados que eu precisaria de tocar

**Nenhum.** Esta auditoria nao alterou nenhum ficheiro de `core/`, de `tools/` nem
de `tests/`: todas as mudancas foram aplicadas, medidas e **desfeitas** com
`git checkout` (o `sha256` de cada ficheiro foi conferido no fim de cada mutante).
Os unicos ficheiros novos sao este documento e a tabela de mutantes, em
`curupira/docs/rewrite/`.

Para quem for corrigir os achados, o que eu **pediria** (sem o fazer):

| ficheiro | o que falta | porque |
|---|---|---|
| `tests/brew_test.cpp` | `CablarDetetaUmaCablagemPerdida` tem de arrancar a leitura de volta para o teste ter dentes (ou mudar de nome) | hoje passa com o codigo apagado |
| `core/brew/interface.cpp` | decidir se a leitura de volta fica (com teste) ou sai | codigo sem teste |
| `tools/sonda_mod.cpp` | usar a convencao de 4 argumentos, como a bateria mediu | dois instrumentos a discordar |
| `tests/cpu_test.cpp` | um teste do `cpsr` do CONSTRUTOR | o bug que custou uma investigacao inteira nao esta coberto no sitio onde acontecia |
| `tests/bar_test.cpp` | agulha especifica no `RecusaMimeSemTerminador` | a agulha `"mime"` apanha 3 diagnosticos |
| `tools/baseline/bateria.json` | um campo de proveniencia do CODIGO (sha256 dos fontes, e nao o commit do checkout) | o `build` de hoje mente |
| `tools/bateria.cpp` | uma coluna de AUDIO por titulo | a etapa 5 nao tem numero na bateria |

---

## ANEXO -- a tabela dos 45 mutantes

| 1 | `core/traco/traco.cpp` | etapa0: a recusa de caminho relativo (P7/regra 3) | `Traco.LogRelativo*` | VERMELHO | VERMELHO | Traco.LogRelativoERecusadoComMotivo |
| 2 | `core/traco/traco.cpp` | etapa0: recusa de comparar configuracoes diferentes | `Traco.CompararRecusa*` | VERMELHO | VERMELHO | Traco.CompararRecusaConfiguracoesDiferentes |
| 3 | `core/traco/traco.cpp` | P2: a falta fica contada | `Traco.NaoImplementadoFicaContadoENomeado` | VERMELHO | VERMELHO | Traco.NaoImplementadoFicaContadoENomeado |
| 4 | `core/traco/traco.cpp` | regra 4: marcas distintas, nao linhas | `Traco.MarcasDeDepuracao*` | VERMELHO | VERMELHO | Traco.MarcasDeDepuracaoSaoListaveisParaLimpeza |
| 5 | `core/memoria/memoria.cpp` | P6: uma vez por endereco | `Memoria.VigiaRegistaOPrimeiroEscritor*` | VERMELHO | VERMELHO | Memoria.VigiaRegistaOPrimeiroEscritorDeCadaEndereco |
| 6 | `core/memoria/memoria.cpp` | a faixa da vigia limita o registo | `Memoria.VigiaRegistaOPrimeiroEscritor*` | VERMELHO | VERMELHO | Memoria.VigiaRegistaOPrimeiroEscritorDeCadaEndereco |
| 7 | `core/memoria/memoria.cpp` | P6: escritas sem escritor declarado sao contadas | `Memoria.EscreverSemEscritorDeclaradoEsContado` | VERMELHO | VERMELHO | Memoria.EscreverSemEscritorDeclaradoEsContado |
| 8 | `core/memoria/memoria.cpp` | limite da cadeia terminada em NUL | `Memoria.CadeiaTerminadaEmNul*` | VERMELHO | VERMELHO | Memoria.CadeiaTerminadaEmNulRespeitaOLimite |
| 9 | `core/cpu/arm_interpreter.cpp` | etapa1: cpsr inicial e modo valido | `Cpu.AposRepor*,Cpu.MrsDevolve*` | VERMELHO | VERDE | - |
| 10 | `core/cpu/arm_interpreter.cpp` | etapa1: zero nao e modo nenhum | `Cpu.ZeroNaoEUmModoValido` | VERMELHO | VERMELHO | Cpu.ZeroNaoEUmModoValido |
| 11 | `core/cpu/arm_interpreter.cpp` | etapa1: modo invalido escrito e recusado e contado | `Cpu.EscreverUmModoInvalidoERecusado` | VERMELHO | VERMELHO | Cpu.EscreverUmModoInvalidoERecusado |
| 12 | `core/carga/mod.cpp` | etapa2: a tabela de ajudantes em base-4 (ROPI) | `Carga.OCarregadorEscreveATabelaEmBaseMenosQuatro` | VERMELHO | VERMELHO | Carga.OCarregadorEscreveATabelaEmBaseMenosQuatro |
| 13 | `core/carga/mod.cpp` | etapa2: o ponto de entrada | `Carga.OCarregadorEscreveATabelaEmBaseMenosQuatro` | VERMELHO | VERMELHO | Carga.OCarregadorEscreveATabelaEmBaseMenosQuatro |
| 14 | `core/cpu/arm_interpreter.cpp` | etapa1: a ordem do MLA (o bug real encontrado pelos testes) | `Cpu.MulEMla` | VERMELHO | VERMELHO | Cpu.MulEMla |
| 15 | `core/video/rasterizador.cpp` | etapa3: a inversao do y na projeccao | `Rasterizador.*` | VERMELHO | VERMELHO | Rasterizador.TrianguloConhecidoDaExactamenteSeisPixels, Rasterizador.ArestaPartilhadaNaoEscreveDuasVezesNemDeixaFenda |
| 16 | `core/video/rasterizador.cpp` | etapa3: a regra do canto (top-left) | `Rasterizador.*` | VERMELHO | VERMELHO | Rasterizador.ArestaPartilhadaNaoEscreveDuasVezesNemDeixaFenda, Rasterizador.ATexturaAmostraOCantoCerto |
| 17 | `core/video/rasterizador.cpp` | etapa3: o limite da janela no pixel | `Rasterizador.*` | VERMELHO | VERDE | - |
| 18 | `core/video/rasterizador.cpp` | etapa3: o teste de profundidade | `Rasterizador.*` | VERMELHO | VERMELHO | Rasterizador.OTesteDeProfundidadeDecideQuemFica |
| 19 | `core/video/rasterizador.cpp` | etapa3: o descarte de faces usa a orientacao | `Rasterizador.*` | VERMELHO | VERDE | - |
| 20 | `core/carga/bar.cpp` | FRENTE B: find("mime") apanha o diagnostico ERRADO -- assercao generica | `BlobSintetico.RecusaMimeSemTerminador` | VERDE | VERDE | - |
| 21 | `core/carga/bar.cpp` | FRENTE B: guarda da recusa arrancada e o teste continua verde (a outra recusa tem "mime") | `BlobSintetico.RecusaMimeSemTerminador` | VERDE | VERMELHO | BlobSintetico.RecusaMimeSemTerminador |
| 22 | `core/carga/bar.cpp` | guarda do mime vazio: aqui o teste TEM dentes | `BlobSintetico.RecusaMimeVazio` | VERMELHO | VERMELHO | BlobSintetico.RecusaMimeVazio |
| 23 | `core/brew/igl.cpp` | etapa6: o desenho sem tela recusa | `EstadoGl.DrawArraysSemTelaRecusaMasContaAGeometria` | VERMELHO | VERMELHO | EstadoGl.DrawArraysSemTelaRecusaMasContaAGeometria |
| 24 | `core/brew/igl.cpp` | etapa6: o tamanho do vertice 2..4 | `EstadoGl.*` | VERMELHO | VERMELHO | EstadoGl.ArraysDeVerticesGuardamOTipoEOPasso |
| 25 | `core/brew/egl.cpp` | IEGL: o versionamento 2 recusa | `CicloDoEgl.OCriaContextoRecusaOVersionamento2` | VERMELHO | VERMELHO | CicloDoEgl.OCriaContextoRecusaOVersionamento2 |
| 26 | `core/brew/egl.cpp` | IEGL: o valor servido vem do cabecalho | `CicloDoEgl.OGetConfigAttrib*` | VERMELHO | VERMELHO | CicloDoEgl.OGetConfigAttribServeOMedidoERecusaONaoMedido |
| 27 | `core/audio/misturador.cpp` | etapa5: a escala do volume | `Misturador.*` | VERMELHO | VERMELHO | Misturador.ContaAsAmostrasEAsNaoNulas, Misturador.VolumeMetadeBaixaOPicoAMetade |
| 28 | `core/brew/imedia.cpp` | etapa5: amostras nao nulas contadas | `Media.OMisturadorContaAsAmostrasQueAMidiaEntrega` | VERMELHO | VERDE | - |
| 29 | `core/brew/ihid_entrada.cpp` | etapa8: o nState 0/1 | `Hid.GuiaoInvalidoEhRecusadoInteiro` | VERMELHO | VERMELHO | Hid.GuiaoInvalidoEhRecusadoInteiro |
| 30 | `core/brew/ihid_entrada.cpp` | etapa8: o guiao fora de ordem | `Hid.GuiaoInvalidoEhRecusadoInteiro` | VERMELHO | VERMELHO | Hid.GuiaoInvalidoEhRecusadoInteiro |
| 31 | `core/brew/classes.cpp` | classes: o nome do metodo vem da tabela do cabecalho | `Classes.OMetodoNaoImplementadoRecusaComONomeDoMetodo` | VERMELHO | VERMELHO | Classes.OMetodoNaoImplementadoRecusaComONomeDoMetodo |
| 32 | `tools/comparar.cpp` | etapa9: campo desconhecido e recusado | `Comparar.CampoDesconhecidoERecusado` | VERMELHO | VERMELHO | Comparar.CampoDesconhecidoERecusado |
| 33 | `core/brew/formato.cpp` | formato: o hexadecimal do sistema | `Formato.HexadecimalComLargura` | VERMELHO | VERDE | - |
| 34 | `core/brew/ihiddevice.cpp` | etapa8: botao desconhecido recusa com E_ENOSUCH | `Hid.BotaoDesconhecidoRecusaComEnosuch` | VERMELHO | VERMELHO | Hid.BotaoDesconhecidoRecusaComEnosuch |
| 35 | `core/video/rasterizador.cpp` | etapa3: o descarte de faces (mutante nao-equivalente) | `Rasterizador.ODescarteDeFaces*` | VERMELHO | VERMELHO | Rasterizador.ODescarteDeFacesUsaAOrientacaoEmNDC |
| 36 | `core/brew/despacho.cpp` | etapa8/laco: a assinatura do SetTimer (duracao no r1, funcao no r2) | `Widget.OSetTimer*` | VERMELHO | VERMELHO | Widget.OSetTimerLeADuracaoDoR1EAFuncaoDoR2, Widget.OSetTimerComARelacaoTrocadaNaoArmaOLaco |
| 37 | `core/brew/imedia.cpp` | FRENTE B: find("nao voltou") nao fixa o formato -- assercao generica | `Media.UmCallbackQueNaoVolta*` | VERDE | VERDE | - |
| 38 | `core/brew/interface.cpp` | a IBase nao se cabla | `Interface.CablarRecusaOSlotDaIBase` | VERMELHO | VERMELHO | Interface.CablarRecusaOSlotDaIBase |
| 39 | `core/cpu/arm_interpreter.cpp` | FRENTE B: o cpsr do CONSTRUTOR (o bug do projeto antigo) pode ficar a zero -- nenhum teste apanha | `Cpu.*` | VERDE | VERDE | - |
| 40 | `core/brew/formato.cpp` | o caminho sem largura do %x | `Formato.*` | ? | VERDE | - |
| 41 | `core/brew/imedia.cpp` | o contador nao_nulas do imedia (so vai para o log) | `Media.*` | ? | VERDE | - |
| 42 | `core/brew/ajudantes.cpp` | etapa7: instalar uma tabela com slot por preencher RECUSA | `Ajudantes.RecusaInstalarComSlotPorImplementar` | VERMELHO | VERMELHO | Ajudantes.RecusaInstalarComSlotPorImplementar |
| 43 | `core/brew/interface.cpp` | etapa7: a leitura de volta da cablagem | `Interface.CablarDetetaUmaCablagemPerdida` | VERMELHO | VERDE | - |
| 44 | `core/brew/ihiddevice.cpp` | etapa8: handle de dispositivo desconhecido | `Hid.OIhidEntregaUmDispositivo*` | ? | VERDE | - |
| 45 | `core/brew/ihiddevice.cpp` | etapa8: handle desconhecido (filtro largo) | `Hid.*` | ? | VERDE | - |
