#!/usr/bin/env python3
"""Replace braced symbol imports with qualified use sites.

`import M::{ X };` plus bare `X` becomes `import M;` plus `M::X`. The module
import stays -- it is what puts the qualifier in scope and records the
dependency edge; only the symbol list goes.

WHY THIS SHAPE
--------------
A braced import naming a symbol the module does not offer binds NOTHING and says
nothing: it falls into the sub-module branch, finds no module either, and the
failure surfaces at the use site rather than the import line. A qualified name
either resolves or errors exactly where it is written. It also reaches a
strictly LARGER set: `intrinsic` and `extern` declarations are never exported,
so no import can bind one, yet `libc::strlen(...)` resolves through the module
scope.

WHAT IT MUST NOT TOUCH
----------------------
Only BARE-name positions are rewritten -- never after `.` or `::`, never a
`name:` binding site, never a method head, an enum variant, or a match arm's
pattern binds. Those reach a member, a qualified name, or a name the file
introduces itself, and none of them consults imports.

Prelude names are DELETED from the import list and left bare at the use site:
`std::prelude` is injected as a real glob, so qualifying them would be noise.
Stdlib's own modules get no prelude, so there the names are qualified like any
other.

Positions come from a LENGTH-PRESERVING mask of each line, so an offset in the
mask is the same offset in the source. Computing them on a stripped line and
applying them to the original is how a rewriter corrupts a file that has a
string or a comment on the same line as code.

Line endings are read and written with newline='' so a CRLF file stays CRLF.

Usage:
    python3 scripts/qualify-imports.py PATHSPEC [...]        # report
    python3 scripts/qualify-imports.py --apply PATHSPEC [...]
"""
import collections
import importlib.util
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_spec = importlib.util.spec_from_file_location(
    "mig", os.path.join(ROOT, "scripts", "migrate-plain-imports.py"))
mig = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(mig)

IDENT = mig.IDENT_RE
BRACE_HEAD = mig.BRACE_HEAD_RE
PLAIN = mig.PLAIN_IMPORT_RE


def unusable_segments():
    """Module-path segments that cannot be WRITTEN in a qualified path.

    A path segment is lexed as a keyword or as an identifier, and a keyword
    segment cannot be written. `default::Default` fails immediately, at any
    depth - `std::core::default::Default` fails the same way.

    PRIMITIVES ARE INCLUDED and they are the dangerous half. `string` is a type,
    so `mut &string::String` parses `&string` as a COMPLETE type and then chokes
    on the `::` left over. It parses far enough to look fine in some positions
    and fails in others, which is why this cannot be settled by trying one and
    seeing.

    Both sets are read from the compiler rather than listed here: a list of
    names in a migration script is a special case waiting to go stale, and this
    one would go stale the first time a keyword is added.
    """
    kw = set(re.findall(r'"([a-z_][a-z0-9_]*)"', _read_src(
        "compiler/src/compiler/lex/_module.cryo")))
    prim = set(re.findall(r'"([a-z0-9_()]+)"', _primitive_block()))
    return kw | prim


def _read_src(rel):
    with io.open(os.path.join(ROOT, rel), encoding="utf-8", errors="replace") as fh:
        return fh.read()


def _primitive_block():
    src = _read_src("compiler/src/compiler/resolver/res.cryo")
    i = src.find("is_primitive_spelling")
    return src[i:i + 2000] if i >= 0 else ""


UNUSABLE = unusable_segments()


