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
   `associated-constants`: `i32::MAX`, `f64::NAN`, `Duration::NANOSECONDS_PER_SECOND`,
   with `max_value()` removed (ruling 510).  Trait constants used as
   values come after v1.0; trait constants as array lengths are not in
   v1.0; see "Associated constants" below.
5. **An internal compiler logger.**  Built: `utils::log`, with areas, levels, `CRYO_LOG` / `--log` and a file
   sink, in place of `LOG_DEBUG`, `cdebug` and `CRYO_TIMINGS`; see
   "An internal compiler logger" below.
6. **The standard library's visibility pass.**  Built: every item of the
   library is written `public`, `private` or `internal`, so internals such
   as `Array`'s grower are no longer public API; what is left is listed
   under "The standard library's visibility pass" below.
7. **The slice and API pass**, after the visibility pass:
   - first, the fix for a by-value struct parameter accepting
     `&OtherStruct` (bug 0063);
   - `&this` enforced as read-only (D37) is built: a write through a
     read-only place is E0250, and a read-only place given where a
     writable one is wanted is E0251; const pointers follow;
   - `Read` taking a `Slice`;
   - `Slice` finished: `s[i]`, `Eq`, borrowed-only `&Slice<T>` and
     `mut &Slice<T>` with `AsSlice`/`AsMutableSlice`, the container
     conversion and the fixed-size array's conversion and slice methods
     (compiler-answered, since no implementation can name `N`) and `Hash`
     are built; `s.slice(1..4)` is not;
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
  `FormatWrite` sink.  Open: whether `Array<u8>` (a byte buffer that would
  only ever be given UTF-8 through `FormatWrite`) should implement
  `FormatWrite`, so they become `out.write(f"...")`.
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
  `BuildStamp`, `VersionReport` in `compiler/utils/build_stamp.cryo`.
- standard library: 4.  Three are `BufferedStream<S>` extended from the
  HTTP/1.1, HTTP/2 and WebSocket modules, the split-type exception; one is
  `Array<T, A>`'s second block in its own file, to fold inline (its bound
  becomes a `where` on the methods that need it, or stays on the type).
- tests: 108, converted with the tests they belong to.

## Merge notes

`naming-impl` is merged into `main`, and CI runs verify's gate list.  These
notes keep what the gates mean and what is still red or unexercised.

**What the local gates mean.**  `make check-fast` runs verify's self-test,
cryogate's self-test (verify-pin's checks among its unit tests) and `make
verify-pin` (each pin's sha256 and stamped commit match its sidecar, and
both pins name one commit).  It builds only cryogate, with the pin, so it
says nothing about the compiler.  `make verify` is the
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
| `incremental-runtime-check` | yes | yes, both hosts |
| `check-fast` (the two self-tests) | yes | yes, both hosts |
| object comparison against a baseline | yes | no: a baseline from before a language feature's removal does not build with the current pin, so it is run locally (ruling 508) |
| `roster-check` | no (the census reads the same golden) | yes |
| `examples-golden` | no | yes |
| `valgrind-check`, `verify-freestanding` | no (POSIX only) | yes |
| `incremental-check` | no | yes |
| `vendor-check` | no | yes |
| `api-index-check` | no | yes |
| `selfhost-check` | no | yes, both arms: the Windows chain under wine on the Linux runner |

Every CI job is green on `main`.  The rows `make verify` does not run are
CI's alone, so a local green does not cover them.  CI also builds the
Windows release zip and builds and runs a hello-world with it under wine,
the way the release workflow does before it publishes.

**Warnings.**  A clean build of the compiler, the standard library's own
build and the editor's build each report 0 warnings.  An incremental build
reports the same warnings as a clean one: every module is analysed in every
build, and a build that reported warnings is never "up to date".

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
(`cryogate lsp`) drives the server through its protocol checks and its
fixtures, and refuses below its floor of passing checks (`docs/testing.md` section
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
retiring the census's and the roster's text reading).  `cryogate census` then
reads the runner's own counts, and `cryogate roster` reduces to a diff of
`--list` against the golden.

