#!/usr/bin/env python3
"""One verification run: every gate over the built tree, and the objects it compiled.

`make verify` builds the compiler under test (and the runtime and test-helper
archives) once, then this runs, AT THE SAME TIME, the gates that read that
build:

    census     scripts/test-census.py    the three suites, counts reconciled
    examples   scripts/examples-gate.py  every examples/ project builds
    lsp        scripts/lsp-gate.py       tools/CryoLSP builds
    cross      scripts/cross-check.py    the other OS's gated half compiles
    incr       scripts/incremental-instance-check.py
                                         an edit asking a cached module for a
                                         new generic instance still links
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

A change that makes a refused program compile makes the baseline's census fail
on that program's project, by design; so does any change that makes a project
pass that the baseline's run of it failed - a program that ran wrong, a
refusal whose report was missing what the project expects.  Such a project
is DECLARED, one per line with the reason, committed with the change: in
`tests/started-compiling` when the baseline refuses the program and the tree
builds it, in `tests/started-passing` otherwise.  The entries that apply to a
run are the ones the working tree's file lists and REV's does not, so an
entry is inert once the baseline has it, and asserted again against any
older baseline.  For every one, the baseline census must fail on exactly the
declared projects and nothing else, and the tree's census passing them is
the rest of the claim; a started-compiling project must also be REFUSED by
the baseline with a diagnostic and BUILT by the tree.  Their objects are
left out of the comparison and counted.  A declaration is an assertion,
never an exemption: declaring a project the baseline already passed, or a
started-compiling one the baseline builds or the tree refuses, fails the
run.

Usage:
    make verify [ARGS="--baseline HEAD"]
    python scripts/verify.py --cryo compiler/build/cryo.exe [--baseline REV]
                             [--require-identical] [--only census,examples]
    python scripts/verify.py --selftest

Exit codes: 0 every gate passed (and, with --require-identical, no object
moved); 1 otherwise.
"""
import argparse
import hashlib
import importlib.util
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
    "incr":     ([PY, "scripts/incremental-instance-check.py", "--cryo", "{cryo}"],
                 r"^incremental-instance-check: "),
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

# What a compiler build reads besides compiler/src.  The baseline compiler's
# cache key and the clone's sync both use this one list, so the key names
# exactly the files the baseline was built from.
BUILD_INPUTS = ["stdlib", "runtime", "bin", "compiler", "Makefile"]


def build_inputs(cwd=ROOT):
    return [f for f in listed(BUILD_INPUTS, cwd=cwd) if not f.startswith("compiler/src/")]


def copy_over(a, b):
    """Copy a -> b, clearing a read-only attribute git may have left on b."""
    os.makedirs(os.path.dirname(b), exist_ok=True)
    if os.path.exists(b):
        os.chmod(b, 0o644)
    shutil.copyfile(a, b)


def sync_clone(src, sha):
    """Make `src` REV's compiler/src with the working tree's build inputs."""
    if not os.path.isdir(os.path.join(src, ".git")):
        os.makedirs(os.path.dirname(src), exist_ok=True)
        subprocess.run(["git", "clone", "--quiet", "--no-checkout", ROOT, src], check=True)
    git("fetch", "--quiet", ROOT, sha, cwd=src)
    git("checkout", "--quiet", "--force", "--detach", sha, cwd=src)
    tree = build_inputs()
    have = build_inputs(src)
    for f in set(have) - set(tree):
        p = os.path.join(src, f)
        os.chmod(p, 0o644)
        os.remove(p)
    for f in tree:
        a, b = os.path.join(ROOT, f), os.path.join(src, f)
        if not os.path.isfile(a):
            continue
        if os.path.isfile(b) and file_sha(a) == file_sha(b):
            continue
        copy_over(a, b)
    # Ignored inputs the build needs and git does not carry.
    for extra in ("bin", os.path.join(".toolchains", "llvm-win", "lib")):
        s = os.path.join(ROOT, extra)
        if os.path.isdir(s):
            for dirpath, _, files in os.walk(s):
                for f in files:
                    a = os.path.join(dirpath, f)
                    b = os.path.join(src, os.path.relpath(a, ROOT))
                    if not os.path.isfile(b) or os.path.getsize(a) != os.path.getsize(b):
                        copy_over(a, b)


