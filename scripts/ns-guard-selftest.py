#!/usr/bin/env python3
"""Exercise scripts/ns-commit-guard.py against real commits, and show it refuse.

A gate nobody has watched fail is a decoration, and the guard is the awkward
kind to demonstrate: proving it works means making commits, and the repository
it guards is shared with other agents whose index must not be disturbed.  So
this builds a throwaway git repository in a temp directory, stands up a
miniature ledger with a §0 of its own, and drives the guard through every rule
in both directions - the commit it must refuse, and the commit it must let
through.

That makes the proof reproducible instead of a paragraph in a commit message.
Run it after touching the guard:

    python scripts/ns-guard-selftest.py

Exit codes: 0 every case behaved as stated; 1 any case did not.
"""
import io
import os
import shutil
import subprocess
import sys
import tempfile

if hasattr(sys.stdout, "reconfigure"):
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, OSError):
        pass

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROWS = 26          # at or above ns-status-check's --min-rows floor


def ledger(rows, extra_entries=""):
    """A miniature ledger: a §0 whose rows check `data.txt`, plus a §8."""
    lines = [
        "# Name Resolution - fixture",
        "",
        "## 0. Current state",
        "",
        "### 0.1 Decisions",
        "",
        "| # | decision | status | check -> expected |",
        "|---|---|---|---|",
    ]
    for i in range(1, ROWS + 1):
        want = rows.get(i, 1)
        lines.append("| D%d | fixture row %d | TAKEN | `grep -c 'ROW%d;' data.txt` "
                     "→ **%d** |" % (i, i, i, want))
    lines += ["", "## 8. Archive", "", extra_entries, ""]
    return "\n".join(lines) + "\n"


def data(counts):
    """`data.txt`, carrying each row's marker as many times as `counts` says."""
    out = []
    for i in range(1, ROWS + 1):
        out += ["ROW%d;" % i] * counts.get(i, 1)
    return "\n".join(out) + "\n"


class Repo(object):
    def __init__(self, path):
        self.path = path

    def git(self, *args, **kw):
        return subprocess.run(["git"] + list(args), cwd=self.path,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, **kw)

    def write(self, rel, text):
        full = os.path.join(self.path, rel)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        io.open(full, "w", encoding="utf-8", newline="\n").write(text)

    def guard(self, message):
        msg = os.path.join(self.path, "COMMIT_MSG")
        io.open(msg, "w", encoding="utf-8", newline="\n").write(message)
        r = subprocess.run(
            [sys.executable, os.path.join(self.path, "scripts", "ns-commit-guard.py"),
             "--message-file", msg],
            cwd=self.path, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True)
        return r.returncode, r.stdout


def build(path):
    repo = Repo(path)
    repo.git("init", "-q")
    repo.git("config", "user.email", "selftest@example.invalid")
    repo.git("config", "user.name", "selftest")
    for name in ("ns-commit-guard.py", "ns-status-check.py"):
        os.makedirs(os.path.join(path, "scripts"), exist_ok=True)
        shutil.copy2(os.path.join(ROOT, "scripts", name),
                     os.path.join(path, "scripts", name))
    hooks = os.path.join(path, "scripts", "git-hooks")
    os.makedirs(hooks, exist_ok=True)
    shutil.copy2(os.path.join(ROOT, "scripts", "git-hooks", "commit-msg"),
                 os.path.join(hooks, "commit-msg"))
    repo.git("config", "core.hooksPath", "scripts/git-hooks")
    repo.write("data.txt", data({}))
    repo.write("src/thing.txt", "one\n")
    repo.write("docs/name-resolution.md",
               ledger({}, "### 8.100 A first entry - MEASURED AND FIXED 2026-01-01\n"))
    repo.git("add", "-A")
    repo.git("commit", "-qm", "fixture")
    return repo


CASES = []


def case(name, expect_refused):
    def deco(fn):
        CASES.append((name, expect_refused, fn))
        return fn
    return deco


@case("the ledger staged on its own", True)
def _(repo):
    repo.write("docs/name-resolution.md",
               ledger({}, "### 8.100 A first entry - MEASURED AND FIXED 2026-01-01\n"
                          "\nsome prose added on its own\n"))
    repo.git("add", "docs/name-resolution.md")
    return repo.guard("Tidy the ledger\n")


