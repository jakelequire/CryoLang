# Cryo Deep Review — Findings Report

> Generated 2026-07-16 from a seven-track deep review of the full repository:
> compiler frontend, sema/type system, codegen/pipeline, stdlib, test suite &
> build/bootstrap infrastructure, LSP/tooling, and language design/docs.
> All grades are calibrated against **production compilers** (rustc, clang,
> gopls, Go/Zig stdlibs), not against hobby projects. Every finding marked
> **[verified]** was confirmed against source by a second pass; the rest were
> reported by a single deep-read reviewer.
>
> Usage: check items off as they land. Move items between tiers freely — the
> tiers below are the reviewers' recommendation for pre- vs post-1.0, not law.

---

## Scorecard

| Subsystem | Grade | One-line verdict |
|---|---|---|
| Frontend (lex/parse/AST/diag) | B+ | Diagnostics subsystem is rustc-calibre; span precision + hand-synced tables are the debt |
| Sema / type system | B- | Execution discipline A-; structural flaw is non-canonical type identity |
| Codegen / pipeline | B- | ABI classifier near clang quality; "correct-by-vigilance" is the pattern to break |
| Stdlib | B+ | Reads like a designed stdlib; ahead of Rust 1.0 in places; safety-by-documentation is the cost |
| Tests / bootstrap | B+ | Selfhost byte-identity gate stricter than rustc/Go/Zig practice; diagnostics coverage thin |
| LSP / tooling | B | Bindgen A- (best in repo); LSP B-; formatter D (dead prototype) |
| Language design / docs | B- | Coherent identity, honest docs; sharp edges frozen too early |

Calibration anchor: codegen/pipeline ≈ "rustc circa 2013–2014" (pre-MIR,
string-keyed monomorphization era).

---

## Tier 0 — Verified soundness / miscompile bugs (fix before 1.0)

