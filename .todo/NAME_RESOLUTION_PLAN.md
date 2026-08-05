# Name Resolution: Unification Plan

> Status: **proposal / roadmap**. Nothing here is committed work. This document
> describes the path from Cryo's current six-system, fallback-cascade name
> resolution to a two-table resolution model with a single authoritative
> path-resolution pass and package-scoped cycle rules.
>
> Every current-state claim in §2 was verified against the tree on
> **2026-07-27** and carries a `file:line` anchor. Where a claim is judgment
> rather than measurement (effort sizing, §7) it is labelled as such. Where it
> is a measurement, the method is stated so it can be re-run.

### Decisions locked (2026-07-27)

| # | Decision | Effect |
|---|---|---|
| D1 | Re-export syntax is **`public import`** | Consistent with `public module`; no new keyword. §5 Phase 1. |
| D2 | The package is a **cycle unit only** — not a rib tier | Siblings still write imports; intra-package import cycles become legal. Rib chain unchanged. §3.1. |
| D3 | All work lands on branch **`name-res-impl`**, cut from `main` after `ll-impl` merges | One long-lived branch; phase boundaries are commits, not merges. §5. |
| D4 | Deletion of the global leaf index is what fixes §2.4 | Not the package. Credit assigned to Phase 5, not Phase 2. §3.1. |

### Revision historyc

**Revision 3 (2026-07-27)** — D1–D4 applied. D2 is the substantive one: revision
2 gave the package two jobs (cycle unit *and* implicit rib), and only the first
is load-bearing. Dropping the rib removes a rib tier, a sibling-ambiguity rule,
and the `alloc::Weak` migration; it also required correcting revision 2's claim
that the package fixes §2.4 — it does not, Phase 5 does (D4).

**Revision 2 (2026-07-27)** — audit against the tree:
- The **package** introduced as the unit of cycle-freedom (§3.1), replacing
  revision 1's "whole module graph" framing, which would not have dissolved the
  leaf-index fallback (§2.10a).
- Resolution split into **two tables** — path and type-dependent (§3.2 rule 1).
  Revision 1 treated method/trait dispatch as absorbable by the resolver; it is
  not, and the old Phase 4 exit criterion was unreachable because of it.
- `Res` constrained to be **substitution-stable** (§3.2 rule 5). Revision 1
  listed mono as an untouched non-goal; that was wrong.
- Path rule relaxed from "rooted or bare" to **"first segment in scope,
  remainder rooted"** (§3.2 rule 6).
- **Phases renumbered into execution order**; the counter moved into Phase 0.
- Six lookup systems, not five; five copies of the suffix heuristic, not two.

---

## 1. Purpose & scope

Cryo resolves names — types, functions, methods, paths — in at least six
independent subsystems that do not share an answer. The authoritative
resolution pass runs, computes a correct result, stores it in a lossy
span-keyed side table, and every downstream stage then re-derives names from
strings using its own heuristics.

The result is a class of bug that recurs indefinitely: a name binds to
whichever same-leaf entity happened to register first, program-wide, and the
mis-binding surfaces at a distant stage (monomorphization, codegen) rather
than at the write site. Each fix to date has taught one more stage one more
heuristic, because there is no single authority to defer to.

**Goal:** path resolution happens once, its answer is stamped on the AST node,
and every downstream stage reads it. Type-dependent resolution happens once, in
sema, and is likewise recorded rather than re-derived. No global name index, no
fuzzy matching, no ordered fallback cascade, no duplicate implementations.

**This is a multi-week structural change, not a bug fix.** It is written to be
staged: each phase is independently valuable and leaves the tree better than it
found it, and the sequence can pause at any phase boundary.

### Non-goals

- **Not a rewrite of the `Resolver` core.** The scope tree, `SymbolID` arena,
  intern table, and `Scope::find` are sound (`resolver/resolver.cryo:18-53`,
  `resolver/scope.cryo`). They are under-used, not wrong. Keep them.
- **Not a change to module *discovery*.** How a file is found on disk and
  becomes a `ModuleInfo` (namespace-scan plus `public module` declarations,
  `module_loader.cryo`) stays as-is. §3.1 groups modules that discovery has
  already found; it does not change how they are found.
- **Not a change to the rib chain.** Under D2 the bare-name precedence order is
  exactly what §3.2 rule 6 states today. No new tier is introduced.
- **Not a renaming or re-namespacing of anything.** Module namespaces stay
  file-derived and unchanged: `Compiler::AST::Visitor::ASTVisitor` remains
  exactly that. This is load-bearing for the mangling non-goal below.
- **Not a mangling change.** `mangled_name.cryo` / `demangler.cryo` consume
  resolved entities and should need no semantic change. This holds only under
  an invariant that must be asserted explicitly and tested: **a canonical name
  is always the declaration site, never the re-export path or the import
  alias.** Phase 1 adds re-exports; if that invariant slips, mangles move and
  the pin delta is nonzero.
- **Not a rewrite of monomorphization** — but mono is *not* untouched. §3.2
  rule 5 imposes an invariant on `Res` that mono must be audited against, and
  `mono/call_specializer.cryo` carries one of the six lookup systems (§2.1).

---

## 2. Verified current state

### 2.1 Six independent lookup systems

| # | System | Anchor | Keyed by |
|---|--------|--------|----------|
| 1 | `Resolver` scope tree | `resolver/resolver.cryo:18-53`, `:316` | `SymbolID` via scope chain |
| 2 | `TypeArena` leaf index | `types/arena.cryo:140`, `:1023` | bare leaf → qualified, **global** |
| 3 | `DeclarationIndex` | `decl_index.cryo:167-180`, `:214`, `:216-249` | qualified + `bare_alts` + own privacy gate |
| 4 | Namespace suffix match | five copies — see §2.5 | namespace **substring** |
| 5 | Mono template registry | `mono/call_specializer.cryo:2364-2385` | bare name + arity + longest-shared-prefix |
| 6 | Codegen LLVM symtab | `codegen/` (~77 name-keyed call sites) | name strings |

