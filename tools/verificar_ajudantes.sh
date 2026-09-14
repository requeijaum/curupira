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

# A REFERENCIA DE LINHA, conferida contra o cabecalho.
#
# PORQUE ESTA GUARDA EXISTE, medido: cada registo do `.inc` cita a linha do
# cabecalho onde o campo esta, e o campo existe para a medicao ser LOCALIZAVEL
# sem contar campos a mao. **17 dos 117 citavam a linha do COMENTARIO DE SECCAO
# acima da declaracao** (o `malloc` citava `// Memory allocation routines` no
# lugar da declaracao), porque o gerador tomava por posicao do campo o primeiro
# caracter nao branco do bloco -- e o bloco comeca no comentario.
#
# Um numero de linha errado nao da erro nenhum: quem o segue encontra um
# comentario e conclui que o campo esta noutro sitio. E preciso uma guarda, e
# ela e esta -- o diff do ficheiro gerado NAO a apanha, porque o `.inc` e
# regerado a partir do mesmo gerador e concorda consigo proprio.
python3 - "$SDK" "$RAIZ/tools/ajudantes_slots.inc" <<'PY'
import os, re, sys
sdk, inc_path = sys.argv[1], sys.argv[2]
cab = os.path.join(sdk, "platform", "system", "inc", "AEEStdLib.h")
linhas_do_cabecalho = open(cab, encoding="latin-1").read().splitlines()
inc = open(inc_path, encoding="utf-8").read()
registos = re.findall(r'\{0x([0-9A-F]{3}), "(\w+)", "(?:[^"\\]|\\.)*", (\d+)\},', inc)
maus = []
for off, nome, ln in registos:
    n = int(ln)
    if n < 1 or n > len(linhas_do_cabecalho) or not re.search(
        r"\(\s*\*\s*" + nome + r"\s*\)", linhas_do_cabecalho[n - 1]
    ):
        texto = linhas_do_cabecalho[n - 1].strip()[:60] if 0 < n <= len(linhas_do_cabecalho) else "FORA"
        maus.append("0x%s %s: linha %d -> %r" % (off, nome, n, texto))
if maus:
    print("FALHA: a linha citada NAO e a da declaracao, em %d de %d campos:"
          % (len(maus), len(registos)))
    for m in maus[:20]:
        print("   " + m)
    print("Corre: python3 tools/nomear_ajudantes.py \"$SDK\" tools/ajudantes_slots.inc")
    sys.exit(1)
print("OK: as %d referencias de linha levam a declaracao no cabecalho." % len(registos))
PY
