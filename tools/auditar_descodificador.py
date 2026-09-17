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
for m in ("and eor sub rsb add adc sbc rsc tst teq cmp cmn orr mov bic mvn neg lsl lsr asr ror").split():
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
    # O capstone escreve `trap` onde o binutils escreve `udf`: a MESMA
    # instrucao indefinida (medido: 0xDE00 do Thumb).
    "trap": "udf",
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
            texto = m.group(3)
            if thumb:
                # UMA INSTRUCAO DE 32 BITS VEM NUMA LINHA SO: o objdump imprime
                # `f000 f001 bl 0x...` -- as DUAS metades antes do mnemonico. Sem
                # isto, o auditor lia `f001` como se fosse o mnemonico e acusava
                # divergencia em TODO `bl` de 32 bits (medido no espaco inteiro).
                par = re.match(r"^([0-9a-f]{4})\s+(\S.*)$", texto)
                if par:
                    texto = par.group(2)
            saida.append((int(m.group(1), 16), m.group(2).lower(), texto))
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






# ===========================================================================
# MODO DE EFEITO: a comparacao passa a ser de VALORES, e nao de nomes.
# ===========================================================================
#
# PORQUE ISTO EXISTE. Nas ultimas horas apareceram QUATRO defeitos no
# descodificador e NENHUM deles mudava o NOME da instrucao:
#
#   * o `LSL #0` do Thumb lido como deslocamento de 32 (`35c09dc`);
#   * o `BL`/`BLX` de 32 bits do Thumb com o `imm10` a `<<13` -- a instrucao era
#     um `bl` e o ALVO estava errado (`reksio` 0xd5170 em vez de 0x36170,
#     `brainchallenge`, `rocketweb`) (`9a0bbdb`);
#   * o PC como operando no Thumb (`add rX, pc`) lido a seco (`9a0bbdb`);
#   * o regresso de uma saida servida a escrever o `lr` CRU no PC (`9a0bbdb`).
#
# O auditor de NOMES compara o que CASA: o mnemonico e a forma. Uma instrucao com
# o alvo deslocado de 0x30000 casa na mesma -- e faz o guest saltar para o meio
# de outra funcao. Este modo compara o EFEITO: o alvo resolvido, o endereco de
# cada carregamento PC-relativo, a lista e a escrita de volta dos blocos, e as
# contagens de deslocamento 0 e 32.
#
# DOIS ORACULOS INDEPENDENTES, UM SO MODELO. O `objdump` (binutils) e o
# `capstone` sao traduzidos, CADA UM POR SI, para os MESMOS CAMPOS (mnemonico,
# operandos, lista de registadores, escrita de volta, alvo). Quando os dois
# discordam num campo, o caso vai para `oraculos_discordam` e NAO se compara
# valor nenhum: um campo em que os oraculos nao concordam nao pode acusar o
# interpretador.
#
# O NOSSO LADO vem da sonda (`zb2_sonda_descodificador --efeito`), que executa a
# instrucao UMA vez numa caixa de areia e devolve o estado: PC depois,
# registadores mudados e as PALAVRAS DE MEMORIA mudadas. Nao ha aqui uma segunda
# descodificacao nossa.

M32 = 0xFFFFFFFF

REG_NOMES = {"sp": 13, "lr": 14, "pc": 15, "ip": 12, "fp": 11, "sl": 10, "sb": 9}

# Os mnemonics que este modo conhece. Fora desta lista a instrucao e
# `nao_modelado`, e conta-se: a cobertura declarada tem de ser honesta.
MNEMONICOS_EFEITO = set("""
b bl bx blx
ldr ldrb ldrh ldrsb ldrsh ldrd ldrt ldrbt ldrht ldrsbt ldrsht
str strb strh strd strt strbt strht
ldm ldmia ldmib ldmda ldmdb stm stmia stmib stmda stmdb push pop
mov mvn lsl lsr asr ror rrx
add adc sub sbc rsb rsc and orr eor bic cmp cmn tst teq neg mul mla adr nop
""".split())

ESCREVEM_DESTINO = set("mov mvn lsl lsr asr ror rrx add adc sub sbc rsb rsc and orr eor "
                       "bic neg mul mla adr smull umull smlal umlal".split())
# As multiplicacoes LONGAS escrevem DOIS registadores (Rd e Rd+1), como o `ldrd`.
ESCREVEM_DOIS = set("smull umull smlal umlal".split())
NAO_ESCREVEM_DESTINO = set("cmp cmn tst teq nop".split())
CARGAS = set("ldr ldrb ldrh ldrsb ldrsh ldrd ldrt ldrbt ldrht ldrsbt ldrsht".split())
GUARDAS = set("str strb strh strd strt strbt strht".split())
BLOCOS_CARREGA = set("pop ldm ldmia ldmib ldmda ldmdb ldmfd ldmea ldmfa ldmed".split())
BLOCOS_GUARDA = set("push stm stmia stmib stmda stmdb stmfd stmea stmfa stmed".split())
RAMOS = set("b bl bx blx".split())
DADOS = set("mov mvn lsl lsr asr ror rrX rrx add adc sub sbc rsb rsc and orr eor bic "
            "cmp cmn tst teq neg mul mla nop".split()) | {"adr"}
DESLOCADORES = set("lsl lsr asr ror rrx".split())


def partes(texto):
    """Parte operandos por virgulas, respeitando `[...]` e `{...}`."""
    saida, actual, nivel = [], "", 0
    for c in texto:
        if c in "[{":
            nivel += 1
        elif c in "]}":
            nivel -= 1
        if c == "," and nivel == 0:
            saida.append(actual.strip())
            actual = ""
        else:
            actual += c
    if actual.strip():
        saida.append(actual.strip())
    return saida


def registador_de(t):
    t = (t or "").strip().lower()
    if t in REG_NOMES:
        return REG_NOMES[t]
    if re.fullmatch(r"r\d{1,2}", t):
        v = int(t[1:])
        return v if v < 16 else None
    return None


# CONDICOES SINONIMAS. O binutils escreve `bcc`/`bcs` onde o capstone escreve
# `blo`/`bhs` -- a MESMA condicao (medido em 512 palavras do espaco Thumb, todas
# iguais a menos do nome).
CONDICOES_SINONIMAS = {"hs": "cs", "lo": "cc"}


def separar_mnemonico(m):
    """`ldmdbne` -> (`ldmdb`, `ne`, False). Le a CONDICAO e o `s` do nome.

    NAO traduz os apelidos: no modo de efeito o `rrx` e um `rrx` e o `push` e um
    `push` enquanto a lista nao estiver lida (o apelido `push -> stmdb` apagava a
    distincao de que o `push` tem a lista IMPLICITA, e o auditor lia a lista
    errada -- medido no `push {r1,r2,r3,lr}` do micro.bin)."""

    m = m.split(".")[0].lower()
    for sufixo in SUFIXOS:
        if m.endswith(sufixo) and m[:-len(sufixo)] in MNEMONICOS_EFEITO:
            tem_s = sufixo == "s" or (sufixo.startswith("s") and len(sufixo) > 1)
            cond = sufixo[1:] if (sufixo.startswith("s") and len(sufixo) > 1) else sufixo
            cond = CONDICOES_SINONIMAS.get(cond, cond)
            return m[:-len(sufixo)], (cond if cond else "al"), tem_s
    return m, "al", False


