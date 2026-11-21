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
| `build-deb.sh` | Packages a Linux release tarball as `dist/cryo_<version>-<revision>_amd64.deb`: the tree in `/usr/lib/cryo`, `/usr/bin` links, the C toolchain as a dependency (and shared LLVM's, named `-dynamic`, for a `--dynamic` tarball) | release, the user |
| `build-msi.sh` | Packages the Windows release zip as a per-user MSI with wixl (`packaging/cryo.wxs`, the wizard in `packaging/cryo-ui.wxs`, the dependency step's script `packaging/dependency-step.js`, the zip's tree harvested by wixl-heat) | release, the user |
| `check-msi.sh` | Judges a built MSI against its zip: the same files, the PATH actions inside the install transaction, the dependency step in the wizard (never in the install's own sequence, which runs with the Windows Installer service's environment), and the wizard's pages | release, the user |
| `build-winget.sh` | Writes a release MSI's winget manifests (`CryoLang.Cryo`, llvm-mingw as a dependency) under `dist/winget/` | release, the user |
| `user-setup.ps1` | Shipped at the root of every Windows install, and run as the installing user: adds the install's `bin` to the User PATH, and on removal takes it out and deletes the archives the install built in the user's cache | `install.ps1`, the MSI |
| `build-static-libclang.sh` | Builds libclang's C API, with the clang libraries it needs, as one archive under `.toolchains/libclang-static/` for the static Linux compiler (`[link.release-static]`) | `build-release.sh linux` |
| `install-dependencies.sh` | Shipped at the root of every Linux install: builds and runs a hello-world with the installed compiler, and when the compiler refuses it with E0705 (no C toolchain) offers to install gcc and the C library headers with the package manager | `install.sh`, the user |
| `install-dependencies.ps1` | Shipped at the root of every Windows install: the same check, with the PATH a new terminal gets, offering llvm-mingw for the current user through winget (`-Yes` installs without asking, `-No` only reports, `-Log` writes the report to a file) | `install.ps1`, the MSI's wizard, the user |

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
