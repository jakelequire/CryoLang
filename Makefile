# Cryo top-level build orchestration.
#
# The compiler is self-hosted.  The pinned binaries at bin/cryo (Linux
# ELF) and bin/cryo.exe (Windows PE) bootstrap every other target - 
# every build flows through one of them depending on the host OS.
#
# `make help` lists every target, and every side effect a target has beyond
# the build output it names.

ROOT       := $(CURDIR)
PIN        := $(ROOT)/bin/cryo
STAGE2     := $(ROOT)/compiler/build/cryo
LIBCRYO_A   = $(ROOT)/stdlib/.bin/$(HOST_TRIPLE)/libcryo.a

# Every .cryo the self-hosted compiler is built from.  The stage-2 file rules
# below depend on these so an existing `compiler/build/cryo[.exe]` is treated
# as stale once any source changes: without prerequisites Make considers the
# binary up to date merely because it exists, and `make test` then gates a
# stale compiler while reporting green.  Pure-Make recursion (no `$(shell)`)
# so the Windows host doesn't need a POSIX `find`.
rwildcard = $(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))
CRYO_SOURCES := $(call rwildcard,$(ROOT)/compiler/src,*.cryo) \
                $(call rwildcard,$(ROOT)/stdlib,*.cryo)

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
#     uname) and therefore takes the Linux branch - which is correct: a
#     `make` invoked inside WSL runs the build natively, not via the
#     Windows host's WSL re-entry path.
ifeq ($(OS),Windows_NT)
    HOST_OS := windows
    UNAME_S := Windows_NT
    # Pin the recipe shell to cmd.  Every HOST_OS==windows recipe below is
    # written in cmd syntax (`if exist`, `rmdir /s /q`, `copy`, backslash
    # paths), but make picks `sh.exe` whenever one is on PATH - and a Windows
    # box with Git installed always has one.  Under that shell a cmd `if`
    # becomes `syntax error: unexpected end of file`, which is what breaks
    # `make stdlib` on a stock Windows CI runner while a developer box without
    # Git Bash on PATH builds fine.
    SHELL := cmd.exe
    .SHELLFLAGS := /C
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

# The triple a NATIVE build resolves to, asked of the pinned compiler.
#
# It cannot be derived from the host OS: the value comes from a toolchain probe
# (mingw `gcc` on PATH -> `...-windows-gnu`, an MSVC linker -> `...-windows-msvc`;
# on Linux whatever the linked libLLVM reports).  `cryo version --triple` prints
# exactly what `resolve_effective_triple` hands the tier lookup, so the directory
# named from it is the one the linker will search.  Guessing is SILENT when
# wrong: `runtime_dir_pick` falls back to the flat directory and the two
# toolchains resume overwriting each other's archives there.
#
# Asking means launching the pin, and the rules below name the triple in their
# prerequisites, so it would be asked while the Makefile is read.  The goals in
# NO_TRIPLE_GOALS build nothing and read no archive, so when every goal given
# is one of them the pin is not launched; any other goal asks once, up front.
NO_TRIPLE_GOALS := help clean check-fast verify-selftest verify-pin verify-pin-clean
ifneq ($(filter-out $(NO_TRIPLE_GOALS),$(or $(MAKECMDGOALS),help)),)
ifeq ($(HOST_OS),windows)
HOST_TRIPLE  := $(shell "$(subst /,\,$(PIN_EXE))" version --triple)
else
HOST_TRIPLE  := $(shell "$(PIN)" version --triple)
endif
endif
MINGW_GCC    := x86_64-w64-mingw32-gcc
MINGW_STRIP  := x86_64-w64-mingw32-strip
# The link driver for the cross build of cryo.exe: clang driving lld over the
# same mingw-w64 sysroot, not mingw gcc's GNU ld.  The GNU ld that Ubuntu's
# binutils-mingw-w64 ships (2.41.90) emits a 32-bit base relocation over every
# thread-local's section-relative offset, so the loader adds the ASLR delta to
# the offset and the first thread-local access faults; lld emits none.
# verify-pin refuses a pin carrying such a relocation.
WIN_LINK_CC  := clang-20 --target=x86_64-w64-mingw32 -fuse-ld=lld
WIN_LLVM_LIB := $(ROOT)/.toolchains/llvm-win/lib/libLLVM-C.dll.a
WIN_LLVM_DLL := $(ROOT)/.toolchains/llvm-win/bin/LLVM-C.dll
# libclang import lib + DLL: the C-import engine (Compiler::Bindgen) links
# libclang, so a cryo.exe cross-build needs both alongside LLVM-C.
WIN_CLANG_LIB := $(ROOT)/.toolchains/llvm-win/lib/libclang.dll.a
WIN_CLANG_DLL := $(ROOT)/.toolchains/llvm-win/bin/libclang.dll

# C-side helpers for the ABI tests.  Compiled with the host cc to a
# static archive that `tests/cryoconfig` links by verbatim path - see
# tests/helpers/abi_helpers.c for the contract.
#
# The artifact names are per-OS (`-unix` / `-windows`): a native Windows
# `make test` and a WSL/Linux one share this working tree, and an
# OS-agnostic path let a PE archive satisfy the Linux link (and vice
# versa) - the resulting failure is cryptic (`__mingw_vsnprintf` /
# `__ImageBase` relocation errors buried behind E0900).  Each host OS owns
# its own artifact; `tests/cryoconfig` selects it via the matching
# `[link.unix]` / `[link.windows]` overlay.
ifeq ($(HOST_OS),windows)
    HELPER_OS := windows
else
    HELPER_OS := unix
endif
TEST_HELPERS_DIR := $(ROOT)/tests/helpers
TEST_HELPERS_C   := $(TEST_HELPERS_DIR)/abi_helpers.c
TEST_HELPERS_O   := $(TEST_HELPERS_DIR)/abi_helpers-$(HELPER_OS).o
TEST_HELPERS_A   := $(TEST_HELPERS_DIR)/libabihelpers-$(HELPER_OS).a

