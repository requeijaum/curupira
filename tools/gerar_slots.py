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
    ("SQLMgr",   "platform/deprecated/inc/AEESQL.h",      "#define INHERIT_ISQLMgr("),
    ("IAStream", "platform/system/inc/AEEIAStream.h",     "#define INHERIT_IAStream("),
    ("IFile",    "platform/deprecated/inc/AEEFile.h",     "#define INHERIT_IFile("),
    # As quatro abaixo foram acrescentadas a pedido do sub-agente `hid-entrada`: ele
    # tinha os numeros conferidos a mao e com static_assert, e isso tira-os da mao.
    ("IHID",     "platform/hardware/inc/AEEIHID.h",       "#define INHERIT_IHID("),
    ("ISignal",  "platform/system/inc/AEEISignal.h",        "#define INHERIT_ISignal("),
    ("ISignalCtl", "platform/system/inc/AEEISignalCtl.h",   "#define INHERIT_ISignalCtl("),
    ("ISignalCBFactory", "platform/system/inc/AEEISignalCBFactory.h",
     "#define INHERIT_ISignalCBFactory("),
]


# A CABECA DA INTERFACE, e ela decide o deslocamento.
#
# `INHERIT_IBase` tem DOIS membros (AddRef, Release) e `INHERIT_IQI` tem TRES
# (AddRef, Release, QueryInterface) -- lidos de `AEEIBase.h` e `AEEIQI.h`.
#
# **Foi nao saber isto que me fez errar todos os slots por um**: eu supunha tres
# membros sempre, a procura de um `QueryInterface` que so existe em `INHERIT_IQI`.
CABECAS = [
    ("INHERIT_IQueryInterface", 3, "AEEIQI.h: AddRef, Release, QueryInterface"),
    ("INHERIT_IQI", 3, "AEEIQI.h: AddRef, Release, QueryInterface"),
    ("INHERIT_IBase", 2, "AEEIBase.h: AddRef, Release -- e SO estes dois"),
]


def _membros(bloco: str):
    """Os nomes dos MEMBROS da interface, na ordem.

    UM MEMBRO ACABA EM `);` -- OU NO FIM DO MACRO, porque o ULTIMO NAO TEM
    ponto e virgula. E isso distingue-o de um ponteiro de funcao dentro de uma
    LISTA DE PARAMETROS, que acaba em `,` ou em `)`.

    Duas armadilhas, e caí nas duas: o `void (*pfn)(void *)` dentro dos parametros
    do `SetTimer` era contado como membro (empurrando o `CancelTimer` para o 13), e
    o `Cancel` do `INHERIT_IAStream` NAO era contado (por nao ter `;`), o que dava
    ao `IFile` uma cabeca de 4 slots em vez de 5.

    Sem esta exigencia, o `void (*pfn)(void *)` que esta dentro dos parametros do
    `SetTimer` era contado como um membro:

        int (*SetTimer)(iname *po, int32 dwMsecs, void (*pfn)(void *), void *pUser);
        int (*CancelTimer)(iname *po, void (*pfn)(void *), void *pUser);

    -- e o `CancelTimer` ia parar ao slot 13 em vez do 12, empurrando tudo o que
    vem depois. **Um falso positivo no gerador e pior do que um numero escrito a
    mao: parece que foi medido.**
    """
    return re.findall(r"\(\*\s*(\w+)\s*\)\s*\([^;]*?\)\s*(?:;|$)", bloco)


def _raiz(nome_macro: str, profundidade: int = 0) -> tuple:
    """Quantos slots a cabeca desta interface ocupa, seguindo a CADEIA.

    `INHERIT_IFile` nao comeca em `IBase`: comeca em `INHERIT_IAStream`, que por
    sua vez comeca em `INHERIT_IBase` mais TRES membros. Contar so a linha
    imediata daria 5 slots a menos em todo o `IFile`, e o `Read` ficaria no sitio
    do `Write`.

    A cadeia resolve-se lendo os cabecalhos, que e a fonte primaria -- nunca por
    uma tabela escrita a mao, que e exatamente o erro que esta a ser corrigido.
    """
    if profundidade > 8:
        raise SystemExit(f"cadeia de heranca demasiado funda em {nome_macro}")
    for raiz, quantos, _ in CABECAS:
        if nome_macro == raiz:
            return quantos, raiz
    # procurar a definicao em todos os cabecalhos
    for cab in SDK.rglob("*.h"):
        try:
            t = cab.read_text(errors="replace")
        except OSError:
            continue
        i = t.find(f"#define {nome_macro}(")
        if i < 0 or "\n" not in t[i:]:
            continue
        # AS MESMAS REGRAS DO `metodos`: linhas[0] e a heranca, o resto sao
        # membros. Ter duas logicas de leitura foi o que fez o `INHERIT_IAStream`
        # contar 4 slots em vez de 5.
        linhas = _linhas_do_macro(t, i)
        if not linhas:
            continue
        pai = re.search(r"(INHERIT_\w+)\s*\(", linhas[0])
        if not pai:
            continue  # raiz sem heranca
        quantos_pai, _ = _raiz(pai.group(1), profundidade + 1)
        proprios = len(_membros("".join(linhas[1:])))
        return quantos_pai + proprios, f"{pai.group(1)}({quantos_pai}) + {proprios}"
    raise SystemExit(f"nao consegui resolver a cabeca de {nome_macro}")


