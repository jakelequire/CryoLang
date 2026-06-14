# Cryo top-level build orchestration.
#
# The compiler is self-hosted.  The pinned binaries at bin/cryo (Linux
# ELF) and bin/cryo.exe (Windows PE) bootstrap every other target —
# every build flows through one of them depending on the host OS.
#
# Targets:
#   stdlib           Build the standard library via bin/cryo
#   cryo             Build the self-hosted compiler via bin/cryo
#   pin              Refresh both pins (host-aware; Windows uses WSL)
#   selfhost-check   Host-aware byte-identity gate
#   install          Symlink bin/cryo + stdlib system-wide (delegates to install.sh)
#   uninstall        Remove the install.sh symlinks
#   clean            Remove compiler + stdlib build outputs

ROOT       := $(CURDIR)
PIN        := $(ROOT)/bin/cryo
STAGE2     := $(ROOT)/compiler/build/cryo
LIBCRYO_A  := $(ROOT)/stdlib/.bin/libcryo.a

# ---- Host detection ---------------------------------------------------
# Decides which native flow runs (Linux vs Windows) and where WSL has to
# step in.  Two probes:
#
#   $(OS): native Windows GNU Make (chocolatey, msys2, git-bash, ...) and
#     mingw/msys2 bash all set this to `Windows_NT` from the inherited
#     environment.  On Linux / macOS / inside WSL it's empty.
#
#   uname -s: only consulted off Windows so we don't end up shelling out
#     to cmd from a native-make build.  Distinguishes Linux from macOS.
#     WSL Linux reports plain `Linux` (the Microsoft kernel is opaque to
#     uname) and therefore takes the Linux branch — which is correct: a
#     `make` invoked inside WSL runs the build natively, not via the
#     Windows host's WSL re-entry path.
ifeq ($(OS),Windows_NT)
    HOST_OS := windows
    UNAME_S := Windows_NT
else
    UNAME_S := $(shell uname -s 2>/dev/null || echo Unknown)
    ifeq ($(UNAME_S),Linux)
        HOST_OS := linux
    else ifeq ($(UNAME_S),Darwin)
        HOST_OS := macos
    else
        HOST_OS := unknown
    endif
endif

# ---- Windows cross-build/pin ------------------------------------------
# cryo.exe is cross-built with the mingw-w64 toolchain and linked against
# the windows libLLVM-C import lib fetched into .toolchains/llvm-win by
# scripts/fetch-windows-llvm.sh.  The link is dynamic (LLVM-C.dll), mirroring
# how bin/cryo expects a system libLLVM-20.so.
WIN_TRIPLE   := x86_64-pc-windows-gnu
STAGE2_EXE   := $(ROOT)/compiler/build/cryo.exe
PIN_EXE      := $(ROOT)/bin/cryo.exe
MINGW_GCC    := x86_64-w64-mingw32-gcc
MINGW_STRIP  := x86_64-w64-mingw32-strip
WIN_LLVM_LIB := $(ROOT)/.toolchains/llvm-win/lib/libLLVM-C.dll.a
WIN_LLVM_DLL := $(ROOT)/.toolchains/llvm-win/bin/LLVM-C.dll

# C-side helpers for the ABI tests.  Compiled with the host cc to a
# static archive that `tests/cryoconfig` links via -L./helpers
# -labihelpers — see tests/helpers/abi_helpers.c for the contract.
TEST_HELPERS_DIR := $(ROOT)/tests/helpers
TEST_HELPERS_C   := $(TEST_HELPERS_DIR)/abi_helpers.c
TEST_HELPERS_O   := $(TEST_HELPERS_DIR)/abi_helpers.o
TEST_HELPERS_A   := $(TEST_HELPERS_DIR)/libabihelpers.a

# `nproc` is POSIX-only; on Windows make (cmd as recipe shell) the
# `$(shell)` call would spam "system cannot find the path specified" from
# the bash-flavoured 2>/dev/null redirect.  Just default to 4 there.
ifeq ($(HOST_OS),windows)
    NPROC := 4
else
    NPROC := $(shell nproc 2>/dev/null || echo 4)
endif

# The LSP binary grows a `.exe` suffix on a Windows host (the self-hosted
# compiler appends it for windows targets), so the build output and its pin
# both carry it there; empty elsewhere.
ifeq ($(HOST_OS),windows)
    EXE_SUFFIX := .exe
else
    EXE_SUFFIX :=
