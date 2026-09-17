"""`register_methods` takes the owner TYPE, not a key: the index derives the
registration key from its own reverse map, so a caller hands over the
TypeRef it holds (a declaration's stamped type through the `type_of_decl`
door, a synthesizer's freshly registered type, an impl head's `impl_owner`,
a spec's receiver) and no caller derives a key for it.  The six bare-name
mappings `register_decl_in_index` re-registered, which the type-declaration
stage had already written for the same declaration, are deleted.

--shadow keeps every old call and adds, beside it, a SHADOW line comparing
the old key with the reverse map's name for the new TypeRef (`RM`), and one
at each duplicate name mapping saying whether the pair was already held
(`NM`).  Without it, the clean tree.  Nothing is saved until every count
has been asserted."""
import io, sys
import os as _os
_REPO = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), "..", "..", ".."))
C = _REPO + "/compiler/src/compiler/"
SHADOW = "--shadow" in sys.argv

files = {}

def load(rel):
    files[rel] = io.open(C + rel, encoding="utf-8", newline="").read()

def nl_of(t):
    return "\r\n" if "\r\n" in t else "\n"

def rep(rel, old, new, n=1):
    t = files[rel]
    assert t.count(old) == n, (rel, old[:80], t.count(old))
    files[rel] = t.replace(old, new)

def N(rel, text):
    """Normalize a block's line endings to the file's."""
    return text.replace("\n", nl_of(files[rel]))

# ---------------------------------------------------------------- the index
DI = "decl_index.cryo"
load(DI)
OLD_DOC = ("    /// Register each method under the combined key \"QualifiedType::method\",\n"
           "    /// which is what a cross-module extern declares and codegen finds.\n"
           "    /// `type_sym` is the owner's registration key - a declared type's\n"
           "    /// canonical name, a primitive's spelling - and the only key the\n"
           "    /// methods go under: a written spelling of the owner is a leaf shared\n"
           "    /// by every module's type of that name, and a reader holding one asks\n"
           "    /// the head's stamp.\n")
OLD_HEAD = ("    register_methods(mut &this, type_sym: SymbolStr, methods: &MethodNode*[],\n"
            "                     intern: InternTable*, arena: TypeArena*) -> void {\n"
            "        const type_str: string = intern.resolve(type_sym);\n")
# The TypeRef registrar, ahead of the key one it delegates to.
NEW_HEAD = ("    /// Register a type's inline or impl-block methods under the key the\n"
            "    /// type itself is registered under.  The owner is the TYPE: the key is\n"
            "    /// read off this index's reverse map, so the caller derives none, and\n"
            "    /// a type this index holds no key for is a registration it never saw\n"
            "    /// - recorded, nothing registered, because a key guessed here would be\n"
            "    /// a second registration under a name no reader asks.\n"
            "    register_methods(mut &this, owner: TypeRef, methods: &MethodNode*[],\n"
            "                     intern: InternTable*, arena: TypeArena*, site: string) -> void {\n"
            "        const type_sym: SymbolStr = this.lookup_type_name(owner);\n"
            "        if (!type_sym.is_valid()) {\n"
            "            compiler::resolver::res::record_unregistered_def(site);\n"
            "            return;\n"
            "        }\n"
            "        this.register_methods_by_key(type_sym, methods, intern, arena);\n"
            "    }\n"
            "\n"
            "    /// The same registration by a KEY the caller holds, for an owner this\n"
            "    /// index cannot name from a type: an `async` owner's method repointed\n"
            "    /// under its template's key while the owner in hand is the instance;\n"
            "    /// a specialization whose methods are registered before (or without)\n"
            "    /// the specialization itself; an impl head on an alias keyword.  Each\n"
            "    /// is a name-keyed door the lane gate counts; none is a lookup.\n"
            "    register_methods_by_key(mut &this, type_sym: SymbolStr, methods: &MethodNode*[],\n"
            "                            intern: InternTable*, arena: TypeArena*) -> void {\n"
            "        const type_str: string = intern.resolve(type_sym);\n")
