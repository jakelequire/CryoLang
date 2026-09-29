"""Delete OwnershipQuery's by-name consume helpers (type_method_consumes,
ast_method_consumes, impl_blocks_method_consumes) from ownership.cryo.
Asserts the exact boundaries before cutting."""
p = r"C:\Programming\apps\CryoLang\compiler\src\compiler\types\ownership.cryo"
raw = open(p, "rb").read()
crlf = b"\r\n" in raw
lines = raw.decode("utf-8").splitlines(keepends=True)
start = next(i for i, l in enumerate(lines) if "Receiver-type + method-name fallback" in l)
end = next(i for i, l in enumerate(lines) if "Strip a qualified name down to its bare leaf" in l)
assert lines[start - 1].strip() == "", lines[start - 1]
assert lines[end - 1].strip() == "", lines[end - 1]
cut = "".join(lines[start:end])
for name in ("static type_method_consumes(", "static ast_method_consumes(", "static impl_blocks_method_consumes("):
    assert name in cut, name
assert cut.count("    static ") == 3, cut.count("    static ")
out = "".join(lines[:start] + lines[end:])
open(p, "wb").write(out.encode("utf-8"))
print("cut lines", start + 1, "to", end, "crlf", crlf)
