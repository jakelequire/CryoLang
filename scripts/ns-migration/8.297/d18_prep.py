#!/usr/bin/env python3
"""D18's keyword half, the part that needs no ruling (§8.297):

1. The ten `x as u32 < y` sites are parenthesized `(x as u32) < y`.  A
   cast binds tighter than any binary operator (`parse_cast`: unary
   (`as` type)*), so the parentheses spell the parse the tree already has
   under the keyword lexer, and they are what keeps the line parsing when
   `u32` is an identifier and the `<` after a cast's type opens generic
   arguments.  Valid under both lexers; every program compiles to the same
   thing.
2. An intrinsic's parameter and return annotations, and an enum's
   discriminant annotation, are stamped by the name layer's walk as every
   other written type-position name is.  Today every one is a keyword
   primitive (`void*`, `u32*`, `: i32`), which `stamp_annotation` matches
   nothing for, so this answers 0 asks until the keyword half lands; it is
   the walk's contract (D23) either way, and the keyword half found both
   holes (§8.295).
3. `va_list` joins `ResBase::is_primitive_spelling`: it is a keyword
   primitive to the lexer and was not one to the resolver's authority - the
   drift the lexer's comment records for `never`, the other way round.

Run from the repository root.  Every edit is asserted to match exactly once.
"""
import io
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))


def edit(rel, pairs):
    path = os.path.join(ROOT, rel)
    text = io.open(path, encoding="utf-8").read()
    for old, new in pairs:
        n = text.count(old)
        if n != 1:
            raise SystemExit("%s: expected exactly one match, found %d:\n%s" % (rel, n, old[:200]))
        text = text.replace(old, new)
    io.open(path, "w", encoding="utf-8", newline="\n").write(text)
    print("%s: %d edit(s)" % (rel, len(pairs)))


# -- 1. the ten casts ----------------------------------------------------------
edit("compiler/src/compiler/sema/call_resolver.cryo", [
    ("        if (line.length() as u32 < span.end_col - 1) { return \"\"; }",
     "        if ((line.length() as u32) < span.end_col - 1) { return \"\"; }"),
])
edit("tools/CryoLSP/src/handlers/hover.cryo", [
    ("            if (line0 as u64 < docp.line_index.line_count()) {",
     "            if ((line0 as u64) < docp.line_index.line_count()) {"),
    ("                if (col as u64 < lb", "                if ((col as u64) < lb"),
    ("                    while (e as u64 < lb", "                    while ((e as u64) < lb"),
])
edit("tools/CryoLSP/src/handlers/keyword_docs.cryo", [
    ("    if (p + 4 as u64 < len && KeywordLookup::is_ident_byte(ptr[p + 4])) {",
     "    if (p + (4 as u64) < len && KeywordLookup::is_ident_byte(ptr[p + 4])) {"),
])
edit("tools/CryoLSP/src/handlers/semantic_tokens.cryo", [
    ("        while (ln as u64 <= last as u64 && ln as u64 < total) {",
     "        while (ln as u64 <= last as u64 && (ln as u64) < total) {"),
    ("            if (c == 47 as u8 && i + 1 as u32 < n",
     "            if (c == 47 as u8 && i + (1 as u32) < n"),
    ("            if (c == 36 as u8 && i + 1 as u32 < n",
     "            if (c == 36 as u8 && i + (1 as u32) < n"),
    ("                    if ((base[j as i64] as u8) == 92 as u8 && j + 1 as u32 < n) {",
     "                    if ((base[j as i64] as u8) == 92 as u8 && j + (1 as u32) < n) {"),
    ("            const is_neg_num: boolean = c == 45 as u8 && i + 1 as u32 < n",
     "            const is_neg_num: boolean = c == 45 as u8 && i + (1 as u32) < n"),
])

# -- 2. the two walk holes ----------------------------------------------------
edit("compiler/src/compiler/resolver/name_resolution.cryo", [
    ("""    override visit(node: IntrinsicDeclNode*) -> void {
        // Already forward-declared; visit parameters/body if needed.
        if (node.has_body()) {""",
     """    override visit(node: IntrinsicDeclNode*) -> void {
        // Already forward-declared.  The signature's annotations are stamped
        // here as a function's are: a primitive (`void*`, `u32*`) is a
        // written name the name layer answers, not a token that needs no
        // answer.
        for (mut pi: i64 = 0; pi < node.parameters.length; pi++) {
            this.stamp_annotation(node.parameters[pi].type_annotation);
        }
        this.stamp_annotation(node.return_type_annotation);
        if (node.has_body()) {"""),
    ("""        // Declare generic params
        this.declare_generics(node.generic_params);
        this.stamp_trait_bounds(&node.where_bounds);

        // Declare variants
        for (mut i: int = 0; i < node.variants.length; i++) {
            const variant: EnumVariantNode* = node.variants[i];
            this.resolver.declare_enum_variant(variant.name, variant.span);""",
     """        // Declare generic params
        this.declare_generics(node.generic_params);
        this.stamp_trait_bounds(&node.where_bounds);
        // The discriminant's type is a written name (`: i32`, `: u8`),
        // answered like every other.
        this.stamp_annotation(node.discriminant_annotation);

        // Declare variants
        for (mut i: int = 0; i < node.variants.length; i++) {
            const variant: EnumVariantNode* = node.variants[i];
            this.resolver.declare_enum_variant(variant.name, variant.span);"""),
])

# -- 3. va_list ----------------------------------------------------------------
edit("compiler/src/compiler/resolver/res.cryo", [
    ("""            || name.eq("f64")
            || name.eq("never");
    }""",
     """            || name.eq("f64")
            || name.eq("never")
            || name.eq("va_list");
    }"""),
])

print("d18_prep: done")
