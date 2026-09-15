#!/usr/bin/env python3
"""Le `AEEGLES10.h` + `AEEGLES11.h` do SDK e escreve a tabela NOMEADA do IGLES11.

O QUE ISTO RESOLVE, MEDIDO (corrida `/tmp/corrida_base.json`, 62 titulos): a
lista de demanda tinha DEZASSETE entradas com a forma `IGLES11::slotN` -- um
numero cru. A maior delas, `IGLES11::slot67`, e pedida por DEZ titulos da
familia `emulator_neo` (cninja, spinmast, strhoop, supbtime, karnovr, wizdfire,
magdrop3, darkseal, baddudes, hbarrel), e nenhuma leitura da lista dizia QUE
metodo e -- e o `glGetString`.

A CONTA DOS SLOTS, lida da cadeia de heranca e nao transcrita:

    INHERIT_IGLES11 = INHERIT_IGLES10          (106 campos + cabeca)
                    + 39 campos proprios
    INHERIT_IGLES10 = INHERIT_IQueryInterface  (3: AddRef, Release, QueryInterface)
                    + 106 campos proprios
    total = 3 + 106 + 39 = 148

148 e EXACTAMENTE o `kIglesSlots` que `core/brew/classes.h` ja tinha escrito a
mao. As duas fontes concordam -- e agora a fonte e o cabecalho.

PORQUE ISTO NAO E `tools/gerar_slots.py`: o `AEEGL.h` (interface `IGL`, da
extensao OpenGL ES para BREW) e OUTRA interface, com OUTRA ordem de slots. O
`gl_slots.inc` e dela. O IGLES11 e a interface que o `QEGL::QueryInterface`
entrega, e vive nos cabecalhos do SDK 4.0.2 da consola. Confundir as duas foi o
erro do mapa do IGLES11 na arvore antiga (ver `core/brew/igl.h`).

Uso:
    python3 tools/nomear_igles.py <dir_inc_do_SDK_4.0.2> [saida.inc]
    python3 tools/nomear_igles.py <dir_inc_do_SDK_4.0.2> --demanda <bateria.json>

`--demanda` NAO escreve nada: le o JSON da bateria e imprime as faltas
`IGLES11::slotN` com o nome do metodo, ordenadas por TITULOS AFECTADOS (e nao
por numero de pedidos: 77 pedidos de um so titulo valem menos que 10 pedidos de
10 titulos).
"""

import hashlib
import json
import os
import re
import sys

# AS ANCORAS. Cada uma e um par (slot, nome), conferido a mao contra o cabecalho
# ANTES de este gerador existir. Se a extracao se enganar (uma mudanca de
# formato, um `(*pfn)` dentro dos parametros a contar como campo), o gerador
# FALHA em vez de escrever uma tabela bonita e errada -- um falso positivo num
# gerador e pior do que um numero escrito a mao, porque parece medido.
ANCORAS = {
    33: "BindTexture",
    64: "GenTextures",
    65: "GetError",
    67: "GetString",
    74: "LoadIdentity",
    79: "MatrixMode",
    104: "TexParameterx",
    108: "Viewport",
}

# 3 (INHERIT_IQueryInterface) + 106 (IGLES10) + 39 (IGLES11).
QUANTOS = 148
# Os tres da cabeca, na ordem do `INHERIT_IQI` (AEEQueryInterface.h).
CABECA = ["AddRef", "Release", "QueryInterface"]

CABECALHOS = [("AEEGLES10.h", "INHERIT_IGLES10"), ("AEEGLES11.h", "INHERIT_IGLES11")]

# A SEGUNDA TABELA: o `IGLES11Ext` (AEEGLES11Ext.h), que e OUTRA interface e
# OUTRO objecto -- nao e uma extensao da vtable do IGLES11.
#
# MEDIDO (sonda, cninja com `GL_OES_draw_texture` anunciado): o titulo pede
# `QEGL::QueryInterface` com OITO IIDs seguidos, e o `0x0103d8eb` desta interface
# e o que lhe da o `glDrawTexivOES`. Sem ele o `InitGLExtensions` do
# `framework/GLES_ext.c` nao tem a funcao que a extensao anunciada promete.
CABECALHOS_EXT = [("AEEGLES11Ext.h", "INHERIT_IGLES11Ext")]
ANCORAS_EXT = {
    3: "CurrentPaletteMatrixOES",
    8: "DrawTexiOES",
    11: "DrawTexivOES",
    14: "DrawTexfvOES",
}
QUANTOS_EXT = 15  # 3 (IQueryInterface) + 12 metodos


