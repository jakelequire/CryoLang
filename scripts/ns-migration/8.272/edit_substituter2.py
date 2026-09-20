p = 'compiler/src/compiler/AST/substituter.cryo'
s = open(p, encoding='utf-8', newline='').read()
def rep(old, new, count=1):
    global s
    assert s.count(old) == count, (old[:70], s.count(old))
    s = s.replace(old, new)
rep("""        const i: i64 = this.param_slot(named.param_sym());
        if (i >= 0) {
            {
                // Instead of blindly setting named.name (which produces
""", """        const i: i64 = this.param_slot(named.param_sym());
        if (i >= 0) {
                // Instead of blindly setting named.name (which produces
""")
rep("""                named.name = this.type_arg_displays[i];
                named.pre_resolved = this.resolved_arg_typeref(i);
                return ann;
            }
        }

        // Rewrite the base type name to the specialized name.
""", """                named.name = this.type_arg_displays[i];
                named.pre_resolved = this.resolved_arg_typeref(i);
                return ann;
        }

        // Rewrite the base type name to the specialized name.
""")
# re-indent the branch body by four fewer spaces: lines between the two markers
start = s.index("        if (i >= 0) {\n                // Instead of blindly")
end = s.index("                return ann;\n        }\n\n        // Rewrite the base type name to the specialized name.")
body = s[start:end]
lines = body.split("\n")
out = [lines[0]]
for ln in lines[1:]:
    out.append(ln[4:] if ln.startswith("                ") else ln)
s = s[:start] + "\n".join(out) + s[end:]
rep("""                return ann;
        }

        // Rewrite the base type name to the specialized name.
""", """            return ann;
        }

        // Rewrite the base type name to the specialized name.
""")
open(p, 'w', encoding='utf-8', newline='').write(s)
print("ok")
