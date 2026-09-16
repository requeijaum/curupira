# Curupira -- plano incremental

Regra de cada etapa: um entregavel **testavel em isolamento**, um criterio de
aceitacao que corre num comando, e um titulo do corpus que a prova.

Ordem escolhida por uma razao: **cada etapa so depende das anteriores**, e cada
uma acrescenta um titulo novo a lista dos que sobem.

> **Revisto a 15/09**, depois de uma ronda de cinco frentes em paralelo. O que
> mudou nao foi so a percentagem: mudou a ORDEM. Tres etapas dadas como fechadas
> tinham defeitos, e a etapa que parecia melhor (a 3) e a que esta pior contra o
> seu proprio criterio. As percentagens abaixo sao medidas, nao estimadas.

## Estado medido (corpus de 62, `build/zb2_bateria`)

| degrau | inicio | 15/09 (1a metade) | **15/09 (2a metade)** |
|---|---|---|---|
| carga | 62 | 62 | **62/62** |
| ponteiro de modulo | 62 | 62 | **62/62** |
| vtable valida | 48 | 62 | **62/62** |
| applet instanciado | 37 | 58 | **62/62** |
| ciclo completo (create+start retornam) | 4 | 30 | **28/62** |
| sem faltas nenhumas | 0 | 34 | **42/62** |
| pixels escritos | 0 | 11 | **18/62** |
| **cores > 10 (criterio da etapa 3)** | 0 | 0 | **1/62** (tekken2, 31 cores) |
| total de pixels | 0 | ~3 M | **1 642 418 175** |

**Todos os 62 títulos instanciam.** O tecto do degrau seguinte mudou de sítio: 30 títulos
passam o ciclo sem UMA falta e sem um pixel (esperam entrada ou a thread), e 16 queimam o
tecto de 8 M passos (a maior parte por caminhadas de memória sem chamada de API).

| # | etapa | estado | % |
|---|---|---|---|
| 0 | fundacao de medicao | FECHADA | 100% |
| 1 | CPU de referencia | reaberta DUAS vezes | 95% |
| 2 | carregador MOD + 1o titulo | FECHADA | 100% |
| 3 | IShell / IDisplay / desenho 2D | **PARCIAL** | **45%** |
| 4 | ficheiros, VFS, 62 a correr | FECHADA | 100% |
| 5 | midia e audio | PARCIAL | 50% |
| 6 | **GL e as duas familias 3D** | **PARCIAL** | **40%** |
| 7 | tabelas verificadas vs SDK | PARCIAL | 85% |
| 8 | entrada | PARCIAL | 60% |
| 9 | regressoes automaticas | FECHADA | 100% |
| 10 | o instrumento mede menos do que acontece | **PARCIAL** | 80% |
| 11 | formatos que faltam | por comecar | 0% |
| 12 | Crazyball Engine | por comecar | 0% |
| 13 | IUnzipAStream (NOVA) | por comecar | 0% |

**Media: 68%** (baixou por se terem acrescentado tres etapas novas, e nao por
regressao -- os degraus todos subiram ou ficaram). A media continua a enganar: o
criterio da etapa 3 (**mais de 10 cores**) esta em **0 de 62**, e e esse que diz
se o emulador desenha ou nao.

---

## O que a ronda de 15/09 ensinou, e que muda o plano

1. **"FECHADA" nao e para sempre.** A etapa 1 estava fechada com 47 testes verdes
   e tinha ONZE defeitos, dois deles a bloquear uma familia inteira de dez
   titulos. Uma etapa fecha contra o criterio que tinha na altura, e o criterio
   envelhece.
2. **O teste pode copiar o erro do emulador.** O montador de instrucoes dos
   testes lia o campo do `MLA` da mesma forma errada que o interpretador: os dois
   concordavam e discordavam do processador. Um teste que copia a leitura do
   codigo defende o defeito em vez de o apanhar.
3. **Numero de execucoes nao prediz efeito.** O `MLA` corrigido tem 15 585
   execucoes no corpus e nao moveu um titulo; o `RRX`, com OITO, e o que mais
   rasto move.
4. **Ler a referencia ANTES de propor.** Tres defeitos desta ronda so
   apareceram por comparacao com o zeebx/zeemu/zeebulator e com o SDK. Num deles
   (`SetColor`) a correccao que eu ia fazer estava certa e aplicada ao argumento
   errado.
5. **A coluna que se festeja pode nao medir o que se pensa.** Os "640 pixels" que
   tiraram a coluna PIXELS do zero nao eram desenho: eram um `DrawText` com
   `nChars = -1` lido como `uint32`, cortado pelo clip na largura do ecra. O
   texto era "InitGLSurface failed".

---

## Etapa 0 -- A fundacao de medicao (sem emulacao) -- FECHADA

Entregavel: `core/tempo/`, `core/traco/`, `core/memoria/` e os testes.
Criterio: `ctest` verde; dois registadores com o mesmo argumento dao a mesma
linha; escrever num endereco vigiado dispara o aviso uma vez por escritor.

## Etapa 1 -- CPU de referencia -- REABERTA E FECHADA (95%)

Entregavel: `core/cpu/ArmInterpreter`, ARM e Thumb.

**Reaberta a 15/09 e fechada outra vez.** Onze defeitos, dez deles SILENCIOSOS
(`recusadas = 0`, nada nas faltas):
- `LDR PC,[Rn,#imm]` com o despacho a escrever `pc+4` por cima (5240 ocorrencias
  so no `karnovr.mod`) e C/V escritos sem o bit S -- estes dois valeram
  **15 titulos** de applet;
