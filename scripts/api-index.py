#!/usr/bin/env python3
"""Generate docs/stdlib-api.txt -- a one-line-per-symbol index of the standard
library, for finding out whether something already exists before writing it.

WHY THIS IS GENERATED AND NOT WRITTEN BY HAND
---------------------------------------------
A hand-maintained API list rots silently: it goes stale the first time someone
adds a function and forgets, and from then on it is worse than nothing because
it is trusted. This is regenerated from the tree and from the compiled archive,
so a stale index is a CI failure rather than a wrong answer.

TWO SOURCES, DELIBERATELY
-------------------------
1. THE ARCHIVE (authoritative).  `nm` over `stdlib/.bin/libcryo.a`, decoded by
   the compiler's OWN demangler (`cryo demangle`), not by a regex here. A
   symbol in the archive provably exists -- it linked. This is the ground
   truth for free functions.

2. THE SOURCE (complete).  A declaration scan of `stdlib/**/*.cryo`, which
   sees things the archive cannot: generic templates never instantiated in the
   stdlib's own build, types, traits, and every method of a generic type.

Neither alone is enough: the archive is provably real but incomplete, the
source scan is complete but is a text scan. So they are CROSS-CHECKED, and the
disagreement is printed rather than hidden -- a free function in the source
with no symbol in the archive is expected (uninstantiated generic) but the
COUNT is reported so a sudden jump is visible.

The cross-check goes to STDOUT and never into the file. `nm` sees a different
symbol set per platform, so a count written into the index would make a
committed, byte-compared artifact depend on the host that generated it, and
`--check` could only ever pass on one OS. Only source 2 reaches the file, which
is what makes it reproducible everywhere.

The source scan tracks brace depth rather than matching indentation, and
strips comments and string literals first, so a brace inside a string or a
doc comment cannot desynchronize it.

Usage:
    python3 scripts/api-index.py                 # write docs/stdlib-api.txt
    python3 scripts/api-index.py --check         # fail if the file is stale
"""

import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STDLIB = os.path.join(ROOT, "stdlib")
ARCHIVE = os.path.join(ROOT, "stdlib", ".bin", "libcryo.a")
# Both pins live side by side, so the name has to be chosen rather than assumed:
# handing the ELF to CreateProcess raises WinError 193 ("not a valid Win32
# application"), which surfaces as a traceback rather than as the "cryo
# unavailable" degradation the archive step is written to tolerate.
CRYO = os.path.join(
    ROOT, "bin", "cryo.exe" if sys.platform.startswith("win") else "cryo"
)
OUT = os.path.join(ROOT, "docs", "stdlib-api.txt")

# ---------------------------------------------------------------- source scan

NAMESPACE_RE = re.compile(r"^\s*namespace\s+([A-Za-z_][\w:]*)\s*;")
TYPE_RE = re.compile(
    r"^\s*(?:(public|private)\s+)?type\s+(struct|enum|class|union)\s+"
    r"([A-Za-z_]\w*)\s*(<[^{;]*>)?"
)
TRAIT_RE = re.compile(r"^\s*(?:(public|private)\s+)?trait\s+([A-Za-z_]\w*)\s*(<[^{;]*>)?")
IMPL_RE = re.compile(
    r"^\s*implement\s*(?:<[^>]*>)?\s*trait\s+([A-Za-z_][\w:<>, ]*?)\s+for\s+([^{]+?)\s*\{?\s*$"
)
# `implement<A> trait Eq for struct String<A>` -- the target carries the type
# keyword, which is noise in an index keyed by type name.
IMPL_TARGET_KW_RE = re.compile(r"^(?:struct|enum|class|union)\s+")
FUNC_RE = re.compile(
    r"^\s*(?:(public|private)\s+)?function\s+([A-Za-z_]\w*)\s*(<[^(]*>)?\s*\(([^)]*)\)"
    r"\s*(?:->\s*([^{;]+?))?\s*[{;]"
)
CONST_RE = re.compile(r"^\s*(?:(public|private)\s+)?const\s+([A-Z_][A-Z0-9_]*)\s*:\s*([^=;]+)")
# A method inside a type body: optional `static`, a name, args, optional return.
METHOD_RE = re.compile(
    r"^\s*(?:(public|private)\s+)?(static\s+)?([a-z_]\w*)\s*(<[^(]*>)?\s*\(([^)]*)\)"
    r"\s*(?:->\s*([^{;]+?))?\s*\{"
)

KEYWORDS = {"if", "while", "for", "switch", "match", "return", "else", "catch"}


def strip_noise(line):
    """Remove line comments and string/char literal contents so brace counting
    cannot be thrown off by a `{` inside text."""
    out = []
    i = 0
    n = len(line)
    while i < n:
        c = line[i]
        if c == "/" and i + 1 < n and line[i + 1] == "/":
            break
        if c in ('"', "'"):
            quote = c
            out.append(" ")
            i += 1
            while i < n:
                if line[i] == "\\":
                    i += 2
                    continue
                if line[i] == quote:
                    break
                i += 1
            i += 1
            continue
        out.append(c)
        i += 1
    return "".join(out)


