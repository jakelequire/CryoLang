# Name resolution: the five rules and the doors

Normative. This is what "names are resolved correctly" means for the
compiler in `compiler/src`, stated as rules a change can be checked against.
Where the code breaks one, the code is the defect.

## The five rules

1. **Every written name is resolved once, where it is written.** The name
   layer stamps the node that carries it with a definition, or refuses it at
   that spot. No later pass looks a name up again from its text. A consumer
   that finds an unstamped node treats it as a hard error, never as a cue to
   try another lookup.
2. **Stores are keyed by identity.** Every write into a declaration store
   takes its key from a definition, type or module identity, or from a door.
3. **A spelling reaches a lookup only through a door** from the list below.
4. **Identities are opaque.** The text an identity hands back (a path, a
   leaf, a display name) never feeds a lookup key.
5. **The answers are right.** Not mechanically checkable; a committed corpus
   of resolution shapes with expected answers stands in for it.

```cryo
// Rule 3, refused: a spelling reaches the store outside a door.
const t: TypeRef = types_by_name.get(&intern("Point"));

// Allowed: the name was bound where it was written; the table is asked by
// the identity the stamp carries.
const k: DefKind = defs.kind_of(node.def);

// Rule 4, refused: identity text rebuilt into a key.
const key: SymbolStr = intern(table.resolve(defs.path_of(d)) + "::new");
```

## The doors

A door is the one function where a spelling may become a binding, for one
kind of question. Each carries a doc comment beginning ``/// Door `<id>`.``
followed by the reason it is allowed to take text; the comment sits on the
declaration so the reason cannot be removed without touching the code.

The ruled kinds of door: the bare-identifier scope lookup and its
type-namespace twin; one door per member kind, each taking an owner identity
plus a leaf name; module-by-path (private to the name layer and the module
loader, its other callers converting or becoming its body); the fixed
primitive and language-item table; the interner; and diagnostic suggestions,
whose result may flow only into message text.

`scripts/resolution-doors.py` (run by `make check-fast`) refuses the tree
when this table and the doc-comment markers in `compiler/src` disagree in
either direction: a marker whose door is not listed here, or a row here whose
function carries no marker.

<!-- doors -->
| door | function | file | kind |
|---|---|---|---|
| `scope-value` | `Resolver::lookup_value` | `compiler/src/compiler/resolver/resolver.cryo` | bare identifier to its binding, value namespace |
| `scope-type` | `NameResolver::type_spelling_res` | `compiler/src/compiler/resolver/name_resolution.cryo` | written type name to its binding, type namespace |
| `member-function` | `DeclarationIndex::lookup_family_entries` | `compiler/src/compiler/decl_index.cryo` | member: a function or method family, by owner and leaf |
| `member-field` | `TypeChecker::check_field_access` | `compiler/src/compiler/types/checker.cryo` | member: a field, by owner type and leaf |
| `member-variant` | `EnumType::variant_index` | `compiler/src/compiler/types/user_defined.cryo` | member: an enum variant, by owner type and leaf |
| `module-by-path` | `ModuleGraph::module_named` | `compiler/src/compiler/module_graph.cryo` | written module path to its module |
| `primitive` | `ResBase::is_primitive_spelling` | `compiler/src/compiler/resolver/res.cryo` | the fixed primitive table |
| `primitive` | `ResBase::primitive_of_alias` | `compiler/src/compiler/resolver/res.cryo` | the fixed primitive table: alias keywords |
| `lang-item` | `GenericRegistry::claim_wellknown` | `compiler/src/compiler/types/generic_registry.cryo` | the fixed language-item table |
| `intern` | `InternTable::intern` | `compiler/src/compiler/resolver/intern_table.cryo` | text to `SymbolStr` |
| `suggestion` | `find_best_candidate` | `compiler/src/compiler/diag/edit_distance.cryo` | "did you mean": result flows only into message text |
<!-- /doors -->

What this table does not say: that the tree obeys rule three. Other
functions in `compiler/src` still look a spelling up - duplicates of a
door (`EnumType::get_variant` beside `variant_index`, the rib walks
`Resolver::lookup` and `lookup_prelude` beside `lookup_value`, the four
family readers beside `lookup_family_entries`) and lookups outside every
door. Those are conversions still to make: each becomes a door's body, a
caller of a door, or an identity lookup. No member door is identified yet
for an associated type.
