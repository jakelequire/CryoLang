# Roadmap

What comes after `naming-impl` is merged into `main`.  Each item says what
it is and what is still open about it.  Nothing here is built yet; the
rulings that constrain an item are in the appendix of
`docs/resolution-rules.md`, and the language as it stands is
`docs/cryo.md`.

## The editor's symbol layer, rebuilt on compiler identities

The language server answers from the compiler's own identities - the
definitions, types and module identities the name layer stamps - instead
of finding declarations by their spelling.

- **On-save analysis.**  The compiler analyses a file when it is saved, not
  on every keystroke.
- **A last-good snapshot.**  The editor keeps the most recent analysis that
  succeeded, so a half-typed line does not take every answer away.
- **Completion** comes from that snapshot plus the line being typed.
- **Module caching on the compiler side**, so a save re-analyses what
  changed rather than the whole program.
- **The editor's remaining allows go then.**  Its 39 `![allow]`s naming the
  deleted spelling lint are inert today and are deleted in this work (ruling
  184: the editor's allows are deleted during its own work after the
  merge).  The bare-name finders the editor still carries (D38) and its
  fallback for a file with no project (D63) go in the same work.

Open: how the snapshot is invalidated when an imported module changes; and
whether the cache is per module or per file.

## CI

Continuous integration comes back after the merge (ruling 163: CI is out
of scope until after the merge into `main`).  Today it fires only on
`main` and on a manual dispatch, so on a branch the gates are what a
person or an agent runs.

Open: which gates run on every push and which only before a release; and
whether branch pushes trigger anything at all.

## Array syntax

`T[]` becomes `[T]` for a dynamic array, and `[T; N]` is the fixed-size
array.

The first phase changes nothing: it re-measures the corpus - every array
type written in `compiler/`, `stdlib/`, `tools/`, `tests/` and `examples/`
- so the conversion is planned from today's numbers, not old ones.

Open: whether `T[]` stays accepted for a transition period or is refused
at once; how a fixed-size array's length is written when it is a constant
expression; and what the conversion does with arrays of function types,
which today need parentheses (`((i32) -> i32)[]`).

## The compiler pipeline: AST, HIR, MIR, LLVM

The front end lowers the syntax tree to a high-level representation, then
to a mid-level one, and only then to LLVM IR.  Today the checked syntax
tree goes straight to code generation, so every later pass rereads
source-shaped nodes.

Open: which of today's passes (drop insertion, async lowering, the move
checker, monomorphization) move to which level, and in what order the
move happens.

## Compile-time execution

Code runs at compile time by interpreting MIR, so it needs the pipeline
above.

Open: whether any function can run at compile time, with a program free to
run arbitrary code during its build, or a narrower model with marked
compile-time functions and limits on what they may touch.

## A context allocator scope

`with_allocator(arena) { ... }` makes `arena` the allocator for the `new`
expressions inside the block.  It arrives with the compile-time work.  A
class's own `Allocate` implementation wins over it (ruling 249: `new` and
`delete` go through the `Allocate` trait, and the nearest ancestor's
implementation wins).

Open: whether the scope reaches into the functions the block calls or only
the `new` written inside it; and what `delete` does with memory allocated
under a scope that has ended.

## `cryo-lint`

A lint tool built on the compiler as a library, as the language server is:
it reads the compiler's stamps and types, so a lint is a question about
identities rather than a pattern over text.  The deny-by-default lint with
a reason-carrying allow, and the `deprecated` attribute (D49, D64), belong
here.

Open: which lints ship first, and whether `![allow]` names lints of this
tool or only the compiler's own.

## Decided language work waiting for its own branch

Each of these was decided and moved off `naming-impl` by the freeze on the
language's features; each is a branch of its own after the merge.

- **Primitive type names stop being keywords** (D18), and with them a cast
  followed by a comparison without parentheses, `x as u32 < y`, is refused
  and the parentheses asked for (D66).
- **Explicit receivers** on every non-static method (D36, D65).
- **A reference-mutability check**, run before the async lowering (D37).
- **The `raw` subcommand builds a module graph** as `check` and `build` do
  (D60).

## Paths through generic type aliases

`type Stack<T> = Array<T>; Stack::<i32>::new()` names `Array::<i32>::new()`,
and `Stack::new()` infers the alias's arguments as a path through the target
does.  Refused today with `E0233` where the path is written (ruling 265: a
generic alias used as a path is refused with a clear error until it is
supported); a non-generic alias's path already names its target's members.

Open: whether an alias that fixes some of its target's parameters
(`type StrMap<V> = HashMap<string, V>`) infers only the alias's own.

## Open questions

- **How wide a `char` is**, and so what a character literal holds above
  255 (`'\u{e9}'` fits either way; `'\u{1F600}'` does not fit a byte).
  Undecided.
- **A module cycle's report.**  E0501 names the modules in a cycle; whether
  it prints the cycle as a path, in order, is open, and so is whether a
  cycle that crosses packages is E0501 too or an error of its own.
- **A private type reached by inference.**  E0503 refuses a `private` type
  where the source names it, but a value of one returned by a public
  function and bound by inference crosses the module boundary and answers
  method calls.  `tests/tests/projects/resolution_tripwire` pins the
  behaviour as it is.  Whether that is refused, and where, is open.
- **`std::Range`.**  Nothing re-exports `Range` at the root of `std`, so the
  path is refused today.  Whether `stdlib/lib.cryo` exports it there is open.
