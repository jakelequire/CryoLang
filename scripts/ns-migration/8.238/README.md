# §8.238 — the spec type registered when the entry is placed

* `shadow.patch` — the instrumented tree: `git checkout 2e3e7f42 && git apply
  scripts/ns-migration/8.238/shadow.patch && rm -rf compiler/build && make
  cryo`, then `bash scripts/objcmp/corpus2.sh <tag>`.  It carries the
  conversion itself (the monomorphizer's `register_type` at the seam, the
  two `call_specializer` sites on `register_methods(recv_type, …)` behind
  `receiver_unmaterialized`) plus one instrument, `SPO` at those two sites:
  the old key beside the index's name for `recv_type`, the type's kind,
  whether the instantiation is materialized, whether the arena names it,
  and whether the site defers.
* Table: `cut -f4,5,7,8,9,10 .objcmp/<tag>-lines.txt | sort | uniq -c`;
  the deferred rows: `awk -F'\t' '$11=="DEFERRED"'`.
* The pair for the door: with the seam registration moved back to the
  pass (`SpecInjector::register` registering the type, the monomorphizer
  not), `examples/01-hello` is refused - `error[E0900]: 7 canonical
  name(s) named no registered declaration; first consumed at mono/spec
  inherent method` - where the by-key registration built it green
  (§8.235's `SPO`: 2 `NONAME` rows for hello, keyed silently).

The clean tree is the commit itself minus the `shadow_spec_owner` hunks.
