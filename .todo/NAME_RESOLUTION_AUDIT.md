# Name Resolution: Post-Migration Audit Brief

> **Written 2026-08-05, mid-migration, to be executed AFTER the name-resolution
> unification is declared complete.** Write-once: it is not maintained, and by
> the time you read it the tree will have moved past the state described here.
> Every current-state claim below is anchored to 2026-08-05 and must be
> **re-verified, not trusted**. Where this document and the tree disagree about
> what exists, the tree wins. Where this document and the tree disagree about
> what *should* exist, `docs/name-resolution.md` wins.

---

## 0. What you are being asked to do

You are auditing Cryo's name resolution — symbol resolution, modules,
visibility, and scope — after a multi-session refactor that replaced a
six-subsystem, nine-step fallback cascade with a single path-resolution entry
point, explicit scope, and on-node resolution storage.

**The question is not "does it pass".** It passed before the refactor too. The
question is:

> Is this a *proper* implementation of symbol resolution, modules, and scope —
> or is it the old implementation with the failures routed around?

The owner's standing directive, in his words: *"I would rather rewrite/update
the compiler's source code vs working around the existing source code to get
things working."* A change that makes a defect unobservable while leaving its
cause in place is a **failed** audit item, even if every gate is green and the
code is tidy.

**Your default posture is falsification.** Do not open this expecting to
confirm the work. Try to produce a program that resolves a name incorrectly,
and try to find the place where a shortcut was taken. A clean audit is a
finding you *earned*, not a starting assumption.

### Report format

For each item: **verdict** (sound / workaround / incomplete / cannot-determine),
the **evidence** you gathered yourself, and — if you are calling something a
workaround — the **root fix** you would do instead. "Cannot-determine" is a
legitimate and useful verdict; a confident wrong verdict is not.

---

## 1. What the implementation was supposed to be

**Normative:** `docs/name-resolution.md`. Read it in full before anything else.
It is the spec; where the code disagrees with it, the code is the defect. Note
its §9 "Open questions" and the "Decided" subsections — decisions taken there
are binding and some are counterintuitive (see §5.3 below).

**Roadmap and history:** `.todo/NAME_RESOLUTION_PLAN.md` (rev 3). Useful for
intent; **not** normative, and parts of it were measured wrong during
implementation — the plan's two-bucket model needed three buckets, and several
milestones (M3, M4, M5) were retired as measured-dead rather than completed.

The five load-bearing claims the implementation had to deliver:

1. **One entry point.** All path resolution goes through a single function with
   `ns` as a *parameter*, not through separate type-position and value-position
   code paths.
2. **Explicit scope.** No ambient cursor. Resolution never depends on "whichever
   module the compiler happens to be standing in".
3. **Visibility is a gate, not a tie-breaker.** A `private` declaration is
   *unreachable* from outside its module — not merely deprioritized when
   something else is also a candidate.
4. **The answer lives on the node.** Not in a span-keyed side table that can
   silently collide.
5. **`Res::Err` is a value, not a retry signal.** There is no state meaning
   "didn't resolve, try something else." This is the rule whose absence created
   the nine-step cascade in the first place.

---

## 2. THE TRAP LIST — signals that look like evidence and are not

**Read this section before running a single command.** Every entry cost real
time during implementation. Several of them are *inverted*: they read as
clearance to proceed and mean the opposite. An auditor who trusts these will
write a confident, wrong, all-clear report.

### 2.1 The self-host fixed point cannot see a wrong scope

Measured 2026-08-04 on a full compiler build: of **13,370** resolutions where
the ambient cursor named the wrong module, **13,369** had exactly ONE candidate
in the entire program. Statically corroborated: 666 distinct type leaves across
`stdlib` + `compiler/src`, only **8** declared by more than one module.

⇒ A completely incorrect scope implementation produces a **bit-identical**
compiler. `make test`, both self-host halves, `b1-check`, and `make examples`
all stay green through it.

**The self-host is still the right gate for regressions. It is not evidence of
correctness for anything scope-related.** Do not accept "the self-host is
byte-identical" as an answer to any question in §5.2.

### 2.2 `CRYO_SCOPE_PROBE` measures the complement of what it appears to measure

