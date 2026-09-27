"""The editor server's answers to questions that have one right answer and
one wrong one, checked against the right one.

The fixture (`fixture/`, which the pinned compiler builds) declares the
same type, method and free function in two modules (`Probe::A`,
`Probe::B`), an `implement` block for one of them, a shadowed local, a
match whose arms read a constant and a binding named like an enum's
variant, and a brace import.  Every check asks a question whose answer
differs between the two candidates, and states the right one: what the
program binds.  Definitions are checked by the declaration's file and
line, hovers by text they must (and must not) contain, completions by an
item they must hold, code lenses by where they point.

Positions are found by text, never by line and column, so an edit to the
fixture cannot silently retarget a check: a check whose text is not found
exactly once is an error of the harness, not a wrong answer.

Usage: python editor_answers.py [server.exe]
  default server: tools/CryoLSP/build/gate/cryolsp.exe, the one
  `make lsp-check` links with the compiler under test.
Prints one line per check and a last line `wrong-or-missing W of N`.
Exit 0 when every check ran (whatever the answers), 2 on a harness error.
"""
import importlib.util
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
FIX = os.path.join(HERE, "fixture")
spec = importlib.util.spec_from_file_location(
    "smoke", os.path.join(ROOT, "tools", "CryoLSP", "tests", "smoke.py"))
smoke = importlib.util.module_from_spec(spec)
spec.loader.exec_module(smoke)


def line_of(fname, text):
    """1-based line of the one line in `fname` containing `text`."""
    with open(os.path.join(FIX, "src", fname), encoding="utf-8") as fh:
        lines = fh.read().split("\n")
    hits = [i for i, l in enumerate(lines) if text in l]
    if len(hits) != 1:
        raise SystemExit("harness: %r found %d times in %s" % (text, len(hits), fname))
    return hits[0] + 1


