"""The type lane's Pending population measured at the SLOT, not at the walk's
return: section 8.255's probe printed at `type_spelling_res`'s five Pending
returns, and a node whose synthesizer stamped it before the walk (bindgen's
`CTypeMapper::named` answers `Res::Def` on every annotation it builds) still
sends the walk through a Pending return whose answer first-wins then drops.
This probe prints once per NODE left unstamped after its stamp site ran:

    SHADOW<TAB>TSR-SLOT<TAB><site><TAB><file>:<line><TAB><spelling>

sites: impl-head (the target of `implement ... for T`), trait-ref (a trait
bound's path, its `resolved_name`), annotation (a `Named` annotation),
new-expr, struct-literal, enum-pattern.

Applied over the tree as it stands; `--revert` removes it.
"""
import io, sys
SRC = r"C:\Programming\apps\CryoLang\compiler\src\compiler\resolver\name_resolution.cryo"
revert = "--revert" in sys.argv


def line(site, cond, span, spelling):
    return ('if (%s) { fmt::eprintf("SHADOW\\tTSR-SLOT\\t%s\\t%%s:%%u\\t%%s\\n", %s.file, %s.start_line, %s); }'
            % (cond, site, span, span, spelling))


EDITS = [
    # impl head
    ("        match (this.type_spelling_res(node.target_type, node.span)) {\n"
     "            ResSlot::Answered(r) => {\n"
     "                node.set_res(r);\n"
     "            }\n"
     "            _                    => { }\n"
     "        }\n",
     "        match (this.type_spelling_res(node.target_type, node.span)) {\n"
     "            ResSlot::Answered(r) => {\n"
     "                node.set_res(r);\n"
     "            }\n"
     "            _                    => { }\n"
     "        }\n"
     "        " + line("impl-head", "node.res.is_pending()", "node.span",
                      "this.resolver.intern_table.resolve(node.target_type)") + "\n"),
    # trait ref
    ("        match (this.type_spelling_res(this.resolver.intern_table.intern(written), tref.span)) {\n"
     "            ResSlot::Answered(r) => {\n"
     "                match (r) {\n"
     "                    Res::Def(q) => { tref.resolved_name = q.qualified_name(); }\n"
     "                    _           => { }\n"
     "                }\n"
     "            }\n"
     "            _ => { }\n"
     "        }\n",
     "        match (this.type_spelling_res(this.resolver.intern_table.intern(written), tref.span)) {\n"
     "            ResSlot::Answered(r) => {\n"
     "                match (r) {\n"
     "                    Res::Def(q) => { tref.resolved_name = q.qualified_name(); }\n"
     "                    _           => { }\n"
     "                }\n"
     "            }\n"
     "            _ => { }\n"
     "        }\n"
     "        " + line("trait-ref", "!tref.resolved_name.is_valid()", "tref.span", "written") + "\n"),
    # annotation
    ("        match (this.type_spelling_res(n.name, n.span)) {\n"
     "            ResSlot::Answered(r) => { n.res.answer(r); }\n"
     "            _                    => { }\n"
     "        }\n",
     "        match (this.type_spelling_res(n.name, n.span)) {\n"
     "            ResSlot::Answered(r) => { n.res.answer(r); }\n"
     "            _                    => { }\n"
     "        }\n"
     "        " + line("annotation", "n.res.is_pending()", "n.span",
                      "this.resolver.intern_table.resolve(n.name)") + "\n"),
    # new expr
    ("        match (this.type_spelling_res(node.type_name, node.span)) {\n"
     "            ResSlot::Answered(r2) => { node.set_res(r2); }\n"
     "            _                     => { }\n"
     "        }\n",
     "        match (this.type_spelling_res(node.type_name, node.span)) {\n"
     "            ResSlot::Answered(r2) => { node.set_res(r2); }\n"
     "            _                     => { }\n"
     "        }\n"
     "        " + line("new-expr", "node.res.is_pending()", "node.span",
                      "this.resolver.intern_table.resolve(node.type_name)") + "\n"),
    # struct literal
    ("        match (this.type_spelling_res(node.struct_type, node.span)) {\n"
     "            ResSlot::Answered(r) => { node.set_res(r); }\n"
     "            _                    => { }\n"
     "        }\n",
     "        match (this.type_spelling_res(node.struct_type, node.span)) {\n"
     "            ResSlot::Answered(r) => { node.set_res(r); }\n"
     "            _                    => { }\n"
     "        }\n"
     "        " + line("struct-literal", "node.res.is_pending()", "node.span",
                      "this.resolver.intern_table.resolve(node.struct_type)") + "\n"),
    # enum pattern
    ("        match (this.type_spelling_res(node.enum_name, node.enum_span)) {\n"
     "            ResSlot::Answered(r) => { node.set_res(r); }\n"
     "            _                    => { }\n"
     "        }\n",
     "        match (this.type_spelling_res(node.enum_name, node.enum_span)) {\n"
     "            ResSlot::Answered(r) => { node.set_res(r); }\n"
     "            _                    => { }\n"
     "        }\n"
     "        " + line("enum-pattern", "node.res.is_pending()", "node.enum_span",
                      "this.resolver.intern_table.resolve(node.enum_name)") + "\n"),
]


def main():
    src = io.open(SRC, encoding="utf-8", newline="").read()
    nl = "\r\n" if "\r\n" in src else "\n"
    edits = [(a.replace("\n", nl), b.replace("\n", nl)) for a, b in EDITS]
    if revert:
        for a, b in edits:
            if src.count(b) != 1:
                raise SystemExit("no probe to revert at: %r" % a[:60])
            src = src.replace(b, a)
    else:
        if "TSR-SLOT" in src:
            raise SystemExit("already probed")
        for a, b in edits:
            if src.count(a) != 1:
                raise SystemExit("anchor not exactly once (%d): %r" % (src.count(a), a[:70]))
            src = src.replace(a, b)
    io.open(SRC, "w", encoding="utf-8", newline="").write(src)
    print("reverted" if revert else "probed")


if __name__ == "__main__":
    main()
