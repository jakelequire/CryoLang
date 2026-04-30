# Contributing to Cryo

Thanks for your interest in Cryo. The project is in pre-0.1.0 development —
expect rough edges and ongoing churn.

## Repository layout

| Directory | Status |
|---|---|
| `compiler/` | Self-hosted compiler (written in Cryo). **Active.** |
| `stdlib/` | Standard library (written in Cryo). **Active.** |
| `legacy/bootstrap/` | Original C++23 bootstrap. **Frozen.** Don't extend or fix non-blocking issues here. |
| `experimental/stdlib-next/` | Parked stdlib rewrite. **Don't depend on this.** |
| `tools/` | Out-of-tree LSP / formatter / analyzer. Not built or shipped for 0.1. |

## Building

```bash
make cryo                # ~5 min cold; canonical path through bootstrap
make cryo-fast           # ~30 s; uses the pinned bin/cryo, skips bootstrap
make selfhost-check      # ~3-10 min; full 8-stage byte-identity gate
```

To run the resulting compiler from anywhere on your `PATH`, run
`./install.sh` after `make cryo`.

### Pinned binary (`bin/cryo`)

`bin/cryo` is a known-good self-hosted compiler committed to the repo. It's
the fast rung for `make cryo-fast` and skips the C++ bootstrap entirely.

The pin is **stale** by design — it understands the dialect of `compiler/src/`
at the moment it was committed, nothing more. When `compiler/src/` adopts
new parser syntax that the pinned binary can't read, you'll see parse errors
on `make cryo-fast`.

To refresh the pin:

```bash
make cryo                # build canonically through bootstrap
make pin-cryo            # copy compiler/build/bin/cryo to bin/cryo, stripped
git add bin/cryo
git commit -m "build: refresh pinned cryo binary"
```

Refresh **only** when compiler/src/ has actually adopted new syntax. Do not
refresh just because a new build exists — the pin is for syntax compatibility,
not freshness.

The canonical `make cryo` path through bootstrap remains the source of truth
and is what CI runs. `make selfhost-check` always uses bootstrap.

## Filing issues

Use the GitHub issue tracker. The bug-report template asks for:

- `cryo --version` output.
- Your OS and the LLVM/Clang version you have installed.
- The smallest reproducer you can produce.

## Submitting changes

1. Fork the repo and branch off `main`.
2. Make your change. **Don't touch `legacy/bootstrap/`** unless you're
   genuinely blocked on a 0.1.0 ship issue.
3. Run `make selfhost-check` locally — your change must preserve the
   stage-4 / stage-5 byte-identical fixed point.
4. Open a PR using the provided template; explain *why* in the description.

## Style

A few conventions worth knowing:

- Prefer fixes at the root cause over workarounds.
- Don't add backwards-compatibility shims for code that's never shipped.
- If a change in `compiler/` or `stdlib/` exposes a bootstrap bug, work
  around it in `compiler/` or `stdlib/` rather than touching the bootstrap.
- New compiler-side helpers belong in `CodegenContext` /
  `DeclarationIndex` / `InternTable` — not as inline string manipulation
  inside codegen.

## Known limitations (pre-0.1.0)

- The compiler hard-codes `<project_root>/../stdlib` for stdlib resolution.
  Your project must live as a sibling of `stdlib/` (typically inside this
  repo's tree). System-wide install isn't supported yet.
- No package manager.
- No cross-compilation.
- The LSP, formatter, and analyzer in `tools/` are not built or shipped.
