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
  that writes its receiver returns), a generated struct or enum, named by
  its index, and a generic function's type parameter `T`.
- **Expression nodes** carry their type and one of: an integer or boolean
  literal; a type's named limit (`i8::MIN`, `u32::MAX`); a variable read; a
  unary operator; a binary operator; a wrapping method call
  (`a.wrapping_add(b)`); a cast (`a as T`); an implicit widening (a narrower
  value where its wider type is expected, printed bare); an `if` expression;
  a call of a free function or a static method; a generic call, its type
  argument written, a placeholder or left out (`g0::<u16>(a, b)`,
  `g0::<_>(a, b)`, `g0(a, b)`); a method call on a place
  (`v2.f1.m0(a)`); a field (`v2.f1`); a struct literal (`S1 { f2: a, f0: b }`),
  an instance's spelling its type argument the same three ways; an enum's
  variant (`E0::V1`); a `match` expression. A `match`, expression or
  statement, names a run of **arms** in an arena of its own, each a
  pattern (a variant, or `_`) and a body (an expression, or a block).
- **Statement nodes:** a declaration (`const` / `mut`), an assignment (plain
  or compound) to a place - a `mut` local, or a field reached from one or
  from a `mut &this` - an observation (`observed = mix(observed, ...)`), a
  call evaluated for its effect (a method that writes its receiver), `if` /
  `else`, a loop (three shapes, below), a loop exit (`if (c) { break; }`,
  `if (c) { continue; }`) and a return, early (`if (c) { return e; }`) or
  final.
- **Structs** `S<N>` have fields `f<N>` of an integer type, `boolean` or an
  earlier struct (so none contains itself), some with a literal default.
  **Generic structs** `B<N><T>` are held apart from them; each **instance**
  (`B0<u16>`) is a struct of its own in the structs' arena, its fields the
  generic struct's with `T` replaced, so every form that makes, reads,
  writes or observes a struct serves an instance unchanged. A type's name
  is the program's to spell, since an instance's names its argument.
