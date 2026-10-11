import sys
p = "C:/Programming/apps/CryoLang/compiler/src/compiler/types/generic_registry.cryo"
src = open(p, encoding="utf-8", newline="").read()
nl = "\r\n" if "\r\n" in src else "\n"
lines = src.split(nl)
start = next(i for i, l in enumerate(lines) if l.strip() == "/// One written argument of a head against one argument of the subject.")
sig = next(i for i, l in enumerate(lines) if "static annotation_unifies(" in l)
assert sig > start
# end: the line '            _ => { return true; }' followed by '        }' after sig
end = None
for i in range(sig, sig + 80):
    if lines[i] == "            _ => { return true; }" and lines[i + 1] == "        }":
        end = i + 1
        break
assert end is not None
new = open("C:/Programming/apps/CryoLang/.objcmp/s136/au_new.txt", encoding="utf-8").read().rstrip("\n").split("\n")
lines[start:end + 1] = new
open(p, "w", encoding="utf-8", newline="").write(nl.join(lines))
print("replaced", start + 1, end + 1, "nl", repr(nl))
