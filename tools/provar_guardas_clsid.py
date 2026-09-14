#!/usr/bin/env python3
"""PROVA CADA GUARDA DESTA FRENTE POR VIOLACAO DELIBERADA.

Uma guarda que passa sem a mudanca nao e guarda. Para cada violacao: quebra-se a
coisa de proposito e corre-se o que a deve apanhar; o resultado esperado e
VERMELHO. No fim o ficheiro e reposto do original, e a reposicao e CONFERIDA byte
a byte -- uma violacao esquecida e pior do que nenhuma.

Uso: python3 tools/provar_guardas_clsid.py <raiz_do_repo>

A raiz e a do REPOSITORIO (a pasta acima de `curupira/`). O SDK e procurado no
sitio habitual desta maquina; sem ele, a guarda dos CLSIDs sai 77 e o script
di-lo, em vez de contar um verde que nao correu.
"""
import subprocess
import sys
import tempfile
from pathlib import Path

RAIZ = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
CURUPIRA = RAIZ / "curupira"
SDK = Path("/home/rafaelfrequiao/projects/zeebo-emulator/research/docs/sdk-extract/"
           "BrewMPSDK-7.12.5/SDKPro/1.0.4.601 Pro")


def corre(cmd, cwd=None):
    r = subprocess.run(cmd, cwd=str(cwd or CURUPIRA), capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def guarda_do_inc():
    """`tools/verificar_clsids.sh` regenera e compara. Vermelho = rc != 0."""
    rc, _ = corre(["./tools/verificar_clsids.sh"])
    return rc != 0


def inc_regenerado_difere():
    """O gerador dos slots tem de produzir um `brew_slots.inc` DIFERENTE."""
    with tempfile.NamedTemporaryFile(suffix=".inc", delete=False) as tmp:
        destino = tmp.name
    corre(["python3", "tools/gerar_slots.py", str(SDK), destino])
    igual = Path(destino).read_bytes() == (CURUPIRA / "tools/brew_slots.inc").read_bytes()
    Path(destino).unlink(missing_ok=True)
    return not igual


def teste_vermelho(filtro):
    def f():
        corre(["cmake", "--build", "build", "-j4"])
        rc, _ = corre(["./build/zb2_tests", "--gtest_filter=" + filtro])
        return rc != 0
    return f


# (nome, ficheiro, ancora, substituto, o que verifica, porque)
VIOLACOES = [
    ("V1 o gerador deixa de avaliar a EXPRESSAO do cabecalho",
     "tools/gerar_clsids.py",
     "QVERSION = 0x01000000",
     "QVERSION = 0x01000001",
     guarda_do_inc,
     "A GUARDA TEM DE FALHAR. O 0x01003109 e AEECLSID_TEXTCTL_10 + 0x100; o _10 e"
     " AEECLSID_CONTROL+9; e o AEECLSID_CONTROL e QVERSION + 0x3000. Uma cadeia de"
     " TRES expressoes: se o gerador as deixar de avaliar, o numero sai errado."),

    ("V2 o .inc versionado passa a dizer outro valor para o AEECLSID_TEXTCTL",
     "tools/clsids.inc",
     "constexpr unsigned kClsid_TEXTCTL = 0x01003109u;",
     "constexpr unsigned kClsid_TEXTCTL = 0x01003108u;",
     guarda_do_inc,
     "A GUARDA TEM DE FALHAR: e a comparacao com o regenerado, E a conferencia dos"
     " tres CLSIDs do corpus que a segunda metade do script faz. O mesmo numero esta"
     " num static_assert de core/brew/classes.cpp -- o motor tambem nao compilaria."),

    ("V3 o gerador dos slots deixa de ler a CABECA do ITextCtl",
     "tools/gerar_slots.py",
     '    "DECLARE_ICONTROL",    # HandleEvent .. Reset   -- AEE.h:297',
     '    # (violacao deliberada: a cabeca do ITextCtl deixa de ser lida)',
     inc_regenerado_difere,
     "O REGENERADO TEM DE SAIR DIFERENTE, e com ele falha o teste"
     " Classes.OsSlotsMedidosNoModuloSaoOsQueOCabecalhoDa: sem os 9 slots do"
     " DECLARE_ICONTROL, o SetRect deixa de estar no 6 e o SetInputMode no 18 --"
     " que sao os indices MEDIDOS no desmonte do zenonia."),

    ("V4 o AtenderClasse deixa de REGISTAR a recusa (o caminho mudo, P2)",
     "core/brew/classes.cpp",
     "  traco.RegistarFalta(Area::Brew, nome, det);",
     "  // (violacao deliberada: recusa em silencio)",
     teste_vermelho("Classes.OMetodoNaoImplementadoRecusaComONomeDoMetodo"),
     "O TESTE TEM DE FALHAR. Sem o registo, a lista de demanda perde o nome do"
     " metodo e volta a ser uma recusa anonima -- o defeito que este trabalho veio"
     " tirar do caminho."),

    ("V5 a guarda de FAIXA do AtenderClasse desaparece",
     "core/brew/classes.cpp",
     "  if (indice < kVtableClasseBase || indice >= VtClasse(kQuantasClasses)) return false;",
     "  // (violacao deliberada: sem guarda de faixa)",
     teste_vermelho("Classes.AChamadaDeOutraFaixaNaoEApanhada"),
     "O TESTE TEM DE FALHAR. Sem a guarda, um indice da faixa do IShell (o 2002, o"
     " CreateInstance) e apanhado por este ramo -- o erro de ORDEM, que ja apareceu"
     " oito vezes nesta arvore. Sem a guarda, o indice tambem indexa as tabelas de"
     " nomes com um valor negativo em aritmetica sem sinal."),

    ("V6 o nome do slot deixa de corresponder ao slot (deslocado em 2)",
     "core/brew/classes.cpp",
     '  return k < kQuantasClasses ? kNomeDoSlot[k](slot) : "?";',
     '  return k < kQuantasClasses ? kNomeDoSlot[k](slot + 2) : "?";',
     teste_vermelho("Classes.OsSlotsMedidosNoModuloSaoOsQueOCabecalhoDa"),
     "O TESTE TEM DE FALHAR. Ele compara os indices MEDIDOS no modulo (o desmonte"
     " do zenonia e do tectoy) com os nomes que o gerador leu do cabecalho: sao duas"
     " fontes, e o teste existe para elas nao divergirem em silencio."),
]


def main():
    if not (CURUPIRA / "tools").is_dir():
        print("uso: %s <raiz_do_repo>  (esperava %s)" % (sys.argv[0], CURUPIRA))
        return 2
    sem_sdk = not (SDK / "platform/system/inc/AEEClassIDs.h").is_file()
    provadas = nao_provadas = 0
    for nome, rel, ancora, substituto, verificar, porque in VIOLACOES:
        alvo = CURUPIRA / rel
        original = alvo.read_text()
        if ancora not in original:
            print("[XX] %s: a ancora NAO esta em %s -- violacao nao aplicada" % (nome, rel))
            nao_provadas += 1
            continue
        alvo.write_text(original.replace(ancora, substituto, 1))
        try:
            vermelho = verificar()
        finally:
            alvo.write_text(original)
        if alvo.read_text() != original:
            print("[XX] %s NAO foi reposto -- PARAR" % rel)
            return 2
        provadas += 1 if vermelho else 0
        nao_provadas += 0 if vermelho else 1
        print("[%s] %s" % ("ok" if vermelho else "XX", nome))
        print("     porque:  %s" % porque)
        print("     resultado: %s" % ("VERMELHO" if vermelho else "VERDE <<< a guarda NAO existe"))
    corre(["cmake", "--build", "build", "-j4"])
    print()
    if sem_sdk:
        print("AVISO: o SDK nao foi encontrado -- V1/V2/V3 podem ter saido 77.")
    print("%d guardas provadas por violacao, %d NAO provadas" % (provadas, nao_provadas))
    return 1 if nao_provadas else 0


if __name__ == "__main__":
    sys.exit(main())
