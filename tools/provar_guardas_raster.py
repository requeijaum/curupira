#!/usr/bin/env python3
"""PROVA CADA GUARDA DO RASTERIZADOR POR VIOLACAO DELIBERADA.

Uma guarda que passa sem a mudanca nao e guarda (a licao da etapa 2). Para cada
violacao: quebra-se a coisa de proposito, constroi-se e corre-se o teste que a
deve apanhar; o resultado esperado e VERMELHO. Depois restaura-se.

O QUE ESTAS VIOLACOES PROVAM, uma a uma:
  V1  a regra do canto (a aresta partilhada pertence a UM triangulo so)
  V2  a inversao do y (a orientacao do ecra)
  V3  os pesos baricentricos da cor interpolada
  V4  o teste de profundidade
  V5  a coordenada de textura amostrada
  V6  o descarte de faces
  V7  o limite da janela (o `glViewport` limita o que se desenha)
  V8  o `glClear` que poe o clip de lado
  V9  a recusa de uma primitiva sem caminho
  V10 a recusa de desenhar sem tela (`Igl::DefinirTela`)

Uso: python3 tools/provar_guardas_raster.py <raiz_da_arvore>
"""
import shutil
import subprocess
import sys
from pathlib import Path

# (nome, ficheiro, de, para, filtro gtest)
VIOLACOES = [
    ("V1 a regra do canto deixa de decidir (a aresta pertence aos dois)",
     "core/video/rasterizador.cpp",
     "bool ArestaDeCanto(double dx, double dy) { return dy < 0.0 || (dy == 0.0 && dx > 0.0); }",
     "bool ArestaDeCanto(double dx, double dy) { (void)dx; (void)dy; return true; }",
     "Rasterizador.ArestaPartilhadaNaoEscreveDuasVezesNemDeixaFenda"),
    ("V2 o y deixa de ser invertido (a orientacao do ecra muda)",
     "core/video/rasterizador.cpp",
     "  v->y = static_cast<float>(vy + (1.0 - y_ndc) * vh * 0.5);",
     "  v->y = static_cast<float>(vy + (y_ndc + 1.0) * vh * 0.5);",
     "Rasterizador.TrianguloConhecidoDaExactamenteSeisPixels:Rasterizador.ODesenhoEscreveNaTelaQueABateriaLe"),
    ("V3 os pesos da cor interpolada trocam de aresta",
     "core/video/rasterizador.cpp",
     """      const double wc = e_ab / area;
      const double wa = e_bc / area;
      const double wb = e_ca / area;""",
     """      const double wc = e_bc / area;
      const double wa = e_ab / area;
      const double wb = e_ca / area;""",
     "Rasterizador.CorInterpoladaEntreVertices"),
    ("V4 o teste de profundidade e desligado por dentro",
     "core/video/rasterizador.cpp",
     "  if (e.teste_de_profundidade && profundidade_.size() > indice) {",
     "  if (false && profundidade_.size() > indice) {",
     "Rasterizador.OTesteDeProfundidadeDecideQuemFica"),
    ("V5 a coordenada de textura amostrada passa a ser a outra",
     "core/video/rasterizador.cpp",
     "  int tx = static_cast<int>(std::floor(u * static_cast<float>(t.largura)));",
     "  int tx = static_cast<int>(std::floor(v * static_cast<float>(t.largura)));",
     "Rasterizador.ATexturaAmostraOCantoCerto"),
    ("V6 o descarte de faces deixa de acontecer",
     "core/video/rasterizador.cpp",
     "  if (e.descartar_faces && (e.descartar_face == GL_FRONT_AND_BACK ||",
     "  if (false && (e.descartar_face == GL_FRONT_AND_BACK ||",
     "Rasterizador.ODescarteDeFacesUsaAOrientacaoEmNDC"),
    ("V7 o limite inferior da janela deixa de ser aplicado",
     "core/video/rasterizador.cpp",
     "  const int y1 = std::min(vp_fim_y, static_cast<int>(std::ceil(maxy)));",
     "  const int y1 = std::min(superficie_.Altura(), static_cast<int>(std::ceil(maxy)));",
     "Rasterizador.AJanelaLimitaORasto"),
    ("V8 o glClear deixa de por o clip de lado",
     "core/video/rasterizador.cpp",
     "  tela_->ClipLimpo();\n  tela_->CorAtual(rgb565);",
     "  tela_->CorAtual(rgb565);",
     "Rasterizador.OGlClearPintaATelaTodaEIgnoraOClip"),
    ("V9 uma primitiva sem caminho passa a ser desenhada como leque",
     "core/video/rasterizador.cpp",
     "  if (!triangulos) {\n    ++recusadas_;",
     "  if (false) {\n    ++recusadas_;",
     "Rasterizador.PrimitivaSemCaminhoRecusaComONome"),
    ("V10 o desenho sem tela deixa de ser recusado",
     "core/brew/igl.cpp",
     """      if (!destino_.Pronto()) {
        return recusa_com(3, "geometria submetida (" + std::to_string(quantos) +""",
     """      if (false) {
        return recusa_com(3, "geometria submetida (" + std::to_string(quantos) +""",
     "Rasterizador.SemTelaODesenhoERecusadoEComOMotivoEscrito"),
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
  codigo, resumo = correr(raiz, "Rasterizador.*")
  print(f"RESTAURADO: {resumo}")
  if codigo != 0:
    falhas += 1
  print(f"\nGUARDAS QUE NAO FICARAM VERMELHAS: {falhas}")
  return 1 if falhas else 0


if __name__ == "__main__":
  sys.exit(main())
