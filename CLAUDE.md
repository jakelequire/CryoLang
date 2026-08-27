# Cryo

A self-hosted compiler and language. `bin/cryo` (the pin) builds everything;
the compiler is written in Cryo, in `compiler/src`, and the standard library
is in `stdlib`. LLVM 20 backend, Linux + Windows.

## Before writing a helper: grep the API index

```bash
grep -i "split" docs/stdlib-api.txt
```

`docs/stdlib-api.txt` is a generated one-line-per-symbol index of the whole
standard library — 154 namespaces, ~4,300 declarations. Most utility
operations already exist. **A near-duplicate is worse than an imperfect call**,
because from then on there are two of them and they drift.

Regenerate with `make api-index` after adding public API; `make
api-index-check` fails if it is stale.

## Commands

```bash
make cryo             # build the self-hosted compiler (compiler/build/cryo)
make test             # unit + compile-fail + project suites
make b1-check         # name-resolution fallback ratchet
make roster-check     # unit-test roster golden
make examples         # smoke-build every examples/ project
make selfhost-check   # byte-identity fixed point, BOTH OS, ~17 min
```

Read the log's **own summary line**, not a chained exit code — `make test;
echo $?; tail log` reports `tail`'s status.

Never edit sources while `make test` or `selfhost-check` is running; both
rebuild from `$(CRYO_SOURCES)`, and a mid-run edit reads as a broken fixed
point. Docs and `scripts/` are safe.

## Specs — normative, and they win

| document | governs |
|---|---|
| `docs/cryo.md` | the language |
| `docs/name-resolution.md` | symbol resolution, modules, visibility, scope |
| `docs/cryo-mangling-spec.md` | symbol mangling |
| `docs/abi.md` | calling convention, layout |
| `docs/grammar.md` | syntax |
| `docs/testing.md` | how to add tests |

Where the code disagrees with a spec, **the code is the defect**. Where a spec
has an open-questions section, an unanswered question is not yours to decide —
ask.

---

# How to work here

## Green is not the goal

**A red build with a correct diagnosis is a better outcome than a green build
with a workaround.** If you cannot fix something properly, leave it failing,
write down what you know, and stop. That is a successful session.

You are not scored on whether the suite passes. You are scored on whether what
you wrote down is true.

## When the build breaks or tests fail

Follow this in order. Do not skip to step 5.

1. **STOP.** Do not modify a test, assertion, golden file, or tripwire to get
   back to green. Weakening an assertion is a **finding**, not a fix. If an
   assertion looks wrong, say so and leave it.

2. **Check your instruments before your code.** A missing or half-built
   artifact reads exactly like a regression:
   - `stdlib/.bin/libcryo.a` present? If not: `make stdlib runtime-tiers`.
   - `runtime/.bin` left holding objects for the other OS? Same fix.
   - Anything rebuilding in another process right now? Wait for it.
   - Did you edit a source file while a long build was running? That reads as
     a broken fixed point. Re-run before believing it.

3. **Record the failure verbatim** — the log's own summary line, not the exit
   code of a chained command.

4. **Does it reproduce with your change reverted?** Stash and re-run. If it
   fails both ways it is pre-existing: say so, and continue with your task.

5. **Form ONE hypothesis and instrument it before acting.** A code comment is
   a hypothesis, not evidence. A counter that says "how much" cannot tell you
   "which one" — if you need to know which, emit a line at the event.

6. **After three failed hypotheses: stop.** Leave it red. Write down what you
   ruled out, how you ruled it out, and what you would try next.

## Before you change anything: predict the measurement

Write down, before editing:

- which corpus entry or test fails now and must pass after,
- which counter or number should move, in which direction, and roughly how much,
- what result would mean you were wrong.

Then check it afterwards. A change that produces the right outcome for the
wrong reason is indistinguishable from a correct one unless you said in advance
what the right reason looks like. If a number moves and you did not predict it,
that is a finding to explain, not a bonus.

## Ask instead of assuming

Use the question tool. Do not guess on any of these:

- A design question a spec leaves open. If its open-questions section does not
  answer it, it is not yours to decide.
- A new error code, diagnostic wording, or public API name.
- Deleting or relaxing an existing test, gate, golden, or lint.
- Anything that changes what an existing program compiles to, when that was not
  the stated task.
- Whether a defect you found is in scope for this session.

Being blocked on a question is cheap. Building on a wrong assumption is not.

## House rules

**Fix the root, don't route around it.** Rewriting compiler source is preferred
over making the compiler work around itself, even when the proper fix is
harder. A change that makes a defect unobservable while leaving its cause in
place is a failure, not a fix.

**One question, one answering path.** If a lookup fails, do not try a second
strategy. `if (a) { … } else { try_another_way() }` for the *same* question is
how this codebase grew a nine-step resolution cascade. Fallback chains are the
single most common defect class here.

**No special-casing by name.** A branch that mentions a specific module, type,
or symbol is a workaround until proven otherwise.

**No `xfail` / expect-fail / skip.** It was removed once and its re-addition
refused: a failing test beats a patchy green wearing a mask.

**Never edit a golden to match new output** without stating why the output
changed. A golden updated silently converts a behavior change into a
non-event.

**A comment is a hypothesis, not evidence.** Several comments in this tree say
a path is unreachable while it answers thousands of times. Instrument before
believing one, and before reverting on one.

**Comments describe the logic, not the project narrative.** Keep the
*invariant* and the *failure mode*; drop the label and the story. Specifically,
do not write:

- **Plan or spec coordinates** — `per spec §4.4`, `D1 §2.4`, `Batch A`,
  `phase 3`. Plans are deleted once the feature ships, and even a living spec
  renumbers its sections, so the pointer rots while looking authoritative.
  Name the rule instead of its coordinate: *"an inner tier shadows the outer
  one"* survives; *"§4 rule 1"* does not.
- **Dated stamps** — `audited 2026-…`, `LANDED 2026-…`, `fixed 2026-…`. Git
  knows when. A date in a comment only tells a future reader how long it has
  been since anyone checked.
- **Migration framing** — "this used to be X", "before the refactor", "the old
  behaviour was". The reader needs what the code does now and what breaks if
  it changes; the previous design is in the history.
- **A measurement as the justification** — `5 of 17 on examples/09-json-config`.
  The number is stale next week. State the mechanism it demonstrated: *"a trait
  default's body lives in the trait's file while the owner is the implementing
  type."*

Write the reason it cannot be otherwise, not the reason someone changed it. If
a decision genuinely needs its evidence recorded, that belongs in the spec or
the commit message, not at the call site.

**Measure before you conclude.** `nm`/`strace`/a counter beats an
intent-audit. Assumptions in this project have an unusually high rate of
turning out backwards — usually attributing a defect to the subsystem already
in view rather than the one actually responsible.

**A zero needs a control.** Before reading anything into a count of 0, ask what
would have to be true for it to be zero for an uninteresting reason. Several
honest zeros here were measured over the wrong population entirely.

---

## Conventions

- Bare integer literals: `1`, not `1u32` — including in synthesized code.
- No parallel/mirrored module structures; extend the module that exists.
- Commit messages are plain, with no trailers.
- Cryo has no `else if` in an if-**expression**; statements are fine.
- Basic `grep` has no `\t` — use `grep -P` for tab-separated audit streams.

## Environment landmines

- The runtime tiers build into `runtime/.bin/<triple>/`, one directory per
  target, so a Windows and a Linux build no longer overwrite each other's
  archives there and nothing needs cleaning between them. The directory is
  named from `cryo version --triple`, asked of the compiler because a native
  triple comes from a toolchain probe; an empty answer is a hard error, since
  building flat is the failure this prevents.
- The stdlib archive builds to `stdlib/.bin/<triple>/libcryo.a` for the same
  reason, so a WSL-native `make stdlib` no longer overwrites the Windows one.
  A flat `stdlib/.bin/libcryo.a` is the pre-split layout and is removed by the
  `stdlib` target; the flat path stays meaningful for an INSTALLED tree, which
  holds one target by construction, and a release stages it there.
- A scratch project outside the repo cannot find the stdlib — set
  `CRYO_STDLIB`. `compiler/build/cryo` cannot find it from another directory;
  `bin/cryo` can.
- `.gitignore` has a repo-wide `*.txt`. A new golden or generated `.txt` needs
  an explicit `!` negation or it is never committed and CI fails on a fresh
  clone.
- Copy the compiler out of the build tree before running it as an instrument,
  or it hits `ETXTBSY` overwriting itself.
- Module discovery is **import-driven**: a file nothing imports is never
  compiled, so a test project can silently exercise nothing.
- cryoconfig keys are `project_name` / `entry_point` / `source_dir`.

## Enforcement (maintainer setup — not yet wired)

The rules above are honored by discipline, which is the same thing
`docs/name-resolution.md` §7 says will be violated under deadline. To make them
harness-enforced, add to `.claude/settings.json`:

- **`PreToolUse`** on `Edit|Write` → `scripts/guard-edit.py`, exiting non-zero
  to block an edit that introduces `xfail`/`expect_fail`/a skip status under
  `tests/`, modifies a committed golden without an explicit override in the
  environment, or weakens an assertion in a file whose header declares a flip
  protocol.
- **`Stop`** → `make b1-check roster-check api-index-check`, so a session
  cannot be declared done while a ratchet is red.

`scripts/guard-edit.py` does not exist yet.
