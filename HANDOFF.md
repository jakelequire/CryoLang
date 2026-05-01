# Cryo — handoff (DefaultExpansion pass mid-debug)

**Branch:** `new-stdlib`
**Date:** 2026-04-30 evening → 2026-05-01 (passing midnight, switching PCs).
**Goal:** Land `experimental/stdlib-next/` so it produces a clean `libcryo.a`.

> Previous handoff was committed alongside that session's work. This file
> picks up where the user is mid-task on the **DefaultExpansion** pass —
> a new compiler pass to canonicalize bare references to generic types
> with all-default type parameters.

---

## TL;DR — where you are

- **Committed (HEAD = `6d0ad2fe`):** B fix (spec name lookup) + B.6
  default trait-method synthesis. **stdlib-next is at 5 errors**, down
  from 9. Selfhost-check passes. Bridge tests + legacy stdlib green.
- **Uncommitted (working tree):** new `DefaultExpansion` pass — partial.
  Brings stdlib-next from 5 → **2 errors**, but the 2 remaining (`op`
  undefined function + `HashMap insert` not found) appear to be caused
  by the new pass and need diagnosing.
- **Hot debug printfs in `compiler/src/compiler/passes/sema.cryo`** —
  added during diagnosis, must be cleaned up before commit.

```bash
git status                       # 4 modified + 1 new file (default_expansion.cryo)
git diff --stat HEAD             # ~74 lines across 4 files
ls compiler/src/compiler/passes/default_expansion.cryo  # 765 lines, untracked
```

---

## Operating rules (the user reinforces — please honor)

1. **No workarounds. No shortcuts. Find and fix root causes.**
2. **Don't auto-commit.** User commits at their cadence.
3. **`make selfhost-check` ~7 min** — run after a batch, not after every edit.
   Do **not** run it concurrently with edits — it stages source for each
   compiler stage and will catch in-progress edits, leading to spurious
   failures.
4. **Plumb every visitor when AST shape changes** — cloner, substituter,
   dumper, visitor, specializer.
5. **`compiler/src/` must keep parsing under both `legacy/bootstrap/bin/cryo`
   AND the pinned `bin/cryo`.** No new syntax in compiler source.

---

## How to drive

```bash
# Build cryoc (~30s, uses pinned bin/cryo):
make cryo-fast

# Run a single .cryo file:
./compiler/build/bin/cryo raw path/to/file.cryo

# Sandbox tests (must stay green):
cd /workspaces/CryoLang
for f in compiler/sandbox/bridge/*.cryo; do
  printf "%-55s " "$(basename $f):"
  timeout 8 ./compiler/build/bin/cryo raw "$f" 2>&1 |
    grep -q "Compilation succeeded" && echo OK || echo FAIL
done

# stdlib-next full build (~1-2 min):
cd /workspaces/CryoLang/experimental/stdlib-next
/workspaces/CryoLang/compiler/build/bin/cryo build 2>&1 | tail -50

# Legacy stdlib (regression gate — must stay green):
cd /workspaces/CryoLang/stdlib
/workspaces/CryoLang/compiler/build/bin/cryo build 2>&1 | tail -3
# expect: "Project compilation succeeded." with libcryo.a

# Full selfhost gate (~7 min):
make selfhost-check    # NOTE: runs FROM repo root; cwd matters
```

---

## What's committed (in HEAD `6d0ad2fe`)

### B fix — spec name lookup mapping

**File:** `compiler/src/compiler/passes/specialization.cryo` (15 lines).
**Effect:** stdlib-next 9 → 7 errors (knocked out the 2 `try_with_capacity_in`
errors).

In `SpecInjector::register`, after registering the spec'd type under both
local and arena qualified names, also register a bare→qualified name
mapping: `register_name_mapping(spec_name, arena_qname)`. Without this,
in-body calls inside a spec'd method body — substituted to use the
bare-form spec scope (`Array_<spec>::method`) — couldn't be resolved at
codegen time because methods are registered under the qualified arena
name.

### B.6 default trait-method synthesis

**Files:**
- `compiler/src/compiler/types/generic_registry.cryo` (+40) —
  `trait_decls` index, `register_trait_decl` / `get_trait_decl`.
- `compiler/src/compiler/passes/type_resolution.cryo` (+128) —
  `synthesize_default_trait_methods` static helper, called from the
  ImplementationBlock branch of FunctionSignature pass.
- `compiler/src/compiler/AST/cloner.cryo` (+13) —
  `ASTCloner::new_plain()` static constructor.

**Effect:** stdlib-next 7 → 5 errors (knocked out the 3 default-method
errors: `read_line` on Stdin, `write_all` on Stderr/File).

