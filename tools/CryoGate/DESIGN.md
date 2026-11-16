# CryoGate: how the code is laid out

`cryogate` is the repository's gate runner: every gate a change must pass
before it merges is a `cryogate` subcommand. `PLAN.md` is the porting plan
(which Python gate becomes which subcommand, and in what order) and is
deleted with the last Python gate; this file is the architecture, and stays.

## The rule the layout serves

cryogate judges the compiler, so it must survive a compiler that does not
build, crashes or miscompiles. It is built by the **pin** against the tree's
stdlib, links nothing but the stdlib, and drives every compiler it judges as
a **child process**. Nothing in `src/` may import the compiler library.

## Layers

Each directory is one layer, with a `_module.cryo` aggregator whose doc
comment lists its files and what each owns. A layer imports only the
standard library and the layers **above it in this table**; nothing imports
upward, and no two files of one layer import each other in a cycle.

| layer | owns | may use |
|---|---|---|
| `core/` | child processes, the scheduler, the heavy-run lock, run logs, the command-line parser, host facts, git, directory listings, text helpers, the unit-suite judge | the stdlib |
| `corpus/` | the test corpus as read from disk and from a runner's output: the roster golden, `![target]` gates, discovery and merging (`roster`); a census run's reading and what the golden pins on this host (`census`) | core |
| `baseline/` | verify's baseline: what git lists, each program's record key, the baseline compiler's key, the clone it is built in, the bounded cache of homes | core |
| `verify/` | verify's decisions: the declaration files, object populations and owners, the runtime archives' members, a program's baseline record, the judges, the object comparison | core, corpus |
| `verify/run/` | a verify run itself: the children it starts, the baseline compiler, the baseline's per-program records, the declared projects' builds, the run from start to report | core, corpus, baseline, verify's decisions |
| `gates/` | one file per subcommand: its command line, the children it runs, the lines it prints, its exit code | every layer above; **never another gate** |
| `main.cryo`, `selftest.cryo`, `probe.cryo`, `heavy.cryo` | the entry point and the subcommands that are not gates (`selftest`, its child `probe`, `heavy`) | everything |

Inside `verify/` the files depend on each other in one direction:
declarations, objects, runtime, hashing, record, judges, comparison,
gates - each may use only those before it.  `verify/run/` comes after all
of them, and its files are ordered the same way: children, compiler,
programs, declared, session.  The decision files are pure and run no
compiler; `verify/run/` is where verify's children are started, so
`gates/verify.cryo` stays the command line only.

The test files under `tests/` import whichever layer they test.

## What goes where

- **A decision is not a gate.** A function that turns text, lists or files
  into a verdict or a list of problems belongs in a layer below `gates/`,
  pure where it can be (its inputs handed in), so a `![test]` can drive it
  without a compiler. A gate file reads its inputs, calls the decisions,
  runs children and prints.
- **Shared reading goes down a layer.** When a second gate needs what one
  gate reads (verify reads the census log and the roster as the census gate
  does), the reading moves into a layer both use - which is why `corpus/`
  exists - rather than one gate importing the other.
- **One concern per file.** A file that holds two concerns, or grows past
  a few hundred lines because it does, is split by concern, in a
  directory of its own with an aggregator once there are several files.
- **Visibility.** Every aggregator declares its modules `internal`:
  cryogate is an executable, and nothing outside its package may use its
  layers. Within a file, what other files call is `public` and documented;
  a helper only its own type calls is left private.
- **Doc comments.** Every file opens with a `///!` comment saying what it
  owns; every public type, field and function has a `///` comment.

## Adding a gate

1. Put the gate's decisions in the layer they belong to (or a new
   directory below `gates/`, with an aggregator, when no layer fits), with
   `![test]`s in `tests/`, each one proved able to fail.
2. Add `gates/<name>.cryo` with a `public type struct` whose
   `public static run(repository, logs, arguments, first) -> i32` parses
   the command line with `core::options`, runs children with `core::child`
   (or `core::schedule` for several at once), and prints the verdict line
   its Python predecessor printed, spelled the same.
3. List the file in `gates/_module.cryo`, and dispatch to it from
   `main.cryo`.
4. A gate that runs a heavy job takes the heavy-run lock itself
   (`core::lock`); a caller never writes the lock for it.
5. Wire the Makefile target to `$(CRYOGATE_COMMAND) <name> ...`, and
   delete the script it replaces in the same commit, once the port's
   side-by-side proof (PLAN.md section 9) is done.
