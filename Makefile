# Cryo top-level build orchestration.
#
# The compiler is self-hosted.  The pinned binary at bin/cryo bootstraps
# every other target — no C++ compiler required for day-to-day work.
# The legacy C++ bootstrap at legacy/bootstrap/ is no longer on the
# default path; it stays on disk as archaeology and is still buildable
# via `make legacy-bootstrap` if you want to compare or rebootstrap from
# scratch.
#
# Targets:
#   stdlib           Build the standard library via bin/cryo
#   cryo             Build the self-hosted compiler via bin/cryo
#   selfhost-check   3-round chain (6 stages) + stage-3/stage-4 byte-identity
#   pin-cryo         Refresh bin/cryo from compiler/build/bin/cryo
#   install          Symlink bin/cryo + stdlib system-wide (delegates to install.sh)
#   uninstall        Remove the install.sh symlinks
#   clean            Remove compiler + stdlib build outputs
#   distclean        Also clean the legacy bootstrap
#   legacy-bootstrap Build the C++ bootstrap (archaeology / from-scratch)

ROOT     := $(CURDIR)
PIN      := $(ROOT)/bin/cryo
STAGE2   := $(ROOT)/compiler/build/bin/cryo

# Legacy: still buildable for archaeology, but not on the default path.
LEGACY_BOOT := $(ROOT)/legacy/bootstrap/bin/cryo

NPROC := $(shell nproc 2>/dev/null || echo 4)

LSP_BUILD_DIR := $(ROOT)/tools/CryoLSP/build
LSP_BIN       := $(LSP_BUILD_DIR)/bin/cryolsp
LSP_PIN       := $(ROOT)/bin/cryolsp

.DEFAULT_GOAL := help
.PHONY: help stdlib cryo selfhost-check test test-list pin-cryo install uninstall \
        clean distclean legacy-bootstrap lsp

help:
	@echo "Cryo build targets:"
	@echo "  make stdlib            Build the standard library via bin/cryo"
	@echo "  make cryo              Build the self-hosted compiler via bin/cryo"
	@echo "  make lsp               Build the Cryo-language LSP server (bin/cryolsp)"
	@echo "  make selfhost-check    3-round chain (6 stages) + byte-identity gate"
	@echo "  make test              Run the repo-level test suite (tests/) via cryo test"
	@echo "  make test-list         List the discovered test cases without running them"
	@echo "  make pin-cryo          Refresh bin/cryo from compiler/build/bin/cryo"
	@echo "  make install           Symlink bin/cryo + stdlib system-wide (sudo)"
	@echo "  make uninstall         Remove the install.sh symlinks"
	@echo "  make clean             Remove compiler + stdlib build outputs"
	@echo "  make distclean         Also clean legacy bootstrap"
	@echo
	@echo "Archaeology:"
	@echo "  make legacy-bootstrap  Build the C++ bootstrap (legacy/bootstrap/bin/cryo)"

# ---- guard: pin must exist --------------------------------------------
$(PIN):
	@echo "ERROR: $(PIN) does not exist."
	@echo "       The committed pin is missing.  Either check out a revision"
	@echo "       that has bin/cryo committed, or rebootstrap with"
	@echo "       'make legacy-bootstrap && legacy/bootstrap/bin/cryo build'"
	@echo "       in compiler/, then 'make pin-cryo'."
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

# ---- pin refresh ------------------------------------------------------
# After running 'make cryo', commit the new bin/cryo + bin/cryo.pin.txt
# so a fresh clone can reproduce this state without re-bootstrapping.
pin-cryo:
	@python3 scripts/cryo-pin.py --source "$(STAGE2)" --pin "$(PIN)"

# ---- Cryo-language LSP server -----------------------------------------
# Builds tools/CryoLSP/ (entirely Cryo source) into bin/cryolsp.
# Depends on `cryo` because the LSP imports the self-hosted compiler
# library + stdlib via include_paths.
lsp: cryo
	@echo "==> Building CryoLSP via bin/cryo"
	@cd tools/CryoLSP && "$(PIN)" build
	@cp "$(LSP_BIN)" "$(LSP_PIN)"
	@echo "==> bin/cryolsp ready"

# ---- selfhost byte-identity check -------------------------------------
# Implementation lives in scripts/selfhost-check.py — that gives us
# per-stage progress + timings, per-stage logs in build-logs/, and a
# tail-on-failure dump.  Run the script directly with --verbose for
# streaming subprocess output.
selfhost-check: $(PIN)
	@python3 scripts/selfhost-check.py

# ---- test suite -------------------------------------------------------
# Builds the stage-2 compiler if needed, then drives `cryo test` against
# the tests/ project.  See tests/cryoconfig and docs/testing.md for the
# project layout and the framework surface (`![test]`, `![ignore]`,
# `![should_panic]`).  Pass arguments through with `make test ARGS=...`
# (e.g. `make test ARGS="--ignored some_filter"`).
test: cryo
	@cd tests && "$(STAGE2)" test $(ARGS)

test-list: cryo
	@cd tests && "$(STAGE2)" test --list $(ARGS)

# ---- system install via symlink ---------------------------------------
install:
	@./install.sh

uninstall:
	@./install.sh --uninstall

# ---- legacy bootstrap (archaeology / from-scratch rebootstrap) --------
legacy-bootstrap: $(LEGACY_BOOT)
$(LEGACY_BOOT):
	@echo "==> Building legacy C++ bootstrap"
	@$(MAKE) -C legacy/bootstrap compiler -j$(NPROC)

# ---- clean ------------------------------------------------------------
clean:
	@echo "==> Cleaning compiler + stdlib build outputs"
	@rm -rf compiler/build stdlib/.bin
	@rm -rf tools/CryoLSP/build
	@rm -f bin/cryolsp

distclean: clean
	@echo "==> Cleaning legacy bootstrap"
	@$(MAKE) -C legacy/bootstrap clean
