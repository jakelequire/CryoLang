"""After an entry unwrap, the caller's text is `p_text`; count / fix `Text::new(p)` inside a function whose entry unwrap is
`const p: string = p_text.as_string();` (the re-wrap of the caller's text).
argv: root [--fix]"""
import os, re, sys
root = sys.argv[1]; fix = "--fix" in sys.argv
UNW = re.compile(r"const (\w+): string = (\w+)_text\.as_string\(\);")
def code(line):
    s = re.sub(r'"(?:\\.|[^"\\])*"', '""', line)
    s = re.sub(r"'(?:\\.|[^'\\])'", "''", s)
    return s.split("//")[0]
tot = 0
for base, _, files in os.walk(root):
    if "build" in base.split(os.sep):
        continue
    for fn in files:
        if not fn.endswith(".cryo"):
            continue
        p = os.path.join(base, fn)
        raw = open(p, encoding="utf-8", newline="").read()
        nl = "\r\n" if "\r\n" in raw else "\n"
        L = raw.split(nl)
        n = 0
        for i, l in enumerate(L):
            m = UNW.search(l)
            if not m or m.group(1) != m.group(2):
                continue
            v = m.group(1)
            # the body: from the unwrap to where the enclosing depth closes
            depth = 0
            j = i
            opened = code(l).count("{") - code(l).count("}")
            depth = 1 if opened <= 0 else opened
            k = i + 1
            if opened > 0:
                depth = opened
            while k < len(L) and depth > 0:
                c = code(L[k])
                pat = re.compile(r"Text::new\(%s\)" % re.escape(v))
                if pat.search(L[k]):
                    n += len(pat.findall(L[k]))
                    if fix:
                        L[k] = pat.sub(v + "_text", L[k])
                depth += c.count("{") - c.count("}")
                k += 1
        if n:
            tot += n
            print("%4d %s" % (n, os.path.relpath(p, root)))
            if fix:
                open(p, "w", encoding="utf-8", newline="").write(nl.join(L))
print("total", tot)
