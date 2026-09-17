"""Every reader of a type declaration's own type asks `type_of_decl(node.def,
site)`, a door: an unstamped node is recorded and reported (unconditionally -
the stamping stage has no error exit before its registration, so no user
error can leave a declaration unstamped), a stamped one the index cannot
serve goes to the unregistered-definition tally, and both hand back an
invalid type so the caller stops as before.  Nothing is saved until every
count has been asserted, so a stale tree is left untouched."""
import io
import os as _os
_REPO = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), "..", "..", ".."))
C = _REPO + "/compiler/src/compiler/"

files = {}

def load(rel):
    files[rel] = io.open(C + rel, encoding="utf-8", newline="").read()

def nl_of(text):
    return "\r\n" if "\r\n" in text else "\n"

def rep(rel, old, new, n=1):
    t = files[rel]
    assert t.count(old) == n, (rel, old[:70], t.count(old))
    files[rel] = t.replace(old, new)

def door(rel, receiver, var, site):
    """Convert the FIRST remaining `<receiver>.type_of_def(<var>.def)` in rel."""
    old = receiver + ".type_of_def(" + var + ".def)"
    assert old in files[rel], (rel, old)
    files[rel] = files[rel].replace(old, receiver + '.type_of_decl(' + var + '.def, "' + site + '")', 1)

# 1. res.cryo: the third tally.
load("resolver/res.cryo")
nl = nl_of(files["resolver/res.cryo"])
anchor = "/// How many `require` calls found a `Pending` slot, and where the first one"
block = nl.join([
    "/// How many type-declaration nodes reached a consumer with no registration",
    "/// stamped on them, and where the first one was.  Distinct from both tallies",
    "/// above: the stamp is written by the registration that minted it, in a",
    "/// stage with no error exit before that registration, so a missing one is",
    "/// never the consequence of a user error and is reported whatever else the",
    "/// build said - deferring it would hide the defect behind the cascade it",
    "/// causes (a method that cannot find `this`, a field that is not on its own",
    "/// type).",
    "mut g_unstamped_decls: u64 = 0;",
    'mut g_unstamped_first_site: string = "";',
    "",
    "/// Note that a consumer held a type declaration no registration stamped.",
    "public function record_unstamped_decl(site: string) -> void {",
    "    if (g_unstamped_decls == 0) { g_unstamped_first_site = site; }",
    "    g_unstamped_decls = g_unstamped_decls + 1;",
    "}",
    "",
    "/// How many type declarations reached a consumer unstamped.",
    "public function unstamped_decl_count() -> u64 {",
    "    return g_unstamped_decls;",
    "}",
    "",
    "/// Where the first unstamped declaration was consumed, for the report.",
    "public function unstamped_decl_first_site() -> string {",
    "    return g_unstamped_first_site;",
    "}",
    "",
    "",
]) + anchor
rep("resolver/res.cryo", anchor, block)

# 2. decl_index.cryo: the door, beside type_of_def.
load("decl_index.cryo")
nl = nl_of(files["decl_index.cryo"])
anchor = "    /// The type of the definition `d`, or invalid when `d` names none or the"
block = nl.join([
    "    /// The type of the declaration whose registration stamped `d` on its",
    "    /// node.  A door, not a lookup with a fallback: the stamp is written by",
    "    /// the registration that minted it, so an invalid `d` is a node no",
    "    /// registration reached and a valid one the index does not answer is a",
    "    /// registration lost after it stamped.  Neither is a licence to ask by",
    "    /// another key - the only other key is one re-derived from the node,",
    "    /// which names the wrong module for a trait default's `async` future -",
    "    /// so both are recorded and the caller gets an invalid type, which stops",
    "    /// it as a real failure would.",
    "    type_of_decl(&this, d: DefId, site: string) -> TypeRef {",
    "        if (!d.is_valid()) {",
    "            compiler::resolver::res::record_unstamped_decl(site);",
    "            return TypeRef::invalid();",
    "        }",
    "        const found: TypeRef = this.type_of_def(d);",
    "        if (!found.is_valid()) { compiler::resolver::res::record_unregistered_def(site); }",
    "        return found;",
    "    }",
    "",
]) + anchor
rep("decl_index.cryo", anchor, block)

