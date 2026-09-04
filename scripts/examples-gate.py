#!/usr/bin/env python3
"""Build every examples/*/ project and fail if any of them does not compile.

Why this is a script rather than a shell loop
---------------------------------------------
The loop used to live in the Makefile, POSIX-only, and the Windows branch
printed "run it from WSL" and exited 0.  A gate that exits 0 having done
nothing is worse than no gate: it is counted as evidence.  A rename that
wrongly rewrote every examples/ project passed the whole local suite because
this target swept nothing on the host it ran on.

So the sweep must be able to say what it covered.  Three refusals, all of
which exit non-zero rather than reporting success:

  * the compiler binary is missing               -> cannot sweep
  * no examples/*/cryoconfig was discovered      -> swept nothing
  * fewer projects than --min                    -> swept less than expected

and the summary line states the population, so a reader sees "14 projects"
instead of inferring it from an exit code.
"""
import argparse, os, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def discover(examples_dir):
    out = []
    if not os.path.isdir(examples_dir):
        return out
    for name in sorted(os.listdir(examples_dir)):
        d = os.path.join(examples_dir, name)
        if os.path.isfile(os.path.join(d, "cryoconfig")):
            out.append(d)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cryo", required=True, help="compiler binary to sweep with")
    ap.add_argument("--min", type=int, default=1,
                    help="fail if fewer than this many projects are discovered")
    ap.add_argument("--stdlib", default=os.path.join(ROOT, "stdlib"))
    args = ap.parse_args()

    # Resolved before any chdir: each project is built with cwd set to its own
    # directory, so a relative binary or stdlib path would silently vanish.
    args.cryo = os.path.abspath(args.cryo)
    args.stdlib = os.path.abspath(args.stdlib)

    if not os.path.isfile(args.cryo):
        print("examples-gate: FAIL -- compiler binary not found: %s" % args.cryo)
        print("examples-gate: refusing to report success without sweeping anything")
        return 2

    projects = discover(os.path.join(ROOT, "examples"))
    if len(projects) < args.min:
        print("examples-gate: FAIL -- discovered %d project(s), expected at least %d"
              % (len(projects), args.min))
        print("examples-gate: refusing to report success without sweeping anything")
        return 2

    env = dict(os.environ)
    env["CRYO_STDLIB"] = args.stdlib

    failed = []
    for d in projects:
        rel = os.path.relpath(d, ROOT).replace(os.sep, "/")
        r = subprocess.run([args.cryo, "build", "."], cwd=d, env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if r.returncode == 0:
            print("  ok    %s" % rel)
        else:
            failed.append(rel)
            print("  FAIL  %s" % rel)
            tail = r.stdout.decode("utf-8", "replace").splitlines()[-25:]
            for ln in tail:
                print("        | %s" % ln)

    if failed:
        print("examples-gate: FAIL -- %d of %d project(s) did not build: %s"
              % (len(failed), len(projects), " ".join(failed)))
        return 1

    print("examples-gate: OK -- %d project(s) built" % len(projects))
    return 0


if __name__ == "__main__":
    sys.exit(main())
