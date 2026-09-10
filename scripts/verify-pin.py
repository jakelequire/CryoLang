#!/usr/bin/env python3
"""verify-pin: assert each committed pin matches its .pin.txt sidecar.

A pinned compiler (bin/cryo, bin/cryo.exe) is only trustworthy if the bytes
on disk are exactly the bytes the sidecar describes. Nothing currently
enforces that: a binary committed without regenerating its sidecar, a
corrupted blob, or a hand-edited sidecar would all go unnoticed until a
confusing downstream build failure. This script is the cheap gate.

For each pin it:
  1. recomputes sha256(binary) and compares it to the sidecar's `sha256:`,
  2. (optional, --require-clean) asserts the sidecar recorded `worktree: clean`,
     which is what a *release* pin must look like (see M12 - pins built from a
     dirty worktree are not reproducible from any commit).

Then, across the pins, it asserts THE PAIR AGREES: both sidecars must name the
same `git-commit`.  Checking each pin against its own sidecar cannot see the
failure the pins actually have, because a half-finished re-pin regenerates each
sidecar beside the binary it just wrote - so the ELF can come from one commit
and the PE from another with both halves verifying.  `make pin` refreshes both
from one tree in one run; a pair that disagrees means one of them did not
finish, and nothing downstream can tell, because each OS only ever loads its
own half.

A pin whose binary is absent is skipped with a note (a Linux-only checkout
may not carry bin/cryo.exe); a binary present without a sidecar, or a
sidecar without a binary, is an error.

Usage:
    python3 scripts/verify-pin.py                 # sha256 integrity only
    python3 scripts/verify-pin.py --require-clean  # also require clean worktree
    python3 scripts/verify-pin.py --pin bin/cryo   # check one specific pin

Exit codes:
    0  all checked pins verified
    1  a mismatch / missing-sidecar / (with --require-clean) dirty pin
"""

import argparse
import hashlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_PINS = [ROOT / "bin" / "cryo", ROOT / "bin" / "cryo.exe"]


def sha256_of(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def sidecar_path(pin: Path) -> Path:
    # Mirror cryo-pin.py: bin/cryo -> bin/cryo.pin.txt,
    # bin/cryo.exe -> bin/cryo.exe.pin.txt.
    return pin.parent / (pin.name + ".pin.txt")


def parse_sidecar(sidecar: Path) -> dict:
    fields = {}
    for line in sidecar.read_text().splitlines():
        if ":" in line:
            key, _, val = line.partition(":")
            fields[key.strip()] = val.strip()
    return fields


def verify_pair(pins) -> bool:
    """Assert every present pin was built from the same commit.

    Silent when fewer than two pins carry a sidecar: a Linux-only checkout has
    nothing to compare, and a gate that invents a failure there gets switched
    off.  Two present pins that disagree is always a defect.
    """
    seen = {}
    for pin in pins:
        sidecar = sidecar_path(pin)
        if not pin.exists() or not sidecar.exists():
            continue
        seen[pin.name] = parse_sidecar(sidecar).get("git-commit", "") or "<absent>"
    if len(seen) < 2:
        return True
    commits = set(seen.values())
    if len(commits) == 1 and "<absent>" not in commits:
        print(f"  [ ok ] pair: both pins built from {list(commits)[0][:12]}")
        return True
    print("  [FAIL] the pins disagree on the commit they were built from:")
    for name, commit in sorted(seen.items()):
        print(f"           {name:<12} {commit}")
    print("           a re-pin refreshes both from one tree; one of these did")
    print("           not finish, and each half still matches its own sidecar")
    return False


def verify_one(pin: Path, require_clean: bool) -> tuple[bool, bool]:
    """Returns (checked, ok)."""
    sidecar = sidecar_path(pin)
    if not pin.exists() and not sidecar.exists():
        print(f"  [skip] {pin.name}: neither binary nor sidecar present")
        return (False, True)
    if not pin.exists():
        print(f"  [FAIL] {pin.name}: sidecar present but binary missing")
        return (True, False)
    if not sidecar.exists():
        print(f"  [FAIL] {pin.name}: binary present but sidecar {sidecar.name} missing")
        return (True, False)

    fields = parse_sidecar(sidecar)
    want = fields.get("sha256", "")
    got = sha256_of(pin)
    if not want:
        print(f"  [FAIL] {pin.name}: sidecar has no sha256 field")
        return (True, False)
    if got != want:
        print(f"  [FAIL] {pin.name}: sha256 mismatch")
        print(f"           sidecar: {want}")
        print(f"           binary:  {got}")
        return (True, False)

    if require_clean:
        worktree = fields.get("worktree", "")
        if worktree != "clean":
            print(f"  [FAIL] {pin.name}: pinned from a {worktree or 'unknown'} worktree "
                  f"(git-describe: {fields.get('git-describe', '?')}); "
                  f"release pins must be built from a clean tree at the release commit")
            return (True, False)

    note = " (clean)" if require_clean else ""
    print(f"  [ ok ] {pin.name}: sha256 matches{note}  {got[:16]}...")
    return (True, True)


def main() -> int:
    ap = argparse.ArgumentParser(description="Verify pinned compilers match their sidecars.")
    ap.add_argument("--pin", type=Path, action="append",
                    help="pin binary to check (repeatable; default: bin/cryo + bin/cryo.exe)")
    ap.add_argument("--require-clean", action="store_true",
                    help="also require the sidecar to record a clean worktree (release gate)")
    args = ap.parse_args()

    pins = args.pin if args.pin else DEFAULT_PINS
    print("verify-pin - pinned compiler integrity")
    any_checked = False
    all_ok = True
    for pin in pins:
        checked, ok = verify_one(pin, args.require_clean)
        any_checked = any_checked or checked
        all_ok = all_ok and ok

    all_ok = verify_pair(pins) and all_ok

    if not any_checked:
        print("ERROR: no pins found to verify", file=sys.stderr)
        return 1
    if not all_ok:
        print("verify-pin: FAILED")
        return 1
    print("verify-pin: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