The probe runs only inside cascade step 2c, which is reached only when a home
module is **already set**. The sites the keystone existed to fix are exactly the
ones that never reach it. A 4-module program with a genuinely plural leaf
reported `HomeDiffPlural = 0` while `lookup_qualified_alternatives` reported 2
ambiguous lookups in the same run.

⇒ `HomeDiffPlural > 0` is the **wrong** acceptance criterion. A correct entry
registers zero on it. Validate scope by **runtime-observable divergence** — a
wrong bind must produce a wrong *value* — not by this probe.

### 2.3 Two visibility counters are inverted signals

- **`FnBindPrivateBound == 0`** counts bindings enforcement would *catch*. The
  real failure mode of the visibility gate is **false positives**, which it does
  not measure at all. On 2026-08-04 this counter read 0, the gate was switched
  on, the compiler and stdlib self-built clean — and the test tree failed with 8
  errors, every one a call that never left its own file.
- **`RC_VIS_REJECT_NOTPUBLIC == 0`** is *expected*, not evidence. Top-level
  declarations are **public by default** (decision Q7), so `private` is the only
  informative keyword.

Additionally: **`is_candidate_public` defaults to `true`** when visibility is
unrecorded. A measured zero there needs a positive control before it means
anything.

### 2.4 A zero needs a control, and sometimes a control on the control

Budget for two layers. And note §2.2: some zeros are measured over the *wrong
population entirely*, where no number of controls helps — only changing the
question does. When you see a zero, ask "what would have to be true for this to
be zero for an uninteresting reason?" before asking anything else.

### 2.5 Gate-reading traps

- **A chained command masks a failed gate.** `make test > log; echo $?; tail log`
  reports `tail`'s exit code. **Read the log's own summary line.**
- **`projects: N passed` looks identical whether the project's tests ran or
  not.** Project tests (`<project>/tests/*.cryo`) execute via a *sub-invocation*
  and never appear in `cryo test --list` or `tests/test-roster.txt`.
  Counter-check by running the project directly.
- **`cryo check` emits no counter report** — it is on the success path after
  link, so every counter measurement needs a full successful build.
- **Diagnostics are buffered until `flush()`.** An early return that skips it
  prints `Project compilation failed (N errors)` with **no message**. If you see
  an error count with no errors, look for a flush-skipping return.
- **B1 is a ratchet, not `== 0`.** The `B1` flag in the counter report is a
  **family label, not the summation set**: `M1..M5 calls` and `lookup_by_leaf
  calls` carry the flag but are not summed. `total <= row_sum` is the invariant.
- **Counts drift between self-build runs, legitimately** — the counter's own
  source is part of the compiler being measured. Never diff two self-build runs;
  compare a **fixed external target** built by both compilers, with
  `CRYO_STDLIB` set for both or they are not comparable.

### 2.6 Measurement hygiene

`--no-incremental` and `CRYO_CODEGEN_THREADS=1` are both mandatory or the
numbers are silently wrong (incremental prints "up to date" and counts nothing;
multiprocess codegen drops child tallies). An instrument that *replays* a
production lookup must wrap the replay in `ResolveCounter::suspend()`/`resume()`
or it inflates what it measures.

---

## 3. Independent verification protocol

Do not audit by reading the implementer's tests and agreeing with them. Their
tests encode their model; if the model is wrong the tests are wrong in the same
direction.

**Build your own probes.** The corpus under test cannot exercise this subsystem
(§2.1) — you must supply the collisions yourself. The working pattern, already
in-tree as of 2026-08-05:

- `tests/tests/projects/resolution_scope` — three modules declaring the same
  leaves with **different values**, three consumers each importing exactly one.
  Three-way rather than two-way on purpose: a fix that prefers one module
  program-wide passes a two-way collision one time in two.
- `tests/tests/projects/generic_name_collision` — the same idea for generic
  functions, generic types, instance methods, and static method templates.
- `tests/tests/projects/resolution_tripwire` — known defects pinned **as they
  behave today**, each with a flip-protocol header.

**The design rule that makes these work:** a wrong bind must produce a wrong
*number*, not a failed compile. A compile error tells you something broke; a
wrong value tells you *which module won*. Where a wrong bind cannot produce a
wrong value, give the colliding types **different field names** so a wrong bind
is a hard error at the field access rather than a silent type-check.

