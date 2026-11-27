# Cryo Examples

A curated tour of the Cryo language and standard library. Each
example is a self-contained project - `cryo run` from its directory
builds and executes it.

| #  | Example                                | What it covers                                                                |
| -- | -------------------------------------- | ----------------------------------------------------------------------------- |
| 01 | [01-hello](01-hello/)                  | Hello world - minimal project layout, `fmt::println`.                         |
| 02 | [02-fizzbuzz](02-fizzbuzz/)            | Control flow + enums + `match`; f-strings.                                    |
| 03 | [03-fibonacci](03-fibonacci/)          | Recursion and iteration; reading CLI args via `env::args()`.                  |
| 04 | [04-calculator](04-calculator/)        | Structs, methods, enum-typed errors, `?`, RPN expression evaluation.          |
| 05 | [05-todo-cli](05-todo-cli/)            | `Array<T>` of owned structs, `Option`-based lookup.                           |
| 06 | [06-word-count](06-word-count/)        | File I/O (`File::read_bytes`), byte scanning, `HashMap` keyed top-N, `sort_by`. |
| 07 | [07-shapes](07-shapes/)                | Traits, multiple impls, generics with `where` bounds, float formatting.      |
| 08 | [08-game-of-life](08-game-of-life/)    | 2D simulation on a torus, ANSI terminal output, `ArgumentParser`, `match` on a `Str`. |
| 09 | [09-json-config](09-json-config/)      | `std::encoding::json` parsing into typed structs with a derived `FromValue`.  |
| 10 | [10-expr-interpreter](10-expr-interpreter/) | Lexer + recursive-descent parser + evaluator over arithmetic.            |
| 11 | [11-http-server](11-http-server/)      | `std::net::http` Router, four routes (a preview module).                      |
| 12 | [12-guessing-game](12-guessing-game/)  | Stdin reads (`stdin().line()`), `random::range`.                              |
| 13 | [13-closures](13-closures/)            | Capturing closures over `Copy` values, passed through `(Args) -> Ret` slots. |
| 14 | [14-threads](14-threads/)              | Native threads: `thread::spawn`/`join` (move data in & out), `sync::channel` channels, scoped fork-join over a borrowed atomic. |
| 15 | [15-files](15-files/)                  | `File::read_text` and friends, `BufferedReader.lines()`, directories, `TemporaryDirectory`, errors naming their path. |
| 16 | [16-echo-server](16-echo-server/)      | `async main`, `TcpListener::bind` + `await accept()`, `TcpStream::connect`, `read_some`/`write_all`, two tasks joined by `Futures::join`, one `BoxedError`. |
| 17 | [17-http-get](17-http-get/)            | `Client::new()` + `await client.get(url)` (HTTP or HTTPS), `Url::parse`, `[link] system = ["ssl", "crypto"]`; reaches the network. |

## Running

From within an example directory:

```sh
cryo run                  # build + execute
cryo run -- arg1 arg2     # forward args to the built binary
cryo build                # build only (artifact lands at build/<project-name>)
```

The first build pulls in everything the example imports out of the
standard library. Subsequent runs reuse cached object files under
`build/target/<profile>/`. Delete `build/` to force a clean rebuild.

## Language tour, in order

Reading the examples in numeric order doubles as a guided tour of
the language. The first few cover only what's in the prelude plus
`std::fmt`. By example 5, you've seen structs, enums, generics,
traits, `Array<T>`, `String` and `Str`. The later examples layer in
I/O, parsing, threads and a real network server.

## Notable patterns

* **Project layout.** Every example uses the standard `cryoconfig` +
  `src/main.cryo` shape. The fields you can set are documented at
  `docs/cryo.md`.
* **Text.** Output is `fmt::println(f"total: {count}")`: an f-string
  formats each hole through `Display` (`{x:?}` through `Debug`), with
  a format spec after a colon (`{name:<30}`, `{area:.4}`). A string
  literal is written wherever a `Str` is declared - an argument, a
  binding, a return - with no conversion call. `printf` is the legacy
  C-format path and the examples do not use it.
* **Ownership.** Anything that owns heap memory (`String`, `Array<T>`,
  `HashMap<K, V>`, `Box<T>`, ...) is released exactly once, when the
  binding that owns it goes out of scope; moving it into a field, an
  array or a thread hands that release over. No example releases a
  value by hand.
* **Error handling.** `Option<T>` for absent values, `Result<T, E>`
  for fallible operations with a typed reason, `?` to pass an error
  on, and `match (op()) { Result::Ok(...) => ..., Result::Err(...) => ... }`
  where the example handles it.

## Adding a new example

1. `cryo init <name>` from this directory (or copy an existing
   example's layout).
2. Set `project_name` in `cryoconfig` to match the directory.
3. Add an entry to the table above.
