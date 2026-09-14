# Zeebulator 2 -- desenho

Branch `full-rewrite`. Este documento vem antes do codigo, e existe para que as
decisoes sejam discutidas antes de custarem semanas.

## Porque reescrever

Nao e porque o codigo esteja mal. E porque **o custo dominante do projeto nao e
o emulador, e o desconhecimento** -- e o desenho actual nao foi feito para o
reduzir.

Numeros da sessao de 13/09 que sustentam isto:

- **A instrumentacao mentiu mais vezes do que o emulador falhou.** Sete casos
  medidos: 71 dos 84 handlers de GL sem log nenhum; `GlBindTexture` com a linha
  duplicada; logs de depuracao a nascerem na pasta do titulo (midia de usuario);
  a amostragem de PC a aliar com lacos de periodo potencia de dois; o contador de
  `wander` a nao ser comparavel entre interpretador e JIT; o censo a medir
  imagem em vez de jogabilidade; e o canal de controle a morrer para todas as
  conexoes seguintes por causa de um pedido preso.
- **`glCullFace` descartou 86 377 chamadas em silencio.** Ninguem sabia, porque
  havia um stub que devolvia sucesso e nao fazia nada.
- **Duas constantes com o nome da funcao errada** (`kStrstrSlotOffset` guardava o
  offset do `stristr`), o que fez tres testes chamados `Strstr*` testarem o
  `stristr` -- e um deles passava por acidente.
- **Dois testes afirmavam comportamento falso** e passavam porque a memoria
  estava a zero.
- **O mapa de slots do IGLES11 tinha sido copiado por citacao** de outro
  emulador, nao medido.
- **Comparei numeros de corridas diferentes duas vezes** e chamei a um deles
  regressao.
- **Os descartes eram invisiveis**: nao havia, em lado nenhum, a lista do que
  estava implementado a serio e do que era fachada.

O zeebx, do outro lado, tem o problema oposto: chegou a produto (interface,
biblioteca, actualizacao, Discord) mas assenta nas mesmas incognitas, e as suas
mensagens de commit **nomeiam a medicao** -- que e o que torna o trabalho deles
auditavel. Dessa pratica vem o principio P1 abaixo.

## Os sete principios

**P1 -- Nenhuma afirmacao entra sem medicao que a sustente.**
Cada comportamento implementado tem, no proprio codigo, a medicao que o
justifica: um endereco, um numero, um comando que o produz. Um comentario que diz
"isto e assim" sem dizer como se sabe nao entra.
*Vem de:* a regra de evidencia ja usada nesta sessao, e das mensagens de commit
do zeebx.

**P2 -- Stub silencioso e proibido. Recusa ruidosa e obrigatoria.**
O estado por omissao de qualquer caminho nao implementado e **recusar e
registar**, nunca "devolver sucesso e nao fazer nada". Uma vtable incompleta
recusa ao ser instalada, com o nome do que falta.
*Vem de:* as 86 377 chamadas descartadas em silencio.

**P3 -- O corpus e a especificacao.**
Os 62 titulos tem, cada um, um estado medido e registado. Um titulo que piora
falha a bateria. O progresso nao e uma opiniao sobre uma captura de ecra: e um
numero por titulo, versionado.
*Vem de:* o censo medir imagem em vez de jogabilidade, e de "capturei a tela e
parece melhor".

**P4 -- Determinismo por construcao.**
Tempo, aleatoriedade e ordem de threads sao injectados, nunca lidos do sistema.
O mesmo titulo com a mesma entrada produz a mesma sequencia de eventos e o mesmo
ecra, byte a byte. Toda a medicao se apoia nisto.
*Vem de:* o JIT mudar o entrelacamento de temporizadores e tornar o zenonia
**outro jogo**, nao um jogo mais lento.

**P5 -- As incognitas ficam isoladas atras de uma fronteira, com sonda propria.**
O que exige engenharia reversa e a parte cara. Cada pedaco desses vive atras de
uma interface pequena, com uma sonda que o observa de fora -- para que o esforco
de RE seja limitado, reutilizavel e nao contamine o resto.
*Vem de:* as quatro hipoteses seguidas que eu levantei e derrubei sobre o
Zeebo Extreme, e do custo de as derrubar tarde.

