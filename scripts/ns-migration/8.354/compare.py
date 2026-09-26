"""Compare two `nsdump.py` recordings row by row.

    python scripts/ns-migration/8.354/compare.py <before.json> <after.json>

The run's own set-up and clean-up commands (`mktemp -d`, `test -d ..`,
`rm -rf ..`) are not section 0 rows and are left out of the `after` side.
"""
import json
import sys

OWN = ("mktemp -d", 'test -d "$CRYO_PARSE_CACHE" && echo visible', 'rm -rf -- "$1"')


def rows(path):
    with open(path, encoding="utf-8") as fh:
        rec = json.load(fh)
    return rec, [r for r in rec["rows"] if r["cmd"] not in OWN]


old, orows = rows(sys.argv[1])
new, nrows = rows(sys.argv[2])
same_cmds = [r["cmd"] for r in orows] == [r["cmd"] for r in nrows]
differ = [a["cmd"] for a, b in zip(orows, nrows) if a["out"] != b["out"]]
print("rows: before %d, after %d; same commands in order: %s" % (len(orows), len(nrows), same_cmds))
print("rows whose full output differs: %d" % len(differ))
for cmd in differ:
    print("  " + cmd[:120])
print("exit: before %s, after %s; total: before %.1fs, after %.1fs"
      % (old["exit"], new["exit"], old["total"], new["total"]))
sys.exit(0 if same_cmds and not differ and old["exit"] == new["exit"] else 1)
