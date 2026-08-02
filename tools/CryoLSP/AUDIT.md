# CryoLSP Deep Audit — 2026-07-01

## STATUS (updated 2026-07-01, later same day)

**The LSP now builds, launches, and works on Windows** (verified against the
`examples/03-fibonacci` project via scripted stdio: clean handshake, 0 spurious
diagnostics, hover shows real signatures + doc comments, go-to-definition jumps
correctly, all with real percent-encoded VS Code URIs). All changes are
LSP-local; `compiler/` and `stdlib/` are untouched. **Not yet validated on
Linux** (the new `![target(unix)]` branches need a WSL build) and **not yet
committed**.

Fixed this session:
- **Build (compile):** deleted the dead, colliding `Diagnostic` wire struct from
  `protocol/lsp.cryo` (§0). It was never constructed; its declaration alone
  poisoned the compiler-library build.
- **Build (link):** added per-OS libclang link overlays to `cryoconfig` (the
  compiler dep gained libclang since this was last built).
- **Runtime — framing dead on arrival:** `String::push(byte)` had regressed to
  appending the byte's *decimal text*; the byte-at-a-time framing reader used it,
  so every header parsed as garbage → server bailed on frame 1. Switched the 12
  single-byte sites to `push_byte`. (This was the "it's been broken for weeks"
  runtime bug, on top of the build blocker.)
- **Runtime — Windows text-mode stdio:** `\n`→`\r\n` translation corrupted the
  wire (`Content-Length: N\r\r\n`). Added `_setmode(_,_O_BINARY)` for fd 0/1 in
  `main.cryo`, target-gated (no-op on POSIX).
- **Cross-document UAF (audit §1a):** added a compile-epoch guard
  (`SessionMap::ensure_fresh`) so a handler never walks a `ctx` whose shared AST
  arena was reset by another document's compile; recompiles the target doc if
  stale. Wired into hover/definition/completion/semantic_tokens/code_lens.
- **`file://` URI handling on Windows (was the reason stdlib/hover produced
  nothing):** `uri_to_fs_path` now percent-decodes (`%3A`→`:`) and strips the
  leading slash before a drive letter (`/C:/x`→`C:/x`); the reverse builder emits
  `file:///C:/...`. Both target-gated. This is what let the stdlib resolve.

Still outstanding (see §5 roadmap): debounce/coalesce the per-keystroke full
recompile (§1b — the biggest remaining usability item; needs timed stdin I/O),
the small monotonic leaks (§1e DiagRenderCache, §1f diag-id/hover strings), the
per-recompile ctx-header leak (§1c), UTF-16 position encoding (§F4/§3), and the
symbol-resolution `::`-vs-`.` unification (§2).

---


Full audit of the language server (`tools/CryoLSP/`, ~8,800 lines of Cryo) plus the
VS Code client (`tools/CryoAnalyzer/`, TypeScript). Goal: understand why the LSP was
disabled (memory + inconsistent `::`/`.` resolution) and chart the path to a clean,
production-grade server.

Method: verified build state directly (pinned `bin/cryo`), plus four parallel
deep-reads — symbol resolution (hover/completion/definition), compiler integration +
memory, protocol/JSON-RPC, and semantic tokens + client.

---

## 0. Current state: THE LSP DOES NOT BUILD

`cryo build` in `tools/CryoLSP/` fails — not on LSP code, but while compiling the
**compiler library it links as a dependency**:

```
error[E0233]: cannot find `Diagnostic::error`
 --> ../../compiler/src/compiler/types/trait_checker.cryo:71
 note: another `Diagnostic` exists: `Compiler::Diag::Diagnostic::Diagnostic`
error[E0358]: no method named `at` found on type `Lsp::Protocol::Lsp::Diagnostic`
 --> ../../compiler/src/compiler/parser/expr_parser.cryo:449
 ... (cascades across many compiler files)
```

