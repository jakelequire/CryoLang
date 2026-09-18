# §8.237 — `TraitRef::identity()`'s leaf fallback

* `shadow.patch` — the instrumented tree: `git checkout b3b36ca6 && git apply
  scripts/ns-migration/8.237/shadow.patch && rm -rf compiler/build && make
  cryo`, then `bash scripts/objcmp/corpus2.sh <tag>`.  One print at the
  fallback arm (`SHADOW TRID leaf <symbol id> <path length>`); the entry's
  measurement is the line count (0), and `.objcmp/sok-ctl/nobound.cryo`-shaped
  input (`where T: Nope`) is the control that makes it print.

The clean tree is the commit itself; the arm was rewritten by hand.
