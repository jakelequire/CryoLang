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
STAGE2 := $(ROOT)/compiler/build/cryo
STAGE3 := $(ROOT)/compiler/build/bin/cryo
STAGE4 := $(ROOT)/compiler/build-s4/bin/cryo
STAGE5 := $(ROOT)/compiler/build-s5/bin/cryo

NPROC := $(shell nproc 2>/dev/null || echo 4)

.DEFAULT_GOAL := help
.PHONY: help bootstrap stdlib cryo selfhost-check clean distclean

help:
	@echo "Cryo build targets:"
	@echo "  make bootstrap        Build the C++ bootstrap compiler"
	@echo "  make stdlib           Build the standard library via bootstrap"
	@echo "  make cryo             Build the self-hosted Cryo compiler"
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

# ---- self-hosted cryo via bootstrap -----------------------------------
cryo: stdlib
	@echo "==> Building self-hosted cryo via bootstrap (stage-2)"
	@cd compiler && "$(BOOT)" build
	@echo "==> Bootstrapping to stage-3"
	@rm -rf compiler/build/obj compiler/build/bin
	@cd compiler && "$(STAGE2)" build
	@echo "==> Self-hosted cryo built: $(STAGE3)"

# ---- full selfhost-check + byte-identity diff -------------------------
selfhost-check: $(BOOT)
	@echo "==> Wiping all stage outputs"
	@rm -rf compiler/build compiler/build-s4 compiler/build-s5
	@rm -rf stdlib/.bin stdlib/.bin-s2 stdlib/.bin-s3 stdlib/.bin-s4
	@echo "==> [1/8] stdlib via bootstrap"
	@mkdir -p stdlib/.bin/obj
	@cd stdlib && "$(BOOT)" build
	@echo "==> [2/8] compiler via bootstrap -> stage-2 ($(STAGE2))"
	@cd compiler && "$(BOOT)" build
	@echo "==> [3/8] stdlib via stage-2 -> stdlib/.bin-s2"
	@mkdir -p stdlib/.bin-s2/obj
	@cd stdlib && "$(STAGE2)" build --build-dir=.bin-s2
	@echo "==> [4/8] compiler via stage-2 -> stage-3 ($(STAGE3))"
	@rm -rf compiler/build/obj compiler/build/bin
	@cd compiler && "$(STAGE2)" build
	@echo "==> [5/8] stdlib via stage-3 -> stdlib/.bin-s3"
	@mkdir -p stdlib/.bin-s3/obj
	@cd stdlib && "$(STAGE3)" build --build-dir=.bin-s3
	@echo "==> [6/8] cryo via stage-3 -> stage-4 ($(STAGE4))"
	@cd compiler && "$(STAGE3)" build --build-dir=build-s4
	@echo "==> [7/8] stdlib via stage-4 -> stdlib/.bin-s4"
	@mkdir -p stdlib/.bin-s4/obj
	@cd stdlib && "$(STAGE4)" build --build-dir=.bin-s4
	@echo "==> [8/8] cryo via stage-4 -> stage-5 ($(STAGE5))"
	@cd compiler && "$(STAGE4)" build --build-dir=build-s5
	@echo "==> Verifying stage-4 == stage-5 IR byte identity"
	@if diff -q compiler/build-s4/bin/cryo.ll compiler/build-s5/bin/cryo.ll > /dev/null; then \
		echo ""; \
		echo "FIXED POINT OK: stage-4 and stage-5 produce byte-identical IR"; \
	else \
		echo ""; \
		echo "FIXED POINT BROKEN: stage-4 and stage-5 IR differ"; \
		diff compiler/build-s4/bin/cryo.ll compiler/build-s5/bin/cryo.ll | head -40; \
		exit 1; \
	fi

# ---- clean -------------------------------------------------------------
clean:
	@echo "==> Cleaning compiler and stdlib build outputs"
	@rm -rf compiler/build compiler/build-s4 compiler/build-s5
	@rm -rf stdlib/.bin stdlib/.bin-s2 stdlib/.bin-s3 stdlib/.bin-s4

distclean: clean
	@echo "==> Cleaning bootstrap"
	@$(MAKE) -C legacy/bootstrap clean
