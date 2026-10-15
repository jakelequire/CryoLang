# Array Type Syntax: Circumfix Migration Plan

> Status: **plan, refreshed**.  Jake wrote the first version on 2026-08-10;
> this one keeps its structure and its decision list (D1-D6, plus new ones)
> and re-measures every claim against the tree at `341f949a` (the LSP merge),
> on branch `type-syntax-impl`.  It replaces postfix `T[]` / `T[N]` with
> circumfix `[T]` / `[T; N]`.
>
> Every current-state claim carries a `file:line` anchor or a probe that was
> run.  Where a claim is judgment rather than measurement it says so.  §11
> records how each number was taken.
>
> Retention: when the work lands, what is still open moves into
> `docs/roadmap.md` and this file is deleted.

---

## 0. What changed since August

The August plan was measured before the name-resolution migration, the LSP
rewrite and strict booleans.  Five of its premises no longer hold:

1. **Grouping parentheses now take suffixes.**  `(T)[]`, `(&T)[]`, `(&T)*`,
   `(&T)?` and `((i32) -> i32)[]` all parse and type-check
   (`expr_parser.cryo:2758-2761`).  August's D5 is built.  Nothing in the type
   grammar is unspellable any more; the case for circumfix is now uniformity
   and readability, not reach.
2. **`T[]` is not `Array<T>`.**  It is a separate builtin type
   (`TypeKind::Array`, dynamic) that is bridged to `Array<T>` for methods and
   for references.  By value the two refuse each other in both directions,
   an array literal cannot initialise an `Array<T>`, only the builtin can be
   indexed with `[]`, and they mangle differently (§2.4).  The surface change
   is still pure syntax, but "the shorthand for `Array<T>`" is the wrong
   model, and it bears on the class removal (§2.10, D7).
3. **Arrays of functions and of references work.**  Ruling 243 recorded that
   `((i32) -> i32)[]` did not parse and arrays of function values did not
   work; both build and run now (probe: `fns=11`, `refs=7`).
4. **Nested dynamic arrays crash.**  `i32[][]` with a row pushed into it dies
   with an access violation at run time; `Array<Array<i32>>` works.  Logged
   as a bug.  `[[T]]` is therefore not a working shape to advertise until it
   is fixed (§2.8).
5. **Objects embed source columns.**  Every panic site carries
   `(file, line, column)` as read-only data (`intrinsic_emitter.cryo:1034-1039`).
   `T[]` -> `[T]` is the same length, so its migration can be proven with
   zero objects moved; `T[N]` -> `[T; N]` is two characters longer and can
   legitimately move an object that has a panic site later on the same line.
   The migration is split accordingly (§7, slices 3a and 3b).

The volume also grew: about 2,390 dynamic and 360 fixed array types today,
against August's anchored lower bound of 1,687 and 319 (§8.1).

---

## 1. Purpose & scope

Cryo's type grammar mixes fixities: `&` / `mut &` are **prefix**, while `*`,
`[]`, `[N]`, `?` are **postfix**.  Mixed fixity needs a precedence rule, and
the rule needs a grouping form for the case that loses.  Cryo now has both:
`&` takes everything to its right, and `( )` regroups.

**Goal:** make array types circumfix, so that an array type never needs a
precedence rule or parentheses - the brackets delimit their own operand, and
the element reads first.

**In scope:** surface syntax of array types; the parser; type *display* in
diagnostics, the demangler, the AST dumper, the language server and the
binding generator; the migration of every array type in the tree; the spec,
grammar and editor grammar.

**Out of scope (as planned):** `TypeRef`, mangling, ABI, layout, codegen,
ownership, and the relation between the builtin dynamic array and
`Array<T>`.  §2.4 measures that the change can stay out of all of them; D7
asks whether it should.

**Not migrated:** `legacy/` (frozen, builds nothing; 494 `.cryo` files).

---

## 2. Current state, measured

### 2.1 `&` binds loosest; suffixes bind tightest

- `expr_parser.cryo:2745-2751` - a leading `&` (or `mut &`) dispatches to
  `parse_reference_type`.
- `expr_parser.cryo:3172-3181` - `finish_reference_type` recurses through
  `parse_type_annotation()`, so `&` takes everything to its right.
- `expr_parser.cryo:3029-3149` - `parse_type_suffixes_q` loops left to right
  over `*`, `[]`, `[N]`, `?`, wrapping the base each time.

`&Node*[]` is `&((Node*)[])`.  Fixed suffixes apply innermost first:
`u64[2][3]` is three rows of `u64[2]` (probe: `G[2][1]` of
`[[1, 2], [3, 4], [5, 6]]` is 6).  The circumfix spelling is
`[[u64; 2]; 3]` - **the numbers swap sides of the element**, which any
rewrite must get right.

These readings are pinned by `tests/tests/lang/array_type_grouping.cryo`,
added with this plan.  The migration rewrites that file too, and it must keep
passing.

