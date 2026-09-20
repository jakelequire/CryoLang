#!/usr/bin/env python3
"""The language reference's expression-position generic arguments, rewritten to
the turbofish, and the rule stated where generic calls are introduced (§4.2)
and in the grammar.  Prose that names a trait's member by its head
(`Index<Idx, Output>::index`, `Deref<Target>::deref`) is a path in trait
terms, not code, and is left; the mangling spec's `Pair<int>::new` is a
symbol's display and is left.

    python scripts/ns-migration/8.278/docs_turbofish.py

Every replacement asserts its count before saving.
"""
import os

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))

CRYO = [
    ("`apply<T>(c, x)`", "`apply::<T>(c, x)`"),
    ("(`some.map<int>(...)`)", "(`some.map::<int>(...)`)"),
    ("    return SliceIter<T> { ptr: this.ptr, remaining: this.length };",
     "    return SliceIter::<T> { ptr: this.ptr, remaining: this.length };"),
    ("(e.g. `return SliceIter<T> { ... }`)", "(e.g. `return SliceIter::<T> { ... }`)"),
    ("= Range<i32>::new(0, 10);", "= Range::<i32>::new(0, 10);"),
    ("const n: int    = identity<int>(42);\nconst s: string = identity<string>(\"hello\");",
     "const n: int    = identity::<int>(42);\nconst s: string = identity::<string>(\"hello\");"),
    ("`va.next<T>()` is the explicit form", "`va.next::<T>()` is the explicit form"),
    ("const ints: Pair<int>    = Pair<int>::new(1, 2);\nconst strs: Pair<string> = Pair<string>::new(\"hello\", \"world\");",
     "const ints: Pair<int>    = Pair::<int>::new(1, 2);\nconst strs: Pair<string> = Pair::<string>::new(\"hello\", \"world\");"),
    ("const e: Either<i64, f64> = Either<i64, f64> { a: 100 };",
     "const e: Either<i64, f64> = Either::<i64, f64> { a: 100 };"),
    ("mut b: Boxed<Vec2> = Boxed<Vec2> { value: Vec2 { x: 1, y: 2 } };",
     "mut b: Boxed<Vec2> = Boxed::<Vec2> { value: Vec2 { x: 1, y: 2 } };"),
    ("Calling `Array<int>::new()` uses `GlobalAlloc`; calling `Array<int, Arena>::new_in(my_arena)`",
     "Calling `Array::<int>::new()` uses `GlobalAlloc`; calling `Array::<int, Arena>::new_in(my_arena)`"),
    ("const n: int = identity<int>(42);", "const n: int = identity::<int>(42);"),
    ("const a: Pair<int>    = Pair<int>::new(1, 2);\nconst b: Pair<string> = Pair<string>::new(\"x\", \"y\");",
     "const a: Pair<int>    = Pair::<int>::new(1, 2);\nconst b: Pair<string> = Pair::<string>::new(\"x\", \"y\");"),
    ("write the arguments explicitly (`f<T>(...)`) there.", "write the arguments explicitly (`f::<T>(...)`) there."),
    # The rule, where generic calls are introduced.
    ("At the call site, supply the concrete type:\n",
     "At the call site, supply the concrete type after `::` - the *turbofish*:\n"),
    ("Each call produces a fully specialised version of the function. See [section 12.6 Monomorphisation](#126-monomorphisation).\n",
     "Each call produces a fully specialised version of the function. See [section 12.6 Monomorphisation](#126-monomorphisation).\n"
     "\n"
     "In expression position `<` opens generic arguments **only immediately after `::`**; anywhere else it is the comparison operator. So `identity::<int>(42)`, `Array::<int>::new()`, `Pair::<int, int> { first: 1, second: 2 }`, `obj.method::<T>(x)` and `Scope::method::<T>(x)` name a specialization, while `g(LIMIT < x, MAXV > (y))` is two comparisons whatever `LIMIT` and `MAXV` are - the same text never means two things by where its names were declared. Every `::` after the arguments is an ordinary path step. Type position is unaffected: `Array<int>` in an annotation, a `where` bound or a `static match` arm, and `new Array<int>(..)`, are written as before, because a `<` after a type name is never a comparison.\n"),
]

CRYO_VA = [
    ("`va.next<i8>()` is a compile error", "`va.next::<i8>()` is a compile error"),
    ("when reading with `next<i64>()`", "when reading with `next::<i64>()`"),
]

GRAMMAR = [
    ("GenericArgs        ::= \"<\" Type (\",\" Type)* \">\"\n",
     "GenericArgs        ::= \"<\" Type (\",\" Type)* \">\"\n"
     "TurbofishArgs      ::= \"::\" GenericArgs\n"
     "                       (* generic arguments in EXPRESSION position: `<`\n"
     "                          opens them only immediately after `::`, and is\n"
     "                          the comparison operator anywhere else in an\n"
     "                          expression.  `Vec::<i32>::new()`, `f::<T>(x)`,\n"
     "                          `o.m::<T>()`, `Pair::<A, B> { .. }`, `f::<T>` as a\n"
     "                          value.  Type position uses GenericArgs directly. *)\n"),
    ("                     | \".\" MemberName GenericArgs?    (* `.` auto-derefs pointers; there is no `->` operator *)\n"
     "                     | \"::\" MemberName GenericArgs?\n",
     "                     | \".\" MemberName TurbofishArgs?  (* `.` auto-derefs pointers; there is no `->` operator *)\n"
     "                     | \"::\" MemberName TurbofishArgs?\n"),
    ("                     | Ident GenericArgs (\"(\" ArgList? \")\")?\n"
     "                     | QualName GenericArgs (\"(\" ArgList? \")\")?\n",
     "                     | Ident TurbofishArgs (\"(\" ArgList? \")\")?\n"
     "                     | QualName TurbofishArgs (\"(\" ArgList? \")\")?\n"),
    ("ImplQualPath       ::= \"(\" Type \"for\" Type \")\" \"::\" MemberName GenericArgs?\n",
     "ImplQualPath       ::= \"(\" Type \"for\" Type \")\" \"::\" MemberName TurbofishArgs?\n"),
    ("StructLit          ::= Ident GenericArgs?\n", "StructLit          ::= Ident TurbofishArgs?\n"),
]


def apply(path, pairs):
    with open(path, "r", encoding="utf-8", newline="") as f:
        s = f.read()
    for entry in pairs:
        old, new = entry[0], entry[1]
        want = entry[2] if len(entry) > 2 else 1
        n = s.count(old)
        assert n == want, "%s: %d match(es) of %r, expected %d" % (path, n, old[:70], want)
        s = s.replace(old, new)
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(s)
    print("%s: %d replacement(s)" % (os.path.relpath(path, ROOT), len(pairs)))


if __name__ == "__main__":
    apply(os.path.join(ROOT, "docs", "cryo.md"), CRYO + CRYO_VA)
    apply(os.path.join(ROOT, "docs", "grammar.md"), GRAMMAR)
