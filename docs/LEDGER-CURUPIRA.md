# Ledger — Curupira

> **Histórico transferido de `requeijaum/zeebulator`.** O código novo deixou de
> viver dentro do fork GPLv3 e passou a `requeijaum/curupira` (MIT). Os factos,
> medições, erros e *rulings* abaixo foram preservados porque a memória da conversa
> não é uma fonte de verdade. Caminhos antigos `research/sources/zeebulator/src2/`
> devem ser lidos como `curupira/` quando se referem ao rewrite.


Formato copiado de `obra/superpowers` (`subagent-driven-development`). MOTIVO:
"conversation memory does not survive compaction"; apos uma compactacao, confiar
neste ficheiro e no `git log` acima da propria memoria.

Regra de escrita: cada linha nomeia factos que existem fora da minha cabeca --
commits, caminhos, numeros medidos. Quando uma decisao foi tomada sozinho, entra
como `Ruling:` com o custo de errar.

Repositorio: `/home/rafaelfrequiao/projects/zeebo-emulator` (R).
Emulador: `research/sources/zeebulator` (ZB). Remoto `zeebulator`.

---

## Objetivo maior (do dono do projeto)

Equiparar o Zeebulator ao zeebx POR MEDICAO, nunca por citacao. FASE 0 (bateria
comparativa) **continua sem autorizacao explicita** -- sem ela, "equiparado" nao
tem numero. Pedida uma vez; nao repetir o pedido.

## Rewrite (branch `full-rewrite`)

Desenho: `docs/rewrite/DESIGN.md` (7 principios). Plano: `docs/rewrite/PLAN.md`
(10 etapas). Arvore: `src2/`, com o seu proprio CMake e build.

| etapa | estado | prova |
|---|---|---|
| 0 -- fundacao de medicao (`core/tempo`, `core/traco`, `core/memoria`) | **FECHADA** | 20 testes verdes, 3 guardrails provados por violacao deliberada, 0 avisos |
| 1 -- CPU de referencia (ARM + Thumb) | **FECHADA** | 47 testes verdes, 4 guardrails provados por violacao deliberada, 6 bugs reais encontrados |
| 2 -- carregador de MOD + primeiro titulo (`imicro3d`) | **PARCIAL** | carrega, `AEEMod_Load` corre e devolve ponteiro nao nulo; estrutura do modulo verificada campo a campo. Falta `EVT_APP_START` |

Ruling: a arvore nova vive em `src2/` com CMake proprio, e nao substitui a
antiga. Custo se errado: duas arvores para manter ate a nova provar valor.
Custo se tivesse substituido: perder a unica referencia que funciona.

## Etapa 1 -- o que os testes revelaram

Seis bugs reais no interpretador, encontrados pelos testes e nao por leitura:
ordem do `MLA` (`Rn*Rm + Rs`, nao `Rm*Rs + Rn`); bandeiras N/Z suprimidas por um
`carry_ja_posto` mal delimitado; o bloco (`LDM`/`STM`) nao avancava o PC; a
mascara do `MUL` apanhava o `UMULL` (separa-os o bit 23); faltava o +8 no acesso
relativo ao PC; e o despachante testava os bits 27-26 antes do bit 25.

E cinco erros MEUS, no teste e nao no emulador: encodings ARM escritos a mao com
campos trocados, ordem de preparacao invalida, expectativa errada de carry (o C
e do lado sem sinal), `LDR` de literal sobreposto, e uma afirmacao contraditoria.

Ruling: nos testes, as instrucoes ARM passam a ser construidas por um
**montador minimo de campos nomeados**, e nao por literais hexadecimais. Custo se
errado: ~40 linhas de teste a mais. Custo se nao fosse feito: uma classe inteira
de erro em que o TESTE acusa o EMULADOR -- que ja custou varias rondas hoje.

## Etapa 2 -- ate onde chegou, medido

O `imicro3d.mod` (90068 bytes) carrega, o `AEEMod_Load` corre **60 instrucoes**,
pede **um** bloco de **36** bytes ao `malloc` (nSize 20 + `sizeof(IModuleVtbl)`
16) e **devolve um ponteiro de modulo nao nulo** (`0x80200010`). Nenhuma
instrucao recusada.

**Falta**: `IModule::CreateInstance` (vtable do modulo, slot 2) e
`HandleEvent(EVT_APP_START)`. Nao esta feito.

A sonda `tools/sonda_mod` mede cada passo e diz qual e a proxima funcao do
sistema que falta -- foi isso que tornou a etapa possivel.

Ruling: a faixa de saida para C++ e a mesma da arvore antiga (`0xF0000000`), com
um endereco executavel por slot, e `Correr` para quando o PC entra nela. Custo se
errado: um endereco a mais por slot. Custo se nao fosse feito: um ponteiro de
funcao do guest que nao e executavel, e um `bx` para memoria que nao existe.

## Etapa 2 -- a questao aberta, com precisao

**Pergunta**: quem escreve `module+12` (o `CreateInstance` do APPLET)?

E o que falta para o `IModule::CreateInstance` funcionar: ele le `+12`, encontra
zero e sai por falha, sem criar o applet.

**O que JA se sabe, medido**: `module+12 == 0` depois do `AEEMod_Load` e o
comportamento CORRECTO do modulo -- o proprio `AEEMod_Load` empurra zero para os
argumentos 5 e 6 da pilha, que sao os que `AEEStaticMod_New` escreve em `+12` e
`+16` (`stmib r0, {r1, r6, r7, r8}` em 0x0010077c). Nao e memoria por preencher,
e nao e um argumento em falta na nossa chamada.

**Ruling**: o proximo passo NAO e "implementar mais slots da tabela". E procurar
quem escreve `+12` -- provavelmente um caminho de registo do applet que corre
antes do `EVT_APP_START`. Custo se errado: um par de horas. Custo de continuar a
acrescentar slots: acumular codigo que nao e o que falta.

## Bateria (etapa 4, adiantada) -- numeros por titulo

`tools/bateria` corre os 62 titulos e regista um estado medido por titulo.
Trazida para antes das etapas 3-4 porque, ao fim de tres hipoteses falhadas sobre
o MESMO modulo (`imicro3d`), a regra mandou mudar de instrumento em vez de tentar
a quarta.

| metrica | valor |
|---|---|
| titulos que carregam | **62 de 62** |
| com ponteiro de modulo | **48** (era 1) |
| com applet | **0** |
| esgotam o orcamento na carga | 14 (inclui a familia Neo Geo inteira) |

**A mudanca que valeu 47 titulos**: o `IShell` passou a ser um objecto a serio --
um endereco cujo primeiro campo e uma vtable. Medido no desmonte do `peggle`
(0x0010278c): depois do `malloc`, o primeiro que TODO modulo faz e
`ldr r0,[pishell] / ldr r1,[r0] / bx r1` = `shell->AddRef()`. Com `pishell` a
apontar para memoria sem vtable, `r1` saia zero e o `bx` saltava para 0 -- que era
o `saiu_do_modulo_para_0x0` de 61 titulos.

E, pela mesma altura, todos os slots da tabela de ajudantes sem implementacao
passaram a receber um stub que **recusa** em vez de ficarem a zero.

## O mapa do que falta, medido (bateria, 62 titulos)

Depois de o `IShell` ganhar vtable e a assinatura do `CreateInstance` ser medida
(`r0=po, r1=pIShell, r2=ClsId, r3=ppApplet`), a bateria da isto:

    == carga 62 | ponteiro de modulo 48 | applet 1 ==

    IShell::slot2 (QueryInterface)     pedido 26x
        r1=0x01001001 AEECLSID_DISPLAY      22x
        r1=0x01001003 AEECLSID_FILEMGR       4x
    AEEHelperFuncs[0x09c] dbgprintf     pedido 16x
    AEEHelperFuncs[0x08c] GetAEEVersion  pedido 1x
    IShell::slot4                       pedido 1x

**O que o modulo pede ao sistema e, por esta ordem: `IShell::QueryInterface` por
um `IDisplay` (22 titulos) e por um `IFileMgr` (4), depois `dbgprintf` (16).**

## Ruling: a etapa 3 tem ordem medida

`IDisplay` antes de `IFileMgr` antes do resto -- e nao por intuicao, por 22 contra
4. Custo se errado: implementar uma interface que menos titulos usam primeiro.

## Erros meus, registados para nao os repetir

Quatro hipoteses erradas seguidas sobre a MESMA coisa (a assinatura do
`CreateInstance`). A que resolveu nao foi a quinta tentativa: foi **mudar de
instrumento** (a bateria) e **tracar registradores** em vez de os deduzir.

E tres defeitos de instrumento meus, todos encontrados ao usar: um stub generico
para 117 slots (44 titulos com pedidos sem nome), `substr(17)` num prefixo de 18
(todas as chaves com um espaco a frente, e a lista final vazia sem dar erro), e o
detalhe so com `r0` quando o que importa no `QueryInterface` e o `r1`.

## O numero, agora (bateria)

    carga 62 de 62 | ponteiro de modulo 48 | applet **22** (era 1)

`applet` quer dizer que o `IModule::CreateInstance` escreveu um ponteiro nao nulo.
**NAO quer dizer jogavel**: nada desenha, nada toca som, e nada disso esta
implementado. E um marco de ARRANQUE.

O que desbloqueou os 21: servir o `IShell::QueryInterface` com as duas interfaces
que a bateria mediu -- `AEECLSID_DISPLAY` (22 titulos) e `AEECLSID_FILEMGR` (4).

## Demanda por interface e por slot (a lista que dirige a etapa 3)

    IDisplay::slot3, slot4, slot5, slot7, slot19
    IFileMgr::slot2
    IShell::slot20, slot41
    AEEHelperFuncs[0x040], [0x0a8], [0x014] strlen, [0x08c] GetAEEVersion

## O erro mais reincidente desta sessao, registado

**Um teste generico a engolir um especifico.** Apareceu TRES vezes:
1. a ordem dos testes de descodificacao (bits 27-26 antes do bit 25) na CPU;
2. `idx >= kBaseDoShell` antes de `idx == kBaseDoShell + 2` no despacho;
3. no mesmo despacho, o `idx >= kBaseDoSlot` generico.

Sempre o mesmo sintoma: a funcionalidade parece nao estar implementada porque
outro ramo a apanha primeiro. Regra para o proximo codigo de despacho: **do mais
especifico para o mais generico, e um teste que verifique a ORDEM.**

## Estado, depois dos helpers

    carga 62 | ponteiro de modulo 48 | applet 22

Os helpers do sistema (`strlen`, `strcpy`, `strcmp`, `strchr`, `memset`,
`memmove`, `strtowstr`, `GetAEEVersion`, `aee_GetRand` deterministico) entraram e
**nao aumentaram os applets**. Aprofundaram a demanda: os titulos que paravam no
`strlen` agora param no `IFileMgr`. Os helpers eram pre-requisito, nao o
obstaculo.

## A proxima parede, com nome

    IFileMgr::slot2   r1=0x0013b9a0 r2=0x00000001
    IFileMgr::slot7   r1=0x0010b498
    IFileMgr::slot8   r1=0x00000000 r2=0xf0006d80
    IDisplay::slot3, slot4, slot5, slot7, slot19
    IShell::slot20, slot41

## O erro mais reincidente: QUATRO vezes, e agora impossivel por construcao

**Um passo generico a atropelar trabalho especifico.** As quatro:
1. e 2. a ordem dos testes de descodificacao (antes do bit 25) e o
   `idx >= kBaseDoShell` antes do especifico;
3. o `ConstruirShell` a escrever a base fixa;
4. o laco de preenchimento dos 117 slots a SOBRESCREVER os helpers ja escritos.

A correcao da quarta nao foi reordenar: foi uma **lista explicita** de slots
implementados que o laco generico consulta e salta. Regra para o proximo codigo:
**do mais especifico para o mais generico, e a lista de excepcoes e explicita em
vez de implicita.**

## Estado, depois do IFileMgr e do VFS

    carga 62 | ponteiro de modulo 48 | applet 22

Entrou o `IFileMgr` (ABI lida de `platform/deprecated/inc/AEEFile.h`, ordem do SDK
e nao copiada) e um VFS minimo de leitura sobre os ficheiros irmaos do modulo.

**A parede mudou de sitio e agora esta em tres interfaces:**

    IShell::slot12    r1=0x00000000 r2=0x80200048
    IDisplay::slot2   r1=0x00008000 / 0x00008001
    IDisplay::slot3, slot4, slot5, slot7, slot19
    IFileMgr::slot2, slot7
    IShell::slot20, slot41

## Motivos agrupados (o JSON agora leva o motivo)

    15 x orcamento_esgotado | create:saiu_do_modulo
    14 x orcamento_esgotado | sem_ponteiro_de_modulo
     8 x retornou | create:retornou            <- o modulo devolve sem criar
     6 x retornou | create:saiu_do_modulo_para_0x0
     6 x retornou | create:orcamento_esgotado

## Nota de honestidade

Estes 22 "applets" sao ponteiros nao nulos devolvidos pelo `CreateInstance`.
**Nada desenha, nada toca som, nada e jogavel** -- nada disso esta implementado.
O numero mede ARRANQUE, e o ledger diz-lo nesses termos em toda a parte.

## Laco de eventos (a peca que faltava para um jogo ANDAR)

`IShell::SetTimer` (slot 12) implementado, com um laco de eventos em tempo
VIRTUAL: 1 ms emulado por passo, temporizador cumprido quando vence, callback
chamado com o contexto no r0 e o par `(funcao, contexto)` lido dos dois primeiros
campos do `AEECallback`. O proprio callback re-arma o seguinte -- e assim que o
laco de quadro de um jogo se sustenta em BREW.

**A sentinela de retorno passou a ter dois significados** (retorno da entrada e
retorno de um callback de temporizador), distinguidos por uma bandeira. Sem isso
o primeiro callback seria lido como "o modulo retornou".

## Erro novo, e nao e o de ordem: confundir o endereco com a memoria

Escrevi `mem.Escrever32(s.Endereco(base + 12), ...)` -- isso escreve por cima do
PROPRIO endereco de saida, e nao da entrada da vtable. A entrada esta em
`mem[vtable + 12*4]`. Sintoma: a bateria a dizer "falta SetTimer" com o SetTimer
a funcionar. **Mesmo sintoma da quarta ocorrencia do erro de ordem, causa
diferente.**

## Estado, depois do IDisplay e do framebuffer

    carga 62 | ponteiro de modulo 48 | applet 22 | PIXELS 0

`IDisplay` implementado (ABI de `platform/ui/inc/AEEIDisplay.h`) sobre um
**framebuffer de software** de 640x480. Existe para haver uma medida VISUAL que
nao dependa de capturar ecra: quantos pixels distintos cada titulo escreveu.

`DrawText` foi chamado UMA vez em todo o corpus e nenhum pixel foi escrito. **Isso
significa que os titulos ainda nao chegam ao desenho** -- nao que o framebuffer
esteja errado. A medida visual esta montada e a zero.

## A demanda mais rica ate agora

    IShell::QueryInterface IID desconhecido   76x
        iid=0x0102c4e8 (SQLMgr)   66x
        iid=0x01001056 (SOUND)     4x
        iid=0x01001002 (HEAP) / 0x01001014 (FILE) / 0x01002001 (GRAPHICS)
        iid=0x01028e51 (ROOTFORM) / 0x0106c411 (HID)
    IShell::slot4 (QueryClass)   17x

