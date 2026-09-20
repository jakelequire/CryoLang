p = 'compiler/src/compiler/mono/ast_resolver.cryo'
s = open(p, encoding='utf-8', newline='').read()
assert s.count('"D30ID-FALLBACK ') == 2
s = s.replace('"D30ID-FALLBACK ', '"SHADOW D30ID-FALLBACK ')
open(p, 'w', encoding='utf-8', newline='').write(s)
print("ok")
