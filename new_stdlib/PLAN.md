# Cryo Standard Library Rebuild — Plan

> **Status:** Phases 1–4 drafted (Arc, Deque, BTreeMap deferred).
> Phase 5 drafted: ffi/cstr, io, fmt, fs, env, math, net (TCP +
> HTTP server + client), process (Command + Child + signals).
> time, os still deferred (2026-04-23).
> **Why this exists:** The original `stdlib/` feels hacked together —
> null-terminated strings, `HashMap::get(key, hash)` with caller-supplied
> hashes, non-atomic `Rc`, O(n) `NullTerminatedArray`, O(n) `nth_char`. The
> root cause is that the old code was written as if Cryo had Rust's trait
> system and then patched when it didn't. This directory is the rebuild,
> designed around where Cryo is going.

---

## 1. Design principles

These are load-bearing. Follow them when adding anything new.

1. **A helper earns its place or it doesn't exist.** The old stdlib is full
   of methods that add noise without adding power: `x.is_zero()` when
   `x == 0` works, `some(x)` when `Option::Some(x)` works, `b.and(other)`
   when `b && other` works, `x.default()` when `0` works. Phase 1 deletes
   those. Every method must either aggregate a non-obvious check, express
   something operators can't, or be used often enough that inlining it
   would make call sites worse.

2. **Ownership is explicit and documented.** Every method that transfers
   or releases ownership says so in its doc comment. No hand-waving.

3. **Panic is for broken invariants; Result is for conditions callers
   handle.** `option.unwrap()` panics. `file.open()` returns `Result`.
   `array[i]` out of range panics; `array.get(i)` returns `Option`.
   Mixing these within a single operation is a bug.

4. **No null-terminated native types.** NUL lives only at the C ABI
   boundary, in `ffi::CStr` / `ffi::CString`. Every other type is
   length-typed: `(data, length)`.

5. **Length-typed slices are load-bearing.** Algorithms operate on
   `Slice<T>`, not on `Array<T>`. Every collection exposes `as_slice()`.

6. **Private fields, method-based access.** Structs do not publish their
   internals unless there is a specific reason. Users should not be able
   to mutate `array.length` directly.

7. **One name per concept.** `length`, not `len`. `capacity`, not `cap`.
   `buffer`, not `buf`. `index`, not `idx`. User preference.

8. **Doc comments document WHY, not WHAT.** `/// Returns the length.`
   above `length(&this) -> u64` is noise. Delete. Document non-obvious
   invariants, ownership contracts, performance surprises, and
   requirements on type parameters.

9. **Module-level `///!` comments are different** — they describe what
   the module is for and should exist on every file.

---

## 2. Key decisions (locked 2026-04-22)

### 2.1 Traits: write to the spec, not the current compiler

The stdlib uses traits from Phase 2 onward even though the current C++
bootstrap compiler can't parse them. `cryoc` stage 3 adds trait support;
until then, this code doesn't compile. That's accepted — the stdlib is
the spec, not a 2026-Q2 snapshot.

**Assumed trait syntax** (subject to final cryoc decisions):

```cryo
type trait Eq {
    equals(&this, other: &This) -> boolean;
}

type trait Ord : Eq {                // supertrait bound
    compare(&this, other: &This) -> Ordering;
}

implement Eq for i32 {
    equals(&this, other: &i32) -> boolean {
        return this == *other;
    }
}

function sort<T>(arr: Slice<T>) -> void
    where T: Ord {                   // inline where clause, per cryo.md
    ...
}

implement<T> Iterator<T> for Range<T> where T: Step {
    ...
}
```

Notes:
- `This` inside a trait refers to the implementing type. (Cryo uses
  `This` where Rust uses `Self`, matching the lowercase `this`
  value-level keyword.)
- Default methods are trait methods with bodies; required methods have
  none.
- Trait objects / dynamic dispatch are **not** used. Everything is
  static dispatch via monomorphization.
