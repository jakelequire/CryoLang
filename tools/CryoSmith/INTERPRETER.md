# The reference interpreter

`cryo interpret <file>` runs a Cryo program without generating code. It is
the oracle of CryoSmith's corpus check (DESIGN.md section 9, item 12): a
second executor of the language that shares the compiler's front end and
nothing after it, so a program it runs and a compiled build print the same
`cryosmith-hash` line, or one of them is wrong.

Comparing `-O0` with `-O2` cannot see a mistake both levels make. The
interpreter is written against the language reference, not against code
generation, so it sees three classes the agreement check is blind to:

- **a miscompile shared by every optimisation level** - wrong IR before the
  optimiser runs, such as a `match` binding that aliases its subject where
  the language copies it;
- **a standard library defect both builds link** - the library is the same
  archive at every level, so a miscounting `Rc` passes the agreement check;
- **aliasing and ownership errors** - a read after a move, a double drop, a
  drop that never runs - which a compiled program may survive silently.

It also checks CryoSmith's own guarantees: a generated program that reads
an uninitialised value, indexes out of range, divides by zero or shifts by
the width is refused by the interpreter even where a compiled build happens
to print a hash.

It is slow - a tree walk over the typed program, two orders of magnitude
behind a compiled build - which suits the small programs CryoSmith writes.

## 1. Where it lives

The interpreter is a module of the compiler, `compiler/src/compiler/interpret/`
(namespace `compiler::interpret`), reached by the command `cryo interpret`.
It is part of the compiler because the program it needs - parsed, resolved,
type-checked and monomorphised - exists only inside the compiler, and a
second front end would be a second set of name and type rules to drift.

The command builds the program as `cryo check` does, through the same
multi-module project driver, with the standard library loaded and
type-checked from source, in a compile mode of its own,
`CompileMode::Interpret`:

```
parse -> resolve names -> declare -> resolve types -> type-check bodies
      -> monomorphise -> resolve generic expressions -> validate generics
      -> [interpret main]
```

The mode stops **before** the move check, drop insertion and type lowering.
Drop insertion rewrites the program for code generation (it writes drop
calls and drop flags into the tree), and the interpreter keeps its own
account of moves and drops (section 5); reading a tree that pass had
rewritten would make the interpreter agree with the pass by construction.
The move checker reports, rather than rewrites, but the interpreter does
not run it either: a read after a move is the interpreter's own finding.

The command line:

```
cryo interpret <file> [--stdlib PATH] [--target TRIPLE] [--step-limit N]
```

What the program writes to standard output is written there. The exit code
is the program's own (what `main` returns) when it runs to its end; a
refusal by the front end exits 1, as `cryo check` does; anything the
interpreter itself reports exits with a code of its own (section 7).

## 2. What it reads from the compiler

Everything the interpreter knows about a name comes from the resolver and
the type checker, through the doors they already expose
(docs/resolution-rules.md). It never looks a spelling up, never adds a
finder, and never branches on a name.

| question | answered by |
|---|---|
| which binding a name reads | the identifier's `ResSlot` (a `Res::Local` symbol, a definition) |
| which function a call runs | the call's `CalleePin`, read through the declaration index (`CalleePin::entry`, `DeclarationIndex::entry_decl`) |
| which field a member access reads | the access's `MemberPin` |
| which implementation a trait object calls | the conversion's `ObjectConversionExpr.implementation` and the call's `resolved_trait_slot` |
| which language item a lowering calls | the slot's `ResSlot::Lang` item, through the declaration that claims it |
| what type a value has | the expression's `resolved_type` in the type arena |
| how a generic body runs at a type | the monomorphiser's copy, which the pin names: generics are interpreted as the compiler instantiated them |
| whether a callee is an intrinsic | `CalleePin::intrinsic_kind`, the kind registration put on the entry |

If something the interpreter needs is not recorded by the front end, it is
not reconstructed from text: the construct is reported as not covered
(section 7), and the missing record is raised as a question for the front
end's owners.

## 3. Values

A value is one of:

- **an integer** - its bits and its type (width 8 to 64, signed or not);
  every arithmetic result is reduced to its type's width at the operation
  that produced it, never later;
- **a boolean**;
- **unit**;
- **an aggregate** - a struct's, tuple's or fixed array's members, in
  declaration order, each a value;
