"""The pair for the coherence key's door: a mutant name layer that REPORTS
an impl head's undeclared argument but leaves its slot `Pending` (the
`Err` stamp removed).  Under it, `ann_canon_key`'s `Named` arm reads a slot
with no answer and the door records it; under the tree, the same negative
compiles to E0302 alone.  `--revert` restores the stamp."""
import io, sys

NR = r"C:\Programming\apps\CryoLang\compiler\src\compiler\resolver\name_resolution.cryo"
RS = r"C:\Programming\apps\CryoLang\compiler\src\compiler\resolver\res.cryo"
# The E0900 that reads the tally is raised only on a build nothing else
# refused, and this negative is refused by design; so the mutant also prints
# every recorded site as it is recorded, on either exit:
#     SHADOW<TAB>PENDING<TAB><site>
EDITS = [
    (NR, "                        bare.res.answer(Res::Err);\n",
         "                        /* MUTANT: slot left Pending */\n"),
    (RS, "    if (g_pending_bugs == 0) { g_pending_first_site = site; }\n",
         "    if (g_pending_bugs == 0) { g_pending_first_site = site; }\n"
         "    fmt::eprintf(\"SHADOW\\tPENDING\\t%s\\n\", site);\n"),
]
revert = "--revert" in sys.argv
for path, old, new in EDITS:
    text = io.open(path, encoding="utf-8", newline="").read()
    if revert:
        assert text.count(new) == 1, path
        text = text.replace(new, old)
    else:
        assert text.count(old) == 1, (path, text.count(old))
        text = text.replace(old, new)
    io.open(path, "w", encoding="utf-8", newline="").write(text)
print("reverted" if revert else "mutated")
