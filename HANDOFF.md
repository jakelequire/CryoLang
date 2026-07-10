# HANDOFF — separate compilation was removed; here is what must come back

> **Read this first.** Branch `remove-sepcomp` was **hard-reset** to `d63bbb72`, the last
> commit before separate compilation began. The reset discarded 27 commits. Most were
> sepcomp and are gone on purpose. **Six were not**, and they are listed below.
>
> **Nothing is lost.** `main` still points at `c9b25eef`. Every commit named here is
> reachable: `git log main`, `git show <sha>`, `git cherry-pick <sha>`.
>
> Maintainer (Jake) wants **correct, principled** solutions, never expedient hacks or
> silent fallbacks. **Only Jake commits**; you may repin.
>
> (Not to be confused with `HANDOFF.md`, which is the *parallel-codegen* handoff and is
> unrelated to this work.)

---

## 0. Why the reset, and why `d63bbb72`

Separate compilation (`.crymeta` metadata + `.crygen` generic **source** slices + `.cryart`
bundles) was correct but unfinished. Finishing it properly meant giving Cryo a serialized,
name-resolved IR — weeks of work, itself gated on the mono-after-sema pipeline reorder.
Not a v1.0 problem. It was cut.

The cut point is **`d63bbb72`** ("feat: implement compile-once, link-twice optimization for
bin and lib projects"). Its child `cf8a2161` is the **true first sepcomp commit** — *not*
the one that created `compiler/src/compiler/sepcomp/` (`e9635cc7`). `cf8a2161` introduced
`is_external`, `prebuilt`, `nonexternal_source_for_namespace`, and the monomorphization
*placement inversion* in `instance.cryo`, `module_graph.cryo`, and
`passes/specialization.cryo`. Resetting to `e9635cc7^` would have left all of that behind.

Verified clean at `d63bbb72`: `crymeta`, `crygen`, `cryart`, `sepcomp`, `prebuilt`,
`is_external`, `DepArtifact`, `CRYO_EMIT_ARTIFACT` → **0 hits** across `compiler/src/`.

---

## 1. ALREADY RE-APPLIED — do not re-apply

| what | from | status |
|---|---|---|
| `init_native_target()` at the `create_target_machine_with_opt` chokepoint | `c9b25eef` | **applied, uncommitted** |

**Why it matters.** LLVM's target registry is per-process and starts empty.
`init_native_target()` used to run only inside per-module IR generation. An incremental
build whose modules are **all** cached codegens zero modules — so nothing registers a
target — yet a missing final binary still drives it on to link and, in test mode, to
test-main synthesis, which creates a target machine and dies:

```
error[E0900]: test-main synthesis: native target machine unavailable
[Codegen] Target lookup failed for triple '<host>': ... (no targets are registered)
```

…on a build whose sources never changed. It hit `collect_multimod` and `ffi_cpp_link` —
the only two `collect`-outcome projects, the only ones that synthesize a test main.

**Deterministic repro** (failing before, passing after — re-verified on this branch):
1. `cryo test` in `tests/tests/projects/collect_multimod` (cold) → pass
2. rerun (warm) → pass
3. delete **only** `build/collect_multimod-test[.exe]`, keep `build/target/**` objects → **failed before the fix**

The fix registers the backend at the single chokepoint every target machine is created
through, instead of trusting callers to have passed through codegen first. LLVM's
initializers guard on a `static once_flag`, so it is idempotent and free. **Do not "fix"
this by adding a fallback lookup elsewhere.**

---

## 2. MUST COME BACK — apply from `main`, in this order

Prefer `git cherry-pick` from `main` (authoritative). Standalone patch files were also
saved to the agent scratchpad under `scratchpad/keepers/`, but they are a convenience only.

### 2a. `2df466ad` — Windows self-hosting build paths + module path handling
Pure Windows/build-path work, no sepcomp content. **Apply first**; later items sit on it.

> ⚠️ This is the commit that makes per-module IR land in **nested** subdirectories under
> `ir/` (`ir/Compiler/AST/Cloner.ll`). That is what breaks `selfhost-check.py` — see §3.

### 2b. `ec1bba2b` — `fix(mono)`: register static generic method templates declared in impl blocks
Independent mono bug fix. No sepcomp content. Clean.

### 2c. `bb9cfaf0` — `feat(mono)`: place monomorphizations at their type argument, with `linkonce_odr`
**The codegen improvement Jake explicitly wants back.** It encodes the Rust rule: a
monomorphization lives with the module owning the concrete type argument that forced it.

> ⚠️ **Not clean.** It touches `module_graph.cryo` / `passes/specialization.cryo` and drags
> in `is_external` + `nonexternal_source_for_namespace`. With sepcomp gone there are **no
> external modules**, so the external-module skip is vacuous. Keep the placement rule;
> drop `is_external`, `is_owner_key_external()`, and the "non-external" qualifier on
> `nonexternal_source_for_namespace()` (it collapses to a plain namespace → source-path
> lookup). At the time of removal `is_external` had exactly **one writer** (the bundle
> ingest, now gone) and five readers — two in `specialization.cryo`, one each in
> `type_resolution.cryo` and `sema.cryo`, plus the `module_graph.cryo` helpers. All five
> become dead once it is always `false`; delete them rather than leaving them dangling.

### 2d. `61088f93` — `fix(codegen)`: give monomorph definitions `weak_odr` linkage in a comdat
**Apply after 2c — it depends on that placement.** It fixed real duplicate-definition
breakage (duplicate monomorph definitions; LLVM rejects the module / duplicate symbols at
link). If the placement rule goes back, this must too. If it does *not* go back, re-validate
before assuming this is safe to omit.

---

## 3. MUST COME BACK, BUT ONLY *WITH* §2a

### `scripts/selfhost-check.py` — `_compare_ir_trees._key` must use `as_posix()`

```python
# WRONG: on a Windows host str(Path) yields backslashes, so the "/ir/" split never
# matches; every key stays a full absolute path containing the stage dir
# (win-s3 vs win-s4) and the two sets can never be equal.
def _key(p: Path) -> str:
    return str(p).rsplit("/ir/", 1)[-1]

# RIGHT:
def _key(p: Path) -> str:
    return p.as_posix().rsplit("/ir/", 1)[-1]
```

**Do NOT apply this at `d63bbb72`.** Here `_compare_ir_trees` keys on `p.name`, and the
build emits a *flattened* IR name (`Compiler__AST__Cloner.ll`) alongside the nested one, so
basenames are unique and the comparator is correct. The `_key`/`rsplit` rewrite — and thus
the bug — arrives with the nested-IR layout in **§2a**. Re-apply `as_posix()` in the same
change that introduces `_key`.

**The tell** you've hit it: `✗ cannot compare windows IR: module sets differ (233 vs 233)`
— *equal counts, different name sets*. It means the Windows byte-identity fixed point has
never actually been compared. It only ever worked under wine from a Linux host, where paths
are already POSIX.

---

## 4. WANTED BACK, BUT RE-DERIVE — do not cherry-pick

### The orthogonal `test.json` schema (from `c9b25eef`)

The old schema fused two orthogonal things — the **fixture** (what you set up) and the
**assertion** (what you check) — so every (fixture × assertion) pair needed its own enum
variant and its own arm in `dispatch_project`. It reached seven outcomes:

```
collect | compile_fail | run | crymeta_roundtrip | crygen_e2e | crygen_fail | prebuilt_std_e2e
```

`crygen_e2e` and `crygen_fail` were *the same fixture*, differing only in whether the
consumer must run or must fail to build. That is the proof the axes were fused.

**Re-derive it, sepcomp-free:**

```
outcome:  collect | build | run

expect:   exit_code         exact status (default 0)
          fails             must exit non-zero; exit_code then ignored
          stdout_contains   substring of combined output
          output_contains   [] all must appear
          output_excludes   [] none may appear
          diagnostic        sugar: implies fails + appends error[<code>] to output_contains
```

`compile_fail` collapses into `build` + `expect.fails`, because `diagnostic: "E0200"` was
always just `output_contains: ["error[E0200]"]` in disguise.

> ⚠️ **Drop the fixture axis entirely.** `prepare` / `emit_artifact` / `argv` / `command` /
> `{crymeta}` / `find_suffixed()` exist only to serve sepcomp and have **zero users** once
> it is gone. The saved patch `KEEP-harness-schema-refactor-CONTAINS-SEPCOMP.patch`
> contains them — do not apply it verbatim.

One portability lesson worth preserving: the old sepcomp arms used `(cd X && VAR=1 …)` and
`rm -rf`, which `cmd /c` cannot run — which is why `crygen_e2e` and `crymeta_roundtrip`
were **silently red on every native Windows host**. Any future fixture must set env with
`env::set_var` and change directory with `chdir` **in the runner process**, never through
shell syntax.

---

## 5. Deliberately NOT coming back

All pure sepcomp: `cf8a2161`, `e9635cc7`, `a64ae200`, `b3723b32`, `04d5c78c`, `39a427a9`,
`2ba49789`, `f2c46166`, `566061a3`, `2c7def13`, `1cc336ca`, `2ddc8d22`, `a9a3001a`,
`fe865a97`, `7dd371c0`, `8cf703a7`, `c917197f`, `a5b7262f`, `d46bf2c2`.

Plus three repin chores (`c9599741`, `6033a243`, `7f54ba5a`) — **never cherry-pick a
repin**; regenerate with `make pin`.

Thirteen test projects went with them: `assoc_e2e`, `coherence_fail_e2e`, `crygen_e2e`,
`crymeta_roundtrip`, `drop_e2e`, `methods_e2e`, `multimod_e2e`, `ops_e2e`, `overload_e2e`,
`prebuilt_std_e2e`, `primmethod_e2e`, `traits_e2e`, `visibility_e2e`. The projects suite is
back to **6**.

> **Coverage gap this opens — worth closing.** Those projects were *phrased* as `lib/` +
> prebuilt `consumer/`, but what they actually tested was the **language**: associated
> types, drop semantics, methods, multi-module resolution, operator overloading, overload
> resolution, primitive methods, traits, visibility, and trait coherence — **across a
> module boundary**. Only their single-module unit tests remain.
>
> They can be rebuilt as ordinary **source path-dependencies**, which do work. The gotcha:
> the dependency's `cryoconfig` must declare a `[lib]` section (`name` + `source_dir`),
> otherwise `DepResolver::harvest_roots` contributes no source root and the consumer fails
> with `error[E0500]: cannot find module ...`. The old fixtures declared only `[project]`,
> because prebuilt mode never needed it.

---

## 6. Traps that cost real time — do not rediscover them

- **`make test` does NOT rebuild the compiler.** `$(STAGE2)` and `$(LIBCRYO_A)` are bare
  file targets with **no source prerequisites**; the Makefile says so outright: *"Run
  `make cryo` first to pick up compiler changes."* Edit compiler source, run `make test`,
  and you silently exercise the **previous** binary. Always:
  ```sh
  rm -f compiler/build/cryo stdlib/.bin/libcryo.a && make cryo && make test
  ```
  and **prove it**: `stat -c '%y %n' <edited source> compiler/build/cryo` — the binary must
  be newer than the source. This single trap made a real bug look intermittent for hours.

- **Cross-OS artifact clobber.** `stdlib/.bin/libcryo.a` and `tests/helpers/lib*.a`
  alternate between ELF and PE/COFF when you build on Windows and then in WSL against the
  same tree. Delete them when switching host; `make test` will not notice and the link
  fails with confusing `undefined reference` errors.

- **`make selfhost-check` can exit 0 while skipping Windows entirely.** Count the
  successes: `grep -c 'FIXED POINT OK'` must be **2** (Linux + Windows). From a Linux host
  the wine path returns `"skip"` (not `"fail"`) when `.toolchains/llvm-mingw/bin/clang.exe`
  and `llvm-ar.exe` are absent. To get both fixed points **without** that download, run the
  gate from the **Windows** host: `selfhost-check.py` drives the Linux six stages through
  WSL and then runs the Windows six natively against `bin/cryo.exe` (needs `CRYO_CC=gcc`).
  To re-check only the Windows half after the stages are built, import the script and call
  `run_windows_selfhost([])`.

- **`make cryo-exe` is a gate `selfhost-check` does not cover** — the Windows selfhost
  stages only compare IR; they never link `cryo.exe`.

- **Never `wsl --shutdown` while detached WSL processes are alive.** It wedges
  `WSLService`; afterwards *every* `wsl.exe` call hangs — including `wsl --shutdown` itself
  and `make pin`, which blocks on `pin-windows.cmd`'s first WSL call (`wslpath -a "%CD%"`)
  before printing anything of its own, with `Ctrl+C` swallowed by the `for /f` subshell.
  Recovery needs **elevation**: `Stop-Process -Name wslservice,vmmemWSL -Force`.
  `Restart-Service` also hangs — it waits for an acknowledgement the wedged service never
  sends. Also: WSL2 balloons its VM across repeated builds and never returns the memory to
  Windows; that is not a compiler leak.

- **Do not force `CRYO_CC=gcc` on `make pin`** — it breaks the `cryo.exe` cross-link.

---

## 7. State of this branch

- `HEAD` = `d63bbb72` + the `llvm_types` native-target fix (§1), **uncommitted**.
- `main` = `c9b25eef`, untouched. Everything above is recoverable from it.
- Pinned `bin/cryo` / `bin/cryo.exe` are the ones committed at `d63bbb72`; `verify-pin` is
  **OK**, and the tree bootstraps as-is.
- **Re-pin after landing §1 and anything from §2** — the pins no longer match the source
  once those go in. A pin from a dirty worktree is not reproducible
  (`verify-pin.py --require-clean` is the release gate): commit first, then re-pin clean.

### Verified at `d63bbb72` + §1 (Windows host)

```
unit           1418 passed, 0 failed
compile-fail    124 passed, 0 failed
projects          4 passed, 2 skipped (cxx, display)
OVERALL        PASS
```

### Gate checklist before committing anything

```sh
rm -f compiler/build/cryo stdlib/.bin/libcryo.a && make cryo   # $(STAGE2) will not rebuild itself
make test                                                      # projects: 6
make selfhost-check                                            # require "FIXED POINT OK" x2
make cryo-exe                                                  # separate gate
cd compiler && CRYO_CTX_AUDIT=1 ../bin/cryo build --no-incremental 2>&1 | grep -c FOREIGN-CTX   # must be 0
make pin                                                       # NOT with CRYO_CC=gcc
```
