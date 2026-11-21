# Testing in Cryo

Cryo ships a built-in unit-test framework (`std::test`) and a first-class
`cryo test` runner. There are no external harnesses, no shell glue, and no
third-party dependencies - the compiler discovers tests, synthesizes a test
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
    expect_equal(2 + 2, 4)?;
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

`std::test` is **not** in the prelude - test files import what they need
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
| `expect_equal(a, b)` / `expect_not_equal(a, b)` | equality |
| `expect_close_f64(a, b, eps)` | float within tolerance |
| `expect_some(opt)` / `expect_none(opt)` | `Option` shape |
| `expect_ok(res)` / `expect_error(res)` | `Result` shape |
| `bail(msg)` / `bail_other(msg)` | fail immediately |

Each assertion returns a `Result<(), TestError>`; propagate failures with the
`?` operator (`expect_equal(x, y)?;`) or by matching and returning `Err`.

### Annotations

- `![ignore]` - the test is skipped unless `cryo test --ignored` is passed.
- `![should_panic]` - the test passes **only** if its body panics or returns
  `Err`; a clean `Ok` exit is reported as a failure.
- `![test_child]` - not a test: a function a test starts as a child process
  by name (see "Test children" below).

```cryo
![test]
![should_panic]
function rejects_out_of_range() -> Result<(), TestError> {
    intrinsics::panic("expected", intrinsics::location());
    return Result::Ok(());
}
```

### Test children

Some tests need a real child process: one in its own process group that
receives an interrupt, or one whose exit status or output is the thing under
test. Mark a function in the test file `![test_child]` instead of `![test]`.
It has a test's shape, `() -> Result<(), TestError>`, but the runner never
runs it as a test and `--list` does not show it. A test starts it by its
fully-qualified name with `TestChild::command` (`std::test::test_child`),
which returns a `Command` that runs this test binary as that test child
(`--test-runner-role=<name>`); the test configures it and spawns it like any
other command.

The child runs that one function and exits 0 when it returns `Ok`, 1 when it
returns `Err` (the error's message on stderr), and through the panic's own
exit when it panics. The per-test timeout applies to the child as well. A
name no test child has makes the child print an error and exit 2.

```cryo
![test_child]
function prints_ready() -> Result<(), TestError> {
    fmt::print("ready");
    return Result::Ok(());
}

![test]
function a_test_child_runs_by_name_and_exits_zero() -> Result<(), TestError> {
    const command: Command = TestChild::command(Str::new("CryoTests::Tests::TestChildren::prints_ready"))?;
    const output: Output = command.output()?;
    return expect_equal(output.status().exit_code(), Option::Some(0));
}
```

### Fixtures (`std::test::fixture`)

A **fixture** is a resource a test sets up and tears down - a temp directory, a
seeded RNG, a probe allocator, a socket. The `Fixture` trait standardizes the
setup half; the value's `Drop` impl is the teardown:

```cryo
type trait Fixture {
    static setup() -> Result<This, TestError>;
}
```

Cryo runs drop glue on **every** scope exit - the success path and the
early-return paths that `?` and `return Err(...)` take - so a fixture bound as a
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
    return expect_equal(read_file(dir.path.as_str()), Str::new("hi"));
}
```

This is plain RAII - no closures - so the body calls `expect_*` directly. Cryo
v1.0 does not provide a `with_fixture(|f| ...)` closure combinator: a closure
passed to a generic helper cannot call the generic `expect_*` assertions, which
would make such a combinator unusable for real tests. The trait still earns its
place - it names the convention and is the dispatch point a future
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
    const cases: [Case] = [
        Case { input: 1, want: 2 },
        Case { input: 2, want: 4 },
        Case { input: 3, want: 6 },
    ];
    mut i: i64 = 0;
    while (i < cases.length()) {
        const c: &Case = &cases[i];
        match (expect_equal(c.input * 2, c.want)) {
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
(section 7).

---

## 3. Isolation model

Each test runs in **its own child process** - `fork` on POSIX, a
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
| `--suite=unit\|compile-fail\|projects` | Run one suite alone; without it all three run, in that order |
| `--list` | List matching unit tests without running them (refused beside `--suite=compile-fail` or `--suite=projects`) |
| `--ignored` | Run only the `![ignore]`-marked tests |
| `--exact` | Treat `PATTERN` as an exact name, not a substring |
| `-q`, `--quiet` | Terse output |
| `--timeout=N` | Per-test wall-clock limit in seconds (default 60; `0` disables) |
| `--format=plain\|pretty\|compact` | Output layout |
| `--color=auto\|always\|never` | ANSI color control |
| `--jobs=N` | How many tests run at once (`0`, the default, is one per processor) |
| `--nocapture` | Let each test write straight to the terminal instead of capturing its output |
| `--show-output` | Replay the captured output of passing tests too, not only failing ones |

Environment overrides (useful in CI): `CRYO_TEST_TIMEOUT`, `CRYO_TEST_FORMAT`,
`CRYO_TEST_COLOR`, `CRYO_TEST_JOBS`, `CRYO_TEST_NOCAPTURE`.

A built test binary takes the same flags, and `-j N` for `--jobs`. A flag it
does not know is an error: it prints the flag and its usage to stderr and exits
with code 2 before running anything.

`cryo test` hands every option the compiler declares no flag for to the test
binary as written - `cryo test --no-capture` reaches the runner's
`--no-capture` - so the runner, not the compiler, judges it. Every other
command refuses an option the compiler does not declare.

---

## 5. Project layout

A test project is an ordinary Cryo project whose `cryoconfig` opts into the
test surface. Test files live under `tests/`; `src/main.cryo` is a placeholder
that `cryo test` suppresses.

```
my-project/
+-- cryoconfig
+-- src/
|   +-- main.cryo          # real entry point (suppressed under `cryo test`)
+-- tests/
    +-- math.cryo          # ![config(testing)] + ![test] functions
    +-- negative/          # optional compile-fail tests (see section 6)
    +-- projects/          # optional multi-module test projects (see section 7)
