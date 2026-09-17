#!/usr/bin/env python3
"""Gera `tools/clsids.inc` a partir dos `*.bid` e `*.h` do SDK.

PORQUE ISTO EXISTE, medido. O motor sabe responder por NOME a uma pergunta "que
metodo e este?" (o `despacho` nomeia cada slot), mas nao sabia responder a
pergunta irma: "que CLASSE e este numero?". A lista de demanda da bateria dizia

    IShell::CreateInstance CLSID desconhecido  pedido 3x
        iid=0x0100104f ppo=0x8007ff74   1x
        iid=0x01003109 ppo=0x80200380   1x
        iid=0x01028e3c ppo=0x8020339c   1x

-- tres numeros, e nenhum nome. Uma lista que diz o numero obriga a ir ao
cabecalho contar em cada ronda; uma que diz o nome e uma medida.

E O PROJECTO JA PAGOU POR ESCREVER ESTES NUMEROS A MAO. Duas copias de um numero
medido divergiram (os offsets dos ajudantes `strtowstr`/`aee_GetRand`, e os
slots do IDisplay), e uma delas servia bytes aleatorios. Aqui o numero vem do
cabecalho, e ha uma guarda (`tools/verificar_clsids.sh`) que regenera e compara.

O QUE ESTE GERADOR LE, e a razao de ler os DOIS tipos de ficheiro:

  - `*.bid` -- o ficheiro em que o SDK DECLARA a classe (ex.:
    `AEEAppHistory.bid:9  #define AEECLSID_AppHistory 0x0100104f`);
  - `*.h`   -- a lista mestra (`AEEClassIDs.h`) e os cabecalhos de interface, onde
    muitos CLSID sao EXPRESSOES, e nao literais:
    `AEECLSID_TEXTCTL (AEECLSID_TEXTCTL_10 + 0x100)` (AEEClassIDs.h:209),
    `AEECLSID_TEXTCTL_10 (AEECLSID_CONTROL+9)`, `AEECLSID_CONTROL (QVERSION + 0x3000)`.

Um gerador que so lesse literais perdia tudo o que e derivado -- e o
`0x01003109` e exactamente um desses.

A EXPRESSAO E AVALIADA A SERIO, nome a nome, pela mesma razao que o gerador dos
slots resolve a cadeia de heranca: um numero escrito a mao ja divergiu uma vez.
Nomes que nao se resolvem (8 de 2485, todos APELIDOS de nomes que vivem fora de
`*.bid`/`*.h`) NAO entram em silencio: sao listados no stderr e contados no fim.
"""
import re
import sys
from collections import defaultdict
from pathlib import Path

# Os prefixos de CLASSE/IID do BREW. `AEECLSID_` e a classe, `AEEIID_` o IID da
# interface, `AEEPRIVID_` o IID privado (o OEM declara-os assim).
PREFIXOS = ("AEECLSID_", "AEEIID_", "AEEPRIVID_")
# O inicio do espaco de CLSID, lido de `AEEClassIDs.h:20`
# (`#define QVERSION 0x01000000  // Most significant 8-bits are version`).
QVERSION = 0x01000000
# O prefixo -> prefixo da constante gerada. UM POR FAMILIA: 64 nomes curtos
# colidem entre familias (`AEECLSID_BITMAP` e `AEEIID_BITMAP`), e uma colisao de
# constantes e um erro de compilacao -- ou, pior, uma escolha silenciosa.
CONST = {"AEECLSID_": "kClsid_", "AEEIID_": "kIid_", "AEEPRIVID_": "kPrivId_"}

DEFINE = re.compile(r"^\s*#\s*define\s+([A-Za-z_]\w*)\s*(.*)$")
TOKEN = re.compile(r"\s*(0[xX][0-9a-fA-F]+|\d+|[A-Za-z_]\w*|<<|>>|[()+/|&~-]|\*)")


def sem_comentarios(texto: str) -> str:
    """Tira os comentarios PRESERVANDO as linhas.

    A primeira versao substituia cada bloco `/* */` por um espaco, e isso
    DESLOCAVA a numeracao: o `AEECLSID_TEXTCTL` passava a ser reportado na linha
    197 em vez da 209. O numero da linha entra no `.inc` (e por ele que se vai ao
    cabecalho), portanto tem de ser o do ficheiro.
    """
    texto = re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group(0)), texto, flags=re.S)
    saida = []
    for linha in texto.split("\n"):
        i = linha.find("//")
        saida.append(linha[:i] if i >= 0 else linha)
    return "\n".join(saida)


