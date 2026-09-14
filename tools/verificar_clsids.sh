#!/bin/sh
# Regenera `tools/clsids.inc` (o nome e o valor de CADA CLSID/IID do SDK) e falha
# se o ficheiro versionado DIVERGIR dos `*.bid`/`*.h`.
#
# PORQUE ESTA GUARDA EXISTE. O motor passou a NOMEAR as classes que o corpus
# pede, e o nome sai daqui. Um numero escrito a mao ja divergiu neste trabalho
# DUAS vezes (os offsets dos ajudantes `strtowstr`/`aee_GetRand`, e os slots do
# IDisplay): da primeira vez o efeito foi servir bytes aleatorios no sitio do
# `atoi`. Com esta guarda, uma divergencia entre o `.inc` e o cabecalho falha
# aqui, e nao numa bateria.
#
# UMA GUARDA QUE NAO CORRE NAO E UMA GUARDA. Sem o SDK, sai 77 -- o codigo que o
# CTest le como "saltado", e nao como "passou".
set -e

RAIZ=$(cd "$(dirname "$0")/.." && pwd)

SDK=${ZB2_SDK_DIR:-}
if [ -z "$SDK" ]; then
  for c in \
    "/home/rafaelfrequiao/projects/zeebo-emulator/research/docs/sdk-extract/BrewMPSDK-7.12.5/SDKPro/1.0.4.601 Pro" \
    "$RAIZ/../../../docs/sdk-extract/BrewMPSDK-7.12.5/SDKPro/1.0.4.601 Pro"
  do
    if [ -f "$c/platform/system/inc/AEEClassIDs.h" ]; then SDK="$c"; break; fi
  done
fi
if [ -z "$SDK" ]; then
  echo "SALTADO: SDK nao encontrado (define ZB2_SDK_DIR). A guarda NAO correu."
  exit 77
fi

TMP=$(mktemp)
trap 'rm -f "$TMP"' EXIT
python3 "$RAIZ/tools/gerar_clsids.py" "$SDK" "$TMP" >/dev/null

if ! diff -u "$RAIZ/tools/clsids.inc" "$TMP"; then
  echo ""
  echo "FALHA: tools/clsids.inc DIVERGE dos cabecalhos do SDK."
  echo "Corre: python3 tools/gerar_clsids.py \"$SDK\" tools/clsids.inc"
  exit 1
fi

# A CONFERENCIA PELO OUTRO LADO: os TRES CLSIDs que este trabalho foi buscar tem
# de estar no `.inc`, com o valor que o cabecalho da. Sem isto, a guarda so
# provava que o ficheiro e igual a si mesmo.
python3 - "$RAIZ" <<'PY'
import re, sys
raiz = sys.argv[1]
inc = open(raiz + "/tools/clsids.inc", encoding="utf-8").read()
constantes = dict(re.findall(r"^constexpr unsigned (k\w+) = 0x([0-9a-f]{8})u;", inc, re.M))
esperado = {
    # AEEAppHistory.bid:9   -> `#define AEECLSID_AppHistory 0x0100104f`
    "kClsid_AppHistory": "0100104f",
    # AEEClassIDs.h:209     -> `AEECLSID_TEXTCTL (AEECLSID_TEXTCTL_10 + 0x100)`,
    #                          com o `_10` em `(AEECLSID_CONTROL+9)` e o
    #                          `AEECLSID_CONTROL` em `(QVERSION + 0x3000)`.
    "kClsid_TEXTCTL": "01003109",
    # AEECLSID_VALUEMODEL_1.bid:31
    "kClsid_VALUEMODEL_1": "01028e3c",
}
maus = [k for k, v in esperado.items() if constantes.get(k) != v]
if maus:
    print("FALHA: o .inc diz outro valor para: %s" % ", ".join(sorted(maus)))
    sys.exit(1)
print("OK: clsids.inc corresponde aos cabecalhos (%d constantes, os 3 do corpus conferidos)."
      % len(constantes))
PY
