# Roadmap

The work planned since `naming-impl` was merged into `main`.  Each item
says what it is, and what of it is built and still open.  The rulings
that constrain an item are in the appendix of `docs/resolution-rules.md`,
and the language as it stands is `docs/cryo.md`.  Known bugs are not here:
they are in the local bug log, `.bug-report/` (see `CLAUDE.md`).

## Order to v1.0

CI runs `make verify`'s gate list (ruling 266), and the editor's rewrite
came first (ruling 316); what it built and what it left are under "The
language server".  From there, in order, to the v1.0 freeze:

1. **Array type syntax.**  Array types are written `[T]` and `[T; N]`, and
   the postfix forms are refused.  Built, on `type-syntax-impl`.
2. **One dynamic array type.**  `[T]` becomes `Array<T>`, which also
   removes the crash of nested builtin arrays (below).
3. **Class removal and borrowed trait objects.**  Built, on
   `class-removal`: the class feature is deleted, each of its forms refused
   where it is written (E0117, ruling 465), and `virtual &T`,
   `virtual mut &T` and `virtual T*` dispatch through a table (rulings 285,
   288, 289, 415), a generic implementation's at its instantiation (583).
   Left for trait objects: owning boxes and upcasting; see "Trait objects"
   below.
4. **Associated constants on concrete types.**  Built, on
   `associated-constants`: `i32::MAX`, `f64::NAN`, `Duration::NANOS_PER_SEC`,
   with `max_value()` removed (ruling 510).  Trait constants used as
   values come after v1.0; trait constants as array lengths are not in
   v1.0; see "Associated constants" below.
5. **An internal compiler logger.**  Built: `utils::log`, with areas, levels, `CRYO_LOG` / `--log` and a file
   sink, in place of `LOG_DEBUG`, `cdebug` and `CRYO_TIMINGS`; see
   "An internal compiler logger" below.
6. **The standard library's visibility pass.**  Everything in it is public
   today, so internals such as `Array`'s grower are public API.  The pass
   decides what stays public, and which standard-library APIs want trait
   objects.
7. **The slice and API pass**, after the visibility pass:
   - first, the fix for a by-value struct parameter accepting
     `&OtherStruct` (bug 0063);
   - `&this` enforced as read-only (D37), with const pointers following;
   - `Read` taking a `Slice`;
   - `Slice` finished: `s[i]`, `Eq` and `Hash`, `s.slice(1..4)`, and
     `as_slice()` on fixed-size arrays;
   - `implement Trait` in type position: the concrete type hidden, the
     return promise checked at the function, and a refusal of its own in
     parameters, fields, aliases and type arguments (rulings 428-430);
   - a trait declaring two methods with the same name refused at the
     declaration (ruling 424);
   - `![implicit]`'s conversion of a string literal written down in
     `docs/cryo.md` (the directive stays).
8. **Bug fixing and bug hunting** before the freeze.

The rule of thumb: a breaking change goes in before the freeze; an
additive feature, or an internal rewrite such as the HIR and MIR pipeline
or compile-time execution, comes after it.  The other items here are not
ordered yet.

## The language, used by its own compiler

An ongoing principle rather than an item (ruling 368): the compiler and
the standard library use Cryo's own language features wherever one fits -
`Eq`, `Index`, `Drop`, `Allocate` and the other traits - instead of an
ad-hoc method that does the same job, so that the compiler exercises the
whole language in live code and its defects surface there first.  A type
with an `equal` method that does not implement `Eq` is the shape to
remove.

Open: a survey of the compiler and the standard library for ad-hoc
`equal`, compare, hash and to-string style methods that should be trait
implementations, each one a candidate for its trait.  Not started.

### `fmt::Arguments`: what is left (ruling 607)

- The HTTP status line, HTTP/2's `:status` and `set_body`'s Content-Length
  still write their decimals through a stack buffer by hand: they write
  into an `Array<u8>` and a header map's `Str`, neither of which is an
  `FmtWrite` sink.  Open: whether `Array<u8>` (a byte buffer that would
  only ever be given UTF-8 through `FmtWrite`) should implement
  `FmtWrite`, so they become `out.write(f"...")`.
- A spec on a non-numeric hole (`{name:>8}`) still formats the value into
  a `String` to measure it before padding.
- The object conversion it needed (ruling 606) does not yet cover an
  object of a supertrait of a bound, nor an instantiation over a
  parameter whose implementation holds for every argument
  (`&Wrap<T>` with `implement<T> trait Shape for Wrap<T>`).

