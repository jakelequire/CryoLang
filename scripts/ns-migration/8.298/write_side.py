#!/usr/bin/env python3
"""Attack W's analysis: over a corpus run's `SHADOW<TAB>W<TAB><door><TAB><key>
<TAB><identity>` lines (one line per name-keyed registration, the identity
independent of the key: an arena id `t..`, a node address `n..`, a mangled
symbol at its span `s..@file:line`), per program (a `corpus2.sh` half's
label is the first column), per door:

  collisions - one KEY given two different identities in one program: a
               later writer replaced or was refused by an earlier one, and
               no reader can tell which declaration it holds;
  aliases    - one IDENTITY registered under two keys in one program: the
               shape `method_returns` had (§8.246), a second spelling of one
               declaration that a reader keyed by the first never matches.

A re-registration of the SAME identity under the SAME key (a fixpoint
re-walk, a second module asking the arena for a type it already made) is
neither and is counted as `repeats`.  Node addresses are per process, so a
program is one process: the corpus label plus the file for the negatives.

    python scripts/ns-migration/8.298/write_side.py .objcmp/<tag>-lines.txt [--show DOOR]
"""
import collections
import io
import sys


def spell(table, text):
    """`k123`, `k123/456` and `s123@file:line` with each symbol id replaced by
    its text when the program's intern dump has it."""
    def one(tok):
        if tok[:1] in "ks" and tok[1:].isdigit():
            return tok[0] + "`" + table.get(tok[1:], tok[1:]) + "`"
        return tok
    head, sep, tail = text.partition("@")
    head = "/".join(one(t) for t in head.split("/"))
    return head + sep + tail


def main():
    path = sys.argv[1]
    show = sys.argv[sys.argv.index("--show") + 1] if "--show" in sys.argv else None
    # {(program, door): {key: set(identity)}}, {(program, door): {identity: set(key)}}
    by_key = collections.defaultdict(lambda: collections.defaultdict(set))
    by_id = collections.defaultdict(lambda: collections.defaultdict(set))
    asks = collections.Counter()
    repeats = collections.Counter()
    seen = set()
    # {program: {id: text}} from the `SHADOW<TAB>I<TAB><id><TAB><text>` lines
    # the intern table prints once per new symbol, so a key `k<id>` can be
    # spelled per program.
    names = collections.defaultdict(dict)
    # One corpus label can hold several PROCESSES (`make test` builds the
    # unit binary and runs every project as a child); node addresses and
    # intern ids are per process, so a process is a program.  The intern
    # dump's first interned symbol (id 1; id 0 is the pre-seeded empty symbol) marks each process's start.
    procs = collections.Counter()
    for line in io.open(path, encoding="utf-8", errors="replace"):
        parts = line.rstrip("\n").split("\t")
        if len(parts) >= 5 and parts[1] == "SHADOW" and parts[2] == "I":
            if parts[3] == "1":
                procs[parts[0]] += 1
            program = "%s#%d" % (parts[0], procs[parts[0]])
            names[program][parts[3]] = parts[4]
            continue
        if len(parts) < 5 or parts[1] != "SHADOW" or parts[2] != "W":
            continue
        program = "%s#%d" % (parts[0], procs[parts[0]])
        door, key, ident = parts[3], parts[4], parts[5] if len(parts) > 5 else ""
        key = spell(names[program], key)
        ident = spell(names[program], ident)
        if "--dump" in sys.argv:
            print("%s\t%s\t%s\t%s" % (program, door, key, ident))
            continue
        asks[door] += 1
        if (program, door, key, ident) in seen:
            repeats[door] += 1
        seen.add((program, door, key, ident))
        by_key[(program, door)][key].add(ident)
        by_id[(program, door)][ident].add(key)
    doors = sorted(asks)
    print("%-30s %9s %9s %10s %9s" % ("door", "asks", "repeats", "collisions", "aliases"))
    total_c = total_a = 0
    examples = collections.defaultdict(list)
    for door in doors:
        c = a = 0
        for (program, d), keys in by_key.items():
            if d != door:
                continue
            for key, ids in keys.items():
                if len(ids) > 1:
                    c += 1
                    examples[("collision", door)].append((program, key, sorted(ids)[:4]))
        for (program, d), ids in by_id.items():
            if d != door:
                continue
            for ident, keys in ids.items():
                if len(keys) > 1:
                    a += 1
                    examples[("alias", door)].append((program, ident, sorted(keys)[:4]))
        total_c += c
        total_a += a
        print("%-30s %9d %9d %10d %9d" % (door, asks[door], repeats[door], c, a))
    print("%-30s %9d %9d %10d %9d" % ("TOTAL", sum(asks.values()), sum(repeats.values()), total_c, total_a))
    if show:
        for kind in ("collision", "alias"):
            for program, k, vs in examples[(kind, show)][:40]:
                print("  %s %s %s -> %s" % (kind, program, k, vs))
    return 0


if __name__ == "__main__":
    sys.exit(main())