- **Functions** are free functions `h<N>`, generic functions `g<N><T>`,
  `main`, and a struct's or generic struct's methods `m<N>`: static, or
  taking `&this` or `mut &this`. Functions of one owner and number are
  one **overload set**, printed with one name. Each has numbered
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
that operand's type, and so does an operand made only of literals
(`x + (12 & 63)`), which is its exact value - but only where the operand's
type holds that value; one it does not hold stays an `i32` or wider, and
the operation widens. Some places give even a bare literal no type (a
method's receiver, a cast's operand, a shifted value, an operand of `~`, an
`if` expression's arms, the left side of a comparison). So every composite
expression the generator writes **reads a name**: its first operand does,
and every masked operand does, and its type is never decided by a value. Every scope has one: `observed`, the program's
global hash state, is always readable, a named limit (`u8::MAX`) is a typed
constant, and a function has at least one parameter.

**Constants.** A parameter types literals combined with each other: an
argument made only of literals is its exact value, and takes its
parameter's type when the parameter holds that value. With `constants`, an
integer argument - of a call, a method call or a wrapping method - is now
and then such an expression: `(a & b)`, `(a | b)` or `(a ^ b)` of two
literals in `[0, M]` (`M` the type's largest literal), and for a signed type
sometimes its negation, `(-(a & b))`. Every intermediate is in the type's
range, so the argument is typed by the parameter and never refused.

**Generics.** With `generics`, one to three generic functions
`g<N><T>` are declared after the structs' methods and before `h0`: the
first two parameters are `T`, the rest `T` or a value type, and the
function returns `T`. Half are bounded `where T: Copy` and half `where T:
Eq + Copy`. In the
body a `T` value is a read of a `T` name, an `if` expression over two, or a
call at `T` of an earlier generic function or generic struct's static
method (an `Eq`-bounded callee only
from an `Eq`-bounded body); under `Eq`, a boolean may compare two `T`
values with `==` or `!=`. A call writes its type argument,
`g0::<u16>(a, b)`: anywhere a value of that type is wanted, at an integer
type or `boolean` for an `Eq`-bounded function and at any value type for an
unbounded one, and `main` observes one call of each.

**Generic structs.** With `generic-structs`, one or two generic structs
`B<N><T>` are declared after the structs' methods and before the generic
functions: the first field is `T`, the rest `T` or a scalar, and a scalar
field sometimes has a literal default. Each is instantiated at one to three
type arguments - a scalar, a declared struct, or an earlier instance, so
`B1<B0<u32>>` and `B0<S1>` both occur - and every instance is a type a value
may have, drawn as a declared struct is: a local, a parameter, a return
type, a field of nothing (an instance is never a declared struct's field),
a generic function's type argument. A literal writes the type argument,
`B0::<u16> { f1: a, f0: b }`; a field of an instance is read, written and
observed as any struct field is.

**Generic methods.** With `generic-methods`, each generic struct has one
to three methods written in its body, each static, taking `&this` or
taking `mut &this`. They belong to the struct's **own instance**,
`B<N><T>`, the struct at its own parameter, made after every other
structure and the type of no value but `this`; so inside a method a `T`
field of `this` is a `T` value, read, passed, returned and written as a
generic function's are, and never observed. A static method takes a `T`
first and returns `T`, and is called as a generic function is, its type
argument written, a placeholder or left out: `B0::<u16>::m0(a)`,
`B0::<_>::m0(a)`, `B0::m0(a)`. A method taking a receiver is called on a
place of any instance of its struct, `T` standing for that instance's
argument in its parameters and its result: `v1.m1(a)` with `v1: B0<u16>`
takes and returns `u16` where `m1` says `T`. One taking `mut &this` returns
nothing and is called as a statement on a writable place, as a declared
struct's is. A method calls only earlier ones, its own struct's on `this`
among them. Each method is bounded `where T: Copy`, as every generic
function is, so its body may pass, store and select a `T` value as often
as it likes (a type parameter is otherwise not `Copy`, and each of those
moves it); every type a value has is `Copy`, so any instance may call it,
and a generic function may call a static one at its own `T`.

Inside a generic struct's method, a literal of that same generic struct is
spelled as any instance's literal is: written, with `_`, or left to
inference. Left out, its type argument is the one its fields bind, which
need not be the body's own, so `B0::m0(B0 { f0: x })` calls the static
method of `B0<B0<u16>>` for an `x: u16`.

**Overload sets.** With `overloads`, four times in ten a free function
`h<N>` or a declared struct's method `m<N>` is followed at once by one or
two more functions spelled with its name and taking the same receiver,
each with its first member's parameters with one added, one removed or one
retyped, and a return type of its own; a member whose parameters some
member already has is not made. A call of the name passes deciding
values, exactly each parameter's type and reading a name, so its
arguments' types are one member's parameter types and no other's (section
3.2). A member's body never calls its own set, which is not whole while it
is generated. A generic function and a generic struct's method are never
overloaded: a call two generic overloads, or a generic and a plain one,
both accept is ambiguous, and nothing generated would be certain to avoid
that.

**Enums and `match`.** With `enums`, one or two enums `E<N>` with two to
four unit variants `V<N>` are declared first, before the structs, from a
stream of their own. An enum is a type a value may have - a local, a
parameter, a return type, a generic function's type argument - and every
enum is `Copy`, its variants carrying nothing. A value of one is a variant
(`E0::V1`), a variable, a call, a method call or an `if` expression, so it
always reads a name. A `match` over a value of some enum writes an arm for
one to all of its variants, in a shuffled order, then `_` when a variant
is left (section 3.2). As a statement its arms are blocks. As an
expression, of an integer, boolean or enum type, its arms are values of
that type that read a name, since an arm, as an `if` expression's, gives a
bare literal no type. A `match` expression is typed by its arms, so it
stands wherever a value of its type that reads a name may: the whole value
of a place, an operand, a receiver, another `match`'s arm or subject. An
enum value is observed by passing `match (e) { E0::V0 => { 1 } E0::V1 => {
2 } }`, its variant's number plus one, to `mix`, whose `u64` parameter
types the literal arms.

**Payload variants.** With `enum-payloads`, each variant carries none, one
or two scalar values, `V1(u8, boolean)`, drawn from the enums' own stream;
an enum stays `Copy`, as each of its payload's types is. A variant value
gives a value of each payload type, `E0::V1(v2, (p0 > 3))`, generated as a
call's argument is. A `match` arm over a payload variant binds every
value, `E0::V1(b0, b1)`; the bindings `b<N>` are numbered per function,
read only, and in scope in their arm alone. Every binding reaches the
hash: a statement's arm observes them at the end of its block; an integer
expression's arm folds its value and then its bindings by `mix`, from its
position plus one, and converts the fold back to its type
(`E0::V1(b0, b1) => { (mix(mix(mix(2, (v3 as u64)), (b0 as u64)), if (b1)
{ 1 } else { 2 }) as i32) }`; no operator, so no feature, is needed), since
an observation there would run inside the value an outer
observation folds, `observed = mix(observed, h0(..))`, after that
observation read `observed`, and be overwritten; any other expression's arm
(a boolean's or an enum's) observes them by statements before its value
(`E0::V1(b0) => { observed = mix(observed, (b0 as u64)); v3 }`), which the
same overwrite can lose. The self-test's binding audit holds every binding
to one of these. The
printer's observation folds a payload into its variant's number,
`E0::V1(b0, b1) => { mix(mix(2, (b0 as u64)), if (b1) { 1 } else { 2 }) }`.

**Generic enums.** With `generic-enums`, one or two generic enums
`G<N><T>` follow the declared enums, from the same stream; a generic
enum's `T` is carried only by payloads, so without `enum-payloads` none is
declared. Variant 1
carries a payload of type `T` first, and every other payload value is of
type `T` half the time, otherwise a scalar. Each is instantiated at one or
two scalar type arguments; an instance is an enum of its own whose
variants are the generic enum's with `T` replaced, so its values, matches,
bindings and observation are an enum's. A pattern names the variant
through the generic enum alone, `G0::V1(b0, b1)`. A variant value with a
payload of type `T` spells its type argument as `inferred-type-arguments`
draws it (`G0::<u16>::V1(a, b)`, `G0::<_>::V1(a, b)`, `G0::V1(a, b)`),
giving a deciding value for each such payload when it is inferred; one
with no such payload writes it, `G0::<u16>::V0`, since nothing in it says
which instance.

**Nested patterns.** With `nested-patterns`, a declared enum's payload
value is of an enum declared before it half the time, `V2(E0, u8)`, and a
`match` arm's pattern gives each payload value an element of its own: a
binding, `_` a fifth of the time, or, for a value of an enum, a variant
pattern of that enum two times in five, itself with elements,
`E1::V2(E0::V1(b0, _), b1)`. A variant pattern matches only its variant,
so an arm holding one is refutable, and its `match` then ends with `_`
(section 3.2) even when every variant has an arm. The printer's
observation folds a payload value of an enum through that value's own
variant number. Every binding is still observed; a `_` binds nothing.

**Inferred type arguments.** With `inferred-type-arguments`, a generic
call or an instance's literal leaves its type argument to inference four
times in ten (`g0(a, b)`, `B0 { f0: a }`) and writes the placeholder twice
(`g0::<_>(a, b)`, `B0::<_> { f0: a }`). Inference reads the type argument
off the values given for `T`, so there every such value is a **deciding
value**: exactly the type argument, reading a name. A narrower value,
widened where it is written, would bind `T` to its own narrower type,
since inference never widens; a literal, or an expression made only of
literals, decides no type parameter. An instance's first field is of type
`T` and has no default, so its literal always gives one. The other
parameters and fields are generated as before, widening and constants
included.

**Closures.** With `closures`, a statement of a function that is not
generic may declare a `const` local holding a closure of its function
type, `const v3: (u8, i32) -> boolean = (q0: u8, q1: i32) -> boolean {
return .. };`: zero to two scalar parameters `q<N>`, numbered per
function, and a scalar return type. A function type is a `ValueType` of
its own, each signature written once in the program (`Program::function_type`),
so two closures of one signature have one type. The body returns one value,
generated in a scope of its own (`Environment::enter_closure`) that holds
its parameters and nothing of the enclosing function, not even `observed`,
so it captures nothing and its function type may hold it; it may call the
functions generated before it. A closure local is called wherever a value
of its return type is wanted, `v3(a, b)`, with an argument of each
parameter's type generated as a call's argument is, and is observed by one
such call where its block observes its locals. It is never `mut`, so it is
never assigned.

A `match` statement's arm may assign the local its subject reads. Its
bindings are never written `mut` and their payloads are `Copy`, so each is
a copy taken when the arm is entered (docs/cryo.md, enum destructuring)
and keeps the value the arm was chosen with; `match (v) { E::X(a) => { v =
E::Y(1); .. a .. } }` observes the `a` of `E::X`.

**Kept off.** Nothing.

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
  `p<N>`, counters `n<N>`, functions `h<N>` and `g<N>`, structs `S<N>` and
  `B<N>`, fields `f<N>`, methods `m<N>`); only `mut` names are assigned, and no loop counter is. No
  name is spelled like a type: a local `i8` would stand in front of the type
  in `i8::MIN`, and a function `f32` in front of `f32`. A struct has at most
  four fields, so no field is `f32` either.
- **Receivers.** A field is written only through a `mut` local or a
  `mut &this`; a method taking `mut &this` is called only on such a place.
  A parameter, a `const` local and a `&this` are read and never written. A
  static method is called through its struct (`S1::m0()`), never on a value.
  `this` is never assigned whole.
- A struct literal writes every field that has no default.
- **Generic bodies.** A `T` value is never observed, cast or combined by
  an operator: nothing converts a `T` to a `u64`, and no `T` local is
  declared. The one `T` place assigned is a `T` field of a `mut &this`, and
  only whole, never with a compound operator. It is compared only under `where T: Eq`, and a generic
  function is instantiated only at a type its bound admits - an `Eq`-bounded
  one never at a struct, which does not implement `Eq`.
- **Moves.** A type parameter is not `Copy` unless a bound says so, and
  each pass, store or selection of a value that is not `Copy` moves it.
  Every generic function and generic struct method is bounded by `Copy`
  (`where T: Copy`, or `where T: Eq + Copy`), so a body uses a `T` value
  as often as it likes, and every type it is instantiated at is `Copy`.
  The self-test's bound audit (section 7) refuses a generic definition
  that names a `T` parameter twice, or once inside a loop, without the
  bound; it reads the text, since the compiler accepts such a function
  when every instance's type is `Copy` and so cannot be the check.
- **Every `match` is exhaustive and every arm reachable.** A `match`
  covers each variant once, by its own arm or by a `_` written last, and
  writes `_` only when a variant is left
  (`guarantees/exhaustiveness.cryo`) or an arm's pattern nests a variant
  pattern, which leaves the rest of its variant to `_`
  (`Program::elements_refutable`): no variant is uncovered, no arm
  follows `_`, and no variant has two arms.
- **Overload resolution is certain.** A call of an overloaded name
  selects the member its arguments fit, and no implicit conversion makes
  one fit: a narrower integer is not widened, and a literal where the
  members' parameters differ is an `i32`. So the members of a set differ
  in their parameter count or in some parameter's type
  (`guarantees/overloading.cryo`), and every argument of a call of an
  overloaded name is a deciding value, of exactly its parameter's type and
  never a literal or made only of literals: one member fits, and no other.
- **Inference is certain.** A call or literal that leaves its type
  argument to inference gives `T` only deciding values (section 2), so
  each of them has the type argument's type and they agree; nothing else
  the call or literal holds names a type parameter. One that writes its
  type argument may pass anything that converts to it.
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
  methods, then every generic struct's, then `g0`, `g1`, ..., then `h0`,
  `h1`, ..., then `main` - and
  each calls only functions generated before it: the call graph is acyclic.
- **A cost model.** A statement costs its expressions' nodes (a store's
  target is written, not read, and costs nothing); a loop costs its trip
  count times its body; an `if` the dearer arm; a call the callee's cost;
  a closure's body is measured on a budget of its own when it is made,
  and that cost is charged at every call of it.
  A call is generated only while the caller's cost stays under its limit, so
  every program does a bounded amount of work, known at generation time.

