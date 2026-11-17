# scripts/

One line per script: what it does, and who calls it. `make help` lists the
targets; this lists what is behind them.

A script with no caller in the Makefile, CI or another script is deleted. A
one-off codemod lands in the commit that ran it and is deleted in the next
one; the history keeps it.

## Gates

| script | what it does | called by |
|---|---|---|
| `lsp-gate.py` | Builds `tools/CryoLSP` with the compiler under test into its own directory, then runs that binary through `tools/CryoLSP/tests/smoke.py` (protocol checks and every fixture's checks) | `make lsp-check`, `cryogate verify` (lsp), CI |

## Release

| script | what it does | called by |
|---|---|---|
| `build-release.sh` | Builds and stages the Linux tarball (static, stamped `cryo`) or the Windows zip under `dist/`, then smokes it with `cryogate release-smoke` | `make release`, `make release-linux`, `make release-windows`, CI, release |

## Toolchain

| script | what it does | called by |
|---|---|---|
| `llvm-version.env` | The one place the LLVM version is pinned; CI cache keys hash it | CI, release, `ci/install-llvm.sh`, `fetch-windows-llvm.sh` |
| `ci/install-llvm.sh` | Installs and caches LLVM on the CI runner | CI, release |
| `fetch-windows-llvm.sh` | Downloads the Windows LLVM-C and libclang import libraries and DLLs into `.toolchains/llvm-win` | CI, release, by hand before `make pin` |

## By hand

| script | what it does | called by |
|---|---|---|
| `install-lsp-windows.cmd` | Windows host: packages `tools/CryoAnalyzer` and installs it into VS Code, replacing the installed extension; the server itself is built, installed and checked by `make install-lsp` before it runs | `make install-lsp` (Windows) |
