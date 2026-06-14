# Testing in Cryo

Cryo ships a built-in unit-test framework (`std::test`) and a first-class
`cryo test` runner. There are no external harnesses, no shell glue, and no
third-party dependencies — the compiler discovers tests, synthesizes a test
`main`, and the runner executes each test in an isolated child process.

This document is the canonical reference for the test surface. The framework
source lives in `stdlib/test/` (`error`, `descriptor`, `assert`, `runner`).

---

## 1. Quick start

Tests live in files under a project's `tests/` directory (a sibling of `src/`).
Every test file's `namespace` declaration carries `![config(testing)]`, and
every test function carries `![test]`:

```cryo
![config(testing)]
namespace MyApp::Tests::Math;

import std::test::*;

![test]
function add_works() -> Result<(), TestError> {
    expect_eq(2 + 2, 4)?;
    return Result::Ok(());
}
```

Run the suite from the project root:

```
cryo test
```

The compiler suppresses the project's real `main` during a test build and
synthesizes a runner main that drives every discovered `![test]` function
through `std::test::runner::run_all`.

`std::test` is **not** in the prelude — test files import what they need
explicitly (`import std::test::*;`).

---

## 2. Writing tests

### Test functions

A test is a `![test]` function returning `Result<(), TestError>`:

- return `Result::Ok(())` to pass;
- return `Result::Err(e)` (or panic) to fail.

### Assertions (`std::test::assert`)

| Helper | Checks |
| --- | --- |
| `expect(cond, msg)` | `cond` is true |
| `expect_true(cond)` / `expect_false(cond)` | boolean |
| `expect_eq(a, b)` / `expect_ne(a, b)` | equality |
| `expect_close_f64(a, b, eps)` | float within tolerance |
| `expect_some(opt)` / `expect_none(opt)` | `Option` shape |
| `expect_ok(res)` / `expect_err(res)` | `Result` shape |
| `bail(msg)` / `bail_other(msg)` | fail immediately |

Each assertion returns a `Result<(), TestError>`; propagate failures with the
`?` operator (`expect_eq(x, y)?;`) or by matching and returning `Err`.

### Annotations

- `![ignore]` — the test is skipped unless `cryo test --ignored` is passed.
- `![should_panic]` — the test passes **only** if its body panics or returns
  `Err`; a clean `Ok` exit is reported as a failure.

```cryo
![test]
![should_panic]
function rejects_out_of_range() -> Result<(), TestError> {
    intrinsics::panic("expected", FILE, LINE);
    return Result::Ok(());
}
```

---

## 3. Isolation model

Each test runs in **its own child process** — `fork` on POSIX, a
`CreateProcessA` re-exec on Windows. A panic, abort, or crash in one test
cannot corrupt or take down any other. Cryo has no in-process panic catch
(`setjmp`/`longjmp`), so process isolation is the simplest correct option.

A per-test wall-clock watchdog (default 60 s) kills a hung test and reports it
as a timeout rather than letting the whole run stall.

---

## 4. Running tests

```
cryo test [PATTERN] [FLAGS]
```

`PATTERN` is an optional substring filter on the test's fully-qualified name.

| Flag | Effect |
| --- | --- |
| `--list` | List matching tests without running them |
| `--ignored` | Run only the `![ignore]`-marked tests |
| `--exact` | Treat `PATTERN` as an exact name, not a substring |
| `-q`, `--quiet` | Terse output |
| `--timeout=N` | Per-test wall-clock limit in seconds (default 60; `0` disables) |
| `--format=plain\|pretty\|compact` | Output layout |
| `--color=auto\|always\|never` | ANSI color control |

Environment overrides (useful in CI): `CRYO_TEST_TIMEOUT`, `CRYO_TEST_FORMAT`,
`CRYO_TEST_COLOR`.

---

## 5. Project layout

A test project is an ordinary Cryo project whose `cryoconfig` opts into the
test surface. Test files live under `tests/`; `src/main.cryo` is a placeholder
that `cryo test` suppresses.

```
my-project/
├── cryoconfig
├── src/
│   └── main.cryo          # real entry point (suppressed under `cryo test`)
└── tests/
    ├── math.cryo          # ![config(testing)] + ![test] functions
    └── negative/          # optional compile-fail tests (see §6)
```

An optional `[test]` section in `cryoconfig` sets defaults:

```toml
[test]
format = "pretty"          # plain | pretty | compact
color  = "auto"            # auto | always | never
```

End-user projects with no `[test]` section get the cargo-style `plain`
default; override per run with `--format` / `--color` or the `CRYO_TEST_*`
environment variables.

---

## 6. Compile-fail (negative) tests

Negative tests assert that the compiler **rejects** a program with a specific
diagnostic. They are not `![test]` functions — they are standalone files under
`tests/negative/`, each carrying `![config(negative, <CODE>)]` (plus the usual
`![config(testing)]`):

```cryo
![config(testing)]
![config(negative, E0214)]
namespace MyApp::Tests::Negative::E0214;

function cf_takes_int(x: i32) -> void {}
function main() -> int {
    cf_takes_int("bad");        // string where an i32 is expected
    return 0;
}
```

`cryo test` skips this directory when building the shared test binary (the
files are intentionally malformed), then compiles each file on its own via
`cryo check <file>` and asserts the build fails with the declared code.
Results appear in the normal `cryo test` output under `compile-fail result:`.

A case fails the suite — honestly, as a red test — if the file compiles
cleanly or emits a different code. Name each file after the code it asserts
(`E0214_free_function.cryo`) and use collision-proof identifiers (e.g. a `cf_`
prefix) so a snippet can't shadow a stdlib symbol.

---

## 7. The repository's own suite

This repo's end-to-end suite lives in `tests/` and exercises the language, the
compiler, and the standard library. Drive it through the top-level `Makefile`:

```
make test                  # build the stage-2 compiler if needed, then `cryo test`
make test-list             # enumerate discovered tests without running them
make test ARGS="--ignored some_filter"
```

`make examples` is a companion smoke gate that compiles every `examples/*/`
project with the freshly built compiler. Both `make test` and `make examples`
run in CI on every push and pull request to `main`.
