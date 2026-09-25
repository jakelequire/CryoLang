"""Delete the six writers of the bare-leaf store (`register_name_mapping`).

Each writer is a three-line `if (...) {` / call / `}` block; the comment
lines directly above a block that speak only of the mapping go with it.
Run from the repo root after delete-bare-alts.py.  Refuses on any count other
than the expected one per file.
"""
import sys

EXPECT = {
    "compiler/src/compiler/passes/pass_registry.cryo": 5,
    "compiler/src/compiler/mono/monomorphizer.cryo": 1,
}
# Comment lines above a writer that describe the mapping and nothing else.
MAPPING_COMMENTS = (
    "// A C-imported type (binding_namespace set) is reachable",
    "// ONLY via its alias (`cit::Vec2`); registering its bare",
    "// name globally would pollute the namespace and shadow",
    "// same-named types in other modules.",
    "// The bare spec identifier maps to the canonical name: the",
    "// substituter rewrites a clone body's self-references to the",
    "// bare spelling, and the LSP's completion resolves it through",
    "// this mapping.",
)

for path, want in EXPECT.items():
    with open(path, encoding="utf-8", newline="") as f:
        lines = f.read().split("\n")
    drop = set()
    found = 0
    for i, l in enumerate(lines):
        if "register_name_mapping(" not in l:
            continue
        found += 1
        if not lines[i - 1].strip().startswith("if (") or lines[i + 1].strip() != "}":
            print("FAIL: unexpected shape at %s:%d" % (path, i + 1))
            sys.exit(1)
        drop.update((i - 1, i, i + 1))
        j = i - 2
        while j >= 0 and lines[j].strip() in MAPPING_COMMENTS:
            drop.add(j)
            j -= 1
    if found != want:
        print("FAIL: %s has %d writers, expected %d" % (path, found, want))
        sys.exit(1)
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write("\n".join(l for i, l in enumerate(lines) if i not in drop))
    print("%s: %d writers, %d lines" % (path, found, len(drop)))
print("WRITERS_DONE")
