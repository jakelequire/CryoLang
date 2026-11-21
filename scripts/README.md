# scripts/

One line per script: what it does, and who calls it. `make help` lists the
targets; this lists what is behind them.

A script with no caller in the Makefile, CI or another script is deleted. A
one-off codemod lands in the commit that ran it and is deleted in the next
one; the history keeps it.

Every gate is a `cryogate` subcommand (`tools/CryoGate`); none is a script.

## Release

| script | what it does | called by |
|---|---|---|
| `build-release.sh` | Builds and stages the Linux tarball (static, stamped `cryo` and `cryolsp`; `--dynamic` for a local test tarball no release ships) or the Windows zip under `dist/`, marks the stdlib and runtime trees shipped, then smokes it with `cryogate release-smoke` | `make release`, `make release-linux`, `make release-windows`, CI, release |
| `install-dependencies.sh` | Shipped at the root of every Linux install: builds and runs a hello-world with the installed compiler, and when the link fails offers to install gcc and the C library headers with the package manager | `install.sh`, the user |
| `install-dependencies.ps1` | Shipped at the root of every Windows install: the same check, offering llvm-mingw for the current user through winget | `install.ps1`, the user |

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