@case("a LANDED entry that does not move §0", True)
def _(repo):
    repo.write("src/thing.txt", "two\n")
    repo.write("docs/name-resolution.md",
               ledger({}, "### 8.100 A first entry - MEASURED AND FIXED 2026-01-01\n"
                          "\n### 8.101 A second entry - MEASURED AND LANDED 2026-01-02\n"))
    repo.git("add", "-A")
    return repo.guard("Land the second thing\n")


@case("the same commit, with the waiver line", False)
def _(repo):
    repo.write("src/thing.txt", "two\n")
    repo.write("docs/name-resolution.md",
               ledger({}, "### 8.100 A first entry - MEASURED AND FIXED 2026-01-01\n"
                          "\n### 8.101 A second entry - MEASURED AND LANDED 2026-01-02\n"))
    repo.git("add", "-A")
    return repo.guard("Land the second thing\n\n"
                      "no-section-0: it renames a local, no row describes it\n")


@case("a DEFERRED entry, which owes §0 nothing", False)
def _(repo):
    repo.write("src/thing.txt", "two\n")
    repo.write("docs/name-resolution.md",
               ledger({}, "### 8.100 A first entry - MEASURED AND FIXED 2026-01-01\n"
                          "\n### 8.101 A third entry - PREPARED, SWITCH DEFERRED 2026-01-02\n"))
    repo.git("add", "-A")
    return repo.guard("Prepare the switch\n")


@case("§0 re-pinned with nothing outside docs/ changed", True)
def _(repo):
    repo.write("docs/name-resolution.md",
               ledger({7: 3}, "### 8.100 A first entry - MEASURED AND FIXED 2026-01-01\n"))
    repo.git("add", "-A")
    return repo.guard("Re-pin row 7\n")


@case("§0 moved alongside code, but the new number is wrong", True)
def _(repo):
    repo.write("src/thing.txt", "two\n")
    repo.write("docs/name-resolution.md",
               ledger({7: 3}, "### 8.100 A first entry - MEASURED AND FIXED 2026-01-01\n"))
    repo.git("add", "-A")
    return repo.guard("Move row 7 without moving the tree\n")


@case("§0 moved with the tree change that moved it", False)
def _(repo):
    repo.write("data.txt", data({7: 3}))
    repo.write("docs/name-resolution.md",
               ledger({7: 3}, "### 8.100 A first entry - MEASURED AND FIXED 2026-01-01\n"
                              "\n### 8.102 Row 7 grows - MEASURED AND LANDED 2026-01-03\n"))
    repo.git("add", "-A")
    return repo.guard("Grow row 7 and say so in §0\n")


def hook_case(repo):
    """The whole chain: git -> commit-msg shim -> guard, on a real commit.

    Everything above calls the guard directly, which leaves the shim untested -
    and the shim is where the last defect was: it picked its interpreter with
    `command -v python3`, which on Windows resolves to an App Execution Alias
    that prints "Python was not found" and exits 49.  Every commit went through
    unchecked and nothing said so.  A guard that stops guarding is silent by
    construction, so the chain gets a case of its own.
    """
    repo.write("docs/name-resolution.md",
               ledger({}, "### 8.100 A first entry - MEASURED AND FIXED 2026-01-01\n"
                          "\nprose added alone\n"))
    repo.git("add", "docs/name-resolution.md")
    r = repo.git("commit", "-m", "Ledger on its own")
    return r.returncode, r.stdout


def main():
    failures = []
    for name, expect_refused, fn in CASES + [
            ("through the installed hook, on a real commit", True, hook_case)]:
        tmp = tempfile.mkdtemp(prefix="ns-guard-")
        try:
            repo = build(tmp)
            code, out = fn(repo)
            refused = code != 0
            ok = refused == expect_refused
            print("  %-6s %-52s %s"
                  % ("ok" if ok else "FAIL", name,
                     "refused" if refused else "allowed"))
            if not ok:
                failures.append(name)
                for ln in out.splitlines()[:12]:
                    print("         | %s" % ln)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    if failures:
        print("ns-guard-selftest: FAILED -- %d case(s): %s"
              % (len(failures), ", ".join(failures)))
        return 1
    print("ns-guard-selftest: OK -- %d case(s), refusals and allowances both, "
          "one of them through the installed hook" % (len(CASES) + 1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
