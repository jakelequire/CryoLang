"""The type lane's refusals, printed where they are made, one line per
diagnostic the name layer emits for a written type spelling it could not
answer - and one per residual the type layer still reports:

    SHADOW<TAB>NL-TYPE<TAB><code><TAB><file>:<line><TAB><spelling>
    SHADOW<TAB>NL-HEAD<TAB>E0302<TAB><file>:<line><TAB><names>
    SHADOW<TAB>NL-PATH<TAB><code><TAB><file>:<line><TAB><leaf>
    SHADOW<TAB>TR-RESIDUAL<TAB><file>:<line><TAB><kind> <decl>

NL-TYPE: `refuse_unbound_type` (a bare or qualified spelling nothing binds);
NL-HEAD: an impl head's undeclared target argument; NL-PATH: the
module-rooted walk's own refusals (a facade offering a leaf twice, a private
declaration named by a path); TR-RESIDUAL: `emit_undefined_type`'s residual,
an annotation whose every name is placed and that still names no type.

Over the six halves the lines are expected ONLY on refused programs - every
green half must print nothing, since a refusal is a diagnostic.  Applied over
the tree as it stands; `--revert` removes it.
"""
import io, sys

NR = r"C:\Programming\apps\CryoLang\compiler\src\compiler\resolver\name_resolution.cryo"
TR = r"C:\Programming\apps\CryoLang\compiler\src\compiler\passes\type_resolution.cryo"
revert = "--revert" in sys.argv


def region(text, head, tail):
    """The [start, end) of the text from the line containing `head` up to the
    line containing `tail` (exclusive), both required to exist once."""
    s = text.index(head)
    e = text.index(tail, s)
    return s, e


def patch_region(text, head, tail, old, new):
    s, e = region(text, head, tail)
    body = text[s:e]
    n = body.count(old)
    assert n > 0, (head, old)
    return text[:s] + body.replace(old, new) + text[e:], n


NL_TYPE = ('fmt::eprintf("SHADOW\\tNL-TYPE\\t%s\\t%s:%u\\t%s\\n", '
           'd.code.format(), span.file, span.start_line, written); '
           'this.ctx.emit_diagnostic(d);')
NL_HEAD = ('fmt::eprintf("SHADOW\\tNL-HEAD\\tE0302\\t%s:%u\\t%s\\n", '
           'span.file, span.start_line, listed); '
           'this.ctx.emit_diagnostic(diag);')
NL_PATH = ('fmt::eprintf("SHADOW\\tNL-PATH\\t%s\\t%s:%u\\t%s\\n", '
           'd.code.format(), span.file, span.start_line, leaf); '
           'this.ctx.emit_diagnostic(d);')
TR_RES = ('fmt::eprintf("SHADOW\\tTR-RESIDUAL\\t%s:%u\\t%s %s\\n", '
          'fallback_span.file, fallback_span.start_line, kind_label, decl_name); '
          'this.ctx.emit_error_at(ErrorCode::E0203_UNDEFINED_TYPE,')

EDITS = [
    (NR, "    refuse_unbound_type(&this, name: SymbolStr, span: SourceSpan) -> boolean {",
     "    override visit(node: IdentifierNode*) -> void {",
     "this.ctx.emit_diagnostic(d);", NL_TYPE, 6),
    (NR, "    stamp_impl_target_args(&this, node: ImplBlockNode*) -> void {",
     "    override visit(node: VarDeclNode*) -> void {",
     "this.ctx.emit_diagnostic(diag);", NL_HEAD, 1),
    (NR, "    walk_module_rooted_type(&this, written: string,",
     "    /// Record which MODULE the scope segment of `Ns::member` names",
     "this.ctx.emit_diagnostic(d);", NL_PATH, 3),
    (TR, "    emit_undefined_type(&this, ann: TypeAnnotation*,",
     "    // Signature and field resolution helpers",
     "this.ctx.emit_error_at(ErrorCode::E0203_UNDEFINED_TYPE,", TR_RES, 1),
]

for path, head, tail, old, new, want in EDITS:
    text = io.open(path, encoding="utf-8", newline="").read()
    if revert:
        assert new in text, ("not applied", path, head)
        text = text.replace(new, old)
    else:
        assert new not in text, ("already applied", path, head)
        text, n = patch_region(text, head, tail, old, new)
        assert n == want, (head, n, want)
    io.open(path, "w", encoding="utf-8", newline="").write(text)
print("reverted" if revert else "applied")