SHADOW_RM = (
    "    shadow_rm(&this, site: string, key: SymbolStr, owner: TypeRef, intern: InternTable*) -> void {\n"
    "        const by_ref: SymbolStr = this.lookup_type_name(owner);\n"
    "        mut v: string = \"DISAGREE\";\n"
    "        if (key.is_valid() && by_ref.is_valid() && key.id == by_ref.id) { v = \"AGREE\"; }\n"
    "        else if (!key.is_valid() && !owner.is_valid()) { v = \"BOTHINV\"; }\n"
    "        else if (!key.is_valid()) { v = \"KEYINV\"; }\n"
    "        else if (!owner.is_valid()) { v = \"OWNERINV\"; }\n"
    "        else if (!by_ref.is_valid()) { v = \"NONAME\"; }\n"
    "        fmt::eprintf(\"SHADOW\\tRM\\t%s\\t%s\\t%s\\t%s\\n\", site, v, intern.resolve(key), intern.resolve(by_ref));\n"
    "    }\n\n"
    "    shadow_nm(&this, site: string, bare: SymbolStr, q: SymbolStr, intern: InternTable*) -> void {\n"
    "        const alts: SymbolStr[] = this.lookup_qualified_alternatives(bare);\n"
    "        mut held: boolean = false;\n"
    "        for (mut i: i64 = 0; i < alts.length; i++) { if (alts[i].id == q.id) { held = true; } }\n"
    "        fmt::eprintf(\"SHADOW\\tNM\\t%s\\t%s\\t%s\\t%s\\n\", site, if (held) { \"DUP\" } else { \"NEW\" }, intern.resolve(bare), intern.resolve(q));\n"
    "    }\n\n")
if SHADOW:
    rep(DI, N(DI, OLD_HEAD), N(DI, SHADOW_RM + OLD_HEAD))
else:
    rep(DI, N(DI, OLD_DOC + OLD_HEAD), N(DI, NEW_HEAD))

# ------------------------------------------------------------ the callers
def call(rel, old, tref, site, indent, intern):
    """Replace one `register_methods(<key>, <rest>)` call: `old` is the exact
    old text, `tref` the owner expression, `site` the door's site, `intern`
    the caller's intern table (shadow mode prints names).  In shadow mode the
    old call stays and a shadow line precedes it."""
    t = files[rel]
    assert t.count(old) == 1, (rel, old[:80], t.count(old))
    key = old[old.index("register_methods(") + len("register_methods("):].split(",")[0].strip()
    if SHADOW:
        recv = old[:old.index("register_methods(")].strip()
        line = indent + recv + 'shadow_rm("' + site + '", ' + key + ", " + tref + ", " + intern + ");" + nl_of(t)
        files[rel] = t.replace(old, line + old)
    elif tref is None:
        files[rel] = t.replace(old, old.replace("register_methods(", "register_methods_by_key(", 1))
    else:
        new = old.replace(key + ",", tref + ",", 1)
        new = new.rstrip().rstrip(";").rstrip(")") + ', "' + site + '");' + old[len(old.rstrip()):]
        files[rel] = t.replace(old, new)

TR = "passes/type_resolution.cryo"
load(TR)
old = N(TR, "                ctx.decl_index.register_methods(qualified_sym, node.methods, ctx.intern_table, arena);\n")
assert files[TR].count(old) == 3, files[TR].count(old)
cursor = 0
for kind in ("struct", "union", "class"):
    # three identical lines, converted in order: struct, union, class
    t = files[TR]
    i = t.index(old, cursor)
    before, after = t[:i], t[i + len(old):]
    tref = 'ctx.decl_index.type_of_decl(node.def, "type_resolution/register %s methods")' % kind
    if SHADOW:
        line = N(TR, '                ctx.decl_index.shadow_rm("tr_%s", qualified_sym, %s, ctx.intern_table);\n' % (kind, tref))
        files[TR] = before + line + old + after
        cursor = i + len(line) + len(old)
    else:
        new = N(TR, "                ctx.decl_index.register_methods(%s, node.methods, ctx.intern_table, arena, \"type_resolution/register %s methods\");\n" % (tref, kind))
        files[TR] = before + new + after
        cursor = i + len(new)

