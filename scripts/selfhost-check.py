#!/usr/bin/env python3
"""
selfhost-check: build the Cryo compiler through 3 rounds (6 stages) and
verify that stage-3 and stage-4 produce byte-identical IR.

The chain is rooted at the pinned `bin/cryo` self-hosted binary, not the
legacy C++ bootstrap. That binary is the canonical entry point for the
new chain (the C++ bootstrap at `legacy/bootstrap/bin/cryo` is retained
for archaeology only, and is no longer required by this script).
Whenever `compiler/src/` adopts new syntax that `bin/cryo` can't parse,
refresh the pin via `make pin-cryo` after a clean selfhost-check.

Why 3 rounds and not 4: stage-2 was built by the (potentially older) pin,
so its codegen behavior may differ from the new source's intent. Stage-3
is the first compiler whose codegen comes purely from the current source.
Once stage-3's codegen matches stage-4's codegen, the fixed point is
reached. The previous 4-round chain (stage-4 vs stage-5) added a single
extra safety round; in practice convergence happens at stage-3.

Stage outputs are nested under the regular build dirs to avoid
top-level clutter:

    compiler/build/                 (stage-2 — also what `make cryo` builds)
    compiler/build/self/s3/         (stage-3 compiler)
    compiler/build/self/s4/         (stage-4 compiler — IR-identity gate)
    stdlib/.bin/                    (built by pin — canonical link target)
    stdlib/.bin/self/s2/            (rebuilt by stage-2; smoke-test only)
    stdlib/.bin/self/s3/            (rebuilt by stage-3; smoke-test only)

The `.bin/self/sN` archives are written by their corresponding compiler
stage but never read back — every compiler stage links against the
canonical `<stdlib>/.bin/libcryo.a` produced in round 1. Rebuilding
stdlib at each round is a smoke test that the stage's codegen handles
the stdlib source, not a link dependency.

Usage:
    python3 scripts/selfhost-check.py            # (or `make selfhost-check`)
    python3 scripts/selfhost-check.py --verbose  # also stream subprocess output
    python3 scripts/selfhost-check.py --keep-logs

Exit codes:
    0  fixed point holds (stage-3 IR == stage-4 IR)
    1  any stage failed, or stage-3/stage-4 IR differ
    2  prerequisites missing (e.g. bin/cryo not present)
"""

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path

# ---------------------------------------------------------------------------
# Paths (resolved from this script's location, NOT the cwd)
# ---------------------------------------------------------------------------
ROOT   = Path(__file__).resolve().parent.parent
BOOT   = ROOT / "bin" / "cryo"                                       # pinned boot
STAGE2 = ROOT / "compiler" / "build"           / "bin" / "cryo"      # boot → stage-2
STAGE3 = ROOT / "compiler" / "build" / "self" / "s3" / "bin" / "cryo"  # stage-2 → stage-3
STAGE4 = ROOT / "compiler" / "build" / "self" / "s4" / "bin" / "cryo"  # stage-3 → stage-4
S3_LL  = ROOT / "compiler" / "build" / "self" / "s3" / "bin" / "cryo.ll"
S4_LL  = ROOT / "compiler" / "build" / "self" / "s4" / "bin" / "cryo.ll"
LOG_DIR = ROOT / "build-logs" / "selfhost-check"

# Top-level dirs we wipe before the chain runs. Recursive — covers the
# nested `self/` subtree as well.
WIPE_PATHS = [
    ROOT / "compiler" / "build",
    ROOT / "stdlib"   / ".bin",
]

# ---------------------------------------------------------------------------
# Color helper (honors NO_COLOR; only colorizes on a TTY)
# ---------------------------------------------------------------------------
class _Colors:
    def __init__(self, enabled):
        ansi = {
            "RESET": "\033[0m", "BOLD": "\033[1m", "DIM": "\033[2m",
            "RED": "\033[31m", "GREEN": "\033[32m", "YELLOW": "\033[33m",
            "BLUE": "\033[34m", "CYAN": "\033[36m",
        }
        for k, v in ansi.items():
            setattr(self, k, v if enabled else "")

