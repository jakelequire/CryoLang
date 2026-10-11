R = "C:/Programming/apps/CryoLang/"

def sub(path, pairs, append=None):
    p = R + path
    s = open(p, encoding="utf-8", newline="").read()
    for old, new in pairs:
        c = s.count(old)
        assert c == 1, (path, old[:70], c)
        s = s.replace(old, new)
    if append:
        s = s.rstrip("\n") + "\n" + append
    open(p, "w", encoding="utf-8", newline="").write(s)

sub("docs/cryo-mangling-spec.md", [
    ("""| `impl Display for Dog { fmt(&this) -> void }`       | `C$tr$7Display$f3Dog-3fmt$F$s$Rv` |
""",
     """| `impl Display for Dog { fmt(&this) -> void }`       | `C$tr$7Display$f3Dog-3fmt$F$s$Rv` |
| `impl Conv<int> for S { conv(&this) -> int }`       | `C$tr$4Conv$Li$G$f1S-4conv$F$s$Ri` |
"""),
])

entry = """
### 8.594 One type implementing a generic trait at two arguments is answered through a bound by the implementation the bound names, and the two link as two symbols - 2026-10-11

## What changed

* `S` implementing `Conv<i32>` and `Conv<i64>`, called as `t.conv()`
  under `where T: Conv<X>`, was an internal E0900 in each copy of the
  body: monomorphization found the receiver's implementations of the
  trait by trait and receiver only, and with two it bound neither.
  Sema now records the bound's trait arguments on the call
  (`CallExprNode::bound_trait_args`, resolved in the body's own
  parameters where the bound answers the call, `MethodBinding::
  bound_trait_arg_types`; a trait the call's bounds name twice records
  none).  The cloner carries them and the substituter applies the copy's
  substitution, and `MonoCallSpecializer::trait_item_on` keeps, among
  several implementations, the one whose head writes those arguments
  (`GenericRegistry::trait_args_unify`, which asks `annotation_unifies`
  position by position).
* Binding the right method then linked the wrong one: both
  implementations' methods had one symbol,
  `C$tr$...4Conv$f...1S-4conv$F$s$Ri`, because a trait-impl method's
  symbol named the trait without its arguments.  The spec's grammar
  puts a segment's argument list on the trait path and says a correctly
  mangled symbol never collides; the symbol now carries them
  (`C$tr$...4Conv$Li$G$f...`).  An implement block records its trait
  arguments where its signatures are resolved
  (`ImplBlockNode::trait_arg_types`, a specialization's clone holding
  them substituted), and `DeclarationIndex::register_methods_through`,
  the one place a method's symbol is made, passes them to
  `MangledName::for_method_or_trait_impl`.  A trait taking no arguments,
  and one written with the associated-type shorthand
  (`implement trait Iterator<T>`), keeps its symbol.  The demangler
  already reads a segment's argument list.  A spec example is added.
* New project `generic_trait_implemented_twice_through_bound`, declared
  in `tests/started-compiling`.

## Proof

* The new project: the compiler before this change refuses it (E0900 in
  both copies of `via`); this one builds it and it exits 0.  Controls,
  each a copy of this tree with one half removed: without the selection
  in `trait_item_on`, E0900 in both copies again; without the trait
  arguments in the symbol, it builds and the program exits 2 (both
  copies reach the `Conv<i32>` method, one symbol having replaced the
  other at link).
* Over the full corpus (521 entries) every status is the one before the
  change but the new project's; logs the same but for the compiler's own
  warnings' line numbers and the order a project's tests print in.
* Objects: the unit-test project and the 14 examples built by the
  compiler before and this one (1,183 objects): 1,064 identical, 119
  differing only in that a trait-impl method's symbol gained its trait's
  argument list - equal symbol tables (as sets) and disassembly with
  relocations once that list is removed - and none otherwise.  The same
  comparison against the copy without the symbol change: 1,183
  identical, so selecting the implementation moved no object.  The
  comparison's control: one byte flipped in one object's code is
  reported as differing.
* `make verify ARGS="--baseline HEAD"`: 449 test and 116 example objects
  moved, the renamed symbols; census, examples, LSP, cross, incremental,
  blind-spot and flow gates OK; the declared project refused by the
  baseline and built by the tree.  check-fast OK; roster OK (merged);
  clean build 324 warnings, the same set.  The residue table gains the
  bound-argument resolution's `ResolutionContext::new("")` site (L).
  §0: residue 208 to 209, projects 237 to 238.

## Not done

* `const z: boolean = Result::Ok(1);` still passes the type check and is
  refused at code generation (E0600).  Two rules meet there: a variant
  constructor whose generic enum's arguments cannot be inferred keeps the
  enum's uninstantiated type (by design, refused when its payload is
  lowered), and the type checker converts every enum to `boolean`
  implicitly, a payload-carrying or generic one included
  (`TypeChecker::check_compatibility`), so even
  `Result::<i32, i32>::Ok(1)` is accepted as a `boolean`.  Which rule is
  wrong is a language question.
* `"a\\0b"`'s bytes after the NUL are still lost: the escape decoder
  (`Lexer::process_escape_sequences`) returns the decoded literal as a
  `string`, NUL-terminated, so the compiler's own value ends at the NUL
  and every later stage (the literal node, the string cache,
  `LType::const_string`) measures it by `strlen`.  Keeping them needs the
  literal's bytes carried with their length from the decoder to the
  constant, or a refusal of an embedded NUL in a `string` literal;
  either is a decision.
"""

sub("docs/name-resolution.md", [
    ("`python3 scripts/ns-migration/residue.py --count` → **208** (",
     "`python3 scripts/ns-migration/residue.py --count` → **209** (+1 in §8.594: a bound's trait arguments resolved where the bound answers a call, a `ResolutionContext::new(\"\")` beside its neighbours; 208 before, "),
    ("`ls -d tests/tests/projects/*/test.json | wc -l` → **237** (",
     "`ls -d tests/tests/projects/*/test.json | wc -l` → **238** (+1 in §8.594, `generic_trait_implemented_twice_through_bound`: one type implementing a generic trait at two arguments, called through a bound; 237 before, "),
], append=entry)
print("ok")