```

An optional `[test]` section in `cryoconfig` sets defaults:

```toml
[test]
format = "pretty"          # plain | pretty | compact
color  = "auto"            # auto | always | never
```

End-user projects with no `[test]` section get the one-line-per-test `plain`
default; override per run with `--format` / `--color` or the `CRYO_TEST_*`
environment variables.

---

## 6. Compile-fail (negative) tests

Negative tests assert that the compiler **rejects** a program with a specific
diagnostic. They are not `![test]` functions - they are standalone files under
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

A case fails the suite - honestly, as a red test - if the file compiles
cleanly or emits a different code. Name each file after the code it asserts
(`E0214_free_function.cryo`) and use collision-proof identifiers (e.g. a `cf_`
prefix) so a snippet can't shadow a stdlib symbol.

A `//~ ERROR[<CODE>] <message>` comment (`//~^` for the line above) pins a
diagnostic to its line, and every diagnostic in the file must be pinned. When
several diagnostics share one cause and one fix - every change a method makes
through its `&this` receiver - the compiler prints them as one report with an
`-->` arrow per place, stacked under the header. Each place is still a
diagnostic of the report's code at its own line, carrying the report's
headline, and each needs its own `//~`. The language server names the report
in each place's related information, `diag-<ten hex digits>`, hashed from its
code, headline, file name and first place, so the places of one report read
as one; the compiler's output prints no name.

A warning is pinned with `//~ WARN[<CODE>]` (`WARNING` reads the same). The
declared code must appear at the severity the compiler reports it at, and that
severity is chosen where the code is emitted, not by its letter: E0153 (an
unknown directive) is a warning. So when a `//~` names the declared code, its
severity is the one the declared code must be reported at; a file with no such
annotation is held to the letter, `W....` as `warning[W....]` and every other
code as `error[....]`, and an error reported as a warning fails it.

---

## 7. Multi-module test projects

A single `![test]` file is one compilation unit. When you need to test something
that spans **several cooperating modules** - FFI/C-interop with real link
config, the module system itself (cross-module imports, visibility), or a
realistic end-to-end program - write a **multi-module test project**: a real
Cryo package living under `tests/projects/<name>/`, built through the normal
package pipeline.

```
tests/projects/
+-- my_feature/
    +-- cryoconfig        # the BUILD config (link libs, C sources, deps, ...)
    +-- test.json         # the TEST-HARNESS metadata (how to check the project)
    +-- src/
        +-- main.cryo
        +-- lib.cryo
```