C = _Colors(enabled=(sys.stdout.isatty() and not os.environ.get("NO_COLOR")))


# ---------------------------------------------------------------------------
# Stage definitions
# ---------------------------------------------------------------------------
@dataclass
class Stage:
    src: str         # "stdlib" or "compiler"
    via: str         # "pinned" / "stage-2" / "stage-3"
    to: str          # short descriptor of the output location
    cwd: Path
    cmd: list

    @property
    def label(self):
        return f"{self.src:<8}  via {self.via:<10}  →  {self.to}"


def make_stages():
    return [
        # ------------------------------------------------------------------
        # Round 1: pinned boot → stage-2  (default build dirs)
        # ------------------------------------------------------------------
        Stage(
            src="stdlib", via="pinned", to=".bin",
            cwd=ROOT / "stdlib",
            cmd=[str(BOOT), "build"],
        ),
        Stage(
            src="compiler", via="pinned", to="build",
            cwd=ROOT / "compiler",
            cmd=[str(BOOT), "build"],
        ),
        # ------------------------------------------------------------------
        # Round 2: stage-2 → stage-3  (nested under self/)
        # ------------------------------------------------------------------
        Stage(
            src="stdlib", via="stage-2", to=".bin/self/s2",
            cwd=ROOT / "stdlib",
            cmd=[str(STAGE2), "build", "--build-dir=.bin/self/s2"],
        ),
        Stage(
            src="compiler", via="stage-2", to="build/self/s3",
            cwd=ROOT / "compiler",
            cmd=[str(STAGE2), "build", "--build-dir=build/self/s3"],
        ),
        # ------------------------------------------------------------------
        # Round 3: stage-3 → stage-4  (byte-identity gate)
        # ------------------------------------------------------------------
        Stage(
            src="stdlib", via="stage-3", to=".bin/self/s3",
            cwd=ROOT / "stdlib",
            cmd=[str(STAGE3), "build", "--build-dir=.bin/self/s3"],
        ),
        Stage(
            src="compiler", via="stage-3", to="build/self/s4",
            cwd=ROOT / "compiler",
            cmd=[str(STAGE3), "build", "--build-dir=build/self/s4"],
        ),
    ]


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
def fmt_dur(secs: float) -> str:
    if secs < 1.0:
        return f"{secs * 1000:.0f}ms"
    if secs < 60:
        return f"{secs:.1f}s"
    return f"{int(secs) // 60}m{secs % 60:04.1f}s"


def hr(width: int = 72) -> str:
    return "─" * width


def wipe_paths(paths):
    for p in paths:
        if p.exists():
            shutil.rmtree(p)


def ensure_dir(p: Path):
    p.mkdir(parents=True, exist_ok=True)