**How it works:** When `implement trait T for X { ... }` is processed,
the helper looks up T in the new `trait_decls` registry, walks T's methods,
and clones every default-bodied method that the impl doesn't override
into the impl's `methods` array using `ASTCloner::new_plain()`. The
clones flow through the rest of the pipeline as if they were
user-written. `This` resolves to the impl target via the existing B.1
path.

**Cryoc bug worked around:** Value-constructing a `type class` at
function scope (`mut x: ASTCloner = ASTCloner();`) miscompiles and
segfaults far away from the construction site. Routed through a static
`new_plain()` to dodge it.

---

## What's uncommitted — `DefaultExpansion` pass

A new compiler pass that walks the AST and rewrites bare references to
generic-with-all-defaults types into their explicit `Generic(Named, [defaults])`
form. The motivation: bare `String` (declared `String<A = GlobalAlloc>`)
should resolve to the same TypeRef as explicit `String<GlobalAlloc>` —
otherwise mono cache keys, name mangling, and method tables diverge.

### Why a new pass and not a resolver-side fix

A previous attempt expanded inside `resolve_named` and broke type-id
consistency: some paths went through expansion, others didn't, producing
two TypeRef IDs for the same logical type. The mono cache then created
two specs with different mangled names, and methods registered against
one couldn't be found via the other. **Fixing this at the AST level
(rewriting annotations once, before resolution) means every downstream
consumer sees the same shape.**

### Files touched

| file | change |
|---|---|
| `compiler/src/compiler/passes/pass_id.cryo` | New `PassID::DefaultExpansion`, new `Provision::DefaultsExpanded`, metadata block, order=10 (others bumped). Comment "23 provisions fit in u32" → "Up to 32". |
| `compiler/src/compiler/passes/pass_registry.cryo` | Import `Compiler::Passes::DefaultExpansion`; add `PassID::DefaultExpansion` to all 4 build_*_pipeline functions; dispatch in `run_pass`. |
| `compiler/src/compiler/passes/default_expansion.cryo` | **NEW** — 765 lines. The pass itself. |
| `compiler/src/compiler/instance.cryo` | Phase 4b reordered: `[DefaultExpansion, FunctionSignature, TemplateRegistration]` — pass runs FIRST per-module so FuncSig sees the rewritten annotations. Cross-module templates are visible because earlier modules' TemplateRegistration already populated the shared GenericRegistry. |
| `compiler/src/compiler/passes/sema.cryo` | **DEBUG PRINTFS — must clean up before commit:** `enter_function` logs func name + each param's name and resolved-type validity; `resolve_direct_call` logs the failure path with locals count and `this_type.id`; the function-pointer-call branch logs `local_t.kind`. |

### Pass design (in `default_expansion.cryo`)

- **Step 1 — Collect candidates.** Walk this module's top-level
  decls (`collect_local_defaults`) AND iterate the shared
  `GenericRegistry.entries` (`collect_cross_module_defaults`). Register
  any struct/class/enum whose every generic param has a non-null
  `default_annotation`. Each registered entry stores deep-cloned
  default annotations so each rewrite site can clone freely.

- **Step 2 — Walk the AST and rewrite.**
  `rewrite_stmt` → `rewrite_function_decl` → `rewrite_block` →
  `rewrite_body_stmt` → `rewrite_expr` → `rewrite_annotation_in_place`.
  No visitor pattern (cryoc visitor bugs); explicit walks per kind.

  A `ScopeStack` of currently-active "template names whose params are
  bound" is pushed/popped as we enter/leave generic decl bodies, impl
  block param lists, and method's own `<T>` lists. `rewrite_annotation_in_place`
  on `Named(name)` skips when `scope.contains(name)` (we're inside
  that template's body — bare `String` should keep meaning the bound
  template, not `String<GlobalAlloc>`).

- **The actual rewrite:** `Named(name)` becomes
  `Generic(Named(name), [clone(defaults)...])` by mutating the
  `*ann` enum tag through its pointer. Recursion continues into the
  newly-created Generic args (in case those are also expansion
  candidates).

### Templates currently being expanded

By design, only types where **every** param has a default qualify. In
stdlib-next that's exactly:

- `String<A = GlobalAlloc>`
- `PathBuf<A = GlobalAlloc>`
- `CString<A = GlobalAlloc>`

Types like `Array<T, A = GlobalAlloc>` and `HashMap<K, V, A = GlobalAlloc>`
have non-default leading params and **don't** qualify. Bare `Array` is
not a meaningful type without `T`, so this is correct.

---

## State of the build with the uncommitted pass

### With body walking enabled (current state of working tree)