# C++-side helper for the ffi_cpp_link project (the C++ direct-mangled-binding
# exemplar).  Compiled with the host C++ compiler so the project can `![link]`
# its Itanium-mangled symbols across a module boundary.  Built only on unix
# (the project gates on `requires: ["cxx"]`, which a C++-toolchain-less host
# fails, so it is skipped where this archive is absent).  Same per-OS
# naming discipline as the C helpers (`-unix` today; a windows arm would
# add `-windows` + a `[link.windows]` overlay in the project cryoconfig).
TEST_CPP_HELPERS_CPP := $(TEST_HELPERS_DIR)/cpp_link_helper.cpp
TEST_CPP_HELPERS_O   := $(TEST_HELPERS_DIR)/cpp_link_helper-unix.o
TEST_CPP_HELPERS_A   := $(TEST_HELPERS_DIR)/libcpplinkhelper-unix.a
CXX                  ?= c++

# Python interpreter for the helper scripts, and the only spelling of it in
# the recipes.  Windows ships `python.exe`, and its `python3` is an App
# Execution Alias that exits 49; Linux/macOS use `python3`.  NOOP is a recipe
# line that does nothing in either shell, so a target that only prints through
# `$(info)` does not also report "Nothing to be done".
ifeq ($(HOST_OS),windows)
    PYTHON := python
    NOOP   := @rem
else
    PYTHON := python3
    NOOP   := @:
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
.PHONY: help stdlib cryo cryo-exe selfhost-check test test-list test-census verify roster-check verify-selftest incremental-instance-check check-fast lsp-check cross-check vendor-check api-index api-index-check examples examples-golden valgrind-check verify-freestanding runtime-tiers runtime-tiers-win pin \
        pin-linux-impl pin-windows-impl _pin-windows-do verify-pin verify-pin-clean incremental-check \
        install uninstall clean lsp install-lsp release release-linux release-windows

# The help text is printed by make itself (`$(info)`), not by a shell, so it
# reads the same under cmd and sh.  A target added to this Makefile is added
# here too.
define HELP_TEXT
Cryo build targets.  ARGS="..." passes arguments through where noted.

Build
  make cryo              Stage-2 compiler from the pin, then stamped with the
                         commit it was built from (`cryo stamp`, which is what
                         `cryo version` reports; the stamp rewrites a region
                         of the binary, so no object changes).
  make stdlib            Standard library archive for the host triple.
  make runtime-tiers     Runtime tier archives for the host triple (every
                         hosted link needs the panic tier).
  make runtime-tiers-win The same tiers cross-built for the Windows triple
                         (skipped without the mingw toolchain).
  make cryo-exe          Cross-build compiler/build/cryo.exe from Linux, and
                         stamp it.
  make lsp               Build tools/CryoLSP with the pin and install it as
                         bin/cryolsp.
  make install-lsp       Package tools/CryoAnalyzer and install it into VS
                         Code, REPLACING the installed CryoAnalyzer extension.

Tests and gates
  make test              cryo test over tests/ (the working loop; ARGS=filter).
  make test-list         List the discovered unit tests without running them.
  make test-census       The same run as test, with each suite's count
                         asserted against tests/test-roster.txt (evidence).
  make roster-check      The discovered roster against tests/test-roster.txt
                         (ARGS=--merge after adding, --update after removing).
  make verify            Build once; census, examples, lsp-check, cross-check,
                         incremental-instance-check and check-fast run
                         concurrently and their objects are hashed
                         (ARGS="--baseline HEAD --require-identical").
  make check-fast        verify's self-test and verify-pin; builds nothing.
  make verify-selftest   verify's judges and verify-pin's checks, driven
                         through fixtures they must accept and refuse.
  make lsp-check         tools/CryoLSP builds with the compiler under test
                         (into its own directory; installs nothing), and
                         the binary passes its behavioural checks
                         (tools/CryoLSP/tests/smoke.py).
  make cross-check       runtime, stdlib, compiler and CryoLSP for the other
                         OS's triple, objects only (ARGS=--triple=...).
  make examples          Build every examples/ project.
  make examples-golden   Build and run four examples, diff their stdout
                         (POSIX only; refuses on Windows).
  make valgrind-check    The same four under valgrind (Linux only).
  make incremental-check Incremental build == clean build, per module
                         (refuses on Windows: a PE carries a link timestamp).
  make incremental-instance-check  An edit asking a cached module for a new
                         generic instance still links.
  make vendor-check      Every constant shape survives `cryo vendor`.
  make verify-freestanding  Runtime tiers' freestanding acceptance check
                         (POSIX only; refuses on Windows).
  make selfhost-check    Byte-identity fixed point.  Linux host: the 6-stage
                         chain, then the Windows chain under wine (a skipped
                         arm fails unless ARGS=--allow-skipped-arm).  Windows
                         host: a native cryo.exe pre-check, then the Linux
                         chain through WSL.
  make api-index         Regenerate docs/stdlib-api.txt.
  make api-index-check   Fail if docs/stdlib-api.txt is stale.

Pins and release
  make pin               Refresh bin/cryo and bin/cryo.exe and their sidecars
                         (Windows host: through WSL).
  make verify-pin        Each pin matches its sidecar's sha256 and is stamped
                         with the commit the sidecar names, and both name the
                         same commit.
  make verify-pin-clean  The same, and the pins were built from a clean tree.
  make release           Alias of release-linux.
  make release-linux     dist/ tarball with a static, stamped cryo (POSIX).
  make release-windows   dist/ zip (POSIX host, cross-built).
  make install           Symlink bin/cryo and the stdlib into a prefix (POSIX).
  make uninstall         Remove those symlinks.
  make clean             Remove every build output in the tree: compiler,
                         stdlib, runtime tiers, LSP, test helpers, test and
                         example builds, .verify/, .cross-check/,
                         build-logs/ and dist/.

Internal, called by pin: pin-linux-impl, pin-windows-impl, _pin-windows-do.

