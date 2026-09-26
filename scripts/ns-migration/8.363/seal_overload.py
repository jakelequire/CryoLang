"""Move OverloadId from resolver/res.cryo into decl_index.cryo (the module
that mints it), constructor and number private there; rewrite importers."""
import io
import os
import re

SRC = r"C:\Programming\apps\CryoLang\compiler\src\compiler"


def load(p):
    s = io.open(p, encoding="utf-8", newline="").read()
    return s, ("\r\n" if "\r\n" in s else "\n")


def save(p, s):
    io.open(p, "w", encoding="utf-8", newline="").write(s)


# 1. cut the type from res.cryo
p = SRC + r"\resolver\res.cryo"
s, nl = load(p)
start = s.index("type struct OverloadId {")
# include the doc comment block above it
doc = s.rfind(nl + nl, 0, start) + len(nl + nl)
end = s.index(nl + "}" + nl, start) + len(nl + "}" + nl)
old_block = s[doc:end]
s = s[:doc] + s[end:]
save(p, s)
print("cut", old_block.count("\n"), "lines from res.cryo")

# 2. the sealed type in decl_index.cryo, after the imports
p = SRC + r"\decl_index.cryo"
s, nl = load(p)
new_block = """/// A signature's position in the index's function registry: the identity of
/// one registered declaration signature, which a call pins and codegen reads
/// the symbol and signature off.  Minted only here, by `register_signature`,
/// and read only here: the position is private to this module, so an id
/// cannot be built from a number anywhere else - an in-range position would
/// silently name whichever entry sits there.
type struct OverloadId {
private:
    /// Position in the registry's parallel arrays; `0xFFFFFFFF` for none.
    pos: u32;

    /// The id of the entry at registry position `pos`.  Registration's mint.
    static at_position(pos: u32) -> OverloadId {
        return OverloadId { pos: pos };
    }

    /// The registry position, for this index's own accessors.
    position(&this) -> u32 {
        return this.pos;
    }

public:
    /// The id naming no entry, for a slot that must hold one before an
    /// answer exists.
    static invalid() -> OverloadId {
        return OverloadId { pos: 4294967295 };
    }

    is_valid(&this) -> boolean {
        return this.pos != 4294967295;
    }

    equals(&this, other: OverloadId) -> boolean {
        return this.pos == other.pos;
    }
}

"""
anchor = "type struct DeclarationIndex {"
assert s.count(anchor) == 1
i = s.index(anchor)
# place before the DeclarationIndex's own doc comment block
j = s.rfind(nl + nl, 0, i) + len(nl + nl)
s = s[:j] + new_block.replace("\n", nl) + s[j:]
s = s.replace("import compiler::resolver::res::{ DeclVisibility, DefId, FamilyOwner, OverloadId, Res, ResSlot, DefTable };",
              "import compiler::resolver::res::{ DeclVisibility, DefId, FamilyOwner, Res, ResSlot, DefTable };")
save(p, s)

# 3. importers
pat = re.compile(r"import compiler::resolver::res::\{([^}]*)\};")
n = 0
for dirpath, _, files in os.walk(SRC):
    for f in files:
        if not f.endswith(".cryo") or f == "decl_index.cryo":
            continue
        p = os.path.join(dirpath, f)
        s, nl = load(p)
        m = pat.search(s)
        if not m or "OverloadId" not in [x.strip() for x in m.group(1).split(",")]:
            continue
        names = [x.strip() for x in m.group(1).split(",") if x.strip() and x.strip() != "OverloadId"]
        repl = ("import compiler::resolver::res::{ %s };" % ", ".join(names)) if names else ""
        repl += (nl if repl else "") + "import compiler::decl_index::{ OverloadId };"
        s = s[:m.start()] + repl + s[m.end():]
        save(p, s)
        n += 1
        print("  importer", os.path.relpath(p, SRC))
print("importers rewritten:", n)
