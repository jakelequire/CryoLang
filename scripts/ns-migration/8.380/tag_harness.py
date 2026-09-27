#!/usr/bin/env python3
"""Build the harness beside this script against the compiler library in
compiler/src, run each mode alone, and check what each did.

A definition id or symbol id read against a table that did not make it -
a second table's, or a table's predecessor before a rebuild - must be
REFUSED (the table panics, exit 101); an id read against its own table must
be ANSWERED.  A symbol admitted to the resolver's arena at a slot its id
does not name must be refused too.

Usage: python3 scripts/ns-migration/8.380/tag_harness.py [--cryo EXE] [--project DIR]
(`--project` names a copy of `harness/` whose dependency points at another
compiler source, four levels under the repository root like this one - how
the harness is shown to FAIL over a tree without the tags.)
Needs CRYO_STDLIB (the repository's stdlib) and, on Windows, CRYO_CC=gcc;
the LLVM runtime DLLs are taken from .toolchains/llvm-win/bin.  About
thirty seconds, most of it compiling the compiler library.  Prints one line
per mode and a final `tag-harness: OK` or `tag-harness: FAIL`.
"""
import argparse, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
PROJECT = os.path.join(HERE, "harness")

# mode -> (expected exit, text the output must contain)
EXPECT = {
    "own":              (0,   "own: 'alpha'"),
    "foreign":          (101, "a definition id made by table"),
    "stale":            (101, "a definition id made by table"),
    "sym_own":          (0,   "sym_own: 'x_one'"),
    "sym_foreign":      (101, "made by allocator"),
    "synth_interleave": (101, "symbol admitted at arena slot 0, its id names slot 1"),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cryo", default=os.path.join(ROOT, "bin", "cryo"))
    ap.add_argument("--project", default=PROJECT)
    a = ap.parse_args()
    project = os.path.abspath(a.project)
    env = dict(os.environ)
    if "CRYO_STDLIB" not in env:
        sys.exit("tag-harness: CRYO_STDLIB is not set (a build without it exits early looking clean)")
    if os.name == "nt":
        env.setdefault("CRYO_CC", "gcc")
        env["PATH"] = os.path.join(ROOT, ".toolchains", "llvm-win", "bin") + os.pathsep + env["PATH"]
    b = subprocess.run([a.cryo, "build"], cwd=project, env=env, capture_output=True,
                       text=True, errors="replace")
    exe = os.path.join(project, "build", "tagharness.exe" if os.name == "nt" else "tagharness")
    if b.returncode != 0 or not os.path.exists(exe):
        print((b.stdout + b.stderr)[-3000:])
        sys.exit("tag-harness: FAIL -- the harness did not build")
    bad = 0
    for mode, (want_exit, want_text) in EXPECT.items():
        r = subprocess.run([exe, mode], cwd=project, env=env, capture_output=True,
                           text=True, errors="replace")
        out = (r.stdout + r.stderr).strip().replace("\n", " | ")
        ok = r.returncode == want_exit and want_text in out
        bad += 0 if ok else 1
        print("%-5s %-17s exit %3d  %s" % ("ok" if ok else "WRONG", mode, r.returncode, out[:160]))
    if bad:
        print("tag-harness: FAIL -- %d of %d modes did not do what they must" % (bad, len(EXPECT)))
        return 1
    print("tag-harness: OK -- %d modes: every foreign or stale id refused, every own id answered" % len(EXPECT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
