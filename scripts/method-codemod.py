#!/usr/bin/env python3
"""Rewrite reads of the method class to the `MethodDecl` payload.

One-off: `MethodNode` (a subclass of `DeclarationNode` with its own
`NodeKind::MethodDeclaration`) becomes the payload `MethodDecl` under
`DeclKind::Method`.  Wherever a method is HELD - a field, a list, a return
value, a parameter - it is held by its header, read through
`DeclarationNode::method_of`.  The forms are the function conversion's
(`scripts/fn-codemod.py`, in the commit before):

* `X.kind == NodeKind::MethodDeclaration` (or `(X as ASTNode*).kind`)
  becomes `DeclarationNode::method_of(X) != null`, and `!=` the negation;
* a local holding a method, `const m: MethodNode* = <e>;`, becomes the
  header local `m_decl` (a cast `X as MethodNode*` in `<e>` becomes
  `DeclarationNode::with_shape(X)`) and the payload local `m` projected
  from it; `= null` keeps one local, typed as the header;
* a parameter `p: MethodNode*` of a function whose head ends its line with
  `{` becomes the header `p_decl: DeclarationNode*`, and the body opens
  with the payload local `p` projected from it;
* a one-line construction, `const m: MethodNode* = new MethodNode(f, s);`,
  becomes the payload `m` (`MethodDecl::new(f)`) and its header `m_decl`;
* a member read through a cast, `(X as MethodNode*).m`, becomes
  `DeclarationNode::method_of(X).m`, and any other `X as MethodNode*` the
  header, `DeclarationNode::with_shape(X)`;
* a payload member read through a held method (`<e>.method.<member>`,
  `<e>.spec_of.<member>`, ...) becomes
  `DeclarationNode::method_of(<e>.method).<member>`;
* a header read through a split payload local (`m.span`, `m as ASTNode*`)
  becomes a read through its header (`m_decl.span`), to the end of the
  function - here the end is the next line that opens a function head;
* every other spelling of the type: `MethodNode*` as a field, list
  element or return type becomes `DeclarationNode*`, and the name itself
  `MethodDecl`.

What is left refuses in the type check (and `scripts/fn-callfix.py`, made
general, takes the mechanical refusals) or is reported here: multi-line
constructions, the `NodeKind::MethodDeclaration` arms of a `match`, and a
cast of any other operand to the class.  Deleted in the commit after the
one that ran it.

usage: python scripts/method-codemod.py compiler/src tools/CryoLSP/src
"""
import os
import re
import sys

CLS = "MethodNode"
PAY = "MethodDecl"
PROJ = "method_of"
VARIANT = "Method"
KIND = "MethodDeclaration"
SEG = r"[A-Za-z_][A-Za-z0-9_]*(?:\[[A-Za-z0-9_.+\- ]*\])?"
EXPR = r"(?<![A-Za-z0-9_.])((?:this\.)?(?!null\b)" + SEG + r"(?:\." + SEG + r")*)"
QUAL = r"(?:declaration::|ast::declaration::)?"
CAST = re.compile(EXPR + r" as " + QUAL + CLS + r"\*")
PAREN_CAST = re.compile(r"\(" + EXPR + r" as " + QUAL + CLS + r"\*\)\.")
EQ = re.compile(r"(?:\(" + EXPR + r" as ASTNode\*\)|" + EXPR
                + r")\.kind (==|!=) (?:ast::)?NodeKind::" + KIND + r"\b")
NEW = re.compile(r"^(\s*)(const|mut) (\w+): " + CLS + r"\* = new " + CLS + r"\((.*)\);(.*)$")
LOAD_NULL = re.compile(r"^(\s*)(const|mut) (\w+): " + CLS + r"\* = null;(.*)$")
LOAD = re.compile(r"^(\s*)(const|mut) (\w+): " + CLS + r"\* = (.*);(.*)$")
PARAM = re.compile(r"(?<![A-Za-z0-9_])(\w+): " + CLS + r"\*(?=[,)])")
HELD = r"(?:method|spec_of|pending_base_ctor|poll_method)"
MEMBERS = ("func visibility is_constructor is_destructor is_static is_default_destructor "
           "is_virtual is_override is_sink base_ctor_name base_ctor_args spec_of has_base_ctor "
           "set_visibility set_constructor set_destructor set_static set_default_destructor "
           "set_virtual set_override set_sink set_base_ctor_name add_base_ctor_arg").split()
