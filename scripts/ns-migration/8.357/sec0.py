"""Section 0 rows the arena flip moved, updated by hand (reviewed text)."""
p = 'docs/name-resolution.md'
src = open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in src else '\n'

def rep(a, b):
    global src
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    c = src.count(a)
    assert c == 1, (c, a[:90])
    src = src.replace(a, b, 1)

rep("""   Outside that population, and therefore NOT on the list although they are
   lookups by spelling: `lookup_subst_for_param` (a where-bound's subject by
   its parameter's spelling, in `mono/trait_specializer.cryo` and
   `types/trait_checker.cryo`), and any name-taking lookup on a type that is
   not a store.""",
"""   Outside that population, and therefore NOT on the list although they are
   lookups by spelling: any name-taking lookup on a type that is not a
   store. (`lookup_subst_for_param`, a where-bound's subject by its
   parameter's spelling, was one until §8.357; it asks by the bound's
   symbol.)""")

rep("done.py --outstanding | wc -l` → **14** (23 before",
    "done.py --outstanding | wc -l` → **8** (14 before §8.357: the six "
    "generic-parameter scans - `TemplateEntry::param_names[]`, `local::TraitBound[]`, "
    "`FunctionDeclNode::trait_bounds[]`, `ImplBlockNode::where_bounds[]` and the two "
    "`SemaState` parameter lists - compare by declaration once the arena keys a "
    "parameter by it; 23 before")

rep("done.py --name-taking | wc -l` → **1120** (condition two's population, all of it unplaced; ",
    "done.py --name-taking | wc -l` → **1117** (condition two's population, all of it unplaced; "
    "1,120 before §8.357: `symbolic_name_is_generic_param` is deleted, "
    "`AsyncLower::param_spelled` and both `lookup_subst_for_param` take a parameter "
    "type or a symbol (-4), `TypeArena::this_placeholder(name)` is new (+1); ")

rep("subject_sym' compiler/src --include=*.cryo \\| wc -l` → **14** (",
    "subject_sym' compiler/src --include=*.cryo \\| wc -l` → **23** (+9 in §8.357: "
    "the bound scans and `lookup_subst_for_param`'s callers ask by the bound's symbol; ")

rep("param_syms' compiler/src --include=*.cryo \\| wc -l` → **92** (",
    "param_syms' compiler/src --include=*.cryo \\| wc -l` → **102** (+10 in §8.357: "
    "the compares against a template's parameters, and the parameter types the "
    "monomorphizer makes from each parameter's symbol; ")

rep("\\.equals' compiler/src --include=*.cryo \\| wc -l` → **3** (",
    "\\.equals' compiler/src --include=*.cryo \\| wc -l` → **0** (3 before §8.357, "
    "which retired the arena-name residual; ")

rep("**Left keyed by spelling, by design**: ",
    "**Left keyed by spelling until §8.357, which keys both by the declaration's symbol**: ")

rep("**The arena keyed by symbol is Jake's design unit**; that project becomes a run project exiting 25 when it lands",
    "**The arena keyed by symbol LANDED (§8.357)**; that project is a run project exiting 25")

rep("residue.py --count` → **311** (",
    "residue.py --count` → **301** (311 before §8.357: the ten generic-parameter "
    "compares it made by identity, nine S and the async lowering's J; ")

rep("cut -d' ' -f2` → **160** (the justified residue; ",
    "cut -d' ' -f2` → **159** (the justified residue; 160 before §8.357, whose "
    "async lowering matches a parameter by its declaration; ")

rep("cut -d' ' -f2` → **27** (S + B + C + F + W",
    "cut -d' ' -f2` → **18** (27 before §8.357, the nine S sites the arena flip "
    "retired; S + B + C + F + W")

rep("| **RULED; the nine head-binding places TAKEN (§8.347, §8.348)**",
    "| **TAKEN (§8.347, §8.348, §8.357)**")
rep("the seven comparisons and the arena flip remain, and the flip waits on the layer §8.350 found (a method's owner parameters returned unsubstituted when the receiver is abstract) |",
    "the arena flip TAKEN (§8.357): a parameter's type is its declaration's, every comparison is by identity, and §8.348's owner-to-head translation is deleted as redundant |")
rep("add_owner_to_head_bindings(' compiler/src \\| wc -l` → **6** (the two definitions; the trait checker's nested check, the monomorphizer's head bindings, the declaration index's returns, the owner's parameters read as the impl's) | §8.343, §8.347, §8.348 |",
    "add_owner_to_head_bindings(' compiler/src \\| wc -l` → **4** (the definition; the trait checker's nested check, the monomorphizer's head bindings, the declaration index's returns; 6 before §8.357 deleted `add_owner_to_head_bindings` and its one caller) | §8.343, §8.347, §8.348, §8.357 |")

rep("→ **35** (the kind that stays, the control for the zero beside it; 34 before §8.348, whose `MethodBinding::param_name_through_impl_head` asks it;",
    "→ **34** (the kind that stays, the control for the zero beside it; 35 from §8.348 to §8.357, `MethodBinding::param_name_through_impl_head`, deleted with the owner-to-head translation;")

rep("codegen failed for module' tests/tests/projects/chain_default_param_conflict/test.json` → **1**",
    "codegen failed for module' tests/tests/projects/chain_default_param_conflict/test.json` → **0** "
    "(1 until §8.357, when the arena keyed by declaration made the program compile and "
    "the project became a run project exiting 25)")

open(p, 'w', encoding='utf-8', newline='').write(src)
print('ok')
