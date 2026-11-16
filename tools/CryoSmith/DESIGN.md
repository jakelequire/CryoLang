# CryoSmith

CryoSmith writes random, valid, deterministic Cryo programs from a seed. It is
a generator and nothing else: it does not run a compiler, judge an output or
file a bug. CryoGate calls it as an external program, builds what it wrote
under several configurations, runs the results and compares them.

```
seed + options --> cryosmith --> program.cryo --> cryogate: build -O0, -O2, cross
                                                            run, compare the hash
                                                            reduce, triage, file
```

Every program ends by printing one line, `cryosmith-hash <u64>`, a hash of
every value it observed, and exits 0. Two builds of one program that print
different lines, or a build that refuses the program, crashes or does not
exit 0, is a compiler finding, because the program was valid and
deterministic by construction.

## 1. Program model

CryoSmith builds a program as a tree first and prints it last. Nothing is
generated as text.

- **Arenas, not pointers.** A `Program` holds three arenas: expression nodes,
  statement nodes and functions. A node names its children by index. A block
  is a chain of statements, each naming the next (`-1` ends the chain), so no
  node owns another and the whole model is plain data.
- **Expression nodes** carry their scalar type and one of: an integer or
  boolean literal; a type's named limit (`i8::MIN`, `u32::MAX`); a variable
  read; a unary operator; a binary operator; a wrapping method call
  (`a.wrapping_add(b)`); a cast (`a as T`); an implicit widening (a narrower
  value where its wider type is expected, printed bare); an `if` expression;
  a call (its arguments are a run of a fourth arena, argument indices).
- **Statement nodes:** a declaration (`const` / `mut`), an assignment (plain
  or compound), an observation (`observed = mix(observed, ...)`), `if` /
  `else`, a loop (three shapes, below), a loop exit (`if (c) { break; }`,
  `if (c) { continue; }`) and a return, early (`if (c) { return e; }`) or
  final.
- **Functions** have numbered parameters, a return type, a body and a
  **cost**: the most work one call can do (section 3.3).
- **The printer** walks the model once. Every binary expression is
  parenthesised, so precedence never decides a meaning, and every statement
  has its own line.

The model is a structure an evaluator can walk later: the plan's expected
output (a value model) and the reference interpreter both start from it.

## 2. Type-directed generation

Generation asks for an expression **of a type**, with a depth budget. An
environment lists every name in scope with its type, whether it is `mut`, and
whether it may be assigned (a loop counter may be read and never assigned).
A scope is a length of that list; leaving it truncates the list.

For a wanted integer type `T` the forms are chosen by weight from those the
enabled features allow:

| form | shape printed | why it is valid |
|---|---|---|
| literal | `42`, `(-7)` | its value is chosen inside `T` |
| named limit | `T::MIN`, `T::MAX` | the extremes without an edge literal |
| variable | `v3` | a name of type `T` in scope |
| cast | `(e as T)` | `e` of any other integer type |
| wrapping | `e.wrapping_add(e)`, `_subtract`, `_multiply` | defined for every value |
| guarded plain | `((a & M) + (b & M))` | section 3.1 |
| bitwise | `(a & b)`, `(a \| b)`, `(a ^ b)`, `(~a)` | defined for every value |
| shift | `(a << (b & (W-1)))`, `>>` | the amount is below the width |
| division | `(a / ((b & M) \| 1))`, `%` | the divisor is in `[1, M]` |
| negation (signed) | `(-(a & MAX))` | the operand is not `MIN` |
| `if` expression | `if (c) { a } else { b }` | both arms are `T` |
| call | `h2(a, b)` | a function returning `T` (section 3.3) |

A boolean is a literal, a variable, a comparison of two integers of one
type, a comparison of two integers of **different** types (they compare by
their true values), `&&`, `||`, `!`, or `==` / `!=` of two booleans.

At depth 0 only leaves are chosen. **Widening** (`v` of type `u8` where a
`u32` is wanted) is generated only where the language converts implicitly: an
initializer, an assignment, a call's argument and a return value.

