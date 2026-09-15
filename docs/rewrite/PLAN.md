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
| applet instanciado | 37 | **56** | 90% |
| ciclo completo (create+start retornam) | 4 | **30** | 48% |
| sem faltas nenhumas | 0 | **32** | 52% |
| **pixels escritos** | 0 | **10** | 16% |
| **cores > 10 (criterio da etapa 3)** | 0 | **0** | **0%** |

| # | etapa | estado | % |
|---|---|---|---|
| 0 | fundacao de medicao | FECHADA | 100% |
| 1 | CPU de referencia | REABERTA E FECHADA | 95% |
| 2 | carregador MOD + 1o titulo | FECHADA | 100% |
| 3 | IShell / IDisplay / desenho 2D | **PARCIAL** | **40%** |
| 4 | ficheiros, VFS, 62 a correr | FECHADA | 100% |
| 5 | midia e audio | PARCIAL | 50% |
| 6 | GL e as duas familias 3D | **PARCIAL** | **25%** |
| 7 | tabelas verificadas vs SDK | PARCIAL | 70% |
| 8 | entrada | PARCIAL | 60% |
| 9 | regressoes automaticas | FECHADA | 100% |

**Media: 74%.** E a media engana: as tres etapas que faltam sao as que produzem
IMAGEM, que e o que o projecto existe para fazer.

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

## Etapa 3 -- IShell, IDisplay e desenho 2D -- PARCIAL (40%)

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

## Etapa 6 -- GL e as duas familias 3D -- PARCIAL (25%) -- **O GARGALO**

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

## Etapa 7 -- A tabela de interfaces verificada contra o SDK -- PARCIAL (70%)

`AEEHelperFuncs`: os 117 nomes estao GERADOS do cabecalho e conferidos --
**incluindo contra o SDK 4.0.2, que e o da consola: zero divergencias com o
BrewMP 7.12.5**. Mas so **20 dos 117 estao LIGADOS**.

Arbitragem fechada nesta ronda: o zeebulator tem `strstr` em `0x0e8` (que e o
`stristr`) e `sprintf` em `0x13c` (que e o `vsprintf`). Nos e o zeebx batemos com
o cabecalho em 13/13. As duas trocas sao perigosas em silencio: `stristr` nao
distingue maiusculas, e `vsprintf` recebe `AEEOldVaList` em vez de varargs.

Falta: o `IGLES11` **nao tem gerador** e aparece na demanda com 14 slots
distintos. E o `0x0dc`, que o zeebulator identificou empiricamente como
"descomprime gzip" e o cabecalho diz `memcmp` -- as duas nao podem ser verdade.

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

### Etapa 10 -- O instrumento mede menos do que acontece

**O achado mais grave da ronda**, e esta por confirmar: a bateria nao reporta as
recusas da fase `start`. O agente `cpu` contou **21 145 027** recusas no
interpretador contra um maximo de **936** no campo `recusadas` do JSON.

Se isto se confirmar, varias das prioridades tiradas da coluna de faltas estao
mal ordenadas -- e o P3 (a bateria como especificacao executavel) esta a falhar
em silencio, que e exactamente o que o P2 existe para impedir.

Criterio: a soma das recusas do JSON bate com a soma contada no interpretador,
por titulo e por fase. Um teste que falha se divergirem.

**Esta etapa vem ANTES de escolher a proxima frente por numeros.**

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

## A ordem a seguir, por ganho MEDIDO

1. **Etapa 10** -- confirmar (ou desmentir) as 21 M recusas nao reportadas.
   Vem primeiro porque decide se os numeros que ordenam tudo o resto sao de fiar.
2. **Etapa 6** -- `GL_OES_draw_texture` + `glDrawTexivOES`, as duas juntas.
   Unico caminho medido de ecra de erro para imagem; 10 titulos; 1e5 px cada.
3. **Etapa 12** -- o que o `ttdGraphicsDeviceZeebo` pede. 17 titulos de uma vez.
4. **Etapa 11** -- `.pakz` (15 pastas, formato ja resolvido) e depois `.mif`.
5. **Etapa 3** -- so depois disto e que o criterio das 10 cores tem hipotese.

O que NAO fazer agora, e porque: ligar mais ajudantes da `AEEHelperFuncs`
(`strncpy` e `strstr` entraram nesta ronda e deram ZERO na bateria -- entram por
correccao de contrato, nao por demanda medida); implementar formatos de estudio;
e tirar prioridades da coluna de PEDIDOS em vez da de TITULOS AFECTADOS (o
`IFile::Read` tem 70 pedidos e e UM titulo so).

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
