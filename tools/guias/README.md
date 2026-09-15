# tools/guias -- guiaos de ENTRADA, um ficheiro por titulo

Um guiao e um roteiro de teclas: `ZB2_GUIAO=<pasta>` faz a bateria ler
`<pasta>/<nome_do_titulo>.txt` antes de correr esse titulo, e injectar os eventos
no IHID no instante pedido. Sem a variavel, nada muda: os numeros sao os de sempre.

Formato (o mesmo de `ZB2_ENTRADA`, `core/brew/ihid_entrada.h`):

    <t> <eixo|botao> <uid> <valor>     # `#` comenta; uid decimal ou hex

Uids, do arquivo do proprio console (`hid_devices.cfg`) e das tabelas de salto dos
jogos -- a tabela completa esta em `core/brew/ihiddevice.cpp`, `kBotoesDoZeebo`:

    botao 0x0106c40a  Button_1 (A)        botao 0x0106c402  Start
    botao 0x0106c3fe  DPad_Up             botao 0x0106c3ff  DPad_Left
    botao 0x0106c400  DPad_Down           botao 0x0106c401  DPad_Right
    eixo  0x0106c4d0  X    eixo 0x0106c4d1 Y     (0..255, centro 128)

## A ESCALA DO `t`, que e a armadilha deste formato

**O `t` nao e tempo de parede: e o RELOGIO INJECTADO, e nessa arvore o relogio
avanca 1 ms POR INSTRUCAO** (`core/brew/despacho.cpp`, o `++agora_ms_` do laco de
eventos; a entrada le o mesmo relogio por `entrada_.Repor`).

Consequencia medida, e e onde um guiao "obvio" falha: 300 quadros pedidos podem
acabar em 2 quadros, e a CARGA de um modulo pode gastar 8 milhoes de passos.
`ZB2_ENTRADA="0 botao ... 1"` num titulo cujo `create` acaba aos 10150 passos
injecta a tecla DURANTE A CARGA, antes de o applet existir -- a leitura de
referencia em `docs/rewrite/AUDITORIA-PLANO-E-TESTES.md` (etapa 8) mediu isso como
"o guiao nao muda nada".

Onde cair: `a linha ENTRADA <titulo>` que a bateria escreve em `stderr` da, por
titulo, `aplicados`, `consumidos`, `perguntas` e `relogio_final`:

    ENTRADA alpineracerex guiao=... eventos=1 aplicados=1 (botao=1 eixo=0)
      ultimo_aplicado=2430000 consumidos=1 ultimo_consumido=2430000 perguntas=2
      vazias=1 relogio_final=2430053 passos=2430052

  * `aplicados=0` -- o instante pedido NAO coube na corrida: o guiao esta fora do
    alcance, e o numero do `relogio_final` diz onde e que ela acabou.
  * `aplicados>0, perguntas=0` -- o jogo nunca perguntou pelo controle: nao ha
    guiao que o mova por aqui.
  * `consumidos>0` -- o jogo LEU o evento.

## O que este caminho NAO faz (medido, 2026-09-15)

A entrega e POR BORDA, no instante: o sinal `RegisterForButtonEvent` so e marcado
se o evento for injectado DEPOIS de o jogo o registar. Um evento injectado antes
fica na fila e o jogo pode le-lo -- mas so se ele proprio PERGUNTAR
(`GetNextButtonEvent`); um jogo que so reage ao callback nunca o ve. Medido em
`alpineracerex`: uma tecla aos 2 410 000/2 420 000 passos nao faz nada
(`perguntas=0`), a mesma tecla aos 2 435 000 muda o caminho do `start`
(`saiu_do_modulo` + 2 recusas -> `retornou`, `consumidos=1`).

## Comparabilidade

`ZB2_GUIAO` muda a ENTRADA, e a bateria avisa disso em `stderr`. Uma corrida com
guiao NAO se compara com `tools/baseline/bateria.json` (que e sem guiao): os
pixels e o motivo mudam porque a entrada mudou. `tools/regressao.sh` nao define
`ZB2_GUIAO`, logo a referencia versionada nao e afectada.
