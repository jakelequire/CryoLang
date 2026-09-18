# §8.240 — the pass registers nothing for a placed named specialization

* `shadow.patch` — the instrumented tree: `git checkout 5a464de4 && git apply
  scripts/ns-migration/8.240/shadow.patch && rm -rf compiler/build && make
  cryo`, then `bash scripts/objcmp/corpus2.sh <tag>`.  It carries the
  monomorphizer's seam registering the clone's methods, its impl blocks'
  methods and the bare-name mapping, with the pass's registrations still
  in place and two instruments: `PM` at each of the pass's four
  `register_methods` calls (site, whether the method already carries an
  `overload_entry`, its name, and its kind - `plain`, `method-spec`,
  `self-ret-default`, or `plain-RET-INVALID` for a resolved-return-less
  method the registration guard skips), and `SEAM` in the monomorphizer
  for an impl method whose return type is unresolved at placement.
* Table: `awk -F'\t' '$3=="PM"' .objcmp/<tag>-lines.txt | cut -f4,5,7 |
  sort | uniq -c`; `'$3=="SEAM"' | cut -f4 | sort | uniq -c`.

The clean tree is the commit itself: the pass's four registrations and
its name mapping deleted, `SpecInjector::register` reduced to the
function-clone arm.