Systems 2–6 exist because system 1's answer is unavailable downstream (§2.2).

System 5 was missed in revision 1. `GenericRegistry::get_template` is properly
keyed by qualified name (`types/generic_registry.cryo:773`); the problem is the
fallback beneath it, which linear-scans every template by **bare** name and
disambiguates by call arity, then by longest shared module prefix with the
calling module (`mono/call_specializer.cryo:2364-2385`). Its own comment
concedes the hazard: *"Multiple modules can define a same-leaf generic free
function … a blind first-match ignores arity and module and can bind the call
to the wrong body."*

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
(`:33-39`) records that collisions silently overwrite, calling the map
*"best-effort … not a correctness gate."*

**This is the root cause.** Every other finding below is a consequence.

Note precisely what the defect is: the **span key**, not the side table. Go
records the same information in side maps (`types.Info.Defs` / `Uses`) keyed by
node *identity* and it is exact. On-node storage (§3.2 rule 3) is still the
better choice for Cryo — cheaper and impossible to desynchronize — but "side
table" is not itself the error, and the LSP index in Phase 3 may remain one.

### 2.3 `resolve_named` is a nine-step fallback cascade

`types/resolver.cryo:1224-1439`, in order:

1. Generic bindings (`:1229`)
2. Associated-type projection, by scanning the string for `::` (`:1241-1260`)
3. Primitive-name check (`:1266`)
4. Home-module preference (`:1287-1302`) — the most recent addition
5. Ambiguity check → E0203 (`:1304-1328`)
6. DI exact qualified literal (`:1349-1355`)
7. DI canonicalized qualified (`:1358-1365`)
8. DI bare name (`:1368`); then arena / scope-chain+arena, bootstrap-only (`:1379-1407`)
9. Arena **leaf index** (`:1431`)

Each step's comment documents the specific past defect that motivated it.
Steps annotated as removed dead code — *"0 hits across all builds"*
(`:1409-1412`) — show the cascade is grown and pruned empirically rather than
designed.

Step 2 is worth calling out separately: `I::Item` where `I` is a generic
parameter is a **type-relative path**, and resolving it requires knowing `I`'s
bound. It is type-dependent resolution living inside the path resolver. §3.2
rule 1 is what gives it a home.

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

**This case is the plan's acceptance test.** A design that does not make that
file writable has not solved the problem this document exists to solve. Under
D4, the fix is Phase 5's deletion of this map, plus Phase 1's aliasing where a
genuine collision remains — **not** the package (§3.1).

### 2.5 Partial paths match by substring, not by rooting — in five places

`resolver/resolver.cryo:584-600`, `module_ns_matches_prefix`, accepts the
written prefix as a run of whole `::`-delimited segments occurring **anywhere**
inside the module namespace. Consequently `Lsp::Protocol::Lsp` is matched by
both `Protocol::…` (segment 1) and `Lsp::…` (segments 0 *and* 2).

The same idea is hand-reimplemented four more times:

| Copy | Anchor | Form |
|---|---|---|
| 1 | `resolver/resolver.cryo:584-600` | segment-run substring scan |
| 2 | `sema/call_resolver.cryo:4067` | `resolve_module_qualified_function` |
| 3 | `codegen/visit/call_emitter.cryo:1930-1939` | *"equals `scope_str` OR ends with `::scope_str`"* |
| 4 | `mono/call_specializer.cryo:2364-2385` | longest-shared-prefix + arity |
| 5 | `resolver/name_resolution.cryo:1036-1053` | suffix-match fallback for sub-module imports |

The codegen copy's comment at `:585` states that sema *"mirrors this"*; the mono
copy's comment says it matches *"sema's `resolve_module_qualified_function`."*
Five hand-maintained copies of one heuristic, each documented as tracking one of
the others.

**Consequence:** the meaning of a partially-qualified path depends on what
other modules exist in the program.

### 2.6 Ambiguity is diagnosed in six places — but they are not six copies of one thing

Sites: `types/resolver.cryo:1306-1327` (E0203), `sema/call_resolver.cryo:1862`,
`:2093`, `:2915`, `codegen/visit/call_emitter.cryo:604-609`, and
`decl_index.cryo:630`.

Revision 1 treated these as one duplicated diagnostic. They are two distinct
kinds, and conflating them made the old plan's exit criteria unreachable:

**Path ambiguity** — a written path names more than one entity. Resolvable
before types exist; belongs in the resolver, once.
`types/resolver.cryo:1306`, `sema/call_resolver.cryo:2093` (bare call over
same-signature free functions), `:2915` (`report_scope_ambiguity`, same-leaf
types in a scope-resolution path), `codegen/visit/call_emitter.cryo:604`,
`decl_index.cryo:630`.

**Type-dependent ambiguity** — resolvable only once the receiver's type is
known. `sema/call_resolver.cryo:1862` is *"call to `X` is ambiguous: it is
provided by multiple traits implemented for this type."* That is trait
selection over a receiver type. It **cannot** move into a pre-typeck pass and
must stay in sema permanently. Rust reports the same condition from typeck
(`E0034`), never from its resolver; Go separates it the same way
(`types.Info.Selections` vs `Uses`).

A backend emitting a *path*-resolution diagnostic is still direct evidence the
backend is resolving paths — that part of revision 1's argument stands, and
`codegen/visit/call_emitter.cryo:604` remains a deletion target.

This also produces position-dependent behavior. Observed 2026-07-27 in
`tools/CryoLSP/src/handlers/keyword_docs.cryo:103`: on one line, bare `Range`
in **annotation** position resolved to `Lsp::Protocol::Lsp::Range` (proven — the
`Location { uri, range }` literal type-checked), while bare `Range::new` in
**call** position on the same line, same scope, was rejected as E0154-ambiguous.
Two positions, two resolvers, one name.

