#!/usr/bin/env python3
"""D31's retirement: the lookahead spelling `Name<T>` in expression position
and everything that decided it - `is_generic_call_ahead` (six askers), the
module tables `local_names` / `global_value_names` / `generic_decl_names`
with their writers and readers (`scan_module_names`, `note_local_name`,
`reset_local_names`, `is_local_name`, `is_global_value_name`,
`is_generic_decl_name`, `binds_a_value`), and the `generic_angle_guessed`
flag with the two notes it fed.  After it the parser opens generic arguments
in expression position at `::<` alone; type position and `new Type<T>` open
`<` outright, since no comparison can stand there.

    python scripts/ns-migration/8.278/retire_lookahead.py

Run over the tree AFTER `turbofish.py apply` (the sources must already spell
`::<`, or the compiler this produces refuses its own stdlib).  Every
replacement asserts its match count before anything is saved.
"""
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
P = os.path.join(ROOT, "compiler", "src", "compiler")

EDITS = []  # (path, old, new, count)


def edit(path, old, new, count=1):
    EDITS.append((os.path.join(P, path), old, new, count))


# ---- expr_parser.cryo -------------------------------------------------------
X = "parser/expr_parser.cryo"

edit(X, '''        // Generic arguments on the name: `Name::<T, U>`.
        mut turbofish: boolean = false;
        if (this.at_turbofish()) {
            this.advance(); // consume '::'
            turbofish = true;
        }
        // The lookahead spelling `Name<T, U>` is still read while the pin
        // predates the turbofish; the tree is rewritten and this arm retired
        // once the pin reads `::<`.
        //
        // A name bound to a value - a parameter, a local, or a module-level
        // global - is not a generic function or type, so its `<` is a
        // comparison however much the following tokens resemble type
        // arguments. This is what settles `f(a < b, c > (d))`, which the token
        // scan alone reads as the generic call `a<b, c>(d)`. Names that bind
        // no known value - including everything imported - still go to the
        // scan.
        if (turbofish || (this.check(TokenType::LAngle) && !this.binds_a_value(name) &&
                          this.is_generic_call_ahead())) {
            // Record whether this was knowledge or a guess. Knowing the name is
            // declared generic in this module settles it; otherwise only the
            // token scan spoke, and the same text may have been comparisons.
            if (!turbofish && !this.is_generic_decl_name(name)) {
                (expr as IdentifierNode*).set_generic_angle_guessed(true);
            }
            return this.parse_generic_head(expr, name, tok);
        }
        return this.parse_identifier_tail(expr, name, tok);
''', '''        // Generic arguments on the name: `Name::<T, U>`.  A bare `<` after a
        // name is a comparison, whatever the name binds.
        if (this.at_turbofish()) {
            this.advance(); // consume '::'
            return this.parse_generic_head(expr, name, tok);
        }
        return this.parse_identifier_tail(expr, name, tok);
''')

edit(X, '''        // Optional generic args on the type: new Type<T>(...)
        // Scope `scope_gen_args` to this branch: it is moved into the node
        // unconditionally here and never read afterwards, so a branch-local
        // binding leaves nothing conditionally moved past the if.
        if (this.check(TokenType::LAngle) && this.is_generic_call_ahead()) {
''', '''        // Optional generic args on the type: `new Type<T>(...)`.  The operand
        // of `new` is a type, so a `<` here is never a comparison.
        // Scope `scope_gen_args` to this branch: it is moved into the node
        // unconditionally here and never read afterwards, so a branch-local
        // binding leaves nothing conditionally moved past the if.
        if (this.check(TokenType::LAngle)) {
''')

edit(X, '''        // Explicit type arguments on the member: `obj.method::<T, U>(...)`.
        // The lookahead spelling `obj.method<T>(..)` is read until the pin
        // reads the turbofish.
        mut turbofish: boolean = false;
        if (this.at_turbofish()) {
            this.advance(); // consume '::'
            turbofish = true;
        }
        if (turbofish || (this.check(TokenType::LAngle) && this.is_generic_call_ahead())) {
            const member_gen_args: TypeAnnotation*[] = this.parse_generic_args();
            node.set_generic_args(member_gen_args);
        }
''', '''        // Explicit type arguments on the member: `obj.method::<T, U>(...)`.
        if (this.at_turbofish()) {
            this.advance(); // consume '::'
            const member_gen_args: TypeAnnotation*[] = this.parse_generic_args();
            node.set_generic_args(member_gen_args);
        }
''')