def mask(src):
    """Each line with comments and string bodies replaced by spaces, LENGTH
    PRESERVED, so an offset in the mask is the same offset in the source."""
    out = []
    in_block = False
    for raw in src.split("\n"):
        line = raw.rstrip("\r")
        buf = list(line)
        i = 0
        n = len(line)
        while i < n:
            if in_block:
                if line.startswith("*/", i):
                    buf[i] = " "
                    buf[i + 1] = " "
                    i += 2
                    in_block = False
                    continue
                buf[i] = " "
                i += 1
                continue
            if line.startswith("//", i):
                for j in range(i, n):
                    buf[j] = " "
                break
            if line.startswith("/*", i):
                buf[i] = " "
                buf[i + 1] = " "
                i += 2
                in_block = True
                continue
            c = line[i]
            if c == '"' or c == "'":
                buf[i] = " "
                i += 1
                while i < n:
                    if line[i] == "\\":
                        buf[i] = " "
                        if i + 1 < n:
                            buf[i + 1] = " "
                        i += 2
                        continue
                    if line[i] == c:
                        buf[i] = " "
                        i += 1
                        break
                    buf[i] = " "
                    i += 1
                continue
            i += 1
        out.append("".join(buf))
    return out


def brace_blocks(masked):
    """[(module path, [names], first line, last line)] for every braced import."""
    out = []
    i = 0
    while i < len(masked):
        m = BRACE_HEAD.match(masked[i])
        if m:
            j, buf = i, ""
            while j < len(masked):
                buf += masked[j] + " "
                if "}" in masked[j]:
                    break
                j += 1
            inner = buf.split("{", 1)[1].split("}", 1)[0]
            out.append((m.group(2), IDENT.findall(inner), i, j))
            i = j + 1
            continue
        i += 1
    return out


# Positions the PARSER reads as a single identifier, so a path cannot be
# written there however well it resolves. Each is a name in a declaration HEAD:
# an impl's target type, a base class, a base trait, a base-constructor call.
#
# They are skipped rather than fixed. Widening them is a parser change, and the
# impl target is keyed `(leaf, target)` by three trait-lookup consumers -
# re-keying those is explicitly ruled against, so it is not a change to make in
# passing during a migration.
UNWRITABLE_POS = [
    # An impl's target, with or without the type keyword: `for struct X` and
    # `for X` are both written. Anchored on `implement` so a `for (` loop
    # cannot match.
    re.compile(r"\bimplement\b.*\bfor\s+(?:struct\s+|enum\s+|class\s+|union\s+)?"
               r"([A-Za-z_]\w*)"),
    # An INHERENT impl names its target straight after `implement`, with no
    # `trait ... for` in between.
    re.compile(r"^\s*implement\s*(?:<[^>]*>)?\s*"
               r"(?:struct|enum|class|union)\s+([A-Za-z_]\w*)"),
    re.compile(r"^\s*(?:public\s+|private\s+)?type\s+(?:class|trait|struct)\s+"
               r"[A-Za-z_]\w*\s*(?:<[^{]*>)?\s*:\s*([A-Za-z_]\w*)"),
    re.compile(r"\)\s*:\s*([A-Za-z_]\w*)\s*\("),
]


def enum_body_lines(masked):
    """Line indices inside a `type enum` body.

    A variant DECLARATION with a payload - `Array(Array<JsonValue>);` - looks
    exactly like a call statement, so the leading name cannot be told from a use
    by its shape. Inside an enum body it is always the variant being declared;
    the payload beside it is a real type and is still qualified."""
    out = set()
    depth = 0
    enum_at = None
    for i, code in enumerate(masked):
        if enum_at is None and re.match(
                r"^\s*(?:public\s+|private\s+)?type\s+enum\s", code):
            enum_at = depth
        if enum_at is not None and depth >= enum_at:
            out.add(i)
        depth += code.count("{") - code.count("}")
        if enum_at is not None and depth <= enum_at:
            enum_at = None
    return out


