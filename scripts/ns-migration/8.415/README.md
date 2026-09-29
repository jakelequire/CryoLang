# Method-selection probe (ledger 8.415)

`method-probe.patch` applies to `c2277e06` and holds this change PLUS two
probes: `MPROBE` (one line per method-call typing in `resolve_method_call`:
which arm answered, whether the selection pinned the call, the receiver's
shape, symbolic or concrete walk) and `SPROBE` (one line per selection in
`bind_method_on_receiver`: why it declined, or `ok0`/`ok1` for the
non-generic/generic pass).  Build it with `make cryo`, copy
`compiler/build/cryo.exe` out, and build the compiler with it:

    cd compiler && CRYO_STDLIB=<repo>/stdlib <probe-cryo> build --build-dir=<abs dir> > probe.out 2>&1

Then:

    python why_unpinned.py probe.out             arm x pin x shape x walk x reason
    python diff_sites.py before.out after.out    sites whose typing count changed

Without the change (probe lines only), the selection declines generic
methods with `nomatch`; with it they bind `ok1`.  Exclude
`call_resolver.cryo` when comparing runs: the corpus is the compiler, and
the change moves that file's own lines.
