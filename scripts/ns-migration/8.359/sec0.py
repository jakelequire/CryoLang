"""Section 0 rows the refused-signature unit moved, updated by hand
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

rep("done.py --outstanding | wc -l` → **6** (",
    "done.py --outstanding | wc -l` → **5** (6 before §8.359: `signature_refused` is asked by "
    "identity - a function's refusal on its definition, a method's by its owner type and leaf; ")
rep("done.py --name-taking | wc -l` → **1115** (condition two's population, all of it unplaced; ",
    "done.py --name-taking | wc -l` → **1114** (condition two's population, all of it unplaced; "
    "1,115 before §8.359: `mark_signature_refused`, `signature_refused` and "
    "`signature_refused_in_module` deleted, `mark_method_signature_refused` and "
    "`method_signature_refused` new, each a method's leaf on an owner type; ")
rep("grep -rho 'require_scope_res(' compiler/src --include=*.cryo \\| wc -l` → **21** (",
    "grep -rho 'require_scope_res(' compiler/src --include=*.cryo \\| wc -l` → **20** "
    "(-1 in §8.359: a module function's refused signature is read off the member's stamp; ")
rep("residue.py --count` → **298** (",
    "residue.py --count` → **297** (298 before §8.359: `signature_refused`'s two C sites and "
    "`signature_refused_in_module`'s J went, `method_signature_refused`'s two J came; ")
rep("grep -o 'J [0-9]*' \\| cut -d' ' -f2` → **160** (the justified residue; ",
    "grep -o 'J [0-9]*' \\| cut -d' ' -f2` → **161** (the justified residue; 160 before "
    "§8.359: -1 `signature_refused_in_module`, +2 `method_signature_refused`, a member read by "
    "its leaf off the owner type in hand; ")
rep("grep -o 'convertible [0-9]*' \\| cut -d' ' -f2` → **14** (",
    "grep -o 'convertible [0-9]*' \\| cut -d' ' -f2` → **12** (14 before §8.359, "
    "`signature_refused`'s two C sites; ")
rep("grep -c '_of_def(&this' compiler/src/compiler/decl_index.cryo` → **4** (",
    "grep -c '_of_def(&this' compiler/src/compiler/decl_index.cryo` → **3** "
    "(4 before §8.359 deleted `signature_refused_of_def` - a refusal is read off the "
    "definition itself; ")
rep("lane-gate.py --row DEFID_PATH` → **62** (",
    "lane-gate.py --row DEFID_PATH` → **60** (-2 in §8.359: `signature_refused_of_def` and "
    "`signature_refused_in_module` asked the refusal map by the definition's path; ")
rep("grep -rho 'mark_signature_refused(' compiler/src --include=*.cryo \\| wc -l` → **3** (the definition, the free-function registrar, the method registrar)",
    "grep -rho 'mark_signature_refused(' compiler/src --include=*.cryo \\| wc -l` → **0** "
    "(3 until §8.359 - the definition and its two registrars: a function's refusal is "
    "`DefTable::record_signature_refused` on its definition, a method's "
    "`mark_method_signature_refused(owner, leaf)`)")
rep("grep -o 'signature_refused' \\| wc -l` → **17** (14 at §8.265; the three value asks)",
    "grep -o 'signature_refused' \\| wc -l` → **15** (17 before §8.359, which replaced the "
    "composed-name map's functions with the definition's column and the method pair; "
    "14 at §8.265; the three value asks)")
rep("grep -o 'decl_fn_key' \\| wc -l` → **13** (",
    "grep -o 'decl_fn_key' \\| wc -l` → **12** (13 before §8.359: a refused signature is "
    "recorded on the definition, not under the key the function would have registered by; ")
rep("grep -c 'scope_owner_key' compiler/src/compiler/sema/call_resolver.cryo` → **8** (",
    "grep -c 'scope_owner_key' compiler/src/compiler/sema/call_resolver.cryo` → **7** "
    "(8 before §8.359: the refused-signature ask on a static path reads the owner type, not "
    "its key; ")

open(p, 'w', encoding='utf-8', newline='').write(src)
print('ok')
