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
Res = Def(SymbolID)        // a resolved declaration
    | Local(...)           // a local binding
    | GenericParam(...)    // a type parameter, BY NAME
    | PrimTy(...)          // a primitive
    | Err                  // resolution failed; diagnostic ALREADY emitted
```

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

Path-bearing node kinds requiring a slot (inventory taken 2026-08-03):

| Node | Anchor |
|---|---|
| `IdentifierNode` | `AST/expression.cryo:104` |
| `ScopeResolutionNode` | `AST/expression.cryo:789` |
| `NamedAnnotation` | `AST/_module.cryo:456` |
| `NewExprNode` | `AST/expression.cryo:436` |
| `SizeofExprNode` / `AlignofExprNode` | `AST/expression.cryo:483`, `:506` |
| `CallExprNode` (callee) | `AST/expression.cryo:398` |
| `ImportDeclNode` (per segment) | `AST/declaration.cryo:575` |
| enum variant reference | `AST/expression.cryo:424` |

---

## 7. Enforcement

A specification that can only be honored by discipline will be violated under
deadline — that is the observed history of this subsystem. This section lists
what actually forces compliance, and is normative.

### 7.1 What the language can force — measured

Measured 2026-08-03 by compiling probe programs; the function row re-measured
2026-08-05; the **call/reference split** and the **type row** re-measured
2026-08-06 (§8.2n), which corrected both.

| Mechanism | Enforced? | Evidence |
|---|---|---|
| Exhaustive `match` over an enum | **YES** | `E0405`; `tests/negative/E0405_non_exhaustive_match.cryo` |
| Private struct/class **field** | **YES** | `E0353`; `tests/negative/E0353_private_field_access.cryo` |
| Non-public **function**, cross-module, **called** | **YES**, since 2026-08-05 | `E0353`; `tests/projects/visibility_gate` — all four *binding* doors, §8.1f |
| Non-public **function**, taken as a **value** | **NO** | compiles, links, runs; §8.2n door 5 |
| Non-public **static method**, cross-module | **NO** | compiles, links, runs; §8.2n door 6 |
| Non-public **instance method**, cross-module | **YES** | `E0353`; §8.2n positive control |
| Non-public **type**, cross-module, leaf **unique** | **YES** | `E0503` fires; §8.2n variant A |
| Non-public **type**, cross-module, leaf **shared** | **NO** — *maskable* | §8.2n variant B |

Two corrections to what this table said before 2026-08-06, both measured:

**The type row was not "no enforcement" — it was "enforcement that is not a
gate."** `E0503` does fire for a cross-module private type. But
`ModuleTypeRegistry::private_owner_module` is keyed on the **leaf**, scans every
registered module, and returns "allowed" the moment *any* same-leaf type
anywhere in the program is public — so one unrelated `public type struct Hidden`
in a module the use site never imports silently unmasks a private `Hidden` in a
module it does. Its use site also asks the **ambient cursor**
(`current_module_name()`) for the use-site module rather than the module that
wrote the syntax. The masking is written as intent in the function's own doc
comment; §3.3 says visibility is a gate, so the comment describes a defect.

**The function row overstated its scope.** "All four binding doors" are four
ways a *call* binds, and the gate is enforced at the call. A private function
that is never called — taken as a value into a function pointer — and a private
*static* method both reach across a module boundary and execute.
`enforce_callee_visibility` is reached only from call paths, and
`enforce_method_visibility` only from member-access paths, so neither lane sees
these two shapes.

The rider below is therefore **unchanged in force but sharper in reason**: a
private type is reachable not merely when it is unambiguous, but whenever any
unrelated module happens to export its leaf. Therefore, and until that closes:

> **Any design that relies on "make the fallback API private" works only when
> the fallback is a FUNCTION. A private TYPE is still reachable from anywhere it
> is unambiguous, so type-level fallback entry points must be DELETED, not
> hidden.**

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
| M1 `module_ns_matches_prefix` | 49,718 | 49,586 | 99.7% hit — load-bearing |
| M2 `resolve_module_qualified_symbol` | 28,748 | 22,516 | |
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

### 8.3 B2 is unmeasured

Only the assoc-type projection (62) is instrumented. Sema's method and trait
dispatch — the actual bulk of type-dependent resolution — is not counted.
§7.3's B2 target is "enumerated and justified"; that is not currently possible.

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
  with a suggestion — decided per name, deliberately.
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
