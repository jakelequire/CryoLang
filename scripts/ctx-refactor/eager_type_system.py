"""Delete the null checks on the compilation context's resolver and type
system, which the context now builds in its constructor.

`ctx.resolver`, `ctx.type_arena`, `ctx.generic_registry` and
`ctx.type_resolver` were made lazily, by the first pass that needed them, and
every reader guarded against the not-yet-made case.  They exist from `new`
on, so each guard is a constant.  This rewrites the shapes a guard takes:

    if (G == null [|| G2 == null]) { return ..; }  -> deleted
    if (G != null) { <one line> }          -> <one line>
    G != null && rest  /  rest && G != null -> rest
    G == null || rest  /  rest || G == null -> rest
    if (G != null) {  ... }                 -> the block's body, dedented

`G` is one of the four fields read through a context receiver.  Anything
else that mentions one of them next to `null` is printed and left for a
person: `--check` lists what it would do and exits non-zero if any guard is
left.

    python scripts/ctx-refactor/eager_type_system.py [--check]
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DIRS = ["compiler/src", "tools/CryoLSP/src"]
FIELDS = "resolver|type_arena|generic_registry|type_resolver"
RECV = r"(?:\bthis\.cg\.ctx|\bthis\.ctx|\bctx|\bctx_ptr|\bcg_ctx|\bc)"
G = r"%s\.(?:%s)" % (RECV, FIELDS)
SKIP = {"compiler/src/compiler/compilation_context.cryo"}

GUARD_ANY = re.compile(r"%s\s*(?:==|!=)\s*null" % G)
RETURN_GUARD = re.compile(r"^(\s*)if \(%s\s*==\s*null(?:\s*\|\|\s*%s\s*==\s*null)*\)\s*\{ return[^}]*\}\s*$" % (G, G))
ONE_LINE_POS = re.compile(r"^(\s*)if \(%s\s*!=\s*null\) \{ (.*) \}\s*$" % G)
BLOCK_POS = re.compile(r"^(\s*)if \(%s\s*!=\s*null\) \{\s*$" % G)
AND_LEFT = re.compile(r"%s\s*!=\s*null\s*&&\s*" % G)
AND_RIGHT = re.compile(r"\s*&&\s*%s\s*!=\s*null" % G)
OR_LEFT = re.compile(r"%s\s*==\s*null\s*\|\|\s*" % G)
OR_RIGHT = re.compile(r"\s*\|\|\s*%s\s*==\s*null" % G)


def rewrite(lines, rel, log):
    out = []
    i = 0
    while i < len(lines):
        line = lines[i]
        body = line.rstrip("\r")
        cr = line[len(body):]
        if not GUARD_ANY.search(body.split("//")[0]):
            out.append(line)
            i += 1
            continue
        m = RETURN_GUARD.match(body)
        if m:
            log.append("%s:%d: deleted  %s" % (rel, i + 1, body.strip()))
            i += 1
            continue
        m = ONE_LINE_POS.match(body)
        if m:
            out.append(m.group(1) + m.group(2) + cr)
            log.append("%s:%d: unwrapped %s" % (rel, i + 1, body.strip()))
            i += 1
            continue
        m = BLOCK_POS.match(body)
        if m:
            indent = m.group(1)
            j = i + 1
            depth = 1
            inner = []
            while j < len(lines):
                t = lines[j].rstrip("\r")
                code = t.split("//")[0]
                if depth == 1 and t.strip() == "}" and t.startswith(indent + "}"):
                    break
                depth += code.count("{") - code.count("}")
                inner.append(lines[j])
                j += 1
            else:
                log.append("%s:%d: UNMATCHED  %s" % (rel, i + 1, body.strip()))
                out.append(line)
                i += 1
                continue
            for t in inner:
                out.append(t[4:] if t.startswith(indent + "    ") else t)
            log.append("%s:%d: unwrapped block (%d lines)" % (rel, i + 1, len(inner)))
            i = j + 1
            continue
        new = body
        for pat in (AND_LEFT, AND_RIGHT, OR_LEFT, OR_RIGHT):
            new = pat.sub("", new)
        if new != body and not GUARD_ANY.search(new.split("//")[0]):
            out.append(new + cr)
            log.append("%s:%d: dropped term %s" % (rel, i + 1, body.strip()))
            i += 1
            continue
        log.append("%s:%d: LEFT  %s" % (rel, i + 1, body.strip()))
        out.append(line)
        i += 1
    return out


def main():
    check = "--check" in sys.argv
    log = []
    for d in DIRS:
        for dp, _, fns in os.walk(os.path.join(ROOT, d)):
            if os.sep + "build" in dp:
                continue
            for fn in fns:
                if not fn.endswith(".cryo"):
                    continue
                path = os.path.join(dp, fn)
                rel = os.path.relpath(path, ROOT).replace(os.sep, "/")
                if rel in SKIP:
                    continue
                text = open(path, "rb").read().decode("utf-8")
                lines = text.split("\n")
                new = rewrite(lines, rel, log)
                if new != lines and not check:
                    open(path, "wb").write("\n".join(new).encode("utf-8"))
    for l in log:
        print(l)
    left = [l for l in log if "LEFT" in l or "UNMATCHED" in l]
    print("guards: %d, left for a person: %d" % (len(log), len(left)))
    return 1 if left else 0


if __name__ == "__main__":
    sys.exit(main())
