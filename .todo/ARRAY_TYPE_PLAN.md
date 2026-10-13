# Array Type Syntax: Circumfix Migration Plan

> Status: **proposal / roadmap**. Nothing here is committed work. This document
> describes replacing Cryo's postfix array syntax `T[]` / `T[N]` with a
> circumfix form `[T]` / `[T; N]`, so that array types are self-delimiting and
> the type grammar carries no precedence question.
>
> Every current-state claim in §2 and §8 was verified against the tree on
> **2026-08-10** and carries a `file:line` anchor or a re-runnable command.
> Where a claim is judgment rather than measurement it is labelled as such.
> Measurement method is recorded in §11 so every number can be re-derived.
>
> **This is an array-syntax migration, not a reference fix.** It was surfaced
> by a question about nesting `&` inside an array, but §4 and §10 show the
> reference case is not what justifies it.

---

## 1. Purpose & scope

Cryo's type grammar mixes fixities: `&` / `&mut` are **prefix**, while `*`,
`[]`, `[N]`, `?` are **postfix**. Mixed fixity forces a precedence rule, and a
precedence rule forces a grouping escape hatch for whichever case loses. Cryo
currently has the precedence rule but *not* the escape hatch, so one direction
of nesting is simply unspellable.

**Goal:** make array types circumfix, so that arrays never participate in a
precedence question — the brackets delimit their own operand.

**In scope:** surface syntax of array types; the parser; type *display* in
diagnostics, the demangler, and the AST dumper; the migration of every array
type annotation in the tree; the spec and grammar documents; the editor
grammar.

**Out of scope:** the type system, `TypeRef`, mangling, ABI, layout, codegen,
and ownership. §3.3 establishes that this is a pure surface change.

---

## 2. Current state, measured

### 2.1 `&` binds loosest; suffixes bind tightest

- `expr_parser.cryo:2630` — a leading `&` dispatches to `parse_reference_type`.
- `expr_parser.cryo:3043` — `finish_reference_type` recurses through
  `parse_type_annotation()`, so `&` consumes **everything to its right**,
  suffixes included.
- `expr_parser.cryo:2900` — `parse_type_suffixes_q` is a loop that wraps the
  base left-to-right for `*`, `[]`, `[N]`, `?`.

Consequence: `&Node*[]` is `&( (Node*)[] )`. The expressible set is
`&ⁿ(base + suffix-chain)`; a suffix can never appear *outside* a reference.

### 2.2 Grouping parens have no suffix pass

- `expr_parser.cryo:3110-3112` — the `(T)` grouping branch returns the inner
  annotation directly, skipping `parse_type_suffixes`.
- `expr_parser.cryo:3120` — the tuple branch immediately below *does* call it.

So `(int, int)[]` parses and `(int)[]` does not:

```
(int, int)[]   parses
(int)[]        error[E0100]: expected ')', found '['
(int)*         error[E0100]: expected ')', found '*'
(int)?         error[E0100]: expected ')', found '?'
```

`docs/grammar.md:333-336` agrees with the code — the array suffix is defined on
`BaseType`, and `(...)` is not a `BaseType`. This is a consistent gap, not a
code-vs-spec defect. **Types have no precedence-override syntax at all.**

### 2.3 `&&` is a lexer collision, not a parser one

`lexer.cryo:796` does maximal munch: `&&` lexes as one `AmpAmp`. The type
parser only tests `TokenType::Amp`, so `&&T` falls through to
`parse_base_type` and produces `error[E0104]: expected type, found '&&'`.
A space (`& &T`) parses but means ref-to-ref-to-array, not ref-to-array-of-ref.

### 2.4 `T[]` already *is* `Array<T, GlobalAlloc>`

Verified: a `Node[]` argument binds to an `Array<Node>` parameter with no
error, and `&Node*[]` monomorphizes `Array<main::Node*, GlobalAlloc>`.

Note a doc tension worth resolving in the same pass: `docs/cryo.md:264` (§2.4)
describes `T[]` as the *raw* array type for FFI and scratch buffers, and says
the shorthand desugars to `Array<T>` "in expression position when the prelude
is loaded." In a parameter annotation it desugars too. The spec understates
what `T[]` does.