def clean_sig(s):
    return re.sub(r"\s+", " ", s.strip()) if s else ""


def scan_file(path):
    """Return (namespace, [entries]) for one .cryo file."""
    ns = None
    entries = []
    depth = 0
    type_stack = []          # (name, depth_at_open)
    in_block_comment = False

    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        lines = fh.readlines()

    for raw in lines:
        line = raw.rstrip("\n")

        # block comments
        if in_block_comment:
            if "*/" in line:
                line = line.split("*/", 1)[1]
                in_block_comment = False
            else:
                continue
        if "/*" in line and "*/" not in line:
            line = line.split("/*", 1)[0]
            in_block_comment = True

        code = strip_noise(line)
        if not code.strip():
            continue

        if ns is None:
            m = NAMESPACE_RE.match(code)
            if m:
                ns = m.group(1)
                continue

        opened_type = False

        # -- declarations that open a body -------------------------------
        m = TYPE_RE.match(code)
        if m:
            vis, kind, name, generics = m.group(1), m.group(2), m.group(3), m.group(4) or ""
            entries.append(("type", f"{kind} {name}{clean_sig(generics)}", vis == "private"))
            if "{" in code:
                type_stack.append((name, depth))
                opened_type = True

        if not opened_type:
            m = TRAIT_RE.match(code)
            if m:
                vis, name, generics = m.group(1), m.group(2), m.group(3) or ""
                entries.append(("type", f"trait {name}{clean_sig(generics)}", vis == "private"))
                if "{" in code:
                    type_stack.append((name, depth))
                    opened_type = True

        if not opened_type:
            m = IMPL_RE.match(code)
            if m:
                trait = clean_sig(m.group(1))
                target = IMPL_TARGET_KW_RE.sub("", clean_sig(m.group(2)))
                entries.append(("impl", f"{target}: implements {trait}", False))
                if "{" in code:
                    # Body opens here; suppress method capture inside it -- an
                    # impl's methods belong to the trait, which lists them.
                    type_stack.append((None, depth))
                opened_type = True

        if not opened_type:
            m = FUNC_RE.match(code)
            if m and m.group(2) not in KEYWORDS:
                vis, name, generics, args, ret = m.groups()
                sig = f"{name}{clean_sig(generics)}({clean_sig(args)})"
                if ret:
                    sig += f" -> {clean_sig(ret)}"
                entries.append(("fn", sig, vis == "private"))

            elif type_stack and type_stack[-1][0] is not None:
                m = METHOD_RE.match(code)
                if m and m.group(3) not in KEYWORDS:
                    vis, static, name, generics, args, ret = m.groups()
                    owner = type_stack[-1][0]
                    joiner = "::" if static else "."
                    sig = f"{owner}{joiner}{name}{clean_sig(generics)}({clean_sig(args)})"
                    if ret:
                        sig += f" -> {clean_sig(ret)}"
                    entries.append(("method", sig, vis == "private"))

            m = CONST_RE.match(code)
            if m:
                vis, name, ty = m.groups()
                entries.append(("const", f"{name}: {clean_sig(ty)}", vis == "private"))

        # -- brace tracking ----------------------------------------------
        depth += code.count("{") - code.count("}")
        while type_stack and depth <= type_stack[-1][1]:
            type_stack.pop()

    return ns, entries


def scan_stdlib():
    by_ns = {}
    for dirpath, dirnames, filenames in os.walk(STDLIB):
        dirnames[:] = [d for d in dirnames if not d.startswith(".")]
        for fn in sorted(filenames):
            if not fn.endswith(".cryo"):
                continue
            path = os.path.join(dirpath, fn)
            ns, entries = scan_file(path)
            if ns is None:
                continue
            # The index is a committed artifact compared byte-for-byte, so the
            # host's path separator must not reach it: `os.sep` would emit
            # `stdlib\alloc\arc.cryo` on Windows against a golden written with
            # forward slashes, and every namespace header would read as stale.
            rel = os.path.relpath(path, ROOT).replace(os.sep, "/")
            by_ns.setdefault(ns, {"file": rel, "entries": []})["entries"].extend(entries)
    return by_ns


# ------------------------------------------------------------- archive symbols

def archive_symbols():
    """Free-function signatures that provably linked, via nm + cryo demangle.
    Returns (set_of_names, note) -- note explains any degradation."""
    if not os.path.exists(ARCHIVE):
        return set(), "archive not built (run `make stdlib`); source scan only"
    if not os.path.exists(CRYO):
        return set(), "bin/cryo not present; source scan only"
    try:
        nm = subprocess.run(
            ["nm", "--defined-only", ARCHIVE],
            capture_output=True, text=True, check=True,
        )
    except (subprocess.CalledProcessError, FileNotFoundError) as exc:
        return set(), f"nm unavailable ({exc}); source scan only"

    mangled = [
        parts[2]
        for parts in (ln.split() for ln in nm.stdout.splitlines())
        if len(parts) == 3 and parts[1] == "T" and parts[2].startswith("C$")
    ]
    if not mangled:
        return set(), "no Cryo text symbols in archive"

    dem = subprocess.run(
        [CRYO, "demangle"], input="\n".join(mangled),
        capture_output=True, text=True,
    )
    names = set()
    for line in dem.stdout.splitlines():
        line = line.strip()
        if not line or "(" not in line:
            continue
        head = line.split("(", 1)[0]
        if "::" in head:
            names.add(head.rsplit("::", 1)[1])
    return names, ""


