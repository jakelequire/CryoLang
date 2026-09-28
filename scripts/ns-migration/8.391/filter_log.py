"""Keep only the error blocks of a check log whose location is in files
matching a substring, so `retype.py --refusals` can be re-run for one file.

usage: python filter_log.py <log> <path-substring> > out.log
"""
import sys

log, want = sys.argv[1], sys.argv[2].lower()
lines = open(log, encoding="utf-8", errors="replace").read().splitlines()
starts = [i for i, l in enumerate(lines) if l.startswith("error[")] + [len(lines)]
kept = 0
for a, b in zip(starts, starts[1:]):
    block = lines[a:b]
    loc = next((l for l in block[1:6] if l.strip().startswith("-->")), "")
    if want in loc.lower():
        print("\n".join(block))
        kept += 1
sys.stderr.write("kept %d block(s)\n" % kept)
