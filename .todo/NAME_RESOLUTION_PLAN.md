# Name Resolution: Unification Plan

> Status: **proposal / roadmap**. Nothing here is committed work. This document
> describes the path from Cryo's current five-system, fallback-cascade name
> resolution to a single authoritative resolution pass.
>
> Every current-state claim in §2 was verified against the tree on
> **2026-07-27** and carries a `file:line` anchor. Where a claim is judgment
> rather than measurement (effort sizing, §6) it is labelled as such.

---

## 1. Purpose & scope

Cryo resolves names — types, functions, methods, paths — in at least five
independent subsystems that do not share an answer. The authoritative
resolution pass runs, computes a correct result, stores it in a lossy
span-keyed side table, and every downstream stage then re-derives names from
strings using its own heuristics.

The result is a class of bug that recurs indefinitely: a name binds to
whichever same-leaf entity happened to register first, program-wide, and the
mis-binding surfaces at a distant stage (monomorphization, codegen) rather
than at the write site. Each fix to date has taught one more stage one more
heuristic, because there is no single authority to defer to.

**Goal:** one resolution pass, whose output is stamped on the AST node, is
read by every downstream stage, and is scoped per-module rather than
program-global. No fuzzy matching, no ordered fallback cascade, no duplicate
implementations.

**This is a multi-week structural change, not a bug fix.** It is written to be
staged: each phase is independently valuable and leaves the tree better than it
found it, and the sequence can pause at any phase boundary.

### Non-goals

- **Not a rewrite of the `Resolver` core.** The scope tree, `SymbolID` arena,
  intern table, and `Scope::find` are sound (`resolver/resolver.cryo:18-53`,
  `resolver/scope.cryo`). They are under-used, not wrong. Keep them.
- **Not a change to module *discovery*.** Namespace-scan discovery and
  `public module` visibility stay as-is. This plan is about how a *name*
  resolves, not how a *file* becomes a module.
- **Not a mangling change.** `mangled_name.cryo` / `demangler.cryo` consume
  resolved entities; they are downstream of this work and should need no
  semantic change (their inputs get more accurate, not different in kind).
- **Not "remove all string lookups from the compiler."** Codegen legitimately
  resolves LLVM symbols by name at emission time. The rule is narrower and
  stated in §3: no stage may re-derive *which entity a source path refers to*
  after resolution has answered that question.

---

## 2. Verified current state

### 2.1 Five independent lookup systems

| # | System | Anchor | Keyed by |
|---|--------|--------|----------|
| 1 | `Resolver` scope tree | `resolver/resolver.cryo:18-53`, `:316` | `SymbolID` via scope chain |
| 2 | `TypeArena` leaf index | `types/arena.cryo:140`, `:1023` | bare leaf → qualified, **global** |
| 3 | `DeclarationIndex` | `decl_index.cryo:167-180`, `:214`, `:216-249` | qualified + `bare_alts` + own privacy gate |
| 4 | ModuleGraph suffix match | `sema/call_resolver.cryo:4067`, `codegen/visit/call_emitter.cryo:1915` | namespace **substring** |
| 5 | Codegen LLVM symtab | `codegen/` (~77 name-keyed call sites) | name strings |

Systems 2–5 exist because system 1's answer is unavailable downstream (§2.2).

### 2.2 The resolver's output is discarded

`resolver/name_resolution.cryo:8-9` states:

> *"After this pass, downstream consumers look up results by SymbolID; no more
> string-based name lookups."*

This is not true today. The only consumer of `ResolutionMap` outside
`resolver/` is `passes/dead_code.cryo:112`. Sema, type_resolution, mono, and
codegen never read it.

It also could not be authoritative in its current form. The map is keyed by a
packed span — 32-bit file hash | 16-bit start line | 16-bit start col
(`resolver/resolution_map.cryo:40-46`) — and its own doc comment
(`:36-39`) records that collisions silently overwrite, calling the map
*"best-effort … not a correctness gate."*

**This is the root cause.** Every other finding below is a consequence.

### 2.3 `resolve_named` is a nine-step fallback cascade

`types/resolver.cryo:1205-1439`, in order:

1. Associated-type projection, by scanning the string for `::` (`:1241-1260`)
2. Primitive-name check (`:1266`)
3. Home-module preference (`:1287-1302`) — the most recent addition
4. Ambiguity check → E0203 (`:1304-1328`)
5. DI exact qualified literal (`:1349-1355`)
6. DI canonicalized qualified (`:1358-1365`)
7. DI bare name (`:1368`)
8. Arena by name / scope-chain + arena — bootstrap-only (`:1379-1407`)
9. Arena **leaf index** (`:1431`)

Each step's comment documents the specific past defect that motivated it.
Steps 5b and 6 of an earlier numbering are annotated as removed dead code —
*"0 hits across all builds"* (`:1409-1412`) — which shows the cascade is grown
and pruned empirically rather than designed.

### 2.4 The leaf index is global and first-writer-wins

`types/arena.cryo:1032-1042`:

```cryo
// First registration wins; ambiguous names require qualified imports
if (this.leaf_index.get(&leaf_sym.id).is_none()) {
    this.leaf_index.insert(leaf_sym.id, qualified_name.id);
}
```

One unscoped `HashMap<u32, u32>` (`:140`) for the entire compilation. Because
stdlib types register before user modules, `std::core::ops::Range` claims the
leaf `Range` program-wide; every later lookup reaching step 9 receives it.

This is not a latent risk — it has already shaped source. `tools/CryoLSP/src/protocol/lsp.cryo:8-14`
declines to model a wire-side `Diagnostic` because *"Cryo's resolver exposes a
dependency's type names globally,"* which would break the compiler library the
LSP links against. That comment is an accurate description of this map, and it
records a feature omitted to route around it.

### 2.5 Partial paths match by substring, not by rooting

`resolver/resolver.cryo:584-600`, `module_ns_matches_prefix`, accepts the
written prefix as a run of whole `::`-delimited segments occurring **anywhere**
inside the module namespace. Consequently `Lsp::Protocol::Lsp` is matched by
both `Protocol::…` (segment 1) and `Lsp::…` (segments 0 *and* 2).

Codegen carries a second implementation of the same idea — *"namespace equals
`scope_str` OR ends with `::scope_str`"* (`codegen/visit/call_emitter.cryo:1930-1939`)
— and its comment at `:585` states that sema *"mirrors this."* Two
hand-maintained copies of one heuristic.

**Consequence:** the meaning of a partially-qualified path depends on what
other modules exist in the program.

### 2.6 Ambiguity is diagnosed in four places

`types/resolver.cryo:1306-1327` (as E0203), `sema/call_resolver.cryo:1862`,
`:2093`, `:2915`, `codegen/visit/call_emitter.cryo:604-609`, and
`decl_index.cryo:630`. A backend emitting a *name-resolution* diagnostic is
direct evidence the backend is still resolving names.

This also produces position-dependent behavior. Observed 2026-07-27 in
`tools/CryoLSP/src/handlers/keyword_docs.cryo:103`: on one line, bare `Range`
in **annotation** position resolved to `Lsp::Protocol::Lsp::Range` (proven — the
`Location { uri, range }` literal type-checked), while bare `Range::new` in
**call** position on the same line, same scope, was rejected as E0154-ambiguous.
Two positions, two resolvers, one name.

### 2.7 Two constraints that dictate sequencing

**(a) The fallback is load-bearing for the compiler's own source.**
`types/resolver.cryo:1414-1419` records that the leaf index is *"load-bearing
for circular forward references: AST node files reference `ASTVisitor*` in
`accept()` methods but can't import `Compiler::AST::Visitor` without creating a
cycle."* The fallback cannot be deleted until cyclic module references resolve
properly.

**(b) Imports cannot express what users need.** `ImportStyle` is
`{ Wildcard, Specific }` (`AST/_module.cryo:217-220`). `ImportDeclNode.alias`
(`AST/declaration.cryo:563`) supports only whole-module `import … as X`. There
is **no per-item aliasing** and **no explicit re-export**. A user facing a
genuine same-leaf collision has no tool except writing the full path at every
use site.

Together these mean the fuzzy matching is *compensating for missing language
features*. Removing it before adding them would make the language harder to
write, and the fallbacks would be re-added under pressure.

### 2.8 Test coverage is insufficient to refactor against

Name-resolution behavior is pinned by five `tests/tests/lang/` files
(`leaf_scope_defn_a`, `leaf_scope_defn_b`, `leaf_scope_use_a`,
`leaf_scope_use_b`, `scope_shadowing`) and two negative tests
(`tests/tests/negative/E0154_ambiguous_bare_call.cryo`,
`E0154_ambiguous_trait_method.cryo`). For five lookup systems and a nine-step
cascade, that is far too thin to refactor safely.