### 2.5 Usage, tree-wide

| shape | occurrences |
|---|---|
| `&T*[]` — ref to array of pointers | 113 |
| `&T[]` — ref to array | 162 |
| `&Array<...>` | 23 |
| **`Array<&...>` — array of references** | **0** |
| `Option<&...>` / `Result<&...>` | 0 / 0 |
| `&T?` | 0 |
| `(&T)*` | 0 |

275 sites want exactly the current greedy reading. **No shape in the tree
requires a suffix outside a reference.**

The zero has a control: it is not zero because the syntax is unavailable —
`&Array<&Node>` compiles, links, and runs today (verified with a program that
sums an `Array<&Node>` and prints `total=7`). It is zero because
`docs/cryo.md:12` and `:2358` state Cryo has **no borrow checker and no
lifetimes**, so a reference stored in a container is an unchecked dangling
pointer with no compiler help. The shape is expressible and discouraged.

### 2.6 The `[` token is free

- `expr_parser.cryo:2651` — a leading `[` in type position hits one targeted
  diagnostic: *"bracket tuple types '[T, U]' are no longer supported; use
  '(T, U)'"*.
- `expr_parser.cryo:3123` — the doc comment for `parse_bracket_tuple` survives
  with **no function body under it**; the next construct is the
  `// Type Helper Predicates` banner. The function was deleted and the comment
  orphaned. (Minor cleanup, independent of this plan.)

Nothing else claims leading `[` in type position.

### 2.7 `is_type_start` is dead

`expr_parser.cryo:3132` defines `is_type_start()`; grep finds **zero callers**
tree-wide. It lists `Amp`, `LParen`, `Identifier`, `KwVoid` and would need
`LSquare` — but only if it is ever revived. Flagged so a future reader does not
mistake it for a live disambiguator that this plan forgot.

---

## 3. The change

### 3.1 Surface forms

| meaning | today | proposed |
|---|---|---|
| dynamic array | `T[]` | `[T]` |
| fixed buffer | `T[16]` | `[T; 16]` (D1) |
| named-const size | `T[CHUNK]` | `[T; CHUNK]` (D1) |
| ref to array | `&T[]` | `&[T]` |
| ref to array of ptr | `&T*[]` | `&[T*]` |
| ref to array of ref | *unspellable* | `&[&T]` |
| nested | *unspellable* | `[[T]]` |

### 3.2 Why circumfix rather than the alternatives

Brackets delimit their own operand, so array nesting needs no precedence rule
and no grouping escape. The alternatives each answer the precedence question
instead of removing it, or move the cost:

- **grouping gains a suffix pass** (`&(&T)[]`) — smallest change, but keeps
  mixed fixity and taxes the nested case with parens.
- **`&` binds tightest** — silently reinterprets 275 existing sites with no
  diagnostic. Rejected.
- **all-prefix, Go style** (`&[]&T`, `*T`) — uniform, but `docs/cryo.md:241`
  makes C-mirroring pointer syntax an explicit design commitment, and there are
  308 bare `T*[]` plus thousands of `T*` sites.
- **all-postfix** (`T&`, `T&[]&`) — uniform, but splits `&`'s fixity between
  type and expression position, and `mut &T` has no natural postfix form.

### 3.3 What does *not* change

`T[]` already resolves to `Array<T, GlobalAlloc>` (§2.4), and mangling is
structural — the observed mangling for `Array<Node*>` is
`5Array$LPN$L4main.4Node$G_N$L3std.5alloc.9allocator.11GlobalAlloc$G$G`, which
encodes the generic instantiation, not the surface spelling.

Therefore: **no change to `TypeRef`, `docs/cryo-mangling-spec.md`,
`docs/abi.md`, layout, codegen, ownership, or drop insertion.** This is a
parser + display + mass-rename change. That is the principal de-risking fact
in this document and should be re-verified at Phase 0 (§7).

---

## 4. What this buys — and what it does not

**Buys:**

1. Array types can never raise a precedence question, in any future
   combination, without a grammar change.
