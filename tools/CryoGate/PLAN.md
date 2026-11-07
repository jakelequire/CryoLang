# CryoGate: the plan

`cryogate` is Cryo's test infrastructure, written in Cryo: the home and the
orchestrator of every gate, fuzzer, generator, oracle and triage tool the
repository runs, held to the bar of a mature language's test system. It covers
all of `.todo/TEST_INFRA_V1_PLAN.md`:

- the gates of today, with every Python gate replaced;
- fuzzing, incremental (edit) fuzzing included;
- CryoSmith, the random program generator;
- the reference interpreter for differential testing;
- refusal injection;
- property testing (`std::test::property`);
- corpus management, test-case reduction, bisect and bug filing;
- LSP protocol testing and LSP fuzzing;
- the overflow-checking rule's debug-compiler sweep;
- the plants that prove each tool can see what it claims to, the
  deterministic smoke in verify, and the farm.

Replacing every Python gate is a **milestone along the way** (§8, §11), not the
goal. At the end no Python is left in the build, the gates, CI or the release
(`legacy/` is frozen and untouched).

This is a plan. Facts about the tree were first read at `5d4a3c9a`
(`origin/perf`), and re-checked at `8828ddc3` (`origin/stdlib-visibility`),
after the perf branch was fast-forwarded into it: §7 says what perf changed.
Jake's answers to §13 are recorded there. It **supersedes** the test-infra
plan's §2a layout (a `tools/CryoFuzz` with `cryogate` as its second binary,
Decisions 8 and 17): every item of that plan keeps its design, its
must/should classification and its proof, and moves into the home described
here.

**Built so far:** G1's core (§8): the child runner, the scheduler, the
heavy-run lock and `cryogate heavy`, the run logs, `cryogate selftest`, and
`make cryogate`; the heavy Makefile targets run under the lock. Ported:
`roster` and `census` (G2, G3), `incr --instance`, `examples` and `vendor`
(G4), `cross` (G7).

---

## 1. Where it lives

`tools/CryoGate/`, holding two projects that build two binaries:
`tools/CryoGate/build/cryogate[.exe]` and
`tools/CryoGate/harness/build/cryogate-harness[.exe]`.

- `tools/` already holds the repository's own programs that are not the
  compiler: `tools/CryoLSP` (Cryo, built by its own make target) and
  `tools/CryoAnalyzer` (the editor extension); the old test-infrastructure
  plan put `tools/CryoFuzz` there too. The test infrastructure is the same
  kind of thing: programs the repository builds for its own use, not shipped
  to users and not part of the compiler.
- The directory follows the existing naming (`CryoLSP` builds `cryolsp`), so
  `CryoGate` builds `cryogate`.
- Not under `scripts/`: that directory is for scripts, it is being emptied by
  this work, and the roadmap restructures it.
- Not beside `compiler/` at the top level: the top level holds the language's
  deliverables (compiler, stdlib, runtime). `cryogate` is repository
  infrastructure.

### 1a. Two tiers, split by what each may depend on

Some tools must survive the compiler they judge; others can only work by
running the compiler's own code in process. That decides the split:

| tier | binary | built by | depends on | holds |
|---|---|---|---|---|
| **orchestrator** | `cryogate` | the **pin** | the stdlib only | every gate; the scheduler, sandbox and governor; the store and corpus management; CryoSmith; the differential driver; triage, reduction, bisect, bug filing, regression export; the LSP stdio driver and LSP fuzzing; the farm, the smoke, the plant gate; the property-test seed runs |
| **harness** | `cryogate-harness` | the **compiler under test** (as `lsp-check` builds CryoLSP), optionally with `--instrument=coverage` or `--sanitize=address` | the compiler library, and the orchestrator's library for the worker protocol | the per-phase harnesses and their oracles; the coverage counters and the fork server; the span (type-aware) mutators; refusal injection's site finder; the reference interpreter; the in-process LSP harness |

- **The orchestrator never links the compiler.** It drives `cryo` and
  `cryogate-harness` as child processes. A compiler change that does not
  compile, crashes or miscompiles cannot break the program that is meant to
  report it.
- **The harness is itself under test.** It is rebuilt with each compiler
  under test, because what it exercises is that compiler. If it does not
  build, every lane that needs it reports `FAIL: the harness did not build`
  with the diagnostic, and every other lane and gate still runs.
- **CryoSmith lives in the orchestrator on purpose.** A generator whose type
  model shared code with the compiler's checker would share its bugs. It
  emits text, so it needs nothing from the compiler. The interpreter is the
  opposite case: it consumes the compiler's checked AST, so it links the
  front end, and its selection, layout and ownership rules are written from
  the specs, never from the compiler's code (test-infra item 8).
- **The worker protocol** between the two is one record per line over the
  harness's stdin and stdout: the orchestrator sends an input (text or a
  path, the lane, the options); the harness answers with a verdict record
  (outcome, diagnostics as (code, file, line, column), the oracle that fired,
  coverage novelty, a signature). The harness's fork server stays inside
  the harness. A harness that dies mid-input is restarted, and the input that
  killed it is a finding.

### 1b. Layout

```
tools/CryoGate/
  cryoconfig          the cryogate executable; the stdlib only; built by the pin
                      (a [lib] is added when the harness first needs the
                      worker protocol's code)
  PLAN.md             this file (deleted with the last Python gate)
  src/
    main.cryo         argv -> subcommand
    selftest.cryo     `cryogate selftest`; probe.cryo, the child it runs
    heavy.cryo        `cryogate heavy`
    core/             child runner, scheduler, heavy-run lock, logs, the
                      unit-suite judge, host (OS, pin, repository); later the
                      sandbox limits, governor, verdicts, digests, tree walk,
                      git, WSL
    store/            the store outside the repo, records, corpus, pruning
    gates/            one file per gate (census, roster, examples, lsp, cross,
                      incr, vendor, selfhost, freestanding, pin, verify, ...)
    judges/           the pure decisions each gate's verdict rests on
    lsp/              JSON-RPC stdio driver, checks.json reader, LSP state fuzzing
    smith/            CryoSmith: model, generator, expected-output and drop log
    lanes/            the lane drivers: syntax, coverage, injection, diff,
                      incr, lsp, perf, property, interp (each mostly orchestration)
    diff/             differential axes: opt level, profile, OS, compiled /
                      expected / interpreted, last release
    triage/           signatures, dedupe, reducer, bisect, filing, regression export
    farm/             the farm, its governor hooks and the nightly report
    plants/           the plant gate's runner
  harness/
    cryoconfig        [[bin]] cryogate-harness; depends on ../../../compiler and ..
    src/
      phases/         lex, parse, resolve, sema, specialise, ownership, codegen
      oracle/         the shared harness oracles
      coverage/       counter export, fork server, cmp dictionary
      mutate/         token and span mutators (spans need types)
      inject/         site finder and the refusal rule table
      interp/         the reference interpreter
      lsp/            the in-process LSP handler harness
  plants/             <name>.patch + <name>.json (lane, budget, signature, source)
  seeds/              the small curated seed corpus
  tests/              ![test]s over the judges, the core, the model and the rules
```

Two cryoconfigs is the price of the tier split. The alternative (one project)
would make every gate depend on the compiler library, which is the property
§1a exists to avoid.

---

## 2. Principles carried over from the gates it replaces

Each of these is something a Python gate learned by being wrong once. A port
that drops one is weaker than the gate it replaces.

1. **A verdict line, not an exit code.** Every gate prints one summary line
   (`test-census: OK ...`, `lsp-gate: OK ...`); a gate that printed no
   verdict did not finish, whatever it exited. cryogate keeps every existing
   verdict line's spelling, so docs, CLAUDE.md and memory that quote them stay
   true, and so the side-by-side proof can diff them.
2. **A zero is a failure unless something could have made it non-zero.**
   Empty hash lists, an empty sweep, a suite with no summary block, a collect
   project that ran no tests: each is refused.
3. **A skipped arm fails** unless `--allow-skipped-arm` records it.
4. **Caches are keyed by the bytes that decide them**, and a key that misses
   an input is a gate reporting green over a change it never compiled. Every
   cache gets a mutation pair (an input the key must cover, changed, and the
   entry refused).
5. **The stdlib path is spelled the way each gate always spelled it**: objects
   embed it, so a different spelling moves every object.
6. **Nothing is temporary without a deletion point**: run logs keep the newest
   N; baseline compilers too (closing bug 0029, which the Python gate never
   did).
