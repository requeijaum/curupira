#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""AUDITOR DIFERENCIAL DO DESCODIFICADOR ARM: a NOSSA descodificacao contra o objdump.

O QUE ISTO RESPONDE
-------------------
"Qual instrucao o interpretador pensa que esta palavra e, e qual e que ela e?"

Medicao que obrigou a ferramenta a existir (no `LEDGER-ZEEBULATOR.md` e no commit
`0286921`): o grupo "extra load/store" (LDRH/STRH/LDRSB/LDRSH/LDRD/STRD) NAO
existia no interpretador. Os bits 27-25 dessas instrucoes sao `000`, os MESMOS do
grupo de dados processados, e nao havia ramo proprio -- logo cada
`ldrd r8, sb, [sp, #0x20]` corria como `BIC r8, sp, r0, LSR r2`, EM SILENCIO. O
`+12` do objecto do modulo ficava com o proprio SP e 21 dos 62 titulos saltavam
para a PILHA. Ninguem deu por isso durante centenas de commits porque uma
instrucao mal descodificada NAO recusa: da um resultado plausivel e errado.

O CAMINHO, e porque nao ha uma segunda descodificacao aqui
---------------------------------------------------------
    objdump -D -b binary -m armv6 <ficheiro>          (o oraculo)
    zb2_sonda_descodificador <ficheiro> --bin=...     (o NOSSO ramo, por palavra)

O segundo binario NAO descodifica nada por sua conta: executa a palavra uma vez e
le o NOME que o ramo de execucao escreveu (`FamiliaDaUltima`). Uma segunda
descodificacao escrita aqui, em Python, poderia divergir do interpretador e
concordar consigo mesma -- que e o defeito a encontrar, e nao o metodo.

AS TRES CLASSES DE DIVERGENCIA (e por que a ordem importa)
---------------------------------------------------------
  (b) `executamos_outra` -- o caso do `ldrd`: nos corremos uma coisa e o objdump
      diz outra. E o SILENCIOSO, e o mais grave. Vem primeiro no relatorio.
  (a) `recusamos`        -- falta a classe, e a recusa tem nome (P2).
  (c) `nome_diferente`   -- o mesmo efeito com dois nomes (`push` e `stmdb`,
      `svc` e `swi`). Ruido, contado a parte para nao esconder os outros dois.

Os `.word` do objdump (o que ele NAO sabe descodificar) sao `objdump_nao_sabe`:
em binario cru a maior parte e DADOS (tabelas, literais, texto) e o interpretador
executa-os na mesma quando o PC lhes chega. Ficam contados a parte, porque
"dados executados" nao se arranja com instrucoes.

USO
---
    tools/auditar_descodificador.py <mods|ficheiro.mod|corpus.json> [opcoes]

Opcoes:
    --sonda CAMINHO     binario da sonda (por omissao `build/zb2_sonda_descodificador`)
    --objdump CAMINHO   objdump    (por omissao `arm-none-eabi-objdump`)
    --json FICHEIRO     escreve o relatorio em JSON (para comparar corridas)
    --limite N          so as primeiras N palavras de cada ficheiro
    --titulo NOME       so o titulo NOME (com --corpus)
    --exemplos N        exemplos guardados por classe (por omissao 5)
    --top N             linhas do relatorio (por omissao 25)
    --thumb             modo Thumb: a sonda le 16 bits e o objdump usa force-thumb
    --verboso           imprime cada divergencia, e nao so o resumo

Codigos de saida: 0 sem divergencias silenciosas | 1 ha divergencias | 2 uso |
3 ferramenta em falta ou ficheiro ilegivel | 4 a sonda e o objdump nao alinham
(recusar e melhor do que comparar palavras trocadas) | 5 nada para auditar.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
from collections import Counter, defaultdict

