"""Builds one compiler per mutation of the unbounded-receiver check, each
alone over an otherwise unmutated tree, and restores the source after
each build whatever its outcome.  Binaries land in <out>/cryo-<name>.exe.

usage: python scripts/ns-migration/8.420/mutants.py <out-dir> [name ...]
"""
import os, sys, shutil, subprocess

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
MB = 'compiler/src/compiler/sema/method_binding.cryo'
CR = 'compiler/src/compiler/sema/call_resolver.cryo'
MUTANTS = {
    # the check never finds an unbounded receiver
    'off': (MB, 'if (t == null || t.kind != TypeKind::GenericParam) { return null; }',
                'if (true) { return null; }'),
    # the explicit destructor call is checked like any method call
    'drop': (CR, 'if (!this.is_explicit_drop_call(call)) {\n                const unbounded',
                 'if (true) {\n                const unbounded'),
    # the owner declaration's bounds are not consulted
    'owner': (MB, 'if (owner_bounds != null && MethodBinding::bounds_name_param(owner_bounds, param)) { return null; }',
                  'if (false) { return null; }'),
    # the function's own `where` clause is not consulted
    'func': (MB, 'if (MethodBinding::bounds_name_param(&func.trait_bounds, param)) { return null; }',
                 'if (false) { return null; }'),
}

def main():
    out = os.path.abspath(sys.argv[1])
    names = sys.argv[2:] or list(MUTANTS)
    os.makedirs(out, exist_ok=True)
    for name in names:
        rel, old, new = MUTANTS[name]
        path = os.path.join(ROOT, rel)
        orig = open(path, encoding='utf-8', newline='').read()
        norm = orig.replace('\r\n', '\n')
        if norm.count(old) != 1:
            sys.exit('%s: mutation site found %d times in %s' % (name, norm.count(old), rel))
        crlf = '\r\n' in orig
        mutated = norm.replace(old, new)
        if crlf: mutated = mutated.replace('\n', '\r\n')
        try:
            open(path, 'w', encoding='utf-8', newline='').write(mutated)
            log = os.path.join(out, 'build-%s.log' % name)
            with open(log, 'w') as f:
                rc = subprocess.call(['make', 'cryo'], cwd=ROOT, stdout=f, stderr=subprocess.STDOUT)
            print('%s: make cryo exit %d' % (name, rc), flush=True)
            if rc == 0:
                shutil.copy(os.path.join(ROOT, 'compiler/build/cryo.exe'), os.path.join(out, 'cryo-%s.exe' % name))
        finally:
            open(path, 'w', encoding='utf-8', newline='').write(orig)
    print('MUTANTS_DONE', flush=True)

main()
