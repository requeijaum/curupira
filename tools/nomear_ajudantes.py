#!/usr/bin/env python3
"""Le `AEEStdLib.h` do SDK e escreve a tabela NOMEADA dos ajudantes do sistema.

O QUE ISTO RESOLVE, medido: a lista de demanda da bateria dizia
`AEEHelperFuncs[0x138]` -- um numero cru. Nomear obrigava a ir ao cabecalho
contar 78 slots em cada ronda, e a contagem a mao ja divergiu duas vezes
(`0x0a0` no sitio do `0x040`, `0x090` no sitio do `0x0a8`; ver
`docs/LEDGER-ZEEBULATOR.md`). O gerador tira os nomes da mao:

  * a ORDEM e a do `struct AEEHelperFuncs` do cabecalho, e o offset de cada
    campo e `4 * posicao` (o AEEHelperFuncs e um struct de ponteiros de funcao
    de 32 bits -- o Zeebo e ARM de 32 bits);
  * a ASSINATURA de cada campo e o TEXTO da declaracao, lido do cabecalho e
    nao transcrito de memoria;
  * ANCORAS CONFERIDAS: um punhado de nomes tem offset conhecido, medido e
    escrito aqui. Se a extracao se enganar (mudanca de formato do cabecalho,
    comentario com `;`), o gerador FALHA em vez de produzir uma tabela bonita e
    errada. **Um falso positivo num gerador e pior do que um numero escrito a
    mao: parece que foi medido.**

AS QUATRO QUE ENGANAM (todas medidas no cabecalho, e por isso ancoradas):

    0x020 sprintf            0x13c vsprintf
    0x0d8 strstr             0x0e8 stristr
    0x040 strtowstr          0x0a0 wstrcompress
    0x090 atoi               0x0a8 aee_GetRand

Uso:
    python3 tools/nomear_ajudantes.py <SDK> [saida.inc]
    python3 tools/nomear_ajudantes.py <SDK> --demanda <bateria.json>

`--demanda` NAO escreve nada: le o JSON da bateria e imprime a lista de demanda
com os nomes, dizendo quantos offsets continuam CRUS. E o numero que a tarefa
tem de baixar.
"""

import hashlib
import json
import os
import re
import sys

# As ancoras. Cada uma e um par (nome no cabecalho, offset em bytes).
ANCORAS = {
    "memmove": 0x000,
    "memset": 0x004,
    "strcpy": 0x008,
    "strcmp": 0x010,
    "strlen": 0x014,
    "strchr": 0x018,
    "strrchr": 0x01C,
    "sprintf": 0x020,       # e NAO vsprintf, que e 0x13c
    "strtowstr": 0x040,     # e NAO wstrcompress, que e 0x0a0
    "wstrtostr": 0x044,
    "utf8towstr": 0x050,
    "malloc": 0x068,
    "free": 0x06C,
    "wstrdup": 0x070,
    "OEMStrSize": 0x088,
    "GetAEEVersion": 0x08C,
    "atoi": 0x090,          # e NAO aee_GetRand, que e 0x0a8
    "dbgprintf": 0x09C,
    "wstrcompress": 0x0A0,
    "aee_GetRand": 0x0A8,
    "aee_GetUpTimeMS": 0x0B0,
    "GetAppInstance": 0x0C0,
    "strstr": 0x0D8,        # e NAO stristr, que e 0x0e8
    "stristr": 0x0E8,
    "GetRAMFree": 0x138,
    "vsprintf": 0x13C,
    "aee_IsBadPtr": 0x194,
    "GetALSContext": 0x1D0,
}

# O tamanho da tabela, medido: 117 campos. E o numero que o cabecalho do SDK
# declara, e o mesmo que a arvore antiga conferiu campo a campo.
QUANTOS = 117

CABECALHO_REL = os.path.join("platform", "system", "inc", "AEEStdLib.h")


def ler_cabecalho(sdk):
    caminho = os.path.join(sdk, CABECALHO_REL)
    if not os.path.isfile(caminho):
        raise SystemExit("ERRO: nao encontrei %s em %s" % (CABECALHO_REL, sdk))
    with open(caminho, "r", encoding="latin-1") as f:
        return caminho, f.read()


