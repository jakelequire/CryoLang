# 8.422 - method typing reads the selection: the measurements' probes

Throwaway probes, kept so the figures in ledger entry 8.422 can be re-run.
None is a gate; none runs from `make`.

| file | applies to | prints |
|---|---|---|
| `typing-site-probe.patch` | `d1aef7c2` (before) | `MPROBE site=main` at the typing's by-name lookup: receiver shape, whether the selection pinned the call, whether the finder answered and whether its return equals the pin's; `site=timpl` at the trait-impl arm; `MPROBE_ENTRY` on entry to both finders |
| `finder-entry-probe.patch` | the 8.422 tree (after) | `MPROBE_ENTRY` on entry to `lookup_method_with_inheritance` and `lookup_method_through_trait_impls` |
| `final-type-probe.patch` | the 8.422 tree; the same two hunks apply by hand to `d1aef7c2` | `MPROBE site=final` for every method call `resolve_call` types: site, pass, final type, pinned method |

Run: apply a patch, `make cryo`, copy `compiler/build/cryo.exe` out of the
build tree, then

    bash scripts/ns-migration/8.422/probe_run.sh <copied cryo.exe> <absolute out file>

`probe_run.sh` builds the compiler, `tests/`, every test project and every
example from clean, and the unit suite, collecting the probe lines
deduplicated per build (`sort | uniq -c`).  It rebuilds `compiler/build`
with the probe compiler: run `make cryo` again afterwards.

Tabulation was by hand from the out files: sum the `uniq -c` counts
grouped by the probe's `key=value` fields; for the final types, compare
the multiset of `site=final` lines per build section, leaving out the
files the change edited (their line numbers moved).
