# Compile-fail tests

Each `.cryo` file here is a **negative test**: a complete program that the
compiler must *reject*. They are driven natively by `cryo test` - no scripts,
no separate command.

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
(`cryo check <file>`) and asserts the build fails with the declared code.
Results appear in the normal `cryo test` output under `compile-fail result:`.

## The directive

```
![config(negative, <CODE>)]    // the file must fail with error[<CODE>]
```

A case **fails the suite** (honestly, as a red test) if the file compiles
cleanly or emits a different code. Some files here intentionally fail today
because they pin a diagnostic the compiler *should* emit but doesn't yet -
that red is the signal, not something to paper over.

Keep `![config(testing)]` too (every test file carries it). Use collision-proof
names (`cf_`-prefixed) so a snippet can't accidentally shadow stdlib symbols.

Name each file after the error code it asserts (e.g. `E0214_free_function.cryo`).