- [x] **Enum drop-glue payload offsets ignore alignment** **[verified]**
  `compiler/src/compiler/codegen/ops/call_emitter.cryo:1406-1442`
  Enum payload layout is computed independently in **four** places. Three
  align fields up (e.g. `pattern_emitter.cryo:205-231`, whose comment promises
  "the SAME align-up rules as compute_enum_layout … and the construction
  side"); the drop-glue copy uses a packed running sum with **no align-up**
  and no `InstantiatedType` unwrap (wrappers report size 0). Construction
  writes at aligned offsets; glue drops at packed offsets → frees a garbage
  pointer for e.g. `V(u32, String)`-shaped payloads.
  *Fix direction: single layout authority (see D1) or at minimum make glue
  call the same align-up helper as construction.*

- [x] **MoveCheck / DropInsertion diverge on assignment RHS → double-free hole** **[verified]**
  `compiler/src/compiler/passes/move_check.cryo:1198-1201` walks the RHS of
  `x = y` in **read** context (never marks `y` moved);
  `passes/drop_insertion.cryo:2479-2494` walks the same RHS in **move**
  context (suppresses `y`'s scope-exit drop). Result: `x = y; f(y);` with
  non-Copy `y` is compile-accepted; `x`'s scope-exit drop and `f`'s callee
  param drop free the same storage. Violates the passes' own stated invariant
  (`drop_insertion.cryo:137-143`) that their move-sets must be identical.
  *Fixed:* `move_check::read_binary` now moves the `=` RHS
  (`walk_expr_move(rhs, real=false)` + explicit `check_field_move_out`), matching
  drop_insertion's move-set. `real=false` deliberately skips the real-move
  array-move-out checks so the swap-remove idiom `a[i] = a[last]` and shallow
  array-field copies don't false-positive. The fix exposed 4 genuine (benign but
  model-illegal) assign-move-then-use sites in the compiler's own source
  (`pass_registry`, `config_gating`, `module_graph`), now reading via the new
  owner. Regression test: `tests/negative/E0452_assign_rhs_use_after_move.cryo`.
  Gated (test + selfhost fixed-point ×2) and repinned.

- [x] **ICEs terminate with no message printed** **[verified]**
  `compiler/src/compiler/compilation_context.cryo:566-572` — `ice()` emits
  E0900 into the deferred sink then `process_exit(1)` **without flushing**
  (`diag/sink.cryo:111-114` defers rendering until `flush()`). Every ICE is a
  silent exit-1.

- [x] **Array element compatibility falls back to TypeKind equality**
  `compiler/src/compiler/types/checker.cryo:187-194` — if element TypeRefs
  differ, elements are "ok" when their *kinds* match: `StructA[]` is
  `Compatible` with `StructB[]`. Silent layout mismatch. Related holes at
  :202-221 (`int[] -> char[]`, `Array -> string` as `ImplicitConvert`).

- [x] **Width-blind `Int <-> Char` implicit conversion (both directions)**
  `types/checker.cryo:470-475` — `i64 -> char` silently truncates outside the
  call-arg lossy-narrowing gate.

- [x] **No branch-type agreement checks in if/ternary/match expressions**
  `sema/sema.cryo:1750-1766` (if/ternary return then-type or else-type with no
  compatibility check); `sema.cryo:1808-1810` (match returns `outer_expected`
  or the first arm's tail type; comment at :1866 admits arm agreement is
  assumed, never verified).

- [x] **Tuple drop glue is a silent no-op — tuples of owned values leak**
  `codegen/ops/call_emitter.cryo:1280-1284` (documented in-code).

---

## Tier 1 — Real bugs, smaller blast radius

### Compiler

- [x] **`KwVaList` missing from `TokenType::is_keyword()`** (table drift)
  `lex/_module.cryo:431-487` lacks it; `from_keyword` (:875) and `to_string`
  (:706) have it. Every parser path gated on `is_keyword()`
  (keyword-as-member/param/path-segment) treats `va_list` inconsistently.

- [x] **Lexer column double-increment after embedded newlines**
  `lex/lexer.cryo:336-339, 396-399, 561-564` — `lex_string`/`lex_fstring`/
  `lex_comment`/`lex_doc_comment` call `newline()` then `advance()`, which
  re-increments column to 2. `skip_whitespace` (:775-785) already bypasses
  `advance()` specifically to avoid this — pattern known, fixed in 1 of 5 sites.

- [x] **`make_token` span math wrong for tabs and multi-line tokens**
  `lex/lexer.cryo:819-826` — column = current column − lexeme length; wrong
  for tokens containing tabs; multi-line block/doc comments get the *end*
  line plus a nonsense column (feeds doc-cluster gap logic at
  `parser/parser_base.cryo:748-756`).

- [x] **Generic-call lookahead misparse corner**
  `parser/expr_parser.cryo:2242-2319` — `f(a < b, c > (d))` misparses as a
  generic call. Follower-token list has grown by bugfix ("GAP 1a" comments).

- [x] **Untyped lambda `(x) -> ...` produces the wrong diagnostic**
  Falls into the `->`-is-not-an-operator error path instead of a
  "lambda params need types" message.

- [x] **Parallel emit failures discard the LLVM error string**
  `codegen/passes.cryo:1917`.

- [ ] **`static match` arm pruning compares raw `TypeRef.id`**
  `mono/ast_resolver.cryo:445-452` — while the codebase elsewhere documents
  that TypeID identity ≠ logical identity across the mono boundary
  (`mono/call_specializer.cryo:1630-1632`).

- [ ] **Mono fixpoint gaps**: convergence metric misses method-spec-only
  progress; spec bodies walked once, not to fixpoint
  (`call_specializer.cryo:1303-1307`). Dead `in_progress` cycle detection can
  never fire (`monomorphizer.cryo:80, 266-271`).

- [x] **Ternary/if-expr phi handling lacks the terminated-arm and coercion
  handling the match-expr path has**
  `codegen/ir_generator.cryo:1808-1842` vs `:1739-1773`.

- [x] **Raw `void*` LLVMTypeRef `==` comparisons at three coercion gates**
  contradict a comment claiming `==` on them lowers to strcmp
  (`ir_generator.cryo:2018-2021` vs `ops/expr_ops.cryo:869, 2141`,
  `call_emitter.cryo:1139`). Determine which is true; make it one way.

- [x] **Loop-resident dynamic allocas at ~10 spill sites** — unbounded stack
  growth in hot loops.

- [ ] **DirectPair expansion triggered by an arity-mismatch heuristic**
  `ops/expr_ops.cryo:653-664`, whose reduced inline coercion loop differs
  from the main path.

- [x] **`@panic` discards its message** — every unwrap failure is a mute
  SIGABRT (`intrinsic_emitter.cryo:1073-1115`, documented TODO).

### Incremental compilation (stale-`.o` holes)

- [x] **Injected imports invisible to the leaf closure** — prelude/f-string
  modules force-discovered with no dependency edge back to consumers
  (`instance.cryo:1153-1224`, `passes/pass_registry.cryo:1047-1058`). A
  prelude-reachable layout change leaves a consumer's key unchanged while its
  `.o` has baked-in field offsets. Most likely future "impossible bug."

- [x] **Unresolvable dependency names silently dropped from the closure**
  `module_graph.cryo:427-431` — a missing invalidation edge, not a
  conservative one. Note: the same skip is accidentally why import cycles
  work (`module_graph.cryo:351-353` + `module_loader.cryo:660-668`) — cycles
  survive iff the back-edge fails to resolve. Make cycle support intentional.

- [x] **64-bit FNV cache keys** vs rustc's 128-bit SipHash — collision =
  silent miscompile. Consider widening.

### Stdlib

- [x] **`LineWriter` short-write data loss** **[found by direct read]**
  `stdlib/io/buf.cryo:227-236` — `write_some(head)?` discards the returned
  count and assumes the full head was accepted. BufWriter's large-chunk
  bypass (`buf.cryo:154-157`) forwards to the raw sink which may legitimately
  short-write; LineWriter then reports `head_len + n` consumed, silently
  dropping the unwritten tail. Needs a `write`-style loop on the head.

- [x] **Hash-path width inconsistency** — `Hash for u16/u32` feed **8 bytes**
  (`core/hash.cryo:176-188`) but `DefaultHasher::fold` uses natural width
  (:87-98). `digest(&x)` and `h.fold(x)` differ for the same u16.

- [x] **Stale doc contradiction in `core/cmp.cryo:4-6`** — claims "no operator
  overloading" directly contradicted by `core/ops.cryo:47-90` and cmp's own
  `is_lt` docs (:31-34).

- [x] **`fs::File::read` copies 4096-byte chunks byte-by-byte via `push`**
  `fs/file.cryo:243-249` — `try_append` exists; use it.

- [x] **Cosmetic**: `collections/string.cryo:181` — `clear()` jammed on the
  same line as `try_push_byte`'s closing brace.

### Tooling / infra

- [x] **Bindgen LLP64 bug: C `long` → i64/u64 unconditionally**
  `compiler/src/compiler/bindgen/type_map.cryo:44-45` — on Windows `long` is
  32-bit; any C API trafficking in bare `long` gets wrong-width fields/params.
  Masked so far because SDL2/raylib/sqlite mostly use `int`/fixed-width.

- [x] **`install.ps1` non-atomic swap** — `Remove-Item -Recurse -Force $Prefix`
  runs *before* `Move-Item` (~line 143); a failed move destroys the previous
  install. The bash installer stages + atomically swaps (`install.sh:243-247`);
  mirror that.

- [ ] **Bindgen flat namespace drops colliding symbols** — C++ overloads and
  cross-namespace leaf-name collisions keep only the first symbol
  (`bindgen/generator.cryo:119-144`). Reported but not disambiguated; rename
  instead.

- [x] **LSP body read is 1 byte per syscall**
  `tools/CryoLSP/src/protocol/framing.cryo:159-167` — the poll-based-debounce
  justification (:19-23) only holds for headers; once Content-Length is known
  a bulk `read_exact` is safe. A 100 KB didChange ≈ 100k syscalls.

- [x] **CryoAnalyzer has no version handshake with the server binary**
  `tools/CryoAnalyzer/src/config.ts:59-150` — 7-location auto-probe can
  silently launch a stale `cryolsp`. Also `serverInfo.version = "0.1.0"`
  (`handlers/lifecycle.cryo:177`) contradicts package.json `1.0.0`.

- [x] **LSP `::` completion is still a lexical text-scrape** while `.` is
  semantic (`handlers/completion.cryo:15-18`) — the known "works sometimes"
  flakiness root cause (AUDIT F1).

---

## Tier 2 — Structural debt (decide: pre-1.0 vs post-1.0 architecture work)

These are the highest-leverage investments. Multiple reviewers independently
named **D1** as the single change that would eliminate the majority of the
hazard classes above.

- [x] **D1. Canonical type identity.** TypeArena is TyCtxt-shaped but without
  the property that makes TyCtxt work: interning such that ID equality =
  semantic equality. Today ≥6 compensating mechanisms exist —
  `propagate_instantiated_resolution`, `canon_type_id`,
  `find_resolved_instantiation`, `canonical_resolved`, `display_equal`, and
  `find_inst_wrapping` (a **full linear arena scan** on hot paths:
  `types/arena.cryo:1556`, used by `inference.cryo:180`,
  `trait_checker.cryo:157, :294`). `check_compatibility`, `display_equal`,
  and `unify` implement **three subtly different equality relations**.
  Dedup caches keyed by `format()`-built strings while comments admit
  string-keyed HashMap equality has been pointer-based
  (`arena.cryo:66-69` vs `:573` — contradictory comments).
  *Scope: likely coupled to the planned mono-after-sema reorder.*

  **DONE.** A real hash-cons interner (`types/arena.cryo`) makes identity
  exactly `(kind, aux0, aux1, operand ids)`, with `entry_matches` as the single
  formal authority; each entry stores an IMMUTABLE copy of its key, so
  in-place node mutation (`swap_wrapper_to_concrete`, `populate_concrete_type`)
  can't fork identity. All six compensating mechanisms are deleted, every dedup
  cache is integer-keyed, and `swap_wrapper_to_concrete` collapses an
  instantiation into its concrete aggregate at the SAME arena id
  (rustc's `Adt(AdtDef, SubstsRef)` shape).

  Two corrections to the audit text above, both verified:
  * "Three equality relations" was a miscount. What remains is one *identity*
    relation (arena id), one *coercion/subtyping* relation
    (`check_compatibility`, returning a lattice), and one *unification*
    relation (`unify`). That is the same three-way split rustc draws
    (`eq` / `sub`+coerce / `unify`) and is correct as-is.
  * The function-template wrapper and its `FunctionType` are deliberately NOT
    unified — they are `FnDef(DefId, Substs)` vs `FnPtr(FnSig)`, two different
    semantic types bridged by a coercion. Sharing an id is arithmetically
    unavailable, not unfinished work.

  `ErrorType` is the one kind excluded from interning (it carries a per-failure
  reason + span, and never reaches an identity comparison); documented at
  `create_error` as deliberate.

  Guarded by `verify_canonical_identity()`, wired to the `CRYO_TYPE_AUDIT` env
  gate before codegen (off by default, ICE on violation). Observed reporting
  **0 violations** across the full test suite; both failure and success paths
  exercised. `make test` green, `make selfhost-check` green (2/2 fixed points).

- [x] **D2. Single authority for enum/aggregate layout.** Layout computed in
  4 places (one divergent = Tier-0 bug #1). One `compute_enum_layout` used by
  construction, pattern match, drop glue, and size queries.

- [ ] **D3. Name-keyed semantic tables are a recurring collision-bug
  generator.** Last-write-wins single-slot maps (`func_returns`,
  `func_type_refs` — documented poison at `decl_index.cryo:622-627`,
  `call_resolver.cryo:135-146`), bare-leaf trait-impl keys packed into u64
  (`generic_registry.cryo:516`) patched with a second "precise leaf" index,
  `format("%s::%s")` keys, string-surgery `spec_base_name`
  (`generic_registry.cryo:80-133`). Every collision class so far was patched
  reactively; more exist. Long-term fix: DefId-style symbol identity.

- [ ] **D4. CallEmitter name-string fallback tower.** ~900-line ordered
  fallback resolution in codegen (`call_emitter.cryo:129-884`): sema-pinned
  mangle → `$MG` reconciliation → spec-name → combined-key → namespace walk →
  bare-name+arity → bare-name → enum-variant synthesis, over last-write-wins
  `(name, arity)` registries. Comments document a shipped wrong-function bug
  of exactly this class (`try_push<Str>` → `try_push<i32>`, :807-830) and
  admit pins go stale post-substitution (:843-845). Goal: sema pin is the
  *only* path; everything else becomes an ICE.

- [x] **D5. Mono instantiation identity: three parallel string keying
  schemes** (registry raw-id key `generic_registry.cryo:901-907`, canonical
  `make_key` `monomorphizer.cryo:752-760`, display-name mangles).
  `make_key`'s own doc (:734-747) admits canonical-vs-structural keying is
  time-dependent and unsound; dedup currently works partly *by linker*
  (`linkonce_odr`).

- [ ] **D6. Silent-null / silent-skip error model in codegen.** `LBuilder`
  returns `LValue::null()` on any null operand (dozens of sites,
  `llvm_types.cryo:457-792`), backstopped by a pre-verify corruption scan
  (`passes.cryo:392-430`) and a cascade-strip pass (`passes.cryo:1522-1557`);
  ~40 bare soft-returns in `call_specializer.cryo` alone. Errors surface
  modules away from cause as generic E0633/E0636. Goal: convert soft-skips to
  ICEs incrementally (one comment already nominates a candidate,
  `call_specializer.cryo:536-540`).

- [ ] **D7. The inference story is scattered.** One mutable
  `state.expected_type` field manually saved/restored at dozens of sites (one
  missed restore corrupts downstream typing); literal typing by lexeme-shape
  heuristics (`sema.cryo:1404-1514`), no `{integer}`-style deferred literal
  types; the principled `InferCtx::unify` (`inference.cryo:94-212`) has **no
  Tuple, Array, or Optional arms**; three admitted inference copies awaiting
  the pipeline reorder (`call_resolver.cryo:19-24`).

- [ ] **D8. Hand-maintained parallel tables with proven drift.** TokenType ×4
  tables (KwVaList bug is the smoking gun), NodeKind predicates ×3, AST
  visitor/cloner/substituter/dumper/node_locator ×5-way sync per new field,
  LLVM constants ×3 surfaces (header enums, Cryo mirrors, inline magic
  numbers — `op == 45` at `passes.cryo:1438`, opcode 27 sniffing ×~5 in
  `expr_ops.cryo`), pass identity ×3 drifted encodings (for
  `DirectiveProcessing`: `stage()` says Specialization `pass_id.cryo:125`,
  `metadata()` says SemanticAnalysis/order 12 `:355-363`, `order()` says 14
  `:158`; `run_until` compares the wrong one). Any codegen/derive facility —
  even a Python script emitting Cryo — pays for itself immediately.

- [ ] **D9. Ceremonial pass manager.** `ProvisionSet` bitsets only feed a
  linear order-validation assert (`pass_registry.cryo:91-146`); nothing
  schedules from dependencies; driver hand-marks provisions in ten
  near-identical blocks (`instance.cryo:2459-2737`);
  `compile_project_with_ctx` is a ~1,500-line ordering-sensitive monolith
  (`instance.cryo:884-2392`). `pass_id.cryo:159-162` still declares
  Monomorphization(15) before FunctionBodyTypeCheck(18) though the driver
  runs sema→mono→re-sema.

- [ ] **D10. Frontend `size_bytes()` vs LLVM DataLayout** — layout agreement
  by discipline, one recorded historical divergence
  (`codegen/type_map.cryo:550-557`). By-value `this` bypasses ABI
  classification entirely (`abi.cryo:359-369`, admitted in comment). No SysV
  MEMORY demotion for packed/misaligned aggregates ≤16 bytes.

- [ ] **D11. Duplication hotspots** (each a divergence risk): integer
  width-coercion block ×~9, class field-flattening ×3, InstantiatedType
  unwrap ×~15 with inconsistent depth caps (5/8/16),
  `classify_param`/`classify_return` near-twins, `populate_*_methods` ×3
  verbatim (`type_populator.cryo:116-234`), method-mangle computed twice with
  a "must mirror exactly" comment (`declaration_emitter.cryo:943-951` vs
  `:1945-1951`), `parse_type_body` triple-nullable dispatch ×6
  (`parser.cryo:1060-1180`), `parse_numeric_lexeme_i64`/
  `numeric_lexeme_overflows_u64` ~50 duplicated lines
  (`parser_base.cryo:426-518`).

- [x] **D12. Dead/vestigial code**: `TypeAnnotation::Optional` never
  constructed but matched in ~12 files; `SourceRange`
  (`lex/_module.cryo:1069-1086`); `parse_match_arm_var_decl`
  (`expr_parser.cryo:1794`); `parse_lambda_block_body` degraded duplicate
  (`expr_parser.cryo:1295`); debug leftover `cdebug("[PARSE-DBG] ...")`
  (`parser.cryo:165`); `generate_bodies` no-op
  (`declaration_emitter.cryo:1402-1422`); stale 40-line superseded rationale
  (`type_map.cryo:23-41`).

---

## Tier 3 — Test & validation gaps

- [x] **Snapshot the test roster.** CI never asserts a test *count*; a
  compiler change that silently drops `![test]` discovery stays green.
  Cheapest high-value fix: golden-file the `cryo test --list` output.

- [x] **Known-failing canary test.** The test framework is compiled by the
  compiler under test; a miscompile of `expect_eq` or failure-propagation
  converts failures into passes. One tiny test CI asserts *fails* closes the
  trust loop.

- [ ] **Diagnostics are pinned by error code only.** No message text, no
  span/line assertions, no stderr goldens (rustc `//~ ERROR` model). Wrong
  spans / garbled messages / cascades regress silently. Coverage: **63 of 217
  E-codes (~29%) have negative tests; 0 of 12 W-codes.**

- [ ] **Run the real suite under ASAN/valgrind** (at least nightly).
  Currently only 4 toy examples get valgrind. Given drop-glue history, a
  drop-path miscompile that doesn't crash the test binary ships.

- [ ] **No compiler-internal unit tests at all** — sema/mono/parser
  correctness is inferred entirely from e2e + the fixed point (which proves
  *determinism*, not *correctness*: a deterministic miscompile passes).

- [ ] **No lexer/parser fuzzing** (the 8 recursion guards suggest past manual
  fuzzing; automate it).

- [ ] **LSP has zero automated tests** — 10.9k lines validated by hand-scripted
  stdio only, despite a documented UAF/leak history and a discipline-enforced
  global-arena invariant (`session.cryo:404-417`) where one forgotten
  `ensure_fresh` call-site reintroduces a use-after-free.

- [x] **Unknown `requires` strings in project tests default to *run***
  (`CLI/commands.cryo:1753`) — a typo'd requirement silently changes
  semantics from "gated" to "always-on". Fail on unknown tokens.

- [x] **`make test` does not rebuild the compiler** when
  `compiler/build/cryo` exists (`Makefile:375-378`) — stale-binary gating
  footgun; consider a fingerprint check.

- [x] **No `.gitattributes`** despite mixed-OS development with
  `autocrlf=true` — phantom-modified files; bad for a repo whose gates
  depend on byte identity.

- [x] **Doc/fixture disagreement on compile-fail spelling** —
  `docs/testing.md` §7 documents `compile_fail`;
  `tests/tests/projects/compile_fail_typeerror/test.json` uses
  `"outcome": "build"` (alias exists at `commands.cryo:1769-1780`). Pick a
  canonical spelling.

- [x] **`tests/tests/negative/README.md`** claims some negative files
  "intentionally fail today" — incompatible with `make test` as a required
  gate; either stale or a policy conflict.

- [ ] **Committed pinned binaries grow git without bound** — 354 pin
  refreshes; pack at ~113.6 MiB. Fine for 1.0; plan an LFS/snapshot-download
  strategy before year 3 forces a history rewrite. Note: reproducibility is
  IR-level, not binary-level (sidecar sha256 is machine-of-origin-specific).

---

## Tier 4 — Language design & documentation (pre-freeze decisions)

### Spec holes on load-bearing features

- [x] **`static match` has no section in the language reference.** Two passing
  mentions in `docs/cryo.md` (lines ~1336, ~2585); no section, no TOC entry —
  despite 30+ stdlib uses and powering `Atomic<T>`, `String::push<T>`,
  `io::Write::write<T>`. `docs/grammar.md:305-311` defines `StaticMatch` but
  never references it from `Statement`/`Primary` — an orphan production.
  **Single worst doc gap in the project.**
- [x] **`Send`/`Sync` semantics unspecified** — only module-table one-liners
  (cryo.md:2585, README:532). What derives, what enforces, what `!Send`
  means: undefined, for a language shipping threads+channels at 1.0. Note
  also the implementation stance: raw pointers and references are
  unconditionally Send+Sync with a hardcoded 4-entry deny-list
  (`types/ownership.cryo:503-513`) — document that `Send` is advisory.
- [x] **`->` contradiction**: cryo.md §5.6 + precedence table list
  `p->field`; `grammar.md:234` says "there is no `->` operator."
- [x] **`![derive(Trait, ...)]` listed (§17.1) with no list of derivable
  traits anywhere.**
- [x] **Tuple story inconsistency**: cryo.md §2.6 documents heterogeneous
  tuple literals fully; CHANGELOG.md:151 says "Cryo has no heterogeneous
  tuple literal" (re `.zip`/`.enumerate` using `Pair`). One is stale.
- [x] **Closure capability boundary scattered across three documents**
  (capturing closures advertised in §2.5; E0458 non-capturing iterator limit
  only in CHANGELOG "Known limitations").
- [x] **`abi.md` covers SysV x86-64 only** — Win64 ships but is undocumented.
- [x] **grammar.md drift**: `UnionDecl` defined but absent from
  `TopLevelItem`; self-demoting ("the parser is the source of truth").

### Design debt to decide on before freeze

- [ ] **`![sink]` falsifies receiver signatures** — `unwrap(&this)` etc. in
  `stdlib/core/option.cryo:58-99` are syntactically borrowing, semantically
  consuming; contradicts cryo.md §8.3's "a caller knows whether a method
  borrows or consumes without reading the body." Exists to patch match-arm
  payload double-frees (absence of by-value enum receivers). Decide: fix the
  mechanism, or document the attribute as first-class.
- [ ] **Auto-drop is partial and every example shows it** — §16.2: no
  coverage of match-arm pattern bindings or field/index access. Examples are
  littered with manual `.drop()` ladders on every error path
  (`examples/10-expr-interpreter`: 8× `left.drop(); return Result::Err(e);`).
- [ ] **Frozen speculative/no-op surface**: pipeline operators `|>`/`<|`
  (zero uses outside parser + one test), `unsafe` as a committed permanent
  no-op (used once in the entire tree), `switch` redundant with `match`,
  parse-only struct field defaults (E0355), parse-only `async`/`await`/
  `yield`. §21's candor is exemplary; freezing unused syntax into 1.0 semver
  is still debt.
- [ ] **Documented literal-widening trap** (§1.4): integer literals over
  `i64::MAX` wrap negative against `u64` operands — shipped as a "Trap"
  rather than fixed.
- [ ] **No checked/wrapping/saturating arithmetic opt-in** — frozen
  wrapping-only contract (`core/primitives.cryo:11-18`); consequences leak
  (`Duration::scale`/`as_millis` wrap silently, `abs` footgun). Even a
  `checked_*` family without a trapping mode would close most of it.
- [ ] **Examples/idiom drift** — flagship examples don't use flagship
  ergonomics (`?`, f-strings, for-in, auto-drop); they teach
  `intrinsics::printf` + manual-drop Cryo. Modernize before launch: the
  language as documented is nicer than the language as demonstrated.
- [ ] **Naming/surface idiosyncrasies to consciously affirm or fix**:
  primitive `string` (NUL-terminated `u8*`) owns the best name while
  `Str`/`String` are the safe types; `boolean` not `bool`; `function` not
  `fn`; `char` is a byte. Enum methods only via `implement` blocks while
  structs/unions declare inline (asymmetry, §10.4).

### Stdlib gaps (ranked by user pain)

- [x] No Unicode scalar iterator on `Str` (`chars()`); text handling is
  byte/ASCII-only (trim/case/split all documented byte-level).
- [x] No format specifiers in f-strings (width/precision/fill/hex) —
  `fmt/interp.cryo` supports `{expr}` / `{expr:?}` only.
- [x] `fmt::printf` is type-unchecked UB in the most-used API family
  (`fmt/_module.cryo:53-56` admits it); two parallel stdout paths
  (printf-family vs Display/`io::stdio` lock story).
- [ ] Collections variety: no sorted map/BTree, no Deque, no `binary_search`,
  iterator adapters missing `min/max/sum/rev/skip`; `from_iter` free-fn only
  (documented mono-divergence reason, `collections/array.cryo:562-569`).
- [ ] No `io::Seek` trait (File has inherent seek methods) and no in-memory
  Cursor; no URL parser (http client takes SocketAddr + path only); no
  calendar/date formatting; no `env::vars()` enumeration; no Weak refs
  (Rc/Arc leak cycles — documented); no mutex poisoning (documented);
  `Path::extension`/`join` deferred (documented).
- [x] **`fs/path.cryo` is POSIX-semantics-only** — `C:\...` doesn't split
  correctly; the weakest cross-platform link in an otherwise Windows-serious
  stdlib.
- [ ] **ABI-by-offset brittleness**: `struct stat` at hardcoded glibc-x86_64
  offsets (`fs/metadata.cryo:34-37`), dirent `d_type`@18/`d_name`@19
  (`fs/dir.cryo:31-45`), pthread objects as magic buffer sizes 40/48/56
  (`sync/mutex.cryo:96-99`, `sync/mpsc.cryo:118-122`). All documented, all
  one-arch; any new target is silent corruption. (The libc-weaning plan
  presumably subsumes this.)
- [x] `ffi/libc.cryo:992-1000` — Windows `readlink` special-cases the literal
  `"/proc/self/exe"`, breaking the module's "mirrors headers verbatim"
  contract; `env::current_exe` depends on the magic string.
- [x] `math/` type-suffixed free functions (`min_f32`, `clamp_i64` —
  `math/_module.cryo:196-251`) against the codebase's own no-suffix
  direction (blocked on a generic-codegen limitation; note the blocker).
- [x] `core/error.cryo` — 22-line catch-all with raw `string` message;
  vestigial next to per-module error types. Keep or delete deliberately.

### LSP / tooling roadmap

- [ ] **Missing table-stakes LSP features**: references, rename,
  documentSymbol, workspaceSymbol, signatureHelp, inlay hints, folding —
  all absent (`handlers/lifecycle.cryo:113-181`). `workspace/symbol` hurts
  most in a 100+-file self-hosted repo. documentSymbol + references are the
  highest-value next two.
- [ ] **No incrementality, no cancellation, single-threaded** — whole-project
  recompile per edit burst (~10-30 s cold per session.cryo:64-65 comment); a
  long compile blocks every request including `shutdown`
  (`server.cryo:296-303`; `$/cancelRequest` is a no-op). Even
  serve-stale-while-recompiling would fix the UX cliff.
- [ ] **Semantic tokens: full-document only**, O(n²) insertion sort
  (`semantic_tokens.cryo:1090-1097`).
- [ ] **Non-ASCII position desync** — utf-8 negotiated when offered, but the
  UTF-16 fallback treats byte offsets as UTF-16 units
  (`server/line_index.cryo:9-13`, documented TODO).
- [ ] **CryoFormat is a dead-end prototype** (Rust, parses ~10% of the
  language, deletes comments by construction — `parser.rs:78-79`; AST-reprint
  architecture wrong for a formatter). Decide: kill it or rebuild
  token/trivia-preserving in Cryo. Shipping 1.0 with no `cryo fmt` is a
  visible gap (gofmt/`zig fmt` shipped at/before their 1.0s).
- [ ] **LSP link overlays hand-mirror the compiler's** (libclang/LLVM) —
  documented sync trap in `tools/CryoLSP/cryoconfig`.

---

## Strengths inventory (what NOT to break)

For calibration — the things reviewers rated at or above production quality:

1. **Bootstrap discipline**: per-PR byte-identical stage-3=stage-4 IR fixed
   point on both OSes (`scripts/selfhost-check.py`, 822 lines, incl. per-module
   IR-tree compare keyed by relative path); `incremental-check.py`'s
   deliberate line-shift edit; sha256 pin sidecars + `verify-pin` in every CI
   job + `--require-clean` release gate. Stricter than rustc/Go/Zig per-PR
   practice.
2. **ABI classifier** (`codegen/abi.cryo`): plan-based recursive SysV
   eightbyte classification incl. buried-float SSE and `<2 x float>`, separate
   Win64 path, typed sret/byval symmetry, per-ABI va_list shapes.
   Near-clang-quality for common cases.
3. **Parallel codegen architecture**: context-per-module, emit-only
   concurrency, cross-context auditor retained as invariant check
   (`ctx_audit_module`), allocation-free workers, structurally-justified
   byte-identical determinism.
4. **Ownership pass pair**: flow-sensitive MoveCheck + DropInsertion with
   synthesized drop flags, snapshot branch joins, loop-carried detection,
   consistent leak-over-double-free bias. Pre-MIR-rustc drop-flag level.
5. **Diagnostics subsystem**: Applicability-graded machine-applicable fixits,
   multipart atomic edits, Damerau-Levenshtein did-you-mean at rustc's
   threshold, speculative suppression scopes, errors-last deferred flush,
   fixit-withholding when a fix would harm (`lexer.cryo:506-521`).
6. **Bindgen** (A-): skipped/approximated/ignored honesty channel; bitfield
   storage-unit spanning with accessors; layout-faithful blobs; va_list
   runtime tests; provenance-keyed per-triple caches with versioned changelog.
7. **Stdlib conventions**: try_ pairs, allocator-generic containers
   (`*_in` ctors — ahead of stable Rust), Copy-gated by-value vs `_ref`
   borrow APIs, `describe() -> Str` convergence, seeded hashmaps, overflow-
   checked growth, unbiased RNG, canonical Arc ordering protocol with written
   rationale.
8. **Test framework** (`stdlib/test/`): fork/CreateProcess isolation,
   watchdog, output capture, parallel live feed with stall reporting,
   negative + multi-module project tests driven natively by `cryo test`.
9. **LSP memory architecture**: per-session bump arenas with bulk reclaim,
   LRU-of-3 live contexts, compiler genuinely adapted for daemon use
   (`CompileMode::Lsp`, buffer-override project compiles, LSP-safe mangler
   poison sentinels).
10. **Documentation honesty**: §21 reserved-syntax table, "Trap" callouts,
    as-built `abi.md`, normative mangling spec, `tools/CryoLSP/AUDIT.md`
    (372-line severity-ranked self-audit with verified-landed fixes), and
    postmortem-grade comments throughout — the codebase is its own bug
    database.

---

## Suggested attack order

1. **Tier 0** in order listed — enum drop-glue offsets first (small,
   verified, high blast radius), then the move-set divergence, the mute ICE,
   the checker holes. Each needs a regression test in `tests/tests/lang/`.
2. **Cheap Tier-3 wins alongside**: test-roster snapshot, known-fail canary,
   `.gitattributes`, unknown-`requires` hard error. Each is <1 day.
3. **Tier 1 compiler bugs** as encountered; the two lexer span items and
   `KwVaList` are mechanical.
4. **Decide the Tier-4 freeze questions once**, in writing (this file), so
   1.0 scope stops moving: sink/auto-drop stance, no-op surface (`unsafe`,
   pipes, `switch`), checked arithmetic, `string` naming.
5. **Tier 2 (D1 canonical identity + mono-after-sema)** is the big rock —
   schedule it deliberately (pre- or post-1.0), don't let it happen by
   accretion. D2 (layout authority) falls out of the Tier-0 fix if done
   right. D8 (table codegen) is cheap and pays forever.
