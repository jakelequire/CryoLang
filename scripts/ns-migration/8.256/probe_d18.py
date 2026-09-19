"""D18's two readers of an alias keyword's spelling, measured over the corpus
with the fold in place (`int` stamps `PrimTy("i32")` at the name layer):

  SHADOW<TAB>NEW-SPELLING<TAB><file>:<line><TAB><type_name>
      sema's `new` step: the stamp answered no type and the index answered the
      written spelling (`lookup_type_exact(new_expr.type_name)`).  Every line
      is a `new` the fold left behind; 0 is what retires the step.
  SHADOW<TAB>NEW-STAMPED<TAB><file>:<line><TAB><type_name>
      the control: the stamp answered.  `new int[100]` is one of these once
      folded, so a run with the fold prints it here and not above.
  SHADOW<TAB>ALIASREAD<TAB><name-id>
      the index's `lookup_type` asked for one of the four forward-only alias
      registrations (`int`/`uint`/`float`/`double` -> i32/u32/f32/f64,
      pass_registry.cryo).  Every line is a reader the fold did not reach; 0
      retires the four registrations.  The same probe over the tree BEFORE the
      fold is the control: `new int[100]` reads one.

Applied over the tree as it stands; `--revert` removes it.
"""
import io, sys
SEMA = r"C:\Programming\apps\CryoLang\compiler\src\compiler\sema\sema.cryo"
INDEX = r"C:\Programming\apps\CryoLang\compiler\src\compiler\decl_index.cryo"
revert = "--revert" in sys.argv

EDITS = {
    SEMA: [
        ("        mut base: TypeRef = this.types.spelling_type(new_expr.res);\n"
         "        if (!base.is_valid()) {\n"
         "            base = this.types.lookup_type_exact(new_expr.type_name);\n"
         "        }\n",
         "        mut base: TypeRef = this.types.spelling_type(new_expr.res);\n"
         "        if (base.is_valid()) { fmt::eprintf(\"SHADOW\\tNEW-STAMPED\\t%s:%u\\t%s\\n\", new_expr.span.file, new_expr.span.start_line, this.ctx.intern_table.resolve(new_expr.type_name)); }\n"
         "        if (!base.is_valid()) {\n"
         "            base = this.types.lookup_type_exact(new_expr.type_name);\n"
         "            if (base.is_valid()) { fmt::eprintf(\"SHADOW\\tNEW-SPELLING\\t%s:%u\\t%s\\n\", new_expr.span.file, new_expr.span.start_line, this.ctx.intern_table.resolve(new_expr.type_name)); }\n"
         "        }\n"),
    ],
    INDEX: [
        ("    register_type_forward_only(mut &this, alias_name: SymbolStr, ty: TypeRef) -> void {\n"
         "        this.type_map.insert(alias_name.id, ty);\n",
         "    register_type_forward_only(mut &this, alias_name: SymbolStr, ty: TypeRef) -> void {\n"
         "        g_alias_ids.push(alias_name.id);\n"
         "        this.type_map.insert(alias_name.id, ty);\n"),
        ("    lookup_type(&this, name: SymbolStr) -> TypeRef {\n"
         "        return match (this.type_map.get(&name.id)) {\n",
         "    lookup_type(&this, name: SymbolStr) -> TypeRef {\n"
         "        for (mut ai: i64 = 0; ai < g_alias_ids.length; ai++) { if (g_alias_ids[ai] == name.id) { fmt::eprintf(\"SHADOW\\tALIASREAD\\t%u\\n\", name.id); } }\n"
         "        return match (this.type_map.get(&name.id)) {\n"),
        ("import std::fmt;\n",
         "import std::fmt;\nmut g_alias_ids: u32[] = [];\n"),
    ],
}


def main():
    for path, edits in EDITS.items():
        src = io.open(path, encoding="utf-8", newline="").read()
        nl = "\r\n" if "\r\n" in src else "\n"
        edits = [(a.replace("\n", nl), b.replace("\n", nl)) for a, b in edits]
        if revert:
            for a, b in edits:
                if src.count(b) != 1:
                    raise SystemExit("no probe to revert at: %r" % a[:60])
                src = src.replace(b, a)
        else:
            if "ALIASREAD" in src or "NEW-SPELLING" in src:
                raise SystemExit("already probed: " + path)
            for a, b in edits:
                if src.count(a) != 1:
                    raise SystemExit("anchor not exactly once (%d): %r" % (src.count(a), a[:70]))
                src = src.replace(a, b)
        io.open(path, "w", encoding="utf-8", newline="").write(src)
    print("reverted" if revert else "probed")


if __name__ == "__main__":
    main()
