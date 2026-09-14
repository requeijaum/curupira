#!/usr/bin/env bash
# A GUARDA DO COMPARADOR, provada pelo BINARIO e pelo CODIGO DE SAIDA.
#
# O que o `ctest` tem de provar e o que o CI vai ver: o codigo de saida de
# `zb2_comparar`, e nao a API. Os testes do `gtest` (tests/comparar_test.cpp)
# provam a regra por dentro; este script prova o CONTRATO por fora, com ficheiros
# a serio e com o codigo de saida a ser lido por outra pessoa.
#
# As fichas de teste sao copias curtas de DUAS fichas reais da referencia
# (`/tmp/ref.json`, corrida de 62 titulos), para que os valores nao sejam
# inventados: `imicro3d` (12875) e o titulo mais simples do corpus, e `pacmania`
# (276212) e o unico cujo `faltas` tem tres chaves.
#
# Uso: comparar_guarda.sh [caminho_do_binario]
# Codigo de saida: 0 = todas as guardas verdes; 1 = alguma guarda falhou.
set -u

COMPARAR="${1:-${ZB2_BUILD:-.}/zb2_comparar}"
if [ ! -x "$COMPARAR" ]; then
  echo "VERMELHO: nao encontrei o binario do comparador em '$COMPARAR'" >&2
  echo "          constroi primeiro: cmake --build <build> --target zb2_comparar" >&2
  exit 1
fi

TMP="$(mktemp -d "${TMPDIR:-/tmp}/zb2-guarda-XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

# Uma linha por ficha, e a linha 2 e a linha 3 nomeadas no comentario: os `sed`
# abaixo dirigem-se por NUMERO DE LINHA, e nao por texto solto, porque um `sed`
# por texto pode apanhar a ficha errada (as duas tem os mesmos campos).
cat > "$TMP/ref.json" <<'FIM'
[
  {"mod":"imicro3d","pasta":"12875","tamanho":0,"carga":true,"modulo":true,"vtable":false,"applet":true,"passos_carga":60,"passos_create":340,"recusadas":0,"motivo":"retornou | create:retornou","pixels":0,"cores":1,"textos":0,"blits":0,"faltas":{}},
  {"mod":"pacmania","pasta":"276212","tamanho":0,"carga":true,"modulo":true,"vtable":false,"applet":true,"passos_carga":240,"passos_create":900,"recusadas":0,"motivo":"retornou | create:retornou","pixels":0,"cores":1,"textos":0,"blits":0,"faltas":{"IShell::slot41":1}}
]
FIM

falhas=0

# A SUBSTITUICAO TEM DE MUDAR ALGUMA COISA. Nesta sessao uma substituicao de
# texto ja falhou em SILENCIO e a "verificacao" passou por contar um simbolo que
# ja la estava (LEDGER, "uma verificacao que passa sem a mudanca"). Aqui o
# `cmp -s` recusa uma edicao que nao mudou nada -- a guarda do proprio teste.
aplicar() {  # aplicar <destino> <origem> <expressao sed>
  local destino="$1" origem="$2" expr="$3"
  sed -e "$expr" "$origem" > "$destino"
  if cmp -s "$destino" "$origem"; then
    echo "VERMELHO: a substituicao nao mudou nada: $expr"
    falhas=$((falhas + 1))
  fi
}

# O comparador acima de tudo: duas corridas iguais NAO falham. Sem isto, um
# comparador que falhasse sempre passaria nos tres casos abaixo.
exigir() {  # exigir <descricao> <codigo esperado> <a> <b> [agulha na saida]
  local desc="$1" esperado="$2" a="$3" b="$4" agulha="${5:-}"
  local saida codigo
  saida="$("$COMPARAR" "$a" "$b" 2>&1)"
  codigo=$?
  if [ "$codigo" -ne "$esperado" ]; then
    echo "VERMELHO: $desc -- codigo de saida $codigo, esperava $esperado"
    printf '%s\n' "$saida"
    falhas=$((falhas + 1))
    return
  fi
  if [ -n "$agulha" ] && ! printf '%s' "$saida" | grep -qF -- "$agulha"; then
    echo "VERMELHO: $desc -- a saida nao diz '$agulha'"
    printf '%s\n' "$saida"
    falhas=$((falhas + 1))
    return
  fi
  echo "verde: $desc (codigo $codigo)"
}

