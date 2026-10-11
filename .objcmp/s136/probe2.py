p = "C:/Programming/apps/CryoLang/compiler/src/compiler/mono/call_specializer.cryo"
s = open(p, encoding="utf-8", newline="").read()
old = """        const heads: ImplBlockNode*[] = this.trait_heads_on(ma.resolved_trait, recv_type);
        mut head: ImplBlockNode* = null;
"""
new = """        const heads: ImplBlockNode*[] = this.trait_heads_on(ma.resolved_trait, recv_type);
        fmt::eprintf("NRPROBE2 heads=%d args=%d recv=%s\\n", heads.length, call.bound_trait_args.length, this.arena.resolve_display_name(recv_type));
        for (mut q: i64 = 0; q < call.bound_trait_args.length; q++) {
            fmt::eprintf("NRPROBE2   arg %s\\n", this.arena.resolve_display_name(call.bound_trait_args[q]));
        }
        for (mut q: i64 = 0; q < heads.length; q++) {
            if (this.generic_registry.trait_args_unify(heads[q], &call.bound_trait_args, this.intern_table)) {
                fmt::eprintf("NRPROBE2   head %d unifies\\n", q);
            }
        }
        mut head: ImplBlockNode* = null;
"""
assert s.count(old) == 1
s = s.replace(old, new)
open(p, "w", encoding="utf-8", newline="").write(s)
print("probe2 in")
