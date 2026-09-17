# §8.234 — `scope_owner_key`'s spelling arms

* `shadow.patch` — the instrumented tree: `git checkout fd3756af && git apply
  scripts/ns-migration/8.234/shadow.patch && rm -rf compiler/build && make
  cryo`, then `bash scripts/objcmp/corpus2.sh <tag>`.  `shadow_sok` prints
  one `SHADOW SOK <site> <kind> <spec> <spelling> <key> <member> <hit>` line
  at each of the owner key's five readers: `kind` is the stamp that reached
  the key (`Def-canon`/`Def-suffix` for a module by whether the spelling is
  its canonical name, `TRDef-*`, `TRPrim`, `TRGP-same`/`TRGP-subst` for a
  type parameter by whether the substituter rewrote the spelling, `Pending`,
  or `Res::to_string()`), `spec` whether `spec_owner` answered, `hit` what
  the reader's lookup under the key found.
* The table is `cut -f4,5,6,10 .objcmp/<tag>-lines.txt | sort | uniq -c |
  sort -rn`.

The clean tree is the commit itself; the two arms were rewritten by hand.