edit(X, '''        // Generic args on the member itself: `Foo::bar::<U>`.  The lookahead
        // spelling `Foo::bar<U>` is read until the pin reads the turbofish.
        mut turbofish: boolean = false;
        if (this.at_turbofish()) {
            this.advance(); // consume '::'
            turbofish = true;
        }
        if (turbofish || (this.check(TokenType::LAngle) && this.is_generic_call_ahead())) {
            const member_gen_args: TypeAnnotation*[] = this.parse_generic_args();
            sr.set_generic_args(member_gen_args);
        }
''', '''        // Generic args on the member itself: `Foo::bar::<U>`.
        if (this.at_turbofish()) {
            this.advance(); // consume '::'
            const member_gen_args: TypeAnnotation*[] = this.parse_generic_args();
            sr.set_generic_args(member_gen_args);
        }
''')

# The type-position site: a `<` after a type name is never a comparison.
edit(X, '''            // Check for generic parameters: Type<A, B>
            if (this.check(TokenType::LAngle) && this.is_generic_call_ahead()) {
''', '''            // Generic arguments: `Type<A, B>`.  Type position - a `<` after a
            // type name is never a comparison.
            if (this.check(TokenType::LAngle)) {
''')

# The literal-in-generic-args diagnostic no longer has a comparison to blame.
edit(X, '''            // A literal can never begin a type, so the `<` was a comparison
            // that the lookahead misread - `f(x < 2, 3 > (4))` being the usual
            // shape. Diagnosed here rather than in `parse_type_annotation`,
            // which would first report a bare "expected type" from a position
            // where the reader has no reason to expect one, leaving two errors
            // for one mistake.
''', '''            // A literal can never begin a type.  Diagnosed here rather than in
            // `parse_type_annotation`, which would first report a bare
            // "expected type" from a position where the reader has no reason
            // to expect one, leaving two errors for one mistake.
''')
edit(X, '''                    fmt::format("expected a type in generic arguments, found the value `%s`; if `<` was meant as a comparison, parenthesise the operands - `(x < y)`",
''', '''                    fmt::format("expected a type in generic arguments, found the value `%s`",
''')

# is_generic_call_ahead itself: the method and its doc comment, whole.
edit(X, re.compile(
    r"    /// Scan forward to determine if `<` begins generic type arguments\..*?"
    r"    is_generic_call_ahead\(\) -> boolean \{.*?\n    \}\n\n", re.S), "", count=1)

# ---- parser_base.cryo -------------------------------------------------------
B = "parser/parser_base.cryo"
edit(B, '''    // Names bound as parameters or locals in the function being parsed, used
    // to settle `ident <` in expression position.  `f(a < b, c > (d))` is a
    // generic call and a pair of comparisons under the same grammar, and the
    // token scan cannot tell them apart; knowing `a` names a local settles it,
    // because a local is not a generic function or type.
    //
    // Reset per parameter list, so a name is only ever consulted inside the
    // function that bound it.  Deliberately flat rather than a scope stack: a
    // name declared in an inner block stays listed for the rest of the
    // function, which only ever biases toward the comparison reading, and
    // calling a generic whose name a local in the same function shadows is
    // pathological either way.
    local_names:   SymbolStr[];
    // Module-level names collected up front by `scan_module_names`, which
    // settles the same `ident <` question for names that are not locals.
    // Gathered by a token pre-scan rather than as declarations are parsed
    // because the parse is single-pass: a global or generic declared further
    // down the file must still be known to a function above it.
    //
    // `global_value_names` bind values, so their `<` is a comparison;
    // `generic_decl_names` are declared with type parameters, so theirs opens
    // type arguments. A name in neither - anything imported, most obviously -
    // falls through to the token scan.
    global_value_names: SymbolStr[];
    generic_decl_names: SymbolStr[];
''', '')
edit(B, '''        this.local_names  = [];
        this.global_value_names = [];
        this.generic_decl_names = [];
''', '')
edit(B, re.compile(
    r"    /// Forget the previous function's bindings\. Called when a parameter list\n"
    r".*?"
    r"    scan_module_names\(\) -> void \{.*?\n    \}\n\n", re.S), "", count=1)

