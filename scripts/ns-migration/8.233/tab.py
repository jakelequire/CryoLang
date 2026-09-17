#!/usr/bin/env python3
"""Tabulate the trait-impl registry shadow corpus (section 8.233).

Reads `.objcmp/<tag>-lines.txt` as `corpus2.sh` writes it (each line prefixed
with the half's label) and prints, per instrument:

  SEL   one selection, old beside new:  AGREE / DISAGREE / OLDONLY / NEWONLY
  LIST  a block list, old beside new:   AGREE / SAMETRAITS (split into
        old-subset-of-new and same-traits-different-blocks) / DIFFER
  METH  a method answer, old beside new
  FILT  the monomorphizer's head filter: KEEP / DROPHEAD
  LMT   lookup_method_through_trait_impls: valid / invalid answers

then every non-agreeing line's shape (site, subject, trait, old, new) with
specialization names folded to SPEC, so the entry can name each one.

usage: python scripts/ns-migration/8.233/tab.py .objcmp/w1-lines.txt
"""
import collections
import re
import sys

SPEC = re.compile(r"@[^;\t]*\$G\$G")
SPECNAME = re.compile(r"[A-Za-z0-9_:.]*\$[^;\t ]*\$G\$G")


def fold(s):
    s = SPEC.sub("@SPEC", s)
    return SPECNAME.sub("SPEC", s)


def main(path):
    kinds = collections.Counter()
    shapes = collections.Counter()
    subset = collections.Counter()
    filt = collections.Counter()
    total = 0
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            parts = line.rstrip("\n").split("\t")
            if len(parts) < 4 or parts[1] != "SHADOW":
                continue
            total += 1
            kind = parts[2]
            if kind == "LMT":
                kinds[(kind, parts[3])] += 1
                if parts[3] == "valid":
                    shapes[("LMT", fold("\t".join(parts[4:])))] += 1
                continue
            if kind == "FILT":
                kinds[(kind, parts[3])] += 1
                if parts[3] == "DROPHEAD":
                    filt[fold("\t".join(parts[4:]))] += 1
                continue
            site, verdict = parts[3], parts[4]
            kinds[(kind, site, verdict)] += 1
            if verdict == "AGREE":
                continue
            if kind == "LIST":
                only_old, only_new = parts[8], parts[9]
                if verdict == "SAMETRAITS":
                    subset[(site, "old-subset-of-new" if only_old == "-" else "same-traits-different-blocks")] += 1
                    if only_old == "-":
                        continue
                shapes[(kind, site, verdict, fold(parts[7]), fold(only_old), fold(only_new))] += 1
            elif kind == "SEL":
                shapes[(kind, site, verdict, parts[6], parts[7], fold(parts[8]), fold(parts[9]))] += 1
            elif kind == "METH":
                shapes[(kind, site, verdict, parts[6], fold(parts[7]), fold(parts[8]), fold(parts[9]), fold(parts[10]))] += 1
    print(f"SHADOW lines: {total}")
    print("\n== per instrument / site / verdict")
    for k, n in sorted(kinds.items(), key=lambda kv: (-kv[1], kv[0])):
        print(f"{n:>9}  {'  '.join(k)}")
    print("\n== SAMETRAITS split")
    for k, n in sorted(subset.items(), key=lambda kv: (-kv[1], kv[0])):
        print(f"{n:>9}  {'  '.join(k)}")
    print("\n== DROPHEAD (head's written args, subject args)")
    for k, n in sorted(filt.items(), key=lambda kv: (-kv[1], kv[0])):
        print(f"{n:>9}  {k}")
    print("\n== non-agreeing shapes")
    for k, n in sorted(shapes.items(), key=lambda kv: (-kv[1], kv[0])):
        print(f"{n:>9}  {'  '.join(k)}")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else ".objcmp/w1-lines.txt")
