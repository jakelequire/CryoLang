#!/usr/bin/env python3
"""D18's keyword half, the tree: with the primitive names identifiers, a
primitive in type position parses as a `Named` annotation and is answered
by the name layer's stamp (`Res::PrimTy`), so `TypeAnnotation::Primitive`
and `PrimitiveAnnotation` are deleted, every arm that read one goes (the
`Named` arm already reads the stamp), every synthesizer that minted one
mints a `Named` stamped at birth, and the name layer answers a primitive
spelling AFTER every scope has declined it - the alias fold's rule, one
rule for both.  The constant table records the annotation and reads its
stamp when it folds, because a module-level constant is recorded before
the walk that stamps it.

Run from the repository root after `primitive_keywords_to_identifiers.py`.
Every edit is asserted to match exactly once.
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


NAMED_PRIM = """new NamedAnnotation {
                name: name,
                span: this.span_from_token(tok),
                pre_resolved: TypeRef::invalid(),
                res: ResSlot::Pending,
            }"""

# -- the parser --------------------------------------------------------------
edit("compiler/src/compiler/parser/expr_parser.cryo", [
    ("    PatternSub, PatternWildcard, PointerAnnotation, PrimitiveAnnotation,\n    ProjectionAnnotation, ",
     "    PatternSub, PatternWildcard, PointerAnnotation, ProjectionAnnotation, "),
    # A primitive at an expression head is an identifier now.
    ("""        // Primitive type used as identifier (e.g., i32::max_value())
        if (this.is_primitive_type_token()) {
            const ptok: Token = this.advance();
            const name: SymbolStr = this.intern_lexeme(ptok);
            return new IdentifierNode(name, this.span_from_token(ptok));
        }

""", ""),
    ("""            TypeAnnotation::Named(n)     => { scope_name = n.name; }
            TypeAnnotation::Primitive(p) => { scope_name = p.name; }
""",
     """            TypeAnnotation::Named(n)     => { scope_name = n.name; }
"""),
    ("""        // The target type is usually a user-defined name (Identifier), but
        // `new T[n]` array allocation also accepts a primitive element type
        // whose name is a keyword (`new int[100]`, `new u8[1024]`).  Accept
        // either: a primitive keyword's lexeme ("int", "u8", ...) interns the
        // same way an identifier would, and resolve_new_expr falls back to
        // primitive resolution for it.
        mut type_tok: Token = this.current();
        if (this.is_primitive_type_token()) {
            this.advance();
        } else {
            type_tok = this.consume(TokenType::Identifier, "type name");
        }
""",
     """        // The target is a name - a declared type's or a primitive's (`new
        // u8[1024]`): both are identifiers, and which it is, is resolution's.
        const type_tok: Token = this.consume(TokenType::Identifier, "type name");
"""),
    # `void`: the one type-position keyword left, a `Named` the name layer
    # answers as the primitive it spells.
    ("""        // Void
        if (tok.kind == TokenType::KwVoid) {
            this.advance();
            const name: SymbolStr = this.ctx.intern("void");
            const ann: PrimitiveAnnotation* = new PrimitiveAnnotation {
                name: name,
                span: this.span_from_token(tok),
            };
            return new TypeAnnotation::Primitive(ann);
        }
""",
     """        // Void: a keyword, because it is a return-position shape and not a
        // type a value can have; in type position it is the primitive it
        // spells, answered by the name layer as every primitive is.
        if (tok.kind == TokenType::KwVoid) {
            this.advance();
            const name: SymbolStr = this.ctx.intern("void");
            return new TypeAnnotation::Named(%s);
        }
""" % NAMED_PRIM),
    ("""        // Primitive types
        if (this.is_primitive_type_token()) {
            this.advance();
            const name: SymbolStr = this.ctx.intern(tok.lexeme);
            const ann: PrimitiveAnnotation* = new PrimitiveAnnotation {
                name: name,
                span: this.span_from_token(tok),
            };
            return new TypeAnnotation::Primitive(ann);
        }

