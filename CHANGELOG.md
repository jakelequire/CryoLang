# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2026-05-25

The first stable release. The compiler is self-hosted, the standard library
is written entirely in Cryo, and the public surface is frozen under semver.

### Compiler

- **Self-hosted compiler** with an LLVM 20 backend. Every release is built
  by the previous release; `make selfhost-check` runs a 3-round (6-stage)
  byte-identical IR check that gates every push to `main`.
- **Type system:** static, monomorphic generics with `where` bounds, no
  implicit conversions, no inference on variable bindings. `typeof(expr)`
  in type position.
- **Traits:** trait declarations with default methods; impls on primitives,
  structs, classes, and enums; same-file coherence checks.
- **Classes:** single-inheritance with virtual dispatch, destructors,
  `protected`/`private` visibility, `override` and `virtual` modifiers.
- **Enums:** algebraic enums with explicit discriminants and discriminant
  base type (`type enum X : u8 { … }`), exhaustive match enforcement on
  enum subjects.
- **Pattern matching:** literal, identifier, wildcard, enum-destructure,
  range patterns (`a..b` and the explicit `a..=b`; both are inclusive in
  pattern position), and or-patterns (`a | b | c`).
- **Ownership:**
  - `Copy` and `Drop` traits with automatic recursive drop glue through
    struct fields, enum payloads, and container elements.
  - Fatal use-after-move (E0452), partial-move out of owning aggregate
    (E0453), and loop-carried move detection.
  - `![sink]` attribute for methods that consume the receiver.
- **Layout directives:** `![repr(...)]` and `![align(N)]` honored per
  generic instantiation.
- **ABI:** SysV-amd64 lowering with eightbyte classification, DirectPair,
  `sret`, `byval`, and `<2 x float>` packing for both extern-C and
  Cryo-internal calls.
- **FFI:** `extern "C"` functions, variadic functions via `VaArgs::new(...)`,
  the `![link]` directive for extern symbols.
- **Operators:** `?` error propagation, `|>` pipeline, `??` null-coalescing,
  `T?` optional-type sugar (desugars to `Option<T>`), `a..b` / `a..=b`
  range expressions (desugar to `Range::new` / `RangeInclusive::new`;
  valid in any expression position, looser than arithmetic).
- **`for (x in iter)` iteration:** the parser desugars to
  `loop { match (iter.next()) { Some(x) => { ... } None => break; } }`,
  evaluating the scrutinee exactly once. The scrutinee may be any
  `Iterator` (stdlib `Range<T>` / `RangeInclusive<T>` or any
  `implement trait Iterator<T>`), a range literal (`for (i in 0..n)`),
  an iterable exposing `iter()` (`Array<T>`, `Slice<T>`), or a
  fixed-size array `T[N]` (viewed as a `Slice<T>`).
- **Lambdas and closures:** `(params) -> Ret { body }` function literals.
  Non-capturing lambdas compile to anonymous function pointers; capturing
  lambdas over `Copy` bindings (i32/u64/bool/char/references and any
  `![derive(Copy)]` type) compile to a synthesised anonymous closure
  struct with a `__call__` method. Both forms call directly with zero
  overhead. Closures bound to a `(Args) -> Ret`-typed parameter are
  delivered via per-call-site receiver specialisation, so the body
  dispatches through `__call__` without an indirect call. Generic
  type-parameter inference from function-typed arguments
  (`opt.map(lambda)` infers `U`).
- **Modules:** `_module.cryo` aggregators, `public`/`private`/`protected`
  visibility, import cycle detection.

### Standard library

- **`alloc`:** `Allocator` trait, `GlobalAlloc`, `Arena`, `Pool`, `Box<T>`,
  `Rc<T>`, `Arc<T>` (all allocator-generic).
- **`collections`:** `Array<T>`, `String`, `HashMap<K,V>`, `HashSet<T>`,
  `Pair<A,B>`, `Slice<T>`, `Str`.
- **`core`:** `Copy`/`Drop`/`Clone`/`Eq`/`Ord`/`Hash`/`Default`/`From`/`Into`/
  `TryFrom`/`TryInto`/`Display`/`Debug`/`FmtWrite`/`Iterator`/`Error`
  traits; `Option<T>` and `Result<T,E>`.
- **`io`:** `Read`/`Write` traits with default `read_all`/`read_byte`/
  `read_line`/`read_to_end`/`write_all`; `Stdin`/`Stdout`/`Stderr`/
  `BufReader<R>`/`BufWriter<W>`/`LineWriter<W>`.
- **`fs`:** file `read`/`write`/`open`/`create`/`seek` over `Path`/`PathBuf`.
- **`net`:** `TcpStream`, `TcpListener`, `IpAddr`/`IpV4Addr`/`IpV6Addr`,
  `SocketAddr`. HTTP/1.1 server with keep-alive and read timeouts, HTTP
  client, router with route registration.
- **`json`:** parser and serializer for `JsonValue`/`JsonObject`/`JsonNumber`;
  round-trip clean.
- **`process`:** `Command`/`Child`/`ExitStatus` via `fork + execve`;
  `spawn`/`status`/`output`/`wait`/`try_wait`/`kill`/`send_signal`.
- **`env`:** `args()`, `var`, `set_var`, `remove_var`, `process_exit`.
- **`sync`:** atomics (`AtomicU8`/`U32`/`U64`/`I32`/`I64`/`Bool`,
  `MemoryOrder`, `fence`, `compiler_fence`), `Mutex<T>`, `RwLock<T>`,
  `CondVar`, `Once`, `Barrier`. `Send` / `Sync` auto-derive with
  call-site enforcement.