# (label, file, the line's text, the token's text within it, offset into
#  the token, method, expectation, optional buffer edit (old, new))
#
# expectations:
#   ("def", file, line)          the definition's location
#   ("hover", must, must_not)    substrings of the hover text
#   ("compl", label)             an item with this label
#   ("lens_not", file)           no lens points into `file`
#   ("lens_has", file, line)     some lens points at file:line
CHECKS = [
    # Two modules declare `Item`, `make`, `get` and `helper`; the call names B's.
    ("qualified static call -> B's make", "main.cryo", "B::Item::make(4)", "make", 1, "definition",
     ("def", "b.cryo", line_of("b.cryo", "static make"))),
    ("qualified static call hover -> B's signature", "main.cryo", "B::Item::make(4)", "make", 1, "hover",
     ("hover", "i64", None)),
    ("method on B's Item -> B's get", "main.cryo", "wb.get()", "get", 1, "definition",
     ("def", "b.cryo", line_of("b.cryo", "get(&this)"))),
    ("method on B's Item hover -> returns i64", "main.cryo", "wb.get()", "get", 1, "hover",
     ("hover", "i64", None)),
    ("qualified free call -> B's helper", "main.cryo", "B::helper()", "helper", 1, "definition",
     ("def", "b.cryo", line_of("b.cryo", "function helper"))),
    # Inside B's own file, `Item` and `this` mean B's Item.
    ("this in B's method -> B's Item", "b.cryo", "get(&this) -> i64", "this.by", 1, "definition",
     ("def", "b.cryo", line_of("b.cryo", "type struct Item"))),
    ("return type Item in b.cryo -> B's Item", "b.cryo", "static make", "-> Item", 4, "definition",
     ("def", "b.cryo", line_of("b.cryo", "type struct Item"))),
    ("return type Item in b.cryo hover -> B's fields", "b.cryo", "static make", "-> Item", 4, "hover",
     ("hover", "by", "ax")),
    ("struct literal Item in b.cryo hover -> B's fields", "b.cryo", "static make", "Item { by", 1, "hover",
     ("hover", "by", "ax")),
    ("implementations lens on A's Item -> none in b.cryo", "a.cryo", None, None, 0, "codeLens",
     ("lens_not", "b.cryo")),
    ("implementations lens on B's Item -> b.cryo's implement", "b.cryo", None, None, 0, "codeLens",
     ("lens_has", "b.cryo", line_of("b.cryo", "implement Item"))),
    # A match arm: `LIMIT` compares with the constant, `Ready` binds the subject.
    ("arm pattern Ready hover -> a binding, not the variant", "main.cryo", "Ready => {", "Ready", 1, "hover",
     ("hover", "i64", "Mode")),
    ("Ready in arm body hover -> the binding, not a constant", "main.cryo", "Ready => {", "Ready + 100", 1, "hover",
     ("hover", "i64", "const")),
    ("arm pattern LIMIT hover -> the constant", "main.cryo", "LIMIT => {", "LIMIT", 1, "hover",
     ("hover", "LIMIT", None)),
    ("arm pattern LIMIT -> the constant's declaration", "main.cryo", "LIMIT => {", "LIMIT", 1, "definition",
     ("def", "main.cryo", line_of("main.cryo", "const LIMIT"))),
    # A qualified type annotation names B's Item.
    ("annotation B::Item -> B's Item", "main.cryo", "const wb: B::Item", "Item", 1, "definition",
     ("def", "b.cryo", line_of("b.cryo", "type struct Item"))),
    ("annotation B::Item hover -> B's fields", "main.cryo", "const wb: B::Item", "Item", 1, "hover",
     ("hover", "by", "ax")),
    # Scope completion on a module-relative path.
    ("completion after B::Item:: -> make", "main.cryo", "B::Item::make(4)", "make", 0, "completion",
     ("compl", "make")),
    ("completion after Probe::B::Item:: -> make", "main.cryo", "Probe::B::Item::make(4)", "make", 0, "completion",
     ("compl", "make"), ("B::Item::make(4)", "Probe::B::Item::make(4)")),
    # A shadowed local.
    ("inner x -> the inner declaration", "main.cryo", "const y: i64 = x;", "x;", 0, "definition",
     ("def", "main.cryo", line_of("main.cryo", "const x: i64 = 2;"))),
    ("outer x -> the outer declaration", "main.cryo", "const z: i32 = x;", "x;", 0, "definition",
     ("def", "main.cryo", line_of("main.cryo", "const x: i32 = 1;"))),
    # A brace import in another module.
    ("brace-imported Formatter -> C's Formatter", "d.cryo", "const f: Formatter", "Formatter", 1, "definition",
     ("def", "c.cryo", line_of("c.cryo", "type struct Formatter"))),
    ("method through a brace import -> C's width", "d.cryo", "return f.width()", "width", 1, "definition",
     ("def", "c.cryo", line_of("c.cryo", "width(&this)"))),
    ("completion after f. -> width", "main.cryo", "f.width()", "width", 0, "completion",
     ("compl", "width")),
    # A buffer with a syntax error elsewhere still answers.
    ("outer x in a broken buffer -> the outer declaration", "main.cryo", "const z: i32 = x;", "x;", 0, "definition",
     ("def", "main.cryo", line_of("main.cryo", "const x: i32 = 1;")),
     ("const w: i32 = f.width();", "const w: i32 = f.")),
]


def position(text, line_has, token, off):
    lines = text.split("\n")
    hits = [i for i, l in enumerate(lines) if line_has in l]
    if len(hits) != 1:
        raise SystemExit("harness: %r found %d times" % (line_has, len(hits)))
    li = hits[0]
    if lines[li].count(token) < 1:
        raise SystemExit("harness: %r not in line %r" % (token, lines[li]))
    return li, lines[li].index(token) + off


