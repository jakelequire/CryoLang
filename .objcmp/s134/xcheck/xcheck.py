"""Cross-check the spelling-flow list against the compiler's own checks.

Inputs: the live tree's function index (index.json, allows and doors as
they stand), the stripped tree's index (strip-index.json), the stripped
build's log (every function-level allow of `lookup_by_spelling` removed,
built by the compiler under test), the list itself.

For each list entry, its lookup function L:
  REFUSED-WHEN-UNALLOWED  L carries an allow, and with the allow removed the
                          compiler refuses a site in L: the compiler sees the
                          lookup; the allow is the written reason it stands.
  ALLOWED-UNSEEN          L carries an allow, and with it removed the
                          compiler still refuses nothing in L.
  UNSEEN                  L carries no allow and the compiler accepts it.
Writes a TSV of every entry with its verdict and prints the summary."""
import json
import re
import sys
from collections import Counter, defaultdict

live = json.load(open(".objcmp/s134/xcheck/index.json"))
strip = json.load(open(".objcmp/s134/xcheck/strip-index.json"))
log = open(".objcmp/s134/strip/strip-u2.log", encoding="utf-8", errors="replace").read()
log = re.sub(r"\x1b\[[0-9;]*m", "", log)


def keyed(idx):
    by = {}
    for f in idx:
        by.setdefault(f["q"], []).append(f)
        if f["owner"]:
            by.setdefault(f["q"].rsplit(".", 1)[0] + "::" + f["name"], []).append(f)
    return by


live_by = keyed(live)
strip_by = keyed(strip)

# Every refusal: (file, line) of each span it names, primary first.
refusals = []
lines = log.splitlines()
for i, l in enumerate(lines):
    if not l.startswith("error[E0157]"):
        continue
    spans = []
    j = i + 1
    while j < len(lines) and not lines[j].startswith("error") and j < i + 60:
        m = re.search(r"(?:-->|^ ::) (\S+?):(\d+):\d+", lines[j])
        if m:
            spans.append((m.group(1).replace("\\", "/"), int(m.group(2))))
        j += 1
    refusals.append((l, spans))


def fn_at(idx, file, line):
    for f in idx:
        if f["file"].lower().endswith(file.replace("src/", "", 1).lower()) and f["start"] <= line <= f["end"]:
            return f
    return None


refused_fns = defaultdict(list)
unmapped = []
for msg, spans in refusals:
    for (file, line) in spans[:1]:
        f = fn_at(strip, file, line)
        if f:
            refused_fns[f["q"]].append(msg)
        else:
            unmapped.append((msg, file, line))

rows = []
for l in open("scripts/spelling-flow.outstanding.tsv", encoding="utf-8"):
    if l.startswith("#") or not l.strip():
        continue
    c = l.rstrip("\n").split("\t")
    count, rule, kind, keycls, lookup, okind, ofn, otext = c[:8]
    lf = live_by.get(lookup, [])
    allowed = any(f["allow"] for f in lf)
    door = any(f["door"] for f in lf)
    sq = strip_by.get(lookup, [])
    refused = any(f["q"] in refused_fns for f in sq)
    if allowed and refused:
        v = "REFUSED-WHEN-UNALLOWED"
    elif allowed:
        v = "ALLOWED-UNSEEN"
    elif door:
        v = "DOOR"
    else:
        v = "UNSEEN"
    reason = next((f["allow"] for f in lf if f["allow"]), "")
    rows.append([v, count, rule, kind, keycls, lookup, okind, ofn, otext, reason])

out = open(".objcmp/s134/xcheck/verdicts.tsv", "w", encoding="utf-8")
for r in rows:
    out.write("\t".join(r) + "\n")
out.close()
tot = Counter()
w = Counter()
for r in rows:
    tot[r[0]] += 1
    w[r[0]] += int(r[1])
for k in tot:
    print(k, "entries", tot[k], "weighted", w[k])
print("total entries", len(rows), "weighted", sum(int(r[1]) for r in rows))
print("refusals in the stripped build:", len(refusals), "in", len(refused_fns), "functions;", len(unmapped), "unmapped")
for u in unmapped:
    print("  UNMAPPED", u)
json.dump(sorted(refused_fns), open(".objcmp/s134/xcheck/refused-fns.json", "w"), indent=0)
removed = json.load(open(".objcmp/s134/xcheck/removed.json"))
print("allows removed:", len(removed))
