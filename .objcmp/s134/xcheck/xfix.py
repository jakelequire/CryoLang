p = '.objcmp/s134/xcheck/xcheck.py'
s = open(p, encoding='utf-8').read()
pairs = [
    ("""        if f["file"].endswith(file.replace("src/", "", 1)) and f["start"] <= line <= f["end"]:""",
     """        if f["file"].lower().endswith(file.replace("src/", "", 1).lower()) and f["start"] <= line <= f["end"]:"""),
    ("""refused_fns = defaultdict(list)
for msg, spans in refusals:
    for (file, line) in spans[:1]:
        f = fn_at(strip, file, line)
        if f:
            refused_fns[f["q"]].append(msg)
""", """refused_fns = defaultdict(list)
unmapped = []
for msg, spans in refusals:
    for (file, line) in spans[:1]:
        f = fn_at(strip, file, line)
        if f:
            refused_fns[f["q"]].append(msg)
        else:
            unmapped.append((msg, file, line))
"""),
    ("""print("refusals in the stripped build:", len(refusals), "in", len(refused_fns), "functions")""",
     """print("refusals in the stripped build:", len(refusals), "in", len(refused_fns), "functions;", len(unmapped), "unmapped")
for u in unmapped:
    print("  UNMAPPED", u)
removed = json.load(open(".objcmp/s134/xcheck/removed.json"))
print("allows removed:", len(removed))"""),
]
for a, b in pairs:
    assert s.count(a) == 1, a
    s = s.replace(a, b)
open(p, 'w', encoding='utf-8').write(s)
print('ok')
