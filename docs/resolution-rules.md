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
| `member-field` | `MemberResolver::field_of` | `compiler/src/compiler/sema/member_resolver.cryo` | member: a field, by owner type and leaf |
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

## Appendix: rulings

Decisions Jake made in plain text on questions the rules above leave open.
Each is stated as the behaviour it requires; whether the compiler has it
yet is recorded with the change that builds it, not here.

### 2026-09-29

1. **A method call on a type parameter with no bound is an error.** Inside
   a generic body a parameter has no methods except those a trait bound
   gives it, as in Rust. The standard library's calls that relied on the
   opposite get bounds.

   ```cryo
   type trait Write { write_some(&this, n: u64) -> u64; }

   type struct Sink<W> {
       w: W;
       push(&this, n: u64) -> u64 { return this.w.write_some(n); }   // error: `W` has no bound
   }

   type struct Sink2<W> where W: Write {
       w: W;
       push(&this, n: u64) -> u64 { return this.w.write_some(n); }   // Write::write_some
   }
   ```

2. **Calling a private function-typed field from another module is
   refused**, as reading it already is.

   ```cryo
   namespace A;
   type struct Hook { private run: (i32) -> i32; }

   namespace B;
   import A::{ Hook };
   function fire(h: &Hook) -> i32 { return h.run(1); }   // error: `run` is private to `A`
   ```

3. **After an import collision is refused, a later qualified use binds to
   the item its qualified path names.** The collision refuses the bare
   name only.

   ```cryo
   import Json::{ parse };
   import Toml::{ parse };              // error: `parse` imported twice
   const a: i32 = parse("1");           // refused: the bare name is the collision
   const b: i32 = Toml::parse("1");     // binds to Toml's `parse`
   ```

4. **When a generic and a non-generic method of the same name both apply
   to a call, the call is ambiguous and is an error.** Neither wins by
   being non-generic.

   ```cryo
   type struct Box2 { v: i32; }
   implement Box2 {
       put(&this, x: i32) -> i32 { return x; }
       put<T>(&this, x: T) -> i32 { return 0; }
   }
   const n: i32 = b.put(1);   // error: both `put`s apply
   ```

5. **The command-line flag lookups are out of scope for the rules.** A
   flag or subcommand string (`"--emit"`, `"build"`) is an option string,
   not a name in a program, so matching it is not a lookup rule three
   governs.

6. **Rulings live in this appendix** once `docs/name-resolution.md` is
   deleted.

### 2026-09-28

Relayed in plain text. Done means the five rules above; the definition and
the language's features are frozen for this work. `ModuleGraph::module_named`
becomes a door private to the name layer and the module loader. These were
dropped, and are not to be started:

- a type of its own for file paths used as lookup keys;
- command-line flags taking `Keyword`;
- a written exception list for functions that turn raw text into names;
- a pointer to a name (`SymbolStr*`) counting as taking a name;
- writing the reasons for the pending spelling-lint allows;
- the lane gate no longer counting calls that pass only identities.

The pending allows, the residue file and the approved-names file are
retired after the flow conversions, and are left alone until then.
