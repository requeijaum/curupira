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
# Os dois argumentos do GL sao OPCIONAIS: sem eles o gerador faz exatamente o que
# fazia antes, e o `brew_slots.inc` sai byte a byte igual (a guarda `slots_do_sdk`
# compara-o, e uma mudanca acidental ali seria um defeito noutro modulo).
SAIDA_GL = Path(sys.argv[4]) if len(sys.argv) > 4 else None
AEGL = Path(sys.argv[3]) if len(sys.argv) > 3 else None

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
    # A MIDIA (etapa 5). O `INHERIT_IMedia` comeca em `INHERIT_IQI`, e nao em
    # `INHERIT_IBase`: o IMedia TEM `QueryInterface` (slot 2), e por isso os
    # metodos proprios comecam no 3. Ler a linha da heranca e o que evita o erro
    # de um que ja custou uma ronda inteira.
    ("Media",    "platform/media/inc/AEEIMedia.h",        "#define INHERIT_IMedia("),
    # A HIERARQUIA DE WIDGETS (IRootForm -> IForm -> IHandler, e
    # IWidget -> IHandler; IDrawDecorator -> IDecorator -> IWidget).
    #
    # PORQUE ISTO ENTRA AQUI, e nao a mao: a bateria MEDIU, depois de o
    # `EVT_APP_START` passar a ser entregue, pedidos a `IRootForm` -- uma camada
    # que SO aparece quando o titulo chega ao codigo que constroi a propria
    # interface. O numero de cada metodo sai da CADEIA de heranca lida do
    # cabecalho, como todos os outros:
    #     INHERIT_IHandler   = INHERIT_IQI(3) + 2 = 5
    #     INHERIT_IForm      = IHandler           = 5
    #     INHERIT_IRootForm  = IForm + 5          = 10
    #     INHERIT_IWidget    = IHandler + 9       = 14
    #     INHERIT_IDecorator = IWidget + 2        = 16
    #     INHERIT_IDrawDecorator = IDecorator + 1 = 17
    # AS TRES CLASSES DO ARRANQUE (o `CreateInstance` do corpus). Ver
    # `core/brew/classes.{h,cpp}`: o `tectoy` cria `AEECLSID_AppHistory` e
    # `AEECLSID_VALUEMODEL_1`, o `zenonia` cria `AEECLSID_TEXTCTL`.
    #
    # O `AEEText.h` NAO tem `INHERIT_`: a cabeca do `ITextCtl` e a forma antiga
    # (`QINTERFACE` + `DECLARE_IBASE` + `DECLARE_ICONTROL`), e o `metodos` sabe
    # le-la -- o nome do macro aqui e um marcador, porque o que ele procura e o
    # `QINTERFACE(ITextCtl)`.
    ("AppHistory", "platform/system/inc/AEEIAppHistory.h", "#define INHERIT_IAppHistory("),
    ("ValueModel", "platform/ui/inc/AEEIValueModel.h",     "#define INHERIT_IValueModel("),
    ("TextCtl",    "platform/deprecated/inc/AEEText.h",    "#define INHERIT_ITextCtl("),
    ("IHandler",  "platform/ui/inc/AEEIHandler.h",      "#define INHERIT_IHandler("),
    ("IForm",     "platform/ui/inc/AEEIForm.h",         "#define INHERIT_IForm("),
    ("IRootForm", "platform/ui/inc/AEEIRootForm.h",     "#define INHERIT_IRootForm("),
    ("IWidget",   "platform/ui/inc/AEEIWidget.h",       "#define INHERIT_IWidget("),
    ("IDecorator", "platform/ui/inc/AEEIDecorator.h",   "#define INHERIT_IDecorator("),
    ("IDrawDecorator", "platform/ui/inc/AEEIDrawDecorator.h",
     "#define INHERIT_IDrawDecorator("),
    # AS TRES INTERFACES QUE ESTAVAM ESCRITAS A MAO EM `core/brew/classes.cpp`.
    #
    # O `IThread` (o `tectoy` cria `AEECLSID_THREAD`), o `IImageDecoder` (o
    # `AEECLSID_PNGDECODER_BREW`) e o `IForceFeed` tinham os nomes de slot num
    # `switch` escrito a mao no `classes.cpp`, e as CONTAGENS (12 e 5) como
    # literais na tabela `kSlotsDaInterface` -- um numero de cabecalho transcrito
    # a mao, que e exactamente o defeito que este gerador existe para tirar do
    # caminho. As cadeias resolvem-se como todas as outras, lendo os cabecalhos:
    #
    #     INHERIT_IThread       = INHERIT_IRscPool(7) + 5 = 12
    #     INHERIT_IRscPool      = INHERIT_IQI(3) + 4      = 7   (AEEIRscPool.h)
    #     INHERIT_IImageDecoder = INHERIT_IQI(3) + 2      = 5
    #     INHERIT_IForceFeed    = INHERIT_IQI(3) + 2      = 5
    #
    # A CONTAGEM E OS NOMES SAEM DA MESMA LEITURA (`k<nome>Slots` e
    # `NomeDe<nome>`), para nao poderem divergir um do outro.
    #
    # O `IForceFeed` NAO e usado por nenhuma classe do motor hoje. Entra na mesma
    # porque a tabela e do SDK, e nao do que o motor ja usa: a proxima interface
    # desta familia fica com o numero ja lido, em vez de mais um `switch` a mao.
    ("Thread",   "platform/system/inc/AEEThread.h",       "#define INHERIT_IThread("),
    ("ImageDecoder", "platform/media/inc/AEEIImageDecoder.h",
     "#define INHERIT_IImageDecoder("),
    ("ForceFeed", "platform/system/inc/AEEIForceFeed.h",  "#define INHERIT_IForceFeed("),
    # IHashCtx is IQI (three head slots) plus Init/Update/Final/SetKey.
    # MD5Ctx is caller-context based: its 88 bytes are owned by the guest.
    ("HashCtx", "platform/security/inc/AEEIHashCtx.h", "#define INHERIT_IHashCtx("),
    # O `IGraphics` (a interface 2D do BREW 4.0, frente igfx). E da FORMA ANTIGA:
    # `QINTERFACE` + `DECLARE_IBASE`, sem `INHERIT_` -- o `metodos` ja sabe le-la
    # pelo `QINTERFACE(IGraphics)`, como faz com o `ITextCtl`. PORQUE ENTRA AQUI:
    # os quatro slots que o corpus pede (4 `SetColor`, 6 `SetFillMode`, 8
    # `SetFillColor`, 22 `DrawRect`) e os outros 40 nao podem ser numeros
    # escritos a mao -- um slot deslocado nesta interface NAO da erro, da desenho
    # errado.
    ("Graphics", "platform/ui/inc/AEEGraphics.h", "#define INHERIT_IGraphics("),
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


