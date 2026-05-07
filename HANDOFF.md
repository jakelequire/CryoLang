# Handoff — finish NewCryoLSP (Phase B + C)

**Mission**: complete the in-process Cryo language server. Phase A (lifecycle
+ initialize/shutdown) is done and verified end-to-end. You are picking up
**Phase B** (text sync + diagnostic publishing) and **Phase C** (hover, go-to-
definition, completion). The plan that produced Phase A still applies; this
handoff captures what's in place, the Cryo idioms that matter, and a
file-by-file map for the rest of the work.

**Branch**: `new-stdlib`. Nothing has been committed in this session — the
user handles commits. Surface the diff at the end of your turn so they can
commit at their own cadence.

---

## What works today

```bash
cd /workspaces/CryoLang/tools/NewCryoLSP
/workspaces/CryoLang/compiler/build/bin/cryo build --stdlib=/workspaces/CryoLang/stdlib
# → build/bin/cryolsp                                                           (~0.4s)
```

End-to-end smoke test (proves framing + JSON-RPC + lifecycle):

```bash
{
  printf 'Content-Length: 116\r\n\r\n{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":null,"rootUri":"file:///tmp","capabilities":{}}}'
  printf 'Content-Length: 58\r\n\r\n{"jsonrpc":"2.0","id":2,"method":"shutdown","params":null}'
  printf 'Content-Length: 47\r\n\r\n{"jsonrpc":"2.0","method":"exit","params":null}'
} | /workspaces/CryoLang/tools/NewCryoLSP/build/bin/cryolsp
```

Expected output: an `initialize` response with capabilities, a `shutdown`
response with `null`, then a clean exit (code 0). Verified working before
the handoff.

---

## Repo state

**Branch**: `new-stdlib`
**Pinned bootstrap binary**: `/workspaces/CryoLang/bin/cryo` — older self-host,
suitable as the bootstrap for rebuilding the compiler.
**Latest self-built compiler**: `/workspaces/CryoLang/compiler/build/bin/cryo`.
Use this to build the LSP and stdlib.

**Verification cadence** (run after any non-trivial change):

```bash
# Compiler self-build (~30s)
cd /workspaces/CryoLang/compiler && rm -f build/libcompiler.a && \
  /workspaces/CryoLang/compiler/build/bin/cryo build

# Stdlib (~3s)
cd /workspaces/CryoLang/stdlib && rm -rf .bin && \
  /workspaces/CryoLang/compiler/build/bin/cryo build

# LSP (Phase A: ~0.4s; should grow as B/C land)
cd /workspaces/CryoLang/tools/NewCryoLSP && \
  /workspaces/CryoLang/compiler/build/bin/cryo build --stdlib=/workspaces/CryoLang/stdlib
```

All three must continue to pass. If a Phase B/C change touches the compiler
or stdlib, run all three; otherwise just the LSP build is enough.

---

## What's already in the tree

```
tools/NewCryoLSP/
├── cryoconfig                      # [dependencies] compiler = { path = "../../compiler" }
└── src/
    ├── _module.cryo                # imports the three sibling namespaces
    ├── main.cryo                   # entry: instantiate Server, run, drop, exit code
    ├── protocol/                   # WIRE LAYER (Phase A — done)
    │   ├── _module.cryo
    │   ├── framing.cryo            #   Content-Length frame read/write
    │   ├── jsonrpc.cryo            #   IncomingMessage / ResponseMessage / clone_value
    │   ├── lsp.cryo                #   Position / Range / Diagnostic / Hover / CompletionItem
    │   └── conv.cryo               #   STUB — Phase B: compiler-Diag → LSP wire
    ├── server/
    │   ├── _module.cryo
    │   ├── state.cryo              # ServerState (initialized, shutdown_requested, root_uri)  — done
    │   ├── server.cryo             # event loop + dispatch + lifecycle gate                   — done
    │   ├── line_index.cryo         # STUB — Phase B
    │   ├── docs.cryo               # STUB — Phase B
    │   └── session.cryo            # STUB — Phase B (per-doc CompilationContext)
    └── handlers/
        ├── _module.cryo
        ├── lifecycle.cryo          # initialize + shutdown                                    — done
        ├── text_sync.cryo          # STUB — Phase B
        ├── diagnostics.cryo        # STUB — Phase B
        ├── hover.cryo              # STUB — Phase C
        ├── definition.cryo         # STUB — Phase C
        └── completion.cryo         # STUB — Phase C
```

