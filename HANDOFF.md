# CryoLang Self-Hosting Handoff

> Current focus: **Get stage 2 → stage 3 linking, even if stage 3 can't yet self-build.**
> The compiler (cryoc) builds and emits LLVM IR / object files. The stdlib builds via cryoc and bundles to `libcryo.a`. Self-build link still fails on **210 undefined references** — all generic specializations the monomorphizer skips.

---

## TL;DR for the next agent

1. **Read `~/.claude/projects/-home-phock-Programming-apps-CryoLang/memory/MEMORY.md` first.** That index points at the long-form context for every subsystem (mangling, runtime, type cache, monomorphizer, etc.). The short notes below are deliberately not exhaustive — they tell you where the work is and what the user wants, not how each subsystem works.
2. The user's primary goal this session is **stage 2 → 3 linking**. Stage 3 doesn't need to compile itself yet, but the link command should resolve cleanly once you've fixed enough of the missing-spec gap.
3. **Fix root causes upstream. Do not paper over upstream bugs in codegen.** This is a hard rule — the prior C++ Cryo compiler accumulated codegen workarounds and became unmaintainable. The user has specifically called this out twice this session.
4. The stdlib builds cleanly via cryoc (`Project compilation succeeded.` from `stdlib/`), and now produces `stdlib/.bin/libcryo.a` (1.1 MB, 53 objects). The `Passes::bundle_archive` step that creates it landed this session.
5. Bootstrap path: `bin/cryo` (C++) → builds `cryoc/build/cryoc` → builds `stdlib/.bin/libcryo.a`. The C++ bootstrap link expects bootstrap-mangled libcryo.a; the cryoc-self-build link expects v0.2-mangled libcryo.a. **Always re-bootstrap libcryo.a before re-bootstrapping cryoc**, or the C++ link breaks.

---

## Repo layout cheat-sheet

```
CryoLang/
├── bin/cryo                              # C++ bootstrap compiler (binary)
├── src/                                  # C++ bootstrap compiler source
├── cryoc/                                # Self-hosted compiler (Cryo)
│   ├── cryoconfig                        # output_dir = "build", target_type = "executable"
│   ├── llvm_bindings.h                   # extern "C" decls for libLLVM-20
│   ├── build/cryoc                       # cryoc binary (after bootstrap)
│   ├── build/obj/*.o                     # cryoc's per-module object files
│   └── src/                              # cryoc source
│       ├── main.cryo                     # entry point
│       └── compiler/                     # passes, codegen, types, resolver, AST, etc.
└── stdlib/
    ├── cryoconfig                        # output_dir = ".bin/", target_type = "stdlib"
    ├── .bin/libcryo.a                    # bundled archive (cryoc OR bootstrap-built)
    └── .bin/obj/*.o                      # per-module object files
```

---

## Current state

| Step | Status |
|---|---|
| C++ bootstrap (`bin/cryo`) → `cryoc/build/cryoc` | ✅ works (using bootstrap-built libcryo.a) |
| `cryoc` → stdlib build | ✅ all 53 modules compile, libcryo.a bundled |
| `cryoc` → cryoc self-build (stage 2 → 3) | ❌ link fails: 210 undefined references |
| Stage 3 self-test | not attempted; gated on stage 2→3 linking |

### The 210 undefined references

```
63 × HashMap<K,V>::insert(...)       — various (K,V) pairs
52 × HashMap<K,V>::find_entry(...)   — various (K,V) pairs
40 × Option<T>::unwrap()             — various T (mostly user types)
37 × Array<T>::partition(...)        — various T (mostly user types)
 9 × ::new specs                     — Map::new, Array<…>::new
 4 × Writer::flush                   — virtual dispatch shim
 4 × ASTNode::accept                 — visitor base
 1 × panic                           — std::prelude direct call
```

**All are generic specializations.** The monomorphizer emits *most* specs in the requesting module (e.g. `Compiler__CompileMode.o` defines `Array<String>::new`, `::get`, `::set`, `::length`, `::rotate_left`, `::swap_remove`, etc.) — but skips a specific subset.

