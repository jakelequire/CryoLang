"""The function registry's entry carries its declaration's span:
register_signature(..., span) writes `overload_func_span`, `entry_span(id)`
reads it; every registrar hands the node's span over."""
import io
def load(p): return io.open(p, encoding="utf-8", newline="").read()
def save(p, t): io.open(p, "w", encoding="utf-8", newline="").write(t)
def rep(t, old, new, n=1, where=""):
    c = t.count(old); assert c == n, (where, old[:70], c, n); return t.replace(old, new)

DI = "compiler/src/compiler/decl_index.cryo"
t = load(DI)
t = rep(t, "    overload_func_trait:   u32[];     // SymbolStr.id of the trait a trait-impl\n",
           "    overload_func_span:    SourceSpan[]; // where the entry's declaration is written\n"
           "    overload_func_trait:   u32[];     // SymbolStr.id of the trait a trait-impl\n", where=DI)
t = rep(t, "            overload_func_trait:   [],\n",
           "            overload_func_trait:   [],\n            overload_func_span:    [],\n", where=DI)
t = rep(t,
"    register_signature(mut &this, name: SymbolStr, func_type_ref: TypeRef,\n"
"                       owner: SymbolStr, symbol: SymbolStr, trait_sym: SymbolStr) -> OverloadId {\n",
"    register_signature(mut &this, name: SymbolStr, func_type_ref: TypeRef,\n"
"                       owner: SymbolStr, symbol: SymbolStr, trait_sym: SymbolStr,\n"
"                       span: SourceSpan) -> OverloadId {\n", where=DI)
t = rep(t, "        this.overload_func_trait.push(trait_sym.id);\n",
           "        this.overload_func_trait.push(trait_sym.id);\n        this.overload_func_span.push(span);\n", where=DI)
t = rep(t,
"    /// The linker symbol of entry `id`; empty for an invalid id.\n",
"    /// Where entry `id`'s declaration is written; `none()` for an invalid id.\n"
"    entry_span(&this, id: OverloadId) -> SourceSpan {\n"
"        if (!id.is_valid()) { return SourceSpan::none(); }\n"
"        return this.overload_func_span[id.position() as i64];\n"
"    }\n\n"
"    /// The linker symbol of entry `id`; empty for an invalid id.\n", where=DI)
# register_methods: the method node's span
t = rep(t,
"            func.set_overload_entry(this.register_signature(\n"
"                combined_sym, func_type_ref, SymbolStr::empty(), eff_mangled, func.origin_trait));\n",
"            func.set_overload_entry(this.register_signature(\n"
"                combined_sym, func_type_ref, SymbolStr::empty(), eff_mangled, func.origin_trait,\n"
"                if (func.has_name_span()) { func.name_span } else { func.span }));\n", where=DI)
# register_function_signature takes the span
t = rep(t,
"    register_function_signature(mut &this, name: SymbolStr,\n"
"                                return_type: TypeRef, func_type_ref: TypeRef,\n"
"                                intern: InternTable*, arena: TypeArena*,\n"
"                                symbol: SymbolStr) -> OverloadId {\n",
"    register_function_signature(mut &this, name: SymbolStr,\n"
"                                return_type: TypeRef, func_type_ref: TypeRef,\n"
"                                intern: InternTable*, arena: TypeArena*,\n"
"                                symbol: SymbolStr, span: SourceSpan) -> OverloadId {\n", where=DI)
t = rep(t,
"            return this.register_signature(name, func_type_ref, SymbolStr::empty(), symbol, SymbolStr::empty());\n",
"            return this.register_signature(name, func_type_ref, SymbolStr::empty(), symbol, SymbolStr::empty(), span);\n", where=DI)
t = rep(t,
"        return this.register_signature(name, func_type_ref, SymbolStr::empty(), mangled.value, SymbolStr::empty());\n",
"        return this.register_signature(name, func_type_ref, SymbolStr::empty(), mangled.value, SymbolStr::empty(), span);\n", where=DI)
save(DI, t)

TR = "compiler/src/compiler/passes/type_resolution.cryo"
t = load(TR)
t = rep(t,
"                    const entry: OverloadId = ctx.decl_index.register_function_signature(\n"
"                        q_name, func.resolved_return_type, func_type_ref, ctx.intern_table, arena,\n"
"                        SymbolStr::empty());\n",
"                    const entry: OverloadId = ctx.decl_index.register_function_signature(\n"
"                        q_name, func.resolved_return_type, func_type_ref, ctx.intern_table, arena,\n"
"                        SymbolStr::empty(), if (func.has_name_span()) { func.name_span } else { func.span });\n", where=TR)
t = rep(t,
"                        ctx.decl_index.register_signature(\n"
"                            func.name, func_type_ref, q_name, ctx.decl_index.entry_symbol(entry), SymbolStr::empty());\n",
"                        ctx.decl_index.register_signature(\n"
"                            func.name, func_type_ref, q_name, ctx.decl_index.entry_symbol(entry), SymbolStr::empty(),\n"
"                            ctx.decl_index.entry_span(entry));\n", where=TR)
t = rep(t,
"                            fn_node.set_overload_entry(ctx.decl_index.register_function_signature(\n"
"                                ext_q_name, fn_node.resolved_return_type, func_type_ref,\n"
"                                ctx.intern_table, arena, ext_link_sym));\n",
"                            fn_node.set_overload_entry(ctx.decl_index.register_function_signature(\n"
"                                ext_q_name, fn_node.resolved_return_type, func_type_ref,\n"
"                                ctx.intern_table, arena, ext_link_sym,\n"
"                                if (fn_node.has_name_span()) { fn_node.name_span } else { fn_node.span }));\n", where=TR)
t = rep(t,
"                                ctx.decl_index.register_function_signature(\n"
"                                    q_sym, fn_node.resolved_return_type, func_type_ref,\n"
"                                    ctx.intern_table, arena, ext_link_sym);\n",
"                                ctx.decl_index.register_function_signature(\n"
"                                    q_sym, fn_node.resolved_return_type, func_type_ref,\n"
"                                    ctx.intern_table, arena, ext_link_sym,\n"
"                                    if (fn_node.has_name_span()) { fn_node.name_span } else { fn_node.span });\n", where=TR)
t = rep(t,
"                        ctx.decl_index.register_function_signature(\n"
"                            q_name, node.resolved_return_type, i_func_type_ref, ctx.intern_table, arena,\n"
"                            node.name);\n",
"                        ctx.decl_index.register_function_signature(\n"
"                            q_name, node.resolved_return_type, i_func_type_ref, ctx.intern_table, arena,\n"
"                            node.name, node.span);\n", where=TR)
save(TR, t)

LS = "compiler/src/compiler/sema/lambda_synth.cryo"
t = load(LS)
t = rep(t,
"        const entry: OverloadId = this.ctx.decl_index.register_function_signature(\n"
"            qualified_spec, spec_func.resolved_return_type, spec_ft,\n"
"            this.intern, this.arena, SymbolStr::empty());\n",
"        const entry: OverloadId = this.ctx.decl_index.register_function_signature(\n"
"            qualified_spec, spec_func.resolved_return_type, spec_ft,\n"
"            this.intern, this.arena, SymbolStr::empty(), spec_func.span);\n", where=LS)
save(LS, t)
print("ok")
