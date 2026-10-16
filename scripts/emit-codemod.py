"""One-off codemod: codegen's emitters return what they emitted instead of
leaving it on `state.last_value` / `state.last_is_lvalue`.

    python scripts/emit-codemod.py FILE FUNC...

Each named method (4-space indent, `-> void {`) is rewritten to return an
`Emitted`: it opens with `mut out: Emitted = Emitted::none();`, every
`this.state.last_value` / `this.state.last_is_lvalue` in it becomes
`out.value` / `out.is_lvalue`, every `return;` becomes `return out;`, and
`return out;` closes it unless it already ends in a return.  `FUNC` may be
`visit_*`, which names every method starting `visit_`.

A read of `out` after a call that used to write the channel is NOT fixed
here: those lines are reported for a hand edit.
"""
import io
import re
import sys


def read(path):
    with io.open(path, encoding="utf-8", newline="") as fh:
        return fh.read().splitlines(True)


def write(path, lines):
    with io.open(path, "w", encoding="utf-8", newline="") as fh:
        fh.write("".join(lines))


def wanted(name, funcs):
    return name in funcs or ("visit_*" in funcs and name.startswith("visit_"))


def main():
    path, funcs = sys.argv[1], set(sys.argv[2:])
    lines = read(path)
    eol = "\r\n" if lines[0].endswith("\r\n") else "\n"
    out, n, reads = [], 0, []
    i = 0
    while i < len(lines):
        m = re.match(r"    ([a-z_][a-z0-9_]*)\(", lines[i])
        if not (m and wanted(m.group(1), funcs)):
            out.append(lines[i])
            i += 1
            continue
        # The signature may span lines; it ends at the line holding `{`.
        j = i
        while not lines[j].rstrip().endswith("{"):
            j += 1
        if "-> void {" not in lines[j]:
            raise SystemExit("%s:%d: %s does not return void" % (path, i + 1, m.group(1)))
        for k in range(i, j):
            out.append(lines[k])
        out.append(lines[j].replace("-> void {", "-> Emitted {"))
        out.append("        mut out: Emitted = Emitted::none();" + eol)
        k = j + 1
        body = []
        while lines[k].rstrip("\r\n") != "    }":
            body.append(lines[k])
            k += 1
        for b in body:
            b = b.replace("this.state.last_value", "out.value")
            b = b.replace("this.state.last_is_lvalue", "out.is_lvalue")
            b = re.sub(r"\breturn;", "return out;", b)
            if re.search(r"out\.(value|is_lvalue)\b(?!\s*=[^=])", b):
                reads.append(len(out) + 1)
            out.append(b)
        last = [b for b in body if b.strip()]
        if not last or not last[-1].strip().startswith("return"):
            out.append("        return out;" + eol)
        out.append(lines[k])
        i = k + 1
        n += 1
    write(path, out)
    print("%s: %d methods; reads of `out` to check by hand at lines %s" % (path, n, reads))


PRODUCERS = ("emit|emit_binary|emit_unary|short_circuit|call_through_value|emit_panic_unwind|"
             "emit_catch_unwind|codegen_literal|codegen_identifier|codegen_struct_literal|"
             "codegen_tuple_literal|emit_capturing|emit_new|emit_new_array|emit_delete|"
             "emit_dynamic|emit_fill|codegen_member_access|codegen_array_access|visit_[a-z_]+|"
             "generate_expression|generate_statement|generate_block")
CALL = re.compile(r"^(\s+)(this\.(?:[a-z_]+\.)?(?:%s)\()" % PRODUCERS)


def tail(path):
    """A producer called as a statement right before `return out;` is the
    method's result: fold the two into `return <call>;`."""
    lines = read(path)
    out, n = [], 0
    i = 0
    while i < len(lines):
        m = CALL.match(lines[i])
        if m and "diag." not in lines[i]:
            j = i
            while not lines[j].rstrip().endswith(";"):
                j += 1
            if j + 1 < len(lines) and lines[j + 1].strip() == "return out;":
                out.append(m.group(1) + "return " + lines[i][len(m.group(1)):])
                out.extend(lines[i + 1:j + 1])
                i = j + 2
                n += 1
                continue
        out.append(lines[i])
        i += 1
    write(path, out)
    left = [k + 1 for k, l in enumerate(out) if CALL.match(l) and "return" not in l
            and "diag." not in l]
    print("%s: %d calls folded; statement calls left at %s" % (path, n, left))


def prune(path):
    """Drop the `out` accumulator from a method that no longer reads,
    writes or returns it."""
    lines = read(path)
    out, n = [], 0
    i = 0
    while i < len(lines):
        if lines[i].strip() == "mut out: Emitted = Emitted::none();":
            j = i + 1
            while lines[j].rstrip("\r\n") != "    }":
                j += 1
            if not any(re.search(r"\bout\b", l) for l in lines[i + 1:j]):
                n += 1
                i += 1
                continue
        out.append(lines[i])
        i += 1
    write(path, out)
    print("%s: %d unused accumulators dropped" % (path, n))


if sys.argv[1] == "--prune":
    for p in sys.argv[2:]:
        prune(p)
elif sys.argv[1] == "--tail":
    for p in sys.argv[2:]:
        tail(p)
else:
    main()
