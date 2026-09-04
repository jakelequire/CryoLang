#!/usr/bin/env python3
"""Compile tools/CryoLSP against the current compiler source and fail if it breaks.

The LSP links the compiler as a *library*, so it sees every AST, NodeKind and
public-signature change, and nothing that runs during validation compiles it.
A fully green local gate set is therefore not evidence that the LSP builds.

Why this is a gate of its own rather than `make lsp`
---------------------------------------------------
`make lsp` does two jobs: it compiles the server, and it installs the result
over bin/cryolsp.  The install fails whenever an editor holds that file open,
which on a working machine is most of the time, and it fails as a bare
`error: linking failed` with no linker diagnostic - after a clean compile.  A
gate that fails for a reason unrelated to the code stops being read as
evidence.  So this one never writes to a path anything runs from: it builds
into its own directory and installs nothing.  Whether the pin can be replaced
is a separate question from whether the source still compiles, and only the
second one is a gate.

Why it starts cold
------------------
`cryo build` is incremental, and a warm build prints

    cryolsp is up to date (release)
    Compiled -> <dir>/cryolsp.exe

then exits 0 in ten seconds having compiled nothing.  That is the same shape
as a sweep that covered no projects: an absence that reads as progress.  So
the build directory is removed first, and success is reported only when the
compiler stated the population it built - the "N local, M std, K dep
module(s)" line - and that population clears --min-modules.  An "up to date"
build is a refusal here, not a pass; the gate cannot report what it did not
watch happen.
"""
import argparse, os, re, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROJECT = os.path.join(ROOT, "tools", "CryoLSP")

# "Building cryolsp [release]: 24 local, 81 std, 161 dep module(s)"
POPULATION = re.compile(
    r"^Building\s+(\S+)\s+\[(\w+)\]:\s+(\d+)\s+local,\s+(\d+)\s+std,\s+(\d+)\s+dep\s+module")
WARNINGS = re.compile(r"^(\d+)\s+warnings?\s+emitted")
COMPILED = re.compile(r"^Compiled\s+->\s+(.+)$")


def fail(msg):
    print("lsp-gate: FAIL -- %s" % msg)
    return 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cryo", required=True,
                    help="compiler binary to build the LSP with (the pin)")
    ap.add_argument("--build-dir", default="build/gate",
                    help="build directory, relative to tools/CryoLSP; must be a "
                         "path nothing runs a server from")
    ap.add_argument("--min-modules", type=int, default=200,
                    help="fail if the build reports fewer modules than this")
    ap.add_argument("--keep", action="store_true",
                    help="do not remove the build directory first.  Only for "
                         "exercising the refusal an incremental build triggers; "
                         "a gate run must not use it.")
    args = ap.parse_args()

    cryo = os.path.abspath(args.cryo)
    if not os.path.isfile(cryo):
        return fail("compiler binary not found: %s" % cryo)
    if not os.path.isfile(os.path.join(PROJECT, "cryoconfig")):
        return fail("no cryoconfig at %s; nothing to build" % PROJECT)

    out_dir = os.path.join(PROJECT, args.build_dir)
    if not args.keep:
        shutil.rmtree(out_dir, ignore_errors=True)
        if os.path.isdir(out_dir):
            return fail("could not clear %s; a warm build compiles nothing and "
                        "reports success" % out_dir)

    r = subprocess.run([cryo, "build", "--build-dir=%s" % args.build_dir],
                       cwd=PROJECT, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT)
    out = r.stdout.decode("utf-8", "replace")
    lines = out.splitlines()

    modules = None
    warnings = 0
    binary = None
    for ln in lines:
        m = POPULATION.match(ln)
        if m:
            modules = (int(m.group(3)), int(m.group(4)), int(m.group(5)))
            continue
        m = WARNINGS.match(ln)
        if m:
            warnings = int(m.group(1))
            continue
        m = COMPILED.match(ln)
        if m:
            binary = m.group(1).strip()

    errors = [ln for ln in lines if ln.startswith("error")]

    if r.returncode != 0:
        print("lsp-gate: the LSP does not compile against this compiler source")
        for ln in errors[:40]:
            print("        | %s" % ln)
        if not errors:
            for ln in lines[-25:]:
                print("        | %s" % ln)
        return fail("build exited %d with %d error line(s)"
                    % (r.returncode, len(errors)))

    if modules is None:
        print("lsp-gate: the build reported no population.  The first line was:")
        print("        | %s" % (lines[0] if lines else "<no output>"))
        return fail("nothing was compiled; an incremental build that skipped "
                    "the tree has not gated it")

    total = sum(modules)
    if total < args.min_modules:
        return fail("compiled %d module(s), expected at least %d"
                    % (total, args.min_modules))

    if binary is None:
        return fail("compiled %d module(s) but linked no binary" % total)

    abs_bin = binary if os.path.isabs(binary) else os.path.join(PROJECT, binary)
    if not os.path.isfile(abs_bin):
        return fail("build named %s but no such file exists" % binary)

    print("lsp-gate: OK -- compiled %d module(s) (%d local, %d std, %d dep), "
          "0 errors, %d warning(s); linked %s"
          % (total, modules[0], modules[1], modules[2], warnings, binary))
    return 0


if __name__ == "__main__":
    sys.exit(main())
