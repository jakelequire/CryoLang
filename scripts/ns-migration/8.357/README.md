# The arena flip: how the diff was produced

Every script lives beside this file and runs from the repository root
(`python scripts/ns-migration/8.357/<script>`), on the parent commit.

1. `git apply --3way scripts/ns-migration/8.348/probe-cf23.patch` - the probe
   that threads each parameter's declaration symbol through the arena and
   answers every comparison both ways (`B_STRICT`).  Three files conflict
   with the head-binding work that landed after the probe was made:
   `python resolve.py compiler/src/compiler/sema/method_binding.cryo c,o,c,c`,
   `python resolve.py compiler/src/compiler/sema/call_resolver.cryo o`,
   `python resolve.py compiler/src/compiler/types/trait_checker.cryo c`
   (`c` takes `resolve-<file>-<n>.fragment`: the landed signatures with the probe's
   identity comparisons).  With `B_STRICT=all` this build is the flip; its
   census fails only on `chain_default_param_conflict`, which compiles.
2. `convert.py` - each `TypeArena::b_match(tag, A, An, X.param_sym, X.param_name)`
   becomes `X.declared_by(A)`, each `lookup_subst_for_param(tag, sym, name,
   subst)` becomes `lookup_subst_for_param(sym, subst)`, every
   `TypeSubstitution::b_site(..)` line goes.  10, 7 and 20 sites.
3. By hand: the two `lookup_subst_for_param` definitions ask by symbol only;
   `GenericParamType::declared_by`; `create_generic_param` gives a
   symbol-less parameter no type.
4. `cut_arena.py`, `cut_subst.py` - the probe's scaffolding (`b_match`,
   `b_strict`, `get_probe`, the site tags, the SHADOW prints) goes.
   `undo_cosmetic.py` restores two hunks the probe had reflowed.
5. `tidy.py` - `symbolic_name_is_generic_param` deleted for the existing
   `symbolic_param_in_scope`; the "crossed" skip in
   `add_owner_to_head_bindings` (only two same-spelled parameters could
   cross); comments that described the spelling-keyed arena.
6. By hand in `scripts/lane-gate.py`: the six `SCANNED_ARRAYS` entries rule 1c
   reported stale.  `cut_classify.py` removes the same six from
   `residue_classify.py`, then `python scripts/ns-migration/residue_classify.py`
   regenerates `residue.md`.
7. `drop_translation.py` - the owner-to-head translation is deleted, measured
   redundant first (the build with `owner_type_through_head` answering `ty`
   unchanged passed the unit suite, all compile-fail and project tests, the
   14 examples and both shape sets).
8. `sec0.py` - the section 0 rows the change moved.
