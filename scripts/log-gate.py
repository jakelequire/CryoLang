#!/usr/bin/env python3
"""The compiler's logger is off by default, and each switch does what it says.

Builds `tests/fixtures/log` in a scratch copy, once per case, with
`--no-incremental` so every stage (the link included) runs each time, and
reads the log lines the compiler prints.  A log line is

    [<level> <area>] <file>:<line> <function>: <message>

Cases:
  - nothing set: no log line on either stream;
  - CRYO_LOG=driver: driver lines on stderr, none on stdout, every one at
    `debug` and naming a place in the compiler's source (not the logger's
    own file), and a known line among them;
  - CRYO_LOG=driver=info: no line (the driver logs at `debug`), so a level
    filters;
  - CRYO_LOG=loader / loader=trace: the module-order lines appear only at
    `trace`;
  - CRYO_LOG=lex,resolve,mono: exactly those areas log; parse=trace logs
    each statement;
  - CRYO_LOG=codegen: codegen lines, no trace among them; codegen=trace adds
    them;
  - CRYO_LOG=driver with --log=link,driver=off: the flag wins - link lines,
    no driver lines;
  - CRYO_LOG=loader,sema,drop,types: exactly those areas log; types=trace
    dumps the type arena;
  - --debug: more than one area logs, and no line on either stream looks
    like a `[Tag]` trace the logger did not write;
  - CRYO_LOG_FILE, and --log-file: the lines go to the file, not stderr;
  - CRYO_LOG=bogus: a warning naming the item, and the build still succeeds.

Usage:
    python scripts/log-gate.py --cryo compiler/build/cryo.exe

Exit codes: 0 when every case holds; 1 otherwise.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIXTURE = os.path.join(ROOT, "tests", "fixtures", "log")
SCRATCH = os.path.join(ROOT, ".verify", "log-gate")
STDLIB = os.path.join(ROOT, "stdlib").replace("\\", "/")
LINE = re.compile(r"^\[(error|warn|info|debug|trace) ([a-z]+)\] (\S+):(\d+) (\S+): (.*)$")


class Run:
    def __init__(self, code, out, err):
        self.code = code
        self.out = out
        self.err = err
        self.out_lines = [m for m in map(LINE.match, out.splitlines()) if m]
        self.err_lines = [m for m in map(LINE.match, err.splitlines()) if m]


def build(cryo, base_env, extra_env=None, flags=()):
    env = dict(base_env)
    env.update(extra_env or {})
    shutil.rmtree(os.path.join(SCRATCH, "build"), ignore_errors=True)
    r = subprocess.run([cryo, "build", "--stdlib=" + STDLIB, "--no-incremental"] + list(flags),
                       cwd=SCRATCH, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return Run(r.returncode, r.stdout.decode("utf-8", "replace"),
               r.stderr.decode("utf-8", "replace"))


def read_file_lines(path):
    if not os.path.isfile(path):
        return None
    with open(path, encoding="utf-8", errors="replace") as f:
        return [m for m in map(LINE.match, f.read().splitlines()) if m]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cryo", required=True)
    args = ap.parse_args()
    cryo = os.path.abspath(args.cryo)
    env = dict(os.environ)
    env.pop("CRYO_LOG", None)
    env.pop("CRYO_LOG_FILE", None)
    if os.name == "nt":
        env.setdefault("CRYO_CC", "gcc")

    shutil.rmtree(SCRATCH, ignore_errors=True)
    shutil.copytree(FIXTURE, SCRATCH, ignore=shutil.ignore_patterns("build"))

    failures = []
    checks = 0

    def check(ok, what, run=None):
        nonlocal checks
        checks += 1
        if not ok:
            failures.append(what)
            if run is not None:
                print("--- %s: exit %d\n--- stdout\n%s\n--- stderr\n%s"
                      % (what, run.code, run.out[-2000:], run.err[-2000:]))

    def areas(lines):
        return sorted(set(m.group(2) for m in lines))

    r = build(cryo, env)
    check(r.code == 0, "the fixture builds with logging off", r)
    check(not r.out_lines and not r.err_lines, "nothing is logged by default", r)

    r = build(cryo, env, {"CRYO_LOG": "driver"})
    check(r.code == 0 and r.err_lines, "CRYO_LOG=driver logs on stderr", r)
    check(not r.out_lines, "nothing is logged on stdout", r)
    check(areas(r.err_lines) == ["driver"], "CRYO_LOG=driver logs the driver alone", r)
    check(all(m.group(1) == "debug" for m in r.err_lines), "the driver logs at debug", r)
    check(all(m.group(3).endswith(".cryo") and int(m.group(4)) > 0 and "::" in m.group(5)
              for m in r.err_lines), "every line names a file, a line and a function", r)
    check(not any(m.group(3).endswith("log.cryo") for m in r.err_lines),
          "the place is the caller's, not the logger's", r)
    check(any(m.group(3).lower().endswith("instance.cryo")
              and m.group(5).endswith("compile_project")
              and m.group(6).startswith("compiling the project at")
              for m in r.err_lines), "a known driver line is among them", r)

    r = build(cryo, env, {"CRYO_LOG": "driver=info"})
    check(r.code == 0 and not r.err_lines, "driver=info filters out debug lines", r)

    r = build(cryo, env, {"CRYO_LOG": "loader"})
    check(r.code == 0 and r.err_lines
          and not any(m.group(6).startswith("compilation order") for m in r.err_lines),
          "loader at debug logs no trace lines", r)
    r = build(cryo, env, {"CRYO_LOG": "loader=trace"})
    check(r.code == 0 and any(m.group(1) == "trace" and m.group(6).startswith("compilation order")
                              for m in r.err_lines), "loader=trace logs trace lines", r)

    r = build(cryo, env, {"CRYO_LOG": "lex,resolve,mono"})
    check(r.code == 0 and areas(r.err_lines) == ["lex", "mono", "resolve"],
          "the lexer, the resolver and monomorphization log at debug", r)
    r = build(cryo, env, {"CRYO_LOG": "parse=trace"})
    check(r.code == 0 and any(m.group(6).startswith("statement at") for m in r.err_lines),
          "the parser logs each statement at trace", r)

    r = build(cryo, env, {"CRYO_LOG": "codegen"})
    check(r.code == 0 and areas(r.err_lines) == ["codegen"]
          and all(m.group(1) != "trace" for m in r.err_lines),
          "codegen logs at debug, without its trace lines", r)
    r = build(cryo, env, {"CRYO_LOG": "codegen=trace"})
    check(r.code == 0 and any(m.group(1) == "trace" for m in r.err_lines),
          "codegen=trace logs codegen's trace lines", r)

    r = build(cryo, env, {"CRYO_LOG": "driver"}, ["--log=link,driver=off"])
    check(r.code == 0 and areas(r.err_lines) == ["link"], "--log wins over CRYO_LOG", r)

    r = build(cryo, env, {"CRYO_LOG": "loader,sema,drop,types"})
    check(r.code == 0 and areas(r.err_lines) == ["drop", "loader", "sema", "types"],
          "the loader, sema, drop checking and types log at debug", r)
    r = build(cryo, env, {"CRYO_LOG": "types=trace"})
    check(r.code == 0 and any(m.group(6).startswith("type arena dump:") for m in r.err_lines)
          and sum(1 for m in r.err_lines if m.group(6).startswith("type ")) > 100,
          "types=trace dumps the type arena", r)

    r = build(cryo, env, flags=["--debug"])
    check(r.code == 0 and len(areas(r.err_lines)) > 1, "--debug turns every area on", r)
    check(not [l for l in (r.out + "\n" + r.err).splitlines()
               if l.startswith("[") and not LINE.match(l)],
          "--debug prints no trace outside the logger", r)

    log_path = os.path.join(SCRATCH, "env.log")
    r = build(cryo, env, {"CRYO_LOG": "driver", "CRYO_LOG_FILE": log_path})
    lines = read_file_lines(log_path)
    check(r.code == 0 and not r.err_lines and lines and areas(lines) == ["driver"],
          "CRYO_LOG_FILE takes the lines off stderr", r)

    log_path = os.path.join(SCRATCH, "flag.log")
    r = build(cryo, env, flags=["--log=driver", "--log-file=" + log_path.replace("\\", "/")])
    lines = read_file_lines(log_path)
    check(r.code == 0 and not r.err_lines and lines and areas(lines) == ["driver"],
          "--log-file takes the lines off stderr", r)

    r = build(cryo, env, {"CRYO_LOG": "bogus"})
    check(r.code == 0 and "warning: CRYO_LOG: 'bogus'" in r.err and not r.err_lines,
          "an unknown item is reported and the build goes on", r)

    shutil.rmtree(SCRATCH, ignore_errors=True)
    if failures:
        for f in failures:
            print("  failed: " + f)
        print("log-gate: FAIL -- %d of %d checks" % (len(failures), checks))
        return 1
    print("log-gate: OK (%d checks)" % checks)
    return 0


if __name__ == "__main__":
    sys.exit(main())
