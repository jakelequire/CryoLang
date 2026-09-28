"""For each string-only name-taking row, list how the body uses each `string` parameter.

Usage: python uses.py <sigs.tsv> <out.tsv>
Kinds per use line (first match wins):
  intern   - handed to an intern/parse edge (intern, intern_str, parse, single, child, push on a QualifiedName, lookup on a table)
  store    - compared with / looked up in something reached through `this.` or a `[i]` element, or a map get/contains/insert/push
  literal  - compared with a string literal or matched against literal arms
  display  - inside fmt::format / LOG_ / Text::new / a diagnostic builder
  forward  - an argument to another call
  other
"""
import re, sys, collections, os
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..")).replace("\\", "/") + "/"

def strip_lits(line):
    line = re.sub(r'"(\\.|[^"\\])*"', '""', line)
    line = re.sub(r"'(\\.|[^'\\])*'", "''", line)
    i = line.find("//")
    return line if i < 0 else line[:i]

def body_of(L, ln):
    """Lines of the declaration starting at 1-based ln, head to closing brace."""
    depth = 0; started = False; out = []
    for i in range(ln - 1, len(L)):
        s = strip_lits(L[i]); out.append((i + 1, L[i]))
        for ch in s:
            if ch == "{": depth += 1; started = True
            elif ch == "}": depth -= 1
        if started and depth == 0: return out
        if not started and s.rstrip().endswith(";"): return out
    return out

def params_of(sig):
    m = re.search(r"\((.*)\)", sig)
    if not m: return []
    ps = []
    for p in m.group(1).split(","):
        p = p.strip()
        mm = re.match(r"(?:mut\s+)?(\w+)\s*:\s*(.+)$", p)
        if mm and re.search(r"\bstring\b", mm.group(2)): ps.append((mm.group(1), mm.group(2).strip()))
    return ps

def kind(line, p):
    s = strip_lits(line)
    if not re.search(r"\b%s\b" % re.escape(p), s): return None
    if re.search(r"\b(intern|intern_str|parse|single|child|lookup|of_spelling)\s*\([^)]*\b%s\b" % re.escape(p), s): return "intern"
    if re.search(r"(LOG_\w+|fmt::format|Text::new|format\(|emit|Diagnostic|\.error\(|\.warning\(|note\()", s): return "display"
    if re.search(r'\b%s\s*(==|!=)\s*""|""\s*(==|!=)\s*%s\b|\b%s\.(eq|starts_with|ends_with)\(""\)' % (p, p, p), s): return "literal"
    if re.search(r"match\s*\(\s*%s\s*\)" % p, s): return "literal"
    if re.search(r"(this\.\w+|\]\s*\.\w+|\w+\[\w+\])[\w.\[\]]*\.(eq|equals|get|contains_key|contains|insert|push|find|index_of)\(\s*&?%s\b" % p, s): return "store"
    if re.search(r"\.(get|contains_key|insert|put)\(\s*&?%s\b" % p, s): return "store"
    if re.search(r"\b%s\.eq\(\s*(this\.|\w+\[)" % p, s): return "store"
    if re.search(r"\w\s*\([^()]*\b%s\b" % p, s): return "forward"
    return "other"

rows = [l.rstrip("\n").split("\t") for l in open(sys.argv[1], encoding="utf-8")]
cache = {}
out = open(sys.argv[2], "w", encoding="utf-8", newline="\n")
tot = collections.Counter()
for k, loc, name, sig in rows:
    if k != "string": continue
    f, ln = loc.rsplit(":", 1); ln = int(ln)
    if f not in cache: cache[f] = open(ROOT + f, encoding="utf-8", errors="replace").read().splitlines()
    body = body_of(cache[f], ln)
    for p, ty in params_of(sig):
        c = collections.Counter()
        for i, (n, line) in enumerate(body):
            if i == 0 and "{" not in line: continue
            s = line if i else line[line.find("{"):] if "{" in line else ""
            kd = kind(s, p)
            if kd: c[kd] += 1
        summary = ",".join("%s:%d" % kv for kv in sorted(c.items())) or "unused"
        tot[max(c, key=lambda x: ["other","forward","display","literal","store","intern"].index(x)) if c else "unused"] += 1
        out.write("%s\t%s\t%s\t%s\t%s\n" % (loc, name, p, ty, summary))
for k, v in tot.most_common(): print("%5d %s" % (v, k))
