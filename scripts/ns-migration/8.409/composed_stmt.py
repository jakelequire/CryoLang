"""Print the whole `intern(...)` argument expression at each composed-key site."""
import re, sys
ROOT = __import__("os").path.dirname(__import__("os").path.dirname(__import__("os").path.dirname(__import__("os").path.dirname(__import__("os").path.abspath(__file__))))) + "/"
recs = [l.rstrip("\n").split("\t") for l in open(ROOT + ".facts/compiler.facts", encoding="utf-8")]
INTERN = re.compile(r"(InternTable|CompilationContext)\.intern\(")
sites = sorted({(r[1], int(r[2]), r[13].split("(")[0].split("::")[-1], "fmt" if r[6].startswith("call") else "cat")
                for r in recs if r[0] in ("arg", "sarg") and r[1].startswith("src/")
                and INTERN.search(r[12]) and (r[6].startswith("expr:BinaryExpression")
                                             or r[6].startswith("call:std::fmt::format"))})
cache = {}
for i, (path, n, fn, kind) in enumerate(sites, 1):
    if path not in cache:
        cache[path] = open(ROOT + "compiler/" + path, encoding="utf-8").read().splitlines()
    lines = cache[path]
    text = lines[n - 1]
    j = n
    start = text.find("intern(")
    depth = 0
    buf = ""
    k = n - 1
    s = text[start:]
    while True:
        for ch in s:
            buf += ch
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
                if depth == 0:
                    break
        if depth == 0 or k + 1 >= len(lines):
            break
        k += 1
        s = " " + lines[k].strip()
    print("%2d %s %s:%d %s  %s" % (i, kind, path.replace("src/compiler/", ""), n, fn, re.sub(r"\s+", " ", buf)))
