# Sema's five "is this identifier use the receiver" sites, asked both ways
# (section 8.314): by the spelling `this` (the old test) and by the identity
# (`names_receiver`: the binding the use names is the receiver parameter's).
# One line per disagreement, the site first:
#   SHADOW recvuse <site> spell=<0|1> ident=<0|1> <file>:<line>
# and nothing where they agree.  The old answer is the one returned, so the
# build behaves as without the patch.
#
# A MEASUREMENT: applied to a tree carrying `names_receiver`, never committed
# applied.
#   python scripts/ns-migration/8.314/receiver-use-shadow.py
#   rm -rf compiler/build && make cryo
#   bash scripts/objcmp/corpus2.sh ru
#   git checkout compiler/src/compiler/sema/sema.cryo   (then re-apply the unit)
import os
R = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
P = os.path.join(R, "compiler", "src", "compiler", "sema", "sema.cryo")
s = open(P, newline="").read()

SITES = [
    ("resolve_identifier",
     "        if (ident.name.equals(this.this_sym)) {\n            if (this.state.this_type.is_valid()) { return this.state.this_type; }",
     "        if (this.shadow_recv(\"resolve_identifier\", ident, ident.name.equals(this.this_sym))) {\n            if (this.state.this_type.is_valid()) { return this.state.this_type; }"),
    ("assignment_lvalue",
     "                if (id.name.equals(this.this_sym)) { return; }",
     "                if (this.shadow_recv(\"assignment_lvalue\", id, id.name.equals(this.this_sym))) { return; }"),
    ("deref_this",
     "            if (id.name.equals(this.this_sym)) { return operand_type; }",
     "            if (this.shadow_recv(\"deref_this\", id, id.name.equals(this.this_sym))) { return operand_type; }"),
    ("cast_byref_receiver",
     "        return (expr as IdentifierNode*).name.equals(this.this_sym);",
     "        return this.shadow_recv(\"cast_byref_receiver\", expr as IdentifierNode*, (expr as IdentifierNode*).name.equals(this.this_sym));"),
    ("caller_backed",
     "            if (id.name.id == this.this_sym.id) { return this.state.this_is_by_ref; }",
     "            if (this.shadow_recv(\"caller_backed\", id, id.name.id == this.this_sym.id)) { return this.state.this_is_by_ref; }"),
]
for name, old, new in SITES:
    assert s.count(old) == 1, (name, s.count(old))
    s = s.replace(old, new)

anchor = "    resolve_identifier(mut &this, ident: IdentifierNode*) -> TypeRef {"
probe = """    shadow_recv(&this, site: string, id: IdentifierNode*, spelled: boolean) -> boolean {
        const ident: boolean = this.names_receiver(id);
        if (spelled != ident) {
            fmt::printf("SHADOW recvuse %s spell=%d ident=%d %s:%d\\n", site,
                if (spelled) { 1 } else { 0 }, if (ident) { 1 } else { 0 },
                id.span.file, id.span.start_line as i32);
        }
        return spelled;
    }

"""
assert s.count(anchor) == 1
s = s.replace(anchor, probe + anchor)
open(P, "w", newline="").write(s)
print("patched")
