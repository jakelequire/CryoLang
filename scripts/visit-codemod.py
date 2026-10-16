"""One-off codemod: the last three `BaseASTVisitor` classes become structs
that dispatch on the node's kind, and `accept` goes from the tree.

    python scripts/visit-codemod.py type FILE TYPE
        `type class TYPE : BaseASTVisitor { public:` -> `type struct TYPE {`;
        each `override visit(... n: XNode*)` -> `visit_<kind>(mut &this, n: XNode*)`;
        a method of TYPE's body written without a receiver gets `mut &this`.
    python scripts/visit-codemod.py retype NEWTYPE FILE...
        `ASTVisitor*` -> `NEWTYPE*` and `x.accept(v)` -> `v.dispatch_node(x)`.
    python scripts/visit-codemod.py strip FILE...
        deletes every `override accept(visitor: ASTVisitor*)` method.

What a pattern cannot see - a direct `this.visit(x)`, a constructor, an
import - is left for a hand edit, and the lines are reported.
"""
import io
import re
import sys

SUFFIX = {
    "ProgramNode": "program", "LiteralNode": "literal", "IdentifierNode": "identifier",
    "BinaryExprNode": "binary", "UnaryExprNode": "unary", "TernaryExprNode": "ternary",
    "IfExprNode": "if_expr", "MatchExprNode": "match_expr",
    "StaticMatchExprNode": "static_match_expr", "CallExprNode": "call", "NewExprNode": "new",
    "SizeofExprNode": "sizeof", "AlignofExprNode": "alignof", "CastExprNode": "cast",
    "StructLiteralNode": "struct_literal", "ArrayLiteralNode": "array_literal",
    "TupleLiteralNode": "tuple_literal", "LambdaExprNode": "lambda",
    "ArrayAccessNode": "array_access", "MemberAccessNode": "member_access",
    "ScopeResolutionNode": "scope_resolution", "TypeofExprNode": "typeof",
    "DeleteExprNode": "delete", "AwaitExprNode": "await", "YieldExprNode": "yield",
    "TryExprNode": "try", "BlockStmtNode": "block", "UnsafeBlockStmtNode": "unsafe_block",
    "AsmBlockStmtNode": "asm_block", "ReturnStmtNode": "return", "IfStmtNode": "if",
    "WhileStmtNode": "while", "DoWhileStmtNode": "do_while", "ForStmtNode": "for",
    "MatchStmtNode": "match_stmt", "StaticMatchStmtNode": "static_match_stmt",
    "BreakStmtNode": "break", "ContinueStmtNode": "continue", "ExprStmtNode": "expr_stmt",
    "DeclStmtNode": "decl_stmt", "LoopStmtNode": "loop", "VarDeclNode": "var",
    "DestructureDeclNode": "destructure", "FunctionDeclNode": "function",
    "MethodNode": "method", "IntrinsicDeclNode": "intrinsic",
    "IntrinsicConstDeclNode": "intrinsic_const", "ImportDeclNode": "import",
    "ModuleDeclNode": "module", "StructDeclNode": "struct", "UnionDeclNode": "union",
    "ClassDeclNode": "class", "TraitDeclNode": "trait", "TypeAliasDeclNode": "type_alias",
    "EnumDeclNode": "enum", "EnumVariantNode": "enum_variant", "ImplBlockNode": "impl",
    "ExternBlockNode": "extern_block", "FieldDeclNode": "field",
    "GenericParamNode": "generic_param", "MatchArmNode": "match_arm",
    "StaticMatchArmNode": "static_match_arm", "PatternNode": "pattern",
    "EnumPatternNode": "enum_pattern", "DirectiveNode": "directive",
}

SIG = re.compile(r"^(\s*)override visit\((?:mut &this, )?(\w+): (\w+)\*\)")
METHOD = re.compile(r"^    ([a-z_][a-z0-9_]*)\(")
ACCEPT = re.compile(r"(\([\w.\[\]]+ as \w+\*\)|[\w.\[\]]+)\.accept\(([\w.]+(?: as \w+\*)?)\)")


def read(path):
    with io.open(path, encoding="utf-8", newline="") as fh:
        return fh.read().splitlines(True)


def write(path, lines):
    with io.open(path, "w", encoding="utf-8", newline="") as fh:
        fh.write("".join(lines))