## O metodo: uma verificacao que passa sem a mudanca nao e verificacao

Nesta ronda a substituicao de texto falhou em SILENCIO, e eu "verifiquei" com um
`grep -c` que contava `kVtableDisplay` -- que aparece nas linhas do
`ConstruirShell` de qualquer maneira. A contagem dava 4 com ou sem a mudanca.

Regra adoptada: **`assert` antes de escrever e `assert` depois, sobre um texto que
so existe se a mudanca entrou.** A contagem de um simbolo que ja la estava nao
verifica nada.

## O erro de ordem: SEIS vezes

1. e 2. os testes de descodificacao da CPU e o `idx >= kBaseDoShell`;
3. o `ConstruirShell` com base fixa;
4. o laco de preenchimento a sobrescrever os helpers;
5. o diagnostico `idx >= 1400 && idx < 1600` a engolir os especificos, deixando a
   lista de demanda VAZIA;
6. (o erro de endereco-vs-memoria tem o mesmo sintoma mas causa diferente).

**Regra, agora com seis casos por tras: no codigo de despacho, do mais especifico
para o mais generico, e a lista de excepcoes e explicita.**

## A ABI do IDisplay estava ERRADA, e era o defeito silencioso maior ate agora

Transcrevi de memoria e escrevi as assinaturas do BREW 4.x. As deste SDK
(`platform/ui/inc/AEEIDisplay.h`) sao outras. Tres eram silenciosas de verdade:

- `GetFontMetrics`: eu lia uma struct de metricas no r1; aqui o r1 e a FONTE e os
  ponteiros de saida sao r2/r3. Escrita no sitio errado, sem sintoma.
- `DrawText`: os argumentos 5 e 6 sao COORDENADAS, nao uma rect.
- `CreateDIBitmap`: `IDIB **ppIDIB` e o SEGUNDO argumento e o r0 e um codigo.

`BitBlt` tambem: a origem e um bloco cru de pixels, sem cabecalho de bitmap.

**Regra: a assinatura le-se do cabecalho, argumento a argumento, antes de
escrever a implementacao. Nao se transcreve de memoria.**

## A lista de demanda mentia, e a causa era estrutural

Um so objecto generico servia SETE interfaces, com uma so vtable: o `slot 7` era
`IHeap::slot7` ou `ISound::slot7` conforme quem chamou, e a bateria nomeava tudo
`IFileMgr::slot2007`. **A lista nao dizia que metodo cada titulo quer.** Agora um
objecto e uma vtable por interface.

## Duas listas que tem de concordar sao ZERO listas

O `aee_GetUpTimeMS` foi escrito na tabela mas nao acrescentado a lista de "quem ja
tem implementacao". O laco de preenchimento apagou-o -- sintoma identico ao do erro
que essa lista existia para evitar.

Substituidas por UMA tabela `{offset, saida}` de onde saem as escritas E a lista
de saltos. **Com uma so, e impossivel acrescentar uma implementacao sem que o laco
a respeite.**

## Verificar: `assert` antes e depois

Antes de escrever, `assert` de que a ancora existe uma so vez; depois, `assert`
sobre um texto que so existe se a mudanca entrou. Na primeira tentativa o assert
apanhou uma ancora errada (`kFmGetLastError` vs `kFmLastErr`) que teria passado em
silencio -- e na ronda anterior a "verificacao" por `grep -c` tinha passado sem a
mudanca.

## Um fantasma que nao existia

`r0=0xf0001060` num pedido de helper pareceu-me "enderecos de saida a escapar para
o guest". Sao REGISTOS RESIDUAIS: o `GetAppInstance` nao tem argumentos. Nao havia
fuga. **Antes de anunciar um defeito, verificar se o argumento e sequer usado pelo
metodo.**

E uma leitura deslocada por um fez-me crer que `0x0b0` era `aee_GetSeconds`. A
derivacao da struct inteira (117 slots, offsets a partir do inicio) confirma
`0x0b0 = aee_GetUpTimeMS`. Tabela em `/tmp/tab_helpers.json`.

## Uma terceira causa para o mesmo sintoma: a EDICAO

A escrita do `SetTimer` na vtable do `IShell` desapareceu numa edicao de texto. O
`SetTimer` continuava escrito e a funcionar -- o pedido chegava ao despacho -- mas a
entrada da vtable apontava para o stub que recusa, e a bateria voltou a dizer
"falta SetTimer".

Os sintomas das tres causas sao identicos e as causas sao diferentes:

1. um ramo generico antes do especifico (a ORDEM);
2. um laco de preenchimento a apagar implementacoes (a ORDEM outra vez);
3. uma edicao de texto que perdeu uma linha (a EDICAO).

Correccao: **toda a cablagem numa tabela `{objecto, slot, saida}` com um laco
unico, mais uma LEITURA DE VOLTA que aborta no arranque se uma entrada nao bater
certo.** Com uma so tabela nao se acrescenta uma implementacao sem a cablar; com a
leitura de volta, uma cablagem perdida custa 1 segundo em vez de 4 minutos.

## A guarda certa no proposito e errada na conta

A guarda de limites disparou no primeiro arranque (`slot 7 de 0x00002468 sai da
vtable`). A cablagem estava certa: a guarda comparava com a base da PRIMEIRA vtable
generica, e cada uma tem 64 slots -- o fim de uma e o inicio da seguinte.

**Distinguir "a guarda esta errada no proposito" de "a guarda esta errada na
conta".** Uma guarda que aborta por engano custa um minuto. Uma guarda que nao
aborta quando devia custa uma bateria inteira.

## Nomes vindos do cabecalho, um a um

    IHIDDevice slot  7 = GetNumberOfButtons      (declarado: 14)
    IHIDDevice slot 14 = RegisterForPositionChange
    IDisplay   slot 15 = SetDestination(IBitmap*)
    IDisplay   slot 16 = GetDestination(void) -> IBitmap*
    IFileMgr   slot  7 = RmDir(const char*)
    IHeap      slot  7 = Walk  (AEEIHeap1.h, INHERIT_IHeap1) -- DEPURACAO de heap

`RmDir` recusa: a VFS desta etapa e SO DE LEITURA, e e deliberado -- um jogo que
apague um ficheiro do modulo destroi a reprodutibilidade.

`SetDestination` so aceita um bitmap NOSSO: aceitar um ponteiro qualquer poria o
desenho num sitio que nao existe.

## `INHERIT_IBase` tem DOIS membros -- e TODOS os slots estavam errados

    #define INHERIT_IBase(iname) \
       uint32 (*AddRef)(iname*); \
       uint32 (*Release)(iname*)

**Nao ha `QueryInterface` em `INHERIT_IBase`.** Eu contava TRES membros e portanto
todos os numeros de slot que usei estavam errados.

