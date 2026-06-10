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

# The status glyphs (✓ ✗ ↷) are above cp1252's range, so on a Windows
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
#      Both target the GNU triple - cryo.exe's native default (its
#      get_default_triple() coerces LLVM's msvc default to the mingw ABI the
#      port links against).  The Linux side names it explicitly (its own
#      `x86_64-pc-windows-gnu` bucket); the wine side gets it by default (the
#      `host-windows` bucket).  Disjoint buckets, identical triples, so they
#      coexist with no --build-dir juggling.
#
# Skipped (not failed) when the mingw toolchain, wine, llvm-link, or the
# fetched .toolchains/llvm-win import lib are absent, so Linux-only checkouts
# and CI without the Windows bits still pass.
# ---------------------------------------------------------------------------
WIN_TRIPLE      = "x86_64-pc-windows-gnu"
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


def run_win_phase(tag: str, title: str, fn) -> tuple:
    """Run one Windows phase as a live-ticking, Linux-style status row.

    `fn` does its work silently and returns `(status, detail, extra)` where
    `status` is 'ok' | 'skip' | 'fail', `detail` is a short trailing note (an
    IR md5, a version string, a failure reason), and `extra` is a list of
    already-formatted lines to print under the row (log paths, diff snippets).
    Mirrors `run_stage`: a 0.5 s ticker on a TTY, then the verdict + duration.
    Returns `(status, elapsed, label)`."""
    prefix = f"  {C.CYAN}[{tag}]{C.RESET} {title}  "
    sys.stdout.write(prefix)
    sys.stdout.flush()
    is_tty = sys.stdout.isatty()
    start = time.perf_counter()
    stop = threading.Event()

    def ticker():
        while not stop.wait(0.5):
            el = time.perf_counter() - start
            sys.stdout.write(f"\r{prefix}{C.DIM}{fmt_dur(el)}{C.RESET}")
            sys.stdout.flush()

    t = threading.Thread(target=ticker, daemon=True) if is_tty else None
    if t:
        t.start()
    try:
        status, detail, extra = fn()
    except Exception as e:                       # one phase must never abort the rest
        status, detail, extra = "fail", f"exception: {e}", []
    stop.set()
    if t:
        t.join(timeout=1.0)
    el = time.perf_counter() - start

    mark = {"ok":   f"{C.GREEN}✓{C.RESET}",
            "skip": f"{C.YELLOW}↷ skipped{C.RESET}",
            "fail": f"{C.RED}✗{C.RESET}"}.get(status, status)
    line_start = f"\r{prefix}" if is_tty else ""
    note = f"  {C.DIM}{detail}{C.RESET}" if detail else ""
    sys.stdout.write(f"{line_start}{mark}  {C.DIM}{fmt_dur(el):>7s}{C.RESET}{note}{' ' * 4}\n")
    sys.stdout.flush()
    for ln in (extra or []):
        print(ln)
    return status, el, f"[{tag}] {title}"