# ---------------------------------------------------------------------------
# AS CLASSES DE INSTRUCAO: um vocabulario, e a traducao dos DOIS lados para ele.
# ---------------------------------------------------------------------------
#
# A comparacao e feita em DUAS escalas, e as duas aparecem no relatorio:
#   - a CLASSE (o que a instrucao faz): dados, multiplicacao, carga, guarda,
#     bloco, ramo, status, coprocessador, swap, swi, extensao/sinal (ARMv6) ...
#   - o MNEMONICO (o nome). Um `ldrh` executado onde o objdump diz `strh` e um
#     erro de EFEITO (leitura contra escrita) e tem de aparecer; um `push` contra
#     `stmdb` e o mesmo efeito com dois nomes.
CLASSE_DE_MNEMONICO = {}
for m in ("and eor sub rsb add adc sbc rsc tst teq cmp cmn orr mov bic mvn").split():
    CLASSE_DE_MNEMONICO[m] = "dados"
for m in ("mul mla umull umlal smull smlal").split():
    CLASSE_DE_MNEMONICO[m] = "multiplicacao"
for m in ("smulbb smulbt smultb smultt smulwb smulwt smlawb smlawt "
          "smlabb smlabt smlaltb smlaltt smlalwb smlalwt "
          "smulxy smlaxy smulwy smlalxy qadd qsub qdadd qdsub").split():
    CLASSE_DE_MNEMONICO[m] = "dsp"
for m in ("uxtb uxth sxtb sxth uxtab uxtah sxtab sxtah rev rev16 revsh sel clz").split():
    CLASSE_DE_MNEMONICO[m] = "extensao_armv6"
# CARGA E GUARDA EM CLASSES SEPARADAS, de proposito: um `strb` executado como
# `ldrb` e o MESMO tipo de defeito que o `ldrd` (a operacao trocada, em
# silencio). Com as duas na mesma classe, o auditor contava-o como
# "nome_diferente" e ESCONDIA-O -- foi o que aconteceu com o `strb` do Thumb
# (0x7000) ate esta classe ser partida em duas.
for m in ("ldr ldrb ldrt ldrbt").split():
    CLASSE_DE_MNEMONICO[m] = "carrega"
for m in ("str strb strt strbt").split():
    CLASSE_DE_MNEMONICO[m] = "guarda"
for m in ("ldrh ldrsb ldrsh ldrd ldrht ldrsbt ldrsht").split():
    CLASSE_DE_MNEMONICO[m] = "extra_carrega"
for m in ("strh strd strht").split():
    CLASSE_DE_MNEMONICO[m] = "extra_guarda"
for m in ("ldmia ldmib ldmda ldmdb push").split():
    CLASSE_DE_MNEMONICO[m] = "bloco_carrega"
for m in ("stmia stmib stmda stmdb pop").split():
    CLASSE_DE_MNEMONICO[m] = "bloco_guarda"
for m in ("b bl bx blx").split():
    CLASSE_DE_MNEMONICO[m] = "ramo"
for m in ("mrs msr").split():
    CLASSE_DE_MNEMONICO[m] = "status"
for m in ("swp swpb").split():
    CLASSE_DE_MNEMONICO[m] = "swap"
for m in ("mcr mrc cdp ldc stc mcrr mrrc ldc2 stc2 cdp2 mcr2 mrc2").split():
    CLASSE_DE_MNEMONICO[m] = "coprocessador"
for m in ("svc swi".split()):
    CLASSE_DE_MNEMONICO[m] = "swi"
for m in ("pld pldw pli".split()):
    CLASSE_DE_MNEMONICO[m] = "pre-carga"
for m in ("bkpt".split()):
    CLASSE_DE_MNEMONICO[m] = "quebra"