- **`thread`:** `ThreadLocal<T>` via `pthread_key`. (`thread::spawn`,
  `JoinHandle`, `Builder` are post-1.0.)
- **`math`:** `sqrt`, `pow`, `sin`/`cos`/`tan`, `ln`/`log2`/`log10`/`exp`,
  `abs_i32`/`abs_i64`/`abs_f64`, `is_nan`/`is_inf`/`is_finite`, `hypot`.
- **`fmt`:** `Display`/`Debug`/`FmtWrite` traits, `Formatter<W>`,
  `print`/`println`/`eprint`/`eprintln`, `format_to_string`,
  `format_debug_to_string`, floating-point formatting.
- **`test`:** the `cryo test` framework - `![test]`, `![ignore]`,
  `![should_panic]`, `expect_*` assertion helpers.
- **`ffi`:** `CStr`/`CString`/`NulError`; `libc` bindings (~2k LoC);
  syscall layer (~2k LoC).

### Tooling

- **`cryo` CLI** subcommands: `build`, `run`, `test`, `check`, `init`,
  `fetch`, `update`, `raw`, `demangle`, `version`, `help`.
- **`cryoconfig` package format** with `[project]`, `[compiler]`,
  `[dependencies]`, optional `[[bin]]` and `[lib]` sections. Git-backed
  dependencies (`git = "..."` with `version`/`tag`/`branch`/`rev`),
  `cryoconfig.lock` lockfile written by `cryo fetch`, content-addressed
  cache under `$CRYO_HOME` / `$XDG_CACHE_HOME` / `$HOME/.cache`.
- **Language Server (`bin/cryolsp`):** hover, go-to-definition, completion
  (member + scope-resolution with trigger characters `.` and `:`),
  semantic tokens, code actions, code lenses, push diagnostics.
- **CryoAnalyzer VS Code extension:** custom `cryo-diagnostic:` virtual
  document scheme with themed rendering, code-lens/code-action wiring,
  LSP server auto-discovery.
- **CI:** `make cryo` + smoke-test + `make test` on every PR and push;
  `make selfhost-check` on push-to-main.

### Diagnostics

- 102 active error codes (E0001–E0900 range) with source-span underlines,
  fix suggestions, and machine-applicable quickfixes where applicable.
- ANSI color rendering respecting `NO_COLOR`/`FORCE_COLOR`/`CLICOLOR_FORCE`
  with `isatty(2)` fallback.
- Quickfix system covering literal coercions (E0218/E0200), parser
  punctuator misses (E0100), undefined-type "did you mean" suggestions
  (E0203), and use-after-move move-site anchors (E0452).

### Examples

Thirteen worked examples under `examples/`, covering hello-world,
fizzbuzz, recursion, structs and methods, `Array<T>` ownership, file
I/O and `HashMap`, traits with `where` bounds, 2D simulation,
`std::json` parsing, recursive-descent parsing, an HTTP server,
stdin-driven interactive I/O, and capturing closures.

### Known limitations (deferred to post-1.0)

These are not bugs against 1.0 - the language deliberately ships
without them and the grammar reserves the relevant syntax. See
[`docs/cryo.md` § 21](./docs/cryo.md#21-reserved-syntax) for the
authoritative list.

- Iterator combinators are partial. `.take(n)` ships as a lazy
  `Iterator` default returning a `TakeIter` adapter, and works when the
  iterator is a named local consumed directly - e.g. `r.take(n).count()`,
  `r.take(n).fold(...)`, or `for (x in r.take(n)) { ... }`. Not yet
  supported: binding the adapter to an explicitly-typed local
  (`mut t: TakeIter<..> = r.take(n)`), a call-expression receiver
  (`make_range().take(n)`), and chaining adapters (`r.take(a).take(b)` and,
  by extension, `.map`/`.filter`/`.zip`/`.chain`/`.enumerate`/`.collect`,
  which are not yet implemented). The `Iterator` trait otherwise ships with
  `next`/`count`/`fold`/`for_each`; `for (x in iter)` iteration and range
  *expressions* (`a..b` / `a..=b`, see Compiler above) ship in 1.0 and work
  against any of these.
- Pattern guard clauses (`x if cond =>`).
- Nested patterns in `match`.
- `Display`/`Debug` impls for container and ADT types
  (`Option`/`Result`/`Array`/`HashMap` print via field access, not via
  `println("{}", x)`).
- Async / await / coroutines.
- `thread::spawn` / `JoinHandle` / `Builder` and `mpsc` channels. (The
  primitives under `std::sync` and `Arc<T>` ship in 1.0; what's missing
  is the way to start a second thread from Cryo source.)
- `time::Instant` / `Duration` / `sleep`; `random` module.
- Filesystem operations beyond read/write/open (`remove_file`,
  `create_dir`, `read_dir`, `metadata`, `rename`, `exists`, `copy`).
- TLS, UDP, HTTP/2, WebSocket in `net`.
- Macros / user-defined `![attr]` directives.
- Cross-compilation.
- Cross-module trait-impl coherence checks (same-file only in 1.0).
  Relatedly, two distinct user types that share the same *leaf* name in
  different modules (e.g. `ModA::Widget` and `ModB::Widget`) are
  disambiguated correctly as types, but trait-method dispatch on them
  resolves through the bare leaf name and can bind to the
  first-registered type's impl. Give such types distinct names until
  fully-qualified trait dispatch lands post-1.0.

[1.0.0]: https://github.com/jakelequire/CryoLang/releases/tag/v1.0.0
