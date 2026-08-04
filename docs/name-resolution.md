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

Measured 2026-08-03 by compiling probe programs:

| Mechanism | Enforced? | Evidence |
|---|---|---|
| Exhaustive `match` over an enum | **YES** | `E0405`; `tests/negative/E0405_non_exhaustive_match.cryo` |
| Private struct/class **field** | **YES** | `E0353`; `tests/negative/E0353_private_field_access.cryo` |
| Non-public **function**, cross-module | **NO** | probe compiled clean |
| Non-public **type**, cross-module | **NO** | probe compiled clean |

**Cryo cannot currently enforce an API boundary.** Marking a function private
does not stop another module from calling it. Therefore:

> **Any design that relies on "make the fallback API private" does not work.
> The fallback entry points must be DELETED, not hidden.**

This is not a limitation to work around — §3.3 makes item visibility a real
gate, which fixes it. But the resolver's own enforcement cannot depend on the
feature it is implementing, so the mechanisms below are what carry it.

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

   > **NOT YET WIRED (checked 2026-08-04).** `CRYO_RESOLVE_COUNTER` appears
   > nowhere in `Makefile`, `scripts/`, or `.github/` — the gate is currently a
   > statement in this document, which §7's own opening sentence says is not
   > enough. Until a build step runs the counter and fails on a nonzero B1,
   > every B1 figure in §8 is a snapshot taken by hand, and the cascade can
   > regrow between snapshots exactly as it did before. Wiring it is cheap and
   > blocks on no other phase.

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
