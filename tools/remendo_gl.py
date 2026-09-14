#!/usr/bin/env python3
"""Aplica o REMENDO da cablagem do GL aos dois ficheiros partilhados do despacho.

PORQUE E UM SCRIPT, E NAO UM `.patch`. Um `.patch` de `diff` depende de numeros de
linha e de contexto exacto; se o dono do ficheiro mexer em duas linhas ao lado, ele
recusa -- ou pior, aplica no sitio errado e passa. Aqui cada alteracao e um par
(ancora, texto) com `assert` ANTES de escrever e `assert` DEPOIS sobre um texto que
so existe se a alteracao entrou. **Foi assim que se apanhou, neste trabalho, uma
substituicao que falhou em silencio e uma "verificacao" por `grep -c` que contava o
simbolo errado.**

O remendo esta descrito em `docs/rewrite/REMENDO-GL-DESPACHO.md`. Este script e a
versao executavel dele, e o comando que o aplica e o mesmo que o prova:

    python3 tools/remendo_gl.py <raiz_da_arvore>            # aplica
    python3 tools/remendo_gl.py <raiz_da_arvore> --reverter # desfaz

Devolve 0 se aplicou (ou ja estava aplicado), 1 se uma ancora nao existe (nesse caso
NADA foi escrito), 2 se o ficheiro ficou com uma contagem de linhas inesperada (uma
escrita que apaga -- o defeito do `bateria.cpp` truncado).
"""
import sys
from pathlib import Path

# --- despacho.h -------------------------------------------------------------
H_INCLUDE_ANCORA = """#include "core/brew/ajudantes.h\""""
H_INCLUDE_NOVO = """#include "core/brew/ajudantes.h"

// A CABLAGEM DO GL (etapa 6): o IGL e o IEGL. Ver docs/rewrite/REMENDO-GL-DESPACHO.md.
//
// ESTE `#define` E A GUARDA DO TESTE. O `tests/gl_cablagem_test.cpp` corre de
// verdade quando ele existe e SALTA com o motivo escrito quando nao existe -- e um
// teste que passa sem ter corrido e pior do que um teste vermelho.
#define ZB2_CABLAGEM_GL 1
#include "core/brew/egl.h"
#include "core/brew/igl.h\""""

H_MEMBRO_ANCORA = """  void DefinirVtableBitmap(std::uint32_t v) { vtable_bitmap_ = v; }"""
H_MEMBRO_NOVO = """  // --- O GL (etapa 6) ------------------------------------------------------
  //
  // O `Igl` e o `Egl` vivem AQUI, e nao na ferramenta: a cablagem e do motor.
  // `InstalarGl` escreve as duas vtables na faixa de saida e CONFIRMA com leitura
  // de volta (cada modulo ja faz a sua); e o `Correr` que entrega os pedidos.
  std::uint32_t InstalarGl(const Saidas& saidas);
  Igl& IglRef() { return igl_; }
  const Igl& IglRef() const { return igl_; }
  Egl& EglRef() { return egl_; }
  const Egl& EglRef() const { return egl_; }

  void DefinirVtableBitmap(std::uint32_t v) { vtable_bitmap_ = v; }"""

H_PRIVADO_ANCORA = """  std::int64_t agora_ms_ = 0;
  Temporizador timer_;"""
H_PRIVADO_NOVO = """  std::int64_t agora_ms_ = 0;
  Temporizador timer_;

  // O GL e o EGL. Depois do que eles nao usam e antes de nada que os use: a ordem
  // de declaracao e a ordem de construcao.
  Igl igl_;
  Egl egl_;"""

# --- despacho.cpp -----------------------------------------------------------
C_INSTALAR_ANCORA = """  for (std::uint32_t off = 0; off < 117 * 4; off += 4) {
    if (off == 0x68 || off == 0x6c || ja_tem(off)) continue;"""
C_INSTALAR_NOVO = """  // O GL entra AQUI, junto da tabela de ajudantes: e o mesmo passo de construcao
  // do sistema, e nao um segundo caminho que alguem tem de lembrar de chamar.
  const std::uint32_t slots_gl = InstalarGl(saidas);
  if (slots_gl == 0) {
    traco_.RegistarFalta(Area::Video, "cablagem_do_GL",
                         "o IGL e/ou o IEGL nao cablaram; os pedidos de GL vao recusar");
  }

  for (std::uint32_t off = 0; off < 117 * 4; off += 4) {
    if (off == 0x68 || off == 0x6c || ja_tem(off)) continue;"""

