p = 'compiler/src/compiler/types/trait_checker.cryo'
src = open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in src else '\n'
def swap(a, b):
    global src
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert src.count(a) == 1, a[:60]
    src = src.replace(a, b, 1)
swap("""        for (mut gi: i64 = 0; gi < subj_nargs; gi++) { args.push(this.arena.inst_type_arg_at(subject, gi)); }
""", """        for (mut gi: i64 = 0; gi < subj_nargs; gi++) {
            args.push(this.arena.inst_type_arg_at(subject, gi));
        }
""")
swap("""    /// Bind `impl_block`'s own parameters to `args` by the position its head
    /// writes each at (`implement<T, E> ... for Result<T, E>`: the impl's `T`
    /// to `args[0]`).  The impl's parameters are its own declarations, not the
    /// target template's, so a substitution over the template's parameters
    /// answers nothing for a bound the impl writes.  A concrete argument in
    /// the head binds nothing; a head whose count differs from `args` binds
    /// nothing at all.
""", """    /// Bind the parameters an implement block's head writes to an
    /// instantiation's arguments: `U` in `implement<U> .. for Wrap<U>`, for
    /// `Wrap<Plain>`, to `Plain`.  An impl's parameters are its own; they
    /// relate to the type's only through the head, so a bound the impl
    /// writes (`where U: Show`) is checked against these bindings and never
    /// against the type's parameters, which may be spelled differently.  A
    /// head whose argument count differs from `args` binds nothing.
""")
open(p, 'w', encoding='utf-8', newline='').write(src)
print('ok')