# Apelidos que o binutils imprime e que sao a MESMA instrucao. Sem esta tabela o
# relatorio enchia-se de `push` contra `stmdb` e escondia o que interessa.
APELIDOS = {
    "push": "stmdb", "pop": "ldmia",
    "stmfd": "stmdb", "ldmfd": "ldmia", "stmea": "stmia", "ldmea": "ldmdb",
    "stmfa": "stmib", "ldmfa": "ldmda", "stmed": "stmda", "ldmed": "ldmib",
    "ldm": "ldmia", "stm": "stmia",
    "svc": "swi",
    "ldmdb": "ldmdb", "stmdb": "stmdb",  # escritos por extenso no binutils novo
    # O modificador `L` do LDC/STC (transferencia longa) faz parte do nome que o
    # binutils imprime (`ldclvs`): para o auditor e a MESMA instrucao.
    "ldcl": "ldc", "stcl": "stc",
    # Os deslocamentos de registrador sao o MESMO codigo que o `mov`: o
    # binutils imprime `lsl r0, r1, #4` onde o opcode e o do `mov`. Contar 904
    # `mov` contra `asr` como divergencia seria ruido de vocabulario.
    "lsl": "mov", "lsr": "mov", "asr": "mov", "ror": "mov", "rrx": "mov",
    # `nop` e o `mov r0, r0` (0xE1A00000) com o nome que o binutils lhe da.
    "nop": "mov",
}

CONHECIDOS = set(CLASSE_DE_MNEMONICO) | set(APELIDOS)

# O objdump NAO sabe: e dados. Ficam contados, mas fora das divergencias de
# instrucao -- ver o cabecalho.
DESCONHECIDO = {".word", ".short", ".byte", "undefined", "udf", "bad"}


# Os nomes que a SONDA escreve e o mnemonico do objdump, quando diferem sem
# diferenca de efeito. Cada entrada tem a medicao que a justifica.
NOMES_NOSSOS = {
    # `blx <rotulo>` e a forma imediata; o objdump escreve so `blx`.
    "blx_imediato": "blx",
    "media_armv6_desconhecida": "",
    "dsp_desconhecida": "",
}


def classe_do_nosso(nome):
    """"Familia|motivo" (como a sonda escreve) -> (mnemonico, classe, recusou)."""
    recusou = "|" in nome
    mnemonico = nome.split("|", 1)[0]
    motivo = nome.split("|", 1)[1] if recusou else ""
    if mnemonico.startswith("thumb:formato"):
        # Uma FORMA de primeiro nivel do Thumb que nao esta implementada: a
        # recusa diz QUAL (o nome e do ARM ARM A6.2), e a classe e propria para
        # nao se confundir com uma instrucao mal descodificada.
        return mnemonico, "thumb_formato", recusou
    if mnemonico.startswith("thumb:"):
        return mnemonico[6:], CLASSE_DE_MNEMONICO.get(mnemonico[6:], "thumb"), recusou
    if mnemonico == "condicao_falsa":
        return "condicao_falsa", "nao_executada", False
    if mnemonico == "nenhuma":
        return "nenhuma", "instrumento", False
    if mnemonico == "nv":
        return "nv", "nv", recusou
    mnemonico = NOMES_NOSSOS.get(mnemonico, mnemonico)
    if mnemonico != "":
        return mnemonico, CLASSE_DE_MNEMONICO.get(mnemonico, "desconhecida"), recusou
    mnemonico = nome.split("|", 1)[0]
    return mnemonico, CLASSE_DE_MNEMONICO.get(mnemonico, "desconhecida"), recusou


def apelido_de_um_registrador(mnemonico, texto):
    """`push {lr}` e `pop {pc}` sao `str`/`ldr` de UM registrador, e nao LDM/STM.

    Medido no `a3d.mod` (+0x200, palavra 0xe52de004): o binutils imprime
    `push {lr}` e o interpretador diz `str` -- e o interpretador esta CERTO, porque
    a codificacao e a de transferencia simples. Sem esta distincao o relatorio
    acusava 49 divergencias que nao existem.
    """
    # O binutils escreve o PUSH/POP com a condicao colada (`popeq {lr}`), e
    # `push {lr}` de UM registrador e o `str lr, [sp, #-4]!` -- uma transferencia
    # simples, e nao um bloco. Sem esta limpeza, 1 418 divergencias no corpus
    # eram so isto.
    base = mnemonico
    for sufixo in SUFIXOS:
        if base.endswith(sufixo) and base[:-len(sufixo)] in ("push", "pop"):
            base = base[:-len(sufixo)]
            break
    if base not in ("push", "pop"):
        return None
    dentro = texto.split("{", 1)
    if len(dentro) != 2:
        return None
    regs = [r for r in dentro[1].split("}")[0].split(",") if r.strip()]
    if len(regs) != 1:
        return None
    return "str" if base == "push" else "ldr"


