# Name resolution: what enforces each rule, and what does not

Input for the final read-only audit of the name-resolution migration on
`naming-impl`.  Measured on the tree of the commit carrying this file.
The rules are `docs/resolution-rules.md`'s five; the door list and the
rulings appendix (through 226) are in the same file.  Every pointer below is
a path in the tree; every number was taken from a run named beside it.

## Rule 1 - every written name is resolved once, where it is written

**Enforced by**

* The name layer stamps each node that carries a written name with a `Res`
  in a `ResSlot` (`compiler/src/compiler/resolver/res.cryo`).  A consumer
  reads it with `ResSlot::require(site)`, which treats an unanswered slot as
  an internal error, never as a cue to look the name up again
  (`require_scope_res` is the scope-path form; 17 reads).
* The name layer's exit check, `StampCheck`
  (`compiler/src/compiler/resolver/stamp_check.cryo`, run from
  `compiler/src/compiler/instance.cryo`), refuses a build in which any
  written name was left unanswered.
* Sema does not re-resolve: a static call through a generic segment
  (`Wrap::<i32>::make()`) asks the instance built from the stamped template
  and the written arguments (`CallResolver::scope_owner_type`); the name
  minted from the template's spelling that it used to be asked by is
  deleted (8.591).

**Tests**

* `tests/tests/projects/stamp_check_refuses_unanswered_name` - the exit
  check refuses a build whose first identifier the test switch resets to
  unanswered.
* The resolution corpus below (rule 5) exercises the stamps' answers.

**Knowingly out of scope**

* Not a mechanical proof that no pass re-asks by text: rules 3 and 4 are
  what refuse that, over the compiler's own modules.

## Rule 2 - stores are keyed by identity

**Enforced by**

* The store-key lint, `DirectiveProcessingPasses::lint_store_keys`
  (`compiler/src/compiler/passes/directive_processing.cryo`): a field of a
  compiler module whose hash table is keyed by text or by a raw number is
  E0157 unless the field's own allow says why.
* Visibility locks: eight stores keep their name fields `private` to the
  module that holds the store and its door, so a by-hand lookup elsewhere
  is a private access (E0353).  The table of locked stores is in
  `docs/resolution-rules.md` ("Rule three is also held by visibility").
* `DefTable::slot` refuses a definition id made by another table.
* The lane gate (`scripts/lane-gate.py`, `make lane-check`) pins how many
  calls reach each store by which kind of key; a rise is refused.
* A specialization is registered and found by its template and type
  arguments (instance arena slot, definition beside the template, methods
  by the concrete type, specializer index by instance); its identifier is a
  name only (8.591).

**Tests**

* `tests/tests/projects/store_key_lint_refuses_raw_key`.
* `make lane-check` (in `make check-fast`) with its self-test.

**Knowingly out of scope**

* Stores outside the compiler's own modules (the standard library, user
  programs): the lint runs over the compiler's modules only.

## Rule 3 - a spelling reaches a lookup only through a door

**Enforced by**

* The door list: 28 door functions of 15 kinds, each marked by a
  ``/// Door `<id>`.`` doc comment; the ids are a fixed table
  (`DirectiveProcessingPasses::door_ids`) and a marker naming any other id
  is E0157.  `scripts/resolution-doors.py` (`make doors-check`) refuses the
  tree when the table in `docs/resolution-rules.md` and the markers
  disagree in either direction.