- mais nove: MLA com a parcela lida como factor, RRX como identidade, offset de
  registador sem as regras da quantidade 0, `STM{..,pc}` a gravar `pc` em vez de
  `pc+8`, `LDM{..,pc}` a ignorar o bit Thumb, LDM com a base na lista, SBC/RSC a
  dar a volta, Thumb formato 11 lido como formato 9, e `blx <reg>` com a mascara
  a exigir o bit que o define.

Fica a 95% e nao a 100% por causa do ponto 2 acima: o instrumento que a fechou
tinha o mesmo defeito que o codigo.

## Etapa 2 -- Carregador de MOD e o primeiro titulo -- FECHADA

62/62 carregam. O `imicro3d` faz o ciclo completo.

## Etapa 3 -- IShell, IDisplay e desenho 2D -- PARCIAL (45%)

Criterio: **um titulo 2D desenha e o ecra tem mais de 10 cores.**

**MEDIDO: 0 de 62 tem mais de 10 cores.** Ha 10 titulos com pixels, e sao todos
ecra de erro (limpeza + barra de texto). O criterio da etapa esta INTACTO.

Feito nesta ronda, e nenhum move o criterio:
- `SetColor` tinha tres defeitos sobrepostos (lia a cor do `r1`, que e o ITEM;
  truncava o RGBVAL com `& 0xFFFF`, fazendo branco e vermelho darem o mesmo
  pixel; devolvia 0 em vez da cor anterior, e o idioma do SDK e repor o retorno);
- `GetClipRect` escrevia 16 bytes num `AEERect` de 8, na pilha do guest;
- `DrawRect(NULL, IDF_RECT_FILL)` limpa o ecra, e era deitado fora por um
  `if (prc != 0)` -- era TODA a limpeza de ecra do corpus;
- `DrawText` com `nChars = -1` conta a string;
- a vtable do `IBitmap` tinha os slots da IBase a ZERO e o guest fazia `blx 0`;
- `IBitmap::GetInfo` (slot 12) passou a responder;
- o `IDIB` era uma struct inventada, e passou a ser a do `AEEIDIB.h`.

Falta: um titulo que desenhe DE VERDADE. O caminho medido esta na etapa 6.

## Etapa 4 -- Ficheiros, VFS e os 62 titulos a correr -- FECHADA

62/62 com estado medido. PACK (`.pkg`), INFLATE e a VFS a servir os pacotes.

## Etapa 5 -- Midia e audio -- PARCIAL (50%)

`IMedia` instalado com 14 slots; os avisos por serie nao entregam a endereco
reciclado. **O criterio (`a3d` toca som, misturador com amostras nao nulas) nao
foi medido nesta ronda** -- fica por provar.

## Etapa 6 -- GL e as duas familias 3D -- PARCIAL (40%) -- **O GARGALO**

Entregavel: `core/brew/igl`, `core/video/`.
Criterio: um titulo 3D desenha geometria com textura.

Feito: `IGL` (80 slots), `IEGL` (28), `QEGL` a delegar, rasterizador com
TRIANGLES/STRIP/FAN, interpolacao corrigida por perspectiva, recorte contra o
plano proximo, teste de profundidade, descarte de faces. `EGL_DEPTH_SIZE` 0 -> 16
(o config negava o buffer que o rasterizador ja tinha). O QEGL devolve o valor
pelo ponteiro final e os argumentos estao deslocados pelo `pMe`.

**O BLOQUEIO, medido e com um so caminho de saida:** os dez titulos da familia
`emulator_neo` conferem `GL_OES_draw_texture` no `glGetString(GL_EXTENSIONS)`,
nao o encontram, e desistem -- desenham "InitGLExtensions failed" e param. O
comentario do zeebx (`machine/gl.rs:78`) diz exactamente o mesmo.

**Proximo passo, e as duas metades TEM de ir juntas:**
- `glGetString(GL_EXTENSIONS)` a anunciar `GL_OES_draw_texture`;
- `eglGetProcAddress` a devolver trampolins REAIS, e `glDrawTexivOES` servido.

Anunciar sem servir faz o titulo saltar para uma funcao que nao existe. Ordem de
grandeza medida pelo agente `px`: 1e5 pixels por titulo, e a saida do ecra de erro
para imagem. **Temos agora os cabecalhos oficiais**: o SDK BREW 4.0.2 SP19 foi
extraido para `/tmp/sdk402` (tem `sdk/inc/gles/`), que e a versao que a consola usa.

Titulo: `karnovr` (ou qualquer um dos dez).

## Etapa 7 -- A tabela de interfaces verificada contra o SDK -- PARCIAL (85%)

`AEEHelperFuncs`: os 117 nomes estao GERADOS do cabecalho e conferidos --
**incluindo contra o SDK 4.0.2, que e o da consola: zero divergencias com o
BrewMP 7.12.5**. Mas so **20 dos 117 estao LIGADOS**.

Arbitragem fechada nesta ronda: o zeebulator tem `strstr` em `0x0e8` (que e o
`stristr`) e `sprintf` em `0x13c` (que e o `vsprintf`). Nos e o zeebx batemos com
o cabecalho em 13/13. As duas trocas sao perigosas em silencio: `stristr` nao
distingue maiusculas, e `vsprintf` recebe `AEEOldVaList` em vez de varargs.

O `IGLES11` JA TEM GERADOR (`tools/nomear_igles.py`, com guarda no CTest):
`INHERIT_IBase(3) + IGLES10(106) + IGLES11(39) = 148`, que bate com o
`kIglesSlots` que estava a mao. As faltas anonimas do corpus passaram de
**dezassete para QUATRO** (`IBitmap::slot13`, `IShell::slot19`, `IFile::slot3`,
`IFile::slot5`).

