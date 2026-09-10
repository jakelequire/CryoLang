#!/usr/bin/env python3
"""Put back the plain `import M;` line the migration replaced with a braced one.

`import M;` and `import M::{ X };` are not the same statement.  The first is
what makes `M` usable as a QUALIFIER; the second binds `X`.  The loader records
the base path of a braced import too, but not in a way that survives as a
visibility edge - measured: replacing the plain line in `stdlib/test/runner.cryo`
made `mpsc::Sender` unresolvable, and restoring it alongside compiled clean.

So a file that used both needs both lines, which is also how Rust writes it:
`use m;` for the qualifier, `use m::X;` for the name.

Forward-only and idempotent: it adds a line that is missing, never removes one,
and skips a file that already has the plain form.
"""
import io, os, re, subprocess, sys

REPO = r"C:\Programming\apps\CryoLang"

def plain_re(mod):
    return re.compile(r'^[ \t]*import[ \t]+%s[ \t]*;[ \t]*$' % re.escape(mod), re.M)

def braced_re(mod):
    return re.compile(r'^([ \t]*)import[ \t]+%s[ \t]*::[ \t]*\{' % re.escape(mod), re.M)

MOD_OF_PLAIN = re.compile(r'^[ \t]*import[ \t]+([A-Za-z0-9_:]+)[ \t]*;[ \t]*$', re.M)


def main():
    changed = files = 0
    listed = subprocess.run(["git", "diff", "--name-only"], cwd=REPO,
                            capture_output=True, text=True).stdout.split("\n")
    for rel in listed:
        rel = rel.strip()
        if not rel.endswith(".cryo"):
            continue
        full = os.path.join(REPO, rel.replace("/", os.sep))
        try:
            with io.open(full, encoding="utf-8", errors="replace", newline="") as fh:
                cur = fh.read()
        except OSError:
            continue
        orig = subprocess.run(["git", "show", "HEAD:" + rel], cwd=REPO,
                              capture_output=True, text=True).stdout
        if not orig:
            continue
        eol = "\r\n" if "\r\n" in cur else "\n"
        # Every module the ORIGINAL imported plainly.
        wanted = [m.group(1) for m in MOD_OF_PLAIN.finditer(orig)]
        added = 0
        for mod in wanted:
            if plain_re(mod).search(cur):
                continue                      # still there, or already restored
            bm = braced_re(mod).search(cur)
            if not bm:
                continue                      # not replaced by a braced form
            line = "%simport %s;%s" % (bm.group(1), mod, eol)
            cur = cur[:bm.start()] + line + cur[bm.start():]
            added += 1
        if added:
            with io.open(full, "w", encoding="utf-8", newline="") as fh:
                fh.write(cur)
            files += 1
            changed += added
    print("files fixed: %d   plain imports restored: %d" % (files, changed))
    return 0


if __name__ == "__main__":
    sys.exit(main())