# --- a leitura do BINUTILS -------------------------------------------------
#
# A ORDEM DOS OPERANDOS E A DO TEXTO, e e a mesma do capstone: `ldr r1, [r0], #4`
# da [reg r1, mem, imm] nos dois. Sem isso o mero SITIO do operando de memoria
# (`[r0, #4]!` contra `[r0], #4`) fazia o binutils parecer pos-indexado onde nao
# era -- e uma divergencia inventada pelo instrumento esconde as verdadeiras.
def campos_do_objdump(texto, endereco, caixa, thumb):
    t = texto.split("@")[0].strip()
    if not t:
        return None
    pedacos = t.split(None, 1)
    base, cond, tem_s = separar_mnemonico(pedacos[0])
    corpo = pedacos[1].strip() if len(pedacos) > 1 else ""
    campos = {"mnemonico": base, "cond": cond, "tem_s": tem_s, "oraculo": "objdump",
              "ops": [], "lista": None, "escreve_base": False, "alvo": None,
              "escritos": None, "desloc": None, "pos_indexado": False,
              "texto": t, "endereco": endereco}

    # 1. A LISTA DE REGISTADORES DO BLOCO (`{r4, r5, lr}`).
    lista = re.search(r"\{([^}]*)\}", corpo)
    if lista:
        campos["lista"] = [registador_de(x) for x in partes(lista.group(1))]
    sem_lista = re.sub(r"\{[^}]*\}", "", corpo)

    # 2. OS OPERANDOS, POR ORDEM.
    pos_do_mem = None
    for i, pedaco in enumerate(partes(sem_lista)):
        if not pedaco:
            continue
        if pedaco.startswith("["):
            if pedaco.rstrip().endswith("!"):
                campos["escreve_base"] = True
            dentro = partes(pedaco.strip("[]!"))
            b = registador_de(dentro[0]) if dentro else None
            indice, disp, desloc_indice = None, 0, None
            if len(dentro) > 1:
                if re.fullmatch(r"#?-?(0x[0-9a-f]+|\d+)", dentro[1]):
                    disp = int(dentro[1].lstrip("#"), 0)
                else:
                    indice = registador_de(dentro[1])
            if len(dentro) > 2:
                d = re.fullmatch(r"(lsl|lsr|asr|ror)\s*#?(0x[0-9a-f]+|\d+)", dentro[2])
                if d:
                    desloc_indice = (d.group(1), int(d.group(2), 0), False)
            campos["ops"].append(("mem", b, indice, disp, desloc_indice))
            pos_do_mem = i
            continue
        if pedaco.lstrip("-").startswith("[") is False and pedaco.endswith("!"):
            campos["escreve_base"] = True
            pedaco = pedaco[:-1]
        d = re.fullmatch(r"(lsl|lsr|asr|ror|rrx)(\s*#?((0x[0-9a-f]+)|(\d+)))?", pedaco)
        if d is not None:
            if d.group(3) is None:
                campos["desloc"] = (d.group(1), None, True)
            else:
                campos["desloc"] = (d.group(1), int(d.group(3), 0), False)
            continue
        d = re.fullmatch(r"(lsl|lsr|asr|ror)\s+(\S+)", pedaco)
        if d is not None and registador_de(d.group(2)) is not None:
            campos["desloc"] = (d.group(1), registador_de(d.group(2)), True)
            continue
        r = registador_de(pedaco)
        if r is not None:
            campos["ops"].append(("reg", r, None))
            continue
        if re.fullmatch(r"#?-?(0x[0-9a-f]+|\d+)", pedaco):
            campos["ops"].append(("imm", int(pedaco.lstrip("#"), 0)))
            continue
        campos["ops"].append(("outro", pedaco, None))

    # 3. O ALVO DO RAMO, em coordenadas da CAIXA. O binutils imprime o alvo
    #    ABSOLUTO do ficheiro; a caixa de areia esta noutro endereco, e o que se
    #    compara e o DESLOCAMENTO.
    if base in RAMOS:
        m = re.search(r"(0x[0-9a-f]+)", corpo)
        if m:
            campos["alvo"] = caixa + (int(m.group(1), 16) - endereco)
        # O ALVO SAI DOS OPERANDOS: o endereco absoluto nao e um operando
        # comparavel entre os dois oraculos (um da o alvo, o outro o mesmo alvo
        # noutra base), e deixado dentro de `ops` daria `oraculos_discordam` em
        # TODOS os ramos.
        campos["ops"] = [o for o in campos["ops"] if o[0] != "imm"]

    # 4. O POS-INDEXADO: ha um operando DEPOIS do de memoria.
    if (campos["mnemonico"] in (CARGAS | GUARDAS) and pos_do_mem is not None
            and campos["ops"][-1][0] in ("imm", "reg") and len(campos["ops"]) > pos_do_mem + 1):
        campos["pos_indexado"] = True

    # OS DOIS LADOS PASSAM PELA MESMA CANONICALIZACAO. Sem isto o binutils ficava
    # com o `push` sem o apelido e com o `ldrd` de um registador so, e a
    # divergencia era do INSTRUMENTO (medido no micro.bin).
    campos = canonicalizar(campos, thumb)
    campos["escritos"] = escritos_do_binutils(campos)
    return campos


def escritos_do_binutils(campos):
    """Que registadores o BINUTILS diz que a instrucao escreve. O texto do
    objdump nao os lista: deduzem-se da forma -- e e essa deducao, independente
    do capstone, que faz a segunda opiniao.

    OS RAMOS SAO PELA FORMA, e nao pelo `regs_access` do capstone: medido, o
    `regs_access` do capstone NAO lista o PC num `b` directo e lista-o num `beq`
    -- a mesma familia com duas respostas. A regra (PC sempre; LR no `bl`/`blx`)
    e a do ARM ARM, e e a mesma nos dois lados."""
    m = campos["mnemonico"]
    canonico = campos.get("canonico", APELIDOS.get(m, m))
    empilha = campos.get("empilha", m in ("push", "pop"))
    ops = [o for o in campos["ops"] if o[0] in ("reg", "imm", "mem")]
    escreve = set()
    if m in RAMOS:
        escreve.add(15)
        if m in ("bl", "blx"):
            escreve.add(14)
    elif m == "push" or canonico in ("stm", "stmia", "stmib", "stmda", "stmdb"):
        if campos["escreve_base"] or empilha:
            escreve.add(ops[0][1] if (ops and ops[0][0] in ("mem", "reg")) else 13)
    elif m == "pop" or canonico in BLOCOS_CARREGA:
        if campos["escreve_base"] or empilha:
            escreve.add(ops[0][1] if (ops and ops[0][0] in ("mem", "reg")) else 13)
        if campos["lista"]:
            escreve |= {r for r in campos["lista"] if r is not None}
    elif m in CARGAS:
        if ops:
            escreve.add(ops[0][1])
            if canonico in ESCREVEM_DOIS and len(ops) > 1 and ops[1][0] == "reg":
                escreve.add(ops[1][1])
            if m == "ldrd" and len(ops) > 1 and ops[1][0] == "reg":
                escreve.add(ops[1][1])
        if campos["escreve_base"] or campos["pos_indexado"]:
            mem = [o for o in ops if o[0] == "mem"]
            if mem:
                escreve.add(mem[0][1])
    elif m in GUARDAS:
        if campos["escreve_base"] or campos["pos_indexado"]:
            mem = [o for o in ops if o[0] == "mem"]
            if mem:
                escreve.add(mem[0][1])
    else:
        if canonico in ESCREVEM_DESTINO and ops:
            escreve.add(ops[0][1])
            if canonico in ESCREVEM_DOIS and len(ops) > 1:
                escreve.add((ops[0][1] + 1) % 16)
    return escreve


# --- a leitura do CAPSTONE (o segundo oraculo, independente) ---------------
def capstone_ferramentas():
    try:
        import capstone
    except Exception:
        return None
    registadores = {}
    for i in range(13):
        registadores[getattr(capstone.arm, "ARM_REG_R%d" % i)] = i
    registadores[capstone.arm.ARM_REG_SP] = 13
    registadores[capstone.arm.ARM_REG_LR] = 14
    registadores[capstone.arm.ARM_REG_PC] = 15
    # OS DESLOCAMENTOS POR REGISTADOR tem constantes proprias no capstone
    # (`ARM_SFT_LSR_REG` e companhia) e o campo `value` e um ID DE REGISTADOR, e
    # nao uma contagem (medido: `r8, lsr fp` -> shift (8, 77), e 77 = ARM_REG_R11).
    deslocamentos = {getattr(capstone.arm, "ARM_SFT_ASR_REG"): "asr_reg",
                     getattr(capstone.arm, "ARM_SFT_LSL_REG"): "lsl_reg",
                     getattr(capstone.arm, "ARM_SFT_LSR_REG"): "lsr_reg",
                     getattr(capstone.arm, "ARM_SFT_ROR_REG"): "ror_reg",
                     getattr(capstone.arm, "ARM_SFT_ASR"): "asr",
                     getattr(capstone.arm, "ARM_SFT_LSL"): "lsl",
                     getattr(capstone.arm, "ARM_SFT_LSR"): "lsr",
                     getattr(capstone.arm, "ARM_SFT_ROR"): "ror",
                     getattr(capstone.arm, "ARM_SFT_RRX"): "rrx"}
    return capstone, registadores, deslocamentos