### 2.2 Grouping parentheses take suffixes

`expr_parser.cryo:2753-2762` - `parse_paren_type` handles `()`, `(T)`,
`(T,)`, `(T, U)` and `(T, U) -> R`, and the caller runs the suffix pass over
the result.  Probe (`cryo check`, all accepted):

```cryo
function a2(xs: (&Node)[]) -> i64 { ... }
function a3(xs: ((i32) -> i32)[]) -> i64 { ... }
function a5(xs: (i32)[]) -> i64 { ... }
function a7(p: (&Node)*) -> i64 { ... }
function a8(p: (&Node)?) -> i64 { ... }
```

`docs/grammar.md:430` still defines the array suffix on `BaseType` only, so
the grammar now understates the parser: `( Type )` followed by suffixes is
not in it.  A spec-vs-code disagreement to fix in slice 5.

The cast target is parsed separately: `expr_parser.cryo:459-482` reads a
paren group or a base type, then suffixes (a `*` only when it cannot be
multiplication).  A `[` there must be added in slice 1, or `x as [u8; 4]`
will not parse.

### 2.3 `&&` is a lexer collision, not a parser one

Unchanged.  `&&T` lexes as one `AmpAmp` token; probe: `error[E0104]:
expected type, found '&&'`.  `& &T` parses.  Circumfix does not touch it.

### 2.4 `T[]` is its own type, bridged to `Array<T>`

The parser builds an `ArrayAnnotation` with size `-1` (dynamic) or a size /
size expression (fixed) (`expr_parser.cryo:3060-3116`).  The type resolver
turns it into `TypeKind::Array` (`types/resolver.cryo:340-350`).  That type
is not `Array<T>`:

| question | builtin `i32[]` | `Array<i32>` | evidence |
|---|---|---|---|
| mangling of a parameter | `Ai` | `5Array$L…GlobalAlloc$G$G` | `nm` on the probe: `by_sugar$FRAi`, `by_name$FRN$L3std.11collections.5array.5Array…` |
| layout | 24-byte fat value (`compound.cryo:134-136`) | struct `Array<T, A>` | - |
| `&i32[]` <-> `&Array<i32>` | accepted both ways | | probe ran: `cross=3 2` |
| `i32[]` -> `Array<i32>` by value | refused, E0214 | | probe |
| `Array<i32>` -> `i32[]` by value | refused, E0214 | | probe |
| `const n: Array<i32> = [4, 5];` | | refused, E0200 (`found i32[2]`) | probe |
| indexing `x[i]` | built in | refused: "cannot be indexed with `[]`" | probe on `Array<Array<i32>>` |
| methods (`push`, `length`, ...) | routed to `Array<T>` | native | `sema/type_utils.cryo:178-180, 271-273` |
| monomorphization | the instance `Array<T>` is created for each builtin array | - | `mono/state.cryo:431-441`, `mono/monomorphizer.cryo:299-309` |
| drop | owns a heap buffer, dropped (`types/ownership.cryo:590`) | `Drop` impl | - |

So `T[]` is an owning, growable builtin whose behaviour is borrowed from
`Array<T>`.  `docs/cryo.md:335-345` calls `T[]` a "raw dynamic array (FFI /
unsized)" that "desugars to `Array<T>` in expression position", and
`docs/cryo.md:3949` says the prelude needs `Array<T>` "because `T[]`
desugars to `Array<T>`".  Neither is accurate.  August's §2.4 and §3.3 rested
on the desugaring; the mangling result above replaces that argument: **the
change stays pure syntax because `[T]` would produce the same
`ArrayAnnotation`, hence the same `TypeKind::Array` and the same `A…`
mangling** - not because the two are one type.

Two further spec defects found on the way:

- `docs/cryo-mangling-spec.md:226` shows `int[8] -> A$L8$Gi`, and `:235`
  says "an array's length is not part of the mangling".  The code
  (`resolver/mangled_name.cryo:585-593`) encodes the length; `:235` is wrong.
- `types/checker.cryo:199-202` lets a fixed `T[N]` convert to a dynamic `T[]`
  of the same element.  That is how array literals initialise `T[]`; it is
  not stated in `docs/cryo.md`.

### 2.5 Usage, tree-wide

Counted by the token scanner described in §11, over `compiler/src`,
`stdlib`, `runtime`, `tools/CryoLSP`, `tests` and `examples`, comments and
strings excluded.

