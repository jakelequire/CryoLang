# Roadmap

What comes after `naming-impl` is merged into `main`.  Each item says what
it is and what is still open about it.  Nothing here is built yet; the
rulings that constrain an item are in the appendix of
`docs/resolution-rules.md`, and the language as it stands is
`docs/cryo.md`.  Known bugs are not here: they are in the local bug log,
`.bug-report/` (see `CLAUDE.md`).

**Order.**  CI is aligned with `make verify`'s gate list first, as
housekeeping (ruling 266: CI matches verify's gate list first thing after
the merge).  The first project is the editor's rewrite, on a branch named
`lsp-rewrite` (ruling 316: the editor's rewrite comes first, before the
type-syntax update and the class removal).  Then the type-syntax update
(the array syntax below), then the class removal, then trait objects; the
other items are not ordered yet.

## Merge notes

`naming-impl` is merged into `main`, and CI runs verify's gate list.  These
notes keep what the gates mean and what is still red or unexercised.

**What the local gates mean.**  `make check-fast` runs verify's self-test,
verify-pin's self-test and `make verify-pin` (each pin's sha256 and stamped
commit match its sidecar, and both pins name one commit).  It builds
nothing, so it says nothing about the compiler.  `make verify` is the
evidence: it builds the compiler once, then runs the census (every suite,
counts against `tests/test-roster.txt`), every example's build, the LSP
build with the compiler under test, the other OS's half (objects only), the
incremental new-instance fixture and check-fast, all concurrently; with
`ARGS="--baseline HEAD --require-identical"` it also proves no compiled
object moved.

**CI runs verify's gate list** (ruling 266: CI is aligned with verify's
gate list first thing after the merge).  CI fires only on `main` and on a
manual dispatch.  As it stands:

| gate | `make verify` | CI |
|---|---|---|
| census (`test-census`) | yes | yes, both hosts |
| examples build | yes | yes, both hosts |
| `lsp-check` | yes | yes, both hosts |
| `cross-check` | yes | windows native only: a Linux host links the windows-gnu build and needs mingw and the LLVM-C import lib, until `cryo build` can stop at objects (below) |
| `incremental-instance-check` | yes | yes, both hosts |
| `check-fast` (the two self-tests) | yes | yes, both hosts |
| object comparison against a baseline | yes | Linux, against the previous commit, report only: a moved object is listed and does not fail the job (ruling 321) |
| `roster-check` | no (the census reads the same golden) | yes |
| `examples-golden`, `valgrind-check`, `verify-freestanding` | no (POSIX only) | yes |
| `incremental-check` | no | yes |
| `vendor-check` | no | yes |
| `api-index-check` | no | yes |
| `selfhost-check` | no | yes, both arms: the Windows chain under wine on the Linux runner |

Every CI job is green on `main`.  The rows `make verify` does not run are
CI's alone, so a local green does not cover them.  CI also builds the
Windows release zip and builds and runs a hello-world with it under wine,
the way the release workflow does before it publishes.

**Warnings.**  A clean build of the compiler, the standard library's own
build and the editor's build each report 0 warnings.  Compare warnings only
from a clean build: an incremental build does not replay a cached module's
warnings.

**`--baseline` cannot reach `main`.**  `make verify ARGS="--baseline REV"`
fails for any revision before the version stamp, `main` included (ruling
298: fixed after the merge), because the baseline is stamped by the working
tree's Makefile and an older compiler has no stamp slot.  So the merge
itself cannot be checked object-for-object against `main`; compare against
`naming-impl`'s tip instead.

**Known red or unexercised.**
- `make cross-check`, and with it `make verify`, cannot pass on a Linux
  host: the compiler links the windows-gnu build there rather than stopping
  at objects.  Run verify from Windows, or pass `--only` to leave cross out.
- The release smoke runs the STAGED tree, not the archive, and the Windows
  tree under wine with llvm-mingw's `clang.exe` driving the link; nothing
  runs the zip on a Windows host with the mingw `gcc` an installed
  `cryo.exe` finds there.
- `make release`, `install` and `uninstall` are POSIX recipes with no
  Windows branch; on a Windows host they fail with a cmd parse error rather
  than a refusal.
- `ProcessAsync::two_children_join_concurrently` flakes under load on
  Windows: it compares two wall-clock measurements, and process start-up
  under the suite's load can stretch one past the other.  `CRYO_TEST_JOBS=4`
  steadies it.  It is the only test known to flake.

