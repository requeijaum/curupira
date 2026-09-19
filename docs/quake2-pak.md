# Quake2brew: contrato PAK medido

`quake2brew.mod` em `276153` contém `FS_LoadPakFile` no offset ARM `0x51520`.
Desmontagem local em 2026-09-19:

1. abre `./../quake2res/pak0.pakz`;
2. lê 12 bytes de cabeçalho;
3. interpreta `dirofs` em `+4` e `dirlen` em `+8` como little-endian;
4. calcula `count = dirlen >> 6` e recusa `count > 4096`;
5. aloca `count * 72` bytes para registros internos;
6. faz `Seek(dirofs, SEEK_SET)` e lê `dirlen` bytes de diretório;
7. por registro de 64 bytes, converte `filepos` em `+56` e `filelen` em `+60`.

O PAKZ real tem `PACK`, tabela em 52.319.586, tamanho 213.120 e 3.330 registros. O título primeiro carrega o PAK e depois reabre o mesmo descritor para carregar configurações. `autoexec.cfg` não existe nas entradas reais.

Tentativas rejeitadas:

- expor PAKZ cru: configs chegam como LZMA e `Cmd_Exec_f` faz alocações inválidas;
- PAK lógico com tabela/dados descomprimidos: ainda produziu falhas de heap, portanto a fonte virtual não atendia exatamente algum `Seek/Read` posterior.

Próximo experimento obrigatório antes de nova VFS: fonte PAK virtual instrumentada somente com offset lógico, `Seek`, tamanho de `Read`, entrada resolvida e bytes retornados; comparar as duas reaberturas de `default.cfg`. Não fabricar `autoexec.cfg` nem aceitar `EnumInit` vazio.
