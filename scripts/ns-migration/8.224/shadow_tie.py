"""Second instrument: at every call whose receiver has TWO traits providing the
method (the E0154 detector's `trait_ambiguous`, whether or not a bound settled
it), and at every static-path tie in choose_entry, emit one TSC line per
candidate trait with kind `tie`/`ptie` so the scope verdicts can be paired
by position."""
import io
P = "compiler/src/compiler/sema/call_resolver.cryo"
t = io.open(P, encoding="utf-8", newline="").read()
def rep(old, new, n=1):
    global t
    assert t.count(old) == n, (old[:60], t.count(old))
    t = t.replace(old, new)
rep("        if (trait_ambiguous && !member.resolved_trait.is_valid()) {\n",
"        if (trait_ambiguous) {\n"
"            this.shadow_trait_scope(\"tie\", found_trait, member.span, member.resolved_trait.is_valid());\n"
"            this.shadow_trait_scope(\"tie\", other_trait, member.span, member.resolved_trait.is_valid());\n"
"        }\n"
"        if (trait_ambiguous && !member.resolved_trait.is_valid()) {\n")
rep("        if (n == 0 && fits.length >= 2) { this.report_static_path_tie(fits, path); }\n",
"        if (n == 0 && fits.length >= 2) {\n"
"            for (mut si: i64 = 0; si < fits.length; si++) { this.shadow_trait_scope(\"ptie\", this.ctx.decl_index.entry_trait(fits[si]), path.span, false); }\n"
"            this.report_static_path_tie(fits, path);\n"
"        }\n")
io.open(P, "w", encoding="utf-8", newline="").write(t)
print("ok")