Falta: ligar mais dos 117 ajudantes (estao 20), e nomear as quatro que sobram. O `0x0dc` FICOU RESOLVIDO: e `memcmp`, provado por desmonte
(`ddragonz.mod` 0x11DDB8 faz `memcmp(dados, "OI", 2)`, e `"OI"` e o magic do
OBM1). O engano do zeebulator veio do stub dele, que ao "suceder sempre"
desligou a verificacao do magic -- **um stub que mente pode fabricar a evidencia
que o justifica**.

## Etapa 8 -- Entrada -- PARCIAL (60%)

`IHIDDevice` instalado. Os UIDs `0x1eaa`/`0x0135` foram promovidos de DECLARADO a
MEDIDO (descritor USB capturado). `IShell::SendEvent` (slot 21) implementado com
entrega SINCRONA ao `HandleEvent` -- medido no `tectoy.mod` 0x6a3a0, que le a
resposta na instrucao seguinte ao retorno.

Falta o criterio: **o d-pad responder no `tectoy`**. O proximo pedido desse
titulo ja e outro: `IRootForm::HandleEvent` do evento privado `0x7b0a`.

## Etapa 9 -- Regressoes automaticas -- FECHADA

`tools/bateria` + `tools/comparar`. Nesta ronda correram 14 corridas, cada commit
medido em ISOLADO (a corrida anterior e a referencia da seguinte).

Novo: a bateria publica agora um bloco de **PRESSUPOSTOS** (`GetDeviceInfo` 44x em
37 titulos, HID 4x, `GetFreeSpace` 2x). A classe "DECLARADO" existia no codigo e
era invisivel na corrida; o `comparar` le o campo como NEUTRO e OPCIONAL, e as
corridas antigas continuam a ser lidas.

---

## Etapas novas, que a medicao obrigou a acrescentar

### Etapa 10 -- O instrumento mede menos do que acontece -- PARCIAL (80%)

**FEITO, e a resposta foi "sim, mas os numeros estavam errados".** A perda
existia: `e.recusadas = cpu.InstruscoesRecusadas()` era atribuicao e nao
acumulacao, e o `Repor` do `CreateInstance` apagava a conta da carga. Mas o total
e **28 227 409** e nao 21 M, e o maximo da referencia era **4 549 330** e nao 936.
A parte da alegacao sobre as FALTAS era falsa -- nunca se perderam.

E A RESPOSTA QUE IMPORTAVA: com as recusas completas, **a tabela de faltas por
titulos afectados nao muda nem uma chave**. O plano NAO estava mal ordenado.

Ha agora `recusadas_carga`, `recusadas_create`, `recusadas_start` e
`recusadas_quadros`, tratados como campos OPCIONAIS pelo `comparar` (uma corrida
antiga continua a ser lida). Mais dois defeitos de instrumento corrigidos na
mesma frente:
- **`Memoria::PcAtual` nunca era chamado fora dos testes** -- toda a corrida real
  registava `pc = 0`, e qualquer diagnostico que dependesse de saber QUEM fez o
  acesso estava cego;
- **`kSlotDbgPrintf` estava ligado ao id do `strtowstr`** -- toda a chamada a
  `dbgprintf` do corpus corria a funcao errada, e escrevia UTF-16 por cima do
  codigo do proprio modulo.

FALTA para fechar: a conta continua CURTA -- a instrucao indefinida `0xE7F000F0`
corre como `ldrb` e nao e recusada. E o `Despacho::Correr` aborta a fase em
`saidas > 200` contando todas as saidas em vez de recusas seguidas.

### Etapa 11 -- Os formatos que faltam, por ordem de titulos afectados

Temos QUATRO carregadores (`mod`, `bar`, `pack`, `inflate`). O zeebulator tem
ONZE (`atitc`, `bar`, `ggz`, `midi`, `mif`, `mod`, `obm1`, `pakz`, `pkg`, `png`,
`wav`); o zeebx tem `atc`, `gif`, `paltex`, `font`, `icon` no lado do video.

**`.pakz` -- 15 pastas, e o empacotador do motor first-party.** O
`ttd_packer.cpp` gera `pak%i.pakz` com `LzmaUtil.cpp`: e `PACK` + **LZMA**, e nao
zlib como o `.pkg`. Formato confirmado byte a byte contra
`alice/resources.pakz` (793 entradas, registos de 64 bytes, soma dos tamanhos
igual a `tab_off - 12` exacto, cada entrada um stream LZMA_ALONE que descomprime
limpo). O zeebulator tem-no documentado em `core/loader/pakz.h` e implementado em
100 linhas. Dentro do arquivo do `alice`: 128 `.dds`, 30 `.wav`, 24 `.xui`,
10 `.mp3`, 2 `.lua`.

**`.mif` -- o CLSID vem do dispositivo, e nao de nos.** O zeebulator e o zeebx
implementam-no; nos lemos o CLSID do `corpus62.json`, que e um ficheiro que NOS
escrevemos. E uma fonte de verdade a menos.

Os formatos de estudio (`.big` em 52 pastas com magia `BIGF`, `.viv` `BIG4`,
`.qxt` `QX`, `.tex` `ZTEX`, `.fnz` `FONT`, `.msh` `SHPM`, `.lzc` `JDLZ`, `.rwh`
`IANN`, `.m3g`/`.sar` com cabecalho `SWERVE`/`SWVARC` da Ideaworks) NAO entram
agora: nenhum titulo chega a abri-los, porque todos param antes, no GL.

### Etapa 12 -- O Crazyball Engine, o motor first-party da Tectoy

**Achado a 15/09 nas strings da NAND de debug:**

    Crazyball Engine. Copyright (C) 2009 Tectoy Digital.   (zeeboids.mod)

