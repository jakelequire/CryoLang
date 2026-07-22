# Track 1 — Drop-completeness on unwind (implementation plan)

Sub-plan of `runtime/UNWINDER-PLAN.md` / `HANDOFF.md` Track 1. Written after Step 0
ground-truth enumeration. Branch `ll-impl`, base HEAD `49553df4` (clean).

## Step 0 ground truth (verified `--emit-llvm` @ 49553df4)

| Origin | Node | Current `--panic=unwind` IR | Unwinds? |
|---|---|---|---|
| direct `panic()` | `CallExprNode` (intrinsic) | plain `call @__cryo_panic`; `unreachable` | yes (raises), site LEAKS locals |
| div-zero / `INT_MIN÷-1` | `BinaryExprNode` `/` `%` | `br div.trap → call @abort(); unreachable` **ungated** | no (SIGABRT) |
| non-exhaustive match (stmt) | match stmt | `no_match: call @abort(); unreachable` ungated | no |
| non-exhaustive match (expr) | `MatchExprNode` | `mexpr.no_match: call @abort(); unreachable` ungated | no |
| `new T[n]` size overflow | `NewExprNode` | `icmp ugt → call @abort(); unreachable` ungated | no |
| array bounds `arr[i]` | `ArrayAccessNode` | **NO CHECK** — raw `getelementptr`+`load` (rel+O0) | n/a (UB) |
| arg temporaries `f(a,g(b))` | — | `populate_unwind_cleanup` schedules only NAMED locals | leak (Step 3) |

Decisions: **bounds INCLUDED** in Track 1 (Jake, 2026-07-22) and **always-on both panic
paths** (matches existing div/no-match/`new[]` guards; LLVM O2 folds provably-safe ones).
Consequence: bounds adds a check on the **default abort path** too → Track 1 **moves the
default fixed point → repin required** (the HANDOFF's "byte-identical, no repin" assumed no
new default-path IR; the bounds check breaks that assumption, everything else stays gated).

## Shared machinery to reuse (all exists from Phase C)

- Schedule producer: `DropInsertion.append_cumulative_drops` → `populate_unwind_cleanup(c)`
  writes `CallExprNode.unwind_cleanup: ASTNode*[]` (drop_insertion.cryo:1883/1898/2768).
  **Every** CallExpr already gets a schedule under unwind — incl. the panic call.
- Invoke swap: `expr_ops.build_call_maybe_invoke` (expr_ops.cryo:934) fires an `invoke` when
  `invoke_active`; `get_or_decl_personality` (:954), `landingpad_type` (:970).
- Landing pad: `call_emitter.emit_cleanup_landingpad(visitor,node,cur_fn,lpad_bb)`
  (call_emitter.cryo:1275) — builds cleanup-only `landingpad`, replays `node.unwind_cleanup`
  via `.accept(visitor)`, `resume`s, restores builder. **Refactor step below** makes it take
  a schedule array so non-CallExpr origins reuse it.
- Tier funnels (unwind tier, runtime/panic/unwind/src/lib.cryo): `__cryo_panic` (:… raises),
  `__cryo_panic_bounds_check` (:287), `_overflow` (:301), `_div_zero` (:316), `_no_match`
  (:322). `IntrinsicEmitter.get_or_decl_cryo_panic()` (intrinsic_emitter.cryo:771) is the
  decl-or-get precedent for the raising funnels (add sibling get-or-decls for the check ones).

## Refactor 0 (mechanical, no behavior change): schedule-driven landing pad

Split `emit_cleanup_landingpad` so the schedule is a parameter:
`emit_cleanup_landingpad_sched(visitor, sched: ASTNode*[]*, cur_fn, lpad_bb)`, and keep the
existing `emit_cleanup_landingpad(visitor, node, cur_fn, lpad_bb)` as a one-line forwarder
passing `&node.unwind_cleanup`. Verify byte-identical IR (pure refactor).

## Increment 1 — direct panic (schedule ALREADY on node; smallest end-to-end)

`emit_panic_call` runs on the intrinsic fast-path (call_emitter.emit `:186` Identifier branch
+ `:427` ScopeResolution branch → `try_emit("panic",…)` → `emit_panic_call` → early return),
bypassing the `:1157` invoke-swap. `node.unwind_cleanup` is already populated.

In BOTH dispatch branches, before the generic `try_emit`, detect the panic leaf and, when
`ctx.project_panic_unwind && !intrinsics.no_runtime && node.unwind_cleanup.length > 0`, take an
armed path (new helper `emit_panic_unwind(visitor, node, a0,a1,a2)` on call_emitter):
1. `cur_fn = get_current_function()`; append `panic.cont`, `panic.lpad`.
2. `panic_fn = cg.intrinsics.get_or_decl_cryo_panic()`; marshal (a0,a1,a2) into a 3-slot buf,
   fty `(ptr,ptr,i32)->void`.
