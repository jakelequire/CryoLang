# CryoLang Self-Hosting Handoff (round 3)

> **Where we left off:** stage 3 cryoc starts up cleanly, runs the CLI, parses
> the cryoconfig, finds `src/main.cryo`, and crashes deep inside
> `compile_project` → `std::core::primitives::string::append` → `strlen`
> with a NULL string operand. **That crash is the next blocker.**
>
> Six commits this session took stage 3 from "segfaults during CLI init"
> all the way to "actually starts compiling user code". Detailed memory
> notes for everything below live in `~/.claude/projects/-home-phock-Programming-apps-CryoLang/memory/`.

---

## TL;DR for the next agent

1. **Read `MEMORY.md` first.** Skip to:
   - `project_stage3_startup_fix.md` — first 4 fixes (stage 3 startup)
   - `project_stage3_pipeline_active.md` — fixes that got it to lex
   - `project_stage3_lex_works.md` — fixes that got it to the parser
   - `project_undefined_refs_root_cause.md` — load-bearing prior-session fixes; do not revert
   - `feedback_codegen_style.md` — coding-style preferences; honor strictly
2. **Goal:** chase the next crash in `string::append` → `strlen`. A null pointer is being passed where a `string` is expected. Likely the same struct-passing/init bug family — see "Likely root cause hypotheses" below.
3. **Hard rules:** fix root causes upstream, no inline string manipulation in codegen, no stdlib edits unless stdlib is genuinely incorrect, no `--no-verify` etc. The user has been emphatic about this.
4. The bootstrap chain is **strictly ordered** — see "Bootstrap workflow notes" below. Re-running it out of order produces confusing linker errors that look like new bugs.

---

## Repo layout cheat-sheet

```
CryoLang/
├── bin/cryo                              # C++ bootstrap compiler (binary, ~65 MB)
├── src/                                  # C++ bootstrap compiler source
├── cryoc/                                # Self-hosted compiler (Cryo source)
│   ├── cryoconfig                        # output_dir = "build", target_type = "executable"
│   ├── llvm_bindings.h                   # extern "C" decls for libLLVM-20
│   ├── build/cryoc                       # cryoc binary built by bootstrap (~1.2 MB)  → "stage 2"
│   ├── build/bin/cryoc                   # cryoc binary built by self-build (~2.7 MB) → "stage 3"
│   ├── build/bin/cryoc.ll                # combined IR dump emitted alongside binary (added this session)
│   ├── build/obj/*.{ll,o}                # cryoc's per-module IR + objects
│   └── src/                              # cryoc source
└── stdlib/
    ├── cryoconfig                        # output_dir = ".bin/", target_type = "stdlib"
    ├── .bin/libcryo.a                    # bundled archive
    └── .bin/obj/*.{ll,o}                 # per-module objects
```

---

## Current state

| Step | Status |
|---|---|
| C++ bootstrap (`bin/cryo`) → stdlib (`stdlib/.bin/libcryo.a`, bootstrap-mangled) | ✅ |
| C++ bootstrap (`bin/cryo`) → `cryoc/build/cryoc` (stage 2) | ✅ |
| Stage 2 (`cryoc/build/cryoc`) → stdlib (v0.2-mangled `libcryo.a`) | ✅ |
| Stage 2 → `cryoc/build/bin/cryoc` (stage 3 self-build) | ✅ — links cleanly, ~2.7 MB |
| Stage 3: `cryoc help`, `cryoc --version`, `cryoc -h`, `cryoc -v` | ✅ |
| Stage 3: `cryoc build` (project mode, no args) | ✅ — parses cryoconfig, resolves entry point, **then** segfault |
| Stage 3: `cryoc build /tmp/foo.cryo` (single-file) | ❌ — segfaults deeper inside `InternTable::intern` → `HashMap::get` after parse_function_declaration |
| Stage 3 → stage 4 self-host | not attempted; blocked by above |

---

## The active crash

