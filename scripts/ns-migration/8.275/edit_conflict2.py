p2 = 'compiler/src/compiler/mono/call_specializer.cryo'
s2 = open(p2, encoding='utf-8', newline='').read()
old = """        // The method's own parameter and one of the owner's or the impl's
        // may be ONE arena type (the arena keys a parameter by its spelling),
        // bound here to two different types.  Nothing below can apply such a
        // substitution correctly - `This`'s arguments would be rebuilt with
        // the method's - so the method is not specialized, and the call is
        // reported where its specialization is missed.
        if (subst_val.conflicting_param() != 0) { return null; }
        const subst_ptr: TypeSubstitution* =
"""
new = """        const subst_ptr: TypeSubstitution* =
"""
assert s2.count(old) == 1
s2 = s2.replace(old, new)
open(p2, 'w', encoding='utf-8', newline='').write(s2)
print("ok2")

p = 'compiler/src/compiler/types/substitution.cryo'
s = open(p, encoding='utf-8', newline='').read()
old = """    /// Look up the replacement for a given param TypeID.
    /// Returns TypeRef::invalid() if not found.
    get(&this, param_id: u64) -> TypeRef {
        for (mut i: i64 = 0; i < this.param_ids.length; i++) {
            if (this.param_ids[i] == param_id) {
                return this.replacements[i];
            }
        }
        return TypeRef::invalid();
    }
"""
new = """    /// Look up the replacement for a given param TypeID.
    /// Returns TypeRef::invalid() if not found - or if found TWICE with two
    /// different answers.
    ///
    /// The arena hands out one parameter type per SPELLING
    /// (`create_generic_param` caches by the name alone), so a method's
    /// `<J>` and the `implement<.., J, ..>` its body was copied under are
    /// one arena type, and a specialization binding the method's `J` to one
    /// type and the impl's to another holds two answers for one question.
    /// Answering with whichever was added first rebuilt `This`'s own
    /// arguments with the method's, silently; an invalid answer leaves the
    /// type it was asked for unresolved, and the call that needed it is
    /// reported where its specialization is missed.  A parameter bound
    /// twice to the SAME type is one answer and is given.
    get(&this, param_id: u64) -> TypeRef {
        mut found: TypeRef = TypeRef::invalid();
        for (mut i: i64 = 0; i < this.param_ids.length; i++) {
            if (this.param_ids[i] != param_id) { continue; }
            if (!found.is_valid()) { found = this.replacements[i]; continue; }
            if (found.id != this.replacements[i].id) { return TypeRef::invalid(); }
        }
        return found;
    }
"""
assert s.count(old) == 1
s = s.replace(old, new)
old2 = """
    /// The first parameter bound here to TWO different types, or 0.
    ///
    /// The arena hands out one parameter type per SPELLING
    /// (`create_generic_param` caches by the name alone), so a method's
    /// `<J>` and the `implement<.., J, ..>` its body was copied under are
    /// one arena type, and a specialization binding the method's `J` to one
    /// type and the impl's to another has two answers for one question.
    /// `get` would answer with whichever was added first, silently: a body
    /// that names the impl's `J` inside `This` would be rebuilt with the
    /// method's argument.  A substitution that holds such a pair cannot be
    /// applied, and its owner refuses to specialize rather than guess.
    conflicting_param(&this) -> u64 {
        for (mut i: i64 = 0; i < this.param_ids.length; i++) {
            for (mut j: i64 = i + 1; j < this.param_ids.length; j++) {
                if (this.param_ids[i] != this.param_ids[j]) { continue; }
                if (this.replacements[i].id != this.replacements[j].id) { return this.param_ids[i]; }
            }
        }
        return 0;
    }
"""
assert s.count(old2) == 1
s = s.replace(old2, "")
open(p, 'w', encoding='utf-8', newline='').write(s)
print("ok")
