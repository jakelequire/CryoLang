#!/usr/bin/env python3
"""
cryo-pin: refresh the pinned cryo compiler at bin/cryo and emit a
human-readable metadata sidecar at bin/cryo.pin.txt.

The sidecar captures the binary's SHA256, build timestamp, host info, and
the git state of the worktree at pin time. Committing it alongside the
pinned binary turns each pin refresh into a reviewable diff.

Usage:
    python3 scripts/cryo-pin.py            # (or `make pin-cryo`)
    python3 scripts/cryo-pin.py --no-strip
    python3 scripts/cryo-pin.py --source path/to/cryo
    python3 scripts/cryo-pin.py --pin     path/to/bin/cryo

Exit codes:
    0  pin refreshed
    1  source binary missing or copy/strip failed
"""

import argparse
import datetime as dt
import hashlib
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_SOURCE = ROOT / "compiler" / "build" / "bin" / "cryo"
DEFAULT_PIN = ROOT / "bin" / "cryo"


def git(*args: str) -> str:
    try:
        out = subprocess.run(
            ["git", "-C", str(ROOT), *args],
            check=True,
            capture_output=True,
            text=True,
        )
        return out.stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return "unknown"


# Files that pin-cryo itself writes during a refresh.  Excluded from the
# dirty check below so the pin doesn't record itself as dirty just by
# running.
_PIN_OUTPUTS = (
    ":!bin/cryo",
    ":!bin/cryo.pin.txt",
    ":!bin/cryo.exe",
    ":!bin/cryo.exe.pin.txt",
)


def worktree_dirty() -> bool:
    """True iff anything outside the pin outputs differs from HEAD."""
    try:
        r = subprocess.run(
            ["git", "-C", str(ROOT), "diff-index", "--quiet", "HEAD", "--",
             *_PIN_OUTPUTS],
            capture_output=True,
        )
        return r.returncode != 0
    except FileNotFoundError:
        return False


def sha256_of(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def strip_binary(path: Path, tool: str = "strip") -> bool:
    # A PE/.exe must be stripped with a PE-aware strip (the mingw cross
    # binutils `x86_64-w64-mingw32-strip`); the host GNU `strip` may refuse
    # or corrupt it.  Callers pin `bin/cryo.exe` with --strip-tool.
    if not shutil.which(tool):
        return False
    r = subprocess.run([tool, str(path)], capture_output=True)
    return r.returncode == 0


def write_sidecar(pin: Path, source: Path, stripped: bool) -> Path:
    sidecar = pin.with_suffix(pin.suffix + ".pin.txt") if pin.suffix else pin.parent / (pin.name + ".pin.txt")
    size = pin.stat().st_size
    sha = sha256_of(pin)
    now = dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

    commit = git("rev-parse", "HEAD")
    short = git("rev-parse", "--short", "HEAD")
    branch = git("rev-parse", "--abbrev-ref", "HEAD")
    # Compose the dirty suffix ourselves: `git describe --dirty` checks the
    # whole worktree, but we want to ignore the pin outputs that this run
    # is itself producing.  `worktree_dirty()` excludes those paths.
    is_dirty = worktree_dirty()
    describe_base = git("describe", "--always", "--tags")
    describe = f"{describe_base}-dirty" if is_dirty else describe_base
    user = git("config", "user.name")
    dirty = "dirty" if is_dirty else "clean"

    host = f"{platform.system()} {platform.release()} {platform.machine()}"

    lines = [
        "Cryo pinned compiler",
        "====================",
        "",
        f"binary:       {pin}",
        f"source:       {source}",
        f"size:         {size} bytes",
        f"sha256:       {sha}",
        f"stripped:     {'yes' if stripped else 'no'}",
        "",
        f"built-at:     {now}",
        f"built-by:     {user}",
        f"host:         {host}",
        "",
        f"git-commit:   {commit}",
        f"git-short:    {short}",
        f"git-branch:   {branch}",
        f"git-describe: {describe}",
        f"worktree:     {dirty}",
        "",
    ]
    sidecar.write_text("\n".join(lines))
    return sidecar


def main() -> int:
    ap = argparse.ArgumentParser(description="Refresh bin/cryo and write its .pin.txt sidecar.")
    ap.add_argument("--source", type=Path, default=DEFAULT_SOURCE,
                    help=f"source binary to pin (default: {DEFAULT_SOURCE})")
    ap.add_argument("--pin", type=Path, default=DEFAULT_PIN,
                    help=f"pinned binary path (default: {DEFAULT_PIN})")
    ap.add_argument("--no-strip", action="store_true",
                    help="skip the strip step")
    ap.add_argument("--strip-tool", default="strip",
                    help="strip executable to use (default: strip; for a "
                         "Windows .exe pass x86_64-w64-mingw32-strip)")
    args = ap.parse_args()

    source: Path = args.source
    pin: Path = args.pin

    if not source.exists() or not os.access(source, os.X_OK):
        print(f"ERROR: {source} does not exist or is not executable. "
              f"Run 'make cryo' first.", file=sys.stderr)
        return 1

    print(f"==> Refreshing pinned binary at {pin}")
    pin.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, pin)

    stripped = False
    if not args.no_strip:
        stripped = strip_binary(pin, args.strip_tool)
        if not stripped:
            print(f"    (warning: '{args.strip_tool}' unavailable or failed; "
                  f"binary not stripped)", file=sys.stderr)

    sidecar = write_sidecar(pin, source, stripped)

    size = pin.stat().st_size
    suffix = ", stripped" if stripped else ""
    print(f"==> Pinned: {pin} ({size} bytes{suffix})")
    print(f"==> Wrote:  {sidecar}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