Side effects beyond the named output
  stdlib          deletes stdlib/.bin/<triple>/ and a flat stdlib/.bin/libcryo.a.
  release-windows rebuilds stdlib/.bin/x86_64-pc-windows-gnu/ (the zip's
                  stdlib archive, cross-built).
  lsp             overwrites bin/cryolsp; fails while an editor holds it open.
  install-lsp     uninstalls the VS Code extension it replaces.
  pin             rewrites both pins; copies LLVM-C.dll and libclang.dll to bin/.
  selfhost-check  deletes stdlib/.bin and compiler/build.
  test, examples, lsp-check and every gate that needs the compiler rebuild
                  it first when any .cryo under compiler/src or stdlib is
                  newer than it.
  verify          runs `make check-fast` from inside itself, and keeps a
                  clone and baseline compilers under .verify/ until
                  `make clean`.
  install         writes symlinks outside the repository (may need sudo).
endef

help:
	$(info $(HELP_TEXT))
	$(info )
	$(info Detected host: $(HOST_OS) ($(UNAME_S)))
	$(NOOP)

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
# The archive is built per target into `stdlib/.bin/<triple>/libcryo.a`, and
# only THAT directory is wiped first.  A flat `stdlib/.bin/libcryo.a` is the
# pre-split layout: nothing writes it here any more, and leaving one in a dev
# tree would let a link silently fall back to another target's archive, so it
# is removed rather than left to be found.  The flat path stays meaningful for
# an INSTALLED tree, which holds one target by construction.
ifeq ($(HOST_OS),windows)
stdlib:
	$(if $(strip $(HOST_TRIPLE)),,$(error stdlib: `cryo version --triple` returned nothing; the pinned compiler is too old))
	@echo "==> Building stdlib for $(HOST_TRIPLE) via bin/cryo.exe"
	@if exist "stdlib\.bin\$(HOST_TRIPLE)" rmdir /s /q "stdlib\.bin\$(HOST_TRIPLE)"
	@if exist "stdlib\.bin\libcryo.a" del /q "stdlib\.bin\libcryo.a"
	@cd stdlib && "$(subst /,\,$(PIN_EXE))" build --build-dir=.bin/$(HOST_TRIPLE)
else
stdlib: $(PIN)
	$(if $(strip $(HOST_TRIPLE)),,$(error stdlib: `cryo version --triple` returned nothing; the pinned compiler is too old))
	@echo "==> Building stdlib for $(HOST_TRIPLE) via bin/cryo"
	@rm -rf stdlib/.bin/$(HOST_TRIPLE)
	@rm -f stdlib/.bin/libcryo.a
	@cd stdlib && "$(PIN)" build --build-dir=.bin/$(HOST_TRIPLE)
endif

# ---- self-hosted compiler via the pin ---------------------------------
# Depends on `runtime-tiers` because linking ANY hosted binary - including this
# compiler - needs the panic tier to resolve `__cryo_panic`.  Harmless before the
# pin emits that call; required the moment it does.
#
# The stdlib archive is a FILE prerequisite, not the phony `stdlib`: the phony
# target wipes and rebuilds the archive on every `make cryo` (nine seconds of
# a no-change build) even though the rebuilt bytes are identical.  The file
# rule below rebuilds it, through `make stdlib`, whenever it is missing or
# older than any stdlib source or the pin that compiles it.
#
# The linked compiler is then stamped (`cryo stamp`) with the checkout it was
# built from, which is what `cryo version` reports.  The stamp is a region of
# the binary rewritten in place, so no object changes.  The pin outputs are
# excluded from the dirty check, as cryo-pin.py excludes them from its own:
# a pin refresh rewrites bin/ between stamping its two halves.
CRYO_PROFILE  := release
STAMP_EXCLUDE := bin/cryo,bin/cryo.pin.txt,bin/cryo.exe,bin/cryo.exe.pin.txt
ifeq ($(HOST_OS),windows)
cryo: $(LIBCRYO_A) runtime-tiers
	@echo "==> Building self-hosted cryo via bin/cryo.exe"
	@cd compiler && "$(subst /,\,$(PIN_EXE))" build --profile=$(CRYO_PROFILE)
	@"$(subst /,\,$(PIN_EXE))" stamp "$(STAGE2_EXE)" --profile=$(CRYO_PROFILE) --exclude=$(STAMP_EXCLUDE)
	@echo "==> Self-hosted cryo built: $(STAGE2_EXE)"
else
cryo: $(LIBCRYO_A) runtime-tiers
	@echo "==> Building self-hosted cryo via bin/cryo"
	@cd compiler && "$(PIN)" build --profile=$(CRYO_PROFILE)
	@"$(PIN)" stamp "$(STAGE2)" --profile=$(CRYO_PROFILE) --exclude=$(STAMP_EXCLUDE)
	@echo "==> Self-hosted cryo built: $(STAGE2)"
endif

# File-target rule so downstream targets (test, lsp-check, examples) can depend
# on the binary itself instead of the phony `cryo` target.  The binary is
# rebuilt when it is missing or older than any .cryo under compiler/src or
# stdlib, so those targets never gate a stale compiler.
$(STAGE2): $(CRYO_SOURCES)
	@$(MAKE) --no-print-directory cryo

# Windows counterpart: the native build emits `cryo.exe`, so a Windows-host
# `test`/`test-list` depends on this path instead of $(STAGE2).  Same
# delegate-to-`cryo` shape; `cryo` is host-branched so it builds correctly.
$(STAGE2_EXE): $(CRYO_SOURCES)
	@$(MAKE) --no-print-directory cryo

# File-target rule for the stdlib static library so `test` rebuilds it
# when stdlib/.bin has been wiped (e.g. by selfhost-check) but the
# compiler binary still exists. Without this, `make selfhost-check &&
# make test` fails at link with "cannot find libcryo.a".  It is also rebuilt
# when a stdlib source or the pin is newer than it, which is what lets `cryo`
# depend on it instead of on the always-rebuilding phony `stdlib`.
STDLIB_SOURCES := $(call rwildcard,$(ROOT)/stdlib,*.cryo) $(ROOT)/stdlib/cryoconfig
ifeq ($(HOST_OS),windows)
STDLIB_PIN := $(PIN_EXE)
else
STDLIB_PIN := $(PIN)
endif
$(LIBCRYO_A): $(STDLIB_SOURCES) $(STDLIB_PIN)
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
#                 command-line limit at link time - both paths already
#                 work cleanly inside WSL.  Fails loudly with an
#                 install hint if WSL isn't present.
#
# `cryo-exe` (the cross-build of cryo.exe alone) remains available for
# scripts that want just the Windows binary without touching bin/cryo.