2. `[T]` composes with anything: `&[&T]`, `[[T]]`, `[T*]`, `Option<[T]>`.
3. Reclaiming `[...]` for arrays is coherent with having already removed
   `[T, U]` bracket tuples (§2.6) — it finishes that removal rather than
   reversing it.
4. It is decidable now, before the v1.0 freeze, and undecidable cheaply after.

**Does not buy:**

1. **It does not remove the precedence question from the type grammar.** `*`
   and `?` remain postfix, so `(&T)*` and `&T?`-meaning-`Option<&T>` stay
   unspellable without D5. It removes the question *for arrays*, which is
   where the pressure is (275 sites vs 0).
2. **It fixes no defect that current code exhibits.** §2.5 measured zero uses
   of every shape the change would newly enable. The justification is
   forward-looking uniformity, not a live bug. See §10.

---

## 5. Decisions required before Phase 1

None of these are settled by an existing spec. They change the plan's shape and
cost, so they need sign-off before any code moves.

| # | Decision | Options | Cost delta | Recommendation (judgment) |
|---|---|---|---|---|
| **D1** | Does the fixed-size form migrate too? | (a) `[T; N]` — one array syntax, migrate ~319 more sites. (b) keep `T[N]` — two fixities for arrays forever. | (a) +319 sites, +1 parser form. (b) 0. | **(a).** (b) leaves exactly the inconsistency this plan exists to remove, and violates one-way-to-say-a-thing. If `T[N]` stays it should be because it is deliberately the *C-shaped FFI buffer*, and the spec must say so. |
| **D2** | Is `T[]` deleted or kept as a deprecated alias? | (a) deleted at Phase 3. (b) kept indefinitely. | (b) 0, but permanent dual syntax. | **(a).** A deprecated alias that never expires is dual syntax with extra words. Phase 3 exists to close it. |
| **D3** | Does `T*` stay postfix? | (a) yes. (b) migrate to `*T`. | (b) ≈2000+ sites and breaks a stated design commitment. | **(a).** `docs/cryo.md:241` commits to the C convention. Out of scope. |
| **D4** | Display form in diagnostics, demangler, AST dump | (a) new form `&[Node*]`. (b) keep `&Node*[]`. | (a) touches 4 sites (§8.2). | **(a).** A diagnostic that prints syntax the compiler rejects is a trap. |
| **D5** | Does grouping `(T)` also gain a suffix pass? | (a) yes — closes `(T)*`, `(T)?` too. (b) no. | (a) ≈3 lines at `expr_parser.cryo:3110`. | **Ask.** Independent of this plan and nearly free, but it is a grammar addition and §2.5 measured 0 demand. Worth doing *only* if the answer to "should types have a general grouping form" is yes on principle. |
| **D6** | Branch and pin cadence | one long-lived branch, phase boundaries are commits | — | Match `NAME_RESOLUTION_PLAN.md` D3: one branch, commits at phase boundaries, repin at each (§6). |

---

## 6. Bootstrap ordering — the hard constraint

**This is the section that makes the migration non-trivial. Read it before
touching the parser.**

Cryo is self-hosted. `PIN := $(ROOT)/bin/cryo` (`Makefile:17`) is the compiler
that builds `compiler/src` into `STAGE2 := $(ROOT)/compiler/build/cryo`
(`Makefile:18`), and `cryo: stdlib runtime-tiers` (`Makefile:217`) means the
**pin also builds the stdlib**.

Therefore: *no syntax may appear in `compiler/src` or `stdlib` until a pin that
understands it already exists.* A single commit that both teaches the parser
`[T]` and rewrites the sources in `[T]` cannot build — the old pin cannot read
the new sources.

This forces the parser change and the source migration into **separate phases
with a repin between them**, which is why §7 has three repins.

### 6.1 Both pins, every time

`pin: pin-linux-impl pin-windows-impl` (`Makefile:297`). The Windows half
**skips silently** when the cross-toolchain is absent (`Makefile:302-312`) —
it prints `[skip]` and succeeds.

If a phase lands with only the Linux pin refreshed, `bin/cryo.exe` can no
longer parse `compiler/src`, and Windows self-host is broken until someone
repins on a capable machine — with a *skip* message rather than a failure as
the only warning. Use plain `make pin`; never a variant that suppresses a half.