**Hypothesis (unverified — start here):** the skipped methods all have body operations that fail to resolve for the instantiated `T`. For example:
- `partition(mut &this, low: u64, high: u64)` does `this.ptr[j] < pivot` — needs `T: <`. For `T = String`, no `<` operator is defined, so body sema fails.
- `unwrap` on `Option<T>` may panic-call into a path that needs T-specific operations.
- `HashMap::insert` / `find_entry` need `K: hash` and `K: ==`.

When body sema fails, the monomorphizer apparently drops the spec silently rather than emitting it (or emitting a stub). That leaves the symbol undefined at link time.

**Verify the hypothesis first:**
1. Pick one missing spec, e.g. `C$3std.11collections.5array.495Array$LN$L3std.11collections.6string.6String$G$G-9partition$F$m_m_m$Rm`.
2. Find where the monomorphizer enqueues / processes this spec (search for `populate_struct_methods` / `enqueue_*` in `cryoc/src/compiler/types/monomorphizer.cryo`).
3. Add a printf at the spec-emission point to see whether `partition` is even being attempted for `T = String`, or whether it's being filtered out earlier.
4. If it's being attempted but failing somewhere, find the failure point.

**Investigation entry points:**
- `cryoc/src/compiler/types/monomorphizer.cryo` — the spec generator
- `cryoc/src/compiler/AST/substituter.cryo` — substitutes T in method bodies
- `cryoc/src/compiler/passes/sema.cryo` — type-checks the substituted bodies
- The `T < T` resolution happens in sema's binary-expr handling — search for `BinaryExpression` and `LAngle` / `RAngle`

---

## The user's goals (verbatim)

> Right now, my goals are to have stage 2 -> stage 3 working. Even if stage 3 can't compile itself yet, I want to get all the linker stuff figured out and planned properly.
>
> For library based projects like the stdlib, it should be using the cryoconfig's output folder properly. The IR and object files goes into the `{build_folder}/obj` and the final library object file(s) outputted to `{build_folder}/{files}` (or the executable binary if it's a non-library build type).
>
> The stage 2 -> stage 3 should not be trying to re-codegen stdlib files because the stdlib should be built by `cryoc` so linking against it shouldn't be a problem. The compiler just needs the symbols so it can resolve things properly, but it doesn't need to recreate the IR.
>
> Remember, it's important that we don't do workarounds in codegen so if there's an upstream issue, we should do that even if it's harder instead of hacking around it in codegen. These undefined references that are coming up need to be reduced so we can get stage 2 -> stage 3 working.

---

## Concrete tasks (in priority order)

### Task 1 — Stop re-codegen'ing stdlib in cryoc's self-build

**Symptom:** `cryoc/build/obj/` contains files like `std__core__intrinsics.o`, `std__core__option.o`, `std__core__primitives.o` — cryoc compiles its imported stdlib modules from source as if they were part of its own project. The link command then references both these per-stdlib .o files AND the prebuilt `stdlib/.bin/libcryo.a`. Wasted compile time, and they can fight for the same symbols.

**What you want:** when cryoc compiles itself (or any project), stdlib `import` statements should:
- Load the stdlib module's **declarations** (function signatures, struct/enum layouts, generic templates) into the DI / type arena, so sema, monomorphization, and call-site mangling work.
- **NOT** run codegen on the stdlib module's bodies.
- The link step pulls the actual code from `libcryo.a`.