exigir "a mesma corrida duas vezes nao falha (codigo 0)" 0 "$TMP/ref.json" "$TMP/ref.json" "SEM REGRESSOES"

# (a) UM TITULO QUE PIORA TEM DE FALHAR, com o numero dito.
aplicar "$TMP/piora_applet.json" "$TMP/ref.json" '2s/"applet":true/"applet":false/'
exigir "applet true -> false FALHA (codigo 1)" 1 "$TMP/ref.json" "$TMP/piora_applet.json" "imicro3d (12875): applet true -> false"

aplicar "$TMP/piora_pixels.json" "$TMP/ref.json" '3s/"pixels":0/"pixels":4800/'
exigir "pixels 4800 -> 0 FALHA (codigo 1)" 1 "$TMP/piora_pixels.json" "$TMP/ref.json" "pixels 4800 -> 0"

# (c) UM TITULO QUE MELHORA NAO PODE FALHAR.
# "antes" perde o applet de um titulo; "depois" ganha cores no outro E devolve o
# applet ao primeiro. Sao DUAS melhorias, e o codigo tem de continuar 0.
aplicar "$TMP/antes_melhora.json" "$TMP/ref.json" '3s/"applet":true/"applet":false/'
aplicar "$TMP/depois_melhora.json" "$TMP/ref.json" '2s/"cores":1/"cores":7/'
exigir "cores 1 -> 7 e applet false -> true NAO falham (codigo 0)" 0 "$TMP/antes_melhora.json" "$TMP/depois_melhora.json" "MELHORIAS (2)"

# (b) UMA DIFERENCA DE CONFIGURACAO E RECUSADA, e nao comparada.
# As duas expressoes vao juntas de proposito: apagar a ultima ficha deixa a
# virgula da anterior pendurada, e o ficheiro deixa de ser JSON -- a recusa
# passaria a ser de FORMATO (4) e nao de CONFIGURACAO (3), que e outra coisa.
sed -e '2s/,$//' -e '3d' "$TMP/ref.json" > "$TMP/um_titulo.json"
exigir "corpus com menos um titulo e RECUSADO (codigo 3)" 3 "$TMP/ref.json" "$TMP/um_titulo.json" "Nenhum numero foi comparado"

aplicar "$TMP/outro_titulo.json" "$TMP/ref.json" '3s/"pacmania"/"outro_titulo"/'
exigir "corpus diferente com a mesma contagem e RECUSADO (codigo 3)" 3 "$TMP/ref.json" "$TMP/outro_titulo.json" "so na corrida: 276212/outro_titulo"

# (d) UM CAMPO NOVO DA BATERIA E UMA RECUSA, e nao um silencio.
aplicar "$TMP/campo_novo.json" "$TMP/ref.json" '2s/"tamanho":0/"tamanho":0,"campo_novo_da_bateria":7/'
exigir "campo nao declarado na tabela e RECUSADO (codigo 4)" 4 "$TMP/ref.json" "$TMP/campo_novo.json" "campo desconhecido 'campo_novo_da_bateria'"

# Ficheiro ilegivel: codigo 2, e nao 1 (o defeito seria do instrumento, nao do
# emulador -- e o codigo de saida tem de dizer de quem e o defeito).
exigir "ficheiro ausente e codigo 2 (uso)" 2 "$TMP/ref.json" "$TMP/nao_existe.json" "nao consegui ler"

echo
if [ "$falhas" -gt 0 ]; then
  echo "GUARDAS VERMELHAS: $falhas"
  exit 1
fi
echo "GUARDAS DO COMPARADOR: todas verdes"
exit 0