endif
LSP_BUILD_DIR := $(ROOT)/tools/CryoLSP/build
LSP_BIN       := $(LSP_BUILD_DIR)/cryolsp$(EXE_SUFFIX)
LSP_PIN       := $(ROOT)/bin/cryolsp$(EXE_SUFFIX)

EXT_DIR       := $(ROOT)/tools/CryoAnalyzer
EXT_ID        := cryolang.cryo-analyzer
EXT_VSIX      := $(EXT_DIR)/cryo-analyzer.vsix

.DEFAULT_GOAL := help
.PHONY: help stdlib cryo cryo-exe selfhost-check test test-list examples pin \
        pin-linux-impl pin-windows-impl _pin-windows-do \
        install uninstall clean lsp install-lsp release release-linux release-windows

help:
	@echo "Cryo build targets:"
	@echo "  make stdlib            Build the standard library via bin/cryo"
	@echo "  make cryo              Build the self-hosted compiler via bin/cryo"
	@echo "  make lsp               Build the Cryo-language LSP server (bin/cryolsp)"
	@echo "  make install-lsp       Package + install the CryoAnalyzer VS Code extension"
	@echo "  make pin               Refresh both pins (bin/cryo + bin/cryo.exe)"
	@echo "                         Linux host: builds natively; Windows host: via WSL."
	@echo "  make selfhost-check    Host-aware byte-identity gate."
	@echo "                         Linux host: 6-stage chain + optional wine Windows."
	@echo "                         Windows host: native cryo.exe pre-check + the"
	@echo "                         Linux 6-stage chain via WSL."
	@echo "  make test              Run the repo-level test suite (tests/) via cryo test"
	@echo "  make test-list         List the discovered test cases without running them"
	@echo "  make examples          Compile every examples/*/ project (CI smoke gate)"
	@echo "  make cryo-exe          Cross-build cryo.exe (x86_64-pc-windows-gnu)"
	@echo "  make install           Symlink bin/cryo + stdlib system-wide (sudo)"
	@echo "  make uninstall         Remove the install.sh symlinks"
	@echo "  make clean             Remove compiler + stdlib build outputs"
	@echo ""
	@echo "Detected host: $(HOST_OS) ($(UNAME_S))"

# ---- guard: pin must exist --------------------------------------------
$(PIN):
	@echo "ERROR: $(PIN) does not exist."
	@echo "       Every build target drives off the committed pin."
	@echo "       Check out a revision that has bin/cryo committed."
	@exit 1

# ---- stdlib via the pinned self-hosted compiler -----------------------
# Both branches drive off the committed pin; only the host shell differs.
# Windows recipes run under cmd (no `rm`, no forward-slash exec), so they
# use `bin/cryo.exe` with backslash separators and the cmd `rmdir` builtin.
# The pin auto-detects the stdlib from its own location (it sits beside
# `stdlib/`), so no CRYO_STDLIB is needed - same as the Linux recipe.
ifeq ($(HOST_OS),windows)
stdlib:
	@echo "==> Building stdlib via bin/cryo.exe"
	@if exist "stdlib\.bin" rmdir /s /q "stdlib\.bin"
	@cd stdlib && "$(subst /,\,$(PIN_EXE))" build
else
stdlib: $(PIN)
	@echo "==> Building stdlib via bin/cryo"
	@rm -rf stdlib/.bin
	@cd stdlib && "$(PIN)" build
endif

# ---- self-hosted compiler via the pin ---------------------------------
ifeq ($(HOST_OS),windows)
cryo: stdlib
	@echo "==> Building self-hosted cryo via bin/cryo.exe"
	@cd compiler && "$(subst /,\,$(PIN_EXE))" build
	@echo "==> Self-hosted cryo built: $(STAGE2_EXE)"
else
cryo: stdlib
	@echo "==> Building self-hosted cryo via bin/cryo"
	@cd compiler && "$(PIN)" build
	@echo "==> Self-hosted cryo built: $(STAGE2)"
endif

# File-target rule so downstream targets (test, lsp) can depend on the
# binary itself instead of the phony `cryo` target. If the binary is
# present, Make treats it as up-to-date and skips the rebuild.
$(STAGE2):
	@$(MAKE) --no-print-directory cryo

# Windows counterpart: the native build emits `cryo.exe`, so a Windows-host
# `test`/`test-list` depends on this path instead of $(STAGE2).  Same
# delegate-to-`cryo` shape; `cryo` is host-branched so it builds correctly.
$(STAGE2_EXE):
	@$(MAKE) --no-print-directory cryo

