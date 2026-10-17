"""One-off codemod for the statement conversion.

Renames the statement header class `StatementNode` to `Stmt` and turns each
`new XStmtNode(args)` construction into the matching `Stmt` factory, whose
parameters are the old constructor's in the same order.  Every other use of
a per-kind statement class but the block's (a cast, a typed local, a field)
is left as written: those names no longer exist, so the type check points at
each one and it is rewritten by hand to a `match` on `Stmt.shape` or a
payload type.

Last, each file's import from `compiler::ast::statement` is set to the names
the file uses, so the hand rewrite never edits an import list.  Rerunning it
after a hand edit is safe: every step is idempotent.

Run from the repository root.  Deleted in the commit after the one it lands
in.
"""

import pathlib
import re
import sys

FACTORIES = {
    "BlockStmtNode": "block",
    "UnsafeBlockStmtNode": "unsafe_block",
    "AsmBlockStmtNode": "asm_block",
    "ReturnStmtNode": "return_stmt",
    "IfStmtNode": "if_stmt",
    "WhileStmtNode": "while_stmt",
    "DoWhileStmtNode": "do_while_stmt",
    "ForStmtNode": "for_stmt",
    "MatchStmtNode": "match_stmt",
    "StaticMatchStmtNode": "static_match_stmt",
    "BreakStmtNode": "break_stmt",
    "ContinueStmtNode": "continue_stmt",
    "ExprStmtNode": "expr_stmt",
    "DeclStmtNode": "decl_stmt",
    "LoopStmtNode": "loop_stmt",
}

# Every name `compiler::ast::statement` offers.
EXPORTS = [
    "AsmBlockStmt", "AsmConstraintKind", "AsmDialect", "AsmOpDir", "AsmOperand",
    "AsmPiece", "BlockStmt", "DeclStmt", "DoWhileStmt", "ExprStmt", "ForStmt",
    "IfStmt", "LoopStmt", "MatchStmt", "ReturnStmt", "StaticMatchStmt", "Stmt",
    "StmtKind", "UnsafeBlockStmt", "WhileStmt",
]

ROOTS = ["compiler/src", "tools/CryoLSP/src"]
SKIP = {pathlib.Path("compiler/src/compiler/AST/statement.cryo")}

IMPORT = re.compile(r"[ \t]*import compiler::ast::statement::\{[^}]*\};[ \t]*\r?\n")
ANY_IMPORT = re.compile(r"^import [^\n]*;[ \t]*\r?\n", re.M)


def rewrite(text: str) -> str:
    text = re.sub(r"\bStatementNode\b", "Stmt", text)
    for cls, factory in FACTORIES.items():
        text = re.sub(r"\bnew\s+" + cls + r"\s*\(", "Stmt::" + factory + "(", text)
    # A block is held as the statement it is (a function's body, the block
    # under `unsafe`, a block being built), so its class name becomes the
    # header's; each read of a block's own fields then refuses until it goes
    # through `as_block()` or a `match`.
    text = re.sub(r"\bBlockStmtNode\b", "Stmt", text)
    # A statement just built by a factory is the header the factory returns.
    text = re.sub(r"\b(mut|const)(\s+\w+\s*):\s*\w+StmtNode\*(\s*=\s*Stmt::)", r"\1\2: Stmt*\3", text)
    # The parser's per-kind functions return what the factories build.
    text = re.sub(r"(\bparse_\w+\([^)]*\)\s*)->\s*\w+StmtNode\*", r"\1-> Stmt*", text)
    text = drop_same_type_casts(text)
    return fix_import(text)


def drop_same_type_casts(text: str) -> str:
    """Delete `x as Stmt*` where every declaration of `x` in the file is a `Stmt*`.

    The old per-kind classes were cast up to the header class; with the
    header now the only class, those casts name the type the value already
    has.  A name declared with any other type somewhere in the file keeps
    its casts, for the type check to judge.
    """
    decls: dict = {}
    for m in re.finditer(r"\b(\w+)\s*:\s*([\w:<>\[\]]+\*?)", text):
        decls.setdefault(m.group(1), set()).add(m.group(2))
    # A name a `match` arm binds to a payload is not a `Stmt*`.
    for m in re.finditer(r"\bStmtKind::\w+\((\w+)\)", text):
        decls.setdefault(m.group(1), set()).add("payload")

    def repl(m: re.Match) -> str:
        name = m.group(1)
        return name if decls.get(name) == {"Stmt*"} else m.group(0)

    text = re.sub(r"\b(\w+) as Stmt\*", repl, text)

    # A block's own fields read through a name held as `Stmt*`: the name held
    # a block before the rename (every other kind was held as its own class),
    # so the read goes through the block projection.
    def block_repl(m: re.Match) -> str:
        name = m.group(1)
        if decls.get(name) != {"Stmt*"}:
            return m.group(0)
        return name + ".as_block()." + m.group(2)

    text = re.sub(r"\b(\w+)\.(statements\b|add_statement\(|synth_drops\b|attached_directives\b"
                  r"|add_directive\(|has_directives\()", block_repl, text)
    # The async lowering's state blocks (`PollSm.blocks`, a `[Stmt*]` of
    # blocks) likewise.
    text = re.sub(r"(\bsm\.blocks\[[^\]\[]*\])\.(statements\b|add_statement\()", r"\1.as_block().\2", text)

    # A factory's result is already a `Stmt*`.
    out, pos = [], 0
    for m in re.finditer(r"\bStmt::\w+\(", text):
        if m.start() < pos:
            continue
        depth, i = 0, m.end() - 1
        while i < len(text):
            if text[i] == "(":
                depth += 1
            elif text[i] == ")":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        tail = re.match(r"\s*as Stmt\*", text[i + 1:])
        if tail:
            out.append(text[pos:i + 1])
            pos = i + 1 + tail.end()
    out.append(text[pos:])
    return "".join(out)


