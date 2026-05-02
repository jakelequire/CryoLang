# Cryo top-level build orchestration.
#
# Targets:
#   bootstrap        Build the C++ bootstrap compiler  (legacy/bootstrap/bin/cryo)
#   stdlib           Build the standard library via bootstrap  (stdlib/.bin/)
#   cryo             Build the self-hosted Cryo compiler via bootstrap
#                    (compiler/build/bin/cryo)
#   selfhost-check   Full chain: bootstrap -> stage-2 -> stage-3 -> stage-4 ->
#                    stage-5, then verify stage-4 and stage-5 IR are
#                    byte-identical.
#   clean            Remove compiler + stdlib build outputs (bootstrap kept).
#   distclean        Also clean bootstrap.
#   help             List targets.
#
# Notes:
#   * stdlib/ is the canonical standard library. experimental/stdlib-next/ is
#     parked for a future migration and is intentionally not built here.
#   * The bootstrap binary lives at legacy/bootstrap/bin/cryo; the self-hosted
#     binary lives at compiler/build/bin/cryo. Disambiguated by path, not name.
#   * The fixed point is at stage-4, not stage-3. Bootstrap (C++) emits stage-2
#     with two harmless codegen quirks (dead `@FILE.str` globals in
#     Compiler__CompileMode, and an unmangled `@panic` call in std__prelude)
#     that bake into stage-3's IR but don't affect runtime. Stage-3's behavior
#     is clean, so stage-4 is the first IR-level fixed point. We build stage-5
#     to confirm it.

ROOT   := $(CURDIR)
BOOT   := $(ROOT)/legacy/bootstrap/bin/cryo
PIN    := $(ROOT)/bin/cryo
STAGE2 := $(ROOT)/compiler/build/cryo
STAGE3 := $(ROOT)/compiler/build/bin/cryo
STAGE4 := $(ROOT)/compiler/build-s4/bin/cryo
STAGE5 := $(ROOT)/compiler/build-s5/bin/cryo

NPROC := $(shell nproc 2>/dev/null || echo 4)

.DEFAULT_GOAL := help
.PHONY: help bootstrap stdlib cryo cryo-fast stdlib-fast pin-cryo selfhost-check clean distclean

help:
	@echo "Cryo build targets:"
	@echo "  make bootstrap        Build the C++ bootstrap compiler"
	@echo "  make stdlib           Build the standard library via bootstrap"
	@echo "  make cryo             Build the self-hosted Cryo compiler (canonical)"
	@echo "  make cryo-fast        Build via the pinned bin/cryo (fast dev loop)"
	@echo "  make pin-cryo         Refresh bin/cryo from compiler/build/bin/cryo"
	@echo "  make selfhost-check   Full chain + stage-4/stage-5 byte-identity check"
	@echo "  make clean            Remove compiler + stdlib build outputs"
	@echo "  make distclean        Also clean bootstrap"

# ---- bootstrap (C++) ---------------------------------------------------
bootstrap: $(BOOT)
$(BOOT):
	@echo "==> Building C++ bootstrap compiler"
	@$(MAKE) -C legacy/bootstrap compiler -j$(NPROC)

# ---- stdlib via bootstrap ---------------------------------------------
stdlib: $(BOOT)
	@echo "==> Building stdlib via bootstrap"
	@rm -rf stdlib/.bin && mkdir -p stdlib/.bin/obj
	@cd stdlib && "$(BOOT)" build

# ---- new stdlib via pinned bin/cryo (fast dev loop) -------------------
stdlib-next: $(PIN)
	@echo "==> Building new stdlib via pinned bin/cryo"
	@rm -rf experimental/stdlib-next/.bin && mkdir -p experimental/stdlib-next/.bin/obj
	@cd experimental/stdlib-next && "$(PIN)" build

# ---- self-hosted cryo via bootstrap -----------------------------------
cryo: stdlib
	@echo "==> Building self-hosted cryo via bootstrap (stage-2)"
	@cd compiler && "$(BOOT)" build
	@echo "==> Bootstrapping to stage-3"
	@rm -rf compiler/build/obj compiler/build/bin
	@cd compiler && "$(STAGE2)" build
	@echo "==> Self-hosted cryo built: $(STAGE3)"

# ---- fast dev loop via the pinned binary at bin/cryo ------------------
# The pinned binary is built from a known-good cryoc and committed to the
# repo. As long as compiler/src/ stays in a dialect bin/cryo can parse, we
# can skip the slow bootstrap rung entirely.
#
# When compiler/src/ adopts new syntax that bin/cryo can no longer parse,
# refresh the pin: run `make cryo` (canonical path through bootstrap), then
# `make pin-cryo` to update bin/cryo, then commit. See CONTRIBUTING.md.
stdlib-fast:
	@if [ ! -x "$(PIN)" ]; then \
		echo "ERROR: $(PIN) does not exist. Run 'make cryo && make pin-cryo' first."; \
		exit 1; \
	fi
	@echo "==> Building stdlib via pinned bin/cryo"
	@rm -rf stdlib/.bin && mkdir -p stdlib/.bin/obj
	@cd stdlib && "$(PIN)" build

cryo-fast: stdlib-fast
	@echo "==> Building self-hosted cryo via pinned bin/cryo (stage-2)"
	@cd compiler && "$(PIN)" build
	@echo "==> Bootstrapping to stage-3"
	@rm -rf compiler/build/obj compiler/build/bin
	@cd compiler && "$(STAGE2)" build
	@echo "==> Self-hosted cryo built: $(STAGE3)"

pin-cryo:
	@python3 scripts/cryo-pin.py --source "$(STAGE3)" --pin "$(PIN)"

# ---- full selfhost-check + byte-identity diff -------------------------
# Implementation lives in scripts/selfhost-check.py — that gives us
# per-stage progress + timings, per-stage logs in build-logs/, and a
# tail-on-failure dump. Run the script directly with --verbose for
# streaming subprocess output.
selfhost-check: $(BOOT)
	@python3 scripts/selfhost-check.py

# ---- clean -------------------------------------------------------------
clean:
	@echo "==> Cleaning compiler and stdlib build outputs"
	@rm -rf compiler/build compiler/build-s4 compiler/build-s5
	@rm -rf stdlib/.bin stdlib/.bin-s2 stdlib/.bin-s3 stdlib/.bin-s4

distclean: clean
	@echo "==> Cleaning bootstrap"
	@$(MAKE) -C legacy/bootstrap clean