def mnemonico_do_objdump(texto):
    """A primeira palavra do campo de instrucao do objdump, sem o sufixo de condicao."""
    if not texto:
        return ""
    t = texto.strip()
    if t.startswith("."):
        return t.split()[0].lower()
    t = t.split("@")[0].strip()          # comentarios (`@ 0x94`)
    if not t:
        return ""
    m = t.split()[0].split(";")[0].lower()
    return m


# As condicoes do ARM, como sufixo de mnemonico (`bne`, `andseq`).
CONDICOES = ("eq", "ne", "cs", "hs", "cc", "lo", "mi", "pl", "vs", "vc",
             "hi", "ls", "ge", "lt", "gt", "le", "al")


# O que a tabela conhece: os mnemonics e tambem os APELIDOS (`stm` so existe como
# apelido), senao `stmhi` ficava por cortar e aparecia como divergencia.
CONHECIDOS = set(CLASSE_DE_MNEMONICO) | set(APELIDOS)

# Os sufixos possiveis, do mais longo para o mais curto. O `s` vem ANTES da
# condicao no nome (`andseq` = `and` + `s` + `eq`), logo as duas letras nao sao
# um sufixo so. A ordem e a que evita a leitura errada: `bls` tem de ser lido
# como `b` + `ls` antes de se tentar qualquer coisa com o `s` solto.
SUFIXOS = sorted({("s" + c) for c in CONDICOES} | set(CONDICOES) | {"s"},
                 key=lambda s: (-len(s), s))


def normalizar(m):
    """Mnemonico -> (mnemonico canonico, classe).

    O binutils escreve a CONDICAO colada ao mnemonico (`bne`, `andseq`,
    `ldmdbne`) e o sufixo `s` antes dela. Sem esta limpeza o relatorio acusava
    20 081 `and` contra `andeq` no `a3d.mod` -- **divergencias que nao existem**,
    que e o pior resultado possivel para um auditor. Medido na primeira corrida.
    """
    if not m or m.split(".")[0] in DESCONHECIDO:
        return m, "objdump_nao_sabe"
    # O binutils marca a largura do Thumb-2 com `.n`/`.w` (`b.n`, `beq.n`).
    base = m.split(".")[0]
    # A CONDICAO E O `s` SAO TIRADOS EM QUALQUER ORDEM, e so quando o que fica e
    # um mnemonico que a tabela conhece: `rsbscs` e `rsb`+`s`+`cs` (tirar so a
    # condicao deixava `rsbs`, e o relatorio acusava 83 `rsb` contra `rsbscs`);
    # `bls` e o ramo `b` com a condicao LS, e nao um `bl` com `s`; `movs` le-se
    # como `mov` com `s` ou com a condicao VS, e as duas leituras dao o mesmo.
    for sufixo in SUFIXOS:
        if base.endswith(sufixo) and base[:-len(sufixo)] in CONHECIDOS:
            base = base[:-len(sufixo)]
            break
    base = APELIDOS.get(base, base)
    # O `ldc`/`stc` tem um modificador `L` (transferencia longa) que o binutils
    # imprime: `ldclvs`. E a MESMA instrucao para o auditor.
    if base in ("ldcl", "stcl"):
        base = base[:3]
    return base, CLASSE_DE_MNEMONICO.get(base, "desconhecida:" + m)


def normalizar_linha(texto):
    """A normalizacao a partir da LINHA do objdump (mnemonico + operandos)."""
    m = mnemonico_do_objdump(texto)
    um = apelido_de_um_registrador(m, texto)
    if um is not None:
        return um, CLASSE_DE_MNEMONICO[um]
    if "(UNDEF" in texto.upper():
        # O proprio objdump diz que o operando nao existe (`mrseq r0, (UNDEF: 3)`).
        # Sao palavras de DADOS, e nao uma classe que falte.
        return m, "objdump_nao_sabe"
    return normalizar(m)


