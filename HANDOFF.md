# Cryo — handoff for next agent

**Date:** 2026-04-29, end of session.
**Branch:** `main` at `3d880760` (selfhost-check shifted to stage-4 fixed point).
**Working tree:** clean.

The user is preparing for a 0.1.0 tag and an install/distribution story. They want repo cleanup, CI, and a usable install path before tagging.

> **NOTE:** This handoff is being read on a different machine than where the prior session ran, so it deliberately repeats project context that would otherwise live in agent memory. If you've worked on Cryo before, skim sections 1–3; everything load-bearing is in §4 onward.

---

## 1. About the user

Self-taught dev (4 years), works as a banker, building Cryo as a portfolio capstone to break into the software industry. Cryo is a passion project — they've been working on it for years and care deeply about getting it right. They take feedback well but **don't want band-aids or workarounds**; root-cause fixes only. They prefer terse, technical communication.

They handle git pushes, force-pushes, and most rebuilds themselves unless explicitly told otherwise. In auto mode you can build and commit, but err on the side of asking when destructive or strategic decisions are involved.

## 2. About the project

Cryo is a statically-typed, compiled programming language. Two compilers exist:

- **Bootstrap** (`bootstrap/`): the original C++23 implementation, ~LLVM-20 backend (clang++-20). Treated as **frozen fallback** — see §3.
- **cryoc** (`cryoc/`): the self-hosted compiler, written in Cryo itself. This is what gets shipped going forward. The output binary is named `cryo` (at `cryoc/build/bin/cryo`); the directory is still called `cryoc/` for now.

The compiler self-hosts at a true byte-level fixed point (verified — see §6).

### Cryo language quirks (don't fight these)

- **Variables MUST declare their type with `:`** — `const x: int = 10;` is required. No type inference shorthand on bindings.
- **Trait-method `Self` is spelled `This`** — `function len(this: This*) -> u64` for the implementing-type reference. Not `Self`.
- **No function-pointer parameters in the C++ bootstrap** — callback-style helpers fail codegen there. Keep match blocks inline rather than passing closures. (cryoc-the-compiler handles them fine; this is bootstrap-only.)
- **Struct literals**: `new T { field: value, ... }` syntax. cryoc has had bugs with field-init missing values in the past — fixed in commit `8a743407`.

## 3. Strategic direction — Path 2 → Path 3

The user has decided a path. Read this section before doing any compiler work.

### Where we are now (Path 2)

- **Daily dev**: bootstrap → stage-2 → stage-3, where stage-3 (`cryoc/build/bin/cryo`) is "the working compiler". `make cryo` produces this in ~5 minutes.
- **Verification gate**: `make selfhost-check` (8-stage chain through stage-5, ~10 min) verifies stage-4 == stage-5 byte-identical IR. Run before tagging or pre-commit, **not per-edit**.
- **Bootstrap is frozen**. Don't touch C++ code in `bootstrap/` unless something is genuinely broken. Two known harmless bootstrap quirks (see §6) push the fixed point from stage-3 to stage-4. **Don't fix them** — we're throwing bootstrap away soon.

### Where we're going (Path 3, near-term)

When the user tags 0.1.0:
- The known-good `cryo` binary will be **committed to the repo** (or distributed via GitHub Releases) as the new starting point for builds.
- The C++ bootstrap becomes archeology — kept around for "I have no cryo binary at all" emergencies.
- All future releases use **only** `cryoc` to build `cryoc`. Two stages instead of three: pre-built-cryo → cryoc → verify (== pre-built-cryo).
- At that point, the selfhost-check naturally drops to stage-2 vs stage-3 byte-identity (no bootstrap noise), and the chain shrinks.

**The thing to avoid:** spending time fixing bootstrap bugs that get thrown away in weeks. The user said: "I really want to avoid the bootstrap compiler as much as possible. I just want it to work enough so I can build with it if needed for 0.1.0, but then soon after, I will only use cryoc for future releases."

## 4. Repo layout