The stubs are real `.cryo` files with the right namespace declaration and a
single import — just enough to keep the loader's transitive walk happy.
Replace each with the implementation when its phase comes up.

---

## Stdlib helpers added in this session — USE THEM

The previous draft of the LSP shipped its own custom `push_str_lit(out, lit,
hand_counted_len)` and a per-codebase `s(lit)` wrapper. The user pushed back
on that as hacky, and the same ugliness existed in 31 places in the stdlib
itself (`Str::from_raw("..." as u8*, 23)`). So three primitives went into
the stdlib in this session:

```cryo
// stdlib/collections/str.cryo
Str::from_cstr(text: string) -> Str
//  Wrap a NUL-terminated `string` literal as a Str via strlen.
//  Use this everywhere you'd otherwise write Str::from_raw(lit as u8*, N).

// stdlib/collections/string.cryo
String.push_cstr(mut &this, text: string) -> void
String.try_push_cstr(mut &this, text: string) -> Result<(), AllocError>
//  Append a C-style literal to a String — no manual length.

// stdlib/fmt/integer.cryo
fmt::integer::push_u64_decimal(out: mut &String, value: u64) -> void
fmt::integer::push_i64_decimal(out: mut &String, value: i64) -> void
//  Append a base-10 number to a String.  Replaces the
//  scratch-buffer + push_str(Str::from_raw(...)) dance.
```

**Use these.** Don't reintroduce a per-LSP `Strings::s()` helper. If you find
yourself writing `Str::from_raw("..." as u8*, hand_counted_number)`, swap it
for `Str::from_cstr("...")`. If you're appending a literal to a `String`,
use `s.push_cstr("...")` not `s.push_str(Str::from_cstr("..."))`.

If you spot yet another shared primitive that the LSP needs, **add it to
the stdlib** rather than to a per-tool utility module. The user's preference
is explicit on this.

---

## Compiler changes already in this branch

These are **already applied** — don't redo them.

1. **Parser progress guards** (`compiler/src/compiler/parser/parser.cryo`,
   `expr_parser.cryo`) — the LSP-build OOM I was originally chasing turned
   out to be a parser hang on `&mut x` expression syntax. Five error-recovery
   loops now save `pos` at the top and force `advance()` if no progress was
   made. Notes in
   `~/.claude/projects/-workspaces-CryoLang/memory/feedback_parser_progress_guards.md`
   and `project_lsp_oom_was_parser_hang.md`.

2. **Multi-level enum patterns** (`expr_parser.cryo:parse_enum_pattern_internal`)
   — the parser used to accept only `Enum::Variant(b)`. It now walks any
   number of `::` segments so qualified paths like
   `Protocol::JsonRpc::IncomingMessage::Request(req)` parse. The full path
   (everything but the final segment) is interned as the enum name; the
   final segment becomes the variant.

If a Phase B/C change tempts you to touch the parser, **don't unless you
have to.** The compiler self-build is the ground truth — break it and
everything stops working.

---

## Cryo idioms that matter for this codebase

These are repeated paper cuts from earlier in the session. Save yourself
the rebuild loops.

### Reference call-site syntax

Cryo has no `&mut x` expression syntax. Mutability lives on the *parameter*
declaration (`mut &T`); the call site just uses `&value`:

```cryo
function frob(out: mut &String) -> void { out.push_byte(10); }

mut s: String = String::new();
frob(&s);                      // ✓ — `&s` is a reference; the `mut &` in the
                               //     param signature decides mutability
// frob(&mut s);               // ✗ — does not parse, will hang the parser
                               //     on certain surrounding contexts
```

Likewise inside `match`:

```cryo
match (this) { ... }           // ✓ — match against `this` itself
// match (mut &this) { ... }   // ✗ — `mut &x` is parameter-decl syntax only
```

### Variables must declare their type

```cryo
const x: int = 10;             // ✓
// const x = 10;               // ✗ — no type inference on bindings
```

### Trait keyword is `This`, not `Self`

When implementing trait methods, the implementing type is `This`. There is
no `Self` keyword. (See
`~/.claude/projects/-workspaces-CryoLang/memory/feedback_this_not_self_keyword.md`.)

### No callback-style helpers

Function pointers as parameters fail codegen. Don't write a helper that
takes `cb: fn(...) -> ...` — keep match arms / dispatch inline. (See
`feedback_no_function_pointers.md`.)

### Single-element array literal `[x]` is broken

```cryo
mut arr: T[] = [x];            // ✗ — silently miscompiles, reads stack garbage
mut arr: T[] = [];             // ✓ — empty literal works
arr.push(x);                   //     append after construction
```

### Imports

Three foot-guns I tripped on while writing Phase A:

- Generic types need their module imported. `Array<JsonValue>::with_capacity`
  fails with "cannot find Array::with_capacity" without
  `import std::collections::array;`.
- Same for `String`, `Str`, `HashMap`. The frontend doesn't pre-resolve
  generics from a downstream import.
- The LSP's `_module.cryo` files use `public module X;` declarations. The
  loader walks them as siblings; you don't need to put them in a
  `[compiler] include_paths` list as long as they're under `src/`.

### `mut &T` parameters: pass-through vs explicit `&`

```cryo
function inner(out: mut &String) { /* ... */ }
function outer(out: mut &String) {
    inner(out);                // ✓ pass-through — out is already `mut &String`
}
function caller() {
    mut s: String = String::new();
    inner(&s);                 // ✓ value → ref
    // inner(s);               // ✗ value where ref expected
}
```

---

## Phase B — text sync + diagnostics

Goal: editor squiggles work. didOpen / didChange / didClose round-trip
through the compiler; diagnostics get published as a notification.

Order of work (each step builds and ships a smaller smoke test):

### B.1 `server/line_index.cryo`

Build a `LineIndex` that converts byte offsets ↔ (line, character).

Rationale: the compiler emits `SourceSpan { start_line: u32, start_col: u32,
end_line: u32, end_col: u32 }` (1-based). LSP wants `Position { line, character }`
(0-based, UTF-16 code units). The naïve mapping (subtract 1) is fine for
ASCII; we'll do a real UTF-16 conversion only if it matters in practice.

Suggested API:
```cryo
type struct LineIndex {
    /// Byte offset of the start of each line (line N starts at line_starts[N]).
    line_starts: Array<u64>;
}

LineIndex::build(text: Str) -> LineIndex
LineIndex.line_count(&this) -> u64
LineIndex.byte_offset(&this, line: u32, character: u32) -> u64
LineIndex.position_of(&this, byte_offset: u64) -> Position
```

For Phase B, do byte-character not UTF-16 — leave a TODO comment. UTF-16
matters only for multi-codepoint scalars in non-ASCII source, which is rare
in Cryo source files today.

### B.2 `server/docs.cryo`

In-memory store mapping URI → open document. Each entry holds the text,
the version, and the LineIndex.