# ---------------------------------------------------------------------------
# A sonda e o objdump
# ---------------------------------------------------------------------------
RE_LINHA_OBJDUMP = re.compile(r"^\s*([0-9a-f]+):\s+([0-9a-f]{4,8})\s+(.*)$")


def correr_sonda(sonda, ficheiro, limite, thumb, temporario):
    args = [sonda, ficheiro, "--bin=" + temporario]
    if thumb:
        args.append("--thumb")
    if limite:
        args.append("--fim=%d" % limite)
    p = subprocess.run(args, capture_output=True, text=True)
    if p.returncode != 0:
        sys.stderr.write("a sonda falhou (codigo %d): %s\n" % (p.returncode, p.stderr.strip()))
        sys.exit(3)
    nomes = {}
    palavras = None
    for linha in p.stdout.splitlines():
        if linha.startswith("#classe "):
            _, i, nome = linha.split(" ", 2)
            nomes[int(i)] = nome
        elif linha.startswith("#palavras "):
            palavras = int(linha.split()[1])
    with open(temporario, "rb") as f:
        classes = f.read()
    if palavras != len(classes):
        sys.stderr.write("a sonda nao alinhou: #palavras=%s bytes=%d\n" % (palavras, len(classes)))
        sys.exit(4)
    return nomes, classes


def correr_objdump(objdump, ficheiro, limite, thumb):
    """Devolve [(endereco, palavra_em_hex, texto_da_instrucao)].

    O ENDERECO E OBRIGATORIO, e nao um indice: o objdump NAO imprime uma linha por
    palavra -- ele colapsa as repeticoes de dados numa linha `...` (medido no
    `a3d.mod`: 127 772 palavras na imagem, 122 253 linhas, 866 saltos de
    endereco). Alinhar por indice comparava palavras trocadas a partir do
    primeiro `...`, que e o pior resultado possivel para um instrumento: um
    relatorio inteiro de divergencias que nao existem.
    """
    args = [objdump, "-D", "-b", "binary", "-m", "armv6"]
    if thumb:
        args += ["-M", "force-thumb"]
    args.append(ficheiro)
    p = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    saida = []
    for linha in p.stdout:
        m = RE_LINHA_OBJDUMP.match(linha)
        if m:
            saida.append((int(m.group(1), 16), m.group(2).lower(), m.group(3)))
            if limite and len(saida) >= limite:
                break
    p.stdout.close()
    p.wait()
    return saida


