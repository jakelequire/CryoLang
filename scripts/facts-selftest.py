#!/usr/bin/env python3
"""Show that a gate reading the compiler's facts REFUSES stale facts, and
that no gate regenerates them.

Refreshing `.facts/` is a compiler build and a facts run - minutes, not
seconds - so it is a deliberate act (`make facts`), never a cost a gate
pays on the caller's behalf.  Two halves, each driven in both directions:

  * the freshness check itself (`scripts/facts.py --check`, and
    `facts_path`, which the lane gate and the residue check call before they
    read a record), over a throwaway tree: fresh facts are accepted; an
    edited source, an added source and a missing record of inputs are each
    refused by name;
  * the make targets that read the facts, dry-run (`make -n`) in this
    repository: `lane-check` and `check-fast` run the check and build
    nothing - no compiler build, no facts generation.  A prerequisite that
    regenerates would print its build here whether or not it is stale.

Usage:
    python3 scripts/facts-selftest.py

Exit codes: 0 every case behaved; 1 otherwise, with the case named.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FACTS = os.path.join(ROOT, "scripts", "facts.py")
PY = sys.executable

# One source per tree the freshness hash covers.
SOURCES = {
    "compiler/src/main.cryo": "function main() -> i32 { return 0; }\n",
    "stdlib/core/lib.cryo": "namespace std::core;\n",
    "tools/CryoLSP/src/main.cryo": "function main() -> i32 { return 0; }\n",
}


def write(root, rel, text):
    p = os.path.join(root, rel)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, "w", encoding="utf-8") as fh:
        fh.write(text)


def fixture(root):
    """A tree whose facts are fresh: the script copied in (it roots itself at
    its own parent), the sources, and facts plus a record of the inputs
    written by the script's own hash."""
    os.makedirs(os.path.join(root, "scripts"))
    shutil.copyfile(FACTS, os.path.join(root, "scripts", "facts.py"))
    for rel, text in SOURCES.items():
        write(root, rel, text)
    code = ("import sys; sys.path.insert(0, sys.argv[1]); import facts, os; "
            "src = facts.sources_hash(); os.makedirs(facts.OUT, exist_ok=True)\n"
            "for name, _p, _f in facts.PROJECTS:\n"
            "    open(os.path.join(facts.OUT, name + '.facts'), 'w').write('record\\n')\n"
            "    open(os.path.join(facts.OUT, name + '.inputs'), 'w').write('sources %s\\n' % src)\n")
    r = subprocess.run([PY, "-c", code, os.path.join(root, "scripts")],
                       stdin=subprocess.DEVNULL, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit("facts-selftest: FAIL -- the fixture could not be written:\n" + r.stderr)


def check(root):
    r = subprocess.run([PY, os.path.join(root, "scripts", "facts.py"), "--check"],
                       stdin=subprocess.DEVNULL, capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def facts_path(root):
    """What the lane gate and the residue check call before reading."""
    code = ("import sys; sys.path.insert(0, sys.argv[1]); import facts; "
            "print(facts.facts_path('compiler'))")
    r = subprocess.run([PY, "-c", code, os.path.join(root, "scripts")],
                       stdin=subprocess.DEVNULL, capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def freshness_cases():
    """(name, mutate(root), expect_refused, the word the refusal names)."""
    def edit(root):
        write(root, "compiler/src/main.cryo", "function main() -> i32 { return 1; }\n")

    def add(root):
        write(root, "stdlib/core/more.cryo", "namespace std::core;\n")

    def lose_inputs(root):
        os.remove(os.path.join(root, ".facts", "compiler.inputs"))

    def untouched(root):
        pass

    def unhashed(root):
        # A file the facts cannot depend on: not a source, not a config.
        write(root, "compiler/src/NOTES.md", "notes\n")

    return [
        ("fresh facts are accepted", untouched, False, "OK"),
        ("a non-source file leaves them fresh", unhashed, False, "OK"),
        ("an edited source is refused", edit, True, "STALE"),
        ("an added source is refused", add, True, "STALE"),
        ("a missing record of inputs is refused", lose_inputs, True, "MISSING"),
    ]


# A line of a dry run that builds: the compiler invoked to build, or the facts
# script asked to generate (it takes --cryo only to generate).
BUILDS = re.compile(r'cryo(\.exe)?"?\s+build\b|scripts/facts\.py\s+--cryo')
CHECKS = re.compile(r'scripts/facts\.py\s+--check\b')


def dry_run(target):
    r = subprocess.run(["make", "-n", "--no-print-directory", target], cwd=ROOT,
                       stdin=subprocess.DEVNULL, capture_output=True, text=True,
                       errors="replace")
    return r.returncode, (r.stdout + r.stderr).splitlines()


def main():
    failed = 0
    ran = 0
    for name, mutate, refused, word in freshness_cases():
        tmp = tempfile.mkdtemp(prefix="facts-selftest-")
        try:
            fixture(tmp)
            mutate(tmp)
            for how, (code, out) in (("facts.py --check", check(tmp)),
                                     ("facts_path", facts_path(tmp))):
                ran += 1
                ok = (code != 0) if refused else (code == 0)
                # The refusal must name its cause; facts_path is quiet when
                # fresh and prints the check's line when not.
                if refused and word not in out:
                    ok = False
                if not refused and how == "facts.py --check" and word not in out:
                    ok = False
                if not ok:
                    failed += 1
                    print("facts-selftest: FAIL -- %s (%s): exit %d, expected %s naming %s\n%s"
                          % (name, how, code, "a refusal" if refused else "acceptance",
                             word, out.rstrip()))
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    for target in ("lane-check", "check-fast"):
        ran += 1
        code, lines = dry_run(target)
        builds = [l for l in lines if BUILDS.search(l)]
        checks = [l for l in lines if CHECKS.search(l)]
        if code != 0 or builds or not checks:
            failed += 1
            print("facts-selftest: FAIL -- `make -n %s` (exit %d) must run the freshness "
                  "check and build nothing; builds: %s; checks: %d"
                  % (target, code, builds or "none", len(checks)))

    if failed:
        print("facts-selftest: FAIL (%d of %d cases)" % (failed, ran))
        return 1
    print("facts-selftest: OK (%d cases: stale facts refused, fresh accepted, "
          "no gate regenerates)" % ran)
    return 0


if __name__ == "__main__":
    sys.exit(main())
