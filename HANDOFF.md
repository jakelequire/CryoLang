# Cryo — handoff for next agent (stdlib-next bridge)

**Branch:** `new-stdlib` (off `main`)
**Date:** 2026-04-30, mid-session.
**Goal:** Land the stdlib-next bridge so cryoc can compile
`experimental/stdlib-next/`, then swap that tree in to replace
`stdlib/` for the 0.1.0 release.

> **Note to next agent:** The previous agent's memory files (under
> `~/.claude/...`) do **not** exist on this machine. Treat this file
> as the only source of truth for session state. The big-picture plan
> lives in `experimental/stdlib-next/PLAN_STDLIB_BRIDGE.md` (in-repo).

---

## Quick status

- **Phase 0 done.** `bin/cryo` is a pinned self-host binary committed
  to the repo. `make cryo-fast` skips the C++ bootstrap and rebuilds
  cryoc in ~38 s. `make cryo` (canonical bootstrap path) and
  `make selfhost-check` (8-stage byte-identity gate) both still work
  and remain the source of truth.
- **Phase A done.** Parser + AST changes for all 9 listed gaps
  (G1–G6) plus the stdlib-next prerequisites that surfaced once
  trait bodies started being parsed in real code.
- **Phase B in progress.** Sema work has *not* started; the work
  done after Phase A so far is more parser fallout from running the
  whole-stdlib build (see "uncommitted state" below).

`make selfhost-check` md5 after the last clean commit
(`a0a43f22`): `3d7bacf4d348da7c296ec107c6a25541`. Stage-4 ==
stage-5 byte identity holds.

## Hard constraints (do not violate)

1. `compiler/src/` must keep parsing under both the C++ bootstrap
   (`legacy/bootstrap/bin/cryo`) AND the pinned `bin/cryo`. This
   means **no new syntax in compiler source** — no trait impls,
   no default type params, no `This` in cryoc's own code. The new
   parser features are for stdlib-next consumption only.
2. `make selfhost-check` must stay green after every commit. The
   exact md5 is allowed to drift (it does whenever cryoc itself
   changes), but the stage-4 == stage-5 invariant must hold.
3. Every parser/AST change is **additive** — new optional fields,
   new branches gated on token peek. No reshaping.
4. **No hacky workarounds.** User reinforced this twice in the
   prior session. Plumb every visitor (cloner, substituter, dumper,
   visitor, specializer) when AST shape changes; explicit kind
   switches per `feedback_cloner_vtable.md` rule (the C++ codegen's
   overloaded virtual dispatch goes wrong otherwise).
5. **No milestone-verification scripts in the repo.** User pushed
   back on `scripts/parse-stdlib-next.sh` as bloat. Verify
   ad-hoc during dev or fold into existing test infra.
6. **Test against the whole stdlib-next, not individual files.**
   Run `cd experimental/stdlib-next && /…/bin/cryo build` (or use
   `make cryo-fast` first to refresh the binary, then run from
   stdlib-next/ with `compiler/build/bin/cryo`). Per-file
   `cryo raw` checks miss cross-module signal.

## Commits landed on `new-stdlib` (oldest → newest)

```
8e36db60 build: pin self-host cryo binary at bin/cryo
e7ab399b parse: default type parameters (G3)
3ac2a30f parse: multi-bound + qualified-name trait bounds (G4, G5)
90404597 parse: This as a type annotation (G6 parser side)
81502e40 parse: implement trait Foo for Bar, with where (G1, G2)
28905fc1 parse: stdlib-next prerequisites — trait/impl bodies, mut &T
a0a43f22 build: remove scripts/parse-stdlib-next.sh
```

Each commit message describes its scope and verification.
Phase A commits introduced sandbox tests under
`compiler/sandbox/bridge/`:
- `g3_default_param.cryo`
- `g4_g5_bounds.cryo`
- `g6_this_type.cryo`
- `g1_g2_trait_impl.cryo`

## Uncommitted state (working tree)

Two small parser fixes that surfaced when running the
whole-stdlib build at the end of the previous session:

```
M compiler/src/compiler/parser/expr_parser.cryo
M compiler/src/compiler/parser/parser.cryo
```

1. **`parse_variable_declaration`** in `parser.cryo` — accepts
   keyword-named variables (e.g. `mut string: String`,
   `const default: i32`). Same pattern as `parse_parameter`.
2. **`parse_enum_pattern_internal`** in `expr_parser.cryo` — accepts
   keyword names as pattern bindings (e.g.
   `match x { Result::Ok(string) => { … } }`).

These were rebuilt and `make cryo-fast` succeeded. Selfhost-check
was **not** re-run after them. The previous agent was about to
run the whole-stdlib build to surface the next blocker when the
user said one of the commands was crashing the PC and asked for
the handoff. **Recommendation:** before running the stdlib-next
build on the new machine, decide whether you want these fixes —
they're small, additive, and unblock collections/string.cryo.
Either commit them or revert them.

> The previous deletion of the old `HANDOFF.md` (`D HANDOFF.md` in
> `git status`) is from earlier reorg work and unrelated. The
> deletion is staged in the working tree; just `git restore` it or
> let the new HANDOFF.md replace it.

## Repo layout reminders

- `compiler/` — self-hosted Cryo compiler (active, in current Cryo
  dialect — DO NOT add trait impls here yet).
- `compiler/src/compiler/parser/parser.cryo` — most parser changes
  land here.
- `compiler/src/compiler/parser/expr_parser.cryo` —
  `parse_type_annotation`, `parse_base_type`, `parse_reference_type`,
  `parse_enum_pattern_internal`.
