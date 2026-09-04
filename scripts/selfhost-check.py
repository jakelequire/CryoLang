#!/usr/bin/env python3
"""
selfhost-check: build the Cryo compiler through 3 rounds (6 stages) and
verify that stage-3 and stage-4 produce byte-identical IR.

The chain is rooted at the pinned `bin/cryo` self-hosted binary, not the
legacy C++ bootstrap. That binary is the canonical entry point for the
new chain (the C++ bootstrap at `legacy/bootstrap/bin/cryo` is retained
for archaeology only, and is no longer required by this script).
Whenever `compiler/src/` adopts new syntax that `bin/cryo` can't parse,
refresh the pin via `make pin` after a clean selfhost-check.

Why 3 rounds and not 4: stage-2 was built by the (potentially older) pin,
so its codegen behavior may differ from the new source's intent. Stage-3
is the first compiler whose codegen comes purely from the current source.
Once stage-3's codegen matches stage-4's codegen, the fixed point is
reached. The previous 4-round chain (stage-4 vs stage-5) added a single
extra safety round; in practice convergence happens at stage-3.

Stage outputs are nested under the regular build dirs to avoid
top-level clutter:

    compiler/build/                 (stage-2 - also what `make cryo` builds)
    compiler/build/self/s3/         (stage-3 compiler)
    compiler/build/self/s4/         (stage-4 compiler - IR-identity gate)
    stdlib/.bin/                    (built by pin - canonical link target)
    stdlib/.bin/self/s2/            (rebuilt by stage-2; smoke-test only)
    stdlib/.bin/self/s3/            (rebuilt by stage-3; smoke-test only)

The `.bin/self/sN` archives are written by their corresponding compiler
stage but never read back - every compiler stage links against the
canonical `<stdlib>/.bin/<triple>/libcryo.a` produced in round 1. Rebuilding
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

# The status glyphs (check / cross / skip marks) are above cp1252's range, so on a Windows
# console with a legacy code page (PowerShell / cmd defaults to cp1252)
# the very first print explodes with UnicodeEncodeError.  Reconfigure
# stdout/stderr to UTF-8 before anything prints.  No-op on POSIX where
# the streams are already UTF-8.
if hasattr(sys.stdout, "reconfigure"):
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, OSError):
        pass
from dataclasses import dataclass, field
from pathlib import Path

# ---------------------------------------------------------------------------
# Paths (resolved from this script's location, NOT the cwd)
# ---------------------------------------------------------------------------
ROOT   = Path(__file__).resolve().parent.parent
BOOT   = ROOT / "bin" / "cryo"                                       # pinned boot
STAGE2 = ROOT / "compiler" / "build"                 / "cryo"      # boot -> stage-2


def target_triple(exe, runner=None, env=None) -> str:
    """The triple a native build with `exe` resolves to.

    Asked of the compiler instead of derived from the host: the value comes
    from a toolchain probe (mingw `gcc` on PATH vs an MSVC linker on Windows,
    whatever libLLVM reports on Linux), so it is not computable here.  A wrong
    directory name is SILENT -- the tier lookup prefers `<dir>/<triple>` and
    falls back to the flat directory, which is where the two chains used to
    overwrite each other's archives.
    """
    cmd = (list(runner) if runner else []) + [str(exe), "version", "--triple"]
    out = subprocess.run(cmd, capture_output=True, text=True,
                         env=env).stdout.strip()
    if not out:
        sys.exit("selfhost-check: `version --triple` returned nothing from "
                 f"{exe}; the pinned compiler predates it.")
    return out
STAGE3 = ROOT / "compiler" / "build" / "self" / "s3" / "cryo"      # stage-2 -> stage-3
STAGE4 = ROOT / "compiler" / "build" / "self" / "s4" / "cryo"      # stage-3 -> stage-4
S3_LL  = ROOT / "compiler" / "build" / "self" / "s3" / "cryo.ll"
S4_LL  = ROOT / "compiler" / "build" / "self" / "s4" / "cryo.ll"
# Stage roots, for the per-module IR tree comparison (`_compare_ir_trees`).
# The `*_LL` files above are the linked artifact only; these are what the
# windows half compares, and what the linux half must compare too.
S3_DIR = ROOT / "compiler" / "build" / "self" / "s3"
S4_DIR = ROOT / "compiler" / "build" / "self" / "s4"
LOG_DIR = ROOT / "build-logs" / "selfhost-check"

# Top-level dirs we wipe before the chain runs. Recursive - covers the
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
    env: dict = None # extra env vars merged over os.environ (windows stages)

    @property
    def label(self):
        return f"{self.src:<8}  via {self.via:<10}  →  {self.to}"


def make_stages():
    host_triple = target_triple(BOOT)
    return [
        # ------------------------------------------------------------------
        # Round 0: runtime tiers.  Every stage from stage-2 on emits a call to
        # the external `__cryo_panic`, so LINKING those compilers needs a panic
        # tier archive; without this round the chain dies at stage-2 on an
        # undefined symbol.  Built by the pin (the only compiler that exists
        # yet) into `.bin/<triple>`, so this chain's archives and the windows
        # chain's coexist instead of whichever ran last owning a flat directory.
        # ------------------------------------------------------------------
        Stage(
            src="runtime", via="pinned", to=f".bin/{host_triple}",
            cwd=ROOT / "runtime",
            cmd=[str(BOOT), "build", "--no-incremental",
                 f"--build-dir=.bin/{host_triple}"],
        ),
        Stage(
            src="runtime", via="pinned", to=f".bin/{host_triple} (hosted)",
            cwd=ROOT / "runtime" / "hosted",
            cmd=[str(BOOT), "build", "--no-incremental",
                 f"--build-dir=../.bin/{host_triple}"],
        ),
        # ------------------------------------------------------------------
        # Round 1: pinned boot -> stage-2  (default build dirs)
        #
        # The stdlib archive goes to `.bin/<triple>`, which is where the link
        # resolves it from.  Writing it flat here would leave every compiler
        # stage linking whatever `make stdlib` last left in the per-target
        # directory instead of the archive this round just produced.
        # ------------------------------------------------------------------
        Stage(
            src="stdlib", via="pinned", to=f".bin/{host_triple}",
            cwd=ROOT / "stdlib",
            cmd=[str(BOOT), "build", "--no-incremental",
                 f"--build-dir=.bin/{host_triple}"],
        ),
        Stage(
            src="compiler", via="pinned", to="build",
            cwd=ROOT / "compiler",
            cmd=[str(BOOT), "build", "--no-incremental"],
        ),
        # ------------------------------------------------------------------
        # Round 2: stage-2 -> stage-3  (nested under self/)
        # ------------------------------------------------------------------
        Stage(
            src="stdlib", via="stage-2", to=".bin/self/s2",
            cwd=ROOT / "stdlib",
            cmd=[str(STAGE2), "build", "--no-incremental", "--build-dir=.bin/self/s2"],
        ),
        Stage(
            src="compiler", via="stage-2", to="build/self/s3",
            cwd=ROOT / "compiler",
            cmd=[str(STAGE2), "build", "--no-incremental", "--build-dir=build/self/s3"],
        ),
        # ------------------------------------------------------------------
        # Round 3: stage-3 -> stage-4  (byte-identity gate)
        # ------------------------------------------------------------------
        Stage(
            src="stdlib", via="stage-3", to=".bin/self/s3",
            cwd=ROOT / "stdlib",
            cmd=[str(STAGE3), "build", "--no-incremental", "--build-dir=.bin/self/s3"],
        ),
        Stage(
            src="compiler", via="stage-3", to="build/self/s4",
            cwd=ROOT / "compiler",
            cmd=[str(STAGE3), "build", "--no-incremental", "--build-dir=build/self/s4"],
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
                env={**os.environ, **(stage.env or {})},
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
# Windows cross-build verification (optional stage)
#
# cryo.exe CAN fully self-host under wine, given a wine-runnable Windows
# toolchain.  Two pieces, both fetched by scripts/fetch-windows-llvm.sh:
#
#   * .toolchains/llvm-win/ - the LLVM-20 C API the compiler calls: the
#     official msvc `LLVM-C.dll` (runtime dep, ships beside cryo.exe) plus a
#     synthesized mingw import lib (`libLLVM-C.dll.a`) to link against.
#   * .toolchains/llvm-mingw/ - mstorsjo's llvm-mingw (a Windows-PE clang +
#     ld.lld + a full mingw-w64 sysroot).  Its `clang.exe` defaults to the
#     `x86_64-w64-windows-gnu` target and finds ld.lld + the sysroot on its
#     own, so - unlike the bare msvc clang.exe, which has no bundled linker - 
#     it LINKS a real cryo.exe under wine with no extra flags.  It serves both
#     the `#include "llvm_bindings.h"` preprocess (CRYO_CC) and the final link;
#     `llvm-ar.exe` is the archiver wine can launch (CRYO_AR).
#
# So the Windows section runs the SAME 6-stage byte-identity self-host as the
# Linux chain, booting from bin/cryo.exe: each stage cross-links a runnable
# cryo.exe (mingw + windows libLLVM-C), runs it under wine to build the next,
# and the stage-3/stage-4 per-module IR is compared for the fixed point.
#
# Skipped (not failed) when wine or either fetched toolchain is absent, so
# Linux-only checkouts and CI without the Windows bits still pass.
# ---------------------------------------------------------------------------
WIN_TRIPLE      = "x86_64-pc-windows-gnu"
WIN_LLVM_LIB    = ROOT / ".toolchains" / "llvm-win" / "lib" / "libLLVM-C.dll.a"
WIN_LLVM_DLL    = ROOT / ".toolchains" / "llvm-win" / "bin" / "LLVM-C.dll"
# libclang import lib + DLL: the C-import engine (Compiler::Bindgen) links
# libclang, so a cryo.exe cross-build links/loads it alongside LLVM-C.
WIN_CLANG_LIB   = ROOT / ".toolchains" / "llvm-win" / "lib" / "libclang.dll.a"
WIN_CLANG_DLL   = ROOT / ".toolchains" / "llvm-win" / "bin" / "libclang.dll"
# clang resource dir (contains include/stddef.h, stdint.h, ...). Passed to the
# wine cryo.exe as CRYO_CLANG_RESOURCE_DIR so libclang resolves the builtin
# headers llvm_bindings.h #includes - the default <dll>/../lib/clang/<v> walk
# fails once libclang.dll is staged next to a per-stage cryo.exe. "20" is the
# clang major (LLVM_WIN_VERSION 20.x in scripts/llvm-version.env).
WIN_CLANG_RESDIR = ROOT / ".toolchains" / "llvm-win" / "lib" / "clang" / "20"
# llvm-mingw's clang.exe + llvm-ar.exe (fetched by fetch-windows-llvm.sh) let
# cryo.exe compile AND LINK the compiler itself under wine: clang.exe is both
# the C preprocessor for the one `extern "C" { #include "llvm_bindings.h" }`
# site and the link driver (CRYO_CC, defaults to windows-gnu + bundled ld.lld),
# llvm-ar.exe is an archiver wine can launch (CRYO_AR).
WIN_MINGW_DIR   = ROOT / ".toolchains" / "llvm-mingw"
WIN_CLANG       = WIN_MINGW_DIR / "bin" / "clang.exe"
WIN_AR          = WIN_MINGW_DIR / "bin" / "llvm-ar.exe"
MINGW_GCC       = "x86_64-w64-mingw32-gcc"
LLVM_LINK       = "llvm-link-20"
# [w4] builds the compiler LIBRARY only (no [[bin]]); a lib-only build under
# wine completes on its own well inside this - the timeout is just a safety net.
W4_BUILD_TIMEOUT = 1800  # s


def _win_prereqs_missing():
    missing = []
    if not shutil.which(MINGW_GCC):
        missing.append(f"{MINGW_GCC} (install gcc-mingw-w64-x86-64)")
    if not shutil.which("wine"):
        missing.append("wine")
    if not shutil.which(LLVM_LINK):
        missing.append(LLVM_LINK)
    if not WIN_LLVM_LIB.exists():
        missing.append("windows libLLVM-C (run scripts/fetch-windows-llvm.sh)")
    if not WIN_CLANG_LIB.exists():
        missing.append("windows libclang (run scripts/fetch-windows-llvm.sh)")
    return missing


def _run(cmd, cwd, log, env=None, allow_fail=False, timeout=None):
    """Run a subprocess, tee to a log file. Returns (rc, combined_output).
    On timeout the child is killed and rc 124 is returned (output captured so
    far is still written to the log)."""
    full_env = dict(os.environ)
    if env:
        full_env.update(env)
    with open(log, "w") as f:
        try:
            p = subprocess.run(cmd, cwd=str(cwd), stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True, env=full_env,
                               timeout=timeout)
            out, rc = p.stdout or "", p.returncode
        except subprocess.TimeoutExpired as e:
            out = e.output if isinstance(e.output, str) else (
                e.output.decode(errors="replace") if e.output else "")
            rc = 124
        f.write(out)
    if rc != 0 and not allow_fail:
        return rc, out
    return rc, out


def _llvm_link(ir_dir_glob_root: Path, out: Path) -> bool:
    """llvm-link every <root>/*/ir/*.ll into a single -S module text."""
    ll_files = sorted(str(p) for p in ir_dir_glob_root.glob("*/ir/*.ll"))
    if not ll_files:
        return False
    r = subprocess.run([LLVM_LINK, "-S", *ll_files, "-o", str(out)],
                       capture_output=True, text=True)
    return r.returncode == 0 and out.exists()


def _llvm_link_named(root: Path, names, out: Path) -> bool:
    """llvm-link a SPECIFIC ordered set of <root>/*/ir/<name> modules."""
    files = []
    for n in names:
        files.extend(sorted(str(p) for p in root.glob(f"*/ir/{n}")))
    if not files:
        return False
    r = subprocess.run([LLVM_LINK, "-S", *files, "-o", str(out)],
                       capture_output=True, text=True)
    return r.returncode == 0 and out.exists()


def _config_without_bin(text: str) -> str:
    """Return `cryoconfig` text with every `[[bin]]` block commented out.

    [w4] compares the compiler LIBRARY both ways.  A full lib+bin build is no
    good for that: the [[bin]] driver is a 2nd whole-program unit that
    re-emits the shared `std__*`/`Compiler__*` modules (injecting the bin's
    own monomorphizations and advancing the global string-id counter), so the
    on-disk lib .ll become bin-contaminated - and unless BOTH sides build the
    bin to the exact same point they won't match.  Dropping [[bin]] makes both
    sides emit clean, identical library IR (and skips the slow bin unit under
    wine).  A block runs from its `[[bin]]` line to the next section header."""
    out, in_bin = [], False
    for ln in text.splitlines():
        s = ln.lstrip()
        if s.startswith("[[bin]]"):
            in_bin = True
            out.append("# " + ln)
            continue
        if in_bin and s.startswith("["):       # next [section] or [[table]]
            in_bin = False
        out.append(("# " + ln) if in_bin else ln)
    return "\n".join(out) + "\n"


# ---------------------------------------------------------------------------
# Windows 6-stage byte-identity self-host
#
# The exact mirror of the Linux chain (make_stages / run_stage / fixed point)
# but booting from bin/cryo.exe.  `runner` prefixes every command: [] runs
# cryo.exe natively (Windows host); ['wine'] runs it under wine (Linux host).
#
# HARD REQUIREMENT: a self-host LINKS a runnable compiler at stages 2/4/6 (each
# stage's cryo.exe builds the next, then we run it).  So the Windows toolchain
# must compile + archive + LINK cryo.exe: a C driver (CRYO_CC), an archiver, a
# linker, and LLVM-C.dll.  wine has no linker, so under wine this needs the
# fetched .toolchains/llvm-win bits and still can't link the bin - it's meant
# to run on a native Windows host.
# ---------------------------------------------------------------------------
WIN_BOOT = ROOT / "bin" / "cryo.exe"

# Per-stage build dirs.  Nested under the same `self/` subtree as the Linux
# stages (which use `self/sN`) so every selfhost artifact lives under
# `build/self` and `.bin/self`; the `win-` name keeps them distinct from the
# Linux `sN` stages, so a Windows host running the Linux chain under WSL into
# the same repo still doesn't collide.
_WIN_STAGE_DIRS = [
    ROOT / "stdlib"   / ".bin"  / "self" / "win-s1",
    ROOT / "compiler" / "build" / "self" / "win-s2",
    ROOT / "stdlib"   / ".bin"  / "self" / "win-s2",
    ROOT / "compiler" / "build" / "self" / "win-s3",
    ROOT / "stdlib"   / ".bin"  / "self" / "win-s3",
    ROOT / "compiler" / "build" / "self" / "win-s4",
]
WIN_S2_EXE = ROOT / "compiler" / "build" / "self" / "win-s2" / "cryo.exe"
WIN_S3_EXE = ROOT / "compiler" / "build" / "self" / "win-s3" / "cryo.exe"
# stage-3 / stage-4 emitted per-module IR; compared for the fixed point.
# Build-dir roots for stages 4 and 6; the per-module IR lives under
# <root>/target/release/<bucket>/*/ir/*.ll for whatever target bucket the
# build resolved to (host-windows for a gnu default, the triple otherwise) - 
# _compare_ir_trees globs across buckets so it doesn't matter which.
WIN_S3_IR  = ROOT / "compiler" / "build" / "self" / "win-s3"
WIN_S4_IR  = ROOT / "compiler" / "build" / "self" / "win-s4"


def make_windows_stages(runner: list, env: dict) -> list:
    """Six windows stages mirroring make_stages(), booting from bin/cryo.exe.
    Compiler stages pass --emit-llvm so the fixed point can compare per-module
    IR (windows builds don't write the linked cryo.ll)."""
    boot, s2, s3 = str(WIN_BOOT), str(WIN_S2_EXE), str(WIN_S3_EXE)

    def build(exe, bdir, emit=False):
        cmd = list(runner) + [exe, "build", "--no-incremental", f"--build-dir={bdir}"]
        if emit:
            cmd.append("--emit-llvm")
        return cmd

    sl, cm = ROOT / "stdlib", ROOT / "compiler"
    rt = ROOT / "runtime"
    # Runtime tiers first, for the same reason as the Linux chain: stage-2
    # onwards link against `__cryo_panic`.  Built with the WINDOWS boot
    # compiler into `.bin/<its triple>`, so this chain's archives and the Linux
    # chain's occupy different directories and neither has to re-establish its
    # own after the other has run.
    wt = target_triple(boot, runner, env)
    tiers = [str(x) for x in (boot, "build", "--no-incremental")]
    return [
        Stage("runtime",  "pinned",  f".bin/{wt}",          rt,            list(runner) + tiers + [f"--build-dir=.bin/{wt}"], env),
        Stage("runtime",  "pinned",  f".bin/{wt} (hosted)", rt / "hosted", list(runner) + tiers + [f"--build-dir=../.bin/{wt}"], env),
        # Round 1 writes the CANONICAL per-target archive, as the Linux chain
        # does, because the compiler stages below link it.  Sending it to
        # `.bin/self/win-s1` instead left this chain silently reusing whatever
        # a previous `make cryo` had put at the canonical path -- so it passed
        # only in a tree that already held a matching archive.
        Stage("stdlib",   "pinned",  f".bin/{wt}",          sl, build(boot, f".bin/{wt}"), env),
        Stage("compiler", "pinned",  "build/self/win-s2", cm, build(boot, "build/self/win-s2"), env),
        Stage("stdlib",   "stage-2", ".bin/self/win-s2",  sl, build(s2,   ".bin/self/win-s2"), env),
        Stage("compiler", "stage-2", "build/self/win-s3", cm, build(s2,   "build/self/win-s3", emit=True), env),
        Stage("stdlib",   "stage-3", ".bin/self/win-s3",  sl, build(s3,   ".bin/self/win-s3"), env),
        Stage("compiler", "stage-3", "build/self/win-s4", cm, build(s3,   "build/self/win-s4", emit=True), env),
    ]


def _drop_llvm_dll_beside(exe: Path):
    """cryo.exe loads LLVM-C.dll AND libclang.dll from its own dir; copy them
    next to a freshly built stage compiler so the next stage can run it."""
    if not exe.exists():
        return
    for name, fallback in (("LLVM-C.dll", WIN_LLVM_DLL), ("libclang.dll", WIN_CLANG_DLL)):
        src = ROOT / "bin" / name
        if not src.exists():
            src = fallback
        if src.exists():
            try:
                shutil.copy2(src, exe.parent / name)
            except OSError:
                pass


def _compare_ir_trees(root_a: Path, root_b: Path):
    """Compare two per-module IR trees (<root>/*/ir/**/*.ll).  Returns
    (True, (nmods, bytes)) on match, (False, first_diff_name) on mismatch, or
    (None, reason) when they can't be compared."""
    # Per-module `.ll` files live in per-namespace SUBDIRECTORIES under `ir/`
    # (e.g. `ir/std/core/error.ll`), so glob recursively and key by the path
    # RELATIVE to `ir/` - a plain basename collides across subdirs
    # (`core/error.ll` vs `io/error.ll` both basename `error.ll`).
    def _key(p: Path) -> str:
        return p.as_posix().rsplit("/ir/", 1)[-1]
    a = {_key(p): p for p in root_a.glob("**/ir/**/*.ll")}
    b = {_key(p): p for p in root_b.glob("**/ir/**/*.ll")}
    if not a or not b:
        return None, "no per-module IR found (was --emit-llvm honored?)"
    if set(a) != set(b):
        return None, f"module sets differ ({len(a)} vs {len(b)})"
    total = 0
    for name in sorted(a):
        ba, bb = a[name].read_bytes(), b[name].read_bytes()
        if ba != bb:
            return False, name
        total += len(ba)
    return True, (len(a), total)


def run_windows_selfhost(runner: list, verbose: bool = False) -> str:
    """Windows section: the 6-stage byte-identity self-host with bin/cryo.exe.
    `runner` is [] (native, Windows host) or ['wine'] (Linux host).  Returns
    (status, reason) with status 'ok' / 'skip' / 'fail'.  The reason travels
    with the status because the run's arm summary has to be readable on its
    own: a summary that says "see the output above" is the kind of gate line
    nobody checks."""
    native = not runner
    print()
    print(f"{C.BOLD}==> Windows 6-stage byte-identity gate "
          f"(boot: bin/cryo.exe, {'native' if native else 'wine'}){C.RESET}")

    if not WIN_BOOT.exists():
        print(f"  {C.YELLOW}↷ skipped{C.RESET} (bin/cryo.exe not present)")
        return ("skip", "bin/cryo.exe not present")

    # Environment per runner.  Native inherits the host PATH/CRYO_CC (set
    # CRYO_CC if your C driver isn't `cc`); wine needs the fetched toolchain.
    if native:
        env = {"CRYO_STDLIB": str(ROOT / "stdlib")}
    else:
        missing = []
        if not shutil.which("wine"):
            missing.append("wine")
        if not (WIN_CLANG.exists() and WIN_AR.exists()
                and WIN_LLVM_DLL.exists() and WIN_CLANG_DLL.exists()):
            missing.append("windows toolchain (.toolchains/llvm-mingw + llvm-win - scripts/fetch-windows-llvm.sh)")
        if missing:
            print(f"  {C.YELLOW}↷ skipped{C.RESET} (can't self-host under wine):")
            for m in missing:
                print(f"      {C.DIM}- {m}{C.RESET}")
            print(f"      {C.DIM}A self-host links a runnable cryo.exe at stages 2/4/6; that needs")
            print(f"      wine + the fetched llvm-mingw (clang/ld.lld) and llvm-win (LLVM-C) bits.{C.RESET}")
            return ("skip", "missing " + ", ".join(missing))
        zwin = lambda p: "Z:" + str(p).replace("/", "\\")
        env = {"WINEDEBUG": "-all",
               "CRYO_STDLIB": "Z:" + str(ROOT / "stdlib").replace("\\", "/"),
               "CRYO_CC": zwin(WIN_CLANG),
               "CRYO_AR": zwin(WIN_AR),
               "CRYO_CLANG_RESOURCE_DIR": zwin(WIN_CLANG_RESDIR)}

    wlog = LOG_DIR / "windows-selfhost"
    ensure_dir(wlog)
    wipe_paths(_WIN_STAGE_DIRS)
    for d in _WIN_STAGE_DIRS:
        d.mkdir(parents=True, exist_ok=True)

    stages = make_windows_stages(runner, env)
    start = time.perf_counter()
    times: list = []
    for i, stage in enumerate(stages, 1):
        ok, elapsed = run_stage(i, len(stages), stage, wlog / f"stage-{i:02d}.log", verbose)
        times.append((stage.label, elapsed, ok))
        if not ok:
            print_summary(times, time.perf_counter() - start)
            print()
            print(f"{C.RED}{C.BOLD}✗ Windows FAILED at stage {i}/{len(stages)}{C.RESET}")
            return ("fail", f"stage {i}/{len(stages)} did not build")
        _drop_llvm_dll_beside(WIN_S2_EXE)
        _drop_llvm_dll_beside(WIN_S3_EXE)

    print()
    print(f"{C.BOLD}==> Verifying windows stage-3 == stage-4 IR byte identity{C.RESET}")
    ok, detail = _compare_ir_trees(WIN_S3_IR, WIN_S4_IR)
    if ok is None:
        print(f"  {C.RED}✗ cannot compare windows IR:{C.RESET} {detail}")
        print(f"     {C.DIM}looked under {WIN_S3_IR.relative_to(ROOT)} and …/self/win-s4{C.RESET}")
        result = ("fail", "windows IR could not be compared")
    elif ok:
        nmods, total = detail
        print(f"  {C.GREEN}{C.BOLD}✓ FIXED POINT OK{C.RESET}  "
              f"windows stage-3 and stage-4 produce byte-identical IR")
        print(f"  {C.DIM}modules:{C.RESET} {nmods}")
        print(f"  {C.DIM}IR size:{C.RESET} {total:,} bytes")
        result = ("ok", f"{nmods} modules byte-identical")
    else:
        print(f"  {C.RED}{C.BOLD}✗ FIXED POINT BROKEN{C.RESET}  first differing module: {detail}")
        result = ("fail", f"first differing module: {detail}")

    print_summary(times, time.perf_counter() - start)
    return result


# ---------------------------------------------------------------------------
# What the run actually verified.
#
# This gate certifies the branch, and it has two arms: the Linux 6-stage chain
# and the Windows one.  Both mapped a SKIPPED arm to exit 0 - so an environment
# without wine ran half the gate and reported success, and "the fixed point
# holds on both hosts" was a claim about a run nobody could tell apart from a
# run that checked one host.  The repo already has the rule this violates, in
# scripts/gate-unavailable.py: a gate has two honest outcomes, it ran and
# passed or it did not pass, and "it could not run here" is the second.
#
# So the arms are counted and named, and a skip is a refusal unless the caller
# declared it.  The declaration is a flag rather than a silent default because
# it then lives at the CALL SITE - visible in the workflow or Makefile a reader
# inspects - instead of inside a mapping nobody reads.
# ---------------------------------------------------------------------------

# An arm that a flag put out of scope for THIS invocation, and which something
# else is accountable for.  Distinct from 'skip', which is an arm that was
# attempted and could not run: the first is a division of labour, the second is
# missing coverage.
DECLINED = "declined"


def finish_arms(arms: list, allow_skipped: bool) -> int:
    """Print what each arm did and decide the run's exit status.

    `arms` is a list of (name, status, reason) with status 'ok' / 'skip' /
    'fail' / DECLINED.  Any failure fails the run; so does any skip, unless
    --allow-skipped-arm was passed."""
    verified = [a for a in arms if a[1] == "ok"]
    skipped  = [a for a in arms if a[1] == "skip"]
    failed   = [a for a in arms if a[1] == "fail"]

    print()
    print(f"{C.BOLD}==> selfhost-check arms{C.RESET}  "
          f"{len(verified)} verified, {len(skipped)} skipped, {len(failed)} failed")
    for name, status, reason in arms:
        if status == "ok":
            mark = f"{C.GREEN}✓ verified{C.RESET}"
        elif status == "fail":
            mark = f"{C.RED}✗ FAILED{C.RESET}"
        elif status == "skip":
            mark = f"{C.YELLOW}↷ SKIPPED{C.RESET}"
        else:
            mark = f"{C.DIM}- not this run's job{C.RESET}"
        print(f"      {name:<9} {mark}  {C.DIM}{reason}{C.RESET}")

    if failed:
        return 1
    if skipped and not allow_skipped:
        print()
        print(f"{C.RED}{C.BOLD}✗ selfhost-check: an arm did not run.{C.RESET}")
        for name, _, reason in skipped:
            print(f"  {C.DIM}{name}: {reason}{C.RESET}")
        print(f"  {C.DIM}A gate that did not run has not passed.  Provide what the arm")
        print(f"  needs, or pass --allow-skipped-arm to record deliberately that this")
        print(f"  environment verifies {len(verified)} of {len(arms)} arms.{C.RESET}")
        return 1
    if skipped:
        print(f"  {C.YELLOW}accepted with --allow-skipped-arm: "
              f"{len(verified)} of {len(arms)} arms verified here.{C.RESET}")
    return 0


# ---------------------------------------------------------------------------
# Windows-host entry point.
#
# The selfhost stages build Linux ELF artifacts rooted at bin/cryo, so they
# can't run natively on Windows.  On a Windows host we re-invoke this script
# inside WSL and stream its output verbatim, so `make selfhost-check` looks
# and behaves like a native Linux run - same header, stage rows, live ticker,
# and fixed-point gate.  The wine-based [w1]-[w4] cross-verify is skipped here
# (--no-windows): it's redundant on Windows and wine doesn't run under the
# non-interactive WSL invocation.  WSL is required.
# ---------------------------------------------------------------------------


def is_windows_host() -> bool:
    """True when this Python is running on a Windows host (native or MSYS2/cygwin)."""
    if os.name == "nt":
        return True
    if sys.platform.startswith(("msys", "cygwin")):
        return True
    return False


def _wsl_path(p: Path) -> str | None:
    """Translate a Windows-native path to its WSL view (e.g. C:\\... -> /mnt/c/...).
    Returns None on failure (wsl.exe absent, WSL distro problem, etc.).

    Backslashes are normalized to forward slashes before the call: wsl.exe
    reassembles its arguments via a Windows-style command line so embedded
    `\\` collapses ('C:\\Programming\\...' arrives as 'C:Programming...');
    forward slashes survive intact and wslpath accepts them."""
    try:
        normalized = str(p).replace("\\", "/")
        r = subprocess.run(["wsl.exe", "--", "wslpath", "-a", normalized],
                           capture_output=True, text=True, timeout=15)
        if r.returncode != 0:
            return None
        out = r.stdout.strip()
        return out or None
    except (FileNotFoundError, subprocess.TimeoutExpired):
        return None


def main_windows(args) -> int:
    """Windows-host entry point: two sections, the same 6-stage self-host.

      1. Linux section - runs inside WSL (the Linux stages build ELF artifacts
         rooted at bin/cryo, which can't run natively on Windows).  Streamed
         verbatim so it looks like a native Linux run.  --no-windows so the WSL
         child doesn't also try the windows section (wine can't, and we run it
         natively below).
      2. Windows section - bin/cryo.exe through the same 6 stages, run NATIVELY
         on this Windows host."""
    if not shutil.which("wsl.exe"):
        print(f"{C.RED}✗ wsl.exe not on PATH{C.RESET}")
        print(f"  {C.DIM}The Windows host runs the Linux selfhost chain through WSL.{C.RESET}")
        print(f"  {C.DIM}Install with 'wsl --install' and a distro that has cryo's toolchain.{C.RESET}")
        return 2
    wsl_root = _wsl_path(ROOT)
    if not wsl_root:
        print(f"{C.RED}✗ could not resolve a WSL path for {ROOT}{C.RESET}")
        print(f"  {C.DIM}Is the repo visible inside your default WSL distro?{C.RESET}")
        return 2

    # 1. Linux section via WSL.  The child prints the Linux experience straight
    #    to this console; --no-windows so it doesn't also run a windows section.
    inner = ("cd '" + wsl_root + "' && python3 scripts/selfhost-check.py --no-windows"
             + (" -v" if args.verbose else "")
             + (" --keep-logs" if args.keep_logs else ""))
    # Flush so our (possibly buffered) stdout doesn't interleave with the child
    # writing directly to the underlying console fd.
    sys.stdout.flush()
    sys.stderr.flush()
    rc = subprocess.run(["wsl.exe", "--", "bash", "-lc", inner]).returncode
    if rc != 0:
        # The child printed its own arms; it is the authority on the Linux one.
        return rc

    # 2. Windows section natively (bin/cryo.exe through the same 6 stages).
    #    This is the host that can run BOTH arms, so it is the one whose green
    #    licenses "the fixed point holds on both hosts" - it must not be
    #    reachable with the windows arm skipped.
    st, why = run_windows_selfhost([], verbose=args.verbose)
    arms = [("linux", "ok", "verified in WSL (see above)"),
            ("windows", st, why)]
    return finish_arms(arms, args.allow_skipped_arm)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def ensure_boot():
    """Bail out if `bin/cryo` is missing - the pin is the entry point."""
    if BOOT.exists():
        return True
    print(f"{C.RED}✗ pinned boot not found:{C.RESET} {BOOT.relative_to(ROOT)}")
    print(f"  {C.DIM}Build a fresh self-hosted compiler and run `make pin`,")
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
    parser.add_argument("--no-windows", action="store_true",
                        help="this invocation is not accountable for the Windows arm "
                             "(the Windows-host entry point runs it natively instead)")
    parser.add_argument("--allow-skipped-arm", action="store_true",
                        help="exit 0 even though an arm could not run here.  Records "
                             "a partial verification deliberately; without it a "
                             "skipped arm fails the gate.")
    args = parser.parse_args()

    # Windows host: native pre-check + WSL Linux chain.  This branch never
    # touches BOOT (bin/cryo, the Linux ELF) directly - it routes the Linux
    # work through WSL, which sees the same repo via /mnt/c/...
    if is_windows_host():
        return main_windows(args)

    if not ensure_boot():
        return 2

    print()
    print(f"{C.BOLD}selfhost-check{C.RESET}  {C.DIM} - 6-stage byte-identity gate (boot: bin/cryo){C.RESET}")
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

    for i, stage in enumerate(stages, 1):
        log_path = LOG_DIR / f"stage-{i:02d}.log"
        ok, elapsed = run_stage(i, len(stages), stage, log_path, args.verbose)
        times.append((stage.label, elapsed, ok))
        if not ok:
            total = time.perf_counter() - total_start
            print_summary(times, total)
            print()
            print(f"{C.RED}{C.BOLD}✗ FAILED at stage {i}/{len(stages)}{C.RESET}")
            return 1

    # All stages passed.  Keep the per-stage [N/6] rows on screen - the run
    # reads the same live and after-the-fact (no collapse).

    # Byte-identity verification
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
        # `cryo.ll` is the llvm-link artifact - one file, ~1MB against ~104MB of
        # per-module IR.  Matching it is necessary but nowhere near sufficient:
        # a name that binds differently in any of the other modules does not
        # show up here.  The windows half has always walked the whole tree; the
        # linux half did not, so for a long time the two halves of this gate
        # were not checking comparable things.  Walk it here too, and make a
        # tree mismatch fail the gate even when the linked artifact matches.
        tree_ok, tree_detail = _compare_ir_trees(S3_DIR, S4_DIR)
        if tree_ok is None:
            print(f"  {C.RED}✗ cannot compare linux IR tree:{C.RESET} {tree_detail}")
            result_ok = False
        elif tree_ok:
            nmods, total_bytes = tree_detail
            print(f"  {C.GREEN}{C.BOLD}✓ FIXED POINT OK{C.RESET}  stage-3 and stage-4 produce byte-identical IR")
            print(f"  {C.DIM}IR md5:{C.RESET}  {md5}  {C.DIM}(cryo.ll, {len(s3):,} bytes){C.RESET}")
            print(f"  {C.DIM}modules:{C.RESET} {nmods}")
            print(f"  {C.DIM}IR size:{C.RESET} {total_bytes:,} bytes")
            result_ok = True
        else:
            print(f"  {C.RED}{C.BOLD}✗ FIXED POINT BROKEN{C.RESET}  "
                  f"linked cryo.ll matches but per-module IR differs; "
                  f"first differing module: {tree_detail}")
            result_ok = False
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

    # Windows section: the same 6-stage self-host with bin/cryo.exe, under wine
    # on this Linux host.  Only attempted once the Linux fixed point holds -
    # a broken Linux arm makes the Windows one uninformative, not optional.
    arms = [("linux", "ok" if result_ok else "fail",
             "stage-3 == stage-4 IR" if result_ok else "fixed point broken")]
    if not result_ok:
        return finish_arms(arms, args.allow_skipped_arm)

    if args.no_windows:
        arms.append(("windows", DECLINED, "--no-windows: run natively by the caller"))
    else:
        st, why = run_windows_selfhost(["wine"], verbose=args.verbose)
        arms.append(("windows", st, why))

    return finish_arms(arms, args.allow_skipped_arm)


if __name__ == "__main__":
    sys.exit(main())
