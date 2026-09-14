#!/bin/sh
# Regenera `brew_slots.inc` e falha se o ficheiro versionado DIVERGIR.
#
# E A GUARDA QUE FALTAVA. Os numeros de slot estiveram errados por um durante uma
# ronda inteira, e nada acusou: o `.inc` era gerado, mas nao havia nada que
# comparasse o gerado com o CABECALHO depois de alguem o mudar -- nem que
# comparasse o usado com o gerado.
#
# UMA GUARDA QUE NAO FALHA QUANDO DEVIAM NAO VALE NADA. Por isso este script
# termina com codigo diferente de zero, e nao com um aviso.
set -e

RAIZ=$(cd "$(dirname "$0")/.." && pwd)

# O SDK e opcional: sem ele a guarda nao pode correr. Nesse caso diz-se, e sai
# com 77 (o codigo que o CTest le como "saltado", e nao como "passou").
SDK=${ZB2_SDK_DIR:-}
if [ -z "$SDK" ]; then
  for c in \
    "/home/rafaelfrequiao/projects/zeebo-emulator/research/docs/sdk-extract/BrewMPSDK-7.12.5/SDKPro/1.0.4.601 Pro" \
    "$RAIZ/../../../docs/sdk-extract/BrewMPSDK-7.12.5/SDKPro/1.0.4.601 Pro"
  do
    if [ -d "$c/platform/system/inc" ]; then SDK="$c"; break; fi
  done
fi
if [ -z "$SDK" ]; then
  echo "SALTADO: SDK nao encontrado (define ZB2_SDK_DIR). A guarda NAO correu."
  exit 77
fi

TMP=$(mktemp)
trap 'rm -f "$TMP"' EXIT
python3 "$RAIZ/tools/gerar_slots.py" "$SDK" "$TMP" >/dev/null

if ! diff -u "$RAIZ/tools/brew_slots.inc" "$TMP"; then
  echo ""
  echo "FALHA: tools/brew_slots.inc DIVERGE dos cabecalhos do SDK."
  echo "Corre: python3 tools/gerar_slots.py \"$SDK\" tools/brew_slots.inc"
  exit 1
fi
echo "OK: brew_slots.inc corresponde aos cabecalhos."
