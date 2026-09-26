p = 'scripts/ns-migration/residue_classify.py'
src = open(p, encoding='utf-8', newline='').read()
lines = src.splitlines(keepends=True)
keys = ['"ImplBlockNode::where_bounds[]":', '"FunctionDeclNode::trait_bounds[]":',
        '"SemaState::symbolic_owner_param_nodes[]":', '"SemaState::symbolic_method_param_nodes[]":',
        '"TemplateEntry::param_names[]":', '"local::TraitBound[]":']
out = []
skip = 0
hit = 0
for ln in lines:
    if skip:
        skip -= 1
        continue
    if ln.strip() in keys:
        hit += 1
        skip = 1          # the entry's tuple is the next line
        continue
    out.append(ln)
assert hit == 6, hit
open(p, 'w', encoding='utf-8', newline='').write(''.join(out))
print('removed', hit)
