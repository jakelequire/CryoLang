# `legacy/`

Code in this directory is **frozen**. It is kept for historical reference and
as a fallback bootstrap path until a self-hosted `cryo` binary ships with the
repo. Do not extend, refactor, or fix non-blocking bugs here.

## What's in here

### `legacy/bootstrap/`

The original C++23 implementation of the Cryo compiler. It was used to
bootstrap the self-hosted compiler in `compiler/` and is no longer the
project's primary toolchain.

- It still builds: `make bootstrap` from the repo root.
- It still has two known harmless codegen quirks (dead `@FILE.str` globals,
  unmangled `@panic` calls in `std__prelude.o`) that bake into stage-3 IR but
  don't affect runtime. The 8-stage `make selfhost-check` chain handles them.
- **These quirks will not be fixed.** They go away when the bootstrap is
  retired.

## When does `legacy/bootstrap/` go away?

Once a known-good `cryo` binary is committed (or attached to a GitHub Release)
as the new starting point for builds, the bootstrap becomes unnecessary. At
that point this directory will either be deleted or moved out of the main
branch into a long-term archive.

Until then, it's the only way to build Cryo from source on a machine that
doesn't already have a `cryo` binary.
