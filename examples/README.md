# Cryo Examples

A curated tour of the Cryo language and standard library. Each
example is a self-contained project — `cryo run` from its directory
builds and executes it.

| #  | Example                                | What it covers                                                                |
| -- | -------------------------------------- | ----------------------------------------------------------------------------- |
| 01 | [01-hello](01-hello/)                  | Hello world — minimal project layout, `printf`.                               |
| 02 | [02-fizzbuzz](02-fizzbuzz/)            | Control flow + enums + `match`.                                               |
| 03 | [03-fibonacci](03-fibonacci/)          | Recursion and iteration; reading CLI args via `env::args()`.                  |
| 04 | [04-calculator](04-calculator/)        | Structs, methods, enum-typed errors, RPN expression evaluation.               |
| 05 | [05-todo-cli](05-todo-cli/)            | `Array<T>` ownership, `Drop` trait impls, `Option`-based lookup.              |
| 06 | [06-word-count](06-word-count/)        | File I/O (`fs::file::read`), byte scanning, `HashMap` keyed top-N.            |
| 07 | [07-shapes](07-shapes/)                | Traits, multiple impls, generics with `where` bounds, `math::sqrt`.           |
| 08 | [08-game-of-life](08-game-of-life/)    | 2D simulation on a torus, ANSI terminal output, `libc::usleep` for animation. |
| 09 | [09-json-config](09-json-config/)      | `std::json` parsing, nested object/array walking, fallback defaults.          |
| 10 | [10-expr-interpreter](10-expr-interpreter/) | Lexer + recursive-descent parser + evaluator over arithmetic.            |
| 11 | [11-http-server](11-http-server/)      | `std::net::http` Router, four routes, drop discipline in handlers.            |
| 12 | [12-guessing-game](12-guessing-game/)  | Stdin reads (`stdio::stdin().line()`), seeded RNG via `libc::time` + `rand`.  |
| 13 | [13-closures](13-closures/)            | Capturing closures over `Copy` values, passed through `(Args) -> Ret` slots. |

## Running

From within an example directory:

```sh
cryo run                  # build + execute
cryo run -- arg1 arg2     # forward args to the built binary
cryo build                # build only (artifact lands in build/bin/)
```

The first build pulls in everything the example imports out of the
standard library. Subsequent runs reuse cached object files in
`build/obj/`. Delete `build/` to force a clean rebuild.

## Language tour, in order

Reading the examples in numeric order doubles as a guided tour of
the language. The first few cover only what's in the prelude plus
`core::intrinsics`. By example 5, you've seen structs, enums,
generics, traits, `Box<T>`, `Array<T>`, `String`, and `HashMap<K, V>`.
The later examples layer in I/O, parsing, and a real network server.

## Notable patterns

* **Project layout.** Every example uses the standard `cryoconfig` +
  `src/main.cryo` shape. The fields you can set are documented at
  `docs/cryo.md`.
* **Ownership and `Drop`.** Anything that owns heap memory
  (`String`, `Array<T>`, `HashMap<K, V>`, `Box<T>`, ...) must be
  dropped exactly once. The compiler synthesizes a drop on scope
  exit; the examples make it explicit at the moment the value
  goes "out of useful scope" so the intent is visible. Types that
  contain owning fields implement `trait Drop` so the auto-drop
  pass cascades correctly.
* **Error handling.** `Option<T>` for absent values, `Result<T, E>`
  for fallible operations with a typed reason. The pattern in
  every example is `match (op()) { Result::Ok(...) => ..., Result::Err(...) => ... }`.

## Adding a new example

1. `cryo init <name>` from this directory (or copy an existing
   example's layout).
2. Set `project_name` in `cryoconfig` to match the directory.
3. Add an entry to the table above.
