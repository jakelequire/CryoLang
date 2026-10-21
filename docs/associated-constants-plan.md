# Associated constants: survey and plan

Roadmap item 4 before the v1.0 freeze: a concrete type declares named
constants reached by a path, so `i32::MAX`, `f64::EPSILON` and
`Point::ORIGIN` are values, and the question whether `max_value()` stays is
answered with it.  Trait constants used as values come after v1.0, and trait
constants as array lengths are not in v1.0 (roadmap "Associated
constants").

This file is the plan.  It is deleted when the last slice lands.  Jake
answered every decision in section 5 with its lean, except decision 1:
a struct's constant may be written in its body OR in an `implement
struct` block (rulings 509-521).  Every
citation is `file:line` at `42736c04`.  Compiler paths are relative to
`compiler/src/compiler/`.

---

## 1. Today

### 1.1 What `i32::MAX` does

It is refused where it is written: `error[E0201]: cannot find value
`i32::MAX`` (probed with main's compiler).  The refusal is deliberate
(ruling 263, ruling 275, `docs/cryo.md:2579`):

- `sema/sema.cryo:4001-4021` `scope_member_binds_type`: a struct binds
  `T::member` only for a method it declares, an enum for any member (its
  variants), and a primitive never, "so `i32::MAX` is no value of `i32`".
- `sema/sema.cryo:4179-4183`: the E0201 at the end of
  `resolve_scope_resolution`.
- Negative test `tests/tests/negative/E0201_primitive_has_no_constant.cryo`
  pins it for `i32::MAX`, `Int::MAX` (an alias), `u8::MIN`, `f64::NAN`,
  `char::MAX` and `i64::MAX` (also `tests/started-passing:148`).

### 1.2 How the standard library exposes limits

- **Integers: static methods.**  `static min_value()` and `static
  max_value()` in `stdlib/core/primitives.cryo` for i8 (159-160), i16
  (193-194), i32 (227-228), i64 (264-265), u8 (297-298), u16 (334-335), u32
  (371-372), u64 (408-409), u128 (635-640), i128 (766-775), usize
  (836-837), isize (860-861).  They mangle as functions:
  `C$3i32-9max_value$Fv$Ri` (read off `nm` of the Windows archive).
- **Floats: module constants in `std::math`.**  `F64_EPSILON`,
  `F64_MAX`, `F64_MIN`, `F64_MIN_POSITIVE` and the four `F32_*`
  (`stdlib/math/_module.cryo:44-51`).  `f32`/`f64` have no limit methods.
  Infinity and NaN are FUNCTIONS, `math::infinity()`, `neg_infinity()` and
  `nan()` (`math/_module.cryo:53-62`), because no literal spells them.
- **`char` and `boolean`:** no limits at all.
- **Per-width digit counts:** 25 module constants `U8_MAX_DECIMAL_DIGITS`
  ... `U128_MAX_BIN_DIGITS` (`core/primitives.cryo:48-80`).
- **Private copies of limits:** `U64_MAX` twice (`time/duration.cryo:36`,
  `random/source.cryo:23`), `I64_MAX` (`time/clock.cryo:39`), a local
  `U64_MAX` (`alloc/allocator.cryo:121`), `I64_MIN_MAGNITUDE`
  (`collections/str.cryo:592`), json locals (`json/parser.cryo:338-340`),
  `DEADLINE_MAX` (`future/timer.cryo:52`); in the compiler,
  `SEMA_U64_MAX` and kin (`sema/helpers.cryo:44-58`) and
  `CONST_I64_MAX_MAG` (`const_table.cryo:72`).
- **Stale claim:** `stdlib/lib.cryo:72` says `primitives` provides `BITS`
  for every numeric type.  Nothing named `BITS` exists.
- **Index gap:** `docs/stdlib-api.txt` does not index `implement` blocks on
  primitives at all.  `max_value`, `abs`, `clamp` and the rest are absent
  from the index CLAUDE.md tells everyone to grep first.

Callers of `max_value()`/`min_value()` outside their definitions: stdlib 9
(`core/primitives.cryo:837,860,861`, `core/convert.cryo:122,131,140,158`,
`collections/str.cryo:630`), tests 11 in 6 files, compiler/tools/examples 0.

### 1.3 Existing machinery a constant can reuse

- **Module constants.**  A global `const` is a `VarDecl` with `is_global`
  (`AST/declaration.cryo:328`, parsed at `parser/parser.cryo:484-494`).  It
  is emitted as a global and mangled `C$<path>` (mangling spec section 11).
  Its initializer is folded when it is a constant expression (a number, a
  `null` or string literal, and array and struct literals of those); any
  other initializer runs once before `main` (`docs/cryo.md:755`, ruling
  174).  `mod::NAME` resolves through the `member-global` door
  (`DeclarationIndex::global_in_module`, `decl_index.cryo:1022`) to
  `MemberPin::Global`, and codegen loads it
  (`codegen/visit/ir_generator.cryo:1741`).
- **`ConstEval`** (`const_table.cryo`), the one folder for compile-time
  integers (array lengths, repeat counts, `static_assert`).  It is keyed by
  the canonical qualified name a `Res::Def` carries, with a cycle guard.
  `fold_scoped_res` (`const_table.cryo:532-560`) says a longer
  `TypeRelative` tail "is an associated item, which is sema's".
- **`Type::member` paths.**  The name layer stamps the qualifier
  (`resolver/name_resolution.cryo:1505-1751`): `Res::Def(module)` with
  `member_def` for a module member, otherwise `Res::TypeRelative(base, 1)`
  with base `Def`, `GenericParam`, `PrimTy` or `TraitThis`.  The member leaf
  of a type-relative path is left to sema, which tries ten things in order
  (`sema/sema.cryo:4079-4185`).
- **Associated types** are the one associated item that exists:
  `AssocTypeDeclNode` (`AST/declaration.cryo:1321`), the `assoc-type` door
  (`TraitDecl::assoc_type_index`, :1375), bindings on `ImplBlock`
  (:1689-1887), and projections in `types/resolver.cryo:551-650`.
- **`T::m()` under a bound** goes through `direct_param_path`
  (`sema/call_resolver.cryo:5755`) and
  `lookup_method_through_param_bounds` (`sema/method_binding.cryo:360`).
  The instantiated copy rewrites `T::` via `spec_owner`
  (`AST/substituter.cryo:840-894`).
- **The language server** answers a path's member with `Target::Def` when
  `ScopeResolutionExpr.member_def` is set
  (`tools/CryoLSP/src/handlers/identity.cryo:358`).  Today only a
  module-qualified path sets it.  A constant given a definition and stamped
  there gets go-to-definition with no new member pin.  Hover needs a case
  for the new declaration (`handlers/hover.cryo:136-200`), and completion
  and document symbols need a `Const` member (`handlers/members.cryo:24-90`
  knows `Field | Variant | Method`).
- **Emission.**  `codegen_global_var`
  (`codegen/ops/declaration_emitter.cryo:1172-1362`) emits an LLVM global.
  It tries a literal, then an int or real fold, then `try_const_struct`,
  then `try_const_array`.  Anything else is zeroed and computed in
  `__cryo.global_init`.  A folded constant is marked constant.  Use sites
  load through the address (`ir_generator.cryo:1283-1294`, :1741-1749).
- **Folding limits.**
  - `ConstEval::fold` bails on a value above `i64::MAX`
    (`const_table.cryo:321`), so `u64::MAX` cannot size an array or appear
    in a `static_assert`.
  - `fold_real` folds `1.0 / 0.0` to infinity and `0.0 / 0.0` to NaN
    (probed: both behave at run time).
  - Codegen keeps a u64 wrapper of its own over `ConstEval`
    (`try_fold_const_int`, `declaration_emitter.cryo:2249-2350`).
- **Visibility** for members: `Visibility` (`AST/_module.cryo:42`),
  checked by `MemberResolver::member_reachable`
  (`sema/member_resolver.cryo:859`) against the use site's module, E0353.

### 1.4 What the parser does with a `const` member today

- **Struct or union body:** E0101 "unexpected token 'const'"
  (`parser/parser.cryo:1309-1314`).  The rest of the line, `NAME: T = e;`,
  is then parsed as an ordinary field with a default value.
- **`implement` block (struct, enum or primitive) and trait body:** `const`
  is read as a method *named* `const`, and fails with E0100 "expected '('"
  (`parser.cryo:2253-2294`, :1653-1669).
- **Why:** a member name may be a keyword (`docs/grammar.md:183-192`), so
  that a bound C struct keeps a field called `type`.  A `const` member
  therefore needs one token of lookahead: `const` followed by an identifier
  is a constant, while `const` followed by `:` or `(` is a member named
  `const`.

### 1.5 Two defects in the path a type constant takes

Both were found by probe during this survey, and both are logged in
`.bug-report/`:

- **0104:** a folded struct constant written out of field order swaps its
  fields.  `const S: Point = Point { y: 2, x: 1 };` reads back `x=2 y=1`.
  `try_const_struct` zips by position and ignores each field's pin.
  `Point::ORIGIN` would go through exactly this.
- **0105:** `const BIG: f64 = 1_000.5;` stores `1.0`.  The global's float
  path parses the literal without removing `_`.

Doc drift, also seen:

- `docs/cryo.md:165`'s "integer literals above `i64::MAX` wrap" trap looks
  stale; `tests/tests/lang/integer_literals.cryo` pins the inline case
  working.
- `docs/cryo.md:755` says another global's value is computed at start-up,
  but a named integer constant folds.

### 1.6 No numeric trait exists

There is no `Bounded`, `Integer` or `Float` trait (only `Step`, `Ord`, the
operator traits and `Default`).  Generic numeric code that would want
`T::MAX` does not exist yet.

---

## 2. Design

### 2.1 Declaring a constant on a struct or union: inline

A constant is a member written in the type's body, alongside its fields and
methods (ruling 397):

```cryo
type struct Point {
    x: i32;
    y: i32;

    const ORIGIN: Point = Point { x: 0, y: 0 };

    static new(x: i32, y: i32) -> Point { return Point { x: x, y: y }; }
}

const p: Point = Point::ORIGIN;
```

- The type annotation is required, as for a module constant written in a
  type's body.  A constant has an initializer; `const N: i32;` is refused.
- Inside the type, `This::ORIGIN` names it, and so does `Point::ORIGIN`.
- An `implement struct Point { const ORIGIN: ... }` block declares one
  too; both forms work (ruling 509).

### 2.2 On an enum: in its `implement enum` block

An enum body holds only variants, so an enum's constants go where its
methods go:

```cryo
type enum Level { Quiet; Normal; Loud; }

implement enum Level {
    const DEFAULT: Level = Level::Normal;
}
```

A constant named like a variant is a redeclaration (see 2.9).

### 2.3 On a primitive: a standard-library `implement` block

A primitive has no body, so its constants are written where its methods
already are, in `stdlib/core/primitives.cryo`:

```cryo
implement i32 {
    const MIN: i32 = -2147483648;
    const MAX: i32 = 2147483647;

    abs(&this) -> i32 { ... }
}

implement f64 {
    const EPSILON: f64 = 2.220446049250313e-16;
    const MIN_POSITIVE: f64 = 2.2250738585072014e-308;
    const MAX: f64 = 1.7976931348623157e308;
    const MIN: f64 = -1.7976931348623157e308;
}
```

This dogfoods the feature (ruling 368), and the compiler gets no table of
primitive constants.  The other option is a compiler intrinsic table: the
values would be correct by construction, but it is a second mechanism, and
the language server would have no declaration to jump to.

**Infinity and NaN.**  No literal spells them.  The folder does fold
`1.0 / 0.0` to infinity and `0.0 / 0.0` to NaN, so the following are true
constants, with no intrinsic needed:

```cryo
const INFINITY: f64 = 1.0 / 0.0;
const NAN: f64 = 0.0 / 0.0;
```

Options are in decision 4.

**User code may also declare constants on a primitive**
(`implement i32 { const ANSWER: i32 = 42; }`), as it may declare methods
there today.  This is also how slice 1 is tested before the standard library
can use the feature.

### 2.4 Evaluation: a constant is a module constant that has an owner

The proposed rule is one rule for both kinds of constant.  An associated
constant has the semantics of a module-level `const` (`docs/cryo.md`
section 3):

- It is emitted as one global, with an address.
- Its initializer is folded when it is a constant expression.  Otherwise it
  is computed once before `main`, with its module's globals.
- `ConstEval` folds an integer one, so `[u8; u64::MAX_DECIMAL_DIGITS]` and
  `static_assert(u8::MAX == 255)` work.
- Codegen may inline a folded scalar at its use.  That is an optimisation,
  not part of the semantics.

The alternative is compile-time only: refuse an initializer that does not
fold, give the constant no address, and inline it everywhere.  That is
stricter and would refuse `Duration`-style constants built by a call
(decision 3).

Cycles (`const A: i32 = This::B; const B: i32 = This::A;`) are refused the
way module constants' are.  `ConstEval` already has the guard.

### 2.5 Name resolution: a new member door, stamped in the name layer

- **New door `member-constant`:** a type's constant, by owner definition
  (or primitive) and leaf.  It reads a per-owner table built when types are
  declared.  It joins the doors table in `docs/resolution-rules.md`.
- **Concrete owners** (`Point::ORIGIN`, `i32::MAX`, `Int::MAX` through an
  alias, `This::ORIGIN` in the type's own body) are answered in the NAME
  LAYER: `stamp_module_scope` already knows the qualifier is a type, and
  constants are never overloaded.  It stamps `member_def` with the
  constant's definition.  Three things follow:
  - sema types the path from the definition, and codegen loads the global
    as it does for `MemberPin::Global`;
  - `ConstEval` folds it through the definition it already reads;
  - the language server gets hover and go-to-definition from `member_def`.
- **One answering path.** The member door answers the leaf once.  A name
  that is both a constant and a method of one type is refused at the
  declaration (2.9), so no path ever needs a second lookup to choose.  Two
  things change: `scope_member_binds_type` is no longer the place a
  struct's value member is decided, and its comment is rewritten.
- A constant reached by a value (`p.ORIGIN`) is refused, E0204 with a
  suggestion `Point::ORIGIN`, as a method reached without a call is.

### 2.6 Generics

- **Through a concrete type inside a generic body** (`i32::MAX` inside
  `function f<T>`): no difference from anywhere else.
- **`T::MAX` under a bound** needs a trait that declares a constant, i.e.
  trait constants (2.7).  Without them, `T::MAX` stays refused at the
  function (ruling 468's E0358 shape: no bound declares `MAX`).
- **A constant on a GENERIC type** (`type struct Pair<T> { const N: i32 =
  2; }`, reached as `Pair::<i32>::N`): one global per instance, or one per
  template when the type does not mention `T`?  Not needed for anything in
  the tree today.  Proposed: refused with its own error before v1.0, and
  additive later (decision 5).

### 2.7 Trait constants

```cryo
type trait Bounded {
    const MAX: This;
    const MIN: This;
}

implement trait Bounded for i32 {
    const MAX: i32 = i32::MAX;
    const MIN: i32 = i32::MIN;
}

function clamp_top<T>(x: T) -> T where T: Bounded + Ord { return T::MAX; }
```

The roadmap already puts trait constants as values after v1.0.  They are
additive: the declaration syntax is new, and nothing existing changes
meaning.  Before v1.0 the question is only what `const` in a trait body or a
trait implementation does.  Today it is a garbled E0100 cascade.  It should
become a clear refusal: "a trait cannot declare constants yet" (decision 6).

**Trait objects.**  A constant is not reached through a value, so it is
never in an object's table.  When trait constants arrive, an object of a
trait that declares a constant cannot answer `T::MAX` for `T` an object.
The rule a later change will need: such a trait is satisfied by no object,
as one with a method a table cannot hold is (E0306).  Nothing is needed
before v1.0.

### 2.8 Visibility

A constant takes `public` / `private` like any member, and the `public:` /
`private:` blocks apply to it.  Public is the default.  `private` confines
it to the declaring module (E0353), for a primitive's constant too, though
the standard library writes none private.  It goes through the same
`member_reachable` check as a field.

### 2.9 Collisions

One type has one member namespace for `::` paths.  A constant named like
another constant, a method (static or not) or a variant of the same type is
a redeclaration, E0205, refused at the later declaration.  A constant named
like a FIELD does not collide: a field is reached only by `.` and a constant
only by `::`.  The other option is to refuse that too (decision 7).

### 2.10 Language server

- **Hover:** `const MAX: i32 = 2147483647` (the folded value where one
  exists), with the doc comment.
- **Go-to-definition:** the constant's declaration.
- **Completion after `Type::`:** constants alongside static methods and
  variants.
- **Find uses:** the server's `uses.cryo` reads `member_def` already
  (`handlers/uses.cryo:383` handles variants by pin; a constant needs no
  pin).
- **Semantic tokens:** the constant token type.

### 2.11 Mangling

`Point::ORIGIN` is a data symbol whose path ends in a member segment:
`C$4Geom.5Point-6ORIGIN`.  It follows section 5's `-` member form and
section 11's data-symbol form.  A primitive's constant follows its methods'
shape, so `i32::MAX` is `C$3i32-3MAX` (as `C$3i32-9max_value...`).  This is
a new row in section 11 of the mangling spec, and no existing symbol
changes.

### 2.12 What is breaking and what is additive

**Must land before the freeze:**

- The decision on `max_value()`/`min_value()` (decision 2), because removing
  them later breaks programs.
- Where float limits live (`math::F64_EPSILON` versus `f64::EPSILON`,
  decision 4): keeping both freezes two spellings of one value.
- `const` as a member-leading keyword.  Today `const` may name a field
  (`const: i32;`) or a method (`const(...)`).  With one token of lookahead
  both keep working, but a method named `const` in a trait body (`const
  NAME ...` is then a constant) must be ruled out.
- The refusal of `const` in traits, so that trait constants can later be
  added without changing what any program means.

**Additive (after v1.0):** trait constants and `T::MAX`, constants on
generic types, and constants as type arguments (the roadmap's open
question).

---

## 3. Inventory: what would switch

| where | today | becomes | count |
|---|---|---|---|
| `core/primitives.cryo` | `static min_value()`/`max_value()` x12 types | `const MIN`/`MAX` (decision 2) | 24 methods |
| callers of those | `i64::max_value()` etc. | `i64::MAX` | stdlib 9, tests 11 (6 files) |
| `math/_module.cryo:44-51` | `F64_EPSILON` ... `F32_MIN_POSITIVE` | `f64::EPSILON` ... `f32::MIN_POSITIVE` (decision 4) | 8 constants; callers in `tests/tests/stdlib/math.cryo`, `fmt_float.cryo` |
| `math/_module.cryo:60-62` | `infinity()`, `neg_infinity()`, `nan()` | `f64::INFINITY`, `NEG_INFINITY`, `NAN` (decision 4) | 3 |
| `core/primitives.cryo:48-80` | `U32_MAX_DECIMAL_DIGITS` ... | `u32::MAX_DECIMAL_DIGITS` ... (decision 8) | 25 |
| private limit copies | `U64_MAX`, `I64_MAX`, `I64_MIN_MAGNITUDE`, `DEADLINE_MAX`, json locals | `u64::MAX`, `i64::MAX`, ... | ~10 declarations |
| limit literals | `> 4294967295`, `> 2147483647` clamps, `== 0xFFFFFFFFFFFFFFFF` sentinel | `u32::MAX`, `i32::MAX`, `u64::MAX` | ~35 stdlib lines (`collections/str.cryo` TryFrom checks, `json/value.cryo`, `future/reactor.cryo:451`, `net/tls/future.cryo:308`, `ffi/libc.cryo:561-570`, `random/secure.cryo:65`, `alloc/arena.cryo:102`) |
| compiler | `SEMA_U64_MAX`, `sema_int_unsigned_bound`, `CONST_I64_MAX_MAG`, bindgen range picks | `u64::MAX`, `u8::MAX`, ... | ~15 lines (`sema/helpers.cryo:44-58`, `const_table.cryo:72,253`, `bindgen/importer.cryo:1493-1498,2061`, `lex/lexer.cryo:1066`) |
| user-type constants | module `const` beside a type | type constants (decision 8) | ~40-60 stdlib candidates, e.g. `time/duration.cryo:31-35` `NANOS_PER_SEC`..., `time/datetime.cryo:39-49`, `net/http/request.cryo:39-45`, `json/parser.cryo:32` + `serializer.cryo:31` (`MAX_DEPTH` twice) |

These stay as literals or module constants: Win32 `INFINITE`/`WAIT_FAILED`
`0xFFFFFFFF`, invalid-id sentinels (`decl_index.cryo:117`, `res.cryo:108`),
bit masks, protocol values (HTTP/2 window and settings, `SOL_SOCKET`), and
every FFI constant.  They are values of a protocol, not limits of a type.

`static zero()`-style constructors (`Duration::zero()`,
`IpV4Addr::localhost()`) could become constants.  They are not in this
work: a struct-valued constant is a load of a global, and the static method
already reads well.

---

## 4. Slices

The bootstrap orders the slices.  The standard library is compiled by the
PIN, so the standard library cannot declare `implement i32 { const MAX ...
}` until a pin that understands constants exists.

0. **Bugs 0104 and 0105** (decision 13): fold a struct constant by each
   field's pin, and clean a global's float literal.  Each gets a project
   test that fails on the parent.  Prediction: only objects holding an
   out-of-order constant struct literal or an underscored float global move.
1. **The feature in the compiler.**
   - Parse `const NAME: T = e;` in struct and union bodies and in inherent
     `implement` blocks (struct, union, enum, primitive), with one token of
     lookahead, so that keyword-named fields keep working.
   - Refuse it in a trait body, a trait implementation and a generic type,
     with clear errors (decisions 5 and 6).
   - The AST node, its definition, the `member-constant` door, and the
     name-layer stamp.
   - Sema typing; codegen as a global, with the mangling above;
     `ConstEval`; visibility; collisions.
   - Language server hover and go-to-definition.
   - Docs: a new `docs/cryo.md` section 8 subsection "Constants", 9.4 and
     12.3 updated; grammar; mangling spec section 11; the doors table; and
     ruling 263's text superseded.
   - Tests: unit tests and projects (cross-module, private, alias, `This::`,
     array length, struct-valued, computed initializer, user
     `implement i32 { const ... }`); negative files for each refusal; an
     LSP fixture.  The `i32::MAX` negative test still passes, because the
     standard library declares nothing yet.
   - Prediction: 0 existing objects move; the new projects are declared in
     `tests/started-compiling`, and the new negative files in
     `started-passing`.
2. **Repin** to slice 1's compiler (`make pin`).  Not a commit on its own;
   it rides in slice 3.
3. **The standard library's limits.**
   - `MIN`/`MAX` on every integer type; float constants (decision 4); the
     decision-2 fate of `max_value()`/`min_value()` with every caller
     migrated.
   - The private `U64_MAX`-style copies and the limit literals replaced;
     `lib.cryo:72`'s `BITS` claim fixed.
   - The E0201 negative test rewritten: names still not declared stay
     refused, and the declared ones move to a positive test (decision 9).
   - `api-index.py` taught primitive `implement` blocks and constants, and
     the index regenerated.
   - `docs/cryo.md` 12.3 rewritten; rulings 263 and 275 marked superseded.
   - Proven with a same-path standard-library build before and after,
     hashing `.o` files, because verify's object compare cannot see a
     standard-library edit.
4. **The compiler's own limits**, `u64::MAX` and kin in
   `sema/helpers.cryo`, `const_table.cryo` and bindgen.  This needs the
   slice-3 standard library and the slice-2 pin; it may fold into slice 3.
5. **Per-type constants in the standard library** (decision 8), if in
   scope: `Duration`'s unit counts, the digit counts, the json depth.
6. **Roadmap:** item 4 marked built, and the leftovers (trait constants,
   generic types, constants as type arguments) listed.  This rides in the
   last code commit, and this file is deleted there.

---

## 5. Decisions for Jake

1. **Where a struct's constant is written.**
   - A) Inline in the body, like its methods (ruling 397); an `implement`
     block only where a method would be one (enums, primitives, a split
     type's other files).
   - B) `implement` blocks only.

   Lean A.
2. **`max_value()` / `min_value()` once `i32::MAX` exists.**
   - A) Removed in slice 3, every caller migrated: one spelling for one
     value, and the removal is breaking, so it must happen before the
     freeze.
   - B) Kept beside the constants.
   - C) Kept, documented as the old spelling, removed later.

   Lean A.
3. **What a constant's initializer may be.**
   - A) What a module constant's may be: folded when it is a constant
     expression, otherwise computed once before `main`.  The constant has
     an address.
   - B) Compile-time only: a non-folding initializer is refused, and there
     is no address.

   Lean A: one rule for both kinds of constant.  Related:
   `ConstEval` cannot fold above `i64::MAX`, so `u64::MAX` cannot size an
   array.  Widening the folder is not in this work unless Jake wants it.
4. **Float limits and non-finite values.**
   - A) `f64::EPSILON`, `MAX`, `MIN`, `MIN_POSITIVE` (and the `f32` set) as
     constants, with `math::F64_*`/`F32_*` removed; `INFINITY`,
     `NEG_INFINITY` and `NAN` as constants initialized from arithmetic
     before `main`, and `math::infinity()`/`nan()` removed.
     The initializers are written `1.0 / 0.0` and `0.0 / 0.0`, which fold,
     so these are true constants.
   - B) As A, but keep `math::infinity()`/`nan()` beside them.
   - C) Add the finite limits only, and keep `math::infinity()`/`nan()`.

   Lean A.  The `math::PI`
   family stays in `std::math` either way: those are math constants, not
   properties of `f64`.
5. **Constants on a generic type** (`Pair<T>`).
   - A) Refused with its own error before v1.0; additive later.
   - B) Allowed when the initializer and type do not mention a type
     parameter, as one global for the template.
   - C) One per instance.

   Lean A.
6. **`const` in a trait body or trait implementation before v1.0.**
   - A) Refused where written with a clear error ("a trait cannot declare
     constants"), which keeps trait constants additive.
   - B) Build trait constants now, `T::MAX` included.

   Lean A, as the roadmap orders.
7. **A constant named like a field of the same type.**
   - A) Allowed (`.` and `::` never meet).
   - B) Refused (E0205) like a clash with a method or variant.

   Lean B: one name meaning one member is easier to read, and costs
   nothing today.
8. **Scope of the standard-library migration in this work.**
   - A) Type limits only (slices 3 and 4).
   - B) Also the per-width digit counts (`u32::MAX_DECIMAL_DIGITS`) and the
     clear per-type constants (`Duration`'s unit counts, the json depth).
   - C) B plus every module constant that names a type.

   Lean B.
9. **The `E0201_primitive_has_no_constant` negative test** once `i32::MAX`
   is declared.
   - A) Keep the file and its E0201 for names still undeclared
     (`i32::NOT_DECLARED`, `char::MAX` if `char` gets none, `Int::MISSING`
     through the alias), and move the declared spellings to a positive
     test.
   - B) Delete it and write a new one.

   Lean A.  This changes an existing test, which is why it is asked.
10. **Error codes.**  Each of the new refusals needs one: `const` in a
    trait, a constant on a generic type, a constant without an initializer,
    and a constant reached through a value.
    - A) New codes in the E02xx resolution range for the first three, and
      E0204 (no such field) for the fourth, with a suggestion.
    - B) Reuse E0117 (refused syntax) for the trait and generic forms.

    Lean A.  The codes and wordings come to Jake with slice 1.
11. **Which constants `char` and `boolean` get.**
    - A) `char::MIN`/`char::MAX` (U+0000 and U+10FFFF) and none for
      `boolean`.
    - B) None for either.

    Lean A.
12. **`BITS`.**  `lib.cryo` already advertises it.
    - A) Add `BITS: u32` to every integer type in slice 3.
    - B) Leave it out, and correct the comment.

    Lean A.
13. **Bugs 0104 (struct constant fields swapped) and 0105 (`1_000.5`
    stored as `1.0`).**  Both are silent miscompiles of module constants
    today, and both are in the path a type constant takes.
    - A) Fix both as slice 0 on this branch.
    - B) Fix them in a separate branch first.
    - C) Leave them for the bug-fixing pass.

    Lean A.
