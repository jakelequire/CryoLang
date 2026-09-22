#!/usr/bin/env python3
"""Two instruments over one tree: the review board's `inline_loops.py` (a
`for` header within six lines above a `[i].name.equals(` / `.name.id ==`
compare) and lane-gate's rule 1c (`inline_scans`).  Every board hit must be
a rule 1c scan line - a board hit rule 1c cannot see is a hole in rule 1c -
and every rule 1c line the board does not see is named with the shape that
hides it, so the difference between the two counts is a list of shapes
rather than a number.

    python scripts/ns-migration/8.296/crosscheck.py [<compiler/src>]

Exit 1 when a board hit is not a rule 1c scan.
"""
import collections
import importlib.util
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
GATE = os.path.join(ROOT, "scripts", "lane-gate.py")

# The board's pattern, as inline_loops.py has it.
PAT_CMP = re.compile(r"\[\w+\]\.(name|variant_name|method_name|field_name|param_name)(\.id)?\s*(\.equals\(|==)")
PAT_FOR = re.compile(r"^\s*for\s*\(")
# A key field spelled on the compare line, any name rule 1c's placement
# table carries.
KEYS = r"(?:name|key|triple|kind|message|severity|code|type_parameter|target_key|trait_id|namespace_name|binding_source|err_msg|directive_kind|module_name|async_fut_assoc|qualified_trait_name)"


def board_hits(tree):
    hits = set()
    for rel in tree.rels:
        lines = tree.files[rel]
        for i, line in enumerate(lines):
            if PAT_CMP.search(line) and not line.strip().startswith("//"):
                j = i
                while j >= 0 and i - j <= 6:
                    if PAT_FOR.match(lines[j]):
                        hits.add((rel, i + 1))
                        break
                    j -= 1
    return hits


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "compiler", "src")
    spec = importlib.util.spec_from_file_location("lane_gate", GATE)
    gate = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gate)
    tree = gate.Tree(src)
    mine = {}
    for rel, ln, owner, field, _elem, chain, key in gate.inline_scans(tree):
        mine.setdefault((rel, ln), []).append((owner, field, chain, key))
    board = board_hits(tree)
    print("board hits %d; rule 1c scan lines %d (%d scans)"
          % (len(board), len(mine), sum(len(v) for v in mine.values())))
    missing = sorted(board - set(mine))
    if missing:
        print("board hits that are NOT a rule 1c scan (a hole in rule 1c): %d" % len(missing))
        for rel, ln in missing:
            print("  %s:%d  %s" % (rel, ln, tree.files[rel][ln - 1].strip()[:100]))
    else:
        print("board hits that are not a rule 1c scan: 0")
    only_mine = sorted(set(mine) - board)
    why = collections.Counter()
    for rel, ln in only_mine:
        code = gate.STRING_RE.sub('""', gate.strip_comment(tree.files[rel][ln - 1]))
        if re.search(r"\[\w+\]\.(?:\w+\.)*" + KEYS, code) is None:
            why["through a local bound to the element (no `[i].key` on the compare line)"] += 1
        elif ".eq(" in code:
            why["`.eq(` rather than `.equals(`"] += 1
        elif re.search(r"\[\w+\]\." + KEYS, code) and not re.search(r"\[\w+\]\.name", code):
            why["a key field not spelled `name` (key, triple, kind, target_key, ...)"] += 1
        elif re.search(r"\.equals\s*\(\s*\w+\[\w+\]", code) or re.search(r"==\s*\w+(\.\w+)*\[\w+\]", code):
            why["the element on the RIGHT of the compare"] += 1
        else:
            why["no `for` header within six lines above, or another shape"] += 1
    print("rule 1c scan lines the board does not see: %d" % len(only_mine))
    for k, n in why.most_common():
        print("  %4d  %s" % (n, k))
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
