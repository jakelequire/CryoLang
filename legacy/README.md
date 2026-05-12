# `legacy/`

Code in this directory is **frozen**. It is kept for historical reference
only. Do not modify, extend, refactor, or fix bugs here.

## What's in here

### `legacy/bootstrap/`

The original C++23 implementation of the Cryo compiler. It served as the
bootstrap for the now-shipping self-hosted compiler in `compiler/` and has
been fully retired.

- **It does not build.** No Makefile target produces it; it is not part of
  any current build path.
- **It has no effect on the modern compiler.** Behavior of the self-hosted
  compiler is not influenced by anything in this directory. Bugs that
  existed in the C++ implementation are not reasons to do anything in the
  modern toolchain.
- It is preserved purely so the project's history is inspectable.

The shipping compiler lives at `compiler/`, with the pinned binary at
`bin/cryo` and the stdlib at `stdlib/`.
