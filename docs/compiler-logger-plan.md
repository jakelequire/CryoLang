# The compiler logger: plan

One internal logger replaces the compiler's scattered trace output:
`LOG_DEBUG`, `cdebug`, the `fflush(null)` calls beside them, the
`options.verbose` wrappers around them, and the debug `eprint`s.  Rulings
538-547 (`docs/resolution-rules.md`) are the decisions this plan builds;
this file is the order of the work and is deleted when the last slice lands.

## What is there today

Counted over `compiler/src` and `tools/CryoLSP/src`:

| mechanism | lines | switched on by | writes to |
|---|---:|---|---|
| `LOG_DEBUG(component, fmt, args...)` | 83 | nothing: its body is `return;` | nothing, but its arguments are evaluated |
| `cdebug(fmt, args...)` | 204 | `--debug` (an early argv scan in `Runner::run`) or `debug = true` in cryoconfig; all or nothing | stderr |
| `libc::fflush(null)` beside a trace | 37 | unconditional | flushes every C stream |
| `if (ctx.options.verbose)` around traces | 54 | `config.debug` again | - |
| `Logger`, `LoggerConfig`, `LogLevel`, `LogComponent` | - | `Logger::enable_debug()` sets a level nothing reads | dead, apart from `Logger::init` opening `g_stderr` |
| `CRYO_LAYOUT_AUDIT`, `CRYO_CTX_AUDIT` | 5 | environment | stderr |
| `CRYO_TIMINGS` | 4 | environment | stdout |
| `fmt::eprint*` | 165 | always | stderr: user-facing errors, warnings and reports, with a few traces among them |

The LSP has no logging of its own.  In LSP mode the compiler library writes
to stderr in two places (a frontend failure note, a mangler internal error);
both become log lines.

## Design

### Where it lives

`compiler/src/utils/log.cryo`, namespace `utils::log`.  The CLI and the LSP
both link the compiler library, so both have it; nothing in the standard
library changes.  It replaces `utils/logger.cryo`, which is deleted once its
last caller converts.

### Levels and areas

```cryo
type enum LogLevel {
    Off;
    Error;
    Warn;
    Info;
    Debug;
    Trace;
}

type enum LogArea {
    Driver;     // the build driver, the pass registry, CLI commands
    Loader;     // module discovery, the module graph, imports
    Lex;
    Parse;
    Resolve;    // name resolution
    Types;      // the type arena, type resolution, layouts
    Sema;       // body checking, directives, config gating, validation
    Mono;       // monomorphization
    Async;      // async lowering
    Drop;       // drop insertion and move checking
    Codegen;
    Link;
    Bindgen;    // C import and vendor bindings
    Deps;       // dependencies, lockfile, cache
    Diag;       // the diagnostic engine
    Lsp;
    Time;       // stage timings
}
```

Each area has a level; the whole filter is one `u64`, three bits per area
(17 areas, 51 bits).  An area set to a level prints that level and every
more severe one.

### The API

```cryo
type trait LogSink {
    write(mut &this, line: &String) -> void;
}

type struct StandardErrorSink { ... }   // the default
type struct FileSink { ... }            // appends, one write per line

type struct Log {
    ![inline]
    static enabled(area: LogArea, level: LogLevel) -> boolean;

    ![caller_location]
    static error(area: LogArea, message: String) -> void;
    // warn, info, debug, trace the same; each also overloaded on `string`,
    // so a literal message builds nothing until the line is printed.

    static configure(spec: string, origin: string) -> boolean;
    static set_sink(sink: virtual LogSink*) -> void;
    static configure_from_environment() -> void;
}
```

`enabled` compiles to a load, a shift, a mask and a compare of the area's
integer value (measured: the guard is inlined and no `match` survives).  Each
emitting method checks `enabled` first, so an unguarded call is correct and
only eager: its message is built whether or not the line prints.

### A call site

Cold code - once per module, pass or build - calls bare:

```cryo
Log::debug(LogArea::Drop, f"{inserted} drops inserted, {emitted} diagnostics emitted");
```

Hot code - per token, node, statement, expression, type, declaration, scope
or import - is guarded, so a disabled line builds nothing:

```cryo
if (Log::enabled(LogArea::Codegen, LogLevel::Trace)) {
    Log::trace(LogArea::Codegen, f"statement kind={stmt.kind.to_string()}");
}
```

### A line

```
[debug sema] compiler/src/compiler/sema/sema.cryo:4628 Sema::check_bodies: type-checking function bodies
```

