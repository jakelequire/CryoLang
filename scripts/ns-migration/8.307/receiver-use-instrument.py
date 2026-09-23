# At each of sema's five "is this identifier the keyword `this`" sites,
# classify the ask by what the identifier's own identity says, against the
# receiver parameter's identity (section 8.307).
#   A  spelling says receiver, identity == the receiver parameter's sym
#   B  spelling says receiver, identity is a Local but NOT the receiver param's
#   C  spelling says receiver, identity is no Local answer at all
#   D  identity == receiver param's sym, spelling is NOT `this`   (site 1 only)
# Totals print once per compilation; C and D also print one line each.
#
# A MEASUREMENT: applied to a clean tree, never committed applied.
#   python scripts/ns-migration/8.307/receiver-use-instrument.py
#   rm -rf compiler/build && make cryo
#   bash scripts/objcmp/corpus2.sh rc
#   python scripts/ns-migration/8.307/receiver-use-sum.py .objcmp/rc-lines.txt
#   git checkout compiler/src/compiler/sema/sema.cryo
# It patches by exact text and stops on an assert once sema.cryo has moved.
import os
p = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))),
                 "compiler", "src", "compiler", "sema", "sema.cryo")
s = open(p, newline="").read()
def rep(old, new, n=1):
    global s
    c = s.count(old)
    assert c == n, (old[:60], c)
    s = s.replace(old, new)

rep("""    this_sym: SymbolStr;
    /// The allocator the `?` desugaring""", """    this_sym: SymbolStr;
    probe_recv: SymbolID;
    probe_n: i64[];
    /// The allocator the `?` desugaring""")
rep("""        this.this_sym = intern.intern("this");
        this.resolver = null;""", """        this.this_sym = intern.intern("this");
        this.probe_recv = SymbolID::invalid();
        this.probe_n = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
        this.resolver = null;""")
rep("""        this.state.current_lambda      = null;

        for (mut i: i64 = 0; i < func.parameters.length; i++) {""", """        this.state.current_lambda      = null;
        this.probe_recv = SymbolID::invalid();
        for (mut pi: i64 = 0; pi < func.parameters.length; pi++) {
            const pp: VarDeclNode* = func.parameters[pi];
            if (pp != null && pp.is_receiver()) { this.probe_recv = pp.sym_id; }
        }

        for (mut i: i64 = 0; i < func.parameters.length; i++) {""")
# site 1
rep("""    resolve_identifier(mut &this, ident: IdentifierNode*) -> TypeRef {
        if (ident.name.equals(this.this_sym)) {""", """    probe_rcv(mut &this, site: i64, ident: IdentifierNode*, old: boolean) -> void {
        const s: SymbolID = ident.res.local_sym();
        const nw: boolean = s.is_valid() && this.probe_recv.is_valid() && s.id == this.probe_recv.id;
        mut cls: i64 = -1;
        if (old && nw) { cls = 0; }
        else if (old && s.is_valid()) { cls = 1; }
        else if (old) { cls = 2; }
        else if (nw) { cls = 3; }
        if (cls < 0) { return; }
        this.probe_n[site * 4 + cls] = this.probe_n[site * 4 + cls] + 1;
        if (cls >= 2) {
            fmt::printf("SHADOW rcv site=%lld cls=%lld recv=%d line=%d\\n", site, cls,
                this.probe_recv.is_valid() as i32, ident.span.start_line as i32);
        }
    }

    resolve_identifier(mut &this, ident: IdentifierNode*) -> TypeRef {
        this.probe_rcv(1, ident, ident.name.equals(this.this_sym));
        if (ident.name.equals(this.this_sym)) {""")
# site 2
rep("""                const id: IdentifierNode* = lhs as IdentifierNode*;
                if (id.name.equals(this.this_sym)) { return; }""", """                const id: IdentifierNode* = lhs as IdentifierNode*;
                this.probe_rcv(2, id, id.name.equals(this.this_sym));
                if (id.name.equals(this.this_sym)) { return; }""")
# site 3
rep("""            const id: IdentifierNode* = unary.operand as IdentifierNode*;
            if (id.name.equals(this.this_sym)) { return operand_type; }""", """            const id: IdentifierNode* = unary.operand as IdentifierNode*;
            this.probe_rcv(3, id, id.name.equals(this.this_sym));
            if (id.name.equals(this.this_sym)) { return operand_type; }""")
# site 4 (cast_operand_is_byref_receiver is &this - make it mut for the probe)
rep("""    cast_operand_is_byref_receiver(&this, expr: ExpressionNode*) -> boolean {
        if (expr == null || !this.state.this_is_by_ref) { return false; }
        const n: ASTNode* = expr as ASTNode*;
        if (n.kind != NodeKind::Identifier) { return false; }
        return""", """    cast_operand_is_byref_receiver(mut &this, expr: ExpressionNode*) -> boolean {
        if (expr == null || !this.state.this_is_by_ref) { return false; }
        const n: ASTNode* = expr as ASTNode*;
        if (n.kind != NodeKind::Identifier) { return false; }
        this.probe_rcv(4, expr as IdentifierNode*, (expr as IdentifierNode*).name.equals(this.this_sym));
        return""")
# site 5
rep("""    subject_is_caller_backed(&this, e: ExpressionNode*) -> boolean {""",
    """    subject_is_caller_backed(mut &this, e: ExpressionNode*) -> boolean {""")
rep("""            const id: IdentifierNode* = e as IdentifierNode*;
            if (id.name.id == this.this_sym.id) { return this.state.this_is_by_ref; }""", """            const id: IdentifierNode* = e as IdentifierNode*;
            this.probe_rcv(5, id, id.name.id == this.this_sym.id);
            if (id.name.id == this.this_sym.id) { return this.state.this_is_by_ref; }""")
rep("""        v.visit(root);

        if (ctx.debug_mode) {
            cdebug("[Sema] Type-checked %lld function bodies\\n", v.state.body_count);""", """        v.visit(root);
        for (mut si: i64 = 1; si <= 5; si++) {
            fmt::printf("SHADOW rcv-sum site=%lld A=%lld B=%lld C=%lld D=%lld\\n", si,
                v.probe_n[si * 4], v.probe_n[si * 4 + 1], v.probe_n[si * 4 + 2], v.probe_n[si * 4 + 3]);
        }

        if (ctx.debug_mode) {
            cdebug("[Sema] Type-checked %lld function bodies\\n", v.state.body_count);""")
open(p, "w", newline="").write(s)
print("instrumented")