- `compiler/src/compiler/AST/_module.cryo` — `TypeAnnotation` enum,
  `TraitBound`, `TraitRef`.
- `compiler/src/compiler/AST/declaration.cryo` — `ImplBlockNode`,
  `GenericParamNode`, `FunctionDeclNode`.
- `compiler/src/compiler/AST/{cloner,substituter,dumper,visitor,
  specializer}.cryo` — every AST shape change must touch the
  relevant ones (per constraint #4 above).
- `compiler/src/compiler/types/` — `resolver.cryo`, `checker.cryo`,
  `monomorphizer.cryo`, `generic_registry.cryo`. This is where
  Phase B work lands.
- `compiler/src/compiler/codegen/` — Phase C lands here.
- `compiler/sandbox/bridge/` — sandbox `.cryo` test files for each
  gap. Add new ones for new shapes; keep them small and self-
  contained (each ends in `function main() -> i32 { return 0; }`
  so it's a complete program).
- `legacy/bootstrap/` — C++ bootstrap. **Frozen.** Don't touch.
- `experimental/stdlib-next/` — the target stdlib. Has its own
  `cryoconfig` declaring `target_type = "stdlib"`. Build with
  `cd experimental/stdlib-next && /…/cryo build`.
- `stdlib/` — current canonical stdlib. Will be replaced in Phase D.
- `bin/cryo` — pinned binary. Refresh ONLY if compiler/src/
  syntax changes such that bin/cryo can no longer parse it; that
  should never happen during this bridge by design.

## What's verified to work in stdlib-next

The whole-stdlib build (with the uncommitted fixes applied) gets
through **27 modules** of `experimental/stdlib-next/` before
hitting `collections/string.cryo`. The first 27 include all of
`core/` and `alloc/` parsing cleanly (they fail only at sema/
codegen, which is expected pre-Phase-B).

## What's NEXT

The bridge plan calls for Phase B (sema), but the whole-stdlib
build is exposing more parser shapes that stdlib-next uses. The
practical next step is one of:

**(a) Continue parser-prerequisite hunting** until
`cryo build` from `experimental/stdlib-next/` reaches the
sema/checker without parse errors, then start Phase B. The user's
preferred workflow per the last session is: `cd
experimental/stdlib-next && /…/bin/cryo build`, look at the next
error, fix it, repeat. The two uncommitted fixes above are
examples of this loop.

**(b) Start Phase B regardless.** The bridge plan's Phase B order
is: B.1 This-resolution → B.2 default-param substitution →
B.3 trait-impl registry → B.4 bound-aware dispatch →
B.5 bound enforcement → B.6 default-method synthesis. B.2 is
contained and a good entry point. Several entry points were
scoped in the planning session; full text is in
`experimental/stdlib-next/PLAN_STDLIB_BRIDGE.md`.

**Recommend (a).** Each surfaced parser issue is small, additive,
and removes a real blocker. Phase B is high-risk and benefits
from a clean parser baseline.

## Key gotchas observed during Phase A

1. **cryo lexer treats `\033` as `\0` + `33`.** Most ANSI-coloured
   `printf` calls in the dumper are silently truncated. Pre-existing,
   not bridge-related, but explains why dumper output looks weird.
2. **`is_keyword()` matters everywhere a name is consumed.** Many
   parser sites (`parse_variable_declaration`, the trait body
   handler, `parse_enum_pattern_internal`) had Identifier-only
   checks that broke when stdlib-next used keywords like `string`,
   `from`, `default`, `into` as names. Pattern: accept
   `Identifier || is_keyword()` after we've already disambiguated
   by position.
3. **`mut &T` vs `&mut T`.** stdlib-next uses `mut &T`; cryoc
   itself uses `&mut T`. Both are now supported by
   `parse_type_annotation` and produce the same `ReferenceAnnotation
   { is_mutable: true }` shape.
4. **`is_generic_call_ahead`** now accepts `KwFor` and `KwWhere` as
   followers after `>` so that `Iterator<T> for Bar` and
   `Bar<T> where ...` parse as generic-arg lists.
5. **`parse_function_declaration`** treats the leading `function`
   keyword as optional (line ~336 in `parser.cryo`). Trait body
   methods written as `name(params) -> Ret;` now parse.

## How to verify before each commit

```
cd /home/phock/Programming/apps/CryoLang   # adjust on new machine
make cryo-fast            # ~30-40s; rebuilds cryoc via the pin
make selfhost-check       # ~3 min; full byte-identity gate
```

For stdlib-next specifically:

```
cd experimental/stdlib-next
/…/CryoLang/compiler/build/bin/cryo build 2>&1 | tail -50
```

If a command takes too long or hangs, stop it. The previous
session ran into a command that was crashing the user's PC near
the very end (likely a stdlib-next build invocation); **do not
casually re-run a command that was reported as crash-prone.**

## Plan reference

The full operational plan was at
`~/.claude/plans/serialized-sleeping-mango.md` on the previous
machine. The design rationale (G1–G9 gaps, Phase B/C/D structure,
risk register) lives in-repo at
`experimental/stdlib-next/PLAN_STDLIB_BRIDGE.md` and is the
durable reference. Expect line numbers in that document to be
slightly off from current `parser.cryo` since Phase A's edits
shifted things.

---

**Pick up by:** running `git status` to confirm the two
uncommitted parser changes are still there, deciding whether to
commit or revert them, then either continuing parser-prerequisite
hunting or starting Phase B.1 / B.2.
