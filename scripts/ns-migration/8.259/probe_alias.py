"""Every answer the name layer gives through an `extern module` alias's
module, printed where it is given:

    SHADOW<TAB>ALIAS-SCOPE<TAB><file>:<line><TAB><alias ns>::<member>
        a scope segment bound to the alias, stamped Def(<mod>::alias)
    SHADOW<TAB>ALIAS-PATH<TAB><file>:<line><TAB><declaration>
        a module-rooted path (a type spelling, or a scope segment such as
        `cit::Mode` in `cit::Mode::MODE_ON`) answered from the alias module
    SHADOW<TAB>ALIAS-MISS<TAB><file>:<line><TAB><written>
        a path rooted in the alias whose leaf the alias module does not
        declare (left Pending: refused by the type lane, or the scope lane's
        unowned-segment case)

Over the six halves ALIAS-MISS is expected 0 and the other two are the
population section 8.257 counted at the consumers (968 calls, 22 constants,
11 type spellings, plus the LSP half's).  Applied over the tree as it
stands; `--revert` removes it.
"""
import io, sys

NR = r"C:\Programming\apps\CryoLang\compiler\src\compiler\resolver\name_resolution.cryo"
revert = "--revert" in sys.argv

EDITS = [
    ("                node.set_scope_res(Res::Def(DefId::of_definition(alias_ns)));\n",
     "                fmt::eprintf(\"SHADOW\\tALIAS-SCOPE\\t%s:%u\\t%s::%s\\n\", node.scope_span.file, node.scope_span.start_line, this.resolver.intern_table.resolve(alias_ns), this.resolver.intern_table.resolve(node.member_name));\n"
     "                node.set_scope_res(Res::Def(DefId::of_definition(alias_ns)));\n"),
    ("                    if (!decl.is_valid()) { return ResSlot::Pending; }\n"
     "                    const ds: Symbol = this.resolver.get_symbol(decl);\n",
     "                    if (!decl.is_valid()) { fmt::eprintf(\"SHADOW\\tALIAS-MISS\\t%s:%u\\t%s\\n\", span.file, span.start_line, written); return ResSlot::Pending; }\n"
     "                    const ds: Symbol = this.resolver.get_symbol(decl);\n"
     "                    fmt::eprintf(\"SHADOW\\tALIAS-PATH\\t%s:%u\\t%s\\n\", span.file, span.start_line, this.resolver.intern_table.resolve(this.resolver.qualified_name_of(&ds)));\n"),
]

text = io.open(NR, encoding="utf-8", newline="").read()
for old, new in EDITS:
    if revert:
        assert text.count(new) == 1, ("not applied", old[:60])
        text = text.replace(new, old)
    else:
        assert text.count(old) == 1, ("anchor", old[:60], text.count(old))
        text = text.replace(old, new)
io.open(NR, "w", encoding="utf-8", newline="").write(text)
print("reverted" if revert else "applied")
