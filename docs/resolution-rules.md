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
| `trait-method` | `GenericRegistry::trait_item_slot` | `compiler/src/compiler/types/generic_registry.cryo` | member: a trait's declared method, by trait identity and leaf, to its position in the trait |
| `module-by-path` | `ModuleGraph::module_named` | `compiler/src/compiler/module_graph.cryo` | written module path to its module |
| `primitive` | `ResBase::is_primitive_spelling` | `compiler/src/compiler/resolver/res.cryo` | the fixed primitive table |
| `primitive` | `ResBase::primitive_of_alias` | `compiler/src/compiler/resolver/res.cryo` | the fixed primitive table: alias keywords |
| `primitive` | `ResBase::primitive_position` | `compiler/src/compiler/resolver/res.cryo` | the fixed primitive table: a primitive's position, which its definition is kept under |
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
    as rustc's collector does: an instantiation that appears when
    monomorphization substitutes types is demanded once emitting is driven
    by reachability, so it does not emit methods nothing calls.

98. **A generic type's bare name inside its own body infers its type
    arguments from the call**, as in Rust. `This::...` names the enclosing
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

112. **`abs`, `clamp` and `checked_abs` are per-primitive functions**, as
     Rust's `i32::abs` is, not generic functions with trait bounds: no
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
    generic template**, before monomorphization, as rustc does; it is not
    checked per instance. `a + b` under a bound on a user trait that is
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
    substitutes types into a copy are demanded and emitted**, as in rustc: a
    type appearing in a body counts as a use.

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
    the import line itself**, as in Rust, with an error of its own. The
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
    **method language items** (for example `FuturePoll`, `OptionTake`), as
    rustc does; it never builds a name to look up.

    ```cryo
    // what the async lowering writes at `x.await`, named by identity
    fut.poll(cx)      // LangItem::FuturePoll, not the leaf `poll`
    slot.take()       // LangItem::OptionTake, not the leaf `take`
    ```

50. **Ruling 47's error is approved as E0243**, "`parse` is imported
    twice".

51. **A method with a receiver, named by path, is a function value whose
    type takes the receiver first**, as in Rust. Ruling 46's ambiguity
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
    error**, as in Rust. This supersedes ruling 3's "the bare use is
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
    selection**, one fixed rule like Rust's literal default: with
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
    arguments from its use**, as in Rust: `const h = idf;`, `G::pick`, and
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
    refused where it is written**, with wording that says trait, in the
    style of Rust's "cannot find trait `Foo` in this scope" - not E0203's
    "cannot find type". The code is E0242, new for this; an impl head's
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
    full stop**, as in Rust. Generic code sees only its bounds, so an
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
