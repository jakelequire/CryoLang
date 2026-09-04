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
Res = Def(SymbolStr)       // a declaration or module, by canonical qualified name
    | Local(SymbolID)      // a local binding
    | GenericParam(SymbolStr)  // a type parameter, BY NAME
    | PrimTy(SymbolStr)    // a primitive
    | TypeRelative         // the type layer owns the rest of this path
    | Err                  // resolution failed; diagnostic ALREADY emitted

ResSlot = Pending          // no resolver has answered for this node
        | Answered(Res)    // one has, including with a failure
```

**`Def` carries a qualified name, not a `SymbolID`.** An earlier revision of
this section wrote `Def(SymbolID)`. A `SymbolID` indexes the Resolver's own
arena, and two things make it the wrong handle for this answer: modules are not
declared there at all, so a module scope has no `SymbolID` to name, and every
downstream consumer — `DeclarationIndex`, the type arena, the mangler — keys on
the interned qualified name, so a `SymbolID` stamp would have to be translated
back at each use, re-deriving the thing the stamp exists to stop re-deriving.
Decided 2026-08-11 with the first stamped lane (§8.5).

**`Res` has no variant meaning "missing", and that is the point.** Every
variant is an answer, so a consumer holding one has nothing to branch on and
nowhere to put a second strategy. Absence lives one type up, in `ResSlot`,
where only the resolver can observe it — which makes "re-resolve it later"
unwriteable rather than merely discouraged. A consumer reaches an answer only
through `ResSlot::require`, which is total over `Res`: on `Pending` it records
a bug and hands back `Err`.

Reaching a stage that requires the stamp while still `Pending` is an internal
error (§7.2 mechanism 4), never a licence to search. It is recorded rather than
fatal — a compilation that already emitted a diagnostic may legitimately have
skipped work, so the bug is reported only if the build otherwise succeeds.

**`TypeRelative` is a positive claim of ownership, not a shrug.** The language
says a path whose first segment names a *type* is finished by the type layer
(`TypeDependentRes` below), which needs a receiver type and so cannot run here.
It must never record that a lookup was attempted and failed — that is `Err`
when the name layer is the authority, and a defect in the lookup when it is
not. Laundering a tool's limitation into an ownership claim rebuilds the
cascade under a better name.

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
| `NamedAnnotation` | `AST/_module.cryo:456` | **`res`** (written name; §8.2ak) |
| `NewExprNode` | `AST/expression.cryo:436` | — |
| `SizeofExprNode` / `AlignofExprNode` | `AST/expression.cryo:483`, `:506` | — |
| `CallExprNode` (callee) | `AST/expression.cryo:398` | — |
| `ImportDeclNode` (per segment) | `AST/declaration.cryo:575` | — |
| enum variant reference | `AST/expression.cryo:424` | — |
| `ClassDeclNode` (base class) | `AST/declaration.cryo:854` | **`base_res`** (§8.2am) |
| `ImplBlockNode` (trait head) | `AST/declaration.cryo:1152` | **`qualified_trait_name`** |
| `TraitRef` (where/bound trait) | `AST/_module.cryo:597` | **`resolved_name`** |

`ImplBlockNode.qualified_trait_name` carries a canonical name rather than a
`Res`, because the question asked of it is identity — *are these two impls the
same trait?* — not location. It exists because the trait-impl registry keys on
bare leaves so that a lookup is module-independent, which makes that key a
**search** key: folding it back to a qualified name searches the program and
therefore answers only while the leaf is unique. A single unrelated
declaration sharing the leaf made one trait read as two and turned every method
on it into a false ambiguity. A search key cannot be used as an identity; the
identity has to be carried from where it was resolved.

`TraitRef.resolved_name` is the same rule reached from the other side. A trait
reference in a bound stores its spelling as path segments, and every consumer
asking *which trait is this?* re-derived the answer from `path[length - 1]` —
discarding the qualifier the author wrote, then searching the program to
recover it. The search declines on a plural leaf, so where
`qualified_trait_name` made one trait read as two, this made two traits read as
one: the impl-head coherence key rendered two impls that differed only in which
module their bound came from as the same string, and rejected the second with a
spurious `E0308`. Writing the bound fully qualified did not help, because the
qualifier never reached the key.

The slot is stamped at `register_decl_in_index` — the same chokepoint as
`qualified_trait_name` — and resolved with `resolve_scoped_or_at` against the
bound's own `span.file`, so the answer is judged by the module that *wrote* the
bound rather than the module the compiler is standing in (§5.2), and survives a
clone being re-visited under a different cursor. `TraitRef::identity()` is the
single accessor; it defines the unstamped answer once rather than once per call
site.

Two limits are deliberate. A *relatively* qualified spelling (`alpha::Render`
for `leaf_key::alpha::Render`) has no bare→qualified mapping and so keys by what
was written — the false-negative direction, and the same residual the impl head
carries. And the consumers that ask *does this type implement the trait named
X?* still take the bare leaf, because `get_trait_decl` is keyed by bare leaf
alone and `type_implements_trait` additionally compares against bare `Copy` /
`Send` / `Sync` / `Drop` by `equals`. Handing those a qualified name makes the
lookup miss and moves Copy/Drop classification, so re-keying them is its own
project rather than a call-site change — see §8.2 for the inventory.

Those key spaces are not the whole block, and reading them as it costs a
session: an impl head carried no identity to hand them in the first place,
because its trait annotation was never name-resolved (§8.2an). The consumers
are the second obstacle, not the first.

`lookup_method_return` is the exception, and what distinguishes it is its key
space rather than its call site: a trait's methods are registered under **both**
the bare and the home-module-qualified trait name, so the qualified half is an
unambiguous key that already exists. The bound scans in `method_binding.cryo`
therefore ask it with `TraitRef::identity()`, as does the `lookup_type_by_sym`
scan beside them, which tries a qualified name ahead of its own cascade. The two
key spaces are used side by side within one scan — identity for the registry
lookup, the bare leaf for `subst_bound_assoc_args`, which reaches
`get_trait_decl`. That is not a fallback chain: they are two questions of two
registries, each with one answering path.

Safe because measured, not because reasoned. A/B-ing both key spaces over 56
corpora (the compiler, every example, the whole project suite) recorded 7300
agreeing return lookups and 10160 agreeing type lookups, with **no case where
the leaf answered and the identity did not**, and none where the two answered
differently. The stamped identity is the registry's canonical key: `Iterator`
resolves to `std::core::iter::Iterator`, a user trait to `Main::Shape`. The
trade is visible in the ratchet — 24 answers move off the arena leaf index,
which is valid only while a leaf is unique program-wide, onto resolution whose
written qualifier is checked against the declaring module.

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

### 7.2 The five mechanisms

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
   > every per-site row flagged `B1` or `B3*`.
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
   > - **The asserted set is wider than the B1 family.** A `B3*` row is pinned
   >   too, and that marker is deliberately narrow: it belongs to a site that
   >   answers a module-independent *identity* question rather than binding a
   >   name, so it has no reduction target and cannot sit in B1 — but it is
   >   still a leaf lookup a future caller could misuse for binding. Leaving it
   >   unasserted would buy a reachable B1 target with a lane nobody can watch,
   >   which is the failure this gate exists to prevent. A B3 row without that
   >   regrowth story stays unasserted, because B3's target is "once per path"
   >   and pinning it would make the gate noisy.
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

5. **A lane is a PARAMETER, and a second lane cannot compile.** §5.2 requires
   one `resolve_path(segments, ns, scope)`; the enforcement is what keeps it
   one. Three locks, in increasing order of strength:

   - **The namespace is required at the call.** There is no overload that omits
     it and no default. A caller must state which namespace it is asking about,
     which is what makes "resolve `Foo`" a well-formed question instead of a
     guess about which index to consult.
   - **The per-kind lookups are non-public.** `lookup_type`,
     `lookup_func_return`, `lookup_func_type`, `lookup_global` and
     `lookup_method_return` become thin non-public wrappers over the primitive.
     §7.1 measures a non-public FUNCTION as enforced cross-module (`E0353`), so
     a direct call from `sema`, `mono` or `codegen` **fails to compile** rather
     than being caught in review. This is the one place privatization is a
     legitimate design, and only because these are functions — the §7.1 rider
     still holds for types, which must be deleted rather than hidden.
   - **A surface ratchet pins what remains.** Privatization cannot stop someone
     adding a *new* public wrapper, and deletion cannot stop someone
     reintroducing a helper. `make lane-check` pins two numbers against a
     golden, in the shape mechanism 3 already proved: the count of direct
     per-kind lookup call sites outside the resolver, and the count of
     `get_resolver()` re-entries outside the driver. Both ratchet **downward
     only** — an increase is a build failure, and a decrease must be re-pinned
     deliberately.

   The reason this needs all three is the observed failure mode. Lanes did not
   drift because anyone decided to have two resolvers; they drifted because
   each new caller needed an answer the existing lane could not give, and
   adding a sibling lookup was always locally cheaper than fixing the shared
   one. A convention loses that trade every time, under deadline, which is what
   §7's opening sentence records. A compile error does not.

   **Corollary — name resolution is a PASS, not a service.** A stage that can
   call back into the resolver will, and a resolver called from `sema` no
   longer has the writer's imports in hand, so it answers from the ambient
   cursor or from a string. That is the mechanical origin of B1: every fuzzy
   answer is a re-entry that lacked the inputs to do better. `Res` exists so
   later stages **read** instead of asking, and driving `get_resolver()` out of
   `sema`/`mono`/`codegen` is what makes the property structural rather than
   incidental.

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

The counter classifies every instrumented lookup into four buckets. The
two-bucket model in the roadmap (§3.2 rule 2) was measured wrong on
2026-08-03 and is superseded; B4 was split out of B1 on 2026-08-21 (§8.13):

| Bucket | Meaning | Target |
|---|---|---|
| **B1** | fuzzy fallback — guesses from a string | **zero** |
| **B2** | type-dependent — genuinely needs a receiver type | stays; enumerated and justified |
| **B3** | authoritative — answers from scope/imports | **once per path**, not zero |
| **B4** | what the global leaf index answers — instantiation identity | **0**; the `(res, generic_args)` pair names the slot without a mangled key (§8.20) |

**These are not one quality ladder, and reading them as one is the common
mistake.** B1 and B3 answer the SAME question — *what does this written name
refer to?* — one by guessing from a string and one from the writer's imports,
so that pair is a quality axis and B1's target is zero. **B2 is a different
question**: *given a receiver of type `T`, which method does `.foo()` dispatch
to?* No amount of name-layer correctness answers it, because it needs the type.
So B2 is not a worse B3 and does not converge on B3; it has a permanent floor.
The same split exists in Rust, where method calls are resolved in
`rustc_hir_typeck::method` and never in `rustc_resolve` — the counter only
makes the boundary visible. What B2 *should* do is shrink as the name layer
resolves more of a path's prefix before handing off (§5.3), and it is recorded
as a FLOOR because sema's dispatch is uninstrumented, so its number is
under-counted rather than exact.

**B4 is a floor for the same reason B2 is, arrived at from the type side
rather than the receiver side.** A `Res` names a *declaration*: the thing a path
was written to reach. `Array<JsonValue, GlobalAlloc>` is not a declaration — it
is one instantiation of `Array`, minted by monomorphization long after the name
layer has finished, and the name the arena is asked for is its MANGLED identity
(`5Array$LN$L3std.4json.5value.9JsonValue$G_N$L…$G$G`), which no source file
contains and no import can bring into scope. Asking name resolution to answer it
is a category error, not an unfinished migration, so counting it against a
target of zero makes that target permanently false and hides whatever real fuzzy
fallback might regrow underneath it.

What B4 *should* do, like B2, is shrink as instantiation identity gets a key of
its own rather than a leaf, and it was recorded as a FLOOR because the arena's
leaf index was the only structure that answered it.

**B4 is now 0, and the floor reasoning above is superseded.** The premise —
that instantiation identity has no key a `Res` can supply — is true and does
not entail the conclusion. A `Res` alone cannot name `Array<JsonValue,
GlobalAlloc>`, but nothing required it to: the DEFINITION is on the node and
the ARGUMENTS are beside it, and the pair `(res, generic_args)` names the same
arena slot without minting a mangled string to look up. Struct literals and
generic scope qualifiers resolve from the pair (§8.20); a specialized clone
carries its instantiation as the arena id itself, which IS the pair already
resolved. The mangled name is an OUTPUT of monomorphization, never a lookup
key. `B4_TOTAL` is pinned 0 on both hosts, and the arena leaf index is still
reachable and answers nothing — the control that says the calls stopped
happening rather than stopped being recorded.

The paragraphs below record how the bucket was measured and separated from B1.
They remain accurate about the mechanism and about why a bare mangled name is
not a key; they no longer describe a live population.

**The bucket is named for its mechanism, not purely for its contents, and
deliberately so.** §8.13 has the measurement that separated it from B1: on three
populations every leaf-index hit was an instantiation. A fourth - the test
corpus - additionally shows 76 hits on `Poll`, a written source name reached
through a reference async lowering SYNTHESIZES into modules that do not import
it. That one is a genuine defect with a home (§8.10's family: the node carries a
`set_synthesized_ref` its reader ignores), not a permanent floor, and it leaves
B4 when that reader is fixed. Classifying by mechanism keeps the row honest in
the meantime; classifying it as "instantiation identity" alone would have
asserted something only three of four populations support.

**An arena lookup by the BARE name is not the key B4 is waiting for.** A
specialization is cached under its bare mangled spec name (`5Widget$Li$G`),
which carries no module, so two modules instantiating one generic collide on it
exactly as they do on a leaf - see §8.14.

B3 exists because `resolve_qualified_scoped` accounts for 487,838 of
`lookup_qualified_alternatives`' 496,945 calls, and it is import- and
prelude-aware — close to what §4 specifies. Classifying it as "must reach zero"
inflated B1 from 144,639 to 492,867 and set an unreachable target.
**"Must reach zero" is correct for fuzzy fallbacks and wrong for the
resolver's own lookup**, whose correct target is once-per-path.

`canonical_qualified` is B3 for the same reason, arrived at from the opposite
direction. It reads like a fuzzy fallback — a bare leaf handed to an index —
but it never answers *which declaration this name binds to*; it answers *what
this type's module-independent identity is*, and it folds only when exactly one
qualified candidate is registered, handing a colliding leaf back unchanged. Its
consumers are coherence keys — trait-origin comparison, impl-block dedup,
where-clause keying — which must key the same type identically from every
module, so a use-site-dependent answer would be the defect there and not the
fix. That makes it permanent by construction: no consumer work retires it, and
leaving it in B1 would make `B1 == 0` unreachable and the gate's stated end
state false. It is summed by what it **folded**, not by how often it was
called, since a call that hands its input back produced nothing the caller
would not have had anyway.

Its rows keep the ratchet under the `B3*` marker above. The classification says
the site is legitimate *as its current callers use it*; a future caller that
reached for it to bind a name would be a genuine regression, and the flag alone
would not catch it.

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
| M1 `ns_written_as` (check + export scan) | 49,718 | 49,586 | export scan **deleted**; the check remains and is not a summand — §8.2ai, §8.2aj |
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

**Nothing answers in its place. The annotation simply goes unresolved.**
Diffing the whole `resolve_named` cascade across the stub — it is already
instrumented one site per outcome — shows the work is not redistributed:

| `resolve_named` outcome | scan present | scan stubbed |
|---|---:|---:|
| 3b DI canonicalized qualified | 24 | 23 |
| 5 global leaf index | 0 | 0 |
| **unresolved** | **107** | **109** |

With the scan, the abbreviated spelling is canonicalized and found at 3b. Without
it, `resolve_named` returns nothing two more times — **and the build still
succeeds and the test still passes**, because `mut h: future::executor::JoinHandle<i64>`
is redundant with inference from its own initializer. An annotation that resolves
to nothing is silently tolerated; the declared type is supplied by the callee's
return type instead.

That is the honest shape of the lane. The export scan is not a resolution
strategy competing with another one — it is cover for an annotation lane that
cannot resolve its own qualified path, and the corpus cannot see it working
because the only annotation that needs it is one inference would have typed
anyway. Two consequences, and the second is the more serious:

* **Deleting the scan is a real behaviour change**, just an invisible one: two
  annotations go from resolved to unresolved. Stamping `NamedAnnotation` first
  makes the scan unreachable rather than merely redundant, and then its deletion
  removes a branch that can no longer be taken.
* **A `NamedAnnotation` naming a type that resolves to nothing produces no
  diagnostic.** `resolve_named`'s 107 unresolved answers are not all benign, and
  nothing distinguishes "inference will cover this" from "this annotation names
  a type that does not exist". That is its own defect, independent of M1.

The lane is `TypeResolver::canonical_type_name`, which asks
`resolve_type_qualified_name_from` a second time — from the annotation's HOME
scope rather than the ambient cursor — then falls back to the ambient one, so
the two-strategy shape repeats one level up. Its fall-through hands back the
abbreviated string as though canonical, because a non-empty `SymbolStr` reads as
valid; a resolver that cannot say "I did not resolve this" is why its caller
needs a chain at all.

### 8.2aj The 107 unresolved are ALL generic parameters, and the scan's 1,627 answers are the identity — MEASURED 2026-08-11

Two claims from §8.2ai were taken at face value and both are now measured. One
survives in a stronger form; the other is wrong.

**Controls first.** Compiler rebuilt from the tree at §8.2ai, after
`make stdlib runtime-tiers`. `examples/09-json-config`, `--no-incremental`,
`CRYO_CODEGEN_THREADS=1`: B1 = **5585**, matching `tests/b1-baseline.txt`
`[host:linux]` row for row. `selfhost-check` passes both halves on that tree.

#### The export scan answers nothing, and that is measurable without stubbing it

| row | gate target |
|---|---:|
| `M1 SEARCH export-table scan calls` | 1627 |
| `M1 SEARCH export-table scan hits` | 1627 |
| `  of those, answer != input` | **0** |

The scan returns `mod_str + "::" + leaf_str`, and the fall-through immediately
below it returns `name` unchanged. When those are equal the scan has produced
**the identity function**. So all 1,627 of its B1 answers on the gate target
are answers the caller would have received from the function not existing.

This does not require the stub experiment to establish, and it is a stronger
statement than "redundant": the B1 bucket is crediting a fallback with 1,627
answers, none of which changed a name.

#### All 107 unresolved answers are generic parameters — none is a missing type

`CRYO_RN_AUDIT=1` over the unit tree, step `X-failed`, 56,163 RN lines total.
107 unresolved, reproducing §8.2ai exactly. Six distinct spellings:

| name | count |  | site | count |
|---|---:|---|---|---:|
| `T` | 53 | | `sema/method_binding.cryo:798` | 73 |
| `S` | 22 | | `passes/type_resolution.cryo:62` | 27 |
| `C` | 16 | | `sema/sema.cryo:3365` | 6 |
| `F` | 9 | | `mono/call_specializer.cryo:2164` | 1 |
| `O` | 6 | | | |
| `I` | 1 | | | |

**Every one is a single-letter generic parameter. Not one is a written path
naming a type that does not exist.** The gate target's own 14 are the same
shape — 14 of 14 `T`, home equal to cursor in every case, so not the keystone
and not a missing scope.

Two of the three sema sites fail *by construction*, and say so:

* `sema.cryo:3365` — "An ABSTRACT param … needs a binding, and no home module
  can supply one, so those keep failing here by construction."
* `method_binding.cryo:798` — "an explicit arg referencing the enclosing body's
  abstract params can't select a concrete instantiation — detect in a FRESH
  context (no bindings) and defer the whole call."

Sema resolves in a binding-free context *so that* an abstract parameter fails,
and uses the failure as the answer. `type_resolution.cryo:62` is different: it
is the runner's shared `res_ctx`, built `HomeOrigin::Unset` with an empty home.

**So §8.2ai's second bullet is withdrawn.** There is no population of
undiagnosed "this annotation names a type that does not exist" behind the 107.
A diagnostic hung on `resolve_named` answering nothing would fire on both
deliberate probes and break the symbolic-walk deferral. If an unresolvable
annotation should be diagnosed, the diagnostic belongs on the **annotation**,
which is a node the user wrote — not on this function, which the probes reach
without one.

There is a constructive reading. Under §6.1 a generic parameter is not a
failure at all: it is `Res::GenericParam`, an answer. Stamping would convert
107 of 107 of today's "unresolved" into answers.

#### Stamping does NOT make the scan unreachable

§8.2ai's first bullet gives the deletion order as: stamp `NamedAnnotation`,
which makes the scan unreachable, then delete a branch that can no longer be
taken. The premise does not hold. `resolve_qualified_type_via_exports` is
reached only through `Resolver::resolve_type_qualified_name`, which has four
live callers, of which the annotation lane is one:

| caller | qualified name possible? |
|---|---|
| `types/resolver.cryo:1632` `canonical_type_name` | yes — the annotation lane |
| `sema/type_utils.cryo:146` `resolve_cross_module_name` | yes |
| `passes/type_resolution.cryo:3853` base class | yes |
| `sema/async_lower.cryo:2443` `sr.scope_name` | yes |

Stamping starves the first. The other three keep reaching the scan, so the
branch remains takeable and the deletion cannot be justified as removing dead
code.

It is still justifiable — on the measurement above, by "it answers nothing"
rather than "nothing reaches it". Those are different claims about different
populations, and only the first is measured. The one answer the scan has ever
produced corpus-wide that was not the identity is the `JoinHandle` annotation
of §8.2ai; deleting the scan sends that annotation, and one other, to
unresolved, where inference already covers both.

#### Ordering constraints on the `NamedAnnotation` stamp, read off the source

* §6.1 fixes the owner: `Res` is "produced by the resolver, before types
  exist", so the slot cannot be a memo of `canonical_type_name`'s answer
  written during type resolution. A memo would also not starve the scan — the
  first resolution of each node still runs the cascade.
* The `NameResolver` does not traverse type annotations at all; a
  `TypeAnnotation` is not an `accept`-visited node. The stamp needs an explicit
  recursive walk plus a call from every annotation-bearing visitor, including
  `SizeofExprNode` and `AlignofExprNode`, whose visitors are empty bodies.
* `NameResolutionPass::run` runs per module, and `export_symbol` is called only
  from that module's forward-declare sweep. Intra-module forward references are
  therefore safe, and cross-module ones follow the import DAG rather than
  directory order — but an import **cycle** has no valid order, so some
  annotations stay `Pending` for a structural reason. Under §6.1 that is a
  routing fact and must not become a licence to search.

#### Instrument landmine

`CRYO_RN_AUDIT=1` perturbs the suite exactly as `CRYO_PATH_AUDIT=1` does.
`namespace_gate_methods` asserts its output must not contain `Carrier`, and the
audit stream echoes the module `CryoTests::Tests::Lang::AsyncGiveawayCarrierAddressStable`
31 times. Under `CRYO_RN_AUDIT` the project baseline is **32 passed / 2 failed**
— `resolution_leaf_index` by design, plus this echo — not 33 / 1.

#### The scan is DELETED, on the measurement that holds

`resolve_qualified_type_via_exports` and its three counter sites are gone, and
`M1ExportHits` leaves the B1 summation. The justification on the record is *it
answers nothing* — 1,627 identity answers, one non-identity answer
corpus-wide — and explicitly NOT *nothing reaches it*, which the four-caller
table above disproves.

Predicted before the edit: B1 5585 → **3958**, the two SEARCH rows leave, and
no other row moves. Measured after: B1 = **3958**, 17 sites, and a full diff of
the counter report against the pre-deletion run shows only the three deleted
rows and the total. Every other row — `lookup_by_leaf` 4989/1431, canonical
2527, M1 CHECK 4804/4804, M2 2258, M4 267/0, and `resolve_named` unresolved at
14 — is byte-identical. That is the accounting signature, with no behaviour
drift on this population.

Gates after: `make test` unit ok / compile-fail 170 passed / projects 33 passed,
1 failed (`resolution_leaf_index`, §2's by-design red); `make examples` all 14;
`roster-check` OK; `api-index-check` OK; `b1-check` OK at 3958.

The `[host:windows]` golden now has a **sixth** reason to be stale and still
owes a re-pin from a Windows host.

Two annotations that the scan used to canonicalize now go unresolved, where
inference already supplies the type. No diagnostic was added: per the 107
measurement above, a diagnostic hung on `resolve_named` answering nothing would
fire on two deliberate sema probes. Hanging it on the annotation node instead
is `NamedAnnotation`'s row of §6.3, still unfilled.

### 8.2ak The annotation stamp lands write-only: 87% covered, and the hard case is 4 names — MEASURED 2026-08-11

`NamedAnnotation` now carries `res: Res`, filled by `NameResolver::stamp_annotation`
walking the annotation tree from every visitor that holds one. **Nothing reads
it.** That is deliberate: a stamp with no consumer cannot change what a program
compiles to, so the coverage number below is measured before anything depends
on it being high.

#### What the resolver can actually ask

`register_type` runs in the **TypeDeclaration** pass, which is the pass *after*
`NameResolution`. So at stamp time the `DeclarationIndex` knows module imports
and the prelude — which is why `stamp_module_scope` can use `ns_imports` — but
knows **no types at all**. The only type knowledge available is the Resolver's
own export table, populated per module by the forward-declare sweep of
`visit(ProgramNode*)`.

That is the same table `resolve_qualified_type_via_exports` was scanning before
§8.2aj deleted it. The difference is not the data, it is the question: the scan
searched it by prefix from wherever the compiler was standing and threw the
answer away; the stamp asks once from the writer's own module scope and records
the answer on the node.

#### Coverage, `examples/09-json-config`

| outcome | count |
|---|---:|
| annotations offered to the stamp | 2679 |
| stamped `Def` (bare, via home scope) | **1686** |
| stamped `GenericParam` | **656** |
| UNSTAMPED qualified spelling | 4 |
| UNSTAMPED span names no module | 0 |
| UNSTAMPED module has no scope | 0 |
| UNSTAMPED bare name not in scope | 333 |

**2,342 of 2,679 — 87% — are answered.** B1 is unchanged at 3958 and every
gate is unchanged, as it must be with no consumer.

#### The case the design was worried about is four names

§8.2aj flagged that a qualified spelling needs a lookup that can say "no", and
the one that exists returns its input unchanged on failure, so a canonical
answer and a refusal are the same value. Those are left unstamped and counted —
and there are **4** of them. The shape that looked like the obstacle is a
rounding error; it did not deserve the design weight it was being given.

#### The 333, classified — 227 primitives and 106 ordering casualties

Emitted at the event as `ANN-UNSTAMPED` on `CRYO_PATH_AUDIT`, because a count
cannot say which name:

* **227 are primitive spellings** — `string` 40, `char` 19, `u64` 18, and so on
  down. 58 come from `std::core::primitives` alone and the rest from trait
  impls on primitives (`std::core::cmp`, `std::fmt::display`, `core::hash`,
  `core::drop`, `core::ops`, `core::clone`). Primitives are lexed as keywords
  and normally parse to `PrimitiveAnnotation`, so their arriving here as
  `Named` is itself worth a look. Under §6.1 they are `Res::PrimTy` — an
  answer. Adding it needs the lexer's keyword table as the single authority
  rather than a second list of primitive names.
* **106 are real types, and only twelve distinct names**: `Option`, `Array`,
  `Str`, `String`, `Iterator`, `GlobalAlloc`, `AllocError`, `Arena`,
  `CharIndices`, `Chars`, `ConversionError`, `SplitIter`. Every one is a core
  stdlib type with heavy mutual dependency, and they go unstamped in modules
  that import their declarer — `String` unresolved in `std::fmt::display`,
  `std::io::traits`, `std::io::stdio`; `Str` in `std::collections::string`
  itself.

That second bucket is the structural limit predicted from the pass order, now
with a number on it: the export table is filled in module processing order, so
a module whose declarer has not been walked yet cannot be answered. It is the
import-cycle population, it is 4% of annotations, and it is the question that
has to be answered before any consumer may treat `Pending` as an ICE rather
than as "the type layer still owns this one".

### 8.2al The 227 primitive spellings are synthesized receivers, not written names — MEASURED 2026-08-11

§8.2ak said their arriving as `Named` was "worth a look" and proposed stamping
them `Res::PrimTy` off the lexer's keyword table. The look was taken first, and
it **supersedes that proposal**: they are not written type names at all, so the
stamp would have recorded a true fact about a node the parser should not have
produced.

Measured on this host (`[host:windows]`, `examples/09-json-config`), where the
unstamped bucket is the same 333 as Linux while the offered total is 2,699
rather than 2,679 — a Windows build compiles Windows-only stdlib modules, and
all 20 extra annotations stamp `Def`.

#### Classifying by the span, not by the name

`ANN-UNSTAMPED` names the module and the name; neither says which *construct*
wrote it. The annotation's own span does, so the audit line carries it. Every
one of the 333 was then read off its source line:

| construct at the annotation's span | count | of which primitive/unit |
|---|---:|---:|
| synthesized `&this` receiver | 219 | 211 |
| synthesized `mut &this` receiver | 19 | 14 |
| written type name | 95 | 3 |

**225 of the 228 primitive/unit spellings are synthesized receivers.**

#### Why a keyword never reached the keyword test

`parse_parameter` types a `&this` receiver from `Parser::current_type_name` — a
`string` holding the impl target's **text**, set by `parse_implement_block`.
`is_primitive_type_token` tests a *token kind*, and by the time the receiver is
synthesized the target is text, so the branch that produces
`PrimitiveAnnotation` for every written primitive cannot be reached from there.
`implement string { … }` therefore gives each of its methods a receiver
annotation naming `string` that no module scope can answer.

The control is `()`: it is one hit, and a written `()` parses as an empty
`Tuple`, so `Named("()")` can only come from the one line in
`parse_implement_block` that spells the unit target's text. It is the fingerprint
of the synthesis and it is the only one of the 228 that survives the fix, because
`()` is not a keyword.

#### The second primitive-name list had already drifted, by exactly one name

The other 3 are written `-> never`. `never` was in `TypeResolver::resolve_primitive`'s
string table and **not** in the lexer's keyword table, so it lexed as an
identifier, parsed to `Named`, and resolved only by falling through
`resolve_named` into the same table the keyword would have named directly. The
two lists differed by `{never}` and nothing else. §8.2ak's instruction to use the
keyword table as the single authority is the right rule; there was already a
duplicate to remove rather than one to avoid creating.

#### What changed

`TokenType::is_primitive_type` now holds the one list, `is_primitive_type_token`
delegates to it, `never` joins the keyword table (it becomes reserved), and the
receiver synthesis asks `TokenType::from_keyword` instead of assuming a name.

Predicted before the edit: the primitive spellings leave both the offered and
the unstamped bucket, `Def`/`GenericParam`/qualified hold still, `resolve_named`
step 2 empties, and **B1 does not move** — none of these sites is a B1 summand.

| row | before | after |
|---|---:|---:|
| annotations offered to the stamp | 2,699 | 2,472 |
| stamped `Def` | 1,706 | 1,706 |
| stamped `GenericParam` | 656 | 656 |
| UNSTAMPED qualified spelling | 4 | 4 |
| UNSTAMPED bare name not in scope | 333 | **106** |
| `resolve_named` 2 primitive name (B3) | 230 | **0** |
| B1 total | 3,958 | 3,958 |

Coverage 2,362 of 2,472 — **95.5%**, from 87%. Seven rows moved and ninety were
identical; the B3 total falls 236 = the 230 step-2 answers plus 6 scope
judgements that only walked the tree because `never` was a named annotation.
Five `-> never` positions exist in the compiled corpus and six judgements
stopped; the multiplicity is unattributed.

That the type is unchanged is structural, not a test result: `resolve()`'s
`Primitive` arm and `resolve_named` step 2 call the **same** `resolve_primitive`
with the same name, and steps 1 and 1b cannot shadow it because a generic
parameter cannot be spelled with a keyword.

#### What is left is exactly the ordering question

The remaining 106 are the twelve stdlib types of §8.2ak plus `()`. Nothing about
the primitive population touched them, which is the point: they are a different
defect, and the pass-order question in §8.2ak is still the one that gates any
consumer treating `Pending` as an error.

### 8.2am A base class is a bare leaf the name layer never saw — MEASURED 2026-08-15

`passes/type_resolution.cryo` resolved a class's base through three steps:
current-module qualification, a `get_resolver()` re-entry, then the global leaf
index (`LeafViaTypeResBase`). The step-3 comment justified itself by naming the
case only the leaf index could answer — a base whose own module repeats its name,
`class Type` in `namespace Compiler::Types::Type` registered as
`Compiler::Types::Type::Type`.

Tagged per step over the compiler's own source, the test tree, and all 14
examples:

| population | events | step 1 | step 2 (re-entry) | step 3 (leaf index) |
|---|---:|---:|---:|---:|
| `compiler/` | 103 | 71 | **32** | **0** |
| test tree | 8 | 8 | 0 | **0** |
| `examples/` (14 projects) | **0** | — | — | — |

⇒ **Step 3 never ran.** The 32 that reached step 2 are exactly the
double-qualified bases the step-3 comment claims only the leaf index finds —
`Type`, `ASTNode`, `BaseASTVisitor`, `ParserBase`, `ExprParser`. The comment
named the right construct and the wrong step.

The corpus cannot see this at all: `examples/` is structs and free functions, so
the population is the compiler itself.

**The slot.** The parser accepts a single `Identifier` for a base
(`parser/parser.cryo:1048`), so this is always a one-segment question in the type
namespace — and `NameResolution` never asked it. `visit(ClassDeclNode*)` entered
the type-body scope, declared generics and fields, walked methods, exited.

`ClassDeclNode.base_res` is now stamped from the scope ENCLOSING the class — it
has to be asked before `enter_scope`, or the class's own name and generic
parameters shadow the base. Run in shadow against the live cascade before the
switch, the stamp agreed on **111 of 111** (103 + 8) once the canonical name is
looked up in the arena rather than the declaration index: 7 of the compiler's
bases (`ASTNode`'s subclasses) name a type the `DeclarationIndex` does not hold,
which is why the deleted step 2 needed `arena.lookup_by_name` behind its own
`lookup_type`.

**A base that names nothing was silently dropped.** `class Widget : Nonexistent`
compiled clean, exit 0, and the class lost everything it was written to inherit.
The name layer can finish this question, so it now reports it: `E0203` at the
base's own span, and the slot records `Err`
(`tests/negative/E0203_undefined_base_class.cryo`, verified to compile cleanly
under the pre-fix pin).

### 8.2an An impl head's trait annotation was never name-resolved — MEASURED 2026-08-15

§6 records that the consumers asking *does this type implement the trait named
X?* still take the bare leaf, and attributes the block to their key spaces:
`get_trait_decl` is keyed by bare leaf alone, `type_implements_trait` compares
markers by `equals`. Both are true. Neither is what blocked the migration.

The annotation sites had nothing to migrate **to**. `TraitRef.resolved_name`
exists because a where-clause bound carries its answer; the same question asked
of an impl head reads `path`-free syntax through
`TypeResolutionPasses::extract_trait_leaf`, which returns `NamedAnnotation.name`
and ignores `NamedAnnotation.res` beside it. That looked like the §6 defect —
a consumer re-deriving identity from a spelling — and it is not.
`resolver/name_resolution.cryo` contains the string `trait_annotation`
**zero** times. `visit(ImplBlockNode*)` entered the impl scope, declared
generics, resolved the TARGET through the older span-keyed `record_resolution`,
walked methods, exited. It never called `stamp_annotation` at all.

Measured over the compiler's own source, answering from the leaf and recording
what the identity would have said:

| | AGREE | LEAF-ONLY | DIFFER |
|---|---:|---:|---:|
| before | 40 | **3822** | 0 |
| after one `stamp_annotation` call | **3842** | 20 | **0** |

Every one of the 3822 was `Pending` — no resolver claimed the node — rather than
an answer that was not a definition. The two have different fixes and
`trait_identity` returns them alike, so they were separated at the probe.

**The control on the 3822.** The 40 that did agree were all `Iterator` →
`std::core::iter::Iterator`, so the accessor works wherever a stamp exists;
without that the 3822 reads equally well as a broken probe. The residual 20 are
`implement Trait` BINDING annotations (`ImplTraitAnnotation.bounds`: 12
`Future`, 8 `Iterator`) — the same gap at a different position, not this one.

`stamp_annotation` goes after `declare_generics`, for the reason a parameter
annotation is stamped there: a head spelled like an impl generic is that
parameter, not a type to search for.

**What this does NOT do.** No lookup was flipped, so B1 stands at 196 and the
ratchet remains an unperturbed control for the flip. `TypeAnnotation::trait_identity`
is the accessor the stamp exists to serve and currently has no caller.

#### The flip is blocked on a second slot, and no gate can see it

`method_binding.cryo`'s `bound_leaves` is fed from two sources. Over the
compiler: **267 entries, 267 lookups** — balanced, so the population is whole —
of which **267 are where-clause `TraitRef`s** carrying valid identities (`Copy` →
`std::core::marker::Copy`), and **zero** are inline `<T: Bound>` constraints.

That zero is measured over a corpus that cannot produce the event:
`compiler/src` and `stdlib` contain **no** inline generic constraints, though
`parser/parser.cryo:2584` accepts them. A four-line project using
`implement<T: Greet> Holder<T>` fires the constraint source immediately.
`GenericParam.constraints` is a bare `SymbolStr[]` with no slot to carry an
identity, so re-keying the registry makes that lookup miss **silently**, and the
victim can only ever be user code — the same shape as the `register_trait_decl`
clobber, whose victim is also structurally absent in-tree.

⇒ The registry re-key needs `GenericParam.constraints` stamped the way
`TraitRef` is, and that is an AST change, not a call-site change. It was built
and verified, then backed out with the rest of the attempt below.

#### The registry re-key was ATTEMPTED and BACKED OUT — what is known

The re-key does not fit behind the two blockers above. It was tried, it did not
converge, and the tree was returned to the state this section describes. The
attempt is preserved as a patch rather than left half-applied, because a
partially re-keyed registry misses silently in both directions.

**The defect is real and reproduces.** Two modules declare a trait `Render`;
`main` imports only `Alpha` and writes `implement<T: Render> Holder<T>`. The
program compiles clean, exit 0, no diagnostic — and runs **Omega's** default
body. Controls: the pinned pre-change compiler gives the same wrong answer (so
it is pre-existing, not introduced), and inverting ONLY the two `import` lines
flips it, which pins the mechanism to `register_trait_decl`'s bare-keyed,
last-write-wins registration. **Which method body executes depends on import
order in an unrelated file.**

This is the victim §2a could not produce. That entry justified the re-key on a
user-declared `Copy` colliding with the stdlib marker; this is stronger — no
marker, no stdlib collision, two ordinary same-leaf traits.

**What the attempt established:**

- The trait tables are **two key spaces, not one**. The trait-DECL registry
  answers "which declaration is this trait?" and wants an identity. The
  trait-IMPL tables are `(trait-leaf, target)` DISPATCH indices where the
  target disambiguates — `register_trait_impl_typed` normalizes to the leaf
  deliberately, and `ownership.cryo`'s Drop path pairs with it. Re-keying the
  impl tables was attempted first and is **wrong**; it breaks every operator
  lookup, since `OperatorTraitMap.trait_leaf` names 14 operator traits by
  string literal and resolves them through those tables.
- So the well-known set is **five** (`Copy`/`Send`/`Sync`/`Drop`/`Future`),
  not the ~23 an impl-table re-key would have forced. Operator traits stay
  leaf-keyed by design.
- `traits_implemented_by` returns dispatch leaves, which `get_trait_decl` then
  refuses. The bridge is the impl head's stamped annotation, looked up in the
  SAME table via `lookup_trait_impl(leaf, target)` so the pair cannot disagree
  about what is registered.

**Three hypotheses ruled out for the residual failure** (a re-keyed build
compiles the compiler but miscompiles user programs, `E0358 no method named
'get' on Slice<u8>`, alongside 14 lookups still keyed by a bare name —
`FmtWrite` x8, `Drop` x4, `Display` x2):

1. *Unconverted `register_trait_impl` sites.* Converted; failure unchanged.
2. *`find_generic_trait_default` newly gated on an impl block that may not
   resolve.* Repaired by pairing it with `lookup_trait_impl`; failure unchanged.
3. *Bound stamps resolving to bare names* — `stamp_trait_ref` assigns
   `resolve_scoped_or_at`, documented to return its input when no bare→qualified
   mapping exists. **Measured false:** 200 stamps ran (the control on the zero)
   and every one is qualified — `FmtWrite` → `std::fmt::write::FmtWrite`,
   `Display` → `std::fmt::display::Display`, `Drop` → `std::core::drop::Drop`.

⇒ The 14 bare keys therefore come from `TraitRef::identity()` falling back on a
TraitRef that `stamp_trait_bounds` never reached — an owner outside the four it
walks. **`AssocTypeDeclNode.bounds` is the untested candidate** and is where the
next attempt should instrument first: tag `get_trait_decl` with its call site so
the 14 name themselves, rather than inferring the owner.

#### The second slot was solved, and that solution is NOT in the tree either

`GenericParamNode.constraints` was converted from `SymbolStr[]` to `TraitRef[]`
so an inline `<T: Bound>` and a `where T: Bound` share one representation and
one stamping chokepoint. It verified — the control stamps `Greet` →
`Main::Greet` with its exit code unchanged, and the full suite stayed green —
but it was backed out with the rest of the attempt, since its only consumer is
the re-key. The parser accepts only a bare identifier in constraint position, so
the path is always one segment and a constraint cannot be written qualified.

#### Well-known trait identities — measured, also NOT in the tree

A `GenericRegistry` slot recording the qualified name of the declaration that
claims each of `Copy`/`Send`/`Sync`/`Drop`/`Future`, filled at the registration
site where the name is already import-resolved, so a marker check asks about a
DECLARATION rather than about a spelling. Built and measured, then backed out
with the attempt: the marker comparisons only become identity comparisons once
their callers pass identities, which is the re-key.

Measured while it was in: **2459 marker queries, none against an empty slot**
(1220 `Copy`, 1239 `Drop`). An empty slot is a module-graph fact and not a
"no" — answering "no" off one reclassifies every type at once — so absence must
stay distinguishable from negation.

`Send` and `Sync` were queried **zero** times by this corpus, so their zero says
nothing. The prediction that `Drop` would be the exposed marker, on the grounds
that `std::core::drop` is absent from the prelude's re-export list (§8.2t), was
wrong: prelude re-export is not what puts a module in the graph.

Scope note for whoever picks this up: the well-known set is **five**. It is only
five because the trait-IMPL tables stay leaf-keyed. Re-key those and it becomes
~23, because `OperatorTraitMap` names all 14 operator traits by string literal
and resolves them through exactly those tables.

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

#### Both import forms disagreed with the rule, and with each other — MEASURED 2026-08-30

`export` grants an importer what the same path would grant if it were imported
there. `process_import` implemented that twice, and neither copy was the rule.

**Only a STATIC CALL can observe either defect.** A type annotation resolves
through module-level reachability, and `instance.cryo` closes `ns_imports` over
re-exports without limit, so an annotation passes at any depth under either
form. Every re-export project asserted through an annotation —
`reexport_basic`'s cryoconfig says so in as many words, calling the annotation
"the load-bearing assertion" — which is why both defects survived six projects.

| depth | `import M;` | `import M::{ N };` |
|---|---|---|
| 1 hop | resolves | **E0900** |
| 2 hops | **E0900** | **E0900** |

- The `Specific` branch read no re-export edge at all. `lookup_in_module` scans
  a module's own export table; the sub-module path it fell to builds `M::N`,
  which is not a module, so the name bound nothing.
- The `Wildcard` branch read `ModuleInfo.reexports`, which holds ONE hop, so a
  facade naming a facade granted nothing.

`ModuleGraph::reexport_closure` now answers the question once, transitively,
and both branches read it: the offered set is a module's own exports UNION what
it re-exports. The result is its own worklist and visited set, so mutual
re-export terminates — it cannot terminate on acyclicity, which the edge rule
deliberately permits. A re-exported name keeps the module that DECLARED it, so
one declaration never acquires two qualified names.

Coverage followed the defect rather than the fix: the brace-list form and a
depth-2 static call were the two unexercised positions, and a project that
asserts only through an annotation cannot fail on either.

### 8.5 §5.2 was never implemented: lanes are duplicated code, not a parameter — MEASURED 2026-08-13

§5.2 has specified `resolve_path(segments, ns, scope)` with **`ns` as a
parameter, not a separate code path** since this document was written. The code
has never done it, and the gap was never recorded here, so successive plans
treated spec-mandated work as an optional future refactor.

**There is no namespace concept in the compiler.** No `Namespace` enum exists.
`SymbolKind` (`Variable`, `Function`, `Type`, `Namespace`, `GenericParam`, …)
is a **tag on a result**, not a dimension of a query — it describes what was
found, and cannot be passed to ask *for* something. What exists instead is one
lookup per kind, each with its own path to an answer:

| entry point | call sites |
|---|---:|
| `lookup_type` | 91 |
| `lookup_method_return` | 17 |
| `lookup_func_type` | 14 |
| `lookup_func_return` | 12 |
| `lookup_global` | 3 |
| **total** | **137** |

`get_resolver()` — the re-entry that §7.2's corollary forbids — has **9** call
sites across 7 files, in `sema`, `passes`, and the driver.

**This is the mechanism behind the cascade, and it reframes the remaining
work.** Because each lane re-derives the scope, import and visibility rules
separately, they drift: the annotation lane answers `0` leaf-index hits while
sema's type lane answers **753**, and all 753 come from a single four-step
cascade in `sema/type_utils.cryo::lookup_type_by_sym` — a private resolver
inside sema. That function takes a bare `SymbolStr`, so it holds no node and
can read no `Res`; **guessing is forced by its signature**, and no amount of
stamping fixes a caller that never had a node to stamp. The cascade is not one
bad function to delete, it is the same rules independently re-invented per
lane, and it will regrow in whichever lane is cheapest next.

The §7.2 mechanism-5 locks exist because of this history specifically. Note
what each does and does not cover: the required `ns` parameter makes the
question well-formed, privatization makes a direct call from `sema` fail to
compile, and only the ratchet catches a *newly added* wrapper. Any one of them
alone leaves the cheap shortcut available.

**§5.3 is unimplemented in the same way, and the two are coupled.** The spec
requires `resolve_path` to record the base `Res` **plus a count of unresolved
trailing segments**. The implementation's `Res::TypeRelative` carries no
payload, so the type layer is handed a bare name and must search for the base
that the name layer had already resolved — which is precisely what keeps
`lookup_type_by_sym` alive. Carrying the payload is what lets sema resume at a
stated position and lets that function be deleted rather than fed. Recorded
here because a payload-free marker was briefly adopted on 2026-08-13 before
this section existed; the spec is normative and the code is the defect.

### 8.6 The scope lane answers at every exit, and the flush found the gap the plan had not — MEASURED 2026-08-13

`stamp_module_scope` now answers on every classified exit, `pending_bug_count()`
is wired to `E0900` on an otherwise-successful build, and the surface ratchet
(§7.2 mechanism 5's third lock) is wired as `make lane-check`. B1 is unmoved at
**753** throughout, which is the point: none of this changes what resolves, it
changes what is *recorded* and what is *enforceable*.

#### `binds_in_scope` is not the discriminator, and the 1,199 is not one bucket

The exit that had been described as a single "not a module scope" population of
1,199 splits **1,166 / 33**, and the 33 are the interesting half:

| the segment | count | why it is still type-owned |
|---|---:|---|
| binds in the writer's scope | 1,166 | names a type outright |
| a primitive spelling (`i64::`, `u64::`, `u128::`, `i16/i32/u32`) | 23 | owns members without being declared in any scope |
| a path (`libc::Whence::`, `syscall::Syscall::`) | 10 | its own last segment names the type |

All 1,199 are genuinely `TypeRelative`, so the mapping proposed for them was
right — but it was right for a reason no one had measured, and `binds_in_scope`
alone would have justified only 1,166 of them. The three-way split is kept as
counters, and every member of the non-binding bucket is emitted by name, so a
*fourth* case — a segment nothing in the program owns — shows up as a line
rather than joining a bucket that already has an owner's name on it. That is the
difference between earning the ownership claim and defaulting to it.

#### The four "qualified spellings" were never module paths

§8.2ak's remaining hard case was described as 4 qualified annotation spellings
needing a lookup that can refuse. Read off the audit stream, **all four are
`I::Item`** in `stdlib/core/iter.cryo` — an associated type on a generic
parameter bound by `Iterator`. There is no module `I`, so `Err` would have been
a refusal the name layer has no standing to issue and `Def` was never available.
They are §5.3's case exactly, and they now stamp `TypeRelative`.

The general rule is §5.1's and needs no per-name knowledge: a path's first
segment is the only one resolved in scope, so *who owns the rest* is decided by
what that segment names. A first segment naming a type or a generic parameter
hands the remainder to the type layer. The module-path branch is left unstamped
and emitted by name, because the entry point that could refuse for it does not
exist yet — recording an answer that cannot be produced is how a tool limitation
becomes a claim.

#### The unit receiver was a parser defect, not a missing primitive

The single unstamped bare name was `()`, from `implement trait Drop for ()`.
`ASTTypeSubstituter::rewrite_to_unit` already states the governing rule — the
unit type "is not a Named type and cannot survive as `Named("()")` through the
resolver" — and the receiver synthesis violated it because it types `&this` from
the impl target's TEXT rather than its syntax. §8.2al repaired the keyword half
of that same defect; `()` survived only because it is not a keyword. The
synthesis now builds the empty-tuple annotation a written `()` produces, so the
node stops existing rather than being taught a spelling: offered annotations
2,452 → 2,451, unstamped 1 → 0.

#### Mechanism 4's corollary was unimplemented, and the flush is what said so

With the exits closed, `E0900` fired on the first build — one node reaching
codegen with nothing recorded. **No synthesizer anywhere set `scope_res`**: the
corollary that "a pass that synthesizes a path-bearing node must construct it
with its `Res` already set" was written and never carried out, and the tally had
no way to show it because an unstamped synthesized node is indistinguishable
from an unvisited one until something *requires* the answer. Seven sites in
`sema`, `async_lower` and `call_resolver` synthesize `Type::member` paths
(`Option::None`, `Poll::Ready`, `Poll::Pending`, `Executor::new`,
`Slice::from_raw`, the `?` desugar's `Err` arm, the converter callee); every one
now records `TypeRelative`, which it knows by construction.

Only one of the seven was reaching a `require`, so a fix aimed at the failing
node would have left six live and the gate green. The delayed bug is worth more
than the one diagnostic it produced.

#### The ratchet's inputs are 134 and 8, not 137 and 9

`make lane-check` pins direct per-kind lookup call sites and `get_resolver()`
re-entries, per file, both ratcheting downward with `--update` to re-pin. The
raw greps read 137 and 9; the pinned numbers are **134** and **8**, because a
grep-shaped gate has two ways to pin a number that is not a call site:

- **a commented-out call** — `instance.cryo` carries a commented
  `ctx.get_resolver()`, so pinning 9 would score deleting a real call while
  leaving the comment as progress, and uncommenting it as clean;
- **the owner's own calls** — `decl_index.cryo` defines the five lookups and
  calls them twice internally, which is not the surface other stages reach for.

Unlike `b1-gate.py` this measures the SOURCE, not a build, so it has no
per-host sections (the same tree answers identically everywhere), needs no
compiler or link, and runs on a fresh clone in under a second. It was made to
fail on a simulated increase, a simulated decrease, and a vanished row before
being trusted, and the golden needs the same `.gitignore` negation the B1 golden
does.

### 8.7 `resolve_path` exists, and the namespace was hiding in a kind filter — MEASURED 2026-08-13

`Namespace` (`resolver/namespace.cryo`) and
`Resolver::resolve_path(segments, ns, scope) -> Res` now exist, and
`resolve_type_qualified_name_bare_from` is their first caller. B1 is 753 and
**every counter row is byte-identical** to the measurement before the change,
which is the whole assertion: this moves no answers, it gives the question a
shape.

**The namespace was already in the code, hardcoded as a symbol-kind filter.**
`resolve_type_qualified_name_bare_from`'s scope walk accepted
`Type | TypeAlias | Import` and nothing else. That test *is* a namespace; §8.5's
"there is no namespace concept in the compiler" is true of the vocabulary and
false of the behaviour — every lane had one, written inline, which is precisely
why they could drift apart without anything looking wrong at a call site.
Turning the filter into an argument therefore costs nothing at the call sites
that already agreed with it, and the lanes that disagreed become visible as
disagreement rather than as separate functions.

Two consequences worth recording:

- **`Import` is accepted in every namespace.** An import stands in for whatever
  the exporting module bound, so its kind is unknown until it is followed;
  filtering it per namespace would make an imported name unresolvable in the
  namespace it really belongs to.
- **A generic parameter is a type-namespace answer with no qualified form.** It
  names itself (§6.1), so `resolve_path` reports `GenericParam` while the
  projection back to a qualified-name string has nothing to hand back. The
  string-returning caller treats that exactly as it treated "not found", which
  is what keeps the refactor behaviour-free; a caller that needs to tell the two
  apart reads the `Res`.

**`lookup_method_return` is deliberately NOT routed through the primitive.**
§7.2 mechanism 5 lists it among the five, but §6.2 says method and
associated-item selection cannot move into the resolver, and §6.2 is the section
that is right about it: `lookup_method_return(type_sym, method_sym)` is handed an
already-resolved type plus a member, which is `TypeDependentRes`, not a path.
It is still privatized with the others — privatization is about stopping direct
calls from `sema`/`mono`/`codegen`, and that applies whatever answers underneath
— but its answer stays sema's. Decided 2026-08-13 with the owner; recorded here
because mechanism 5's flat list of five reads as though all five are the same
kind of question, and they are not.

The remaining four are not yet routed either, and the reason is structural
rather than a matter of effort: they return a `TypeRef` read out of a
`DeclarationIndex` map by string key, and they take **no scope**. §5.2's `scope`
is not optional, so giving them the primitive means giving every call site a
scope to pass — which is the call-site migration, not a wrapper. The primitive
landing first is what makes that migration mechanical instead of exploratory.

### 8.8 Name resolution never walked a base-constructor initializer list — FIXED 2026-08-13

With mechanism 4 wired, the first self-host attempt failed at stage 6 of 8 —
the stage-2 compiler could not build the compiler — on
`E0900: 92 path-bearing node(s) reached a stage requiring name resolution with
no Res recorded`.

All 92 were the same construct: a `ScopeResolutionNode` inside a **base-class
initializer list**, `VoidType(id: u64) : Type(id, TypeKind::Void)`.
`MethodNode.base_ctor_args` is walked by sema, codegen, drop insertion, the
substituter, the cloner and the dumper. `resolver/name_resolution.cryo` was the
only pass that did not walk it, so every path written in an initializer list
reached codegen unresolved and was answered from its spelling.

The list is written outside the body and evaluated inside it — the arguments
read the constructor's own parameters — so it is resolved in the function
visitor, after the parameters are declared and before the body, which is the one
place that scope is open. Walking it from the method visitor instead would have
had to open a second scope holding the same parameter names.

#### Two things this says about the instruments, both worth more than the fix

**The B1 corpus cannot see this defect at all.** Every counter row over
`examples/09-json-config` is byte-identical before and after the fix — 293 /
1,199 / 1,166 / 33, B1 = 753 — because that corpus and the stdlib it compiles
use structs and free functions, while class inheritance with initializer lists
is concentrated in the compiler's own AST and type hierarchies. A prediction
that the stamp rows would rise was made before measuring and was wrong, for
exactly the reason §8.2g already recorded about the keystone: the population is
the compiler, and the corpus is not it.

**Only the delayed bug could have found it.** An unstamped node is
indistinguishable from an unvisited one until something *requires* the answer,
so no tally over any corpus would have shown 92 missing answers — it would have
shown 92 nodes that were simply never counted. `require` is what converts a
silent absence into a number, and self-host is what supplied a population big
enough to contain one. This is the concrete case for §7.2 mechanism 4 being an
error rather than a warning: as a warning it would have printed 92 lines into a
log that already prints 331 warnings, and the fixed point would still have been
green.

It also re-states landmine 4 in a sharper form. `make cryo` compiles new sources
with the PIN, so a check newly added to the compiler never runs over the
compiler's own modules; `make test` runs the new compiler over the test tree,
which does not contain this construct in quantity. Both were green while the
self-host was broken.

---

### 8.9 A second lane grew in the constant folder, invisible to both ratchets — FIXED 2026-08-14

The compile-time constant folder (`const_table.cryo::ConstEval`, the one folder
behind `T[N]`, `[v; N]`, `static_assert` and a global initializer) resolved a
qualified name **by spelling**: it interned the written qualifier, probed its
own `by_qualified` map, then the use site's module, then a program-wide leaf
map. That is a three-step cascade, a global leaf index, and no visibility check
— §1's root cause and §6.1's "no state meaning didn't resolve, try something
else", rebuilt from scratch inside a new module.

It failed in the obvious way. The table is keyed by the declaring module's FULL
namespace, and a use site may write any whole-segment suffix, so
`Mesh::GROUND_VERTEX_COUNT` for `namespace Aether::Gpu::Mesh` missed every step.
Measured on the game-engine tree: the fully-written path folded, the suffix did
not, and the boundary was irrelevant — a same-package module failed identically.
The proposed repair was to hoist `modules_written_as` and scan every module for
a suffix match, which is exactly the search §5.1 forbids and the mechanism §8.2
already recorded as `M3 collect_namespace_suffix_matches`, 5,669 calls / **0**
answers, deleted.

**Neither ratchet could see any of it.** `b1-gate` counts instrumented sites and
the new module had none; `lane-gate` counts calls to `decl_index`'s five
per-kind lookups and the new module called none, having built its own indexes
instead. B1 stayed at 214 and LOOKUP at 134 across the whole addition. Both
gates catch a lane that *grows*; a lane that is *born elsewhere* is outside
their population. That is the same blind spot §8.8 found from the other side —
there, an unvisited node; here, an uninstrumented index.

**Three positions had never been name-resolved**, which is why reading the stamp
was not a one-line change:

| position | why the walk was missing |
|---|---|
| `ArrayAnnotation.size_expr` | the annotation walker `stamp_annotation` descends into `Array`'s ELEMENT and not its size — the one expression hanging off an annotation |
| `ProgramNode.static_asserts` | a side list, not the statement stream; TypeResolution already carried a bespoke walk for the same reason |
| struct / union / class FIELDS | all three visitors `declare_field` in a loop and never `accept` the field, so `visit(FieldDeclNode*)` — which stamps the annotation and walks the default value — was reachable from nothing |

The third was found only by following the first: a field's `boolean[N]` cannot
have its size stamped by a walker that never reaches the field.

**The fix** is those three walks plus `fold_scoped` reading `scope_res`:
`Res::Def(ns)` means the scope named a module, so the member is that module's
constant; `Res::TypeRelative(ResBase::Def(q), 1)` means it named a type, so the
member is an enum variant of it. The enum leaf index and the whole spelling
cascade are deleted, not fixed.

**Measured after.** Suffix-qualified constants, cross-module enum-cast
constants, and enum variants through a suffix path all fold, in all four
positions; the fully-written path still folds. A constant in a module that is
compiled but NOT imported is now refused with **E0240 naming the module** rather
than a vague "not a compile-time constant" — Rule 0 reaching a `static_assert`
condition, which it could not previously see. `make test` unit / 176 / 35 green,
lane unchanged, examples 14/14, self-host `FIXED POINT OK` on both halves, and
**zero** mechanism-4 pending bugs, so the three walks cover the population.

**B1 214 → 220.** All +6 is one new pair of rows, `const-table bare leaf
calls/hits`. Nothing regressed: a fallback that was already answering became
*counted*. A bare name is an `IdentifierNode`, which carries no `Res` slot, so
there is no stamp to read and the leaf map is the only answer available. That
row is the residue and the reduction target; it retires when a bare identifier
is stamped, and until then the gate watches it.

### 8.10 The stamp was right and the consumer never read it — FIXED 2026-08-17

Reported as another instance of §8.2a's leaf index, i.e. a name-layer defect.
It is not, and the distinction is the useful part: **the resolver's answer was
correct and the consumer decided without it.**

`Key::E`, for an enum declaring `E = 8`, evaluated to **22377**. That is
`0x5769`, the low 16 bits of `std::math::E`'s f64 bit pattern; the same enum
backed by `u32` read `0x8B145769`. Nothing was diagnosed, and no file in the
program had to import `std::math` — one unrelated module importing it anywhere
was sufficient.

`codegen/visit/ir_generator.cryo::visit(ScopeResolutionNode*)` answers a path in
this order: three function lookups, then `resolve_global(scope::member)`,
`resolve_global_in_scope(member, scope)`, `resolve_global(member)`, and only
then the enum variant. The sixth step matches on the member's **bare leaf**, so
any module's `E` satisfies it. A global resolves as an *lvalue*, so the enum's
own backing width was then applied to that global's storage — the value is a
type-punned read, not a conversion, which is why the bit pattern survives
intact.

**Measured, and it refutes the report's diagnosis.** A `CRYO_PATH_AUDIT` probe
at the node shows `Key::E` stamped **`TypeRelative(Def, 1)`** — §5.3's positive
claim that the qualifier names a type, decided correctly by the name layer. The
node carried the right answer the whole time. This is therefore not a resolution
cascade to be migrated but a consumer reading tables in the wrong order, and it
is the failure mode §6.3 exists to prevent: an answer stored on the node is only
worth what its readers do with it.

**The fix** hoists the enum-variant resolution above the three global lookups.
It decides from sema's `resolved_type`, so a member the enum has no variant for
declines to the lookups below instead of shadowing them, and the function
lookups stay ahead of it so a static method used as a value is untouched.

**A neighbour this exposed, in the constant folder rather than the name layer.**
Giving a qualified path a real reading made it behave like a bare one, which
revealed that both chose the integer-vs-real reading from the EXPRESSION rather
than from the referenced constant's declared type. `const H: i64 = 1 / 2;` is 0,
and a real re-reading of the same initializer answers 0.5 — a value that
constant never held — while `const R: f64 = 7 / 2;` is 3.5 and an integer
reading answers 3. Neither is recoverable from the initializer, so preferring
either fold is wrong in one direction; the declared type is recorded on the
entry instead (`ConstEntry.int_typed`).

**What is still open.** The bare-leaf global step survives, and it would still
answer for a non-enum type member colliding with some module's bare constant
(`SomeStruct::CONST`). Post-fix it answered **0 times** over the 14 examples and
both reduced repros — 5,534 stamp events, 94% of them `TypeRelative` — but the
*pre*-fix count over that corpus was never taken, so this is not evidence that
the step is dead. Gating it on the stamp (a `TypeRelative` qualifier cannot
denote a global) is the root fix and is a behaviour change beyond the defect,
so it is recorded here rather than taken.

### 8.11 A qualified annotation went unstamped because the lookup could not say no — FIXED 2026-08-21

Handed off as one site: a five-segment trait path in an impl head
(`tests/tests/lang/leaf_dispatch_mod_b.cryo:23`), blocked on a module lane that
does not exist, with `ANN-QUALIFIED` reported as firing **exactly once**.

**That measurement was taken over the wrong population.** With
`CRYO_PATH_AUDIT=1` over the test corpus the stream carries **33 events across
14 distinct written names and 6 distinct head modules**, and the bulk are
ordinary two-segment annotations — `thread::JoinHandle` (6), `syscall::` types
(10), `mpsc::Sender`, `hashmap::Entry`. The impl head was **1 of 33**. The
earlier count was read with only `CRYO_TRAIT_AUDIT` set, so it measured a
stream that was switched off.

**The cause is not a missing lane.** `resolve_type_qualified_name_from` returns
its input **unchanged** when it fails, so a canonical answer and a refusal are
the same string. Its callers pass the result straight into an arena lookup, for
which that convention is fine; a caller that must *record* the answer on a node
cannot distinguish the two and so cannot stamp anything. `stamp_named_annotation`
declined every module-qualified path for exactly this reason, and its comment
saying so was correct.

**The fix** splits the walk rather than adding a second one.
`resolve_type_qualified_name_strict_from` returns an invalid `SymbolStr` on
refusal; `resolve_type_qualified_name_from` becomes a thin wrapper that echoes
the input, preserving the contract the arena callers depend on. One walk, two
views — the two cannot come to disagree about what a path names. The annotation
lane calls the strict form from the annotation's **own** module scope, since a
qualified spelling means what the writing file's imports say it means.

| measurement | before | after |
|---|---:|---:|
| `E0900` at `TypeAnnotation::trait_identity` | 46 | **0** |
| `ANN-QUALIFIED`, test corpus | 33 | **5** |
| `M1-AGREE` | 45,266 | 51,148 |
| `M1-REJECT` | 4 | 9 |
| B1 total | 162 | **162 (+0)** |
| `qualifier_agrees` calls / agreed | 5,548 | 5,572 (**both hosts, identical +24**) |

`make test` 2106/178/38 (unchanged from baseline), `selfhost-check`
`FIXED POINT OK` ×2, b1/lane/roster/api-index green, 14 examples, four golden
outputs.

**The `E0900` zero is not evidence that everything stamps.** Five annotations
remain unstamped; they are simply never *required* at `trait_identity`. The
required population was a subset of what the fix covered, which is why one count
went to zero while the other did not.

**What is still open, and it is the substantive part.** The five survivors are
refusals, not misses — `M1-REJECT` rose by exactly five. `leaf_dispatch.cryo`
writes `...LeafDispatchB::Cell` while the leaf `Cell` resolves in the writing
scope to `LeafDispatchA::Cell`; `future_executor.cryo` writes
`thread::JoinHandle` while the leaf resolves to
`std::future::executor::JoinHandle`, which is verbatim the collision
`resolve_type_qualified_name_from`'s own doc comment cites. The walk resolves
the **leaf in the writer's scope and only validates the written qualifier**, so
it can never honour a qualifier that *selects* a different module's same-leaf
type — which is precisely what `leaf_dispatch.cryo`'s header requires ("a
fully-qualified reference must NOT resolve through the bare leaf name").
Refusing beats binding the wrong `Cell`, and those tests pass because the type
layer answers them by another lane; the annotations are merely not recorded.

**A divergence between §5.1 and the implementation, exposed while fixing this.**
`import std::thread;` binds **no symbol named `thread`** — a wildcard import
declares the module's exports directly as `Import`-kind symbols, and only
`import X as Y` binds an alias. `SymbolKind::Namespace` exists solely for
C-import extern blocks, which is what `syscall` is. So `thread::JoinHandle`
cannot be resolved by §5.1's segment walk at all: its first segment resolves in
scope nowhere. §5.1 describes a rooted walk the compiler does not perform for
these paths, and a "module lane" built to §5.1's shape would answer none of
them. Closing the five requires the qualifier to select the module
(`ns_written_as` to find it, then `lookup_in_module`), which changes how the
51k-call M1 path answers wherever leaf-in-scope and the written qualifier
disagree, and raises an ambiguity case — two modules matching one written prefix
— that "module wins in qualifier position" does not settle. Recorded here rather
than taken.

**One inconsistency left in place deliberately.** The head lookup that produces
`TypeRelative` still resolves against the ambient cursor, while the new lane
below it uses the annotation's home scope. Repointing the head lookup is the
§5.2 answer, but it changes which type a bare head binds to in existing
programs, so it is not folded into this fix.

### 8.12 The rooted walk already existed, one lane over — FIXED 2026-08-21

§8.11 left five qualified annotations refused rather than bound: the walk that
ships resolves the LEAF in the writer's scope and only *validates* the written
qualifier, so it cannot honour a qualifier that SELECTS a different module's
same-leaf type. Closing them was scoped as new work — find the module by
spelling, then resolve inside it — with two questions recorded as unsettled:
what to do when the writer cannot reach the module, and when two modules share
the spelling.

**Both were already built, and so was the walk.** `walk_module_rooted_type`
resolves `prefix::leaf` by finding the module the prefix spells and looking the
leaf up in its EXPORT set; `modules_written_as` returns how many modules carry
the spelling (`matched`) and how many of those the writer can reach
(`visible`), refusing an ambiguous spelling and reporting an unreachable one as
E0240 with the import that would fix it. The `ScopeResolutionNode` lane has used
this since §8.2m. The annotation lane simply never called it.

**The fix routes the annotation lane through it**, and decides ownership by
what the head names rather than by which lane answers first: when any module
carries the written prefix the module lane owns the path and either answers or
refuses. It is deliberately not handed on to the leaf reading afterwards — that
reading resolves in the writer's scope and would answer straight past a module
that is unreachable or ambiguous, recording a name the source may not legally
spell as though it were fine.

| measurement | before | after |
|---|---:|---:|
| `ANN-QUALIFIED`, test corpus | 5 | **0** |
| qualified annotations answered by the rooted lane | 0 | **33 of 33** |

Emitted with the definition each landed on, because the point of rooting a path
is *which* of two same-leaf types it names and a tally cannot show that the
qualifier selected rather than the leaf:

```
CryoTests::...::LeafDispatchB::Cell -> CryoTests::...::LeafDispatchB::Cell
CryoTests::...::LeafDispatchA::Cell -> CryoTests::...::LeafDispatchA::Cell
thread::JoinHandle                  -> std::thread::JoinHandle
```

The third is written in a file whose own module declares `executor::JoinHandle`;
it was refused before. `make test` 2106/178/38 unchanged, `selfhost-check`
`FIXED POINT OK` ×2, b1/lane/roster/api-index green, 14 examples, four goldens,
LSP builds.

**A measurement taken and discarded, recorded because it was wrong in a way
that would recur.** Before finding the existing lane, a rooted reading was built
in the resolver and run as a divergence probe against the shipping one over
three populations: 161,893 agreements, 15,892 cases the rooted reading answered
and the shipping one did not, and **zero** where it lost an answer or bound a
different definition. That looked like licence to replace the shipping walk
outright. It was not: the probe resolved a module by spelling **without asking
whether the writer could reach it**, so its "would answer" rows counted paths a
correct walk refuses with E0240. The probe was deleted rather than kept.

Its controls survive as the useful part. Agreement is forced whenever only one
module declares the leaf, so of those 161,893 agreements only **3,323** were
plural-leaf rows and therefore evidence at all — and the 14 examples contributed
**zero** of them, every agreement there being a forced singleton match. A
corpus can produce six figures of agreement and no evidence.

**B1 did not move, and the prediction that it would was wrong.** The rooted lane
fires **0** times over `examples/09-json-config`, so `qualifier_agrees` stays at
5,572 and the golden is untouched. That also settles that the leaf reading is
still live and is not dead code to be deleted. What the instrumentation cannot
say is whether the qualified branch is reached there at all or is reached and
answered silently by the leaf reading; both emit nothing.

**Settled: a qualifier names whichever entity OWNS the next segment.**

The earlier reading that the disagreement is unreachable was wrong. It is
reached constantly: the module and the type do not share a full name, so no
E0200 arises. `namespace A::B::N;` declaring `type N` gives a module `A::B::N`
and a type `A::B::N::N`, and in QUALIFIER position both are spelled `N`. The
file-per-type convention generates this systematically — 53 distinct qualifier
names in the compiler's own source, answering 2,524 times.

Neither precedence is correct, because the two cases want opposite answers and
each breaks the other:

| rule | breaks |
|---|---|
| module wins | 53 names — `TypeRef::invalid()` looks for `invalid` in the module |
| type wins | `ModuleGraph::home_ns_of_file` — a namespaced static the MODULE owns |

What separates them is not precedence but ownership: a path names the entity
that owns its next segment. A scope segment binding in the writer's scope as a
type is stamped `TypeRelative` — unless a module spelled the same way exports
the member, which the module's export set answers directly. One question, asked
once, with a defined tiebreak; no fallback chain, and no name is special-cased.

A segment binding to something that is not a type — a module-qualified free
function such as `metadata::metadata(..)`, whose leaf binds as a `Function` —
is not a type claim and is left to the module lane. An imported name binds as
`Import` and records only the target's qualified name, so the target is reached
through its owning module's export set and ITS kind decides; the type index
cannot serve this, being unpopulated until a later pass.

### 8.13 B1's residue is not name resolution's to answer — MEASURED 2026-08-21

B1's target is zero (§7.3) and it stood at 162 with no attribution: the gate
reports a total and a per-site call/hit pair, and nothing said WHICH names still
needed a fuzzy fallback. The assumption on record was that where-clause trait
bounds dominated — four of the six `lookup_by_leaf` call sites resolve
`gp.constraints[k].written_leaf()`.

**Attribution was already in the counter and simply never read.** Every call
site carries its own site counter, and the report prints them:

```
B1  lookup_by_leaf hits                       156
      by caller: sema type_utils              156
      by caller: type_resolution bound          0
      by caller: resolver generic bound         0
      by caller: symbolic_checker               0
      by caller: resolve_named step 5           0
```

All of it is ONE site, `sema/type_utils.cryo:173`, the last step of
`lookup_type_by_sym`. The four where-bound sites answer zero. No signature
change was needed to learn this.

**What the names are settles which bucket they belong in.** A per-name line at
that site, over four populations:

| population | leaf-index hits | instantiations | written source names |
|---|---:|---:|---:|
| `examples/09-json-config` (the B1 corpus) | 156 | 156 | 0 |
| 14 examples | 1,145 | 1,145 | 0 |
| the compiler's own source | 937 | 937 | 0 |
| test corpus, **as first measured** | 0 | 0 | 0 |
| test corpus, **re-measured on a clean build** | **76** | 0 | **76** |

On the first three, every hit is a mangled instantiation —
`6String$LN$L3std.5alloc.9allocator.11GlobalAlloc$G$G` (`String<GlobalAlloc>`),
`5Slice$Lh$G` (`Slice<u8>`) — and none is a name any source file contains. That
is the B4 case in §7.3, and it confirms a warning already on record: a `Res`
cannot key an instantiation, so nobody should stamp their way to B1 = 0.

**The test corpus's zero was not a real zero, and the control did not catch
it.** That run reused a CACHED stdlib build - only the test executable had been
deleted, not the build directory - so no stdlib module was recompiled and none
of its lookups were measured. `stdlib/net/tls` is compiled by no other corpus.
The control that was run (the same binary reporting 156 on the B1 corpus) proved
the audit LINE was live; it said nothing about whether this population had been
compiled, which is the thing that was actually in doubt. **A control has to
exercise the axis in question.**

Re-measured against a wiped build directory the same corpus reports **76 hits,
all one name: `Poll`** - `type enum Poll<T>` in `stdlib/future/poll.cryo`. So
the leaf index is NOT answering instantiations alone, and §7.3's B4 is defined
more narrowly than what the bucket actually holds.

**What those 76 are.** The asking modules - `std::net::tls::context` and two
async test modules - do not import `std::future::poll`, and the diagnostic spans
point at the `async` keyword: the references are SYNTHESIZED by async lowering,
not written by anyone. `AsyncLower::poll_pending` already calls
`set_synthesized_ref(poll_ty, definition_name_of(poll_ty))`, so the node carries
its identity and a consumer resolves it by name regardless - the §8.10 family,
where the stamp was right and the reader ignored it. `option_none` builds the
same shape and survives only because `Option` is in the prelude and `Poll` is
not. The leaf index is therefore load-bearing for exactly one synthesized name,
and the fix for it belongs in that consumer rather than in `type_utils`.

**The 6 that WERE name resolution's, fixed.** The remainder of B1 was
`const-table bare leaf`, and it was a single constant: `READ_TO_END_CHUNK`,
asked from `std::io::stdio` (4) and `std::fs::file` (2). Both import
`std::io::traits`, where it is declared, so no import was missing. It is used as
an array size in a trait DEFAULT body — `mut scratch: u8[READ_TO_END_CHUNK]` —
and a default's body is written in the trait's file while its owner is the
implementing type. `TypeResolver::array_size_of` built its `ConstEval` from
`ctx.current_module`, the ambient cursor, which during that instantiation names
the IMPLEMENTING module; the qualified lookup then missed and fell through to
the program-wide bare index, which answers from any module with no import in
hand.

Three of the four `ConstEval` construction sites already took provenance from
the node's own span, each with a comment saying why. This was the fourth.
`TypeResolver` already held `module_graph` for exactly this mapping and never
used it. Fixed by asking `ns_sym_of_file(a.span.file)` and falling back to the
cursor only when the file is unknown — an unknown file must not become "written
somewhere else", the false-positive direction that reverted the visibility gate
(§8.1e).

`ctx.home_module` was tried first, on the theory that the field documenting this
exact defect class would be populated here. It is not: the count stayed at 6,
which is what ruled the cheap fix out.

**Measured: const-table calls AND hits 6 → 0** — both, so the site is no longer
reached rather than reached and no longer recorded — **B1 162 → 156 on both
hosts**, array sizes still fold to 4,096 (`[4096 x i8]` allocas in `stdio.ll`,
`file.ll`, `cursor.ll`; no zero-length array anywhere in the stdlib IR),
`make test` 2106/178/38 unchanged.

**The gate now says so.** `LeafCalls`/`LeafHits` are tagged `B4`, the cascade
step-5 row `B4*`, and `LeafHits` is no longer a summand of B1; the counter
prints a fourth bucket line and `b1-gate.py` parses, renders and asserts
`B4_TOTAL` beside `B1_TOTAL`. Both hosts re-pinned:

```
b1-gate: OK -- B1 = 0, B4 = 156, 20 sites, matches golden
```

**B1 is zero.** Not "small enough to live with" - the fuzzy-fallback bucket
whose stated target is zero measures zero, on both hosts, with every per-site
row still asserted underneath it. B4 is pinned as a floor so growth in either is
caught separately, and the two are deliberately not summed.

Both halves of that reading turned out to be incomplete: there were TWO
consumers, not one (§8.15), and the reason a synthesized `Poll` could reach an
index keyed by bare leaf at all was that the lowering asked the USER's module
what `Poll` meant (§8.16).

**What this costs, stated because the gate itself warns about it.** `b1-gate.py`
requires its target to exercise "enough cross-module resolution for B1 to be
nonzero - a target with no B1 events would give a gate that cannot observe
regrowth". B1 = 0 trips that condition. It is not newly broken: every remaining
B1 summand already read zero before this change, so the corpus stopped
demonstrating B1 liveness some time ago and the total was only nonzero because
it carried B4's rows. What proves the leaf machinery still records is B4 = 156;
no equivalent control exists for the other B1 summands, and a corpus that
exercises one of them would be worth more to this gate than a lower number.

### 8.14 The bare mangled spec name is not an identity — ATTEMPTED AND REVERTED 2026-08-21

An attempt to retire B4 by replacing the leaf search at
`sema/type_utils.cryo:173` with an exact arena lookup. The premise: the three
steps above it all ask the `DeclarationIndex`, instantiations live in the
`TypeArena`, and `TypeArena::reserve_spec_names` registers a specialization
under its bare mangled spec name as well as its qualified one - so the key
looked as though it were already there and the leaf index were doing keyed work
by search.

Measured first, over three populations: **2,238 hits, all EXACT-SAME, zero
misses, zero differing ids.** On that evidence the leaf step was deleted rather
than kept as a fallback, on the reasoning that a leaf index maps a bare name to
whichever qualified name registered FIRST and so must never be a second chance.

**`resolution_leaf_index` failed**, which is the test that exists for this:

```
expected `ResolutionLeafIndex::Alpha::Widget<i64>`,
found    `ResolutionLeafIndex::Omega::Widget<i64>`
```

**The bare mangled spec name carries no module.** `5Widget$Li$G` is the whole
key, so two modules instantiating one generic write the same cache slot and the
last one wins. `lookup_by_name(bare_spec)` is the same first-registration-wins
guess as the leaf index with a better name on it, and swapping one for the other
buys nothing.

**Why 2,238 agreements were not evidence.** Agreement is forced wherever a
generic is declared once, which is true of every generic in the stdlib, the
examples and the compiler itself. The corpus that declares one generic in two
modules is the test corpus - and it was the population the probe did not cover.
The same control that the rooted walk (§8.12) applied correctly - *count only
the rows where the two mechanisms COULD disagree* - was not applied here.

Reverted in full. B4 stays. The key an instantiation actually needs is the
QUALIFIED spec name; what is missing is not a lookup but the qualification,
which the caller has already lost by the time it reaches this site.

### 8.15 The leaf index's last written name was a stamp two consumers never read — FIXED 2026-08-21

§8.13 left one non-instantiation name in B4: `Poll`, reaching the arena leaf
index from modules that never imported `std::future::poll`. The references are
synthesized by async lowering, which stamps them
(`set_synthesized_ref(poll_ty, definition_name_of(poll_ty))`), so the answer was
already on the node and a consumer re-derived it from the spelling anyway - the
§8.10 family.

**Two consumers, not one.** The handoff named `Sema::resolve_scope_resolution`,
which opens on `lookup_type_by_sym(scope.scope_name)`. It accounted for 7 of 13
hits on a two-`await` repro; fixing it alone left 6. Probing every caller of
`lookup_type_by_sym` by tag found the rest in
`CallResolver::lookup_scope_variant_payload_types` - the payload-type hint for
`Poll::Ready(v)` - which asks the same question the same way. `Poll::Pending` is
a value and `Poll::Ready(v)` is a call, so the two shapes leave through
different doors and both had to be told to read the stamp.

**The fix** is `TypeUtils::scope_qualifier_type`, next to the cascade it
precedes. `TypeRelative` is the name layer's positive claim that the
qualifier names a type and it carries WHICH - a canonical name that already
accounts for the writing module's imports - so the type is looked up by that
name. Every other answer returns invalid: a qualifier naming a MODULE is not a
type claim, and a primitive or type-parameter base names no declaration. A stamp
that claims a type the index does not carry emits `SCOPE-TY-STAMP-MISS` rather
than searching under a second key; it measures **0** on every population run.

**`lane-check` caught the lookup this added, which is what it is for.** The
first version asked `decl_index.lookup_type` directly and took
`type_utils.cryo` from 10 to 11, an increase the gate refuses. The question the
stamp reader asks - *what type is registered under exactly this canonical name*
- is the same one the cascade's already-qualified step asks, so both now go
through one `lookup_type_exact`, and the surface is unchanged at 132 rather than
re-pinned. Two callers of one primitive is the shape that keeps them from
drifting into different answers.

**Measured**, on a repro with two `await`s and `std::net::tls` imported:

| | leaf hits | of which `Poll` | written names |
|---|---:|---:|---:|
| before | 125 | 13 | 13 |
| `resolve_scope_resolution` only | 118 | 6 | 6 |
| both consumers | **112** | **0** | **0** |

No written source name reaches the leaf index on that corpus any more; all 112
remaining hits are mangled instantiations, which is B4 by mechanism (§8.14).

**B1 and B4 are unchanged at 0 and 156.** `examples/09-json-config` contains no
written name that reached the leaf index, which §8.13 had already measured, so
the gate's own target could not move - and it did not. What DID move is
`M1 CHECK qualifier_agrees`, **calls and agreed both 5572 -> 5566**. Equal
movement means no answer changed, only that six questions stopped being asked:
the stamp answers before `lookup_type_by_sym`'s cross-module tier runs, and that
tier is where `qualifier_agrees` lives. Diffing the `M1-AGREE` stream names all
six as one path - `libc::Whence` written in `std::fs::file`, resolving to
`std::ffi::libc::Whence`, which is exactly what the stamp carries. Nothing
appears in the fixed stream that was not in the baseline.

**A control that had to be built twice.** The first attempt to show the emitted
code was unchanged hashed an empty file list and reported "identical"; the
digest turned out to be `md5sum` of empty stdin. The second attempt compared
executables, and the determinism control - the same compiler run twice - showed
the PE differs run to run. Neither comparison was evidence. Byte-identity here
belongs to `selfhost-check`, which knows how to ask for it.

### 8.16 The async lowering let the user's module decide what `Poll` meant — FIXED 2026-08-21

Found while testing whether §8.15's leaf-index answer was load-bearing or
merely redundant. It was neither: the same root that fed the leaf index also
produced a wrong answer, and both compilers - before and after §8.15 - failed
this program.

```cryo
namespace App::Shadow;
public type struct Poll { marker: i64; }        // an ordinary user type
```

with an `async function` anywhere in a module that can see it:

```
error[E0900]: unresolved generic instantiation after monomorphization:
              'App::Shadow::Poll<?>'
error[E0358]: no method named `is_pending` found on type `App::Shadow::Poll<i64>`
error[E0201]: cannot find value `Poll::Pending` in this scope
```

Five errors, none of which names `async`, all of them about a struct the author
declared correctly and never asked to be a future.

**The cause.** `AsyncLower::lookup_future_type` resolved `Poll`, `Context`,
`Executor` and `Option` **through the import scope of the module being
lowered**:

```cryo
const qual: SymbolStr = this.ctx.resolve_scoped_or(leaf_sym, FILE, LINE);
return this.ctx.decl_index.lookup_type(qual);
```

These are types the lowering itself emits references to. What they denote is
fixed by the language, so asking the user's module what the leaf means is asking
the wrong scope entirely - and the two ways that goes wrong are the two symptoms
already on file: the scope does not carry the name, so the answer falls through
to whatever index answers bare leaves (§8.15's `Poll` in B4), or the scope
carries a DIFFERENT `Poll`, and the state machine is built against it.

The synthesizers had the same defect one level down: `poll_pending`,
`make_poll_ready` and `poll_type_ann` SPELL their nodes with the bare leaf
`Poll`, and a synthesized node's `span.file` names the module it was lowered
INTO. Every consumer that resolves a scope segment by spelling - including
`resolve_scoped_or_at`, which is careful to judge by the WRITING module - is
therefore handed a module that never wrote it.

**The fix** names all four types canonically (`std::future::poll::Poll`,
`std::future::waker::Context`, `std::future::executor::Executor`,
`std::core::option::Option`), following the tree's existing practice for
well-known stdlib symbols (`std::collections::array::Array`,
`std::test::runner::run_all`), and spells the synthesized nodes with the
canonical name rather than the leaf. `POLL_TYPE` names it once.

The `add `import std::future;`` diagnostics stay correct and stay reachable:
module discovery is import-driven, so a program that imports the module nowhere
does not compile it, and the declaration is then absent from the index - which
is now the only thing those lookups can fail on.

**No import requirement was relaxed.** A prelude-only async project compiled
before this change and compiles after it; `future` is not in the prelude, so
those diagnostics were already unreachable for any program whose stdlib was
built. The change removes the hijack, not a check.

**Measured.** The shadow program above now compiles and runs correctly. B1 and
B4 are unchanged at 0 and 156.

**An over-reach this caught, recorded because the comment that warned about it
was right.** A first attempt also made `try_resolve_type_or_variant` read the
stamp. `M1 CHECK qualifier_agrees` rose by 283 on `examples/09-json-config` - a
corpus with no `async` in it at all - which is what said the change was doing
something other than what it was for. The comment at that site already states
why: a scope segment there can arrive already mangled
(`6Result$Lu_N$L...`), the registries are keyed by that instantiation, and a
`Res` names the definition for every instantiation of it. Guarding on written
generic args does not cover the mangled-spelling case. Reverted; the row
returned to -6. Fixing the SPELLING at the synthesizer solves the same symptom
without asking that site to distrust its key.

---

### 8.17 A module-level `mut` global was stamped as a LOCAL — FIXED 2026-08-27

Found while scoping §5.2's remaining work (the per-kind lookups), by asking what
`sema/type_utils.cryo::lookup_global_var` would read if it read a stamp instead
of guessing.

**The defect.** `bare_name_res` mapped `SymbolKind::Variable` straight to
`Res::Local`:

```cryo
if (s.kind == SymbolKind::Variable || s.kind == SymbolKind::Parameter) {
    return ResSlot::Answered(Res::Local(sym_id));
}
```

`Variable` is the kind for **both** a function-local binding and a module-level
`mut` global. Name resolution's own module-level walk makes that split itself —
an immutable global declares as `Constant`, a mutable one as `Variable` — so the
two arrive at `bare_name_res` indistinguishable by kind. `Res::Local` carries a
`SymbolID`, while a global is reached by canonical name in every index a later
stage consults, so the stamp handed those stages a key nothing is stored under.
`Res::Local`'s own definition says what it is for: *a local binding (parameter
or local variable)*.

The asymmetry is why this survived: an immutable global fell through to
`Res::Def(qualified_name_of(s))` and was right all along. Only the mutable half
was wrong, and it is the smaller half — **31** module-level `mut` globals across
stdlib and the compiler against **1,256** consts.

**The fix is the declaring scope, because nothing else separates them.**
`SymbolKind` cannot, and the spelling cannot. `ScopeKind::is_module_level()`
(`Global` or `Module`) is the discriminator; a `Variable` bound anywhere else is
a local and still stamps `Res::Local`.

**Measured, both directions.** A byte-identical result alone would be equally
consistent with the new branch never executing, so it was instrumented before
being believed:

| control | result |
|---|---|
| new branch fires (probe, `examples/01-hello` alone) | **26** stamps over **9** distinct globals |
| the scope those globals bind in | **26 / 26** `ScopeKind::Module` |
| the name each now carries | canonical — `g_seed_state` → `std::collections::hashmap::g_seed_state` |
| codegen, all 14 examples, every object | **800 / 800 byte-identical** |

The zero in the last row is therefore measured over a population the first row
proves is not empty. It is zero for a stated reason: the slot's only consumer
today is `CallResolver::lookup_scope_template`, which answers `null` for
`Res::Local` (no matching arm) and `null` for `Res::Def(q)` (no template is
registered under a global's qualified name). A global named like a generic type
would make these differ, which is what the object comparison was checking; all
31 are `g_*`/`*_global`, none type-shaped.

Gates at the change: `make test` OVERALL PASS (unit 2106, compile-fail 178,
projects 38), `b1-check` B1 = 0 / B4 = 156, `lane-check` LOOKUP 132 / REENTRY 6,
`roster-check` 2106, `vendor-check` 9, `api-index-check` clean.

#### What this was scoping, and what it found instead

The per-kind lookup surface is **132** call sites, and the shape of the
migration is not what a count of them suggests:

- **Only 1 of the 132 reads a `Res` today.** The stamps that landed over the
  preceding sessions are almost entirely unconsumed by the type and codegen
  layers, which is mechanism 4's half that is not yet cashed in.
- **The map reads are downstream of the real lane.**
  `CompilationContext::qualify_symbol_sym` — `this.namespace_str` plus string
  concatenation, the ambient cursor §5.2 says must not exist — has **92** call
  sites, and **33** of the 132 lookups take their argument from it inside the
  same function. Routing `lookup_type` through the primitive without deleting
  the cursor would move the guess one frame up, not remove it.
- **The registration side already writes both keys.** A global is registered
  bare *and* under `qualify_symbol_sym`'s spelling, which is what the
  qualified-then-bare fallback in `lookup_global_var` mirrors. Reading a stamp
  makes the bare key unnecessary rather than merely unpreferred.

`lookup_global_var`'s fallback is not deleted by this change. Its three callers
split: one holds an `IdentifierNode` and can read `ident.res` now that the stamp
is correct, and the other two sit inside `resolve_scope_resolution`'s six-step
cascade, which has to be restructured rather than re-pointed. That cascade is
the actual unit of work behind "the global lane", and it is its own change.

### 8.18 A module-level global was never EXPORTED, so the name layer could not answer for one — FIXED 2026-08-27

§8.17 fixed what a global's stamp *says*. This is why so many globals had no
stamp at all, and it is the reason the global lane had a fallback at all.

**`forward_declare_node` exports every declaration kind except one.**

| kind | export |
|---|---|
| Function | `if (is_public) export_symbol(sym_id)` |
| Struct, Union, Enum, Class, Trait, TypeAlias | `export_symbol(sym_id)` |
| **VariableDeclaration** | **nothing** |

`lookup_in_module` reads the export set, so a module-level `const` or `mut`
global was **unreachable across modules by the name layer at all**. That is the
mechanical origin of the bare-leaf global map: with the name layer unable to
answer, a later stage had to key globals by leaf, and a leaf key is
last-write-wins across modules. The fallback was not a shortcut someone took —
it was the only thing that could answer.

A second gap sat behind it: `Symbol::variable` hardcoded
`SymbolVisibility::Private` while `Symbol::constant` sets `Public`. The language
is public-by-default, so a module-level `mut` global was private for no stated
reason, and `lookup_in_module` filters on `is_public()`. `is_public` is now a
parameter of `declare_variable` — true at the one module-level call, false at
the four local ones — so a local is unaffected.

#### What landed

`resolve_identifier` reads `IdentifierNode.res` through
`TypeUtils::spelling_global`, where "no answer" and "answered, but not a
definition" are ONE outcome, because a bare name may be a local, a builtin or a
function and none of those is a failure. `TypeUtils::lookup_global_var` — the
ambient-cursor qualify followed by a bare-name retry — is **deleted**.

Measured with a tripwire computing both answers and returning the old one:

| corpus | rows | outcome |
|---|---:|---|
| 14 examples | 4,509 | 100% SAME |
| compiler, 245 modules | 1,645 | 100% SAME |

0 REBIND, 0 REGRESS, 0 GAIN over **6,154** rows. SAME requires the stamp to have
answered, so the run is its own positive control rather than a count of silence.

**The tripwire's first run found defects rather than confirming the change.**
32 rows regressed, every one with an empty stamp — a cross-module bare reference
that compiled only through the leaf fallback:

- `INTEREST_READ` / `INTEREST_WRITE` (`std::future::reactor`) and
  `BLOCKING_DEFAULT_THREADS` (`std::future::blocking`), read from **23 sites**
  across `net::socket::tcp`, `net::socket::udp`, `net::tls::future` and
  `process::child`. Fixed by the export gap above.
- `g_logger`, `g_stderr`, `g_compiler_debug`, declared by `Utils`
  (`utils/_module.cryo`) and read from `Utils::Logger` (`utils/logger.cryo`),
  which did not import `Utils`. A child namespace is a separate module and does
  not inherit its parent's scope, so nothing bound those names. Fixed by adding
  the import the file always needed.

#### What did NOT land, and the zero that was measured over the wrong population

`resolve_scope_resolution`'s three global tiers — the spelling-derived
`scope::member` key, a search for a module whose namespace *ends in* that leaf,
and the bare leaf — were replaced by one exact lookup on the namespace the stamp
carries, and **reverted**.

The measurement said it was safe: 632 rows over the examples and 422 over the
compiler, **100% SAME**, with the spelling-derived tier answering **zero** times
and the bare-leaf tier never reached. The test suite then failed to build:

```
error[E0201]: cannot find value `probe::BINDGEN_PROBE_PLAIN_CONST` in this scope
 --> tests/lang/c_import_libclang.cryo:52:15
```

**Neither corpus contains a C import.** The spelling tier is exactly what
answers for a C-imported constant, and the population that exercises it is in
`tests/`, which the tripwire never ran over. A zero measured over 1,054 rows was
still a zero over the wrong population — the failure mode §7 keeps recording,
reproduced here in full.

The underlying defect is a **key-space split**, and it is why one key cannot yet
serve: for `extern module probe := "C" { ... }`, the C-header import engine
registers the constant under the BARE alias (`probe::NAME`, via
`qualify_binding_sym`), while the name layer's canonical name for that same
alias symbol is fully qualified (`CryoTests::…::probe`). One entity, two key
spaces. Unifying them is the prerequisite for this lookup, and it is its own
change — it moves what the C-import engine registers, which codegen mangling
also reads.

Kept from the attempt: `TypeUtils::lookup_global_exact`, the single exact
lookup both the cascade and `spelling_global` now share.

Gates: `make test` OVERALL PASS (unit 2106, compile-fail 178, projects 38),
`b1-check` B1 = 0 / B4 = 156 unchanged, `roster-check` 2106, `vendor-check` 9,
`api-index-check` clean, and **every object of all 14 examples byte-identical**
to the pre-change compiler (800/800). `lane-check` LOOKUP **132 → 131**, the
deleted `lookup_global_var` call sites, re-pinned deliberately.

### 8.19 The ambient cursor could not displace an exact answer, and now cannot — MEASURED AND FIXED 2026-08-27

Found while sizing §5.2's remaining work: `sema/type_utils.cryo::lookup_type_by_sym`
answers one question — "what type does this name mean?" — in four ways, and only
the last of them was tallied. B1 = 0 says the *enumerated* fallback sites answer
nothing; it never spoke for these three.

**The instrument.** `lookup_type_by_sym` takes a `site: string` (threaded through
all 19 call sites, as `get_trait_decl` does) and emits one `TYPE-CASCADE` row per
call on **every** exit, gated on `CRYO_PATH_AUDIT`:

```
TYPE-CASCADE <site> <step> <asked> <answered-as> <control>
```

Every call leaves through exactly one exit, so **row count == call count**. That
identity is the control: without it, a step reading zero cannot be told from a
stream nothing reached — the reading error §7 keeps recording.

**The cross-check.** On `examples/09-json-config` the leaf step reports 156,
which is exactly the pinned `B4_TOTAL` / `lookup_by_leaf hits` row. Two
independent instruments, one number.

**What it measured**, over 176,161 calls on four corpora (09-json-config, all 14
examples, `tests/tests/projects/ffi_c_import`, and the compiler's own source):

| step | share |
|---|---:|
| exact — already canonical, no widening | 50.5% |
| ambient cursor | 8.2% |
| cross-module scope walk | 4.9% |
| arena leaf index | 1.3% |
| answered nothing | 35.1% |

Half of all calls were never fallback. A third answer nothing at all — several
callers use the cascade as a probe, where a miss is the answer. Fallback proper
is 13.1%.

**The finding.** Of 14,411 answers from the ambient-cursor step, the exact lookup
would have answered **SAME 0 times and DIFF 0 times** — it had nothing, every
time. The two steps never both had an answer.

The comment that justified running the cursor first read: *"Current module's
qualified name FIRST so module-local types are preferred over bare-name aliases
from other modules."* That preference was never exercised. It described a
conflict the cascade does not have.

**The fix.** The exact lookup now runs first. The cursor is reached only when
nothing is registered under the written spelling, so it can widen and cannot
rebind — and that is now true by construction rather than by measurement. The
`cursor_control` probe was deleted with it: once the order enforces the property,
a helper that still measured it would be recording a fixed answer as evidence.

This is what makes the remaining call-site migration safe to reason about. A
site that replaces the cursor with a stamp read cannot be changing a binding,
because the cursor was never reached for a name the index already carried.

**Prediction stated before the change, and met.** If SAME = DIFF = 0 is a
property rather than a corpus accident, the two populations are disjoint and
swapping the order repartitions nothing — every per-step count must be unchanged,
only relabelled. Measured on 09-json-config: 4,419 / 627 / 267 / 156 / 2,553,
identical in every row.

**Codegen control.** `.o` hashes, not `.exe` — a PE link timestamp makes an
executable hash useless as a codegen control on Windows. Same-binary-twice first
(61 objects, identical, so the hash is deterministic), then pre- vs post-change:
800 objects compared, 800 identical. 61 of those were 09-json-config, whose
baseline the measurement itself had overwritten, so **739 are genuine
pre-vs-post evidence** and the remaining 61 rest on the step-count identity above.

**Gates:** `make test` OVERALL PASS, `b1-check` B1 = 0 / B4 = 156 unchanged,
`lane-check` LOOKUP 131 / REENTRY 6 unchanged, `roster-check` 2106,
`selfhost-check` FIXED POINT OK x2. No golden moved, and none needed to:
`b1-gate.py` does not set `CRYO_PATH_AUDIT`, so the pinned rows cannot see the
new stream.

**Two things this exposed that are NOT fixed here.**

- **The bare-leaf fallback under a scope qualifier WAS the de-facto module/type
  precedence rule, and is now empty.** `resolve_scope_resolution` and
  `lookup_scope_variant_payload_types` read the stamp first and fell back to a
  spelling lookup when it declined; that fallback answered 2,524 times, every
  one a module/type collision such as `TypeRef -> Compiler::Types::TypeRef`.
  Once the stamp answers by OWNERSHIP rather than by precedence (§8.12), the
  two sites fall to 24 together. The fallback stays for the names the stamp
  still makes no claim for, not as a second attempt at a question it answered.

- **A zero over `examples/` is still not a zero.** The
  `resolve_scope_resolution` fallback answers **0 of 632** there and **332 of
  752** on the compiler's own source. Measuring only `examples/` would have
  justified deleting a live lane. §7 records this for `extern module`; it is not
  specific to C imports.

- **A node stamp cannot serve a post-monomorphization lookup, and most of the
  widening that looked migratable is that.** A tripwire over the two
  struct-literal type lookups computed the `StructLiteralNode.res` stamp beside
  the spelling cascade and returned the cascade's answer, over 10,025 calls:
  **7,201 agreed, 2,824 disagreed, and every one of the 2,824 had the same
  shape.** The node's `struct_type` had been rewritten to a mangled spec name
  (`5Array$Lh_N$L3std…`), while `res` still named the generic base the source
  wrote (`std::collections::array::Array`).

  Neither answer is wrong. §6.1's invariant is that a `Res` names a definition
  and never an instantiation — which is exactly what makes it safe for
  `ASTCloner` to copy verbatim — so after mono the stamp and the spelling name
  different things on purpose, and these sites need the instantiation. The
  tripwire was reverted; only the finding is kept.

  Measured across all 19 call sites, of 24,267 widening answers **26.7% ask
  under a mangled instantiation name**, and the split is not uniform:

  | tier | widening answers | mangled |
  |---|---:|---:|
  | ambient cursor | 13,784 | 31.5% |
  | cross-module scope walk | 8,345 | **0%** |
  | arena leaf index | 2,138 | **100%** |

  The leaf index serves instantiations and nothing else, which is the same
  statement as `B4` being a floor rather than a debt. Four call sites — the ones
  taking a derived `spec_sym` / `lookup_sym` rather than a written name — are
  90–100% mangled and are not name-resolution questions at all. Counting
  fallback without this split overstates what a stamp could ever retire.

### 8.20 The mangled instantiation name was a KEY, and is now only an output — MEASURED AND FIXED 2026-08-27

Monomorphization minted a mangled spec name for an instantiation, wrote it back
into the AST's spelling slot, and later stages re-resolved it BY NAME. The node
already carried the instantiation as a pair -- `res`, which names the definition,
and `generic_args`, substituted to concrete types -- and the arena already keyed
its canonical slot on exactly that pair. The string was a third encoding of one
fact and the only one that needed a lookup to decode.

It also contradicted §6.1, which says substitution happens strictly BELOW `Res`,
in the type layer: minting a name and re-resolving it lifts substitution back up
into the name layer.

**The spelling and the stamp came to mean different things.** A cloner copies
`res` verbatim, which is legal precisely because a `Res` names a definition and
never an instantiation. The substituter then overwrote the spelling beside it
with the spec name. The stamp did not go stale; the field next to it changed
meaning, and every consumer that read the spelling got an instantiation where
the stamp offered a definition.

**Feeding a mangled spelling back to the mangler produced a name for nothing.**
`resolve_generic_scope_name` mangles `base_name` with the resolved arguments. In
a specialized body `base_name` had ALREADY been rewritten to the spec, so the
result was doubly mangled and no declaration carried it. That lookup missed
three times in four -- 3,658 of 4,886 calls -- and the misses fell through to
tiers keyed by bare leaf.

**The fix.** A self-reference carries the arena id the specialization already
has, on `resolved_type`, which is the carrier `NamedAnnotation.pre_resolved`
already was for annotations; the spelling keeps what the source wrote in every
phase. `resolve_generic_scope_type` answers the type question from `(base,
args)` for a struct literal and for a scope qualifier alike -- one primitive,
because two that resolved arguments differently would key one source text to two
slots. Trailing defaults are completed through `expand_default_type_args`, the
same primitive the name path used, so `RawBuffer<u8>` keys `(base, [u8,
GlobalAlloc])` rather than a slot the canonical one never meets.

`lookup_sym` is unchanged at the two static-method sites. The method registries
really do hold a specialization's methods under its mangled form, so that key is
not the compiler working around itself. Only the TYPE question moved.

| site | calls before | calls after |
|---|---:|---:|
| `sema/resolve_struct_literal/spec_sym` | 4,886 | 0 over these corpora, but KEPT |
| `sema/resolve_struct_literal_type/lit.struct_type` | 3,658 | 0 |
| `call_resolver/try_resolve_static_method/lookup_sym` | 1,945 | 0 |
| `call_resolver/lookup_scope_variant_payload_types/spec_sym` | 789 | 0 |
| `call_resolver/check_static_scope_method_args/lookup_sym` | 8,397 | 571 |

Reliance on the ambient cursor fell from 13,784 widening answers to 5,622.

#### The name mint reads zero and is still load-bearing

`resolve_struct_literal/spec_sym` answers nothing over every corpus above, and
deleting it on that evidence was wrong. A literal naming a type the writing
module cannot see is deliberately left UNSTAMPED (§6.2), so no definition is
available to pair the arguments with, and the mangled name is the only remaining
route to the instantiation. Removing it makes `Widget<T> { v: v }` resolve to
the bare base and fail its own `-> Widget<T>` with E0200.

**The zero was measured over the wrong population, by `cryo build`.** Module
discovery is import-driven, so the orphan module in
`tests/projects/resolution_leaf_index` that exists to exercise exactly this is
compiled only by `cryo test` and was never in the measurement. The project test
caught it. This is the §8.13 failure mode a second time: a control has to
exercise the axis in question, and for a name-resolution zero that means the
tool that compiles the file, not just the corpus that contains it.
#### B4 is not a floor, and §7.3 / §8.13 record that it is

B4 was pinned as a floor on the premise that a mangled name minted after the
name layer has finished is one a `Res` cannot name. The premise is true and the
conclusion does not follow: the name is not needed. The definition is on the
node and the arguments are beside it, and that pair is the arena's own key.

`B4_TOTAL` is **0** on both hosts. The arena leaf index is still ASKED 2,206
times and answers none of them, so the tier is reachable and inert rather than
unreached -- which is the control that separates "stopped happening" from
"stopped being recorded". The `lookup_by_leaf calls` row falling by exactly 192
at the struct-literal site, against 15 leaf hits plus 177 misses counted by a
separately written instrument, is the same control at the level of one site.

**§7.3 and §8.13 now describe a bucket that is empty.** Neither is edited here:
what they record about how the residue was MEASURED remains true, and the
decision to restate B4's definition is not one a measurement makes on its own.

#### What the leaf index still answers, over every population measured

One call. `Widget`, in `tests/tests/projects/reexport_chain`, through
`sema/resolve_struct_literal/lit.struct_type` -- the spelling fallback that
remains for a literal whose stamp makes no type claim. It is a PLAIN written
source name reached through a re-export chain, so it belongs to B1's class and
not to B4's: the scope walk did not follow the re-export and the program-wide
bare index did.

Populations measured, all with build directories wiped: `examples/09-json-config`,
all 14 `examples/`, `tests/tests/projects/ffi_c_import`, the compiler's own
source, all 38 `tests/tests/projects/`, and the `tests/` unit-test project (53
stdlib modules recompiled, `Poll` present and resolving at the cursor tier, which
is §8.16's fix holding).

**`stdlib/net/tls` is compiled by none of them.** §8.13's residue was found in
exactly that module, so this zero does not speak for it.

### 8.21 The stage that told the arena never told the index — MEASURED AND FIXED 2026-08-31

`TypeDeclaration` exists to make every type NAME known before any signature
resolves; its own driver says so (`collect_type_declarations`). It registered
those names in the arena and, for struct/union/enum/class/trait, never in the
`DeclarationIndex`. The guarantee held for one store.

A QUALIFIED annotation in a function signature is what that gap reaches.
`resolve_named` step 2c resolves a bare leaf against the writing module and
completes the lookup in the arena itself, so a bare name never needed the index;
2c is guarded on the name having no `::` and is skipped for a qualified one.
Step 3a wants the written spelling, and the index is keyed canonically, so it
misses. Step 3b canonicalizes correctly and then misses because the index does
not yet hold the name. What answered was step 4a: the same canonical string, in
the arena.

**The whole population is signature annotations.** Every one was identified from
source, and the control is that the same type written in a BODY nearby does not
appear:

| site | annotation | x |
|---|---|---:|
| `CLI/commands.cryo:920,928,934` | `input: mut &stdio::Stdin` | 3 |
| `sema/call_resolver.cryo:2645,2693` | `door: ResolveCounter::Site` | 2 |
| `sema/member_resolver.cryo:719` | `door: ResolveCounter::Site` | 1 |
| `examples/14-threads/main.cryo:37` | `tx: mpsc::Sender<i32>` | 1 |

`commands.cryo:862` and `:2615` write `stdio::Stdin` as a LOCAL and do not
appear; `main.cryo:38,66,67` likewise. 3 of 5 and 1 of 4, which is what makes
"signature" the axis rather than "this type".

**The fix registers the name where the arena learns it**, using plain
`register_type`. `register_type_with_module` also appends to `module_types`,
which is not idempotent and is read by sema's suggestion machinery, so per-module
ownership stays `TypeResolution`'s. The TypeAlias branch of the same match
already did exactly this.

Step 3b then answers with the identical canonical string, one step earlier:

| corpus | 4a before | 4a after | 3b gain |
|---|---:|---:|---:|
| examples + ffi + compiler | 7 | 0 | +6 |
| `tests/` | 2 | 0 | +2 |
| `examples/14-threads` alone | 1 | 0 | +1 |
| cross-target `x86_64-pc-linux-gnu` | 7 | 0 | — |

`M1 qualifier_agrees` fell by exactly the same amount on every corpus, because
`canonical_type_name` had been running TWICE for these names -- 3b missing, then
4a recomputing the identical string.

**An effect that was not predicted.** About eleven fewer type resolutions FAIL
per corpus (`unresolved` -10 on `examples/11-http-server`, -11 on `tests/`, with
`2c*` and `resolve_qualified_scoped` down by the same), from a caller that
consults the index first and falls back to the resolver on a miss. Behaviour is
unchanged by every gate, byte-identity included; it is recorded here because a
number moved that nobody predicted.

**`bootstrap_mode` then had nothing left to do.** Steps 4 and 4a answered 0 on
all four corpora, so the flag, both steps, `end_bootstrap`, its call site, both
counter variants and both B1 summands were deleted. The tripwire is no longer a
counter: a name that needed 4a now fails to resolve, and E0203 stops the build.

### 8.22 The bare-leaf template scan answers nothing, on every corpus — MEASURED, TIE-BREAKS DELETED 2026-08-31

`find_function_template_for_call` scanned every registered template for a
matching leaf and chose among candidates by unique call arity, then by longest
module prefix shared with the calling module, then by scan order. The first two
guess; the third is not a property the source states.

Once the identifier's stamp was allowed to answer first (§8.10's shape), the
scan stopped answering at all:

| corpus | scan calls | found none | found one | found PLURAL |
|---|---:|---:|---:|---:|
| examples + ffi + compiler | 7,028 | 7,028 | 0 | 0 |
| `tests/` | 3,199 | 3,199 | 0 | 0 |

The three exits could not state this between them: each reports WHICH
discriminator won, so "no exit fired" and "the scan matched nothing" read alike
in their totals. `M4Scan{None,One,Plural}` splits the population itself, and
survives the deletion of the code it justifies -- which the three exit counters
could not.

`PLURAL` is the only population a tie-break can act on and it is 0, so the three
were deleted. A leaf borne by more than one template now returns null rather
than picking one: refusing is a diagnostic, guessing is a silent misbind. The
scan itself is KEPT -- it answers nothing today, and a scan that is merely inert
is not the same finding as one that is unreachable.

### 8.23 The same ladder, one function over, with no instrumentation at all — MEASURED AND FIXED 2026-08-31

`find_scoped_function_template` carried a second copy of §8.22's ladder --
arity, then longest shared prefix, then scan order -- and bumped no counter
anywhere in its body, so nothing said whether it had ever decided anything. Its
caller is the sibling `else` of the branch §8.22 hardened: the `ScopeResolution`
callee arm.

It matched by SUFFIX: any template whose qualified name ends `::scope::name`.
That is a weaker question than the one asked, because the source may write any
whole-segment suffix of a module's name and several modules can share one.

**The scope segment was already stamped.** `ScopeResolutionNode.scope_res` has
carried a `ResSlot` since the slot was introduced -- written by NameResolution at
seven sites and by the async lowering's synthesizers -- and `Res::Def(ns)` on it
is the module's OWN namespace, which `scope_name` cannot supply. The field is
not named `res`, which is why it reads as absent.

Probed before it was allowed to answer, the stamp and the scan agree in BOTH
directions:

| corpus | calls | scan: none | scan: one | scan: PLURAL | stamp reaches scan's entry | stamp DIFFERS |
|---|---:|---:|---:|---:|---:|---:|
| examples + ffi + compiler | 14,443 | 8,993 | 5,450 | 0 | 5,450 | 0 |
| `tests/` | 4,608 | 3,121 | 1,487 | 0 | 1,487 | 0 |

`stamp reaches the scan's entry` equals `scan: one` exactly, on both corpora --
the pointer-identical `TemplateEntry`, not a matching name. And `scan: none`
decomposes exactly into the stamp naming no template plus the segment carrying a
non-`Def` answer (7,956 + 1,037 = 8,993; 2,867 + 254 = 3,121). Neither side sees
anything the other misses.

After the stamp answers first, the residual scan answers nothing: `scan: one`
falls to 0 on both corpora and `answered from the scope stamp` takes exactly the
population it held (5,450 and 1,487). The tie-breaks were deleted on the same
terms as §8.22's.

### 8.24 A probe was counted as a migration — MEASURED AND FLIPPED 2026-08-31

`qualify_symbol_sym_at` records what the span's home module WOULD mint and
returns the ambient cursor's answer unchanged. Its own docstring says so. Sites
converted to it are instrumented, not migrated, and counting them as migrated
overstates the work done.

Over 586,106 probed mints across examples + ffi + compiler, the LSP, and
`tests/`: 73 site labels exist in source, **61 are ever reached**, 50 of those
never diverge and never lack a home module, 11 diverge or have no span to derive
one from, and **12 are never exercised by any corpus measured**. A site that is
never reached cannot be certified, and was not flipped.

The 50 were moved to `qualify_symbol_sym_home`. Because home and cursor agreed on
every one of those calls, the flip is a no-op by measurement -- and the control
for that claim is not the counter but `selfhost-check`, which reports
byte-identical IR on both hosts across a full self-compilation.

The 11 that are NOT clean are left, and are the remaining population:

| site | calls | diverge | no home |
|---|---:|---:|---:|
| `wrap/tu-method-owner` | 284,343 | 0 | 284,343 |
| `wrap/tu-cascade-cursor` | 90,981 | 0 | 90,981 |
| `look/sema-impl-target-b` | 22,473 | 8,008 | 0 |
| `reg/extern-fn` | 15,700 | 0 | 522 |
| `look/sema-impl-target-a` | 10,141 | 7,993 | 0 |
| `look/sema-struct-qual` | 7,721 | 51 | 0 |
| `reg/spec-register` | 6,279 | 6,266 | 0 |
| `wrap/de-canon-typeref` | 3,695 | 0 | 3,695 |
| `wrap/cgctx-qualify` | 3,455 | 0 | 3,455 |
| `look/sema-struct-sym` | 1,716 | 30 | 0 |
| `reg/al-future-qname` | 481 | 21 | 0 |

"No home" is not agreement: it is the file naming no module in the graph, and
folding it into agreement would report a guess as an authoritative answer.

#### `spelling_type` collapses three answers into one

`spelling_type` returns a `TypeRef`, so a slot that was never answered, an answer
that names no definition, and an answer naming a definition the index does not
hold all reach six consumers as the same `invalid`. Each branches on
`!is_valid()` and falls through to a spelling lookup, and each carries a
near-verbatim paragraph justifying it.

Measured over 27,011 calls (native and `tests/`): `def NOT REGISTERED` is **0**,
so no registration defect is being routed around. `answer names no def` is also
**0** -- the case all six paragraphs cite as the reason for the fallback does not
occur. Every invalid return is a slot that was never answered. The six fallbacks
serve UNSTAMPED nodes, which is a different defect from the one they describe.

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
### Decided 2026-08-10

- **Q12 — the keystone subsumes the scope-template chain.** ANSWERED BY THE
  CODE: `lookup_scope_template` takes a `Res`, and the derived-name variant the
  question was about no longer exists. The question assumed collapsing the
  chain would rebind bare same-leaf names; measured over the corpus it gained
  418 answers, regressed none, and rebound none, because a stamped node names
  its template directly instead of searching for a leaf that a re-export never
  carried.

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

### 8.25 The `spelling_type` fallbacks measured, and the zero that was not one — MEASURED, DELETION REVERTED 2026-08-31

Five consumers call `spelling_type`, not six: `call_resolver.cryo:2774` is
`enforce_static_method_visibility`, a different function with a
`scope_qualifier_type` -> `lookup_type_by_sym` cascade.

Per-site counters split the invalid answers, over four corpora that between
them are the whole of what this tree compiles. **native** = 14 `examples/` +
`ffi_c_import` + `compiler/`, one `cryo build --no-incremental` each. **tree** =
the `tests/` harness compile itself. **projects** = the 41
`tests/tests/projects/` run INDIVIDUALLY. **neg** = the 178 compile-fail cases,
one `cryo check` each.

| consumer | native | tree | projects | neg | TOTAL calls / fires |
|---|---|---|---|---|---|
| impl head `sema.cryo:589` | 16,202 / 12,078 | 4,004 / 3,186 | 24,948 / 17,880 | 31,886 / 22,352 | 77,040 / 55,496 |
| struct literal `sema.cryo:2805` | 2,979 / 0 | 1,204 / 0 | 3,479 / **4** | 4,042 / 0 | 11,704 / **4** |
| enum pattern `pattern_resolver.cryo:120` | 1,120 / 0 | 70 / 0 | 1,820 / 0 | 2,788 / 0 | 5,798 / **0** |
| new expr `sema.cryo:3114` | 1,368 / 0 | 32 / **6** | 2 / 0 | 3 / 0 | 1,405 / **6** |
| call ident `call_resolver.cryo:2900` | 26 / 0 | 6 / 0 | 0 / 0 | 1 / **1 empty** | 33 / **1** |

`answer names no def` = 0 and `def NOT REGISTERED` = 0 on every corpus, with
`def found` as the control, and per-site fires summing to the global `Pending`
exactly on each. So every one of the five justification paragraphs cites a case
that never occurs: an invalid answer here is always a slot that was never
answered, never an answer declining to name a type.

Three of the four non-impl-head fallbacks have a live population, and each one
is confined to a single corpus: the struct literal's 4 to two projects, the new
expr's 6 to the `tests/` tree, the call ident's 1 to one compile-fail case
(where the fallback found nothing and fell through to E0202 regardless). Only
the enum-pattern fallback answers nothing anywhere, against 5,798 calls.

The shape is the finding: this family serves rare populations, each reachable
from exactly one corner of the corpus. A rate of 4 in 11,704 - or 6 in 1,405 -
is invisible to any sweep that misses the one place it lives.

#### The impl head is the population, and it is legitimately unstamped

Each head is consulted exactly twice by `impl_target_type`, which closes the
arithmetic with no remainder: native 2,062 stamped + 3,048 stamp-declined +
2,991 never-reached = 8,101 nodes, x2 = 16,202. Projects: 8,474 visited, 3,534
stamped, 4,940 declined.

The 3,048 declines are **100% primitive impl targets** - `implement trait Clone
for i8` and its siblings - identified off `ANN-UNSTAMPED`, where all 3,048 sit
at column 1 and come from only 195 distinct source lines, the stdlib's own
primitive impls re-resolved once per project. The impl head is 3,048 of all
3,049 `AnnResUnresolved` compiler-wide; the single other row is `CompileMode`,
a name owned by both a namespace and a type. The 2,991 never-reached nodes are
monomorphizer clones, which `cloner.cryo` deliberately does not give a `res` so
that the rewritten spelling stays the only carrier of which specialization the
head is for.

Both halves are correct as they stand. There is no stamping fix behind this
population.

#### A zero that came from an instrument that could not see

`cryo test` echoes a child project's captured output ONLY when that project
fails. Capturing its stderr therefore records the PARENT invocation and nothing
else - one counter block where running the 41 projects individually yields 26,
and a `Pending` total of 3,192 where the real figure is 17,884.

On that false zero the struct-literal, enum-pattern and call-ident fallbacks
were deleted. The struct-literal one has a live population of **4**, in exactly
two projects - `ffi_c_import` and `resolution_leaf_index`, both reachable only
through `cryo test`. Removing it did not produce a diagnostic; it produced a
**silent miscompile**, a C-imported aggregate taking a wrong layout so that
`c_import_struct_construct_and_read` read 1308 for 7 and `c_import_union_native`
read -1233322032 for 1234. All three deletions are reverted and `make test` is
OVERALL PASS.

The lesson is sharper than "measure over the right corpus". The corpus was
right and the tool was right; the tool declines to report when it is happy. A
zero is worth nothing until the number of counter blocks behind it is known -
if it is 1, what was measured is the wrapper.

#### A second hole: no failing compile has ever been counted

`ResolveCounter::report()` was reached only from the end of the driver's success
path, so every compilation that errors returned before it. The 178 compile-fail
cases fail by construction, so **not one counter number on this branch has ever
included them** - and they are the corpus richest in names that do not resolve,
which is the condition that leaves a `res` slot unanswered.

The report is now idempotent and `cryo check` calls it whatever the outcome, so
the compile-fail corpus yields 178 blocks where it previously yielded 0. It
remains inert without `CRYO_RESOLVE_COUNTER`, and `make test` still reports all
178 passing.

What it changed: the call-ident fallback's only fire in the whole tree is in
this corpus, and would otherwise have been recorded as a clean zero.

#### Corrected alongside

`AST/declaration.cryo`'s `res` field doc said a clone "may copy it verbatim",
which `cloner.cryo` contradicts explicitly and for a stated reason. The comment
now records what the cloner does and why.

### 8.26 A written primitive is an ANSWER - 6.1 implemented, superseding 8.2al 2026-08-31

6.1 lists `PrimTy(SymbolStr)` and states that `Res` has no variant meaning
"missing": every variant is an answer. Nothing in the tree ever constructed one.
A written `implement trait Clone for i8` was therefore left `Pending`, because
the bare lane resolves against the writing module's DECLARATIONS and a primitive
is not one - the code disagreeing with the spec, not an open question.

8.2ak drew this conclusion already ("Under 6.1 they are `Res::PrimTy` - an
answer"). **8.2al superseded it on an empirical premise, never a design one**:
that the primitives were not written type names at all but synthesized `&this`
receivers - 225 of its 228 - so a stamp "would have recorded a true fact about a
node the parser should not have produced".

That premise no longer describes the population. Measured over all 16 build
corpora, `ANN-UNSTAMPED` carries 3,049 rows from 195 distinct source lines, and
**every primitive row is at column 1** - the `implement` keyword. Zero rows sit
at any other column but the one `CompileMode` row at column 13. The
receiver population 8.2al measured is gone; what remains is the written-type-name
bucket its own table put at 95. An expired measurement does not override 6.1, so
this entry supersedes 8.2al and reinstates 8.2ak's conclusion.

#### Implementation

`type_spelling_res` answers `Res::PrimTy(name)` for a written primitive, ahead of
the lanes that search for a declaration. 8.2ak's constraint is honoured: the
authority is the lexer's keyword table, via the same
`TokenType::from_keyword(...).is_primitive_type()` idiom `parser.cryo` already
uses, so there is no second list of primitive names to disagree with the first.
The unit spelling `()` is NOT a keyword and is deliberately left `Pending`
rather than special-cased in; it is 16 of the rows.

#### Measured, and neutral

| | before | after |
|---|---:|---:|
| `AnnResPrimTy` (native) | 0 | 3,032 |
| `AnnResUnresolved` (native) | 3,049 | **17** |
| `SpellTyPending` (native) | 12,078 | 6,014 |
| `SpellTyNoDef` (native) | 0 | 6,064 |

3,032 = 3,048 primitives less the 16 `()`; 17 = those 16 plus `CompileMode`;
6,014 = the 5,982 clone consultations plus the 32 for `()`. `SpellTyDefMissing`
stays 0, because `def_name()` is empty for every non-`Def` answer and so never
reaches a lookup.

**Consumer behaviour is unchanged, and that is the point.** Fires at all five
`spelling_type` consumers are identical before and after - impl head 12,078
native and 17,880 over the projects, struct literal 4, new expr 6 - because a
`PrimTy` stamp still yields an invalid `TypeRef` and the same path still runs.
Only the RECORDED REASON moves, from "no resolver answered" to "answered, names
no declaration". `AnnResUnresolved` falling 3,049 -> 17 is what that buys: the
row now names the one real defect instead of burying it under the stdlib's
primitive impls.

The 106 real types 8.2ak identified as the import-cycle population are untouched
by this, and remain the question that has to be answered before any consumer may
treat `Pending` as an ICE rather than as "the type layer still owns this one".

### 8.27 One `spelling_type` consumer converted; two left, with reasons 2026-08-31

The disposition is per site and follows the measurement, not a preference:
`Pending > 0` means fix the stamp and leave the path alone; `Pending == 0` means
delete the path and assert, which converts a correctness risk into a diagnostic
one.

**Enum pattern - CONVERTED.** 0 fires and 0 `NoDef` across all four corpora
(native, the `tests/` tree, the 41 projects, the 178 compile-fail cases), against
5,798 calls. A pattern names its enum outright, never a local, a generic
parameter or a primitive, and `cloner.cryo` copies `res` so a clone carries it -
a mechanism, not luck. The site now takes `require` and matches `Res::Def`, with
no second key. `make test` OVERALL PASS and **zero E0900**.

**New expr - NOT converted, and the reason inverted on measurement.** The
proposal was to delete the spelling lookup and keep `resolve_primitive`, on the
ground that a spelling lookup cannot resolve a primitive at all. Instrumenting
the two steps separately says the opposite: over the `tests/` tree the spelling
lookup answers **6** and `resolve_primitive` answers **0**. The population is
`new int[100]` - `tests/tests/lang/new_array.cryo` covers it by name - and
`lookup_type_by_sym` reaches the primitive through its arena leaf-index step.
Deleting that step breaks a covered test. `resolve_primitive`'s own zero is
SHADOWED, not measured: it is only reached when the step above declines, and the
step above never does.

**Call ident - NOT converted.** 33 calls in the whole tree, one fire, which found
nothing and fell through to E0202 either way. That is too thin a population to
certify in either direction - the same rule this ledger applies to a site no
corpus reaches - and the only outcome on offer is trading E0202 for E0900.

**Struct literal - NOT converted**, settled in 8.25: 4 live fires, C-imported
aggregates, and deleting the path miscompiles them silently.

### 8.28 The impl head answers per `Res`; the `spelling_type` round-trip is gone 2026-09-01

8.27 named the signature as the higher-leverage change, and it is this one.
`spelling_type` returned a bare `TypeRef`, so a caller could not tell an answer
that DECLINES to name a type - which is complete - from one naming a definition
the index does not hold, which is a registration defect. The impl head's
response to either was to look the written spelling up, and that is the fallback
shape this document exists to remove.

The head now matches its own slot, one key per branch and no key retried under
another:

| slot | key | why that key |
|---|---|---|
| `Def(q)` | `q` | the name layer's own answer, already canonical |
| `PrimTy(n)` | `n` | a primitive owns members without being declared anywhere, so its spelling IS its registration key |
| `Pending` | `target_type` | the cloner leaves a specialization's slot alone - a `Res` names a definition and a specialization is not one - so the monomorphizer's rewritten spelling is the carrier |

The variant split was measured BEFORE the match was written, because a match
written blind would send an unmeasured variant somewhere the fallback did not.
Over four corpora - native, the `tests/` tree, the 41 projects, the 178
compile-fail cases - it is `Def` 21,870, `PrimTy` 31,601, `Pending` 24,670, and
everything else **0**. `PrimTy + Pending` is 55,496, which is the fire count
8.27 arrived at through a different instrument.

**The fallback was narrowed, not merely rerouted.** It called
`lookup_type_by_sym`, a five-step widen. The cascade audit says all 12,078
native fires answered at step `1-EXACT`: none at the module cursor, the
cross-module chain, the arena leaf index, or miss. The site now asks
`lookup_type_exact`. `b1-check`'s per-site rows did not move, `lookup_by_leaf
calls` among them, which is the control for that deletion - narrowing a cascade
whose later steps were load-bearing would have moved exactly those rows.

`SpellTyPending` 6,014 -> 0 and `SpellTyNoDef` 6,064 -> 0 on native. The whole
residue of the family is now the 4 struct-literal fires and the 1 call-ident
fire that 8.25 and 8.27 settled as not convertible.

#### A canonical name the index cannot serve is now reported

`def_type` is the single step that turns a `Res::Def` into a type, shared by the
impl head and by `spelling_type_answered`, so every consumer of a canonical name
asks the same question in the same place. A name the index does not hold records
an internal defect on the same deferred terms as the `Pending` tally - raised
only if the build otherwise succeeds - and under its own message, because the
two say different things: a pass that never ran, against a declaration that
never registered. It fires 0 times on all four corpora, which is what the
measured `SpellTyDefMissing` zero predicted.

#### The "projects" column in 8.25-8.27 was 26 projects, not 41

8.24 hoisted `report()` above the success branch of the `check` driver, for the
stated reason that a corpus of deliberately failing files is where names that do
not resolve live. The BUILD driver was left on its success path, so a project
that fails to compile still contributed nothing - and all 13 `compile_fail`
projects were absent from every "projects" figure those three entries record.
The corpus richest in unresolved names was missing from the column named after
it, and the shortfall was visible as a block count of 26 against 41 files.

`cryo build`, `cryo run` and `cryo test` now report on the same terms as `cryo
check`. The sweep yields 41 blocks from 41 projects, which is why the projects
figures here exceed the ones in 8.27 for populations that did not change.

### 8.29 The codegen impl-target cascade cannot read the stamp - MEASURED, NOT MIGRATED 2026-09-01

Two sites re-derive an impl block's target NAME rather than its type:
`codegen/ops/declaration_emitter.cryo` and `codegen/visit/decl_visit_emitter.cryo`.
They are verbatim mirrors, and one says so in a comment. Each is a five-way
choice: the `qualified_target_name` stamp; else the spelling when it already
contains `::`; else the bare name if the declaration index holds it; else a
scoped mapping; else a cursor qualification the report itself marks a guess.

The proposal was to make them read `res`, as 8.28 made the sema-side impl head
do. **That is not available, and the reason is structural rather than
incidental.**

Split over the population that actually REACHES the cascade - the nodes with no
`qualified_target_name` - `res` is `Pending` for **all** of them:

| corpus | asked | re-derived | `Def` | `PrimTy` | `Pending` | other |
|---|---:|---:|---:|---:|---:|---:|
| native | 19,978 | 11,896 | 0 | 0 | 11,896 | 0 |
| tree | 5,514 | 4,540 | 0 | 0 | 4,540 | 0 |
| projects | 30,644 | 16,718 | 0 | 0 | 16,718 | 0 |
| cross-target | 14,580 | 7,644 | 0 | 0 | 7,644 | 0 |

`re-derived` equals the `Pending` column exactly on every corpus. `cloner.cryo`
says why: `res` and `qualified_target_name` are BOTH withheld from a clone, for
the same stated reason - specialization rebinds WHICH type the head is for, so a
stamp naming the template beside a `target_type` the monomorphizer rewrote to
the instantiation would bind the head back to the template. The rewritten
spelling is deliberately the only carrier. A `Res` cannot name an instantiation,
which is the same principle the B4 floor rests on.

So the two carriers are not redundant and the cascade is not reading the wrong
one. **A site whose population is entirely `Pending` by construction cannot be
migrated to the stamp, and a session that "fixes" it is removing a carrier the
clone has nothing to replace with.**

The whole-population split adds the complementary fact: where `res` IS `Def(q)`,
`q` names the SAME symbol the site computes - 21,540 agreements, **0**
disagreements, across all four corpora. The two stamps agree wherever both
exist; they simply do not both exist.

The unstamped split is kept as a tripwire; the whole-population one was removed
with the question it answered. `re-derived, yet res: Def` going nonzero would
mean the stamp had become available where the cascade runs, and this migration
worth revisiting - which is the only reason to keep a counter for a settled
question.

#### Three of the five ways answered nothing, and are gone

Over 40,798 re-derivations on four corpora - native, the `tests/` tree, the 41
projects, and a cross-target build that compiles the other OS's platform-gated
modules - **every one takes the spelling step**. The declaration-index step, the
scoped mapping and the cursor guess answered nothing anywhere, in both copies.

The cross-target arm is the control that matters: it is the only one that
compiles modules a native build never sees, so it is the population a
native-only zero would have missed. It did not overturn the zero. It is also
only observable because the build driver now reports on failure (8.28) - that
link fails by design, and before 8.28 the whole arm would have been silent.

Both copies now read: the stamp when there is one, the written spelling
otherwise. `make test` OVERALL PASS, `selfhost-check` byte-identical on both
arms, and `b1-check` unmoved - the last being the control for the deletion,
since a branch that was load-bearing would have shifted a pinned row. The
`lane-check` ratchet fell 129 -> 127 (`declaration_emitter.cryo` 10 -> 9,
`decl_visit_emitter.cryo` 6 -> 5) and was re-pinned deliberately with this
change.

The two sites remain verbatim mirrors of each other, now four lines each rather
than twenty. De-duplicating them is still available and still unclaimed.

#### A comment corrected

`AST/declaration.cryo`'s `res` field doc said a primitive impl target "is left
unanswered here as well". It has not been since 8.26 stamped written primitives
`PrimTy`; the codegen probe counts 6,064 of them reaching the emitter on the
native corpus alone. The doc now records that a written primitive IS answered,
names no declaration, and carries its registration key in its spelling.

### 8.30 The impl-target derivation is one accessor on the node 2026-09-01

8.29 left the two codegen sites reading stamp-then-spelling, four lines each and
still verbatim mirrors. They are now one call to `ImplBlockNode::codegen_target_name`.

The derivation is a function of the block's own three fields - the stamp, the
slot, the written spelling - and of nothing the emitter knows, so the node is
where it belongs. Both consumers ask the same question and there is now one
place that answers it; a second answering path cannot drift back in without
being visible as one.

`declare_impl_block` registers a method under the returned name and
`generate_impl_block` emits its body against the same name. That those two must
agree is the reason the duplication was dangerous rather than merely untidy:
they were two copies of the rule that decides one key, and nothing checked they
stayed equal.

MEASURED, not assumed. The whole `CgImplTarget` family and the impl-head slot
split were collected over 14 examples plus the compiler's own sources - 15
counter blocks from 15 files, counted both runs - with the pre-change compiler
and again with the post-change one:

| row | before | after |
|---|---:|---:|
| codegen impl targets asked for | 19,012 | 19,012 |
| NO stamp: re-derived here | 11,416 | 11,416 |
| re-derived, yet res: `Def` | 0 | 0 |
| re-derived, res: `PrimTy` | 0 | 0 |
| re-derived, res: `Pending` | 11,416 | 11,416 |
| re-derived, res: OTHER | 0 | 0 |
| impl head slot: `Def` | 3,888 | 3,888 |
| impl head slot: `PrimTy` | 5,686 | 5,686 |
| impl head slot: `Pending` | 5,772 | 5,772 |

Byte-identical. The counters are bumped inside the accessor now rather than at
each call site, and there are still exactly two callers, so a total that moved
would have meant the hoist changed which nodes reach the derivation. It
decomposes with no remainder on both runs - `re-derived` equals `res: Pending`
exactly, and the four impl-head slots sum to the 15,346 calls - which is the
same shape 8.29's four-corpus table has, reproduced here through a rebuild.

`make test` OVERALL PASS (unit ok, 178 compile-fail, 38 projects), `b1-check`
B1=0 B4=0 18 sites on all three Windows sections, both unmoved.

#### The ratchet does not move, and 8.29 was wrong to expect it to

8.29 closed by noting the de-duplication was still available. A handoff written
beside it added that doing it would move `lane-check` and need a re-pin. **It
does not.** `lane-gate.py` counts calls to the five per-kind lookups, and 8.29
had already removed the only one in this block when it deleted the
declaration-index step; the four lines left contain none. Measured before and
after: LOOKUP 127 (20 files), REENTRY 6 (5 files), unchanged, no re-pin.

The general point is the one 7.2 makes about the ratchet: it pins a named
surface, not "code that looks like resolution". Predicting it will move because
a call site changes file is predicting from the shape of the change rather than
from what the gate measures.

#### A dead local the previous change orphaned

`decl_visit_emitter.cryo` still declared `di_ptr` for the declaration-index step
8.29 deleted. The compiler had been reporting it as an unused variable; the
warning total falls 352 -> 351 with its removal.

### 8.31 `()` is a primitive spelling, and one predicate answers for all of them 2026-09-01

The unit type was the last written spelling an impl head could not stamp. It is
now `PrimTy`, on the same terms as every other primitive: it owns members
without being declared, so no scope binds it and no export set carries it, and
its spelling is its registration key - the declaration index already registered
`()` beside the scalars.

**There were TWO predicates answering "is this spelling a primitive", and they
already disagreed.** `ResBase::is_primitive_spelling` served the scope lane;
the lexer's keyword table (`TokenType::from_keyword(..).is_primitive_type()`)
served the annotation lane. Their difference was not `()` alone:

| in | not in | spellings |
|---|---|---|
| the list | the keyword table | `void` |
| the keyword table | the list | `int`, `uint`, `float`, `double`, `va_list` |

The annotation lane carried a comment naming the keyword table the authority and
arguing a second list is "a place for the two to disagree, and the disagreement
would be silent". The reasoning was right and the conclusion inverted: the second
list already existed, one file over, and had already disagreed on six names.
**The keyword table cannot be the survivor, because `()` is punctuation and no
keyword spells it** - a table that cannot express a primitive cannot be the
authority on which spellings are primitive. The list wins, and both lanes ask it.

MEASURED before the switch, not after. A probe logged every spelling where the
two predicates disagree, over two corpora: the compiler's own sources (33,666
spellings offered to the stamp) and a project compiling 56 stdlib modules.
**Zero disagreements on either.** The cause is upstream: the parser turns a
keyword primitive into a `PrimitiveAnnotation` and `()` into an empty
`TupleAnnotation`, so a keyword spelling never arrives here as a written name.
What does arrive is an impl head's target TEXT, which is how `()` reaches the
predicate at all.

#### The list is NOT the set of every type keyword, and `float` is why

Completing the list from the keyword table would have been the obvious tidy-up
and it is wrong. `float`, `int`, `uint` and `double` are alias keywords, and a
module may be named for one - **`float` IS one**, `std::fmt::float`, with 7 live
call sites of the form `float::parse_f64(..)`. The scope lane consults this
predicate for a qualifier, so answering `PrimTy` for `float` would name the
primitive in every file that does not import the module. The predicate holds the
spellings no module can also own; the alias keywords stay out, and the doc
comment says so at the definition.

#### Measured, on one project compiling the stdlib

309 impl heads reached name resolution. Before: 307 stamped, **2 declined** -
`Drop for ()` in `stdlib/core/drop.cryo` and a probe's own `Tag for ()`. The
control was to swap the probe's target from `()` to a struct: declines fell 2 to
1 and stamps rose 307 to 308, which is what identifies the declines as the unit
heads rather than something else that happens to number two.

After: **309 stamped, 0 declined.** Five counter rows moved out of 180, and every
delta accounts for those same two heads - `stamped PrimTy` 189 -> 191, impl-head
slot `PrimTy` 378 -> 382 against `Pending` 244 -> 240 (the four consultations the
two heads draw), and `UNSTAMPED bare name not in scope` 2 -> 0. The slot split
sums to 858 on both sides. **Nothing else moved**, which is the control for the
predicate switch: `stamped PrimTy` rose by the two units rather than falling, so
no keyword-only spelling lost an answer it had been getting.

`make test` OVERALL PASS, `b1-check` B1=0 B4=0 18 sites on all three sections,
`lane-check` 127/6 - none of them moved.

### 8.32 A direct call's return type comes from the stamp, not an ownerless leaf 2026-09-01

`resolve_direct_call` asked `lookup_func_return` twice: once on the name
qualified with the file's home namespace, and on failure again on the BARE
leaf. The second is the shape 7.2's corollary names as the mechanical origin of
B1 - `if (a) { .. } else { try_another_way() }` for the same question - and the
site's own comment already called it "a second, more permissive door" whose
plural-leaf case "is the defect". The bare `func_returns` map is program-wide
and carries no owner, so a leaf two modules spell answers from whichever
registered last.

It now reads `ident.res`. MEASURED over the population that actually takes the
door, before the change: **40 fires, 40 carrying `Res::Def`, 40 reaching the
pointer-identical `TypeRef`** the bare lookup produced. No `Pending`, no
`stamp-EMPTY`, no `stamp-DIFFERENT`. The counter says 40 and the probe emitted
40 rows, which is the control that says no row was dropped.

#### The whole population says the home guess is redundant too

A second probe covered every call, not only the widening ones.
`resolve_direct_call` is reached **68 times** on the compiler's own sources:

| `res` | home lookup | stamp | rows |
|---|---|---|---:|
| `Def` | empty | answers | 40 |
| `Def` | empty | empty | 26 |
| `Def` | hit | SAME | 2 |

Every call carries `Def`; nothing is `Pending`. The home-qualified lookup
answers **2 of 68**, and the stamp reaches the identical type in both. So the
stamp strictly dominates: it answers everything the home guess answers and 40
more, and collapsing the two into one read is available.

**It was not taken here, and the reason is a gate rather than a doubt.** The
bare branch carries the visibility check `enforce_callee_visibility` reaches
through as door 4 (E0353). Door 4 measures 0 on this corpus, and that zero has
no control: the corpus that names a module its writer cannot see is the
compile_fail corpus, which never reaches the end-of-run report - the same
reason `vis_gate_reject` is emitted at the refusal rather than tallied. Moving
the check would also make it fire for the 2 calls the home lookup serves, which
currently bypass it, and that changes what a program compiles to. Left as a
separate question with its own measurement.

#### The row moved buckets; it did not stop being recorded

The site is renamed `FnBindStampedRet`, "return type from the stamp (home
missed)", and reflagged **B3**. It reads an authoritative answer now, so
counting it against a target of zero would make `B1 == 0` false for a
legitimate site - the mistake 7.3 records about `canonical_qualified`.

`b1-gate` reports the old row GONE (was 4), and asks for the mechanism before a
re-pin, because a row can also fall by stopping being recorded. The control:
on the gate's own target, `B3 return type from the stamp (home missed)` reads
**4** - the same population, the same count, a different bucket.

B1 stays 0 and B4 stays 0. `make test` OVERALL PASS (178 compile-fail, 38
projects). `lane-check` 127/6, unmoved: the file still makes one
`lookup_func_return` call in this function, under a better key.

### 8.33 M4's scan answers nothing on three populations, and the fourth is UNMEASURED 2026-09-01

The mono bare-name template scan is a deletion candidate: it is called and it
answers nothing. Measured, per population, one process each with
`CRYO_CODEGEN_THREADS=1`:

| population | blocks | scan calls | scan hits |
|---|---:|---:|---:|
| native - 14 examples + the compiler's own sources | 15 | 6,840 | **0** |
| cross-target - the other OS's platform-gated modules | 3 | 4,134 | **0** |
| the 41 projects, individually | 41 | 7,986 | **0** |
| **total** | **59** | **18,960** | **0** |

59 counter blocks from 59 files: the block count equals the file count, which is
the control that says no run contributed silently nothing.

**It is still not certified, and the missing population is the one that has
already burned this exact deletion.** The `tests/` tree compiles orphan modules
no `cryo build` reaches, and it cannot be measured with this instrument:
`cryo test` forks per test, counters are per-process, and the parent's stderr is
all a caller captures. The run yields ONE block in which **every row is zero** -
not the scan's rows alone, every row - so the process being measured compiled
nothing. That is an empty process, not a population reading zero.

This is the distinction 8.25 records from the other side: `resolve_struct_literal`'s
last resort was deleted on a zero and the suite went red, because the corpus that
exercised it was the one the measurement had not reached. A zero needs a control
that says what would have to be true for it to be zero for an uninteresting
reason, and here the uninteresting reason is confirmed.

**So the scan stays.** What would certify it is an instrument that tallies the
CHILD processes - per-test counter files, or an in-process test driver - not
another corpus run through the same tool. Recorded so the three-population zero
is not mistaken for a complete one.

### 8.34 The first deletion: four dead functions, and one scan that is now one scan 2026-09-02

Mechanism 2 says every string-keyed lookup reachable from `sema`, `mono` or
`codegen` ceases to EXIST, because a stage cannot re-derive what it has no
function to call. Measuring and proving dead had been happening for weeks;
removing had not. This is the first removal.

| removed | where | control |
|---|---|---|
| `qualify_symbol` | `compilation_context.cryo` | its only textual mention is a comment |
| `qualify_name` | `compilation_context.cryo` | one caller, and it was `qualify_symbol` |
| `qualify(leaf) -> QualifiedName` | `compilation_context.cryo` | no caller |
| `qualify(sym) -> SymbolStr` | `codegen/ops/declaration_emitter.cryo` | no caller |
| `resolve_type_qualified_name_bare` | `resolver/resolver.cryo` | no caller; its `_from` sibling has 4, which is what says the grep would have found one |

#### `qualify` was three functions, and one of them is live

An inventory that reports "`qualify`, 0 callers" is reading a name, not a
symbol. **Three** definitions carry it: the two above, and
`codegen/context.cryo`'s, which `decl_visit_emitter` reaches through `this.cg`
- a `CodegenContext*`, so that call resolves to the third. It is not touched.
The error would have gone both ways at once: one too few deleted, and one
deleted that is called.

#### D2: the two M5 suffix scans are one scan, and are NOT deleted

`process_import` carried the same suffix-match loop twice - once for the import
head, once for a sub-module - differing only in variable names and the audit
row they emit. They are now one `module_by_path_suffix`.

**Factored rather than deleted, deliberately.** The scan's counters read 4
calls and **0 hits** over 59 blocks (native, cross-target, the 41 projects),
which looks like the same evidence 8.33 gathered for M4. It is not comparable
evidence: 4 calls is not a population, and the corpus that exercises import
resolution hardest is `tests/`, which 8.33 established cannot be measured with
this instrument at all. A zero over 4 calls, with the relevant corpus missing,
is not grounds for deleting a branch of import resolution. Factoring gets the
same line saving and needs no zero.

The helper preserves the loop character for character, so the gates are a clean
control on a purely structural change - and they did not move: `lane-check`
127/6, `b1-check` B1=0 B4=0 17 sites on all three sections, `make test` OVERALL
PASS (178 compile-fail, 38 projects), all identical to the run immediately
before the change.

**A third copy of the boundary rule exists and was left alone.**
`Resolver::ns_written_as` states the same "suffix must land on a `::` boundary"
rule, over a module's NAMESPACE rather than its registered NAME, and checks
both characters of the `::` where this checks one. Making the helper call it
would fold three copies into two and is probably right - but it is a stricter
relation, so it is a behaviour change, not a de-duplication, and it is not what
this change is.

#### Still standing, and it is correctness debt

**D1 is a different pair from the one 8.30 deduped, and it is still two hand-
maintained copies.** `canonical_type_qname` (`declaration_emitter.cryo`) and
`canonical_impl_target` (`decl_visit_emitter.cryo`) both run the same three-step
cascade - cursor-qualified, then bare, then the arena's bare-name alias - then
ask the arena for the qualified name, differing only in which qualifier they
mint and what they fall back to. 8.30 deduped `declare_impl_block` against
`generate_impl_block`; these are the *other* pair in the same two files, and
nothing has merged them.

The code's own comment says what disagreement costs: a method declared under
one key and defined under another leaves the declaration bodyless, and LLVM
rejects the module. Two copies of the rule that decides that key is the same
hazard 8.30 records, unfixed. Not addressed here because it needs the same
measured treatment 8.30 got, not a textual merge.

### 8.35 D1: the canonical name is derived once 2026-09-02

8.34 left `canonical_type_qname` (`declaration_emitter.cryo`) and
`canonical_impl_target` (`decl_visit_emitter.cryo`) standing as two hand-
maintained copies of one cascade, and said they needed the measured treatment
8.30 got rather than a textual merge. This is that treatment.

#### The two were value-identical, and the proof is per-ingredient

A textual merge would have assumed it. Each ingredient was traced instead:

| ingredient | `canonical_type_qname` | `canonical_impl_target` |
|---|---|---|
| step-1 key | `qualify_symbol_sym_at(name, "wrap/de-canon-typeref", "")` | `cg.qualify(name)` |
| step 2 | `lookup_type(name)` | `lookup_type(name)` |
| step 3 | `get_arena().lookup_by_name(name)` | `cg.get_arena().lookup_by_name(name)` |
| fallback | `qualify_symbol_sym_at(name, "wrap/de-canon-qname", "")` | `local`, the step-1 key |

`CodegenContext::qualify` is `qualify_symbol_sym_at(sym, "wrap/cgctx-qualify",
"")`, and `qualify_symbol_sym_at` **returns the cursor's answer on every path**:
the site string and `span_file` feed counters and nothing else. Both sites pass
`""` for `span_file`, so `module_ns_sym_of_file` is invalid for both and neither
can take the home branch. All four expressions therefore reduce to
`qualify_symbol_sym(name)`. The counter table above is the independent control
on that reading: both rows report **0 diverge** with "no home" equal to their
call count, which is what a site that always returns the cursor looks like.

Both `get_arena()` resolve to `ctx.type_arena` and both `get_decl_index()` to
`ctx.decl_index`, so the two cascades read the same index and the same arena.

#### Where it lives, and why not on either emitter

`DeclarationEmitter` holds a `CompilationContext*`; `DeclVisitEmitter` holds a
`CodegenContext*`. The one object both already reach is `CompilationContext`,
which owns all three ingredients - `decl_index`, `type_arena` and the namespace
cursor. Putting the derivation anywhere else would have meant plumbing one of
them to a second owner, which is how the pair arose.

`canonical_type_ref` remains as a delegate on `DeclarationEmitter` because
`declare_class_methods` needs the `TypeRef` itself, not the name.

#### Measured

`lane-check` moved as a de-duplication must - the duplicate lookups are gone:
LOOKUP **127 -> 125**, `declaration_emitter` 9 -> 7, `decl_visit_emitter`
5 -> 3, `compilation_context` 0 -> 2; REENTRY unmoved at 6. Re-pinned.

Everything that must NOT move did not: `b1-check` B1=0 B4=0 **17 sites** on all
three sections, `make test` **OVERALL PASS** (178 compile-fail, 38 projects, 0
failed) - identical to the run immediately before the change. A change in any
mangled name is what this pair's hazard consists of, so an unchanged suite over
38 projects is the control on value-identity, not merely on compilation.

#### One recorded split is now stale

`canonical_impl_target`'s calls moved from `wrap/cgctx-qualify` to
`wrap/de-canon-typeref`. Total `QualSymCalls` is unchanged and both rows still
diverge zero, but the 3,455 / 3,695 split in the table above was measured
before this and no longer describes the two sites. `wrap/cgctx-qualify`
survives with its other callers.

### 8.36 The deletion sweep, and two probes that answered with the cursor 2026-09-02

8.34 was the first deletion and removed four functions. This removes 96, plus
two probes, and the mechanism-2 surface it clears is what the count is for:
`lane-check` ratcheted **127 -> 122** across the change.

#### The probes returned their input's cursor answer on every path

`qualify_symbol_sym_at` computed `qualify_symbol_sym(sym)` and returned it from
every exit; the site string and `span_file` reached only counters. Its 20 call
sites are therefore plain `qualify_symbol_sym` calls by construction, not by
measurement, and collapsing them cannot change a name. Its own docstring said
"PROBE FORM" in capitals and said the flip was a separate step; the flip is
what this is, and the probe is gone.

`probe_ambient_divergence` re-ran the module-blind chain to classify the pair
it produced against the scoped answer. It early-returned unless
`CRYO_SCOPE_PROBE` was set, returned `void`, and had one caller. Removing it
also removed `home_via`, which three sites wrote and only the probe read.

#### Why `b1-check` could not move, stated before it was run

The probe called `arena.lookup_by_leaf`, and `lookup_by_leaf calls` IS an
asserted row. Two independent reasons said the row was unreachable from it:
`b1-gate.py` sets only `CRYO_RESOLVE_COUNTER`, `CRYO_CODEGEN_THREADS` and
`CRYO_STDLIB`, never `CRYO_SCOPE_PROBE`, so the probe returned at its first
line throughout the measurement; and its replayed lookups sat inside
`suspend()`/`resume()` precisely so they could not inflate the leaf-index
tallies. B1 held at 0 with 17 sites on all three sections. Had it moved, the
reading of the gate's environment was wrong - which is what made it worth
predicting rather than observing.

#### The two re-pins, each predicted before it was taken

`lane-check` 125 -> 124 when `scope_fn_arity` went, which held one
`lookup_func_type`; and 124 -> 122 when the probe went, which held two
`lookup_type`. Each golden diff contains only the predicted rows. A re-pin
that lands where it was predicted is evidence; one that lands elsewhere is a
finding, and the two are indistinguishable without the prediction.

#### The sweep's method, and the guard that earned its place

Candidates came from a name-based scan for definitions with no reference of
any kind across compiler, tools, stdlib, tests, runtime and examples - an
upper bound, not a dead list, since a name-based test cannot see an
address-taken function. The control is `make cryo`: a wrongly deleted function
fails to compile and names itself. `make lsp` is a SECOND control and not
redundant, because the LSP links the compiler library and no other local gate
builds it.

Two false-positive classes were caught before deleting rather than by the
build: the bindgen `visit_*` statics are passed by address to
`clang_visitChildren`, and five one-liner methods sit adjacent to live
siblings, where scanning forward for a closing brace at the same indent
swallows the next definition. A guard rejecting any range containing a second
definition caught all five. Across 94 deletions the build found **no** further
false positive, which says the scan was stronger than assumed - not that the
control was unnecessary.

#### Left standing, and what it now measures

Retiring the probes stranded their reporting: `qual_sym_diff` and
`scope_diverge` have no callers, and ten `Site` variants - four `QualSym*`,
six `Home*` - are never bumped and print zero. They are NOT deleted here. The
question they pose has changed shape, though: keeping an audit stream normally
preserves the ability to measure, and these no longer can, because what fed
them is gone.

`scope_fn_arity`'s deletion also removed one of the per-kind lookup callers,
which is what a dead function holding a live call site looks like.

#### Correction to 8.35

8.35 closed by saying `wrap/cgctx-qualify` "survives with its other callers".
It had none. `CodegenContext::qualify` was its only remaining user and was
itself uncalled once 8.35 landed - 8.34 had kept it because
`decl_visit_emitter` reached it through `this.cg`, and 8.35 removed that call.
The site string is retired with the function.

### 8.37 The reporting the probes fed is removed with them 2026-09-02

8.36 retired `qualify_symbol_sym_at` and `probe_ambient_divergence` and left
their reporting standing. This removes it: `qual_sym_diff`, `scope_diverge`,
ten `Site` variants - four `QualSym*`, six `Home*` - the print block for the
ambient-cursor section, and `audit_registry_key`, which had no caller at all.

An audit stream is normally kept even when it reads zero, because the ability
to measure is the thing of value and a zero is a finding. That reasoning does
not reach these. What produced their rows is gone, so they cannot read
anything: they print zero because nothing can bump them, not because a
population measured zero. Keeping them would preserve the shape of a
measurement without its capacity, which is the same defect the probes
themselves were.

`Audit::Scope` is NOT removed. `rn_trace` still gates on it, and
`CRYO_SCOPE_PROBE` still switches a real stream.

#### The control on the b1 gate, and the one that nearly misread it

None of the ten variants is an asserted row, so removing them cannot move
`b1-check` - which held at B1=0 B4=0, 17 sites, all three sections. That claim
survived a scare worth recording: a search of the golden for `home` returns
six hits, which reads as "the Home rows ARE pinned". They are
`2c  home-module (ambient cursor)`, the label of `RnHomeModule` - a different,
live variant, untouched here. A broad grep answering a question about a
specific symbol is how a live row gets deleted; the narrow search was the
correct one and the broad one was the false alarm.

#### One comment named a function that no longer exists

The `Audit` emitters carry a comment explaining why they are free functions
rather than methods, using `Audit::Leaf.scope_diverge(...)` as the
counterexample that would compile and mis-gate. With `scope_diverge` deleted
the example named nothing; it now uses `body_ns_diff`, which is gated on
`Audit::Scope` and makes the same point. A deleted symbol surviving inside the
rationale for a design is how a comment starts describing a tree that is no
longer there.

### 8.38 The import-cycle ordering casualties are gone, and 8.2ak describes a tree that no longer exists - MEASURED 2026-09-02

8.2ak measured 333 unstamped annotations on `examples/09-json-config` and
classified 106 of them as real types that went unstamped because "the export
table is filled in module processing order, so a module whose declarer has not
been walked yet cannot be answered". It named that the import-cycle population,
put it at 4% of annotations, and made it "the question that has to be answered
before any consumer may treat `Pending` as an ICE".

That account rests on the resolver filling its export table during the same
walk that reads it. The code does not do that, and has not since `c5e7c434` -
the day after 8.2ak was written.

#### One caller, and it is a whole sweep earlier

`NameResolver::forward_declare` is what reaches `export_symbol`, and it has
exactly one caller: `NameDeclarationPass::run`. `run_module_resolution` runs
AutoImport + ImportResolution + NameDeclaration across EVERY module, and only
then NameResolution across every module. The export table is therefore complete
before any module's resolving walk begins.

That is the property itself, not a mitigation of its absence: no module order
can put every declarer first inside a cycle, so declaring every module before
resolving any is what makes the invariant hold by construction. 8.2ak's
"populated per module by the forward-declare sweep of `visit(ProgramNode*)`" is
stale in the literal sense - `forward_declare` is not reachable from
`visit(ProgramNode*)` at all.

#### Measured, two corpora, `[host:windows]`, at `651a9ba3`

| outcome | `examples/09-json-config` | `compiler/` |
|---|---:|---:|
| annotations offered to the stamp | 3848 | 33323 |
| stamped `Def` | 2893 | 32203 |
| stamped `GenericParam` | 753 | 917 |
| stamped `PrimTy` | 194 | 194 |
| stamped `TypeRelative` | 8 | 8 |
| UNSTAMPED span names no module | 0 | 0 |
| UNSTAMPED module has no scope | 0 | 0 |
| **UNSTAMPED bare name not in scope** | **0** | **1** |

`qualified spelling` is a subset marker on the way in, not a disjoint bucket:
36 on the compiler corpus, of which 8 leave as `TypeRelative` and 28 as
`ANN-ROOTED` answers folded into `Def`. With that read, both columns close
exactly - 2893+753+194+8 = 3848, and 32203+917+194+8+1 = 33323 - with no
residue on either.

#### What makes the zero readable

* The population GREW rather than shrank: the same project offered 2,699
  annotations when 8.2ak measured it and offers 3,848 now, and the compiler
  corpus is 8.7x larger again. A zero over a vanished population would be the
  uninteresting reason, and this is not one.
* The stream is live. `ANN-UNSTAMPED` is emitted per name, and on the compiler
  corpus it fired - once. 28 `ANN-ROOTED` rows and the `PATH-*` traffic appear
  in the same logs, so a silent stream is ruled out.
* `ANN-QUALIFIED` is 0 on both, so nothing is parked in the qualified-refusal
  exit either.

#### The one survivor is a different defect

The single unstamped annotation across all of `compiler/src` is `CompileMode`
in `Compiler::Passes::DirectiveProcessing`
(`passes/directive_processing.cryo:1780`). That is the module/type name
collision already on record as unimplemented and awaiting a decision - a
namespace and a type contesting one leaf. It is not an ordering casualty, and
no pass reordering would answer it.

#### Consequence for the privatization buckets

Bucket C was carried as "gated on the import cycle", and the honest limit
recorded against it was that no per-site stamp probe had ever been run to say
which sites were actually blocked. The blocking condition is a `Pending` stamp,
and the stamp is answered 33322 of 33323 times on the compiler's own source.
There is no ordering blocker left for those sites to be gated on.

### 8.39 Three decisions: the stamp is authoritative, Function folds into Value, modules bind in the type namespace 2026-09-02

#### 1. A `Res` stamp is AUTHORITATIVE, not advisory

A consumer that reads a stamp takes the answer. It does not read the stamp and
then fall back to a name lookup when the answer does not suit it. Every
"stamp first, then the old cascade" site is therefore wrong by construction
rather than merely suspect, and needs no per-site argument to remove - only the
measurement that says what the old lane was answering.

This is what licenses deleting a lane on its own numbers. It does NOT license
deleting every second lane: a lane addressing a key space no `Res` can name is
a different question, not a second answer to this one. The extern-module alias
lane in the global cascade is the worked example - see 8.40.

Two sites already in violation, both now straightforwardly wrong:

* `call_specializer.cryo:2542` and `:2577` branch on `is_pending()` as a
  DECISION. `res.cryo`'s own contract forbids exactly that: the predicate is
  "for DIAGNOSING an unstamped node at a consumer that holds it, never for
  deciding what to do about one", because a consumer that branches on absence
  is the fallback the type exists to make unwriteable.
* `sema.cryo:3162` reads the stamp, then falls to `lookup_type_by_sym`, then to
  `resolve_primitive` - six answering paths for one name.

#### 2. `Namespace::Function` folds into `Namespace::Value`

Rust's model. Two of the three arms are never constructed anywhere in the tree,
so this collapses dead code toward the intended end state rather than changing
what any program means. The third namespace is reserved for macros, which are
wanted long after the v1.0 freeze; spending it on a Function/Value split that
nothing constructs would spend it on nothing.

`namespace.cryo` opens with a 20-line argument for the three-namespace model it
does not implement. When the arms collapse, that prose goes with them.

#### 3. Modules become bindings in the TYPE namespace

Rust's model, and it retires the parked "module/type collision needs a
decision" item. The collision is not a design question awaiting a bespoke
precedence rule; it is the consequence of Cryo never declaring a module as a
`Symbol` at all. With modules bound in the type namespace, a module and a type
contesting one leaf is an ordinary duplicate definition, diagnosed by the rule
that already exists.

`CompileMode` at `passes/directive_processing.cryo:1780` - the single
unstamped annotation left across `compiler/src` in 8.38's measurement - is
expected to fall out of this rather than needing its own treatment. It is not a
straggler; it is where the missing structure shows through.

To be SCOPED before implementing. It is the largest of the three and it is
structural.

#### The rule these three share

The prose in `resolver/` is ahead of its code in several places: a namespace
model whose arms are never built, a `TypeRelative` variant given four
paragraphs of justification that `resolve_path` has never produced (both its
callers pass a one-element array, so `trailing` is always 0), and a stated
"no variant means missing" alongside a `def_name()` that returns an
empty-string sentinel. 8.2ak's stale premise rode three weeks of handoffs on
exactly that kind of confident writing. When one of these files is touched, the
prose is brought to what the code does.

### 8.40 Line endings: `git ls-files --eol` is the authority, and byte counts are not 2026-09-02

Three consecutive handoffs recorded the tree's line-ending state, in
alternating directions, and all three were wrong. Each measured by counting
bytes in the working tree. That is the wrong instrument, and the reason is
structural rather than a slip.

`.gitattributes` sets `* text=auto eol=lf`: LF in the repository AND in the
working tree, for every text file. `git ls-files --eol` reports index state,
worktree state and the governing attribute together:

    i/lf    w/lf    attr/text=auto eol=lf   compiler/src/.../sema.cryo
    i/lf    w/crlf  attr/text=auto eol=lf   docs/name-resolution.md

Every file in this tree is `i/lf`. The files that read as CRLF are `w/crlf`
DEVIATIONS from the policy, not a convention to be matched. So the advice each
handoff derived from its byte count - "this file is CRLF, preserve it on
write" - perpetuates the deviation, and a handoff that carries it instructs the
next session to keep doing so.

A byte count also cannot see the attribute, which is what makes the working
tree change under a reader: `git checkout -- <path>` restores the file to
policy LF. A session that measured CRLF before a checkout and patched with
CRLF anchors afterwards finds its anchors do not match. That happened here, and
the `count == 1` assert caught it - the same guard that caught the collapsed
escape sequences.

**The rule: write LF. Verify with `git ls-files --eol`, never by counting
CR bytes.** A byte count answers a question about one drifted worktree file;
`--eol` answers the question that was meant, which is what the repository
holds and what the attribute requires.

#### How to check, because the obvious command lies

A fourth session hit this, and not on the rule above - on the instrument used
to test it. **`grep -c $'\r' <file>` is unreliable under Git Bash**: the shell
does not always expand `$'\r'` to a literal CR, and the pattern then degenerates
to one that matches every line. It reports a CR count equal to the file's LINE
count, which reads as "every line is CRLF" and is indistinguishable from a real
CRLF file at a glance.

The tell is exact equality with `wc -l`. Observed here on five known-LF files:
1,677 / 2,335 / 5,654 / 2,056 / 8,777 "CR lines" against 1,677 / 2,335 / 5,654 /
2,056 / 8,777 lines.

Three commands answer it, and they should agree:

    git ls-files --eol <paths>     # the authority: index, worktree, attribute
    tr -cd '\r' < <file> | wc -c   # actual CR BYTES, not lines containing one
    head -c 60 <file> | od -c      # eyeball the line terminators

`tr` is the byte-level check because it counts characters rather than matching
lines, so a broken escape cannot inflate it. All five files above read 0 CR
bytes, `i/lf w/lf`, and `\n` terminators under `od -c`.

State it as the negative too, since that is the form that keeps being
rediscovered: a nonzero `grep -c $'\r'` is NOT evidence of CRLF, and on its own
is not grounds for changing how a file is written.

#### How widespread the deviation is, so the warning is not read as a fault

**670 of 1,400 `.cryo` files are `w/crlf`** - 48% of the tree. Editing one and
staging it prints

    warning: in the working copy of '<path>', CRLF will be replaced by LF the
    next time Git touches it

and that warning is the policy WORKING: `.gitattributes` normalises on the way
into the index, so the committed blob is LF regardless of what the worktree
holds. Verified across a whole session's commits - every blob measured 0 CR
bytes while one of the edited files was `w/crlf` throughout.

Two consequences worth knowing before reacting to it. `git checkout -- <path>`
does NOT normalise a `w/crlf` file: git compares content after normalisation,
sees no difference, and does nothing - so the deviation cannot be cleaned that
way and there is no need to clean it. And because nearly half the tree is
deviant, a session will meet this warning routinely; it is not a signal that
anything was written wrongly, and the check that settles it is
`git ls-files --eol` on the committed path plus `tr -cd` on the blob, never a
count over the worktree file.

### 8.41 The global cascade is a stamp lane and an alias lane 2026-09-02

`resolve_scope_resolution` asked three questions to find the global a
`A::B` path names: a key built from the WRITTEN qualifier, then a linear scan
over every module global matching a namespace SUFFIX, then the BARE member
leaf. Measured over four corpora before changing anything:

| corpus | 1 spelling | 2 scan | 3 bare leaf | none |
|---|---:|---:|---:|---:|
| `examples/09-json-config` | 0 | 106 | 0 | 0 |
| `compiler/` | 0 | 422 | 0 | 0 |
| `ffi_c_import` (`cryo test`) | 51 | 140 | 0 | 0 |
| `tests/` whole tree | 11 | 584 | 0 | 10 |
| total | 62 | 1252 | 0 | 10 |

Step 2 is now one stamp read: `scope_global_type` takes the qualifier's
`Res::Def`, appends the written member leaf, and asks once. On all 1,252 the
stamp reached the POINTER-IDENTICAL `TypeRef` the scan returned - no
disagreement, and no case where the scan answered and the stamp did not. Step 3
is deleted on 0 of 1,314, which 8.39 licenses without further argument.

#### The alias lane is a different question, not a second answer

Step 1 stays, and the reason is measured rather than argued: its 62 answers are
all C-import constants (`cit::FLAG_A`), and on every one the stamp answered
NOTHING. An `extern module` alias is not a Cryo module, the name layer declares
no module symbol for one, so no `Res` can name it; bindgen registers each
constant under the bare alias. That is a key space `Res` does not address,
which is what distinguishes it from a fallback.

#### The corpus that mattered was reachable only by the other tool

On `examples/` and `compiler/` alone, step 1 answers ZERO and reads as dead.
Deleting it there would have broken C imports silently. The population lives in
`tests/tests/projects/ffi_c_import`, whose bindings are in the project's own
`tests/` subdir - compiled by `cryo test` and NOT by `cryo build`. A right
corpus reached with the wrong tool measures a confident zero over nothing.

#### A door was improved, not closed

`lane-check` did not move, and that was predicted: `lookup_global_exact` and
`lookup_global_in_scope` never matched the gate's `.lookup_global(` pattern, so
the two `DeclarationIndex.lookup_global` sites this work was filed under are
untouched. **Improving a cascade and closing a door are different work.** The
door needs the alias key space AND codegen's `resolve_global`, which is live
traffic (285 hits / 970 misses over the four corpora) with no `Res` available
after sema.

The same split holds for the other two doors examined: `lookup_func_return`
keeps a C-import site (`try_resolve_cimport_function`) and a codegen site;
`lookup_method_return` keeps a post-monomorphisation site keyed by a mangled
instantiation name, which a `Res` cannot serve by construction. Counting DI
callers predicts neither the work nor the outcome.

#### The five lookups do not close INDIVIDUALLY, and the reason repeats

Three doors were examined at caller level and none of them closes, each
stopped by the same two things:

| door | blocker 1 | blocker 2 |
|---|---|---|
| `lookup_global` | `cit::FLAG_A` alias key space | codegen `resolve_global` |
| `lookup_func_return` | `try_resolve_cimport_function` | codegen `symbol_resolver` |
| `lookup_method_return` | - | post-mono mangled instantiation key |

Neither blocker is a caller that has not been converted yet. An `extern module`
alias is a key space `Res` does not model at all, and codegen runs after sema
holding names rather than nodes; the third door swaps blocker 1 for an
instantiation name, which 8.20 already established a `Res` cannot name.

So "N callers, staged, one door at a time" is the wrong shape for this work,
and the DI caller count is not a measure of it - it was wrong about the work
AND about the outcome three times running. The doors close when the ALIAS KEY
SPACE and CODEGEN'S POSITION are addressed as their own items. Until then a
caller conversion improves a cascade, which is worth doing on its own merits,
but it does not retire a lookup and `lane-check` will not move for it.

### 8.42 A subsystem built on every compile that nothing ever read 2026-09-02

`ModuleGraph.cross_module_fns` was populated for every function of every module
on every build - a `lookup_func_type`, an arena fetch and a parameter-array
copy each - and its only reader, `find_cross_module_fn`, had no callers at all.
The comment at its call site already said so. Codegen resolves cross-module
references through `SymbolResolver` and never consulted it.

Removed: the descriptor type, the field and its initializer, the registrar, the
reader, the producer, and the per-module loop that drove it.

Predicted before running, and confirmed: `lane-check` 122 -> **119** with all
three from `module_graph.cryo` (the producer held the only three per-kind
lookups in that file), `b1-check` UNCHANGED at B1=0 B4=0 17 sites on all three
targets, `make test` OVERALL PASS 178 / 38 / 0.

The falsifier was any counter movement whatever: a subsystem nothing reads
cannot change a measurement, so a moved counter would have meant something did
read it. Nothing moved.

One movement was NOT predicted and is explained rather than waved at: warnings
349 -> 348. The deleted producer nested `for (mut i: i64 ...)` inside another
loop over `i`, and that shadow was one warning.

#### Open, and it needs a decision

8.32 measured that `resolve_direct_call`'s stamp strictly dominates its
home-qualified guess - 40 answers the guess misses, identical `TypeRef` on the
2 it serves - and deliberately did NOT collapse them, because the losing branch
carries `enforce_callee_visibility` (E0353) and moving the check makes it fire
for those 2 calls, which currently bypass it. 8.39 makes the stamp
authoritative, which points at collapsing; but the collapse changes what an
existing program compiles to, and that is not a consequence to take silently.
**Left standing pending a ruling.**

### 8.43 `resolve_method_owner` is not a stamp conversion - MEASURED 2026-09-02

Queued as one of "the two cascades" to convert by 8.32's method. It is not one,
and both halves of that judgement are measured rather than argued.

`MOWNER-STEP`, one row per outcome:

| corpus | 0 none | 1 as-is | 2 qualified | 3 cross-module |
|---|---:|---:|---:|---:|
| `examples/09-json-config` | 8509 | 6491 | 18 | 56 |
| `tests/` whole tree | 65751 | 57167 | 154 | 644 |

**The widening steps are LIVE.** 798 answers on `tests/` between them, and
step 3 answers more than step 2. There is no zero here to delete on, so the
shape that made the global cascade collapsible is absent.

**And the stamp answers a disjoint population.** At the one caller that holds a
node (`call_resolver` scope-call), comparing the cascade's returned owner
against the scope segment's stamp gives **STAMP-SAME = 0** over 8,029 rows -
and every `STAMP-DIFFERENT` is the cascade returning NO owner, 0 of them a real
disagreement. Where the stamp names a definition the cascade finds nothing;
where the cascade finds an owner the stamp is not a `Def` at all. There is
nothing to replace, because the two never answer the same call.

That is consistent with what the function is for: it reports WHICH of several
candidate names carries a method, and a method's owner is not the thing the
scope segment's stamp names. It is a name-keyed search, so it belongs to the
string seam and will be answered when that seam carries a `Res` - not by a
per-site stamp read.

#### The probe was wrong twice before it was right

First it compared the stamp against the cascade's FIRST step, which on the
global cascade never fires - measuring a door that never opens says nothing.
Then it reported `STAMP-DIFFERENT` without separating "the cascade returned a
different owner" from "the cascade returned nothing", which are opposite
conclusions from one label. Both were caught by asking what the number would
have to mean, not by the build. A comparison probe must record what the
incumbent answered, not only whether the two strings matched.

### 8.44 An unqualified call took a same-leaf function's signature - MEASURED AND FIXED 2026-09-02

`lookup_callee_function_type` asked two spelling-keyed questions for an
identifier callee: the name qualified with the WRITING module, then the BARE
leaf. Its own comment said the bare slot "stays load-bearing", and it does -
but it is single-slot last-write-wins across every module declaring that leaf,
and nothing had ever measured what it answers with.

Measured over four corpora, comparing each against what the incumbent door
actually returned:

| outcome | `09-json` | `compiler` | `tests` | `ffi` |
|---|---:|---:|---:|---:|
| 1-home, stamp agrees | 724 | 2907 | 7040 | 1217 |
| 2-bare, stamp agrees | 20 | 1571 | 8387 | 157 |
| 2-bare, stamp silent | 73 | 2149 | 278 | 226 |
| 1-home, stamp silent | 0 | 0 | 22 | 0 |
| **2-bare, stamp DISAGREES** | 0 | 0 | **18** | 0 |

#### The 18, and why the stamp is the right one

All 18 are unqualified calls to `sleep` and `write` in test modules that
import them. The stamp says `std::time::clock::sleep` and
`std::fs::file::write` - what the writing scope bound. The bare slot returned a
DIFFERENT `TypeRef`, because three modules declare `sleep`
(`ffi::libc::sleep(u32) -> u32`, `thread::sleep(u64) -> void`,
`time::clock::sleep(Duration) -> void`) and five declare `write`, with
signatures that have nothing in common. Home-qualification cannot save it: an
IMPORTED function is qualified by the declaring module, not the calling one, so
the home key misses and the leaf answers.

This type is what expected-type propagation hands the arguments, and the site's
own comment records the consequence - a generic-enum literal argument then
never instantiates and codegen drops its payload store.

#### The stamp does NOT dominate, and the fix does not pretend it does

2,653 calls take the bare door where the stamp is silent. That is why the
spelling lookups stay: the stamp is asked FIRST and settles the question
whenever it makes a claim, and the spelling keys answer only where it makes
none. That is 8.39's rule applied exactly - a consumer takes the stamp's answer
where there is one - and NOT "the stamp replaces the cascade", which the
measurement would not support.

#### A stamp read that does not move the ratchet

The read is `DeclarationIndex::func_type_of_res(r)`, keyed by the `Res`.
Inlining a `lookup_func_type` call at the site would have taken `lane-check`
from 119 to 120 - regrowth, which is what the ratchet exists to catch - for a
change that is architecturally the right direction. A `Res`-keyed accessor on
the owner is the way the gate says to add one: the call site does not match the
five patterns, and the owner's own internal call is excluded. This is decision
2's shape and not a facade, because the public signature takes a `Res` and not
a string.

Predicted before running and confirmed: `lane-check` **119, unmoved**,
`b1-check` UNCHANGED at B1=0 B4=0 17 sites on all three targets, `make test`
OVERALL PASS 178 / 38 / 0, `make lsp` clean. The only behaviour change is those
18 calls, which now use the signature of the function the call actually names.

### 8.45 Scope: replacing the string seam with the `Res` seam - STEPS 2-3 SUPERSEDED by 8.49/8.51

> Step 1 ran and is recorded in 8.49. Its agreement prediction held; its scope
> did not. `canonical_type_name` answers 22 of 78,310 resolutions on
> `compiler/`, so steps 2 and 3 below are aimed at 0.03% of the traffic and are
> **superseded**. The seam that carries it is step 2c, scoped in 8.51, whose
> blocking coverage gap is now closed. `canonical_type_name` stays: it is the
> only step that expands a written qualifier, and its population is small, not
> zero.


The type layer already consults the new name layer - it just receives the
answer as a canonical STRING. `canonical_type_name` asks
`resolve_type_qualified_name_from` with the annotation's home scope and falls
back to `resolve_type_qualified_name` on the ambient chain. Because that string
is accurate, the later steps of `resolve_named`'s cascade are never reached,
which is why the cascade can stand while B1 reads 0.

#### What is actually in it, counted by symbol

Four definitions in `resolver.cryo` (`resolve_type_qualified_name`, and the
`_from`, `_strict_from`, `_bare_from` variants) and **nine** call sites - not
the 44 a raw grep suggests. Five are internal to `resolver.cryo` or are the
NAME layer producing stamps (`name_resolution.cryo:1459`, `:1494`), which is
the seam's correct side and stays. The consumer side is **three**:
`types/resolver.cryo:1553` and `:1556` (both inside `canonical_type_name`) and
`sema/type_utils.cryo:384`.

`canonical_type_name` has exactly ONE caller: step 3 of `resolve_named`.

#### What it touches, and why the threading is small

`resolve_named(name, ctx)` takes a SPELLING plus a `ResolutionContext` whose
fields are strings and `TypeRef`s - no `Res` anywhere. Its entry point
`resolve_named_at_span(name, span, ctx)` derives the home module from the SPAN,
which is the string-shaped stand-in for the stamp.

That entry point has **two callers**, `types/resolver.cryo:376` and `:1895`,
and BOTH are `TypeAnnotation::Named` dispatch arms holding the annotation node.
The node already carries the answer: 8.38 measured the annotation stamp
answered 33,322 of 33,323 on the compiler's own source. So the work is to pass
the node's `ResSlot` where the span is passed now, and to have step 3 read it
instead of rebuilding the name.

#### It stages, in three steps with a measurement between each

1. Thread the `ResSlot` to `resolve_named` and PROBE only - for every
   annotation reaching step 3, does the stamp's `Res::Def` reach the same
   `TypeRef` `canonical_type_name` produced? Nothing changes behaviour.
2. Make the stamp authoritative where it answers, keeping the string path only
   for where it does not, exactly as 8.44 did for the callee type.
3. Delete `canonical_type_name` and the two consumer-side variant calls once
   the population reaching them is zero, with the call count as the control.

#### The prediction, stated before step 1 runs

Agreement, not coverage, is the risk: coverage is already known to be
33,322/33,323. So the prediction is that step 1 reports STAMP-SAME for
substantially all annotations reaching step 3, with any `STAMP-DIFFERENT`
being a real disagreement about which declaration a written type names - the
same shape as 8.44's 18, and to be investigated as a defect rather than
smoothed over. A material `INCUMBENT-ONLY` population means the stamp does not
dominate and step 2 keeps the string path for it, which 8.44 establishes is a
normal outcome rather than a failure.

The probe must record WHAT THE INCUMBENT ANSWERED, not merely whether two
values matched - 8.43 records what happens twice when it does not.

### 8.46 The visibility gate 8.32 parked on would not have fired - MEASURED 2026-09-02

8.32 measured that `resolve_direct_call`'s stamp strictly dominates its
home-qualified guess and then declined to collapse the two, on this reason:
the losing branch carries `enforce_callee_visibility`, and "moving the check
would also make it fire for the 2 calls the home lookup serves, which currently
bypass it, and that changes what a program compiles to."

That was a hypothesis about what WOULD happen, and it was never run. Running it
means asking the gate's own conditions at the home-lane exit without changing
anything.

| corpus | home-lane calls | would REJECT (E0353) |
|---|---:|---:|
| `compiler/` | 2 | **0** |
| `tests/` whole tree | 64 | **0** |
| `ffi_c_import` | 48 | **0** |

Every one is `would-SKIP:same-module`. Simulating the gate on what the STAMP
lane would actually hand it - the single qualified alternative, which is the
value the existing code passes - gives `alt-SKIP:same-module` for all of them
too; the 12 on `tests/` with no single alternative are calls the stamp lane
does not offer to the gate at all.

#### The reason is structural, not incidental

`qualify_symbol_sym_home` qualifies with the module that WROTE the call. A name
that lookup answers is therefore declared in the caller's own module - and the
gate's first exemption is a module calling its own items (spec §4). A private
callee in ANOTHER module cannot be reached through this door, so the door
cannot be the thing shielding one.

So the parked reason is neither of the two shapes it could have had. It is not
"collapsing introduces E0353 for two real violations", and it is not "the gate
would wrongly reject two legitimate calls". **The gate is a no-op over this
population**, and the blocker does not exist.

#### Stated as a property, because it cannot drift

This is not a number that needs re-measuring as the tree changes. It follows
from two facts that are true by construction: the home lane's key is built by
`qualify_symbol_sym_home`, which qualifies with the module the call was WRITTEN
in; and the gate's first exemption is a module calling its own items. A name
the home lane answers is therefore declared in the caller's own module, and no
such call can be a cross-module privacy violation. The measurement above is a
control on that reasoning, not the reason.

#### The identity check ran, and the collapse is taken

The binding axis was the remaining question - 8.32 had established identical
`TypeRef`s on one corpus only. Re-measured over all three, recording what the
incumbent answered: **114 of 114 STAMP-SAME** (2 `compiler/`, 64 `tests/`, 48
`ffi_c_import`), with no `STAMP-DIFFERENT`, no `STAMP-EMPTY`, and no call where
the home lane answered while the stamp did not.

So the home-qualified lane answered the same declaration wherever it answered
at all, and it is deleted. `resolve_direct_call` now asks the stamp and nothing
else. `lane-check` 119 -> **118**, predicted before it was run, one row off
`call_resolver.cryo`.

### 8.48 The bare function-type slot names a different declaration for 245 calls - MEASURED 2026-09-02

`DeclarationIndex.func_type_refs` is ONE slot per LEAF for the whole program,
last-write-wins across every module that declares that leaf. 8.44 fixed a
consumer that read it; this records what is actually in it, because the
exposure is a property of the map and not of that consumer.

Measured by asking, at every call whose identifier carries a stamp, what the
bare leaf would have returned and which declaration owns it:

| corpus | call sites where they differ |
|---|---:|
| `compiler/` | 7 |
| `tests/` whole tree | 234 |
| `ffi_c_import` | 4 |

Twenty distinct leaves on `tests/` alone. Six of the collisions are stdlib
against stdlib, so they are not an artefact of test naming:

| leaf | the stamp names | the bare slot holds |
|---|---|---|
| `sleep` | `std::time::clock::sleep` | `std::thread::sleep` |
| `read` | `std::fs::file::read` | `std::core::intrinsics::read` |
| `write` | `std::fs::file::write` | `std::core::intrinsics::write` |
| `malloc` | `std::ffi::libc::malloc` | `std::core::intrinsics::malloc` |
| `free` | `std::ffi::libc::free` | `std::alloc::heap::free` |
| `byte_at` | `std::encoding::base64::byte_at` | `std::net::ws::frame::byte_at` |

#### Realized versus latent, kept apart

245 is the EXPOSURE - calls where the two disagree about which declaration the
name means. The REALIZED subset is 8.44's 18: the calls that actually reached
the bare door, because the module-qualified lane missed as well. The rest were
shielded by that lane rather than by anything checking the answer.

Since 8.44 the stamp is asked first, so all 245 now bind the declaration the
name resolves to. The bare door still stands for the 2,653 calls where the
stamp is silent, and nothing establishes that those are collision-free - that
population has not been measured, and a leaf-keyed answer there is exposed in
exactly the same way.

#### A correction to what was reported in conversation

The `sleep` collision was described mid-session as involving
`ffi::libc::sleep(u32) -> u32`. That was inferred from three `sleep`
declarations existing, and it is wrong: the bare slot holds
`std::thread::sleep`. Three do exist and any of them could have been the
last writer, which is the point - WHICH one it is was never a property of the
program, only of registration order. The inference was reported before it was
measured, and the measurement disagrees with it.


### 8.47 The two `is_pending` decisions: one is dead, one is a boundary 2026-09-02

`res.cryo` states that `is_pending` is "for DIAGNOSING an unstamped node at a
consumer that holds it, never for deciding what to do about one", because a
consumer that branches on absence is the fallback the type exists to make
unwriteable. Two sites in `call_specializer.cryo` branched on it. Measured
before touching either:

| site | `examples` | `compiler` | `tests` | `ffi` |
|---|---:|---:|---:|---:|
| `scoped_template_from_stamp` | 0 | 0 | 0 | 0 |
| `find_function_template_for_call` | 1 | 19 | 64 | 32 |

**The first never fires on any corpus** - the slot is never null and never
pending there - so the guard decided nothing. It is removed: `require` yields
`Err` for an unanswered slot and the match below declines it exactly as it
declines a non-definition, so the outcome is identical and the forbidden shape
is gone.

#### The second is extern and intrinsic names, and it is PARKED

The 116 pending askers were emitted by name. They are `strlen` and the
`atomic_*` family - `extern "C"` twins and compiler intrinsics. That is NOT the
post-monomorphisation case where a `Res` cannot name an instantiation; it is
the same class `enforce_callee_visibility` already exempts as "not a module
member to enforce against".

Removing this guard would put 116 calls through `require`, each recording a
pending bug, which prints on an otherwise successful build. That is
user-visible output, so it is written up rather than taken. The question it
raises is the useful one: an `extern "C"` function IS declared, so whether
these are legitimately unstampable or simply unstamped is a real question, and
the answer decides whether the guard becomes a documented boundary or the nodes
get stamped.

#### The comment that contradicted its own file

The doc comment two definitions below claimed `is_pending` is "used for
diagnosis here and nowhere else" - falsified by the two sites above, in the
same file. It turned out to be a stale paragraph left standing in front of the
comment that replaced it, so the file stated both the old claim and the new one
at once. Removed.

### 8.49 The string seam 8.45 scoped carries 0.03% of the traffic - MEASURED 2026-09-02

8.45 scoped the replacement of `canonical_type_name` on the reasoning that the
type layer consults the name layer and receives the answer as a canonical
string, and anchored its prediction on 8.38's annotation-stamp coverage of
33,322 of 33,323. Step 1 was to thread the annotation's `ResSlot` into
`resolve_named` and compare, at that seam, what the string path produced
against what the stamp names.

The threading is done and the probe ran. The agreement prediction held. The
scope did not.

#### Stated before the run

Substantially all annotations reaching step 3 would be `STAMP-SAME`; any
`STAMP-DIFFERENT` is a real disagreement about which declaration a written type
names and is investigated as a defect; a material `INCUMBENT-ONLY` population
means the stamp does not dominate and step 2 keeps the string path for it. The
falsifier written down for coverage was that materially fewer stamps at this
site would mean the population reaching the seam is exactly the one the stamp
does not cover, and step 2's premise fails.

#### What answers, over four corpora

`cryo build --no-incremental` with `CRYO_RN_AUDIT` and `CRYO_RESOLVE_COUNTER`,
counting every `resolve_named` exit:

| corpus | total RN answers | 2c home-syntax | reach `canonical_type_name` | it answers |
|---|---:|---:|---:|---:|
| `compiler/` | 78,310 | 61,290 | 64 | **22** |
| `examples/09-json-config` | 9,521 | 5,749 | 14 | **0** |
| `tests/…/ffi_c_import` | 7,472 | 4,145 | 14 | **0** |
| `examples/14-threads` | 8,944 | 4,868 | 65 | **11** |

On the compiler's own source the seam is consulted 64 times in 78,310
resolutions and answers 22 of them - **0.03%**. Steps `3c-di-bare` and
`5-leaf-index` answer zero on every corpus.

#### The two populations that reach it, and neither is the general case

Every row that reaches `canonical_type_name` is one of exactly two shapes, and
they are the same four corpora over:

**Partially-qualified written names**, which is all of the productive traffic:
`ResolveCounter::Site` -> `Compiler::ResolveCounter::Site`, `stdio::Stdin` ->
`std::io::stdio::Stdin`, `mpsc::Sender` -> `std::sync::mpsc::Sender`. The work
being done is expanding a written prefix to the canonical path. All 22 on
`compiler/` and all 9 on `14-threads` carry a `::`.

**Bare generic parameters that then fail** - `T` and `C`, 42 of them on
`compiler/`, of which 36 carry no stamp at all and 6 stamp `GenericParam`. The
`NEITHER` count equals the `X-failed` count exactly on every corpus: nothing
that reaches this seam without a written qualifier is ever answered by it.

A bare name that succeeds never gets here. Step **2c** takes it first.

#### Agreement held; coverage did not

Where both sides answer: **31 of 31 `SAME`** (22 `compiler/`, 9 `14-threads`),
**0 `DIFFERENT`**, 0 `STAMP-ONLY`. Two `INCUMBENT-ONLY`, both `mpsc::Sender`
with a `Pending` slot - the same spelling that is stamped on three other rows
in the same build, so it is a stamping gap and not a disagreement.

So the agreement prediction is confirmed and the coverage prediction is
falsified, in the shape the falsifier named: the rows reaching this seam are
disproportionately the ones the stamp does not cover, because they are
overwhelmingly generic parameters and failures.

#### Where the string seam actually is

`resolve_named` step 2c answers 61,290 of 78,310 on `compiler/` and the
plurality on every other corpus. It reaches its answer by building one:

    this.intern_table.intern(ctx.home_module + "::" + name_str)

`ctx.home_module` is derived from the annotation's SPAN, through
`home_ns_of_file` - which is the string-shaped stand-in for the stamp that 8.45
itself names, one step above the function it went on to scope. The stamp
replaces 2c's INPUT, not `canonical_type_name`'s output. Retargeting is a
separate scoping exercise and is not taken here.

#### What this does not license

It does not license deleting `canonical_type_name`. The population is small,
not zero, and it is the only step that expands a written qualifier - the
measurement above is the control on that, not a clearance. All four corpora
were built with `cryo build`, which does not compile the orphan modules `cryo
test` reaches, and `tests/` as a whole is unmeasured here; 8.48 found 234 of
its 245 collisions in exactly that tree.

#### The probe

`rn_seam_probe` compares the two as NAMES rather than looking each one up. The
index lookup is a deterministic function of the name handed to it, so equal
names reach one type without a second lookup proving it - and a second lookup
would have added a per-kind call site to the surface `lane-gate.py` pins,
making the instrument indistinguishable from the regrowth that ratchet exists
to catch. `LOOKUP` stayed at 118 across the change, which was predicted before
it was run. The comparison over-reports rather than under-reports: two names
aliasing one target are distinct here and identical after the alias walk, so a
`DIFFERENT` row would be a candidate to adjudicate rather than a defect already
established. None appeared.

### 8.50 The bare door's residual exposure is one signature-identical call - MEASURED 2026-09-02

8.48 left the 2,653 stamp-silent calls reaching the bare `func_type_refs` door
unmeasured for leaf collisions, and said so: "nothing establishes that those are
collision-free". This measures them.

#### Stated before the run

A non-trivial share of bare answers would land on a leaf more than one module
declares. The falsifier: if that share is zero across every corpus, the residual
exposure is closed by measurement rather than by argument, and 8.48's fork can
be answered instead of carried.

#### Method, and the control on the instrument

`lookup_callee_function_type` has three exits - the stamp, the home-qualified
lookup, and the bare slot. A row is emitted at each, carrying the leaf and how
many DISTINCT modules registered a function type under it, read from the index's
own owner-aware overload arrays. Logging every door and not only the bare one is
deliberate: the exposure is a fraction, and a numerator without its denominator
is the shape this ledger has had to retract before.

The instrument was controlled against 8.48's own collision list before its zero
was believed. It reports **4** distinct declarers for `read`
(`intrinsics`/`ffi::libc`/`sys`/`fs::file`), **3** for `free`
(`intrinsics`/`alloc::heap`/`alloc::allocator`) and **2** for `sleep`
(`time::clock`/`thread`) - the exact pair 8.48 recorded. So a colliding leaf is
visible to it.

**Every one of those reaches the STAMP door and never the bare one.**

#### What answers, over five corpora

| corpus | STAMP | HOME | BARE | BARE on a plural leaf |
|---|---:|---:|---:|---:|
| `compiler/` | 4,478 | 0 | 2,173 | 0 |
| `examples/09-json-config` | 744 | 0 | 73 | 0 |
| `tests/…/ffi_c_import` | 550 | 0 | 73 | 0 |
| `examples/14-threads` | 740 | 0 | 194 | 0 |
| `tests/` (41 projects) | 16,364 | 6 | 2,394 | **1** |

On `tests/`, **618 STAMP rows carry a plural leaf** - calls that would be
exposed and are not, which is the value of asking the stamp first, measured
rather than asserted.

#### The one exposed call, and why it cannot harm

`tests/tests/projects/visibility_gate`, the leaf `stash`, declared by both
`VisibilityGate::Annex` and `VisibilityGate::Vault`. The project exists to
construct exactly this: its own source says the second declarer's "only job is
to push `Main`'s bare `stash(1)` off the binder's fast path".

Both declarations are `(v: i32) -> i32` - the same signature, on purpose, so
that which one binds is not visible in the result. The harm the bare slot can do
is hand expected-type propagation a WRONG signature; two identical signatures
cannot. The choice of which function is actually called is made by
`try_pin_overload_mangled_callee`'s import-scoped path, not by this hint.

#### The metric's blind spot, and its control

Owner 0 is a global claim naming no module, so a leaf claimed only by owner-0
entries would read as singly-declared however many modules wrote it. Across all
five corpora that reduces to eight leaves, and cross-tabulating owner count
against whether the door answered closes it: six of them (`Parser`, `ASTCloner`,
`NameResolver`, `SemaVisitor`, `IRGeneratorVisitor`, `ASTDumper`) **miss** - no
answer, so no wrong answer - and the two that hit, `_open_osfhandle` and
`_get_osfhandle`, are each declared once.

`Parser` is the instructive one: it IS plural (`Compiler::Parser::Parser` and
`std::json::parser::Parser`, and the compiler imports the latter), and it still
cannot be exposed, because no function type is registered under the bare leaf
for a constructor to answer from.

#### A zero that had the wrong population, caught in time

The HOME door answers **0** on all four build corpora, and a deletion was very
nearly proposed on that - the shape 8.46 used to retire a lane. `tests/` answers
**6**, all of them the synthesized `main$async` of the three `async_main`
projects. The door is alive and serves one synthesized-name population. Four
corpora agreeing on zero were four corpora that did not contain the case.

#### What this answers, and what it does not

8.48's fork was: leave the bare door shielded by its consumers, or make the map
owner-aware so a leaf cannot answer for a module that did not declare it.
Shielding is **empirically sufficient today** - the residual is one call, and it
is signature-identical. The owner-aware map remains the structurally correct
fix; what changes is that its size is now known rather than feared.

It does not close the question. All five corpora were built with `cryo build`,
which does not compile the orphan modules `cryo test` reaches; 14 of the 41
`tests/` projects are compile-fail gates whose rows stop at the diagnostic. And
the bare count here is not 8.48's 2,653 - that figure and this one are taken at
different sites and are not claimed to measure the same population.

### 8.51 The real string seam agrees 40,450 of 40,450, and is blocked on one unstamped owner - MEASURED 2026-09-02

8.49 located the seam that carries the traffic: step 2c, which answers 61,290 of
78,310 type resolutions on `compiler/` by building
`intern(ctx.home_module + "::" + name_str)`. This measures whether the stamp can
replace that construction, by the same name-comparison the 8.49 probe uses.

#### Stated before the run

Volume of order 61,000; high `Def` coverage, on 8.38's 33,322-of-33,323; a large
`SAME` majority. A material `DIFFERENT` population is a real disagreement about
which declaration a bare annotation names - the 8.44 shape - and a defect. A
material `INCUMBENT-ONLY` population means the stamp cannot replace 2c wholesale.

#### What it answers

| verdict | rows | share |
|---|---:|---:|
| `SAME` | 40,450 | 66% |
| `INCUMBENT-ONLY` | 20,844 | 34% |
| `DIFFERENT` | **0** | - |
| `STAMP-ONLY` | 0 | - |

**Where the stamp answers it agrees, 40,450 of 40,450, with no exceptions.** So
agreement is not the risk at this seam either, and the retarget is not blocked on
correctness. It is blocked on coverage: every one of the 20,844 is `Pending` -
not a non-definition answer, but no answer at all.

#### The coverage figure that was inherited, and what it actually counted

8.45 anchored its prediction on the annotation stamp answering 33,322 of 33,323,
read as "annotations reaching the resolver carry a stamp". At this seam 34% do
not. The two are not in conflict; they count different populations, and only one
of them is the population a consumer meets. A coverage figure taken at the
stamping pass cannot stand in for one taken at the reader.

#### It is one owner, not a spread

| written name | unstamped rows |
|---|---:|
| `GlobalAlloc` | 17,877 |
| `TypeRef` | 684 |
| `LValue` | 420 |
| `String` | 318 |
| everything else | ~1,545 |

`GlobalAlloc` is **86%** of the gap, and its home modules are
`std::collections::array` (14,644), `::string` and `::hashmap`. That is the
signature of one construct: `struct Array<T, A = GlobalAlloc>`. Names such as
`TypeRef` and `LValue` appear in BOTH columns, which is the same story - the
parser-produced use is stamped and the default-argument use is not.

#### The cause

`expand_default_type_args` resolves `gp.default_annotation`, which is a real
parser-produced annotation carrying a `res` slot - the `= GlobalAlloc` in the
template's own declaration. Nothing ever writes that slot.

`declare_generics` is, by its own doc comment, the single call site for
introducing generic parameters. It declares the parameter NAMES and never calls
`stamp_annotation` on their defaults; the identifier `default_annotation` does
not occur anywhere in `name_resolution.cryo`.

So a generic parameter's default is the sixth owner of written syntax the
stamping pass does not visit, after the four already closed and the impl head.
It is not a synthesized node that legitimately has no stamp - it is written type
syntax with a span and a slot, in a file the pass walks.

#### Why this is the whole lever

Closing it takes stamp coverage at the seam carrying 78% of type resolutions
from 66% to roughly 95%, against a measured zero disagreements. No other change
found so far moves the migration by a comparable amount, and none of the
alternatives is a one-owner fix.

#### Fixed, and what the prediction said first

`declare_generics` now stamps each parameter's default after declaring the
parameter NAMES, so a default naming an earlier parameter (`<T, U = T>`) is
answered by that parameter rather than searched for among declarations. It is
the same ordering the sibling `stamp_annotation(param.type_annotation)` call
already depends on, and all seven `declare_generics` call sites are in the
resolving pass's `visit` methods - none in forward declaration, which matters
because `answer` is first-write-wins and an early wrong answer would be
permanent.

Predicted before the run: `INCUMBENT-ONLY` to about 2,970, `DIFFERENT` to stay
at 0, gates unchanged. A nonzero `DIFFERENT` would have meant the stamp just
written disagrees with 2c - a defect in the fix, not a finding about the seam.

| | before | after |
|---|---:|---:|
| `SAME` | 40,450 | **58,115** |
| `INCUMBENT-ONLY` | 20,844 | **3,180** |
| `DIFFERENT` | 0 | **0** |
| coverage | 66% | **94.8%** |

Agreement is now 58,115 of 58,115. `LOOKUP` 118, `REENTRY` 6, B1 0 and B4 0 on
all three arms, 178 compile-fail cases and 38 projects unchanged - the last of
those is the one that mattered, because stamping runs the reachability gate over
syntax it had never been run over, and a default naming something its own module
cannot reach would have produced a diagnostic no program had seen before. None
appeared.

#### The residue, and one thing it is not

3,180 remain, now spread rather than concentrated: `TypeRef` 684, `LValue` 420,
`String` 318, and `GlobalAlloc` down to 213 from 17,877. That last 213 is worth
naming as unfinished rather than rounded away - the same construct is stamped
17,664 times and unstamped 213 times, so there is a second path producing these
nodes, and cloning is the obvious suspect (`ASTCloner` copies a default
annotation, and a `Res` is copied verbatim by contract). It was not chased here.

#### Agreement re-measured over 44 corpora

`compiler/` alone is one corpus, and 8.48 found 234 of its 245 collisions in
`tests/`, so the agreement claim was re-taken over the three build corpora plus
all 41 `tests/` projects:

| | rows |
|---|---:|
| 2c `SAME` | 133,926 |
| 2c `INCUMBENT-ONLY` | 20,438 |
| 2c `DIFFERENT` | **0** |
| 3b `NEITHER` / `SAME` / `INCUMBENT-ONLY` | 517 / 12 / 2 |

**Zero disagreements in 154,895 rows.** Coverage is 86.8% across all corpora
against 94.8% on `compiler/` alone, so the residue is proportionally larger
outside the compiler and the `compiler/`-only figure would have flattered it.

The residue is a fixed population, not a growing one: every one is a stdlib
generic type written in a stdlib module - `std::core::iter` (3,861),
`std::collections::hashmap` (3,790), `::array`, `::str` - spelled `String`,
`RefIter`, `Str`, `TakeIter`, `MapIter`, `ValuesIter`. Many appear exactly 585
times, which is 44 corpora times a constant: the stdlib resolved once per build.
The clone is not the cause - `TypeAnnotation::clone` copies `res` verbatim, and
`NameResolution` runs before `DefaultExpansion`, so a cloned default carries the
stamp the fix now writes. Locating the construct needs the annotation's own span
on the probe row, which it does not carry.

#### What blocks the retarget now, and it is not agreement

Making 2c authoritative requires the answer to a design question rather than
another measurement. Decision 1 says a consumer reads the stamp and takes the
answer, and that "stamp first, then the old cascade" is wrong by construction.
At 86.8% coverage a retarget can only be that hybrid - unless the residual
13.2% is stamped first. So the sequencing is: close the coverage gap, then
collapse 2c; not collapse 2c behind a fallback.

### 8.52 The remaining unstamped annotations were never offered to the stamper - MEASURED 2026-09-02

8.51 closed the largest stamping gap and left 13.2%. Two hypotheses about the
residue were formed and both were wrong before anything was changed, which is
recorded because each looked obviously right:

- **`stamp_annotation` has no `Generic` arm.** Every residual spelling is a
  generic type written `Foo<...>`, so a missing arm would explain all of it. It
  has one, and it stamps both the base and every argument.
- **The clone drops the stamp.** `TypeAnnotation::clone` copies `res` verbatim,
  and `NameResolution` runs before `DefaultExpansion`, so a cloned default
  carries the stamp.

#### Instrumented instead of guessed a third time

The stamper's failure arm was a bare `_ => { }`: a silent decline. That makes
"the stamper refused this name" and "this node was never offered to the stamper"
the same observation at every reader downstream, and they imply opposite fixes -
a lookup that cannot see the name, versus a node minted after the pass ran. An
event was emitted at the arm.

**`STAMP-DECLINE` is 0** on `examples/14-threads`, whose 2c residue is 58
`Shared`, 56 `String`, 45 `Payload`, 30 `ChanNode`, 24 `ScopePayload`.

#### The control on that zero

Both this probe and the existing impl-head decline counter read 0 on that
corpus, which is consistent but proves nothing on its own - a probe that never
fires reads the same as a stamper that never refuses. Run over
`tests/tests/projects/namespace_gate`, the probe emits **6** declines, all the
spelling `Crate` at `src/main.cryo:11-14`. So it fires, and `type_spelling_res`
does return `Pending` when a name is genuinely unreachable.

The zero is therefore real, and it settles the question: these nodes are **not
declined, they are never presented**. Every one names a type that is declared in
a real file (`Shared`, `Payload`, `ScopePayload` in `stdlib/thread/_module.cryo`;
`ChanNode`, `ChannelInner` in `stdlib/sync/mpsc.cryo`), so the SPELLING is
resolvable - it is the NODE that postdates the pass.

#### What that makes it

A different defect class from 8.51's. That one was written syntax the pass
walked past and could simply visit. This one is annotation nodes minted or
rebuilt after name resolution has finished, by async lowering, monomorphisation
and the mint sites in `type_resolution` and `expr_parser`. The rule they violate
is already stated - a synthesizer owes its node a `Res` - and the fix is
per-synthesizer, because only the minting site knows what its node means.

It is scoped, not taken. Closing it is what makes 2c collapsible without the
fallback decision 1 forbids, and it is the last thing standing between the
measurements above and that collapse.

### 8.53 Two inherited claims corrected, with their derivations 2026-09-02

Both arrived through the handoff's "assumptions I did NOT verify" section, and
both were relayed onward as fact before anyone re-derived them. Recorded here
rather than fixed privately, because a correction that lives only in a session's
context is inherited as the original error.

#### `pre_resolved` has six readers, not one

The claim: "`pre_resolved` reduces to one answering site
(`types/resolver.cryo:364`), with three `substituter` sites minting `Pending`."

Derived under `lane-gate.py`'s own rules - `//` tails stripped, comment-only
lines dropped - over every `.cryo` file in `compiler/src`:

| site | what it does |
|---|---|
| `types/resolver.cryo:364-365` | **answers** - returns the value |
| `passes/type_resolution.cryo:342` | guard: `is_valid()` then skip |
| `passes/type_resolution.cryo:390` | guard |
| `passes/type_resolution.cryo:1975` | guard |
| `sema/member_resolver.cryo:838` | guard |
| `AST/_module.cryo:350` | the clone copies it (defining file) |

**Six read sites, five outside the defining file: one answering and four
guards.** The one answering site is right; the four guards were never
enumerated.

The second half is wrong twice over. The substituter writes the field at **five**
sites - `:196`, `:272`, `:291`, `:1010`, `:1031` - not three; two further
`inner_pre_resolved` occurrences at `:1006` and `:1027` are PARAMETER names, not
field accesses, which is the likeliest way a count of three was reached. And
none of them mints `Pending`: `Pending` is a `ResSlot` state, `pre_resolved` is a
`TypeRef` whose absent value is `TypeRef::invalid()`, and the substituter writes
REAL TypeRefs there (`resolved_arg_typeref`, `spec_typeref`) - it is the site
that makes them valid, the opposite of minting absence. `res.cryo` states the
distinction between the two fields explicitly.

Why it mattered: 8.45 step 3 deletes `canonical_type_name` "once the population
reaching them is zero, with the call count as the control". A deletion control
needs every reader of the seam, and four of them were invisible.

#### The section-coordinate comments are not concentrated in `resolver/`

The claim, from the cleanliness audit: "15 of 19 section-coordinate comments sit
in `resolver/`."

    grep -rn '§[0-9]' compiler/src --include=*.cryo | wc -l     -> 103
    grep -rln '§[0-9]' compiler/src --include=*.cryo | ...      -> 19 files

**103 comment lines across 19 files in 7 directories**, of which **3 files** are
under `resolver/`: `compiler/` 6, `sema/` 5, `resolver/` 3, `codegen/visit/` 2,
and one each in `types/`, `passes/`, `mono/`. The audit appears to have counted
FILES, got 19, and attributed them to one directory.

The house rule against spec coordinates in comments still applies to all 103;
what changes is that cleaning `resolver/` would address three files, not
fifteen, so the work is spread rather than concentrated.

### 8.54 `selfhost-check`'s Linux arm leaves an ELF where the Windows binary was 2026-09-02

The Linux arm's stage 4 builds to `build` - `compiler/build` itself, not a
stage-private directory the way the Windows arm's `build/self/win-s2..s4` and
the Linux arm's own `build/self/s3..s4` do. Combined with the run's "Wiping
stage outputs" step, a `make selfhost-check` on Windows ends with
`compiler/build/cryo.exe` **deleted** and `compiler/build/cryo` an
`ELF 64-bit LSB pie executable`.

`make test` does not rebuild the compiler. So a `make test` straight after a
green `selfhost-check`, without `make cryo` first, runs against a missing or
foreign-architecture binary - which presents as a test failure with no source
change behind it, the exact shape this document elsewhere warns reads as a
regression. `make cryo` restores the PE32+.

`stdlib/.bin` is NOT affected: it holds `x86_64-pc-linux-gnu`,
`x86_64-pc-windows-gnu` and `self` side by side, so the per-triple split does
here what it was built to do. It is `compiler/build` alone that is shared.

### 8.55 `CompileMode` was a missing import, not the module/type collision - MEASURED AND FIXED 2026-09-02

Scoping decision 3 begins with the case it is expected to subsume. 8.38 recorded
the single unstamped annotation in `compiler/src` as `CompileMode` at
`passes/directive_processing.cryo:1780` and classified it: "That is the
module/type name collision already on record as unimplemented and awaiting a
decision - a namespace and a type contesting one leaf." 8.39 carried that
forward: it "is expected to fall out of this rather than needing its own
treatment. It is not a straggler; it is where the missing structure shows
through."

Both are wrong. It was a straggler, and one import closed it.

#### The mechanism, read rather than inferred

`type_spelling_res` returns `Pending` at `AnnResUnresolved` when
`resolve_type_qualified_name_bare_from(home_scope, name)` cannot answer - that
is, when **the writing module's own scope cannot reach the name**. It is a
reachability fact about one file's imports, not a contest between two
declarations.

`passes/directive_processing.cryo` imports `Compiler::CompilationContext` and
does not import `Compiler::CompileMode`. Nothing else brings the name in: using
a value of a type does not bind the type's name.

#### The experiment, with its falsifier stated first

Predicted: adding `import Compiler::CompileMode;` drives `ANN-UNSTAMPED` from 1
to 0. Falsifier: if it stayed at 1 the name was already reachable and something
else blocked stamping, which would have supported 8.38's reading over this one.

| | `ANN-UNSTAMPED` on `compiler/` |
|---|---:|
| before | 1 (`CompileMode`, `directive_processing.cryo:1780:13`) |
| after | **0** |

Control on the zero: the same run emits **97,345** `PATH-HIT` rows, so the
stream is live and the zero is an absence of events rather than an absence of
instrumentation.

#### The collision the decision dissolves has no instances

The tree was counted for both shapes a module/type leaf clash can take, over
`compiler/src` and `stdlib` - 319 modules:

| shape | meaning | count |
|---|---|---:|
| A | the type is declared INSIDE the module whose leaf it shares (`namespace A::B;` + `type struct B` in that file) | **76** |
| B | a module `A::B` and a type `B` contesting the SAME scope `A` | **0** |

Shape A is 24% of every module in the tree - it is the file-per-type convention
(`Compiler::Codegen::Ops::ExprOps` holding `type class ExprOps`). Under Rust's
model these do NOT collide: the module binding lives in the PARENT's type
namespace and the type in the module's own, so they are never in one scope.
`CompileMode` is Shape A.

Shape B is the one 8.39 describes as becoming "an ordinary duplicate
definition", and there are none.

#### The defect backlog decision 3 was chosen partly to clear is EMPTY

Stated plainly, because the decision was taken partly on the grounds that it
"retires the parked module/type collision item":

**There is no parked collision to retire.** Shape B has zero instances across
319 modules. The one case ever cited as an instance - `CompileMode` - was a
missing import and is fixed. Nothing in `compiler/src` or `stdlib` today is
waiting on this decision.

The decision may still be right: modules are genuinely not `Symbol`s in this
compiler, and Rust's model is a real structural improvement. But the case for it
has to be made on what the structure buys, because:

* its named beneficiary is fixed, and was never an instance of the problem;
* the collision shape it dissolves has zero occurrences in the tree;
* the shape that IS pervasive is already legal under the model being adopted,
  so adopting it neither breaks nor improves those 76.

This is a re-decision, not a footnote: whoever chose it should get to weigh it
against the real evidence rather than against a backlog that does not exist.

#### How the wrong cause propagated, which is the reusable part

The mechanism matters more than the instance. A cleanliness audit observed ONE
symptom - a single unstamped annotation - and inferred a STRUCTURAL cause for it
(a namespace and a type contesting one leaf). Nobody checked the file's import
list, which is four lines long and settles it. The inference was then repeated
into 8.38, carried into 8.39 as the justification for a design decision, and
relayed onward twice more as fact. Five restatements, no measurement.

That is the shape of 8.2ak exactly: a plausible structural story attached to a
real symptom, repeated until its confidence came from the repetition rather than
from evidence. The guard is the same one this section keeps arriving at - a
claim about a CAUSE has to name the measurement that distinguishes it from the
next cause over, and "the file does not import the name" was one grep away
throughout.

### 8.56 PARKED FOR JAKE: does Cryo keep a qualifier shorthand Rust does not have?

Blocks the qualifier-position half of decision 3. It is a source-language
semantics question - it decides what `A::B` means when `A` names both a module
and a type - so it is not taken here.

#### It is not a precedence rule, it is a feature

8.39 records the intended rule as "module wins in qualifier position", which
reads like an ordering tweak. It is not. Consider the shape, which 8.55 measured
at **76 of 319 modules**:

    namespace Compiler::CompileMode;        // module  Compiler::CompileMode
    type enum CompileMode { Check; ... }    // type    Compiler::CompileMode::CompileMode

Rust's equivalent is `mod compile_mode { enum CompileMode { Check } }`, and Rust
makes you write **`compile_mode::CompileMode::Check`**. There is no shortcut:
the module and the type are separate path segments and both must be spelled.

Cryo today lets you write **`CompileMode::Check`** - the qualifier resolves to
the TYPE, and the module segment is elided. That elision is the convenience
"module wins in qualifier position" would remove, because module-wins resolves
`CompileMode` to the module, whose members do not include `Check`.

#### So the question is

**Does Cryo keep a shorthand Rust deliberately does not have?**

* **Keep it.** `A::B` prefers the TYPE when `A` names both and the module has no
  member `B`. Cryo stays more concise than Rust for the file-per-type
  convention, which is 24% of the tree. Cost: the qualifier's meaning depends on
  what the qualified name resolves to, which is a lookup, not a syntactic rule -
  and a rule of that shape is how the old cascade grew.
* **Drop it, matching Rust.** `A::B` always resolves `A` as a module when one
  exists. Uniform, syntactic, no lookup-dependent meaning. Cost: every
  `CompileMode::Check`-style path in the tree needs the module segment spelled,
  and that is a source-breaking change to existing valid programs.

The second is the one 8.39 currently records. Neither is taken until Jake
answers, and the 76 modules are the blast radius either way.

#### What is NOT blocked on it

The rest of decision 3 - modules declared as `Symbol`s and bound in the type
namespace - does not depend on this. Shape A does not collide under either
answer, because the module binding and the type live in different scopes. Only
the qualifier's meaning is contested.

### 8.57 Scope for decision 3: a module has a name, and no identity - MEASURED 2026-09-02

8.55 removed decision 3's stated justification by measuring the backlog it was
to clear at zero. This is the justification that survives measurement, and the
scope that follows from it. Nothing here is built.

#### The premise checks out, against a prediction that it would not

Predicted: modules ARE declared as `Symbol`s, since `SymbolKind::Namespace`
exists and is constructed, so 8.39's "Cryo never declares a module as a `Symbol`
at all" is overstated. **Wrong.** `Symbol::namespace_sym` has exactly ONE caller
- `name_resolution.cryo:385`, the namespace alias of a C-import `extern` block.
An ordinary `namespace A::B;` never produces a symbol.

The exception is worth keeping in view rather than discarding: the C-import
alias is declared, exported, and given a child `ScopeKind::Module` scope. It is
a working precedent in this tree for exactly what decision 3 proposes, on a
population of one construct.

#### What a module is instead

A string. `find_module_scope(module_name: string)` interns the name and consults
`module_scope_index: HashMap<u32, u64[]>` - a MULTIMAP, because "the resolver
may create several same-named Module scopes across pipeline stages". It then
returns the scope with the most symbols.

Five callers, every one passing a string:
`mono/monomorphizer.cryo:584`, `resolver/name_resolution.cryo:1472` and `:1506`,
`sema/sema.cryo:458`, `types/resolver.cryo:1602`.

`:1506` is inside `type_spelling_res`, and `types/resolver.cryo:1602` is
`home_scope_of`, which feeds `canonical_type_name`. So **the stamp itself is
derived through this lookup**, and so is the 2c seam 8.51 measured at 61,290 of
78,310 resolutions.

#### The tie-break is not an edge case

The field's comment says the resolver "may create" several same-named scopes.
Measured over a full `compiler/` build:

| candidate scopes under one module name | lookups |
|---:|---:|
| 1 | **0** |
| 3 | 10 |
| 4 | 32,162 |
| 7 | 9 |
| 8 | 15 |
| 10 | 2,828 |
| 11 | 38 |
| 12 | 411 |
| 13 | 4,323 |

**39,797 lookups and not one has a single candidate.** The minimum is three, so
`may create` is `always creates`.

Control on the counts: `create_scope` allocates a fresh `next_scope_id` before
pushing, so every entry in a bucket is a DISTINCT scope by construction and the
candidate count cannot be inflated by one scope recorded repeatedly.

#### But the tie-break is not CHOOSING anything - a correction to the line above

This entry first read the count above as "a module name is ambiguous 3-13 ways
on every lookup, resolved by a popularity heuristic standing in for identity."
**That was wrong**, and the measurement that corrects it was run against a
prediction that it would find a defect.

Predicted: some losing candidate holds a name the winner lacks, because scopes
built at different pipeline stages plausibly hold different subsets - which
would make the name unreachable through the lookup, a real defect. Falsifier: a
winner that is a superset in 100% of lookups leaves the argument structural
rather than a bug.

| | lookups |
|---|---:|
| winner holds every name its rivals hold | **39,797** |
| winner MISSING a name a rival holds | **0** |

Zero mis-picks. And the control on that zero is the finding:

| | lookups |
|---|---:|
| every rival scope EMPTY | **39,797** |
| at least one populated rival | **0** |
| loser symbols summed over the whole build | **0** |

So there is exactly ONE populated scope per module name, and the other 2-12 are
empty. The superset result is trivially true - a winner is a superset of nothing
- and the tie-break is **skipping placeholders, not selecting among rival
bindings.** Its outcome is deterministic today.

Control on THAT control, because two zeros in a row are worth distrusting: the
candidate distribution in the same build is unchanged at 3-13 and never 1, so
rivals do exist; and all 39,797 winners are themselves populated, so the compare
had a real set on both sides of the question. A sample row -
`Compiler::Diag::Severity`, 3 candidates, winner holding 1 symbol, rivals
holding 0.

#### What the fragility actually is

Not ambiguity. The honest claim is narrower and still worth making: the index
accumulates 2-12 empty duplicate Module scopes per name, and NOTHING enforces
that only one is ever populated. The tie-break is correct by accident of that
invariant rather than by construction, and the invariant is not written down or
checked anywhere. If a second scope under one name were ever populated, the
lookup would silently pick by symbol count and no diagnostic would fire.

That is a latent hazard and a waste, not a live defect. No mis-pick exists on
this corpus.

#### The justification that survives

One argument survives measurement, and it is the string one:

* because there is no module symbol, there is no `Res` a consumer can hold for
  a module. That is why `ResolutionContext.home_module` is a `string`, why
  `find_module_scope` takes a `string`, and why 2c must rebuild
  `home_module + "::" + leaf` to ask its question. It is a root cause for the
  string seam 8.51 measured at 61,290 of 78,310 resolutions, and it is the
  reason that seam cannot be fully retired by stamping alone: closing 8.52's
  synthesizer gap makes consumers read stamps, but the stamps themselves are
  still derived by NAME, through `type_spelling_res` at
  `name_resolution.cryo:1506`.

Two arguments do NOT survive, and both were load-bearing when the decision was
taken:

* **the parked collision backlog is empty** (8.55): Shape B has zero instances
  across 319 modules, and the one case ever cited was a missing import;
* **the module-name ambiguity is nominal**: one populated scope per name in
  39,797 of 39,797 lookups, with the tie-break skipping empty placeholders
  rather than choosing between bindings.

That is now twice in one session that a structural problem cited for this
decision has measured as nominal. The pattern is worth stating for whoever
re-decides: the case for decision 3 rests on the string seam and on what module
identity buys the migration, and it should be argued there. It is not a defect
backlog, and it is not an ambiguity being resolved by luck.

#### Scope of the change, for planning only

1. Declare a `Symbol` for every `namespace A::B;`, in the parent's scope, kind
   `Namespace` - the C-import alias path is the template.
2. Admit `SymbolKind::Namespace` to the TYPE namespace in `Namespace::accepts`,
   which is the one-line core of "modules bind in the type namespace".
3. Give the module symbol its scope, so `find_module_scope`'s multimap and its
   tie-break can be replaced by identity rather than merely indexed better.
4. Re-key the five string callers onto the symbol.
5. Only then the qualifier question of 8.56, which is Jake's and is parked.

Steps 1-4 are behaviour-preserving in principle and each is separately
measurable; step 5 is not, and is the one that changes what an existing valid
program compiles to.

### 8.58 The residue was never offered to the stamper, and the span cannot say why - MEASURED 2026-09-03

8.52 concluded that the annotations still reaching 2c unstamped are minted after
name resolution finishes, and scoped a per-synthesizer fix on that reading. Its
evidence was `STAMP-DECLINE` measuring 0, which establishes only that the
stamper did not REFUSE them - a node that existed and was never walked to gives
the same zero. This set out to separate the two and did not succeed; what it
established instead is narrower and firmer.

#### The instrument, and why it could not answer the question

The annotation's own span was threaded through `resolve_named` to the seam
probe, on the reasoning that written syntax carries a real file and line while a
node synthesized after parsing carries none. Predicted: a material `<nospan>`
population, concentrated in two or three mint sites dominated by async lowering.

| | rows |
|---|---:|
| unstamped at 2c, `compiler/` | 3,180 |
| carrying `<nospan>` | **0** |
| carrying a real file and line | **3,180** |

**That zero does not mean what the instrument was built to make it mean.** The
premise is false: `AST/substituter.cryo` mints `NamedAnnotation` at `:196`,
`:1010` and `:1031` with `span: named.span` - the ORIGINAL's span, borrowed - so
a synthesized node here is indistinguishable from written syntax by span alone.
The three sites also mint `res: ResSlot::Pending` explicitly, which is a
synthesizer declining to answer for its own node.

So "3,180 of 3,180 carry a real span" is consistent with every one of them being
synthesized, and 8.52's reading is not refuted. The prediction is falsified on
the concentration half; on the `<nospan>` half the measurement is void rather
than negative, because the discriminator does not discriminate.

Recorded because the failure is reusable: **a provenance instrument has to be
checked against what the producers actually write, not against what they would
plausibly write.** Every synthesizer here had a reason to borrow a span - it is
what makes a diagnostic point at the user's code - and that reason was visible
in the code before the probe was built.

#### Three independent instruments agree the nodes were never offered

The counter report from the same run, which was in the tree the whole time:

    annotations offered to the stamp          33323
      stamped Def (bare, via home scope)      32204
      stamped GenericParam                      917
      qualified spelling                         37
      UNSTAMPED span names no module              0
      UNSTAMPED module has no scope               0
      UNSTAMPED bare name not in scope            0

Every annotation the stamper was offered received an answer; all three failure
buckets are zero. `STAMP-DECLINE` is 0 over `compiler/` as well as over the two
corpora 8.52 checked, and that probe is proven to fire - six rows on
`namespace_gate`.

So the 3,180 are not in the 33,323. **They were never offered to the stamper at
all** - not refused, not failed, absent. That holds regardless of the span
question, because it depends only on counts of what the stamper saw.

#### Where they are

| site | spelling | rows |
|---|---|---:|
| `stdlib/collections/array.cryo:49` | `GlobalAlloc` | 119 |
| `stdlib/core/hash.cryo:201` | `Fnv128Hasher` | 54 |
| `stdlib/env/_module.cryo:112` | `String` | 24 |
| `stdlib/collections/hashmap.cryo:517,521,551,817,829` | `Entry` | 23 each |
| `stdlib/collections/hashmap.cryo:101,125` | `GlobalAlloc` | 22 each |
| `stdlib/alloc/box.cryo:71,98` | `GlobalAlloc` | 22 each |

118 distinct spellings, none ever declined. Every site sits inside a generic
template, and the per-site counts cluster near a constant - the shape of once
per instantiation rather than once per written occurrence, which is what a
template body cloned per specialization would produce.

#### Ruled out, and how

* **`stamp_annotation` skipping generic annotations.** It has a `Generic` arm
  that stamps the base and every argument.
* **The struct visitor not reaching inline methods.** `visit(StructDeclNode*)`
  walks `node.methods`, and `visit(MethodNode*)` delegates to the function
  visitor, which stamps parameters and the return type.
* **`ASTCloner` dropping the stamp.** Its `clone_type_annotation` delegates to
  `TypeAnnotation::clone_ptr`, and `TypeAnnotation::clone` copies `res`
  verbatim. So an ordinary clone is not the leak.

#### The live lead

`AST/substituter.cryo` mints `NamedAnnotation` at `:196`, `:1010` and `:1031`
with `res: ResSlot::Pending` written out explicitly. Each also sets
`pre_resolved` to a real `TypeRef`, and a valid one short-circuits at
`types/resolver.cryo:364` before `resolve_named` is ever reached - which is why
these mints are mostly invisible at 2c.

The testable claim was that the residue is the subset whose `pre_resolved` came
back INVALID, falling through the short-circuit to a `Pending` slot.

**Measured, and false.** A counter at each of the three mint sites, split by
whether the minted node got a valid `TypeRef`:

| mint site | valid | invalid |
|---|---:|---:|
| `substituter:rewrite-to-pointer` | 6,642 | **0** |
| `substituter:rewrite-to-array` | 315 | **0** |
| `substituter:projection-base` | 28 | **0** |

All 6,985 substituter mints carry a valid `pre_resolved`, so every one
short-circuits at `types/resolver.cryo:364` and none reaches `resolve_named` at
all. Control on the run: 2c unstamped is still 3,180 and `SAME` still 58,120, so
the probe perturbed nothing. **The substituter is not the source.**

#### Four predictions falsified, which is the finding

This section stops here under the three-hypothesis rule rather than trying a
fifth. What has been ruled out, and how:

| ruled out | by |
|---|---|
| `stamp_annotation` skips generic annotations | it has a `Generic` arm stamping base and args |
| the struct visitor misses inline methods | it walks `node.methods`; the method visitor delegates to the function visitor |
| `ASTCloner` drops the stamp | it delegates to `TypeAnnotation::clone_ptr`, which copies `res` |
| the residue is synthesized, identifiable by a missing span | synthesizers borrow the original's span; the discriminator does not discriminate |
| the residue is the substituter's invalid-`pre_resolved` mints | there are none - 6,985 of 6,985 are valid |

What remains established: the 3,180 were never offered to the stamper, and they
are not produced by any path examined above.

#### The next instrument, and why it is not the span

Every remaining candidate mints with `pre_resolved: TypeRef::invalid()` and so
does reach the cascade: `parser/expr_parser.cryo:2756`, `:2879`, `:2897`,
`:3034`, `parser/parser.cryo:896`, `passes/default_expansion.cryo:752`,
`passes/type_resolution.cryo:1889`, `sema/async_lower.cryo:573` and `:589`,
`bindgen/type_map.cryo:90`, `types/resolver.cryo:1039`.

Enumerating them one at a time is what produced four falsifications. The
instrument that answers it in one pass is the node's OWNER - which declaration
the annotation hangs off - recorded where the node is created rather than where
it is read. That distinguishes "this visitor should have reached it" from "no
visitor owns it" without another guess about which site is responsible, and it
is what the two parser sites in particular need, since a parser-produced node
exists well before the stamping pass runs and cannot be explained by minting at
all.

### 8.59 The residue's owner is a POSITION, not a node kind - MEASURED 2026-09-03

8.58 established that the 3,180 annotations reaching 2c unstamped were never
offered to the stamper, ruled out four producers, and stopped. It named the next
instrument: the node's owner, recorded where the node is created rather than
where it is read.

#### The instrument

`ANN-OFFER` records every spelling the stamper is offered, keyed by the file,
line and COLUMN of its own span. The residue's rows carry the same key, so the
two sets subtract: a site in the residue and absent from the offer stream was
never walked to; a site present in both has a second node standing at the same
syntax. Neither can be read off a count of offers, which is why 8.58's
33,323-with-no-failures could sit beside 3,180 unstamped without contradiction.

The column is not a refinement, it is what makes the key a key. Taken by line
alone the answer is wrong in both directions: `const b: Box<T> = Box<T>::new()`
writes the same spelling twice on one line, the declaration annotation is
offered and the scope qualifier's argument is not, and the offered one masks the
other. Line-only keys put 455 rows in the wrong column here.

#### The controls

`ANN-OFFER` totals 33,324, which is exactly the counter's `annotations offered
to the stamp` - the probe sees the whole offered population and no more. Placed
one frame in, at `stamp_named_annotation`, it reads 27,246: the remaining 6,078
offers are the two node spellings that are not annotations at all (a struct
literal's `struct_type`, an impl head's `target_type`), which is worth knowing
because the counter's line calls all 33,324 "annotations".

Across all three runs 2c stayed at 3,180 `INCUMBENT-ONLY` and 58,120 `SAME`, so
the probe perturbed nothing it was measuring.

#### What owns the residue

| owner | offered? | sites | rows |
|---|---|---:|---:|
| scope qualifier type args (`Pair<String, String>::new`) | no | 145 | 1,734 |
| impl head target (`for struct Fnv128Hasher`) | no | 44 | 495 |
| call type args (`Layout::of<Entry<K, V>>()`) | no | 66 | 353 |
| plain declaration annotation | **yes** | 186 | 219 |
| struct literal type args (`Array<T, GlobalAlloc> { }`) | no | 10 | 215 |
| plain declaration annotation | no | 50 | 74 |
| static-match type pattern (`Slice<u8> => { }`) | no | 5 | 33 |
| declaration type args | no | 12 | 25 |
| declaration type args | **yes** | 19 | 24 |
| struct literal type args | **yes** | 8 | 8 |

**2,929 of 3,180 - 92% - were never offered.** The prediction was that the
residue resolves to one or two owner kinds; the falsifier was a spread across
many, meaning the gap is in the walk's entry points. It resolves to one
POSITION: a type argument written in EXPRESSION position. That is 2,302 rows
across three expression forms, and the mechanism is one line long -

`IdentifierNode`, `CallExprNode`, `StructLiteralNode`, `MemberAccessNode` and
`ScopeResolutionNode` each carry a `generic_args: TypeAnnotation*[]` (and
`ScopeResolutionNode` a `scope_generic_args` as well). `NewExprNode` carries one
too, and its visitor is the only one of the six that calls `stamp_annotation` on
it. The other five walk past written type syntax that has a span, a slot, and a
file the pass is standing in.

This is 8.51's defect class, not 8.52's: written syntax the pass could visit and
does not. 8.52 read the residue as minted-after-the-pass on the strength of
`STAMP-DECLINE` being 0, and that reading is now wrong for 92% of it - a zero
decline count says only that the stamper refused nothing, and the offer stream
is what separates "refused" from "never presented".

#### The impl head's 495 rows are a synthesizer, and it has the answer

The impl head is the one family that is written syntax and still not the
visitor's fault. `visit(ImplBlockNode*)` does stamp the target, onto the NODE,
from `node.span`; the residue's rows carry `target_type_span` - the column of
the target lexeme - so they are a different node. It is minted by
`rewrite_default_method_signature`, which rewrites `This` in a trait-default
method signature cloned into the impl and names the target with the narrower
span. Every one of the 44 sites is a trait impl, and none is inherent.

That mint can answer for its own node without a lookup: `ImplBlockNode.res`
carries the stamp `visit(ImplBlockNode*)` already wrote for the same type, so
the rewrite is a copy of a `Res` the pass produced, not a second opinion about
the spelling.

#### The 251 offered-and-still-unstamped rows

A node was offered at that exact file, line and column, and a node at the same
site reaches 2c `Pending`. They are two nodes, and the copy is taken from
something the stamp never reached - or before it ran. 186 sites of it are plain
declaration annotations (`mut s: String = out;`), which is the population the
declaration visitors provably do stamp. Not chased here; it is 8% of the
residue and a different mechanism from the 92%.

#### Fixed, and what the prediction said first

One helper, called from the five visitors that were walking past their own
`generic_args` (and `ScopeResolutionNode`'s `scope_generic_args`); `new`'s
existing loop folds into the same helper so there is one walk over a written
argument list rather than two.

Predicted before the run: 2c unstamped to about 878, `SAME` up by the same
2,302, `DIFFERENT` to stay 0, gates unchanged. A nonzero `DIFFERENT` would have
meant the stamp just written disagrees with 2c - a defect in the fix rather than
a finding about the seam. A new diagnostic on any corpus would have meant an
expression-position type argument names something its module cannot reach, which
is a source-language question to park rather than to work around.

| | before | after |
|---|---:|---:|
| 2c `INCUMBENT-ONLY` | 3,180 | **936** |
| 2c `SAME` | 58,120 | **60,366** |
| 2c `DIFFERENT` | 0 | **0** |
| `STAMP-DECLINE` | 0 | **0** |
| annotations offered | 33,324 | 33,818 |
| coverage at 2c | 94.8% | **98.5%** |

`STAMP-DECLINE` staying at 0 is the answer to the reachability risk: the gate was
run over syntax it had never been run over and refused none of it. Warning count
349 either side, `LOOKUP` 118, `REENTRY` 6, B1 0 and B4 0 on three arms, 178
compile-fail cases and 38 projects.

The offered figure rises by 494 while `SAME` rises by 2,246, which is the
template ratio: an offer is once per written node, a 2c row is once per
instantiation.

#### What is left, and it is three mechanisms rather than one

| owner | offered? | sites | rows |
|---|---|---:|---:|
| impl head target, via the `This` rewrite | no | 44 | 495 |
| plain declaration annotation | **yes** | 186 | 219 |
| plain declaration annotation | no | 50 | 74 |
| scope qualifier type args | **yes** | 6 | 68 |
| static-match type pattern | no | 5 | 33 |
| declaration type args | **yes** / no | 25 | 37 |
| struct literal type args | **yes** / no | 9 | 10 |

The expression-position families are gone: scope qualifier args 1,734 to 68,
call args 353 to 0, struct literal args 215 to 2. What survives at those owners
is the offered-and-still-unstamped shape, not the unwalked one.

The order by size is the impl head's `This` rewrite (495, a synthesizer holding
the answer already), then the 319 offered-and-still-unstamped rows (two nodes at
one site), then the static-match type patterns (33, which `visit(StaticMatchExprNode*)`
documents itself as skipping).


### 8.60 The impl head's `This` rewrite carries the head's own answer - MEASURED AND FIXED 2026-09-03

8.59 left 936 unstamped at 2c and named the largest survivor: 495 rows over 44
sites, every one an impl head, every one a trait impl and none inherent.

#### Why the head looked stamped and was not

`visit(ImplBlockNode*)` does answer for the target - `ImplResStamped` counts it -
but it answers onto the NODE, from `node.span`. The residue's rows carry
`target_type_span`, the column of the target lexeme itself, so they are a
different node standing at narrower syntax. The column is what separates them;
by line the two are one site.

That node is minted by `rewrite_this_type_annotation`, reached only from
`rewrite_default_method_signature`, reached only from the synthesis that clones
a trait's default methods into an impl. It rewrites `This` in the cloned
signature to the impl's target and wrote `res: ResSlot::Pending` outright. With
`pre_resolved: TypeRef::invalid()` beside it, the node reaches the cascade
rather than short-circuiting, which is why it is visible at 2c at all - and why
8.58's substituter mints, which carry a valid `pre_resolved`, were not.

#### The fix is a copy, not a lookup

The impl head asked what its target spells, in the scope the head was written
in, and this node names that same type. So the head's `ResSlot` is threaded to
the mint and written verbatim.

Threading the SLOT rather than a `Res` is what keeps this from being a branch on
absence: a head no resolver could answer for mints an unanswered node, exactly
as before, and no code tests for pendingness to decide. A second walk here would
be a second opinion about one spelling, which is the shape the old cascade grew
from.

#### Stated before the run, and what happened

Predicted: 2c unstamped 936 to about 441, `DIFFERENT` to stay 0, `SAME` up by
about 495, the offered count UNCHANGED - because this is a copy of an answer and
not an offer, so a rise there would mean the mint had been given its own lookup.

| | before | after |
|---|---:|---:|
| 2c `INCUMBENT-ONLY` | 936 | **441** |
| 2c `SAME` | 60,366 | **60,863** |
| 2c `DIFFERENT` | 0 | **0** |
| `STAMP-DECLINE` | 0 | **0** |
| annotations offered | 33,818 | 33,820 |
| coverage at 2c | 98.5% | **99.3%** |

The impl-head family is 495 to **0**. `LOOKUP` 118, `REENTRY` 6, B1 0 and B4 0
on three arms, 178 compile-fail cases and 38 projects, warning count 349 either
side.

The offered count moved by 2 against a prediction of 0, which is an artifact of
measuring the compiler over its own source: the fix adds `home_res: ResSlot` to
two signatures, and `ResSlot` offers go 45 to 47. Nothing about the pass
changed; the input did.

#### What the 441 are

| owner | offered? | sites | rows |
|---|---|---:|---:|
| plain declaration annotation | **yes** | 186 | 219 |
| scope qualifier type args | **yes** | 6 | 68 |
| declaration type args | **yes** | 19 | 24 |
| struct literal type args | **yes** | 8 | 8 |
| static-match arm type pattern | no | 8 | 57 |
| enum variant payload annotation | no | 39 | 39 |
| associated-type binding (`type Output = ...`) | no | 6 | 15 |
| destructure decl annotation | no | 7 | 7 |
| where-clause bound type args | no | 2 | 4 |

Two populations remain, and they are unrelated:

* **319 offered and still unstamped.** A node was offered and answered at that
  exact file, line and column, and a node at the same site reaches 2c `Pending`.
  Two nodes at one syntax, and the second is copied from something the stamp
  never reached or taken before it ran. This is now the largest single thing
  left and it has not been instrumented.
* **122 never offered**, in five small owners rather than one: static-match arm
  type patterns (which `visit(StaticMatchExprNode*)` documents itself as
  skipping), enum variant payload annotations, an impl body's associated-type
  binding, a destructuring declaration's annotation, and a `where` clause's
  bound arguments. Each is a missing walk of the 8.51 kind, none of them large.

### 8.61 The 2c residue closes to one row, and that row is a wildcard import - MEASURED AND FIXED 2026-09-03

8.60 left 441 unstamped at 2c on `compiler/`, split into two populations with
nothing in common: 319 offered AND still unstamped, and 122 never offered at
all. Both are closed here; they had different causes.

#### The 319: an answer written, then thrown away

`DefaultExpansion` rewrites a bare `Named(X)` into `Generic(Named(X), [defaults])`
so `String` means `String<GlobalAlloc>`. It builds a FRESH `NamedAnnotation` for
the base, copying the original's name and span - and wrote `res: ResSlot::Pending`
outright, discarding the answer NameResolution had already written on the node
it was replacing.

That is the offer stream's second shape exactly: a site offered and answered,
with an unstamped node standing at the same file, line and column. It could not
have been told from a never-walked node without the column, because the two
nodes share a line by construction - one is built from the other.

The fix carries `named.res` onto the rebuilt base. Same shape as 8.60: a copy of
an answer this pass did not have to ask for, not a second walk.

#### The 122: five owners, each written syntax the walk did not reach

| owner | rows |
|---|---:|
| static-match arm type pattern (`Slice<u8> => { }`) | 57 |
| enum variant payload (`Def(SymbolStr);`) | 39 |
| associated-type binding (`type Output = Result<Array<u8>, IoError>;`) | 15 |
| destructuring declaration annotation (`const { ptr, alloc }: Box<T, A> = this;`) | 7 |
| `where` clause bound arguments (`where A: Future<Result<T1, E>>`) | 4 |

One helper for a `where` clause's written types, and a stamp call in each of the
five visitors. The trait PATH in a bound is deliberately not stamped: a bound
names a trait, and `TraitRef.resolved_name` is where that identity lives.

#### Stated before the run

2c unstamped 441 to about 0; `DIFFERENT` to stay 0; the offered count to rise by
the newly walked nodes plus exactly one for the `ResSlot` local the expansion fix
adds to the compiler's own source. A nonzero `STAMP-DECLINE` was the falsifier
that mattered - it would mean some of this syntax names a type its own module
cannot reach, which is a source-language question to record and park.

| on `compiler/` | before | after |
|---|---:|---:|
| 2c `INCUMBENT-ONLY` | 441 | **0** |
| 2c `SAME` | 60,863 | **61,309** |
| 2c `DIFFERENT` | 0 | **0** |
| `STAMP-DECLINE` | 0 | **0** |
| annotations offered | 33,820 | 33,934 |
| UNSTAMPED counter buckets, all three | 0 | **0** |

B1 0 and B4 0 on three arms, `LOOKUP` 118, `REENTRY` 6, 178 compile-fail cases
and 38 projects, warning count 349 throughout.

#### The control on that zero, and why one corpus was not enough

The obvious way for this zero to be uninteresting is a probe that stopped
firing. It did not: `SAME` ROSE by 446 in the same run and the offer stream is
33,934 rows, so both halves of the comparison are still being taken.

The less obvious way is the corpus. `compiler/` is ONE project, it has no extern
module population, and 8.51's own re-take already found the residue
proportionally larger outside it. Re-measured over every corpus:

| corpus | 2c `SAME` | unstamped | `DIFFERENT` | declines |
|---|---:|---:|---:|---:|
| `compiler/` | 61,309 | 0 | 0 | 0 |
| 14 `examples/` projects | 65,713 | 0 | 0 | 0 |
| 36 `tests/` projects that compile | 139,601 | **1** | 0 | 8 |
| **all 57 corpora** | **266,623** | **1** | **0** | **8** |

Six of the 57 emit no rows at all - `stdlib` has no entry point as a project,
and five compile-fail projects abort before the seam is reached - so the claim
covers the 51 that compile. **266,623 agreements, no disagreements, and one
unstamped row.** The `compiler/`-only zero was not the tree-wide zero.

#### The one row: the stamper does not follow a wildcard import

    STAMP-DECLINE  Widget  ReexportBasic::GlobCtl  reexport_basic/src/globctl.cryo:10
    RN-SEAM 2c INCUMBENT-ONLY  ReexportBasic::GlobCtl  Widget
                               ReexportBasic::Inner::Widget  <pending>

`globctl.cryo` writes `import ReexportBasic::Inner::*;` and then
`const w: Widget = make_widget(5);`. The project exists to assert that `M::*`
stays legal as a LOCAL import - only the export form is rejected. 2c resolves
the annotation correctly to `ReexportBasic::Inner::Widget`; the stamper is
offered it and DECLINES.

`type_spelling_res` asks `resolve_type_qualified_name_bare_from`, which is
`resolve_path` over the rib chain outward plus the prelude, then import ALIASES.
No tier there sees a name a wildcard import brought in, while 2c's
`home_module + "::" + leaf` construction reaches it.

The build is green on that project - it is one of the 38 - so this is a silent
under-reach and not a diagnostic. The program compiles today BECAUSE the string
seam answers where the stamp will not, which is precisely the dependency
collapsing 2c would remove.

The other 7 declines are the gate working: `namespace_gate` (6, `Crate`) and
`namespace_gate_methods` (1, `Parcel`) are corpora that exist to assert E0240 on
an unreachable name.

#### It predates this session, measured rather than argued

A compiler built at `e2e588bb`, the session's starting commit, in a worktree,
run over the same project:

| `reexport_basic` | at `e2e588bb` | now |
|---|---:|---:|
| the `Widget` decline | **1, identical row** | 1 |
| 2c `INCUMBENT-ONLY` | 520 | **1** |
| 2c `SAME` | 3,633 | 4,152 |

So the wildcard decline is inherited, and this corpus is an independent
before/after on the session's stamping work that does not come from `compiler/`.

The pin cannot serve as this control: `bin/cryo` predates the `RN-SEAM` and
`STAMP-DECLINE` probes and emits zero rows, which reads exactly like a clean
measurement.

#### The fixed point, on both arms

The briefing this work started from listed `selfhost-check` as not having run
since 35e07375's content, so every measurement above rested on a compiler nobody
had shown reproduces itself. It does, over all four changes:

| arm | stages | verdict | modules | IR |
|---|---|---|---:|---|
| linux (WSL) | 8/8 | `FIXED POINT OK` | 245 | md5 `5468b56f60fb8a1d74df43913b30195c`, cryo.ll 951,350 bytes |
| windows (native) | 8/8 | `FIXED POINT OK` | 245 | 109,473,678 bytes |

Stage-3 and stage-4 produce byte-identical IR on each. One arm passing is not
the gate: the run has to say `FIXED POINT OK` TWICE, and a single occurrence is
what a half-run looks like.

Re-confirmed in passing: the run leaves `compiler/build/cryo` an ELF and DELETES
`cryo.exe`, so a Windows session needs `make cryo` again before anything reads
that path. `make test` does not rebuild it.

#### What is unblocked, what is not, and what is parked

8.51 set the sequencing: close the coverage gap, then collapse 2c - never
collapse it behind a fallback, which decision 1 forbids. Coverage is now 266,623
of 266,624 with zero disagreements, so the collapse is no longer blocked on
coverage or on agreement.

Two things still stand in front of it, and neither is a measurement:

* **The wildcard row.** Making the stamper follow a wildcard import means the
  reachability gate ACCEPTS a spelling it currently refuses. It changes what no
  valid program compiles to - the program already compiles - and it aligns the
  stamp with the incumbent rather than the reverse. It is still a change to the
  gate, so it is recorded here rather than folded into this fix.
* **What a 2c consumer does with a legitimate `Pending`.** A synthesizer that
  genuinely cannot answer for its node is allowed to leave the slot empty; the
  `Res` slot contract frames that as `require`-and-ICE. Which of the remaining
  `Pending` producers are legitimate is a decision, not a count.

### 8.62 `import M::*` bound nothing at all - MEASURED AND FIXED 2026-09-03

8.61's sweep left exactly one unstamped annotation across the 51 corpora that
compile: `Widget` in `reexport_basic/src/globctl.cryo`, reached through
`import ReexportBasic::Inner::*;`. A population of one is a different problem
from the systemic gap, so this was chased with the cheapest instrument that
could still be wrong, and stopped at each step to ask which of several
explanations the row was consistent with.

#### Three free readings before any probe

* The stamper's tier list. `type_spelling_res` asks
  `resolve_type_qualified_name_bare_from`, which is `resolve_path` over the rib
  chain plus the prelude, then import ALIASES. Nothing there names wildcards -
  but nothing there needs to, because a wildcard import declares its names INTO
  the scope the rib chain walks.
* `Namespace::accepts` returns true for `SymbolKind::Import` in every
  namespace, so an import symbol is never filtered out of a type question.
* `qualified_name_of` builds `source_module::name` for an import, which is
  exactly the incumbent's answer. So a FOUND import would have stamped
  correctly.

Each of those would have been a plausible cause; none of them is. That is what
made the next question "is the name in the scope at all", rather than "which
tier rejected it".

#### The probe, and the four outcomes it separates

The counter already answers the first half: `UNSTAMPED bare name not in scope`
reads 1 on this corpus, so the home-scope walk found nothing. A probe at the
failure arm reported the raw `Scope::find` result in the home scope, ignoring
the namespace filter: **`<absent>`**. Not present and refused - not present.

A second probe on the wildcard branch reported the module it resolved, the scope
it binds into, and how many exports it had to bind - emitted in execution order,
so its position also answers whether it ran too late:

    WILDCARD-BIND  ReexportBasic::Inner     ReexportBasic::Aggregate  scope=6  exports=2
    WILDCARD-BIND  ReexportBasic::Inner::*  ReexportBasic::GlobCtl    scope=9  exports=0

Four candidate causes, one row: the branch ran, before the annotation, into the
right scope - and bound **nothing**, because the module it looked up is
`ReexportBasic::Inner::*`.

#### The cause

The parser writes the wildcard into the module PATH:

    const star_path: string = format("%s::*", pinned);

while also recording `ImportStyle::Wildcard` beside it. `*` is not part of any
module's name, so `find_module_index` misses, the suffix fallback misses,
`get_exports` is asked for a namespace that does not exist, and the loop binds
zero names. The one construction of `::*` in the tree is this line, and nothing
anywhere reads the suffix back - it is written and never consumed.

The failure is silent by construction: a wildcard that binds no names is
indistinguishable from a module that exports none.

#### What that means about the corpus

`reexport_basic` compiles, and its `via_glob()` calls `make_widget` and
`Widget::unwrap` through an import that binds nothing. It passes because the
module-blind leaf index answers what the scope could not - the string seam
carrying a program the scope-based lane had already refused. **`M::*` has never
bound a name**, and the project asserting that it stays legal was passing for
the wrong reason.

#### Stated before the fix, and what happened

Predicted: the `WILDCARD-BIND` row goes `exports=0` to `exports=2`, the corpus's
unstamped row and its decline both go to 0, `DIFFERENT` stays 0. The falsifier
that mattered was a NEW diagnostic anywhere: binding names that were previously
bound by nothing can collide, and `insert_import` marks two modules exporting
one leaf as ambiguous.

| `reexport_basic` | before | after |
|---|---:|---:|
| `WILDCARD-BIND` exports | 0 | **2** |
| 2c `INCUMBENT-ONLY` | 1 | **0** |
| `STAMP-DECLINE` | 1 | **0** |
| 2c `SAME` | 4,152 | 4,153 |

Over every corpus:

| corpus | 2c `SAME` | unstamped | `DIFFERENT` | declines |
|---|---:|---:|---:|---:|
| `compiler/` | 61,309 | 0 | 0 | 0 |
| 14 `examples/` projects | 65,713 | 0 | 0 | 0 |
| 36 `tests/` projects that compile | 139,602 | **0** | 0 | 7 |
| **all 57 corpora** | **266,624** | **0** | **0** | **7** |

No new diagnostic appeared. The 7 remaining declines are the reachability gate
working: `namespace_gate` (6, `Crate`) and `namespace_gate_methods` (1,
`Parcel`) exist to assert E0240 on a name its module cannot reach.

**Zero unstamped annotations remain across every corpus that compiles.**

### 8.63 2c reads the name instead of rebuilding it, and that exposed a lane that ignores `private` - MEASURED AND FIXED 2026-09-03

8.61 and 8.62 closed the coverage gap 8.51 named as the precondition: the stamp
answers every annotation reaching 2c, over every corpus that compiles, and
agrees with the incumbent on all of them. This collapses the seam.

#### What 2c was doing, and what it does now

Step 2c derived the qualified name TWICE - `home_module + "::" + leaf` against
the declaration index and then the arena, and failing that a second walk over
the writing module's imports via `resolve_qualified_scoped` - and only then
looked up a `TypeRef`. The stamp already holds the answer to the first question,
so the derivation is gone. What remains is the second question, which was never
the stamp's: WHERE the named type is stored, asked of the index and then the
arena.

Rebuilding a name the node already carries is a second way to ask one question,
free to disagree with the first. It also cannot express what an import resolves
to without repeating the import walk, which is the walk that made this step
expensive.

`RN-2C-UNSERVED` is a new audit row for a `Res::Def` no store can serve - a
declaration that never registered, which `res.cryo` already frames as its own
outcome rather than a licence to search. It reads 0.

#### What the collapse cost, measured off the corpus it was developed against

`compiler/` is the one corpus whose source this work edits, and its counters
have now moved twice for that reason alone. The controls are the 50 corpora it
does not touch, which must come back row-for-row identical:

| corpus | before | after | delta |
|---|---:|---:|---:|
| `compiler/` | 61,309 | 61,304 | -5 |
| `tests/reexport_private_module` | 2,063 | 2,488 | **+425** |
| every other corpus | - | - | **0** |

The `compiler/` delta is not behaviour. The collapse deletes five declarations
from `types/resolver.cryo` - `home_qn` and `home_scoped` (`SymbolStr`),
`home_di`, `home_arena` and `home_st` (`TypeRef`) - and the seam counts
annotations in the source it is compiling: `SymbolStr` 55 to 53, `TypeRef` 96 to
93. Exactly -2 and -3.

The work removed is real, and it is the point:

| counter, on `compiler/` | before | after |
|---|---:|---:|
| `lookup_qualified_alternatives` calls | 559,622 | **511,340** |
| `lookup_qualified_alternatives` hits | 370,122 | **321,819** |
| single-candidate fast path taken | 364,276 | **315,995** |

About 48,280 import-scoped walks per build that no longer happen. `lane-check`
`LOOKUP` goes 118 to 117 in `types/resolver.cryo`, which the gate itself calls
progress and asks to be re-pinned deliberately.

#### The corpus that moved, and why the agreement figure could not have caught it

`reexport_private_module` went from failing to compile to compiling further, and
`make test` reported it:

    reexport_private_module ... [FAIL]  (output missing "error[E0240]")
    error[E0503]: type `Hidden` is private to module `ReexportPrivateModule::Store`

The program is still REJECTED - by a later visibility check rather than by the
use-site reachability gate. So a second, independent part of the compiler
already knew `Hidden` is private, which is what makes the next paragraph a
defect rather than a design question.

**The agreement measurement could not have predicted this, and the reason is
reusable.** `rn_seam_probe` fires inside `if (home_hit.is_valid())` - only where
2c ANSWERS. Every row where the incumbent REFUSED emitted nothing, and those are
exactly the rows a collapse puts at risk. 266,623 of 266,623 was measured over
the answering population and says nothing about the refusing one. A zero, or a
perfect agreement, is a statement about the population the instrument can see.

#### The defect the collapse uncovered

`forward_declare` declares a function with its own visibility and exports it
only when public. Every TYPE kind passed a hardcoded `true` and exported
unconditionally:

    declare_function(fn_node.name, fn_node.span, fn_node.is_public);   // honours private
    declare_type(struct_node.name, struct_node.span, true);            // never did

`StructDeclNode.is_public` exists and is documented as "a `private` type is only
nameable within its own module"; the parser fills it from the `private`
keyword. The resolver discarded it. So a private type is Public and exported in
the symbol table, survives the `is_public()` filter in the re-export closure,
binds in every importer's scope, and the stamper answers for it - while the
declaration index, reading the real visibility, refuses. Two lanes, two answers
about one name, and only the index was right.

Struct, union, enum and class carry `is_public` and now use it. Trait and type
alias have no such field, so there is nothing there to honour and they are left
alone rather than given an invented rule.

#### Stated before the run

That the private fix restores E0240 rather than changing it: with the type
unbound, the stamp is `Pending`, 2c takes the no-answer branch, and the gate
refuses exactly as it did before the collapse. Predicted every other corpus
unchanged, `make test` back to 38, B1 and B4 0, `lane-check` 117.

    error[E0240]: `Hidden` is not reachable from this module

| corpus | 2c `SAME` | unstamped | `DIFFERENT` | declines |
|---|---:|---:|---:|---:|
| `compiler/` | 61,304 | 0 | 0 | 0 |
| 14 `examples/` projects | 65,713 | 0 | 0 | 0 |
| 36 `tests/` projects that compile | 139,602 | **0** | 0 | 3 |
| **all 57 corpora** | **266,619** | **0** | **0** | **9** |

Every corpus is unchanged but two, and both moved in the same direction for
the same reason: `STAMP-DECLINE` went 7 to 9, adding `Hidden` in
`reexport_private_module` and `VisibilityTypeMask::Vault::Hidden` in
`visibility_type_mask`. The decline prediction was wrong - only the first of
those was foreseen - and the two new rows are the fix working on the two
corpora built to test it. Their `SAME` counts do not move (2,063 and 2,484), so
nothing lost a binding; a private type simply stopped binding where it never
should have. All nine declines are now the gate refusing a name a module cannot
reach.

#### The probe that licensed this is now spent, and the figure above says so

The sweep table in this section reports 266,619 `SAME` and no disagreements at
2c AFTER the collapse. **That is not evidence of agreement.** `home_key` is now
taken from `slot`, and it is `home_key` that the probe receives as the
incumbent, so the comparison is the stamp against itself: the verdict can only
be `SAME`, and an unstamped row cannot reach the probe at all because
`home_key` would be invalid and no lookup would hit. Recorded because the
number was published before the tautology was noticed - the section warns two
paragraphs earlier that a perfect agreement is a statement about the population
an instrument can see, and then quotes a figure whose instrument had stopped
seeing anything.

What licenses the collapse is unchanged: the agreement measured BEFORE it, while
the two derivations were independent (266,623 of 266,623), the 50 corpora that
came back row-for-row identical, and `make test` - which is what actually caught
the private-type regression that the vacuous verdict column did not.

So the 2c call is deleted. The control first: `rn_trace`'s `2c-home-syntax` and
`2c-home-cursor` rows number exactly the seam rows on every corpus checked
(`compiler/` 61,304, `reexport_private_module` 2,063, `examples/09-json-config`
5,749), so the volume-and-site signal - the one that caught the regression by
moving a corpus 425 rows - survives the deletion. No script or gate reads
`RN-SEAM`; the 3b call site keeps it, and there the two sides are still derived
separately.

#### Why the gate survives the collapse

The reachability check remains on the no-answer branch. Decision 1 forbids a
second way to ANSWER; refusing is not an answer, it is the reason there is not
one. The branch is not dead - `namespace_gate_methods` and this corpus both
reach it, which is what makes it testable rather than assumed.

### 8.64 What the cascade is now: two steps and a 101-row tail - MEASURED 2026-09-03

With 2c reading the stamp, `resolve_named`'s answers were re-taken over all 57
corpora. This is the payoff figure for the migration, and it is taken over every
corpus rather than the one this work is developed against.

| step | rows | share | corpora answering |
|---|---:|---:|---:|
| `2c-home-syntax` - the stamp | 266,619 | 60.7% | 51 |
| `1-generic` - an O(1) binding in the context | 169,640 | 38.6% | 52 |
| `1b-assoc` - a projection on a generic param | 2,068 | 0.47% | 51 |
| `3a-di-literal` | 63 | 0.014% | 5 |
| `3b-di-canonical` | 38 | 0.009% | 6 |
| `gate-unreachable` (a refusal, not an answer) | 5 | - | 3 |
| `X-failed` | 742 | - | 42 |

**Everything below 2c answers 101 times in 439,175 resolutions.** The nine-step
cascade §7 describes is two steps and a tail, and the two are a context lookup
and a slot read - neither of them a search over the program.

`2c-home-cursor` is **0 on every corpus**. B1 is now zero by construction rather
than by ratchet: there is no longer a path by which the ambient cursor supplies
the module a bare leaf is resolved in, because 2c does not resolve one.

#### The 742 failures are a signal, not breakage

`X-failed` is the largest number in the tail and the one that looks worst - 42
of them on `compiler/`, which builds green, so something recovered. Every one is
a single-letter name: `T` 26, `C` 16, from three sites.

The site carrying 34 of the 42 fails on purpose. `method_binding` resolves an
explicit type argument in a FRESH context carrying no bindings, so that an
argument naming the enclosing body's abstract parameter cannot select a concrete
instantiation and the whole call is deferred. The failed resolution IS the
detection. `sema.cryo:3776` and `type_resolution.cryo:63` account for the other
8.

So the tail below 2c is 101 answers and 742 deliberate refusals, and no part of
it is a fallback recovering from a lookup that should have worked.

#### A zero that is explained but not yet controlled

`2-primitive` also answers 0 everywhere. The mechanism is known: 8.58 measured
the substituter's 6,985 mints all carrying a valid `pre_resolved`, which
short-circuits at `types/resolver.cryo` before `resolve_named` is entered, so
the post-substitution `Named("i32")` the step exists for never arrives.

That explains why these corpora read zero. It does not establish that no program
reaches the step, and the failure mode is not graceful: a substituted primitive
is minted with a `Pending` slot, so with the step gone it would fall past 2c
into the 101-row tail and fail to resolve. Deleting on this zero needs a counter
at the short-circuit showing it catches the WHOLE primitive population first -
the control `resolve_struct_literal` did not have when a measured zero was
deleted on and the suite went red.

Recorded as a candidate, not taken.

### 8.65 The extern and intrinsic callees CAN be stamped, and doing it needs a language answer first - MEASURED, PARKED 2026-09-03

8.47 parked the guard at `find_function_template_for_call`, which branches on
`is_pending` for 116 callees across the corpora, and framed the question: an
`extern "C"` function IS declared, so are these legitimately unstampable or
merely unstamped? The instruction here was to find out and stamp them if they
can be. They can be. Doing it that way breaks the program.

#### The mechanism, measured

The guard's population on `compiler/` is 19 rows, matching 8.47's figure, and
every one is an `intrinsic function`: `strlen` once and the `atomic_*` family
eighteen times. `strlen` is an intrinsic at `core/intrinsics.cryo`, distinct
from libc's `extern "C"` twin - so the population is intrinsics, not externs.

The chain is short and each link was read rather than assumed:

* `NodeKind::IntrinsicDeclaration` calls `declare(sym)` and **never**
  `export_symbol`. Function, struct, union, enum and class arms all export.
* So `std::core::intrinsics` exports **3** names while declaring 142. The
  wildcard-bind probe reported that count directly, on every module importing
  it.
* An importer therefore binds none of them, `Resolver::lookup` fails in
  `visit(IdentifierNode*)`, and `bare_name_res` returns `Pending` for an
  invalid symbol.
* The callee reaches monomorphisation with an empty slot, which is what the
  guard is reading.

The inherited hypothesis was half right. "`IntrinsicDeclaration` declares but
never exports" holds. "`ExternBlock` exports its alias namespace" describes only
the C-import form; a plain `extern "C"` block declares its functions in the
current scope with their own visibility and exports them when public, so extern
callees were never the problem.

#### They can be stamped

Adding `export_symbol` to the intrinsic arm takes the guard's population from
**19 to 0** on `compiler/`. The nodes are not unstampable; nothing about an
intrinsic prevents a `Res`.

#### And that is exactly what must not be done yet

The same build fails:

    error[E0202]: cannot find function `panic` in this scope
     --> stdlib/future/poll.cryo:44:37

`panic` is declared twice - `intrinsic function panic(...) -> never;` in
`core/intrinsics.cryo` and `function panic(...) -> never { ... }` in
`core/_module.cryo`. `poll.cryo` imports nothing and reaches it through the
prelude. Unexported, only the real function is visible and the name resolves.
Exported, both are, the leaf is ambiguous, and it resolves to neither.

This is not one name. **87 of the 142 intrinsic functions have a same-leaf
declaration elsewhere in the stdlib** - `abort`, `exit`, `close`, `malloc`,
`strlen`, `panic` among them. Exporting ambiguates all 87.

The tree already knows. `ffi/libc.cryo` carries an import of
`core::intrinsics` added for no other purpose than to force a topo-sort:

    so the compiler intrinsics (`malloc`/`strlen`/`fopen`/...) claim their bare
    leaf names before libc's same-named `extern "C"` twins are seen

That is REGISTRATION ORDER deciding which of two declarations a bare leaf names,
held in place by an edge in the module graph. It is the same shape §7 describes
and this migration exists to remove, and it is load-bearing today.

#### The question, and why it is not this session's

Stamping these requires deciding what a bare `panic`, `malloc` or `strlen`
names when an intrinsic declaration and a real function or `extern "C"` twin
both declare it. That is a source-language rule, not a resolution defect: it
changes what an existing valid program compiles to, for 87 leaves. No stamping
strategy avoids it, because the ambiguity is in the program's declarations
rather than in how they are looked up - and answering it inside the resolver by
preferring one kind would be a special-case by declaration kind, which is the
shape of the cascade being dismantled.

The guard at `find_function_template_for_call` therefore stays for now, and it
stays as a branch on absence that the `Res` contract forbids - recorded as owed,
not as a boundary. The experiment is reverted; the tree is unchanged by this
section.

### 8.66 Scope and sequence: the old model goes entirely 2026-09-03

Three questions parked across §8.47, §8.56 and §8.57 are answered, and the scope
of the migration is settled with them. Recorded here because the sequence that
follows is not derivable from any measurement in this document, and because the
last item on it retires the instruments every earlier item was made safe by.

#### The scope

Everything from the old model goes. Not "measured inert and ratcheted" - the
defensible stopping point after the 2c collapse is not the target. What remains
at the end is the new implementation, with the cascade, the string seams and the
counters that policed them all removed.

#### The qualifier shorthand is dropped

`A::B` no longer prefers the type when `A` names both a module and a type: the
path is written out, as in Rust. §8.56 laid out the cost and it is accepted -
this is source-breaking across the 76 of 319 modules using the file-per-type
convention. The change is mechanical but lands as one large diff, so it is its
own change with its own verification rather than folded into anything else.

The argument that decided it is the one §8.56 already stated: keep the shorthand
and a qualifier's meaning depends on a lookup rather than on syntax, which is
the shape the old cascade grew from.

#### Modules bind in the type namespace

Decision 3 stands, on structure rather than on a backlog. The two justifications
it was originally given have not survived measurement - the collision backlog is
empty (Shape B, 0 of 319) and the ambiguity is nominal (39,797 of 39,797) - and
the ledger should not lean on either.

What survives is the reason to do it: a module has a NAME and no identity, so
there is no `Res` a consumer can hold for one. That is why 2c rebuilt
`home_module + "::" + leaf` out of strings, and why every consumer asking what a
module-qualified path names has had to re-derive it. 8.63 removed that
construction from the seam carrying 78% of type resolutions; it did not give
modules an identity, so the remaining module-qualified paths still have nothing
to read.

§8.57's five-step plan is now live rather than hypothetical. It is reasoning and
not a tested plan, so step 1 gets its own prediction, falsifier and verification
before step 2 is written.

#### The intrinsic callees

Investigated in 8.65: stampable, and blocked on a language rule about what a
bare leaf names when an intrinsic and a real declaration both claim it. Parked
with the evidence, not worked around.

#### The order

1. ~~Collapse 2c~~ - done, 8.63.
2. Modules as bindings in the type namespace, step 1 first and verified alone.
3. Drop the qualifier shorthand, as its own change.
4. Whatever those strand.
5. **Retire `resolve_counter.cryo`, and it is last for a reason.**

#### Why the counter is last, and what it costs

`resolve_counter.cryo` is 1,615 lines, and retiring it retires `lane-check` and
`b1-check` with it. Those two ratchets are what made every deletion in this
project safe: `LOOKUP` and `REENTRY` catch a per-kind lookup or a resolver
re-entry being added back, and B1/B4 catch a fuzzy fallback or an instantiation
key returning. Every collapse recorded above was verified by a number one of
them holds.

So the last step removes the evidence that the previous ones held. It cannot be
taken until there is an answer to what preserves the guarantee afterwards - a
test that fails when a cascade step is reintroduced, rather than a counter that
merely reports one. That answer does not exist yet, and inventing it is part of
the step rather than a precondition someone else supplies.

Named here so it arrives as the planned final item rather than as a surprise at
the end.

### 8.67 Decision 3 step 1: a module has an identity - MEASURED 2026-09-03

8.57 scoped decision 3 in five steps and said plainly that nothing in it was
built. Step 1 is built here, alone and verified alone, because the plan is
reasoning rather than a tested sequence.

#### The step as written could not be executed

Step 1 says to declare the symbol "in the parent's scope". There is no parent
scope. `set_module` builds every module scope with `this.global_scope` as its
parent and the module's FULL name as its owner, so module scopes are flat
siblings under global rather than a tree - `Compiler::Types::TypeRef` is not
inside any scope belonging to `Compiler::Types`.

Resolved toward the inert reading: the symbol is declared in the global scope
under the module's full name. A full dotted name is never asked for as a bare
leaf, so nothing can resolve differently for its being there. Binding a module
under its LEAF - which is what makes a path like `TypeRef::TypeRef::x` resolve -
is what an import must do, and it belongs to steps 2 and 4 rather than here.

The C-import alias remains the precedent 8.57 identified: declared, exported,
given a child Module scope. It is still the only construct in the tree that
gives a module a symbol, and now it is no longer the only one that gives a
module an identity.

#### What it measures

| | |
|---|---:|
| module symbols declared, `compiler/` build | **245** |
| distinct modules among them | **245** |
| modules the build reports compiling | 164 local + 81 std = **245** |

One symbol per module, and first-wins holds: `set_module` runs once per module
per pipeline stage and builds a fresh scope each time, so without the guard this
would have declared a symbol per stage - which is the same 3-to-13 duplication
8.57 measured in the scope index.

#### The control, and a prediction that was wrong for a now-familiar reason

Predicted behaviour-preserving and identical. On `compiler/` 2c answered 61,304
before and **61,308** after, +4.

Not behaviour. `declare_module_symbol` adds exactly four written annotations to
`resolver.cryo` - `SymbolStr`, `i64`, `SymbolID`, `Symbol` - and `compiler/` is
the corpus that compiles its own source. This is the THIRD time this artifact
has been explained after the fact in this document (8.60's +2 offers, 8.63's -5
rows). It is predictable and should be predicted: **a change to compiler source
moves any count taken over `compiler/` by the number of declarations it adds or
removes.**

The real control is a corpus this work does not touch, and it is exact:

| corpus | before | after | declines |
|---|---:|---:|---:|
| `examples/09-json-config` | 5,749 | **5,749** | 0 -> 0 |
| `tests/reexport_private_module` | 2,063 | **2,063** | 1 -> 1 |

`LOOKUP` 117, `REENTRY` 6, B1 0 and B4 0 on three arms, 178 compile-fail cases
and 38 projects.

#### What step 1 does NOT yet buy, and the ordering that follows

The symbol exists; nothing reads it. `home_module` is still a string,
`find_module_scope` still takes a string and still picks by symbol count among
2-12 empty placeholders, and no `Res` names a module. Those are steps 2 to 4.

The ordering matters more than 8.66 recorded. That entry put the qualifier
change second and decision 3 first on general grounds; the constraint is harder
than that. Measured over the whole tree:

| spelling | occurrences |
|---|---:|
| `Compiler::Types::TypeRef::...` - fully qualified | **0** |
| `Types::TypeRef::...` - bare module leaf | **0** |
| `TypeRef::...` - the shorthand | 3,018 across 151 files |

**There is no spelling of "the module, then the type" that resolves today.** A
plain `import` binds a module's exports and never the module's own name, so a
rewritten path would name something that does not exist. The codemod is blocked
behind steps 2 to 4, exactly as 8.57 ordered it and contrary to 8.66.

#### A number the coming steps need

Binding modules under their leaf is viable but not free. Over the 319 modules of
`compiler/src` plus `stdlib`:

| | |
|---|---:|
| distinct leaf names | 297 |
| leaves used by more than one module | **17** |
| modules sharing a leaf | 39 |

`error` is five modules, `Resolver` and `State` three each. 88% of modules have
a unique leaf; the rest collide only where one file imports two of them, which
`insert_import` already records as ambiguity rather than silently picking.

### 8.68 Decision 3 step 2: modules admitted to the type namespace, and it is inert - MEASURED 2026-09-03

One line: `Namespace::accepts` now answers true for `SymbolKind::Namespace` when
asked in the TYPE namespace. 8.57 calls this "the one-line core of modules bind
in the type namespace" and predicts steps 1 to 4 behaviour-preserving.

#### Predicted NOT inert, and that was wrong

The concern was concrete rather than vague. `resolve_path` resolves
`segments[0]` as the head, and a written qualified annotation arrives as ONE
`SymbolStr` containing `::` rather than as segments. Step 1 puts module symbols
in the global scope under their FULL dotted name, and `resolve_path` walks the
rib chain outward to global. So an annotation spelled `Compiler::Types::TypeRef`
looked able to match the MODULE of that name and answer for it instead of the
type inside - which would be 8.56's qualifier question arriving inside a step
labelled safe.

| corpus | before | after | declines | new diagnostics |
|---|---:|---:|---:|---|
| `examples/09-json-config` | 5,749 | **5,749** | 0 | none |
| `tests/reexport_private_module` | 2,063 | **2,063** | 1 | E0240, its own assertion |
| `compiler/` | 61,308 | **61,308** | 0 | none |

Inert on every corpus, `compiler/` included - and note that `compiler/` does not
move here either, because this change adds no declaration to the source being
measured, which is the other half of 8.67's standing artifact rule.

#### Why it is inert, which is the part worth keeping

A qualified spelling never reaches this tier. `type_spelling_res` separates
qualified from bare and only a BARE name is passed to
`resolve_type_qualified_name_bare_from`, which is the only caller that asks
`resolve_path` in the type namespace with an annotation's name. A bare leaf
cannot equal a full dotted module name, so no module symbol is reachable from
there however the namespace filter is set.

That is also why the step buys nothing on its own: admitting the kind matters
only once modules are bound under their LEAF, which is what an import must do.
The predicate is now correct in advance of the binding that will exercise it.

`LOOKUP` 117, `REENTRY` 6, B1 0 and B4 0 on three arms, 178 compile-fail cases
and 38 projects.

### 8.69 Decision 3 step 3: the duplicate module scopes are a dead save path - MEASURED, NOT TAKEN 2026-09-03

Step 3 is "give the module symbol its scope, so `find_module_scope`'s multimap
and its tie-break can be replaced by identity". Two implementations were
predicted and both measured wrong. What they bought is the shape of the real
one, so the step stops here rather than reaching for a third.

#### First: reuse the module's existing scope. Measured dead.

The obvious reading of step 3 is that a module should have ONE scope, so
`set_module` should reuse the one it already made instead of building another.
8.57 measured 3-13 candidates per name with exactly one populated and the rest
empty, which makes reuse look safe.

It is not, because the populated one is not the first:

| winner's position in its bucket | lookups |
|---|---:|
| ordinal 3 | **40,117** |
| ordinal 2 | 10 |
| ordinal 1 | **0** |

Binding a module to the scope it created first would bind every module to an
empty placeholder. The tie-break is not merely skipping placeholders in some
order - the real scope is reliably the THIRD one built.

And the duplication is larger than 8.57's 3-13 suggested, because that figure is
the bucket size at the moment of a lookup rather than the total:

| Module scopes created per module | modules |
|---:|---:|
| 14 | **244** |
| 13 | 1 |
| 1 | 1 |

About 3,400 Module scopes for 245 modules.

#### Second: stop the leak at its source. Also measured dead, and that is the finding.

`CompilationContext::switch_to_module` calls `set_module`, which CREATES a
scope, under a comment saying "scope restore handled by orchestrator". The
orchestrator then restores the saved scope immediately after, at
`instance.cryo`. So the scope switch_to_module just built is abandoned, once per
module per pass - which is exactly where 14 comes from, and why the rivals are
empty.

The fix looked mechanical: ask the module graph for the module's saved scope and
restore it when there is one, create only when there is not.

**No effect. Not one number moved** - 14 scopes per module, the same bucket
sizes, the same winner ordinal, 2c unchanged.

The reason is the useful part. There are TWO scope-saving mechanisms and the one
the context can see is dead:

* `CompilationContext::save_module_scope`, which writes the scope into
  `ModuleGraph.module_scope_buf` - and **nothing calls it**. Zero callers in the
  tree.
* the orchestrator's own `scope_buf` in `instance.cryo`, which is the live one
  and which the context has no access to.

So `get_module_scope` answered 0 every time and the new branch always took the
create path. A dead save path had been sitting behind a comment that describes
the live one.

#### What step 3 actually requires

Not a stored pointer from symbol to scope - that would freeze the popularity
heuristic rather than remove it. In order:

1. Make the saved scope reachable from the `CompilationContext`, either by
   calling the dead `save_module_scope` after NameResolution or by giving the
   context the orchestrator's buffer. Until then no caller inside the context
   can know a module's scope.
2. Then `switch_to_module` restores instead of creating, which should take the
   per-module count from 14 to the 2 built before NameResolution runs.
3. Then find and remove those 2, which is what makes the bucket single-valued.
4. Only then is there one scope per module to hang the step 1 symbol on, and
   `find_module_scope`'s multimap and symbol-count tie-break can go.

Nothing here is committed; the tree is unchanged by this section. Recorded
because the two dead predictions cost a build each and the next attempt should
not repeat them.

### 8.70 A module scope is not a rib: one scope per module, and the save/restore machinery deleted - MEASURED AND FIXED 2026-09-03

8.69 recorded two dead predictions about the duplicate module scopes, and both
were attempts to make save/restore work. The workaround was the defect.

#### The model that was wrong

`rustc_resolve` has no save/restore of module scopes, because nothing is ever
unset. A module is built ONCE during module-tree construction and that object is
the module's identity; the resolver holds a map from `DefId` to it, and entering
a module sets a pointer to something that already exists.

Rust keeps two kinds of scope with different lifetimes and never confuses them.
**Ribs** - function bodies, blocks, generic parameter lists - are transient, and
push/pop is right for them. **Module scopes** are persistent nodes in a graph,
created once, keyed by identity, alive for the whole compilation.

Cryo was treating a module scope like a rib: building one on entry. That is the
whole cause. Entering a module fourteen times produced fourteen scopes, only the
third populated, and a save/restore mechanism existed solely to carry the
populated one across the gap.

#### Why this is decision 3's third justification, and the only measured one

Rust can key its map because a module has a `DefId`. Cryo modules had a name and
no identity, so there was nothing to key on - which is precisely why the code
resorted to building a scope on entry and hunting for the populated one at
lookup. 8.67's module symbols are that identity.

So this is not a step that follows decision 3, it is what decision 3 is FOR. The
two justifications the decision was originally given both measured empty - the
collision backlog (8.55) and the module-name ambiguity (8.57). The string-seam
argument survived on structure. This one is backed by a defect that was measured
before it was fixed: 3,430 scopes for 245 modules, and 40,127 lookups searching
for the populated one.

#### The ordering precondition, checked rather than assumed

rustc can build the whole module tree before resolving anything. 8.38 records
all-modules-first discipline at four stages, so the ordering this needs may
already hold - and it does:

| module-scope creations | graph size at that moment |
|---:|---|
| **3,430 of 3,430** | complete, 245 modules |

A first run showed 2 at `graph=0`; that was the probe's own initialiser, since
`switch_to_module` sets it and the resolver does not exist on the first call.
Closing the gap made it unanimous. An ambiguous zero is not a measurement.

#### Where the fourteen came from

3,430 = 245 x 14, and the creators are three:

* `switch_to_module` calls `set_module`, which CREATED a scope, once per module
  per pass - and the orchestrator restored the saved one immediately after, so
  the scope just built was abandoned.
* `NameDeclarationPass::run` calls `set_module` TWICE in one invocation, once
  from the module graph's name and once from the AST's namespace. That is why
  the populated scope is ordinal 3.
* `NameResolutionPass::run` already did it correctly, with
  `set_module_with_scope` and a comment naming the hazard. The right model was
  present in one place and contradicted everywhere else.

The double call was never resolving a conflict. The graph's `namespace_name` and
the AST's agree on every module measured - 245 of 245 on `compiler/`, 61 of 61
and 58 of 58 on two projects - with no `<none>` on either side. Two scopes were
being built for one name.

#### What replaced it

`module_scope_of` builds a module's scope on first ask and returns it unchanged
forever after, declaring the module's `Symbol` with it so identity and bindings
are created together. `set_module` is a lookup that never constructs.
`find_module_scope` is a single map read.

Deleted with them: the `HashMap<u32, u64[]>` multimap, the symbol-count
tie-break, and the 55-line `audit_module_scope_pick` that existed only to
interrogate that tie-break - vacuous once there is nothing to choose between,
the same category as the 2c seam probe in 8.63.

And both halves of the save/restore machinery, because with one canonical scope
there is nothing to save and nothing to restore:

* the dead one - `CompilationContext::save_module_scope` and
  `ModuleGraph.module_scope_buf`/`_cap`, which 8.69 found had zero callers;
* the live one - the orchestrator's `scope_buf`, its malloc, its two save sites,
  its frees, `load_module_with_scope`, and the parameter threaded through 12
  signatures.

Neither was repaired. Both were artefacts of the wrong model.

| | before | after |
|---|---:|---:|
| Module scopes, `compiler/` build | **3,430** | **245** |
| scopes per module | 14 | **1** |
| candidate scopes at a lookup | 3-13 | **1** |
| `find_module_scope` tie-break | symbol count | none |

#### Controls

Predicted one scope per module and no behaviour change. The corpora this work
does not edit are exact:

| corpus | 2c before | 2c after | module scopes | diagnostic |
|---|---:|---:|---:|---|
| `examples/09-json-config` | 5,749 | **5,749** | 61 | none |
| `tests/reexport_private_module` | 2,063 | **2,063** | 56 | E0240, its assertion |
| `tests/reexport_basic` | 4,153 | **4,153** | 58 | none |

`compiler/` drifts to 61,294 from 61,309, which is 8.67's standing artifact -
this change deletes far more source than it adds, and `compiler/` compiles its
own source.

The falsifier that mattered was `forward_declare` re-entering a populated scope:
it used to get a fresh one each run, so a second run would re-declare and raise
E0205. None appeared, on 178 compile-fail cases and 38 projects - so the
declaration pass runs once per module, which the canonical model now requires
rather than merely tolerates.

`LOOKUP` 117, `REENTRY` 6, B1 0 and B4 0 on three arms.

#### What is still owed

`set_module_with_scope` survives with six callers, and reading them corrects
the claim this section first made - that all six are now equivalent to
`set_module`. Four are: `monomorphizer` and `sema` each ask
`find_module_scope(name)` and then switch into the answer, and the two in
`NameResolutionPass::run` exist to name a module WITHOUT creating its scope,
which is what `set_module` now does by itself.

The other two are not. They restore a `saved_scope` captured from
`get_current_scope_id()` at the top of a scope-switched region, and that scope
may be a RIB rather than a module scope - a function body the monomorphizer or
sema was standing in. Collapsing those to `set_module` would jump to the module
scope and lose the nesting.

So the helper has a second and legitimate use: restoring an arbitrary saved
scope, which is the transient half of the model and correct as a stack. Four
call sites can collapse; the helper stays. Recorded because the first version of
this paragraph said otherwise on inspection of the call count rather than the
calls.

A temporary probe used to measure the graph/AST name agreement shipped in the
previous commit by mistake and is removed here; it was audit-gated, so it
emitted rows only under `CRYO_PATH_AUDIT`.

### 8.71 What decision 3 step 4 actually costs, and why the leftover call sites are not free - MEASURED 2026-09-03

8.57 step 4 is one line - "re-key the five string callers onto the symbol" - and
it reads like a five-site edit. Measured before starting it, it is not, and the
call sites 8.70 described as collapsible are not collapsible either. Both are
recorded here so the next attempt does not pay for the discovery twice.

#### The seam is the field's TYPE, not the five callers

`find_module_scope`'s five callers all pass a `string`, and every one of those
strings comes from `ResolutionContext.home_module`, which is declared `string`.
Re-keying only the callers would move the intern from callee to caller and
change nothing: the identity has to reach them.

| | |
|---|---:|
| `home_module` references | **86**, across 8 files |
| `set_home_module` call sites | **53** |
| files holding a third or more | `types/resolver.cryo` 29, `sema/call_resolver.cryo` 20 |

So step 4 is converting a field that 53 sites write and 86 read, not editing
five lookups. It is the same order of work as the 8.56 codemod and deserves the
same treatment: its own change, its own verification.

#### The guard on the "collapsible" sites is load-bearing

8.70 said four of the six `set_module_with_scope` callers are now equivalent to
`set_module`. Reading them again, none is a free collapse. `monomorphizer` and
`sema` both have this shape:

    const template_scope: u64 = find_module_scope(mod_str);
    if (template_scope > 0) {
        set_module_with_scope(name, template_scope);
        ...
    }

The guard means "switch only if that module HAS a scope". Canonical
`set_module` CREATES one when it is absent, so the collapse would switch into a
freshly built empty scope for a module that was never entered - and in sema's
case also swap the namespace and set `needs_restore`, on nothing.

The guard is asking a question the canonical model still answers: has this
module been entered at all? `find_module_scope` returning 0 is that answer, and
it stays meaningful. Collapsing needs a way to ask it without also creating -
which is a distinction the current API does not offer.

That is twice in two entries that a claim about these call sites was made from
their shape rather than from their guards. The count of callers is not a
description of them.

### 8.72 `import` binds a type's leaf, so the qualifier change is a rename and not a path rewrite - MEASURED 2026-09-03

8.56 framed dropping the qualifier shorthand as source-breaking across 3,018
call sites, and 8.67 measured that no alternative spelling of "the module, then
the type" resolves today. Both are true. Neither answers the question that
actually sizes the work: does `import` bind a TYPE's leaf into the importing
file's scope?

Cryo has `import`, `export` and `module`; there is no `use`. The parallel to
Rust here is about the mechanism, not the syntax.

#### Measured on four scratch projects, with a negative control

| project | shape | result |
|---|---|---|
| A | module `ImpA::Shapes` holds `Color`; `import ImpA::Shapes;` then `Color::Red` | compiles |
| B | file-per-type: module `ImpB::CompileMode` holds `CompileMode`; `CompileMode::Check` | compiles |
| C | **renamed**: module `ImpC::compile_mode` holds `CompileMode`; `CompileMode::Check` | **compiles** |
| D | `import ImpD::compile_mode::{ CompileMode };` then `CompileMode::Check` | compiles |
| E | same as C but main does NOT import the module | **`error[E0240]`** |

C is the load-bearing row and E is why it can be believed. With the module named
`compile_mode` and the type `CompileMode` there is no module/type collision for
a shorthand to shortcut, and the call site still resolves with no module segment
spelled. E removes only the import and the reachability gate refuses, so the
import is what bound the name - not the module-blind leaf index, which is how
`reexport_basic` passed for the wrong reason in 8.62.

**`import M;` binds the leaf of every type `M` exports into the importing file's
scope.** That is what makes `CompileMode::Check` work, and it keeps working
after a rename.

#### What that does to the size of the change

The call sites do not move. Renaming the modules to snake_case removes the
module/type collision at its source, and every `Leaf::Member` path continues to
resolve through the imported type.

| | |
|---|---:|
| `Leaf::` call sites that DO NOT change | **3,018** across 151 files |
| `namespace` declarations to rename | **75** |
| `import`/`export` lines naming one of them | **282** across 114 files |

So the job is about 357 edited lines plus 75 file-level renames, not 3,018 path
rewrites. The largest single import is `Utils::Logger` at 46 occurrences.

#### What still has to be decided about it

The rename removes the collision; it does not by itself remove the SHORTHAND.
While a module and a type can share a name, `A::B` still has to decide what `A`
means, and decision 3 gives modules a binding in the type namespace that will
compete. Renaming every file-per-type module means no such pair remains in this
tree, but the language rule is still owed - a user can write the colliding shape
tomorrow.

So the two are separable and should be sequenced that way: the rename is a
mechanical change with a compile-time falsifier, and the rule is a language
decision that outlives it.

### 8.73 Decision 3 step 4: `home_module` is an identity, and the seam was un-interning - MEASURED AND FIXED 2026-09-03

8.71 sized step 4 as converting a field that 53 sites write and 86 read, and
said re-keying `find_module_scope`'s five callers alone "would move the intern
from callee to caller and change nothing". The first half is right. The second
is backwards, and reading the five calls rather than counting them is what shows
it.

#### Two corrections to 8.71, both from reading the calls

`home_module` is **86 references across 15 files**, not 8 - ten holding code and
five holding only a doc-comment mention.

And the five `find_module_scope` callers do not intern. Four of them already
hold the identity and UN-INTERN it so the callee can re-intern it:

| caller | passes |
|---|---|
| `monomorphizer.cryo` | `intern_table.resolve(entry.module_name)` |
| `name_resolution.cryo` (two sites) | `intern_table.resolve(q_ns)`, `...(use_ns)` |
| `sema.cryo` | `intern.resolve(template_mod)` |
| `types/resolver.cryo` | `ctx.home_module` - the only real string |

`find_module_scope` then opened with `intern_table.lookup(module_name)` to undo
it. Re-keying removes work at every caller rather than relocating it. That is
the third time this document has been wrong about a set of call sites from their
count, and 8.71's own closing line names the failure it then repeated.

#### Why `SymbolStr` and not the step 1 symbol

The identity is the interned module name, not the `SymbolID` of the module
`Symbol` 8.67 declares. `module_scopes` is already keyed on `SymbolStr.id`,
`Resolver.current_module` is already a `SymbolStr`, `declare_module_symbol`
takes one, and `Res::Def` carries one. It is the identity currency this compiler
already uses, so the field now speaks it. A `SymbolID` would have needed a name
lookup to serve the one consumer that must produce a qualified name - the E0240
gate's `decl_index.resolve_qualified_scoped`.

#### The unset test is exact, and a doc comment said otherwise

The field's "no home" test was `home_module.length() == 0`, in two places.
`InternTable::new` seeds `""` at index 0 and `SymbolStr::empty()` is id 0, so
`intern("")` returns the empty sentinel and `.length() == 0` is exactly
`!is_valid()`. Checked before relying on it, because the substitution is the
whole conversion.

`module_ns_sym_of_file` carried a doc comment claiming the invalid symbol is
"deliberately distinguishable from the module whose namespace is empty". It is
not: both are id 0. Nothing declares an empty namespace, so the two cases do not
both arise - but the comment asserted a property the type does not have, and it
is corrected here rather than left standing. The part of it that was true and
load-bearing is kept: test `is_valid()` and fall back explicitly, because
reading a missing answer as a namespace that compares unequal to every real one
is the false-positive direction that reverted the visibility gate.

#### What the conversion removed

Seven `resolve`/`intern`/`lookup` round trips, each one a place where an
identity was flattened to characters so the next layer could re-derive it:

* four `resolve` at the `find_module_scope` callers;
* the `lookup` inside `find_module_scope`;
* the `intern(ctx.home_module)` at the E0240 gate;
* a seventh at `call_resolver.cryo`, `resolve(tmpl.module_name)` feeding
  `set_home_module`, which the type change surfaced as a compile error.

`CompilationContext::module_ns_of_file` - the string form - is DELETED. Once
`home_module` stopped being a string it had zero callers in the tree: it existed
only to build one.

Three producers convert whole, because every use of each fed `home_module` and
nothing else: `CallResolver::syntax_module_of`, `MethodBinding::owner_module_of`,
and `home_ns_of` in the three mono files. Two new primitives follow the pattern
already here - the `SymbolStr` form is the implementation, the string form a
display wrapper: `ModuleGraph::home_ns_sym_of_file`, which keeps the
empty-answer policy written once, and `TypeArena::module_ns_sym_of` /
`type_module_sym`. `home_module_len` becomes `home_module_sym`; a length helper
is a lie once the field is not a string.

#### The compile errors are the falsifier, and they found the next seam

A missed producer cannot resolve differently - it fails to compile. Six did, in
two rounds, and five of them were one shape: a single local in
`call_specializer` feeding BOTH `ResolutionContext.current_module`, still a
`string`, and `home_module`. Converting the local for one slot broke the other.

So `current_module: string` is the same seam one field up, with 57 construction
sites. It is named here and not taken: folding it in would have invalidated the
falsifier this change was verified against. The five sites resolve at the
`current_module` slot, which leaves the net round-trip count at those sites
unchanged - the string previously came from `home_ns_of`, which resolved.

#### Controls

Predicted behaviour-preserving. The corpora this work does not edit are exact,
and were re-taken from a compiler forced to rebuild from an emptied
`compiler/build` rather than a hoist, because `make cryo` reports "cryo is up to
date" and skips edits:

| corpus | before | after | diagnostic |
|---|---:|---:|---|
| `examples/09-json-config` | 5,749 | **5,749** | none |
| `tests/reexport_private_module` | 2,063 | **2,063** | E0240, its own assertion |
| `tests/reexport_basic` | 4,153 | **4,153** | none |

`2c-home-cursor` stays 0 on all three. `compiler/` moves 61,293 -> **61,321**,
which is 8.67's standing artifact and not a control, because `compiler/`
compiles its own source. The +28 was checked rather than waved at: the diff adds
a net 63 written-annotation tokens by a deliberately over-inclusive count, and
only a fraction of those reach 2c, so the direction and order of magnitude are
what added source predicts. The corpora above are the control precisely because
this one cannot be.

`LOOKUP` 117, `REENTRY` 6, B1 0 and B4 0 on three arms, 178 compile-fail cases
and 38 projects. The compiler build emits 349 warnings before and after.

### 8.74 The qualifier change is a rename, and a module-qualified path has four meanings - MEASURED AND FIXED 2026-09-03

8.72 sized dropping the shorthand as "about 357 edited lines plus 75 file-level
renames", on the finding that `import M;` binds a type's leaf so `Leaf::Member`
call sites do not move. The finding holds. The sizing does not, and neither do
two claims 8.67 made about what resolves today.

#### What the population actually is

| | 8.72 said | measured |
|---|---:|---:|
| `namespace` declarations colliding with a type they declare | 75 | **76** |
| `import`/`export` lines naming one | 282 across 114 files | **496 across 138 files** |
| module-qualified free-fn/const call sites | not measured | **132 across 13 files** |

The third is the one that changes the shape of the work. "The call sites do not
move" is true of `Leaf::Member` where `Leaf` is a TYPE. Ten of the 76 modules
also declare FREE functions, which are called through the MODULE's leaf -
`ModuleGraph::home_ns_of_file`, `NodeLocator::annotation_span`. Those move.

Two 8.67 claims are also wrong, both generalised from one module:

* "There is no spelling of the module, then the type that resolves today" -
  there are **110** in code. `CLI::Runner::new(...)` is module, then type, then
  method, sixteen times.
* "A plain `import` binds a module's exports and never the module's own name" -
  measured on scratch projects with a negative control: `import Scratch::my_mod;`
  then `my_mod::add(1, 2)` compiles AND RUNS, returning 3; removing only the
  import gives `error[E0233]: cannot find my_mod::add`. A module's own leaf IS
  usable as a qualifier for its free functions. Had it not been, the rename
  would have left those call sites with no valid spelling.

#### The scope taken

All 165 `compiler/src` namespaces, whole path, to match stdlib - which is
already 154 of 154 snake_case, so no user-visible import changes. Only 76 of the
165 carry a collision; renaming just those would have left 89 PascalCase
namespaces beside them and required a second pass over the same files.

Two cannot take their mechanical name because it lexes as a KEYWORD, checked
against the lexer's table rather than by eye:

    Compiler::Types::Type         -> compiler::types::type_base
    Compiler::Resolver::Namespace -> compiler::resolver::namespace_kind

Everything else maps with zero segment collisions, zero full-path collisions and
no clash against any stdlib namespace. 74 of 76 file basenames already equalled
the snake form: the file convention was already snake_case and only the
`namespace` line had drifted.

#### A module-qualified path has four meanings, and two of them move

This is what the mechanical change actually has to distinguish:

| form | example | moves |
|---|---|---|
| module + free function | `NodeLocator::annotation_span` | yes |
| module + type | `ResolveCounter::Site` | yes |
| type + static method | `NodeLocator::walk_node` | **no** - the type keeps its name |
| enum variant or plain identifier | `LogComponent::CLI` | **no** - never a module |

`NodeLocator` produces two of the four in ONE file. The rule that separates them
is structural rather than a guess: a leaf that is not a type name anywhere is
unambiguously a module, so `Leaf::` rewrites blanket; a leaf that IS a type name
somewhere needs the module's declaration list, extracted with brace-depth
tracking so a type body's methods are never collected. Multi-segment paths move
wherever they appear; single-segment names - `Main`, `CLI`, `Compiler`, `Utils` -
are ordinary identifiers too and move only in a path position.

#### A green build was not evidence

The transformer took five corrections. Four announced themselves as compile
errors, which is the falsifier 8.72 promised. The fifth did not, and it is the
one worth recording.

The type-name population was computed over `compiler/src`, `stdlib` and `tools`;
the rewrite was applied to the whole repository. A test file declaring its own
`type struct Sink` was invisible to the question and visible to the rewrite. The
result: **110 files wrongly rewritten** across `examples/`, `legacy/bootstrap`
and `tests/` - and the build went green, all three unedited corpora were EXACT,
`LOOKUP` was 117 and B1/B4 were 0 on three arms.

None of those gates compile `examples/` or `legacy/`, so their agreement was a
statement about a population that excluded every damaged file. Only `make test`
saw it, as `error[E0233]: cannot find sink::new`. This is the same shape as the
agreement figure that could only see rows the lane already answered.

The fix is scope, not a larger type list: only `compiler/src` and `tools` can
name a compiler namespace. Checked rather than assumed - the eight outside files
that appear to `import Utils;` are legacy test projects importing their own
local `namespace Utils;`.

#### Mangled names embed the module path, so every cached object is stale

The LSP link failed with undefined references carrying BOTH spellings inside one
symbol:

    ...$L8Compiler.7Codegen.6Passes.13EmitWorkerCtx...   stale std/thread.o
    ...$L8compiler.7codegen.6passes.13EmitWorkerCtx...   fresh instance.o

A generic instantiation carries the instantiating type's module path in its
mangled name, so a rename invalidates every object built before it and an
incremental build links the two spellings against each other. `compiler/build`
had already been emptied - to defeat `make cryo` reporting "cryo is up to date"
and skipping the edits - or it would have hit the same wall. Any build tree
carried across this commit must be cleaned, `make pin` and `selfhost-check`
included.

#### Controls

177 files, +2,125 / -2,125 - exactly symmetric, which is what a pure rename
looks like: every changed line is a substitution. Nothing outside `compiler/src`
and `tools`. Measured from a compiler forced to rebuild rather than hoisted.

The three corpora cannot drift, because nothing they compile was edited, so any
movement in them would be a defect rather than an artifact:

| corpus | before | after |
|---|---:|---:|
| `examples/09-json-config` | 5,749 | **5,749** |
| `tests/reexport_private_module` | 2,063 + E0240 | **2,063** + E0240 |
| `tests/reexport_basic` | 4,153 | **4,153** |

`LOOKUP` 117, `REENTRY` 6, B1 0 and B4 0 on three arms, 178 compile-fail cases,
38 projects, 349 compiler warnings before and after.

`compiler/` was predicted to MOVE - it is the only corpus that compiles the
edited source, and removing a module/type collision could plausibly reroute a
resolution. It does not move: **61,321 before and after**, `2c-home-cursor` 0.

That is the more useful result. A rename changes the SPELLING of a module name
and nothing else: the count and structure of annotations is untouched, every
type leaf is still bound by the same import, and every module-qualified path
still names the same module. The collision was a hazard in what a reader and a
future resolver could write, not a mechanism anything was resolving THROUGH.
Which is also why mechanical substitution compiled once the four meanings of
`Leaf::` were separated.

`make lsp`: 0 compile errors, 481 warnings, and the link succeeds. Only the
hoist copy fails, because a `cryolsp.exe` was running and holds that path. The
LSP is in no local gate and 12 of its files name renamed modules, so it is
verified here explicitly.

`examples/` swept by hand, **14 of 14** - `make examples` is a no-op on Windows
that exits 0, and this is the sweep that would have caught the 110 files.

### 8.75 The fixed point holds on BOTH hosts after a rename that changed every mangled symbol - MEASURED 2026-09-03

`selfhost-check` last passed on the content of `a114beb7`. Thirteen commits have
landed since - the 2c collapse, the canonical module scope, `home_module` as an
identity, and the rename - and every measurement over them was `[host:windows]`.
Single-host measurement has been this document's standing gap.

The rename is why this could not wait any longer. A mangled name embeds the
module path, so renaming 165 namespaces rewrote the symbol of every generic
instantiation in the compiler. That is a specific, tree-wide reason to distrust
any cached object, not merely accumulated drift - and it was observed, not
assumed: the LSP link failed with `undefined reference` symbols carrying
`8Compiler.7Codegen.6Passes` and `8compiler.7codegen.6passes` in the same build.

#### Stated before the run

Both arms reach `FIXED POINT OK`, printed exactly TWICE - a single OK is one arm,
which on this host is a pass-shaped failure. Two failure modes were named in
advance so they could not be confused after the fact:

* `FIXED POINT BROKEN` naming a first-differing module would mean the rename was
  NOT mechanical - some name binds to a different declaration in the compiler
  one stage built than in the next.
* a link error whose `undefined reference` carries BOTH spellings would mean
  stale-object contamination, which is an environment fault and not a defect in
  the change.

Recorded also in advance, because it would otherwise look like a break: the
emitted IR's mangled symbols now carry lowercase module paths. A fixed point
compares stage 3 against stage 4, not against history, and both stages produce
the same new names.

#### Result: green on both, from an emptied `compiler/build`

| arm | stages | fixed point | modules | IR |
|---|---:|---|---:|---:|
| linux (WSL) | 8/8 | **OK** | 245 | 110,528,568 bytes |
| windows (native) | 8/8 | **OK** | 245 | 109,836,681 bytes |

`FIXED POINT OK` appears exactly twice; exit 0. Linux 4m53.5s, Windows 2m10.2s.
The linux arm's `cryo.ll` is md5 `e5589a3a8fb8bf7a806d0f83b1ab2570`, 951,350
bytes. 245 modules on both, matching 8.67's count.

What that buys is stronger than clearing the backlog. The boot compiler is the
PIN, which predates the rename and knows only the old names; it compiles the
renamed source, the compiler it produces compiles that source again, and stage 3
and stage 4 emit byte-identical IR. A rename that altered every symbol in the
tree therefore survives a full six-stage bootstrap on two operating systems.
That is the strongest available confirmation that 8.74's change was mechanical,
and it is consistent with the quieter evidence there: `compiler/` 2c did not move.

#### A wait that looked like progress

The first launch of this check ran nothing for 48 minutes. `Start-Process` was
given the shell redirection inside its `-ArgumentList`, PowerShell re-quoted
`> ... 2>&1` into a single argument, and `cmd` exited immediately. No log was
created and no stage ran.

The failure is worth recording because of its SHAPE rather than its cause. An
absent log is exactly what a healthy run of this check looks like early on - the
output is buffered, so a long silence is expected - and the two states are
indistinguishable without checking whether the process is alive and whether the
build tree is being repopulated. It is the same failure mode as an audit stream
gated on the wrong environment variable: an absence that reads as progress.

Put the redirection inside the wrapper script, and confirm liveness within the
first minute: a live PID and a log that is GROWING. Silence alone says nothing.

### 8.76 An absence that reads as progress, and the gate that had it - MEASURED AND FIXED 2026-09-03

The safety argument for every deletion in this document is that a gate would
have caught it. One of the gates could not, and the way it failed has now
happened three times in three different subsystems. It is one category and it is
named here.

#### The tree state that passed everything

During 8.74 the rename transformer computed "is this leaf also a type name?"
over `compiler/src`, `stdlib` and `tools`, then applied the answer to the whole
repository. A test file declaring its own `type struct Sink` was invisible to
the question and visible to the rewrite. **110 files were wrongly rewritten** -
every `examples/` project, about ninety `legacy/bootstrap` tests, and five
`tests/` files.

That tree passed:

| gate | reported |
|---|---|
| `make cryo` | 0 errors, 349 warnings |
| `examples/09-json-config` | 5,749 - exact |
| `tests/reexport_private_module` | 2,063 + E0240 - exact |
| `tests/reexport_basic` | 4,153 - exact |
| `lane-check` | `LOOKUP` 117, `REENTRY` 6 |
| `b1-check` | B1 0 / B4 0 on three arms |

Six green results over a tree with 110 damaged files. None of them is wrong;
each is a true statement about a population that excluded every damaged file.
`make test` caught it, as `error[E0233]: cannot find sink::new`, and only
because five of the 110 happened to live under `tests/`.

The `examples/` half was caught by a sweep run BY HAND. `make examples` on this
host printed "run it from WSL" and exited 0.

#### The category: an absence that reads as progress

Three instances, three subsystems, one shape - a thing that did nothing and
reported the same way as a thing that did its job:

* **`make examples` on Windows** - printed a message and exited 0. A message
  attached to a success is not a mitigation, because nobody reads it.
* **an audit stream gated on the wrong environment variable** - `CRYO_PATH_AUDIT`
  where `CRYO_RN_AUDIT` was meant. The stream is empty, which is exactly what a
  clean run looks like, so the zero reads as a finding.
* **a detached `selfhost-check` that never started** (8.75) - the launch was
  mis-quoted and `cmd` exited at once. No log is also what a healthy early run
  of that check looks like, because its output buffers.
* **an incremental `cryo build` counting a corpus that was already built**
  (8.78) - the counter reports 0, which is what "this step answered nothing"
  looks like and also what "nothing was compiled" looks like.

In each case the failing state and the passing state are OBSERVATIONALLY
IDENTICAL to the person reading the result. This is the same error as reading a
measured zero without asking what would have to be true for it to be zero for an
uninteresting reason - except the zero here is the gate's own output.

The rule that follows: **a gate must state the population it swept, and refuse
rather than report success when it cannot sweep one.** An exit code is not a
measurement. "OK" and "OK, over nothing" must not print the same way.

#### What was built

`scripts/examples-gate.py` replaces the POSIX-only shell loop and runs on both
hosts. It ends with `examples-gate: OK -- 14 project(s) built`, naming the
population, and it has three refusals that exit non-zero instead of passing:

| condition | outcome |
|---|---|
| compiler binary missing | FAIL, exit 2 |
| fewer projects discovered than `--min` (default 1) | FAIL, exit 2 |
| any project fails to build | FAIL, exit 1, naming the projects |

All three were exercised rather than asserted. The third was checked by breaking
`examples/01-hello` on purpose - `examples-gate: FAIL -- 1 of 14 project(s) did
not build: examples/01-hello` - because a gate nobody has seen fail is a gate
nobody has tested.

`scripts/gate-unavailable.py` gives the refusal one home. `examples-golden` and
`valgrind-check` also exited 0 on Windows having done nothing; they now exit 1
and say so. Both genuinely cannot run there - the first runs the built examples
and diffs stdout, the second needs valgrind - and "it could not run" is the
second honest outcome of a gate, not the first.

CI runs all three on ubuntu, so nothing about the Linux path changes; the script
was run under WSL against the ELF compiler to confirm it, 14 of 14.

#### `legacy/` is not a gate gap

About ninety of the 110 damaged files were under `legacy/bootstrap/tests`. That
tree is 494 `.cryo` files that NOTHING builds - one comment in
`selfhost-check.py` is its only mention in the build. It is inert rather than
ungated, and damage there is invisible because it cannot matter. Recorded so the
next reader does not build a gate for it.

### 8.77 `current_module` is not the seam `home_module` was, and its one reader is a second answering path - MEASURED, NOT TAKEN 2026-09-03

8.73 converted `ResolutionContext.home_module` from a string to a `SymbolStr`
and found the conversion DELETED work, because four of the five consumers
already held the identity and un-interned it. `current_module` sits one field
above it in the same struct and looks like the same job. Measured, it is not,
and converting it the same way would make the compiler slower to no purpose.

#### One reader, and it is a fallback

`current_module` has 57 construction sites and exactly ONE read. At
`types/resolver.cryo`, `array_size_of` asks which module a bare constant in an
array size means, answers it from the annotation's own file, and then - only if
that fails - interns `ctx.current_module` and uses that instead:

    module_ns = this.module_graph.ns_sym_of_file(a.span.file);
    if (!module_ns.is_valid()) { module_ns = intern(ctx.current_module); }

That is one question with two answering paths, which is the defect class this
migration exists to remove. The first path derives the module from the syntax;
the second re-derives it from a field the caller supplied.

#### 35 of the 57 sites supply the wrong KIND of value

The field is documented as a module namespace and its only consumer treats it as
one. What the constructors actually pass:

| passed as `current_module` | sites |
|---|---:|
| `this.ctx.source_file` | 30 |
| `ctx_ptr.source_file` | 3 |
| `source_file`, `this.origin_file` | 2 |
| a real module name (`module_name`, `resolve(module_sym)`) | 10 |
| empty (`""`, `_empty`, `_pm`, `_mod_dummy`) | 10 |
| other | 2 |

**35 of 57 pass a FILE PATH.** Were the fallback to fire at one of them,
`ConstEval` would fold a bare constant against a "module" named
`src/compiler/....cryo`, which names no module, so it would find nothing and
report nothing. A wrong answer with no diagnostic, invisible because the path
does not run.

#### The fallback is dead over five populations

Instrumented at both branches, so the zero has a denominator:

| population | reached | fallback |
|---|---:|---:|
| `examples/09-json-config` | 8 | **0** |
| `tests/reexport_private_module` | 0 | **0** |
| `tests/reexport_basic` | 6 | **0** |
| `examples/`, all 14 projects | 82 | **0** |
| `compiler/` | 12 | **0** |
| `tests/` via `cryo test` | 30 | **0** |
| total | **138** | **0** |

`tests/` was measured with `cryo test` rather than `cryo build`, because that is
the tool that reaches modules only the test runner compiles, and the stream was
confirmed live rather than silently empty: 88,208 audit rows in that run. The
run also exits 1, and that is the probe's doing rather than a regression -
`namespace_gate_methods` and `visibility_gate` assert that output must NOT
contain certain strings, and a probe printing paths and spans breaks them.
178 compile-fail and 36 of 38 projects passed underneath.

#### Why converting it would be a pessimisation

The intern at the fallback runs **zero** times. Converting the field to
`SymbolStr` requires every constructor to supply one, and 35 of them hold a
`string` file path - so the conversion would add an `intern` at 35 sites,
executed on every context construction, to produce a value that is the wrong
kind and that nothing reads. That is exactly the "moves the intern from callee
to caller" outcome 8.71 predicted for `home_module` and was wrong about. Here it
is right.

#### What is owed before the fallback can be deleted

Deleting the second answering path is the correct end state, and the zero is
explained rather than bare: every one of the 42 reached rows inspected carries a
real, non-empty `span.file`, and module discovery is import-driven, so a file
that was compiled is in the graph by construction. `ns_sym_of_file` therefore
answers, and the fallback is unreachable **for a compiled project**.

That is not the whole population. Two cases sit outside a project build, and
one of them is now measured.

**Single-file mode is covered, and it does not fall back.** `cryo build
solo.cryo` on a lone file holding `const N` and `cells: i32[N]` reaches
`array_size_of` seven times and falls back **zero** times, with the row for the
array size itself - `solo.cryo:6:18` - showing the graph naming the file. So
"compiles without a project" does not mean "compiles without a graph": the
loader registers the file it was handed, and `ns_sym_of_file` answers.

**The LSP buffer is not.** `ModuleLoader::set_lsp_override` substitutes editor
text for a path, and a buffer for a file not yet in the graph is the one shape
that could reach the fallback. Nothing in any local gate builds the LSP, so
this is unmeasured rather than measured-zero.

That leaves the same shape as 8.64's `2-primitive` - a zero with a known
mechanism over a population that is now narrow but still not bounded - and that
one was recorded as a candidate rather than taken. So is this.

Recorded, not taken. There is a second reason not to rush it: deleting the
fallback is not obviously behaviour-identical even where it fires. Interning a
file path yields a VALID `SymbolStr` naming no module, while deleting leaves the
EMPTY symbol, and whether `ConstEval` treats "a namespace nothing declares" and
"no namespace" alike is not established here. The missing controls are therefore
`array_size_of` under an LSP override, and that `ConstEval` distinction.

### 8.78 The two halves of the scope model get two names - MEASURED AND FIXED 2026-09-04

8.70 rebuilt module scopes on the rule that a module scope is not a rib: a rib
is transient and a stack is right for it, a module scope is a persistent node
built once and looked up forever after. The code then kept ONE helper for both,
and two consecutive readings of its call sites were wrong about what they do -
8.70 said four of six were free collapses, 8.71 corrected that to none. Both
claims came from the shape of the calls rather than their guards.

#### 8.71's API gap was closed by 8.70 and nobody noticed

8.71 ended: "Collapsing needs a way to ask [whether a module was entered]
without also creating - which is a distinction the current API does not offer."

It does. `find_module_scope` is a single map read that returns 0 for a module
nobody entered and creates nothing; 8.70 made it that in the same commit. So
the guard the call sites already write IS the question, and the collapse is
available with the guard KEPT rather than removed - which is what 8.71 was
right to refuse.

#### The six callers are two different operations

| site | shape | what it means |
|---|---|---|
| `monomorphizer`, `sema` | `if (find_module_scope(m) > 0) { ... }` | **enter module `m`** |
| `monomorphizer`, `sema` restore | scope from `get_current_scope_id()` | **resume a saved scope** |
| `name_resolution` x2 | scope from `get_current_scope_id()` | **resume a saved scope** |

The first pair looked up a module's scope and handed it straight back, which
says "restore" while meaning "enter". Since the guard already proves the scope
exists, `set_module` finds it rather than building one, and the two are
identical by construction: same map, same id, and both helpers set
`current_module` and clear `import_aliases`.

The other four are the transient half and legitimately need both arguments: the
scope came from wherever the pass was standing, which may be a RIB inside a
module. Jumping to the module's own scope would lose the nesting.

So the helper is now `restore_scope`, and `set_module` is the module
destination. The name was doing the confusing: a call that says "restore" while
meaning "enter" is what both earlier readings tripped on.

#### Two comments asserted the model 8.70 replaced

Both were live and both were false:

* `set_module`'s own doc said it "Creates a fresh Module scope parented to
  global". It has not created one on entry since 8.70; it looks up the module's
  single scope.
* `name_resolution` justified not using `set_module` with "`set_module` here
  would start the walk in a fresh empty scope." Same stale premise. The real
  reason to keep the two-argument form there is different and narrower - it
  preserves the cursor the caller is standing in - and the comment now says
  that, along with the fact that the two coincide only while the walk has not
  entered a rib, which is true as far as anyone has measured and is not
  established.

A comment is a hypothesis. These two survived a change that falsified them
because nothing re-reads a comment when the code beneath it moves.

#### Controls

Predicted behaviour-preserving, on the construction argument above rather than
on the call shape.

| corpus | before | after |
|---|---:|---:|
| `examples/09-json-config` | 5,749 | **5,749** |
| `tests/reexport_private_module` | 2,063 + E0240 | **2,063** + E0240 |
| `tests/reexport_basic` | 4,153 | **4,153** |

`LOOKUP` 117, `REENTRY` 6, B1 0 and B4 0 on three arms, 178 compile-fail cases,
38 projects, examples-gate 14 of 14.

#### A fourth instance of the absence that reads as progress, in the instrument

The first reading of those corpora was 2c = **0** on two of the three, against
baselines of 5,749 and 4,153. Taken at face value that is the falsifier firing
and the change reverted.

It was the measurement. `cryo build` is incremental, and the gate chain that ran
immediately before - `make examples`, then `make test` - had just built both
corpora with the new compiler, so the measuring run compiled nothing and the
step it counts answered nothing. Clearing the two `build/` directories restores
5,749 and 4,153 exactly.

The corpus that did NOT move is what identifies it: `reexport_private_module`
reported 2,063 throughout, because it fails with E0240 and can therefore never
cache a successful build. A control that cannot be affected by the confound was
already in the measurement and said so.

Earlier readings in this migration were not affected - they were taken before a
gate chain had rebuilt the corpora, so the cache was stale against a changed
compiler and every run recompiled. But the trap is live for anyone measuring
after a gate run, and it has the shape 8.76 names: 0 rows and 0 work done are
the same number. **Delete the corpus `build/` directory before counting, or
count something that cannot be skipped.**

### 8.79 The language server is gated locally, and the gate found the hole it was built to avoid - MEASURED AND FIXED 2026-09-04

The LSP links the compiler as a LIBRARY, so it sees every AST, NodeKind and
public-signature change, and no local gate compiled it. A green local suite has
never been evidence that it builds, and it has been broken behind one three
times. It survived the 8.74 rename - which renamed modules twelve of its files
name - only because someone ran `make lsp` by hand.

CI does build it, so the gap was local. But the CI step ran LAST, after the test
suite, the roster, both example gates and valgrind, and a job that died earlier
never reached it: that is how one of the three breakages stayed in for weeks.

#### Prediction and falsifier

Wiring the LSP build into the local gate set is behaviour-preserving for the
compiler; the LSP compiles with 0 errors and about 481 warnings, and links.

The falsifier was not about the compile. `make lsp` also installs over
`bin/cryolsp`, which an editor holds open, and the failure is a bare
`error: linking failed` with no linker diagnostic AFTER a clean compile. If the
gate cannot tell that apart from a real link break it is 8.76's defect pointing
the other way - a gate that cries wolf gets ignored, which is how it stops being
evidence.

#### The falsifier is answered by not writing to a held path

`make lsp` does two jobs - compile the server, and install the result. Only the
first is a gate. `make lsp-check` builds into `tools/CryoLSP/build/gate` and
installs nothing, so a held pin cannot reach it. Whether the pin can be replaced
is a different question from whether the source still compiles, and only the
second one belongs in a gate.

This was measured in the confounded state rather than argued: a `cryolsp.exe`
was running throughout, and the gate passed.

| | |
|---|---:|
| modules compiled | 266 (24 local, 81 std, 161 dep) |
| errors | 0 |
| warnings | 481 |
| linked | `build/gate/cryolsp.exe` |
| cold wall time | 93 s |

The prediction holds exactly.

#### A fifth instance of the absence that reads as progress, found in the gate

The naive gate is `cryo build` plus an exit code. Run twice, the second run
prints

    cryolsp is up to date (release)
    Compiled -> build/gate/cryolsp.exe

and exits 0 in ten seconds having compiled nothing. `Compiled ->` is printed
either way, so the line that looks like the result is not one. A gate written on
that exit code sweeps nothing and reports success - `make examples`' defect
reproduced in a new location, a day after it was removed from the old one.

So the gate starts cold and refuses anything it did not watch happen. Success
requires the compiler to have stated the population it built - the
`N local, M std, K dep module(s)` line - and that population to clear
`--min-modules` (200 against 266 today). "Up to date" is a refusal, not a pass.

The general form: **a tool's success message is not evidence that the tool did
the work.** `Compiled ->` names an output file that exists; it says nothing
about whether anything was compiled to produce it. Only the population line
does, and only because the compiler cannot print it without having counted.

#### Controls

Both run, neither assumed.

| control | expected | observed |
|---|---|---|
| warm build (`ARGS=--keep`) | refuse | `FAIL -- nothing was compiled; an incremental build that skipped the tree has not gated it` |
| a real break (`Logger::init` renamed in `main.cryo`) | fail, naming it | `FAIL -- build exited 1 with 1 error line(s)`, quoting `error[E0233]: cannot find Logger::init_removed_by_control` |

The second is the one that matters: without it the gate is a green test asserting
nothing, which is the shape this tree has been caught by before. The break was
reverted and the file compared byte-for-byte against a copy taken before the
edit.

#### The CI ordering, fixed in the same change

The two LSP steps moved from the end of the Linux job to immediately after
`make cryo`. A gate that only runs when everything else has already passed
cannot catch what it is for, and the record shows it failing to.

#### What it does not cover

The install path. `make lsp` still hoists to `bin/cryolsp` and still fails on a
held pin - correctly, because there the install IS the job. The gate covers
compile and link only, which is the surface a compiler change breaks.

It builds with the PIN, like `make lsp` and for the same reason: the compiler
LIBRARY is rebuilt from current source either way, so requiring the stage-2
BINARY would put a full self-host in front of the gate for no added coverage.

`LOOKUP` 117, `REENTRY` 6, B1 0 and B4 0 on three arms, roster 2,106, examples
14 of 14 - unchanged, as they must be, since no compiler or stdlib source moved.

### 8.80 What replaces the counter: two guarantees, two instruments, and one permanent narrowing - DESIGNED 2026-09-04

8.66 put "retire `resolve_counter.cryo`" last and said inventing its replacement
is part of that step rather than a precondition someone supplies. This is that
design, written now while the collapses are recent, because at the end the
reasoning would have to be reconstructed from the instrument being deleted.

Nothing here is built. The step is not started.

#### First correction: `lane-check` does not die with the counter

8.66 says retiring `resolve_counter.cryo` retires `lane-check` and `b1-check`
with it. Measured, that is half wrong.

`scripts/lane-gate.py` counts CALL SITES IN THE SOURCE under `compiler/src` -
the five per-kind lookups outside the file defining them, and `get_resolver()`
outside the driver. It reads no counter, needs no compiler, no stdlib and no
link, and `tests/lane-baseline.txt` contains **zero rows from
`resolve_counter.cryo`**. Deleting that file leaves `LOOKUP` and `REENTRY`
untouched.

So `lane-check` survives verbatim, and the guarantee needing replacement is
`b1-check`'s alone. That halves the problem, and it means the ratchet against a
NEW lookup lane appearing is not in question at any point.

#### Second correction: what `b1-check` holds today is two different things

From the committed golden, on a tree where both totals are zero:

| row | value |
|---|---:|
| `B1_TOTAL` / `B4_TOTAL` | 0 / 0 |
| `2c home-module (ambient cursor)` | 0 |
| `5 GLOBAL LEAF INDEX` | 0 |
| `lookup_by_leaf` calls / hits | **2,206** / 0 |
| `by caller: canonical_qualified` / folded | **638** / **316** |
| `M1 qualifier_agrees` calls / agreed | **5,566** / 5,566 |
| `M2 resolve_module_qualified_sym` calls | **2,258** |
| `M4 mono bare-name scan` / hits | **267** / 0 |

Every ANSWER is zero and no CALL is. The machinery is entered thousands of times
per build and returns nothing. The gate therefore holds two guarantees that have
been read as one:

* **an absence of answers** - no fuzzy fallback binds a name, and no lookup is
  keyed on an instantiation;
* **an exact pin on call volume** - how often each lane is entered, per host and
  per target, which is why the golden has host sections at all.

#### The sharper reason the counter is last

8.66 gave it as "the last step removes the evidence the earlier ones held". The
measurement gives a better one.

The call-volume half is **transitional by construction**. It protects the lanes
while they still exist; once `lookup_by_leaf`, M1, M2, M4 and the leaf index are
deleted, "2,206 calls" is not a number anyone can preserve, because there is
nothing left to call. Every row goes structurally zero, and a golden of zeroes
asserts nothing a deleted function could violate.

So the ordering is not a courtesy to the evidence. **The counter cannot be
retired before the lanes it counts, and needs no replacement for its readings
after them, because by then its readings are vacuous.** What has to be replaced
is not the counter's numbers but the other half of its guarantee: the absence of
answers, which outlives the lanes as a property the tree must keep.

#### The absence has two failure modes, and they need different instruments

This is the substantive design point, and collapsing it into one "cascade gate"
would be wrong.

* **B1 regrowth makes an INVALID program COMPILE.** A name that nothing in scope
  binds gets bound anyway. The observable is a rejection that stops happening.
* **B4 regrowth makes a VALID program COMPUTE THE WRONG THING.** A lookup keyed
  on a mangled instantiation name binds the wrong instantiation; the build
  succeeds and the answer is wrong; a sized and an unsized array instantiation of
  the same generic colliding is one such shape. There is no diagnostic to
  assert.

A rejection test cannot see the second and a runtime test cannot see the first.
Two instruments, not one.

#### Both instruments already exist in this tree, and one is already doing this job

No new harness is needed. What is missing is coverage, not machinery.

**For B1 - a rejection that must keep happening.** `tests/negative/` files carry
`![config(negative, <CODE>)]` plus `//~` annotations, and the checker is
**two-way**: every annotation must match a diagnostic on severity, code, LINE and
message substring, AND every diagnostic anchored in the file must be annotated,
so an unexpected one fails. That polarity is exactly what 8.66 asked for. If a
cascade step returns and the name resolves, the expected `error[E0240]` at that
line stops being emitted and the test goes red; if the step's return produces a
different error instead, the unannotated half catches it. A fixture cannot
quietly pass in either of the two usual ways.

**For B4 - a wrong answer that must stay impossible.** `tests/projects/` with
`"outcome": "collect"`, in the shape `resolution_leaf_index` already uses: the
sources make the binding **observable at runtime** through a discriminator
(`tag()` returning 1 or 2 for the two declarers), so the test cannot be satisfied
by any value that merely round-trips. A lookup that binds the wrong instantiation
returns the wrong tag.

#### The precedent settles the question a counter cannot answer

`resolution_leaf_index`'s header states the decisive property, and it was written
about cascade step 5:

> On every other corpus in the tree step 5 answers ZERO times - 2c takes every
> unique leaf before it is reached - so a change that deleted the leaf index
> would look inert. It is starved, not dead, and only a plural leaf shows it.

**A counter reports the same 0 for a starved lane and a deleted one.** A fixture
that constructs the shape reaching the lane distinguishes them. For the specific
question "is this step gone", the fixture is not a weaker substitute for the
counter - it is the stronger instrument, and the golden above is the evidence:
five lanes reading zero answers against thousands of calls are all starved, and
no number in that table says which would answer if asked.

That is also the standing rule about zeros, applied to the instrument itself: a
zero needs a control, and for a starved lane the control is a corpus that feeds
it.

#### The migration protocol, already demonstrated once

`resolution_leaf_index`'s header records what was done when a case became an
error: resolution_tripwire's visibility half moved to `visibility_gate`. So each
step has a two-phase fixture life.

1. **While the lane exists** - a `collect` project with `WRONG_`-prefixed tests
   pinning the behaviour the spec calls wrong, plus a runtime discriminator, plus
   `CONTROL_` tests proving the shape is live and the lane is actually reached.
2. **Once the lane is deleted** - the same case moves to a rejection fixture with
   the diagnostic it must now produce, in the same change that made it an error.

Phase 2 is literally "a test that fails when a cascade step returns": reintroduce
the step and the program compiles, and a fixture asserting it must not is red.

#### A measured asymmetry that decides which form to prefer

The two-way check runs ONLY on the file-level path (`check_annotations`,
`commands.cryo:1770`, called once at `:1980`). Project-level `compile_fail` is
`exit != 0` plus an `output_contains` substring - **one-way**. It catches the
case that matters (the step returns, the build succeeds, `fails` is violated),
but it will pass while gating nothing if the fixture develops an unrelated error
that happens to carry the same code.

Two consequences:

* Prefer the **file-level** form wherever the shape fits in one file, because it
  is two-way for free.
* A **project-level** fixture, needed whenever the shape is genuinely cross-module
  - which for name resolution is most of them - must be paired with a positive
  control project built from the same sources with the binding made legitimate,
  so a fixture that starts failing for an unrelated reason is visible. This is
  the `CONTROL_` discipline `resolution_leaf_index` already applies within a
  project, lifted to the pair.

The one piece of harness work worth doing instead: extend `//~` to the
project-level `compile_fail` path, which would make the paired control
unnecessary and every existing project fixture stricter. Recorded as the concrete
engineering item, not scheduled.

#### Host independence is a third reason the end state is stronger

A `collect` fixture with a discriminator can be host-dependent - the leaf index's
winner is decided by directory enumeration order, and the B1 golden carries host
sections for the same reason. A rejection fixture asserts that NOTHING binds, so
there is no winner to vary and no host section to keep. Phase 2 fixtures need no
per-host golden, which removes the failure mode where one host's re-pin hides
another's regression.

#### The permanent narrowing, stated plainly

The counter observed every resolution in a build - 439,175 of them across 57
corpora. A fixture set observes only the shapes someone thought to write.

Retiring the counter therefore gives up, permanently, the ability to notice a
lane answering for a shape nobody anticipated. `lane-check` catches a NEW lane
being added; the fixtures catch a KNOWN lane answering a KNOWN shape. **Neither
catches an existing lane answering a new shape**, and after the retirement
nothing will.

That is a real loss, not one the fixture design closes, and it is a project
decision rather than a method one. It is recorded here so it is taken
deliberately at the point of the step instead of discovered afterwards. The
mitigating fact, and the reason it is probably acceptable: once the lanes are
deleted there is no lane left to answer for an unanticipated shape, and what
remains is `2c` reading a stamp and `1-generic` reading a context binding -
neither of which is a search over the program.

#### Sequencing that follows from all of the above

1. **Enumerate the answering exits from the counter WHILE IT EXISTS.** It is what
   makes them enumerable and attributable per site; that map cannot be recovered
   afterwards. This is the one step that must not be deferred.
2. One fixture per exit, in phase-1 form, each with its `CONTROL_` proving the
   lane is reached rather than starved.
3. Prove each fixture red-on-reintroduction before trusting it. A fixture never
   observed failing is a green test asserting nothing.
4. Delete the lane; migrate its fixture to phase 2 in the same change.
5. The counter last, when its rows are structurally zero.

The fixture bodies are not designed here - only the form, the polarity, the
controls each needs, and the order.

### 8.81 `lookup_by_leaf`'s zero is starvation, and the row that pins it cannot see the lane - MEASURED 2026-09-04

8.80 named the question every remaining deletion candidate turns on: a lane
answering 0 may be dead, or it may be starved by a corpus that never presents
the shape reaching it. `lookup_by_leaf` is the largest live-but-silent lane -
entered 2,206 times across the three pinned targets and answering nothing - so
it is the one that settles whether the question has teeth.

It does. The zero is **starvation**.

#### The burden of proof, before the measurement

"Starved" and "dead" have opposite consequences and unequal costs. Call a lane
dead when it is starved and deleting it breaks a program shape no gate covers -
silently, because the corpora that would catch it are exactly the ones that do
not reach the lane. Call it starved when it is dead and unreachable code is
kept.

So the default answer is starved, and only positive evidence that the lane
cannot be reached moves it. **A zero measured over a corpus that does not
present the shape is not that evidence; it is the definition of starvation.**

#### Prediction and falsifier

Predicted: on `tests/tests/projects/resolution_leaf_index` - the project written
because "on every other corpus step 5 answers ZERO times ... it is starved, not
dead, and only a plural leaf shows it" - `lookup_by_leaf hits` would be non-zero,
of order 2 to 20 rather than thousands. Falsified by a zero there.

#### The measurement

| corpus | tool | calls | hits |
|---|---|---:|---:|
| `examples/09-json-config` (the `[host:windows]` row) | `cryo build` | 2,206 | 0 |
| `tests/.../ffi_c_import` | `cryo build` | 1,578 | 0 |
| `examples/14-threads` | `cryo build` | 2,036 | 0 |
| **all three pinned targets** | `cryo build` | **5,820** | **0** |
| `resolution_leaf_index` | `cryo build` | 1,578 | **0** |
| `resolution_leaf_index` | `cryo test` | 3,546 | **8** |

The prediction holds, including the size.

A first draft of this entry cited 2,206 as the three-target total. It is one
target - the golden pins a separate row per target, and they sum to 5,820.
Corrected here rather than left to be re-cited.

The two 1,578 rows are not a coincidence worth ignoring: `ffi_c_import` and
`resolution_leaf_index` are different projects with the same count, because
under `cryo build` almost every call comes from compiling the STDLIB and
hardly any from the project. That is the corpus-independence of the row said
numerically, and it is why moving between small projects never moved it.

#### The tool is a variable, not a detail

The third and fourth rows are **the same corpus**. `ResolutionLeafIndex::Orphan`
is named only from a test file, module discovery is import-driven, and
`cryo build` does not compile `tests/` - so the build never compiles the module
whose signatures reach the lane, and reports a confident zero over sources it
did not read.

This is stronger than the recorded form of the trap. Measuring on the corpus
that presents the shape is **necessary and not sufficient**: the corpus must
also be compiled by the tool that reaches it. A zero is a statement about the
corpus AND the tool, and either alone can produce it.

#### The eight answers, named rather than counted

A count says how many, never which, so the audit stream was read at the event:

    4  LEAF-HIT     ResolutionLeafIndex::Orphan  Widget
    2  PATH-HIT  LEAF-TYPEUTILS  ...::Orphan::Widget        Widget
    2  PATH-HIT  LEAF-TYPEUTILS  ...::Orphan::6Widget$Ll$G  6Widget$Ll$G

Every one is one module asking one leaf - exactly the shape the project
constructs, and exactly the defect its header documents: a bare plural leaf
resolving with nothing in scope.

The last pair also explains the lane's `B4` flag rather than leaving it as a
label. `6Widget$Ll$G` is a mangled instantiation name, minted after the name
layer has finished, being used as a lookup key - which is what B4 means and
what a `Res` cannot name.

Getting those rows needed two env vars, not one: `leaf_hit` is gated on
`CRYO_LEAF_AUDIT` and the `type_utils` row on `CRYO_PATH_AUDIT`. Enabling only
the first shows four hits where the counter says eight, and the missing half
reads as a stream with nothing to report. The audit vars are not
interchangeable and picking wrong is silent.

#### The deletion unit is the caller, not the function

The counter already splits the hits by caller, and the five callers are not in
one state:

| caller | hits on the feeding corpus |
|---|---:|
| `resolve_named` step 5 | 4 |
| `sema type_utils` | 4 |
| `resolver` generic bound | 0 |
| `symbolic_checker` | 0 |
| `type_resolution` bound | 0 |

Two callers are demonstrated live. The other three answer 0 even here, because
this project constructs a plural leaf and not a generic parameter constrained by
a trait leaf - their shape is a different one, and it has not been built yet.

So "delete `lookup_by_leaf`" was never the available move, and the function's
total was never the number to read. **The count of call sites is not a
description of them**, applied to a lane rather than to a seam.

#### What the pinned row is actually worth

`b1-check` pins `lookup_by_leaf hits` at 0 across three targets that are all
built with `cryo build` and none of which contains a plural leaf. The row is
pinned over a population in which it **cannot** be non-zero.

That is not useless - a fallback regrowing on those corpora would still move it -
but it is not what it has been read as. It has never observed this lane
answering, and a reader taking the 0 as the lane's state would be taking a
starvation for a death. The behaviour IS gated, by `resolution_leaf_index`'s
`WRONG_` tests inside `make test`; it is the counter, not the tree, that is
blind here.

This is the concrete instance behind 8.80's claim that for "is this step gone" a
fixture is the stronger instrument. The fixture answered; the counter's row
could not.

#### Standing consequence for the remaining lanes

Every other zero in that report - `M4 mono bare-name scan hits`, the const-table
leaf pair, `2c-home-cursor`, `2-primitive` at the `pre_resolved` short-circuit -
is now a statement about a corpus, a tool, and a caller, until someone builds
the shape that would feed it. None of them may be deleted on the pinned zero
alone, and the burden of proof is on "dead" in each case.

`lookup_by_leaf` itself is not deletable: it answers, and the answers are the
ones `resolution_leaf_index` exists to pin.

#### Open, and the next shape to build

The three generic-bound callers are undetermined. They are called and have not
been observed answering on any corpus measured here. Determining them needs a
fixture presenting a generic parameter whose constraint names a trait by a leaf
that is in the index but not reachable through scope - the constraint analogue
of `Orphan`. Recorded as the next lane fixture, not attempted.

### 8.82 A gate that runs last cannot catch what it exists for

8.79 moved the two language-server steps ahead of the test suite as a one-off
ordering fix. The argument generalises and is recorded here as a rule, because
the same shape can be reintroduced anywhere in a job.

**A gate ordered after the gates most likely to fail is conditional on their
success, and a conditional gate is not one.** The LSP step sat after the test
suite, the roster, both example gates and valgrind; a job dying at `examples`
never reached it, and a breakage stayed in for weeks behind a red that was about
something else entirely.

The failure is invisible in exactly the way 8.76 names: the step is not reported
as skipped, it is simply absent from a run that already failed, and the run's
red is attributed to the earlier gate. Nobody reads a gate that did not print.

The ordering rule that follows: **a gate covering a surface no other gate covers
runs before gates that merely cover it again.** Coverage uniqueness, not cost
and not tradition, decides the position. The LSP compile is the only thing in
the tree that compiles the LSP, so it goes early; `examples-golden` re-covers
ground `examples` already walked, so it can go late.

Worth re-checking the rest of the job against that rule rather than assuming
this was the only instance.

### 8.83 The gate that certifies the branch counted a skipped arm as a pass - MEASURED AND FIXED 2026-09-04

`selfhost-check` has two arms: the Linux 6-stage byte-identity chain and the
Windows one. Both host paths mapped a SKIPPED arm to exit 0.

    scripts/selfhost-check.py:754   return 0 if win in ("ok", "skip") else 1
    scripts/selfhost-check.py:898   if win == "fail": return 1

`run_windows_selfhost` returns `"skip"` when `bin/cryo.exe` is absent, or when
wine and the fetched Windows toolchain are not present. CI's selfhost job runs
on `ubuntu-latest` and installs neither. So the job ran one arm and exited 0,
and "the fixed point holds on both hosts" was a claim about a run that could
not be told apart from a run that checked one host.

8.75 and the commit that recorded the both-arms verification rest on this gate.
The "exactly twice" criterion - two `FIXED POINT OK` lines - lived in a commit
message and in a memory note, which is to say in a discipline, which is what
§7 says gets violated under deadline.

This is the sixth instance of the absence that reads as progress, and the one
that certifies the other five.

#### Demonstrated, not inferred

The mapping was read from the source and then the branch was executed, in the
environment CI uses, by reproducing exactly what CI has: no wine on PATH.

    ARM_RESULT='skip'
    linux-host   (line 898-903): exit 0
    windows-host (line 754)    : exit 0

A code read would have been enough to justify the change; it is not enough to
justify the claim, because the branch reached in practice is a question about
the environment and not about the source.

#### The fix, and why it is a flag rather than a hard failure

The arms are now counted, named and printed, and a skip is a refusal - unless
the caller passes `--allow-skipped-arm`.

Making a skip fail unconditionally would have repeated the failure 8.79 was
built to avoid: a gate that goes red in an environment that legitimately cannot
run an arm is a gate that gets ignored, and an ignored gate is not evidence.
The declaration is a flag precisely so that it lives at the CALL SITE - visible
in the workflow file or the make invocation a reader inspects - rather than
inside a status mapping nobody reads. CI now says out loud, in the job, that it
verifies the Linux arm only.

A fourth status keeps the two kinds of not-running apart. `DECLINED` is an arm a
flag put out of scope for this invocation because something else is accountable
for it - the Windows-host entry point runs the Linux arm inside WSL with
`--no-windows` and then runs the Windows arm natively, so the child is not
failing to cover the Windows arm, it is not responsible for it. `skip` is an arm
that was attempted and could not run. The first is a division of labour; the
second is missing coverage. Collapsing them is how the hole was readable as
intentional.

The Windows-host path is the one that matters most and it takes no flag: that
host can run both arms, so it is the host whose green licenses the both-arms
claim, and it is now unreachable with the Windows arm skipped.

#### Controls

A seven-case decision table, run against the changed function:

| arms | `--allow-skipped-arm` | expected | got |
|---|---|---:|---:|
| both verified | no | 0 | 0 |
| windows skipped | no | 1 | 1 |
| windows skipped | yes | 0 | 0 |
| windows failed | no | 1 | 1 |
| windows failed | yes | 1 | 1 |
| linux broken | yes | 1 | 1 |
| windows declined | no | 0 | 0 |

The fifth row is the one worth stating: the flag forgives an arm that could not
run, never one that ran and broke.

Then the whole gate, on the host that has both arms - the run that the
both-arms claim actually depends on:

    ==> selfhost-check arms  2 verified, 0 skipped, 0 failed
          linux     ✓ verified  verified in WSL (see above)
          windows   ✓ verified  245 modules byte-identical

245 modules on both arms, matching 8.75. `FIXED POINT OK` appears exactly twice
in the log, which was the hand-checked criterion in a memory note and is now a
property of the gate: the summary cannot say "2 verified" unless two arms ran.

The inner WSL child prints its own summary first, and it is the control for the
DECLINED status - `1 verified, 0 skipped, 0 failed` with the Windows arm marked
"not this run's job". A child that had counted its declined arm as skipped would
have failed the run; one that had counted it as verified would have made the
parent's "2 verified" a double count.

#### A seventh instance, in the tooling that made the change

Counting the one above as the sixth, this is the seventh, and it happened
during the fix. The patch script that rewrote the five return sites reported
success while the single most important one - the wine-missing branch, the one CI actually takes -
did not match its anchor and was left untouched. `str.replace` returns the
string unchanged when the anchor is absent, and the script's only assertion was
that *something* had changed.

The decision-table control caught it: `CI_BRANCH='skip'` came back a bare string
where every other site returned a tuple. Without that control the commit would
have shipped a gate that still passed on a skipped arm, with a ledger entry
saying it did not - which is worse than the defect, because it converts a hole
into a hole with a certificate.

The general rule, and it applies to every patch script in `scripts/`: **assert
per anchor, not per file.** A rewrite that reports success having matched
nothing is the same shape as a gate that exits 0 having swept nothing, and it
reached the instrument for closing exactly that.

#### Open, and now visible

Whether CI should install wine and the Windows toolchain and verify both arms is
a real question with a real cost, and it is not answered here. What changed is
that it is now a visible decision in the workflow file instead of a silent
property of a status mapping. Deciding it belongs to whoever weighs the CI
minutes against the coverage.

### 8.84 The `written_leaf` bound sites: the accessor is the symptom, the stamp arrives too late, and the syntax has no coverage - MEASURED, DECLINED 2026-09-04

Three sites resolve a generic parameter's inline constraints by asking the
arena's leaf index for the written leaf:

    types/resolver.cryo:1231        create_param_type
    passes/type_resolution.cryo:1298 create_generic_param_types
    sema/symbolic_checker.cryo:94    symbolic_param_ref

`written_leaf`'s own docstring says it "is a search key and not an identity"
and that "anything asking WHICH trait this names must call `identity()`
instead", and `TraitRef.resolved_name` is stamped by `stamp_trait_ref`. The
proposed change was to swap the accessor at all three. Measured, that change is
a no-op at the only site that runs, and a regression if it is completed.

#### The stamp is fully qualified, so the accessor cannot be swapped alone

`BOUND-STAMP-ALL` names both halves:

    stamp_trait_ref   Ord     std::core::cmp::Ord
    stamp_trait_ref   Clone   std::core::clone::Clone

`resolved_name` is a qualified name; `written_leaf` is a bare leaf. The leaf
index is keyed BY LEAF. So `lookup_by_leaf(identity())` on a stamped ref would
look a qualified name up in a leaf-keyed map and miss - and a miss here is
silent, because a constraint that does not resolve falls back to a plain
`GenericParamType` by design. Swapping the accessor requires also swapping the
lookup to `lookup_by_name`, and the two must move together or bounds are lost
without a diagnostic.

#### Two of the three sites are unreachable, and the third is unstamped

A probe was put at all three sites, OUTSIDE the `if (trait_ref.is_valid())` the
existing counters sit inside - which is the entered-counter those sites lack,
and the reason "never called" and "called and refused" have been the same
number there.

On `examples/14-threads` it produced **no rows at all**, with the audit stream
carrying 9,044 other rows in the same run and the probe's three tags present in
the binary. The loops never execute.

The reason is not the corpus. `GenericParamNode.constraints` is populated only
by the INLINE `<T: Bound>` syntax; a `where` clause becomes a `TraitBound` and
travels elsewhere. The tree writes `where`: **398 where-clauses across 86
stdlib files, and not one inline constraint outside a docstring** in `stdlib`,
`compiler/src`, `examples` or `tests`. The regex was controlled against a known
inline bound first, because an empty result from a broken pattern is the same
output as an empty result from an empty population.

So this is a third kind of starvation, after the corpus and the tool of 8.81:
**starved by source syntax**. No corpus in the repository can reach these sites,
and none ever could.

A project written to use the syntax reaches exactly one of them:

    5 x  typeres-bound   unstamped   namemiss-leafok

`create_generic_param_types` runs; `create_param_type` and `symbolic_param_ref`
do not, even here. And every reach is UNSTAMPED - `identity()` would return the
written leaf, `lookup_by_name` would miss, `lookup_by_leaf` hits.

#### The pass-ordering question, answered by measurement

The stamp is not missing. It lands late:

    log line 578   BOUND-PROBE       typeres-bound  unstamped
    log line 628   BOUND-STAMP-ALL   stamp_trait_ref  Tagged -> InlineGenericBound::Main::Tagged

`stamp_generic_constraints` does stamp the inline constraint, fifty rows after
the site that read it. The identity exists; it is simply not there yet when
`create_generic_param_types` asks.

That makes the accessor swap a no-op today: with `resolved_name` invalid,
`identity()` returns `written_leaf()` by definition, so all three sites would
compile to the same lookup they perform now. A change that produces no
behavioural difference while reading as a correctness fix is worse than no
change, because it retires the question.

#### Why the ordering is not a reordering

`create_generic_param_types` has **seven callers**, spanning
`passes/specialization.cryo`, `passes/type_resolution.cryo`,
`sema/async_lower.cryo` and `sema/sema.cryo`, while the stamper is one routed
pass. Making the stamp precede every read is not moving one pass in front of
another; it is establishing an ordering across sema, specialization and async
lowering. That is a structural change to what a program compiles to - for any
plural leaf it changes which trait a bound names - and it is not this session's
to make.

Declined, with the measurement recorded. The accessor swap should follow the
ordering fix, not precede it.

#### The syntax is inert, which is why nobody noticed

Two controls on the fixture, both of which it failed as a bounds test:

* dropping the constraint (`<T: Tagged>` to `<T>`) compiles identically, so the
  bound is not load-bearing for the body's `x.tag()`;
* instantiating with a type that does not implement the trait is rejected as
  `E0358: no method named 'tag'` - a method-resolution failure in the body, the
  same error an unbounded `<T>` gives. Nothing reports a violated bound.

An inline constraint is parsed, stored, resolved to a trait TypeRef, and then
observed by nothing. That is the answer to why a whole syntax went unwritten and
unnoticed, and why these three sites could sit uninstrumented indefinitely.

The fixture is committed anyway, as `tests/tests/projects/inline_generic_bound`,
and its header says exactly this so that a pass is never read as a bound having
been checked. It exists because it is the only thing in the tree that makes the
constraint loop reachable: a counter over that code reads the same zero whether
it is starved or dead, and this project is the difference. If bound enforcement
is ever implemented, the negative case belongs beside it.

#### What this says about the eight remaining undistinguished zeros

Two of them are these sites, and they are now distinguished: not dead, not
starved by corpus - unreachable without a syntax the tree does not use, and
unstamped when reached. The instrument that answered was an entered-counter
placed outside the guard the existing bump sits inside, which is the same
instrument the other undistinguished zeros need and do not have.

The order stands as the audit put it: instrument, then fixture. A fixture
against a blind instrument answers nothing - and the reverse also held here,
since the entered-counter alone would have said "never runs" without the fixture
to say what running would look like.

#### 8.84 addendum: the two quiet sites are entered thousands of times

8.84 said `create_param_type` and `symbolic_param_ref` are "unreachable, even
here". That was a zero over one corpus stated as a property, which is the error
this section exists to stop. An entered-counter at FUNCTION level, rather than
inside the constraint loop, corrects it:

| site | entered | of those, carrying constraints |
|---|---:|---:|
| `symbolic_param_ref` | 3,165 | **0** |
| `create_generic_param_types` | 665 | **5** |
| `create_param_type` | 423 | **0** |

All three run, and run hot. None is dead code and none is uncalled. What is
empty is the LOOP: only `create_generic_param_types` is ever handed a parameter
carrying an inline constraint, and only on the one project in the tree that
writes the syntax. The `lookup_by_leaf` line in the other two is never reached
because there is nothing to iterate.

"Unreachable" and "entered 3,165 times with an empty loop" are different
findings with different consequences, and only the second is true.

One observation not chased: `symbolic_param_ref` saw 3,165 parameters and zero
constraints on a project whose parameters demonstrably carry them - the same
declarations reach `create_generic_param_types` with `consPLUS`. The nodes the
symbolic walk sees are not the ones the parser attached constraints to. Recorded
as an open question, not investigated.

#### Method: a compound command reported the wrong step's status

The probe-removal script failed its own assertion and printed a traceback, and
the shell line was `python ...; git diff --stat; echo ...; make cryo`, so the
command reported `make cryo`'s exit 0. The probes stayed in the tree AND went
into the rebuilt compiler, and two gates were then run against a probed binary.

Caught by looking at the diff rather than the exit code. This is the rule
CLAUDE.md already states - read the log's own summary, not a chained status -
and it is the third instance in two sessions of a rewrite reporting success
having changed nothing. The per-anchor assertion from 8.83 is necessary and was
not sufficient: the assertion fired correctly and the SHELL discarded it.

The sources were restored with `git checkout` against HEAD rather than by
patching the patch, because HEAD was known probe-free; the compiler was rebuilt
and confirmed to contain zero probe strings before any gate result was believed.

### 8.85 Intrinsic census: 84 of the 87 contested leaves are not intrinsics at all - MEASURED 2026-09-04

The decision on 8.65 is to namespace intrinsics and reach them by explicit path,
with no precedence rule and no registration-order hack, and to reduce the
intrinsic count rather than adjudicate collisions. This is the census that sizes
it, taken before any change.

#### The load-bearing question: codegen recognises intrinsics BY NAME

`IntrinsicKind::from_name(name)` at `codegen/ops/intrinsics_codegen.cryo:176`
maps a literal name string to an enum, and `intrinsic_emitter` switches on that
enum. So lowering is name-driven and a rename breaks it - but the scope of that
exposure is much smaller than the intrinsic count:

**56 of the 142 declarations are matched by `from_name`.** The other 86 are
declared `intrinsic function` and lowered by nothing; codegen emits an ordinary
call for them.

#### The cross-tabulation

87 of 142 have a same-leaf declaration elsewhere in the stdlib, re-measured here
independently of 8.65 and agreeing with it.

| | lowered by `from_name` | not lowered |
|---|---:|---:|
| **contested** | **3** | **84** |
| uncontested | 53 | 2 |

**84 of the 87 collisions are between a declaration codegen does not lower and a
`extern "C"` twin that already exists.** 86 of the 87 twins live in one file,
`stdlib/ffi/libc.cryo` - the file carrying the topo-sort import edge. These are
not intrinsics in any sense the compiler acts on; they are C functions wearing
the keyword.

The three contested AND lowered are `panic`, `vfprintf`, `vprintf`. Those are
the only ones needing a real answer rather than a deletion.

`intrinsics.cryo`'s own header already states the doctrine the decision asks
for: "Everything else that lives in libc (`printf`, `strlen`, `fopen`) belongs
in the `ffi` module as explicit C externs." The file has drifted from its own
rule by 84 declarations.

#### The two uncontested unlowered are NOT deletable

`format` and `try_catch` are absent from `from_name` and are still special:
`format`'s body is emitted per-module by `emit_format_runtime`, and `try_catch`
is the `catch_unwind` primitive. **Absence from `from_name` does not mean "an
ordinary call"**, so category D cannot be swept by the same argument as
category A. Any deletion pass must check the emitter, not just the name table.

#### Blast radius, source-visible

Explicit `intrinsics::<name>(` call sites for the 84:

| tree | sites |
|---|---:|
| `compiler/src` | 273 |
| `examples` | 87 |
| `tests` | 41 |
| `stdlib` | 37 |
| `tools` | 6 |
| **total** | **444** |

`free` 148, `malloc` 138, `printf` 110 account for most of it.

#### The cost nobody has priced: the twins do not have the same signatures

Comparing each of the 84 against its `ffi::libc` twin:

| | count |
|---|---:|
| identical signature | 26 |
| differ in parameter NAMES only | 0 |
| **differ in TYPES** | **58** |

The differences are systematic, not incidental: the intrinsic takes `string`
where libc takes `u8*`, and `void*` where libc takes `u8*`.

    malloc    intrinsic(u64)->void*          libc(u64)->u8*
    memcpy    intrinsic(void*,void*,u64)->void*  libc(u8*,u8*,u64)->u8*
    fopen     intrinsic(string,string)->void*    libc(u8*,u8*)->void*
    printf    intrinsic(string,args...)->i32     libc(u8*,...)->i32

So "point the callers at `libc::`" is not a rename. For 58 of the 84 it is a
rename plus a cast at each call site, and the intrinsic signature is the more
ergonomic of the two - which is presumably why the declarations were added.

#### The question that decides the cheapest route, and is NOT yet answered

How a bare `malloc` binds today is not established, and the obvious answer is
contradicted by two prior measurements. `stdlib/prelude.cryo:17` says
`public module core::intrinsics;` - but `public module` was measured four ways
to grant no visibility, and 8.65 measured that `IntrinsicDeclaration` never
calls `export_symbol`, so the module exports 3 names while declaring 142. An
importer binds none of them.

Yet `libc.cryo`'s comment names the failure as an ambiguous overload **at
codegen** (E0636), and codegen binds unpinned calls by name and arity. That
points at the declaration index rather than the resolver's scope chain as the
place the two twins actually contest.

Until that is measured, the cheapest option cannot be chosen, because it decides
whether the collision disappears by removing a prelude edge, by deleting 84
declarations, or only by changing what codegen indexes. **Measuring it is the
next step and it precedes any edit.**

#### The shape of the options, to be costed once that is answered

* **Delete category A, point callers at `libc::`.** Removes 84 of 87 collisions
  and the topo-sort edge. Costs 444 call-site edits, 58 of them with casts, and
  is source-visible in `examples` and `tests`.
* **Keep the ergonomic signatures as ordinary Cryo functions** wrapping
  `ffi::libc`, the way `core::mem::size_of` wraps its intrinsic. The casts live
  once in the wrapper instead of at 444 call sites, and the wrappers are
  ordinary functions reached by path.
* **Category B (`panic`, `vfprintf`, `vprintf`) is separate under either**, and
  is the only part touching `from_name`.

Not started; reported for a decision.