Prefixo interno `ttd` (Tectoy Technology Digital). **DEZASSETE dos 62 titulos**
usam-no: `AirRacez`, `Bajaz`, `Boiaz`, `JetBoardz`, `Rolimaz`, `a3d`,
`activitycenter`, `alice`, `cnk2`, `dodgeball`, `footparty`, `funsoccer`,
`quake2brew`, `zeeboids`, `zeebopeteca`, `zeebotennis`, `zeebovolley`.

Os simbolos de depuracao trazem ~50 ficheiros `.cpp`, e os nomes das classes sao
**a API M3G (JSR-184) inteira, reimplementada em C++**: `ttdObject3D`,
`ttdTransformable`, `ttdGroup`, `ttdMesh`, `ttdSkinnedMesh`, `ttdMorphingMesh`,
`ttdVertexBuffer`, `ttdIndexBuffer`, `ttdTriangleStripArray`, `ttdAppearance`,
`ttdMaterial`, `ttdTexture2D`, `ttdImage2D`, `ttdPolygonMode`,
`ttdCompositingMode`, `ttdFog`, `ttdBackground`, `ttdCamera`, `ttdWorld`,
`ttdKeyframeSequence`, `ttdAnimationTrack`, `ttdAnimationController`, `ttdLoader`,
`ttdGraphics3D`.

E tem uma camada propria da consola: **`ttdGraphicsAdapterZeebo.cpp` e
`ttdGraphicsDeviceZeebo.cpp`**. Esse e o ponto exacto onde 17 titulos tocam no
nosso GL.

**Porque isto vale uma etapa:** perceber o que o `ttdGraphicsDeviceZeebo` chama
diz-nos o que 17 titulos precisam, de uma vez -- em vez de descobrir um slot de
cada vez, por um titulo de cada vez. E o mesmo raciocinio que transformou os dez
titulos "sem applet" num so defeito de `LDR PC`.

Criterio: a lista das chamadas de GL/EGL que o `ttdGraphicsDeviceZeebo` faz,
medida por traco num titulo que la chegue, e cruzada com o que servimos hoje.

---

## A ordem a seguir, por ganho MEDIDO  *(revista a 15/09, 3a ronda)*

**A ordem anterior tinha o `IDIB::pBmp` (25 titulos) como a maior alavanca. Eram
DOIS titulos** -- a falta media a nossa escrita e nao a leitura do guest. Foi
medido e corrigido; o item caiu de 1o para 10o. A lista abaixo ja conta com isso.

1. **`Despacho::Correr` aborta a fase cedo demais.** Conta TODAS as saidas em
   `saidas > 200`, em vez de recusas SEGUIDAS: depois de centenas de `strncmp`
   legitimos, a primeira recusa legitima mata a fase. Medido pelo agente `gl` como
   "o proximo bloqueio, e ja nao e GL". **E de instrumento, e barato.**
2. **`IThread::Start` -- 12 titulos.** A maior frente real depois da revisao.
3. **Etapa 6, a parte C: `GL_OES_draw_texture` + `glDrawTexivOES` JUNTOS.**
   Ja esta provado que e a extensao que para os dez (teste com `GL_EXTENSIONS`
   vazio), que anunciar sem servir da 10 regressoes, e que a funcao vem do
   `IGLES11Ext` (`0x0103d8eb`) e nao do `eglGetProcAddress`. Falta servir os 12
   metodos que hoje recusam com nome.
4. **Etapa 12 -- o que o `ttdGraphicsDeviceZeebo` pede.** 17 titulos de uma vez.
5. **O bloco GL nomeado:** `BindTexture`, `MatrixMode`, `Viewport` (4 titulos
   cada), `Enable`/`Disable`/`EnableClientState` (3 cada).
6. **`IFileMgr::RmDir` e `Remove`** -- 4 titulos cada.
7. **Etapa 13 -- `IUnzipAStream`** (9 titulos). No BREW a descompressao e uma
   INTERFACE, e nao um slot da `AEEHelperFuncs`. Vem antes dos formatos.
8. **Etapa 11 -- `.pakz`** (15 pastas, formato ja resolvido) e depois `.mif`.
9. **As quatro faltas que ainda nao tem nome:** `IBitmap::slot13` (4 titulos),
   `IShell::slot19`, `IFile::slot3`, `IFile::slot5`.
10. **`pBmp` a serio** -- `tekken2` e `zenonia`, os dois que o leem mesmo.

O QUE NAO FAZER, e porque (tudo medido nesta sessao): ligar mais ajudantes da
`AEEHelperFuncs` sem demanda (o `strncpy` e o `strstr` deram ZERO); anunciar
extensoes de GL sem as servir (10 regressoes, medido); implementar formatos de
estudio (`.big` em 52 pastas -- ninguem chega a abri-los); e tirar prioridades da
coluna de PEDIDOS em vez da de TITULOS AFECTADOS.

## Etapa 13 -- IUnzipAStream, a via de descompressao do BREW

`AEECLSID_UNZIPSTREAM = 0x01001014` (`AEEClassIDs.h:82`, `AEECLSID_CORE + 20`),
interface `IUnzipAStream` (`AEEUnzipStream.h`). A constante esta em NOVE dos 62
`.mod`: `alpineracerex` (6x), `ddragonz`, `ridgeracer`, `tekken2`, `gof`, `rmp`,
`zeeboids`, `allstarcards`, `pbc`. O `AEECLSID_MEMASTREAM` (`0x0100100C`) esta em
mais nove.

Achado ao fechar a contradicao do `0x0DC`: o `ddragonz` **nao tem inflate
proprio** -- sem tabelas de deflate e sem `0x1f8b` no binario. Descomprime pela
interface do sistema. Ate agora tinhamos a constante registada com o nome
`kIidFile`, e o `QueryClass` respondia SIM a uma classe que nao servimos.