old = N(TR, "                ctx.decl_index.register_methods(\n"
            "                    canonical_target, node.methods, ctx.intern_table, arena);\n")
call(TR, old, 'ctx.decl_index.impl_owner(node, "type_resolution/register impl methods")' if SHADOW else None,
     "type_resolution/register impl methods", "                ", "ctx.intern_table")

SP = "passes/specialization.cryo"
load(SP)
for kind, var in (("struct", "sn"), ("union", "un"), ("class", "cn")):
    old = N(SP, "            ctx.decl_index.register_methods(\n"
                "                arena_qname, %s.methods, ctx.intern_table, ctx.type_arena);\n" % var)
    call(SP, old, "spec_type", "specialization/inject %s methods" % kind, "            ", "ctx.intern_table")
old = N(SP, "                ctx.decl_index.register_methods(\n"
            "                    impl_qname, impl_node.methods, ctx.intern_table, ctx.type_arena);\n")
call(SP, old, "entry.specialized_type", "specialization/spec impl methods", "                ", "ctx.intern_table")

CS = "mono/call_specializer.cryo"
load(CS)
old = N(CS, "        this.decl_index.register_methods(\n"
            "            qualified_target, spec_methods, this.intern_table, this.arena);\n")
t = files[CS]
assert t.count(old) == 2, t.count(old)
i = t.index(old)
first, rest = t[:i + len(old)], t[i + len(old):]
files[CS] = first
call(CS, old, 'this.decl_index.impl_owner(impl_node, "mono/spec method on impl")' if SHADOW else None,
     "mono/spec method on impl", "        ", "this.intern_table")
first = files[CS]
files[CS] = rest
call(CS, old, "recv_type" if SHADOW else None, "mono/spec method on inherent owner", "        ", "this.intern_table")
files[CS] = first + files[CS]

AL = "sema/async_lower.cryo"
load(AL)
old = N(AL, "        this.ctx.decl_index.register_methods(\n"
            "            q_name, method_arr, this.intern, this.arena);\n")
call(AL, old, "struct_ref", "async/future poll", "        ", "this.intern")
old = N(AL, "        this.ctx.decl_index.register_methods(\n"
            "            owner.qname, arr, this.intern, this.arena);\n")
call(AL, old, "owner.ty" if SHADOW else None, "async/repoint method", "        ", "this.intern")

LS = "sema/lambda_synth.cryo"
load(LS)
old = N(LS, "        ctx_ptr.decl_index.register_methods(\n"
            "            qual_name, method_arr, intern_ptr, arena_ptr);\n")
call(LS, old, "struct_ref", "closure/struct methods", "        ", "intern_ptr")

