"""Hand-reviewed edits that finish the flip: renames, one duplicate answering
path deleted, comments that described the spelling-keyed arena."""
import re

def edit(path, pairs):
    src = open(path, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in src else '\n'
    for a, b, n in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        c = src.count(a)
        assert c == n, (path, c, n, a[:70])
        src = src.replace(a, b)
    open(path, 'w', encoding='utf-8', newline='').write(src)

SC = 'compiler/src/compiler/sema/symbolic_checker.cryo'
edit(SC, [
("""    /// True when the parameter whose declaration bound `sym` is one of the
    /// parameters in scope for the body under symbolic check.  Only
    /// meaningful while `state.in_symbolic_check`.
    symbolic_name_is_generic_param(&this, t: GenericParamType*) -> boolean {
        if (!this.state.in_symbolic_check) { return false; }
        for (mut i: i64 = 0; i < this.state.symbolic_owner_param_nodes.length; i++) {
            const p: GenericParamNode* = this.state.symbolic_owner_param_nodes[i];
            if (p != null && t.declared_by(p.sym_id)) { return true; }
        }
        for (mut j: i64 = 0; j < this.state.symbolic_method_param_nodes.length; j++) {
            const p: GenericParamNode* = this.state.symbolic_method_param_nodes[j];
            if (p != null && t.declared_by(p.sym_id)) { return true; }
        }
        return false;
    }

""", "", 1),
("this.symbolic_name_is_generic_param(t as GenericParamType*)",
 "this.symbolic_param_in_scope((t as GenericParamType*).param_sym)", 1),
("""    /// body has no entry for it.  Parameters are canonical by NAME, so the
    /// test is by name; a foreign parameter spelled like one in scope is
    /// indistinguishable here, as it is to every other reader of a symbolic
    /// type.  An associated projection over an in-scope parameter passes:
""", """    /// body has no entry for it.  A parameter type carries the symbol of the
    /// declaration that bound it, so a foreign parameter spelled like one in
    /// scope is told apart here.  An associated projection over an in-scope
    /// parameter passes:
""", 1),
])

MB = 'compiler/src/compiler/sema/method_binding.cryo'
edit(MB, [
("param_name_through_impl_head(", "param_through_impl_head(", 3),
("""        // Same (name, sentinel index) pair `resolve_trait_method_signatures`
        // interns; `create_generic_param` dedups, so this finds that entry.
""", """        // The one `This` placeholder `resolve_trait_method_signatures`
        // resolved the trait's signatures against.
""", 1),
])

RS = 'compiler/src/compiler/types/resolver.cryo'
edit(RS, [
("""            // Sentinel index well above any plausible real generic-param
            // count. arena.create_generic_param dedups by (name.id, index),
            // so every trait shares this canonical `This` placeholder.
""", """            // Every trait shares the arena's one `This` placeholder.
""", 1),
])

TC = 'compiler/src/compiler/types/trait_checker.cryo'
edit(TC, [
("""    /// own; this is the substitution between them.  A position is skipped
    /// where the owner's parameter type is also the head's parameter at
    /// another position (`.. for Pair<B, A>` over `Pair<A, B>`): while two
    /// parameters spelled alike are one type, a type there cannot say which
    /// of the two it means.
""", """    /// own; this is the substitution between them.
""", 1),
("""            if (!head_t.is_valid() || head_t.id == owner_params[gi]) { continue; }
            mut crossed: boolean = false;
            for (mut gj: i64 = 0; gj < owner_params.length; gj++) {
                if (gj == gi) { continue; }
                const other: TypeRef = this.head_param_type(impl_block, gj);
                if (other.is_valid() && other.id == owner_params[gi]) { crossed = true; break; }
            }
            if (crossed) { continue; }
            subst.add(owner_params[gi], head_t);
""", """            if (!head_t.is_valid()) { continue; }
            subst.add(owner_params[gi], head_t);
""", 1),
])
print('ok')
