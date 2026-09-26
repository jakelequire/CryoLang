"""Section 0 rows the intrinsics / visibility unit moved, updated by hand
(reviewed text).  Run from the repository root."""
p = 'docs/name-resolution.md'
src = open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in src else '\n'

def rep(a, b):
    global src
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    c = src.count(a)
    assert c == 1, (c, a[:90])
    src = src.replace(a, b, 1)

rep("""   Its two F lines (`is_candidate_public`, `namespace_of`) are **BLOCKED on
   Jake** (§8.340): a module-qualified call can hand the visibility gate the
   callee's identity only once intrinsic functions are offered by their
   module, which is the question §8.340 puts.
""", """   Its two F lines (`is_candidate_public`, `namespace_of`) are gone
   (§8.358): intrinsic functions are exported through their module, a
   module-qualified call carries the declaration the name layer bound, and
   the visibility gate asks by that identity.
""")
rep("done.py --outstanding | wc -l` → **8** (",
    "done.py --outstanding | wc -l` → **6** (8 before §8.358, whose visibility gate asks by "
    "the callee's definition: `is_candidate_public` and `namespace_of` are deleted; ")
rep("done.py --name-taking | wc -l` → **1117** (condition two's population, all of it unplaced; ",
    "done.py --name-taking | wc -l` → **1115** (condition two's population, all of it unplaced; "
    "1,117 before §8.358: five no longer take a spelling (`namespace_of`, `is_candidate_public`, "
    "`set_decl_visibility`, `enforce_value_ref_visibility`, `resolve_module_qualified_symbol`) "
    "and three new ones do (`Resolver::exports_named`, `NameResolution::export_in` and "
    "`private_in`, each a module's member by its leaf in one namespace); ")
rep("lane-gate.py --row LOOKUP_ROUTED` → **38** (",
    "lane-gate.py --row LOOKUP_ROUTED` → **36** (38 before §8.358: a module-qualified "
    "call's function type and return are read off the declaration the name layer bound, "
    "not `lookup_func_type_exact` of a composed name; ")
rep("grep -rho 'require_scope_res(' compiler/src --include=*.cryo \\| wc -l` → **22** (",
    "grep -rho 'require_scope_res(' compiler/src --include=*.cryo \\| wc -l` → **21** "
    "(-1 in §8.358: a module-qualified call reads the member's stamp, not the scope's "
    "to compose a name; ")
rep("residue.py --count` → **301** (",
    "residue.py --count` → **298** (301 before §8.358: both F sites and two "
    "`lookup_func_type_exact` sites gone, one J added - the use site's module asked "
    "of the graph by its namespace; ")
rep("grep -o 'J [0-9]*' \\| cut -d' ' -f2` → **159** (the justified residue; ",
    "grep -o 'J [0-9]*' \\| cut -d' ' -f2` → **160** (the justified residue; 159 before "
    "§8.358, whose visibility gate asks the graph for the use site's module; ")
rep("grep -o 'convertible [0-9]*' \\| cut -d' ' -f2` → **18** (",
    "grep -o 'convertible [0-9]*' \\| cut -d' ' -f2` → **14** (18 before §8.358: the two "
    "F lines and two C sites; ")
rep("| M2 `resolve_module_qualified_sym` | **LIVE — the destination, not a lane**; +246 in §8.172 when the atomics went behind `intrinsics::`; last pinned at 4,006 on the Windows b1 arm before the counter went (§8.203) | — | `grep -c 'resolve_module_qualified_symbol(mut &this' compiler/src/compiler/sema/call_resolver.cryo` → **1** | §8.120, §8.172, §8.203 |",
    "| M2 `resolve_module_qualified_sym` | **REPLACED (§8.358)** by `resolve_module_qualified_def`, which reads the declaration the name layer bound for the member (`ScopeResolutionNode.member_def`) instead of composing `module + \"::\" + leaf`; +246 in §8.172 when the atomics went behind `intrinsics::`; last pinned at 4,006 on the Windows b1 arm before the counter went (§8.203) | — | `grep -c 'resolve_module_qualified_def(&this' compiler/src/compiler/sema/call_resolver.cryo` → **1**; `grep -c 'resolve_module_qualified_symbol' compiler/src/compiler/sema/call_resolver.cryo` → **0** | §8.120, §8.172, §8.203, §8.358 |")
rep("`grep -c 'resolve_module_qualified_symbol(' compiler/src/compiler/sema/call_resolver.cryo` → **5** (the definition, the call's return, the value form, the parameter-type lookup, the argument check) | §8.217 |",
    "`grep -c 'resolve_module_qualified_def(' compiler/src/compiler/sema/call_resolver.cryo` → **5** (the definition, the call's return, the value form, the parameter-type lookup, and `module_qualified_path`, which the argument check and the pins read; `resolve_module_qualified_symbol` until §8.358) | §8.217, §8.358 |")
rep("lane-gate.py --row DEFID_PATH` → **61** (",
    "lane-gate.py --row DEFID_PATH` → **62** (+1 in §8.358, accepted by Jake with its reason: "
    "the E0353 message prints the private callee's path - diagnostic text, which a person "
    "reads, and whose wording a project pins; ")
rep("lane-gate.py --row GRAPH_READ` → **34** (",
    "lane-gate.py --row GRAPH_READ` → **35** (+1 in §8.358, accepted by Jake with its reason: "
    "the visibility gate turns the use site's file into its module - the namespace the "
    "file was declared under, asked of the graph for its definition - so two modules are "
    "compared by identity; ")
rep("grep -rho 'ModulePath::of(' compiler/src --include=*.cryo \\| wc -l` → **39** (",
    "grep -rho 'ModulePath::of(' compiler/src --include=*.cryo \\| wc -l` → **40** (+1 in "
    "§8.358, the same use-site module; ")
rep("ls -d tests/tests/projects/*/test.json | wc -l` → **83** (",
    "ls -d tests/tests/projects/*/test.json | wc -l` → **84** (+1 in §8.358, "
    "`visibility_function_beside_type`; ")
rep("| **RULED; built and measured, NOT landed (§8.351)** - `panic` settled by measurement (bare = `std::core`'s function; the prelude drops `core::intrinsics`); held on one gate ruling (`DEFID_PATH` 61 -> 63) | `no check` — not in the tree | §8.340, §8.351 |",
    "| **TAKEN (§8.358)** - `panic` settled by measurement (bare = `std::core`'s function; the prelude drops `core::intrinsics`); a module-qualified call carries the declaration the name layer bound (`member_def`), looked up in the VALUE namespace, a private one included so the refusal stays E0353; the visibility store is keyed by the definition (`DefTable` records each one's verdict) | `grep -c 'this.resolver.export_symbol(intr_declared);' compiler/src/compiler/resolver/name_resolution.cryo` → **1**; `grep -c '^public module core::intrinsics;' stdlib/prelude.cryo` → **0**; `grep -c 'decl_visibility:' compiler/src/compiler/decl_index.cryo` → **0** (2 before §8.358: the path-keyed map's field and its initializer) | §8.340, §8.351, §8.358 |")

open(p, 'w', encoding='utf-8', newline='').write(src)
print('ok')
