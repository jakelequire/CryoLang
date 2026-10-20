"""One-off codemod for the class deletion's mechanical part.

With the class declaration gone every declaration has a payload, so
`DeclarationNode::with_shape(x)` (which answered null for a class) is `x`
itself, and the header's kind field is named `kind`, as the expression,
statement and pattern headers' are.  With the class type kind gone, a test
for it inside an `||` or `&&` chain is a test for nothing.

Rewrites, under compiler/src and tools/CryoLSP/src:
  * `DeclarationNode::with_shape(<arg>)` -> `<arg>` (balanced parentheses);
  * `.shape` -> `.kind` (the only field named `shape` is the header's);
  * `ClassDeclNode, ` dropped from the head of an import-list line;
  * ` | DeclKind::Legacy` dropped from a `match` arm's alternatives;
  * an operand `x == TypeKind::Class` dropped from an `||` chain and
    `x != TypeKind::Class` from an `&&` chain (likewise `TemplateKind`).
    A test standing alone is left for a hand edit;
  * a field pin loses its depth up a base chain, always 0 now:
    `MemberPin::Field(0, i)`, `MemberPin::Field(_, i)` and
    `MemberPin::Field(fa.depth, fa.index)` become `MemberPin::Field(i)`
    and `MemberPin::Field(fa.index)`.  A pattern binding the depth is left
    for a hand edit;
  * in a comment line only, a list of kinds that names `class` loses it
    (`struct/class/impl` -> `struct/impl`, `struct, class, enum` ->
    `struct, enum`, ...).  Prose about classes is left for a hand edit.

usage: python scripts/class-removal-codemod.py
"""
import os
import re
import sys

ROOTS = ["compiler/src", "tools/CryoLSP/src"]
CALL = "DeclarationNode::with_shape("
FIELD = re.compile(r"\.shape\b")
IMPORTED = re.compile(r"^(\s*(?:AssocTypeDeclNode, )?)ClassDeclNode, ", re.MULTILINE)
LEGACY_ARM = " | DeclKind::Legacy"
OPERAND = r"\w[\w.]*\s*%s\s*(?:Type|Template)Kind::Class\b"
CLASS_TESTS = [
    re.compile(r"\s*\|\|\s*" + OPERAND % "=="),
    re.compile(OPERAND % "==" + r"\s*\|\|\s*"),
    re.compile(r"\s*&&\s*" + OPERAND % "!="),
    re.compile(OPERAND % "!=" + r"\s*&&\s*"),
]


PIN_DEPTHS = ["MemberPin::Field(0, ", "MemberPin::Field(_, ", "MemberPin::Field(fa.depth, "]


def strip_pin_depths(text):
    count = 0
    for spelling in PIN_DEPTHS:
        count += text.count(spelling)
        text = text.replace(spelling, "MemberPin::Field(")
    return text, count


COMMENT_KINDS = [
    ("struct/union/class/impl", "struct/union/impl"),
    ("struct/union/class", "struct/union"),
    ("struct/class/enum", "struct/enum"),
    ("struct/class/impl", "struct/impl"),
    ("struct/enum/class/trait", "struct/enum/trait"),
    ("Struct/Class/Enum", "Struct/Enum"),
    ("Struct/Class/Array", "Struct/Array"),
    ("Struct/Enum/Class", "Struct/Enum"),
    ("struct/class", "struct"),
    ("Struct/Class", "Struct"),
    ("structs/classes/impls", "structs/impls"),
    ("structs/classes", "structs"),
    ("struct, union or class", "struct or union"),
    ("struct, union, class or enum", "struct, union or enum"),
    ("struct, class, enum", "struct, enum"),
    ("struct, class or enum", "struct or enum"),
    ("struct or class", "struct"),
    ("structs, classes and unions", "structs and unions"),
    ("structs, classes, enums", "structs, enums"),
    ("structs, enums, classes,", "structs, enums,"),
    ("Struct, Class, Enum", "Struct, Enum"),
    ("Class, Trait,", "Trait,"),
]


def strip_comment_kinds(text):
    count = 0
    lines = text.split("\n")
    for i, line in enumerate(lines):
        if not line.lstrip().startswith("//"):
            continue
        for old, new in COMMENT_KINDS:
            if old in line:
                count += line.count(old)
                line = line.replace(old, new)
        lines[i] = line
    return "\n".join(lines), count


def strip_class_tests(text):
    count = 0
    for pattern in CLASS_TESTS:
        text, n = pattern.subn("", text)
        count += n
    return text, count


def strip_calls(text):
    out = []
    i = 0
    count = 0
    while True:
        j = text.find(CALL, i)
        if j < 0:
            out.append(text[i:])
            break
        out.append(text[i:j])
        k = j + len(CALL)
        depth = 1
        start = k
        while depth > 0:
            c = text[k]
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
            k += 1
        out.append(text[start:k - 1])
        i = k
        count += 1
    return "".join(out), count


def main():
    total_calls = 0
    total_fields = 0
    total_imports = 0
    total_legacy = 0
    total_tests = 0
    total_pins = 0
    total_kinds = 0
    files = 0
    for root in ROOTS:
        for dirpath, _, names in os.walk(root):
            for name in names:
                if not name.endswith(".cryo"):
                    continue
                path = os.path.join(dirpath, name)
                with open(path, "rb") as f:
                    raw = f.read()
                text = raw.decode("utf-8")
                new, calls = strip_calls(text)
                new, fields = FIELD.subn(".kind", new)
                new, imports = IMPORTED.subn(r"\1", new)
                legacy = new.count(LEGACY_ARM)
                new = new.replace(LEGACY_ARM, "")
                new, tests = strip_class_tests(new)
                new, pins = strip_pin_depths(new)
                new, kinds = strip_comment_kinds(new)
                if calls or fields or imports or legacy or tests or pins or kinds:
                    with open(path, "wb") as f:
                        f.write(new.encode("utf-8"))
                    files += 1
                    total_calls += calls
                    total_fields += fields
                    total_imports += imports
                    total_legacy += legacy
                    total_tests += tests
                    total_pins += pins
                    total_kinds += kinds
    print("files %d, with_shape calls %d, .shape fields %d, imports %d, legacy arms %d, class tests %d, pin depths %d, comment kinds %d"
          % (files, total_calls, total_fields, total_imports, total_legacy, total_tests, total_pins, total_kinds))
    return 0


if __name__ == "__main__":
    sys.exit(main())
