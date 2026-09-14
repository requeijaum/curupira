#!/usr/bin/env python3
"""Encontra `AEEGL.h` -- o cabecalho do IGL -- e imprime o caminho.

PORQUE ISTO E UM SCRIPT E NAO UM `find`:

`AEEGL.h` NAO ESTA na arvore extraida do SDK. Ele vive DENTRO do instalador
`OpenGLES_Extension_1.5.3_For_BREW_SDK_4.x.x_General_Installer/Installer.msi`
(medicao: `find . -iname AEEGL.h` sobre
`research/docs/sdk-extract/` nao devolve nada). O MSI guarda os ficheiros num
CAB com nomes transformados em hash (`_3AD4AA03...`), portanto o nome do ficheiro
NAO diz o que ele e: a unica forma de o identificar e o CONTEUDO.

Assim: extrai-se uma vez para uma cache em `/tmp` (nunca para o repositorio, e
nunca para a pasta de um titulo) e procura-se o ficheiro que declara a interface
`IGL`.

Uso:
    python3 tools/achar_aegl.py [caminho_do_instalador.msi]

Imprime o caminho, ou sai com 3 se nao conseguir (a guarda le isso como
"nao correu", e nao como "passou").
"""
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# O unico instalador da extensao OpenGL ES do Zeebo que existe neste repositorio.
MSI_PADRAO = (
    "/home/rafaelfrequiao/projects/zeebo-emulator/research/docs/sdk-extract/"
    "Zeebo SDK + BREW SDK 4.0.2 + BREW MP SDK/ZeeboSDKPackage-1.2.4/"
    "ZeeboSDKPackage-1.2.4/OpenGLES_Extension_1.5.3_For_BREW_SDK_4.x.x_"
    "General_Installer/Installer.msi"
)

# A assinatura do ficheiro que se procura. E o `AEEINTERFACE (IGL)` do
# `AEEGL.h` -- presente no cabecalho e em mais nada do instalador.
ASSINATURA = re.compile(r"AEEINTERFACE\s*\(\s*IGL\s*\)")


def da_arvore():
    """Um `AEEGL.h` ja extraido, se houver. Procurado em dois sitios: a variavel
    de ambiente (para um SDK instalado a serio) e o `inc/` da extensao."""
    for chave in ("ZB2_AEEGL_H", "ZB2_AEEGL"):
        v = os.environ.get(chave)
        if v and Path(v).is_file():
            return Path(v)
    return None


def da_cache(msi: Path):
    """A cache da extracao. `/tmp` e tmpfs e perde-se no reboot: e de proposito,
    um cabecalho do SDK nao pertence ao repositorio do emulador."""
    chave = hashlib.sha1(str(msi).encode()).hexdigest()[:12]
    return Path(tempfile.gettempdir()) / f"zb2-aegl-{chave}"


def extrair(msi: Path):
    if not msi.is_file():
        return None
    sete = shutil.which("7z") or shutil.which("7za")
    if sete is None:
        return None
    destino = da_cache(msi)
    marca = destino / ".completo"
    if not marca.is_file():
        if destino.exists():
            shutil.rmtree(destino)
        destino.mkdir(parents=True)
        # 1) o MSI da um fluxo que e um CAB.
        if subprocess.run([sete, "x", "-y", f"-o{destino}", str(msi)],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode != 0:
            return None
        # 2) o CAB da os ficheiros, com nomes em hash.
        for cab in list(destino.iterdir()):
            if cab.is_file() and cab.read_bytes()[:4] == b"MSCF":
                sub = destino / "cab"
                sub.mkdir(exist_ok=True)
                subprocess.run([sete, "x", "-y", f"-o{sub}", str(cab)],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        marca.write_text("ok\n")
    for f in sorted(destino.rglob("*")):
        if not f.is_file() or f.stat().st_size < 4096:
            continue
        try:
            t = f.read_text(errors="replace")
        except OSError:
            continue
        if ASSINATURA.search(t):
            return f
    return None


def main():
    if len(sys.argv) > 1:
        achado = da_arvore() or extrair(Path(sys.argv[1]))
    else:
        achado = da_arvore() or extrair(Path(MSI_PADRAO))
    if achado is None:
        print("AEEGL.h nao encontrado (nem na arvore, nem no instalador)", file=sys.stderr)
        return 3
    print(achado)
    return 0


if __name__ == "__main__":
    sys.exit(main())