| shape | compiler | stdlib | LSP | tests | examples | total |
|---|---|---|---|---|---|---|
| `&T*[]` - ref to array of pointers | 142 | 0 | 9 | 0 | 0 | **151** |
| `&T[]` - ref to array | 266 | 1 | 9 | 4 | 0 | **280** |
| `T[]*` - pointer to array | 67 | 0 | 0 | 0 | 0 | **67** |
| `G<…>[]` - array of a generic | 43 | 0 | 0 | 1 | 0 | **44** |
| array inside `<…>` | 8 | 0 | 0 | 17 | 0 | **25** |
| `-> T[]` return type | 134 | 3 | 13 | 2 | 0 | **152** |
| `((A) -> R)[]` - array of functions | 0 | 0 | 0 | 3 | 0 | **3** |
| `(&T)[]` - array of references | 0 | 0 | 0 | 0* | 0 | **0*** |
| nested fixed `T[a][b]` | 0 | 0 | 0 | 1 | 0 | **1** |
| nested dynamic `T[][]` | 0 | 0 | 0 | 0 | 0 | **0** |
| `T[]?`, `T?[]` | 0 | 0 | 0 | 0* | 0 | **0*** |
| `&Array<…>` (not migrated; context) | 0 | 23 | 7 | 25 | 9 | 64 |
| `Array<&…>`, `Option<&…>`, `Result<&…>` | 0 | 0 | 0 | 0 | 0 | 0 |

\* Before this plan's test, which adds one of each.

The split is stark: **the compiler writes `T[]`** (2,231 sites, 7 uses of
`Array<`), while **the standard library and the examples write `Array<T>`**
(stdlib: 240 `Array<` against 11 `T[]`; examples: 29 against 0).

### 2.6 The `[` token is free in type position