def rewrite_positions(code, in_enum=False, module_names=frozenset()):
    """Offsets in `code` of identifiers used as BARE names.

    The same rule `migrate-plain-imports.free_idents` applies, reported as
    positions rather than as a set so each occurrence can be edited, minus the
    declaration-head positions the parser will not accept a path in."""
    used, bound = mig.free_idents(code)
    blocked = []
    for rx in UNWRITABLE_POS:
        for m in rx.finditer(code):
            blocked.append(m.span(1))
    spots = []
    first = True
    for m in IDENT.finditer(code):
        s, e = m.span()
        if in_enum and first:
            # The leading name on a line in an enum body is the variant being
            # DECLARED, not a use. Its payload beside it is a real type and is
            # still qualified.
            first = False
            continue
        first = False
        if m.group(0) not in used:
            continue
        if s > 0 and code[s - 1] in ".:":
            continue
        # A name that is already the HEAD of a qualified path, and which this
        # file imports as a MODULE, is the module - not a bare use to qualify.
        # `std::fs::metadata` declares a free function `metadata`, so the
        # source's own `metadata::metadata(from)` became
        # `metadata::metadata::metadata(from)`. A head that is NOT a module
        # spelling is a type, and a type still needs its qualifier.
        if code[e:e + 2] == "::" and m.group(0) in module_names:
            continue
        if any(bs <= s < be for bs, be in blocked):
            continue
        spots.append((s, e, m.group(0)))
    return spots, bound


def plan(world, path):
    """(edits, dropped, notes) for one file, or None when nothing to do."""
    src = mig.read(path)
    masked = mask(src)
    raw = src.split("\n")
    ns = world.per_file[path][0]
    if ns is None:
        return None
    no_prelude = ns.startswith("std::") or "![no_std]" in src
    prelude = set() if no_prelude else set(world.offers("std::prelude"))
    own = world.per_file[path][1]

    blocks = brace_blocks(masked)
    if not blocks:
        return None

    # Module spellings this file already uses, so a qualifier cannot collide.
    # DISTINCT module paths per leaf. A file importing one module both plainly
    # and in braces names one module twice, which is not a collision -- counting
    # occurrences made every such file fall back to the fully-qualified form.
    leaves = collections.defaultdict(set)
    for written, _n, _a, _b in blocks:
        leaves[written.split("::")[-1]].add(written)
    for line in masked:
        m = PLAIN.match(line)
        if m:
            leaves[m.group(3).split("::")[-1]].add(m.group(3))

    # A name used in a declaration HEAD cannot be qualified there, so it keeps
    # its braced import and stays bare EVERYWHERE in the file. Qualifying its
    # other uses while the head stayed bare would leave the head unresolved once
    # the import went.
    head_only = set()
    for code in masked:
        for rx in UNWRITABLE_POS:
            for m in rx.finditer(code):
                head_only.add(m.group(1))

    qualify = {}      # name -> qualifier
    drop = set()      # names removed from the import list, left bare
    keep = {}         # module path -> names that stay (submodules, unoffered)
    notes = []
    for written, names, _a, _b in blocks:
        target = world.resolve(ns, written)
        leaf = written.split("::")[-1]
        # A spelling two of this file's module paths share cannot be the
        # qualifier; the full written path is unambiguous by construction.
        qual = leaf if len(leaves[leaf]) == 1 else written
        blocked = [g for g in qual.split("::") if g in UNUSABLE]
        for name in names:
            if blocked:
                # The module cannot be NAMED in a path, so its symbols keep the
                # braced form. That is a hole in "qualify, don't import" rather
                # than a preference, and it is reported rather than absorbed.
                keep.setdefault(written, []).append(name)
                notes.append("unwritable qualifier `%s` (keyword segment %s): %s"
                             % (qual, blocked[0], name))
                continue
            if target is None:
                keep.setdefault(written, []).append(name)
                notes.append("module unresolved: %s" % written)
                continue
            offered = world.offers(target)
            if name not in offered:
                # A submodule item, or a name the module does not offer at all.
                keep.setdefault(written, []).append(name)
                continue
            if name in head_only:
                keep.setdefault(written, []).append(name)
                notes.append("declaration head takes no path: %s" % name)
                continue
            if name in prelude:
                drop.add(name)
                continue
            if name in own:
                keep.setdefault(written, []).append(name)
                notes.append("shadows own declaration: %s" % name)
                continue
            if name in qualify and qualify[name] != qual:
                notes.append("two qualifiers for %s: %s / %s"
                             % (name, qualify[name], qual))
                keep.setdefault(written, []).append(name)
                continue
            qualify[name] = qual
    return (src, raw, masked, blocks, qualify, drop, keep, notes,
            set(leaves.keys()))


