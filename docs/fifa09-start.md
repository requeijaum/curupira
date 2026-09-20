# FIFA09: saida do modulo no start

A corrida canonica chega ao codigo ARM em `0x8300`. O construtor aloca uma estrutura e em `0x837c` faz `r0=[r6]`, `r1=[r0]`, `blx r1`.

O trace Curupira mediu na saida: `lr=0x0000838c`, `r0=0x10001800`, `r1=0xea00000e`. `0xea00000e` e palavra de codigo ARM (branch), nao ponteiro de funcao; `blx` salta para `0xea00000e` e sai do modulo antes de HUD/render.

O bug de HUD reportado no Zeebx nao pode ser atribuido a texto Curupira enquanto esse objeto/ponteiro em `r6` estiver corrompido. Proximo experimento: registrar caller/valor de `r6` antes de `0x837c`.