# Cross-compile the self-hosted compiler to cryo.exe (mingw-w64 + the
# .toolchains/llvm-win import lib).  Requires the cross toolchain + the
# fetched windows libLLVM-C; prints an actionable hint and fails if absent.
# Depends on `runtime-tiers-win`: this is a CROSS link, so it needs the tiers
# built for the windows triple to resolve `__cryo_panic`, not the host ones.
cryo-exe: cryo runtime-tiers-win
	@command -v $(MINGW_GCC) >/dev/null 2>&1 || { echo "ERROR: $(MINGW_GCC) not found (install gcc-mingw-w64-x86-64)."; exit 1; }
	@command -v ld.lld-20 >/dev/null 2>&1 || { echo "ERROR: ld.lld-20 not found (install lld-20); WIN_LINK_CC links with it."; exit 1; }
	@test -f "$(WIN_LLVM_LIB)" || { echo "ERROR: windows libLLVM-C import lib missing at"; echo "       $(WIN_LLVM_LIB)"; echo "       Run: scripts/fetch-windows-llvm.sh"; exit 1; }
	@test -f "$(WIN_CLANG_LIB)" || { echo "ERROR: windows libclang import lib missing at"; echo "       $(WIN_CLANG_LIB)"; echo "       Run: scripts/fetch-windows-llvm.sh"; exit 1; }
	@echo "==> Cross-building cryo.exe ($(WIN_TRIPLE)) via the self-hosted compiler"
	@cd compiler && CRYO_CC="$(WIN_LINK_CC)" "$(STAGE2)" build --target=$(WIN_TRIPLE) --no-incremental --profile=$(CRYO_PROFILE)
	@"$(PIN)" stamp "$(STAGE2_EXE)" --profile=$(CRYO_PROFILE) --exclude=$(STAMP_EXCLUDE)
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
	@$(PYTHON) scripts/cryo-pin.py --source "$(STAGE2)" --pin "$(PIN)"

pin-windows-impl:
	@if command -v $(MINGW_GCC) >/dev/null 2>&1 && [ -f "$(WIN_LLVM_LIB)" ] && [ -f "$(WIN_CLANG_LIB)" ]; then \
		$(MAKE) --no-print-directory _pin-windows-do; \
	else \
		echo "==> [skip] bin/cryo.exe pin: Windows cross-toolchain absent."; \
		command -v $(MINGW_GCC) >/dev/null 2>&1 \
			|| echo "       missing: $(MINGW_GCC) (install gcc-mingw-w64-x86-64)"; \
		{ [ -f "$(WIN_LLVM_LIB)" ] && [ -f "$(WIN_CLANG_LIB)" ]; } \
			|| { echo "       missing: windows libLLVM-C / libclang import libs"; \
			     echo "         run: scripts/fetch-windows-llvm.sh"; }; \
	fi

_pin-windows-do: cryo-exe
	@$(PYTHON) scripts/cryo-pin.py --source "$(STAGE2_EXE)" --pin "$(PIN_EXE)" --strip-tool $(MINGW_STRIP)
	@cp -f "$(WIN_LLVM_DLL)" "$(ROOT)/bin/LLVM-C.dll" 2>/dev/null \
		&& echo "==> Runtime LLVM-C.dll copied to bin/ (gitignored)" || true
	@cp -f "$(WIN_CLANG_DLL)" "$(ROOT)/bin/libclang.dll" 2>/dev/null \
		&& echo "==> Runtime libclang.dll copied to bin/ (gitignored)" || true
endif

# ---- Cryo-language LSP server -----------------------------------------
# Builds tools/CryoLSP/ (entirely Cryo source) into bin/cryolsp.
# Drives off the committed pin only: the LSP pulls the compiler in as a
# *library* dependency (tools/CryoLSP/cryoconfig -> path = "../../compiler"),
# so the stage-2 compiler *binary* is not an input here.  Depending on it
# would drag `make cryo` (stdlib + full self-host) in front of every LSP
# build for no benefit.  $(LIBCRYO_A) is a file target, so the stdlib
# archive is only built when it is actually missing.
# Run `make cryo` / `make pin` first if you want compiler changes reflected
# in the pin used to build; the LSP's own copy of the compiler library is
# always rebuilt from current source by the project build.
ifeq ($(HOST_OS),windows)
# Windows host: recipes run under cmd, which has no `cp`.  `$(PIN)` (bin/cryo,
# no extension) launches via PATHEXT -> bin/cryo.exe.  The built binary is
# `cryolsp.exe`; copy it to the pin with the `copy` builtin (backslash paths,
# stdout silenced).
lsp:
	@echo "==> Building CryoLSP via bin/cryo.exe"
	cd tools/CryoLSP && "$(PIN)" build
	copy /Y "$(subst /,\,$(LSP_BIN))" "$(subst /,\,$(LSP_PIN))" >NUL
	@echo "==> bin/cryolsp.exe ready"
else
lsp: $(PIN) $(LIBCRYO_A)
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
# Implementation lives in scripts/selfhost-check.py - that gives us
# per-stage progress + timings, per-stage logs in build-logs/, and a
# tail-on-failure dump.  Run the script directly with --verbose for
# streaming subprocess output.
#
# The script is host-aware:
#   Linux host:   runs the full 6-stage Linux chain natively, then the
#                 wine-based Windows chain.  Without the mingw toolchain,
#                 wine and .toolchains that arm is skipped, and a skipped arm
#                 fails the check unless ARGS=--allow-skipped-arm says so.
#   Windows host: runs a Windows-native pre-check using bin/cryo.exe
#                 (smoke + stdlib byte-identity against the WSL Linux
#                 cross-compiled IR), then delegates the full 6-stage
#                 Linux chain to WSL.  Requires wsl.exe on PATH.
ifeq ($(HOST_OS),windows)
# The wrapper checks for python + wsl.exe, then drives selfhost-check.py.
# Same single-line shape as `pin` so it Just Works whether you invoked
# `make` from cmd / PowerShell or from MSYS2 / Git Bash.
# `runtime-tiers` first: the check's stage-2 and stage-3 rounds are hosted links
# that need libcryort-panic-abort-hosted.a to resolve `__cryo_panic`.  It wipes
# stdlib/.bin and compiler/build but not runtime/.bin, so building the tiers up
# front survives every round.
selfhost-check: runtime-tiers
	scripts\selfhost-check-windows.cmd $(ARGS)