# ------------------------------------ the duplicate bare-name mappings
# register_decl_in_index's six arms re-register the (bare, qualified) pair the
# type-declaration stage wrote for the same node, under the same condition.
ARMS = (
    ("struct", "                const node: StructDeclNode* = stmt as StructDeclNode*;\n"
               "                const qualified_sym: SymbolStr = ctx.decl_type_key(node.name, node.binding_namespace, node.span.file);\n"
               "                // Bare-name mapping only for file-namespaced types; a\n"
               "                // C-imported struct (binding_namespace set) stays\n"
               "                // qualified-only (`cit::Vec2`).\n"
               "                if (!qualified_sym.equals(node.name) && node.binding_namespace.is_empty()) {\n"
               "                    ctx.decl_index.register_name_mapping(node.name, qualified_sym);\n"
               "                }\n",
               "                const node: StructDeclNode* = stmt as StructDeclNode*;\n"),
    ("union",  "                const node: UnionDeclNode* = stmt as UnionDeclNode*;\n"
               "                const qualified_sym: SymbolStr = ctx.decl_type_key(node.name, node.binding_namespace, node.span.file);\n"
               "                if (!qualified_sym.equals(node.name) && node.binding_namespace.is_empty()) {\n"
               "                    ctx.decl_index.register_name_mapping(node.name, qualified_sym);\n"
               "                }\n",
               "                const node: UnionDeclNode* = stmt as UnionDeclNode*;\n"),
    ("class",  "                const node: ClassDeclNode* = stmt as ClassDeclNode*;\n"
               "                const qualified_sym: SymbolStr = ctx.decl_type_key(node.name, SymbolStr::empty(), node.span.file);\n"
               "                if (!qualified_sym.equals(node.name)) {\n"
               "                    ctx.decl_index.register_name_mapping(node.name, qualified_sym);\n"
               "                }\n",
               "                const node: ClassDeclNode* = stmt as ClassDeclNode*;\n"),
    ("enum",   "            NodeKind::EnumDeclaration => {\n"
               "                const node: EnumDeclNode* = stmt as EnumDeclNode*;\n"
               "                const qualified_sym: SymbolStr = ctx.decl_type_key(node.name, node.binding_namespace, node.span.file);\n"
               "                if (!qualified_sym.equals(node.name) && node.binding_namespace.is_empty()) {\n"
               "                    ctx.decl_index.register_name_mapping(node.name, qualified_sym);\n"
               "                }\n"
               "            }\n",
               ""),
    ("trait",  "                const qualified_sym: SymbolStr = ctx.decl_type_key(node.name, SymbolStr::empty(), node.span.file);\n"
               "                if (!qualified_sym.equals(node.name)) {\n"
               "                    ctx.decl_index.register_name_mapping(node.name, qualified_sym);\n"
               "                }\n",
               "                const qualified_sym: SymbolStr = ctx.decl_type_key(node.name, SymbolStr::empty(), node.span.file);\n"),
    ("alias",  "            NodeKind::TypeAliasDeclaration => {\n"
               "                const node: TypeAliasDeclNode* = stmt as TypeAliasDeclNode*;\n"
               "                const qualified_sym: SymbolStr = ctx.decl_type_key(node.alias_name, node.binding_namespace, node.span.file);\n"
               "                if (!qualified_sym.equals(node.alias_name) && node.binding_namespace.is_empty()) {\n"
               "                    ctx.decl_index.register_name_mapping(node.alias_name, qualified_sym);\n"
               "                }\n"
               "            }\n",
               ""),
)
for kind, old, new in ARMS:
    old_n = N(TR, old)
    if SHADOW:
        bare = "node.alias_name" if kind == "alias" else "node.name"
        marker = "ctx.decl_index.register_name_mapping(" + bare + ", qualified_sym);"
        shadowed = old.replace(marker, 'ctx.decl_index.shadow_nm("nm_%s", %s, qualified_sym, ctx.intern_table); %s' % (kind, bare, marker))
        rep(TR, old_n, N(TR, shadowed))
    else:
        rep(TR, old_n, N(TR, new))

if not SHADOW:
    for rel, t in files.items():
        assert "shadow_rm(" not in t and "shadow_nm(" not in t, rel
    assert files[TR].count("ctx.decl_index.register_name_mapping(") == 0, files[TR].count("ctx.decl_index.register_name_mapping(")
    by_ref = sum(t.count(".register_methods(") for t in files.values())
    by_key = sum(t.count(".register_methods_by_key(") for t in files.values())
    assert by_ref == 9 and by_key == 4 + 1, (by_ref, by_key)  # +1: the delegation inside the index

for rel, t in files.items():
    io.open(C + rel, "w", encoding="utf-8", newline="").write(t)
print("register_methods takes the TypeRef: %d files, shadow=%s" % (len(files), SHADOW))