def baseline_compiler(rev, env):
    """Path to a compiler built from REV's compiler/src and the tree's everything else."""
    sha = git("rev-parse", "--verify", rev + "^{commit}").strip()
    src_tree = git("rev-parse", sha + ":compiler/src").strip()
    key = hashlib.sha256((src_tree + digest(build_inputs()).hexdigest()).encode()).hexdigest()[:16]
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
    # Always a clean build: the incremental cache can reuse a module that
    # owns a generic instance a changed module newly requests, and the link
    # then fails - or, in a shape nobody has met yet, links stale code.
    shutil.rmtree(os.path.join(src, "compiler", "build"), ignore_errors=True)
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


# ---- programs a change makes compile ---------------------------------------

DECLARED = "tests/started-compiling"
FIXED = "tests/started-passing"
PROJECTS = "tests/tests/projects"
# A refusal is a rendered diagnostic, not merely a non-zero exit: a crash or a
# link failure exits non-zero too, and neither is the compiler refusing the
# program.
DIAGNOSTIC = re.compile(r"error\[E\d{4}\]")
PROJ_FAILED = re.compile(r"^test \(project\) (\S+) \.\.\. FAILED")


def parse_declared(text):
    """{project: reason} from a declaration file's text."""
    out = {}
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        name, _, reason = line.partition(" ")
        out[name] = reason.strip()
    return out


