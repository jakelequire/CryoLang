"""verify.py's population and comparison, one gate at a time, for a machine
too short of memory to run the census and the examples sweep together.

usage: python seqverify.py <baseline cryo.exe> <tree cryo.exe> <outdir>
"""
import os, sys, importlib.util

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
env.setdefault('CRYO_TEST_JOBS', '2')

ok = True
for tag, exe in (('base', base_exe), ('tree', tree_exe)):
    print('seqverify: %s = %s' % (tag, exe), flush=True)
    verify.wipe_population()
    results = [verify.run_gate(n, exe, out, env) for n in verify.POPULATION_GATES]
    for r in results:
        os.replace(r['log'], os.path.join(out, '%s.%s.log' % (tag, r['name'])))
        r['log'] = os.path.join(out, '%s.%s.log' % (tag, r['name']))
    verify.report_gates(results)
    ok = ok and all(r['ok'] for r in results)
    for b in ('tests', 'examples'):
        n = verify.hash_objects(b, os.path.join(out, '%s.%s.sha256' % (tag, b)))
        print('  objects   %-8s %d' % (b, n), flush=True)
        if n == 0:
            ok = False
print('seqverify: objects, base against tree')
diff = 0
for b in ('tests', 'examples'):
    diff += verify.compare(b, os.path.join(out, 'base.%s.sha256' % b),
                           os.path.join(out, 'tree.%s.sha256' % b), 40)
print('seqverify: %s' % ('OK' if ok else 'FAIL (a gate failed or hashed nothing)'))
