#!/usr/bin/env python3
"""One verification run: every gate over the built tree, and the objects it compiled.

`make verify` builds the compiler under test (and the runtime and test-helper
archives) once, then this runs, AT THE SAME TIME, the gates that read that
build:

    census     scripts/test-census.py    the three suites, counts reconciled
    examples   scripts/examples-gate.py  every examples/ project builds
    lsp        scripts/lsp-gate.py       tools/CryoLSP builds
    cross      scripts/cross-check.py    the other OS's gated half compiles
    fast       make check-fast           lane surface, section 0, pin integrity

They are independent: each writes its own build directory, and nothing here
rebuilds a shared archive while another reads it (which is why the gates are
called as scripts, not as `make` targets whose phony prerequisites rebuild the
runtime tiers).  Run one after another they take the sum of their times; run
together, about the longest one.

Then it hashes the objects the census and the examples sweep compiled - the
same population the object comparison has always used (every `.o` under a
`build/` directory in tests/, and under examples/*/build) - from the runs that
produced them.  A separate hashing run would compile the whole suite a second
time to produce byte-identical objects.

`--baseline REV` compares those objects against the ones a baseline compiler
produces over the SAME population: a compiler built from REV's compiler/src
with everything else taken from the working tree, which is what "this change
moved no compiled output" means.  The baseline compiler is built in a clone
under `.verify/`, never by stashing the working tree, and both it and its
object hashes are cached by the bytes that decide them, so a session pays for
a given baseline once.

Every count this relies on is printed.  A zero is only evidence when the
instrument can report non-zero, so the object counts are floors: a hash list
with no objects is a failure, not a clean comparison.

Usage:
    make verify [ARGS="--baseline HEAD"]
    python scripts/verify.py --cryo compiler/build/cryo.exe [--baseline REV]
                             [--require-identical] [--only census,examples]

Exit codes: 0 every gate passed (and, with --require-identical, no object
moved); 1 otherwise.
"""
import argparse
import hashlib
import io
import os
import re
import shutil
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor

if hasattr(sys.stdout, "reconfigure"):
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, OSError):
        pass

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STATE = os.path.join(ROOT, ".verify")
STDLIB = os.path.join(ROOT, "stdlib").replace("\\", "/")
KEEP_RUNS = 20
PY = sys.executable
EXE = ".exe" if os.name == "nt" else ""

# Each gate's own verdict line: the run is judged on it and on the exit code,
# never on the exit code alone.
GATES = {
    "census":   ([PY, "scripts/test-census.py", "--cryo", "{cryo}"], r"^test-census: "),
    "examples": ([PY, "scripts/examples-gate.py", "--cryo", "{cryo}", "--stdlib", "{stdlib}"],
                 r"^examples-gate: "),
    "lsp":      ([PY, "scripts/lsp-gate.py", "--cryo", "{cryo}"], r"^lsp-gate: "),
    "cross":    ([PY, "scripts/cross-check.py", "--cryo", "{cryo}"], r"^cross-check: "),
    # `ARGS=` on the command line: `make verify ARGS=...` exports ARGS to
    # child makes, and check-fast's lane-check would receive verify's flags.
    "fast":     (["make", "--no-print-directory", "check-fast", "ARGS="], r"check-fast: "),
}
# The gates whose objects are compared.  A baseline run needs only these.
POPULATION_GATES = ("census", "examples")


def rel(p):
    return os.path.relpath(p, ROOT).replace(os.sep, "/")


def git(*args, cwd=ROOT):
    r = subprocess.run(["git"] + list(args), cwd=cwd, stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE)
    if r.returncode != 0:
        raise SystemExit("verify: git %s failed: %s"
                         % (" ".join(args), r.stderr.decode("utf-8", "replace").strip()))
    return r.stdout.decode("utf-8", "replace")


def listed(paths, cwd=ROOT):
    """Tracked and untracked-but-not-ignored files under `paths`."""
    out = git("ls-files", "-co", "--exclude-standard", "-z", "--", *paths, cwd=cwd)
    return sorted(p for p in out.split("\0") if p)


def digest(files, cwd=ROOT, h=None):
    h = h or hashlib.sha256()
    for f in files:
        full = os.path.join(cwd, f)
        if not os.path.isfile(full):
            continue
        h.update(b"\0" + f.encode("utf-8") + b"\0")
        with open(full, "rb") as fh:
            h.update(fh.read())
    return h


