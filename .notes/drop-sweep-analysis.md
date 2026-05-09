# Drop Sweep — Analysis Notes (2026-05-08, mid-session)

This is interim analysis from a session that started the drop sweep
and pivoted to lower-risk tasks. The sweep itself wasn't executed
because it's slow (3-10 min selfhost-check per chunk) and the user
stepped away. The findings below should let the next session move
faster.

## Inventory

`grep -rn '\.drop()' stdlib/ | wc -l` → **175** sites across 23 files
(handoff said ~158; close enough). Saved at `/tmp/drop-sites.txt`
during the session.

## What the synthesizer actually emits drops for

Read `compiler/src/compiler/passes/drop_insertion.cryo` carefully
before sweeping. Key lines:

- `maybe_append_drop` (~929) only emits `binding.drop()` for **let
  bindings** (registered via `register_binding` in `walk_var_decl`,
  `walk_destructure_decl`, etc.) that are non-Copy and not already
  marked moved.
- `read_call` (~1000) explicitly handles `<ident>.drop()` —
  marks the binding moved (line 1011-1024) so synthesis skips at
  scope end. **The handoff's claim that "bare-let drops at end-of-
  scope are the problem cases" is wrong** — they're already handled.
- `read_call` also handles by-value receiver methods AND
  `![consumes_self]` methods (line 1037-1047) — both mark the
  receiver moved.

## Actual double-drop hazards

Three categories, ranked by severity:

### 1. Helper-via-reference functions (CRITICAL)

```
stdlib/json/parser.cryo:678        function drop_array(arr: mut &Array<JsonValue>)
stdlib/process/command.cryo:521    function drop_cstring_array(array: &Array<CString>)
```

Call sites (4 + 3 = 7 hazards):
- `stdlib/json/parser.cryo:566, 572, 582` — `drop_array(&arr)`
- `stdlib/process/command.cryo:394, 410, 426, 506` — `drop_cstring_array(&argv_storage)`

The analyzer can't see these as consumes. Once `SYNTHESIZE_DROPS=true`,
the synthesizer will emit a second `arr.drop()` / `argv_storage.drop()`
at scope exit, double-freeing the storage.

**Fix**: convert the helpers to take by-value (`arr: Array<JsonValue>`),
update call sites to pass without `&`. The analyzer sees by-value
arguments as moves automatically (see `read_call` arg loop line
1051 — `walk_expr_move` on every argument).

Example refactor for `drop_array`:

```cryo
// Before:
function drop_array(arr: mut &Array<JsonValue>) -> void {
    mut i: u64 = 0;
    while (i < arr.length()) {
        arr.ptr[i].drop();
        i++;
    }
    arr.drop();
}

// After:
function drop_array(mut arr: Array<JsonValue>) -> void {
    mut i: u64 = 0;
    while (i < arr.length()) {
        arr.ptr[i].drop();
        i++;
    }
    arr.drop();  // OR remove this once SYNTHESIZE_DROPS=true
}

// Call site:
drop_array(arr);   // was: drop_array(&arr);
```

Verify each call site doesn't use `arr` / `argv_storage` after the
helper call (a quick scan confirms they all return immediately on
error paths).

### 2. Field drops on locals (MINOR)

Found 4 sites:
```
stdlib/net/http/router.cryo:476          req_local.params.drop();
stdlib/collections/hashmap.cryo:242,271  node.key.drop();
stdlib/collections/hashmap.cryo:272      node.value.drop();
```

Pattern: `<local>.field.drop()` where `<local>` is a let binding.
The synthesizer would emit `<local>.drop()` at scope end. If
`<local>`'s destructor also drops the same field (via its struct
layout), that's a double-drop on the field.

Need to inspect each one's `<local>` type:
- `req_local` → likely a Request, `params` is a HashMap field
- `node` → HashMapNode, `key`/`value` are field types

**Fix**: depends on the destructor for the parent type. If
`Request::drop` already drops `params`, the manual call is the
double-drop. If not, the field drop is necessary and the manual call
must stay (the synthesized `<local>.drop()` then becomes the only
drop, and won't iterate to drop `params`).

Inspect by hand. Don't sweep blindly.

### 3. Bare `<ident>.drop()` calls (SAFE — leave them)

The bulk of the 175 sites. Already handled by `read_call`. No
action needed before flipping `SYNTHESIZE_DROPS`. They'll be
no-ops in the analyzer-marked-moved sense, but cosmetically
redundant. Consider stripping for code cleanup, but that's not a
correctness requirement.

### 4. `this.field.drop()` inside destructors (SAFE — must keep)

Most patterns like `this.buffer.drop();` in stdlib `drop()`
implementations. `this` isn't a let binding, so synthesizer doesn't
emit a drop for it. Manual field drops inside destructors are
necessary and must stay.

## `Array::drop` doesn't iterate

Verified at `stdlib/collections/array.cryo:286` — `Array::drop` only
deallocates storage. If `T` owns resources, the caller must drain.
This is why patterns like

```cryo
for (i in 0..arr.length()) { arr.ptr[i].drop(); }
arr.drop();
```

exist throughout — they're necessary, not redundant. The
synthesizer's `arr.drop()` at scope end **does not** drop elements,
so explicit element drops must stay.

## Recommended sequence for the actual sweep

1. **Refactor helpers** (Category 1) — 7 call sites, mechanical.
   Verify with `make cryo && make test`.
2. **Audit Category 2** — 4 sites, requires inspecting parent
   destructors. Hand-fix.
3. **Run `make selfhost-check`** — confirm byte-identity holds.
4. **Optional: strip Category 3 cosmetically** — the
   `<ident>.drop()` calls. This is pure code cleanup, no
   correctness change. Skip if anyone's worried.
5. **Flip `SYNTHESIZE_DROPS = true`** in
   `compiler/src/compiler/passes/drop_insertion.cryo:78`.
6. **Run full suite**: `make cryo && make test && make selfhost-check`.
7. **Add a generic + drop synthesis regression test** —
   the audit flagged this interaction as the riskiest. The
   existing `tests/tests/lang/consumes_self.cryo` covers
   non-generic.
