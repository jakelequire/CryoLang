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

7. **Dropping a generic value in place goes through a compiler intrinsic**,
   like Rust's `ptr::drop_in_place`, which emits each type's drop code
   after monomorphization: a user `drop`, the drop glue, or nothing for a
   `Copy` value. It is how the standard library releases a value of an
   unbounded type parameter behind a pointer, instead of calling `.drop()`
   on it.

   ```cryo
   function release<T>(p: T*) -> void {
       (*p).drop();                      // a method call on an unbounded `T`
   }
   function release<T>(p: T*) -> void {
       drop_in_place::<T>(p);            // the intrinsic; `T` needs no bound
   }
   ```

   The intrinsic's name and module are not ruled; `drop_in_place` above is
   illustrative.

8. **The old finish-line decisions are superseded by the five rules**: the
   count finish line, and the four conditions with the decisions that serve
   them.

9. **The 2026-09-28 freeze defers the language-feature work it moved off
   this branch; it does not kill it.** Explicit receivers, primitive
   keywords, the raw subcommand, the editor's no-project fallback, the
   deprecated attribute and reference mutability each become a branch of
   their own after the merge.

10. **The ledger (`docs/name-resolution.md`), its status rows and its commit
    hook stay until name resolution is complete.** The paperwork is removed
    after that, not before.

11. **Method selection takes its candidates through the member-function
    door** (`member-function` in the table above). It gets no finder of its
    own.

12. **The generic template registry is re-keyed by identity (`DefId`)**, as
    a unit of its own. Today a generic type and a generic function of the
    same name collide in it.

13. **An inherent method wins over a trait method**, as `docs/cryo.md`
    ("Which Method a Call Names") says - whether either is generic. Today a
    non-generic trait method beats a generic inherent one; that changes,
    measured before it does.

    ```cryo
    type trait Show { show(&this, x: i32) -> i32; }
    type struct P { v: i32; }
    implement P { show<T>(&this, x: T) -> i32 { return 1; } }
    implement trait Show for P { show(&this, x: i32) -> i32 { return 2; } }
    const n: i32 = p.show(5);   // the inherent `show`: 1
    ```

14. **The drop-in-place intrinsic is `drop_in_place<T>(p: T*)` in a new
    module `std::core::ptr`**, mirroring Rust's path. It does not go in
    `std::core::intrinsics`. This names the intrinsic ruling 7 left
    unnamed.

    ```cryo
    import std::core::ptr;
    function release<T>(p: T*) -> void {
        ptr::drop_in_place::<T>(p);
    }
    ```

15. **`BufStream::drop`'s explicit `this.inner.drop()` is deleted**; the
    field glue releases `inner` after the body. Explicit drop calls already
    warn, and they become an error at the v1.0 freeze.

    ```cryo
    implement<S> Drop for BufStream<S> {
        drop(mut &this) -> void {
            this.inner.drop();   // deleted: `inner` is released by field glue
        }
    }
    ```

16. **A method call on a bounded type parameter whose bounds do not declare
    the method is an error (E0358)**, as it is for an unbounded one.

    ```cryo
    type trait Counter { count(&this) -> u64; }
    function f<T>(x: &T) -> void where T: Counter {
        x.count();     // Counter::count
        x.missing();   // error: no bound of `T` declares `missing`
    }
    ```

17. **The E0358 note and help wording for a call on an unbounded parameter,
    and the lane gate's ARENA_READ count moving from 64 to 65, are
    approved.**

18. **No new permanent Python instruments.** Workers build no new gates,
    self-tests or tracking files; throwaway probes for measuring and
    running the existing scripts are fine. New enforcement goes into the
    compiler, as a lint or an internal check. The existing scripts stay as
    they are until they are removed after the merge.

19. **An object that `make verify --require-identical` reports moved, whose
    functions and bodies are identical but emitted in another order, is
    accepted with a written explanation in the commit message.** The verify
    script is not changed to ignore emission order.

20. **verify does not see a change to the standard library alone, and that
    is accepted**: both of its runs compile against the working tree's
    standard library. A library change that alters behaviour is proven by a
    test project of its own, the way a counting transport proves that a
    `BufStream` releases what it wraps once. verify is not extended.

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
