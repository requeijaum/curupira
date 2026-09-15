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

| degrau | inicio | agora | % |
|---|---|---|---|
| carga | 62 | 62 | 100% |
| ponteiro de modulo | 62 | 62 | 100% |
| vtable valida | 48 | **62** | **100%** |
| applet instanciado | 37 | **58** | 94% |
| ciclo completo (create+start retornam) | 4 | **30** | 48% |
| sem faltas nenhumas | 0 | **34** | 55% |
| **pixels escritos** | 0 | **11** | 18% |
| **cores > 10 (criterio da etapa 3)** | 0 | **0** | **0%** |

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
