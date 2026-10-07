# Stores keyed by text, a path, a spelling or a display string - at `8ecf5ef0`

Re-measured by the 113th session at the tip `8ecf5ef0` (the audit in
`REPORT.md` is at `d52d84cc`, 34 commits earlier).  Method: every map field
in `compiler/src` (`grep` for `HashMap<`/`HashSet<` fields: 55 not typed by
`DefId`/`TypeRef`/`NodeId`; no map is declared as a local), every key
expression traced to its construction, plus the arrays the compiler scans by
a spelling.  Two read-only sub-agents traced the keys; every row marked
**(checked)** I re-read myself.  Paths are under `compiler/src/compiler/`.

Rules: **2** a declaration store's key comes from an identity or a door;
**4** text an identity hands back (path, leaf, display, mangled symbol)
never feeds a key.

## A. Breaks a rule - to convert

| # | store | key as built | rule | plan |
|---|---|---|---|---|
| 1 | `MonoState.method_specs` (`mono/state.cryo:149`) - a generic static method's instance on a generic owner | the **mangled symbol's text**: `mangled_symbol_for_spec_method(spec, arena.get_qualified_name(recv))` (`mono/call_specializer.cryo:1880`) **(checked)** | 2, 4 | ruling 125's shape: (owner `TypeRef`, the method's `DefId`, the method's type arguments), in the `method_instances` table that already keys a receiver method's instances that way. **Converted in this session (8.523).** |
| 2 | free-function instances: the pin `CalleePin::Family(Def(template), spec_sym)` (`call_specializer.cryo:718,760`) and the clone's registration in that family (`passes/type_resolution.cryo:1054`) | the template's `DefId` + the **minted identifier text** (`MangledName::specialized_identifier(template name, args)`), so the type arguments reach the key as text **(checked)** | 2, 4 | ruling 125: the instance is (template `DefId`, type arguments).  Pin the instantiation (`arena.create_instantiation(base, args)`, interned on exactly that pair) and read the clone's entry off the spec entry; plan in `HANDOFF.md`, 113th section. |
| 3 | module globals: parallel arrays `module_global_keys/_files/_declared_in` (`decl_index.cryo:153-155`), scanned by `global_slot` | write dedups on (leaf id, **source-file text**); reads match (leaf, `ModulePath`); `global_entry_of_def` asks with `defs.leaf_of(d)` (`compilation_context.cryo:689`) **(checked)**; `type_utils.cryo:157` asks with a written `member_name` | 2, 4 | a `def_globals` slot table keyed by `defs.slot(var.def)`, written at registration; readers ask by the stamp. |
| 4 | function families written from text: `decl_fn_family` / `minted_family` (`compilation_context.cryo:701-731`) | the owner is **parsed out of the name's text** (`QualifiedName::parse` -> `display_parent` -> `module_family_named`) **(checked)** | 2 (a write) | a clone's owner is its template's (`copy_of`) or its module's `DefId`, handed in by whoever mints it. Dissolves `module_family_named` (and `module_of_minted_parent`). |
| 5 | family readers through identity text: `call_resolver.cryo:773-774`, `:2118`; codegen `SymbolResolver::resolve_function` (`codegen/ops/symbol_resolver.cryo:271`) | `lookup_family_entries(Def(parent_of(d)), defs.leaf_of(d))` - the door asked with an identity's leaf **(checked :271)** | 4 | ask by the definition: `entry_of_def(d)` / `func_type_of_def(d)`. **Converted in 8.528**: the family read off the definition's own entry (`family_entries_of_def`), a type's constructors off `constructor_entries_of`, codegen by `entry_of_def`.  Codegen's three constructor `resolve_family` calls by the type's leaf remain (same store, not in this row's original three). |
| 6 | `GenericRegistry.coherence_index` (`types/generic_registry.cryo:334`) | `intern(coherence_key_for(..))` - text of the target's qualified name, the trait's annotation and the where-bounds' spellings (`passes/type_resolution.cryo:1975-2008`) | 2, 4 | a structural key: (trait `DefId`, target `TypeRef` with impl parameters positional, trait-argument `TypeRef`s, bound `DefId`s). |
| 7 | the arena's projection intern key, `TypeArena::create_assoc_projection` (`types/arena.cryo:651`) | `aux0 = member.id` - the associated type's **leaf** **(checked)**; the slot the `assoc-type` door answered is passed in and unused for the key | 2, 4 | key by the slot when the door answered one; the `-1` case (no trait, or the trait declares no such type: `types/resolver.cryo:475`) needs a decision - see question in the handoff. **Ruling 143 refuses the `-1` case where it is written (8.526); the key is (base, slot, trait) since 8.527.** |
| 8 | codegen `FunctionRegistry.by_name_index` (`codegen/state/function_registry.cryo:27`) | mostly the linker symbol; but `declaration_emitter.cryo:1253` and `:1324` register the bare source name and `:1259` the `decl_fn_key` qualified text **(checked)** | 4 (those 3 writes) | register only under the entry's symbol (the store then sits under ruling 126 with `GlobalRegistry`). |
| 9 | `overload_func_owner` (`decl_index.cryo:214`) - not a key, a dedup discriminator inside `register_signature` (`:440`) | the `decl_fn_key` qualified-name text of a clone | 4 (soft) | compare `copy_of` (a `DefId`); goes with #2/#4. |
| 10 | `GenericRegistry::find_inherent_impl_method` / `_generic_method` (`types/generic_registry.cryo:578,604`; callers `sema/method_binding.cryo:1163,1680`) - a scan, not a map | matches `m.func.name.equals(method_sym)` inside the owner's inherent blocks **(checked)** | 3 (a duplicate of the member-function door) | ask the member-function door, or by the method's `DefId`. |

