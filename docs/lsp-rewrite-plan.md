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
| C9. A mark on parser-synthesized nodes (f-string runtime calls), or `NameOrigin::Generated` | [neutral] | Replaces two spelling checks in semantic tokens. Built as `ASTNode.generated` (step 7). |
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

### Step 1. Protocol hardening and positions (landed)

- B1's cause, measured with a probe: `match (resp.error)` through a
  reference moves the `Option` payload out and releases it at the end of the
  arm, and the caller then releases the response again (no move checking
  yet). `match (&...)` borrows; the two other by-value field matches in the
  protocol layer (`Hover.range`, `CompletionItem.detail`) are borrowed too.
- Every invalid frame is answered: a parse error under `null`, a request
  with no method as invalid under its own id; a client's response to a
  server request is read and dropped. Requests after `shutdown` are
  refused. `Content-Length` takes digits and surrounding blanks only.
- `line_index::Columns` is the one conversion between wire columns (UTF-16,
  or bytes under `utf-8`), byte offsets and compiler columns (a tab advances
  to the next 4-column stop). `Docs` applies it: a cursor to a compiler
  position, a byte offset to a wire position, a span to a wire range
  measured against the span's file (its open buffer, or the file on disk).
  Diagnostics, related information, suggestions, inactive regions, hover and
  definition ranges, code lenses and semantic tokens all go through it.
- `workspace/semanticTokens/refresh` is sent only to a client that declares
  it.
- Checks: a `protocol errors` run (a request before initialize, two unknown
  methods, a request with no method, a body that is not JSON, a real request
  after them, no refresh to a client that did not declare it, a request
  after shutdown, exit 0) and a `positions` fixture (tab-indented lines,
  text outside ASCII, a warning on a tab-indented line).

### Step 2. One project, all overlays, on save, last-good snapshot (landed)

- Compiler: `BuildOptions.source_overrides` is a list, and
  `SourceOverride::content_for` is the one lookup the loader and the Lexing
  pass both ask. `compile_for_lsp_project(config, focus, overlays)` takes an
  empty config to mean the focus file is its own project (ruling 331),
  through `single_file_config`, which `compile_file` and `check` now share.
  `compile_for_lsp_content`, `build_frontend_pipeline` and
  `TokenStream.source_content` are gone. `CompilationContext.frontend_complete`
  says every module parsed, so the name layer ran over the whole tree. The
  three process tallies are reset at the start of each compilation (C2: read,
  not measured: nothing reset them).
- Server: `session::Projects` (one per cryoconfig, or per lone file)
  replaces the per-document `SessionMap` and its LRU. Every analysis reads
  every open buffer. Analyses run on a project's first open, on save
  (advertised) and on a watched `.cryo` change; edits only update `Docs`. An
  analysis is adopted when `frontend_complete`; otherwise it publishes its
  diagnostics and is released. Each open document keeps the text the current
  analysis read, and positions are carried between it and the buffer by
  shared leading and trailing lines; a line inside the edited region gets no
  answer. Completion's placeholder analysis is a separate scratch analysis.
- Measured with three compiler files open: before, 86.7 s and 5.57 GB (one
  full analysis per document); after, 28.6 s and 1.86 GB (one analysis).
- Checks: an edit to one open file reaches another file's diagnostics when
  saved; a saved syntax error leaves the last analysis answering on
  unchanged lines; a line inserted above, unsaved, moves the answer with it;
  a line edited since the analysis answers nothing.

### Step 3. The compiler's identity lookups (landed, C6 moved to step 6)

