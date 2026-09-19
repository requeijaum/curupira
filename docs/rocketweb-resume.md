# Rocketweb: diagnostico de IShell::Resume

O callback de Rocketweb foi medido, nao aceito por suposicao.

- PCB: `0x10000128`.
- `pfnNotify` lido em `pcb+0x10`: `0x0001d52d` (Thumb).
- A funcao chama `0x18ab0`, `0x1ca40` e `0x1358c`; ela nao salta diretamente para fora do modulo.
- A tentativa de scheduler generico chegou a `0x10000048` e registrou callback nao retornado. O endereco fica no heap guest, nao na faixa de saidas HLE.

Vigia temporaria `Memoria::Vigiar(0x10000048..0x4c)` durante Rocketweb (`ZB2_VIGIA_RESUME=1`) mediu primeiro escritor `PC=0x000005c4`. O mesmo endereco e `pUser` de timers `pfn=0x00021085` (1000 ms) e `pfn=0x0001d4d5` (1500 ms). Portanto e estado real do applet inicializado cedo, nao dado aleatorio.

Proximo experimento: desmontar o escritor em `0x5c4` e seguir a vtable/ponteiro que produz o salto. Nao reintroduzir Resume generico antes de o callback poder atravessar as chamadas HLE que ele exige.

## Escritor do estado heap

A vigia apontou PC `0x5c4`. Desmontagem ARM mostra chamada indireta `r1 = [[sl-4]+0x68]`, `r0=r5+12`, `bx r1`; o retorno vira estrutura em `r4`. O construtor grava três literais em `[r4+0,+4,+8]`. A escrita em `0x10000048` é portanto atribuída ao PC da chamada host/indireta, não a uma instrução Thumb aleatória. O proximo experimento deve vigiar esses campos e a leitura que usa `0x10000048` como alvo de `bx`.

## Sonda de leitura de 0x10000048

Com `ZB2_SONDA_PBMP=10000048:1000004c`, a leitura aparece como `0x10000048@0x0000077e`. A instrumentacao temporaria de `Ler32` tambem verificou que `leitor_host` estava vazio, portanto nao era uma saida HLE etiquetada pelo dispatcher. `0x77e` e `bx r3`, retorno Thumb; a leitura ocorre na transicao/retorno com PC ainda associado a esse ponto. O proximo instrumento deve capturar opcode/estado de CPU no acesso para separar fetch, retorno e leitura de dado.