def file_sha(path):
    with open(path, "rb") as fh:
        return hashlib.sha256(fh.read()).hexdigest()


# ---- the population --------------------------------------------------------

def build_dirs():
    """Every build directory the census and the examples sweep write."""
    out = []
    for base in ("tests", "examples"):
        for dirpath, dirs, _ in os.walk(os.path.join(ROOT, base)):
            if "build" in dirs:
                out.append(os.path.join(dirpath, "build"))
                dirs.remove("build")
    return out


def wipe_population():
    for d in build_dirs():
        shutil.rmtree(d, ignore_errors=True)
    left = build_dirs()
    if left:
        raise SystemExit("verify: could not clear %s; a warm build compiles "
                         "nothing and hashes stale objects" % rel(left[0]))


def hash_objects(base, out_path):
    """sha256 of every .o under a build/ directory below `base`; returns the count."""
    rows = []
    for dirpath, _, files in os.walk(os.path.join(ROOT, base)):
        parts = rel(dirpath).split("/")
        if "build" not in parts:
            continue
        for f in files:
            if f.endswith(".o"):
                p = os.path.join(dirpath, f)
                rows.append("%s  %s" % (file_sha(p), rel(p)))
    rows.sort(key=lambda r: r[66:])
    with io.open(out_path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(rows) + ("\n" if rows else ""))
    return len(rows)


def read_hashes(path):
    m = {}
    with io.open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.rstrip("\n")
            if line:
                m[line[66:]] = line[:64]
    return m


# ---- running gates ---------------------------------------------------------

def run_gate(name, cryo, logdir, env):
    argv, verdict = GATES[name]
    argv = [a.replace("{cryo}", cryo).replace("{stdlib}", STDLIB) for a in argv]
    log = os.path.join(logdir, name + ".log")
    t0 = time.monotonic()
    with open(log, "wb") as fh:
        rc = subprocess.run(argv, cwd=ROOT, env=env, stdout=fh,
                            stderr=subprocess.STDOUT).returncode
    secs = time.monotonic() - t0
    line = ""
    with io.open(log, encoding="utf-8", errors="replace") as fh:
        for ln in fh:
            if re.search(verdict, ln):
                line = ln.strip().strip('"')
    # A gate that printed no verdict line did not finish, whatever it exited.
    ok = rc == 0 and bool(line) and " OK" in line
    return {"name": name, "rc": rc, "secs": secs, "line": line, "ok": ok, "log": log}


def run_gates(names, cryo, logdir, env):
    with ThreadPoolExecutor(max_workers=len(names)) as pool:
        return list(pool.map(lambda n: run_gate(n, cryo, logdir, env), names))


def report_gates(results):
    for r in results:
        print("  %-9s %-4s %6.1f s  %s" % (r["name"], "ok" if r["ok"] else "FAIL",
                                          r["secs"], r["line"] or "<no verdict line; see %s>" % rel(r["log"])))


def population(cryo, tag, logdir, env, names):
    """Wipe, run `names` (which include the population gates), hash."""
    wipe_population()
    results = run_gates(names, cryo, logdir, env)
    counts = {}
    for base in ("tests", "examples"):
        counts[base] = hash_objects(base, os.path.join(logdir, "%s.%s.sha256" % (tag, base)))
    return results, counts


# ---- the baseline ----------------------------------------------------------

def sync_clone(src, sha):
    """Make `src` REV's compiler/src with every other file from the working tree."""
    if not os.path.isdir(os.path.join(src, ".git")):
        os.makedirs(os.path.dirname(src), exist_ok=True)
        subprocess.run(["git", "clone", "--quiet", "--no-checkout", ROOT, src], check=True)
    git("fetch", "--quiet", ROOT, sha, cwd=src)
    git("checkout", "--quiet", "--force", "--detach", sha, cwd=src)
    tree = [f for f in listed(["."]) if not f.startswith("compiler/src/")]
    have = [f for f in listed(["."], cwd=src) if not f.startswith("compiler/src/")]
    for f in set(have) - set(tree):
        os.remove(os.path.join(src, f))
    for f in tree:
        a, b = os.path.join(ROOT, f), os.path.join(src, f)
        if not os.path.isfile(a):
            continue
        if os.path.isfile(b) and file_sha(a) == file_sha(b):
            continue
        os.makedirs(os.path.dirname(b), exist_ok=True)
        shutil.copyfile(a, b)
    # Ignored inputs the build needs and git does not carry.
    for extra in ("bin", os.path.join(".toolchains", "llvm-win", "lib")):
        s = os.path.join(ROOT, extra)
        if os.path.isdir(s):
            for dirpath, _, files in os.walk(s):
                for f in files:
                    a = os.path.join(dirpath, f)
                    b = os.path.join(src, os.path.relpath(a, ROOT))
                    if not os.path.isfile(b) or os.path.getsize(a) != os.path.getsize(b):
                        os.makedirs(os.path.dirname(b), exist_ok=True)
                        shutil.copyfile(a, b)


