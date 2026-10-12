# Shadow-corpus and IR-diff scripts

The claim "this change moved no compiled output" is made with `make verify
ARGS="--baseline HEAD --require-identical"` (see `scripts/verify.py`).  These
scripts answer the questions it does not:

Outputs go to `$OBJCMP_OUT` (default `<repo>/.objcmp`, gitignored).

* `irdiff.sh` - `--emit-llvm` for the unit suite under HEAD and the tree,
  to name the call that moved an object.
* `corpus2.sh <tag>` - the SIX-half shadow corpus: LSP built directly,
  unit suite, every project built one by one, every `collect` project's
  own `tests/` via `cryo test`, the examples, the compile-fail suite run
  as the runner runs it. Every half exists because a runner swallowed a
  child's stderr once.
* `lsp-only.sh <tag>` / `projex.sh <tag>` - single halves.
