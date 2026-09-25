# A module-qualified function (`Module::f`) is the declaration the name layer
# bound for the member segment (section 8.340), where it used to be the
# module's stamp and the written leaf glued together at the use.  Asked both
# ways wherever the call resolver names one:
#   SHADOW mdef agree <name>                  both answers name the same function
#   SHADOW mdef agree-none                    neither names a function
#   SHADOW mdef differ new=<name> old=<name>  (an empty side answered nothing)
#   SHADOW mdef nomember old=<name>           the node carries no member stamp
#                                             but the composed name is a function
#
# A MEASUREMENT: applied to the tree carrying the unit, never committed applied.
#   git diff > .objcmp/u3/unit3.patch
#   python scripts/ns-migration/8.340/member-def-shadow.py
#   rm -rf compiler/build && make cryo
#   bash scripts/objcmp/corpus2.sh mdef
#   git checkout -- compiler/src && git apply .objcmp/u3/unit3.patch
import os
R = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
C = os.path.join(R, "compiler", "src", "compiler")

def patch(rel, pairs):
    p = os.path.join(C, rel)
    s = open(p, newline="").read()
    nl = "\r\n" if "\r\n" in s else "\n"
    for old, new in pairs:
        old = old.replace("\n", nl)
        new = new.replace("\n", nl)
        assert s.count(old) == 1, (rel, old[:90], s.count(old))
        s = s.replace(old, new)
    open(p, "w", newline="").write(s)

patch("sema/call_resolver.cryo", [
    ("        if (!this.overload_return_type(this.ctx.decl_index.func_type_of_def(d)).is_valid()) {\n"
     "            return DefId::invalid();\n"
     "        }\n"
     "        return d;\n",
     "        mut old_q: SymbolStr = SymbolStr::empty();\n"
     "        const old_ns: SymbolStr = scope.require_scope_res(\"mdef shadow\").def_id().qualified_name();\n"
     "        if (old_ns.is_valid()) {\n"
     "            const q: SymbolStr = this.ctx.intern(\n"
     "                this.intern.resolve(old_ns) + \"::\" + this.intern.resolve(scope.member_name));\n"
     "            if (this.family_return_type(q).is_valid()) { old_q = q; }\n"
     "        }\n"
     "        mut new_q: SymbolStr = SymbolStr::empty();\n"
     "        if (this.overload_return_type(this.ctx.decl_index.func_type_of_def(d)).is_valid()) {\n"
     "            new_q = d.qualified_name();\n"
     "        }\n"
     "        if (new_q.equals(old_q)) {\n"
     "            if (new_q.is_valid()) { fmt::printf(\"SHADOW mdef agree %s\\n\", this.intern.resolve(new_q)); }\n"
     "            else { fmt::printf(\"SHADOW mdef agree-none\\n\"); }\n"
     "        } else {\n"
     "            if (!d.is_valid()) { fmt::printf(\"SHADOW mdef nomember old=%s\\n\", this.intern.resolve(old_q)); }\n"
     "            else { fmt::printf(\"SHADOW mdef differ new=%s old=%s\\n\", this.intern.resolve(new_q), this.intern.resolve(old_q)); }\n"
     "        }\n"
     # The OLD answer is returned, so the build measured is the tree's
     # behaviour: the new answer cannot yet stand alone (intrinsic functions
     # are not offered by their module, section 8.340).
     "        if (!old_q.is_valid()) { return DefId::invalid(); }\n"
     "        return DefId::of_definition(old_q);\n"),
])
print("patched")
