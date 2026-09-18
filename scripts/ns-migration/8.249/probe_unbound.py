"""Reporting-move probe (no behaviour change): print every bare identifier
the name layer's lookup misses (NL-UNBOUND) or finds but leaves Pending
(NL-MEMBER), and every E0201 / E0202 sema reports for a bare identifier,
so the two populations can be compared file:line by file:line.
`--revert` removes the prints."""
import io, sys
NL = r"C:\Programming\apps\CryoLang\compiler\src\compiler\resolver\name_resolution.cryo"
SEMA = r"C:\Programming\apps\CryoLang\compiler\src\compiler\sema\sema.cryo"
CALL = r"C:\Programming\apps\CryoLang\compiler\src\compiler\sema\call_resolver.cryo"
revert = "--revert" in sys.argv

EDITS = [
 (NL,
  """            LOG_DEBUG(LogComponent::General, "[NameResolver] Identifier '%s' not found in scope", this.resolver.intern_table.resolve(node.name));
""",
  """            LOG_DEBUG(LogComponent::General, "[NameResolver] Identifier '%s' not found in scope", this.resolver.intern_table.resolve(node.name));
            fmt::eprintf("SHADOW\\tNL-UNBOUND\\t%s\\t%s:%d\\n", this.resolver.intern_table.resolve(node.name), node.span.file, node.span.start_line);
"""),
 (NL,
  """        match (this.bare_name_res(sym_id)) {
            ResSlot::Answered(r) => { node.set_res(r); }
            _                    => { }
        }
        this.stamp_annotation_list(&node.generic_args);
    }
""",
  """        match (this.bare_name_res(sym_id)) {
            ResSlot::Answered(r) => { node.set_res(r); }
            _                    => {
                if (sym_id.is_valid()) {
                    fmt::eprintf("SHADOW\\tNL-MEMBER\\t%s\\t%s:%d\\n", this.resolver.intern_table.resolve(node.name), node.span.file, node.span.start_line);
                }
            }
        }
        this.stamp_annotation_list(&node.generic_args);
    }
"""),
 (SEMA,
  """        mut diag: Diagnostic = Diagnostic::error(ErrorCode::E0201_UNDEFINED_VARIABLE,
            fmt::format("cannot find value `%s` in this scope", name_str));
""",
  """        fmt::eprintf("SHADOW\\tSEMA-E0201\\t%s\\t%s:%d\\n", name_str, ident.span.file, ident.span.start_line);
        mut diag: Diagnostic = Diagnostic::error(ErrorCode::E0201_UNDEFINED_VARIABLE,
            fmt::format("cannot find value `%s` in this scope", name_str));
"""),
 (CALL,
  """        const name: string = this.intern.resolve(ident.name);
        mut diag: Diagnostic = Diagnostic::error(
            ErrorCode::E0202_UNDEFINED_FUNCTION,
""",
  """        const name: string = this.intern.resolve(ident.name);
        fmt::eprintf("SHADOW\\tSEMA-E0202\\t%s\\t%s:%d\\n", name, ident.span.file, ident.span.start_line);
        mut diag: Diagnostic = Diagnostic::error(
            ErrorCode::E0202_UNDEFINED_FUNCTION,
"""),
]

for path, old, new in EDITS:
    src = io.open(path, encoding="utf-8", newline="").read()
    nl = "\r\n" if "\r\n" in src else "\n"
    o = old.replace("\n", nl); n = new.replace("\n", nl)
    if revert:
        assert src.count(n) == 1, (path, "revert", src.count(n))
        src = src.replace(n, o)
    else:
        assert src.count(o) == 1, (path, src.count(o), old[:60])
        src = src.replace(o, n)
    io.open(path, "w", encoding="utf-8", newline="").write(src)
print("reverted" if revert else "probed")