A directory is recognized as a project by the presence of **`test.json`**.
`cryo test` discovers each such project and drives this very compiler as a
subprocess to check it against its declared **outcome**. Projects report under a
`projects` section in the normal `cryo test` output, and a project failure fails
the overall run (non-zero exit).

The two config files have distinct jobs - `cryoconfig` is the build (exactly as
for any package: `[link]`, `[dependencies]`, target type), and `test.json` is
*only* test-harness metadata:

| `test.json` field | Meaning |
| --- | --- |
| `"outcome"` | `"collect"` (default) / `"build"` / `"compile_fail"` / `"run"` / `"interpret"` |
| `"ignore"` | `true` skips the project (reported as ignored) |
| `"expect"` | per-outcome assertions (below) |

`make test-census` runs the suite with `-Werror=W0017`, which `cryo test`
hands to every project's build too: a unit test or a project that discards a
`![must_use]` result fails to build there, and `make examples` holds every
example to the same.  A project that wants to show the warning asserts it
from a compile-fail file instead, which is checked without the flag.

A project's build output is echoed only when the project fails, so the runner
lists each warning its build reported under the project's line, passing or
not, as `    warning <code> <file>:<line>` (a warning with no span has no
place and is not listed).  The projects summary counts them in a `warnings`
row, printed whatever the count.

### Outcomes

- **`collect`** - build the package and run *its own* `![test]` functions
  (its `tests/` dir, discovered exactly as for a top-level project). The project
  passes if all of them pass. This is the common case: a multi-module package
  whose tests exercise cross-module behavior. Its line reports how many tests
  its run reported (`ok (12 tests)`), and `make test-census` refuses a
  passing `collect` project that ran none. The output assertions below
  (`output_contains`, `stderr_contains`, `file`, ...) read that `cryo test`
  run, so a project can assert what reaches its stdout. The run's exit is
  judged as a `build` or `run` project's is: it must be `exit_code` (0 when
  unwritten), or with `fails` any non-zero exit.

  ```json
  { "outcome": "collect" }
  ```

- **`build`** - the package must compile (`cryo build` succeeds); nothing is
  run. Useful when compiling and linking is itself the assertion.

  ```json
  { "outcome": "build" }
  ```

- **`compile_fail`** - the package must **fail** to compile. `cryo build` is run
  and the build must fail emitting `expect.diagnostic`. This is the
  project-granularity lift of the file-level `![config(negative, E0xxx)]`
  mechanism (section 6) - useful for negative tests of imports/visibility/resolution
  that need more than one module.

  ```json
  { "outcome": "compile_fail", "expect": { "diagnostic": "E0200" } }
  ```

- **`run`** - build *and run* the package's binary; assert the process exit code
  (and, optionally, that its combined output contains a substring). The
  realistic-integration case.

  ```json
  { "outcome": "run", "expect": { "exit_code": 0, "stdout_contains": "ready" } }
  ```

- **`interpret`** - run the file `arguments` names with `cryo interpret`
  (tools/CryoSmith/INTERPRETER.md); the assertions judge the interpreter's
  exit and its streams, as `run`'s judge the program's.  The interpreter's
  own tests are these: what a program prints when interpreted, and the
  line and exit code of each ending it reports (a violation, a construct
  not covered, a panic).

  ```json
  { "outcome": "interpret", "arguments": ["main.cryo"], "expect": { "exit_code": 70 } }
  ```

A project whose report carries a machine-applicable suggestion proves the
fix with `applied`: besides its own outcome, a copy of the project (outside
the tree, without its `build/`) is run with `cryo run --apply-fixes`, which
must make at least one fix, build the fixed source and exit with
`applied.exit_code` (default 0). The project passes only if both hold, so a
fix the compiler calls machine-applicable is shown to compile once made,
with no second copy of the program written by hand:

```json
{
  "outcome": "compile_fail",
  "expect": { "diagnostic": "E0101" },
  "applied": { "exit_code": 0 }
}
```

The roster golden records the `applied` exit code on the project's line
(`applied=0`), so dropping the field from a `test.json` is a roster change.

