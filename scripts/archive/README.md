# Applied one-off scripts

Scripts that produced a change already landed, kept because a change made by
a script is reproducible only while the script exists somewhere git can see.
Nothing here is a gate: no Makefile target, CI job, hook or section-0 row runs
any of it.

`docs/name-resolution.md` cites these by their old paths under `scripts/`;
`git log --follow scripts/archive/imports/<name>` shows the move.

| directory | scripts | what they did |
|---|---|---|
| `imports/` | `migrate-plain-imports.py`, `qualify-imports.py`, `migrate-glob-imports.py`, `restore-plain-imports.py`, `migrate-imports-from-errors.py`, `migrate-base-class-imports.py` | the import-syntax migrations: plain imports to braced or qualified ones, driven by source scans and by compiler error output |

They edit the tree they are run against, so run them from a checkout of the
commit they were written for. `qualify-imports.py` loads
`migrate-plain-imports.py` from its own directory; the rest take
repo-relative paths and run from the repository root.
