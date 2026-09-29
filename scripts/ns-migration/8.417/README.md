# Abstract-receiver typing probe

`abstract-receiver-probe.patch` applies to `af8f69d4`. It holds the
change in its first form (the `This` branch calling the trait lookup
directly, before it was folded into `scan_param_bounds` so the lane gate's
routed-lookup count does not grow) plus two probes in
`CallResolver::resolve_method_call`:

* `VPROBE <outcome> shape=<kind> this=<0|1> ... leaf=<method> at=<file:line>`
  once per typing in the abstract-receiver arm: `ok`, `void` (the method is
  declared returning void) or `invalid` (nothing typed the call).
* `NPROBE shape=<kind> this=<0|1> sym=<0|1> ...` once per typing that
  reaches the "receiver with no type symbol" exit, which returns invalid
  silently.

Build with `make cryo` (after `rm -rf compiler/build`: the change adds a
field to `SemaState`), copy `compiler/build/cryo.exe` out, then build the
compiler with it:

    cd compiler && CRYO_STDLIB=<repo>/stdlib <probe-cryo> build --build-dir=<abs dir> > probe.out 2>&1
    grep '^VPROBE' probe.out | awk '{print $2, $4}' | sort | uniq -c
    grep '^NPROBE' probe.out | awk '{print $2, $3, $4, $5}' | sort | uniq -c

To reproduce the "before" column, revert the `sema.cryo` and
`method_binding.cryo` hunks and keep the probes.