def run_windows_stage(total_start) -> str:
    """Cross-build verification, rendered as timed [wN] rows like the Linux
    stages.  Every phase reports its own verdict (a failing phase no longer
    aborts the rest); phases that need cryo.exe are skipped if [w1] fails.
    Returns 'ok', 'skip', or 'fail'.

    Triple note: cryo.exe's own default target is windows-GNU (its
    `get_default_triple()` coerces LLVM's msvc default to the mingw ABI the
    port links against).  So BOTH sides build for GNU here — the Linux side
    explicitly (`--target`, landing in its own `x86_64-pc-windows-gnu`
    bucket), the wine side by default (the `host-windows` bucket).  Disjoint
    buckets, identical triples — the IR matches byte-for-byte."""
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

    results: list = []                           # (label, elapsed, status)
    _skip_no_exe = lambda: ("skip", "no cryo.exe (w1 failed)", [])

    def _log(name):
        return f"     {C.DIM}log:{C.RESET} {(wlog / name).relative_to(ROOT)}"

    # ---- [w1] cross-build cryo.exe via stage-3 (first pure-source compiler).
    def phase_w1():
        rc, _ = _run([str(STAGE3), "build", f"--target={WIN_TRIPLE}", "--no-incremental"],
                     ROOT / "compiler", wlog / "01-cross-build.log")
        if rc != 0 or not cryo_exe.exists():
            return "fail", f"exit {rc}", [_log("01-cross-build.log")]
        shutil.copy2(WIN_LLVM_DLL, cryo_exe.parent / "LLVM-C.dll")
        return "ok", "", []
    st, el, lab = run_win_phase("w1", f"cross-build cryo.exe ({WIN_TRIPLE})", phase_w1)
    results.append((lab, el, st))
    have_exe = st == "ok"

    # ---- [w2] smoke-run under wine.
    def phase_w2():
        rc, out = _run(["wine", str(cryo_exe), "--version"], ROOT / "compiler",
                       wlog / "02-version.log", env=wine_env, allow_fail=True)
        if "cryo" not in out.lower():
            return "fail", "no version output", [_log("02-version.log")]
        first = out.strip().splitlines()[0] if out.strip() else ""
        return "ok", first, []
    st, el, lab = run_win_phase("w2", "wine cryo.exe --version",
                                phase_w2 if have_exe else _skip_no_exe)
    results.append((lab, el, st))

    # ---- [w3] stdlib cross-selfhost: linux GNU IR == wine GNU IR.
    def phase_w3():
        sl = ROOT / "stdlib"
        bin_root = sl / ".bin" / "target" / "release"
        # Linux stage-3 emits the stdlib's windows-GNU IR into its own
        # `x86_64-pc-windows-gnu` bucket (link bails on the cross triple, but
        # --emit-llvm writes the per-module .ll first).
        _run([str(STAGE3), "build", f"--target={WIN_TRIPLE}", "--emit-llvm",
              "--no-incremental"], sl, wlog / "03-linux-stdlib.log", allow_fail=True)
        ir_linux = wlog / "ir_linux.ll"
        if not _llvm_link(bin_root / WIN_TRIPLE, ir_linux):
            return "fail", "no linux windows IR", [_log("03-linux-stdlib.log")]
        # cryo.exe under wine compiles the stdlib for its default (GNU) target
        # into the host-windows bucket; link fails (no toolchain), IR first.
        _run(["wine", str(cryo_exe), "build", "--emit-llvm", "--no-incremental"],
             sl, wlog / "03-wine-stdlib.log", env=wine_env, allow_fail=True)
        ir_wine = wlog / "ir_wine.ll"
        if not _llvm_link(bin_root / "host-windows", ir_wine):
            return "fail", "no wine windows IR", [_log("03-wine-stdlib.log")]
        a, b = ir_linux.read_bytes(), ir_wine.read_bytes()
        if a != b:
            diff = subprocess.run(["diff", "-u", str(ir_linux), str(ir_wine)],
                                  capture_output=True, text=True)
            extra = [f"     {C.DIM}│{C.RESET} {ln}" for ln in diff.stdout.splitlines()[:30]]
            return "fail", "windows IR differs", extra
        md5 = hashlib.md5(a).hexdigest()
        return "ok", f"IR md5 {md5} ({len(a):,} B)", []
    st, el, lab = run_win_phase("w3", "stdlib cross-selfhost (linux == wine windows IR)",
                                phase_w3 if have_exe else _skip_no_exe)
    results.append((lab, el, st))

    # ---- [w4] compiler-LIBRARY cross-selfhost (needs clang.exe + llvm-ar.exe).
    def phase_w4():
        if not (WIN_CLANG.exists() and WIN_AR.exists()):
            return ("skip",
                    "clang.exe/llvm-ar.exe absent (rerun scripts/fetch-windows-llvm.sh)", [])
        cdir = ROOT / "compiler"
        stdlib_same = str(ROOT / "stdlib")          # identical spelling -> matching @FILE.str
        win = lambda p: "Z:" + str(p).replace("/", "\\")
        cfg_path = cdir / "cryoconfig"
        cfg_orig = cfg_path.read_text()
        lin_root = cdir / "build" / "w4-linux" / "target" / "release" / WIN_TRIPLE
        win_bdir = cdir / "build" / "w4-wine"
        win_root = win_bdir / "target" / "release" / "host-windows"
        try:
            # Library only (see _config_without_bin) so neither side is
            # bin-contaminated and both emit identical, clean lib IR.
            cfg_path.write_text(_config_without_bin(cfg_orig))
            shutil.rmtree(cdir / "build" / "w4-linux", ignore_errors=True)
            _run([str(STAGE3), "build", f"--target={WIN_TRIPLE}", "--emit-llvm",
                  "--no-incremental", "--build-dir=build/w4-linux"],
                 cdir, wlog / "04-linux-compiler.log",
                 env={"CRYO_STDLIB": stdlib_same}, allow_fail=True,
                 timeout=W4_BUILD_TIMEOUT)
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
            return ("fail", "wine produced no libcompiler.a (CRYO_CC/CRYO_AR path)",
                    [_log("04-wine-compiler.log")])
        lin_names = {p.name for p in lin_root.glob("*/ir/*.ll")}
        win_names = {p.name for p in win_root.glob("*/ir/*.ll")}
        if lin_names != win_names or len(win_names) < 100:   # lib is ~155 modules
            return ("fail", f"module sets differ: linux {len(lin_names)}, wine {len(win_names)}",
                    [_log("04-wine-compiler.log")])
        names = sorted(win_names)
        ir_lin_c = wlog / "ir_compiler_linux.ll"
        ir_win_c = wlog / "ir_compiler_wine.ll"
        if not (_llvm_link_named(lin_root, names, ir_lin_c)
                and _llvm_link_named(win_root, names, ir_win_c)):
            return "fail", "llvm-link of compiler IR failed", []
        ca, cb = ir_lin_c.read_bytes(), ir_win_c.read_bytes()
        if ca != cb:
            diff = subprocess.run(["diff", "-u", str(ir_lin_c), str(ir_win_c)],
                                  capture_output=True, text=True)
            extra = [f"     {C.DIM}│{C.RESET} {ln}" for ln in diff.stdout.splitlines()[:30]]
            return "fail", "compiler windows IR differs", extra
        cmd5 = hashlib.md5(ca).hexdigest()
        return "ok", f"{len(names)} lib modules, IR md5 {cmd5} ({len(ca):,} B)", []
    st, el, lab = run_win_phase("w4", "compiler cross-selfhost (linux == wine windows lib IR)",
                                phase_w4 if have_exe else _skip_no_exe)
    results.append((lab, el, st))

    # ---- Windows phase summary (mirrors the Linux Stage-timings panel). -----
    print()
    print(f"{C.BOLD}Windows phase timings{C.RESET}")
    print(f"  {C.DIM}{hr(72)}{C.RESET}")
    for lab, el, stt in results:
        m = {"ok": f"{C.GREEN}✓{C.RESET}", "skip": f"{C.YELLOW}↷{C.RESET}",
             "fail": f"{C.RED}✗{C.RESET}"}.get(stt, "?")
        pad = max(0, 56 - len(lab))
        print(f"  {m}  {lab}{' ' * pad}{C.DIM}{fmt_dur(el):>10s}{C.RESET}")
    print(f"  {C.DIM}{hr(72)}{C.RESET}")

    if any(s == "fail" for _, _, s in results):
        return "fail"
    if all(s == "skip" for _, _, s in results):
        return "skip"
    return "ok"