* `SpellingLookupCheck` (`compiler/src/compiler/sema/spelling_lookup.cryo`),
  over every function of the compiler's own modules:
  * a parameter of a spelling type (`SymbolStr`, `QualifiedName`) followed
    through the body and refused (E0157) at a hash table's key, a
    comparison, a `boolean`/`Option` call that compares it, or a `match`'s
    subject;
  * a spelling read off a field (a declaration's, a syntax node's, a table
    row's name), refused at the same sinks in every body;
  * a spelling handed on as plain text (`string`) followed across calls
    through the flow graph (`IdentityFlow`) and refused where it is looked
    up ("`f` looks up a spelling it is handed as plain text");
  * a comparison reading a local array before a spelling is written into
    it later in the body (`SpellingPending`).
* An allow (`![allow(lookup_by_spelling, reason = "...")]`) must carry a
  reason; in the compiler's modules a reason opening `pending:` is refused.
  The compiler has 0 pending allows (locked).
* Module discovery's written paths are spellings, and the `loader-path`
  door alone turns one into a file (8.590); an import's written path is a
  spelling on the syntax node, and whether it names a module the compiler
  imports on a program's behalf is the `lang-item` door
  `LangModule::is_written` (8.592).

**Tests**

* `spelling_lint_refuses_lookup`, `spelling_lint_follows_text_and_numbers`,
  `spelling_lint_follows_plain_text_and_loops`,
  `spelling_lint_allows_with_reason`, `spelling_lint_directive_shapes`,
  `spelling_lint_empty_reason`, `spelling_lint_missing_reason`,
  `spelling_lint_placeholder_reason`, `door_marker_names_no_door`
  (all under `tests/tests/projects`).
* `make doors-check` and its self-test; `make spelling-flow-selftest`.

**Knowingly out of scope**

* The LSP (`tools/CryoLSP`): its by-name lookups are fixed on their own
  branch after the merge (ruling 153: the editor's by-name lookups are
  post-merge work); 41 of its allows still say `pending:`.
* The standard library and user programs: rule 3 is the compiler's rule
  about itself.

## Rule 4 - identities are opaque

**Enforced by**

* `IdentityTextCheck` (`compiler/src/compiler/sema/identity_text.cryo`) with
  the cross-function flow `IdentityFlow`
  (`compiler/src/compiler/sema/identity_flow.cryo`).  Identity text is a
  `DeclName` (a definition's name read off it) or a `MangledName` (a link
  symbol), and anything computed from either: concatenated, formatted,
  cast, indexed, a field read off it, a call handed it that returns text,
  a local holding it.  It is refused (E0157) as a door's argument, a hash
  table's key, an operand of `==`/`!=` with other text, a text handed with
  other text to a call answering a `boolean`, or a `match`'s subject - in
  the function that reads it, or wherever it reaches through any chain of
  calls.
* Text that comes back off identities is sealed at the source: a type's
  display (`TypeArena::format_display` shows the declaration's `DeclName`),
  a specialization's display (`custom_display` is a `DeclName`, 8.592), a
  function's link symbol (`DeclarationIndex::entry_symbol` is a `DeclName`),
  every mangled symbol (`MangledName` is followed since 8.592).

**Tests**

* `identity_text_refused_at_door`, `identity_text_refused_across_calls`,
  `identity_text_refused_as_match_or_question`,
  `identity_text_refused_through_mangled_name` (`tests/tests/projects`).

**Knowingly out of scope / known holes**

* **A call through a base class.**  `t.display_name()` on a `Type*` names
  the base's method; what an override answers is not followed there.  A
  fix exists (`.objcmp/s135/override-flow.patch`: link each override's
  answer to the same-named base methods through the `member-function`
  door).  It finds real sites - it found the where-arg binder's compare,
  converted in 8.592 - but raises the lane gate's pinned `LOOKUP_OTHER`
  count by its one door call, so it waits on a decision.
* **`GenericRegistry::annotation_unifies`.**  Its allow's reason covers the
  primitive arm; its `Res::Def` arm compares a definition's path text with
  a template key.  The registry has no declaration index to compare
  definitions with.  Found this session, not converted.
* Text stored into a field and read back elsewhere is not followed (the
  flow ends at a field write), by design.
* The wording of a refused `MangledName` says "computed from a `DeclName`";
  a link symbol is spelled from one, but the label names the wrong type.

## Rule 5 - the answers are right

**Enforced by** a committed corpus of resolution shapes with expected
answers; no mechanical check.

**Tests** (`tests/tests/projects`): `resolution_scope`,
`resolution_tripwire`, `resolution_leaf_index`, `resolution_module_suffix`,
`resolution_ambiguous_module`, `resolution_trait_default`,
`resolution_unreachable_module`, `qualified_type_selects_module`,
`qualified_path_beside_bare_import`, `module_qualified_static_call`,
`module_type_import_collision`, `import_*` (16 projects: aliases,
collisions, braced items, sub-modules, redeclaration), `trait_qualified_call_disambiguates`,
`impl_qualified_*`, `overload_qualified_*`, `visibility_import_gate`,
`unimported_module_path_refused`, `const_unimported_gate`,
`default_expansion_by_stamp`, among the 236 projects; `make test-census`
reconciles what ran against `tests/test-roster.txt`.

**Knowingly out of scope**

* The editor's answers: `scripts/ns-migration/8.377/editor_answers.py`
  (25 questions) is run by no gate; post-merge with the LSP.

## The cross-check (the Python spelling-flow list against the compiler)

Rerun on this tree (`.objcmp/s135/CROSSCHECK.md`, scripts in
`.objcmp/s135/xcheck/`, method as `.objcmp/s134/CROSSCHECK.md`): every
function allow (109) stripped from a copy of the compiler, the compiler
under test refuses 83 sites in 71 functions, all mapped.  Of the list's 353
entries (491 weighted):

| class | entries | weighted |
|---|---|---|
| refused by the compiler | 140 | 234 |
| not a lookup of a program's name (flags, configuration, text, fixed spellings, module identities, loader files) | 211 | 255 |
| allowed with a reason | 2 | 2 |
| **a lookup only the Python sees** | **0** | **0** |

Ruling 226: the Python list is deleted by the cleanup that removes the
Python gates, now that ruling 223 has landed.

## Gates at the commit carrying this file

`make verify ARGS="--baseline HEAD --require-identical"` OK (8,305 test
and 1,126 example objects, none moved, at 8.591 and 8.592); `make
check-fast` OK; `make roster-check` OK (2,714 entries: 2,226 unit, 236
project, 252 negative); clean build 324 warnings, the same set; pins
refreshed so the bootstrap compiler runs these checks.