Verified present on this machine (2026-08-10):

- `x86_64-w64-mingw32-gcc` — found
- `.toolchains/llvm-win/lib/libLLVM-C.dll.a`, `libclang.dll.a` — present
- `wine-9.0` — found

So both pin halves should build here. Whether `selfhost-check`'s wine-based
Windows half completes is **not** established by toolchain presence — confirm
it at Phase 0 rather than assuming (§7).

### 6.2 Ordering within a phase

`make test` and `make selfhost-check` rebuild from `$(CRYO_SOURCES)`
(`Makefile:28`). Editing sources mid-run reads as a broken fixed point. Each
phase is: edit → build → test → `selfhost-check` → `make pin` → commit.

---

## 7. Phases

Each phase states the measurement to predict **before** editing, per house
rules. A number that moves unpredicted is a finding to explain.

### Phase 0 — baseline and de-risk

No source changes.

- Re-run every §2 and §8 measurement with §11's commands; record the numbers.
- Confirm §3.3: dump the mangled symbols for a program using `T[]` and the
  same program using an explicit `Array<T>`, and diff with `nm`. **Predict:
  identical.** If they differ, §3.3 is wrong and this plan needs rewriting
  before Phase 1.
- Run `make selfhost-check` (~17 min, both OS) on an unmodified tree and
  confirm the Windows half actually *runs* rather than skipping (§6.1).
- Record `make test` and `make b1-check` baselines.

**Exit:** green baseline recorded; §3.3 confirmed by `nm`; Windows half
observed to run or explicitly known-skipped.

### Phase 1 — parser accepts both forms (additive)

Sources stay in `T[]`. Only the parser changes, so the **current pin can still
build it**.

- `expr_parser.cryo:2651` — replace the bracket-tuple diagnostic with a real
  `[T]` / `[T; N]` parse producing the same `ArrayAnnotation` the postfix path
  builds (`:2936`, `:2980`).
- Keep the postfix `[` branch in `parse_type_suffixes_q` (`:2931-2988`)
  untouched and working.
- Remove the orphaned `parse_bracket_tuple` doc comment (`:3123`).
- Add tests covering `[T]`, `[T; N]`, `[T; CONST]`, `&[T]`, `&[&T]`, `[[T]]`,
  `[T*]`, `Option<[T]>` — and the same programs in `T[]` form, both compiling
  to the same thing.

**Predict:** `make test` count rises by the new tests, nothing else moves.
`b1-check` and `roster-check` unchanged. **If `b1-baseline.txt` moves, stop** —
the parser change has reached name resolution, which it should not.

**Exit:** both syntaxes parse to identical types; full green; `make pin`
(both OS); commit.

### Phase 2 — migrate the tree

The Phase-1 pin understands both forms, so sources may now use `[T]`.

Order matters only in that `compiler/src` and `stdlib` must migrate before the
pin is refreshed; `tests/` and `examples/` may lag until Phase 3 because the
Phase-1 pin still accepts `T[]`.

- `compiler/src` — ~1629 type-position sites across 112 files.
- `stdlib` — ~12 sites across 6 files.
- `tests/` — ~45 sites; `examples/` — 1 site.
- Fixed-size sites (~319) only if **D1(a)**.

A mechanical rewrite is possible but must not be trusted blind: the postfix
form is context-sensitive (`a[i]` indexing vs `T[N]` type), so any script must
be anchored to type position and its output reviewed. §11 gives the anchored
patterns; they are a starting point, not a sanctioned rewriter.

**Predict:** `make test` unchanged — same tests, same results. `selfhost-check`
byte-identical. **A behavioral diff here means the rewrite changed a type, not
a spelling** — the whole point of §3.3 is that it cannot.

**Exit:** zero `T[]` in `compiler/src` + `stdlib` (§11 command returns 0); full
green; `selfhost-check` clean; `make pin` (both OS); commit.

### Phase 3 — remove the postfix form

`compiler/src` is now `[T]`-only, so the pin can build it without postfix
support.