**P6 -- Um escritor por memoria.**
A memoria do guest tem um unico escritor: a thread da CPU. Os subsistemas
(HLE, audio, video) sao chamados de dentro dela, de forma sincrona. Nao ha
migracao de subsistema para thread propria.
*Vem de:* medi que nenhum dos travamentos tinha subsistema nosso envolvido
(`hle_depth=0`), logo a migracao nao resolveria nada -- e a memoria do guest e
partilhada por todos, portanto passar a varios escritores troca um bug visivel
por corridas silenciosas.

**P7 -- O instrumento e um produto, nao um script.**
Construido primeiro, com testes proprios, e tratado como codigo de producao.
Um log so entra se puder ser verdadeiro; se nao puder, nao entra.
*Vem de:* os sete defeitos de instrumento acima.

## Arquitetura

    core/
      tempo/        relogio injectado: uma so fonte de tempo, avancavel a mao
      memoria/      espaco de enderecos do guest, um escritor, vigias de escrita
      cpu/          interface ICpu; ArmInterpreter (referencia) e Jit (rapido)
      traco/        instrumentacao: cabecalho de cada evento, contadores, sonda
      carga/        MOD, BAR, VFS, GGZ, PAKZ, EFS2, MIF
      codec/        PNG, GIF, BMP, JPEG, ATITC, OBM1, ADPCM, WAV
      brew/         runtime: IShell, IDisplay, IFile, IMedia, IGL, IHID, ...
      video/        rasterizador (referencia) e backend GL
      audio/        misturador, sintese
      guarda/       laco de eventos: timers, threads, sinais, callbacks
    tools/
      sonda/        executor de um titulo com a instrumentacao ligada
      bateria/      os 62 titulos, com orcamentos e estados registados
      comparar/     diff de duas corridas (o juiz das regressoes)

### As tres decisoes estruturais

**1. O traco vem antes da emulacao.** `core/traco/` e o primeiro modulo a
existir e a funcionar. Cada evento tem um cabecalho comum (quem, onde, com que
argumentos, quando) emitido por um registador unico. Nenhum modulo escreve
`fprintf` directamente; todos pedem ao registador. Isso torna impossivel repetir
os sete defeitos: nao ha linha sem nome, nao ha destino por omissao na pasta do
titulo, nao ha contador que se compare entre configuracoes sem o declarar.

**2. Cada tabela de vtable e DECLARADA, e a instalacao verifica.** Uma tabela de
interface nomeia todos os slots; instalar uma tabela com slots por preencher
**falha em tempo de construcao**, dizendo quais. Os nomes vem do SDK e sao
verificados contra o SDK por um teste -- como se fez com o `AEEHelperFuncs`
(117 slots, zero divergencias). Copiar a ordem de outro emulador e proibido por
teste, nao por disciplina.

**3. Nao ha `bool` a fingir sucesso.** Toda operacao devolve um resultado que
distingue **feito**, **recusado** e **nao implementado**. O terceiro e um erro em
desenvolvimento, nao um valor de retorno silencioso.

## O que se aproveita do Zeebulator

O desenho e novo; o **conhecimento** e o mesmo, e ja esta escrito:

- `docs/AUDITORIA-ZEEBX-v0.1.0.md`, `docs/SIMCARDCTL-CONTRATO-DO-OEM.md`,
  `docs/PAREAMENTO-DE-UIDS-MEDIDO.md`, `docs/DIAGNOSTICO-DE-VIVACIDADE.md`,
  `docs/INVESTIGACAO-POR-SUBSISTEMA.md`, `docs/LACUNAS-2026-09-13.md`.
- `docs/medicoes/` -- a tabela No-Intro, os 62 logs da bateria, os digestos do
  estudo de skills.
- As medicoes que refutaram hipoteses: bytes de ficheiro identicos ao `pakz`;
  o `aee_GetUpTimeMS` com zero chamadas; zero copias longas de `strcpy`; o
  `MM_STATUS_DONE` provado no tratador do `cnk2`.
- O mapa de slots do IGLES11 verificado contra o wrapper do SDK.

O que **nao** se aproveita e o codigo. Nao por estar errado, mas porque cada
linha dele nasceu sem os principios acima e sem os testes que os sustentam.

## Onde o Zeebulator fica

Intacto na branch `new_ez_ui`. Nada nesta branch o altera. O `full-rewrite` e um
recomeco no mesmo repositorio, para que a comparacao seja possivel e para que o
corpus, as ferramentas e os documentos continuem a ser os mesmos.
