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

- **Arenas, not pointers.** A `Program` holds arenas of structs, fields,
  variables, expression nodes, statement nodes and functions, and one of
  index runs (a call's arguments, a function's parameters, a struct
  literal's entries). A node names its children by index. A block is a chain
  of statements, each naming the next (`-1` ends the chain), so no node owns
  another and the whole model is plain data.
- **Types** are the eight integer types, `boolean`, `void` (what a method
  that writes its receiver returns) and a generated struct, named by its
  index.
- **Expression nodes** carry their type and one of: an integer or boolean
  literal; a type's named limit (`i8::MIN`, `u32::MAX`); a variable read; a
  unary operator; a binary operator; a wrapping method call
  (`a.wrapping_add(b)`); a cast (`a as T`); an implicit widening (a narrower
  value where its wider type is expected, printed bare); an `if` expression;
  a call of a free function or a static method; a method call on a place
  (`v2.f1.m0(a)`); a field (`v2.f1`); a struct literal (`S1 { f2: a, f0: b }`).
- **Statement nodes:** a declaration (`const` / `mut`), an assignment (plain
  or compound) to a place - a `mut` local, or a field reached from one or
  from a `mut &this` - an observation (`observed = mix(observed, ...)`), a
  call evaluated for its effect (a method that writes its receiver), `if` /
  `else`, a loop (three shapes, below), a loop exit (`if (c) { break; }`,
  `if (c) { continue; }`) and a return, early (`if (c) { return e; }`) or
  final.
- **Structs** `S<N>` have fields `f<N>` of an integer type, `boolean` or an
  earlier struct (so none contains itself), some with a literal default.
- **Functions** are free functions `h<N>`, `main`, and a struct's methods
  `m<N>`: static, or taking `&this` or `mut &this`. Each has numbered
  parameters, a return type, a body and a **cost**: the most work one call
  can do (section 3.3).
- **The printer** walks the model once. Every binary expression is
  parenthesised, so precedence never decides a meaning, and every statement
  and field has its own line. A struct's methods are printed inside its
  body, and every struct before the free functions.

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
| call | `h2(a, b)`, `S1::m0(a)` | a function or static method returning `T` (section 3.3) |
| field | `v3.f1`, `this.f0.f2` | a field of type `T`, reached from a struct in scope |
| method call | `v3.m1(a)`, `this.f0.m2()` | a `&this` method returning `T`, on a place of its struct |

A boolean is a literal, a variable, a comparison of two integers of one
type, a comparison of two integers of **different** types (they compare by
their true values), `&&`, `||`, `!`, `==` / `!=` of two booleans, or a field,
call or method call returning one.

A struct value of `S` is a struct literal (`S { f1: a, f0: b }`: its fields
in a shuffled order, a field with a default left out half the time), a
variable, a field of type `S`, a call or `&this` method call returning `S`,
or an `if` expression over two of these. A literal names its type, so a
struct expression always reads a name.

At depth 0 only leaves are chosen (a field is a leaf, and so is a struct
literal of leaves). **Widening** (`v` of type `u8` where a `u32` is wanted)
is generated only where the language converts implicitly: an initializer, an
assignment, a call's argument, a return value and a struct literal's field.

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

**Constants.** A parameter does type literals combined with each other: an
argument made only of literals takes its parameter's type when every value
it computes on the way is one the parameter holds. With `constants`, an
integer argument - of a call, a method call or a wrapping method - is now
and then such an expression: `(a & b)`, `(a | b)` or `(a ^ b)` of two
literals in `[0, M]` (`M` the type's largest literal), and for a signed type
sometimes its negation, `(-(a & b))`. Every intermediate is in the type's
range, so the argument is typed by the parameter and never refused.

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
  `p<N>`, counters `n<N>`, functions `h<N>`, structs `S<N>`, fields `f<N>`,
  methods `m<N>`); only `mut` names are assigned, and no loop counter is. No
  name is spelled like a type: a local `i8` would stand in front of the type
  in `i8::MIN`, and a function `f32` in front of `f32`. A struct has at most
  four fields, so no field is `f32` either.
- **Receivers.** A field is written only through a `mut` local or a
  `mut &this`; a method taking `mut &this` is called only on such a place.
  A parameter, a `const` local and a `&this` are read and never written. A
  static method is called through its struct (`S1::m0()`), never on a value.
  `this` is never assigned whole.
- A struct literal writes every field that has no default.
- A function's body ends with a `return` of its type, or with nothing when
  it returns `void`; `main` never returns early, so its final print always
  runs.
- The corpus check (section 7) is the proof that this list is complete:
  every refusal is either a generator defect or a compiler finding.

### 3.3 Terminates, within a budget

- **Bounded loops.** Every loop counts a counter from 0 to a literal trip
  count (at most 6) and the body never assigns the counter:
  `for (mut n0: u16 = 0; n0 < 4; n0 = n0 + 1)`, `while (n1 < 3) { n1 = n1 + 1; ... }`
  (the increment comes first, so a `continue` cannot skip it) and
  `for (n2 in 0..5)`. Loops nest at most three deep.
- **No recursion.** Functions are generated in one order - every struct's
  methods, then `h0`, `h1`, ..., then `main` - and each calls only functions
  generated before it: the call graph is acyclic.
- **A cost model.** A statement costs its expressions' nodes (a store's
  target is written, not read, and costs nothing); a loop costs its trip
  count times its body; an `if` the dearer arm; a call the callee's cost.
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
- A struct is observed field by field, nested structs through their own
  fields (`v2.f1.f0`); a struct a call returns is bound to a local first, so
  the call runs once.
