#!/usr/bin/env python3
"""Census of the intrinsic kinds codegen lowers against what the tree declares.

Reads `IntrinsicKind::from_name`'s rows, every `intrinsic function NAME` in the
tree, and counts each table name's word occurrences outside the two codegen
files that spell the table.  Run from the repo root:

    python scripts/ns-migration/8.324/intrinsics_census.py            # summary
    python scripts/ns-migration/8.324/intrinsics_census.py --list     # every name
"""
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
# Where the table lives: `intrinsic_kind.cryo` since section 8.325, the
# codegen module before it (for measuring the parent commit).
TABLE = ROOT / "compiler/src/compiler/intrinsic_kind.cryo"
if not TABLE.exists():
    TABLE = ROOT / "compiler/src/compiler/codegen/ops/intrinsics_codegen.cryo"
EMITTER = "compiler/src/compiler/codegen/ops/intrinsic_emitter.cryo"
SCAN = ["stdlib", "compiler/src", "tests", "tools", "examples", "runtime"]


def table_rows():
    text = TABLE.read_text(encoding="utf-8")
    body = text[text.index("static from_name"):text.index("static is_name")] \
        if "static is_name" in text else text[text.index("static from_name"):]
    rows = {}
    for m in re.finditer(r'^\s*((?:"[a-z0-9_]+"\s*\|?\s*)+)=>\s*\{\s*IntrinsicKind::(\w+)', body, re.M):
        for name in re.findall(r'"([a-z0-9_]+)"', m.group(1)):
            rows[name] = m.group(2)
    return rows


def declared():
    out = subprocess.run(
        ["git", "grep", "-nE", r"^\s*(public\s+)?intrinsic\s+function\s+[a-z0-9_]+", "--", "*.cryo"],
        cwd=ROOT, capture_output=True, text=True).stdout
    decls = {}
    for line in out.splitlines():
        path, _, rest = line.split(":", 2)
        if path.startswith("legacy/"):
            continue
        name = re.search(r"intrinsic\s+function\s+([a-z0-9_]+)", rest).group(1)
        decls.setdefault(name, []).append(path)
    return decls


def uses(name):
    out = subprocess.run(["git", "grep", "-nw", name, "--"] + SCAN,
                         cwd=ROOT, capture_output=True, text=True).stdout
    hits = []
    for line in out.splitlines():
        path = line.split(":", 1)[0]
        if path in (str(TABLE.relative_to(ROOT)).replace("\\", "/"), EMITTER):
            continue
        if re.search(r"intrinsic\s+function\s+" + name + r"\b", line):
            continue
        hits.append(line)
    return hits


def main():
    rows = table_rows()
    decls = declared()
    kinds = set(rows.values())
    undeclared = sorted(n for n in rows if n not in decls)
    kindless = sorted(n for n in decls if n not in rows)
    print(f"table names {len(rows)}, kinds {len(kinds)}")
    print(f"declared intrinsic names {len(decls)} "
          f"(in the table {len(decls) - len(kindless)}, with no kind {len(kindless)}: {', '.join(kindless)})")
    ukinds = {rows[n] for n in undeclared}
    dkinds = {rows[n] for n in rows if n in decls}
    print(f"table names declared by nothing {len(undeclared)} "
          f"({len(ukinds - dkinds)} kinds reachable by no declared name)")
    used = [n for n in undeclared if uses(n)]
    print(f"of those, names with a word occurrence outside the table: {len(used)}")
    if "--list" in sys.argv[1:]:
        for n in undeclared:
            hits = uses(n)
            print(f"  {n:20s} {rows[n]:18s} {len(hits)}")
            for h in hits[:4]:
                print(f"      {h[:150]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