else
selfhost-check: $(PIN) runtime-tiers
	@$(PYTHON) scripts/selfhost-check.py $(ARGS)
endif

# ---- pin self-verification --------------------------------------------
# Assert each committed pin's bytes match its .pin.txt sidecar sha256
# (catches a binary committed without regenerating its sidecar, corruption,
# or a hand-edited sidecar), and that the commit `cryo stamp` wrote into each
# pin is the one its sidecar names.  Pure-python, no build, runs on any host;
# it reads the stamp from the bytes, so both pins are checked on either host.
#   verify-pin        integrity (sha256 and stamped commit == sidecar)
#   verify-pin-clean  ALSO require a clean-worktree pin - the release gate
#                     (a pin built from a dirty tree isn't reproducible).
verify-pin:
	@$(PYTHON) scripts/verify-pin.py

verify-pin-clean:
	@$(PYTHON) scripts/verify-pin.py --require-clean

# ---- incremental-build soundness --------------------------------------
# Run the per-module incremental byte-identity matrix: every incremental
# build must equal a clean `--no-incremental` build of the same source.
# Needs the stage-2 compiler (builds it first if absent).
ifeq ($(HOST_OS),windows)
incremental-check: $(STAGE2_EXE)
	@$(PYTHON) scripts/incremental-check.py --cryo "$(STAGE2_EXE)"
else
incremental-check: $(STAGE2)
	@$(PYTHON) scripts/incremental-check.py --cryo "$(STAGE2)"
endif

# ---- test suite -------------------------------------------------------
# Rebuilds the stage-2 compiler when it is missing or older than its sources
# (the $(STAGE2) file rule), then drives `cryo test` against the tests/
# project.  See tests/cryoconfig and docs/testing.md for the
# project layout and the framework surface (`![test]`, `![ignore]`,
# `![should_panic]`).  Pass arguments through with `make test ARGS=...`
# (e.g. `make test ARGS="--ignored some_filter"`).
# Build the C-side test helpers archive.  Uses cc + ar from the host
# toolchain.  Only rebuilds when the .c source changes.
$(TEST_HELPERS_A): $(TEST_HELPERS_C)
	@echo "==> Building ABI test helpers archive"
	@cc -O0 -fPIC -c -o $(TEST_HELPERS_O) $<
	@ar rcs $@ $(TEST_HELPERS_O)

# C++ helper archive for the ffi_cpp_link project (Itanium-mangled symbols).
$(TEST_CPP_HELPERS_A): $(TEST_CPP_HELPERS_CPP)
	@echo "==> Building C++ link-test helper archive"
	@$(CXX) -O0 -fPIC -c -o $(TEST_CPP_HELPERS_O) $<
	@ar rcs $@ $(TEST_CPP_HELPERS_O)

ifeq ($(HOST_OS),windows)
# Native Windows host: recipes run under cmd, which can't execute a quoted
# forward-slash path and needs the `.exe` the native build emits - so the
# stage-2 compiler is `compiler\build\cryo.exe` (backslashes, suffix), built
# through the host-branched `cryo`/`stdlib` targets above.  $(STAGE2_EXE)
# and $(LIBCRYO_A) are file targets, so the compiler/stdlib are rebuilt only
# when missing, matching the Linux semantics.
STAGE2_EXE_WIN := $(subst /,\,$(STAGE2_EXE))
test: $(STAGE2_EXE) $(LIBCRYO_A) $(TEST_HELPERS_A) runtime-tiers
	@cd tests && "$(STAGE2_EXE_WIN)" test $(ARGS)

test-list: $(STAGE2_EXE) $(LIBCRYO_A) $(TEST_HELPERS_A)
	@cd tests && "$(STAGE2_EXE_WIN)" test --list $(ARGS)

roster-check: $(STAGE2_EXE) $(LIBCRYO_A) $(TEST_HELPERS_A)
	@$(PYTHON) scripts/roster-check.py "$(STAGE2_EXE_WIN)" $(ARGS)

test-census: $(STAGE2_EXE) $(LIBCRYO_A) $(TEST_HELPERS_A) runtime-tiers
	@$(PYTHON) scripts/test-census.py --cryo "$(STAGE2_EXE)" $(ARGS)

vendor-check: $(STAGE2_EXE) $(LIBCRYO_A) runtime-tiers
	@$(PYTHON) scripts/vendor-consts-gate.py "$(STAGE2_EXE_WIN)" $(ARGS)
else
test: $(STAGE2) $(LIBCRYO_A) $(TEST_HELPERS_A) $(TEST_CPP_HELPERS_A) runtime-tiers
	@cd tests && "$(STAGE2)" test $(ARGS)

test-list: $(STAGE2) $(LIBCRYO_A) $(TEST_HELPERS_A) $(TEST_CPP_HELPERS_A)
	@cd tests && "$(STAGE2)" test --list $(ARGS)

# Golden-file gate on the DISCOVERED unit-test roster: a compiler change
# that silently breaks `![test]` discovery would otherwise stay green
# ("0 of 0 failed" is a pass).  Re-pin deliberately with ARGS=--update.
roster-check: $(STAGE2) $(LIBCRYO_A) $(TEST_HELPERS_A) $(TEST_CPP_HELPERS_A)
	@$(PYTHON) scripts/roster-check.py "$(STAGE2)" $(ARGS)