def campos_do_capstone(cs, registadores, deslocamentos, palavra, tamanho, caixa, thumb):
    md = cs.Cs(cs.CS_ARCH_ARM, cs.CS_MODE_THUMB if thumb else cs.CS_MODE_ARM)
    md.detail = True
    campos = None
    for i in md.disasm(int(palavra).to_bytes(4, "little")[:tamanho], caixa):
        base, cond, tem_s = separar_mnemonico(i.mnemonic)
        campos = {"mnemonico": base, "cond": cond, "tem_s": tem_s, "oraculo": "capstone",
                  "ops": [], "lista": None, "escreve_base": bool(getattr(i, "writeback", False)),
                  "alvo": None, "escritos": set(), "desloc": None,
                  "pos_indexado": bool(getattr(i, "post_index", False)),
                  "texto": (i.mnemonic + (" " + i.op_str if i.op_str else "")).strip(),
                  "endereco": caixa, "tamanho_real": i.size}
        for o in i.operands:
            if o.type == cs.arm.ARM_OP_REG:
                campos["ops"].append(("reg", registadores.get(o.reg), None))
            elif o.type == cs.arm.ARM_OP_IMM:
                campos["ops"].append(("imm", o.imm))
            elif o.type == cs.arm.ARM_OP_MEM:
                desloc_indice = None
                sh = getattr(o, "shift", None)
                if sh is not None and getattr(sh, "type", 0) in deslocamentos:
                    desloc_indice = (deslocamentos[sh.type], sh.value, False)
                indice = registadores.get(o.mem.index) if o.mem.index != 0 else None
                campos["ops"].append(("mem", registadores.get(o.mem.base) if o.mem.base != 0 else None,
                                      indice, o.mem.disp, desloc_indice))
            if o.type == cs.arm.ARM_OP_REG:
                sh = getattr(o, "shift", None)
                if sh is not None and getattr(sh, "type", 0) in deslocamentos:
                    # `_REG` -> o valor e um registador; a forma curta -> a contagem.
                    nome_do_tipo = deslocamentos[sh.type]
                    por_registo = nome_do_tipo.endswith("_reg")
                    campos["desloc"] = (nome_do_tipo.replace("_reg", ""),
                                        registadores.get(sh.value, sh.value) if por_registo
                                        else sh.value, por_registo)
        if base in RAMOS:
            for o in i.operands:
                if o.type == cs.arm.ARM_OP_IMM:
                    campos["alvo"] = o.imm
        for r in (i.regs_access()[1] if hasattr(i, "regs_access") else []):
            if r in registadores:
                campos["escritos"].add(registadores[r])
        if base in RAMOS:
            campos["escritos"] = {15} | ({14} if base in ("bl", "blx") else set())
        break
    if campos is not None:
        campos = canonicalizar(campos, thumb)
    return campos


def canonico_do_nome(m):
    """O nome para decidir a FAMILIA, sem os apelidos que trocam o mnemonic
    (`rrx -> mov`, `lsl -> mov`): aqui o que se pergunta e "e uma operacao de
    dados?", e um `rrx` e."""
    return {k: v for k, v in APELIDOS.items()}.get(m, m) if m in ("push", "pop", "stmfd", "ldmfd") else m


def canonicalizar(campos, thumb):
    # O NOME CRU FICA, e o canonico ao lado. O `push` e o `stmdb` sao a MESMA
    # instrucao para o auditor (o binutils e o capstone tambem os trocam), mas a
    # lista implicita do `push` tem de ser lida com o nome cru: foi com o apelido
    # ja aplicado que o auditor perdeu a lista do `push {r1,r2,r3,lr}` (medido).
    """Normaliza a FORMA dos operandos, para que os dois oraculos produzam
    exactamente os mesmos campos. Cada regra tem a medicao que a justifica."""
    m = campos["mnemonico"]
    ops = [o for o in campos["ops"] if o[0] in ("reg", "imm", "mem")]

    # 1. O `ldrd`/`strd` do binutils imprime UM registador (`ldrd r0, [r1]`); o
    #    segundo e `rd+1` (medido: 0xE1C002D0 = `ldrd r0, [r0, #32]` no binutils
    #    e `ldrd r0, r1, [r0, #0x20]` no capstone).
    if m in ("ldrd", "strd") and len(ops) == 2 and ops[0][0] == "reg" and ops[1][0] == "mem":
        ops = [ops[0], ("reg", (ops[0][1] + 1) % 16, None), ops[1]]

    # 2. O `adr` do binutils e um `add rd, pc, #imm` (medido: `adr r0, #4` do
    #    capstone e `add r0, pc, #4 @ (adr r0, 0x8)` no binutils). O alvo absoluto
    #    do binutils nao serve: fica o `add`.
    if m == "adr":
        if ops and ops[0][0] == "reg" and len(ops) == 2 and ops[1][0] == "imm":
            ops = [ops[0], ("reg", 15, None), ops[1]]
            campos["mnemonico"] = m = "add"

    # 2a-bis. A FORMA DE DOIS OPERANDOS DO THUMB (`add r0, sp`): o capstone
    #     escreve-a por extenso (`add r0, sp, r0`) e o binutils abrevia-a. Fica a
    #     forma curta, que e a que diz que SO os dois registadores baixos mexem
    #     nas bandeiras.
    if (thumb and m in ("add", "sub", "cmp", "mov") and len(ops) == 3
            and ops[1][0] == "reg" and ops[2] == ops[0]
            and (ops[0][1] > 7 or ops[1][1] > 7)):
        # SO COM UM REGISTADOR ALTO ENVOLVIDO. A forma de dois operandos do
        # formato 2 do Thumb (`add r0, sp`) tem um registador alto por definicao,
        # e o binutils imprime-a curta e o capstone por extenso. O formato 1
        # (`00011 00 op Rm Rn Rd`) tambem pode ter `ops[2] == ops[0]` (ex.:
        # `subs r0, r1, r0` do espaco) e NAO e esta forma: colapsa-lo punha a
        # conta ao contrario (192 divergencias falsas medidas).
        ops = ops[:2]

    # 2a. O IMEDIATO RODADO DO ARM. Os dois oraculos escrevem-no em DOIS
    #     operandos (`add r0, sp, #128, #28`) e ele e UM SO: o valor e `imm` ROR
    #     `2*rot` (o binutils imprime `#128, 28` sem o segundo `#`). Sem esta
    #     regra o auditor comparava o imediato POR RODAR -- e acusava 6 796
    #     `add rX, sp, #imm` do `bjt.mod` como divergencia NOSSA quando quem
    #     estava errado era a leitura do instrumento.
    if (len(ops) >= 2 and ops[-1][0] == "imm" and ops[-2][0] == "imm"
            and 0 <= ops[-1][1] <= 31 and canonico_do_nome(m) in DADOS):
        # A ROTACAO VEM EM BITS, e nao no campo de 4 bits: o binutils e o
        # capstone imprimem `add r0, sp, #128, #28` (o campo codifica 14). Ler o
        # 28 como o campo dava um imediato por rodar (6 796 casos no `bjt.mod`).
        rotacao = ops[-1][1] % 32
        imediato = ops[-2][1]
        valor = imediato if rotacao == 0 else (
            ((imediato >> rotacao) | ((imediato << (32 - rotacao)) & M32)) & M32)
        ops = ops[:-2] + [("imm", valor)]

    # 2b-bis. O `nop` DO THUMB E O `mov r8, r8` (o binutils abrevia-o, o capstone
    #     escreve-o por extenso -- medido no 0x46C0 do espaco).
    if m == "mov" and len(ops) == 2 and ops[0] == ops[1] and ops[0][0] == "reg" and ops[0][1] == 8:
        campos["mnemonico"] = m = "nop"
        campos["canonico"] = "nop"

    # 2b. O `neg` E UM `rsb rd, rd, #0` (o capstone escreve-o por extenso, o
    #     binutils abrevia): sem esta regra os dois oraculos discordavam no NOME
    #     de 2 578 palavras do espaco Thumb.
    if m == "rsb" and len(ops) == 3 and ops[2][0] == "imm" and ops[2][1] == 0:
        campos["mnemonico"] = m = "neg"
        ops = ops[:2]

    # 2c. O `muls Rd, Rm` DO THUMB e o `mul Rd, Rm, Rd` do ARM: o binutils
    #     abrevia-o a dois operandos e o capstone escreve-o por extenso.
    if m == "mul" and len(ops) == 2:
        ops = [ops[0], ops[1], ops[0]]

    # 3. AS LISTAS DOS BLOCOS. O binutils escreve a lista em `{...}` e o capstone
    #    da-a como operandos soltos: a lista sai dos operandos e o UNICO operando
    #    que sobra e a BASE (que so existe no `ldm`/`stm`).
    if m in RAMOS:
        # O ALVO SAI DOS OPERANDOS: um oraculo da-o como numero e o outro como
        # deslocamento, e compara-lo dentro de `ops` acusava TODOS os ramos.
        ops = [o for o in ops if o[0] != "imm"]

    # 2d. O `push {rX}`/`pop {rX}` DE UM REGISTADOR SO e um `str`/`ldr`
    #     (medido: `nfs.mod` 0x15c9c `e52d4004` = `push {r4}` no binutils e
    #     `str r4, [sp, #-4]!` no capstone; a nossa sonda diz `str`, e tem razao --
    #     a codificacao e a da transferencia simples). A regra ja existia no
    #     auditor de NOMES (`apelido_de_um_registrador`); faltava aqui, onde ela
    #     decide a LISTA e a ESCRITA DE VOLTA.
    # O CAPSTONE nao escreve a lista em `{...}`: ela esta nos operandos, e por isso
    # a lista ainda e `None` neste ponto para ele.
    if m in ("push", "pop") and campos["lista"] is None:
        campos["lista"] = [o[1] for o in ops if o[0] == "reg"]
    if m in ("push", "pop") and campos["lista"] is not None and len(campos["lista"]) == 1:
        registador = campos["lista"][0]
        carrega = m == "pop"
        campos["mnemonico"] = m = "ldr" if carrega else "str"
        campos["canonico"] = m
        campos["empilha"] = False
        campos["escreve_base"] = True
        campos["pos_indexado"] = carrega
        campos["lista"] = None
        ops = [("reg", registador, None), ("mem", 13, None, 0 if carrega else -4, None)]
        if carrega:
            ops.append(("imm", 4))

    if m in (BLOCOS_CARREGA | BLOCOS_GUARDA):
        if m in ("push", "pop"):
            if campos["lista"] is None:
                campos["lista"] = [o[1] for o in ops if o[0] == "reg"]
            ops = []
        elif campos["oraculo"] == "capstone":
            ops = ops[:1]
            campos["lista"] = [o[1] for o in campos["ops"] if o[0] == "reg"][1:]
        else:
            ops = ops[:1]

    # 4. O DESLOCAMENTO DAS FORMAS DO THUMB E DO ARM, numa so representacao:
    #    (`tipo`, quantidade ou registador, por_registo). O binutils escreve-o no
    #    operando 3 (`lsl r0, r1, #1`, `lsrs r0, r1, #0x20`); o capstone do ARM
    #    agarra-o ao registador de origem.
    if m in DESLOCADORES or m == "rrx":
        if campos["desloc"] is None:
            if len(ops) == 3 and ops[2][0] == "imm":
                campos["desloc"] = (m, ops[2][1], False)
                ops = ops[:2]
            elif len(ops) == 3 and ops[2][0] == "reg":
                campos["desloc"] = (m, ops[2][1], True)
                ops = ops[:2]
            elif len(ops) == 2 and not thumb:
                campos["desloc"] = (m, None, True)
            elif len(ops) == 2 and thumb:
                # `lsls r0, r1`: o valor deslocado e o PROPRIO destino e a
                # quantidade esta no segundo registador (formato 4 do Thumb).
                campos["desloc"] = (m, ops[1][1], True)
                ops = [ops[0], ops[0]]
        elif len(ops) == 2 and not thumb:
            pass
    campos["ops"] = ops
    campos["canonico"] = APELIDOS.get(m, m)
    campos["empilha"] = m in ("push", "pop")
    if campos["pos_indexado"]:
        # O POS-INDEXADO ESCREVE SEMPRE NA BASE: o `!` nao se imprime porque e
        # implicito na forma (`str r5, [r6], #4`), e o `writeback` do capstone
        # diz que sim enquanto o texto do binutils nao tem `!` nenhum.
        campos["escreve_base"] = True
    if campos["empilha"]:
        # O `push`/`pop` ESCREVE SEMPRE no SP -- o `!` e implicito, e o binutils
        # nao o imprime. O capstone tambem nao o poe em `writeback`; a nossa
        # sonda sim (e o interpretador tambem). Sem esta linha os dois oraculos
        # discordavam na escrita de volta em 121 palavras do corpus.
        campos["escreve_base"] = True
    if campos["oraculo"] == "objdump":
        campos["escritos"] = escritos_do_binutils(campos)
    return campos


