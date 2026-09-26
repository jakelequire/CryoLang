"""Delete the owner-to-head translation: under the arena keyed by declaration,
with `this.<field>` / `this.<method>()` substituted through the receiver, a
type computed in an impl body already names the impl's parameters."""

def edit(path, pairs):
    src = open(path, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in src else '\n'
    for a, b, n in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        c = src.count(a)
        assert c == n, (path, c, n, a[:80])
        src = src.replace(a, b)
    open(path, 'w', encoding='utf-8', newline='').write(src)

def cut_between(path, start, end):
    src = open(path, encoding='utf-8', newline='').read()
    a = src.index(start); b = src.index(end, a)
    open(path, 'w', encoding='utf-8', newline='').write(src[:a] + src[b:])

MB = 'compiler/src/compiler/sema/method_binding.cryo'
cut_between(MB, '    /// `ty` as the enclosing implement block reads it.',
            '    /// Mono-after-sema: resolve a method\'s return type when the receiver is an')
edit(MB, [
("""            return this.lookup_method_through_param_bounds(walk, walk, method_name);""",
 """            return this.lookup_method_through_param_bounds(
                t as GenericParamType*, walk, method_name);""", 1),
("""    lookup_method_through_param_bounds(mut &this, param_ty: TypeRef, recv: TypeRef,
                                       method_name: SymbolStr) -> TypeRef {
        const param: GenericParamType* = this.param_through_impl_head(param_ty);
        if (param == null) { return TypeRef::invalid(); }""",
 """    lookup_method_through_param_bounds(mut &this, param: GenericParamType*, recv: TypeRef,
                                       method_name: SymbolStr) -> TypeRef {
        if (param == null) { return TypeRef::invalid(); }""", 1),
("""    param_types_through_param_bounds(mut &this, param_ty: TypeRef, method_name: SymbolStr,
                                     arg_count: i64) -> TypeRef[] {
        const param: GenericParamType* = this.param_through_impl_head(param_ty);
        if (param == null) { return []; }""",
 """    param_types_through_param_bounds(mut &this, param: GenericParamType*, method_name: SymbolStr,
                                     arg_count: i64) -> TypeRef[] {
        if (param == null) { return []; }""", 1),
])

CR = 'compiler/src/compiler/sema/call_resolver.cryo'
edit(CR, [
("""            return this.binding.param_types_through_param_bounds(
                TypeRef::new(base_t.id, this.arena), member.member, arg_count);""",
 """            return this.binding.param_types_through_param_bounds(
                base_t as GenericParamType*, member.member, arg_count);""", 1),
])

AL = 'compiler/src/compiler/sema/async_lower.cryo'
cut_between(AL, '    /// `ty`, a type the method\'s body computed, as its implement block reads',
            '    /// An annotation for the `Future`\'s `Output`')
edit(AL, [
("this.type_ann_for(this.through_owner_head(d, ty), &d.fut_params, span)",
 "this.type_ann_for(ty, &d.fut_params, span)", 1),
])

TC = 'compiler/src/compiler/types/trait_checker.cryo'
src = open(TC, encoding='utf-8', newline='').read()
a = src.index('    /// The inverse of `add_head_param_bindings` over the owner\'s own')
end_marker = '        const out: TypeRef = subst.apply(ty, this.arena);'
b = src.index(end_marker, a)
b = src.index('    }', b) + len('    }')
# swallow the line break and the blank line after the deleted method
while src[b] in '\r\n':
    b += 1
open(TC, 'w', encoding='utf-8', newline='').write(src[:a] + src[b:])
print('ok')