```cryo
type struct Document {
    uri:        String;
    version:    i32;
    text:       String;
    line_index: LineIndex;

    static new(uri: String, text: String, version: i32) -> Document {
        const idx: LineIndex = LineIndex::build(text.as_str());
        return Document { uri: uri, text: text, version: version, line_index: idx };
    }

    drop(mut &this) -> void {
        this.uri.drop();
        this.text.drop();
        this.line_index.drop();
    }
}

type struct Docs {
    by_uri: HashMap<String, Document>;
    static new() -> Docs { ... }
    insert(mut &this, doc: Document) -> void
    get(&this, uri: Str) -> Option<Document*>          // see HashMap caveat below
    remove(mut &this, uri: Str) -> Option<Document>
}
```

**HashMap caveat**: `HashMap.get(&K) -> Option<V>` returns the value **by
value**, not a borrow. For `Document` (which owns heap data) that's a
problem — every lookup would clone. Two options:

1. Use the existing `HashMap.get_bucket(idx) -> Entry<K, V>*` and walk
   yourself for write paths; lookups for read-only handlers can clone the
   `String` text and ignore the line_index (rebuild on demand).
2. Add a `HashMap.get_ptr(&K) -> Option<V*>` to the stdlib that returns a
   borrow into the bucket's slot. **Preferred** — this is exactly the kind
   of primitive the stdlib should grow once a real consumer needs it. Match
   the user's "add it to the stdlib, not a per-tool helper" rule.

Verify HashMap's storage stability before doing option (2): the bucket
pointer must remain valid until the next `insert` that triggers a resize.
Look at `stdlib/collections/hashmap.cryo`'s resize logic.

### B.3 `server/session.cryo`

A `CompilerSession` that keeps a long-lived `CompilationContext` for the
LSP-mode pipeline. The relevant compiler entry point:

```cryo
// compiler/src/compiler/instance.cryo:459
compile_for_lsp_content_into(&this, ctx: CompilationContext*,
                             virtual_path: string, content: string) -> CompilationResult
```

This sets `ctx.artifacts.tokens = TokenStream::new([], virtual_path, content)`,
runs the frontend pipeline, and leaves the AST / resolver / type arena
populated on `ctx`. Subsequent hover/definition handlers walk those.

Suggested shape:
```cryo
type struct CompilerSession {
    ctx_box:  Box<CompilationContext>;       // heap-alloc; the struct is large
    instance: CompilerInstance;              // CompilerConfig::default() is fine
}

CompilerSession::new() -> CompilerSession
CompilerSession.recompile(mut &this, virtual_path: Str, content: Str) -> CompilationResult
CompilerSession.context(&this) -> CompilationContext*    // for handlers to walk
```

**Important — between-recompile state**: the compiler had a leak in this
exact area earlier in the project (`g_shared_type_cache` accumulated stale
TypeRef → LLVMTypeRef entries across recompiles). It's now reset at the
top of `compile_project_with_config`, but `compile_for_lsp_content_into`
*also* needs the reset if you reuse one ctx across many recompiles. Verify:
search `compile_for_lsp_content_into` for `reset_shared_type_cache` — if it's
not there, add it. (See
`~/.claude/projects/-workspaces-CryoLang/memory/feedback_compiler_state_leak_between_compiles.md`.)

### B.4 `handlers/text_sync.cryo`

Three notification handlers — all return void, all dispatch to `Docs` and
`CompilerSession` then publish diagnostics.

```cryo
public function did_open(docs: mut &Docs, sessions: mut &SessionMap,
                          writer: mut &Stdout, params: &JsonValue) -> void
public function did_change(docs: mut &Docs, sessions: mut &SessionMap,
                            writer: mut &Stdout, params: &JsonValue) -> void
public function did_close(docs: mut &Docs, sessions: mut &SessionMap,
                           writer: mut &Stdout, params: &JsonValue) -> void
```

`textDocumentSync` is advertised as `2 = Incremental`. For didChange you'll
get a `contentChanges: ContentChange[]` where each change is either:
- `{ range: Range, text: string }` — replace the byte range
- `{ text: string }` — replace the whole document