7. **Quiet by default.** Per gate: one line with name, ok/FAIL, seconds and
   the verdict line. Everything else goes to `build-logs/cryogate/<run>/`.
   `--verbose` streams. On failure, the tail of the failing log is printed
   (selfhost-check's behaviour today), never the whole thing.

---

## 3. Inventory: every gate script and what it becomes

Verdicts: **port** (a `cryogate` subcommand doing the same job),
**replace** (a better mechanism makes it unnecessary), **fold** (into a
compiler subcommand, because the compiler already holds the answer), **keep**
(not a gate, or must run before any Cryo program can), **drop**.

| file | lines | what it does | verdict | why |
|---|---:|---|---|---|
| `scripts/verify.py` | 1214 | builds the baseline compiler in a clone, runs six gates at once, hashes every object, judges the declared programs; records the baseline per program and reuses a record whose inputs are unchanged; counts objects the prebuilt stdlib archive replaced and hashes the archives | **port** → `cryogate verify` | the orchestrator is the reason cryogate exists; the post-perf script is final, so it is ported as it stands (§7) |
| `scripts/test-census.py` | 326 | runs `cryo test --format=plain`, reconciles counts and names against the roster golden | **ported** → `cryogate census` (script deleted; `verify.py` reads the parse from `cryogate census --read <log>`), then **replace its parsing** with the runner's report (§4d) | rulings 271-272 move the counts into the runner; the reconciliation against the golden stays a gate |
| `scripts/roster-check.py` | 543 | `--list` + project and negative fingerprints vs `tests/test-roster.txt`; `--merge`, `--update`, `--selftest` | **ported** → `cryogate roster` (script deleted; its self-test cases are `![test]`s; it also refuses a `test.json` list field the runner would skip, which the script pinned as one assertion per character) | pure file reading plus one child; once `--list` lists all three suites, the filesystem enumeration moves into the runner and this shrinks to a diff and the merge |
| `scripts/examples-gate.py` | 110 | builds every `examples/*` with a floor (`--min`) | **ported** → `cryogate examples` (script deleted; the floor raised from 14 to the 15 shipped) | small; the absolute forward-slash stdlib spelling is kept, though the objects no longer move with it (stdlib files are named `stdlib/<path>`) |
| `scripts/check-examples-output.sh` | 96 | builds and runs five examples, diffs stdout with the goldens (POSIX) | **replace**: the examples' `expected.out` become `stdout_file` assertions (ruling 271), run by `cryogate examples --run` | runs on Windows too; the hard-coded list becomes "every example with an `expected.out`", keeping the two-way set check |
| `scripts/valgrind-check.sh` | 73 | four examples under valgrind (Linux) | **port** → `cryogate examples --under=valgrind` | the same build-and-run loop with a runner prefix; one loop, not two |
| `scripts/lsp-gate.py` + `tools/CryoLSP/tests/smoke.py` | 189 + 1285 | builds CryoLSP with the compiler under test; drives it over stdio through protocol checks and every fixture's `checks.json` | **port** → `cryogate lsp` (§4g) | the largest port by line count; the JSON-RPC driver is reused by the LSP fuzzing lanes later |
| `scripts/cross-check.py` | 193 | the other OS's runtime, stdlib, compiler and CryoLSP, objects only, concurrently | **ported** → `cryogate cross` (script deleted) | once `cryo build` can stop at objects (ruling 322) it also passes on a Linux host (bug 0035) |
| `scripts/incremental-instance-check.py` | 90 | an edit asking a cached module for a new instance still links and reuses the cache | **ported** → `cryogate incr --instance` (script deleted) | small; part of verify |
| `scripts/incremental-check.py` | 267 | incremental build == clean build over a three-file edit matrix (Linux; a PE carries a link timestamp) | **replace** with the edit-fuzz gate written as `cryogate incr` on perf's bitcode-keyed module cache (question 3: cryogate writes it) | the fuzz gate compares `.o` hashes, not binaries, which also lifts the Windows refusal; the fixed matrix becomes one of its modes. Today's script also leaks its temp directory |
| `scripts/vendor-consts-gate.py` | 279 | header-vs-table check, `cryo vendor` under a throwaway `CRYO_HOME`, builds and runs a consumer of every constant | **ported** → `cryogate vendor` (script deleted) | needs only child processes, files and a temp directory |
| `scripts/api-index.py` | 498 | generates `docs/stdlib-api.txt` from a line scanner over the stdlib plus `nm` of the archive demangled by the pin | **fold** → a compiler subcommand that prints the index from the parsed stdlib (§5) | the parser already knows every declaration; a text scanner is why bug 0178 exists |
| `scripts/annotate-negative-tests.py` | 171 | writes `//~` lines from a real `cryo check` run, keeps only annotations that verify | **fold** → `cryo test` writing annotations (§5) | the code that checks an annotation is the one that should write it: one answering path |
| `scripts/cryo-pin.py` | 233 | copies, strips and writes the `.pin.txt` sidecar | **port** → `cryogate pin` | sha256, git, strip; runs where `make pin` runs (Linux / WSL) |
| `scripts/verify-pin.py` | 414 | sidecar sha256, stamped commit, the pair, and no PE HIGHLOW relocation; `--require-clean`, `--selftest` | **port** → `cryogate verify-pin` | pure byte reading; its 12 self-test cases become `![test]`s |
| `scripts/pin-windows.cmd` | 42 | Windows host: `make pin` inside WSL | **port** into `cryogate pin` (the WSL re-entry selfhost already needs) | one way to reach WSL |
| `scripts/selfhost-check.py` | 1240 | the fixed point, both arms at once on a Windows host (the Linux arm from a digest-checked mirror on WSL's own filesystem), the stage archives compared object by object (`STDLIB ARCHIVES OK`); wine arm on Linux | **port** → `cryogate selfhost` (§4f) | the post-perf script is final; helpers nothing calls (the design read named `_win_prereqs_missing`, `_llvm_link`, `_llvm_link_named`, `_config_without_bin`, `W4_BUILD_TIMEOUT`; re-check each at the port) are not carried over |
| `scripts/selfhost-check-windows.cmd` | 35 | finds Python and WSL, runs the script | **drop** with the port | the Makefile calls cryogate directly |
| `runtime/verify-freestanding.sh` | 495 | writes freestanding programs, hand-links them against the tiers, runs them on Linux and under wine | **port** → `cryogate freestanding` | becomes runnable from a Windows host: the Windows arm natively, the Linux arm through WSL, as selfhost |
| `scripts/release-smoke.sh` | 116 | builds and runs hello-world with the staged release tree | **port** → `cryogate release-smoke`, later | a gate; release packaging calls it. Ported in the last unit so the release has no shell gate |
| `scripts/build-release.sh` | 294 | stages the release archives | **keep** for now | packaging, owned by the installation branch; calls `cryogate release-smoke` once that exists |
| `scripts/fetch-windows-llvm.sh`, `scripts/ci/install-llvm.sh` | 229 + 124 | download and install LLVM | **keep** | they run before any Cryo program can be built on the machine |
| `scripts/install-lsp-windows.cmd` | 52 | packages and installs the VS Code extension | **keep** | not a gate; npm and VS Code tooling |
| `install.sh`, `install.ps1` | 400 + 197 | end-user installers | **keep** | not gates; the installation branch's |
| CI inline steps (`ci.yml`: the `bin/cryolsp` stdin smoke, `windows-smoke`'s `wine cryo.exe --version`; `release.yml` `publish` checks) | | small shell checks in YAML | **port** the LSP stdin smoke into `cryogate lsp`; the rest **keep** | the LSP one is a gate; the publish checks are release plumbing |
| `legacy/bootstrap/**/*.py` | | the old bootstrap | **untouched** (Decision 18) | frozen; nothing runs it |

`check-fast` (verify's self-test, the roster merge self-test, verify-pin's
self-test and verify-pin) becomes `cryogate selftest` plus `cryogate
verify-pin`.

---

## 4. Architecture

### 4a. How the orchestrator is built, and why it survives a broken compiler

This is the orchestrator tier (§1a). The harness tier is built by the
compiler under test by a target like `lsp-check`'s, into
`tools/CryoGate/harness/build/`, and only lanes that need it wait for it.

- **Built with the pin** (`bin/cryo build` in `tools/CryoGate`), against the
  tree's stdlib, by a file rule `$(CRYOGATE): $(CRYOGATE_SOURCES)
  $(STDLIB_SOURCES) $(PIN)` in the Makefile. Every gate target depends on it.
- **It does not link the compiler library.** CryoLSP does, so building it
  compiles `compiler/src` - which is exactly what is under test. cryogate
  drives every compiler as a child process (`cryo test`, `cryo build`,
  `cryo check`, `cryo version`) and reads their output, so a compiler change
  that does not compile, crashes, or miscompiles cannot reach the gate that is
  meant to catch it. It also keeps LLVM and libclang off cryogate's link line,
  so it builds in seconds.
- **The stdlib is the one shared input.** The pin builds the tree's stdlib
  into the archive every project links, so a stdlib the pin cannot compile
  already breaks `make stdlib` before cryogate is involved. A stdlib change
  that compiles but behaves wrongly (a `Command` that drops an argument, a
  `HashMap` that loses a key) could mislead cryogate. Two defences:
  1. `cryogate selftest` runs first in `check-fast` and in `verify`: its
     judges over fixtures, plus a child-runner smoke (exit codes, a timeout,
     a captured stream, an env var, a cwd) - a broken process API fails here,
     loudly, before any gate's verdict is believed.
  2. The proof pairs below run the Python gate beside the Cryo one until each
     port is accepted.

  cryogate is not pinned itself (question 2: built from the tree by the pin).
- **Built:** `make cryogate`, a file rule over `tools/CryoGate/src`, its
  cryoconfig, the stdlib sources, the runtime tiers and the pin; the pin's
  build of it links the prebuilt stdlib archive
  (`stdlib/.bin/<triple>/prebuilt/<key>/`), which perf made the only stdlib
  build there is (`make stdlib` is gone).
- **Only pin-era features.** cryogate's source must compile with `bin/cryo`,
  so it uses nothing newer than the pin. A gate that wants a new stdlib API
  waits for the repin that carries it (the two-phase repin the bootstrap
  already uses).
- **On a Linux host inside WSL**, the same source builds with `bin/cryo`
  (the ELF pin) into the mirror's own `tools/CryoGate/build/`.
- **In a fresh git worktree** (the `cryogate` branch is worked in one, beside
  the main checkout), two things the build needs are per-checkout and not in
  git, so set them up once before `make cryo` or `make cryogate`:
  1. On Windows, copy the main checkout's `.toolchains/llvm-win/` into the
     worktree's `.toolchains/` (gitignored, ~190 MB; the Windows LLVM
     `make cryo` links against). It is never committed.
  2. Run `make runtime-tiers` once: until `runtime/.bin/<triple>/` holds the
     tier archives, nothing links (`__cryo_panic` undefined).

  `.claude/settings.json` is per-checkout too (CLAUDE.md, Conventions).

### 4b. The core

- **Child runner** (`core/child`). One function runs a job: argv, cwd, an
  environment built from the inherited one with named removals and
  additions (verify removes `CRYO_STDLIB`; Windows sets `CRYO_CC=gcc` when
  unset), stdout and stderr to one log file or captured, a wall-clock limit,
  and an exit classification (code, signal, timeout, could not start). The
  job's peak memory is recorded when the stdlib can report it (§6).
- **Scheduler** (`core/schedule`). A run is a set of jobs with dependencies
  (verify: build the baseline, then baseline census + examples, then the
  tree's gates). Jobs are admitted against two budgets: CPU slots and memory
  (§4c). Results are reported in a fixed order, not completion order, so two
  runs' outputs diff cleanly.
- **Verdicts** (`core/verdict`). `Ok`, `Fail(reason)`, `Skipped(reason)`,
  `Declined` (not this run's job - selfhost's Windows arm under WSL). The run
  fails on any `Fail`, and on any `Skipped` without `--allow-skipped-arm`.
- **Digests** (`core/digest`). sha256 of a file, of a sorted file list (path
  and content, the `tree_digest` and `population_key` shapes), hex encoding.
- **Tree walk** (`core/tree`): recursive listing with pruned directory names,
  sorted, `/`-separated relative paths on every host.
- **git** (`core/git`): a child process, never a library. `ls-files -co
  --exclude-standard -z`, `rev-parse`, `show REV:path`, `clone --no-checkout`,
  `fetch`, `checkout --detach`. git is already a build prerequisite.
- **Host** (`core/host`): host OS, the triple (`cryo version --triple`,
  asked, never guessed), the WSL path of a Windows path (`wsl.exe -- wslpath
  -a`, forward slashes, as selfhost-check learned), whether `wsl.exe` exists.
- **Logs** (`core/log`): `build-logs/cryogate/<stamp>/<gate>.log`, newest 20
  runs kept; `.verify/` keeps its role for verify's state.

### 4c. Running in parallel within the machine's RAM

The census already runs `cryo test` with its own worker pool, and verify runs
six gates on top of it. On this machine (12 logical cores, 31 GB) verify is
CPU-saturated and has died with `allocation failed` when another session held
memory. So:

- **Each job declares a cost class** (light: under 0.5 GB; compile: one
  compiler process; suite: `cryo test` with N workers). The first runs use
  conservative defaults; the child runner records each job's peak memory in
  `.cryogate/costs` (keyed by gate and job kind), and later runs schedule from
  the measured figures.
- **Memory budget** = the smaller of `CRYOGATE_MEMORY_MB` and the physical
  memory free at start, less a 4 GB reserve. A job starts only while the sum
  of the running jobs' estimates plus its own fits. A job larger than the
  whole budget runs alone, and the run says so.
- **CPU budget** = `CRYOGATE_JOBS`, default the logical core count. A suite
  job receives its share through the runner's own jobs flag, so the census
  stops starting 12 workers while five other gates are also running.
- **Cross-worktree lock** (question 6: the convention now, by hand until
  cryogate takes it). A heavy run (verify, census, selfhost, pin builds,
  examples-golden, freestanding, profiling) takes one lock file for the
  machine. **Built** (`core/lock`): the file is the one the hand rule uses,
  `cryo-heavy-run.lock` in the directory that holds the checkouts (beside the
  repository; `CRYOGATE_HEAVY_LOCK` names another), created only if absent,
  holding its owner (`CRYOGATE_OWNER`, or the checkout) and the command line
  and start time. A held lock is waited for, polled every minute and named
  once, unless `--no-wait` (then refused); a lock older than three hours is
  reported and not taken. `cryogate heavy [--no-wait] -- <program> ...` runs
  any heavy proof under it, the Python gates included, until each gate takes
  it itself. **Built:** `make verify`, `test-census`, `selfhost-check` and
  `pin` run their recipes as `$(CRYOGATE_HEAVY) <command>`; the Windows
  `pin`'s re-entry into WSL calls `pin-impl`, which takes no lock, since the
  Windows side holds it. Ctrl-C leaves the file behind until the stdlib has an
  interrupt hook (§6 row 7); the staleness rule bounds the damage. This is
  what lets several workers share the PC without one's timings being
  another's noise.
- Verdicts never depend on the schedule: a gate run alone and run under verify
  produces the same verdict and the same objects (the proof checks it).

### 4d. Census and roster

- **First port: identical parsing.** `cryogate census` runs `cryo test`,
  streams nothing to the console by default (log file), and parses the same
  lines `test-census.py` parses (the summary blocks, the per-test lines, the
  project verdicts, the verdict token found wherever it lands - the `cxx`
  probe's leaked line taught that). It reconciles against
  `tests/test-roster.txt` with the same identities, the same OS waiver read
  from `![target]` directives, and the same collect-project rule.
- **Then the runner reports what it ran** (roadmap: rulings 271 and 272):
  `cryo test` prints a count for each suite, zero included, `--list` lists all
  three suites, and a machine-readable report file (one record per test,
  project and negative file, with its verdict and a collect project's test
  count) replaces the text parsing. The census then reads the report, and
  roster-check becomes a diff of `--list` against the golden plus the merge.
  The report's format and the summary line's spelling are Jake's (question 4).
- `roster --merge` / `--update` keep their semantics exactly, including
  `--update` keeping a golden entry whose test is still gated to another OS.

### 4e. Object comparison (verify)

The port keeps verify's whole contract: build once, the gates at the same
time, every `.o` under a `build/` directory of `tests/` and of every `examples/*` project hashed, a baseline compiler built in a clone under `.verify/` from REV's
`compiler/src` with the tree's other build inputs, the baseline compiler and
its hash lists cached by the bytes that decide them, `tests/started-compiling`
and `tests/started-passing` judged exactly as today, `--require-identical`,
`--only`, `--list-limit`.

The perf branch's changes are part of that contract now, ported as landed:
the baseline is recorded per program and a record is reused while the
inputs it was keyed by are unchanged (a unit that adds a test re-runs one
program, not 881); objects the prebuilt stdlib archive replaced are counted
in their own column and the archives hashed as a stdlib population; a
baseline is REV's `compiler/src` built against the tree's C headers (so a
header change cannot be verified against `--baseline HEAD` directly: the
baseline fails to build).

Additions the port makes because it is the natural place:

- **Bounded baseline cache** (bug 0029): newest N baseline compilers kept
  (`.verify/` held 82 at the end of the perf branch).
- **The moved list is written whole** to the run directory (today only the
  hash lists are), so a moved-object explanation starts from a file.

### 4f. Selfhost, including WSL

- **One implementation on both sides.** On a Windows host `cryogate selfhost`
  starts the Linux arm in WSL and runs the Windows arm natively at the same
  time (perf N2's shape). The Linux arm is the same program: the Windows side
  runs `rsync` in WSL to refresh the mirror
  (`~/.cache/cryo-selfhost/<checkout digest>`, the copy list and exclusions
  unchanged), builds the ELF `cryogate` there with `bin/cryo`, and runs
  `cryogate selfhost --arm=linux` from the mirror. The tree digest is
  computed by the same Cryo function on both sides, on the working tree and on
  the mirror, and a mismatch refuses the arm, as today.
- **Stages are data**: the six-stage table (runtime tiers by the pin, stdlib
  and compiler by the pin, stage 2, stage 3) is a list, with each stage's
  directory pre-created (the compiler's single-level mkdir).
- **The comparison is the whole tree**: the linked `cryo.ll` and every
  per-module `.ll` of stage 3 against stage 4, plus stage 3 and stage 4
  answering `version` identically, plus the stage-2 and stage-3 stdlib
  archives compared object by object (`STDLIB ARCHIVES OK`, perf). The
  Windows arm's llvm-link step and its `.ll` tree compare stay as they are.
- **The archive comparison reads each stage's own build**, not a before/after
  listing of `stdlib/.bin`: the script attributes any build that writes an
  archive there during the run to the stage that is running (perf saw an
  unrelated O0 archive fail a run whose fixed point was OK).
- **WSL must be running**: after a reboot `wsl.exe -e true` starts it; the
  port does that itself before the Linux arm, and names it when it fails.
- **Output**: one line per stage with its time, then the arms table; the
  Linux arm's log is written to `build-logs/selfhost-check/linux-arm.log` as
  now. Exactly two `FIXED POINT OK` lines on a passing Windows-host run (the
  memory note: a run is accepted on that count).
- The `.cmd` wrapper goes: the Makefile calls `cryogate selfhost` on both
  hosts. Perf dropped the stages' own stdlib rebuilds (each stage compiler
  builds its archive once, keyed by its own bytes); the stage table is the
  landed one.

### 4g. The LSP gate

`cryogate lsp` builds `tools/CryoLSP` with the compiler under test into its own
directory (never `bin/`), then drives the binary over stdio: JSON-RPC framing
(`Content-Length` headers), the protocol checks `smoke.py` makes today, and
every fixture's `checks.json` unchanged - no fixture is rewritten. Request
timeouts are the child runner's; a request the server never answers is a
failure naming the request (bug 0044's class). The driver lives in `src/lsp/`
so the LSP fuzzing lanes reuse it.

### 4h. What each gate prints

```
cryogate verify: gates over compiler/build/cryo.exe: census, examples, lsp, cross, incr, fast
  census    ok    407.2 s  test-census: OK unit 2394 (...), compile-fail 448, projects 395
  examples  ok     28.0 s  examples-gate: OK 14 example(s) built
  ...
  objects   tests    19019 (build-logs/cryogate/20261104-101500/tree.tests.sha256)
verify: OK in 452 s -- logs in build-logs/cryogate/20261104-101500
```

Every number in that line is the gate's own; the `OK` is derived from the
verdict line and the exit, as today.

---

## 5. Folded into the compiler, or replaced

Each fold is a compiler or runtime change, so each is its own unit with the
full proof (identical objects for everything else), and each new spelling is
Jake's (question 4).

1. **The runner reports what it ran** (rulings 271, 272; roadmap "The test
   runner reports what it ran"). `cryo test` prints a count for each suite,
   zero included; `--list` lists all three suites; a report file records
   every test, project and negative file with its verdict, and a collect
   project's test count. `test.json` gains `stdout_file`. Retires the census's
   text parsing, the roster's filesystem enumeration, and
   `check-examples-output.sh`.
2. **`cryo test` writes `//~` annotations** for a negative file (a flag on the
   negative suite), keeping only annotations its own checker then verifies.
   Retires `annotate-negative-tests.py`.
3. **The API index from the parser.** A compiler subcommand prints
   `docs/stdlib-api.txt` from the parsed stdlib, with visibility, and the
   symbol cross-check reads the archive's symbols through the compiler's own
   demangler without `nm`. `cryogate api-index [--check]` runs it with the pin
   (as the script does today) and compares. Fixes bug 0178. Whether the
   subcommand is public is question 4.
4. **`cryo build` stops at objects** (ruling 322). Makes `cross` pass on a
   Linux host (bug 0035).
5. **sha256 in the stdlib** (roadmap: `cryo version --verbose` waits for it
   too) - a stdlib-branch item, §6.

---

## 5b. Not proposed

- Making cryogate link the compiler library to read diagnostics or ASTs
  directly (§4a: the gate must survive the compiler it judges).
- Sampling a population, running the census warm under verify, or dropping a
  selfhost stage (the perf plan's list, for the same reasons).
- Keeping a Python gate "as a fallback" after its port is accepted: two gates
  for one question drift.

---

## 6. Stdlib APIs it needs (for the stdlib branch)

Read from the stdlib sources at `5d4a3c9a` (the API index misses some
declarations, e.g. `async` methods and some multi-line externs, so the
sources were grepped too). Each public name is Jake's to approve (test-infra
Decision 11 already approves a recursive walk and a child's peak memory, with
names to come). These go to the stdlib branch; cryogate does not grow private
copies of them (no one-off helpers).

**Already enough:** `Command` (args, env set and clear, cwd, `Stdio::Fd` to a
file, `Piped`), `Child` (`wait`, `try_wait`, `wait_timeout`, `kill`),
`ExitStatus`, `std::thread` (`spawn`, `Scope`, `available_parallelism`),
`Mutex`, `Atomic`, `CondVar`, `mpsc`, `File` (`read_text`, `read_bytes`,
`write_*`, `append_text`, `copy`), `Directory` (`create_all`, `entries`,
`remove_all`, which does not follow links), `Path`/`PathBuf`, `TempDirectory`,
`std::env` (args, vars, `current_exe`), `Instant`/`Duration`, `std::json`,
`Str` splitting and searching, `HashMap`/`HashSet`, `Array.sort_by`,
`Utf16::decode`.

Re-checked at `8828ddc3`: perf added none of these; the table stands.

**Broken, found by G1** (row 0 blocks every gate, so it comes first):

| # | defect | effect | needed by |
|---|---|---|---|
| 0 | **`Command.cwd` double-frees** (bug 0207): a by-value `match` on a field reached through `&` releases the payload, and `spawn_impl` matches `cmd.cwd` that way on both OS arms | on Windows any `Command` with a working directory corrupts the heap, at once when the spawn fails and within a few dozen spawns when it succeeds; the process dies with its buffered output | every gate (each runs `cryo` in a project directory); cryogate's self-test catches it by dying with no verdict line |

**Missing or too weak, most important first:**

| # | need | today | used by |
|---|---|---|---|
| 0a | **remove one inherited variable** (`Command.env_remove`) | only `env_clear`; the child runner rebuilds the environment from `env::vars()` less the removed names | verify (`CRYO_STDLIB`), any job that unsets |
| 0b | **a spawn error that names the cause**: the OS code and the program | Windows reports `operating system error` for a file that is not an executable, and `not found` without the path; on POSIX a missing program is the child's exit 127, the same as a program exiting 127 (the child runner checks a path-named program exists first, which covers the gates; a bare name still reads as 127) | every gate's "could not start" line |
| 1 | **sha256** of bytes and of a stream, and a hex encoder for a digest | no SHA-256 anywhere (sha1 and FNV only); no bytes-to-hex | verify-pin, pin, verify's object hashes, selfhost's tree digest; also `cryo version --verbose` on the roadmap |
| 2 | **kill a process tree**: a spawn option putting the child in its own process group (POSIX) / job object (Windows), and a kill of the whole group | `Child.kill` reaches the direct child only; `Command` passes creation flags 0; no job-object bindings | every gate: a timed-out `cryo test` leaves its test binaries and linkers running |
| 3 | **a child's peak memory** (POSIX `wait4` rusage, Windows process memory counters) on `ExitStatus` or `Child` | `sys_wait4` and `getrusage` exist raw; nothing surfaced; no Windows binding | the memory budget's measured costs (§4c) |
| 4 | **the host's free and total physical memory** | nothing (`sysconf` raw on Linux only; no `GlobalMemoryStatusEx`) | the memory budget |
| 5 | **Windows spawn correctness under concurrency**: wide-character `CreateProcessW` (non-ASCII paths and arguments), and only the child's own handles inherited (a handle list, or a spawn lock) | `CreateProcessA`, inherit-all: a sibling spawned at the same moment inherits another child's pipe end and delays its EOF | every concurrent gate on Windows (the runner's own capture-file notes show the same effect) |
| 6 | **wait for any of N children** | `try_wait` polling, a thread per child, or `select` over two futures | the scheduler (polling at a few ms works meanwhile; this is an efficiency item) |
| 7 | **interrupt handling**: run a cleanup when Ctrl-C arrives (kill the job tree, release the lock) | no portable handler (no `SetConsoleCtrlHandler`; POSIX only raw `sigaction`) | every long gate |
| 8 | **recursive directory walk** (pruning, sorted, no link following) | none (`entries` only) | verify, selfhost, roster, api-index |
| 9 | **copy a directory tree** (with excluded names) | none | verify's declared-program copies, incr fixtures, vendor |
| 10 | **file locking** (an exclusive lock on a file, with try and wait) | raw `fcntl` only | the cross-worktree lock, verify's caches |
| 11 | **a line diff** for a golden mismatch (first N differing lines with context) | none | examples goldens, selfhost's IR mismatch |
| 12 | small: `Str.lines()` (CR-tolerant), `CondVar.wait_timeout`, `Receiver.recv_timeout`, an ordered map, sub-second file times | absent | conveniences; each has a workaround with existing API |
| 13 | **take a running child's pipes**: `Child.stdout()` / `stderr()` (or a take) after `spawn` with `Stdio::Piped` | `Child::new` stores them, nothing hands them out: a piped child's output is reachable only through `Command.output()`, after it exits | the census streaming its run (followed from the log file meanwhile), the LSP driver (G6) |
| 14 | **join a list of strings** with a separator | absent; `core/text` holds cryogate's | every report that names several things |
| 15 | **a command-line option parser** (flags, valued options in both spellings, positionals, `--`) | absent; `core/options` holds cryogate's | every subcommand; the compiler's own CLI parses by hand too |
| 16 | **sorted and distinct**: `Array.dedup` after a sort, or an ordered set, and set difference on `HashSet` | absent; `Text::sorted_distinct` and the census's `without` hold cryogate's | the roster and census (every golden comparison), verify's moved lists |
| 17 | **a cursor over a `Str`**: consume a literal, a word, a whitespace-delimited token, up to a delimiter, skip whitespace | absent (`split_once` and `strip_prefix` cover one step each); `core/text`'s `Scanner` holds cryogate's | every fixed-shape line match a gate makes (the census's runner lines, the roster's directive and function lines) |

**For the lanes, later** (Decision 11 keeps these private to the tool,
through `extern "C"` declarations in `tools/CryoGate`, not public API):
shared memory for the fork server's counters (`mmap MAP_SHARED`, a named file
mapping on Windows); suspending and resuming a child or job (SIGSTOP / job
suspension) for the governor's pause; listing running processes by name (the
pause while `soloapps-train` runs); the foreground window's full-screen state
on Windows. Address-space and CPU limits use the existing `setrlimit`
binding on Linux and the job object of row 2 on Windows.

Not needed: a regex engine. Every Python regex in the gates is a fixed-shape
line match (`^test \(project\) (\S+) \.\.\.`), written as prefix, split and
trim. The one POSIX `regcomp` binding does not link on Windows anyway.

**The runner side** (a compiler change, §5): the compile-fail and project
suites have no per-child timeout and no kill, so a hung `cryo check` hangs the
census; and on POSIX a test binary killed by a signal is reported as "failed
to spawn" (`Command::shell` returns -1 for both). Both are bugs to file.

---

## 7. After the perf branch

The perf branch has ended: it was fast-forwarded into `stdlib-visibility`
(`353b4b2f`), and `cryogate` was fast-forwarded onto `origin/stdlib-visibility`
(`8828ddc3`), so cryogate builds with the post-perf pin. Every gate is in its
final form, and **every port takes the post-perf script as it stands**
(question 5). What perf changed, and what that means here:

| gate | what perf changed | consequence here |
|---|---|---|
| `selfhost-check.py` | both arms at once on Windows, the Linux arm from a digest-checked copy at `~/.cache/cryo-selfhost/<checkout>` on WSL's own filesystem; the stage stdlib rebuilds dropped; the stage archives compared by object (`STDLIB ARCHIVES OK` x2 beside `FIXED POINT OK` x2) | ported as landed (§4f), with the archive comparison reading each stage's own build; needs WSL started (`wsl.exe -e true`) |
| `verify.py` | the baseline recorded per program and reused while its keys hold; the archive-replaced column and the stdlib population; baselines cached in `.verify/` | ported as landed (§4e); a clean `--baseline` for a C-header change needs the header trick perf documented |
| `incremental-check.py`, `incremental-instance-check.py` | module objects keyed on their bitcode; every module analysed, so warnings are always shown; the runtime tiers rebuild only when an input is newer | G12 is no longer waiting: cryogate writes the edit-fuzz gate (question 3) |
| `test-census.py` | the census is warm-valid and ~4x faster; the script itself unchanged | portable now |
| the build | `make stdlib` is gone (the stdlib archive is prebuilt per compiler key under `stdlib/.bin/<triple>/prebuilt/`); `make cryo` is timestamp-gated, so after a branch switch `rm -rf compiler/build`; a build with warnings is never "up to date" | `make cryogate` depends on the runtime tiers, not on a stdlib target; gates that compare builds read the prebuilt archive's key |
| C headers | a header edit is not seen by the compiler's up-to-date check (bug 0203) | a gate that builds after a header change does so from a clean build directory |
| `examples-gate.py`, `lsp-gate.py`, `cross-check.py`, `vendor-consts-gate.py`, `roster-check.py`, `cryo-pin.py`, `verify-pin.py`, `api-index.py`, `annotate-negative-tests.py` | timings only | portable now |

**Clashes to avoid while `stdlib-visibility` moves:**

- The Makefile is the one file both edit. cryogate's Makefile changes are
  additive (the `cryogate` file rule, the `CRYOGATE` variable) until a port is
  accepted, and a target's recipe switches from `$(PYTHON) scripts/x.py` to
  `$(CRYOGATE) x` only in the commit that deletes `x.py`.
- No compiler or stdlib source changes on the cryogate branch except the
  folds in §5, each its own unit, each with the full object proof; the stdlib
  APIs and defects in §6 go to the stdlib branch, not here.
- **Following `stdlib-visibility`**: `cryogate` is rebased onto it when a unit
  needs something newer (question 7; Jake authorises that rebase, and only
  that one, as a force-push of `cryogate`). Nothing is merged by a worker.
- **Heavy runs are serialised across worktrees** (§4c lock), so a side-by-side
  proof here never runs while the stdlib worker is timing a gate.

---

## 8. The Python-gate milestone, unit by unit

These are the gate units of the whole programme (§11 places them among the
test-infrastructure units).

Session-sized units. Each lands as one commit: code, its `![test]`s, the
Makefile switch, the docs, and the deletion of the Python it replaces. The
"mutations" are the broken trees each port must refuse exactly as the old gate
does (§9, point 2).

| # | unit | depends on | proof beyond §9's standard |
|---|---|---|---|
| G1 | **Skeleton and core.** `tools/CryoGate` (cryoconfig; one executable, its `![test]`s under `tests/`), `make cryogate` file rule, `make clean` for its `build/` (`build/` and `build-logs/` were already ignored), the child runner, verdicts, logs, host probe, the scheduler and the heavy-run lock, `cryogate selftest` with the child-runner smoke. This file is committed here. No gate is switched | nothing | the child runner's own tests: exit codes, timeout kill, a stream captured to a file, env removal, cwd, a missing executable reported as "could not start", not as exit 1 |
| G2 | **roster** (+ `--merge`, `--update`, `--selftest` cases) | G1 | mutations: a project's `test.json` deleted (hard refusal), an assertion dropped from a `test.json` (fingerprint drift), a `//~` removed (annotation count), a unit test removed; `--update` on Windows keeping the Linux-gated entries |
| G3 | **census** (identical text parsing) | G2 (shares the golden reader and the OS waiver) | mutations: a marker deleted, a stray pattern in ARGS (refused), a collect project whose discovery is broken (ran 0 tests), the `cxx` probe's leaked line (verdict found off the line) |
| G4 | **examples** (build; `--run` with goldens; `--under=valgrind`), **vendor**, **incr --instance** | G1 | examples: one example broken, `--min` above the population; goldens: an `expected.out` edited, a golden with no listed example; vendor: a constant removed from the table, one added to the header; instance: the edit fixture reverted (nothing new to instantiate) |
| G5 | **verify-pin** and **pin** (incl. the WSL re-entry from Windows) | G1, sha256 (§6) | verify-pin's 12 self-test cases as `![test]`s; mutations: a sidecar byte, a second stamp marker, a HIGHLOW fixture; pin: a dirty tree recorded as dirty |
| G6 | **lsp** (driver + every checks.json + the stdin smoke from CI) | G1, child pipes (§6) | mutations: one fixture expectation edited, a handler that never answers (a request timeout named), `--min-checks` above the count; the check count equal to `smoke.py`'s |
| G7 | **cross** | G1 | mutation: a declaration that resolves only on this OS used from an other-OS gated path (the gate's own history); module floor |
| G8 | **check-fast** switched to cryogate; `make verify-selftest` retired into `cryogate selftest` | G2-G5 | `check-fast` refusing each judge case the Python self-tests refused |
| G9 | **verify** (concurrent gates, baseline clone and cache, declared programs, object compare, bounded caches, cross-worktree lock, memory budget) | G3, G4, G6, G7, G8 | verify's 36 self-test cases as `![test]`s; side by side over: an identical change (0 moved both), a codegen plant (same moved list both), a started-passing declaration (same judgement), a stale-cache mutation (an input the key must cover, changed); bug 0029's pair: old keeps N+1 baselines, new keeps N |
| G10 | **selfhost** (both arms, WSL mirror, wine arm on Linux), `.cmd` wrapper deleted | G1, G9 | perf2's pair: a syntax error in `compiler/src` reaches the mirror (both FAILED at the same stage), a skipped sync refused by the digest, a nondeterministic plant (stage 3 vs 4 differ) refused by both; exactly two `FIXED POINT OK` |
| G11 | **freestanding** (Windows arm native, Linux arm via WSL), **release-smoke** | G10's WSL arm | mutations: the panic tier's message changed, an exit code changed, a tier dropped from the link |
| G12 | **incr** as the edit-fuzz gate (question 3: cryogate writes it), `incremental-check.py` deleted | G1 (perf's content keys have landed) | the perf plan's mutation pair: `external_digest`'s layout term removed - the old gate OK, the new gate refusing the stale object |
| G13 | **folds**: the runner's report and counts, `stdout_file`, annotation writing; census and roster switch to the report; `annotate-negative-tests.py` and `check-examples-output.sh` deleted | Jake's answers (question 4) | full object proof for the compiler change (`--require-identical`, 0 moved); the census over the report agreeing with the census over the text on the same run |
| G14 | **api-index** fold; `api-index.py` deleted | question 4 | the new index contains every line the old one did, plus the one-line `implement` blocks (bug 0178's repro), and nothing else changed |
| G15 | **No Python**: `$(PYTHON)` removed from the Makefile, CI and release use no Python, `scripts/README.md`, CLAUDE.md, `docs/testing.md` and `make help` rewritten, this file deleted | everything | a fresh clone with no Python on PATH runs `make check-fast`, `make test-census`, `make verify` (control: the same clone at the parent commit fails at the first `$(PYTHON)` recipe) |

**Status.** G1's core is built (the child runner, the scheduler with CPU
slots and a declared-cost memory budget, the heavy-run lock and `cryogate
heavy`, run logs, `cryogate selftest` over `cryogate probe`, `make
cryogate`), with the option parser (`core/options`), the tree copy and file
listing (`core/tree`); the git helper is built with the first gate that calls
it, since a helper nothing calls is dead code. Its self-test is green on both
hosts' stdlib since the `Command` working-directory double free was fixed.
**Ported:** `roster` and `census` together (G2, G3: the census reads the
roster's `![target]` waiver, and `verify.py` reads the census's parse of a
baseline run from `cryogate census --read <log>`, one JSON record), `cross`
(G7), `incr --instance`, `vendor` and `examples` (G4's build sweep; its
`--run` goldens and `--under=valgrind` modes are still the shell scripts),
with `core/text` (lines, joining, a line scanner) beside the core.
`check-fast` runs `cryogate selftest` (so it builds cryogate), which carries
the roster merge's self-test cases.

**What can start now, beside stdlib work, without clashing:** G4-G12. They
touch only `tools/CryoGate`, new Makefile lines, a gate's row in `verify.py`'s
table, and the Python files they delete. G5 needs sha256 from the stdlib
branch, or waits. G6 needs the child-pipe API.

**What must wait:** G13-G14 (Jake's spellings, proposed in the session that
builds them).

---

## 9. How each port is proved

A port is accepted, and its Python file deleted in the same commit, only when
all of these hold. The figures go in the session's report and handoff, not the
commit message.

1. **Same tree, same verdict, same data.** The old and the new gate run over
   the same clean tree, one after the other (not concurrently, so neither
   competes for memory), and agree on the verdict and on every number and list
   the old one prints (counts, names, moved lists, digests). A difference is
   a finding: the port is wrong, or the old gate was, and which one is written
   down before anything changes.
2. **Same broken trees, same refusals.** Each mutation the gate's own history
   recorded is applied in a scratch copy, both gates run, both refuse, and the
   reasons name the same thing. Restored afterwards. The mutations per gate
   are listed with its unit in §8.
3. **Controls on the new instrument.** A gate that ran nothing refuses (an
   empty population, a suite whose output was deleted); a child killed by the
   timeout is a FAIL, not an ok; a log missing its verdict line is a FAIL.
4. **The self-test moves with it.** Every `--selftest` case of the old gate
   becomes a committed `![test]` in `tools/CryoGate/tests`, with the same
   accept and refuse expectations, run by `cryogate selftest` (and so by
   `check-fast`).
5. **Where the port is also an improvement** (bounded caches, a gate that
   newly runs on Windows), CLAUDE.md's pair applies on top: the old gate over
   a tree broken in the new way reporting OK, the new one refusing it.
6. **Under verify and alone, the same.** A gate run inside `cryogate verify`
   and run on its own give the same verdict and objects.

**A new lane** (there is no old gate to agree with) is proved the way the
test-infra plan proves each item: a control over a population already known
to pass (0 findings), at least one plant it must catch within its budget
(a revert of a historical fix preferred), and a mutation of the lane's own
oracle that makes it blind and is seen to. The plant is committed to
`tools/CryoGate/plants/` with the lane, so the plant gate keeps proving it.

Each pairing is written as a pair of lines in the report: *"`test-census.py`
printed `FAIL ... project deleted_marker not reached`; `cryogate census`
printed the same line"* is checkable; *"census ported"* is not.

---

## 10. The test infrastructure: every item and where it plugs in

Every item of `.todo/TEST_INFRA_V1_PLAN.md` keeps its design, payoff, risk
and proof as written there. This section says where each one lives in
cryogate, what the compiler (`cryo`) has to grow for it, and whether 1.0
needs it. **M** = must for 1.0, **S** = should, **L** = later, all as that
plan and Jake's decisions classify them. "Sessions" are that plan's sizes,
re-checked against this design (§11 says where they changed).

### 10a. The map

| item | what | lives in | cryogate entry | `cryo` grows | M/S/L | sessions |
|---|---|---|---|---|---|---:|
| 0 | overflow rule: debug traps, release wraps; `wrapping_*`; division panics | compiler, spec, stdlib | `cryogate sweep --compiler-profile=debug`: the verify population, the examples, the LSP gate and selfhost under a debug-profile compiler and debug test builds, every trap listed by site (the sweep that finds the intended wraps) | `overflow_checks` profile key, the overflow sink, `intrinsics::wrapping_*`, the constant-fold error | M | 3 |
| 1 | foundations: sandbox, store, budgets, `measure` | orchestrator `core/`, `store/` | `cryogate measure` (the per-lane cost table on this host), `cryogate repro <record>`, `cryogate store ls/prune` | - | M | 1 |
| 2 | per-phase harnesses + generic oracles | harness `phases/`, `oracle/` | the worker protocol; `cryogate harness-check` (every harness over the suite's sources: 0 findings, the control) | `stop_after: PassID` on `CompilerConfig` (library field, no CLI spelling needed) | M | 2 |
| 3 | syntax lane: token mutator, grammar generator, comment transparency | harness `mutate/` (tokens need the lexer), orchestrator `lanes/syntax` | `cryogate fuzz syntax` | - | M | 1 |
| 4 | coverage-guided loop, fork server; span mutators | harness `coverage/`, `mutate/`; orchestrator corpus scheduler | `cryogate fuzz coverage` | `--instrument=coverage` on `cryo build` | M (loop), S (span mutators) | 1 + 1 |
| 5a | ASan build of the compiler, arena audit mode | harness built `--sanitize=address -Darena_audit`, Linux | `cryogate fuzz <lane> --asan` | `--sanitize=address`; `-Darena_audit` in `stdlib/alloc/arena.cryo` | M | 1-2 |
| 5b | Cryo UB checks (`--checks=`), the shift rule | compiler | an axis of `diff/` | `--checks=` | L | 1-2 |
| 6-7 | CryoSmith: model, expressions to closures, drop log, per-profile overflow; async later | orchestrator `smith/` | `cryogate smith gen/run`, `cryogate fuzz smith` | - | M (core + moves/borrows/`?`/read-only pointers/closures), L (async) | 3 + 2 (+1 L) |
| 8 | reference interpreter before specialisation | harness `interp/` | `cryogate interp-check` (the gate and its golden), the `interp` axis of `diff/` | nothing public (Decision 16) | M (core), L (async) | 7 (+1 L) |
| 9 | refusal injection: rules × contexts × pointer levels | harness `inject/` (sites need types), orchestrator `lanes/inject` (the table run, the verdict compare) | `cryogate fuzz inject`, `cryogate inject-table` (the deterministic cross product) | - | M | 2 |
| 10 | differential axes: O0/O2, debug/release, Windows/Linux, compiled/expected/interpreted; last release, EMI | orchestrator `diff/` | `cryogate diff` | - | M (axes), S (last release, EMI) | 1 + 1 |
| 11 | incremental state fuzzing (on perf P8's edit-fuzz gate) | orchestrator `lanes/incr` | `cryogate incr` (fixed matrix: a verify gate; random edits: a farm lane) | `cryo cache dump` (perf P8) | M | 1 |
| 12 | LSP state fuzzing: hover against the compiler, edit sequences, resource drift | orchestrator `lsp/` (stdio) + harness `lsp/` (in process) | `cryogate fuzz lsp` | - | M (Decision 9) | 2 |
| 13 | performance fuzzing: blow-up search | orchestrator `lanes/perf` | `cryogate fuzz perf` | - | S | 1 |
| 14 | property framework + suites | stdlib `std::test::property` (Decision 10), suites in `tests/tests/stdlib/property` | `cryo test` runs them as unit tests; `cryogate fuzz property` reruns them under fresh `CRYO_PROPERTY_SEED`s in the farm and files a failing seed | the framework is stdlib | M (framework + collections), S (other suites) | 1 + 2 |
| 15-16 | triage: signatures, dedupe, reducer, filing; bisect, regression export | orchestrator `triage/` (the reducer's predicate re-runs a lane as a child; span-aware reduction asks the harness for spans) | `cryogate reduce`, `cryogate triage`, `cryogate file`, `cryogate bisect`, `cryogate export` | - | M (signatures, dedupe, reducer, filing), S (bisect, export) | 1 + 1 (shares a session with CryoSmith drops) |
| 17 | coverage reports of compiler code | orchestrator, from item 4's counters | `cryogate coverage-report` | - | S | 1 |
| 18 | smoke in verify, plant gate; farm + governor + nightly report | orchestrator `farm/`, `plants/` | `cryogate fuzz-smoke` (a verify gate), `cryogate plants`, `cryogate farm` | - | M | 1 + 1 |
| 19 | every Python gate replaced | orchestrator `gates/` | §3, §8 | the folds in §5 | milestone (Decision 12: should; question 9) | §11 |
| new | corpus management | orchestrator `store/` | `cryogate corpus`: minimise a lane's corpus to the inputs that keep its coverage, import seeds from the suite and the examples, export a curated set into `tools/CryoGate/seeds/`, prune the store by age and size | - | M | 1 |
| new | LSP protocol conformance | orchestrator `lsp/` | `cryogate lsp` (the ported smoke checks), then the protocol-error and cancellation checks grown into a table | - | M | in G6 |

### 10b. What "the bar of a mature language" adds, and where it is

The items above already cover the techniques mature compilers rely on:
grammar and token fuzzing, coverage guidance, sanitizer builds, a random
program generator with a known answer, a second implementation of the
semantics as an oracle, differential testing across optimisation levels and
platforms, property tests over the standard library, automatic reduction,
deduplication and bisection, and a corpus kept and minimised over time. Four
things a mature system also has are made explicit here, because without them
the rest decays:

1. **Every lane has a plant** (test-infra §4). `cryogate plants` applies each
   plant to an exported tip, runs the lane at its budget, and fails when the
   plant's signature does not appear. A lane with no plant has not been
   shown to see anything, and the nightly report puts a missed plant first.
2. **Every finding becomes a regression test.** `cryogate export` writes a
   reduced finding as a unit test, a negative file with `//~` lines, or a
   project with a `test.json`, and merges the roster. It is a should in the
   test-infra plan; this plan recommends it as a must (question 10), because
   a finding that is not pinned can come back unseen.
3. **Determinism is an oracle**, not an assumption: the same input twice
   gives the same diagnostics (item 2), the same compiler gives the same
   objects whether a gate runs alone or under verify (§9), and every record
   reproduces bit for bit from its seed (item 1).
4. **One runner, one store, one report.** Gates and lanes share the child
   runner, the sandbox, the budgets, the signatures and the log layout, so a
   finding from the farm and a red gate in verify are read the same way.

### 10c. What blocks verify, and what never does

- **Blocks verify:** every ported gate, and `cryogate fuzz-smoke` (fixed
  seeds and counts, so a red smoke is a real finding about the change; the
  test-infra plan's contents: 100 CryoSmith programs × O0/O2 against the
  model, 50 through the interpreter, the injection table over 20 programs,
  2000 syntax inputs, the three cheapest plants).
- **Never blocks anything:** the farm, the full plant gate, perf fuzzing,
  coverage reports. They file bugs (at most 5 a night, Decision 5).

---

## 11. The whole programme: order and sizing

Session-sized units, with no time estimates. The order respects four hard
constraints:

- **Identical-object proofs must not be confounded.** The overflow rule moves
  every debug object, so O1-O3 (on `test-infra`, question 11) land where no
  other branch is proving with `--require-identical` against the same base.
- **The fuzz lanes need perf's costs**, which have landed: a std-importing
  build links the prebuilt archive, and module objects are keyed on bitcode.
- **The tools encode the overflow rule,** so CryoSmith, the interpreter and
  the debug/release axis come after O1-O3.
- **The orchestrator's core comes first,** because every gate and every lane
  runs through it.

### 11a. The order

| phase | units | runs beside | sessions |
|---|---|---|---:|
| **A. Core and the gates perf is not touching** | G1 core; G2 roster; G3 census; G4 examples, vendor, instance; G5 pin, verify-pin; G6 lsp (2); G7 cross; G8 check-fast; G11 freestanding, release-smoke | perf and stdlib work, now | 10 |
| **B. Stdlib asks** (on the stdlib branch): sha256 + hex; process-tree kill + job objects + peak memory + host memory; wide-character, handle-listed Windows spawn; walk, tree copy, file lock, line diff, interrupt hook | the stdlib branch | 4 |
| **C. The big ports, then the overflow rule** | G9 verify (2); G10 selfhost (2); G12 incr = item 11; then O1-O3 overflow and the `cryogate sweep` runner they use (on `test-infra`) | - | 9 |
| **D. Foundations of the lanes** | item 1 (sandbox limits, store, measure); item 2 harnesses, first half; item 2's LSP harness + item 3 syntax; the worker protocol and the harness's build and restart; corpus management | - | 5 |
| **E. Generators and oracles** | item 4 coverage loop; item 5a ASan + arena audit (2); items 6-7 CryoSmith (5, with signatures and dedupe in the third); item 9 injection (2); item 16 reducer + filing; item 10 differential | - | 12 |
| **F. Interpreter** | item 8 (7) | - | 7 |
| **G. Always-on** | item 12 LSP fuzzing (2); item 18 smoke + plant gate; farm + governor + report; item 14 property framework + collections | - | 5 |
| **H. Python gone** | G13 runner report + `stdout_file` + annotations (2); G14 API index; G15 no Python | can run during E-G | 4 |
| **I. Should** | span mutators; bisect + export (recommended must, question 10); last-release axis + EMI; perf fuzzing; coverage reports; property suites (2) | - | 7 |
| **J. Later** | 5b UB checks (1-2); CryoSmith async; interpreter async | after 1.0 | 3-4 |

### 11b. Sizing, honestly

| part | sessions | test-infra plan's figure | why it moved |
|---|---:|---:|---|
| must, test infrastructure (O1-O3, items 1-12, 14 core, 15-16 core, 18) | **34** | 30 | +1 for corpus management made explicit; +1 for ASan at the upper end of its 1-2 (the arena audit is the uncertain half); +1 for the worker protocol and the harness's own build and restart, which the single-project layout did not need; +1 for the sweep runner the overflow units now get as a tool |
| the Python milestone (G1-G15, less G12, which is item 11) | **18** | 6 | the old figure counted one session per group of gates; read line by line, the LSP smoke alone is 1263 lines and verify 870, each port needs two full side-by-side runs plus one per recorded mutation, and three ports need compiler folds with an object proof |
| stdlib asks | **4** | not counted | sha256 and the process-tree, memory and spawn work are prerequisites the old plan assumed |
| should | **7** | about 5 | the same items (span mutators, bisect/export, last release + EMI, perf fuzzing, coverage reports, two property suites), with the two pairs the old plan put in one session each split, since each has its own plant and proof |
| **total for 1.0, must + milestone + asks** | **56** | 36 | |
| later | 3-4 | | |

**Where the uncertainty is,** in order: the interpreter (7 sessions, high
risk, and expected to surface spec questions for Jake); the overflow sweep
(the 106 grep lines are a floor, and every trap needs a ruling: an intended
wrap or a bug); the verify and selfhost ports (each proof run is long); the
LSP smoke port (many small protocol behaviours). If the Python milestone stays
a should (Decision 12), the 1.0 total is 38; it is shown inside the total here
because Jake asked for it as a milestone along the way, and question 9 asks
whether that makes it a must.

**Parallel work.** Phase A and phase B can run now, beside perf and stdlib
work, on this branch and the stdlib branch. Phases C-G are sequential in their
dependencies but not strictly in time: H can interleave with E-G, and within E
the CryoSmith and injection units can alternate with the coverage and ASan
units. Two workers on this programme at once is the useful maximum, and only
one of them may run heavy proofs at a time (§4c).

---

## 12. The end state

- Every must item of §10 is a `cryogate` subcommand with its plant in
  `tools/CryoGate/plants/`, the smoke blocks verify, the farm runs nightly on
  WSL and Windows under the governor, and every finding it files can be
  reproduced from its record, reduced, and exported as a committed test.
- `scripts/` holds no `.py` file. `$(PYTHON)` is gone from the Makefile, and
  neither CI workflow installs or calls Python.
- Every gate target in the Makefile is `$(CRYOGATE) <gate> $(ARGS)`, with the
  same target names and the same `ARGS` spellings, so nobody relearns a
  command and CLAUDE.md's command table stays true.
- `check-fast` builds exactly one thing, cryogate itself, and runs its
  self-test and `verify-pin`.
- The shell and `.cmd` scripts that remain are the ones that must run before
  any Cryo program can exist on the machine (installing LLVM, fetching the
  Windows LLVM libraries) and the release packaging that the installation
  branch owns; each is listed with that reason in `scripts/README.md`.
- CLAUDE.md, `scripts/README.md`, `docs/testing.md` and `make help` describe
  cryogate, and the "what a green gate does not tell you" list is re-checked
  entry by entry against the new runner: an entry the port fixed is removed in
  the commit that fixed it, one it did not is kept.
- `legacy/bootstrap/` keeps its Python, untouched; nothing runs it.
- This file is deleted in the commit that deletes the last Python gate.

---

## 13. Questions for Jake

Asked in the design report; recorded here with the recommendation.

**Jake's answers:** 1 A (both tiers in `tools/CryoGate`, no CryoFuzz);
2 A (built from the tree by the pin, the post-perf pin); 3 C (cryogate writes
the edit-fuzz gate); 4 A (each new spelling proposed by the session that
builds it and approved before it lands; the proposed spelling is used
meanwhile); 5 A (port the post-perf `verify.py`); 6 A (the heavy-run lock,
by hand until cryogate takes it); 7 A (rebase `cryogate` onto
`stdlib-visibility` when needed); 8 A (`check-fast` may build cryogate);
9 A (replacing every Python gate is a must for 1.0; a gate cryogate makes
unnecessary counts); 10 A (regression export is a must); 11 B (gate ports on
`cryogate`, lanes and the overflow work on a later `test-infra` branch);
12 A (gate ports, then overflow, then lanes). Question 13 was answered A by
the session dispatcher on Jake's behalf (roster and census together, verify
reading the census parse from cryogate); the new spellings (`cryogate incr
--instance`, `examples`, `vendor`, `cross`, `roster`, `census`, Makefile
`pin-impl`) are in use and await Jake's approval.

1. **The home** (supersedes test-infra Decisions 8 and 17). A:
   `tools/CryoGate` holds everything: the pin-built orchestrator `cryogate`
   and the harness `cryogate-harness` built by the compiler under test
   (§1a); no `tools/CryoFuzz` is created. B: as A, but with the harness tier
   kept as a separate `tools/CryoFuzz` that cryogate drives. C: the old
   layout, one CryoFuzz project with `cryogate` as its second binary (every
   gate then depends on the compiler library). D: as A, spelled
   `tools/cryogate`. **Recommend A.**
2. **How cryogate itself is built.** A: from the tree by the pin, rebuilt when
   its sources, the stdlib or the pin change. B: pinned as `bin/cryogate`
   beside the compiler pins, refreshed by `make pin` (immune to a
   misbehaving stdlib edit, but gate fixes need a repin). **Recommend A**,
   with the self-test first in every run.
3. **Perf's edit-fuzz gate** (P8 part 3, planned as `cryofuzz incr` in a
   CryoFuzz that perf creates). A: perf writes it as `cryogate incr` once G1
   (the core) is available to perf, and `incremental-check.py` retires into
   it; getting G1 onto perf is a cross-branch step Jake authorises. B: perf
   writes it as a standalone Cryo program and G12 moves it into cryogate. C:
   cryogate writes it (G12) after P8's keys land, and perf proves P8 with the
   existing gates plus its mutation pairs. **Recommend C**: it keeps the
   branches independent, and the fuzz gate is the test-infra plan's item 11
   anyway.
4. **The new spellings** (the report file's flag and format, each suite's
   summary line, `stdout_file`'s line-ending rule, the annotation flag, the
   API-index subcommand and whether it is public). A: proposed in the G13 /
   G14 session and approved before they land. B: named now. **Recommend A.**
5. **verify and the perf changes.** A: perf lands N3 and P17's verify
   changes in `verify.py` (Jake's "small Python edits now"), and G9 ports the
   result. B: perf waits for G9 and builds them in cryogate. **Recommend A.**
6. **Sharing the machine.** A: adopt the cross-worktree heavy-run lock and
   the memory rules (§4c) as the convention now; cryogate enforces them once
   G1 lands, and workers follow them by hand until then. B: no lock; the
   orchestrator schedules heavy runs. **Recommend A.**
7. **Following perf.** The branch is cut from `origin/perf` at `5d4a3c9a`.
   A: rebase `cryogate` onto `perf` when a unit needs a perf change (a
   force-push of `cryogate` only). B: merge `perf` into `cryogate`. C: stay
   on `5d4a3c9a` until perf is merged back. **Recommend A** while the branch
   is young; any of them is Jake's to authorise.
8. **`check-fast` builds one thing.** It "builds nothing" today; after G8 it
   builds cryogate with the pin. A: accept. B: keep `check-fast` on Python
   until G15. **Recommend A.**
9. **Is the Python milestone a must for 1.0?** Decision 12 made it a should
   that may slip. A: must (1.0 needs 56 sessions, §11b). B: should, as
   decided (1.0 then needs 38: the 34 must sessions and the 4 stdlib asks), with the units still
   interleaved as milestones. **Recommend B unless Python on the release
   path is a release blocker for Jake**; the release workflow is the one
   place where it matters to users, and G15 can be pulled forward for that
   alone.
10. **Regression export as a must.** The test-infra plan has `cryogate export`
    (a reduced finding written as a committed test) as a should. A: promote it
    to must; it is one session. B: leave it as should. **Recommend A**: a
    finding not pinned as a test can come back unseen.
11. **Which branch the test-infrastructure units land on.** A: all of it on
    `cryogate` (it is the home), with O1-O3 there too once perf's
    object-identical work is done. B: the gate units on `cryogate`, and the
    test-infrastructure units on a `test-infra` branch cut from it after perf
    merges, as the old plan's branch order says. **Recommend B**: the overflow
    rule moves every debug object, and keeping it off the branch that is
    replacing the gates keeps each branch's proofs readable.
12. **The order against the old plan's.** The old plan put the overflow rule
    first and the Python ports last. This plan runs the gate ports first (now,
    beside perf), then the overflow rule, then the lanes. A: accept. B: keep
    the old order (lanes before ports). **Recommend A**: the ports need nothing
    from perf or the overflow rule, and every lane later runs through the
    core they build.
13. **The census and roster scripts are libraries of `verify.py`** (§8,
    "The Python gates import each other"). A: G2 and G3 land together, and
    in the same commit `verify.py` reads the census parse and the pinned unit
    names from `cryogate census --read <log>` (one JSON record), a mode G9's
    verify keeps as a function call; a few lines of `verify.py` change from an
    import to a child process. B: G2, G3 and G9 land as one unit, so no
    Python changes at all, and both scripts live until verify is ported.
    C: switch `make roster-check` and `make test-census` to cryogate now and
    keep the two scripts only as `verify.py`'s libraries until G9 (two
    readings of the OS waiver meanwhile). **Recommend A**: one reading of the
    waiver from the moment the census moves, and the scripts go at once.
