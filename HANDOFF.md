# Cryo — handoff (`new-stdlib`, mid-session, switching PCs)

**Branch:** `new-stdlib`
**Date:** 2026-05-01
**Goal:** Land `experimental/stdlib-next/` so it produces a clean `libcryo.a`.

> Replaces the prior handoff that was committed alongside `b5564548`. That
> file got deleted from the working tree during this session. The state
> described in the previous handoff (DefaultExpansion pass + 2 stdlib-next
> errors traced to a bootstrap heap-aliasing bug) has now been
> diagnosed AND partially fixed — see "Progress this session" below.

---

## TL;DR — where you are

- **HEAD = `11851422`** (no new commits this session). Working tree dirty
  on 9 files; the prior handoff got deleted and is being replaced by this
  document.
- The 2 original stdlib-next sema errors are **gone**. The fix landed
  three architectural changes (pre_resolved on NamedAnnotation,
  template-name keying for trait-impl bounds, recursive bounds checking).
- stdlib-next now fails at **codegen** with 4 errors (`alloc_entry`,
  `as_str`, `Slice::from_raw`, LLVM verify). Sema is clean for the whole
  tree.
- **Regression**: legacy `stdlib/` builds dirty too (4 `(String, String)`
  vs `(i32, i32)` errors) — diagnosed but not fixed. **Bridge sandbox
  tests still pass (5/5).** `make selfhost-check` fails at stage 3
  because of the legacy stdlib regression.
- **Do not commit yet.** The legacy stdlib regression must be cleared
  before commit.

```bash
git status                       # 9 modified files; HANDOFF.md being recreated
git log --oneline -3
# 11851422 sema, default_expansion: remove session-debug printfs
# b5564548 Implement Default-Expansion Pass for generic type resolution
# 6d0ad2fe sema: implement default trait-method synthesis ...
```

Quick health check:

```bash
make cryo-fast                                # ~30s, uses pinned bin/cryo
cd stdlib && /…/cryo build                     # FAILS — 4 errors (regression)
cd experimental/stdlib-next && /…/cryo build   # FAILS — 4 codegen errors
cd compiler/sandbox && for f in bridge/*.cryo; do …; done  # 5/5 OK
```

---

## Operating rules (carry-over — please honor)

1. **No workarounds. No shortcuts. Find and fix root causes.**
2. **Don't auto-commit.** User commits at their cadence.
3. **`make selfhost-check` ~3min** — run after a batch, not after every edit.
4. **Plumb every visitor when AST shape changes** — cloner, substituter,
   dumper, visitor, specializer.
5. **`compiler/src/` must keep parsing under both `legacy/bootstrap/bin/cryo`
   AND the pinned `bin/cryo`.** No new syntax in compiler source.
