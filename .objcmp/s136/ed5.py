p = "C:/Programming/apps/CryoLang/.objcmp/s135/NR-COMPLETE.md"
s = open(p, encoding="utf-8", newline="").read()
pairs = [
    ("""  calls.
* Text that comes back off identities""",
     """  calls.  What an override answers goes on to the answer of each
  same-named method of every class it derives from (asked through the
  `member-function` door), so a call made through the base class
  (`t.display_name()` on a `Type*`) carries the override's text (8.593).
* Text that comes back off identities"""),
    ("""  `identity_text_refused_through_mangled_name` (`tests/tests/projects`).
""",
     """  `identity_text_refused_through_mangled_name`,
  `identity_text_refused_through_an_override` (`tests/tests/projects`).
"""),
    ("""**Knowingly out of scope / known holes**

* **A call through a base class.**  `t.display_name()` on a `Type*` names
  the base's method; what an override answers is not followed there.  A
  fix exists (`.objcmp/s135/override-flow.patch`: link each override's
  answer to the same-named base methods through the `member-function`
  door).  It finds real sites - it found the where-arg binder's compare,
  converted in 8.592 - but raises the lane gate's pinned `LOOKUP_OTHER`
  count by its one door call, so it waits on a decision.
* **`GenericRegistry::annotation_unifies`.**  Its allow's reason covers the
  primitive arm; its `Res::Def` arm compares a definition's path text with
  a template key.  The registry has no declaration index to compare
  definitions with.  Found this session, not converted.
""",
     """**Knowingly out of scope / known holes**

* Known holes: none.  The two this file named are closed in 8.593: a call
  through a base class now follows the override's answer (above), and
  `GenericRegistry::annotation_unifies` matches a head's written argument
  stamped as a declaration against the subject's argument by definition
  (`DeclarationIndex::def_of`, the registry holding the index), not by the
  definition's path text; its allow now covers only the primitive arms it
  names.
"""),
]
for old, new in pairs:
    c = s.count(old)
    assert c == 1, (old[:60], c)
    s = s.replace(old, new)
open(p, "w", encoding="utf-8", newline="").write(s)
print("ok")
