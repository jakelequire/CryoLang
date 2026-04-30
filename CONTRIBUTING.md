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
make cryo                # ~5 min cold; produces compiler/build/bin/cryo
make selfhost-check      # ~3-10 min; full 8-stage byte-identity gate
```

To run the resulting compiler from anywhere on your `PATH`, run
`./install.sh` after `make cryo`.

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