## A ronda de 15/09 (2a metade) -- o que a medicao obrigou a mudar

**Vinte e oito commits, 668 testes, 0 regressoes em cada medicao isolada.** O que
mudou de facto no corpus:

| frente | efeito medido |
|---|---|
| **IThread** (`043d4cf` + `e697cd6` + `283d7dc`) | o IThread real, a thread a CORRER na fronteira entre APIs, e o `Start` com `lr == sentinela` como fronteira: 10 threads penduradas passam a correr |
| **A TELA no motor do IGLES11** (`3dbddd3`) | o `ridgeracer` desenha (0 -> 307 200 px). O `InstalarGl` ligava a tela ao IGL de 30000 e o `ConstruirIgles` reconstroi um motor PROPRIO para o IGLES11 (40300+) -- ficava sem ela |
| **O TECTO DE SAIDAS por fase** (`3258489`) | de 20 000 para 2 000 000: o `gof` (90,3 M px) e o `rmp` (91,9 M px) desenham, e o `tekken2` passa de 1 para **31 cores** |
| **Imagem e bitmap** (`b3acdbc`) | `IImageDecoder` + `IForceFeed` + descodificador PNG + a familia do `IBitmap` por objecto; `torkandkral` create 8 M -> 1,9 M passos |
| **cpu Thumb** (`eb6667e` + `35c09dc`) | as formas que faltavam (push/pop, shifts, ALU, stmia/ldmia, hi-reg, BL de 32 bits) e o **LSL #0 e um MOV**: applet 59 -> 62 |
| **GL nomeado** (`4c7573f` `bbc084b` `eef6d91` `1aa5b3a`) | os 13 slots nomeados, `Lightfv`/`Materialfv`/`Orthof`, `GL_OES_draw_texture` + `glDrawTexivOES` juntos, e o `eglMakeCurrent` dos quatro zeros (a forma do SDK de LARGAR o contexto) |
| **ajudantes** (`f5ab4e0` `7be87a9`) | `stricmp`, `atoi`, `strends`, `aee_GetTimeMS`, `wsprintf` |

### As licoes que valem mais que os commits

1. **Dois titulos tinham numeros grandes que eram ARTEFACTO.** O `tekken2` mostrava
   322 M pixels que eram **1050 enchimentos de ecra do fallback** (1 cor + barras de
   texto); o `allstarcards` mostrava 94 M que vinham do `atoi` a devolver
   `AEE_EUNSUPPORTED` **lido como numero de configuracao**. Em ambos, servir o que
   faltava MATOU o numero e revelou o estado real. **A coluna `pixels` premeia
   preenchimentos.**
2. **O gargalo desloca-se, nao desaparece.** Levantar o tecto de saidas nao criou
   trabalho; fez o `orcamento_esgotado` subir de 11 para 16 titulos. O tecto de
   PASSOS (8 M) passou a ser o muro dominante.
3. **O defeito de instrumento e TRANSVERSAL.** A leitura nao mapeada fica "pendente" e
   e consumida pela instrucao SEGUINTE: **toda a nossa evidencia de recusas esta
   atribuida ao PC errado** (o `ropi2` corrigiu a evidencia do `memo`). O `zeebx`
   resolve-o na raiz: a leitura devolve `Result` no proprio acesso (`mem.rs`).
4. **As referencias das frentes deslizam.** Um "0 melhorias" medido contra uma corrida
   de dois commits antes e uma contradicao, nao um resultado.
5. **O GUIAO DE ENTRADA existe e esta a zero.** O `EntradaDoZeebo`
   (`core/brew/ihid_entrada.h`) ja le um guiao de texto (`<t_ms> <eixo|botao> <uid>
   <valor>`, com relogio injectado) -- e a bateria entrega `eventos_do_guiao = 0`.
   O `allstarcards` medido acaba a espera de tecla, e 30 titulos estao nessa familia.

### O PROXIMO (por evidencia, nao por gosto)

1. **O defeito de instrumento da pendencia** -- conserta a base de prova de tudo o
   resto (receita do zeebx: `Result` no acesso).
2. **`IShell::slot43`** (4 titulos: abd, pacmania, ridgeracer + 1) -- contrato MEDIDO
   no zeebulator (`ishell.cpp`): alterna **35 -> 0** por objecto e escreve a constante
   pequena **1** no `pOut` (o endereco do shell forca "nao pronto").
3. **O guiao de entrada na bateria** -- mecanismo pronto, 30 titulos na familia.
4. **Blending + iluminacao no rasterizador** (`core/video`) -- receita completa no
   `zeebx` (`rasterizer.rs`). E o unico caminho medido para `cores > 10` nos 3D, que
   hoje so enchem o ecra.
5. **`IShell::slot19`** (`LoadResObject`, 2 titulos) e **`GetIntegerv(GL_MAX_TEXTURE_SIZE)`**.

**Armadilhas registadas:** o `tools/baseline/bateria.json` versionado e do commit
`3f4681d` (velho); e `/tmp/corpus62-ARMADILHA-clsid-zero.json` tem os 62 titulos com
`clsid = 0` -- **nunca medir com ele** (o corpus e o do repo, `sha256 348106f1...`).

---

## A ronda continua -- o que fechou e o que ficou nomeado (fim de 15/09)

**Cinco frentes integradas depois da primeira metade:**

