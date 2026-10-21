"""Delete docs/cryo.md section 9 and renumber sections 10-25 down by one.

Rewrites headings, in-page anchors and "section N[.M]" text in cryo.md, and
`cryo.md#anchor` links in the other files named on the command line.  Every
in-page link is checked to resolve before (excluding #9-classes) and after.
Prints every changed line.  A one-off codemod: run from anywhere as
`python scripts/reference-renumber-codemod.py CHANGELOG.md README.md
docs/async-internals.md`, and deleted in the commit after the one that ran it.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__))).replace("\\", "/") + "/"
SPEC = ROOT + "docs/cryo.md"
HEAD = re.compile(r"^(#{2,5}) (\d+)((?:\.\d+)*)(\.?) (.*?)\s*$")


def slug(text):
    s = text.strip().lower()
    s = re.sub(r"[^\w\- ]", "", s)
    return s.replace(" ", "-")


def anchors(lines):
    out = set()
    for line in lines:
        m = re.match(r"^#{1,6} (.*?)\s*$", line)
        if m:
            out.add(slug(m.group(1)))
    return out


def links(lines):
    out = []
    for i, line in enumerate(lines):
        for m in re.finditer(r"\]\(#([^)]+)\)", line):
            out.append((i + 1, m.group(1)))
    return out


def check(lines, label, skip=()):
    have = anchors(lines)
    bad = [(n, a) for n, a in links(lines) if a not in have and a not in skip]
    print(f"{label}: {len(links(lines))} in-page links, {len(bad)} unresolved")
    for n, a in bad:
        print(f"  line {n}: #{a}")
    return bad


def main():
    with open(SPEC, encoding="utf-8", newline="") as f:
        text = f.read()
    lines = text.split("\n")
    check(lines, "before", skip=("9-classes",))

    start = next(i for i, l in enumerate(lines) if l.rstrip() == "## 9. Classes")
    end = next(i for i, l in enumerate(lines) if l.rstrip() == "## 10. Enums")
    del lines[start:end]
    print(f"deleted lines {start + 1}-{end} (section 9)")

    amap = {}
    for i, line in enumerate(lines):
        m = HEAD.match(line.rstrip("\r"))
        if not m or int(m.group(2)) < 10:
            continue
        hashes, top, sub, dot, title = m.groups()
        old_head = f"{top}{sub}{dot} {title}"
        new_head = f"{int(top) - 1}{sub}{dot} {title}"
        amap[slug(old_head)] = slug(new_head)
        cr = "\r" if line.endswith("\r") else ""
        lines[i] = f"{hashes} {new_head}{cr}"

    def fix_anchor(m):
        return "(#" + amap.get(m.group(1), m.group(1)) + ")"

    def fix_section(m):
        n = int(m.group(2))
        if n < 10:
            return m.group(0)
        return f"{m.group(1)}{n - 1}{m.group(3)}"

    for i, line in enumerate(lines):
        new = re.sub(r"\(#([^)]+)\)", fix_anchor, line)
        new = re.sub(r"(\[?[Ss]ection )(\d+)((?:\.\d+)*)", fix_section, new)
        new = re.sub(r"^(- \[)(\d+)(\. )", fix_section, new) if new.startswith("- [") else new
        if new != line:
            print(f"cryo.md {i + 1}: {new.strip()[:160]}")
            lines[i] = new

    if check(lines, "after"):
        sys.exit(1)
    with open(SPEC, "w", encoding="utf-8", newline="") as f:
        f.write("\n".join(lines))

    for path in sys.argv[1:]:
        with open(ROOT + path, encoding="utf-8", newline="") as f:
            body = f.read()
        changed = re.sub(r"(cryo\.md#)([\w-]+)", lambda m: m.group(1) + amap.get(m.group(2), m.group(2)), body)
        if changed != body:
            for a, b in zip(body.split("\n"), changed.split("\n")):
                if a != b:
                    print(f"{path}: {b.strip()[:160]}")
            with open(ROOT + path, "w", encoding="utf-8", newline="") as f:
                f.write(changed)


main()