- No associated types in Phase 2. Traits that need "an associated type"
  (e.g., `Iterator`) take it as a type parameter instead
  (`Iterator<Item>`).

When cryoc's actual trait syntax diverges from the above, the stdlib
updates to match. The design doesn't change, just the spelling.

### 2.2 No Drop in Phase 1

`cryoc` stage 3 also adds auto-inserted destructors. Until then,
resource types expose `drop(mut &this)` that callers must invoke
manually. Every such method's doc comment makes that obligation
explicit.

### 2.3 Allocator parameterization

Collections will be generic over allocator (`Array<T, A = GlobalAlloc>`)
in Phase 4. Phase 1 is pre-collection, so this decision has no
immediate effect.

### 2.4 Naming

- `length`, not `len`.
- `capacity`, not `cap`.
- `buffer`, not `buf`.
- `index`, not `idx`.
- Predicates: `is_empty`, `is_digit`, `is_aligned`.
- Getters: no `get_` prefix unless disambiguation demands it.
  `array.length()`, not `array.get_length()`.

### 2.5 `IoResult` dies

Old stdlib has its own `io::IoResult<T>`. The rebuild uses
`Result<T, io::IoError>` everywhere.

### 2.6 Breaking changes are fine

No external code depends on the old stdlib API. Break whatever needs
breaking. No compatibility shims.

---

## 3. Architecture

```
new_stdlib/
├── PLAN.md
├── lib.cryo
├── prelude.cryo
│
├── core/                        # foundation, no external deps
│   ├── _module.cryo
│   ├── intrinsics.cryo          # malloc/free/memcpy/... + panic intrinsic
│   ├── panic.cryo               # panic, assert, unreachable
│   ├── primitives.cryo          # small method set on bool/char/ints/floats
│   ├── option.cryo              # Option<T>
│   ├── result.cryo              # Result<T, E>
│   ├── error.cryo               # Error struct (upgrades to trait in Phase 2)
│   ├── slice.cryo               # Slice<T> = (ptr, length)
│   ├── ptr.cryo                 # NonNull<T>
│   ├── mem.cryo                 # size_of, copy, zero, swap, align_*, transmute
│   │
│   │ # Phase 2 (traits):
│   ├── marker.cryo              # Copy, Send, Sync — marker traits
│   ├── clone.cryo               # Clone trait
│   ├── default.cryo             # Default trait
│   ├── cmp.cryo                 # Ordering, Eq, Ord; min/max/clamp
│   ├── convert.cryo             # From, Into, TryFrom, TryInto
│   ├── iter.cryo                # Iterator<Item> trait
│   ├── ops.cryo                 # Step, Range<T>, RangeInclusive<T>
│   └── hash.cryo                # Hash, Hasher, DefaultHasher (FNV-1a)
│
├── alloc/           [Phase 3]
├── collections/     [Phase 4]
├── fmt/             [Phase 5]
├── io/              [Phase 5]
├── fs/              [Phase 5]
├── math/            [Phase 5]
├── time/            [Phase 5]
├── env/             [Phase 5]
├── process/         [Phase 5]
├── os/              [Phase 5]
└── ffi/             [Phase 5]
```

Deliberately gone from the old tree: `NullTerminatedArray` (deleted outright);
`IoResult` (folded into `Result<T, IoError>`); `Rc`/`Weak`/`Shared` from
`core::ptr` (move to `alloc/rc.cryo` in Phase 3 with real atomics or a
loud single-threaded-only warning); `core::ops::Fn0..Fn3` (Cryo has
first-class function types; `(T) -> U` is enough); `MaybeUninit` with a
boolean flag (design is wrong without language support — defer).

---

## 4. Phase roadmap

- **Phase 0 — Compiler prerequisites** (cryoc stage 3): trait parsing +
  static-dispatch monomorphization with bounds, `Drop` with auto-
  inserted destructors, atomic intrinsics. Must land before the
  stdlib compiles, but not before it's written.
