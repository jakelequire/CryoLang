"""Strip the loader's new door marker and its three function allows, in
place, so the checks can be shown refusing what they cover.  Restore from
.objcmp/s134/module_loader.keep afterwards."""
p = "compiler/src/compiler/module_loader.cryo"
s = open(p, encoding="utf-8", newline="").read()
nl = "\r\n" if "\r\n" in s else "\n"
lines = s.split(nl)
out = []
n_allow = 0
n_door = 0
for l in lines:
    st = l.strip()
    if st.startswith("![allow(lookup_by_spelling") and ("records the namespace" in st or "the body of the `loader-path`" in st or "a `vendor::` import" in st):
        n_allow += 1
        continue
    if st.startswith("/// Door `loader-path`.  A module path written"):
        l = l.replace("/// Door `loader-path`.  A module path written", "/// A module path written")
        n_door += 1
    out.append(l)
open(p, "w", encoding="utf-8", newline="").write(nl.join(out))
print("allows removed", n_allow, "door markers removed", n_door)
