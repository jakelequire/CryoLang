# Message text takes `Text`

`retype.py` produced this change's call-site half; `slice1.tsv` is its input
population; `uses.py` lists the uses the compiler does not check; `invert.sh`
is the proof. The compiler decides every call site; the script edits only
where a refusal points.

Recipe, from the parent commit, with `CRYO` a compiler whose diagnostic cap is
off (`max_errors: 0` in `compiler/src/compiler/diag/config.cryo`, built once
and copied out):

1. By hand: `compiler/src/utils/text.cryo` - `Text`, one private `string`,
   `Text::new`, `as_string`, and `Display`.
2. `python retype.py --params slice1.tsv <repo-root>` - each listed
   parameter's `string` becomes `Text`; each file edited imports `Text`.
3. `cd compiler && $CRYO check src/main.cryo > c1.log`, then
   `python retype.py --refusals c1.log compiler` - every E0214 argument
   refused as `string` where `Text` is wanted is wrapped `Text::new(...)`
   (1,685), and every one refused as `Text` where `string` is wanted gets
   `.as_string()` (13). It lists what it does not edit (30 here): field
   initialisers storing a parameter into a `string` field (`.as_string()`),
   `==` against `""` or `null` (`.as_string()`), `{}` formatting (the
   `Display` impl), and one renderer body that indexes the text (it unwraps
   once at the top).
4. `python uses.py slice1.tsv <repo-root>` - every remaining use of a
   retyped parameter inside its own body. Read each: a `Text` passed into a
   C-variadic argument list (`fmt::format`, `printf`), into an array's
   `push` or into `String::push<T>` is NOT refused by `cryo check` - a
   variadic takes anything, and an array method's argument is checked only
   at code generation. 22 lines here needed `.as_string()`; `String::push`
   was the only one the check caught (E0645, reported inside the standard
   library with no call site).
5. The other operating system's code: `$CRYO build --target=<other triple>
   --build-dir=<scratch> --no-incremental` in `compiler/`, then
   `retype.py --refusals` on that log (2 here, both `![target(unix)]`).
6. `CRYO=<compiler> bash invert.sh` - a name handed where text is expected,
   text handed where a name is expected, each alone, then the restored
   tree as the control.

The second slice (`slice2.tsv`: file paths, command lines, URLs and hashes,
target triples and file contents handed to the file system, a tool or the
backend) adds four steps, because most of its bodies work on the bytes:

7. After step 2's check, `python attribute.py slice2.tsv <log> <root> >
   slice2-prologue.tsv` - every function whose body the check refuses
   anywhere other than at a call site's argument.
8. `python retype.py --prologue slice2-prologue.tsv <root>`: those
   parameters are renamed `p_text` and unwrapped once at entry, so the body
   is untouched. Then check, `--refusals`, and `--collapse` (a re-wrap of the
   parameter's own bytes passes `p_text` instead).
9. `uses.py` over the parameters left as `Text`; every one with a use the
   compiler does not check (a C-variadic argument, a generic call such as
   `JsonValue::from(p)` or `HashMap::get(&p)`, an array push) is unwrapped
   at entry the same way (`slice2-prologue2.tsv`, `slice2-prologue3.tsv`,
   re-pointed with `relocate.py <list> HEAD`). The other OS's build
   (`--target`) is a FULL build, so it also catches array pushes (E0636).
10. A clean build; `python retype.py --drop-unused <build-log> <root>`
    removes the unwraps whose every use was collapsed (W0001), and the
    warning set is back to the parent's.
11. The editor: `cd tools/CryoLSP && $CRYO build --build-dir=<scratch>`,
    then `retype.py --refusals <log> tools/CryoLSP`.

Array and pointer parameters (`&string[]`, `string*`) are not in any
slice: retyping one means retyping the store that feeds it.

Slices 3 (`slice3.tsv`, backend labels) and 4 (`slice4.tsv`, literal and
source text) follow steps 2-11, plus:

12. `--refusals` also unwraps a `Text` handed to a C function's `i8*`/`u8*`.
13. `python retype.py --round-trips compiler/src` - `Text::new(x.as_string())`
    is `x` (only `Text` has `as_string`).
14. `python rewraps.py compiler/src --fix` - a re-wrap of a parameter an
    EARLIER slice unwrapped at entry (`--collapse` only sees its own list).

The brace counters in `attribute.py`, `retype.py` and `uses.py` empty string
and char literals first: a body holding `'{'` otherwise runs to the end of
the file, and uses.py's first version missed uses that way and on a
one-line body's head line.
