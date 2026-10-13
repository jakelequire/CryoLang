# incremental-new-instance

Two builds of one program, driven by `scripts/incremental-instance-check.py`
(a `make verify` gate): the project runner builds a program once, and this
defect needs a cached build followed by an edit.

1. `src/main.cryo` spawns a `thread::Scope` job of type `JobA`; built
   incrementally, it runs and exits 3.
2. `edit/main.cryo` replaces it and also spawns a `JobB`, which asks
   `std::thread` for a new instance, `Scope::spawn<JobB>`.  Built again
   incrementally over the first build's cache, it must link and exit 4.

`spawn<C>` is a generic method written inside `type struct Scope { .. }`, so
its instances are emitted in `std::thread`'s object, not in the module that
calls it.  If that module is not recognised as owning a specialization, its
object from the first build - which has no `spawn<JobB>` - is reused, and
the link fails on the undefined symbol.
