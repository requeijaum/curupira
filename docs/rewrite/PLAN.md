# Zeebulator 2 -- plano incremental

Regra de cada etapa: um entregavel **testavel em isolamento**, um criterio de
aceitacao que corre num comando, e um titulo do corpus que a prova.

Ordem escolhida por uma razao: **cada etapa so depende das anteriores**, e cada
uma acrescenta um titulo novo a lista dos que sobem.

## Etapa 0 -- A fundacao de medicao (sem emulacao)

Entregavel: `core/tempo/`, `core/traco/`, `core/memoria/` e os testes.
- Relogio injectado: avanca a mao, determinista, uma so fonte.
- Registador unico de eventos: cabecalho comum, destino configuravel, proibicao
  de caminhos relativos.
- Espaco de enderecos do guest com vigia de escrita e um unico escritor.

Criterio: `ctest` verde, e um teste que **prova** que dois registadores com o
mesmo argumento produzem a mesma linha, e que escrever num endereco vigiado
dispara o aviso uma vez por escritor.

Titulo: nenhum. Esta etapa nao emula nada, e e a que evita os sete defeitos de
instrumento da sessao anterior.

## Etapa 1 -- CPU de referencia

Entregavel: `core/cpu/ArmInterpreter`, ARM e Thumb, com o conjunto que o
`chessbots.mod` usa para ler o CPSR.
Criterio: `mrs/cpsr` devolve um modo **valido** (armadilha ja medida: o
Zeebulator comecava em `cpsr=0`, que nao e modo nenhum), e um teste diferencial
contra um conjunto de instrucoes com resultado conhecido.

Titulo: nenhum ainda; a CPU precisa de um carregador para correr alguma coisa.

## Etapa 2 -- Carregador de MOD e o primeiro titulo

Entregavel: `core/carga/mod`, `core/carga/bar`, execucao de `AEEMod_Load` e
`IModule::CreateInstance`.
Criterio: o `imicro3d` (o mais simples do corpus) carrega, o applet pointer e
nao nulo, e o `HandleEvent(EVT_APP_START)` retorna.

Titulo: `imicro3d`.

## Etapa 3 -- IShell, IDisplay e desenho 2D

Entregavel: `core/brew/ishell`, `core/brew/idisplay`, `core/video/rasterizador`.
Criterio: um titulo 2D desenha e o ecra tem mais de 10 cores. Verificado na
bateria, nao numa captura a olho.

Titulo: `zumar` ou `pacmania`.

## Etapa 4 -- Ficheiros, VFS e os 62 titulos a correr

Entregavel: `core/brew/ifile`, `core/brew/vfs`, `core/carga/ggz` e `pakz`.
Criterio: **os 62 titulos arrancam e a bateria regista um estado por titulo**.
Nao precisam de desenhar; precisam de nao travar e de produzir um numero.

Titulo: os 62. Esta e a etapa que cria a especificacao executavel (P3).

## Etapa 5 -- Midia e audio

Entregavel: `core/brew/imedia`, `core/audio/`.
Criterio: um titulo toca som e o misturador reporta amostras nao nulas.
Titulo: `a3d` (que ja toca no Zeebulator).

## Etapa 6 -- GL e as duas famílias 3D

Entregavel: `core/brew/igl`, `core/video/gl`.
Criterio: um titulo 3D desenha geometria com textura.
Titulo: `ddragonz` ou `cnk2`.

## Etapa 7 -- A tabela de interfaces verificada contra o SDK

Entregavel: o gerador que le os cabecalhos do SDK e produz as tabelas de slots,
mais um teste que falha se uma tabela instalada tiver slot por preencher.
Criterio: as tabelas do `AEEHelperFuncs` (117), do `IGL`, do `IGLES11` e do
`IHIDDevice` conferem com o SDK, com zero divergencias.

Titulo: nenhum; e o que impede repetir o mapa copiado por citacao.

## Etapa 8 -- Entrada

Entregavel: `core/brew/ihiddevice`, com o pareamento de UIDs **medido**.
Criterio: o d-pad responde. O formato do valor ainda esta por determinar -- a
medicao que falta esta descrita em `docs/PAREAMENTO-DE-UIDS-MEDIDO.md`.

Titulo: `tectoy` (o Z-Wheel, que tem menu navegavel).

## Etapa 9 -- Regressoes automaticas

Entregavel: `tools/bateria` e `tools/comparar` a correr em cada commit.
Criterio: mudar o emulador de forma a piorar um titulo **falha** a bateria, com o
numero que piorou.

Titulo: os 62.

---

## O laco de trabalho

Inspirado no que estudei (`obra/superpowers`, `mattpocock/skills`), adaptado ao
facto de eu trabalhar sozinho aqui:

1. **Um titulo de cada vez**, do mais simples ao mais complexo.
2. **Antes de mexer, um comando que fica vermelho** por causa do defeito e verde
   depois. Sem isso, nao avanco.
3. **3 a 5 hipoteses ranqueadas** antes de testar qualquer uma.
4. **Cada log de depuracao com marca unica** (`[DEBUG-xxxx]`) para a limpeza ser
   um `grep`.
5. **3 correcoes falhadas -> questionar o desenho**, nao tentar a quarta.
6. **Progresso no ledger**, nao na memoria.
7. **Ruling em vez de paragem**: decidir, registar, continuar.

## O que NAO faz parte deste plano

- Interagir com o `zeebx` para alem de ler e re-medir. A regra de evidencia
  continua: mensagem de commit alheia e pista, nao prova.
- Tocar no `Zeebulator` da branch `new_ez_ui`. Fica intacto.
- Publicar sem autorizacao.
