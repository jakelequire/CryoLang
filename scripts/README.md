# scripts/

One line per script: what it does, and who calls it. `make help` lists the
targets; this lists what is behind them.

A script with no caller in the Makefile, CI or another script is deleted. A
one-off codemod lands in the commit that ran it and is deleted in the next
one; the history keeps it.

## Gates

| script | what it does | called by |
|---|---|---|
| `verify.py` | Builds once, runs census, examples, lsp-check, cross-check, incremental-instance-check and check-fast concurrently, hashes every object, and with `--baseline REV` compares them against a compiler built from REV; `--selftest` drives its judges | `make verify`, `make verify-selftest` (so `make check-fast` and CI) |
| `test-census.py` | Runs `cryo test` and asserts each suite's count against `tests/test-roster.txt`; a unit test gated to another OS is excused by `roster-check.py`'s own reading of the `![target]` gates | `make test-census`, `verify.py` (census), CI |
| `roster-check.py` | Checks the discovered unit tests, projects and negative files against `tests/test-roster.txt`; `--merge` / `--update` re-pin it | `make roster-check`, CI |
| `examples-gate.py` | Builds every `examples/` project; refuses a sweep that found none, or fewer than `--min` | `make examples`, `verify.py` (examples), CI |
| `check-examples-output.sh` | Builds and runs four examples and diffs their stdout against `examples/<name>/expected.out` (bash, POSIX only) | `make examples-golden`, CI |
| `valgrind-check.sh` | Runs the same four examples under valgrind (Linux only) | `make valgrind-check`, CI |
| `lsp-gate.py` | Builds `tools/CryoLSP` with the compiler under test into its own directory, then runs that binary through `tools/CryoLSP/tests/smoke.py` (protocol checks and every fixture's checks) | `make lsp-check`, `verify.py` (lsp), CI |
| `cross-check.py` | Compiles runtime, stdlib, compiler and CryoLSP for the other OS's triple, objects only from a Windows host (a Linux host links the windows-gnu build and needs mingw plus the LLVM-C import lib) | `make cross-check`, `verify.py` (cross), CI (windows native) |
| `incremental-check.py` | Incremental build == clean build, byte for byte, over an edit matrix (refuses on Windows) | `make incremental-check`, CI |
| `incremental-instance-check.py` | An edit asking a cached module for a new generic instance still links and reuses the cache | `make incremental-instance-check`, `verify.py` (incr), CI |
| `vendor-consts-gate.py` | Registers a fixture C header under a throwaway `$CRYO_HOME`, then builds and runs a consumer of every constant `cryo vendor` emitted | `make vendor-check`, CI |
| `selfhost-check.py` | Six-stage byte-identity fixed point: the Linux chain, the Windows chain natively or under wine | `make selfhost-check`, `selfhost-check-windows.cmd`, CI |
| `selfhost-check-windows.cmd` | Windows host: checks for Python and WSL, then runs `selfhost-check.py` | `make selfhost-check` (Windows) |
| `api-index.py` | Generates `docs/stdlib-api.txt` from the stdlib archive's symbols and a source scan; `--check` fails if it is stale | `make api-index`, `make api-index-check`, CI |

## Pins

| script | what it does | called by |
|---|---|---|
| `cryo-pin.py` | Copies (and strips) a built compiler into `bin/` and writes its `.pin.txt` sidecar | `make pin` (Linux) |
| `verify-pin.py` | Each pin's sha256 and stamped commit match its sidecar, both sidecars name one commit, and the Windows pin carries no 32-bit base relocation (one over a thread-local's offset faults at run time); `--require-clean` for a release; `--selftest` drives the checks through fixtures | `make verify-pin`, `make verify-pin-clean`, `make verify-selftest`, CI, release |
| `pin-windows.cmd` | Windows host: re-enters this Makefile inside WSL to run `make pin` | `make pin` (Windows) |

## Release

| script | what it does | called by |
|---|---|---|
| `build-release.sh` | Builds and stages the Linux tarball (static, stamped `cryo`) or the Windows zip under `dist/`, then smokes it | `make release`, `make release-linux`, `make release-windows`, CI, release |
| `release-smoke.sh` | Compiles and runs hello-world with the staged release tree | `build-release.sh` |

## Toolchain

| script | what it does | called by |
|---|---|---|
| `llvm-version.env` | The one place the LLVM version is pinned; CI cache keys hash it | CI, release, `ci/install-llvm.sh`, `fetch-windows-llvm.sh`, `selfhost-check.py` |
| `ci/install-llvm.sh` | Installs and caches LLVM on the CI runner | CI, release |
| `fetch-windows-llvm.sh` | Downloads the Windows LLVM-C and libclang import libraries and DLLs into `.toolchains/llvm-win` | CI, release, by hand before `make pin` |

## By hand

| script | what it does | called by |
|---|---|---|
| `annotate-negative-tests.py` | Writes `//~` annotations into compile-fail files from a real `cryo check` run, keeping only ones that verify | a person adding a negative test |
| `check-cost.py` | Wall time, peak memory and AST arena figures of `cryo check` over the compiler, for one or more compilers interleaved; deleted with `docs/class-removal-plan.md` | a person measuring a class-removal slice |
| `install-lsp-windows.cmd` | Windows host: packages `tools/CryoAnalyzer` and installs it into VS Code, replacing the installed extension | `make install-lsp` (Windows) |