def defines_do_ficheiro(caminho: Path):
    """[(nome, texto_do_valor, linha)] de um ficheiro, com as continuacoes.

    As continuacoes (`\\` no fim da linha) sao juntadas SEM perder a linha de
    origem: o macro comeca onde comeca.
    """
    try:
        texto = sem_comentarios(caminho.read_text(errors="ignore"))
    except OSError:
        return []
    linhas = texto.split("\n")
    saida = []
    i = 0
    while i < len(linhas):
        m = DEFINE.match(linhas[i])
        if m:
            nome, valor, linha = m.group(1), m.group(2).strip(), i + 1
            while valor.endswith("\\") and i + 1 < len(linhas):
                i += 1
                valor = valor[:-1] + " " + linhas[i].strip()
            if valor:
                saida.append((nome, valor, linha))
        i += 1
    return saida


class ErroExpressao(Exception):
    pass


def avaliar(texto: str, resolver, profundidade=0):
    """Avalia a expressao inteira de um `#define` de CLSID.

    Aceita o que os cabecalhos usam -- literais, nomes de outros defines, `+`,
    `-`, `*`, `/`, `&`, `|`, `~`, `<<`, `>>` e parenteses -- e RECUSA o resto
    (chamadas, strings, `sizeof`) em vez de devolver um numero inventado.
    """
    if profundidade > 40:
        raise ErroExpressao("ciclo de definicoes")
    toks, i = [], 0
    while i < len(texto):
        m = TOKEN.match(texto, i)
        if not m or m.start() != i:
            if texto[i:].strip() == "":
                break
            raise ErroExpressao("token invalido em %r" % texto[i:i + 16])
        toks.append(m.group(1))
        i = m.end()
    pos = [0]

    def espiar():
        return toks[pos[0]] if pos[0] < len(toks) else None

    def tira():
        t = espiar()
        pos[0] += 1
        return t

    def primario():
        t = tira()
        if t is None:
            raise ErroExpressao("fim inesperado")
        if t == "(":
            v = soma()
            if tira() != ")":
                raise ErroExpressao("falta )")
            return v
        if t == "-":
            return -primario()
        if t == "+":
            return primario()
        if t == "~":
            return ~primario()
        if re.match(r"^0[xX]", t):
            return int(t, 16)
        if t.isdigit():
            return int(t)
        if re.match(r"^[A-Za-z_]\w*$", t):
            return resolver(t, profundidade + 1)
        raise ErroExpressao("operando invalido %r" % t)

    def produto():
        v = primario()
        while espiar() in ("*", "/", "&", "|", "<<", ">>"):
            op = tira()
            r = primario()
            if op == "*":
                v *= r
            elif op == "/":
                if r == 0:
                    raise ErroExpressao("divisao por zero")
                v //= r
            elif op == "&":
                v &= r
            elif op == "|":
                v |= r
            elif op == "<<":
                v <<= r
            elif op == ">>":
                v >>= r
        return v

    def soma():
        v = produto()
        while espiar() in ("+", "-"):
            op = tira()
            r = produto()
            v = v + r if op == "+" else v - r
        return v

    valor = soma()
    if pos[0] != len(toks):
        raise ErroExpressao("sobra %r" % toks[pos[0]:])
    return valor


# TODOS os `#define` do SDK entram na TABELA DE RESOLUCAO -- e nao so os da
# familia CLSID. `AEECLSID_CONTROL` e `(QVERSION + 0x3000)`, e o `QVERSION` nao
# tem prefixo nenhum: sem ele, 230 nomes (o `AEECLSID_TEXTCTL` entre eles) ficavam
# por resolver. O que se EMITE continua a ser so a familia AEECLSID_/AEEIID_/
# AEEPRIVID_.
#
# Duas declaracoes do mesmo nome com textos DIFERENTES sao um conflito: quem fica
# e a primeira, e o conflito vai para o stderr.
def recolher(sdk: Path):
    definicoes = {}
    conflitos = []
    for caminho in sorted(sdk.rglob("*")):
        if caminho.suffix not in (".bid", ".h"):
            continue
        for nome, valor, linha in defines_do_ficheiro(caminho):
            if nome in definicoes:
                if definicoes[nome][0] != valor and nome.startswith(PREFIXOS):
                    conflitos.append((nome, definicoes[nome][1],
                                      f"{caminho.relative_to(sdk)}:{linha}"))
                continue
            # O CAMINHO RELATIVO AO SDK, e nao o absoluto: o `.inc` e versionado e
            # vai ser lido noutra maquina -- um caminho que so existe aqui dentro
            # nao leva ninguem ao cabecalho. O SDK e a raiz que o gerador recebeu.
            definicoes[nome] = (valor, f"{caminho.relative_to(sdk)}:{linha}")
    return definicoes, conflitos


