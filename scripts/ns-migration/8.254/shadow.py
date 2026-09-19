"""DefaultRegistry shadow, applied OVER THE OLD default-expansion pass (the
one keyed by the template's bare leaf): at both rewrite sites the NEW
decision - the annotation's stamp, `template_of(def)`, every parameter
defaulted, the enclosing declarations by `DefId` rather than by name - is
computed beside the old one, and a line is printed whenever they disagree:

    SHADOW<TAB>DEFEXP-DIFF<TAB><file>:<line><TAB><name><TAB>old=<0|1> new=<0|1> slot=<Def|GenericParam|...|Pending>

`--invert` prints DEFEXP-CTL on every AGREEMENT instead (the control that the
sites are reached).  `--revert` removes the edit.  The script refuses to
apply twice and refuses when an anchor is not exactly once in the file.
"""
import io, sys
SRC = r"C:\Programming\apps\CryoLang\compiler\src\compiler\passes\default_expansion.cryo"
invert = "--invert" in sys.argv
revert = "--revert" in sys.argv

EDITS = [
    # 1. fmt for the print.
    ("import std::collections::hashmap::{ HashMap };\n",
     "import std::collections::hashmap::{ HashMap };\nimport std::fmt;\nimport compiler::resolver::res::{ Res, ResBase, DefId };\nimport compiler::resolver::intern_table::{ InternTable };\n"),
    # 2. The scope stack carries the enclosing declarations' ids beside their names.
    ("type struct ScopeStack {\n    names: SymbolStr[];\n\n    static new() -> ScopeStack {\n        return ScopeStack { names: [] };\n    }\n",
     "type struct ScopeStack {\n    names: SymbolStr[];\n    defs: DefId[];\n\n    static new() -> ScopeStack {\n        return ScopeStack { names: [], defs: [] };\n    }\n\n"
     "    push_def(mut &this, d: DefId) -> void { this.defs.push(d); }\n"
     "    pop_def(mut &this) -> void { if (this.defs.length > 0) { this.defs.pop(); } }\n"
     "    contains_def(&this, d: DefId) -> boolean {\n"
     "        for (mut i: i64 = 0; i < this.defs.length; i++) { if (this.defs[i].equals(d)) { return true; } }\n"
     "        return false;\n    }\n"),
    # 3. The six declaration pushes push the id too.  Struct / union / class /
    #    trait push `node.name`; the alias pushes `node.alias_name`; the enum's
    #    push is followed by its own comment.
    ("                if (is_generic) { scope.push(node.name); }\n                for (mut i: i64 = 0; i < node.fields.length; i++) {",
     "                if (is_generic) { scope.push(node.name); scope.push_def(node.def); }\n                for (mut i: i64 = 0; i < node.fields.length; i++) {"),
    ("                if (is_generic) { scope.push(node.name); }\n                // Enum variant payload annotations (if any).",
     "                if (is_generic) { scope.push(node.name); scope.push_def(node.def); }\n                // Enum variant payload annotations (if any)."),
    ("                if (is_generic) { scope.push(node.name); }\n                for (mut i: i64 = 0; i < node.methods.length; i++) {\n                    DefaultExpansionPasses::rewrite_function_decl(\n                        node.methods[i], reg, scope);",
     "                if (is_generic) { scope.push(node.name); scope.push_def(node.def); }\n                for (mut i: i64 = 0; i < node.methods.length; i++) {\n                    DefaultExpansionPasses::rewrite_function_decl(\n                        node.methods[i], reg, scope);"),
    ("                if (is_generic) { scope.push(node.alias_name); }\n",
     "                if (is_generic) { scope.push(node.alias_name); scope.push_def(node.def); }\n"),
    ("                if (is_generic) { scope.pop(); }\n",
     "                if (is_generic) { scope.pop(); scope.pop_def(); }\n"),
    # 4. The Named site: both decisions, printed on disagreement.
    ("                if (scope.contains(named.name)) { return; }\n                const entry: DefaultEntry* = reg.lookup(named.name);\n                if (entry == null) { return; }\n",
     "                const sh_old: boolean = !scope.contains(named.name) && reg.lookup(named.name) != null;\n"
     "                mut sh_new: boolean = false;\n"
     "                mut sh_slot: string = \"Pending\";\n"
     "                match (named.res) {\n"
     "                    ResSlot::Answered(sh_r) => {\n"
     "                        sh_slot = sh_r.to_string();\n"
     "                        const sh_d: DefId = sh_r.def_id();\n"
     "                        if (sh_d.is_valid() && !scope.contains_def(sh_d)) {\n"
     "                            sh_new = DefaultExpansionPasses::sh_all_default(sh_d, reg);\n"
     "                        }\n"
     "                    }\n"
     "                    _ => {}\n"
     "                }\n"
     "                if (named.res.is_pending()) {\n"
     "                    fmt::eprintf(\"SHADOW\\tDEFEXP-PENDING\\t%%s:%%u\\t%%s\\told=%%d\\n\", named.span.file, named.span.start_line,\n"
     "                        reg.sh_intern.resolve(named.name), if (sh_old) { 1 } else { 0 });\n"
     "                }\n"
     "                if (sh_old %(op)s sh_new) {\n"
     "                    fmt::eprintf(\"SHADOW\\tDEFEXP-%(tag)s\\t%%s:%%u\\t%%s\\told=%%d new=%%d slot=%%s\\n\", named.span.file, named.span.start_line,\n"
     "                        reg.sh_intern.resolve(named.name), if (sh_old) { 1 } else { 0 }, if (sh_new) { 1 } else { 0 }, sh_slot);\n"
     "                }\n"
     "                if (scope.contains(named.name)) { return; }\n                const entry: DefaultEntry* = reg.lookup(named.name);\n                if (entry == null) { return; }\n"),
    # 5. The scope-segment site.
    ("                if (sr.scope_generic_args.length == 0\n                        && !scope.contains(sr.scope_name)) {\n                    const entry: DefaultEntry* = reg.lookup(sr.scope_name);\n",
     "                if (sr.scope_generic_args.length == 0) {\n"
     "                    const sh_old: boolean = !scope.contains(sr.scope_name) && reg.lookup(sr.scope_name) != null;\n"
     "                    mut sh_new: boolean = false;\n"
     "                    mut sh_slot: string = \"Pending\";\n"
     "                    match (sr.scope_res) {\n"
     "                        ResSlot::Answered(sh_r) => {\n"
     "                            sh_slot = sh_r.to_string();\n"
     "                            mut sh_d: DefId = DefId::invalid();\n"
     "                            match (sh_r) {\n"
     "                                Res::TypeRelative(sh_b, _) => {\n"
     "                                    match (sh_b) { ResBase::Def(sh_bd) => { sh_d = sh_bd; } _ => {} }\n"
     "                                }\n"
     "                                _ => {}\n"
     "                            }\n"
     "                            if (sh_d.is_valid() && !scope.contains_def(sh_d)) {\n"
     "                                sh_new = DefaultExpansionPasses::sh_all_default(sh_d, reg);\n"
     "                            }\n"
     "                        }\n"
     "                        _ => {}\n"
     "                    }\n"
     "                    if (sr.scope_res.is_pending()) {\n"
     "                        fmt::eprintf(\"SHADOW\\tDEFEXP-PENDING\\t%%s:%%u\\t%%s::\\told=%%d\\n\", sr.span.file, sr.span.start_line,\n"
     "                            reg.sh_intern.resolve(sr.scope_name), if (sh_old) { 1 } else { 0 });\n"
     "                    }\n"
     "                    if (sh_old %(op)s sh_new) {\n"
     "                        fmt::eprintf(\"SHADOW\\tDEFEXP-%(tag)s\\t%%s:%%u\\t%%s::\\told=%%d new=%%d slot=%%s\\n\", sr.span.file, sr.span.start_line,\n"
     "                            reg.sh_intern.resolve(sr.scope_name), if (sh_old) { 1 } else { 0 }, if (sh_new) { 1 } else { 0 }, sh_slot);\n"
     "                    }\n"
     "                }\n"
     "                if (sr.scope_generic_args.length == 0\n                        && !scope.contains(sr.scope_name)) {\n                    const entry: DefaultEntry* = reg.lookup(sr.scope_name);\n"),
    # 6. The registry carries the registry and the intern table for the shadow.
    ("type struct DefaultRegistry {\n    entries:   DefaultEntry[];\n    name_index: HashMap<u32, i64>;\n\n    static new() -> DefaultRegistry {\n        return DefaultRegistry {\n            entries:    [],\n            name_index: HashMap<u32, i64>::new(),\n        };\n    }\n",
     "type struct DefaultRegistry {\n    entries:   DefaultEntry[];\n    name_index: HashMap<u32, i64>;\n    sh_greg: GenericRegistry*;\n    sh_intern: InternTable*;\n\n    static new() -> DefaultRegistry {\n        return DefaultRegistry {\n            entries:    [],\n            name_index: HashMap<u32, i64>::new(),\n            sh_greg: null,\n            sh_intern: null,\n        };\n    }\n"),
    ("        mut registry: DefaultRegistry = DefaultRegistry::new();\n",
     "        mut registry: DefaultRegistry = DefaultRegistry::new();\n        registry.sh_greg = ctx.generic_registry;\n        registry.sh_intern = ctx.intern_table;\n"),
    # 7. The new decision's template half.
    ("    // Registry population\n",
     "    /// SHADOW: the new decision's template half - the stamped definition is a\n"
     "    /// registered template whose every parameter carries a default.\n"
     "    static sh_all_default(d: DefId, reg: DefaultRegistry*) -> boolean {\n"
     "        if (reg.sh_greg == null) { return false; }\n"
     "        const entry: TemplateEntry* = reg.sh_greg.template_of(d);\n"
     "        if (entry == null || entry.ast_node == null) { return false; }\n"
     "        const params: GenericParamNode*[] = entry.get_generic_params();\n"
     "        if (params.length == 0) { return false; }\n"
     "        for (mut i: i64 = 0; i < params.length; i++) {\n"
     "            if (params[i] == null || !params[i].has_default_annotation()) { return false; }\n"
     "        }\n"
     "        return true;\n"
     "    }\n\n"
     "    // Registry population\n"),
]


def main():
    src = io.open(SRC, encoding="utf-8", newline="").read()
    nl = "\r\n" if "\r\n" in src else "\n"
    fill = {"op": "==" if invert else "!=", "tag": "CTL" if invert else "DIFF"}
    edits = [(a.replace("\n", nl), (b % fill if "%(op)s" in b else b).replace("\n", nl)) for a, b in EDITS]
    if revert:
        for a, b in edits:
            # The struct-body edits are applied once and reverted once;
            # the six push/pop edits are counted, not single.
            n = src.count(b)
            if n == 0:
                raise SystemExit("no shadow to revert at: %r" % a[:60])
            src = src.replace(b, a)
    else:
        if "sh_all_default" in src:
            raise SystemExit("already shadowed")
        for a, b in edits:
            n = src.count(a)
            if n == 0:
                raise SystemExit("anchor not found: %r" % a[:80])
            src = src.replace(a, b)
    io.open(SRC, "w", encoding="utf-8", newline="").write(src)
    print("reverted" if revert else ("inverted" if invert else "shadowed"))


if __name__ == "__main__":
    main()