# O nome de cada slot da cabeca, na ordem. As MESMAS duas fontes que `CABECAS`
# usa: `AEEIBase.h` (AddRef, Release) e `AEEIQI.h` (mais o QueryInterface).
NOMES_DA_CABECA = {
    "INHERIT_IQueryInterface": ["AddRef", "Release", "QueryInterface"],
    "INHERIT_IQI": ["AddRef", "Release", "QueryInterface"],
    "INHERIT_IBase": ["AddRef", "Release"],
}


def _nomes_da_raiz(nome_macro: str, profundidade: int = 0) -> list:
    """Os NOMES dos slots da CABECA desta interface, na ordem.

    PORQUE ISTO EXISTE: a cabeca (os slots 0..n-1) tambem tem NOME, e o
    `QueryInterface` e o caso que o obrigou. `INHERIT_IHandler` comeca em
    `INHERIT_IQI`, que ocupa TRES slots -- `AddRef`, `Release` e `QueryInterface`
    -- e o `QueryInterface` nao e um "membro proprio" do IHandler, logo nao
    aparecia em tabela nenhuma. Um `kIHandler_QueryInterface = 2` escrito a mao
    seria exactamente o numero de slot transcrito de memoria que este gerador
    existe para impedir.
    """
    if profundidade > 8:
        raise SystemExit(f"cadeia de heranca demasiado funda em {nome_macro}")
    if nome_macro in NOMES_DA_CABECA:
        return list(NOMES_DA_CABECA[nome_macro])
    for cab in SDK.rglob("*.h"):
        try:
            t = cab.read_text(errors="replace")
        except OSError:
            continue
        i = t.find(f"#define {nome_macro}(")
        if i < 0 or "\n" not in t[i:]:
            continue
        linhas = _linhas_do_macro(t, i)
        if not linhas:
            continue
        pai = re.search(r"(INHERIT_\w+)\s*\(", linhas[0])
        if not pai:
            continue
        return _nomes_da_raiz(pai.group(1), profundidade + 1) + _membros("".join(linhas[1:]))
    raise SystemExit(f"nao consegui resolver os nomes da cabeca de {nome_macro}")


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


# Os macros de CABECA da forma antiga (`AEE.h`). Os nomes que eles ocupam sao
# LIDOS do cabecalho, e nao escritos aqui -- pela mesma razao de tudo o resto
# neste ficheiro.
MACROS_DE_CABECA = [
    "DECLARE_IBASE",       # AddRef, Release        -- AEE.h:175
    "DECLARE_ICONTROL",    # HandleEvent .. Reset   -- AEE.h:297
]


def membros_de_macro(nome: str):
    """Os nomes dos membros de um macro de cabeca de `AEE.h`, na ordem."""
    t = (SDK / "platform/system/inc/AEE.h").read_text(errors="replace")
    i = t.find(f"#define {nome}(")
    if i < 0:
        raise SystemExit(f"#define {nome}( nao esta em platform/system/inc/AEE.h")
    nomes = _membros("".join(_linhas_do_macro(t, i)))
    if not nomes:
        raise SystemExit(f"{nome}: nenhum membro lido -- o macro mudou de forma")
    return nomes


def metodos(caminho: Path, macro: str):
    """(membros, slots_de_cabeca, nota) de uma interface.

    `membros` sao SO os metodos proprios: a linha da heranca nao entra.
    `slots_de_cabeca` e o que a cadeia de heranca ocupa antes deles.
    """
    t = caminho.read_text(errors="replace")
    i = t.find(macro)
    if i < 0:
        nome_if = macro.replace("#define INHERIT_", "").replace("(", "")
        # A FORMA ANTIGA: `QINTERFACE(ITextCtl)` + `DECLARE_IBASE` +
        # `DECLARE_ICONTROL`.
        #
        # PORQUE ISTO EXISTE, e o `AEEText.h` que o obrigou. As interfaces
        # anteriores a BREW 3.x (o `ITextCtl`, que o `zenonia` cria por
        # `AEECLSID_TEXTCTL`) NAO usam `INHERIT_I...`: escrevem a cabeca com dois
        # macros de `AEE.h`,
        #
        #     QINTERFACE(ITextCtl) {
        #        DECLARE_IBASE(ITextCtl)     // AddRef, Release
        #        DECLARE_ICONTROL(ITextCtl)  // HandleEvent .. Reset
        #        boolean (*SetTitle)(...)    // e os proprios
        #     };
        #
        # e o `_membros` so ve os PROPRIOS (nao ha `(*` dentro de
        # `DECLARE_IBASE(ITextCtl)`). Os nomes da cabeca sao LIDOS DESSES DOIS
        # MACROS em `AEE.h`, e nao escritos aqui: e a mesma regra que resolve a
        # cadeia dos `INHERIT_` -- o cabecalho e a fonte, e uma lista escrita a
        # mao ja divergiu neste trabalho.
        mq = re.search(r"QINTERFACE\s*\(\s*" + re.escape(nome_if) + r"\s*\)", t)
        if mq:
            i = mq.start()
            j = t.find("};", i)
            bloco = t[i:j] if j > 0 else t[i:i + 8000]
            nomes_cabeca = []
            contagens = []
            for mc in MACROS_DE_CABECA:
                if re.search(r"\b" + mc + r"\s*\(", bloco):
                    quantos_este = membros_de_macro(mc)
                    nomes_cabeca += quantos_este
                    contagens.append(f"{mc} = {len(quantos_este)}")
            fns = [f for f in _membros(bloco) if f != "pfn"]
            repetidos = [f for f in fns if f in nomes_cabeca]
            if repetidos:
                raise SystemExit(f"{nome_if}: {repetidos} aparecem na cabeca E na interface")
            return (fns, len(nomes_cabeca),
                    f"QINTERFACE + {', '.join(contagens)}",
                    nomes_cabeca)
        # O ESPACO ANTES DO PARENTESES NAO E COSMETICO.
        #
        # `AEEGL.h` (a extensao OpenGL ES 1.5.3 do Zeebo) escreve
        # `AEEINTERFACE (IGL)` -- com espaco -- e o `AEEINTERFACE(IGL)` colado que
        # aqui estava nao encontrava nada. Um `find` que nao encontra e um
        # `SystemExit`, e nao um resultado vazio: foi o que fez isto aparecer em
        # vez de gerar uma tabela vazia em silencio.
        m = re.search(r"AEEINTERFACE\s*\(\s*" + re.escape(nome_if) + r"\s*\)", t)
        i = m.start() if m else -1
        if i < 0:
            raise SystemExit(f"macro nao encontrada: {macro} (nem AEEINTERFACE({nome_if})) em {caminho}")
        j = t.find("};", i)
        bloco = t[i:j] if j > 0 else t[i:i + 8000]
        pai = re.search(r"(INHERIT_\w+|INHERIT_IQueryInterface)\s*\(", bloco)
        quantos, origem = _raiz(pai.group(1)) if pai else (0, "interface raiz")
        nomes_cabeca = _nomes_da_raiz(pai.group(1)) if pai else []
        return (_membros(bloco), quantos,
                f"AEEINTERFACE({nome_if}): {pai.group(1) if pai else '-'} = {quantos}",
                nomes_cabeca)

    linhas = _linhas_do_macro(t, i)
    # linhas[0] e a HERANCA (quando existe); o resto sao membros proprios.
    pai = re.search(r"(INHERIT_\w+)\s*\(", linhas[0]) if linhas else None
    if pai:
        quantos, origem = _raiz(pai.group(1))
        corpo = "".join(linhas[1:])
        nota = f"{pai.group(1)} = {quantos} slots de cabeca"
        nomes_cabeca = _nomes_da_raiz(pai.group(1))
    else:
        quantos, nota = 0, "interface raiz"
        corpo = "".join(linhas)
        nomes_cabeca = []
    if len(nomes_cabeca) != quantos:
        raise SystemExit(
            f"{macro}: a cabeca ocupa {quantos} slots e dei {len(nomes_cabeca)} nomes")
    return _membros(corpo), quantos, nota, nomes_cabeca


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
    fns, base_off, nota, nomes_cabeca = metodos(SDK / cab, macro)
    linhas.append(f"// {cab}")
    linhas.append(f"//   cabeca: {nota}")
    linhas.append(f"//   {len(fns)} metodos; slot = {base_off} + posicao")
    # OS SLOTS DA CABECA TAMBEM TEM NOME. Sem isto, o `QueryInterface` -- que em
    # `INHERIT_IQI` e o slot 2 -- nao existia em tabela nenhuma e teria de ser
    # escrito a mao onde fosse preciso.
    for k, fn in enumerate(nomes_cabeca):
        linhas.append(f"constexpr unsigned k{nome}_{fn} = {k};")
    for k, fn in enumerate(fns):
        if fn in ("pfn",):
            continue
        linhas.append(f"constexpr unsigned k{nome}_{fn} = {k + base_off};")
    # E O NOME DE CADA SLOT, indexado pelo proprio slot.
    #
    # Um slot sem nome e uma recusa anonima: o despacho registaria `ITextCtl::slot6`
    # e a lista de demanda voltaria a obrigar a ir ao cabecalho CONTAR em cada
    # ronda -- que e o defeito que este trabalho existe para tirar do caminho. A
    # tabela do IGL ja e assim (`NomeIgl`), e a razao e a mesma (P2/P7).
    todos = nomes_cabeca + [f for f in fns if f != "pfn"]
    linhas.append(f"// Quantos slots esta interface TEM, lido do cabecalho. E o limite da")
    linhas.append(f"// cablagem e o do `NomeDe{nome}` -- uma so constante, para os dois nao")
    linhas.append(f"// poderem divergir.")
    linhas.append(f"constexpr unsigned k{nome}Slots = {len(todos)};")
    linhas.append("")
    linhas.append(f"inline const char* NomeDe{nome}(unsigned slot) {{")
    linhas.append("  static const char* k[] = {")
    linhas.append("      " + ", ".join(f'"{n}"' for n in todos) + ",")
    linhas.append("  };")
    linhas.append(f"  return slot < {len(todos)} ? k[slot] : \"slot_fora_da_tabela\";")
    linhas.append("}")
    linhas.append("")