## B. Identity carried as text - no rule broken, cheap to tidy

| store | key | plan |
|---|---|---|
| `SemaState.closure_spec_map` (`sema/state.cryo:132`) | `format("%lld", defs.slot(orig.def))` + `"|idx:typekey"` per argument (`sema/lambda_synth.cryo:663-666`): identities formatted into a string; the field's comment (`<orig_name_id>`) is stale | a structured (`DefId` slot, [(index, `TypeRef`)]) key. **Converted in 8.525** (`closure_specs`, `ClosureSpec::binds`). |
| `func_type_refs` (`decl_index.cryo:129`) | the family slot (door-internal) - but last-writer-wins per family, so an overloaded family answers its last signature | readers ask `func_type_of_def`; retire the map. |

## C. Not names - position keys, dead stores

| store | key | note |
|---|---|---|
| `ResolutionMap.singles` (`resolver/resolution_map.cryo:21`) | `hash_str(span.file)` + line + column | no lookup reader: `Resolver::resolve(span)` has no callers; dead_code iterates it |
| `ResolutionMap.overloads` (`:24`) | same | **dead store**: written once (`resolver/name_resolution.cryo:2706`), never read. **Deleted in 8.525** with the by-spelling scope walk that fed it (`Resolver::lookup_overloads`) and the dead `Resolver::lookup_local`. |
| `DeadCode.used` / `method_used` (`passes/dead_code.cryo:104,112`) | the same span key | lossy: a span copied onto a clone collides; key by the target's `DefId` |
| `extern_symbol_first` (`decl_index.cryo:262`) | the C link symbol's text | W0011 evidence only; a C symbol is an external name, not a Cryo identity (not ruled) |

## D. Out of scope

| store | why |
|---|---|
| codegen `GlobalRegistry.index` (`codegen/state/global_registry.cryo:22`) | **rulings 126 and 141.** The key is not the linker symbol ruling 126 assumed but `format("%s::%s", ns, leaf)` interned (`codegen/ops/symbol_resolver.cryo:309`); ruling 141 leaves it as it is until the module-globals table (#3) is converted, and it is re-keyed then. |
| `InternTable.lookup` (`resolver/intern_table.cryo:34`) | the `intern` door's own store |
| `Scope.symbol_index` (`resolver/scope.cryo:147`) | the `scope-value` door's ribs |
| `ModuleGraph.name_index`, `module_defs`; `Resolver.module_scopes`; `DeclarationIndex.module_imports` | keyed by `ModulePath`, the module's identity; `name_index` is the `module-by-path` door's own store |
| `SemaState.locals`, `local_muts`, `local_kw_spans`, `lambda_outer_locals` | a body's lexical scope (undo-logged), not a declaration store |
| `ModuleLoader.scanned_ns`, `ns_map` | file discovery, before any identity exists |
| `DiagRenderer.file_index`, `DiagSink.seen_keys`, CLI `commands` | source cache, diagnostic dedup, argv dispatch (ruling 5) |

## E. Converted since the audit (identity-keyed at the tip)

`type_reverse`, `copy_entries`, `array_elem_to_type`, `implicit_converters`
(both halves `TypeRef`), `overload_index` and `refused_methods` (the family
slot: owner `DefId` + leaf, the member-function door's key), `ConstantTable.by_def`
/ `enums_by_def`, `spec_index`, `instance_keys`, `materialized_insts`,
`type_index`, `cache_index`, `mono_set`, `trait_head_index` (trait `DefId` +
target), `inherent_owner_index`, `generic_param_cache`, `intern_cache`
(structural, except #7), `type_map.cache`, `move_check.types`, `facts_*`,
`import_targets`, `explicit_imports`.  `trait_decl_index` is gone
(`trait_decl_slots`, by `DefId`); the arena's nominal caches and the
placeholder cache are gone (each creation allocates; no name cache).

## Count

**10 rows break a rule (A)**, of which #1 is converted here (8.523), #7
in 8.527 and #5 in 8.528, leaving **7**; of the rest, `closure_spec_map` (B) and the dead
`ResolutionMap.overloads` (C) went in 8.525; 2 carry identity as text (B); 4
position/external/dead (C); 1 is rulings 126/141's (D), keyed by
`namespace::name` text rather than the linker symbol, left until #3.