The compiler builds fine **standalone** (`cd compiler && cryo build` → OK). It only
fails as an LSP dependency. Root cause: the LSP protocol module `Lsp::Protocol::Lsp`
(`protocol/lsp.cryo`) defines wire structs whose **bare names collide** with compiler
types — `Diagnostic`, `Position`, `Range`, `Location`, `Hover`, `CompletionItem`.
Cryo's resolver leaks a dependency's type names into the global name pool, so every
compiler file that references bare `Diagnostic` (trait_checker, expr_parser, and many
more) now binds to the LSP's struct instead of `Compiler::Diag::Diagnostic`.

This is the reason the LSP has been un-buildable "for weeks": a compiler-side change
(the `TraitChecker` from the pipeline-reorder work) added a new bare-`Diagnostic` site,
and the collision was already latent.

**Two possible fixes:**
- **LSP-side (recommended, low-risk):** rename the colliding wire structs so they're
  unique — e.g. `LspDiagnostic`, `LspPosition`, `LspRange`, `LspLocation`, `LspHover`,
  `LspCompletionItem` (or move them behind an alias never looked up bare). This unblocks
  the build immediately without touching the compiler.
- **Compiler-side (correct long-term):** fix the resolver so a dependency's type names
  are import-scoped, not globally visible by bare name. Larger change; benefits the whole
  language, not just the LSP. Should be tracked separately.

Everything below assumes the build blocker is cleared.

---

## 1. Memory — why it "leaked" and had to be turned off

The real story is more nuanced (and more dangerous) than a simple leak.

### 1a. CRITICAL — Cross-document use-after-free via the shared global arena
- `codegen/ast_arena.cryo:39` — `g_ast_arena` is **process-global**. AST nodes (and, when
  routing is active, the container heap) for *every* session live in this one arena.
- `instance.cryo:818` — every compile calls `cryo_ast_arena_reset()`, recycling the
  whole arena.
- Each open document has its own leaked `ctx` (`session.cryo:147` `SessionMap`), but they
  all share the one arena. Editing file A → `recompile(A)` → arena reset → **file B's
  still-live AST is recycled.** Any subsequent hover/completion/diagnostic/semantic-tokens
  request for B reads recycled memory → garbage, wrong results, or crash.
- Self-consistent within a single file (ctx is reassigned before reset), so it only bites
  multi-file editing — i.e. the normal case.