| frente | commit | efeito |
|---|---|---|
| `inst` | `2fdd07d` | a leitura nao mapeada ganha DONO: **112 recusas reatribuidas** ao PC certo (alice 21 -> 1). A evidencia de recusas deixa de mentir |
| `ishell2` | `abb3ddd` | **`slot43` = `DetectType`** (`AEE_ENEEDMORE` = 35) e **`slot19` = `LoadResObject`**: o `toyraidzeebo` 307 K -> **75,9 M px, 44 cores**, e deixa de sair do modulo |
| `entrada` | `7f9cdd1` | o guiao de teclas ligado (`ZB2_GUIAO`); neutro sem guiao |
| `rast2` | `18fa217` | mistura + alpha test + mascara de cor + ILUMINACAO no rasterizador (com os valores provados a mao) |
| `fmt` | `33182e6` | o `%f` deslocava argumentos; o `vsnprintf` lia a lista do r2 e faltava uma indirecao (`AEEOldVaList`). **A mensagem de ASSERT passa a ler-se** |

**Os numeros (HEAD `33182e6`, 695 testes):**

    carga 62/62 | modulo 62/62 | vtable 62/62 | applet 62/62
    ciclo completo 28/62 | pixels>0 18/62 | cores>10 2/62 (tekken2 31, toyraidzeebo 44)
    total 1 718 098 509 px | 0 regressoes em cada medicao isolada

### As contradicoes que corrigiram o PLANO (nao so o codigo)

1. **A receita do zeebulator para o `slot43` estava errada** (o alternador 35/0
   deixava 17 dos 35 objectos do abd por inicializar). A referencia mede-se; nao se
   copia.
2. **Os 90 M px do `gof` NAO sao desenho**: sao 294 `glClear`. `glDrawArrays`/
   `glDrawElements` = ZERO -- o muro e a VFS (`.aei`, `.w3t`), e o rasterizador nao
   era o gargalo que o plano dizia.
3. **O guiao de entrada NAO destrava os 30 silenciosos**: 1 de 30 muda e **0 de 30
   ganha um pixel**. O que prende o `allstarcards`: 107 recusas de CPU e 22
   `SetupNativeImage`, a ~4 M passos por quadro.
4. **O `%f` do formato deslocava TODOS os argumentos seguintes** -- e o `vsnprintf`
   lia a lista do registador errado. Dois defeitos que juntos escondiam as mensagens
   de ASSERT de 7 titulos.

### O PROXIMO, por evidencia (substitui a lista anterior)

1. **`.aez`** -- **formato DECIFRADO** (medido): registos `[u8 len][caminho][u32][u32
   tam][gzip]` colados, com `tam` = tamanho COMPRIMIDO. O `res.aez` do gof tem 2 660 646
   bytes; os 4 primeiros registos descomprimem limpo (12 883 / 43 172 / 52 750 / 12 128
   bytes). E o muro do `gof` e do `rmp` -- dois titulos que desenham e nunca submetem
   geometria.
2. **`IShell::slot32` (`GetHandler`)** -- 95 pedidos em 5 titulos (o passo seguinte do
   fluxo do `DetectType`).
3. **`IGLES11::AlphaFunc`** -- 299 recusas no `rmp`; o pedido nem chega ao motor (o
   alpha test que a `rast2` implementou fica inerte).
4. **`SetupNativeImage`** (22x no allstarcards) e a recusa de CPU `0xea00001a` em
   pc=0xf3f4 (107x no mesmo titulo).
5. A leitura nao mapeada `0x200a180e` no pc `0x23430` do `gof` (294x, 1 por quadro).

---

## A ronda da noite -- os CLSIDs com a vtable certa, o SQLite de verdade, e o bitblt

**Seis commits, 756 testes, ctest 13/13, 0 regressoes em cada medicao isolada.** O que
mudou no corpus:

| frente | commit | efeito medido |
|---|---|---|
| `setup` | `0e3d5e8` | `SetupNativeImage` (0x064): **allstarcards 650 K -> 220 216 388 px, cores 2 -> 1406, blits 2 -> 10 073** |
| `zwheel` | `36102f1` | `ISQLMgr::Open` devolve o banco no **r2** (nao no r3): o `tectoy` 255 -> 16 197 passos |
| **`sqlite`** | `56121fb` | o subconjunto de SQL a mao **trocado pela ponte sobre SQLite 3.50.2** (amalgamacao em `third_party/sqlite3/`); os 4 bancos reais abrem |
| `fora` | `4b21651` | o **`BitBlt` lia os pixels a partir do endereco do OBJECT O** -> origem = `pBmp`, passo = `nPitch`: **toyraidzeebo cores 65 -> 332, tekken2 31 -> 101** |
| `mediautil` | `dc132e7` | cada CLSID recebe a vtable da SUA interface; o `CreateMedia` **escreve o `ppm`**: 48 buffers PCM reais, 106 recusas -> 0 |

**O numero que interessa:** `cores > 10` passou de 1 para **3** titulos --
`tekken2` **101**, `toyraidzeebo` **332**, `allstarcards` **1406**. O total de pixels do
corpus saiu de 1,64 para **2 062 839 542**.

### As quatro contradicoes que corrigiram o plano (todas minhas)

1. **O LEDGER tinha a cadeia do `allstarcards` errada.** Eu escrevi que as 106 recusas de
   CPU eram consequencia do `SetupNativeImage`. **Nao eram**: o objecto de `[r4+28]` e o
   `IMedia **ppm` de `IMediaUtil::CreateMedia`, e nos criavamos o `AEECLSID_MEDIAUTIL` com
   a **vtable do `IMedia`** -- o slot 3 respondia `RegisterNotify` sem escrever o `ppm`.
   Corrigido no ledger com os enderecos.
2. **`CreateMediaEx` e o slot 5, nao o 4** (o `EncodeMedia` ocupa o 4). Com o Ex no 4, quem
   pedisse o 4 recebia a funcao errada.