# ---- parser.cryo ------------------------------------------------------------
R = "parser/parser.cryo"
edit(R, '''        // Module-level names first: the parse is single-pass, but deciding
        // whether an `ident <` opens type arguments needs to know about
        // declarations further down the file.
        this.scan_module_names();

''', '')
edit(R, '''        const name: SymbolStr = this.intern_lexeme(name_tok);
        // Only inside a function body: at depth 0 this is a module-level
        // global, which shares a namespace with functions and types and so
        // says nothing about whether a later `ident <` opens type arguments.
        if (this.scope_depth > 0) {
            this.note_local_name(name);
        }
''', '''        const name: SymbolStr = this.intern_lexeme(name_tok);
''')
edit(R, '''        this.pending_named_variadic = false;
        // Every function and method reaches its body through here, so this is
        // where the previous one's bindings stop being in scope.
        this.reset_local_names();
''', '''        this.pending_named_variadic = false;
''')
edit(R, '''            if (param != null) {
                this.note_local_name(param.name);
                params.push(param);
''', '''            if (param != null) {
                params.push(param);
''')

# ---- the guessed flag and its two notes ------------------------------------
A = "AST/expression.cryo"
edit(A, '''    /// True when a following `<` was read as opening type arguments on a
    /// GUESS - the name is not a known local, global, or generic declared in
    /// this module, so the parser fell back to inspecting the tokens after the
    /// matching `>`. The same text is also a valid chain of comparisons, so a
    /// downstream "no such function" is as likely to mean the guess was wrong
    /// as that the function is missing; diagnostics say so rather than sending
    /// the reader after a function they never meant to call.
    generic_angle_guessed: boolean;

''', "")
edit(A, "        this.generic_angle_guessed = false;\n", "")
edit(A, re.compile(r"    set_generic_angle_guessed\(v: boolean\) -> void \{\n"
                   r"        this\.generic_angle_guessed = v;\n    \}\n\n?", re.S), "", count=1)

N = "resolver/name_resolution.cryo"
edit(N, '''        if (as_callee) {
            // The call may not be a call at all: `f(a < b, c > (d))` reads
            // as the generic call `a<b, c>(d)` under the same grammar, and
            // that reading is what produced this lookup.  Say so, because the
            // reader is otherwise sent looking for a function they never
            // wrote.
            if (node.generic_angle_guessed) {
                d = d.with_note(fmt::format(
                    "`<` after `%s` was read as the start of generic type arguments; if a comparison was meant, parenthesise the operands - `(x < y)`",
                    name));
            }
''', '''        if (as_callee) {
''')

C = "sema/call_resolver.cryo"
edit(C, '''        diag = diag.at_with_label(ident.span, "not found in this scope");
        // The call may not be a call at all: `f(a < b, c > (d))` reads as the
        // generic call `a<b, c>(d)` under the same grammar, and that reading is
        // what produced this lookup. Say so, because the reader is otherwise
        // sent looking for a function they never wrote.
        if (ident.generic_angle_guessed) {
            diag = diag.with_note(fmt::format(
                "`<` after `%s` was read as the start of generic type arguments; if a comparison was meant, parenthesise the operands - `(x < y)`",
                name));
        }
        this.ctx.emit_diagnostic(diag);
''', '''        diag = diag.at_with_label(ident.span, "not found in this scope");
        this.ctx.emit_diagnostic(diag);
''')


def main():
    files = {}
    for path, old, new, count in EDITS:
        if path not in files:
            with open(path, "r", encoding="utf-8", newline="") as f:
                files[path] = f.read()
        s = files[path]
        if isinstance(old, str):
            n = s.count(old)
            assert n == count, "%s: %d match(es) of %r, expected %d" % (path, n, old[:60], count)
            s = s.replace(old, new)
        else:
            n = len(old.findall(s))
            assert n == count, "%s: %d match(es) of /%s/, expected %d" % (path, n, old.pattern[:60], count)
            s = old.sub(new, s)
        files[path] = s
    for path, s in files.items():
        for gone in ("is_generic_call_ahead", "binds_a_value", "is_generic_decl_name", "is_local_name",
                     "is_global_value_name", "scan_module_names", "note_local_name", "reset_local_names",
                     "local_names", "global_value_names", "generic_decl_names", "generic_angle_guessed"):
            assert not re.search(r"\b" + gone + r"\b", s), "%s still mentions %s" % (path, gone)
        with open(path, "w", encoding="utf-8", newline="") as f:
            f.write(s)
    print("retire_lookahead: %d edits over %d files" % (len(EDITS), len(files)))


if __name__ == "__main__":
    main()