- The arena code even warns about exactly this (`instance.cryo:816-817`: "if the context
  ever becomes shared across targets, those node-pointer arrays would dangle").
- **Fix:** give each `CompilerSession` its own arena (thread a handle through the LSP
  compile entry points instead of the module global); reset/release only that session's
  arena on its own recompile / `didClose`.

### 1b. CRITICAL (usability) — Full whole-project recompile on every keystroke
- `text_sync.cryo:64-110` — `did_change` unconditionally `recompile()` + `publish` +
  `semantic_tokens_refresh` on **every** change. No debounce, no coalescing, no cancel.
- `textDocumentSync = Full`; `recompile()` (`session.cryo:88-138`) builds a fresh context
  from scratch and, when a `cryoconfig` is found (normal case), compiles the **entire
  project** (`compile_for_lsp_project_into` → all modules).
- For the CryoLang repo itself (123 `.cryo` files) that's ~1 GB of AST + a full frontend
  pass at ~100% CPU **per keystroke**. Because of the arena reset it doesn't grow
  unboundedly, but a sustained ~1-2 GB resident + full recompile per keystroke is exactly
  what reads as "eats memory, had to disable."
- **Fix:** debounce `didChange` (compile after idle, coalesce bursts); cancel in-flight
  compile on newer edit. Longer term: cache the module graph / reuse non-edited modules.

### 1c. HIGH — Genuine unbounded libc leak: context header + 7 sub-components per recompile
- `session.cryo:121-124` boxes + **leaks** the `CompilationContext` each recompile; the
  `Drop for CompilerSession` (`session.cryo:336-347`) deliberately keeps leaking it.
- `CompilationContext::new` itself `Box::leak`s 7 sub-components (DiagRenderer,
  PhaseArtifacts, DiagnosticSink, DiagConfig, ModuleGraph, InternTable, DeclarationIndex —
  `compilation_context.cryo:181-250`). These run **before** arena routing is enabled, so
  the headers are libc-allocated and never reclaimed — monotonic for process lifetime,
  even across `didClose`.
- The "leak is unavoidable" comment (`session.cryo:338-342`) is **half true**: no
  sub-component exposes `drop()`, BUT the reclamation machinery already exists
  (`cryo_ast_arena_release()` + reset, used by the build path at `instance.cryo:783-787`);
  the LSP path simply bypasses it. A per-session arena (1a) gives real teardown wholesale.

### 1d. HIGH — Single-file fallback leaks the entire container heap per keystroke
- `session.cryo:126-133` → `compile_for_lsp_content_into` (`instance.cryo:513-525`) resets
  the arena but **never** calls `cryo_alloc_arena_set_active(true)`. So all container heap
  (intern strings, resolver maps, type-arena tables, decl index, diagnostics) is
  libc-allocated and never freed. Bites scratch/no-project editing hardest.
- **Fix:** bracket this path with `set_active(true/false)` like `compile_project_with_ctx`.

### 1e. MEDIUM — DiagRenderCache grows unbounded
- `diag_render_cache.cryo` has no eviction; comment claims "a few thousand at most."
- Key is `hash(file, start_line, start_col, code)` (`conv.cryo:256-262`). During editing,
  inserting/deleting lines shifts positions → the id changes on nearly every keystroke →
  every publish inserts NEW entries instead of replacing (`conv.cryo:378-381`). Each entry
  holds a fully rendered multi-line report string. Never evicted.
- **Fix:** evict by URI on each publish (clear the file's prior ids before inserting).

### 1f. LOW — misc per-request leaks
- `conv.cryo:256-262` `build_diag_id` returns leaked raw `string`s (+ leaked intermediates),
  once per diagnostic per publish (× keystrokes).
- Hover string helpers `malloc` and never free: `escape_for_hover` (`hover.cryo:2067`),
  `clean_doc_for_hover` (1938), `i64_to_text` (2012), `first_segment_after` (1905);
  `definition.cryo:506` `drop_path_components`. Acknowledged as "per-request, acceptable
  until a per-call arena." A per-request arena fixes the whole class.

**Correctly handled (not bugs):** sessions ARE removed on `didClose`
(`text_sync.cryo:113-126`); backing `*_buf` strings are managed, not leaked; the
raw-`char*` aliasing is benign *within* a session (only 1a's cross-session reads make it
dangerous).

---

## 2. Symbol resolution — why `::` and `.` are inconsistent

Headline: **the two operators use two entirely different mechanisms**, and each of the
three handlers (hover/completion/definition) re-implements the split slightly differently.

- `.` (dot) → real semantic resolution: AST node → `NodeLocator::resolved_type_of` →
  carrier type → `decl_index.lookup_type_name` → member enumeration.
- `::` (scope) → **raw text name-matching**: scrape the identifier bytes before `::` off
  the line, intern the string, match decls whose leaf name string-equals it. The AST/type
  system is never consulted.

### F1 (root cause) — `::` is lexical, `.` is semantic (`completion.cryo:185-219`)
`::` can't see through import aliases, type aliases, or anything not literally spelled as a
top-level decl. `import x::HashMap as Map; Map::` → scraped `"Map"`, no decl named `Map` →
zero items → keyword list. The same type via a value (`m.`) completes fine.
**Fix:** resolve `::` through the AST/type system too — locate the `ScopeResolutionNode`,
resolve the scope to a `TypeRef`/namespace, enumerate from there (mirror the dot path).

### F2 — Trigger detection is a fragile byte back-scan (`completion.cryo:104-134`)
Only fires when `.`/`::` sits immediately before the caret and is preceded by an identifier
byte. So `Vec<i32>::` (preceded by `>`), `arr[i].`, `foo().`, `(expr).` all yield nothing.
No partial-prefix support: `Str::fr` breaks (char before caret is `r`). Completion only
works with the caret exactly on the trigger — matches the "works sometimes" report.
**Fix:** locate receiver/scope via the AST node under the cursor; support in-progress prefix.

### F3 — Failure masked as keywords (`completion.cryo`, 18 sites)
Every early-out returns the same static `make_keyword_list()`, so a failed `.`/`::`
completion is indistinguishable from success → reads as "flaky."
**Fix:** return empty/`isIncomplete` on resolution failure; reserve keywords for statement
position.

### F4 — Positions treated as bytes, not UTF-16 (`line_index.cryo:94-103`, and handlers)
LSP `character` is a UTF-16 code unit; the code adds it as a raw byte offset. Also
inconsistent: completion's dot path recomputes a byte column while hover uses
`character + 1` — so hover and completion can disagree on cursor location. Any non-ASCII
earlier on the line desyncs the cursor → wrong node → wrong/empty hover, mis-triggered
completion. (Same issue independently in semantic tokens and diagnostics — see §3/§4.)
**Fix:** one UTF-16-aware position helper used by every handler; or negotiate
`positionEncoding: "utf-8"` (see §3 F-enc).

### F5 — `ScopeResolutionNode` has no sub-spans (`AST/expression.cryo:667-680`)
`MemberAccessNode` has `member_span`; `ScopeResolutionNode` has only a whole-expr span, and
`node_locator.cryo` has NO `ScopeResolution` case in `first_covering_child`. So hovering
`Foo` in `Foo::bar` shows `bar`'s signature over the whole `Foo::bar`; clicking `Foo` in
`IoErrorKind::NotFound` jumps to the variant, not the type. The `.` path narrows correctly.
**Fix:** add `scope_span`/`member_span` to the node + a `ScopeResolution` case in
`first_covering_child`; branch hover/definition on which side the cursor hits.

### F6 — Three divergent scope-resolution ladders for the same `Foo::bar`
Hover: method → free fn → enum variant. Definition: method → free fn → type decl.
Completion: statics + variants only (no free fns, no type members). So `SomeModule::func`
hovers and go-to-defs but never completes — the exact "one works, other doesn't" symptom.
**Fix:** one shared scope-resolution routine returning a candidate set; all three consume it.

### F7 — `UnionDeclaration` dropped by completion (`completion.cryo:235-309`)
`collect_instance_members`/`collect_scope_members` handle Struct/Class/Impl/Enum but not
unions, though node_locator handles unions everywhere. `union_val.` / `UnionType::` → nothing.
**Fix:** add `UnionDeclaration` arms to both collectors.

### F8 — Dot hover has an annotation fallback; dot completion doesn't (`hover.cryo:322` vs `completion.cryo:194`)
Inside a generic body where sema didn't stamp a valid `resolved_type`, hover falls back to
the type annotation and works; completion falls straight to keywords. Same expression,
opposite results.
**Fix:** give completion hover's `annotation_carrier_name` fallback (share it).

### F9 — Range end off-by-one + two conventions (`node_locator.cryo:33` vs `hover.cryo:1446`)
`span_contains` treats `end_col` as inclusive; `range_from_span` emits `end_col - 1`.
AST-driven hovers/definitions under-highlight by one char, while `keyword_docs.cryo` uses a
correct exclusive-end path — visibly inconsistent.
**Fix:** pin the span-end convention; make `span_contains` and `range_from_span` agree.

### F10 — Field-vs-method heuristic flips mid-typing (`node_locator.cryo:383`)
Disambiguation scans the whole file for a `CallExpression` whose callee is this node. For
`obj.member` with no `(` yet → field-first; typing `(` flips to method-first → hover result
changes mid-keystroke for names that are both.
**Fix:** store an `is_callee`/parent bit at parse time.

### F11/F12 — Duplication that lets the above drift
`unwrap_to_carrier` (×3), `json_u32_field` (×3), `range_from_span` (×2), `leaf_after_separator`,
`unwrap_top_decl`, path builders — all duplicated across the three handlers. `definition.cryo:606`
even says "consolidating into a shared helper module is a follow-up once a third handler
lands" — three exist now. The duplication is what allows F6/F9 to diverge.
**Fix:** move shared resolution + range/JSON/URI helpers into `Lsp::Protocol::Conv` / a shared
resolution module.

---

## 3. Protocol / JSON-RPC / LSP 3.17 compliance

**Correct:** framing doesn't assume one-frame-per-read; CRLF + bare-`\n` handled; missing
Content-Length is a clean error; JSON-RPC id echoing (int + string), `jsonrpc:"2.0"`
validation, error-object shape all correct; Full-advertise matches Full-apply (no
advertise-incremental/apply-full corruption); `didClose` clears squiggles.

**H1 — Unbounded `Content-Length` → OOM DoS** (`framing.cryo:106`). Parses length into a
`u64` with no cap, then `String::with_capacity(len)` — `Content-Length: 99999999999999`
allocates terabytes before reading a byte. `parse_u64_skip_ws` (framing.cryo:216) also has
no overflow guard. **Fix:** clamp to a sane max (e.g. 32-64 MiB) → `InvalidContentLength`.

**F-enc — No `positionEncoding` negotiation; byte offsets shipped as UTF-16** (`lifecycle.cryo`
never reads `general.positionEncodings`, never advertises `positionEncoding`). This is the
protocol-level root of F4/§4. **Cheapest correct fix:** negotiate `positionEncoding:"utf-8"`
when the client offers it (vscode-languageclient v9 does) — then the existing byte math is
correct. Otherwise implement real UTF-16 conversion.

**M1 — Byte-at-a-time reads** (`framing.cryo:114,151`): one `read()` syscall per byte;
stdlib ships `BufReader`. Large `didChange` bodies = tens of thousands of syscalls per
keystroke. **Fix:** wrap `Stdin` in `BufReader`, read body via `read_exact`.

**M2 — No `$/cancelRequest`, no debounce** (see §1b). Stale hover/completion requests are
computed and answered; a slow compile blocks the whole loop (incl. shutdown).

**M4 — Missing production capabilities.** Advertised: hover, definition, completion (`.`/`:`),
semanticTokens (full only), codeAction, codeLens, Full sync. Missing (rough priority for a
systems language): `documentSymbolProvider` + `workspaceSymbolProvider` (table stakes),
`signatureHelpProvider`, `referencesProvider`, `renameProvider`(+prepare),
`documentHighlightProvider`, formatting, `typeDefinition`/`implementation`/`declaration`,
`inlayHintProvider` (inferred types — high value), `foldingRange`, semantic tokens
`range`+`delta`, and notifications `didSave`/`willSave`/`didChangeConfiguration`/
`didChangeWatchedFiles`, plus `$/progress` for compile feedback.

**L-level:** `Invalid` collapses the request id so malformed-but-id-bearing requests can't be
answered per spec (`jsonrpc.cryo:64-70`); `Position::from_json` has dead code (`lsp.cryo:44`,
looks up `"character"` on the line-number value and discards it).

---

## 4. Semantic tokens + VS Code client

**Correct:** delta encoding is right (`protocol/semantic_tokens.cryo:168-182`);
sort + dedup guarantee the encoder's non-overlap precondition; legend indices match the
advertised legend; no multi-line tokens emitted; code_action + code_lens are real, not stubs.

**S1 — Byte offsets, not UTF-16** (`semantic_tokens.cryo:1039-1093`, `cell_col_to_byte_col`):
same class as F4/F-enc. Non-ASCII earlier on a line shifts + mis-lengthens every later token
on that line ("split color mid-identifier" for Unicode). **Fix:** with §3 F-enc.

**S2 — Full re-tokenize per request + O(n²) insertion sort; no delta/range** (`:190,1140`,
capability `full:true` only). Whole-AST walk + n² sort per (debounced) edit. **Fix:**
O(n log n) sort; advertise + implement `range` (VS Code prefers it for large docs) and
optionally `full/delta` with a `resultId`.

**S3/S4 — Tab-column bugs.** Fallback path when the live buffer is missing treats
tab-expanded compiler columns as byte columns (`:1039-1049`); sub-span synthesis mixes
`strlen` byte lengths into cell-column coordinates (`walk_struct_literal:834`,
`walk_scope_resolution:882/911`). Cryo indents with tabs, so tab-indented tokens mis-place.
**Fix:** compute sub-spans in the cell-column space `emit_span` expects, or carry real spans
from the parser.

**S5 — Client never version-checks the server; versions disagree.** `resolveServerPath`
(`config.ts:57-142`) launches the first binary found across 7 probe locations with no version
handshake; `serverInfo.version = "0.1.0"` (`lifecycle.cryo:138`) vs `package.json` `1.0.0`.
A stale/incompatible `cryolsp` on `$PATH` is launched silently. **Fix:** compare
`serverInfo.version` after `initialize`, warn on mismatch; reconcile the version constant.

**Stubs / gaps (low):** `CastExpression` emits no inner tokens (`:679`, "real handling added
when needed"); `walk_new_expr` doesn't highlight the constructed type (`:797`);
`TypeAnnotation::Projection` TODO (`:996`); legend advertises `keyword`/`namespace`/`macro`/
`typeParameter` + `MOD_DEFINITION`/`MOD_DEFAULT_LIBRARY` that are never emitted (dead legend);
`code_action` omits the `diagnostics` correlation array (`:159`, "JsonValue has no clone");
handler uses mutable globals with a `g_doc_file` that dangles between requests (`:68-112,223`).

**Client is otherwise solid:** activation `onLanguage:cryo`, `**/*.cryo` watcher, restart
throttle (3-in-60s), documentSelector matches. Capability match is consistent except the
missing semantic-token `delta`/`range` and encoding negotiation.

---

## 5. Prioritized remediation roadmap

**P0 — make it build & stop corrupting memory (must-do to re-enable at all)**
1. Rename colliding protocol wire structs (`Diagnostic`→`LspDiagnostic`, etc.) → unblocks build (§0).
2. Per-session AST arena (own + reset/release per session) → kills the cross-document UAF (1a)
   and enables real teardown for 1c/1d/1f.
3. Debounce `didChange` + cancel in-flight compile → kills the per-keystroke ~GB recompile (1b).

**P1 — correctness of the features people use**
4. One UTF-16-aware position layer (or negotiate `positionEncoding:"utf-8"`) used by hover,
   definition, completion, semantic tokens, diagnostics (F4, F-enc, S1).
5. Unify `::` onto the AST/type resolution path; AST-based trigger + prefix support
   (F1, F2); one shared scope-resolution routine for all three handlers (F6); stop masking
   failures as keywords (F3).
6. `ScopeResolutionNode` sub-spans + `first_covering_child` case (F5); pin span-end
   convention (F9); union completion (F7); completion annotation fallback (F8).
7. Clamp `Content-Length` (H1); per-URI eviction in DiagRenderCache (1e).

**P2 — production polish**
8. Consolidate duplicated handler helpers into a shared module (F11/F12) — do this alongside
   5/6 so they can't re-diverge.
9. `BufReader` framing (M1); semantic tokens O(n log n) + `range`/`delta` (S2); tab-column
   fixes (S3/S4).
10. New capabilities in priority order: documentSymbol, references, rename, signatureHelp,
    inlayHint, formatting, foldingRange; notifications didSave/didChangeWatchedFiles;
    `$/progress` compile feedback (M4).
11. Client version handshake + reconcile version constants (S5); per-request arena to retire
    the malloc-leak helpers (1f); thread a per-request context struct to retire the mutable
    globals (S-globals).

**Separate compiler track (benefits the whole language):** fix bare-type-name resolution so a
dependency's types don't pollute the global name pool (§0 root cause).