# `make test` with the counts asserted.  The runner reports a zero the same way
# it reports success - silently - so an exit code cannot tell a suite that ran
# everything from one that ran nothing.  This runs the same suite, streams the
# same output, and then reconciles what ran against the roster golden.  Use it
# wherever the run is being taken as evidence; `make test` stays for the
# working loop, where a pattern filter is the point.
test-census: $(STAGE2) $(LIBCRYO_A) $(TEST_HELPERS_A) $(TEST_CPP_HELPERS_A) runtime-tiers
	@$(PYTHON) scripts/test-census.py --cryo "$(STAGE2)" $(ARGS)

# End-to-end gate on the constants `cryo vendor` carries out of a C header.
# The importer binds a constant and the serializer writes it, and when those
# two disagree on an AST shape the constant vanishes with the generator still
# reporting success -- the use site fails with `cannot find value` one build
# later, pointing at nothing.  Consuming every constant is what turns that into
# a build failure here instead.
#
# Needs the compiler and a working link (it builds and RUNS a consumer), but no
# external library and no display, so unlike the raylib acceptance project this
# runs everywhere.  Hermetic: the fixture registers under a throwaway
# $CRYO_HOME and leaves nothing on the machine.
vendor-check: $(STAGE2) $(LIBCRYO_A) runtime-tiers
	@$(PYTHON) scripts/vendor-consts-gate.py "$(STAGE2)" $(ARGS)
endif

# ---- one verification run ----------------------------------------------
# Build once, then run census, examples, lsp-check, cross-check,
# incremental-instance-check and check-fast at the same time, and hash the objects the census and examples compiled.
# ARGS="--baseline HEAD" also compares those objects against a compiler built
# from HEAD's compiler/src (in a clone under .verify/, cached), which is the
# evidence that a change moved no compiled output.  See scripts/verify.py.
#
# Everything it needs built is a prerequisite HERE, before any gate starts:
# the gates run concurrently, and a phony prerequisite rebuilt by one of them
# (the runtime tiers) would be rewritten while another is linking against it.
ifeq ($(HOST_OS),windows)
verify: cryo $(TEST_HELPERS_A)
	@$(PYTHON) scripts/verify.py --cryo "$(STAGE2_EXE)" $(ARGS)
else
verify: cryo $(TEST_HELPERS_A) $(TEST_CPP_HELPERS_A)
	@$(PYTHON) scripts/verify.py --cryo "$(STAGE2)" $(ARGS)
endif

# An incremental build after an edit that asks an unchanged module for a new
# generic instance must link (tests/fixtures/incremental-new-instance).
ifeq ($(HOST_OS),windows)
incremental-instance-check: cryo
	@$(PYTHON) scripts/incremental-instance-check.py --cryo "$(STAGE2_EXE)"
else
incremental-instance-check: cryo
	@$(PYTHON) scripts/incremental-instance-check.py --cryo "$(STAGE2)"
endif

# The judges behind verify's tests/started-compiling declarations, and
# verify-pin's checks, driven through every case they must refuse and every
# case they must allow.
verify-selftest:
	@$(PYTHON) scripts/verify.py --selftest
	@$(PYTHON) scripts/verify-pin.py --selftest

# ---- the pre-commit sweep ----------------------------------------------
# Every gate here is source-derived, so this target builds nothing.  A short
# gate everybody runs is worth more than a twenty-minute one nobody does.
check-fast: verify-selftest verify-pin
	@echo "check-fast: OK (verify's declared-program self-test, pin integrity)"

# ---- language-server compile gate --------------------------------------
# The LSP links the compiler as a LIBRARY (tools/CryoLSP/cryoconfig ->
# path = "../../compiler"), so it sees every AST, NodeKind and public-signature
# change - and no other gate compiles it.  A green local suite has therefore
# never been evidence that the LSP builds, and it has been broken behind one
# more than once.
#
# Separate from `make lsp` on purpose.  That target also installs over
# bin/cryolsp, which any running editor holds open, and the failure is a bare
# `error: linking failed` with no linker diagnostic AFTER a clean compile.  A
# gate that goes red because a language server is running gets ignored, and an
# ignored gate is not a gate.  This one builds into its own directory and
# installs nothing, so a held pin cannot reach it.
#
# Built with the COMPILER UNDER TEST, not the pin.  Rebuilding the compiler
# LIBRARY from current source is only half the surface: the other half is the
# checking the compiler BINARY performs on it, and the pin performs an older
# one.  A build with the pin therefore certifies the LSP against a compiler
# nobody will ship - and it did: the pin compiled tools/CryoLSP clean for 19
# commits after a signedness change had already made it uncompilable by the
# compiler being built.  The gate must run the checker it is gating.
#
# This costs a stage-2 build when one is not already present.  That is the
# price of the coverage, and CI pays nothing extra: `make cryo` runs first.
ifeq ($(HOST_OS),windows)
lsp-check: $(STAGE2_EXE) $(LIBCRYO_A)
	@$(PYTHON) scripts/lsp-gate.py --cryo "$(STAGE2_EXE)" $(ARGS)
else
lsp-check: $(STAGE2) $(LIBCRYO_A)
	@$(PYTHON) scripts/lsp-gate.py --cryo "$(STAGE2)" $(ARGS)
endif

# ---- the other OS's config-gated half ----------------------------------
# Config gating prunes `![config(linux)]` from a Windows build before name
# resolution sees it, and `![config(windows)]` from a Linux one, so every
# host-native gate measures a tree with the other OS's half cut out.  This
# compiles runtime/, stdlib/, compiler/ and tools/CryoLSP for the other OS with
# `--target`, which selects that OS's gates and stops at object files - no
# cross toolchain, no link, no WSL.  A declaration that stops resolving on
# the other OS is refused here, on this host, in about a minute.
#
# Built with the COMPILER UNDER TEST for the same reason `lsp-check` is:
# the checking is the surface being gated, and the pin performs an older one.
# Objects only: nothing links and nothing runs, so a link-time or runtime
# defect on the other OS is still `verify-freestanding` / CI's to find.
ifeq ($(HOST_OS),windows)
cross-check: $(STAGE2_EXE)
	@$(PYTHON) scripts/cross-check.py --cryo "$(STAGE2_EXE)" $(ARGS)
