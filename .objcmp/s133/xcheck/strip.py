"""Copy the compiler to .objcmp/s133/strip/compiler and delete every
`![allow(lookup_by_spelling, ...)]` that sits on a function or method (a
field's stays: the store-key lint runs in an earlier phase and would stop
the build before sema).  Lists what it removed."""
import json
import os
import shutil

SRC = "compiler"
DST = ".objcmp/s133/strip/compiler"
if os.path.exists(DST):
    shutil.rmtree(DST)
os.makedirs(DST)
shutil.copy(os.path.join(SRC, "llvm_bindings.h"), DST)
cfg = open(os.path.join(SRC, "cryoconfig"), encoding="utf-8").read()
cfg = cfg.replace('search = ["../.toolchains/llvm-win/lib"]',
                  'search = ["C:/Programming/apps/CryoLang/.toolchains/llvm-win/lib"]')
open(os.path.join(DST, "cryoconfig"), "w", encoding="utf-8").write(cfg)
shutil.copytree(os.path.join(SRC, "src"), os.path.join(DST, "src"))
removed = []
for dp, dn, fn in os.walk(os.path.join(DST, "src")):
    for f in fn:
        if not f.endswith(".cryo"):
            continue
        path = os.path.join(dp, f)
        lines = open(path, encoding="utf-8", newline="").read().split("\n")
        out = []
        for i, l in enumerate(lines):
            s = l.strip()
            if s.startswith("![allow(lookup_by_spelling"):
                j = i + 1
                while j < len(lines) and (lines[j].strip().startswith("///") or lines[j].strip().startswith("![")):
                    j += 1
                nxt = lines[j] if j < len(lines) else ""
                if "(" in nxt.split("//")[0]:
                    rel = os.path.relpath(path, DST).replace("\\", "/")
                    removed.append({"file": rel, "line": j + 1, "decl": nxt.strip()[:100]})
                    continue
            out.append(l)
        open(path, "w", encoding="utf-8", newline="").write("\n".join(out))
json.dump(removed, open(".objcmp/s133/xcheck/removed.json", "w"), indent=0)
print("removed", len(removed), "function allows")