### Inherent `implement struct` blocks to inline methods

Ruling 397 puts a struct's own methods inline in its body.  Existing
inherent blocks are converted as their files are next touched, not in one
sweep.  Counted (`grep -E '^\s*implement(<[^>]*>)? (struct|union) '`):

- compiler: 9.  Three are `Parser`'s: the blocks in `expr_parser.cryo`
  and `parser.cryo` are the split-type exception and stay; the one in
  `cursor.cryo`, the struct's own file, goes inline.  The others are
  `AsmPiece`, `InlineBodies`, `DirectiveRegistry`, and `StampSlot`,
  `BuildStamp`, `VersionReport` in `utils/build_stamp.cryo`.
- standard library: 4.  Three are `BufStream<S>` extended from the
  HTTP/1.1, HTTP/2 and WebSocket modules, the split-type exception; one is
  `Array<T, A>`'s second block in its own file, to fold inline (its bound
  becomes a `where` on the methods that need it, or stays on the type).
- tests: 108, converted with the tests they belong to.

## Merge notes

`naming-impl` is merged into `main`, and CI runs verify's gate list.  These
notes keep what the gates mean and what is still red or unexercised.

**What the local gates mean.**  `make check-fast` runs verify's self-test,
verify-pin's self-test and `make verify-pin` (each pin's sha256 and stamped
commit match its sidecar, and both pins name one commit).  It builds
nothing, so it says nothing about the compiler.  `make verify` is the
evidence: it builds the compiler once, then runs the census (every suite,
counts against `tests/test-roster.txt`), every example's build, the LSP
build with the compiler under test, the other OS's half (objects only), the
incremental new-instance fixture and check-fast, all concurrently; with
`ARGS="--baseline HEAD --require-identical"` it also proves no compiled
object moved.

**CI runs verify's gate list** (ruling 266: CI is aligned with verify's
gate list first thing after the merge).  CI fires only on `main` and on a
manual dispatch.  As it stands:

| gate | `make verify` | CI |
|---|---|---|
| census (`test-census`) | yes | yes, both hosts |
| examples build | yes | yes, both hosts |
| `lsp-check` | yes | yes, both hosts |
| `cross-check` | yes | windows native only: a Linux host links the windows-gnu build and needs mingw and the LLVM-C import lib, until `cryo build` can stop at objects (below) |
| `incremental-instance-check` | yes | yes, both hosts |
| `check-fast` (the two self-tests) | yes | yes, both hosts |
| object comparison against a baseline | yes | no: a baseline from before a language feature's removal does not build with the current pin, so it is run locally (ruling 508) |
| `roster-check` | no (the census reads the same golden) | yes |
| `examples-golden`, `valgrind-check`, `verify-freestanding` | no (POSIX only) | yes |
| `incremental-check` | no | yes |
| `vendor-check` | no | yes |
| `api-index-check` | no | yes |
| `selfhost-check` | no | yes, both arms: the Windows chain under wine on the Linux runner |

Every CI job is green on `main`.  The rows `make verify` does not run are
CI's alone, so a local green does not cover them.  CI also builds the
Windows release zip and builds and runs a hello-world with it under wine,
the way the release workflow does before it publishes.

**Warnings.**  A clean build of the compiler, the standard library's own
build and the editor's build each report 0 warnings.  Compare warnings only
from a clean build: an incremental build does not replay a cached module's
warnings.

**`--baseline` cannot reach `main`.**  `make verify ARGS="--baseline REV"`
fails for any revision before the version stamp, `main` included (ruling
298: fixed after the merge), because the baseline is stamped by the working
tree's Makefile and an older compiler has no stamp slot.  So the merge
itself cannot be checked object-for-object against `main`; compare against
`naming-impl`'s tip instead.

**Known red or unexercised.**
- `make cross-check`, and with it `make verify`, cannot pass on a Linux
  host: the compiler links the windows-gnu build there rather than stopping
  at objects.  Run verify from Windows, or pass `--only` to leave cross out.
- The release smoke runs the STAGED tree, not the archive, and the Windows
  tree under wine with llvm-mingw's `clang.exe` driving the link; nothing
  runs the zip on a Windows host with the mingw `gcc` an installed
  `cryo.exe` finds there.  That stays so for now; a test of the zip on a
  real Windows host is added before the first tagged release (ruling 323).
- `make release`, `install` and `uninstall` are POSIX recipes with no
  Windows branch; on a Windows host they fail with a cmd parse error rather
  than a refusal.