- **Phase 1 — Foundation** (complete): `core/` trait-free leaf modules.
- **Phase 2 — Traits wave** (complete): `marker`, `clone`, `default`,
  `cmp`, `convert`, `iter`, `ops`, `hash`. Retrofit Phase 1 types.
- **Phase 3 — Alloc** (drafted; blocked on Drop for real auto-cleanup):
  Allocator trait, Layout, GlobalAlloc, Box, Arena, Pool, Rc done —
  every owning type exposes a manual `drop(mut &this)` per §2.2. `Arc`
  deferred until atomic intrinsics land.
- **Phase 4 — Collections** (first slice drafted): Array, Str, String,
  HashMap, HashSet done. Deque and BTreeMap deferred as non-
  load-bearing for pre-codegen work.
- **Phase 5 — Essentials** (drafted): ffi/cstr, io, fmt, fs, env,
  math, net (TCP + HTTP), process (Command + Child + signals).
  `time`, `os` still deferred — neither blocks the self-hosted
  compiler or typical programs; each earns its module when a
  caller actually needs it.
- **Phase 6 — Migration**: swap stdlib/ → new_stdlib/ in the compiler's
  search path, fix fallout, delete old tree.

---

## 5. Phase 1 deliverables (complete)

- [x] `lib.cryo`
- [x] `prelude.cryo`
- [x] `core/_module.cryo`
- [x] `core/intrinsics.cryo`
- [x] `core/panic.cryo`
- [x] `core/option.cryo`
- [x] `core/result.cryo`
- [x] `core/error.cryo`
- [x] `core/slice.cryo`
- [x] `core/ptr.cryo`
- [x] `core/mem.cryo`
- [x] `core/primitives.cryo`

## 6. Phase 2 deliverables (complete)

- [x] `core/marker.cryo` — Copy, Send, Sync
- [x] `core/clone.cryo` — Clone
- [x] `core/default.cryo` — Default
- [x] `core/cmp.cryo` — Ordering, Eq, Ord, min/max/clamp
- [x] `core/convert.cryo` — From, Into, TryFrom, TryInto, ConversionError
- [x] `core/iter.cryo` — Iterator<Item>
- [x] `core/ops.cryo` — Step, Range, RangeInclusive + Iterator impls
- [x] `core/hash.cryo` — Hash, Hasher, DefaultHasher (FNV-1a)
- [x] Retrofit `option` / `result` / `slice` with trait impls
- [x] Upgrade `core/_module.cryo` to expose Phase 2 modules

## 7. Phase 3 deliverables

- [x] `alloc/_module.cryo`
- [x] `alloc/layout.cryo` — Layout (size + alignment invariants)
- [x] `alloc/allocator.cryo` — Allocator trait, AllocError, GlobalAlloc
- [x] `alloc/box.cryo` — Box<T> with manual drop
- [x] `alloc/arena.cryo` — bump allocator with chunk list + reset
- [x] `alloc/pool.cryo` — fixed-slot allocator with intrusive free list
- [x] `alloc/rc.cryo` — single-threaded Rc<T>
- [x] Expose `alloc` from `lib.cryo`
- [ ] `alloc/arc.cryo` — blocked on cryoc atomic intrinsics
- [ ] `Weak<T>` — deferred alongside Arc (shared cycle story)

Every Phase 3 owning type exposes a manual `drop(mut &this)` whose doc
comment spells out the caller's obligation. Auto-inserted destructors
arrive with cryoc stage 3; the call sites are already correct today.

## 8. Phase 4 deliverables

- [x] `collections/_module.cryo`
- [x] `collections/array.cryo` — `Array<T, A = GlobalAlloc>`, doubling
      growth, swap_remove, reserve/shrink_to_fit, manual drop