def verdict(method, res, exp):
    kind = exp[0]
    if kind == "def":
        if isinstance(res, list):
            res = res[0] if res else None
        if not isinstance(res, dict):
            return False, "null"
        got_file = res.get("uri", "").rsplit("/", 1)[-1]
        got_line = res.get("range", {}).get("start", {}).get("line", -1) + 1
        got = "%s:%d" % (got_file, got_line)
        return (got_file == exp[1] and got_line == exp[2]), got
    if kind == "hover":
        if not isinstance(res, dict):
            return False, "null"
        c = res.get("contents", {})
        val = c.get("value", "") if isinstance(c, dict) else json.dumps(c)
        ok = exp[1] in val and (exp[2] is None or exp[2] not in val)
        return ok, json.dumps(val)[:120]
    if kind == "compl":
        items = res.get("items", []) if isinstance(res, dict) else (res or [])
        labels = [i.get("label") for i in items]
        return (exp[1] in labels), "%d items: %s" % (len(labels), ", ".join(map(str, labels[:8])))
    if kind in ("lens_not", "lens_has"):
        where = []
        for lens in (res or []):
            args = (lens.get("command") or {}).get("arguments") or []
            locs = args[2] if len(args) > 2 and isinstance(args[2], list) else []
            for l in locs:
                where.append("%s:%d" % (l.get("uri", "").rsplit("/", 1)[-1],
                                        l.get("range", {}).get("start", {}).get("line", -1) + 1))
        if kind == "lens_not":
            return (not any(w.startswith(exp[1] + ":") for w in where)), "[%s]" % ", ".join(where)
        return ("%s:%d" % (exp[1], exp[2]) in where), "[%s]" % ", ".join(where)
    raise SystemExit("harness: unknown expectation %r" % (exp,))


def main():
    server = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        ROOT, "tools", "CryoLSP", "build", "gate",
        "cryolsp.exe" if os.name == "nt" else "cryolsp")
    if not os.path.exists(server):
        print("harness: no server at %s (make lsp-check builds it)" % server)
        return 2
    srv = smoke.Server(server, smoke.uri_for(FIX))
    srv.request("initialize", {
        "processId": os.getpid(), "rootUri": smoke.uri_for(FIX),
        "capabilities": {"general": {"positionEncodings": ["utf-8"]}},
    }, timeout=120)
    srv.notify("initialized", {})
    texts, versions = {}, {}
    wrong = 0
    for check in CHECKS:
        label, fname, line_has, token, off, method, exp = check[:7]
        edit = check[7] if len(check) > 7 else None
        path = os.path.join(FIX, "src", fname)
        uri = smoke.uri_for(path)
        if fname not in texts:
            with open(path, encoding="utf-8") as fh:
                texts[fname] = fh.read()
            versions[fname] = 1
            srv.notify("textDocument/didOpen", {"textDocument": {
                "uri": uri, "languageId": "cryo", "version": 1, "text": texts[fname]}})
        text = texts[fname]
        if edit is not None:
            if text.count(edit[0]) != 1:
                raise SystemExit("harness: edit target %r found %d times" % (edit[0], text.count(edit[0])))
            text = text.replace(edit[0], edit[1])
        if text != texts[fname] or edit is not None:
            versions[fname] += 1
            srv.notify("textDocument/didChange", {
                "textDocument": {"uri": uri, "version": versions[fname]},
                "contentChanges": [{"text": text}]})
        if method == "codeLens":
            r = srv.request("textDocument/codeLens", {"textDocument": {"uri": uri}}, timeout=180)
        else:
            # A completion's cursor is the token's start: right after the
            # `::` or `.` it completes.
            li, ci = position(text, line_has, token, off)
            r = srv.request("textDocument/" + method, {"textDocument": {"uri": uri},
                            "position": {"line": li, "character": ci}}, timeout=180)
        ok, got = verdict(method, r.get("result"), exp)
        if not ok:
            wrong += 1
        print("%-5s %-58s %s" % ("ok" if ok else "WRONG", label, got))
        sys.stdout.flush()
        if edit is not None:
            # Put the buffer back for the checks after this one.
            versions[fname] += 1
            srv.notify("textDocument/didChange", {
                "textDocument": {"uri": uri, "version": versions[fname]},
                "contentChanges": [{"text": texts[fname]}]})
    try:
        srv.request("shutdown", {}, timeout=20)
        srv.notify("exit", {})
        srv.proc.wait(timeout=10)
    except Exception:
        srv.proc.kill()
    print("wrong-or-missing %d of %d" % (wrong, len(CHECKS)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