### 2.7 Imports bind by last-writer-wins, silently

`resolver/name_resolution.cryo:995-1008`, wildcard import:

> *"Uses `declare_import` (last-import-wins) so that when two modules export the
> same bare name, the later import takes priority."*

Two wildcard imports exporting the same leaf shadow each other by **import
statement order**, with no diagnostic. This is a third source of
order-dependence, alongside the global leaf index (§2.4) and the substring
matchers (§2.5), and revision 1 did not list it.

### 2.8 Importing a parent module does not import its submodules

`resolver/name_resolution.cryo:988` calls `get_exports(module_sym)` for the
imported module **only**. `import Compiler::AST;` therefore binds the exports
declared in `AST/_module.cryo` and nothing from `Compiler::AST::Visitor`.

This is the direct proof that the leaf index is the *sole* path by which
`AST/node.cryo:27` resolves `ASTVisitor*` today. It is not one fallback among
several for that case; it is the only one.

### 2.9 A parallel namespace exists for C-FFI bindings

Types carrying a `binding_namespace` (C-imported structs, unions, enums,
aliases, globals) are **deliberately not declared** in the file scope
(`resolver/name_resolution.cryo:78-84`, `:87-93`, `:96-102`, `:115-122`,
`:126-130`). Their comments state the reason: declaring the bare name *"would
shadow same-named types in other modules."* They are reachable only through the
`decl_index` qualified key (`cit::Vec2`).

That is a second name environment with its own visibility rules, and any spec
or DI cleanup that does not account for it will break FFI.

### 2.10 Two constraints that dictate sequencing

**(a) The fallback is load-bearing for the compiler's own source — and the
reason is a cyclic *import*, not a forward reference.**

`types/resolver.cryo:1414-1419` records that the leaf index is *"load-bearing
for circular forward references: AST node files reference `ASTVisitor*` in
`accept()` methods but can't import `Compiler::AST::Visitor` without creating a
cycle."*

Revision 1 proposed to fix this by running resolution over the whole graph
after parsing. That would not have worked, because the ordering it describes
already exists:

- `run_frontend` (`instance.cryo:2588-2617`) lexes and parses **every** module
  before any later stage runs.
- `run_module_resolution` (`:2622-2654`) then runs `NameResolution` over every
  module in topo order.
- `collect_type_declarations` (`:2660-2688`) registers all type **names** across
  all modules specifically to handle forward references — its own comment cites
  the `node.cryo` / `visitor.cryo` case.

The actual blocker is that **cycles are illegal**:
`ModuleGraph::compute_order()` returns false when it cannot place every module
(`module_graph.cryo:328`, `:364-391`). `AST/node.cryo:7` imports only the parent
`Compiler::AST`; `AST/visitor.cryo:8-9` imports `Compiler::AST::Node`. A direct
`import Compiler::AST::Visitor;` in `node.cryo` would close a cycle and fail the
sort. Combined with §2.8, that leaves the leaf index as the only route.

So the unlock is not "resolve later"; it is **to give cyclic sibling imports a
legal home**. §3.1 is that home.

**(b) Imports cannot express what users need.** `ImportStyle` is
`{ Wildcard, Specific }` (`AST/_module.cryo:217-220`). `ImportDeclNode.alias`
(`AST/declaration.cryo:563`) supports only whole-module `import … as X`. There
is **no per-item aliasing** and **no explicit re-export**. A user facing a
genuine same-leaf collision has no tool except writing the full path at every
use site.

Together these mean the fuzzy matching is *compensating for missing language
features*. Removing it before adding them would make the language harder to
write, and the fallbacks would be re-added under pressure.

### 2.11 Module granularity, measured

Method: `namespace` declaration extracted from every `.cryo` under
`compiler/src`, `stdlib`, `tools`, and `tests`, then counted by frequency;
`_module.cryo` manifests located with `find`.

- **Every namespace maps to exactly one file.** No namespace appears twice.
  Cryo's `module` is therefore a *file* — the granularity of Rust's `mod`, not
  of a Go package.
- **50 directories carry a manifest**: 46 under `compiler/src` and `stdlib`,
  plus 4 under `tools/CryoLSP` (`src`, `src/server`, `src/handlers`,
  `src/protocol`). Largest: `stdlib/core` (19 files),
  `compiler/src/compiler/sema` (16), `compiler/src/compiler/types` (15).
  Mean ≈ 7.
- **One directory has `.cryo` files but no `_module.cryo`:** the `stdlib` root,
  whose manifest is `lib.cryo` (`namespace std;`). One special case, not a
  pattern.
- `_module.cryo` already functions as a package manifest: `AST/_module.cryo:9-18`
  declares `public module Node; … public module NodeLocator;`, exactly matching
  the ten sibling files.
- Those declarations are already **discover-only, with no dependency edge**
  (`module_loader.cryo:671-672`; `submodule_paths` at `:1066` is commented
  *"`public module` declarations (discover only, no dep edge)"*).
- Of 363 `public module` declarations, 153 use a nested `A::B` spelling. Spot
  check: `stdlib/process/_module.cryo` has `namespace std::process;` and
  declares `public module process::signal;` — a redundant self-prefix, not a
  grandchild edge. This is a path-spelling artifact of discovery, not a
  structural relationship. **A full inventory is a Phase 2 task**; the design
  below assumes the spot check generalizes, and Phase 2 must confirm it.

**Reading:** the package concept is already latent in the tree — manifest,
membership list, and exclusion from the dependency graph all exist. What is
missing is that a directory is not a *cycle* unit.

### 2.12 Intra-package name collisions, measured

Method: for each of the 50 manifest directories, extract every
`type struct|enum|class|union|trait <Name>` declared across its files and report
duplicates within the directory.