**Write probes for cases the tree has never had.** As of 2026-08-05 the entire
project corpus contained **zero static generic methods** until one was added
that session — the largest single cluster of call-binding sites had no coverage
at all. Assume similar holes exist. Look for constructs the compiler supports
that no test exercises, and probe *those* first.

### Suggested probe axes

Trait-bounded receivers · associated-type projection · nested modules ≥3 deep ·
a package root addressed with no `import` · re-exports (if `public import`
landed) · import cycles within a package · generic default type arguments ·
turbofish at every call shape · `This`/`Self` inside trait defaults ·
cross-package `private` · a leaf declared by 3+ modules where all three are in
scope simultaneously · shadowing between a local, a param, an import, and a
prelude name.

---

## 4. Workaround smells — what a routed-around defect looks like

The failure mode to hunt for is **a fallback that was hidden rather than
deleted**. Concretely:

1. **A fallback made unreachable instead of removed.** Look for resolution steps
   still present in the source but with a guard that can never be false, or a
   counter that reads 0 because nothing calls it. If the plan says a step was
   deleted, confirm the *code* is gone — `grep` for the function, not for the
   counter row.
2. **A special case named after the thing that broke.** Any branch whose comment
   or identifier references a specific module, type, or symbol
   (`fmt_err`, `GlobalAlloc`, `Array`, `std::`) is a workaround until proven
   otherwise. Grep the resolution paths for string literals naming stdlib
   entities.
3. **An exemption that reads the ambient cursor.** This is *the* known trap. The
   visibility gate was built and reverted on 2026-08-04 because its same-module
   exemption asked `current_module_ns()` — the ambient cursor — and rejected
   calls that never left their own file. Any predicate of the form "…unless
   we're in the same module" must derive "same module" from the **node's own
   provenance**, never from what the compiler is currently walking.
4. **Two answers for one question.** The spec's whole premise is one entry
   point. If type position and value position still reach different code, or if
   a second binder (e.g. `sema/call_resolver.cryo` for bare free-function calls)
   still has its own scope model, the unification is incomplete regardless of
   test results. **Count the entry points yourself.**
5. **`Res::Err` used as a retry trigger.** If any consumer treats a failed
   resolution as "try the next strategy", rule 5 of §1 is violated and the
   cascade has been rebuilt under a new name.
6. **A side table that can collide.** The pre-refactor `ResolutionMap` was keyed
   by a packed span (32-bit file hash | 16-bit line | 16-bit col) and its own
   doc comment conceded collisions silently overwrite. If a span-keyed table
   still carries authoritative answers, §1 claim 4 is not met. A *derived* span
   index rebuilt from node slots is legitimate; an authoritative one is not.
7. **`xfail` / expect-fail.** Explicitly rejected for this repo — it was removed
   once and its re-addition refused: *"I would rather have a test fail over a
   patchy green using xfail as a mask."* If an expect-fail status has been
   reintroduced, that is a finding.
8. **A gate that cannot fail.** On 2026-08-04 the Linux self-host gate was found
   to be comparing **one** 953 KB file against the Windows half's 243-module
   103 MB tree — it had been passing for months and could not have failed.
   For every gate you rely on, make it fail on purpose once.

---

## 5. Audit checklist

Anchors are as of 2026-08-05 and may have moved. Re-locate by symbol name.

### 5.1 Visibility is a gate (spec §3.3)

The pre-refactor defect: `resolve_qualified_scoped`'s `cands.length == 1` fast
path returned `Unique` without consulting `is_candidate_public` or `ns_imports`
⇒ every declaration was in every module's namespace ⇒ every stage had to guess
⇒ each guess needed a heuristic. **This is why fallbacks regrew**, and it is the
single most important thing to confirm was fixed at the root.

Free-function calls historically had **three independent doors**, and closing
any two left the third open:

1. a written qualified path (`Vault::secret()`) resolving straight to a symbol;
2. the bare binder's single-overload fast path — the free-function twin of the
   `decl_index` fast path above, *the same root cause in the other lane*;
3. the multi-candidate bare path.

**Verify:** write a cross-package `private` call through each door
independently. All three must be rejected. Then verify the same-module exemption
does **not** false-positive — the reverted attempt failed here, not on
enforcement. Confirm the exemption's notion of "same module" is not the ambient
cursor (§4.3).