### 2.9 What was not verified

- The ~77 name-keyed lookup sites under `codegen/` were counted, not
  classified. Some are legitimate LLVM symbol emission rather than entity
  resolution. Treat 77 as an **upper bound** on the Phase 4 worklist.
- Effort estimates in §6 are judgment, not measurement.

---

## 3. Target architecture

Four rules. They are the acceptance criteria for the whole effort.

1. **Resolve once. The answer is an entity, not a string.**
   A single pass produces a `Res` for every path-bearing AST node. Downstream
   stages read it. No stage may re-derive *which entity a source path names*
   after resolution has answered.

2. **The answer lives on the node, not in a side table.**
   Span-keyed lookup is lossy by construction (§2.2). A resolution slot on the
   node is exact and costs nothing.

3. **One `resolve_path(segments, ns, scope) -> Res`.**
   Namespace (type / value / module) is a *parameter*, not a separate code
   path. The §2.6 annotation-vs-call asymmetry cannot exist after this.

4. **Paths are rooted or bare; never fuzzy.**
   A path resolves segment-by-segment through the module tree, or it is an
   error with a suggestion. A bare name resolves through one rib chain with a
   written-down precedence:

   ```
   locals / generic params  →  current-module items  →  file imports  →  prelude
   ```

   Prelude is an implicit glob import at the outermost rib, not a privileged
   tier — so "imports shadow prelude" becomes a consequence of the model rather
   than a rule someone implemented. Ambiguity is reportable only *within* a
   tier, and only from the resolver.

### Guiding principles

1. **The self-host is the canary.** Every phase ends with a green Linux **and**
   Windows 6-stage byte-identity self-host (`scripts/selfhost-check.py`) plus
   the full test suite. The compiler is the largest Cryo program in existence
   and exercises resolution harder than any test.
2. **Tests are permanent; switches are temporary.** The Phase 0 corpus is a
   permanent gate. Any migration switch is deleted at its phase boundary, and
   at most one is live at a time.
3. **Fix the root in the shared layer.** No per-call-site special cases. If a
   phase's fix has a "…except when" clause, the model is wrong.
4. **Every deleted fallback must be replaced by a capability, not a
   workaround.** §2.7 is why. If removing a heuristic makes valid code
   unwritable, the enabling feature ships first.

---

## 4. What already exists — do not rebuild

- `Resolver` scope tree, `SymbolID` arena, scope kinds, `Scope::find` with its
  O(1) name index (`resolver/scope.cryo:87`) — sound; extend, don't replace.
- `InternTable` (`resolver/intern_table.cryo`) — sound.
- Per-scope ambiguity marking (`resolver/scope.cryo:100`, `:161-189`) — the
  right idea, currently consulted by only one of four ambiguity sites.
- `resolve_type_qualified_name_bare_from`'s **explicit start-scope parameter**
  (`resolver/resolver.cryo:653`) — the doc comment at `:639-652` reasons
  correctly about why an ambient cursor is unsafe under recursion. This is the
  shape `resolve_path` should keep.
- `DeclarationIndex` qualified registry — keep; retire only the `bare_alts`
  fuzzy paths.
- `ImportDeclNode.path_segments` / `path_segment_spans`
  (`AST/declaration.cryo:575-576`) — per-segment spans already exist, which
  Phase 2's diagnostics and the LSP both need.

---

## 5. Phased plan

### Phase 0 — Spec + conformance corpus

**Deliverables**

- `docs/name-resolution.md`: the normative rules. Namespaces; the rib
  precedence from §3.4; what a path means; when ambiguity is an error; how
  re-exports and the prelude participate.
- `tests/tests/lang/resolve/` — 40–60 minimal programs, plus negative cases
  under `tests/tests/negative/`.

**Why first.** Today the rules exist only as ~15 comment blocks, each
describing a patch. That makes "is this a bug or intended?" unanswerable, which
is precisely why every fix has been local. The spec makes the question
answerable; the corpus makes it enforceable.

**Corpus must cover**

