# Name resolution: the five rules and the doors

Normative. This is what "names are resolved correctly" means for the
compiler in `compiler/src`, stated as rules a change can be checked against.
Where the code breaks one, the code is the defect.

## The five rules

1. **Every written name is resolved once, where it is written.** The name
   layer stamps the node that carries it with a definition, or refuses it at
   that spot. No later pass looks a name up again from its text. A consumer
   that finds an unstamped node treats it as a hard error, never as a cue to
   try another lookup.  A pass that synthesizes a name-bearing node -
   desugaring, the async lowering, the AST cloner - knows what it refers
   to, so it creates the node already stamped.
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
const key: SymbolStr = intern(table.shown(defs.path_of(d)) + "::new");
```

A definition's path or leaf is read as a `DeclName`, whose text comes back
only through `InternTable::shown` (for a message or a mangled symbol) and
`DeclName::spelling` (for a node synthesized to carry the name).  A named
type's display (`TypeArena::format_display`) is its declaration's
`DeclName` shown, and a function's linker symbol
(`DeclarationIndex::entry_symbol`) is a `MangledName`.

## The doors

A door is the one function where a spelling may become a binding, for one
kind of question.  The doors are the functions the table below lists; the
table is the normative list.

The ruled kinds of door: the bare-identifier scope lookup and its
type-namespace twin; one door per member kind, each taking an owner identity
plus a leaf name; module-by-path (private to the module graph, which offers
its callers methods each named for the one kind of text it turns into a
module); the loader's path door, which turns a module path written in an
`import`, `export` or `public module` into the file it names, and matches a
body path's head against the file's imports and its continuation against
the namespaces files declare, before any module exists - discovery's
written paths are spellings (`SymbolStr`), and what the door answers is a
file, whose path keys discovery's own sets; the fixed
primitive and language-item table; the interner; and diagnostic suggestions,
whose result may flow only into message text.

No mechanical check enforces rules three and four.  They are held by
structure: each store keeps its names in private fields reachable only
through its door; tables are keyed by typed identities (`DefKey`,
`TypeKey`, `SymbolKey`, `OverloadKey` and the rest), which a spelling's
number cannot fill; a definition's name is a `DeclName` and a link symbol a
`MangledName`, whose text comes back only through `InternTable::shown`;
and the named types' paths are sealed the same way.  A new accessor that
hands identity text out as a `SymbolStr`, or a table keyed by a raw number
or text, is a change a reviewer has to refuse.

Rule three is also held by visibility, one store at a time: a store's name
fields are `private` to the module whose file holds the store and its
door, so a lookup written by hand anywhere else is refused as a private
access (E0353), and every other reader asks for the name as text to print
or reads the store by identity.  Locked so far:

| store | file | its door(s) | readers outside |
|---|---|---|---|
| the scope ribs (`Scope`, `ScopeEntry`) | `compiler/src/compiler/resolver/resolver.cryo` | `scope-value`, `scope-type` | by identity: `Scope::binding_count`, `binding_at`, `Resolver::visible_bindings` (a scope's rib chain), `Resolver::scope_at` (the scope around a position in a module); a module's own binding by leaf: `Resolver::module_binding` |
| an enum type's variants (`EnumVariantInfo.name`) | `compiler/src/compiler/types/user_defined.cryo` | `member-variant` (`EnumType::variant_index`) | text for a message: `EnumVariantInfo::display_name` |
| the declaration index's families and module globals (`overload_index`, `func_type_refs`, `refused_methods`, `overload_func_family`, `overload_func_mangled`, `module_global_leaves`, `module_global_extern_syms`, `extern_symbol_first`, `family_slot`) | `compiler/src/compiler/decl_index.cryo` | `member-function`, `member-global` | none |
| the interner (`strings`, `lookup`) | `compiler/src/compiler/resolver/intern_table.cryo` | `intern` | `resolve`, `shown` |
| a struct's or class's fields (`FieldInfo.name`) | `compiler/src/compiler/types/user_defined.cryo` | `member-field` (`StructType::field_position`, `ClassType::own_field_position`; `MemberResolver::field_of` asks them) | by position; the name sealed for a message or a synthesized node: `FieldInfo::decl_name` |
| the loader's namespace maps (`ModuleLoader.scanned_ns`, `ns_map`) | `compiler/src/compiler/module_loader.cryo` | `loader-path` | none |
| the module graph's names (`ModuleInfo.name`, `namespace_name`; `ModuleGraph.name_index`, `module_defs`, `def_list`, `def_paths`) and each module's import list (`ModuleInfo.visible`) | `compiler/src/compiler/module_graph.cryo` | `module-by-path` | by identity: `ModuleInfo::path`, `namespace_path`, `declares_namespace`, `ModuleGraph::imports`; a body path's modules through `ModuleGraph::modules_written_as`; text for a message, a mangled symbol or a manifest entry through `ModulePath::decl_name`; a declaration's canonical name, `module::leaf`, composed by `ModulePath::qualified` and nowhere else |
| the template registry's names (`TemplateEntry.name`) | `compiler/src/compiler/types/generic_registry.cryo` | none: the registry is asked by definition | text for a message: `TemplateEntry::display_name`; a mangled name is spelled from `leaf_name`, a `DeclName` |

<!-- doors -->
| door | function | file | kind |
|---|---|---|---|
| `scope-value` | `Resolver::lookup_value` | `compiler/src/compiler/resolver/resolver.cryo` | bare identifier to its binding, value namespace |
| `scope-type` | `Resolver::type_binding` | `compiler/src/compiler/resolver/resolver.cryo` | written type name, or a type path's first segment, to its binding in the writer's ribs; the name layer's `NameResolver::type_spelling_res` asks it and reads the binding's kind |
| `scope-type` | `Resolver::type_res_in_scope` | `compiler/src/compiler/resolver/resolver.cryo` | written type name's leaf, type namespace, walking outward from an explicit scope: a module's own scope, or the scope enclosing a class whose base it names |
| `member-function` | `DeclarationIndex::lookup_family_entries` | `compiler/src/compiler/decl_index.cryo` | member: a function or method family, by owner and leaf; a generic type's own methods under the template itself (`FamilyOwner::Template`) |
| `member-export` | `Resolver::exports_named` | `compiler/src/compiler/resolver/resolver.cryo` | member: a module's public declarations, by the module and leaf, read off its export list |
| `member-global` | `DeclarationIndex::global_in_module` | `compiler/src/compiler/decl_index.cryo` | member: a module's global, by the module and leaf |
| `member-field` | `MemberResolver::field_of` | `compiler/src/compiler/sema/member_resolver.cryo` | member: a field, by owner type and leaf |
| `member-field` | `StructType::field_position` | `compiler/src/compiler/types/user_defined.cryo` | member: a field of a struct in hand, by leaf, to its position among the struct's fields |
| `member-field` | `ClassType::own_field_position` | `compiler/src/compiler/types/user_defined.cryo` | member: a field of a class in hand, by leaf, to its position among the class's own fields |
| `member-variant` | `EnumType::variant_index` | `compiler/src/compiler/types/user_defined.cryo` | member: an enum variant, by owner type and leaf |
| `member-variant` | `EnumDecl::variant_position` | `compiler/src/compiler/AST/declaration.cryo` | member: an enum variant, by the enum's declaration and leaf, to its position among the declared variants - for a constant folded off the declaration |
| `trait-method` | `GenericRegistry::trait_item_slot` | `compiler/src/compiler/types/generic_registry.cryo` | member: a trait's declared method, by trait identity and leaf, to its position in the trait |
| `assoc-type` | `TraitDeclNode::assoc_type_index` | `compiler/src/compiler/AST/declaration.cryo` | member: a trait's associated type, by the trait's declaration and leaf, to its position among the trait's associated types |
| `module-by-path` | `ModuleGraph::module_named` | `compiler/src/compiler/module_graph.cryo` | written module path to its module |
| `module-by-path` | `ModuleGraph::module_path_over` | `compiler/src/compiler/module_graph.cryo` | an import's written path to itself as a path to list modules within, when the graph holds a module at it or under it, whether or not it names a module (`import std::`) |
| `module-by-path` | `ModuleGraph::modules_in_view` | `compiler/src/compiler/module_graph.cryo` | a path written in a body to the modules it names, among those the writing file can see: its own, the ones it imports, the prelude, or a package root written whole |
| `loader-path` | `ModuleLoader::resolve_import_path` | `compiler/src/compiler/module_loader.cryo` | a module path written in an `import`, an `export` or a `public module` to the file of the module it names: a vendored library, a file declaring the namespace, or a file at the path on disk |
| `loader-path` | `ModuleLoader::binding_of` | `compiler/src/compiler/module_loader.cryo` | a body path's head to the import of the file that binds it |
| `loader-path` | `ModuleLoader::sub_module_file` | `compiler/src/compiler/module_loader.cryo` | a path's continuation past an import binding to the file of the sub-module it names |
| `primitive` | `ResBase::is_primitive_spelling` | `compiler/src/compiler/resolver/res.cryo` | the fixed primitive table |
| `primitive` | `ResBase::primitive_of_alias` | `compiler/src/compiler/resolver/res.cryo` | the fixed primitive table: alias keywords |
| `primitive` | `ResBase::primitive_position` | `compiler/src/compiler/resolver/res.cryo` | the fixed primitive table: a primitive's position, which its definition is kept under |
| `lang-item` | `GenericRegistry::claim_wellknown` | `compiler/src/compiler/types/generic_registry.cryo` | the fixed language-item table |
| `lang-item` | `LangModule::is_written` | `compiler/src/compiler/resolver/lang_item.cryo` | the fixed table of modules the compiler imports on a program's behalf: whether an import's written path names one (the prelude a file already globs) |
| `lang-item` | `IntrinsicKind::from_name` | `compiler/src/compiler/intrinsic_kind.cryo` | the fixed intrinsic table: an `intrinsic function` declaration's name to the operation it lowers as |
| `intern` | `InternTable::intern` | `compiler/src/compiler/resolver/intern_table.cryo` | text to `SymbolStr` |
| `suggestion` | `find_best_candidate` | `compiler/src/compiler/diag/edit_distance.cryo` | "did you mean": result flows only into message text |
| `suggestion` | `Diagnostics::find_shadowed_type_candidates` | `compiler/src/compiler/sema/diagnostics.cryo` | "import the type you meant": the paths, as text, of the types whose last segment is a refused access's type spelling and which own its member; result flows only into message text |
<!-- /doors -->

What this table does not say: that the tree obeys rule three. Other
functions in `compiler/src` still look a spelling up - duplicates of a
door (`EnumType::get_variant` beside `variant_index`, the rib walks
`Resolver::lookup` and `lookup_prelude` beside `lookup_value`, the four
family readers beside `lookup_family_entries`) and lookups outside every
door. Those are conversions still to make: each becomes a door's body, a
caller of a door, or an identity lookup.

## Appendix: rulings

Decisions Jake made in plain text on questions the rules above leave open.
Each is stated as the behaviour it requires; whether the compiler has it
yet is recorded with the change that builds it, not here.

### 2026-10-17

405. **A `return` inside an await-free `unsafe` block or `static match`
     arm of an `async function` compiles,** fixed in its own change ahead
     of the remaining declaration kinds: every walk of the async lowering
     descends into both forms as it does into a block or a `match` arm.
     (Dispatch, delegated)

406. **`intrinsic const` is left as it is, parked for Jake:** reading one
     emits invalid IR, nothing is built on the form, and it is neither
     deleted nor given a value source until he decides.  (Dispatch,
     delegated)

407. **The error code for the removed class syntax (ruling 388) and the
     trait-object symbol spelling (ruling 391) are chosen by whoever
     implements those slices,** and reported.  How verify treats a moved
     project object is likewise the implementer's call if it blocks the
     work, and otherwise stays as it is.  (Dispatch, delegated)

408. **A `break` or `continue` inside an `unsafe` block or a `static
     match` arm that leaves a loop containing an `await` stays refused
     with E0600,** as built; the edge is not lowered.  (Dispatch,
     delegated)

409. **The generic registry records each template's kind as a kind of its
     own** (`TemplateKind`: function, struct, union, class, enum, trait,
     type alias) rather than the declaration's `NodeKind`, converted in a
     change of its own that moves no compiled output, before any type
     declaration converts to a payload.  (Dispatch, delegated)

410. **The cloner's uncopied fields on intrinsics, imports and extern
     blocks are low priority:** how often each of those kinds is cloned is
     counted first; if any is, every field is copied and a test added; if
     none is, that is recorded and the defect stays open.  (Dispatch,
     delegated)

411. **The three places a type alias is not transparent** - calling a
     value typed by a function-type alias, an operator on a call returning
     an alias, and a struct literal through a generic alias - are fixed as
     one change after every declaration kind but classes has converted to
     a payload, not before.  (Dispatch, delegated)

412. **A comparison between unrelated types is refused with E0200:** two
     values of different enum types, or two pointers neither of which
     converts to the other (pointers to unrelated structs among them).
     Built as a change of its own, with a compile-fail test for each.
     (Jake)

413. **Class declarations are not converted to a declaration payload:** the
     class feature is deleted outright, so a class stays a subclass behind
     the declaration bridge until then.  (Jake)

414. **`intrinsic const` is replaced by the declared intrinsic function
     `location() -> Location`** in `std::core::intrinsics`.  A call
     evaluates to its own place - file, line, column and the readable
     qualified name of the enclosing function - and in a
     `![caller_location]` body to its caller's, the same `Location` that
     function is passed.  `FILE` and `LINE` migrate to it and are
     deleted; the `intrinsic const` form is deleted and the parser
     refuses it.  `Location` is read-only to programs: the standard
     library's visibility pass closes construction.  (Jake)

415. **Borrowed trait objects are built after the class feature is
     deleted,** not before it as ruling 384 had it.  The table emission
     copies the few dozen lines of the class vtable code it needs rather
     than sharing code with the class path, which is deleted.  (Jake, on
     the trait-object audit)

416. **A trait object's type is a type kind of its own,** not a pointer or
     reference to a trait.  (Jake, on the trait-object audit)

417. **A method is callable on a trait object only if `This`, the
     concrete type, appears in its signature as the receiver alone.**  Any
     other use of `This` - a parameter, a return, a type argument - makes
     that method unavailable on the object; the trait is still usable as an
     object through its other methods.  (Jake, on the trait-object audit)

418. **Every trait-object table starts with three reserved words: drop,
     size and alignment,** before any method slot, so owning boxes after
     v1.0 need no change to the table's shape.  (Jake, on the trait-object
     audit)

419. **A table holds the supertraits' slots first,** then the trait's own
     methods.  (Jake, on the trait-object audit)

420. **A table is emitted in the module that holds the implementation** of
     the trait for the type.  (Jake, on the trait-object audit)

421. **The trait-object codes and symbol:** E0314, a method unavailable on
     an object; E0315, a conversion to an object whose type does not
     implement the trait, or implements it generically; E0316, `virtual`
     written on something that is not a trait, or an object in an
     `extern` signature.  A table's symbol is `C$vt$<trait>$f<type>`.
     (Jake, on the trait-object audit)

422. **A trait's associated types are bound positionally in an object
     type,** as type arguments in declaration order: `virtual mut
     &Iterator<i32>`.  (Jake, on the trait-object audit)

423. **Printing goes through objects by a non-generic formatting method**
     taking a `virtual mut &FmtWrite` formatter, added in the standard
     library's visibility pass.  (Jake, on the trait-object audit)

424. **A trait that declares two methods with the same name is refused**
     where the trait is declared.  (Jake, on the trait-object audit)

425. **Converting a value whose trait implementation is generic to an
     object is deferred,** and revisited before the language freeze.
     (Jake, on the trait-object audit)

426. **A raw object pointer `virtual T*` accepts `null` and compares only
     with `== null` and `!= null`;** a borrowed object never compares.
     (Jake, on the trait-object audit)

427. **A trait object satisfies its own trait wherever the trait is
     required.**  The compiler makes the implementation: a method without a
     default calls through the table, and a default method is compiled
     with `This` as the object type.  (Jake, on the trait-object audit)

428. **The concrete type behind `implement Trait` in type position is
     hidden:** only the trait's methods are visible through it.  Built in
     the slice and API pass before v1.0.  (Jake, on the trait-object audit)

429. **A function's `-> implement Trait` promise is checked at the
     function itself,** not at its callers.  Built in the slice and API
     pass.  (Jake, on the trait-object audit)

430. **`implement Trait` in a parameter, a field, an alias or a type
     argument is refused with a message of its own** suggesting a bounded
     type parameter or `virtual &Trait`, instead of an unrelated error or
     `void`.  Built in the slice and API pass.  (Jake, on the trait-object
     audit)

431. **`panic` takes a `Location` instead of separate file and line
     arguments,** so the column and the function name reach the panic
     message.  Built as a change of its own with a pin refresh.  (Jake, on
     class-removal session 9's questions)

432. **`Location.function` is the full qualified path of the function**
     (`App::Main::Probe::own`); a lambda, closure or async body is named for
     the function it is written in, a specialization for its generic.
     (Jake, on class-removal session 9's questions)

433. **The unrelated-types refusal (E0200, ruling 412) applies to every
     comparison operator,** not only `==` and `!=`, as built.  (Jake, on
     class-removal session 9's questions)

### 2026-10-16

379. **A fixed-size array does not convert implicitly to a dynamic `[T]`:**
     `const d: [i32] = buf;` with `buf: [i32; 3]` stays refused with E0200.
     (Dispatch, delegated)

380. **The demangler prints the full `Array<...>` type a `[T]` symbol
     names** - `std::collections::array::Array<i64,
     std::alloc::allocator::GlobalAlloc>` - rather than sugaring it back to
     `[i64]`.  (Dispatch, delegated)

381. **A call-site argument error spells a dynamic array `[T]`;** only
     declaration-site text follows the written spelling (ruling 369).
     (Dispatch, delegated)

382. **An AST node is a header struct of the fields every kind shares plus
     a `kind` enum of pointers to per-kind structs** (`Expr { span, ...,
     kind: ExprKind }`), not a bare enum value as the handle: identity stays
     the address, an absent child stays `null`, and the header is a field.
     (Jake)

383. **Each node struct carries its own small `Allocate` implementation**
     over the AST arena, about 75 of them, rather than factories that
     allocate from the arena directly.  (Jake)

384. **Borrowed trait objects are built inside the class removal:** after
     the compiler is free of classes and before the class feature is
     deleted, reusing the class vtable emission rather than deleting and
     rewriting it.  (Jake)

385. **Declarations and expressions convert kind by kind under a temporary
     bridge variant; patterns and statements convert all at once.**  The
     bridge is temporary in fact: each category ends with its bridge
     deleted, every bridge piece is tracked until it is, and the final
     audit finds none left.  (Jake)

386. **Tests of the class feature are deleted with it; tests that use a
     class incidentally are ported to structs.**  (Jake)

387. **Constructors and destructors written in a struct body are removed
     along with classes;** a struct is built by a `static` factory and
     released by `Drop`.  Their use is measured before they go.  (Jake)

388. **The removed class syntax is refused with one new error code,** with
     a help line per form saying what to write instead.  (Jake)

389. **The visitors' side channels become return values in a commit of
     their own,** after the dispatch change and apart from it.  (Jake)

390. **`TypeKind` is derived from the type's data as a follow-up** to the
     type conversion, not in it.  (Jake)

391. **The trait-object error codes and the vtable symbol's spelling are
     chosen by the implementation** and reported.  (Jake)

392. **The language server's yield hook becomes a `virtual YieldHook*`**
     once trait objects exist, as the compiler's own use of them.  (Jake)

393. **A supertrait's methods are callable directly on a subtrait
     object;** the subtrait's table holds the supertrait's slots.  (Jake)

394. **`protected` is removed** with classes.  (Jake)

395. **`![caller_location]` on a variadic function or method is refused
     (E0151)** where the directive is written.  The hidden location
     follows the written arguments, and a variadic call's written
     arguments have no fixed end.  (Dispatch, delegated)

396. **`delete p` releases `*p` as a scope exit releases a value of the
     pointee type:** its `Drop`, if it has one, then the values its fields
     own; then the storage is returned.  (Dispatch, delegated)

397. **A struct's or union's own methods, static and instance, are written
     inline in its body.**  An `implement` block is for implementing a trait
     on a type, for an enum's methods (an enum body holds only variants),
     and for the cases the language requires one.  A type whose methods
     are split across several files (the parser's `Parser`) keeps
     `implement struct` blocks in the other files, rather than growing one
     huge file; the methods in its own file are inline.  A bound only some
     methods need is a `where` on those methods, which works inline on a
     generic struct.  (Jake)

398. **A `match` whose arms test constants dispatches through one
     `switch`,** built as its own change ahead of deriving `TypeKind`
     (ruling 390), which then follows.  Every object with such a `match`
     moves, so verify is re-baselined on it.  (Jake)

399. **A method's name read as a field is refused by the type check with
     E0204,** with a help line naming the call: "`kind` is a method; call
     it: `t.kind()`".  (Jake)

400. **The stamp checker's planting test switch plants a name of any
     kind,** not only an identifier, and the enum-pattern check has a
     committed test through it.  (Jake)

401. **`??` and `for` accept only the language's `Option`;** a program's
     own enum spelled like it is refused (E0200).  (Jake)

402. **Statements convert as the class-removal plan designs them,** with
     the node split's memory cost (about 3% of the arena) accepted.
     (Jake)

403. **`Type` keeps its stored `kind` field.**  Deriving it from the
     payload (ruling 390) waits until the compiler itself is built with
     LLVM optimisation, which folds a `match` yielding constants into a
     table load; built unoptimised, the derived read measured 5.5% slower
     with `![inline]`.  The roadmap carries the two steps.  (Jake)

404. **A method's name read as a field is fixed by a suggestion showing
     the corrected line** (`t.kind()` under the source line), with no
     separate plain help line.  This settles the wording ruling 399 left
     open.  (Jake)

### 2026-10-15

347. **Fixed-size arrays change syntax with dynamic ones:** `T[N]` is
     written `[T; N]`, so the language has one array syntax.  (Jake)

348. **The postfix array forms are refused once the tree is migrated,**
     with no period in which both are accepted.  (Jake)

349. **Diagnostics, the language server, the demangler and generated
     bindings print array types in the circumfix form,** `[T]` and
     `[T; N]`.  (Jake)

350. **`[T]` means exactly the type `T[]` names today,** so the change is
     pure syntax, provable with no compiled output changed.  Unifying the
     builtin dynamic array with `Array<T>` is a separate step, done before
     the class removal.  (Jake)

351. **`new u8[n]` keeps its spelling:** it is an allocation expression
     yielding a pointer, not an array type.  (Jake)

352. **A postfix array type is refused with "array types are written
     `[T]`: write `[i32]`"** (filled in with the type actually written) and
     an automatic fix, under a code of the existing parser error family
     rather than a new one.  (Jake approved the wording; the code choice was
     delegated)

353. **`[i32, i32]` keeps its message asking for `(T, U)`** once `[` starts
     an array type.  (Jake)

354. **The rulings stay as written records:** an old spelling inside a past
     ruling is not respelled.  (Jake)

355. **The pins are refreshed after a change to what the compiler accepts or
     what it compiles a program to.**  A change only to text the compiler
     prints needs no repin.  (Jake)

356. **The refused postfix array type is E0104** ("expected type"), worded
     as 352 states, with the type actually written filled in, and carries
     its bracketed spelling as an automatic fix.  (Jake)

357. **Code comments that spell an array type postfix are respelled** in
     the brackets once the postfix form is refused.  (Jake)

358. **A struct field passed by value inside an `if` condition is not
     released twice:** the defect is fixed on the branch that changes the
     array syntax, where it was found.  (Jake)

359. **The demangler's output is pinned by a test.**  (Jake)

360. **Trait objects come before v1.0, borrowed only:** `virtual &T`
     (285, 288, 289), right after or folded into the class removal, built
     on the class vtable machinery.  Owning boxes, upcasting and generic
     implementations coerced to an object come after v1.0.  (Jake)

361. **The refused postfix fixed array keeps the wording of 352 exactly:**
     `u8[4]` reads "array types are written `[T]`: write `[u8; 4]`".
     (Jake)

362. **A value moved in a loop's condition, or in any expression the loop
     evaluates again at its back edge, is refused** with the existing
     loop-carried report ("moved inside a loop is used again on the next
     iteration").  (Jake)

363. **`[T]` means exactly `Array<T>` with the default allocator.**  An
     array with another allocator is written `Array<T, A>`; `[T]` never
     names one.  (Jake)

364. **`Array<T>` is indexed through `Index` implementations in the
     standard library,** not by extending the compiler's built-in
     indexing: `xs[i]` on an `Array<T>` is `*(xs.index(i))`.  (Jake)

365. **The `length` field of an array stays readable for now;** whether it
     stays public is decided in the standard library's visibility pass.
     (Jake)

366. **`&[T]` stays a reference to the owning array,** and a borrowed view
     stays `Slice<T>`.  (Jake)

367. **The change that renames the dynamic array's symbols is proved by a
     one-off comparison** of the compiled output with old symbol names
     mapped to new ones; the comparison script is deleted in the next
     commit.  (Jake)

368. **The compiler and the standard library use Cryo's own language
     features wherever one fits** - `Eq`, `Index`, `Drop`, `Allocate` and
     the other traits - rather than an ad-hoc method of the same purpose,
     so that the compiler exercises the whole language in live code.
     (Jake)

369. **Editor and diagnostic text spells a type the way the user wrote
     it:** an annotation written `[i32]` displays as `[i32]`, one written
     `Array<i32>` as `Array<i32>`.  The spelling is remembered per
     annotation, for display only; it never takes part in identity, keys
     or resolution.  A type with no written spelling displays as `[T]`
     when its allocator is the default and as `Array<T, A>` otherwise.
     (Jake)

370. **A function marked `![inline]` is copied into every module that
     calls it,** so the call can be inlined there; `Array`'s `index` and
     the like are marked `![inline]`.  (Jake)

371. **`p[i]` on a pointer to an array is pointer arithmetic,** as on any
     other pointer; indexing does not look through a pointer to an
     array.  (Jake)

372. **An attribute makes a function report its caller's source location
     in a panic,** and the standard library's index bounds check carries
     it, so an out-of-range index reports the `xs[i]` that wrote it.  The
     attribute's name is proposed with the change and confirmed by Jake.
     (Jake)

373. **Emitting only the methods of an instantiated generic that are
     used** is scouted: built if the work is moderate, recorded as an
     accepted cost otherwise.  (Jake)

374. **A fill literal `[v; n]` is refused unless its element type is a
     plain-copy type,** with an error saying so: the one value would
     otherwise be released `n` times.  (Jake)

375. **`Array`'s `length` and `capacity` are `i64`,** the type the
     built-in `[T]`'s `length` had and every array in the tree was written
     against.  The change is confined to `array.cryo` and the code that
     reads those two values.  (Jake)

376. **Taking a `![caller_location]` function as a value is refused,** and
     the refusal is made early enough that the editor and `cryo check`
     report it, not only a build.  (Jake)

377. **A refused fill literal keeps code E0453 and its wording.**  (Jake)

378. **The attribute that reports the caller's location is named
     `![caller_location]`.**  (Jake)

### 2026-10-14

333. **One project's analysis covers every target of its cryoconfig, and
     the target whose view it takes is chosen by the file opened or saved
     last.**  This stays until a real project needs a separate view per
     target.  (Jake)

334. **The editor's identity lookups come before its definition and hover
     rebuild,** and their work starts by measuring that the name layer's
     stamps are filled in when the editor runs the compiler.  (Jake)

335. **Hover on a name a pattern binds labels it a binding** - a match arm
     `Ready` that binds the matched value hovers as `binding Ready: i64`, at
     the arm and at every use - not as a `const`.  (Jake)

336. **The editor's workspace symbol search may match the typed query
     against declaration names.**  It is a text search the user asked for,
     not name resolution: nothing it finds is used as a binding, so it is
     not a door and breaks no rule above.  (Jake)

337. **Find-references builds its index of uses per request,** with no
     cache, until a measurement on the compiler project shows that it needs
     one.  (Jake)

338. **Work on a branch does not run or wait on CI;** the local gates are
     its proof, and CI is watched on `main` only.  (Jake)

339. **The editor's completion goes only through the compiler's lookups.**
     The typed prefix is not matched as text against module names; the
     text search ruling 336 allows stays limited to workspace symbol
     search.  So `import Probe::` with `Probe` only a prefix of real
     modules offers nothing, until the module graph can list a module
     path's children by identity through a door.  (Jake)

340. **The compiler fields `ASTNode.generated` and
     `BuildOptions.yield_hook` keep their names.**  (Jake)

341. **The names inside an import's braces get their positions and the
     definitions they name before the editor's rename is built,** so a
     rename reaches every import of what it renames.  (Jake)

342. **The suggestion for a module that is already imported reads
     "`Probe::A` is already imported; write `A::Item`"** and is not applied
     automatically.  (Jake)

343. **The language server's `serverInfo.version` from the build stamp
     stays on the roadmap;** it needs a matching change in the VS Code
     extension.  (Jake)

344. **Signature help reads the typed text only for its structure** - which
     call the cursor is in, and which argument.  It resolves the callee
     through the compiler's doors in the scope at the cursor, as completion
     resolves a path, and takes the signature from the callee's identity;
     it never matches the callee's text against names.  It shows every
     overload and highlights the one that fits the argument count so far.
     (Jake)

345. **Ruling 329 is amended: the editor analyses the standard library
     once per server process** and reuses it as a frozen base layer, and in
     the editor's mode the passes the editor does not need are skipped.
     Whole-module reuse between saves stays on the roadmap, folded into the
     intermediate representation work with stable identities.  (Jake)

346. **The editor's text prints names relative to the cursor's scope, by
     identity, and leaves out defaulted type arguments.**  Hover, signature
     help, completion details and inlay hints write each definition and
     type in the shortest form that names the same identity from the
     cursor, decided through the doors and compared by identity, never by
     trimming text; a short name that would name something else there, or
     nothing, keeps enough of its path.  A type argument equal to the
     default its template declares is left out.  The expanded hover keeps
     every path in full.  (Dispatch, delegated, at Jake's request)

### 2026-10-13

299. **Before the merge, the scratch directories are deleted:** `.objcmp/`,
     `compiler/build-b2`, `compiler/.selfprobe`, `.cross-check`, `scratch`,
     `dist` and `.verify` - only after an inventory confirms each is
     untracked, regenerable scratch.  (Jake)

300. **The compiler's warning count is reduced before the merge.**  (Jake)

301. **Calling an overloaded generic static method on a non-generic owner
     (`Plain::conv(7, 9)`), which crashes with internal error E0900, is
     fixed before the merge,** like the generic static call of ruling 250.
     (Dispatch, delegated)

302. **E0313's code and wording stay:** "`Shape` is a trait, so `&Shape` is
     not a type", with the `virtual &Shape` note and the
     bounded-type-parameter help.  (Dispatch, delegated)

303. **Ruling 294 stays as built:** no class with a base class may
     implement `Allocate`.  (Dispatch, delegated)

304. **A bare trait used as a value type (`p: Shape`) is refused with E0313
     where it is written.**  (Dispatch, delegated)

305. **E0458's wording stays:** "a capturing closure cannot be held by a
     variable of function type ...".  (Dispatch, delegated)

306. **The standard library's explicit destructor calls that release a
     field or a match binding inside a destructor are removed;** each field
     is then released when the destructor finishes.  Every site is checked on
     its own, and a site whose release order is observable is kept and
     reported instead.  (Dispatch, delegated)

307. **The explicit-destructor-call warning and the language document say
     what the compiler does for a local:** `x.drop()` on a local stops its
     release at scope exit and stops later use of it.  (Dispatch, delegated)

308. **The editor's purely mechanical warnings are fixed when there is
     room** - an explicit drop of a local becomes `mem::drop(x)`, and a
     literal suffix the declared type already gives is removed - proved as
     the standard library's were.  This is not the editor's rebuild.
     (Dispatch, delegated)

309. **Moving a field of `this` out inside a destructor is refused** - for
     example `mem::drop(this.inner)` in `drop` - because field glue releases
     the field again when the destructor returns.  Full move checking, which
     refuses every move out from behind a reference, waits until after the
     merge.  (Jake)

310. **A free-function call that a generic and a non-generic overload both
     accept is refused with E0154,** the same as a method or a static call;
     the non-generic overload is not chosen silently.  (Jake)

311. **The remaining explicit `.drop()` calls in the standard library and the
     editor are fixed before the merge,** each site checked on its own as
     before.  (Jake)

312. **`Child::wait` and `Child::join` close stdin without moving the field
     out of `this`,** leaving `None` behind, and are fixed together with 309.
     (Jake)

313. **No more fixes before the merge.**  Known bugs are logged in
     `.bug-report/` and fixed on later branches.  (Jake)

314. **`.bug-report/` at the repo root is gitignored.**  Each bug gets its
     own markdown file named `NNNN-short-name.md`
     (`0001-generic-class-allocate-link-error.md`), numbered in order.  A
     file is deleted when its bug is resolved.  (Jake)

315. **Retention, written into `CLAUDE.md` as rules for agents:** anything
     temporary (debug scripts, probes, scratch builds, logs, codemods) has
     a stated deletion point and does not litter the repo; scratch lives
     outside the repo or in an ignored scratch location and is deleted when
     the work ends; a one-off codemod is deleted in the very next commit
     (ruling 268); a bug file is deleted when its bug is fixed; no dead code
     or dead scripts are left behind.  (Jake)

316. **The first project after the merge is the editor's rewrite,** on a
     new branch `lsp-rewrite`, before the type-syntax update and the class
     removal.  (Jake)

317. **The last fix session's five open questions are logged, not acted on
     now:** assignment through a reference releasing nothing, adding
     `mem::replace` / `Option::take`, making W0015 an error, a bare call
     only the generic overload accepts being refused, and static-method
     ties ignoring `where` clauses - as bugs in `.bug-report/` or items in
     `docs/roadmap.md`.  (Dispatch, delegated, per ruling 313)

318. **A vendored library's module is named `vendor::<Name>`,** the path
     its import writes, whatever namespace the generated binding declares.
     `import vendor::RayLib;` names it whole and `RayLib::InitWindow`
     reaches its items, as with any import.  The binding generator and the
     written import are unchanged; the loader names the module it reached
     through a `vendor::` import.  (Jake, choosing this over making the
     generator declare `namespace vendor::<Name>` or changing how a vendor
     import is written)

319. **The Windows release zip ships a Windows stdlib archive, built on the
     Linux release runner** for the windows triple.  The release's wine
     smoke test links with llvm-mingw's `clang.exe`, which the job already
     fetches and selfhost-check's wine arm already uses.  (Jake)

320. **selfhost-check's wine arm is made to run** by putting `LLVM-C.dll`,
     and whatever else it needs, where wine finds it, and is then measured
     again on CI; ruling 270 had the arm measured on CI before its fate is
     decided.  (Jake)

321. **On CI, the object comparison runs against the previous commit and
     reports its differences without failing the job.**  (Jake)

322. **A compiler flag that stops a build at object files,** so cross-check
     can run on a Linux host, goes on the roadmap; it is not built now.
     (Jake)

323. **The release smoke test stays wine-only for now;** a test of the
     Windows zip on a real Windows host is added before the first tagged
     release.  (Jake)

324. **The editor's behavioural harness grows with each step of its
     rewrite,** and each step's commit shows the previous server failing
     the checks it adds.  (Jake)

325. **The editor's new lookups by identity are named
     `DefTable::span_of`, `DeclarationIndex::declaration_of`,
     `Resolver::scope_at` and `Resolver::visible_bindings`.**  (Jake)

326. **The index of every use of a definition lives in the editor,** built
     from the stamps the name layer writes.  (Jake)

327. **An `ImportDeclNode` records the module its import resolved to.**
     (Jake)

328. **A qualified type name's span covers its whole path,** and every
     negative test whose annotated columns move is re-annotated.  (Jake)

329. **In the editor's first release, module caching is a design only:**
     each save analyses the whole project, and a hook in the compiler lets
     the editor answer from its last analysis while a long one runs.
     (Jake)

330. **The editor gets parsing that continues past an error only after its
     first release,** and only if the last good analysis proves not to be
     enough.  (Jake)

331. **A file outside any project is analysed as its own entry point.**
     (Jake)

332. **The suggestion to import a module that is already imported stops
     being machine-applicable,** and suggests the qualified name instead.
     (Jake)

### 2026-10-12

247. **The compiler's `![allow]`s that named the deleted lint, and the
     door doc-comment markers, are deleted now.**  `![allow]` stays a
     language feature; the editor's allows wait for its rebuild after the
     merge (ruling 184).  (Jake)

248. **`x as boolean` is refused for a float as for an integer:** nothing
     but a `boolean` converts to `boolean`, so a test on a float is
     written `x != 0.0`.  (Jake)

249. **Allocation goes through a trait.**  `new T(...)` is sugar for an
     `Allocate` trait, as `a[i]` is for `Index` and `*p` for `Deref`.  A
     class that implements `Allocate` gets its memory from it, and its
     subclasses inherit it: the nearest ancestor's implementation wins.
     The trait carries a matching release method, so `delete` hands the
     memory back the same way; for an arena it is typically a no-op.
     `Allocate` replaces the `![arena(f)]` directive entirely, and
     `ASTNode` implements it over the AST arena.  The allocator's
     signature is checked by the type checker, as any trait method's is.
     A context allocator (an allocator scope) may come later; a class's
     own `Allocate` wins over it.  Not built now.  (Jake)

250. **Calling a generic type's generic method with explicit type
     arguments, `Wrap::<i32>::conv::<u8>(7)`, is fixed before the merge,**
     in the spirit of ruling 243: known defects are fixed before the merge,
     as the last tasks.  (Dispatch, delegated)

251. **The ledger (`docs/name-resolution.md`) is retired now,** with its
     status check, its commit hook, `install-hooks`, and the Makefile and
     verify wiring that served only them, as ruling 106 required before
     the merge.  (Jake)

252. **Every trait implementation is checked against its trait before the
     merge:** a missing method, a method the trait does not declare, and a
     parameter, receiver or return type that differs from the trait's are
     each refused, under an error code of their own.  A method the trait
     gives a default body is not missing.  (Jake)

253. **Casting a class value to a pointer (`av as void*`) is refused;**
     the address is written `&av`.  (Jake)

254. **A type alias reaches its type's static methods:** with `type PI =
     Pl<i32>;`, `PI::plain(8)` is `Pl<i32>::plain(8)`.  Fixed before the
     merge.  (Jake)

255. **A method call with explicit type arguments inside a generic
     function works when the method is declared in an `implement` block,**
     as it does when the method is in the type's body: `w.conv::<u32>(5)`
     inside `function thn<A>(w: Wrap<A>)`.  Fixed before the merge.  (Jake)

256. **`Allocate`'s release method keeps its default body, which does
     nothing.**  (Jake)

257. **A struct may implement `Allocate`;** with no inheritance, only that
     struct's own `new` goes through it.  (Jake)

258. **Casting a class value up or down its hierarchy stays refused;**
     casting a pointer along the hierarchy is allowed.  (Jake)

259. **The ledger's status rows go with it,** the ones expecting zero
     included; none is ported to another check.  (Jake)

260. **What `import std::future;` brings into scope is settled by what the
     compiler does, checked first, and `docs/cryo.md` is made to match the
     rule** that a plain import binds no names (D7).  (Jake)

261. **The wording of the cast errors stays as it is, and E0307 stays the
     error for too many type arguments.**  (Jake)

262. **E0312, the trait-implementation check, keeps its number and
     wording.**  (Jake)

263. **`const a: i32 = i32::MAX;` storing nothing, so the local is read
     uninitialized, is fixed before the merge.**  (Jake)  No type declares
     constants, so the path names no value: it is refused where it is
     written (E0201), as any path naming nothing is; a primitive's bounds
     are its `min_value()` and `max_value()`.

264. **A generic impl method with a parameter typed `&This` is found when
     called,** as it is with the parameter written `&Wrap<T>`.  Fixed
     before the merge.  (Jake)

265. **A generic alias used as a path (`type Stack<T> = Array<T>;
     Stack::<i32>::new()`) is refused with a clear error for now;** full
     support is on the roadmap.  (Jake)

266. **CI is aligned with `make verify`'s gate list first thing after the
     merge.**  Until then the difference is written down in the merge notes
     (`docs/roadmap.md`).  (Jake)

267. **Session scratch under `.objcmp/` is never tracked.**  The files that
     were committed there are untracked and stay on disk.  (Jake)

268. **A one-off codemod is deleted in the commit right after the one that
     ran it.**  The commit that ran it carries the generator; the history
     keeps it from then on.  (Jake)

269. **`tools/CryoLSP/tests/smoke.py` stays, and is wired into the LSP
     check after the merge.**  (Jake)

270. **selfhost-check's wine path is measured on the CI runner after the
     merge, before its fate is decided.**  (Jake)

271. **A project test may compare a program's output against a file,**
     through a `test.json` field named `stdout_file`.  After the merge.
     (Jake)

272. **Right after the merge, `cryo test` prints each suite's count and
     lists every suite,** so the census and roster scripts can retire.
     (Jake)

273. **`scripts/` is restructured into `gates/`, `pin/`, `release/`,
     `toolchain/` and `dev/` after the merge,** not before.  (Jake)

274. **`make install-lsp` stays in the Makefile,** and its help line says
     that it replaces the installed VS Code extension.  (Jake)

275. **Primitives keep `max_value()` and `min_value()` for now;**
     associated constants such as `i32::MAX` are roadmap work for after the
     merge.  (Jake)

276. **The wordings of the class-value cast error and the generic-alias
     path error stay as they are.**  (Jake)

277. **A plain import reaches a submodule its target re-exports whole:**
     after `import std::future;`, `waker::Waker` names the re-exported
     module's type, as `docs/cryo.md` states.  (Jake)

278. **The release build stamps the static Linux binary it ships.**  (Jake)

279. **`make verify-pin` also checks that each pin's stamped commit is the
     commit its sidecar names.**  (Jake)

280. **On Windows, `make clean` fails, naming what it could not delete,
     when a running program holds a file.**  (Jake)

281. **The Makefile keeps its short list of targets that do not need the
     host triple.**  (Jake)

282. **On Windows, the targets that only refuse (`examples-golden`,
     `valgrind-check`, `verify-freestanding`) do not build the compiler
     first.**  (Jake)

283. **The merge notes stay at the top of `docs/roadmap.md`.**  (Jake)

284. **`.objcmp/` is deleted entirely as the last step before the merge,**
     by Jake.  (Jake)

285. **Trait objects, when they come, are spelled `virtual &Shape` and
     `virtual Shape*`:** a prefix, as `mut &this` is.  There is no `dyn`.
     (Jake)

286. **Trait objects are built after the class removal,** possibly after
     v1.0.  (Jake)

287. **A trait used as a pointer or reference type (`p: &Shape`,
     `p: Shape*` with `Shape` a trait) is refused where it is written.**
     (Jake)

288. **Object safety is per method:** a method that cannot go in a vtable
     is unavailable on the object, and the rest of the trait still works.
     (Jake)

289. **Trait objects start with borrowed pointers only;** owning boxes come
     later.  (Jake)

290. **The class removal converts the AST straight to enums and `match`.**
     (Jake)

291. **The known class defects are left alone, since classes are going:**
     `override` is not checked, a missing vtable slot is not reported, and
     vtable slots are matched by name.  (Jake)

292. **Before the merge: an overloaded generic static method taken as a
     value is chosen like a function's or refused; type arguments written
     on an alias with no parameters are refused; an `Allocate`
     implementation for an enum is refused.**  `Allocate` on a generic
     class failing to link is left for the roadmap.  (Jake)

293. **A capturing closure stored in a function-typed variable, field or
     return value is refused (E0458),** where it compiled and crashed.
     Supporting it is roadmap work.  (Jake)

294. **`Allocate` on a class whose base class gets its memory from another
     allocator is refused,** so `delete` through a base pointer never
     releases through an allocator that did not hand out the memory.
     (Jake)

295. **`delete` through a base-class pointer runs the static type's `Drop`,
     not the derived class's.**  The spec says so; it is not fixed, since
     classes are going.  (Jake)

296. **The merge into `main` is a `--no-ff` merge commit,** when Jake
     authorizes it.  (Jake)

297. **Old commit messages are left as they are.**  (Jake)

298. **`make verify --baseline` against a commit from before the version
     stamp, `main` included, is fixed after the merge.**  (Jake)

### 2026-10-11

227. **The cleanup that removes the Python gates also removes
     `--emit=facts`, the compiler-internal `![allow]`s and the internal
     lint layer they serve.**  The structural locks stay: private fields,
     typed keys and `DeclName`.  Recorded only; a later change plans and
     does it.  (Dispatch, delegated)

228. **The check that follows what an override answers to a call made
     through the base lands, and the lane gate's pinned `LOOKUP_OTHER`
     rises from 35 to 36 for its one `member-function` door call.**
     (Dispatch, delegated)

229. **`LangModule::is_written` keeps the `lang-item` door id,** as the
     fixed tables ruling 225 names do.  (Dispatch, delegated)

230. **Name resolution is called complete only after the two known holes
     are closed** - what an override answers through the base, and
     `GenericRegistry::annotation_unifies` comparing a definition's path
     as text - **and a final read-only audit passes.**
     (Dispatch, delegated)

231. **Five defects the final audit found are fixed before any slice of
     the cleanup ruling 227 names:** overloaded `async` functions all
     calling one body; `a[i]` and a `Deref` field read finding their
     method by name rather than through their trait; a string `+`
     finding `append` by name in code generation; the `printf` format
     check deciding by the name a call is written with; and the type
     objects' path held as open text.  Sealing that path is the
     structural replacement for the identity-text lint once the lint
     is removed.  (Dispatch, delegated)

232. **An enum converts implicitly to `boolean` only when it is a simple
     enum:** no payload on any variant and no type parameters.  A variant
     whose type arguments cannot be inferred is refused in the type
     check, not left for code generation.  (Jake)

233. **A string literal keeps all its bytes and its length end to end,**
     so `"a\0b"` is three bytes.  (Jake)

234. **The link-symbol rename that puts a trait's arguments into its
     methods' symbols stays.**  (Jake)

235. **Name resolution is called complete** now that the final audit's
     fixes and the type-path seal have landed.  (Jake)

236. **The self-checks are deleted with the lint layer:** E0157 (its
     number stays reserved), the typed-key lint, and the door-id and
     store-key lints.  The E0900 stamp check is not a lint and stays
     always on, with no flag.  The wording question about the refused
     link-symbol message is moot.  (Jake)

237. **AST classes are routed into their arena by a marker on the AST
     base class,** not by the `compiler::` path, and no cryoconfig flag is
     added.  After this nothing treats the `compiler::` namespace
     specially.  (Jake)

238. **The door doc-comment markers are deleted along with the
     allows.**  (Jake)

239. **The 15 test projects that exist only for the lints are deleted
     along with the lints.**  (Jake)

240. **`\a \b \f \v` are supported in string literals as well as
     characters,** and the language spec documents them.  (Jake)

241. **The language spec documents `\u{...}` escapes,** noting that what a
     `char` holds above 255 depends on the open decision on `char`'s
     width.  (Jake)

Noted with no ruling: the identity-text check does not follow a number
taken from `.spelling().id` used as a key.  It is left as it is, since the
check is being deleted and the seals are what hold.  (Jake)

242. **Enums are strict: no enum ever converts implicitly to `boolean`,**
     simple or not.  A test on one is written `c == Color::Red`.  This
     amends the conversion half of 232; its other half stands - a variant
     whose type arguments cannot be inferred is refused in the type check
     (E0307).  (Jake)

243. **Five known defects are fixed before the merge, as the last tasks
     after the cleanup:** an overloaded generic used as a value takes the
     first overload; `p as SomeClass` passes the type check and then fails
     in LLVM; arrays of function values do not work and
     `((i32) -> i32)[]` does not parse; a generic method cannot be used as
     a value; and the closure synthesizer's leftover inherent `drop`
     (ruling 78).  Recorded only.  (Jake)

244. **`const n = Option::None;`, with no type arguments determinable
     anywhere, is refused** (the E0307 family).  (Jake)

245. **A variant's payload is checked against the type arguments written
     on it:** `Result::<i32, i32>::Ok("x")` and
     `Result::Ok::<i32, i32>("x")` are refused.  (Jake)

246. **Booleans are strict.**  An `if`, `while` or loop condition, the
     operands of `&&`, `||` and `!`, and anything assigned or passed to a
     `boolean` must be exactly `boolean`.  No integer, `char`, pointer,
     enum, `Option` or `Result` is ever truthy.  `boolean as` an integer
     type stays allowed (0 or 1); an integer `as boolean` is refused
     (write `n != 0`); a pointer needs an explicit null comparison.  The
     type checker refuses each case with a clear error rather than leaving
     it to fail in LLVM.  (Jake)

### 2026-10-10

211. **Before `docs/name-resolution.md` is deleted, its decisions D1-D77
     and its open-issues list fold into this appendix, and the plans for
     after the merge go into a new `docs/roadmap.md`.**  A cleanup of its
     own, after the code work.

212. **Codegen's table of local values is keyed by each local's binding
     identity, not by its name (rule two).**  Registering a drop flag that
     outlives its block (`ValueTable::set_persistent`) then needs no
     allow.

213. **`TraitDeclNode::lookup_method`, which only the editor calls, stays
     in the compiler with a written reason** until the editor's own
     migration after the merge.

214. **`member-export` stays a door kind of its own** (a module's public
     declarations, by the module and leaf, read off its export list).

215. **The module graph holds each file's import list, so the question
     "which modules does this written path name, as this file sees them"
     (`modules_written_as`) is the graph's to answer through its
     `module-by-path` door,** not a match of written paths against module
     text outside the graph.

216. **An expression made only of literals that would silently narrow
     where it is stored - a constant's or a variable's initializer, an
     assignment (`const c: u8 = 200 + 100;`, which wraps to 44) - is
     refused,** with the "would silently truncate" wording a call's
     argument already gets.

217. **A type declared after a function of the same name in one module
     must not go missing from that module's scope while its export list
     holds it.**  A defect: measured, then fixed.

218. **The two refusals' wordings stand:** a `reason` opening `pending:`
     on a compiler allow, and "`<id>` is not a door".

219. **The Python gates (`lane-gate.py`, `spelling-flow.py`) are not
     taught anything new** - a module path's comparison counted as an
     identity's, or any other refinement: they are to be removed.  Where
     the compiler's own check and a Python list disagree only because of
     what the Python cannot see, that is recorded, not fixed in the
     Python.

220. **The refusal of a literal-only value its integer type does not hold
     is E0010,** the code a single out-of-range literal in the same place
     already gets.

221. **The wordings stand:** the narrowing refusal's "this value is 300,
     which `u8` does not hold: it narrows the value and would silently
     truncate; add an explicit `as` cast", and the identity-text
     refusals' "`==` is handed text read off a definition, which is shown
     and never looked up by", its label "computed from a `DeclName`, and
     the operand of a comparison", and the note "the definition's text
     leaves the function that read it here".

222. **Every lookup the Python spelling-flow list sees that the
     compiler's own checks do not is brought under the checks, module
     discovery included:** text derived from a definition that is not
     typed `DeclName` (a type's display name, a link symbol), discovery's
     matching of written module paths and file paths as plain strings, a
     written name handed as plain text to a fixed table, and a duplicate
     check that records a spelling after the comparison that reads it.
     Each real site found is routed through a door or an identity, or
     says why it is no lookup.

223. **A generic's compiled copies are registered and found by the
     template's identity and their type arguments** (rule four), not by a
     key spelled from the template's name.

224. **The wordings of the plain-text spelling refusal stand:** "`f` looks
     up a spelling it is handed as plain text", its label "looked up here,
     outside a door: the operand of a comparison" (or "the subject of a
     `match`", "the key of a hash table"), the note "the spelling leaves
     the function that holds it here, as text", and the identity-text
     refusal's "the subject of a `match`".

225. **A fixed table the language declares nowhere reuses an existing
     door id rather than getting a kind of its own:** the intrinsic table
     (`IntrinsicKind::from_name`) is a `lang-item` door, and the
     source-location pseudo-constants `FILE` and `LINE`
     (`SourceLoc::of_spelling`) are a `primitive` door.

226. **The Python spelling-flow list is deleted once ruling 223 has
     landed.**  The deletion itself belongs to the cleanup that removes
     the Python gates, not to the change that lands ruling 223.

### 2026-10-09

188. **The comparison primitives (`SymbolStr::equals` and the like) carry
     written reasons on their allows**: they are the comparison itself, and
     whether a comparison is a lookup is judged where it is called.  There
     is no door kind for them.

189. **The scope table's own methods (`Scope::find`, `insert`,
     `get_overloads` and the rest) are made private to the resolver's
     module or modules first, and then carry written reasons.**  This is the
     first step of rule three's structural check.

190. **In `A(v) | B(v)` the two `v`s are one binding.**  The name layer
     declares one binding per name across an arm's alternatives.

191. **`QualifiedName::equals`, `starts_with` and `ends_with` are deleted**:
     nothing calls them (ruling 185).

192. **In a string or character literal, `\0` followed by a digit is a
     compile error with a code of its own** (`"\033"` would otherwise be read
     as a NUL and then the digits).  The help points to the hex escape
     (`\x1b`).

193. **`ModuleGraph::register_c_import_module` carries a written reason**:
     the module graph creating the path of a module that only it makes.

194. **An arm's alternatives must bind the same names, with the same types
     and the same mutability**; otherwise the arm is refused, because its
     body could read a name that the alternative which matched never set.

195. **Rule three for names on syntax nodes and type objects is held by the
     spelling lint (E0157), which follows a spelling read off a FIELD** -
     `decls[i].name.equals(&node.name)` - as it follows a parameter today.
     The compiler's own tables still follow ruling 150: private name
     fields, the door in the store's file.  No non-comparable getter type
     is built.

196. **The list of valid doors moves into the compiler**, as a fixed table
     of door ids, so a `/// Door` marker naming an unknown door is refused
     there before the Python that checks it today is deleted.

197. **verify's census and examples scripts stay** until verify itself
     shrinks at the very end.

198. **Ruling 109's last clause (refuse an allow on a function that takes no
     spelling) is superseded by ruling 179** - the lint refuses only real
     lookups - and is not built.

