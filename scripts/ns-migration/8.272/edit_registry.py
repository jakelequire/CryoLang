p = 'compiler/src/compiler/types/generic_registry.cryo'
s = open(p, encoding='utf-8', newline='').read()
def rep(old, new, count=1):
    global s
    assert s.count(old) == count, (old[:70], s.count(old))
    s = s.replace(old, new)

rep("""    name:           SymbolStr;           // Unqualified: "Array"
    qualified_name: SymbolStr;             // interned "std::collections::Array"
    module_name:    SymbolStr;
    param_names:    SymbolStr[];         // [intern("T"), intern("E")]
    param_type_ids: u64[];              // TypeIDs of GenericParamType objects
""", """    name:           SymbolStr;           // Unqualified: "Array"
    qualified_name: SymbolStr;             // interned "std::collections::Array"
    module_name:    SymbolStr;
    /// The parameters' spellings, index-aligned with `param_syms` and
    /// `param_type_ids`.  What the arena's parameter types and a display
    /// are minted from; never what a written annotation is matched by.
    param_names:    SymbolStr[];         // [intern("T"), intern("E")]
    /// The symbol each parameter's declaration bound - the key a written
    /// `T` carries in its stamp, and the one thing that tells the owner's
    /// `T` from a nested declaration's.
    param_syms:     SymbolID[];
    param_type_ids: u64[];              // TypeIDs of GenericParamType objects
""")
rep("""    static new(name: SymbolStr, qualified_name: SymbolStr, module_name: SymbolStr,
               param_names: SymbolStr[], param_type_ids: u64[],
               ast_node: ASTNode*, node_kind: NodeKind,
               base_type: TypeRef) -> TemplateEntry {
        const param_count_val: i64 = param_names.length;
        return TemplateEntry {
            name:           name,
            qualified_name: qualified_name,
            module_name:    module_name,
            param_names:    param_names,
            param_type_ids: param_type_ids,
""", """    static new(name: SymbolStr, qualified_name: SymbolStr, module_name: SymbolStr,
               param_names: SymbolStr[], param_syms: SymbolID[], param_type_ids: u64[],
               ast_node: ASTNode*, node_kind: NodeKind,
               base_type: TypeRef) -> TemplateEntry {
        const param_count_val: i64 = param_names.length;
        return TemplateEntry {
            name:           name,
            qualified_name: qualified_name,
            module_name:    module_name,
            param_names:    param_names,
            param_syms:     param_syms,
            param_type_ids: param_type_ids,
""")
if 'symbol_id::{ SymbolID }' not in s:
    rep("""import compiler::resolver::intern_table::{ InternTable };
import compiler::ast::declaration::{
""", """import compiler::resolver::intern_table::{ InternTable };
import compiler::resolver::symbol_id;
import compiler::resolver::symbol_id::{ SymbolID };
import compiler::ast::declaration::{
""")
open(p, 'w', encoding='utf-8', newline='').write(s)
print("ok")
