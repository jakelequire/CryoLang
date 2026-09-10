#!/usr/bin/env python3
"""Per-module incremental-build soundness harness.

The contract for per-module incremental compilation (see HANDOFF.md §5 and
build_manifest.cryo) is BYTE-IDENTITY: for ANY edit, an incremental build must
produce a final binary byte-for-byte identical to a clean `--no-incremental`
build of the same source. A false cache hit is a silent miscompile, so this
harness is the gate that guards the feature.

For a target project it runs an edit matrix. Each scenario:
  1. applies an edit to the working tree (or none),
  2. builds INCREMENTALLY (reusing whatever cache exists),
  3. builds the same source CLEAN with `--no-incremental`,
  4. asserts the two final binaries are byte-identical,
  5. reverts the edit.

It also checks the cold-cache path (first incremental build == clean) and the
no-change outer short-circuit.

Usage:
  scripts/incremental-check.py [--cryo PATH] [--project DIR] [--bin NAME]

Defaults target the self-hosted compiler project, which is the realistic,
generic-heavy workload. `--cryo` defaults to compiler/build/cryo.
"""
import argparse
import os
import shutil
import subprocess
import sys

# The check / cross glyphs this prints are above cp1252's range, so on a
# Windows console (PowerShell and cmd default to a legacy code page) the first
# scenario result raises UnicodeEncodeError and takes the run down mid-matrix -
# after the builds, before the verdict.  A gate that cannot finish on a host is
# a gate that host does not have.  selfhost-check.py already does this; so does
# this one now.  No-op on POSIX, where the streams are UTF-8 already.
if hasattr(sys.stdout, "reconfigure"):
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, OSError):
        pass
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def run_build(cryo, project, *extra):
    env = dict(os.environ)
    env.setdefault("CRYO_STDLIB", os.path.join(ROOT, "stdlib"))
    p = subprocess.run([cryo, "build", *extra], cwd=project, env=env,
                       capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


def clean_tree(project):
    bd = os.path.join(project, "build")
    if os.path.isdir(bd):
        shutil.rmtree(bd)


class Edit:
    """A reversible source edit."""

    def __init__(self, name, path, kind):
        self.name = name
        self.path = path          # absolute
        self.kind = kind          # 'append' | 'topinsert' | 'none' | flag tuple
        self._saved = None

    def apply(self):
        if self.kind in ("none",) or isinstance(self.kind, tuple):
            return
        with open(self.path, "rb") as f:
            self._saved = f.read()
        if self.kind == "append":
            with open(self.path, "ab") as f:
                f.write(b"\n// incremental-check: appended comment\n")
        elif self.kind == "topinsert":
            # Insert AFTER the first line so line numbers of all subsequent
            # code shift -> the emitted IR (which bakes FILE/LINE) actually
            # changes, exercising a real recompile, not just a hash bump.
            nl = self._saved.find(b"\n")
            if nl < 0:
                nl = len(self._saved)
            head, tail = self._saved[:nl + 1], self._saved[nl + 1:]
            with open(self.path, "wb") as f:
                f.write(head)
                f.write(b"// incremental-check: inserted line (shifts line numbers)\n")
                f.write(tail)

    def revert(self):
        if self._saved is not None:
            with open(self.path, "wb") as f:
                f.write(self._saved)
            self._saved = None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cryo", default=os.path.join(ROOT, "compiler", "build", "cryo"))
    ap.add_argument("--project", default=os.path.join(ROOT, "compiler"))
    ap.add_argument("--bin", default="cryo",
                    help="final binary name under <project>/build/")
    args = ap.parse_args()

    cryo = os.path.abspath(args.cryo)
    project = os.path.abspath(args.project)
    binpath = os.path.join(project, "build", args.bin)
    if not os.path.exists(cryo):
        print(f"error: compiler-under-test not found: {cryo}")
        return 2

    tmp = tempfile.mkdtemp(prefix="incr-check-")

    # Every cold-cache scenario wipes <project>/build, and the default
    # compiler-under-test lives inside it, so driving builds from `cryo`
    # directly would delete the binary mid-run. Drive them from a copy placed
    # outside the tree that gets wiped. The copy is transparent to the cache:
    # `Manifest::compiler_fingerprint` folds the compiler's own bytes, not its
    # path, and copy2 preserves both the content and the executable bit. The
    # exe-relative runtime-tier candidates miss from here, but `run_build` sets
    # CRYO_STDLIB, so the stdlib-relative candidate still resolves the tiers.
    driver = os.path.join(tmp, os.path.basename(cryo))
    shutil.copy2(cryo, driver)

    failures = []

    def build_and_snapshot(label, *extra):
        rc, out = run_build(driver, project, *extra)
        if rc != 0:
            print(f"  [{label}] BUILD FAILED (rc={rc})")
            print("\n".join("    " + l for l in out.splitlines()[-12:]))
            return None
        if not os.path.exists(binpath):
            print(f"  [{label}] no binary produced at {binpath}")
            return None
        snap = os.path.join(tmp, f"{label}.bin")
        shutil.copy2(binpath, snap)
        # surface the reuse line if present
        for l in out.splitlines():
            if "reusing" in l or "up to date" in l:
                print(f"  [{label}] {l.strip()}")
        return snap

    def assert_identical(a, b, scenario):
        if a is None or b is None:
            failures.append(scenario)
            print(f"  {scenario}: FAIL (missing build)")
            return
        with open(a, "rb") as fa, open(b, "rb") as fb:
            same = fa.read() == fb.read()
        print(f"  {scenario}: {'BYTE-IDENTICAL ✓' if same else 'DIFFER ✗'}")
        if not same:
            failures.append(scenario)

    print(f"== incremental-check: {project} (cryo={cryo}) ==")

    # CONTROL, before any scenario: is a link on this host byte-reproducible
    # AT ALL?  Every assertion below compares two binaries, so a toolchain
    # that stamps something into the artifact makes all of them read DIFFER
    # whatever the compiler did - and the run then blames incremental
    # compilation for the linker.  A Windows PE carries a link timestamp, so
    # on that host every scenario fails today and the gate has never been
    # able to say anything; measured here, two clean builds of one unchanged
    # source gave different md5s.
    #
    # The second clean build IS the reference, so the control costs one build
    # and not two.
    print("\n[control] two clean builds of unchanged source must agree")
    clean_tree(project)
    ctl = build_and_snapshot("control-clean", "--no-incremental")
    clean_tree(project)
    ref = build_and_snapshot("cold-clean", "--no-incremental")
    if ctl is None or ref is None:
        print("incremental-check: FAIL -- the control build produced no binary,"
              " so nothing below could be measured against anything.")
        return 1
    with open(ctl, "rb") as fa, open(ref, "rb") as fb:
        reproducible = fa.read() == fb.read()
    print(f"  control: {'BYTE-IDENTICAL ✓' if reproducible else 'DIFFER ✗'}")
    if not reproducible:
        print("incremental-check: FAIL -- this host does not link reproducibly.")
        print("  Two clean --no-incremental builds of unchanged source produced")
        print("  different bytes, so every scenario below would read DIFFER no")
        print("  matter what incremental compilation did.  On Windows this is the")
        print("  PE link timestamp.  Nothing can be measured until what is being")
        print("  compared is something the toolchain reproduces.")
        return 1

    # Cold path: that clean --no-incremental build is the reference; a
    # from-scratch incremental build must match it.
    print("\n[scenario] cold cache (first incremental build == clean)")
    clean_tree(project)
    cold_inc = build_and_snapshot("cold-incr")
    assert_identical(ref, cold_inc, "cold-cache")

    # No-change: the outer whole-build fingerprint should short-circuit, and
    # the (unchanged) binary must still match the reference.
    print("\n[scenario] no change (outer short-circuit)")
    nochg = build_and_snapshot("nochg-incr")
    assert_identical(ref, nochg, "no-change")

    # Edit matrix. The cache is warm from the build above; each edit triggers
    # a partial incremental rebuild that must match a clean build of the edit.
    src = os.path.join(project, "src")
    candidates = [
        ("leaf-comment",     "CLI/commands.cryo",                   "append"),
        ("type-lineshift",   "compiler/AST/node.cryo",              "topinsert"),
        ("generic-comment",  "compiler/types/generic_registry.cryo", "append"),
    ]
    edits = []
    missing = []
    for name, rel, kind in candidates:
        p = os.path.join(src, rel)
        if os.path.exists(p):
            edits.append(Edit(name, p, kind))
        else:
            missing.append((name, rel))
    # A missing edit target is a REFUSAL, not a note.  The matrix is three
    # hardcoded paths and a rename takes one out of it silently: the scenario
    # never runs, nothing else changes, and the run still ends "all scenarios
    # BYTE-IDENTICAL".  Renaming modules is most of what happens in this tree,
    # so a matrix quietly emptying is not hypothetical - and an empty matrix
    # reports exactly what a full one reports.
    if missing:
        print("\nincremental-check: FAIL -- edit target(s) no longer exist:")
        for name, rel in missing:
            print(f"  {name}: src/{rel}")
        print("  The scenario cannot run, and a matrix that shrank in silence")
        print("  certifies less each time a file moves.  Point it at whatever")
        print("  file replaced this one.")
        return 1

    for e in edits:
        print(f"\n[scenario] edit: {e.name} ({e.kind})")
        e.apply()
        try:
            inc = build_and_snapshot(f"{e.name}-incr")
            clean_tree(project)
            cln = build_and_snapshot(f"{e.name}-clean", "--no-incremental")
            assert_identical(inc, cln, f"edit:{e.name}")
        finally:
            e.revert()
        # rewarm the cache for the next scenario from the reverted source
        build_and_snapshot(f"{e.name}-rewarm")

    # Flag change: --dev after a --release-cached tree must still equal a clean
    # --dev build (different toolchain fingerprint -> full rebuild expected).
    print("\n[scenario] flag change (--dev)")
    dev_inc = build_and_snapshot("dev-incr", "--dev")
    clean_tree(project)
    dev_clean = build_and_snapshot("dev-clean", "--dev", "--no-incremental")
    assert_identical(dev_inc, dev_clean, "flag:--dev")

    print("\n== summary ==")
    if failures:
        print(f"FAILED scenarios: {', '.join(failures)}")
        return 1
    print(f"all scenarios BYTE-IDENTICAL ✓  "
          f"(no-change + {len(edits)} edit(s) + flag change)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
