R = "C:/Programming/apps/CryoLang/compiler/src/compiler/"

def sub(path, pairs):
    p = R + path
    s = open(p, encoding="utf-8", newline="").read()
    for old, new, n in pairs:
        c = s.count(old)
        assert c == n, (path, old, c)
        s = s.replace(old, new)
    open(p, "w", encoding="utf-8", newline="").write(s)

sub("types/generic_registry.cryo", [
    ("GenericRegistry::head_matches(heads[i], subject, this.arena, intern)",
     "this.head_matches(heads[i], subject, intern)", 2),
    ("GenericRegistry::head_matches(h.block, subject, this.arena, intern)",
     "this.head_matches(h.block, subject, intern)", 1),
    ("type struct GenericRegistry {\n    arena: TypeArena*;\n",
     "type struct GenericRegistry {\n    arena: TypeArena*;\n"
     "    /// The declaration index a written head's stamped definition is\n"
     "    /// compared against a subject's through (`annotation_unifies`).\n"
     "    decls: DeclarationIndex*;\n", 1),
    ("    static new(arena: TypeArena*) -> GenericRegistry {\n        return GenericRegistry {\n            arena:             arena,\n",
     "    static new(arena: TypeArena*, decls: DeclarationIndex*) -> GenericRegistry {\n        return GenericRegistry {\n            arena:             arena,\n            decls:             decls,\n", 1),
    ("import compiler::types::arena::{ TypeArena };\n",
     "import compiler::types::arena::{ TypeArena };\nimport compiler::decl_index::{ DeclarationIndex };\n", 1),
])
sub("mono/trait_specializer.cryo", [
    ("!GenericRegistry::head_matches_args(impl_node, type_args, this.arena, this.intern_table)",
     "!this.generic_registry.head_matches_args(impl_node, type_args, this.intern_table)", 1),
])
sub("compilation_context.cryo", [
    ("GenericRegistry::new(arena_ptr)", "GenericRegistry::new(arena_ptr, decl_ptr)", 1),
])
print("ok")