### 3.4 Deterministic output

- No input, no clock, no address, no environment, no thread: the only
  output is the final hash line.
- `observed` is a global `u64`. An observation folds a value into it with
  `mix`, defined in the program as
  `(state.wrapping_multiply(1099511628211) ^ value).wrapping_multiply(1099511628211).wrapping_add(2654435769)`.
  For a fixed value it is a bijection of the state and for a fixed state
  one of the value, and folding a value equal to the state still depends
  on the state: a program that observes `observed` itself (a generic call
  handed `observed` twice returns it) keeps every earlier observation,
  where a plain exclusive-or of the two would cancel them. An integer is
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
- **Operand evaluation order is guaranteed.** That exception's hash
  depends on the order a call's arguments and an operator's operands are
  evaluated in: `observed = mix(observed, (h1(v0) as u64))` reads
  `observed` before `h1` changes it because the language evaluates
  operands left to right - a binary operator's left operand first, a
  call's arguments in the order written, a method call's receiver before
  its arguments (docs/cryo.md section 5.8, "Evaluation Order"). So the
  hash a program prints is fixed by the source, and these statements stay.
  A compiler that changed the order would print a different hash at every
  optimisation level alike, which the corpus check (comparing -O0 with
  -O2) cannot see; the test project `evaluation_order_left_to_right` is
  what refuses it.

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
  seeded with its place among the methods. The generic functions' count
  comes from a stream of its own, and each generic function from one
  seeded with its number. The generic structs and their instances come
  from one stream of their own; how many methods each has and how each
  takes its receiver from another, and each such method from one seeded
  with its place among them. The enums come from one stream of their
  own. Whether a function gains an overload set
  comes from a stream keyed by its name, and each member from one keyed by
  the name and the member's place in the set.
