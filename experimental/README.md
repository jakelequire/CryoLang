# `experimental/`

Code in this directory is **not built by default**. It exists in the tree
for visibility and version control, but is not on any release path and is
not guaranteed to compile against the current `compiler/`.

## What's in here

### `experimental/stdlib-next/`

A second-generation rewrite of the standard library, parked while the
compiler grows the features it depends on. When `experimental/stdlib-next/`
can compile cleanly under the current `cryo` compiler, it will replace the
top-level `stdlib/` directory and the current `stdlib/` will be archived
under `legacy/stdlib/`.

For now, **only `stdlib/` ships**. Don't try to build, link, or import
from `experimental/stdlib-next/` in production code.
