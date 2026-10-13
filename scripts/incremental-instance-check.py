#!/usr/bin/env python3
"""An incremental build must link when an edit asks an unchanged module for a
generic instance its cached object does not hold.

Runs `tests/fixtures/incremental-new-instance` (see its README) in a scratch
copy: builds `src/` incrementally and runs it (exit 3), replaces
`src/main.cryo` with `edit/main.cryo`, builds again over the first build's
cache and runs it (exit 4).  The second build must also REUSE some cached
objects, or the per-module cache was switched off and the build linked for
the wrong reason - a gate that passes with the cache disabled tests nothing.

A failed build's output is printed: the linker's report is the evidence.

Usage:
    python scripts/incremental-instance-check.py --cryo compiler/build/cryo.exe

Exit codes: 0 when both builds link, run as expected and the second reused
the cache; 1 otherwise.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIXTURE = os.path.join(ROOT, "tests", "fixtures", "incremental-new-instance")
SCRATCH = os.path.join(ROOT, ".verify", "incremental-instance")
STDLIB = os.path.join(ROOT, "stdlib").replace("\\", "/")
EXE = ".exe" if os.name == "nt" else ""
REUSED = re.compile(r"reusing (\d+) cached object")


def build(cryo, env):
    r = subprocess.run([cryo, "build", "--stdlib=" + STDLIB], cwd=SCRATCH, env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return r.returncode, r.stdout.decode("utf-8", "replace")


def run():
    exe = os.path.join(SCRATCH, "build", "incremental_new_instance" + EXE)
    if not os.path.isfile(exe):
        return None
    return subprocess.run([exe]).returncode


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cryo", required=True)
    args = ap.parse_args()
    cryo = os.path.abspath(args.cryo)
    env = dict(os.environ)
    if os.name == "nt":
        env.setdefault("CRYO_CC", "gcc")

    shutil.rmtree(SCRATCH, ignore_errors=True)
    shutil.copytree(FIXTURE, SCRATCH, ignore=shutil.ignore_patterns("build"))

    rc1, out1 = build(cryo, env)
    exit1 = run() if rc1 == 0 else None
    if rc1 != 0 or exit1 != 3:
        print(out1[-4000:])
        print("incremental-instance-check: FAIL -- the first build: build exit %d, run exit %s "
              "(want 0 and 3)" % (rc1, exit1))
        return 1

    shutil.copyfile(os.path.join(SCRATCH, "edit", "main.cryo"),
                    os.path.join(SCRATCH, "src", "main.cryo"))
    rc2, out2 = build(cryo, env)
    exit2 = run() if rc2 == 0 else None
    m = REUSED.search(out2)
    reused = int(m.group(1)) if m else 0
    if rc2 != 0 or exit2 != 4:
        print(out2[-6000:])
        print("incremental-instance-check: FAIL -- the incremental build after the edit: "
              "build exit %d, run exit %s (want 0 and 4)" % (rc2, exit2))
        return 1
    if reused == 0:
        print(out2[-2000:])
        print("incremental-instance-check: FAIL -- the second build reused no cached "
              "object; the cache was not in effect, so the link proves nothing")
        return 1
    print("incremental-instance-check: OK -- first build ran 3; after the edit the "
          "incremental build reused %d cached object(s), linked and ran 4" % reused)
    return 0


if __name__ == "__main__":
    sys.exit(main())
