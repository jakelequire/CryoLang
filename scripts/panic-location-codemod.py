"""One-off codemod: `panic` takes a `Location`.

1. Every call passing the place as two arguments,
       panic(msg, intrinsics::location().file, intrinsics::location().line)
   passes the `Location` itself:
       panic(msg, intrinsics::location())
   in every tracked `.cryo` file and every example in `docs/`.  The two
   arguments may be split across lines.
2. Every prose mention of the panic funnel `__cryo_panic` (a comment, a
   Makefile or config note, a doc) names `__cryo_panic_at`, the funnel's one
   symbol.  `__cryo_panic_<suffix>` symbols (check handlers, accessors) are
   other lang items and are left alone, as are the runtime tiers and
   `lang_items.def`, whose definitions are edited by hand.

Run from the repository root: python scripts/panic-location-codemod.py
"""
import io
import re
import subprocess

CALL = re.compile(r",(\s*)intrinsics::location\(\)\.file,\s*intrinsics::location\(\)\.line\)")
FUNNEL = re.compile(r"__cryo_panic(?![A-Za-z0-9_])")
HAND_EDITED = (
    "runtime/panic/abort/src/lib.cryo",
    "runtime/panic/unwind/src/lib.cryo",
    "runtime/abi/lang_items.def",
)
PROSE = (
    "Makefile",
    "compiler/src",
    "docs/async-internals.md",
    "runtime",
    "scripts/build-release.sh",
    "scripts/release-smoke.sh",
    "scripts/selfhost-check.py",
)


def tracked(*paths):
    out = subprocess.run(["git", "ls-files", "--", *paths], capture_output=True, text=True, check=True)
    return [p for p in out.stdout.splitlines() if p]


def rewrite(path, fn):
    with io.open(path, "r", encoding="utf-8", newline="") as fh:
        text = fh.read()
    new, n = fn(text)
    if n:
        with io.open(path, "w", encoding="utf-8", newline="") as fh:
            fh.write(new)
    return n


calls = files = 0
for path in tracked("*.cryo", "docs/*.md"):
    n = rewrite(path, lambda t: CALL.subn(",\\1intrinsics::location())", t))
    if n:
        calls += n
        files += 1
print("calls: %d in %d files" % (calls, files))

mentions = mfiles = 0
for path in tracked(*PROSE):
    if path in HAND_EDITED:
        continue
    n = rewrite(path, lambda t: FUNNEL.subn("__cryo_panic_at", t))
    if n:
        mentions += n
        mfiles += 1
        print("  prose:", path, n)
print("funnel mentions: %d in %d files" % (mentions, mfiles))