- [x] `collections/str.cryo` — `Str` (borrowed UTF-8 slice)
- [x] `collections/string.cryo` — `String<A = GlobalAlloc>` (owned
      length-typed UTF-8)
- [x] `collections/hash_map.cryo` — `HashMap<K, V, A>` with separate
      chaining, FNV-1a, manual drop
- [x] `collections/hash_set.cryo` — `HashSet<T, A>` over
      `HashMap<T, ()>`
- [x] Expose `collections` from `lib.cryo`
- [ ] `collections/deque.cryo` — ring buffer; deferred
- [ ] `collections/btree_map.cryo` — ordered map; deferred
- [ ] UTF-8 char iterator for `Str` / `String`; deferred until caller
      shape settles

Every Phase 4 owning type exposes a manual `drop(mut &this)`. Chaining
(rather than open addressing) was chosen for HashMap because empty
slots would otherwise require `MaybeUninit` — a Phase-6+ feature per
§3's "deliberately gone" list. Rework when the type system allows.

## 9. Phase 5 deliverables (essentials)

- [x] `ffi/_module.cryo`, `ffi/cstr.cryo` — `CStr`, `CString`, the
      only stdlib surface where NUL is allowed
- [x] `io/_module.cryo`, `io/error.cryo`, `io/traits.cryo`,
      `io/stdio.cryo` — `IoError`, `Read`/`Write` traits with
      default `read_all`/`write_all`, `stdin`/`stdout`/`stderr`
- [x] `fmt/_module.cryo`, `fmt/display.cryo` — `Display`/`Debug`
      traits, `Formatter<W>`, primitive impls, `print`/`println`
      helpers. Format-string parsing deferred until variadics.
- [x] `fs/_module.cryo`, `fs/path.cryo`, `fs/file.cryo` — `Path` /
      `PathBuf`, `File` implementing Read + Write, `fs::read` /
      `fs::write` whole-file helpers
- [x] `env/_module.cryo` — `args`, `var`, `set_var`, `remove_var`,
      `process_exit`. Relies on cryoc wiring `main`'s `argc`/`argv`
      into `env::set_args`; intentional hand-off documented in
      source.
- [x] `math/_module.cryo` — sqrt, cbrt, pow, exp, log*, sin/cos/tan
      (+ hyperbolic and inverse), floor/ceil/round/trunc, abs,
      classification helpers
- [x] Expose all six from `lib.cryo`

Deferred by design:
- `time/` — `Instant`, `Duration`, `SystemTime`. Nothing in the
  self-hosted compiler path blocks on them; defer until a module
  actually needs them.
- `os/` — platform constants (`LINE_ENDING`, `PATH_SEPARATOR`,
  etc.). Largely compile-time; cryoc can emit them directly rather
  than through a module indirection.

## 10. Phase 5 add-on: net + http

User-requested, in the spirit of Go's batteries-included stdlib:
"no third-party imports for a web server."

Shipped:

- [x] `net/_module.cryo`
- [x] `net/ip.cryo` — `IpV4Addr`, `IpV6Addr` (storage only),
      `IpAddr` discriminated union, `a.b.c.d` parser
- [x] `net/socket_addr.cryo` — `SocketAddr` + `a.b.c.d:port` parser
- [x] `net/tcp.cryo` — `TcpListener::bind/accept`,
      `TcpStream::connect` + `Read/Write` impls, manual drop
      closing fd, hand-packed `sockaddr_in`
- [x] `net/http/_module.cryo`
- [x] `net/http/method.cryo` — 9-variant `Method` enum + parse/wire
- [x] `net/http/status.cryo` — `StatusCode` + common-code
      constructors + reason phrases + is_* predicates
- [x] `net/http/headers.cryo` — `Headers` with lowercase-canonical
      keys, `parse_line` for the wire, manual drop
