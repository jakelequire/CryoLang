# The arena's reads take a `TypeRef`

`strip_id_args.py` produced the call-site half of this change. The compiler
decides every site; the script only edits where a refusal points.

Recipe, from the parent commit, with `CRYO` a compiler whose diagnostic cap
is off (`max_errors: 0` in `compiler/diag/config.cryo`, built once and copied
out - the shipped cap of 500 stops a count early):

1. By hand: `TypeArena::lookup`, `pointer_pointee_of`, `array_element_of`,
   `resolve_display_name`, `resolve_display_name_short` take `ty: TypeRef`
   in place of `id: u64`; codegen's three `lookup_type_by_id(id: u64)`
   forwarders become `arena_type(ty: TypeRef)`.
2. `python strip_id_args.py --rename lookup_type_by_id arena_type compiler/src`
3. `cd compiler && $CRYO check src/main.cryo > a1.log`, then
   `python strip_id_args.py a1.log compiler` - every argument the check
   refuses as `u64` where a `TypeRef` is wanted, and which reads
   `<expr>.id`, loses the `.id`. Repeat on the next log until only the
   non-`.id` arguments are listed (two stored numbers).
4. By hand: `TemplateEntry.param_type_ids`, `TypeSubstitution.param_ids`
   and `InferCtx.param_ids` become `TypeRef[]` (`param_types`, `params`,
   `params`); `TypeSubstitution::add/get` and `InferCtx::binding_index`
   take the parameter's handle.
5. Check again; `python strip_id_args.py --field b1.log compiler
   param_type_ids param_types` and `... param_ids params` rename the field
   at each E0204 the check reports; `python strip_id_args.py b2.log
   compiler` strips the `.id` arguments that are left.
6. By hand: the `u64[]` locals that feed those stores become `TypeRef[]`
   and their pushes drop `.id` (an array push is not argument-checked
   before code generation, so no check lists them - they were found by
   reading each store's writers); `TypeRef::new(tmpl.param_type_ids[i],
   arena)` in `type_resolution.cryo` reads the stored handle; the
   monomorphizer's array scan walks the arena's type list instead of
   counting ids.
7. Editor: `cd tools/CryoLSP && $CRYO build --build-dir=<scratch>`, then
   `python strip_id_args.py <log> tools/CryoLSP`.
