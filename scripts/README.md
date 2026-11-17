# scripts/

One line per script: what it does, and who calls it. `make help` lists the
targets; this lists what is behind them.

A script with no caller in the Makefile, CI or another script is deleted. A
one-off codemod lands in the commit that ran it and is deleted in the next
one; the history keeps it.

## Gates

| script | what it does | called by |
|---|---|---|
| `verify.py` | Builds once (cryogate too, which runs the gates already ported to it), runs census, examples, lsp-check, cross-check, incremental-instance-check, incremental-runtime-check, runtime-abi-check and check-fast concurrently, hashes every object (and every runtime archive member), and with `--baseline REV` compares them against a compiler and runtime built from REV, whose objects and verdicts it records per program and runs again only for a program whose inputs changed, reading a baseline census run through `cryogate census --read`; `--selftest` drives its judges | `make verify`, `make verify-selftest` (so `make check-fast` and CI) |
| `lsp-gate.py` | Builds `tools/CryoLSP` with the compiler under test into its own directory, then runs that binary through `tools/CryoLSP/tests/smoke.py` (protocol checks and every fixture's checks) | `make lsp-check`, `verify.py` (lsp), CI |
| `incremental-check.py` | An edit to an included C header is not reported up to date; incremental build == clean build, byte for byte, over an edit matrix (refuses on Windows after the header check) | `make incremental-check`, CI |
| `selfhost-check.py` | Byte-identity fixed point of the compiler and of the stdlib archives its stages build: the Linux chain, the Windows chain natively or under wine; on a Windows host both at once, the Linux one from a digest-checked copy of its sources on WSL's own filesystem (`--mirror`) | `make selfhost-check`, `selfhost-check-windows.cmd`, CI |
| `selfhost-check-windows.cmd` | Windows host: checks for Python and WSL, then runs `selfhost-check.py` | `make selfhost-check` (Windows) |

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
| `install-lsp-windows.cmd` | Windows host: packages `tools/CryoAnalyzer` and installs it into VS Code, replacing the installed extension; the server itself is built, installed and checked by `make install-lsp` before it runs | `make install-lsp` (Windows) |