- Each block ends by observing every name it declared, a function body ends
  by observing its parameters (and a method's, its receiver), and `main` ends
  by observing one call of each helper the cost model allows, so every value
  a program computes reaches the hash. A nested block spends at most half the
  statements left, so no function is one untaken branch.
- **No expression both writes a struct and reads it.** A method taking
  `mut &this` returns `void` and is called only as a statement of its own,
  so the order in which an expression's operands are evaluated never
  decides a struct's value. (`observed` is the exception the stage-1 forms
  already make: a call inside `mix(observed, ...)` may observe, and so
  change `observed`, while the same statement reads it.)
- **Open guarantee: operand evaluation order.** That exception's hash
  depends on the order a call's arguments and an operator's operands are
  evaluated in: `observed = mix(observed, (h1(v0) as u64))` reads
  `observed` before `h1` changes it only when the arguments are evaluated
  left to right. The pin and the current compiler both evaluate left to
  right, but the spec does not yet promise an order; the question is open.
  Until it is answered CryoSmith keeps generating these statements, and a
  compiler that changed the order would print a different hash at every
  optimisation level alike, so the corpus check (which compares -O0 with
  -O2) would not see it. If the spec leaves the order unspecified, every
  call is bound to a local before the statement that observes it.

### 3.5 Reproducible from the seed

- One seed and one set of options give byte-identical text, on every host:
  CryoSmith reads no clock, no environment and no file.
- The random stream is the standard library's `Rng::from_seed`, whose
  sequence is fixed for every version of the library.
- **Each function has its own stream**, seeded from the program seed and the
  function's index, so a change in how one function spends random draws does
  not reshuffle the next function's choices (it still sees the earlier
  functions' signatures and costs, which its calls use). The structs and
  their fields come from a stream of their own, and each method from one
  seeded with its place among the methods.
- **The function count shapes nothing before `main`**: a program generated
  with `--functions 3` is the same structs and methods and the first three
  helpers of the same seed's program with more, plus its own `main`.
- **A new stage leaves the old forms' draws alone.** A form a stage adds
  takes no random draw where its feature is off or nothing it needs exists,
  so with `--features default,no-structs,no-constants` a seed generates the
  text the version-1 generator did, header aside.
- The header records the **generator version**, raised whenever one seed may
  produce different text, so a seed filed in a bug report names the
  generator that reproduces it.

## 4. Features and stages

Features are named, and enabled as a comma-separated list. `default` is
every feature of the current stage; `no-<name>` removes one.

| stage | features |
|---|---|
| 1 | `integers` (always on), `booleans`, `wrapping`, `guarded`, `bitwise`, `shifts`, `division`, `casts`, `widening`, `if-expressions`, `branches`, `loops`, `loop-exits`, `calls`, `early-returns` |
| 2 (this one) | `structs`: one to three structs with integer, boolean and nested struct fields, defaults, struct literals, field reads and writes, structs as parameters, returns and locals, and inline methods (static, `&this`, `mut &this`); `constants`: arguments made only of literals |
| 3 | `generics`: generic functions and structs, `where` bounds, overload sets |
| 4 | `references`, `moves`: `&` / `mut &`, receivers, move-checked bindings |
| 5 | `drop`: a drop log folded into the hash, the expected drop order |
| 6 | `closures`: capturing and not, passed to function-typed slots |
| 7 | `traits`: default methods, implementations, calls through a bound |
| 8 | `async` |

A new stage adds features; it never changes what an existing seed and
feature list generate without raising the generator version.

