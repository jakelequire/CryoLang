# Language server rewrite: plan

The plan for the `lsp-rewrite` branch (ruling 316). It is not normative:
`docs/resolution-rules.md` and `docs/cryo.md` win wherever they speak. It is
folded into `docs/roadmap.md` and deleted in the commit that finishes the
work. Each step below records what landed as it lands.

Figures were measured on `main` at `0a1438ea` (2026-10-13) on the Windows
host, with the server `make lsp-check` built, unless they say "read" (taken
from the source, not observed).

---

## 1. Where the server stood

`tools/CryoLSP`: 12,165 lines of Cryo, single-threaded, synchronous, stdio.
It links the compiler as a library and, for every answer, runs the real
front end (`CompileMode::Lsp`: everything except code generation) over the
**whole project plus the whole standard library**, then walks the result.
The VS Code client is `tools/CryoAnalyzer`.

Advertised: full-text sync, hover, definition, completion (`.` and `:`
triggers), full semantic tokens, code actions (from compiler suggestions),
code lenses. Not offered: references, document or workspace symbols, rename,
signature help, formatting, inlay hints, `didSave`.

### Solid

- Framing: bounded headers and body, chunked reads, CRLF or LF.
- The JSON layer is the standard library's (`std::json`), depth-capped.
- Diagnostics convert faithfully (code, labels, children, suggestions with
  applicability), with a rendered-report view and quick fixes.
- The debounce and the per-session arena: memory per edit is flat
  (249, 258, 259 MB over three edits of `examples/10-expr-interpreter`).
- Member completion after `.`, and definition and hover of locals and
  parameters, are right.

### Broken