A project that tests what a SECOND build does - an incremental build after
an edit, or after a change of input - says so with `rebuild`. The runner
copies the project (outside the tree, without its `build/`), builds the copy
once (`cryo build`, or the project's own `cryo test` for a collect
fixture), which must exit 0; then overwrites each `replace` entry's `file`
in the copy with the project's own `with` file, and runs the project's
outcome again in the same copy, so the second build finds the first one's
build directory. `rebuild.arguments`, when given, replace the project's
`arguments` for the second run. `expect` judges the second run only. A
`rebuild` that replaces nothing and changes no argument, or a replacement
missing `file` or `with`, makes the `test.json` malformed. A `with` file
outside `src/` is never compiled by the first build, since module discovery
is import-driven:

```json
{
  "outcome": "run",
  "rebuild": {
    "replace": [ { "file": "src/value.cryo", "with": "edits/value.cryo" } ]
  },
  "expect": { "exit_code": 2, "output_contains": ["reusing 1 cached object(s)"] }
}
```

The roster golden records the number of replaced files on the project's
line (`rebuild=1`).

A project can also drive the compiler itself, for a test of what `cryo`
writes rather than what the program does. `environment` sets variables for
that one command (`"NAME=value"` entries), `arguments` adds to its command
line after the outcome's own verb, and the compiler's stdout and stderr are
kept apart, so `expect` can say which stream a line belongs on:

- `stderr_contains` / `stderr_excludes` - substrings of stderr alone
  (`output_contains` / `output_excludes` read both streams together);
- `output_in_order` - substrings of both streams together, each after the
  one before it: the order things are printed in, which a contains list
  cannot say;
- `stdout_line_prefixes` / `stderr_line_prefixes` - every non-empty line of
  that stream must start with one of these, so a line nobody expected fails
  the project whatever it says;
- `file`, with `file_contains` / `file_excludes` - a file the command writes,
  relative to the project; it is removed before the command runs and after
  it is read, so what is checked is always this run's.

A `.cryo` file named in `arguments` makes a `build` project a single-file
build of that file (the directory then needs no `cryoconfig`), so the flags
a single-file build takes are tested the same way:
`"arguments": ["main.cryo", "--dev"]`.

```json
{
  "outcome": "build",
  "arguments": ["--log", "driver", "--log-file", "compiler.log"],
  "expect": {
    "stderr_excludes": ["[debug "],
    "file": "compiler.log",
    "file_contains": ["[debug driver] "]
  }
}
```

Each project is built and run as its own process tree, so a crash in one project
can't take down the suite - the same isolation guarantee fork-per-test gives
individual tests (section 3). The `PATTERN` argument to `cryo test` filters projects by
directory name just as it filters tests by name.

> Why JSON rather than the INI `cryoconfig` format? `test.json` is read by the
> standard library's own `std::json` parser, and keeping test metadata in a
> separate file leaves `cryoconfig` to mean exactly what it means for every
> other package - the build.

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

### Declaring what a change moves

`make verify` runs its gates concurrently; `ARGS="--sequential"` runs them
one after another, for a host whose memory cannot hold them all at once.
The gates and their verdicts are the same either way.

`make verify ARGS="--baseline REV --require-identical"` compares every
compiled object against the compiler built from `REV`, and fails on any
program whose verdict or objects differ unless the change declares it.  A
change declares a program, one line each with its reason, in one of two files
the comparison reads:

- `tests/started-compiling` - a project the baseline refuses with a
  diagnostic and the change builds.
- `tests/started-passing` - a program the baseline's census fails and the
  change corrects: a project by its directory name, a compile-fail file as
  `negative/<file>.cryo`, an example as `examples/<name>`, a unit-test file by
  its path under `tests/tests` (its object must move), or `archive:<path>` for
  a unit-test file the baseline fails through the stdlib archive.
- `moved:<project>` in `tests/started-passing` - a project both compilers
  build and pass whose objects the change moves on purpose (a codegen
  correction no assertion of the project observes).  The baseline's census
  must pass it, its objects are left out of the comparison, and at least one
  of them must have moved; a declared project whose objects are all
  unchanged fails, and so does any object that moved in a project nothing
  declares.  `moved:examples/<name>` says the same of an example both
  compilers build, judged in the examples' comparison.
- `moved-archive:<module>` in `tests/started-passing` - a standard-library
  module, written by its full name (`moved-archive:std::time::clock`), whose
  objects in the stdlib archives the change moves on purpose: a compiler
  change that alters what the library's own source compiles to.  The
  module's object in every archive both runs built is left out of the
  archive comparison, and at least one of them must have moved; a declared
  module whose archive objects are all unchanged fails, and so does a name
  that is no module of the standard library.  A change to the library's
  SOURCE never needs it: verify builds this tree's stdlib with both
  compilers, so a source edit moves nothing there by construction - prove one
  by building the library before and after at one path.