3. arm `expr_ops.invoke_active/invoke_normal=cont/invoke_lpad=lpad`;
   `expr_ops.build_call_maybe_invoke(fty,panic_fn,buf,3,"")` → invoke; builder now at cont.
4. `builder.build_unreachable()` (cont is unreachable for the noreturn funnel).
5. `emit_cleanup_landingpad(visitor, node, cur_fn, lpad)` (reuses node.unwind_cleanup).
6. `state.last_value = null`; return.
Else fall through to the existing `try_emit` plain-call path (abort / no_runtime / no-locals
all byte-identical). IntrinsicEmitter untouched. Gated → default IR unchanged.

Validate: drop-count run-test — a named droppable local live across a direct `panic()` drops
exactly once on unwind (root catch → exit 101), plus the §2 e2e probes still pass. Default
abort path byte-identical (234-module s3/s4 check).

## Increment 2 — div-zero / no-match / `new[]` overflow (gate abort→raise under unwind)

These synthesize a check at codegen with NO AST call node, so they need a schedule attached to
their origin node by DropInsertion (mirroring CallExpr). Plan:
- Add `unwind_cleanup: ASTNode*[]` to `BinaryExprNode`, `MatchExprNode` (+ match-stmt node),
  `NewExprNode`. Populate in DropInsertion `read_binary`(only for `/`,`%`), the match-arm
  walker (only when non-exhaustive/no wildcard), `read_new` (only array-form w/ elem_size>1) —
  each via `append_cumulative_drops` at that node, gated on `project_panic_unwind`.
- At each emit site, gate on `ctx.project_panic_unwind`:
  - unwind: get-or-decl the raising funnel (`__cryo_panic_div_zero`/`_no_match`/`_overflow`),
    and if the node's schedule is non-empty emit `invoke funnel → cont/unwind lpad` +
    `emit_cleanup_landingpad_sched` + `cont: unreachable`; empty schedule → plain
    `call funnel; unreachable`.
  - abort (default): keep `call @abort(); unreachable` — byte-identical.
  Sites: expr_ops.cryo:1826 (div), :1871 (`new[]` mul) + new_delete_emitter.cryo:364;
  ir_generator.cryo:734 (stmt no_match), :1785 (expr no_match).
Validate: drop-count run-tests for div-zero / no-match live-local; each now unwinds (exit 101)
not SIGABRT; abort path byte-identical.

## Increment 3 — array bounds (NEW check; memory-safety; moves default IR → repin)

At the fat-array subscript emit site (place_emitter.cryo, the `arr[i]` GEP path that Step 0
showed unchecked): before the `getelementptr`, emit `icmp uge %i, %len` (len = fat-array field
[1]) → `bounds.trap` / `bounds.ok`. `bounds.trap`:
- abort (default): `call @abort(); unreachable`.
- unwind: raise `__cryo_panic_bounds_check(idx,len)` as call/invoke (+cleanup if schedule).
Only for DYNAMIC indices (const in-range indices stay elided — Step 0 showed sema already
handles const bounds; match that). This is the ONLY origin whose ABORT path changes → **repin**
after this increment. Attach the schedule via `ArrayAccessNode.unwind_cleanup` for the invoke
form; the abort form needs no schedule.
Validate: OOB dynamic index aborts under abort / unwinds+drops under unwind; in-range unaffected;
NO bounds check for provably-const in-range; then **`make pin` + verify-pin** (default IR moved).

## Increment 4 (optional, deferrable) — arg-temp drop flags (Step 3)

Extend `populate_unwind_cleanup` + MoveCheck temp tracking to schedule anonymous arg temporaries
live at the origin (needs a "constructed?" init-flag like `maybe_append_drop`). Defer if it
balloons — an arg-temp leak is rarer than a named-local leak.

## Gates / repin (see HANDOFF §2)

Per increment: `CRYO_CC=gcc make cryo && make test`; unwind e2e probes via WSL; `make
selfhost-check` with the FULL 234-module s3/s4 IR check (Linux md5 is DRIVER-only — gate-hole).
Increments 1-2 gated → default byte-identical → NO repin. Increment 3 moves the default path →
**`make pin` (plain, never `CRYO_CC=gcc make pin`) + `python scripts/verify-pin.py`**. Never
commit (Jake commits). Never two heavy builds at once (SIGTERM landmine).
