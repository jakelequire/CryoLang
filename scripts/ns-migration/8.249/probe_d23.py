"""D23 probe: a print in each of the two call-resolver arms that answer a
Pending callee slot silently (`find_fn_template_for_call`,
`lookup_callee_function_type`'s hint).  `--revert` removes them."""
import io, sys
CALL = r"C:\Programming\apps\CryoLang\compiler\src\compiler\sema\call_resolver.cryo"
revert = "--revert" in sys.argv

EDITS = [
 (CALL,
  """        const t: TemplateEntry* = match (ident.res) {
            ResSlot::Answered(r) => { this.lookup_scope_template(r) }
            _                    => { null }
        };
""",
  """        const t: TemplateEntry* = match (ident.res) {
            ResSlot::Answered(r) => { this.lookup_scope_template(r) }
            _                    => {
                fmt::eprintf("SHADOW\\tD23-TEMPLATE-PENDING\\t%s\\t%s:%d\\n", this.intern.resolve(ident.name), ident.span.file, ident.span.start_line);
                null
            }
        };
"""),
 (CALL,
  """                        Res::Def(_) => { return this.ctx.decl_index.func_type_of_res(r); }
                        _           => { return TypeRef::invalid(); }
                    }
                }
                _ => { return TypeRef::invalid(); }
""",
  """                        Res::Def(_) => { return this.ctx.decl_index.func_type_of_res(r); }
                        _           => { return TypeRef::invalid(); }
                    }
                }
                _ => {
                    fmt::eprintf("SHADOW\\tD23-HINT-PENDING\\t%s\\t%s:%d\\n", this.intern.resolve(ident.name), ident.span.file, ident.span.start_line);
                    return TypeRef::invalid();
                }
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
