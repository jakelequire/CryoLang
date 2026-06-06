# Handoff: production-grade `std::time` and `std::random`

Date: 2026-06-05. Plan: `~/.claude/plans/distributed-bouncing-grove.md`.
Compiler gaps found this session: `BUG_REPORT.md` (read it — several
shaped the design).

---

## TL;DR status

- **`std::random` — DONE and validated.** Full trait-centered rewrite;
  all **15** tests in `tests/tests/stdlib/random.cryo` passed earlier this
  session (`make test ARGS="--filter Random"`).
- **`std::time` — ~80% done, NOT yet validated.** `duration.cryo` and
  `clock.cryo` are written and compile standalone. `datetime.cryo`
  (civil calendar, B3) is **not started**. Time tests are written but
  **never run** (blocked, see below).
- **CRITICAL BLOCKER:** `make stdlib` currently fails with **E0633**
  (unterminated basic block) in `stdlib/net/ws/conn.cryo::recv` — a
  module I never touched. It built fine earlier in the session; my later
  `duration.cryo` edits almost certainly shifted IR line numbers and
  tipped this **pre-existing latent codegen bug** (see
  `MEMORY.md`: "IR embeds line numbers" + the deferred `?`-in-`loop`
  E0633 note). **Nothing can be tested until this is resolved.**

---

## FIRST THING TO DO: unblock the build

The E0633 is in pre-existing untracked net-stack code, not in time/random.
Confirm cause, then pick a fix:

1. **Isolate.** Temporarily shrink `stdlib/time/duration.cryo` back toward
   the baseline (e.g. comment out `to_string`/`to_debug_string` +
   `copy_str`) and `rm -rf stdlib/.bin && make stdlib`. If it goes green,
   my line-count shift tipped it (expected).
2. **Fix options (pick one):**
   - (a) Make `net/ws/conn.cryo::recv` robust to E0633 — it has the known
     trigger (`loop { ... ? ... }` with an owning local needing drop as
     the last statement). Workaround from `MEMORY.md`: nest the `loop` in
     a `match` arm, or read the deref into a local first. This is the
     surgical fix and unrelated to time.
   - (b) Fix the E0633 codegen bug for real (unterminated-BB when an
     early-return/`?` is the last stmt in a `loop` with a pending drop).
     Bigger, but it's a recurring deferred bug — worth it.
   - (c) Last resort: keep `duration.cryo` smaller (move `to_string`
     bodies into fewer lines) to avoid tipping it. Fragile; don't rely on
     this.
3. Re-run `make stdlib` until green, then `make test ARGS="--filter Random"`
   (should be 15/15) and `make test ARGS="--filter Time"`.

---

## Remaining work (in order)

1. **Unblock the build** (above).
2. **Run the time tests** (`tests/tests/stdlib/time.cryo`, already written
   — 9 tests). Fix any failures in `duration.cryo`/`clock.cryo`.