- `moved-instances:<module>` in `tests/started-passing` - a standard-library
  module, by its full name, whose object in the unit suite's own build the
  change moves on purpose.  That object holds the generic instances of the
  module the unit tests use and no archive holds, so a compiler change to how
  generic code compiles moves it whether or not it moves the module's archive
  objects.  The unit suite's object of the module is left out of the tests'
  comparison and must have moved; a declared module the unit suite built no
  object of, or whose object is unchanged, fails.  A project's object of the
  same module is the project's, declared with `moved:<project>`.
- `moved-runtime:<configuration>/<member>` in `tests/started-passing` - a
  member of the runtime archive of one configuration (`freestanding`,
  `hosted-abort` or `hosted-unwind`), written by the configuration and the
  member's file name (`moved-runtime:hosted-unwind/Unwind.o`), that the change
  moves on purpose: a runtime source edit, or a compiler change that alters
  what the runtime compiles to.  Each compiler builds the runtime it links -
  the baseline's from the baseline's `runtime/`, the tree's from the tree's -
  so a runtime edit moves its members, and so can a codegen change.  The
  member is left out of the runtime comparison and must have moved, or be in
  the tree's archive alone - a member the change adds; a declared member that
  is unchanged fails, and so does a name that matches no member of the tree's
  archive, and any member that moved in an archive nothing declares.  Members
  are compared by name, not by their place in the archive, so a member added
  ahead of another does not move that one.  Every configuration compiles every module, so a change to
  one declares its member once per configuration whose archive it moved in.
  Against a baseline whose runtime is laid out as one archive per tier, the
  runtime population is reported as not compared: no member pairs with one.

A line stays once committed: against a later baseline it is inert.
Rewriting a line into another form with its reason kept declares nothing
again.

A baseline run also fails on a NEW warning.  The warnings the unit suite's
build prints in the census are compared with the baseline's as sets of code,
file and line, so one warning traded for another fails as surely as one
added; a warning the change removes passes.  There is no declaration for a
new warning: a change that introduces one fixes it.  Each project's own build
warnings, which the runner lists under its line (see the projects suite
above), are compared the same way, project by project, and a warning only the
tree's build of a project reports fails the run; a project declared in
`tests/started-compiling` or `tests/started-passing` is left out, since the
change is declared to alter its build.  A baseline whose runner predates that
listing has none to compare against - its projects summary has no `warnings`
row, which is how "none listed" is told from "no warnings" - so its run prints
`project build warnings  not compared` instead.

## 9. The language server's tests

`make lsp-check` (`cryogate lsp`) builds `tools/CryoLSP` with the compiler
under test and then drives the binary it linked through its behavioural
checks, over stdio, the way an editor does. The gate passes only when no
check fails and at least `--min-checks` pass; a run that lost its fixtures is
a refusal. `cryogate lsp-smoke [<server>] [--fixture NAME]` runs the checks
alone against a server already built.

A fixture is a directory under `tools/CryoLSP/tests/fixtures/`: a real
project (a `cryoconfig` and its sources) that compiles, so the expected
answers are what the program actually binds, plus a `checks.json`. Each
fixture runs in its own server process. Build a fixture with the compiler
before relying on it; the gate does not compile it to a binary.

Positions are named by markers, not by line and column:

- `/*@name*/` written immediately before a token names the token's first
  character.
- `/*^name*/` names the position where the comment starts, which is where a
  completion cursor sits (`wb./*^after_dot*/`).