def convert_type(path, owner):
    lines = read(path)
    out, inside, sigs, recv = [], False, 0, 0
    i = 0
    while i < len(lines):
        line = lines[i]
        if re.match(r"type class %s : BaseASTVisitor \{" % owner, line):
            eol = line[len(line.rstrip("\r\n")):]
            out.append("type struct %s {%s" % (owner, eol))
            inside = True
            if lines[i + 1].strip() == "public:":
                i += 1
            i += 1
            continue
        if inside and line.rstrip("\r\n") == "}":
            inside = False
        m = SIG.match(line)
        if m and m.group(3) in SUFFIX:
            line = "%svisit_%s(mut &this, %s: %s*)%s" % (
                m.group(1), SUFFIX[m.group(3)], m.group(2), m.group(3), line[m.end():])
            sigs += 1
        elif inside:
            n = METHOD.match(line)
            if n and "&this" not in line and n.group(1) != owner:
                rest = line[n.end():]
                line = "    %s(mut &this%s%s" % (n.group(1), "" if rest.startswith(")") else ", ", rest)
                recv += 1
        out.append(line)
        i += 1
    write(path, out)
    left = [k + 1 for k, l in enumerate(out) if "this.visit(" in l or ".accept(" in l]
    print("%s: %d visit signatures, %d receivers added; by hand: %s" % (path, sigs, recv, left))


def retype(newtype, paths):
    for path in paths:
        lines = read(path)
        n_t = n_a = 0
        out = []
        for line in lines:
            if line.lstrip().startswith("import "):
                out.append(line)
                continue
            line, k = re.subn(r"\bASTVisitor\*", newtype + "*", line)
            n_t += k
            line, k = ACCEPT.subn(lambda m: "%s.dispatch_node(%s)" % (m.group(2), m.group(1)), line)
            n_a += k
            out.append(line)
        write(path, out)
        print("%s: %d types, %d accepts" % (path, n_t, n_a))


def strip(paths):
    for path in paths:
        lines = read(path)
        out, n = [], 0
        i = 0
        while i < len(lines):
            if lines[i].strip() == "override accept(visitor: ASTVisitor*) -> void {":
                j = i
                while lines[j].rstrip("\r\n") != "    }":
                    j += 1
                if out and out[-1].strip() == "":
                    out.pop()
                i = j + 1
                n += 1
                continue
            out.append(lines[i])
            i += 1
        write(path, out)
        print("%s: %d accept methods removed" % (path, n))


def unimport(paths):
    """Drop the module `visitor` from `compiler::ast::{ ... }` import lists."""
    for path in paths:
        lines = read(path)
        out, n, in_list = [], 0, False
        for line in lines:
            if line.startswith("import compiler::ast::{"):
                in_list = True
            if in_list and re.search(r"\bvisitor\b", line):
                line = re.sub(r",\s*visitor\b", "", line)
                line = re.sub(r"\bvisitor,\s*", "", line)
                n += 1
            if in_list and "}" in line:
                in_list = False
            out.append(line)
        write(path, out)
        print("%s: %d import lists" % (path, n))


def seam(paths):
    """`this.dispatch.resolve_expr(visitor, e)` -> `visitor.resolve_expr(e)` and
    `this.dispatch.dispatch_stmt(visitor, s)` -> `visitor.dispatch_node(s)`."""
    for path in paths:
        lines = read(path)
        out, n = [], 0
        for line in lines:
            line, k1 = re.subn(r"this\.dispatch\.resolve_expr\(visitor, ", "visitor.resolve_expr(", line)
            line, k2 = re.subn(r"this\.dispatch\.dispatch_stmt\(visitor, ", "visitor.dispatch_node(", line)
            n += k1 + k2
            out.append(line)
        write(path, out)
        print("%s: %d calls" % (path, n))


def returns(path):
    """Each `visit_*` method that ends by setting `this.result` returns the
    copy instead: `-> void {` -> `-> ASTNode* {`, `this.result = x;` ->
    `return x;`.  Refuses a method that sets it other than once, last."""
    lines = read(path)
    out, n = [], 0
    i = 0
    while i < len(lines):
        line = lines[i]
        if re.match(r"    visit_\w+\(mut &this, .*\) -> void \{\s*$", line):
            j = i + 1
            while lines[j].rstrip("\r\n") != "    }":
                j += 1
            body = lines[i + 1:j]
            sets = [k for k, l in enumerate(body) if re.search(r"this\.result\s*=", l)]
            last = max(k for k, l in enumerate(body) if l.strip())
            if sets != [last]:
                raise SystemExit("%s:%d: result is not set once, last" % (path, i + 1))
            out.append(line.replace("-> void {", "-> ASTNode* {"))
            for k, l in enumerate(body):
                if k == last:
                    l = re.sub(r"this\.result\s*=\s*", "return ", l)
                out.append(l)
            out.append(lines[j])
            i = j + 1
            n += 1
            continue
        out.append(line)
        i += 1
    write(path, out)
    print("%s: %d methods return their copy" % (path, n))


if sys.argv[1] == "returns":
    returns(sys.argv[2])
elif sys.argv[1] == "seam":
    seam(sys.argv[2:])
elif sys.argv[1] == "unimport":
    unimport(sys.argv[2:])
elif sys.argv[1] == "type":
    convert_type(sys.argv[2], sys.argv[3])
elif sys.argv[1] == "retype":
    retype(sys.argv[2], sys.argv[3:])
elif sys.argv[1] == "strip":
    strip(sys.argv[2:])