# ---------------------------------------------------------------------------
# Windows-host pre-check (cryo.exe + WSL delegation)
#
# When invoked on a Windows host, selfhost-check runs three things in order:
#
#   1. A native cryo.exe smoke (--version) — proves the binary loads and
#      LLVM-C.dll resolves.
#   2. A native cryo.exe stdlib build (default triple) — proves the
#      windows-msvc → windows-gnu coercion fix actually links end-to-end.
#   3. A native cryo.exe single-file hello build + run with fmt::println
#      varargs — proves codegen + link + the Win64 va_list path work.
#   4. The full 6-stage Linux byte-identity chain via WSL.  Wine-based
#      Windows verification (the original [w1]-[w4]) is skipped because we
#      already verified cryo.exe natively above.
#
# WSL is required (the Linux chain doesn't run natively on Windows).
# ---------------------------------------------------------------------------
WIN_BOOT = ROOT / "bin" / "cryo.exe"


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


def _quick_stage(label: str, log_name: str, cmd, cwd, env=None,
                 allow_fail: bool = False, timeout=None,
                 expect_substring: str | None = None,
                 wlog: Path | None = None) -> tuple[bool, str]:
    """Run a single stage, render its row, return (ok, captured_output).

    `expect_substring` (case-insensitive) lets a "smoke" call assert that the
    child's stdout/stderr contains something specific (e.g. "cryo" for --version)."""
    assert wlog is not None
    sys.stdout.write(label)
    sys.stdout.flush()
    rc, out = _run(cmd, cwd, wlog / log_name, env=env,
                   allow_fail=True, timeout=timeout)
    if rc != 0:
        sys.stdout.write(f"{C.RED}✗ exit {rc}{C.RESET}\n")
        print(f"     {C.DIM}log:{C.RESET} {(wlog / log_name).relative_to(ROOT)}")
        return False, out
    if expect_substring and expect_substring.lower() not in (out or "").lower():
        sys.stdout.write(f"{C.RED}✗ unexpected output{C.RESET}\n")
        print(f"     {C.DIM}log:{C.RESET} {(wlog / log_name).relative_to(ROOT)}")
        return False, out
    sys.stdout.write(f"{C.GREEN}✓{C.RESET}\n")
    return True, out


