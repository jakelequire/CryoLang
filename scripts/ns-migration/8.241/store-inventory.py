"""The §8.241 inventory: every method under lane-gate's key rule on every
type the CompilationContext carries, with the external call sites grouped
by receiver SPELLING (this script predates the typed placer, so its site
counts are by name and over-count where another type has a same-named
method - the table in the entry says which).  `--sites` lists each site.

    python scripts/ns-migration/8.241/store-inventory.py [--sites]
"""
import os
import re
import sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
SRC = os.path.join(ROOT, "compiler", "src")

KEY_TYPES = ("SymbolStr", "string")
STORES = [
    ("compiler/decl_index.cryo", "DeclarationIndex"),
    ("compiler/sema/type_utils.cryo", "TypeUtils"),
    ("compiler/types/generic_registry.cryo", "GenericRegistry"),
    ("compiler/types/arena.cryo", "TypeArena"),
    ("compiler/module_graph.cryo", "ModuleGraph"),
    ("compiler/const_table.cryo", "ConstantTable"),
    ("compiler/resolver/intern_table.cryo", "InternTable"),
    ("compiler/resolver/resolver.cryo", "Resolver"),
    ("compiler/types/resolver.cryo", "TypeResolver"),
    ("compiler/mono/monomorphizer.cryo", "Monomorphizer"),
    ("compiler/mono/state.cryo", "MonoState"),
    ("compiler/types/checker.cryo", "TypeChecker"),
    ("compiler/module_loader.cryo", "ModuleLoader"),
    ("compiler/artifacts.cryo", "PhaseArtifacts"),
]

STRING_RE = re.compile(r'"(?:[^"\\]|\\.)*"')
METHOD_HEAD_RE = re.compile(r"^    (static\s+)?([a-z_][a-z_0-9]*)\s*(?:<[^>]*>)?\s*\(")
KEY_RE = re.compile(r"\b(?:%s)\b" % "|".join(KEY_TYPES))


def strip_comment(line):
    cut = line.find("//")
    return line if cut < 0 else line[:cut]


def all_files():
    for dirpath, _d, files in os.walk(SRC):
        for f in sorted(files):
            if f.endswith(".cryo"):
                full = os.path.join(dirpath, f)
                yield os.path.relpath(full, SRC).replace(os.sep, "/"), full


def block_methods(lines, start, rel):
    """Methods declared inside the block whose head line is `start`."""
    found = {}
    depth = 0
    entered = False
    i = start
    while i < len(lines):
        code = STRING_RE.sub('""', strip_comment(lines[i]))
        m = METHOD_HEAD_RE.match(code)
        if m and depth == 1:
            head = code
            j = i
            while "{" not in head or head.count("(") > head.count(")"):
                j += 1
                head += " " + STRING_RE.sub('""', strip_comment(lines[j])).strip()
            head = head[:head.index("{")]
            keyed = sorted(set(KEY_RE.findall(head)))
            if keyed:
                if m.group(1):
                    kind = "static"
                elif "mut &this" in head:
                    kind = "write"
                else:
                    kind = "read"
                found[m.group(2)] = (kind, ",".join(keyed), rel, i + 1, head.strip())
        for ch in code:
            if ch == "{":
                depth += 1
                entered = True
            elif ch == "}":
                depth -= 1
        if entered and depth == 0:
            break
        i += 1
    return found


def store_methods(type_name):
    head_re = re.compile(r"^type struct %s\b" % re.escape(type_name))
    impl_re = re.compile(r"^implement(?:\s*<[^>]*>)?\s+(?:struct\s+)?%s\b(?!\s+for\b)" % re.escape(type_name))
    found = {}
    blocks = []
    for rel, full in all_files():
        with open(full, "r", encoding="utf-8", errors="replace") as fh:
            lines = fh.read().split("\n")
        for i, line in enumerate(lines):
            if head_re.match(line) or impl_re.match(line):
                blocks.append((rel, i + 1))
                found.update(block_methods(lines, i, rel))
    return found, blocks


def main():
    for rel_def, type_name in STORES:
        methods, blocks = store_methods(type_name)
        print("=" * 78)
        print("%s  (%s)  blocks: %s" % (type_name, rel_def, blocks))
        print("  %d name-keyed methods:" % len(methods))
        for n in sorted(methods):
            kind, keyed, r, ln, head = methods[n]
            print("    %-6s %-10s %s:%d  %s" % (kind, keyed, r, ln, head[:110]))
        if not methods:
            continue
        names = sorted(methods, key=len, reverse=True)
        alt = "|".join(names)
        dotted_re = re.compile(r"([A-Za-z_][A-Za-z_0-9.]*)\.(%s)\s*\(" % alt)
        static_re = re.compile(r"([A-Za-z_][A-Za-z_0-9]*)::(%s)\s*\(" % alt)
        by_recv = defaultdict(int)
        by_method = defaultdict(int)
        by_file = defaultdict(int)
        sites = []
        for rel, full in all_files():
            if rel == rel_def:
                continue
            with open(full, "r", encoding="utf-8", errors="replace") as fh:
                for lineno, raw in enumerate(fh, 1):
                    line = strip_comment(raw)
                    for m in dotted_re.finditer(line):
                        recv, name = m.group(1), m.group(2)
                        by_recv[recv] += 1
                        sites.append((rel, lineno, recv, name))
                    for m in static_re.finditer(line):
                        by_recv[m.group(1) + "::"] += 1
                        sites.append((rel, lineno, m.group(1) + "::", m.group(2)))
        print("  receivers seen (%d sites):" % len(sites))
        for r, c in sorted(by_recv.items(), key=lambda kv: -kv[1]):
            print("    %4d  %s" % (c, r))
        if len(sys.argv) > 1 and sys.argv[1] == "--sites":
            for s in sites:
                print("    %s:%d  %s.%s(" % s)


main()
