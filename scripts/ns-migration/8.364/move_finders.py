"""Move the eight bare-name declaration finders out of the compiler's node
locator into the editor (`tools/CryoLSP/src/handlers/decl_finder.cryo`,
written by hand), with the spelling matchers only they used, and point the
editor's 33 call sites and hover's own copy of the impl walk at them."""
import io
import os
import re

ROOT = r"C:\Programming\apps\CryoLang"
NL_PATH = os.path.join(ROOT, "compiler", "src", "compiler", "AST", "node_locator.cryo")
LSP = os.path.join(ROOT, "tools", "CryoLSP", "src", "handlers")

FINDERS = ["find_method_in_modules", "find_function_in_modules", "find_const_in_modules",
           "find_trait_method_in_modules", "find_impls_of_type", "find_impls_of_trait",
           "find_field_in_modules", "find_type_decl_in_modules"]
HELPERS = ["leaf_segment_str", "type_name_matches", "impl_jump_span", "annotation_head_name",
           "match_methods_on_type", "match_fields_on_type", "decl_name_matches"]


def load(p):
    s = io.open(p, encoding="utf-8", newline="").read()
    return s, ("\r\n" if "\r\n" in s else "\n")


def save(p, s):
    io.open(p, "w", encoding="utf-8", newline="").write(s)


def cut_block(lines, head_re):
    """Remove the declaration whose head matches `head_re`, with the doc
    comment and blank lines directly above it and its whole body."""
    heads = [i for i, l in enumerate(lines) if re.match(head_re, l)]
    assert len(heads) == 1, (head_re, heads)
    h = heads[0]
    start = h
    while start > 0 and lines[start - 1].strip().startswith("///"):
        start -= 1
    while start > 0 and lines[start - 1].strip() == "":
        start -= 1
    depth = 0
    opened = False
    end = h
    while True:
        code = lines[end].split("//")[0]
        depth += code.count("{") - code.count("}")
        opened = opened or "{" in code
        if opened and depth == 0:
            break
        end += 1
    return lines[:start] + lines[end + 1:]


s, nl = load(NL_PATH)
lines = s.split(nl)
for f in FINDERS:
    lines = cut_block(lines, r"^public function %s\(" % f)
for f in HELPERS:
    lines = cut_block(lines, r"^    static %s\(" % f)
save(NL_PATH, nl.join(lines))

for name in ("code_lens.cryo", "definition.cryo", "hover.cryo"):
    p = os.path.join(LSP, name)
    s, nl = load(p)
    n = 0
    for f in FINDERS:
        a = "node_locator::%s(" % f
        n += s.count(a)
        s = s.replace(a, "decl_finder::DeclFinder::%s(" % f)
    a = "import compiler::ast::node_locator;" + nl
    assert s.count(a) == 1, (name, s.count(a))
    s = s.replace(a, a + "import lsp::handlers::decl_finder;" + nl)
    save(p, s)
    print(name, n)

p = os.path.join(LSP, "hover.cryo")
s, nl = load(p)
old = """    /// Name matching mirrors `find_impls_of_type`.
    format_impl_methods(&this, type_name: symbol_str::SymbolStr, want_trait: boolean) -> string {
        if (this.graph == null || this.intern == null) { return ""; }
        const type_leaf: string = node_locator::NodeLocator::leaf_segment_str(
            this.intern.resolve(type_name));
        mut out: string = "";
        const n: u32 = this.graph.module_count();
        for (mut mi: u32 = 0; mi < n; mi++) {
            const ast: node::ProgramNode* = this.graph.get_module_ast(mi);
            if (ast == null) { continue; }
            for (mut si: i64 = 0; si < ast.statements.length; si++) {
                const stmt: node::ASTNode* = node_locator::NodeLocator::unwrap_decl_stmt(
                    ast.statements[si]);
                if (stmt == null) { continue; }
                if (stmt.kind != ast::NodeKind::ImplementationBlock) { continue; }
                const impl_blk: declaration::ImplBlockNode* = stmt as declaration::ImplBlockNode*;
                if (!node_locator::NodeLocator::type_name_matches(this.intern,
                        impl_blk.target_type, type_name, type_leaf)) { continue; }
                const is_trait: boolean = impl_blk.trait_annotation != null;
                if (is_trait != want_trait) { continue; }
                // Trait impls prefix each signature with `trait <Trait>::`.
                mut prefix: string = "";
                if (is_trait) {
                    const th: symbol_str::SymbolStr = node_locator::NodeLocator::annotation_head_name(
                        impl_blk.trait_annotation);
                    if (th.is_valid()) {
                        prefix = "trait " + HoverEngine::leaf_after_separator(
                            this.intern.resolve(th)) + "::";
                    }
                }
                for (mut k: i64 = 0; k < impl_blk.methods.length; k++) {
                    const m: declaration::MethodNode* = impl_blk.methods[k];
                    if (m == null || m.func == null) { continue; }
                    out = out + "    " + prefix
                        + this.format_method_signature(m, "") + "\\n";
                }
            }
        }
        return out;
    }
"""
new = """    /// The impl blocks are the ones `DeclFinder::impls_of_type` finds.
    format_impl_methods(&this, type_name: symbol_str::SymbolStr, want_trait: boolean) -> string {
        if (this.graph == null || this.intern == null) { return ""; }
        mut out: string = "";
        const impls: declaration::ImplBlockNode*[] =
            decl_finder::DeclFinder::impls_of_type(this.graph, this.intern, type_name);
        for (mut ii: i64 = 0; ii < impls.length; ii++) {
            const impl_blk: declaration::ImplBlockNode* = impls[ii];
            const is_trait: boolean = impl_blk.trait_annotation != null;
            if (is_trait != want_trait) { continue; }
            // Trait impls prefix each signature with `trait <Trait>::`.
            mut prefix: string = "";
            if (is_trait) {
                const th: symbol_str::SymbolStr = decl_finder::DeclFinder::annotation_head_name(
                    impl_blk.trait_annotation);
                if (th.is_valid()) {
                    prefix = "trait " + HoverEngine::leaf_after_separator(
                        this.intern.resolve(th)) + "::";
                }
            }
            for (mut k: i64 = 0; k < impl_blk.methods.length; k++) {
                const m: declaration::MethodNode* = impl_blk.methods[k];
                if (m == null || m.func == null) { continue; }
                out = out + "    " + prefix
                    + this.format_method_signature(m, "") + "\\n";
            }
        }
        return out;
    }
"""
old = old.replace("\n", nl)
new = new.replace("\n", nl)
assert s.count(old) == 1, s.count(old)
s = s.replace(old, new)
save(p, s)

p = os.path.join(LSP, "_module.cryo")
s, nl = load(p)
a = "public module code_lens;" + nl
assert s.count(a) == 1
s = s.replace(a, a + "public module decl_finder;" + nl)
save(p, s)
print("ok")
