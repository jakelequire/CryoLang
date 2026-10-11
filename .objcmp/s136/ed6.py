R = "C:/Programming/apps/CryoLang/"
p = R + "docs/name-resolution.md"
s = open(p, encoding="utf-8", newline="").read()
entry = """
### 8.593 The identity-text check follows an override's answer through the base class, and a trait-impl head's written argument is matched by definition; rulings 227-230 recorded - 2026-10-11

## What changed

* A call made through a base class (`s.label()` on a `Shape*`) names the
  base's method, so the text an overriding method answers was not
  followed to it.  `IdentityTextCheck::answer_overridden` links the
  answer of each `override` method to the answer of every same-named
  method of each class it derives from, asked through the
  `member-function` door (`lookup_family_entries` with the base's
  `FamilyOwner::Type`).  The where-arg binder site this found was
  converted in 8.592; nothing else in the compiler is refused by it.
  New project `identity_text_refused_through_an_override`, declared in
  `tests/started-passing`.
* `GenericRegistry::annotation_unifies` decided that a head's written
  argument stamped as a declaration (`Wrap<T, Alpha>`) matched a
  subject's argument by comparing the definition's path text with the
  argument's template key.  It compares the definition with the one the
  argument is (`DeclarationIndex::def_of`, an instantiation reduced to its
  template).  The registry holds the declaration index, handed to
  `GenericRegistry::new`; `head_matches`, `head_matches_args` and
  `annotation_unifies` are methods of the registry rather than static
  functions handed the arena.  The function's allow stays: its reason
  names the two primitive arms, which is all that is left of it.
* Rulings 227-230 recorded (Dispatch, delegated): the Python-gate
  cleanup also removes `--emit=facts` and the compiler-internal allows;
  the override flow lands with the lane gate's `LOOKUP_OTHER` re-pinned
  35 to 36; `LangModule::is_written` keeps the `lang-item` id; name
  resolution is complete once these two holes are closed and a final
  read-only audit passes.

## Proof

* Override flow, as a triplet over the new project: the compiler before
  this change builds it (exit 0); this one refuses it once, E0157 at
  `src/main.cryo:34:9` with the override's line in the note; a copy of
  this tree with the `answer_overridden` call removed builds it (exit 0).
* `annotation_unifies`, agreement over the full corpus (520 entries:
  compiler, stdlib, tests, every project and example with its `tests/`,
  the unit leg, each negative file): a probe computing the old and the
  new answer at every `Res::Def` arm printed 2,438,238 answers in 458
  logs, all agreeing, none disagreeing.  Control: the same probe with the
  new answer forced to `true` printed 27 disagreements (in
  `impl_concrete_arg_filters_impl` and `impl_concrete_arg_selects_impl`),
  and `impl_concrete_arg_filters_impl` built instead of being refused -
  the corpus sees a wrong answer here.
* Every corpus status the same under the compiler before and this one;
  logs the same but for the order a project's tests print in.
* `make verify ARGS="--baseline HEAD --require-identical"`: OK, 8,305
  test and 1,126 example objects, none moved.  The baseline's census
  failed `ProcessAsync::two_children_join_concurrently` (a wall-clock
  bound) twice under the default test concurrency, on the unchanged
  compiler; it passes alone, and the run with `CRYO_TEST_JOBS=4` passed
  it on both sides.  check-fast OK; roster OK (merged, 2,715 entries);
  clean build 324 warnings, the same set.  Lane gate: `LOOKUP_OTHER` 35
  to 36 (the door call, ruling 228), `DEFID_PATH` 29 to 28 (the path
  read), re-pinned.  `residue.md` regenerated (the door call's site, J
  135 to 136).  §0: residue 207 to 208, J 135 to 136, `template_key_of`
  9 to 8, `DEFID_PATH` 29 to 28, projects 236 to 237.
"""
s = s.rstrip("\n") + "\n" + entry
open(p, "w", encoding="utf-8", newline="").write(s)

p = R + ".objcmp/s135/NR-COMPLETE.md"
s = open(p, encoding="utf-8", newline="").read()
old = """`make verify ARGS="--baseline HEAD --require-identical"` OK (8,305 test
and 1,126 example objects, none moved, at 8.591 and 8.592); `make
check-fast` OK; `make roster-check` OK (2,714 entries: 2,226 unit, 236
project, 252 negative); clean build 324 warnings, the same set; pins
refreshed so the bootstrap compiler runs these checks."""
new = """`make verify ARGS="--baseline HEAD --require-identical"` OK (8,305 test
and 1,126 example objects, none moved, at 8.591, 8.592 and 8.593); `make
check-fast` OK; `make roster-check` OK (2,715 entries: 2,226 unit, 237
project, 252 negative); clean build 324 warnings, the same set."""
assert s.count(old) == 1
s = s.replace(old, new)
s = s.replace("among the 236 projects;", "among the 237 projects;")
open(p, "w", encoding="utf-8", newline="").write(s)
print("ok")