**Tree state.**  The session scratch directories are deleted (ruling 299:
the scratch directories are deleted after an inventory), the one-off
codemods are deleted (ruling 268: a codemod goes in the commit after the
one that ran it), and `scripts/README.md` names every remaining script and
its caller.  `make clean` removes every build output and gate state the
targets write.  The untracked plans in `docs/` and `.todo/` are not part of
the branch.

## First: the editor's rewrite (`lsp-rewrite`)

The first project after the merge, on its own branch (ruling 316).  The
language server answers from the compiler's own identities - the
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

The first change after the merge makes CI run the gate list `make verify`
runs (ruling 266: CI is aligned with verify's gate list first thing after
the merge), so green on `main` means what green meant on the branch.  Until
then the difference is recorded in the merge notes.

Open: which gates run on every push and which only before a release; and
whether branch pushes trigger anything at all.

## The test runner reports what it ran

Right after the merge, `cryo test` always prints a count for each of its
three suites - zero included - and `--list` lists all three, not the unit
suite alone (ruling 272: the runner prints each suite's count and lists
every suite, retiring the census and roster scripts).  `test-census.py`
then reduces to nothing, and `roster-check.py` to a diff of `--list`
against the golden.

A project's `test.json` gains `stdout_file`, which compares the program's
whole output against a file (ruling 271: a `stdout_file` field compares a
program's output against a file).  The examples' `expected.out` goldens move
onto it, which retires `scripts/check-examples-output.sh` and makes the
check run on Windows as well.

Open: the summary line's exact format, and whether line endings are
normalised before the comparison.

## Gate and script follow-ups

- **`scripts/` is restructured** into `gates/`, `pin/`, `release/`,
  `toolchain/` and `dev/` (ruling 273: the restructure happens after the
  merge).  Renames only, with every caller updated in the same commit.
- **The language server's smoke test runs in the LSP check.**
  `tools/CryoLSP/tests/smoke.py` starts the server and asks it questions;
  nothing calls it today (ruling 269: it stays and is wired into the LSP
  check after the merge).
- **`cryo build` can stop at object files** (ruling 322: on the roadmap,
  not built yet).  A Linux host links a windows-gnu build rather than
  stopping at objects, so `make cross-check` there needs mingw and the
  LLVM-C import lib, and `make verify` cannot pass on Linux; CI runs
  cross-check from the Windows job only.  A flag that ends the build before
  the link lets the gate check what it claims to, objects only, from either
  host.
- **The language server reports the stamped version** (`serverInfo`) that
  `cryo version` prints.
- **`cryo version --verbose` shows the binary's sha256,** once the standard
  library has a sha256; it has sha1 only today.
- **`make verify ARGS="--baseline REV"` works for a revision from before
  the version stamp,** `main` included (ruling 298: fixed after the merge).
  verify builds every baseline with the working tree's Makefile, which runs
  `cryo stamp` on it, and a compiler from before the stamp has no slot to
  stamp: "it holds 0 stamp regions; expected exactly one".  A baseline
  build needs to skip the stamp step.

## Associated constants

A type declares named constants reached by a path, so `i32::MAX` is a
value.  Until then a primitive's bounds are `min_value()` and `max_value()`,
and `i32::MAX` is refused (ruling 275: primitives keep `max_value()` and
`min_value()` for now).

Open: whether a trait may declare a constant its implementations supply,
and whether a constant may be used where a type argument is expected.

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

## The class removal

Classes are removed as a language feature before the v1.0 freeze: after
the array syntax and the type-system update, and well before the pipeline
work, compile-time execution and what follows them.  What remains is
structs, enums and traits.  The compiler's AST goes straight to enums and
`match` (ruling 290: no intermediate struct-node stage).

It starts with a read-only scoping audit.  What is known now:

- 109 classes, all in the compiler; the standard library, the language
  server and the examples declare none.
- The AST is 69 of them, at most 3 deep.
- 6 visitors walk it (cloner, dumper, substituter, IR generator, name
  resolver, sema).
- Dispatch is already by tag: about 1,300 `kind` checks and 2,400 casts,
  against 188 `accept()` calls.
- Every hierarchy is a closed set, so each becomes an enum with no need for
  dynamic dispatch.

The class defects known today stay unfixed, since classes are going
(ruling 291: an unchecked `override`, a silently missing vtable slot, and
slots matched by name).  Trait objects must not repeat them.

Open: how the six visitors' `visit` overloads become `match` arms without
a flag day across the whole compiler, and what replaces `ASTNode`'s
`Allocate` arena when the nodes are enum values.

## Trait objects

A pointer or reference to a trait, dispatched through a table of the
implementation's methods.  Built after the class removal, possibly after
v1.0 (ruling 286).

- **Spelling:** `virtual &Shape` and `virtual Shape*`, a prefix as in
  `mut &this`; there is no `dyn` (ruling 285).  Until then `&Shape` and
  `Shape*` are refused where they are written (`E0313`; ruling 287), as
  is a bare `p: Shape` (ruling 304).
- **Object safety is per method** (ruling 288): a method that cannot go in
  the table - one with type parameters of its own, for instance - is unavailable on
  the object, and the rest of the trait still works.
- **Borrowed pointers first** (ruling 289); an owning box, which must take
  its drop, size and alignment from the table, comes later.
- The table's slots come from the trait's method numbering, the same one
  the trait-implementation check (E0312) relies on, never from method
  names.

Open: whether a supertrait's methods are reachable from a subtrait object
directly or through an upcast; and how a generic implementation coerced to
an object becomes a monomorphization root, including in the incremental
cache.

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

## Capturing closures held by function-typed slots

A capturing closure stored in a variable, field or array element of
function type, assigned to one, or returned from a function is refused
today (`E0458`; ruling 293: refused until it is supported properly).  A
function-typed value is a bare function pointer, with nowhere to keep the
captures, so supporting it means a function value that carries an
environment: a pointer to the code beside a pointer to the captures, with
an owner that releases them.

Open: whether every function-typed value becomes the two-pointer form or
only a separate closure-value type does; who owns an escaping closure's
captures and when they are released; and whether a non-capturing function
keeps its single-pointer form across the C boundary.

## `Allocate` on a generic class

`implement<T> trait Allocate for class G<T>` type-checks, and then `new
G<i64>(77)` fails at link time: the allocator is never instantiated for
`G<i64>`, and the linker reports the missing symbol.  It is refused late,
not miscompiled (ruling 292: left for after the merge).  It may become
moot with the class removal.  Whether a generic struct's `Allocate` fails
the same way has not been measured.

## Moving a value out from behind a reference

A field moved out of a place reached through a reference or a pointer -
`mut old: Body = this.body;`, `match (this.opt)` by value, a generic getter
returning a `T` field, an array field read through `node.fields` - copies an
owning handle the place still holds, and the move checker lets it through:
the borrowed storage is someone else's to release, and nothing records
whether they will.  Only the destructor's own receiver is refused today
(`E0453`; ruling 309: moving a field of `this` out inside `drop` is refused,
full move checking waits for after the merge), because field glue certainly
releases it again.

Full move checking refuses every such move, and needs a way to write the
legitimate ones: a take that leaves a replacement behind (`mem::swap` does it
today, in two statements) and a release of what an assignment through a
reference overwrites, which today is not released at all.  The take-and-
replace idiom in the standard library's `set_body`, the editor's
`take_pending`, and `tests/tests/lang/move_field_out_ok.cryo`, which asserts
the idiom, are the first sites it reaches.

Open: whether a raw-pointer root is in scope (the compiler's own AST cloner
moves array fields through node pointers), and the spelling of a take.

## `mem::replace` and `Option::take`

The take that full move checking needs, as one call instead of a
`mem::swap` into a temporary followed by `mem::drop`:
`mem::replace(&place, value)` returns what the place held and leaves
`value` behind, and `Option::take(mut &this)` returns the option's value
and leaves `None`.  The standard library's `Child::close_stdin`, its
request/response `set_body` and the editor's `take_pending` are the first
callers.

Open: whether `Option::take` is enough on its own, or every owning type
needs a take with a default left behind.

## W0015 becomes an error

An explicit destructor call, `x.drop()`, is a warning today, and the
compiler, the standard library and the editor have none left.  The
warning's own text says it will become an error; then `mem::drop(x)` is
the only way to release a value early.  An ordinary method that happens to
be named `drop` on a type with no `Drop` is not a destructor call, and is
told apart before the warning becomes an error.

Open: whether `ptr::drop_in_place` stays the spelling for releasing a
value through a raw pointer.

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
