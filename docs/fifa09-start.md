# FIFA09: saida do modulo no start

A corrida canonica chega ao codigo ARM em `0x8300`. O construtor aloca uma estrutura e em `0x837c` faz `r0=[r6]`, `r1=[r0]`, `blx r1`.

O trace Curupira mediu na saida: `lr=0x0000838c`, `r0=0x10001800`, `r1=0xea00000e`. `0xea00000e` e palavra de codigo ARM (branch), nao ponteiro de funcao; `blx` salta para `0xea00000e` e sai do modulo antes de HUD/render.

O bug de HUD reportado no Zeebx nao pode ser atribuido a texto Curupira enquanto esse objeto/ponteiro em `r6` estiver corrompido. Proximo experimento: registrar caller/valor de `r6` antes de `0x837c`.

## Segunda entrada nao-ROPI

Vigia instalada antes do loader nao registrou escrita em `0x10001800`: o carregamento usa caminho bruto que nao passa pela vigia de byte. O trace confirmou segunda entrada `pc=0`, `r0=0x10001800`, `lr=0x71f1c`, classificada como fora da veneer ARMCC ROPI. Portanto a guarda ROPI nao deve bloquea-la; o proximo experimento deve recuperar o protocolo de segunda entrada do modulo FIFA.

## Hook global identificado

Dump runtime: `[0x1fc]=0x80010000`, `[0x8001006c]=0xf0000004`, que e helper HLE free (indice 1). Assim `0xa0ff8` libera `r4`; em seguida `0xa1034` aloca 88 bytes. A segunda entrada/construtor recebe `r6=0x10001800` e encontra opcode no primeiro campo. Proximo trace deve capturar resultado da alocacao e r4/r5/r6 em `0x8300` para distinguir bloco nao zerado de deslocamento de construtor.

## Vtable nula confirmada

Na entrada `0x8300`, segundo caminho tem `r6=0x10001800`, `[r6]=0`, `r4=0x10025948`, `r5=0x14`. Em `0x837c` o guest faz `r0=[r6]=0`, `r1=[r0]=0xea00000e` (codigo base-zero), e `blx r1`. Assim o problema e vtable nula: a factory/interface que deveria preencher `[0x10001800]` nao o fez. Proximo trace deve identificar essa factory, nao mudar heap/construtor.