- **a variant** - an enum's variant number and its payload's values;
- **an address** - an allocation, a path into it, and the type it points
  at (section 4); references and raw pointers are both addresses, and a
  reference's validity is checked where it is read - except a reference
  to the slice item (`&Slice<T>`), which holds the slice's own two words,
  its first element's address and its length (docs/cryo.md section 2.3):
  it is laid out and read as the slice itself, `&` of a slice gives those
  two words where they lie, and a slice method's receiver is them;
- **a function** - a declaration-index entry, for a named function used as
  a value;
- **a closure that captures nothing** - its lambda node, a function value
  whose body a call runs; a closure that captures is an aggregate, the
  struct the type checker made for it, holding each capture by copy or, for
  a capture by reference, its address;
- **a trait object** - an address and the implementation its table is.

The width of `usize` and `isize` is the target's, read from the target the
build was given.

## 4. Places and memory

Every value lives in a tracked **place**. A place is a slot in an
**allocation**:

- each local and parameter of a running call is an allocation of its own,
  made when the declaration runs and ended when its scope ends;
- each module-level variable is an allocation made, and initialised in
  declaration order, before `main` runs;
- each string literal's bytes are a read-only allocation, ending in a zero
  byte;
- each block a program obtains from the allocator machine rule (section 6)
  is a heap allocation of the size it asked for, ended by `free`.

An allocation holds a tree of cells shaped like its type: a scalar cell, or
one cell per member. Each cell is **uninitialised**, **holds a value**, or
**moved-from**. An address names an allocation, the path of member indices
to a cell, and for an address into an array or a heap block an element
index that pointer arithmetic moves. A heap block is typed by its first
store: a block written as `T*` holds elements of `T`, as many as its byte
size holds at the size the interpreter computes for `T` from the layout
rules (docs/abi.md); a later access as another type is refused rather than
reinterpreted, except for reading any element as bytes when the element
type is `u8`.

Every read and write checks, and reports a violation (section 7) on:

- an access to an ended allocation (a dangling reference or pointer, a
  use after `free`);
- an element index outside the allocation;
- a read of an uninitialised cell;
- a read of a moved-from cell;
- a write through a reference or pointer that is not `mut`;
- `free` of an address that is not the start of a live heap block, or of
  one already freed.

At exit every heap block must have been freed. A block still live is a
**leak** and a violation: this is how the interpreter judges the standard
library's reference counting without trusting it (section 6).

## 5. Execution

The interpreter walks the typed tree of each function body. A call pushes a
frame holding the frame's allocations, binds each parameter to a fresh
allocation, runs the body, and pops the frame - ending its allocations in
reverse order of creation, dropping first what section 5.3 says to drop.

### 5.1 Expressions

Operands are evaluated **left to right**, each completely before the next
(docs/cryo.md section 5.8): a binary operator's left operand first, `&&`
and `||` the right one only when the left does not decide; an assignment's
place before its value; a call's arguments in written order, the receiver
first.

| construct | rule |
|---|---|
| literal | the value its text spells, at the literal's resolved type, reduced as two's complement reads it: the only literal past its type's range the type checker admits is the magnitude of a least value, `-9223372036854775808`, which is negated at once |
| name | a read of the binding's place: a copy for a `Copy` type, a move otherwise (section 5.3) |
| implicit conversion | the type checker records none as a node: a value stored where another integer type is expected (an initializer, an assignment, an argument, a return, a field, an arm) is converted keeping its value, and a conversion that would change the value is a violation |
| `+ - *` | each operand converted to the expression's type keeping its value (docs/cryo.md section 2.9: two operands compute in the type that holds both), computed exactly, then reduced to that type's width (wrapping, docs/cryo.md section 5.1) |
| `/ %` | truncating toward zero; a zero divisor, and `MIN / -1` / `MIN % -1`, are violations |
| `& \| ^ ~` | on the bits at the result type's width |
| `<< >>` | arithmetic right shift on a signed type, logical on an unsigned one; a shift amount not below the width is a violation |
| unary `-` | wrapping negation |
| `== != < <= > >=` | integers compare by their true values whatever their types (docs/cryo.md section 2.9); booleans, addresses and enums as the type checker allows |
| implicit widening | the value kept, at the wider type: an unsigned value widens as unsigned and a signed one as signed |
| `as` between integers | reduced to the target width (sign-extended from a signed source, zero-extended from an unsigned one, truncated when narrowing) |
| `if` expression, ternary | the condition, then the chosen arm only |
| `match` | the subject once; arms tried in order; the first whose pattern matches runs (section 5.2) |
| call | the callee by its pin; a method's receiver first.  In a `&this` or `mut &this` method `this` names the receiver's value where it lies - the type checker types `this` as the owner type, not a reference - reached from the object through as many references and pointers as it takes; a `this` method gets the value moved in |
| field | the member at the pin's index of the place or value |
| struct literal | each field's value in the order written, then defaults for the fields not written, stored in declaration order |
| `&x`, `mut &x` | the address of `x`'s place |
| `*p` | the cell `p` addresses, checked as section 4 says |
| closure | one that captures: the value of its struct, each field initialized in capture order from the capture's value where the closure is made (the type checker's `capture_values`: the binding, or `&binding` for a capture by reference); a call is the struct's `__call__` method, by the call's pin. One that captures nothing: a function value naming the lambda; a call binds its parameters and runs its body |
| trait object call | the method in the object's implementation at the call's slot |
| `?` | the type checker's desugaring (`Try::branch` and `FromResidual::from_residual`), interpreted |

