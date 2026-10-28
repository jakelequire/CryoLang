# Cryo

A self-hosted compiler and language. `bin/cryo` (the pin) builds everything;
the compiler is written in Cryo, in `compiler/src`, and the standard library
is in `stdlib`. LLVM 20 backend, Linux + Windows.

## Before writing a helper: grep the API index

```bash
grep -i "split" docs/stdlib-api.txt
```

`docs/stdlib-api.txt` is a generated one-line-per-symbol index of the whole
standard library, every namespace and declaration. Most utility
operations already exist. **A near-duplicate is worse than an imperfect call**,
because from then on there are two of them and they drift.

Regenerate with `make api-index` after adding public API; `make
api-index-check` fails if it is stale.

## Commands

```bash
make cryo                # build the self-hosted compiler (compiler/build/cryo)
make test                # unit + compile-fail + project suites
make test-census         # the same run, with the suite COUNTS asserted
make verify              # build once; census, examples, lsp, cross, incr, check-fast CONCURRENTLY,
                         # objects hashed; ARGS="--baseline HEAD --require-identical" proves
                         # a change moved no compiled output (baseline built in .verify/, cached);
                         # a change that makes a refused program compile DECLARES its project in
                         # tests/started-compiling (refused before, builds now); one that corrects
                         # a program the baseline built, in tests/started-passing
make roster-check        # every unit test, project and negative file against the roster
                         # golden (tests/test-roster.txt); its OK line prints the current counts
make check-fast          # verify's and verify-pin's self-tests + pin integrity, builds nothing.
                         # Run before committing.
make lsp-check           # compile tools/CryoLSP with the compiler under test
make cross-check         # runtime/stdlib/compiler/LSP for the OTHER OS, objects only, ~80s;
                         # from WINDOWS only - a Linux host links the windows-gnu
                         # build and fails without mingw + the LLVM-C import lib
make vendor-check        # every constant shape survives `cryo vendor`
make api-index-check     # docs/stdlib-api.txt is not stale
make verify-pin          # both pins match their sidecars (sha256 and stamped commit) AND
                         # each other
make verify-freestanding # runtime/ tiers built by the compiler under test
make examples            # smoke-build every examples/ project (refuses a sweep below
                         # scripts/examples-gate.py's --min)
make examples-golden     # build AND run them, diff stdout (POSIX only)
make incremental-check   # incremental build == clean build, per module
make incremental-instance-check  # an edit asking a cached module for a new generic
                         # instance still links (a verify gate: `incr`)
make selfhost-check      # byte-identity fixed point, BOTH OS, ~17 min
make help                # every target, and every side effect beyond its named output
```

Every target that runs the compiler under test (`test`, `test-census`,
`examples`, `lsp-check`, `verify`, ...) rebuilds `compiler/build/cryo` first
when any `.cryo` under `compiler/src` or `stdlib` is newer than it, so a manual
`make cryo` before them is not needed. That is a timestamp check: after a
branch switch, see the landmine below. `scripts/README.md` says what each
script does and who calls it.

### CI fires on `main` only, so on a branch the enforcement is LOCAL

That is deliberate - the commit rate here would otherwise spend real money on
Actions minutes - and it has a consequence worth stating: **everything in
`.github/` is a merge gate, not a branch gate.** Between now and the merge, the
only things that run are what a person or an agent runs.

So:

- **`make check-fast`** before committing: verify's and verify-pin's
  self-tests and pin integrity. It builds nothing and takes seconds, so it is
  no evidence about the compiler; `make verify` is.
- **No commit hook runs.** Nothing refuses a commit; the gates above are
  what a person or an agent runs.

### What a green gate does NOT tell you

Most of these were, at some point, reporting success for work they had
not done. The remaining limits are here so nobody rediscovers them:

- **A compiler warning total from an incremental build is not a count.** A
  module reused from cache skips the stage after monomorphization, and its
  warnings from that stage (unused variables, unused functions, unreachable
  code among them) are not replayed: the same source reads fewer warnings
  after a comment is added to a leaf file than it does clean. Take a warning
  total only after `rm -rf compiler/build`, and compare two builds' warnings
  as SETS (code, file, line), never as totals. The skip is
  `if (pm_active && pm_cached[...]) { continue; }` in
  `compiler/src/compiler/instance.cryo`; the fix is to store each reused
  module's warnings beside its object and replay them, and it is not done.
  This was diagnosed once already and lost, because it lived only in a
  status document's prose.
