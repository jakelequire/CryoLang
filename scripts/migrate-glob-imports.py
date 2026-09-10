#!/usr/bin/env python3
"""Turn every glob import into an explicit one, from measured demand.

Input is the GLOB-USE audit stream: one row per lookup that resolved to an
import-bound symbol, carrying (using module, name, DECLARING module).  The
declaring module is what the resolver records, so a name reached through a
re-export is attributed to where it was written, which is also the module the
Specific import branch can find it in.

Three placements, in order:
  * the file already imports that module plainly  -> braces are added to it
  * the file already imports it with braces       -> names merge into the list
  * the file does not import it at all            -> a new line after the last
                                                     import (a re-exported name)

Line endings are read and written with newline='' so a CRLF file stays CRLF.
The tree is not uniformly LF and a rewriter that assumes it is corrupts the
files it touches least visibly.
"""
import collections, io, os, re, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__)))
REPO = r"C:\Programming\apps\CryoLang"

NS_RE    = re.compile(r'^([ \t]*)namespace[ \t]+([A-Za-z0-9_:]+)[ \t]*;[ \t]*\r?$', re.M)
PLAIN_RE = r'^([ \t]*)import[ \t]+%s[ \t]*;[ \t]*\r?$'
BRACE_RE = r'^([ \t]*)import[ \t]+%s[ \t]*::[ \t]*\{([^}]*)\}[ \t]*;[ \t]*\r?$'
ANY_IMP  = re.compile(r'^[ \t]*(?:import|export)[ \t]+[^;]*;[ \t]*\r?$', re.M)


def read_rows(paths):
    rows = []
    for p in paths:
        for line in io.open(p, encoding="utf-8", errors="replace"):
            if line.startswith("PATH-HIT\tGLOB-USE\t"):
                q = line.rstrip("\n").rstrip("\r").split("\t")
                if len(q) >= 5 and q[2] and q[3] and q[4]:
                    rows.append((q[2], q[3], q[4]))
    return rows


def module_files():
    """namespace -> [file].  A namespace may be declared by several files."""
    out = collections.defaultdict(list)
    listed = subprocess.run(["git", "ls-files", "*.cryo"], cwd=REPO,
                            capture_output=True, text=True).stdout.split("\n")
    for rel in listed:
        if not rel.strip():
            continue
        full = os.path.join(REPO, rel.replace("/", os.sep))
        try:
            with io.open(full, encoding="utf-8", errors="replace", newline="") as fh:
                src = fh.read()
        except OSError:
            continue
        m = NS_RE.search(src)
        if m:
            out[m.group(2)].append(full)
    return out


def wrap(indent, module, names, eol):
    """One import line, wrapped at a sane width rather than run on forever."""
    joined = ", ".join(names)
    head = "%simport %s::{ " % (indent, module)
    if len(head) + len(joined) + 3 <= 96:
        return "%s%s };%s" % (head, joined, eol)
    lines, cur = [], []
    width = len(indent) + 4
    for n in names:
        if cur and width + len(n) + 2 > 92:
            lines.append(", ".join(cur) + ",")
            cur, width = [], len(indent) + 4
        cur.append(n)
        width += len(n) + 2
    if cur:
        lines.append(", ".join(cur))
    body = ("%s    " % indent).join(l + eol for l in lines)
    return "%simport %s::{%s%s    %s%s};%s" % (
        indent, module, eol, indent, body, indent, eol)


def migrate(path, needed):
    """needed: {declaring module -> set(names)}.  Returns (changed, stats)."""
    with io.open(path, encoding="utf-8", errors="replace", newline="") as fh:
        src = fh.read()
    eol = "\r\n" if "\r\n" in src else "\n"
    stats = collections.Counter()

    for mod in sorted(needed):
        names = sorted(needed[mod], key=lambda s: (s.lower(), s))
        esc = re.escape(mod)

        bm = re.search(BRACE_RE % esc, src, re.M)
        if bm:
            have = [x.strip().split(" as ")[0].strip()
                    for x in bm.group(2).split(",") if x.strip()]
            merged = sorted(set(have) | set(names), key=lambda s: (s.lower(), s))
            if merged == sorted(have, key=lambda s: (s.lower(), s)):
                stats["already"] += 1
                continue
            src = src[:bm.start()] + wrap(bm.group(1), mod, merged, eol).rstrip("\r\n") + src[bm.end():]
            stats["merged"] += 1
            continue

        pm = re.search(PLAIN_RE % esc, src, re.M)
        if pm:
            src = src[:pm.start()] + wrap(pm.group(1), mod, names, eol).rstrip("\r\n") + src[pm.end():]
            stats["braced"] += 1
            continue

        # Not imported at all: a name reached through a re-export.  Anchor
        # after the LAST existing import so the block stays together.
        last = None
        for m in ANY_IMP.finditer(src):
            last = m
        if last is None:
            nm = NS_RE.search(src)
            if nm is None:
                stats["no_anchor"] += 1
                continue
            at = nm.end()
            src = src[:at] + eol + eol + wrap("", mod, names, eol).rstrip("\r\n") + src[at:]
        else:
            at = last.end()
            src = src[:at] + eol + wrap("", mod, names, eol).rstrip("\r\n") + src[at:]
        stats["added"] += 1

    with io.open(path, "w", encoding="utf-8", newline="") as fh:
        fh.write(src)
    return stats


def main():
    logs = sys.argv[1:]
    if not logs:
        print("usage: migrate_imports.py <probe log> ...")
        return 1
    rows = read_rows(logs)
    print("GLOB-USE rows read: %d" % len(rows))

    files = module_files()
    demand = collections.defaultdict(lambda: collections.defaultdict(set))
    unplaced = collections.Counter()
    for use_mod, name, src_mod in rows:
        if use_mod == src_mod:
            continue
        targets = files.get(use_mod)
        if not targets:
            unplaced[use_mod] += 1
            continue
        for t in targets:
            demand[t][src_mod].add(name)

    print("files to touch: %d   pairs: %d"
          % (len(demand), sum(len(v) for d in demand.values() for v in d.values())))
    if unplaced:
        print("using-modules with no source file (skipped): %d  %s"
              % (len(unplaced), list(unplaced)[:6]))

    total = collections.Counter()
    for path in sorted(demand):
        total.update(migrate(path, demand[path]))
    print("placements:", dict(total))
    return 0


if __name__ == "__main__":
    sys.exit(main())