Phase B can punt on the incremental form: treat every didChange as
"contentChanges has length 1 and the only entry is whole-doc replacement"
and emit a TODO. Most editors send the whole text by default if you set
`textDocumentSync: 1`. If you want to be honest, switch the advertised
value in `lifecycle.cryo:build_initialize_result` to `1` (Full) for now —
that simplifies didChange to "blow away the doc, store the new text".

### B.5 `handlers/diagnostics.cryo` + `protocol/conv.cryo`

After every successful `recompile`, walk `ctx.diagnostics.diagnostics` and
build the LSP `publishDiagnostics` notification.

`Compiler::Diag::Diagnostic` shape:
```cryo
type struct Diagnostic {
    severity:    Severity;     // Error / Warning / Note / Help
    code:        ErrorCode;
    message:     string;
    span:        SourceSpan;   // 1-based line/col, both ends inclusive
    labels:      SpanLabel[];
    children:    Diagnostic[];
    suggestions: Suggestion[];
}
```

Conversion:
- `severity`: Error → 1, Warning → 2, Note/Help → 3 (Information). Hint
  (4) is unused by Cryo today.
- `code`: format the `ErrorCode` (e.g. `"E0102"`).
- `source`: hard-code `"cryoc"` so users can filter by source in their
  editor.
- `message`: pass through.
- `range`: `start_line - 1`, `start_col - 1`, `end_line - 1`, `end_col - 1`.
  Bounds-check (the compiler emits `0,0,0,0` for unspanned diagnostics —
  use a safe fallback like `Range { start: 0/0, end: 0/0 }`).

Notification body (LSP spec):
```json
{
  "method": "textDocument/publishDiagnostics",
  "params": { "uri": "<uri>", "diagnostics": [...] }
}
```

Use `JsonRpc::encode_notification(method: Str, params: JsonValue) -> String`
(already exists in jsonrpc.cryo) and write through `Framing::write_message`.

### B.6 wire it up in `server/server.cryo`

The `dispatch_notification` arm currently routes only `exit`. Add:

```cryo
if (method.equals(&Str::from_cstr("textDocument/didOpen"))) {
    TextSync::did_open(&this.docs, &this.sessions, &this.writer, &notif.params);
    return;
}
// ...didChange, didClose...
```

`Server` grows two new fields: `docs: Docs` and `sessions: SessionMap` (a
`HashMap<String, CompilerSession>` keyed by URI).

### B.7 Phase B verification

A real editor is the right test, but the smoke test from Phase A extends:

```bash
{
  printf 'Content-Length: 116\r\n\r\n{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":null,"rootUri":"file:///tmp","capabilities":{}}}'
  # didOpen with broken source — expect publishDiagnostics back
  BODY='{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/x.cryo","languageId":"cryo","version":1,"text":"namespace T;\nfunction f() -> i32 { return ohno; }\n"}}}'
  printf 'Content-Length: %d\r\n\r\n%s' "${#BODY}" "$BODY"
  printf 'Content-Length: 47\r\n\r\n{"jsonrpc":"2.0","method":"exit","params":null}'
} | /workspaces/CryoLang/tools/NewCryoLSP/build/bin/cryolsp
```

Expect: an `initialize` response, then a `publishDiagnostics` notification
flagging `ohno` as undefined, then clean exit.

---

## Phase C — hover, definition, completion

Phase B leaves you with a populated `CompilationContext` per open URI. Phase
C handlers walk that context to answer position-based queries.

### C.1 Position → AST node

The compiler doesn't ship a "find AST node at position" helper today. You
need one — it's the substrate for all three handlers. Suggested location:
**inside the compiler library**, not the LSP. Two reasons:

1. The walk needs to know about every AST node kind. Putting it in the
   compiler keeps it next to the AST definition where additions to the
   node enum will be noticed at review time.
2. Other tools (formatter, doc generator) will want it eventually.

Put it at `compiler/src/compiler/AST/position.cryo`:

```cryo
namespace Compiler::AST::Position;

/// Find the innermost AST node whose span contains (line, character).
/// Returns null if no node covers the position.
public function find_node_at(root: ProgramNode*, file: string,
                              line: u32, character: u32) -> ASTNode*
```

Implementation: pre-order walk; descend only into children whose span
covers the cursor. Track innermost match. The `SourceSpan` struct fields
are `file: string, start_line/col: u32, end_line/col: u32` — straightforward
range check. Watch for the 1-based-vs-0-based mismatch (compiler is 1-based,
LSP is 0-based — convert at the boundary).

### C.2 Hover

Once you have the node:

- If it's an `IdentifierNode` or `MemberAccessNode` whose `resolved_type`
  is valid, render the type. The arena's display formatter is
  `arena.resolve_display_name(type_ref.id)` — pulls a human-readable name
  out (e.g. `i32`, `Option<String>`, `HashMap<String, Document>`).
- Wrap the result in a fenced markdown code block:
  ```
  ```cryo
  <type_name>
  ```
  ```
  Wrap the markdown in `MarkupContent::markdown(value)` (already exists in
  `protocol/lsp.cryo`).
- Set `hover.range` to the node's span so VS Code highlights the matched
  token.

Caveats:

- `resolved_type` is set during sema. If sema didn't run (because earlier
  passes errored), the type will be invalid. Bail to "no hover info" rather
  than emit garbage.
- See `~/.claude/projects/-workspaces-CryoLang/memory/feedback_no_bare_name_lookups_for_cryo_types.md`
  — use `node.resolved_type`, not name-based lookups, for Cryo types.

### C.3 Go-to-definition

For an `IdentifierNode` resolved to a symbol, the resolver records the
declaring span:

```cryo
// compiler/src/compiler/resolver/resolver.cryo
record_resolution(span: SourceSpan, sym_id: SymbolID)
resolve(span: SourceSpan) -> SymbolID
get_symbol(sym_id) -> Symbol*
// Symbol carries:
//   declaring_span: SourceSpan
//   source_module:  string
```

So: identifier-at-position → `resolver.resolve(node.span) → SymbolID` →
`resolver.get_symbol(sym_id).declaring_span` → LSP `Location { uri, range }`.

The URI is `file://` + `declaring_span.file`. Compiler spans use absolute
paths after module discovery, so this should be a direct conversion (with
URL-escape if you have spaces — for the v1 don't bother, error if the path
has any).

### C.4 Completion

Two trigger contexts (both already advertised in `lifecycle.cryo`):

- `.` — member access on the preceding expression. Walk the AST to find
  the receiver, look up its type, enumerate fields + methods.
- `:` — only fires on `::` (scope resolution). Look up the namespace name
  to the left, list its public members.

For v1, **a generic "every in-scope symbol" completion is acceptable** when
you can't determine the trigger context. The trigger character arrives in
the request's `context.triggerCharacter` field — read it, dispatch to one
of three sub-handlers (member, scope, generic), each walking the resolver's
scope chain or the receiver type.

Stdlib `JsonValue::Array` is the result type — push `CompletionItem` JSON
into it.

### C.5 Verification

By Phase C the smoke test has graduated from "useful for a regression
shim" to "needs a real client". Hook the LSP into VS Code via a workspace
extension or hand-craft a request-response sequence:

```bash
# After didOpen with a known file, send hover at line/col of an identifier:
BODY='{"jsonrpc":"2.0","id":3,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/x.cryo"},"position":{"line":1,"character":24}}}'
```

Expect a response with a `MarkupContent` whose `value` contains the
identifier's type rendered as a fenced code block.

---

## Where to find things

- **LSP entry point on the compiler**: `compiler/src/compiler/instance.cryo:459`
  — `compile_for_lsp_content_into(&this, ctx, virtual_path, content)`.