**Result: exactly one collision in the entire tree** — `Weak`, declared in both
`stdlib/alloc/arc.cryo:257` and `stdlib/alloc/rc.cryo:198`. `tools` and `tests`
add none.

The equivalent scan for top-level free functions returned no duplicates, but the
free-function pattern is less reliable than the type pattern and should be
re-run with a real AST query during Phase 0 rather than trusted as-is.

**Under D2 this measurement carries no migration cost** — without a package rib,
siblings never collide by bare name, and `alloc::Weak` needs no change. It is
retained as evidence for a different purpose: it is the cost of *later* adopting
an implicit package rib (§6), and that cost is currently one name.

### 2.13 Test coverage is insufficient to refactor against

Name-resolution behavior is pinned by a handful of `tests/tests/lang/` files
(`leaf_scope_defn_a`/`_b`, `leaf_scope_use_a`/`_b`, `leaf_dispatch*`,
`scope_shadowing`, `fn_pointer_shadowing`) and two negative tests
(`tests/tests/negative/E0154_ambiguous_bare_call.cryo`,
`E0154_ambiguous_trait_method.cryo`). For six lookup systems and a nine-step
cascade, that is far too thin to refactor safely.

**The harness has no expected-fail mechanism.** Searched for
`expected_fail` / `xfail` / `known_fail` across `scripts/`, `tests/`, and
`stdlib/test/`: the only hit is `tests/tests/projects/known_fail_canary`, which
verifies that failure *detection* works and is unrelated. Phase 0's corpus
strategy depends on marking entries expected-fail with a phase number, so
**building that mechanism is a Phase 0 work item**, not an assumption.

### 2.14 What was not verified

- The ~77 name-keyed lookup sites under `codegen/` were counted, not
  classified. Some are legitimate LLVM symbol emission rather than entity
  resolution. Treat 77 as an **upper bound** on the Phase 4 worklist; Phase 0's
  counter replaces this guess with a measurement.
- The 153 nested `public module A::B;` declarations were spot-checked, not
  enumerated (§2.11).
- The free-function half of the collision scan (§2.12).
- Effort estimates in §7 are judgment, not measurement.

---

## 3. Target architecture

### 3.1 The package — a cycle unit, and nothing more

**A package is a directory carrying a manifest (`_module.cryo`; `lib.cryo` at
the `stdlib` root).** It is not recursive — a subdirectory is a different
package. 50 exist today (§2.11).

Under **D2** the package has exactly one job:

> **Intra-package import cycles are legal. Cross-package imports must form a
> DAG.**

Two supporting rules:

1. **Membership is derived from location.** A discovered module belongs to the
   package whose manifest sits in its directory. *Discovery itself is
   unchanged* (§1 non-goal) — this is a grouping over modules the loader has
   already found, however it found them.
2. **Nothing else changes.** Siblings still write imports. The rib chain is
   untouched. `public module` keeps both its current meanings. No new ambiguity
   tier exists, so §2.12's `alloc::Weak` needs no migration.

**What this buys, precisely.** `AST/node.cryo` may write
`import Compiler::AST::Visitor;` and `AST/visitor.cryo` may keep
`import Compiler::AST::Node;`. Both are in `compiler/src/compiler/AST/`, so the
cycle is legal and `ASTVisitor` resolves through an ordinary import. That
removes the only route by which §2.10a required the leaf index, which is what
unblocks Phase 5.

**What this does *not* buy — a correction to revision 2.** Revision 2 argued
that the package makes ambiguity "package-local" and thereby fixes the LSP case
in §2.4. That argument only held under the implicit-rib variant, which D2
rejects. Under D2, `Lsp::Protocol::Lsp::Diagnostic` and
`Compiler::Diag::Diagnostic` are kept apart by ordinary imports plus the
**deletion of the global leaf index in Phase 5** (D4). The package is not doing
that work and should not be credited with it.

**Why the package rather than the whole compilation as the cycle unit.**
Legalizing cycles program-wide would force every stage that loops
`ctx.module_graph.topo_order` — roughly fifteen in `instance.cryo` — to tolerate
a non-topological order. Confining cycles to a directory keeps the DAG at
package granularity: those loops iterate packages, then members within a
package. The blast radius shrinks instead of growing.

**The one genuinely new mechanism.** Order within a package is undefined, so
intra-package declaration resolution must be lazy and dependency-driven, with
cycle detection that distinguishes two cases:

- **Declaration cycle** — reached through a pointer or reference (`ASTVisitor*`).
  Needs a declaration, not a layout. **Legal.**
- **Definition cycle** — reached through by-value containment (struct `A` holds
  a `B` that holds an `A`). Needs a layout that cannot exist. **Hard error**,
  printing the cycle.

This is Go's `objDecl` colour-marking discipline. It is the substance of Phase
2 and the reason the cycle rule is sound rather than hopeful.

### 3.2 Six rules

These are the acceptance criteria for the whole effort.

1. **There are two resolution answers, and they are recorded separately.**

   - `Res` — *path → definition*. Produced by the resolver, before types exist.
     Covers bare names, rooted paths, imports, and the **base** of a
     type-relative path.
   - `TypeDependentRes` — *node → definition*. Produced by sema once the
     receiver type is known. Covers method calls, trait selection,
     associated-item projection, and overload selection.

   A path the resolver can only partially answer (`I::Item`, §2.3 step 2)
   records its base `Res` plus a count of unresolved trailing segments, which
   sema finishes. Rust splits these as `Res`/`PartialRes` vs
   `TypeckResults::type_dependent_defs`; Go splits them as
   `types.Info.Uses` vs `Selections`. Two independent implementations converged
   here; this is not a stylistic choice.

