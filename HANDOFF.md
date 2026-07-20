# HANDOFF — working through `REPORT.md`

**Objective:** get as much of `REPORT.md` genuinely done as possible, ticking
items as they land. `REPORT.md` is the work queue.

Read this whole file before touching code. §1 and §7 exist because previous
agents got those things wrong first, and §6 records claims — in `REPORT.md`
itself — that are **factually wrong**. Working from the report text alone will
waste your time and, in one case, would have introduced a bug.

---

## 0. READ FIRST — machine switch in progress

**Two commits are UNPUSHED on branch `ll-impl`:**

```
928cc861  docs: specify Send/Sync semantics and the capturing-closure boundary
08815532  Windows path semantics, dead-code removal, and E0406 unknown enum variant
```

`08815532` contains the **refreshed pins** (both ELF and PE). Push before
switching machines or the Linux box gets a stale `bin/cryo` and rebuilds
everything from an older compiler.

**Uncommitted, not yet staged:**

- `.gitignore` — a real fix, see §5.2. Worth keeping.
- `tools/CryoLSP/tests/smoke.py` — new LSP smoke harness, see §5.1.
- `tools/CryoLSP/AUDIT.md` — **pre-existing**, not written this session. It was
  invisible to `git status` until the `.gitignore` fix; decide whether it
  should be tracked.

---

## 1. Standing rules (Jake's — these override your defaults)

**1.1 — If a change is the more correct option and it breaks the build, FIX THE
BREAKAGE.** Do not revert, do not carve out an exception. A wave of errors
after a correctness fix is the fix *surfacing existing sloppiness*.

> This keeps paying off. Deleting the dead `TypeAnnotation::Optional` variant
> broke the LSP in three places; fixing those was right, and reverting would
> have kept dead code the report explicitly wanted gone.

**1.2 — When something genuinely needs Jake's opinion, ASK.** Use the question
tool. For language-semantics / semver decisions with two defensible answers,
and before starting a LARGE Tier-2 rock. Not for routine judgement calls.

**1.3 — `REPORT.md` is TICK-ONLY.** The only edit it may receive is flipping
`- [ ]` to `- [x]`. No fix summaries, no corrections to finding text, no status
notes. Findings worth recording go in **this** file.

**1.4 — Comments describe the logic, not the project narrative.** No `D1 §2.4`,
`Batch A`, dated `audited 2026-…` stamps, or "this used to be X" framing. Keep
the *invariant* and the *failure mode*; drop the label and the story.

**1.5 — Do not commit unless asked.** Jake commits. He authorised it twice this
session explicitly, each time with **no co-author trailer** — if asked again,
omit both `Co-Authored-By` and `Claude-Session`. You **may** repin.

**1.6 — Recurring preferences:** no free functions (prefer methods / namespaced
statics); no type-suffixed method names (prefer one generic + `static match (T)`);
avoid suffixed numeric literals; proper solutions over workarounds.

---

## 2. Where things stand

`REPORT.md` is at **52 checked / 38 unchecked** (was 42/48 at the start of this
session, 25/65 when the whole pass began).

Landed this session, all gated:

| Item | Note |
|---|---|
| Generic-call lookahead misparse | Two bugs: scan escaping its group, plus the name-table disambiguation Jake chose |
| Injected imports invisible to leaf closure | Silent-miscompile hole, proved in both directions |
| 64-bit → 128-bit cache keys | `fold<T>` moved to the `Hasher` trait; new `Fnv128Hasher` |
| `fs/path.cryo` Windows semantics | Root-aware parsing: drive, UNC, drive-relative, both separators |
| D12 dead code | Incl. the whole `TypeAnnotation::Optional` variant, 20 arms, 11 files |
| `ffi/libc.cryo` Windows `readlink` | Magic-string shim gone; dispatch moved into `env::current_exe` |
| CryoAnalyzer version handshake | Warns on server/extension mismatch, names the resolved path |
| `core/error.cryo` | Deleted per Jake's call (zero consumers) |
| `Send`/`Sync` + closure-boundary docs | New `cryo.md` §16.4 and a §2.5 subsection |

Plus, not in `REPORT.md`: `-> never` functions failed to compile at all
(E0633); new **E0406** for unknown enum variants in patterns; four broken
doc anchors.

---

## 3. Build & gate procedure — **you are now on LINUX**