- `ProcessAsync::two_children_join_concurrently` flakes under load on
  Windows: it compares two wall-clock measurements, and process start-up
  under the suite's load can stretch one past the other.  `CRYO_TEST_JOBS=4`
  steadies it.  It is the only test known to flake.

**Tree state.**  The session scratch directories are deleted (ruling 299:
the scratch directories are deleted after an inventory), the one-off
codemods are deleted (ruling 268: a codemod goes in the commit after the
one that ran it), and `scripts/README.md` names every remaining script and
its caller.  `make clean` removes every build output and gate state the
targets write.  The untracked plans in `docs/` and `.todo/` are not part of
the branch.

## The language server

Rewritten on the `lsp-rewrite` branch (ruling 316) and merged into `main`.
`tools/CryoLSP`
answers from the identities the compiler stamps, never by a declaration's
spelling: a position finds its node, the node's stamp names a definition,
and every answer is read from that definition.  One analysis per project
runs on open and on save, with every open buffer in place of its file; the
last analysis that parsed answers while a buffer is broken or being typed,
and answers requests between modules while the next one runs.  It serves
diagnostics, hover, definition, references and highlights, document and
workspace symbols, completion, semantic tokens, the implementations lens,
rename, signature help and inlay hints, and writes names as the cursor's
scope writes them (ruling 346).  A standard library or dependency file is
answered from the analysis of the project that holds it.  `make lsp-check`
drives the server through `tools/CryoLSP/tests/smoke.py` over its fixtures
and refuses below its floor of passing checks (`docs/testing.md` section
9).  Two open defects are in the bug log: memory that may grow across saves,
and one unreproduced unanswered request.

What is left:

- **Module caching** (ruling 345: the standard library analysed once per
  server process as a frozen base layer; whole-module reuse waits for the
  intermediate representation work).  Measured in the server, a save of
  the compiler project (166 local and 81 standard library modules) takes
  about 8 s and holds 1.6 GB, 3.1 GB at its peak while the next analysis
  runs beside the last: discovery 2.4 s, name resolution 2.3 s,
  monomorphization 0.9 s, the standard library's passes 1.8 s in all.  A
  file under no project takes 0.85 s, all but a few milliseconds of it the
  standard library: discovery 0.33 s, name resolution 0.19 s.  Every pass
  after semantic analysis reports diagnostics the editor shows (move
  checking, the dead-code lint, E0456 from drop insertion, E0236 and E0237
  from type lowering), so the editor's mode skips none of them.  In order:
  1. The frozen standard library layer.  Every table of an analysis lives
     in one context and one arena, and monomorphization routes the
     specializations a project asks for into the standard library's own
     module trees, so the library's state after an analysis depends on the
     project.  A frozen layer needs the library to finish the stages before
     monomorphization ahead of the project's modules, every table to
     retract to a mark, the arena split in two, and specializations kept
     out of the frozen trees.
  2. Lex each file once: discovery lexes every project file whole for its
     `namespace`, again for its imports, and the frontend lexes it a third
     time.  Then a cache of each file's scan result (namespace, import
     paths, `module` lines) keyed by path and content hash, held by
     the server across analyses and handed to the loader through
     `BuildOptions` as the yield hook is.  Its entries are strings, not
     identities, so they can outlive an analysis's arena.
  3. A token cache per file by content hash; a parsed-tree cache only if
     cloning a pristine tree measures cheaper than parsing, since later
     stages write stamps into the tree and monomorphization writes
     specializations into the owning module's.
  4. Per-module reuse after name resolution, as part of identities that
     are stable across analyses.  Invalidation would be the object cache's
     key: a module's source hash folded with every hash in its import
     closure and the prelude's.  What stops it today is that every identity
     (`SymbolStr`, `DefId`, `ScopeID`, `SymbolID`, `TypeRef`) is a position
     in a table one analysis builds and the next rebuilds, all in the
     analysis's arena, and a module's results name other modules'
     identities.  Either identities keyed by path and structure, or one
     long-lived context whose tables can retract a module's entries: a
     compiler architecture project.
- **Parsing that continues past an error** (ruling 330), only if the last
  good analysis proves not to be enough.
- **Completion is not read for a receiver whose head is a path**
  (`T::make().`).  After an import's path it lists only the modules the
  last analysis loaded, so `import std::` offers the standard library's
  modules the project reaches, not every one on disk.