2. **Resolve once per table; never re-derive.** No stage may re-derive what the
   resolver answered, and no stage may re-derive what sema answered. This is
   what the Phase 0 counter measures, and it is measurable precisely *because*
   the two tables are distinguished — legitimate post-resolution lookups fall
   in the second bucket by construction, not by exception.

3. **The answer lives on the node.** Span-keyed lookup is lossy by construction
   (§2.2). A resolution slot on the node is exact and costs nothing.

4. **One `resolve_path(segments, ns, scope) -> Res`.** Namespace (type / value /
   module) is a *parameter*, not a separate code path. The §2.6
   annotation-vs-call asymmetry cannot exist after this.

5. **A `Res` names a definition, never an instantiation.** A generic parameter
   `T` resolves to `Res::GenericParam`, not to whatever `T` is bound to at some
   call site. Substitution happens strictly in the type layer, below `Res`.

   This rule exists because of a recorded landmine: `NamedAnnotation.pre_resolved`
   short-circuits annotation resolution, so a pre-resolved annotation is
   **invisible to substitution**, and `ASTCloner` deliberately drops every
   `resolved_type` for exactly that reason. Stamping `Res` on template bodies
   without rule 5 would reintroduce that bug at whole-AST scale. Under rule 5
   the cloner may copy `Res` verbatim, because it is substitution-stable by
   construction.

   **If a node kind cannot satisfy rule 5, it keeps re-resolving and that is
   documented** — an explicit, enumerated exception, not a silent one.

6. **Paths are non-fuzzy: the first segment resolves in scope, the remainder is
   rooted.**

   The first segment resolves as a bare name through the rib chain. Every
   segment after it is rooted at what the first one named, resolved
   segment-by-segment, or it is an error with a suggestion. `future::ready(...)`
   is legal exactly when `future` is bound in scope, and then it means exactly
   one thing.

   This kills all five substring matchers (§2.5) without requiring every partial
   path in the tree to be rewritten — where a prefix is genuinely in scope the
   code stays legal, and where it isn't the fix is to add the missing import.
   It is also what Rust does, so the ergonomics are known-good.

   The bare-name rib chain, in precedence order — **unchanged from today under
   D2**:

   ```
   locals / generic params
     →  current-module items
     →  file imports
     →  prelude
   ```

   Prelude is the outermost rib, not a privileged tier — so "imports shadow
   prelude" is a consequence of the model rather than a rule someone
   implemented. Ambiguity is reportable only *within* a tier, and only from the
   resolver.

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
   workaround.** §2.10b is why. If removing a heuristic makes valid code
   unwritable, the enabling feature ships first.
5. **Measure before scoping.** §2.11, §2.12, §2.13's harness gap, and the Phase
   0 counter each replaced a guess with a number, and each changed the plan.
   Prefer the tool to the intent-audit.

---

## 4. What already exists — do not rebuild

- `Resolver` scope tree, `SymbolID` arena, scope kinds, `Scope::find` with its
  O(1) name index (`resolver/scope.cryo:87`) — sound; extend, don't replace.
- `InternTable` (`resolver/intern_table.cryo`) — sound.
- Per-scope ambiguity marking (`resolver/scope.cryo:100`, `:161-189`) — the
  right idea, currently consulted by only one of the path-ambiguity sites.
- `resolve_type_qualified_name_bare_from`'s **explicit start-scope parameter**
  (`resolver/resolver.cryo:653`) — the doc comment at `:639-652` reasons
  correctly about why an ambient cursor is unsafe under recursion. This is the
  shape `resolve_path` should keep.
- `DeclarationIndex` qualified registry — keep; retire only the `bare_alts`
  fuzzy paths.
- `ImportDeclNode.path_segments` / `path_segment_spans`
  (`AST/declaration.cryo:575-576`) — per-segment spans already exist, which
  Phase 1's diagnostics and the LSP both need.
- **The latent package machinery** (§2.11): `_module.cryo` as manifest,
  `submodule_paths` as a dependency-edge-free membership list
  (`module_loader.cryo:671-672`, `:1066`), `ModuleGraph::name_index`. Phase 2
  adds a grouping level and a cycle rule; it does not invent the concept.
- `GenericRegistry::get_template`'s **qualified** key
  (`types/generic_registry.cryo:773`) — correct; only the bare-name fallback
  beneath it (§2.1 system 5) is the problem.

---

## 5. Phased plan

**Branch (D3):** all work lands on `name-res-impl`, cut from `main` after
`ll-impl` merges. Phase boundaries are commits on that branch, not merges to
main. Each boundary still requires a green self-host both OS and a green
`make test`; a repin happens at any boundary that moves the pin (Phase 1
certainly does — it adds syntax).

Phases are listed in execution order. Two dependencies drive the sequence:

- **Imports before packages.** Phase 2 makes intra-package cycles legal, which
  means source starts writing sibling imports that did not exist before; Phase
  1's aliasing and re-exports should be available when that migration happens.
  Phase 1 also needs repin lead time, so starting it early is free.
- **Packages before `Res`.** Stamping `Res` before the cycle rule exists would
  stamp answers for a graph shape that Phase 2 then changes.

### Phase 0 — Spec, conformance corpus, and the counter

**Deliverables**

- `docs/name-resolution.md`: the normative rules. The package as cycle unit
  (§3.1); the two resolution tables and where the boundary lies (§3.2 rule 1);
  the rib chain; what a path means; when ambiguity is an error; how re-exports,
  the prelude, and the FFI binding namespace (§2.9) participate.
- **Expected-fail support in the test harness** (§2.13) — the corpus strategy
  below does not work without it. Each entry carries a phase number; the harness
  fails if an expected-fail entry unexpectedly *passes*, so flips are noticed.
- `tests/tests/lang/resolve/` — 40–60 minimal programs, plus negative cases
  under `tests/tests/negative/`.
- **The instrumentation counter**, moved here from revision 1's Phase 4.

