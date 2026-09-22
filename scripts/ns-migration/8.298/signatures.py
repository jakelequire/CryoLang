#!/usr/bin/env python3
"""Attack W over the function-signature registry, where a family key holds a
SET by design (the overload set): the questions that survive are asked of
the SYMBOL, the one identity a definition has.

  symbol from two spans  - one linker symbol registered for two different
                           declarations (a mangling collision);
  bare key, two symbols  - a family key with no `::` (a monomorphized clone's
                           identifier, the `CalleePin::Family` key) holding
                           two symbols: two clones from different templates
                           under one program-unique-by-assumption name;
  aliases by shape       - a symbol under two family keys: the known pair
                           (the clone's bare identifier and its module-
                           qualified form, written by type_resolution for a
                           clone that is not a source declaration), and any
                           other shape, listed.

    python scripts/ns-migration/8.298/signatures.py <write_side --dump output>
"""
import collections
import io
import sys


def main():
    by_sym = collections.defaultdict(lambda: collections.defaultdict(set))   # program -> symbol -> spans
    by_key = collections.defaultdict(lambda: collections.defaultdict(set))   # program -> key -> symbols
    keys_of = collections.defaultdict(lambda: collections.defaultdict(set))  # program -> symbol -> keys
    n = 0
    for line in io.open(sys.argv[1], encoding="utf-8", errors="replace"):
        parts = line.rstrip("\n").split("\t")
        if len(parts) < 4 or parts[1] != "register_signature":
            continue
        program, key, ident = parts[0], parts[2], parts[3]
        sym, _, span = ident.partition("@")
        n += 1
        by_sym[program][sym].add(span)
        by_key[program][key].add(sym)
        keys_of[program][sym].add(key)
    two_spans = [(p, s, sorted(v)) for p, d in by_sym.items() for s, v in d.items() if len(v) > 1]
    bare_two = [(p, k, sorted(v)) for p, d in by_key.items() for k, v in d.items()
                if "::" not in k.strip("k`") and len(v) > 1]
    pair = other = 0
    other_ex = []
    for p, d in keys_of.items():
        for s, ks in d.items():
            if len(ks) < 2:
                continue
            plain = sorted(k.strip("k`") for k in ks)
            if len(plain) == 2 and plain[1].endswith("::" + plain[0]):
                pair += 1
            else:
                other += 1
                other_ex.append((p, s, plain[:4]))
    print("register_signature asks: %d over %d programs" % (n, len(by_key)))
    print("a symbol registered from two spans (a mangling collision): %d" % len(two_spans))
    for p, s, v in two_spans[:20]:
        print("   ", p, s, v)
    print("a bare family key (a clone's identifier) holding two symbols: %d" % len(bare_two))
    for p, k, v in bare_two[:20]:
        print("   ", p, k, v)
    print("aliases: a symbol under two keys - the clone pair (bare + module-qualified): %d; any other shape: %d" % (pair, other))
    for p, s, v in other_ex[:20]:
        print("   ", p, s, v)
    return 0


if __name__ == "__main__":
    sys.exit(main())