3. **A referencia pode ser VELHA.** O `/tmp/corrida_park.json` (de antes do `zwheel`) deu
   "5 regressoes e 11 melhorias FALSAS" a frente `sqlite`; a base verdadeira e a corrida
   imediatamente anterior. Ja aconteceu com a `igl2` e com a `thrd`.
4. **"Vai mais longe" nao e o criterio.** O `tectoy` com SQLite REAL faz **233 passos a
   MENOS** no create -- porque o `SELECT` responde e o jogo salta o `CREATE`+`INSERT` que o
   subconjunto forcava. Menos passos, e um banco que funciona.

### Licao de metodo (a quarta vez na sessao)

**O desmonte local diz COMO o jogo morre, nao PORQUE o ponteiro esta a zero.** A cadeia do
`allstarcards` so ficou certa quando a frente mediu o **valor** de `[r4+28]` em vez de o
inferir do `ldr`/`blx`. O mesmo padrao apareceu no `pbmSource` do `BitBlt` e no `dwSize`
impar do `IMediaUtil`.

### O PROXIMO, por evidencia

1. **`Memoria::Ler fora de instrucao`** -- 9 titulos; os que ficam sao o `strstr` do guest
   com agulha que nao e ponteiro (a frente `fora` mediu: qualquer "arranjo" seria mascara).
2. **Os 4 ajudantes que faltam**: `utf8towstr` (2), `wstrtoutf8`, `aee_GetSeconds`,
   `aee_GetJulianDate` (o `tectoy` e o `pbc`).
3. **`IGLES11::GetIntegerv`** -- 3 titulos (gof/pbc/rmp); o motor tem o slot e o que falta e
   responder os `pname` que eles pedem DE FACTO.
4. **`AEECLSID_CONFIG` e `AEECLSID_DOWNLOAD`** (tectoy) + o `IRootForm HandleEvent`.
5. **Os 5 `dwSize` impares** do `allstarcards` no `IMediaUtil::CreateMedia` (221 531,
   299 689, 587 245, 470 217, 2 507): recusados com motivo -- **nao inventar formato**.
6. A familia do `0xbfffff64` (6 titulos) e a fila estatica do `cninja`.

---

## O tecto de passos (o terceiro limite do instrumento que tapava trabalho)

Medido em tres valores, e a conclusao foi imediata:

| tecto de passos por fase | `heavyweaponbrew` | `tekken2` | bateria dos 62 |
|---|---|---|---|
| **8 M** (era o valor) | **0 px** (`create:orcamento_esgotado`) | 101 cores | ~40 s |
| **16 M** (`43e8557`) | **116 851 652 px** | **174 cores** | 1m48 |
| 32 M | 116 851 652 px | 174 cores | 2m53 |

O `create` do `heavyweaponbrew` precisava de **8 505 337** passos -- **0,5 M** acima do tecto. E 16 M da
as MESMAS 5 melhorias que 32 M, em dois tercos do tempo: por isso o valor e 16 M, e o custo (a bateria
4x mais lenta) fica declarado.

**E a terceira vez nesta sessao** que um limite do INSTRUMENTO tapava trabalho real: os 200 despachos
(`6c72cb5`), as 20 000 saidas por fase (`3258489`) e agora os 8 M passos.

### O que ficou por decidir (precisa do dono)

O `corpus62.json` declara `max_steps` para **dois** titulos e os dois valores sao artefactos do GUI do
zeebulator -- `cnk2` 186 486 543 e `fifa09` 483 295 456. O agente `cnk` provou que **um numero igual ao
`max_steps` nao mede trabalho**: o `cnk2` nao consome 186 M, consome o que o tecto lhe der. Removê-los
muda o `sha256` do corpus, que e a proveniencia de todas as corridas de referencia -- nao se toca sem
ordem explicita.

### Os ajudantes do Z-Wheel (`51f3a43`)

- `utf8towstr` (0x050): **a regra veio do SDK DESMONTADO** (`BREWSim.dll1`, export `aee_UTF8ToWStr`):
  enche o destino, nao reserva o NUL, devolve TRUE quando gasta o `nLen`. O nosso reservava -- e **comia
  o ultimo caracter** (`"http://www.ats.com/"` -> `"http://www.ats.com"`).
- `wstrtoutf8` (0x054) implementado (`nLen` em AECHARs, CESU-8, nunca 4 bytes por unidade).
- `aee_GetSeconds`/`aee_GetJulianDate` com as ancoras do teste do proprio SDK: 1980/01/06 = **domingo**.
  **O zeebx usa domingo a zero -- erra um dia em toda a semana.**
- **Prova qualitativa**: a Z-Wheel gravava `'���������ȿ'` e passou a gravar
  `'CreditServerURL','http://www.ats.com/'`.
- As 5 faltas do `pbc` **nao somem de proposito**: o chamador corta o texto de creditos em janelas de 21
  bytes, 4 comecam em byte de continuacao -- **o proprio SDK recusa igual**, e o numero nao se maquilha.

---

## O heap do guest (o quarto limite do instrumento)

O heap do guest tinha **12 MiB** e a familia TTD pede **20 a 23 MiB so no `CreateInstance`** --
medido nos `Malloc` do proprio guest (`zeeboids` 23 MiB, `zeebotennis`/`dodgeball` 22, `funsoccer` 21,
`activitycenter` 20+3). Com 12 MiB o motor do jogo **dizia-o pelos seus `dbgprintf`**: "Could not
initialize the MemoryManager", erro 10, "TTDMM ASSERT" -- e o `footparty` dizia mesmo "The game will
refuse beginning". 17 ASSERTs e **0 pixels** em 9 titulos.

Medido nos dois eixos, porque so um podia ser a causa:

| | heap | resultado |
|---|---|---|
| BASE | 12 MiB em `0x80200000` | 17 ASSERTs, 0 px |
| so o ENDERECO | 12 MiB em `0x10000000` | 17 ASSERTs, 0 px -- **nao era o endereco** |
| so o TAMANHO | 64 MiB em `0x80200000` | colide com a nossa banda de objectos; 2 titulos derrapam |
| **o par certo** | **64 MiB em `0x10000000`** (layout do zeebx) | **0 regressoes, 8 melhorias** |

`gof` **0 -> 90 316 800 px**; `dodgeball`/`zeebopeteca`/`footparty`/`zeeboids`/`alice`/`funsoccer`/
`zeebotennis` de 0 a **307 200 px**. Titulos sem um pixel: 42 -> **34**.

**Quarto limite do instrumento levantado nesta sessao**: os 200 despachos, as 20 000 saidas por fase,
os 8 M passos e agora os 12 MiB de heap. O padrao e sempre o mesmo -- o instrumento a tapar trabalho
legitimo, e o sintoma a aparecer como "o titulo nao faz nada".

### O estado dos 62, depois de tudo isto

**28/62 desenham** (era 20), e o total subiu a **2 272 866 620 px**. Os 34 restantes estao nomeados por
causa, e a maior e a **re-entrada do modulo** (a veneer da ROPI a correr sobre um objecto NULO, 9
titulos) -- o mecanismo que a frente `ropi2` mediu e para o qual ja ha teste.

---

## A ronda do 16/09 -- tres defeitos GERAIS, e nenhum deles era uma API em falta

### 1. O bit alto do `malloc` e uma bandeira (`fe1eaad`)

`AEEStdLib.h:547` define `ALLOC_NO_ZMEM (0x80000000L)` e o `MALLOCREC_EX` (`:556`) usa-o como mascara:
**o bit alto do tamanho nao e tamanho, e uma bandeira -- e o `malloc` zera por omissao.**

O `quake2brew` faz `orr r0, r5, #0x80000000` (`0x000b0650`) antes de chamar o helper, e o nosso servidor
lia aquilo como 2 GiB: **339 `MALLOC ERROR` do proprio Quake com o heap VAZIO** (`alocado=0,0 MiB de
64,0 MiB`), as `cvars` todas a NULL e **99 `atoi(NULL)`**. `pixels 0 -> 307 200`, e o `chessbots` passa a
fazer 19x mais trabalho (`passos_start` 1418 -> 26 939). **Um numero que nao cabe na leitura e um numero
mal lido** -- e quem "corrigisse" o `atoi` fechava 99 faltas e deixava o jogo partido.

### 2. O `GetAppInstance` respondia zero dentro do proprio `CreateInstance` (`81e3ca2`)

A re-entrada do modulo (o grupo G2) tem **tres mecanismos**, e um era nosso. A cadeia medida no `Rolimaz`:
`GetAppInstance` devolve 0 -> o codigo usa o zero como objecto -> `ldr r0,[r4,#0xc]` com `r4=0` le o campo
`+0xc` do **cabecalho do modulo** -> salta para o lixo. **O `0x000fea00` dos 5 titulos TecToy nao era um
endereco em falta**: era uma palavra do cabecalho, porque o objecto era nulo. A regra que faltava era a que
o `EntregarEventoAoApplet` ja tinha. **0 regressoes, 8 melhorias**: `ddragonz` 0 -> **120 972 000 px** e
1500 textos, 5 titulos 0 -> 307 200/614 400, `bio4_brew` 497 853 recusas -> 0. **35/62 desenham** (eram 29).

### 3. O desenho 2D nao tinha destino (`b810e4e`)

A pagina do ecra no guest so nascia a pedido -- e o `allstarcards` **desenha 296 rectangulos sem nunca a
pedir**. O ecra passa a nascer quando o titulo o usa: `pixels 220 216 388 -> 224 621 076` e as 296 recusas a
zero. **Um campo desceu** (cores 1406 -> 1405) e o comparador chamou-lhe regressao -- o `ZB2_HIST`
(`cdf259c`) mostrou que era **uma cor com 4 pixeis** (`0x1060`) coberta pelos rectangulos que o titulo pediu.

---

## O laco de trabalho

1. **Um titulo de cada vez**, do mais simples ao mais complexo.
2. **Antes de mexer, um comando que fica vermelho** por causa do defeito e verde
   depois. E o vermelho prova-se ARRANCANDO a correccao, nao imaginando-o.
3. **3 a 5 hipoteses ranqueadas** antes de testar qualquer uma.
4. **Cada log de depuracao com marca unica** (`[DEBUG-xxxx]`).
5. **3 correcoes falhadas -> questionar o desenho**, nao tentar a quarta.
6. **Progresso no ledger**, nao na memoria.
7. **Ruling em vez de paragem**: decidir, registar, continuar.
8. **Ler a referencia antes de propor** (zeebx, zeemu, zeebulator, SDK 4.0.2).
   Tres defeitos da ronda de 15/09 so apareceram assim.
9. **Medir cada commit em ISOLADO**, com a corrida anterior como referencia.
10. **Dizer quando a medicao nao mudou.** Metade dos commits desta ronda deram
    0 melhorias, e escrever isso e o que os impede de serem lidos como ganho.

## O que NAO faz parte deste plano

- Interagir com o `zeebx` para alem de ler e re-medir. Mensagem de commit alheia
  e pista, nao prova.
- Tocar no `Zeebulator` da branch `new_ez_ui`. Fica intacto.
- Publicar sem autorizacao.
- **Actualizar `tools/baseline/bateria.json` sem pedido explicito.** A referencia
  so muda quando o dono do projecto o disser.
