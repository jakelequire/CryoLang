"""The examples half of verify's comparison with the gate's builds capped at
two at once.  usage: python seqexamples.py <baseline cryo> <tree cryo> <outdir>"""
import os, sys, shutil, subprocess, importlib.util

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
spec = importlib.util.spec_from_file_location('verify', os.path.join(ROOT, 'scripts', 'verify.py'))
verify = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verify)

base_exe, tree_exe, out = sys.argv[1], sys.argv[2], sys.argv[3]
os.makedirs(out, exist_ok=True)
env = dict(os.environ)
env.pop('CRYO_STDLIB', None)
if os.name == 'nt':
    env.setdefault('CRYO_CC', 'gcc')
for tag, exe in (('base', base_exe), ('tree', tree_exe)):
    for d in os.listdir(os.path.join(ROOT, 'examples')):
        shutil.rmtree(os.path.join(ROOT, 'examples', d, 'build'), ignore_errors=True)
    log = os.path.join(out, '%s.examples.log' % tag)
    with open(log, 'wb') as fh:
        rc = subprocess.run([verify.PY, 'scripts/examples-gate.py', '--cryo', exe,
                             '--stdlib', verify.STDLIB, '--jobs', '2'],
                            cwd=ROOT, env=env, stdout=fh, stderr=subprocess.STDOUT).returncode
    verdict = [l for l in open(log, encoding='utf-8', errors='replace') if l.startswith('examples-gate: ')]
    print('%s rc=%d %s' % (tag, rc, verdict[-1].strip() if verdict else '<no verdict>'), flush=True)
    print('  objects %d' % verify.hash_objects('examples', os.path.join(out, '%s.examples.sha256' % tag)))
verify.compare('examples', os.path.join(out, 'base.examples.sha256'),
               os.path.join(out, 'tree.examples.sha256'), 40)
