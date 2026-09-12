# Object-comparison and shadow-corpus scripts

The instruments behind every "0 of N objects moved" claim in
`docs/name-resolution.md` from §8.160 on. They measure `tests/` (2,126
objects on Windows) and `examples/` (1,126), the populations the deletions
were checked over - `scripts/obj-hash.sh` hashes the compiler, stdlib and
runtime, which none of them was.

Outputs go to `$OBJCMP_OUT` (default `<repo>/.objcmp`, gitignored).

* `objcmp.sh` - HEAD's compiler vs the working tree's, both populations:
  stashes `compiler/src`, builds, hashes, pops, builds, hashes, `comm -3`.
  Docs edits are safe while it runs; source edits are not.
* `hash-tree.sh <tag>` - hash the CURRENT compiler's output as `ex-<tag>.s`
  / `t-<tag>.txt`, for comparing against an earlier tag with `comm -3`.
* `ex-hash.sh <out>` / `tests-hash.sh <out>` - the two halves.
* `irdiff.sh` - `--emit-llvm` for the unit suite under HEAD and the tree,
  to name the call that moved an object.
* `corpus2.sh <tag>` - the SIX-half shadow corpus: LSP built directly,
  unit suite, every project built one by one, every `collect` project's
  own `tests/` via `cryo test`, the examples, the compile-fail suite run
  as the runner runs it. Every half exists because a runner swallowed a
  child's stderr once.
* `lsp-only.sh <tag>` / `projex.sh <tag>` - single halves.

No baseline is tracked yet: a claim is checked by running `objcmp.sh` (or
`hash-tree.sh` twice) across the commit in question.