# File-target rule for the stdlib static library so `test` rebuilds it
# when stdlib/.bin has been wiped (e.g. by selfhost-check) but the
# compiler binary still exists. Without this, `make selfhost-check &&
# make test` fails at link with "cannot find libcryo.a".
$(LIBCRYO_A):
	@$(MAKE) --no-print-directory stdlib

# ---- unified pin refresh ----------------------------------------------
# `make pin` is the single canonical entry point: it refreshes both
# bin/cryo (Linux ELF) and bin/cryo.exe (Windows PE), routing through
# the right toolchain for whichever host you're on.
#
#   Linux host:   builds native cryo, then cross-builds cryo.exe via the
#                 mingw-w64 toolchain + the .toolchains/llvm-win import
#                 lib.  If the cross toolchain isn't installed, the
#                 Windows half is gracefully skipped (with a hint).
#
#   Windows host: delegates the whole job to WSL because (a) a native
#                 Linux ELF can only be produced by a cross toolchain
#                 not generally installed on Windows, and (b) the native
#                 cryo.exe cross-link currently exceeds cmd.exe's 8KB
#                 command-line limit at link time — both paths already
#                 work cleanly inside WSL.  Fails loudly with an
#                 install hint if WSL isn't present.
#
# `cryo-exe` (the cross-build of cryo.exe alone) remains available for
# scripts that want just the Windows binary without touching bin/cryo.

# Cross-compile the self-hosted compiler to cryo.exe (mingw-w64 + the
# .toolchains/llvm-win import lib).  Requires the cross toolchain + the
# fetched windows libLLVM-C; prints an actionable hint and fails if absent.
cryo-exe: cryo
	@command -v $(MINGW_GCC) >/dev/null 2>&1 || { echo "ERROR: $(MINGW_GCC) not found (install gcc-mingw-w64-x86-64)."; exit 1; }
	@test -f "$(WIN_LLVM_LIB)" || { echo "ERROR: windows libLLVM-C import lib missing at"; echo "       $(WIN_LLVM_LIB)"; echo "       Run: scripts/fetch-windows-llvm.sh"; exit 1; }
	@echo "==> Cross-building cryo.exe ($(WIN_TRIPLE)) via the self-hosted compiler"
	@cd compiler && "$(STAGE2)" build --target=$(WIN_TRIPLE) --no-incremental
	@echo "==> cryo.exe: $(STAGE2_EXE)"

ifeq ($(HOST_OS),windows)
# Windows host: refresh both pins by delegating to WSL.  The Linux
# branch of this Makefile runs there and does the real work.
#
# The recipe is a single command (`scripts\pin-windows.cmd`) so it works
# unchanged whether you invoked `make` from cmd / PowerShell (recipe shell
# is cmd) or from MSYS2 / Git Bash (recipe shell is bash).  The wrapper
# checks for wsl.exe, resolves the WSL path, and re-enters this Makefile
# inside WSL.
pin:
	scripts\pin-windows.cmd
else
# Linux/macOS host: native build for bin/cryo, cross-build for bin/cryo.exe.
# The Windows half is skipped (not failed) when the mingw toolchain or
# the fetched .toolchains artifacts are absent, so a Linux-only checkout
# can still refresh the Linux pin without dragging in the Windows bits.
pin: pin-linux-impl pin-windows-impl

pin-linux-impl: cryo
	@python3 scripts/cryo-pin.py --source "$(STAGE2)" --pin "$(PIN)"

pin-windows-impl:
	@if command -v $(MINGW_GCC) >/dev/null 2>&1 && [ -f "$(WIN_LLVM_LIB)" ]; then \
		$(MAKE) --no-print-directory _pin-windows-do; \
	else \
		echo "==> [skip] bin/cryo.exe pin: Windows cross-toolchain absent."; \
		command -v $(MINGW_GCC) >/dev/null 2>&1 \
			|| echo "       missing: $(MINGW_GCC) (install gcc-mingw-w64-x86-64)"; \
		[ -f "$(WIN_LLVM_LIB)" ] \
			|| { echo "       missing: $(WIN_LLVM_LIB)"; \
			     echo "         run: scripts/fetch-windows-llvm.sh"; }; \
	fi

_pin-windows-do: cryo-exe
	@python3 scripts/cryo-pin.py --source "$(STAGE2_EXE)" --pin "$(PIN_EXE)" --strip-tool $(MINGW_STRIP)
	@cp -f "$(WIN_LLVM_DLL)" "$(ROOT)/bin/LLVM-C.dll" 2>/dev/null \
		&& echo "==> Runtime LLVM-C.dll copied to bin/ (gitignored)" || true
