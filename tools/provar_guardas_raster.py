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
  V11 a mistura olha para o destino (o `GL_BLEND` com os factores do GL)
  V12 o alpha test descarta o fragmento
  V13 a mascara de cor preserva o canal que proibe
  V14 a iluminacao decide a cor do vertice
  V15 o alfa iluminado vem da difusa do material
  V16 o `GL_COLOR_MATERIAL` troca a cor do material pela do vertice
  V17 a posicao da luz e guardada no espaco do olho
  V18 a tabela `por_fazer` nao fica com o que ja se faz
  V19 o recorte do plano proximo do SEGMENTO (o segmento que cruza e cortado, e nao descartado)
  V20 a ponta final ABERTA de um segmento (o vertice partilhado escreve um pixel, e nao dois)
  V21 a largura de linha > 1 recusa o desenho (e nao desenha 1 px a fingir 2)
  V22 a largura de linha chega ao rasterizador pela cablagem (`Igl::MontarEstado`)
  V23 GL_LINEAR nao volta a GL_NEAREST em silencio

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
     "  v->y = static_cast<float>(vy_topo + (1.0 - y_ndc) * vh * 0.5);",
     "  v->y = static_cast<float>(vy_topo + (y_ndc + 1.0) * vh * 0.5);",
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
    ("V5 a coordenada de textura NEAREST passa a ser a outra",
     "core/video/rasterizador.cpp",
     "    return texel(static_cast<int>(std::floor(u * static_cast<float>(t.largura))),",
     "    return texel(static_cast<int>(std::floor(v * static_cast<float>(t.largura))),",
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
     "  if (!triangulos && !linhas) {\n    ++recusadas_;",
     "  if (false) {\n    ++recusadas_;",
     "Rasterizador.PrimitivaSemCaminhoRecusaComONome"),
    ("V10 o desenho sem tela deixa de ser recusado",
     "core/brew/igl.cpp",
     """      if (!destino_.Pronto()) {
        return recusa_com(3, "geometria submetida (" + std::to_string(quantos) +""",
     """      if (false) {
        return recusa_com(3, "geometria submetida (" + std::to_string(quantos) +""",
     "Rasterizador.SemTelaODesenhoERecusadoEComOMotivoEscrito"),
    ("V11 a mistura deixa de ler o destino (escreve a origem)",
     "core/video/rasterizador.cpp",
     "  if (e.mistura_ligada || !mascara_limpa) {",
     "  if (false && !mascara_limpa) {",
     "Rasterizador.AMisturaSomaOrigemEDestinoComOsFactoresDoGL:Rasterizador.OMisturaChegaATelaPelaCablagemDoIglesSemFalta"),
    ("V12 o alpha test deixa de descartar",
     "core/video/rasterizador.cpp",
     "    if (!Comparar(e.funcao_de_alfa, alfa, e.alfa_de_referencia)) {",
     "    if (false) {",
     "Rasterizador.OAlphaTestDescartaOFragmentoAntesDeEscrever"),
    ("V13 a mascara de cor deixa de preservar o canal que proibe",
     "core/video/rasterizador.cpp",
     "      if ((e.mascara_de_cor & 0x2u) != 0) mascara |= 0x07E0u;  // verde",
     "      mascara |= 0x07E0u;  // verde",
     "Rasterizador.AMascaraDeCorPreservaOCanalQueProibe"),
    ("V14 a iluminacao deixa de decidir a cor do vertice",
     "core/video/rasterizador.cpp",
     "  if (e.iluminacao_ligada) {",
     "  if (false) {",
     "Rasterizador.ALuzDecideACorDoVerticePeloCossenoDaNormal"),
    ("V15 o alfa iluminado passa a ser 1 em vez da difusa do material",
     "core/video/rasterizador.cpp",
     "  r.a = canal(difusa[3]);",
     "  r.a = 255;",
     "Rasterizador.OAlfaIluminadoVemDaDifusaDoMaterial"),
    ("V16 o GL_COLOR_MATERIAL deixa de trocar a cor do material",
     "core/video/rasterizador.cpp",
     "  if (e.cor_do_material) {",
     "  if (false) {",
     "Rasterizador.OColorMaterialPoeACorDoVerticeNoLugarDaAmbienteEDaDifusa"),
    ("V17 a posicao da luz deixa de ir para o espaco do olho",
     "core/brew/igl.cpp",
     "        if (pname == GL_POSITION || pname == GL_SPOT_DIRECTION) {",
     "        if (false) {",
     "Rasterizador.ALuzChegaAoMotorPelaCablagemEAPosicaoVaiParaOEspacoDoOlho"),
    ("V18 a tabela `por_fazer` volta a dizer que falta o que ja se faz",
     "core/brew/igl.cpp",
     """      {GL_DITHER, "dithering_sem_rasterizador"},""",
     """      {GL_DITHER, "dithering_sem_rasterizador"},
      {GL_BLEND, "blending_de_GL_sem_rasterizador"},""",
     "Rasterizador.OQueSeImplementouSaiuDaTabelaDasFaltas"),
    ("V19 o recorte do plano proximo do SEGMENTO deixa de acontecer",
     "core/video/rasterizador.cpp",
     "  if (a_dentro != b_dentro) {\n    const double t = d0 / (d0 - d1);",
     "  if (false) {\n    const double t = d0 / (d0 - d1);",
     "Rasterizador.SegmentoTodoAtrasDoPlanoProximoEDescartado"),
    ("V20 a ponta final dos segmentos passa a ser FECHADA (o pixel do vertice partilhado conta duas vezes)",
     "core/video/rasterizador.cpp",
     "    if (k == passos && !incluir_fim) break;",
     "    if (false) break;",
     "Rasterizador.LinhaHorizontalDeSeisPixelsEscreveSeisPixels:Rasterizador.FaixaDeTresPontosColinearesNaoContaOPixelDuasVezes"),
    ("V21 a largura de linha > 1 deixa de ser recusada (desenha 1 px a fingir 2)",
     "core/video/rasterizador.cpp",
     "  if (linhas && estado.largura_de_linha > 1.0f) {",
     "  if (false) {",
     "Rasterizador.LarguraDeLinhaMaiorQueUmRecusaComONome"),
    ("V22 a largura de linha deixa de chegar ao rasterizador (o valor fica guardado e nao aplicado)",
     "core/brew/igl.cpp",
     "    e.largura_de_linha = RealDoFixo((*largura)[0]);",
     "    e.largura_de_linha = 1.0f;",
     "Rasterizador.AsLinhasChegamATelaPelaCablagemDoIglEALarguraEGuardada"),
    ("V23 GL_LINEAR volta a amostrar NEAREST",
     "core/video/rasterizador.cpp",
     "  if (!t.filtro_linear) {",
     "  if (true) {",
     "Rasterizador.TexturaLinearRGB565Interpola2x2EPrendeNasBordas"),
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