HELD_READ = re.compile(r"(?<![A-Za-z0-9_.:])((?:this\.)?" + SEG + r"(?:\." + SEG + r")*\."
                       + HELD + r")\.(" + "|".join(MEMBERS) + r")\b")
HEADER_FIELD = r"(span|name_span|generated|has_error|attached_directives|documentation|source_file|modifier_span|kind)\b"
HEADER_CALL = r"(has_directive|find_directive|outer_span|link_directive_symbol|has_name_span|set_name_span|set_documentation|add_directive|note_modifier_span|set_source_file)\("
SPLIT = re.compile(r"^\s*(?:const|mut) (\w+): " + PAY + r"\* = DeclarationNode::" + PROJ + r"\(\1_decl\);")
SPLIT_NEW = re.compile(r"^\s*const (\w+)_decl: DeclarationNode\* = DeclarationNode::of\(DeclKind::" + VARIANT + r"\(\1\)")
HEAD_START = re.compile(r"^( {0,4})(?:static |public )*(?:function )?[a-z_][a-z0-9_]*(?:<[^>]*>)?\(")
IMPORT = re.compile(r"import compiler::ast::declaration::\{([^}]*)\};", re.S)


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


def eol_of(line):
    return "\r\n" if line.endswith("\r\n") else ("\n" if line.endswith("\n") else "")


def locals_and_news(text):
    out = []
    for line in text.splitlines(keepends=True):
        eol = eol_of(line)
        body = line[:len(line) - len(eol)]
        m = NEW.match(body)
        if m:
            ind, kw, name, args, rest = m.groups()
            parts = split_last_comma(args)
            if parts is not None:
                out.append(f"{ind}{kw} {name}: {PAY}* = {PAY}::new({parts[0]});{rest}{eol}")
                out.append(f"{ind}const {name}_decl: DeclarationNode* = "
                           f"DeclarationNode::of(DeclKind::{VARIANT}({name}), {parts[1]});{eol}")
                continue
        m = LOAD_NULL.match(body)
        if m:
            ind, kw, name, rest = m.groups()
            out.append(f"{ind}{kw} {name}: DeclarationNode* = null;{rest}{eol}")
            continue
        m = LOAD.match(body)
        if m and ("new " + CLS + "(") not in body:
            ind, kw, name, expr, rest = m.groups()
            expr = CAST.sub(lambda c: f"DeclarationNode::with_shape({c.group(1)})", expr)
            out.append(f"{ind}const {name}_decl: DeclarationNode* = {expr};{eol}")
            out.append(f"{ind}{kw} {name}: {PAY}* = DeclarationNode::{PROJ}({name}_decl);{rest}{eol}")
            continue
        out.append(line)
    return "".join(out)


def params(text):
    lines = text.splitlines(keepends=True)
    out = []
    i = 0
    while i < len(lines):
        m = HEAD_START.match(lines[i])
        if not m or CLS not in "".join(lines[i:i + 6]):
            out.append(lines[i])
            i += 1
            continue
        j = i
        while j < len(lines) and j < i + 8 and not lines[j].rstrip("\r\n").rstrip().endswith("{"):
            j += 1
        if j >= len(lines) or j >= i + 8:
            out.append(lines[i])
            i += 1
            continue
        head = "".join(lines[i:j + 1])
        if "{" in head[:-1].rstrip().rstrip("{") or CLS + "*" not in head:
            out.append(lines[i])
            i += 1
            continue
        names = PARAM.findall(head)
        if not names:
            out.append(lines[i])
            i += 1
            continue
        head = PARAM.sub(lambda p: f"{p.group(1)}_decl: DeclarationNode*", head)
        out.append(head)
        eol = eol_of(lines[j])
        ind = m.group(1) + "    "
        for n in names:
            out.append(f"{ind}const {n}: {PAY}* = DeclarationNode::{PROJ}({n}_decl);{eol}")
        i = j + 1
    return "".join(out)


