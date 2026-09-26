#!/usr/bin/env python3
"""Rekey residue_classify.py's per-site overrides by the key's PROVENANCE
(as `cryo build --emit=facts` reports it) instead of the argument's source
text, each old key mapped through the site's line in the old and new
populations.  Each replacement must match exactly once.  The two sites the
old text key `sym` covered in bindgen/importer.cryo become two keys, the
second added by hand beside this script's run.
"""
import io, os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
PATH = os.path.join(ROOT, "scripts", "ns-migration", "residue_classify.py")
INTERN = "call:compiler::resolver::intern_table::InternTable.intern(mut &this, string) -> compiler::resolver::symbol_str::SymbolStr of "

KEYS = [
    ('"n"):', '"local:n"):'),
    ('"DeclarationIndex::lookup_type", "name"):', '"DeclarationIndex::lookup_type", "param:name"):'),
    ('"owner, leaf"):', '"param:leaf"):'),
    ('"owner, method_sym"):', '"param:method_sym"):'),
    ("'owner_ref, this.intern.intern(\"iter\")'):", "'" + INTERN + "literal:\"iter\"'):"),
    ("'this.intern.intern(\"Ready\")'):", "'" + INTERN + "literal:\"Ready\"'):"),
    ("'this.intern.intern(\"__call__\")'):", "'" + INTERN + "literal:\"__call__\"'):"),
    ('"StructType::get_method", " ..."):', '"StructType::get_method", \'' + INTERN + 'literal:"__call__"\'):'),
    ('"param.name.id"):',
     '"field:compiler::resolver::symbol_str::SymbolStr.id<-field:compiler::ast::declaration::VarDeclNode*.name'
     '<-local:param@1=element@1:field:compiler::ast::declaration::FunctionDeclNode*.parameters<-param:node"):'),
]
BINDING = ('"field:compiler::resolver::symbol_str::SymbolStr.id<-field:compiler::ast::declaration::DestructureBinding.local_name'
           '<-local:b@1=element@1:field:compiler::ast::declaration::DestructureDeclNode*.bindings<-param:node"):')

EDITS = [
    ('# {(file, "Holder::method", argument text): (class, reason)} - a site whose\n'
     '# key comes from somewhere the method\'s other callers\' do not.',
     '# {(file, "Holder::method", key provenance): (class, reason)} - a site whose\n'
     '# key comes from somewhere the method\'s other callers\' do not.  The key is\n'
     '# where the compiler reports the key argument\'s value comes from\n'
     '# (`residue.py`, from `cryo build --emit=facts`).'),
    ('    lines.append("| site | read | key as written | class | reason |")',
     '    lines.append("| site | read | key\'s provenance | class | reason |")'),
]


def main():
    s = io.open(PATH, encoding="utf-8").read()
    for old, new in KEYS:
        if s.count(old) != 1:
            print("rekey: %d of %r" % (s.count(old), old[:60])); return 1
        s = s.replace(old, new)
    if s.count('"b.local_name.id"):') != 2:
        print("rekey: binding keys"); return 1
    s = s.replace('"b.local_name.id"):', BINDING)
    for old, new in EDITS:
        if s.count(old) != 1:
            print("rekey: %d of %r" % (s.count(old), old[:60])); return 1
        s = s.replace(old, new)
    io.open(PATH, "w", encoding="utf-8", newline="\n").write(s)
    print("rekey: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
