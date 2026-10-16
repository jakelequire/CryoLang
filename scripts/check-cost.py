#!/usr/bin/env python3
"""Time and peak memory of `cryo check` over a project, one or more compilers.

    python scripts/check-cost.py --cryo base=.scratch/base/cryo.exe \
        --cryo new=compiler/build/cryo.exe [--runs 3] [--project compiler] \
        [--file src/main.cryo]

Runs `<cryo> check <file>` in the project directory, interleaving the
compilers run by run (base, new, base, new, ...) so a change in the
machine's load lands on both.  Each run reports wall time, the process's
peak memory (Windows: the peak working set, read from the process handle
after exit; POSIX: the child's ru_maxrss), and the AST arena figures the
compiler prints under CRYO_TIMINGS: node allocations, their bytes, and the
arena's total use.  The medians per compiler close the report.

A run that does not end in a clean check, or that prints no arena figures,
is a failure: a number taken from a refused program measures the wrong
thing.  Every compiler is given the same `--stdlib`, this tree's.

Deletion point: delete this script when the class-removal plan
(docs/class-removal-plan.md) is deleted, its last slice landed.
"""

import argparse
import os
import re
import statistics
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

NODES_RE = re.compile(r"AST nodes\s+(\d+) allocations, (\d+) bytes")
ARENA_RE = re.compile(r"AST arena\s+(\d+) bytes used, (\d+) bytes capacity")
CLEAN_RE = re.compile(r"^(No errors found\.|Check passed with \d+ warnings?\.)$", re.M)


def peak_memory_windows(handle):
    """Peak working set, in bytes, of the (exited) process behind `handle`."""
    import ctypes
    from ctypes import wintypes

    class Counters(ctypes.Structure):
        _fields_ = [
            ("cb", wintypes.DWORD),
            ("PageFaultCount", wintypes.DWORD),
            ("PeakWorkingSetSize", ctypes.c_size_t),
            ("WorkingSetSize", ctypes.c_size_t),
            ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
            ("QuotaPagedPoolUsage", ctypes.c_size_t),
            ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
            ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
            ("PagefileUsage", ctypes.c_size_t),
            ("PeakPagefileUsage", ctypes.c_size_t),
        ]

    counters = Counters()
    counters.cb = ctypes.sizeof(Counters)
    query = ctypes.windll.kernel32.K32GetProcessMemoryInfo
    query.argtypes = [wintypes.HANDLE, ctypes.POINTER(Counters), wintypes.DWORD]
    query.restype = wintypes.BOOL
    if not query(wintypes.HANDLE(int(handle)), ctypes.byref(counters), counters.cb):
        raise OSError(ctypes.get_last_error(), "K32GetProcessMemoryInfo failed")
    return counters.PeakWorkingSetSize


def run_once(cryo, project, file, stdlib):
    env = dict(os.environ, CRYO_TIMINGS="1")
    cmd = [str(cryo), "check", file, f"--stdlib={stdlib}"]
    start = time.perf_counter()
    proc = subprocess.Popen(cmd, cwd=project, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if os.name == "nt":
        out = proc.communicate()[0]
        wall = time.perf_counter() - start
        peak = peak_memory_windows(proc._handle)
        code = proc.returncode
    else:
        # wait4 returns the rusage of this child alone, so read the output
        # through to EOF first and reap the child here, not in communicate().
        out = proc.stdout.read()
        _, status, usage = os.wait4(proc.pid, 0)
        wall = time.perf_counter() - start
        peak = usage.ru_maxrss * (1 if sys.platform == "darwin" else 1024)
        code = os.waitstatus_to_exitcode(status)
        proc.returncode = code
    text = out.decode("utf-8", "replace").replace("\r\n", "\n")
    nodes = NODES_RE.search(text)
    arena = ARENA_RE.search(text)
    if code != 0 or not CLEAN_RE.search(text) or not nodes or not arena:
        sys.stderr.write(text[-4000:])
        raise SystemExit(f"check-cost: {cryo} did not finish a clean check "
                         f"with arena figures (exit {code})")
    return {
        "wall": wall,
        "peak": peak,
        "nodes": int(nodes.group(1)),
        "node_bytes": int(nodes.group(2)),
        "arena_used": int(arena.group(1)),
    }


def mib(n):
    return f"{n / (1024 * 1024):.1f} MiB"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--cryo", action="append", required=True, metavar="LABEL=PATH",
                    help="a compiler to measure; repeat to interleave several")
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("--project", default="compiler")
    ap.add_argument("--file", default="src/main.cryo")
    args = ap.parse_args()

    compilers = []
    for spec in args.cryo:
        label, sep, path = spec.partition("=")
        if not sep or not label or not path:
            raise SystemExit(f"check-cost: --cryo wants LABEL=PATH, got {spec!r}")
        exe = Path(path).resolve()
        if not exe.is_file():
            raise SystemExit(f"check-cost: no compiler at {exe}")
        compilers.append((label, exe))

    project = (REPO / args.project).resolve()
    stdlib = (REPO / "stdlib").resolve()
    results = {label: [] for label, _ in compilers}

    for i in range(args.runs):
        for label, exe in compilers:
            r = run_once(exe, project, args.file, stdlib)
            results[label].append(r)
            print(f"run {i + 1} {label:>8}: {r['wall']:.2f} s, peak {mib(r['peak'])}, "
                  f"{r['nodes']} nodes / {r['node_bytes']} bytes, "
                  f"arena {r['arena_used']} bytes", flush=True)

    print("median:")
    for label, _ in compilers:
        rs = results[label]
        print(f"  {label:>8}: {statistics.median(r['wall'] for r in rs):.2f} s, "
              f"peak {mib(statistics.median(r['peak'] for r in rs))}, "
              f"{rs[-1]['nodes']} nodes / {rs[-1]['node_bytes']} bytes, "
              f"arena {rs[-1]['arena_used']} bytes")


if __name__ == "__main__":
    main()