- [x] `net/http/request.cryo` — `Request::parse` reads request
      line + headers + Content-Length body; `write_to` emits for
      clients. 16 MiB body cap to reject runaway Content-Length.
- [x] `net/http/response.cryo` — `Response::new` + `text` /
      `json` convenience constructors, `write_to` emits,
      `parse` reads
- [x] `net/http/server.cryo` — `serve(addr, handler)` —
      blocking accept loop, connection-per-request. Handler
      signature `(Request) -> Response`; routing is the
      handler's job.
- [x] `net/http/client.cryo` — `Client::get` / `post` with a
      `SocketAddr` + path. Opens, writes, reads one response,
      closes.
- [x] Expose `net` from `lib.cryo`

Out of scope for Phase 5 (documented in `net/_module.cryo`):
- **TLS.** Belongs behind a trusted crypto stack wrapper at the
  `TcpStream` boundary. Writing one from scratch would be
  irresponsible.
- **UDP, Unix sockets.** Easy add; nothing's asked for them.
- **HTTP/2, WebSocket, chunked transfer-encoding, keep-alive,
  pipelining.** Phase 5 is HTTP/1.1 connection-per-request.
- **Full URL parsing, redirect following, cookie jars, DNS.**
  Clients that need these build them at the application layer for
  now.
- **Header iteration in write_to.** Blocked on HashMap iterator
  support; documented inline in both request and response. Callers
  setting Content-Length-only responses (via `set_body`) need to
  install that header manually in the current snapshot.

## 11. Phase 5 add-on: process

Moved out of the "deferred" list when the user asked for it. Goal:
Go-style `os/exec` — build a `Command`, spawn a `Child`, manage
pipes and signals without third-party code.

Shipped:

- [x] `process/_module.cryo`
- [x] `process/signal.cryo` — `Signal` wrapper + SIGHUP/INT/KILL/
      TERM/USR1/2/PIPE/CHLD/STOP/CONT/... constants
- [x] `process/child.cryo` — `Child`, `ExitStatus` with
      exit-code/signal decode, `ChildStdin`/`Stdout`/`Stderr` as
      `Read`/`Write` fd wrappers, `wait` / `try_wait` / `kill` /
      `send_signal`
- [x] `process/command.cryo` — `Command` builder (program, arg,
      env, env_clear, cwd, stdin/out/err), `Stdio` enum
      (Inherit/Null/Piped/Fd), `spawn` via fork + execvp,
      `status` and `output` sugar, `Output` with captured buffers.

Implementation notes worth knowing:

- Child post-fork code uses only async-signal-safe libc calls
  (`dup2`, `close`, `open`, `chdir`, `setenv`, `execvp`, `_exit`).
  If any fd plumbing fails the child `_exit(127)`s, the standard
  "exec not found" status; the parent sees that via waitpid.
- Env inheritance: default is "inherit parent env"; `env()` calls
  layer on top via `setenv` after fork; `env_clear` wipes via
  `clearenv` before layering. That avoids needing to expose the
  `environ` global explicitly.
- `Child::drop` closes any captured pipe ends but does **not**
  reap the child — the Cryo type system can't express
  must-use-ness yet, so forgetting to `wait` leaks a zombie.
  Documented on `Child::drop`.
- `Command::output` drains stdout then stderr sequentially. Small
  outputs are fine; large stderr while stdout is slow could
  deadlock. A `select`-capable version waits for an async story.

Still out of scope: detached / double-fork daemonization, process
group / session-leader control, Windows semantics (`CreateProcess`,
job objects), and signal *handling* (installing a `sigaction` from
Cryo — that crosses into async-signal-safety territory that
interacts with every other stdlib primitive).

Not included by design: `Iterator` adapters (`Map`/`Filter`/`Take`),
`PartialEq` / `PartialOrd` (floats use `==` directly; fine until someone
asks), `core::num` (pow/log/gcd), and an upgraded trait-based `Error`.
Each earns its module when a caller actually needs it.