- **The function count shapes nothing before `main`**: a program generated
  with `--functions 3` is the same structs and methods and the first three
  helpers of the same seed's program with more, plus its own `main`.
- **A new stage leaves the old forms' draws alone.** A form a stage adds
  takes no random draw where its feature is off or nothing it needs exists,
  so with every later stage's features removed a seed generates an earlier
  generator's text, header aside. `default,no-nested-patterns` generates
  version 14's text, `default,no-nested-patterns,no-generic-enums`
  version 13's, and `default,no-enum-payloads` (which declares no
  generic enum either) version 12's. `default,no-enums` generates version 9's
  text, `default,no-overloads,no-enums` version 8's, and with both added
  to each list,
  `default,no-structs,no-constants,no-generics,no-generic-structs,no-inferred-type-arguments,no-generic-methods`
  version 1's, `default,no-generics,no-generic-structs,no-inferred-type-arguments,no-generic-methods`
  version 3's, `default,no-generic-structs,no-inferred-type-arguments,no-generic-methods`
  version 4's, `default,no-inferred-type-arguments,no-generic-methods`
  version 5's, and `default,no-generic-methods` version 6's - for version
  7 and earlier, once the text is rewritten back in the three places
  version 8 alone changed: `mix`'s body, and a generic function's bound,
  `where T: Copy` (dropped) and `where T: Eq + Copy` (written `where T:
  Eq`). Version 8 also lets a generic function call a generic struct's
  static method at its own `T`, so with `generic-methods` on a seed's
  generic functions may draw differently.
