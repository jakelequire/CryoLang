"""Run a checkout's `scripts/ns-status-check.py` in-process and record every
row command it runs, with its full output and wall time, and the verdict.

    python scripts/ns-migration/8.354/nsdump.py <checkout> <out.json>

Used for §8.354: the same clone recorded with the scripts before and after
the shared parse, then compared by `compare.py`.
"""
import contextlib
import io
import json
import os
import runpy
import subprocess
import sys
import time

sys.dont_write_bytecode = True
root, out = sys.argv[1], sys.argv[2]
_real = subprocess.run
rows = []


def record(*a, **k):
    t = time.time()
    r = _real(*a, **k)
    cmd = a[0] if a else k.get("args")
    if isinstance(cmd, list) and cmd[:2] == ["bash", "-c"]:
        rows.append({"cmd": cmd[2], "out": (r.stdout or b"").decode("utf-8", "replace"),
                     "sec": round(time.time() - t, 2)})
    return r


subprocess.run = record
os.chdir(root)
sys.argv = ["ns-status-check.py"]
buf = io.StringIO()
t0 = time.time()
code = 0
with contextlib.redirect_stdout(buf):
    try:
        runpy.run_path(os.path.join(root, "scripts", "ns-status-check.py"), run_name="__main__")
    except SystemExit as e:
        code = e.code
total = time.time() - t0
with open(out, "w", encoding="utf-8") as fh:
    json.dump({"exit": code, "total": total,
               "verdict": buf.getvalue().strip().splitlines()[-1:], "rows": rows}, fh, indent=0)
print("exit", code, "total %.1fs" % total, "rows", len(rows))