```
exit=1, 2 errors:
error[E0045]: call to undefined function 'op' (in ./core/intrinsics.cryo)
error[E0091]: no method 'insert' found on type
  'std::collections::hash_map::HashMap<String<GlobalAlloc>, String<GlobalAlloc>, GlobalAlloc>'
  (in ./net/http/headers.cryo)
```

### With body walking commented out (sanity-check earlier in the session)

```
exit=1, 5 errors:
- 'op' undefined (same as above)
- 2× Array<String, GlobalAlloc> vs Array<String<GlobalAlloc>, GlobalAlloc> mismatch
- HashMap insert (same)
- Option<String> vs Option<String<GlobalAlloc>> mismatch
```

So body-walking fixed the type-mismatch errors (3 of them) by ensuring
local declarations inside function bodies also get rewritten. The `op`
and `HashMap insert` errors persist regardless and **need diagnosis**.

### What's *gone* compared to the committed (5-error) state

- `as_str` on String — fixed (String annotation expanded)
- `as_str` on `&60` — fixed
- `Slice::from_raw` — fixed (cloned default body's calls now resolve)
- `alloc_entry` — fixed (no longer surfaces; suspected to be unblocked
  by the type-system consistency this pass provides)
- LLVM verify on `std::core::intrinsics` — fixed (suspected same reason
  as alloc_entry)

### What's *new* / blocking

#### Error 1: `call to undefined function 'op'`

Surface site (per debug printfs):

```
[sema::enter_function] 'unwrap_or_else' params=2 src='./core/intrinsics.cryo'
  param[0] name='&this' resolved_valid=1
  param[1] name='op' resolved_valid=1
[sema::resolve_direct_call] FAIL name='op' src='./core/intrinsics.cryo'
  locals=4 this_type.id=1474
```

`op` IS registered as a local with valid resolved_type. So
`lookup_local("op")` should return a valid TypeRef. The check that
follows is:

```cryo
if (local_t != null && local_t.kind == TypeKind::Function) {
    return fn_t.return_type;
}
```

If this fails, control falls through to the "undefined function" error.
**The next debug step (interrupted)** was adding a printf to log
`local_t.kind` in this branch. Without that data, the hypothesis is:
`op`'s resolved_type is something OTHER than `TypeKind::Function`
(e.g., wrapped in InstantiatedType or a Reference, or possibly the
substituted-but-not-yet-resolved form).

The body of `unwrap_or_else` — `Result::Err(err) => { return op(err); }`
— ends up in a spec'd Result method that gets injected into
`./core/intrinsics.cryo`'s AST (because intrinsics uses Result somewhere).
Sema processes it there.

**Suspect:** my pass walks Result's template body (Result has
`<T, E>`, no defaults — not in the registry). The annotation `(E) -> T`
goes through `rewrite_annotation_in_place` on `TypeAnnotation::Function`,
which recurses into the Named("E") and Named("T") nodes. Both should
no-op because E and T aren't in the registry. **But somehow the
parameter's resolved type ends up not being `TypeKind::Function` after
specialization.** Worth instrumenting the `op` parameter's resolution
in detail.

The same error class has a sibling: `call to undefined function 'default_fn'`
on Option's `unwrap_or_else(&this, default_fn: () -> T)` — same shape.

#### Error 2: `no method 'insert' found on type HashMap<...>`

Receiver type's mangled name shows `String<GlobalAlloc>` (the spec
form) inside HashMap's args — confirming my pass rewrote the
annotation. `cross='std::collections::hash_map::HashMap_<spec>'` — the
qualified spec name. But `lookup_method_return(spec_qname, "insert")`
returns invalid.

**Hypothesis:** The HashMap spec's methods are registered under one
qualified name, sema looks them up under a slightly different one.
Mangling consistency between SpecInjector::register and the call site's
type lookup needs verification. One signal: the `qualified=` form in
the diagnostic shows double-namespace (`std::net::http::headers::std::collections::hash_map::...`)
— that's `qualify_symbol_sym` adding the consuming module's prefix to
an already-qualified name. This may or may not be the failing key.

A `specialize_request` log line appears for HashMap with the right type
arg ids, so the spec WAS created. Question is whether its methods were
successfully registered against the spec's qualified name.

---

## Pre-commit cleanup (BEFORE running selfhost-check)

The `sema.cryo` debug printfs MUST be removed:

1. **`enter_function`** (around the original line 494):
   ```cryo
   printf("[sema::enter_function] '%s' params=%lld src='%s'\n", ...);
   for (mut i: i64 = 0; i < func.parameters.length; i++) {
       ...
       printf("  param[%lld] name='%s' resolved_valid=%d\n", ...);
       ...
   ```
   Revert to original (no printfs).