# ------------------------------------------------------------------- rendering

ORDER = {"type": 0, "impl": 1, "const": 2, "fn": 3, "method": 4}


def cross_check(by_ns, linked):
    """(linked, unlinked) counts for the archive cross-check.

    Reported to stdout rather than written into the index: `nm` sees a different
    symbol set per platform -- the Windows archive yields more Cryo text symbols
    yet matches fewer source names than the Linux one -- so embedding these
    counts would make a committed, byte-compared artifact depend on the host that
    generated it.  The index body is derived from the stdlib sources alone and is
    identical everywhere, which is what lets `--check` run on any host.
    """
    src_fns = {
        e[1].split("(", 1)[0]
        for v in by_ns.values() for e in v["entries"]
        if e[0] == "fn" and not e[2]
    }
    return len(linked), len(src_fns - linked) if linked else 0


def render(by_ns):
    total = sum(len(v["entries"]) for v in by_ns.values())

    out = []
    out.append("# Cryo standard library -- API index")
    out.append("#")
    out.append("# GENERATED by scripts/api-index.py. Do not edit.")
    out.append("# Regenerate with `make api-index`.")
    out.append("#")
    out.append("# Grep this file BEFORE writing a helper. Most utility operations")
    out.append("# already exist; a near-duplicate is worse than an imperfect call.")
    out.append("#")
    out.append("#   Type::name(...)   static method / constructor")
    out.append("#   Type.name(...)    instance method")
    out.append("#   name(...)         free function")
    out.append("#   [private]         not reachable from another module")
    out.append("#")
    out.append(f"# {len(by_ns)} namespaces, {total} declarations.")
    out.append("")

    for ns in sorted(by_ns):
        info = by_ns[ns]
        seen = set()
        rows = []
        for kind, sig, private in info["entries"]:
            key = (kind, sig)
            if key in seen:
                continue
            seen.add(key)
            rows.append((ORDER.get(kind, 9), sig, private))
        if not rows:
            continue
        out.append("=" * 78)
        out.append(f"{ns}")
        out.append(f"    {info['file']}")
        out.append("=" * 78)
        for _, sig, private in sorted(rows, key=lambda r: (r[0], r[1].lower())):
            out.append(f"  {sig}{'   [private]' if private else ''}")
        out.append("")

    return "\n".join(out) + "\n"


def report_cross_check(n_linked, n_unlinked, note):
    """The archive cross-check, on stdout so it never enters the artifact."""
    if note:
        print(f"api-index: {note}")
        return
    print(f"api-index: cross-check -- {n_linked} free functions linked into "
          f"stdlib/.bin/libcryo.a; {n_unlinked} declared in source without a "
          f"symbol (expected -- generic templates the stdlib never instantiates)")


def main():
    check = "--check" in sys.argv
    by_ns = scan_stdlib()
    if not by_ns:
        print("api-index: no namespaces found under stdlib/", file=sys.stderr)
        return 1
    linked, note = archive_symbols()
    text = render(by_ns)
    n_linked, n_unlinked = cross_check(by_ns, linked)

    if check:
        if not os.path.exists(OUT):
            print(f"api-index: {os.path.relpath(OUT, ROOT)} is missing; "
                  f"run `make api-index`", file=sys.stderr)
            return 1
        with open(OUT, "r", encoding="utf-8") as fh:
            if fh.read() != text:
                print(f"api-index: {os.path.relpath(OUT, ROOT)} is STALE; "
                      f"run `make api-index` and commit the result", file=sys.stderr)
                return 1
        print(f"api-index: {os.path.relpath(OUT, ROOT)} is up to date")
        report_cross_check(n_linked, n_unlinked, note)
        return 0

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    # `newline="\n"` suppresses the translation that would write CRLF on
    # Windows, matching `roster-check.py`.  The reader above keeps universal
    # newlines on purpose, so a golden checked out through `core.autocrlf` still
    # compares equal; only what this script PRODUCES has to be pinned, or the
    # artifact differs by host.
    with open(OUT, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)
    total = sum(len(v["entries"]) for v in by_ns.values())
    print(f"api-index: wrote {os.path.relpath(OUT, ROOT)} "
          f"({len(by_ns)} namespaces, {total} declarations)")
    report_cross_check(n_linked, n_unlinked, note)
    return 0


if __name__ == "__main__":
    sys.exit(main())
