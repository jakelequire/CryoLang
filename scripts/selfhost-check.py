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
STAGE2 = ROOT / "compiler" / "build"                 / "cryo"      # boot → stage-2
STAGE3 = ROOT / "compiler" / "build" / "self" / "s3" / "cryo"      # stage-2 → stage-3
STAGE4 = ROOT / "compiler" / "build" / "self" / "s4" / "cryo"      # stage-3 → stage-4
S3_LL  = ROOT / "compiler" / "build" / "self" / "s3" / "cryo.ll"
S4_LL  = ROOT / "compiler" / "build" / "self" / "s4" / "cryo.ll"
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
            cmd=[str(BOOT), "build", "--no-incremental"],
        ),
        Stage(
            src="compiler", via="pinned", to="build",
            cwd=ROOT / "compiler",
            cmd=[str(BOOT), "build", "--no-incremental"],
        ),
        # ------------------------------------------------------------------
        # Round 2: stage-2 → stage-3  (nested under self/)
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
        # Round 3: stage-3 → stage-4  (byte-identity gate)
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
# cryo.exe cannot fully self-host under wine: it can emit objects/IR but there
# is no linker inside wine, and the compiler's own source imports a C header
# (llvm_bindings.h) that needs a Windows clang to preprocess.  What IS
# verifiable, and what this stage does:
#
#   1. the self-hosted compiler cross-links a real cryo.exe (mingw + windows
#      libLLVM-C),
#   2. that cryo.exe loads + runs under wine (`--version`),
#   3. CROSS-SELFHOST: cryo.exe run under wine reproduces, byte-for-byte, the
#      windows IR the Linux compiler emits for the whole stdlib (131 modules).
#      Both target the MSVC triple - cryo.exe's native default - which lands
#      the two builds in disjoint cache buckets (host-windows vs the explicit
#      triple), so they coexist with no --build-dir juggling.
#
# Skipped (not failed) when the mingw toolchain, wine, llvm-link, or the
# fetched .toolchains/llvm-win import lib are absent, so Linux-only checkouts
# and CI without the Windows bits still pass.
# ---------------------------------------------------------------------------
WIN_TRIPLE      = "x86_64-pc-windows-gnu"
WIN_MSVC_TRIPLE = "x86_64-pc-windows-msvc"
WIN_LLVM_LIB    = ROOT / ".toolchains" / "llvm-win" / "lib" / "libLLVM-C.dll.a"
WIN_LLVM_DLL    = ROOT / ".toolchains" / "llvm-win" / "bin" / "LLVM-C.dll"
# clang.exe + llvm-ar.exe (same msvc tarball, fetched by fetch-windows-llvm.sh)
# let cryo.exe compile the COMPILER itself under wine: clang.exe is the C
# preprocessor for the one `extern "C" { #include "llvm_bindings.h" }` site
# (CRYO_CC), llvm-ar.exe is an archiver wine can launch (CRYO_AR).  Optional:
# [w4] skips cleanly if they're absent.
WIN_CLANG       = ROOT / ".toolchains" / "llvm-win" / "bin" / "clang.exe"
WIN_AR          = ROOT / ".toolchains" / "llvm-win" / "bin" / "llvm-ar.exe"
MINGW_GCC       = "x86_64-w64-mingw32-gcc"
LLVM_LINK       = "llvm-link-20"
# [w4] builds the compiler LIBRARY only (no [[bin]]); a lib-only build under
# wine completes on its own well inside this — the timeout is just a safety net.
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
    on-disk lib .ll become bin-contaminated — and unless BOTH sides build the
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


