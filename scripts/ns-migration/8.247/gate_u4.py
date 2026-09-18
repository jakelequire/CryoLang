"""lane-gate: the arena has no written-name read; LOOKUP_ARENA retires and
ARENA_READ pins every name-keyed arena read."""
import io
R = r"C:\Programming\apps\CryoLang"

def edit(path, pairs):
    src = io.open(path, encoding="utf-8", newline="").read()
    for old, new in pairs:
        assert src.count(old) == 1, (path, src.count(old), old[:80])
        src = src.replace(old, new)
    io.open(path, "w", encoding="utf-8", newline="").write(src)

edit(R + r"\scripts\lane-gate.py", [
("""  * LOOKUP_ARENA -- `lookup_by_name` on the TypeArena outside the file that
    defines it: the arena's written-name read.  Type resolution asked it at
    17 sites, seven of them a qualified-miss→bare retry, and the gate read
    OK over every one.  Pinned apart from the arena's other reads so a name
    lookup cannot leave the index for the arena and read as progress.
  * ARENA_READ / ARENA_WRITE -- the arena's other name-keyed reads (the
    reverse maps: `get_qualified_name`, `template_key_of`, the display
    formatters) and its creators (`create_struct(qualified_name, module)`,
    `add_name_alias`, `reserve_spec_names`).  A creator is where a declared
    type is keyed by the spelling the caller minted for it.""",
"""  * ARENA_READ / ARENA_WRITE -- the arena's name-keyed reads (the reverse
    maps: `get_qualified_name`, `template_key_of`, the display formatters)
    and its creators (`create_struct(qualified_name, module)`,
    `add_name_alias`, `reserve_spec_names`).  A creator is where a declared
    type is keyed by the spelling the caller minted for it.  The arena has
    no written-name read: `lookup_by_name` was the index's lane on a second
    store (type resolution asked it at 17 sites, seven a qualified-miss→bare
    retry, under a gate that read OK) and is deleted; a name lookup added to
    the arena under any spelling lands in ARENA_READ."""),
("""ARENA_NAME_LOOKUP = "lookup_by_name"
""", ""),
("""         "LOOKUP_ARENA", "ARENA_READ", "ARENA_WRITE",
""", """         "ARENA_READ", "ARENA_WRITE",
"""),
("""    if ARENA_NAME_LOOKUP not in sets[ARENA_TYPE]:
        raise SystemExit("lane-gate: %s does not declare %s as name-crossing; "
                         "the parser has not measured the tree"
                         % (STORES[ARENA_TYPE].defn, ARENA_NAME_LOOKUP))
""", ""),
("""        if store_name == ARENA_TYPE and name == ARENA_NAME_LOOKUP:
            return "LOOKUP_ARENA"
""", ""),
("""    "# LOOKUP_ARENA   lookup_by_name on the TypeArena outside arena.cryo. The",
    "#                arena's written-name read, pinned apart from its other",
    "#                reads so a name lookup cannot leave the index for the",
    "#                arena and read as progress.",
    "# ARENA_READ     the arena's other name-keyed reads: reverse maps and the",""",
 """    "# ARENA_READ     the arena's name-keyed reads: reverse maps and the","""),
])
src = io.open(R + r"\scripts\lane-gate.py", encoding="utf-8", newline="").read()
assert "ARENA_TYPE" in src  # still used by STORES? check below
print("ARENA_TYPE uses:", src.count("ARENA_TYPE"))

edit(R + r"\scripts\lane-gate-selftest.py", [
("""# lookup_by_name - the parser's own controls), a context carrying them, and""",
 """# get_qualified_name - the parser's own controls), a context carrying them, and"""),
("""    "LOOKUP_LOCAL": 1, "LOOKUP_ARENA": 1, "ARENA_READ": 1, "ARENA_WRITE": 0,""",
 """    "LOOKUP_LOCAL": 1, "ARENA_READ": 2, "ARENA_WRITE": 0,"""),
])
print("ok")
