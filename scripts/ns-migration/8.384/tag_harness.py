#!/usr/bin/env python3
"""Build the harness beside this script against the compiler library, run
each mode alone, and check what each did.

A type handle or a signature id read against an arena or index that did not
make it - a second one's, one's predecessor rebuilt in the same variable, or
a free-standing allocator's - must be REFUSED (a panic, exit 101); one read
against its own arena or index must be ANSWERED.

Usage:
  python3 scripts/ns-migration/8.384/tag_harness.py [--cryo EXE]
  python3 scripts/ns-migration/8.384/tag_harness.py --before REV [--cryo EXE]

`--before REV` builds the same harness against REV's compiler source (a
`git archive` under .objcmp/tagharness-before/, four levels under the root
as the link overlay needs) and prints what that tree ANSWERS, one line per
mode; it asserts nothing, because over a tree without the tags the answers
are the evidence.  REV's `TypeRefs::next` took the arena, so the free-standing
allocator's mint is swapped for `mint_before.cryo`.

Needs CRYO_STDLIB (the repository's stdlib) and, on Windows, CRYO_CC=gcc;
the LLVM runtime DLLs are taken from .toolchains/llvm-win/bin.  About thirty
seconds, most of it compiling the compiler library.  Ends with
`tag-harness: OK` or `tag-harness: FAIL` (or `tag-harness: BEFORE` lines).
"""
import argparse, os, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
PROJECT = os.path.join(HERE, "harness")

# mode -> (expected exit, text the output must contain)
EXPECT = {
    "sizes":        (0,   "sizes: TypeRef 8, OverloadId 8"),
    "ty_own":      (0,   "ty_own: 'i32*'"),
    "ty_foreign":   (101, "a type handle made by allocator"),
    "ty_stale":     (101, "a type handle made by allocator"),
    "ty_allocator": (101, "a type handle made by allocator"),
    "ovl_own":      (0,   "ovl_own: 'sym_alpha'"),
    "ovl_foreign":  (101, "a signature id made by index"),
    "ovl_stale":    (101, "a signature id made by index"),
}


def before_project(rev):
    base = os.path.join(ROOT, ".objcmp", "tagharness-before")
    shutil.rmtree(base, ignore_errors=True)
    os.makedirs(base)
    src = os.path.join(base, "src")
    os.makedirs(src)
    arc = subprocess.run(["git", "archive", "--format=tar", rev, "compiler"], cwd=ROOT,
                         capture_output=True)
    if arc.returncode != 0:
        sys.exit("tag-harness: git archive %s failed: %s" % (rev, arc.stderr.decode(errors="replace")))
    subprocess.run(["tar", "-x", "-C", src], input=arc.stdout, check=True)
    project = os.path.join(base, "x", "harness")
    shutil.copytree(PROJECT, project, ignore=shutil.ignore_patterns("build"))
    shutil.copyfile(os.path.join(HERE, "mint_before.cryo"), os.path.join(project, "src", "mint.cryo"))
    cfg = os.path.join(project, "cryoconfig")
    with open(cfg, encoding="utf-8") as f:
        text = f.read()
    text = text.replace('path = "../../../../compiler"', 'path = "../../src/compiler"')
    with open(cfg, "w", encoding="utf-8") as f:
        f.write(text)
    return project


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cryo", default=os.path.join(ROOT, "bin", "cryo.exe" if os.name == "nt" else "cryo"))
    ap.add_argument("--before", metavar="REV")
    a = ap.parse_args()
    project = before_project(a.before) if a.before else PROJECT
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
        if a.before:
            print("tag-harness: BEFORE %-13s exit %3d  %s" % (mode, r.returncode, out[:160]))
            continue
        ok = r.returncode == want_exit and want_text in out
        bad += 0 if ok else 1
        print("%-5s %-13s exit %3d  %s" % ("ok" if ok else "WRONG", mode, r.returncode, out[:160]))
    if a.before:
        return 0
    if bad:
        print("tag-harness: FAIL -- %d of %d modes did not do what they must" % (bad, len(EXPECT)))
        return 1
    print("tag-harness: OK -- %d modes: every foreign, stale or free-standing handle refused, every own one answered" % len(EXPECT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
