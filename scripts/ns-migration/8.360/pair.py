"""The approved-check gate pair: OLD gate (HEAD's check-fast set, section 0
without the new D35 checks) and NEW gate (make check-fast), each run alone
over the tree with one family leaf composed where it is bound."""
import io
import os
import shutil
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
OUT = os.path.join(ROOT, ".objcmp", "s54")  # the run's scratch: logs, the kept source
CR = os.path.join(ROOT, "compiler", "src", "compiler", "sema", "call_resolver.cryo")
KEEP = os.path.join(OUT, "call_resolver.keep")
LEDGER = os.path.join(ROOT, "docs", "name-resolution.md")
OLD_LEDGER = os.path.join(OUT, "ledger-old-d35.md")

s = io.open(LEDGER, encoding="utf-8", newline="").read()
a = s[s.index("| D35 |"):]
row = a[:a.index("\n")]
cells = row.split(" | ")
# cells: [| D35, decision, status, check, sections |]
cells[3] = "`no check` — not built"
s_old = s.replace(row, " | ".join(cells))
io.open(OLD_LEDGER, "w", encoding="utf-8", newline="").write(s_old)

shutil.copyfile(KEEP, CR)
src = io.open(CR, encoding="utf-8", newline="").read()
m = "        mut leaf: SymbolStr = scope.member_name;"
assert src.count(m) == 1
io.open(CR, "w", encoding="utf-8", newline="").write(src.replace(
    m, '        mut leaf: SymbolStr = this.intern.intern(fmt::format("%s::%s", "x", this.intern.resolve(scope.member_name)));'))
try:
    with open(os.path.join(OUT, "pair-old.log"), "wb") as fh:
        r1 = subprocess.run(["make", "--no-print-directory", "lane-check", "lane-selftest",
                             "residue-selftest", "verify-selftest", "verify-pin"],
                            cwd=ROOT, stdout=fh, stderr=subprocess.STDOUT).returncode
        r2 = subprocess.run(["python", "scripts/ns-status-check.py", "--ledger", OLD_LEDGER],
                            cwd=ROOT, stdout=fh, stderr=subprocess.STDOUT).returncode
    print("OLD gate: make targets exit %d, section 0 exit %d" % (r1, r2))
    with open(os.path.join(OUT, "pair-new.log"), "wb") as fh:
        r3 = subprocess.run(["make", "--no-print-directory", "check-fast"],
                            cwd=ROOT, stdout=fh, stderr=subprocess.STDOUT).returncode
    print("NEW gate: make check-fast exit %d" % r3)
finally:
    shutil.copyfile(KEEP, CR)
    print("restored:", io.open(CR, encoding="utf-8", newline="").read() == io.open(KEEP, encoding="utf-8", newline="").read())