- Delete the `[` branch from `parse_type_suffixes_q` (`:2931-2988`).
- Add a targeted diagnostic for `T[]` pointing at `[T]` — mirroring how
  `:2651` currently handles removed bracket tuples. **New diagnostic wording
  needs sign-off before it lands** (house rule).
- Migrate any remaining `tests/`, `examples/` sites first, or the suite goes
  red at this boundary by design.

**Predict:** every remaining `T[]` in the tree becomes a compile error. Count
them before deleting; the number of new failures should equal that count
exactly. A mismatch means the inventory missed a site class.

**Exit:** one array syntax tree-wide; full green; `selfhost-check` clean;
`make pin` (both OS); commit.

### Phase 4 — spec, grammar, tooling

- `docs/grammar.md:333-336` — rewrite the `Type` production; array moves out of
  the `BaseType` suffix list into its own bracketed form.
- `docs/cryo.md:264` (§2.4) — rewrite, and fix the §2.4 understatement in
  §2.4 of this document (the `T[]` → `Array<T>` desugar is not
  expression-position-only).
- `docs/cryo.md:253` (§2.3) — **state the binding rule**: `&` takes everything
  to its right. It is currently unstated, which is what made this whole
  question hard to answer from the docs.
- `tools/CryoAnalyzer/syntaxes/cryo.tmGrammar.json` — no array-type rule
  currently matches (grep for `[]`/`array` returns nothing), so this may be a
  no-op; verify by opening a `[T]` file in the editor rather than by grep.
- `make api-index` — regenerate; `docs/stdlib-api.txt` renders signatures.

**Exit:** `make api-index-check` green; grammar and spec describe what the
parser does.

---

## 8. Surface inventory (measured 2026-08-10)

### 8.1 Migration volume, type position only

| tree | `T[]` | `T[N]` | files (`T[]`) |
|---|---|---|---|
| `compiler/src` | 1629 | 34 | 112 |
| `stdlib` | 12 | 157 | 6 |
| `tests` | 45 | 126 | — |
| `examples` | 1 | 2 | — |
| **total** | **~1687** | **~319** | — |

These are regex counts anchored to `:` / `->` / `<` / `,` and are a **lower
bound** — an unanchored count of `T[]`-shaped text gives 1767 in
`compiler/src`, so roughly 140 sites sit in contexts the anchor misses
(multi-line generic argument lists are the likely bulk). Phase 2 must not
treat the anchored figure as complete; the Phase 3 error count is the real
census.

### 8.2 Code sites to change

| site | role |
|---|---|
| `parser/expr_parser.cryo:2651` | leading-`[` diagnostic → becomes the `[T]` parse entry (Phase 1) |
| `parser/expr_parser.cryo:2931-2988` | postfix `[` branch — kept in P1, deleted in P3 |
| `parser/expr_parser.cryo:3123` | orphaned `parse_bracket_tuple` doc comment — delete |
| `parser/expr_parser.cryo:3132` | `is_type_start` — dead (§2.7); add `LSquare` only if revived |
| `types/arena.cryo:1259` | `elem_name + "[]"` display (D4) |
| `types/arena.cryo:1360` | `elem + "[]"` display (D4) |
| `resolver/demangler.cryo:268` | `inner + "[]"` demangled output (D4) |
| `AST/dumper.cryo:85` | `printf("[]")` in the AST dump (D4) |
| `parser/expr_parser.cryo:3110-3112` | grouping suffix pass — **only if D5(a)** |

`mangled_name.cryo:171` and `demangler.cryo:394` also contain `"[]"`, but as
the spelling of the **index operator** for operator overloading — unrelated,
do not touch.

### 8.3 Goldens

`tests/test-roster.txt` and `tests/b1-baseline.txt` are the two committed
goldens; neither contains array-type text. Golden churn is expected to be
**zero**, and any movement is a finding, not a thing to update silently.

Note `.gitignore` has a repo-wide `*.txt` — any new golden this work adds needs
an explicit `!` negation or it never lands and CI fails on a fresh clone.

---

## 9. Risks and landmines

1. **A pin refreshed on one OS only** (§6.1). The failure is a `[skip]` line,
   not an error. Highest-consequence risk in the plan.
2. **A mechanical rewrite that catches `a[i]`.** Indexing and fixed-size array
   types share bracket syntax; only type position may be rewritten. Review the
   diff, do not trust the script.
