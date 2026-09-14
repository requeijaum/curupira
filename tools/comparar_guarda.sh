#!/usr/bin/env bash
# A GUARDA DO COMPARADOR, provada pelo BINARIO e pelo CODIGO DE SAIDA.
#
# O que o `ctest` tem de provar e o que o CI vai ver: o codigo de saida de
# `zb2_comparar`, e nao a API. Os testes do `gtest` (tests/comparar_test.cpp)
# provam a regra por dentro; este script prova o CONTRATO por fora, com ficheiros
# a serio e com o codigo de saida a ser lido por outra pessoa.
#
# A corrida de teste usa a FORMA REAL, com o cabecalho de proveniencia que o
# `bateria.cpp` escreve desde cfb031e:
#
#   {"config": {"corpus_sha256": <64 hex>, "titulos": N, "build": <git>},
#    "titulos": [ ...fichas... ]}
#
# As duas fichas sao copias CURTAS de duas fichas reais do baseline
# (`tools/baseline/bateria.json`): `imicro3d` (12875, 90068 bytes, o titulo mais
# simples do corpus) e `pacmania` (276212, 148400 bytes, o unico cujo `faltas` tem
# tres chaves). Os numeros nao sao inventados -- vem do baseline.
#
# Uso: comparar_guarda.sh [caminho_do_binario]
# Codigo de saida: 0 = todas as guardas verdes; 1 = alguma guarda falhou.
set -u

# `ZB2_DIR` e a pasta de build (`ZB2_BUILD` e o commit da bateria: um nome, um
# sentido -- ver o cabecalho de tools/regressao.sh).
COMPARAR="${1:-${ZB2_DIR:-.}/zb2_comparar}"
if [ ! -x "$COMPARAR" ]; then
  echo "VERMELHO: nao encontrei o binario do comparador em '$COMPARAR'" >&2
  echo "          constroi primeiro: cmake --build <build> --target zb2_comparar" >&2
  exit 1
fi

TMP="$(mktemp -d "${TMPDIR:-/tmp}/zb2-guarda-XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

# Dois resumos DIFERENTES. O segundo muda UM caractere do primeiro, que e o que o
# teste do corpus diferente tem de apanhar.
SHA_A="348106f1aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaad8b"
SHA_B="348106f1bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbd8b"

# A LINHA 2 e o cabecalho, a linha 4 e o `imicro3d` e a linha 5 o `pacmania`: os
# `sed` abaixo dirigem-se por NUMERO DE LINHA, e nao por texto solto, porque um
# `sed` por texto pode apanhar a ficha errada (as duas tem os mesmos campos).
cat > "$TMP/ref.json" <<FIM
{
  "config": {"corpus_sha256": "$SHA_A", "titulos": 2, "build": "abc1234"},
  "titulos": [
  {"mod":"imicro3d","pasta":"12875","tamanho":90068,"carga":true,"modulo":true,"vtable":true,"applet":true,"passos_carga":60,"passos_create":340,"recusadas":0,"motivo":"retornou | create:retornou","pixels":0,"cores":1,"textos":0,"blits":0,"faltas":{}},
  {"mod":"pacmania","pasta":"276212","tamanho":148400,"carga":true,"modulo":true,"vtable":true,"applet":true,"passos_carga":3555,"passos_create":4414,"recusadas":0,"motivo":"retornou | create:retornou","pixels":0,"cores":1,"textos":0,"blits":0,"faltas":{"AEEHelperFuncs[0x040]":1,"AEEHelperFuncs[0x0a8]":1,"IShell::slot41":1}}
  ]
}
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
# comparador que falhasse sempre passaria nos casos abaixo.
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
aplicar "$TMP/piora_applet.json" "$TMP/ref.json" '4s/"applet":true/"applet":false/'
exigir "applet true -> false FALHA (codigo 1)" 1 "$TMP/ref.json" "$TMP/piora_applet.json" "imicro3d (12875): applet true -> false"

aplicar "$TMP/ganha_pixels.json" "$TMP/ref.json" '5s/"pixels":0/"pixels":4800/'
exigir "pixels 4800 -> 0 FALHA (codigo 1)" 1 "$TMP/ganha_pixels.json" "$TMP/ref.json" "pixels 4800 -> 0"