| # | Defect | Evidence |
|---|---|---|
| B1 | **Any error reply corrupts the heap; the next message kills the server** (exit `0xC0000374`, no stderr). The client restarts it up to 3 times a minute and then gives up. | measured, 4 of 4 sequences |
| B2 | Definition and hover on a qualified call land in the wrong module: `B::Item::make`, `wb.get()`, `B::helper()` all answer with A's. | measured |
| B3 | Inside B's own file, `this`, `Item` and a struct literal mean A's `Item`; the implementations lens on A's `Item` points at B's `implement`. | measured |
| B4 | Match arms contradict the compiler: a binding `Ready` hovers as `Mode::Ready = 0`; a constant `LIMIT` gets nothing. | measured |
| B5 | A qualified annotation `B::Item`: no definition, hover just `Item` (the parser's span covers the first segment only). | measured |
| B6 | Completion after `B::Item::` or `B::`, and on any bare identifier, is the keyword list. | measured |
| B7 | Any syntax error blanks the file: definition and hover answer `null` everywhere. One syntax error in any module stops the whole project after parsing. | measured |
| B8 | Every edit re-analyses the whole project and the whole standard library: the fixture 1.2 s, `examples/10` 1.8 s, the compiler project **38 s cold, 40 s per edit, 2.1 GB** (`cryo check` on it: 33 s, so this is the front end's own cost). | measured |
| B9 | One full-project analysis per open document, up to 3 live: three open compiler files are three 2 GB analyses. | read; matches the 2.8-3.7 GB seen beside low-memory `verify` failures |
| B10 | Each document's analysis overlays only its own buffer; other unsaved buffers are read from disk, and editing A never invalidates B's analysis. | read |
| B11 | Byte columns are labelled UTF-16 (VS Code's client offers only UTF-16), and the compiler's column advances to a 4-column stop on a tab. Only semantic tokens converts. | measured |
| B12 | No cancellation; a malformed request with an `id` gets no reply; requests after `shutdown` are still served; `Content-Length: 12abc` reads as 12. | read |
| B13 | About 108 call sites answer by spelling, over 13 last-segment finders in `handlers/decl_finder.cryo` and the name-returning walks left in `node_locator.cryo`; 39 inert `![allow(lookup_by_spelling)]`. | read |
| B14 | Nothing gated the server's answers: `lsp-check` proved it compiles; `smoke.py` had no caller. | read |

So the wire itself is sound; B1, B11 and B8-B10 are not resolution
problems, and are in this plan beside the resolution work.

---

## 2. Goals and non-goals

Goals for the first release:

1. The server never dies from a protocol event, and answers every request.
2. Every answer about a name comes from an identity the compiler stamped.
   No spelling is a key; a spelling reaches a lookup only through a door
   (rules 3 and 4).
3. One analysis per project, not per document, with every open buffer
   overlaid.
4. Analysis on open and save; a last-good snapshot answers while the buffer
   is broken or mid-edit.
5. Positions are right on every line.
6. A behavioural harness gates every feature, run by `lsp-check`, so CI on
   `main` covers it on both hosts.

Features, in order: diagnostics, definition, hover, references and document
highlight, document symbols, completion; then workspace symbols, semantic
tokens from stamps, the implementations lens, rename, signature help, inlay
hints.

Not in the first release: per-keystroke analysis, incremental text sync,
threads, a query-based incremental compiler, formatting, module caching
beyond its design (ruling 329), parsing that continues past an error
(ruling 330).

---

## 3. Target design

```
 VS Code (tools/CryoAnalyzer)
     |  stdio, JSON-RPC 2.0, positions UTF-16
     v
 +-----------------------------------------------------------+
 | protocol/   framing, jsonrpc, dispatch, lifecycle          |
 |             PositionMap: UTF-16 <-> byte <-> compiler col  |
 +-----------------------------------------------------------+
 | workspace/  Docs: every open buffer (text, version)        |
 |             Projects: one per cryoconfig root              |
 |               - overlays = all open buffers in the project |
 |               - current: Snapshot  (last good, immutable)  |
 |               - building: Snapshot (own arena, on save)    |
 +-----------------------------------------------------------+
 | features/   diagnostics, definition, hover, references,    |
 |             symbols, completion, tokens, lenses, rename    |
 |             (each: position -> node -> identity -> answer) |
 +-----------------------------------------------------------+
                    | compiler library (in-process)
                    v
 +-----------------------------------------------------------+
 | compiler: CompileMode::Lsp over a list of overlays         |
 |   identity doors:                                          |
 |     node at (file, line, col)        node_locator, exists  |
 |     DefTable::span_of(DefId)                               |
 |     DeclarationIndex::declaration_of(DefId)                |
 |     Resolver::scope_at(file, line, col) -> ScopeID         |
 |     Resolver::visible_bindings(ScopeID)                    |
 |     members of a type / exports of a module, by identity   |
 |     the module an import resolved to (stamp)               |
 |   a yield hook between modules during a long analysis      |
 +-----------------------------------------------------------+
```

**Snapshot.** One `CompilationContext` in its own arena, the text of every
file as it was analysed, and a uses index built on first need. Never mutated
after its analysis. A new analysis builds in a second arena and replaces the
current snapshot **only if it reached name resolution**; otherwise the
current one stays and only the new diagnostics are published. At most two
analyses live per project.

**Answering from an older snapshot.** A line whose text is unchanged since
the snapshot is mapped by a line diff and answered exactly; a changed line
gets no stamp-based answer, so a stale answer never points at the wrong
token.

**One path per answer:** position -> innermost node -> the stamp on it
(`Res`, `CalleePin`, `MemberPin`, `scope_res`, `const_res`, `def`,
`resolved_type`) -> identity -> `span_of` / `declaration_of` / type display /
uses index. A node with no stamp answers nothing; there is no second lookup.

**Completion = snapshot + current line.** The text before the cursor is
lexed into a path (`a.b.`, `X::Y::`, a bare prefix). Its head is resolved
through the doors in the scope at the cursor (`scope_at` on the snapshot):
`Resolver::lookup_value` / `type_binding` for a bare head, `exports_named` /
`module_named` for a module path, `field_of` / `lookup_family_entries` for
members. Candidates are enumerated by identity, plus the keywords valid at
that point. No placeholder splice, no analysis per completion.

**Triggers.** `didOpen` of a project's first file, `didSave` of any file in
it, and `didChangeWatchedFiles` for a `.cryo` changed on disk. Edits between
saves only update `Docs`. A long analysis services read requests from the
current snapshot between modules (the yield hook); there are no threads.

**A file outside any project** is analysed as its own entry point through
the same path (ruling 331); the single-file fallback goes.

---

## 4. Compiler-side work

**[neutral]** changes no compiled output (proven with
`make verify ARGS="--baseline HEAD --require-identical"`); **[behaviour]**
changes what a program compiles to or what a diagnostic says.

| Item | Kind | Notes |
|---|---|---|
| C1. `BuildOptions.source_override` becomes a list of overlays | [neutral] | The loader and the Lexing pass compare one path today (`module_loader.cryo:194-214`, `pass_registry.cryo:512-529`). |
| C2. Reset the per-compile process tallies (`g_unregistered_defs`, `g_unstamped_decls`, `g_pending_bugs`) | [neutral] for the CLI | read: never reset. Measured before it is changed. |
| C3. `DefTable::span_of(DefId)`, `DeclarationIndex::declaration_of(DefId)` | [neutral] | ruling 325. `DefTable` already stores a span per def with no reader. |
| C4. `ImportDeclNode` records the module it resolved to | [neutral] | ruling 327. |
| C5. `Resolver::scope_at`, `Resolver::visible_bindings`, shared with did-you-mean's `visible_*_names` | [neutral] if did-you-mean's text is unchanged | ruling 325. Scopes keep spans and children. |
| C6. Enumerate a type's members and a module's exports by identity | [neutral] | Enumeration takes no spelling, so it is not a door. |
| C7. The uses index | none | In the server, built from stamps (ruling 326). |
| C8. A qualified annotation's span covers its whole path | **[behaviour]** | ruling 328: diagnostic underlines move; negative tests whose columns move are re-annotated. |
| C9. A mark on parser-synthesized nodes (f-string runtime calls), or `NameOrigin::Generated` | [neutral] | Replaces two spelling checks in semantic tokens. |
| C10. A yield hook between modules in the stage drivers, null in the CLI | [neutral] | ruling 329. The server swaps the published arena around the callback, as it already does between analyses. |
| C11. Module caching | design only in the first release | ruling 329. The stages run stage-major over all modules and mutate shared tables, and monomorphization writes specializations into the tree. The standard library is about 1 s of the compiler project's 40 s; the rest needs per-module reuse. |
| C12. Parsing that continues in `CompileMode::Lsp` | **[behaviour]**, after the first release | ruling 330. |
| C13. The import suggestion for an already-imported module is not machine-applicable and suggests the qualified name | **[behaviour]** | ruling 332 (`sema/diagnostics.cryo:357-380`). |

Any step touching `compiler/src` runs `make selfhost-check` and refreshes the
pins before it lands.

---

## 5. Steps

Every step's gates: `make check-fast`; `make lsp-check` (which runs the
harness); `make verify ARGS="--baseline HEAD --require-identical"`, with any
moved object declared; a clean build with 0 warnings. With compiler changes,
also `make selfhost-check` and refreshed pins.

The evidence each step's commit states (ruling 324): the checks it adds, the
server from before the step failing them, and the new one passing.

### Step 0. The harness that can refuse (landed)

`tools/CryoLSP/tests/smoke.py` (ruling 269) drives a server over stdio:
protocol checks in code, then each fixture under
`tools/CryoLSP/tests/fixtures/` in its own process. Positions by marker
(`/*@name*/`, `/*^name*/`); `checks.json` holds definition, hover,
completion and diagnostics checks; an `edit` asks about an unsaved buffer.
`scripts/lsp-gate.py` runs it against the binary it linked and passes only
with no failure and at least `--min-checks` passing. `docs/testing.md`
section 9 says how to add a check.

First fixture, `two_modules`: two modules declaring the same `Item`, `make`,
`get` and `helper`, an `implement` in one of them, a match arm binding and a
constant arm, locals and a parameter. Its other questions are asked by later
steps.

### Step 1. Protocol hardening and positions

- Instrument B1 first, then fix the ownership on the error-reply path.
- Reply `InvalidRequest` / `ParseError` when an id can be recovered; honour
  the post-shutdown gate; strict `Content-Length` digits.
- `PositionMap`: one conversion between UTF-16 columns, byte offsets and the
  compiler's columns (a tab advances to the next 4-column stop), used by
  every handler and by diagnostics. UTF-8 when the client offers it.
- `workspace/semanticTokens/refresh` only when the client declares it.
- Checks: unknown methods, a malformed request, a request after shutdown;
  a `positions` fixture with tab-indented lines and text outside ASCII.

### Step 2. One project, all overlays, on save, last-good snapshot

- C1, C2. `Projects` replaces the per-document `SessionMap` and its LRU.
  Analysis on open, save (advertised) and watched-file change. The
  double-buffered snapshot. Diagnostics published per file with the
  document's version.
- Deletes the single-file fallback, `ensure_fresh`, `needs_recompile`,
  `flush_pending`.
- Checks: an unsaved edit in one file reaches another file's diagnostics
  after a save; a syntax error leaves definitions on unchanged lines
  answering from the last good snapshot. Memory with three compiler files
  open is at most two analyses.

### Step 3. The compiler's identity doors

- First instrument that every stamp the features read is populated in
  `CompileMode::Lsp` at the fixture's markers, and how often a method call's
  pin is not `Decl`.
- C3, C4, C5, C6. Proven `--require-identical`.

### Step 4. Definition and hover from stamps

- New definition and hover on the single path. Receivers by
  `is_receiver()` / `is_this_keyword`; match arms by `const_res` /
  `binding_sym`; generic parameters by `SymbolID` and
  `GenericParamType.param_sym`; imports by C4. C8 lands here.
- Deletes `handlers/hover.cryo`, `handlers/definition.cryo`,
  `handlers/decl_finder.cryo`, the name-returning walks in `node_locator.cryo`
  (`find_method_owner`, `find_enclosing_method_owner`, `enclosing_owner_of`,
  `find_program_by_namespace`), the server's reads of
  `Resolver::resolve(span)`, `TraitDeclNode::lookup_method` if unused, and
  every `![allow]` on deleted code.
- Checks: B2-B5 on `two_modules`.

### Step 5. References, document highlight, document and workspace symbols

- The uses index (ruling 326). Checks: reference sets that tell A's `Item`
  from B's; a symbol outline.

### Step 6. Completion from the snapshot plus the current line

- Member, path (absolute and relative), bare identifiers from the scope at
  the cursor, module paths after `import`, keywords. Deletes the placeholder
  splice, the analysis per completion, and `lookup_type_name` if unused.
- Checks: B6; completion in a broken buffer; completion runs no analysis.

### Step 7. Semantic tokens and lenses from stamps

- Tokens coloured from stamps; synthesized nodes by C9; the implementations
  lens by `ImplBlockNode.res`. The `![allow]` count reaches zero.

### Step 8. Long analyses do not freeze the editor

- C10, and `$/cancelRequest` for queued requests. Measured: a hover sent
  during a save's analysis of the compiler project answers from the
  snapshot.

### Step 9. Rename, signature help, inlay hints

- Rename from the uses index (refusing to rename into the standard library
  or a vendored module); signature help from `CalleePin`; inlay hints from
  `resolved_type`. C13.

### Step 10. Module caching: the design

- Measure per-stage time and the standard library's share in Lsp mode;
  answer how a snapshot is invalidated when an imported module changes, and
  whether the cache is per module or per file. Written here; built after the
  first release.

### Step 11. Close-out

- Delete `tools/CryoLSP/AUDIT.md`, the ignored `build_dbg/` and
  `build_linux/`, stale comments. `serverInfo.version` from the stamp. Fold
  this plan into `docs/roadmap.md` and delete it.

---

## 6. What goes

- `handlers/decl_finder.cryo`; `handlers/hover.cryo`, `definition.cryo`,
  `completion.cryo`, `semantic_tokens.cryo`, `code_lens.cryo` as their
  replacements land.
- In the compiler: `node_locator.cryo`'s name-returning walks and
  namespace-by-string lookup; `compile_for_lsp_content` if unused;
  `lookup_type_name` and `TraitDeclNode::lookup_method` if unused.
- All 39 `![allow(lookup_by_spelling)]` (ruling 184).
- `SessionMap`, its LRU, `ensure_fresh`, the placeholder splice.
- Kept: framing, the JSON-RPC codec, diagnostic conversion, the rendered
  diagnostic view, code actions, keyword docs, inactive regions, `smoke.py`.

---

## 7. Risks

- Stamps not populated where read: step 3 instruments before step 4 relies
  on them.
- The analysis freeze on large projects until step 8, and the cost per save
  after it. The compiler project is the largest Cryo program; examples
  analyse in under 2 s.
- Memory: two analyses per project during a save, about 4 GB at peak on the
  compiler project.
- Process-global state across analyses in one process (C2), and the
  published-arena discipline: every read of a snapshot publishes its arena
  first.
- The harness is a gate CI runs on both hosts: one server per fixture,
  generous timeouts on cold analyses, no wall-clock assertions.
- Missing move checking can produce more double frees in server code that
  drops by hand.

## 8. Decisions

All Jake's, recorded as rulings 324-332: the harness grows per step (324);
the door names (325); the uses index in the server (326); the resolved
module on `ImportDeclNode` (327); the whole-path span (328); module caching
a design only, with the yield hook (329); parsing that continues, after the
first release (330); a lone file as its own entry point (331); the import
suggestion (332).