# ---------------------------------------------------------------------------
# A comparacao
# ---------------------------------------------------------------------------
class Relatorio(object):
    def __init__(self, exemplos):
        self.exemplos = exemplos
        self.pares = Counter()          # (nosso, objdump) -> n
        self.nomes_apelidos = Counter() # (nosso ~ objdump, classe) -> n
        self.motivos = Counter()        # motivo da recusa -> n
        self.ao_contrario = Counter()   # classe nossa -> n
        self.ex = defaultdict(list)
        self.total = 0
        self.concordam = 0
        self.recusamos_nome_certo = 0
        self.recusamos_sem_forma = 0
        self.classe_igual_mnemonico_diferente = 0
        self.ficheiros = {}

    def junta(self, ficheiro, nomes, classes, linhas_objdump, tamanho_da_palavra, verboso):
        # A GUARDA DO ALINHAMENTO: para cada linha do objdump, a palavra que ele
        # IMPRIMIU tem de ser a palavra que esta no ficheiro, no endereco que ele
        # diz. Sem esta conferencia, um erro de alinhamento produz um relatorio
        # cheio de divergencias falsas -- e ninguem desconfia de um relatorio.
        with open(ficheiro, "rb") as f:
            bytes_do_ficheiro = f.read()
        total_do_ficheiro = len(bytes_do_ficheiro) // tamanho_da_palavra
        if total_do_ficheiro != len(classes):
            sys.stderr.write("a sonda leu %d palavras de %s; o ficheiro tem %d\n"
                             % (len(classes), ficheiro, total_do_ficheiro))
            sys.exit(4)
        for endereco, palavra_hex, _ in linhas_objdump:
            i = endereco // tamanho_da_palavra
            if i >= total_do_ficheiro:
                sys.stderr.write("o objdump apontou para fora do ficheiro: 0x%x em %s\n"
                                 % (endereco, ficheiro))
                sys.exit(4)
            no_ficheiro = int.from_bytes(
                bytes_do_ficheiro[i * tamanho_da_palavra:(i + 1) * tamanho_da_palavra], "little")
            if int(palavra_hex, 16) != no_ficheiro:
                sys.stderr.write(
                    "NAO ALINHA em %s +0x%x: o objdump diz 0x%08x e o ficheiro tem 0x%08x. "
                    "Recuso comparar palavras trocadas.\n" % (ficheiro, endereco, int(palavra_hex, 16), no_ficheiro))
                sys.exit(4)
        contagem = Counter()
        contagem["palavras_do_ficheiro"] = total_do_ficheiro
        contagem["cobertas_pelo_objdump"] = len(linhas_objdump)
        for endereco, palavra_hex_para_guardar, texto in linhas_objdump:
            i = endereco // tamanho_da_palavra
            nome = nomes[classes[i]]
            mnemonico, classe, recusou = classe_do_nosso(nome)
            obj = normalizar_linha(texto)
            self.total += 1
            contagem["total"] += 1
            if classe == "nao_executada" or classe == "instrumento":
                contagem["anomalia_" + classe] += 1
                continue
            if obj[1] == "objdump_nao_sabe":
                contagem["objdump_nao_sabe"] += 1
                continue
            if recusou:
                # (a) falta a classe -- e o objdump sabe qual e. Duas sub-classes,
                # porque a accao e diferente: se o NOME bate, a descodificacao esta
                # certa e o que falta e a EXECUCAO (o SWI sem tratador, por
                # exemplo); se o nome nao bate, falta a forma de descodificar.
                if mnemonico == obj[0]:
                    chave = (mnemonico + " (recusado, nome certo)", obj[0])
                    self.recusamos_nome_certo += 1
                    contagem["recusamos_nome_certo"] += 1
                else:
                    chave = (mnemonico + " (recusado, falta a forma)", obj[0])
                    self.recusamos_sem_forma += 1
                    contagem["recusamos_sem_forma"] += 1
                self.pares[chave] += 1
                self.motivos[(mnemonico, nome.split("|", 1)[1])] += 1
                if len(self.ex[chave]) < self.exemplos:
                    self.ex[chave].append((ficheiro, endereco, palavra_hex_para_guardar, texto))
            elif mnemonico == obj[0]:
                self.concordam += 1
                contagem["concorda"] += 1
            elif classe == obj[1] and classe not in ("desconhecida",):
                # (c) mesmo efeito, dois nomes. Contado a parte: e ruido de
                # vocabulario (`push` contra `stmdb`), e mistura-lo com os outros
                # dois esconderia o que interessa.
                chave = (mnemonico + " ~ " + obj[0], "classe %s" % classe)
                self.nomes_apelidos[(mnemonico + " ~ " + obj[0], obj[0])] += 1
                self.classe_igual_mnemonico_diferente += 1
                contagem["nome_diferente"] += 1
                if len(self.ex[chave]) < self.exemplos:
                    self.ex[chave].append((ficheiro, endereco, palavra_hex_para_guardar, texto))
            else:
                # (b) O SILENCIOSO: executamos outra coisa
                chave = (mnemonico + " (nossa classe %s)" % classe, obj[0] + " (classe %s)" % obj[1])
                self.pares[chave] += 1
                self.ao_contrario[classe] += 1
                contagem["executamos_outra"] += 1
                if len(self.ex[chave]) < self.exemplos:
                    self.ex[chave].append((ficheiro, endereco, palavra_hex_para_guardar, texto))
        self.ficheiros[ficheiro] = dict(contagem)


