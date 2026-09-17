"""Shadow is_candidate_public's permissive default at its three doors.
Adds a temporary `visibility_recorded` query on the index and a SHADOW line
at each door: door, recorded|DEFAULT, and the key only when DEFAULT."""
import io
def rw(p, pairs):
    t = io.open(p, encoding="utf-8", newline="").read()
    for old, new in pairs:
        assert t.count(old) == 1, (p, old[:60], t.count(old))
        t = t.replace(old, new)
    io.open(p, "w", encoding="utf-8", newline="").write(t)

DI = "compiler/src/compiler/decl_index.cryo"
rw(DI, [(
"    is_candidate_public(&this, qualified: SymbolStr) -> boolean {\n",
"    visibility_recorded(&this, qualified: SymbolStr) -> boolean {\n"
"        return this.decl_visibility.get(&qualified.id).is_some();\n"
"    }\n\n"
"    is_candidate_public(&this, qualified: SymbolStr) -> boolean {\n")])

TR = "compiler/src/compiler/passes/type_resolution.cryo"
rw(TR, [(
"        if (reach && this.ctx.decl_index.is_candidate_public(cand)) {\n",
"        if (this.ctx.decl_index.visibility_recorded(cand)) { fmt::eprintf(\"SHADOW\\tICP\\tunreach\\trecorded\\n\"); } else { fmt::eprintf(\"SHADOW\\tICP\\tunreach\\tDEFAULT\\t%s\\n\", intern.resolve(cand)); }\n"
"        if (reach && this.ctx.decl_index.is_candidate_public(cand)) {\n"
), (
"        if (!this.ctx.decl_index.is_candidate_public(qualified)) { return \"\"; }\n",
"        if (this.ctx.decl_index.visibility_recorded(qualified)) { fmt::eprintf(\"SHADOW\\tICP\\tsuggest\\trecorded\\n\"); } else { fmt::eprintf(\"SHADOW\\tICP\\tsuggest\\tDEFAULT\\t%s\\n\", intern.resolve(qualified)); }\n"
"        if (!this.ctx.decl_index.is_candidate_public(qualified)) { return \"\"; }\n"
)])

CR = "compiler/src/compiler/sema/call_resolver.cryo"
rw(CR, [(
"        if (di.is_candidate_public(callee)) { return; }\n",
"        if (di.visibility_recorded(callee)) { fmt::eprintf(\"SHADOW\\tICP\\tcallee\\trecorded\\n\"); } else { fmt::eprintf(\"SHADOW\\tICP\\tcallee\\tDEFAULT\\t%s\\n\", this.intern.resolve(callee)); }\n"
"        if (di.is_candidate_public(callee)) { return; }\n"
)])
print("shadowed 3 doors")
