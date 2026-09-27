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