def ficheiros_a_auditar(alvo, titulo=None):
    """"Um caminho pode ser um ficheiro, um directoria de titulos, ou um corpus."""
    if os.path.isdir(alvo):
        saida = []
        for raiz, _, ficheiros in os.walk(alvo):
            for f in sorted(ficheiros):
                if f.lower().endswith(".mod") or f.lower().endswith(".bin"):
                    saida.append(os.path.join(raiz, f))
        return sorted(saida)
    if alvo.lower().endswith(".json"):
        with open(alvo) as f:
            corpus = json.load(f)
        base = os.path.dirname(os.path.abspath(alvo))
        saida = []
        for entrada in corpus:
            if titulo and entrada.get("mod") != titulo:
                continue
            pasta = entrada.get("pasta") or entrada.get("folder")
            # O corpus guarda o `mod` e a PASTA; o caminho do `mods` e do lado de
            # fora (variavel de ambiente ou `--mods`).
            saida.append((entrada["mod"], pasta))
        return saida
    return [alvo]


def exemplo_texto(exemplo):
    ficheiro, endereco, palavra, texto = exemplo[:4]
    instr = texto.split("@")[0].strip()
    return "%s +0x%x  palavra 0x%s  objdump: %s" % (
        os.path.basename(ficheiro), endereco, palavra, instr)


