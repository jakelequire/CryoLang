p = '.objcmp/s134/xcheck/xcheck.py'
s = open(p, encoding='utf-8').read()
a = 'removed = json.load(open(".objcmp/s134/xcheck/removed.json"))'
b = '''json.dump(sorted(refused_fns), open(".objcmp/s134/xcheck/refused-fns.json", "w"), indent=0)
removed = json.load(open(".objcmp/s134/xcheck/removed.json"))'''
assert s.count(a) == 1
s = s.replace(a, b)
open(p, 'w', encoding='utf-8').write(s)

p = '.objcmp/s134/xcheck/classify.py'
s = open(p, encoding='utf-8').read()
a = 'open(".objcmp/s134/CROSSCHECK.md", "w", encoding="utf-8").write("\\n".join(out) + "\\n")'
b = '''import json
refused = json.load(open(".objcmp/s134/xcheck/refused-fns.json"))
listed = set()
for r in rows:
    listed.add(r[5])
    listed.add(r[5].rsplit(".", 1)[0] + "::" + r[5].rsplit(".", 1)[-1] if "." in r[5] else r[5])
extra = [q for q in refused if q not in listed and (q.rsplit(".", 1)[0] + "::" + q.rsplit(".", 1)[-1]) not in listed]
out.append("## The other direction: functions the compiler refuses that the list does not name (%d)\\n" % len(extra))
out.append("With its allow removed, each of these is refused by the compiler; no entry of "
           "the list has it as its lookup function.  The list is therefore no superset "
           "of the compiler's check either.\\n")
for q in extra:
    out.append("* `%s`" % q.replace("compiler::", ""))
out.append("")
open(".objcmp/s134/CROSSCHECK.md", "w", encoding="utf-8").write("\\n".join(out) + "\\n")
print("refused but unlisted:", len(extra))'''
assert s.count(a) == 1
s = s.replace(a, b)
open(p, 'w', encoding='utf-8').write(s)
print('ok')