def extrair_membros(texto):
    """Devolve a lista de campos do `struct AEEHelperFuncs`, NA ORDEM.

    Cada membro e um dict com `texto` (a declaracao normalizada numa linha),
    `nome`, `linha` e `offset`.
    """
    m = re.search(r"struct\s+AEEHelperFuncs\s*\n?\s*\{", texto)
    if not m:
        raise SystemExit("ERRO: nao encontrei `struct AEEHelperFuncs` no cabecalho.")
    inicio = texto.index("{", m.start())
    prof = 0
    fim = -1
    for k in range(inicio, len(texto)):
        if texto[k] == "{":
            prof += 1
        elif texto[k] == "}":
            prof -= 1
            if prof == 0:
                fim = k
                break
    if fim < 0:
        raise SystemExit("ERRO: o `struct AEEHelperFuncs` nao fecha.")
    corpo = texto[inicio + 1 : fim]

    # O campo acaba no `;` de profundidade ZERO de parenteses. A profundidade e
    # o que impede um `void (*pfn)(void *)` DENTRO dos parametros de contar
    # como membro -- foi esse o primeiro defeito do gerador das vtables.
    membros = []
    prof = 0
    comeco = None
    for k, c in enumerate(corpo):
        if c == "(":
            prof += 1
        elif c == ")":
            prof -= 1
        if c == ";" and prof == 0:
            bruto = corpo[comeco if comeco is not None else 0 : k]
            limpo = re.sub(r"//[^\n]*", " ", bruto)
            limpo = re.sub(r"/\*.*?\*/", " ", limpo, flags=re.S)
            texto_m = " ".join(limpo.split())
            if texto_m:
                nome_m = re.search(r"\(\s*\*\s*(\w+)\s*\)", texto_m)
                if not nome_m:
                    raise SystemExit(
                        "ERRO: membro sem nome extraivel: %r" % texto_m[:120]
                    )
                pos = comeco if comeco is not None else 0
                # a linha do primeiro caracter NAO-BRANCO do membro
                desloc = pos
                while desloc < len(corpo) and corpo[desloc].isspace():
                    desloc += 1
                membros.append(
                    {
                        "texto": texto_m,
                        "nome": nome_m.group(1),
                        "linha": texto.count("\n", 0, inicio + 1 + desloc) + 1,
                        "offset": 4 * len(membros),
                    }
                )
            comeco = None
            continue
        if comeco is None and not c.isspace():
            comeco = k
    return membros


def conferir_ancoras(membros):
    por_nome = {}
    for m in membros:
        por_nome[m["nome"]] = m
    erros = []
    for nome, off in ANCORAS.items():
        m = por_nome.get(nome)
        if m is None:
            erros.append("ancora %s: NAO EXISTE no cabecalho" % nome)
        elif m["offset"] != off:
            erros.append(
                "ancora %s: esperado 0x%03x, medido 0x%03x" % (nome, off, m["offset"])
            )
    if len(membros) != QUANTOS:
        erros.append(
            "o cabecalho tem %d campos; a tabela tem de ter %d"
            % (len(membros), QUANTOS)
        )
    if erros:
        print("FALHA: a extracao do cabecalho nao bate com o que esta medido:")
        for e in erros:
            print("   " + e)
        raise SystemExit(2)


