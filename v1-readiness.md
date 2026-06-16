# Cryo v1.0.0 Readiness Tracker

> Living checklist for the 1.0.0 release. Rebuilt from the **2026-06-15 deep repo audit**
> (7-domain parallel audit: CI/DevOps, compiler internals, stdlib, docs, tests/examples,
> language design, CLI/package-manager — every headline claim re-verified against source).
> Status legend: `[ ]` todo · `[~]` in progress · `[x]` done · `[?]` needs a decision

## Snapshot (2026-06-15)

- **Verdict:** ~90% ready. The gap to 1.0 is **freeze discipline + a few Windows correctness gaps + one over-promised feature** — *not* missing capability.
- **Compiler internals: no implementation blockers.** Zero error-swallowing found across ~95K LOC, 1077 diagnostic-emission sites, clean architecture.
- `VERSION = "1.0.0"` in `compiler/src/main.cryo`; README/CHANGELOG say 1.0.0; pinned `cryo.exe` reports `version 1.0.0`.
- HEAD = `7787c96c`; committed pins = `7c2abfb7-dirty` (one commit behind **and** dirty).

### Decisions — 2026-06-15 (DECIDED by maintainer, rounds 1–3)

Maintainer chose the ambitious "build it properly" option on nearly every open question. These are now committed direction (not `[?]`):

