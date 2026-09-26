"""Wrote scripts/ns-migration/approved-names.tsv's first five entries, the
overload-family doors (docs/name-resolution.md 8.360).  Entries added
later are written into the file itself."""
import io
import os

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "approved-names.tsv")

HEADER = """# Functions in compiler/src that still take a name, each with the reason the
# name is correct where it is taken and a command that fails when the reason
# stops being true.  One entry per line, three tab-separated fields:
#
#     <signature> TAB <reason> TAB <command>
#
# A command is `python scripts/ns-migration/approved.py` followed by the
# assertions it runs (see that script); `approved.py --check` runs them all
# and `make check-fast` runs it.  The list does not yet cover every
# name-taking function, and nothing refuses one it does not list:
# `done.py --name-taking` is that population.
"""

FAMILY_REASON = (
    "An overload family is the set of definitions one owner declares under one written leaf, "
    "as a definition is its parent and its leaf. The owner arrives as an identity - a module's "
    "or a type's DefId, or a type's arena id - so the leaf is asked INSIDE an owner already "
    "settled, the way a field or method is looked up by name off its type. Overloading is "
    "kept, so several definitions share the leaf and the leaf is the question, not a stand-in "
    "for an identity; Rust's resolver keys the same set by parent module, identifier and "
    "namespace. The leaf is a stamp's leaf, a written member name, or a minted "
    "specialization identifier - never a path composed at the call."
)

DOORS = [
    ("compiler/src/compiler/decl_index.cryo",
     "DeclarationIndex::lookup_family_entries",
     "lookup_family_entries(&this, owner: FamilyOwner, leaf: SymbolStr) -> OverloadId[]",
     FAMILY_REASON),
    ("compiler/src/compiler/decl_index.cryo",
     "DeclarationIndex::lookup_func_type_overloads",
     "lookup_func_type_overloads(&this, owner: FamilyOwner, leaf: SymbolStr) -> TypeRef[]",
     "The signatures of an overload family, asked as lookup_family_entries asks the entries. " + FAMILY_REASON),
    ("compiler/src/compiler/decl_index.cryo",
     "DeclarationIndex::lookup_func_type",
     "lookup_func_type(&this, owner: FamilyOwner, leaf: SymbolStr) -> TypeRef",
     "The signature last registered in an overload family, asked as lookup_family_entries asks the entries. " + FAMILY_REASON),
    ("compiler/src/compiler/decl_index.cryo",
     "DeclarationIndex::family_intrinsic_kind",
     "family_intrinsic_kind(&this, owner: FamilyOwner, leaf: SymbolStr) -> IntrinsicKind",
     "The intrinsic kind an overload family's entries share, for a call pinned to the family; a call pinned to one entry reads the kind off the entry instead. " + FAMILY_REASON),
    ("compiler/src/compiler/sema/type_utils.cryo",
     "TypeUtils::lookup_func_type_exact",
     "lookup_func_type_exact(&this, owner: FamilyOwner, leaf: SymbolStr) -> TypeRef",
     "Sema's funnel onto DeclarationIndex::lookup_func_type, with no widening. " + FAMILY_REASON),
]

lines = [HEADER]
for rel, qual, sig, reason in DOORS:
    method = sig.split("(", 1)[0]
    cmd = ("python scripts/ns-migration/approved.py --signature %s '%s' --owner-is-identity "
           "--no-composed-leaf %s" % (rel, sig, method))
    lines.append("%s %s\t%s\t%s\n" % (qual, sig[len(method):], reason, cmd))
with io.open(OUT, "w", encoding="utf-8", newline="\n") as fh:
    fh.write("".join(lines))
print("wrote", len(DOORS))
