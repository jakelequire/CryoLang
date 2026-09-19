"""The type lane's Pending population: a print at each of the five places
`type_spelling_res` / `leaf_in_module_scope` (resolver/name_resolution.cryo)
leave a written type spelling unanswered, with the reason:

    SHADOW<TAB>TSR-PENDING<TAB><file>:<line><TAB><spelling><TAB><reason>

reasons: empty (no spelling), qualified-miss (a `::` path no module and no
alias scope answers), synth-span (a span naming no module: a synthesized
node), no-home-scope (the writing module has no scope), leaf-miss (a bare
leaf the writing module's scope, imports and prelude do not declare).

Applied over the tree as it stands; `--revert` removes it.  The same
standard as section 8.250's probe on the identifier lane: the count on GREEN
halves is what decides whether the lane can stamp `Err` where it is written.
"""
import io, sys
SRC = r"C:\Programming\apps\CryoLang\compiler\src\compiler\resolver\name_resolution.cryo"
revert = "--revert" in sys.argv

def line(reason, spelling_expr):
    return ('fmt::eprintf("SHADOW\\tTSR-PENDING\\t%%s:%%u\\t%%s\\t%s\\n", span.file, span.start_line, %s); '
            % (reason, spelling_expr))

EDITS = [
    ("        if (written.length() == 0) { return ResSlot::Pending; }\n",
     "        if (written.length() == 0) { " + line("empty", "written") + "return ResSlot::Pending; }\n"),
    ("            // Nothing in the writing module's scope answers the leaf, or the\n"
     "            // module it was declared in contradicts the written qualifier.\n"
     "            // Left unstamped, because recording an answer this pass cannot\n"
     "            // produce is how a tool limitation becomes a claim.\n"
     "            return ResSlot::Pending;\n",
     "            // Nothing in the writing module's scope answers the leaf, or the\n"
     "            // module it was declared in contradicts the written qualifier.\n"
     "            // Left unstamped, because recording an answer this pass cannot\n"
     "            // produce is how a tool limitation becomes a claim.\n"
     "            " + line("qualified-miss", "written") + "\n"
     "            return ResSlot::Pending;\n"),
    ("        if (!use_ns.is_valid()) {\n            return ResSlot::Pending;\n        }\n",
     "        if (!use_ns.is_valid()) {\n            " + line("synth-span", "written") + "\n            return ResSlot::Pending;\n        }\n"),
    ("        if (home_sid == 0) {\n            return ResSlot::Pending;\n        }\n",
     "        if (home_sid == 0) {\n            " + line("no-home-scope", "written") + "\n            return ResSlot::Pending;\n        }\n"),
    ("        if (mods.length < 2) { return ResSlot::Pending; }\n",
     "        if (mods.length < 2) { " + line("leaf-miss", "this.resolver.intern_table.resolve(name)") + "return ResSlot::Pending; }\n"),
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
        if "TSR-PENDING" in src:
            raise SystemExit("already probed")
        for a, b in edits:
            if src.count(a) != 1:
                raise SystemExit("anchor not exactly once (%d): %r" % (src.count(a), a[:70]))
            src = src.replace(a, b)
    io.open(SRC, "w", encoding="utf-8", newline="").write(src)
    print("reverted" if revert else "probed")


if __name__ == "__main__":
    main()
