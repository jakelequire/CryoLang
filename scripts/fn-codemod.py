#!/usr/bin/env python3
"""Rewrite reads of the function declaration class to the `FunctionDecl` payload.

One-off: `FunctionDeclNode` (a subclass of `DeclarationNode` with its own
`NodeKind::FunctionDeclaration`) becomes the payload `FunctionDecl` under
`DeclKind::Function`.  Wherever a function is HELD - a field, a list, a
return value, a parameter - it is held by its header, read through
`DeclarationNode::fn_of`.  This rewrites the mechanical forms:

* `X.kind == NodeKind::FunctionDeclaration` (or `(X as ASTNode*).kind`)
  becomes `DeclarationNode::fn_of(X) != null`, and `!=` the negation;
* a local holding a function, `const f: FunctionDeclNode* = <e>;`,
  becomes the header local `f_decl` (a cast `X as FunctionDeclNode*` in
  `<e>` becomes `DeclarationNode::with_shape(X)`) and the payload local `f`
  projected from it; `= null` keeps one local, typed as the header;
* a parameter `p: FunctionDeclNode*` of a function whose head ends its
  line with `{` becomes the header `p_decl: DeclarationNode*`, and the
  body opens with the payload local `p` projected from it;
* a one-line construction, `const f: FunctionDeclNode* = new
  FunctionDeclNode(n, s);`, becomes the payload `f` (`FunctionDecl::new(n)`)
  and its header `f_decl`;
* a member read through a cast, `(X as FunctionDeclNode*).m`, becomes
  `DeclarationNode::fn_of(X).m`, and any other `X as FunctionDeclNode*`
  the header, `DeclarationNode::with_shape(X)`;
* a payload member read through a held function, `<e>.func.<member>`,
  `<e>.current_fn.<member>` and the like, becomes
  `DeclarationNode::fn_of(<e>.func).<member>`;
* a header read through a split payload local (`f.span`, `f as ASTNode*`)
  becomes a read through its header (`f_decl.span`), to the end of the
  function;
* every other spelling of the type: `FunctionDeclNode*` as a field, list
  element or return type becomes `DeclarationNode*`, and the name itself
  `FunctionDecl`.

What is left - a payload handed where a header is wanted, a payload read
through any other header-typed value, multi-line constructions and the
`NodeKind::FunctionDeclaration` arms of a `match` - refuses in the type
check and is fixed by hand; the constructions and arms are reported.
Deleted in the commit after the one that ran it.

usage: python scripts/fn-codemod.py compiler/src tools/CryoLSP/src
"""
import os
import re
import sys

CLS = "FunctionDeclNode"
SEG = r"[A-Za-z_][A-Za-z0-9_]*(?:\[[A-Za-z0-9_.+\- ]*\])?"
EXPR = r"(?<![A-Za-z0-9_.])((?:this\.)?(?!null\b)" + SEG + r"(?:\." + SEG + r")*)"
QUAL = r"(?:declaration::|ast::declaration::)?"
CAST = re.compile(EXPR + r" as " + QUAL + CLS + r"\*")
PAREN_CAST = re.compile(r"\(" + EXPR + r" as " + QUAL + CLS + r"\*\)\.")
EQ = re.compile(r"(?:\(" + EXPR + r" as ASTNode\*\)|" + EXPR
                + r")\.kind (==|!=) (?:ast::)?NodeKind::FunctionDeclaration\b")
NEW = re.compile(r"^(\s*)(const|mut) (\w+): " + CLS + r"\* = new " + CLS + r"\((.*)\);(.*)$")
LOAD_NULL = re.compile(r"^(\s*)(const|mut) (\w+): " + CLS + r"\* = null;(.*)$")
LOAD = re.compile(r"^(\s*)(const|mut) (\w+): " + CLS + r"\* = (.*);(.*)$")
PARAM = re.compile(r"(?<![A-Za-z0-9_])(\w+): " + CLS + r"\*(?=[,)])")
HELD = r"(?:func|current_fn|current_func|symbolic_current_func|poll_fn)"
MEMBERS = ("name parameters generic_params trait_bounds body is_public is_static is_async "
           "is_inline is_noinline is_caller_located written_in is_variadic is_implicit "
           "is_synthesized_body is_synth_default is_intrinsic is_self_returning_default "
           "is_lazy_self_growing link_name has_link_name is_weak is_naked link_section "
           "return_type_annotation resolved_return_type origin_trait async_fut_slot "
           "spec_type_args overload_entry is_specialization def copy_of instance has_body "
           "is_generic has_resolved_return_type add_parameter add_generic_param "
           "add_trait_bound set_name set_body set_public set_static set_async set_variadic "
           "set_synthesized_body set_synth_default set_intrinsic set_self_returning_default "
           "set_lazy_self_growing set_return_type_annotation set_resolved_return_type "
           "set_origin_trait set_async_fut_slot set_spec_type_args set_overload_entry").split()