```
$ cd cryoc && ./build/bin/cryoc build
[Executor] Registering built-in commands...
[Executor] Registered built-in commands: help, version, build, check, raw, project
[Runner] Dispatching command: build
[Build] Starting build process.
[Build] No input file specified, but found cryoconfig. Assuming project build.
[Project] Starting project build.
[Project] Compiling project from config: ./cryoconfig
Segmentation fault (core dumped)

#0  __strlen_evex
#1  std::core::primitives::string::append
#2  Compiler::Instance::CompilerInstance::compile_project
#3  CLI::Commands::Executor::cmd_project
...
```

So `compile_project` (running on `./cryoconfig`) reaches `string::append`, which calls `strlen` on what must be a NULL operand.

`compile_project` runs lots of `format(...)` and string concatenation as it iterates discovered modules. The crash is somewhere in that path — not in user-source compilation yet (that comes later in the pipeline).

---

## Likely root cause hypotheses (in order)

1. **Struct-field access path returning a NULL string.** `compile_project` reads fields from `ProjectConfig` (paths, project_name, source_dir, etc.). If any of those got NULL and we `format("%s/foo", config.field)`, strlen will crash. Verify by adding a printf right before the failing call, or step through gdb. Compare the value of each `config.*` field against what we know was parsed (now the cryoconfig parses correctly — see fix #6 below).

2. **Variadic forwarding loses args.** Same family as the `(null)` issue we already fixed for global string constants. cryoc's `format(fmt, args...)` runtime is `linkonce_odr` and uses `va_list`. The forwarding from a Cryo-source `args...` param to `format()`'s `vasprintf` may drop or mangle args. Visible mid-session as `[DEBUG][GENERAL] [Compiler] compile_project: ` — the path arg is missing in LOG_DEBUG output even though regular printf the next line shows it correctly.

3. **`File::content()` accessor returning stale data.** Earlier-session pattern: `file: File` (value type) gets mutated by `file.load()` (which takes `this: File*`). cryoc's struct-passing-by-value is fine here in principle, but a similar pattern hit Map/Parser earlier — keep an eye on it.

To bisect: run with `--debug` and watch where the LOG_DEBUG calls stop (or print garbage) before the crash.

---

## Six fixes in this session (do NOT revert)

Each addresses a real codegen bug — not a workaround. If a fix is "obviously wrong on a given test", check the IR dump (`cryoc/build/bin/cryoc.ll`) and confirm it isn't a newer cousin of the same family before changing it.

### `e0371403` — stage 3 startup + early compile-pipeline codegen bugs

- **`T[]` LLVM layout** (`type_map.cryo:map_array`, `compound.cryo:size_bytes`): emit `{T*, i64, i64}` (24 bytes) matching stdlib's `Array<T>`. Was 16-byte `{T*, i64}`. `Array<T>::push`'s `this.cap` write at offset 16 was clobbering the next struct field.
- **Pointer-receiver dispatch** (`ir_generator.cryo:visit_call_expr`): `runner.method()` for `runner: Runner*` now passes the loaded Runner ptr (rvalue), not the alloca address (Runner**).
- **Struct→ptr arg coercion** (`expr_codegen.cryo:codegen_call`): when a loaded struct value is being coerced to a ptr arg, recover the load's source pointer instead of always copying to a fresh temp. Mirrors `stmt_codegen.cryo`'s return-path pattern.
- **`i++`/`i--` codegen** (`ir_generator.cryo:visit_unary_expr`): wasn't emitted at all. For-loops emitted just `load i; br cond` with no add+store — infinite loop. Now load → add/sub 1 → store.

### `c334dd92` — stage 3 reaches lex pass

- **`sizeof()` of generic-instantiated types**: SizeofExprNode/AlignofExprNode gain a `type_ann: TypeAnnotation*` field captured by parser, substituted by AST substituter, resolved by `ctx.type_resolver.resolve(ann)` at codegen. `codegen_sizeof` falls back to `LLVMSizeOf(map_type)` when `t.size_bytes()` returns 0. Pre-fix `sizeof(HashMapEntry<K, V>)` collapsed to bare `HashMapEntry` (the unspecialised template, size 0), and HashMap's `calloc(N, 0)` returned 0-byte buffers.
- **Module-level constant initialisers** (`decl_codegen.cryo:codegen_global_var`): ints/bools/floats/null/strings emit a constant initialiser typed against the global's LLVM type. Pre-fix every global got `zeroinitializer` — `BUCKET_OCCUPIED` was 0 (should be 1), so HashMap probing never found a matching bucket.
- **Fixed→dynamic array coerce at struct-literal field stores** (`expr_codegen.cryo:codegen_struct_literal` + `coerce_fixed_array_to_fat_ptr`): `[Provision::Tokens]` (1-elem fixed array) being stored into a `Provision[]` (24-byte fat-pointer) field — alloca → malloc + memcpy → `{ptr, len=N, cap=N}`. Mirrors the C++ bootstrap pattern. **Important:** `LLVMArrayTypeKind = 11`, not 14. Got bit by this — the first attempt at the coercion never fired because of wrong constant.
- **`string == null`** (`expr_codegen.cryo:codegen_string_compare`): pointer-equality `icmp` instead of `strcmp(s, null)` (which crashes).

### `4ce7b302` — stage 3 lex pass works

- **Char escapes** (`expr_codegen.cryo:codegen_literal::Char`): decode `\n`, `\t`, `\r`, `\0`, `\\`, `\'`, `\"`, `\a`, `\b`, `\f`, `\v`, `\xHH`. Lexer hands codegen the raw two-char lexeme `"\\n"`, codegen used to take just `value[0]` (the backslash, 92). `'\n' == 10` thus always failed in `skip_whitespace`'s newline branch — every char beyond `}` looked unexpected.
- **`*string` deref** (`checker.cryo:check_unary_op`, `ir_generator.cryo:codegen_rvalue`): sema returns `char` for `*string`; codegen emits `load i8 + zext i32` (mirroring the existing string-index special case). Pre-fix `*ptr as char` lowered to a pointer-sized load + ptrtoint, reading 8 bytes treated as a pointer instead of one byte.
- **String global initialisers** (`decl_codegen.cryo:codegen_global_var`): `const ANSI_RED: string = "..."` initialises to the address of the interned string global, not zeroinitializer. Pre-fix every string global was null and printf("%s", RED) emitted "(null)" everywhere in diagnostics.

### `56ba5d36` — emit base-class constructor calls for derived constructors

- **Base-class ctor delegation** (`ir_generator.cryo:generate_method_body` + `emit_base_ctor_call`): `Parser(...) : ExprParser(tokens, source_file, ctx)` now actually *calls* ExprParser's constructor before the body runs, instead of emitting an empty body that returns immediately after stashing params into local allocas. Pre-fix every derived-class constructor returned without ever initialising inherited fields — `ParserBase`'s `tokens`/`pos` stayed garbage and downstream reads segfaulted.

### `9974e1a2` — widen integer literal initialisers to local-var width

- **`mut i: i64 = 0`** (`stmt_codegen.cryo:codegen_local_var`): was emitting `store i32 0, ptr %i, align 4` into an 8-byte i64 alloca, leaving the high 4 bytes uninitialised. Once the loop reloaded `i` as i64, the value was 0 in the low 32 bits but garbage in the high 32, so `i < len` exited on the first iteration.

  Sema doesn't propagate the variable's expected type to the literal `0`, so `codegen_literal` defaults the literal to i32. `codegen_local_var` now zext-widens (or trunc-narrows) the initialiser to match the alloca's integer width.

  **This was the bug behind `cryoc build` (project mode) misreading the cryoconfig.** Every for-loop counter in the parser collapsed to garbage on the first iteration, so all parses returned defaults and `entry_point` came out as `"main.cryo"` (the default) instead of the parsed `"src/main.cryo"`.

### `c334dd92` (additional) — combined IR dump alongside the binary

- **`run_linking`** (`passes.cryo`): after the executable links, also calls `llvm-link-20 -S build/obj/*.ll -o <exe>.ll`. The combined `cryoc/build/bin/cryoc.ll` (~17 MB, ~324k lines) is invaluable when chasing codegen bugs — grep across the whole program at once. Stdlib `.ll` files are intentionally excluded (non-`linkonce` duplicate globals like `MAX_ALIGN` block re-link; that's a separate fix worth doing later).

---

## Files modified across the session

```
cryoc/llvm_bindings.h                              + LLVMIsNull, LLVMIsConstant
cryoc/src/CLI/_module.cryo                         restored deleted `first_arg` line
cryoc/src/CLI/commands.cryo                        (no functional changes from us; user added [Executor] / [Runner] debug printfs in-progress)
cryoc/src/compiler/AST/cloner.cryo                 clone Sizeof/Alignof type_ann
cryoc/src/compiler/AST/expression.cryo             SizeofExprNode/AlignofExprNode + type_ann field
cryoc/src/compiler/AST/substituter.cryo            recurse into type_ann
cryoc/src/compiler/codegen/decl_codegen.cryo       global var literal initializers (int/bool/float/null/string)
cryoc/src/compiler/codegen/expr_codegen.cryo       sizeof fallback; struct-literal field coerce; coerce_fixed_array_to_fat_ptr; struct→ptr arg recovery; string==null icmp; char escape decoding
cryoc/src/compiler/codegen/ir_generator.cryo       array literal cap field; pointer-receiver dispatch; PlusPlus/MinusMinus; *string i8-load+zext; SizeofExpression handler with resolve_sizeof_operand_type; emit_base_ctor_call from generate_method_body
cryoc/src/compiler/codegen/passes.cryo             combined IR dump via llvm-link-20 after run_linking
cryoc/src/compiler/codegen/stmt_codegen.cryo       return-path fat-pointer cap field; integer-init widen/trunc in codegen_local_var
cryoc/src/compiler/codegen/type_map.cryo           map_array → 3-field {T*, i64, i64}
cryoc/src/compiler/parser/expr_parser.cryo         parse_sizeof/alignof use parse_type_annotation, store as type_ann
cryoc/src/compiler/types/checker.cryo              Function-pointer == case (prior session); *string returns char
cryoc/src/compiler/types/compound.cryo             ArrayType::size_bytes 24 (was 16)
```

Stdlib was **not** modified.

---

## Critical principles — read before touching ANY code

> "Don't add a permissive TypeChecker rule. User explicitly rejected as the kind of hack that plagued the C++ Cryo compiler."
>
> "Don't workaround sema gaps in codegen — fix the root cause in sema."
>
> "Don't modify stdlib to dodge a cryoc bug. Stdlib patches are only OK when stdlib is genuinely incorrect."
>
> "Bootstrap C++ Cryo is the immovable obstacle until cryoc self-hosts. Source-level changes that break the bootstrap are out — even if they're 'the right thing'."
>
> "No inline string manipulation in codegen — substring extraction, character replacement loops, pointer arithmetic on strings. The C++ compiler became unmaintainable due to ad-hoc name-resolution hacks; the user is emphatic this won't repeat in cryoc."

These came up explicitly multiple times across sessions. Follow them.

---

## Reasonable first hour for the next agent

1. **Read `MEMORY.md` and the project_stage3_*.md files** (10 min). Especially `feedback_codegen_style.md`.
2. **Confirm the baseline** (5 min): `cd cryoc && ./build/bin/cryoc build` — should run far enough to print `[Project] Compiling project from config: ./cryoconfig` then segfault. If you see a different crash, the bootstrap chain might be in a bad state — see below.
3. **Pick attack on the `string::append → strlen` crash:**
   1. **First**: `gdb --args ./build/bin/cryoc build` → `b strlen` → `run` → at the breakpoint, inspect `%rdi` (strlen arg). It'll be NULL or a bad pointer. Then `bt` and `frame 1` to find which `string::append` call is doing it.
   2. Identify the source-level call site (look up the mangled name in cryoc's source). It's somewhere in `compile_project` between cryoconfig parse and the first compile pass.
   3. Add a `printf` before the call site to print all the strings it depends on, rebuild, re-run. The null one is the bug.
   4. Trace upstream from the null string to where it should have been initialised. Likely candidates: a struct field access, a function return, a chained method call.
4. **Don't try to fix everything at once.** Match the diagnose-then-fix cadence the previous sessions used. Each fix should be commit-sized.

---

## Bootstrap workflow notes (chicken-and-egg gotchas)

The bootstrap chain is **strictly ordered** and easy to break:

```
1. bootstrap stdlib    → bootstrap-mangled libcryo.a    (C++ bin/cryo)
2. bootstrap cryoc     → cryoc/build/cryoc              (C++ bin/cryo links against #1)
3. v0.2-mangle stdlib  → v0.2-mangled libcryo.a         (cryoc from #2)
4. self-build cryoc    → cryoc/build/bin/cryoc          (cryoc from #2 links against #3)
```

If step 3 runs (or stdlib's `.bin/` is wiped), step 2 will fail with `undefined reference to std::collections::hashmap::hash_int` (etc.) because libcryo.a is now v0.2-mangled and the C++ build expects bootstrap mangling. You then need to redo step 1 first.

When changing cryoc source: **always** start over from step 1 — `cd stdlib && bin/cryo build` — even if the change "shouldn't affect linking". The cwd matters: `bin/cryo build` and `cryoc/build/cryoc build` both look at the cwd's `cryoconfig`.

Common mistake (we made this many times): chaining `bin/cryo build` twice without `cd`-ing to `cryoc` between them, so step 2 never happens. Always check that step 2 prints `Build successful: build/cryoc`.

The C++ bootstrap is **slow** (~3–5 min normally; up to 10+ min if it hits something it doesn't grok) and sometimes hangs on certain Cryo source patterns — for example, blocks of multiple `const` declarations inside a `match` arm have triggered minute-scale slowdowns. If the bootstrap is running >5 min, suspect it's stuck and consider simplifying whatever Cryo source pattern you just added.

### Reproduce the active crash

```bash
cd /home/phock/Programming/apps/CryoLang/stdlib
rm -rf .bin && mkdir -p .bin/obj
/home/phock/Programming/apps/CryoLang/bin/cryo build       # step 1

cd /home/phock/Programming/apps/CryoLang/cryoc
/home/phock/Programming/apps/CryoLang/bin/cryo build       # step 2

cd /home/phock/Programming/apps/CryoLang/stdlib
rm -rf .bin && mkdir -p .bin/obj
/home/phock/Programming/apps/CryoLang/cryoc/build/cryoc build   # step 3

cd /home/phock/Programming/apps/CryoLang/cryoc
rm -rf build/obj build/bin
/home/phock/Programming/apps/CryoLang/cryoc/build/cryoc build   # step 4 — produces build/bin/cryoc

./build/bin/cryoc build                                          # crashes in string::append → strlen
```

---

## Where to find more context

`~/.claude/projects/-home-phock-Programming-apps-CryoLang/memory/` is the canonical session log. Highlights:

- **`MEMORY.md`** — index. Loaded automatically; under 200 lines.
- **`project_stage3_startup_fix.md`** — first 4 fixes (stage 3 boots).
- **`project_stage3_pipeline_active.md`** — fixes that get to lex.
- **`project_stage3_lex_works.md`** — fixes that get to the parser, ending state at "lex pass works, parser hangs in InternTable".
- **`project_undefined_refs_root_cause.md`** — prior session's load-bearing fixes for the 210→0 undefined-refs work. **Required reading before touching codegen — these are the reason stage 3 links at all.**
- **`feedback_codegen_style.md`** — the user's style preferences. They are firm about these.
- `project_codegen_decisions.md`, `project_pipeline_phases.md`, `project_mangling_v0_2.md`, `project_runtime_inlined.md`, `project_type_cache_shared.md` — architectural reference.
- `project_hashmap_string_bug.md` — bootstrap pointer-compare-on-strings bug; **read before touching arena caches**.

---

## Open architectural debts (ordered by importance)

1. **Active blocker:** `string::append` → `strlen` crash from `compile_project`. Bisect with gdb + printf.

2. **`cryoc build /tmp/foo.cryo` (single-file mode)** — crashes deeper, in `InternTable::intern` → `HashMap::get` after `parse_function_declaration`. Stack:
   ```
   HashMap<S,j>::get  → InternTable::intern  → ParserBase::intern_lexeme
                     → Parser::parse_function_declaration
                     → Parser::parse_top_level
                     → Parser::parse
   ```
   Parser actually runs. Then crashes. Probably the same `T[]`-struct-passing/init bug family — InternTable's HashMap field landing in a partially-initialised state.

3. **Variadic forwarding** (`format(fmt, args...)`). Symptom: `[DEBUG][GENERAL] [Compiler] compile_project: ` (path arg missing) even when the path is correct at the call site. May or may not be the same bug as #1 above. Worth a focused investigation once the active crash is fixed — it's likely the source of more (null)-style symptoms downstream.

4. **Stop re-codegen'ing stdlib in cryoc's self-build** (Task 1 from the original handoff). Currently `cryoc/build/obj/std__*.o` exist and the link command also pulls `stdlib/.bin/libcryo.a`. linkonce_odr collapses duplicates so it links, but it's wasted compile time and architectural noise.

5. **`-> never` as a real source annotation** (instead of the 2-name hardcoded list `panic` / `prelude::panic` in the unreachable-after-divergent-call codegen). Blocked on bootstrap C++'s lack of return-path-with-divergent-call analysis. Reachable once cryoc self-hosts.

6. **`T[]` ↔ `Array<T>` unification** in type_resolution. Currently they're layout-compatible but distinct LLVM types (literal `{T*, i64, i64}` vs named `Array<T>` struct). Resolving `T[]` to `InstantiatedType<Array, [T]>` would eliminate the duplicate.

7. **Stdlib `MAX_ALIGN` non-`linkonce` duplicates** block including stdlib in the IR-dump. Small fix; gives a more useful combined dump.

8. **Optimization passes never run.** Phase 7 emits unoptimized IR. Stage 3 binary is 2.7 MB; with `-O2` it'd be smaller. Add the LLVM optimization pipeline call after IRGen.

---

## State of the working tree at handoff

```
$ git log --oneline -7
9974e1a2 fix: widen integer literal initializers to local var width
56ba5d36 fix: emit base-class constructor calls for derived constructors
4ce7b302 fix: stage 3 lex pass works; char escapes, *string deref, string globals
c334dd92 fix: stage 3 reaches lex pass; codegen fixes for sizeof, globals, struct literals
e0371403 fix: stage 3 startup + early compile-pipeline codegen bugs
3d4281cc feat: enhance code generation and type resolution with new functions and panic handling
2fb544a1 ###

$ git status
On branch main
Your branch is ahead of 'origin/main' by 6 commits.
nothing to commit, working tree clean
```

Build artifacts present (regeneratable):
- `bin/cryo` — C++ bootstrap, ~65 MB
- `cryoc/build/cryoc` — bootstrap-built cryoc (stage 2), ~1.2 MB
- `cryoc/build/bin/cryoc` — self-built cryoc (stage 3, the one you'll be debugging), ~2.7 MB
- `cryoc/build/bin/cryoc.ll` — combined IR dump for stage 3, ~17 MB / ~324k lines
- `stdlib/.bin/libcryo.a` — current state depends on which step ran last; redo the chain if unsure

---

## What works at stage 3 right now

- `cryoc` (no args) — prints help, exits 0.
- `cryoc help`, `cryoc version`, `cryoc --version`, `cryoc -v`, `cryoc --help`, `cryoc -h` — all work.
- `cryoc build` (project mode):
  - Loads `./cryoconfig` correctly (484-byte file, 14 parsed lines).
  - Resolves entry_point to `src/main.cryo`.
  - **Then** crashes inside `compile_project` → `string::append`.

The CLI/dispatch/PassRegistry/Lexer all work end-to-end. The crash is squarely in compile-pipeline-internals territory now.
