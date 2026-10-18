#!/usr/bin/env python3
"""Rewrite reads of the variable declaration class to the `VarDecl` payload.

One-off: `VarDeclNode` (a subclass of `DeclarationNode` with its own
`NodeKind::VariableDeclaration`) becomes the payload `VarDecl` under
`DeclKind::Variable`.  A list of variables (a function's or a lambda's
parameters, an intrinsic's, an extern block's globals) holds the headers,
read through `DeclarationNode::var_of`.  This rewrites the mechanical forms:

* `X as VarDeclNode*` (and qualified spellings), a cast of a node handle,
  becomes `DeclarationNode::var_of(X)`, the bridge helper that answers the
  payload or null;
* `X.kind == NodeKind::VariableDeclaration` (or `(X as ASTNode*).kind`) becomes
  `DeclarationNode::var_of(X) != null`, and `!=` the negation;
* a load of one list element into a local,
  `const p: VarDeclNode* = <list>[<i>];`, becomes the header local `p_decl`
  and the payload local `p` projected from it;
* a one-line construction, `const v: VarDeclNode* = new VarDeclNode(n, s);`,
  becomes the payload `v` (`VarDecl::new(n)`) and its header `v_decl`;
* `[VarDeclNode*]` becomes `[DeclarationNode*]`;
* every other spelling of the type becomes `VarDecl`;
* a read of a header field or method through such a payload local
  (`p.span`, `v.has_directive(...)`) becomes a read through its header
  (`p_decl.span`), to the end of the function.

Reads of the header's fields through any other payload value, a payload
handed where a header is wanted, the remaining constructions and the
`NodeKind::VariableDeclaration` arms of a `match` are left to refuse in the
type check and are fixed by hand; the constructions and arms are reported.
Deleted in the commit after the one that ran it.

usage: python scripts/var-codemod.py compiler/src tools/CryoLSP/src
"""
import os
import re
import sys

SEG = r"[A-Za-z_][A-Za-z0-9_]*(?:\[[A-Za-z0-9_.+\- ]*\])?"
EXPR = r"(?<![A-Za-z0-9_.])((?:this\.)?(?!null\b)" + SEG + r"(?:\." + SEG + r")*)"
CAST = re.compile(EXPR + r" as (?:declaration::|ast::declaration::)?VarDeclNode\*")
EQ = re.compile(r"(?:\(" + EXPR + r" as ASTNode\*\)|" + EXPR + r")\.kind (==|!=) (?:ast::)?NodeKind::VariableDeclaration\b")
LOAD = re.compile(r"^(\s*)(const|mut) (\w+): VarDeclNode\* = ([^;]*\]);(.*)$")
NEW = re.compile(r"^(\s*)(const|mut) (\w+): VarDeclNode\* = new VarDeclNode\((.*)\);(.*)$")
LIST = re.compile(r"\[VarDeclNode\*\]")
NAME = re.compile(r"\bVarDeclNode\b(?!\()")
IMPORT = re.compile(r"import compiler::ast::declaration::\{([^}]*)\};", re.S)


def cast(m):
    return f"DeclarationNode::var_of({m.group(1)})"


def eq(m):
    op = "!=" if m.group(3) == "==" else "=="
    return f"DeclarationNode::var_of({m.group(1) or m.group(2)}) {op} null"


def split_last_comma(args):
    depth = 0
    for i in range(len(args) - 1, -1, -1):
        c = args[i]
        if c in ")]":
            depth += 1
        elif c in "([":
            depth -= 1
        elif c == "," and depth == 0:
            return args[:i].strip(), args[i + 1:].strip()
    return None