```
.
├── bootstrap/         C++ bootstrap compiler. FROZEN. Don't edit unless emergency.
│   ├── bin/cryo       The bootstrap binary (66MB, in .gitignore)
│   ├── include/, src/, libs/, scripts/, tests/
│   └── makefile       `cd bootstrap && make compiler` builds bin/cryo
├── cryoc/             Self-hosted Cryo compiler (written in Cryo)
│   ├── src/           cryoc source — `compiler/`, `CLI/`, `utils/`
│   ├── build/         stage-2 + stage-3 outputs (gitignored)
│   ├── build-s4/      stage-4 outputs (gitignored)
│   ├── build-s5/      stage-5 outputs (gitignored)
│   ├── cryoconfig     project_name = "cryo", target = executable
│   └── llvm_bindings.h
├── stdlib/            CURRENT stdlib (~25k LOC, 53 modules) — what cryoc compiles
├── new_stdlib/        FUTURE-spec stdlib (~10k LOC) — parked; cryoc can't compile it yet
├── tools/             CryoLSP, CryoFormat, CryoAnalyzer (all C++; deferred until post-0.1)
├── docs/              cryo.md, grammar.md, mangling-spec.md, syntax JSON (stale, needs audit)
├── examples/, scripts/, assets/
├── Makefile           Top-level orchestration (NEW this session — see §5)
├── install.sh         BROKEN — references dead `./bin/cryo` path (see §7 punch list)
└── README.md          BROKEN paths — references ./bin/cryo
```

No `.github/`. No `CHANGELOG.md`. No release artifacts yet.

### Stdlib decision

Per the user (this session): keep both stdlibs as-is for now. cryoc currently depends on `./stdlib`. `new_stdlib/` is parked — when cryoc gains the features it requires, `new_stdlib/` will replace `stdlib/` (and `stdlib/` will become `stdlib-legacy/`). For 0.1.0, only `./stdlib` ships. **Don't try to merge them or migrate now.**

## 5. Top-level Makefile (committed `de0a16fd` + `3d880760`)

Targets:
| Target | Time | What it does |
|---|---|---|
| `make help` | instant | Lists targets |
| `make bootstrap` | ~30s if cached, ~3min cold | Builds C++ bootstrap → `bootstrap/bin/cryo` |
| `make stdlib` | ~30s | Builds stdlib via bootstrap → `stdlib/.bin/libcryo.a` |
| `make cryo` | ~5min | Full daily build: stdlib + bootstrap → stage-2 → stage-3. Output: `cryoc/build/bin/cryo` |
| `make selfhost-check` | ~10min | 8-stage chain through stage-5; diffs stage-4 vs stage-5 IR for byte identity |
| `make clean` | instant | Wipes cryoc + stdlib build outputs (keeps bootstrap binary) |
| `make distclean` | instant | Also runs `make -C bootstrap clean` |

**Why 8 stages instead of 6**: Bootstrap (C++) has two harmless codegen quirks that bake into stage-3's IR but don't affect runtime. Real fixed point is stage-4. Building stage-5 confirms it. Going away once we drop bootstrap (Path 3). See §6 for the gory detail.

Variables in Makefile:
- `BOOT = $(ROOT)/bootstrap/bin/cryo`
- `STAGE2 = $(ROOT)/cryoc/build/cryo`
- `STAGE3 = $(ROOT)/cryoc/build/bin/cryo`
- `STAGE4 = $(ROOT)/cryoc/build-s4/bin/cryo`
- `STAGE5 = $(ROOT)/cryoc/build-s5/bin/cryo`

## 6. Self-host state

🟢 **The compiler self-hosts at a true byte-level fixed point at stage-4.**

- `bootstrap` (C++) → `cryoc/build/cryo` (stage-2)
- stage-2 → `cryoc/build/bin/cryo` (stage-3)
- stage-3 → `cryoc/build-s4/bin/cryo` (stage-4)
- stage-4 → `cryoc/build-s5/bin/cryo` (stage-5)

**stage-4 and stage-5 are byte-identical**: linked `cryo.ll` MD5 `a6b1a2910054e3cbad854b2bf53c6525` either way, all 103 per-module `.ll` files match, all 103 `.o` files match. 0 errors at every stage.

### Why not stage-3 == stage-4 (the bootstrap quirks)

Bootstrap emits stage-2 with two codegen quirks that bake into stage-3 IR but don't affect runtime:

1. **Dead `@FILE.str` globals**: `bootstrap/src/Codegen/Expressions/ExpressionCodegen.cpp:430-438` calls `CreateGlobalStringPtr` on every visit to a `FILE` identifier with no caching, AND something in bootstrap's monomorphization pipeline visits `FILE` identifier nodes ~3.5× more times than cryoc does. Result: stage-3 IR for `Compiler__CompileMode` has 3206 `@FILE.str` declarations but only 900 references — 2306 orphan globals. They become dead read-only data in the binary; nothing references them at runtime.
2. **Unmangled `@panic` call** in `std__prelude.o`: `bootstrap/src/Compiler/StandardPasses.cpp:2426` special-cases `panic`/`unreachable`/`abort`/`todo` and emits `call void @panic(...)` (bare name) where cryoc-built code emits `call void @"C$3std.7prelude.5panic$FS_S_j$Rv"(...)` (mangled). Linker resolves both correctly.

