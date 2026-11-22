# incremental-runtime-change

Two builds of one program against one runtime source tree, driven by
`cryogate incr --runtime` (`make incremental-runtime-check`, a `make verify`
gate): the project runner builds every program against the tree's runtime,
and this defect needs the runtime itself to change between two builds with
nothing else changed.

1. The check copies `runtime/` to `.verify/incremental-runtime/runtime` and
   names the copy in `CRYO_RUNTIME` for both builds.
2. `src/main.cryo` divides by the count of its arguments, 0 when the check
   runs it, so the program panics with the runtime's divide-by-zero message.
3. The check changes that message in the copy's `panic/unwind.cryo` and
   `panic/abort.cryo` and builds again. The compiler rebuilds the copy's
   runtime archive, since its key covers the runtime's sources; the build
   must then link the program again, because the program's whole-build
   fingerprint covers the runtime archives the link names, and the program
   must report the changed message.

A fingerprint that leaves the runtime archives out calls the second build
up to date, and the program keeps the old runtime linked and reports the
old message.