def resolver(mapa):
    """{nome: valor} dos que se conseguem avaliar."""
    cache = {}

    def valor_de(nome, profundidade):
        if nome in cache:
            return cache[nome]
        if nome not in mapa:
            raise ErroExpressao("nao definido: " + nome)
        v = avaliar(mapa[nome][0], valor_de, profundidade)
        cache[nome] = v
        return v

    ok, maus = {}, {}
    for nome in sorted(n for n in mapa if n.startswith(PREFIXOS)):
        try:
            v = valor_de(nome, 0)
            if not 0 <= v <= 0xFFFFFFFF:
                raise ErroExpressao("fora de 32 bits: 0x%x" % v)
            ok[nome] = v
        except ErroExpressao as e:
            maus[nome] = str(e)
    return ok, maus


def main():
    if len(sys.argv) < 3:
        raise SystemExit("uso: gerar_clsids.py <dir_do_SDK> <saida.inc>")
    sdk, saida = Path(sys.argv[1]), Path(sys.argv[2])
    if not sdk.is_dir():
        raise SystemExit(f"SDK nao e um directorio: {sdk}")

    mapa, conflitos = recolher(sdk)
    # A LISTA DO TOOLSET, como TERCEIRA fonte.
    #
    # O SDK declara as classes em `*.bid` e nos `*.h`, mas ha 3208 delas no
    # `classes.txt` do Toolset (`utilities/SystemTask/c_systemtaskapp/`) e **1447
    # nao estao em nenhum daqueles** -- incluindo as FONTES
    # (`AEECLSID_FONT_STANDARD11` = 0x0102f679, `_15` = 0x01030852), que dois
    # titulos do corpus pedem e recebiam como "CLSID desconhecido": um numero, e
    # nao um nome, que e o que a regra P2 desta casa exige.
    #
    # O CSV esta VENDORIZADO em `tools/clsids-do-toolset.csv` (ficheiro do SDK,
    # nao escrito por nos): o Toolset vive FORA da raiz que este gerador recebe, e
    # uma leitura de um caminho que so existe nesta maquina nao serve para o CI.
    # Entra como fonte de MENOR prioridade -- quem declara a classe e o `*.bid`.
    acrescentados = 0
    csv = Path(__file__).resolve().parent / "clsids-do-toolset.csv"
    if csv.is_file():
        for linha in csv.read_text(errors="replace").splitlines():
            campos = linha.split(",")
            if len(campos) < 2 or not campos[1].lower().startswith("0x"):
                continue
            nome, valor = campos[0].strip(), campos[1].strip()
            if nome in mapa:
                continue
            # O `mapa` guarda o TEXTO da definicao (o mesmo que vem dos `*.h`, onde
            # muitos sao EXPRESSOES) -- o `resolver` e que avalia. Guardar aqui um
            # inteiro ja avaliado parte o `avaliar` (foi o erro da primeira versao).
            mapa[nome] = (valor, "clsids-do-toolset.csv")
            acrescentados += 1
    ok, maus = resolver(mapa)
    if acrescentados:
        print(f"clsids-do-toolset.csv: {acrescentados} nomes que o SDK nao declarava")
    if not ok:
        raise SystemExit("nenhum CLSID resolvido -- o SDK nao e este")

    # A ESCOLHA ENTRE APELIDOS. 504 dos 1793 valores tem mais do que um nome
    # (`AEECLSID_BITMAP` e `AEEIID_BITMAP` sao o mesmo 0x01001021, por exemplo).
    # A regra, declarada: primeiro o nome que vem de um `*.bid` -- o ficheiro em
    # que o SDK DECLARA a classe, e o que a documentacao da classe cita -- e
    # dentro do mesmo tipo, ordem alfabetica. Assim `0x0100104f` responde
    # `AEECLSID_AppHistory` e nao `AEECLSID_APPHISTORY`.
    # A TABELA DE NOMES SO LEVA VALORES DO ESPACO DE CLSID.
    #
    # E preciso, e a prova esta no proprio SDK: `AEECLSID_MD5Ctx_SIZE` vale **88**
    # (`AEECMD5Ctx.h:21`), e um TAMANHO em bytes, nao uma classe; o
    # `AEEPRIVID_PLFile` vale 0x0001 (`AEEPLPrivs.bid:9`), e um id de PRIVILEGIO.
    # Os dois entram na familia dos nomes e nenhum dos dois e um CLSID -- se
    # ficassem na tabela, um pedido de iid=0x58 respondia "AEECLSID_MD5Ctx_SIZE".
    #
    # O limite vem do cabecalho: `AEEClassIDs.h:20  #define QVERSION 0x01000000
    # // Most significant 8-bits are version`. Todo o CLSID deste SDK e
    # `QVERSION + familia + indice`, e por isso o espaco comeca em 0x01000000.
    # AS CONSTANTES ficam todas (sao factos do cabecalho, e ha quem as queira).
    porvalor = defaultdict(list)
    for nome, v in ok.items():
        if v < QVERSION:
            continue
        origem = mapa[nome][1]
        eh_bid = 0 if origem.endswith(".bid") or ".bid:" in origem else 1
        porvalor[v].append((eh_bid, nome))

    linhas = [
        "// GERADO por tools/gerar_clsids.py a partir dos `*.bid` e `*.h` do SDK.",
        "// NAO EDITAR A MAO: corre o gerador (`tools/verificar_clsids.sh` regenera",
        "// e compara). **Um CLSID escrito a mao ja divergiu neste trabalho** -- e o",
        "// preco foi uma ronda inteira a olhar para o sitio errado.",
        "//",
        "// Cada constante traz a ORIGEM (`ficheiro:linha`) a frente. E por ela que",
        "// se vai ao cabecalho conferir o numero sem o procurar.",
        "#pragma once",
        "",
        "namespace brew_clsids {",
        "",
        f"// {len(ok)} nomes, de {len(porvalor)} valores distintos. Os nomes que o",
        "// gerador NAO conseguiu avaliar sao listados no stderr do gerador e nao",
        "// entram aqui: um zero que parecesse uma medicao seria pior.",
        "",
    ]
    for nome in sorted(ok):
        prefixo = next(p for p in PREFIXOS if nome.startswith(p))
        curto = nome[len(prefixo):]
        linhas.append(f"constexpr unsigned {CONST[prefixo]}{curto} = 0x{ok[nome]:08x}u;"
                      f"  // {nome}  ({mapa[nome][1]})")
    linhas.append("")
    linhas.append("// O MESMO conjunto indexado pelo VALOR, para o motor poder responder")
    linhas.append("// `NomeDoClsid(iid)` -- que e o que faz a lista de demanda dizer")
    linhas.append("// `AEECLSID_AppHistory` em vez de `0x0100104f`. Ordenada por (valor,")
    linhas.append("// preferencia, nome); a preferencia esta explicada no gerador.")
    linhas.append("struct ClasseNomeada {")
    linhas.append("  unsigned valor;")
    linhas.append("  const char* nome;")
    linhas.append("};")
    linhas.append("constexpr ClasseNomeada kPorValor[] = {")
    for v in sorted(porvalor):
        cand = sorted(porvalor[v])
        linhas.append("    {0x%08xu, \"%s\"}," % (v, cand[0][1]))
    linhas.append("};")
    linhas.append("constexpr unsigned kQuantosValores = %d;" % len(porvalor))
    linhas.append("")
    linhas.append("}  // namespace brew_clsids")
    saida.write_text("\n".join(linhas) + "\n")
    print(f"{saida}: {len(linhas)} linhas, {len(ok)} nomes, {len(porvalor)} valores")
    if conflitos:
        print(f"AVISO: {len(conflitos)} nomes declarados com textos diferentes "
              "(fica a primeira declaracao):", file=sys.stderr)
        for nome, a, b in conflitos[:10]:
            print(f"  {nome}: {a} vs {b}", file=sys.stderr)
    if maus:
        print(f"AVISO: {len(maus)} nomes NAO resolvidos (e ficam de fora):", file=sys.stderr)
        for nome in sorted(maus):
            print(f"  {nome}: {maus[nome]}", file=sys.stderr)


if __name__ == "__main__":
    main()
