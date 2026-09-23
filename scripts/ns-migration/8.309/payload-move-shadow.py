# Shadow of the match-arm payload move check: every refusal the new tracking
# WOULD emit for a payload binding prints a SHADOW line instead, so a corpus
# run lists the whole blast radius without one refusal stopping the build.
# Also audits the inputs the fix depends on:
#   untyped   a payload registered with no type sema stamped (starved: it is
#             then untracked, silently)
#   fallback  `is_owned_value_place` reached its pattern-binding fallback
#   nosym     a binding reached a registration point with no identity
#             (move checker or drop inserter) - the rider's population
#   sum       one line per compilation: payloads registered (the control that
#             the instrument was entered at all)
#
# A MEASUREMENT: applied to a clean tree carrying the payload fix, never
# committed applied.
#   python scripts/ns-migration/8.309/payload-move-shadow.py
#   rm -rf compiler/build && make cryo
#   bash scripts/objcmp/corpus2.sh pp
#   git checkout compiler/src/compiler/passes/move_check.cryo compiler/src/compiler/passes/drop_insertion.cryo
# It patches by exact text and stops on an assert once a file has moved.
import os
R = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
MC = os.path.join(R, "compiler", "src", "compiler", "passes", "move_check.cryo")
DI = os.path.join(R, "compiler", "src", "compiler", "passes", "drop_insertion.cryo")

def patch(path, reps):
    s = open(path, newline="").read()
    for old, new in reps:
        c = s.count(old)
        assert c == 1, (os.path.basename(path), old[:70], c)
        s = s.replace(old, new)
    open(path, "w", newline="").write(s)

patch(MC, [
("""    fired: i64;
    /// Count of fatal""", """    fired: i64;
    probe_fn: string;
    probe_reg: i64;
    /// Count of fatal"""),
("""            fired: 0,
            fatal: 0,
        };""", """            fired: 0,
            probe_fn: "",
            probe_reg: 0,
            fatal: 0,
        };"""),
("""    register_binding(mut &this, sym: SymbolID, ty: TypeRef) -> void {
        if (!sym.is_valid()) { return; }""", """    register_binding(mut &this, sym: SymbolID, ty: TypeRef) -> void {
        if (!sym.is_valid()) {
            fmt::printf("SHADOW pp nosym mc.register_binding fn=%s\\n", this.probe_fn);
            return;
        }"""),
("""            if (pb != null && pb.sym_id.is_valid()) {
                this.pat_binding_keys.push(pb.sym_id.id);""", """            if (pb != null && !pb.sym_id.is_valid()) {
                fmt::printf("SHADOW pp nosym mc.payload fn=%s line=%d\\n", this.probe_fn,
                    pb.span.start_line as i32);
            }
            if (pb != null && pb.sym_id.is_valid()) {
                this.probe_reg += 1;
                if (!pb.resolved_type.is_valid()) {
                    fmt::printf("SHADOW pp untyped fn=%s %s:%d\\n", this.probe_fn,
                        pb.span.file, pb.span.start_line as i32);
                }
                this.pat_binding_keys.push(pb.sym_id.id);"""),
("""            if (this.is_pat_binding(id.res.local_sym().id)) {
                return !this.is_ptr_or_ref(obj.resolved_type);""", """            if (this.is_pat_binding(id.res.local_sym().id)) {
                fmt::printf("SHADOW pp fallback fn=%s\\n", this.probe_fn);
                return !this.is_ptr_or_ref(obj.resolved_type);"""),
("""        cdebug("[MoveCheck] === func '%s' ===\\n", this.intern.resolve(func.name));""",
 """        cdebug("[MoveCheck] === func '%s' ===\\n", this.intern.resolve(func.name));
        this.probe_fn = this.intern.resolve(func.name);"""),
# handle_ident: a payload's would-be refusal prints instead of emitting
("""        if (_is_copy) { return; }

        // A whole by-value move of an aggregate""", """        if (_is_copy) { return; }
        const probe_pat: boolean = this.is_pat_binding(sym.id);

        // A whole by-value move of an aggregate"""),
("""        if (in_move_context && real && this.pm_has_binding(sym.id)) {
            if (!this.suppress_emit) {""", """        if (in_move_context && real && this.pm_has_binding(sym.id)) {
            if (!this.suppress_emit && probe_pat) {
                fmt::printf("SHADOW pp refuse partial fn=%s ty=%s %s:%d\\n", this.probe_fn,
                    this.arena.format_display(this.arena.lookup(ty.id)), ident.span.file,
                    ident.span.start_line as i32);
            } else if (!this.suppress_emit) {"""),
("""            if (this.suppress_emit) { return; }
            if (this.is_carried(sym)) {""", """            if (this.suppress_emit) { return; }
            if (probe_pat) {
                fmt::printf("SHADOW pp refuse %s fn=%s ty=%s %s:%d\\n",
                    if (this.is_carried(sym)) { "loop" } else { "reuse" }, this.probe_fn,
                    this.arena.format_display(this.arena.lookup(ty.id)), ident.span.file,
                    ident.span.start_line as i32);
                return;
            }
            if (this.is_carried(sym)) {"""),
("""        const sym: SymbolID = id.res.local_sym();
        if (!this.is_polled(sym)) { return; }""", """        const sym: SymbolID = id.res.local_sym();
        if (!this.is_polled(sym)) { return; }
        if (this.is_pat_binding(sym.id)) {
            fmt::printf("SHADOW pp refuse polled fn=%s\\n", this.probe_fn);
            return;
        }"""),
("""        checker.walk_program(root);""", """        checker.walk_program(root);
        fmt::printf("SHADOW pp sum reg=%lld\\n", checker.probe_reg);"""),
])

patch(DI, [
("""                     span: SourceSpan, definit: boolean) -> void {
        if (!name.is_valid()) { return; }
        if (!sym.is_valid()) { return; }""", """                     span: SourceSpan, definit: boolean) -> void {
        if (!name.is_valid()) { return; }
        if (!sym.is_valid()) {
            fmt::printf("SHADOW pp nosym di.register_binding %s:%d\\n", span.file,
                span.start_line as i32);
            return;
        }"""),
("""    register_param_type(mut &this, name: SymbolStr, sym: SymbolID, ty: TypeRef) -> void {
        if (!sym.is_valid()) { return; }""", """    register_param_type(mut &this, name: SymbolStr, sym: SymbolID, ty: TypeRef) -> void {
        if (!sym.is_valid()) {
            fmt::printf("SHADOW pp nosym di.register_param_type\\n");
            return;
        }"""),
("""                if (!pb.sym_id.is_valid()) { continue; }""", """                if (!pb.sym_id.is_valid()) {
                    fmt::printf("SHADOW pp nosym di.payload\\n");
                    continue;
                }"""),
])
print("patched")
