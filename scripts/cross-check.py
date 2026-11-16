#!/usr/bin/env python3
"""Compile runtime/, stdlib/, compiler/ and tools/CryoLSP for the OTHER
operating system's triple - objects only, from a Windows host - and fail if
any of it does not compile.

What the host cannot see
------------------------
Config gating prunes a `![config(linux)]` module or declaration from a
Windows build before name resolution ever sees it, and the other way round.
Every gate that runs on one host - the suite, the LSP build, the object
comparison, selfhost-check's native arm - therefore measures a tree with the
other OS's half cut out.  A `linux`-gated declaration can stop resolving and
nothing on a Windows host reports it; the runtime's backtrace module went red
that way and stayed red until someone ran the freestanding script from WSL.

`cryo build --target=<triple>` selects the other OS's gates and compiles
every module through name resolution, sema and codegen to object files.
That is the whole gate: four projects - the runtime once per configuration -
one foreign triple, exit codes read.

On a Windows host the Linux triple has no link configuration, so the build
stops at objects and no cross toolchain is needed.  On a Linux host it does
not: the compiler links a windows-gnu executable with
`x86_64-w64-mingw32-gcc` and the LLVM-C import library, so `compiler` and
`tools/CryoLSP` fail here unless both are installed.  The gate is run from
Windows - by `make verify` there and by CI's windows native job.

What it does not cover
----------------------
Nothing links and nothing runs.  A module that compiles to a wrong object
passes here as it does everywhere the object comparison is not run, and a
symbol the other OS's libc lacks is a link error this gate never reaches.
`tests/` and `examples/` are not built: they are host-native projects whose
config-gated surface is the stdlib's, which is built whole.

Why it starts cold
------------------
`cryo build` is incremental.  A warm build compiles nothing, prints
"up to date" and exits 0 - the same shape as a sweep that covered nothing.
The build directories are removed first, and the gate reports success only
when each project stated the population it built (the "N local ..." line)
and the total clears --min-modules.
"""
import argparse, os, re, shutil, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The projects, in the order they are cheapest to fail: the runtime is nine
# small modules, the stdlib is 154, the compiler is those plus its own 164,
# the LSP is the compiler again plus its own 24.  Each is a cryoconfig
# project, named by its path from the repo root.
PROJECTS = ["runtime", "stdlib", "compiler", "tools/CryoLSP"]

# Each build: its project, the options it takes and its name in the report.
# The runtime's declarations are gated on the configuration it is built as,
# so it is built as each configuration a link names.
BUILDS = [
    ("runtime", ["--no-runtime"], "runtime freestanding"),
    ("runtime", ["--panic=abort"], "runtime hosted-abort"),
    ("runtime", ["--panic=unwind"], "runtime hosted-unwind"),
    ("stdlib", [], "stdlib"),
    ("compiler", [], "compiler"),
    ("tools/CryoLSP", [], "tools/CryoLSP"),
]

# The triple to build for when none is given: the OS this host is not.  The
# `pc` vendor is what `cryo version --triple` reports on both hosts, so the
# per-triple directories the tree already carries spell it this way.
OTHER_OS_TRIPLE = {
    "windows": "x86_64-pc-linux-gnu",
    "linux":   "x86_64-pc-windows-gnu",
}

# "Building cryo [release]: 164 local, 81 std module(s)"
# "Building cryort [release]: 9 local module(s)"
POPULATION = re.compile(r"^Building\s+(\S+)\s+\[\w+\]:\s+(.*?)\s+module")
COUNT = re.compile(r"(\d+)\s+(local|std|dep)")


def host_os():
    return "windows" if os.name == "nt" else "linux"


def fail(msg):
    print("cross-check: FAIL -- %s" % msg)
    return 1


