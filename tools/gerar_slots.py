#!/usr/bin/env python3
"""Gera `brew_slots.inc` a partir dos cabecalhos do SDK.

PORQUE ISTO EXISTE: eu tinha os numeros de slot escritos a mao, transcritos de
memoria, e estavam TODOS errados por um -- porque `INHERIT_IBase` tem DOIS
metodos (`AddRef`, `Release`) e eu contava TRES, a procura de um `QueryInterface`
que nao existe em `INHERIT_IBase`.

O sintoma: o `pacmania` chamava `[vtable+8]` (indice 2) com
`(po, AEE_FONT_NORMAL, &asc, &desc)` -- exatamente a assinatura do
`GetFontMetrics` -- e eu recusava, porque tinha o 2 como "Release".

A cabecalho e a fonte primaria. Aqui ele e lido a serio: o `INHERIT_<Interface>`
comeca por `INHERIT_IBase(iname)`, que o proprio `AEEIBase.h` define com dois
membros, e os restantes vem pela ordem em que aparecem no macro.
"""
import re
import sys
from pathlib import Path

SDK = Path(sys.argv[1])
SAIDA = Path(sys.argv[2])

# interface -> (cabecalho, macro)
INTERFACES = [
    ("Shell",    "platform/system/inc/AEEIShell.h",       "#define INHERIT_IShell("),
    ("Display",  "platform/ui/inc/AEEIDisplay.h",         "#define INHERIT_IDisplay("),
    ("FileMgr",  "platform/deprecated/inc/AEEFile.h",     "#define INHERIT_IFileMgr("),
    ("HIDDevice","platform/hardware/inc/AEEIHIDDevice.h", "#define INHERIT_IHIDDevice("),
    ("Heap1",    "platform/system/inc/OEM/AEEIHeap1.h",   "#define INHERIT_IHeap1("),
]


def metodos(caminho: Path, macro: str):
    t = caminho.read_text(errors="replace")
    i = t.find(macro)
    if i < 0:
        raise SystemExit(f"macro nao encontrada: {macro} em {caminho}")
    # o corpo do macro vai ate a ultima linha continuada
    linhas, dentro = [], t[i:].split("\n")
    for linha in dentro[1:]:
        corpo = linha.split("//")[0].rstrip()
        linhas.append(corpo)
        if not linha.rstrip().endswith("\\"):
            break
    bloco = "".join(linhas)
    # cada membro e `tipo (*Nome)(...)` -- e o `INHERIT_IBase` nao aparece no
    # corpo porque e a PRIMEIRA linha, ja consumida
    nomes = re.findall(r"\(\*\s*(\w+)\s*\)", bloco)
    return nomes


linhas = [
    "// GERADO por tools/gerar_slots.py a partir dos cabecalhos do SDK.",
    "// NAO EDITAR A MAO: corre o gerador. Um numero escrito a mao ja divergiu",
    "// uma vez, e o custo foi uma ronda inteira a olhar para o sitio errado.",
    "//",
    "// `INHERIT_IBase` ocupa os slots 0 e 1 (AddRef, Release). NAO ha",
    "// QueryInterface em INHERIT_IBase -- foi essa a origem do erro de um.",
    "#pragma once",
    "",
    "namespace brew_slots {",
]
for nome, cab, macro in INTERFACES:
    fns = metodos(SDK / cab, macro)
    linhas.append(f"// {cab}")
    linhas.append(f"//   {len(fns)} metodos; slot = 2 + posicao")
    for k, fn in enumerate(fns):
        if fn in ("pfn",):
            continue
        linhas.append(f"constexpr unsigned k{nome}_{fn} = {k + 2};")
    linhas.append("")
linhas.append("}  // namespace brew_slots")
SAIDA.write_text("\n".join(linhas) + "\n")
print(f"{SAIDA}: {len(linhas)} linhas")