- The header records the **generator version**, raised whenever one seed may
  produce different text, so a seed filed in a bug report names the
  generator that reproduces it.

## 4. Features and stages

Features are named, and enabled as a comma-separated list. `default` is
every feature of the current stage; `no-<name>` removes one.

| stage | features |
|---|---|
| 1 | `integers` (always on), `booleans`, `wrapping`, `guarded`, `bitwise`, `shifts`, `division`, `casts`, `widening`, `if-expressions`, `branches`, `loops`, `loop-exits`, `calls`, `early-returns` |
| 2 | `structs`: one to three structs with integer, boolean and nested struct fields, defaults, struct literals, field reads and writes, structs as parameters, returns and locals, and inline methods (static, `&this`, `mut &this`); `constants`: arguments made only of literals |
| 3 | `generics`: generic functions over one type parameter, `where T: Copy` or `where T: Eq + Copy`, called with the type argument written; `generic-structs`: generic structs and their instances at written type arguments, as the types of values (section 2); `inferred-type-arguments`: calls and instance literals that leave the type argument to inference; `generic-methods`: a generic struct's static, `&this` and `mut &this` methods, called on its instances; `overloads`: free functions and methods sharing a name, each call choosing exactly one |
| 4 | `enums`: enums with unit variants as the types of values, and `match` over them, as a statement and as an expression, with qualified variants and wildcards (section 2); `enum-payloads`: variants carrying scalar values, constructed with them and matched with a binding for each; `generic-enums`: generic enums whose payloads may be of type `T`, and their instances at scalar type arguments; `nested-patterns`: payload values of earlier enums, and `_` and variant patterns inside a payload's pattern. The stage is complete |
| 5 (this one) | `closures`: closures that capture nothing, held by `const` locals of their function type and called (section 2). Still to come: closures capturing locals, closures passed to function-typed parameters and returned, closures in generic bodies |
| 6 | traits, classes and dynamic dispatch: implementations, default methods, calls through a bound and through a trait object |
| 7 | ownership: moves, drops (a drop log folded into the hash), references, `Box` and `Rc`, `Option` and `Result` |
| 8 | strings, arrays and collections |

