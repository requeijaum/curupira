#!/usr/bin/env python3
"""Mede o formato `.bar` do BREW. E o instrumento do P1 para `core/carga/bar.h`.

Tres subcomandos:

  censo <caminho> [<caminho> ...]
      Para cada `.bar` (ficheiro ou pasta, recursivo) confere as invariantes que
      `core/carga/bar.h` afirma e imprime o resultado. Sem isto, "320 de 320" no
      comentario do codigo seria uma afirmacao sem medicao.

  recurso <ficheiro.bar> <tipo> <id>
      Resolve um par (tipo, id) pela regra dos registos e imprime o indice, a
      faixa de bytes, o tamanho e, quando for `AEEResBlob`, o mime e o dado.

  desmontar <ficheiro.mod> <endereco> <tamanho>
      Desmonta ARM a volta de um endereco do guest. O `.mod` carrega em
      0x00100000 (medido: `tools/bateria.cpp`, `kBase`), portanto o
      deslocamento no ficheiro e `endereco - 0x100000`. Precisa de `capstone`,
      que no python3 do sistema existe (5.0.7) e no do agente nao.

Uso (caminhos reais):
  python3 tools/medir_bar.py censo "<SDK>" "<mods>/276212/pacmania.bar"
  python3 tools/medir_bar.py recurso "<mods>/276212/pacmania.bar" 6 5091
  /usr/bin/python3 tools/medir_bar.py desmontar "<mods>/276212/pacmania.mod" 0x110d8c 0x120
"""

import array
import os
import struct
import sys

CABECALHO = 32
DESLOCAMENTO_DOS_REGISTOS = 32


def ler_cabecalho(dd):
    # QUATRO u16 (versao, campo2, campo4, num_registos) e SEIS u32. Ler os
    # primeiros 32 bytes como 10 u32 da valores deslocados -- foi um defeito
    # deste instrumento, e "encontrou" uma centena de ficheiros em falta.
    (versao, campo2, campo4, num_registos) = struct.unpack_from('<4H', dd, 0)
    (off_registos, tam_registos, off_indices, num_ids, off_dados,
     campo28) = struct.unpack_from('<6I', dd, 8)
    return dict(versao=versao, campo2=campo2, campo4=campo4, num_registos=num_registos,
                off_registos=off_registos, tam_registos=tam_registos, off_indices=off_indices,
                num_ids=num_ids, off_dados=off_dados, campo28=campo28)


