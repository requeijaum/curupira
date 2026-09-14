#!/usr/bin/env bash
# REGRESSAO: corre a bateria AGORA e compara com a corrida de referencia guardada.
#
# E a etapa 9 do PLANO posta num comando: "mudar o emulador de forma a piorar um
# titulo FALHA a bateria, com o numero que piorou".
#
# ONDE FICA A REFERENCIA, e por que (a decisao pedida): em DIRETORIO DO REPOSITORIO,
# `tools/baseline/bateria.json`, versada no git.
#   1. `/tmp` e tmpfs -- perde-se no reboot, e o proprio LEDGER registra ter tido
#      de copiar para o repo a tabela No-Intro e os logs que estavam em `/tmp`.
#      Uma referencia que desaparece no reboot nao serve para nada.
#   2. Guardada no git, uma regressao passa a ser um `git diff` do ficheiro de
#      referencia: quem revir o commit VE o numero que mudou e em que titulo.
#   3. Sob a arvore (`src2/tools/baseline/`), para nao escrever em diretorios
#      partilhados com os outros agentes (`docs/`), que sao do merge do dono.
#
# A CORRIDA NOVA NUNCA SOBRESCREVE a referencia sozinha. Atualizar e um ato
# explicito (`--atualizar`), porque um instrumento que se auto-atualiza quando
# falha transforma toda a regressao num "sem regressoes".
#
# Uso:
#   tools/regressao.sh                 # corre a bateria e compara com a referencia
#   tools/regressao.sh --atualizar     # aceita a corrida de agora como referencia
# Caminhos (todos por variavel de ambiente, com valores por omissao):
#   ZB2_DIR (build/), ZB2_CORPUS, ZB2_MODS, ZB2_BASELINE,
#   ZB2_SCRATCH (/tmp, para a corrida nova e o log)
#
# `ZB2_BUILD` NAO e o directoria de build: e o COMMIT que a bateria escreve no
# cabecalho de proveniencia (`tools/bateria.cpp`, `getenv("ZB2_BUILD")`). Houve
# aqui uma colisao de nomes -- este script usava `ZB2_BUILD` para a pasta de
# build, e o `ctest` passava-lhe o caminho do build, que a bateria gravava como
# se fosse um commit. Um nome, um sentido: a pasta e `ZB2_DIR`, o commit e
# `ZB2_BUILD`.
# Codigo de saida: o do comparador (0 sem regressao | 1 regressao | 2 uso |
# 3 configuracoes diferentes | 4 formato). 77 = nao correu por falta do corpus ou
# da midia -- e `SKIP_RETURN_CODE 77` no ctest traduz isso em "saltado", e nao em
# "passou", porque um teste que passa sem ter corrido e pior do que um vermelho.
set -u

RAIZ="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${ZB2_DIR:-$RAIZ/build}"
# O COMMIT que a bateria vai gravar (campo NEUTRO do cabecalho, nunca criterio).
# Se o operador nao o der, este script pergunta-o ao git -- e o script PODE, porque
# e a ferramenta do operador e nao o emulador: quem nao pode ler o sistema por sua
# conta e o INSTRUMENTO (P4). Sem git, fica vazio e a bateria escreve
# `desconhecido`, que e uma resposta honesta.
if [ -z "${ZB2_BUILD:-}" ]; then
  ZB2_BUILD="$(git -C "$RAIZ" rev-parse --short HEAD 2>/dev/null || true)"
fi
export ZB2_BUILD

# O HASH DO BINARIO que vai medir. Ver o comentario em `tools/bateria.cpp`: o nome do
# commit mente quando a corrida e feita com a arvore suja -- foi medido.
if [ -x "$BATERIA" ]; then
  ZB2_BINARIO="$(sha256sum "$BATERIA" | cut -c1-64)"
  export ZB2_BINARIO
