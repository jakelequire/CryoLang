p = "C:/Programming/apps/CryoLang/docs/name-resolution.md"
s = open(p, encoding="utf-8", newline="").read()
pairs = [
    ("`python3 scripts/ns-migration/residue.py --count` → **207** (",
     "`python3 scripts/ns-migration/residue.py --count` → **208** (+1 in §8.593: the identity-text check asks the member-function door for the base methods an override answers for; 207 before, "),
    ("grep -o 'J [0-9]*' \\| cut -d' ' -f2` → **135** (",
     "grep -o 'J [0-9]*' \\| cut -d' ' -f2` → **136** (+1 in §8.593, as the population; 135 before, "),
    ("grep -o 'template_key_of' \\| wc -l` → **9** (",
     "grep -o 'template_key_of' \\| wc -l` → **8** (-1 in §8.593: a trait-impl head's written argument stamped as a declaration is matched against the subject's argument by definition, `DeclarationIndex::def_of`; 9 before, "),
    ("`python3 scripts/lane-gate.py --row DEFID_PATH` → **29** (",
     "`python3 scripts/lane-gate.py --row DEFID_PATH` → **28** (-1 in §8.593: `GenericRegistry::annotation_unifies`' `path_of(d).spelling()` goes; 29 before, "),
    ("`ls -d tests/tests/projects/*/test.json | wc -l` → **236** (",
     "`ls -d tests/tests/projects/*/test.json | wc -l` → **237** (+1 in §8.593, `identity_text_refused_through_an_override`: a definition's text answered by an overriding method and compared where the call is made through the base class; 236 before, "),
]
for old, new in pairs:
    c = s.count(old)
    assert c == 1, (old, c)
    s = s.replace(old, new)
open(p, "w", encoding="utf-8", newline="").write(s)
print("ok")