### 5.2 Scope is explicit (spec §5.2)

Confirm the ambient cursor is *gone*, not renamed. Specific checks:

- Is there still a "home-module preference" step, or anything that *corrects* a
  scope after the fact? Under an explicit-scope design there is nothing to
  correct and it should be deleted.
- Does any resolution entry point still consult "the module currently being
  processed"? Grep for the accessor, not the concept.
- **A context's home module is decided by where its SYNTAX was written**, not by
  which function constructs it. Two contexts built six lines apart in one
  function can belong to different modules — a turbofish written at the call
  site is the *caller's*, while a template's declared default type argument is
  the *callee's*. Verify the implementation distinguishes these. Getting it
  backwards is a miscompile that every gate in the repo stays green through.
- As of 2026-08-05 there was **no file→module mapping anywhere in the
  compiler**, and `TraitDeclNode` (`AST/declaration.cryo:877`) carried no module
  or namespace field at all — only a leaf `name` and an inherited span, with
  `get_trait_decl` keyed by **leaf name**. If those are still true, some sites
  *cannot* have been migrated correctly, and you should find out what was done
  there instead.

### 5.3 Paths (spec §5.1)

"The first segment resolves in scope; the remainder is rooted. No substring
matching, no suffix matching, no dependence on what other modules happen to
exist in the program."

Probe all four directions and expect the spec's answers, not the code's:

| direction | expectation |
|---|---|
| omit **leading** segments (`future` → `std::future`) | permitted — a scope question |
| omit **trailing** segments (`Lib` → `Lib::Helper`) | **rejected** — a false claim about where a declaration lives |
| interior/substring match | rejected |
| a prefix naming no module at all | rejected, with a suggestion |

Known-live as of 2026-08-05: **a bare name still resolved with nothing in
scope.** Deleting a module's only `import` produced no error — the global leaf
index answered from the whole program. Six leaf-index hits appeared, three with
a home module set and three with an **empty** use-site. Pinned in
`resolution_tripwire` (`orphan.cryo`, `depot.cryo`). **Confirm this is now an
error.** If the tripwire still passes unchanged, §5.1 is not enforced.

Binding decisions to check against (spec §9): a package root **is** implicitly
addressable; a parent module does **not** implicitly bind its submodules; an
import binds its **leaf** name.

### 5.4 Storage (spec §6.3)

Confirm a resolution slot exists on every path-bearing node kind —
`IdentifierNode`, `ScopeResolutionNode`, `NamedAnnotation`, `NewExprNode`,
`SizeofExprNode`/`AlignofExprNode`, `CallExprNode` callee, `ImportDeclNode`
(per segment), enum variant reference — and that consumers read the slot rather
than a span lookup.

**The `Res`/instantiation invariant is the subtle one.** `Res` must name a
*definition*, never an instantiation: `T` resolves to `Res::GenericParam`, not
to whatever `T` is bound to at some call site. Substitution happens strictly
below `Res`. This is what makes `Res` safe for `ASTCloner` to copy verbatim, and
its absence is exactly why `ASTCloner` had to drop every `resolved_type`
(`NamedAnnotation.pre_resolved` is a `TypeRef` — an instantiation). **Verify
`ASTCloner` now copies resolutions**, and that a generic instantiated at two
different types does not leak one's binding into the other.

### 5.5 Type-dependent resolution (spec §6.2)

`TypeDependentRes` — method calls, trait selection, associated-item projection,
overload selection — **cannot** move into the resolver; dispatch needs the
receiver's type. If the implementation claims to have absorbed method/trait
dispatch into path resolution, that is a red flag, not an achievement: an
earlier revision of the roadmap set an exit criterion that was unreachable for
exactly this reason. Rust reports the same condition from typeck (E0034), never
from its resolver.

Separately: B2 (type-dependent resolution) was **unmeasured** as of 2026-08-05 —
only assoc-type projection was instrumented, sema's method and trait dispatch
were not. If B2 is now claimed to be enumerated, verify the instrumentation
actually covers dispatch.

### 5.6 Fallback inventory

The cascade had nine steps. Several were measured dead (M3, M4, cascade 3a/3c,
2b, M5) and slated for deletion, each requiring a corpus entry pinning it first.

