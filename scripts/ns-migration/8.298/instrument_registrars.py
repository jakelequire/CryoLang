"""Attack W's instrument: every name-keyed registration prints
SHADOW<TAB>W<TAB><door><TAB><key><TAB><identity>, the identity independent of
the key (arena id, node address, mangled symbol + span)."""
import io, sys
import os
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
R = os.path.join(ROOT, "compiler", "src", "compiler") + os.sep
EDITS = {
 "decl_index.cryo": [
  ("""    register_type(mut &this, qualified_name: SymbolStr, ty: TypeRef, is_public: boolean) -> DefId {
        this.type_map.insert(qualified_name.id, ty);
""",
   """    register_type(mut &this, qualified_name: SymbolStr, ty: TypeRef, is_public: boolean) -> DefId {
        fmt::printf("SHADOW\\tW\\tregister_type\\tk%u\\tt%llu\\n", qualified_name.id, ty.id);
        this.type_map.insert(qualified_name.id, ty);
"""),
  ("""    register_type_reverse_only(mut &this, qualified_name: SymbolStr, ty: TypeRef) -> void {
        this.type_reverse.insert(ty.id, qualified_name.id);
""",
   """    register_type_reverse_only(mut &this, qualified_name: SymbolStr, ty: TypeRef) -> void {
        fmt::printf("SHADOW\\tW\\tregister_type_reverse\\tk%u\\tt%llu\\n", qualified_name.id, ty.id);
        this.type_reverse.insert(ty.id, qualified_name.id);
"""),
  ("""                       span: SourceSpan) -> OverloadId {
        this.func_type_refs.insert(name.id, func_type_ref);
""",
   """                       span: SourceSpan) -> OverloadId {
        fmt::printf("SHADOW\\tW\\tregister_signature\\tk%u\\ts%u@%s:%u\\n", name.id, symbol.id, span.file, span.start_line);
        this.func_type_refs.insert(name.id, func_type_ref);
"""),
  ("""                                 thread_local: boolean) -> void {
        // Dedup so re-processing doesn't grow the list unboundedly.
""",
   """                                 thread_local: boolean) -> void {
        fmt::printf("SHADOW\\tW\\tregister_global\\tk%u/%u\\tt%llu\\n", namespace.id, name.id, ty.id);
        // Dedup so re-processing doesn't grow the list unboundedly.
"""),
 ],
 "types/generic_registry.cryo": [
  ("import compiler::ast;\nimport compiler::ast::{ declaration, node, NodeKind };\n",
   "import compiler::ast;\nimport compiler::ast::{ declaration, node, NodeKind };\nimport std::fmt;\n"),
  ("""    register_impl_block(mut &this, qualified_name: SymbolStr, block: ImplBlockNode*) -> void {
        const opt: Option<i64> = this.name_index.get(&qualified_name.id);
""",
   """    register_impl_block(mut &this, qualified_name: SymbolStr, block: ImplBlockNode*) -> void {
        fmt::printf("SHADOW\\tW\\tregister_impl_block\\tk%u\\tn%p\\n", qualified_name.id, block as void*);
        const opt: Option<i64> = this.name_index.get(&qualified_name.id);
"""),
  ("""                                 block: ImplBlockNode*) -> void {
        if (block == null || !name.is_valid()) { return; }
        this.inherent_impl_keys.push(name.id);
""",
   """                                 block: ImplBlockNode*) -> void {
        if (block == null || !name.is_valid()) { return; }
        fmt::printf("SHADOW\\tW\\tregister_inherent_impl_block\\tk%u\\tn%p\\n", name.id, block as void*);
        this.inherent_impl_keys.push(name.id);
"""),
  ("""                       node: ImplBlockNode*) -> ImplBlockNode* {
        if (node == null || !key_sym.is_valid()) { return null; }
        const key: u64 = key_sym.id as u64;
""",
   """                       node: ImplBlockNode*) -> ImplBlockNode* {
        if (node == null || !key_sym.is_valid()) { return null; }
        fmt::printf("SHADOW\\tW\\tregister_coherence\\tk%u\\tn%p\\n", key_sym.id, node as void*);
        const key: u64 = key_sym.id as u64;
"""),
  ("""                        block: ImplBlockNode*) -> void {
        if (block == null || !trait_id.is_valid() || !target_key.is_valid()) { return; }
        const key: u64 = GenericRegistry::trait_impl_key(trait_id, target_key);
""",
   """                        block: ImplBlockNode*) -> void {
        if (block == null || !trait_id.is_valid() || !target_key.is_valid()) { return; }
        fmt::printf("SHADOW\\tW\\tregister_trait_impl\\tk%u/%u\\tn%p\\n", trait_id.id, target_key.id, block as void*);
        const key: u64 = GenericRegistry::trait_impl_key(trait_id, target_key);
"""),
  ("""    register_trait_decl(mut &this, name: SymbolStr, node: TraitDeclNode*) -> void {
        if (node == null || !name.is_valid()) { return; }
""",
   """    register_trait_decl(mut &this, name: SymbolStr, node: TraitDeclNode*) -> void {
        if (node == null || !name.is_valid()) { return; }
        fmt::printf("SHADOW\\tW\\tregister_trait_decl\\tk%u\\tn%p\\n", name.id, node as void*);
"""),
  ("""                             node: ASTNode*) -> void {
        if (node == null || !qualified_name.is_valid()) { return; }
        const existing: Option<i64> = this.inherent_owner_index.get(&qualified_name.id);
""",
   """                             node: ASTNode*) -> void {
        if (node == null || !qualified_name.is_valid()) { return; }
        fmt::printf("SHADOW\\tW\\tregister_inherent_owner\\tk%u\\tn%p\\n", qualified_name.id, node as void*);
        const existing: Option<i64> = this.inherent_owner_index.get(&qualified_name.id);
"""),
 ],
 "types/arena.cryo": [
  ("""        mut ref: TypeRef = this.alloc_type(new StructType(this.next_id, qualified_name, module_name, this.intern_table));
        this.struct_cache.insert(qualified_name.id, ref);
        return ref;
""",
   """        mut ref: TypeRef = this.alloc_type(new StructType(this.next_id, qualified_name, module_name, this.intern_table));
        this.struct_cache.insert(qualified_name.id, ref);
        fmt::printf("SHADOW\\tW\\tcreate_struct\\tk%u\\tt%llu\\n", qualified_name.id, ref.id);
        return ref;
"""),
  ("""        this.class_cache.insert(qualified_name.id, ref);
        return ref;
""",
   """        this.class_cache.insert(qualified_name.id, ref);
        fmt::printf("SHADOW\\tW\\tcreate_class\\tk%u\\tt%llu\\n", qualified_name.id, ref.id);
        return ref;
"""),
  ("""        this.enum_cache.insert(qualified_name.id, ref);
        return ref;
""",
   """        this.enum_cache.insert(qualified_name.id, ref);
        fmt::printf("SHADOW\\tW\\tcreate_enum\\tk%u\\tt%llu\\n", qualified_name.id, ref.id);
        return ref;
"""),
  ("""        this.trait_cache.insert(qualified_name.id, ref);
        return ref;
""",
   """        this.trait_cache.insert(qualified_name.id, ref);
        fmt::printf("SHADOW\\tW\\tcreate_trait\\tk%u\\tt%llu\\n", qualified_name.id, ref.id);
        return ref;
"""),
  ("""        this.alias_cache.insert(alias_name.id, ref);
        return ref;
""",
   """        this.alias_cache.insert(alias_name.id, ref);
        fmt::printf("SHADOW\\tW\\tcreate_type_alias\\tk%u\\tt%llu\\n", alias_name.id, ref.id);
        return ref;
"""),
  ("""        this.func_template_cache.insert(qualified_name.id, ref);
        return ref;
""",
   """        this.func_template_cache.insert(qualified_name.id, ref);
        fmt::printf("SHADOW\\tW\\tcreate_function_template\\tk%u\\tt%llu\\n", qualified_name.id, ref.id);
        return ref;
"""),
 ],
 "const_table.cryo": [
  ("""             int_typed: boolean) -> void {
        match (this.by_qualified.get(&qualified.id)) {
""",
   """             int_typed: boolean) -> void {
        fmt::printf("SHADOW\\tW\\tconst_register\\tk%u\\tn%p\\n", qualified.id, init as void*);
        match (this.by_qualified.get(&qualified.id)) {
"""),
  ("""    register_enum(mut &this, qualified: SymbolStr, node: EnumDeclNode*) -> void {
        if (node == null) { return; }
""",
   """    register_enum(mut &this, qualified: SymbolStr, node: EnumDeclNode*) -> void {
        if (node == null) { return; }
        fmt::printf("SHADOW\\tW\\tconst_register_enum\\tk%u\\tn%p\\n", qualified.id, node as void*);
"""),
 ],
}
on = sys.argv[1] == "on"
for rel, pairs in EDITS.items():
    p = os.path.join(R, rel.replace('/', os.sep))
    t = io.open(p, encoding="utf-8").read()
    for old, new in pairs:
        a, b = (old, new) if on else (new, old)
        assert t.count(a) == 1, (rel, a[:70], t.count(a))
        t = t.replace(a, b)
    io.open(p, "w", encoding="utf-8", newline="\n").write(t)
    print(rel, len(pairs), "edits", "on" if on else "off")
