#!/bin/sh
# Regenera `gl_slots.inc` (a tabela de slots do IGL) e falha se o ficheiro
# versionado DIVERGIR do cabecalho `AEEGL.h` do SDK.
#
# DUAS FONTES INDEPENDENTES, e as duas tem de concordar:
#   1. `AEEGL.h`  -- o cabecalho, extraido do instalador da extensao OpenGL ES.
#   2. `conftest.elf` -- o `.mod` de exemplo do proprio SDK, compilado pela Zeebo.
#      Os 102 thunks `gl*`/`egl*` sao DESMONTADOS e o slot que cada um pede e
#      lido da instrucao `ldr rX,[rX,#off]`.
#
# UMA GUARDA QUE NAO CORRE NAO E UMA GUARDA. Sem o SDK ou sem o instalador, este
# script sai com 77 (o codigo que o CTest le como "saltado") em vez de 0.
set -e

RAIZ=$(cd "$(dirname "$0")/.." && pwd)

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

# O `AEEGL.h` vive DENTRO do instalador, e nao na arvore `inc/`. Ver
# `tools/achar_aegl.py` para a medicao.
if ! AEGL=$(python3 "$RAIZ/tools/achar_aegl.py" 2>/dev/null); then
  echo "SALTADO: AEEGL.h nao encontrado (instalador da extensao OpenGL ES ausente)."
  echo "         A guarda NAO correu -- e nao passou."
  exit 77
fi

ELF=${ZB2_CONFTEST_ELF:-}
if [ -z "$ELF" ]; then
  for c in \
    "/home/rafaelfrequiao/projects/zeebo-emulator/research/docs/sdk-extract/Zeebo SDK + BREW SDK 4.0.2 + BREW MP SDK/ZeeboSDKPackage-1.2.4/ZeeboSDKPackage-1.2.4/samples/samples/conftest_source/conftest/conftest.elf" \
    "$RAIZ/../../../docs/sdk-extract/Zeebo SDK + BREW SDK 4.0.2 + BREW MP SDK/ZeeboSDKPackage-1.2.4/ZeeboSDKPackage-1.2.4/samples/samples/conftest_source/conftest/conftest.elf"
  do
    if [ -f "$c" ]; then ELF="$c"; break; fi
  done
fi

TMP=$(mktemp)
TMPG=$(mktemp)
TMPB=$(mktemp)
trap 'rm -f "$TMP" "$TMPG" "$TMPB"' EXIT

# `brew_slots.inc` e gerado no mesmo passo, para uma mudanca no gerador que
# parta a tabela antiga nao passar despercebida aqui.
python3 "$RAIZ/tools/gerar_slots.py" "$SDK" "$TMPB" "$AEGL" "$TMPG" ${ELF:+"$ELF"} >/dev/null

if ! diff -u "$RAIZ/tools/brew_slots.inc" "$TMPB"; then
  echo ""
  echo "FALHA: tools/brew_slots.inc DIVERGE dos cabecalhos do SDK."
  exit 1
fi

if ! diff -u "$RAIZ/tools/gl_slots.inc" "$TMPG"; then
  echo ""
  echo "FALHA: tools/gl_slots.inc DIVERGE de AEEGL.h."
  echo "Corre: python3 tools/gerar_slots.py \"$SDK\" tools/brew_slots.inc \"$AEGL\" tools/gl_slots.inc"
  exit 1
fi

if [ -z "$ELF" ]; then
  echo "AVISO: conftest.elf ausente: a tabela confere com o cabecalho, mas nao"
  echo "       foi conferida contra os thunks do wrapper compilado pelo SDK."
fi
echo "OK: gl_slots.inc corresponde a AEEGL.h (e aos thunks do conftest.elf)."
