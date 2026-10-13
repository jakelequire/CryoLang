#!/usr/bin/env python3
"""verify-pin: assert each committed pin matches its .pin.txt sidecar.

A pinned compiler (bin/cryo, bin/cryo.exe) is only trustworthy if the bytes
on disk are exactly the bytes the sidecar describes. Nothing currently
enforces that: a binary committed without regenerating its sidecar, a
corrupted blob, or a hand-edited sidecar would all go unnoticed until a
confusing downstream build failure. This script is the cheap gate.

For each pin it:
  1. recomputes sha256(binary) and compares it to the sidecar's `sha256:`,
  2. reads the build stamp `cryo stamp` wrote into the binary and asserts its
     `commit` is the sidecar's `git-commit`, so the commit `cryo version`
     reports for a pin is the one its sidecar names,
  3. (optional, --require-clean) asserts the sidecar recorded `worktree: clean`,
     which is what a *release* pin must look like (pins built from a dirty
     worktree are not reproducible from any commit).

The sha256 cannot see a stamp that disagrees with the sidecar: the sidecar is
written from the stamped bytes, so a binary stamped with one commit and pinned
by a sidecar naming another verifies on its hash alone.  The stamp is read
from the bytes rather than by running the pin, so both pins are checked on
either host.

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
    python3 scripts/verify-pin.py --selftest       # drive the checks through
                                                   # fixtures they must refuse

Exit codes:
    0  all checked pins verified
    1  a mismatch / missing-sidecar / stamp disagreement / (with
       --require-clean) dirty pin
"""

import argparse
import contextlib
import hashlib
import io
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_PINS = [ROOT / "bin" / "cryo", ROOT / "bin" / "cryo.exe"]

# The build stamp region (compiler/src/utils/build_stamp.cryo, `StampSlot`):
# a sixteen-byte marker, then `key=value` lines ended by a NUL in a fixed
# 496-byte field, all zero when the binary was never stamped.
STAMP_MARKER = bytes([67, 82, 89, 79, 45, 66, 85, 73, 76, 68, 45, 83, 84, 65, 77, 80])
STAMP_TEXT_LEN = 496


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


def read_stamp(data: bytes) -> tuple[dict, str]:
    """The stamp's fields, or an empty dict and the reason there are none.

    One marker exactly: `cryo stamp` refuses a binary holding two, since it
    cannot tell which region is the compiler's, and so does this."""
    count = data.count(STAMP_MARKER)
    if count != 1:
        return ({}, f"holds {count} stamp regions, not one")
    start = data.index(STAMP_MARKER) + len(STAMP_MARKER)
    text = data[start:start + STAMP_TEXT_LEN].split(b"\0", 1)[0]
    fields = {}
    for line in text.decode("utf-8", "replace").splitlines():
        key, eq, val = line.partition("=")
        if eq and key:
            fields[key] = val
    if not fields.get("commit"):
        return ({}, "was never stamped (its stamp region is empty)")
    return (fields, "")


def verify_stamp(pin: Path, fields: dict) -> bool:
    """The commit stamped into the binary is the commit its sidecar names."""
    stamp, why = read_stamp(pin.read_bytes())
    if not stamp:
        print(f"  [FAIL] {pin.name}: {why}; `make pin` stamps both pins")
        return False
    want = fields.get("git-commit", "")
    if stamp["commit"] != want:
        print(f"  [FAIL] {pin.name}: stamped with a different commit than its sidecar names")
        print(f"           stamp:   {stamp['commit']}")
        print(f"           sidecar: {want or '<absent>'}")
        return False
    return True


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

    if not verify_stamp(pin, fields):
        return (True, False)

    if require_clean:
        worktree = fields.get("worktree", "")
        if worktree != "clean":
            print(f"  [FAIL] {pin.name}: pinned from a {worktree or 'unknown'} worktree "
                  f"(git-describe: {fields.get('git-describe', '?')}); "
                  f"release pins must be built from a clean tree at the release commit")
            return (True, False)

    note = " (clean)" if require_clean else ""
    print(f"  [ ok ] {pin.name}: sha256 and stamp match{note}  {got[:16]}...")
    return (True, True)


def judge(name: str, pin: Path, require_clean: bool, must_pass: bool) -> int:
    """1 when verify_one judges the fixture wrongly, printing what it said;
    0 otherwise, silently, so a passing run shows no [FAIL] lines."""
    said = io.StringIO()
    with contextlib.redirect_stdout(said):
        _, ok = verify_one(pin, require_clean)
    if ok == must_pass:
        return 0
    print(f"-- {name}: expected {'accept' if must_pass else 'refuse'}, "
          f"got {'accept' if ok else 'refuse'}")
    print(said.getvalue(), end="")
    return 1


def selftest() -> int:
    """Drive verify_one through fixtures it must accept and must refuse.

    The failure this guards against is silence: a check that stops comparing
    still prints `ok`.  So every refusal is a fixture that differs from the
    accepted one in the one field that check reads."""
    commit = "6b2997d649dbbbce6adf1c509ddb1dd082347086"
    other = "975faa5d" + commit[8:]

    def region(text: str) -> bytes:
        body = text.encode()
        return STAMP_MARKER + body + b"\0" * (STAMP_TEXT_LEN - len(body))

    def stamped(c: str) -> bytes:
        return region(f"commit={c}\ndate=2026-10-12\ndirty=no\nprofile=release\n")

    cases = [
        # (name, binary bytes, sidecar commit, worktree, require_clean, must pass)
        ("stamp names the sidecar's commit", stamped(commit), commit, "clean", False, True),
        ("clean pin under --require-clean", stamped(commit), commit, "clean", True, True),
        ("stamp names another commit", stamped(other), commit, "clean", False, False),
        ("sidecar has no git-commit", stamped(commit), "", "clean", False, False),
        ("never stamped", region(""), commit, "clean", False, False),
        ("no stamp region", b"no region here", commit, "clean", False, False),
        ("two stamp regions", stamped(commit) + stamped(commit), commit, "clean", False, False),
        ("dirty pin under --require-clean", stamped(commit), commit, "dirty", True, False),
    ]
    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        for name, data, sidecar_commit, worktree, require_clean, must_pass in cases:
            pin = Path(tmp) / "cryo"
            pin.write_bytes(b"\x7fELF" + data + b"\0" * 64)
            lines = [f"sha256:       {sha256_of(pin)}", f"worktree:     {worktree}"]
            if sidecar_commit:
                lines.append(f"git-commit:   {sidecar_commit}")
            sidecar_path(pin).write_text("\n".join(lines) + "\n")
            failures += judge(name, pin, require_clean, must_pass)
        # A hash mismatch is refused before the stamp is read.
        pin = Path(tmp) / "cryo"
        pin.write_bytes(stamped(commit))
        sidecar_path(pin).write_text(f"sha256: {'0' * 64}\ngit-commit: {commit}\n")
        failures += judge("sha256 differs from the sidecar", pin, False, False)
    if failures:
        print(f"verify-pin selftest: FAILED ({failures} case(s) judged wrongly)")
        return 1
    print(f"verify-pin selftest: OK ({len(cases) + 1} cases)")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="Verify pinned compilers match their sidecars.")
    ap.add_argument("--pin", type=Path, action="append",
                    help="pin binary to check (repeatable; default: bin/cryo + bin/cryo.exe)")
    ap.add_argument("--require-clean", action="store_true",
                    help="also require the sidecar to record a clean worktree (release gate)")
    ap.add_argument("--selftest", action="store_true",
                    help="run the checks over fixtures they must accept and refuse")
    args = ap.parse_args()
    if args.selftest:
        return selftest()

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