- **Rename** refuses a module (its name is its namespace declaration and
  its file's place), a method a trait declares or implements (the trait's
  and every implementation's would move together), and a name at an
  import's alias.  Its collision check asks the bare-name lookups at every
  use, a qualified use (`B::helper()`) included, which can refuse a rename
  that would have been safe there.
- **Signature help** reads a call whose `(` is on the cursor's line only;
  **inlay hints** show inferred types, not parameter names at call sites.
- **The uses index** is built per request (ruling 337); a cache waits for a
  measurement that needs one.
- **A library file opened before any analysis holds it** (a standard
  library file opened first, with no project open) is analysed as a project
  of its own cryoconfig.  With the frozen standard library layer it would
  be answered from that layer instead.
- **Code the compiler writes is marked `generated`** by drop insertion, the
  closure synthesis and the conversions the type check writes, which the
  editor looks through.  The asynchronous lowering and the `?` desugaring
  write locals with synthetic bindings and do not mark them; a sweep of
  every request over async and `?` programs found no wrong answer, but
  nothing holds the mark there.

## CI

CI runs the gate list `make verify` runs (ruling 266), so green on `main`
means what green meant on the branch; the rows it runs that verify does
not are in the merge notes.  It fires only on `main` and on a manual
dispatch, so on a branch the gates are what a person or an agent runs.

Open: which gates run on every push and which only before a release; and
whether branch pushes trigger anything at all.

## The test runner reports what it ran

Not built: `cryo test` is to print a count for each of its three suites -
zero included - and `--list` to list all three, not the unit suite alone
(ruling 272: the runner prints each suite's count and lists every suite,
retiring the census and roster scripts).  `test-census.py` then reduces to
nothing, and `roster-check.py` to a diff of `--list` against the golden.

A project's `test.json` gains `stdout_file`, which compares the program's
whole output against a file (ruling 271: a `stdout_file` field compares a
program's output against a file).  The examples' `expected.out` goldens move
onto it, which retires `scripts/check-examples-output.sh` and makes the
check run on Windows as well.

Open: the summary line's exact format, and whether line endings are
normalised before the comparison.

## Gate and script follow-ups

- **`scripts/` is restructured** into `gates/`, `pin/`, `release/`,
  `toolchain/` and `dev/` (ruling 273: the restructure happens after the
  merge).  Renames only, with every caller updated in the same commit.
- **`cryo build` can stop at object files** (ruling 322: on the roadmap,
  not built yet).  A Linux host links a windows-gnu build rather than
  stopping at objects, so `make cross-check` there needs mingw and the
  LLVM-C import lib, and `make verify` cannot pass on Linux; CI runs
  cross-check from the Windows job only.  A flag that ends the build before
  the link lets the gate check what it claims to, objects only, from either
  host.
- **The Windows release zip is tested on a real Windows host before the
  first tagged release** (ruling 323): unpacked, and a hello-world built and
  run with the mingw `gcc` an installed `cryo.exe` finds there.  Until then
  the release smoke stays the staged tree under wine.
- **The language server reports the stamped version** (`serverInfo`) that
  `cryo version` prints, with the matching change in the VS Code extension
  (ruling 343).
- **`cryo version --verbose` shows the binary's sha256,** once the standard
  library has a sha256; it has sha1 only today.
- **`make verify ARGS="--baseline REV"` works for a revision from before
  the version stamp,** `main` included (ruling 298: fixed after the merge).
  verify builds every baseline with the working tree's Makefile, which runs
  `cryo stamp` on it, and a compiler from before the stamp has no slot to
  stamp: "it holds 0 stamp regions; expected exactly one".  A baseline
  build needs to skip the stamp step.

## Associated constants

Built, on `associated-constants`: a struct, union, enum or primitive
declares constants reached by a path, `Type::NAME` (docs/cryo.md section
8.8; rulings 509-524).  The standard library gives every integer type
`MIN`, `MAX`, `BITS` and its `MAX_*_DIGITS` buffer sizes, `char` its
`MIN` and `MAX`, and `f32`/`f64` their limits and non-finite values;
`max_value()`/`min_value()` and `math`'s float limits are gone.  The
constant folder computes over every integer type's range, `i128::MIN`
through `u128::MAX`, and wraps a constant's value to its declared type.

Left:
- constants in a trait and in its implementations (E0245, E0246 today):
  used as values after v1.0; used as array lengths, not in v1.0;
- a constant on a generic type (E0247 today), additive, after v1.0;
- whether a constant may be used where a type argument is expected
  (open).

## An internal compiler logger

Built: one logger in `utils::log` replaces `LOG_DEBUG`, `cdebug` and
`CRYO_TIMINGS` (rulings 538-551; `docs/cryo.md` section 24.2 has the
switches).  The `CRYO_LAYOUT_AUDIT`, `CRYO_CTX_AUDIT` and
`CRYO_TYPE_AUDIT` checks keep their own variables: each runs a check and
reports what it finds, which is not a log line.

Open:

- several lines only ever shown under `--debug` are really a warning or
  an internal error a user should see - a missing bindgen header, a
  missing cryoconfig, a path dependency without one, a pass pipeline that
  fails validation, codegen's corrupt function types, a generic
  instantiation reached with no generic registry.  They are `warn` and
  `error` log lines, off by default; whether each becomes a diagnostic is
  open;
- the language server logs to stderr or a file when `CRYO_LOG` is set; a
  sink sending lines to the editor as `window/logMessage` is not built;
- one progress line is still printed directly rather than logged or
  reported: `[deps] cloning <url>` when a git dependency is first
  fetched;
- `cryo check` of the compiler is about 2% slower since the logger
  merged, measured but not yet profiled.

## The standard library's visibility pass

Everything in the standard library is public today, so its internals -
`Array`'s grower among them - are public API, and would be frozen with
v1.0.  Cryo stays public by default and nothing is linted; the library
itself writes `public`, `private` or `internal` on every item, so each
exposure is a decision.  Built: `private` holds on every member and item
(rulings 562, 563); `internal` reaches the declaring package (567); and a
signature may not name a type seen by fewer modules than it is, a trait's
implementation included (564, 565, 567); and `module x;`, `internal module
x;` and the grouped forms reach as far as they say, with `public module`
refused (568); and the compiler finds its language items by the
library's `![language("name")]` on each, so an f-string's builder calls
name their items and are never refused (570, 571), `a <=> b` is `Ord`'s
`compare` by identity (572), and only the library declares items (573);
and a `Location` is read through `file()`, `line()`, `column()` and
`function()`, its fields private, so only the compiler makes one (414).
Left over from that: the async entry point calls `Executor::new` and
`block_on` by name.  The rest, in order:

1. **Every length, position and size is an `i64`.**  Built: the
   collections' `length` and `capacity` fields are private behind
   `![inline]` accessors (574), every `length()` and every position and
   size a collection method takes or answers is an `i64` (575), and a
   negative one means what the method's kind says (576).  The byte counts
   and positions of the I/O traits, the number-to-text writers,
   `next_below` and the HTTP/2 lengths and indexes are `i64` too: a read
   or seek given a negative one fails with `InvalidInput`, moving a
   cursor by one panics, and a width or precision of zero or less writes
   nothing extra (578).  `HashMap::bucket_count` and `get_bucket` stay
   `u64`, because the bucket index is masked from a `u64` hash (579).
   Reaching a length through its accessor costs `cryo check` about a
   tenth more than a field read did; that is to be optimized when the HIR
   and MIR pipeline lands.
2. **`Display` and `Debug` over a trait object.**  Built: one
   non-generic `fmt(&this, f: mut &Formatter)` writing through the
   `virtual mut &FmtWrite` a `Formatter` holds (423).  Since that `fmt`
   is no longer generic, the implementation for an instantiation of a
   generic type (`Pair<i64, Str>`, `Option<T>`) is emitted with every
   instantiation whose bound holds, as each of its other methods is,
   where `fmt<W>` was instantiated only when called: the compiler's code
   grew by about a thirtieth and the library archive by about a
   twentieth, accepted for now (delegated).  "A generic type's methods
   emitted only when called" below would take it back.
3. **Every item marked.**  About 650 helpers used only in their own
   module become `private`; about 215 used across the library become
   `internal`; implementation modules become `internal module`.
   `internal` is for plumbing only the library uses; an API a program
   could build on stays public, and an item nothing calls yet is not
   hidden for that (587).  `net::addr::sockaddr` stays public: low-level
   programming is not hidden.  The C bindings (`libc`, `syscall`, `openssl`) stay public,
   outside the 1.0 stability promise.  Tests that reach internals are
   rewritten through the public API, or deleted where they pinned only
   an internal.  Anything borderline is listed for Jake rather than
   hidden.  Built: `future::reactor` and `net::http2::huffman`/`hpack`
   are `internal module`s with every item marked; a connection codes its
   header blocks through a public `HeaderCodec` it is generic over, whose
   default `HpackCodec` keeps the HPACK encoder and decoder private (588).
   `collections::raw_buffer` is public, `RawBuffer` with private fields,
   and so is `alloc::heap`, as the allocator `NativeHeap` with its raw
   calls internal (589).  `Array`'s, `HashMap`'s and `Pool`'s own grow
   helpers are private, `String`'s buffer is a private field, and so are
   `Layout`'s size and alignment, behind its accessors.  `alloc` is
   marked item by item, `ArcInner` private behind an `Arc` raw API that
   hands out and takes the value's own pointer (596): `Box`'s fields are private
   behind `from_raw_in`, the growth curve `next_capacity` internal (592),
   the arena's and pool's default sizes public and `GlobalArena`'s four
   routing reads internal (594).  `ffi` and `sys` mark every constant,
   type, field and wrapper `public` but `sys`'s private `normalize`, and
   every `extern` block `public extern` (593); `core`'s unwind-runtime
   hooks are a `private extern` block.  `time`'s clocks and `Duration`
   keep their seconds and nanoseconds private behind accessors (595).
   `encoding`'s base64 and SHA-1 helpers are
   private, behind `encode`, `decode` and `sha1`.  `net::sys` is public
   but for its own helpers, and `net::addr`, `dns` and `http` are marked
   item by item (597), and the rest of `net` (599) - HTTP/2, HTTPS,
   sockets, TLS and WebSocket - so all of `net` is marked: its three
   private `push_all` copies are gone, the encoders writing
   `out.try_append(bytes)?` now that `?` converts an allocation error
   into an I/O error through `From` (601).
   A struct member's keyword follows its directives (598), and the
   attributed accessors (`length`, `capacity`, `Str::new`,
   `String::from_string`, `Location`'s four) are public.  HPACK
   is tested through `Http2Connection` against RFC 7541's examples, and
   the heap through `GlobalAlloc` with `native_alloc` on and as the
   allocator of an array, a hash map and a string without it.  An `internal module` line now hides its
   module from other packages whichever files a build loads, so the
   standard library's internal modules are refused at the import as a
   dependency's are.  The library's one-off helpers give way to general
   APIs its callers use directly (602): a byte classifies itself (608).
   Open: `u8.to_ascii_uppercase`, which `Str.to_ascii_uppercase` still
   folds by hand for want of.

## A generic type's methods emitted only when called

Every method of every specialization of a generic type is emitted, called
or not, wherever its bound holds: `Array`'s unused indexing methods, and
since `Display` and `Debug` stopped being generic, a `fmt` for every
instantiation of `Option`, `Result`, `Pair` and the rest.  Emitting only
the methods something reaches takes that code back for every
specialization, not only formatting's.  It needs reachability over every
reference to a specialization's method - codegen's own synthesized calls,
trait-object tables and drop glue among them - where a missed one is a
link failure.

## One dynamic array type

Done: `[T]` is `Array<T>` with the default allocator (ruling 363), indexed
through the library's `Index` implementations (364), with `length` and
`capacity` of type `i64` (375).  The symbol rename was proved by comparing
compiled output with old names mapped to new (367); the one difference in
compiled code, the calling convention of a `[T]` passed by value, was made
alike first.  What stays open:

- **A fixed array converted to a dynamic one.**  `const d: [i32] = buf;`
  with `buf: [i32; 3]` is refused (E0200).  The built-in `[T]` accepted it
  and miscompiled it; whether the conversion is built (allocate and copy,
  moving an owning element) is undecided.
- **`xs[i] = v` does not release the displaced element**, as `cryo.md`
  states for a run-time index.
- **Operators on a generic container in a generic body** (`a + b` with
  `a: Vec<T>`) have the gap indexing had: only a bare bounded parameter is
  desugared there.
- **Every method of an instantiated generic is emitted**, `Array`'s
  eleven `index` and `element_at` among them, used or not: about 1% of a
  stripped binary (373: accepted); see "A generic type's methods emitted
  only when called".
- **The written spelling reaches declaration mismatches only.**  An
  argument mismatch shows the parameter as `[T]` even where the parameter
  was written `Array<T>`: the check holds the types, not the annotations.
- **The demangler prints `[T]` as the instance it is**,
  `std::collections::array::Array<i64, std::alloc::allocator::GlobalAlloc>`.

## Build the compiler with LLVM optimisation, then derive `TypeKind`

Ruling 403.  `Type` carries both a stored `kind: TypeKind` and the payload
`data: TypeData`, so the kind is said twice.  Deriving it from `data`
(ruling 390) is right, but the compiler is built at O0, where
`passes.cryo` runs no IR pipeline, so nothing folds a `match` that yields
a constant per arm into a table load: every one of the compiler's 1,130
`kind` reads becomes a jump-table branch, plus a call when not inlined.
Measured by `cryo check src/main.cryo` on the compiler (five runs,
medians, all built by a compiler that lowers a `match` on constants to a
`switch`):

| `kind` | check time |
|---|---|
| stored field | 8.60 s |
| derived | 9.55 s (+11%) |
| derived, both `kind`s `![inline]` | 9.07 s (+5.5%) |
| derived, built by a compiler lowering `match` to a compare chain | 10.01 s (+16%) |

Peak memory was unchanged (1,530 MiB).  The steps, in order:

1. Build the compiler with an LLVM pipeline (at least `simplifycfg`,
   which turns a `switch` yielding constants into a table load), with the
   selfhost fixed point and the object gates re-baselined on it.
2. Derive `TypeKind` from `TypeData` and delete the field, measured
   against the stored field under the optimised build.

## Trait objects

A pointer or reference to a trait, dispatched through a table of the
implementation's methods.  **Borrowed trait objects are built** (ruling
360 brought them forward from ruling 286's "possibly after v1.0"; ruling
415 placed them after the class removal); `docs/cryo.md` section 10.7
describes them.  Left: owning boxes, upcasting, and generic
implementations as objects.

- **Spelling:** `virtual &Shape`, `virtual mut &Shape` and
  `virtual Shape*`, a prefix as in `mut &this`; there is no `dyn` (ruling
  285).  `&Shape` and `Shape*` are refused where they are written
  (`E0313`; ruling 287), as is a bare `p: Shape` (ruling 304).  The prefix
  composes inside an array's brackets with no parentheses:
  `[virtual &Shape]`.
- **Object safety is per method** (ruling 288): a method that cannot go in
  the table - one with type parameters of its own, for instance - is unavailable on
  the object, and the rest of the trait still works.
- **Borrowed pointers first** (ruling 289); an owning box, which must take
  its drop, size and alignment from the table, is still to build.
- The table's slots come from the trait's method numbering, the same one
  the trait-implementation check (E0312) relies on, never from method
  names.

A supertrait's methods are reachable from a subtrait object directly: the
table holds them, and a value whose implementation is generic converts at
its instantiation, whose copy of the implementation holds the table
(ruling 583).  Still to build:

- **Owning boxes**, above.
- **Upcasting** a subtrait object to an object of one of its supertraits.

## The compiler pipeline: AST, HIR, MIR, LLVM

The front end lowers the syntax tree to a high-level representation, then
to a mid-level one, and only then to LLVM IR.  Today the checked syntax
tree goes straight to code generation, so every later pass rereads
source-shaped nodes.

Open: which of today's passes (drop insertion, async lowering, the move
checker, monomorphization) move to which level, and in what order the
move happens.

## Compile-time execution

Code runs at compile time by interpreting MIR, so it needs the pipeline
above.

Open: whether any function can run at compile time, with a program free to
run arbitrary code during its build, or a narrower model with marked
compile-time functions and limits on what they may touch.

## A context allocator scope

`with_allocator(arena) { ... }` makes `arena` the allocator for the `new`
expressions inside the block.  It arrives with the compile-time work.  A
type's own `Allocate` implementation wins over it (ruling 249: `new` and
`delete` go through the `Allocate` trait).