**Why the spec first.** Today the rules exist only as ~15 comment blocks, each
describing a patch. That makes "is this a bug or intended?" unanswerable, which
is precisely why every fix has been local. The spec makes the question
answerable; the corpus makes it enforceable.

**Why the counter first.** It is instrumentation only — zero risk, no behavior
change — and it converts §2.14's *"77 is an upper bound"* into a measured,
classified worklist. Phase 4 is one of the two variance drivers in §7;
measuring it up front for a day's work removes that variance before the
sequence commits to it. Build it with **two buckets** (§3.2 rule 2): lookups
that must reach zero, and type-dependent lookups that are legitimate and stay.

Instrument: `TypeArena::lookup_by_leaf`, the `DeclarationIndex` `bare_alts`
paths, all five suffix matchers (§2.5), `mono/call_specializer`'s bare-name
scan, and the name-keyed `codegen` sites.

**Corpus must cover**

- local shadowing; generic param vs. same-named type
- import vs. prelude precedence; two wildcard imports of the same leaf (§2.7)
- same leaf declared in two modules, one imported / both imported / neither
- home-module preference (the `:1287-1302` case)
- **intra-package import cycle through a pointer** (legal)
- **intra-package by-value definition cycle** (hard error, cycle printed)
- **cross-package cycle** (still an error)
- first-segment-in-scope paths: legal when the prefix is bound, error when not
- fully-rooted path exactness
- **type-relative paths**: `T::Assoc`, `This::Item`, `Self::new`
- re-export chains, and per-item aliases
- a mangled symbol pinned **through** a re-export chain (the §1 invariant)
- glob/wildcard vs. explicit-import shadowing precedence
- private type invisibility across modules; private item shadowing a visible one
- FFI `binding_namespace` types reachable only by qualified key (§2.9)
- ambiguity → E0154 with both candidates named
- **annotation position and call position agree** (the §2.6 regression)
- resolution of nodes synthesized *after* the resolver runs (`async_lower.cryo`)
- the §2.4 acceptance test: two packages each defining `Diagnostic`, both used

Write tests against **desired** behavior. ~~Mark currently-failing ones as
expected-fail.~~ **SUPERSEDED — there is no `xfail` mechanism and one will not
be added.** It was built once and removed, and its re-addition was refused:
*"I would rather have a test fail over a patchy green using xfail as a mask."*

Use the **tripwire pattern** instead: a real, passing test of the behavior the
compiler has *today*, under a `WRONG_` name, with a loud header stating what
the spec requires and a FLIP PROTOCOL for the change that fixes it — move the
case to a `compile_fail` project with the right `expect.diagnostic`, in the
same change, rather than weakening the assertion. `resolution_tripwire` is the
worked example, and the protocol was exercised end-to-end on 2026-08-05 when
its visibility half became `tests/tests/projects/visibility_gate`.

Two things that pattern must carry, both learned the hard way:

- **A control, and sometimes a control on the control.** A probe written to the
  spec rather than to the parser tests nothing: top-level declarations are
  PUBLIC by default (Q7), so a visibility probe that omits `private` is vacuous.
- **A way to show WHICH mechanism it exercised.** `visibility_gate` needs one
  private callee per binding door, because a single firing door otherwise
  satisfies the whole assertion — and its door-3 module turned out not to be
  compiled at all, which nothing but a per-rejection audit line could see.

**Exit:** corpus committed and wired into `make test`; every entry either
passes, or is a tripwire whose header names the spec section it violates and
the flip it will get; counter builds and reports a baseline for both buckets.

---

### Phase 1 — Make imports expressive

**Change.** Add the two capabilities the fallbacks are substituting for.

- **Per-item aliasing:** `import Lsp::Protocol::Lsp::{ Range as LspRange };`
- **Explicit re-export (`public import`, D1):** so `std::Range` resolves because
  `stdlib/lib.cryo` *says so* — greppable, and the resolver follows the edge.

**Why first.** Phase 5 removes fuzzy matching and Phase 2 changes which imports
source files write. Without aliasing, a genuine collision leaves the author
writing `A::B::C::Type::method(...)` at every use site, and the heuristics get
re-added under pressure. Principle 4.

**Bootstrap sequencing — do not skip.** `public import` and per-item aliases are
new syntax. The pinned compiler cannot parse them, so this is a two-phase repin:

1. Land parse + resolve support. Do not use the syntax anywhere yet.
2. `make pin` — **both OS** (`bin/cryo` and `bin/cryo.exe`).
3. Only then migrate stdlib, compiler, and LSP source to use it.

**Work**

- Parser: extend `ImportStyle` (`AST/_module.cryo:217-220`) and
  `specific_imports` to carry an optional per-item alias.
- Resolver: aliases become ordinary rib bindings; re-exports become edges the
  scope walk follows. Reuse the existing `import_aliases` mechanism
  (`resolver/resolver.cryo:42`, `:413`).
- Replace last-import-wins (§2.7) with a diagnostic at the file-imports tier.
- Stdlib: declare the re-exports that shallow paths currently get by accident.
  `std::Range` becomes real or becomes an error — decide per name, deliberately
  (§9 Q1).
- Assert the canonical-name invariant from §1: re-export and alias edges must
  not change a canonical name. The corpus entry that pins a mangled symbol
  through a re-export is the gate.
- LSP: `Lsp::Protocol::Lsp::Range::new(...)` at the four sites fixed on
  2026-07-27 becomes an aliased import.

**Exit:** aliasing + re-export corpus entries pass; stdlib's intended shallow
paths are explicit; pin delta zero for the mangling invariant; self-host green
both OS.

---

### Phase 2 — The package becomes a cycle unit

**Change.** Implement §3.1: package grouping, package-level topological
ordering, and lazy intra-package declaration resolution with definition-cycle
detection.

**Why.** This is the architectural unlock. It **dissolves the stated
justification for the leaf-index fallback** (§2.10a) rather than working around
it. Without it, Phase 5 is impossible — the compiler's own AST files cannot
compile.

