import sys
p = 'compiler/src/compiler/mono/call_specializer.cryo'
s = open(p, encoding='utf-8', newline='').read()
old = """    concretize_stashed_arg(mut &this, b_in: TypeRef, subst: TypeSubstitution*) -> TypeRef {
        mut b: TypeRef = b_in;
        if (subst != null) { b = subst.apply(b, this.arena); }
        return this.trait_spec.reduce_projections(b);
    }
"""
new = """    concretize_stashed_arg(mut &this, b_in: TypeRef, subst: TypeSubstitution*) -> TypeRef {
        mut b: TypeRef = b_in;
        if (subst != null) { b = subst.apply(b, this.arena); }
        fmt::eprintf("SHADOW STASH %s -> %s\\n", this.arena.resolve_display_name(b_in.id), this.arena.resolve_display_name(b.id));
        return this.trait_spec.reduce_projections(b);
    }
"""
if sys.argv[1] == 'on':
    assert s.count(old) == 1
    s = s.replace(old, new)
else:
    assert s.count(new) == 1
    s = s.replace(new, old)
open(p, 'w', encoding='utf-8', newline='').write(s)
print("ok", sys.argv[1])
