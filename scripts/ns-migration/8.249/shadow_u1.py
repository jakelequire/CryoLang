"""Unit 1 (FILE/LINE) shadow, applied OVER the final code: sema's identifier
resolver and codegen's identifier emitter each compare the spelling test
they used to run with the stamp they now read, and print SRCLOC-DIFF when
the two disagree.  `--revert` removes the edits; `--invert` prints
SRCLOC-CTL on every AGREEMENT where both say pseudo-constant (the control
that proves the line can fire)."""
import io, sys
SEMA = r"C:\Programming\apps\CryoLang\compiler\src\compiler\sema\sema.cryo"
CG = r"C:\Programming\apps\CryoLang\compiler\src\compiler\codegen\visit\ir_generator.cryo"

invert = "--invert" in sys.argv
revert = "--revert" in sys.argv

SEMA_ANCHOR = """        const name_str: string = this.intern.resolve(ident.name);
        // A function named as a value.  The stamp is the writer's own answer
"""
SEMA_SHADOW = """        const name_str: string = this.intern.resolve(ident.name);
        mut shadow_stamped: boolean = false;
        match (ident.res) {
            ResSlot::Answered(Res::SourceLoc(_)) => { shadow_stamped = true; }
            _ => { }
        }
        const shadow_spelled: boolean = name_str.eq("FILE") || name_str.eq("LINE");
        if (shadow_spelled %s shadow_stamped) {
            fmt::eprintf("SHADOW\\tSRCLOC-%s\\tsema\\t%%s\\t%%s:%%d\\n", name_str, ident.span.file, ident.span.start_line);
        }
        // A function named as a value.  The stamp is the writer's own answer
"""
CG_ANCHOR = """    codegen_identifier(mut &this, node: IdentifierNode*) -> void {
        // A source-location pseudo-constant expands to the location of the
"""
CG_SHADOW = """    codegen_identifier(mut &this, node: IdentifierNode*) -> void {
        mut shadow_stamped: boolean = false;
        match (node.res) {
            ResSlot::Answered(Res::SourceLoc(_)) => { shadow_stamped = true; }
            _ => { }
        }
        const shadow_name: string = this.cg.resolve(node.name);
        const shadow_spelled: boolean = shadow_name.eq("FILE") || shadow_name.eq("LINE");
        if (shadow_spelled %s shadow_stamped) {
            fmt::eprintf("SHADOW\\tSRCLOC-%s\\tcodegen\\t%%s\\t%%s:%%d\\n", shadow_name, node.span.file, node.span.start_line);
        }
        // A source-location pseudo-constant expands to the location of the
"""

def fill(t):
    if invert:
        return t % ("&&", "CTL")
    return t % ("!=", "DIFF")

def edit(path, anchor, shadow):
    src = io.open(path, encoding="utf-8", newline="").read()
    nl = "\r\n" if "\r\n" in src else "\n"
    a = anchor.replace("\n", nl)
    s = fill(shadow).replace("\n", nl)
    if revert:
        for variant in (shadow % ("!=", "DIFF"), shadow % ("&&", "CTL")):
            v = variant.replace("\n", nl)
            if src.count(v) == 1:
                src = src.replace(v, a)
                break
        else:
            raise SystemExit("no shadow to revert in " + path)
    else:
        assert src.count(a) == 1, (path, src.count(a))
        src = src.replace(a, s)
    io.open(path, "w", encoding="utf-8", newline="").write(src)
    print(("reverted " if revert else "shadowed ") + path)

edit(SEMA, SEMA_ANCHOR, SEMA_SHADOW)
edit(CG, CG_ANCHOR, CG_SHADOW)
