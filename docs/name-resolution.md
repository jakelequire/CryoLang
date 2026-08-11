# Name Resolution — Normative Specification

> Status: **normative spec, in progress.** This document defines how Cryo
> resolves names. Where it disagrees with the current implementation, the
> document is right and the implementation is a defect. Each such gap is
> listed in §8 with the phase that closes it.
>
> Companion roadmap: `.todo/NAME_RESOLUTION_PLAN.md`. That document sequences
> the work; this one defines the target. Measurements cited here were taken on
> **2026-08-03** against branch `naming-impl` with the `CRYO_RESOLVE_COUNTER`
> instrumentation, and each states its method so it can be re-run.

---

## 1. The root cause

Cryo resolves names in six independent subsystems that do not share an answer.
The roadmap (§2.1–§2.10) catalogues them. This section states why they exist,
because every rule below follows from it.

**Visibility in Cryo is a tie-breaker among collisions, not a gate.**

Measured 2026-08-03, by compiling a two-module program with the stage-2
compiler:

| Program | Result |
|---|---|
| Module `B` calls non-public `Helper::secret_fn()`, qualified | **compiles clean** |
| Module `B` imports `Helper`, calls `secret_fn()` bare | **compiles clean** |
| Module `B` references non-public `Helper::Secret` type | **compiles clean** |
| Two modules each declare non-public `secret_fn`, `B` calls it bare | **E0154 ambiguous** — and the diagnostic offers *both private functions* as candidates |

The mechanism is a single fast path in
`resolve_qualified_scoped` (`compiler/src/compiler/decl_index.cryo:1345`):

```cryo
const cands: SymbolStr[] = this.lookup_qualified_alternatives(bare);
if (cands.length == 0) { return ScopeResolution::NotFound; }
if (cands.length == 1) { return ScopeResolution::Unique(cands[0]); }  // <- no gate
```

`is_candidate_public` and `ns_imports` are consulted only in the
multi-candidate branch below it (`:1359-1360`). A name that is unique
program-wide therefore resolves from anywhere, regardless of visibility or
whether it was imported.

**Consequence.** Every declaration in the program is in every module's
effective namespace. "Which one did you mean?" then has no principled answer,
so each stage must guess; each guess is wrong in some case; each fix is another
heuristic. That is the fallback cascade, and it is why deleting fallbacks
without fixing visibility cannot hold — the ambiguity they paper over is real
as long as everything is visible.

This also explains three otherwise-unrelated observations in the roadmap:

- **§2.4** the global leaf index *is* the mechanism of global visibility.
- **§2.9** the FFI parallel namespace exists because "declaring the bare name
  would shadow same-named types in other modules" — a workaround for having no
  encapsulation boundary.
- **§2.4** the LSP declines a wire-side `Diagnostic` because "Cryo's resolver
  exposes a dependency's type names globally." That is this defect, reported
  from the outside.

**Rule 0, from which the rest follows: a name is resolvable only if it is
visible. Visibility is checked before resolution, not after a collision.**

---

## 2. Scope of this document

Defines: what a name means at a use site, which declarations are reachable,
how paths resolve, when ambiguity is an error, and what downstream stages may
assume.

Does **not** define: module discovery on disk (`module_loader.cryo` is
unchanged), mangling (`cryo-mangling-spec.md`), or trait selection semantics
beyond where its answer is recorded.

---

## 3. Visibility

### 3.1 Declarations

Every declaration has a visibility. A top-level declaration is **public by
default**; `private` opts it out. `public` is accepted and is a no-op at top
level, written where an author wants the export to be explicit.

Visibility is not advisory in either direction: `private` on an item means
*no other module may name it*, and that is enforced as a gate (§3.3).

> **Decided 2026-08-04 (Q7).** Earlier revisions of this section specified the
> opposite default. The parser has always defaulted to public
> (`parser.cryo:405`), so the disagreement was invisible until visibility
> became a gate — see §9. `private` is now the only visibility keyword that
> carries information.

### 3.2 The export set

A module's **export set** is exactly its declarations not marked `private`.
Nothing else about a module is nameable from outside it. There is no path —
index, alias, fallback, or otherwise — by which a module-private declaration
becomes reachable from another module.

The known cost of this default: a new helper is exported the moment it is
written, and forgetting `private` is silent. §3.3's gate is what makes
`private` mean something when it *is* written; it does not and cannot recover
an export set the author never stated.

### 3.3 Enforcement is a gate

Visibility is checked **when a candidate is proposed**, not when candidates
collide. A single candidate that is not visible at the use site is
`Res::Err` with a diagnostic, never `Unique`.

> This is the single largest behavioral change in this spec. §8.1 records the
> measured blast radius.

---

## 4. Scope and the rib chain

A use site resolves a bare name against a **rib chain**, in this precedence
order:

```
  locals / generic params
    -> current-module items          (public AND private — a module sees itself)
    -> imported modules' export sets  (§3.2)
    -> prelude
```

Rules:

1. **Precedence is total.** An inner tier shadows an outer one silently; that
   is the point of the tier. A local shadowing an import is not a diagnostic.
2. **Ambiguity is reportable only *within* a tier.** Two imported modules both
   exporting `Foo` is ambiguous. An import shadowing the prelude is not.
3. **Prelude is the outermost rib, not a privileged tier.** "Imports shadow
   prelude" is a consequence of the ordering, not a rule anyone implements.
4. **Nothing is reachable that is not on the chain.** No global leaf index, no
   program-wide uniqueness, no namespace substring matching.

Rule 4 is what deletes the six lookup systems. Rules 1–3 are unchanged from
today's intended behavior.

---

## 5. Paths

### 5.1 Meaning

**The first segment resolves in scope; the remainder is rooted.**

The first segment resolves as a bare name through the rib chain (§4). Every
segment after it is resolved *within what the previous segment named*,
one segment at a time. `future::ready(...)` is legal exactly when `future` is
bound in scope, and then it means exactly one thing.

There is no substring matching, no suffix matching, and no dependence on what
other modules happen to exist in the program. A path that does not resolve is
an error with a suggestion, not an invitation to search.

### 5.2 One entry point

All path resolution goes through one function:

```
resolve_path(segments, ns, scope) -> Res
```

- **`ns` is a parameter**, not a separate code path. Type position and value
  position use the same function with a different `ns`. The §2.6 asymmetry —
  where bare `Range` resolved in annotation position but was E0154-ambiguous in
  call position on the same line — is unrepresentable after this.
- **`scope` is explicit.** There is no ambient cursor. This is load-bearing:
  the `home-module preference` step (`types/resolver.cryo:1387`, **26,473
  answers**, the largest single answering step in the cascade) exists solely to
  correct an ambient cursor pointing at the wrong module. With an explicit
  scope it has nothing to correct and is deleted, not fixed.

### 5.3 Type-relative paths

`T::Assoc`, `This::Item`, `Self::new` are **not** path resolution. The base
resolves via `resolve_path`; the remainder needs `T`'s bound and is finished in
sema (§6.2). `resolve_path` records the base `Res` plus a count of unresolved
trailing segments.

---

## 6. The two answers

Resolution produces two kinds of answer, recorded separately. Rust splits these
as `Res`/`PartialRes` vs `TypeckResults::type_dependent_defs`; Go as
`types.Info.Uses` vs `Selections`. Two independent implementations converged
here; it is not a stylistic choice.

### 6.1 `Res` — path to definition

Produced by the resolver, before types exist.

```
Res = Unstamped            // no resolver has answered for this node
    | Def(SymbolStr)       // a declaration or module, by canonical qualified name
    | Local(SymbolID)      // a local binding
    | GenericParam(SymbolStr)  // a type parameter, BY NAME
    | PrimTy(SymbolStr)    // a primitive
    | Err                  // resolution failed; diagnostic ALREADY emitted
```

**`Def` carries a qualified name, not a `SymbolID`.** An earlier revision of
this section wrote `Def(SymbolID)`. A `SymbolID` indexes the Resolver's own
arena, and two things make it the wrong handle for this answer: modules are not
declared there at all, so a module scope has no `SymbolID` to name, and every
downstream consumer — `DeclarationIndex`, the type arena, the mangler — keys on
the interned qualified name, so a `SymbolID` stamp would have to be translated
back at each use, re-deriving the thing the stamp exists to stop re-deriving.
Decided 2026-08-11 with the first stamped lane (§8.5).

**`Unstamped` is not "unresolved".** It means no resolver has claimed the node —
which for a path-bearing node is a routing fact, not a failure: a first segment
naming a *type* needs a receiver type and belongs to `TypeDependentRes` below.
Reaching a stage that requires the stamp while still `Unstamped` is an ICE
(§7.2 mechanism 4), never a licence to search.

Two invariants:

- **`Res` names a definition, never an instantiation.** `T` resolves to
  `Res::GenericParam`, not to whatever `T` is bound to at some call site.
  Substitution happens strictly below `Res`, in the type layer. This is what
  makes `Res` safe for `ASTCloner` to copy verbatim, and it is the invariant
  whose absence forced `ASTCloner` to drop every `resolved_type`
  (`NamedAnnotation.pre_resolved` is a `TypeRef` — an instantiation — which is
  exactly the landmine this rule exists to prevent).
- **`Res::Err` is a value, not a failure.** It is produced once, with its
  diagnostic already emitted, and it poisons downstream without triggering
  re-lookup. **There is no state meaning "didn't resolve, try something else."**
  This is the single rule that would have prevented the cascade: most of the
  nine steps are error-recovery paths that were promoted to correctness paths.

### 6.2 `TypeDependentRes` — node to definition

Produced by sema, once the receiver type is known. Covers method calls, trait
selection, associated-item projection, and overload selection.

