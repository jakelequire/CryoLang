import sys
which = sys.argv[1]
R = "C:/Programming/apps/CryoLang/compiler/src/compiler/"
if which == "a":
    p = R + "mono/call_specializer.cryo"
    old = "        } else if (call.bound_trait_args.length > 0) {\n"
    new = "        } else if (call.bound_trait_args.length > 999999) {\n"
else:
    p = R + "resolver/mangled_name.cryo"
    old = "        if (trait_args.length > 0) {\n            buf += \"$L\";\n"
    new = "        if (trait_args.length > 999999) {\n            buf += \"$L\";\n"
s = open(p, encoding="utf-8", newline="").read()
assert s.count(old) == 1
s = s.replace(old, new)
open(p, "w", encoding="utf-8", newline="").write(s)
print("ctl2", which, "in")