# (c) UM TITULO QUE MELHORA NAO PODE FALHAR.
# "antes" perde o applet de um titulo; "depois" ganha cores no outro E devolve o
# applet ao primeiro. Sao DUAS melhorias, e o codigo tem de continuar 0.
aplicar "$TMP/antes_melhora.json" "$TMP/ref.json" '5s/"applet":true/"applet":false/'
aplicar "$TMP/depois_melhora.json" "$TMP/ref.json" '4s/"cores":1/"cores":7/'
exigir "cores 1 -> 7 e applet false -> true NAO falham (codigo 0)" 0 "$TMP/antes_melhora.json" "$TMP/depois_melhora.json" "MELHORIAS (2)"

# (b) UMA DIFERENCA DE CONFIGURACAO E RECUSADA, e nao comparada.
# As TRES expressoes vao juntas de proposito, e cada uma tira uma causa de recusa
# que NAO e a que se quer medir:
#   - `4s/,$//` + `5d`: apagar a ultima ficha deixa a virgula da anterior
#     pendurada, e o ficheiro deixa de ser JSON;
#   - `2s/"titulos": 2/"titulos": 1/`: sem isto o cabecalho passa a contradizer a
#     lista, e a recusa seria de FORMATO (4) -- a mentira do instrumento -- em vez
#     de CONFIGURACAO (3), que e o que este caso mede.
sed -e '2s/"titulos": 2/"titulos": 1/' -e '4s/,$//' -e '5d' "$TMP/ref.json" > "$TMP/um_titulo.json"
exigir "corpus com menos um titulo e RECUSADO (codigo 3)" 3 "$TMP/ref.json" "$TMP/um_titulo.json" "Nenhum numero foi comparado"

aplicar "$TMP/outro_titulo.json" "$TMP/ref.json" '5s/"pacmania"/"outro_titulo"/'
exigir "corpus diferente com a mesma contagem e RECUSADO (codigo 3)" 3 "$TMP/ref.json" "$TMP/outro_titulo.json" "so na corrida: 276212/outro_titulo"

# (b) COMPLETA: os MESMOS titulos, o corpus DIFERENTE. Era o buraco que sobrava.
aplicar "$TMP/outro_corpus.json" "$TMP/ref.json" "2s/$SHA_A/$SHA_B/"
exigir "corpus diferente com os MESMOS titulos e RECUSADO (codigo 3)" 3 "$TMP/ref.json" "$TMP/outro_corpus.json" "cabecalho.corpus_sha256"

# Um `.mod` de outro tamanho e OUTRO ficheiro: identidade, e nao progresso.
aplicar "$TMP/outro_mod.json" "$TMP/ref.json" '4s/"tamanho":90068/"tamanho":91000/'
exigir "modulo de outro tamanho e RECUSADO (codigo 3)" 3 "$TMP/ref.json" "$TMP/outro_mod.json" "tamanho 90068 -> 91000"

# E, AO CONTRARIO, um `build` diferente NAO recusa: comparar duas versoes do
# emulador e o uso normal da ferramenta.
aplicar "$TMP/outro_build.json" "$TMP/ref.json" '2s/"build": "abc1234"/"build": "def5678"/'
exigir "build diferente NAO falha (codigo 0) e e reportado" 0 "$TMP/ref.json" "$TMP/outro_build.json" "cabecalho.build"

# Cabecalho e corpo a discordar: mentira do instrumento, recusa 4.
aplicar "$TMP/cabecalho_mente.json" "$TMP/ref.json" '2s/"titulos": 2/"titulos": 61/'
exigir "cabecalho que contradiz a lista e RECUSADO (codigo 4)" 4 "$TMP/ref.json" "$TMP/cabecalho_mente.json" "mentira do instrumento"

# (d) UM CAMPO NOVO DA BATERIA E UMA RECUSA, e nao um silencio.
aplicar "$TMP/campo_novo.json" "$TMP/ref.json" '4s/"tamanho":90068/"tamanho":90068,"campo_novo_da_bateria":7/'
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
