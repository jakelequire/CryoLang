"""One-off codemod for the class deletion: drop the `protected` / virtual /
override flag lines from MethodInfo and FieldInfo construction in FILEs.

Removes lines whose stripped text starts with `is_protected:`,
`is_virtual:    mnode.is_virtual,`, `is_override:   mnode.is_override,` or
`const is_prot: boolean =`, and rewrites `is_pub, is_prot, true` to
`is_pub, true`.  Prints the counts per file.

usage: python scripts/class-removal-drop-flags.py FILE...
"""
import sys

DROP = (b"is_protected:", b"is_virtual:    mnode.is_virtual,", b"is_override:   mnode.is_override,",
        b"const is_prot: boolean =")
for path in sys.argv[1:]:
    with open(path, "rb") as f:
        data = f.read()
    eol = b"\r\n" if b"\r\n" in data else b"\n"
    lines = data.split(eol)
    out = []
    dropped = 0
    for s in lines:
        if s.strip().startswith(DROP):
            dropped += 1
            continue
        out.append(s)
    text = eol.join(out)
    n = text.count(b"is_pub, is_prot, true")
    text = text.replace(b"is_pub, is_prot, true", b"is_pub, true")
    with open(path, "wb") as f:
        f.write(text)
    print("%s: dropped %d lines, %d FieldInfo calls" % (path, dropped, n))
