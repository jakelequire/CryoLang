"""Move `LangItem` and `LangModule` out of the type layer's registry into
their own name-layer module, so `ResSlot` can hold a language item.

Run once from the repo root.  Idempotent: refuses if the new module exists.
"""
import os, re, subprocess, sys

REG = "compiler/src/compiler/types/generic_registry.cryo"
NEW = "compiler/src/compiler/resolver/lang_item.cryo"

if os.path.exists(NEW):
    sys.exit("already moved: " + NEW)

lines = open(REG, encoding="utf-8", newline="").read().split("\n")
start = next(i for i, l in enumerate(lines) if l.startswith("/// A declaration the language itself reasons about"))
tstart = next(i for i in range(start, len(lines)) if lines[i].startswith("type struct GenericRegistry"))
end = tstart
while lines[end - 1].strip() == "":
    end -= 1
block = lines[start:end]
assert any(l.startswith("type enum LangItem") for l in block)
assert any(l.startswith("type enum LangModule") for l in block)

header = [
    "/// Language items: the declarations the compiler itself reasons about by",
    "/// identity, and the modules it imports on a program's behalf.",
    "///",
    "/// In the name layer because a reference the compiler writes before any",
    "/// declaration exists - a desugared `for`, `a..b`, `x ?? d` - names a",
    "/// language item rather than a spelling, and the slot that holds it",
    "/// (`ResSlot::Lang`) is the name layer's.",
    "",
    "namespace compiler::resolver::lang_item;",
    "",
]
open(NEW, "w", encoding="utf-8", newline="\n").write("\n".join(header + block) + "\n")
del lines[start:tstart]
reg_src = "\n".join(lines).replace(
    "import compiler::resolver::res;\n",
    "import compiler::resolver::lang_item;\nimport compiler::resolver::lang_item::{ LangItem };\nimport compiler::resolver::res;\n", 1)
open(REG, "w", encoding="utf-8", newline="").write(reg_src)

# Importers: `generic_registry::{ GenericRegistry, LangItem }` and friends.
files = subprocess.run(["git", "grep", "-l", r"generic_registry::{.*Lang\(Item\|Module\)", "--", "compiler/src", "tools"],
                       capture_output=True, text=True).stdout.split()
pat = re.compile(r"^import compiler::types::generic_registry::\{([^}]*)\};", re.M)
for f in files:
    src = open(f, encoding="utf-8", newline="").read()
    def fix(m):
        names = [n.strip() for n in m.group(1).split(",") if n.strip()]
        lang = [n for n in names if n in ("LangItem", "LangModule")]
        rest = [n for n in names if n not in ("LangItem", "LangModule")]
        out = []
        if rest:
            out.append("import compiler::types::generic_registry::{ %s };" % ", ".join(rest))
        if lang:
            out.append("import compiler::resolver::lang_item;")
            out.append("import compiler::resolver::lang_item::{ %s };" % ", ".join(lang))
        return "\n".join(out)
    new = pat.sub(fix, src)
    if new != src:
        open(f, "w", encoding="utf-8", newline="").write(new)
        print("rewrote", f)