- **CompilationContext public fields**: `compiler/src/compiler/compilation_context.cryo`
  lines 43–110. The handlers care about `artifacts.ast.root` (the AST),
  `resolver`, `type_arena`, `decl_index`, `diagnostics`.
- **Diagnostic struct**: `compiler/src/compiler/diag/diagnostic.cryo:16`.
- **Source span**: `compiler/src/compiler/diag/source_span.cryo`.
- **Symbol / Resolver**: `compiler/src/compiler/resolver/symbol.cryo:106`,
  `resolver.cryo` (lookup, resolve, record_resolution, get_symbol).
- **TypeArena display formatter**: `compiler/src/compiler/types/arena.cryo`
  — search for `resolve_display_name` or `format_display`.
- **stdlib JSON**: `stdlib/json/{value,parser,serializer,error}.cryo`.
  `parser::parse(text: Str) -> Result<JsonValue, JsonError>` and
  `serializer::stringify(v: &JsonValue) -> String` are the entry points.
- **stdlib io**: `stdlib/io/stdio.cryo` — `stdin()`, `stdout()`, `stderr()`.
  Read/Write traits on `Stdin` / `Stdout` come from
  `stdlib/io/traits.cryo`.

---

## Memory files of interest (auto-loaded; you'll see them)

`~/.claude/projects/-workspaces-CryoLang/memory/`:

- `feedback_parser_progress_guards.md` — why the recovery loops have
  `if (this.pos == before_pos) { this.advance(); }`.
- `project_lsp_oom_was_parser_hang.md` — story of how Phase A's foundation
  got laid (the prior agent's `&mut x` syntax tripped a parser hang, fixed
  with progress guards before this rewrite).
- `feedback_compiler_state_leak_between_compiles.md` — relevant if you
  reuse one ctx across recompiles in `session.cryo`.
- `feedback_user_handles_commits.md` — make changes, surface them, do not
  `git commit`.
- `feedback_codegen_architecture_rules.md`,
  `feedback_no_workarounds.md` — general project rules.
- `feedback_no_bare_name_lookups_for_cryo_types.md` — use `node.resolved_type`
  not name lookups when answering hover.

---

## What NOT to do

- **Don't reintroduce a per-LSP utility module** for string-handling. If
  you find a missing primitive, add it to the stdlib. The user is explicit
  on this and the path is well-trodden now (see the three helpers added
  in this session).
- **Don't add `&mut x` expression syntax to the parser** to make the LSP
  source easier. The whole point of the previous round was to use `&x`
  with mutability declared on the parameter.
- **Don't extend `synchronize()` to advance past statement-start tokens.**
  Other callers rely on the current behaviour. If you find another
  loop that loops without progress, add a progress guard there (the
  pattern is in `parser.cryo` and `expr_parser.cryo`).
- **Don't commit.** The user handles all commits. Surface the diff with
  `git diff --stat`.
- **Don't rewrite framing/jsonrpc/lifecycle.** Phase A is verified end-to-
  end. Any change there is a regression risk.

---

## Scratch list — small things that came up but were out of scope

These are nice-to-haves you might pick off if you're between phases:

1. The 31 stdlib places that still use `Str::from_raw("..." as u8*, hand_count)`
   — a one-pass tidy with `Str::from_cstr("...")` would shrink stdlib by
   ~50 lines and remove a class of off-by-one bugs. Run after Phase B is
   green; verify with `make selfhost-check` (~7 min, only run after a
   batch of changes per
   `~/.claude/projects/-workspaces-CryoLang/memory/feedback_selfhost_check_cadence.md`).
2. `HashMap.get_ptr(&K) -> Option<V*>` (mentioned in B.2). Add when Phase
   B's docs store actually needs it; don't speculate.
3. Server-side logging. Right now write failures are silently swallowed
   in `server.cryo:send_response`. A `log::warn(message: Str)` that goes
   to stderr (never stdout — that's the wire) would help. Wait until Phase
   B/C surface a real "wish I could see this" moment.
