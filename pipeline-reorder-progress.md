# Pipeline Reorder — Progress Log

**Mission:** Move Monomorphization to run *after* Semantic Analysis. Sema becomes
the single source of truth for resolved types; mono's private inference layer is
deleted. See the handoff for the full plan (Phases 0–7).

## Validation commands (this Windows host)
- Build: run from **PowerShell** so `make` uses `cmd` recipe shell —
  `$env:CRYO_CC='gcc'; make cryo` then `make test`.
  (Git Bash's `make` picks the Windows/cmd recipe branch but runs it under `sh`
  → `syntax error: unexpected end of file`. Use PowerShell for `make`.)
- Self-host: WSL — `wsl bash -lc "cd /mnt/c/Programming/apps/CryoLang &&
  export CRYO_CC=gcc && python3 scripts/selfhost-check.py --no-windows"`.
  (Windows host routes selfhost through WSL automatically; calling it in WSL
  directly skips the re-entry.)
- Run build/test/selfhost **serially** — shared .o cache corrupts on concurrent runs.

## Phase 0 — Baseline (COMPLETE, 2026-06-16)
- `make cryo`: green. `make test`: unit ok, compile-fail **99 passed**.
- `selfhost-check --no-windows`: **FIXED POINT OK**.
  - Baseline IR md5: `4ef7c5bfa5fbe8f2bee0cb9792750746`  (size 48,963,800 bytes)
- Guard corpus: `tests/` + `stdlib` + `examples/` already exercise collections,
  iterators/adapters, nested + cross-module generics, trait bounds. Added focused
  guards under `/tmp/reorder-guards/` (not committed) — see Phase 1 notes.

## Phase 1 — Extract TraitChecker, sever sema→mono dependency (COMPLETE, 2026-06-16)
**Validated green + byte-identical fixed point.**
- New self-host IR md5: `edd51eee51bfd94536469f555892c57c` (size 48,973,596).
  (Differs from the Phase-0 baseline md5 by design — the compiler source gained a
  file/struct; the new compiler is its own byte-identical fixed point, which is
  the invariant that matters, not equality with the old bytes.)
- `make cryo` green; `make test` unit ok + compile-fail **99**; `selfhost-check
  --no-windows` **FIXED POINT OK**.

What landed:
- New file `compiler/src/compiler/types/trait_checker.cryo` —
  `type struct TraitChecker { arena, generic_registry, intern_table, diagnostics }`
  holding the trait-resolution cluster (moved verbatim): `type_implements_trait`,
  `type_satisfies_opaque_bound`, `diagnose_method_bounds_failure`,
  `bounds_satisfied`, `bounds_satisfied_at_depth`, `method_bounds_satisfied`,
  `lookup_subst_for_param`, `bare_name_of`. No mutable analysis state.
- `monomorphizer.cryo`: added `trait_checker: TraitChecker` field (built in `new`);
  the 5 methods still called by mono-internal code (`type_implements_trait`,
  `bounds_satisfied`, `method_bounds_satisfied`, `lookup_subst_for_param`,
  `bare_name_of`) are now thin forwarders; the 3 sema-only ones
  (`type_satisfies_opaque_bound`, `diagnose_method_bounds_failure`,
  `bounds_satisfied_at_depth`) were deleted from mono (live only in TraitChecker).
- `sema.cryo`: added `trait_checker(&this) -> TraitChecker` helper (builds from
  `this.arena / this.ctx.generic_registry / this.intern / this.ctx.diagnostics`);
  all 5 `ctx.monomorphizer` sites now call through it. The mono-null guards became
  `generic_registry`-null guards (the only state the queries need). **`sema.cryo`
  has zero live `ctx.monomorphizer` references** (only comments remain).

Why this is sound regardless of pass order: the trait queries read template-level
trait-impl blocks (populated at TemplateRegistration, well before mono) + arena
types; they never touch mono's worklist/instantiation-cache. Pure relocation, no
behavior change — proven by byte-identical self-host.

## Phase 2 — Teach sema to type-check generic bodies symbolically (NOT STARTED)
The heart of the project. See handoff Part 3 Phase 2. Run additively first
(alongside existing concrete-output checking, emitting nothing new) and prove zero
false positives on the whole suite + stdlib before flipping anything. Today sema
skips generic bodies: `visit_methods` in sema.cryo skips `method.func.is_generic()`
and `is_self_returning_default` (see ~line 1804). That skip is the gap to close.
Goal: the 5 `ctx.monomorphizer` sites in `sema.cryo` (~1730, 1770, 1889, 7429,
7480, 7711) all call read-only trait queries. Extract the cluster into a
standalone `TraitChecker` (depends only on arena/generic_registry/intern_table/
diagnostics). Mono composes a `TraitChecker` and forwards; sema uses it directly.
Nothing reorders yet — suite stays green, selfhost stays a fixed point.

Cluster to move (all closed over the 4 read-only pointers; verified):
`type_implements_trait`, `type_satisfies_opaque_bound`,
`diagnose_method_bounds_failure`, `bounds_satisfied`, `bounds_satisfied_at_depth`,
`method_bounds_satisfied`, `lookup_subst_for_param`, `bare_name_of`.
Free-fn deps: `find_inst_wrapping` (types/inference.cryo), `OwnershipQuery::*`.

Status: writing `compiler/src/compiler/types/trait_checker.cryo`.
