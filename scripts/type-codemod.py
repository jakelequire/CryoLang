"""One-off codemod: `Type` subclass casts become `Type`'s checked accessors.

`t as StructType*` becomes `t.as_struct()`, `(t as IntType*)` becomes
`t.int_kind()` (an `IntegerKind` value, as is `FloatType` -> `float_kind()`),
and an `IntType*`/`FloatType*` annotation becomes the kind enum.  The parens
around a cast that only grouped it are dropped.  A cast of `null` is kept.

Prints, for review, every local initialised from an accessor and compared
against null nearby: the cast passed a null operand through, the accessor
does not.

Usage: python scripts/type-codemod.py <root>...
"""

import os
import re
import sys

ACCESSOR = {
    "PointerType": "as_pointer",
    "ReferenceType": "as_reference",
    "ArrayType": "as_array",
    "FunctionType": "as_function",
    "TupleType": "as_tuple",
    "OptionalType": "as_optional",
    "StructType": "as_struct",
    "ClassType": "as_class",
    "EnumType": "as_enum",
    "TraitType": "as_trait",
    "TypeAliasType": "as_alias",
    "GenericParamType": "as_generic_param",
    "InstantiatedType": "as_instantiated",
    "AssocProjectionType": "as_assoc_projection",
    "FunctionTemplateType": "as_function_template",
    "IntType": "int_kind",
    "FloatType": "float_kind",
}

CAST = re.compile(r" as (?:[a-z_]+::)*(" + "|".join(ACCESSOR) + r")\*")
IDENT = re.compile(r"[A-Za-z0-9_]")
PRIMITIVE_IMPORTS = {"IntType", "FloatType", "BoolType", "CharType", "StringType",
                     "NeverType", "VaListType", "VoidType", "UnitType", "ErrorType"}


def operand_start(text, end):
    """The start of the postfix expression ending just before `end`."""
    i = end
    while i > 0:
        c = text[i - 1]
        if c in ")]":
            close, open_ = c, "(" if c == ")" else "["
            depth = 0
            j = i - 1
            while j >= 0:
                if text[j] == close:
                    depth += 1
                elif text[j] == open_:
                    depth -= 1
                    if depth == 0:
                        break
                j -= 1
            i = j
            continue
        if IDENT.match(c):
            while i > 0 and IDENT.match(text[i - 1]):
                i -= 1
            if i > 0 and text[i - 1] == ".":
                i -= 1
                continue
            if i > 1 and text[i - 2:i] == "::":
                i -= 2
                continue
            break
        break
    return i


def rewrite_casts(text):
    out = text
    for m in reversed(list(CAST.finditer(text))):
        name = m.group(1)
        start = operand_start(out, m.start())
        operand = out[start:m.start()]
        if operand == "" or operand == "null":
            if operand == "":
                raise SystemExit("no operand before cast at offset %d: %r"
                                 % (m.start(), out[max(0, m.start() - 60):m.end()]))
            continue
        new = operand + "." + ACCESSOR[name] + "()"
        lo, hi = start, m.end()
        if (lo > 0 and out[lo - 1] == "(" and hi < len(out) and out[hi] == ")"
                and (lo < 2 or not (IDENT.match(out[lo - 2]) or out[lo - 2] in ")]>"))):
            lo -= 1
            hi += 1
        out = out[:lo] + new + out[hi:]
    out = out.replace(".int_kind().int_kind", ".int_kind()")
    out = out.replace(".float_kind().float_kind", ".float_kind()")
    out = re.sub(r"\bIntType\*", "IntegerKind", out)
    out = re.sub(r"\bFloatType\*", "FloatKind", out)
    return out


def drop_imports(text):
    def fix(m):
        names = [n.strip() for n in m.group(2).split(",") if n.strip()]
        kept = [n for n in names if n not in PRIMITIVE_IMPORTS]
        if kept == names:
            return m.group(0)
        if not kept:
            return ""
        return m.group(1) + "{ " + ", ".join(kept) + " };"
    return re.sub(r"(import [A-Za-z0-9_:]+::)\{([^}]*)\};", fix, text)


KIND_FIELD = {"IntegerKind": "int_kind", "FloatKind": "float_kind"}
BRACE_IMPORT = re.compile(r"import compiler::types::\{([^}]*)\};")


def unwrap_kind_fields(text):
    """A name declared as the kind enum held an `IntType*`: `it.int_kind` is `it`."""
    for enum, field in KIND_FIELD.items():
        for name in set(re.findall(r"\b(\w+): " + enum + r"\b", text)):
            text = re.sub(r"\b" + name + r"\." + field + r"\b(?!\()", name, text)
    return text


def import_kinds(text):
    """Import each kind enum the file now names and does not import."""
    if re.search(r"^namespace compiler::types;", text, re.M):
        return text
    body = "\n".join(l for l in text.split("\n") if not l.lstrip().startswith("import "))
    for enum in KIND_FIELD:
        if not re.search(r"\b" + enum + r"\b", body):
            continue
        if re.search(r"import compiler::types::\{[^}]*\b" + enum + r"\b", text):
            continue
        m = BRACE_IMPORT.search(text)
        if m:
            names = [n.strip() for n in m.group(1).split(",") if n.strip()]
            names = sorted(names + [enum], key=lambda n: n.lower())
            inner = m.group(1)
            if "\n" in inner:
                new = "import compiler::types::{\n    " + ", ".join(names) + "\n};"
            else:
                new = "import compiler::types::{ " + ", ".join(names) + " };"
            text = text[:m.start()] + new + text[m.end():]
        else:
            anchor = re.search(r"^import compiler::types;\n", text, re.M)
            line = "import compiler::types::{ " + enum + " };\n"
            if anchor:
                text = text[:anchor.end()] + line + text[anchor.end():]
            else:
                first = re.search(r"^import ", text, re.M)
                text = text[:first.start()] + line + text[first.start():]
    return text


def null_checked(path, text):
    lines = text.split("\n")
    decl = re.compile(r"(?:const|mut) (\w+): \w+\* = .*\.as_\w+\(\);")
    for i, line in enumerate(lines):
        d = decl.search(line)
        if not d:
            continue
        name = d.group(1)
        probe = re.compile(r"\b" + name + r" *(==|!=) *null|!" + name + r"\b")
        for k in range(i + 1, min(len(lines), i + 30)):
            if probe.search(lines[k]):
                print("NULLCHECK %s:%d %s" % (path, i + 1, line.strip()))
                break


def main():
    changed = 0
    sites = 0
    for root in sys.argv[1:]:
        for dirpath, _, files in os.walk(root):
            for f in files:
                if not f.endswith(".cryo"):
                    continue
                path = os.path.join(dirpath, f)
                with open(path, "r", encoding="utf-8", newline="") as h:
                    text = h.read()
                n = len(CAST.findall(text))
                new = import_kinds(unwrap_kind_fields(drop_imports(rewrite_casts(text))))
                if new != text:
                    with open(path, "w", encoding="utf-8", newline="") as h:
                        h.write(new)
                    changed += 1
                    sites += n
                null_checked(path, new)
    print("files changed %d, casts seen %d" % (changed, sites))


if __name__ == "__main__":
    main()
