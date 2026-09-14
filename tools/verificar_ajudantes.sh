#!/bin/sh
# Regenera `tools/ajudantes_slots.inc` (a tabela NOMEADA dos 117 ajudantes do
# sistema) e falha se o ficheiro versionado DIVERGIR de `AEEStdLib.h`.
#
# PORQUE ESTA GUARDA EXISTE, medido: os offsets desta tabela estao ESCRITOS no
# despacho, e dois deles divergiram do cabecalho -- o `strtowstr` ficou no 0x0a0
# (que e `wstrcompress`) e o `aee_GetRand` no 0x090 (que e `atoi`). Nada acusou,
# porque nao havia nada que comparasse o usado com o MEDIDO. Com esta guarda, uma
# divergencia entre o `.inc` e o cabecalho falha aqui.
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
    if [ -f "$c/platform/system/inc/AEEStdLib.h" ]; then SDK="$c"; break; fi
  done
fi
if [ -z "$SDK" ]; then
  echo "SALTADO: SDK nao encontrado (define ZB2_SDK_DIR). A guarda NAO correu."
  exit 77
fi

TMP=$(mktemp)
trap 'rm -f "$TMP"' EXIT
# O gerador tem ANCORAS internas: se a extracao se enganar, ele sai com 2 e diz
# quais ancoras falharam. Este script nao corre sem esse passo passar.
python3 "$RAIZ/tools/nomear_ajudantes.py" "$SDK" "$TMP" >/dev/null

if ! diff -u "$RAIZ/tools/ajudantes_slots.inc" "$TMP"; then
  echo ""
  echo "FALHA: tools/ajudantes_slots.inc DIVERGE de AEEStdLib.h."
  echo "Corre: python3 tools/nomear_ajudantes.py \"$SDK\" tools/ajudantes_slots.inc"
  exit 1
fi

# A CONFERENCIA PELO OUTRO LADO: os offsets que o DESPACHO usa tem de ser os que
# o cabecalho da. Isto e o que faltava quando o `strtowstr` ficou no 0x0a0.
python3 - "$RAIZ" <<'PY'
import re, sys
raiz = sys.argv[1]
inc = open(raiz + "/tools/ajudantes_slots.inc", encoding="utf-8").read()
valores = dict(re.findall(r"kAjudante_(\w+) = 0x([0-9A-F]{3});", inc))
esperado = {
    "strtowstr": "040", "wstrcompress": "0A0", "aee_GetRand": "0A8", "atoi": "090",
    "sprintf": "020", "vsprintf": "13C", "strstr": "0D8", "stristr": "0E8",
    "GetRAMFree": "138", "wstrtostr": "044", "utf8towstr": "050",
}
maus = [k for k, v in esperado.items() if valores.get(k) != v]
if maus:
    print("FALHA: o .inc diz outro offset para: %s" % ", ".join(sorted(maus)))
    sys.exit(1)
print("OK: ajudantes_slots.inc corresponde a AEEStdLib.h (%d campos)." % len(valores))
PY