endif

# ---- Cryo-language LSP server -----------------------------------------
# Builds tools/CryoLSP/ (entirely Cryo source) into bin/cryolsp.
# Depends on the stage-2 binary as a file so an existing compiler build
# is reused. Run `make cryo` first if you want to pick up compiler changes.
ifeq ($(HOST_OS),windows)
# Windows host: recipes run under cmd, which has no `cp`.  `$(PIN)` (bin/cryo,
# no extension) launches via PATHEXT -> bin/cryo.exe.  The built binary is
# `cryolsp.exe`; copy it to the pin with the `copy` builtin (backslash paths,
# stdout silenced).  `$(STAGE2)` is the Linux ELF and isn't a real
# prerequisite here, so this target stands alone.
lsp:
	@echo "==> Building CryoLSP via bin/cryo.exe"
	cd tools/CryoLSP && "$(PIN)" build
	copy /Y "$(subst /,\,$(LSP_BIN))" "$(subst /,\,$(LSP_PIN))" >NUL
	@echo "==> bin/cryolsp.exe ready"
else
lsp: $(STAGE2)
	@echo "==> Building CryoLSP via bin/cryo"
	@cd tools/CryoLSP && "$(PIN)" build
	@cp "$(LSP_BIN)" "$(LSP_PIN)"
	@echo "==> bin/cryolsp ready"
endif

# ---- CryoAnalyzer VS Code extension -----------------------------------
# Packages tools/CryoAnalyzer/ into a .vsix and installs it into VS Code,
# evicting any previously installed copy and any stale .vsix artifacts so
# the install always reflects the current source.
ifeq ($(HOST_OS),windows)
# Windows host: the POSIX recipe below (`command -v`, `{ ... }`, `[ -d ]`,
# `rm`, `./node_modules/.bin/...`) is unparseable by cmd, so the whole job
# lives in a batch wrapper - same single-line shape as `pin`.  It uses
# `where`/`if exist`/`del` and the `.cmd` shims (`code.cmd`, `npm.cmd`,
# `vsce.cmd`) that npm installs on Windows.
install-lsp:
	scripts\install-lsp-windows.cmd
