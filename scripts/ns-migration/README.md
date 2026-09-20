# Generators and instruments of the name-resolution migration

Each directory is one `docs/name-resolution.md` §8 entry, and holds the
scripts that produced the edits that entry landed or the measurement it
reports. They exist so a landing can be re-derived from outside the
session that made it: a change produced by a script is reproducible only
while the script exists somewhere git can see.

Every script edits or reads the tree it is run against, so they run from a
checkout at the commit BEFORE the entry they belong to (`git log --grep
"section 8.NNN"` names the commit; check out its parent). The scripts whose
paths were once this checkout's absolute path now derive the repository
root from their own location (`_REPO`, three directories up); the rest
take repo-relative paths and are run from the repository root.

Two kinds of script:

* **Edit scripts** rewrite compiler sources and assert every count they
  depend on BEFORE saving, so a stale tree leaves the files untouched.
  Those that take `--shadow` produce the instrumented tree (the old answer
  and the new one printed side by side as `SHADOW` lines); without it they
  produce the clean one. A re-measurement is therefore
  `git checkout <files> && python <script> --shadow && rm -rf compiler/build
  && make cryo`, then the corpus run the entry names.
* **Tabulators** (`tsc_tab.py`, `tie_tab.py`, `loose.py`) read a shadow
  corpus (`.objcmp/<tag>-lines.txt`, gitignored) and print the tables the
  entry carries.

