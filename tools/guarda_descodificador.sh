#!/usr/bin/env bash
# A GUARDA DO DESCODIFICADOR: o auditor diferencial contra o objdump, no ctest.
#
# PORQUE O ESPACO THUMB INTEIRO, e nao o corpus: o corpus leva ~3 minutos (13
# milhoes de palavras, 62 titulos) e um teste que demora isso deixa de correr. O
# espaco Thumb INTEIRO sao 65 536 meias-palavras -- gera-se em memoria, cabe numa
# corrida de segundos, e e COBERTURA COMPLETA (nao uma amostra). Foi esta corrida
# que encontrou o `strb`/`str` do Thumb a ser executado como LEITURA.
#
# Criterio: ZERO palavras em que a nossa descodificacao execute OUTRA instrucao em
# silencio. As recusas com nome (P2) NAO reprovam este teste.
#
# Codigo de saida: 0 sem divergencia silenciosa | 1 ha divergencia | 77 nao correu
# (falta o binutils ou a sonda) -- e sem o binutils nao ha oraculo, logo "passou"
# seria uma mentira. `SKIP_RETURN_CODE 77` no ctest traduz isso em SALTADO.
set -u

RAIZ="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${ZB2_DIR:-$RAIZ/build}"
SONDA="${ZB2_SONDA:-$BUILD/zb2_sonda_descodificador}"
OBJDUMP="${ZB2_OBJDUMP:-arm-none-eabi-objdump}"
ESPACO="${ZB2_ESPACO:-$(mktemp -t espaco-thumb.XXXXXX.bin)}"
TABELA="$(mktemp -t auditor-XXXXXX.txt)"

command -v "$OBJDUMP" >/dev/null 2>&1 || { echo "SKIP: sem $OBJDUMP"; exit 77; }
[ -x "$SONDA" ] || { echo "SKIP: sem a sonda em $SONDA"; exit 77; }

# As 65 536 meias-palavras, escritas pelo Python (e nao pelo shell).
python3 - "$ESPACO" <<'FIM'
import struct, sys
with open(sys.argv[1], "wb") as f:
    f.write(b"".join(struct.pack("<H", w) for w in range(65536)))
FIM

"$RAIZ/tools/auditar_descodificador.py" "$ESPACO" --sonda "$SONDA" --objdump "$OBJDUMP" \
    --thumb --exemplos 0 > "$TABELA" 2>&1
CABECALHO="$(grep -m1 '^# concorda' "$TABELA")"
echo "$CABECALHO"
SILENCIOSO="$(echo "$CABECALHO" | sed -n 's/.*SILENCIOSO): \([0-9]*\).*/\1/p')"
rm -f "$ESPACO" "$TABELA"
if [ "${SILENCIOSO:-x}" = "x" ]; then
  echo "o auditor nao devolveu o numero -- leia-o como falha, e nao como sucesso"
  exit 1
fi
if [ "$SILENCIOSO" -ne 0 ]; then
  echo "DIVERGENCIA SILENCIOSA: $SILENCIOSO meias-palavras do espaco Thumb"
  exit 1
fi
echo "OK: 0 meias-palavras do espaco Thumb executadas como outra instrucao"
exit 0