# ---------------------------------------------------------------------------
# AS CONSTANTES DAS PROPRIEDADES DA INTERFACE DE WIDGETS.
# ---------------------------------------------------------------------------
#
# PORQUE ISTO ESTA AQUI E NAO ESCRITO A MAO: `PROP_FORM` e `0x5000` e o
# `WID_FORM` e `PROP_FORM + 0` -- um deslocamento DENTRO da familia, nao um
# numero solto. Um numero de offset transcrito de memoria ja divergiu uma vez
# neste trabalho (os slots do IDisplay) e custou uma ronda inteira. Aqui a
# expressao do cabecalho e LIDA e AVALIADA, com o valor que sai escrita ao lado
# para quem le o `.inc` poder conferir sem abrir o cabecalho.
#
# Estas constantes aparecem no `r2` dos pedidos que a bateria MEDIU:
#     IRootForm::slot3 r1=0x00000800 r2=0x00005000   -> EVT_WDG_GETPROPERTY, WID_FORM
#     IRootForm::slot3 r1=0x00000801 r2=0x00005001   -> EVT_WDG_SETPROPERTY, WID_TITLE
#     IRootForm::slot3 r1=0x00000800 r2=0x00005002   -> EVT_WDG_GETPROPERTY, WID_SOFTKEYS
# -- medidos no `tectoy` (o Z-Wheel), ver o relatorio.
CONSTANTES = [
    ("AEEIForm.h", "platform/ui/inc/AEEIForm.h", [
        "PROP_FORM", "WID_FORM", "WID_TITLE", "WID_SOFTKEYS", "WID_BACKGROUND",
        "WID_CONTAINER", "WID_DECORATOR", "WID_BACKDROP", "WID_STATIC",
        "FID_ACTIVE", "FID_ROOT", "FID_THEME", "FID_THEME_FNAME", "FID_TITLE",
        "FID_BACKGROUND", "FID_VISIBLE", "FID_DISPLAY", "FID_THEME_BASENAME",
    ]),
    ("AEEWidgetProperties.h", "platform/ui/inc/AEEWidgetProperties.h", [
        "PROP_BGCOLOR", "PROP_ACTIVE_BGCOLOR", "PROP_INACTIVE_BGCOLOR",
        "PROP_FGCOLOR", "PROP_ACTIVE_FGCOLOR", "PROP_INACTIVE_FGCOLOR",
        "PROP_BORDERCOLOR", "PROP_ACTIVE_BORDERCOLOR", "PROP_INACTIVE_BORDERCOLOR",
        "PROP_BORDERSTYLE",
    ]),
    ("AEEIWidget.h", "platform/ui/inc/AEEIWidget.h", [
        "EVT_WDG_SETPROPERTY", "EVT_WDG_GETPROPERTY", "EVT_WDG_SETFOCUS",
        "EVT_WDG_HASFOCUS", "EVT_WDG_CANTAKEFOCUS", "EVT_WDG_SETLAYOUT",
    ]),
    # Os IIDs das interfaces da hierarquia. Sao o que o `QueryInterface` tem de
    # responder -- e o valor TEM de vir do cabecalho: um IID escrito de memoria
    # foi, nesta arvore, a causa medida de o `QueryInterface` deixar de responder
    # ao `AEECLSID_DISPLAY` e a bateria cair de 22 applets para 1.
    ("AEEIHandler.h", "platform/ui/inc/AEEIHandler.h", ["AEEIID_IHandler"]),
    ("AEEIForm.h", "platform/ui/inc/AEEIForm.h", ["AEEIID_IForm"]),
    ("AEEIRootForm.h", "platform/ui/inc/AEEIRootForm.h", ["AEEIID_IRootForm"]),
    ("AEEIWidget.h", "platform/ui/inc/AEEIWidget.h", ["AEEIID_IWidget"]),
    ("AEEIDecorator.h", "platform/ui/inc/AEEIDecorator.h", ["AEEIID_IDecorator"]),
    # `AEEIID_IDrawDecorator` NAO ENTRA, e a razao e uma medicao: este SDK nao o
    # DEFINE em cabecalho nenhum. `grep -rn AEEIID_IDrawDecorator` sobre a arvore
    # extraida so encontra uma mencao na DOCUMENTACAO (`AEEIDrawDecorator.h:397`).
    # Inventar o valor seria exactamente o defeito que este gerador existe para
    # impedir -- e um valor de memoria ja regrediu a bateria uma vez.
]