A project's `test.json` gains `stdout_file`, which compares the program's
whole output against a file (ruling 271: a `stdout_file` field compares a
program's output against a file).  The examples' `expected.out` goldens move
onto it, so the comparison `cryogate examples --run` makes today (on both
hosts) is the runner's own.

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
- **`cryo version --verbose` shows the binary's sha256.**  The standard
  library computes one (`std::encoding::sha256`); the report does not use
  it yet.
- **`make verify ARGS="--baseline REV"` works for a revision from before
  the version stamp,** `main` included (ruling 298: fixed after the merge).
  verify builds every baseline with the working tree's Makefile, which runs
  `cryo stamp` on it, and a compiler from before the stamp has no slot to
  stamp: "it holds 0 stamp regions; expected exactly one".  A baseline
  build needs to skip the stamp step.
- **verify builds the baseline's own stdlib, so stdlib-only object moves
  are visible and declarable.**  Both sides compile every program against
  the working tree's stdlib, so a change to the stdlib alone moves no
  object verify compares and cannot be declared.  Until then such a change
  is measured by hand: the stdlib built before and after at the same path
  by the tree compiler, every object hashed, and the moved ones accounted
  for with `llvm-nm` - each must define or reference what the change
  touched, and none that does may stay unmoved.

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

A type's constant is a pattern (`u32::MAX =>`), as a module's is.

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

A standard library whose every item was public would freeze its internals -
`Array`'s grower among them - as public API with v1.0.  Cryo stays public
by default and nothing is linted; the library itself writes `public`, `private` or `internal` on every item, so each
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
   `virtual mut &FormatWrite` a `Formatter` holds (423).  The
   implementation for an instantiation of a generic type (`Pair<i64,
   Str>`, `Option<T>`) is analysed with every instantiation whose bound
   holds and emitted only where something reaches it, as each of its
   other methods is; see "A generic type's methods emitted only when
   called" below.
3. **Every item marked.**  About 650 helpers used only in their own
   module become `private`; about 215 used across the library become
   `internal`; implementation modules become `internal module`.
   `internal` is for plumbing only the library uses; an API a program
   could build on stays public, and an item nothing calls yet is not
   hidden for that (587).  `net::address::socket_address_storage` stays public: low-level
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
   `encoding`'s base64 and SHA-1 internals are
   private, behind `Base64`'s methods and `sha1`.  `net::sys` is an
   internal module (705), and `net::address`, `dns` and `http` are marked
   item by item (597), and the rest of `net` (599) - HTTP/2, HTTPS,
   sockets, TLS and WebSocket - so all of `net` is marked: its three
   private `push_all` copies are gone, the encoders writing
   `out.try_append(bytes)?` now that `?` converts an allocation error
   into an I/O error through `From` (601).
   A struct member's keyword follows its directives (598), and the
   attributed accessors (`length`, `capacity`, `Str::new`,
   `String::from_string`, `Location`'s four) are public.  HPACK
   is tested through `Http2Connection` against RFC 7541's examples, and
   the heap through `GlobalAllocator` with `native_alloc` on and as the
   allocator of an array, a hash map and a string without it.  An `internal module` line now hides its
   module from other packages whichever files a build loads, so the
   standard library's internal modules are refused at the import as a
   dependency's are.  The library's one-off helpers give way to general
   APIs its callers use directly (602): a byte classifies itself (608),
   and `Str` searches from either end while a `Slice` indexes and
   compares (609).  `Str`, `Slice` and `Path` are marked (610).
   Integers cross bytes through `ByteReader` and `ByteWriter`, and `u8`
   folds to uppercase too (611).  An OS error becomes an `IoError` in
   `io::error`, which is marked (612).  A float classifies itself and an
   integer rotates itself (613).  The executor, the reactor, the
   blocking pool and the channels lock with `sync`'s `Mutex` and
   `ConditionVariable` and start their threads through `thread`, which creates
   every thread one way (622), and each lock holds the state it guards
   (624).  A descriptor's non-blocking mode has one switch, and a drain
   reads through a borrowed descriptor's `read_to_end` (625).  A
   millisecond wait rounds through `Duration.as_milliseconds_ceiling`, a deadline
   saturates through `Instant.saturating_add`, `thread::sleep` is
   `time::clock::sleep`, and a `FILETIME` becomes a `SystemTime` one way
   (626).  A process exits through `sys::exit` and shells out through
   `Command::shell` alone (627).  The compiler's C-literal, number,
   identifier and suffix helpers left `core` (628).  `fs::metadata` and
   `env` are marked (629), and so are `math`, `random` and `sync`
   (630), `thread` and `fmt` (631), `io` and `fs` (632), `json` (633)
   and `process` (634).  Open: whether `File`'s and the child pipes'
   descriptors go behind an accessor, whether the JSON `Parser` is
   public, and how `ExitStatus` is built once the test runner spawns
   through `Command`.  Open: whether
   `string.is_under`, which the compiler asks of a module's file, gives
   way to a component-wise `Path` prefix test.  Open: a writer over a fixed
   region - SHA-1's digest, the generator's fill, `pack_sockaddr_in` and
   the IPv6 parser still store their bytes by hand - and whether the
   Winsock and OpenSSL error mappers in `net` become `IoError`
   constructors too.

## A generic type's methods emitted only when called

Built.  Every method of every specialization of a generic type is analysed
wherever its bound holds, but an executable and the standard library's
archive emit only the copies their code reaches: the walk runs over every
call, every function named as a value, and every reference code
generation makes with no call written - trait-object tables, `Drop` and
`Allocate` implementations, operators, indexing, `?` and the releases drop
insertion adds (`codegen/reachable_instances.cryo`).  A program linking
the archive defines in its own objects every copy the archive's manifest
does not list.  Any other library, and a single object, assembly or IR
file, keeps every copy, since its callers are not in the build;
`docs/cryo.md` states the rule.

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
- **The written spelling reaches declaration mismatches only.**  An
  argument mismatch shows the parameter as `[T]` even where the parameter
  was written `Array<T>`: the check holds the types, not the annotations.
- **The demangler prints `[T]` as the instance it is**,
  `std::collections::array::Array<i64, std::alloc::allocator::GlobalAllocator>`.

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
describes them.  Left: owning boxes and upcasting.

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
- **A checked conversion from an integer to an enum.**  `op as u8` reads an
  enum's discriminant; nothing turns a `u8` back into a variant without a
  hand-written `match`, so every wire format with an enum (WebSocket's
  opcodes, HTTP/2's frame types) carries its own `op_from_u8`.  The
  conversion answers `Option` (or `TryFrom`) for a value no variant holds.

## Two major versions of one library in a build

For v1.0 a build holds one version of each library, and entries asking for
two major versions are refused (`docs/cryo.md`, "One version of each
library").  After v1.0, each package may take the major version it asks
for, so a program whose dependencies need `gauge` 1 and `gauge` 2 builds
both:

- **Resolution per package, through the existing doors.**  A module path
  written in a package already resolves against that package's own
  `[dependencies]` (E0508's rule).  Two majors of one library become two
  packages with one top-level namespace each, and the package-declaration
  table, not the spelling, says which of them a package's `import gauge;`
  names.  No lookup gains a second strategy; the doors take the declaring
  package, which they already know.
- **Versioned symbols.**  The mangled name of every symbol of a dependency
  carries its package's major version, so `gauge::value` from 1.x and from
  2.x link side by side (`docs/cryo-mangling-spec.md` gains the component).
- **A rename key.**  An entry may name the library's namespace for its own
  package, `gauge2 = { git = "...", version = "2", namespace = "gauge2" }`,
  so one package can import both majors.  Without it, a package names the
  one major its own cryoconfig asks for.

The one-version rule then holds per package rather than per build: within
one series, entries still unify to one version.

## A package registry

After v1.0: a registry that names a library and a version requirement
without a URL (`json5 = "1.2"`), resolved to a repository and a commit the
lock pins as it pins a git dependency today.  The `[lib] version` key stays
the version's only source.

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
an owner that releases them.  A type that keeps a closure today holds it
through a type parameter with a callable bound (`F: (T) -> U`), which
needs neither.

A closure that leaves the function that made it already has to own what it
captures: one written without `move` borrows, and returning it is refused
(`E0457`), so an escaping closure is a `move` closure and releases its
captures with itself.

Open: whether every function-typed value becomes the two-pointer form or
only a separate closure-value type does; and whether a non-capturing
function keeps its single-pointer form across the C boundary.

## A forced unwind releases what the catching frame owns

A forced unwind - a thread ending through the C library's thread exit, or
being cancelled - passes through `catch_unwind` without being caught: under
it the personality answers "keep unwinding" for the catching call site. That
call site's one landing pad is the catch, which never reads the selector, so
the frame that holds the catch is left without running any of its drops:
the body `catch_unwind` was given, and what that body captured, are not
released, while every frame further out, the caller of `catch_unwind`
included, is. Releasing them needs the catching call site to carry a cleanup
separate from its catch, for the personality to choose under a forced
unwind.

## One definition of the runtime's check handlers

`runtime/panic/abort.cryo` and `runtime/panic/unwind.cryo` each define the
four check handlers (`__cryo_panic_bounds_check`, `_overflow`, `_div_zero`,
`_no_match`), and the two copies are the same: each formats its operands and
diverges through the strategy's `__cryo_panic`. They belong once, ungated,
beside the report they write (`runtime/sys.cryo`), leaving each strategy file
only what differs between the strategies. Decided for after the merge of the
runtime work.

## Moving a value out from behind a reference

A field that owns a resource, moved by value out of a place reached through
a reference or a pointer, is refused (`E0453`) in every body, each copy of
a generic body included, and in a closure's or an `async` function's;
inside an `unsafe` block it is the programmer's.  The legitimate takes are
written with `mem::swap` or `Option::take`, and a store through a reference
releases the value it displaces.

Open: whether a raw-pointer root should stay refused outside `unsafe` (the
compiler's own AST cloner moves array fields through node pointers), and the
spelling of a take for an owning type that is no `Option` (below).

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


Open: whether `pointer::drop_in_place` stays the spelling for releasing a
value through a raw pointer.

## Typed JSON through the derive list

Built.  `std::json::convert` has `ToJson` and `FromJson`, implemented for
the primitives, `String`, `Str` (writing only), `Array<T>`, `Option<T>`,
and a `HashMap` or `BTreeMap` keyed by `String`; the derive list writes
both for a struct or a field-less enum, as follows.

- **Spelling.**  `type struct Server with [ToJson, FromJson] { ... }`, the
  derive list that exists.  A field's options are a field directive:
  `![json(rename = "port_number")]` reads and writes the member under that
  key, `![json(skip)]` leaves the field out of both, `![json(default)]`
  reads a missing member as the field type's `default()`, and
  `![json(skip_none)]` leaves an `Option` field that holds `None` out of
  the written object.
- **Writing.**  The generated `to_json` makes a `JsonObject` sized for the
  fields not skipped and puts each one, in declaration order, under its
  key (its rename, or the field's name) as the field's own `to_json()`,
  then answers `JsonValue::Object`.  An `Option` field holding `None` is
  written as `null` unless the field says `skip_none`.  Every field's type
  must implement `ToJson`, and a refusal names the field.
- **Reading.**  The generated `from_json` refuses a value that is not an
  object with `JsonDecodeError::unexpected(Str::new("an object"), value)`.
  Each field reads the member under its key: a member present is read with
  the field type's `from_json`, and a refusal there is wrapped with
  `at_field(<key>)` so the error names the path; a member absent is the
  type's `default()` for a `default` or `skip` field (which needs
  `Default`), `None` for an `Option` field, and otherwise
  `JsonDecodeError::missing_field(<key>)`.  The struct is then built in one
  literal.  A member the struct does not name is ignored.
- **Enums.**  An enum whose variants carry nothing is written as its
  variant's name, `"Red"`, and read back from that string; another string
  is refused with `unexpected(Str::new("a Color"), value)`.  An enum whose
  variants carry values is out of scope for the first version, and a
  derive on one is refused (E0318).

## libc bindings spelled as their C headers

Low priority.  Most of the standard library's libc bindings spell C's
`void*` and `char*` as `u8*`; only the `mem*`/`b*` family takes and returns
`void*` as C does.  Every binding is aligned with the C header it binds, so
a call passes the pointer type C declares and a generic copy does not need
a cast to reach it.

## Decisions for the standard library's v1.0 shape

Jake's answers to the standard library plan.  Each is binding on the work
it names, built or not.

| # | decision |
|---|---|
| 1 | An `Error` trait plus a concrete `ErrorReport` carrier. |
| 2 | `IoError` holds an optional owned path; it drops and moves. |
| 3 | Capturing closures are allowed in method position, and `move` moves locals into a closure. |
| 4 | No `MutableSlice`: one `Slice<T>` whose mutability comes from the borrow, `&Slice<T>` reading and `mut &Slice<T>` writing; `&` is enforced read-only, `&this` included, with const pointers following. |
| 5 | A sorted map is named by its structure: `BTreeMap`/`BTreeSet` when it is a B-tree, `SortedMap`/`SortedSet` otherwise. |
| 6 | Logging is per-area `Logger` values plus a process-wide `Logging` configuration. |
| 7 | Fixed and local UTC offsets are in the standard library; the time zone database is a package. |
| 8 | Regular expressions are a package, after v1.0. |
| 9 | `Duration` stays `u64`. |
| 10 | The plan's new and renamed names are approved as written, `Signal.resume` included. |
| 11 | A documentation-coverage gate, with its mutation pair. |
| 12 | The API index is generated by a `cryo` subcommand rather than a script. |
| 13 | `Result` is `must_use`. |
| 14 | `Str` and `String` are in the prelude. |
| 15 | A callable bound is written as a function type, `F: (T) -> U`; an adapter that stores a callable is generic over it. |
| 16 | E0458 stays the code for what remains refused of closures (a function-pointer local, `virtual` dispatch, `extern "C"`, function-typed slots), its wording naming each reason. |
| 17 | By-reference, non-escaping captures: a borrowing closure may only be passed straight to a function-typed parameter, never stored, returned or used as a type argument, and an escaping one has an error code of its own. |
| 18 | `move` means "take ownership".  A closure without `move` borrows its captures and does not escape; a non-`Copy` capture is never moved implicitly.  Stored adapters (`map`, `filter`) take `move` closures; call-only callbacks (`for_each`, `sort_by_key`) take borrowing ones. |
| 19 | `&x` takes its mutability from what is expected and what `x` is; there is no `mut &x` expression. |
| 20 | `IndexMutable`/`index_mutable` and `DerefMutable`/`deref_mutable`. |
| 21 | Three codes: a write through a read-only place, a read-only borrow where a writable one is wanted, and `Slice<T>` used as a value. |
| 22 | `as_slice`/`as_mutable_slice`, plus the implicit conversion of an array to a slice reference through a language-item trait. |
| 23 | `Str` stays a `Copy` read-only view value; `as_bytes()` returns `&Slice<u8>`. |
| 24 | `Scope::spawn` accepts borrowing closures, under a rule tied to the scope. |
| 25 | HTTPS connects by trying every resolved address in turn, IPv4 first. |
| 26 | `SocketAddr::parse` accepts `a.b.c.d:port` and `[v6]:port`, beside `parse_v4`. |
| 27 | An opaque `implement Trait` value is `Copy` only when the return type says `+ Copy`; the concrete type's `Copy` does not show through. |
| 28 | Any trait method, not only a default, may return `implement Trait`, and an implementation may write its own body with its own concrete type, as a hidden associated type per implementation.  Such a method is not callable through `virtual &Trait`, and the refusal says so. |
| 29 | One trait over every iterator yielding `T*`, implemented once for every such iterator, carries `copied` and `cloned`. |
| 30 | The hidden associated type may carry the trait method's own type parameters, with no writable syntax.  Writable associated types with parameters (`type Name<T>;` in a trait) are a possible additive feature, perhaps after v1.0. |
| 31 | Which cheap opaque iterators promise `+ Copy` is decided per method and reported to Jake. |

The slice and API pass (item 7 under "Order to v1.0") is absorbed into this
work: read-only `&` and const pointers, `Read` on slices, `Slice`
finished, the `![implicit]` docs and `implement Trait` in type position.

### Left to packages, or to after v1.0

| domain | decision | reason |
|---|---|---|
| Regular expressions | package, after v1.0 | A correct linear-time engine is large, and nothing in the tree needs one. |
| Compression (deflate, gzip, zstd) | package | Nothing in the standard library needs it; CRC32 is the piece it would share. |
| Cryptography beyond SHA-1, SHA-256 and HMAC | package | Ciphers and signatures belong behind OpenSSL or a dedicated package.  Secure random exists (`SecureRng`). |
| Big integers | package | Arbitrary precision is its own library. |
| Memory-mapped files | package | Only raw `sys::mmap` exists, with no Windows arm; an owning `MappedFile` for both OSes is additive. |
| Time zone database | package | Fixed and local offsets are in the standard library. |
| Unicode normalization, graphemes, collation | package | |
| Glob | package | The standard library has the recursive walk. |
| A libc-free `core` tier for `runtime/` | after v1.0 | `runtime/` stays free of the standard library; `core::mem::copy`, `zero` and `swap` keep their names when moved. |
| A Windows reactor for pipes (IOCP on named pipes) | after v1.0 | Async child I/O works through the blocking pool. |
| Linked list, small vector, bit set, interner, slab | package | |

## Decisions for the test infrastructure

Jake's answers to the test-infrastructure plan.

1. **The fuzz farm runs on both WSL and Windows**, at a quarter of the
   cores, under a 6 GB farm-wide memory cap, between 01:00 and 07:00, and
   pauses while `soloapps-train` runs in WSL or a full-screen application
   is in front on Windows.  WSL runs the coverage, AddressSanitizer and
   fork-server lanes; Windows runs the OS differential's Windows half and
   the Windows language server.
2. **The interpreter runs before specialisation,** with its own
   implementation selection and layout written from the specs.
3. **The program generator's prelude is measured first,** leaning towards
   a minimal fuzz core.
4. **The fuzz corpus is stored outside the repository,** with a small
   curated seed set in `tools/CryoFuzz/seeds/`.
5. **The fixed-seed fuzz smoke blocks verify; the farm never does.**  At
   most 5 automatically filed bugs a night.
6. **Integer overflow traps in debug builds and wraps in release.**
   `overflow_checks` is a profile key (true in `debug`, false in
   `release`, settable in a `[profile.<name>]` table, unchanged by
   `--opt-level`); division by zero and `MIN / -1` panic in both profiles;
   shifts by the width or more are decided separately.  `docs/cryo.md`
   (Arithmetic Operators, "Overflow") states the rule.
7. **"The last release" means the pin until 1.0, then the tags.**
8. **The edit-fuzz gate is written in Cryo**, as the first `cryofuzz`
   subcommand in `tools/CryoFuzz`.
9. **The language server is a 1.0 deliverable,** so fuzzing it is
   required.
10. **The property framework** is `std::test::property`, with the
    `Property` trait, `run`, and `CRYO_PROPERTY_SEED`.
11. **A recursive directory walk and a child's peak memory are public
    standard-library API.**  Job objects and shared memory stay private to
    `tools/CryoFuzz`.
12. **The Python gates stay for now; the goal is to drop Python
    altogether** once the new infrastructure can replace them, possibly
    after 1.0.
13. **A bug found while building is filed and building continues,** unless
    its fix is genuinely very easy.
14. **The interpreter is internal to `tools/CryoFuzz` for 1.0;** there is no
    public `cryo interp`.
15. **Ported gates live in a second binary, `cryogate`,** in the
    `tools/CryoFuzz` project, sharing its library.
16. **`legacy/` is frozen:** not ported, not deleted, not edited.
17. **The wrapping arithmetic names** are `wrapping_add`,
    `wrapping_subtract`, `wrapping_multiply`, `wrapping_negate` and
    `wrapping_power`.  `power` overflows as `*` does and a signed `abs` as
    unary `-`; a compound assignment's overflow is judged at its target's
    type.

## Open questions

- **How wide a `char` is**, and so what a character literal holds above
  255 (`'\u{e9}'` fits either way; `'\u{1F600}'` does not fit a byte).
  Undecided.
- **A module cycle's report.**  E0501 names the modules in a cycle; whether
  it prints the cycle as a path, in order, is open, and so is whether a
  cycle that crosses packages is E0501 too or an error of its own.
- **`std::Range`.**  Nothing re-exports `Range` at the root of `std`, so the
  path is refused today.  Whether `stdlib/lib.cryo` exports it there is open.
- **Overlap of implementations bounded on a type.**  Two `implement` blocks
  whose heads overlap are refused when their bounds are on their own
  parameters; a block with a bound written on another type (`where
  HighError: From<E>`, 618) takes part in no overlap check, so two such
  blocks for one instantiation are not reported.  Whether, and how, two
  such bounds are compared is open.