Section 9 places each stage among the rest of the work, the tools that
run what the stages generate.

A new stage adds features; it never changes what an existing seed and
feature list generate without raising the generator version.

## 5. Command line

```
cryosmith generate --seed N [options] [--out FILE]
cryosmith batch --first-seed N --count K --out-dir DIR [options]
cryosmith features
cryosmith version
cryosmith selftest
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
- **Exit codes:** 0 done; 1 an output could not be written, or the
  self-test failed; 2 a usage error.
- **The header** (default on) is one comment line, the same for the same
  seed and options wherever the file goes:

  ```
  // cryosmith 5 seed 42 features booleans,wrapping,... functions 4 size 24
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
- **The corpus check** is CryoGate's, `cryogate corpus-check`: CryoSmith
  writes K seeds' programs (`batch`), each is built at `--opt-level=0` and
  `--opt-level=2` with the compiler named, and both run. A program passes
  when both builds succeed, both exit 0 and both print the same single hash
  line; the summary counts the distinct hashes, since a hash every program
  shares is a stuck one. CryoSmith generates and never runs a compiler;
  checking what it generated is the gate runner's, and the output contract
  of section 5 is what the two share.
- **Plants prove the checks can refuse.** `--plant unguarded-division`
  generates `(a / (b & M))`, without the `| 1`. The static audit must refuse
  every planted program that divides, and the corpus check must report the
  plant as a failure. `--plant uncopied-type-parameter` prints every
  generic function and method without its `Copy` bound; the bound audit
  must refuse such a program exactly when the same seed's unplanted text
  has a generic definition that uses a `T` value more than once. A plant
  is never on by default and the header names it.