- **`make test` passes on an exit code.** A suite that ran nothing prints no
  header, no summary and no explanation, and exits 0 with `OVERALL PASS`.
  Use `make test-census` wherever the run is being taken as evidence; it
  reconciles what ran against the roster golden. `make test` stays for the
  working loop, where a pattern filter is the point.
- **`cryo test --list` enumerates the UNIT suite only** - it returns before
  the compile-fail and project suites. `roster-check` therefore enumerates
  those two from the filesystem itself. Adding a project or a negative file
  means re-pinning the golden (`--merge` when ADDING; `--update` only when
  deliberately REMOVING). `--update` no longer deletes the other host's
  OS-gated entries - it keeps a golden entry whose test is still
  `![target]`-gated elsewhere in the SOURCE, and drops one whose test is
  actually gone.
- **`outcome: "collect"` ignores every `expect` field.** `project_outcome`
  (`compiler/src/CLI/commands.cryo`) checks the child's exit code and nothing
  else, so a `collect` project whose `![test]` discovery broke runs zero
  tests, exits 0, and passes. Writing assertions into such a `test.json` does
  not make them run. `grep -l '"collect"' tests/tests/projects/*/test.json`
  lists the projects that use this fixture.
- **A compile-fail file with no `//~` annotation asserts a code and nothing
  else** - not the line, not the symbol, not the absence of a cascade. Write
  the annotations; `scripts/annotate-negative-tests.py` generates them from a
  real run and refuses to keep one that does not verify.
- **A diagnostic emitted with no `-->` span cannot be pinned to anything.**
  E0236 is one; its negative test can assert the code alone.
- **`selfhost-check` proves stability, not correctness.** Stage 3 builds
  stage 4, so a miscompile that reproduces itself is invisible to it.
- **`make lsp-check` builds with the stage-2 compiler**, which is what makes
  it a gate; `make lsp` builds with the pin and certifies nothing about the
  compiler being built.
- **`incremental-check` compares BINARIES, so it needs a reproducible link.**
  A Windows PE carries a link timestamp - two clean builds of one unchanged
  source differ - so the gate refuses on this host rather than reporting a
  matrix of failures that blame incremental compilation for the linker.
- **The release smoke runs the STAGED tree, and the Windows one under
  wine.** `scripts/release-smoke.sh` compiles and runs a hello-world with
  the staged tree before it is zipped, not with the archive itself; the
  verify job exercises a different binary by a different link (`make cryo`
  is not `--release-static`).  The Windows tree is smoked under wine with
  llvm-mingw's `clang.exe` driving the link, not on a Windows host with the
  mingw `gcc` an installed `cryo.exe` finds there.
- **A tag off `main` gets `release.yml`'s verify job and nothing else** - CI
  fires only on `main` or a manual dispatch. That job is the last gate before
  a published artifact; keep it in step with what the archive ships.
- **A Windows `make verify` baseline from before the `cxx` requirement stopped
  probing through the shell still leaks `The system cannot find the path
  specified.`** onto the last compile-fail file's line, which moves that
  file's verdict to the next line where verify cannot name it. The tree's own
  runner no longer prints it; a baseline that old does.

Read the log's **own summary line**, not a chained exit code — `make test;
echo $?; tail log` reports `tail`'s status.

Never edit sources while `make test` or `selfhost-check` is running; both
rebuild from `$(CRYO_SOURCES)`, and a mid-run edit reads as a broken fixed
point. Docs and `scripts/` are safe.

### Long jobs: background them, then poll INSIDE the same tool call

An agent does not wake when a backgrounded job finishes. A job launched
with the turn ended is a job whose result nobody reads; this has cost
seven sessions. The shape that works:

```bash
mkdir -p .verify; rm -f .verify/run.out
(make verify ARGS="--baseline HEAD" > .verify/run.out 2>&1; echo "JOB_EXIT $?" >> .verify/run.out) &
for i in $(seq 1 19); do
  grep -q JOB_EXIT .verify/run.out && break
  sleep 30