def baseline_compiler(rev, env):
    """Path to a compiler built from REV's compiler/src and the tree's everything else."""
    sha = git("rev-parse", "--verify", rev + "^{commit}").strip()
    src_tree = git("rev-parse", sha + ":compiler/src").strip()
    # Everything else a compiler build reads, taken from the working tree.
    rest = [f for f in listed(["stdlib", "runtime", "bin", "compiler", "Makefile"])
            if not f.startswith("compiler/src/")]
    key = hashlib.sha256((src_tree + digest(rest).hexdigest()).encode()).hexdigest()[:16]
    home = os.path.join(STATE, "baseline", key)
    exe = os.path.join(home, "cryo" + EXE)
    if os.path.isfile(exe):
        print("verify: baseline compiler for %s (compiler/src %s) is cached: %s"
              % (rev, src_tree[:12], rel(exe)))
        return exe, home, sha
    print("verify: building the baseline compiler for %s (compiler/src %s) in %s"
          % (rev, src_tree[:12], rel(os.path.join(STATE, "src"))))
    src = os.path.join(STATE, "src")
    sync_clone(src, sha)
    log = os.path.join(STATE, "baseline-build.log")
    with open(log, "wb") as fh:
        rc = subprocess.run(["make", "--no-print-directory", "cryo"], cwd=src, env=env,
                            stdout=fh, stderr=subprocess.STDOUT).returncode
    built = os.path.join(src, "compiler", "build", "cryo" + EXE)
    if rc != 0 or not os.path.isfile(built):
        raise SystemExit("verify: the baseline compiler did not build (exit %d); see %s"
                         % (rc, rel(log)))
    os.makedirs(home, exist_ok=True)
    for f in os.listdir(os.path.join(ROOT, "bin")):
        if f.lower().endswith(".dll"):
            shutil.copyfile(os.path.join(ROOT, "bin", f), os.path.join(home, f))
    shutil.copyfile(built, exe)
    return exe, home, sha


def population_key(cryo):
    """The bytes that decide what the population compiles to under `cryo`."""
    h = hashlib.sha256(file_sha(cryo).encode())
    digest(listed(["tests", "examples", "stdlib", "runtime", "bin"]), h=h)
    # The procedure that takes the hashes decides them too (how the stdlib is
    # spelled to each run, which flags a build gets).
    digest(["scripts/verify.py", "scripts/test-census.py", "scripts/examples-gate.py"], h=h)
    return h.hexdigest()[:16]


