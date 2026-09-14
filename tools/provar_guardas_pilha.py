#!/usr/bin/env python3
"""PROVA CADA GUARDA DO GRUPO "EXTRA LOAD/STORE" POR VIOLACAO DELIBERADA.

Uma guarda que passa sem a mudanca nao e guarda. Para cada violacao: quebra-se a
coisa de proposito, constroi-se e corre-se o teste que a deve apanhar; o
resultado esperado e VERMELHO. Depois restaura-se.

O QUE ESTAS VIOLACOES PROVAM, uma a uma:
  V1  o grupo existe (sem ele o `LDRD` volta a ser lido como `BIC`) -- e ESTA a
      violacao que reproduz a parede dos 21 titulos
  V2  o grupo nao engole o `MUL`/`UMULL`/`SWP`, que vivem no mesmo espaco
  V3  no `LDRD`/`STRD` quem decide a direccao e o campo 7-4, e nao o bit 20
      (a armadilha que o binutils desfez)
  V4  o `LDRH` carrega 16 bits, e nao 8
  V5  o `STRH` escreve 16 bits, e nao 32
  V6  o `LDRSB` estende o sinal
  V7  o `!` anda com a base (e o pos-indexado anda depois de ler)
  V8  a forma nao privilegiada (`STRHT`) RECUSA
  V9  o `LDRD` com `Rt` impar RECUSA
  V10 o `LDRD` desalinhado RECUSA
  V11 o `Rt` = PC RECUSA

Uso: python3 tools/provar_guardas_pilha.py <raiz_da_arvore>
"""
import shutil
import subprocess
import sys
from pathlib import Path

CPU = "core/cpu/arm_interpreter.cpp"

VIOLACOES = [
    ("V1 o grupo extra load/store deixa de ser descodificado (o LDRD volta a ser um BIC)",
     CPU,
     "    if (EhTransferenciaExtra(instr)) {",
     "    if (false) {",
     "Cpu.ExtraLdrdCarregaDoisRegistradoresDaPilha:Cpu.ExtraLdrdNaoDevolveOEnderecoDaPilha"),
    ("V2 a mascara deixa de excluir o espaco do MUL/SWP (bits 6-5 = 00)",
     CPU,
     "  return (instr & 0x0E000000u) == 0u && (instr & 0x90u) == 0x90u && (instr & 0x60u) != 0u;",
     "  return (instr & 0x0E000000u) == 0u && (instr & 0x90u) == 0x90u;",
     "Cpu.ExtraContinuaADescodificarOMulNoMesmoEspaco"),
    ("V3 a direccao da palavra dupla passa a vir do bit 20 (LDRD trocado com STRD)",
     CPU,
     "  const bool dupla_carrega = (campo == 0xDu);",
     "  const bool dupla_carrega = carrega;",
     "Cpu.ExtraLdrdCarregaDoisRegistradoresDaPilha:Cpu.ExtraStrdEscreveDoisRegistradoresSeguidos"),
    ("V4 o LDRH passa a carregar 8 bits",
     CPU,
     "      Set(static_cast<int>(rt), mem_.Ler16(endereco));  // zero-extendido",
     "      Set(static_cast<int>(rt), mem_.Ler8(endereco));",
     "Cpu.ExtraLdrhZeraOsBitsDeCima"),
    ("V5 o STRH passa a escrever 32 bits",
     CPU,
     "      mem_.Escrever16(endereco, static_cast<std::uint16_t>(valor_lo & 0xFFFFu));",
     "      mem_.Escrever32(endereco, valor_lo);",
     "Cpu.ExtraStrhEscreveSoDoisBytes"),
    ("V6 o LDRSB perde a extensao de sinal",
     CPU,
     "    const auto b = static_cast<std::int8_t>(mem_.Ler8(endereco));",
     "    const auto b = static_cast<std::uint8_t>(mem_.Ler8(endereco));",
     "Cpu.ExtraLdrsbEstendeOSinalDoByte"),
    ("V7 a escrita na base deixa de acontecer (o `!` nao anda com o ponteiro)",
     CPU,
     "  if (p && w) Set(static_cast<int>(rn), endereco);",
     "  if (false) Set(static_cast<int>(rn), endereco);",
     "Cpu.ExtraLdrdComEscritaNaBaseAndaComOPonteiro:Cpu.ExtraLdrhPosIndexadoAndaComABase"),
    ("V8 a forma nao privilegiada deixa de recusar",
     CPU,
     "  if (!p && w) {",
     "  if (false) {",
     "Cpu.ExtraFormaNaoPrivilegiadaERecusadaEmVozAlta"),
    ("V9 o LDRD com Rt impar deixa de recusar",
     CPU,
     "  if (palavra_dupla && (rt & 1u) != 0) {",
     "  if (false) {",
     "Cpu.ExtraLdrdComRtImparERecusado"),
    ("V10 o LDRD desalinhado deixa de recusar",
     CPU,
     "    if ((endereco & 3u) != 0u) {",
     "    if (false) {",
     "Cpu.ExtraLdrdDesalinhadoERecusadoEmVozAlta"),
    ("V11 o Rt = PC deixa de recusar",
     CPU,
     "  if (rn == 15 || rt == 15) {",
     "  if (false) {",
     "Cpu.ExtraComRtIgualAoPcERecusado"),
]


def correr(raiz: Path, filtro: str):
  r = subprocess.run([str(raiz / "build" / "zb2_tests"), "--gtest_filter=" + filtro],
                     capture_output=True, text=True)
  resumo = [l for l in r.stdout.splitlines()
            if l.startswith("[  PASSED  ]") or l.startswith("[  FAILED  ]")]
  return r.returncode, " | ".join(resumo)


def main() -> int:
  raiz = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path.cwd()
  falhas = 0
  for nome, rel, de, para, filtro in VIOLACOES:
    f = raiz / rel
    texto = f.read_text()
    if texto.count(de) != 1:
      print(f"{nome}: ANCORA NAO UNICA em {rel} ({texto.count(de)} vezes)")
      falhas += 1
      continue
    guardado = str(f) + ".sao"
    shutil.copy(f, guardado)
    f.write_text(texto.replace(de, para))
    b = subprocess.run(["cmake", "--build", "build", "-j4", "--target", "zb2_tests"],
                       cwd=raiz, capture_output=True, text=True)
    if b.returncode != 0:
      print(f"{nome}: NAO COMPILOU (a violacao partiu o build)")
      shutil.copy(guardado, f)
      Path(guardado).unlink()
      falhas += 1
      continue
    codigo, resumo = correr(raiz, filtro)
    veredito = "VERMELHO (como devia)" if codigo != 0 else "VERDE -- A GUARDA NAO APANHOU"
    if codigo == 0:
      falhas += 1
    print(f"{nome}\n    -> {veredito}\n    -> {resumo}")
    shutil.copy(guardado, f)
    Path(guardado).unlink()
  subprocess.run(["cmake", "--build", "build", "-j4", "--target", "zb2_tests"],
                 cwd=raiz, capture_output=True, text=True)
  codigo, resumo = correr(raiz, "Cpu.Extra*")
  print(f"RESTAURADO: {resumo}")
  if codigo != 0:
    falhas += 1
  print(f"\nGUARDAS QUE NAO FICARAM VERMELHAS: {falhas}")
  return 1 if falhas else 0


if __name__ == "__main__":
  sys.exit(main())