def main():
    ap = argparse.ArgumentParser(description="Auditor diferencial do descodificador ARM.")
    ap.add_argument("alvo")
    ap.add_argument("--mods", default=os.environ.get("ZB2_MODS", ""),
                    help="directorio com as pastas de mods (para um corpus.json)")
    ap.add_argument("--sonda", default=os.path.join("build", "zb2_sonda_descodificador"))
    ap.add_argument("--objdump", default="arm-none-eabi-objdump")
    ap.add_argument("--json", default="")
    ap.add_argument("--limite", type=int, default=0)
    ap.add_argument("--titulo", default="")
    ap.add_argument("--exemplos", type=int, default=5)
    ap.add_argument("--top", type=int, default=25)
    ap.add_argument("--thumb", action="store_true")
    ap.add_argument("--verboso", action="store_true")
    args = ap.parse_args()

    if not os.path.exists(args.sonda):
        sys.stderr.write("sonda em falta: %s (construir o alvo zb2_sonda_descodificador)\n" % args.sonda)
        return 3
    if not os.path.exists(args.objdump) and not any(
            os.path.exists(os.path.join(p, args.objdump)) for p in os.environ.get("PATH", "").split(":")):
        sys.stderr.write("objdump em falta: %s\n" % args.objdump)
        return 3

    alvos = ficheiros_a_auditar(args.alvo, args.titulo or None)
    if not alvos:
        sys.stderr.write("nada para auditar em %s\n" % args.alvo)
        return 5

    relatorio = Relatorio(args.exemplos)
    temporario = tempfile.mktemp(suffix=".classes")
    for alvo in alvos:
        if isinstance(alvo, tuple):
            mod, pasta = alvo
            ficheiro = os.path.join(args.mods, pasta, mod + ".mod")
        else:
            ficheiro = alvo
        if not os.path.exists(ficheiro):
            sys.stderr.write("salto %s (nao existe)\n" % ficheiro)
            continue
        nomes, classes = correr_sonda(args.sonda, ficheiro, args.limite, args.thumb, temporario)
        linhas = correr_objdump(args.objdump, ficheiro, args.limite, args.thumb)
        relatorio.junta(ficheiro, nomes, classes, linhas, 2 if args.thumb else 4, args.verboso)
    if os.path.exists(temporario):
        os.unlink(temporario)

    silenciosos = sum(n for (n_our, n_obj), n in relatorio.pares.items() if "(recusado" not in n_our)
    recusados = relatorio.recusamos_sem_forma + relatorio.recusamos_nome_certo

    print("# AUDITOR DO DESCODIFICADOR -- %d ficheiros, %d palavras" % (len(relatorio.ficheiros), relatorio.total))
    print("# concorda: %d | executamos_outra (SILENCIOSO): %d | recusamos falta a forma: %d | recusamos nome certo: %d | nome_diferente: %d | objdump_nao_sabe: %d"
          % (relatorio.concordam, silenciosos, relatorio.recusamos_sem_forma, relatorio.recusamos_nome_certo,
             relatorio.classe_igual_mnemonico_diferente,
             sum(c.get("objdump_nao_sabe", 0) for c in relatorio.ficheiros.values())))
    anomalias = sum(c.get("anomalia_condicao_falsa", 0) + c.get("anomalia_instrumento", 0)
                    for c in relatorio.ficheiros.values())
    if anomalias:
        print("# ATENCAO: %d palavras que a sonda nao conseguiu classificar (condicao_falsa/nenhuma)" % anomalias)
    print()
    print("## (b) EXECUTAMOS OUTRA COISA -- o silencioso, por frequencia")
    for (nosso, obj), n in sorted(relatorio.pares.items(), key=lambda par: -par[1]):
        if "(recusado" in nosso:
            continue
        print("%9d  %-46s  vs objdump: %s" % (n, nosso, obj))
        for exemplo in relatorio.ex[(nosso, obj)]:
            print("           ex: %s" % exemplo_texto(exemplo))
    if not silenciosos:
        print("        ZERO")
    print()
    print("## (a) RECUSAMOS (falta a classe; o objdump sabe qual e)")
    for (nosso, obj), n in sorted(relatorio.pares.items(), key=lambda par: -par[1]):
        if "(recusado" not in nosso:
            continue
        print("%9d  %-46s  vs objdump: %s" % (n, nosso, obj))
        for exemplo in relatorio.ex[(nosso, obj)]:
            print("           ex: %s" % exemplo_texto(exemplo))
    if not recusados:
        print("        ZERO")
    print()
    print("## por que motivo recusamos")
    for (mnemonico, motivo), n in relatorio.motivos.most_common(args.top):
        print("%9d  %s: %s" % (n, mnemonico, motivo))
    print()
    print("## (c) nome_diferente -- mesmo efeito, dois nomes (top %d)" % args.top)
    for (n_our, n_obj), n in relatorio.nomes_apelidos.most_common(args.top):
        print("%9d  %-30s  vs objdump: %s" % (n, n_our, n_obj))
    if not relatorio.nomes_apelidos:
        print("        ZERO")
    print()
    print("## por ficheiro")
    for ficheiro, c in sorted(relatorio.ficheiros.items(), key=lambda kv: -kv[1].get("executamos_outra", 0)):
        print("%8d palavras  silencioso=%d recusamos=%d nome_diferente=%d concorda=%d dados=%d  %s"
              % (c["total"], c.get("executamos_outra", 0), c.get("recusamos", 0),
                 c.get("nome_diferente", 0), c.get("concorda", 0), c.get("objdump_nao_sabe", 0),
                 os.path.basename(ficheiro)))

    if args.json:
        with open(args.json, "w") as f:
            json.dump({
                "total": relatorio.total,
                "concorda": relatorio.concordam,
                "executamos_outra": silenciosos,
                "recusamos_falta_a_forma": relatorio.recusamos_sem_forma,
                "recusamos_nome_certo": relatorio.recusamos_nome_certo,
                "nome_diferente": relatorio.classe_igual_mnemonico_diferente,
                "pares": {" || ".join(k): v for k, v in relatorio.pares.items()},
                "motivos": {" || ".join(k): v for k, v in relatorio.motivos.items()},
                "ficheiros": {os.path.basename(k): v for k, v in relatorio.ficheiros.items()},
            }, f, indent=1, sort_keys=True)
        print("\n# relatorio escrito em %s" % args.json)

    return 1 if (silenciosos or relatorio.recusamos_sem_forma) else 0


if __name__ == "__main__":
    sys.exit(main())