def _todos_os_defines(sdk: Path):
    """{nome: valor_em_texto} de TODOS os `#define NOME valor` dos cabecalhos.

    Le so o `inc/` da plataforma: e onde estao os cabecalhos de interface. Um
    `#define` com parametros (`NOME(...)`) nao entra -- nao e uma constante.
    """
    out = {}
    for cab in sdk.rglob("*.h"):
        try:
            t = cab.read_text(errors="replace")
        except OSError:
            continue
        for m in re.finditer(r"^[ \t]*#define[ \t]+([A-Za-z_]\w*)(?!\()[ \t]+([^\n]*?)[ \t]*$", t, re.M):
            nome, valor = m.group(1), m.group(2).strip()
            if valor and nome not in out:
                out[nome] = valor
    return out


def valor_de(nome: str, defines: dict, profundidade: int = 0):
    """O valor INTEIRO de uma constante, seguindo as referencias a outras.

    Devolve `None` quando nao consegue -- e quem chama diz que nao conseguiu, em
    vez de escrever um zero que pareceria uma medicao.
    """
    if profundidade > 8:
        return None
    if nome in ("TRUE", "FALSE"):
        return 1 if nome == "TRUE" else 0
    bruto = defines.get(nome)
    if bruto is None:
        m = re.match(r"^0[xX][0-9A-Fa-f]+$|^\d+$", nome)
        if m:
            return int(nome, 0)
        return None
    expr = bruto.split("//")[0].strip().rstrip("uUlL")
    if re.match(r"^0[xX][0-9A-Fa-f]+$|^\d+$", expr):
        return int(expr, 0)
    # uma expressao: resolve cada NOME que la esteja e avalia-a em aritmetica
    # inteira. A lista de operadores aceitos e curta de proposito -- `<<`, `|` e
    # `~` nao aparecem nestas familias, e se aparecerem quero saber.
    def trocar(m):
        v = valor_de(m.group(0), defines, profundidade + 1)
        return str(v) if v is not None else m.group(0)
    substituido = re.sub(r"[A-Za-z_]\w*", trocar, expr)
    if re.search(r"[A-Za-z_]", substituido):
        return None
    if not re.match(r"^[0-9+\-*/(). \t]+$", substituido):
        return None
    try:
        return int(eval(substituido, {"__builtins__": {}}, {}))  # noqa: S307
    except Exception:
        return None


def escrever_constantes(sdk: Path):
    defines = _todos_os_defines(sdk)
    linhas, faltam = [], []
    for cab, caminho, nomes in CONSTANTES:
        linhas.append(f"// {caminho}")
        for nome in nomes:
            v = valor_de(nome, defines)
            if v is None:
                faltam.append(f"{nome} ({caminho})")
                continue
            linhas.append(f"constexpr unsigned {nome} = {v}u;")
        linhas.append("")
    return linhas, faltam


# As constantes das propriedades (ver a nota acima). Ficam DEPOIS dos slots e
# DENTRO do mesmo namespace: sao a mesma classe de numero -- um valor da ABI
# lido do cabecalho -- e por isso recebem a mesma guarda (`verificar_slots.sh`
# regenera o ficheiro e compara).
_cl, _falta = escrever_constantes(SDK)
if _falta:
    raise SystemExit("constantes nao resolvidas no SDK: " + ", ".join(_falta))
linhas += _cl
linhas.append("}  // namespace brew_slots")
SAIDA.write_text("\n".join(linhas) + "\n")
print(f"{SAIDA}: {len(linhas)} linhas")


# =============================================================================
# A TABELA DO IGL (e do IEGL)
# =============================================================================
#
# PORQUE ISTO NAO SAI DE UM CABECALHO NO `inc/` DO SDK.
#
# `AEEGL.h` NAO ESTA na arvore extraida do SDK: ele vive DENTRO do instalador
# `OpenGLES_Extension_1.5.3_For_BREW_SDK_4.x.x_General_Installer/Installer.msi`
# (medido: `find` sobre a arvore inteira nao o encontra). O cabecalho e extraido
# do MSI por `tools/achar_aegl.py` e o caminho chega aqui como argumento.
#
# OS NOMES E A ORDEM SAO DO CABECALHO, argumento a argumento. Nao ha numero de
# slot escrito a mao -- que foi exatamente o erro que a arvore antiga cometeu com
# o mapa do IGLES11, copiado de outro emulador.
#
# E A CONFERENCIA NAO E O CABECALHO A CONFERIR-SE A SI PROPRIO: quando o caminho
# do `conftest.elf` (o `.mod` de exemplo do proprio SDK, compilado pela Zeebo)
# e dado, os 77 thunks `gl*` e os 25 `egl*` sao DESMONTADOS, e o slot que cada um
# pede tem de ser o mesmo. Duas fontes independentes do SDK tem de concordar.

# O nome dos slots herdados, na ordem. Vem de `AEEIBase.h` e `AEEIQI.h`.
# A MESMA tabela de cima, com o nome que o gerador do GL ja usava. Uma so fonte:
# duas listas que tem de concordar sao zero listas.
CABECAS_NOMES = NOMES_DA_CABECA


def nomes_da_cabeca(pai: str):
    return CABECAS_NOMES.get(pai, ["?slot?"] * 0)