- Measured first (ruling 334) with a probe linking the compiler library and
  walking the trees `compile_for_lsp_project` leaves, over the `two_modules`
  fixture and the compiler project (247 modules). Outside specializations,
  every identifier, `this`, path scope segment, path used as a value, path
  call, method call, struct literal, `new`, pattern and impl target written
  in the compiler project and the standard library it reaches carries its
  stamp. What carries none:
  - imports: 0 of 4,066 (no field; C4 below makes it 4,066 of 4,066);
  - `.length` on a built-in array: 3,634 accesses, no member pin and no
    declaration to point at - the answer is the array type's;
  - a class constructor called by the class's name, `Parser(...)`: 13 calls
    with the identifier stamped with the class and no call pin;
  - a call through a field holding a function, `this.f(x)`: 2;
  - about 79 type names inside specialized type declarations, which carry
    `pre_resolved` instead.
  Method calls: 24,625 pinned `Decl`, 608 carried by another slot
  (`template_method`, `resolved_template`, `named_method`, a bound's trait).
  With a type error in the project only the refused call loses its stamp.
- **A position lookup must skip specializations.** A module's tree holds
  its templates and also their monomorphized copies: 32,089 functions marked
  `is_specialization` and 12,501 cloned nodes whose spans name another file,
  every one carrying a template's spans. The innermost node at a position can
  be a copy.
- C3: `DefTable::span_of`; `DeclarationIndex::declaration_of`, the store the
  methods' declarations already used, now kept for every type, function,
  global and intrinsic the name layer declares (`method_decl_of` answers it
  for a method only, as before).
- C4: `ImportDeclNode.resolved_module`, written where the name layer
  resolves the path.
- C5: `Resolver::scope_at(module, line, col)` and
  `Resolver::visible_bindings(scope)`; the did-you-mean pools are filters
  over `visible_bindings`. A scope already kept its span and children.
- C8 (from step 4): a qualified type name's span covers its whole path.
  Negative tests pin lines, not columns, so none moved.
- C6 lands with completion (step 6), its only reader.

### Step 4. Definition and hover from stamps (landed)

- `handlers/identity.cryo`: a position's chain of nodes, from the module's
  first top-level statement holding it (a monomorphized copy is skipped)
  down through `NodeLocator::first_covering_child`, and the target the
  innermost node's stamp names: a definition, a local or binding, a type
  parameter, a primitive, a field (owner and position) or a variant (enum and
  ordinal). Receivers by `is_this_keyword` and the receiver's type; calls by
  their pin, `template_method` or a bound's trait and slot; paths by
  `scope_res` and `member_def`; match arms by `binding_sym` / `const_res`;
  annotations by `NamedAnnotation.res`; imports by `resolved_module`.
- `definition.cryo` answers the target's declared name
  (`DeclarationIndex::declaration_of`, then `DefTable::span_of`; a binding's
  own declaration; a module's `namespace`), `hover.cryo` the declaration it
  names. The implementations lens counts the blocks whose stamped target or
  trait is the declaration's definition.
- Deleted: the old hover (3,595 lines) and definition, `decl_finder.cryo`,
  34 of the 39 `![allow(lookup_by_spelling)]` (19 in hover, 15 in the
  finders; completion's 3 and semantic tokens' 2 remain), the walks in
  `node_locator.cryo` nothing calls now, `Resolver::resolve(span)`,
  `ResolutionMap::resolve`, `TraitDeclNode::lookup_method`.
- Not kept: the expanded hover's method list (`cryo/expandHover` answers the
  ordinary hover until C6 lands); hovers on expressions that name nothing
  (a literal's type).
- Checks: B2-B5 and the lens's targets on `two_modules`, plus a field
  through `this`, a variant path, a type parameter and an import.

### Step 5. References, document highlight, document and workspace symbols (landed)

- `handlers/uses.cryo` (ruling 326): every name a module's tree holds where
  it is written, with its target, read by the same stamp readers
  `identity.cryo` uses at one position, and whether it is the declaration.
  A struct literal's type and its initializers' fields have no span of
  their own; their range is the written name's length from where the node
  starts. A method's receiver annotation, which the parser writes, is not a
  use. The index is built per request and not kept (ruling 337). On the
  compiler project (247 modules, a 2.14 GB analysis): references of
  `SourceSpan`, 774 uses in 61 files, 0.42 s; of `CompilationContext`, 228
  uses in 42 files, 0.12 s; at most 6 MB more while a request runs, given
  back after it. No cache is needed at that cost.
- `references.cryo`: `textDocument/references` (with `includeDeclaration`)
  over every module, `documentHighlight` over the document's.
  `symbols.cryo`: `documentSymbol` (members nested under their type,
  implementation blocks as containers) and `workspace/symbol` (a name
  holding the query, ignoring case, at most 200). All four advertised.
- Checks: B's `Item` references without A's and A's without B's, a method
  call's, a field's (its declaration, a literal's initializer, a read
  through `this`), a local's highlight, an outline, a workspace search.

### Step 6. Completion from the snapshot plus the current line (landed)

- The cursor's line, up to the cursor, is read as a receiver before `.`
  (`a.b().c[0].`), a path before `::`, a module path after `import`, or a
  bare name. The scope is `Resolver::scope_at` at the cursor's position in
  the snapshot; a cursor on an edited line is placed on the analysed line no
  further than the text the two share, or at the start of the edit.
- A receiver's head through `lookup_value` (a local's or parameter's type
  read off its declaration, a global's, a function's return) or `this` (the
  type whose declaration or block holds the method); each `.f` through
  `MemberResolver::field_of`, each `.m()` through `lookup_family_entries`.
  A path's head through `type_binding`, then `modules_written_as`; later
  segments through `exports_named`, then `modules_written_as` under the
  module. An import's path through `ModuleGraph::module_of_written_path`.
- C6 needed no compiler change: a type's members are its declaration's,
  the methods of every block whose stamped target it is, and its base
  class's (`handlers/members.cryo`); a module's are `Resolver::get_exports`
  and the modules nested under it. A bare name lists `visible_bindings`
  (not a type body's fields, methods or variants, which a bare name never
  reaches, nor a local declared after the cursor), the prelude's exports,
  the imported modules and the keywords.
- Not read: a receiver whose head is a path (`T::make().`); an import path
  that names no loaded module (`import Probe::` where only `Probe::A` and
  `Probe::B` exist). The client filters by the typed prefix.
- `cryo/expandHover` lists a type's members under its hover again.
- Deleted: the placeholder splice, the analysis per completion
  (`Projects::analyse_buffer`, the project's scratch analysis), the
  spelling-matched member walks and their 3 `![allow]`s. The compiler
  project answers a completion in under 5 ms.
- Checks: B6 (a module path, a type path, an enum, a bare name in a
  function and in a method), member completion with a syntax error
  elsewhere in the buffer and nothing written to stderr, `this.`, an
  import's path, the extended hover.

### Step 7. Semantic tokens and lenses from stamps (landed)

- C9 is `ASTNode.generated`: the compiler wrote the node where the source
  spells nothing, with the span of the source it stands for. Set on an
  f-string's lowering (its calls, their callees, its literal runs and the
  `&` around each hole), an implicit conversion's call and callee, a
  lambda's closure struct and its `Drop` block; the cloner keeps it. A
  marked expression is looked through to the written code it wraps (the
  holes, the converted operand); a marked declaration is skipped whole,
  since the closure struct's method holds the lambda's body, which is
  written where the lambda is. `NodeLocator`'s picks look through marked
  nodes.
- Tokens are the uses index (`uses.cryo`) coloured by identity: a module,
  a type by its declaration's kind, a function, a method (static or not), a
  global (`readonly` when constant), a local or parameter (`readonly` when
  not `mut`), a type parameter, a field, a variant; `declaration` on a
  declaring name. A method's receiver and `this` keep the grammar's keyword
  colour (the receiver is marked in the index, not compared by spelling).
  Import and namespace path segments are namespaces; inline assembly keeps
  its lexer, its `${...}` operands now in the uses index. The
  implementations lens already counted blocks by stamp; it now skips
  compiler-written blocks.
- Deleted: the syntax-driven walker and its spelling checks (`__Closure`
  in a struct's name, receiver names, f-string runtime callee names, the
  span-shape test for a conversion) with the last 2 `![allow]`s - the
  server has none.
- Checks (a third fixture, `generated`: an f-string, a capturing lambda, a
  converted literal): tokens for a parameter, type parameter, constant,
  local, call, capture and a name in an f-string's hole, none at a lambda
  or a converted literal; hover, definition and references through an
  f-string's hole; references of a captured local; tokens on
  `two_modules` (module path, type path, match arm binding and constant,
  parameter, static and instance calls). The previous server coloured a
  parameter as a variable and a module path as a type, left match arms and
  f-string holes uncoloured, answered hover inside a hole with
  `fmt_append_display`, and listed a captured local's use twice (the closure
  struct's copy of the lambda body).

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