def fix_import(text: str) -> str:
    """Set the file's statement import to exactly the names it uses."""
    nl = "\r\n" if "\r\n" in text else "\n"
    m = IMPORT.search(text)
    body = IMPORT.sub("", text) if m else text
    used = [n for n in EXPORTS if re.search(r"(?<![\w:])" + n + r"\b", strip_comments(body))]
    if not used:
        return body if m else text
    line = "import compiler::ast::statement::{ " + ", ".join(used) + " };"
    if len(line) > 100:
        rows, row = [], "   "
        for n in used:
            piece = " " + n + ","
            if len(row) + len(piece) > 96:
                rows.append(row)
                row = "   "
            row += piece
        rows.append(row.rstrip(","))
        line = "import compiler::ast::statement::{" + nl + nl.join(rows) + nl + "};"
    line += nl
    if m:
        return body[: m.start()] + line + body[m.start():]
    imports = list(ANY_IMPORT.finditer(body))
    if not imports:
        sys.exit("no import to place the statement import after")
    at = imports[-1].end()
    return body[:at] + line + body[at:]


def strip_comments(text: str) -> str:
    return re.sub(r"//[^\n]*", "", text)


HEADER_FIELDS = r"(span|has_error|generated|kind)"


def body_end(text: str, open_at: int) -> int:
    """Index just past the `}` matching the `{` at `open_at`."""
    depth, i, n = 0, open_at, len(text)
    while i < n:
        c = text[i]
        if c == "/" and text.startswith("//", i):
            i = text.find("\n", i)
            if i < 0:
                return n
            continue
        if c == '"' or c == "'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            i = j + 1
            continue
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    sys.exit("unbalanced braces")


def hook(path: str, func: str, payload: str, header: str) -> None:
    """Give the statement function `func` in `path` the header and payload.

    The first parameter of `func` typed as a statement, `<v>: <statement
    type>`, becomes `<header>: Stmt*, <v>: <payload>*`, and inside its body
    each read of a header field through `<v>` reads `<header>`.  Used for a
    pass's per-kind function, which its dispatch now hands both.
    """
    p = pathlib.Path(path)
    text = p.read_bytes().decode("utf-8")
    sig = re.compile(r"\b" + func + r"\(([^)]*?)\b(\w+)\s*:\s*(\w+StmtNode|Stmt)\*")
    m = sig.search(text)
    if not m:
        sys.exit(f"{func}: signature not found")
    var = m.group(2)
    new_sig = f"{func}({m.group(1)}{header}: Stmt*, {var}: {payload}*"
    open_at = text.index("{", m.end())
    end = body_end(text, open_at)
    body = re.sub(r"\b" + var + r"\." + HEADER_FIELDS + r"\b", header + r".\1", text[open_at:end])
    text = text[: m.start()] + new_sig + text[m.end():open_at] + body + text[end:]
    p.write_bytes(text.encode("utf-8"))
    print(f"{func}: {var} -> {header}: Stmt*, {var}: {payload}*")


def main() -> int:
    if len(sys.argv) > 1 and sys.argv[1] == "--hook":
        # --hook FILE HEADER FUNC=Payload ...
        path, header = sys.argv[2], sys.argv[3]
        for spec in sys.argv[4:]:
            func, payload = spec.split("=")
            hook(path, func, payload, header)
        return 0
    changed = 0
    for root in ROOTS:
        for path in sorted(pathlib.Path(root).rglob("*.cryo")):
            if path in SKIP:
                continue
            raw = path.read_bytes().decode("utf-8")
            out = rewrite(raw)
            if out != raw:
                path.write_bytes(out.encode("utf-8"))
                changed += 1
    print(f"{changed} file(s) rewritten")
    return 0


if __name__ == "__main__":
    sys.exit(main())