Stage-3's *behavior* is identical to stage-4's (same call counts, same definition counts). So stage-3 → stage-4 produces clean IR, and stage-4 → stage-5 confirms the fixed point. Per §3, **don't fix these in bootstrap** — we're dropping bootstrap soon.

### Path 3 transition: when bootstrap goes away

Once the user commits a known-good `cryo` binary as a starting point:
- Drop the 8-stage chain back to 4 stages: `pre-built-cryo` builds stdlib + cryoc → stage-A; stage-A builds stdlib + cryoc → stage-B; verify A == B byte-identical.
- The bootstrap-induced FILE.str / panic-mangling quirks vanish (they only existed because bootstrap emitted stage-2).
- Selfhost-check time drops back to ~5min.
- Update Makefile: replace `BOOT := $(ROOT)/bootstrap/bin/cryo` with `BOOT := $(ROOT)/path/to/committed/cryo`. Stages 1-2 collapse.

## 7. 0.1.0 readiness — punch list

Status of the punch list, ordered by what blocks the next thing.

**Done this session:**
- ✅ Top-level Makefile orchestration (commit `de0a16fd`)
- ✅ `selfhost-check` target with byte-identity gate (commit `3d880760`)
- ✅ cryoc binary renamed → `cryo` (commit `de0a16fd`)
- ✅ History rewrite: 70MB `bootstrap/bin/cryolsp` purged from all git history (was bloating clones forever)
- ✅ History rewrite: ~330MB of build/debug logs purged (`full_build_log_after_fix.txt` 84MB, `clean_build.txt` 76MB, `stdlib/debug2.txt` 52MB, etc.)
- ✅ History rewrite: all `*.old` backup files purged
- ✅ 12 stale local branches deleted (8 `claude/*` + `codegen-rewrite` + `lsp-rewrite` + `optimize-cryo-codegen-UUapt` + `stdlib-integration` — all 0 commits ahead of main)
- ✅ Repo size: was ~500MB+ in git (with cryolsp + logs), now `.git` is 8.1M, pack 7.4 MiB
- ✅ Force-push to `origin/main` happened (user authorized)
- ✅ `.gitignore` updated: `cryolsp` matches at any depth (was root-only `/bin/cryolsp`)
- ✅ Memory finding: stage-4 is the real fixed point, not stage-3

**Must do before tagging 0.1.0:**

1. **Fix the post-move dead references**: `README.md` and `install.sh:389` still reference `./bin/cryo` (pre-move). Quick win, ~15 min, but the repo currently doesn't pass a "follow the README and try it" smoke test. Both should reference `bootstrap/bin/cryo` for the bootstrap or (better) `cryoc/build/bin/cryo` for the self-hosted binary, depending on which the user wants users to encounter first.

2. **Decide and commit `0.1.0` cryo binary** (Path 3 prep). This is the user's near-term goal. Need to figure out:
   - Where does the binary live? (`bin/cryo` at root? `releases/v0.1.0/cryo`? GitHub Releases asset?)
   - How big is it? (Currently `cryoc/build/bin/cryo` = 2.6MB — small enough to commit if desired.)
   - What .gitignore changes are needed if it goes in-repo?
   - Update Makefile to support "use committed binary" as the bootstrap path.

3. **`install.sh` rewrite for the new layout**. Currently broken. Decide install layout — typical: `/usr/local/bin/cryo`, `/usr/local/lib/cryo/libcryo.a`, `/usr/local/include/cryo/`. Ship the cryo binary (not bootstrap) by default.

4. **CHANGELOG.md stub** with a 0.1.0 section.

5. **CI workflow** (single GitHub Actions YAML): on push/PR, run `make cryo` + `make selfhost-check`. Without this, regressions sneak in. Path 2 means selfhost-check is the gate — if stage-4 != stage-5, refuse the merge. Note: ~10min CI run is borderline; you can split into two jobs (`make cryo` fast for PRs, full `selfhost-check` only on main pushes).

**0.1.0 polish (nice-to-have):**

6. **README** must have an example that compiles and runs end-to-end (smoke test).
7. **`docs/cryo.md`, `docs/grammar.md`** accuracy pass — both predate recent compiler work.
8. **tools/ disposition**: in 0.1 or "coming in 0.2"? If they ship, they need build targets and install paths. If not, README must say so. Recommend deferring to 0.2.
9. **scripts/ audit**: `scripts/build-stdlib.py` was used by old layout, may not be relevant now.
10. **`cryo --help` audit**: make sure flags like `--build-dir=...`, `--debug`, `--ast` are either documented or hidden. Confirmed `cryo --version` works (returns "cryo 0.1.0").

