# Handoff — Cryo Test Suite Expansion + Compiler Fixes (2026-05-06)

This document captures the state of an in-progress work session so a fresh
agent can pick up cleanly.  Everything described here is committed only to
the working tree — there is nothing pushed yet.

## What this session did

1. **Expanded the stdlib test suite** in `tests/tests/stdlib/`.  Coverage
   added for: `box`, `cmp`, `cstr`, `fmt`, `math`, `mem`, `path`, `ptr`,
   `slice`, plus a new `rc.cryo` (currently failing — see below).  Net
   tests: **217 passing**, 1 ignored, when `rc.cryo` is removed; with
   `rc.cryo` in place the build aborts on a remaining LLVM-verify error.

2. **Fixed several real compiler bugs surfaced by the test suite** —
   actual root-cause fixes, not workarounds:

   - **Hex digit-table consts** (FIXED).  `const HEX_LOWER: u8* = "..." as u8*`
     was emitted as `null` because `codegen_global_var` only handled bare
     `Literal` initializers and fell through `null` for the
     `CastExpression` wrapper.  Patched to peel one cast layer.

   - **`obj.method<T>()` end-to-end pipeline** (PARTIALLY FIXED).  Cryo
     never actually parsed/lowered explicit method-level type args on
     dotted member calls.  Wired through:
     * Parser consumes `<...>` after a member name.
     * `MemberAccessNode.generic_args` field added.
     * Cloner / Substituter handle the new field.
     * Sema re-resolves the method's *template* return type with the
       receiver's owner-level args + the call's method-level args bound,
       so the returned TypeRef is correct.
     * Mono's `try_infer_method_call` accepts explicit args (no longer
       bails) and uses them to populate `inference_bindings`.
     * Mono's `find_spec_inline_method` walks the spec'd type's own
       `methods[]` and falls back to the template's, enqueuing the
       receiver for specialization.
     * Mono's match-statement walker pushes pattern bindings onto a new
       per-walker `Monomorphizer.scope_names` / `scope_types` field-stack
       so identifiers like `raw` from `Result::Ok(raw)` resolve at
       inference time. (Pushing into the `names`/`types` parameters
       directly is broken — those arrays are passed by value, so pushes
       don't propagate to the caller but pops do, corrupting state.)
     * Mono's `resolve_arg_type_for_inference` consults that scope stack
       and handles `MemberAccess`-callee `CallExpression` subjects.

   - **Parser support for `()` as an impl target** (LANDED, but unused).
     `implement trait Drop for () { ... }` now parses.  The stdlib drop
     impl is *not* committed because codegen warns
     "method 'std::core::drop::()::drop' has no LLVM declaration" — the
     declare-method pipeline doesn't yet wire unit-type impls.

3. **Self-host gate**: `make selfhost-check` passes byte-identically
   (stage-3 ≡ stage-4 IR).  `bin/cryo` is re-pinned at 1,607,824 bytes
   with all the above changes baked in.

## Current state of the working tree

Modified compiler / stdlib files (uncommitted):

```
compiler/src/compiler/AST/cloner.cryo                  (MA + Identifier clone fields)
compiler/src/compiler/AST/expression.cryo              (MemberAccessNode.generic_args)
compiler/src/compiler/AST/substituter.cryo             (substitute MA generic_args)
compiler/src/compiler/codegen/decl_codegen.cryo        (peel cast in const init)
compiler/src/compiler/parser/expr_parser.cryo          (parse .method<T>)
compiler/src/compiler/parser/parser.cryo               (impl target = `()`)
compiler/src/compiler/passes/sema.cryo                 (refine method-return)
compiler/src/compiler/types/monomorphizer.cryo         (~325 lines added)
stdlib/core/drop.cryo                                  (NOTE comment only)
bin/cryo                                               (re-pinned)
```

New test files (untracked):

```
tests/tests/stdlib/{box,cmp,cstr,fmt,math,mem,path,ptr,rc,slice}.cryo
```

`tests/tests/stdlib/rc.cryo` is currently the only failing test —
intentionally left in place to surface the remaining compiler gap.

## What's still broken — pickup points for the next agent

### 1. Rc `cast<T>` codegen picks the wrong specialization (BLOCKING)

**Symptom:** `tests/tests/stdlib/rc.cryo` causes the test build to abort
with:

```
error[E0173]: LLVM module verification failed for std::core::intrinsics
  LLVM reports: Call parameter type does not match function signature!
  cast site `this.inner.cast<u8>()` in `Rc::drop` is emitted as
  `cast` returning NonNull<RcInner<i32>> instead of NonNull<u8>.
```

The first cast in the file (`raw.cast<RcInner<T>>()` in `Rc::try_new`)
works.  Both calls are correctly *parsed* and the *sema-time return
type* is right.  The mono pass *creates* both spec'd `MethodNode`s with
the right `U` substitution.  The bug is that codegen at the
`this.inner.cast<u8>()` call site picks the OTHER spec'd sibling
(`cast` with `U=RcInner<i32>`, the one created earlier for `try_new`)
instead of the freshly-spec'd `cast<u8>`.

**Where to look:**
- `try_infer_method_call` in `compiler/src/compiler/types/monomorphizer.cryo`
  — the `from_template` branch I added routes the spec'd `MethodNode`
  to the receiver's spec entry's `ast_node`.  That path executes; the
  spec'd sibling lands on `NonNull<RcInner<i32>>`'s `methods[]`.
- The fix probably needs to live in codegen's call-site resolution.
  Look at how a `CallExprNode` whose callee is a `MemberAccess` finds
  its target LLVM symbol — it likely walks the receiver type's
  methods array by name *only*, without consulting `call.resolved_method`.
  When two siblings share the method name and only differ in their
  spec'd substitution, the first one wins.
- `call.resolved_method` is set in `try_infer_method_call`
  (`call.set_resolved_method(spec_method as ASTNode*)`) — codegen should
  honor that node and compute its mangled name from there, not from a
  name-only lookup.
- Alternative angle: maybe each spec'd sibling needs a
  uniquely-mangled symbol that includes the method-level type args
  (e.g. `cast_u8` vs `cast_RcInner_i32`).  Check
  `MangledName::for_method` / similar in `compiler/src/compiler/codegen/`
  to see whether the method's own generic args are encoded.

### 2. HashSet `Drop` bound (PARSER LANDED, codegen + sema pending)

`stdlib/collections/hashset.cryo` is `HashMap<T, ()>` underneath.
`HashMap<K, V>::clear` and `::drop` carry `where K: Drop, V: Drop`.
With `V = ()` no `Drop` impl exists, so `HashSet<T>` is uninstantiable.

**What's done:** Parser accepts `implement ... for ()` (see
`parser/parser.cryo:~1452`).

