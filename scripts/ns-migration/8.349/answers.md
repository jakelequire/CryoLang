# Completion answers, recorded

Server built from `fb6114bd` (before) - identical with the handler changed and
again with the store deleted (section 8.349).  `probe_lsp.py <cryolsp.exe> lspf src/<file> q-<file>.json`.

```
dot wb.                                      | at 'get();' | -> 3 items: by[i64], get[(...) -> i64], doubled[(...) -> i64]
dot f.                                       | at 'width();' | -> 2 items: n[i32], width[(...) -> i32]
scope B::Item::                              | at 'make(4);' | -> 22 items: function[], return[], if[], else[], while[], for[], loop[], match[], break[], continue[], type[], struct[], union[], enum[]
scope C::                                    | at 'Formatter = ' | -> 22 items: function[], return[], if[], else[], while[], for[], loop[], match[], break[], continue[], type[], struct[], union[], enum[]
scope C::Formatter::                         | at 'mk();' | -> 22 items: function[], return[], if[], else[], while[], for[], loop[], match[], break[], continue[], type[], struct[], union[], enum[]
scope bare Item::                            | at 'make(4);' | -> 2 items: make[(...) -> Item], make[(...) -> Item]
PROBE_DONE
d dot f.                                     | at 'width();' | -> 2 items: n[i32], width[(...) -> i32]
d scope Formatter::                          | at 'mk();' | -> 1 items: mk[(...) -> Formatter]
d half-typed Formatter::                     | at '' | -> 1 items: mk[(...) -> Formatter]
PROBE_DONE
```