def header_reads(text):
    out = text.splitlines(keepends=True)
    for i, line in enumerate(out):
        m = SPLIT.match(line) or SPLIT_NEW.match(line)
        if not m:
            continue
        name = m.group(1)
        field = re.compile(r"(?<![A-Za-z0-9_.])" + name + r"\." + HEADER_FIELD)
        call = re.compile(r"(?<![A-Za-z0-9_.])" + name + r"\." + HEADER_CALL)
        cast = re.compile(r"(?<![A-Za-z0-9_.])" + name + r" as (ASTNode|DeclarationNode|void)\*")
        for j in range(i + 1, len(out)):
            if HEAD_START.match(out[j]):
                break
            out[j] = field.sub(name + r"_decl.\1", out[j])
            out[j] = call.sub(name + r"_decl.\1(", out[j])
            out[j] = cast.sub(name + r"_decl as \1*", out[j])
    return "".join(out)


def fix_import(text):
    def repl(m):
        body = m.group(1)
        if PAY not in body:
            return m.group(0)
        if not re.search(r"\bDeclarationNode\b", body):
            body = body.replace(PAY, "DeclarationNode, " + PAY, 1)
        if "DeclKind::" in text and not re.search(r"\bDeclKind\b", body):
            body = body.replace(PAY, "DeclKind, " + PAY, 1)
        return "import compiler::ast::declaration::{" + body + "};"
    return IMPORT.sub(repl, text)


def convert(src):
    out = EQ.sub(lambda m: "DeclarationNode::%s(%s) %s null"
                 % (PROJ, m.group(1) or m.group(2), "!=" if m.group(3) == "==" else "=="), src)
    out = locals_and_news(out)
    out = params(out)
    out = PAREN_CAST.sub(lambda m: f"DeclarationNode::{PROJ}({m.group(1)}).", out)
    out = CAST.sub(lambda m: f"DeclarationNode::with_shape({m.group(1)})", out)
    out = HELD_READ.sub(lambda m: f"DeclarationNode::{PROJ}({m.group(1)}).{m.group(2)}", out)
    out = re.sub(r"\[" + CLS + r"\*\]", "[DeclarationNode*]", out)
    out = re.sub(r"(:\s+)" + CLS + r"\*;", r"\1DeclarationNode*;", out)
    out = re.sub(r"(?<=-> )" + CLS + r"\*", "DeclarationNode*", out)
    out = out.replace("new " + CLS + "(", "new CLASS_CTOR_PLACEHOLDER(")
    out = re.sub(r"\b" + CLS + r"\b(?!\()", PAY, out)
    out = out.replace("new CLASS_CTOR_PLACEHOLDER(", "new " + CLS + "(")
    out = header_reads(out)
    return fix_import(out)


for root in sys.argv[1:]:
    for dp, _, fs in os.walk(root):
        for f in sorted(fs):
            if not f.endswith(".cryo"):
                continue
            p = os.path.join(dp, f)
            with open(p, encoding="utf-8", newline="") as h:
                src = h.read()
            if CLS not in src and KIND not in src and not re.search(r"\." + HELD + r"\.", src):
                continue
            out = convert(src)
            if out != src:
                with open(p, "w", encoding="utf-8", newline="") as h:
                    h.write(out)
                print("rewrote", p)
            for m in re.finditer(r"new " + CLS + r"\(|NodeKind::" + KIND + r"|as " + PAY + r"\*", out):
                line = out.count("\n", 0, m.start()) + 1
                print(f"left for hand edit: {p}:{line}")