# A LISTA DE INICIALIZACAO, TRANSCRITA como esta no ficheiro (e nao como eu a
# supunha): o `despacho.cpp` inicializa `arquivos_`, `sinais_` e `ihid_` nela.
C_CTOR_ANCORA = """      ihid_(mem, traco, sinais_, entrada_) {}"""
C_CTOR_NOVO = """      ihid_(mem, traco, sinais_, entrada_),
      igl_(mem, traco),
      egl_(mem, traco) {}

// A INSTALACAO DO GL. Devolve quantos slots foram cablados NO TOTAL (0 = falhou).
//
// AS FAIXAS SAO 30000 E 31000, e nao 20000/21000: a faixa 20000 esta ocupada pela
// ENTRADA (`tools/bateria.cpp` instala-a em 20000; 32 + 16 slots, de 20000 a 20047)
// e as duas escrevem nos MESMOS enderecos. Os numeros medidos estao no comentario
// das constantes em `core/brew/igl.h`.
std::uint32_t Despacho::InstalarGl(const Saidas& saidas) {
  const std::uint32_t a = igl_.Instalar(saidas);
  const std::uint32_t b = egl_.Instalar(saidas);
  if (a == 0 || b == 0) return 0;
  // DOIS OBJECTOS NO MESMO ENDERECO dariam uma vtable a servir as duas interfaces.
  // Cada `Instalar` confere a sua vtable; nada confere que os dois objectos sao
  // distintos, e um `if` custa menos do que uma ronda a olhar para o sitio errado.
  if (igl_.Objeto() == egl_.Objeto()) {
    traco_.RegistarFalta(Area::Video, "cablagem_do_GL",
                         "o IGL e o IEGL ficaram no mesmo endereco de objecto");
    return 0;
  }
  traco_.Emitir(Area::Video, Nivel::Informacao, "GL_CABLADO",
                "IGL em 0x800B0000 (faixa 30000) e IEGL em 0x800B1000 (faixa 31000)");
  return a + b;
}"""

# O RAMO DO GL, ANTES DO `idx >= kBaseDoShell`.
#
# O `idx >= kBaseDoShell` e 2000, e a faixa do GL e 30000/31000: o ramo generico
# ENGOLIA os dois e dava-lhes o NOME de um metodo do IFileMgr (`IFileMgr::slot23007`
# para o `glClear`). MEDIDO pela sonda antes deste remendo: 41 de 41 chamadas do
# `ddragonz` engolidas. E a OITAVA vez que este erro de ordem aparece no projeto.
C_RAMO_ANCORA = """      } else if (idx >= kBaseDoShell) {"""
C_RAMO_NOVO = """      } else if (idx >= kVtableIgl && idx < kVtableIgl + gl_slots::kIglSlots) {
        // O GL. ESTE RAMO VEM ANTES DO `idx >= kBaseDoShell`, e por isso e que ele
        // esta escrito AQUI e nao no fim da cadeia: a faixa e 30000+ e o ramo
        // generico (2000) engole-a e da-lhe o nome de um metodo do IFileMgr.
        // **Do mais especifico para o mais generico -- oito casos neste trabalho.**
        //
        // O `po` NAO E PASSADO: medido no `conftest.elf` (gli.h documenta as seis
        // instrucoes do `glCullFace`), o wrapper carrega um argumento por registo e
        // nao toca no r0. So os slots da cabeca (AddRef/Release/QueryInterface) o
        // recebem, e esses vao em `a.reg[0]`.
        ArgumentosGl av;
        for (int k2 = 0; k2 < 4; ++k2) av.reg[k2] = cpu.Get(kR0 + k2);
        av.sp = cpu.Get(kSP);
        av.lr = cpu.Get(kLR);
        std::uint32_t retorno = 0;
        igl_.Executar(idx - kVtableIgl, av, &retorno);
        cpu.Set(kR0, retorno);
      } else if (idx >= kVtableIegl && idx < kVtableIegl + gl_slots::kIeglSlots) {
        // O IEGL, pela mesma razao e com a mesma forma. Sem ele NENHUM `gl*` do
        // jogo acontece: o wrapper do SDK chama `eglInitialize`/`eglChooseConfig`/
        // `eglCreateWindowSurface`/`eglMakeCurrent` ANTES do primeiro `gl*`
        // (medido no `ddragonz.mod`, 0x11d6c4-0x11d890).
        ArgumentosGl av;
        for (int k2 = 0; k2 < 4; ++k2) av.reg[k2] = cpu.Get(kR0 + k2);
        av.sp = cpu.Get(kSP);
        av.lr = cpu.Get(kLR);
        std::uint32_t retorno = 0;
        egl_.Executar(idx - kVtableIegl, av, &retorno);
        cpu.Set(kR0, retorno);
      } else if (idx >= kBaseDoShell) {"""

C_CLSID_ANCORA = """        if (iid == kIidDisplay) devolver = zb2::brew::kObjDisplay;
        else if (iid == kIidFileMgr) devolver = zb2::brew::kObjFileMgr;"""
