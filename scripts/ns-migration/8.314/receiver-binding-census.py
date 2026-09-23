# Which receiver each INSTANCE method has, as the name layer sees it
# (section 8.314).  One SHADOW line per instance method the name layer walks:
#   SHADOW rb <form> <kind> <file>:<line>
# form:
#   ref     a written `&this` / `mut &this` parameter (declared under the
#           spelling `&this`; `this` gets a second binding whose identity is
#           discarded)
#   value   a written by-value `this` parameter (already the binding)
#   none    no receiver parameter written at all (the binding the name layer
#           declares for `this` is the only one)
# kind: ctor, dtor, method (a class / struct / impl member), trait (a trait
# declaration's method, a default body or a bare signature).
#
# A MEASUREMENT: applied to a clean tree, never committed applied.
#   python scripts/ns-migration/8.314/receiver-binding-census.py
#   rm -rf compiler/build && make cryo
#   bash scripts/objcmp/corpus2.sh rb
#   git checkout compiler/src/compiler/resolver/name_resolution.cryo
import os
R = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
P = os.path.join(R, "compiler", "src", "compiler", "resolver", "name_resolution.cryo")
s = open(P, newline="").read()

old_m = """            const outer_receiver: FunctionDeclNode* = this.receiver_of;
            this.receiver_of = if (node.is_static) { null } else { node.func };
            node.func.accept(this);"""
new_m = """            const outer_receiver: FunctionDeclNode* = this.receiver_of;
            this.receiver_of = if (node.is_static) { null } else { node.func };
            if (!node.is_static) {
                this.probe_rb(node.func, if (node.is_constructor) { "ctor" } else {
                    if (node.is_destructor) { "dtor" } else { "method" } });
            }
            node.func.accept(this);"""
assert s.count(old_m) == 1, s.count(old_m)
s = s.replace(old_m, new_m)

old_t = """            this.receiver_of = if (node.methods[i].is_static) { null } else { node.methods[i] };
            node.methods[i].accept(this);"""
new_t = """            this.receiver_of = if (node.methods[i].is_static) { null } else { node.methods[i] };
            if (!node.methods[i].is_static) { this.probe_rb(node.methods[i], "trait"); }
            node.methods[i].accept(this);"""
assert s.count(old_t) == 1, s.count(old_t)
s = s.replace(old_t, new_t)

anchor = """    override visit(node: MethodNode*) -> void {"""
probe = """    probe_rb(&this, f: FunctionDeclNode*, kind: string) -> void {
        if (f == null) { return; }
        mut form: string = "none";
        for (mut i: i64 = 0; i < f.parameters.length; i++) {
            if (f.parameters[i].is_value_receiver()) { form = "value"; }
            if (f.parameters[i].is_ref_receiver()) { form = "ref"; }
        }
        fmt::printf("SHADOW rb %s %s %s:%d\\n", form, kind, f.span.file, f.span.start_line as i32);
    }

"""
assert s.count(anchor) == 1, s.count(anchor)
s = s.replace(anchor, probe + anchor)
open(P, "w", newline="").write(s)
print("patched")
