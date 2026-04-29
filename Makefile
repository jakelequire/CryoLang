# Cryo top-level build orchestration.
#
# Targets:
#   bootstrap        Build the C++ bootstrap compiler  (bootstrap/bin/cryo)
#   stdlib           Build the standard library via bootstrap  (stdlib/.bin/)
#   cryo             Build the self-hosted Cryo compiler via bootstrap
#                    (cryoc/build/bin/cryo)
#   selfhost-check   Full chain: bootstrap -> stage-2 -> stage-3 -> stage-4,
#                    then verify stage-3 and stage-4 IR are byte-identical.
#   clean            Remove cryoc + stdlib build outputs (bootstrap kept).
#   distclean        Also clean bootstrap.
#   help             List targets.
#
# Notes:
#   * stdlib/ is the canonical standard library. new_stdlib/ is parked for a
#     future migration and is intentionally not built here.
#   * The bootstrap binary lives at bootstrap/bin/cryo; the self-hosted binary
#     lives at cryoc/build/bin/cryo. Disambiguated by path, not name.

ROOT   := $(CURDIR)
BOOT   := $(ROOT)/bootstrap/bin/cryo
STAGE2 := $(ROOT)/cryoc/build/cryo
STAGE3 := $(ROOT)/cryoc/build/bin/cryo
STAGE4 := $(ROOT)/cryoc/build-s4/bin/cryo

NPROC := $(shell nproc 2>/dev/null || echo 4)

.DEFAULT_GOAL := help
.PHONY: help bootstrap stdlib cryo selfhost-check clean distclean

help:
	@echo "Cryo build targets:"
	@echo "  make bootstrap        Build the C++ bootstrap compiler"
	@echo "  make stdlib           Build the standard library via bootstrap"
	@echo "  make cryo             Build the self-hosted Cryo compiler"
	@echo "  make selfhost-check   Full chain + stage-3/stage-4 byte-identity check"
	@echo "  make clean            Remove cryoc + stdlib build outputs"
	@echo "  make distclean        Also clean bootstrap"

# ---- bootstrap (C++) ---------------------------------------------------
bootstrap: $(BOOT)
$(BOOT):
	@echo "==> Building C++ bootstrap compiler"
	@$(MAKE) -C bootstrap compiler -j$(NPROC)

# ---- stdlib via bootstrap ---------------------------------------------
stdlib: $(BOOT)
	@echo "==> Building stdlib via bootstrap"
	@rm -rf stdlib/.bin && mkdir -p stdlib/.bin/obj
	@cd stdlib && "$(BOOT)" build

# ---- self-hosted cryo via bootstrap -----------------------------------
cryo: stdlib
	@echo "==> Building self-hosted cryo via bootstrap (stage-2)"
	@cd cryoc && "$(BOOT)" build
	@echo "==> Bootstrapping to stage-3"
	@rm -rf cryoc/build/obj cryoc/build/bin
	@cd cryoc && "$(STAGE2)" build
	@echo "==> Self-hosted cryo built: $(STAGE3)"

# ---- full selfhost-check + byte-identity diff -------------------------
selfhost-check: $(BOOT)
	@echo "==> Wiping all stage outputs"
	@rm -rf cryoc/build cryoc/build-s4
	@rm -rf stdlib/.bin stdlib/.bin-s2 stdlib/.bin-s3
	@echo "==> [1/6] stdlib via bootstrap"
	@mkdir -p stdlib/.bin/obj
	@cd stdlib && "$(BOOT)" build
	@echo "==> [2/6] cryoc via bootstrap -> stage-2 ($(STAGE2))"
	@cd cryoc && "$(BOOT)" build
	@echo "==> [3/6] stdlib via stage-2 -> stdlib/.bin-s2"
	@mkdir -p stdlib/.bin-s2/obj
	@cd stdlib && "$(STAGE2)" build --build-dir=.bin-s2
	@echo "==> [4/6] cryoc via stage-2 -> stage-3 ($(STAGE3))"
	@rm -rf cryoc/build/obj cryoc/build/bin
	@cd cryoc && "$(STAGE2)" build
	@echo "==> [5/6] stdlib via stage-3 -> stdlib/.bin-s3"
	@mkdir -p stdlib/.bin-s3/obj
	@cd stdlib && "$(STAGE3)" build --build-dir=.bin-s3
	@echo "==> [6/6] cryo via stage-3 -> stage-4 ($(STAGE4))"
	@cd cryoc && "$(STAGE3)" build --build-dir=build-s4
	@echo "==> Verifying stage-3 == stage-4 IR byte identity"
	@if diff -q cryoc/build/bin/cryo.ll cryoc/build-s4/bin/cryo.ll > /dev/null; then \
		echo ""; \
		echo "FIXED POINT OK: stage-3 and stage-4 produce byte-identical IR"; \
	else \
		echo ""; \
		echo "FIXED POINT BROKEN: stage-3 and stage-4 IR differ"; \
		diff cryoc/build/bin/cryo.ll cryoc/build-s4/bin/cryo.ll | head -40; \
		exit 1; \
	fi

# ---- clean -------------------------------------------------------------
clean:
	@echo "==> Cleaning cryoc and stdlib build outputs"
	@rm -rf cryoc/build cryoc/build-s4
	@rm -rf stdlib/.bin stdlib/.bin-s2 stdlib/.bin-s3

distclean: clean
	@echo "==> Cleaning bootstrap"
	@$(MAKE) -C bootstrap clean
