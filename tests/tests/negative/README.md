# Compile-fail tests

Each `.cryo` file here is a **negative test**: a complete program on which the
compiler must emit a specific diagnostic - either an **error** that rejects the
program (an `E`-code) or a **warning** it must raise (a `W`-code). They are
driven natively by `cryo test` - no scripts, no separate command.

```cryo
![config(testing)]
![config(negative, E0214)]
namespace CryoTests::Negative::E0214FreeFunction;

function cf_takes_int(x: i32) -> void {}
function main() -> int {
    cf_takes_int("bad");      // string where an i32 is expected
    return 0;
}
```

## How it works

`cryo test` **skips this directory** when building the shared test binary
(these files are intentionally malformed, so one would otherwise break the
whole build). It then runs the compiler on each file on its own
(`cryo check <file>`) and asserts the compiler emits the declared code -
`error[<CODE>]` for an `E`-code (which also fails the build), or
`warning[<CODE>]` for a `W`-code (which does not fail the build; the assertion
is on the emitted diagnostic, not the exit status). Results appear in the normal
`cryo test` output under `compile-fail result:`.

## The directive

```
![config(negative, <CODE>)]    // output must contain error[<CODE>] / warning[<CODE>]
```

A case **fails the suite** (honestly, as a red test) if the declared code is
absent from the output - whether because nothing was emitted or a different code
was. `make test` is a required gate, so every file in this directory must emit
its declared code *today*. A diagnostic the compiler *should* emit but doesn't
yet is tracked as an issue/TODO - it cannot live here as a permanently red test.

## Message and span assertions (`//~`)

The `![config(negative, <CODE>)]` directive alone only checks the code appears
*somewhere*. To also pin the **message text** and the **source line** - and to
catch stray cascade diagnostics - annotate the offending lines rustc-style:

```cryo
function main() -> int {
    return cf_missing;   //~ ERROR[E0201] cannot find value
}
```

- `//~ <SEV>[<CODE>] <text>` asserts a diagnostic on **this** line, of severity
  `ERROR` (or `WARN`), carrying `<CODE>`, whose message **contains** `<text>`
  (the trailing text is optional; omit it to assert only code + line).
- `//~^` points one line up, `//~^^` two, for a diagnostic whose line can't hold
  a trailing comment (e.g. the token itself runs to end-of-line).
- Once a file carries **any** `//~`, the check becomes two-way: every annotation
  must match a diagnostic anchored in this file, **and** every such diagnostic
  must be annotated - so an unexpected extra/cascade diagnostic fails the test.
  Diagnostics anchored in other files (e.g. the stdlib) are out of scope.

Annotations are optional; a file with none keeps the code-presence-only check.

Keep `![config(testing)]` too (every test file carries it). Use collision-proof
names (`cf_`-prefixed) so a snippet can't accidentally shadow stdlib symbols.

Name each file after the code it asserts (e.g. `E0214_free_function.cryo`,
`W0001_unused_variable.cryo`).
