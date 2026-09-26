import io
import os

ROOT = r"C:\Programming\apps\CryoLang"
SRC = os.path.join(ROOT, "compiler", "src", "compiler")


def load(p):
    s = io.open(p, encoding="utf-8", newline="").read()
    return s, ("\r\n" if "\r\n" in s else "\n")


os.remove(os.path.join(SRC, "resolver", "module_path.cryo"))
roots = [os.path.join(ROOT, "compiler", "src"), os.path.join(ROOT, "tools", "CryoLSP", "src")]
n = 0
for r in roots:
    for dirpath, _, files in os.walk(r):
        for f in files:
            if not f.endswith(".cryo"):
                continue
            q = os.path.join(dirpath, f)
            s, nl = load(q)
            if "compiler::resolver::module_path" not in s:
                continue
            s = s.replace("import compiler::resolver::module_path;" + nl, "")
            has_mod = ("import compiler::module_graph;" + nl) in s
            s = s.replace("import compiler::resolver::module_path::{ ModulePath };",
                          ("" if has_mod else "import compiler::module_graph;" + nl)
                          + "import compiler::module_graph::{ ModulePath };")
            assert "resolver::module_path" not in s, q
            io.open(q, "w", encoding="utf-8", newline="").write(s)
            n += 1
            print("  ", os.path.relpath(q, ROOT))
print("importers rewritten:", n)