| Area | Decision | Implied work |
| --- | --- | --- |
| **Package manager** | **Fix all 4 gaps, freeze as stable** | Windows `![target]`-gate `deps/` shell-outs; semver/lockfile/resolver tests; remove-or-implement `alias`; document semver subset in `cryo help config` |
| **`env` mutators** | **Convert to `Result<(), IoError>`** | `set_var`/`remove_var`/`set_current_dir` via `IoError::from_errno` |
| **`Iterator<Item>`** | **Switch to associated type** (`Iterator { type Item; }`) | Large compiler + stdlib rework; reshapes every iterator/adapter signature; unblocks capturing-closures-in-combinators |
| **`fs::Path`** | **Make separator handling platform-aware now** | Target-gate `\`/`/` so `file_name`/`parent`/`push` work on Windows |
| **Codegen miscompile cluster** | **Investigate → repro → decide per-bug** | Minimal repros for the 4 bugs, root-cause, then fix-vs-document each |
| **`base64::decode`** | **Switch to `Result<_, Base64Error>`** | Add `encoding/error.cryo` |
| **`async`/`await`/`yield`/`typeof`** | **Reject at parse/sema** | Clear "reserved / not in v1.0" diagnostic; remove reliance on late codegen E0600 |
| **HashMap `Entry<K,V>`** | **Ship in 1.0** | Implement occupied/vacant `Entry` API |
| **Operator overloading** | **Add arithmetic traits + wire `[]`/`*` sugar** | `Add`/`Sub`/`Mul`/`Eq`/… traits + lower `container[i]`/`*ptr` to existing `Index`/`Deref` |
| **Trait objects (`dyn`)** | **Post-1.0 roadmap** (door open, not "never") | Document as a future possibility |
| **String/Str surface** | **Build out full surface now** | `replace`/`chars`/`lines`/`insert`/`remove` + Unicode `to_lowercase`/`to_uppercase` (keep `to_ascii_*`) |
| **Repo hygiene** | **Remove `tools/CryoFormat`; generate TLS key at test-time** | (Keep `legacy/` in main; leave `*.txt` gitignore behavior) |

> ⚠️ This is a **large pre-tag workload**, not a polish pass — assoc-type `Iterator`, operator traits, full String, deps hardening, `Entry`, `base64`/`env`→`Result`, `fs::Path`, plus the codegen investigation. Sequence deliberately.

### Progress — 2026-06-15 (automated pass, validated on native Windows)

Done & validated this pass (stdlib changes compiled + run-tested via `bin/cryo.exe`):
- ✅ **LLVM license bundling** (Tier 0) — vendored `LLVM-LICENSE.txt` (Apache-2.0 + LLVM exceptions, LLVM 20), `build-release.sh` now stages it into `THIRD_PARTY_LICENSES/` in both archives with a hard fail-if-missing check.
- ✅ **`AF_INET6` Windows fix** (Tier 1) — target-gated (unix 10 / windows 23); IPv6 DNS no longer dropped on Windows.
- ✅ **`random::from_os` Windows link fix** (Tier 1) — extracted a `![target]`-gated `fallback_seed()` (Win32 `QueryPerformanceCounter`+`GetCurrentProcessId`); link-tested: a Windows exe calling `Rng::from_os()` builds and runs.
- ✅ **`IoError` rendering** (Tier 4, partial) — added `IoErrorKind::label()` + `IoError::label()` (slug). Full `Display`/`message()` still open.
- ✅ **Doc-drift** (Tier 3) — README prelude list (7→12), README iterator-adapter roadmap, `core/iter.cryo` docstring, `lib.cryo` math + random manifest entries all corrected to match the code.
- ✅ **Release version single-source** (Tier 2) — release gate now also asserts CHANGELOG latest header == `main.cryo` VERSION.
- ✅ **Pre-publish test gate** (Tier 2) — new `test` job re-runs the suite (O2 + O0 + goldens) on the tagged commit; `publish` now `needs: [linux, windows, test]`.
- ✅ **CI cancel-in-progress** scoped to PRs so `main` push runs always complete.
- ✅ **`.gitignore`** — removed the redundant duplicate ignore block (kept `*.txt` semantics; that's a policy call).
- ℹ️ **O0 matrix already exists** in CI (`ci.yml` runs `make test ARGS="--opt-level=0"`) — the residual nit is only that `tests/cryoconfig` itself is O2-only.

Needs a Linux build to fully confirm: the **unix arms** of the `dns.cryo`/`rng.cryo` gating (logic unchanged from before, just relocated under `![target(unix)]`; validated the Windows arms here).

---

## Tier 0 — Blockers (a clean, honest tag depends on these)

- [ ] **Package manager is dead on Windows (a supported target).** `compiler/src/compiler/deps/` (~1223 LOC) shells out POSIX-only via `system()` — `cache.cryo:128/141` (`mkdir -p` / `rm -rf`, single-quoted), `git.cryo` (`2>/dev/null`). **Zero `target()` gating in the entire `deps/` tree** (the CLI binary-exec path *is* gated — follow that pattern). `cryo fetch`/`update`/git-deps fail under `cmd`.
    - Also: **zero tests**, **no example uses `[dependencies]`**, no committed lockfile; `alias` is a parsed-but-never-consumed schema key (`project_config.cryo:130`); semver supports only exact + caret `^` (`semver.cryo`); `cryo help config` doesn't document the dep-table grammar.
    - **DECIDED → fix all 4 gaps, freeze as stable.** (1) Windows `![target]`-gate the `deps/` shell-outs (`cache.cryo`/`git.cryo`) following the CLI's gating pattern; (2) add a semver/lockfile/resolver test suite; (3) remove-or-implement the dead `alias` key; (4) document the supported semver subset in `cryo help config`. All four are blockers for the stable freeze.
- [ ] **Committed pins fail the release gate.** `bin/cryo.pin.txt` / `cryo.exe.pin.txt` record `worktree: dirty`; `make verify-pin-clean` (run by `release.yml`) exits 1 → a `v1.0.0` tag will not publish. Pins are also at `7c2abfb7`, so the pinned compiler lacks the HEAD `match-const-pattern` fix. **Re-pin from a clean tagged commit**, verify sha256 sidecars.
- [x] **LLVM license not bundled.** ✅ Done — `LLVM-LICENSE.txt` vendored (LLVM 20 Apache-2.0-with-LLVM-exceptions); `build-release.sh` `stage_third_party_licenses()` copies it into `THIRD_PARTY_LICENSES/` in both the linux tarball and windows zip, with a hard error if the file is missing. `.gitignore` re-includes it past the `*.txt` rule.

## Tier 1 — Correctness bugs (fix or consciously accept before tag)

- [ ] **Tuple-of-owned-elements drop leak.** `codegen/visit/call_emitter.cryo:1266` — drop glue for bare tuples isn't emitted ("returning true… no-op"); a `(String, String)` leaks its elements. Untested (`tests/tests/stdlib/tuple.cryo` has no drop case). Tuples are a flagship 1.0 feature.
- [x] **Stdlib Windows link gap — `random::from_os` fallback won't link.** ✅ Done — extracted `![target]`-gated `fallback_seed()` in `random/rng.cryo` (unix: `clock_gettime`+`getpid`; windows: `syscall::QueryPerformanceCounter`+`GetCurrentProcessId`). Link-tested on Windows (exe calling `Rng::from_os()` builds + runs). *(Linux arm unchanged logic; confirm on a Linux build.)*
- [x] **Stdlib Windows correctness — IPv6 DNS dropped.** ✅ Done — `net/dns.cryo` `AF_INET6` now `![target]`-gated (unix 10 / windows 23); `AF_INET` is 2 on both. Compiles on Windows.
- [ ] **Self-host codegen miscompile cluster (the one compiler ship-gate).** The source *dances around* latent backend bugs: vtable-offset on base-pointer field writes, consecutive-cast-and-call, virtual-dispatch-on-`Type*` SIGSEGV, match-arm casts. Workarounds in ~5 files; fragile. **DECIDED → investigate: build minimal repros for each, root-cause, then fix-vs-document per bug.** (Could not be reproduced in the 06-14 audit, so the investigation itself is the de-risking step.)
- [ ] **async/await/yield/typeof → DECIDED: reject at parse/sema.** AST nodes exist, codegen errors late (E0600). Move rejection earlier with a clear "reserved / not supported in v1.0" diagnostic.

## Tier 2 — Release process & CI

- [ ] **Add a native `windows-latest` CI job.** All jobs are `ubuntu-latest`; `cryo.exe` is cross-built and only smoke-tested under **wine** (not real Windows: DLL loader, paths, console code pages, and the dead deps subsystem all go unexercised). Back the "full Windows support" decision with native CI (at minimum `cryo.exe --version/--help` + `stdlib` build + `cryo test`; ideally selfhost). **Deferred from the automated pass:** this is the one CI item that needs iteration against the real runner (LLVM-C.dll provisioning + `CRYO_CC=gcc` toolchain on the runner), so shipping it blind risks a broken/red job. The native flow itself is confirmed working — this whole readiness pass built the stdlib and ran programs natively via `bin/cryo.exe`.
- [x] **Release workflow publishes without re-running tests on the tagged ref.** ✅ Done — added a `test` job (verify-pin → build → `make test` O2 + O0 → examples-golden); `publish` now `needs: [linux, windows, test]`. *(selfhost-check still not in the release gate — optional to add.)*
- [x] **Version single-source-of-truth.** ✅ Partial — release gate now asserts CHANGELOG latest header == `main.cryo` VERSION (the two authoritative sources). README still has version mentions (incl. the `--version=1.0.0` *pin example* at line 66) not machine-checked — left as-is since they're illustrative; reconsider the pin example separately.
- [x] **CI `cancel-in-progress` scoped to PRs** (was MEDIUM finding M1) — `main` push runs now always complete.
- [ ] **Confirm `cryo-lang.org/install.sh` (+ `install.ps1`) is live** and serving current scripts before tagging — headline install one-liner depends on it. Add a link-check / document the raw-GitHub fallback URL.
- [?] **Installer authenticity (`curl|bash`).** `install.sh`/`install.ps1` verify the archive against a **same-origin** `.sha256` (integrity, not authenticity). Consider cosign/minisign signatures, or at minimum document the trust model.
- [~] **`.gitignore` cleanup.** ✅ Partial — removed the redundant duplicate ignore block (`*.o`/`*.a`/`*.exe`/`*.dll`/`*.out`/`*.log`/`*.obj` re-listed). Left the blanket `*.txt` rule's *behavior* unchanged (narrowing it is a repo-policy call — decide whether future `.txt` fixtures should auto-ignore).
- [x] `concurrency:` group on CI to cancel superseded runs. *(Carried from prior audit — note: scope `cancel-in-progress` to PRs so `main` push runs always complete.)*

## Tier 3 — Documentation accuracy

- [x] **Fix doc-drift that UNDERsells the implementation.** ✅ Done:
    - README "Beyond 1.0" rewritten: adapters `.map/.filter/.take/.chain/.enumerate/.zip` ship & compose; the real residual limitation (re-adapting an opaque-typed local) is stated instead.
    - `stdlib/core/iter.cryo` docstring rewritten to describe the adapters it actually defines.
    - README prelude list corrected 7→12 to match `prelude.cryo`.
- [x] **Reconcile `stdlib/lib.cryo` manifest with code.** ✅ Done — math entry now lists the real C-style names (`sqrt`/`ln`/`sin`/`fabs`/…); random entry now describes `SecureRng::fill`/`try_fill` instead of the nonexistent `secure_bytes`.
- [ ] README minor: Hello-World snippet doesn't match what `cryo init` actually generates (`import std::fmt;` + `fmt::printf`). *(Note: the `cryo raw` CLI-table row was intentionally NOT added — see CLI cleanup below.)*
- [ ] **Add `SECURITY.md`** (10-line disclosure process — GitHub flags its absence). *(Deferred per maintainer — reconsider.)*
- [ ] **Add `CODE_OF_CONDUCT.md`**. *(Deferred per maintainer.)*
- [ ] (Adoption, can be 1.0.x) Getting-started tutorial / "Cryo book".
- [ ] (Adoption, can be 1.0.x) Generated stdlib API reference (or at least a `docs/stdlib.md` index).

## Tier 4 — Stdlib API surface (irreversible at 1.0 — decide names now)

- [ ] **`env` error-style divergence → DECIDED: convert to `Result<(), IoError>`.** `set_var`/`remove_var`/`set_current_dir` (`env/_module.cryo:68/87/129`) via `IoError::from_errno`. (`var → Option` kept deliberately.)
- [ ] **`fs::Path` POSIX-only → DECIDED: make separator handling platform-aware now.** Target-gate `\`/`/` so `file_name`/`parent`/`push` work on Windows (`path.cryo:25`).
- [ ] **`base64::decode` returns `Option` → DECIDED: switch to `Result<_, Base64Error>`** (`encoding/base64.cryo:55`; add `encoding/error.cryo`).
- [~] **`IoError` rendering.** ✅ Partial — added `IoErrorKind::label()` + `IoError::label()` (stable diagnostic slugs, mirroring `JsonErrorKind::label`), validated on Windows. Still open: a full `Display`/`message()` that combines the slug with `os_code` (needs the `fmt` Display trait wiring; additive, 1.0.x-safe).
- [?] **Platform-gate convention: `linux` vs `unix`.** `random/secure.cryo:48`, `math/_module.cryo`, `fmt/float.cryo` gate on `linux` while `time`/`thread`/`env` use `unix`. This *defines the platform contract* — if v1 is Linux+Windows only, fix the docs to say "Linux"; otherwise `linux` gates break macOS/BSD.
- [ ] **`Path::join` / `Path::extension` missing** (additive, 1.0.x-safe — but `join` is fundamental enough to feel like a gap).
- [ ] **`String`/`Str` surface → DECIDED: build out the full surface now.** Add `replace`/`chars`/`lines`/`insert`/`remove` + Unicode `to_lowercase`/`to_uppercase` (keep `to_ascii_*` as the ASCII ops).
- [ ] **`Iterator<Item>` → DECIDED: switch to associated type** (`Iterator { type Item; }`). Large compiler + stdlib rework; reshapes every iterator/adapter signature and unblocks capturing-closures-in-combinators. **Syntax + migration designed → `associated-types-plan.md`** (declare `type Item;`, project `X::Item`, positional `Iterator<T>` sugar everywhere, etc.). Adds associated types as a new type-system feature (Iterator-only rollout).
- [ ] **HashMap entry API → DECIDED: ship `Entry<K,V>` in 1.0** (occupied/vacant).

## Permanent language/design decisions (freeze deliberately)

- [x] **DECIDED — `unsafe` no-op is the 1.0 contract** (future-enforcement promise removed; `docs/cryo.md §6.12`).
- [x] **DECIDED — per-module error enums; no unifying `Error` trait.**
- [x] **DECIDED — tuples as `(T,U)` / `()` unit; `[T,U]` removed.** *(Verified done: `parse_bracket_tuple_type` removed, stdlib migrated, `E0104` negative test exists.)*
- [x] **DECIDED — full `i128`/`u128`.** *(Verified done at O0/O2.)*
- [ ] **Trait objects (`dyn`) → DECIDED: post-1.0 roadmap.** Document as a future possibility (door open), not a permanent "never".
- [ ] **Operator overloading → DECIDED: add arithmetic traits + `[]`/`*` sugar.** Add `Add`/`Sub`/`Mul`/`Eq`/… operator traits AND lower `container[i]`/`*ptr` to the existing `Index`/`Deref` for user types.
- [?] **`?`-with-error-conversion:** `?` requires the same error type (no `From`-driven conversion) → real code nests `match` 5-6 deep. Document the limitation or add `From`-conversion post-1.0 (additive).

## Testing improvements

- [ ] **Strengthen the negative-test matcher.** `commands.cryo:1468` asserts only `strstr(out,"error[Exxxx]")` — ignores span, message, **and the process exit code**, and tolerates extra unrelated codes. Add per-file expected-line/code annotation or assert sole-error + non-zero exit.
- [ ] **Add an O0 (no-opt) test matrix.** Suite runs O2-only despite the config comment saying O0 is now viable; optimizer-masked codegen bugs won't surface.
- [ ] **Test `process::signal` + `Child.kill`/`send_signal`/`try_wait`** (entirely untested; POSIX-only with a Windows stub — highest-risk untested cross-platform surface).
- [ ] **Test the package manager** (semver parsing/constraints, lockfile round-trip, resolver cycle detection) — currently zero coverage (tied to Tier-0 deps decision).
- [ ] Thin iterator-combinator coverage: `for_each`/`any`/`all`/`find` (1 ref each) lack short-circuit/empty cases.
- [ ] (Stretch) Golden IR/AST snapshot tests for monomorphizer + optimizer.

## Examples polish

- [ ] **f-strings have zero example coverage** despite being a flagship feature advertised in `01-hello` (all examples use `printf`). Convert at least one example.
- [ ] **No example uses iterator combinator chains** (`.map`/`.filter`) — gated by the closures-into-generic-methods deferral. Add one, or confirm the scope cut and note it.

## CLI cleanup

- [ ] **Remove the `cryo raw` command for 1.0** — `--no-std` is the supported way to compile without the stdlib/prelude (fully wired: `commands.cryo:965/1773`, `cryoconfig` `no_std` key). `raw` is a redundant top-level command surface that should NOT freeze into 1.0. Remove `CommandKind::Raw` and its ~8 sites in `compiler/src/CLI/commands.cryo` (enum, dispatch `:534`, parse `:510`, help `:148/165`, `cmd_raw` `:1569`), plus the `docs/cryo.md §24.1` entry. *(Code change — needs a compiler rebuild + repin to validate; not done in the automated pass. Already removed from the README CLI table.)*

## Repo hygiene

- [x] **DECIDED — keep `legacy/` in `main` for 1.0** (not archived).
- [ ] **DECIDED — remove `tools/CryoFormat`** (experimental Rust prototype, not-in-1.0). Delete from the 1.0 tree.
- [ ] **DECIDED — generate the TLS test key at test-time** — stop committing `tests/fixtures/tls/key.pem`; produce it during the test run (and gitignore it).
- [x] **Keep raw asset sources** (`.ai`/`.eps`) — maintainer decision.
- [ ] Confirm `legacy/bootstrap/libs/cjson` is first-party (only matters if `legacy/` ships).

---

## False positives caught during the audit (do NOT chase)

- **"switch/case is documented-but-unimplemented"** — FALSE. Fully implemented: `parser.cryo:2832` (`parse_switch_statement`), AST `SwitchStmtNode`, codegen `ir_generator.cryo:1662`.
- **"Committed build artifacts under `examples/*/build/`"** — FALSE. Those dirs exist on disk but are gitignored; nothing tracked.

## Open decisions

All major freeze decisions were **closed on 2026-06-15** — see the **Decisions** table near the top. Remaining genuinely-open items are minor:

1. **Platform-gate `linux` vs `unix` naming** — some stdlib gates say `linux` while docstrings say "unix". Given macOS is out of v1 scope, the `linux` gates are functionally fine; this is a doc-consistency cleanup, not a contract decision.
2. **README Hello-World snippet** vs `cryo init` output (cosmetic).
3. **Negative-test matcher shape** — extend `![config(negative, CODE)]` with a message/span assertion (test-DSL change; pick the shape when implementing).
4. Confirm `cryo-lang.org` is live; confirm `legacy/bootstrap/libs/cjson` provenance (only matters since `legacy/` stays).