- `expr_parser.cryo:2770-2774` - a leading `[` in type position reports the
  bracket-tuple diagnostic (E0104, *"bracket tuple types '[T, U]' are no
  longer supported; use '(T, U)'"*).  It is pinned by
  `tests/tests/negative/E0104_bracket_tuple_type.cryo`, which also pins the
  cascade that follows (`E0100 expected ';' or '{', found '['`).
- The orphaned `parse_bracket_tuple` doc comment August found
  (`:3253-3258`) is **deleted in the commit that adds this plan**; the
  verify run over it moved 0 objects.

Expression position needs no new disambiguation: generic arguments open only
at the turbofish `::<` (`expr_parser.cryo:1243-1245`), and the only other
type parsed in an expression is the cast target (§2.2).

### 2.7 `is_type_start`

Gone: the function August found dead has been deleted.  Nothing to do.

### 2.8 The shapes circumfix newly shows

Probe results with the pin (`1.0.0-alpha+167f22a6`):

| program | result |
|---|---|
| `(&Node)[]`, push two refs, sum through `&(&Node)[]` | builds, runs, `refs=7` |
| `((i32) -> i32)[]`, push two functions, apply | builds, runs, `fns=11` |
| `i32[][]`, push one row, read `length` | builds, **crashes** (0xC0000005) |
| `Array<Array<i32>>`, push one row, read `length` | builds, runs, `name=1` |
| `i32[][]` declared, nothing pushed | builds, runs |

The nested crash is logged in `.bug-report/`.  `drop_insertion.cryo:1663`
and `resolver/resolver.cryo:362-363` already carry comments that route
around `T[][]`.

### 2.9 Interactions the August plan did not cover

- **`virtual &Shape` (rulings 285-289) and E0313.**  Today `&Shape[]`,
  `(&Shape)[]` and `&Shape*[]` are each refused with E0313 at the trait.
  When trait objects come, an array of them is `(virtual &Shape)[]` in the
  postfix grammar and `[virtual &Shape]` in circumfix - the prefix keyword
  composes without parentheses.  This is the strongest forward-looking
  argument for circumfix found in this re-measurement (judgment).
- **The class removal (ruling 290).**  It rewrites the AST into enums, and
  the AST's child lists are today `ExpressionNode*[]`, `StatementNode*[]`,
  `TypeAnnotation*[]` (compiler: 399 bare `T*[]`, 142 `&T*[]`).  Doing this
  change first means those are written once, as `[Expr]` or `[Expr*]`.
  Which array type they should be is D7.
- **`new T[n]`** is not an array type: it is a raw allocation expression
  that yields `T*` (`expr_parser.cryo:1799-1880`,
  `new_delete_emitter.cryo:379-392`), with a run-time count.  Five uses, all
  in tests.  D8.
- **The binding generator** writes `T[N]` into generated `.cryo` files
  (`bindgen/generator.cryo:661`), so `cryo vendor` output and
  `make vendor-check` move with the display switch.
- **The version stamp** is `marker: u8[16]; text: u8[496];`
  (`utils/build_stamp.cryo:26-27`).  Its layout must not change (a slot
  change is a two-phase repin).  A syntax-only rewrite keeps it, and nothing
  follows the type on those lines, so slice 3b predicts it does not move.
- **Strict booleans:** no interaction found.  Array types do not appear in
  conditions, and `boolean[N]` arrays are unaffected.
- **Language server:** semantic tokens and references come from identities
  and do not care.  Hover, signature help, completion details and inlay
  hints print types through `handlers/names.cryo:233-235` (ruling 346's
  relative names), which needs the display switch.  The LSP harness (170
  checks) **asserts no array type text anywhere**, so a display switch would
  pass it unnoticed - slice 2 adds a check first.
- **Negative tests:** of 282 negative files, exactly one prints array type
  text today - `E0200_array_literal_widening_element.cryo` (`expected
  `i64[2]`, found `i32[2]``).  None of the `//~` annotations quote array
  type text.
- **Spans (fixed).**  `ArrayAnnotation.span`, `PointerAnnotation.span` and
  the `Option<…>` a `?` desugars to were taken *after* the closing token, so
  they pointed at the token after the type.  The editor's locator could not
  walk into them (definition and hover on `Node` in `xs: Node[]` answered
  nothing).  Each now runs from the type's first token - a group's `(`, a
  `const` - through the suffix; six language-server checks pin it, and the
  diagnostics that label such a type now underline the whole type (four
  negative files' carets moved, no `//~` line changed, 0 objects moved).

### 2.10 Which array the class removal should write (the open question)

The brief asked whether this change should stay pure surface syntax or also
settle array ownership and semantics.  What the tree shows:

- As syntax, it **can** stay pure: `[T]` lowers to the same annotation, the
  same type and the same mangling, and slice 3a can prove it with zero
  objects moved.
- But there are **two dynamic array types**, and they differ in everything
  but name (§2.4).  The compiler uses one, the library and examples the
  other.  Whichever one `[T]` names becomes the array the class removal
  writes thousands of times.
- The builtin has open defects the library type does not: nested arrays
  crash (§2.8), and `drop_insertion.cryo:1663` routes around them.
- `[T]` is also the spelling a reader may take for a borrowed view, which in
  Cryo is `Slice<T>` (`stdlib/core/slice.cryo`; 169 uses).  `&[T]` would be
  a reference to an owning growable array, not a slice.

So the syntax change does not *force* a semantic decision, but the class
removal will feel one.  D7 lays out the options.  This plan does not decide
it.

---

## 3. The change

### 3.1 Surface forms

| meaning | today | proposed |
|---|---|---|
| dynamic array | `T[]` | `[T]` |
| fixed array | `T[16]` | `[T; 16]` (D1) |
| constant-expression length | `T[CHUNK * 2]` | `[T; CHUNK * 2]` (D1) |
| ref to array | `&T[]` | `&[T]` |
| ref to array of pointers | `&T*[]` | `&[T*]` |
| pointer to array | `T[]*` | `[T]*` |
| array of references | `(&T)[]` | `[&T]` |
| array of functions | `((i32) -> i32)[]` | `[(i32) -> i32]` |
| optional array / array of optionals | `T[]?` / `T?[]` | `[T]?` / `[T?]` |
| nested fixed | `u64[2][3]` | `[[u64; 2]; 3]` |
| repeat expression (unchanged) | `[value; N]` | `[value; N]` |

The fixed form `[T; N]` matches the repeat expression `[value; N]` that
already fills one, so a field and its initializer read alike:
`cells: [boolean; CAPACITY] = [false; CAPACITY]`.  That answers the
roadmap's open question on constant-expression lengths: the length is the
same compile-time integer the repeat expression accepts
(`docs/cryo.md:347`), written after the `;`.

### 3.2 Why circumfix rather than the alternatives

- **Keep postfix, use grouping** - built already (§2.2).  It answers the
  precedence question instead of removing it: `(&T)[]`, `((i32) -> i32)[]`,
  `(virtual &Shape)[]`.
- **`&` binds tightest** - silently reinterprets 431 existing `&T…[]`
  sites with no diagnostic.  Rejected.
- **All-prefix** (`&[]&T`, `*T`) - `docs/cryo.md:310` commits pointer
  syntax to the C convention, and `T*` has thousands of sites.
- **All-postfix** (`T&`) - splits `&` between type and expression
  position, and `mut &T` has no natural postfix form.

### 3.3 What does not change

`[T]` builds the same `ArrayAnnotation` as `T[]`, which resolves to the same
`TypeKind::Array` and mangles to the same `A…` symbol (§2.4).  So: **no
change to `TypeRef`, `docs/cryo-mangling-spec.md`, `docs/abi.md`, layout,
codegen, ownership or drop insertion** - unless D7 chooses otherwise.

---

## 4. What this buys - and what it does not

**Buys:**

1. Array types never need a precedence rule or parentheses: `[&T]`,
   `[(i32) -> i32]`, `[virtual &Shape]`, `[[T]]`.
2. The element reads first and the length sits where the repeat expression
   puts it.
3. Nested fixed arrays read outside-in instead of the reverse
   (`[[u64; 2]; 3]` against `u64[2][3]`).
4. The class removal is written once, in the final syntax (Jake's reason for
   the order).

**Does not buy:**

1. `*` and `?` stay postfix, so `(&T)*` and `(&T)?` keep their parentheses.
2. It fixes no defect: every shape it makes easier is already spellable
   (§2.2), and one of them crashes (§2.8).
3. It does not make `T[]` and `Array<T>` one type (D7).

---

## 5. Decisions

None of these are settled by a spec.  Each says which slice it blocks.

**Decided by Jake on 2026-10-15** (rulings 347-354 in
`docs/resolution-rules.md`):

- **D1 (a)** - fixed arrays migrate: `T[N]` becomes `[T; N]` (347).
- **D2 (a)** - the postfix forms are refused once the tree is migrated; no
  alias period (348).
- **D4 (a)** - diagnostics, the language server, the demangler and
  generated bindings print the new form (349).
- **D7 (c)** - `[T]` is exactly today's `T[]`; unifying the builtin with
  `Array<T>` is a separate step before the class removal, on the roadmap
  (350).
- **D8 (a)** - `new u8[n]` stays (351).
- **D9** - wording "array types are written `[T]`: write `[i32]`" with the
  type filled in, an automatic fix, and a code of the existing parser error
  family; the code is chosen in slice 4 (352).
- **D10 (a)** - `[i32, i32]` keeps the "use `(T, U)`" message; the E0104
  test's follow-on annotation is re-annotated with the reason (353).
- **D11 (a)** - rulings are not respelled (354).

D3, D5 and D6 stand as written below.

| # | Decision | Options | Lean (judgment) | Blocks |
|---|---|---|---|---|
| **D1** | Does the fixed form migrate too? | (a) `[T; N]`, one array syntax, ~360 more sites. (b) keep `T[N]` as the C-shaped buffer. | **(a)** - it matches `[value; N]`, and two fixities for arrays is the inconsistency this exists to remove. | 1, 3b |
| **D2** | Is `T[]` refused or kept as an alias? | (a) refused once the tree is migrated (slice 4); both forms live only between slices 1 and 4, on the branch. (b) accepted for a release with a warning. (c) accepted indefinitely. | **(a)** - (c) is dual syntax; (b) costs a lint for a language with no external users yet. | 4 |
| **D3** | Does `T*` stay postfix? | (a) yes. (b) `*T`. | **(a)** - C convention, out of scope. | none |
| **D4** | Display form in diagnostics, demangler, AST dump, language server, generated bindings | (a) the new form. (b) keep the old. | **(a)** - a message that prints syntax the compiler refuses is a trap. | 2 |
| **D5** | Grouping takes suffixes | built (§2.2) | Keep it for `(&T)*` and `(&T)?`; the migration removes the `(…)[]` uses. | none |
| **D6** | Branch and pin cadence | one branch `type-syntax-impl`; a commit per slice; repin after slice 1 (required: the sources cannot use `[T]` before a pin reads it) and after slice 4 (so the pin refuses what the tree refuses); local gates only (ruling 338) | as stated | - |
| **D7** | **What `[T]` names** | (a) today's builtin dynamic array, exactly as `T[]` - pure syntax, provable with 0 objects moved; the builtin/`Array<T>` split stays a separate item. (b) `Array<T>` itself, one type - changes mangling, indexing and by-value conversion; objects move; not provable by identity. (c) as (a) now, and unify the two types as a separate change **before** the class removal. | **(c)** - keep this change provable, and settle the type before thousands of new array types are written in the class removal. Whether unification is the right end state is Jake's question. | (b) changes slices 1-3 entirely |
| **D8** | `new T[n]` | (a) unchanged - it is an allocation expression yielding `T*`, not an array type. (b) respell. | **(a)** - `new [T; n]` would read as a fixed array with a run-time length. | 4 (the refusal must not touch it) |
| **D9** | Diagnostic for the refused `T[]` | code and wording, e.g. E0104 *"array types are written `[T]`: write `[i32]`"* with a replacement suggestion | needs Jake: a new message is not mine to word. | 4 |
| **D10** | `[i32, i32]` once `[` starts an array type | (a) keep the targeted bracket-tuple message when a `,` follows the first type; the E0104 test's cascade annotation (`E0100 … found '['`) changes and is re-annotated with the reason stated. (b) generic "expected `;` or `]`". | **(a)** | 1 |
| **D11** | Old spellings inside the rulings appendix (`docs/resolution-rules.md`, e.g. 243) | (a) leave rulings verbatim as records. (b) respell them. | **(a)** | 5 |

---

## 6. Bootstrap ordering - the hard constraint

Cryo is self-hosted.  `PIN := $(ROOT)/bin/cryo` (`Makefile:11`) builds the
standard library and `compiler/src` (`cryo: $(LIBCRYO_A) runtime-tiers`,
`Makefile:327/333`).  So **no `[T]` may appear in `compiler/src`, `stdlib`
or `runtime` until a pin that reads it exists**.  Tests, examples and
`tools/CryoLSP` are built by the stage-2 compiler, so they may use `[T]` as
soon as slice 1 is in the tree.

### 6.1 Both pins, every time

`pin: pin-linux-impl pin-windows-impl` (`Makefile:419`); on Windows `make
pin` delegates to WSL.  Both halves must refresh, and `make verify-pin`
checks that both pins name one commit.  `wsl --shutdown` before and after
`make pin`, with no detached WSL processes running.

### 6.2 Ordering within a slice

`make test`, `make verify` and `make selfhost-check` rebuild from the
sources; an edit during a run reads as a broken fixed point.  A slice is:
edit -> `make verify` -> `selfhost-check` (x2 on Windows: require two
`FIXED POINT OK`) -> `make pin` if the slice needs one -> commit.
`selfhost-check` on Windows deletes `compiler/build/cryo.exe`: `make cryo`
after it.

---

## 7. Slices

Each states its prediction before any edit, per the house rules.  A number
that moves unpredicted is a finding to explain.

### Slice 0 - baseline (this commit)

- Re-measured everything (this document, §11).
- `tests/tests/lang/array_type_grouping.cryo`: five tests pinning the
  readings a rewrite could get wrong - `&Node*[]`, `(&Node)[]`,
  `u64[2][3]`, `T[]?`, `T?[]`.  Roster 2764 -> 2769 (`--merge`).
- The orphaned `parse_bracket_tuple` doc comment deleted.
- Proof: `make verify --baseline HEAD --require-identical` OK, 9731 / 1126
  objects, 0 moved.

### Slice 1 - the parser accepts both forms (additive)

- `parse_type_annotation_inner` (`expr_parser.cryo:2764-2774`): a leading
  `[` parses a type, then `]` (dynamic) or `;` and an expression then `]`
  (fixed), building the **same** `ArrayAnnotation` the postfix path builds
  (`:3060-3116`) - literal sizes decoded as there, others kept as
  `size_expr`.  The result goes through the suffix pass, so `[T]*`, `[T]?`
  and `&[T]` follow.  A `,` after the first element keeps the bracket-tuple
  message (D10).
- The cast target (`:459-470`) gains the same `[` branch.
- The new form's `ArrayAnnotation.span` covers `[` to `]`.  The postfix
  spans (§2.9) are left alone in this slice, so no diagnostic position moves.
- Tests: `array_type_grouping` duplicated in the new form, both compiling to
  the same thing; `[T; CONST]`, `[T; CONST * 2]`, `x as [u8; 4]`,
  `Option<[T]>`, `[(i32) -> i32]`, `[&T]` as an array of references; negative
  files for `[T;]`, `[T; ]`, `[; N]`, `[T` unclosed, and `[T; N` unclosed.

**Predict:** the tree uses no new form, so `make verify --require-identical`
moves 0 objects; the census rises by exactly the new tests;
`E0104_bracket_tuple_type`'s cascade annotation changes (D10), and nothing
else in compile-fail output moves (full-output diff over every negative file
and compile-fail project, before vs after: one file differs).  If any other
object or output moves, the new branch reached a path it should not.

**Exit:** verify OK; selfhost x2; `make pin` (both OS); commit.

### Slice 2 - display switches (D4)

Sites: `types/arena.cryo:1137-1139` and `:1280-1281` (two display
functions), `resolver/demangler.cryo:245-268`, `AST/dumper.cryo:117-120`
(which today prints `[]` for fixed arrays too - lossy), the language
server's `handlers/names.cryo:233-235`, `bindgen/generator.cryo:661`, the
help text at `passes/move_check.cryo:1976` (`&T[]`), and the keyword docs at
`tools/CryoLSP/src/handlers/keyword_docs.cryo:230` (`u8[]`).
`types/compound.cryo:124-126` builds a cache key, not display text: do not
touch.  `mangled_name.cryo` and `demangler.cryo:395` spell the index
operator `[]`: do not touch.

First, in its own commit or at the head of this one: an LSP harness check
that hovers an array-typed binding (floor 170 -> 171), so the switch is
visible to a gate.  Its expected text then changes in the switch, with the
reason stated.

**Predict:** objects 0 moved (display text lives in the compiler, not in test
objects); compile-fail output differs in exactly one file
(`E0200_array_literal_widening_element`: `i64[2]` -> `[i64; 2]`,
`i32[2]` -> `[i32; 2]`); `cryo demangle Ai` prints `[i32]`;
`make vendor-check` and any committed generated bindings move only in array
text.

### Slice 3 - migrate the tree (codemod)

The pin from slice 1 reads both forms.  The codemod rewrites **only** array
types, identified by the compiler's own parse (spans fixed for the purpose)
or by a token scanner checked against it - never by a bare regex, since
`a[i]` and `T[N]` share brackets.  Per ruling 268 the codemod lands in the
commit that runs it and is deleted in the next.

- **3a - dynamic arrays**, all areas.  Column-neutral (`i32[]` and `[i32]`
  are both five characters; `&Node*[]` and `&[Node*]` both eight) except the
  `(…)[]` forms, which shrink by two (3 sites, all in tests).
  **Predict:** `make verify --require-identical` moves 0 objects, apart from
  the three `(…)[]` test files' objects if a panic site follows on the same
  line; selfhost x2 fixed point; LSP harness unchanged.
- **3b - fixed arrays** (D1(a)).  `[T; N]` is two characters longer, so an
  object whose panic site sits later on the same line moves.  **Predict**
  that set before running: list every rewritten fixed type with a
  panic-capable expression after it on its line; the moved objects must be
  exactly those, and each must differ only in a source-location column.  A
  moved object outside that list is a rewrite that changed a type.

Order inside slice 3: `compiler/src`, `stdlib`, `runtime` (need the slice-1
pin), then `tools/CryoLSP`, `tests`, `examples`.  `legacy/` is not touched.

**Exit:** the scanner finds zero postfix array types outside `legacy/`;
verify OK as predicted; selfhost x2; commit.  No repin needed - the slice-1
pin builds the migrated tree.

### Slice 4 - the postfix form is refused

- Delete the `[` branch from `parse_type_suffixes_q` (`:3059-3117`); a `[`
  after a type reports D9's diagnostic with a replacement.  `new T[n]` is
  untouched (D8).
- Negative files: `T[]`, `T[N]`, `&T*[]`, `(&T)[]` each refused with the
  suggested rewrite asserted by `//~`.

**Predict:** count the postfix array types left in the tree just before (0
after slice 3); the new refusals equal the new negative files exactly.

**Exit:** verify OK; selfhost x2; `make pin` (both OS); commit.

### Slice 5 - docs and editor grammar

- `docs/grammar.md:430` - array leaves the `BaseType` suffix list for its own
  bracketed production; add `"(" Type ")"` followed by suffixes, which the
  parser already accepts.
- `docs/cryo.md` §2.4 (`:335-355`) rewritten: what `[T]` is (D7's answer),
  `[T; N]`, the constant-length rule, fixed-to-dynamic conversion; §2.3
  states that `&` takes everything to its right; §2.6's note (`:562-564`);
  §2.10's example (`:622`); the prelude table (`:3949`).
- `docs/cryo-mangling-spec.md:225-226` (examples), and `:235` corrected (the
  length *is* mangled).  `docs/abi.md` array mentions.
- `tools/CryoAnalyzer/syntaxes/cryo.tmGrammar.json`: types are coloured by
  name and `[` `]` are already bracket punctuation (`:412-418`), so this is
  expected to be a no-op.  Check by opening a `[T]` file in the editor, not
  by grep.
- `make api-index`: `docs/stdlib-api.txt` contains no array type text today
  (the public API uses `Array<T>` and `Slice<T>`), so it is expected not to
  move.

Docs that state behaviour commit with the slice that builds it; this slice
holds what is left.

### Slice 6 - cleanup

Delete this file, moving anything still open into `docs/roadmap.md`; delete
the codemod (if slice 3 did not already); remove the scratch.

---

## 8. Surface inventory (measured on `341f949a`)

### 8.1 Migration volume

| area | dynamic `T[]` | fixed `T[N]` | files |
|---|---|---|---|
| `compiler/src` | 2,231 | 17 | 116 |
| `stdlib` | 11 | 149 | 45 |
| `runtime` | 0 | 9 | 4 |
| `tools/CryoLSP/src` | 85 | 2 | 15 |
| `tests` (lang / negative / projects / stdlib) | 61 (37 / 5 / 15 / 4) | 176 (64 / 7 / 23 / 82) | 72 |
| `examples` | 0 | 2 | 1 |
| docs, fenced code | 5 | 7 | 3 |
| **total** | **~2,393** | **~362** | |

Prose mentions in docs: `cryo.md` 6 dynamic and up to 50 fixed-shaped spans
(most are `t[0]` indexing, not types), `abi.md` 2 + 2, mangling spec 2 + 1,
`roadmap.md` 3, `testing.md` 1, `resolution-rules.md` 1 (D11).

Against August: `compiler/src` 1,629 -> 2,231 is mostly method, not growth.
August's anchored regex was a stated lower bound (its unanchored count was
1,767); the token scanner counts every type position.  `tests` grew
(45 -> 61, 126 -> 176); `stdlib` and `examples` shrank.

Diagnostic text that spells an array type: 1 help string
(`move_check.cryo:1976`), 1 language-server keyword doc
(`keyword_docs.cryo:230`).  No `//~` annotation, no LSP harness check, no
`test.json` and no `docs/stdlib-api.txt` line contains array type text.

### 8.2 Code sites to change

| site | role | slice |
|---|---|---|
| `parser/expr_parser.cryo:2764-2774` | leading `[` -> `[T]` / `[T; N]` parse | 1 |
| `parser/expr_parser.cryo:459-470` | cast target `as [T; N]` | 1 |
| `parser/expr_parser.cryo:3059-3117` | postfix `[` - kept in 1, refused in 4 | 4 |
| `types/arena.cryo:1137-1139, 1280-1281` | display | 2 |
| `resolver/demangler.cryo:245-268` | demangled display | 2 |
| `AST/dumper.cryo:117-120` | AST dump (also lossy for fixed) | 2 |
| `tools/CryoLSP/src/handlers/names.cryo:233-235` | hover / inlay / signature text | 2 |
| `bindgen/generator.cryo:661` | generated bindings | 2 |
| `passes/move_check.cryo:1976` | help text | 2 |
| `tools/CryoLSP/src/handlers/keyword_docs.cryo:230` | keyword doc example | 2 |

Not to touch: `types/compound.cryo:124-126` (cache key),
`resolver/mangled_name.cryo` / `demangler.cryo:395` (the index operator's
name), `new T[n]` (D8).

### 8.3 Goldens

`tests/test-roster.txt` is the committed golden; it names tests, not types,
so it moves only when tests are added (slices 0, 1, 4).  August's
`tests/b1-baseline.txt` and `make b1-check` no longer exist.  The repo-wide
`*.txt` ignore still applies to any new golden.

---

## 9. Risks and landmines

1. **A pin refreshed on one OS only** (§6.1).
2. **A rewrite that catches `a[i]`.**  Struct-literal fields look like
   annotations (`LTargetMachine { raw: (*jobs)[j].tm }` reads as `: X[j]`);
   the scanner in §11 had three such false positives in `compiler/src`.
3. **Nested fixed arrays swap sides**: `u64[2][3]` is `[[u64; 2]; 3]`.
4. **Columns in objects** (§0 item 5): slice 3b moves objects by design;
   predict which.
5. **Editing sources during a long run.**
6. **Stale build artifacts** after a branch switch: `make stdlib && make
   cryo` before believing anything.
7. **The E0104 bracket-tuple test pins a cascade** that slice 1 changes
   (D10).  Re-annotating it is changing an assertion: state why in the
   commit.
8. **A display switch no gate sees**: add the LSP array hover check before
   switching (slice 2).
9. **`[[T]]` crashes at run time** until the nested-array bug is fixed;
   slice 1's tests must not use it as a working shape.

---

## 10. The case against

- **No shape is unspellable any more.**  Grouping (§2.2) made every
  composition expressible; circumfix is now a readability and uniformity
  change, about 2,750 sites and two repins, against no fixed defect.
- **The shapes it makes nicest are the ones the language cannot make safe**:
  `[&T]` is an array of unchecked references in a language with no borrow
  checker (`docs/cryo.md:12`).
- **It names the wrong question if D7 is wrong.**  Respelling `T[]` as
  `[T]` without deciding what `[T]` is gives the class removal a new
  spelling for the type that crashes when nested.

The counterargument stands: the cost only rises after v1.0, `[` is free in
type position, `[T; N]` already has a twin in `[value; N]`, and
`[virtual &Shape]` is the one future shape that reads badly in postfix.

---

## 11. Method - how each number was taken

- **Array type census (§2.5, §8.1):** a token scanner (scratch, deleted with
  the session; the codemod in slice 3 replaces it).  It drops comments,
  strings and characters; takes each `[` after an identifier, `>`, `*`, `)`,
  `?` or `]`; walks back over the element (paths, generic arguments,
  parenthesised groups, chained suffixes); counts `[]` always, and `[N]`
  only when the element is type-shaped (a primitive or a capitalised,
  non-SCREAMING name, or a parenthesised type) and the context before it is
  `:`, `->`, `as`, a generic argument list or a function-type parameter
  list.  Controls: a known site (`expr_parser.cryo:2809`,
  `TypeAnnotation*[]`) is found; for `compiler/src` and the language server,
  every non-comment line a plain `grep` finds with `T[]` and the scanner
  does not was read - all 92 are `return [];` array literals or string text;
  `stdlib` agrees exactly (11 and 11).  Known error: 3 struct-literal false
  positives in `compiler/src` (subtracted above).
- **`&Array<`, `Array<&`, `Option<&`, `Slice<`:** `grep -o` over
  non-comment lines.
- **Probes (§2.2, §2.4, §2.8):** small programs built with `bin/cryo.exe`
  (`1.0.0-alpha+167f22a6`), run on Windows; mangled names read with `nm`.
- **Negative output (§2.9):** `cryo check` on each of the 282 negative files
  with the pin, the output searched for backquoted array type text.
- **Objects:** `make verify ARGS="--baseline HEAD --require-identical"`.

`grep -P` does not work in Git Bash and reports nothing; none of the above
uses it.
