"""One-off: the generic parameter class became the payload GenericParamDecl.

A declaration's `generic_params` now hold the declaration headers, read
through `DeclarationNode::generic_param_of`.  Over the compiler and the
language server:

- a load of one element into a `GenericParamNode*` local,
  `const gp: GenericParamNode* = <list>[<i>];`, becomes the header local
  `<gp>_decl` and the payload local `gp` projected from it;
- every list type `[GenericParamNode*]` (fields, parameters, locals,
  returns) becomes `[DeclarationNode*]`, and `add_generic_param` takes a
  header;
- `GenericParamNode` in an import of `compiler::ast::declaration` becomes
  `GenericParamDecl`, and `DeclarationNode` is added to that import if
  missing.

Header reads through a payload local (`gp.span`), element reads through a
list (`params[i].name`) and the constructions are left to refuse in the type
check and are fixed by hand.

Run from the repository root: python scripts/generic-param-codemod.py
"""
import os
import re
import sys

ROOTS = [os.path.join("compiler", "src"), os.path.join("tools", "CryoLSP", "src")]
LOAD = re.compile(r"^(\s*)(const|mut) (\w+): GenericParamNode\* = ([^;]*\[[^\];]+\]);(.*)$")
LIST = re.compile(r"\[GenericParamNode\*\]")
ADD = "add_generic_param(param: GenericParamNode*)"
IMPORT = re.compile(r"import compiler::ast::declaration::\{([^}]*)\};", re.S)


def fix_import(text):
    def repl(m):
        body = m.group(1)
        if "GenericParamNode" not in body:
            return m.group(0)
        body = re.sub(r"\bGenericParamNode\b", "GenericParamDecl", body)
        if not re.search(r"\bDeclarationNode\b", body):
            body = body.replace("GenericParamDecl", "DeclarationNode, GenericParamDecl", 1)
        return "import compiler::ast::declaration::{" + body + "};"
    return IMPORT.sub(repl, text)


def convert(text):
    count = 0
    out = []
    for line in text.splitlines(keepends=True):
        eol = "\r\n" if line.endswith("\r\n") else ("\n" if line.endswith("\n") else "")
        body = line[:len(line) - len(eol)]
        m = LOAD.match(body)
        if m:
            ind, kw, name, expr, rest = m.groups()
            out.append("%sconst %s_decl: DeclarationNode* = %s;%s" % (ind, name, expr, eol))
            out.append("%s%s %s: GenericParamDecl* = DeclarationNode::generic_param_of(%s_decl);%s%s"
                       % (ind, kw, name, name, rest, eol))
            count += 1
            continue
        body, n = LIST.subn("[DeclarationNode*]", body)
        count += n
        if ADD in body:
            body = body.replace(ADD, "add_generic_param(param: DeclarationNode*)")
            count += 1
        out.append(body + eol)
    return fix_import("".join(out)), count


def main():
    total = 0
    for root in ROOTS:
        for dirpath, _, files in os.walk(root):
            for f in files:
                if not f.endswith(".cryo"):
                    continue
                path = os.path.join(dirpath, f)
                with open(path, "r", encoding="utf-8", newline="") as h:
                    text = h.read()
                if "GenericParamNode" not in text:
                    continue
                new, n = convert(text)
                if new == text:
                    continue
                with open(path, "w", encoding="utf-8", newline="") as h:
                    h.write(new)
                print("%4d  %s" % (n, path))
                total += n
    print("rewritten: %d" % total)
    return 0


if __name__ == "__main__":
    sys.exit(main())