def metodos_gl(header: Path, iface: str):
    """(nomes_de_todos_os_slots, nota) do `AEEINTERFACE (iface)` num cabecalho.

    Diferente de `metodos`, devolve uma lista INDEXADA PELO SLOT -- com os nomes
    da cabeca nas primeiras posicoes -- porque o despacho precisa do nome para
    registar a chamada (P2/P7: um slot sem nome e uma recusa anonima).
    """
    t = header.read_text(errors="replace")
    m = re.search(r"AEEINTERFACE\s*\(\s*" + re.escape(iface) + r"\s*\)", t)
    if not m:
        raise SystemExit(f"AEEINTERFACE({iface}) nao encontrada em {header}")
    i = m.start()
    # O BLOCO ACABA NO `};` DA PROPRIA INTERFACE.
    #
    # A primeira versao procurava um `AEEINTERFACE_DEFINE` a frente, e o do IEGL
    # NAO EXISTE: o bloco esticava-se pelas 300 linhas de DOCUMENTACAO que se
    # seguem, e o `metodos_gl` devolvia 105 nomes em vez de 28. **Um falso
    # positivo no gerador e pior do que um numero escrito a mao: parece que foi
    # medido.** Foi a conferencia contra o ELF que o apanhou -- 25 divergencias de
    # `egl*`, todas por 77 de diferenca.
    j = t.find("};", i)
    bloco = t[i:j] if j > 0 else t[i:i + 40000]
    pai = re.search(r"(INHERIT_\w+)\s*\(", bloco)
    if not pai:
        raise SystemExit(f"{iface}: sem INHERIT reconhecida em {header}")
    cabeca, _ = _raiz(pai.group(1))
    fns = [f for f in _membros(bloco) if f != "pfn"]
    nomes = nomes_da_cabeca(pai.group(1)) + fns
    if len(nomes) != cabeca + len(fns):
        raise SystemExit(f"{iface}: cabeca {cabeca} + {len(fns)} metodos != {len(nomes)} nomes")
    return nomes, f"{pai.group(1)} = {cabeca} slots de cabeca + {len(fns)} metodos"


def thunk_do_wrapper(vaddr: int, ler32) -> tuple:
    """(base_ROPI, deslocamento, slot) do thunk de um wrapper `gl*`/`egl*`.

    O QUE ISTO MEDE, e porque e a prova mais forte que ha aqui. O desmonte do
    `conftest.elf` (o `.mod` de exemplo do PROPRIO SDK da Zeebo, compilado pela
    Zeebo) mostra, para o `glCullFace` em 0x28500:

        ldr  r1, [pc, #16]   ; 0x28518 = 0x20f0, um deslocamento, nao um ponteiro
        add  r1, pc, r1      ; 0x2850c + 0x20f0 = 0x2a5fc
        ldr  r1, [r1]        ; 0x2a5fc e EXACTAMENTE o endereco que `nm` da a gpIGL
        ldr  r1, [r1]        ; a vtable do objecto
        ldr  r1, [r1, #76]   ; o slot 19 (76/4)
        bx   r1

    O cabecalho `AEEGL.h` poe o `glCullFace` no 19. Duas fontes independentes
    concordam, e e isso que a guarda exige -- nao uma citacao de outro emulador.

    A leitura NAO e posicional: as funcoes com mais de quatro argumentos empilham
    o resto antes de chamar, e `mov` intercalados aparecem entre os `ldr` da
    cadeia (`glCompressedTexImage2D` em 0x28398 faz `mov r3,r6` no meio). Por isso
    o que se segue e uma maquina de estados de tres passos sobre o registo que
    carrega a cadeia, e nao quatro instrucoes fixas.
    """
    ws = [ler32(vaddr + 4 * k) for k in range(32)]

    def ldr_imm(w):
        return (w & 0x0F700000) == 0x05100000   # P=1,U=1,B=0,W=0,L=1

    def add_pc(w, rd):
        return ((w >> 12) & 0xF) == rd and ((w >> 16) & 0xF) == 15 and (w & 0xF) == rd \
            and ((w >> 21) & 0xF) == 0x4

    def escreve_em(w, reg):
        """Se a instrucao escreve no registo alvo, a cadeia perdeu-se.

        O CAMPO Rd DE UM `str` NAO E UM DESTINO, e essa distincao foi MEDIDA.

        A primeira versao devolvia `op in (0, 1, 2, 3, 5)` -- "transferencia de
        dados, branch, ldr, ldm, ldr-imediato" -- e com isso um `str r3, [sp]`
        (op=2, L=0) contava como escrita do r3 e QUEBRAVA a cadeia. O sintoma
        estava no `ddragonz.mod`, no thunk do `eglChooseConfig` em 0x123dac:

            123dac  push {r3, lr}          ; o prologo de um metodo com 5 argumentos
            123db8  str  r3, [sp]          ; <<< aqui a cadeia "perdia-se"
            123dbc  ldr  r3, [pc, #32]
            123dc0  add  r3, pc, r3
            123dc4  ldr  r3, [r3, #4]      ; &gpIEGL
            123dc8  ldr  r3, [r3]
            123dcc  ldr  ip, [r3, #40]     ; slot 10 (40/4) -- escolhido por r12, e nao por r3
            123dd0  mov  r3, lr            ; devolve o 5.o argumento ao r3
            123dd4  mov  lr, pc
            123dd8  bx   ip

        Com aquela versao, o gerador (e a sonda) NAO VIA este thunk: o `ddragonz`
        aparecia com 37 thunks em vez de 41, e o `eglChooseConfig` -- que o jogo
        chama mesmo -- ficava invisivel. **Uma contagem que parece medida e mede a
        coisa errada e pior do que nenhuma.**

        Duas correccoes, as duas com a medicao acima:
          1. so um `ldr` (L=1) ou um `ldm` (L=1) escrevem no Rd; um `str`/`stm` nao;
          2. entre o `ldr` do slot e o `bx` pode haver ate `JANELA_DO_BX`
             instrucoes -- o prologo de 5 argumentos mete o `mov r3, lr` e o
             `mov lr, pc` no meio. O registo do slot nao pode ser reescrito nessa
             janela, e o `bx` tem de ser ao registo que recebeu o slot.
        """
        if ((w >> 12) & 0xF) != reg:
            return False
        if w == 0xE12FFF10 | reg:          # bx reg -- nao escreve
            return False
        op = (w >> 25) & 0x7
        if op in (0, 1):                   # processamento de dados
            return not ((w >> 20) & 1 and not ((w >> 21) & 1))   # cmp/tst nao escrevem
        if op == 2:                        # ldr escreve; STR NAO
            return ((w >> 20) & 1) == 1
        if op == 3:                        # ldm escreve; stm nao
            return ((w >> 20) & 1) == 1
        if op == 5:                        # bl escreve o LR; b nao
            return ((w >> 24) & 1) == 1
        return False

    # Quantas instrucoes podem separar o `ldr` do slot do `bx` que salta para ele.
    JANELA_DO_BX = 4

    for k in range(len(ws) - 6):
        if not ldr_imm(ws[k]) or ((ws[k] >> 16) & 0xF) != 15:
            continue
        rd = (ws[k] >> 12) & 0xF
        if not add_pc(ws[k + 1], rd):
            continue
        base = (vaddr + 4 * (k + 1) + 8) + ler32(vaddr + 4 * k + 8 + (ws[k] & 0xFFF))
        reg, estagio, kk = rd, 0, None
        for j in range(k + 2, min(k + 20, len(ws))):
            w = ws[j]
            if w in (0xE12FFF10 | reg, ):      # bx reg
                break
            if ldr_imm(w) and ((w >> 16) & 0xF) == reg:
                destino, imm = (w >> 12) & 0xF, w & 0xFFF
                if estagio == 0:
                    # O primeiro salto le o PONTEIRO da interface: +0 = gpIGL,
                    # +4 = gpIEGL. Sao os dois unicos valores possiveis, e a
                    # conferencia contra os simbolos confirma qual e qual.
                    if imm not in (0, 4):
                        break
                    kk, reg, estagio = imm, destino, 1
                elif estagio == 1:
                    if imm != 0:
                        break
                    reg, estagio = destino, 2
                else:
                    if imm == 0 or imm % 4:
                        break
                    # O `bx` nao vem obrigatoriamente na instrucao seguinte: o
                    # thunk de 5 argumentos do `eglChooseConfig` no `ddragonz`
                    # (0x123dac) mete o `mov r3, lr` e o `mov lr, pc` entre o
                    # `ldr ip, [r3, #40]` e o `bx ip`. Exigir a instrucao
                    # seguinte perdia o thunk -- MEDIDO: 37 thunks em vez de 41.
                    for j2 in range(j + 1, min(j + 1 + JANELA_DO_BX, len(ws))):
                        # `bx` (0xE12FFF10) e `blx` (0xE12FFF30) saltam AMBOS para
                        # o registo, e os dois aparecem: o `conftest.elf` do SDK
                        # fecha o `glCompressedTexImage2D` (0x28398) com `blx ip`
                        # e o `ddragonz.mod` fecha o `eglChooseConfig` (0x123dac)
                        # com `bx ip`. Exigir so o `bx` recusava o thunk do SDK.
                        if ws[j2] in (0xE12FFF10 | destino, 0xE12FFF30 | destino):
                            return base, kk, imm // 4
                        if escreve_em(ws[j2], destino):
                            break
                    break
            elif escreve_em(w, reg):
                break
    return None