else
cross-check: $(STAGE2)
	@$(PYTHON) scripts/cross-check.py --cryo "$(STAGE2)" $(ARGS)
endif

# ---- stdlib API index --------------------------------------------------
# Regenerate docs/stdlib-api.txt, the one-file answer to "does this already
# exist?".  Two sources, cross-checked: `nm` over stdlib/.bin/libcryo.a decoded
# by the compiler's OWN demangler (authoritative -- a symbol in the archive
# linked), plus a declaration scan of stdlib/**/*.cryo (complete -- it sees
# generic templates the stdlib never instantiates, which have no symbol).
#
# Depends on $(LIBCRYO_A) so the archive half is present; without it the script
# degrades to the source scan and SAYS SO in the file header rather than
# silently emitting a thinner index.
#
# `api-index-check` is the CI half: a hand-edited or stale index is worse than
# no index, because it gets trusted.
api-index: $(LIBCRYO_A)
	@$(PYTHON) scripts/api-index.py

api-index-check: $(LIBCRYO_A)
	@$(PYTHON) scripts/api-index.py --check

# ---- examples smoke build ---------------------------------------------
# Compile every examples/*/ project with the freshly-built stage-2 compiler
# so a stdlib/compiler change that breaks a shipped "getting started" example
# fails CI instead of reaching users.  Build-only (no run); the per-example
# build/ dirs are gitignored.  The script pins CRYO_STDLIB to the in-tree
# stdlib so resolution is independent of which compiler binary builds the
# examples.
# Driven by a script rather than a shell loop because the loop was POSIX-only
# and the Windows branch printed "run it from WSL" and exited 0.  A gate that
# exits 0 having done nothing is counted as evidence, so this one refuses to
# report success unless it can say what it swept.
ifeq ($(HOST_OS),windows)
examples: $(STAGE2_EXE) $(LIBCRYO_A)
	@$(PYTHON) scripts/examples-gate.py --cryo "$(STAGE2_EXE)"
else
examples: $(STAGE2) $(LIBCRYO_A)
	@$(PYTHON) scripts/examples-gate.py --cryo "$(STAGE2)"
endif

# ---- examples golden-output check -------------------------------------
# Build AND run the deterministic, input-free examples and diff their stdout
# against committed goldens (examples/<name>/expected.out).  Catches runtime
# regressions that the build-only `examples` target misses.  POSIX-only: the
# script is bash, so a Windows host refuses rather than reporting success,
# and builds nothing first.
ifeq ($(HOST_OS),windows)
examples-golden:
	$(error examples-golden: UNAVAILABLE on this host -- it builds AND RUNS the examples and diffs stdout with bash; run it from WSL)
else
examples-golden: $(STAGE2) $(LIBCRYO_A)
	@CRYO="$(STAGE2)" CRYO_STDLIB="$(ROOT)/stdlib" bash scripts/check-examples-output.sh
endif

# ---- runtime memory-safety gate (valgrind) ----------------------------
# Build AND run the deterministic examples under valgrind, failing on any
# invalid free/read/write or definite leak.  The static move-checker rejects
# use-after-move at compile time; this exercises the generated drop/free paths
# at runtime, so a miscompile that double-freed or leaked an owned value is
# caught here.  POSIX/Linux-only and requires valgrind on PATH; a Windows host
# refuses and builds nothing first.
ifeq ($(HOST_OS),windows)
valgrind-check:
	$(error valgrind-check: UNAVAILABLE on this host -- valgrind is POSIX/Linux only; run it from WSL)
else
valgrind-check: $(STAGE2) $(LIBCRYO_A)
	@CRYO="$(STAGE2)" CRYO_STDLIB="$(ROOT)/stdlib" bash scripts/valgrind-check.sh
endif

# ---- runtime tier archives --------------------------------------------
# NOT optional, and not only for freestanding projects: codegen emits a call to
# the external `__cryo_panic` for every panic, so a HOSTED link needs
# libcryort-panic-abort-hosted.a on the line or it fails on an undefined symbol.
# Built with the PIN, not the self-hosted compiler, and that is load bearing:
# once the pin itself emits `__cryo_panic`, LINKING the self-hosted compiler
# needs these archives, so making them depend on that compiler would be a
# bootstrap cycle (tiers -> cryo -> tiers).  The pin is a known-good compiler
# and the tier sources do not depend on any newer codegen.
#
# Two workspaces over the same sources: `runtime/` builds them freestanding,
# `runtime/hosted/` rebuilds the abort tier with `no_runtime = false` so its
# `__cryo_panic` exits through libc (flushing stdio) instead of a raw syscall.
# Both write into `runtime/.bin/$(HOST_TRIPLE)`, one directory per target.
#
# The per-target directory is what keeps two toolchains apart.  A project's
# artifacts hoist to the root of its `output_dir`, so while every target wrote
# `runtime/.bin` flat, building for a second target OVERWROTE the first's
# archives in place and the next link died on another architecture's objects
# (`__ImageBase undefined` one way, `undefined reference to RtlAllocateHeap`
# the other).  `runtime_dir_pick` prefers `<dir>/<triple>`, so naming the
# directory for the triple the linker will search removes the collision rather
# than scheduling a cleanup around it.
#
# `HOST_TRIPLE` is asked of the compiler because a native triple comes from a
# toolchain probe; a wrong guess is silent, since the lookup falls back to the
# flat directory.  Empty means the pinned compiler predates `version --triple`,
# which is a hard error here: building flat is the bug this target exists to
# prevent, so it must not be the fallback.
#
# `--no-incremental` stays.  The incremental cache tracks SOURCE freshness and
# not the target the objects were built for, so a directory left holding
# another target's archives reports `cryort-core is up to date`, skips the
# re-archive, and exits 0 with the wrong objects still in place - measured in
# both directions.  The per-target split makes that unreachable through this
# target, but the skip is a live defect in its own right and the tier sources
# are small enough that forcing the re-emit costs seconds.
ifeq ($(HOST_OS),windows)
runtime-tiers:
	$(if $(strip $(HOST_TRIPLE)),,$(error runtime-tiers: `cryo version --triple` returned nothing; the pinned compiler is too old))
	@echo "==> Building runtime tiers for $(HOST_TRIPLE) via bin/cryo.exe"
	@cd runtime && "$(subst /,\,$(PIN_EXE))" build --no-incremental --build-dir=.bin/$(HOST_TRIPLE)
	@cd runtime\hosted && "$(subst /,\,$(PIN_EXE))" build --no-incremental --build-dir=../.bin/$(HOST_TRIPLE)