Most of the previous handoff's traps were Windows-specific and no longer apply.
On Linux:

```
make cryo            # REQUIRED FIRST
make test            # expect: OVERALL PASS (compile-fail 136; projects 8)
make selfhost-check  # expect: FIXED POINT OK  ×2 — count it
make cryo            # AGAIN if selfhost-check removed the binary
```

**Still true on Linux:**

- **Count the fixed points.** Require **2**. `grep -c 'FIXED POINT OK'` works
  natively here (the UTF-16 tee-log problem was a PowerShell artifact).
- **`make test` does not rebuild the compiler** reliably enough to trust —
  run `make cryo` first or you gate a stale binary.
- **Trap 3 — the LSP is covered by NOTHING.** `make test` and `selfhost-check`
  do not build it, and `make lsp` builds it with the **pin**. A sema/AST change
  can leave `tools/CryoLSP` broken with every gate green. This bit us **twice**
  this session. Validate explicitly:
  ```
  cd tools/CryoLSP && CRYO_STDLIB=$PWD/../../stdlib cryo build
  ```
- **`cryo build <proj>` writes `build/` relative to the SHELL's cwd.** `cd` into
  the project first.
- **`NetHttp2::loopback_h2c_round_trip` is FLAKY** — "h2 server bind failed",
  socket-bind contention. It alternates pass/fail with an identical binary. Do
  not chase it as a regression; re-run.

**No longer relevant on Linux:** `CRYO_CC=gcc`, running `make` from PowerShell,
WSL delegation, the "selfhost-check deletes `cryo.exe`" trap, and the CRLF
warning noise. Note `.gitattributes` now enforces LF everywhere, so the old
"never bulk-sed" rule is obsolete — the working tree on the Windows box was
CRLF, but the index is LF and diffs are content-only.

**3.6 — Repinning.** `make pin`. Do **not** force `CRYO_CC` on it. Gates passing
is *not* sufficient evidence a stdlib change is safe: the gates build with the
pin and test with stage-2, so a change the pin can compile but whose
**consumers** it cannot passes everything and still ships broken. Check by
compiling a small consumer of the changed surface with `bin/cryo` directly.

---

## 4. Remaining work, in the order I'd do it

### 4.1 The three LSP items — **now unblocked, because you are on Linux**

The Linux LSP build **works** (verified this session: clean `initialize`
handshake, well-formed response). The Windows build does not (§5.1) — but that
is parked deliberately, and none of these three need Windows.

- **LSP `::` completion is a lexical text-scrape** while `.` is semantic
  (`handlers/completion.cryo:15-18`). Known root cause of the "works sometimes"
  flakiness. **MEDIUM.**
- **Non-ASCII position desync** (`server/line_index.cryo:9-13`). `character` is
  a **byte** offset. The server advertises `positionEncoding: "utf-8"` only when
  the client offers it (`handlers/lifecycle.cryo:40-51`), which VS Code does —
  so this is correct under CryoAnalyzer and silently wrong under clients that
  only offer UTF-16 (Neovim built-in LSP, Helix, older clients). The fix is real
  UTF-16 code-unit counting when utf-8 was **not** negotiated; the negotiated
  encoding needs threading from `lifecycle` into `LineIndex`. **MEDIUM.**
- **Semantic tokens: full-document only, O(n²) insertion sort**. The sort is
  mechanical. **SMALL.**

Use `tools/CryoLSP/tests/smoke.py` (§5.1) to verify — it drives the real binary
over stdio the way an editor does.

### 4.2 Other tractable compiler/tooling items (no approval needed)

- **Mono fixpoint gaps** — convergence metric misses method-spec-only progress;
  spec bodies walked once, not to fixpoint (`call_specializer.cryo:1303-1307`);
  dead `in_progress` cycle detection that can never fire. **MEDIUM.** The most
  interesting remaining Tier-1 item; deserves a fresh session, not a tail-end
  slot.
- **Bindgen flat namespace drops colliding symbols** (`bindgen/generator.cryo:119-144`).
  C++ overloads and cross-namespace leaf collisions keep only the first. Rename
  instead of dropping. **MEDIUM.**

### 4.3 Deliberately left unticked — read before re-doing

- **`static match` arm pruning compares raw `TypeRef.id`.** A previous agent
  wrote the alias/`InstantiatedType` peel, **could not make it change behaviour
  in any repro**, and reverted it. D1 made arena ids canonical; this is likely
  closed-by-D1. **Your first job is a failing repro.** Do not add a 16th
  `InstantiatedType`-unwrap copy (D11 hotspot) on an unprovable premise.
