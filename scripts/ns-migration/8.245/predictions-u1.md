# Unit 1 predictions (written before the build)

Shadow tags over six halves (examples 14, unit suite via `cryo test async`, lsp-check, cross-check):
- SCOPEQUAL-MISS: 0 (a stamped Def the index does not hold would be a registration defect; the
  door then makes it E0900).
- STAMP-ARENA (index miss, arena hit) at types/resolver.cryo: 0 - if non-zero, the arena is
  answering a name the index never registered; characterise which names.
- STAMP-MISS (both miss): 0.
- FNRET-DIFF: 0 - func_returns and func_type_refs are written in lockstep at all three writers.
- COHKEY-DIFF: 0 - a Named annotation that is unstamped and yet finds a type by spelling would
  be the only way to differ.
- PATLEAF-DIFF: 0 - a constant pattern's written leaf is the stamp's leaf.
Control: an inverted build (each condition flipped) prints each tag at least once over one example.

lane-check after the deletion commit (from tests/lane-baseline.txt at d156d2a3):
- LOOKUP 12 -> 11 (types/resolver.cryo 1 -> 0; the row disappears for that file)
- LOOKUP_OTHER 104 -> 103 (sema/type_utils.cryo 7 -> 6: find_global_by_qualified gone)
- LOOKUP_ROUTED 60 -> 59 (sema/call_resolver.cryo: this.types.lookup_func_return at resolve_direct_call)
- REGISTRY_READ 67 -> 66 (sema/sema.cryo: get_template(def_q) -> template_of(def_id))
- DEFID_UNWRAP 31 -> 28: decl_index +1 (global_of_def), type_utils -2, call_resolver -1,
  type_resolution -1 (ann_canon_key's Named arm)
- everything else unmoved
Objects: 0 of 1,126 examples and 0 of 2,665 tests moved against half U (d156d2a3).