def run_windows_stage(total_start) -> str:
    """Returns 'ok', 'skip', or 'fail'."""
    print()
    print(f"{C.BOLD}==> Windows cross-build verification{C.RESET}")
    missing = _win_prereqs_missing()
    if missing:
        print(f"  {C.YELLOW}↷ skipped{C.RESET} (prerequisites absent):")
        for m in missing:
            print(f"      {C.DIM}- {m}{C.RESET}")
        return "skip"

    wlog = LOG_DIR / "windows"
    ensure_dir(wlog)
    cryo_exe = ROOT / "compiler" / "build" / "cryo.exe"
    # wine: forward-slash Z: path maps to the unix root; the DLL sits next to
    # the exe so the loader resolves it; silence wine's debug chatter.
    stdlib_unix = ROOT / "stdlib"
    wine_env = {"WINEDEBUG": "-all",
                "CRYO_STDLIB": "Z:" + str(stdlib_unix).replace("\\", "/")}

    # 1. Cross-build cryo.exe via stage-3 (the first pure-source compiler).
    sys.stdout.write(f"  {C.CYAN}[w1]{C.RESET} cross-build cryo.exe ({WIN_TRIPLE})  ")
    sys.stdout.flush()
    rc, _ = _run([str(STAGE3), "build", f"--target={WIN_TRIPLE}", "--no-incremental"],
                 ROOT / "compiler", wlog / "01-cross-build.log")
    if rc != 0 or not cryo_exe.exists():
        print(f"{C.RED}✗ exit {rc}{C.RESET}")
        print(f"     {C.DIM}log:{C.RESET} {(wlog / '01-cross-build.log').relative_to(ROOT)}")
        return "fail"
    shutil.copy2(WIN_LLVM_DLL, cryo_exe.parent / "LLVM-C.dll")
    print(f"{C.GREEN}✓{C.RESET}")

    # 2. Smoke-run under wine.
    sys.stdout.write(f"  {C.CYAN}[w2]{C.RESET} wine cryo.exe --version  ")
    sys.stdout.flush()
    rc, out = _run(["wine", str(cryo_exe), "--version"], ROOT / "compiler",
                   wlog / "02-version.log", env=wine_env, allow_fail=True)
    if "cryo" not in out.lower():
        print(f"{C.RED}✗ (no version output){C.RESET}")
        print(f"     {C.DIM}log:{C.RESET} {(wlog / '02-version.log').relative_to(ROOT)}")
        return "fail"
    print(f"{C.GREEN}✓{C.RESET}  {C.DIM}{out.strip().splitlines()[0] if out.strip() else ''}{C.RESET}")

    # 3. Cross-selfhost: Linux vs wine windows IR for the stdlib must match.
    sys.stdout.write(f"  {C.CYAN}[w3]{C.RESET} stdlib cross-selfhost (linux == wine windows IR)  ")
    sys.stdout.flush()
    sl = ROOT / "stdlib"
    bin_root = sl / ".bin" / "target" / "release"
    # IR_linux: Linux stage-3 emits MSVC-target IR (link bails on the
    # unsupported triple, but --emit-llvm writes the per-module .ll first).
    _run([str(STAGE3), "build", f"--target={WIN_MSVC_TRIPLE}", "--emit-llvm",
          "--no-incremental"], sl, wlog / "03-linux-stdlib.log", allow_fail=True)
    ir_linux = wlog / "ir_linux.ll"
    if not _llvm_link(bin_root / WIN_MSVC_TRIPLE, ir_linux):
        print(f"{C.RED}✗ (no linux windows IR){C.RESET}")
        return "fail"
    # IR_wine: cryo.exe under wine compiles the stdlib for its native target
    # (MSVC) into the host-windows bucket; link fails (no toolchain), IR is
    # written first.
    _run(["wine", str(cryo_exe), "build", "--emit-llvm", "--no-incremental"],
         sl, wlog / "03-wine-stdlib.log", env=wine_env, allow_fail=True)
    ir_wine = wlog / "ir_wine.ll"
    if not _llvm_link(bin_root / "host-windows", ir_wine):
        print(f"{C.RED}✗ (no wine windows IR){C.RESET}")
        print(f"     {C.DIM}log:{C.RESET} {(wlog / '03-wine-stdlib.log').relative_to(ROOT)}")
        return "fail"
    a, b = ir_linux.read_bytes(), ir_wine.read_bytes()
    if a != b:
        print(f"{C.RED}✗ windows IR differs{C.RESET}")
        diff = subprocess.run(["diff", "-u", str(ir_linux), str(ir_wine)],
                              capture_output=True, text=True)
        for line in diff.stdout.splitlines()[:30]:
            print(f"     {C.DIM}│{C.RESET} {line}")
        return "fail"
    md5 = hashlib.md5(a).hexdigest()
    print(f"{C.GREEN}✓{C.RESET}  {C.DIM}IR md5 {md5} ({len(a):,} B){C.RESET}")

    # 4. Compiler cross-selfhost (LIBRARY): cryo.exe under wine reproduces the
    #    Linux compiler's windows IR for the compiler itself.  Unlike the
    #    stdlib, the compiler imports `llvm_bindings.h` via a C-header import,
    #    so cryo.exe under wine needs a C preprocessor (CRYO_CC -> the fetched
    #    clang.exe) and an archiver it can launch (CRYO_AR -> llvm-ar.exe); the
    #    host's clang-20 / ELF ar can't run under wine.  An identical
    #    CRYO_STDLIB *string* on both sides keeps the embedded @FILE.str source
    #    paths matching (wine resolves a /-rooted path against drive Z:).  Both
    #    sides build the LIBRARY only (see _config_without_bin): comparing the
    #    library is what proves the compiler reproduces, and it dodges the
    #    [[bin]] unit's shared-module re-emission (and its slow wine codegen).
    sys.stdout.write(f"  {C.CYAN}[w4]{C.RESET} compiler cross-selfhost (linux == wine windows lib IR)  ")
    sys.stdout.flush()
    if not (WIN_CLANG.exists() and WIN_AR.exists()):
        print(f"{C.YELLOW}↷ skipped{C.RESET} "
              f"{C.DIM}(clang.exe/llvm-ar.exe absent; rerun scripts/fetch-windows-llvm.sh){C.RESET}")
        return "ok"
    cdir = ROOT / "compiler"
    stdlib_same = str(ROOT / "stdlib")          # identical spelling -> matching @FILE.str
    win = lambda p: "Z:" + str(p).replace("/", "\\")
    cfg_path = cdir / "cryoconfig"
    cfg_orig = cfg_path.read_text()
    lin_root = cdir / "build" / "w4-linux" / "target" / "release" / WIN_MSVC_TRIPLE
    win_bdir = cdir / "build" / "w4-wine"
    win_root = win_bdir / "target" / "release" / "host-windows"
    try:
        # Build the LIBRARY only (see _config_without_bin) so neither side is
        # bin-contaminated and both emit identical, clean lib IR.
        cfg_path.write_text(_config_without_bin(cfg_orig))
        # IR_linux: stage-3 emits the lib's windows-msvc IR (link bails on the
        # unsupported triple; .ll written first).  Isolated build-dir so it
        # never clobbers compiler/build/cryo.exe.
        shutil.rmtree(cdir / "build" / "w4-linux", ignore_errors=True)
        _run([str(STAGE3), "build", f"--target={WIN_MSVC_TRIPLE}", "--emit-llvm",
              "--no-incremental", "--build-dir=build/w4-linux"],
             cdir, wlog / "04-linux-compiler.log",
             env={"CRYO_STDLIB": stdlib_same}, allow_fail=True,
             timeout=W4_BUILD_TIMEOUT)
        # IR_wine: cryo.exe under wine, native (msvc) bucket, with the fetched
        # clang.exe (CRYO_CC) + llvm-ar.exe (CRYO_AR).  A lib-only build
        # completes on its own; the timeout is just a safety net.
        shutil.rmtree(win_bdir, ignore_errors=True)
        _run(["wine", str(cryo_exe), "build", "--emit-llvm", "--no-incremental",
              "--build-dir=build/w4-wine"],
             cdir, wlog / "04-wine-compiler.log",
             env={"WINEDEBUG": "-all", "CRYO_STDLIB": stdlib_same,
                  "CRYO_CC": win(WIN_CLANG), "CRYO_AR": win(WIN_AR)},
             allow_fail=True, timeout=W4_BUILD_TIMEOUT)
    finally:
        cfg_path.write_text(cfg_orig)           # always restore cryoconfig
    # The wine archive doubles as proof the CRYO_AR/llvm-ar.exe path works.
    if not (win_bdir / "libcompiler.a").exists():
        print(f"{C.RED}✗ (wine produced no libcompiler.a — CRYO_CC/CRYO_AR path){C.RESET}")
        print(f"     {C.DIM}log:{C.RESET} {(wlog / '04-wine-compiler.log').relative_to(ROOT)}")
        return "fail"
    lin_names = {p.name for p in lin_root.glob("*/ir/*.ll")}
    win_names = {p.name for p in win_root.glob("*/ir/*.ll")}
    if lin_names != win_names or len(win_names) < 100:   # lib is ~155 modules
        print(f"{C.RED}✗ (module sets differ: linux {len(lin_names)}, wine {len(win_names)}){C.RESET}")
        print(f"     {C.DIM}log:{C.RESET} {(wlog / '04-wine-compiler.log').relative_to(ROOT)}")
        return "fail"
    names = sorted(win_names)
    ir_lin_c = wlog / "ir_compiler_linux.ll"
    ir_win_c = wlog / "ir_compiler_wine.ll"
    if not (_llvm_link_named(lin_root, names, ir_lin_c)
            and _llvm_link_named(win_root, names, ir_win_c)):
        print(f"{C.RED}✗ (llvm-link of compiler IR failed){C.RESET}")
        return "fail"
    ca, cb = ir_lin_c.read_bytes(), ir_win_c.read_bytes()
    if ca == cb:
        cmd5 = hashlib.md5(ca).hexdigest()
        print(f"{C.GREEN}✓{C.RESET}  "
              f"{C.DIM}{len(names)} lib modules, IR md5 {cmd5} ({len(ca):,} B){C.RESET}")
        return "ok"
    print(f"{C.RED}✗ compiler windows IR differs{C.RESET}")
    diff = subprocess.run(["diff", "-u", str(ir_lin_c), str(ir_win_c)],
                          capture_output=True, text=True)
    for line in diff.stdout.splitlines()[:30]:
        print(f"     {C.DIM}│{C.RESET} {line}")
    return "fail"


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
    parser.add_argument("--no-windows", action="store_true",
                        help="skip the optional Windows cross-build verification stage")
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

    # Optional Windows cross-build verification.  Only runs once the Linux
    # fixed point holds (it builds on stage-3); skips cleanly when the
    # Windows toolchain isn't present, so it never breaks a Linux-only run.
    if result_ok and not args.no_windows:
        win = run_windows_stage(total_start)
        if win == "fail":
            return 1

    return 0 if result_ok else 1


if __name__ == "__main__":
    sys.exit(main())