LARGURAS = {"ldr": (4, False), "ldrt": (4, False), "ldrb": (1, False), "ldrbt": (1, False),
            "ldrh": (2, False), "ldrht": (2, False), "ldrsb": (1, True), "ldrsbt": (1, True),
            "ldrsh": (2, True), "ldrsht": (2, True), "ldrd": (8, False),
            "str": (4, False), "strt": (4, False), "strb": (1, False), "strbt": (1, False),
            "strh": (2, False), "strht": (2, False), "strd": (8, False)}


def condicao_verdadeira(cond, n, z, c, v):
    if cond == "al":
        return True
    if cond == "nv":
        return True
    return {"eq": z, "ne": not z, "cs": c, "hs": c, "cc": not c, "lo": not c,
            "mi": n, "pl": not n, "vs": v, "vc": not v, "hi": c and not z,
            "ls": (not c) or z, "ge": n == v, "lt": n != v,
            "gt": (not z) and (n == v), "le": z or (n != v)}.get(cond, True)


def deslocar(v, tipo, n, c_in):
    """O deslocamento do ARM ARM. As contagens 0 e 32 estao aqui de proposito: o
    defeito que esta frente persegue (`LSL #0` lido como 32) e exactamente uma
    troca entre as duas."""
    if tipo == "lsl":
        if n == 0:
            return v, c_in
        if n < 32:
            return (v << n) & M32, ((v >> (32 - n)) & 1) != 0
        if n == 32:
            return 0, (v & 1) != 0
        return 0, False
    if tipo == "lsr":
        if n == 0:
            return v, c_in
        if n < 32:
            return v >> n, ((v >> (n - 1)) & 1) != 0
        if n == 32:
            return 0, (v >> 31) != 0
        return 0, False
    if tipo == "asr":
        if n == 0:
            return v, c_in
        sinal = (v >> 31) != 0
        if n < 32:
            r = (v >> n) | ((M32 << (32 - n)) & M32) if sinal else (v >> n)
            return r & M32, ((v >> (n - 1)) & 1) != 0
        return (M32 if sinal else 0), sinal
    if tipo == "ror":
        if n == 0:
            return v, c_in
        q = n & 31
        if q == 0:
            return v, (v >> 31) != 0
        return ((v >> q) | ((v << (32 - q)) & M32)) & M32, ((v >> (q - 1)) & 1) != 0
    if tipo == "rrx":
        return ((v >> 1) | (0x80000000 if c_in else 0)) & M32, (v & 1) != 0
    return None, c_in


def valor_do_registo(est, r, thumb):
    if r == 15:
        return est["caixa"] + (4 if thumb else 8)
    return est["regs"][r]


def byte_em(a, est):
    if est["caixa"] <= a < est["caixa"] + est["tamanho"]:
        return est["bytes"][a - est["caixa"]]
    ini, fim = est["padrao"][2], est["padrao"][3]
    if ini <= a < fim:
        w = a & ~3
        p = (((w * est["padrao"][0]) & M32) ^ est["padrao"][1])
        return (p >> (8 * (a & 3))) & 0xFF
    return 0


def palavra_em(a, est):
    a &= ~3
    return sum(byte_em(a + i, est) << (8 * i) for i in range(4))


def ler_memoria(end, largura, est):
    v = sum(byte_em(end + i, est) << (8 * i) for i in range(largura))
    return v


def operando2(campos, est, ops, thumb, c_in, indice=1):
    """O operando 2 do ARM/Thumb: o valor e o carry que sai do deslocador.

    O INDICE E 2 NA FORMA DE TRES OPERANDOS (`adds r0, r1, r0`), e nao 1. Com o
    indice fixo em 1, o segundo operando lia-se duas vezes (a soma dava o dobro do
    primeiro) -- e foram 1 920 divergencias FALSAS no espaco Thumb inteiro antes
    de se ver que o defeito era do MODELO, e nao do interpretador."""
    desloc = campos["desloc"]
    if len(ops) <= indice:
        return None, c_in
    if ops[indice][0] == "imm":
        v = ops[indice][1]
    elif ops[indice][0] == "reg":
        v = valor_do_registo(est, ops[indice][1], thumb)
    else:
        return None, c_in
    if desloc is None:
        return v, c_in
    tipo, quantidade, por_registo = desloc
    if tipo == "rrx":
        return deslocar(v, "rrx", 1, c_in)
    if por_registo:
        n = est["regs"][quantidade] & (0xFF if not thumb else 0xFF)
        if n == 0 and tipo in ("lsr", "asr"):
            n = 32          # o zero do LSR/ASR por registador e 32, o do LSL e "nao deslocar"
        elif n == 0:
            return v, c_in
    else:
        n = quantidade
    return deslocar(v, tipo, n, c_in)