Open: whether the scope reaches into the functions the block calls or only
the `new` written inside it; and what `delete` does with memory allocated
under a scope that has ended.

## `cryo-lint`

A lint tool built on the compiler as a library, as the language server is:
it reads the compiler's stamps and types, so a lint is a question about
identities rather than a pattern over text.  The deny-by-default lint with
a reason-carrying allow, and the `deprecated` attribute (D49, D64), belong
here.

Open: which lints ship first, and whether `![allow]` names lints of this
tool or only the compiler's own.

## Decided language work waiting for its own branch

Each of these was decided and moved off `naming-impl` by the freeze on the
language's features; each is a branch of its own after the merge.

- **Primitive type names stop being keywords** (D18), and with them a cast
  followed by a comparison without parentheses, `x as u32 < y`, is refused
  and the parentheses asked for (D66).
- **Explicit receivers** on every non-static method (D36, D65).
- **A reference-mutability check**, run before the async lowering (D37).
- **The `raw` subcommand builds a module graph** as `check` and `build` do
  (D60).
- **A checked conversion from an integer to an enum.**  `op as u8` reads an
  enum's discriminant; nothing turns a `u8` back into a variant without a
  hand-written `match`, so every wire format with an enum (WebSocket's
  opcodes, HTTP/2's frame types) carries its own `op_from_u8`.  The
  conversion answers `Option` (or `TryFrom`) for a value no variant holds.

## Paths through generic type aliases

`type Stack<T> = Array<T>; Stack::<i32>::new()` names `Array::<i32>::new()`,
and `Stack::new()` infers the alias's arguments as a path through the target
does.  Refused today with `E0233` where the path is written (ruling 265: a
generic alias used as a path is refused with a clear error until it is
supported); a non-generic alias's path already names its target's members.

Open: whether an alias that fixes some of its target's parameters
(`type StrMap<V> = HashMap<string, V>`) infers only the alias's own.

## Capturing closures held by function-typed slots

A capturing closure stored in a variable, field or array element of
function type, assigned to one, or returned from a function is refused
today (`E0458`; ruling 293: refused until it is supported properly).  A
function-typed value is a bare function pointer, with nowhere to keep the
captures, so supporting it means a function value that carries an
environment: a pointer to the code beside a pointer to the captures, with
an owner that releases them.

Open: whether every function-typed value becomes the two-pointer form or
only a separate closure-value type does; who owns an escaping closure's
captures and when they are released; and whether a non-capturing function
keeps its single-pointer form across the C boundary.

## Moving a value out from behind a reference

A field moved out of a place reached through a reference or a pointer -
`mut old: Body = this.body;`, `match (this.opt)` by value, a generic getter
returning a `T` field, an array field read through `node.fields` - copies an
owning handle the place still holds, and the move checker lets it through:
the borrowed storage is someone else's to release, and nothing records
whether they will.  Only the destructor's own receiver is refused today
(`E0453`; ruling 309: moving a field of `this` out inside `drop` is refused,
full move checking waits for after the merge), because field glue certainly
releases it again.

Full move checking refuses every such move, and needs a way to write the
legitimate ones: a take that leaves a replacement behind (`mem::swap` does it
today, in two statements) and a release of what an assignment through a
reference overwrites, which today is not released at all.  The take-and-
replace idiom in the standard library's `set_body`, the editor's
`take_pending`, and `tests/tests/lang/move_field_out_ok.cryo`, which asserts
the idiom, are the first sites it reaches.

Open: whether a raw-pointer root is in scope (the compiler's own AST cloner
moves array fields through node pointers), and the spelling of a take.

## `mem::replace` and `Option::take`

The take that full move checking needs, as one call instead of a
`mem::swap` into a temporary followed by `mem::drop`:
`mem::replace(&place, value)` returns what the place held and leaves
`value` behind, and `Option::take(mut &this)` returns the option's value
and leaves `None`.  The standard library's `Child::close_stdin`, its
request/response `set_body` and the editor's `take_pending` are the first
callers.

Open: whether `Option::take` is enough on its own, or every owning type
needs a take with a default left behind.

## W0015 becomes an error

An explicit destructor call, `x.drop()`, is a warning today, and the
compiler, the standard library, the editor, the examples and the tests
have none left outside the tests of the diagnostic itself.  The
warning's own text says it will become an error; then `mem::drop(x)` is
the only way to release a value early.  An ordinary method that happens to
be named `drop` on a type with no `Drop` is not a destructor call, and is
told apart before the warning becomes an error.

Open: whether `ptr::drop_in_place` stays the spelling for releasing a
value through a raw pointer.

## libc bindings spelled as their C headers

Low priority.  Most of the standard library's libc bindings spell C's
`void*` and `char*` as `u8*`; only the `mem*`/`b*` family takes and returns
`void*` as C does.  Every binding is aligned with the C header it binds, so
a call passes the pointer type C declares and a generic copy does not need
a cast to reach it.

## Open questions

- **How wide a `char` is**, and so what a character literal holds above
  255 (`'\u{e9}'` fits either way; `'\u{1F600}'` does not fit a byte).
  Undecided.
- **A module cycle's report.**  E0501 names the modules in a cycle; whether
  it prints the cycle as a path, in order, is open, and so is whether a
  cycle that crosses packages is E0501 too or an error of its own.
- **`std::Range`.**  Nothing re-exports `Range` at the root of `std`, so the
  path is refused today.  Whether `stdlib/lib.cryo` exports it there is open.
- **A `where` bound on a concrete type.**  `where HighError: From<E>` is
  accepted, but a bound directs only a path through a parameter, so neither
  `HighError::from(e)` nor `(From<E> for HighError)::from(e)` resolves
  through it in the generic body, and a `?` converting `E` into `HighError`
  is refused (604).  Whether such a bound should direct a call on its
  concrete subject, or be refused where it is written, is open.
