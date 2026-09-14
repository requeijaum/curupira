#!/usr/bin/env python3
"""REMENDO: o que o rasterizador precisa em ficheiro PARTILHADO.

DUAS ALTERACOES, e nenhuma delas e codigo de rasterizacao:

  1. `core/brew/despacho.cpp` -- UMA linha: `igl_.DefinirTela(&tela_)`. E o que
     liga a superficie ao IGL. Sem ela o `glDrawArrays` RECUSA com o motivo
     escrito ("o IGL NAO TEM TELA LIGADA") e nenhum pixel aparece -- uma recusa
     honesta (P2), e nao um defeito silencioso.
  2. `core/brew/egl.cpp` -- TEXTO. O `Egl::Instalar` registava a falta
     `rasterizador_de_GL` com "nenhum pixel e escrito (nao ha rasterizador)".
     Isso era VERDADE antes de `core/video/rasterizador.cpp` existir e passa a ser
     MENTIRA no mesmo commit (P7: um log so entra se puder ser verdadeiro). A
     falta desaparece da lista de demanda, porque deixou de faltar; fica um evento
     de informacao que diz QUAL e o modulo que desenha.

PORQUE ISTO E UM SCRIPT, e nao um `git apply`: o protocolo do remendo da etapa 6
(`tools/remendo_gl.py`) -- `assert` ANTES de escrever (a ancora existe exactamente
uma vez?), `assert` DEPOIS (contagem de linhas, e um texto que SO existe se a
mudanca entrou). Uma ancora em falta NAO escreve nada. Isto nasceu de uma ronda em
que um `grep -c` de um simbolo que ja la estava "verificou" uma mudanca que nao
tinha entrado.

Uso: python3 tools/remendo_raster.py <raiz_da_arvore> [--reverter]
"""
import sys
from pathlib import Path

DESPACHO_ANCORA = """  traco_.Emitir(Area::Video, Nivel::Informacao, "GL_CABLADO",
                "IGL em 0x800B0000 (faixa 30000) e IEGL em 0x800B1000 (faixa 31000)");
  return a + b;
"""

DESPACHO_NOVO = """  traco_.Emitir(Area::Video, Nivel::Informacao, "GL_CABLADO",
                "IGL em 0x800B0000 (faixa 30000) e IEGL em 0x800B1000 (faixa 31000)");
  // A TELA DO DESENHO. O rasterizador (`core/video/rasterizador.cpp`) escreve
  // AQUI, e nao num buffer proprio: a medida `PIXELS`/`CORES` do
  // `tools/bateria.cpp` le a `Tela` do `Despacho`, e uma tela propria dentro do
  // IGL daria um numero que nao mede o que o titulo escreveu no ecra.
  //
  // Sem esta linha o `glDrawArrays` RECUSA com o motivo escrito ("o IGL NAO TEM
  // TELA LIGADA") e nenhum pixel aparece -- e o unico remendo que a etapa do
  // rasterizador precisa num ficheiro partilhado.
  igl_.DefinirTela(&tela_);
  return a + b;
"""

EGL_ANCORA = """  // O QUE ESTA ETAPA NAO FAZ, DITO UMA VEZ E COM NOME. Sem isto, o proximo a ler
  // este modulo conclui que a geometria apresentada aqui aparece em algum lado.
  traco_.RegistarFalta(Area::Video, "rasterizador_de_GL",
                       "o IEGL existe e serve o config e as superficies; nenhum pixel e escrito "
                       "(nao ha rasterizador). Medido: 0 pixels em 62 titulos.");
"""

EGL_NOVO = """  // O RASTERIZADOR EXISTE, E NAO E AQUI (`core/video/rasterizador.cpp`).
  //
  // AQUI ESTAVA UMA FALTA -- `rasterizador_de_GL`, "nenhum pixel e escrito (nao ha
  // rasterizador). Medido: 0 pixels em 62 titulos." -- e ela era verdadeira. Com o
  // rasterizador escrito, a mesma linha passaria a ser MENTIRA no minuto seguinte
  // (P7: um log so entra se puder ser verdadeiro). O que fica dito e o que este
  // modulo continua a ser: o config e as superficies. Quem desenha e o IGL, na
  // Tela que o despacho lhe liga (`igl_.DefinirTela`).
  traco_.Emitir(Area::Video, Nivel::Informacao, "IEGL_SEM_DESENHO_PROPRIO",
                "o IEGL serve o config e as superficies; os pixels sao escritos pelo IGL "
                "em core/video/rasterizador.cpp");
"""

# (nome, ficheiro, ancora, novo, marcador de "ja aplicado", linhas que cresce)
ALTERACOES = [
    ("a tela ligada ao IGL", "core/brew/despacho.cpp", DESPACHO_ANCORA, DESPACHO_NOVO,
     "igl_.DefinirTela(&tela_);", 9),
    ("o texto do IEGL sem rasterizador", "core/brew/egl.cpp", EGL_ANCORA, EGL_NOVO,
     "IEGL_SEM_DESENHO_PROPRIO", 6),
]


def aplicar(raiz: Path, reverter: bool) -> int:
  falhou = False
  for nome, relativo, ancora, novo, marcador, delta in ALTERACOES:
    alvo = raiz / relativo
    texto = alvo.read_text()
    antes = len(texto.splitlines())
    aplicado = marcador in texto
    # Nada a fazer: aplicar o que ja esta aplicado, ou reverter o que nao esta.
    if (not reverter and aplicado) or (reverter and not aplicado):
      continue
    if reverter:
      if texto.count(novo) != 1:
        print(f"RECUSO ({nome}): o bloco a reverter nao aparece uma vez so")
        falhou = True
        continue
      texto = texto.replace(novo, ancora)
      esperado = antes - delta
    else:
      if texto.count(ancora) != 1:
        print(f"RECUSO ({nome}): a ancora aparece {texto.count(ancora)} vezes em {relativo} "
              f"(esperado: 1). Alguem mexeu no ficheiro; nada foi escrito.")
        falhou = True
        continue
      texto = texto.replace(ancora, novo)
      esperado = antes + delta
    if len(texto.splitlines()) != esperado:
      print(f"RECUSO ({nome}): contagem de linhas errada ({len(texto.splitlines())} != {esperado})")
      falhou = True
      continue
    assert marcador in texto or reverter
    alvo.write_text(texto)
    print(f"{'revertido' if reverter else 'aplicado'} ({nome}): {relativo} "
          f"{antes} -> {len(texto.splitlines())} linhas")
  return 1 if falhou else 0


def main() -> int:
  if len(sys.argv) < 2:
    print(__doc__)
    return 2
  return aplicar(Path(sys.argv[1]), "--reverter" in sys.argv)


if __name__ == "__main__":
  sys.exit(main())