HELD_READ = re.compile(r"(?<![A-Za-z0-9_.:])((?:this\.)?" + SEG + r"(?:\." + SEG + r")*\."
                       + HELD + r")\.(" + "|".join(MEMBERS) + r")\b")
HEADER_FIELD = r"(span|name_span|generated|has_error|attached_directives|documentation|source_file|modifier_span|kind)\b"
HEADER_CALL = r"(has_directive|find_directive|outer_span|link_directive_symbol|has_name_span|set_name_span|set_documentation|add_directive|note_modifier_span|set_source_file|set_source_file)\("
SPLIT = re.compile(r"^\s*(?:const|mut) (\w+): FunctionDecl\* = DeclarationNode::fn_of\(\1_decl\);")
SPLIT_NEW = re.compile(r"^\s*const (\w+)_decl: DeclarationNode\* = DeclarationNode::of\(DeclKind::Function\(\1\)")
FN_HEAD = re.compile(r"^ {0,4}(?:static |public )*(?:function )?[a-z_][a-z0-9_]*(?:<[^>]*>)?\(.*\)\s*(?:->.*)?\{\s*$")
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
                out.append(f"{ind}{kw} {name}: FunctionDecl* = FunctionDecl::new({parts[0]});{rest}{eol}")
                out.append(f"{ind}const {name}_decl: DeclarationNode* = "
                           f"DeclarationNode::of(DeclKind::Function({name}), {parts[1]});{eol}")
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
            out.append(f"{ind}{kw} {name}: FunctionDecl* = DeclarationNode::fn_of({name}_decl);{rest}{eol}")
            continue
        out.append(line)
    return "".join(out)


def params(text):
    """A function head naming `p: FunctionDeclNode*` and ending its line with
    `{` takes the header and projects the payload first thing."""
    lines = text.splitlines(keepends=True)
    out = []
    i = 0
    while i < len(lines):
        m = HEAD_START.match(lines[i])
        if not m or CLS not in "".join(lines[i:i + 6]):
            out.append(lines[i])
            i += 1
            continue
        # gather the head up to the line ending with `{`
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
            out.append(f"{ind}const {n}: FunctionDecl* = DeclarationNode::fn_of({n}_decl);{eol}")
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
            if FN_HEAD.match(out[j].rstrip("\r\n")):
                break
            out[j] = field.sub(name + r"_decl.\1", out[j])
            out[j] = call.sub(name + r"_decl.\1(", out[j])
            out[j] = cast.sub(name + r"_decl as \1*", out[j])
    return "".join(out)


def fix_import(text):
    def repl(m):
        body = m.group(1)
        if "FunctionDecl" not in body:
            return m.group(0)
        if not re.search(r"\bDeclarationNode\b", body):
            body = body.replace("FunctionDecl", "DeclarationNode, FunctionDecl", 1)
        if "DeclKind::" in text and not re.search(r"\bDeclKind\b", body):
            body = body.replace("FunctionDecl", "DeclKind, FunctionDecl", 1)
        return "import compiler::ast::declaration::{" + body + "};"
    return IMPORT.sub(repl, text)


def convert(src):
    out = EQ.sub(lambda m: "DeclarationNode::fn_of(%s) %s null"
                 % (m.group(1) or m.group(2), "!=" if m.group(3) == "==" else "=="), src)
    out = locals_and_news(out)
    out = params(out)
    out = PAREN_CAST.sub(lambda m: f"DeclarationNode::fn_of({m.group(1)}).", out)
    out = CAST.sub(lambda m: f"DeclarationNode::with_shape({m.group(1)})", out)
    out = HELD_READ.sub(lambda m: f"DeclarationNode::fn_of({m.group(1)}).{m.group(2)}", out)
    out = re.sub(r"\[" + CLS + r"\*\]", "[DeclarationNode*]", out)
    out = re.sub(r"(:\s+)" + CLS + r"\*;", r"\1DeclarationNode*;", out)       # fields
    out = re.sub(r"(?<=-> )" + CLS + r"\*", "DeclarationNode*", out)          # returns
    out = out.replace("new " + CLS + "(", "new FUNCTION_DECL_NODE_CTOR(")
    out = re.sub(r"\b" + CLS + r"\b(?!\()", "FunctionDecl", out)
    out = out.replace("new FUNCTION_DECL_NODE_CTOR(", "new " + CLS + "(")
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
            if CLS not in src and "FunctionDeclaration" not in src and ".func." not in src:
                continue
            out = convert(src)
            if out != src:
                with open(p, "w", encoding="utf-8", newline="") as h:
                    h.write(out)
                print("rewrote", p)
            for m in re.finditer(r"new " + CLS + r"\(|NodeKind::FunctionDeclaration", out):
                line = out.count("\n", 0, m.start()) + 1
                print(f"left for hand edit: {p}:{line}")