else
runtime-tiers: $(PIN)
	$(if $(strip $(HOST_TRIPLE)),,$(error runtime-tiers: `cryo version --triple` returned nothing; the pinned compiler is too old))
	@echo "==> Building runtime tiers for $(HOST_TRIPLE) via bin/cryo"
	@cd runtime && "$(PIN)" build --no-incremental --build-dir=.bin/$(HOST_TRIPLE)
	@cd runtime/hosted && "$(PIN)" build --no-incremental --build-dir=../.bin/$(HOST_TRIPLE)
endif

# The same tiers cross-built for the windows triple, for `cryo-exe`.  They land
# in `runtime/.bin/$(WIN_TRIPLE)`, beside the host triple's directory, where
# `runtime_dir_pick` (codegen/passes.cryo) looks first.  The triple is safe to
# spell here because an explicit `--target=` is returned verbatim by
# `resolve_effective_triple` - it is the same string the lookup will use; a
# native build's triple comes from a toolchain probe, which is why
# `runtime-tiers` asks the pin for it instead.  Skipped (not failed) without
# the cross toolchain, matching how `pin-windows-impl` treats a Linux-only
# checkout.
runtime-tiers-win: $(PIN)
	@if command -v $(MINGW_GCC) >/dev/null 2>&1; then \
		echo "==> Building runtime tiers for $(WIN_TRIPLE) via bin/cryo"; \
		( cd runtime && "$(PIN)" build --target=$(WIN_TRIPLE) --no-incremental --build-dir=.bin/$(WIN_TRIPLE) >/dev/null ); \
		( cd runtime/hosted && "$(PIN)" build --target=$(WIN_TRIPLE) --no-incremental --build-dir=../.bin/$(WIN_TRIPLE) >/dev/null ); \
	else \
		echo "==> [skip] windows runtime tiers: $(MINGW_GCC) absent."; \
	fi

# ---- freestanding runtime gate ----------------------------------------
# Build every `runtime/` tier and run its acceptance check on both supported
# OSes.  `runtime/` is a SEPARATE freestanding workspace with its own
# cryoconfig, so nothing else in this Makefile compiles it — which is exactly
# how it silently rotted once: the tiers still built, but codegen had moved on
# and the panic tier no longer linked.  This target is what makes that loud.
#
# A skipped arm FAILS.  The Windows half needs mingw + wine, and where those
# are absent the script used to return 0 having run half of itself and print
# OK - so a bare Linux box could be quoted as evidence the Windows tiers link.
# Accept that deliberately with `ARGS=--allow-skipped-arm` (the spelling
# selfhost-check uses); `FREESTANDING_LINUX_ONLY=1` still selects the skip, it
# just no longer hides it.
#
# Not host-branched until now, and on a Windows host that meant it could not be
# invoked at all: it depended on `compiler/build/cryo`, which a Windows build
# never produces, and its recipe is sh syntax under a cmd shell.  A gate with no
# local invocation on half the supported hosts is a gate only CI runs - and CI
# fires on `main` alone.  It now refuses out loud there instead, the same way
# examples-golden and valgrind-check do.
ifeq ($(HOST_OS),windows)
verify-freestanding:
	$(error verify-freestanding: UNAVAILABLE on this host -- the acceptance script is bash and links ELF objects natively; run it from WSL)
else
verify-freestanding: $(STAGE2)
	@CRYO="$(STAGE2)" CRYO_STDLIB="$(ROOT)/stdlib" bash runtime/verify-freestanding.sh $(ARGS)
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
# Every output the targets above write inside the tree, for every triple:
# the compiler, the stdlib and runtime tier archives, the LSP build and its
# installed copy, the test helper archives, the test suite's and the
# examples' build directories, the gates' state (.verify/ with its cached
# baseline compilers, .cross-check/, build-logs/) and the release staging
# (dist/).  Each is rebuilt by the target that wrote it.  The committed pins
# and the fetched .toolchains/ stay.
# Windows cannot delete a file a running process holds (an editor's
# cryolsp.exe), and `rmdir` does not report it, so the Windows recipe checks
# what is left afterwards and fails naming it.
ifeq ($(HOST_OS),windows)
clean:
	@echo ==^> Cleaning build outputs, test and example builds, and gate state
	@if exist compiler\build rmdir /s /q compiler\build
	@if exist stdlib\.bin rmdir /s /q stdlib\.bin
	@if exist runtime\.bin rmdir /s /q runtime\.bin
	@if exist tools\CryoLSP\build rmdir /s /q tools\CryoLSP\build
	@if exist bin\cryolsp.exe del /q bin\cryolsp.exe
	@if exist bin\cryolsp del /q bin\cryolsp
	@del /q tests\helpers\*.o tests\helpers\*.a 2>NUL || rem
	@for %%d in (tests\build .verify .cross-check build-logs dist) do @if exist %%d rmdir /s /q %%d
	@for /d %%p in (examples\* tests\tests\projects\*) do @if exist %%p\build rmdir /s /q %%p\build
	@for %%d in (compiler\build stdlib\.bin runtime\.bin tools\CryoLSP\build bin\cryolsp.exe) do @if exist %%d (echo clean: %%d is still there; a running process holds a file in it & exit /b 1)
else
clean:
	@echo "==> Cleaning build outputs, test and example builds, and gate state"
	@rm -rf compiler/build stdlib/.bin runtime/.bin
	@rm -rf tools/CryoLSP/build
	@rm -f bin/cryolsp bin/cryolsp.exe
	@rm -f $(TEST_HELPERS_DIR)/*.o $(TEST_HELPERS_DIR)/*.a
	@rm -rf tests/build .verify .cross-check build-logs dist
	@rm -rf examples/*/build tests/tests/projects/*/build
endif
