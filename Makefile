# Cryo top-level build orchestration.
#
# The compiler is self-hosted.  The pinned binaries at bin/cryo (Linux
# ELF) and bin/cryo.exe (Windows PE) bootstrap every other target - 
# every build flows through one of them depending on the host OS.
#
# `make help` lists every target, and every side effect a target has beyond
# the build output it names.

ROOT       := $(CURDIR)
# Every rule here names its recipe.  Make's built-in rules would have it look
# for a way to remake each of the several hundred source prerequisites,
# which on a Windows host is most of a no-change `make cryo`.
MAKEFLAGS  += --no-builtin-rules --no-builtin-variables
.SUFFIXES:
PIN        := $(ROOT)/bin/cryo
STAGE2     := $(ROOT)/compiler/build/cryo

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
    # those recipes on a stock Windows CI runner while a developer box without
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

# The triple of the operating system this host is not, for the gates that
# build the other OS's config-gated half (`warnings-check`).
ifeq ($(HOST_OS),windows)
OTHER_OS_TRIPLE := x86_64-pc-linux-gnu
else
OTHER_OS_TRIPLE := x86_64-pc-windows-gnu
endif
STAGE2_EXE   := $(ROOT)/compiler/build/cryo.exe
PIN_EXE      := $(ROOT)/bin/cryo.exe

# The triple a NATIVE build resolves to, asked of the pinned compiler.
#
# It cannot be derived from the host OS: the value comes from a toolchain probe
# (mingw `gcc` on PATH -> `...-windows-gnu`, an MSVC linker -> `...-windows-msvc`;
# on Linux whatever the linked libLLVM reports).  `cryo version --triple` prints
# exactly the triple a build names its archive directories by (`.bin/<triple>/`
# in stdlib/ and runtime/), so the directory named from it is the one the
# compiler fills.  Guessing is SILENT when wrong: warnings-check would delete
# another target's stdlib archives and then link the host's, already built.
#
# Asking means launching the pin, and the rules below name the triple in their
# prerequisites, so it would be asked while the Makefile is read.  The goals in
# NO_TRIPLE_GOALS build nothing and read no archive, so when every goal given
# is one of them the pin is not launched; any other goal asks once, up front.
#
# The answer is kept in `compiler/build/host-triple-<os>.mk`, remade when the
# pin is newer than it, so a build that changes nothing does not launch the
# pin to ask: starting it costs more than the rest of a no-change `make cryo`.
# One file per host OS: a Windows checkout's `compiler/build` is the one WSL
# builds in, and each host's pin answers its own triple.
NO_TRIPLE_GOALS := help clean
HOST_TRIPLE_MK  := $(ROOT)/compiler/build/host-triple-$(HOST_OS).mk
ifneq ($(filter-out $(NO_TRIPLE_GOALS),$(or $(MAKECMDGOALS),help)),)
-include $(HOST_TRIPLE_MK)
ifeq ($(HOST_OS),windows)
$(HOST_TRIPLE_MK): $(PIN_EXE)
	@if not exist "$(subst /,\,$(dir $@))" mkdir "$(subst /,\,$(dir $@))"
	@for /f %%t in ('"$(subst /,\,$(PIN_EXE))" version --triple') do @echo HOST_TRIPLE := %%t> "$(subst /,\,$@)"
else
$(HOST_TRIPLE_MK): $(PIN)
	@mkdir -p "$(dir $@)"
	@printf 'HOST_TRIPLE := %s\n' "$$("$(PIN)" version --triple)" > "$@"
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

# C startup hooks (a .preinit_array entry, a constructor, a destructor) for the
# linux_startup_runs_c_initializers_once project, which runs only on Linux.
# Built only on unix, like the C++ helper.
TEST_STARTUP_HELPERS_C := $(TEST_HELPERS_DIR)/startup_helpers.c
TEST_STARTUP_HELPERS_O := $(TEST_HELPERS_DIR)/startup_helpers-unix.o
TEST_STARTUP_HELPERS_A := $(TEST_HELPERS_DIR)/libstartuphelpers-unix.a

# NOOP is a recipe line that does nothing in either shell, so a target that
# only prints through `$(info)` does not also report "Nothing to be done".
ifeq ($(HOST_OS),windows)
    NOOP   := @rem
else
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
.PHONY: help cryo cryo-exe selfhost-check test test-list test-census verify roster-check verify-selftest incremental-instance-check incremental-runtime-check check-fast lsp-check cross-check vendor-check api-index api-index-check doc-coverage-check doc-coverage-update examples examples-golden valgrind-check verify-freestanding warnings-check runtime-abi-check pin \
        pin-impl pin-linux-impl pin-windows-impl _pin-windows-do verify-pin verify-pin-clean incremental-check \
        install uninstall clean lsp install-lsp cryogate cryosmith cryosmith-selftest smith-corpus-pin release release-linux release-windows install-check

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
  make cryo-exe          Cross-build compiler/build/cryo.exe from Linux, and
                         stamp it.
  make lsp               Build tools/CryoLSP from the current tree with the pin
                         and install it as bin/cryolsp.
  make install-lsp       lsp, the server's behavioural checks on the installed
                         binary, then package tools/CryoAnalyzer and install
                         it into VS Code, REPLACING the installed extension.
                         No gates; reload the VS Code window afterwards.
  make cryogate          Build the gate runner tools/CryoGate with the pin
                         (tools/CryoGate/build/); its runs log to
                         build-logs/cryogate/, newest 20 kept.  verify,
                         test-census, selfhost-check and pin run under its
                         heavy-run lock (see below).
  make cryosmith         Build the program generator tools/CryoSmith with the
                         pin (tools/CryoSmith/build/); see its DESIGN.md.

