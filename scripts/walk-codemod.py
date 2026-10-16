"""One-off codemod: a visitor's `override visit(n: XNode*)` methods become the
`Walk` trait's hooks, and `e.accept(this)` becomes `this.walk_node(e)`.

    python scripts/walk-codemod.py FILE...
    python scripts/walk-codemod.py --split FILE OWNER

Only those two shapes are rewritten.  A direct `this.visit(x)` names a hook
by the static type of `x`, which this script cannot see, so it is left for a
hand edit and reported.  `--split` then moves the renamed hooks out of the
type's body into the type's `Walk` implementation, placed right after the
body's closing `}`.
"""
import io
import re
import sys

HOOKS = {
    "ProgramNode": "walk_program",
    "LiteralNode": "walk_literal",
    "IdentifierNode": "walk_identifier",
    "BinaryExprNode": "walk_binary",
    "UnaryExprNode": "walk_unary",
    "TernaryExprNode": "walk_ternary",
    "IfExprNode": "walk_if_expr",
    "MatchExprNode": "walk_match_expr",
    "StaticMatchExprNode": "walk_static_match_expr",
    "CallExprNode": "walk_call",
    "NewExprNode": "walk_new",
    "SizeofExprNode": "walk_sizeof",
    "AlignofExprNode": "walk_alignof",
    "CastExprNode": "walk_cast",
    "StructLiteralNode": "walk_struct_literal",
    "ArrayLiteralNode": "walk_array_literal",
    "TupleLiteralNode": "walk_tuple_literal",
    "LambdaExprNode": "walk_lambda",
    "ArrayAccessNode": "walk_array_access",
    "MemberAccessNode": "walk_member_access",
    "ScopeResolutionNode": "walk_scope_resolution",
    "TypeofExprNode": "walk_typeof",
    "DeleteExprNode": "walk_delete",
    "AwaitExprNode": "walk_await",
    "YieldExprNode": "walk_yield",
    "TryExprNode": "walk_try",
    "BlockStmtNode": "walk_block",
    "UnsafeBlockStmtNode": "walk_unsafe_block",
    "AsmBlockStmtNode": "walk_asm_block",
    "ReturnStmtNode": "walk_return",
    "IfStmtNode": "walk_if",
    "WhileStmtNode": "walk_while",
    "DoWhileStmtNode": "walk_do_while",
    "ForStmtNode": "walk_for",
    "MatchStmtNode": "walk_match_stmt",
    "StaticMatchStmtNode": "walk_static_match_stmt",
    "BreakStmtNode": "walk_break",
    "ContinueStmtNode": "walk_continue",
    "ExprStmtNode": "walk_expr_stmt",
    "DeclStmtNode": "walk_decl_stmt",
    "LoopStmtNode": "walk_loop",
    "VarDeclNode": "walk_var",
    "DestructureDeclNode": "walk_destructure",
    "FunctionDeclNode": "walk_function",
    "MethodNode": "walk_method",
    "IntrinsicDeclNode": "walk_intrinsic",
    "IntrinsicConstDeclNode": "walk_intrinsic_const",
    "ImportDeclNode": "walk_import",
    "ModuleDeclNode": "walk_module",
    "StructDeclNode": "walk_struct",
    "UnionDeclNode": "walk_union",
    "ClassDeclNode": "walk_class",
    "TraitDeclNode": "walk_trait",
    "TypeAliasDeclNode": "walk_type_alias",
    "EnumDeclNode": "walk_enum",
    "EnumVariantNode": "walk_enum_variant",
    "ImplBlockNode": "walk_impl",
    "ExternBlockNode": "walk_extern_block",
    "FieldDeclNode": "walk_field",
    "GenericParamNode": "walk_generic_param",
    "MatchArmNode": "walk_match_arm",
    "StaticMatchArmNode": "walk_static_match_arm",
    "PatternNode": "walk_pattern",
    "EnumPatternNode": "walk_enum_pattern",
    "DirectiveNode": "walk_directive",
}

SIG = re.compile(r"^(\s*)override visit\((\w+): (\w+)\*\) -> void \{")
# The receiver: a parenthesized cast, or a path of fields and indexing.
ACCEPT = re.compile(r"(\([\w.\[\]]+ as \w+\*\)|[\w.\[\]]+)\.accept\(this\)")


def convert(path):
    with io.open(path, encoding="utf-8", newline="") as fh:
        text = fh.read()
    out = []
    sigs = accepts = 0
    for line in text.splitlines(True):
        m = SIG.match(line)
        if m and m.group(3) in HOOKS:
            line = "%s%s(mut &this, %s: %s*) -> void {%s" % (
                m.group(1), HOOKS[m.group(3)], m.group(2), m.group(3), line[m.end():])
            sigs += 1
        line, k = ACCEPT.subn(lambda n: "this.walk_node(" + n.group(1) + ")", line)
        accepts += k
        out.append(line)
    with io.open(path, "w", encoding="utf-8", newline="") as fh:
        fh.write("".join(out))
    left = [i + 1 for i, l in enumerate(out) if "this.visit(" in l or ".accept(" in l]
    print("%s: %d signatures, %d accepts; left by hand at lines %s" % (path, sigs, accepts, left))


HOOK_SIG = re.compile(r"^    (walk_\w+)\(mut &this, ")


def split(path, owner):
    """Move every hook method of the type whose body is `owner`'s - the
    methods `convert` renamed, with the comment lines right above each - out
    of the type's body into one `implement trait Walk for struct <owner>`
    block after it.  A method ends at the first line that is `    }`."""
    with io.open(path, encoding="utf-8", newline="") as fh:
        lines = fh.read().splitlines(True)
    keep, hooks = [], []
    i = 0
    while i < len(lines):
        if HOOK_SIG.match(lines[i]):
            start = len(keep)
            while start > 0 and keep[start - 1].lstrip().startswith("//"):
                start -= 1
            block = keep[start:]
            del keep[start:]
            while True:
                block.append(lines[i])
                i += 1
                if lines[i - 1].rstrip("\r\n") == "    }":
                    break
            if keep and keep[-1].strip() == "" and i < len(lines) and lines[i].strip() == "":
                i += 1
            hooks.append(block)
            continue
        keep.append(lines[i])
        i += 1
    eol = "\r\n" if lines[0].endswith("\r\n") else "\n"
    head = min(k for k, l in enumerate(keep)
               if re.match(r"type (class|struct) %s\b" % owner, l))
    end = min(k for k, l in enumerate(keep) if k > head and l.rstrip("\r\n") == "}")
    body = [eol, "implement trait Walk for struct %s {%s" % (owner, eol)]
    for n, block in enumerate(hooks):
        if n > 0:
            body.append(eol)
        body.extend(block)
    body.append("}" + eol)
    out = keep[:end + 1] + body + keep[end + 1:]
    with io.open(path, "w", encoding="utf-8", newline="") as fh:
        fh.write("".join(out))
    print("%s: moved %d hooks into the Walk implementation for %s" % (path, len(hooks), owner))


if len(sys.argv) > 2 and sys.argv[1] == "--split":
    split(sys.argv[2], sys.argv[3])
else:
    for p in sys.argv[1:]:
        convert(p)
