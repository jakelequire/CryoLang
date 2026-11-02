# Pointer flip: marking written pointers `mut`

The tools that marked every pointer the standard library and the compiler
write through `mut T*`, ahead of a bare `T*` becoming read-only.  While
`mut T*` and `T*` are the same type the marks change spelling only.

- `audit.patch` - applied to `compiler/src`, it builds an audit compiler: a
  bare `T*` is read-only, there is no error cap, and each refused write or
  read-only value meeting a writable slot prints
  `AUDIT <file> <line> <col> <end line> <end col>`, the pointer annotation
  that must become `mut`, or `AUDIT-MISS <file> <line> <col> <why>` where the
  declaration behind it is not traced.  It traces locals, parameters,
  globals, fields, calls, casts, indexing (a `[T]` to its element
  annotation), pointer arithmetic, enum payload bindings, and, where the
  marks of two sides must match exactly (below a pointer, inside an array or
  a type argument), the slot's own annotation: a call's parameter, a
  declared type, an assignment's left side.  Not traced: a generic
  instance's method reached through the instance, a destructured field, a
  method call whose declaration is chosen after its arguments are checked.
- `codemod/` - a Cryo program that applies AUDIT lines: `mut ` before the
  innermost level of a run, `mut (...)*` around an outer one.
- `mark.sh` - writes AUDIT lines by hand for the misses.
- `audit-rounds.ps1` - audit, codemod, repeat, until a project checks clean
  or a round has nothing it can mark; converts E0312 (a trait method's
  parameter marked on one side only) with `e0312-audit.ps1`, which marks the
  trait's declaration when an implementation writes through it and every
  implementation once the trait is marked.
- `mark-signatures.ps1` - marks the elements of `[X*]` arrays of the AST
  node types in named functions' signatures, for the calls the audit cannot
  trace to a declaration.
- `strip-array-marks.ps1` - removes `mut ` before an array type, for a tree
  the pin builds while the pin cannot parse `mut [T]*`.

Procedure, from the directory a build runs in (`stdlib/` for the library):

```bash
git apply scripts/pointer-flip/audit.patch
(cd compiler && ../bin/cryo build --profile=release --build-dir=<audit dir>)
git apply -R scripts/pointer-flip/audit.patch
<audit dir>/cryo build --no-incremental --build-dir=<scratch>   # output captured to a file
codemod/build/pointer_codemod <captured output> [more AUDIT files]
```

Repeat until the build succeeds; then again with
`--target=x86_64-pc-linux-gnu` for the platform-gated code.  For the
compiler, `check src/main.cryo --stdlib=<abs>` from `compiler/` in place of
the build.  `audit-rounds.ps1 -Dir <dir> -AuditArgs <args> -Scratch <dir>`
runs the loop.

The library's hand marks, in `mark.sh` form from `stdlib/`:

```
core/option.cryo|79|-> |T*            io/buf.cryo|583|-> |Array<u8>*
io/traits.cryo|605|-> |Array<u8>*     core/ptr.cryo|31|-> |T*
core/mem.cryo|43|ptr: |T*             core/mem.cryo|43|-> |T*
collections/raw_buffer.cryo|52|-> |T* collections/array.cryo|149|-> |T*
core/slice.cryo|61|-> |T*             sync/mutex.cryo|116|-> |T*
alloc/arc.cryo|266|-> |ArcInner<T, A>* alloc/arc.cryo|222|-> |T*
alloc/box.cryo|143|-> |T*             collections/raw_buffer.cryo|59|-> |T*
alloc/box.cryo|27|ptr:   |T*          io/buf.cryo|526|-> |S*
collections/hashmap.cryo|87|next: |Entry<K, V>*
future/reactor.cryo|229|spawn::<|Reactor*
future/blocking.cryo|345|try_spawn::<|BlockingPool*
future/executor.cryo|745|spawn::<|ExecInner*
```
