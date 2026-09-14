#!/usr/bin/env python3
# Runner de mutantes: aplica uma mudanca, constroi, corre o teste, restaura.
import argparse, hashlib, json, os, subprocess, time

def sha(p):
    return hashlib.sha256(open(p, 'rb').read()).hexdigest()

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--raiz', required=True)
    ap.add_argument('--build', required=True)
    ap.add_argument('--mutantes', default='/tmp/mutantes.json')
    ap.add_argument('--ids', required=True)
    ap.add_argument('--saida', required=True)
    a = ap.parse_args()
    todos = {x['id']: x for x in json.load(open(a.mutantes))}
    ids = [int(t) for t in a.ids.split(',') if t.strip()]
    out = open(a.saida, 'a')
    for i in ids:
        x = todos[i]
        p = os.path.join(a.raiz, x['file'])
        original = open(p).read()
        h0 = sha(p)
        n = original.count(x['old'])
        if n != 1:
            reg = dict(id=i, resultado='ANCORA_INVALIDA', ocorrencias=n, esperado=x['esperado'], nota=x['nota'], file=x['file'])
            out.write(json.dumps(reg) + chr(10)); out.flush()
            print(i, 'ANCORA_INVALIDA', n, flush=True)
            continue
        aberto = original.replace(x['old'], x['new'])
        assert aberto != original, 'a substituicao nao mudou nada'
        open(p, 'w').write(aberto)
        t0 = time.time()
        b = subprocess.run(['cmake', '--build', a.build, '-j4', '--target', 'zb2_tests'], cwd=a.raiz, capture_output=True, text=True)
        if b.returncode != 0:
            reg = dict(id=i, resultado='NAO_COMPILA', esperado=x['esperado'], nota=x['nota'], file=x['file'], saida=(b.stdout[-1500:] + b.stderr[-1500:]))
        else:
            r = subprocess.run([os.path.join(a.build, 'zb2_tests'), '--gtest_filter=' + x['filt']], cwd=a.raiz, capture_output=True, text=True)
            res = 'VERMELHO' if r.returncode != 0 else 'VERDE'
            falhas = [l for l in (r.stdout + r.stderr).split(chr(10)) if l.startswith('[  FAILED  ]')]
            reg = dict(id=i, resultado=res, esperado=x['esperado'], bate=(res == x['esperado']), nota=x['nota'], file=x['file'], filtro=x['filt'], falhas=falhas[:4], segundos=round(time.time() - t0, 1))
        subprocess.run(['git', 'checkout', '--', x['file']], cwd=a.raiz, check=True)
        if sha(p) != h0:
            reg['RESTAURO'] = 'FALHOU'
        out.write(json.dumps(reg) + chr(10)); out.flush()
        print(i, reg['resultado'], 'esperado', reg['esperado'], reg.get('falhas'), flush=True)
    out.close()

main()
