"""Rename the spelling parameter of the async lowering's walks to a binding key.

Usage: python scripts/ns-migration/8.561/key_async_walks.py [--apply]

Each listed method of `compiler/src/compiler/sema/async_lower.cryo` takes the
local it asks about as `name: SymbolStr`.  This rewrites the parameter to
`key: SymbolID` and every bare use of `name` in the method's body to `key`
(a member read `.name` is left alone, as are comment lines).  The comparisons
themselves - `<id>.name.equals(key)` - are converted by hand afterwards: a
binding is asked of the use's `res`, which this script cannot know.

Without --apply it prints the count per method and writes nothing.
"""
import re
import sys

PATH = "compiler/src/compiler/sema/async_lower.cryo"

METHODS = [
    "name_read_in_expr", "name_read_in_stmt", "name_is_whole_place",
    "subst_name_expr", "subst_nested_stmt", "subst_stmt_list", "subst_name_stmt",
    "block_reads_name", "block_first_use", "stmt_first_use", "expr_first_use",
    "needs_handback", "borrowed_by_awaited_future", "last_use_consumes",
    "mark_last_use_stmt", "mark_last_use_expr", "mark_last_use_arm",
    "decl_at_first_assignment", "top_level_assignment_index", "block_cond_write",
    "assigned_frame_addr_root",
]

BARE = re.compile(r"(?<![.\w])name\b")


def main():
    apply = "--apply" in sys.argv
    with open(PATH, encoding="utf-8", newline="") as fh:
        lines = fh.read().split("\n")
    done = {}
    i = 0
    while i < len(lines):
        m = re.match(r"    ([a-z_0-9]+)\(", lines[i])
        if not m or m.group(1) not in METHODS:
            i += 1
            continue
        meth = m.group(1)
        if meth in done:
            sys.exit("method %s found twice" % meth)
        n = 0
        j = i
        while True:
            line = lines[j]
            if j > i and line.rstrip("\r") == "    }":
                break
            if not line.lstrip().startswith("//"):
                new = line.replace("name: SymbolStr", "key: SymbolID")
                new = BARE.sub("key", new)
                if new != line:
                    n += new.count("key") - line.count("key")
                    lines[j] = new
            j += 1
        done[meth] = n
        i = j + 1
    missing = [m for m in METHODS if m not in done]
    if missing:
        sys.exit("not found: %s" % ", ".join(missing))
    for meth in METHODS:
        print("%-28s %d" % (meth, done[meth]))
    if apply:
        with open(PATH, "w", encoding="utf-8", newline="") as fh:
            fh.write("\n".join(lines))
        print("written")


if __name__ == "__main__":
    main()
