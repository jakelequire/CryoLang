# Every catch-all match-arm binding (`y => ...`) drop insertion registers over
# an OWNING subject, and every one an arm gives away (section 8.313):
#   SHADOW catchall reg <file>:<line>       the binding is linked to its subject
#   SHADOW catchall consumed <file>:<line>  the arm moved it, so the subject's own
#                                           release is suppressed - one release
#                                           fewer than before the fix: the
#                                           programs whose output changes
# A MEASUREMENT: applied to a tree carrying the fix, never committed applied.
#   python scripts/ns-migration/8.313/catchall-census.py
#   rm -rf compiler/build && make cryo
#   bash scripts/objcmp/corpus2.sh ca
#   git checkout compiler/src/compiler/passes/drop_insertion.cryo   (then re-apply the fix)
import os
R = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
P = os.path.join(R, "compiler", "src", "compiler", "passes", "drop_insertion.cryo")
s = open(P, newline="").read()

old_reg = """                this.register_param_type(pat.binding_name, pat.binding_sym, proxy_ty, pat.span);"""
new_reg = old_reg + """
                fmt::printf("SHADOW catchall reg %s:%d\\n", pat.span.file, pat.span.start_line as i32);"""
assert s.count(old_reg) == 1, s.count(old_reg)
s = s.replace(old_reg, new_reg)

old_arm = """            if (reg_count > 0) {
                if (this.check_and_unregister_arm_bindings(reg_count, link_mark)) {"""
new_arm = """            for (mut cp: i64 = 0; cp < arm.patterns.length; cp++) {
                const cpat: PatternNode* = arm.patterns[cp];
                if (reg_count > 0 && cpat != null && cpat.pattern_kind == PatternKind::Identifier
                        && cpat.binding_sym.is_valid() && this.is_moved_id(cpat.binding_sym.id)) {
                    fmt::printf("SHADOW catchall consumed %s:%d\\n", cpat.span.file,
                        cpat.span.start_line as i32);
                }
            }
""" + old_arm
assert s.count(old_arm) == 1, s.count(old_arm)
s = s.replace(old_arm, new_arm)
open(P, "w", newline="").write(s)
print("patched")
