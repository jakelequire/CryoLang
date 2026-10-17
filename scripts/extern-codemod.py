"""One-off: move the `NodeKind::ExternBlock` arm of type resolution's and
name resolution's top-level dispatch into their `NodeKind::Declaration` arm
as a `DeclKind::Extern(<var>)` arm, once ExternBlockNode becomes the
ExternBlock payload.

In each file the old arm is found by its head line, its cast line
(`const <var>: ExternBlockNode* = <src> as ExternBlockNode*;`) is dropped,
its body is re-indented one level deeper (it now sits inside a `match`),
`<var>.span` becomes `decl.span` (the header the Declaration arm already
names `decl`), and the arm is inserted as the first arm of the
`match (decl.shape)` inside the Declaration arm.  Refuses to run unless
each anchor is found exactly once.

Deletion point: the commit after the one that ran it.
"""
import pathlib
import re
import sys

# (file, extern arm head, where the same dispatch's Declaration arm stands)
FILES = [
    ("compiler/src/compiler/passes/type_resolution.cryo", "                NodeKind::ExternBlock => {", "after"),
    ("compiler/src/compiler/resolver/name_resolution.cryo", "            NodeKind::ExternBlock => {", "before"),
]


def one(lines, pred, what):
    hits = [i for i, l in enumerate(lines) if pred(l)]
    if len(hits) != 1:
        sys.exit("%s: %d matches" % (what, len(hits)))
    return hits[0]


for path, head, where in FILES:
    p = pathlib.Path(path)
    raw = p.read_bytes()
    crlf = b"\r\n" in raw
    lines = raw.decode("utf-8").replace("\r\n", "\n").split("\n")

    start = one(lines, lambda l: l == head, path + " arm head")
    indent = head[:len(head) - len(head.lstrip())]
    end = next(i for i in range(start + 1, len(lines)) if lines[i] == indent + "}")
    cast = re.match(r"\s*const (\w+): ExternBlockNode\* = \w+ as ExternBlockNode\*;$", lines[start + 1])
    if not cast:
        sys.exit("%s: no cast line after the arm head" % path)
    var = cast.group(1)
    body = lines[start + 2:end]

    # The Declaration arm of the same dispatch: the next one after the
    # extern arm, or the last one before it (name resolution has a second,
    # in keep_declared, after it), found before the extern arm is removed.
    arms = [i for i, l in enumerate(lines) if l == indent + "NodeKind::Declaration => {"]
    if where == "after":
        arms = [i for i in arms if i > end][:1]
    else:
        arms = [i for i in arms if i < start][-1:]
    if not arms:
        sys.exit("%s: no Declaration arm %s the extern arm" % (path, where))
    decl_arm = arms[0]
    print("%s: extern arm at %d, Declaration arm at %d" % (path, start + 1, decl_arm + 1))
    del lines[start:end + 1]
    if decl_arm > end:
        decl_arm -= end + 1 - start
    match_line = next(i for i in range(decl_arm + 1, len(lines)) if lines[i].strip() == "match (decl.shape) {")
    arm_indent = lines[match_line + 1][:len(lines[match_line + 1]) - len(lines[match_line + 1].lstrip())]
    moved = [arm_indent + "DeclKind::Extern(%s) => {" % var]
    for l in body:
        l = re.sub(r"\b%s\.span\b" % var, "decl.span", l)
        moved.append(("        " + l) if l else l)
    moved.append(arm_indent + "}")
    lines[match_line + 1:match_line + 1] = moved

    out = "\n".join(lines)
    if crlf:
        out = out.replace("\n", "\r\n")
    p.write_bytes(out.encode("utf-8"))
    print("%s: moved %d lines (var %s)" % (path, len(body), var))