def modelo(campos, est, thumb):
    """O EFEITO que os dois oraculos, ja de acordo nos campos, dizem que vai
    acontecer. `None` quando a instrucao nao esta modelada."""
    m = campos["mnemonico"]
    ops = campos["ops"]
    depois = dict(enumerate(est["regs"]))
    depois[15] = est["caixa"]
    memoria = {}
    bandeiras = None
    modo = thumb

    c_in = est["c"]

    def guarda(r, v):
        depois[r] = v & M32

    def guarda_memoria(end, valor, largura):
        # A COMPOSICAO E ACUMULADA, e nao byte a byte sobre o padrao: reler a
        # palavra em cada byte apagava o byte anterior (medido: o `push` de quatro
        # registadores dava uma palavra do padrao com o byte de cima a zero).
        por_palavra = {}
        for i in range(largura):
            a = (end + i) & M32
            w = a & ~3
            novo = por_palavra.get(w, palavra_em(w, est))
            deslocamento = 8 * (a & 3)
            novo = ((novo & ~(0xFF << deslocamento))
                    | (((valor >> (8 * i)) & 0xFF) << deslocamento)) & M32
            por_palavra[w] = novo
            memoria[w] = novo

    def endereco_do_operando(mem, base_ja, indice_ja):
        b, indice, disp, desloc_ind = mem[1], mem[2], mem[3], mem[4]
        if b is None:
            return None
        if b == 15:
            if thumb:
                base = (est["caixa"] + 4) & ~3      # o PC literal do Thumb alinha a 4
            else:
                base = est["caixa"] + 8
        else:
            base = est["regs"][b]
        v = base
        if indice is not None:
            vi = est["regs"][indice]
            if desloc_ind is not None:
                vi, _ = deslocar(vi, desloc_ind[0], desloc_ind[1], c_in)
            v = (v + vi) & M32
        return (v + disp) & M32, base

    # --- RAMOS -------------------------------------------------------------
    if m in RAMOS:
        alvo = campos["alvo"]
        if alvo is None:
            rm = next((o[1] for o in ops if o[0] == "reg"), None)
            if rm is None:
                return None
            alvo = valor_do_registo(est, rm, thumb)
        if not condicao_verdadeira(campos["cond"], est["n"], est["z"], est["c"], est["v"]):
            guarda(15, est["caixa"] + est["tamanho"])
            return {"regs": depois, "mem": memoria, "flags": None, "modo": modo}
        if m == "b":
            guarda(15, alvo)
        elif m == "bl":
            guarda(14, est["caixa"] + est["tamanho"] + (1 if thumb else 0))
            guarda(15, alvo & ~1 if thumb else alvo)
        elif m == "bx":
            modo = bool(alvo & 1)
            guarda(15, alvo & ~1)
        else:   # blx
            if campos["alvo"] is not None:      # imediato
                if thumb:
                    guarda(14, est["caixa"] + est["tamanho"] + 1)
                    guarda(15, alvo & ~2)
                    modo = False
                else:
                    guarda(14, est["caixa"] + 4)
                    guarda(15, alvo & ~3)
                    modo = True
            else:
                guarda(14, (est["caixa"] + (2 if thumb else 4)) + (1 if thumb else 0))
                modo = bool(alvo & 1)
                guarda(15, alvo & ~1)
        return {"regs": depois, "mem": memoria, "flags": None, "modo": modo}

    # --- CARREGAMENTOS E DEPOSITOS -----------------------------------------
    if m in CARGAS or m in GUARDAS:
        registos = [o for o in ops if o[0] == "reg"]
        mems = [o for o in ops if o[0] == "mem"]
        if not mems or not registos:
            return None
        mem = mems[0]
        largura, sinal = LARGURAS[m]
        b_reg = mem[1]
        if b_reg is None:
            return None
        if b_reg == 15:
            base = ((est["caixa"] + 4) & ~3) if thumb else (est["caixa"] + 8)
        else:
            base = est["regs"][b_reg]
        passo = mem[3]
        if mem[2] is not None:
            vi = est["regs"][mem[2]]
            if mem[4] is not None:
                vi, _ = deslocar(vi, mem[4][0], mem[4][1], c_in)
            passo = (passo + vi) & M32
        pos_indexado = campos["pos_indexado"]
        if pos_indexado:
            # O POS-INDEXADO: o acesso e na BASE e a escrita de volta soma o
            # deslocamento a seguir. O proprio objdump distingue as duas formas
            # pelo sitio do `!` (`[r1, #4]!` contra `[r1], #4`).
            extra = ops[1] if len(ops) > 1 and ops[1][0] in ("imm", "reg") else None
            if extra is None:
                return None
            if extra[0] == "reg":
                passo_pos = valor_do_registo(est, extra[1], thumb)
            else:
                passo_pos = extra[1]
            end = base
        else:
            end = (base + passo) & M32
            passo_pos = passo
        if m in CARGAS:
            if largura == 8:
                if len(registos) < 2:
                    return None
                v_baixo = ler_memoria(end, 4, est)
                v_alto = ler_memoria(end + 4, 4, est)
                for (registo, valor) in ((registos[0][1], v_baixo), (registos[1][1], v_alto)):
                    if registo == 15:
                        modo = bool(valor & 1)
                        guarda(15, valor & ~1)
                    else:
                        guarda(registo, valor)
            else:
                v = ler_memoria(end, largura, est)
                if largura == 4:
                    # A LEITURA NAO ALINHADA do ARM: o valor roda pelo
                    # desalinhamento (`TransferenciaSimples` faz `ROR` de `desal*8`).
                    desal = end & 3
                    if desal != 0:
                        v = ((v >> (desal * 8)) | ((v << (32 - desal * 8)) & M32)) & M32
                if sinal:
                    if largura == 1 and (v & 0x80):
                        v |= 0xFFFFFF00
                    if largura == 2 and (v & 0x8000):
                        v |= 0xFFFF0000
                registo = registos[0][1]
                if registo == 15 and largura == 4:
                    modo = bool(v & 1)
                    guarda(15, v & ~1)
                else:
                    guarda(registo, v)
        else:
            fontes = [o[1] for o in registos]
            if largura == 8:
                if len(fontes) < 2:
                    return None
                guarda_memoria(end, valor_do_registo(est, fontes[0], thumb), 4)
                guarda_memoria(end + 4, valor_do_registo(est, fontes[1], thumb), 4)
            else:
                guarda_memoria(end, valor_do_registo(est, fontes[0], thumb), largura)
        if campos["escreve_base"] or pos_indexado:
            if pos_indexado:
                guarda(b_reg, (base + passo_pos) & M32)
            else:
                guarda(b_reg, end)
        if b_reg != 15 and not (m in GUARDAS and False):
            pass
        if not (m in CARGAS and registos[0][1] == 15):
            guarda(15, est["caixa"] + est["tamanho"])
        return {"regs": depois, "mem": memoria, "flags": None, "modo": modo}

    # --- BLOCOS (push/pop/ldm/stm) -----------------------------------------
    #
    # A ORDEM DOS EFEITOS NAO E INDIFERENTE: com o registador de BASE dentro da
    # lista de um `ldm`, O VALOR CARREGADO MANDA (foi um defeito medido: a
    # escrita de volta vinha depois e apagava-o em silencio). Por isso o modelo
    # carrega para um dicionario a parte e so no fim escreve.
    canonico = campos.get("canonico", m)
    if canonico in BLOCOS_CARREGA or canonico in BLOCOS_GUARDA:
        lista = campos["lista"]
        if not lista or any(r is None for r in lista):
            return None
        carrega = canonico in BLOCOS_CARREGA
        empilha = campos.get("empilha", m in ("push", "pop"))
        base_reg = 13
        if not empilha:
            if not ops or ops[0][0] != "reg":
                return None
            base_reg = ops[0][1]
        base = est["regs"][base_reg]
        n = len(lista)
        if m == "push":
            primeiro = base - 4 * n
            escreve_volta = base - 4 * n
        elif m == "pop":
            primeiro = base
            escreve_volta = base + 4 * n
        else:
            forma = canonico[3:] or "ia"
            if forma not in ("ia", "ib", "da", "db"):
                return None
            primeiro = {"ia": base, "ib": base + 4, "da": base - 4 * n + 4,
                        "db": base - 4 * n}[forma]
            escreve_volta = base + 4 * n if forma in ("ia", "ib") else base - 4 * n
        carregados = {}
        end = primeiro
        for r in lista:
            if carrega:
                carregados[r] = ler_memoria(end, 4, est)
            else:
                guarda_memoria(end, valor_do_registo(est, r, thumb), 4)
            end += 4
        if campos["escreve_base"] or empilha:
            if not (carrega and base_reg in lista):
                guarda(base_reg, escreve_volta)
        for r, v in carregados.items():
            if r == 15:
                modo = bool(v & 1)
                guarda(15, v & ~1)
            else:
                guarda(r, v)
        if not (carrega and 15 in lista):
            guarda(15, est["caixa"] + est["tamanho"])
        return {"regs": depois, "mem": memoria, "flags": None, "modo": modo}

    # --- DADOS (a ALU, o MOV com deslocamento, o ADD com o PC) -------------
    #
    # O PC DESTA FAMILIA tem duas saidas, e as duas existem no corpus: uma
    # instrucao de dados que ESCREVE o PC e um salto (e quem manda e ela -- a
    # guarda do `DespacharDadosProcessados`); as outras avancam um passo. Escrever
    # `pc + passso` por cima do destino foi um defeito medido (`mov pc,lr`: 2 727
    # vezes em 43 dos 62 modulos).
    if m == "nop":
        guarda(15, est["caixa"] + est["tamanho"])
        return {"regs": depois, "mem": memoria, "flags": None, "modo": modo}
    if m not in DADOS or not ops:
        return None
    rd = ops[0][1] if ops[0][0] == "reg" else None
    # O NOME CRU MANDA AQUI, e o `canonico` so serve para os blocos: o
    # `canonico` do `lsl` e `mov` (sao o mesmo codigo no ARM), e usar o canonico
    # mandava o deslocamento pelo ramo do `mov` de dois operandos -- o auditor
    # acusava entao o `lsls r1, r0, #4` e o `lsrs r0, r1, #32` como divergencia
    # NOSSA, quando quem estava errado era o modelo (medido no microt.bin).
    canonico = campos.get("canonico", m)
    bandeiras = None

    def fecha():
        """Fecha a instrucao com o PC da familia, e aplica a REGRA DAS BANDEIRAS.

        NO ARM so a flag `S` (ou um `cmp`/`cmn`/`tst`/`teq`, que a tem implicita)
        escreve as bandeiras; no Thumb quase todas escrevem. Sem esta regra o
        modelo acusava 3 176 bandeiras que o interpretador NAO mexe -- e estava
        certo a nao mexer: um `asr r0, r0, #16` sem `s` nao poe bandeira
        nenhuma (`alpineracerex.mod` 0x1c38c, 127 casos)."""
        nonlocal bandeiras
        if not (campos["tem_s"] or thumb) and m not in NAO_ESCREVEM_DESTINO:
            bandeiras = None
        if rd == 15 and depois[15] != est["caixa"]:
            # NO ARM a escrita do PC por uma instrucao de dados E um salto com
            # TROCA DE ESTADO (o bit 0 escolhe, como um `bx` -- ARM ARM A2.3.1),
            # e no THUMB-1 nao: o `add pc, rX`/`mov pc, rX` do formato 2 ignora o
            # bit 0 e NAO muda de estado.
            novo_modo = bool(depois[15] & 1) if not thumb else modo
            depois[15] &= ~1
            return {"regs": depois, "mem": memoria, "flags": bandeiras, "modo": novo_modo}
        guarda(15, est["caixa"] + est["tamanho"])
        return {"regs": depois, "mem": memoria, "flags": bandeiras, "modo": modo}

    if m in ("mul", "mla"):
        registos = [o[1] for o in ops if o[0] == "reg"]
        if len(registos) < 3 or (canonico == "mla" and len(registos) < 4):
            return None
        r = (est["regs"][registos[1]] * est["regs"][registos[2]]) & M32
        if m == "mla":
            r = (r + est["regs"][registos[3]]) & M32
        guarda(registos[0], r)
        bandeiras = ((r >> 31) != 0, r == 0, c_in, est["v"])
        return fecha()

    if m in DESLOCADORES or m == "rrx":
        if len(ops) < 2 or ops[1][0] != "reg":
            return None
        v = valor_do_registo(est, ops[1][1], thumb)
        desloc = campos["desloc"]
        if m == "rrx":
            r, c_novo = deslocar(v, "rrx", 1, c_in)
        elif desloc is None:
            r, c_novo = v, c_in
        else:
            tipo, quantidade, por_registo = desloc
            if por_registo:
                n = est["regs"][quantidade] & 0xFF
                if n == 0 and tipo in ("lsr", "asr"):
                    n = 32      # o zero do LSR/ASR por registador e 32; o do LSL e "nao deslocar"
                elif n == 0:
                    n = None
                r, c_novo = (v, c_in) if n is None else deslocar(v, tipo, n, c_in)
            else:
                r, c_novo = deslocar(v, tipo, quantidade, c_in)
        guarda(rd, r)
        bandeiras = ((r >> 31) != 0, r == 0, c_novo, est["v"])
        return fecha()

    # As restantes operacoes de dados: um operando 2 (com deslocamento), ou a
    # forma de DOIS operandos do Thumb (`add rX, pc`, formato 2, e o `add rd, #imm`
    # do formato 3).
    if len(ops) == 2 and thumb and m in ("add", "sub", "cmp", "mov"):
        # O PC COMO OPERANDO VALE `endereco + 4` NO THUMB (e nao o endereco a
        # seco). Ler `est["regs"][15]` era ler a CAIXA de areia, e acusava o
        # `add pc, rX`/`cmp pc, rX` com 4 bytes de erro (medido no espaco Thumb).
        a = valor_do_registo(est, ops[0][1], thumb)
        b = valor_do_registo(est, ops[1][1], thumb) if ops[1][0] == "reg" else (
            ops[1][1] if ops[1][0] == "imm" else None)
        if b is None:
            return None
        alto = (ops[0][1] > 7) or (ops[1][0] == "reg" and ops[1][1] > 7)
        imediato = ops[1][0] == "imm"
        r = None
        if m == "add":
            r = (a + b) & M32
            if (imediato and ops[0][1] != 13) or (not imediato and not alto):
                bandeiras = ((r >> 31) != 0, r == 0, (a + b) > M32,
                             ((a >> 31) == (b >> 31)) and ((r >> 31) != (a >> 31)))
        elif m == "sub":
            r = (a - b) & M32
            if (imediato and ops[0][1] != 13) or (not imediato and not alto):
                bandeiras = ((r >> 31) != 0, r == 0, a >= b,
                             ((a >> 31) != (b >> 31)) and ((r >> 31) != (a >> 31)))
        elif m == "cmp":
            r = (a - b) & M32
            bandeiras = ((r >> 31) != 0, r == 0, a >= b,
                         ((a >> 31) != (b >> 31)) and ((r >> 31) != (a >> 31)))
            return fecha()
        else:
            r = b
            # O `movs r0, #imm` (formato 3 do Thumb) POE as bandeiras: medido no
            # espaco Thumb inteiro, 8 casos em que o modelo dizia "nao mexe" e o
            # interpretador mexia (o Z do `movs r0, #0`).
            # O `add/sub sp, #imm` (formato 11 do Thumb) NAO escreve bandeiras.
            if imediato and ops[0][1] != 13:
                bandeiras = ((r >> 31) != 0, r == 0, c_in, est["v"])
        guarda(ops[0][1], r)
        if bandeiras is None:
            bandeiras = (est["n"], est["z"], c_in, est["v"])
        return fecha()

    if len(ops) < 2:
        return None
    # A FORMA DE DOIS OPERANDOS (`cmp r0, #0`, `mov r0, r1`): o registador de
    # entrada e o PROPRIO operando 1. Ler sempre `ops[1]` punha `a = 0` em cada
    # `cmp rn, #imm` e acusava 8 005 bandeiras que nao existem (medido no
    # `imicro3d.mod`).
    indice_op2 = 1 if len(ops) == 2 else 2
    origem = ops[0] if len(ops) == 2 else ops[1]
    a = valor_do_registo(est, origem[1], thumb) if origem[0] == "reg" else 0
    v2, c_novo = operando2(campos, est, ops, thumb, c_in, indice_op2)
    if v2 is None:
        return None
    if m in ("mov", "lsl", "lsr", "asr", "ror", "rrx", "adr"):
        r = v2
        bandeiras = ((r >> 31) != 0, r == 0, c_novo, est["v"])
    elif m == "mvn":
        r = (~v2) & M32
        bandeiras = ((r >> 31) != 0, r == 0, c_novo, est["v"])
    elif m == "add":
        r = (a + v2) & M32
        bandeiras = ((r >> 31) != 0, r == 0, (a + v2) > M32,
                     ((a >> 31) == (v2 >> 31)) and ((r >> 31) != (a >> 31)))
    elif m == "adc":
        entrada = 1 if c_in else 0
        r = (a + v2 + entrada) & M32
        bandeiras = ((r >> 31) != 0, r == 0, (a + v2 + entrada) > M32,
                     ((a >> 31) == (v2 >> 31)) and ((r >> 31) != (a >> 31)))
    elif m in ("sub", "sbc", "cmp"):
        # O `sbc` SUBTRAI O COMPLEMENTO DO CARRY: `Rd - Rm - NOT(C)`. O modelo
        # tinha a entrada invertida e acusava 120 divergencias que nao existem
        # (`sbcs r1, r0` do espaco Thumb: 0x2004 - 0x2000 - 1 = 3 e o
        # interpretador estava certo; quem estava errado era esta linha).
        entrada = 1 if (m != "sbc" or c_in) else 0
        r = (a - v2 - (1 - entrada)) & M32
        bandeiras = ((r >> 31) != 0, r == 0, a >= (v2 + (1 - entrada)),
                     ((a >> 31) != (v2 >> 31)) and ((r >> 31) != (a >> 31)))
        if m == "cmp":
            return fecha()
    elif m in ("rsb", "rsc"):
        entrada = 1 if (m != "rsc" or c_in) else 0
        r = (v2 - a - (1 - entrada)) & M32
        bandeiras = ((r >> 31) != 0, r == 0, v2 >= (a + (1 - entrada)),
                     ((v2 >> 31) != (a >> 31)) and ((r >> 31) != (v2 >> 31)))
    elif m in ("and", "tst"):
        r = a & v2
        bandeiras = ((r >> 31) != 0, r == 0, c_novo, est["v"])
    elif m == "orr":
        r = a | v2
        bandeiras = ((r >> 31) != 0, r == 0, c_novo, est["v"])
    elif m == "eor":
        r = a ^ v2
        bandeiras = ((r >> 31) != 0, r == 0, c_novo, est["v"])
    elif m == "bic":
        r = a & ((~v2) & M32)
        bandeiras = ((r >> 31) != 0, r == 0, c_novo, est["v"])
    elif m == "teq":
        r = a ^ v2
        bandeiras = ((r >> 31) != 0, r == 0, c_novo, est["v"])
        return fecha()
    elif m == "cmn":
        r = (a + v2) & M32
        bandeiras = ((r >> 31) != 0, r == 0, (a + v2) > M32,
                     ((a >> 31) == (v2 >> 31)) and ((r >> 31) != (a >> 31)))
        return fecha()
    elif m == "neg":
        r = (0 - v2) & M32
        bandeiras = ((r >> 31) != 0, r == 0, v2 == 0, (v2 & 0x80000000) != 0)
        guarda(rd, r)
        return fecha()
    else:
        return None
    if m in ("tst", "teq", "cmp", "cmn"):
        return fecha()
    if rd is None:
        return None
    guarda(rd, r)
    if not (campos["tem_s"] or thumb):
        bandeiras = None
    return fecha()


