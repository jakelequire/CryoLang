p = "C:/Programming/apps/CryoLang/compiler/src/compiler/resolver/mangled_name.cryo"
s = open(p, encoding="utf-8", newline="").read()
old = """                                    origin_trait: SymbolStr,
                                    method: SymbolStr, receiver: ReceiverKind,
                                    params: &TypeRef[], ret: TypeRef,
                                    is_variadic: boolean) -> MangledName {
        if (origin_trait.is_valid()) {
            const trait_qname: QualifiedName =
                QualifiedName::from_symbol(origin_trait);
            return MangledName::for_trait_impl(
                table, arena, trait_qname, owner_qname, method, receiver,
                params, ret, is_variadic);
"""
new = """                                    origin_trait: SymbolStr, trait_args: &TypeRef[],
                                    method: SymbolStr, receiver: ReceiverKind,
                                    params: &TypeRef[], ret: TypeRef,
                                    is_variadic: boolean) -> MangledName {
        if (origin_trait.is_valid()) {
            const trait_qname: QualifiedName =
                QualifiedName::from_symbol(origin_trait);
            return MangledName::for_trait_impl(
                table, arena, trait_qname, trait_args, owner_qname, method, receiver,
                params, ret, is_variadic);
"""
assert s.count(old) == 1
s = s.replace(old, new)
open(p, "w", encoding="utf-8", newline="").write(s)
print("ok")
