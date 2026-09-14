#!/usr/bin/env python3
"""PROVA CADA GUARDA DO DESCODIFICADOR POR VIOLACAO DELIBERADA.

Uma guarda que passa sem a mudanca nao e guarda. Para cada violacao: quebra-se a
coisa de proposito, constroi-se, corre-se o teste que a deve apanhar, e o
resultado esperado e VERMELHO. Depois restaura-se.

ESTAS VIOLACOES SAO AS CLASSES QUE O AUDITOR DIFERENCIAL ENCONTROU
(`tools/auditar_descodificador.py`), uma a uma:

  V1  o grupo media do ARMv6 existe (sem ele o `uxth` volta a ser `ldrb`)
  V2  e o grupo DSP do ARMv5TE existe (sem ele o `smulbb` volta a ser `cmn`)
  V3  a extensao RODA antes de extrair a metade
  V4  o `SXTAB` soma ao registrador da base
  V5  o `REV` inverte os quatro bytes
  V6  o `REV16` troca os bytes DENTRO de cada meia-palavra
  V7  o `REVSH` estende o sinal depois de trocar
  V8  as formas `xy` usam os bits 6 e 5 para escolher as metades
  V9  o `SMLAxy` soma o acumulador
  V10 o `SMULxy` exige os bits 15-12 = 0 (senao um `cmn` vira multiplicacao)
  V11 o `QADD` satura e poe a bandeira Q
  V12 o `PLD` nao tem efeito (e nao recusa)
  V13 a fronteira do grupo media: o bit 4 = 0 continua a ser transferencia
  V14 as formas conhecidas e nao implementadas RECUSAM (o `SEL`, o `SXTB16`)
  V15 o `MRS` exige os doze bits baixos a zero (senao engole o `SWP`)
  V16 o `MCR`/`MRC` vao para o coprocessador (e nao para o `SWI`)
  V17 o `L` do Thumb no formato 8/9 e o bit 11 (o `strh` guarda dois bytes)
  V18 o `strb` e o `str` de palavra do Thumb GUARDAM (o `L` e o bit 11)
  V19 as formas de memoria do formato 5 do Thumb existem (o `ldrsh` estende)
  V20 as formas Thumb que faltam RECUSAM com o nome da forma

Uso: python3 tools/provar_guardas_descodificador.py <raiz_da_arvore>
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

CPU = "core/cpu/arm_interpreter.cpp"

# Trechos multilinha, escritos como listas para nao misturar as aspas do C++ com
# as do Python.
REV_SET = "\n".join([
    "      Set(static_cast<int>(rd), ((origem & 0xFFu) << 24) | ((origem & 0xFF00u) << 8) |",
    "                                ((origem >> 8) & 0xFF00u) | ((origem >> 24) & 0xFFu));",
])
REVSH_SET = "\n".join([
    "      const Reg trocado = ((origem & 0xFF00u) >> 8) | ((origem & 0xFFu) << 8);",
    "      Set(static_cast<int>(rd), static_cast<Reg>(static_cast<std::int32_t>(static_cast<std::int16_t>(trocado & 0xFFFFu))));",
])
REVSH_SET_VIOLADO = "\n".join([
    "      const Reg trocado = ((origem & 0xFF00u) >> 8) | ((origem & 0xFFu) << 8);",
    "      Set(static_cast<int>(rd), trocado & 0xFFFFu);",
])
SMLAXY_SET = "\n".join([
    "    const Reg resultado = static_cast<Reg>(static_cast<std::int32_t>(",
    "        produto + static_cast<std::int64_t>(static_cast<std::int32_t>(Get(static_cast<int>(rd))))));",
])
SMLAXY_SET_VIOLADO = "    const Reg resultado = static_cast<Reg>(static_cast<std::int32_t>(produto));"
THUMB_HALF_SET = "\n".join([
    "      if (carrega) Set(static_cast<int>(rd), mem_.Ler16(end));",
    "      else mem_.Escrever16(end, static_cast<std::uint16_t>(Get(static_cast<int>(rd)) & 0xFFFF));",
])
THUMB_HALF_SET_VIOLADO = "\n".join([
    "      if (carrega) Set(static_cast<int>(rd), mem_.Ler16(end));",
    "      else mem_.Escrever32(end, Get(static_cast<int>(rd)));",
])

VIOLACOES = [
    ("V1 o grupo media do ARMv6 deixa de ser descodificado (o uxth volta a ser ldrb)",
     CPU,
     "    if (EhMediaArmv6(instr)) { MediaArmv6(instr, pc); Set(kPC, pc + 4); return; }",
     "",
     "Cpu.MediaUxthExtraiAMeiaPalavraSemSinal:Cpu.MediaSxthEstendeOSinalDaMeiaPalavra:"
     "Cpu.MediaSxtbEstendeOSinalDoByte:Cpu.MediaUxtbZeraOsBitsDeCima"),
    ("V2 o grupo DSP do ARMv5TE deixa de ser descodificado (o smulbb volta a ser cmn)",
     CPU,
     "    if (EhAritmeticaDsp(instr)) { AritmeticaDsp(instr, pc); Set(kPC, pc + 4); return; }",
     "",
     "Cpu.DspSmulbbMultiplicaAsMeiasPalavrasDeBaixo:Cpu.DspClzContaOsZerosDaEsquerda"),
    ("V3 a rodagem de bytes deixa de ser aplicada antes de extrair a metade",
     CPU,
     "                         ? RodarBytes(Get(static_cast<int>(rm)), rot)",
     "                         ? Get(static_cast<int>(rm))",
     "Cpu.MediaRodagemDeBytesVemAntesDaExtensao"),
    ("V4 o SXTAB deixa de somar o registrador da base",
     CPU,
     "      if (forma->semantica == SemanticaMedia::ExtensaoAcumulada) valor += Get(static_cast<int>(rn));",
     "",
     "Cpu.MediaSxtabSomaAoRegistradorDaBase"),
    ("V5 o REV deixa de inverter os quatro bytes", CPU, REV_SET,
     "      Set(static_cast<int>(rd), origem);", "Cpu.MediaRevInverteOsQuatroBytes"),
    ("V6 o REV16 troca as metades em vez dos bytes dentro de cada metade",
     CPU,
     "      Set(static_cast<int>(rd), ((origem & 0xFF00FF00u) >> 8) | ((origem & 0x00FF00FFu) << 8));",
     "      Set(static_cast<int>(rd), ((origem & 0xFFFF0000u) >> 16) | ((origem & 0x0000FFFFu) << 16));",
     "Cpu.MediaRev16InverteEmCadaMeiaPalavra"),
    ("V7 o REVSH deixa de estender o sinal", CPU, REVSH_SET, REVSH_SET_VIOLADO,
     "Cpu.MediaRevshInverteEEstendeOSinal"),
    ("V8 as metades do SMULxy passam a vir dos bits trocados (x e y)",
     CPU,
     "  const int xy = (x_alto ? 2 : 0) | (y_alto ? 1 : 0);",
     "  const int xy = (y_alto ? 2 : 0) | (x_alto ? 1 : 0);",
     "Cpu.DspSmulbtUsaAMeiaDeCimaDeRs"),
    ("V9 o SMLAxy deixa de somar o acumulador", CPU, SMLAXY_SET, SMLAXY_SET_VIOLADO,
     "Cpu.DspSmlaXySomaORegistradorDeAcumulacao"),
    ("V10 o SMULxy deixa de exigir os bits 15-12 a zero",
     CPU,
     "    {0x0FF0F090u, 0x01600080u, \"smulxy\"},",
     "    {0x0FF00090u, 0x01600080u, \"smulxy\"},",
     "Cpu.DspSmulxyNaoAceitaBits15a12DiferentesDeZero"),
    ("V11 o QADD deixa de saturar",
     CPU,
     "    if (saturou) modo_atual_ |= (1u << 27);  // a bandeira Q do CPSR",
     "",
     "Cpu.DspQdaddSaturaEDevolveOMaximo:Cpu.DspQsubSaturaNoNegativo"),
    ("V12 o PLD passa a recusar (deixa de ser uma dica sem efeito)",
     CPU,
     "    if ((instr & 0xFF70F000u) == 0xF550F000u || (instr & 0xFF70F000u) == 0xF750F000u) {",
     "    if (false) {",
     "Cpu.PldNaoTocaNaMemoriaNemNosRegistradores"),
    ("V13 a fronteira do grupo media deixa de excluir as transferencias (bit 4 = 0)",
     CPU,
     "  return (instr & 0x0F000010u) == 0x06000010u;",
     "  return (instr & 0x0F000000u) == 0x06000000u;",
     "Cpu.MediaNaoEngoleATransferenciaComDeslocamentoDeRegistrador"),
    ("V14 uma forma media conhecida e nao implementada passa a ser executada",
     CPU,
     "      Recusar(instr, pc, \"forma do grupo media do ARMv6 conhecida e NAO implementada\");",
     "      Set(static_cast<int>(rd), origem);",
     "Cpu.MediaSxtb16ConhecidaERecusadaComNome"),
    ("V15 a mascara do MRS deixa de exigir os bits baixos a zero",
     CPU,
     "    if ((instr & 0x0FBF0FFFu) == 0x010F0000u) {  // MRS",
     "    if ((instr & 0x0FBF0F00u) == 0x010F0000u) {  // MRS",
     "Cpu.MrsComBitsBaixosDiferentesDeZeroNaoEMrs"),
    ("V16 o bit 24 volta a decidir o SWI ao contrario (o MCR vira SWI)",
     CPU,
     "  if ((instr & (1u << 24)) == 0) {\n    Coprocessador(instr, pc);",
     "  if ((instr & (1u << 24)) != 0) {\n    Coprocessador(instr, pc);",
     "Cpu.McrNaoVaiParaOSwiEDepoisDoArranjoVaiParaOCoprocessador"),
    ("V17 o L do Thumb volta a ser lido nos bits 12-11 (o strh vira ldr)",
     CPU,
     "    const bool carrega = ((instr >> 11) & 1u) != 0;",
     "    const bool carrega = ((instr >> 11) & 3u) != 0;",
     "Cpu.ThumbLdrhEStrhDoFormato8UsamDoisBytes:Cpu.ThumbStrbEStrDePalavraGuardamENaoCarregam"),
    ("V18 o strh do Thumb deixa de escrever so dois bytes", CPU, THUMB_HALF_SET,
     THUMB_HALF_SET_VIOLADO,
     "Cpu.ThumbLdrhEStrhDoFormato8UsamDoisBytes:Cpu.ThumbStrbEStrDePalavraGuardamENaoCarregam"),
    ("V19 o formato 5 do Thumb deixa de existir (o ldrsh e recusado)",
     CPU,
     "  if ((instr & 0xF000u) == 0x5000u) {",
     "  if (false) {",
     "Cpu.ThumbFormato5FazAsSeteFormasDeMemoriaComRegistrador"),
    ("V20 as formas Thumb em falta deixam de recusar com nome",
     CPU,
     "  familia_ = NomeDoFormatoThumb(instr);",
     "  familia_ = \"nenhuma\";",
     "Cpu.ThumbFormato5ContinuaARecusarOQueNaoConhece"),
]


def pasta_de_build(raiz):
  # `ZB2_DIR` e o nome que a arvore ja usa para a pasta de build (ver
  # `tools/regressao.sh`): a pasta pode estar fora da arvore.
  return Path(os.environ.get("ZB2_DIR", str(raiz / "build")))


def correr(raiz, filtro):
  r = subprocess.run([str(pasta_de_build(raiz) / "zb2_tests"), "--gtest_filter=" + filtro],
                     capture_output=True, text=True)
  resumo = [l for l in r.stdout.splitlines()
            if l.startswith("[  PASSED  ]") or l.startswith("[  FAILED  ]")]
  return r.returncode, " | ".join(resumo)


def construir(raiz):
  ambiente = dict(os.environ)
  # O compilador escreve os temporarios onde o `TMPDIR` mandar. Nesta maquina o
  # `/tmp` e um tmpfs partilhado com os ficheiros dos outros agentes e enche; uma
  # compilacao falha com "sem espaco", que seria um VERMELHO que nao e da guarda.
  ambiente.setdefault("TMPDIR", str(Path.home() / "tmpbuild"))
  return subprocess.run(["cmake", "--build", str(pasta_de_build(raiz)), "-j4", "--target", "zb2_tests"],
                        cwd=raiz, capture_output=True, text=True, env=ambiente)


def main():
  raiz = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path.cwd()
  falhas = 0
  for nome, rel, de, para, filtro in VIOLACOES:
    f = raiz / rel
    texto = f.read_text()
    if texto.count(de) != 1:
      print("%s: ANCORA NAO UNICA em %s (%d vezes)" % (nome, rel, texto.count(de)))
      falhas += 1
      continue
    guardado = str(f) + ".sao"
    shutil.copy(f, guardado)
    f.write_text(texto.replace(de, para))
    b = construir(raiz)
    if b.returncode != 0:
      print("%s: NAO COMPILOU (a violacao partiu o build)" % nome)
      shutil.copy(guardado, f)
      Path(guardado).unlink()
      falhas += 1
      continue
    codigo, resumo = correr(raiz, filtro)
    veredito = "VERMELHO (como devia)" if codigo != 0 else "VERDE -- A GUARDA NAO APANHOU"
    if codigo == 0:
      falhas += 1
    print("%s\n    -> %s\n    -> %s" % (nome, veredito, resumo))
    shutil.copy(guardado, f)
    Path(guardado).unlink()
  construir(raiz)
  codigo, resumo = correr(raiz, "Cpu.Media*:Cpu.Dsp*:Cpu.Pld*:Cpu.Blx*:Cpu.Mcr*:Cpu.Mrs*:Cpu.Thumb*")
  print("RESTAURADO: %s" % resumo)
  if codigo != 0:
    falhas += 1
  print("\nGUARDAS QUE NAO FICARAM VERMELHAS: %d" % falhas)
  return 1 if falhas else 0


if __name__ == "__main__":
  sys.exit(main())
