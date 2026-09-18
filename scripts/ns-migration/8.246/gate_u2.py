"""lane-gate: the LOOKUP row is four names now (lookup_func_return deleted)."""
import io
R = r"C:\Programming\apps\CryoLang"

def edit(path, pairs):
    src = io.open(path, encoding="utf-8", newline="").read()
    for old, new in pairs:
        assert src.count(old) == 1, (path, src.count(old), old[:80])
        src = src.replace(old, new)
    io.open(path, "w", encoding="utf-8", newline="").write(src)

edit(R + r"\scripts\lane-gate.py", [
("""  * LOOKUP -- answered by the `DeclarationIndex` under one of the five
    per-kind names mechanism 5 gives (`lookup_type`, `lookup_func_return`,
    `lookup_func_type`, `lookup_global`, `lookup_method_return`).""",
"""  * LOOKUP -- answered by the `DeclarationIndex` under one of the four
    per-kind names mechanism 5 gives (`lookup_type`, `lookup_func_type`,
    `lookup_global`, `lookup_method_return`; `lookup_func_return` read a
    second map written in lockstep with the signature's and is deleted)."""),
("""    sixth name was one the five-name rule could not see.""",
 """    fifth name was one the four-name rule could not see."""),
("""# The five per-kind lookups §7.2 mechanism 5 names: the LOOKUP row.  They are""",
 """# The four per-kind lookups §7.2 mechanism 5 names: the LOOKUP row.  They are"""),
("""    "lookup_type",
    "lookup_func_return",
    "lookup_func_type",
""",
"""    "lookup_type",
    "lookup_func_type",
"""),
("""    # Control on the parser: the LOOKUP row is the five names, and they are""",
 """    # Control on the parser: the LOOKUP row is the four names, and they are"""),
("""    "# LOOKUP         answered by the DeclarationIndex, under one of the five",""",
 """    "# LOOKUP         answered by the DeclarationIndex, under one of the four","""),
("""    "#                surface pinned at five names is one a caller can leave by",""",
 """    "#                surface pinned at four names is one a caller can leave by","""),
("""    "#                name was invisible to a five-name rule.",""",
 """    "#                name was invisible to a four-name rule.","""),
])
edit(R + r"\scripts\lane-gate-selftest.py", [
("""# name-keyed method (the index with the five LOOKUP names, the arena with""",
 """# name-keyed method (the index with the four LOOKUP names, the arena with"""),
("""    lookup_func_return(&this, name: SymbolStr) -> TypeRef { return this.entries[0]; }
""", ""),
])
print("ok")
