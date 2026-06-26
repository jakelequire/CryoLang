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

### Fixtures (`std::test::fixture`)

A **fixture** is a resource a test sets up and tears down — a temp directory, a
seeded RNG, a probe allocator, a socket. The `Fixture` trait standardizes the
setup half; the value's `Drop` impl is the teardown:

```cryo
type trait Fixture {
    static setup() -> Result<This, TestError>;
}
```

Cryo runs drop glue on **every** scope exit — the success path and the
early-return paths that `?` and `return Err(...)` take — so a fixture bound as a
local is torn down on success *and* on assertion failure, with no per-test
cleanup code. (A hard process abort/panic is the one path `Drop` cannot cover;
there the OS reclaims everything.)

```cryo
type struct TmpDir { path: String; }

// Define setup() ONCE, in the Fixture impl. A second inherent setup() that
// called TmpDir::setup() would resolve to the trait static and recurse.
implement trait Fixture for struct TmpDir {
    static setup() -> Result<TmpDir, TestError> {
        return Result::Ok(TmpDir { path: make_temp_dir() });
    }
}
implement trait Drop for struct TmpDir {
    drop(mut &this) -> void { remove_dir_all(this.path.as_str()); this.path.drop(); }
}

![test]
function writes_then_reads() -> Result<(), TestError> {
    mut dir: TmpDir = match (TmpDir::setup()) {
        Result::Ok(v)  => { v }
        Result::Err(e) => { return Result::Err(e); }
    };
    // `dir` is torn down on EVERY path below, including the failing `?`.
    write_file(dir.path.as_str(), Str::new("hi"))?;
    return expect_eq(read_file(dir.path.as_str()), Str::new("hi"));
}
```

This is plain RAII — no closures — so the body calls `expect_*` directly. Cryo
v1.0 does not provide a `with_fixture(|f| ...)` closure combinator: a closure
passed to a generic helper cannot call the generic `expect_*` assertions, which
would make such a combinator unusable for real tests. The trait still earns its
place — it names the convention and is the dispatch point a future
`![test(fixture = T)]` directive would call.

> The `Fixture` *trait* (a code-level setup/teardown concept) is unrelated to the
> repo's `tests/fixtures/` directory, which holds static **test-data assets**
> (TLS certs, golden files).

### Parametrized (data-driven) tests (`std::test::table`)

To run one body over a table of cases, loop over a const array and wrap a
per-row failure with `prefix_case(index, e)` so the message names the offending
row (`case [2]: ...`):

```cryo
import std::test::table;

type struct Case { input: i32; want: i32; }

![test]
function doubling_table() -> Result<(), TestError> {
    const cases: Case[] = [
        Case { input: 1, want: 2 },
        Case { input: 2, want: 4 },
        Case { input: 3, want: 6 },
    ];
    mut i: i64 = 0;
    while (i < cases.length) {
        const c: &Case = &cases[i];
        match (expect_eq(c.input * 2, c.want)) {
            Result::Ok(_)  => { }
            Result::Err(e) => { return Result::Err(prefix_case(i as u64, e)); }
        }
        i = i + 1;
    }
    return Result::Ok(());
}
```

Use `prefix_named_case(label, e)` instead when the rows carry their own names
(`case [empty input]: ...`). All cases share one process and one pass/fail line:
the index prefix identifies *which* row failed, but a row that aborts takes the
group down and the group shares one timeout. For cases that must be isolated,
give each its own `![test]` function, or use a separate multi-module project
(§7).

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
    ├── negative/          # optional compile-fail tests (see §6)
    └── projects/          # optional multi-module test projects (see §7)
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

## 7. Multi-module test projects

A single `![test]` file is one compilation unit. When you need to test something
that spans **several cooperating modules** — FFI/C-interop with real link
config, the module system itself (cross-module imports, visibility), or a
realistic end-to-end program — write a **multi-module test project**: a real
Cryo package living under `tests/projects/<name>/`, built through the normal
package pipeline.

```
tests/projects/
└── my_feature/
    ├── cryoconfig        # the BUILD config (link libs, C sources, deps, ...)
    ├── test.json         # the TEST-HARNESS metadata (how to check the project)
    └── src/
        ├── main.cryo
        └── lib.cryo
```

A directory is recognized as a project by the presence of **`test.json`**.
`cryo test` discovers each such project and drives this very compiler as a
subprocess to check it against its declared **outcome**. Projects report under a
`projects` section in the normal `cryo test` output, and a project failure fails
the overall run (non-zero exit).

The two config files have distinct jobs — `cryoconfig` is the build (exactly as
for any package: `[link]`, `[dependencies]`, target type), and `test.json` is
*only* test-harness metadata:

| `test.json` field | Meaning |
| --- | --- |
| `"outcome"` | `"collect"` (default) · `"compile_fail"` · `"run"` |
| `"ignore"` | `true` skips the project (reported as ignored) |
| `"expect"` | per-outcome assertions (below) |

### Outcomes

- **`collect`** — build the package and run *its own* `![test]` functions
  (its `tests/` dir, discovered exactly as for a top-level project). The project
  passes iff all of them pass. This is the common case: a multi-module package
  whose tests exercise cross-module behavior.

  ```json
  { "outcome": "collect" }
  ```

- **`compile_fail`** — the package must **fail** to compile. `cryo build` is run
  and the build must fail emitting `expect.diagnostic`. This is the
  project-granularity lift of the file-level `![config(negative, E0xxx)]`
  mechanism (§6) — useful for negative tests of imports/visibility/resolution
  that need more than one module.

  ```json
  { "outcome": "compile_fail", "expect": { "diagnostic": "E0200" } }
  ```

- **`run`** — build *and run* the package's binary; assert the process exit code
  (and, optionally, that its combined output contains a substring). The
  realistic-integration case.

  ```json
  { "outcome": "run", "expect": { "exit_code": 0, "stdout_contains": "ready" } }
  ```

Each project is built and run as its own process tree, so a crash in one project
can't take down the suite — the same isolation guarantee fork-per-test gives
individual tests (§3). The `PATTERN` argument to `cryo test` filters projects by
directory name just as it filters tests by name.

> Why JSON rather than the INI `cryoconfig` format? `test.json` is read by the
> standard library's own `std::json` parser, and keeping test metadata in a
> separate file leaves `cryoconfig` to mean exactly what it means for every
> other package — the build.

---

## 8. The repository's own suite

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