2. **`resolve_direct_call`** (around the original line 1527 — error emit):
   ```cryo
   printf("[sema::resolve_direct_call] FAIL name='%s' src='%s' locals=%lld this_type.id=%llu\n", ...);
   ```
   Revert.

3. **`resolve_direct_call`** function-pointer branch (added but the user
   rejected the rebuild before it was tested):
   ```cryo
   printf("[sema::resolve_direct_call] name='%s' local_type.id=%llu local_t.kind=?\n", ...);
   if (local_t == null) { printf("  local_t == null\n"); }
   else { printf("  local_t.kind=%s\n", local_t.kind.to_string()); }
   ```
   This last set of printfs WAS WRITTEN to disk via Edit but the
   subsequent `make cryo-fast` run was cancelled — the binary wasn't
   rebuilt with them. The file changes are still there to clean up.

`git diff HEAD compiler/src/compiler/passes/sema.cryo` will show all of
them.

---

## Recommended next moves (for the next agent)

1. **Clean up sema.cryo printfs.** Revert `enter_function` and
   `resolve_direct_call` to the pre-debug state. Diff should be empty
   for that file after.

2. **Diagnose the `op` error.** The next debug step that was about to
   run: log `local_t.kind` in the function-pointer-call branch of
   `resolve_direct_call`. If it's not `Function`, walk back to where
   `op`'s `resolved_type` was set (likely in the substituter or
   `resolve_func_signature`) and find what kind it actually is. Likely
   answer: `InstantiatedType` (where the wrapper hasn't been unwrapped
   to its resolved Function form), or something else exotic.

3. **Diagnose the `HashMap insert` error.** Add a printf in
   `decl_index.lookup_method_return` to log the SymbolStr being looked
   up and whether it matches what `SpecInjector::register` registered
   for HashMap's spec. The double-namespace `qualified=` form in the
   diagnostic is a clue — `sema::lookup_method_return` may be qualifying
   an already-qualified name.

4. **Once both errors are resolved**, run:
   - bridge tests (`for f in compiler/sandbox/bridge/*.cryo; do ...`)
   - legacy stdlib build
   - `make selfhost-check` — must produce stage-4 == stage-5 byte
     identity. **Run from repo root, NOT from a subdirectory.**

5. **Address `alloc_entry` and `LLVM verify intrinsics`** — these
   appeared to vanish with the DefaultExpansion pass enabled. Confirm
   they actually stay fixed and aren't just being masked by earlier
   pipeline failures. Hint: re-check after fixing errors 1 and 2 above.

---

## Background context (carry-over from earlier handoffs)

### Templates with all-default generic params (in stdlib-next)

```
String<A = GlobalAlloc>
PathBuf<A = GlobalAlloc>
CString<A = GlobalAlloc>
```

### Phase 4b ordering (with the new pass)

Per-module:
1. `DefaultExpansion` (NEW) — rewrites annotations, reads
   GenericRegistry for cross-module templates and the local AST for
   this-module templates.
2. `FunctionSignature` — resolves rewritten annotations; sees explicit
   `Generic(Named, [defaults])` shape from step 1.
3. `TemplateRegistration` — registers this module's templates so the
   NEXT module's DefaultExpansion can find them.

### Critical AST fields the pass walks (full list in
`default_expansion.cryo`)

- `StructDeclNode`: fields, methods, generic_params (push scope)
- `ClassDeclNode`: same
- `EnumDeclNode`: variants[i].associated_types
- `TraitDeclNode`: methods, generic_params (push)
- `ImplBlockNode`: methods, generic_params (push)
- `TypeAliasDeclNode`: target_annotation
- `FunctionDeclNode`: parameters, return_type_annotation, body
- `BlockStmtNode` and every statement kind
- All expression kinds with embedded annotations: Cast, Sizeof, Alignof,
  StructLiteral.generic_args, NewExpr.generic_args + struct_init,
  Call.generic_args, ScopeResolution.generic_args + scope_generic_args

### The cryoc bug noted in the B.6 work

`mut x: ASTCloner = ASTCloner();` at function scope miscompiles. Use
`ASTCloner::new_plain()` instead. Documented in
`feedback_no_function_pointers.md`-adjacent territory; worth a memory
file if you find more cases.

### MEMORY.md

`/memory` to load — full project memory. Most relevant entries for this
work are about cryoc bugs, feedback rules, and prior audits.

---

## Just before you commit

- `git diff HEAD` — sanity-check, only DefaultExpansion-related changes
  + the new file. NO debug printfs in sema.cryo.
- `make cryo-fast` — clean rebuild.
- bridge tests + legacy stdlib + stdlib-next.
- `make selfhost-check` — ~7 minutes; **must not run while editing source**.

The user does the actual `git commit`. Surface the diff and proposed
commit message; don't commit yourself.

Good luck.
