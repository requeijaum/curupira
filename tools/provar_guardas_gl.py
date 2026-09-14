#!/usr/bin/env python3
"""PROVA CADA GUARDA POR VIOLACAO DELIBERADA.

Uma guarda que passa sem a mudanca nao e guarda. Para cada violacao: quebra-se a
coisa de proposito, constroi-se e corre-se o teste que a deve apanhar; o resultado
esperado e VERMELHO. Depois restaura-se.

Uso: python3 tools/provar_guardas_gl.py <raiz_da_arvore>
"""
import shutil
import subprocess
import sys
from pathlib import Path

# (nome, ficheiro, de, para, filtro gtest)
VIOLACOES = [
    ("V1 o ramo do GL deixa de ser especifico (o erro de ordem)",
     "core/brew/despacho.cpp",
     "} else if (idx >= kVtableIgl && idx < kVtableIgl + gl_slots::kIglSlots) {",
     "} else if (false && idx >= kVtableIgl && idx < kVtableIgl + gl_slots::kIglSlots) {",
     "CablagemGl.ODespachoEntregaAChamadaAoIgl:CablagemGl.AORDEMDoRamoEAGuardaDoOitavoCaso"),
    ("V2 a faixa do IGL volta para dentro da faixa da entrada",
     "core/brew/igl.h",
     "constexpr std::uint32_t kVtableIgl = 30000;",
     "constexpr std::uint32_t kVtableIgl = 20000;",
     "CablagemGl.AsFaixasNaoSeSobrepoem:CablagemGl.AEntradaEOGlNaoPartilhamNemFaixaNemArmazenamento"),
    ("V3 o config passa a prometer 8 bits de vermelho (o config de folheto)",
     "core/brew/egl.cpp",
     '{EGL_RED_SIZE, 5, "tela.h: CorAtual(rgb565) -- 5 bits de vermelho"},',
     '{EGL_RED_SIZE, 8, "tela.h: CorAtual(rgb565) -- 5 bits de vermelho"},',
     "ConfigDoEgl.OsValoresSaoOsDaTela:CicloDoEgl.OEscolheConfigComOPedidoMedidoNoCorpus"),
    ("V4 o eglGetError deixa de consumir o erro",
     "core/brew/egl.cpp",
     "      erro_ = EGL_SUCCESS;  // a leitura CONSOME o erro, como o EGL define",
     "      // (violacao deliberada: o erro deixa de ser consumido)",
     "CicloDoEgl.OGetErrorConsomeOErro"),
    # A PRIMEIRA VERSAO DESTA VIOLACAO NAO MUDOU NADA: escrevia `*retorno = 0` no
    # ramo do `EGL_VENDOR`, e a ultima linha do ramo (`feito_com`) volta a escrever
    # o `r0` por cima. O teste ficou VERDE -- e ficou verde com razao, porque a
    # violacao era um no-op. **Uma violacao que nao muda o comportamento nao prova
    # guarda nenhuma**; a que fica e a que devolve o nulo mesmo.
    ("V5 o eglQueryString passa a devolver nulo no EGL_VENDOR",
     "core/brew/egl.cpp",
     """      if (qual == EGL_VENDOR) {
        EscreverString(1, "");
        return feito_com(2, kZonaDeStrings + 1 * kPassoDeString,""",
     """      if (qual == EGL_VENDOR) {
        EscreverString(1, "");
        return feito_com(2, 0,""",
     "CicloDoEgl.OQueryStringNuncaDevolveNulo"),
    ("V6 uma chamada deixa de deixar linha no traco (o caminho mudo)",
     "core/brew/egl.cpp",
     """    c.resultado = ResultadoGl::Feito;
    Registar(c);
    if (retorno != nullptr) *retorno = r0;
    return ResultadoGl::Feito;
  };
  const auto feito_com""",
     """    c.resultado = ResultadoGl::Feito;
    if (retorno != nullptr) *retorno = r0;
    return ResultadoGl::Feito;
  };
  const auto feito_com""",
     "NenhumCaminhoMudo.AsVinteEOitoChamadasFicamNoTraco"),
]


def correr(raiz: Path, filtro: str):
    r = subprocess.run([str(raiz / "build" / "zb2_tests"), "--gtest_filter=" + filtro],
                       capture_output=True, text=True)
    resumo = [l for l in r.stdout.splitlines() if l.startswith("[  PASSED  ]")
              or l.startswith("[  FAILED  ]")]
    return r.returncode, " | ".join(resumo)


def main() -> int:
    raiz = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path.cwd()
    falhas = 0
    for nome, rel, de, para, filtro in VIOLACOES:
        f = raiz / rel
        texto = f.read_text()
        if texto.count(de) != 1:
            print(f"{nome}: ANCORA NAO UNICA em {rel}")
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
    subprocess.run(["cmake", "--build", "build", "-j4", "--target", "zb2_tests"], cwd=raiz,
                   capture_output=True, text=True)
    codigo, resumo = correr(raiz, "CablagemGl.*:CicloDoEgl.*:ConfigDoEgl.*")
    print(f"RESTAURADO: {resumo}")
    if codigo != 0:
        falhas += 1
    print(f"\nGUARDAS QUE NAO FICARAM VERMELHAS: {falhas}")
    return 1 if falhas else 0


if __name__ == "__main__":
    sys.exit(main())
