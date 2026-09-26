# Object-comparison and shadow-corpus scripts

**`make verify ARGS="--baseline HEAD --require-identical"` supersedes
`objcmp.sh`, `hash-tree.sh`, `ex-hash.sh` and `tests-hash.sh`** for the claim
"this change moved no compiled output": it hashes the same two populations,
from the census and examples runs that compiled them rather than from a
second suite run, and builds the baseline compiler in a clone under
`.verify/` instead of stashing `compiler/src` (see `scripts/verify.py`). Its
lists match these scripts' byte for byte for the same compiler. These stay
because the ledger's evidence was taken with them; `corpus2.sh` and its
halves remain the only shadow-corpus instrument.

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
