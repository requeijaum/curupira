# Rocketweb: diagnostico de IShell::Resume

O callback de Rocketweb foi medido, nao aceito por suposicao.

- PCB: `0x10000128`.
- `pfnNotify` lido em `pcb+0x10`: `0x0001d52d` (Thumb).
- A funcao chama `0x18ab0`, `0x1ca40` e `0x1358c`; ela nao salta diretamente para fora do modulo.
- A tentativa de scheduler generico chegou a `0x10000048` e registrou callback nao retornado. O endereco fica no heap guest, nao na faixa de saidas HLE.

Vigia temporaria `Memoria::Vigiar(0x10000048..0x4c)` durante Rocketweb (`ZB2_VIGIA_RESUME=1`) mediu primeiro escritor `PC=0x000005c4`. O mesmo endereco e `pUser` de timers `pfn=0x00021085` (1000 ms) e `pfn=0x0001d4d5` (1500 ms). Portanto e estado real do applet inicializado cedo, nao dado aleatorio.

Proximo experimento: desmontar o escritor em `0x5c4` e seguir a vtable/ponteiro que produz o salto. Nao reintroduzir Resume generico antes de o callback poder atravessar as chamadas HLE que ele exige.
