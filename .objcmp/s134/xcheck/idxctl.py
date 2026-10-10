"""Control on the index: every allow and door marker in the source is
either on an indexed function or on a field; list the ones that are not."""
import json
import os
import re

idx = json.load(open(".objcmp/s134/xcheck/index.json"))
starts = {(f["file"], f["start"]) for f in idx}
missed = []
for dp, dn, fn in os.walk("compiler/src"):
    for f in fn:
        if not f.endswith(".cryo"):
            continue
        path = os.path.join(dp, f).replace("\\", "/")
        lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
        for i, l in enumerate(lines):
            s = l.strip()
            if not (("allow(lookup_by_spelling" in s and s.startswith("![")) or s.startswith("/// Door `")):
                continue
            j = i + 1
            while j < len(lines) and (lines[j].strip().startswith("///") or lines[j].strip().startswith("![")):
                j += 1
            nxt = lines[j] if j < len(lines) else ""
            if (path, j + 1) in starts:
                continue
            kind = "field" if "(" not in nxt else "FUNCTION-MISSED"
            missed.append((kind, path, j + 1, nxt.strip()[:80]))
for m in missed:
    if m[0] != "field":
        print(*m)
print("fields:", sum(1 for m in missed if m[0] == "field"), "missed functions:", sum(1 for m in missed if m[0] != "field"))
