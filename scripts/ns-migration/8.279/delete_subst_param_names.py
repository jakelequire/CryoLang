#!/usr/bin/env python3
"""`ASTTypeSubstituter.param_names` deleted: the parameters' spellings, kept
beside `param_syms` (the key since the substitution chain was keyed by
SymbolID) and read by nothing - stored by the constructor, never consulted.
The four callers stop building the copy they handed it.  Rule 1b of the lane
gate (an array of names on a type that takes a name) surfaced the type; the
grep for its readers found none.

    python scripts/ns-migration/8.279/delete_subst_param_names.py

Every replacement asserts its count before anything is saved.
"""
import os

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
P = os.path.join(ROOT, "compiler", "src", "compiler")

EDITS = {
    "AST/substituter.cryo": [
        ('''    spec_name:         SymbolStr;         // "Array_i32"
    /// The parameters' spellings, index-aligned with `param_syms` and the
    /// substitution's replacements; a display, never a key.
    param_names:       SymbolStr[];       // ["T"]
''', '''    spec_name:         SymbolStr;         // "Array_i32"
'''),
        ('''                       base_name: SymbolStr, spec_name: SymbolStr,
                       param_names: SymbolStr[], param_syms: SymbolID[],
                       type_arg_displays: SymbolStr[],
''', '''                       base_name: SymbolStr, spec_name: SymbolStr,
                       param_syms: SymbolID[],
                       type_arg_displays: SymbolStr[],
'''),
        ('''        this.spec_name         = spec_name;
        this.param_names       = param_names;
        this.param_syms        = param_syms;
''', '''        this.spec_name         = spec_name;
        this.param_syms        = param_syms;
'''),
        ('''    /// True if `args` is a leading prefix of the outer type's param
    /// names (each `args[i]` is exactly `Named(param_names[i])`). Allows
''', '''    /// True if `args` is a leading prefix of the outer type's parameters
    /// (each `args[i]` is a `Named` stamped with `param_syms[i]`). Allows
'''),
    ],
    "mono/call_specializer.cryo": [
        ('''        mut pn_owned: SymbolStr[] = [];
        mut ps_owned: SymbolID[] = [];
        for (mut i: i64 = 0; i < param_names.length; i++) {
            pn_owned.push(param_names[i]);
            ps_owned.push(param_syms[i]);
        }
''', '''        mut ps_owned: SymbolID[] = [];
        for (mut i: i64 = 0; i < param_syms.length; i++) {
            ps_owned.push(param_syms[i]);
        }
'''),
        ('''            empty_sym, empty_sym, pn_owned, ps_owned, arg_displays,
''', '''            empty_sym, empty_sym, ps_owned, arg_displays,
'''),
    ],
    "mono/specializer.cryo": [
        ('''        mut pn_copy: SymbolStr[] = [];
        mut ps_copy: SymbolID[] = [];
        for (mut pi: i64 = 0; pi < entry.param_names.length; pi++) {
            pn_copy.push(entry.param_names[pi]);
            ps_copy.push(entry.param_syms[pi]);
        }
''', '''        mut ps_copy: SymbolID[] = [];
        for (mut pi: i64 = 0; pi < entry.param_syms.length; pi++) {
            ps_copy.push(entry.param_syms[pi]);
        }
'''),
        ('''            pn_copy, ps_copy, disp_copy, spec_typeref
''', '''            ps_copy, disp_copy, spec_typeref
'''),
        ('''        mut names: SymbolStr[] = [];
        mut syms: SymbolID[] = [];
        mut displays: SymbolStr[] = [];
''', '''        mut syms: SymbolID[] = [];
        mut displays: SymbolStr[] = [];
'''),
        ('''                    names.push(named.name);
                    syms.push(sym);
''', '''                    syms.push(sym);
'''),
        ('''            names, syms, displays, spec_typeref);
''', '''            syms, displays, spec_typeref);
'''),
    ],
    "mono/trait_specializer.cryo": [
        ('''        mut pnames: SymbolStr[] = [];
        mut psyms: SymbolID[] = [];
        for (mut i: i64 = 0; i < decl.derived_param_names.length; i++) {
            pnames.push(decl.derived_param_names[i]);
            psyms.push(decl.derived_param_syms[i]);
        }
''', '''        mut psyms: SymbolID[] = [];
        for (mut i: i64 = 0; i < decl.derived_param_syms.length; i++) {
            psyms.push(decl.derived_param_syms[i]);
        }
'''),
        ('''            pnames, psyms, arg_displays, TypeRef::invalid());
''', '''            psyms, arg_displays, TypeRef::invalid());
'''),
    ],
}


def main():
    for rel, pairs in EDITS.items():
        path = os.path.join(P, rel)
        with open(path, "r", encoding="utf-8", newline="") as f:
            s = f.read()
        for old, new in pairs:
            n = s.count(old)
            assert n == 1, "%s: %d match(es) of %r" % (rel, n, old[:60])
            s = s.replace(old, new)
        with open(path, "w", encoding="utf-8", newline="") as f:
            f.write(s)
        print("%s: %d edit(s)" % (rel, len(pairs)))


if __name__ == "__main__":
    main()
