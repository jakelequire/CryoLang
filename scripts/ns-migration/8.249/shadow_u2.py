"""Reporting-move shadow, applied OVER the final code: a print at the name
layer's refusal (NL-E0201 / NL-E0202), at each of sema's two residual
reports (SEMA-RESIDUAL-E0201 / -E0202) and at each of sema's two `require`
doors when the slot is Pending (SEMA-PENDING).  `--revert` removes them.
`--mutate` instead applies the three controls (a refused import binding
nothing, a nested pattern not descended, no receiver declared) so each
refusal the final code prevents can be shown firing."""
import io, sys
NL = r"C:\Programming\apps\CryoLang\compiler\src\compiler\resolver\name_resolution.cryo"
SEMA = r"C:\Programming\apps\CryoLang\compiler\src\compiler\sema\sema.cryo"
CALL = r"C:\Programming\apps\CryoLang\compiler\src\compiler\sema\call_resolver.cryo"
revert = "--revert" in sys.argv
mutate = "--mutate" in sys.argv

SHADOW = [
 (NL,
  """        this.ctx.emit_diagnostic(d);
        node.set_res(Res::Err);
    }
""",
  """        this.ctx.emit_diagnostic(d);
        fmt::eprintf("SHADOW\\tNL-%s\\t%s\\t%s:%d\\n", if (as_callee) { "E0202" } else { "E0201" }, name, node.span.file, node.span.start_line);
        node.set_res(Res::Err);
    }
"""),
 (SEMA,
  """        match (ident.res.require("sema/resolve_identifier")) {
            Res::Err => { return TypeRef::invalid(); }
            _        => { }
        }
        mut diag: Diagnostic = Diagnostic::error(ErrorCode::E0201_UNDEFINED_VARIABLE,
""",
  """        if (ident.res.is_pending()) { fmt::eprintf("SHADOW\\tSEMA-PENDING\\t%s\\t%s:%d\\n", name_str, ident.span.file, ident.span.start_line); }
        match (ident.res.require("sema/resolve_identifier")) {
            Res::Err => { return TypeRef::invalid(); }
            _        => { }
        }
        fmt::eprintf("SHADOW\\tSEMA-RESIDUAL-E0201\\t%s\\t%s:%d\\n", name_str, ident.span.file, ident.span.start_line);
        mut diag: Diagnostic = Diagnostic::error(ErrorCode::E0201_UNDEFINED_VARIABLE,
"""),
 (CALL,
  """        match (ident.res.require("sema/resolve_direct_call")) {
            Res::Err => { return TypeRef::invalid(); }
            _        => { }
        }
        const name: string = this.intern.resolve(ident.name);
""",
  """        if (ident.res.is_pending()) { fmt::eprintf("SHADOW\\tSEMA-PENDING\\t%s\\t%s:%d\\n", this.intern.resolve(ident.name), ident.span.file, ident.span.start_line); }
        match (ident.res.require("sema/resolve_direct_call")) {
            Res::Err => { return TypeRef::invalid(); }
            _        => { }
        }
        const name: string = this.intern.resolve(ident.name);
        fmt::eprintf("SHADOW\\tSEMA-RESIDUAL-E0202\\t%s\\t%s:%d\\n", name, ident.span.file, ident.span.start_line);
"""),
]

MUTATIONS = [
 # 1. the private import binds nothing
 (NL,
  """                    this.ctx.emit_diagnostic(d);
                    // The name is bound to nothing, HERE: a use of it is then
                    // `Err` with this as its report, not a second diagnostic
                    // saying the name does not exist.
                    this.resolver.declare_refused(name, node.span);
""",
  """                    this.ctx.emit_diagnostic(d);
"""),
 # 2. a nested pattern is not descended
 (NL,
  """            const sub: PatternNode* = node.sub_at(i);
            if (sub != null) { sub.accept(this); }
""",
  """"""),
 # 3. no receiver is declared
 (NL,
  """            if (!written_this) {
                this.resolver.declare_parameter(this.this_sym, node.span);
            }
""",
  """"""),
]

def apply(edits):
    for path, old, new in edits:
        src = io.open(path, encoding="utf-8", newline="").read()
        nl = "\r\n" if "\r\n" in src else "\n"
        o = old.replace("\n", nl); n = new.replace("\n", nl)
        if revert:
            assert src.count(n) == 1, (path, "revert", src.count(n), new[:60])
            src = src.replace(n, o)
        else:
            assert src.count(o) == 1, (path, src.count(o), old[:60])
            src = src.replace(o, n)
        io.open(path, "w", encoding="utf-8", newline="").write(src)

apply(MUTATIONS if mutate else SHADOW)
print(("reverted " if revert else "applied ") + ("mutations" if mutate else "shadow"))