O sintoma que o obrigou a aparecer foi o `pacmania`:

    00109d5c  ldr r1, [r0]        ; a vtable
    00109d60  ldr ip, [r1, #8]    ; <<< INDICE 2
    00109d64  mov r1, r6          ; AEE_FONT_BOLD
    00109d68  blx ip              ; (po, fonte, &asc, &desc)

Assinatura exata do `GetFontMetrics`, que o cabecalho poe no slot 2. **O jogo estava
certo e eu errado.**

**Correcao estrutural: `tools/gerar_slots.py` le os cabecalhos e escreve
`tools/brew_slots.inc`.**

    kShell_CreateInstance = 2   (eu chamava-lhe "QueryInterface")
    kShell_QueryClass     = 3   (eu tinha 4 = GetDeviceInfo)
    kShell_SetTimer       = 11  (eu tinha 12 = CancelTimer)
    kDisplay_DrawText     = 4   (eu tinha 5)
    kFileMgr_OpenFile     = 2   -- o `IFileMgr::slot2` que a bateria pedia

**Um numero escrito a mao ja divergiu uma vez, e o custo foi uma ronda.** Agora o
gerador e a unica fonte.

## Uma guarda construida sobre um numero errado recusa o que esta certo

A guarda de cablagem dizia `slot < 3 = IBase`. Com a IBase a ser 0 e 1, recusou o
`GetFontMetrics` no slot 2 -- **que era o pedido certo**. Passou a `< 2`.

## Um limite verificado so no destino nao limita o trabalho

O `Retangulo` do framebuffer percorria `w*h`; o `Ponto` recusava o que saia do ecra,
mas **o laco corria na mesma**. Uma rect grande vinda do guest: mais de 900 segundos
para UM titulo. Cortando antes do laco: **7 segundos**.

E um limite de PASSOS nao serve como orcamento, porque o custo por passo depende do
titulo e quem paga e o INSTRUMENTO. `kOrcamentoSegundos = 25` por fase, com o motivo
registado -- **um titulo que bate no orcamento nao falhou: ainda estava a andar.**

## Verificar a CONTAGEM DE LINHAS depois de cada escrita

Escrevi por cima do `bateria.cpp` com uma versao TRUNCADA: 412 linhas onde havia
1295. O `.bak` guardava o mesmo conteudo truncado (foi escrito do mesmo objecto ja
errado). Nao houve aviso: um ficheiro truncado continua a ser um ficheiro valido em
Python. Salvou-me o `git`, e so porque o commit anterior estava feito.

**Depois de cada `write_text`, comparar a contagem de linhas com a esperada.** E a
unica verificacao que apanha uma escrita que apaga.

## O gerador de slots, e os tres defeitos dele

`tools/gerar_slots.py` resolve a CADEIA de heranca a partir dos cabecalhos:

    INHERIT_IBase       = 2   (AddRef, Release) -- e SO estes dois
    INHERIT_IQI         = 3   (mais o QueryInterface, que NAO esta na IBase)
    INHERIT_IAStream    = IBase + 3 = 5
    INHERIT_IFile       = IAStream + 7 = 12
    INHERIT_IRealloc    = IQI + 4 = 7
    INHERIT_IHeap1      = IRealloc + 8 = 15

O gerador teve TRES defeitos meus, todos do mesmo tipo:

1. O `void (*pfn)(void *)` DENTRO dos parametros do `SetTimer` contado como membro
   -- empurrava o `CancelTimer` para o 13.
2. O ULTIMO membro de um macro NAO TEM `;`, e o padrao exigia-o -- o `Cancel` do
   `IAStream` nao existia e o `IFile` saia com 4 slots de cabeca em vez de 5.
3. A cabeca procurada no CORPO, quando esta na primeira linha do macro.

**Um falso positivo no gerador e pior do que um numero escrito a mao: parece que
foi medido.**

## Uma so regra de normalizacao de caminhos

`OpenFile` usa a MESMA VFS que o `Test` -- nao ha duas verdades sobre que ficheiros
existem. `_OFM_CREATE` e `_OFM_APPEND` sao recusados em voz alta: a VFS e so de
leitura por DECISAO (um jogo que escreva no modulo destroi a reprodutibilidade),
nao por falta.

## Se a guarda proibe o que e preciso, a excepcao e EXPLICITA

O objecto FICHEIRO nao tem `Release`, e o jogo fecha ficheiros com `IFILE_Release`
(slot 1). A guarda de IBase recusou-o -- corretamente, porque para os objectos do
`ConstruirShell` escrever no slot 1 destroi a IBase. A excepcao ficou nomeada
(`e_o_ficheiro`), com o motivo escrito.

## Delegacao: cinco frentes em paralelo, com o desenho lido ANTES

Antes de delegar, li o `DESIGN.md` (P1-P7) e o `PLAN.md` (etapas 0-9), e copiei as
partes relevantes PARA DENTRO de cada tarefa -- um sub-agente que nao le o desenho
trabalha contra ele. Cada tarefa leva: os sete principios, o laco de trabalho, os
caminhos exatos, o que NAO fazer, e o formato do relatorio.

**Uma arvore quebrada multiplica-se pelo numero de sub-agentes.** Antes de os
lancar corri a bateria: `carga 62 | modulo 48 | applet 22`, 87 testes verdes. E
ainda bem -- porque a migracao do despacho tinha deixado TRES defeitos meus:

1. `Correr` caia fora do fim sem `return` -> comportamento indefinido numa funcao
   que devolve por valor -> `free(): double free detected`, com a pilha a apontar
   para o `append` de uma cadeia VIZINHA. **O `append` era a vitima.**
2. Chave de abertura duplicada no `Correr`.
3. **REGESSAO `applet 22 -> 1`**: escrevi `kIidDisplay = 0x01003003` de memoria,
   quando o valor medido e `0x01001001` -- e estava escrito no ficheiro que eu
   estava a migrar, a quatro linhas do sitio.

As cinco frentes, cada uma na sua worktree (sem colisao de ficheiros nem de
compilacao):

    igl          etapa 6, o IGL       (a arvore antiga tinha 71/84 handlers sem log)
    imedia       etapa 5, media+audio (MM_STATUS_DONE, notificacao a mais/menos)
    hid-entrada  etapa 8, IHIDDevice  (UIDs medidos; cabeca IQI = 3 slots)
    formato-bar  etapa 3, o `.bar`    (pacmania: pacmania.bar, id 0x13e3)
    regressoes   etapa 9, comparar    (multiplica o valor de tudo o resto)

## Etapa 9 ENTREGUE, e o juiz viu que ia julgar a coisa errada

O sub-agente `regressoes` entregou `tools/comparar` + `tools/regressao.sh`
(branch `comparar`, commit `1c4a61c`), com 19 testes gtest, 8 verificacoes pelo
binario dentro do ctest, e **4 guardas quebradas de proposito uma a uma** -- mais
uma violacao no proprio emulador (o `QueryInterface` deixa de responder ao
`AEECLSID_DISPLAY`): `applet 22 -> 1`, 21 regressoes, codigo de saida 1. Depois de
restaurar, verde.

Tracejou a ancora da referencia para `tools/baseline/bateria.json`, VERSIONADA, e
recusou a auto-atualizacao: *"um instrumento que se auto-atualiza quando falha
transforma toda a regressao num 'sem regressoes'"*.

## DOIS DEFEITOS MEUS, achados pelo agente que construiu o juiz

**1. O campo `vtable` estava MORTO, e eu lia-o como medicao.**

`e.tamanho` nunca era atribuido -- a linha foi removida no commit `d75281d` --
e o `vtable` compara `alvo >= kBase && alvo < kBase + e.tamanho`. Com o tamanho a
zero isso e SEMPRE falso: **um dos quatro degraus do arranque estava morto**, e a
bateria imprimia `vtable NAO` nos 62 titulos.

Eu lia `vtable NAO` como "os modulos nao tem vtable" em vez de "o campo nao mede
nada". **Um campo que nao mede e pior do que um campo ausente: um ausente nao se
le.** Corrigido: `vtable` verdadeiro em **48 de 62**, a coincidir com os 48 modulos.

**2. O orcamento de fase media a CARGA DA MAQUINA.** Usava
`std::chrono::steady_clock` do hospedeiro -- violacao do P4, latente enquanto o
limite de passos dominou (os 36 titulos com `orcamento_esgotado` param em 4000000
passos, nenhum pelo relogio). No dia em que um titulo ficasse mais lento por passo,
a bateria acusaria regressoes de JOGABILIDADE que eram de CARGA -- **e o
`tools/comparar` acreditaria nelas.** Removido.

**O agente que constroi o juiz foi quem viu que o juiz ia julgar a coisa errada.**

## E o comparador validou a correcao sozinho

Depois de corrigir, `./tools/regressao.sh` deu **0 regressoes e 48 MELHORIAS** --
exatamente os 48 titulos cujo `vtable` passou de falso a verdadeiro. E classificou
`tamanho` como **NEUTRO**, sem o inventar como melhoria.

Baseline atualizado com `--atualizar` explicito. `ctest`: **5/5**.

## PENDENTE, pedido pelo agente (e concordo)

**Nenhum campo de PROVENIENCIA no JSON.** O comparador deriva a configuracao da
lista pasta/mod, logo um corpus diferente que mantenha os mesmos 62 pasta/mod NAO e
detetavel. Falta um bloco de cabecalho com `corpus_sha256`, `titulos` e o commit.
A guarda de configuracao e real mas incompleta.

## Onde as coisas estao, ao fim de 81 commits numa ronda

### Os numeros

    carga 62 | modulo 62 | applet 37 | DESENHA 0
    409 testes | ctest 9/9 | 25 541 linhas

### As seis frentes que entregaram

| frente | resultado |
|---|---|
| `parede-pilha` | **21 -> 0** titulos a saltar para a PILHA |
| `descodificador` | **66 376 -> 2 838** palavras executadas como outra instrucao (**-95,7%**) |
| `auditoria-stubs` | 98 achados; `vsnprintf` (116x) e `realloc` (9x) servidos |
| `auditoria-plano` | 45 mutantes; 36 testes provados por violacao |
| `auditoria-zeebx` | 17 afirmacoes verificadas; o `BMPIds.csv` com 25 847 CLSIDs |
| `auditoria-testes` | o plano etapa a etapa; 8 testes fracos COM prova |

### A parede que fica, e porque acredito que e esta

**A area de rascunho do carregador cai DENTRO da imagem do modulo em 29 dos 62 titulos.**
Com a base a ZERO, 29 tem imagem maior que `0x90010`; o carregador escreve o ponteiro do
modulo em cima do CODIGO, e nada acusa.

E a razao de eu acreditar: **o `descodificador` fechou 95,7% das divergencias silenciosas
-- incluindo o `ldrd`, o `strb` do Thumb e uma guarda MORTA -- e NENHUM titulo mudou de
estado.** A parede esta no ESTADO com que o titulo entra.

DUAS TENTATIVAS DE ARRANJO, ambas medidas, ambas PIORES:

    0x00090000 (o original)  -> modulo 62 | applet 37
    0x81000000               -> modulo 48 | applet 38
    heap do alocador         -> modulo 48 | applet 38

**Qualquer endereco que nao seja o antigo perde 14 titulos.** O endereco do rascunho NAO E
NOSSO para escolher -- o carregador do GUEST decide algo com ele. Falta medir: **o que o
guest escreve e le em 0x90000.**

### Defeitos meus, achados pelos agentes

1. **O `applet 41` estava INFLADO** -- lixo no slot de saida contado como applet. O honesto
   e **37**. Mesma classe do `tamanho` que esteve a zero.
2. **`Despacho::Faltas()` era um mapa NUNCA escrito** -- quem o lesse concluia "nao falta
   nada". Terceira ocorrencia da classe. Removido, com os outros acessores mortos.
3. **11 sitios imprimiam DECIMAL com prefixo `0x`** -- `saiu_do_modulo_para_0x2148007884`
   era `0x8007FFCC`, a pilha. **Envenenava o LEDGER.**
4. **O rotulo `build` da referencia MENTIA** (dizia `eb62459`, os dados vinham de
   `0286921`). Passou a haver `binario_sha256`.
5. **A assinatura do `SetTimer`** -- impedia QUALQUER titulo de armar o laco de quadro.
6. **O teste `CablarDetetaUmaCablagemPerdida`** mentia no nome: verde com a leitura de
   volta arrancada. Renomeado.
7. **A contagem de faltas guardada ANTES do `EVT_APP_START`** -- o `IRootForm` era invisivel
   na bateria.
8. **O meu edit truncou o `bateria.cpp` DUAS vezes.** A regra que eu proprio escrevi --
   verificar a contagem de linhas -- foi o que salvou as duas.

### Publicado

    github.com/requeijaum/curupira    publico | MIT | 84 commits

O fork (`research/sources/zeebulator/`) fica GPLv3. **O `curupira/` ainda nao foi removido
do fork** -- esta adiado enquanto houver worktree de agente assente nele.

## Estado verificado

- `ctest`: **777 testes verdes** (ultima corrida nesta sessao).
- `tools/verificar_memoria.sh`: **777 testes sob valgrind, 0 erros**.
- Corpus: 62 titulos em `research/sources/scripts/corpus62.json`.
- Bateria com log maximo concluida (62/62). Resultado: 41 desenharam, 21 sem
  desenho, 5 travamentos de guest, 12 recusaram entrada. Logs em `/tmp/smoke3/`.

## Fechado nesta sessao (com commit)

| item | commit | prova |
|---|---|---|
| GpuLog em 56 handlers de GL que nao tinham log | `9aabf96` | 71 de 84 nao logavam |
| slots da C stdlib 0xd8/0xe8 e 0x20/0x13c estavam trocados | `23d3f7c` | testes `Strstr*` testavam o stristr |
| 7 slots de GL ligados (cullFace 86377 chamadas, etc.) | `9aabf96`+ | A/B no `alpineracerex`: 2872 vs 2828 cores |
| `NAND 1.1.2`: contrato do SIMCARDCTL (`VerifyPin_cb`, AUTHORIZED/INVALID) | `dad236a` | strings do `1.1.2_APPS.bin` |
| canal de controle responde com o laco preso (`alive`) | `54e5461` | resposta em 0,00 s; antes silencio |
| `Stop` do IMedia avisa com `MM_STATUS_DONE` | `d54503d` | tratador do `cnk2` em `0x00101a80` |
| UIDs medidos: eixos confirmados, d-pad e UM controlo | `a6c24c9` | 24-38 titulos embutem as constantes |
| correlacao No-Intro -> pasta/mod (62 nomes reais) | (tabela em `/tmp/no_intro_tab.json`) | cada zip tem `mif/<pasta>.mif` |

## Refutado por medicao (nao mexer outra vez sem prova nova)

- **Caminho de ficheiro entrega dados errados**: bytes identicos ao `pak0.pakz`
  real em 4 offsets (`ZEEB_LOG_FILE_BYTES=1`).
- **Ordem de leitura do buffer de midia**: 105 chamadas em 4 titulos, zero
  mudancas. A alegacao do zeebx nao se reproduz.
- **Espera ocupada no relogio** (Zeebo Extreme): `aee_GetUpTimeMS` com ZERO
  chamadas no perfil do `Rolimaz`.
- **Transbordo de `strcpy`**: zero copias acima de 4096 bytes em 8 titulos.
- **HID**: nenhuma chamada no caminho quente do `funsoccer`.
- **`SIMCARDCTL` com resultado nao-zero**: regride (tick 3). Com zero: neutro.

## Em aberto, por ordem de valor

1. **`funsoccer` (= Zeebo F.C. Super League) e a familia Zeebo Extreme** gastam
   o orcamento dentro da MESMA rotina de biblioteca de copia de bytes
   (`funsoccer` 0x130530, `Boiaz` 0x107e50). Proximo passo: identificar a rotina
   procurando o padrao de bytes nos outros 60 modulos e nomeando-a pelo uso.
2. **Canal de controle**: 12 titulos ainda nao aceitam entrada (o `alive` diz que
   estao presos no guest, mas o jogo nao recebe).
3. **d-pad**: a medicao diz que e UM controlo com valor de direcao; falta
   determinar o formato (injetar `0x0106C3FE` com 0..11 e ver o menu mexer).
4. **4 slots de GL sem handler**: `glScissor` 4057, `glColorMask` 2940,
   `glFlush` 1740, `glPolygonOffsetx` 80.
5. **8 classes recusadas** (`CCipherFactory` 3 titulos, `ENH`, `CMD5Ctx`, `NET`,
   `0x010292c3`). Duas ja registadas sem medir.
6. **Correlacao No-Intro na GUI** e **config por jogo editavel** -- pedidos
   explicitos do dono.

## Rulings (decisoes tomadas sozinho, com o custo de errar)

- **Nao mexer no modelo do d-pad** antes de saber o formato do valor. Custo se
  estiver errado: nenhum -- a informacao vem de graca do passo 3.
- **Nao aplicar as correcoes do zeebx por citacao.** Quatro das dez alegacoes
  auditadas eram falsas para o nosso corpus. Custo se estiver errado: perco
  tempo a re-medir o que ja estava certo.
- **Nao publicar** sem autorizacao. Custo se estiver errado: nada sai.
- **Registar as hipoteses refutadas** no artigo, com o numero que as matou.
  Custo se estiver errado: artigo mais longo.

## Ficheiros de referencia que sobrevivem a compactacao

- `docs/AUDITORIA-ZEEBX-v0.1.0.md` -- auditoria dos 12 commits + Zeebo Extreme
- `docs/SIMCARDCTL-CONTRATO-DO-OEM.md`
- `docs/PAREAMENTO-DE-UIDS-MEDIDO.md`
- `docs/DIAGNOSTICO-DE-VIVACIDADE.md`
- `docs/INVESTIGACAO-POR-SUBSISTEMA.md`
- `docs/LACUNAS-2026-09-13.md`
- `docs/ESTUDO-DE-SKILLS-2026-09-14.md` -- este estudo


- `docs/medicoes/no_intro_tab.json` -- tabela No-Intro (62 nomes reais)
- `docs/medicoes/2026-09-13-bateria-log-maximo/` -- os 62 logs da bateria comprimidos
- `docs/medicoes/estudo-skills-2026-09-14/` -- os dois digestos do estudo de skills

**Higiene feita**: os ficheiros que estavam em tmpfs (tabela No-Intro, 62 logs,
digestos) foram copiados para dentro do repo. `/tmp` e tmpfs e perde-se no reboot.


---

## Frente `raster` (etapa 3/6 do plano): O RASTERIZADOR -- os primeiros pixels

Worktree `/tmp/wt-raster`, branch `raster`, base `ecc97b0`. Entregue em dois
commits: o rasterizador (arvore nova) e o REMENDO dos ficheiros partilhados.

### O numero, antes -> depois, e o comando

    ./build/zb2_bateria "$corpus" "$mods" /tmp/raster/antes.json     # antes
    ./build/zb2_bateria "$corpus" "$mods" /tmp/raster/depois.json    # depois
    ./build/zb2_comparar /tmp/antes.json /tmp/depois.json

    PIXELS 0 -> 0 em 62 de 62 | CORES maximo 1 -> 1 | carga 62 | modulo 62 | applet 41
    comparar: 0 regressoes, 0 melhorias -- e a falta `rasterizador_de_GL` (124x -> 0)

**O ALVO NAO FOI ATINGIDO, E A RAZAO E MEDIDA, e nao uma desculpa.** O alvo era
`PIXELS > 0` num titulo com `CORES > 10`.

### O que CONTRADIZ o que se pensava: as 124 chamadas de GL nao chegavam

As "124x" da lista de demanda sao **exactamente 2 registos de instalacao x 62
titulos**: o `Igl::Instalar` e o `Egl::Instalar` registavam a falta
`rasterizador_de_GL` uma vez cada, no arranque de cada titulo. Nao sao 124
chamadas de jogos.

Medido com o traco CRU dos 62 titulos (`ZB2_TRACE=1`, 454 MB de log):

| evento de video em 62 titulos | quantas vezes |
|---|---|
| `GL_CABLADO` (instalacao) | 62 |
| `IGL_INSTALADO` / `IEGL_INSTALADO` | 62 / 62 |
| `GL_AddRef` -- o **unico** metodo de GL que um titulo chama | 19 998 (todos do `pbc`) |
| qualquer outro `gl*` (incluindo `glDrawArrays`) | **0** |

O `pbc` chama `GL_AddRef` 19 998 vezes de `lr=0xf001d4c0` (o proprio endereco do
slot 0) ate ao limite `saidas > 20000` do despacho: e o `laco_de_saidas` que a
bateria ja registava. **Nenhum titulo chega a desenhar.**

### A parede, agora com nome: o guest NAO CHEGA ao codigo de desenho

Os quatro titulos com o wrapper de GL estatico (medidos pelo sub-agente `igl`:
`chessbots`, `ddragonz`, `tectoy`, `nfs`) nao alcancam o `GLES_Init` nas fases
que a bateria corre:

| titulo | create | start | quadro |
|---|---|---|---|
| `chessbots` | 4 000 000 passos, `orcamento_esgotado`, **sem applet** | -- | -- |
| `ddragonz` | 177 passos, `saiu_do_modulo_para_0xe3a03000` | `retornou` | 1 quadro, sai para `0xe5940028` |
| `tectoy` | 255 passos, `retornou` | `retornou` | 0 quadros (nenhum temporizador armado) |
| `nfs` | 10 passos, `saiu_do_modulo` | `sem_handle_event` | -- |

Nenhum pedido de servico em falta aparece nestes titulos (a lista de faltas do
`ddragonz` e vazia): o guest sai do modulo sem pedir nada que falte. O PC para
onde ele sai, por familia, nos 62 titulos:

| para onde o PC sai | titulos | exemplo |
|---|---|---|
| area da PILHA (`0x8007ffc0..0x8007ffcc`) | 20 | `alice`, `AirRacez`, `zeebotennis` |
| dentro do modulo (0..16 MB) | 30 | `cnk2` `0x9e8ec`, `cninja` `0x2b2db0` |
| HEAP, no ponteiro do `AEEMod_Load` (`0x80200010`) | 4 | `zeebo_app` |
| palavra de INSTRUCAO ARM (`0xe3a03000` = `mov r3,#0` 2x, `0xe3a06000`, `0xea00000e` = `b`) | 4 | `ddragonz`, `zeebo_app`, `tekken2`, `ridgeracer` |
| dados usados como endereco (`0x57455242` = "BREM" 2x, `0xff00f20e`, `0x140003e`) | 4 | `quake2brew` |

**Hipotese (NAO provada): um valor de DADOS esta a ser usado como ponteiro de
funcao** -- o guest le de um sitio onde o emulador escreveu dados (o cabecalho do
modulo, a pilha, uma palavra de codigo) e salta para la. O `0x57455242` e a
assinatura "BREW" do cabecalho lida como endereco. Isto esta no caminho da CPU e
do despacho, e nao no do rasterizador: **quem o tiver de resolver precisa da
sonda que traca as ultimas instrucoes antes da saida** (nao foi feita).

### O que foi entregue, e como esta provado

`core/video/rasterizador.{h,cpp}` -- NOVO. TRIANGLES/STRIP/FAN por varrimento de
caixa delimitadora com funcoes de aresta e a regra "top-left"; cor por vertice
interpolada; textura GL_NEAREST com clamp; teste de profundidade; descarte de
faces. Escreve na `core/brew/tela.h` (a mesma tela que a bateria le, linha 794) e
nao num buffer proprio. O que ficou FORA esta escrito no topo do ficheiro, com o
que cada omissao custa: afinidade sem correccao de perspectiva, sem recorte de
frustum, sem blending/stencil/mipmaps/bilinear/neblina/iluminacao/scissor/dither.

**17 testes** em `tests/rasterizador_test.cpp` (351 no total, era 335) e **10
guardas provadas por violacao deliberada** (`tools/provar_guardas_raster.py`,
10 de 10 VERMELHAS): a regra do canto, a inversao do y, os pesos da cor, o teste
de profundidade, a coordenada de textura, o descarte de faces, o limite da janela,
o `glClear` que poe o clip de lado, a primitiva sem caminho e a recusa sem tela.

Duas licoes de teste, das violacoes: (1) o teste da cor interpolada passou VERDE
com os pesos trocados porque **dois vertices tinham a MESMA cor** -- o teste tinha
de distinguir, e agora tem tres cores diferentes; (2) o teste da janela passou
VERDE com o limite quebrado porque a geometria nao saia da janela -- um teste que
nao exercita o que a guarda protege nao e guarda.

### A prova de ponta a ponta que EXISTE (e o que ela NAO e)

`tools/sonda_gl` chama os thunks de GL **do proprio titulo** (achados por
varrimento da imagem) e o pedido percorre a vtable, a faixa de saida, o despacho e
a Tela. Com a bancada a pedir um triangulo a serio:

    IGL 26 glDrawArrays  CHEGOU  feito: 3 vertices -> 38400 pixels em 1 triangulos
    PIXELS: 345600 | CORES: 2      (ddragonz.mod)

A GEOMETRIA E DA BANCADA: nao e um titulo a desenhar, e por isso nao substitui o
`PIXELS` da bateria. Antes desta frente, a bancada escrevia nos vertices o valor
`0x3F800000` (1.0f) TRES vezes: um triangulo de area ZERO, e a sonda imprimia
"PIXELS: 0" sem distinguir "nao ha rasterizador" de "a bancada pediu um triangulo
degenerado". Os dois numeros iguais com causas diferentes -- **o defeito que esta
arvore persegue** -- estavam dentro do proprio instrumento.

### Ruling: a tela liga-se por fora, e o remendo tem de ser aplicado

O `Igl` nao tem, e nao pode ter, a `Tela`: ela vive no `Despacho` (ficheiro
partilhado). Sem `igl_.DefinirTela(&tela_)` o desenho e a limpeza **RECUSAM** com
o motivo escrito ("o IGL NAO TEM TELA LIGADA") -- recusa honesta, e nenhum pixel.
`tools/remendo_raster.py` aplica as duas alteracoes partilhadas com `assert` antes
e depois (uma ancora em falta nao escreve nada; foi ele que apanhou um erro MEU de
aritmetica de linhas: 3 e 5 em vez de 6). Custo se estiver errado: nenhum -- o
numero nao muda, e a recusa di-lo.

### Ficheiros partilhados que a frente tocou (para o merge)

| ficheiro | alteracao | porque |
|---|---|---|
| `core/brew/despacho.cpp` | `igl_.DefinirTela(&tela_);` (1 linha) | ligar a superficie ao IGL |
| `core/brew/egl.cpp` | a falta `rasterizador_de_GL` -> evento de informacao | era VERDADE e passaria a MENTIRA no mesmo commit (P7) |
| `core/brew/igl.cpp` | o desenho e a limpeza chamam o rasterizador | e o ponto de entrada, como estava planeado |
| `tests/igl_test.cpp` | 2 testes mudaram de nome e de assert | afirmavam "sem rasterizador", que deixou de ser verdade |
| `tools/sonda_gl.cpp` | a linha dos PIXELS passa a ser o numero real | "PIXELS: 0" era verdade e deixou de ser |

Aviso de merge: o `despacho.h` tem um `-Wreorder` (o `widgets_` e inicializado
antes do `igl_`), anterior a esta frente.

### Registado para nao repetir

O erro que mais tempo custou nesta frente nao foi no rasterizador: foi **ler a
lista de demanda da bateria como se fosse o que os titulos pedem**. A linha
`rasterizador_de_GL pedido 124x` e um sinal do modulo a instalar-se, e foi lida
como 124 pedidos de jogos. **Um contador de faltas de INSTALACAO e um pedido de
EXECUCAO contam-se no mesmo mapa** -- e foi preciso o traco cru de 62 titulos
(454 MB) para os separar.
# Ledger -- trabalho no Zeebulator

Formato copiado de `obra/superpowers` (`subagent-driven-development`). MOTIVO:
"conversation memory does not survive compaction"; apos uma compactacao, confiar
neste ficheiro e no `git log` acima da propria memoria.

Regra de escrita: cada linha nomeia factos que existem fora da minha cabeca --
commits, caminhos, numeros medidos. Quando uma decisao foi tomada sozinho, entra
como `Ruling:` com o custo de errar.

Repositorio: `/home/rafaelfrequiao/projects/zeebo-emulator` (R).
Emulador: `research/sources/zeebulator` (ZB). Remoto `zeebulator`.

---

## Objetivo maior (do dono do projeto)

Equiparar o Zeebulator ao zeebx POR MEDICAO, nunca por citacao. FASE 0 (bateria
comparativa) **continua sem autorizacao explicita** -- sem ela, "equiparado" nao
tem numero. Pedida uma vez; nao repetir o pedido.

## Rewrite (branch `full-rewrite`)

Desenho: `docs/rewrite/DESIGN.md` (7 principios). Plano: `docs/rewrite/PLAN.md`
(10 etapas). Arvore: `src2/`, com o seu proprio CMake e build.

| etapa | estado | prova |
|---|---|---|
| 0 -- fundacao de medicao (`core/tempo`, `core/traco`, `core/memoria`) | **FECHADA** | 20 testes verdes, 3 guardrails provados por violacao deliberada, 0 avisos |
| 1 -- CPU de referencia (ARM + Thumb) | **FECHADA** | 47 testes verdes, 4 guardrails provados por violacao deliberada, 6 bugs reais encontrados |
| 2 -- carregador de MOD + primeiro titulo (`imicro3d`) | **PARCIAL** | carrega, `AEEMod_Load` corre e devolve ponteiro nao nulo; estrutura do modulo verificada campo a campo. Falta `EVT_APP_START` |

Ruling: a arvore nova vive em `src2/` com CMake proprio, e nao substitui a
antiga. Custo se errado: duas arvores para manter ate a nova provar valor.
Custo se tivesse substituido: perder a unica referencia que funciona.

## Etapa 1 -- o que os testes revelaram

Seis bugs reais no interpretador, encontrados pelos testes e nao por leitura:
ordem do `MLA` (`Rn*Rm + Rs`, nao `Rm*Rs + Rn`); bandeiras N/Z suprimidas por um
`carry_ja_posto` mal delimitado; o bloco (`LDM`/`STM`) nao avancava o PC; a
mascara do `MUL` apanhava o `UMULL` (separa-os o bit 23); faltava o +8 no acesso
relativo ao PC; e o despachante testava os bits 27-26 antes do bit 25.

E cinco erros MEUS, no teste e nao no emulador: encodings ARM escritos a mao com
campos trocados, ordem de preparacao invalida, expectativa errada de carry (o C
e do lado sem sinal), `LDR` de literal sobreposto, e uma afirmacao contraditoria.

Ruling: nos testes, as instrucoes ARM passam a ser construidas por um
**montador minimo de campos nomeados**, e nao por literais hexadecimais. Custo se
errado: ~40 linhas de teste a mais. Custo se nao fosse feito: uma classe inteira
de erro em que o TESTE acusa o EMULADOR -- que ja custou varias rondas hoje.

## Etapa 2 -- ate onde chegou, medido

O `imicro3d.mod` (90068 bytes) carrega, o `AEEMod_Load` corre **60 instrucoes**,
pede **um** bloco de **36** bytes ao `malloc` (nSize 20 + `sizeof(IModuleVtbl)`
16) e **devolve um ponteiro de modulo nao nulo** (`0x80200010`). Nenhuma
instrucao recusada.

**Falta**: `IModule::CreateInstance` (vtable do modulo, slot 2) e
`HandleEvent(EVT_APP_START)`. Nao esta feito.

A sonda `tools/sonda_mod` mede cada passo e diz qual e a proxima funcao do
sistema que falta -- foi isso que tornou a etapa possivel.

Ruling: a faixa de saida para C++ e a mesma da arvore antiga (`0xF0000000`), com
um endereco executavel por slot, e `Correr` para quando o PC entra nela. Custo se
errado: um endereco a mais por slot. Custo se nao fosse feito: um ponteiro de
funcao do guest que nao e executavel, e um `bx` para memoria que nao existe.

## Etapa 2 -- a questao aberta, com precisao

**Pergunta**: quem escreve `module+12` (o `CreateInstance` do APPLET)?

E o que falta para o `IModule::CreateInstance` funcionar: ele le `+12`, encontra
zero e sai por falha, sem criar o applet.

**O que JA se sabe, medido**: `module+12 == 0` depois do `AEEMod_Load` e o
comportamento CORRECTO do modulo -- o proprio `AEEMod_Load` empurra zero para os
argumentos 5 e 6 da pilha, que sao os que `AEEStaticMod_New` escreve em `+12` e
`+16` (`stmib r0, {r1, r6, r7, r8}` em 0x0010077c). Nao e memoria por preencher,
e nao e um argumento em falta na nossa chamada.

**Ruling**: o proximo passo NAO e "implementar mais slots da tabela". E procurar
quem escreve `+12` -- provavelmente um caminho de registo do applet que corre
antes do `EVT_APP_START`. Custo se errado: um par de horas. Custo de continuar a
acrescentar slots: acumular codigo que nao e o que falta.

## Bateria (etapa 4, adiantada) -- numeros por titulo

`tools/bateria` corre os 62 titulos e regista um estado medido por titulo.
Trazida para antes das etapas 3-4 porque, ao fim de tres hipoteses falhadas sobre
o MESMO modulo (`imicro3d`), a regra mandou mudar de instrumento em vez de tentar
a quarta.

| metrica | valor |
|---|---|
| titulos que carregam | **62 de 62** |
| com ponteiro de modulo | **48** (era 1) |
| com applet | **0** |
| esgotam o orcamento na carga | 14 (inclui a familia Neo Geo inteira) |

**A mudanca que valeu 47 titulos**: o `IShell` passou a ser um objecto a serio --
um endereco cujo primeiro campo e uma vtable. Medido no desmonte do `peggle`
(0x0010278c): depois do `malloc`, o primeiro que TODO modulo faz e
`ldr r0,[pishell] / ldr r1,[r0] / bx r1` = `shell->AddRef()`. Com `pishell` a
apontar para memoria sem vtable, `r1` saia zero e o `bx` saltava para 0 -- que era
o `saiu_do_modulo_para_0x0` de 61 titulos.

E, pela mesma altura, todos os slots da tabela de ajudantes sem implementacao
passaram a receber um stub que **recusa** em vez de ficarem a zero.

## O mapa do que falta, medido (bateria, 62 titulos)

Depois de o `IShell` ganhar vtable e a assinatura do `CreateInstance` ser medida
(`r0=po, r1=pIShell, r2=ClsId, r3=ppApplet`), a bateria da isto:

    == carga 62 | ponteiro de modulo 48 | applet 1 ==

    IShell::slot2 (QueryInterface)     pedido 26x
        r1=0x01001001 AEECLSID_DISPLAY      22x
        r1=0x01001003 AEECLSID_FILEMGR       4x
    AEEHelperFuncs[0x09c] dbgprintf     pedido 16x
    AEEHelperFuncs[0x08c] GetAEEVersion  pedido 1x
    IShell::slot4                       pedido 1x

**O que o modulo pede ao sistema e, por esta ordem: `IShell::QueryInterface` por
um `IDisplay` (22 titulos) e por um `IFileMgr` (4), depois `dbgprintf` (16).**

## Ruling: a etapa 3 tem ordem medida

`IDisplay` antes de `IFileMgr` antes do resto -- e nao por intuicao, por 22 contra
4. Custo se errado: implementar uma interface que menos titulos usam primeiro.

## Erros meus, registados para nao os repetir

Quatro hipoteses erradas seguidas sobre a MESMA coisa (a assinatura do
`CreateInstance`). A que resolveu nao foi a quinta tentativa: foi **mudar de
instrumento** (a bateria) e **tracar registradores** em vez de os deduzir.

E tres defeitos de instrumento meus, todos encontrados ao usar: um stub generico
para 117 slots (44 titulos com pedidos sem nome), `substr(17)` num prefixo de 18
(todas as chaves com um espaco a frente, e a lista final vazia sem dar erro), e o
detalhe so com `r0` quando o que importa no `QueryInterface` e o `r1`.

## O numero, agora (bateria)

    carga 62 de 62 | ponteiro de modulo 48 | applet **22** (era 1)

`applet` quer dizer que o `IModule::CreateInstance` escreveu um ponteiro nao nulo.
**NAO quer dizer jogavel**: nada desenha, nada toca som, e nada disso esta
implementado. E um marco de ARRANQUE.

O que desbloqueou os 21: servir o `IShell::QueryInterface` com as duas interfaces
que a bateria mediu -- `AEECLSID_DISPLAY` (22 titulos) e `AEECLSID_FILEMGR` (4).

## Demanda por interface e por slot (a lista que dirige a etapa 3)

    IDisplay::slot3, slot4, slot5, slot7, slot19
    IFileMgr::slot2
    IShell::slot20, slot41
    AEEHelperFuncs[0x040], [0x0a8], [0x014] strlen, [0x08c] GetAEEVersion

## O erro mais reincidente desta sessao, registado

**Um teste generico a engolir um especifico.** Apareceu TRES vezes:
1. a ordem dos testes de descodificacao (bits 27-26 antes do bit 25) na CPU;
2. `idx >= kBaseDoShell` antes de `idx == kBaseDoShell + 2` no despacho;
3. no mesmo despacho, o `idx >= kBaseDoSlot` generico.

Sempre o mesmo sintoma: a funcionalidade parece nao estar implementada porque
outro ramo a apanha primeiro. Regra para o proximo codigo de despacho: **do mais
especifico para o mais generico, e um teste que verifique a ORDEM.**

## Estado, depois dos helpers

    carga 62 | ponteiro de modulo 48 | applet 22

Os helpers do sistema (`strlen`, `strcpy`, `strcmp`, `strchr`, `memset`,
`memmove`, `strtowstr`, `GetAEEVersion`, `aee_GetRand` deterministico) entraram e
**nao aumentaram os applets**. Aprofundaram a demanda: os titulos que paravam no
`strlen` agora param no `IFileMgr`. Os helpers eram pre-requisito, nao o
obstaculo.

## A proxima parede, com nome

    IFileMgr::slot2   r1=0x0013b9a0 r2=0x00000001
    IFileMgr::slot7   r1=0x0010b498
    IFileMgr::slot8   r1=0x00000000 r2=0xf0006d80
    IDisplay::slot3, slot4, slot5, slot7, slot19
    IShell::slot20, slot41

## O erro mais reincidente: QUATRO vezes, e agora impossivel por construcao

**Um passo generico a atropelar trabalho especifico.** As quatro:
1. e 2. a ordem dos testes de descodificacao (antes do bit 25) e o
   `idx >= kBaseDoShell` antes do especifico;
3. o `ConstruirShell` a escrever a base fixa;
4. o laco de preenchimento dos 117 slots a SOBRESCREVER os helpers ja escritos.

A correcao da quarta nao foi reordenar: foi uma **lista explicita** de slots
implementados que o laco generico consulta e salta. Regra para o proximo codigo:
**do mais especifico para o mais generico, e a lista de excepcoes e explicita em
vez de implicita.**

## Estado, depois do IFileMgr e do VFS

    carga 62 | ponteiro de modulo 48 | applet 22

Entrou o `IFileMgr` (ABI lida de `platform/deprecated/inc/AEEFile.h`, ordem do SDK
e nao copiada) e um VFS minimo de leitura sobre os ficheiros irmaos do modulo.

**A parede mudou de sitio e agora esta em tres interfaces:**

    IShell::slot12    r1=0x00000000 r2=0x80200048
    IDisplay::slot2   r1=0x00008000 / 0x00008001
    IDisplay::slot3, slot4, slot5, slot7, slot19
    IFileMgr::slot2, slot7
    IShell::slot20, slot41

## Motivos agrupados (o JSON agora leva o motivo)

    15 x orcamento_esgotado | create:saiu_do_modulo
    14 x orcamento_esgotado | sem_ponteiro_de_modulo
     8 x retornou | create:retornou            <- o modulo devolve sem criar
     6 x retornou | create:saiu_do_modulo_para_0x0
     6 x retornou | create:orcamento_esgotado

## Nota de honestidade

Estes 22 "applets" sao ponteiros nao nulos devolvidos pelo `CreateInstance`.
**Nada desenha, nada toca som, nada e jogavel** -- nada disso esta implementado.
O numero mede ARRANQUE, e o ledger diz-lo nesses termos em toda a parte.

## Laco de eventos (a peca que faltava para um jogo ANDAR)

`IShell::SetTimer` (slot 12) implementado, com um laco de eventos em tempo
VIRTUAL: 1 ms emulado por passo, temporizador cumprido quando vence, callback
chamado com o contexto no r0 e o par `(funcao, contexto)` lido dos dois primeiros
campos do `AEECallback`. O proprio callback re-arma o seguinte -- e assim que o
laco de quadro de um jogo se sustenta em BREW.

**A sentinela de retorno passou a ter dois significados** (retorno da entrada e
retorno de um callback de temporizador), distinguidos por uma bandeira. Sem isso
o primeiro callback seria lido como "o modulo retornou".

## Erro novo, e nao e o de ordem: confundir o endereco com a memoria

Escrevi `mem.Escrever32(s.Endereco(base + 12), ...)` -- isso escreve por cima do
PROPRIO endereco de saida, e nao da entrada da vtable. A entrada esta em
`mem[vtable + 12*4]`. Sintoma: a bateria a dizer "falta SetTimer" com o SetTimer
a funcionar. **Mesmo sintoma da quarta ocorrencia do erro de ordem, causa
diferente.**

## Estado, depois do IDisplay e do framebuffer

    carga 62 | ponteiro de modulo 48 | applet 22 | PIXELS 0

`IDisplay` implementado (ABI de `platform/ui/inc/AEEIDisplay.h`) sobre um
**framebuffer de software** de 640x480. Existe para haver uma medida VISUAL que
nao dependa de capturar ecra: quantos pixels distintos cada titulo escreveu.

`DrawText` foi chamado UMA vez em todo o corpus e nenhum pixel foi escrito. **Isso
significa que os titulos ainda nao chegam ao desenho** -- nao que o framebuffer
esteja errado. A medida visual esta montada e a zero.

## A demanda mais rica ate agora

    IShell::QueryInterface IID desconhecido   76x
        iid=0x0102c4e8 (SQLMgr)   66x
        iid=0x01001056 (SOUND)     4x
        iid=0x01001002 (HEAP) / 0x01001014 (FILE) / 0x01002001 (GRAPHICS)
        iid=0x01028e51 (ROOTFORM) / 0x0106c411 (HID)
    IShell::slot4 (QueryClass)   17x

## O metodo: uma verificacao que passa sem a mudanca nao e verificacao

Nesta ronda a substituicao de texto falhou em SILENCIO, e eu "verifiquei" com um
`grep -c` que contava `kVtableDisplay` -- que aparece nas linhas do
`ConstruirShell` de qualquer maneira. A contagem dava 4 com ou sem a mudanca.

Regra adoptada: **`assert` antes de escrever e `assert` depois, sobre um texto que
so existe se a mudanca entrou.** A contagem de um simbolo que ja la estava nao
verifica nada.

## O erro de ordem: SEIS vezes

1. e 2. os testes de descodificacao da CPU e o `idx >= kBaseDoShell`;
3. o `ConstruirShell` com base fixa;
4. o laco de preenchimento a sobrescrever os helpers;
5. o diagnostico `idx >= 1400 && idx < 1600` a engolir os especificos, deixando a
   lista de demanda VAZIA;
6. (o erro de endereco-vs-memoria tem o mesmo sintoma mas causa diferente).

**Regra, agora com seis casos por tras: no codigo de despacho, do mais especifico
para o mais generico, e a lista de excepcoes e explicita.**

## A ABI do IDisplay estava ERRADA, e era o defeito silencioso maior ate agora

Transcrevi de memoria e escrevi as assinaturas do BREW 4.x. As deste SDK
(`platform/ui/inc/AEEIDisplay.h`) sao outras. Tres eram silenciosas de verdade:

- `GetFontMetrics`: eu lia uma struct de metricas no r1; aqui o r1 e a FONTE e os
  ponteiros de saida sao r2/r3. Escrita no sitio errado, sem sintoma.
- `DrawText`: os argumentos 5 e 6 sao COORDENADAS, nao uma rect.
- `CreateDIBitmap`: `IDIB **ppIDIB` e o SEGUNDO argumento e o r0 e um codigo.

`BitBlt` tambem: a origem e um bloco cru de pixels, sem cabecalho de bitmap.

**Regra: a assinatura le-se do cabecalho, argumento a argumento, antes de
escrever a implementacao. Nao se transcreve de memoria.**

## A lista de demanda mentia, e a causa era estrutural

Um so objecto generico servia SETE interfaces, com uma so vtable: o `slot 7` era
`IHeap::slot7` ou `ISound::slot7` conforme quem chamou, e a bateria nomeava tudo
`IFileMgr::slot2007`. **A lista nao dizia que metodo cada titulo quer.** Agora um
objecto e uma vtable por interface.

## Duas listas que tem de concordar sao ZERO listas

O `aee_GetUpTimeMS` foi escrito na tabela mas nao acrescentado a lista de "quem ja
tem implementacao". O laco de preenchimento apagou-o -- sintoma identico ao do erro
que essa lista existia para evitar.

Substituidas por UMA tabela `{offset, saida}` de onde saem as escritas E a lista
de saltos. **Com uma so, e impossivel acrescentar uma implementacao sem que o laco
a respeite.**

## Verificar: `assert` antes e depois

Antes de escrever, `assert` de que a ancora existe uma so vez; depois, `assert`
sobre um texto que so existe se a mudanca entrou. Na primeira tentativa o assert
apanhou uma ancora errada (`kFmGetLastError` vs `kFmLastErr`) que teria passado em
silencio -- e na ronda anterior a "verificacao" por `grep -c` tinha passado sem a
mudanca.

## Um fantasma que nao existia

`r0=0xf0001060` num pedido de helper pareceu-me "enderecos de saida a escapar para
o guest". Sao REGISTOS RESIDUAIS: o `GetAppInstance` nao tem argumentos. Nao havia
fuga. **Antes de anunciar um defeito, verificar se o argumento e sequer usado pelo
metodo.**

E uma leitura deslocada por um fez-me crer que `0x0b0` era `aee_GetSeconds`. A
derivacao da struct inteira (117 slots, offsets a partir do inicio) confirma
`0x0b0 = aee_GetUpTimeMS`. Tabela em `/tmp/tab_helpers.json`.

## Uma terceira causa para o mesmo sintoma: a EDICAO

A escrita do `SetTimer` na vtable do `IShell` desapareceu numa edicao de texto. O
`SetTimer` continuava escrito e a funcionar -- o pedido chegava ao despacho -- mas a
entrada da vtable apontava para o stub que recusa, e a bateria voltou a dizer
"falta SetTimer".

Os sintomas das tres causas sao identicos e as causas sao diferentes:

1. um ramo generico antes do especifico (a ORDEM);
2. um laco de preenchimento a apagar implementacoes (a ORDEM outra vez);
3. uma edicao de texto que perdeu uma linha (a EDICAO).

Correccao: **toda a cablagem numa tabela `{objecto, slot, saida}` com um laco
unico, mais uma LEITURA DE VOLTA que aborta no arranque se uma entrada nao bater
certo.** Com uma so tabela nao se acrescenta uma implementacao sem a cablar; com a
leitura de volta, uma cablagem perdida custa 1 segundo em vez de 4 minutos.

## A guarda certa no proposito e errada na conta

A guarda de limites disparou no primeiro arranque (`slot 7 de 0x00002468 sai da
vtable`). A cablagem estava certa: a guarda comparava com a base da PRIMEIRA vtable
generica, e cada uma tem 64 slots -- o fim de uma e o inicio da seguinte.

**Distinguir "a guarda esta errada no proposito" de "a guarda esta errada na
conta".** Uma guarda que aborta por engano custa um minuto. Uma guarda que nao
aborta quando devia custa uma bateria inteira.

## Nomes vindos do cabecalho, um a um

    IHIDDevice slot  7 = GetNumberOfButtons      (declarado: 14)
    IHIDDevice slot 14 = RegisterForPositionChange
    IDisplay   slot 15 = SetDestination(IBitmap*)
    IDisplay   slot 16 = GetDestination(void) -> IBitmap*
    IFileMgr   slot  7 = RmDir(const char*)
    IHeap      slot  7 = Walk  (AEEIHeap1.h, INHERIT_IHeap1) -- DEPURACAO de heap

`RmDir` recusa: a VFS desta etapa e SO DE LEITURA, e e deliberado -- um jogo que
apague um ficheiro do modulo destroi a reprodutibilidade.

`SetDestination` so aceita um bitmap NOSSO: aceitar um ponteiro qualquer poria o
desenho num sitio que nao existe.

## `INHERIT_IBase` tem DOIS membros -- e TODOS os slots estavam errados

    #define INHERIT_IBase(iname) \
       uint32 (*AddRef)(iname*); \
       uint32 (*Release)(iname*)

**Nao ha `QueryInterface` em `INHERIT_IBase`.** Eu contava TRES membros e portanto
todos os numeros de slot que usei estavam errados.

O sintoma que o obrigou a aparecer foi o `pacmania`:

    00109d5c  ldr r1, [r0]        ; a vtable
    00109d60  ldr ip, [r1, #8]    ; <<< INDICE 2
    00109d64  mov r1, r6          ; AEE_FONT_BOLD
    00109d68  blx ip              ; (po, fonte, &asc, &desc)

Assinatura exata do `GetFontMetrics`, que o cabecalho poe no slot 2. **O jogo estava
certo e eu errado.**

**Correcao estrutural: `tools/gerar_slots.py` le os cabecalhos e escreve
`tools/brew_slots.inc`.**

    kShell_CreateInstance = 2   (eu chamava-lhe "QueryInterface")
    kShell_QueryClass     = 3   (eu tinha 4 = GetDeviceInfo)
    kShell_SetTimer       = 11  (eu tinha 12 = CancelTimer)
    kDisplay_DrawText     = 4   (eu tinha 5)
    kFileMgr_OpenFile     = 2   -- o `IFileMgr::slot2` que a bateria pedia

**Um numero escrito a mao ja divergiu uma vez, e o custo foi uma ronda.** Agora o
gerador e a unica fonte.

## Uma guarda construida sobre um numero errado recusa o que esta certo

A guarda de cablagem dizia `slot < 3 = IBase`. Com a IBase a ser 0 e 1, recusou o
`GetFontMetrics` no slot 2 -- **que era o pedido certo**. Passou a `< 2`.

## Um limite verificado so no destino nao limita o trabalho

O `Retangulo` do framebuffer percorria `w*h`; o `Ponto` recusava o que saia do ecra,
mas **o laco corria na mesma**. Uma rect grande vinda do guest: mais de 900 segundos
para UM titulo. Cortando antes do laco: **7 segundos**.

E um limite de PASSOS nao serve como orcamento, porque o custo por passo depende do
titulo e quem paga e o INSTRUMENTO. `kOrcamentoSegundos = 25` por fase, com o motivo
registado -- **um titulo que bate no orcamento nao falhou: ainda estava a andar.**

## Verificar a CONTAGEM DE LINHAS depois de cada escrita

Escrevi por cima do `bateria.cpp` com uma versao TRUNCADA: 412 linhas onde havia
1295. O `.bak` guardava o mesmo conteudo truncado (foi escrito do mesmo objecto ja
errado). Nao houve aviso: um ficheiro truncado continua a ser um ficheiro valido em
Python. Salvou-me o `git`, e so porque o commit anterior estava feito.

**Depois de cada `write_text`, comparar a contagem de linhas com a esperada.** E a
unica verificacao que apanha uma escrita que apaga.

## O gerador de slots, e os tres defeitos dele

`tools/gerar_slots.py` resolve a CADEIA de heranca a partir dos cabecalhos:

    INHERIT_IBase       = 2   (AddRef, Release) -- e SO estes dois
    INHERIT_IQI         = 3   (mais o QueryInterface, que NAO esta na IBase)
    INHERIT_IAStream    = IBase + 3 = 5
    INHERIT_IFile       = IAStream + 7 = 12
    INHERIT_IRealloc    = IQI + 4 = 7
    INHERIT_IHeap1      = IRealloc + 8 = 15

O gerador teve TRES defeitos meus, todos do mesmo tipo:

1. O `void (*pfn)(void *)` DENTRO dos parametros do `SetTimer` contado como membro
   -- empurrava o `CancelTimer` para o 13.
2. O ULTIMO membro de um macro NAO TEM `;`, e o padrao exigia-o -- o `Cancel` do
   `IAStream` nao existia e o `IFile` saia com 4 slots de cabeca em vez de 5.
3. A cabeca procurada no CORPO, quando esta na primeira linha do macro.

**Um falso positivo no gerador e pior do que um numero escrito a mao: parece que
foi medido.**

## Uma so regra de normalizacao de caminhos

`OpenFile` usa a MESMA VFS que o `Test` -- nao ha duas verdades sobre que ficheiros
existem. `_OFM_CREATE` e `_OFM_APPEND` sao recusados em voz alta: a VFS e so de
leitura por DECISAO (um jogo que escreva no modulo destroi a reprodutibilidade),
nao por falta.

## Se a guarda proibe o que e preciso, a excepcao e EXPLICITA

O objecto FICHEIRO nao tem `Release`, e o jogo fecha ficheiros com `IFILE_Release`
(slot 1). A guarda de IBase recusou-o -- corretamente, porque para os objectos do
`ConstruirShell` escrever no slot 1 destroi a IBase. A excepcao ficou nomeada
(`e_o_ficheiro`), com o motivo escrito.

## Delegacao: cinco frentes em paralelo, com o desenho lido ANTES

Antes de delegar, li o `DESIGN.md` (P1-P7) e o `PLAN.md` (etapas 0-9), e copiei as
partes relevantes PARA DENTRO de cada tarefa -- um sub-agente que nao le o desenho
trabalha contra ele. Cada tarefa leva: os sete principios, o laco de trabalho, os
caminhos exatos, o que NAO fazer, e o formato do relatorio.

**Uma arvore quebrada multiplica-se pelo numero de sub-agentes.** Antes de os
lancar corri a bateria: `carga 62 | modulo 48 | applet 22`, 87 testes verdes. E
ainda bem -- porque a migracao do despacho tinha deixado TRES defeitos meus:

1. `Correr` caia fora do fim sem `return` -> comportamento indefinido numa funcao
   que devolve por valor -> `free(): double free detected`, com a pilha a apontar
   para o `append` de uma cadeia VIZINHA. **O `append` era a vitima.**
2. Chave de abertura duplicada no `Correr`.
3. **REGESSAO `applet 22 -> 1`**: escrevi `kIidDisplay = 0x01003003` de memoria,
   quando o valor medido e `0x01001001` -- e estava escrito no ficheiro que eu
   estava a migrar, a quatro linhas do sitio.

As cinco frentes, cada uma na sua worktree (sem colisao de ficheiros nem de
compilacao):

    igl          etapa 6, o IGL       (a arvore antiga tinha 71/84 handlers sem log)
    imedia       etapa 5, media+audio (MM_STATUS_DONE, notificacao a mais/menos)
    hid-entrada  etapa 8, IHIDDevice  (UIDs medidos; cabeca IQI = 3 slots)
    formato-bar  etapa 3, o `.bar`    (pacmania: pacmania.bar, id 0x13e3)
    regressoes   etapa 9, comparar    (multiplica o valor de tudo o resto)

## Etapa 9 ENTREGUE, e o juiz viu que ia julgar a coisa errada

O sub-agente `regressoes` entregou `tools/comparar` + `tools/regressao.sh`
(branch `comparar`, commit `1c4a61c`), com 19 testes gtest, 8 verificacoes pelo
binario dentro do ctest, e **4 guardas quebradas de proposito uma a uma** -- mais
uma violacao no proprio emulador (o `QueryInterface` deixa de responder ao
`AEECLSID_DISPLAY`): `applet 22 -> 1`, 21 regressoes, codigo de saida 1. Depois de
restaurar, verde.

Tracejou a ancora da referencia para `tools/baseline/bateria.json`, VERSIONADA, e
recusou a auto-atualizacao: *"um instrumento que se auto-atualiza quando falha
transforma toda a regressao num 'sem regressoes'"*.

## DOIS DEFEITOS MEUS, achados pelo agente que construiu o juiz

**1. O campo `vtable` estava MORTO, e eu lia-o como medicao.**

`e.tamanho` nunca era atribuido -- a linha foi removida no commit `d75281d` --
e o `vtable` compara `alvo >= kBase && alvo < kBase + e.tamanho`. Com o tamanho a
zero isso e SEMPRE falso: **um dos quatro degraus do arranque estava morto**, e a
bateria imprimia `vtable NAO` nos 62 titulos.

Eu lia `vtable NAO` como "os modulos nao tem vtable" em vez de "o campo nao mede
nada". **Um campo que nao mede e pior do que um campo ausente: um ausente nao se
le.** Corrigido: `vtable` verdadeiro em **48 de 62**, a coincidir com os 48 modulos.

**2. O orcamento de fase media a CARGA DA MAQUINA.** Usava
`std::chrono::steady_clock` do hospedeiro -- violacao do P4, latente enquanto o
limite de passos dominou (os 36 titulos com `orcamento_esgotado` param em 4000000
passos, nenhum pelo relogio). No dia em que um titulo ficasse mais lento por passo,
a bateria acusaria regressoes de JOGABILIDADE que eram de CARGA -- **e o
`tools/comparar` acreditaria nelas.** Removido.

**O agente que constroi o juiz foi quem viu que o juiz ia julgar a coisa errada.**

## E o comparador validou a correcao sozinho

Depois de corrigir, `./tools/regressao.sh` deu **0 regressoes e 48 MELHORIAS** --
exatamente os 48 titulos cujo `vtable` passou de falso a verdadeiro. E classificou
`tamanho` como **NEUTRO**, sem o inventar como melhoria.

Baseline atualizado com `--atualizar` explicito. `ctest`: **5/5**.

## PENDENTE, pedido pelo agente (e concordo)

**Nenhum campo de PROVENIENCIA no JSON.** O comparador deriva a configuracao da
lista pasta/mod, logo um corpus diferente que mantenha os mesmos 62 pasta/mod NAO e
detetavel. Falta um bloco de cabecalho com `corpus_sha256`, `titulos` e o commit.
A guarda de configuracao e real mas incompleta.

## Onde as coisas estao, ao fim de 81 commits numa ronda

### Os numeros

    carga 62 | modulo 62 | applet 37 | DESENHA 0
    409 testes | ctest 9/9 | 25 541 linhas

### As seis frentes que entregaram

| frente | resultado |
|---|---|
| `parede-pilha` | **21 -> 0** titulos a saltar para a PILHA |
| `descodificador` | **66 376 -> 2 838** palavras executadas como outra instrucao (**-95,7%**) |
| `auditoria-stubs` | 98 achados; `vsnprintf` (116x) e `realloc` (9x) servidos |
| `auditoria-plano` | 45 mutantes; 36 testes provados por violacao |
| `auditoria-zeebx` | 17 afirmacoes verificadas; o `BMPIds.csv` com 25 847 CLSIDs |
| `auditoria-testes` | o plano etapa a etapa; 8 testes fracos COM prova |

### A parede que fica, e porque acredito que e esta

**A area de rascunho do carregador cai DENTRO da imagem do modulo em 29 dos 62 titulos.**
Com a base a ZERO, 29 tem imagem maior que `0x90010`; o carregador escreve o ponteiro do
modulo em cima do CODIGO, e nada acusa.

E a razao de eu acreditar: **o `descodificador` fechou 95,7% das divergencias silenciosas
-- incluindo o `ldrd`, o `strb` do Thumb e uma guarda MORTA -- e NENHUM titulo mudou de
estado.** A parede esta no ESTADO com que o titulo entra.

DUAS TENTATIVAS DE ARRANJO, ambas medidas, ambas PIORES:

    0x00090000 (o original)  -> modulo 62 | applet 37
    0x81000000               -> modulo 48 | applet 38
    heap do alocador         -> modulo 48 | applet 38

**Qualquer endereco que nao seja o antigo perde 14 titulos.** O endereco do rascunho NAO E
NOSSO para escolher -- o carregador do GUEST decide algo com ele. Falta medir: **o que o
guest escreve e le em 0x90000.**

### Defeitos meus, achados pelos agentes

1. **O `applet 41` estava INFLADO** -- lixo no slot de saida contado como applet. O honesto
   e **37**. Mesma classe do `tamanho` que esteve a zero.
2. **`Despacho::Faltas()` era um mapa NUNCA escrito** -- quem o lesse concluia "nao falta
   nada". Terceira ocorrencia da classe. Removido, com os outros acessores mortos.
3. **11 sitios imprimiam DECIMAL com prefixo `0x`** -- `saiu_do_modulo_para_0x2148007884`
   era `0x8007FFCC`, a pilha. **Envenenava o LEDGER.**
4. **O rotulo `build` da referencia MENTIA** (dizia `eb62459`, os dados vinham de
   `0286921`). Passou a haver `binario_sha256`.
5. **A assinatura do `SetTimer`** -- impedia QUALQUER titulo de armar o laco de quadro.
6. **O teste `CablarDetetaUmaCablagemPerdida`** mentia no nome: verde com a leitura de
   volta arrancada. Renomeado.
7. **A contagem de faltas guardada ANTES do `EVT_APP_START`** -- o `IRootForm` era invisivel
   na bateria.
8. **O meu edit truncou o `bateria.cpp` DUAS vezes.** A regra que eu proprio escrevi --
   verificar a contagem de linhas -- foi o que salvou as duas.

### Publicado

    github.com/requeijaum/curupira    publico | MIT | 84 commits

O fork (`research/sources/zeebulator/`) fica GPLv3. **O `curupira/` ainda nao foi removido
do fork** -- esta adiado enquanto houver worktree de agente assente nele.

## Estado verificado

- `ctest`: **777 testes verdes** (ultima corrida nesta sessao).
- `tools/verificar_memoria.sh`: **777 testes sob valgrind, 0 erros**.
- Corpus: 62 titulos em `research/sources/scripts/corpus62.json`.
- Bateria com log maximo concluida (62/62). Resultado: 41 desenharam, 21 sem
  desenho, 5 travamentos de guest, 12 recusaram entrada. Logs em `/tmp/smoke3/`.

## Fechado nesta sessao (com commit)

| item | commit | prova |
|---|---|---|
| GpuLog em 56 handlers de GL que nao tinham log | `9aabf96` | 71 de 84 nao logavam |
| slots da C stdlib 0xd8/0xe8 e 0x20/0x13c estavam trocados | `23d3f7c` | testes `Strstr*` testavam o stristr |
| 7 slots de GL ligados (cullFace 86377 chamadas, etc.) | `9aabf96`+ | A/B no `alpineracerex`: 2872 vs 2828 cores |
| `NAND 1.1.2`: contrato do SIMCARDCTL (`VerifyPin_cb`, AUTHORIZED/INVALID) | `dad236a` | strings do `1.1.2_APPS.bin` |
| canal de controle responde com o laco preso (`alive`) | `54e5461` | resposta em 0,00 s; antes silencio |
| `Stop` do IMedia avisa com `MM_STATUS_DONE` | `d54503d` | tratador do `cnk2` em `0x00101a80` |
| UIDs medidos: eixos confirmados, d-pad e UM controlo | `a6c24c9` | 24-38 titulos embutem as constantes |
| correlacao No-Intro -> pasta/mod (62 nomes reais) | (tabela em `/tmp/no_intro_tab.json`) | cada zip tem `mif/<pasta>.mif` |

## Refutado por medicao (nao mexer outra vez sem prova nova)

- **Caminho de ficheiro entrega dados errados**: bytes identicos ao `pak0.pakz`
  real em 4 offsets (`ZEEB_LOG_FILE_BYTES=1`).
- **Ordem de leitura do buffer de midia**: 105 chamadas em 4 titulos, zero
  mudancas. A alegacao do zeebx nao se reproduz.
- **Espera ocupada no relogio** (Zeebo Extreme): `aee_GetUpTimeMS` com ZERO
  chamadas no perfil do `Rolimaz`.
- **Transbordo de `strcpy`**: zero copias acima de 4096 bytes em 8 titulos.
- **HID**: nenhuma chamada no caminho quente do `funsoccer`.
- **`SIMCARDCTL` com resultado nao-zero**: regride (tick 3). Com zero: neutro.

## Em aberto, por ordem de valor

1. **`funsoccer` (= Zeebo F.C. Super League) e a familia Zeebo Extreme** gastam
   o orcamento dentro da MESMA rotina de biblioteca de copia de bytes
   (`funsoccer` 0x130530, `Boiaz` 0x107e50). Proximo passo: identificar a rotina
   procurando o padrao de bytes nos outros 60 modulos e nomeando-a pelo uso.
2. **Canal de controle**: 12 titulos ainda nao aceitam entrada (o `alive` diz que
   estao presos no guest, mas o jogo nao recebe).
3. **d-pad**: a medicao diz que e UM controlo com valor de direcao; falta
   determinar o formato (injetar `0x0106C3FE` com 0..11 e ver o menu mexer).
4. **4 slots de GL sem handler**: `glScissor` 4057, `glColorMask` 2940,
   `glFlush` 1740, `glPolygonOffsetx` 80.
5. **8 classes recusadas** (`CCipherFactory` 3 titulos, `ENH`, `CMD5Ctx`, `NET`,
   `0x010292c3`). Duas ja registadas sem medir.
6. **Correlacao No-Intro na GUI** e **config por jogo editavel** -- pedidos
   explicitos do dono.

## Rulings (decisoes tomadas sozinho, com o custo de errar)

- **Nao mexer no modelo do d-pad** antes de saber o formato do valor. Custo se
  estiver errado: nenhum -- a informacao vem de graca do passo 3.
- **Nao aplicar as correcoes do zeebx por citacao.** Quatro das dez alegacoes
  auditadas eram falsas para o nosso corpus. Custo se estiver errado: perco
  tempo a re-medir o que ja estava certo.
- **Nao publicar** sem autorizacao. Custo se estiver errado: nada sai.
- **Registar as hipoteses refutadas** no artigo, com o numero que as matou.
  Custo se estiver errado: artigo mais longo.

## Ficheiros de referencia que sobrevivem a compactacao

- `docs/AUDITORIA-ZEEBX-v0.1.0.md` -- auditoria dos 12 commits + Zeebo Extreme
- `docs/SIMCARDCTL-CONTRATO-DO-OEM.md`
- `docs/PAREAMENTO-DE-UIDS-MEDIDO.md`
- `docs/DIAGNOSTICO-DE-VIVACIDADE.md`
- `docs/INVESTIGACAO-POR-SUBSISTEMA.md`
- `docs/LACUNAS-2026-09-13.md`
- `docs/ESTUDO-DE-SKILLS-2026-09-14.md` -- este estudo


- `docs/medicoes/no_intro_tab.json` -- tabela No-Intro (62 nomes reais)
- `docs/medicoes/2026-09-13-bateria-log-maximo/` -- os 62 logs da bateria comprimidos
- `docs/medicoes/estudo-skills-2026-09-14/` -- os dois digestos do estudo de skills

**Higiene feita**: os ficheiros que estavam em tmpfs (tabela No-Intro, 62 logs,
digestos) foram copiados para dentro do repo. `/tmp` e tmpfs e perde-se no reboot.


---

## Frente `raster` (etapa 3/6 do plano): O RASTERIZADOR -- os primeiros pixels

Worktree `/tmp/wt-raster`, branch `raster`, base `ecc97b0`. Entregue em dois
commits: o rasterizador (arvore nova) e o REMENDO dos ficheiros partilhados.

### O numero, antes -> depois, e o comando

    ./build/zb2_bateria "$corpus" "$mods" /tmp/raster/antes.json     # antes
    ./build/zb2_bateria "$corpus" "$mods" /tmp/raster/depois.json    # depois
    ./build/zb2_comparar /tmp/antes.json /tmp/depois.json

    PIXELS 0 -> 0 em 62 de 62 | CORES maximo 1 -> 1 | carga 62 | modulo 62 | applet 41
    comparar: 0 regressoes, 0 melhorias -- e a falta `rasterizador_de_GL` (124x -> 0)

**O ALVO NAO FOI ATINGIDO, E A RAZAO E MEDIDA, e nao uma desculpa.** O alvo era
`PIXELS > 0` num titulo com `CORES > 10`.

### O que CONTRADIZ o que se pensava: as 124 chamadas de GL nao chegavam

As "124x" da lista de demanda sao **exactamente 2 registos de instalacao x 62
titulos**: o `Igl::Instalar` e o `Egl::Instalar` registavam a falta
`rasterizador_de_GL` uma vez cada, no arranque de cada titulo. Nao sao 124
chamadas de jogos.

Medido com o traco CRU dos 62 titulos (`ZB2_TRACE=1`, 454 MB de log):

| evento de video em 62 titulos | quantas vezes |
|---|---|
| `GL_CABLADO` (instalacao) | 62 |
| `IGL_INSTALADO` / `IEGL_INSTALADO` | 62 / 62 |
| `GL_AddRef` -- o **unico** metodo de GL que um titulo chama | 19 998 (todos do `pbc`) |
| qualquer outro `gl*` (incluindo `glDrawArrays`) | **0** |

O `pbc` chama `GL_AddRef` 19 998 vezes de `lr=0xf001d4c0` (o proprio endereco do
slot 0) ate ao limite `saidas > 20000` do despacho: e o `laco_de_saidas` que a
bateria ja registava. **Nenhum titulo chega a desenhar.**

### A parede, agora com nome: o guest NAO CHEGA ao codigo de desenho

Os quatro titulos com o wrapper de GL estatico (medidos pelo sub-agente `igl`:
`chessbots`, `ddragonz`, `tectoy`, `nfs`) nao alcancam o `GLES_Init` nas fases
que a bateria corre:

| titulo | create | start | quadro |
|---|---|---|---|
| `chessbots` | 4 000 000 passos, `orcamento_esgotado`, **sem applet** | -- | -- |
| `ddragonz` | 177 passos, `saiu_do_modulo_para_0xe3a03000` | `retornou` | 1 quadro, sai para `0xe5940028` |
| `tectoy` | 255 passos, `retornou` | `retornou` | 0 quadros (nenhum temporizador armado) |
| `nfs` | 10 passos, `saiu_do_modulo` | `sem_handle_event` | -- |

Nenhum pedido de servico em falta aparece nestes titulos (a lista de faltas do
`ddragonz` e vazia): o guest sai do modulo sem pedir nada que falte. O PC para
onde ele sai, por familia, nos 62 titulos:

| para onde o PC sai | titulos | exemplo |
|---|---|---|
| area da PILHA (`0x8007ffc0..0x8007ffcc`) | 20 | `alice`, `AirRacez`, `zeebotennis` |
| dentro do modulo (0..16 MB) | 30 | `cnk2` `0x9e8ec`, `cninja` `0x2b2db0` |
| HEAP, no ponteiro do `AEEMod_Load` (`0x80200010`) | 4 | `zeebo_app` |
| palavra de INSTRUCAO ARM (`0xe3a03000` = `mov r3,#0` 2x, `0xe3a06000`, `0xea00000e` = `b`) | 4 | `ddragonz`, `zeebo_app`, `tekken2`, `ridgeracer` |
| dados usados como endereco (`0x57455242` = "BREM" 2x, `0xff00f20e`, `0x140003e`) | 4 | `quake2brew` |

**Hipotese (NAO provada): um valor de DADOS esta a ser usado como ponteiro de
funcao** -- o guest le de um sitio onde o emulador escreveu dados (o cabecalho do
modulo, a pilha, uma palavra de codigo) e salta para la. O `0x57455242` e a
assinatura "BREW" do cabecalho lida como endereco. Isto esta no caminho da CPU e
do despacho, e nao no do rasterizador: **quem o tiver de resolver precisa da
sonda que traca as ultimas instrucoes antes da saida** (nao foi feita).

### O que foi entregue, e como esta provado

`core/video/rasterizador.{h,cpp}` -- NOVO. TRIANGLES/STRIP/FAN por varrimento de
caixa delimitadora com funcoes de aresta e a regra "top-left"; cor por vertice
interpolada; textura GL_NEAREST com clamp; teste de profundidade; descarte de
faces. Escreve na `core/brew/tela.h` (a mesma tela que a bateria le, linha 794) e
nao num buffer proprio. O que ficou FORA esta escrito no topo do ficheiro, com o
que cada omissao custa: afinidade sem correccao de perspectiva, sem recorte de
frustum, sem blending/stencil/mipmaps/bilinear/neblina/iluminacao/scissor/dither.

**17 testes** em `tests/rasterizador_test.cpp` (351 no total, era 335) e **10
guardas provadas por violacao deliberada** (`tools/provar_guardas_raster.py`,
10 de 10 VERMELHAS): a regra do canto, a inversao do y, os pesos da cor, o teste
de profundidade, a coordenada de textura, o descarte de faces, o limite da janela,
o `glClear` que poe o clip de lado, a primitiva sem caminho e a recusa sem tela.

Duas licoes de teste, das violacoes: (1) o teste da cor interpolada passou VERDE
com os pesos trocados porque **dois vertices tinham a MESMA cor** -- o teste tinha
de distinguir, e agora tem tres cores diferentes; (2) o teste da janela passou
VERDE com o limite quebrado porque a geometria nao saia da janela -- um teste que
nao exercita o que a guarda protege nao e guarda.

### A prova de ponta a ponta que EXISTE (e o que ela NAO e)

`tools/sonda_gl` chama os thunks de GL **do proprio titulo** (achados por
varrimento da imagem) e o pedido percorre a vtable, a faixa de saida, o despacho e
a Tela. Com a bancada a pedir um triangulo a serio:

    IGL 26 glDrawArrays  CHEGOU  feito: 3 vertices -> 38400 pixels em 1 triangulos
    PIXELS: 345600 | CORES: 2      (ddragonz.mod)

A GEOMETRIA E DA BANCADA: nao e um titulo a desenhar, e por isso nao substitui o
`PIXELS` da bateria. Antes desta frente, a bancada escrevia nos vertices o valor
`0x3F800000` (1.0f) TRES vezes: um triangulo de area ZERO, e a sonda imprimia
"PIXELS: 0" sem distinguir "nao ha rasterizador" de "a bancada pediu um triangulo
degenerado". Os dois numeros iguais com causas diferentes -- **o defeito que esta
arvore persegue** -- estavam dentro do proprio instrumento.

### Ruling: a tela liga-se por fora, e o remendo tem de ser aplicado

O `Igl` nao tem, e nao pode ter, a `Tela`: ela vive no `Despacho` (ficheiro
partilhado). Sem `igl_.DefinirTela(&tela_)` o desenho e a limpeza **RECUSAM** com
o motivo escrito ("o IGL NAO TEM TELA LIGADA") -- recusa honesta, e nenhum pixel.
`tools/remendo_raster.py` aplica as duas alteracoes partilhadas com `assert` antes
e depois (uma ancora em falta nao escreve nada; foi ele que apanhou um erro MEU de
aritmetica de linhas: 3 e 5 em vez de 6). Custo se estiver errado: nenhum -- o
numero nao muda, e a recusa di-lo.

### Ficheiros partilhados que a frente tocou (para o merge)

| ficheiro | alteracao | porque |
|---|---|---|
| `core/brew/despacho.cpp` | `igl_.DefinirTela(&tela_);` (1 linha) | ligar a superficie ao IGL |
| `core/brew/egl.cpp` | a falta `rasterizador_de_GL` -> evento de informacao | era VERDADE e passaria a MENTIRA no mesmo commit (P7) |
| `core/brew/igl.cpp` | o desenho e a limpeza chamam o rasterizador | e o ponto de entrada, como estava planeado |
| `tests/igl_test.cpp` | 2 testes mudaram de nome e de assert | afirmavam "sem rasterizador", que deixou de ser verdade |
| `tools/sonda_gl.cpp` | a linha dos PIXELS passa a ser o numero real | "PIXELS: 0" era verdade e deixou de ser |

Aviso de merge: o `despacho.h` tem um `-Wreorder` (o `widgets_` e inicializado
antes do `igl_`), anterior a esta frente.

### Registado para nao repetir

O erro que mais tempo custou nesta frente nao foi no rasterizador: foi **ler a
lista de demanda da bateria como se fosse o que os titulos pedem**. A linha
`rasterizador_de_GL pedido 124x` e um sinal do modulo a instalar-se, e foi lida
como 124 pedidos de jogos. **Um contador de faltas de INSTALACAO e um pedido de
EXECUCAO contam-se no mesmo mapa** -- e foi preciso o traco cru de 62 titulos
(454 MB) para os separar.

### Um defeito de instrumento encontrado por esta frente: o "0x" com DECIMAL

**11 sitios no motor imprimem um numero DECIMAL com o prefixo `0x`**, que e o
mesmo defeito de instrumento que o `DESIGN.md` lista (um log so entra se puder
ser verdadeiro). Medido, e custou-me duas leituras erradas nesta frente:

    core/brew/despacho.cpp:1115  "saiu_do_modulo_para_0x" + std::to_string(pc)
    core/brew/despacho.cpp:815   "pfn=0x" + std::to_string(timer_.pfn)
    core/brew/despacho.cpp:341   "funcao 0x" + std::to_string(fn)
    core/brew/despacho.cpp:1102  "chamado com r0=0x" + std::to_string(r0)
    core/brew/widget.cpp:102/112, core/brew/interface.cpp:49/58/68,
    core/brew/egl.cpp:414/613

Efeito concreto: `saiu_do_modulo_para_0x2148007884` NAO e `0x2148007884` (38 bits,
impossivel num `Reg` de 32) -- e o decimal 2148007884 = **0x8007FFCC**, a area da
pilha. E `pfn=0x145884` e o decimal 145884 = **0x239DC**, DENTRO do `ddragonz.mod`
(462 748 bytes): o primeiro valor parece um endereco fora do modulo, e o segundo
nao e. Ler os registos do ledger com esta doenca e ler numeros que nao existem.

Correccao (nao aplicada: sao 4 ficheiros de 4 frentes diferentes): um `%08x`
unico, como ja existe em `bar.cpp:26`, `imedia.cpp:100`, `ihiddevice.cpp:11` e
`ihid_entrada.cpp:12` (quatro ficheiros que ja tem o helper certo).


---

## Sessao 2026-09-15 -- os medidores, medidos (branch `dev`)

Esta sessao comecou com uma pergunta ("esta a 70% de que?") e a resposta honesta
so existe com os medidores a frente. Ficam aqui, com a corrida que os produziu:
`tools/baseline/bateria.json` (referencia) contra a corrida de `dev`.

| medidor | valor | % | nota |
|---|---|---|---|
| carga do `.mod` | 62/62 | 100% | todos devolvem ponteiro de modulo |
| ponteiro de modulo | 62/62 | 100% | |
| vtable valida | 61/62 | 98% | falta o `quake2brew` (carga estoura 4M) |
| applet instanciado | 40/62 | 65% | era 37 |
| ciclo completo (create+start) | 4/62 | 6% | `imicro3d`, `tectoy`, `zeebo_app`, `peggle` |
| pixels escritos | 0/62 | 0% | ninguem chega ao desenho |
| demanda por atender | 8 nomes / 9 pedidos | -- | era 20+ no inicio da sessao |

`ctest` 9/9 (o teste 9 salta sem `ZB2_MODS` na cache, ja era assim), 451 casos de
teste. Regressoes na ultima corrida: **0**, com **16 melhorias**.

### O que subiu de degrau, e porque

| mudanca | commit | medicao |
|---|---|---|
| `STM`/`LDM` com PC na lista nao avancava o PC | `a8d2749` | `push {fp,ip,lr,pc}` reexecutava sempre; **vtable 48->61, applet 37->40** |
| `AEERect` era lido como 4x u32 (e 4x int16) | `e9d8653` | coords lixo -> clip calava o desenho |
| header do bitmap no `GetDeviceBitmap` + QI no `IBitmap` | `e03e25b` | 4 titulos saiam para o ASCII `WERV` |
| `QEGL` delega no `IEGL` (27 slots) + pixmap servida | `77219df` | **16 melhorias**, 0 regressoes |
| `strdup`, `strncmp`, `GetClass`, `Back`, `MkDir`, `Remove`, `GetClass`, `ITextCtl`, PNG, QEGL | `6e5fa74`..`08734b6` | demanda 20+ -> 9 pedidos |

### A licao que voltou a aparecer

O erro de ORDEM no despacho (do mais especifico para o mais generico) apareceu
**pela nona vez**: o ramo do `QEGL` foi escrito *depois* do `AtenderClasse`, que
apanhou os indices 40000+ e transformou 15 pedidos de EGL em `QEGL::?` sem
comportamento. O sintoma foi uma lista de faltas que dizia "slot desconhecido"
para slots que existiam. **Nao e descuido: e uma armadilha estrutural de uma
cadeia longa de `else if`** -- a cura seria uma tabela, e nao ramos.

### Factos novos, medidos fora do codigo (e que servem para a proxima)

1. **`boot.pkg` e um contentor `PACK`** (formato: `"PACK"`, contagem u32, offset
   u32 da tabela de nomes, 256 bytes de padding, depois `count` registos de 20
   bytes: {const 0x00020000, hash, comprimido, descomprimido, offset}), com os
   nomes num ultimo stream zlib (256 bytes por nome, NUL no fim). O `boot.pkg` do
   `karnovr` tem UMA entrada: `boot.rom`, 8192 bytes -- o mesmo tamanho que o
   zeebulator mediu como "portao" da familia Neo Geo.
2. **`karnovr.pkg` tem 22 entradas** (as ROMs: `066-c1..c6`, `066-p1/m1/s1/v1`,
   `000-lo.lo`, `sfix.sfx`, `sp-*.sp1`, `vs-bios.rom`, ...). O guest procura
   `.<jogo>\boot.rom`, `roms\<jogo>\boot.rom` e `roms\neogeo\<jogo>\boot.rom`.
3. **`0x0103d8ec` nao e uma classe do SDK** (so aparece em comentarios de teste
   OpenVG) e **`0x01011810` nao aparece em lado nenhum** do SDK extraido. O
   segundo e o unico CLSID que resta por identificar.
4. **`STM`+PC**: o prologo GCC `push {fp,ip,lr,pc}` (0xE92DD800) e a instrucao que
   faltava; ela so aparece em titulos >576KB, e por isso 13 deles pareciam
   "sem vtable" (a vtable era consequencia, e nao causa).

### Em aberto (frentes com agente ou medidas a faltar)

1. `quake2brew`: unica vtable que falta -- carga a vaguear (memcpy com contagem
   vinda de campo nao inicializado) e salto para o ASCII do proprio cabecalho.
2. `0x01011810` (1 pedido, `tectoy`): sem cabecalho no SDK. Stub nomeado e o
   proximo passo honesto.
3. `abd`/`torkandkral`: pedem `eglInitialize` com um `dpy` (0xf0027390) que o
   emulador nunca deu -- o valor e o endereco da saida do proprio `GetDisplay`.
   Fica registado com o endereco na recusa.
4. Contadores de desenho da bateria (`textos`, `blits`, `updates`) sao **colunas
   mortas**: o motor conta-os por dentro e a ferramenta nunca os le.
5. `max_steps` do corpus e ignorado: o `cnk2` pede 186M passos na carga e recebe
   o teto fixo de 4M.

---

## Sessao de 15/09 -- ICM (`0x01011810`) identificado e recorte do plano proximo

Retomada depois de um crash de RAM/swap (o culpado foi `/tmp/bateria_media.err`,
828 MB de log em laco; apagado). Dois commits novos, ambos com medicao.

### 1. `0x01011810` = `AEECLSID_CM` (ICM, Call Manager) -- commit `be1cd3f`

O ultimo CLSID por identificar (ponto 3 dos "factos novos" acima) **deixou de
ser desconhecido**. Nao foi adivinhado: veio de tres fontes independentes que
concordam --

| fonte | caminho | o que diz |
|---|---|---|
| `zeebx-emu` | `src/brew/aee_slots.rs:1027-1056` | slot 28 = `ICM_GetSSInfo`, buffer de `0x340` bytes |
| `zeebx-emu` | `src/machine/diversos.rs` (`cm_call`) | `+0x00`=2 (servico pleno), `+0x0C`=5 (online), `+0x28`=0x0048 (sinal) |
| `zeemu` | `brew/BrewShell.cpp:2730` | mesma classe, mesmo slot |

Implementado em `core/brew/classes.{h,cpp}` como `Classe::kCM` (indice 6, 29
slots). O `GetSSInfo` zera o buffer, escreve os tres campos e devolve sucesso;
`pinfo == 0` ou `tamanho < 0x2A` devolve `kAeeBadParm`. Teste
`OCMRespondeGetSSInfoComRadioNoArEServicoPleno` em `tests/classes_test.cpp`.

Efeito colateral necessario: `kObjetoIgles` mudou de `0x8F006000` para
`0x8F010000` -- o objecto da classe nova colidia com a faixa antiga.

**Medido no `tectoy` (pasta 274755), antes e depois:**

| | antes | depois |
|---|---|---|
| `IShell::CreateInstance CLSID desconhecido` | 1x | **0** |
| `IAppHistory::Back` / `GetClass` | 1x cada | **0** |
| `passos_start` | 321 | **4 000 000** (orcamento) |
| motivo do `start` | `retornou` | `orcamento_esgotado` |
| falta nova | -- | `IShell::slot21` (= `SendEvent`) 1x |

O `start` que "retornava" em 321 passos era o applet a **desistir**; agora corre
ate ao teto e pede `IShell::SendEvent(clsTo=0, clsFrom=0x01070798, evt=0x7b0a)`.
Isto e um degrau: o titulo passou de abortar em silencio a rodar o seu ciclo.

**Ruling**: `0x01006c01` (`LCT_SIMCardCtl`) continua RECUSADO de proposito, como
no zeebx. Custo se errado: o `tectoy` pede SIM e apanha uma recusa nomeada em vez
de um stub que mente. Custo de stubar: inventar estado de SIM sem medicao.

### 2. Recorte contra o plano proximo -- commit `c60bd49`

O ponto 2 do cabecalho de `core/video/rasterizador.h` dizia "SEM RECORTE DE
FRUSTUM: um triangulo com qualquer vertice com `w <= 0` e DESCARTADO". Deixou de
ser verdade, e o cabecalho foi reescrito para dizer o que passou a fazer.

Sutherland-Hodgman contra **um** plano (`clip.z + clip.w >= 0`), com os atributos
(cor e UV) interpolados linearmente na aresta cortada; `t = d_a / (d_a - d_b)`.
O poligono sai com 3 ou 4 vertices; com 4, e dividido em leque (`0,1,2` e
`0,2,3`). So DEPOIS do recorte e que `Projetar` corre -- projectar antes dividia
por um `w` negativo. Contador novo: `TriangulosRecortados()`.

Dois testes, ambos com numeros escritos:
- `OTrianguloQueAtravessaOPlanoProximoERecortadoEmVezDeDesaparecer`: A e B a
  frente, C atras (`w = -2`). Antes: **0 pixels**, 1 descartado. Depois:
  **32 pixels** (metade superior da janela 8x8), 1 recortado, **0 descartados**,
  2 triangulos.
- `OTrianguloTotalmenteAtrasDoPlanoProximoEDescartado`: os tres vertices atras;
  0 pixels, 1 descartado, 0 recortados. (A prova de que o recorte nao "salva"
  o que devia morrer.)

### Estado medido no fim da sessao

`build/zb2_bateria corpus62.json ... /tmp/corrida_dev.json`, comparado com
`tools/baseline/bateria.json` por `build/zb2_comparar`:

```
carga:   62 -> 62
modulo:  62 -> 62
vtable:  48 -> 61
applet:  37 -> 40
resultado: 0 regressao(oes), 16 melhoria(s) em 62 titulos -- SEM REGRESSOES
```

114 campos neutros mudaram (faltas 36, motivo 29, `passos_create` 25,
`passos_carga` 14, `passos_start` 7, recusadas 2) -- neutros por decisao da
tabela `kCampos` de `tools/comparar.cpp`, e nao por conveniencia.

Testes: **472 casos, 467 verdes, 5 saltados** (os `BarDeVerdade`/`RecursosDeVerdade`
que precisam do corpus na cache). `ctest` 10/10 (o `regressao` salta).

### Correccao ao metodo, para nao repetir

A primeira corrida do `tectoy` foi feita com um corpus escrito a mao com
`clsid_hex: "0x01006c00"` -- **errado**; o valor certo (`0x1070798`) estava em
`corpus62.json` o tempo todo. A corrida deu "sem applet" e quase foi lida como
falha da implementacao. **Regra**: um corpus de um titulo so se faz por RECORTE
do `corpus62.json` (grep + copia da entrada), nunca escrevendo os campos de novo.

Segundo tropecao na mesma sessao: `make zb2_tests` compila os testes, **nao** a
bateria. A primeira corrida "depois do ICM" ainda usava o binario velho e
continuava a acusar o CLSID desconhecido. **Regra**: antes de medir, `make -j4`
(alvo omisso = todos), e nao o alvo dos testes.

### Em aberto, actualizado

1. `quake2brew`: continua a unica vtable em falta (ponto 1 da lista anterior).
2. ~~`0x01011810`~~ **RESOLVIDO** nesta sessao (`be1cd3f`).
3. `abd`/`torkandkral`: `eglInitialize` com `dpy` que o emulador nunca deu --
   inalterado.
4. `IShell::SendEvent` (slot 21): a falta NOVA do `tectoy`, e o proximo passo
   honesto para esse titulo. A fila de eventos do BREW nao existe.
5. `pixels` continua **0/62**: nem o recorte do plano proximo nem a correccao de
   perspectiva mudaram isso, porque nenhum titulo chega ao `glDraw*`. As duas
   mudancas sao correctas por teste unitario, e **nao** por medicao de titulo --
   fica escrito para nao serem lidas como "o desenho ja funciona".

---

## Sessao de 15/09, segunda parte -- cinco frentes em paralelo, e o dia em que PIXELS saiu do zero

Retomada com cinco sub-agentes, um por frente, cada um no seu worktree. **Os
agentes corrigiram-me quatro vezes**, e isso foi o mais valioso da ronda: tres
premissas minhas estavam erradas e uma proposta minha desfazia uma decisao
correcta tomada antes. Fica tudo escrito abaixo, porque o valor esta no erro.

### O que mudou, medido

| degrau | baseline (`tools/baseline/bateria.json`) | fim desta ronda |
|---|---|---|
| carga | 62/62 | 62/62 |
| ponteiro de modulo | 62/62 | 62/62 |
| vtable valida | 48/62 | **62/62** |
| applet instanciado | 37/62 | **56/62** |
| **pixels escritos** | **0/62** | **10/62** |

Quatro commits, cada um medido em ISOLADO (a corrida anterior e a referencia da
seguinte), todos com `0 regressoes`. 478 testes verdes.

### 1. `e68a35f` -- o tecto de passos e o que o corpus declara

O `max_steps` estava no `corpus62.json` DESDE SEMPRE e a ferramenta ignorava-o: o
`cnk2` pedia 186 486 543 passos e recebia 4 000 000 (2,1%). O campo passou a ser
lido (e um numero, nao uma cadeia -- precisa de leitor proprio, delimitado pela
chaveta do objecto, senao um titulo sem campo apanha o do seguinte).

`kLimite` 4M -> 8M pelo `quake2brew`, que precisa de 5 932 075 passos so para a
CARGA. **A hipotese do memcpy corrompido estava ERRADA**: o laco em 0x144 e a
zeragem legitima da ZI/BSS (campo +0x24 do cabecalho = 0x0076de90 bytes = 1 947 556
iteracoes de tres instrucoes). Nenhum campo por inicializar. Era orcamento curto,
e nao defeito de carregador. Confirmado por segunda ferramenta (`zb2_sonda_mod`)
e pelas referencias (zeebulator 64M, zeemu 500M).

**Ruling revertido a meio**: propus reactivar `kOrcamentoSegundos` como guarda de
tempo. Fui ler o codigo e o projecto JA tinha removido o orcamento por relogio de
proposito -- viola o P4, porque o emulador passaria a medir a carga da maquina e o
`tools/comparar` acusaria regressoes de jogabilidade que eram regressoes de CPU do
anfitriao. A constante morta foi APAGADA com o motivo escrito, para nao ser
reinventada uma terceira vez. **Uma proposta minha desfazia uma decisao correcta;
o codigo defendeu-se sozinho porque o porque estava escrito.**

Efeito isolado: vtable 61 -> 62, applet 40 -> 41. **VTABLE 62/62, o primeiro degrau
completo do projecto.**

### 2. `d0f1146` -- dois defeitos silenciosos do interpretador ARM

Os dez titulos com `create:retornou_sem_applet` **nao sao jogos**: sao UM emulador
multi-arcade dentro do BREW (`emulator_neo` v0.95, de Miguel Angel Horna), com
drivers NEOGEO + CPS1 + CPS2 + System16/18 + Data East no mesmo `.mod`. Medido nas
strings: `c:/my_code/emulator_neo/framework/*.cpp`. O `toyraidzeebo` **nao e da
familia** -- so partilhava o sintoma.

A causa nao era BREW nem VFS. Eram dois defeitos nossos, ambos com `recusadas = 0`:

1. **`LDR PC,[Rn,#imm]`**: o despacho escrevia `pc + 4` DEPOIS da transferencia e
   anulava o salto. Esta familia chama o sistema SO assim (`mov lr,pc` +
   `ldr pc,[tabela,#slot]`): **5240 ocorrencias** so no `karnovr.mod`. O `malloc`,
   o `free` e o `CreateInstance(AEECLSID_DISPLAY)` nunca corriam. E como o PC nunca
   entrava na faixa de saida, NAO HAVIA FALTA PARA REGISTAR.
2. **C e V escritos sem o bit S** em SUB/RSB/ADD/ADC/SBC/RSC. O `add r0,r0,#16`
   apagava o carry do `cmp` e o `bcc` ficava sempre tomado: laco infinito, vivo aos
   60 milhoes de passos. As de logica nao o faziam, e foi por isso que atravessou a
   etapa 1 inteira.

Efeito isolado: **applet 41 -> 56**, 15 melhorias. Alem dos dez da familia: `nfs`,
`prey3d`, `zeebotennis`, `Boiaz`, `allstarcards`.

### 3. `f15d752` -- a vtable do IBitmap, e o dia em que PIXELS saiu do zero

Achado a investigar OUTRA coisa. O `abd` e o `torkandkral` apanhavam
`EGL_BAD_DISPLAY` com um `dpy` (0xF0027390) que o emulador nunca emitiu. **O EGL
estava certo.** O 0xF0027390 era LIXO DE PILHA:

    ldr r0,[sp]     r0 = 0x80050300   (o IBitmap do ecra)
    ldr r1,[r0]     r1 = 0xF0007D00   (a vtable do bitmap)
    ldr r1,[r1,#4]  -> 0              (o slot 1, `Release`, A ZERO)
    blx r1          -> salta para 0

`tools/bateria.cpp` era a UNICA cablagem da vtable do IBitmap e cablava so o slot
2. Os slots 0 e 1 (IBase) nunca eram escritos. O guest saltava para 0, passava a
executar o cabecalho do proprio `.mod` como codigo, e derramava a pilha.

`DefinirVtableBitmap` passou a receber as `Saidas` e a fazer AS DUAS COISAS --
guardar o endereco E construir a tabela. Enquanto foram duas chamadas, a
ferramenta fazia a primeira e ninguem fazia a segunda.

Efeito isolado: **0 -> 640 pixels em dez titulos**. A coluna `PIXELS` estava a
`0/62` desde o inicio do projecto.

**Duas premissas minhas caidas nesta frente**: a recusa era em `egl.cpp:368` e nao
`:373`; e a hipotese de que os titulos precisavam da extensao
`EGL_QUALCOMM_get_color_buffer` NAO se sustenta -- ha **zero** chamadas a
`eglQueryString`/`eglGetProcAddress`/`glGetString` nos 62 titulos. Morrem antes.

### 4. `95c9859` -- o IDisplay, e a licao do dia

Ia corrigir SO a conversao RGB565 do `SetColor`. Ao abrir o
`zeebx-emu/src/machine/display.rs:18-25` para comparar, vi que ele le o `r2` onde
nos liamos o `r1`. O SDK confirma (`AEEIDisplay.h:232`):

    RGBVAL IDisplay_SetColor(IDisplay *po, AEEClrItem clr, RGBVAL rgb)

**Tres argumentos, e devolve a cor ANTERIOR.** O nosso codigo tinha tres defeitos
sobrepostos: lia a cor do `r1` (que e o ITEM, 1..16 -- usava-se o numero do item
como cor); truncava o RGBVAL com `& 0xFFFF` (e `RGB_WHITE` e `MAKE_RGB(255,0,0)`
davam AMBOS 0xFF00); e devolvia 0, quando o idioma do proprio cabecalho
(`:134-136`) e guardar o retorno e repo-lo -- com zero, o jogo repunha PRETO.

**Sem ler a referencia, teria convertido correctamente o argumento errado** -- um
defeito mais dificil de ver que o original.

Tambem: `GetClipRect` escrevia DEZASSEIS bytes num `AEERect` de OITO. Os oito a
mais caiam na pilha do guest, por cima das suas proprias locais. O `SetClipRect`,
no mesmo ficheiro, JA lia int16. Era metade do par por corrigir.

Efeito isolado: **0 regressoes, 0 melhorias, 0 campos neutros**. Nenhum dos 62
exercita hoje estes caminhos de forma que mude a medicao. Fica escrito: estas duas
estao provadas por TESTE e pelo SDK, **nao** por medicao de titulo.

### A demanda mudou de forma, e isso e o mapa da proxima ronda

Antes desta ronda: **8 nomes / 9 pedidos**. Agora: **37 nomes**, muito mais
pedidos. Nao e uma regressao -- e o que acontece quando 56 titulos passam a criar
applet e a andar. Os que mais aparecem:

| pedidos | falta | leitura |
|---|---|---|
| 70x | `IFile::slot3` | de longe o maior. Uma so chamada de ficheiro a bloquear muita coisa |
| 11x | `IBitmap::slot12` | agora que a vtable existe, os jogos usam-na a serio |
| 11x | `IThread::Start` | |
| 10x + 40x | `IGLES11::slot*` (14 slots distintos) | o GL de verdade a ser pedido pela primeira vez |
| 10x | `AEEHelperFuncs[0x00c] strcat` | |
| 9x | `AEEHelperFuncs[0x184] sleep` | |
| 1x | `IShell::slot21` (`SendEvent`) | so o `tectoy`, e ja esta com agente |

### Metodo: o que se confirmou nesta ronda

1. **Ler a referencia ANTES de propor.** Apanhou tres defeitos que eu nao ia ver.
   O `SetColor` e o caso exemplar: a minha correccao estava certa e aplicada ao
   argumento errado.
2. **Medir cada commit em isolado**, usando a corrida anterior como referencia da
   seguinte. Sem isso, os 15 titulos do interpretador e o 1 do `max_steps` viriam
   misturados e nao se saberia o que valeu o que.
3. **Provar o teste VERMELHO antes do verde**, arrancando a correccao. O do
   `GetClipRect` falhou nos quatro bytes-sentinela; o do IBitmap deu
   "IBitmap::Release sem endereco: o guest faz blx 0".
4. **Dizer quando a medicao nao mudou.** O `95c9859` nao move um numero da bateria.
   Escrever isso e o que impede que seja lido como ganho.
5. Um sub-agente que contradiz o enunciado que recebeu vale mais do que um que o
   cumpre: quatro das correccoes desta ronda vieram de agentes que disseram
   "a premissa que me deste esta errada, e aqui esta a medicao".
