# §8.233 — the trait-impl registry reshape (D24 + §8.223)

* `shadow.patch` — the instrumented tree: `git checkout c1577f38 && git apply
  scripts/ns-migration/8.233/shadow.patch && rm -rf compiler/build && make
  cryo`, then `bash scripts/objcmp/corpus2.sh <tag>`.  The new table
  (`trait_heads`) and selector live beside the old tables; every reader
  computes the new answer beside the old, prints a `SHADOW` line and returns
  the old.  Instruments: `SEL` (one selection), `LIST` (a block list, with a
  `SAMETRAITS` verdict for the same traits through different blocks), `METH`
  (a method answer), `FILT` (the monomorphizer's head filter), `LMT`
  (`lookup_method_through_trait_impls`).
* `tab.py` — tabulates `.objcmp/<tag>-lines.txt` into the entry's table.

The clean tree is the commit itself; there is no edit script, the reader
conversions were written by hand.
