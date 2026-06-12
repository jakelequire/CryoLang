# HANDOFF: Iterator-combinator limitation — REMOVED ✅

**Date:** 2026-06-12 (continuation). **Branch:** `main`, base `9b8dd690`.
**State:** UNCOMMITTED (Jake commits). Linux pin refreshed; Windows pin refreshed.

The CHANGELOG "Iterator combinators are a … set" known-limitation bullet is
**deleted**. Combinators are now a shipped `core::iter` feature; the only residual
is reframed as a *general* "local type inference" limitation, not iterator-specific.

---

## What landed this session

### 1. `.zip` shipped — and the real blocker was NOT what the prior handoff thought
The prior handoff hypothesized a deep monomorphization-ordering bug (adapter laying
out `Option<Pair>` before its `Pair` payload). **That was wrong.** The adapter and
its `next`/`count` bodies were always correct.

The actual bug was in **sema**: for `a.zip(b).count()`, `count`'s receiver
`a.zip(b)` is typed by `resolve_method_call`'s argument-**blind**
`lookup_method_with_inheritance(Range<i32>, "zip")`, which returns an arbitrary
sibling's return type when two `zip` specializations coexist on the same receiver
(`J=Range<i32>` vs `J=Range<u64>`). `count` (no own generics → never pinned by mono;
codegen dispatches on `a.zip(b).resolved_type`) then ran on the **wrong** `ZipIter`
→ silent `0`.

**Fix** — `compiler/src/compiler/passes/sema.cryo` (~line 5016, MemberAccess call
branch): `resolve_method_overload` already arg-disambiguates `call.resolved_method`;
when that pinned method is concrete and its return type disagrees with the name
lookup, prefer it. Gated on both-valid-and-differing, so non-ambiguous calls are
untouched.

`ZipIter` is chain-shaped (`type struct ZipIter<I,J>` + `implement<I,J,A,O>
Iterator<Pair<A,O>> … where I:Iterator<A>, J:Iterator<O>`), `zip<J>` trait default
infers exactly like `chain<J>`. In `stdlib/core/iter.cryo`.

### 2. Nested adapter bound to a typed local — fixed
`mut m: MapIter<TakeIter<Range<i32>>,i32,i32> = r.take(3).map(f); m.next()` crashed
with an LLVM null receiver. The type resolver can't instantiate a nested
where-bound-adapter *annotation*, so sema left the binding's `resolved_type` invalid;
`codegen_local_var` bails on an invalid type *without registering the local* → null
`this`.

**Fix** — `sema.cryo` (~line 2051, var-decl back-fill): when any `type_annotation`
fails to resolve but the initializer types concretely, set
`var_node.resolved_type = init_type`. Typos are already caught upstream by E0203 in
name resolution, so this only rescues valid-but-unresolvable annotations, and it
mirrors sema's existing scope-table fallback (the AST node now agrees with codegen).
Single-level adapter locals always worked; only the nested form was broken.

### 3. Docs / tests
- CHANGELOG: limitation deleted, `core::iter` feature bullet added, residual reframed
  as general "local type inference".
- `docs/cryo.md` §21.1: honest note that an opaque `implement Iterator<T>` local
  can't take combinators (chain on the expression / use a concrete adapter local).
- +8 tests in `tests/tests/stdlib/iter.cryo` (6 zip incl. the two-coexisting-combos
  regression; 2 nested-adapter typed-local).

## Validation
- **1097 unit / 91 compile-fail green.**
- **Linux selfhost: byte-identical fixed point** (md5 `1ad5db8f…`).
- **Linux pin + Windows pin both refreshed.**
- Known flaky test `Stdlib::NetHttps :: https_loopback_round_trip` (TLS over
  loopback, pre-existing, ~40% fail rate) — UNRELATED to these changes.

## Deliberately left open (general, not iterator-specific)
- Opaque `implement Iterator<T>` local + combinator → E0636 (`TakeIter<This>`):
  the concrete cursor is erased by the opaque type. Clean workaround (chain on the
  expression). Documented in docs/cryo.md.
- No general local type inference (E0104).
- §5 mono divergence guard (a `collect` *trait default* diverging through `RefIter`)
  — still deferred; not needed, since `from_iter` is a free function and doesn't
  diverge.