3. **Write `datetime.cryo` (B3)** — the only unwritten piece. Spec:
   - `type struct DateTime { year:i64; month:u8; day:u8; hour:u8;
     minute:u8; second:u8; nanos:u32; weekday:u8; ordinal:u16; }`
   - `static from_unix_secs(i64)`, `from_system(&SystemTime)`,
     `utc_now()`, `static new(y,mo,d,h,mi,s) -> Result<DateTime, DateError>`
     (range-validated), `to_unix_secs() -> i64`, `to_system_time()`.
   - Pure-Cryo civil math (days↔civil, Howard Hinnant's algorithm), UTC
     only, no leap seconds. Private `days_from_civil`, `civil_from_days`,
     `is_leap`.
   - `DateError` enum (`InvalidMonth`/`InvalidDay`/`InvalidTime`); Eq/Ord.
   - **Formatting:** follow the `Duration` precedent below — provide an
     inherent `to_iso8601()/to_string()` that builds a `String` via a
     stack `u8` buffer + `to_decimal_buf` + the `copy_str` helper. You may
     also add `Display`/`Debug` trait impls (inline body — see GOTCHAS),
     but they are not usable via f-strings yet (compiler gap #2/#3 in
     `BUG_REPORT.md`).
   - Then uncomment `public module time::datetime;` in
     `stdlib/time/_module.cryo` and add DateTime tests (round-trip on
     epoch, 2000-01-01, 2024-02-29 leap, 2100-03-01 non-leap; weekday;
     `new` range rejection; ISO-8601 string).
4. **Docs + changelog:** update the `time`/`random` blurbs in
   `stdlib/lib.cryo` (lines ~220–236) to describe the new submodule APIs;
   add a `CHANGELOG.md` entry.
5. **Full gates (run ALONE — they race):** `make selfhost-check` **and**
   `make test`. Then re-pin: `make cryo && make pin-cryo` (per
   `MEMORY.md` house rule; pin is NOT refreshed yet).
6. **Decide on the compiler bugs** in `BUG_REPORT.md` — fix now or defer.
   The leaf-name const collision (#1) and the f-string/Display gaps (#2,
   #3) are the most impactful for "production-grade".

---

## File inventory

**`std::random` (DONE):** `stdlib/random/` — `_module.cryo` (rewritten),
`bits.cryo`, `error.cryo`, `source.cryo`, `rng.cryo`, `secure.cryo`,
`distribution.cryo`. Test: `tests/tests/stdlib/random.cryo` (rewritten).

**`std::time` (in progress):** `stdlib/time/` — `_module.cryo` (rewritten,
`datetime` line commented out), `duration.cryo` (new), `clock.cryo` (new).
Test: `tests/tests/stdlib/time.cryo` (rewritten). **`datetime.cryo` not
written.**

**Note:** the old single-file `stdlib/time/_module.cryo` content (Duration
+Instant+SystemTime+sleep) was split into `duration.cryo`/`clock.cryo`.
The old `stdlib/random/_module.cryo` single-file impl was split too.

**NOT mine / pre-existing untracked** (don't attribute to this work):
`stdlib/net/ws/`, `stdlib/net/tls/`, `stdlib/net/dns.cryo`,
`stdlib/net/https.cryo`, `stdlib/net/udp.cryo`, `stdlib/net/sockaddr.cryo`,
`stdlib/encoding/`, `stdlib/ffi/openssl.cryo`, the `compiler/*` edits, and
the `tests/tests/stdlib/net_*.cryo` + `tests/fixtures/`. These were already
in the working tree.

---

## Design notes / API shape (so the style stays consistent)

**`std::random`** is built on one trait, `RandomSource` (required:
`next_u64`; defaults: `next_u32/bool/f64/f32`, `next_below`,
`next_range`, `next_range_i64`, `fill`). `Rng` (xoshiro256**) and
`SecureRng` (getrandom) both implement it. `shuffle`/`choose` are **free
generic functions** in `source.cryo` (NOT trait defaults — see BUG_REPORT
#2). `distribution.cryo` has `Distribution<T>` + `UniformU64/I64/F64`,
`Bernoulli`, `Normal`, `Exponential`, `WeightedIndex`. Internal endian
helpers live in `random::bits` (public-but-internal, like `SliceIter`).

**`std::time::duration`** — `Duration` with checked/saturating/scale/
`mul_f64`/`divide`/`from_secs_f64`/`as_secs_f64` + constants + Eq/Ord +
inline `Display`/`Debug` trait impls + inherent `to_string()` /
`to_debug_string()` (manual stack-buffer formatting — the reliable path).

**`std::time::clock`** — `Instant`, `SystemTime`, `sleep`; checked
arithmetic; private `diff_saturating`. Its i64 nanos-per-sec const is
named `NSEC_PER_SEC` (NOT `NANOS_PER_SEC`) to avoid the leaf-name
collision with `duration.cryo`'s u64 `NANOS_PER_SEC` (BUG_REPORT #1).

---

## GOTCHAS that cost time this session (avoid re-hitting)

- **Enum methods:** match the receiver with `match (this)`, NOT
  `match (&this)` — the latter silently mis-evaluates (no diagnostic).
- **Don't extract a generic free fn** `write_x<W>(f: mut &Formatter<W>,
  d: &SomeStruct)` and call it from a `Display::fmt` impl — it **segfaults
  the compiler** (mangler ICE). Keep the formatting body **inline** in the
  trait impl, and do `to_string()` with a manual `u8` buffer instead of a
  `Formatter<String>` (a `Formatter<String>` in an inherent method also
  segfaulted). This is why `duration.cryo` has both an inline `Display`
  and a separate manual `to_string`. Add these to `BUG_REPORT.md` if you
  want them tracked (currently only #1–#5 + the enum note are there).
- **Same leaf name, different type, two files** under one module → the
  reference binds to whichever registered first (BUG_REPORT #1). Keep
  global const names unique across a module's files.
- **`implement struct X { }`** (separate inherent block for a struct)
  **segfaults**; put inherent methods inside the struct body.
- Run `make stdlib` / `make test` / `make selfhost-check` **one at a
  time** — they race and corrupt `.bin` if overlapped. After a crash,
  `rm -rf stdlib/.bin` before retrying.

---

## Quick verify once unblocked

```
rm -rf stdlib/.bin && make stdlib            # must be green first
make test ARGS="--filter Random"             # expect 15/15
make test ARGS="--filter Time"               # expect all green
# after datetime.cryo + docs:
make selfhost-check                          # run ALONE
make test                                    # run ALONE
make cryo && make pin-cryo                   # refresh the pin (not done yet)
```
