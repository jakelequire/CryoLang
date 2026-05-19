# Contributing to Cryo

Thanks for your interest in Cryo. Releases follow semver from 1.0.0 onward;
expect ongoing development on top of a stable surface.

## Repository layout

| Directory | Status |
|---|---|
| `compiler/` | Self-hosted compiler (written in Cryo). **Active.** |
| `stdlib/` | Standard library (written in Cryo). **Active.** |
| `tools/CryoLSP/` | Language Server Protocol implementation. Built via `make lsp`. **Active.** |
| `tools/CryoAnalyzer/` | VS Code extension front-end for `cryolsp`. Built and installed via `make install-lsp`. **Active.** |
| `tools/CryoFormat/` | Exploratory formatter; not built by default. |
| `legacy/bootstrap/` | Retired C++23 bootstrap, kept in the tree for historical reference only. **Do not modify.** It will not build against the current language. |

## Building

```bash
make cryo                # ~15 s; builds the self-hosted compiler via bin/cryo
make selfhost-check      # ~50 s; 6-stage byte-identity gate (3 rounds)
make test                # runs the test suite via the freshly-built compiler
```

To run the resulting compiler from anywhere on your `PATH`, run
`./install.sh` after `make cryo`.

### Pinned binary (`bin/cryo`)

`bin/cryo` is a known-good self-hosted compiler committed to the repo. Every
build target — `make cryo`, `make selfhost-check`, `make test` — drives off
this pin. There is no longer a path back to a C++ bootstrap: the compiler
is the pin, and the pin is the compiler.

The pin is **stale** by design — it understands the dialect of `compiler/src/`
at the moment it was committed, nothing more. When `compiler/src/` adopts new
parser syntax or codegen behaviour that the pinned binary can't represent,
`make cryo` errors out and you'll need to refresh the pin (or roll back).

To refresh the pin:

```bash
make cryo                # build the next-generation compiler via the current pin
make pin-cryo            # copy compiler/build/bin/cryo to bin/cryo, stripped
git add bin/cryo
git commit -m "build: refresh pinned cryo binary"
```

Refresh **only** when `compiler/src/` has actually adopted something the
existing pin can't handle. Do not refresh just because a new build exists —
the pin is for compatibility, not freshness.

## Filing issues

Use the GitHub issue tracker. The bug-report template asks for:

- `cryo --version` output.
- Your OS and the LLVM/Clang version you have installed.
- The smallest reproducer you can produce.

## Submitting changes

1. Fork the repo and branch off `main`.
2. Make your change. **Don't touch `legacy/bootstrap/`** — it's retired.
3. Run `make selfhost-check` locally — your change must preserve the
   stage-3 / stage-4 byte-identical fixed point.
4. Open a PR using the provided template; explain *why* in the description.

## Style

A few conventions worth knowing:

- Prefer fixes at the root cause over workarounds.
- Don't add backwards-compatibility shims for code that's never shipped.
- If a change in `compiler/` exposes a bug in the pinned binary
  (`bin/cryo` rejects or miscompiles new source), work around it in
  `compiler/` or `stdlib/` until you can refresh the pin from the
  fixed compiler in a follow-up.
- New compiler-side helpers belong in `CodegenContext` /
  `DeclarationIndex` / `InternTable` — not as inline string manipulation
  inside codegen.

## Known limitations

- Stdlib resolution looks at `$CRYO_HOME/stdlib` (set by `install.sh`) or
  the `stdlib_root` key in `cryoconfig`, with `<repo>/stdlib` as the
  in-tree fallback. There is no system package path.
- Async / await / coroutines parse but do not lower.
- No package registry. Dependencies resolve via git URL with a lockfile
  and content-addressed cache.
- No cross-compilation; the host toolchain is the target.
- `process::Command` is POSIX-only.
- `tools/CryoFormat` is exploratory and not built by default. The shipped
  tooling is `tools/CryoLSP` (the LSP server, `make lsp`) and
  `tools/CryoAnalyzer` (the VS Code extension, `make install-lsp`).