C_CLSID_NOVO = """        if (iid == kIidDisplay) devolver = zb2::brew::kObjDisplay;
        else if (iid == kIidFileMgr) devolver = zb2::brew::kObjFileMgr;
        // O GL E O EGL (AEEGL.h). O `ddragonz` cria um objecto com o AEECLSID_GL
        // (0x01014bc3) e passa o resultado como `gpIGL` (0x11d61c-0x11d634).
        // O AEECLSID_EGL (0x01014bc4) NAO aparece em nenhum dos 4 titulos com
        // wrapper -- esta linha vem do cabecalho e nao de uma medicao do corpus.
        else if (iid == zb2::brew::kClsidIgl) devolver = igl_.Objeto();
        else if (iid == zb2::brew::kClsidIegl) devolver = egl_.Objeto();"""

# A tabela de pares. A ORDEM importa: as ancoras nao se sobrepoem.
EDICOES = [
    ("core/brew/despacho.h", H_INCLUDE_ANCORA, H_INCLUDE_NOVO),
    ("core/brew/despacho.h", H_MEMBRO_ANCORA, H_MEMBRO_NOVO),
    ("core/brew/despacho.h", H_PRIVADO_ANCORA, H_PRIVADO_NOVO),
    ("core/brew/despacho.cpp", C_CTOR_ANCORA, C_CTOR_NOVO),
    ("core/brew/despacho.cpp", C_INSTALAR_ANCORA, C_INSTALAR_NOVO),
    ("core/brew/despacho.cpp", C_CLSID_ANCORA, C_CLSID_NOVO),
    ("core/brew/despacho.cpp", C_RAMO_ANCORA, C_RAMO_NOVO),
]

# A contagem de linhas ESPERADA por ficheiro, depois do remendo. E a unica
# verificacao que apanha uma escrita que apaga: um ficheiro truncado continua a ser
# um ficheiro valido em Python, e foi assim que um `bateria.cpp` de 1295 linhas
# virou 412 sem um aviso.
# MEDIDO: os ficheiros tinham 181 e 962 linhas antes do remendo (commit f461798) e
# ficam com 206 e 1030 depois. Os numeros nao sao decorativos: sao o que apanha uma
# escrita que apaga, e por isso o script RECUSA aplicar sobre uma versao em que eles
# ja nao batam certo.
ESPERADO = {
    "core/brew/despacho.h": (181, 206),
    "core/brew/despacho.cpp": (962, 1030),
}


def aplicar(raiz: Path, reverter: bool) -> int:
    for rel, ancora, novo in EDICOES:
        f = raiz / rel
        texto = f.read_text()
        # A ORDEM DESTES TESTES E O DEFEITO QUE QUASE ENTROU: o texto novo CONTEM a
        # ancora (acrescenta linhas a volta dela), portanto perguntar primeiro "a
        # ancora ainda esta la?" respondia SIM depois de aplicado e aplicava OUTRA
        # VEZ -- duas declaracoes do membro `Igl igl_;` no mesmo cabecalho. **Uma
        # verificacao tem de ser sobre o TEXTO NOVO, e nao sobre o antigo.**
        if not reverter:
            if novo in texto:
                continue  # ja aplicado
            if texto.count(ancora) != 1:
                print(f"ANCORA NAO ENCONTRADA (1 vez) em {rel}: {ancora.splitlines()[0][:70]!r}",
                      file=sys.stderr)
                return 1
            f.write_text(texto.replace(ancora, novo))
        else:
            if novo not in texto:
                continue  # ja revertido
            if texto.count(novo) != 1:
                print(f"TEXTO DO REMENDO NAO UNICO (1 vez) em {rel}", file=sys.stderr)
                return 1
            f.write_text(texto.replace(novo, ancora))
    for rel, (antes, depois) in ESPERADO.items():
        n = len((raiz / rel).read_text().splitlines())
        esperado = (antes, depois) if not reverter else (depois, antes)
        if n not in esperado:
            print(f"CONTAGEM DE LINHAS INESPERADA em {rel}: {n} (esperado {esperado})",
                  file=sys.stderr)
            return 2
    return 0


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    raiz = Path(sys.argv[1]).resolve()
    reverter = "--reverter" in sys.argv[2:]
    if not (raiz / "core/brew/despacho.cpp").is_file():
        print(f"{raiz} nao parece a arvore do emulador (falta core/brew/despacho.cpp)",
              file=sys.stderr)
        return 1
    codigo = aplicar(raiz, reverter)
    if codigo == 0:
        print(("REVERTIDO" if reverter else "APLICADO") + f": {raiz}")
    return codigo


if __name__ == "__main__":
    sys.exit(main())