This **cannot** move into the resolver — dispatch needs the receiver's type.
Attempting it is what made an earlier revision of the roadmap's exit criterion
unreachable. `sema/call_resolver.cryo:1862` ("provided by multiple traits
implemented for this type") is permanently sema's, by design. Rust reports the
same condition from typeck (E0034), never from its resolver.

### 6.3 Storage

**The answer lives on the node.** A resolution slot on the AST node is exact
and cannot desynchronize. The current `ResolutionMap` is keyed by a packed span
— 32-bit file hash | 16-bit line | 16-bit col — and its own doc comment
(`resolver/resolution_map.cryo:31-38`) concedes that collisions silently
overwrite and that it is "best-effort … not a correctness gate."

Note precisely what the defect is: the **span key**, not the side table. A
node-identity-keyed side table would be exact. On-node storage is still the
better choice here — cheaper, and impossible to desynchronize — but a derived
span index rebuilt from node slots is legitimate for the LSP and dead-code
passes.

Path-bearing node kinds requiring a slot (inventory taken 2026-08-03; the
`ScopeResolutionNode` row landed 2026-08-11, and §8.2ah is the fallback it
retired):

| Node | Anchor | Slot |
|---|---|---|
| `IdentifierNode` | `AST/expression.cryo:104` | — |
| `ScopeResolutionNode` | `AST/expression.cryo:789` | **`scope_res`** (SCOPE segment) |
| `NamedAnnotation` | `AST/_module.cryo:456` | — |
| `NewExprNode` | `AST/expression.cryo:436` | — |
| `SizeofExprNode` / `AlignofExprNode` | `AST/expression.cryo:483`, `:506` | — |
| `CallExprNode` (callee) | `AST/expression.cryo:398` | — |
| `ImportDeclNode` (per segment) | `AST/declaration.cryo:575` | — |
| enum variant reference | `AST/expression.cryo:424` | — |

---

## 7. Enforcement

A specification that can only be honored by discipline will be violated under
deadline — that is the observed history of this subsystem. This section lists
what actually forces compliance, and is normative.

### 7.1 What the language can force — measured

Measured 2026-08-03 by compiling probe programs; the function row re-measured
2026-08-05; the **call/reference split** and the **type row** re-measured
2026-08-06 (§8.2n), which corrected both; the last three `NO` rows closed and
re-measured 2026-08-10 (§8.2ag). Every row is measured by a RUNNING program
where one can exist — an absent diagnostic is equally consistent with a gate
that passed and a probe that never reached it.

| Mechanism | Enforced? | Evidence |
|---|---|---|
| Exhaustive `match` over an enum | **YES** | `E0405`; `tests/negative/E0405_non_exhaustive_match.cryo` |
| Private struct/class **field** | **YES** | `E0353`; `tests/negative/E0353_private_field_access.cryo` |
| Non-public **function**, cross-module, **called** | **YES**, since 2026-08-05 | `E0353`; `tests/projects/visibility_gate` — all four *binding* doors, §8.1f |
| Non-public **function**, taken as a **value** | **YES**, since 2026-08-10 | `E0353`; `tests/projects/visibility_value_gate`, both spellings, §8.2ag |
| Non-public **static method**, cross-module | **YES**, since 2026-08-10 | `E0353`; `tests/projects/visibility_static_gate`, both spellings, §8.2ag |
| Non-public **instance method**, cross-module | **YES** | `E0353`; §8.2n positive control |
| Non-public **type**, cross-module, leaf **unique** | **YES** | `E0503` fires; §8.2n variant A |
| Non-public **type**, cross-module, leaf **shared** | **YES**, since 2026-08-10 | `E0503`; `tests/projects/visibility_type_mask`, §8.2ag |

Every row is a gate, and each is pinned by a corpus entry that the compiler
predating its fix builds at **exit 0** — the property that makes an entry a gate
rather than a decoration. Two rules carry all of them, and a verdict that breaks
either one is a defect regardless of what it catches:

**The use site comes from the syntax, never from the ambient cursor.** Every
verdict asks `module_ns_sym_of_file(span.file)` — the module that WROTE the
name. The cursor is parked on whichever module is being processed, so a verdict
computed against it describes the walk rather than the source. When a span names
no module the gate REFUSES to judge, because an invalid namespace compares
unequal to every real one: judging against it turns "I don't know where this was
written" into "written somewhere else" and rejects everything.

**The question is asked about a RESOLVED QUALIFIED SYMBOL, never about a bare
leaf.** A leaf-keyed question is "is any `Hidden` anywhere public?", and any
unrelated module can answer it yes — so enforcement keyed on a leaf is
switchable off by a declaration the name does not even resolve to.

The rider below **survives, narrowed, and is still normative.** `E0503` judges a
type where the source NAMES one — an annotation or a struct literal. A value
whose type is never written reaches no gate at all: measured 2026-08-10, a
`private` type returned by a public function and bound by inference crosses a
module boundary and answers a method call. Pinned as a `WRONG_` arm in
`tests/projects/resolution_tripwire`, which fails when someone closes it.

> **Any design that relies on "make the fallback API private" works only when
> the fallback is a FUNCTION. A private TYPE is still reachable wherever the use
> site can avoid naming it, so type-level fallback entry points must be DELETED,
> not hidden.**

The resolver's own enforcement still cannot depend on the feature it is
implementing, so the mechanisms below are what carry the remaining work.

### 7.2 The four mechanisms

1. **`Res` is an enum, and `match` is exhaustive.** Adding or changing a
   variant forces every consumer to handle it (E0405). This is the primary
   compile-time lever and the reason `Res` is a sum type rather than a struct
   with sentinel fields.

2. **Deletion, not deprecation.** Every string-keyed entity lookup reachable
   from `sema`, `mono`, or `codegen` ceases to exist. A stage cannot re-derive
   what it has no function to call. Since visibility cannot wall these off
   (§7.1), non-existence is the only durable boundary.

3. **`B1 == 0` is a CI gate, permanently.** The `CRYO_RESOLVE_COUNTER`
   instrumentation is not scaffolding to be removed after Phase 4; it is the
   mechanism that keeps the property true. The cascade grew for years because
   nobody could see it growing — steps were added empirically and pruned only
   when someone noticed "0 hits across all builds." A gate converts "do not add
   fallbacks" from a principle into a build failure.

   > **WIRED 2026-08-04.** `scripts/b1-gate.py`, `make b1-check`, golden at
   > `tests/b1-baseline.txt`, and a CI step next to `roster-check`. It runs a
   > fixed external target (`examples/09-json-config`) with
   > `--no-incremental CRYO_CODEGEN_THREADS=1` and asserts the B1 total plus
   > every per-site `B1`-flagged row.
   >
   > **It is a RATCHET, not a literal `B1 == 0`.** B1 is not zero today, so
   > that assertion would be red from the moment it was wired, and §7.2a's
   > lesson has an inverse: a gate that can *only* fail gets switched off. The
   > golden pins the current value and **any drift fails** — an increase is the
   > regression this exists to catch, and a decrease is progress that must be
   > re-pinned so the lower value becomes the new bound. A tolerated decrease
   > would leave the old higher number as the ceiling and let a later
   > regression back up to it pass unnoticed. When Phase 2–4 drive it to 0 the
   > golden reads 0 and this becomes the permanent gate, with no change to the
   > script.
   >
   > Three things that were not obvious and are encoded in the script:
   >
   > - **The `B1` flag is a family label, not the summation set.** `M1..M5
   >   calls` and `lookup_by_leaf calls` are flagged `B1` but are *not* summed
   >   (B1 counts answers, never attempts), and cascade step 5 is flagged `B1*`
   >   and excluded as nested inside `lookup_by_leaf hits`. Asserting
   >   `rows == total` is therefore wrong and makes the gate unrunnable; the
   >   invariant that does hold, and is checked, is `total <= row_sum`.
   > - **`cryo check` emits no counter report at all.** `ResolveCounter::report()`
   >   sits on the success path of a full build, after link
   >   (`instance.cryo:2647`), so every measurement needs a successful link —
   >   which is why the gate depends on `runtime-tiers` and why landmine 6
   >   presents as a B1 failure. The script recognizes `__ImageBase` and says so.
   > - **The golden needed a `.gitignore` exemption.** `*.txt` is ignored
   >   repo-wide; without `!tests/b1-baseline.txt` the golden is never
   >   committed and CI fails on a fresh clone with "no golden".
   >
   > Verified before being trusted: two consecutive runs agree exactly
   > (B1 = 19,292 over 20 rows), and the gate was made to fail on a simulated
   > increase, a simulated decrease, and a vanished site, each with a per-site
   > diff naming what moved.

4. **An unstamped node is an ICE, never a fallback.** A path-bearing node that
   reaches a stage after `NameResolution` without a `Res` is an internal
   compiler error. Corollary, and the rule that keeps this from regrowing:
   **a pass that synthesizes a path-bearing node must construct it with its
   `Res` already set.** The synthesizer knows what it is referring to — it is
   creating the reference. Desugaring (`async_lower`, `?`), and `ASTCloner`
   are all bound by this. The same lesson was already learned once in the async
   work, where a synthesized call had to set its own `arg_binding`.

### 7.2a The self-host gate was checking 1% of the IR on Linux — FIXED 2026-08-04

The self-host is the gate this work leans on hardest: "a name binding
differently shows up as a broken fixed point even when tests pass." That claim
was only true on one of the two halves.

`selfhost-check.py` verified the **windows** fixed point with
`_compare_ir_trees`, which walks every per-module `.ll` under the stage root —
243 modules, 103.6 MB. The **linux** half compared exactly one file,
`self/s3/cryo.ll` against `self/s4/cryo.ll`: the `llvm-link` artifact, **953,674
bytes against 104.4 MB of per-module IR**. A name that bound differently in any
of the other modules did not have to show up there, and the run still printed
`✓ FIXED POINT OK`.

Nothing was wrong with the *stages*; the two halves simply verified
non-comparable things, and the Linux number was small enough that the
discrepancy was visible in the report all along (`IR size: 953,674` next to
windows' `103,578,577`) without being read.

**Fix.** The linux verification now runs `_compare_ir_trees(S3_DIR, S4_DIR)` as
well, and a per-module mismatch fails the gate even when the linked artifact
matches. The linked `cryo.ll` check is kept — it is a cheap whole-program
check — but it is no longer the *only* one. Linux stages already write
per-module IR without `--emit-llvm`, so no stage command changed; if that ever
regresses, `_compare_ir_trees` returns "no per-module IR found" and the gate
**fails loudly** rather than passing on one file.

Verified against the already-built stage trees before the change was trusted:
match → `(True, (243, 104400627))`, mismatch → `(False, 'CLI.ll')`, missing →
`(None, 'no per-module IR found …')`; all three are handled.

> **The general lesson, and it applies to every gate in §7.2.** A gate that
> cannot fail is indistinguishable from a gate that passes. Two gates that are
> supposed to check the same property must be checked *against each other*, not
> just against their own output. This one was asymmetric for as long as the
> windows half has existed, and the whole point of §7 is that discipline does
> not survive deadline — so the gates have to be audited like code.

### 7.3 Buckets

The counter classifies every instrumented lookup into three buckets. The
two-bucket model in the roadmap (§3.2 rule 2) was measured wrong on
2026-08-03 and is superseded:

| Bucket | Meaning | Target |
|---|---|---|
| **B1** | fuzzy fallback — guesses from a string | **zero** |
| **B2** | type-dependent — genuinely needs a receiver type | stays; enumerated and justified |
| **B3** | authoritative — answers from scope/imports | **once per path**, not zero |

B3 exists because `resolve_qualified_scoped` accounts for 487,838 of
`lookup_qualified_alternatives`' 496,945 calls, and it is import- and
prelude-aware — close to what §4 specifies. Classifying it as "must reach zero"
inflated B1 from 144,639 to 492,867 and set an unreachable target.
**"Must reach zero" is correct for fuzzy fallbacks and wrong for the
resolver's own lookup**, whose correct target is once-per-path.

Counter caveats, both load-bearing when reading any number it prints:

- Counters are **per-process**. Multiprocess codegen (≥16 modules) drops child
  tallies. A baseline run must pin `CRYO_CODEGEN_THREADS=1`.
- Sites **nest** (cascade step 5 ⊂ leaf-index hits). Totals are taken over a
  disjoint selection and count site events, not distinct resolutions.

---

## 8. Known gaps against this spec

### 8.1 Visibility is not enforced (§3.3)

The defect in §1. Turning the gate on will reject code that compiles today.

> **Scope note, 2026-08-05.** This section and its numbers are about the TYPE
> lane — `decl_index`'s `resolve_qualified_scoped`. The FUNCTION lane is
> enforced as of §8.1f and is no longer a gap. The two lanes were always
> separate subsystems (§8.1c); only one of them has closed.

**Blast radius, measured 2026-08-03** (compiler building itself, via
`CRYO_VIS_AUDIT=1`, which logs every resolution that succeeded *only* because
the single-candidate fast path skipped the gate):

- 288,084 trips through the fast path
- **62,267 (21.6%) would be rejected** once gated
- those collapse to **1,131 distinct (use-site, candidate) pairs**

Classified by whether the use site's source file textually imports the
candidate's namespace (method: `scripts/`-style regex over `import` lines,
including brace lists; re-runnable):

| Class | Pairs | What it actually is |
|---|---:|---|
| Source **does** import it; `ns_imports` disagrees | **772 (68%)** | **Bug.** Brace-list imports (`import A::B::{ X, Y };`) do not register dependency edges for `A::B::X`. Dominated by `Compiler::AST::Expression`, `Compiler::Codegen::LLVMTypes`, `Compiler::AST::Declaration` — all brace-imported. |
| Implicit runtime dependency | **~318 (28%)** | **Omission.** `std::alloc::allocator` (157), `std::alloc::layout` (58), `std::core::ptr` (50), `std::collections::{hashmap,str,string}` (53). The compiler injects references to these for any heap type, f-string, or allocation. The prelude set (12 namespaces) omits every one of them — it was a hardcoded list here, and is derived from `stdlib/prelude.cryo` as of §8.2e; the omission is the same either way, because the prelude source does not name them. |
| Genuinely missing import | **~20 (2%)** | Add the import. |

**Reading: enforcing visibility is feasible**, but the second class is not what
it first appeared. See §8.1b.

### 8.1b Update after fixing class 1 (2026-08-03)

**Class 1 is fixed.** Brace-list import items are now scanned
(`module_loader.cryo`, `ModuleScanResult.import_items`) and recorded as
`ModuleInfo.imported_namespaces` — a **visibility** edge, deliberately not a
dependency edge, because `compute_order()` walks `dependencies` and an edge per
brace item recreates the false cycles the loader already avoids for
`public module`. `register_module_imports` now receives the union.

Re-measured:

| | before | after |
|---|---:|---:|
| would-be-rejected events | 62,267 | **21,296** |
| distinct pairs | 1,131 | **397** |
| of those, ARTIFACT | 772 | **38** |
| of those, GENUINE | 359 | 359 |

**Class 2 is NOT a prelude gap — it is lost use-site attribution.** The
`seed_prelude_namespaces` list turns out to mirror `stdlib/prelude.cryo`
exactly, so nothing is missing from it, and the prelude's own doc comment is
explicit that adding names has a cost. (That list is no longer hardcoded — it is
derived from the prelude source as of §8.2e, which is what makes "mirrors it
exactly" true by construction rather than by inspection.) Two measurements
identify the real mechanism:

- A module whose entire content is `public function open_fn() -> i32 { return 1; }`
  — no types, no strings, no heap — still logs **64 violation events** against
  `NonNull`, `AllocError`, `GlobalAlloc`, `RawBuffer`, `String`, `Str`. It
  cannot be referencing any of them.
- Every use site blamed for `std::alloc::allocator` is a **consumer** (`CLI`,
  `Compiler::AST`, …). The stdlib module that actually owns the code never
  appears.

So generic/template bodies are being resolved **in the scope of the module that
instantiates them, not the module that wrote them**. `resolve_scoped`
(`compilation_context.cryo:386`) passes `current_module_ns()` — "the module
currently being processed" — an ambient cursor. This is the same root defect
as the `home-module preference` step (§8.2, 26,473 answers), which exists
purely to correct that cursor.

**Consequence for sequencing — this supersedes the ordering proposed before
the measurement.** The visibility gate cannot be turned on while template
bodies are re-resolved by name in a foreign scope; they would fail the gate for
names they never wrote. Fixing the cursor is not a prerequisite that can be
skipped or approximated with an allowlist — an allowlist here would be exactly
the "bandaid to get an easy green" this architecture exists to prevent.

Revised order:

1. ~~Brace-import visibility edges~~ — **done**, class 1 retired.
2. **`Res` on the node (§6.3) plus the explicit-scope rule (§5.2)**, so an
   instantiated body carries the resolution from where it was written and is
   never re-resolved in the consumer's scope. This is §3.2 rule 5 and §7.2
   mechanism 4 doing the work they were specified for.
3. Wire item visibility so it is actually recorded (see the caveat below).
4. Turn the gate on. The ~20 genuinely-missing imports are added here.

Two caveats on the classification: `is_candidate_public` **defaults to `true`
when visibility is unrecorded** (`decl_index.cryo:1319`), so the measured
"0 rejected for non-public" means *visibility is largely never recorded*, not
that everything is correctly public — item visibility must be wired before the
gate can distinguish the two. And the import classifier is a textual regex, so
"source imports it" does not prove the name legitimately comes from there.
Neither caveat changes the conclusion's direction.

### 8.1a A diagnostic counted but never rendered — FIXED 2026-08-03

Found while probing brace imports: a two-module program using
`import VisTest::{ Helper };` reported
`Project compilation failed (1 errors, 0 warnings)` and printed **no
diagnostic at all**. An error that cannot be read is worse than a crash.

Root cause was three separate defects stacked on one input:

1. **A self-dependency made the module graph unplaceable.**
   `import A::{ X };` records the parent path `A` as the dependency edge, so a
   file whose own namespace is `A` gained a self-edge and could never be
   placed. `add_dependency` now drops an edge to the module's own name: it
   carries no ordering information, and the brace items are recorded
   separately as visibility edges anyway.
2. **The cycle was reported only to the debug channel.** `compute_order()`
   printed the unplaced modules and their blockers via `cdebug`, so a plain
   build showed nothing. It now records `topo_unplaced`, and the caller names
   the modules in the cycle in the E0501 message (this closes §9 Q3's
   "needs a printed cycle path", though a full cycle *path* rather than a
   member list is still worth doing).
3. **The E0501 diagnostic was buffered but never flushed.** Diagnostics render
   on `flush()` (`diag/sink.cryo:111`); the `compute_order()` failure path
   returned without calling it. Notably the adjacent E0505 path
   (`instance.cryo:1442`) carries a comment saying exactly this — *"without
   this the buffered E0505 would never reach the terminal"* — so the bug class
   had been found and fixed at **one** site while this one was missed. Every
   `CompilationResult::failed` path in `compile_project` was audited; the
   others either print directly with `fmt::eprintln` (no buffered diagnostic)
   or sit after the flush at `:2497`.

A fourth defect surfaced from the fix and was also closed: brace-list items
were not being **discovered**, on the assumption that the parent's
`public module` manifest already had. False when the parent has no
`_module.cryo` — `import A::{ B };` then compiled without `B` while
`import A::B;` compiled with it. Two spellings, two meanings, which is the
same failure shape as §2.6. Brace items are now discovered exactly like plain
import paths.

### 8.1c Function visibility, wired — and the second binder it exposed (2026-08-04)

§8.1b's caveat said the measured "0 rejected for non-public" meant *visibility
is largely never recorded*, and listed wiring it as step 3. Wiring it produced
a more interesting result than the count it was meant to fix.

**What was wired.** `set_type_visibility` recorded only types (5 sites, all in
`register_decl_in_index`). It is now `set_decl_visibility` — the old name
described a map that also has to answer for functions — and the
`FunctionDeclaration` arm records `func.is_public` alongside its
`register_name_mapping`. Intrinsics record `true` explicitly, as traits already
did.

**Read the zero with a positive control.** `is_candidate_public` defaults to
`true`, so an unwired recorder and a genuinely-all-public program are
indistinguishable from the counter alone. `CRYO_VIS_AUDIT=1` now emits
`VIS-RECORD <0|1> <qualified>` per recorded verdict, and `VIS-PRIVATE <kind>` for
every private candidate the fast path meets. Both are the control the earlier
zero lacked. A probe project with a cross-module `private function` is the
end-to-end control; it must appear in the audit, and it does.

**Two corrections to the model, both found by that control:**

1. **Top-level declarations are PUBLIC BY DEFAULT** (`parser.cryo:405`,
   deliberate and commented); you opt out with `private`. §3.1 says the default
   is module-private. The spec and the parser disagree, and the first probe —
   written to the spec — silently tested nothing. **§9 Q7** records the
   decision this needs. Nothing in §8.1's numbers depends on it.

2. **A bare free-function call never reaches `resolve_qualified_scoped`.** It is
   bound by `try_pin_overload_mangled_callee` (`sema/call_resolver.cryo`), a
   separate subsystem with its own scope model (`bare_candidate_scope`:
   same-module / Imported / Prelude / Hidden). So the §8.1 gate — the entire
   subject of §8.1 and §8.1b — **cannot see function calls at all**, and
   `RC_VIS_REJECT_NOTPUBLIC` will stay 0 no matter how well visibility is
   recorded. The probe's private function compiles, links, and *runs*
   cross-module.

**The free-function binder has its own copy of the §1 root cause**, plus one
defect the type path does not have:

- **D-A** — the single-overload branch pins its one candidate *without
  consulting the tier at all*, the exact shape of `cands.length == 1` in
  `decl_index`. A function whose module the use site never imported binds.
- **D-B** — `bare_candidate_scope` has no visibility dimension, so an imported
  module's `private` function classifies as `Imported` and binds.

Measured (compiler building itself, `CRYO_RESOLVE_COUNTER=1`):

| | events | distinct pairs |
|---|---:|---:|
| single-overload pins (no scope check) | 2,905 | — |
| **D-A: owner not in scope** | **536** | **25** |
| **D-B: bound to a private candidate** | **0** | 0 |
| bare return-type answers, no scope input | 40 | — |

D-B is now an *honest* zero: the positive control fires (1 event) on a probe
that exercises it. The compiler and stdlib simply never call a cross-module
`private` function by bare name.

D-A's 536 events collapse to **6 distinct callees**, in two classes:

| callee | events | pairs | class |
|---|---:|---:|---|
| `std::fmt::display::fmt_err` | 324 | 1 | compiler-synthesized |
| `std::fmt::interp::fmt_append_lit` | 114 | 9 | compiler-synthesized |
| `std::fmt::interp::fmt_new` | 65 | 9 | compiler-synthesized |
| `Utils::Logger::cdebug` | 31 | 4 | **missing import** |
| `Utils::Logger::set_compiler_debug` | 1 | 1 | **missing import** |
| `Compiler::Diag::EditDistance::find_best_candidate` | 1 | 1 | **missing import** |

The first class is not user churn: `stdlib/core/result.cryo` contains no
textual `fmt_err` call, and the f-string helpers are injected by lowering into
modules that never imported them. It is the same mechanism as §8.1b's class 2 —
a call synthesized into a foreign scope — and it is fixed by §5.2/§6.3, not by
adding imports.

The second class is 6 import lines. **It was corroborated independently**: of
the 26 files calling `cdebug`, 21 import `Utils::Logger`, 4 do not — exactly the
4 flagged — and the 5th non-importer is `logger.cryo` itself, which the audit
correctly excludes as same-module. No false positives, no false negatives.

**The 6 imports are applied, and the delta confirms the model.** Re-measured
after adding them:

| | before | after |
|---|---:|---:|
| D-A events | 536 | **503** |
| D-A distinct pairs | 25 | **19** |
| genuine-missing-import pairs | 6 | **0** |

−33 events is exactly the count those 6 pairs carried, and every remaining pair
is compiler-synthesized. **The entire user-code migration for D-A was 6 import
lines.** What is left is one lowering defect, not corpus churn.

**Two doors, and the count is a floor.** Free-function binding is not one
function: `try_pin_overload_mangled_callee` pins the *symbol*, while
`resolve_direct_call` supplies the *return type* by falling back to the
program-wide bare `func_returns` map — which has no owner column, hence no
scope input of any kind. Both are now instrumented (the second answers 40
times, contributing no violations). ~~They are still not provably the whole
surface: 8 modules call `find_best_candidate` bare and none import
`Compiler::Diag::EditDistance`, yet only one is flagged, so at least one more
path types such a call.~~ **That inference is withdrawn — see §8.1d.** Treat 503
the way §8.3 treats B2 — a floor with a named instrument, not a total — but the
one piece of positive evidence for a third door turned out to be a measurement
artifact, not a finding.

**Consequence for sequencing.** §8.1b's revised order is still right about the
cursor coming first, but its step 4 ("turn the gate on") is under-specified:
there are **two** gates, in two subsystems, and turning on the `decl_index` one
does nothing for function calls. ~~D-A is the cheaper of the two — its migration
is 6 import lines plus a lowering fix already on the roadmap — and unlike the
type path it does **not** depend on the ambient cursor, because
`bare_candidate_scope` already resolves against `current_module_ns()` at the
call site and the measured attribution is clean.~~ **Both halves of that last
sentence are wrong; §8.1d replaces them.** D-A's attribution is *not* clean —
64% of its remaining events are ambient-cursor false positives — and its real
residue is not import lines at all.

### 8.1d D-A, fully attributed (2026-08-04) — 64% is the cursor, the rest is one lowering defect

§8.1c left D-A at "503 events, 19 pairs, all compiler-synthesized" and drew two
conclusions from it. Both were wrong, and the residue is smaller and differently
shaped than they implied. Each result below is *entailed* by the source plus the
audit's own early-exit, not inferred from a count.

**1. The third-door evidence does not exist.** §8.1c reasoned that "8 modules
call `find_best_candidate` bare and none import `Compiler::Diag::EditDistance`,
yet only one is flagged" ⇒ some further path must type such a call. Re-checked
against the tree: there are **6 files with 7 bare calls**, and **all 6 import
`EditDistance`** — five through *multi-line* brace lists
(`import Compiler::Diag::{\n  …, EditDistance\n};`) and one through the plain
import added in `49f5e6eb`. The original check was a single-line regex, which
cannot see a brace list that wraps. The audit had **no false negatives**: the one
file it flagged was the one file genuinely missing the import.

> **Method landmine, and the second time this exact one has bitten.** §8.1's
> class 1 (772 pairs, 68%) was also a brace-list blind spot in a textual import
> classifier. Any regex over `import` lines must flatten whitespace first.
> Prefer the compiler's own `ns_imports` (as §8.2c does) over a regex.

**2. `fmt_err` — 324 of the 503 events (64%) — is not a violation.** It is
`private function fmt_err` in `stdlib/fmt/display.cryo` (`namespace
std::fmt::display`), and the only references to that name anywhere in the tree
are its definition and its 8 call sites **in that same file**, inside
`implement<T, E> trait Display for enum Result<T, E>`. A same-module call is
legal under §4 whatever its visibility — and `audit_fnbind_candidate` agrees: it
returns early on `c_ns.id == use_ns.id`. So the event can only have fired with a
`use_ns` that is not the writing module, and the audit indeed blames
`std::core::result` — the module that owns `Result`, i.e. the one the cursor is
parked on while that generic impl body is specialized.

**This is the ambient cursor (§2b/§5.2), inside the binder §8.1c said the cursor
did not affect.** `audit_fnbind_candidate` and `bare_candidate_scope` both take
their use site from `this.ctx.current_module_ns()`, which is
`CompilationContext.namespace_str` — the module *currently being processed*. The
free-function lane has the same defect as the type lane, and its measured
attribution is no cleaner.

**3. The real D-A residue is 179 events from one mechanism.** The remaining
`fmt_append_lit` (114) and `fmt_new` (65) are genuine out-of-scope binds, and
their cause is exact rather than statistical:

- the parser desugars every `f"…"` into bare `fmt_new` / `fmt_append_lit` /
  `fmt_append_*` calls (`parser/expr_parser.cryo:813`, `:963`, via `fstr_call0`
  / `fstr_call2`, each of which builds a plain `IdentifierNode`);
- `AutoImport` injects `import std::fmt::interp` for any module that used one —
  **except** when the module's namespace starts with `std::`
  (`passes/pass_registry.cryo:1002`), or under `--no-std`;
- **exactly 9 stdlib files contain an f-string, and none imports
  `std::fmt::interp`.** That is a one-to-one match with the 9 distinct pairs
  measured for each helper.

**The stdlib skip cannot simply be deleted.** `std::core::primitives` is one of
the 9, and `std::fmt::interp` transitively depends on core; the skip's comment
("prevents circular imports when compiling the prelude or any module the prelude
re-exports") is load-bearing. So the fix is not an import, and it is not an
allowlist — it is **§7.2 mechanism 4**: the synthesizer knows exactly which
function it is referring to, so it must construct the node already resolved,
rather than emitting a bare name for a later stage to re-bind by string. Today
that means the parser emitting the rooted path it means; after §6.3 it means
stamping `Res` directly.

**Revised reading of D-A:**

| class | events | disposition |
|---|---:|---|
| `fmt_err` — same-module, cursor artifact | 324 | not a violation; retired by §5.2 |
| f-string helpers synthesized into stdlib | 179 | real; retired by §7.2 mechanism 4 |
| genuinely missing imports | 0 | fixed in `49f5e6eb` |

So D-A's migration is **zero import lines** beyond the six already applied, and
turning the call-lane gate on requires the *same* keystone as the type lane —
not, as §8.1c claimed, a cheaper independent path.

### 8.1e The visibility gate was BUILT and REVERTED (2026-08-04) — it is blocked on the keystone, and the counter cannot see why

> **SUPERSEDED on its conclusion by §8.1f.** The diagnosis below is correct and
> still worth reading — the ambient cursor *was* the cause. Its closing claim is
> not: the gate needed provenance for one question, not the whole keystone, and
> it landed on 2026-08-05. Do not read "blocked" here as current.

§8.1d attributed D-A to the ambient cursor. D-B — "bound to a PRIVATE
candidate" — looked separable: it measures **0** on a compiler build, and
rejecting a `private` declaration needs only visibility data the index already
holds, not a scope. That argument is wrong, and the way it fails is worth
recording because the counter actively points the other way.

**What was built.** Every door to a cross-module `private` function call was
shut, and there turned out to be **three**, not the one §8.1c describes:

1. a **written qualified path** (`Vault::secret()`) resolves straight to a
   symbol in `resolve_module_qualified_function` and never goes near the scope
   classifier — it was not even audited;
2. the bare binder's **single-overload fast path** (`call_resolver.cryo:2210`,
   "AUDIT ONLY — behavior deliberately unchanged") pins the sole candidate
   without consulting `bare_candidate_scope` at all. This is the free-function
   twin of `decl_index`'s single-candidate fast path — the *same* root cause as
   §8.1, in the other lane;
3. the **multi-candidate bare path**, via `bare_candidate_scope`, which had no
   visibility dimension.

Shutting any two leaves the third open, which is why the first attempt appeared
to work: the qualified door was closed, the compiler self-built, and a bare call
to the same private function still compiled and ran.

**Why it was reverted.** With all three shut the compiler *and stdlib* still
self-built clean — and the test tree failed with 8 errors, all one symbol:

```
error[E0353]: `std::fmt::display::fmt_err` is private and cannot be called from this module
```

`fmt_err` is declared `private` in `stdlib/fmt/display.cryo` and **every caller
is in that same file** (`:610`–`:656`); no cross-module caller exists. The gate
rejected calls that never leave their own module, reporting the use site as
`std::core::result`.

The cause is not the visibility data. Every gate needs a "does the use site live
in the declaring module?" exemption, and that question is answered by
`current_module_ns()` — **the ambient cursor** (§2b). The f-string lowering
synthesizes those `fmt_err` calls and they are checked with the cursor parked in
another module, so the exemption misfires. Visibility enforcement needs the same
explicit scope the type lane does. §8.1d already said this; this is the
measurement behind it.

**The part that should change how the counter is read.** `FnBindPrivateBound == 0`
reads like evidence the gate is safe to switch on. It is not, and it cannot be:
it counts *bindings that would be rejected*, and the failure mode here is
**false positives** — correct same-module calls rejected because the cursor lied.
A counter over the wrong population is not a weak signal, it is an inverted one.
The same caution applies to D-A's 503: that number bounds what enforcement would
*catch*, never what it would *break*.

Sequencing consequence: the visibility gate is **not** available before the
keystone, in either lane. §7's step 6 ("turn on BOTH gates") stays after step 4,
and no part of it can be pulled forward.

The sibling defect attempted in the same session — a path silently dropping
TRAILING module segments — **was** fixable independently and landed; see §8.2h.

### 8.1f The visibility gate LANDED (2026-08-05) — provenance was the whole missing input

§8.1e concluded "the visibility gate is **not** available before the keystone,
in either lane". That was right about the cause and wrong about the size of the
prerequisite: the gate needed *one* question answered correctly — **which module
wrote this call** — not the whole keystone. §8.2k had already built the machinery
(`module_ns_of_file`); it had simply never been pointed at the exemption.

**The hypothesis, and the number that settled it.** The exemption asked
`current_module_ns()`. Point it at the call's own span instead and every §8.1e
false positive should vanish, while genuine violations survive. Measured on
`examples/09-json-config`, a fixed external target (§10):

| | before | after |
|---|---:|---:|
| D-A "owner NOT in scope" | 204 | **0** |
| use site from the syntax's own module | — | 254 (all of them) |
| **no provenance: fell back to the cursor** | — | **0** |
| syntax ≠ cursor | — | **204** |

All 204 D-A events on this target were `std::fmt::display::fmt_err` blamed on
`std::core::result` — the §8.1e false positive, and *nothing else*. The 204 that
changed verdict are exactly the 204 where syntax and cursor disagree. Provenance
was available for **every** decision, so the gate never has to fall back to a
guess. The change was verified behaviour-neutral by building that target with the
old and new compilers into separate trees: 63 files, byte-identical, including
the linked binary.

**What was built.**

- `CompilationContext::module_ns_sym_of_file` — §8.2k's file→module map as an
  interned id, for callers that *compare* namespaces rather than print them. It
  returns the INVALID symbol when no module claims the file, which is
  deliberately distinguishable from "the module whose namespace is empty": a
  classifier that conflated them would read *unknown* use site as *different*
  use site and reject everything.
- `CallResolver::call_use_site_ns(span)` — the use site for a call, from the
  syntax, with a counted fallback to the cursor.
- `enforce_callee_visibility` — the gate, shared by every door.

**Four doors, not three.** §8.1e named three; the return-type lane
(`resolve_direct_call`'s fallback to the owner-less bare `func_returns` map,
already instrumented as `FnBindUnscopedRet`) is a fourth and is now gated too.
Each rejection records **which door caught it**, because shutting three of four
is indistinguishable from shutting all four unless each is named — that
indistinguishability is precisely why the first attempt looked like it worked.

**Enforcement is by DIAGNOSTIC, not by candidate selection.** A private
candidate still binds exactly as it did; the compile fails with E0353. Removing
it from overload selection would silently rebind the call to a *different*
function, and §4.1 says a wrong bind is a miscompile that every gate in this repo
stays green through. An error is the honest outcome; a silent rebind is not.

**D-A is still NOT enforced, and that is deliberate.** The "owner not in scope"
dimension stays audit-only: §8.1d's 179 f-string-helper events are real
out-of-scope binds that the stdlib depends on, because `AutoImport` cannot inject
`std::fmt::interp` into a module the prelude re-exports. That is retired by §7.2
mechanism 4, not by this gate. **Visibility and scope were separable after all —
just not in the direction §8.1c guessed.**

**The corpus, and what writing it caught.**
`tests/tests/projects/visibility_gate` is a `compile_fail` project with one
private callee *per door* (`latch` qualified, `secret` bare-unique, `stash`
bare-plural) so that one firing door cannot satisfy the whole assertion, plus a
public control asserted ABSENT from the output via `expect.output_excludes`.

Writing it produced a result worth keeping. The door-3 case depends on a second
module declaring the same leaf — and **that module was never compiled**. Module
discovery is import-driven, so a file sitting in `src/` that nothing imports
contributes no declarations, the leaf was never plural, and the call was quietly
caught by door 2. The project passed. Only the per-rejection `VIS-GATE` audit
line showed it, and only because that line is emitted **at the rejection** rather
than tallied for the end-of-run report — which never prints on a failing build
(§11 landmine 8), i.e. never prints for a `compile_fail` entry. A corpus entry
that cannot report which mechanism it exercised is the same class of instrument
as §8.2g's vacuous zero.

`resolution_tripwire` was flipped exactly as its own protocol directs: the
private-call case moved out as an error, the still-open §5.1 half stayed, and the
public control stayed there too — it has to keep *running*, and a `compile_fail`
project can only assert that a diagnostic is absent.

### 8.2 Fallback inventory

Measured hit counts, compiler building itself, 2026-08-03:

| Fallback | Calls | Answers | Note |
|---|---:|---:|---|
| Global leaf index (cascade step 5) | 62,478 | **17,326** | 18% of all `resolve_named` answers |
| home-module preference (2c) | — | **26,473** | largest answering step; §5.2 deletes it |
| bootstrap arena (4, 4a) | — | 12,603 | deleted with `bootstrap_mode` |
| M1 `ns_written_as` (check + export scan) | 49,718 | 49,586 | 99.7% hit, ~0 of it information — §8.2ai |
| M2 `resolve_module_qualified_symbol` | 28,748 | 22,516 | scan **deleted** — §8.2ah |
| M3 `collect_namespace_suffix_matches` | 5,669 | **0** | dead |
| M4 mono bare-name scan | 3,500 | **1** | effectively dead |
| M5 import suffix fallback | 456 | **456** | 100% — a missing capability, not a heuristic |
| cascade 3a / 3c (DI literal / bare) | — | 1 / **0** | dead |
| cascade 2b (E0203) | — | **0** | never fires |

### 8.2a The leaf index, attributed (2026-08-03) — one root cause under everything

Per-caller instrumentation of `lookup_by_leaf`'s six call sites, plus a
`CRYO_LEAF_AUDIT` log of every name it answered:

| Caller | Hits |
|---|---:|
| `resolve_named` step 5 | 17,084 |
| `sema/type_utils` leaf fallback | 6,438 |
| `type_resolution` base class | 17 |
| `resolve_named` generic bound | **0** |
| `symbolic_checker` generic bound | **0** |
| `type_resolution` generic bound | **0** |

**Three of six sites are dead** — every generic-bound lookup. Deletable with a
corpus entry pinning it.

The 17,084 collapse to **199 distinct (module, name) pairs / 165 names**:

| Name | Events | |
|---|---:|---|
| `GlobalAlloc` | 13,687 | **80% of the total** |
| `AllocError` | 898 | |
| `Formatter` | 825 | |
| `Option` | 461 | |
| `FmtError` | 258 | |
| … | | |
| **`ASTVisitor`** | **69** | **0.4% — the case §2.10a cites to justify the whole fallback** |

**14,588 of 17,084 (85%) were resolved with an EMPTY home module**, and
**39 of 57 `ResolutionContext::new(...)` sites pass `""`** as the home module.

**This is the single root cause under all three of today's measurements.** The
resolver is routinely called with no use-site scope. With no scope it cannot
answer from imports, so it falls the length of the cascade and lands on a
program-wide index — which is the only thing that *can* answer a question asked
without a scope. The same defect appears as:

- **17,084 leaf-index answers** — resolution with no scope at all (§8.2a)
- **327 visibility violations** — template bodies resolved in the *consumer's*
  scope rather than the definer's (§8.1b)
- **26,473 `home-module preference` answers** — a step that exists solely to
  correct the ambient cursor (§8.2)

**Consequence for D4 and Phase 2.** Legalizing intra-package import cycles
addresses `ASTVisitor` and its peers: **0.4%** of leaf-index usage. It does not
retire the fallback. What retires it is making the use-site scope mandatory and
explicit (§5.2) and stamping `Res` on nodes so synthesized and instantiated
bodies carry their resolution instead of being re-resolved scope-less (§6.3,
§7.2 mechanism 4). Those two changes are the keystone; nearly everything else
in §8.2 follows from them.

### 8.2b First scope fix landed (2026-08-03) — 87% of the leaf index retired

Two changes, both applying §5.2 ("resolve where it was written"):

1. **Default generic annotations resolve in the template's module.** A default
   (`struct String<A = GlobalAlloc>`) is syntax written in the template's
   module, so `expand_default_type_args` now sets `home_module` from
   `entry.module_name` on the context it already clones. Set in the shared
   layer rather than at the nine call sites: the owning module is a property of
   `entry`, so it is correct by construction and a new caller cannot forget it.
2. **Home-module resolution became import-aware.** Step 2c previously consulted
   only what the home module *declares* (`home_module::Name`). But a bare leaf
   in an annotation means what that module can *see* — its imports and the
   prelude. `GlobalAlloc` is declared in `std::alloc::allocator` and merely
   imported by `std::collections::string`, so the declaration-only check missed
   every time. It now falls through to `resolve_qualified_scoped_or` against
   the home namespace, which is import-scoped and therefore cannot bind to an
   unrelated same-leaf type in a module the home module never imported.

Measured effect on a full compiler build:

| | before | after |
|---|---:|---:|
| `resolve_named` step 5 (leaf index) | 17,084 | **2,280** |
| `lookup_by_leaf` hits, all callers | 23,539 | **8,735** |
| `lookup_by_leaf` calls | 62,222 | 47,424 |
| step 2c (import-scoped home) | 26,473 | 48,764 |

Change 1 alone moved nothing (the declaration-only check still missed);
change 2 alone moved 2,503. Together they moved 14,804. **The two are only
useful in combination**, which is worth remembering before either is
"simplified" later.

This is the empirical confirmation of §8.2a: the global leaf index was
load-bearing because the resolver was being called without a scope, not
because of circular imports.

Note the visibility-gate reading rose (21,296 → 35,672) because more traffic
now flows through `resolve_qualified_scoped`'s unguarded single-candidate fast
path (§1). That is not a regression — those resolutions previously used a
program-wide first-writer-wins index with no scope at all — but it does mean
§1's fast path is now the dominant remaining source of scope-less binding.

Two readings matter for sequencing:

- **The leaf index answers far more than its documented justification.** The
  roadmap (§2.10a) justifies it as the only route for `ASTVisitor*` in AST node
  files. That cannot account for 17,326. Making intra-package import cycles
  legal will therefore **not**, on its own, retire it. A per-callsite
  breakdown of those 17,326 is required before that phase is scoped.
- **M5 hits 456 of 456.** It never fails, which means imports genuinely cannot
  express what the source needs today. It is a missing *capability*
  (per-item aliasing, re-export), and the capability must ship before the
  fallback is removed — otherwise it gets re-added under pressure.
  **Superseded by §8.2c**, which resolved those 456 and found they are not a
  missing capability: every one is an import path with `std::` dropped.

### 8.2c Rule-6 path churn, measured (2026-08-03) — the migration is ~1 edit

§5.1 ("first segment resolves in scope, the remainder is rooted") is what
deletes the five substring matchers, and its cost was the last unmeasured
number in Phase 0: every partially-qualified path whose prefix is not genuinely
in scope becomes an error needing an import.

**Method.** M1, M2 and M5 emit one `PATH-HIT` line per *accepted path* under
`CRYO_PATH_AUDIT=1`, plus the compiler's own module→visible-namespace table
(`PATH-SCOPE`) and prelude set (`PATH-PRELUDE`). Emitting at the decision, not
inside the matcher, is essential: M1's 49,607 raw hits are probes inside a
per-candidate loop, so that number cannot answer "how many written paths need
an import" — only one event per accepted path can. Reachability is then the
compiler's own (`ns_imports` + prelude + own namespace), not a regex over
`import` lines, which cannot see brace-list items or the prelude.

The rule-6 walk is forced, which makes the classification exact rather than
heuristic. For written `w0::…::wn::leaf` binding to canonical `r0::…::rm::leaf`,
`w0` must bind to a module M and `w1..wn` walk down from it; since the answer is
`r`, that succeeds only when `w` is a `::`-suffix of `r`, and M is then exactly
`r[:m-n]`. So there is one question per event: **is that ancestor in scope at
the use site?**

72,242 use-site path events (1,553 distinct) and 456 import-path events:

| class | events | distinct | disposition |
|---|---:|---:|---|
| ancestor already in scope | 69,757 | 1,403 | legal as written |
| fully-rooted path, root not an import edge | 2,190 | 79 | zero churn (see below) |
| ambient-cursor artifacts | 267 | 67 | not real (see below) |
| **abbreviation, ancestor not in scope** | **28** | **4** | **the whole use-site migration** |
| **UNWALKABLE — needs rewrite or re-export** | **0** | **0** | **none exists** |
| abbreviated import paths | 456 | 58 | mechanical, one rule |

Four results, in descending order of consequence:

1. **Nothing in the tree needs a re-export. `public import` is not a
   prerequisite.** Zero paths have a PREFIX or INTERIOR shape — no source
   anywhere writes `Lib::Widget` for a `Lib::Helper::Widget`. That hazard is
   real in the *language* (§8.2a documents how the fallbacks cover for it) but
   the corpus never exercises it. This **contradicts the sequencing claim that
   D1 must land before the fallbacks are deleted**: on this corpus the
   fallbacks can be deleted first, and D1 stands on its own merits.
2. **The use-site migration is one module.** `std::sys` writes
   `syscall::sys_callN` while importing only `std::ffi::libc`; the four
   distinct paths are one missing `import std::sys::syscall;`. Because
   `std::sys::syscall` is a *submodule* of `std::sys`, even this reduces to a
   single design decision: **does a parent module implicitly bind its
   submodules?** If yes, use-site churn is **zero**.
3. **The 456 M5 hits are one mechanical rule, not a missing capability.** Every
   one is an import path with `std::` dropped (`import core::option;` for
   `std::core::option`) — 58 distinct modules, no other pattern. Under the Q5
   decision below these are the migration: `std` being addressable makes
   `std::core::option` legal, but `core::option` still is not, because `core`
   is not itself a root and nothing binds it. So all 456 get `std::` prefixed.
   This supersedes §8.2b's reading of M5 as evidence for a capability gap.
   **456 is the compiler-build-visible subset; the whole-tree count is 1,011 —
   see the correction under §9's "Net rule-6 migration".**
4. **The 79 fully-rooted paths are zero churn.** They are spellings like
   `std::fs::path::Path` and `Utils::Logger::Logger` whose required ancestor is
   just the package root. No design makes a fully-rooted path illegal; Rust
   keeps crate roots in the extern prelude for exactly this reason.

Two defects surfaced from the audit rather than from the churn question:

- **Dependency edges are recorded under the string the source wrote.** `import
  alloc::allocator;` lands in the module graph as `alloc::allocator`, so
  `ns_imports("std::core::primitives", "std::alloc::allocator")` answers *not
  imported* for a module that plainly imports it, while the sibling `import
  core::intrinsics;` in the same file is recorded resolved. Six edge targets are
  affected. This is the same class as the brace-list bug in §8.1b — the visible
  set under-reports — and it inflates both this measurement and the
  visibility-gate blast radius. Repairing it in the classifier is what removed
  `std::core::primitives`' `allocator::alloc` from the churn list.
- **M5's second copy is dead.** `name_resolution.cryo`'s submodule suffix
  fallback (the `M5-SUBMOD` site) records **0** hits across a full build; all
  456 come from the first copy. It joins M3 and M4 in §8.2's deletable set.

**Caveat on attribution.** The use-site of an M1 event is
`Resolver.current_module`, the ambient cursor §5.2 exists to remove — so it
names the module being *processed*, not the one that wrote the path. 267 events
(67 distinct) prove it directly: they report a stdlib use site for a
`Compiler::`/`CLI::` path, and stdlib cannot depend on the compiler. They are
excluded above. This does not weaken results 1, 3 or 4, which do not depend on
the use site at all; it means result 2 is an *upper* bound on use-site churn.

### 8.2d Cycle imports lost their namespace (2026-08-03) — 59% of the gate

Found while auditing §8.2c, not while looking for it. **The visibility-gate
blast radius fell 35,672 → 14,444 (-59.5%) with no change to what the compiler
resolves.**

`discover_module` adds a module to the graph at the *end* of its own
discovery, but returns early when the target is already on the loading stack —
which is the normal case inside an import cycle, and the early return says so
("the topological sort handles actual ordering"). The caller, however, needs
the target's namespace *immediately*, to record the dependency edge:

```
discover_module(resolved)                  // returns true; graph entry not written yet
const dep_idx = graph.find_module_by_path(resolved)   // -> -1
… fallback: add_dependency(intern(import_path))       // the WRITTEN path
```

So an import that closes a cycle recorded its edge under the string the source
wrote. `std::collections::string` imports `alloc::allocator`, and the edge went
in as `alloc::allocator` — while `core::intrinsics` **in the same file**, not in
a cycle, went in resolved as `std::core::intrinsics`. `ns_imports` then answered
*not imported* forever after for a module that plainly imports it. Six edge
targets, and 21,168 of the 35,672 gate rejections.

**Fix.** Publish each module's namespace into a `path -> namespace` map as soon
as it is scanned — before recursing into its imports, so the recursion can see
it — and consult that map when the graph lookup misses.

**The edge must be a VISIBILITY edge, not a dependency edge.** Recording it as
a dependency first, which is the obvious reading of the code being fixed, put
every module in the program into one E0501 cycle. That is not a bug in the fix;
it is the fix telling the truth. Reaching this branch *means* the import closes
a cycle, so it cannot also be an ordering constraint. The raw-path edges were
tolerable only because they were **inert** — they matched no module name, so
`compute_order()` never saw them while `ns_imports` never matched them either.
Resolving them makes them real, and they must land on the side that carries
visibility. This is §8.1b's rule (`visibility edges ≠ dependency edges`)
arriving a second time, by a different road.

Two readings worth keeping:

- **A silently-degraded edge is worse than a missing one.** It looked like a
  recorded dependency and answered every visibility question wrongly, for
  years, with no diagnostic. The `should not happen` comment on the fallback
  was load-bearing documentation that was simply false.
- **The remaining 14,444 are all `namespace not reachable`; `candidate not
  public` is still exactly 0.** Visibility is recorded for types
  (`set_type_visibility`, 5 sites) but nothing records it for *functions*, so
  `is_candidate_public` cannot reject one. The gate's blast radius is entirely
  a reachability question today, and §8.1's "is the migration mechanical?"
  cannot be answered for functions until function visibility is recorded at
  all.

### 8.2f Rule-6 migration LANDED (2026-08-04) — M5 retired, and what it exposed

The migration §9 determined is applied: **1,011 plain `import <path>;` statements
re-rooted with `std::`** across 126 files (1,007 stdlib, 4 `compiler/src`, 0
runtime), plus 6 in `tests/` and the one `import std::sys::syscall;` that Q6
requires in `stdlib/sys/_module.cryo`. `legacy/` (159) is dead and
`.claude/worktrees/` (625) is not built; neither is compiled.

**Result — M5 is dead, by its own counter:**

| | before | after |
|---|---:|---:|
| M5 import suffix fallback **calls** | 456 | **0** |
| M5 import suffix fallback **hits** | 456 | **0** |
| B1 total | 153,804 | 153,408 |

Zero *calls*, not merely zero hits: no import path in the tree now needs a
suffix search. Every other counter is unchanged within run-to-run drift
(fast path 310,656; would-be-rejected 14,444; D-A 503), which is what a
behaviour-neutral migration should look like. `make test` green (unit, 168
compile-fail, 15 projects).

**Finding 1 — the abbreviations were masking a real import cycle, and the
stale pin turned that into a bootstrap trap.** With the migration applied, the
*pinned* compiler (then 3 commits behind, at `259bb212`) failed to build stdlib:
`[TopSort] Cycle detected! 42 of 154 modules placed`, rooted at
`core/option.cryo` ↔ `core/result.cryo`, and — landmine 5 again — it printed
`Project compilation failed (1 errors)` with **no diagnostic**. The same tree
built clean with the HEAD-built compiler.

The mechanism is §8.2d's, a third time. `option` and `result` genuinely import
each other. Before `fd1f1750`, a cycle-closing import degraded its edge to the
*written* path; `core::result` matched no module name, so `compute_order()`
silently skipped it (`find_module_index(...) >= 0` guard) and the cycle was
invisible. Spelling it `std::core::result` makes that degraded edge **match**,
so the cycle becomes a hard E0501. The old behaviour was not correct — it was
an accident of the abbreviation.

> **Two lessons.** (a) A silently-degraded edge is worse than a missing one —
> stated in §8.2d, and it has now cost time three times; the remaining
> `EDGE-FALLBACK` branch is the last place it can still happen, and it is
> instrumented. (b) **Repin before measuring anything.** A pin that predates the
> fix under test makes a correct change look like a regression, and the failure
> it produces points at the wrong subsystem entirely. §7's ordering (repin
> first) is load-bearing, not hygiene.

**Finding 2 — deleting M5 needs a diagnostic first; it is not pure dead-code
removal.** M5 is dead, but the naive deletion produces *bad errors*, because two
different subsystems consume an import path:

- the **loader** resolves `core::option` on the filesystem to `./core/option.cryo`
  and records the dependency edge under the file's real namespace. It does not
  need M5 and does not care about the spelling.
- **`process_import`** (`resolver/name_resolution.cryo`) needs the written path
  to name a *graph module*, so it can pull that module's export set. That is the
  only consumer M5 serves.

Delete M5 and a stale abbreviation no longer errors at the import. It silently
binds **nothing** — `find_module_index` misses, `get_exports` returns empty — and
the user gets a scatter of downstream "undefined type" errors pointing at use
sites, with nothing pointing at the import that caused them. That is strictly
worse than today.

So M5's removal is paired work: `process_import` must emit an E0500-family
diagnostic *at the import* when the written path names no module, with the
`suggest_module`-style "did you mean `std::core::option`?" note. The corpus
entry that pins the deletion (§7.2 mechanism 2) is a negative test asserting
exactly that diagnostic — and it cannot be written until the diagnostic exists.
`tests/tests/negative/E0500_module_not_found.cryo` is the shape to follow.

### 8.2e The prelude is derived, not transcribed (2026-08-03)

`seed_prelude_namespaces` was a hardcoded list of 12 namespaces whose own doc
comment said **"KEEP IN SYNC if the prelude's `public module` set changes"** —
a constant that had to be hand-maintained against a source file. §8.1b already
noted it mirrors `stdlib/prelude.cryo` exactly; that is precisely what makes it
a transcription rather than a design.

The comment justified it: prelude entries "arrive as `public module`
SUBMODULES of the prelude, which the loader does NOT record as dependency
edges, so they can't be derived from the graph." The first half is true and
must stay true — a `public module` dependency edge recreates the false cycles
`compute_order()` avoids (§8.1b, and again in §8.2d). The conclusion no longer
follows. The loader **does** discover those submodules; it simply discarded the
result:

```
if (sub_resolved.length() > 0) {
    if (!this.discover_module(...)) { success = false; }
    // NO add_dependency here; submodules are not real deps
}
```

The namespace is in hand at that line and was dropped on the floor.

**Change.** `ModuleInfo.submodules` records each `public module` target's
resolved namespace, and `instance.cryo` derives the prelude set by reading
`std::prelude`'s entry off the graph. `stdlib/prelude.cryo` becomes the single
source of truth. Derived set verified **set-equal** to the 12 it replaces, so
behavior is preserved by construction.

**`submodules` is deliberately neither a dependency edge nor a visibility
edge.** Recording it grants nothing and changes no resolution input. Making
`public module` also feed `ns_imports` — i.e. treating it as a real re-export —
is the `public import` / D1 question, and §8.2c showed the fallbacks are
currently covering for that gap. It should be decided and measured on its own
terms, not acquired as a side effect of deleting a constant. The data is now
recorded, so that measurement is available when the decision is taken.

**Latent bug fixed for free.** The old seeding ran *unconditionally*, so a
`--no-std` build registered 12 standard-library namespaces as "prelude" when no
prelude was loaded. Derivation makes the set empty there. Verified on a probe
project: 12 namespaces on a normal build, **0** under `--no-std`.

### 8.2g This corpus CANNOT exercise the keystone (2026-08-04)

The §5.2 keystone — explicit scope replacing the ambient cursor — was measured
directly, and the result changes its sequencing: **the tree cannot tell a
correct implementation from a broken one.**

The instrument (`CRYO_SCOPE_PROBE`) runs at step 2c, the one place where the
true scope is known because a caller set `home_module` explicitly. There the
module-blind chain below 2c (3b canonical, 3c bare, 5 leaf index) is re-run and
compared against the scoped answer. Full compiler build, serial codegen:

| | events |
|---|---:|
| step 2c hits compared | 48,777 |
| home **==** ambient cursor (proves nothing) | 35,407 |
| **home ≠ ambient cursor** (the evidence) | **13,370** |
| of those, leaf declared by >1 module | **1** |
| both chains agreed | 48,777 |
| blind chain binds a DIFFERENT type | **0** |
| blind chain answers nothing | **0** |

**Read the last three rows together or not at all.** Zero divergence is not
evidence that scope-less resolution is safe. It is a fact about the corpus: in
13,369 of 13,370 wrong-cursor resolutions there was exactly **one** candidate
in the entire program, so no scope — right, wrong, or absent — could have
changed the answer.

Corroborated statically and independently: across `stdlib` and `compiler/src`
there are **666** distinct type leaves and only **8** declared by more than one
module (`Cursor` ×3; `Weak`, `Scope`, `JoinHandle`, `Frame`, `Executor`,
`DiagSink`, `Command` ×2). Several live in `thread`/`process`/async modules a
compiler build never loads, which is why the measured plural count is 1 rather
than 8.

Three consequences, in order of how much they cost if ignored:

1. **The self-host fixed point is not a correctness gate for this change.**
   Making the 49 scope-less `ResolutionContext` constructions explicit will
   produce a bit-identical compiler. `make test` and both self-host halves will
   pass whether the new scopes are right or wrong. Every gate this repo
   currently has would stay green through a completely incorrect
   implementation.
2. **The conformance corpus is a PREREQUISITE for the keystone, not a
   follow-up.** §7 already noted the tree contains zero prefix/interior paths;
   this generalizes that to leaf collisions — the tree has essentially no
   same-leaf ambiguity of any kind. The corpus must supply the collisions the
   corpus-under-test lacks, and it must exist *before* step 2, or step 2 ships
   unverified.
3. **The keystone's justification is architectural, not bug-fixing.** No
   miscompile is being fixed here on today's sources; what is being fixed is
   that a wrong answer is *representable*. That is still worth doing — §7's
   whole premise is that this subsystem regressed for years because nothing
   could see it — but it should be argued on that basis rather than on a defect
   count, and it means the change carries no user-visible payoff to point at.

> **Instrument note.** The probe re-runs production lookups, which normally bump
> the leaf-index and M1 tallies. `rc_suspend`/`rc_resume` in
> `resolve_counter.cryo` suppress recording across the replay. Verified exact:
> with the probe ON and OFF, `lookup_by_leaf calls` (49,528), `M1 calls`
> (49,760) and step 2c (48,777) are identical. Any future instrument that
> replays a lookup must do the same or it will inflate what it measures.

### 8.2h The probe cannot see the keystone's worklist (2026-08-04)

§8.2g established that the corpus has no collisions for the probe to find. A
purpose-built collision shows the constraint is tighter than that, and it
changes what a corpus entry has to prove.

**The probe runs only inside step 2c**, which is reached only when
`ctx.home_module` is non-empty — the 8 of 57 sites that already set a scope.
The 49 scope-less `ResolutionContext::new("")` constructions never reach it.
So the probe measures divergence across exactly the *complement* of the
keystone's worklist. It is not merely starved of collisions; it is structurally
blind to the sites being fixed.

Measured on a purpose-built 4-module program (`Widget` declared by two modules,
both in scope at the use site, the bare return annotation re-resolved from a
third):

| | events |
|---|---:|
| home == ambient cursor | 2,477 |
| home ≠ ambient cursor | 150 |
| **of those, leaf declared by >1 module** | **0** |
| `lookup_qualified_alternatives` ambiguous (>1 candidate), same run | **2** |

The last two rows are the finding. The collision is real and the resolver saw
it twice — but not at step 2c, so `HomeDiffPlural` stayed 0. **A corpus entry
that exercises the keystone will therefore register nothing on this probe**,
and "drive `HomeDiffPlural` above zero" is the wrong acceptance criterion for
one. It measures where a *scoped* site would have diverged, not where a
scope-less site gets it wrong.

**What a corpus entry must use instead**, in preference order:

1. **Runtime-observable divergence.** The existing
   `tests/tests/projects/generic_name_collision` is the template and already
   does this correctly: `Alpha::pick` returns the smaller argument and
   `Beta::pick` the larger, so a wrong bind is a wrong *value*, not merely a
   failed compile. This is checkable today, after the keystone, and without
   any instrument.
2. **A pinned diagnostic** for cases that must become errors.

Independently: that project already covers same-leaf collisions in **both**
lanes (value via `pick`, type via `Widget`/`WidgetOther`), so §7's "the corpus
must supply a leaf declared by two modules" is *partly already satisfied* —
what it lacked was the M1 case below (since fixed) and the cross-module
`private` call (still open — §8.1e).

#### The M1 interior-prefix violation — FIXED 2026-08-04

> **The next three paragraphs describe the behavior BEFORE the fix**, kept
> because the reduction and its control are what identified the cause. The fix
> and its verification follow them.

§5.1 says the first segment resolves in scope and the remainder is **rooted** —
"no substring matching, no suffix matching". `module_ns_matches_prefix`
(`resolver/resolver.cryo:620`) instead accepted the written prefix anywhere it
occurred in the module path as a whole-segment run. Reduced to a 2-module
program:

```
namespace ProbeM1::Lib::Helper;      // the ONLY Widget
type struct Widget { v: i64; ... }

// in ProbeM1::Main:
const w: ProbeM1::Lib::Widget = ProbeM1::Lib::Widget { v: 7 };   // COMPILES, RUNS
```

`ProbeM1::Lib` declares no `Widget`; the path resolves to
`ProbeM1::Lib::Helper::Widget` and the program prints 7. **Control**, required
before reading anything into that: `ProbeM1::Bogus::Widget` correctly fails
with `E0203`, so this is M1's prefix matching specifically and not paths being
ignored wholesale.

This is the concrete instance of §8.2g consequence 3 — a wrong answer that is
*representable* rather than a miscompile on today's sources — and unlike the
scope cases it needs no instrument and no collision to demonstrate.

**Fixed 2026-08-04.** `module_ns_matches_prefix` is now anchored to the END of
the module namespace: the written qualifier must be a whole-segment **suffix**
of the declaring module's path, which is a test rather than a search. The
distinction that makes this safe to do ahead of the keystone:

| direction | example | verdict |
|---|---|---|
| omit **leading** segments | `future` for `std::future` | **kept** — a scope question, and the tolerance this function exists to provide |
| omit **trailing** segments | `Lib` for `Lib::Helper` | **rejected** — a false claim about where a declaration lives |

Only the second was ever wrong, and separating them is what let this land
without the keystone: "which module does this qualifier name?" is answerable
from the qualifier and the candidate alone, with no use-site scope involved.
Contrast §8.1e, where the visibility gate could *not* be separated for exactly
that reason.

Verified: the defect case is now `E0203`; canonical paths, root-omitted paths
(`Lib::Helper::Widget`), and bare leaves via import all still resolve; the
compiler self-builds and both self-host halves stay byte-identical. Pinned at
`tests/tests/negative/E0203_path_drops_trailing_segment.cryo`, which compiled
clean before the fix and errors after — a before/after the negative suite can
hold by itself.

#### The corpus slice that landed (2026-08-04)

Two projects, both value-checked, both control-verified:

**`tests/tests/projects/resolution_scope`** — 5 tests pinning what the keystone
must **not** change. Three modules (`Red`/`Green`/`Blue`) declare the same
`tint` and `Paint` leaves with different values; three consumers each import
exactly one. Three-way rather than two-way deliberately: a fix that prefers one
module program-wide passes a two-way collision one time in two. Covers the
value lane (bare call), the type lane (bare return annotation re-resolved from
a module that binds a different `Paint`), qualified paths with two candidates
in scope, and the decided "an import binds its leaf name" rule.

**`tests/tests/projects/resolution_tripwire`** — the two defects above, pinned
as they behave **today**, with the flip protocol in the file header: when the
keystone or the visibility gate lands, these tests fail, and the fix is to
convert the project to `compile_fail` in the same change — not to adjust the
assertions. Same trust-loop reasoning as `known_fail_canary`. Its control
(`E0203_unknown_module_prefix.cryo` in the negative suite) pins the *correct*
rejection and must stay green throughout.

Verified rather than assumed: an assertion was deliberately broken and the
suite went red, and the parent project runner was confirmed to propagate a
child project's failure (`[FAIL] (project tests failed)`, exit 1) rather than
reporting a false green.

Both defects were reproduced from scratch before being pinned — the
cross-module `private` call compiles *and runs*, printing 99.

### 8.2i The worklist is not 49 decisions — it is one question per context (2026-08-05)

The keystone has been described as "the 49 scope-less `ResolutionContext`
constructions are the worklist". Reading all 57 shows that framing is
misleading in a way that would have produced a wrong change passing every gate.

**A context's home module is decided by where its SYNTAX was written**, not by
which function constructs it. Two contexts built six lines apart in the same
function can belong to different modules. Sorted by that question:

| what the context resolves | home | sites |
|---|---|---|
| `call.generic_args`, `scope.generic_args`, `scope.scope_generic_args` — turbofish | the **caller** | `call_resolver` 1262, 1394, 1457, 1623, 3663, 3851, 3910, 4180 |
| `expand_default_type_args` — the template's declared defaults | the **callee** | `call_resolver` 4243 |
| owner ctx for the callee's own annotations | the **owner type's module** | `method_binding` 1177, 1192 |
| a method found on a trait decl | the **trait's module** | `method_binding` 1045 |

Eight of the twelve sites previously grouped as "callee signature
re-resolution" are **caller-scoped**: they resolve turbofish the caller wrote.
`call_resolver.cryo:1457` already says so in a comment
("Resolve the turbofish in the CALLER's scope"). Setting those to the callee's
module is a miscompile, and §8.2g's measurement says the self-host, `make test`,
`b1-check` and the scope probe would all have stayed green through it.

`method_binding.cryo:1192` was already migrated (line 1249,
`owner_module_of(recv_type)`) with a comment stating the rule exactly. It is the
reference implementation for this shape, not a special case.

**Landed 2026-08-05:** `method_binding.cryo:1177` and `call_resolver.cryo:4243`,
the two sites where the home module was recoverable from data already in hand
(`owner_module_of(recv_type)`; `TemplateEntry.module_name`). Behaviour-neutral
as predicted: corpus 21/21, `make test` 2001/170/17, B1 unchanged at 19,292, and
a counter diff against the previously pinned compiler on `examples/09-json-config`
was **byte-identical across every row**.

**Blocked: `method_binding.cryo:1045`.** It resolves a method located through
`generic_registry.get_trait_decl(leaf)` — a lookup keyed by LEAF NAME — and
`TraitDeclNode` (`AST/declaration.cryo:877`) carries no module or namespace
field, only `name: SymbolStr` and the inherited span. There is no correct home
module to set, so the site cannot be migrated without guessing. This is the same
class of defect as the keystone itself, one level down: the trait registry has
no module identity. Prerequisite work, not a one-liner.

### 8.2j A bare name resolves with NOTHING in scope — reduced (2026-08-05)

§5.1 says the first segment resolves in scope. It does not have to.

Deleting the *only* `import` from a module that names a type in two signatures
produced **no error**: the signatures still resolved, and still to the right
type. Diffing `CRYO_LEAF_AUDIT` between the two variants of the same project
named the mechanism with no inference required — exactly six new hits, all for
the orphaned leaf:

```
3  LEAF-HIT  GenericNameCollision::WidgetStatic  Widget   <- home module SET, fell through to step 5
3  LEAF-HIT  (empty use-site)                    Widget   <- NO scope: the keystone's own sites
```

Nothing else in the counter moved except step 5 (753 → 759) and 2c (5102 →
5099). This is §8.2a's root cause reduced from an aggregate over a full
compiler build to a project small enough to read in one sitting, and it is why
the leaf index cannot simply be deleted: 2,538 answers in the compiler's own
build depend on it, and every one of them is a name that was never in scope.

Pinned as the second tripwire in `resolution_tripwire`
(`orphan.cryo`, `depot.cryo`, two `WRONG_*` tests plus a qualified-path
control), under the same flip protocol as the visibility tripwire.

> **Corpus note.** `find_static_method_template` — the largest single cluster in
> this shape — had **no coverage at all**: there was not one static generic
> method anywhere in `tests/tests/projects`. Added as `widget_static.cryo` /
> `widget_static_test.cryo` in `generic_name_collision`, reusing the existing
> `WidgetDeep`/`WidgetOther` collision. Control-verified: retyping one
> assertion to the colliding `Widget` fails the build with E0200 + E0358, so a
> wrong bind is a hard error rather than a silent pass.

### 8.2k §5.2's premise about 2c is 0.6% true — MEASURED (2026-08-05)

§5.2 says the home-module preference step "exists solely to correct an ambient
cursor pointing at the wrong module. With an explicit scope it has nothing to
correct and is deleted, not fixed." **That premise is now measured, and it is
wrong.**

The step could not previously be measured because the counter could not tell
*where a context's home module came from*. `ResolutionContext.home_module` is a
bare string; a module recovered from a declaration's own record and one copied
off the ambient scope cursor are indistinguishable once stored, so both landed
in one tally that was flagged B1 on the assumption that the second case
dominated.

`HomeOrigin { Unset, Cursor, Syntax }` makes the provenance explicit, and
`set_home_module` now **requires** it as an argument — the origin is not
recoverable from the string, and a site that cannot say which it is has not
established the annotation's module at all. Measured on
`examples/09-json-config`:

| | answers |
|---|---:|
| 2c total | 3,893 |
| **from syntax provenance** (a declaration's recorded module, or a node span) | **3,870** |
| from the ambient cursor | **23** |

⇒ **2c is 99.4% authoritative scoped resolution.** It is already doing what
§5.1 asks — resolving a leaf in the module that wrote it. Deleting it would
delete the correct mechanism and leave the module-blind chain below it as the
only answer. What should be deleted instead is **3b's ambient-cursor
canonicalization and step 5**, the global leaf index.

The classification was inverting the migration's own signal. 3b is flagged B3
("answers from scope/imports rather than by guessing") while consulting the
ambient cursor; 2c was flagged B1 while consulting the syntax's own module. So
teaching a site real provenance moved answers 3b → 2c and made **B1 go up** —
the ratchet reporting progress as regression. With the split, 2c's
provenance-derived arm counts as B3 and the signal points the right way.

Effect on B1, and the two parts must not be conflated:

```
B1  19,292 -> 15,460   (-3,832)
       -35  a REAL reduction: answers that fell through to the global leaf
            index now resolve in scope (the 8 turbofish sites, below)
    -3,797  a MEASUREMENT CORRECTION: answers that were always authoritative,
            previously miscounted as fuzzy
```

Only the 35 is fewer fallbacks. Reporting the 3,832 as "fallbacks removed"
would be false.

> **Design note.** The two-setter alternative — `set_home_module` plus
> `set_home_module_from_syntax` — was rejected. A caller that forgets which one
> to use silently gets the wrong bucket, and the wrong direction is the
> dangerous one: labelling a cursor guess as authoritative launders it into
> B3 and hides exactly what this instrument exists to expose. A required
> argument cannot be forgotten.

#### Where provenance comes from

`CompilationContext::module_ns_of_file(file)` maps a span's file to its module.
This is exact, not a heuristic: a `.cryo` file declares exactly one namespace
(the parser records the first non-directive `ModuleDeclaration` as the file's
`namespace_name` and stops), so file → module is a total function. Verified
across `compiler/src` + `stdlib`: every file declares exactly one namespace, and
no namespace is declared by two files. `ModuleInfo` already paired `file_path`
with `namespace_name` and `ModuleGraph::find_module_by_path` already existed —
this assembles data the compiler already had rather than adding a new index.

Eight caller-scoped turbofish sites in `call_resolver.cryo` now take their home
module from the call node's own span rather than from `current_module_name()`,
which reads the cursor. That is the −35 above.

#### The last 23 resisted a cache — LANDED 2026-08-05 without one

The remaining cursor-derived answers were **free-function bodies**:
`body_res_ctx` reads the owner type's module, which is invalid when there is no
owner, and fell back to `namespace_display()`.

Caching the module at `enter_function` from `func.span.file` **breaks the
build** — `stdlib/core/mem.cryo`'s `transmute<From, To>` types
`mut result: To` as `void` (E0200).

The cause is lifetime, not value, and the instrument is what established that:
`body_ns` and the cursor were **identical in all 26 sampled calls, zero
divergence**. `enter_function` has no matching exit hook and `SemaState` has no
current-function node, so a value cached there goes stale; `body_res_ctx` runs
later, at annotation-resolution time, and reads whichever function was entered
last. `namespace_display()` is *live*, and therefore self-correcting — which is
why the worse mechanism works and the better one does not.

**The fix keeps the liveness and drops the cache.** `body_res_ctx` now takes the
`span` of the syntax it is resolving and derives the home module from
`module_ns_of_file(span.file)` at the point of use. That is the same shape as
`call_use_site_ns` (§8.1f), and it has no lifetime at all: there is no state to
go stale, because nothing is stored. The two callers — `DeclStmtNode` and
`DestructureDeclNode` — each already had the declaration node in hand. A span
that no module claims still reaches the cursor and is still counted and labelled
`Cursor`; on every corpus measured so far that count is **zero**.

| on `examples/09-json-config` | before | after |
|---|---:|---:|
| `2c` home-module (ambient cursor) | 23 | **0** |
| `2c*` home-module (syntax provenance) | 3,870 | **3,893** |
| **B1 total** | 15,460 | **15,437** |

Behaviour-neutral: that target built with the old and new compilers into
separate trees is **63 files byte-identical**, linked binary included. The −23 is
the whole of the cursor's remaining presence at this site, not a sample of it.

> **The zero was controlled, twice.** `BodyNsDiff` — span-derived module ≠
> cursor — reads **0** over 241 free-function contexts in the compiler's own
> build and 70 in `09-json-config`, with `BodyNsCursor` (no provenance at all)
> also 0. A zero that a change is justified by has to be attacked, so
> `body_ns_diff` emits **every** row rather than only the diverging ones, and
> `awk -F'\t' '$3!=$4'` over the stream re-derives the count with a comparison
> this compiler did not perform. It agrees. The rows carry 13 distinct real
> namespaces, so the comparison demonstrably distinguishes values rather than
> being uniformly false. `std::core::mem` is **127 of the 241** — the flip is
> exercised hardest at exactly the site the cached version broke.

This is the shape to reuse for §8, task #3: the failed mechanism and the working
one differ only in *when* the question is asked, not in what is asked or what
data answers it. A cursor read is not always a missing input; sometimes it is a
correct input consulted at the wrong time.

> **The zero is structural at this site, and that was tested rather than
> assumed.** "No divergence on the corpora measured" and "this site cannot
> diverge" are different claims, and §8.2g is the standing warning against
> reading the first as the second. So the adversarial case was *built*:
> `wrap_via_local` in `generic_name_collision`, a free generic function whose
> body-local annotation is a bare `Widget`, called from a module with a second
> `Widget` in scope. The `BODY-NS` stream shows the site is reached, and syntax
> and cursor still agree. `body_res_ctx` runs during the declaring module's own
> in-order walk, where the cursor is correct by construction; the specialization
> re-check that parks the cursor elsewhere does not come back through this path.
> That is *why* the cache failed and the live read did not, and it is the
> boundary of what this change buys: provenance, not a behaviour fix.
>
> Kept as a corpus entry anyway — `body_res_ctx`'s home module previously had no
> coverage discriminating one module from another. Control-verified in the same
> way as the rest of that file: retyping the binding to the colliding
> `WidgetOther::Widget` fails with E0200 + E0358, so a wrong bind is a hard
> error rather than a silent pass.

> Two intermediate hypotheses were tested and **disproved** before the lifetime
> one: that 2c was mis-resolving a generic parameter (binding the enclosing
> params changed nothing), and that `module_ns_of_file` was returning `""` for
> stdlib paths (it returns `std::core::mem` correctly). Both would have been
> plausible as written-up causes. Neither survived measurement.

### 8.2l The worklist, classified in full (2026-08-05)

§8.2i established that the question is **per context, not per function**, and
classified twelve sites. This is the same pass run over all of them. Every
`ResolutionContext::new` in the compiler was read — 57 total, 42 of which set no
home module — and sorted by *where the syntax it resolves was written*.

**Five of the 42 are not worklist items at all**, and finding that out first
changes the size of the job:

| site | why it needs nothing |
|---|---|
| `types/resolver` 156 | `clone()` — copies `home_module` **and** `home_origin` verbatim |
| `types/resolver` 1301, `mono/state` 302 · 366, `monomorphizer` 290 | bare by design: each only feeds `expand_default_type_args`, which clones the context and sets the home from `entry.module_name` itself |

That leaves **37**. The second row is the important one, and it is the pattern
the rest of this migration should copy: `expand_default_type_args` is correct at
*nine* call sites without any of them knowing it, because the home module is a
property of the `entry` it already receives. Setting provenance **inside the
resolver function that knows the owning entity** is correct by construction;
setting it at each call site is a rule every future caller can forget. Prefer
the former wherever the callee holds the entity.

| what the context resolves | home module | sites | n |
|---|---|---|---:|
| turbofish / scope generic args | the **caller** | `call_resolver` 577 · 4783 · 4833 · 4871, `method_binding` 798 · 1428, `call_specializer` 1368 | 7 |
| annotations written inside a body (cast target, static-match scrutinee, struct-literal args, lambda params/return, `sizeof` operand) | the **enclosing body's** module | `sema` 2079 · 2310 · 2534, `lambda_synth` 72, `lambda_emitter` 108 · 123, `ir_generator` 171 | 7 |
| a specialized body being re-walked, and template defaults | the **template's** module | `call_specializer` 111 · 177 · 1134 · 1149 · 1211 · 1481 · 1532 · 1706, `ast_resolver` 151, `type_resolution` 3629 | 10 |
| an impl block's trait annotation / target args / assoc bindings | the **impl block's** module | `method_binding` 1628 · 1897, `types/resolver` 542, `trait_specializer` 120 | 4 |
| a where-clause bound's trait args | the module that **wrote the bound** | `method_binding` 475, `types/resolver` 726, `sema` 3326 | 3 |
| a method reached through a trait decl | the **trait's** module | `method_binding` 1045, `sema` 687 | 2 |
| the callee's own annotations, off a receiver | the **owner type's** module | `method_binding` 1197 | 1 |
| a whole module's declarations, walked in order | the module being walked — the cursor is correct **by construction** here | `type_resolution` 60 | 1 |
| `Output` on an awaited future — a synthesized leaf, not user syntax | n/a | `sema` 1486 | 1 |
| **two homes through ONE context** | see below | `method_binding` 865 | 1 |

#### `method_binding.cryo:865` is a defect, not a site

One `imp_ctx` resolves the caller's turbofish (`member.generic_args[i]`) and
then the callee's return annotation (`im.func.return_type_annotation`) — two
different modules through one context. There is no single home module that is
correct for it, so it cannot be migrated; it has to be **split into two
contexts** first. It is the only site of its shape, and it is invisible to a
per-function audit: the function looks like one context doing one job.

#### What this predicts

The `caller` and `body` rows are the shapes already proven — §8.2i's eight
turbofish sites and §8.2k's `body_res_ctx` respectively — and both measured
behaviour-neutral, because the cursor is parked correctly during an in-order
walk. **The `template` row is the one to expect divergence from**: those
contexts are built while mono re-walks a specialized clone, which is exactly the
condition under which the cursor names the module that *demanded* the
specialization rather than the one that wrote the code. That is where a
measurement should be pointed first, and it is 10 of the 37.

`type_resolution:3629` belongs to that row and is the clearest single case: it
computes its module as `ctx.has_namespace() ? ctx.namespace_display() : ...` —
the cursor, spelled out — while re-resolving a template's generic parameters.

#### Migrated: `call_specializer:1481`

One site from the `template` row landed with this classification, chosen because
its correctness argument needs no new evidence: `finish_generic_static_method_spec`
builds **two** contexts over the **same body** — `prune_ctx` for the
static-match patterns and `body_ctx` for the local annotations — and only
`body_ctx` had a home module. The value is `owner_entry.module_name`, already
derived fourteen lines below and already documented there as the module the body
was written in. The derivation is now hoisted once and shared, so the two cannot
drift.

**It is behaviour-neutral, and the honest caveat is that no available corpus
reaches it.** `09-json-config` produced a byte-identical output tree and a
byte-identical counter report; `generic_name_collision`, which *does* declare
generic static methods, produced a byte-identical counter report too (97 lines,
checked non-empty — an "identical" between two empty files is the §8.2g failure
in miniature). A counter that does not move here means the path is not
exercised, **not** that the change is proven safe on it. The evidence that it is
safe is the self-host: stdlib and compiler both specialize generic static
methods, and stage-3/stage-4 IR is byte-identical across 244 modules on Linux
and 243 on Windows. Per §4.1 that proves no regression and says nothing about
scope correctness.

#### Migrated: the `caller` and `body` rows — 13 more sites

Both rows use a mechanism already proven and measured (§8.2i's turbofish sites;
§8.2k's `body_res_ctx`), so they landed together rather than one at a time.

| row | migrated | site |
|---|---|---|
| caller | 6 of 7 | `call_resolver` 577 · 4783 · 4833 · 4871, `method_binding` 798 · 1428 |
| body | 7 of 7 | `sema` 2079 · 2310 · 2534, `lambda_synth` 72, `lambda_emitter` 108 · 123, `ir_generator` 171 |

Two of these needed a **required** span parameter rather than an inferred one —
`resolve_generic_scope_name` (7 call sites) and `resolve_sizeof_operand_type`
(2). Required, not defaulted, for the same reason `set_home_module` takes a
required origin: a caller that can omit it silently reinstates the scope-less
context, and every one of those 9 call sites already had the node in hand.

**This is the first change in the migration to move the number.** On
`examples/09-json-config`, output byte-identical:

| | before | after | Δ |
|---|---:|---:|---:|
| **B1 total** | 15,437 | **15,195** | **−242** |
| step 5, GLOBAL LEAF INDEX | 656 | **414** | **−242** |
| `lookup_by_leaf` hits | 2,096 | 1,854 | −242 |
| 2c* home-module (syntax provenance) | 3,893 | 4,283 | +390 |
| B3 authoritative | 65,628 | 66,207 | +579 |

The −242 is the whole point: those are answers that used to fall through to the
global leaf index — §8.2a's root cause, a name resolved with nothing in scope —
and now resolve in the module that wrote them. It retires **37% of that row** on
this target. Unlike §8.2k's provenance-only result, this one is a real reduction
in fallback reliance.

> **One row moved the wrong way, and it is not a regression.** "WOULD BE
> REJECTED once gated" rose 4,192 → 4,360 (+168). Giving a context a home module
> is what lets `resolve_qualified_scoped` judge it against a real scope at all,
> so closing the keystone *grows* the type lane's measured blast radius rather
> than shrinking it. §8.1e's lesson applies unchanged: a violation count says
> what a gate would CATCH, never what it would BREAK, so this number is not
> evidence for or against switching the type gate on. It does mean §6's "re-measure
> before acting" now has a moving target — the type-lane audit should happen
> *after* the keystone closes, not alongside it.

**23 of the 37 remain**, and none of them are the same shape as these. What is
left needs a home value that does not yet exist in hand: a trait's module
(`TraitDeclNode` has no module field), an impl block's module, the module that
wrote a where-clause bound, or — for `call_specializer` 1368, the one `caller`
site not migrated — a `CompilationContext` that `MonoCallSpecializer` does not
hold, since it carries only `current_ns`. §8.2i's warning stands for all of
them: getting caller and callee backwards is a miscompile that every gate in
this repo stays green through.

### 8.2m The rib chain did not exist — FIXED 2026-08-06

§4 defines a tiered precedence and calls it total. **`Scope` had no tiers.**
`resolver/scope.cryo` was one flat `name -> SymbolID` map in which a module's
own declarations and its imports competed for the same slot, first-writer-wins:

- `Scope::insert` returned `false` when the name was taken — a declaration
  arriving *after* an import silently failed to register.
- `Scope::insert_import` kept the **first** entry and raised ambiguity only for
  import-vs-import. A declaration-vs-import conflict was not detected at all.

This was pre-existing rather than a regression — `scope.cryo` was untouched on
this branch and `main` had the same code — and it is the defect the whole
refactor exists to remove.

#### Three failure modes, one flat map

Measured on three-file probes, no generics, no mono. `Vault` declares a leaf
and imports `Decoy`, which declares it too.

| probe | result before the fix |
|---|---|
| standalone project, `import` **before** the declaration | `error[E0214]: expected Decoy::Widget, found Vault::Widget` — the module cannot name its own type, in **every** annotation position (free-fn return, free-fn param, local decl, struct field, static-method return). Every *expression* position was correct — §2.6's asymmetry, alive, on the same line. Both claims re-measured independently rather than inherited: the static-method return fails `E0200` on its own `-> Widget`, and an expression-position probe with every annotation qualified returns the right value before and after. |
| the same project, `import` **after** the declaration | compiled clean, right answer |
| corpus module, `import` **after** the declaration | `error[E0203]: ambiguous type 'Paint': imported from both 'ResolutionScope::OwnsAfter' and 'ResolutionScope::Red'` — the module's **own declaration** reported as an import, and declared ambiguous against a real one. §4 rule 2 confines ambiguity to *within* a tier, so these two never compete. |

Read rows 2 and 3 together: **the same source order produced a clean build in
one program and E0203 in another.** What a name meant depended not only on
where the `import` line sat but on the shape of the rest of the program, which
is what "first-writer-wins over a flat map" amounts to from the outside. No
attempt is made here to explain the difference between those two pre-fix
outcomes; the mechanism that produced both is gone.

And the silent one, which is the reason this matters:

```cryo
namespace BR::Vault;
import BR::Decoy;                                  // Decoy::Widget::make stores v*100
public type struct Widget { v: i64; ... }
public function local_decl() -> i64 {
    mut a: Widget = BR::Decoy::Widget::make(3);    // compiling AT ALL => bound to the IMPORT
    return a.v;
}
```

**Compiles, links, runs, returns 300.** Controlled: retyping the initializer to
`BR::Vault::Widget::make(3)` fails `E0200 expected BR::Decoy::Widget, found
BR::Vault::Widget`, so the bind is proven rather than inferred. After the fix
the same probe fails with expected/found **flipped**, which is the same control
read in the other direction.

#### Why every existing instrument read green

`HomeOrigin`, `BodyNsDiff`, `mono_home`, `CRYO_SCOPE_PROBE` and the B1 ratchet
all answer *"was the home module derived correctly?"*. None answers *"did the
right home module produce the right answer?"*. A new per-event stream
(`CRYO_RN_AUDIT`, `ResolveCounter::rn_answer`) reports the cascade step, the
context's home module, **the ambient cursor**, the name, and the type that won.
On the 300 probe it emits exactly one line:

```
RN  3b-di-canonical  home=<none>  cursor=BR::Vault  Widget  BR::Decoy::Widget
```

**The cursor was correct** — it named the module that wrote the syntax — **and
the bind was still wrong.** The site sets no home module, so 2c is skipped
entirely and 3b canonicalizes through `resolve_type_qualified_name_bare_from`,
which walks the flat scope where the import owns the slot.

That is the finding, and it is what makes this different from the preceding
migration steps: **correct provenance is necessary and not sufficient.** Handing
the remaining 23 scope-less `ResolutionContext` sites their home module would
not have fixed any of the four cases above. §8.2l's dismissal of
`type_resolution:60` ("the cursor is correct **by construction** here") is
accurate about the cursor and says nothing about the bind.

> **The instrument bar from §8.2g, one level up.** Every zero cited in §8.2i–l
> was measured over the population of *provenance* decisions, which is the
> complement of the population where this defect lives. Any new instrument here
> must be able to fail on the four cases above; `rn_answer` is the first one in
> the tree that can, and it prints the cursor and the answer as separate columns
> precisely so a green provenance column cannot stand in for a correct bind.

#### The fix

`Scope`'s entries became a `ScopeEntry { name, sym, is_import }` and the two
entry points now state the tier instead of racing for the slot:

- `insert` (a declaration) arriving over an import **takes** the slot and
  retracts any ambiguity recorded for that name — the recorded conflict was
  import-vs-import, and a declaration shadows all of them (§4 rules 1 and 2).
- `insert_import` arriving over a declaration is **skipped silently** — §4 rule
  1 is explicit that a shadowed outer tier is not a diagnostic.

The displaced import is dropped rather than demoted into the overload set,
because that is what makes the two source orders agree: declaration-first
already produced exactly that candidate set, since `insert_import` never added
itself as an overload.

> **The tier is a field, not a parallel array, and that was a deliberate second
> pass.** The first cut carried `sym_is_import: boolean[]` alongside `symbols`.
> The invariant held — two push sites, both updated — but it held *by
> discipline*: a third insert path added later desyncs the arrays silently, and
> every index-based read after the missing push then returns a NEIGHBOUR's
> tier, which is a wrong bind with no diagnostic. That is the same class of
> defect this section exists to remove, reintroduced one level down. §8.2l's
> rule — set the fact inside the thing that knows it, so a caller cannot forget
> — applies to data layout as much as to `expand_default_type_args`. One
> `push_entry` helper is now the only place a binding is created.

> **One of the two lookup systems already had this.**
> `DeclarationIndex::resolve_qualified_scoped` documents and implements the same
> precedence — *"a local definition beats imports"* — and it is what the VALUE
> lane goes through. That is why a bare **call** in a module that both declares
> and imports a leaf has always bound correctly while the same shape in an
> **annotation** bound to the import, in both source orders. §4.4's "two answers
> for one question" was this, concretely: two lookup systems, one of which
> implemented §4.

#### Measured effect — and what it is NOT

Three counters were added at the tier decisions themselves
(`ScopeDeclOverImport`, `ScopeImportShadowed`, `ScopeAmbigRetracted`), because
a fix whose population is unmeasured is a fix whose blast radius is unknown.

| target | declaration displaced an import | import shadowed | ambiguity retracted |
|---|---:|---:|---:|
| `examples/09-json-config` | **0** | **0** | **0** |
| the compiler's own build (162 local + 81 std modules) | **0** | **0** | **0** |

**No module in `compiler/src` or `stdlib` declares a leaf it also imports.** So
on today's sources this change is inert, and that is the honest headline: the
payoff is for *user* code and for what is representable, not for a bug in the
shipped tree.

Consequently **B1 is unmoved by this change**, and the −101 the ratchet reports
is not mine. Verified rather than assumed: with the compiler changes stashed
and stage-2 rebuilt at `3b65cd36`, `b1-check` reports the identical
`15195 -> 15094 (-101)` with the identical five per-site rows. That drift
belongs to `3b65cd36`, which changed resolution and did not re-pin the golden.
The golden is re-pinned to 15,094 here to keep the ratchet usable — a
permanently-red ratchet gets switched off — and the attribution is recorded so
the two changes are not conflated. §8.2k's rule applies to conflating two
behaviour changes just as much as to conflating a correction with a change.

#### Corrections to earlier sections

- **§8.2g consequence 1 is confirmed, and is now demonstrated rather than
  predicted.** The Linux self-host fixed point passes with the defect present
  *and* with it fixed.
- **§8.2g consequence 2 is confirmed, and is the reason this was findable at
  all.** The corpus was the prerequisite: the entry below was written first,
  failed, and is what the fix is verified against.
- **§8.2g consequence 3 needs splitting, and only half of it survives.** It
  reads: *"No miscompile is being fixed here on today's sources … the change
  carries no user-visible payoff to point at."*
  - *"on today's sources"* — **confirmed, and now measured directly** rather
    than inferred from candidate counts: the three tier counters above are zero
    across the whole self-host.
  - *"no user-visible payoff to point at"* — **falsified.** Ordinary user code
    that declares a leaf it also imports mis-binds silently, with no generics
    and no mono. The two statements were run together because the tree's own
    sources were standing in for "what a program can look like", and §8.2g's
    own argument is why they must not: the corpus cannot contain the shapes it
    is being used to rule out.

#### Corpus

`tests/projects/resolution_scope` gained the tier-2/tier-3 boundary, which
nothing in the tree previously reached — every collision in `resolution_scope`,
`generic_name_collision` and `resolution_tripwire` is import-vs-import or
consumer-vs-its-import, and both live entirely in tier 3.

| file | what it pins |
|---|---|
| `owns_before.cryo` | declares `tint`/`Paint`, imports colliding `Red`; import written **first** |
| `owns_after.cryo` | the same file with the import written **last** — the pair asserts that source order is not part of what a name means |
| `owns_two.cryo` | declares the leaves and imports **two** modules that collide with each other, so tier 3 alone is genuinely ambiguous; exercises retracting the recorded ambiguity, which only the imports-first order reaches |

Every position is value-checked, and each file carries the control that the
shadowed import stays reachable by its qualified path — without it, "a module's
own declarations win" would be satisfiable by dropping the import entirely.

#### A coupling this creates, and the control that guards it

Shadowing changes **how a qualified shadowed TYPE is found**, and the
replacement mechanism is one §6 schedules for deletion.

`resolve_type_qualified_name_from` answers a written path by extracting the
leaf, resolving it in scope, and accepting the result only when
`qualifier_agrees`. Before the fix, bare `Paint` inside a module that shadows
`Red::Paint` resolved to *Red's*, the qualifier agreed, and the path was
answered there. After it, the leaf resolves to the module's own, the qualifier
**disagrees**, and the answer falls through to
`resolve_qualified_type_via_exports` — the M1 export scan.

Measured with `CRYO_PATH_AUDIT` on the corpus, not reasoned:

```
PATH-HIT  M1-AGREE   OwnsBefore  OwnsBefore::Paint  OwnsBefore::Paint   <- own decl
PATH-HIT  M1-EXPORT  OwnsBefore  Red::Paint         Red::Paint          <- shadowed import
PATH-HIT  M1-EXPORT  OwnsTwo     Red::Paint         Red::Paint
PATH-HIT  M1-EXPORT  OwnsTwo     Green::Paint       Green::Paint
```

⇒ **Deleting the export scan without a replacement now silently breaks
qualified access to every shadowed type.** At that point the cheapest-looking
move is to put the deleted mechanism back, which is exactly the ratchet this
subsystem has slipped down before. So the control is written *now*, while the
reason is fresh, rather than discovered as a regression later:
`a_shadowed_imports_TYPE_is_still_reachable_qualified`, exercised in `OwnsTwo`
with two same-leaf candidates so the scan must tell them apart rather than be
trivially right. The function-lane control (`the_import_still_resolves`) does
**not** cover this — the whole defect was type-lane, and the two lanes take
different routes.

#### Still open after this

- **The value lane's overload set.** §4 rule 1 implies that a module declaring
  `foo(i32)` while importing `foo(string)` makes bare `foo("x")` an error, not a
  call to the import. This change makes the two source orders **agree** on the
  candidate set; it does not settle what overload resolution should then do with
  it. Deliberately separate — §8.2k's rule about not conflating a measurement
  correction with a behaviour change applies to conflating two behaviour changes
  too.
- **The prelude tier is still not in `Scope`.** It lives in the
  `DeclarationIndex` (`prelude_ns`), so `sym_is_import == false` means "not an
  import into this scope", not "tier 2 exclusively".
- **3b, 4, 4a and step 5 still exist.** This makes 3b's *answer* correct; it
  does not retire the fallbacks below it. §6's promotion of 2c to `resolve_path`
  is what starves them, and it is now unblocked in a way it was not before: the
  scope it would consult finally means what §4 says.
- **The tier counters are zero on every corpus in the tree**, so they cannot
  catch a regression in this fix by themselves — only `resolution_scope` can.
  Read them as a population size, never as a gate. If a future change makes them
  nonzero on the self-host, that is new information about the *sources*, not
  about the resolver.

### 8.2n Visibility: two more doors, and E0503 is maskable — MEASURED 2026-08-06

§8.1f closed the function **call** lane and §7.1 recorded it as "cross-module
private functions are enforced". Three claims inherited from the previous
handoff were carried as *unverified leads*. All three are now measured against
this tree, two of them with a running program rather than an absent diagnostic.

**Method.** Two scratch projects, each with its own positive control, because an
absent diagnostic is not evidence that a gate was consulted — it is equally
consistent with the probe never reaching it (§4's "a zero needs a control").
Every declaration spells `private` explicitly, since Q7 makes a top-level
declaration public by default.

**Doors 5 and 6 are open, and they execute.** One project, four call shapes,
observed by exit code rather than by diagnostic:

| shape | result |
|---|---|
| private free fn, **called** (door 2 control) | `E0353` — gate fired |
| private **instance** method, member access (control) | `E0353` — gate fired |
| private free fn, taken as a **value** (`const f: () -> i32 = door5;`) | **no diagnostic; runs** |
| private **static** method (`Vault::PubType::door6()`) | **no diagnostic; runs** |

With the two controls removed the program compiles, links, and exits `119` —
`5 + 6 + 105 + 3`, i.e. both private callees returned their own values across a
module boundary. The controls firing in the same batch is what makes the two
zeros interesting.

The cause is structural, not a missed case: `enforce_callee_visibility` is
reached only from call paths, so a reference that never becomes a call is never
offered to it; `enforce_method_visibility` is reached only from member-access
paths, so the static lane never reaches it. Neither is a branch that got the
answer wrong — both are lanes that were never asked.

**E0503 fires, and one unrelated module switches it off.** A/B on a single
import line, with the use site's own scope held identical between the two:

- **A** — `Main` names a cross-module `private type struct Hidden`, spelled
  fully qualified, and nothing else in the program declares that leaf:
  `error[E0503]: type 'Hidden' is private to module 'TypeProbe::Vault'`.
- **B** — add one `public type struct Hidden` in a third module that `Main`
  never imports and never references, pulled into the build by an import in
  `Vault`: **the error disappears**, the program links and exits `7` — the
  private type's own value.

`check_type_name_visibility` strips a written path to its **leaf** before asking
the registry, so the qualifier in `TypeProbe::Vault::Hidden` is discarded and the
question becomes "is any `Hidden` anywhere public?". `private_owner_module`
answers it with a program-wide scan that short-circuits to "allowed" on the first
public same-leaf entry. It also takes its use-site module from
`current_module_name()` — the ambient cursor — which is the same input §8.1e
found misfiring in the function lane, still live here.

**What this changes about §6's plan.** The type lane's defect is *not* only
`resolve_qualified_scoped`'s single-candidate fast path, which is what §7.1
asserted; there is a second, independent leaf-keyed decision in
`ModuleTypeRegistry`. Re-deriving E0503 from the resolved qualified symbol
retires both at once, and is the same shape as the fix that closed the function
lane: ask the resolved symbol, not the leaf, and ask where the syntax was
written, not where the cursor is parked.

**Not yet pinned by a corpus entry.** These four shapes are reproductions, not
gates; nothing in the tree fails today if a door reopens. `visibility_gate`
covers the four *call* doors only. Doors 5 and 6 and the E0503 A/B belong in it
as siblings before either is fixed, on §8.1e's precedent that shutting some
doors is indistinguishable from shutting all of them unless each is named.

### 8.2o The fallback chain's supply is scope-less contexts — MEASURED, and the scope is now required 2026-08-06

§6 step 1 says to promote 2c to `resolve_path`: scope a **required parameter**,
the lookup **total**, `Res::Err` on a miss rather than a fall-through into
3/4/4a/5, so the steps below die of starvation. Measured on
`examples/09-json-config` before changing anything, over all 11,475
`resolve_named` calls (the `CRYO_RN_AUDIT` stream is exactly that population):

| step | answers | `home=<none>` | home set |
|---|---:|---:|---:|
| 2c home-module (syntax provenance) | 4,394 | 0 | **4,394** |
| 3b DI canonicalized | 203 | **203** | 0 |
| 4a scope+arena | 2,304 | **2,304** | 0 |
| 5 global leaf index | 328 | **328** | 0 |
| X failed | 15 | 7 | 8 |

**The partition is exact: every answer below 2c comes from a context carrying no
home module, and no answer below 2c comes from one that does.** The cursor is
set on all 2,835 — it is the *scope* that is absent, not the position.

Two consequences, and the second reorders §6:

**2c does not miss when it runs.** With a scope present the only fall-throughs
are 8 `X-failed` lines, all the generic param `T` in `std::alloc::box` /
`std::alloc::rc` — an unbound-binding defect, unrelated to scope. So the
module-blind chain is not a safety net for names 2c gets wrong; it serves
scope-less callers **exclusively**. The names it serves are the stdlib's core
vocabulary (`Result` 281, `Formatter` 224, `Option` 171, `Str` 170, `String`
118, `GlobalAlloc` 93) — precisely the imported-type case 2c's import-scoped arm
already handles correctly whenever a home module is present.

**Totality is therefore blocked on scope coverage, not the other way round.**
Making `resolve_path` total today would convert 2,835 currently-correct
resolutions into errors — not because the answer was wrong, but because nobody
asked the question. §6's step 3 ("the remaining `ResolutionContext` sites") is a
**prerequisite** of step 1's totality, not a follow-on.

#### What landed: the scope can no longer be omitted

`ResolutionContext::new` now takes `home_module` and `home_origin` as required
arguments. It previously defaulted them to `"" / Unset`, and that silent default
was the fallback chain's entire supply. The reasoning is `set_home_module`'s
own, applied one level out: a site that cannot state its scope has not
established one, and a required argument cannot be forgotten. A site with
nothing to say now writes `HomeOrigin::Unset` and is greppable; a site that
forgot no longer compiles.

57 construction sites were updated mechanically to state today's value, so this
is a **pure refactor**. Controls, all held: B1 `15094 -> 15094`, B3
`66331 -> 66331`, every cascade row unchanged to the answer
(2c\* 4,394 · 3b 203 · 4a 2,304 · 5 328), `resolution_scope` 10/10,
`make test` PASS (unit; compile-fail 170; projects 18).

> **Does this make a wrong bind impossible, or only unmeasured?** Neither — it
> makes an *unasked* one impossible to write by accident, which is strictly
> less. No binding changed. The remaining work is to give those sites a scope
> that is right, and §3.1's question ("what should this name bind to") is not
> answered by threading a module string into them.

#### The worklist, and where 4a's supply comes from

Of the 57 construction sites, **33 establish a scope and 24 do not**. One of the
24 is a false positive: `ResolutionContext::clone` states `Unset` and then copies
`home_module`/`home_origin` from the source on the next two lines, so it
preserves scope. It should pass them to `new` directly rather than assign
after — the state-then-overwrite shape is what made it read as unscoped.

**Structural attribution for 4a — inferred, NOT measured.** 4a is gated on
`bootstrap_mode`, which is true for the whole of TypeResolution, and that pass
runs off a single `TypeResolutionRunner.res_ctx` built at
`passes/type_resolution.cryo:60` with an empty home module. Every per-declaration
context in the pass is a `clone` of it, and `clone` faithfully propagates the
empty scope. That accounts for a bootstrap-only step answering 2,304 times with
`home=<none>` in every single one. This is a structural argument from the code,
not an instrumented one: no counter yet ties a below-2c answer to the site that
built its context. Confirm before crediting a fix with the drop.

**The fix is not the obvious one.** `TypeResolutionRunner::new` already computes
a module name via `ctx.namespace_display()`, and passing that as the home module
is the tempting one-line change. It is wrong: `namespace_display()`'s own
contract is "which module is the compiler standing in right now" — the ambient
cursor — so it would launder a cursor guess into `HomeOrigin::Syntax`, the exact
failure `set_home_module`'s required-origin argument exists to prevent, and the
one §8.1e already paid for once. `ctx.source_file` is no better: it is the walk,
not the syntax. The established idiom is
`ctx.module_ns_of_file(node.span.file)` with `HomeOrigin::Syntax`, applied per
declaration where the node is in hand, so the answer travels with the syntax.
Whether a single per-runner scope is equivalent depends on a compilation unit
being exactly one file; that is true today (file → module is total and 1:1) but
it is an assumption the per-node form does not need.

### 8.2p The fallback chain has SEVEN suppliers, and one is 91% — MEASURED 2026-08-06

§8.2o attributed 4a's 2,304 answers to `TypeResolutionRunner.res_ctx` by reading
the code, and labelled the claim **inferred, not measured**, because no
instrument tied a below-2c answer to the site that built its context. It is now
measured, and the inference was right.

**The instrument.** `ResolutionContext` carries `origin_file` / `origin_line`,
required `new` arguments that every one of the 56 sites supplies as **`FILE,
LINE`** — the pseudo-constants the compiler expands at the call site, which
exist so callers do not pass locations by hand. `clone` passes the original
pair forward rather than naming itself, because a copy inherits the scope, and
therefore the defect, of what it was copied from. The `RN` line gained the pair
as one `file:line` column between `cursor` and `name`.

A hand-written site label was the first cut and is the wrong shape: it is a
second copy of a fact the compiler already holds, kept in step only by
discipline, so renaming the enclosing function or moving the call leaves the
label asserting a location that is no longer true. That is the same defect as
the parallel `sym_is_import` array §8.2m rejected, one level up. A computed
location cannot drift and needs no ratchet to keep honest.

Measured on `examples/09-json-config`, all 2,850 answers below cascade 2c:

| step | answers | construction site |
|---|---:|---|
| 4a scope+arena | **2,304** | `passes/type_resolution.cryo:60` |
| 5 leaf index | 185 | `passes/type_resolution.cryo:60` |
| 3b DI canonical | 95 | `passes/type_resolution.cryo:60` |
| X failed | 1 | `passes/type_resolution.cryo:60` |
| 5 leaf index | 102 | `sema/method_binding.cryo:1200` (`find_generic_method_for_call`) |
| 3b DI canonical | 53 | `types/resolver.cryo:599` (`resolve_concrete_member`) |
| 5 leaf index | 33 | `types/resolver.cryo:599` |
| 3b DI canonical | 44 | `sema/method_binding.cryo:1048` |
| 5 / 3b | 8 / 8 | `mono/trait_specializer.cryo:120` |
| X failed | 6 | `sema/sema.cryo:3336` |
| 3b DI canonical | 3 | `mono/ast_resolver.cryo:181` |
| X failed | 8 | `sema/method_binding.cryo:798` (home **set**) |

The same partition was measured twice, once with hand-written labels and once
with `FILE`/`LINE`, and the two agree row for row.

The attribution is **exhaustive** — no answer reports `site=<none>`, so the
column is measuring the whole population rather than a sample of it.

Three things follow that the structural argument could not give:

- **4a is one site, entirely.** 2,304 of 2,304. §8.2o's inference is confirmed.
- **That site supplies 2,585 of 2,850 (91%), not 2,304.** The same context also
  feeds 185 of step 5 and 95 of 3b. Scoping it is therefore a larger change than
  the row named 4a suggests, and the prediction to check is per-row: **4a → 0,
  step 5 → 143, 3b → 108.** The B1 *total* will move by more than 2,489, because
  `lookup_by_leaf` and `canonical_qualified` are separate rows fed by the same
  lookups; predicting the total to a number is not possible, and a total that
  moves without those rows moving is a finding, not a confirmation.
- **Seven sites supply the whole chain**, not the 22–24 the static worklist
  names. The other unscoped sites either never resolve a name here, or answer
  above 2c.

#### A third category the static scan cannot see

`find_generic_method_for_call` *does* call `set_home_module`, so any grep-shaped
worklist marks it done — and it still answers 102 times with `home=<none>`.
Instrumented at the call rather than inferred: it passes an **empty module while
claiming `HomeOrigin::Syntax`, 1,086 times**, and it is the only site in the
tree that does.

```
RN-HOME-EMPTY   src/compiler/sema/method_binding.cryo:1200   Syntax
```

The mechanism is `owner_module_of` → `arena.module_ns_of`, which has no answer
for a symbolic receiver; `set_home_module` then normalizes the empty string to
`Unset`. Every one of the 102 is the leaf `Formatter` binding to
`std::fmt::display::Formatter` through the global leaf index, in a context whose
other resolution is a generic param.

Returning nothing for a receiver that genuinely has no module is honest. Turning
that into a silent fall into the module-blind chain is not, and it is the same
shape as the default `ResolutionContext::new` used to carry: the source reads as
though a scope was established. When the chain below 2c is deleted these become
`Res::Err`, so this site needs an answer before the deletion, not after.

⇒ The worklist has three categories, not two: **never scoped**, **scoped**, and
**scoped on the success path and unscoped on the one that matters**. Only the
event can tell the third from the second.

#### The control on the zeros

Of the 56 sites, **31 resolve a name at all on this corpus and 25 never run**.
Thirteen of the 22 statically-unscoped sites are among the 25. So "seven
suppliers" is a statement about this program, and the remaining thirteen are
untested rather than clean — §8.2g's rule, applied to the worklist itself. Two
more (`method_binding.cryo:475`, `types/resolver.cryo:1358`) run unscoped and
answer entirely above 2c, so being unscoped does not by itself put a site in
the chain.

#### Controls

Pure instrumentation, and every counter held to the answer: calls 11,475 · 2c\*
4,394 · 3b 203 · 4a 2,304 · 5 328 · B1 15,094 · B3 66,331 · `resolution_scope`
10/10 · `make test` PASS (unit; compile-fail 170; projects 18) · `b1-check` OK ·
`roster-check` OK (2,001).

`clone` also stopped stating `Unset` and overwriting it, and now passes the
scope to `new` directly. §8.2o recorded it as the false positive in the static
scan; the shape, not the behaviour, was the defect.

### 8.2q TypeResolution is scoped, and 4a is dead — MEASURED 2026-08-06

The pass now scopes its shared context **per declaration**, from the node's own
span, at the six top-level walks that thread `runner.res_ctx`:

```cryo
runner.scope_to_decl(stmt.span);        // module_ns_of_file(span.file), Syntax
```

`namespace_display()` was rejected: it answers "which module is the compiler
standing in", so passing it would have recorded a cursor reading as
`HomeOrigin::Syntax` — the substitution that reverted the visibility gate once
already (§8.1e). The seventh walk (`run_struct_field_sync`) already built its
contexts per node and was left alone.

**Predicted before the edit, and checked after.** §8.2p's attribution was the
input, so this is the measurement that tests the attribution as much as the fix:

| row | predicted | measured |
|---|---|---|
| 4a scope+arena | → ~0 | **2,304 → 0** |
| 3b DI canonical | → ~108 | **203 → 108** |
| 5 leaf index | falls by *at most* 185 | **328 → 143** (−185, the maximum) |
| 2c\* syntax provenance | rises by roughly the same | **4,394 → 6,978** (+2,584) |
| calls (total) | unchanged | **11,475** |
| B1 | falls ≥2,000 | **15,094 → 12,605** (−2,489) |
| both `WRONG_` tripwires | still pass | **still pass** |

−2,489 is exactly 2,304 + 185. **The 2,585 answers §8.2p attributed to
`type_resolution.cryo:60` are the 2,584 that moved, plus one that still fails —
and it now fails with a scope set**, which makes it an unresolvable name rather
than an unasked question.

Below-2c fell **2,850 → 266**. That site is gone from the fallback chain.

#### Two numbers that did not behave as predicted

- **B1 moved by exactly the row sum, not more.** §8.2p predicted the total would
  fall by *more* than 2,489 because `lookup_by_leaf` and `canonical_qualified`
  are fed by the same lookups. `lookup_by_leaf` did fall by 185 — but it carries
  the B1 *flag* without being in the summation set, which the trap list already
  says (`total <= row_sum` is the invariant, the label is a family). The
  prediction was wrong for a reason that was written down before it was made.
- **`WOULD BE REJECTED once gated` rose 4,366 → 4,423 (+57)**, alongside
  `lookup_qualified_alternatives` +1,190 and `resolve_qualified_scoped` +1,190.
  Unpredicted, and the mechanism is the change itself: 2c now runs a scoped
  qualified lookup for 2,584 more names, so the population that counter is
  evaluated over grew. It is a preview of the future visibility gate, not a
  regression — the gate is off — but note that a **rate** was not measured, only
  a total, so this says nothing yet about whether scoping makes rejection more
  or less likely per lookup.

#### Six declarations the pass cannot scope, and why the fix is not a skip

`RN-HOME-EMPTY` — the instrument from §8.2p — fired **6 times at
`type_resolution.cryo:60` after this change and 0 times before it**, so the
change introduced them. Measured rather than reasoned: all six report
`file='' line=0`. They are synthesized top-level declarations carrying no span,
so there is no syntax to derive a scope from, and `set_home_module` normalizing
the empty answer to `Unset` is the correct outcome.

The tempting tidy-up — skip `set_home_module` when the span has no location — is
**wrong, and dangerously so**. The context is shared across the walk and
re-scoped per declaration, so skipping leaves the *previous* declaration's home
module in place. A scope that outlives the node it came from binds the next
declaration's bare names in the wrong module, with no diagnostic: the same
failure this whole section exists to remove, reintroduced by an apparent
cleanup. The reset is load-bearing and is now stated at the function.

That an instrument built for one defect immediately caught a second one, in the
change that consumed its output, is the argument for reporting at the event
rather than tallying at exit.

#### What is left, and what deleting the leaf index would cost

266 answers below 2c, from six sites. Step 5's entire remaining population on
this target is **two leaf names**:

| leaf | answers | site |
|---|---:|---|
| `Formatter` | 102 | `method_binding.cryo` `find_generic_method_for_call` |
| `Str` | 41 | `types/resolver.cryo` `resolve_concrete_member` (33), `trait_specializer.cryo` (8) |

So the leaf index's blast radius here is not 328 names but two, and **102 of the
143 are the empty-scope defect §8.2p already isolated** — the site that calls
`set_home_module` with an empty module 1,086 times while claiming `Syntax`.
Fixing that one site is what takes step 5 to 41 on this target; it is a
prerequisite of the deletion, not separate work.

The two `WRONG_` tripwires in `tests/projects/resolution_tripwire` are still
green, and correctly so: `Orphan` imports nothing, so a scope makes 2c *miss*
rather than answer, and step 5 catches it exactly as before. **Scoping cannot
flip them; deleting step 5 is what flips them**, and that flip — the project
converting to `compile_fail` with E0203 in the same change — is the outcome that
turns this from a counter delta into a defect that stops existing.

### 8.2r The declaration carries the scope — three sites, MEASURED 2026-08-06

§8.2q left `find_generic_method_for_call` as the largest remaining supplier: 102
of step 5's 143, and the one site in the tree that passes an empty module while
claiming `HomeOrigin::Syntax`, 1,086 times. The mechanism §8.2p measured was
`owner_module_of` → `arena.module_ns_of`, which has no answer for a symbolic
receiver.

**The fix is a deletion of a proxy, not a guard on it.** The context exists to
re-resolve `orig_func`'s own parameter and return annotations, so their scope is
the file that *declares the method*. Keying off the receiver's type asks a
different question and merely agrees with the right one whenever a type's methods
live beside it:

```cryo
owner_ctx.set_home_module(this.ctx.module_ns_of_file(orig_func.span.file),
                          HomeOrigin::Syntax);
```

The three answers genuinely differ — a trait default's body lives in the trait's
file while the receiver is the implementing type, and an `implement` block may
sit in a third module again. `module_ns_of_file` is the same total
file → module function §8.2q scopes `TypeResolution` with, so this is one
question with one answering path rather than a receiver lookup with an
empty-string branch. The assignment moved below the method search because the
declaration is the thing that carries the answer; the early returns above hand
back an unscoped context, and all three callers discard it when `*out_func` is
null.

**Predicted before the edit, and checked after.** Every row hit exactly:

| row | predicted | measured |
|---|---|---|
| `RN-HOME-EMPTY` at this site | → 0 | **1,086 → 0** |
| 5 leaf index | → 41 | **143 → 41** |
| step-5 leaf names | `Str` only | **`Formatter`×102 gone, `Str`×41** |
| 3b DI canonical | unchanged | **108** |
| calls (total) | unchanged | **11,495** |
| 2c\* syntax provenance | +102 | **6,998 → 7,100** |
| B1 | −102 | **12,605 → 12,503** |

The site is now absent from the below-2c attribution entirely; below-2c fell
**266 → 164**. `RN-HOME-EMPTY` is left with only the six span-less synthesized
declarations §8.2q explained. `b1-check` reported the same −102 against its own
golden and was re-pinned to 12,503, with `lookup_by_leaf hits` 1,583 → 1,481 —
the family label falling by the row it actually contains, as §8.2q's correction
predicts.

#### What this is NOT: a route change, and the corpus says so

**No binding changed.** Three corpus entries were built to make the old and new
scopes disagree observably, and none could:

- **a generic trait default returning a bare leaf** — the declaration fails to
  typecheck against its own return annotation *before* this site is reached,
  because the default's **body** binds bare names in the implementing module.
  Reproduces with this fix reverted.
- **the same with the body qualified** — the instantiated-for-implementor check
  binds the bare *return annotation* in the implementing module too. Reproduces
  with the fix reverted.
- **a cross-module inherent `implement struct` block** — both compilers emit the
  identical mangled symbol, carrying the *declaring* module's type, and both then
  fail an unrelated E0900 (`Incorrect number of arguments passed to called
  function` for a generic method returning an aggregate).

So on every shape reachable today the leaf index was already finding the same
type by global index, which is why the self-host stays byte-identical and why
`resolution_scope` stays 10/10. The measurable effect is the counter, and the
counter is the pin: `tests/b1-baseline.txt` asserts the per-site rows, so
restoring the receiver-type proxy fails `b1-check` on the row it moved.

> **Does this make a wrong bind impossible, or only unmeasured?** It removes the
> largest remaining *unasked* question — 1,086 contexts that read as scoped and
> were not. It does not by itself change an answer, and the entry that would
> prove one cannot be written until the trait-default lanes above are fixed.

The two `WRONG_` tripwires are still green, still correctly: deleting step 5
remains what flips them.

#### The same mechanism at two more sites

`find_generic_method_on_receiver_bounds` (the abstract-receiver path) built its
context and **never scoped it at all**. Its `found` comes from `td.methods` — a
method of the *bound trait* — so the annotations were written in the trait's
declaration file, and the receiver is a generic param with no module to key off
even in principle. Same one-line answer, from `found.span.file`.

`TypeResolver::resolve_concrete_member` resolves an **assoc-type binding
annotation**, which is the `implement` block's own text. Neither the trait nor
the target type can stand in for it: an `implement` block may live in a third
module again, which is exactly the disagreement `resolve_counter.cryo` names as
the thing that decides between "derive the home from an entity in hand" and
"plumb a file → namespace map". The entity cannot answer here, so the file does.

`TypeResolver` had no `CompilationContext` — that is the import cycle
`compilation_context.cryo` creates — so it takes a `ModuleGraph*` and calls
`ModuleGraph::home_ns_of_file`, the same escape mono already uses for the same
reason. It is a **required** constructor argument, matching the file's own stated
rule that registries are mandatory so no caller can silently bypass one; there is
a single construction site, in `compilation_context.cryo`.

| row | after §8.2q | receiver-bounds | assoc-binding |
|---|---:|---:|---:|
| 3b DI canonical | 108 | **64** | **11** |
| 5 leaf index | 41 | 41 | **8** |
| 2c\* syntax provenance | 7,100 | **7,144** | **7,230** |
| B1 | 12,503 | 12,503 | **12,470** |
| below 2c | 164 | **120** | **34** |
| calls (total) | 11,495 | 11,495 | 11,495 |

**B1 did not move for the receiver-bounds fix, and that was predicted**: 3b is a
B3 row, so draining it changes which authoritative step answers without touching
the fuzzy total. `by caller: canonical_qualified` held at 2,527, which was the
stated condition under which B1 *would* have moved.

#### What is left below 2c

| answers | site | kind |
|---:|---|---|
| 8 + 8 | `mono/trait_specializer.cryo:120` | 5 / 3b — the last `Str` |
| 3 | `mono/ast_resolver.cryo:181` | 3b |
| 8 | `sema/method_binding.cryo:798` | X-failed, home **set** |
| 6 | `sema/sema.cryo:3336` | X-failed |
| 1 | `passes/type_resolution.cryo:60` | X-failed, home **set** |

Only 19 of the 34 are a missing scope. The other 15 are `X-failed` with a scope
present — an unbound-binding defect, not this one, and they will not be fixed by
scoping anything. **Step 5's entire remaining population is `Str`×8 from one
site**, so the leaf index's blast radius is now a single leaf name.

#### Three defects this exposed, none of them this fix's

Each reproduces with the change reverted, so each is pre-existing:

1. **A generic trait default's body binds bare names in the implementing
   module**, not the trait's file — so a default whose body and return
   annotation share a bare leaf fails E0200 against itself.
2. **A generic trait default's bare return annotation binds in the implementing
   module** when instantiated per implementor, disagreeing with the same
   annotation at its declaration site.
3. **A generic method in a cross-module inherent impl miscompiles** — E0900,
   wrong argument count at the call, when it returns an aggregate.

Also: `docs/grammar.md` §188 gives `TargetType ::= QualName GenericArgs?`, but
the parser accepts only a bare identifier as an inherent impl target —
`implement struct Gadget`, never `implement Widget::Gadget`. Where the code and
the grammar disagree, the code is the defect.

### 8.2s The chain below 2c is starved, and 2c answers out of scope — MEASURED 2026-08-07

Two sites carried the whole remaining below-2c population. Both are the §8.2r
mechanism — the declaration carries the scope — applied where the declaration is
an `implement` block and a method body.

**`mono/trait_specializer.cryo` `concrete_trait_args_for`** resolves
`impl_node.trait_annotation`'s args, which are the `implement` block's own text,
so the scope is `impl_node.span.file`. It is the same shape as
`resolve_concrete_member` and takes the same answer. `MonoTraitSpecializer` had
no `ModuleGraph*`; it gained one from `Monomorphizer::set_module_graph`, which
already cascaded to the two collaborators that build contexts.

**`mono/ast_resolver.cryo` `prune_static_match_arms`** was unscoped *on purpose*,
and the stated reason does not survive its own sibling. The site records that it
resolves "a mix of written annotations and synthesized spec names", and that a
synthesized name has no home to be right about. But the **same walk**
(`prune_static_match_in_block`) is entered from `call_specializer.cryo:1191` with
a context scoped by the body's span, and there a synthesized spec name resolves
at 2c under that scope:

```
RN  2c-home-syntax  home=std::collections::string  cursor=std::io::stdio
    6String$LN$L3std.5alloc.9allocator.11GlobalAlloc$G$G  ->  std::collections::string::String<...>
```

Two entry points into one walk, disagreeing about one question. The spec name is
registered under the module the body was written in, which is the module the span
names, so both kinds of name take the same scope. The walk now re-scopes from
each body's own span — for **every** method, never skipped, because the context
is shared and a home that outlives its node binds the next body's bare names in
the wrong module (§8.2q's reset rule).

Measured on `examples/09-json-config`, predicted per row before the edit:

| row | before | trait args | static-match arms |
|---|---:|---:|---:|
| 3b DI canonical | 11 | 3 | **0** |
| 5 leaf index | 8 | **0** | 0 |
| 2c\* syntax provenance | 7,230 | 7,246 | **7,249** |
| below 2c | 34 | 18 | **15** |
| calls (total) | 11,495 | 11,495 | **11,495** |
| B1 | 12,470 | 12,462 | **12,462** |

**Every cascade row below 2c is zero on this target** — 3a, 3b, 3c, 4, 4a and 5
all answer nothing. B1 fell by exactly the 8 leaf-index answers and no more,
because 3b is a B3 row; `by caller: canonical_qualified` held at 2,527, the
stated condition under which B1 would have moved further.

**On this target.** A second target says the chain is not empty, and says
something better: measured on `resolution_tripwire`'s *test* build, 19 answers
remain below 2c after this change, and they fall into two kinds that the
09-json-config population happened to hide.

| answers | site | shape |
|---:|---|---|
| 12 | `passes/type_resolution.cryo:60` (3b, home **set**) | `syscall::STARTUPINFOA`, `mpsc::Receiver`, `thread::Scope` |
| 3 | `passes/type_resolution.cryo:60` (3a, home **set**) | `ResolutionTripwire::Depot::Crate`, fully literal |
| 4 | `sema/method_binding.cryo:475` (3b, home **none**) | bare `Result` |

The first fifteen are **not a scope gap at all**: 2c runs only for a bare leaf
(`!contains_separator`), so an abbreviated rule-6 path skips it by construction
and 3a/3b are the lane that answers it. Those rows cannot go to zero by scoping
anything — they need §5.1's "first segment in scope, remainder rooted", which is
`resolve_path`. **3b is the qualified-path answerer, and deleting it is gated on
`resolve_path` existing, not on the bare-leaf supply drying up.**

The last four are a genuine unscoped supplier, and they correct §8.2p: that
section lists `method_binding.cryo:475` among the sites that "run unscoped and
answer entirely above 2c". That was true of `09-json-config` and is false here.
"Seven suppliers" was always a statement about one program; this is what it costs
to read it as a statement about the compiler.

**No answer changed.** The multiset of (name → resolved type) over all 11,495
resolutions is byte-identical before and after, across 353 distinct pairs. This
is a route change, and the counter is what pins it.

The 15 that remain are all `X-failed`, and the earlier classification of them was
wrong in a way worth correcting: **9 carry a scope and 6 do not**
(`sema/sema.cryo:3336`, home `<none>`). The conclusion is unchanged but for a
different reason — five of the six names are the generic param `T` and the sixth
is `(`, and no module scope can answer a generic param. They are an
unbound-binding defect, not a scope one.

#### The starvation does not reach the tripwire, and that was assumed

§8.2q and §8.2r both state that scoping cannot flip the two `WRONG_` tests in
`tests/projects/resolution_tripwire` and that **deleting step 5 is what flips
them**. Measured against the tripwire project itself, that is false:

```
RN  2c-home-syntax  home=ResolutionTripwire::Orphan  Crate  ->  ResolutionTripwire::Depot::Crate
```

`Orphan` has no `import` statement at all. The bare `Crate` is answered by **2c**,
under a home module that is correct and that cannot see the name — four of its
five resolutions, at `type_resolution.cryo:60` and `ast_resolver.cryo:90`. Step 5
answers **0** on that project too, and the tests still pass. The pre-session
compiler produces the identical rows, so this is pre-existing and not the
starvation's doing; what is new is that nothing below 2c is left to blame.

The mechanism is 2c's import-scoped arm calling
`DeclarationIndex::resolve_qualified_scoped`, whose single-candidate fast path
(`decl_index.cryo:1342`) computes the reachability verdict, counts what a gate
would reject, and then returns `Unique` regardless. That is the fast path §1
names as the root cause, reached through the step that was supposed to be the
scope-respecting one. The arm's own comment claims it "cannot bind to some
unrelated same-leaf type in a module the home module never imported" — true only
while the leaf has two candidates. For a leaf that is unique program-wide the
imports are never consulted, and that is exactly the tripwire's shape.

⇒ **Deleting 3b/4/4a/5 cannot flip the tripwire.** They answer nothing, so the
flip is gated on the fast path, which is §3.3's enforcement for the type lane and
needs the same decision the function lane got in §8.1f.

**But "they answer nothing" is a property of the corpus, not of the code**, and
one A/B says so. Copy `resolution_tripwire` out of the tree, add a second module
declaring the leaf `Crate`, and import that module from the *test* file only —
`Orphan` still imports nothing, which is the property under test:

| variant | who answers `Crate` in `Orphan` | result |
|---|---|---|
| unique leaf (the corpus today) | **2c**, `home=Orphan` | binds `Depot::Crate`, tests pass |
| leaf declared twice | **5 GLOBAL LEAF INDEX**, `home=Orphan` | binds `Decoy::Crate` — the wrong one — and the tests fail `E0200` |

Two candidates skip the fast path, the multi-candidate branch finds no local and
no imported candidate, `resolve_qualified_scoped` returns `NotFound`, and 2c
misses exactly as it should. The leaf index then answers out of scope and picks
by declaration order. So the deletion is not cosmetic: **step 5 is live, it is
merely starved by 2c grabbing every unique leaf first**, and a plural leaf with
nothing in scope is a wrong bind today that surfaces as a type error at the use
site rather than as a resolution diagnostic.

#### The corpus entry that pins it — `tests/projects/resolution_leaf_index`

That A/B is now a project, and it had to be a **separate** one: adding the second
declaration inside `resolution_tripwire` makes its two existing `WRONG_` tests
fail, because the leaf they depend on stops being unique.

`Alpha` and `Omega` both declare `Widget<T>`, distinguished by a `tag()` method
so the winner is observable at runtime and not only in a type annotation.
`Orphan` imports neither and names bare `Widget` in a static template and a free
function — sema's two binding doors. Measured, `Orphan`'s four resolutions are
**step 5**, answering `Alpha::Widget`; `Importer`, which imports `Omega`, is
answered by **2c**.

`Importer` is the control that says what the defect is, and it is built so it
cannot pass by luck: `Omega` **loses** the name-order pick, so binding it is only
explicable by the import. It also guards the fix in the other direction — a
change that made `Orphan` an error by making every bare plural leaf unresolvable
would break `Importer` too, and §4's rib chain says an imported module's export
set is in scope.

**The pin was verified by breaking the property, not by observing green.**
Renaming `Omega` to a name that sorts before `Alpha` — changing nothing about
`Orphan`, which still imports nothing — moves the binding with the rename and
both `WRONG_` tests fail to build. So the tests are pinned to *which* module the
leaf index picks, and a change in the pick cannot pass silently.

#### A body's bare struct literal does not take the scoped path

Building that control surfaced a defect, and it reproduces on the pre-session
compiler, so it is pre-existing. In `Importer`:

```cryo
static pack<T>(v: T) -> Widget<T> {     // annotation  -> Omega::Widget  (2c, imported)
    return Widget<T> { v: v };          // literal     -> Alpha::Widget  (leaf index)
}
```

`error[E0200]: expected Omega::Widget<i64>, found Alpha::Widget<i64>` — **the
function fails to typecheck against its own return annotation**, because the
annotation resolves through the scoped path and the body's struct literal does
not. This is the §8.2r trait-default family in its simplest possible shape: no
traits, no generics beyond one param, no monomorphization — one module, one
function, two answers for one written leaf. The literal is written qualified in
the corpus so the control isolates the annotation, with the reason stated at the
site.

Sized on two targets, so the rate is available and not only the total:

| target | single-candidate fast paths | would be rejected once gated | of which not public |
|---|---:|---:|---:|
| `examples/09-json-config` | 40,673 | 4,278 (10.5%) | 0 |
| `resolution_tripwire` | 50,881 | 5,098 (10.0%) | 0 |

Every rejection is **unreachable** (the use site never imported the namespace),
none is a private candidate. Read the number as "what a gate would reject given
today's `use_site_ns` inputs", which are a mix of syntax provenance and ambient
cursor — §8.1e is the record of what happens when a cursor-derived use site
reaches a gate.

#### A zero that needed a control, and got one

`5 GLOBAL LEAF INDEX = 0` does **not** mean the global leaf index is unused.
`lookup_by_leaf` is still called 5,051 times with 1,440 hits on the same run —
it fell by exactly the 8 answers cascade step 5 lost. The other callers are the
qualified-path lane (`canonical_qualified`, 2,527), which answers a different
question and which §8.2m's coupling note already flagged as load-bearing for
reaching a shadowed type by its qualified path. The trap-list rule that B1's flag
is a family label rather than a summation set applies to the deletion plan too:
retiring cascade step 5 does not retire the leaf index.

### 8.2t The gate's migration set is three imports — MEASURED 2026-08-07

§8.2s sized what a gate at `decl_index.cryo:1342` would reject (4,278 on
`examples/09-json-config`) and said the number could not be read until each
rejection was attributed. The instrument for that **already existed**:
`CRYO_VIS_AUDIT` has emitted `VIS-VIOLATION <use-site> <candidate>` per event all
along. What was missing was not compiler code but a question asked of its output.

**The classifier.** For each event, does the use site's own source contain the
candidate's leaf as a token at all? If it does not, that module cannot have
written the name, so the `use_site_ns` handed to the gate is the ambient cursor
(or a synthesized call), not the syntax's home. Token-*present* does not prove
the use site wrote that particular occurrence, so this is a **lower bound** on
the cursor share.

| bucket | events | share |
|---|---:|---:|
| use site never writes the leaf — cursor artifact | 4,174 | 97.6% |
| use site does write the leaf — candidate violation | 72 | 1.7% |
| use site outside `stdlib`, unmapped (`Main`) | 32 | 0.7% |

72 is six distinct pairs, so they were read rather than counted. **Three are
real** — a module using a leaf whose namespace it does not import, with
`std::core::drop` confirmed absent from `stdlib/prelude.cryo`'s re-export list:

| module | leaf | uses |
|---|---|---|
| `std::collections::hashmap` | `NonNull` | 5 annotations and constructor calls |
| `std::io::stdio` | `Drop` | 3 `implement trait Drop for …` |
| `std::future::waker` | `Drop` | 1 `implement trait Drop for …` |

The other three (`allocator → String`, `allocator → RawBuffer`,
`io::traits → File`) are the classifier's own false positives: every mention is
in a doc comment. The unmapped 32 are `Main → GlobalAlloc`, and the example's
source never writes `GlobalAlloc` either, so they join the cursor bucket.

**The migration is three `import` statements.** Added, and predicted per row
before the edit: `VIS-VIOLATION` **4,278 → 4,215** (−63, exactly the genuine
events), every cascade row unchanged (2c\* 7,249 · below-2c 15 · calls 11,495),
and the (name → resolved type) multiset over all 11,495 resolutions **identical**
to the pre-session compiler. The imports make the source say what it already
meant; no binding moved.

One row moved that was **not** predicted, and it is recorded rather than
absorbed: `lookup_by_leaf` fell by **9** on both targets (B1 12,462 → 12,453,
re-pinned), the three names now resolving through the import-scoped arm instead.
Nine is also the number of source *uses* the imports cover (`NonNull` ×5,
`Drop` ×3+1), but that attribution is **unconfirmed**: the only instrument that
could tie a leaf lookup to a name — `CRYO_LEAF_AUDIT`'s `LEAF-HIT` — reports
cascade step 5's *answers*, which are zero on this target, so it emits nothing
here. `lookup_by_leaf` and step 5 are different populations, which is the
family-label trap arriving a third time in one session.

⇒ **Zero genuine §5.1 violations remain on this target**, and the 4,215 that
remain are, to the limit of this measurement, entirely use sites that never wrote
the name. The gate is therefore **not** blocked on migrating source. It is
blocked on `resolve_qualified_scoped` being handed a use site with real
provenance — which is precisely §8.1e's conclusion for the function lane,
now measured for the type lane rather than inherited from it.

#### Which caller supplies them — partially measured

`resolve_qualified_scoped` has four callers. Three pass the ambient cursor by
their own contracts: `CompilationContext::resolve_scoped` (26 call sites,
`current_module_ns()` — "the module currently being processed") and
`call_specializer.cryo:542` / `:1299` (`current_ns`). Only 2c passes
`ctx.home_module`, whose origin is known.

Cross-checking the `VIS-VIOLATION` stream against `CRYO_RN_AUDIT` splits them:

- **107 are 2c**, and they match exactly — `option → Formatter` (56) and
  `result → Formatter` (51) appear in both streams with those counts, all from
  `mono/ast_resolver.cryo:90`. That site scopes by the **template entry's
  module** and labels it `HomeOrigin::Syntax`; for an `implement Display for
  Option<T>` written in `std::fmt::display`, the entry's module is
  `std::core::option` while the annotation's home is `std::fmt::display`. Its own
  comment already concedes this is TWO homes through ONE context and must be
  split; this is the first measurement of what the compromise costs.
- **The largest pair is not 2c at all.** `option → Str` is 1,568 `VIS-VIOLATION`
  events and **zero** `RN` lines — no context with that home ever resolves `Str`
  through `resolve_named`. Those arrive through the cursor-based callers.

Which of the three cursor callers dominates was not measurable from that stream:
it carried no construction-site column. §8.2u builds it.

### 8.2u The gate's rejections are 88% the caller's cursor — MEASURED 2026-08-07

`resolve_qualified_scoped` now takes `origin_file`, `origin_line` and `origin`
alongside `use_site_ns`, on the reasoning that made `home_origin` required: a
namespace is only as good as the caller's claim about it, and the claim is not
recoverable from the value. Every caller passes `FILE`/`LINE`; the three
cursor-based ones pass `Cursor` by construction rather than by inspection, and
2c passes `ctx.home_origin.display_name()`. `CompilationContext::resolve_scoped`
takes the site from **its** caller rather than naming itself — the
`ResolutionContext::clone` rule, without which its 23 call sites collapse into
one wrapper location and the wrapper reads as the whole population.

Pure instrumentation: the B1/B3 counter block is identical to the run before it,
and the (name → resolved type) multiset over all 11,495 resolutions is unchanged.

**Attribution is exhaustive** — 4,215 of 4,215, no event reports an unknown site.
Crossed with §8.2t's question (does the use site's own source contain the leaf at
all?):

| claimed provenance | use site writes the leaf | events | share |
|---|---|---:|---:|
| `Cursor` | never | 3,702 | 87.8% |
| `Syntax` | never | 472 | 11.2% |
| `Syntax` | unmapped (`Main`, and it does not write it either) | 32 | 0.8% |
| `Cursor` | yes | 7 | 0.2% |
| `Syntax` | yes | 2 | 0.0% |

The nine that "write the leaf" are §8.2t's own doc-comment false positives
(`allocator → String`/`RawBuffer`, `io::traits → File`), read by hand and
confirmed. **After the three imports, no would-be rejection is a name its use
site actually wrote.**

Three classes remain, and none of them is a source migration:

- **3,702 — the caller handed over the ambient cursor.** Five sites in
  `sema/call_resolver.cryo` supply 3,549 of them (`:3741` and `:3548` at 1,014
  each; `:220`, `:3454`, `:4964` at 507 each). A gate would reject a name because
  of where the compiler was standing.
- **214 — laundered provenance at `mono/ast_resolver.cryo:90`**, all
  `option`/`result` → `Formatter`/`FmtError`. The site scopes by the template
  *entry's* module and labels it `Syntax`; `implement Display for Option<T>` is
  written in `std::fmt::display` while the entry's module is `std::core::option`.
  Its own comment concedes the two-homes-one-context problem. §8.2t inferred 107
  of these by cross-referencing `RN`; the direct instrument says 214, and the
  direct number supersedes the inference.
- **~290 — a default type argument resolved at the use site.** Every remaining
  `Syntax` row is `… → std::alloc::allocator::GlobalAlloc`, spread across
  `json::value`, `json::parser`, `json::serializer`, `fs::metadata` and `Main`.
  `std::json::value` uses `String`/`Array` 68 times, **never writes
  `GlobalAlloc`**, and does not import `std::alloc::allocator`. The default in
  `type struct String<A = GlobalAlloc>` is written in `std::collections::string`,
  so by §8.2r's rule its scope is that file — but it is expanded and then
  resolved against whoever wrote `String`. This is the declaration-carries-the-
  scope defect again, in a shape none of the earlier sections reached, and it is
  the one that cannot be fixed by adding imports: it would demand
  `import std::alloc::allocator` in every module that names a `String`.

⇒ Turning the gate on is gated on all three, in that order of size. The first is
a scope problem in `call_resolver`, the second is the split
`ast_resolver.cryo:90` already documents as owed, and the third is a defect this
measurement found.

### 8.2v The scope lane is scoped, and the fast path swallows the difference — MEASURED 2026-08-07

§8.2u's largest class was the caller's ambient cursor: five sites in
`sema/call_resolver.cryo` supplying 3,549 of 4,215 would-be rejections. All five
resolve the first segment of a written path, so the answer is the one §8.2q/§8.2r
use — the module that wrote the syntax.

`CompilationContext` gained `resolve_scoped_at` / `resolve_scoped_or_at` /
`scope_is_ambiguous_at`, taking the node's own `span.file`. They fall back to the
cursor, not to "no scope", when the file names no module: an invalid namespace
compares unequal to every real one, so judging against it would read missing
provenance as "written somewhere else" and reject everything — the false-positive
direction that reverted the gate once. The fallback is counted *and* reported as
the `Cursor` origin.

Six sites moved, not five. `resolve_scope_call` asks `scope_is_ambiguous` one
line before it binds with `resolve_scoped_or`; leaving the first on the cursor
while the second took the syntax would make the E0154 diagnostic and the binding
two answers to one question from two different modules. `resolve_scope_owner_template`
takes a bare `SymbolStr`, which carries no provenance, so the span threads in from
its four callers.

**The migration is exact, and the untouched half proves it:**

| population | before | after |
|---|---:|---:|
| would-be rejections claiming `Cursor` | 3,709 | **160** |
| would-be rejections claiming `Syntax` | 506 | **506** |
| total | 4,215 | **666** |

The `Cursor` drop is 3,549 — the five sites, to the event. The `Syntax` half is
bit-identical, which is the control: a change that only re-homed cursor callers
must leave every non-cursor caller alone, and it did.

**Provenance was never missing.** `ScopeUseCursor` = **0** over 40,828 scope
resolutions, so the cursor fallback is dead code on this target and the drop
cannot be an artifact of names quietly falling back. `ScopeUseDiff` = **9,563**:
on 23% of scope resolutions the writing module and the ambient cursor name
different modules.

**And not one of those 9,563 changed its answer.** The entire counter report is
identical apart from the three new rows and the rejection count; all 11,495
(name → resolved type) pairs are unchanged; `b1-check` reads 12,453 against its
golden with no re-pin; `resolution_scope` 10/10, `resolution_leaf_index` 4/4 and
`resolution_tripwire` 4/4 are unmoved.

> Read "the counter report is identical" with §8.2w's correction: three `Scope*`
> tier rows were past the end of the tally array in *both* runs, so their 0 = 0
> comparison was vacuous rather than confirming. The claim above holds for every
> row that was actually being measured, which was not all of them.

⇒ **§8.2u's expectation that a correct use site would select a different candidate
is falsified on this corpus, and the reason is §1's root cause.**
`resolve_qualified_scoped` consults `use_site_ns` in two places: the
single-candidate fast path, which computes the verdict and returns `Unique`
regardless, and the multi-candidate branch, which actually selects. Every one of
the 9,563 mis-judged names is a leaf that is unique program-wide, so every one
took the fast path. **A use site cannot change an answer while the fast path
answers before consulting it.** The scope work is not a prerequisite that might
also fix bindings; it is the input the gate needs, and the gate is now the only
thing between a correct use site and a correct binding for all 9,563.

**The 666 survivors attribute exhaustively, into three defects and no source
migration:**

- **~325 — a default type argument resolved at the use site** (§8.2u's third
  class, now sized directly): 283 `GlobalAlloc`, 35 `AllocError`, 7 `Layout`.
  The largest supplier is `resolve_generic_scope_name`, with
  `type_resolution.cryo:60` behind it. **The mechanism is NOT
  `expand_default_type_args`** — that reading is corrected, with a measurement,
  in §8.2w.
- **247 — `Formatter`/`FmtError` from `mono/ast_resolver.cryo:90`**, the
  laundered-provenance split that site's comment already concedes is owed.
  §8.2u measured 214 here; the direct count is 245 at the site.
- **122 — a third cursor family, in mono, that §8.2u's three classes did not
  separate.** `mono/call_specializer.cryo:544` and `:1302`, 61 each, both
  honestly declaring `Cursor`. Every one of the 122 names `std::core::intrinsics`
  as the use site — a module with **no `import` statement at all**, which writes
  `NonNull` zero times. It is the same defect as the six just fixed, but harder:
  the site's own comment states the caller's import scope "is not reconstructible
  here", so mono cannot supply a use site the way sema can.

Four cursor sites remain in `call_resolver` (`scope_is_generic_template` ×2,
`scope_names_a_type`, `lookup_scope_template`). All take a bare `SymbolStr` with
no node in hand, and all answer **zero** would-be rejections on this target, so
threading spans through them is unverifiable here and was left alone rather than
done blind.

### 8.2w The default annotation is spliced, not expanded — and the tally was blind — MEASURED 2026-08-07

**The mechanism §8.2u and §8.2v both named was wrong.** `expand_default_type_args`
does not resolve a default against the caller: it clones the context and re-homes
it on `entry.module_name` before resolving. A counter on the skipped branch
(`DefaultArgNoOwner`) reads **0** — every entry reaching it carries a valid
module. The zero has a control: the taken branch is what produces the 77
`GlobalAlloc` rows homed on `std::collections::array`, so the site is reached.

The real path is a **splice**. `passes/default_expansion.cryo` deep-clones the
template's own default annotation and writes it into the *caller's*
`scope_generic_args`; the clone keeps the template's span, which is correct,
because `GlobalAlloc` means what it means in `string.cryo`.
`resolve_generic_scope_name` then homed ONE context on the SCOPE node's span and
resolved every element of that list against it — treating a spliced declaration
as caller-written syntax.

Measured over `examples/09-json-config`: **152 of 762** scope turbofish arguments
have an annotation whose file is not the scope's file, and **every one of the 152
is `stdlib/collections/string.cryo:30`** — `type struct String<A = GlobalAlloc>`.
Nothing is synthesized: 0 of 762 arguments lack a source span. The declaration
carries the scope (§8.2r), and here the annotation's own span was already
recording it.

**Fix:** each argument resolves in the module that wrote *it*; the scope's module
answers only when an argument carries no file. Result:

| | before | after |
|---|---:|---:|
| would-be rejections | 666 | **547** |
| `GlobalAlloc` rows homed on the caller at that site | 139 | **20** |
| relocated to `std::collections::string` | — | **0** |
| (name → resolved type) over 11,495 | — | **identical** |

Zero relocation is the load-bearing check: `std::collections::string` imports
`std::alloc::allocator` and is a use site in no violation, so the rejection is
removed rather than moved. The same defect class remains at
`type_resolution.cryo:60` and two sibling contexts, which is why 20 survive; a
central fix in `TypeResolver::resolve` would catch all of them at once and is a
design change, not an implementation detail.

#### The tally array was smaller than the enum

Adding one `Site` variant made `M4 mono bare-name template scan` fall from 267 to
0, and `b1-gate` offered to re-pin it as progress. It was not progress. `bump()`
discards any index at or past `TALLY_CAP`, which stood at **64 against 71
variants** — and against **67** before this session. Seven rows were reporting a
0 they had never measured, and three of them are the `Scope*` rib-chain tier
counters §8.2m landed, blind **from the day they were added**. Measured once the
array was resized: `declaration took a slot an import held` = **9**, not 0.

The module's own docstring asserted this could not happen — "sized well above the
variant count … the bound is a property of the STORAGE rather than a
hand-maintained constant a new variant could silently outrun." The capacity was a
hand-maintained literal, and the assertion aged into a false one. A comment is a
hypothesis.

`TALLY_CAP` is now 256, and `g_tally_dropped` counts discarded bumps and prints a
loud banner above the report. That detects **exactly** the false zeros — a site
past the end that would have fired increments it, while one that never fires has
a genuine 0 either way — and nothing about it is hand-maintained, which is the
property the capacity comment claimed and did not have.

`b1-gate.py` now refuses to certify a report carrying that banner, before it
parses anything, and says explicitly not to re-pin. The blindness was detectable
from outside the compiler the whole time; nothing was watching for it.
Verified by breaking the property, not by observing green: the healthy report
still parses to B1 = 12,453 over 20 rows, and the same report with the banner
prepended exits 1.

⇒ Two rules earned here. **A gate offering to re-pin a golden downward is not
evidence the number improved**; B1's total never moved, and the row that did move
had simply stopped being recorded. And **an instrument's own zero needs the same
control as the compiler's** — the three tier counters passed four sessions of
review as a measured 0.

### 8.2x One of the fifteen unresolved names was a punctuation mark — MEASURED 2026-08-07

§8.2s counted 15 `X-failed` resolutions on `examples/09-json-config` and read
them as one defect class ("no module scope can answer a generic param"). Fourteen
are that. The fifteenth was not: its name was the single character **`(`**.

`implement trait Drop for ()` is parsed correctly — the target interns as `"()"`,
and `pass_registry` registers `"()"` → `arena.get_unit()` precisely so unit impls
"resolve through the same canonical-target path as the other primitives". But the
parser then set `current_type_name` from the target TOKEN's lexeme, and the unit
type is two tokens with only the `(` retained. `current_type_name` is what types
an `&this` receiver, so the receiver of `()`'s `drop` was annotated with the name
`(` — which names nothing and resolved to an invalid type, once, silently, on
every build of the standard library.

The name that would have worked was never tried: **0** resolutions of `()` appear
in the audit before the fix. Carrying the target's spelling instead of the token's
lexeme:

| | before | after |
|---|---:|---:|
| `X-failed` | 15 | **14** |
| resolutions of `()` | 0 | 1, answered at `3c-di-bare` |
| `lookup_by_leaf calls` | 5,042 | **5,041** |
| B3 (authoritative answers) | 69,986 | **69,987** |

The last two corroborate each other and are the whole re-pin: exactly one
resolution stopped failing through the cascade — including the leaf-index probe
it used to reach — and became an authoritative answer one step earlier. B1's
total did not move. The golden was re-pinned for that reason and no other.

Structurally nothing else changed: masking digit runs in the (name → answer)
multiset leaves **one** differing row, the intended one. The dozen rows that
differ with digits intact are arena IDs shifted by one, because a type is now
resolved that previously was not.

⇒ **A population summarised by its majority hides its minority.** Fourteen of the
fifteen were the generic-param defect, so the fifteenth inherited that label
across three sections and was never read. It cost one `awk` to separate them.

#### And the remaining fourteen are not one defect either

Read individually rather than as a count, they split two ways, and **neither half
is the "unbound-binding defect" §8.2s called the thing that stops the cascade
being provably "2c or an error"**:

- **8 at `sema/method_binding.cryo:798`** — `resolve_method_return_with_explicit_args`
  builds a **deliberately empty** context on the symbolic walk, and its own
  comment says so: "detect in a FRESH context (no bindings)". It then defers the
  whole call when an explicit arg is invalid or still abstract. A `T` that fails
  to resolve here is the **signal the probe exists to produce**, not a failure.
  Binding `T` would break the detector.
- **6 at `sema/sema.cryo:3336`** — `opaque_assoc_item_mismatch`, a diagnostic
  helper deciding whether `implement Trait<Item = X>` mismatches its initializer.
  Its context was `HomeOrigin::Unset`: a genuine scope-less site of the §8.2p
  family. But the six failures are all the abstract param `T`, and **no home
  module can answer a generic param** — a home matters only if the argument is a
  bare type name, which on this corpus it never is.

So the honest count of unresolved names attributable to missing scope is **zero**
on this target: one was punctuation, eight are a working detector, and six need
bindings rather than a namespace.

**The scope-less site was closed anyway**, because "it changes no answer here" is
not "it is correct": a bare same-leaf type name in that position reaches the
module-blind chain and can bind a type the module never imported. Homed on the
bound annotation's own module, the six rows move from `home=<none>` to
`std::fmt::display` and `std::collections::hashset`, still fail (they are abstract
params), and change no binding.

That leaves the scope-less population on this target at:

| | pre-session | now |
|---|---:|---:|
| resolutions with no home at all | 26 | **20** |
| …of those, answered by `1-generic` | 20 | **20** |
| …of those, reaching the module-blind chain | 6 | **0** |

The surviving 20 are all `method_binding.cryo:475` answered by a generic
**binding**, which consults no module scope by construction. **No resolution on
this target now reaches the fallback chain without a scope.**

> Counting that population needs `$3=="<none>"`, not `$3==""`. `rn_answer` prints
> an absent home as the literal string `<none>`, so the empty-field test matches
> nothing and reports a clean zero over the wrong population — which it did here,
> twice, before the raw row was read. The instrument says what it means; the
> query has to ask what the instrument says.

### 8.2y A bare name with nothing in scope binds by DIRECTORY ORDER — MEASURED 2026-08-08

`tests/tests/projects/resolution_leaf_index` fails on Linux at `843c8d04`
itself, and the failure is **pre-existing** — the compiler built from that commit
with no local change produces it, so it is not attributable to any edit in this
session. `make test` reads `projects: 18 passed, 1 failed`; every other gate is
as §8.2v left it.

The corpus asserts `Alpha::Widget`; the compiler binds `Omega::Widget`, and
`ResolutionLeafIndex::Alpha::Widget<i64>` vs `Omega::Widget<i32>` is an E0200
against the annotation. The `<i32>` is a **consequence, not a second defect**:
unification against `Alpha::Widget` fails on the module, so the expected type
never flows into `T` and the bare literal types it. Annotating
`Omega::Widget<i64>` and asserting `tag() == 2` passes all four tests unchanged,
which isolates it to one difference.

`CRYO_RN_AUDIT` puts that difference at one step:

| module | step | answer |
|---|---|---|
| `Alpha` (declares it) | `2c-home-syntax` | `Alpha::Widget` |
| `Omega` (declares it) | `2c-home-syntax` | `Omega::Widget` |
| `Importer` (imports `Omega`) | `2c-home-syntax` | `Omega::Widget` |
| **`Orphan` (imports nothing)** | **`5-leaf-index`** | **`Omega::Widget`** |

Every module with scope is answered correctly at 2c. Only the scope-less one
reaches the global leaf index, which is §8.2s's starvation working as described.

**The corpus's own stated mechanism is false.** Its header says "The leaf index
picks by NAME ORDER: `Alpha` wins over `Omega` here", and
`Alpha` does sort first while losing. `register_leaf_name`
(`types/arena.cryo`) is **first registration wins**, so the winner is decided by
registration order and by nothing else. Three candidate determinants were ruled
out by control, each by moving the input and re-measuring:

| input moved | result |
|---|---|
| renamed `alpha.cryo` → `aaa_alpha.cryo` (module names untouched) | winner unmoved |
| module-name sort order | `Alpha` sorts first and **loses** |
| deleted `importer.cryo`, the only `import` edge to `Omega` | winner unmoved |

A temporary instrument on `register_leaf_name`, emitted on the existing
`CRYO_LEAF_AUDIT` stream (no new `Site` variant, so no discriminant shift), gave
the order directly, and it is deterministic across runs:

```
LEAF-REG  Widget  ResolutionLeafIndex::Omega::Widget    WON
LEAF-REG  Packer  ResolutionLeafIndex::Importer::Packer WON
LEAF-REG  Packer  ResolutionLeafIndex::Orphan::Packer   lost
LEAF-REG  Widget  ResolutionLeafIndex::Alpha::Widget    lost
```

That order is **depth-first module discovery seeded by filesystem enumeration
order.** Raw `readdir` order of the copied directory is `leaf_index_test,
importer, omega, orphan, alpha`; registration is `Omega, Importer, Orphan,
Alpha`. The one transposition is `importer.cryo`'s `import
ResolutionLeafIndex::Omega`, resolved before the module that wrote it.

**The prediction that confirms it.** The repo's own directory enumerates in a
different order (`orphan, leaf_index_test, omega, alpha, importer`), which puts
`Orphan` ahead of `Importer`, so `Packer` must have a *different* winner there.
Stated before running, and measured:

| directory | `readdir` order | `Widget` | `Packer` |
|---|---|---|---|
| repo | `orphan, test, omega, alpha, importer` | `Omega` | **`Orphan`** |
| copy | `test, importer, omega, orphan, alpha` | `Omega` | **`Importer`** |

**Byte-identical source, two directories, two different global bindings.**
Copying a directory can change what a program compiles to, and nothing in the
source of either program says which answer it gets.

This is §1's root cause with its sharpest edge yet, and it supersedes the
weaker readings in §8.2s and in the corpus header. It also explains the
Windows/Linux divergence with no appeal to a compiler difference: NTFS
enumerates alphabetically, so `alpha.cryo` registers first, `Alpha` wins, and the
corpus passes — which is why the expectation was pinned that way and why it was
never observed to be host-specific.

**Do not relax the assertion.** The corpus is pinning a real defect and is doing
its job; what is wrong is its stated *mechanism*, not its expectation. Two things
are owed: the header's "name order" claim corrected to registration order, and an
expectation that does not depend on the host — which, for a program whose answer
is decided by `readdir`, can only come from the gate that makes the bare name an
error in the first place. That is the same gate §3.3 needs, so this corpus entry
is now a second reason it is the critical path rather than one.

### 8.2z The specialized-AST home split, measured on both sides — MEASURED 2026-08-08

§8.2v listed splitting `resolve_specialized_ast`'s home as owed and unsized.
Homing it on the writing file's module — `home_ns_of(ast_node.span.file)`, the
pattern two sibling sites in the same file already use — costs and pays exactly
this:

| | before | after |
|---|---:|---:|
| would-be rejections, total | 509 | **265** |
| …at `ast_resolver.cryo:90` | 245 | **1** |
| `Formatter` / `FmtError` as candidates | 122 / 124 | **0 / 2** |
| cascade `2c-home-syntax` | 7,229 | 7,106 |
| cascade **`4 arena (bootstrap-only)`** | **0** | **123** |
| B1 total | 12,453 | **12,576 (+123)** |
| (name → resolved type) over 11,475 | — | **identical** |

The written annotations are fixed: `implement Display for Option<T>` is written
in `std::fmt::display`, so its `Formatter` and `FmtError` are that file's
imports, while the template entry names `std::core::option`, which imports
neither. 244 of 245 rejections at the site were that one mistake.

**The cost is real and was predicted by the code's own comment.** All 123 new
step-4 events are mangled spec names (`6Result$Lm_N$L3std.2io.5error.7IoError$G$G`,
`7HashMap$L…`), homed on `std::fmt::display` (122) and `std::fmt::write` (1).
They are registered under the *entry's* module by `reserve_spec_names`, so a
span-derived home makes 2c miss them and the arena answers instead. **No answer
changed** — the multiset is identical — so this is a route change, not a
rebinding.

**Why `pre_resolved` does not already cover them.** `TypeResolver::resolve`
returns `n.pre_resolved` before it consults the context, and the substituter sets
it on the generic-param branch. Eight lines below that, the **base-type** rewrite
(`substituter.cryo`) writes `named.name = this.spec_name` and sets no
`pre_resolved` — so the spec name has to be re-derived by name through the
cascade, and that only worked because the home happened to be the module it was
registered under.

**Why it cannot simply be attached there.** `ASTSpecializer::specialize` (clone +
substitute) runs at `monomorphizer.cryo:429`, *before* `reserve_spec_names` at
`:506`, so at substitution time the spec name is registered nowhere and there is
no TypeRef to attach. The wrapper TypeRef does exist — it is
`request.generic_type` on the swap path — but it is not threaded into
`ASTSpecializer`, and function templates are not `is_named` and have no wrapper
at all.

**What step 4 actually is.** `arena.lookup_by_name(name)`, an **exact-name**
lookup gated on `bootstrap_mode`, whose comment says it "should never be
reached" after bootstrap. So the +123 was not a fuzzy match, but it did make 123
resolutions depend on `bootstrap_mode` still being on during monomorphization,
which is a property nothing asserts.

#### The trade dissolves: the name carries its own answer

The +123 was a symptom of the base-name rewrite, not a cost of the span home.
`ASTSpecializer::specialize` now takes the arena id the specialization will
occupy and hands it to the substituter, which attaches it as `pre_resolved` on
every annotation it rewrites to the spec name — exactly what the generic-param
branch eight lines above it already did. `request.generic_type` is that id on the
swap-relocate path, and it is the same id `reserve_spec_names` publishes, so type
identity cannot fork. The two sites that pass an empty base/spec name
(`call_specializer`, `trait_specializer`) pass `TypeRef::invalid()`; their
base-name branch cannot fire, so they are behaviour-neutral by construction.

| | HEAD | span home alone | span home + `pre_resolved` |
|---|---:|---:|---:|
| would-be rejections | 509 | 265 | **265** |
| cascade `4 arena (bootstrap-only)` | 0 | 123 | **0** |
| B1 total | 12,453 | 12,576 | **12,453** |
| `resolve_named` calls | 11,475 | 11,475 | **9,419** |
| cascade `2c` | 7,229 | 7,106 | 5,173 |

**A prediction that was wrong in magnitude, which is itself the finding.** −123
`resolve_named` calls were predicted; the measurement is **−2,056**. Only 123 of
them were reaching step 4. The other 1,933 were resolving *correctly* at 2c — by
round-tripping a mangled spec name through the scope chain and finding it because
the home happened to be the module it was registered under. They were never
wrong, only needlessly derived, and load-bearing on a coincidence: any change to
that home would have moved them to step 4 too. The 123 were the visible edge of a
population sixteen times larger.

**Controls.** No `(name → answer)` pair exists after that did not exist before —
the pairs are a strict subset — and no name's answer *set* differs, so nothing
rebound; the rows that vanish are annotations that no longer ask. `B1` returns to
its golden 12,453 with no re-pin. `make test` is unchanged from HEAD (`unit: ok;
compile-fail: 170 passed; projects: 18 passed, 1 failed`, the one failure being
§8.2y's pre-existing readdir defect). All 14 examples build. Both self-host fixed
points hold byte-identically.

⇒ **`resolve_specialized_ast` no longer resolves two kinds of name through one
context**, which is what §8.2v recorded as owed, and the site's `Syntax` label is
now honest rather than laundered. What remains at it is **1** event, not 245.

### 8.2aa An annotation TREE is not written in one place — MEASURED 2026-08-08

§8.2w recorded that a default type argument is *spliced*, not expanded: the
template's own default annotation is cloned into a use site's argument list and
keeps its own span. What that implies for resolution had not been drawn.
`TypeResolver::resolve` descends through `Pointer`/`Reference`/`Array`/`Generic`
children carrying ONE context, so a tree assembled from two files is judged
entirely at the enclosing declaration's module. `String<A = GlobalAlloc>`'s
default is then whatever `GlobalAlloc` means in the module that named `String` —
a different answer per caller, and a module that neither writes nor imports it.

**Size of the population.** A probe in the `Named` arm reporting the annotation's
own span file against the home its context carried: **6,575** named-annotation
resolutions, of which **573 (8.7%)** were homed at a module other than the one
their span names. The gate's rejections were the visible edge of it — **137** of
the 143 `Syntax`-origin would-be rejections, every one of them `GlobalAlloc`,
across four context sites (`TypeResolutionRunner::make` 57,
`compute_static_owner_bindings` 30, `try_resolve_generic_return` 29,
`resolve_generic_scope_name` 20, one more at `type_resolution.cryo:3659`).

The remaining 436 were binding correctly by coincidence: their carried home and
the annotation's real module disagreed about the *module* while agreeing about
the *answer*.

**The fix** is one helper, `resolve_named_at_span`, at the only two places that
turn an annotation into a name lookup (`resolve`'s `Named` arm and
`resolve_generic`'s `Named` base — there are no others, in this file or any
other). It takes the home from the annotation's own span and RESTORES the
caller's afterwards, because the context is shared with the rest of the tree and
a home that outlives the node it came from is the same defect one level down. An
empty answer keeps the caller's home rather than clearing it: missing provenance
read as a namespace compares unequal to every real module, the false-positive
direction that reverted the gate once (§8.1e). File → module is exact and total —
a `.cryo` file declares exactly one namespace — so the question always has an
answer where the span has a file.

**The prediction was wrong, and that is the finding.** Predicted 265 → 128;
measured **323**. The 137 did go to zero, but `TypeResolutionRunner::make` went
**57 → 196**. Re-homing had not created those; it had stopped hiding them.

`rewrite_this_type_annotation` mints the concrete annotation that replaces `This`
in a trait default cloned into an impl. It took the **name** from the impl's
target (`HashMapIter`) and the **span** from the trait's `This` token
(`std::core::iter`), and left `pre_resolved` invalid, so the name had to be
re-derived by scope. Name and provenance named different modules. While the home
came from the enclosing walk it landed on the implementing type's module — right
by accident — and homing on the span exposed every one at once: 16 pairs of the
shape `<trait module> → <implementor>::<Iter type>`.

**The control that settles it:** `stdlib/core/iter.cryo` contains **zero**
occurrences of `HashMapIter`, and `stdlib/io/traits.cryo` zero of `Stdin`, while
13 and 8 annotations respectively carried those files as their span. A file
cannot be the provenance of a name it never writes. The span, not the re-home,
was the lie.

Giving the rewritten annotation the impl's span (`target_type_span`, else the
block) is the root fix: the name written IS the impl's target, so the syntax it
stands for was written in the impl's file. Neither the trait's file nor the
target type's can stand in — an `implement` block may live in a third module
again, which is the same reason `resolver.cryo` already homes assoc-type
bindings on `impl_node.span.file`.

| | before | span-home only | both |
|---|---:|---:|---:|
| would-be rejections | 265 | 323 | **122** |
| — `Syntax` origin | 143 | 201 | **0** |
| — `Cursor` origin (mono) | 122 | 122 | 122 |
| annotations homed off their own span | 573 | — | **0** |

**Controls.** No `(name → answer)` pair exists after that did not exist before
and none was lost — the sets are identical, so nothing rebound; this changes
which module a name is JUDGED at, never which declaration it binds to. `B1`
holds at its golden 12,453 with no re-pin, and the RN row count is unchanged at
9,419, so no resolution moved down the cascade. `roster-check` OK (2001 tests),
`api-index-check` up to date, all 14 examples build, `make test` unchanged from
HEAD (`unit: ok; compile-fail: 170 passed; projects: 18 passed, 1 failed`, the
one failure being §8.2y's pre-existing readdir defect) — the 170 is the load-
bearing one, since a moved span could have moved a diagnostic golden and did not.
`find_module_by_path` is a linear case-insensitive scan and is now on the
per-annotation path; no build-time regression was measurable, but that is the
first place to look if one appears.

⇒ **Every `Syntax`-origin rejection is gone.** What the gate would now reject is
122 events at two mono sites, all of them the ambient cursor of §8.2l — one
family, not two, and the last one standing between here and turning the gate on.

### 8.2ab The mono cursor family is gone — the gate's input is CLEAN — MEASURED 2026-08-08

The last 122 would-be rejections were `call_specializer.cryo:544`
(`specialize_free_call`) and `:1302` (`specialize_static_method_on_generic_owner`),
61 each, both handing `current_ns` to `resolve_qualified_scoped_or` and honestly
declaring `Cursor`. §8.2v called this family "harder" than the six sema sites it
fixed, on the site's own comment that the caller's import scope "is not
reconstructible here". **That comment is a hypothesis, and it is false.**

Both sites hold a `ScopeResolutionNode*` — the written `Owner::member` — so the
module that wrote the scope is its span's module. `MonoCallSpecializer` already
carries `module_graph` for exactly this, and already uses `home_ns_of` at five
sites in the same file, one of them (`compute_owner_default_binds`) on the same
node type. Nothing needed threading; the span was in hand at both sites and the
door was already open.

The fix is one helper, `resolve_scope_at_span`. An invalid answer keeps the
cursor and keeps reporting `Cursor`, for §8.2v's reason: an invalid id compares
unequal to every module, so passing it as a use site reads missing provenance as
"written somewhere else" and rejects everything — the false-positive direction
that reverted the gate once (§8.1e). That policy lives in the helper rather than
at each site, on `ModuleGraph::home_ns_of_file`'s stated reasoning: a rule
spelled out per call site is a rule the next call site forgets. `origin_file` /
`origin_line` are still the CALLER's — a wrapper that named itself would collapse
both sites onto one location, and the wrapper would then read as the whole
population in any audit (§8.2u's `resolve_scoped` rule).

| | before | after |
|---|---:|---:|
| would-be rejections | 122 | **0** |
| — `cause: namespace not reachable` | 122 | **0** |
| — `cause: candidate not public` | 0 | 0 |

**Controls.** The `(name → answer)` multiset is identical — 174 distinct pairs,
same counts, nothing gained and nothing lost in either direction — so nothing
rebound. RN rows unchanged at 9,419; `b1-check` reads 12,453 against its golden
with no re-pin; the whole 493-line counter report differs in **exactly the two
rows above**, and the audit stream shrank from 10,040 lines to 9,918 — **exactly
122**, all of them `VIS-VIOLATION`. `make test` OVERALL PASS (unit 2001,
compile-fail 170, projects 19), `roster-check` OK (2001 tests),
`api-index-check` up to date, all 14 examples build.

**The zero is not the site going quiet.** `B3 by caller:
resolve_qualified_scoped` is unchanged at 58,458, and it is one of the 491 rows
the report diff leaves untouched: the same resolutions still happen and still
take the same lane. What changed is which module they are judged at.

⇒ **No would-be rejection remains on this corpus, from any origin.**

> **Do not read that as "the gate's input is clean."** It was written that way
> here first and §8.2ac corrects it with a measurement: `09-json-config` is one
> example project, and the gate would run over the whole tree. Swept properly the
> count is **4,272 events**, including **1,237 on the compiler's own build**. The
> zero above is true and is about this corpus only.

#### `resolution_leaf_index`'s axis is the FILESYSTEM, not the host

§3 of the working notes carried this as "fails on Linux, passes on Windows".
That framing sends a reader looking for OS-conditional code, and there is none.
§8.2y's mechanism is `register_leaf_name` being first-registration-wins over
**depth-first module discovery seeded by filesystem enumeration order**, and the
enumeration order is a property of the filesystem, not the OS:

| filesystem | `ls -U` on `stdlib/` | corpus |
|---|---|---|
| v9fs (`/mnt/c` from WSL) | `alloc collections core cryoconfig encoding env …` — alphabetical | **PASS** |
| ext4 (native) | `alloc fs net process math io future cryoconfig …` — hash order | **FAIL** |

Same compiler, same sources, same host, copied between the two. On ext4 it fails
as `WRONG_a_free_generic_resolves_a_plural_leaf_not_in_its_scope`, binding
`Omega::Widget<i32>` where `Alpha::Widget<i64>` was written — §8.2y's defect,
exactly. NTFS enumerates alphabetically too, which is why Windows "passes" and
why that pass is the same non-event as the v9fs one rather than a second data
point.

**Consequence for anyone measuring here: a repo checked out on `/mnt/c` and
built from WSL SILENTLY MASKS this defect.** A green `resolution_leaf_index` in
that environment is the §8.2g failure again — it says the ordering happened to
be favourable, not that the name binds correctly. Every number in §8.2ab above
was therefore taken on **both** filesystems and agrees on both; the ext4 run is
the one that proves the 122 → 0 is not itself an artifact of alphabetical
discovery order.

The host-independent expectation this corpus needs is still owed, and still
blocked on the gate (§3). It is now known to need a *filesystem*-independent
expectation, which is a strictly stronger requirement than the per-host golden
Q9 settled for `b1-check`.

### 8.2ac The gate's migration set is 106 imports, and every one is real — MEASURED 2026-08-08

> **Corrected 2026-08-08 — the re-export correction below is itself wrong.**
> `public module` grants NO visibility, so the "25 already reachable / 784
> events (37%)" split does not exist and the original "106 of 106 genuinely
> missing" reading was right for the wrong reason. §8.2ae measures this four
> ways. In particular `resolve_qualified_type_via_exports`, cited here as
> evidence, never consults `public module` — it is a segment-boundary SUFFIX
> match (`module_ns_matches_prefix`), the rule §5.1 forbids. Read §8.2ae before
> acting on anything in this section.

Every would-be-rejection figure in §8.2t through §8.2ab was taken on
`examples/09-json-config`. The gate would run over the whole tree. Swept across
all 14 examples, all 20 test projects and the compiler's own build, on both
target surfaces:

| population | events |
|---|---:|
| `examples/09-json-config` | **0** |
| `examples/11-http-server` | 343 |
| `proj:async_main` | 210 |
| **the compiler building itself** | **1,237** |
| most other projects | 2 each |
| **pooled, both surfaces** | **4,272** |

`09-json-config` is the *only* target in the sweep that reads zero. A count of 0
measured there says nothing about the population the gate would actually judge —
this is §4.6's rule, and the zero had been read the other way.

**What the 1,237 are.** 1,211 of them declare `Syntax`, i.e. they are correctly
homed at the module that wrote the name and that module still cannot reach it.
Reduced to the import each one implies — the candidate's declaring module — they
are **77 distinct (use site → declaring module) pairs** over 53 use sites for the
compiler alone, and **106 pairs over 59 use sites** pooled.

**The control said 106 of 106 are genuinely missing. THE CONTROL WAS WRONG.**
It asked only whether the use site's file already contains `import M;` or a
brace form naming the leaf. It never asked whether `M` is reachable through a
`public module` RE-EXPORT from a parent the file already imports — and that is
a real reachability path, documented at `resolver/resolver.cryo`'s
`resolve_qualified_type_via_exports`: *"`future::Ready` when only the parent
`std::future` was imported, so `Ready` entered as a re-export of the
`future::ready` submodule."*

Demonstrated false positives, not inferred: `tests/projects/async_main` already
imported `std::future` and `std::time`, and `examples/11-http-server` already
imported `std::future` and `std::net::http`, while
`stdlib/future/_module.cryo` and `stdlib/net/http/_module.cryo` re-export
`executor` / `poll` / `waker` / `timer` / `combinator` and `request` /
`response` / `router` / `server` / `status` respectively. Every import added to
those files was redundant.

**Re-derived with a re-export-following control, and attributed per project**
(the `Main` namespace is declared by several, and first-match-wins reads the
wrong file — which is what made `11-http-server`'s pairs look genuine):

| | pairs | host-surface events |
|---|---:|---:|
| already reachable — **false positives** | **25** | **784 (37%)** |
| genuinely missing | **80** | 1,352 |
| total | 105 | 2,136 |

Where the gate looked worst it was almost entirely wrong, and where it looked
quiet it was almost entirely right:

| target | events | redundant | genuine |
|---|---:|---:|---:|
| `ex:11-http-server` | 343 | **341** | 2 |
| `proj:async_main` | 210 | **207** | 3 |
| `proj:async_main_void` | 91 | **89** | 2 |
| **COMPILER-SELF** | 1,237 | 17 | **1,220** |

So the compiler's own source does carry ~1,220 genuine events over ~80 pairs,
while the example and test projects were noise from the predicate. A rejection
count is not evidence of missing imports until the predicate that produced it
understands every way a name can legally be reached.

This is Q6 read too narrowly. Q6 settles that a parent does not *implicitly*
bind its submodules.

> **FALSIFIED — see §8.2ae and Q10.** The paragraph continued: "`public module`
> is the explicit opt-in that makes the parent import sufficient … no
> `public import` feature is needed for this." `public module` grants nothing,
> measured four ways, and Q10 fixed that as the language rule. A re-export
> feature is therefore exactly what is needed, and Q11 spells it `export`.

⇒ **The gate inherits the same defect.** `DeclarationIndex::ns_imports` knows
only direct import edges, so it judges a re-exported name unreachable. That is
a false-positive class independent of the provenance one in §8.2ae, and either
one is fatal to turning the gate on.

The pairs below were read by hand and ARE genuine — a missing import with no
re-export path:

- `Compiler::BuildManifest` writes `Str::from_raw` / `Str::new` five times and
  imports `std::collections::string` — never `std::collections::str`.
- `Compiler::Parser::ExprParser` names `TypeRef` eight times and imports nothing
  that declares it.

They compile today only through §1's root cause. **So the last class is a source
migration after all** — the thing §8.2t and §8.2u each concluded was empty, on a
corpus where it is empty. It is the same shape as §8.2c's rule-6 migration and
subject to the same rule that Cryo permits import cycles, which several of these
pairs need (`Compiler::AST::Expression → Compiler::AST::Statement`).

**Both surfaces, and the control on it.** `--target=x86_64-pc-windows-gnu` from
Linux reproduces a native-Windows front end exactly — RN rows 9,439 / 12,371 /
75,047 against native Windows 9,439 / 12,371 / 75,047, each differing from the
Linux-host 9,419 / 12,319 / 74,982. The Windows surface adds **0** pairs over the
Linux one on the population both cover. Comparing the two runs' *home-module
sets* does **not** show this and reads as "the surface was never reached" — the
sets are identical because the gating is intra-file; the RN row count is the
sensitive measure. A cross-target build exits non-zero at link with no mingw
toolchain, long after the front end has emitted every event, so the audit stream
is usable while the end-of-run counter report is not.

⇒ Turning the gate on is gated on the 106 imports, not on more provenance work.
The remaining `Cursor` rows are 26 of 1,237 on the compiler build and are a
separate, much smaller question.

### 8.2ad The migration LANDED — 4,272 rejections become 10, and all 10 are the tripwire — MEASURED 2026-08-08

> **Corrected 2026-08-08 — the correction below is withdrawn.** §8.2ae measures
> that `public module` grants no visibility, so none of the 105 imports is
> redundant on that ground and the "4,272 → 10" drop is not a false positive
> being silenced. What the drop *does* overstate is enforcement: §8.2ae shows
> the gate was rejecting names that still compiled, so the counter fell without
> the program being rejected. The migration is also larger than this section's
> 105, because `tests/` was never in the swept population.

> ~~**Read with §8.2ac's correction: 25 of these 105 imports are REDUNDANT**~~, and
> they account for **784 of the 2,136 host-surface events** this section credits
> itself with clearing. The control that derived them did not follow `public
> module` re-exports. The measurements below are accurate about what the
> rejection counter did; they are not evidence that every import was needed —
> a third of the drop is a false positive being silenced rather than a defect
> being fixed. The correct order is: fix the predicate, re-measure, then migrate
> the ~80 that remain.

105 imports across 59 files: §8.2ac's 106 pairs less
`ResolutionTripwire::Orphan → ResolutionTripwire::Depot`, which is **excluded on
purpose**. `tests/orphan.cryo` states it has no import deliberately and that the
bare name resolves anyway; adding one would delete the only witness to the
defect. Attribution is per sweep target rather than per namespace, because
`Main` is not unique across projects and a namespace-keyed map edits whichever
project it happens to find first.

| population | before | after |
|---|---:|---:|
| pooled, both surfaces | 4,272 | **10** |
| the compiler building itself | 1,237 | **0** |
| `examples/11-http-server` | 343 | **0** |
| `proj:async_main` | 210 | **0** |
| every other target | 0–80 | **0** |
| `proj:resolution_tripwire` | 12 | **10** |

**All 10 survivors are one pair**, `ResolutionTripwire::Orphan →
ResolutionTripwire::Depot::Crate`, across three sites. That is the gate rejecting
exactly the thing a tripwire exists to prove is wrong, and nothing else.

**Nothing rebound, and the control is specific.** An import brings in a module's
whole export set, so the risk is shadowing some *other* leaf in the importing
module — which shows up as a changed `(name → answer)` pair, never as a changed
count. Restricted to answers that name a declaration (containing `::`): **697
before, 697 after, 0 gained, 0 lost**, on the compiler's own build. 33 of 35
targets are 0/0 outright. The compiler build's 46 delta rows are all on the
generic-parameter names `T` and `V`, whose answers are arena type ids
(`218*`, `251[]`) that renumber when the instantiation set shifts. No qualified
name changed. Builds are clean — no new ambiguity diagnostic anywhere.

**B1 did not move, and the prediction that it would was wrong.** Predicted a
drop, on the reasoning that a name reachable by import stops needing the
fallback. Measured **12,453, unchanged, golden intact, no re-pin**. The reason is
§8.2v's mechanism and it is worth stating because it separates two things this
document has been counting together: on the compiler build the fallback lanes
read `2c` 0, `2b` 0, step-5 GLOBAL LEAF INDEX **0**, while *single-candidate fast
path taken* reads **334,498**. These names never took a B1-counted fallback —
they took the fast path, which returns `Unique` before consulting the use site.

⇒ **B1 and the gate measure disjoint things.** B1 counts *which lane answered*;
the gate counts *whether the answer was reachable from the use site*. A name can
be judged at a module that cannot see it and still be answered authoritatively,
which is the whole of §1's root cause. Do not expect gate work to move B1, or
B1 work to move the gate. (Where the leaf index *was* carrying these names it did
move: `11-http-server`'s `lookup_by_leaf calls` fell 6,536 → 6,478.)

### 8.2ae `public module` grants NO visibility, and the gate was leaking — MEASURED 2026-08-08

§8.2ac corrected itself with a re-export reachability path and sized a 37%
false-positive class on it. **That path does not exist.** Four independent
measurements, each with its control:

| | with `public module` | without it |
|---|---|---|
| gate lane (two projects differing ONLY in the declaration) | E0240 | E0240 |
| multi-candidate lane (same, plus a colliding leaf) | compiles, binds via step 5 | compiles, binds via step 5 |
| real stdlib (`std::net::http` DOES re-export `request`/`response`) | 12 × E0240 | — |
| consumers of `ModuleInfo.submodules` in the tree | prelude derivation only | — |

`ModuleInfo.submodules` — the `public module` record — is read at exactly one
site, `instance.cryo`'s prelude derivation. `register_module_imports` is handed
`dependencies + imported_namespaces` and nothing else. `module_graph.cryo` says
this is deliberate and names "should `public module` also grant visibility" as an
open question, which is where it still belongs (§9).

**The cited evidence was misread.** `resolve_qualified_type_via_exports` never
consults `public module`; it calls `module_ns_matches_prefix`, a
segment-boundary **suffix match** over the global export table — the rule §5.1
forbids by name. It matches identically whether or not the re-export is written.

⇒ The original "106 of 106 genuinely missing" reading stands, though the control
that produced it was weak for the reason §8.2ac gave. **A legal alternative path
must be shown to be one the compiler TAKES, not one the source could support.**

#### The gate was not a gate

`resolver.cryo` returned `TypeRef::invalid()` on `NotReachable` and left the
diagnostic to `emit_undefined_type`, which runs only when the annotation is still
unresolved. Two suppliers dropped it on the floor:

- **The rejected name can be a generic ARGUMENT.** `extract_unresolved_named`
  peels pointer/array wrappers but never descends into arguments, so
  `Result<(), TestError>` re-asked the gate about `Result` — which is reachable —
  discarded the verdict, and fell through to E0203 on the wrong type with a
  spurious "did you mean". This is the whole of the `roster-check` failure.
- **The local-variable guard recognised only a bare `Named`.** A failed
  `Generic` annotation matched nothing, so no diagnostic was emitted at all and
  the invalid type was dropped silently.

Measured before the fix: `tests/projects/async_main` with its `std::future::*`
imports removed builds **exit 0, zero diagnostics**, while the audit records
**4 `gate-unreachable` events on `Elapsed`**. A name the gate rejected still
compiled.

⇒ **§8.2ad's "4,272 → 10" counts events, not enforcement.** The counter can fall
while the program is still accepted, so it cannot be read as "the gate is on".
It also makes "compiles clean without the import" worthless as a redundancy test,
which is how the 37% class was inferred in the first place.

Both suppliers are closed by asking one question in one place: which name written
anywhere in the annotation tree does the gate reject. Predicted and confirmed —
`roster-check`'s 7 × E0203 on `Result` became 7 × E0240 on `TestError` with the
caret on `TestError`; stripped `async_main` became E0240 on `Elapsed`. Controls:
all 14 examples build, `b1-check` reads 12,453 against its golden unmoved (B1 and
the gate measure disjoint things), and the committed projects stay green.

#### The migration is larger than 105, and the abort hides it

`tests/` — 2001 tests — was never in the swept population. It is where the
remaining work is, and it is **not** visible in one run: the compiler aborts
after the first batch of errors, so each fix surfaces the next batch. Sizing it
by iterating the diagnostic's own `add \`import M;\`` note:

| round | errors visible |
|---|---:|
| 1 | 7 (one file) |
| 2 | 44 |
| … | 12, 5, 4 … |

**45 test files edited and still converging.** Reading the first run's 7 as the
size is the §4.6 error again, with the abort as the mechanism rather than a
badly chosen corpus.

### 8.2af The gate never judged a METHOD signature — MEASURED 2026-08-10

The gate closed the annotation door for free functions only. `resolve_func_signature`
in the pass wrapper ran core resolution and then reported; **every method
population went through the core resolver directly and was never reported at
all** — impl-block methods and inline struct/union/class methods via
`Resolver::resolve_method_signatures`, trait methods via
`resolve_trait_method_signatures`. The pass-level wrappers were pure
pass-throughs.

A failed parameter annotation is not poisoned, it is simply *left unset*
(`types/resolver.cryo`'s `resolve_func_signature` assigns `param.resolved_type`
only when the lookup succeeds), and every consumer downstream reads an unset
param as `void`. So the missing diagnostic did not merely lose an error — the
parameter silently acquired a **different type than the one written**, which is
§6.1's "`Res::Err` is a value, not a failure" violated in the one place nothing
was checking.

Three outcomes, all reproduced on a 15-line project (a type behind a
`public module` edge, used as a parameter):

| the impl's param is… | what the author gets |
|---|---|
| unused, method never called | **compiles clean, exit 0** — the impl's signature disagrees with the trait's, and the method is DCE'd before anything compares them |
| unused, method called | 16 × `codegen failed for module N`, plus `E0636` and `E0619`, naming neither cause nor fix |
| used | `E0358: no method named 'get' found on type 'void'` |

The same type as a **free function** parameter gave the correct `E0240` with
`add 'import probe::inner';`. The pointer was not the variable and neither was
genericity: bare and `Ctx*` behave identically, and an all-local generic trait
impl is clean.

**Fix: one helper, called once per population at the point resolution is
FINAL.** `emit_signature_diagnostics` is extracted from the free-function
wrapper so there is a single implementation of "did this signature fail",
and is invoked from `run_type_resolution` for impl blocks and for inline
struct/union/class methods, and from `run_function_signature` for traits.
Where a population is resolved twice — an early pass runs before the owner type
is in the arena, a later one repairs what it missed — only the later call
reports, or the first pass's misses would be errors.

**The receiver must be skipped, and that is the whole false-positive class.**
`&this`/`this` carry a synthesized annotation naming the impl's *target type*
with the impl header's span, and `resolve_method_signatures` back-fills their
type from the owner — leaving it unset whenever the owner lookup returns
invalid. Reporting them produced `E0203` on `BufStream` pointing at
`implement trait AsyncRead for struct BufStream<S>`, a line whose author can do
nothing about it. `async_lower.cryo`'s `is_receiver_param` already records the
invariant: a by-value receiver "is left without a resolved type by type
resolution."

Recorded because it was believed first and was wrong: the false positives were
attributed to **async return types** being rewritten after this pass, and a
guard was added for them. Removing that guard entirely leaves all 14 examples
building — the async return branch was never the supplier. The instrument that
settled it emitted one line per rejection naming the function and parameter, and
every line read `param='&this'`. Three variable-at-a-time probes had missed it;
one line at the event did not.

Controls: all 14 examples build; each of the four populations (free function,
inline method, trait method, trait-impl method) reports **exactly one** `E0240`
for one failing site, so nothing double-reports across the two resolution
passes.

**Pinned** in `tests/tests/projects/namespace_gate_methods`, separate from
`namespace_gate` because the two are reported by different passes: a free
function's signature is judged in the FunctionSignature pass, whose abort stops
the build before the TypeResolution pass that judges methods ever runs, so one
project cannot hold both arms — the method arms would never be reached and the
project would pass while testing nothing. The compiler built with this fix
reverted **compiles that project exit 0**, which is what makes it a gate rather
than a decoration.

**What the hole was hiding, measured:** re-running the migration loop after the
fix found **6 more files needing 12 more imports** — `std::future::waker` and
`std::future::poll`, i.e. `Context*` and `Poll<T>` as trait-impl method
parameters — that the gate could not see before. Migration total across both
runs: **48 test files, 69 imports**, all pure additions.

Also surfaced, and NOT this fix's doing: `tests/tests/negative/E0459_future_moved_after_poll.cryo`
was failing to reach its own diagnostic because `mut cx: Context` needs
`import std::future::waker;` under the *local-variable* gate. Control: the
identical `E0240` reproduces with this section's change stashed. A compile-fail
test is invisible to the migration loop — its diagnostics are the expected
output, not part of the build's error stream — so this class has to be swept
separately.

### 8.2ag The last three visibility doors CLOSED — MEASURED 2026-08-10

§8.2n left three `NO` rows in §7.1 and said none of them was pinned: "these four
shapes are reproductions, not gates; nothing in the tree fails today if a door
reopens." All three are now gated, and each was pinned by a corpus entry
**before** the fix, so the pre-fix compiler builds each one at exit 0.

**The reproductions, re-measured on this tree before any edit.** Exit code, not
absent diagnostic, because a zero needs a control:

| probe | pre-fix | what executed |
|---|---|---|
| `visibility_value_gate` | **exit 17** = 5+6+3+3 | two `private` free functions taken as values, cross-module, plus two public controls |
| `visibility_static_gate` | **exit 17** = 5+6+3+3 | two `private` statics called cross-module, both spellings, plus two public controls |
| `visibility_type_mask` | **exit 8** = 7+1+0 | a `private` type named cross-module while an unrelated module's public same-leaf type masked the error |

The type probe's A/B is the control that the gate was reached at all: renaming
*only* the masking `Decoy::Hidden`, with the use site's scope held identical,
turns the same program into two `E0503`s.

**Doors 5 and 6 were lanes, not branches.** `enforce_callee_visibility` is
reached only from call paths and `enforce_method_visibility` only from
member-access paths, so a function that is referenced without being called and a
static method reach neither. Each now asks for itself:

- `enforce_value_ref_visibility` gates a bare name in value position, resolving
  the leaf against the module that WROTE it before judging, because
  `namespace_of` on an unqualified leaf has no owner to compare;
- `lookup_scope_value_function` gates a qualified path in value position, and
  resolves it through `resolve_module_qualified_symbol` — the *same* symbol the
  call form of that path resolves through, so `Ns::Mod::f` and `Ns::Mod::f()`
  cannot disagree about which function they name;
- `enforce_static_method_visibility` is called from `pin_scope_callee_combined`,
  which is where all four static-bind paths funnel, and *before* its early
  returns: a call whose callee is already pinned is still a call. An owner that
  names a module rather than a type resolves to no type and falls out without a
  verdict, so module-qualified free functions keep their own door.

`enforce_method_visibility` gained a `door` parameter for the same reason the
function lane has one: shutting one lane is indistinguishable from shutting both
unless each is named at the rejection.

**Sema did not own the qualified value lane at all, and that is why door 5 was
unasked.** `resolve_scope_resolution` tried types, then globals, then returned
invalid with no diagnostic; codegen's `ScopeResolutionNode` visit resolved the
function by itself. So `Ns::Vault::nosuchfn` in value position — naming a
function that **does not exist** — compiled, linked, and **segfaulted** on a null
function pointer, and the dead-code lint reported the referenced function as
never used. Sema now asks for the function and, having asked every lane codegen
can emit (type, global, enum variant, function), reports the miss as `E0201` in
the wording `resolve_identifier` already uses for a bare unknown value.

**Hole 3 was two leaf-keyed decisions, and both are gone.**
`check_type_name_visibility` stripped the written path to its leaf before asking,
and `ModuleTypeRegistry::private_owner_module` answered with a program-wide scan
that short-circuited to "allowed" on the first public same-leaf entry — so the
question degraded to "is any `Hidden` anywhere public?". It now asks
`is_candidate_public` about the **resolved qualified symbol**, with the use site
from `module_ns_sym_of_file(span.file)` rather than the ambient cursor. That is
the same shape that closed the function lane in §8.1f, and it retires the leaf
key and the cursor together. `private_owner_module` had exactly one caller and is
**deleted**, not deprecated (§7.2 mechanism 2) — its doc comment asserted the
masking as intent ("a name that is private in A but public in B is legal"), which
§3.3 makes a defect, and a comment left in place is how the behaviour comes back.

**Controls.** Every project carries a public sibling asserted ABSENT from the
output, because the failure mode that reverted this gate once (§8.1e) is the
false positive. Beyond those: all 14 examples build; the projects suite goes
26 → 29 passing with the same single failure (`resolution_leaf_index`, §2's
pinned filesystem-order defect); compile-fail stays 170/0; `b1-check` reads
**12,453 across 18 sites, unchanged** — predicted before the edit, and the
prediction is what makes it evidence: routing the value lane through M2 added no
new fallback answers on the b1 target.

### 8.2ah M2 is DELETED, and a trait default was never name-resolved — MEASURED 2026-08-11

§8.2's inventory row `M2 resolve_module_qualified_symbol` no longer exists. The
graph scan under it — match every namespace carrying the written suffix, break
ties by arity, then by longest shared prefix with the ambient cursor, then by
enumeration order — is **deleted, not deprecated**, and `ns_shared_prefix_len`,
its only tie-break helper, went with it. `resolve_module_qualified_symbol` now
reads the `Res` that NameResolution stamped and returns the empty symbol when
there is none. There is deliberately no second strategy: the empty answer
becomes an unresolved-path diagnostic, which is the honest result for a question
this stage no longer has the inputs to answer.

**What held the lane open was not synthesized nodes.** The residual was
predicted to be nodes built after NameResolution runs. It was not. Classifying
each remaining scan answer at the answer itself — `NO-NODE` (no syntax passed),
`NO-PROVENANCE` (a span naming no module), `UNSTAMPED` (provenance fine, never
stamped) — put all 88 in the third bucket, with a real file behind every one:

| writer | written | scan answered | enclosing construct |
|---|---|---|---|
| `std::core::hash` | `mem::transmute` | `std::core::mem::transmute` | `Hasher::fold` default |
| `std::random::source` | `core::panic` | `std::core::panic` | `RandomSource::next_below` default |
| `std::random::secure` | `mem::offset` | `std::core::mem::offset` | `RandomSource::fill` default |
| `std::alloc::allocator` | `intrinsics::memcpy` | `std::core::intrinsics::memcpy` | `Allocator::reallocate` default |

`visit(TraitDeclNode*)` declared method signatures and stopped. Struct, union,
class and impl visitors all walk their method bodies; the trait visitor had no
equivalent, so **no node inside any trait default body was ever name-resolved**.
The control that made this readable rather than inferred: `mem` is stampable —
56 stamps from six other modules on the same run, `std::random::secure` among
them — while `std::core::hash` and `std::random::source` produced **zero stamps
of any kind**.

The row in the middle of that table is the whole mechanism in one line: the
ambient cursor said `std::random::secure` while the syntax lived in
`source.cryo`. A default body is written in the trait's file and instantiated
against each implementing type, so the cursor names a module that did not write
the path, and every existing instrument reads green through it.

**Pinned before the fix.** `resolution_trait_default` is a running program: two
traits in two files, both defaults spelling `Text::tag()`, importing opposite
parents, implemented by one `Widget` that imports neither. Exit `5+6 = 11`.

| | exit |
|---|---|
| pre-fix (`bin/cryo`) | **10** = 5+5 — both defaults bound to `Alpha` |
| post-fix | **11** |

10 is both bound to Alpha, 12 is both bound to Beta, and 12 is what any resolver
answering from one global scan order must produce: it has a single answer to
give and the two files need different ones. That is what makes the exit code
independent of the enumeration order this host does not control, unlike §2's
`resolution_leaf_index`.

**Q5 was in the spec and not in the code.** Deleting the scan broke
`generic_name_collision`, which writes `GenericNameCollision::Alpha::pick<i32>`
without importing `Alpha`. Q5 ("a package root IS implicitly addressable; a
fully-qualified path always works") makes that legal, and `stamp_module_scope`'s
reachability relation was missing the clause — own namespace, prelude and
imports only. Added as `cand_str.eq(written)`: the WHOLE namespace, so an
abbreviation like `core::option` stays a proper suffix rooted at an interior
segment and remains unreachable, exactly as Q5 says.

**Numbers**, `examples/09-json-config`, `--no-incremental`, threads=1:

| row | before | after |
|---|---:|---:|
| M2 calls | 2,258 | 2,258 |
| M2 answered from the stamp | 1,976 | **2,064** |
| M2 scan hits | 88 | **row deleted** |
| B1 total | 10,477 | **10,389** |
| M1 calls / hits | 6,431 / 6,431 | 6,431 / 6,431 |

The −88 is the trait-body fix; the deletion that followed contributes 0, because
its hits were already 0 when it was removed. Both were predicted before the
edit.

**Two population holes this measurement had to close first.**

The three refusal counters (`module NOT visible from writer`, `two visible
modules match`, `stamped module lacks the member`) print only in the end-of-run
tally, which prints only after a successful link — so the compile_fail corpus,
the population most likely to name a module its writer cannot see, contributed
nothing and its blank row read as a zero. Emitting at the refusal instead
(`SCOPE-RES-REFUSED`, with `SCOPE-RES-STAMPED` as the control, the same pairing
`vis_record` gives `vis_violation`) put 44 of 46 projects on the record: all
three counters **0**, against 8,799 stamps. The two that report nothing are
`reexport_glob_rejected`, which aborts at parse, and `vendor_raylib`, which has
no external dependency present — both never reach the lane, and neither is a
silent blind spot.

The second hole is not fixed and is worth naming: **`cryo build` and `cryo test`
compile different source sets.** A sweep over every `cryoconfig` project reported
0 residual everywhere while two failures were waiting in files that only
`cryo test` compiles — a project's own `tests/` subdirectory, and the unit-test
tree. A corpus sweep that builds is not a corpus sweep that tests.

**Migration.** One source edit: `tests/tests/lang/impl_trait_iter.cryo` wrote
`mem::offset` while importing no `mem`, and had been binding to a module it
never imported. It now says `import std::core::mem;` like every other test that
uses it.

**Both refusals now report, and NO new error code was needed.** With the scan
gone each refusal already failed the build, but on the generic unresolved-path
message. They now name their own cause:

| refusal | code | pinned by |
|---|---|---|
| a module carries the spelling, the writer cannot reach it | **E0240** | `resolution_unreachable_module` |
| two reachable modules carry it | **E0154** | `resolution_ambiguous_module` |

E0240 is `NAMESPACE_NOT_REACHABLE` — "a name that EXISTS but is not reachable
from the module that wrote it" — which is this case exactly, so minting a code
would have produced a near-duplicate of one already carrying the meaning. The
E0240 arm suggests the import that fixes it, naming the whole namespace, since
what makes the path unreachable is that its first segment binds to nothing.

Both are gated on the segment binding to **nothing in scope**, using the lookup
`visit(ScopeResolutionNode*)` has already performed. Without that guard a
segment naming a TYPE would be judged against the module graph, and a type
sharing its name with any unreachable module anywhere in the program would be
rejected on the strength of a coincidence. The counters read 0 across the
corpus, so the two new projects are the only things that make either arm fire —
a gate nothing exercises is indistinguishable from one that passes.

Each project carries a control (`Only::ping`, a uniquely-suffixed module that
IS imported) asserted absent from the output. The control lives in its own
function several lines from the rejection, and the assertion excludes the
control's *diagnostic* rather than its source token: the renderer echoes the
source around an error, so a token on the failing line appears in the output
whatever the compiler decided about it, and cannot be asserted absent. The
first version of both projects failed for exactly that reason.

### 8.2ai M1 attributed per caller: a validator that never rejects and a search that never informs — MEASURED 2026-08-11

M1 was 62% of B1 and had never been attributed. One row read **6,431 calls /
6,431 hits — a predicate that had never once rejected**, which is the shape of
something counted in the wrong bucket. It was one counted wrapper over two
callers asking opposite questions of the same predicate, so the row could not
distinguish a search that always answers from a validator that never refuses.
The wrapper is gone; each caller now counts itself, and `ns_written_as` — the
predicate both share, so the two can never drift about what a spelling may
name — is the only thing left.

| site | question | 09-json-config |
|---|---|---:|
| `qualifier_agrees` | **CHECK**: does the answer already chosen agree with the qualifier the source wrote? | 4,804 |
| export-table scan | **SEARCH**: which module could this spelling mean? | 1,627 |

B1's total did not move (10,389, +0): no answer changed hands, only its
attribution. Both sums were predicted before the edit, along with the constraint
that binds them — the scan is entered only when the check refuses or is not
asked, so `reject + leaf-miss` must be at least the scan's answer count. It is:
0 + 2,563 ≥ 1,627.

**Neither site produces information.** Two counters decide this, and both are
new: whether the scan's answer differs from the string it was handed, and
whether the check ever says no.

```
M1 CHECK qualifier_agrees calls    4804
M1 CHECK qualifier_agrees agreed   4804
  CHECK rejected the leaf answer      0
  leaf unresolved, CHECK not asked 2563
M1 SEARCH export-table scan calls  1627
M1 SEARCH export-table scan hits   1627
  of those, answer != input           0
```

A scan hit equal to its input is one no caller could tell from the scan not
existing, because the fall-through returns the input unchanged. Pooled over
every population reachable from a Linux host — 43 example and test projects,
the unit-test tree with all 170 compile_fail negatives, and the compiler's own
163 modules — the scan answered **91,587 times and was novel once**, and the
check **refused once**. Both are the same name in the same module:

```
M1-REJECT  CryoTests::Tests::Stdlib::FutureExecutor
           future::executor::JoinHandle   vs decl  std::thread
M1-EXPORT  CryoTests::Tests::Stdlib::FutureExecutor
           future::executor::JoinHandle -> std::future::executor::JoinHandle
```

That is the construction the scan was written for, and the pair is the mechanism
in two lines: `future_executor.cryo` imports `std::thread` and `std::future`
deliberately, both export a `JoinHandle` leaf, so the leaf-keyed scope chain
lands on `std::thread::JoinHandle`, the check refuses it against the written
`future::executor`, and the scan recovers the right one by prefix.

**And the corpus still cannot tell whether the scan exists.** Stubbed to return
nothing, `make test` reports unit ok, compile-fail 170 passed, projects 33
passed / 1 failed, and `make examples` builds all 14 — identical to the run
with it in, including `spawn_returns_the_executors_own_join_handle`, the one
test whose one answer it supplies. So the refused path reaches a correct binding
by some *other* route, which makes the scan the redundant middle strategy of
three answering one question, not the answer to it.

**Which route is not yet known, and the obvious suspect is ruled out.** Building
the unit tree both ways, threads=1, `--no-incremental`:

| row | scan present | scan stubbed |
|---|---:|---:|
| `lookup_by_leaf` hits | 14,905 | 14,905 |
| — by caller: sema `type_utils` | 14,905 | 14,905 |
| CHECK rejected | 1 | 4 |
| SEARCH hits / of those novel | 12,157 / 1 | 0 / 0 |
| B1 total | 84,738 | 72,581 |

The global leaf index absorbs **none** of it — the natural fear, since a leaf
answer is the one that binds by directory enumeration order and would make the
suite's verdict a property of the filesystem rather than the program. B1 falls
by exactly the scan's hits and nothing else moves, so the work is not handed to
another counted fallback. It is handed to an **uncounted** one: the fall-through
returns the abbreviated string `future::executor::JoinHandle` and some later
lookup accepts it, which means a suffix-abbreviated key resolves somewhere that
no counter watches. Deleting the scan before that path is named would move a
question out of sight rather than answer it. The rejection count rising 1 → 4 is
the same effect from the other side: with nothing short-circuiting it, the
annotation is re-refused on every later pass.

**A zero measured over the wrong population, again.** The first sweep put the
scan at 73,420 answers and 0 novel and read as proof it was dead. It was driven
per project directory, which never compiles the unit-test tree — where the only
instance lives. The suite-wide sweep that found it went through the test runner,
which swallows each project's stderr, so the event stream it captured was 3% of
the events. Neither sweep alone spans the corpus; the union does. A count of
zero says nothing until the population it was taken over is stated.

**What this makes M1.** 6,430 of 6,431 answers on the gate target confirm a name
that already named itself: 4,798 of the check's agreements are literally
`written == resolved`, and 1,627 of the scan's hits reproduce their input. The
six non-identity agreements (`libc::Whence` → `std::ffi::libc::Whence`) are
leading-segment abbreviations, and the expansion is the *leaf* resolver's work —
M1 only checked it. So M1 is not a resolution strategy that happens to be
right; it is bookkeeping around one, plus a recovery path for a single shape.

**That shape is a `NamedAnnotation`.** `mut h: future::executor::JoinHandle<i64>`
is an annotation carrying a qualified path, resolved with no provenance beyond
the leaf and the ambient cursor — the same inputs M2 was answering from before
its question moved earlier. Its module produced exactly one module-scope stamp
on the whole run (`thread` → `std::thread`); the annotation's own path produced
none, so it does not travel the `Res` lane at all. Stamping the node is the M2
treatment applied to the one case M1 exists to catch, and it is the measured
reason to take the `NamedAnnotation` row of §6.3 next.

**The check no longer counts toward B1.** `qualifier_agrees` produces no answer:
its `true` lets stand a binding `resolve_type_qualified_name_bare_from` already
made, so summing it credits the fallback bucket with another lookup's work and
counts that binding twice. B1 on the gate target is **5,585**, and the per-site
rows did not move — the drift report shows `-4,804` with no row added, removed
or changed, which is what distinguishes a summation fix from a behaviour change.
The row stays flagged and asserted: a validator that starts agreeing with
something it used to refuse is a resolution change and the golden must still
catch it. This is the same rule that already excludes cascade step 5 and every
`calls` row — B1 counts answers produced, and a check produces none.

**Where the refused path actually binds, narrowed but not closed.** The
annotation lane is `TypeResolver::canonical_type_name`, which asks
`resolve_type_qualified_name_from` a second time — from the annotation's HOME
scope rather than the ambient cursor — and then falls back to the ambient one,
so the two-strategy shape repeats one level up. The home attempt refuses for the
same reason the cursor attempt does, and the fall-through hands back the
abbreviated `future::executor::JoinHandle` as though it were canonical, because
a non-empty `SymbolStr` reads as valid. `TypeResolver::resolve_named_type`'s own
chain then answers from a step no counter watches: not `lookup_by_leaf` (Δ 0
across the stub), not the module-scope stamp (none exists), not `type_utils`'s
cross-module branch (the abbreviated name never reaches it). Instrumenting that
chain per outcome, the way `resolve_method_call` was, is what has to happen
before the export scan can be deleted rather than merely stubbed — otherwise the
deletion moves the question somewhere with no counter on it.

### 8.3 B2, enumerated — MEASURED 2026-08-09

B2 was previously the assoc-type projection alone, and §7.3's "enumerated and
justified" target was not assessable. `resolve_method_call` — the central
dispatcher — is now instrumented one site per OUTCOME, the same shape as the
`resolve_named` cascade, because a total cannot separate a receiver-typed lookup
from a retry after one missed.

**It is a fourteen-outcome cascade**, structurally the same object as the
nine-step one, and it was carrying a reported B2 of 50.

| outcome | 09-json-config | compiler self |
|---|---:|---:|
| m1 generic-owner receiver, own template | 196 | 196 |
| m1b generic-owner receiver, via template | 378 | 378 |
| m2 abstract receiver, via bounds | 1,011 | 1,703 |
| *m3\* `no type sym` branch ENTERED* | *150* | *1,361* |
| m3 no type sym: BoundedParam bound | **0** | **0** |
| m3b no type sym: projection bound | **0** | **0** |
| m4 no type sym: generic method return | **0** | **0** |
| m4b no type sym: via template | **0** | **0** |
| m5 by name + inheritance (concrete) | 2,784 | **52,310** |
| m6 BoundedParam bound, after m5 missed | **0** | **0** |
| m7 through trait impls | **0** | **0** |
| m8 field holding a function | 3 | 4 |
| m9 no-op drop on a Copy type | 1 | 255 |
| m10 deref coercion | **0** | **0** |
| unresolved | 608 | 1,839 |
| **calls (total)** | **4,981** | **56,685** |

**The rows account for exactly 100% of the calls** on both populations
(54,846 + 1,839 = 56,685), so no outcome is unattributed. `b1-check` reads
12,453 against its golden, unmoved — counters only.

Three results, each with its control:

**m5 is 92% of B2 and is justified.** A concrete receiver, looked up by name.
This is §6.2's case exactly: it cannot move into the resolver because it needs
the receiver's type. B2 stays nonzero because of this row, as §7.3 predicted.

**The `no type sym` branch is entered 1,361 times and answers ZERO.** All four
of its rescue outcomes are 0 on both populations while the branch itself is
entered — which is why the entry is counted separately; the outcome rows alone
cannot distinguish "never reached" from "reached and never answers". Those
1,361 entries are 74% of all 1,839 failures. **Four of the fourteen outcomes are
pure delay before a failure and are deletable**, which is the first concrete
answer §7.3's "justify or delete" has had.

**m1/m1b is a fallback chain inside B2.** m1b runs only when m1 returned
invalid — `if (a) { … } else { try_another_way() }` for the same question — and
the second attempt answers **378** against the first's **196**, so the fallback
wins 2:1. Both numbers are identical across the two populations, which places
the whole population in the stdlib rather than in either project.

**m6, m7 and m10 read 0 on both populations but are NOT dead.** A probe that
forces deref coercion (`Box<Widget>` receiver, method on `Widget`) makes m10
read 1, so the counter is placed correctly and the zero means *unexercised*.
Deleting them needs corpus coverage first, not a rejection count — the §8.2ac
rule, applied to the other direction.

⇒ **Still a FLOOR, for one named reason.** Overload selection
(`resolve_method_overload`) and member access outside a call are not counted, so
a method reached by those routes is missing from every number above.

### 8.3a Two dead lanes DELETED — MEASURED 2026-08-09

The population that justified each deletion is 35 projects (all examples, all
test projects) pooled, plus the compiler building itself. Both deletions were
predicted to move nothing and moved nothing.

**The method cascade is 14 outcomes → 10.** The `no type sym` branch's four
rescues are gone. Every receiver shape they could answer for is answered by the
generic-owner and abstract-receiver arms above them, so arriving here means
those arms declined, and re-asking the same sources with strictly less
information cannot change that. Control: `unresolved` **1,839 → 1,839**, branch
entries **1,361 → 1,361**, every surviving outcome identical, and the pooled
35-project table byte-identical across all nine rows. An untyped receiver is now
a failure, not a search.

**M3 `collect_namespace_suffix_matches` is gone** — a namespace-SUFFIX walk in
codegen, which §5.1 forbids and §7.2 mechanism 2 requires be deleted rather than
hidden. It bumped `M3Hits` once per match pushed and read **0 hits over 13,567
calls**, so it returned an empty array every time and both of its consumer
branches — including an `E0154` ambiguity report — were unreachable. It sat
directly above a comment reading "Fallback 1". Control: the compiler still
self-hosts, all 14 examples build, and the pooled table is unchanged.

`b1-check` re-pinned for host **linux only**: total **12,453 → 12,453 (+0)**,
18 sites where there were 20. The bound is not relaxed — M3's hits were already
zero, so its removal cannot lower the total. **The `[host:windows]` section
still carries the two M3 rows and will fail until it is re-pinned there.**

#### m1/m1b is a real fallback chain, and its call-site comment is false

Not deleted, because it needs a decision rather than a measurement. The comment
justifying the second lookup says a trait-impl-delivered method "is not in the
template's own method list, so the owner-template lookup above cannot see it"
and that the second reads the declaration index instead. **Both read the same
source**: `generic_registry.get_template_by_type_id(...)` then `tmpl.ast_node`'s
`methods` array, with the same struct/union/class dispatch.

They differ in three ways, and neither is a superset:

- m1b returns `TypeRef::invalid()` for a method with generic params; m1 answers.
- m1b, on finding nothing, consults the DI and then trait impls; m1 does not.
- m1b substitutes the receiver into the return
  (`subst_method_return_from_receiver` + `subst_this_in_type`); **m1 returns the
  raw `resolved_return_type`.**

So the first path answers 5,747 times with a less-substituted type purely by
running first, and the second answers 11,786 — the fallback wins 2:1. Making
this one question with one answering path is a semantic choice about which
return type is correct for a generic-owner receiver, and it changes what
existing programs compile to.

### 8.4 `export` LANDED, and the one door it does not open — MEASURED 2026-08-10

Q11 is implemented. `TokenType::KwExport` was already lexed with no production
consuming it, so this is a parser and resolver change and no existing source
changed meaning.

**Where the answer lives.** An `export` is recorded by the module loader as a
namespace on `ModuleInfo.reexports` — a VISIBILITY edge, never a dependency
edge — and `instance.cryo` closes over it when it builds each module's visible
set for `register_module_imports`. `DeclarationIndex::ns_imports` then answers
re-exported names with no new lookup path, which is why the gate, the E0240
note and the ambiguity rules all inherited the feature without being told about
it. The closure uses `imi_visible` as both worklist and visited set, so mutual
re-export terminates; it cannot terminate on acyclicity, because the edge rule
deliberately permits the cycle.

`NameResolver::process_import` is not branched on `is_export`. An export grants
the exporting module what it grants importers, so binding it locally is half
the rule rather than a side effect, and one production means the two forms
cannot drift.

**The parser rejects `export M::*`.** Excluded from the grammar rather than
diagnosed downstream, so no later stage has to decide what it means.

**E0241** reports an export that names a `private` or nonexistent item, at the
export site. It runs once after the LAST module's type resolution, not inside
the per-module pass: `is_candidate_public` answers "public" for anything not
yet recorded, so asked mid-stage the same export would be accepted or rejected
depending on where its target landed in topological order — §8.2y's defect
relocated into a diagnostic.

**E0240's fix note now names the shortest import that would actually work**,
tie-broken by length then byte order on the path. Two behaviours this
corrected: it no longer suggests an import for a `private` declaration, where
no import helps and the old note sent the author to edit a file that could not
fix it; and where an aggregate re-exports the declarer, it names the aggregate
instead of sending them one level deeper. The provenance note still names the
declarer — Q8's two halves answer different questions.

#### The hole: a bare generic static call does not see a re-export

`Box<i32>::new(...)` fails with `E0233` on a type reachable only through an
`export`, while `Box<i32>` as an ANNOTATION on the same line resolves. Reduced
to a three-module project: declaring module, re-exporting facade, use site.

Four measurements bound it, and the first three are the controls that make the
fourth mean something:

| spelling | result |
|---|---|
| `const b: Box<i32> = Box<i32> { v: 7 };` (annotation + literal) | resolves |
| `Plain::new(7)` — static method, NON-generic owner | resolves |
| `StatM::Store::Box<i32>::new(7)` — fully qualified | resolves |
| `Box<i32>::new(7)` — bare, generic owner | **E0233** |

`CRYO_VIS_AUDIT=1` emits **no** `VIS-VIOLATION` for the failing name, so the
scoped resolver is not rejecting it: the name resolves and the SPECIALIZATION
is never found. The supplier is `call_resolver.cryo`'s `lookup_scope_template`,
a four-step chain whose second step asks `resolve_scoped_or` — the ambient
cursor, with no span — and whose remaining steps try `qualify_symbol_sym` and a
cross-module scan. None of them is the use site's reachable set.

Recorded because it was tried and was wrong: passing the already-scope-resolved
name into `resolve_generic_scope_name` instead of the bare leaf does **not**
fix it. That was one hypothesis, refuted by rebuilding; the audit above is what
located the real supplier. The fix is not a fifth step in that chain — it is
§6's `Res` stamped on the node, which is what lets this door ask the same
question as every other.

**Cost, measured rather than estimated.** Of the 91 deep imports that
`std::future`, `std::net::http` and `std::test`'s new `export` lines make
redundant, **90 could be dropped and exactly 1 could not**:
`tests/tests/lang/async_generic_function.cryo` needs `import
std::future::ready;` back for `Ready<T>::new`. That import carries a comment
naming the mechanism, and it is the marker for this section — when the keystone
lands, that import is the thing to delete to prove it.

---

## 9. Open questions

- **Q1** — Does enforcing §3.3 require per-item `public` on declarations that
  are currently bare, at a scale that makes the migration mechanical? Answered
  by §8.1's measurement.
- **Q2** — Does the FFI binding namespace (§2.9) become an ordinary module
  under §3.2, or does it need a distinct scope kind? It must get a principled
  home; a special case here regenerates heuristics.
- **Q3** — What is the diagnostic for a cross-package cycle, and its error
  code?
- **Q4** — Does `std::Range` survive? It works today only via the mechanism in
  §1. Either `stdlib/lib.cryo` re-exports it explicitly or it becomes an error
  with a suggestion — decided per name, deliberately. `export` now exists to
  spell the first option (§8.4).
- **Q12** — A bare generic static call (`Box<i32>::new`) does not see a
  re-export, while the same type resolves as an annotation (§8.4). This is not
  a question about what the language should mean — that is settled — but about
  whether the keystone (§6) is allowed to subsume `lookup_scope_template`'s
  four-step chain wholesale, since collapsing it changes which template a bare
  same-leaf name binds to and therefore what existing programs compile to.
### Decided 2026-08-10

- **Q11 — symbol re-export is spelled `export`, and it is a declaration in the
  module that re-exports.** IMPLEMENTED — see §8.4, including the one door it
  does not open.

  ```ebnf
  Export      ::= "export" ExportForm ";"
  ExportForm  ::= ModulePath "::" "{" Ident ("," Ident)* "}"
                | ModulePath "as" Ident
                | ModulePath
  ```

  ```cryo
  namespace std::future;

  public module future::waker;                  // structure, per Q10
  export future::waker::{Context, Waker};       // re-export, this decision
  export future::poll::{Poll};
  ```

  …after which `import std::future;` alone puts `Context`, `Waker` and `Poll`
  in scope.

  **The rule, entire: an `export` grants an importer of THIS module exactly
  what the same path would grant if it were imported here.** So
  `export M::{A};` gives importers bare `A`; `export M;` gives them the name
  `M`, hence `M::A`; `export M as N;` gives them `N`. There is no second
  mechanism and no new path syntax — `ExportForm` is `ImportForm` minus one
  case.

  **`*` is deliberately not in `ExportForm`.** A glob re-export is the one
  form under which an importer's in-scope set changes when a *child* module
  gains a declaration, with the new name written down at neither end — §1's
  ambient namespace re-created one level down, inside precisely the modules
  that aggregate most (`std::future` has 9 submodules, `std::net::http` 9,
  `std::test` 6). `M::*` remains legal as a LOCAL import, where the blast
  radius is one file. Cost of the restriction, measured: the glob import form
  has **zero** users across stdlib, compiler, tests and examples.

  Why `export` rather than `public import`: **it is already a reserved keyword
  and already lexed** (`TokenType::KwExport`, `lex/_module.cryo:75` and `:863`)
  with no production consuming it. The feature therefore costs no keyword, no
  lexer change, and breaks no existing source; it is a parser and resolver
  change. `public import` was the other candidate and is the name the older
  parts of this document use for the *concept* — read those as this feature.

  **It chains, and every hop is explicit.** An `export` may name anything in
  scope in the exporting module, including a name that module itself obtained
  via an `export` — so `std` can write `export future::{Context};` because
  `std::future` exports it. What it cannot do is propagate on its own: a level
  that writes no `export` re-exports nothing, so no name arrives anywhere by
  accident. This is what keeps a module's internal layout its own — moving
  `Context` from `future::waker` to `future::poll` is invisible to `std`,
  which names only `future`. The rejected alternative was to require every
  re-export to name the DECLARING module, which reads as more explicit and is
  in fact the opposite: it publishes every submodule's layout to every
  ancestor and breaks all of them on any move.

  Two consequences that follow and are not optional:

  - **An `export` cannot widen visibility.** Re-exporting an item never grants
    more than the item's own visibility allows; a `private` declaration stays
    unreachable through any number of hops. Q7 makes public the default, so
    this bites only where `private` is spelled.
  - **Resolution through re-exports needs a visited set.** Mutual re-export
    between two modules is *legal* under the edge rule below, so the walk
    terminates on a seen-module check, not on the graph being acyclic.

  **`E0240` suggests the shortest import that would actually work, and keeps
  naming the declarer separately.** The two halves of Q8's help answer
  different questions and both survive: `` `X` is declared in `A::B` ``
  is provenance, `add import A;` is the fix. The suggestion is computed
  against what the file ALREADY imports — if the aggregate is imported and
  does not re-export the name, suggesting it again fixes nothing, and the note
  must fall back to the declaring module.

  **The tie-break must be deterministic.** Where two aggregates would equally
  make a name reachable, the choice is by a stable total order on the path,
  never by discovery order. §8.2y is the standing example of what happens
  otherwise: a bare name already binds by directory enumeration, which is why
  `resolution_leaf_index` passes on one filesystem and fails on another. A
  suggestion that varies by enumeration order would be a second instance of
  that defect, in the diagnostic instead of the binding.

  **The edge is a VISIBILITY edge and never a build-order edge.** An `export`
  changes what names are reachable and contributes nothing to compilation
  order. `module_graph.cryo` already warns that adding a dependency edge per
  re-exported item "recreates the false cycles the module loader avoids for
  `public module` declarations", and that is the same `E0501` trap `public
  module` hit. Under this rule two modules may re-export from each other
  without either becoming unsatisfiable. The requirement it places on the
  implementation: `DeclarationIndex` must answer a re-exported name without
  the exporting module having been compiled first — which is how the index
  already works, since it is populated at declaration collection.

  The rejected middle option — a dependency edge *only where it would not
  close a cycle* — is one question with two answering paths, the defect class
  this document exists to remove.

  `docs/grammar.md` carries the `Export` production as of the change that
  implemented it, and `docs/cryo.md` §14.5 documents the feature. A normative
  production the compiler rejects would make the compiler retroactively the
  defect, so the two land together or not at all.

- **Q10 — `public module X;` grants NO visibility. It is module structure, and
  nothing more.** It suppresses a build-order edge, triggers discovery, and
  feeds the prelude derivation; it binds neither the child's name nor the
  child's symbols at any importer. A name is reachable only through the rib
  chain §4 describes, so §4 stays literally true and the gate is correct as
  written: `import std::test;` does not reach `std::test::error`, and
  `Result<(), TestError>` there is `E0240` until the importer says
  `import std::test::error;`.

  Symbol re-export is a separate feature, deliberately not folded into this
  keyword. The reason it cannot be folded in: an importer of an aggregator
  would acquire the union of its children's export sets — `std::future`
  re-exports 9 submodules, `std::net::http` 9, `std::test` 6 — which is the
  ambient-namespace root cause of §1 re-created one level down, inside exactly
  the modules that aggregate the most. A keyword that means "structure" cannot
  also mean "bring these names along" without reintroducing the condition the
  rib chain exists to remove.

  The two rejected answers and why the choice was nearly free: the gate judges
  **bare leaves** by construction (`resolve_qualified_scoped` takes a bare
  name) and every rejection measured is a bare use, so "grants nothing" and
  "grants the module NAME" imply the *same* migration. Only "grants the
  child's SYMBOLS" would have shrunk it, and that is `pub use` wearing
  `pub mod`'s spelling.

  Migration cost, measured rather than estimated: **197 of 222** `TestError`
  files under `tests/` already carried `import std::test::error;` before this
  decision, and the 25 that did not were all under `tests/tests/lang/`. Those
  197 imports are valid under all three answers, so no answer here invalidated
  work already done — the decision only ever governed the tail.

### Decided 2026-08-08

- **Q8 — the fast-path gate gets its OWN error code, with a help line.** A name
  that exists but is not reachable is a different condition from one that does
  not exist, and it gets its own code rather than reusing `E0203`. The number and
  wording are the owner's; what is settled is that it is new, and that it carries
  a help naming the module that declares the candidate and the `import` that
  would make it reachable.

  Two measurements narrow what the code has to say, both taken on
  `examples/09-json-config`: of the would-be rejections, **all** are *"namespace
  not reachable"* and **zero** are *"candidate not public"*. The gate's first
  job is scope, not privacy — privacy already has `E0353` and its own corpus.

  Recorded because it was believed otherwise: `E0203` does **not** read
  "ambiguous type" anywhere in the tree. It is `E0203_UNDEFINED_TYPE`, rendered
  "cannot find type `X` in this scope" with the label "not found in this scope",
  and both tripwire corpora already name it. The case for a new code is the
  *distinction* it draws, not a mismatch in `E0203`'s wording.

- **Q9 — the B1 golden is host-aware.** `lookup_by_leaf calls` reads 5,041 on
  Windows and **4,989** on Linux for the same commit, because the Windows build
  compiles Windows-only stdlib modules; the same asymmetry moves `2c` by 20 and
  `declaration took a slot an import held` from 9 to 2. B1's *total* is unmoved
  at 12,453 on both. A single cross-host golden on a host-dependent row makes
  `make b1-check` red on whichever host did not pin it, and a permanently red
  ratchet is precisely what §7 says gets switched off — so the gate is taught the
  dimension rather than left to flip-flop. Re-pinning to one host's number was
  rejected for that reason.

### Decided 2026-08-04

- **Q7 — a top-level declaration is PUBLIC by default.** The parser wins;
  §3.1 and §3.2 are amended. `private` is the only visibility keyword that
  carries information, and `public` at top level is an explicit no-op.

  Consequences, recorded so they are not rediscovered as bugs:
  - **Zero code churn.** The alternative (default private) would have required
    an explicit `public` on every stdlib and compiler free function intended
    as API.
  - **The export set is inherited, not stated.** A new helper is exported the
    moment it is written; forgetting `private` is silent. This is the accepted
    cost — §3.3's gate enforces `private` where it appears, and no gate can
    recover an intent the author never wrote.
  - **Probes must say `private` explicitly.** A test written to the old §3.1
    exercises nothing (§8.1c, and the D-B zero in §8.1's table depends on this
    control firing).
  - `RC_VIS_REJECT_NOTPUBLIC` staying at 0 is therefore *expected* for the
    current tree, and is not evidence about the gate either way. The reason it
    cannot fire is §8.1c — function calls never reach that gate — not the
    default.

### Decided 2026-08-03

- **Q5 — a package root IS implicitly addressable.** `std::fs::path::Path`
  resolves with no `import std;`, the way Rust keeps crate names in the extern
  prelude. A fully-qualified path always works. This makes §8.2c's 79
  fully-rooted spellings legal with no edit.

  It does **not** rescue an abbreviated path: `core::option` is still an error,
  because `core` is not itself a root and nothing binds it. Roots are
  addressable; interior segments are not.

- **Q6 — a parent module does NOT implicitly bind its submodules.** `std::sys`
  must write `import std::sys::syscall;` to say `syscall::sys_call3`. A
  submodule is an ordinary module and reaching it takes an import like any
  other, so §4's rib chain gains no tier and "nothing is reachable that is not
  on the chain" stays literally true. Cost: one `import` in
  `stdlib/sys/_module.cryo`.

- **An import binds its leaf name.** `import std::core::mem;` binds `mem`, so
  `mem::swap(a, b)` is rule-6-legal. This is what keeps §8.2c's 22,536
  in-scope abbreviations legal, and it is why the migration is import
  statements rather than call sites.

**Net rule-6 migration, fully determined:** ~~456~~ **1,011** import statements
re-rooted with `std::`, plus one import added to `stdlib/sys/_module.cryo`. Zero
path rewrites, zero re-exports.

> **Corrected 2026-08-04.** The 456 figure is a `CRYO_PATH_AUDIT` event count
> from the compiler building *itself*, and a compiler build loads only the part
> of the stdlib the compiler uses — no `net`, `json`, `tls`, `http2`, `random`,
> `process`, `thread`. The whole-tree count, taken statically over every
> `import <path>;` in `stdlib`, `compiler/src` and `runtime`, is **1,011
> statements in 126 files across 97 distinct target modules** (1,007 in stdlib,
> 4 in `compiler/src`, 0 in `runtime`). Nothing about the *shape* of the
> migration changes — it is still one mechanical rule — only its size.
>
> **The rewrite is provably behaviour-identical.** M5 accepts a written path
> when some module's name has it as a `::`-delimited suffix, taking the first
> such module in graph order. Checked over all 97 paths: each has **exactly
> one** suffix candidate in the whole tree, and it is always `std::<path>`. So
> the static rewrite reproduces M5's answer by construction, and cannot be
> ambiguous. Verified alongside: **0** brace-list imports need re-rooting, and
> **0** of the rewrites produce a self-import.
>
> A measured event count and a corpus size answer different questions. Use the
> audit to learn *which rule* applies; count the corpus statically to size it.