**Post-0.1.0:**

- Real test suite. Current: only `cryoc/BattleTest.cryo.test` exists.
- Strip the now-redundant cloner workarounds in `cryoc/src/compiler/AST/cloner.cryo` (`clone_match_arm`, `clone_stmt` explicit kind dispatch) — only after Path 3 transition, when CI is guarding the fixed point.
- Package management story.
- Cross-compilation / Windows.
- Decide what to do with `bootstrap/` long-term (delete? archive? keep as escape hatch?).

## 8. Hard rules — carry forward

These came up across sessions; honor them.

- **No workarounds, fallbacks, or hacks.** Diagnose root causes; never relocate code "where it builds" to dodge a tooling bug. The vtable/cloner fixes in commits `f65e1737` and `d86f37ed` are canonical examples of doing it right.
- **No safe-fallback defaults for invariant violations.** Bail with a real diagnostic, don't silently substitute placeholders.
- **No fallback chains in lookups.** Bare-name DI lookups are reserved for C externs only; for Cryo types use `node.resolved_type` not name-based lookups.
- **No inline string manipulation in codegen.** Add a method to `CodegenContext`/`DeclarationIndex`/`InternTable` instead.
- **The user prefers foreground execution** for builds. Don't run the build chain as `run_in_background=true`. Run it synchronously.
- **The user handles all rebuilds and commits** by default. In auto mode, build/commit yourself, but err on the side of asking when in doubt — especially for destructive or strategic actions.
- **Don't push `--force` to `main`** unless the user explicitly authorizes that specific push.
- **Bootstrap is FROZEN.** Don't fix bootstrap bugs unless they actively block 0.1.0 shipping. The user is committed to Path 3 — bootstrap goes away soon.

## 9. Investigation discipline

- **Use IR for debugging — keep stage outputs in separate dirs.** `--build-dir=...` is the right tool. Pattern that works:
  - `cryoc/build/cryo.ll` (bootstrap-emitted stage-2) vs `cryoc/build/obj/<module>.ll` (stage-2-emitted) — for spotting "stage-2 emits this code wrong".
  - `stdlib/.bin-s2/obj/<module>.ll` vs `stdlib/.bin-s3/obj/<module>.o.pre.ll` — for spotting "stage-3 emits this code wrong".
