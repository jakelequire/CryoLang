p = 'tests/tests/negative/E0203_refused_signature_no_cascade.cryo'
s = open(p, encoding='utf-8', newline='').read()
old = """// A local whose annotation was refused keeps no type either, so its member
// read is not "no field `v` on type `i32`".  The expectation lines are
// exhaustive: an extra diagnostic anywhere in this file fails the test.
"""
new = """// A local whose annotation was refused keeps no type either, so its member
// read is not "no field `v` on type `i32`".  A refused function taken as a
// VALUE - bare, through its module, or a type's static - reports nothing
// either: "cannot find value `ret_bad`" was the same false statement as the
// call's, and `Holder::make_bad` as a value was typed `() -> ?` off the
// owner's method table and refused against its annotation.  The expectation
// lines are exhaustive: an extra diagnostic anywhere in this file fails the
// test.
"""
assert s.count(old) == 1
s = s.replace(old, new)
old2 = """    const x: CfNope = 1;                       //~ ERROR[E0203] cannot find type `CfNope` in this scope
    const w: i32 = x.v;
    return a + b + c + d + e + f + g + w;
"""
new2 = """    const x: CfNope = 1;                       //~ ERROR[E0203] cannot find type `CfNope` in this scope
    const w: i32 = x.v;
    const fv: (i32) -> i32 = ret_bad;
    const gv: (i32) -> i32 = CryoTests::Negative::RefusedSignatureNoCascade::ret_bad;
    const sv: () -> i32 = CfHolder::make_bad;
    return a + b + c + d + e + f + g + w + fv(1) + gv(1) + sv();
"""
assert s.count(old2) == 1
s = s.replace(old2, new2)
open(p, 'w', encoding='utf-8', newline='').write(s)
print("ok")
