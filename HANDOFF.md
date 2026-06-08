# Windows Cross-Compile Handoff

This doc captures the state of the Windows cross-compilation work as of
end-of-session 2026-06-07.  Pick up here tomorrow.

## TL;DR

Cryo programs that don't use `process` / `thread` / `sync` / `net` now
cross-compile to runnable Windows `.exe`s.  **12 of 14 stock examples
build for x86_64-pc-windows-gnu and run correctly under wine.**

The self-hosted compiler itself cross-compiles to valid Windows COFF
objects.  The only thing stopping `cryo.exe` from existing is the
linker can't find `LLVM-20` for Windows.

## Verify in one command

After a fresh shell:

```sh
# Sanity: host build still byte-identical to before today's work.
make selfhost-check

# Pick any of the working examples to confirm cross-compile path:
cd examples/04-calculator && rm -rf build
CRYO_STDLIB=$PWD/../../stdlib ../../bin/cryo build --target=x86_64-pc-windows-gnu --no-incremental
wine build/04-calculator.exe
```

Working cross-compile examples: `01-hello 02-fizzbuzz 03-fibonacci
04-calculator 05-todo-cli 06-word-count 07-shapes 08-game-of-life
09-json-config 10-expr-interpreter 12-guessing-game 13-closures`.

Not yet working: `11-http-server` (needs Winsock), `14-threads` (needs
CreateThread).

## What's committed before today

Jake committed an earlier batch with: Phase 1 (smoke test), Phase 2a
(va_list virtualization), Phase 3 (Windows-gnu linker driver).

## What's NOT yet committed (today's work, waiting on your commit)

```
compiler/llvm_bindings.h
compiler/src/compiler/codegen/abi.cryo                      (Phase 2b: Win64 classifier)
compiler/src/compiler/codegen/context.cryo
compiler/src/compiler/codegen/llvm_types.cryo
compiler/src/compiler/codegen/ops/expr_ops.cryo
compiler/src/compiler/codegen/ops/intrinsic_emitter.cryo
compiler/src/compiler/codegen/passes.cryo
compiler/src/compiler/codegen/visit/expr_dispatch.cryo
compiler/src/compiler/diag/renderer.cryo                    (Phase 4d: render_to_string per-OS)
compiler/src/compiler/instance.cryo
compiler/src/compiler/project_config.cryo                   (Phase 5: per-triple cache)
stdlib/core/intrinsics.cryo                                 (Phase 4d: popen/pclose/isatty/readlink/tmpfile gates)
stdlib/env/_module.cryo                                     (Phase 4a: set_var/remove_var)
stdlib/ffi/libc.cryo                                        (errno + isnan/finite + _putenv_s gates)
stdlib/fmt/float.cryo                                       (fp_is_nan/fp_is_inf locals)
stdlib/fs/file.cryo                                         (mirror_permissions gate)
stdlib/fs/metadata.cryo                                     (symlink_metadata gate)
stdlib/io/stdio.cryo                                        (Phase 4b: stream_lock/stream_unlock SRWLOCK)
stdlib/math/_module.cryo                                    (is_nan/is_infinite/is_finite gates)
stdlib/random/secure.cryo                                   (fill_secure_bytes gate)
stdlib/time/clock.cryo                                      (Phase 4c.time: read_monotonic/realtime/sleep_for)
bin/cryo, bin/cryo.pin.txt                                  (re-pinned)
```

`git diff` will show all of these.  `make selfhost-check` passes; the
IR is byte-identical between stage-3 and stage-4 on the host side, so
none of these edits change host-build behaviour.

## Phases done today