- local shadowing; generic param vs. same-named type
- import vs. prelude precedence
- same leaf declared in two modules, one imported / both imported / neither
- home-module preference (the `:1287-1302` case)
- fully-rooted path exactness; **partially-qualified path is an error**
- re-export chains (`public module` re-export reachability)
- cyclic module references
- ambiguity → E0154 with both candidates named
- **annotation position and call position agree** (the §2.6 regression)
- private type invisibility across modules
- the LSP regression: two modules defining `Range`, one imported, both used

Write tests against **desired** behavior and mark currently-failing ones as
expected-fail. The corpus then doubles as the executable spec, and Phases 1–5
are measured by expected-fail entries flipping to pass.

**Exit:** corpus committed and wired into `make test`; every entry either
passes or is a recorded expected-fail with a phase number attached.

---

### Phase 1 — Resolve over the whole module graph

**Change.** Run name resolution after all modules are parsed, over the complete
graph, rather than per-module during a walk that can outrun its dependencies.

**Why.** This is the architectural unlock. It makes forward and cyclic
references resolve naturally, which **dissolves the stated justification for the
leaf-index fallback** (§2.7a) instead of working around it. Without this, Phase
5 is impossible — the compiler's own AST files cannot compile.

**Work**

- Split resolution into declaration collection (all modules) then body
  resolution (all modules). Pass 1 of `NameResolver` already forward-declares
  (`resolver/name_resolution.cryo:53-62`); widen its horizon from file to graph.
- Remove the `bootstrap_mode` arena fallbacks (`types/resolver.cryo:1374-1407`),
  which exist only because types are queried mid-population.
- Verify against the `Compiler::AST` ↔ `Compiler::AST::Visitor` cycle
  specifically — that is the known-hard case.

**Exit:** cyclic-reference corpus entries pass; `bootstrap_mode` deleted;
self-host green both OS.

---

### Phase 2 — Make imports expressive

**Change.** Add the two capabilities the fallbacks are substituting for.

- **Per-item aliasing:** `import Lsp::Protocol::Lsp::{ Range as LspRange };`
- **Explicit re-export:** a `public import` (or `public use`) form, so
  `std::Range` resolves because `stdlib/lib.cryo` *says so* — greppable, and
  the resolver simply follows the edge.

**Why here.** Phase 5 removes fuzzy matching. Without these, a genuine
collision leaves the author writing `A::B::C::Type::method(...)` at every use
site, and the heuristics get re-added under pressure. Principle 4.

**Work**

- Parser: extend `ImportStyle` (`AST/_module.cryo:217-220`) and
  `specific_imports` to carry an optional per-item alias.
- Resolver: aliases become ordinary rib bindings; re-exports become edges the
  scope walk follows. Reuse the existing `import_aliases` mechanism
  (`resolver/resolver.cryo:42`, `:413`).
- Stdlib: declare the re-exports that shallow paths currently get by accident.
  `std::Range` becomes real or becomes an error — decide per name, deliberately.
- LSP: `Lsp::Protocol::Lsp::Range::new(...)` at the four sites fixed on
  2026-07-27 becomes an aliased import.

**Exit:** aliasing + re-export corpus entries pass; stdlib's intended shallow
paths are explicit; self-host green both OS.

---

### Phase 3 — `Res` on the node, one entry point

**Change.**

- Add a resolution slot to path-bearing AST nodes:
  `Res = Def(SymbolID) | Local(...) | PrimTy(...) | Err`.
- Collapse annotation-position and call-position onto a single
  `resolve_path(segments, ns, scope) -> Res`, keeping the explicit start-scope
  parameter from `resolver/resolver.cryo:653`.
- Retire `ResolutionMap` as a correctness path. Keep a span→symbol index **only**
  as an LSP/dead-code convenience, explicitly documented as best-effort, or
  rebuild it from node slots.

**Why.** Rules 2 and 3. This is where the §2.6 asymmetry becomes structurally
impossible rather than fixed.

**Exit:** annotation/call agreement corpus entries pass; `ResolutionMap` no
longer consulted for correctness anywhere; self-host green both OS.

---

### Phase 4 — Convert downstream, and prove it

**Change.** `type_resolution` → `sema` → `mono` → `codegen` read the stamped
`Res` instead of re-deriving.

**The enforcement mechanism is the point of this phase.** Instrument every
string-keyed entity lookup — `TypeArena::lookup_by_leaf`, `DeclarationIndex`
`bare_alts` paths, `CallEmitter::collect_namespace_suffix_matches`,
`resolve_module_qualified_function`, name-keyed `resolve_function` — with a
counter that trips when hit *after* the resolution pass. Build the compiler
with it enabled and it reports the exact remaining worklist.