def lines(text):
    out = []
    for line in text.splitlines(keepends=True):
        eol = "\r\n" if line.endswith("\r\n") else ("\n" if line.endswith("\n") else "")
        body = line[:len(line) - len(eol)]
        m = NEW.match(body)
        if m:
            ind, kw, name, args, rest = m.groups()
            parts = split_last_comma(args)
            if parts is not None:
                out.append(f"{ind}{kw} {name}: VarDecl* = VarDecl::new({parts[0]});{rest}{eol}")
                out.append(f"{ind}const {name}_decl: DeclarationNode* = "
                           f"DeclarationNode::of(DeclKind::Variable({name}), {parts[1]});{eol}")
                continue
        m = LOAD.match(body)
        if m and "new VarDeclNode(" not in body:
            ind, kw, name, expr, rest = m.groups()
            out.append(f"{ind}const {name}_decl: DeclarationNode* = {expr};{eol}")
            out.append(f"{ind}{kw} {name}: VarDecl* = DeclarationNode::var_of({name}_decl);{rest}{eol}")
            continue
        out.append(line)
    return "".join(out)


HEADER_FIELD = r"(span|name_span|generated|has_error|attached_directives|documentation|source_file|modifier_span)\b"
HEADER_CALL = r"(has_directive|find_directive|outer_span|link_directive_symbol|has_name_span|set_name_span|set_documentation|add_directive|note_modifier_span|set_source_file)\("
SPLIT = re.compile(r"^\s*(?:const|mut) (\w+): VarDecl\* = DeclarationNode::var_of\(\1_decl\);")
SPLIT_NEW = re.compile(r"^\s*const (\w+)_decl: DeclarationNode\* = DeclarationNode::of\(DeclKind::Variable\(\1\)")
FN_HEAD = re.compile(r"^ {0,4}(?:static )?[a-z_][a-z0-9_]*(?:<[^>]*>)?\(.*\)\s*(?:->.*)?\{\s*$")


def header_reads(text):
    """`p.span` through a payload local `p` split from `p_decl` reads `p_decl.span`,
    up to the end of the function the split is in."""
    out = text.splitlines(keepends=True)
    for i, line in enumerate(out):
        m = SPLIT.match(line) or SPLIT_NEW.match(line)
        if not m:
            continue
        name = m.group(1)
        field = re.compile(r"(?<![A-Za-z0-9_.])" + name + r"\." + HEADER_FIELD)
        call = re.compile(r"(?<![A-Za-z0-9_.])" + name + r"\." + HEADER_CALL)
        for j in range(i + 1, len(out)):
            if FN_HEAD.match(out[j].rstrip("\r\n")):
                break
            out[j] = field.sub(name + r"_decl.\1", out[j])
            out[j] = call.sub(name + r"_decl.\1(", out[j])
    return "".join(out)


def fix_import(text):
    def repl(m):
        body = m.group(1)
        if "VarDecl" not in body:
            return m.group(0)
        if not re.search(r"\bDeclarationNode\b", body):
            body = body.replace("VarDecl", "DeclarationNode, VarDecl", 1)
        if "DeclKind::" in text and not re.search(r"\bDeclKind\b", body):
            body = body.replace("VarDecl", "DeclKind, VarDecl", 1)
        return "import compiler::ast::declaration::{" + body + "};"
    return IMPORT.sub(repl, text)


for root in sys.argv[1:]:
    for dp, _, fs in os.walk(root):
        for f in sorted(fs):
            if not f.endswith(".cryo"):
                continue
            p = os.path.join(dp, f)
            with open(p, encoding="utf-8", newline="") as h:
                src = h.read()
            if "VarDecl" not in src and "VariableDeclaration" not in src:
                continue
            out = CAST.sub(cast, src)
            out = EQ.sub(eq, out)
            out = lines(out)
            out = LIST.sub("[DeclarationNode*]", out)
            out = out.replace("new VarDeclNode(", "new VAR_DECL_NODE_CTOR(")
            out = NAME.sub("VarDecl", out)
            out = out.replace("new VAR_DECL_NODE_CTOR(", "new VarDeclNode(")
            out = header_reads(out)
            out = fix_import(out)
            if out != src:
                with open(p, "w", encoding="utf-8", newline="") as h:
                    h.write(out)
                print("rewrote", p)
            for m in re.finditer(r"new VarDeclNode\(|NodeKind::VariableDeclaration", out):
                line = out.count("\n", 0, m.start()) + 1
                print(f"left for hand edit: {p}:{line}")