**Literal-only expressions.** A bare literal beside a typed operand takes
that operand's type, but literals combined with each other (`(12 & 63)`) are
typed only by where the whole stands, and an operand gives them nothing: they
are an `i32`, and `x + (12 & 63)` with `x: u32` is refused. So every
composite expression the generator writes **reads a name**: its first operand
does, and every masked operand does. Some places give even a bare literal no
type (a method's receiver, a cast's operand, a shifted value, an operand of
`~`, an `if` expression's arms, the left side of a comparison); there the
expression reads a name too. Every scope has one: `observed`, the program's
global hash state, is always readable, a named limit (`u8::MAX`) is a typed
constant, and a function has at least one parameter.

## 3. Guarantees, and how each is enforced

### 3.1 Valid under either overflow rule

The spec says integer `+ - *` wrap today; the test-infra plan replaces that
with *debug traps, release wraps*. A program CryoSmith writes is valid under
both, because no plain `+`, `-`, `*` or negation can overflow:

- **Wrapping methods** (`wrapping_add`, `wrapping_subtract`,
  `wrapping_multiply`) are the generated form wherever any value may meet any
  value. They are defined for every input under both rules.
- **Guarded plain operators** mask their operands into a range whose result
  fits `T` (`W` is the width):

  | operation | signed | unsigned |
  |---|---|---|
  | `+` | `(a & M) + (b & M)`, `M = 2^(W-2) - 1` | the same |
  | `-` | `(a & M) - (b & M)`, `M = 2^(W-2) - 1` | `(a \| H) - (b & M)`, `H = 2^(W-2)`, `M = H - 1` |
  | `*` | `(a & M) * (b & M)`, `M = 2^((W-1)/2) - 1` | `M = 2^(W/2) - 1` |
  | unary `-` | `-(a & MAX)` | not generated |

  Every mask is below `2^63`, so no literal meets the documented trap of a
  literal past `i64::MAX` used inline against a `u64`.
- **Compound assignment** is generated only for the operators that cannot
  overflow (`&=`, `|=`, `^=`, the guarded shifts and the guarded `/=`, `%=`).
- **Shifts** are outside both rules (a shift by the width or more is left
  undecided by the plan): the amount is masked to `W - 1`.
- **Division and remainder** fault on a zero divisor and on `MIN / -1` today,
  and panic under the plan: the divisor is `((b & M) | 1)`, which is in
  `[1, M]`, never zero and never negative.
- **Literals** fit their type; `MIN` of a signed type is written `T::MIN`, and
  no unsigned literal exceeds `i64::MAX` (`T::MAX` is the named limit).

### 3.2 Well typed and accepted

- Every expression is generated for a type, and every form above produces
  exactly that type: operands are of `T`, a different type is converted with
  `as`, and widening appears only at the implicit-conversion places.
- Names are declared before use, in scope, unique per function (`v<N>`,
  `p<N>`, counters `n<N>`, functions `h<N>`); only `mut` names are assigned,
  and no loop counter is. No name is spelled like a type: a local `i8` would
  stand in front of the type in `i8::MIN`, and a function `f32` in front of
  `f32`.
- A function's body ends with a `return` of its type; `main` never returns
  early, so its final print always runs.
- The corpus check (section 7) is the proof that this list is complete:
  every refusal is either a generator defect or a compiler finding.

### 3.3 Terminates, within a budget

- **Bounded loops.** Every loop counts a counter from 0 to a literal trip
  count (at most 6) and the body never assigns the counter:
  `for (mut n0: u16 = 0; n0 < 4; n0 = n0 + 1)`, `while (n1 < 3) { n1 = n1 + 1; ... }`
  (the increment comes first, so a `continue` cannot skip it) and
  `for (n2 in 0..5)`. Loops nest at most three deep.
- **No recursion.** Function `h<k>` calls only `h<j>` with `j < k`, and `main`
  is last: the call graph is acyclic.
- **A cost model.** A statement costs its expressions' nodes; a loop costs its
  trip count times its body; an `if` the dearer arm; a call the callee's cost.
  A call is generated only while the caller's cost stays under its limit, so
  every program does a bounded amount of work, known at generation time.

### 3.4 Deterministic output

- No input, no clock, no address, no environment, no thread: the only
  output is the final hash line.
- `observed` is a global `u64`. An observation folds a value into it with
  `mix`, defined in the program as FNV-1a's step plus a constant
  (`(state ^ value).wrapping_multiply(1099511628211).wrapping_add(2654435769)`),
  so folding the state into itself does not stick at zero. An integer is
  folded as `(e as u64)`; a boolean as `1` or `2`. `observed` is read as a
  value only where no other name of the wanted type is in scope.
- Each block ends by observing every name it declared, a function body ends
  by observing its parameters, and `main` ends by observing one call of each
  helper the cost model allows, so every value a program computes reaches the
  hash. A nested block spends at most half the statements left, so no
  function is one untaken branch.

### 3.5 Reproducible from the seed

- One seed and one set of options give byte-identical text, on every host:
  CryoSmith reads no clock, no environment and no file.
- The random stream is the standard library's `Rng::from_seed`, whose
  sequence is fixed for every version of the library.
- **Each function has its own stream**, seeded from the program seed and the
  function's index, so a change in how one function spends random draws does
  not reshuffle the next function's choices (it still sees the earlier
  functions' signatures and costs, which its calls use).
- **The function count shapes nothing before `main`**: a program generated
  with `--functions 3` is the first three helpers of the same seed's program
  with more, plus its own `main`.
- The header records the **generator version**, raised whenever one seed may
  produce different text, so a seed filed in a bug report names the
  generator that reproduces it.

## 4. Features and stages

Features are named, and enabled as a comma-separated list. `default` is
every feature of the current stage; `no-<name>` removes one.

| stage | features |
|---|---|
| 1 (this one) | `integers` (always on), `booleans`, `wrapping`, `guarded`, `bitwise`, `shifts`, `division`, `casts`, `widening`, `if-expressions`, `branches`, `loops`, `loop-exits`, `calls`, `early-returns` |
| 2 | `structs`: fields, defaults, inline methods, struct literals |
| 3 | `generics`: generic functions and structs, `where` bounds, overload sets |
| 4 | `references`, `moves`: `&` / `mut &`, receivers, move-checked bindings |
| 5 | `drop`: a drop log folded into the hash, the expected drop order |
| 6 | `closures`: capturing and not, passed to function-typed slots |
| 7 | `traits`: default methods, implementations, calls through a bound |
| 8 | `async` |

A new stage adds features; it never changes what an existing seed and
feature list generate without raising the generator version.

## 5. Command line (proposed; awaiting approval)

```
cryosmith generate --seed N [options] [--out FILE]
cryosmith batch --first-seed N --count K --out-dir DIR [options]
cryosmith features
cryosmith version
cryosmith selftest
cryosmith corpus-check --cryo COMPILER --work DIR [--first-seed N] [--count K] [options]
```

Generation options: `--features LIST` (default `default`), `--functions N`
(helper functions; default chosen from the seed, 2 to 6), `--size N` (the
statement budget of one function, default 24), `--no-header`, and
`--plant NAME` (section 7).

- `generate` writes one program to `--out`, or to standard output without
  one.
- `batch` writes `DIR/smith-<seed>.cryo` for `K` consecutive seeds and prints
  each path on its own line.
- `features` prints every feature, its stage and whether `default` holds it;
  `version` prints the generator version.
- **Exit codes:** 0 done; 1 an output could not be written, or
  `corpus-check` found a failing program; 2 a usage error.
- **The header** (default on) is one comment line, the same for the same
  seed and options wherever the file goes:

  ```
  // cryosmith 1 seed 42 features booleans,wrapping,... functions 4 size 24
  ```

  Generating again from the header's fields reproduces the file.
- **The output contract:** the program prints exactly one line,
  `cryosmith-hash <decimal u64>`, and exits 0.

## 6. Reduction-friendliness

- One statement per line, every name numbered, functions in index order and
  `main` last: a line-based reducer deletes lines, and a deletion that breaks
  a use is refused by the compiler, which the reducer reads as "not the
  same failure".
- Functions are independent of the function count (section 3.5), so a
  reducer can first ask CryoSmith for fewer functions and a smaller `--size`
  from the same seed, then reduce lines.
- A guard is a subtree of its own (`((b & M) | 1)`): a reducer that rewrites
  expressions keeps a guard whole, or the program it tests is no longer
  CryoSmith's guarantee.

## 7. CryoSmith's own tests

- **`cryosmith selftest`** runs in process and builds nothing, so
  `make check-fast` runs it: the same seed gives the same bytes; different
  seeds differ; the function prefix property of section 3.5; feature lists
  parse and print canonically; a disabled feature's forms never appear; and
  the static audit below passes over a few hundred programs and refuses
  every planted one. It counts its checks and fails if it ran none.
- **A static audit** of printed programs: every `/` and `%` divides by a
  `| 1)` guard and every `<<` / `>>` amount is masked. The audit reads the
  printed text, not the model, so it does not share the generator's
  mistakes about what it built.
- **The corpus check**, `cryosmith corpus-check`: K seeds are generated,
  each built at `--opt-level=0` and `--opt-level=2` with the compiler named,
  and both run. A program passes when both builds succeed, both exit 0 and
  both print the same single hash line. It is CryoSmith's acceptance test,
  not CryoGate's differential lane, and it is the one place CryoSmith runs a
  compiler.
- **Plants prove the checks can refuse.** `--plant unguarded-division`
  generates `(a / (b & M))`, without the `| 1`. The static audit must refuse
  every planted program that divides, and the corpus check must report the
  plant as a failure. A plant is never on by default and the header names it.