3. **Editing sources during `make test` / `selfhost-check`.** Both rebuild from
   `$(CRYO_SOURCES)`; a mid-run edit reads as a broken fixed point.
4. **`runtime/.bin` holding the other OS's objects** after a `selfhost-check` —
   the next Linux build fails at *link* with `__ImageBase undefined`. Fix:
   `make stdlib runtime-tiers`. Expect to hit this at least once across three
   repin cycles.
5. **Phase 3 boundary is red by design** until `tests/` and `examples/` are
   migrated. Do not read that red as a regression; do not weaken a test to
   clear it.
6. **New diagnostic wording** for the removed `T[]` form needs sign-off before
   it lands.

---

## 10. The case against (read before starting)

The plan should be judged against the honest version of its own motivation.

- **No current code needs it.** §2.5 measured zero uses of every shape this
  change newly enables: `Array<&T>`, `Option<&T>`, `&T?`, `(&T)*` — all 0. The
  nesting problem that surfaced this is theoretical in this codebase.
- **The shape it enables is one the language cannot make safe.** With no borrow
  checker and no lifetimes (`docs/cryo.md:12`), `[&T]` is a dangling-pointer
  container. Making it *ergonomic* is arguably the wrong direction.
- **It is not a full fix.** `*` and `?` stay postfix; the precedence question
  survives for them (§4).
- **The cost is ~2000 edited sites and three repin cycles**, each gated on a
  ~17-minute both-OS `selfhost-check`, against a benefit that is uniformity
  rather than a fixed defect.

The counterargument — and it is a real one — is that this is exactly the class
of decision that is cheap now and permanent after a v1.0 freeze, and that
"`[T, U]` bracket tuples were already removed" leaves `[...]` sitting unused
in type position where it would read best.

A materially cheaper alternative exists if the goal is only *"no unspellable
type"*: **D5(a) alone** — give grouping a suffix pass. Three lines, one phase,
no repin cadence, no migration, and it closes `(T)[]`, `(T)*`, and `(T)?`
together. It answers the precedence question rather than removing it, and it
means writing `&(&T)[]`. That trade is the actual decision.

---

## 11. Method — how to re-run every measurement

```bash
# §2.5 usage counts
grep -rPoh '&\w+\*\[\]'      compiler/src stdlib --include=*.cryo | wc -l   # 113
grep -rPoh '&\w+\[\](?!\w)'  compiler/src stdlib --include=*.cryo | wc -l   # 162
grep -rPoh '&Array<'         compiler/src stdlib --include=*.cryo | wc -l   # 23
grep -rPoh 'Array<\s*&'      compiler/src stdlib --include=*.cryo | wc -l   # 0
grep -rPoh 'Option<\s*&'     compiler/src stdlib --include=*.cryo | wc -l   # 0

# §8.1 migration volume, anchored to type position (LOWER BOUND)
grep -rPoh '(?::|->|<|,)\s*&?\s*(?:mut\s+)?[\w:]+\s*\*{0,3}\s*\[\]' \
    compiler/src --include=*.cryo | wc -l                                   # 1629
grep -rPoh '(?::|->)\s*&?\s*(?:mut\s+)?[\w:]+\s*\*{0,3}\s*\[\s*[0-9A-Za-z_]+\s*\]' \
    stdlib --include=*.cryo | wc -l                                         # 157

# Phase 2 exit check — must return 0
grep -rPo '(?::|->|<|,)\s*&?\s*(?:mut\s+)?[\w:]+\s*\*{0,3}\s*\[\]' \
    compiler/src stdlib --include=*.cryo | wc -l

# §3.3 — surface-only proof
nm compiler/build/cryo | grep 5Array    # mangling encodes Array<...>, not the spelling
```

Basic `grep` has no `\t` and no lookaround — these use `-P` deliberately.

Probe programs live outside the repo and need `CRYO_STDLIB`, or use `bin/cryo`
(which finds the stdlib from any directory; `compiler/build/cryo` does not).
A scratch project needs a `[project]` section header in its `cryoconfig` —
bare keys parse but the entry point is not found.