**Work**

- Group discovered modules into packages by manifest directory; handle the
  `stdlib`-root special case (§2.11).
- Complete the nested-`public module` inventory (§2.11, §2.14) and normalize the
  redundant self-prefix spelling.
- Move `compute_order()` to package granularity. Within a package, order is
  undefined; the per-file loops in `instance.cryo` iterate packages, then
  members. Cross-package cycles remain a hard error, with the cycle printed.
- Implement lazy, dependency-driven intra-package declaration resolution with
  colour-marking; **declaration cycles legal, definition cycles a hard error**
  naming the cycle.
- Widen `NameResolver`'s pass-1 horizon (`resolver/name_resolution.cryo:56-62`)
  from file to package.
- Remove the `bootstrap_mode` arena fallbacks (`types/resolver.cryo:1379-1407`),
  which exist only because types are queried mid-population.
- Migrate `AST/node.cryo` and its peers to a real
  `import Compiler::AST::Visitor;` — the case §2.10a is about.
- **LSP impact:** per-file compilation (persistent per-file contexts, LRU) now
  needs the whole *package* resolved before any member resolves. Measure the
  latency change and decide whether the LRU is keyed per package rather than
  per file.

**Exit:** package and cyclic-sibling corpus entries pass; `bootstrap_mode`
deleted; the counter's bucket-1 reading has dropped measurably; LSP latency
measured and acceptable; self-host green both OS.

---

### Phase 3 — `Res` on the node, one entry point, two tables

**Change.**

- Add a resolution slot to path-bearing AST nodes:
  `Res = Def(SymbolID) | Local(...) | GenericParam(...) | PrimTy(...) | Err`,
  plus an unresolved-trailing-segment count for type-relative paths.
- Add the `TypeDependentRes` table, populated by sema (§3.2 rule 1).
- Collapse annotation-position and call-position onto a single
  `resolve_path(segments, ns, scope) -> Res`, keeping the explicit start-scope
  parameter from `resolver/resolver.cryo:653`.
- **Audit mono against §3.2 rule 5** before anything downstream reads `Res`:
  confirm `ASTCloner` may copy the slot verbatim, and enumerate any node kind
  that cannot satisfy the rule.
- Retire `ResolutionMap` as a correctness path. Keep a span→symbol index **only**
  as an LSP/dead-code convenience, rebuilt from node slots so it is a derived
  view rather than a parallel authority (§9 Q2).

**Why.** Rules 1, 3 and 4. This is where the §2.6 asymmetry becomes structurally
impossible rather than fixed.

**Exit:** annotation/call agreement corpus entries pass; type-relative-path
entries pass; `ResolutionMap` no longer consulted for correctness anywhere;
mono audit written down; self-host green both OS.

---

### Phase 4 — Convert downstream, and prove it

**Change.** `type_resolution` → `sema` → `mono` → `codegen` read the stamped
`Res` (or `TypeDependentRes`) instead of re-deriving.

**The counter built in Phase 0 is the instrument.** It reports the exact
remaining worklist rather than an intent-audit. Bucket 1 (path re-derivation)
must reach zero; bucket 2 (type-dependent lookup) is expected to be nonzero and
is enumerated, not eliminated.

**Work order** (each independently verifiable):

1. `type_resolution` — the largest consumer of the cascade.
2. `sema` / `call_resolver` — delete `resolve_module_qualified_function`'s
   suffix walk once callers read `Res`. Keep `:1862`: it is bucket 2.
3. `mono` — `call_specializer` bare-name scan (§2.1 system 5) and
   `trait_specializer` leaf handling.
4. `codegen` — classify the ~77 sites (§2.14); convert entity resolution, leave
   genuine LLVM symbol emission alone.

**Exit:** bucket 1 reads **zero** on a full self-host build; bucket 2 is
enumerated and each entry justified in `docs/name-resolution.md`; self-host
green both OS.

---

### Phase 5 — Delete

**Remove**

- `TypeArena::leaf_index`, `lookup_by_leaf`, `register_leaf_name`
  (`types/arena.cryo:140`, `:1023-1042`) — this is the deletion that fixes §2.4
  (D4)
- All five suffix matchers (§2.5), including `module_ns_matches_prefix` and
  `resolve_qualified_type_via_exports` (`resolver/resolver.cryo:526-600`),
  `CallEmitter::collect_namespace_suffix_matches`
  (`codegen/visit/call_emitter.cryo:1915`), `mono/call_specializer.cryo:2364-2385`,
  and `resolver/name_resolution.cryo:1036-1053`
- The codegen path-ambiguity site (`codegen/visit/call_emitter.cryo:589-613`)
- `DeclarationIndex` `bare_alts` / `bare_alts_index` fuzzy paths
  (`decl_index.cryo:167-180`)
- The nine-step cascade in `resolve_named` → one lookup
- Every **path**-ambiguity site except the resolver's; path ambiguity is
  diagnosed **once**, with both candidates and a suggested aliased import.
  Type-dependent ambiguity (`sema/call_resolver.cryo:1862`) stays, by design.

**Also land here:** the `tools/CryoLSP/src/protocol/lsp.cryo:8-14` wire-side
`Diagnostic` that §2.4 records as omitted. It is the acceptance test; write it
and confirm it compiles.

**Exit:** whole corpus green including previously-expected-fail entries;
self-host byte-identical fixed point both OS; `make test` green; repin both OS.

---

## 6. Rejected and deferred alternatives

Recorded so they are not re-proposed.

**Whole compilation as the cycle unit** — *rejected*. Forces every `topo_order`
loop in `instance.cryo` to tolerate non-topological order, for no benefit the
package doesn't already deliver.

