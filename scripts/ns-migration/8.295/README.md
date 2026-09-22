# §8.295 - D18's keyword half, BUILT and BACKED OUT at the parser's fork

The two scripts here apply the keyword half to the tree, in order:

1. `primitive_keywords_to_identifiers.py` - the lexer: the 23 primitive-type
   keywords and the two reserved words nothing reads (`tuple`, `optional`)
   stop being tokens; `is_primitive_type` goes.
2. `retire_primitive_annotation.py` - the tree: `TypeAnnotation::Primitive`
   and `PrimitiveAnnotation` deleted, every reader's arm gone (the `Named`
   arm reads the `PrimTy` stamp), every synthesizer mints a `Named` stamped
   at birth, the name layer answers a primitive spelling AFTER every scope
   declines it, the type layer reads the stamp instead of the spelling, the
   constant table records the annotation and reads its stamp when it folds.
   (The two annotations the walk never stamped - an intrinsic's signature,
   an enum's discriminant - `va_list` in `is_primitive_spelling`, and the
   parentheses on the ten `x as u32 < y` sites landed ahead of this in
   §8.297, `scripts/ns-migration/8.297/d18_prep.py`; this script assumes
   that tree.)

Applied at `f547ba30`, the tree builds (a clean `make cryo`), the unit
suite passes (2,138 unit, 213 compile-fail, 73 projects), every project,
example and negative behaves, no annotation reaches `resolve_named`
unstamped, and **0 of 1,126 example and 0 of 3,002 test objects move**.

It is backed out because the LSP - and the compiler's own source, under
the compiler it builds - stops parsing at **`x as u32 < y`**: with `u32` an
identifier, the `<` after a cast's type opens generic arguments (`as
Entry<K, V>*`, 8 sites in the tree need that) and the comparison (10 sites
in `compiler/src` and `tools/CryoLSP`, 0 in `stdlib`, `tests`, `examples`)
no longer parses.  Rust refuses exactly this form and asks for
parentheses; whether Cryo does the same (parenthesize the ten sites, a
language behaviour change) or the parser decides the `<` some other way
is a ruling, and the entry records the choice for Jake.  The full diff of
the attempt is `.objcmp/u2-keyword-half-attempt.patch` on the host that
built it.
