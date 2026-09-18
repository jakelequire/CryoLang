"""D23 scoping probe: print the identifier at the three refused-only arms."""
import io, sys
R = r"C:\Programming\apps\CryoLang\compiler\src\compiler"

def edit(path, pairs):
    src = io.open(path, encoding="utf-8", newline="").read()
    for old, new in pairs:
        assert src.count(old) == 1, (path, src.count(old), old[:80])
        src = src.replace(old, new)
    io.open(path, "w", encoding="utf-8", newline="").write(src)

CR = R + r"\sema\call_resolver.cryo"
TR = R + r"\passes\type_resolution.cryo"
pairs_cr = [
("""        const t: TemplateEntry* = match (ident.res) {
            ResSlot::Answered(r) => { this.lookup_scope_template(r) }
            _                    => { null }
        };
""",
"""        const t: TemplateEntry* = match (ident.res) {
            ResSlot::Answered(r) => { this.lookup_scope_template(r) }
            _                    => { fmt::eprintf("SHADOW\\tD23-TMPL\\t%s\\n", this.intern.resolve(ident.name)); null }
        };
"""),
("""                        Res::Def(_) => { return this.ctx.decl_index.func_type_of_res(r); }
                        _           => { return TypeRef::invalid(); }
                    }
                }
                _ => { return TypeRef::invalid(); }
            }
""",
"""                        Res::Def(_) => { return this.ctx.decl_index.func_type_of_res(r); }
                        _           => { return TypeRef::invalid(); }
                    }
                }
                _ => { fmt::eprintf("SHADOW\\tD23-HINT\\t%s\\n", this.intern.resolve(ident.name)); return TypeRef::invalid(); }
            }
"""),
]
pairs_tr = [
("""                const r: Res = match (n.res) {
                    ResSlot::Answered(a) => { a }
                    _                    => { Res::Err }
                };
                TypeResolutionPasses::type_name_key(n.name, r, node, ctx)
""",
"""                const r: Res = match (n.res) {
                    ResSlot::Answered(a) => { a }
                    _                    => { fmt::eprintf("SHADOW\\tD23-CANON\\t%s\\n", ctx.intern_table.resolve(n.name)); Res::Err }
                };
                TypeResolutionPasses::type_name_key(n.name, r, node, ctx)
"""),
]
if sys.argv[1] == "flip":
    edit(CR, pairs_cr); edit(TR, pairs_tr)
else:
    edit(CR, [(b, a) for a, b in pairs_cr]); edit(TR, [(b, a) for a, b in pairs_tr])
print(sys.argv[1], "ok")