def escapar(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')


def gerar_inc(membros, sha):
    L = []
    L.append("// GERADO por tools/nomear_ajudantes.py. NAO EDITAR A MAO.")
    L.append("//")
    L.append("// FONTE: %s" % CABECALHO_REL.replace(os.sep, "/"))
    L.append("// sha256 do cabecalho: %s" % sha)
    L.append("//")
    L.append("// A ORDEM E A DO CABECALHO. O `AEEHelperFuncs` e um struct de %d" % QUANTOS)
    L.append("// ponteiros de funcao de 32 bits e o offset do campo `k` e `4 * k`.")
    L.append("//")
    L.append("// AS QUATRO QUE ENGANAM (medidas neste cabecalho):")
    L.append("//   0x020 sprintf      0x13c vsprintf")
    L.append("//   0x0d8 strstr       0x0e8 stristr")
    L.append("//   0x040 strtowstr    0x0a0 wstrcompress")
    L.append("//   0x090 atoi         0x0a8 aee_GetRand")
    L.append("//")
    L.append("// Um numero escrito a mao ja divergiu nesta tabela: o despacho ligou")
    L.append("// `strtowstr` ao 0x0a0 e `aee_GetRand` ao 0x090. Ver")
    L.append("// docs/LEDGER-ZEEBULATOR.md e tools/ajudantes_hook.patch.")
    L.append("#pragma once")
    L.append("")
    L.append("namespace brew_ajudantes {")
    L.append("")
    L.append("constexpr unsigned kPassoBytes = 4;")
    L.append("constexpr unsigned kQuantos = %d;" % QUANTOS)
    L.append("")
    for m in membros:
        L.append("constexpr unsigned kAjudante_%s = 0x%03X;" % (m["nome"], m["offset"]))
    L.append("")
    L.append("// A ASSINATURA de cada campo, LIDA do cabecalho: `assinatura` e o texto")
    L.append("// da declaracao, e `linha` diz onde ele esta para a medicao ser")
    L.append("// localizavel sem contar campos a mao.")
    L.append("struct Declaracao {")
    L.append("  unsigned offset;")
    L.append("  const char* nome;")
    L.append("  const char* assinatura;")
    L.append("  unsigned linha;")
    L.append("};")
    L.append("")
    L.append("inline constexpr Declaracao kDeclaracoes[kQuantos] = {")
    for m in membros:
        L.append(
            '    {0x%03X, "%s", "%s", %d},'
            % (m["offset"], m["nome"], escapar(m["texto"]), m["linha"])
        )
    L.append("};")
    L.append("")
    L.append("}  // namespace brew_ajudantes")
    L.append("")
    return "\n".join(L)


def modo_demanda(membros, caminho_json):
    """Imprime a lista de demanda do JSON da bateria, COM NOMES.

    O numero que interessa: quantos offsets do `AEEHelperFuncs` continuam CRUS
    (sem nome) na lista de demanda.
    """
    por_offset = {m["offset"]: m for m in membros}
    with open(caminho_json, "r", encoding="utf-8") as f:
        dados = json.load(f)
    cru = 0
    nomeado = 0
    fora = 0
    linhas = []
    for titulo in dados.get("titulos", []):
        for chave, quantos in (titulo.get("faltas") or {}).items():
            m = re.match(r"^AEEHelperFuncs\[0x([0-9a-fA-F]+)\]\s*(.*)$", chave)
            if not m:
                continue
            off = int(m.group(1), 16)
            nome = m.group(2).strip()
            if not nome:
                d = por_offset.get(off)
                if d is None:
                    fora += 1
                    nome = "(offset fora dos %d)" % QUANTOS
                else:
                    nome = "%s   [%s]" % (d["nome"], d["assinatura"])
                cru += 1
                linhas.append((off, nome, quantos, titulo.get("mod", "?")))
            else:
                nomeado += 1
    print("demanda de ajudantes em %s:" % caminho_json)
    for off, nome, quantos, mod in sorted(set(linhas)):
        print("  0x%03x  %-70s %4dx  (%s)" % (off, nome, quantos, mod))
    print("")
    print("  offsets CRUS (sem nome): %d" % cru)
    print("  ja com nome:             %d" % nomeado)
    if fora:
        print("  fora da tabela:          %d" % fora)
    return 0


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    sdk = argv[1]
    caminho, texto = ler_cabecalho(sdk)
    membros = extrair_membros(texto)
    conferir_ancoras(membros)
    sha = hashlib.sha256(open(caminho, "rb").read()).hexdigest()

    if len(argv) >= 4 and argv[2] == "--demanda":
        return modo_demanda(membros, argv[3])

    saida = argv[2] if len(argv) >= 3 else None
    conteudo = gerar_inc(membros, sha)
    if saida is None:
        sys.stdout.write(conteudo)
    else:
        with open(saida, "w", encoding="utf-8") as f:
            f.write(conteudo)
        print("%s: %d campos, sha256 do cabecalho %s" % (saida, len(membros), sha[:16]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