| # | Phase | Notes |
|---|---|---|
| 2b | Win64 ABI classifier | `AbiClassifier.classify_param`/`classify_return` branch on `is_win64()`; sizes 1/2/4/8 → Direct coerced; others → ByVal/SRet.  No DirectPair, no HFA. |
| 4a | POSIX-only stdlib gating | Per-function gating, NOT per-module (per your call).  See pattern below. |
| 4b | Windows backends for fs/env/io::stdio | env::set_var/remove_var via `_putenv_s`; fs::file::mirror_permissions no-op on Windows; fs::metadata::symlink_metadata falls back to stat; io::stdio uses SRWLOCK in place of pthread_mutex |
| 4c.time | time::clock cross-platform | Instant→QueryPerformanceCounter; SystemTime→GetSystemTimePreciseAsFileTime+epoch shift; sleep→Sleep with sub-ms round-up |
| 4d | Compiler-touched intrinsics | popen/pclose/isatty/readlink/open_memstream all gated; render_to_string uses tmpfile-based path on Windows |
| 5 | Per-triple build cache | `build/target/<profile>/<triple-or-host>/<origin>/...` so host and Windows builds coexist |

## Big landmine you'll re-discover otherwise: per-function gating pattern

**ConfigGating does NOT recurse into struct method bodies.**  This is
called out in `compiler/src/compiler/passes/config_gating.cryo:370`.

The "two methods with `![target(...)]`" pattern silently fails — both
bodies coexist post-gating, the one with unresolved symbols fails
name resolution.

**Don't:**
```cryo
type struct Foo {
    ![target(unix)]
    do_thing(&this) -> i32 { ... uses linux symbol ... }
    ![target(windows)]
    do_thing(&this) -> i32 { ... uses win32 symbol ... }
}
```

**Do:**
```cryo
type struct Foo {
    do_thing(&this) -> i32 { return do_thing_impl(this); }
}

![target(unix)]
private function do_thing_impl(f: &Foo) -> i32 { ... }
![target(windows)]
private function do_thing_impl(f: &Foo) -> i32 { ... }
```

Free functions ARE gated correctly.  Examples in the wild:
`Renderer::render_to_string` (renderer.cryo), `Instant::now` /
`SystemTime::now` / `sleep` (time/clock.cryo), `Stdin/Stdout/Stderr::lock`
+ `Drop` impls (io/stdio.cryo).

A future cleanup is to re-enable method recursion in
`config_gating.cryo` — would let the simpler form work.  Out of scope
for the port.

## What's left (priority order for `cryo.exe`)

### 1. `compiler/cryoconfig` per-target `[link]`

Today the project's `[link]` section hardcodes Linux paths:
```toml
[link]
system = ["LLVM-20"]
search = ["/usr/lib/llvm-20/lib"]
```

For windows-gnu cross we need either:
- Extend the cryoconfig parser to support `[link.unix]` / `[link.windows]`
  sections (modify `ProjectConfig` parser around the existing `[link]` handler).
- Or accept CLI `--link-search=PATH` / `--link-lib=NAME` overrides
  (simpler; less elegant).

Either way: when the user cross-compiles, the `[link]` line in
`run_linking` (`passes.cryo`) should use the target-specific libs and
search paths, not the Linux ones.

### 2. libLLVM-20 for windows-gnu  (external dependency)

Cryo's compiler binary links against libLLVM-20.  For `cryo.exe` we need
the windows-gnu equivalent.  Options:

  (a) Build LLVM 20 from source for `x86_64-pc-windows-gnu` — multi-hour
      compile; produces `LLVM-C.dll` + import lib.

  (b) Grab the official LLVM Windows distribution and use its
      `LLVM-C.dll` (mingw can link against MSVC-built DLLs given an
      import library; LLVM distributes one).

  (c) Statically link a small LLVM subset — most complex.

`cryo.exe` users will need `LLVM-C.dll` installed alongside (or
LD_LIBRARY_PATH/PATH set), unless we go fully static.

Once (1) and (2) are in place:

```sh
cd compiler && rm -rf build
CRYO_STDLIB=$PWD/../stdlib ../bin/cryo build --target=x86_64-pc-windows-gnu --no-incremental
# Should produce: build/cryo.exe
wine build/cryo.exe --version
```

Should work, because the front-end already produces valid Windows COFF
for all 127 local + 28 std modules (already verified).

### 3. Remaining stdlib OS-bound modules