def apply_to(path, planned, keep_lines=False):
    """Rewrite the file. `keep_lines` pads each import block back to its
    original height.

    That exists for VERIFICATION, not for delivery. An object carries line
    information, so deleting an import line shifts every line below it and the
    object changes for that reason alone -- which swamps the only signal the
    object baseline exists to give. Padded, a pure requalification is
    byte-identical, and the delivered form then differs from the verified one by
    blank lines only."""
    (src, raw, masked, blocks, qualify, drop, keep, _notes,
     module_names) = planned
    eol = "\r\n" if "\r\n" in src else "\n"
    enum_lines = enum_body_lines(masked)
    import_lines = set()
    for _w, _n, a, b in blocks:
        for k in range(a, b + 1):
            import_lines.add(k)

    out = []
    sites = 0
    for i, line in enumerate(raw):
        text = line.rstrip("\r")
        if i in import_lines:
            out.append(text)
            continue
        spots, _bound = rewrite_positions(masked[i], i in enum_lines,
                                          module_names)
        edits = [(s, e, qualify[n]) for s, e, n in spots if n in qualify]
        if edits:
            for s, e, q in sorted(edits, reverse=True):
                text = text[:s] + q + "::" + text[s:]
                sites += 1
        out.append(text)

    # Rewrite the import blocks themselves.
    final = []
    i = 0
    while i < len(out):
        blk = None
        for w, n, a, b in blocks:
            if a == i:
                blk = (w, n, a, b)
                break
        if blk is None:
            final.append(out[i])
            i += 1
            continue
        written, names, a, b = blk
        left = keep.get(written, [])
        indent = re.match(r"^([ \t]*)", raw[a]).group(1)
        before = len(final)
        if left:
            final.append(mig.wrap(indent, written, sorted(set(left)), eol)
                         .rstrip("\r\n"))
        # A plain module import must exist for the qualifier to resolve; the
        # migration that preceded this one left one beside every braced form,
        # so this only fires where that is not true.
        leaf_q = written.split("::")[-1]
        if any(q in (leaf_q, written) for q in qualify.values()):
            want = "%simport %s;" % (indent, written)
            if not any(l.strip() == want.strip() for l in raw):
                final.append(want)
        if keep_lines:
            while len(final) - before < (b - a + 1):
                final.append("")
        i = b + 1
    with io.open(os.path.join(ROOT, path), "w", encoding="utf-8", newline="") as fh:
        fh.write(eol.join(final))
    return sites


def main(argv):
    do_apply = "--apply" in argv
    keep_lines = "--keep-lines" in argv
    specs = [a for a in argv if not a.startswith("--")]
    if not specs:
        print("a pathspec is required; this tool does not sweep by default",
              file=sys.stderr)
        return 2
    world = mig.World(mig.tracked_files([]))
    total_sites = 0
    for path in mig.tracked_files(specs):
        planned = plan(world, path)
        if planned is None:
            continue
        _s, _r, _m, _b, qualify, drop, keep, notes, _mn = planned
        if not qualify and not drop:
            continue
        if do_apply:
            n = apply_to(path, planned, keep_lines)
            total_sites += n
            print("%s\t%d sites\t%d qualified\t%d prelude-dropped"
                  % (path, n, len(qualify), len(drop)))
        else:
            print("%s\tqualify=%d drop=%d keep=%d"
                  % (path, len(qualify), len(drop),
                     sum(len(v) for v in keep.values())))
        for note in notes:
            print("    NOTE %s" % note, file=sys.stderr)
    if do_apply:
        print("total sites rewritten: %d" % total_sites, file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