def main_windows(args) -> int:
    """Windows-host entry point: native cryo.exe pre-check + WSL Linux chain."""
    print()
    print(f"{C.BOLD}selfhost-check{C.RESET}  "
          f"{C.DIM}— Windows host (cryo.exe pre-check + Linux chain via WSL){C.RESET}")
    print(f"  {C.DIM}root:{C.RESET} {ROOT}")
    print(f"  {C.DIM}logs:{C.RESET} {LOG_DIR.relative_to(ROOT)}/")
    print()

    # Prereqs.
    if not WIN_BOOT.exists():
        print(f"{C.RED}✗ pinned cryo.exe not found:{C.RESET} {WIN_BOOT.relative_to(ROOT)}")
        print(f"  {C.DIM}Refresh with `make pin`, or check out a revision that has bin/cryo.exe committed.{C.RESET}")
        return 2
    if not shutil.which("wsl.exe"):
        print(f"{C.RED}✗ wsl.exe not on PATH{C.RESET}")
        print(f"  {C.DIM}The Windows host flow needs WSL for the Linux 6-stage chain.{C.RESET}")
        print(f"  {C.DIM}Install with 'wsl --install' and a Linux distro that has cryo's toolchain.{C.RESET}")
        return 2
    wsl_root = _wsl_path(ROOT)
    if not wsl_root:
        print(f"{C.RED}✗ could not resolve a WSL path for {ROOT}{C.RESET}")
        print(f"  {C.DIM}Is the repo visible inside your default WSL distro?{C.RESET}")
        return 2

    if not args.keep_logs and LOG_DIR.exists():
        shutil.rmtree(LOG_DIR)
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    wlog = LOG_DIR / "windows-host"
    wlog.mkdir(parents=True, exist_ok=True)

    overall_start = time.perf_counter()

    # Phase 1: Windows-native pre-check.
    print(f"{C.BOLD}==> Windows-native pre-check (bin/cryo.exe){C.RESET}")

    # [W1] cryo.exe --version smoke.
    label = f"  {C.CYAN}[W1]{C.RESET} cryo.exe --version  "
    ok, out = _quick_stage(label, "01-version.log",
                           [str(WIN_BOOT), "--version"], ROOT,
                           expect_substring="cryo", wlog=wlog)
    if not ok:
        return 1
    version_line = (out or "").strip().splitlines()[0] if (out or "").strip() else ""
    print(f"     {C.DIM}{version_line}{C.RESET}")

    # Wipe stdlib/.bin/ so we measure a clean Windows-native build.  We don't
    # need an archive from this run (the cryo.exe link path through the Win
    # cross-toolchain uses host-windows/ bucket objects, not the stdlib
    # archive — handy because the Linux chain rebuilds libcryo.a anyway).
    wipe_paths([ROOT / "stdlib" / ".bin"])

    # [W2] cryo.exe builds the stdlib at the default (gnu-coerced) triple.
    # This exercises the windows-msvc → windows-gnu fix end-to-end: link
    # produces an archive without unresolved __chkstk references.
    label = f"  {C.CYAN}[W2]{C.RESET} cryo.exe build stdlib (native, no --target)  "
    ok, _ = _quick_stage(label, "02-stdlib.log",
                         [str(WIN_BOOT), "build", "--no-incremental"],
                         ROOT / "stdlib", wlog=wlog)
    if not ok:
        return 1
    if not (ROOT / "stdlib" / ".bin" / "libcryo.a").exists():
        print(f"  {C.RED}✗ stdlib build produced no libcryo.a{C.RESET}")
        return 1

    # [W3] cryo.exe builds + runs a fmt::println program: verifies codegen,
    # link, the Win64 va_list seam, and the new gnu-default linker config.
    smoke_dir = ROOT / "build-logs" / "selfhost-check" / "windows-host" / "smoke"
    smoke_dir.mkdir(parents=True, exist_ok=True)
    (smoke_dir / "main.cryo").write_text(
        'namespace WinSmoke;\n'
        'import std::fmt;\n'
        'function main() -> int {\n'
        '    fmt::println("smoke ok: %d %d %s", 1, 2, "yes");\n'
        '    return 0;\n'
        '}\n'
    )
    (smoke_dir / "cryoconfig").write_text(
        '[project]\n'
        'project_name = "winsmoke"\n'
        'output_dir = "build"\n'
        '[[bin]]\n'
        'name = "winsmoke"\n'
        'entry_point = "main.cryo"\n'
        '[compiler]\n'
        'optimize = O2\n'
    )
    shutil.rmtree(smoke_dir / "build", ignore_errors=True)
    # The smoke project sits under build-logs/, far from the repo's stdlib/,
    # so neither the project-relative nor cwd-relative fallback in
    # ProjectConfig::resolve_stdlib_root would find it.  Pin it explicitly.
    smoke_env = {"CRYO_STDLIB": str(ROOT / "stdlib")}
    label = f"  {C.CYAN}[W3]{C.RESET} cryo.exe build + run hello (fmt varargs)  "
    ok, _ = _quick_stage(label, "03-smoke-build.log",
                         [str(WIN_BOOT), "build", "--no-incremental"],
                         smoke_dir, env=smoke_env, wlog=wlog)
    if not ok:
        return 1
    smoke_exe = smoke_dir / "build" / "winsmoke.exe"
    if not smoke_exe.exists():
        print(f"  {C.RED}✗ build produced no {smoke_exe.relative_to(ROOT)}{C.RESET}")
        return 1
    rc, out = _run([str(smoke_exe)], smoke_dir, wlog / "03-smoke-run.log",
                   allow_fail=True)
    if rc != 0 or "smoke ok: 1 2 yes" not in (out or ""):
        print(f"  {C.RED}✗ smoke binary exit {rc}, output:{C.RESET} "
              f"{(out or '').strip()!r}")
        return 1
    print(f"     {C.DIM}{(out or '').strip()}{C.RESET}")

    win_elapsed = time.perf_counter() - overall_start
    print(f"  {C.GREEN}✓ Windows pre-check OK{C.RESET}  {C.DIM}({fmt_dur(win_elapsed)}){C.RESET}")

    # Phase 2: Linux 6-stage chain via WSL.
    # --no-windows skips the wine-based Windows verification — we already
    # verified cryo.exe natively above.
    print()
    print(f"{C.BOLD}==> Linux 6-stage chain (via WSL){C.RESET}")
    print(f"  {C.DIM}wsl.exe -- bash -lc 'cd {wsl_root} && python3 scripts/selfhost-check.py --no-windows'{C.RESET}")
    print()
    # Flush before handing stdout to the child — Python's stdout is line/
    # block-buffered while the WSL child writes directly to the underlying
    # fd, so without an explicit flush the child's output starts appearing
    # in the middle of our buffered prints above.
    sys.stdout.flush()
    sys.stderr.flush()
    wsl_cmd = ["wsl.exe", "--", "bash", "-lc",
               f"cd '{wsl_root}' && python3 scripts/selfhost-check.py --no-windows"
               + (" -v" if args.verbose else "")
               + (" --keep-logs" if args.keep_logs else "")]
    # Stream WSL stdout/stderr live so the user sees the per-stage rows.
    proc = subprocess.run(wsl_cmd)
    if proc.returncode != 0:
        return proc.returncode

    total = time.perf_counter() - overall_start
    print()
    print(f"{C.GREEN}{C.BOLD}✓ ALL CHECKS PASSED{C.RESET}  {C.DIM}(total {fmt_dur(total)}){C.RESET}")
    return 0


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def ensure_boot():
    """Bail out if `bin/cryo` is missing — the pin is the entry point."""
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
                        help="skip the optional Windows cross-build verification stage")
    args = parser.parse_args()

    # Windows host: native pre-check + WSL Linux chain.  This branch never
    # touches BOOT (bin/cryo, the Linux ELF) directly — it routes the Linux
    # work through WSL, which sees the same repo via /mnt/c/...
    if is_windows_host():
        return main_windows(args)

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