- **DirectPair expansion.** The reduced inline coercion loop is *fixed* (both
  paths call one `coerce_call_arg`). The item stays unticked because its
  headline is the `expected_count > n` **arity heuristic**, untouched — fixing
  that properly means threading the ABI plan into codegen (D10-adjacent).
  **Ask before starting.**

### 4.4 Needs Jake's decision — ASK

- The **Tier-2 rocks** (D3, D4, D6–D11) are LARGE and architectural.
- The **Tier-4 design-debt block**: `![sink]` receiver honesty, partial
  auto-drop, the frozen no-op surface (`unsafe`, `|>`/`<|`, `switch`, parse-only
  `async`/`await`/`yield`), the literal-widening trap, checked/wrapping
  arithmetic, naming idiosyncrasies.
- **Stdlib API gaps**: `chars()`, f-string format specifiers, `fmt::printf`
  type-safety, collections variety, `io::Seek`. These are 1.0 scope calls.
- **CryoFormat**: kill it or rebuild it token/trivia-preserving in Cryo.

`REPORT.md`'s own attack order says to decide these once, in writing, so 1.0
scope stops moving. That is still the highest-leverage thing Jake can do.

---

## 5. Parked: the Windows LSP failure (Jake will return to this)

### 5.1 The finding

**CryoLSP is completely non-functional on Windows.** It never answers a single
request: `FrameCodec::read_message` fails on the first frame with **`errno=22`
(EINVAL)**, `Server::run` returns 1, and the process exits having written
**zero bytes** to stdout. No error, no log, no crash — an editor just sees a
server that starts and does nothing.

**It is a REGRESSION, and therefore bisectable.** `tools/CryoLSP/AUDIT.md`
(2026-07-01) records the LSP "builds, launches, and works on Windows", verified
by scripted stdio against `examples/03-fibonacci`. It does not now. It is also
not a regression from *this* session — the pre-existing installed
`bin/cryolsp.exe` fails identically.

**Ruled out, each by direct experiment — do not re-test these:**

| Hypothesis | Result |
|---|---|
| `std::io::stdio` broken on Windows | No — scratch binary reads a pipe fine |
| `_setmode(_O_BINARY)` breaks reads | No — same call in a scratch binary, reads fine |
| One-byte-at-a-time reads | No — works standalone, `\r` preserved |
| 64 KiB stack `chunk` buffer (`BODY_CHUNK_SIZE`) | No — 4 KiB changed nothing |
| `&local_u8` buffer pointer shape | No — `&arr[0]` changed nothing |
| `STDIN_FD` wrong / Windows `read` shim | No — it is `0`, shim is correct |

**Best lead:** a direct `libc::read(0, …)` inserted into the LSP's own `main`
**succeeds**, and after it header parsing proceeds normally (the error changes
from `io` to `missing-cl`, because the probe consumed a byte). So fd 0 is valid
at startup, yet the *first* `Stdin::read` inside the run loop returns EINVAL.
`Server::new()` is the only thing between those two points.

The Linux build of the same source works, so this is platform-specific, not
logic.

**`tools/CryoLSP/tests/smoke.py`** drives the real binary over stdio
(initialize → didOpen → hover → completion → semantic tokens → mid-edit buffer
→ shutdown/exit) and asserts each step. It found this on its first run. It
should **pass on Linux** and currently fails at `initialize` on Windows, which
is correct behaviour.

### 5.2 `.gitignore` was silently hiding the LSP source tree

`.gitignore` had a bare `cryolsp` pattern intended for the built binary.
Unanchored patterns also match **directories**, and ignore matching is
case-insensitive on Windows and macOS — so it swallowed `tools/CryoLSP/` whole.
Already-tracked files kept working, so nothing looked wrong, but **every new
file added there was invisible to git**. `AUDIT.md` was already a casualty.

Fixed by re-including the directory and explicitly naming the two alternate
build trees (`build_dbg/`, `build_linux/`) that had only been ignored as a side
effect. Verified both directions: source visible, build output still ignored.

---

## 6. Verified findings — do NOT re-verify or re-litigate

`REPORT.md` claims that are **factually wrong**. Corrections live here, not
there (rule 1.3). This list is now at **five**.