def ler(inc, nome):
    caminho = os.path.join(inc, nome)
    if not os.path.isfile(caminho):
        raise SystemExit("ERRO: nao encontrei %s em %s" % (nome, inc))
    with open(caminho, "r", encoding="latin-1") as f:
        return caminho, f.read()


def corpo_do_macro(texto, macro):
    """O texto do `#define <macro>(iname) ...`, com as continuacoes `\\`."""
    marca = "#define " + macro + "("
    if marca not in texto:
        raise SystemExit("ERRO: nao encontrei `%s` no cabecalho." % marca)
    i = texto.index(marca)
    linhas = []
    for linha in texto[i:].split("\n"):
        linhas.append(linha)
        if not linha.rstrip("\r\n").rstrip().endswith("\\"):
            break
    return "\n".join(linhas), texto.count("\n", 0, i) + 1


def campos(corpo, inicio_linha):
    """Os campos `int (*Nome) (...)` do macro, NA ORDEM, com a linha de cada um.

    A profundidade de parenteses NAO e precisa aqui porque cada campo e uma
    linha da continuacao -- mas o nome e lido do proprio `(*Nome)`, que so
    aparece uma vez por campo; um `(*pfn)` dentro dos parametros nao existe em
    nenhum destes dois cabecalhos, e a guarda das ancoras apanha-o se aparecer.
    """
    saida = []
    for n, linha in enumerate(corpo.split("\n")):
        limpo = re.sub(r"/\*.*?\*/", " ", linha)
        m = re.search(r"\(\s*\*\s*(\w+)\s*\)", limpo)
        if not m:
            continue
        texto = " ".join(limpo.replace("\\", " ").split())
        saida.append({"nome": m.group(1), "texto": texto, "linha": inicio_linha + n})
    return saida


def extrair(inc, cabecalhos=None):
    membros = [{"nome": n, "texto": "INHERIT_IQueryInterface", "linha": 0} for n in CABECA]
    shas = []
    for nome_h, macro in (cabecalhos if cabecalhos is not None else CABECALHOS):
        caminho, texto = ler(inc, nome_h)
        shas.append((nome_h, hashlib.sha256(open(caminho, "rb").read()).hexdigest()))
        corpo, linha = corpo_do_macro(texto, macro)
        for c in campos(corpo, linha):
            c["cabecalho"] = nome_h
            membros.append(c)
    return membros, shas


def conferir(membros, quantos=None, ancoras=None):
    QUANTOS = quantos if quantos is not None else globals()["QUANTOS"]
    ANCORAS = ancoras if ancoras is not None else globals()["ANCORAS"]
    erros = []
    if len(membros) != QUANTOS:
        erros.append("o cabecalho da %d slots; a conta da heranca da %d"
                     % (len(membros), QUANTOS))
    for slot, nome in sorted(ANCORAS.items()):
        if slot >= len(membros):
            erros.append("ancora %d (%s): fora da tabela" % (slot, nome))
        elif membros[slot]["nome"] != nome:
            erros.append("ancora %d: esperado %s, medido %s"
                         % (slot, nome, membros[slot]["nome"]))
    vistos = {}
    for i, m in enumerate(membros):
        if m["nome"] in vistos:
            erros.append("nome repetido: %s nos slots %d e %d" % (m["nome"], vistos[m["nome"]], i))
        vistos[m["nome"]] = i
    if erros:
        print("FALHA: a extracao dos cabecalhos nao bate com o que esta medido:")
        for e in erros:
            print("   " + e)
        raise SystemExit(2)