# 3. instance.cryo: the report, unconditional, reached from BOTH exits.  The
# staged front-end returns through `project_failure` at the first failing
# stage, before the success path's tally flush, so a report placed only there
# would never be seen on the build it exists to explain.
load("instance.cryo")
nl = nl_of(files["instance.cryo"])
helper_anchor = "    project_failure(&this, ctx: CompilationContext*) -> CompilationResult {"
helper = nl.join([
    "    /// A type declaration reached a consumer with no registration stamped",
    "    /// on it.  NOT gated on the build being otherwise clean, unlike the two",
    "    /// tallies the success path reports: the stamping stage has no error",
    "    /// exit before its registration, so no user error can leave a",
    "    /// declaration unstamped, and the consumers of an unstamped one refuse",
    "    /// the program with errors that blame it (a method that cannot find",
    "    /// `this`, a field not on its own type) - gating this on those, or",
    "    /// reporting it only on the success path, would hide the defect behind",
    "    /// its own cascade.  The two exits are exclusive (the failure one is",
    "    /// reached only by a stage's early return, before the success path's",
    "    /// reports), so the tally is read once per build.",
    "    static report_unstamped_decls(ctx: CompilationContext*) -> void {",
    "        const unstamped: u64 = compiler::resolver::res::unstamped_decl_count();",
    "        if (unstamped > 0) {",
    "            ctx.emit_error(ErrorCode::E0900_INTERNAL_COMPILER_ERROR,",
    '                fmt::format("%llu type declaration(s) reached a consumer with no registration stamped; first consumed at %s",',
    "                    unstamped, compiler::resolver::res::unstamped_decl_first_site()));",
    "        }",
    "    }",
    "",
]) + helper_anchor
rep("instance.cryo", helper_anchor, helper)

fail_anchor = ("        const skip_flush: boolean = match (ctx.mode) {" + nl
               + "            CompileMode::Lsp => { true }" + nl
               + "            _                => { false }" + nl
               + "        };" + nl
               + "        if (!skip_flush) {" + nl
               + "            ctx.diagnostics.flush();")
rep("instance.cryo", fail_anchor,
    "        CompilerInstance::report_unstamped_decls(ctx);" + nl + fail_anchor)

ok_anchor = ("                    unreg, compiler::resolver::res::unregistered_def_first_site()));" + nl
             + "        }" + nl + nl)
rep("instance.cryo", ok_anchor,
    ok_anchor + "        CompilerInstance::report_unstamped_decls(ctx);" + nl + nl)

# 4. The readers, in file order.
load("codegen/ops/declaration_emitter.cryo")
for site in ("codegen/declare struct type", "codegen/declare union type",
             "codegen/declare class type", "codegen/declare enum type",
             "codegen/declare struct methods", "codegen/declare union methods",
             "codegen/declare class methods"):
    door("codegen/ops/declaration_emitter.cryo", "this.ctx.decl_index", "node", site)

load("codegen/visit/decl_visit_emitter.cryo")
for site in ("codegen/struct method bodies", "codegen/union method bodies",
             "codegen/class method bodies"):
    door("codegen/visit/decl_visit_emitter.cryo", "this.cg.ctx.decl_index", "node", site)

load("passes/directive_processing.cryo")
for site in ("directive/struct layout", "directive/union layout",
             "directive/class layout", "directive/enum layout"):
    door("passes/directive_processing.cryo", "ctx.decl_index", "node", site)

load("passes/type_resolution.cryo")
for site in ("type_resolution/struct methods", "type_resolution/union methods",
             "type_resolution/class methods", "type_resolution/trait populate",
             "type_resolution/alias target",
             "type_resolution/struct field sync", "type_resolution/union field sync",
             "type_resolution/class field sync", "type_resolution/enum field sync"):
    door("passes/type_resolution.cryo", "ctx.decl_index", "node", site)

load("sema/sema.cryo")
door("sema/sema.cryo", "this.ctx.decl_index", "s", "sema/async declare struct")
door("sema/sema.cryo", "this.ctx.decl_index", "u", "sema/async declare union")
door("sema/sema.cryo", "this.ctx.decl_index", "c", "sema/async declare class")
for site in ("sema/visit:StructDeclNode", "sema/visit:UnionDeclNode", "sema/visit:ClassDeclNode"):
    door("sema/sema.cryo", "this.ctx.decl_index", "node", site)

# Every node.def reader is converted: none may remain under the old name.
for rel, t in files.items():
    assert ".type_of_def(node.def)" not in t, rel
    assert ".type_of_def(s.def)" not in t and ".type_of_def(u.def)" not in t and ".type_of_def(c.def)" not in t, rel

for rel, t in files.items():
    io.open(C + rel, "w", encoding="utf-8", newline="").write(t)
converted = sum(t.count(".type_of_decl(") for t in files.values())
assert converted == 29, converted
print("doors: %d files written, %d readers converted" % (len(files), converted))