**What's blocking:**
- Codegen emits a warning when an impl block targets `()`:
  ```
  warning[E0136]: codegen: method 'std::core::drop::()::drop' has no
    LLVM declaration in this module
  ```
  i.e. `declare_method` doesn't produce an LLVM declaration for impls
  on the unit type.  Find where impls on primitive/keyword targets get
  wired (the `boolean`/`i32`/etc. drops work) and extend that path to
  cover the `()` symbol.
- Sema's `bounds_satisfied` / `lookup_trait_impl` would need to
  recognize the `()` impl as the satisfier for `V: Drop` when V is the
  unit type.  Verify this once codegen emits a declaration.

### 3. **Important pivot from the user**

> "I'm actually thinking about removing the tuple type `()` and keeping
> it for `unit`.  For something similar, the syntax of
> `[i32, string, boolean]` can act as the tuple type instead.  Which is
> different from the array type like `i32[]`."

**Implication for #2 above:** if `()` becomes purely the unit type (no
longer a tuple synonym), the `Drop for ()` impl becomes unambiguously
"Drop for unit" and the codegen wiring gets simpler — there is one
canonical name for the type.  Tuple syntax shifts from `(T, U)` to
`[T, U]`, distinct from arrays `T[]`.

**This is forward-looking design**, not a constraint on the current
HashSet fix — the `()` parser support and the eventual `Drop for unit`
impl are still needed.  But anyone touching tuple/unit code in the
near term should be aware that:
- `()` → reserved for the unit type only
- `[T, U, V]` → new tuple syntax (parser/sema would need a new branch
  to disambiguate this from array literals)
- `T[]` → unchanged, single-element array of `T`

The transition from tuple-as-`(T, U)` to tuple-as-`[T, U]` is a
language-level change that touches lex / parse / type system and AST.
Worth doing as its own task before chasing the HashSet fix further.

## How to verify

```bash
# Build the compiler from the pinned state
make -C /workspaces/CryoLang cryo

# Run the test suite (will fail on rc.cryo's LLVM verify until the
# Rc cast<T> codegen issue lands)
cd /workspaces/CryoLang/tests && /workspaces/CryoLang/compiler/build/bin/cryo test

# Confirm the compiler still self-hosts byte-identically
make -C /workspaces/CryoLang selfhost-check
# Expect: ✓ FIXED POINT OK  stage-3 and stage-4 produce byte-identical IR

# After Rc / HashSet are fixed and tests pass, re-pin
make -C /workspaces/CryoLang pin-cryo
```

## Reference: relevant memory files

- `project_compiler_fixes_2026_05_06.md` — the full design notes for
  the changes above, including the by-value-array pitfall in mono's
  walker, the multi-pronged `obj.method<T>()` plumbing, and the hex
  fix.
- `project_stdlib_gaps_surfaced_by_tests.md` — original gap inventory
  from the test sweep that kicked this off.
- `feedback_no_workarounds.md` — operating principle for this
  session: fix the compiler, never paper over with workarounds.
- `project_struct_init_bug.md`, `project_selfhost_byte_identity_broken_2026_04_29.md`,
  and the `feedback_*` chain in `MEMORY.md` — broader context on the
  self-hosted compiler's known weak points.