def escapar(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')


def bloco(L, ns, prefixo, membros, quantos):
    L.append("namespace %s {" % ns)
    L.append("")
    L.append("constexpr unsigned kQuantos = %d;" % quantos)
    L.append("")
    for i, m in enumerate(membros):
        L.append("constexpr unsigned %s%s = %d;" % (prefixo, m["nome"], i))
    L.append("")
    L.append("// O NOME de cada slot, pela ordem da vtable. Uma recusa sem nome nao se")
    L.append("// pode ler: `IGLES11::slot67` nao diz nada, `IGLES11::GetString` diz tudo.")
    L.append("inline constexpr const char* kNomes[kQuantos] = {")
    linha = "   "
    for m in membros:
        peca = ' "%s",' % m["nome"]
        if len(linha) + len(peca) > 96:
            L.append(linha)
            linha = "   "
        linha += peca
    if linha.strip():
        L.append(linha)
    L.append("};")
    L.append("")
    L.append("// A ASSINATURA lida do cabecalho, para o argumento de uma recusa se poder")
    L.append("// ler sem ir contar campos a mao.")
    L.append("struct Declaracao {")
    L.append("  unsigned slot;")
    L.append("  const char* nome;")
    L.append("  const char* assinatura;")
    L.append("  const char* cabecalho;")
    L.append("  unsigned linha;")
    L.append("};")
    L.append("")
    L.append("inline constexpr Declaracao kDeclaracoes[kQuantos] = {")
    for i, m in enumerate(membros):
        L.append('    {%d, "%s", "%s", "%s", %d},'
                 % (i, m["nome"], escapar(m["texto"]), m.get("cabecalho", "AEEQueryInterface.h"),
                    m["linha"]))
    L.append("};")
    L.append("")
    L.append("}  // namespace %s" % ns)
    L.append("")


def gerar_inc(membros, shas, membros_ext, shas_ext):
    L = []
    L.append("// GERADO por tools/nomear_igles.py. NAO EDITAR A MAO.")
    L.append("// Corre a guarda: tools/verificar_slots_igles.sh")
    L.append("//")
    L.append("// FONTE: BREW-4.0.2-SP19/sdk/inc/AEEGLES10.h, AEEGLES11.h e AEEGLES11Ext.h")
    for nome_h, sha in shas + shas_ext:
        L.append("//   sha256 %s  %s" % (sha, nome_h))
    L.append("//")
    L.append("// A CONTA: INHERIT_IQueryInterface(3) + IGLES10(106) + IGLES11(39) = 148,")
    L.append("// e 148 e o `kIglesSlots` que `core/brew/classes.h` ja tinha. Duas fontes,")
    L.append("// e elas concordam.")
    L.append("//")
    L.append("// O `IGLES11Ext` (AEEIID_GLES11EXT = 0x0103d8eb) e OUTRA interface e OUTRO")
    L.append("// objecto: 3 + 12 = 15 slots. E dela que vem o `glDrawTexivOES`.")
    L.append("//")
    L.append("// ANCORAS CONFERIDAS A MAO antes deste gerador existir (o gerador FALHA se")
    L.append("// alguma divergir):")
    for slot, nome in sorted(ANCORAS.items()):
        L.append("//   IGLES11    slot %-3d %s" % (slot, nome))
    for slot, nome in sorted(ANCORAS_EXT.items()):
        L.append("//   IGLES11Ext slot %-3d %s" % (slot, nome))
    L.append("#pragma once")
    L.append("")
    bloco(L, "igles_slots", "kIgles_", membros, QUANTOS)
    bloco(L, "igles_ext_slots", "kIglesExt_", membros_ext, QUANTOS_EXT)
    return "\n".join(L)


def modo_demanda(membros, caminho_json):
    with open(caminho_json, "r", encoding="utf-8") as f:
        dados = json.load(f)
    por_slot = {}
    for titulo in dados.get("titulos", []):
        for chave, quantos in (titulo.get("faltas") or {}).items():
            m = re.match(r"^IGLES11::slot(\d+)$", chave)
            if not m:
                continue
            slot = int(m.group(1))
            d = por_slot.setdefault(slot, {"pedidos": 0, "titulos": []})
            d["pedidos"] += quantos
            d["titulos"].append(titulo.get("mod", "?"))
    print("demanda do IGLES11 em %s (ordenada por TITULOS, nao por pedidos):" % caminho_json)
    for slot, d in sorted(por_slot.items(), key=lambda kv: (-len(kv[1]["titulos"]), kv[0])):
        nome = membros[slot]["nome"] if slot < len(membros) else "(fora dos %d)" % QUANTOS
        print("  slot %-3d %-24s %2d titulos %4d pedidos  %s"
              % (slot, nome, len(d["titulos"]), d["pedidos"], ",".join(sorted(d["titulos"])[:6])))
    print("")
    print("  slots CRUS (sem nome): %d" % len(por_slot))
    return 0


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    inc = argv[1]
    membros, shas = extrair(inc)
    conferir(membros)
    membros_ext, shas_ext = extrair(inc, CABECALHOS_EXT)
    conferir(membros_ext, QUANTOS_EXT, ANCORAS_EXT)
    if len(argv) >= 4 and argv[2] == "--demanda":
        return modo_demanda(membros, argv[3])
    saida = argv[2] if len(argv) >= 3 else None
    conteudo = gerar_inc(membros, shas, membros_ext, shas_ext)
    if saida is None:
        sys.stdout.write(conteudo)
    else:
        with open(saida, "w", encoding="utf-8") as f:
            f.write(conteudo)
        print("%s: %d slots" % (saida, len(membros)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
