"""One-off codemod: postfix array types to bracketed ones.

    T[]  -> [T]          T[N] -> [T; N]

The positions come from the compiler's own parser, never from a pattern over
the text: `a[i]` and `T[N]` share their brackets.  A compiler built with
`array-codemod-trace.patch` applied prints one line for every postfix array
suffix it parses, when CRYO_ARRAY_TRACE is set:

    @@ARRSPAN@@ <file> <start line> <start col> <`[` line> <`[` col>
                <`]` line> <`]` col> <D|F> <element is a tuple: 0|1>

`start` is the type's first token (a group's `(` included), `[` opens the
suffix and `]` closes it; columns are 1-based.  The element is the text from
`start` up to the `[`.  A grouping `( ... )` around a non-tuple element is
dropped (`(&T)[]` -> `[&T]`); a tuple keeps its parentheses.  Arrays nest:
an array's element is rewritten first, so `u64[2][3]` -> `[[u64; 2]; 3]`.

    collect <cryo> <trace.tsv> <dir>...   check every .cryo under the dirs
                                          (legacy/ excluded) with the traced
                                          compiler; keep each array once.  A
                                          `build:<dir>` argument builds that
                                          project instead, for files `check`
                                          cannot parse alone (a project with
                                          dependencies)
    apply <trace.tsv> dynamic|fixed [prefix]
                                          rewrite the arrays of that kind, in
                                          files whose path starts with prefix
    count <trace.tsv>                     arrays by kind and area

Every position is checked against the text before anything is written: the
`[` and `]` must be there, and the element text must be non-empty.  A file
with any position that does not check is left untouched and reported.

Delete in the commit after the one that runs it.
"""
import os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TAG = "@@ARRSPAN@@"


def cryo_files(dirs):
    out = []
    for d in dirs:
        for base, subdirs, files in os.walk(os.path.join(ROOT, d)):
            rel = os.path.relpath(base, ROOT).replace("\\", "/")
            subdirs[:] = [s for s in subdirs
                          if s not in ("build", ".bin", "legacy") and not s.startswith(".")]
            if rel.startswith("legacy"):
                continue
            for f in files:
                if f.endswith(".cryo"):
                    out.append(os.path.join(base, f))
    return sorted(out)


def collect(cryo, trace, dirs):
    cryo = os.path.abspath(cryo)
    env = dict(os.environ, CRYO_ARRAY_TRACE="1",
               CRYO_STDLIB=os.path.join(ROOT, "stdlib"))
    files = cryo_files([d for d in dirs if not d.startswith("build:")])
    projects = [os.path.join(ROOT, d[len("build:"):]) for d in dirs if d.startswith("build:")]
    out_root = os.path.join(os.path.dirname(os.path.abspath(trace)), "codemod-builds")
    seen = {}
    malformed = []

    def run(path):
        if os.path.isdir(path):
            d = path
            bdir = os.path.join(out_root, os.path.relpath(path, ROOT).replace(os.sep, "_"))
            cmd = [cryo, "build", "--build-dir=%s" % bdir]
        else:
            d = os.path.dirname(path)
            cmd = [cryo, "check", os.path.basename(path)]
        r = subprocess.run(cmd, cwd=d, env=env,
                           stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                           stderr=subprocess.DEVNULL)
        rows = []
        for line in r.stdout.decode("utf-8", "replace").splitlines():
            if TAG not in line:
                continue
            parts = line[line.index(TAG):].split("\t")
            if (len(parts) != 10 or not all(x.isdigit() for x in parts[2:8])
                    or parts[8] not in ("D", "F") or parts[9] not in ("0", "1")):
                malformed.append("%s: %r" % (path, line))
                continue
            src = parts[1]
            if not os.path.isabs(src):
                src = os.path.join(d, src)
            rows.append([os.path.normcase(os.path.abspath(src))] + parts[2:])
        return path, rows

    with ThreadPoolExecutor(6) as ex:
        for path, rows in ex.map(run, projects + files):
            for row in rows:
                seen[tuple(row)] = True
    kept = 0
    with open(trace, "w", encoding="utf-8") as fh:
        for row in sorted(seen):
            rel = os.path.relpath(row[0], ROOT).replace("\\", "/")
            if rel.startswith("..") or rel.startswith("legacy/"):
                continue
            fh.write("\t".join([rel] + list(row[1:])) + "\n")
            kept += 1
    for m in malformed:
        print("collect: MALFORMED " + m)
    print("collect: %d file(s) checked, %d array suffix(es) kept, %d malformed line(s)"
          % (len(files), kept, len(malformed)))


