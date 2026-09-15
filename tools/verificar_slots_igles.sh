#!/bin/sh
# Regenera `tools/igles_slots.inc` (os 148 nomes do IGLES11) dos cabecalhos do
# SDK 4.0.2 da consola e FALHA se o ficheiro versionado divergir.
#
# UMA GUARDA QUE NAO CORRE NAO E UMA GUARDA: sem o SDK extraido este script sai
# com 77 (o codigo que o CTest le como "saltado") e NAO com 0.
set -e

RAIZ=$(cd "$(dirname "$0")/.." && pwd)

INC=${ZB2_SDK402_INC:-}
if [ -z "$INC" ]; then
  for c in \
    "/home/rafaelfrequiao/projects/zeebo-emulator/research/docs/sdk-extract/BREW-4.0.2-SP19/sdk/inc" \
    "$RAIZ/../../../docs/sdk-extract/BREW-4.0.2-SP19/sdk/inc"
  do
    if [ -f "$c/AEEGLES11.h" ]; then INC="$c"; break; fi
  done
fi
if [ -z "$INC" ]; then
  echo "SALTADO: AEEGLES10.h/AEEGLES11.h nao encontrados (define ZB2_SDK402_INC)."
  echo "         A guarda NAO correu -- e nao passou."
  exit 77
fi

TMP=$(mktemp)
trap 'rm -f "$TMP"' EXIT

python3 "$RAIZ/tools/nomear_igles.py" "$INC" "$TMP" >/dev/null

if ! diff -u "$RAIZ/tools/igles_slots.inc" "$TMP"; then
  echo ""
  echo "FALHA: tools/igles_slots.inc DIVERGE de AEEGLES10.h + AEEGLES11.h."
  echo "Corre: python3 tools/nomear_igles.py \"$INC\" tools/igles_slots.inc"
  exit 1
fi
echo "OK: igles_slots.inc corresponde a AEEGLES10.h + AEEGLES11.h (148 slots)."