# --- A COMPARACAO ----------------------------------------------------------
def campos_iguais(a, b):
    """Os dois oraculos concordam nos campos? Quando nao, NAO se compara valor
    nenhum: um campo em que os oraculos discordam nao pode acusar o
    interpretador."""
    if a is None or b is None:
        return "sem_oraculo"
    for campo in ("canonico", "desloc", "lista", "alvo", "escreve_base", "pos_indexado",
                  "cond", "ops", "escritos"):
        if a.get(campo) != b.get(campo):
            return campo
    return None


class RelatorioDeEfeito(object):
    def __init__(self, exemplos=5):
        self.exemplos = exemplos
        self.contagem = Counter()
        self.ex = defaultdict(list)
        self.pares = Counter()
        self.ficheiros = {}

    def registar(self, classe, chave, exemplo):
        self.contagem[classe] += 1
        self.pares[(classe, chave)] += 1
        if len(self.ex[chave]) < self.exemplos:
            self.ex[chave].append(exemplo)


def estado_do_cabecalho(linha):
    campos = {}
    for pedaco in linha.split():
        if "=" in pedaco:
            k, v = pedaco.split("=", 1)
            campos[k] = v
    return campos


def comparar_uma(est, nossa, campos_obj, campos_cap, registadores, deslocamentos, cs, rel, etiqueta):
    """Compara UMA instrucao: os campos dos dois oraculos e, se concordarem, o
    EFEITO observado contra o esperado."""
    endereco = nossa["endereco"]
    exemplo = {"ficheiro": etiqueta, "endereco": endereco, "palavra": nossa["palavra"],
               "objdump": campos_obj["texto"] if campos_obj else "",
               "capstone": campos_cap["texto"] if campos_cap else "",
               "nosso": nossa["nome"], "recusou": nossa["recusou"]}
    if nossa["recusou"]:
        rel.registar("recusamos", nossa["nome"], dict(exemplo, detalhe=nossa["motivo"]))
        return
    discordancia = campos_iguais(campos_obj, campos_cap)
    if discordancia == "sem_oraculo":
        rel.registar("sem_oraculo", nossa["nome"], exemplo)
        return
    if discordancia is not None:
        rel.registar("oraculos_discordam", "%s [%s] %s" % (etiqueta, discordancia,
                    " || objdump %s || capstone %s" % (campos_obj["texto"], campos_cap["texto"])),
                    dict(exemplo, detalhe=discordancia))
        return
    esperado = modelo(campos_cap, est, est["thumb"])
    if esperado is None:
        rel.registar("nao_modelado", campos_cap["mnemonico"], exemplo)
        return

    # O NOSSO estado depois do passo: o inicial mais o que a sonda reportou.
    observado = {r: v for r, v in enumerate(est["regs"])}
    observado[15] = est["caixa"]
    observado.update(nossa["regs"])
    observado[15] = nossa["pc_depois"]

    # 1. O PC, primeiro: um alvo errado salta para o meio de outra funcao.
    if observado[15] != esperado["regs"][15]:
        rel.registar("alvo_diferente", "%s -> pc 0x%08x vs 0x%08x" % (
            campos_cap["texto"], observado[15], esperado["regs"][15]),
            dict(exemplo, esperado="pc=0x%08x" % esperado["regs"][15],
                 observado="pc=0x%08x" % observado[15]))
        return
    # 2. Os outros registadores.
    for r in range(15):
        if observado[r] != esperado["regs"][r]:
            rel.registar("registo_diferente", "%s -> r%d 0x%08x vs 0x%08x" % (
                campos_cap["texto"], r, observado[r], esperado["regs"][r]),
                dict(exemplo, esperado="r%d=0x%08x" % (r, esperado["regs"][r]),
                     observado="r%d=0x%08x" % (r, observado[r])))
            return
    # 3. O modo (o bit T) e as bandeiras.
    t_observado = (nossa["cpsr_depois"] & 0x20) != 0
    if bool(esperado["modo"]) != t_observado:
        rel.registar("modo_diferente", "%s -> thumb=%d vs %d" % (
            campos_cap["texto"], t_observado, bool(esperado["modo"])),
            dict(exemplo, esperado="thumb=%d" % bool(esperado["modo"]),
                 observado="thumb=%d" % t_observado))
        return
    if esperado["flags"] is not None:
        e_n, e_z, e_c, e_v = esperado["flags"]
        o_n = (nossa["cpsr_depois"] & 0x80000000) != 0
        o_z = (nossa["cpsr_depois"] & 0x40000000) != 0
        o_c = (nossa["cpsr_depois"] & 0x20000000) != 0
        o_v = (nossa["cpsr_depois"] & 0x10000000) != 0
        if (e_n, e_z, e_c, e_v) != (o_n, o_z, o_c, o_v):
            rel.registar("bandeira_diferente", "%s -> NZCV %d%d%d%d vs %d%d%d%d" % (
                campos_cap["texto"], o_n, o_z, o_c, o_v, e_n, e_z, e_c, e_v),
                dict(exemplo, esperado="NZCV %d%d%d%d" % (e_n, e_z, e_c, e_v),
                     observado="NZCV %d%d%d%d" % (o_n, o_z, o_c, o_v)))
            return
    # 4. A memoria: o que mudou tem de ser o que os oraculos dizem.
    vigia_ini, vigia_fim = est["vigia"]
    fora = [w for w in esperado["mem"] if not (vigia_ini <= w < vigia_fim)]
    if fora:
        # UMA ESCRITA FORA DA JANELA NAO SE PODE VER: a sonda le a janela, e o
        # resto do espaco nao e observado. Conta-se como limitacao DECLARADA, e
        # nao como divergencia -- um instrumento que acusa o que nao ve e pior do
        # que um que diz o que nao ve.
        rel.registar("fora_da_janela", "%s -> %d palavras fora de [0x%08x,0x%08x)" % (
            campos_cap["texto"], len(fora), vigia_ini, vigia_fim), exemplo)
        return
    previsto = {w: v for w, v in esperado["mem"].items() if v != palavra_em(w, est)}
    if previsto != nossa["mem"]:
        rel.registar("memoria_diferente", "%s -> %s vs %s" % (
            campos_cap["texto"],
            " ".join("%08x=%08x" % (w, v) for w, v in sorted(nossa["mem"].items())),
            " ".join("%08x=%08x" % (w, v) for w, v in sorted(previsto.items()))),
            dict(exemplo,
                 esperado=" ".join("%08x=%08x" % (w, v) for w, v in sorted(previsto.items())),
                 observado=" ".join("%08x=%08x" % (w, v) for w, v in sorted(nossa["mem"].items()))))
        return
    rel.contagem["concorda"] += 1


