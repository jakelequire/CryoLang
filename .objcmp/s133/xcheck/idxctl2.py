"""Control: each allow line preceding an indexed function is recorded on it."""
import json
import os

idx = json.load(open(".objcmp/s133/xcheck/index.json"))
by = {(f["file"], f["start"]): f for f in idx}
bad = 0
for dp, dn, fn in os.walk("compiler/src"):
    for f in fn:
        if not f.endswith(".cryo"):
            continue
        path = os.path.join(dp, f).replace("\\", "/")
        lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
        for i, l in enumerate(lines):
            s = l.strip()
            if not ("allow(lookup_by_spelling" in s and s.startswith("![")):
                continue
            j = i + 1
            while j < len(lines) and (lines[j].strip().startswith("///") or lines[j].strip().startswith("![")):
                j += 1
            rec = by.get((path, j + 1))
            if rec and not rec["allow"]:
                bad += 1
                print("NOT RECORDED", path, j + 1, lines[j].strip()[:70])
print("unrecorded:", bad)