def declared_since(sha, rel=DECLARED):
    """The declarations the working tree's `rel` carries and revision `sha`'s
    does not."""
    path = os.path.join(ROOT, rel)
    tree = {}
    if os.path.isfile(path):
        with io.open(path, encoding="utf-8") as fh:
            tree = parse_declared(fh.read())
    r = subprocess.run(["git", "show", "%s:%s" % (sha, rel)], cwd=ROOT,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    base = parse_declared(r.stdout.decode("utf-8", "replace")) if r.returncode == 0 else {}
    return {k: v for k, v in tree.items() if k not in base}


def census_failures(log):
    """(verdict accounted?, unit failed, compile-fail failed, failed projects)."""
    spec = importlib.util.spec_from_file_location(
        "test_census", os.path.join(ROOT, "scripts", "test-census.py"))
    census = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(census)
    with io.open(log, encoding="utf-8", errors="replace") as fh:
        lines = fh.readlines()
    blocks, _, _, _ = census.parse(lines)
    accounted = any(l.startswith("test-census: OK") for l in lines)
    failed = sorted({m.group(1) for m in map(PROJ_FAILED.match, lines) if m})
    return (accounted, blocks.get("unit test", {}).get("failed", -1),
            blocks.get("compile-fail", {}).get("failed", -1), failed)


def judge_baseline_census(declared, accounted, unit_failed, neg_failed, failed, fixed=()):
    """Problems with a baseline census that may fail on declared projects only:
    `declared` the ones it refuses, `fixed` the ones it builds and runs wrong."""
    problems = []
    if not accounted:
        problems.append("the baseline census did not account for the pinned corpus")
    if unit_failed != 0 or neg_failed != 0:
        problems.append("the baseline failed %s unit and %s compile-fail test(s); only "
                        "declared projects may fail there" % (unit_failed, neg_failed))
    for p in sorted(set(failed) - set(declared) - set(fixed)):
        problems.append("the baseline fails project %s, which neither %s nor %s declares"
                        % (p, DECLARED, FIXED))
    for p in sorted((set(declared) | set(fixed)) - set(failed)):
        problems.append("%s is declared as changed by this change, but the baseline census "
                        "passes it; a program this change makes compile or corrects fails "
                        "the baseline's census" % p)
    for p in sorted(set(declared) & set(fixed)):
        problems.append("%s is declared in both %s and %s" % (p, DECLARED, FIXED))
    return problems


def judge_builds(name, base_rc, base_out, tree_rc, tree_out, fixed=False):
    """Problems with one declared project's two direct builds.  A
    started-passing (`fixed`) project's claim is its census verdicts, so its
    builds are reported and not judged; a started-compiling one must be
    refused by the baseline and built by the tree."""
    problems = []
    if fixed:
        return problems
    if base_rc == 0:
        problems.append("%s: the baseline compiler BUILDS it (exit 0); it is not a "
                        "program this change makes compile" % name)
    elif not DIAGNOSTIC.search(base_out):
        problems.append("%s: the baseline exits %d with no error diagnostic; a crash or "
                        "a link failure is not a refusal" % (name, base_rc))
    if tree_rc != 0:
        problems.append("%s: the compiler under test does not build it (exit %d)"
                        % (name, tree_rc))
    return problems


def build_copy(cryo, name, dest):
    """`cryo build` over a copy of project `name` at `dest`: (exit, output)."""
    shutil.rmtree(dest, ignore_errors=True)
    shutil.copytree(os.path.join(ROOT, PROJECTS, name), dest,
                    ignore=shutil.ignore_patterns("build"))
    r = subprocess.run([cryo, "build", "--stdlib=" + STDLIB], cwd=dest,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = r.stdout.decode("utf-8", "replace")
    with io.open(dest + ".log", "w", encoding="utf-8") as fh:
        fh.write(out)
    return r.returncode, out


def check_declared(declared, base_exe, cryo, logdir, env, fixed=()):
    """Build every declared project with both compilers; the problems found."""
    problems = []
    for name, is_fixed in sorted([(n, False) for n in declared] + [(n, True) for n in fixed]):
        rel = FIXED if is_fixed else DECLARED
        if not os.path.isfile(os.path.join(ROOT, PROJECTS, name, "test.json")):
            problems.append("%s: declared in %s but %s/%s is not a test project"
                            % (name, rel, PROJECTS, name))
            continue
        brc, bout = build_copy(base_exe, name, os.path.join(logdir, "declared", "base", name))
        trc, tout = build_copy(cryo, name, os.path.join(logdir, "declared", "tree", name))
        found = judge_builds(name, brc, bout, trc, tout, is_fixed)
        print("  %-9s %-40s baseline exit %d, tree exit %d  %s"
              % ("passing" if is_fixed else "declared", name, brc, trc,
                 "ok" if not found else "FAIL"))
        problems += found
    return problems


def selftest():
    """Drive the two judges through every case they must refuse and allow."""
    cases = [
        ("census: fails on the declared project only",
         judge_baseline_census({"p"}, True, 0, 0, ["p"]), 0),
        ("census: nothing declared, nothing failed",
         judge_baseline_census({}, True, 0, 0, []), 0),
        ("census: an undeclared project fails",
         judge_baseline_census({"p"}, True, 0, 0, ["p", "q"]), 1),
        ("census: a declared project the baseline passes",
         judge_baseline_census({"p", "q"}, True, 0, 0, ["p"]), 1),
        ("census: a unit failure is never declarable",
         judge_baseline_census({"p"}, True, 1, 0, ["p"]), 1),
        ("census: the corpus not accounted for",
         judge_baseline_census({"p"}, False, 0, 0, ["p"]), 1),
        ("builds: refused before, builds now",
         judge_builds("p", 1, "error[E0203]: x", 0, ""), 0),
        ("builds: the baseline builds it too",
         judge_builds("p", 0, "", 0, ""), 1),
        ("builds: the baseline crashed, no diagnostic",
         judge_builds("p", 3, "segfault", 0, ""), 1),
        ("builds: the tree still refuses it",
         judge_builds("p", 1, "error[E0203]: x", 1, "error[E0203]: x"), 1),
        ("started-passing: a program the baseline built",
         judge_builds("p", 0, "", 0, "", True), 0),
        ("started-passing: a refusal whose report changes",
         judge_builds("p", 1, "error[E0900]: x", 1, "error[E0900]: x", True), 0),
        ("census: a corrected project fails the baseline",
         judge_baseline_census({}, True, 0, 0, ["q"], {"q"}), 0),
        ("census: a corrected project the baseline passes",
         judge_baseline_census({}, True, 0, 0, [], {"q"}), 1),
        ("census: one project declared as both kinds",
         judge_baseline_census({"q"}, True, 0, 0, ["q"], {"q"}), 1),
        ("declarations: parse skips comments and blanks",
         [] if parse_declared("# c\n\np why\nq\n") == {"p": "why", "q": ""} else ["bad"], 0),
    ]
    bad = 0
    for label, problems, want in cases:
        got = len(problems)
        ok = got == want
        bad += not ok
        print("  %-4s %-50s %d problem(s), want %d" % ("ok" if ok else "FAIL", label, got, want))
    print("verify selftest: %d of %d cases as expected" % (len(cases) - bad, len(cases)))
    return 1 if bad else 0


def compare(tag_a, a_path, b_path, limit, excluded=()):
    a, b = read_hashes(a_path), read_hashes(b_path)
    if excluded:
        prefixes = tuple("%s/%s/" % (PROJECTS, n) for n in excluded)
        drop_a = [p for p in a if p.startswith(prefixes)]
        drop_b = [p for p in b if p.startswith(prefixes)]
        for p in drop_a:
            del a[p]
        for p in drop_b:
            del b[p]
        if drop_a or drop_b:
            print("  %-9s declared projects' objects left out: baseline %d, tree %d"
                  % (tag_a, len(drop_a), len(drop_b)))
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
    ap.add_argument("--cryo", help="the compiler under test")
    ap.add_argument("--selftest", action="store_true",
                    help="drive the declared-program judges through their cases")
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
    if args.selftest:
        return selftest()
    if not args.cryo:
        ap.error("--cryo is required")

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
    declared = {}
    fixed = {}
    if args.baseline:
        exe, home, sha = baseline_compiler(args.baseline, env)
        declared = declared_since(sha)
        fixed = declared_since(sha, FIXED)
        print("verify: %d program(s) declared as starting to compile since %s%s"
              % (len(declared), args.baseline,
                 "".join("\n      %s -- %s" % (n, r) for n, r in sorted(declared.items()))))
        print("verify: %d program(s) declared as starting to pass since %s%s"
              % (len(fixed), args.baseline,
                 "".join("\n      %s -- %s" % (n, r) for n, r in sorted(fixed.items()))))
        pkey = population_key(exe)
        cached = os.path.join(home, "pop-" + pkey)
        if os.path.isfile(os.path.join(cached, "base.tests.sha256")):
            print("verify: baseline objects cached for this population: %s" % rel(cached))
            # Cached only after the baseline census was judged, so its verdict
            # is recorded as the projects it failed on.
            with io.open(os.path.join(cached, "base.failed"), encoding="utf-8") as fh:
                census = (True, 0, 0, fh.read().split())
        else:
            print("verify: baseline run (%s) over the working tree's population"
                  % ", ".join(POPULATION_GATES))
            bdir = os.path.join(logdir, "baseline")
            os.makedirs(bdir)
            bres, bcounts = population(exe, "base", bdir, env, list(POPULATION_GATES))
            report_gates(bres)
            census = census_failures(os.path.join(bdir, "census.log"))
            # The census may fail on declared projects; every other gate, and
            # every other suite, must pass outright.
            complete = all(r["ok"] for r in bres if r["name"] != "census") \
                and all(bcounts.values()) \
                and (next(r for r in bres if r["name"] == "census")["ok"] or declared or fixed)
            if not complete:
                print("verify: FAIL -- the baseline run did not complete; its objects "
                      "are not a baseline (counts %s)" % bcounts)
                return 1
        problems = judge_baseline_census(declared, *census, fixed=fixed)
        problems += check_declared(declared, exe, cryo, logdir, env, fixed)
        if problems:
            for p in problems:
                print("verify: FAIL -- %s" % p)
            return 1
        if not os.path.isfile(os.path.join(cached, "base.tests.sha256")):
            os.makedirs(cached, exist_ok=True)
            for base in ("tests", "examples"):
                shutil.copyfile(os.path.join(bdir, "base.%s.sha256" % base),
                                os.path.join(cached, "base.%s.sha256" % base))
            with io.open(os.path.join(cached, "base.failed"), "w", encoding="utf-8") as fh:
                fh.write("\n".join(census[3]) + "\n")
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
                            os.path.join(logdir, "tree.%s.sha256" % base), args.list_limit,
                            excluded=set(declared) | set(fixed))
        if diff and args.require_identical:
            ok = False

    print("verify: %s in %.0f s -- logs in %s"
          % ("OK" if ok else "FAIL", time.monotonic() - t0, rel(logdir)))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