- **The bound audit** reads every generic signature's `T` parameters and
  counts their uses in its body, a use inside a loop twice; one used more
  than once needs `Copy` in the bound (section 3.2).

## 8. Architecture

The source is one directory per concern under `src/`, each with a
`_module.cryo` that declares its files and exports the layer's surface. The
layers, lowest first:

| layer | owns |
|---|---|
| `model/` | the program tree: `ValueType`, the operators, expression and statement nodes, variables, fields, structs, functions, and `Program`'s arenas and its type names |
| `random/` | `RandomStream`: every draw, and the seed of each part's stream (section 3.5) |
| `configuration/` | `Feature` and `FeatureSet`, `GenerationOptions`, `Plant`, `GENERATOR_VERSION`, and `GenerationArguments`, the options as a command line spells them |
| `typing/` | `Environment`, the names in scope and what each allows (section 3.2), and `TypeChoice`, drawing a type |
| `guarantees/` | each rule of section 3 stated once: `ArithmeticGuard`'s masks (3.1), `CostBudget` and the loop limits (3.3), `Observation`'s hash and output line (3.4), `Overloading`'s distinct signatures and `Exhaustiveness`'s arms (3.2) |
| `generate/` | `Generator`: the struct and its entry point in `generator.cryo`, and one file per stage's forms - `functions`, `control_flow`, `scalars`, `observations`, `structs`, `constants`, `generics`, `generic_structures`, `inference`, `generic_methods`, `overloads`, `enums`, `closures` |
| `emit/` | `Printer`: the header, the prelude, items, statements and expressions as text |
| `selftest/` | `SelfTest` and the static audit (section 7) |
| `cli/` | `Cli`: the commands, the usage text, the exit codes |

`main.cryo` only hands the command line to `Cli`.

**Dependency rules.**

- A layer imports only layers above it in the table, and the rank is strict
  where it matters: `model` and `random` import only the standard library;
  `configuration` imports `random`; `typing` imports `model`, `random` and
  `configuration`; `guarantees` imports `model` alone; `generate` imports
  the five layers above it; `emit` imports `model`, `configuration` and
  `guarantees`, and never reads a random stream; `selftest` imports
  `generate`, `emit` and what those export; `cli` imports any layer,
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

## 9. Roadmap

CryoSmith joins `main` when all twelve items below are done, in this
order. Items 1 to 6 are generator stages (section 4): each adds features,
their forms, the guarantees that keep the forms valid, a switch check in
the self-test, an old-seed reproduction check and a corpus. Items 7 to 12
are the tools that use what the stages generate; most of them belong to
CryoGate, which runs compilers, while CryoSmith stays a generator.

**A compiler bug the corpus finds is fixed, not avoided.** The fix lands on
this branch as any compiler change does: a test of the shape, the pin as
the control, a mutation the test refuses, and `make verify` with its
declarations. The generator is then left generating the shape. A keep-off
(a shape the generator avoids, written under "Kept off" in section 2 with
the bug that needs it) is allowed only while that bug's fix is queued in
the same session, or when the fix is clearly a large project of its own,
which is then put to the maintainer as a question rather than left in
place.

1. **Finish stage 3: overload sets** (done: `overloads`, section 2).
   `model/`: functions of one owner and number share a name.
   `generate/overloads.cryo`: a set's members differ in parameter count or
   in a parameter's type, and a call passes arguments of exactly one
   member's types, never a value that would widen to another's.
   `guarantees/overloading.cryo`: the rule that keeps a call unambiguous.