A check refers to a marker as `file@name`. A missing marker fails the run, so
editing a fixture cannot quietly move a check onto another token. A comment
between the segments of an import's path stops the import compiling, so a
marker on an import's last segment is written after it, `import
Probe::B/*^name*/;`. A reference may add `+N` columns
(`src/main.cryo@fstr+5`), for a position inside a string, where no comment
can sit.

`checks.json` names the documents opened, in order, and the checks:

```
{
  "open":   ["src/main.cryo", ...],
  "checks": [
    {"name": "...", "method": "definition", "at": "src/main.cryo@marker",
     "expect": "src/b.cryo@marker" | null},
    {"name": "...", "method": "hover" | "expandHover", "at": "...",
     "contains": ["..."], "excludes": ["..."]}       or "expect": null
    {"name": "...", "method": "completion", "at": "...",
     "includes": ["..."], "excludes": ["..."]},
    {"name": "...", "method": "diagnostics", "file": "src/main.cryo",
     "expect": [{"code": "W0001", "at": "src/main.cryo@marker",
                 "message": "part of the text"}]},
    {"name": "...", "method": "references" | "documentHighlight",
     "at": "...", "declaration": true, "expect": ["src/b.cryo@marker", ...]},
    {"name": "...", "method": "documentSymbol", "file": "src/b.cryo",
     "includes": ["Item", "Item/get"], "excludes": ["..."]},
    {"name": "...", "method": "workspaceSymbol", "query": "...",
     "includes": ["..."], "excludes": ["..."]},
    {"name": "...", "method": "semanticTokens", "file": "src/main.cryo",
     "expect": {"src/main.cryo@marker": "variable.readonly", "...": null}},
    {"name": "...", "method": "inlayHints", "file": "src/main.cryo",
     "expect": {"src/main.cryo@marker+4": ": i32", "...": null}},
    {"name": "...", "method": "signatureHelp", "at": "...", "contains": [...],
     "excludes": [...], "active": 1, "param": "b: i32", "count": 2}  or "expect": null
    {"name": "...", "method": "prepareRename", "at": "...",
     "expect": "src/main.cryo@marker", "placeholder": "name"},
    {"name": "...", "method": "rename", "at": "...", "new_name": "...",
     "old": "name", "expect": ["src/main.cryo@marker", ...]},
    {"name": "...", "method": "rename" | "prepareRename", "at": "...",
     "new_name": "...", "refused": "part of the reason"},
    {"name": "...", "method": "codeAction", "at": "...", "code": "W0001",
     "old": "x", "new_text": "_x", "fix_at": "...", "fix_old": ""}
  ]
}
```

- A diagnostic's `message` is optional; `"code": null` is one with no code,
  the server's own notice of a file not analysed.
- A references, highlight or rename check compares the set of starts with the
  markers'; every rename edit must replace exactly `old`'s width with
  `new_name`, and a refusal passes only on an error reply holding `refused`.
- A symbol check names a nested symbol by its path, `Parent/child`, and a
  document symbol check also fails on a symbol whose `selectionRange` is not
  inside its `range` (an editor refuses the whole answer otherwise).
- A semantic-tokens check names, per position, the token that must start
  there - its type, then modifiers it must carry (it may carry more) - or
  `null` for none; an inlay-hints check names the hint's label there.
- A signature check reads the active signature's label, the active
  parameter's index, that parameter's label as its offsets cut it, and
  `count`, how many signatures (every overload) the answer offers.
- A code-action check waits for a diagnostic of `code` underlining `old` at
  `at`, asks for its actions, and passes when one quickfix replaces exactly
  `old`'s width there with `new_text`; a fix that edits elsewhere names that
  place with `fix_at` and the text it replaces there with `fix_old`.
- A completion check may carry `"details": {"label": "text"}` (the item's
  detail holds the text) and `"trigger": ":"` (sent as the editor sends one
  the typed character triggered).
- Any check may carry `"edit": {"file", "from", "to"}`: the document is
  changed by replacing the first `from` with `to`, the check runs against the
  changed text and its markers, and the document is changed back; with
  `"save": true` each change is followed by a save.
- A position check may carry `"quiet_stderr": true`: it then also fails when
  the server writes to its error stream while answering.
- A fixture may name vendored libraries, `"vendor": {"Widgets":
  "bindings/Widgets.cryo"}`: each is registered, with that file as its
  bindings, in a registry under a temporary `CRYO_HOME`, and the fixture runs
  from a copy; a check naming the bindings file means the copy an analysis
  writes into the build folder.

The protocol checks - the lifecycle, the error replies, requests behind an
analysis, a file outside any project, a jump into the standard library - are
written in `tools/CryoGate/src/lsp/run/smoke.cryo`.

Adding a check: write the marker, add the check, run
`cryogate lsp-smoke <server> --fixture <name>`, and show the check failing on
the server from before the change. Raise `--min-checks`' default in
`tools/CryoGate/src/gates/lsp.cryo` to the new passing count in the same
commit.
