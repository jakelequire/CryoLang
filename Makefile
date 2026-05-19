# Cryo top-level build orchestration.
#
# The compiler is self-hosted.  The pinned binary at bin/cryo bootstraps
# every other target — every build flows through it.
#
# Targets:
#   stdlib           Build the standard library via bin/cryo
#   cryo             Build the self-hosted compiler via bin/cryo
#   selfhost-check   3-round chain (6 stages) + stage-3/stage-4 byte-identity
#   pin-cryo         Refresh bin/cryo from compiler/build/bin/cryo
#   install          Symlink bin/cryo + stdlib system-wide (delegates to install.sh)
#   uninstall        Remove the install.sh symlinks
#   clean            Remove compiler + stdlib build outputs

ROOT       := $(CURDIR)
PIN        := $(ROOT)/bin/cryo
STAGE2     := $(ROOT)/compiler/build/bin/cryo
LIBCRYO_A  := $(ROOT)/stdlib/.bin/libcryo.a

# C-side helpers for the ABI tests.  Compiled with the host cc to a
# static archive that `tests/cryoconfig` links via -L./helpers
# -labihelpers — see tests/helpers/abi_helpers.c for the contract.
TEST_HELPERS_DIR := $(ROOT)/tests/helpers
TEST_HELPERS_C   := $(TEST_HELPERS_DIR)/abi_helpers.c
TEST_HELPERS_O   := $(TEST_HELPERS_DIR)/abi_helpers.o
TEST_HELPERS_A   := $(TEST_HELPERS_DIR)/libabihelpers.a

NPROC := $(shell nproc 2>/dev/null || echo 4)

LSP_BUILD_DIR := $(ROOT)/tools/CryoLSP/build
LSP_BIN       := $(LSP_BUILD_DIR)/bin/cryolsp
LSP_PIN       := $(ROOT)/bin/cryolsp

EXT_DIR       := $(ROOT)/tools/CryoAnalyzer
EXT_ID        := cryolang.cryo-analyzer
EXT_VSIX      := $(EXT_DIR)/cryo-analyzer.vsix

.DEFAULT_GOAL := help
.PHONY: help stdlib cryo selfhost-check test test-list pin-cryo install uninstall \
        clean lsp install-lsp

help:
	@echo "Cryo build targets:"
	@echo "  make stdlib            Build the standard library via bin/cryo"
	@echo "  make cryo              Build the self-hosted compiler via bin/cryo"
	@echo "  make lsp               Build the Cryo-language LSP server (bin/cryolsp)"
	@echo "  make install-lsp       Package + install the CryoAnalyzer VS Code extension"
	@echo "  make selfhost-check    3-round chain (6 stages) + byte-identity gate"
	@echo "  make test              Run the repo-level test suite (tests/) via cryo test"
	@echo "  make test-list         List the discovered test cases without running them"
	@echo "  make pin-cryo          Refresh bin/cryo from compiler/build/bin/cryo"
	@echo "  make install           Symlink bin/cryo + stdlib system-wide (sudo)"
	@echo "  make uninstall         Remove the install.sh symlinks"
	@echo "  make clean             Remove compiler + stdlib build outputs"

# ---- guard: pin must exist --------------------------------------------
$(PIN):
	@echo "ERROR: $(PIN) does not exist."
	@echo "       Every build target drives off the committed pin."
	@echo "       Check out a revision that has bin/cryo committed."
	@exit 1

# ---- stdlib via the pinned self-hosted compiler -----------------------
stdlib: $(PIN)
	@echo "==> Building stdlib via bin/cryo"
	@rm -rf stdlib/.bin && mkdir -p stdlib/.bin/obj
	@cd stdlib && "$(PIN)" build

# ---- self-hosted compiler via the pin ---------------------------------
cryo: stdlib
	@echo "==> Building self-hosted cryo via bin/cryo"
	@cd compiler && "$(PIN)" build
	@echo "==> Self-hosted cryo built: $(STAGE2)"

# File-target rule so downstream targets (test, lsp) can depend on the
# binary itself instead of the phony `cryo` target. If the binary is
# present, Make treats it as up-to-date and skips the rebuild.
$(STAGE2):
	@$(MAKE) --no-print-directory cryo

# File-target rule for the stdlib static library so `test` rebuilds it
# when stdlib/.bin has been wiped (e.g. by selfhost-check) but the
# compiler binary still exists. Without this, `make selfhost-check &&
# make test` fails at link with "cannot find libcryo.a".
$(LIBCRYO_A):
	@$(MAKE) --no-print-directory stdlib

# ---- pin refresh ------------------------------------------------------
# After running 'make cryo', commit the new bin/cryo + bin/cryo.pin.txt
# so a fresh clone can reproduce this state.
pin-cryo:
	@python3 scripts/cryo-pin.py --source "$(STAGE2)" --pin "$(PIN)"

# ---- Cryo-language LSP server -----------------------------------------
# Builds tools/CryoLSP/ (entirely Cryo source) into bin/cryolsp.
# Depends on the stage-2 binary as a file so an existing compiler build
# is reused. Run `make cryo` first if you want to pick up compiler changes.
lsp: $(STAGE2)
	@echo "==> Building CryoLSP via bin/cryo"
	@cd tools/CryoLSP && "$(PIN)" build
	@cp "$(LSP_BIN)" "$(LSP_PIN)"
	@echo "==> bin/cryolsp ready"

# ---- CryoAnalyzer VS Code extension -----------------------------------
# Packages tools/CryoAnalyzer/ into a .vsix and installs it into VS Code,
# evicting any previously installed copy and any stale .vsix artifacts so
# the install always reflects the current source.
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

# ---- selfhost byte-identity check -------------------------------------
# Implementation lives in scripts/selfhost-check.py — that gives us
# per-stage progress + timings, per-stage logs in build-logs/, and a
# tail-on-failure dump.  Run the script directly with --verbose for
# streaming subprocess output.
selfhost-check: $(PIN)
	@python3 scripts/selfhost-check.py

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

test: $(STAGE2) $(LIBCRYO_A) $(TEST_HELPERS_A)
	@cd tests && "$(STAGE2)" test $(ARGS)

test-list: $(STAGE2) $(LIBCRYO_A) $(TEST_HELPERS_A)
	@cd tests && "$(STAGE2)" test --list $(ARGS)

# ---- system install via symlink ---------------------------------------
install:
	@./install.sh

uninstall:
	@./install.sh --uninstall

# ---- clean ------------------------------------------------------------
clean:
	@echo "==> Cleaning compiler + stdlib build outputs"
	@rm -rf compiler/build stdlib/.bin
	@rm -rf tools/CryoLSP/build
	@rm -f bin/cryolsp
	@rm -f $(TEST_HELPERS_O) $(TEST_HELPERS_A)