# ---------------------------------------------------------------------------
# Stage runner
# ---------------------------------------------------------------------------
def run_stage(idx: int, total: int, stage: Stage, log_path: Path,
              verbose: bool) -> tuple[bool, float]:
    ensure_dir(log_path.parent)

    prefix = f"  {C.CYAN}[{idx}/{total}]{C.RESET} {stage.label}  "
    sys.stdout.write(prefix)
    sys.stdout.flush()

    is_tty = sys.stdout.isatty()
    start = time.perf_counter()
    stop = threading.Event()

    # Live elapsed-time updater (TTY + non-verbose only)
    def ticker():
        while not stop.wait(0.5):
            elapsed = time.perf_counter() - start
            sys.stdout.write(f"\r{prefix}{C.DIM}{fmt_dur(elapsed)}{C.RESET}")
            sys.stdout.flush()

    t = threading.Thread(target=ticker, daemon=True) if (is_tty and not verbose) else None
    if t:
        t.start()

    rc = -1
    try:
        with open(log_path, "w") as logf:
            proc = subprocess.Popen(
                stage.cmd, cwd=str(stage.cwd),
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                bufsize=1, text=True,
            )
            assert proc.stdout is not None
            for line in proc.stdout:
                logf.write(line)
                if verbose:
                    sys.stdout.write(line)
            proc.wait()
            rc = proc.returncode
    except FileNotFoundError as e:
        stop.set()
        if t:
            t.join(timeout=1.0)
        elapsed = time.perf_counter() - start
        line_start = f"\r{prefix}" if is_tty else ""
        sys.stdout.write(f"{line_start}{C.RED}✗ command not found{C.RESET}  {fmt_dur(elapsed)}\n")
        sys.stdout.flush()
        print(f"     {C.RED}{e}{C.RESET}")
        return False, elapsed

    stop.set()
    if t:
        t.join(timeout=1.0)
    elapsed = time.perf_counter() - start
    line_start = f"\r{prefix}" if is_tty else ""

    if rc == 0:
        sys.stdout.write(f"{line_start}{C.GREEN}✓{C.RESET}  {fmt_dur(elapsed)}{' ' * 8}\n")
        sys.stdout.flush()
        return True, elapsed

    # Failure: print error tail
    sys.stdout.write(f"{line_start}{C.RED}✗ exit {rc}{C.RESET}  {fmt_dur(elapsed)}\n")
    sys.stdout.flush()
    try:
        with open(log_path) as f:
            lines = f.readlines()
        tail = lines[-30:]
    except FileNotFoundError:
        tail = []

    print(f"     {C.DIM}log:{C.RESET} {log_path.relative_to(ROOT)}")
    print(f"     {C.RED}─── last {len(tail)} log lines {hr(48)}{C.RESET}")
    for line in tail:
        print(f"     {C.DIM}│{C.RESET} {line.rstrip()}")
    print(f"     {C.RED}{hr(72)}{C.RESET}")
    return False, elapsed


# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
def print_summary(times, total):
    print()
    print(f"{C.BOLD}Stage timings{C.RESET}")
    print(f"  {C.DIM}{hr(72)}{C.RESET}")
    for label, elapsed, ok in times:
        marker = f"{C.GREEN}✓{C.RESET}" if ok else f"{C.RED}✗{C.RESET}"
        pad = max(0, 56 - len(label))
        print(f"  {marker}  {label}{' ' * pad}{C.DIM}{fmt_dur(elapsed):>10s}{C.RESET}")
    print(f"  {C.DIM}{hr(72)}{C.RESET}")
    pad = max(0, 56 - len("Total"))
    print(f"     {C.BOLD}Total{' ' * pad}{fmt_dur(total):>10s}{C.RESET}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def ensure_boot():
    """Bail out if `bin/cryo` is missing — the pin is the entry point."""
    if BOOT.exists():
        return True
    print(f"{C.RED}✗ pinned boot not found:{C.RESET} {BOOT.relative_to(ROOT)}")
    print(f"  {C.DIM}Build a fresh self-hosted compiler and run `make pin-cryo`,")
    print(f"  or check out a revision that has bin/cryo committed.{C.RESET}")
    return False


def main():
    parser = argparse.ArgumentParser(
        description="Cryo selfhost byte-identity check (3 rounds, 6 stages, rooted at bin/cryo).",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="stream subprocess output to stdout in addition to logs")
    parser.add_argument("--keep-logs", action="store_true",
                        help="keep build-logs/selfhost-check/ from previous runs")
    args = parser.parse_args()

    if not ensure_boot():
        return 2

    print()
    print(f"{C.BOLD}selfhost-check{C.RESET}  {C.DIM}— 6-stage byte-identity gate (boot: bin/cryo){C.RESET}")
    print(f"  {C.DIM}root:{C.RESET} {ROOT}")
    print(f"  {C.DIM}logs:{C.RESET} {LOG_DIR.relative_to(ROOT)}/")
    print()

    if not args.keep_logs and LOG_DIR.exists():
        shutil.rmtree(LOG_DIR)
    ensure_dir(LOG_DIR)

    print(f"{C.BOLD}==> Wiping stage outputs{C.RESET}")
    wipe_paths(WIPE_PATHS)
    # Pre-create the nested `self/sN` parent dirs.  The compiler uses
    # `mkdir(path, mode)` (single-level) rather than `mkdir -p` when
    # honoring --build-dir, so it can't create `.bin/self/s2/obj/` if
    # `.bin/self/` doesn't exist yet.
    for p in [
        ROOT / "stdlib"   / ".bin" / "self" / "s2",
        ROOT / "stdlib"   / ".bin" / "self" / "s3",
        ROOT / "compiler" / "build" / "self" / "s3",
        ROOT / "compiler" / "build" / "self" / "s4",
    ]:
        p.mkdir(parents=True, exist_ok=True)
    print()

    stages = make_stages()
    total_start = time.perf_counter()
    times: list = []
    # "==> Wiping" header + the blank line under it = 2 lines so far
    wiping_lines = 2

    for i, stage in enumerate(stages, 1):
        log_path = LOG_DIR / f"stage-{i:02d}.log"
        ok, elapsed = run_stage(i, len(stages), stage, log_path, args.verbose)
        times.append((stage.label, elapsed, ok))
        wiping_lines += 1
        if not ok:
            total = time.perf_counter() - total_start
            print_summary(times, total)
            print()
            print(f"{C.RED}{C.BOLD}✗ FAILED at stage {i}/{len(stages)}{C.RESET}")
            return 1

    # All stages passed: collapse the per-stage section so the final view is just
    # the verification block + timings panel. Only safe on a real TTY.
    cleared_wiping = sys.stdout.isatty() and not args.verbose
    if cleared_wiping:
        sys.stdout.write(f"\033[{wiping_lines}F\033[J")
        sys.stdout.flush()

    # Byte-identity verification
    if not cleared_wiping:
        print()
    print(f"{C.BOLD}==> Verifying stage-3 == stage-4 IR byte identity{C.RESET}")

    if not S3_LL.exists() or not S4_LL.exists():
        if not S3_LL.exists():
            print(f"  {C.RED}✗ missing:{C.RESET} {S3_LL.relative_to(ROOT)}")
        if not S4_LL.exists():
            print(f"  {C.RED}✗ missing:{C.RESET} {S4_LL.relative_to(ROOT)}")
        total = time.perf_counter() - total_start
        print_summary(times, total)
        return 1

    s3 = S3_LL.read_bytes()
    s4 = S4_LL.read_bytes()

    if s3 == s4:
        md5 = hashlib.md5(s3).hexdigest()
        print(f"  {C.GREEN}{C.BOLD}✓ FIXED POINT OK{C.RESET}  stage-3 and stage-4 produce byte-identical IR")
        print(f"  {C.DIM}IR md5:{C.RESET}  {md5}")
        print(f"  {C.DIM}IR size:{C.RESET} {len(s3):,} bytes")
        result_ok = True
    else:
        print(f"  {C.RED}{C.BOLD}✗ FIXED POINT BROKEN{C.RESET}  stage-3 and stage-4 IR differ")
        diff = subprocess.run(
            ["diff", "-u", str(S3_LL), str(S4_LL)],
            capture_output=True, text=True,
        )
        diff_lines = diff.stdout.splitlines()
        head = diff_lines[:60]
        print(f"  {C.DIM}showing first {len(head)} of {len(diff_lines)} diff lines:{C.RESET}")
        print(f"  {C.RED}{hr(72)}{C.RESET}")
        for line in head:
            print(f"  {C.DIM}│{C.RESET} {line}")
        print(f"  {C.RED}{hr(72)}{C.RESET}")
        result_ok = False

    total = time.perf_counter() - total_start
    print_summary(times, total)
    return 0 if result_ok else 1


if __name__ == "__main__":
    sys.exit(main())