def read_trace(trace):
    by_file = {}
    with open(trace, encoding="utf-8") as fh:
        for line in fh:
            p = line.rstrip("\n").split("\t")
            if len(p) != 9:
                raise SystemExit("malformed trace line: %r" % line)
            rel, sl, sc, ql, qc, el, ec, kind, tup = p
            by_file.setdefault(rel, []).append(
                (int(sl), int(sc), int(ql), int(qc), int(el), int(ec), kind, tup == "1"))
    return by_file


def line_starts(data):
    starts = [0]
    for i, b in enumerate(data):
        if b == 0x0A:
            starts.append(i + 1)
    return starts


def matching_parens(text):
    """True when text is `( ... )` and the first `(` closes at the end."""
    if not (text.startswith("(") and text.endswith(")")):
        return False
    depth = 0
    for i, ch in enumerate(text):
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                return i == len(text) - 1
    return False


def plan_file(path, entries):
    """[(start, end, kind, tuple, sq)] as byte offsets, or an error string."""
    with open(path, "rb") as fh:
        data = fh.read()
    starts = line_starts(data)

    def off(line, col):
        if line < 1 or line > len(starts):
            return None
        return starts[line - 1] + col - 1

    spans = []
    for sl, sc, ql, qc, el, ec, kind, tup in entries:
        s, q, e = off(sl, sc), off(ql, qc), off(el, ec)
        if s is None or q is None or e is None:
            return data, None, "position past the end of the file"
        if data[q:q + 1] != b"[" or data[e - 1:e] != b"]" or not s < q < e:
            return data, None, "no `[ ... ]` at %d:%d-%d:%d" % (ql, qc, el, ec)
        if not data[s:q].strip():
            return data, None, "empty element at %d:%d" % (sl, sc)
        spans.append((s, e, kind, tup, q))
    return data, sorted(set(spans)), None


def rewrite(data, lo, hi, spans, kind):
    """The bytes data[lo:hi] with every array of `kind` inside rewritten."""
    out = []
    pos = lo
    inside = [sp for sp in spans if lo <= sp[0] and sp[1] <= hi and sp[2] == kind]
    # Outermost first: an array that another array of this kind contains is
    # rewritten as part of the containing one's element.
    for sp in sorted(inside, key=lambda t: (t[0], -t[1])):
        s, e, k, tup, q = sp
        if s < pos:
            continue
        out.append(data[pos:s])
        out.append(render(data, sp, spans, kind))
        pos = e
    out.append(data[pos:hi])
    return b"".join(out)


def render(data, sp, spans, kind):
    s, e, k, tup, q = sp
    inner = [t for t in spans if t != sp]
    elem = rewrite(data, s, q, inner, kind).rstrip()
    text = elem.decode("utf-8")
    if not tup and matching_parens(text):
        text = text[1:-1].strip()
    if k == "D":
        return ("[" + text + "]").encode("utf-8")
    size = rewrite(data, q + 1, e - 1, inner, kind).decode("utf-8").strip()
    return ("[" + text + "; " + size + "]").encode("utf-8")


def apply(trace, which, prefix):
    kind = "D" if which == "dynamic" else "F"
    by_file = read_trace(trace)
    changed, rewritten, refused = 0, 0, []
    for rel, entries in sorted(by_file.items()):
        if not rel.startswith(prefix):
            continue
        path = os.path.join(ROOT, rel)
        data, spans, err = plan_file(path, entries)
        if err:
            refused.append("%s: %s" % (rel, err))
            continue
        n = len([sp for sp in spans if sp[2] == kind])
        if n == 0:
            continue
        new = rewrite(data, 0, len(data), spans, kind)
        if new != data:
            with open(path, "wb") as fh:
                fh.write(new)
            changed += 1
            rewritten += n
    for r in refused:
        print("apply: REFUSED " + r)
    print("apply: %d %s array type(s) rewritten in %d file(s); %d file(s) refused"
          % (rewritten, which, changed, len(refused)))
    return 1 if refused else 0


def count(trace):
    by_file = read_trace(trace)
    tally = {}
    for rel, entries in by_file.items():
        top = rel.split("/")[0]
        if top == "tools":
            top = "/".join(rel.split("/")[:2])
        for e in entries:
            key = (top, e[6])
            tally[key] = tally.get(key, 0) + 1
    total = {"D": 0, "F": 0}
    for (area, kind), n in sorted(tally.items()):
        print("count: %-16s %s %5d" % (area, "dynamic" if kind == "D" else "fixed  ", n))
        total[kind] += n
    print("count: total dynamic %d, fixed %d" % (total["D"], total["F"]))


def main():
    a = sys.argv[1:]
    if a and a[0] == "collect" and len(a) >= 4:
        collect(a[1], a[2], a[3:])
        return 0
    if a and a[0] == "apply" and len(a) in (3, 4) and a[2] in ("dynamic", "fixed"):
        return apply(a[1], a[2], a[3] if len(a) == 4 else "")
    if a and a[0] == "count" and len(a) == 2:
        count(a[1])
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
