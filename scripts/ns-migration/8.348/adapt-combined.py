"""Adapt a checkout holding probe-cf23.patch plus this entry's change to build.

Run from the checkout's root after
    git apply probe-cf23.patch && git apply --3way <this entry's diff>
and taking the change's side of the three conflicts (call_resolver,
method_binding, trait_checker).  The probe's arena mints a parameter's type
from its declaration's symbol, and its two bound scans compare a
`GenericParamType*`, so:

  * `TraitChecker::head_param_type` mints with the head argument's symbol;
  * `MethodBinding::param_name_through_impl_head` hands the scans the
    parameter's type rather than its spelling.
"""
import sys

def rep(path, old, new):
    with open(path, encoding="utf-8", newline="") as f:
        s = f.read()
    n = s.count(old)
    if n != 1:
        print("FAIL", path, "matches:", n)
        sys.exit(1)
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(s.replace(old, new))
    print("ok", path)

C = "compiler/src/compiler/"
rep(C + "types/trait_checker.cryo",
    "return this.arena.create_generic_param(n.name, index as u64);",
    "return this.arena.create_generic_param(n.param_sym(), n.name, index as u64);")
rep(C + "sema/method_binding.cryo",
    "    param_name_through_impl_head(mut &this, ty: TypeRef) -> SymbolStr {\n"
    "        const seen: TypeRef = this.through_impl_head(ty);\n"
    "        const t: Type* = this.arena.lookup(seen.id);\n"
    "        if (t == null || t.kind != TypeKind::GenericParam) { return SymbolStr::empty(); }\n"
    "        return (t as GenericParamType*).param_name;\n",
    "    param_name_through_impl_head(mut &this, ty: TypeRef) -> GenericParamType* {\n"
    "        const seen: TypeRef = this.through_impl_head(ty);\n"
    "        const t: Type* = this.arena.lookup(seen.id);\n"
    "        if (t == null || t.kind != TypeKind::GenericParam) { return null; }\n"
    "        return t as GenericParamType*;\n")
path =C + "sema/method_binding.cryo"
with open(path, encoding="utf-8", newline="") as f:
    s = f.read()
old1 = ("        const param: SymbolStr = this.param_name_through_impl_head(param_ty);\n"
        "        if (!param.is_valid()) { return TypeRef::invalid(); }\n")
new1 = ("        const param: GenericParamType* = this.param_name_through_impl_head(param_ty);\n"
        "        if (param == null) { return TypeRef::invalid(); }\n")
old2 = ("        const param: SymbolStr = this.param_name_through_impl_head(param_ty);\n"
        "        if (!param.is_valid()) { return []; }\n")
new2 = ("        const param: GenericParamType* = this.param_name_through_impl_head(param_ty);\n"
        "        if (param == null) { return []; }\n")
if s.count(old1) != 1 or s.count(old2) != 1:
    print("FAIL", path, "scan entry points", s.count(old1), s.count(old2))
    sys.exit(1)
s = s.replace(old1, new1).replace(old2, new2)
with open(path, "w", encoding="utf-8", newline="") as f:
    f.write(s)
print("ok", path, "scan entry points")
print("ADAPT_DONE")