| entry | scripts | what they produced |
|---|---|---|
| §8.221 | `shadow_rdi.py`, `delete_rdi.py` | the shadow at `register_decl_in_index`'s six lookup-then-re-register pairs; their deletion |
| §8.222 | `shadow_icp.py`, `fix_icp.py` | the shadow at `is_candidate_public`'s three doors; `register_type(key, ty, is_public)` at 28 callers, the extern arm's verdicts, the `None`-arm door |
| §8.224 | `entry_span.py`, `shadow_tsc.py`, `shadow_tie.py`, `tsc_tab.py`, `tie_tab.py`, `loose.py` | the registry entry's span; the trait-in-scope and two-trait-tie instruments and the tables built from them (277 sites, 0 of 28 ties) |
| §8.225 | `stamp_tdef.py`, `readers_tdef.py`, `shadow_names.py` | the `DefId` on the six type-declaration nodes; the 21 readers moved to `type_of_def(node.def)` (`--shadow` keeps the re-derived key beside the stamp) |
| §8.226 | `owner_tref.py` | codegen's method owner as a `TypeRef`; `DeclarationIndex::impl_owner` |
| §8.228 | `async_owner.py` | sema's async declare pass taking the owner's type and key from the caller |
| §8.231 | `doors_tdecl.py`, `mutate_nostamp.py`, `probes/` | the 29 readers of a type declaration's own type as `type_of_decl` doors, the third tally and its two-exit report; the no-stamp mutation and the two probes that show the old readers' silence and the door's report |
| §8.232 | `rm_tref.py` | `register_methods` by the owner type at nine callers, `register_methods_by_key` at the four the index cannot name, the six duplicate name mappings deleted (`--shadow`: the `RM`/`NM` instrument behind the 130,001 / 38,974) |
| §8.233 | `shadow.patch`, `tab.py` | the trait-impl registry shadow: the identity-keyed head table and its selector beside the old tables, every reader printing old beside new (`git apply` over `c1577f38`, then `corpus2.sh <tag>`); the tabulator behind the 8,240,226-line table |
| §8.234 | `shadow.patch` | `scope_owner_key`'s shadow: the stamp kind, the key and the hit at each of its five readers (`git apply` over `fd3756af`, then `corpus2.sh <tag>`); the 2,509,947-line table is one `cut \| sort \| uniq -c` |
| §8.235 | `shadow.patch` | the async repoint's key beside the index's name for the registered owner (`RPT`), and the two mono spec-method sites' receiver beside the key with an end-of-build check of whether the key is ever registered (`SPO`, `SPOEND`) (`git apply` over `7e2541a2`, then `corpus2.sh <tag>`) |
| §8.237 | `shadow.patch` | a print at `TraitRef::identity()`'s leaf fallback (`TRID`), whose corpus count is 0 and whose control is a `where T: Nope` bound (`git apply` over `b3b36ca6`, then `corpus2.sh <tag>`) |
| §8.238 | `shadow.patch` | the spec type registered at the monomorphizer's seam and the two spec-method sites on `register_methods(recv_type, …)`, with the `SPO` print at both (the old key beside the index's name, the kind, materialized, arena-named, deferred) (`git apply` over `2e3e7f42`, then `corpus2.sh <tag>`) |
| §8.239 | `shadow.patch` | the `RP` print at the nine `Resolver::lookup` sites: the unfiltered rib walk's binding beside the namespace-filtered walk `resolve_path` makes (`git apply` over `7d6705ac`, then `corpus2.sh <tag>`); the 3,059,355-line route table is one `cut \| sort \| uniq -c` |
| §8.240 | `shadow.patch` | the seam registering a placed specialization's methods and mapping with the pass's registrations still in place, and the `PM` print at the pass's four `register_methods` calls (already registered, or new, and why) plus `SEAM` for an impl method unresolved at placement (`git apply` over `5a464de4`, then `corpus2.sh <tag>`) |
| §8.241 | `store-inventory.py`, `lane-pair.sh` | the inventory of every method under the lane gate's key rule on every type the context carries, with sites by receiver spelling (the entry's table); and the three audit mutations run under `1f852403`'s gate against its golden and under this tree's gate against its own, one at a time over the real tree, the tree restored after each (`bash scripts/ns-migration/8.241/lane-pair.sh` from the root) |
| §8.242 | `shadow.patch`, `corpus.sh`, `tick-ctl/` | the leaf scan printed beside the identity at the receiver-refresh decision (`RECVTRAIT`, one line per reader call) and every ask that names a trait at the arena's new `Trait` arm (`GQN-TRAIT`); `corpus.sh` builds the 14 examples and the async-filtered unit suite under the shadow compiler and collects the lines (`git apply` over `01617854`'s successor, `make cryo`, then `bash scripts/ns-migration/8.242/corpus.sh`); `tick-ctl/` is the consequence repro - `GenTick::tick(mut &this)` writing after a suspend behind a sync `SyncTick` declared first, polled from a different stack depth each time (33: the write lands, the receiver is the frame's own field) and `refused/driver.cryo`, the moving driver E0459 refuses |
| §8.243 | `corrupt.py`, `ns-pair.sh` | `corrupt.py` writes four copies of the ledger each carrying one check-cell corruption (D5's splice and the trait-registry fragment as at `fd3756af`; an answer in backticks; an unescaped pipe); `ns-pair.sh` runs `9e7b778b`'s `ns-status-check` (no shape rules) beside this tree's over each copy, both against the current tree (`bash scripts/ns-migration/8.243/ns-pair.sh` from the root; `PYTHON=…` if `python` is not the interpreter on PATH) |
| §8.244 | `shadow.patch`, `corpus.sh` | a print at `resolve_path`'s new `Err` arm - every head a tied import binds, with the two modules (`AMBPATH`) - and the six-halves run that collected it: the 14 examples, the whole unit suite, `make lsp-check`, `make cross-check` (`git apply` over `bc7b25ae`'s successor, `make cryo`, then `bash scripts/ns-migration/8.244/corpus.sh`; the tests half compiles the whole suite through `cryo test async` - `--list` parses without resolving and prints nothing) |
| §8.272 | `edit_*.py`, `default_shadow.py`, `impl_params.py`, `probe_prefix.py`, `probe_entry.py`, `probe_block.py` | the re-key of every substitution table by `SymbolID`, as 25 edit scripts run in the order the entry's file list gives (each asserts its match counts before saving; over `20e74985`); `default_shadow.py <root>` scans every trait default with its own `<P>` against every `implement<..P..> trait` of it (the in-tree population 2), `impl_params.py <root>` every impl head against its struct's parameter spellings; the three `probe_*.py` put the `SHADOW D30ID-*` prints on the mono signature fallback (0 resolutions over `corpus2.sh`, 0 entries over the LSP, 1,290 block entries in a one-file control) |
| §8.273 | `edit_value_asks.py`, `edit_neg.py` | the three value reporters' `signature_refused` asks in `sema.cryo`; the three value lines added to `E0203_refused_signature_no_cascade.cryo` |
| §8.274 | `probe_mr.py`, `probe_mr2.py`, `probe_mr_ctl.py`, `edit_mr_final.py` | the probe recording every bare-leaf `method_returns` key and printing each `lookup_method_return` that hits one (`SHADOW MR-BARE-WRITE` / `-HIT`, 25,374 / 0 over `corpus2.sh rc`); `probe_mr_ctl.py on|off` adds the one read by the leaf that makes the hit fire (96 / 96); `edit_mr_final.py` removes the probe and the bare-leaf write |
| §8.275 | `probe_stash.py`, `edit_conflict.py`, `edit_conflict2.py` | `probe_stash.py on|off` prints each stashed type argument before and after `subst.apply` (the `ChainIter<Range, TakeIter>` the entry names); `edit_conflict.py` is the first, over-broad refusal (whole-substitution, refused the `conv<U>` control too) and `edit_conflict2.py` replaces it with `TypeSubstitution::get` answering nothing for a parameter bound to two different types |
| §8.277 | `halves.sh`, `population.md`, `sites.tsv` | D31's population re-verified against the tree: `halves.sh <tag>` runs the two halves `corpus2.sh` does not reach (every stdlib file under `cryo check`, the compiler built from `compiler/` by the probed compiler) after `8.269/probe_d31.py apply` + `make cryo` + `corpus2.sh <tag>`; `probe_d31.py tally` / `sites` over both lines files give `population.md` (1,368 / 271) and `sites.tsv`, the location list the turbofish rewrite (`8.278/turbofish.py`) takes as its input |
| §8.278 | `turbofish.py`, `sites-extra.tsv`, `retire_lookahead.py`, `docs_turbofish.py` | D31's rewrite and retirement, over `3ab83793` (the pin that reads both spellings), in this order: `python scripts/ns-migration/8.278/turbofish.py apply scripts/ns-migration/8.277/sites.tsv scripts/ns-migration/8.278/sites-extra.tsv` (1,376 sites, 274 files; a second run and `check` read every site as `::<` and write nothing), then `retire_lookahead.py` (20 asserted edits over 6 compiler files: the lookahead, the module tables, the guessed flag and its notes; it moves one compiler/src site's line, so `check` is run before it), then `docs_turbofish.py` (the reference's 18 examples and §4.2's rule, the grammar's `TurbofishArgs`); `sites-extra.tsv` lists the eight sites in the control projects under `8.242/tick-ctl` and `8.261/control`, which no corpus half parses |

Not here: the scripts that edited the ledger itself, the entry drafts, and
the one-off controls written against a gate version that no longer exists
(§8.227's `gate_diff.py` read `lane-gate.py`'s `ANY_LOOKUP_RE`, which the
definition-derived rule removed).