- **Stop iterating blindly.** Each full chain is ~10 min. Add many probes per build cycle, then look — don't add one-line probes one at a time.
- **`gdb -batch -ex 'attach <PID>' -ex 'thread 1' -ex 'bt 30' -ex 'detach' -ex 'quit'`** works on stage-3 for live SIGSEGV/hang diagnosis.
- **Use `param_types_equal_structural` over `param_types_equal`** when comparing AST-derived types in cryoc — arena-dedup misses are common across resolution-context boundaries (this was one of yesterday's two bug fixes).

## 10. Build commands cheatsheet

```bash
ROOT=/workspaces/CryoLang     # adapt to your machine
cd $ROOT

# === Daily dev ===
make cryo                     # ~5min: full bootstrap -> stage-2 -> stage-3 -> cryoc/build/bin/cryo

# Sanity:
./cryoc/build/bin/cryo --version    # "cryo 0.1.0"
./cryoc/build/bin/cryo --help

# === Verification (pre-commit / pre-tag) ===
make selfhost-check           # ~10min: 8 stages, diffs stage-4 vs stage-5 IR

# === Cleanup ===
make clean                    # wipes cryoc + stdlib outputs, keeps bootstrap binary
make distclean                # also nukes bootstrap/bin/cryo + bootstrap intermediates

# === Bootstrap (only if bootstrap binary missing or you absolutely must rebuild) ===
make bootstrap                # ~3min cold, no-op if bootstrap/bin/cryo exists

# === Manual stage-by-stage (matches what selfhost-check does internally) ===
BOOT=$ROOT/bootstrap/bin/cryo
STAGE2=$ROOT/cryoc/build/cryo
STAGE3=$ROOT/cryoc/build/bin/cryo
STAGE4=$ROOT/cryoc/build-s4/bin/cryo

# 1. stdlib via bootstrap
cd $ROOT/stdlib && rm -rf .bin && mkdir -p .bin/obj && "$BOOT" build

# 2. cryoc via bootstrap → stage-2
cd $ROOT/cryoc && "$BOOT" build

# 3. stdlib via stage-2
cd $ROOT/stdlib && rm -rf .bin-s2 && mkdir -p .bin-s2/obj && "$STAGE2" build --build-dir=.bin-s2

# 4. cryoc via stage-2 → stage-3
cd $ROOT/cryoc && rm -rf build/obj build/bin && "$STAGE2" build

# 5. stdlib via stage-3
cd $ROOT/stdlib && rm -rf .bin-s3 && mkdir -p .bin-s3/obj && "$STAGE3" build --build-dir=.bin-s3

# 6. cryo via stage-3 → stage-4
cd $ROOT/cryoc && rm -rf build-s4 && "$STAGE3" build --build-dir=build-s4

# 7. stdlib via stage-4
cd $ROOT/stdlib && rm -rf .bin-s4 && mkdir -p .bin-s4/obj && "$STAGE4" build --build-dir=.bin-s4

# 8. cryo via stage-4 → stage-5 + verify byte identity
cd $ROOT/cryoc && rm -rf build-s5 && "$STAGE4" build --build-dir=build-s5
diff -q $ROOT/cryoc/build-s4/bin/cryo.ll $ROOT/cryoc/build-s5/bin/cryo.ll && echo "FIXED POINT OK"
```

## 11. Don'ts

- **Don't fix bootstrap bugs.** Per §3, bootstrap is going away. Two known harmless quirks (FILE.str dead globals, unmangled @panic) are documented in §6 — leave them. The 8-stage chain handles them correctly.
- **Don't try to make stage-3 == stage-4** without fixing bootstrap (which you shouldn't fix). The fixed point is at stage-4.
- **Don't push `--force` to `main`.** User authorizes individual force-pushes only.
- **Don't drop the `--build-dir=...` separation between stage outputs** — without it, IR diffs across stages are useless.
- **Don't add probes one or two lines at a time.** Each cycle is ~10 min. Batch a comprehensive set per build.
- **Don't strip the cloner workarounds** (`clone_match_arm`, `clone_stmt` explicit kind dispatch in `cryoc/src/compiler/AST/cloner.cryo`) until after 0.1.0 ships and CI is guarding the fixed point.
- **Don't merge `stdlib/` and `new_stdlib/`** — the user has a plan for that, it's not now.
- **Don't add features to bootstrap.** Frozen means frozen.
- **Don't try to add the `tools/` (CryoLSP, CryoFormat, CryoAnalyzer) to the 0.1.0 release** without asking — they're deferred to 0.2 unless the user says otherwise.

## 12. Recent commits worth knowing

- `3d880760` (this session) — `build: shift selfhost-check fixed point from stage-3 to stage-4`. Why: bootstrap noise (see §6).
- `de0a16fd` (this session) — `build: top-level Makefile + rename self-hosted binary cryoc -> cryo`. Adds Makefile, renames `project_name = "cryo"` in `cryoc/cryoconfig`, updates CLI banner in `cryoc/src/main.cryo:9`.
- `7281a6f4` (this session) — `.gitignore`: `cryolsp` now matches at any depth.
- `f65e1737` — `fix: enhance vtable slot index lookup with structural parameter type equality`. Fixed virtual dispatch breaking on overrides where param types had different arena IDs but same display name. Use `param_types_equal_structural` for AST-derived comparisons.
- `d86f37ed` — `fix: improve statement node cloning to avoid C++ vtable bug and enhance error reporting`. Cloner now uses explicit kind dispatch in `clone_match_arm`/`clone_stmt` to dodge a bootstrap C++ vtable issue. Workaround can be stripped post-0.1.0 once Path 3 is in.
- `8a743407` — `fix: initialize struct fields in struct literals to prevent uninitialized memory access`. Recent struct-init bug fix.

## 13. Why this handoff exists

Yesterday's session fixed two real cryoc bugs at the source level (vtable + cloner) and confirmed self-hosting reaches a byte-identical fixed point at HEAD. Today's session:

1. Cleaned up the repo (history rewrite, stale branches, broken references)
2. Created top-level build orchestration (`Makefile`)
3. Renamed `cryoc` binary → `cryo`
4. Discovered that the "byte-identical fixed point" was actually at stage-4 not stage-3 due to bootstrap codegen quirks; updated `selfhost-check` accordingly
5. Reached strategic alignment: Path 2 (selfhost-check at stage-4) → Path 3 (drop bootstrap entirely, ship pre-built cryo as starting point)

The next agent should focus on the §7 punch list, **not on bootstrap fixes**. The fastest path to 0.1.0 is README/install.sh fixes, then Path 3 transition (commit a cryo binary, retire bootstrap), then CI.

Good luck.
