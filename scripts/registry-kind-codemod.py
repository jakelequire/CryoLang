"""One-off: a template entry's `node_kind: NodeKind` became `kind: TemplateKind`.

On every line of the compiler that reads `<expr>.node_kind`, the read becomes
`<expr>.kind` and each `NodeKind::<X>Declaration` the line compares it with
becomes `TemplateKind::<X>`.  Lines that do not read the field are left for
hand edits (the registration calls, the dispatch functions' parameters).
Each touched file gets `TemplateKind` added to its `generic_registry` import.

Run from the repository root: python scripts/registry-kind-codemod.py
"""
import os
import re
import sys

ROOT = os.path.join("compiler", "src")
READ = re.compile(r"\.node_kind\b")
KIND = re.compile(r"NodeKind::(Struct|Union|Class|Enum|Function|Trait|TypeAlias)Declaration\b")
IMPORT = re.compile(r"^(import compiler::types::generic_registry::\{ )([^}]*?)( \};)", re.M)


def convert(text):
    out = []
    count = 0
    for line in text.splitlines(keepends=True):
        if READ.search(line):
            line, n = READ.subn(".kind", line)
            count += n
            line = KIND.sub(lambda m: "TemplateKind::" + m.group(1), line)
        out.append(line)
    return "".join(out), count


def main():
    total = 0
    for dirpath, _, files in os.walk(ROOT):
        for f in files:
            if not f.endswith(".cryo"):
                continue
            path = os.path.join(dirpath, f)
            if path.replace(os.sep, "/").endswith("types/generic_registry.cryo"):
                continue
            with open(path, "r", encoding="utf-8", newline="") as h:
                text = h.read()
            new, n = convert(text)
            if n == 0:
                continue
            m = IMPORT.search(new)
            if m and "TemplateKind" not in m.group(2):
                new = new[:m.start()] + m.group(1) + m.group(2) + ", TemplateKind" + m.group(3) + new[m.end():]
            elif not m:
                print("no generic_registry import: " + path)
            with open(path, "w", encoding="utf-8", newline="") as h:
                h.write(new)
            print("%4d  %s" % (n, path))
            total += n
    print("reads rewritten: %d" % total)
    return 0


if __name__ == "__main__":
    sys.exit(main())