def _cabeca_do_bloco(bloco: str):
    for nome, quantos, _ in CABECAS:
        if nome in bloco:
            return quantos
    return 2


def cabeca(caminho: Path, macro: str):
    t = caminho.read_text(errors="replace")
    i = t.find(macro)
    if i < 0:
        raise SystemExit(f"macro nao encontrada: {macro} em {caminho}")
    j = t.find("\\\n", i)
    if j < 0:
        j = t.find("AEEINTERFACE_DEFINE", i)
    corpo = t[i:j]
    for nome, quantos, nota in CABECAS:
        if nome + "(" in corpo.split("\\\n")[0] or nome in corpo.split("\\\n")[1] if len(corpo.split("\\\n")) > 1 else False:
            return quantos, nota
    return 2, "sem cabeca reconhecida -- assumido INHERIT_IBase (2)"


def _linhas_do_macro(t: str, i: int):
    """As linhas continuadas do macro que comeca em `i`, SEM o `#define`."""
    dentro = t[i:].split("\n")
    out = []
    for linha in dentro[1:]:
        out.append(linha.split("//")[0].rstrip())
        if not linha.rstrip().endswith("\\"):
            break
    return out


def metodos(caminho: Path, macro: str):
    """(membros, slots_de_cabeca, nota) de uma interface.

    `membros` sao SO os metodos proprios: a linha da heranca nao entra.
    `slots_de_cabeca` e o que a cadeia de heranca ocupa antes deles.
    """
    t = caminho.read_text(errors="replace")
    i = t.find(macro)
    if i < 0:
        nome_if = macro.replace("#define INHERIT_", "").replace("(", "")
        alt = f"AEEINTERFACE({nome_if})"
        i = t.find(alt)
        if i < 0:
            raise SystemExit(f"macro nao encontrada: {macro} (nem {alt}) em {caminho}")
        j = t.find("};", i)
        bloco = t[i:j] if j > 0 else t[i:i + 8000]
        pai = re.search(r"(INHERIT_\w+|INHERIT_IQueryInterface)\s*\(", bloco)
        quantos, origem = _raiz(pai.group(1)) if pai else (0, "interface raiz")
        return _membros(bloco), quantos, f"AEEINTERFACE({nome_if}): {pai.group(1) if pai else '-'} = {quantos}"

    linhas = _linhas_do_macro(t, i)
    # linhas[0] e a HERANCA (quando existe); o resto sao membros proprios.
    pai = re.search(r"(INHERIT_\w+)\s*\(", linhas[0]) if linhas else None
    if pai:
        quantos, origem = _raiz(pai.group(1))
        corpo = "".join(linhas[1:])
        nota = f"{pai.group(1)} = {quantos} slots de cabeca"
    else:
        quantos, nota = 0, "interface raiz"
        corpo = "".join(linhas)
    return _membros(corpo), quantos, nota


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
    fns, base_off, nota = metodos(SDK / cab, macro)
    linhas.append(f"// {cab}")
    linhas.append(f"//   cabeca: {nota}")
    linhas.append(f"//   {len(fns)} metodos; slot = {base_off} + posicao")
    for k, fn in enumerate(fns):
        if fn in ("pfn",):
            continue
        linhas.append(f"constexpr unsigned k{nome}_{fn} = {k + base_off};")
    linhas.append("")
linhas.append("}  // namespace brew_slots")
SAIDA.write_text("\n".join(linhas) + "\n")
print(f"{SAIDA}: {len(linhas)} linhas")