Level, area, then the caller's place from `![caller_location]` (file and line
as the compiler's sources were spelled when it was built, and the qualified
function), then the message.  No colour, no timestamp: the same run prints
the same lines.  Each line is built whole and handed to the sink in one
write, so lines from codegen's worker threads do not interleave mid-line.

### Switching it on

- `CRYO_LOG=<spec>` in the environment, and `--log=<spec>` on the command
  line; the flag is applied after the environment, so it wins.
- `CRYO_LOG_FILE=<path>` and `--log-file=<path>` choose the file sink; the
  default sink is stderr.
- `--debug` is `--log=debug`.  `debug = true` in cryoconfig keeps its meaning
  and turns logging on the same way.
- Off by default: every area `Off`.
- The spec: `item (',' item)*`, where an item is `area` (that area at
  `debug`), `area=level`, or `level` (every area at that level); later items
  override earlier ones.  So `CRYO_LOG=sema,codegen`,
  `CRYO_LOG=info,codegen=trace`, `--log=debug,parse=off`.  An unknown area
  or level is reported once on stderr, naming the known ones, and the rest
  of the spec still applies.

The CLI reads both in `Runner::run`, before any command runs.  The LSP takes
no flags, so it reads the environment alone, in its `main`.  In the LSP,
nothing is logged unless `CRYO_LOG` is set, so the editor's stderr channel
sees no log output by default.

### What stays out

User-facing output stays `eprint`: cryoconfig, dependency and usage errors,
the bindgen translation report, `--emit-llvm`'s path line.  A trace that is
really an internal error a user should see (codegen's `CORRUPT` line, the
cryoconfig-not-found warning, bindgen's header-not-found warning) is listed
in its slice's commit and left as it is, since changing it changes what a
user sees; each is a question for the review.  `CRYO_TYPE_AUDIT` is a check
that fails a build, not a log, and stays.

## Slices

Each slice is one commit with its tests and docs.  verify's objects are the
tests' and examples', which no logging change reaches: every slice predicts
0 moved objects there.  The compiler's own objects move in every slice;
`selfhost-check` is their gate.  The cost gate is `cryo check
src/main.cryo` on the compiler, five runs each side, medians compared: with
logging off a slice must not be slower, and slices 2 and 3 are predicted to
be faster (they delete real calls, `to_string()`s and `fflush(null)`s on hot
paths).

1. **Core and the driver.**  `utils/log.cryo` (levels, areas, `Log`, the
   sink trait, the two sinks, the spec parser); `CRYO_LOG`, `CRYO_LOG_FILE`,
   `--log`, `--log-file`, `--debug` wired in `Runner::run` and the LSP's
   `main`; the driver's and the CLI's traces converted (`instance.cryo`,
   `CLI/`).  A new gate, `scripts/log-gate.py` (`make log-check`, verify
   gate `log`), builds `tests/fixtures/log` and asserts: nothing logged by
   default; `CRYO_LOG=driver` prints driver lines, each with a place, and no
   other area; the flag wins over the variable; `--debug` turns every area
   on; the file sink writes the file and leaves stderr alone; an unknown
   area is reported.  Its mutation pair: a tree with the area filter broken
   passes the old verify and fails the new one.
2. **`LOG_DEBUG`.**  Every call converts to a guarded `trace` or is deleted
   where it repeats a diagnostic the compiler already emits (the lexer's
   malformed-literal lines).  `LOG_DEBUG` and `LogComponent` are deleted.
3. **Codegen.**  The per-function, block, statement and expression traces
   are guarded; every `fflush(null)` beside a trace is deleted.
4. **The rest of `cdebug`.**  Loader, sema and types, mono, drop and move
   checking, bindgen, deps, diag; the `options.verbose` wrappers go as their
   contents convert, and `options.verbose` when its last reader does.
   `cdebug`, `g_compiler_debug`, `Logger` and `utils/logger.cryo` are
   deleted.
5. **The LSP and the audits.**  The two compiler-library `eprintln`s that
   reach the LSP become log lines; an LSP gate check asserts a broken
   buffer leaves stderr empty.  `CRYO_LAYOUT_AUDIT` and `CRYO_CTX_AUDIT`
   become `codegen` and `driver` lines; `CRYO_TIMINGS` becomes the `time`
   area, on stderr.  The roadmap entry is removed and `docs/cryo.md`
   documents the variables and flags.