done; grep -a "^verify\|^  [a-z]\|JOB_EXIT" .verify/run.out
```

- Launch detached, then **poll in a bounded loop in the same call** (under
  the tool's ten-minute limit); chain another bounded loop if it is not
  done. Never end the turn waiting.
- The done marker is written by the wrapper **whatever the job's exit**, so
  a job that dies in its first ten seconds ends the loop at once instead of
  after ten minutes of waiting on a corpse. Delete the log before
  relaunching: a stale marker in a reused log ends the loop immediately.
- Read the job's OWN summary line at the end, not the loop's exit code.
- A bare `python -` (or `python - <<EOF`) with nothing on stdin hangs the
  call until the timeout backgrounds it. Every script goes in a file.

## Specs — normative, and they win

| document | governs |
|---|---|
| `docs/cryo.md` | the language |
| `docs/resolution-rules.md` | the five rules resolution is done by, and the doors - the only functions a spelling may be looked up through; its appendix holds the rulings |
| `docs/roadmap.md` | the work planned for after the merge, and the open questions |
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

## A gate is not fixed until a mutation that used to pass now fails

Touching a gate - adding one, widening one, tightening an assertion - is not
done when the gate passes. **Break something the old gate accepted, and show
the new one refusing it.** Then restore.

State it as a pair, because only the pair is evidence:

- the OLD gate over the broken tree, reporting OK,
- the NEW gate over the same broken tree, reporting the failure.

Without the first half you have shown that a gate fails on a broken tree,
which was never in doubt. Without the second you have shown nothing at all.
Both halves go in the commit message, named concretely: *"the old gate printed
`OK (N tests)` over a tree with a project's marker deleted"* is checkable;
*"now catches missing markers"* is a claim.

This is the same discipline as predicting the measurement, applied to the
instrument rather than to the code, and it is the only way to tell a gate that
covers more from a gate that merely runs more. Every hole in this suite that
was ever found was found by tripping over it - which is what a gate reporting
green over real breakage looks like from the outside.

**Run a control on the instrument too.** Before believing a count, put a case
you KNOW should appear through the same grep, regex or parser. Before believing
a checker you wrote, run it over the population that is already known to pass -
if it disagrees there, it is the instrument that is wrong. `grep -P` errors in
Git Bash and reports nothing, which reads exactly like a clean sweep.

**A guard that runs on every commit needs a test that proves it can refuse.**
Not a mutation done once by hand - a committed test, because the failure mode
is silence. A hook is installed, looks installed, and stops guarding: the
commit-msg guard was inert on every commit here because its shim picked its
interpreter with `command -v python3`, which on Windows resolves to an App
Execution Alias that prints "Python was not found" and exits 49. Before that,
one of its three rules had never fired at all, because a revspec was built as
`::path` and the staged side always read as empty. Both looked correct in the
source; the self-test found both. Choose an interpreter by RUNNING it, and
drive every rule through a throwaway repository in both directions - the case
it must refuse and the case it must let through.

## A brief does not override this file

Where a task brief and a standing rule here disagree, **the standing rule wins**,
and the conflict gets raised rather than resolved silently in the brief's favour.
A brief is written by someone who may not have this file in front of them; these
rules were written by someone who did, and who had already paid for them.

This has gone wrong twice: an exception to the commit-granularity rule, and a
side branch created because red work seemed to need somewhere to go. Both were
instructed, both contradicted something written here, and neither was flagged.
The cost is not the mistake, it is that the rule looked negotiable afterwards.

Raising it is cheap - one sentence naming the rule and the conflict - and the
answer may well be "do it anyway", which is a decision rather than a drift.

## Ask instead of assuming

**Ask in plain text, in the report or in `HANDOFF.md`.** An in-session question
dialog does not reach Jake - he works from the orchestrator's summaries, usually
on a phone and away from the Code tab - so a question raised there is the same
as a question not asked, except that it may hand you an answer you then
attribute to him. That has happened: a gate re-pin was asked for through the
dialog, answered, recorded as his ruling in the ledger and the commit message,
and committed on, before he had seen the question.

Do not guess on any of these:

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

**Describe Cryo on its own terms.** Never reference Rust, or any other
language, in code comments, commit messages, docs, error messages or test
names - no "as in Rust", "rustc-style", "like C++'s", "Cargo-style". State what
Cryo does and why, so the point stands without the reader knowing another
language, and so the text does not go wrong when Cryo's rule and the other
language's diverge. Naming C where it is the actual interface - the C ABI, an
`extern "C"` declaration, a C header, libc - is a fact about the boundary, not
a comparison, and stays.

**Commit a generator with what it generated.** Work that exists only as a diff,
produced by tooling that exists only in a scratch directory, is not reproducible
however mechanical it looks - and "it was script-generated" is exactly the
reasoning that makes deleting it feel safe. A 6,587-edit migration once had its
generator and its whole input population living in a session temp dir while the
edits lived on one branch; the branch diff was the only copy of either. If a
change was produced by a script, the script lands in the same commit.
A one-off codemod is then deleted in the very next commit: the commit that
ran it holds it, and that is where anyone reproducing the change looks. Only a
generator that is re-run (an index, a golden) stays in the tree.

**Measure before you conclude.** `nm`/`strace`/a counter beats an
intent-audit. Assumptions in this project have an unusually high rate of
turning out backwards — usually attributing a defect to the subsystem already
in view rather than the one actually responsible.

**A zero needs a control.** Before reading anything into a count of 0, ask what
would have to be true for it to be zero for an uninteresting reason. Several
honest zeros here were measured over the wrong population entirely.

---

## Retention: nothing temporary outlives its work

Scratch once reached 150 GB inside and beside this repo, most of it nobody
could name. These rules keep that from coming back.

- **Everything temporary has a deletion point, stated when it is made.**
  Debug scripts, probes, scratch builds, logs, instrumented compilers and
  codemods all count. Write the deletion point down with the thing ("delete
  when unit 2 lands"), not afterwards.
- **Scratch lives outside the repo, or in `.scratch/`** - the one ignored
  in-tree scratch location. Never anywhere else in the tree: not beside a
  test project, not in `examples/`, not in `stdlib/`.
- **Scratch is deleted when the work ends.** At the end of a session, delete
  your scratch. Copy anything worth keeping (a repro, a measurement) into a
  bug file, a test, or a commit message first.
- **A one-off codemod is deleted in the very next commit.** The commit that
  ran it carries it (see "Commit a generator with what it generated").
- **No dead code or dead scripts.** A function, target, script or file
  nothing calls is deleted in the change that orphans it, not left for
  later.
- **Build outputs are regenerable or they are removed.** Every directory a
  target writes is gitignored and removed by `make clean`. A target that
  writes somewhere new adds that path to both, in the same commit.

### The bug log: `.bug-report/`

Known open bugs live in `.bug-report/` at the repo root - gitignored, local
to the checkout, one markdown file per bug, named `NNNN-short-name.md`
(`0001-generic-class-allocate-link-error.md`) and numbered in order. Each
file has a title, a plain description, a minimal repro where one exists, what
is known about the cause, a severity (silent miscompile / crash / wrong
error / missing check / cosmetic / infrastructure) and where it came from.

- A bug found and not fixed gets a file, with its repro in the file - not
  only in a scratch directory that is about to be deleted.
- **A bug's file is deleted when its fix lands.**
- Features and plans go in `docs/roadmap.md`, not here.

---

## Conventions

- Bare integer literals: `1`, not `1u32` — including in synthesized code.
- A struct's or union's own methods, static and instance, go inline in its
  body: `type struct Foo { bar: int; static new() -> Foo { … } }`, not a
  separate `implement struct Foo { … }`.  `implement` blocks are for
  implementing a trait, for an enum's methods (an enum body holds only
  variants), and where the language requires one.  A type whose methods are
  split across files (`Parser`) uses `implement struct` blocks in the other
  files; the ones in its own file are inline.  A bound only some methods
  need is a `where` on those methods, which works inline on a generic struct.
- Names are written out in full, not abbreviated: `ExpressionNode`, not
  `Expr`; `declaration`, not `decl`, in new names.  It is the same reason the
  keyword is `implement` and not a shortening of it.
- No parallel/mirrored module structures; extend the module that exists.
- Commit messages are plain, with no trailers. That includes `Co-Authored-By:`,
  which some agent harnesses append automatically - turn it off at the source
  rather than editing it out per commit: `includeCoAuthoredBy: false` in
  `.claude/settings.json`. **`.claude/` is gitignored**, so that file is
  per-checkout and a fresh clone does NOT inherit it - set it again there. This
  line is the part that travels.
- A commit message says what changed and why, in plain terms. It does not
  cite internal bookkeeping: no ruling numbers ("Rulings 149-158"), old
  ledger entries ("8.540"), session numbers, `HANDOFF.md` sections, audit files or
  store/slice labels. Those are dead references within months; state the
  behaviour or rule itself instead.
- Cryo has no `else if` in an if-**expression**; statements are fine.
- Basic `grep` has no `\t` — use `grep -P` for tab-separated audit streams.

## Commits

**A change's docs commit with its code.** A ruling recorded in
`docs/resolution-rules.md`, a rule written into `docs/cryo.md`, an item moved
in `docs/roadmap.md` - each lands in the commit that builds it. A doc change
that lands apart from its code turns a behaviour change into two half-records:
the commit says what moved without saying why, and the doc claims a behaviour
with no diff to check it against.

- **Bundle to complete-work boundaries.** Prefer fewer, larger commits. A
  commit is one finished thing - the code, its tests, its goldens and its
  docs - not one file's worth of edits.
- **The same goes for goldens.** A re-pinned `test-roster.txt` rides with
  the change that moved the number, and the message says why it moved.
- Write the docs **before** the last gate pass, so they are staged with the
  code rather than appended after it. If a doc change belongs to a change
  already committed, fold it into the next commit touching the same area.

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
- **`make selfhost-check` on Windows deletes `compiler/build/cryo.exe`** and
  empties `stdlib/.bin/x86_64-pc-windows-gnu/` while it runs.  It rebuilds
  the archive from the pin, but not `compiler/build/cryo.exe`: run `make
  cryo` after it.  Start no other build during it - one that links while the
  archive is gone fails, naming the flat `stdlib/.bin/libcryo.a`.
- A scratch project outside the repo cannot find the stdlib — set
  `CRYO_STDLIB`. `compiler/build/cryo` cannot find it from another directory;
  `bin/cryo` can.
- `.gitignore` has a repo-wide `*.txt`. A new golden or generated `.txt` needs
  an explicit `!` negation or it is never committed and CI fails on a fresh
  clone.
- Copy the compiler out of the build tree before running it as an instrument,
  or it hits `ETXTBSY` overwriting itself.
- **Build artifacts do not switch branches with you.** `stdlib/.bin/<triple>/`
  and `compiler/build/` are gitignored, so they survive a `git checkout` and
  still hold whatever the *other* branch's sources produced. `make cryo`
  rebuilds them from wherever you were standing. After switching branches, run
  `make stdlib && make cryo` before believing any result — a stale archive
  reads exactly like a regression, and a green run over it means nothing.
- Module discovery is **import-driven**: a file nothing imports is never
  compiled, so a test project can silently exercise nothing.
- **The project COUNT is the evidence, not the word PASS.** `cryo test` echoes
  only FAILING projects, so a passing one prints nothing at all — and a project
  that never ran also prints nothing. The two are indistinguishable except by
  `projects: N passed` moving. After adding a project, check N went up.
- **A test project needs a `test.json`** (`{"outcome": "collect"}`) beside its
  `cryoconfig`, or `cryo test` skips the whole directory without a word. A
  freshly added project with no marker reports the same green, same N, as
  before it existed.
- cryoconfig keys are `project_name` / `entry_point` / `source_dir`.

## Enforcement (maintainer setup — not yet wired)

The rules above are honored by discipline, which is what gets violated under
deadline. To make them
harness-enforced, add to `.claude/settings.json`:

- **`PreToolUse`** on `Edit|Write` → `scripts/guard-edit.py`, exiting non-zero
  to block an edit that introduces `xfail`/`expect_fail`/a skip status under
  `tests/`, modifies a committed golden without an explicit override in the
  environment, or weakens an assertion in a file whose header declares a flip
  protocol.
- **`Stop`** → `make roster-check api-index-check`, so a session
  cannot be declared done while a ratchet is red.

`scripts/guard-edit.py` does not exist yet.