## 5. Command line

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
  // cryosmith 3 seed 42 features booleans,wrapping,... functions 4 size 24
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
  both print the same single hash line; the summary counts the distinct
  hashes, since a hash every program shares is a stuck one. It is
  CryoSmith's acceptance test, not CryoGate's differential lane, and it is
  the one place CryoSmith runs a compiler; it moves into CryoGate once
  CryoGate's differential driver exists.
- **Plants prove the checks can refuse.** `--plant unguarded-division`
  generates `(a / (b & M))`, without the `| 1`. The static audit must refuse
  every planted program that divides, and the corpus check must report the
  plant as a failure. A plant is never on by default and the header names it.

## 8. Architecture

The source is one directory per concern under `src/`, each with a
`_module.cryo` that declares its files and exports the layer's surface. The
layers, lowest first:

| layer | owns |
|---|---|
| `model/` | the program tree: `ValueType`, the operators, expression and statement nodes, variables, fields, structs, functions, and `Program`'s arenas |
| `random/` | `RandomStream`: every draw, and the seed of each part's stream (section 3.5) |
| `configuration/` | `Feature` and `FeatureSet`, `GenerationOptions`, `Plant`, `GENERATOR_VERSION`, and `GenerationArguments`, the options as a command line spells them |
| `typing/` | `Environment`, the names in scope and what each allows (section 3.2), and `TypeChoice`, drawing a type |
| `guarantees/` | each rule of section 3 stated once: `ArithmeticGuard`'s masks (3.1), `CostBudget` and the loop limits (3.3), `Observation`'s hash and output line (3.4) |
| `generate/` | `Generator`: the struct and its entry point in `generator.cryo`, and one file per stage's forms - `functions`, `control_flow`, `scalars`, `observations`, `structs`, `constants` |
| `emit/` | `Printer`: the header, the prelude, items, statements and expressions as text |
| `selftest/` | `SelfTest` and the static audit (section 7) |
| `corpus/` | `CorpusCheck`: building and running programs with a named compiler |
| `cli/` | `Cli`: the commands, the usage text, the exit codes |

`main.cryo` only hands the command line to `Cli`.

**Dependency rules.**

- A layer imports only layers above it in the table, and the rank is strict
  where it matters: `model` and `random` import only the standard library;
  `configuration` imports `random`; `typing` imports `model`, `random` and
  `configuration`; `guarantees` imports `model` alone; `generate` imports
  the five layers above it; `emit` imports `model`, `configuration` and
  `guarantees`, and never reads a random stream; `selftest` and `corpus`
  import `generate`, `emit` and what those export; `cli` imports any layer,
  and no layer imports `cli`.
- Another layer is imported through its aggregator
  (`import cryosmith::model::{ Program };`), never by one of its files'
  paths, so a layer may move a declaration between its files without its
  users noticing. Files of one layer import each other by path.
- What an aggregator exports is the layer's surface and is `public`. A
  member that only other files of its layer use is `internal` - the
  `Generator`'s and `Printer`'s methods, split across files with
  `implement struct` blocks, are - and one that only its own file uses is
  `private`. Every public item carries a doc comment.
- A rule stated in section 3 has one home in `guarantees/`. The static
  audit is the exception on purpose: it spells the guards out itself, so a
  wrong mask in `guarantees/` is refused rather than shared.

**Adding a stage or a feature.**

1. Name the feature in `configuration/features.cryo`: its variant, name,
   stage and bit; `default` takes it from there.
2. Give the model what it needs: a new `ExpressionKind` or `StatementKind`
   variant, a new item. The printer must then print it (`emit/`), or the
   build stops at the non-exhaustive `match`.
3. Write the forms in a new `generate/<stage>.cryo`, an `implement struct
   Generator` block listed in `generate/_module.cryo`. A form is a weight in
   the dispatch of the type it produces (`scalars`, `structs`,
   `control_flow`) that is 0, and so takes no draw, where the feature is off
   or nothing it needs exists - which is what keeps every old seed's text
   (section 3.5).
4. A new safety rule goes in `guarantees/`, and section 3 says why it holds.
5. `selftest/features.cryo` gets the feature's switch check: some program
   spells it with the feature on, none with it off. Raise
   `GENERATOR_VERSION` when an existing seed's text changes.

**Proving a change preserves output.** A change meant to move no text is
checked by generating the same seeds before and after and comparing the
files' bytes: the binary before the change is copied aside, `cryosmith
batch` writes 500 seeds under several feature lists (`default`, `default,
no-structs,no-constants`, every feature named, and smaller ones), and the
two corpora must be byte-identical. A one-character change to the printer
must make that comparison fail, or the comparison is not reading what it
claims to.
