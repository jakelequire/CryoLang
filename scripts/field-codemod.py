"""One-off: the field declaration class became the payload FieldDecl.

A type's `fields` now hold the declaration headers, read through
`DeclarationNode::field_of`.  Over the compiler and the language server:

- a load of one element into a `FieldDeclNode*` local,
  `const f: FieldDeclNode* = <list>[<i>];`, becomes the header local
  `<f>_decl` and the payload local `f` projected from it;
- a field-list parameter `&[FieldDeclNode*]` becomes `&[DeclarationNode*]`;
- `FieldDeclNode` in an import of `compiler::ast::declaration` becomes
  `FieldDecl`, and `DeclarationNode` is added to that import if missing.

Reads of the header's fields through the payload local (`f.span`,
`f.name_span`) are left to refuse in the type check and are fixed by hand,
as are the constructions and anything else naming the class.

Run from the repository root: python scripts/field-codemod.py
"""
import os
import re
import sys

ROOTS = [os.path.join("compiler", "src"), os.path.join("tools", "CryoLSP", "src")]
LOAD = re.compile(r"^(\s*)(const|mut) (\w+): FieldDeclNode\* = ([^;]*\[[^\];]+\]);(.*)$")
PARAM = re.compile(r"&\[FieldDeclNode\*\]")
IMPORT = re.compile(r"import compiler::ast::declaration::\{([^}]*)\};", re.S)


def fix_import(text):
    def repl(m):
        body = m.group(1)
        if "FieldDeclNode" not in body:
            return m.group(0)
        body = re.sub(r"\bFieldDeclNode\b", "FieldDecl", body)
        if not re.search(r"\bDeclarationNode\b", body):
            body = body.replace("FieldDecl", "DeclarationNode, FieldDecl", 1)
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
            out.append("%s%s %s: FieldDecl* = DeclarationNode::field_of(%s_decl);%s%s" % (ind, kw, name, name, rest, eol))
            count += 1
            continue
        new, n = PARAM.subn("&[DeclarationNode*]", body)
        count += n
        out.append(new + eol)
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
                if "FieldDeclNode" not in text:
                    continue
                new, n = convert(text)
                if new == text:
                    continue
                with open(path, "w", encoding="utf-8", newline="") as h:
                    h.write(new)
                print("%4d  %s" % (n, path))
                total += n
    print("loads and parameters rewritten: %d" % total)
    return 0


if __name__ == "__main__":
    sys.exit(main())