RE_LINHA_EFEITO = re.compile(r"^#efeito ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8}) "
                             r"([0-9a-f]{8}) (\d) regs:(.*) mem:(.*) nome:(.*)$")


def ler_saida_da_sonda(caminho_probe, ficheiro, enderecos, thumb, caixa, temporario):
    """Corre a sonda em modo de efeito e devolve a lista de leituras."""
    # O PREFIXO `0x` NAO E ESTILO: a sonda le a lista com `strtoul(..., 0)`, e
    # sem o prefixo um `3010` era lido como DECIMAL -- o auditor corria entao
    # sobre OUTROS enderecos (5 2429 das 65 536 palavras, com repeticoes), e o
    # resultado era um relatorio plausivel sobre o sitio errado.
    with open(temporario, "w") as f:
        for e in enderecos:
            f.write("0x%x\n" % e)
    args = [caminho_probe, ficheiro, "--efeito", "--pcs=" + temporario]
    if thumb:
        args.append("--thumb")
    p = subprocess.run(args, capture_output=True, text=True)
    if p.returncode != 0:
        sys.stderr.write("a sonda --efeito falhou (codigo %d): %s\n" % (p.returncode, p.stderr[:400]))
        return None, None
    cabecalho = None
    leituras = []
    for linha in p.stdout.splitlines():
        if linha.startswith("#efeito_sentinela"):
            campos = estado_do_cabecalho(linha)
            cabecalho = {
                "caixa": int(campos["caixa"], 16), "thumb": campos["modo"] == "thumb",
                "tamanho": int(campos["tamanho"]), "base": int(campos["base"], 16),
                "padrao": (int(campos["padrao_m"], 16), int(campos["padrao_x"], 16),
                           int(campos["padrao_ini"], 16), int(campos["padrao_fim"], 16)),
                "vigia": (int(campos["vigia_ini"], 16), int(campos["vigia_fim"], 16)),
                "regs": [(int(campos["sp"], 16) if r == 13 else
                          int(campos["lr"], 16) if r == 14 else
                          int(campos["caixa"], 16) if r == 15 else
                          int(campos["r%d" % r], 16)) for r in range(16)],
            }
            continue
        if linha.startswith("#efeito_motivo"):
            # O motivo da recusa vem na linha seguinte, e e ele que diz O QUE
            # faltou (P2): uma recusa sem motivo escrito nao serve para decidir.
            if leituras:
                leituras[-1]["motivo"] = linha.split(None, 2)[2] if len(linha.split(None, 2)) > 2 else ""
            continue
        m = RE_LINHA_EFEITO.match(linha)
        if m is None:
            continue
        endereco, palavra, cpsr_antes, pc_depois, cpsr_depois, recusou, regs_txt, mem_txt, nome = m.groups()
        regs = {}
        for pedaco in regs_txt.split():
            r, v = pedaco.split("=")
            regs[int(r)] = int(v, 16)
        mem = {}
        for pedaco in mem_txt.split():
            a, v = pedaco.split("=")
            mem[int(a, 16)] = int(v, 16)
        leituras.append({"endereco": int(endereco, 16), "palavra": int(palavra, 16),
                         "cpsr_antes": int(cpsr_antes, 16), "pc_depois": int(pc_depois, 16),
                         "cpsr_depois": int(cpsr_depois, 16), "recusou": recusou == "1",
                         "regs": regs, "mem": mem, "nome": nome, "motivo": ""})
    return cabecalho, leituras