def compare(tag_a, a_path, b_path, limit):
    a, b = read_hashes(a_path), read_hashes(b_path)
    moved = sorted(p for p in a.keys() & b.keys() if a[p] != b[p])
    only_a = sorted(a.keys() - b.keys())
    only_b = sorted(b.keys() - a.keys())
    print("  %-9s baseline %5d  tree %5d  moved %d  only-baseline %d  only-tree %d"
          % (tag_a, len(a), len(b), len(moved), len(only_a), len(only_b)))
    for label, items in (("moved", moved), ("only-baseline", only_a), ("only-tree", only_b)):
        for p in items[:limit]:
            print("      %-13s %s" % (label, p))
        if len(items) > limit:
            print("      ... %d more %s (full lists: %s, %s)"
                  % (len(items) - limit, label, rel(a_path), rel(b_path)))
    return len(moved) + len(only_a) + len(only_b)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cryo", required=True, help="the compiler under test")
    ap.add_argument("--baseline", default=None,
                    help="compare objects against a compiler built from this "
                         "revision's compiler/src (e.g. HEAD)")
    ap.add_argument("--require-identical", action="store_true",
                    help="fail when any object differs from the baseline")
    ap.add_argument("--only", default=None,
                    help="comma-separated gates to run (default all: %s)"
                         % ",".join(GATES))
    ap.add_argument("--list-limit", type=int, default=40)
    args = ap.parse_args()

    cryo = os.path.abspath(args.cryo)
    if not os.path.isfile(cryo):
        print("verify: FAIL -- compiler under test not found: %s" % cryo)
        return 1
    names = [n.strip() for n in args.only.split(",")] if args.only else list(GATES)
    for n in names:
        if n not in GATES:
            print("verify: FAIL -- no gate named %r (gates: %s)" % (n, ", ".join(GATES)))
            return 1
    if args.baseline and not set(POPULATION_GATES) <= set(names):
        print("verify: FAIL -- a baseline comparison needs the census and examples runs")
        return 1

    # Objects embed stdlib source paths exactly as the stdlib root was spelled
    # (panic locations), so the same source compiles to different bytes under
    # `C:\...\stdlib`, `C:/.../stdlib` and `./../stdlib`.  Each gate therefore
    # sees the stdlib the way its own flow always has: the census with no
    # CRYO_STDLIB (tests/ finds it relative to the project, whichever binary
    # runs, as `make test` does), the examples sweep with the absolute
    # forward-slash root the object hashes were always taken with.  An
    # inherited CRYO_STDLIB would silently change every census object.
    env = dict(os.environ)
    env.pop("CRYO_STDLIB", None)
    if os.name == "nt":
        env.setdefault("CRYO_CC", "gcc")

    stamp = time.strftime("%Y%m%d-%H%M%S")
    runs = os.path.join(STATE, "runs")
    logdir = os.path.join(runs, stamp)
    os.makedirs(logdir)
    # Keep the newest runs only: each holds a census log and two hash lists,
    # and an ignored scratch directory that only grows is how `.objcmp/`
    # reached 15 GB.
    for old in sorted(os.listdir(runs))[:-KEEP_RUNS]:
        shutil.rmtree(os.path.join(runs, old), ignore_errors=True)
    t0 = time.monotonic()
    ok = True

    base_counts = None
    if args.baseline:
        exe, home, sha = baseline_compiler(args.baseline, env)
        pkey = population_key(exe)
        cached = os.path.join(home, "pop-" + pkey)
        if os.path.isfile(os.path.join(cached, "base.tests.sha256")):
            print("verify: baseline objects cached for this population: %s" % rel(cached))
        else:
            print("verify: baseline run (%s) over the working tree's population"
                  % ", ".join(POPULATION_GATES))
            bdir = os.path.join(logdir, "baseline")
            os.makedirs(bdir)
            bres, bcounts = population(exe, "base", bdir, env, list(POPULATION_GATES))
            report_gates(bres)
            if not all(r["ok"] for r in bres) or not all(bcounts.values()):
                print("verify: FAIL -- the baseline run did not complete; its objects "
                      "are not a baseline (counts %s)" % bcounts)
                return 1
            os.makedirs(cached, exist_ok=True)
            for base in ("tests", "examples"):
                shutil.copyfile(os.path.join(bdir, "base.%s.sha256" % base),
                                os.path.join(cached, "base.%s.sha256" % base))
        base_counts = cached

    print("verify: gates over %s: %s" % (rel(cryo), ", ".join(names)))
    results, counts = population(cryo, "tree", logdir, env, names)
    report_gates(results)
    ok = all(r["ok"] for r in results)
    hashed = [b for b, g in (("tests", "census"), ("examples", "examples")) if g in names]
    for base in hashed:
        print("  objects   %-8s %d (%s)" % (base, counts[base],
                                             rel(os.path.join(logdir, "tree.%s.sha256" % base))))
        if counts[base] == 0:
            print("verify: FAIL -- no %s objects were hashed; an empty list compares "
                  "clean against anything" % base)
            ok = False

    if base_counts:
        print("verify: objects against %s" % args.baseline)
        diff = 0
        for base in ("tests", "examples"):
            diff += compare(base, os.path.join(base_counts, "base.%s.sha256" % base),
                            os.path.join(logdir, "tree.%s.sha256" % base), args.list_limit)
        if diff and args.require_identical:
            ok = False

    print("verify: %s in %.0f s -- logs in %s"
          % ("OK" if ok else "FAIL", time.monotonic() - t0, rel(logdir)))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
