# Cheats declarados (`ZB2_CHEATS`)

Memory patching on-the-fly, auditavel. Cada disparo vai para o traco como
`CHEAT_APLICADO` com o nome -- um patch silencioso e o stub mudo com outro
nome, e esta casa nao os escreve.

## Formato

```json
{"cheats": [
  {"nome": "bandeira", "titulo": "a3d", "fase": "start",
   "escreve": [{"endereco": "0x1000", "u32": 1}]},
  {"nome": "codigo", "pc": "0x58c",
   "escreve": [{"endereco": "0x2000", "u8": 255}]}
]}
```

- `nome` (obrigatorio) e `escreve` nao vazio (obrigatorio). Entrada invalida
  e IGNORADA sem derrubar as validas.
- `titulo` omitido = todos os titulos (`ReporPorTitulo` filtra por `mod`).
- `fase`: `carga`, `create`, `start`, ou `quadros` + `quadro` (dispara quando
  os quadros corridos alcancam o numero). Sem `fase` nem `pc` = ignorada.
- `pc`: gatilho de passo -- dispara quando o PC do guest passa pelo endereco
  (fronteira de instrucao, a mesma garantia dos callbacks). Custa um `if` por
  passo com a lista vazia.
- Escritas `u8`/`u16`/`u32` (nunca `u64`: nao existe escrita de 64 bits nesta
  arvore). Cada cheat dispara UMA vez por titulo.
- Limites honestos: so memoria (sem registradores), sem condicao sobre
  valores, sem escrita por quadro.

## Uso

`ZB2_CHEATS=/tmp/meus.json ./build/zb2_bateria corpus mods saida` -- a bateria
avisa em voz alta que a corrida NAO e comparavel com referencia sem cheats.

## Receita (provada em `a3d`, 17/09)

Para provar o mecanismo sem tocar no jogo, escreva FORA do mapa dele
(ex. `0xE0000000`) na fase `start` e confira `CHEAT_APLICADO` no traco com o
JSON byte-identico ao sem cheats (`pixels` 164156, `pixeis_do_ecra` 76800).
Para um hack real: `fase` + endereco do flag/byte de codigo (medido por
`ZB2_SONDA_WATCH`), `pc` no sitio do bug para pre-corrigir o buffer antes da
leitura. Receitas por titulo entram aqui com nome, endereco e medicao.
