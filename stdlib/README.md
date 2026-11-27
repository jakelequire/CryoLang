# The Cryo standard library

Written in Cryo, shipped with the compiler as its sources and a prebuilt
archive. The language specification's chapter 19 (`docs/cryo.md`) describes
it module by module; `docs/stdlib-api.txt` lists every declaration, one line
each.

## Where things are

| module | what it holds |
|---|---|
| `core` | `Option`, `Result`, the error trait, comparison, cloning, conversion, hashing, iterators, operators, slices, pointers, the primitives' methods; no allocation, no I/O |
| `alloc` | allocators, `Box`, `Rc`, `Arc`, arenas and pools |
| `collections` | `Array`, `HashMap`, `HashSet`, the B-tree map and set, `Deque`, `BinaryHeap`, `Str` (a borrowed view of text) and `String` (owned text) |
| `fmt` | `Display`, `Debug`, f-strings, `print` / `println` / `eprint` / `eprintln` |
| `io`, `fs`, `env`, `process` | readers and writers, files and paths, the environment and arguments, child processes |
| `time`, `sync`, `thread`, `future` | durations and clocks, locks and atomics and channels, threads, the async executor |
| `net` | addresses, DNS, TCP and UDP sockets; HTTP/1.1, HTTP/2, WebSocket and TLS |
| `encoding`, `hash`, `random`, `math`, `log`, `test` | the JSON, YAML and TOML value model and the byte codecs; SHA-1 and SHA-256; random numbers; float functions; logging; the test framework |
| `ffi`, `sys` | the C library, OpenSSL and raw system calls |

The prelude, imported into every file, brings in `Option`, `Result`, `Error`,
`Array`, `String`, `Str`, `Box`, `Rc` and the core traits.

## Stability

What 1.0 promises, by tier (chapter 19.5 of the specification is the
normative text):

- **Stable**: everything not named below. Networking's foundations are
  stable: the address types, DNS and the TCP and UDP sockets.
- **Preview**: `net::http`, `net::http2`, `net::ws` and `net::tls`. They may
  change shape in a 1.x release.
- **Unstable**: `ffi` and `sys`, which bind the platform's interface as it is.

A standard-library enum may gain variants (`Option`, `Result`, `Ordering` and
`Bound` excepted), so a `match` over one keeps a `_` arm. `printf`, `eprintf`
and `format` are the legacy text path: supported, and an f-string is the
checked one.

## Text

Text is `Str` (a view) and `String` (owned). Their methods take the text they
look for as a `Str`, so a literal is passed as it is:
`line.starts_with("#")`, `text.replace("\\", "/")`. A `String` is passed as
its view, `name.as_str()`. The raw `string` type is a NUL-terminated C
string; `Str::new(text)` reads one.

Output goes through the print family with an f-string,
`fmt::println(f"{name}: {count}")`, and a `Str` is matched against string
patterns directly: `match (command) { "start" | "go" => ..., _ => ... }`.
The `examples/` directory at the repository root is written this way
throughout.
