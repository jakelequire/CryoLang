#!/usr/bin/env python3
"""The two mechanical rewrites of the registry re-key's first stage (section
8.332), as they were applied to `compiler/src` at 78914c0e.

1. `get_template_by_type_id(<expr>.id)` -> `template_of_type(<expr>)`: the
   template door takes the type, not its numeric id.  One caller whose
   argument was not `<expr>.id` (`method_binding.cryo`, a `u64` local) was
   rewritten by hand.
2. `this.ctx.definition_id_of(<expr>)` -> `this.ctx.decl_index.def_of(<expr>,
   this.ctx.type_arena)`: the one door from a type to its definition moved
   from the compilation context onto the declaration index.

Idempotent: a tree already rewritten reports 0 and 0.  Run from the repo root.
"""
import glob
import re

TYPE_ID = re.compile(r'get_template_by_type_id\(([A-Za-z_][\w.]*)\.id\)')
DEF_OF_KEY = 'this.ctx.definition_id_of('


def rewrite_def_of(s):
    """Append the arena argument to every `definition_id_of(...)` call, the
    argument found by balancing parentheses."""
    s = s.replace(DEF_OF_KEY, 'this.ctx.decl_index.def_of(')
    key = 'this.ctx.decl_index.def_of('
    out, i, n = [], 0, 0
    while True:
        j = s.find(key, i)
        if j < 0:
            out.append(s[i:])
            break
        k, depth = j + len(key), 1
        while depth:
            depth += {'(': 1, ')': -1}.get(s[k], 0)
            k += 1
        tail = s[j + len(key):k - 1]
        if tail.endswith(', this.ctx.type_arena'):
            out.append(s[i:k])
        else:
            out.append(s[i:k - 1] + ', this.ctx.type_arena)')
            n += 1
        i = k
    return ''.join(out), n


def main():
    by_type = by_def = 0
    for p in glob.glob('compiler/src/**/*.cryo', recursive=True):
        s = open(p, encoding='utf-8', newline='').read()
        s2, n1 = TYPE_ID.subn(r'template_of_type(\1)', s)
        n2 = 0
        if DEF_OF_KEY in s2:
            s2, n2 = rewrite_def_of(s2)
        if n1 or n2:
            open(p, 'w', encoding='utf-8', newline='').write(s2)
        by_type += n1
        by_def += n2
    print('template_of_type: %d rewritten; def_of: %d rewritten' % (by_type, by_def))


if __name__ == '__main__':
    main()