fi
BATERIA="${ZB2_BATERIA:-$BUILD/zb2_bateria}"
COMPARAR="${ZB2_COMPARAR:-$BUILD/zb2_comparar}"
# O CORPUS PROCURA-SE, e nao se assume um caminho relativo.
#
# O caminho era `$RAIZ/../../scripts/corpus62.json`, que valia quando este ficheiro
# vivia em `research/sources/zeebulator/src2/tools/`. Quando a arvore passou a
# `curupira/` na raiz do repositorio, o caminho deixou de existir -- e o script
# passou a SAIR 77, ou seja **SALTADO**, que o ctest traduz em "nao correu".
#
# **Um teste que salta por um caminho partido e pior do que um teste vermelho: nao
# deixa rasto no ctest.** (Foi assim que se descobriu: `ctest` a dizer 7/7 com um
# SALTADO pelo meio.)
#
# Agora procura-se por uma lista de sitios plausiveis, e o primeiro que exista ganha.
_procurar_corpus() {
  # UM CAMINHO DADO MAS INEXISTENTE NAO SE ACEITA EM SILENCIO.
  #
  # Havia isto: `if [ -n "$ZB2_CORPUS" ]; then echo "$ZB2_CORPUS"; return; fi`. Com
  # um caminho velho no ambiente (o CMake guardava um, relativo a `src2/`), o script
  # usava-o, nao encontrava nada, e saia 77 -- **"saltado", que o ctest mostra como
  # nao-falha.** O aviso fica no stderr, e a procura continua.
  if [ -n "${ZB2_CORPUS:-}" ]; then
    if [ -r "$ZB2_CORPUS" ]; then echo "$ZB2_CORPUS"; return; fi
    echo "AVISO: ZB2_CORPUS='$ZB2_CORPUS' nao existe. A PROCURAR em vez de saltar." >&2
  fi
  for c in \
    "$RAIZ/../research/sources/scripts/corpus62.json" \
    "$RAIZ/../../scripts/corpus62.json" \
    "$RAIZ/research/sources/scripts/corpus62.json" \
    "$RAIZ/scripts/corpus62.json"
  do
    if [ -r "$c" ]; then (cd "$(dirname "$c")" && printf '%s/%s\n' "$(pwd)" "$(basename "$c")"); return; fi
  done
  echo ""
}
CORPUS="$(_procurar_corpus)"
MODS="${ZB2_MODS:-/media/rafaelfrequiao/8C5F-19E51/zeebo/ROMs/debug_nand/mod}"
BASE="${ZB2_BASELINE:-$RAIZ/tools/baseline/bateria.json}"
RASCUNHO="${ZB2_SCRATCH:-/tmp}"
NOVA="$RASCUNHO/zb2-bateria-nova.json"
LOG="$RASCUNHO/zb2-bateria-nova.log"

ATUALIZAR=0
for arg in "$@"; do
  case "$arg" in
    --atualizar) ATUALIZAR=1 ;;
    -h|--help) sed -n '2,25p' "${BASH_SOURCE[0]}"; exit 0 ;;
    *) echo "argumento desconhecido: $arg" >&2; exit 2 ;;
  esac
done

for bin in "$BATERIA" "$COMPARAR"; do
  if [ ! -x "$bin" ]; then
    echo "nao encontrei '$bin'" >&2
    echo "constroi primeiro: cmake -S \"$RAIZ\" -B \"$BUILD\" && cmake --build \"$BUILD\" -j4" >&2
    exit 2
  fi
done

if [ ! -r "$CORPUS" ]; then
  echo "SALTADO: sem corpus em '$CORPUS' (ZB2_CORPUS para apontar outro)" >&2
  exit 77
fi
if [ ! -d "$MODS" ]; then
  # A midia do corpus vive num disco externo. Sem ele nao ha corrida possivel --
  # e dizer "passou" seria a mentira que este projeto paga caro.
  echo "SALTADO: sem a midia do corpus em '$MODS' (ZB2_MODS para apontar outro)" >&2
  exit 77
fi

echo "== bateria: $BATERIA"
"$BATERIA" "$CORPUS" "$MODS" "$NOVA" > "$LOG" 2>&1
CODIGO_BATERIA=$?
if [ "$CODIGO_BATERIA" -ne 0 ]; then
  echo "a bateria falhou (codigo $CODIGO_BATERIA); log em $LOG" >&2
  tail -20 "$LOG" >&2
  exit 2
fi
if [ ! -s "$NOVA" ]; then
  echo "a bateria nao escreveu a corrida em '$NOVA' (codigo 0 sem ficheiro)" >&2
  exit 2
fi
grep -F "titulos |" "$LOG" | tail -1
echo "   corrida nova: $NOVA   (log: $LOG)"
echo "   commit gravado no cabecalho: ${ZB2_BUILD:-(vazio: a bateria escreve 'desconhecido')}"

if [ "$ATUALIZAR" -eq 1 ]; then
  mkdir -p "$(dirname "$BASE")"
  cp "$NOVA" "$BASE"
  echo "== referencia atualizada: $BASE"
  echo "   (o git diff deste ficheiro e a lista do que mudou de estado)"
  exit 0
fi

if [ ! -r "$BASE" ]; then
  echo "SALTADO: nao ha corrida de referencia em '$BASE'" >&2
  echo "  para criar a referencia a partir da corrida de agora: $0 --atualizar" >&2
  exit 77
fi

echo
"$COMPARAR" "$BASE" "$NOVA"
CODIGO=$?

echo
case "$CODIGO" in
  0) echo "== regressao: SEM REGRESSOES" ;;
  1) echo "== regressao: FALHA -- um titulo piorou. O numero esta acima."
     echo "   (se a mudanca de estado e deliberada, aceita-a com: $0 --atualizar)" ;;
  3) echo "== regressao: RECUSADO -- a corrida de agora NAO correram a mesma especificacao."
     echo "   Corpus, midia (.mod de outro tamanho) ou formato do cabecalho diferentes? Entao a"
     echo "   referencia e que tem de ser refeita, depois de se confirmar que a mudanca e a esperada:"
     echo "   $0 --atualizar" ;;
  4) echo "== regressao: RECUSADO -- o JSON saiu do formato declarado." ;;
  *) echo "== regressao: codigo inesperado $CODIGO" ;;
esac
exit "$CODIGO"
