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
#   ZB2_BUILD (build/), ZB2_CORPUS, ZB2_MODS, ZB2_BASELINE,
#   ZB2_SCRATCH (/tmp, para a corrida nova e o log)
# Codigo de saida: o do comparador (0 sem regressao | 1 regressao | 2 uso |
# 3 configuracoes diferentes | 4 formato). 77 = nao correu por falta do corpus ou
# da midia -- e `SKIP_RETURN_CODE 77` no ctest traduz isso em "saltado", e nao em
# "passou", porque um teste que passa sem ter corrido e pior do que um vermelho.
set -u

RAIZ="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${ZB2_BUILD:-$RAIZ/build}"
BATERIA="${ZB2_BATERIA:-$BUILD/zb2_bateria}"
COMPARAR="${ZB2_COMPARAR:-$BUILD/zb2_comparar}"
CORPUS="${ZB2_CORPUS:-$RAIZ/../../scripts/corpus62.json}"
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
  3) echo "== regressao: RECUSADO -- a corrida de agora nao e a mesma configuracao da referencia."
     echo "   (corpus diferente? nesse caso a referencia tem de ser refeita: $0 --atualizar)" ;;
  4) echo "== regressao: RECUSADO -- o JSON saiu do formato declarado." ;;
  *) echo "== regressao: codigo inesperado $CODIGO" ;;
esac
exit "$CODIGO"