""", ""),
    ("""    is_type_start() -> boolean {
        if (this.is_primitive_type_token()) { return true; }
        return match""",
     """    is_type_start() -> boolean {
        return match"""),
    ("""    /// Check if the current token is a primitive type keyword.
    is_primitive_type_token() -> boolean {
        return this.current().kind.is_primitive_type();
    }

""", ""),
])

edit("compiler/src/compiler/parser/parser.cryo", [
    ("    pattern, PatternBinding, PatternElement, PrimitiveAnnotation, ReferenceAnnotation,\n",
     "    pattern, PatternBinding, PatternElement, ReferenceAnnotation,\n"),
    ("""                // The receiver of `implement string { ... }` denotes the
                // primitive, not a name to look up.  This synthesis runs from
                // the impl target's TEXT rather than its token, so the token
                // test that shapes every written annotation cannot be reached
                // from here; asking the keyword table restores it.  Without
                // this the receiver is a named annotation no scope can answer,
                // and it resolves only by falling through a name lookup into
                // the same primitive table the keyword names directly.
                if (TokenType::from_keyword(this.current_type_name).is_primitive_type()) {
                    const prim_ann: PrimitiveAnnotation* = new PrimitiveAnnotation {
                        name: type_name,
                        span: span,
                    };
                    inner = new TypeAnnotation::Primitive(prim_ann);
                } else if (this.current_type_name == "()") {
                    // The unit target is spelled `()`, which is not a keyword,
                    // so the primitive test above cannot reach it.  A WRITTEN
                    // `()` parses as the empty tuple, and the unit type has no
                    // named form - `ASTTypeSubstituter::rewrite_to_unit` states
                    // the same rule for substitution.  Synthesizing a name here
                    // would give `implement trait Drop for ()` a receiver that
                    // no scope can answer, purely because the receiver is typed
                    // from the target's TEXT rather than its syntax.
""",
     """                // The receiver of `implement string { ... }` is the same
                // `Named` the head is: the name layer answers a primitive's
                // spelling for it as for the head, once every scope declines.
                if (this.current_type_name == "()") {
                    // The unit target is spelled `()`.  A WRITTEN `()` parses as
                    // the empty tuple, and the unit type has no named form -
                    // `ASTTypeSubstituter::rewrite_to_unit` states the same
                    // rule for substitution.  Synthesizing a name here would
                    // give `implement trait Drop for ()` a receiver that no
                    // scope can answer, purely because the receiver is typed
                    // from the target's TEXT rather than its syntax.
"""),
])

# -- the AST -----------------------------------------------------------------
edit("compiler/src/compiler/AST/_module.cryo", [
    ("""type enum TypeAnnotation {
    Primitive(PrimitiveAnnotation*);
    Named(NamedAnnotation*);""",
     """type enum TypeAnnotation {
    /// A written name, a primitive's included: `i32` is a `Named` whose
    /// stamp is `Res::PrimTy`, answered by the name layer once every scope
    /// has declined the spelling.
    Named(NamedAnnotation*);"""),
    ("""            TypeAnnotation::Named(n)      => { intern.resolve(n.name) }
            TypeAnnotation::Primitive(pr) => { intern.resolve(pr.name) }
""",
     """            TypeAnnotation::Named(n)      => { intern.resolve(n.name) }
"""),
    ("""        return match (this) {
            TypeAnnotation::Primitive(prim) => {
                const p: PrimitiveAnnotation* = new PrimitiveAnnotation { name: prim.name, span: prim.span };
                new TypeAnnotation::Primitive(p)
            }
            TypeAnnotation::Named(named) => {""",
     """        return match (this) {
            TypeAnnotation::Named(named) => {"""),
    ("""type struct PrimitiveAnnotation {
    name: SymbolStr;
    span: SourceSpan;
}

""", ""),
])

edit("compiler/src/compiler/AST/dumper.cryo", [
    ("""            TypeAnnotation::Primitive(p) => { fmt::printf("%s", this.resolve(p.name)); }
""", ""),
])

edit("compiler/src/compiler/AST/node_locator.cryo", [
    ("""        TypeAnnotation::Primitive(p) => { p.span }
""", ""),
])

edit("compiler/src/compiler/AST/substituter.cryo", [
    ("    PrimitiveAnnotation, ProjectionAnnotation, ", "    ProjectionAnnotation, "),
    ("import compiler::resolver::res::{ ResBase, ResSlot };",
     "import compiler::resolver::res::{ Res, ResBase, ResSlot };"),
    ("""            TypeAnnotation::Primitive(prim) => {
                // Primitives never contain generic params
                return ann;
            }
""", ""),
    ("""            // Primitive type? -> Primitive annotation
            if (ResBase::is_primitive_spelling(display)) {
                ASTTypeSubstituter::rewrite_to_primitive(ann, this.type_arg_displays[i], named.span);
                return ann;
            }
""",
     """            // A primitive: a Named stamped with the primitive it spells,
            // carrying the argument's TypeRef so it never re-resolves.
            if (ResBase::is_primitive_spelling(display)) {
                ASTTypeSubstituter::rewrite_to_primitive(ann, this.type_arg_displays[i],
                                                         this.resolved_arg_typeref(i), named.span);
                return ann;
            }
"""),
    ("""    /// Rewrite a TypeAnnotation* in-place from Named to Primitive variant.
    static rewrite_to_primitive(ann: TypeAnnotation*, name: SymbolStr, span: SourceSpan) -> void {
        const prim: PrimitiveAnnotation* = new PrimitiveAnnotation { name: name, span: span };
        *ann = TypeAnnotation::Primitive(prim);
    }
""",
     """    /// Rewrite a TypeAnnotation* in-place to the Named form of a primitive:
    /// stamped `PrimTy` at birth, since the substituter knows what it put
    /// there, and carrying the argument's TypeRef as the pointer and array
    /// rewrites do.
    static rewrite_to_primitive(ann: TypeAnnotation*, name: SymbolStr,
                                pre_resolved: TypeRef, span: SourceSpan) -> void {
        const named: NamedAnnotation* = new NamedAnnotation {
            name: name,
            span: span,
            pre_resolved: pre_resolved,
            res: ResSlot::Answered(Res::PrimTy(name)),
        };
        *ann = TypeAnnotation::Named(named);
    }
"""),
    # The pointer and array rewrites mint an inner Named from a display;
    # when that display is a primitive's, the mint says so.
    ("""    static rewrite_to_pointer(ann: TypeAnnotation*, inner_name: SymbolStr,
                                inner_pre_resolved: TypeRef, span: SourceSpan) -> void {
        const inner_named: NamedAnnotation* = new NamedAnnotation {
            name: inner_name,
            span: span,
            pre_resolved: inner_pre_resolved,
            res: ResSlot::Pending,
        };""",
     """    static rewrite_to_pointer(ann: TypeAnnotation*, inner_name: SymbolStr,
                                inner_pre_resolved: TypeRef, span: SourceSpan,
                                intern: InternTable*) -> void {
        const inner_named: NamedAnnotation* = new NamedAnnotation {
            name: inner_name,
            span: span,
            pre_resolved: inner_pre_resolved,
            res: ASTTypeSubstituter::minted_stamp(inner_name, intern),
        };"""),
    ("""    static rewrite_to_array(ann: TypeAnnotation*, inner_name: SymbolStr,
                              inner_pre_resolved: TypeRef, span: SourceSpan) -> void {
        const inner_named: NamedAnnotation* = new NamedAnnotation {
            name: inner_name,
            span: span,
            pre_resolved: inner_pre_resolved,
            res: ResSlot::Pending,
        };""",
     """    static rewrite_to_array(ann: TypeAnnotation*, inner_name: SymbolStr,
                              inner_pre_resolved: TypeRef, span: SourceSpan,
                              intern: InternTable*) -> void {
        const inner_named: NamedAnnotation* = new NamedAnnotation {
            name: inner_name,
            span: span,
            pre_resolved: inner_pre_resolved,
            res: ASTTypeSubstituter::minted_stamp(inner_name, intern),
        };"""),
    ("                ASTTypeSubstituter::rewrite_to_pointer(ann, inner_sym, inner_pre, named.span);",
     "                ASTTypeSubstituter::rewrite_to_pointer(ann, inner_sym, inner_pre, named.span, this.intern_table);"),
    ("                ASTTypeSubstituter::rewrite_to_array(ann, inner_sym, inner_pre, named.span);",
     "                ASTTypeSubstituter::rewrite_to_array(ann, inner_sym, inner_pre, named.span, this.intern_table);"),
    ("""    /// Rewrite a TypeAnnotation* in-place to the empty-tuple (unit) annotation.""",
     """    /// The stamp a minted inner name carries: a primitive's spelling is
    /// answered at the mint - the substituter wrote it and knows - and any
    /// other spelling is `Pending`, for the type layer to place by the
    /// TypeRef the rewrite carries beside it.
    static minted_stamp(name: SymbolStr, intern: InternTable*) -> ResSlot {
        if (ResBase::is_primitive_spelling(intern.resolve(name))) {
            return ResSlot::Answered(Res::PrimTy(name));
        }
        return ResSlot::Pending;
    }

    /// Rewrite a TypeAnnotation* in-place to the empty-tuple (unit) annotation."""),
])

# -- bindgen -----------------------------------------------------------------
edit("compiler/src/compiler/bindgen/type_map.cryo", [
    ("    PointerAnnotation, PrimitiveAnnotation, TypeAnnotation\n",
     "    NamedAnnotation, PointerAnnotation, TypeAnnotation\n"),
    ("""    /// Heap-allocate a PrimitiveAnnotation wrapped in a TypeAnnotation.
    prim(&this, name: string) -> TypeAnnotation* {
        const sym: SymbolStr = this.ctx.intern_table.intern(name);
        const p: PrimitiveAnnotation* = new PrimitiveAnnotation { name: sym, span: SourceSpan::none() };
        const ann: TypeAnnotation* = libc::malloc(sizeof(TypeAnnotation)) as TypeAnnotation*;
        *ann = TypeAnnotation::Primitive(p);
        return ann;
    }
""",
     """    /// Heap-allocate the Named annotation of a primitive, stamped at birth:
    /// the importer knows the C type it mapped, and the stamp is the only
    /// thing naming a synthesized annotation's referent.
    prim(&this, name: string) -> TypeAnnotation* {
        const sym: SymbolStr = this.ctx.intern_table.intern(name);
        const n: NamedAnnotation* = new NamedAnnotation {
            name: sym,
            span: SourceSpan::none(),
            pre_resolved: TypeRef::invalid(),
            res: ResSlot::Answered(Res::PrimTy(sym)),
        };
        const ann: TypeAnnotation* = libc::malloc(sizeof(TypeAnnotation)) as TypeAnnotation*;
        *ann = TypeAnnotation::Named(n);
        return ann;
    }
"""),
])

edit("compiler/src/compiler/bindgen/generator.cryo", [
    ("import compiler::resolver::{ symbol_str, intern_table };\n",
     "import compiler::resolver::{ symbol_str, intern_table };\nimport compiler::resolver::res;\nimport compiler::resolver::res::{ Res, ResSlot };\n"),
    ("""        match (*ann) {
            TypeAnnotation::Primitive(p) => {
                return this.ctx.intern_table.resolve(p.name);
            }
            TypeAnnotation::Pointer(p) => {
                // C const is frontend-only; Cryo FFI lowers `const T*` as `T*`.
                return this.render_type(p.inner) + "*";
            }
            TypeAnnotation::Named(n) => {
""",
     """        match (*ann) {
            TypeAnnotation::Pointer(p) => {
                // C const is frontend-only; Cryo FFI lowers `const T*` as `T*`.
                return this.render_type(p.inner) + "*";
            }
            TypeAnnotation::Named(n) => {
                // A primitive renders as itself: its stamp says so, and its
                // spelling is no keyword to escape.
                match (n.res) {
                    ResSlot::Answered(Res::PrimTy(p)) => { return this.ctx.intern_table.resolve(p); }
                    _ => { }
                }
"""),
])

edit("compiler/src/compiler/bindgen/importer.cryo", [
    ("""    /// True if `ann` denotes `void` (null, or the `void` primitive).
    is_void_annotation(&this, ann: TypeAnnotation*) -> boolean {
        if (ann == null) { return true; }
        match (*ann) {
            TypeAnnotation::Primitive(p) => {
                return this.ctx.intern_table.resolve(p.name).eq("void");
            }
            _ => { return false; }
        }
    }
""",
     """    /// True if `ann` denotes `void` (null, or the `void` primitive the
    /// mapper minted).
    is_void_annotation(&this, ann: TypeAnnotation*) -> boolean {
        if (ann == null) { return true; }
        match (*ann) {
            TypeAnnotation::Named(n) => {
                return this.ctx.intern_table.resolve(n.name).eq("void");
            }
            _ => { return false; }
        }
    }
"""),
])

# -- the constant table --------------------------------------------------------
edit("compiler/src/compiler/const_table.cryo", [
    ("""    static declares_integer(ann: TypeAnnotation*, intern: InternTable*) -> boolean {
        if (ann == null || intern == null) { return false; }
        return match (*ann) {
            TypeAnnotation::Primitive(p) => {
                const n: string = ConstantTable::keyword_primitive(p.name, intern);
                return n == "i8"  || n == "i16" || n == "i32"  || n == "i64"
                    || n == "i128"|| n == "u8"  || n == "u16"  || n == "u32"
                    || n == "u64" || n == "u128"|| n == "isize"
                    || n == "usize" || n == "char" || n == "boolean";
            }
            _ => { false }
        };
    }
""",
     """    static declares_integer(ann: TypeAnnotation*, intern: InternTable*) -> boolean {
        if (ann == null || intern == null) { return false; }
        const n: string = ConstantTable::primitive_name_of(ann, intern);
        return n == "i8"  || n == "i16" || n == "i32"  || n == "i64"
            || n == "i128"|| n == "u8"  || n == "u16"  || n == "u32"
            || n == "u64" || n == "u128"|| n == "isize"
            || n == "usize" || n == "char" || n == "boolean";
    }

    /// The primitive an annotation names, off its stamp, or "" when it names
    /// anything else or has not been stamped.  The stamp is the primitive's
    /// own spelling (an alias keyword folded by the name layer), so no fold
    /// is applied here.
    static primitive_name_of(ann: TypeAnnotation*, intern: InternTable*) -> string {
        if (ann == null || intern == null) { return ""; }
        return match (*ann) {
            TypeAnnotation::Named(n) => {
                match (n.res) {
                    ResSlot::Answered(Res::PrimTy(p)) => { intern.resolve(p) }
                    _                                 => { "" }
                }
            }
            _ => { "" }
        };
    }
"""),
    ("""    /// A keyword annotation's primitive, by its own spelling: an alias
    /// keyword's lexeme (`int`, `uint`) folds to the primitive it spells, so
    /// no width table here carries a second spelling of one type.
    static keyword_primitive(name: SymbolStr, intern: InternTable*) -> string {
        const lexeme: string = intern.resolve(name);
        const canon: string = ResBase::primitive_of_alias(lexeme);
        return if (canon.length() > 0) { canon } else { lexeme };
    }

""", ""),
    # The entry keeps the declared annotation and reads its stamp when it
    # folds: a module-level constant is recorded by the declaration pass,
    # before the walk that stamps its annotation.
    ("""    /// Whether the declaration named an INTEGER primitive.
    ///
    /// The reading of an initializer is the DECLARED TYPE's to choose, not the
    /// expression's: `1 / 2` is 0 in `const H: i64` and 0.5 in `const H: f64`,
    /// so a real fold that re-interprets the initializer of an integer constant
    /// answers with a value that constant never had.  False for a real, for a
    /// non-primitive, and for an absent annotation - all of which leave the
    /// choice to the expression, which is what an unannotated constant wants.
    int_typed: boolean;
}
""",
     """    /// The DECLARED type, read when the constant is folded.
    ///
    /// The reading of an initializer is the declared type's to choose, not the
    /// expression's: `1 / 2` is 0 in `const H: i64` and 0.5 in `const H: f64`,
    /// so a real fold that re-interprets the initializer of an integer constant
    /// answers with a value that constant never had.  A real, a non-primitive
    /// and an absent annotation all leave the choice to the expression, which
    /// is what an unannotated constant wants.  Held as the annotation rather
    /// than as a flag because the constant is recorded before the name layer
    /// has stamped it, and the stamp is what says which primitive it is.
    declared: TypeAnnotation*;
}
"""),
    ("""    register(mut &this, qualified: SymbolStr, init: ExpressionNode*,
             int_typed: boolean) -> void {""",
     """    register(mut &this, qualified: SymbolStr, init: ExpressionNode*,
             declared: TypeAnnotation*) -> void {"""),
    ("                this.entries[existing].int_typed  = int_typed;\n",
     "                this.entries[existing].declared   = declared;\n"),
    ("            int_typed:      int_typed,\n",
     "            declared:       declared,\n"),
    ("        if (this.table.entries[idx].int_typed) {\n",
     "        if (ConstantTable::declares_integer(this.table.entries[idx].declared, this.intern)) {\n"),
    ("""    /// The primitive name a cast targets, or "" when the target is anything
    /// else.  Only a primitive target has a width known without resolving a
    /// name, and a cast to a user type is not an integer anyway.
    primitive_name_of(&this, ann: TypeAnnotation*) -> string {
        if (ann == null || this.intern == null) { return ""; }
        return match (*ann) {
            TypeAnnotation::Primitive(p) => { ConstantTable::keyword_primitive(p.name, this.intern) }
            _ => { "" }
        };
    }

""", ""),
    ("                this.primitive_name_of(c.target_annotation), v, out);",
     "                ConstantTable::primitive_name_of(c.target_annotation, this.intern), v, out);"),
])

edit("compiler/src/compiler/resolver/name_resolution.cryo", [
    ("""        this.ctx.const_table.register(
            qualified, node.initializer,
            ConstantTable::declares_integer(node.type_annotation, intern));""",
     """        this.ctx.const_table.register(qualified, node.initializer, node.type_annotation);"""),
    # The primitive answer moves to where the alias answer is: after every
    # scope has declined the spelling.
    ("""        // A primitive names a builtin rather than a declaration, so the bare
        // lane below - which resolves against the writing module's declared
        // names - can never answer one, and `implement trait Clone for i8`
        // would be left Pending for a name that is not in doubt.  `Res` has no
        // variant meaning "missing", so this is an ANSWER: the spelling names
        // `i8` outright and nothing later can refine it.
        //
        // `is_primitive_spelling` is the authority, and the scope lane asks the
        // same one.  Two predicates for one question is a place for them to
        // disagree silently, and the keyword table cannot be the survivor: `()`
        // is punctuation, so no keyword spells the unit type.
        if (ResBase::is_primitive_spelling(written)) {
            return ResSlot::Answered(Res::PrimTy(name));
        }

""", ""),
    ("""        // Nothing the writer can see binds the leaf.  An alias keyword
        // (`implement trait Show for int`, `new int[100]`) names the
        // primitive it spells ONLY now, after every scope has declined it:
        // a module may own the same name, and the module wins while it is in
        // scope.  The stamp is the primitive's own spelling, never the
        // alias - `int` and `i32` are one type and carry one key.
        const canon: SymbolStr = this.alias_keyword_primitive(written);
        if (!canon.is_valid()) { return ResSlot::Pending; }
        return ResSlot::Answered(Res::PrimTy(canon));
    }
""",
     """        // Nothing the writer can see binds the leaf.  A primitive names a
        // builtin rather than a declaration, so no scope can answer one, and
        // it is answered ONLY now, after every scope has declined it: a type
        // or a module a scope binds under the same spelling wins while it is
        // in scope, as Rust's `struct u8;` does.  One rule for the primitive's
        // own spelling (`i32`) and for an alias keyword (`int`), whose stamp
        // is the primitive's own spelling, never the alias - `int` and `i32`
        // are one type and carry one key.  `is_primitive_spelling` is the
        // authority, and the scope lane asks the same one.
        if (ResBase::is_primitive_spelling(written)) {
            return ResSlot::Answered(Res::PrimTy(name));
        }
        const canon: SymbolStr = this.alias_keyword_primitive(written);
        if (!canon.is_valid()) { return ResSlot::Pending; }
        return ResSlot::Answered(Res::PrimTy(canon));
    }
"""),
])

# An intrinsic's parameter and return annotations were never stamped: as
# keyword primitives they needed no stamp, and the walk skipped them.  As
# Named annotations they are answered like every other.
edit("compiler/src/compiler/resolver/name_resolution.cryo", [
    ("""    override visit(node: IntrinsicDeclNode*) -> void {
        // Already forward-declared; visit parameters/body if needed.
        if (node.has_body()) {""",
     """    override visit(node: IntrinsicDeclNode*) -> void {
        // Already forward-declared.  The signature's annotations are stamped
        // here as a function's are: a primitive (`void*`, `u32*`) is a
        // written name the name layer answers, not a token that needs no
        // answer.
        for (mut pi: int = 0; pi < node.parameters.length; pi++) {
            this.stamp_annotation(node.parameters[pi].type_annotation);
        }
        this.stamp_annotation(node.return_type_annotation);
        if (node.has_body()) {"""),
])

# An enum's discriminant annotation (`type enum Whence : i32`) was never
# stamped either: a keyword primitive needed no stamp, and the walk
# declared the variants and moved on.
edit("compiler/src/compiler/resolver/name_resolution.cryo", [
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

edit("compiler/src/compiler/resolver/res.cryo", [
    ("""            || name.eq("f64")
            || name.eq("never");
    }""",
     """            || name.eq("f64")
            || name.eq("never")
            || name.eq("va_list");
    }"""),
])

# -- the type layer ------------------------------------------------------------
edit("compiler/src/compiler/passes/type_resolution.cryo", [
    ("""            TypeAnnotation::Primitive(p) => { ctx.type_resolver.resolve_primitive(p.name) }
            TypeAnnotation::Generic(g)   => { TypeResolutionPasses::item_type_of(g.base, ctx) }""",
     """            TypeAnnotation::Generic(g)   => { TypeResolutionPasses::item_type_of(g.base, ctx) }"""),
    ("""            TypeAnnotation::Primitive(p) => {
                // A keyword primitive carries no slot; its lexeme is read
                // as the name layer would stamp it, an alias keyword
                // (`Feed<int>`) under the primitive's own spelling, so
                // `Feed<int>` and `Feed<i32>` render to one key.
                mut prim: SymbolStr = p.name;
                const canon: string = ResBase::primitive_of_alias(ctx.intern_table.resolve(p.name));
                if (canon.length() > 0) { prim = ctx.intern_table.intern(canon); }
                TypeResolutionPasses::type_name_key(prim, Res::PrimTy(prim), node, ctx)
            }
            TypeAnnotation::Named(n) => {
                // Through the door:""",
     """            TypeAnnotation::Named(n) => {
                // Through the door:"""),
])

edit("compiler/src/compiler/sema/method_binding.cryo", [
    ("""            TypeAnnotation::Primitive(p) => { this.ctx.type_resolver.resolve_primitive(p.name) }
            TypeAnnotation::Generic(g)   => { this.item_type_of(g.base) }""",
     """            TypeAnnotation::Generic(g)   => { this.item_type_of(g.base) }"""),
])

edit("compiler/src/compiler/types/generic_registry.cryo", [
    ("""            TypeAnnotation::Primitive(p) => {
                const k: SymbolStr = arena.template_key_of(arg, intern);
                return k.is_valid() && k.equals(p.name);
            }
            TypeAnnotation::Generic(g) => {
                if (!GenericRegistry::annotation_unifies(g.base, arg, arena, intern)) { return false; }""",
     """            TypeAnnotation::Generic(g) => {
                if (!GenericRegistry::annotation_unifies(g.base, arg, arena, intern)) { return false; }"""),
    ("""                    TypeAnnotation::Primitive(q) => {
                        match (n.res.require("generic_registry:overlap head argument")) {
                            Res::PrimTy(p) => { p.equals(q.name) }
                            _              => { false }
                        }
                    }
                    TypeAnnotation::Generic(h) => {
                        h.args.length == 0 && GenericRegistry::head_unify(xa, h.base, bindings)""",
     """                    TypeAnnotation::Generic(h) => {
                        h.args.length == 0 && GenericRegistry::head_unify(xa, h.base, bindings)"""),
    ("""            TypeAnnotation::Primitive(p) => {
                match (*ya) {
                    TypeAnnotation::Primitive(q) => { p.name.equals(q.name) }
                    TypeAnnotation::Named(m) => {
                        match (m.res.require("generic_registry:overlap head argument")) {
                            Res::PrimTy(q) => { q.equals(p.name) }
                            _              => { false }
                        }
                    }
                    _ => { false }
                }
            }
            TypeAnnotation::Generic(g) => {""",
     """            TypeAnnotation::Generic(g) => {"""),
])

edit("compiler/src/compiler/types/resolver.cryo", [
    ("""        return match (*ann) {
            TypeAnnotation::Primitive(p) => { this.resolve_primitive(p.name) }
            TypeAnnotation::Named(n) => {
                if (n.pre_resolved.is_valid()) { return n.pre_resolved; }""",
     """        return match (*ann) {
            TypeAnnotation::Named(n) => {
                if (n.pre_resolved.is_valid()) { return n.pre_resolved; }"""),
    ("""        // 2. A primitive spelling that arrived as a Named annotation.
        //    Substituting a primitive into a generic does NOT produce one:
        //    the rewrite replaces the whole annotation with a Primitive node,
        //    which never enters this cascade.  What does produce one is the
        //    pointer and array rewrite putting a primitive back into Named
        //    position - `T = i32*` becomes `Pointer(Named("i32"))` - and every
        //    such node carries the pointee's TypeRef, so it answers at the
        //    short-circuit in `resolve` and never reaches here either.
        //
        //    This step is therefore the recovery for one case: a mint whose
        //    carried TypeRef is INVALID, which happens when the unwrap
        //    supplying it declines.  It answers zero while no unwrap declines.
        const prim: TypeRef = this.resolve_primitive(name);
        if (prim.is_valid()) {
            return prim;
        }
""",
     """        // 2. A primitive, off the stamp: the name layer answered `PrimTy`
        //    for a written spelling once every scope declined it, and a mint
        //    (the substituter's, bindgen's) answered its own at birth.  The
        //    spelling is not asked: a scope that binds `i32` to a declaration
        //    stamps `Def`, and that declaration is what the name means there.
        match (slot) {
            ResSlot::Answered(Res::PrimTy(p)) => { return this.resolve_primitive(p); }
            _                                 => { }
        }
"""),
])

# -- the LSP ---------------------------------------------------------------------
edit("tools/CryoLSP/src/handlers/hover.cryo", [
    ("""            ast::TypeAnnotation::Primitive(p) => { this.intern.resolve(p.name) }
            ast::TypeAnnotation::Named(n)     => {""",
     """            ast::TypeAnnotation::Named(n)     => {"""),
    ("""                ast::TypeAnnotation::Named(n)     => { n.name }
                ast::TypeAnnotation::Primitive(p) => { p.name }
                _                            => { symbol_str::SymbolStr::empty() }""",
     """                ast::TypeAnnotation::Named(n)     => { n.name }
                _                            => { symbol_str::SymbolStr::empty() }"""),
])

edit("tools/CryoLSP/src/handlers/semantic_tokens.cryo", [
    ("""        match (*ann) {
            ast::TypeAnnotation::Primitive(p) => {
                // Primitives (`i32`, `bool`, ...) are TextMate-coloured as
                // types already; emitting again here would double-paint.
                // Skip.
            }
            ast::TypeAnnotation::Named(n) => {
                if (n.span.has_location()) {""",
     """        match (*ann) {
            ast::TypeAnnotation::Named(n) => {
                // A primitive is a `Named` now; its span is painted as a
                // type like any other name's, which is what it is.
                if (n.span.has_location()) {"""),
])

print("retire_primitive_annotation: done")