def ler_elf(elf: Path):
    """(simbolos, leitor32) de um ELF de 32 bits armlittle."""
    import struct
    b = elf.read_bytes()
    if b[:4] != b"\x7fELF":
        raise SystemExit(f"{elf} nao e um ELF")
    (_, _, machine, _, _, _, _, _, _, _, _, _, shnum, shstrndx) = struct.unpack_from(
        "<16sHHIIIIIHHHHHH", b, 0)
    assert machine == 40, "so ARM"
    e_shoff = struct.unpack_from("<I", b, 32)[0]
    e_shentsize = struct.unpack_from("<H", b, 46)[0]
    secoes = []
    for k in range(shnum):
        o = e_shoff + k * e_shentsize
        nome, tipo, flags, addr, offset, size, link, info, align, entsize = \
            struct.unpack_from("<IIIIIIIIII", b, o)
        secoes.append(dict(nome=nome, tipo=tipo, addr=addr, offset=offset, size=size,
                           link=link, entsize=entsize))
    strtab_sh = secoes[shstrndx]

    def cstr(off, i):
        j = b.index(b"\0", off + i)
        return b[off + i:j].decode()

    for s in secoes:
        s["nome_str"] = cstr(strtab_sh["offset"], s["nome"])
    sym = next(s for s in secoes if s["tipo"] == 2)
    strs = secoes[sym["link"]]
    simbolos = {}
    for k in range(sym["size"] // 16):
        o = sym["offset"] + k * 16
        st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack_from(
            "<IIIBBH", b, o)
        if st_name == 0:
            continue
        # TODOS os simbolos, e nao so as funcoes: `gpIGL`/`gpIEGL` sao OBJECT, e
        # sao eles que provam que o thunk resolve o endereco certo (o `add pc`
        # tem de dar exatamente 0x2a5fc e 0x2a600).
        simbolos[cstr(strs["offset"], st_name)] = (st_value, st_info & 0xF)

    def ler32(vaddr):
        for s in secoes:
            if s["addr"] and s["addr"] <= vaddr < s["addr"] + s["size"]:
                return struct.unpack_from("<I", b, s["offset"] + (vaddr - s["addr"]))[0]
        raise SystemExit(f"endereco 0x{vaddr:x} fora de qualquer secao de {elf}")

    return simbolos, ler32


# As CONSTANTES DO GL que o modulo usa, lidas de `platform/ui/inc/gles/gl.h`.
#
# A LISTA de nomes e escolhida (sao as que o estado implementado precisa de
# comparar); os VALORES sao lidos do cabecalho. Escrever `0x0DE1` a mao seria
# repetir o erro dos slot do IDisplay -- um numero da ABI transcrito de memoria.
GL_ENUMS = """GL_POINTS GL_LINES GL_LINE_LOOP GL_LINE_STRIP GL_TRIANGLES GL_TRIANGLE_STRIP
GL_TRIANGLE_FAN GL_NEVER GL_LESS GL_EQUAL GL_LEQUAL GL_GREATER GL_NOTEQUAL GL_GEQUAL
GL_ALWAYS GL_ZERO GL_ONE GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA GL_FRONT GL_BACK
GL_FRONT_AND_BACK GL_CW GL_CCW GL_BYTE GL_UNSIGNED_BYTE GL_SHORT GL_UNSIGNED_SHORT
GL_FIXED GL_FLOAT GL_CULL_FACE GL_LIGHTING GL_TEXTURE_2D GL_NORMALIZE GL_ALPHA_TEST
GL_BLEND GL_DITHER GL_FOG GL_DEPTH_TEST GL_SCISSOR_TEST GL_POLYGON_OFFSET_FILL
GL_VERTEX_ARRAY GL_NORMAL_ARRAY GL_COLOR_ARRAY GL_TEXTURE_COORD_ARRAY GL_MODELVIEW
GL_PROJECTION GL_TEXTURE GL_NO_ERROR GL_INVALID_ENUM GL_INVALID_VALUE
GL_INVALID_OPERATION GL_STACK_OVERFLOW GL_STACK_UNDERFLOW GL_OUT_OF_MEMORY
GL_COLOR_BUFFER_BIT GL_DEPTH_BUFFER_BIT GL_STENCIL_BUFFER_BIT GL_VENDOR GL_RENDERER
GL_VERSION GL_EXTENSIONS GL_NEAREST GL_LINEAR GL_TEXTURE_MAG_FILTER
GL_TEXTURE_MIN_FILTER GL_SMOOTH GL_FLAT GL_FASTEST GL_NICEST GL_DONT_CARE
GL_UNPACK_ALIGNMENT GL_PACK_ALIGNMENT GL_MAX_TEXTURE_SIZE GL_MAX_MODELVIEW_STACK_DEPTH
GL_MAX_PROJECTION_STACK_DEPTH GL_MAX_TEXTURE_STACK_DEPTH GL_SUBPIXEL_BITS GL_RED_BITS
GL_GREEN_BITS GL_BLUE_BITS GL_ALPHA_BITS GL_DEPTH_BITS GL_STENCIL_BITS GL_RGBA GL_RGB
GL_ALPHA GL_LUMINANCE GL_LUMINANCE_ALPHA GL_POINT_SIZE GL_LINE_WIDTH
GL_POLYGON_OFFSET_FACTOR GL_POLYGON_OFFSET_UNITS GL_TEXTURE0""".split()

CABECALHO_GL = "platform/ui/inc/gles/gl.h"


def enums_do_gl(sdk: Path):
    """[(nome, valor_em_texto)] das constantes que o modulo precisa."""
    t = (sdk / CABECALHO_GL).read_text(errors="replace")
    achadas = {}
    for nome in GL_ENUMS:
        m = re.search(r"#define\s+" + re.escape(nome) + r"\s+(0x[0-9A-Fa-f]+|\d+)\s*(?:/\*.*?\*/)?\s*$",
                      t, re.M)
        if not m:
            raise SystemExit(f"{CABECALHO_GL}: constante {nome} nao encontrada")
        achadas[nome] = m.group(1)
    return [(n, achadas[n]) for n in GL_ENUMS]


# As CONSTANTES DO EGL, do cabecalho `platform/ui/inc/EGL/egl.h` do SDK (o
# `gles/egl.h` e um esqueleto de compatibilidade que so inclui este).
#
# SO AS QUE TEM VALOR NUMERICO ENTRAM AQUI. `EGL_DEFAULT_DISPLAY`,
# `EGL_NO_DISPLAY`, `EGL_NO_CONTEXT` e `EGL_NO_SURFACE` sao CASTS
# (`((EGLDisplay)0)`), e o valor que o ARM carrega e zero -- escreve-los por um
# `re` seria inventar. Ficam no `core/brew/egl.h`, com a linha do cabecalho
# citada, porque zero e um numero que se le.
#
# A LISTA e escolhida (sao as que o estado implementado compara); os VALORES sao
# lidos do cabecalho -- a mesma regra do GL.
EGL_ENUMS = """EGL_FALSE EGL_TRUE EGL_SUCCESS EGL_NOT_INITIALIZED
EGL_BAD_ACCESS EGL_BAD_ALLOC EGL_BAD_ATTRIBUTE EGL_BAD_CONFIG EGL_BAD_CONTEXT
EGL_BAD_CURRENT_SURFACE EGL_BAD_DISPLAY EGL_BAD_MATCH EGL_BAD_NATIVE_PIXMAP
EGL_BAD_NATIVE_WINDOW EGL_BAD_PARAMETER EGL_BAD_SURFACE EGL_BUFFER_SIZE EGL_ALPHA_SIZE
EGL_BLUE_SIZE EGL_GREEN_SIZE EGL_RED_SIZE EGL_DEPTH_SIZE EGL_STENCIL_SIZE
EGL_CONFIG_CAVEAT EGL_CONFIG_ID EGL_LEVEL EGL_MAX_PBUFFER_HEIGHT EGL_MAX_PBUFFER_PIXELS
EGL_MAX_PBUFFER_WIDTH EGL_NATIVE_RENDERABLE EGL_NATIVE_VISUAL_ID EGL_NATIVE_VISUAL_TYPE
EGL_SAMPLES EGL_SAMPLE_BUFFERS EGL_SURFACE_TYPE EGL_TRANSPARENT_TYPE EGL_NONE
EGL_BIND_TO_TEXTURE_RGB EGL_BIND_TO_TEXTURE_RGBA EGL_MIN_SWAP_INTERVAL
EGL_MAX_SWAP_INTERVAL EGL_LUMINANCE_SIZE EGL_ALPHA_MASK_SIZE EGL_COLOR_BUFFER_TYPE
EGL_RENDERABLE_TYPE EGL_MATCH_NATIVE_PIXMAP EGL_CONFORMANT EGL_SLOW_CONFIG
EGL_NON_CONFORMANT_CONFIG EGL_TRANSPARENT_RGB EGL_RGB_BUFFER EGL_LUMINANCE_BUFFER
EGL_NO_TEXTURE EGL_TEXTURE_RGB EGL_TEXTURE_RGBA EGL_TEXTURE_2D EGL_PBUFFER_BIT
EGL_PIXMAP_BIT EGL_WINDOW_BIT EGL_OPENGL_ES_BIT EGL_VENDOR EGL_VERSION EGL_EXTENSIONS
EGL_CLIENT_APIS EGL_HEIGHT EGL_WIDTH EGL_LARGEST_PBUFFER EGL_TEXTURE_FORMAT
EGL_TEXTURE_TARGET EGL_MIPMAP_TEXTURE EGL_MIPMAP_LEVEL EGL_RENDER_BUFFER
EGL_HORIZONTAL_RESOLUTION EGL_VERTICAL_RESOLUTION EGL_PIXEL_ASPECT_RATIO
EGL_SWAP_BEHAVIOR EGL_BACK_BUFFER EGL_SINGLE_BUFFER EGL_BUFFER_PRESERVED
EGL_BUFFER_DESTROYED EGL_CONTEXT_CLIENT_TYPE EGL_CONTEXT_CLIENT_VERSION EGL_OPENGL_ES_API
EGL_DRAW EGL_READ EGL_CORE_NATIVE_ENGINE""".split()

CABECALHO_EGL = "platform/ui/inc/EGL/egl.h"


def enums_do_egl(sdk: Path):
    """[(nome, valor_em_texto)] das constantes do EGL que o modulo usa."""
    t = (sdk / CABECALHO_EGL).read_text(errors="replace")
    achadas = {}
    for nome in EGL_ENUMS:
        m = re.search(r"#define\s+" + re.escape(nome) + r"\s+(0x[0-9A-Fa-f]+|\d+)\s*(?:/\*.*?\*/)?\s*$",
                      t, re.M)
        if not m:
            raise SystemExit(f"{CABECALHO_EGL}: constante {nome} nao encontrada")
        achadas[nome] = m.group(1)
    return [(n, achadas[n]) for n in EGL_ENUMS]


# AS DUAS INTERFACES DO GL, DECLARADAS UMA SO VEZ.
#
# (interface, prefixo do metodo no SDK, prefixo da constante, simbolo no ELF)
#
# Estavam escritas em TRES sitios -- as duas chamadas ao `metodos_gl`, os dois
# lacos das constantes e os dois blocos da conferencia contra o ELF -- e tres
# sitios que tem de concordar sao zero sitios: acrescentar uma terceira interface
# era acrescentar em tres lugares e esquecer-se num. **Foi esquecer-se de um
# acrescento numa lista que ja apagou uma implementacao neste trabalho** (o
# `aee_GetUpTimeMS` na lista dos ajudantes).
# (interface, prefixo do metodo no SDK, prefixo da constante, simbolo no ELF,
#  nome da funcao que devolve o nome do slot, constante do tamanho da tabela)
INTERFACES_GL = [
    ("IGL", "gl", "kIgl_", "gpIGL", "NomeIgl", "kIglSlots"),
    ("IEGL", "egl", "kIegl_", "gpIEGL", "NomeIegl", "kIeglSlots"),
]


def escrever_gl(saida: Path, aegl: Path, elf, sdk: Path):
    nomes = {}
    notas = {}
    for iface, _, _, _, _, _ in INTERFACES_GL:
        nomes[iface], notas[iface] = metodos_gl(aegl, iface)
    igl, nota_igl = nomes["IGL"], notas["IGL"]
    ieg, nota_ieg = nomes["IEGL"], notas["IEGL"]
    linhas = [
        "// GERADO por tools/gerar_slots.py a partir de `AEEGL.h`, argumento a argumento.",
        "// NAO EDITAR A MAO: corre o gerador (`tools/verificar_slots_gl.sh`).",
        "//",
        "// O `AEEGL.h` do Zeebo NAO esta no `inc/` do SDK: vive dentro do instalador",
        "// `OpenGLES_Extension_1.5.3_For_BREW_SDK_4.x.x_General_Installer/Installer.msi`,",
        "// e `tools/achar_aegl.py` extrai-o. Este ficheiro e a unica fonte dos numeros",
        "// de slot do IGL -- copiar a ordem de outro emulador foi o erro do IGLES11 na",
        "// arvore antiga, e um numero escrito a mao ja divergiu uma vez neste trabalho.",
        "//",
        "// Cada slot tem NOME, porque uma recusa sem nome nao se pode ler (P2/P7).",
        "#pragma once",
        "",
        "namespace gl_slots {",
        *[linha for iface, _, _, _, _, const_slots in INTERFACES_GL
          for linha in (f"// AEEGL.h -- {notas[iface]}",
                        f"constexpr unsigned {const_slots} = {len(nomes[iface])};")],
        "",
    ]
    # O prefixo do SDK cai no NOME DA CONSTANTE, e nao no nome do metodo: o
    # namespace ja diz de que interface se trata, e `kIgl_glMatrixMode` diria
    # "gl" duas vezes. O nome POR EXTENSO (`"glMatrixMode"`) continua a ser o do
    # SDK -- e o que aparece no traco.
    for iface, prefixo_metodo, prefixo_const, _, _, _ in INTERFACES_GL:
        for k, nome in enumerate(nomes[iface]):
            curto = nome[len(prefixo_metodo):] if nome.startswith(prefixo_metodo) else nome
            linhas.append(f"constexpr unsigned {prefixo_const}{curto} = {k};")
        linhas.append("")
    linhas.append("// O nome de CADA slot, indexado pelo proprio slot. Um despacho que")
    linhas.append("// devolvesse um sucesso mudo sem nome de metodo seria o defeito do")
    linhas.append("// `glCullFace` outra vez -- 86 377 chamadas descartadas em silencio.")
    for iface, _, _, _, fn_nome, const_slots in INTERFACES_GL:
        linhas.append(f"inline const char* {fn_nome}(unsigned slot) {{")
        linhas.append("  static const char* k[] = {")
        linhas.append("      " + ", ".join(f'"{n}"' for n in nomes[iface]) + ",")
        linhas.append("  };")
        linhas.append(f'  return slot < {const_slots} ? k[slot] : "slot_fora_da_tabela";')
        linhas.append("}")
    linhas.append("")
    linhas.append(f"// As constantes do GL, lidas de {CABECALHO_GL}.")
    for nome, valor in enums_do_gl(sdk):
        linhas.append(f"constexpr unsigned {nome} = {valor}u;")
    linhas.append("")
    linhas.append(f"// As constantes do EGL, lidas de {CABECALHO_EGL}.")
    for nome, valor in enums_do_egl(sdk):
        linhas.append(f"constexpr unsigned {nome} = {valor}u;")
    linhas.append("")
    linhas.append("}  // namespace gl_slots")

    # A CONFERENCIA CONTRA O WRAPPER COMPILADO PELO PROPRIO SDK.
    if elf is not None:
        simbolos, ler32 = ler_elf(Path(elf))
        base_igl = simbolos.get("gpIGL", (None, 0))[0]
        base_ieg = simbolos.get("gpIEGL", (None, 0))[0]
        if base_igl is None or base_ieg is None:
            raise SystemExit(f"{elf}: sem os simbolos gpIGL/gpIEGL")
        medidas = {}
        for nome, (va, tipo) in sorted(simbolos.items()):
            if tipo != 2:                      # so STT_FUNC
                continue
            if not (nome.startswith("gl") or nome.startswith("egl")):
                continue
            r = thunk_do_wrapper(va, ler32)
            if r is None:
                raise SystemExit(f"{elf}: nao consegui desmontar o thunk de {nome} (0x{va:x})")
            base, kk, slot = r
            # O TRUNK resolve `&gpIGL + kk`, com kk em {0,4}: o bloco ROPI tem os
            # dois ponteiros const seguidos (`gpIGL` e `gpIEGL`), e o deslocamento
            # diz qual deles. Exigir as duas igualdades e exigir que o `add pc` do
            # thunk caia mesmo em cima do simbolo -- nao basta "parecer um slot".
            if base != base_igl or base + 4 != base_ieg:
                raise SystemExit(
                    f"{elf}: {nome} resolve &gpIGL+{kk} = 0x{base + kk:x}, mas gpIGL=0x"
                    f"{base_igl:x} e gpIEGL=0x{base_ieg:x}")
            medidas[nome] = slot
        divergencias = []
        todos_os_nomes = [n for iface, _, _, _, _, _ in INTERFACES_GL for n in nomes[iface]]
        for iface, _, _, _, _, _ in INTERFACES_GL:
            for k, nome in enumerate(nomes[iface]):
                if nome in ("AddRef", "Release", "QueryInterface"):
                    continue
                if nome not in medidas:
                    divergencias.append(f"{nome}: no cabecalho (slot {k}), ausente do ELF")
                elif medidas[nome] != k:
                    divergencias.append(f"{nome}: cabecalho diz {k}, ELF diz {medidas[nome]}")
        for nome, slot in medidas.items():
            if nome not in todos_os_nomes:
                divergencias.append(f"{nome}: no ELF (slot {slot}), ausente do cabecalho")
        if divergencias:
            for d in divergencias:
                print(f"DIVERGENCIA: {d}", file=sys.stderr)
            raise SystemExit(
                f"{len(divergencias)} divergencias entre AEEGL.h e {elf} -- "
                "as duas fontes do SDK TEM de concordar")
        linhas.insert(9, f"// CONFERIDO contra {Path(elf).name}: "
                         f"{len(medidas)} thunks desmontados, {len(igl) + len(ieg)} slots, "
                         "zero divergencias com o cabecalho.")
        print(f"CONFERIDO: {len(medidas)} thunks de {Path(elf).name} contra o cabecalho, "
              "zero divergencias")

    saida.write_text("\n".join(linhas) + "\n")
    print(f"{saida}: {len(linhas)} linhas ({len(igl)} slots do IGL, {len(ieg)} do IEGL)")


if len(sys.argv) > 4:
    # `--conferir-elf` sozinho: gera e confere sem escrever (usado pela guarda).
    escrever_gl(SAIDA_GL, Path(sys.argv[3]), sys.argv[5] if len(sys.argv) > 5 else None, SDK)
