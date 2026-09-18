# §8.239 — what enters name resolution, and by which route

* `shadow.patch` — the instrumented tree: `git checkout 7d6705ac && git apply
  scripts/ns-migration/8.239/shadow.patch && rm -rf compiler/build && make
  cryo`, then `bash scripts/objcmp/corpus2.sh <tag>`.  One instrument, `RP`,
  at each of the nine `Resolver::lookup` sites in `name_resolution.cryo`:
  the site, the namespace the site is asking in, the name, the verdict, the
  kind and canonical name the UNFILTERED rib walk bound, and the kind and
  name `resolve_path`'s namespace-FILTERED walk (`shadow_lookup_ns`, the
  same walk with `ns.accepts`) binds.  Verdicts: `SAME` (one symbol),
  `NONE` (neither binds), `UNF-ONLY` (the filter refuses what the site
  bound), `FIL-ONLY` (the filter binds past a refused nearer rib), `DIFF`
  (two symbols).
* Table: `cut -f4,5,7 .objcmp/<tag>-lines.txt | sort | uniq -c`; every
  non-`SAME`/`NONE` row: `awk -F'\t' '$7!="SAME" && $7!="NONE"' | cut
  -f4,7,8,9`.
* The shadow prints NAMES, so the two output-exclusion projects
  (`namespace_gate_methods`, `visibility_gate`) read FAIL under it - the
  known trap, not a finding.

The clean tree is the commit itself: `resolve_type_qualified_name_bare_from`
deleted, its two callers on `leaf_in_module_scope`.
