# §8.235 — `AsyncOwner.decl` and the mono spec-method ordering

* `shadow.patch` — the instrumented tree: `git checkout 7e2541a2 && git apply
  scripts/ns-migration/8.235/shadow.patch && rm -rf compiler/build && make
  cryo`, then `bash scripts/objcmp/corpus2.sh <tag>`.  Three instruments:
  `RPT` at `repoint_method` (the index's name for `owner.decl` beside the
  carried `owner.qname`, and whether the instance `owner.ty` is named);
  `SPO` at the two `call_specializer` registrations (the index's name for
  `recv_type` beside the key, the type's kind, whether the instantiation is
  materialized, whether the index holds ANY type under the key); `SPOEND` at
  the build's end in `instance.cryo` (whether each key `SPO` could not name
  holds a type by then - `registered-later` or `NEVER`).
* Tables: `awk -F'\t' '$3=="RPT"' .objcmp/<tag>-lines.txt | cut -f4,7,8 |
  sort | uniq -c`; `'$3=="SPO"' | cut -f4,5,8,9,11,12`; `'$3=="SPOEND"' |
  cut -f5`.

The clean tree is the commit itself; the conversions were written by hand.
