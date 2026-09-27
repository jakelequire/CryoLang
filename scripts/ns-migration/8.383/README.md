# The `TypeRef` seal

`seal_reads.py` produced the read half of this change; the compiler decided
every site.

Recipe, from the parent commit, with `CRYO` a compiler whose diagnostic cap
is off (see `../8.382/README.md`):

1. Over a COPY of `compiler/`, make `TypeRef.id`, `TypeRef.arena` and
   `TypeRef::new` private, and run `$CRYO check src/main.cryo > seal.log`
   there (205 refusals at the parent: 200 reads of `.id`, 5 of `new`).
2. In the real tree: `python seal_reads.py seal.log compiler` - 54
   comparisons become `equals`, 92 reads become `key()`, the 5
   constructions are listed.
3. By hand: `type_ref.cryo` gains `private:`, `key()` and the allocator
   `TypeRefs`; the arena holds a private `TypeRefs` in place of its
   `next_id` counter and mints through it; the mangler's `encode_type`
   takes the handle its only caller holds; `TypeUtils::unwrap_to_base_ref`
   is the one peeler, `unwrap_to_base_type` reads through it, and the
   symbolic checker keeps the handle.
4. `$CRYO check src/main.cryo` over the tree: 0 errors. The editor
   (`tools/CryoLSP`, built with `$CRYO build`) reads no handle's number.