**Directory as the *namespace* (true Go packages)** — *rejected*. Collapsing
`Compiler::AST::Node::ASTNode` to `Compiler::AST::ASTNode` would match Go
exactly, but it changes every canonical name, therefore every mangle, therefore
the pin — and it breaks the §1 non-goal for no resolution benefit.

**Package as an implicit rib (Go-style sibling visibility)** — *deferred, not
rejected* (D2). Siblings would be visible with no import at all. It costs a rib
tier, a sibling-ambiguity rule, and makes dropping a file into a directory
change resolution for its siblings. §2.12 measures the migration cost at
**one name** (`alloc::Weak`), so this stays cheap to adopt later if the
sibling-import boilerplate proves annoying. Do not adopt it during this plan —
it would add a moving part to the phase that already carries the most risk.

**"Rooted or bare, never partial"** (revision 1's rule 4) — *rejected*. Sound,
but it makes every partially-qualified path in stdlib and the compiler an error
at once, for no gain over §3.2 rule 6, which is equally non-fuzzy.

**Absorbing method/trait dispatch into the resolver** — *rejected*. Not
implementable; dispatch needs the receiver's type. Attempting it is what made
revision 1's Phase 4 exit criterion unreachable.

---

## 7. Sizing

Judgment, not measurement. Assumes focused work, one phase at a time.

| Phase | Work | Estimate |
|---|---|---|
| 0 | Spec + corpus + expected-fail harness + counter | ~1 week |
| 1 | Import aliasing + re-export (incl. 2-phase repin) | ~1 week |
| 2 | Packages + lazy intra-package resolution | 1–2 weeks |
| 3 | `Res` on node, two tables, mono audit | ~1 week |
| 4 | Downstream conversion | 1–2 weeks |
| 5 | Deletion + fallout | ~3 days |

**Total: 5–7 weeks.** Phases 2 and 4 carry the variance — Phase 2 because the
lazy declaration resolver is genuinely new code, Phase 4 because of the codegen
long tail. D2 removed the package rib from Phase 2 but the estimate is
unchanged: the rib was the cheap half, and the lazy resolver is the expensive
half.

---

## 8. Risks

| Risk | Mitigation |
|---|---|
| Phase 2's lazy declaration resolver proves harder than estimated | It is the load-bearing unlock; if it stalls, stop at Phase 0 + 1 (both independently valuable) and re-scope. Do not attempt Phase 5 without it. |
| Removing fallbacks breaks valid stdlib/compiler/LSP code | Phase 1 ships the replacement capability first (principle 4). The Phase 0 counter finds every reliance before anything is deleted. |
| `Res` goes stale under monomorphization | §3.2 rule 5 makes `Res` substitution-stable by construction; Phase 3 gates on an explicit mono audit before any downstream stage reads the slot. This is the specific bug that `ASTCloner`'s `resolved_type` drop was working around. |
| Canonical names shift under Phase 1's re-exports, moving mangles | Explicit invariant in §1, asserted by a corpus entry that pins a mangled symbol through a re-export chain; zero pin delta is a Phase 1 exit condition. |
| Silent behavior change — a name binds differently without erroring | Exactly what the Phase 0 corpus exists to catch; byte-identical self-host is the second net. |
| New syntax outruns the pinned compiler | Phase 1's two-phase repin is mandatory and sequenced in the phase body. Both OS, never Linux-only. |
| LSP latency regresses when package-granular resolution lands | Measured as a Phase 2 exit condition, with per-package LRU keying as the fallback. |
| Long-lived branch diverges from main | D3 accepts this deliberately. Keep `name-res-impl` rebased on main at each phase boundary rather than at the end. |

---

## 9. Questions

### Resolved

- **R1 — Re-export syntax.** `public import` (D1). Consistent with the existing
  `public module`; no new keyword enters the language.
- **R2 — Is `stdlib/core` (19 files) one package?** **Yes.** It has one
  `_module.cryo`, so it is one package. Package size is not a concern; Go
  packages routinely run larger, and the measured collision count inside it is
  zero (§2.12).
- **R3 — Does the package carry a rib?** **No** (D2). Cycle unit only. This
  supersedes revision 2, which said yes, and moots the `alloc::Weak` question
  entirely — `Rc::Weak` and `Arc::Weak` are untouched and never collide.
- **R4 — Should partially-qualified paths be legal?** **Yes, under §3.2 rule 6
  only**: the first segment must resolve in scope, and the remainder is rooted
  from there. This is not the fuzzy matching of §2.5 — no substring scanning, no
  dependence on what other modules exist — and it avoids rewriting every partial
  path in the tree.
- **R5 — Branch strategy.** One long-lived `name-res-impl` branch off `main`,
  cut after `ll-impl` merges (D3). Phase boundaries are commits; rebase on main
  at each boundary.

### Open

- **Q1 — Does `std::Range` remain valid after Phase 1?** It works today only by
  accident. Either `stdlib/lib.cryo` re-exports it explicitly or it becomes an
  error with a suggestion. Same question for every shallow stdlib path
  currently reachable by leaf matching — worth an inventory during Phase 1.
- **Q2 — Keep a span→symbol index for the LSP?** `passes/dead_code.cryo` and
  the LSP both benefit. Cleanest is to rebuild it from node slots after
  resolution, so it is a derived view rather than a parallel authority. Note
  §2.2: the defect was the span *key*, not the side table — a node-identity key
  would be exact.
- **Q3 — Do the 153 nested `public module A::B;` declarations all follow the
  spot-checked pattern?** (§2.11, §2.14.) If any are genuine grandchild
  declarations rather than redundant self-prefixes, §3.1 rule 1 needs a
  qualifier. Inventory during Phase 2.
- **Q4 — What is the diagnostic for a cross-package cycle?** Today
  `compute_order()` prints unplaced modules to a debug channel
  (`module_graph.cryo:364-382`). Under Phase 2 it becomes a user-facing error
  and needs an error code and a printed cycle path.