def build(cryo, project, options, triple, build_dir, env):
    """Run one project's build; return (returncode, modules, populations
    reported, all lines)."""
    shutil.rmtree(build_dir, ignore_errors=True)
    if os.path.isdir(build_dir):
        raise RuntimeError("could not clear %s; a warm build compiles nothing "
                           "and reports success" % build_dir)
    # The driver creates the build directory but not its parents: with the
    # per-triple level below --build-root absent too, every module's object
    # write fails as `codegen failed for module ...: no such file or directory`.
    os.makedirs(build_dir)
    r = subprocess.run([cryo, "build", "--target=%s" % triple,
                        "--build-dir=%s" % build_dir, "--no-incremental"] + options,
                       cwd=os.path.join(ROOT, project), stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, env=env)
    lines = r.stdout.decode("utf-8", "replace").splitlines()
    modules = 0
    populations = 0
    for ln in lines:
        m = POPULATION.match(ln)
        if m:
            populations += 1
            modules += sum(int(n) for n, _ in COUNT.findall(m.group(2)))
    return r.returncode, modules, populations, lines


def error_report(lines):
    """Every `error[...]` line with the `-->` span that follows it."""
    out = []
    for i, ln in enumerate(lines):
        if ln.startswith("error"):
            out.append(ln)
            for follow in lines[i + 1:i + 3]:
                if follow.strip().startswith("-->"):
                    out.append(follow)
                    break
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cryo", required=True,
                    help="compiler binary to build with (the compiler under test)")
    ap.add_argument("--triple", default=None,
                    help="target triple; default: the OS this host is not")
    ap.add_argument("--build-root", default=os.path.join(ROOT, ".cross-check"),
                    help="directory the per-project build dirs go under; "
                         "cleared per project before each build")
    ap.add_argument("--min-modules", type=int, default=350,
                    help="fail if the builds report fewer modules in total")
    ap.add_argument("--project", action="append", choices=PROJECTS,
                    help="build only this project (repeatable); default all four")
    args = ap.parse_args()

    cryo = os.path.abspath(args.cryo)
    if not os.path.isfile(cryo):
        return fail("compiler binary not found: %s" % cryo)
    triple = args.triple or OTHER_OS_TRIPLE[host_os()]
    projects = args.project or PROJECTS

    # The compiler under test resolves the stdlib relative to its own
    # location and cannot find it from runtime/ or compiler/; pinning the
    # in-tree stdlib also makes the result independent of which binary runs.
    env = dict(os.environ)
    env["CRYO_STDLIB"] = os.path.join(ROOT, "stdlib")

    for project in projects:
        if not os.path.isfile(os.path.join(ROOT, project, "cryoconfig")):
            return fail("no cryoconfig at %s; nothing to build" % project)

    # The builds are independent, each into its own directory, so they run
    # at once; each is still judged in the order above, and the first to
    # fail is the one reported.
    builds = [b for b in BUILDS if b[0] in projects]

    def one(planned):
        project, options, name = planned
        build_dir = os.path.join(os.path.abspath(args.build_root), triple,
                                 name.replace("/", "_").replace(" ", "_"))
        try:
            return build(cryo, project, options, triple, build_dir, env)
        except RuntimeError as e:
            return e

    with ThreadPoolExecutor(max_workers=len(builds)) as pool:
        results = list(pool.map(one, builds))

    total = 0
    summary = []
    for (project, _, name), res in zip(builds, results):
        if isinstance(res, RuntimeError):
            return fail(str(res))
        rc, modules, populations, lines = res
        errors = error_report(lines)
        if rc != 0:
            print("cross-check: %s does not compile for %s" % (name, triple))
            for ln in (errors or lines[-25:])[:40]:
                print("        | %s" % ln)
            return fail("%s: build exited %d with %d error line(s)"
                        % (name, rc, len([l for l in errors if l.startswith("error")])))
        if populations == 0:
            print("cross-check: %s reported no population.  The first line was:" % name)
            print("        | %s" % (lines[0] if lines else "<no output>"))
            return fail("%s: nothing was compiled; a build that skipped the tree "
                        "has not gated it" % name)
        total += modules
        summary.append("%s %d module(s)" % (name, modules))

    # The floor is a claim about the whole gate; a `--project` subset is a
    # way to reproduce one refusal, not a run of the gate.
    if not args.project and total < args.min_modules:
        return fail("compiled %d module(s) in total, expected at least %d"
                    % (total, args.min_modules))

    print("cross-check: OK -- %s: %s; 0 errors" % (triple, ", ".join(summary)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
