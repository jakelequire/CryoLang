"""Compare two object trees built by objbuild.sh.  An object identical byte
for byte is SAME.  Otherwise its symbol table and its disassembly with
relocations are compared after removing a trait-impl symbol's trait argument
list (`C$tr$<trait>$L...$G$f` -> `C$tr$<trait>$f`); equal after that is
RENAMED-ONLY, anything else is DIFFERENT.
   usage: objcmp.py <tree-a> <tree-b>"""
import os, re, subprocess, sys, hashlib

a, b = sys.argv[1], sys.argv[2]
TRARG = re.compile(r"(C\$tr\$[0-9A-Za-z_.]+?)\$L.*?\$G\$f")


def objs(root):
    out = {}
    for dp, _, fs in os.walk(root):
        for f in fs:
            if f.endswith(".o"):
                p = os.path.join(dp, f)
                out[os.path.relpath(p, root).replace("\\", "/")] = p
    return out


def norm(text):
    return TRARG.sub(r"\1$f", text)


def view(p):
    nm = subprocess.run(["nm", p], capture_output=True, text=True).stdout
    od = subprocess.run(["objdump", "-d", "-r", "--no-show-raw-insn", p],
                        capture_output=True, text=True).stdout
    od = "\n".join(od.splitlines()[2:])  # drop the file-name header
    # nm lists by name, so a renamed symbol moves in the listing: compare as sets.
    return sorted(norm(nm).splitlines()), norm(od)


def digest(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


oa, ob = objs(a), objs(b)
same = renamed = 0
diff = []
renamed_with = 0
only = sorted(set(oa) ^ set(ob))
for k in sorted(set(oa) & set(ob)):
    if digest(oa[k]) == digest(ob[k]):
        same += 1
        continue
    va, vb = view(oa[k]), view(ob[k])
    if va == vb:
        renamed += 1
        nmb = subprocess.run(["nm", ob[k]], capture_output=True, text=True).stdout
        if TRARG.search(nmb):
            renamed_with += 1
    else:
        diff.append(k)
print("objects in both:", len(set(oa) & set(ob)), "same:", same,
      "renamed-only:", renamed, "(holding a trait-argument symbol:", renamed_with, ")",
      "different:", len(diff), "only-one-side:", len(only))
for k in diff[:40]:
    print("DIFFERENT", k)
for k in only[:40]:
    print("ONLY", k)
