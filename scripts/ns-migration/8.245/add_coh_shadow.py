"""Insert a SHADOW copy of coherence_key_for (the _res form) after the original,
and a comparison at its one caller.  Asserts every anchor matches exactly once."""
import io
P = r"C:\Programming\apps\CryoLang\compiler\src\compiler\passes\type_resolution.cryo"
src = io.open(P, encoding="utf-8", newline="").read()
nl = "\r\n" if "\r\n" in src else "\n"
lines = src.split(nl)

# 1. copy the function body
start = [i for i, l in enumerate(lines) if l.startswith("    static coherence_key_for(node: ImplBlockNode*,")]
assert len(start) == 1, start
s = start[0]
# the doc comment above it
d = s
while lines[d - 1].startswith("    ///"):
    d -= 1
end = [i for i, l in enumerate(lines) if l.startswith("    /// Peel a trait annotation down")]
assert len(end) == 1, end
e = end[0]
body = lines[s:e]
assert body[-1] == "", repr(body[-1])
new = []
for l in body:
    l2 = l.replace("static coherence_key_for(", "static coherence_key_for_res(")
    l2 = l2.replace("TypeResolutionPasses::ann_canon_key(", "TypeResolutionPasses::ann_canon_key_res(")
    new.append(l2)
text = nl.join(new)
old_target = ("        key = key + TypeResolutionPasses::type_name_key(" + nl +
              "            canonical_target, canonical_target, node, ctx);")
assert text.count(old_target) == 1, text.count(old_target)
new_target = ("        const head_res: Res = match (node.res) {" + nl +
              "            ResSlot::Answered(a) => { a }" + nl +
              "            _                    => { Res::Err }" + nl +
              "        };" + nl +
              "        key = key + TypeResolutionPasses::type_name_key_res(" + nl +
              "            canonical_target, head_res, node, ctx);")
text = text.replace(old_target, new_target)
text = "    /// SHADOW: `coherence_key_for` over the stamp-keyed renderers." + nl + text
lines[e:e] = text.split(nl)

# 2. the comparison at the caller
anchor = "                    const coh_sym: SymbolStr ="
idx = [i for i, l in enumerate(lines) if l == anchor]
assert len(idx) == 1, idx
i = idx[0]
assert lines[i - 3] == "                    const coh_key: string =", lines[i - 3]
ins = [
    "                    {",
    "                        const coh_key2: string =",
    "                            TypeResolutionPasses::coherence_key_for_res(",
    "                                node, canonical_target, ctx);",
    "                        if (!coh_key2.equals(coh_key)) {",
    "                            fmt::eprintf(\"SHADOW\\tCOHKEY-DIFF\\t%s\\t%s\\n\", coh_key, coh_key2);",
    "                        }",
    "                    }",
]
lines[i:i] = ins
io.open(P, "w", encoding="utf-8", newline="").write(nl.join(lines))
print("ok", nl == "\r\n")
