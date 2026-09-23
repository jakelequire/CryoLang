# Every comparison (== != < > <= >=) sema type-checks where an operand is a
# REFERENCE, classified by the other operand (section 8.312):
#   ref-ref    both references            (identity, unchanged by the rule)
#   ref-null   the other is `null` / void (a null check, unchanged)
#   ref-ptr    the other is a pointer / class / function (unchanged)
#   ref-value  the other is a plain value: number, bool, char, string, enum,
#              struct - the comparison the rule refuses
# A SHADOW line per ask, with the file and line, so the population is a list.
#
# A MEASUREMENT: applied to a clean tree, never committed applied.
#   python scripts/ns-migration/8.312/ref-compare-census.py
#   rm -rf compiler/build && make cryo
#   bash scripts/objcmp/corpus2.sh rc
#   git checkout compiler/src/compiler/sema/sema.cryo
import os
R = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
P = os.path.join(R, "compiler", "src", "compiler", "sema", "sema.cryo")
s = open(P, newline="").read()
old = """        if (lhs.is_valid() && rhs.is_valid()) {
            const result: TypeCheckResult = this.checker.check_binary_op(bin.op.lexeme, lhs, rhs);"""
new = """        if (lhs.is_valid() && rhs.is_valid()) {
            this.probe_ref_compare(bin, lhs, rhs);
            const result: TypeCheckResult = this.checker.check_binary_op(bin.op.lexeme, lhs, rhs);"""
assert s.count(old) == 1, s.count(old)
s = s.replace(old, new)
anchor = """    /// When an overloadable operator hits a user type that lacks the required"""
probe = """    probe_ref_compare(&this, bin: BinaryExprNode*, lhs: TypeRef, rhs: TypeRef) -> void {
        const op: string = bin.op.lexeme;
        if (!(op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=")) { return; }
        const lt: Type* = this.arena.lookup(lhs.id);
        const rt: Type* = this.arena.lookup(rhs.id);
        if (lt == null || rt == null) { return; }
        const lref: boolean = lt.kind == TypeKind::Reference;
        const rref: boolean = rt.kind == TypeKind::Reference;
        if (!lref && !rref) { return; }
        const other: Type* = if (lref) { rt } else { lt };
        mut cls: string = "ref-value";
        if (lref && rref) { cls = "ref-ref"; }
        else if (other.kind == TypeKind::Void) { cls = "ref-null"; }
        else if (other.kind == TypeKind::Pointer || other.kind == TypeKind::Class
                 || other.kind == TypeKind::Function) { cls = "ref-ptr"; }
        fmt::printf("SHADOW refcmp %s %s %s:%d\\n", cls, op, bin.span.file,
            bin.span.start_line as i32);
    }

"""
assert s.count(anchor) == 1
s = s.replace(anchor, probe + anchor)
open(P, "w", newline="").write(s)
print("patched")
