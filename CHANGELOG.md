# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).


## [1.0.0] - 2026-06-24

The first stable release. The compiler is self-hosted, the standard library
is written entirely in Cryo, and the public surface is frozen under semver.

### Compiler

- **Self-hosted compiler** with an LLVM 20 backend. Every release is built
  by the previous release; `make selfhost-check` runs a 3-round (6-stage)
  byte-identical IR check that gates every push to `main`.
- **Type system:** static, monomorphic generics with `where` bounds, no
  implicit conversions. Local type inference on `const`/`mut` bindings: a
  binding adopts its initializer's concrete type, so an explicit `: T` is
  required only when there is no initializer to infer from (or to widen the
  declared type). `typeof(expr)` in type position.
- **Traits:** trait declarations with default methods; impls on primitives,
  structs, classes, and enums; coherence checks (a trait may be implemented
  at most once per type) enforced uniformly within a file and across
  modules, keyed on the import-resolved impl head so generic traits
  (`From<i8>` vs `From<i16>`), generic targets, and `where`-bounds are all
  distinguished.
- **Associated types.** A trait may declare an associated type
  (`type Item;`) and refer to it by projection (`This::Item` inside the trait,
  `I::Item` off a generic parameter). Each impl binds it positionally
  (`implement trait Iterator<i32> for X` - sugar for `Iterator<Item = i32>`,
  available when the trait has no generic params of its own) or with the
  explicit body form (`type Item = i32;`). Two diagnostics enforce the binding
  rules: `E0309` (a declared associated type left unbound by an impl) and
  `E0310` (an associated type bound positionally on a trait that also has
  generic params - use the explicit body form `type Out = ...;` instead).
  Declaration-site bounds on an associated type (`type Item: Copy;`) are
  enforced against each impl's concrete binding (`E0306`). Opaque `implement Iterator<T>`
  bindings now cross-check `<T>` against the iterator's actual `Item` (`E0200`
  on a category-level mismatch). See [`docs/cryo.md` section 11.5](./docs/cryo.md#115-associated-types).
- **Classes:** single-inheritance with virtual dispatch, destructors,
  `protected`/`private` visibility, `override` and `virtual` modifiers.
- **Enums:** algebraic enums with explicit discriminants and discriminant
  base type (`type enum X : u8 { ... }`), exhaustive match enforcement on
  enum subjects.
- **Pattern matching:** literal, identifier-binding, wildcard,
  enum-destructure (including nested sub-patterns such as `Some(Some(n))`
  and `Branch(Leaf(x), r)`, with discrimination on literal payloads like
  `Some(5)`), range patterns (`a..b` and the explicit `a..=b`; both are
  inclusive in pattern position), or-patterns (`a | b | c`), and guard
  clauses (`pattern if (cond) => ...`, checked after the pattern matches
  with a false guard falling through to the next arm). Exhaustiveness
  accounts for nested coverage (`Wrap(A)` + `Wrap(B(n))` covers `Wrap`).
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
- **f-strings (`f"...{expr}...{expr:?}..."`):** string interpolation. The
  parser desugars each f-string into a chain of `std::fmt::interp` builder
  calls that produce an owned `String`; `{expr}` formats the value through
  `Display` and `{expr:?}` through `Debug` (so it works for any type that
  implements them, including `Option`/`Result`/`Array`). Embedded
  expressions are full expressions (`f"{a + b}"`); `{{` / `}}` are literal
  braces. `std::fmt::interp` is auto-imported into any module that uses an
  f-string. The printf-style `print`/`println` (C `%d`/`%s` specifiers,
  variadic, not type-checked) remain available for raw formatted output.
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
  dispatches through `__call__` without an indirect call; that
  specialisation is wired for non-generic free functions only, so a
  *capturing* closure passed to a generic function, a method, or a
  scope-resolution call is E0458 (non-capturing lambdas are function
  pointers and bind anywhere). Generic
  type-parameter inference from function-typed arguments
  (`opt.map(lambda)` infers `U`).
- **Modules:** `_module.cryo` aggregators, `public`/`private`/`protected`
  visibility, import cycle detection.

### Build system

- **New build-directory layout.** The final artifact is **hoisted** to the
  **root** of the build directory - `build/<name>` for an executable,
  `build/lib<name>.a` for a library - so it runs as `build/<name>` regardless of
  profile. All intermediates live under a visible, per-profile cache tree
  (`build/target/<profile>/`) grouped by **package origin**: the standard
  library (`std/`), the local project (`local/`), and one subtree per
  third-party dependency (`<depname>/`), each holding its own `deps/*.o` and
  `ir/*.ll`. The combined IR dump is `build/<name>.ll`. *(Breaking for tooling
  that hardcoded `build/bin/`, `build/obj/`, or `build/.cryo/`.)*
- **Build manifest.** A successful build writes a per-profile
  `build/target/<profile>/build-manifest.json` (schema v2) recording the
  profile, target type/triple, optimization level, debug-info and emit-llvm
  flags, the linked stdlib archive, the `[link]` lists, every source module
  (namespace + path + origin/kind + object + size), and the input fingerprint.
- **Build profiles.** `release` (O2, no debug info; the default) and `debug`
  (O0 + DWARF), selectable with `--release` / `--dev` / `--profile=NAME`, or
  `[profile] default = "..."` in cryoconfig. Each profile keeps its own
  `build/target/<profile>/` cache. An explicit `[compiler] optimize` or
  `--opt-level=N` still overrides the profile's level; `-g` forces debug info.
- **Incremental builds.** `cryo build` now skips the entire compile+link when
  no input changed (sources, resolved knobs, the compiler binary, or the
  linked stdlib) and the artifact still exists, printing `<name> is up to date`.
  The fingerprint is content-based and keyed per target. `--no-incremental`
  forces a full rebuild. (Whole-build granularity is sound under Cryo's
  whole-program monomorphization; per-module reuse is future work, and the
  manifest already carries per-module hashes for it.)

### Standard library

- **`alloc`:** `Allocator` trait, `GlobalAlloc`, `Arena`, `Pool`, `Box<T>`,
  `Rc<T>`, `Arc<T>` (all allocator-generic).
- **`collections`:** `Array<T>`, `String`, `HashMap<K,V>`, `HashSet<T>`,
  `Pair<A,B>`, `Slice<T>`, `Str`.
- **`core`:** `Copy`/`Drop`/`Clone`/`Eq`/`Ord`/`Hash`/`Default`/`From`/`Into`/
  `TryFrom`/`TryInto`/`Display`/`Debug`/`FmtWrite`/`Iterator` traits;
  `Option<T>`, `Result<T,E>`, and the catch-all `Error` struct (there is no
  unifying `Error` *trait* — each module defines its own precise error type).
- **`core::iter`:** `Iterator` with an associated `type Item` (one required
  `next() -> Option<This::Item>`); the legacy generic-param form `Iterator<T>`
  remains accepted as positional sugar for `Iterator<Item = T>` in impls,
  `where I: Iterator<T>` bounds, and `implement Iterator<T>` opaque returns.
  Default consumers `count`/`fold`/`for_each`/`any`/`all`/`find` and lazy
  combinator
  adapters `.take(n)` / `.map(f)` / `.filter(pred)` / `.chain(other)` /
  `.enumerate()` / `.zip(other)` (returning `TakeIter` / `MapIter` /
  `FilterIter` / `ChainIter` / `EnumerateIter` / `ZipIter`); `f`/`pred` must
  be non-capturing, since the adapters are methods and a capturing closure
  may only be passed to a non-generic free function (E0458 - see
  [`docs/cryo.md` section 2.5](./docs/cryo.md#25-function-types) for the
  full boundary). `.enumerate()` yields `Pair<u64, Item>`,
  `.zip(other)` yields `Pair<Item, B>` and stops at the shorter side (`Pair` is
  the element type so the adapters stay monomorphization-friendly). Combinators
  chain freely (`r.take(n).map(f).filter(p)`, `r.chain(b).map(f)`,
  `a.zip(b).count()`, longer mixed chains) and feed `.count()` / `.fold(..)` /
  `for (x in ..)`. The adapters resolve on any concrete `Iterator` receiver,
  including a user struct that `implement`s `Iterator`
  (`mut c: Counter = ...; c.take(3).map(f)`), not just stdlib iterators; bind an
  adapter to a concrete-typed local (`mut z: ZipIter<.., ..> = a.zip(b)`) or
  chain on the expression directly when you need a named local.
  `collections::array::from_iter(it)` collects into an `Array<T>` (a free
  function, fixed by the expected `Array<T>` at the call site).
- **`io`:** `Read`/`Write` traits with default `read_all`/`read_byte`/
  `read_line`/`read_to_end`/`write_all`; `Stdin`/`Stdout`/`Stderr`/
  `BufReader<R>`/`BufWriter<W>`/`LineWriter<W>`.
- **`fs`:** files (`read`/`write`/`read_to_string`/`open`/`create`/`seek`/
  `copy`) and whole-path operations (`remove_file`, `rename`,
  `create_dir`/`create_dir_all`, `remove_dir`/`remove_dir_all`, `read_dir`,
  `canonicalize`) over `Path`/`PathBuf`; `metadata`/`symlink_metadata`/
  `exists`/`is_file`/`is_dir` backed by `stat`/`lstat`/`access`.
- **`net`:** `TcpStream`, `TcpListener`, `UdpSocket`, `IpAddr`/`IpV4Addr`/
  `IpV6Addr`, `SocketAddr`; `dns` name resolution; `tls` (OpenSSL) with
  `https` on top; `ws` (RFC 6455) over any `Read + Write` transport. HTTP/1.1
  server with keep-alive and read timeouts, HTTP client, router with route
  registration. HTTP/2 (`net::http2`): HPACK (RFC 7541), framing (RFC 7540),
  h2c client + server over any stream, generic over the transport.
- **`json`:** parser and serializer for `JsonValue`/`JsonObject`/`JsonNumber`;
  round-trip clean.
- **`process`:** `Command`/`Child`/`ExitStatus` via `fork + execve`;
  `spawn`/`status`/`output`/`wait`/`try_wait`/`kill`/`send_signal`.
- **`env`:** `args()`, `var`, `set_var`, `remove_var`, `process_exit`.
- **`sync`:** a generic `Atomic<T>` (`T` = `u8`/`u32`/`u64`/`i32`/`i64`/`boolean`,
  dispatched at compile time via `static match`), `MemoryOrder`, `fence`,
  `compiler_fence`, `Mutex<T>`, `RwLock<T>`,
  `CondVar`, `Once`, `Barrier`. `Send` / `Sync` auto-derive with
  call-site enforcement.
- **`thread`:** `ThreadLocal<T>` via `pthread_key`; OS threads via
  `spawn` / `try_spawn` / `JoinHandle<T>` (returns the body's value on
  `join`), `spawn_with_attr`, scoped threads (`thread::Scope`),
  `current` / `yield_now` / `sleep` / `sleep_ms`. Channels in
  `sync::mpsc` (`channel`, `Sender`, `Receiver`). `Builder` for
  configured spawns (`stack_size`, `name`; `spawn` / `try_spawn`).
- **`math`:** `square_root`, `cube_root`, `power`, `sine`/`cosine`/`tangent`,
  `natural_log`/`log_base2`/`log_base10`/`exponential`, `absolute` (f64)
  /`absolute_f32`, `abs_i32`/`abs_i64`, `is_nan`/`is_infinite`/`is_finite`,
  `hypot`.
- **`time`:** `Duration` (normalized seconds + sub-second nanos; built
  and read in seconds/millis/micros/nanos; `add`/`saturating_sub`,
  `Eq`/`Ord`), `Instant` (monotonic clock - `now`/`elapsed`/
  `duration_since`), `SystemTime` (wall clock - `now`/`duration_since_epoch`
  for a Unix timestamp), and `sleep(Duration)`. Differences saturate at
  zero.
- **`random`:** `Rng`, a fast non-cryptographic xoshiro256** generator
  seeded deterministically (`from_seed`) or from the OS (`from_os`):
  `next_u64`/`next_u32`/`next_bool`/`next_f64`, unbiased `below(bound)` /
  `range_u64(lo, hi)`, and `fill_bytes`. `secure_bytes(buf, len)` pulls
  cryptographically secure bytes from the kernel CSPRNG (`getrandom`).
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
  `make selfhost-check` (stage-3 == stage-4 byte-identity) on every PR
  and push as well.

### Diagnostics

- Over 200 defined error codes (in the E0000-E0999 range) with source-span
  underlines, fix suggestions, and machine-applicable quickfixes where applicable.
- ANSI color rendering respecting `NO_COLOR`/`FORCE_COLOR`/`CLICOLOR_FORCE`
  with `isatty(2)` fallback.
- Quickfix system covering literal coercions (E0218/E0200), parser
  punctuator misses (E0100), undefined-type "did you mean" suggestions
  (E0203), and use-after-move move-site anchors (E0452).

### Examples

Fourteen worked examples under `examples/`, covering hello-world,
fizzbuzz, recursion, structs and methods, `Array<T>` ownership, file
I/O and `HashMap`, traits with `where` bounds, 2D simulation,
`std::json` parsing, recursive-descent parsing, an HTTP server,
stdin-driven interactive I/O, capturing closures, and OS threads with
`thread::spawn` / `JoinHandle` / `mpsc` channels.

### Target triples

Verified end-to-end - compile, link, run, and a self-host byte-identity gate
(`make selfhost-check`) - on x86_64:

- **`x86_64-*-linux-gnu`** - native host builds; the canonical dev/CI host.
- **`x86_64-pc-windows-gnu`** - native Windows host builds *and*
  Linux->Windows cross-compilation via the mingw-w64 toolchain: Win64 ABI,
  COFF objects, `.exe` linking, and a native 6-stage self-host fixed point.

The host C toolchain (header preprocessor + linker) is auto-detected
(`clang-20` -> `clang` -> `gcc` -> `cc`) and the standard library is auto-located
relative to the `cryo` binary, so a stock toolchain needs neither `CRYO_CC`
nor `CRYO_STDLIB`.

### Known limitations (deferred to post-1.0)

These are not bugs against 1.0 - the language deliberately ships
without them and the grammar reserves the relevant syntax. See
[`docs/cryo.md` section 21](./docs/cryo.md#21-reserved-syntax) for the
authoritative list.

- Async / await / coroutines.
- Macros / user-defined `![attr]` directives.
- macOS / Darwin targets (no Mach-O backend or toolchain wiring yet). See
  **Target triples** above for the supported set.

[1.0.0]: https://github.com/jakelequire/CryoLang/releases/tag/v1.0.0
