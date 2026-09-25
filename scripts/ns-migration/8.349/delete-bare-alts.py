"""Delete the bare-leaf store from compiler/src/compiler/decl_index.cryo.

Removes, each located by a marker that must match exactly once:
  * `ScopeResolution` and its impl (the result type of the scoped lookup);
  * the `bare_alts` / `bare_alts_index` fields and their initializers;
  * `register_name_mapping`;
  * `resolve_qualified_scoped` and `lookup_qualified_alternatives`.
Run from the repo root.  Refuses (exit 1) on any marker count other than one.
"""
import sys

PATH = "compiler/src/compiler/decl_index.cryo"

with open(PATH, encoding="utf-8", newline="") as f:
    lines = f.read().split("\n")


def find(pred, start=0, what=""):
    hits = [i for i in range(start, len(lines)) if pred(lines[i])]
    if not hits:
        print("FAIL: no line for", what)
        sys.exit(1)
    return hits[0]


def only(text):
    hits = [i for i, l in enumerate(lines) if l == text]
    if len(hits) != 1:
        print("FAIL: %d lines equal %r" % (len(hits), text))
        sys.exit(1)
    return hits[0]


cuts = []  # (first, last) inclusive

# ScopeResolution enum + impl: from its doc comment to the impl's closing brace.
a = only("/// Outcome of import-scoped bare-leaf name resolution")
b = only("implement enum ScopeResolution {")
c = find(lambda l: l == "}", b, "end of ScopeResolution impl")
cuts.append((a, c + 1 if lines[c + 1] == "" else c))

# Fields.
a = only("    // Bare-name -> all qualified names ever registered for it.")
cuts.append((a, a + 3 if lines[a + 3] == "" else a + 2))
a = only("    // Bare name SymbolStr.id -> positions in `bare_alts`.")
cuts.append((a, a + 1))
cuts.append((only("            bare_alts:      [],"),) * 2)
cuts.append((only("            bare_alts_index:    HashMap::<u32, u32[]>::new(),"),) * 2)

# register_name_mapping: doc comment through its closing brace and a blank line.
a = only("    /// Register a bare -> qualified name mapping.")
c = find(lambda l: l == "    }", a, "end of register_name_mapping")
cuts.append((a, c + 1 if lines[c + 1] == "" else c))

# resolve_qualified_scoped + lookup_qualified_alternatives, contiguous.
a = only("    /// Resolve a bare leaf to the qualified name the USE SITE actually means,")
m = only("    lookup_qualified_alternatives(&this, bare: SymbolStr) -> SymbolStr[] {")
c = find(lambda l: l == "    }", m, "end of lookup_qualified_alternatives")
cuts.append((a, c + 1 if lines[c + 1] == "" else c))

drop = set()
for first, last in cuts:
    drop.update(range(first, last + 1))
out = [l for i, l in enumerate(lines) if i not in drop]
with open(PATH, "w", encoding="utf-8", newline="") as f:
    f.write("\n".join(out))
print("deleted %d lines in %d blocks" % (len(drop), len(cuts)))
print("DELETE_DONE")
