#!/usr/bin/env python3
"""
selfhost-check: build the Cryo compiler through 8 stages and verify that
stage-4 and stage-5 produce byte-identical IR.

Replaces the inline shell logic that used to live in the top-level
Makefile's `selfhost-check` target. The chain itself is unchanged; this
script just gives it a usable terminal UI: per-stage progress with live
elapsed time, ✓/✗ markers, per-stage logs in build-logs/selfhost-check/,
a tail-of-log dump on failure, and a summary table at the end.

Usage:
    python3 scripts/selfhost-check.py            # (or `make selfhost-check`)
    python3 scripts/selfhost-check.py --verbose  # also stream subprocess output
    python3 scripts/selfhost-check.py --keep-logs

Exit codes:
    0  fixed point holds (stage-4 IR == stage-5 IR)
    1  any stage failed, or stage-4/stage-5 IR differ
    2  prerequisites missing (e.g. bootstrap not buildable)
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
BOOT   = ROOT / "legacy"   / "bootstrap" / "bin" / "cryo"
STAGE2 = ROOT / "compiler" / "build"     / "cryo"
STAGE3 = ROOT / "compiler" / "build"     / "bin" / "cryo"
STAGE4 = ROOT / "compiler" / "build-s4"  / "bin" / "cryo"
STAGE5 = ROOT / "compiler" / "build-s5"  / "bin" / "cryo"
S4_LL  = ROOT / "compiler" / "build-s4"  / "bin" / "cryo.ll"
S5_LL  = ROOT / "compiler" / "build-s5"  / "bin" / "cryo.ll"
LOG_DIR = ROOT / "build-logs" / "selfhost-check"

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
    via: str         # "bootstrap" / "stage-2" / "stage-3" / "stage-4"
    to: str          # ".bin" / "stage-2" / ".bin-s2" / "stage-3" / ...
    cwd: Path
    cmd: list
    pre_wipe: list = field(default_factory=list)

    @property
    def label(self):
        return f"{self.src:<8}  via {self.via:<10}  →  {self.to}"


def make_stages():
    return [
        Stage(
            src="stdlib", via="bootstrap", to=".bin",
            cwd=ROOT / "stdlib",
            cmd=[str(BOOT), "build"],
            pre_wipe=[ROOT / "stdlib" / ".bin"],
        ),
        Stage(
            src="compiler", via="bootstrap", to="stage-2",
            cwd=ROOT / "compiler",
            cmd=[str(BOOT), "build"],
        ),
        Stage(
            src="stdlib", via="stage-2", to=".bin-s2",
            cwd=ROOT / "stdlib",
            cmd=[str(STAGE2), "build", "--build-dir=.bin-s2"],
            pre_wipe=[ROOT / "stdlib" / ".bin-s2"],
        ),
        Stage(
            src="compiler", via="stage-2", to="stage-3",
            cwd=ROOT / "compiler",
            cmd=[str(STAGE2), "build"],
            pre_wipe=[ROOT / "compiler" / "build" / "obj",
                      ROOT / "compiler" / "build" / "bin"],
        ),
        Stage(
            src="stdlib", via="stage-3", to=".bin-s3",
            cwd=ROOT / "stdlib",
            cmd=[str(STAGE3), "build", "--build-dir=.bin-s3"],
            pre_wipe=[ROOT / "stdlib" / ".bin-s3"],
        ),
        Stage(
            src="compiler", via="stage-3", to="stage-4",
            cwd=ROOT / "compiler",
            cmd=[str(STAGE3), "build", "--build-dir=build-s4"],
            pre_wipe=[ROOT / "compiler" / "build-s4"],
        ),
        Stage(
            src="stdlib", via="stage-4", to=".bin-s4",
            cwd=ROOT / "stdlib",
            cmd=[str(STAGE4), "build", "--build-dir=.bin-s4"],
            pre_wipe=[ROOT / "stdlib" / ".bin-s4"],
        ),
        Stage(
            src="compiler", via="stage-4", to="stage-5",
            cwd=ROOT / "compiler",
            cmd=[str(STAGE4), "build", "--build-dir=build-s5"],
            pre_wipe=[ROOT / "compiler" / "build-s5"],
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
    wipe_paths(stage.pre_wipe)

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
def ensure_bootstrap():
    """Build the C++ bootstrap if it's missing. Returns True on success."""
    if BOOT.exists():
        return True
    print(f"{C.YELLOW}{C.BOLD}==> Bootstrap not present at {BOOT.relative_to(ROOT)} — building it first{C.RESET}")
    rc = subprocess.call(["make", "bootstrap"], cwd=str(ROOT))
    if rc != 0:
        print(f"{C.RED}✗ make bootstrap failed (exit {rc}){C.RESET}")
        return False
    return True


def main():
    parser = argparse.ArgumentParser(
        description="Cryo selfhost byte-identity check (8 stages).",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="stream subprocess output to stdout in addition to logs")
    parser.add_argument("--keep-logs", action="store_true",
                        help="keep build-logs/selfhost-check/ from previous runs")
    args = parser.parse_args()

    if not ensure_bootstrap():
        return 2

    print()
    print(f"{C.BOLD}selfhost-check{C.RESET}  {C.DIM}— 8-stage byte-identity gate{C.RESET}")
    print(f"  {C.DIM}root:{C.RESET} {ROOT}")
    print(f"  {C.DIM}logs:{C.RESET} {LOG_DIR.relative_to(ROOT)}/")
    print()

    if not args.keep_logs and LOG_DIR.exists():
        shutil.rmtree(LOG_DIR)
    ensure_dir(LOG_DIR)

    print(f"{C.BOLD}==> Wiping stage outputs{C.RESET}")
    wipe_paths([
        ROOT / "compiler" / "build",
        ROOT / "compiler" / "build-s4",
        ROOT / "compiler" / "build-s5",
        ROOT / "stdlib" / ".bin",
        ROOT / "stdlib" / ".bin-s2",
        ROOT / "stdlib" / ".bin-s3",
        ROOT / "stdlib" / ".bin-s4",
    ])
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
    print(f"{C.BOLD}==> Verifying stage-4 == stage-5 IR byte identity{C.RESET}")

    if not S4_LL.exists() or not S5_LL.exists():
        if not S4_LL.exists():
            print(f"  {C.RED}✗ missing:{C.RESET} {S4_LL.relative_to(ROOT)}")
        if not S5_LL.exists():
            print(f"  {C.RED}✗ missing:{C.RESET} {S5_LL.relative_to(ROOT)}")
        total = time.perf_counter() - total_start
        print_summary(times, total)
        return 1

    s4 = S4_LL.read_bytes()
    s5 = S5_LL.read_bytes()

    if s4 == s5:
        md5 = hashlib.md5(s4).hexdigest()
        print(f"  {C.GREEN}{C.BOLD}✓ FIXED POINT OK{C.RESET}  stage-4 and stage-5 produce byte-identical IR")
        print(f"  {C.DIM}IR md5:{C.RESET}  {md5}")
        print(f"  {C.DIM}IR size:{C.RESET} {len(s4):,} bytes")
        result_ok = True
    else:
        print(f"  {C.RED}{C.BOLD}✗ FIXED POINT BROKEN{C.RESET}  stage-4 and stage-5 IR differ")
        diff = subprocess.run(
            ["diff", "-u", str(S4_LL), str(S5_LL)],
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
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print()
        print(f"{C.YELLOW}interrupted{C.RESET}")
        sys.exit(130)