End-user programs that use these don't cross-compile yet:

  - `sync::Mutex` / `RwLock` / `CondVar` / `Once` / `Barrier` / `mpsc`
    — port `pthread_*` → Win32 `SRWLOCK` + `CONDITION_VARIABLE`.
    The Win32 surface is already declared in `ffi/syscall.cryo`.
  - `thread::spawn` / `JoinHandle` / `scope` / `local` — pthread_create
    → `CreateThread`; pthread_key → TLS slots.
  - `process::Command` — fork+execve → `CreateProcess` + `CreatePipe`.
    Substantial work because the model is different (CreateProcess
    bundles the spawn+exec as one call; the pre-exec fd-massage hooks
    map differently).
  - `net::TcpStream` / `TcpListener` / `UdpSocket` / `dns` / `tls` / `ws`
    / `http2` — BSD sockets → Winsock with `WSAStartup`/`WSACleanup`,
    `SOCKET` handle (not int fd), `WSAGetLastError` instead of errno.
  - `random::Rng` (the non-secure xoshiro256**) — only `from_os`
    seeding is OS-specific, and that already routes through
    `random::secure::fill_secure_bytes` which IS ported.
  - `test::runner` — fork-per-test → CreateProcess re-exec.  Will
    require designing an argv flag like `--test-runner-child=<idx>`.

None of these block `cryo.exe` itself.  The compiler's only OS-specific
needs (env, io::stdio, fs::file, libc::system, intrinsics::popen) are
already ported.

### 4. Release-artifact pinning (Phase 8 — design work)

Per your no-clutter constraint, `bin/cryo.exe` should NOT live in the
repo.  Sketch: CI on tag cross-builds `cryo.exe` + windows-targeted
`libcryo.a` (+ possibly `LLVM-C.dll`) and uploads them as GitHub
release artifacts.  `install.ps1` (or similar) on Windows fetches by
version.  `scripts/cryo-pin.py` may need a per-OS pin-map mode.

This is mostly design + scripting; no compiler/stdlib changes.

## Key context files for tomorrow-you

- **`~/.claude/projects/-home-kiji-Programming-apps-CryoLang/memory/project_cryo_windows_port.md`**
  — full state-of-the-port notes including all gated symbols, the
  per-function-gating pattern, and conventions.  My memory loads this
  on every Claude session so I have continuity.

- `docs/abi.md` §7 — original "Multi-target plug-in" design that
  Phase 2a/2b implements.

- `compiler/src/compiler/passes/config_gating.cryo` — the pass that
  evaluates `![target(...)]` gates.  The comment at line 370 is the
  source of truth on why method-level gating doesn't work today.

- `compiler/src/compiler/codegen/abi.cryo` — AbiClassifier with the
  Win64 branches.  Header comment block has a phased-rollout history.

- `compiler/src/compiler/codegen/passes.cryo` —
  `linker_config_for_triple` + `profile_bin_path_for_target` +
  `bin_artifact_path_for_target` + `with_exe_suffix`.  Run_linking
  and run_linking_singlefile both consult them.

- `stdlib/ffi/syscall.cryo` — Win32 API surface (kernel32, user32,
  advapi32, ntdll).  Use this for ANY native Windows function.  For
  msvcrt C-runtime aliases (`_popen`, `_isatty`, `_putenv_s`,
  `_aligned_malloc`) use `stdlib/ffi/libc.cryo` instead.

## How to resume

```sh
git status                          # see uncommitted work
git diff --stat                     # quick file-by-file summary
make selfhost-check                 # ~2 min; confirms no host regression
```

For continued porting, the established pattern is in
`stdlib/time/clock.cryo` and `stdlib/io/stdio.cryo` — copy that shape
when handling sync / thread / process.

Re-pin after compiler changes:
```sh
make cryo && make pin-cryo
```

You enabled re-pinning for this session; the pin is already up to date.

---

Good night.  Tomorrow's biggest wins: (1) cryoconfig per-target `[link]`
(small change, unblocks Phase 6), (2) one of sync/thread (start with
`sync::Mutex` — small, demonstrates the SRWLOCK pattern that thread can
then build on).
