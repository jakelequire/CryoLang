# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] — 2026-05-11

The first stable release. Cryo self-hosts and the public surface is frozen
under semver from this point forward.

### Compiler

- Self-hosted compiler (`compiler/` written in Cryo). The original C++
  bootstrap in `legacy/bootstrap/` is retired; it remains in the tree
  for historical reference only and is no longer buildable from the
  Makefile.
- Three-round selfhost gate: stage-3 and stage-4 produce byte-identical
  LLVM IR (`make selfhost-check`).
- LLVM 20 backend via `clang++-20`.
- Pinned binary at `bin/cryo` so a fresh clone reproduces the canonical
  compiler immediately. Refresh via `make pin-cryo`.

### Language

- Traits with `where`-bound generics, monomorphisation, and inherent
  impls on primitives.
- Single-inheritance classes with virtual dispatch and destructors.
- Algebraic enums with exhaustive pattern matching (literal, identifier,
  wildcard, enum-destructure, range, or-patterns, guard clauses).
- Generic types and functions; type-arena-deduplicated `TypeRef`s.
- References (`&T`, `mut &T`), pointers (`T*`), arrays, slices, tuples,
  `Option<T>`, `Result<T, E>`.
- Move/copy classification driven by `Copy`/`Drop` discipline; auto-drop
  synthesis at scope exit and explicit `delete` for heap pointees.
- Module system with `_module.cryo` aggregators, prelude auto-import,
  `import` paths, and visibility (`public` / module-private / internal).
- Directives: `![inline]`, `![packed]`, `![test]`, `![ignore]`,
  `![should_panic]`, `![consumes_self]`, `![config(testing)]`.

### Standard library

Go-style "batteries included" surface; everything below is in the prelude
namespace and resolves through `_module.cryo` aggregators.

- **`core`** — `Option`, `Result`, `Slice`, `NonNull`, `Range`,
  `RangeInclusive`, `Ordering`. Traits: `Copy`, `Drop`, `Clone`,
  `Default`, `Eq`, `Ord`, `Hash`, `Iterator<Item>`, `IntoIterator`,
  `From`/`Into`/`TryFrom`/`TryInto`, `Step`. Memory utilities
  (`copy`, `zero`, `swap`, `transmute`, `align_up`/`align_down`).
  FNV-1a hashing.
- **`alloc`** — `Layout`, `Allocator` trait, `GlobalAlloc`, `Box<T>`,
  `Arena` (bump + reset), `Pool` (fixed-slot slab), `Rc<T>`.
- **`collections`** — `Array<T, A>`, `Str` (borrowed UTF-8 view),
  `String<A>` (owned UTF-8), `HashMap<K, V, A>`, `HashSet<T, A>`,
  `Pair`. Allocator-generic with `GlobalAlloc` default.
- **`io`** — `Read` / `Write` traits with rich defaults, `Stdin` /
  `Stdout` / `Stderr` with `is_tty` / `as_fd`, `BufWriter<W>`,
  `LineWriter<W>`, `BufReader<R>`, `IoError` / `IoErrorKind`.
- **`fmt`** — `Display`, `Debug`, `Formatter<W>`, `FmtWrite`,
  `print` / `println` / `eprint` / `eprintln`, `format_to_string`,
  heap-free integer and float writers.
- **`json`** — RFC 8259 parser and serializer; `JsonValue`,
  `JsonNumber`, `JsonObject` (insertion-ordered); `parse`,
  `stringify`, `stringify_pretty`.
- **`fs`** — `Path` (borrowed), `PathBuf` (owned), `OpenOptions`,
  `File` (`Read + Write`), convenience `read(path)` / `write(path, bytes)`.
- **`net`** — `IpV4Addr`, `IpV6Addr`, `IpAddr`, `SocketAddr`,
  `TcpStream`, `TcpListener`. HTTP/1.1 layer (`net::http`):
  `Method`, `StatusCode`, `Headers`, `Request`, `Response`, `Router`,
  `Client::get` / `post`, `HttpServer` with connection keep-alive,
  `Connection: close` opt-out, per-connection read timeouts.
- **`process`** — POSIX subprocess (`fork + execve`). `Command` builder
  (`arg`, `env`, `stdin`/`stdout`/`stderr`, `current_dir`), `Stdio`,
  `Child`, `ExitStatus`, `Signal`.
- **`ffi`** — `libc` (the single home for `extern "C"` declarations),
  `cstr` (`CStr`, `CString`, `NulError`).
- **`env`** — `args`, `var`, `set_var`, `remove_var`, `process_exit`.
- **`math`** — libm wrappers and constants (`PI`, `TAU`, `E`).
- **`test`** — built-in framework. Tests live in `<project>/tests/`,
  are marked `![test]`, run fork-per-test by `cryo test`. `expect`,
  `expect_eq`, `expect_ne`, `bail`, `bail_other`.

### CLI

- `cryo init` — scaffold a new project.
- `cryo build` — compile to executable, static library, or stdlib bundle.
- `cryo run` — build and run.
- `cryo test` — discover and run `![test]` functions in `tests/`.
- `cryo check` — frontend-only typecheck without codegen.
- `cryo project` — inspect resolved configuration.
- `cryo demangle` — decode Cryo's symbol mangling.
- `cryo fetch` / `cryo update` — git-backed dependencies with lockfile
  and content-addressed cache.
- `cryo raw` — direct single-file compile, no project.
- `cryo --version` reports the version compiled into the binary.

### Tooling

- `tools/CryoLSP` — Language Server Protocol implementation
  (`make lsp` builds `bin/cryolsp`).

### Known limitations

- Async / await / coroutines parse but do not lower.
- TLS, UDP, HTTP/2, WebSocket are out of scope for `net::http`.
- `process::Command` is POSIX-only; Windows is not yet supported.
- No package registry — dependencies resolve via git URL.
- No cross-compilation; the host toolchain is the target.
