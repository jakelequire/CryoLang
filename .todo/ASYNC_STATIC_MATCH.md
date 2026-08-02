# `await` inside a `static match` arm

**Status:** rejected in sema, deliberately. This document is the design record for supporting it.

Today an `await` in a `static match` arm is counted by the lowering's walkers and rejected with its
own span:

```
error[E0600]: async: an `await` inside a `static match` arm is not supported — the arms are
selected after the body is lowered, so the suspend cannot be placed; bind it to a `const` ahead
of the `static match`
 --> main.cryo:10:5
 10 |     static match (T) {
    |     ^
```

Pinned by `tests/tests/negative/E0600_await_in_static_match_arm.cryo`.

## Why it is not a missing walker

The obvious reading — "add `StaticMatch` to the nine walkers like any other form" — is wrong. The
obstacle is pass ordering.

- `mono/ast_resolver.cryo` (`prune_static_match_arms`, statement form ~`:281`, expression form
  ~`:307`) selects an arm by **reducing `arms` to the single match, with the bodies still inside the
  node**. Codegen then emits `arms[0].body`.
- `async_lower` runs inside **sema**, and the pass order is `sema → Monomorphization`. So the
  lowering sees *every* arm.
- Lowering an arm moves its body **out** of the node into flat state blocks. Pruning can then no
  longer drop the arms an instantiation did not pick, so every arm's code reaches codegen — narrowed
  to a type that instantiation does not have.

Underneath that: the future is built once on the **template**. `f$Future<T>` is registered so mono
specializes it from the same demand that specializes `f<T>`, which means one field set and one state
count for every `T`. An arm-dependent suspend needs them to differ per `T`.

## Two candidate approaches

### A. Per-instantiation async lowering

Build the state machine after the arm is known, i.e. lower async bodies per instantiation rather
than once on the template.

Architecturally the right answer, and it would also retire the "generic contexts" caveats elsewhere
in the async design. But it inverts the `sema → mono` order the whole async implementation rests on,
and touches the state builder, the future-template registration, and drop-flag handling together.
This is a project, not a change.

### B. Guard each arm's state blocks with the same scrutinee

Emit every state block generated from arm *i* wrapped in `static match (T) { <arm i pattern> => { … }
_ => {} }`, so the existing pruning empties the blocks belonging to arms this instantiation did not
pick. The dispatch keeps a few empty states; harmless.

Much smaller than A, and it reuses machinery that already exists. **It has a real hole:** the future
still carries the *union* of all arms' carried locals, so a local whose type is only valid under its
own arm's narrowing — an associated type, or anything bound-dependent — becomes a field of an
invalid type for the other instantiations. Fine for arms that carry concrete types; broken for arms
that carry narrowed ones.

If B is taken, it must reject the narrowed-type case explicitly rather than miscompile it.

## What is already done

Counting is landed and is a prerequisite for either approach: `expr_await_count` and
`stmt_await_count` walk both `StaticMatchExpression` and `StaticMatchStatement`, summing over every
arm — the arms are pruned long after this runs, so the suspend has to be seen here whichever arm
holds it. That is what moved the diagnostic from codegen into sema with a correct span.

One gap remains inside the counting: a `static match` **expression** whose arm awaits falls through
to the generic "could not reduce to a statement boundary" message rather than the precise one. The
advice it gives is still correct.