**Where to look:**
- `cryoc/src/compiler/instance.cryo` — top-level pipeline. Module loading happens here.
- `cryoc/src/compiler/module_loader.cryo` (or similar) — how source files are discovered and parsed.
- `cryoc/src/compiler/passes/pass_registry.cryo` — what passes run for which modules.
- `Compiler__ModuleGraph` (in cryoc's source) tracks the per-module dependency graph.

**Sketch of the fix (verify against the actual code before implementing):**
1. Tag each loaded module as either "owned by current project" or "imported library".
2. Phase 4 (TypeRegistration), Phase 5 (TypeResolution), Phase 6 (Sema) should run on imported library modules — they need to populate the DI / type arena with declarations so the project's modules can resolve names. Already mostly works, since these passes don't emit object code.
3. Phase 7 (Codegen) should **skip** imported library modules. Their .o files already exist in `libcryo.a`.
4. The Phase 8 link command keeps the existing `libcryo.a` reference; just don't add the per-stdlib-module .o files to `ctx.artifacts.object_files`.

**Open question worth asking:** do generic specializations need to be emitted in the project module (which uses them) or in the stdlib module (which defines the template)? Per `MEMORY.md` → `project_codegen_decisions.md`, the architecture decision is "specializations in requesting module" — so each project that uses `HashMap<K,V>` for new (K,V) emits the specialization itself. That should still work after this change: stdlib's `libcryo.a` provides specializations for stdlib-internal users (Array<u8>, etc.), and the project emits its own specs for its own (K,V) pairs. Confirm this is how the current monomorphizer is structured before relying on it.

---

### Task 2 — Fix the missing-spec gap so the link resolves

This is the 210-undefined-references issue. Even after Task 1, the project still needs to emit specs for its own user-type instantiations. Currently it skips many.

**Start with the smallest reproducible case.** Pick `Array<String>::partition` since you can see the contrast cleanly: `Array<String>::new` and `::get` are emitted in `Compiler__CompileMode.o`, but `::partition` isn't.

**Steps:**
1. Verify the hypothesis (body sema fails because `T < T` doesn't resolve for `T = String`).
2. If confirmed, **fix in stdlib** — `String` should have an `operator<` (or whichever spelling cryo uses). Search `stdlib/collections/string.cryo` for existing operators on `String`. The `==` impl, if it exists, is a model.
3. *Don't* fix in monomorphizer by skipping the failing op or generating a stub call — that's a codegen workaround.
4. After adding `String::lt`, re-bootstrap stdlib + cryoc, and re-run the self-build link. The 37 `partition` references should drop to whatever subset is for types that *also* lack `<`.

Then iterate on the next category (`HashMap::insert`, `::find_entry` need `K: hash` and `K: ==`; `Option::unwrap` may need a different fix entirely).

**Don't try to make the monomorphizer more permissive.** The user explicitly rejected the C++ Cryo TypeChecker's "if both types have a T somewhere, accept" rule.

---

### Task 3 — Build-folder convention (per user)

> For library based projects like the stdlib, it should be using the cryoconfig's output folder properly. The IR and object files goes into the `{build_folder}/obj` and the final library object file(s) outputted to `{build_folder}/{files}` (or the executable binary if it's a non-library build type).

**Current state:**
- `cryoc/cryoconfig` has `output_dir = "build"`. `cryoc` emits `.o` into `build/obj/` and the executable into `build/bin/cryoc`. ✅
- `stdlib/cryoconfig` has `output_dir = ".bin/"`. Stdlib emits `.o` into `.bin/obj/` and the archive into `.bin/libcryo.a` (via `Passes::bundle_archive` added this session). ✅

**What may need fixing:**
- Verify executable targets put the binary in `{output_dir}/bin/{name}` vs `{output_dir}/{name}` — the user wrote "or the executable binary if it's a non-library build type" without specifying `bin/` subdir. Currently it's `build/bin/cryoc`. Confirm with the user whether the `bin/` subdir is desired or if they want `build/cryoc` flat.
- `cryoc` doesn't auto-create `.bin/obj/` — the stdlib build currently fails on a clean checkout because the dir doesn't exist. `Phase 7: ObjectEmission` calls `mkdir(output_dir, 493)` for `output_dir = "build/obj"` (for cryoc) but for stdlib's `.bin/obj`, the `mkdir` may not create intermediate `.bin`. Check `Codegen::Passes::run_object_emission` (around line 280 of `cryoc/src/compiler/codegen/passes.cryo`). Use `mkdir -p` semantics or create both levels.

---

## Critical principles — read before touching the codegen

> "Don't add a permissive TypeChecker rule. User explicitly rejected as the kind of hack that plagued the C++ Cryo compiler."
>
> "Don't workaround sema gaps in codegen — fix the root cause in sema."
>
> "Don't modify stdlib to dodge a cryoc bug. Stdlib patches are only OK when stdlib is genuinely incorrect."

The session before this one fixed a sema gap (by-value `this` resolved_type was unset) by **adding the back-fill in `types/resolver.cryo`**, NOT by working around it in codegen. That's the model. If you see a "hmm, codegen could special-case this" thought, treat it as a signal that there's an upstream pass that should have set the field correctly.

---

## What the prior session(s) changed (reference, not exhaustive)

Run `git diff HEAD` for the full diff. High-level:

**cryoc-side compiler changes:**
- `codegen/expr_codegen.cryo` — int↔ptr coercion in `codegen_binary` (was missing for `int < ptr` comparisons in `alloc/stack.cryo:357`); new ptr→float and ptr→integer-receiver coercion in `codegen_call` (so by-value primitive receivers can be called via `&this`).
- `codegen/stmt_codegen.cryo` — `coerce_return_value` now handles struct→ptr by detecting a load instruction (`LLVMOpcode::Load = 27`) and substituting its source operand. Handles `return this` from `&Self`-returning methods.
- `codegen/passes.cryo` — new `Passes::bundle_archive` runs after Phase 7 for library/stdlib targets, calling `ar rcs`. Stdlib hardcoded to `libcryo.a`.
- `codegen/decl_codegen.cryo` — `declare_impl_block` always qualifies `target_type` (even for primitives) so definition mangling matches what call sites compute via `lookup_type_name(obj_ref)`. Globals now declared in Phase 2 (declare_functions) instead of Phase 3 to handle forward-references.
- `codegen/ir_generator.cryo` — Identifier-call path uses arity-aware lookup before non-arity fallback (discriminates `pipe()` 0-arg free vs `pipe(int*)` intrinsic). ScopeResolution Fallback1 same. Phase 3 no longer redoes globals (Phase 2 owns them).
- `decl_index.cryo` — `register_function_type_with_module` also calls `register_overload_mangled` so per-(name, type_ref) mangled name is pinned (not last-wins on the bare-name slot).
- `passes/type_resolution.cryo` — symmetric overload-mangled registration for free-function bare aliases and intrinsics. `resolve_method_signatures` takes a new `owner_type: TypeRef` param to back-fill `this`/`&this` resolved_type for receiver params (sema gap fix).
- `types/resolver.cryo` — `resolve_method_signatures` accepts `owner_type` and back-fills receiver types after `resolve_func_signature`.
- `types/arena.cryo` + `types/monomorphizer.cryo` — earlier session: integer-keyed `generic_param_cache` / `bounded_param_cache`; recursive `type_contains_generic_param`; idempotency guard on `populate_enum_variants`. Don't expand the integer-keyed-cache pattern further (see `~/.claude/projects/.../memory/project_hashmap_string_bug.md`).
- `llvm_bindings.h` — added `LLVMGetOperand`, `LLVMGetNumOperands`.

**stdlib-side patches** (genuine stdlib bugs, not workarounds):
- `stdlib/core/primitives.cryo` — six unqualified `log(...)` calls qualified to `intrinsics::log(...)`.
- `stdlib/alloc/arena.cryo:383` — `0 as u64` cast to fix PHI-type mismatch.
- `stdlib/math/_module.cryo` — `sin/cos/tan` (lines 300/305/310) call `intrinsics::sin/cos/tan` (f64) instead of the `*f` (f32) variants. f32 wrappers at lines 660+ are correct.
- `stdlib/math/_module.cryo` — `clz`/`ctz` (lines 537/542) on `u64` call `intrinsics::clz64`/`ctz64` instead of `clz32`/`ctz32`.

---

## Verification commands

```bash
# 1. Bootstrap stdlib (sets up bootstrap-mangled libcryo.a for the C++ build):
cd /home/phock/Programming/apps/CryoLang/stdlib
/home/phock/Programming/apps/CryoLang/bin/cryo build

# 2. Bootstrap-build cryoc (uses bootstrap-mangled libcryo.a):
cd /home/phock/Programming/apps/CryoLang/cryoc
/home/phock/Programming/apps/CryoLang/bin/cryo build       # → build/cryoc

# 3. Stdlib rebuild via cryoc (produces v0.2-mangled .o + libcryo.a):
cd /home/phock/Programming/apps/CryoLang/stdlib
rm -rf .bin && mkdir -p .bin/obj   # cryoc doesn't auto-create .bin/obj on a fresh checkout
/home/phock/Programming/apps/CryoLang/cryoc/build/cryoc build
# → "Bundled archive: .bin//libcryo.a (53 objects)"
# → "Project compilation succeeded."

# 4. Self-build cryoc (stage 2→3) — currently fails on 210 undefined refs:
cd /home/phock/Programming/apps/CryoLang/cryoc
/home/phock/Programming/apps/CryoLang/cryoc/build/cryoc build 2>&1 | grep "undefined reference" | wc -l
# → 210

# 5. Categorize the missing specs:
/home/phock/Programming/apps/CryoLang/cryoc/build/cryoc build 2>&1 \
  | grep "undefined reference" \
  | sed 's/.*reference to .C\$[^ ]*-[0-9]*\([a-z_]*\)\$.*/\1/' \
  | sort | uniq -c | sort -rn
```

**If the C++ bootstrap link fails** with `undefined reference to 'std::collections::hashmap::hash_int'` (or similar old-mangling names), it means the cryoc-built libcryo.a is in place but the C++ compiler needs the bootstrap-mangled one. Re-run step 1, then step 2.

---

## Where to find more context

The `~/.claude/projects/-home-phock-Programming-apps-CryoLang/memory/` directory holds the long-form notes from prior sessions. `MEMORY.md` is the index. Highlights:

- `project_linking_pass.md` — full status of the linking work, including this session
- `project_codegen_decisions.md` — "specializations in requesting module" architecture
- `project_codegen_progress.md` — remaining codegen gaps
- `project_pipeline_phases.md` — phase ordering (Phase 4 → 5 → 6a mono → 6b sema → 7 codegen → 8 link)
- `project_mangling_v0_2.md` — mangling spec
- `project_hashmap_string_bug.md` — the bootstrap pointer-compare-on-strings bug; **read before touching arena caches**
- `project_runtime_inlined.md` — why `cryoc/runtime/` was deleted
- `project_type_cache_shared.md` — cross-module struct-type dedup
- `feedback_codegen_style.md` — coding style preferences (no inline string manipulation, no hacky workarounds, fix root causes upstream)

The user's working directory is `/home/phock/Programming/apps/CryoLang`. They use Linux + `bash`.

---

## A reasonable first hour for the next agent

1. Read `MEMORY.md` and skim the linked project docs above. Ten minutes.
2. Run the verification commands above (steps 1–4) to confirm the same starting state on whichever machine you're on. Five minutes.
3. Pick the smallest concrete missing spec — `Array<String>::partition`. Look at `stdlib/collections/array.cryo:570-585` to see what `partition` requires of `T`. Then check `stdlib/collections/string.cryo` for whether `String` has `<`.
4. If `String` has no `<`, that confirms the hypothesis. Add `String::lt` (or whatever the conventional spelling is in this codebase — check how `==` is implemented on `String`), re-bootstrap stdlib, re-bootstrap cryoc, rebuild stdlib via cryoc, re-run the self-build link, and check whether the 37 `partition` undefineds drop to a smaller number (those for types that still lack `<`).
5. Loop on the next category once `partition` is done.
6. **In parallel**, start scoping Task 1 (stop re-codegen'ing stdlib in self-build) — that's the bigger architectural win even if it doesn't itself reduce the undefined-ref count, because it sets up the right model for "library" projects.

Don't try to do everything at once. The user wants linker stuff figured out and planned properly — incremental progress with each fix verified is better than a big-bang change.