2. **Stage 4: enums and `match`.** `model/`: enums `E<N>` (and generic
   `E<N><T>`) with unit and payload variants, an enum type in `ValueType`,
   variant constructions, and a `match` node, both a statement and an
   expression, holding arms of patterns. `generate/enums.cryo` and
   `generate/matching.cryo`: values of an enum type, and matches over
   them with qualified variants, payload bindings, nested patterns and
   wildcards. `guarantees/`: every match is exhaustive (each variant
   covered, or a wildcard last) and every binding is used as its type
   allows (observed, or passed where its type is wanted). `typing/`:
   bindings enter an arm's scope and leave with it. `emit/`: enum
   declarations, constructions, patterns. Observation: an enum value is
   observed by a `match` that folds its variant's number and its payload.
3. **Stage 5: closures.** `model/`: function types and closure
   expressions, captures. `generate/`: closures with and without captures,
   passed to function-typed parameters and called. `guarantees/`: a
   closure's cost is its body's, charged at each call; a capture is read,
   never moved twice. `typing/`: a closure's own scope over its captures.
   (In progress: `closures`, closures capturing nothing, is done.)
4. **Stage 6: traits, classes and dynamic dispatch.** `model/`: traits
   with required and default methods, implementations for structs and
   enums, classes, trait objects. `generate/`: calls through a bound and
   through a trait object. `guarantees/`: every call resolves to one
   implementation; the hash is the same whichever way it is dispatched.
5. **Stage 7: ownership.** `model/`: references (`&`, `mut &`) as values,
   `Box`, `Rc`, `Option`, `Result`, types with a drop. `generate/`: moves,
   borrows, early drops. `guarantees/`: no use after a move and no borrow
   outliving its owner, decided by the generator's own ownership state in
   `typing/`; a drop log folded into the hash, so the order of drops is
   checked by both optimisation levels agreeing.
6. **Stage 8: strings, arrays and collections.** `model/`: `Str`,
   `String`, fixed arrays, `Array`, `HashMap`. `generate/`: building,
   indexing within bounds, iterating. `guarantees/`: an index is in range
   by construction (masked against a length the generator knows);
   iteration order is observed only where the collection fixes it.
7. **A differential driver** (CryoGate, `cryogate smith-diff`). Builds one
   program under several configurations - `-O0` and `-O2`, the pin and
   the compiler under test, Windows and Linux (through WSL) - runs each
   under a time limit, and reports a mismatch, a crash, a refusal or a
   hang, naming the seed and the configuration. CryoSmith adds nothing but
   the output contract it already has.
8. **A reducer** (CryoGate, `cryogate smith-reduce`). Shrinks a failing
   program while the same failure reproduces: first by asking CryoSmith
   for fewer functions and a smaller `--size` from the same seed (section
   6), then by deleting lines and subtrees. It drafts the bug file: the
   reduced repro, the configurations that disagree, the seed and header.
   CryoSmith's part is section 6's reduction-friendliness, kept as the
   stages grow.
9. **A crash fuzzer.** Near-valid programs: CryoSmith's `--plant` grows
   into mutations of a valid program (a deleted line, a swapped operand,
   a wrong type), each expected to be refused with a diagnostic, never to
   crash the compiler. CryoGate builds them and reports a crash, a hang or
   an internal error, not a refusal.
10. **A fuzz farm** (CryoGate, `cryogate smith-farm`). Overnight batches
    of the differential driver over consecutive seeds, failures
    deduplicated by their signature (the diagnostic code, the crash site,
    the disagreeing configuration), reduced, and a summary written.
11. **A corpus gate in `make verify`.** A small fixed set of seeds at a
    pinned generator version, checked as `corpus-check` checks them, run
    by verify beside its other gates; a seed that fails is a compiler
    finding, and a new generator version re-pins the set.
12. **The reference interpreter.** An evaluator over the program model
    (section 1) that computes the hash without compiling: an oracle that
    does not share the compiler's mistakes. CryoSmith gains an `evaluate`
    command, or a library CryoGate links, and the corpus check compares
    the compiled programs' hash with the evaluated one.
