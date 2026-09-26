#!/usr/bin/env python3
"""Have the compiler under test report its own calls: `--emit=facts` over the
compiler, the standard library and the editor, written to `.facts/`.

    python scripts/facts.py --cryo compiler/build/cryo.exe    # generate
    python scripts/facts.py --check                            # fresh?

Each project is built with `cryo build --emit=facts` - the build command
itself, not a separate mode, so the facts are what the body check resolved
while compiling the code that ships - into a build directory of its own
under `.facts/build/`, and its facts file is copied to `.facts/<name>.facts`:

    compiler.facts  compiler/       the compiler's sources and the stdlib it uses
    stdlib.facts    stdlib/         the standard library built on its own
    lsp.facts       tools/CryoLSP/  the editor, with the compiler as a library

Beside each goes `<name>.inputs`: a hash over every source file the facts
could depend on (the compiler's, the stdlib's and the editor's `.cryo` files
and project configs).  `--check` recomputes it and refuses a missing or
stale file, so a gate reading `.facts/` cannot count from facts that
describe some other tree.  The hash does not cover the compiler binary: the
make target builds it from the same sources first.

A build is refused unless it states the population it compiled ("Building
<name> [..]: N local, M std ...") and writes a non-empty facts file: an
incremental "up to date" answer compiles nothing, and a facts file that
reads empty from a build that never ran the body check is the zero this
instrument exists to rule out.
"""
import argparse, hashlib, os, re, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, ".facts")

# (name, project directory, the facts file the build writes: <project_name>.facts)
PROJECTS = [
    ("compiler", "compiler", "cryo.facts"),
    ("stdlib", "stdlib", "cryo-stdlib.facts"),
    ("lsp", os.path.join("tools", "CryoLSP"), "cryolsp.facts"),
]
# The trees whose sources any of the three can compile.
INPUT_TREES = ["compiler/src", "stdlib", "tools/CryoLSP/src"]
POPULATION = re.compile(r"^Building\s+\S+\s+\[\w+\]:\s+(\d+)\s+local")


def inputs_hash(cryo):
    h = hashlib.sha256()
    for tree in INPUT_TREES:
        base = os.path.join(ROOT, tree)
        for dirpath, dirnames, filenames in os.walk(base):
            dirnames[:] = sorted(d for d in dirnames if not d.startswith(".") and d != "build")
            for fn in sorted(filenames):
                if not (fn.endswith(".cryo") or fn == "cryoconfig"):
                    continue
                p = os.path.join(dirpath, fn)
                h.update(os.path.relpath(p, ROOT).replace("\\", "/").encode())
                h.update(b"\0")
                with open(p, "rb") as fh:
                    h.update(fh.read())
                h.update(b"\0")
    if cryo is not None:
        with open(cryo, "rb") as fh:
            h.update(fh.read())
    return h.hexdigest()


def sources_hash():
    return inputs_hash(None)


def generate(cryo, names):
    os.makedirs(OUT, exist_ok=True)
    stdlib = os.path.join(ROOT, "stdlib").replace("\\", "/")
    env = dict(os.environ, CRYO_STDLIB=stdlib)
    src = sources_hash()
    for name, proj, produced in PROJECTS:
        if names and name not in names:
            continue
        build_dir = os.path.join(OUT, "build", name)
        shutil.rmtree(build_dir, ignore_errors=True)
        facts = os.path.join(build_dir, produced)
        p = subprocess.run([cryo, "build", "--emit=facts",
                            "--build-dir=" + build_dir.replace("\\", "/")],
                           cwd=os.path.join(ROOT, proj), env=env,
                           capture_output=True, text=True, errors="replace")
        log = p.stdout + p.stderr
        with open(os.path.join(OUT, name + ".log"), "w", encoding="utf-8") as fh:
            fh.write(log)
        pops = [m.group(1) for m in (POPULATION.match(l) for l in log.splitlines()) if m]
        if p.returncode != 0:
            print("facts: FAIL -- %s: the build exited %d; see .facts/%s.log"
                  % (name, p.returncode, name))
            return 1
        if not pops:
            print("facts: FAIL -- %s: the build stated no population it compiled; "
                  "an up-to-date answer compiles nothing" % name)
            return 1
        if not os.path.exists(facts) or os.path.getsize(facts) == 0:
            print("facts: FAIL -- %s: the build wrote no facts (%s)" % (name, facts))
            return 1
        shutil.copyfile(facts, os.path.join(OUT, name + ".facts"))
        with open(os.path.join(OUT, name + ".inputs"), "w", encoding="utf-8") as fh:
            fh.write("sources %s\n" % src)
        with open(facts, encoding="utf-8") as fh:
            n = sum(1 for _ in fh)
        print("facts: %s -- %d records (%s local modules)" % (name, n, pops[0]))
    return 0


def check(names):
    src = sources_hash()
    bad = 0
    for name, _proj, _produced in PROJECTS:
        if names and name not in names:
            continue
        f = os.path.join(OUT, name + ".facts")
        i = os.path.join(OUT, name + ".inputs")
        if not os.path.exists(f) or not os.path.exists(i):
            print("facts: MISSING -- .facts/%s.facts; run `make facts`" % name)
            bad += 1
            continue
        with open(i, encoding="utf-8") as fh:
            recorded = fh.read().split()
        if len(recorded) != 2 or recorded[1] != src:
            print("facts: STALE -- .facts/%s.facts was written from other sources; run `make facts`" % name)
            bad += 1
    if bad == 0:
        print("facts: OK -- fresh")
    return 1 if bad else 0


def facts_path(name):
    """The facts file `name` names, refused unless it is fresh.  For a gate."""
    if check([name]) != 0:
        raise SystemExit(1)
    return os.path.join(OUT, name + ".facts")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cryo", help="the compiler under test")
    ap.add_argument("--check", action="store_true", help="refuse a missing or stale facts file")
    ap.add_argument("names", nargs="*", help="compiler, stdlib, lsp (default: all three)")
    args = ap.parse_args()
    if args.check:
        return check(args.names)
    if not args.cryo:
        ap.error("--cryo is required to generate")
    return generate(args.cryo, args.names)


if __name__ == "__main__":
    sys.exit(main())