def ler_registos(dd, h):
    out = []
    for k in range(h['tam_registos'] // 8):
        out.append(struct.unpack_from('<4H', dd, h['off_registos'] + 8 * k))
    return out


def ler_deslocamentos(dd, h):
    nvalores = (h['off_dados'] - h['off_indices']) // 4
    return list(array.array('I', dd[h['off_indices']:h['off_indices'] + 4 * nvalores]))


def ler_ficheiro(caminho):
    dd = open(caminho, 'rb').read()
    if len(dd) < CABECALHO:
        raise ValueError('abreviado')
    h = ler_cabecalho(dd)
    return dd, h, ler_registos(dd, h), ler_deslocamentos(dd, h)


def conferir(caminho):
    """Devolve a lista de invariantes que FALHARAM. Vazia = tudo medido e igual."""
    try:
        dd, h, regs, desl = ler_ficheiro(caminho)
    except Exception as e:
        return ['nao foi possivel ler: %s' % e], None
    falhas = []
    n = len(dd)
    if (h['versao'], h['campo2'], h['campo4']) != (0x0011, 1, 1):
        falhas.append('assinatura %04x %04x %04x' % (h['versao'], h['campo2'], h['campo4']))
    if h['tam_registos'] % 8:
        falhas.append('tam_registos=%d nao e multiplo de 8' % h['tam_registos'])
    if h['off_registos'] != DESLOCAMENTO_DOS_REGISTOS:
        falhas.append('off_registos=%d' % h['off_registos'])
    if h['tam_registos'] != 8 * h['num_registos']:
        falhas.append('tam_registos=%d num_registos=%d' % (h['tam_registos'], h['num_registos']))
    if h['off_indices'] != h['off_registos'] + h['tam_registos']:
        falhas.append('off_indices=%d' % h['off_indices'])
    nvalores = (h['off_dados'] - h['off_indices']) // 4
    if nvalores != h['num_ids'] + 1:
        falhas.append('%d valores de indice para %d ids' % (nvalores, h['num_ids']))
    soma = sum(r[2] + 1 for r in regs)
    if soma != h['num_ids']:
        falhas.append('soma(delta+1)=%d num_ids=%d' % (soma, h['num_ids']))
    if desl and desl[0] != h['off_dados']:
        falhas.append('deslocamento[0]=%d off_dados=%d' % (desl[0], h['off_dados']))
    if desl and desl[-1] != n:
        falhas.append('ultimo deslocamento=%d tamanho=%d' % (desl[-1], n))
    for k in range(1, len(desl)):
        if desl[k] < desl[k - 1]:
            falhas.append('deslocamentos nao monotonos em %d' % k)
            break
    for k, r in enumerate(regs):
        if r[3] + r[2] >= len(desl) - 1:
            falhas.append('registo %d aponta para fora da tabela' % k)
    return falhas, (dd, h, regs, desl)


def procurar(h, regs, tipo, ident):
    for (t, primeiro_id, delta, primeiro_indice) in regs:
        if t == tipo and primeiro_id <= ident <= primeiro_id + delta:
            return primeiro_indice + (ident - primeiro_id)
    return None


def censo(caminhos):
    ficheiros = []
    for c in caminhos:
        if os.path.isdir(c):
            for raiz, _, nomes in os.walk(c):
                ficheiros += [os.path.join(raiz, no) for no in nomes if no.endswith('.bar')]
        else:
            ficheiros.append(c)
    sem_falhas = 0
    sem_falha_campo28 = 0
    for f in sorted(ficheiros):
        falhas, dados = conferir(f)
        if dados is not None:
            h = dados[1]
            if h['off_dados'] + h['campo28'] == len(dados[0]):
                sem_falha_campo28 += 1
        if falhas:
            print('FALHA  %s -> %s' % (f, '; '.join(falhas)))
        else:
            sem_falhas += 1
    print('== %d de %d ficheiros com o formato medido ==' % (sem_falhas, len(ficheiros)))
    print('== campo 28 == off_dados + tamanho do ficheiro em %d de %d (nao e invariante; ver bar.h) =='
          % (sem_falha_campo28, len(ficheiros)))
    return 0 if sem_falhas == len(ficheiros) and ficheiros else 1


def recurso(caminho, tipo, ident):
    dd, h, regs, desl = ler_ficheiro(caminho)
    print('ficheiro %s: %d bytes, %d registos, %d ids, %d recursos'
          % (caminho, len(dd), len(regs), h['num_ids'], len(desl) - 1))
    for r in regs:
        print('  registo tipo=%d primeiro_id=%d delta=%d primeiro_indice=%d -> ids %d..%d, indices %d..%d'
              % (r[0], r[1], r[2], r[3], r[1], r[1] + r[2], r[3], r[3] + r[2]))
    i = procurar(h, regs, tipo, ident)
    if i is None:
        print('  (tipo=%d, id=%d) NAO esta em registo nenhum' % (tipo, ident))
        return 1
    inicio, fim = desl[i], desl[i + 1]
    dados = dd[inicio:fim]
    print('  (tipo=%d, id=%d) -> indice %d, ficheiro[%d .. %d) = %d bytes'
          % (tipo, ident, i, inicio, fim, len(dados)))
    print('  primeiros 32 bytes: %s' % dados[:32].hex(' '))
    if len(dados) >= 4 and dados[1] == 0:
        deslocamento = dados[0]
        fim_mime = dados.find(b'\x00', 2)
        if fim_mime > 0 and deslocamento <= len(dados):
            print('  AEEResBlob: bDataOffset=%d mime=%r dado=%d bytes: %s'
                  % (deslocamento, dados[2:fim_mime].decode('latin-1'), len(dados) - deslocamento,
                     dados[deslocamento:deslocamento + 16].hex(' ')))
    return 0


def desmontar(caminho, endereco, tamanho, base=0x00100000):
    try:
        import capstone
    except ImportError:
        print('capstone nao existe neste python3; usa /usr/bin/python3', file=sys.stderr)
        return 77
    dd = open(caminho, 'rb').read()
    inicio = endereco - base
    if inicio < 0 or inicio + tamanho > len(dd):
        print('endereco fora do ficheiro', file=sys.stderr)
        return 1
    md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM)
    for ins in md.disasm(dd[inicio:inicio + tamanho], endereco):
        print('%08x  %-8s %-8s %s' % (ins.address, ins.bytes.hex(), ins.mnemonic, ins.op_str))
    return 0


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    ordem = argv[1]
    if ordem == 'censo':
        return censo(argv[2:])
    if ordem == 'recurso':
        return recurso(argv[2], int(argv[3], 0), int(argv[4], 0))
    if ordem == 'desmontar':
        return desmontar(argv[2], int(argv[3], 0), int(argv[4], 0))
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