else
install-lsp:
	@command -v code >/dev/null 2>&1 || { echo "ERROR: 'code' CLI not found on PATH"; exit 1; }
	@echo "==> Ensuring CryoAnalyzer node_modules are present"
	@cd "$(EXT_DIR)" && [ -d node_modules ] || npm install
	@echo "==> Uninstalling previously installed $(EXT_ID) (if any)"
	@code --uninstall-extension $(EXT_ID) >/dev/null 2>&1 || true
	@echo "==> Removing cached .vsix artifacts"
	@rm -f "$(EXT_DIR)"/*.vsix
	@echo "==> Packaging CryoAnalyzer"
	@cd "$(EXT_DIR)" && ./node_modules/.bin/vsce package --out "$(EXT_VSIX)"
	@echo "==> Installing $(EXT_VSIX)"
	@code --install-extension "$(EXT_VSIX)" --force
	@echo "==> CryoAnalyzer extension installed"
endif

# ---- selfhost byte-identity check -------------------------------------
# Implementation lives in scripts/selfhost-check.py — that gives us
# per-stage progress + timings, per-stage logs in build-logs/, and a
# tail-on-failure dump.  Run the script directly with --verbose for
# streaming subprocess output.
#
# The script is host-aware:
#   Linux host:   runs the full 6-stage Linux chain natively, then the
#                 optional wine-based Windows verification (skipped if the
#                 mingw toolchain + wine + .toolchains are absent).
#   Windows host: runs a Windows-native pre-check using bin/cryo.exe
#                 (smoke + stdlib byte-identity against the WSL Linux
#                 cross-compiled IR), then delegates the full 6-stage
#                 Linux chain to WSL.  Requires wsl.exe on PATH.
ifeq ($(HOST_OS),windows)
# The wrapper checks for python + wsl.exe, then drives selfhost-check.py.
# Same single-line shape as `pin` so it Just Works whether you invoked
# `make` from cmd / PowerShell or from MSYS2 / Git Bash.
selfhost-check:
	scripts\selfhost-check-windows.cmd
else
selfhost-check: $(PIN)
	@python3 scripts/selfhost-check.py
endif

# ---- test suite -------------------------------------------------------
# Builds the stage-2 compiler only if $(STAGE2) is missing, then drives
# `cryo test` against the tests/ project.  Run `make cryo` first to pick
# up compiler changes.  See tests/cryoconfig and docs/testing.md for the
# project layout and the framework surface (`![test]`, `![ignore]`,
# `![should_panic]`).  Pass arguments through with `make test ARGS=...`
# (e.g. `make test ARGS="--ignored some_filter"`).
# Build the C-side test helpers archive.  Uses cc + ar from the host
# toolchain.  Only rebuilds when the .c source changes.
$(TEST_HELPERS_A): $(TEST_HELPERS_C)
	@echo "==> Building ABI test helpers archive"
	@cc -O0 -fPIC -c -o $(TEST_HELPERS_O) $<
	@ar rcs $@ $(TEST_HELPERS_O)

ifeq ($(HOST_OS),windows)
# Native Windows host: recipes run under cmd, which can't execute a quoted
# forward-slash path and needs the `.exe` the native build emits — so the
# stage-2 compiler is `compiler\build\cryo.exe` (backslashes, suffix), built
# through the host-branched `cryo`/`stdlib` targets above.  $(STAGE2_EXE)
# and $(LIBCRYO_A) are file targets, so the compiler/stdlib are rebuilt only
# when missing, matching the Linux semantics.
STAGE2_EXE_WIN := $(subst /,\,$(STAGE2_EXE))
test: $(STAGE2_EXE) $(LIBCRYO_A) $(TEST_HELPERS_A)
	@cd tests && "$(STAGE2_EXE_WIN)" test $(ARGS)

test-list: $(STAGE2_EXE) $(LIBCRYO_A) $(TEST_HELPERS_A)
	@cd tests && "$(STAGE2_EXE_WIN)" test --list $(ARGS)
else
test: $(STAGE2) $(LIBCRYO_A) $(TEST_HELPERS_A)
	@cd tests && "$(STAGE2)" test $(ARGS)

test-list: $(STAGE2) $(LIBCRYO_A) $(TEST_HELPERS_A)
	@cd tests && "$(STAGE2)" test --list $(ARGS)
endif

# ---- examples smoke build ---------------------------------------------
# Compile every examples/*/ project with the freshly-built stage-2 compiler
# so a stdlib/compiler change that breaks a shipped "getting started" example
# fails CI instead of reaching users.  Build-only (no run); the per-example
# build/ dirs are gitignored.  Run `make cryo` first to pick up compiler
# changes.  CRYO_STDLIB pins the in-tree stdlib so resolution is independent
# of which compiler binary builds the examples.
ifeq ($(HOST_OS),windows)
examples: $(STAGE2_EXE) $(LIBCRYO_A)
	@echo "make examples runs under POSIX/WSL; native Windows cmd looping is unsupported."
	@echo "Run it from WSL or Git Bash instead."
else
examples: $(STAGE2) $(LIBCRYO_A)
	@failed=""; \
	for cfg in examples/*/cryoconfig; do \
	    dir=$$(dirname "$$cfg"); \
	    printf '==> %s\n' "$$dir"; \
	    if ! ( cd "$$dir" && CRYO_STDLIB="$(ROOT)/stdlib" "$(STAGE2)" build >/dev/null ); then \
	        failed="$$failed $$dir"; \
	    fi; \
	done; \
	if [ -n "$$failed" ]; then echo "==> FAILED examples:$$failed"; exit 1; fi; \
	echo "==> All examples built"
endif

# ---- release packaging -------------------------------------------------
# Build distributable archives under dist/.  `release` does the host
# platform (linux here); `release-windows` cross-builds the windows zip.
# Linux ships a static `cryo` (--release-static); see scripts/build-release.sh
# for the static-link env knobs (musl under Alpine).
release: release-linux

release-linux:
	@./scripts/build-release.sh linux

release-windows:
	@./scripts/build-release.sh windows

# ---- dev install via symlink (the repo-local toolchain) ----------------
# `make install` is the DEV install: symlink the committed bin/cryo +
# stdlib into a prefix.  End users instead run the production downloader:
#   curl -fsSL https://cryo-lang.org/install.sh | bash
install:
	@./install.sh --dev

uninstall:
	@./install.sh --dev --uninstall

# ---- clean ------------------------------------------------------------
clean:
	@echo "==> Cleaning compiler + stdlib build outputs"
	@rm -rf compiler/build stdlib/.bin
	@rm -rf tools/CryoLSP/build
	@rm -f bin/cryolsp
	@rm -f $(TEST_HELPERS_O) $(TEST_HELPERS_A)