This converts "I suspect there are workarounds" into a finite, ordered list,
empirically. It is the same discipline as preferring `nm` over intent-audits:
let the tool enumerate reality rather than reasoning about it.

**Work order** (each independently verifiable):

1. `type_resolution` — the largest consumer of the cascade.
2. `sema` / `call_resolver` — delete `resolve_module_qualified_function`'s
   suffix walk once callers read `Res`.
3. `mono` — `call_specializer` / `trait_specializer` leaf handling.
4. `codegen` — classify the ~77 sites (§2.9); convert entity resolution, leave
   genuine LLVM symbol emission alone.

**Exit:** the post-resolution counter reads zero on a full self-host build;
self-host green both OS.

---

### Phase 5 — Delete

**Remove**

- `TypeArena::leaf_index`, `lookup_by_leaf`, `register_leaf_name`
  (`types/arena.cryo:140`, `:1023-1042`)
- `module_ns_matches_prefix` substring scanning and
  `resolve_qualified_type_via_exports` (`resolver/resolver.cryo:526-600`)
- `CallEmitter::collect_namespace_suffix_matches` and the codegen E0154 site
  (`codegen/visit/call_emitter.cryo:589-613`, `:1915`)
- `DeclarationIndex` `bare_alts` / `bare_alts_index` fuzzy paths
  (`decl_index.cryo:167-180`)
- The nine-step cascade in `resolve_named` → one lookup
- Three of the four ambiguity sites; ambiguity is diagnosed **once**, in the
  resolver, with both candidates and a suggested aliased import.

**Exit:** whole corpus green including previously-expected-fail entries;
self-host byte-identical fixed point both OS; `make test` green; repin.

---

## 6. Sizing

Judgment, not measurement. Assumes focused work, one phase at a time.

| Phase | Work | Estimate |
|---|---|---|
| 0 | Spec + conformance corpus | ~4 days |
| 1 | Whole-graph resolution | 1–2 weeks |
| 2 | Import aliasing + re-export | ~4 days |
| 3 | `Res` on node, one entry point | ~4 days |
| 4 | Downstream conversion + counter | ~2 weeks |
| 5 | Deletion + fallout | ~3 days |

**Total: 5–7 weeks.** Phase 1 and Phase 4 carry the variance — Phase 1 because
the cyclic-reference case is genuinely hard, Phase 4 because of the codegen
long tail.

---

## 7. Risks

| Risk | Mitigation |
|---|---|
| Phase 1 cyclic resolution proves harder than estimated | It is the load-bearing unlock; if it stalls, stop at Phase 0 + 2 (both independently valuable) and re-scope. Do not attempt Phase 5 without it. |
| Removing fallbacks breaks valid stdlib/compiler/LSP code | Phase 2 ships the replacement capability first (principle 4). Phase 4's counter finds every reliance before anything is deleted. |
| Silent behavior change — a name binds differently without erroring | Exactly what the Phase 0 corpus exists to catch; byte-identical self-host is the second net. |
| Scope creep into mangling / mono | Explicit non-goal (§1). Those consume resolved entities; their inputs get more accurate, not different in kind. |
| Interaction with in-flight async work | `sema/async_lower.cryo` is 5,745 lines and already leans on leaf handling (`:5140`). Do not run these tracks concurrently — debugging two moving frontends at once is a false economy. |

---

## 8. Open questions

1. **Does `std::Range` remain valid after Phase 2?** It works today only by
   accident. Either `stdlib/lib.cryo` re-exports it explicitly or it becomes an
   error with a suggestion. Same question for every shallow stdlib path
   currently reachable by leaf matching — worth an inventory during Phase 2.
2. **Re-export syntax:** `public import` reads consistently with the existing
   `public module`; `public use` reads more conventionally. Pick one before
   Phase 2 starts.
3. **Keep a span→symbol index for the LSP?** `passes/dead_code.cryo` and the
   LSP both benefit. Cleanest is to rebuild it from node slots after
   resolution, so it is a derived view rather than a parallel authority.
4. **Should partially-qualified paths ever be legal?** This plan says no —
   rooted or bare. Worth confirming, since some stdlib and compiler code
   currently relies on them and Phase 5 would need a mechanical sweep.
