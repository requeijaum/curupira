# Brainchallenge: heap failure 0x658d3000

Trace temporario no dispatcher HLE mediu `malloc` com pedido bruto `0xe58d3000`, tamanho apos mascarar `0x658d3000`, LR `0x1e32b`.

O helper guest `0x1e318` apenas encaminha seu argumento para o alocador. Watchers mostram o wrapper `0x1e30c` sendo chamado pelo caller `0x111be` com esse valor.

`0x111a0` monta um inteiro dos bytes `[r4+8..11]` e chama o wrapper. No caso valido `r4=0x10008cb8`, bytes `05 00 01 00`. No caso falho `r4=0`, bytes em endereco base-zero sao opcode `00 30 8d e5`, que viram `0xe58d3000`.

O caller `0x42e50` chama `0x42d3c` e passa o retorno em `r0` a `0x111a0`; no caso falho, `r0=0` e `r1=0xffffffff`. Portanto o proximo experimento e determinar por que `0x42d3c` retorna essa dupla. Nao corrigir por clamp de malloc ou pagina zero inventada.

## Cadeia HLE apos falha

Sonda de `0x42d0c` mostrou `r3=0xf0001780`, que mapeia para `kSlotIdMemset=1504`. O wrapper `0x1e318` faz malloc e tail-call para memset. Com tamanho corrupto, malloc devolve zero e o guest chama `memset(0,0,0xe58d3000)`, retornando ao parser nulo. A causa continua antes do malloc: `0x42d3c` recebe/propaga origem nula.

## Retorno de 0x42d3c

Sonda no epilogo `0x42e42` mediu no caso falho: `r0=0`, `r1=0`, `r4=0`, `r5=0xe58d3000`, `r6=0xffffffc0`, `lr=0x1e33b`. `r5` vem do helper `0x4667c` e propaga opcode lido da origem nula; o parser retorna nulo, mas o caller `0x42e50` nao testa esse retorno antes de `0x111a0`.

## ABI de memset confirmada

Sonda temporaria no `kSlotIdMemset=1504` mostrou chamadas normais, por exemplo `memset(0x10008cb8,0,0x28a4)`, e a unica falha como `memset(0,0,0xe58d3000)` com `lr=0x1e33b`. Portanto a ponte HLE recebe ABI correta; o ponteiro nulo/tamanho opcode ja existem no guest antes da chamada. Proximo experimento deve rastrear produtor de `r1=0` na entrada de `0x42d3c`.