1. **`void*` == strcmp — INVERTED.** The *comment* was false folklore; the code
   was correct. Comparisons are gated strictly on `TypeKind::String`. The
   comments are deleted. **Anyone who "fixes" the comparisons on that comment's
   authority introduces a bug.**
2. **Import cycles.** A genuine bidirectional `import` fails hard with
   `E0501_CIRCULAR_IMPORT`. Cycles work via two *intentional* mechanisms (the
   `is_loading()` guard, and `public module` submodules never calling
   `add_dependency`), not via the silent-drop bug.
3. **Diagnostics coverage** is **67/200** E-codes (33.5%), not 63/217. 0/12
   W-codes. Valgrind runs on **every push**, not nightly.
4. **D12's "stale 40-line comment"** at `type_map.cryo:23-41` is **not stale** —
   it documents the live `TypeMapperCache` singleton. Sub-claim struck.
5. **D12's `parse_lambda_block_body` and `generate_bodies` claims were both
   wrong.** `parse_lambda_block_body` was *not* dead — it was called from an
   `ExprParser::parse_inner_block` override, so deleting it alone breaks the
   build (Jake chose to delete both, which is what landed). `generate_bodies`
   was dead by having **zero callers**, not by being a no-op — it really calls
   `codegen_global_var`, and deleting it on "it's empty anyway" without checking
   that call would have been the risky move.
6. **The closure-boundary item's description was wrong.** It claimed the E0458
   limit was documented in CHANGELOG "Known limitations". That section lists
   async/await, macros, and macOS — closures are not mentioned. The only E0458
   mention anywhere was a parenthetical inside CHANGELOG's iterator-combinator
   paragraph. Now fixed properly (`cryo.md` §2.5).

Also established this session:

- **The capturing-closure boundary is deliberate and enforced, not
  half-implemented.** Capturing closures may be passed to **non-generic free
  functions only**; generic functions, methods, and scope-resolution calls are
  E0458, and `extern "C"` callbacks can never take one. The distinction is
  **capture, not syntax** — a non-capturing lambda is a function pointer and
  binds everywhere.
- **`Send`/`Sync` are compiler-decided structural markers**, like `Copy`;
  `implement trait Send` has no effect. Raw pointers are unconditionally
  `Send + Sync`, the deny-list is a fixed 4 entries keyed on qualified name, and
  lock constructors carry no `T: Send` bound (so `Mutex<Rc<_>>` is
  constructible). Treat `Send` as advisory at the edges.
- **An unknown enum variant in a match arm** used to get no diagnostic at all —
  the binder fell back to binding against the *subject's* type. Now **E0406**.
  Severity is lower than it looks: exhaustiveness (`E0405`) catches the case
  where a real variant ends up uncovered, so a bogus arm alongside complete
  coverage was only ever dead code.
- **`u128` works correctly in Cryo**, verified byte-exact against a reference
  FNV-1a implementation including 128-bit multiply.

---

## 7. Method notes — two habits that earned their keep

1. **Verify the report against the tree before working from it.** Six claims
   were wrong (§6). In the `void*` case, following the text would have
   introduced a bug; in the `parse_lambda_block_body` case it would have broken
   the build.
2. **When a fix is supposed to reject something, prove it rejects it — and
   write the test so a partial fix fails.** The injected-imports cache fix was
   proved by showing the consumer's key was *unchanged* on the pin and *changed*
   after, **and** that an unrelated rebuild still reported "up to date" — a fix
   that simply always-invalidated would have looked identical without that
   second check.

---

## 8. Do NOT

- **Do not add prose to `REPORT.md`.** Tick only.
- **Do not revert a correct change because it breaks the build** (rule 1.1).
- **Do not "fix" the `void*` comparisons** — the comment was what was wrong.
- **Do not reintroduce structural fallbacks in `check_compatibility`.** The
  deleted Tuple one was actively **unsound**.
- **Do not touch `unify` or `check_compatibility`'s coercion branches** — they
  are *supposed* to walk structure.
- **Do not re-add `index`/`bounds` to the `GenericParam` intern key** —
  name-only keying is a deliberate correctness fix.
- **Do not assume green gates mean a stdlib change is safe to ship** (§3.6), or
  that they cover the LSP at all (Trap 3).
- **Do not chase `NetHttp2::loopback_h2c_round_trip`** — it is a bind flake.
