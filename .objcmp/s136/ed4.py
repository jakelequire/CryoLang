R = "C:/Programming/apps/CryoLang/"
p = R + "tests/started-passing"
s = open(p, encoding="utf-8", newline="").read()
if not s.endswith("\n"):
    s += "\n"
s += ("identity_text_refused_through_an_override text read off a definition and answered by a method "
      "that overrides a base class's is E0157 where it is compared at a call made through the base, "
      "where the program built\n")
open(p, "w", encoding="utf-8", newline="").write(s)

p = R + "docs/resolution-rules.md"
s = open(p, encoding="utf-8", newline="").read()
anchor = "### 2026-10-10\n"
assert s.count(anchor) == 1
new = """### 2026-10-11

227. **The cleanup that removes the Python gates also removes
     `--emit=facts`, the compiler-internal `![allow]`s and the internal
     lint layer they serve.**  The structural locks stay: private fields,
     typed keys and `DeclName`.  Recorded only; a later change plans and
     does it.  (Dispatch, delegated)

228. **The check that follows what an override answers to a call made
     through the base lands, and the lane gate's pinned `LOOKUP_OTHER`
     rises from 35 to 36 for its one `member-function` door call.**
     (Dispatch, delegated)

229. **`LangModule::is_written` keeps the `lang-item` door id,** as the
     fixed tables ruling 225 names do.  (Dispatch, delegated)

230. **Name resolution is called complete only after the two known holes
     are closed** - what an override answers through the base, and
     `GenericRegistry::annotation_unifies` comparing a definition's path
     as text - **and a final read-only audit passes.**
     (Dispatch, delegated)

"""
s = s.replace(anchor, new + anchor)
open(p, "w", encoding="utf-8", newline="").write(s)
print("ok")