### 5.2 Patterns

A pattern is tested against the subject's value without moving it, and
every alternative of an arm is tested before any binding is made. A
binding is made on entering the arm, by docs/cryo.md section 7.2: in a
subject matched by value, a binding not written `mut` whose type is `Copy`
is a **copy** of the payload - so assigning the subject in the arm does not
change the binding; every other binding (a `mut` one, one of a type that is
not `Copy`, and every binding of a subject reached through a borrow or a
pointer) names the payload where it lies, owning nothing, so moving out of
it moves out of the subject. A nested pattern tests the payload's own
value; `_` matches anything and binds nothing.

### 5.3 Moves and drops

The interpreter keeps its own account, by the rules of docs/cryo.md
section 15, and reads nothing the compiler's ownership passes decided:

- a read of a place whose type is not `Copy` moves it: the cell becomes
  moved-from, and a later read of it is a violation;
- assigning a place drops the value it held, if any, before storing; the
  right side's value is taken first, so a right side that moves the place
  itself out (`v = if (c) { v } else { .. }`) leaves nothing to drop;
- a statement's temporaries are released when it ends, the newest first; a
  declaration the language marks as keeping them (a `for` loop's iterable)
  keeps them until its block ends, after the block's bindings are dropped;
- at the end of a scope, every live binding the scope declared that is not
  moved-from and whose type has a destructor is dropped, in reverse order
  of declaration; a struct's members are dropped after its own `drop`, in
  declaration order, and a moved-from member is skipped;
- `mem::drop(v)` and a discarded value drop at once.

A type has a destructor when it implements the `Drop` language item, or
holds a member that does. Dropping runs the implementation's `drop` body,
interpreted like any other.

## 6. The machine

Every function with a body is interpreted, the standard library's included:
`wrapping_add` runs its body (`this + other` at its type), `fmt::println`
runs its body down to the C library call that writes the bytes, and `Rc`
counts with its own code. A function with no body is the **machine**, and
the machine is exactly two kinds of declaration:

- **intrinsics**, identified by the `IntrinsicKind` the compiler registered
  on the declaration-index entry: `malloc`, `realloc` and `free` act on
  heap allocations (section 4); `size_of`/`align_of` answer from the
  interpreter's own layout of the type; the bit operations compute their
  result; the atomics act on the cell as a plain read and write (a run has
  one thread); `panic` ends the run as a program panic (section 7);
  `location` builds the `Location` the call site's span gives;
- **foreign functions**, the declarations of an `extern` block, identified
  by the C symbol they bind - a fact of the boundary, not a spelling in
  Cryo: `memcpy`, `memmove`, `memset`, `memcmp` and `strlen` act on cells;
  `printf`, `putchar` and `fflush` write to standard output (the subset of
  `printf` conversions the standard library's print path uses: `%.*s`,
  `%c`, `%s`, `%d`, `%u`, `%llu`); the host's console queries answer that
  standard output is not a console, so a run's output is the same on every
  host.

A machine rule is small and written against the C or LLVM definition of the
operation, never against the compiler's lowering of it. A foreign function
with no rule is not covered (section 7).

**Why the library is interpreted rather than given rules.** A rule per
library function would need an identity for each - `println`, `Box::new`,
`Rc::clone` - and the only identities the compiler has for declarations it
reasons about are language items, which `println` and `Rc::clone` are not;
recognising them any other way is looking a spelling up. Interpreting the
library needs no identity beyond the C boundary, keeps one set of rules
(the language's), and checks the library as strictly as the program: a
`clone` that counts twice leaves the block alive at exit (a leak), and one
that forgets to count frees it while a handle still points at it (a read of
an ended allocation). What it does not check is a library function whose
every result is wrong in the same way at every call, such as a
`strong_count` that answers one more than the count: the hash agrees with
the compiled program's because both ran the same code. That is the cost of
the choice, and the reason the agreement on `Rc` is backed by the leak check
rather than by the hash.

## 7. What it reports

The interpreter writes one line to standard error for anything it reports,
and exits with its code:

| exit | line | meaning |
|---|---|---|
| main's value | (none) | ran to the end |
| 1 | the front end's diagnostics | the front end refused the program |
| 101 | `interpret: panic: <message> at <file>:<line>:<column>` | the program panicked, as a compiled build would |
| 70 | `interpret: violation: <what> at <file>:<line>:<column>` | the program broke a rule of section 4 or 5: undefined or spec-forbidden behaviour, or an interpreter defect |
| 71 | `interpret: not covered: <construct> at <file>:<line>:<column>` | the program uses a construct the interpreter does not run yet |
| 72 | `interpret: step limit reached` | the program ran more steps than `--step-limit` (default 200 000 000) |

A violation is a finding about the program or the compiler's front end, not
a refusal: CryoSmith promises programs that never commit one.

## 8. Coverage, by generator stage

Each unit lands with its forms, its tests and a corpus agreement run over
seeds generated with only the features covered so far.

| unit | stage | forms |
|---|---|---|
| I1 | 1 | integers and booleans, exact wrapping, guarded operators, shifts, division, casts, widening, `if` expressions and statements, the three loop shapes, `break`/`continue`, calls, early returns, module-level `observed`, `mix`, and everything `fmt::println` and the f-string's lowering run: structs, `match` on the library's enums, arrays, raw pointers, the allocator, trait objects (`Formatter`'s sink) |
| I2 | 2, 3 | structs, defaults, literals, field reads and writes, methods (static, `&this`, `mut &this`), constants; generic functions, structs and methods as the monomorphiser copied them; overloads by their pins |
| I3 | 4 | enums, payloads, generic enums, `match` statements and expressions, nested patterns, the copy rule for bindings |
| I4 | 5 | closures, captures by copy, function values, generic closures, callable bounds |
| I5 | 6 | traits, bounded calls, trait objects, default methods, supertraits, enum implementations |
| I6 | 7 | owned values, moves, references, `mem::drop`, discarded results, `Box`, `Rc`, `Option`, `Result`, `?` |
| I7 | 8 | strings, arrays, collections |

Generics are interpreted as the monomorphiser instantiated them: each call
runs the copy its pin names. The interpreter checks the language's rules on
the instance it runs, which is what the compiled program runs too.

**Status.** I1 to I5 are done, and I6 to I8 run: closures as section 5.1
says; a call through a trait object, a supertrait's method included, by
the implementations the type checker recorded with the object's table
(the conversion's implementation, or its copy for the value's type); moves,
drops and the release rules of section 5.3; `Box`, `Rc`, `Option`, `Result`
and `?`, strings and slices through the standard library's own bodies.
Every construct the default features generate runs: a 200-seed default
corpus reports nothing not covered.  Anything the interpreter meets that it
does not run stops the run as not covered; the corpus check reports such a
program as not covered, never as agreeing.

**Open.** The order of the releases at the end of one statement - a
discarded result and a borrowed temporary - is not stated by the language
reference: the interpreter releases the newest first (the result before
the temporary it was made from), code generation the borrowed temporaries
first.  Seeds that observe both disagree until the reference decides.

## 9. The corpus check's third comparison

`cryogate corpus-check --interpret` adds the interpreter to the comparison:
each seed is built at `-O0` and `-O2` as before and also run with
`cryo interpret`, under the same time limit scaled for the interpreter. A
seed passes when the three print the same line and exit 0.

The interpreter's verdict for a seed is one of: **agrees**, **disagrees**
(a different hash, which fails the seed and names which side differs), a
**violation**, a **panic** or a **step limit** (each fails the seed), or
**not covered** - which does not fail the seed but is never counted as
agreement: the summary line counts the seeds the interpreter ran and the
seeds it did not cover, each by its construct, so a corpus the interpreter
skipped entirely reads as `0 interpreted`, never as a pass.

Without `--interpret` the check is unchanged.