Tests and gates
  make test              cryo test over tests/ (the working loop; ARGS=filter).
                         Its single-file checks keep namespace-index caches
                         in tests/tests/negative/.bin (so does test-census).
  make test-list         List the discovered unit tests without running them.
  make test-census       The same run as test, with each suite's count, and
                         the unit tests and projects by name, asserted
                         against tests/test-roster.txt (evidence); a passing
                         collect project must have run at least one test.
  make roster-check      The discovered roster against tests/test-roster.txt
                         (ARGS=--merge after adding, --update after removing).
  make verify            Build once; census, examples, lsp-check, cross-check,
                         incremental-instance-check, incremental-runtime-check,
                         runtime-abi-check, CryoSmith's pinned corpus
                         (tests/smith-corpus, built in .verify/smith-corpus)
                         and check-fast run
                         concurrently and their objects are hashed
                         (ARGS="--baseline HEAD --require-identical";
                         --sequential runs them one at a time).
  make check-fast        verify-selftest, verify-pin and cryosmith-selftest;
                         builds only cryogate, its unit tests and cryosmith,
                         with the pin.
  make cryosmith-selftest
                         cryosmith's in-process checks: one seed, one
                         program; the feature switches; the static audit of
                         guarded division and shifts, and its plant; split
                         into parts run at once (`cryogate smith-selftest`,
                         CRYOGATE_JOBS of them, default the logical CPUs).
  make smith-corpus-pin  Re-pin tests/smith-corpus (verify's `smith` gate)
                         after a CryoSmith generator version change: seeds 1
                         to 24 built at -O0 and -O2 by the compiler under
                         test, each one's hash written beside it.  Writes in
                         .verify/smith-corpus.
  make verify-selftest   cryogate's checks (verify's, selfhost's and
                         verify-pin's among them), driven through
                         fixtures they must accept and refuse.
  make lsp-check         tools/CryoLSP builds with the compiler under test
                         (into its own directory; installs nothing), and
                         the binary passes its behavioural checks
                         (`cryogate lsp`).
  make warnings-check    compiler/, the stdlib and the runtime build with
                         -Werror under the compiler under test, the stdlib
                         under both panic strategies and the runtime in each
                         configuration, for both operating systems (the
                         other one by --target, objects only into
                         build-logs/); DELETES the host's prebuilt stdlib
                         archives first (into build-logs/).
  make cross-check       runtime (each configuration), stdlib, compiler,
                         CryoLSP, CryoGate and CryoSmith for the other OS's triple,
                         objects only
                         (ARGS=--triple=...).
  make examples          Build every examples/ project.
  make examples-golden   Build every example, run the ones with an
                         expected.out and compare their output with it.
  make valgrind-check    The same runs under valgrind (Linux only).
  make incremental-check Incremental build == clean build, per module: the
                         objects, and the program where the link reproduces
                         (not on Windows: a PE carries a link timestamp);
                         edits compiler/src while it runs and writes
                         .verify/incremental-check.
  make incremental-instance-check  An edit asking a cached module for a new
                         generic instance still links.
  make incremental-runtime-check  A program is linked again when its runtime
                         is rebuilt with nothing else changed; writes
                         .verify/incremental-runtime.
  make vendor-check      Every constant shape survives `cryo vendor`.
  make verify-freestanding  The runtime's freestanding acceptance check
                         (from a Windows host, run inside WSL).
  make runtime-abi-check The lang-item registry (runtime/abi/lang_items.def)
                         against the runtime's three configurations, and the
                         `__cryo_` names compiler/src and stdlib/ spell.
                         Builds each configuration first; a verify gate.
  make selfhost-check    Byte-identity fixed point.  Linux host: the Linux
                         chain, then the Windows chain under wine (a skipped
                         arm fails unless ARGS=--allow-skipped-arm).  Windows
                         host: the Linux chain in WSL, from a digest-checked
                         copy of its sources on WSL's own filesystem, at the
                         same time as the Windows chain natively.
  make api-index         Regenerate docs/stdlib-api.txt.
  make api-index-check   Fail if docs/stdlib-api.txt is stale.
  make doc-coverage-check  The stdlib's undocumented public declarations are
                         exactly docs/doc-coverage.txt (tools/CryoDocCoverage,
                         built with the compiler under test into build-logs/,
                         after its own self-test).
  make doc-coverage-update Rewrite docs/doc-coverage.txt to the current set.

Pins and release
  make pin               Refresh bin/cryo and bin/cryo.exe and their sidecars
                         (Windows host: through WSL).
  make verify-pin        Each pin matches its sidecar's sha256 and is stamped
                         with the commit the sidecar names, both name the
                         same commit, and the Windows pin has no 32-bit base
                         relocation (`cryogate verify-pin`; builds cryogate).
  make verify-pin-clean  The same, and the pins were built from a clean tree.
  make release           Alias of release-linux.
  make release-linux     dist/ tarball with a static, stamped cryo (POSIX).
  make release-windows   dist/ zip (cross-built on a POSIX host; Git Bash
                         builds it natively on Windows).
  make install-check ARCHIVE=<file>
                         The host OS's installer over a release archive, in a
                         scratch home: install, build and run a two-module
                         program, the install left byte-identical by a build
                         that needs archives it does not ship, a build with no
                         C toolchain refused with E0705, upgrade, uninstall
                         (`cryogate install-check`).
  make install           Symlink bin/cryo and the stdlib into a prefix (POSIX).
  make uninstall         Remove those symlinks.
  make clean             Remove every build output in the tree: compiler,
                         stdlib, runtime archives, LSP, test helpers, test and
                         example builds, .verify/, .cross-check/,
                         build-logs/ and dist/.

Internal, called by pin: pin-impl (the refresh, without the lock),
pin-linux-impl, pin-windows-impl, _pin-windows-do.

Side effects beyond the named output
  lsp             replaces bin/cryolsp and tools/CryoLSP/build/cryolsp (Windows:
                  moves held copies into tools/CryoLSP/build/retired/).
  install-lsp     uninstalls the VS Code extension it replaces.
  pin             rewrites both pins; copies LLVM-C.dll and libclang.dll to bin/.
  selfhost-check  Linux host: deletes stdlib/.bin and compiler/build.  Windows
                  host: rewrites stdlib/.bin/<triple> and compiler/build/self,
                  and keeps the Linux arm's copy of its sources in WSL's
                  ~/.cache/cryo-selfhost/.
  test, examples, lsp-check and every gate that needs the compiler rebuild
                  it first when any .cryo under compiler/src or stdlib is
                  newer than it.  Any build of a program by a compiler that
                  has no stdlib archive for its key builds one into
                  stdlib/.bin/<triple>/prebuilt/<key>/ and removes those
                  there beyond the six used most recently and idle for an
                  hour.  A link whose C driver has not answered before
                  keeps its answers in stdlib/.bin/<triple>/toolchain/.
  verify         runs `make check-fast` from inside itself, and keeps a
                  clone and baseline compilers under .verify/ until
                  `make clean`; clears stdlib/.bin/*/prebuilt/ before each
                  population run.
  install         writes symlinks outside the repository (may need sudo).
  verify, test-census, selfhost-check, pin, examples-golden, valgrind-check
                  hold the heavy-run lock, cryo-heavy-run.lock in the
                  directory holding the MAIN checkout, the one file for every
                  worktree (CRYOGATE_HEAVY_LOCK names another), while their
                  recipe runs (their prerequisites build before it is
                  taken): one waits, polling each minute,
                  while another checkout's heavy run holds it.  The file
                  names its holder's process id; an interrupted run leaves
                  it behind, and the next run that finds that process gone
                  takes it over.  A lock naming no process (one taken by
                  hand) older than three hours is refused; delete it once
                  nobody holds it.
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

# ---- self-hosted compiler via the pin ---------------------------------
# The pin links the compiler against the stdlib archive and the runtime
# archive it builds for its own key, under stdlib/.bin/<triple>/prebuilt/ and
# runtime/.bin/<triple>/, the first time it needs each, so nothing here builds
# either.
#
# The linked compiler is then stamped (`cryo stamp`) with the checkout it was
# built from, which is what `cryo version` reports.  The stamp is a region of
# the binary rewritten in place, so no object changes.  The pin outputs are
# excluded from the dirty check, as `cryogate pin` excludes them from its own:
# a pin refresh rewrites bin/ between stamping its two halves.
#
# `cryo` names the binary, and the binary's rule runs only when a prerequisite
# is newer than it - any .cryo under compiler/src or stdlib, the compiler's
# cryoconfig and C headers, the runtime, the pin -
# so a build with nothing changed launches no compiler.  `.git/index` is a
# prerequisite too: a commit rewrites it, and the stamp then names the new
# commit.  The binary is touched last: an up-to-date build or an unchanged
# stamp leaves it as it was, older than what made the rule run.
CRYO_PROFILE  := release
STAMP_EXCLUDE := bin/cryo,bin/cryo.pin.txt,bin/cryo.exe,bin/cryo.exe.pin.txt
RUNTIME_SOURCES := $(call rwildcard,$(ROOT)/runtime,*.cryo) $(ROOT)/runtime/cryoconfig
ifeq ($(HOST_OS),windows)
STAGE2_FILE := $(STAGE2_EXE)
STAGE2_PIN  := $(PIN_EXE)
else
STAGE2_FILE := $(STAGE2)
STAGE2_PIN  := $(PIN)
endif

# The gate runner (tools/CryoGate, built below by `make cryogate`); a recipe
# runs it as `$(CRYOGATE_COMMAND) <gate> ...`.  A heavy
# target (verify, test-census, selfhost-check, pin, examples-golden,
# valgrind-check) runs its recipe as
# `$(CRYOGATE_HEAVY) <command>`: under the machine's heavy-run lock, beside
# the main checkout, waited for while another checkout's heavy run holds it, and
# released when the command ends.  cmd cannot run a quoted forward-slash path.
CRYOGATE_DIR    := $(ROOT)/tools/CryoGate
CRYOGATE        := $(CRYOGATE_DIR)/build/cryogate$(EXE_SUFFIX)
CRYOGATE_TESTS  := $(CRYOGATE_DIR)/build/cryogate-test$(EXE_SUFFIX)
CRYOGATE_INPUTS := $(call rwildcard,$(CRYOGATE_DIR)/src,*.cryo) $(CRYOGATE_DIR)/cryoconfig \
                   $(call rwildcard,$(ROOT)/stdlib,*.cryo) $(RUNTIME_SOURCES) $(STAGE2_PIN)
ifeq ($(HOST_OS),windows)
CRYOGATE_COMMAND := "$(subst /,\,$(CRYOGATE))"
else
CRYOGATE_COMMAND := "$(CRYOGATE)"
endif
CRYOGATE_HEAVY  := $(CRYOGATE_COMMAND) heavy --

# The program generator (tools/CryoSmith, built below by `make cryosmith`),
# built and run like cryogate.
CRYOSMITH_DIR    := $(ROOT)/tools/CryoSmith
CRYOSMITH        := $(CRYOSMITH_DIR)/build/cryosmith$(EXE_SUFFIX)
CRYOSMITH_INPUTS := $(call rwildcard,$(CRYOSMITH_DIR)/src,*.cryo) $(CRYOSMITH_DIR)/cryoconfig \
                    $(call rwildcard,$(ROOT)/stdlib,*.cryo) $(RUNTIME_SOURCES) $(STAGE2_PIN)
ifeq ($(HOST_OS),windows)
CRYOSMITH_COMMAND := "$(subst /,\,$(CRYOSMITH))"
else
CRYOSMITH_COMMAND := "$(CRYOSMITH)"
endif
STAGE2_INPUTS := $(CRYO_SOURCES) $(ROOT)/compiler/cryoconfig $(wildcard $(ROOT)/compiler/*.h) \
                 $(RUNTIME_SOURCES) $(STAGE2_PIN) $(wildcard $(ROOT)/.git/index)

cryo: $(STAGE2_FILE)

ifeq ($(HOST_OS),windows)
$(STAGE2_EXE): $(STAGE2_INPUTS)
	@echo "==> Building self-hosted cryo via bin/cryo.exe"
	@cd compiler && "$(subst /,\,$(PIN_EXE))" build --profile=$(CRYO_PROFILE)
	@"$(subst /,\,$(PIN_EXE))" stamp "$(STAGE2_EXE)" --profile=$(CRYO_PROFILE) --exclude=$(STAMP_EXCLUDE)
	@copy /b "$(subst /,\,$(STAGE2_EXE))" +,, "$(subst /,\,$(STAGE2_EXE))" >NUL
	@echo "==> Self-hosted cryo built: $(STAGE2_EXE)"
else
$(STAGE2): $(STAGE2_INPUTS)
	@echo "==> Building self-hosted cryo via bin/cryo"
	@cd compiler && "$(PIN)" build --profile=$(CRYO_PROFILE)
	@"$(PIN)" stamp "$(STAGE2)" --profile=$(CRYO_PROFILE) --exclude=$(STAMP_EXCLUDE)
	@touch "$(STAGE2)"
	@echo "==> Self-hosted cryo built: $(STAGE2)"

# On a POSIX host `cryo.exe` is the cross build `cryo-exe` makes; a target
# depending on it builds the native compiler first, as it always has.
$(STAGE2_EXE): $(CRYO_SOURCES)
	@$(MAKE) --no-print-directory cryo
endif

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
# The self-hosted compiler builds the runtime for the windows triple itself,
# on first use, as it does for any target it links.
cryo-exe: cryo
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
# `cryogate pin --through-wsl` checks for wsl.exe, resolves the checkout's
# WSL path, and re-enters this Makefile inside WSL at `pin-impl`, which
# takes no lock: this host's run holds it.
pin: $(CRYOGATE)
	@$(CRYOGATE_HEAVY) $(CRYOGATE_COMMAND) pin --through-wsl
else
# Linux/macOS host: native build for bin/cryo, cross-build for bin/cryo.exe.
# The Windows half is skipped (not failed) when the mingw toolchain or
# the fetched .toolchains artifacts are absent, so a Linux-only checkout
# can still refresh the Linux pin without dragging in the Windows bits.
pin: $(CRYOGATE)
	@$(CRYOGATE_HEAVY) $(MAKE) --no-print-directory pin-impl

pin-impl: pin-linux-impl pin-windows-impl

# The Linux half builds cryogate before it rewrites bin/cryo, one of
# cryogate's inputs; the Windows half runs after it and names no cryogate
# prerequisite at all.  make remakes an out-of-date prerequisite even when
# it is order-only, so naming cryogate there rebuilds it halfway through,
# over the running `cryogate heavy` that holds the lock.
pin-linux-impl: cryo | $(CRYOGATE)
	@$(CRYOGATE_COMMAND) pin --source "$(STAGE2)" --pin "$(PIN)" --stamp-profile=$(CRYO_PROFILE)

pin-windows-impl: pin-linux-impl
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
	@$(CRYOGATE_COMMAND) pin --source "$(STAGE2_EXE)" --pin "$(PIN_EXE)" --strip-tool $(MINGW_STRIP) \
		--stamper "$(STAGE2)" --stamp-profile=$(CRYO_PROFILE)
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
# would drag `make cryo` (a full self-host) in front of every LSP build for
# no benefit.
# What the server knows of the language is the compiler SOURCE it links, which
# the project build always compiles from the tree; the pin only does the
# compiling.
ifeq ($(HOST_OS),windows)
# Windows host: recipes run under cmd, which has no `cp`.  `$(PIN)` (bin/cryo,
# no extension) launches via PATHEXT -> bin/cryo.exe.  The built binary is
# `cryolsp.exe`; copy it to the pin with the `copy` builtin (backslash paths,
# stdout silenced).
#
# A running server holds its binary open, and Windows refuses to overwrite it
# but allows renaming it, so both installed copies are moved into
# build\retired\ first; the running servers keep the old code until the
# editor restarts them.  A retired copy still held stays until a later run
# can delete it.  A failed build leaves no server installed, so the editor
# reports a missing server rather than starting a stale one.
LSP_RETIRED := tools\CryoLSP\build\retired
lsp:
	@echo "==> Building CryoLSP via bin/cryo.exe"
	-@if exist $(LSP_RETIRED) del /Q $(LSP_RETIRED)\*
	@if not exist $(LSP_RETIRED) mkdir $(LSP_RETIRED)
	@if exist tools\CryoLSP\build\cryolsp.exe move /Y tools\CryoLSP\build\cryolsp.exe $(LSP_RETIRED)\build-%RANDOM%.exe
	@if exist bin\cryolsp.exe move /Y bin\cryolsp.exe $(LSP_RETIRED)\bin-%RANDOM%.exe
	cd tools/CryoLSP && "$(PIN)" build
	copy /Y "$(subst /,\,$(LSP_BIN))" "$(subst /,\,$(LSP_PIN))" >NUL
	@echo "==> bin/cryolsp.exe ready"
else
# `mv` over the installed copy replaces its directory entry, so a running
# server keeps the file it has open and the next start gets the new one.
lsp: $(PIN)
	@echo "==> Building CryoLSP via bin/cryo"
	@cd tools/CryoLSP && "$(PIN)" build
	@cp "$(LSP_BIN)" "$(LSP_PIN).new"
	@mv -f "$(LSP_PIN).new" "$(LSP_PIN)"
	@echo "==> bin/cryolsp ready"
endif

# ---- cryogate -----------------------------------------------------------
# The gate runner (tools/CryoGate), built by the pin against the tree's
# stdlib and linked against the stdlib only, so a compiler under test that
# does not build or misbehaves cannot break the program that reports it.
# Rebuilt when its sources, the stdlib, the runtime's sources or the pin are
# newer than the binary.  Its variables are set beside the pin's, above,
# because the heavy targets name it as a prerequisite.  Its unit tests are
# built here too, by listing them (`cryo test --list` builds the test binary
# and prints its tests), and `cryogate selftest` runs that binary: a check-fast
# run inside verify then builds nothing with the pin, whose stdlib archive
# would otherwise land among the archives verify hashes.
cryogate: $(CRYOGATE) $(CRYOGATE_TESTS)

# Each binary is touched last, as the compiler is: a build the pin finds up
# to date leaves it as it was, older than the input that made the rule run,
# and the rule would then run the pin on every later make - inside verify,
# too, where the pin's stdlib archive lands among the archives it hashes.
ifeq ($(HOST_OS),windows)
$(CRYOGATE): $(CRYOGATE_INPUTS)
	@echo "==> Building cryogate via bin/cryo.exe"
	@cd tools/CryoGate && "$(subst /,\,$(PIN_EXE))" build
	@copy /b "$(subst /,\,$(CRYOGATE))" +,, "$(subst /,\,$(CRYOGATE))" >NUL
$(CRYOGATE_TESTS): $(CRYOGATE_INPUTS) $(call rwildcard,$(CRYOGATE_DIR)/tests,*.cryo)
	@echo "==> Building cryogate's unit tests via bin/cryo.exe"
	@cd tools/CryoGate && "$(subst /,\,$(PIN_EXE))" test --list
	@copy /b "$(subst /,\,$(CRYOGATE_TESTS))" +,, "$(subst /,\,$(CRYOGATE_TESTS))" >NUL
else
$(CRYOGATE): $(CRYOGATE_INPUTS)
	@echo "==> Building cryogate via bin/cryo"
	@cd tools/CryoGate && "$(PIN)" build
	@touch "$(CRYOGATE)"
$(CRYOGATE_TESTS): $(CRYOGATE_INPUTS) $(call rwildcard,$(CRYOGATE_DIR)/tests,*.cryo)
	@echo "==> Building cryogate's unit tests via bin/cryo"
	@cd tools/CryoGate && "$(PIN)" test --list
	@touch "$(CRYOGATE_TESTS)"
endif

# ---- cryosmith ----------------------------------------------------------
# The program generator (tools/CryoSmith), built by the pin against the
# tree's stdlib and linked against the stdlib only: it writes program text
# and needs nothing from the compiler it helps to test.  Rebuilt when its
# sources, the stdlib, the runtime's sources or the pin are newer than the
# binary.  `cryosmith selftest` builds nothing, so check-fast runs it.  The
# binary is touched last, as cryogate's is.
cryosmith: $(CRYOSMITH)

ifeq ($(HOST_OS),windows)
$(CRYOSMITH): $(CRYOSMITH_INPUTS)
	@echo "==> Building cryosmith via bin/cryo.exe"
	@cd tools/CryoSmith && "$(subst /,\,$(PIN_EXE))" build
	@copy /b "$(subst /,\,$(CRYOSMITH))" +,, "$(subst /,\,$(CRYOSMITH))" >NUL
else
$(CRYOSMITH): $(CRYOSMITH_INPUTS)
	@echo "==> Building cryosmith via bin/cryo"
	@cd tools/CryoSmith && "$(PIN)" build
	@touch "$(CRYOSMITH)"
endif

cryosmith-selftest: $(CRYOSMITH) $(CRYOGATE)
	@$(CRYOGATE_COMMAND) smith-selftest --smith "$(CRYOSMITH)"

# Re-pin tests/smith-corpus, the corpus verify's `smith` gate checks, after
# the generator version changes: seeds 1 to 24 at the default features, each
# built at -O0 and -O2 and interpreted by the compiler under test, and each
# hash written beside its seed once every program passes, the interpreter
# agreeing on all of them.  `cryogate corpus-check --pin --interpret`.  The
# known-compiler-bug list, tests/smith-known-bugs, is at the generator
# version too, and is written at the new one by hand: each entry's seed is
# run again and kept only where it still meets its bug.
smith-corpus-pin: cryo $(CRYOGATE) $(CRYOSMITH)
	@$(CRYOGATE_HEAVY) $(CRYOGATE_COMMAND) corpus-check --cryo "$(STAGE2_FILE)" --stdlib "$(ROOT)/stdlib" --work .verify/smith-corpus --pin tests/smith-corpus --first-seed 1 --count 24 --interpret

# ---- CryoAnalyzer VS Code extension -----------------------------------
# Builds and installs the server from the current tree (`lsp`), runs its
# behavioural checks on the installed binary, then packages
# tools/CryoAnalyzer/ into a .vsix and installs it into VS Code, evicting any
# previously installed copy and any stale .vsix artifacts so the install
# always reflects the current source.  The extension finds the server itself
# (tools/CryoLSP/build/ in this workspace, bin/ through PATH elsewhere), and
# `lsp` writes both, so neither can be left behind.
install-lsp: lsp $(CRYOGATE)
ifeq ($(HOST_OS),windows)
# Windows host: the POSIX recipe below (`command -v`, `{ ... }`, `[ -d ]`,
# `rm`, `./node_modules/.bin/...`) is unparseable by cmd, so the whole job
# lives in a batch wrapper - same single-line shape as `pin`.  It uses
# `where`/`if exist`/`del` and the `.cmd` shims (`code.cmd`, `npm.cmd`,
# `vsce.cmd`) that npm installs on Windows.
install-lsp:
	@$(CRYOGATE_COMMAND) lsp-smoke "$(LSP_PIN)"
	scripts\install-lsp-windows.cmd
else
install-lsp:
	@$(CRYOGATE_COMMAND) lsp-smoke "$(LSP_PIN)"
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
# `cryogate selfhost`: per-stage progress and timings, per-stage logs in
# build-logs/cryogate/, and the tail of a stage that failed.
#
# It is host-aware:
#   Linux host:   runs the full Linux chain natively, then the
#                 wine-based Windows chain.  Without the mingw toolchain,
#                 wine and .toolchains that arm is skipped, and a skipped arm
#                 fails the check unless ARGS=--allow-skipped-arm says so.
#   Windows host: runs the Linux chain in WSL, from a copy of its
#                 sources on WSL's own filesystem (refused unless its digest
#                 is the working tree's), at the same time as the
#                 Windows chain natively.  Requires wsl.exe on PATH.
selfhost-check: $(PIN) $(CRYOGATE)
	@$(CRYOGATE_HEAVY) $(CRYOGATE_COMMAND) selfhost $(ARGS)

# ---- pin self-verification --------------------------------------------
# Assert each committed pin's bytes match its .pin.txt sidecar sha256
# (catches a binary committed without regenerating its sidecar, corruption,
# or a hand-edited sidecar), and that the commit `cryo stamp` wrote into each
# pin is the one its sidecar names, that both sidecars name one commit, and
# that the Windows pin carries no 32-bit base relocation.  `cryogate
# verify-pin` reads the stamp from the bytes, so both pins are checked on
# either host; cryogate is built by the pin it checks, which reads the pin's
# bytes, not its behaviour.
#   verify-pin        integrity (sha256 and stamped commit == sidecar)
#   verify-pin-clean  ALSO require a clean-worktree pin - the release gate
#                     (a pin built from a dirty tree isn't reproducible).
verify-pin: $(CRYOGATE)
	@$(CRYOGATE_COMMAND) verify-pin

verify-pin-clean: $(CRYOGATE)
	@$(CRYOGATE_COMMAND) verify-pin --require-clean

# ---- incremental-build soundness --------------------------------------
# Run the per-module incremental byte-identity matrix: every incremental
# build of the compiler project must leave the same objects as a clean
# `--no-incremental` build of the same source, and the same program where the
# host links reproducibly.  `cryogate incr --matrix`; its builds go to
# .verify/incremental-check, and it edits compiler/src while it runs.
incremental-check: cryo $(CRYOGATE)
	@$(CRYOGATE_COMMAND) incr --matrix --cryo "$(STAGE2_FILE)"

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

# C startup-hook archive for the linux_startup_runs_c_initializers_once project.
$(TEST_STARTUP_HELPERS_A): $(TEST_STARTUP_HELPERS_C)
	@echo "==> Building C startup-hook helper archive"
	@cc -O0 -fPIC -c -o $(TEST_STARTUP_HELPERS_O) $<
	@ar rcs $@ $(TEST_STARTUP_HELPERS_O)

ifeq ($(HOST_OS),windows)
# Native Windows host: recipes run under cmd, which can't execute a quoted
# forward-slash path and needs the `.exe` the native build emits - so the
# stage-2 compiler is `compiler\build\cryo.exe` (backslashes, suffix), built
# through the host-branched `cryo` target above.  $(STAGE2_EXE) is a file
# target, so the compiler is rebuilt only when an input is newer, matching the
# Linux semantics.
STAGE2_EXE_WIN := $(subst /,\,$(STAGE2_EXE))
test: $(STAGE2_EXE) $(TEST_HELPERS_A)
	@cd tests && "$(STAGE2_EXE_WIN)" test $(ARGS)

test-list: $(STAGE2_EXE) $(TEST_HELPERS_A)
	@cd tests && "$(STAGE2_EXE_WIN)" test --list $(ARGS)

roster-check: $(STAGE2_EXE) $(TEST_HELPERS_A) $(CRYOGATE)
	@$(CRYOGATE_COMMAND) roster --cryo "$(STAGE2_FILE)" $(ARGS)

test-census: $(STAGE2_EXE) $(TEST_HELPERS_A) $(CRYOGATE)
	@$(CRYOGATE_HEAVY) $(CRYOGATE_COMMAND) census --cryo "$(STAGE2_FILE)" -- $(ARGS)
else
test: $(STAGE2) $(TEST_HELPERS_A) $(TEST_CPP_HELPERS_A) $(TEST_STARTUP_HELPERS_A)
	@cd tests && "$(STAGE2)" test $(ARGS)

test-list: $(STAGE2) $(TEST_HELPERS_A) $(TEST_CPP_HELPERS_A) $(TEST_STARTUP_HELPERS_A)
	@cd tests && "$(STAGE2)" test --list $(ARGS)

# Golden-file gate on the DISCOVERED unit-test roster: a compiler change
# that silently breaks `![test]` discovery would otherwise stay green
# ("0 of 0 failed" is a pass).  Re-pin with ARGS=--merge when adding tests,
# ARGS=--update when removing them.  `cryogate roster`.
roster-check: $(STAGE2) $(TEST_HELPERS_A) $(TEST_CPP_HELPERS_A) $(TEST_STARTUP_HELPERS_A) $(CRYOGATE)
	@$(CRYOGATE_COMMAND) roster --cryo "$(STAGE2_FILE)" $(ARGS)

# `make test` with the counts asserted.  The runner reports a zero the same way
# it reports success - silently - so an exit code cannot tell a suite that ran
# everything from one that ran nothing.  This runs the same suite (its output
# in the run's log; the tail printed when it fails), and then reconciles what
# ran against the roster golden.  Use it wherever the run is being taken as
# evidence; `make test` stays for the working loop, where a pattern filter is
# the point.  ARGS are passed to `cryo test`.  `cryogate census`.
test-census: $(STAGE2) $(TEST_HELPERS_A) $(TEST_CPP_HELPERS_A) $(TEST_STARTUP_HELPERS_A) $(CRYOGATE)
	@$(CRYOGATE_HEAVY) $(CRYOGATE_COMMAND) census --cryo "$(STAGE2_FILE)" -- $(ARGS)
endif

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
# $CRYO_HOME and leaves nothing on the machine.  `cryogate vendor`.
vendor-check: $(STAGE2_FILE) $(CRYOGATE)
	@$(CRYOGATE_COMMAND) vendor --cryo "$(STAGE2_FILE)"

# ---- one verification run ----------------------------------------------
# Build once, then run census, examples, lsp-check, cross-check,
# incremental-instance-check, incremental-runtime-check, runtime-abi-check,
# CryoSmith's pinned corpus (tests/smith-corpus) and check-fast at the same
# time, and hash the objects the census and examples compiled.
# ARGS="--baseline HEAD" also compares those objects against a compiler built
# from HEAD's compiler/src (in a clone under .verify/, cached), which is the
# evidence that a change moved no compiled output.  `cryogate verify`.
#
# Everything it needs built is a prerequisite HERE, before any gate starts:
# the gates run concurrently, and a phony prerequisite rebuilt by one of them
# (a test helper archive) would be rewritten while another is linking against it.
# cryogate's unit tests and cryosmith are two: check-fast runs them, and
# building them there makes the pin's own stdlib archive among the archives
# verify hashes.
ifeq ($(HOST_OS),windows)
verify: cryo $(TEST_HELPERS_A) $(CRYOGATE) $(CRYOGATE_TESTS) $(CRYOSMITH)
	@$(CRYOGATE_HEAVY) $(CRYOGATE_COMMAND) verify --cryo "$(STAGE2_EXE)" $(ARGS)
else
verify: cryo $(TEST_HELPERS_A) $(TEST_CPP_HELPERS_A) $(TEST_STARTUP_HELPERS_A) $(CRYOGATE) $(CRYOGATE_TESTS) $(CRYOSMITH)
	@$(CRYOGATE_HEAVY) $(CRYOGATE_COMMAND) verify --cryo "$(STAGE2)" $(ARGS)
endif

# An incremental build after an edit that asks an unchanged module for a new
# generic instance must link (tests/fixtures/incremental-new-instance).
incremental-instance-check: cryo $(CRYOGATE)
	@$(CRYOGATE_COMMAND) incr --instance --cryo "$(STAGE2_FILE)"

# A program built against a runtime must be linked again when the runtime
# is rebuilt with nothing else changed (tests/fixtures/incremental-runtime-change).
incremental-runtime-check: cryo $(CRYOGATE)
	@$(CRYOGATE_COMMAND) incr --runtime --cryo "$(STAGE2_FILE)"

# cryogate's own checks (the judges behind verify's declarations and
# selfhost's stage-archive comparison, the roster merge, the census and
# verify-pin's checks among them), driven through every case they must
# refuse and every case they must allow.
verify-selftest: $(CRYOGATE) $(CRYOGATE_TESTS)
	@$(CRYOGATE_COMMAND) selftest

# ---- the pre-commit sweep ----------------------------------------------
# Every gate here is source-derived; the only things it builds are cryogate,
# its unit tests and cryosmith, with the pin, and only when they are stale,
# so run inside verify it makes no stdlib archive the pin's other builds do
# not.  A short gate everybody runs is worth more than a twenty-minute one
# nobody does.
check-fast: verify-selftest verify-pin cryosmith-selftest
	@echo "check-fast: OK (cryogate's self-test, pin integrity, cryosmith's self-test)"

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
lsp-check: $(STAGE2_FILE) $(CRYOGATE)
	@$(CRYOGATE_COMMAND) lsp --cryo "$(STAGE2_FILE)" $(ARGS)

# The compiler's own source and the whole stdlib build with no warning.  A
# stdlib warning is reported only by the build that MAKES the archive, so the
# host's prebuilt archives are removed first and the compiler under test
# builds its own with `-Werror`, then builds compiler/ with `-Werror` into
# build-logs/warnings-check - from inside compiler/, whose cryoconfig names
# the LLVM libraries relative to it.  The compiler unwinds, so that archive is
# the `--panic=unwind` one; config gating removes a different set of
# declarations under `--panic=abort`, which is what a program builds with by
# default, so the stdlib is built once more on its own under it.  Config
# gating also removes the other operating system's half before analysis, so
# the stdlib is built for that OS's triple under both strategies too - objects
# only, as `cross-check` builds it - or a warning in `![target(unix)]` code is
# never seen on a Windows host, nor one in `![target(windows)]` code on Linux.
# The runtime is a program's archive the same way, its declarations gated on
# its configuration, so it is built on its own in each, for both OSes.
ifeq ($(HOST_OS),windows)
warnings-check: $(STAGE2_EXE)
	@if exist "stdlib\.bin\$(HOST_TRIPLE)\prebuilt" rmdir /s /q "stdlib\.bin\$(HOST_TRIPLE)\prebuilt"
	@cd compiler && "$(STAGE2_EXE)" build --build-dir=$(ROOT)/build-logs/warnings-check --no-incremental -Werror
	@"$(STAGE2_EXE)" build stdlib --build-dir=$(ROOT)/build-logs/warnings-check/stdlib-panic-abort --no-incremental --panic=abort -Werror
	@"$(STAGE2_EXE)" build stdlib --target=$(OTHER_OS_TRIPLE) --build-dir=$(ROOT)/build-logs/warnings-check/other-os-panic-unwind --no-incremental --panic=unwind -Werror
	@"$(STAGE2_EXE)" build stdlib --target=$(OTHER_OS_TRIPLE) --build-dir=$(ROOT)/build-logs/warnings-check/other-os-panic-abort --no-incremental --panic=abort -Werror
	@"$(STAGE2_EXE)" build runtime --build-dir=$(ROOT)/build-logs/warnings-check/runtime-freestanding --no-incremental --no-runtime -Werror
	@"$(STAGE2_EXE)" build runtime --build-dir=$(ROOT)/build-logs/warnings-check/runtime-hosted-abort --no-incremental --panic=abort -Werror
	@"$(STAGE2_EXE)" build runtime --build-dir=$(ROOT)/build-logs/warnings-check/runtime-hosted-unwind --no-incremental --panic=unwind -Werror
	@"$(STAGE2_EXE)" build runtime --target=$(OTHER_OS_TRIPLE) --build-dir=$(ROOT)/build-logs/warnings-check/other-os-runtime-freestanding --no-incremental --no-runtime -Werror
	@"$(STAGE2_EXE)" build runtime --target=$(OTHER_OS_TRIPLE) --build-dir=$(ROOT)/build-logs/warnings-check/other-os-runtime-hosted-abort --no-incremental --panic=abort -Werror
	@"$(STAGE2_EXE)" build runtime --target=$(OTHER_OS_TRIPLE) --build-dir=$(ROOT)/build-logs/warnings-check/other-os-runtime-hosted-unwind --no-incremental --panic=unwind -Werror
	@echo warnings-check: OK -- compiler, stdlib and runtime built with -Werror, the stdlib under both panic strategies and the runtime in each configuration, for both operating systems
else
warnings-check: $(STAGE2)
	@rm -rf "stdlib/.bin/$(HOST_TRIPLE)/prebuilt"
	@cd compiler && "$(STAGE2)" build --build-dir=$(ROOT)/build-logs/warnings-check --no-incremental -Werror
	@"$(STAGE2)" build stdlib --build-dir=$(ROOT)/build-logs/warnings-check/stdlib-panic-abort --no-incremental --panic=abort -Werror
	@"$(STAGE2)" build stdlib --target=$(OTHER_OS_TRIPLE) --build-dir=$(ROOT)/build-logs/warnings-check/other-os-panic-unwind --no-incremental --panic=unwind -Werror
	@"$(STAGE2)" build stdlib --target=$(OTHER_OS_TRIPLE) --build-dir=$(ROOT)/build-logs/warnings-check/other-os-panic-abort --no-incremental --panic=abort -Werror
	@"$(STAGE2)" build runtime --build-dir=$(ROOT)/build-logs/warnings-check/runtime-freestanding --no-incremental --no-runtime -Werror
	@"$(STAGE2)" build runtime --build-dir=$(ROOT)/build-logs/warnings-check/runtime-hosted-abort --no-incremental --panic=abort -Werror
	@"$(STAGE2)" build runtime --build-dir=$(ROOT)/build-logs/warnings-check/runtime-hosted-unwind --no-incremental --panic=unwind -Werror
	@"$(STAGE2)" build runtime --target=$(OTHER_OS_TRIPLE) --build-dir=$(ROOT)/build-logs/warnings-check/other-os-runtime-freestanding --no-incremental --no-runtime -Werror
	@"$(STAGE2)" build runtime --target=$(OTHER_OS_TRIPLE) --build-dir=$(ROOT)/build-logs/warnings-check/other-os-runtime-hosted-abort --no-incremental --panic=abort -Werror
	@"$(STAGE2)" build runtime --target=$(OTHER_OS_TRIPLE) --build-dir=$(ROOT)/build-logs/warnings-check/other-os-runtime-hosted-unwind --no-incremental --panic=unwind -Werror
	@echo "warnings-check: OK -- compiler, stdlib and runtime built with -Werror, the stdlib under both panic strategies and the runtime in each configuration, for both operating systems"
endif

# ---- the other OS's config-gated half ----------------------------------
# Config gating prunes `![config(linux)]` from a Windows build before name
# resolution sees it, and `![config(windows)]` from a Linux one, so every
# host-native gate measures a tree with the other OS's half cut out.  This
# compiles runtime/ (as each configuration a link names), stdlib/, compiler/,
# tools/CryoLSP, tools/CryoGate and tools/CryoSmith (which the pin builds to run
# the gates and to generate programs, so only this build shows the compiler
# under test refusing them) for the other OS with
# `--target`, which selects that OS's gates and stops at object files - no
# cross toolchain, no link, no WSL.  A declaration that stops resolving on
# the other OS is refused here, on this host, in about a minute.
#
# Built with the COMPILER UNDER TEST for the same reason `lsp-check` is:
# the checking is the surface being gated, and the pin performs an older one.
# Objects only: nothing links and nothing runs, so a link-time or runtime
# defect on the other OS is still `verify-freestanding` / CI's to find.
# `cryogate cross`; ARGS passes --triple, --project, --min-modules through.
cross-check: $(STAGE2_FILE) $(CRYOGATE)
	@$(CRYOGATE_COMMAND) cross --cryo "$(STAGE2_FILE)" $(ARGS)

# ---- stdlib API index --------------------------------------------------
# Regenerate docs/stdlib-api.txt, the one-file answer to "does this already
# exist?".  `cryo api-index` reads every stdlib/**/*.cryo with the compiler
# under test's own lexer and parser, so the index lists what the parser
# reads as a declaration, written as the source writes it.
#
# `api-index-check` is the CI half: a hand-edited or stale index is worse than
# no index, because it gets trusted.
ifeq ($(HOST_OS),windows)
api-index api-index-check: $(STAGE2_EXE)
	@"$(STAGE2_EXE)" api-index $(if $(filter api-index-check,$@),check,write) stdlib docs/stdlib-api.txt
else
api-index api-index-check: $(STAGE2)
	@"$(STAGE2)" api-index $(if $(filter api-index-check,$@),check,write) stdlib docs/stdlib-api.txt
endif

# ---- documentation coverage -------------------------------------------
# docs/doc-coverage.txt is the standard library's public declarations with
# no documentation, exactly: `doc-coverage-check` fails on an undocumented
# one it does not list (document it) and on a listed one that is documented
# or gone (`make doc-coverage-update`).  tools/CryoDocCoverage parses with
# the compiler library, so it is built with the compiler under test, into
# build-logs/doc-coverage; it first proves over its own fixture that the
# comparison passes a matching golden and refuses one a name short or a
# name over, then checks the stdlib.
ifeq ($(HOST_OS),windows)
DOC_COVERAGE := $(ROOT)/build-logs/doc-coverage/cryo-doc-coverage.exe
doc-coverage-check doc-coverage-update: $(STAGE2_EXE)
	@cd tools/CryoDocCoverage && "$(STAGE2_EXE)" build --stdlib=$(ROOT)/stdlib --build-dir=$(ROOT)/build-logs/doc-coverage
else
DOC_COVERAGE := $(ROOT)/build-logs/doc-coverage/cryo-doc-coverage
doc-coverage-check doc-coverage-update: $(STAGE2)
	@cd tools/CryoDocCoverage && "$(STAGE2)" build --stdlib=$(ROOT)/stdlib --build-dir=$(ROOT)/build-logs/doc-coverage
endif
	@"$(DOC_COVERAGE)" self-test tools/CryoDocCoverage/tests/fixture tools/CryoDocCoverage/tests/fixture-golden.txt
	@"$(DOC_COVERAGE)" $(if $(filter doc-coverage-update,$@),update,check) stdlib docs/doc-coverage.txt

# ---- examples smoke build ---------------------------------------------
# Compile every examples/*/ project with the freshly-built stage-2 compiler
# so a stdlib/compiler change that breaks a shipped "getting started" example
# fails CI instead of reaching users.  Build-only (no run); the per-example
# build/ dirs are gitignored.  `cryogate examples` pins CRYO_STDLIB to the
# in-tree stdlib so resolution is independent of which compiler binary builds
# the examples.  A gate that exits 0 having done nothing is counted as
# evidence, so this one refuses to report success unless it swept at least
# the shipped set (its floor; raise it when an example is added).
examples: $(STAGE2_FILE) $(CRYOGATE)
	@$(CRYOGATE_COMMAND) examples --cryo "$(STAGE2_FILE)"

# ---- examples golden-output check -------------------------------------
# Build every example, then run each one holding a committed golden
# (examples/<name>/expected.out) with no input and compare its output with
# it.  Catches runtime regressions that the build-only `examples` target
# misses.  Heavy: it runs under the heavy-run lock.
examples-golden: $(STAGE2_FILE) $(CRYOGATE)
	@$(CRYOGATE_HEAVY) $(CRYOGATE_COMMAND) examples --run --cryo "$(STAGE2_FILE)"

# ---- runtime memory-safety gate (valgrind) ----------------------------
# Build every example, then run each one holding an expected.out under
# valgrind, failing on any invalid free/read/write or definite leak, or on
# output that differs from its golden.  The static move-checker rejects
# use-after-move at compile time; this exercises the generated drop/free paths
# at runtime, so a miscompile that double-freed or leaked an owned value is
# caught here.  Linux-only and requires valgrind on PATH; a Windows host
# refuses and builds nothing first.  Heavy: it runs under the heavy-run lock.
ifeq ($(HOST_OS),windows)
valgrind-check:
	$(error valgrind-check: UNAVAILABLE on this host -- valgrind is POSIX/Linux only; run it from WSL)
else
valgrind-check: $(STAGE2_FILE) $(CRYOGATE)
	@$(CRYOGATE_HEAVY) $(CRYOGATE_COMMAND) examples --run --under=valgrind --cryo "$(STAGE2_FILE)"
endif

# ---- the runtime -------------------------------------------------------
# Every program links the runtime (`runtime/`, one project), and the compiler
# that links it builds it, on first use, under runtime/.bin/<triple>/; see
# compiler/src/compiler/prebuilt_runtime.cryo.  Nothing here builds it - not
# for the pin, and not for the compiler under test.

# The lang-item registry (runtime/abi/lang_items.def) against the runtime the
# compiler under test builds in each configuration, and the `__cryo_` names
# compiler/src and stdlib/ spell; `cryogate abi`, whose gate file's header
# says what it refuses.
runtime-abi-check: $(STAGE2_FILE) $(CRYOGATE)
	@$(CRYOGATE_COMMAND) abi --cryo "$(STAGE2_FILE)"

# ---- freestanding runtime gate ----------------------------------------
# Build the runtime's freestanding configuration and run its acceptance check
# on both supported OSes.  No hosted program reaches the freestanding entry,
# allocator or backtrace, so nothing else exercises them.
#
# A skipped arm FAILS.  The Windows half needs mingw + wine; where those are
# absent, accept running half of the gate deliberately with
# `ARGS=--allow-skipped-arm` (the spelling selfhost-check uses), and the run
# names the arm it did not run; `FREESTANDING_LINUX_ONLY=1` selects the skip.
#
# The Linux arm links ELF objects natively, so a Windows host runs the whole
# check inside WSL, from the digest-checked copy of the tree selfhost-check
# uses, with the tree's compiler built there by the copy's pin (`--cryo` is
# not used there).  `cryogate freestanding`.
ifeq ($(HOST_OS),windows)
verify-freestanding: $(CRYOGATE)
	@$(CRYOGATE_COMMAND) freestanding --cryo "$(STAGE2_FILE)" $(ARGS)
else
verify-freestanding: cryo $(CRYOGATE)
	@$(CRYOGATE_COMMAND) freestanding --cryo "$(STAGE2)" $(ARGS)
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

# A release archive of this host's OS put in place by this checkout's
# installer and used, upgraded and removed in a scratch home (`cryogate
# install-check`).  ARCHIVE names it: scripts/build-release.sh builds one
# (`linux --dynamic` on a host without the static LLVM libraries; Git Bash
# runs it on Windows).  Run by hand; it installs nothing outside the scratch
# home, and a host with no C toolchain fails it at the install.
install-check: $(CRYOGATE)
ifeq ($(ARCHIVE),)
	@echo "usage: make install-check ARCHIVE=dist/cryo-<version>-<os>-x86_64.<zip|tar.gz>"
	@exit 2
else
	@$(CRYOGATE_COMMAND) install-check --archive "$(ARCHIVE)" $(ARGS)
endif

# ---- dev install via symlink (the repo-local toolchain) ----------------
# `make install` is the DEV install: symlink the committed bin/cryo +
# stdlib into a prefix.  End users instead run the production downloader:
#   curl -fsSL https://cryolang.org/install.sh | bash
install:
	@./install.sh --dev

uninstall:
	@./install.sh --dev --uninstall

# ---- clean ------------------------------------------------------------
# Every output the targets above write inside the tree, for every triple:
# the compiler, the stdlib and runtime archives, the LSP build and its
# installed copy, the test helper archives, the test suite's and the
# examples' build directories, the namespace-index caches the compile-fail
# suite's single-file checks keep in tests/tests/negative/.bin, the gates' state (.verify/ with its cached
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
	@if exist tools\CryoGate\build rmdir /s /q tools\CryoGate\build
	@if exist tools\CryoSmith\build rmdir /s /q tools\CryoSmith\build
	@if exist bin\cryolsp.exe del /q bin\cryolsp.exe
	@if exist bin\cryolsp del /q bin\cryolsp
	@del /q tests\helpers\*.o tests\helpers\*.a 2>NUL || rem
	@for %%d in (tests\build tests\tests\negative\.bin .verify .cross-check build-logs dist) do @if exist %%d rmdir /s /q %%d
	@for /d %%p in (examples\* tests\tests\projects\*) do @if exist %%p\build rmdir /s /q %%p\build
	@for %%d in (compiler\build stdlib\.bin runtime\.bin tools\CryoLSP\build tools\CryoGate\build tools\CryoSmith\build bin\cryolsp.exe) do @if exist %%d (echo clean: %%d is still there; a running process holds a file in it & exit /b 1)
else
clean:
	@echo "==> Cleaning build outputs, test and example builds, and gate state"
	@rm -rf compiler/build stdlib/.bin runtime/.bin
	@rm -rf tools/CryoLSP/build tools/CryoGate/build tools/CryoSmith/build
	@rm -f bin/cryolsp bin/cryolsp.exe
	@rm -f $(TEST_HELPERS_DIR)/*.o $(TEST_HELPERS_DIR)/*.a
	@rm -rf tests/build tests/tests/negative/.bin .verify .cross-check build-logs dist
	@rm -rf examples/*/build tests/tests/projects/*/build
endif
