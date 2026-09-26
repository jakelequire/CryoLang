"""Seal SymbolID: private number and constructor, an allocator as the one
mint, a read-only key(); rewrite each refused `.id` read (from the build
log's E0353 locations) to `.key()`."""
import collections
import io
import os
import re
import sys

ROOT = r"C:\Programming\apps\CryoLang"
LOG = os.path.join(ROOT, "scripts", "ns-migration", "8.363", "refused-symbolid.lst")


def load(p):
    s = io.open(p, encoding="utf-8", newline="").read()
    return s, ("\r\n" if "\r\n" in s else "\n")


# 1. the type
p = os.path.join(ROOT, r"compiler\src\compiler\resolver\symbol_id.cryo")
s, nl = load(p)
a = """type struct SymbolID {
private:
    id: u64;

    /// Create a new symbol ID with the given value.
    static new(id: u64) -> SymbolID {
        return SymbolID { id: id };
    }

public:
    /// Create an invalid/unresolved symbol ID (id = 0).
    static invalid() -> SymbolID {
        return SymbolID { id: 0 };
    }
""".replace("\n", nl)
b = """type struct SymbolID {
private:
    id: u64;

    static new(id: u64) -> SymbolID {
        return SymbolID { id: id };
    }

public:
    /// Create an invalid/unresolved symbol ID (id = 0).
    static invalid() -> SymbolID {
        return SymbolID { id: 0 };
    }

    /// The number a pass keys its own table of bindings by, and the
    /// resolver its arena.  A read, one way: nothing outside this module
    /// turns a number back into an id, so a table keyed by it holds only
    /// ids the resolver handed out.
    key(&this) -> u64 {
        return this.id;
    }
""".replace("\n", nl)
assert s.count(a) == 1
s = s.replace(a, b)
s = s.rstrip() + nl + nl + """/// The one source of `SymbolID`s: each `next` answers a fresh id, from 1
/// upward (0 is the invalid id).  The resolver holds the only one a
/// compilation uses; an id is never built from a number.
type struct SymbolIds {
private:
    next_id: u64;

public:
    static new() -> SymbolIds {
        return SymbolIds { next_id: 1 };
    }

    next(mut &this) -> SymbolID {
        const id: SymbolID = SymbolID::new(this.next_id);
        this.next_id += 1;
        return id;
    }
}
""".replace("\n", nl)
io.open(p, "w", encoding="utf-8", newline="").write(s)

# 2. rewrite `.id` reads at the refused positions
text = io.open(LOG, encoding="utf-8", errors="replace").read()
locs = collections.defaultdict(set)
for m in re.finditer(r"error\[E0353\]: field `id` of `compiler::resolver::symbol_id::SymbolID` is private\s*\n\s*--> src/(\S+?):(\d+):(\d+)", text):
    locs[m.group(1).lower()].add((int(m.group(2)), int(m.group(3))))
total = 0
for rel, spots in sorted(locs.items()):
    p = os.path.join(ROOT, "compiler", "src", *rel.split("/"))
    s, nl = load(p)
    lines = s.split(nl)
    for ln, col in sorted(spots, key=lambda t: (t[0], -t[1])):
        line = lines[ln - 1]
        i = col - 1
        if line[i:i + 2] != "id" or (i + 2 < len(line) and (line[i + 2].isalnum() or line[i + 2] == "_")):
            sys.exit("unexpected text at %s:%d:%d: %r" % (rel, ln, col, line[i:i + 12]))
        lines[ln - 1] = line[:i] + "key()" + line[i + 2:]
        total += 1
    io.open(p, "w", encoding="utf-8", newline="").write(nl.join(lines))
    print("  %-45s %d" % (rel, len(spots)))
print("rewritten reads:", total)