6. **Compile through PIN for `cryo-fast`, but selfhost-check uses
   bootstrap for stage-2.** A change that PIN handles fine can still
   miscompile under bootstrap (this is exactly what's biting us now).

---

## How to drive

```bash
# Build cryoc (~30s, uses pinned bin/cryo at $REPO_ROOT/bin/cryo):
make cryo-fast

# Run a single .cryo file (need build/obj/ in cwd first):
mkdir -p build/obj && ./compiler/build/bin/cryo raw path/to/file.cryo

# Bridge sandbox tests (must stay green):
mkdir -p build/obj
for f in compiler/sandbox/bridge/*.cryo; do
  printf "%-55s " "$(basename $f):"
  timeout 15 ./compiler/build/bin/cryo raw "$f" 2>&1 |
    grep -q "Compilation succeeded" && echo OK || echo FAIL
done

# stdlib-next full build:
cd experimental/stdlib-next
/home/phock/Programming/apps/CryoLang/compiler/build/bin/cryo build 2>&1 | tail -20

# Legacy stdlib (regression gate — must stay green):
cd stdlib
/home/phock/Programming/apps/CryoLang/compiler/build/bin/cryo build 2>&1 | tail -3

# Full selfhost gate (~3 min):
make selfhost-check
```

When you build with PIN, the resulting binary lands at
`compiler/build/bin/cryo`. When `make cryo-fast` reports success, that
binary is your stage-3-equivalent. `make selfhost-check` is the *only*
target that exercises the bootstrap-built stage-2; it will catch
regressions PIN hides.

---

## Progress this session

### Original 2 errors → state

```
error[E0045]: call to undefined function 'op'           ← FIXED
error[E0091]: no method 'insert' found on HashMap<…>    ← FIXED
```

### Current stdlib-next state — 4 codegen errors (sema clean)

```
error[E0147]: codegen: cannot resolve function 'alloc_entry'
error[E0167]: LLVM module verification failed for std::core::intrinsics
error[E0147]: codegen: no method 'as_str' found on type 'std::collections::string::String'
error[E0147]: codegen: cannot resolve 'Slice::from_raw'
```

These are all **codegen** lookups (E0147 / E0167), not sema. Likely the
codegen has its own scope-resolution pass that doesn't honor the same
spec/template name keying we fixed in sema. Same family of issues —
codegen can't find spec'd names in its DI tables.

### Current legacy stdlib regression — 4 `(String, String)` vs `(i32, i32)` errors

```
error[E0043]: cannot return value of type '(String, String)'
              from function returning '(i32, i32)'
  (×4, all in core/option.cryo's unwrap-family methods)
```

This regression is from this session's changes (see "Diagnosis of the
regression" below). The bridge sandbox tests all pass through PIN, so
the regression is specific to how Option<T>'s body is type-checked when
multiple specs exist.

### Diagnosis chain (proven via probes)

The 2 original errors had a single chained root cause:

1. **`op` undefined**: `Result::unwrap_or_else`'s `op: (E) -> T` parameter was
   resolved to `FunctionType` id=1476 (kind=11), then later — between
   `arena.get_function` returning and the next `alloc_type` — the kind
   field of the in-arena Type at that pointer flipped to 0 (Void).
   - Probes pinpointed the corruption to inside
     `name_resolver.resolve_type_qualified_name(name)` for a 161-char
     mangled spec name like `std::collections::array::5Array$LN$L…$G$G`.
   - Bootstrap-cryoc's heap allocator aliases something in the long-string
     processing path (string slicing / interning), overwriting unrelated
     arena slots' first 4 bytes with zero.
   - Fix landed (architectural, not a bootstrap patch): see
     "pre_resolved on NamedAnnotation".
2. **HashMap `insert` not found**: After fixing #1, this remained. Probes
   showed the spec'd HashMap struct registered correctly in the DI under
   its long mangled qname AND `insert/get/remove` registered with
   `resolved_return_type.id=0` (invalid). The reason: `resolve_func_signature`
   wasn't running for those methods because `method_bounds_satisfied`
   returned false (where `K: Hash + Eq` for `K = String<GlobalAlloc>` was
   reading "the spec's leaf name" instead of the template's bare name).
   - Trait impls are registered by parser as `(Hash, "String")` — the
     template's bare name. Spec'd type's bare leaf was the long mangled
     spec name. Lookup missed.
   - Fix: `bare_name_of` for `InstantiatedType` now recurses on
     `generic_base` instead of unwrapping through `resolved_type` →
     returns "String" for a `String<GA>` spec. (`monomorphizer.cryo`)

After those two fixes, two new errors surfaced:

3. **`Option<String<GA>>` type-mismatch (LHS vs RHS same display string)**:
   probes showed two distinct InstantiatedTypes for the same logical type
   (id=337 args=[263] vs id=1150 args=[1052]) where 263 was an
   InstantiatedType for `String<GA>` and 1052 was the spec'd struct
   directly. My `resolved_arg_typeref` was unwrapping through
   `InstantiatedType.resolved_type`, while the natural resolve_generic
   path leaves the InstantiatedType wrapper in place. Asymmetry.
   - Fix: `resolved_arg_typeref` returns `subst.replacements[i]` as-is,
     no unwrapping. (`substituter.cryo`)
4. **`equals` not found on `String<GA>` spec**: probes showed `String`'s
   `implement trait Eq for struct String { equals }` impl block had its
   `methods.length=0` after specialization. Cause: the specializer's
   `method_has_modified_self_type` was filtering methods whose `&this`
   annotation became `&String<GlobalAlloc>` after DefaultExpansion. The
   filter checked "args match bare outer params" only — not "args match
   the spec's concrete args".
   - Fix: filter now also accepts `Named(N)` matching either the spec's
     `type_arg_displays[i]` whole string or its leaf segment.
     (`specializer.cryo`)

Then a third new error surfaced — `equals` on `Option<ExitStatus>` —
because `bounds_satisfied` was shallow: it checks "Option has Eq impl"
but not "Option<ExitStatus>: Eq" (which would require ExitStatus: Eq).
- Fix: recursive `type_implements_trait` walks impl blocks' where-bounds
  with the InstantiatedType's args bound to the impl's generic params.
  (`monomorphizer.cryo`)

After all four fixes, sema passes for stdlib-next entirely. 4 codegen
errors remain, separate concern.

### What got reverted in-session (so the diff stays clean)

- `arena.create_instantiation` linear-scan dedup — caused worse
  regressions in legacy stdlib. Reverted; the existing
  `instantiated_cache` (HashMap<string, V>, leaks dups due to bootstrap
  pointer-compare bug) is back in place and we tolerate the duplication
  for now. Whether that's actually what's biting us is unclear; see
  "Diagnosis of the regression" below.
- A bunch of probe printfs across `arena.cryo`, `decl_index.cryo`,
  `monomorphizer.cryo`, `resolver.cryo`, `sema.cryo`, `specializer.cryo`,
  `substituter.cryo` — all removed before writing this handoff.

---

## Diagnosis of the regression (legacy stdlib)

**Symptom (legacy `stdlib/`, fails 4× in `core/option.cryo`):**

```
error[E0043]: cannot return value of type '(String, String)'
              from function returning '(i32, i32)'
  ./core/option.cryo:59:38      Option::Some(value) => { return value; }
```

The error fires on `unwrap`, `expect`, `unwrap_or`, `unwrap_or_else`.
All four bodies are `match (&this) { Option::Some(value) => { return
value; } … }`. The function returns `T`, so `value: T` should match.

**Hypothesis (not yet verified):** the spec'd Option<(i32, i32)>'s
match-arm binding `value` is getting its resolved_type from a
DIFFERENT spec (Option<(String, String)>) — i.e., pattern bindings or
the variant-payload TypeRef cache is shared across specs.

Why this newly fires: my `pre_resolved` field on NamedAnnotation
short-circuits the resolver. If two spec's cloned ASTs end up sharing
a NamedAnnotation pointer (or if pattern bindings share resolved_type
via a back-channel), the second spec to write pre_resolved would
overwrite the first's. The first spec's body would then resolve T to
the second spec's type.

**What I confirmed:**

- `TypeAnnotation::clone()` for Named *does* allocate a fresh
  NamedAnnotation and copy `pre_resolved` from the source.
- `cloner.clone_type_annotation` delegates to `TypeAnnotation::clone_ptr`,
  which calls `(*ann).clone()` — same path.
- EnumVariantNode cloning calls `clone_type_annotation` for each
  associated_type — so variant payload annotations are deep-cloned.
- BUT: when the substituter writes `named.name = …; named.pre_resolved
  = …` it's writing into the cloned annotation's storage. If that
  annotation slot is somehow shared (e.g., via a cached tuple
  annotation), the write would leak between specs.

**What I have NOT verified:**

- Whether MatchArmNode / PatternElement / EnumPattern bindings get
  deep-cloned (separate path from type annotations).
- Whether a tuple annotation `(T, T)` becomes a TupleAnnotation that
  wraps a single shared inner Named("T"), in which case the substituter
  writes pre_resolved twice for two different T arguments and second
  write wins.
- Whether the `instantiated_cache` is actually returning shared
  TypeRefs across specs (see "Reverted linear scan" above).

**Recommended next step:** before pushing further, confirm whether the
PIN-built `cryo-fast` binary regresses on legacy stdlib. Earlier in the
session I observed legacy stdlib passing under PIN with all my fixes
applied; later in the session the same binary failed. I never
investigated which intermediate change flipped that — that's the
fastest path to the answer.

Concrete steps:

1. `git stash` — verify legacy stdlib is clean on HEAD without changes.
2. Apply the changes one at a time (NamedAnnotation field only,
   substituter only, resolver only, …) and test legacy stdlib after
   each.
3. The first change that flips legacy stdlib to red is the regression
   trigger.

The bootstrap C++ cryoc has known struct-layout / sizeof bugs (see
`feedback_cloner_vtable.md`); adding a field to a heavily-cloned
struct is the kind of change that can trip those.

---

## Architectural changes that landed (uncommitted, working)

### 1. `pre_resolved: TypeRef` on `NamedAnnotation`

The substituter already knows the concrete TypeRef for each generic
param substitution (`subst.replacements[i]`). Previously it set only
`named.name = type_arg_displays[i]` — discarding the TypeRef and forcing
the resolver to re-derive it from a long mangled spec name. That round
trip:
- Routed through `resolve_type_qualified_name` → `extract_leaf` →
  `resolve_type_qualified_name_bare`, all of which build temporary
  strings.
- Triggered the bootstrap heap-aliasing bug for sufficiently long
  names.

The fix: attach `pre_resolved: TypeRef` to NamedAnnotation. The resolver
short-circuits to it when valid. Architecturally cleaner regardless of
the bug — don't throw away information you have.

**Files:**

- `compiler/src/compiler/AST/_module.cryo` — added field, updated clone.
- `compiler/src/compiler/AST/substituter.cryo` — sets pre_resolved in
  the user-defined branch + new `resolved_arg_typeref` helper.
- `compiler/src/compiler/types/resolver.cryo` — short-circuit at the
  match arm.
- All `NamedAnnotation { … }` constructors plumbed with
  `pre_resolved: TypeRef::invalid()` initializer:
  - `compiler/src/compiler/parser/parser.cryo`
  - `compiler/src/compiler/parser/expr_parser.cryo`
  - `compiler/src/compiler/passes/default_expansion.cryo`
  - `compiler/src/compiler/AST/substituter.cryo` (the rewrite_to_*
    helpers)

### 2. Template-keyed bounds in `bare_name_of`

For an InstantiatedType, `bare_name_of` now recurses on `generic_base`
instead of unwrapping through `resolved_type`. So `String<GlobalAlloc>`
keys against `"String"` (matches `implement trait Eq for struct
String`) instead of the spec's mangled leaf name.

**Files:** `compiler/src/compiler/types/monomorphizer.cryo`.

### 3. Spec-aware modified-self filter

The specializer's `method_has_modified_self_type` filter previously
treated any `&this: &String<GlobalAlloc>` as a "modified" self-type
(because the args don't match the bare param `T`). After
DefaultExpansion expands bare `String` references in impl blocks to
`String<GlobalAlloc>`, this filter would drop *every* method whose
self-type touched the defaulted form — including inherent methods like
`as_str` and trait methods like `equals` / `hash`.

The filter now also accepts `Named(N)` where N matches the spec's
`type_arg_displays[i]` (whole string or leaf segment).

**Files:** `compiler/src/compiler/AST/specializer.cryo` —
`method_has_modified_self_type`, `annotation_is_modified_outer`, new
`annotation_matches_param_or_spec`. All three now take
`type_arg_displays: string[]`.

### 4. Recursive `type_implements_trait`

`bounds_satisfied` was checking the trait-impl table at one level: for
`T: Eq` with `T = Option<X>`, it confirmed `(Eq, "Option")` exists in
the registry and stopped. But `Option`'s Eq impl has its own
`where T: Eq` clause; if `X: Eq` is false, `Option<X>: Eq` is false too.

The fix: `type_implements_trait` recursively checks the impl block's
own where-bounds with the InstantiatedType's args bound to the impl's
generic params. Depth-bounded at 16 to guard cyclic chains; depleted
depth conservatively returns false.

**Files:** `compiler/src/compiler/types/monomorphizer.cryo`.

---

## stdlib-next codegen errors (next-up)

```
error[E0147]: codegen: cannot resolve function 'alloc_entry'
error[E0167]: LLVM module verification failed for std::core::intrinsics
error[E0147]: codegen: no method 'as_str' found on type 'std::collections::string::String'
error[E0147]: codegen: cannot resolve 'Slice::from_raw'
```

All E0147 are emitted from
`compiler/src/compiler/codegen/ir_generator.cryo:2146`. The codegen has
its own member/scope resolution pass that walks the DI tables. These
errors look like the same family that sema had before this session's
fixes — the codegen lookup uses different keys than registration.

Worth checking:

1. For `as_str`: probably the codegen sees a value of type
   `String<GlobalAlloc>` (the spec'd struct) but looks up `as_str` under
   the bare `String` template name, not the spec's qualified name. Mirror
   of the sema-side issue I fixed for inherent methods.
2. For `Slice::from_raw`: scope-resolution likely needs the same
   spec→template mapping.
3. For `alloc_entry`: probably a free function inside a generic that's
   only emitted when its caller is monomorphized.
4. The LLVM verify failure likely cascades from the resolution gaps
   above — fix those first.

---

## Pre-commit checklist (when you eventually do commit)

- [ ] Legacy `stdlib/` regression cleared. **Do not commit until this
      passes again.**
- [ ] `make cryo-fast` clean rebuild (~30s).
- [ ] Bridge sandbox tests pass (5/5 OK).
- [ ] Legacy stdlib builds.
- [ ] stdlib-next sema-clean (codegen errors are OK to leave for a
      follow-up commit, but document them).
- [ ] `make selfhost-check` — must produce stage-4 == stage-5 byte
      identity. Don't run while editing source.

---

## Background context

### Phase 4b ordering

Per-module:
1. **DefaultExpansion** — rewrites annotations whose Named base is a
   registered all-default-generic template (currently `String`,
   `PathBuf`, `CString` in stdlib-next).
2. **FunctionSignature** — resolves rewritten annotations.
3. **TemplateRegistration** — registers this module's templates so the
   NEXT module's DefaultExpansion can find them.

### Files modified across this session

```
compiler/src/compiler/AST/_module.cryo            (NamedAnnotation field)
compiler/src/compiler/AST/specializer.cryo        (modified-self filter)
compiler/src/compiler/AST/substituter.cryo       (pre_resolved + helper)
compiler/src/compiler/parser/expr_parser.cryo     (init pre_resolved)
compiler/src/compiler/parser/parser.cryo          (init pre_resolved)
compiler/src/compiler/passes/default_expansion.cryo (init pre_resolved + import)
compiler/src/compiler/passes/sema.cryo            (whitespace only — leftover)
compiler/src/compiler/types/monomorphizer.cryo    (bare_name_of + recursive bounds)
compiler/src/compiler/types/resolver.cryo         (pre_resolved short-circuit)
```

### Key memory entries (in
`~/.claude/projects/-home-phock-Programming-apps-CryoLang/memory/`)

- `project_default_expansion_corruption.md` — the prior session's
  diagnosis; superseded by this handoff.
- `project_mangling_v0_2.md` — spec name mangling.
- `project_pipeline_phases.md` — Phase 6a/6b ordering.
- `project_hashmap_string_bug.md` — Bootstrap HashMap<string,V>
  pointer-compare bug. Touches the duplicate-InstantiatedType issue we
  saw briefly this session.
- `feedback_cloner_vtable.md` — bootstrap class-method codegen bugs
  that bite when adding fields or new methods.
- `feedback_no_milestone_scripts.md`, `feedback_test_whole_stdlib.md` —
  test discipline.
- `feedback_codegen_style.md`, `feedback_bridge_quality.md` — quality
  bar; "no hacky workarounds".

---

## Just before you commit

- `git diff HEAD` — sanity-check, only the change you intended.
- `make cryo-fast` — clean rebuild.
- Bridge sandbox tests + legacy stdlib + stdlib-next.
- `make selfhost-check` — ~3 minutes. Stage 2 builds via bootstrap, so
  it catches changes that PIN can compile but bootstrap can't (which is
  exactly what's biting the legacy stdlib regression — see "Diagnosis
  of the regression").

**The user does the actual `git commit`.** Surface the diff and a
proposed message; don't commit yourself unless explicitly asked.

Good luck.