For each: confirm it is **deleted, not merely unreached**, and that the entry
pinning it exists. One paired-work case to check specifically: deleting M5 means
a stale module abbreviation no longer errors *at the import* — it silently binds
nothing and the user gets scattered "undefined type" errors with nothing
pointing at the cause. That deletion required an **E0500-family diagnostic with
a `suggest_module` hint first**. If M5 is gone and no such diagnostic exists,
that is a regression in diagnostic quality disguised as cleanup.

---

## 6. Acceptance criteria

Sound implementation, all of which must hold:

1. **One path-resolution entry point**, with namespace as a parameter. Count them.
2. **No ambient cursor** anywhere in resolution, including in visibility
   exemptions.
3. **A `private` declaration is unreachable from outside its module through
   every door**, with no false positives on same-module calls.
4. **A bare name that is not in scope is an error**, with a suggestion.
5. **Trailing-segment path omission is an error**; leading omission still works.
6. **Resolutions live on nodes**; any span-keyed table is derived, not
   authoritative.
7. **`Res` is substitution-stable** and `ASTCloner` copies it.
8. **No `Res::Err`-triggered retry** anywhere.
9. **Every deleted fallback has a corpus entry** pinning the behavior that
   replaced it.
10. **B1 = 0**, and the B1 gate has been converted from a ratchet to a literal
    `== 0` assertion.
11. **Every tripwire has been flipped**, each in the same change that made its
    defect an error — not by adjusting assertions.

Any item you cannot verify independently is `cannot-determine`, and say why.

---

## 7. Practical notes

```bash
make -C /workspaces/CryoLang test            # unit + compile-fail + projects
make -C /workspaces/CryoLang b1-check        # B1 ratchet
make -C /workspaces/CryoLang roster-check    # unit-test roster golden
make -C /workspaces/CryoLang examples
make -C /workspaces/CryoLang selfhost-check  # BOTH OS, ~17 min
python3 scripts/verify-pin.py --require-clean
```

| env var | emits |
|---|---|
| `CRYO_RESOLVE_COUNTER=1` | the cascade bucket report |
| `CRYO_SCOPE_PROBE=1` | `SCOPE-DIVERGE` + ambient-cursor rows (**see §2.2**) |
| `CRYO_VIS_AUDIT=1` | `VIS-VIOLATION`, `VIS-RECORD`, `VIS-PRIVATE`, `FNBIND-VIOLATION` |
| `CRYO_LEAF_AUDIT=1` | `LEAF-HIT` |
| `CRYO_PATH_AUDIT=1` | `PATH-HIT`, `PATH-SCOPE`, `PATH-PRELUDE` |

Environment gotchas that will cost you an hour each:

- Always `make -C <root>` and `cd <abs path> && …`; bash cwd persists between
  calls inconsistently.
- **Never edit sources while `make test` or `selfhost-check` runs** — both
  rebuild from `$(CRYO_SOURCES)`. Docs and `scripts/` are safe.
- `selfhost-check` wipes `stdlib/.bin` and `runtime/.bin` and leaves **Windows**
  objects in the flat shared `runtime/.bin`; the next Linux build then fails at
  *link* with `R_AMD64_IMAGEBASE with __ImageBase undefined`. Fix:
  `make -C /workspaces/CryoLang stdlib runtime-tiers`.
- A scratchpad project cannot find stdlib — set `CRYO_STDLIB`.
  `compiler/build/cryo` cannot find it from another directory; `bin/cryo` can.
- cryoconfig keys are `project_name` / `entry_point` / `source_dir`.
- A single-file build never reaches the prelude-seeding path — probe with a real
  project that has a `cryoconfig`.
- `.gitignore` has a repo-wide `*.txt`; a new golden needs an explicit negation
  or it is never committed and CI fails on a fresh clone.
- Basic `grep` has no `\t` — use `grep -P` for the tab-separated audit streams.
- Cryo has no `else if` in an if-**expression** (statements are fine).
- Copy the compiler out of the build tree before running it as an instrument, or
  it will hit `ETXTBSY` overwriting itself.

---

## 8. One last thing

If the honest answer is "this is sound", say so plainly and show the probes that
could have caught it and didn't. If the honest answer is "this works but it is
not a proper implementation", say that plainly too — that is the outcome this
audit exists to catch, and it is the more useful finding of the two.