def correr_efeito(args):
    if not os.path.exists(args.sonda):
        sys.stderr.write("sonda em falta: %s\n" % args.sonda)
        return 3
    ferramentas = capstone_ferramentas()
    if ferramentas is None:
        sys.stderr.write("capstone em falta: sem o SEGUNDO oraculo a comparacao de valores "
                         "nao tem contraste independente (usar o python3 do sistema, que tem 5.0.x)\n")
        return 3
    cs, registadores, deslocamentos = ferramentas

    alvos = alvos_de_efeito(args)
    if not alvos:
        sys.stderr.write("nada para auditar em %s\n" % args.alvo)
        return 5
    rel = RelatorioDeEfeito(args.exemplos)
    temporario = tempfile.mktemp(suffix=".pcs")
    for caminho, thumb, enderecos, etiqueta in alvos:
        with open(caminho, "rb") as f:
            dados = f.read()
        tamanho = 2 if thumb else 4
        enderecos = [e for e in enderecos if e // tamanho < len(dados) // tamanho]
        if not enderecos:
            continue
        # O oraculo do binutils: UMA corrida por ficheiro, o texto por endereco.
        linhas = correr_objdump(args.objdump, caminho, 0, thumb)
        textos = {end: texto for end, _, texto in linhas}
        cabecalho, leituras = ler_saida_da_sonda(args.sonda, caminho, enderecos, thumb, 0, temporario)
        if cabecalho is None or leituras is None:
            return 3
        caixa = cabecalho["caixa"]
        rel.ficheiros[etiqueta] = rel.ficheiros.get(etiqueta, 0) + len(leituras)
        for nossa in leituras:
            est = dict(cabecalho)
            est["regs"] = list(cabecalho["regs"])
            est["bytes"] = dados[nossa["endereco"]:nossa["endereco"] + tamanho]
            est["n"] = (nossa["cpsr_antes"] & 0x80000000) != 0
            est["z"] = (nossa["cpsr_antes"] & 0x40000000) != 0
            est["c"] = (nossa["cpsr_antes"] & 0x20000000) != 0
            est["v"] = (nossa["cpsr_antes"] & 0x10000000) != 0
            est["motivo"] = ""
            campos_obj = campos_do_objdump(textos.get(nossa["endereco"], ""), nossa["endereco"],
                                           caixa, thumb)
            campos_cap = campos_do_capstone(cs, registadores, deslocamentos, nossa["palavra"],
                                            tamanho, caixa, thumb)
            comparar_uma(est, nossa, campos_obj, campos_cap, registadores, deslocamentos, cs,
                         rel, etiqueta)
    if os.path.exists(temporario):
        os.unlink(temporario)
    imprimir_relatorio_de_efeito(rel, args)
    if args.json:
        with open(args.json, "w") as f:
            json.dump({"contagem": dict(rel.contagem),
                       "pares": {" || ".join(k): v for k, v in rel.pares.items()}}, f,
                      indent=1, sort_keys=True)
        print("\n# relatorio escrito em %s" % args.json)
    return 1 if (rel.contagem.get("alvo_diferente", 0) or rel.contagem.get("registo_diferente", 0)
                 or rel.contagem.get("memoria_diferente", 0) or rel.contagem.get("bandeira_diferente", 0)
                 or rel.contagem.get("modo_diferente", 0)) else 0


def alvos_de_efeito(args):
    """A lista de (ficheiro, modo, enderecos, etiqueta) a auditar."""
    alvo = args.alvo
    if alvo.lower().endswith(".json"):
        with open(alvo) as f:
            corpus = json.load(f)
        titulos = corpus["titulos"] if isinstance(corpus, dict) else corpus
        saida = []
        for entrada in titulos:
            pasta = entrada.get("pasta") or entrada.get("folder")
            ficheiro = os.path.join(args.mods, pasta, entrada["mod"] + ".mod") if pasta else \
                os.path.join(args.mods, entrada["mod"] + ".mod")
            for modo, chave in ((False, "arm"), (True, "thumb")):
                enderecos = entrada.get(chave) or []
                if enderecos:
                    saida.append((ficheiro, modo, enderecos,
                                  os.path.basename(ficheiro) + (":thumb" if modo else "")))
        return saida
    # Um ficheiro binario: todas as palavras, no modo pedido.
    with open(alvo, "rb") as f:
        tamanho = 2 if args.thumb else 4
        quantas = len(f.read()) // tamanho
    return [(alvo, args.thumb, [i * tamanho for i in range(quantas)], os.path.basename(alvo))]


def imprimir_relatorio_de_efeito(rel, args):
    total = sum(rel.contagem.values())
    print("# AUDITOR DE EFEITO (VALORES) -- %d instrucoes em %d ficheiros" %
          (total, len(rel.ficheiros)))
    print("# concorda: %d | alvo_diferente: %d | registo_diferente: %d | bandeira_diferente: %d | "
          "modo_diferente: %d | memoria_diferente: %d | fora_da_janela: %d | nao_modelado: %d | "
          "recusamos: %d | oraculos_discordam: %d | sem_oraculo: %d" % (
              rel.contagem.get("concorda", 0), rel.contagem.get("alvo_diferente", 0),
              rel.contagem.get("registo_diferente", 0), rel.contagem.get("bandeira_diferente", 0),
              rel.contagem.get("modo_diferente", 0), rel.contagem.get("memoria_diferente", 0),
              rel.contagem.get("fora_da_janela", 0), rel.contagem.get("nao_modelado", 0),
              rel.contagem.get("recusamos", 0),
              rel.contagem.get("oraculos_discordam", 0), rel.contagem.get("sem_oraculo", 0)))
    # A LINHA QUE A GUARDA LE. O criterio da guarda e o numero das divergencias
    # de VALOR, e nao o das recusas (P2) nem o das palavras em que so ha um
    # oraculo: esses sao cobertura declarada, e nao concordancia.
    print("# efeito divergencias_de_valor: %d" % (
        rel.contagem.get("alvo_diferente", 0) + rel.contagem.get("registo_diferente", 0)
        + rel.contagem.get("modo_diferente", 0) + rel.contagem.get("bandeira_diferente", 0)
        + rel.contagem.get("memoria_diferente", 0)))
    for classe in ("alvo_diferente", "registo_diferente", "modo_diferente", "bandeira_diferente",
                   "memoria_diferente", "oraculos_discordam", "nao_modelado", "recusamos"):
        pares = [(k, v) for (c, k), v in rel.pares.items() if c == classe]
        if not pares:
            continue
        print()
        print("## %s (top %d)" % (classe, args.top))
        for chave, n in sorted(pares, key=lambda kv: -kv[1])[:args.top]:
            print("%9d  %s" % (n, chave))
            for exemplo in rel.ex[chave][:1]:
                print("           %s +0x%x palavra 0x%08x objdump: %s | capstone: %s | nosso: %s"
                      % (exemplo["ficheiro"], exemplo["endereco"], exemplo["palavra"],
                         exemplo["objdump"], exemplo["capstone"], exemplo["nosso"]))
                if exemplo.get("esperado") or exemplo.get("observado"):
                    print("           esperado: %s | observado: %s"
                          % (exemplo.get("esperado", ""), exemplo.get("observado", "")))


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
    # --- modo de EFEITO (valores, e nao nomes) ---
    ap.add_argument("--efeito", action="store_true",
                    help="compara o EFEITO (alvos, enderecos, listas, deslocamentos)")

    ap.add_argument("--verboso", action="store_true")
    args = ap.parse_args()

    if args.efeito:
        return correr_efeito(args)

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