199. **The type-name door (`NameResolver::type_spelling_res`) is split** into
     a scope-side door beside the rib and a name-layer caller.

200. **A duplicate-declaration check - two declared names compared with
     each other - is not a rule-three lookup.**  Each carries a written
     reason.

201. **E0013 (`\0` followed by a digit) and E0407 (an arm's alternatives
     binding differently) are the codes for rulings 192 and 194, with the
     wording they were built with.**  E0013 may yet give way to real octal
     escapes.

202. **Order of work: the generic-body miscompile (a parameter accepted
     where `Option` of it was written), then the method-binding scans,
     then ruling 195's field-following lint.**

203. **Octal escapes replace E0013's refusal (amends 192 and 201).**  In a
     string or character literal, `\` and one to three octal digits is the
     byte they spell, read as C reads it: at most three digits, ending at
     the first non-octal character; `\033` is ESC, `\0` is NUL, `\177` is
     127.  A value above `\377`, and `\8` or `\9`, is an error.

204. **A generic type's own methods are registered with the
     member-function door under the template itself**, so the door
     answers a lookup made inside the generic body (`b.map::<i64>(g)` on
     a `Box<T>`).  There is no second door over a declaration's method
     list.

205. **The four editor sites the field-following lint flags (`this`
     keyword checks) are fixed when the lint lands**, although the editor
     is otherwise left until after the merge.

206. **E0157's wording for a spelling read off a field is accepted**:
     "`f` looks up a spelling read off a field", with the help "ask a
     door, or say why this is no lookup".

207. **E0013's octal-escape wording is accepted**: "`\8` is not an
     escape: an octal escape's digits are `0` to `7`" and "`\400` is out
     of range: an octal escape is one byte, `\0` to `\377`".

208. **The program's entry point is recorded once by the name layer, as
     an identity.**  The places that compared a function's name with
     `"main"` compare its definition with that identity instead.  There is
     no door for it and no written reasons.

209. **E0358's help for a type parameter's unmet bound is accepted**:
     "add `T: Copy` to the bounds in scope here".

210. **An expression made only of integer literals (`8 * 10000 + 64 *
     100`) is converted to the parameter's type the same way a single
     literal is**: `take_u32(5)` compiles, and so does
     `take_u32(8 * 10000 + 64 * 100)`.

### 2026-10-08

171. *(amended)* **The `Text` type is not deleted yet.**  Deleting it now
     would weaken the Python gates' counts of functions that take a name,
     which read a `string` parameter as one; `Text` is how message text drops
     out of those counts.  It is deleted together with those gates, once
     nothing treats a `string` parameter as a name.

172. **The rule-4 self-check honours `![allow(lookup_by_spelling, reason =
     "...")]` for now**, as the rule-2 check does.  The pending allows that
     also exempt a body from it go when ruling 149's conversion deletes
     them.

173. **A shell redirect writes a job's log and nothing else** (`> log`, as
     the long-job pattern in `CLAUDE.md` does).  A source, test, ledger or
     scratch-script file is written with an editor, never through a
     redirect.

174. **A module-level constant whose initializer calls something is
     computed once, before `main`**, as `docs/cryo.md` §3 says.  The
     start-up initialization the compiler has is kept.

175. **Ruling 170 governs loading only.**  A module must be imported to be
     loaded; a module that is already loaded can still be reached by its
     full path without an import of its own.

176. **`const c = Cell(4);` infers the class's type arguments from the
     constructor's arguments**, as a call of a generic function infers its
     own.

177. **The rule-4 refusal's wording is accepted**: "`intern` is handed text
     read off a definition, which is shown and never looked up by", with its
     help line.

178. **The compiler is re-pinned now**, so the allows on functions carrying
     a door marker can be deleted.

179. **The spelling lint (E0157) refuses a spelling only where it reaches a
     lookup outside a door.**  A function that carries a written name toward
     a door - a pass-along helper, a syntax-tree constructor or setter -
     needs no allow, and its pending allow is deleted.  Rule three's
     structural guarantee (a store's name fields private, its doors in the
     store's own file) follows this.

180. **Entering a closure does not forget the enclosing function's
     parameters.**  `return &param` after a closure in the same function is
     judged as it would be with no closure there.

181. **The async lowering's pending allows are converted next**: the locals
     it tracks and renames are keyed by their binding, not their spelling.

182. **The E0157 second label's wording is accepted**: "looked up here,
     outside a door: compared" / "...: the key of a hash table" / "...: the
     subject of a `match`".

183. **The spelling lint keeps counting any comparison or hash-table insert
     of a spelling as a lookup**, at least until rule three's structural
     check lands.

184. **The LSP's pending allows that the lint no longer needs are deleted
     during the LSP's own work after the merge**, not before.

185. **Dead code is deleted, not converted.**  A name-lookup function or
     helper with no live callers is removed - not given an allow, not
     routed through a door.  This holds for everything a change touches.

186. **The dead-code sweep covers the compiler only.**  A standard-library
     function is public API and is not swept, even when nothing in the
     corpus calls it.

187. **Under rule three's structural check (ruling 150), the worker places
     each door with the store it reads most.**  A store and its doors move
     to a file of their own only when one grows egregiously big; file size
     is otherwise not a concern.

### 2026-10-07

160. **The `()` unit type (ruling 103) is built after the merge, before the
     v1.0 freeze.** `docs/cryo.md` already describes it.

161. **The rule-2 self-check reuses E0157 and its allow-with-reason
     marker**, `![allow(lookup_by_spelling, reason = "...")]`; it gets no
     code of its own.

162. **Rulings 4 and 65 are each built with their fallout fixed in the same
     change**: standard-library code or tests that stop compiling are
     corrected alongside, not left for later.

163. **CI is out of scope until after the merge into `main`.**

164. **The rule-2 table-key check applies to the compiler's own modules
     only**: those whose path starts with `compiler`.

165. **A path loads the sub-module it names.** `json::Inner::deep()`, or
     `J::Inner::deep()` through an alias, loads `json::Inner` when nothing
     imports it; the loader does not wait for an import.

166. **The wordings chosen for rulings 4, 65, 113 and the rule-2 check are
     accepted**: E0157 "`S.f` keys a table by `u32`, not by an identity";
     E0205 "... it is imported"; E0154 "`put` is ambiguous: a non-generic and
     a generic method of `Box2` both accept these arguments"; E0244's help
     "rename the item `Json` or the module `A::Json`".

167. **The loader matches a path's head against the file's imports through
     a door of its own**, and the sub-module the path continues into
     through the same door; the text matching the loader gained for ruling
     165 is not left as outstanding lookups.

168. **A plain import carries reachability into the imported module's
     sub-modules**, as an alias does.  With `import A::Toml;`, the path
     `Toml::Sub::f()` loads `A::Toml::Sub` and resolves to `f` in it,
     rather than E0240.

169. **The wording of E0307 is accepted**: "cannot infer `T` for `idf`
     used as a value", labelled "generic, and nothing here says which
     instance this names", with the help "write the type arguments,
     `idf::<...>`, or give the binding a function type".

170. **A fully spelled path that no import leads to loads nothing.**
     `json::Inner::deep()` in a file with no `import json;` does not load
     `json::Inner`; a module must be imported to be visible.

171. *Amended by ruling 171 of 2026-10-08.* **The `Text` type is deleted if
     that is safe.**  It wraps a string and `to_string()` unwraps it, so it
     seals nothing.

149. **Ruling 109 is narrowed: the spelling lint (E0157) is door-only for
     LOOKUPS.**  A declaration's own name - registering it, printing it,
     mangling it, passing it along - is carried by a separate sealed type
     the lint accepts.  The pending allows go by converting each lookup to
     a door and moving the rest onto that type.

150. **The rule-3 self-check is enforced by making each store's name fields
     `private` and moving its door into the store's own file**, so a
     hand-written `.name` loop outside the door is refused.

151. **Rulings 85 and 97 (instances that appear only through substitution)
     come after the merge**, with a test that would catch a miss.

152. **The calls recorded as "owner + name", which code generation looks
     up again, are retired before the merge**, alongside ruling 125's last
     slice.

153. **The editor's (LSP's) by-name lookups are fixed on their own branch
     after the merge.**

154. **A test-only compiler switch gives the rule-1 self-check a committed
     test** that plants an unresolved name in a correct program.

155. **The lookup gate counts only spelling-driven reads**: a call that asks
     by identity (a `DefId`, a `TypeRef`) and hands a name back is not
     counted.

156. **Ruling 73's help names the pointer as written**: `(*q).drop()` gets
     "use `std::core::ptr::drop_in_place(q)`", not a literal `p`.

157. **Store #2's leftover name-keyed instance registration is finished
     together with store #4.**

158. **`plain_opt(Option::Some(3))`, with the parameter `d: Option<T>`,
     compiles**, as it does through a local; the internal error E0900 it
     gave is a correct program refused.

     ```cryo
     function plain_opt<T>(d: Option<T>) -> Option<T> { return d; }
     const p: Option<i32> = plain_opt(Option::Some(3));
     ```

159. **One CI run of this branch happens before the merge**, so its
     surprises show early.

### 2026-10-06

145. **Calling a value that is not a function keeps E0213**, declared as
     `NON_CALLABLE` and used by ruling 142, rather than a new number.

146. **The two type displays rulings 139 and 142 print in diagnostic text
     are accepted as name-keyed reads**: the lane's LOOKUP_ROUTED count
     going from 25 to 27 for them stands.

147. **A call through a local or a field whose type is no function is
     E0213 too**, "expected function, found `i32`", rather than "cannot find
     function" or "no method named".

     ```cryo
     const x: i32 = 3;
     const a: i32 = x(3);     // expected function, found `i32`
     const b: i32 = h.n(4);   // `n: i32` a field, no method `n`: the same
     ```

148. **A projection through a trait resolves through the traits it
     inherits**: `I::Item` under `I: DoubleEnded`, where
     `DoubleEnded` extends `Iterator`, is `Iterator`'s `Item`.  A projection
     nothing declares stays E0203.

     ```cryo
     type trait DoubleEnded : Iterator { next_back(mut &this) -> Option<This::Item>; }
     function last<I>(it: mut &I) -> Option<I::Item> where I: DoubleEnded { .. }
     ```

139. **E0154 on an overloaded function used as a value with no expected
     type keeps its wording and gains a help line showing how to pick one**
     by typing the variable, with the value's own name and one of its
     overloads' signatures.

     ```cryo
     const f = twice;
     // help: give the variable a function type to pick one: `const f: (i32) -> i32 = twice;`
     ```

140. **Ruling 138's refusal covers a field read through any type parameter,
     bounded or not.**

141. **Codegen's `GlobalRegistry` is keyed by `namespace::name` text, not by
     the linker symbol ruling 126 assumed.** It stays as it is for now and
     is re-keyed when the module-globals table is converted.

142. **Calling a value that is not a function is refused with its own code
     and the wording** "expected function, found `i32`".

     ```cryo
     function one() -> i32 { return 1; }
     const a: i32 = one()(3);   // expected function, found `i32`
     ```

143. **A projection whose trait is unknown, or whose trait declares no such
     associated type, is refused where it is written**, rather than kept as a
     projection interned by its spelling.

     ```cryo
     function b<I>(it: &I) -> I::Nope where I: Seq { ... }   // `Seq` declares no `Nope`
     function c<I>(it: &I) -> I::Item { ... }                // no bound of `I` declares `Item`
     ```

144. **No dedicated check after monomorphization for unbound calls.** The
     monomorphizer visits every call in every copy and is the one place an
     unbindable call is reported (E0900); code generation's own refusal is
     an internal assertion.

### 2026-10-05

131. **A synthesizer that lays out a hidden struct records the pins of what
     it builds.**  The async lowering (a future's state struct) and the
     closure synthesizer (a closure's capture struct) give each field access,
     struct-literal field and variant they build its position as they build
     it, since a node built with its type is never walked again.  Where a
     synthesizer only names a type that already exists, the type check
     checks the lowered body as it checks ordinary code.

132. **A generic function's destructure is checked for its shape on the
     generic body**: a field bound twice, a field the type does not have and
     a field left out are reported there, not only in a copy, so a broken
     generic nothing instantiates is refused too.

     ```cryo
     function first<T>(p: Two<T>) -> T {
         const {a, a}: Two<T> = p;   // refused on the generic body: `a` bound twice
         return a;
     }
     ```

133. **`&T == &T` compares the referents in every copy**, a primitive one
     included: equality through a reference is the equality of what it
     refers to, so no copy compares the addresses.

     ```cryo
     function same<T>(a: &T, b: &T) -> boolean where T: Eq { return a == b; }
     same::<i64>(&m, &n)   // true when m == n
     ```

134. **A float's `abs` clears the sign bit, so `-0.0` and a negative NaN
     come back positive, and `fabs` is removed**; its callers use `abs`.

135. **An overloaded function used as a value is the overload the expected
     function type selects.**

     ```cryo
     const f: (i32) -> i32 = Conv::conv;   // the `conv(i32) -> i32` overload
     ```

136. **Ruling 124's refusal keeps E0214.**

137. **`make verify`'s `tests/started-passing` may name a unit test as well
     as a negative test file**, extending ruling 129, so a change that
     deliberately moves a unit test's object is verified in one run.

138. **A field read through a type parameter is refused on the generic
     body**, as ruling 1 refuses a method call: a bound
     gives a parameter methods, never fields, so what a copy's type
     argument declares is not consulted.

     ```cryo
     implement struct Cell<T, A> {
         tagged(&this) -> i32 { return this.tag.n; }    // E0204: no field `n` on type `A`
         tag_ref(&this) -> &A { return &this.tag; }     // fine; `c.tag_ref().n` reads it where `A` is known
     }
     ```

### 2026-10-04

119. **`module_named` is private to `module_graph.cryo`.** Callers outside
     the module graph reach a module through the graph, not through the
     door, so ruling 108's privacy is expressed by ordinary visibility.

120. **`math::min` and `math::max` take `where T: Ord`**, so their
     comparisons are licensed by the bound under ruling 82.

     ```cryo
     function min<T>(a: T, b: T) -> T where T: Ord { .. }
     ```

121. **The tests that apply an operator to an unbounded type parameter get
     the bound the operator needs**; they do not become negative tests of
     ruling 82's refusal.

122. **Ruling 82's refusal is E0229, with a message naming the type
     parameter and its bounds**, reported on the generic body.

     ```cryo
     function add<T>(a: T, b: T) -> T { return a + b; }   // error[E0229], names `T` and its bounds
     ```

123. **The standard library's "frozen, no `checked_*`" overflow contract
     exempts `checked_abs`**: it is the one checked operation ruling 112
     names, and the contract says so.

124. **After ruling 82, an unbounded type parameter passed where a concrete
     type is expected is refused.** `math::sqrt` and `math::fabs` then take
     a bound or become per-type functions.

     ```cryo
     function f<T>(x: T) -> f64 { return takes_f64(x); }   // refused: `T` is not `f64`
     ```

125. **A generic function's instances are filed under the template's
     definition**, keyed by that definition plus the type arguments: an
     instance is the template applied to its arguments, never a name.

126. **Codegen's `GlobalRegistry`, keyed by linker symbol, stays as it is**;
     it is outside rule two.

127. **`LangItem::AstArenaAlloc` stays.**

128. **`math::sqrt` and `math::fabs` become methods of each float type**, as
     ruling 112 made `abs` one of each numeric primitive; the generic
     functions are removed.

     ```cryo
     const r: f64 = (2.0).sqrt();
     ```

129. **`make verify`'s `tests/started-passing` may name a negative test
     file whose report changes deliberately**, so a change to a refusal's
     message is verified in one run rather than composed by hand from two.

130. **`This::` stamps the owner's definition directly** (and the owner's
     parameters as its arguments) instead of writing the owner's name into
     the path and resolving it again.

### 2026-10-03

95. *Superseded by ruling 112.* **`math::abs<T>` and `math::clamp<T>` are bounded by the standard
    library's existing ordering and negation traits**: `abs` takes
    `where T: Ord + Neg<T>` and `clamp` takes `where T: Ord`
    (`std::core::cmp::Ord`, `std::core::ops::Neg<Output>`). With the bounds
    written, ruling 82 applies to their bodies.

    ```cryo
    function clamp<T>(value: T, lo: T, hi: T) -> T where T: Ord { .. }
    ```

96. **In a monomorphized copy, a trait-bound call passes its arguments the
    way monomorphization bound the call**: `b` in `a.equals(b)` is
    borrowed, not moved. The second pass's answer is the wrong one. The
    change is `.objcmp/s100/args-a1.patch`, and the 105 objects it moves are
    accepted with that explanation.

97. **Ruling 85's demand comes after only reachable methods are emitted**,
    so emitting is driven by what is reached: an instantiation that appears when
    monomorphization substitutes types is demanded once emitting is driven
    by reachability, so it does not emit methods nothing calls.

98. **A generic type's bare name inside its own body infers its type
    arguments from the call**, as it does outside its body. `This::...` names the enclosing
    type.

    ```cryo
    implement struct Array<T, A> {
        grow(&this) -> .. {
            const a = Array::try_with_capacity_in(n, alloc);  // arguments inferred from the call
            const b = This::try_with_capacity_in(n, alloc);   // the enclosing Array<T, A>
        }
    }
    ```

99. **When the second pass after monomorphization is deleted, the literal
    widths it was wrongly deciding move** (about 50 objects: `r.take(5)`'s
    argument becomes `u64`), and those movers are accepted with that
    explanation.

100. **Ruling 94's help on `append` reads**: "`String` owns resources, so it
     isn't `Copy`, and `append` is only available when `T` is".

101. **A never-instantiated generic function holding an ambiguous call on a
     concrete receiver is an error**, as commit b6770551 made it.

102. **A `for` loop's initializer that does not fit its declared type is
     E0200**, as for any declaration.

     ```cryo
     for (mut j: i32 = s.length(); j > 0; j--) { }   // error[E0200]
     ```

103. **`()` becomes a real unit type**, as a task of its own after the
     merge. Until then the type checker keeps typing it `void*`.

104. **The lane gate's `LOOKUP_OTHER` count going from 45 to 46** for the
     member-function door call that replaces the template registry's
     composed key, **and the spelling-flow check counting a map method's key
     passed as an `arg`** as it counts one passed as a `sarg`, are accepted.

105. **The Python paperwork is removed before the merge**, replaced where
     a guarantee must survive by a check inside the compiler. This amends
     ruling 10, which kept the ledger until name resolution was complete,
     and ruling 18, which kept the scripts until after the merge.

106. **The ledger (`docs/name-resolution.md`) goes before the merge too**,
     with its status check and its commit hook, as the last paperwork
     slice.

107. **"The Python" means the migration paperwork and the test plumbing.**
     General tooling stays: the pin, the self-host check, the release
     scripts and verify.

108. **Rule three outlives the flow tracer by structure**: the methods of
     a name-keyed store that take a spelling are private to the module of
     the door that owns the store, so a spelling reaching the store from
     anywhere else is a visibility error.

109. **The spelling lint (E0157) becomes "only a door may take a
     spelling".** The pending allows are deleted, and an allow on a
     function that takes no spelling is refused.

110. **`--emit=facts` and its readers are deleted.**

111. **`make verify --require-identical` stays until the last
     name-resolution slice has landed**, and then shrinks to a short make
     target.

112. **`abs`, `clamp` and `checked_abs` are per-primitive functions**
     (`i32::abs`, `f64::abs`), not generic functions with trait bounds: no
     trait says "a literal converts to `T`", which their bodies need. With
     them, ruling 82 applies to the standard library. Replaces ruling 95.

     ```cryo
     const a: i32 = (-5 as i32).abs();
     ```

113. **Every braced import of a name that is both an item and a module is
     an error**, aliased or not (`import A::{ B }`, `import A::{ B as C }`),
     settling rulings 57 and 63 together. E0244's help suggests renaming
     one of the two rather than the braced form.

114. **The 148 command-line flag entries stay on the spelling-flow list**
     until the list itself is deleted.

115. **The residue self-test's fixture supplying its own classes is
     accepted**, and so is the conversion of the method return-type table:
     each reader reads the return of the method it selects, and a
     trait-declared method's return comes through the `trait-method` door.

116. **A static call that is refused has no type**: when the selection picks
     no method (two methods tie, or no overload fits the arguments), the call
     is not typed with the return of the method registered last.

     ```cryo
     const v: i32 = Both::go(&b);   // error[E0156], and no second error on `v`
     ```

117. **The function-template placeholders are converted before
     `module_named` is made private**, the order the change converting
     them took.

118. **Two call-resolution defects are fixed on this branch**: a call to
     overloaded generic free functions selects the overload its arguments
     fit, and a generic owner's method returning a bare type parameter,
     called through a path that writes the owner's arguments, has that
     parameter substituted.

     ```cryo
     function pick<T>(a: T) -> T { return a; }
     function pick<T>(a: T, b: T) -> T { return b; }
     const y: i32 = pick(4, 5);             // the two-argument `pick`

     const c: i32 = Box2::<i32>::get(&bx);  // `get` returns `T`, here `i32`
     ```

### 2026-10-02

80. **The internal check after monomorphization names the specialization's
    owner by its short display name** (`SliceIter<u8>`), as in "internal:
    call to `next` in `SliceIter<u8>::for_each` reached codegen with no
    resolved method (written at iter.cryo:74)". The lane gate's ARENA_READ
    count rising by one for that rendering is accepted.

81. **An enum constructor over an unminted argument infers its
    instantiation** under ruling 35: `const o = Option::Some(String::from_string("hi"));`
    compiles with `o: Option<String>`. It was refused with E0200.

82. **An operator the trait bounds do not license is refused on the
    generic template**, before monomorphization, where the bounds are all
    that is known; it is not checked per instance. `a + b` under a bound on a user trait that is
    merely named `Add` is an error in the generic body.

    ```cryo
    type trait Add<R, O> { add(&this, r: R) -> O; }
    function sum<T>(a: T, b: T) -> T where T: Add<T, T> {
        return a + b;   // error: the bound is a user trait, not the language's `+`
    }
    ```

83. **`static match (T)` checks every arm before monomorphization.** A
    single-type arm (`string =>`) narrows `T` to that type inside the arm;
    a multi-type arm (`i32 | i64 =>`) must be valid for each listed type,
    checked once per listed type; the wildcard arm `_` knows only `T`'s
    declared bounds, like ordinary generic code. How many existing sites the
    wildcard rule rejects is measured before it is switched on, and the
    bounds they need are added in the same change.

    ```cryo
    function show<T>(x: T) -> i32 {
        return static match (T) {
            string    => { x.length() as i32 }   // `x: string` here
            i32 | i64 => { x as i32 }            // checked as i32, then as i64
            _         => { 0 }                    // only `T`'s bounds
        };
    }
    ```

84. **The post-monomorphization write probe is a committed compiler
    instrument**, an `--emit=` mode or a debug mode, not a Python script.
    It gates the slices that remove the pass after monomorphization.

85. **Instantiations that come into existence only when monomorphization
    substitutes types into a copy are demanded and emitted**: a type
    appearing in a body counts as a use.

86. **The internal check after monomorphization words a free function's
    unbound call like a method's**: "internal: call to `panic` in ...
    reached codegen with no resolved function (written at mem.cryo:74)" -
    ruling 71's wording, with "function" for "method".

87. **This ends ruling 79.** An ordinary method merely named `drop`, called
    explicitly, gets no field release after it; its fields are released at
    scope exit.

88. **The count of functions that take a name going from 699 to 701**, for
    the `member_entries` and `trait_slot_of` helpers, is accepted.

89. **The lane gate's ARENA_READ count going from 64 to 65** for the
    `--emit=postmono-writes` report, which renders type names into its
    text, is accepted.

90. **The three spelling-flow rows the `--emit=postmono-writes` flag
    adds** (772 to 775: the `--emit` flag's read and the
    `"postmono-writes"` match) are accepted. They are the same shape as
    the existing flag rows, and under ruling 18 the gate is not changed to
    exempt command-line flags.

91. **A call left with no instance after monomorphization is the internal
    E0900**, not the user-facing E0214 ("no overload accepts these
    arguments"). A user-facing error comes from the check before
    monomorphization; a test that reaches this E0900 marks a gap in that
    check, which is closed there.

92. **A method whose `where` clause the receiver's arguments do not meet is
    refused before monomorphization** with the wording a minted receiver
    already gets: "the trait bound `String: Copy` is not satisfied", note
    "method `get` requires `V: Copy`". It replaces "no method named `get`
    found on type ...".

93. **A deliberate wording change in a negative test's expected report is
    accepted as a verify mismatch**, explained in the commit body and the
    ledger entry, as with ruling 19; `scripts/verify.py` is not changed
    (ruling 18). The test's expected output changes in the same commit.

94. **The help on a `Copy`-gated method's bound error is correct for the
    method it names.** "borrow the element in place with `get_ref` instead
    of copying it out with `append`" is wrong advice for `append`.

### 2026-10-01

70. **The pass after monomorphization does not re-walk generic template
    bodies that are never emitted**, once it is measured that no diagnostic
    depends on that walk.

71. **The internal check after monomorphization uses plain wording** that
    names the method, the specialization and the template call site:
    "internal: call to `push` in `String<GlobalAlloc>::from_str` reached
    codegen with no resolved method (written at string.cryo:120)".

72. **The temporary identity-first, by-name fallback for the calls whose
    receiver monomorphization could not type is accepted until the receiver
    fix**, which must remove it before the by-name finders are deleted.

73. **E0358 on `(*p).drop()` gets a help of its own**: "use
    `std::core::ptr::drop_in_place(p)`".

74. **The spelling-flow entry for `IntrinsicKind::from_name` is accepted.**

75. **`CalleePin::intrinsic_kind` may live in the AST file.**

76. **`Array` keeps `where T: Drop` until the compiler's use-after-drop is
    fixed.** That fix is queued.

77. **Inside a generic template, a call keeps the method it was written
    against** (rule 1). `Bx::<i32>{..}.run()` returning 1 is confirmed.

78. **The closure synthesizer's inherent `drop` becomes a real `Drop`
    implementation**, not deleted.

79. *Ended by ruling 87.* **An explicit inherent `x.drop()` that runs field release is accepted
    until the call pin's method node and codegen's `.drop()` name branch are
    removed.**

### 2026-09-30

40. **The explicit-path piece of the monomorphizer plan lands ahead of the
    plan's order**: a pin on `(Trait for Concrete)::m` survives
    substitution, as built before the plan's earlier slices.

41. **A trait method called on a concrete receiver reached through a field
    inside a generic body is specialized now**, not left for the
    monomorphizer's conversion. The receiver's type is re-derived in the
    clone, and only a receiver that is a generic type's specialization is
    left to the placement machinery.

    ```cryo
    type trait Bb { go<W>(&this, w: W) -> i32; }
    type struct S { v: i32; }
    implement trait Bb for S { go<W>(&this, w: W) -> i32 { return 2; } }
    type struct T2 { s: S; }
    function run<W>(t: &T2, w: W) -> i32 { return t.s.go(w); }   // S's go<W>, specialized
    ```

42. **A trait-method door** (`trait-method` in the table above) takes a
    trait's identity and a method's leaf and returns the method's position
    among the trait's declared methods. It is the one place an
    implementation's method is matched to a trait's method by name; the
    implementation's position table is filled through it.

43. **The internal E0900 wording for a disagreement between the slot table
    and the selection by name is approved**: "`m` was selected by name as a
    trait's method, and the method table of the implementation the trait and
    the receiver select answers a different one" (or "no method").

44. **Only `implement trait Drop for X` defines a destructor.** An inherent
    `drop` is not a destructor. This completes ruling 32. Both land with the
    `drop_in_place` intrinsic (ruling 14) and never before it: applied
    alone, `Box` would stop releasing what it holds, silently.

    ```cryo
    type struct H { p: i32*; }
    implement struct H { drop(mut &this) -> void { .. } }            // an ordinary method
    implement trait Drop for H { drop(mut &this) -> void { .. } }    // the destructor
    ```

45. **Ruling 30's suppression stays broad**: any error caused only by an
    unknown trait is suppressed once the unknown trait is reported,
    including in type positions and after E0155 or E0240.

46. **Ruling 26 covers method values.** `const f = IgTn::go;` is the E0156
    ambiguity when a trait implemented for `IgTn` also provides `go`.

    ```cryo
    const f = IgTn::go;                // error[E0156]: both `go`s apply
    const g = (Beta for IgTn)::go;     // Beta's
    ```

47. **A second import bringing in an already-imported name is refused at
    the import line itself**, with an error of its own. The
    code is **E0243**, primary line "`parse` is imported twice", a label
    on the second import ("`parse` is imported again here") and a note on
    the first ("first imported here"), with the help "remove one of the
    imports, or import one under another name with `as`". The code and
    wording were chosen by the worker recording this ruling, as the ruling
    asked; ruling 3's qualified use is unchanged.

    ```cryo
    import Json::{ parse };
    import Toml::{ parse };   // error[E0243]: `parse` is imported twice
    ```

48. **The explicit call of a generic trait method on a generic owner,
    `(BetaG for GgTf<i32>)::go(&e, 7)`, is handled by the monomorphizer's
    conversion** (the plan's slices 6 and 7), not fixed separately. Until
    then it is E0636.

49. **Methods get `DefId`s, registered by the name layer like other
    items.** Code the compiler generates names the methods it calls through
    **method language items** (for example `FuturePoll`, `OptionTake`); it
    never builds a name to look up.

    ```cryo
    // what the async lowering writes at `x.await`, named by identity
    fut.poll(cx)      // LangItem::FuturePoll, not the leaf `poll`
    slot.take()       // LangItem::OptionTake, not the leaf `take`
    ```

50. **Ruling 47's error is approved as E0243**, "`parse` is imported
    twice".

51. **A method with a receiver, named by path, is a function value whose
    type takes the receiver first**. Ruling 46's ambiguity
    (E0156) applies to it when a trait implemented for the owner also
    provides the method.

    ```cryo
    const f = IgTn::go;                // type `(&IgTn) -> i32`
    ```

52. **Cryo gets import aliasing**, on this branch, despite the 2026-09-28
    freeze: an imported item may be bound under another name, in a brace
    entry and in a single-item import. The alias names the same
    definition. E0243's help then suggests it.

    ```cryo
    import Toml::{ parse as toml_parse };
    import Toml::parse as toml_parse;
    ```

53. **After E0243, a bare use binds to the first import with no further
    error**, so one mistake is reported once. This supersedes ruling 3's "the bare use is
    refused".

54. **A run project checks that a qualified path (`Toml::parse(...)`) calls
    that module's function when a single bare import of another module's
    `parse` exists.**

55. **`ResBase::primitive_position` as a third row of the `primitive`
    door is confirmed.**

56. **Types and values are separate namespaces for import collisions.** A
    type `Tag` from one module and a function `Tag` from another, both
    imported, do not collide.

    ```cryo
    import Shapes::{ Tag };     // a type
    import Labels::{ Tag };     // a function: no collision
    ```

57. **`import A::B as C;` where `A` has an item `B` and a module `A::B`
    exists is an ambiguity error**, neither the item nor the module
    winning. The writer uses the braced form for the item or names the
    module by its path. The code is **E0244**, primary line "`A::B` names
    both an item of `A` and a module", a label on the import ("`B` is
    declared in `A`, and `A::B` is a module") and the help "import the item
    with `import A::{ B as C };`, or the module with `import A::B;`". The
    code and wording were chosen by the worker recording this ruling, as
    the ruling asked.

    ```cryo
    import Shapes::Json as J;        // error[E0244]
    import Shapes::{ Json as J };    // the item
    ```

58. **Whole-module aliases work.** `import M as N;` binds `N` to the
    module's identity, and `N::f()`, `N::Type` and the rest resolve in that
    module.

    ```cryo
    import Json as J;
    const n: i32 = J::twice(3);
    ```

59. **`export` accepts `as` on an entry**, as `import` does.

    ```cryo
    export std::fmt::{ printf as print };
    ```

60. **A qualified call to an overloaded module function,
    `Json::conv(1 as i32)`, compiles and calls the overload its arguments
    select**, where it was E0900. Fixed on this branch.

61. **An integer literal argument defaults to `i32` in overload
    selection**, one fixed rule for an unsuffixed literal: with
    `conv(i32)` and `conv(i64)`, `conv(1)` calls `conv(i32)`.

62. **Under E0244's item-and-module tie there is no way to alias the
    module, and that is accepted**: the writer renames one of them.

63. **A braced import `import A::{ B }` where `B` is both an item of `A`
    and a module `A::B` is an error too**, consistent with E0244; neither
    the item nor the module wins.

    ```cryo
    import Shapes::{ Json };   // error, as `import Shapes::Json as J;` is (E0244)
    ```

64. **Brace exports restrict**: `export A::{ a };` exposes `a` and nothing
    else of `A`, as `docs/cryo.md` section 14.5 says. The compiler is
    fixed to match, with the breakage measured first.

65. **A local declaration and an import of the same name in the same
    namespace is E0205.** Neither the local winning silently nor an alias
    being dropped silently is allowed.

    ```cryo
    import M::{ Tag };
    type struct Tag {}        // error[E0205]
    import Json as Loc;
    type struct Loc {}        // error[E0205]
    ```

66. **A path continues through a module alias into its sub-modules.**

    ```cryo
    import Json as J;
    const n: i32 = J::Inner::deep();   // Json::Inner::deep
    ```

67. **The E0214 help line for a free-function overload no signature
    accepts is approved**: "an integer literal is `i32` where the overloads
    differ in its type; write the type you mean with `as`".

68. **A generic function or method used as a value infers its type
    arguments from its use**: `const h = idf;`, `G::pick`, and
    a generic owner's `W::get`. Today `const h = idf;` fails LLVM
    verification.

    ```cryo
    function idf<T>(x: T) -> T { return x; }
    const h: (i32) -> i32 = idf;   // idf::<i32>
    ```

69. **A float literal argument defaults to `f64` in overload selection
    where the overloads differ**, mirroring ruling 61.

### 2026-09-29

1. **A method call on a type parameter with no bound is an error.** Inside
   a generic body a parameter has no methods except those a trait bound
   gives it. The standard library's calls that relied on the
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
   name only. *The bare use's refusal is superseded by ruling 53.*

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
   which emits each type's drop code
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

13. *Superseded by ruling 22.* **An inherent method wins over a trait
    method**, as `docs/cryo.md` ("Which Method a Call Names") said - whether
    either is generic. Today a non-generic trait method beats a generic
    inherent one; that changes, measured before it does.

    ```cryo
    type trait Show { show(&this, x: i32) -> i32; }
    type struct P { v: i32; }
    implement P { show<T>(&this, x: T) -> i32 { return 1; } }
    implement trait Show for P { show(&this, x: i32) -> i32 { return 2; } }
    const n: i32 = p.show(5);   // the inherent `show`: 1
    ```

14. **The drop-in-place intrinsic is `drop_in_place<T>(p: T*)` in a new
    module `std::core::ptr`**, beside the pointer operations. It does not go in
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

21. *Superseded by ruling 22 the same day.* **A generic inherent method and
    a non-generic trait method of the same name that both apply to a call
    make the call ambiguous.**

22. **Whenever an inherent method and a trait method of the same name both
    apply to a call, the call is an ambiguity error**, in every combination
    of generic and non-generic receiver and method. There is no precedence
    rule. The caller writes `(Trait for Owner)::member(args)` or renames one
    of them. This supersedes ruling 13.

    ```cryo
    type trait Beta { go(&this, u: i32) -> i64; }
    type struct IgTn { v: i32; }
    implement trait Beta for IgTn { go(&this, u: i32) -> i64 { return 2; } }
    implement struct IgTn { go<U>(&this, u: U) -> i64 { return 1; } }

    const n: i64 = ig.go(7);                     // error: both `go`s apply
    const m: i64 = (Beta for IgTn)::go(&ig, 7);  // Beta's: 2
    ```

23. **An unknown supertrait, or an unknown trait in a `where` clause, is
    refused where it is written**, with wording that says trait, "cannot
    find trait `Foo` in this scope" - not E0203's "cannot find type". The code is E0242, new for this; an impl head's
    unknown trait, which already said "trait", takes it too.

    ```cryo
    type trait Sub : Missing { go(&this) -> i32; }             // error[E0242]: cannot find trait `Missing` in this scope
    function f<T>(x: &T) -> i32 where T: Nowhere { return 1; } // error[E0242]: cannot find trait `Nowhere` in this scope
    ```

24. **Ruling 22's ambiguity is E0156**, the error for a name more than one
    trait provides, whose help already writes
    `(Trait for Owner)::member(args)`, with a primary line that names the
    inherent method.

    ```
    error[E0156]: `go` is both an inherent method of `IgTn` and provided by a trait implemented for it
     note: candidate #1: the inherent method `IgTn::go`
     note: candidate #2: `Beta::go`, from `implement trait Beta for IgTn`
     help: neither outranks the other: call the trait's method as `(Beta for IgTn)::go(&ig, 7)`, or rename one of them
    ```

25. **A call written on a bounded type parameter means the bound's method,
    full stop**. Generic code sees only its bounds, so an
    instantiation never makes such a call ambiguous or redirects it to an
    inherent method the instantiated type also has.

    ```cryo
    function same<T>(x: &T, y: &T) -> boolean where T: Eq {
        return x.equals(y);   // Eq::equals, even for a T with an inherent `equals`
    }
    ```

26. **Ruling 22 covers static paths too.** When a trait implemented for the
    type also provides the member, `IgTn::go(&ig, 7)` is the E0156
    ambiguity, as `ig.go(7)` is; so are `T::m()` for a static method and
    `T::m` as a value. The explicit forms are `(Beta for IgTn)::go(...)` for
    the trait's method, or renaming.

    ```cryo
    const n: i64 = IgTn::go(&ig, 7);              // error[E0156]: both `go`s apply
    const m: i64 = (Beta for IgTn)::go(&ig, 7);   // Beta's: 2
    ```

27. **The E0156 wording for an inherent method beside a trait's is
    approved**: the primary line "`go` is both an inherent method of `IgTn`
    and provided by a trait implemented for it", the note "candidate #1: the
    inherent method `IgTn::go`", and the help "call the trait's method as
    `(Beta for IgTn)::go(&ig, 7)`, or rename one of them".

28. **E0242 ("cannot find trait") is approved**, including an unknown trait
    in `implement trait X for ...` moving from E0203 to E0242.

29. **The explicit call of a generic trait method, `(BetaG for NgTf)::go(&g,
    7)`, compiles and runs**, since E0156's help sends people to that form.

    ```cryo
    type trait BetaG { go<U>(&this, u: U) -> i64; }
    type struct NgTf { v: i32; }
    implement trait BetaG for NgTf { go<U>(&this, u: U) -> i64 { return 2; } }
    implement struct NgTf { go<U>(&this, u: U) -> i64 { return 1; } }

    const n: i64 = (BetaG for NgTf)::go(&g, 7);   // 2
    ```

30. **Under `where T: Nowhere`, a call does not add E0306 after the E0242**
    the bound already reported. The E0306 is suppressed only when the
    E0242 was actually reported.

    ```cryo
    function f<T>(x: &T) -> i32 where T: Nowhere { return 1; }  // error[E0242]
    const r: i32 = f::<i32>(&a);                               // no second error
    ```

31. **A method two traits provide, called inside one of those traits' impls
    on a concrete receiver, is E0156.** There is no "the impl's own trait
    wins" rule; the call names the trait it means.

    ```cryo
    implement trait Debug for struct String<GlobalAlloc> {
        fmt<W>(&this, f: mut &Formatter<W>) -> Result<(), FmtError> where W: FmtWrite {
            const view: Str = this.as_str();
            return view.fmt(f);                    // error[E0156]: Display::fmt and Debug::fmt
            return (Debug for Str)::fmt(&view, f); // the one meant
        }
    }
    ```

32. **An inherent `drop` does not satisfy `where T: Drop`.** A type
    implements the trait with `implement trait Drop for ...`.

33. **A call on a bounded type parameter records its bound where it is
    written, from every `where` level (the owner's and the method's) and
    from associated-type bounds, by identity**, however the receiver is
    reached (an index, a field, a projection). Such a call is never
    re-selected by name after monomorphization, so ruling 22's ambiguity
    cannot meet it there.

    ```cryo
    type struct Q { v: i32; }
    implement struct Q { equals(&this, o: &Q) -> boolean { return false; } }
    implement trait Eq for struct Q { equals(&this, o: &Q) -> boolean { return this.v == o.v; } }
    a.index_of(&Q { v: 2 })   // Eq::equals inside `index_of` (`where T: Eq`); was E0156
    ```

34. **The crash of a generic overload sharing a name is fixed by
    construction in the monomorphizer's conversion**, not by patching the
    finder that crashes.

35. **A call sema cannot type before monomorphization gets its typing
    completed**; it is not looked up during monomorphization instead.

36. **The async lowering pins the calls it generates itself.**

37. **A call's final pin stays `CalleePin::Decl`**, with an instance table
    in the monomorphizer; there is no new instance pin kind.

38. **A specialization's entry is registered without a name key**, so no
    name can reach an instance.

39. **The post-monomorphization check for an unpinned call (E0900) also
    names the template call site it came from.**

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

### Earlier decisions (D1-D77)

Decided before the numbered rulings began, and numbered apart from them.
Each is stated as the behaviour it requires.  A decision since replaced,
dropped or deferred says so; a deferred one is listed in
`docs/roadmap.md`.

- **D1.** A name's stamp (`Res`) is authoritative: what the name layer
  bound is what the name means, not a hint a later pass may override.
- **D2.** There is no separate function namespace.  The value namespace
  holds every value-bearing declaration: variables, parameters, constants,
  enum variants, functions, methods and intrinsics.
- **D3, D4.** *Replaced by D5.* Modules binding in the type namespace, and
  `A::B` preferring the type.
- **D5.** Modules and types share one namespace.  In a file's scope a name
  is a module or a type, never both; the pair is a redeclaration (E0205)
  at the declaration or import that completed it.
- **D6.** Intrinsics are namespaced.  A bare name always means a
  declaration in scope, never an intrinsic reached around it; intrinsics
  are reached through `std::core::intrinsics`.
- **D7.** Imports do not glob.  A plain `import M;` binds the module and no
  names from it.  The globs that remain are the written `M::*`, the prelude
  and the formatted-string runtime the compiler imports for a program.
- **D8.** There is no inline `<T: Bound>` form; bounds are written in
  `where`.
- **D9.** Type declarations take `where` between the head and the body,
  and an instantiation whose argument does not satisfy a bound is E0306 at
  the annotation that wrote it.
- **D10.** A leaf that names two declarations is E0155, never a bind in
  directory order - and so is a leaf two children of one facade declare,
  reached by a qualified path, in a call as in an annotation.
- **D11.** The resolution counter and its audit streams are removed from
  the compiler entirely.
- **D12.** *Ended by the five rules.* A public name-keyed lookup guarded by
  a gate instead of privacy.
- **D13.** The parser records a `new` path whole; resolution classifies
  it.  A type owning the tail means a variant; any other answer means the
  path names the type.
- **D14.** A re-exported name is reachable through the facade that
  re-exports it: one item, many paths, one identity.  A name two of the
  facade's children declare is refused, not picked.
- **D15.** *Stopped.* Requalifying the tree to `import M;` plus `M::Thing`
  is not resumed without a new decision.
- **D16.** An `implement` head writes every parameter of the template it
  names, as its own parameters or as a concrete instantiation; one left
  out is E0302.  `implement<A> trait Display for String<A>`, or `for
  String<GlobalAlloc>`.  The concrete spelling is honoured by selection:
  `for Wrap<T, Alpha>` is not selected for `Wrap<i32, Beta>`.
- **D17.** An `extern "C"` function is public unless written `private`.
- **D18.** The primitive type names stop being keywords, and the alias
  keywords `int`, `uint`, `float`, `double` fold to their primitive at the
  name layer (`int` is `i32`).  The alias fold is part of the language; the
  keyword half is deferred to a branch of its own after the merge.
- **D19.** A bare `malloc`, `free` or `realloc` with no declaration in the
  writing module is an error (E0202), whose note names the modules that
  declare the leaf: write `libc::free` or `heap::free`.
- **D20.** `import Unknown;` naming no module is E0502 at the import, in
  every form; the help names the module the path abbreviates.
- **D21.** An unknown type in expression or type-argument position
  (`sizeof(Nope)`, `1 as Nope`, `String<Nope>`) is an error.  A defaulted
  type parameter resolves in the module where the type is declared, so
  `String` fills in `GlobalAlloc` without the user importing it; a user who
  writes the argument needs it in scope.
- **D22.** A trait head's trailing type list declares nothing: a name there
  declared nowhere is E0302, and the head is written `implement<T> trait
  Atomic<T> for ...`.
- **D23.** A consumer that meets an unstamped node treats it as an error,
  never as a cue to look the name up another way.
- **D24.** The trait-implementation registry is keyed by the trait's
  identity and the target, a list per key, with one selector by
  unification.  Two traits of one leaf implemented for one type are two
  implementations.
- **D25.** `(Beta for P)::make()` is the impl-qualified call: the method
  `Beta`'s implementation for `P` delivers.  It covers static and receiver
  methods (`(Beta for P)::go(&p)`), values, and generic bodies (`(Beta for
  T)::make()`).
- **D26.** Two traits providing one method for a type is an error (E0156)
  in both call forms, `recv.m(...)` and `T::m(recv, ...)`, with each
  candidate named; never a pick by declaration or import order.  The
  trait-qualified `Tr::m(&recv, ...)` or the impl-qualified call chooses.
- **D27.** *Rejected.* Requiring a trait to be in scope before its methods
  can be called.  Trait methods are called with the trait imported nowhere,
  and scope would not settle a tie; D25 gives a tie its spelling.  Not to
  be proposed again without new measurements.
- **D28.** Overlapping implementations - two heads of one trait that could
  both accept one type - are refused at the later head (E0308), not
  resolved by specialization or declaration order.
- **D29.** `implement trait Tick for struct Holder<S>` with `S` declared
  nowhere is E0302, whose message says the head names `S`, declared
  nowhere.  A nested `Holder<Vec<S>>` is E0203 from the general refusal.
- **D30.** A generic parameter that shadows an enclosing one is refused
  (E0311): a method's `<T>` inside `Box<T>`, a trait method's, an impl
  method's, and `<T, T>` in one list.  Each parameter is its own identity,
  so substitution never merges two parameters of one name.
- **D31.** In expression position `<` opens type arguments only
  immediately after `::`; anywhere else it is a comparison.
  `foo::<i32>(y)`, `Array::<int>::new()`.
- **D32.** *Replaced by the five rules.* A completion criterion counted in
  name-keyed reads.
- **D33.** A `DefId` is a position in a `DefTable` only its own module can
  extend.  `register` always appends and never looks anything up; no
  accessor returns the number, which never reaches output and is never
  persisted or compared across builds.  Instantiations carry their own
  handle rather than a definition.
- **D34.** Intrinsic functions are exported through their module: `import`
  brings them in and the module path reaches them.  A bare `panic` is
  `std::core`'s function.
- **D35.** Function and method overloading are both kept.  A family is
  keyed by its owner (a module or a type) and the written leaf; a lookup
  returns the set and its caller chooses.
- **D36.** Every non-static method writes its receiver explicitly; a method
  the compiler generates still gets one synthesized.  Deferred to a branch
  of its own after the merge.
- **D37.** A check of reference mutability runs before the async lowering,
  which turns an async method's receiver into a raw pointer.  Deferred to
  a branch of its own after the merge.
- **D38.** The editor's bare-name finders live in the editor, not the
  compiler, until the editor's own work after the merge.
- **D39.** An implement block's parameters bind through its head, and a
  parameter's type is its declaration's: every comparison of parameters is
  by identity.
- **D40.** An incremental build loses the warnings of a module reused from
  cache.  Filed, not fixed: a warning total is evidence only from a clean
  build, and warnings compare as sets.
- **D41.** *Replaced by the five rules.* An approved list of name-taking
  functions.
- **D42.** *Removed with the lint layer.* The compiler reporting its own
  lookups through `--emit=facts`.
- **D43.** *Removed with the lane gate.* The lane counts' re-pins.
- **D44.** An import that binds nothing in the requested namespace keeps
  its answer in the other; it is neither an error nor a cue to walk on.
- **D45.** Nothing stores a namespace as text.  A type's, template's or
  symbol's module is a module identity, made once where source text enters;
  `ModuleGraph::module_named` is a door private to the name layer and the
  module loader.
- **D46.** `TypeRef` is sealed: its number and constructor are private to
  its module, read through `key`, compared through `equals`, and made only
  by the arena's allocator.
- **D47.** Identities carry a generation tag checked where they are read: a
  `DefId`, `SymbolID`, `TypeRef` or `OverloadId` another table made, or one
  made before a rebuild, is refused by the table rather than answered.
- **D48.** *Replaced by D47.* One allocator per identity kind.  A second
  table still compiles; nothing it mints is answered.
- **D49.** Both a `deprecated` attribute and a deny-by-default lint whose
  allow carries a reason, for their separate jobs.  Deferred to a branch of
  its own after the merge; the attribute is reconsidered once the lint
  exists (D64).
- **D50.** Two records that disagree are re-measured, not reconciled to
  either document.
- **D51.** A literal or range pattern whose kind is not the subject's type
  is E0200 at the pattern, not an arm that silently never matches.
- **D52-D55, D57, D58.** *Removed with the lint layer.* The spelling-type
  list, the spelling lint's site, E0157, its spec text, the `Keyword` type
  and the lane counts' re-pins.
- **D56.** A link symbol is a `MangledName`, a type of its own, not a
  spelling with a written reason.
- **D59.** A bare identifier in expression position may not name a type:
  it is asked in the value namespace alone, and a name that is only a type
  is E0201 as a value and E0231 as a callee.
- **D60.** The `raw` subcommand builds a module graph as `check` and
  `build` do.  Deferred to a branch of its own after the merge.
- **D61.** A dynamic array's method arguments are type-checked before code
  generation, against `Array<T>`'s method (E0214).
- **D62.** *Removed with the facts emitter.* Declaration records for the
  move checker's count.
- **D63.** The editor drops its fallback for a file with no project.
  Deferred to the editor's work after the merge.
- **D64.** The `deprecated` attribute is reconsidered once the lint of D49
  exists.  Deferred with it.
- **D65.** Explicit receivers on every non-static function, as D36.
  Deferred to a branch of its own after the merge.
- **D66.** With the primitive keywords (D18), a cast followed by a
  comparison without parentheses (`x as u32 < y`) is refused and the
  parentheses asked for.  Deferred with D18.
- **D67, D75.** *Replaced by the five rules.* The audit rounds and the
  third condition of the old finish line.
- **D68.** A class that declares a constructor binds it in the value
  namespace beside the class in the type namespace: `Node(1)` calls the
  constructor.
- **D69-D72, D76.** *Dropped* (2026-09-28, above).
- **D73.** `new C(args)` and the by-value `C(args)` are checked against the
  constructor of their arity.
- **D74.** The monomorphizer matches type arguments by identity: a type
  argument's shape is its kind in the arena, and a written instantiation is
  compared as the type it denotes, never by its display text.
- **D77.** The fast check catches what the full build does for a struct
  compared with `null`, `printf`-style call arguments, and `Point(1, 2)` on
  a struct (closed by D59).

### Open questions carried from the ledger

- **Q1.** *Answered by measurement.* Per-item `public` was not needed at a
  scale that blocked the work.
- **Q2.** *Answered.* An `extern module` alias (`extern module libc as
  cit;`) is a real module in the graph, with a stamp, not a namespace
  symbol resolution special-cases.
- **Q3.** A module cycle is E0501, which names the modules in the cycle.
  Whether it prints the cycle's path, in order, is open; see
  `docs/roadmap.md`.
- **Q4.** Whether `std::Range` is a name: nothing re-exports `Range` at the
  root of `std` today, so the path is refused.  Open; see
  `docs/roadmap.md`.
- **Q13.** *Answered by D68.*
