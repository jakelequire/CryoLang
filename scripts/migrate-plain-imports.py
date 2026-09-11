#!/usr/bin/env python3
"""Give every plain `import M;` the braced form its file actually needs.

A plain `import M;` names a module and binds no names. Every file that reached
a name through the glob needs that name written down. This tool computes, per
file and per plainly-imported module, the names the file MENTIONS that the
module OFFERS, and writes them into a braced import.

WHY THE DEMAND IS COMPUTED THIS WAY
-----------------------------------
Two earlier attempts measured it differently and both were incomplete by
construction:

  * from `Resolver::lookup`, which sees one resolution path -- a type
    ANNOTATION resolves through the type layer and never appears there;
  * from the compiler's own `E0203` stream, which names the generic HEAD when
    an ARGUMENT is what failed, so the fixpoint added imports for names that
    were never missing.

This computes an UPPER BOUND instead -- every offered name the file's token
stream mentions -- which needs no model of resolution at all. Importing a name
the file did not need is inert; the one thing it must not do is bind a name the
file was reaching somewhere else, so a name offered by two plainly-imported
modules of one file is REFUSED and reported rather than assigned to one of them.

The declaration scan reuses `scripts/api-index.py`'s comment and string-literal
stripping, and adds the top-level forms an API index has no reason to list
(`extern "C"` blocks, `intrinsic`, `static`, `type trait`).

Line endings are read and written with newline='' so a CRLF file stays CRLF.
The tree is not uniformly LF and a rewriter that assumes it is corrupts the
files it touches least visibly.

Usage:
    python3 scripts/migrate-plain-imports.py [PATHSPEC ...]        # report
    python3 scripts/migrate-plain-imports.py --apply [PATHSPEC ...]
"""
import collections
import importlib.util
import io
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _load_api_index():
    """`api-index.py` is not an identifier, so it is loaded by path rather than
    imported; its `strip_noise` is the stripper this scan has to agree with."""
    path = os.path.join(ROOT, "scripts", "api-index.py")
    spec = importlib.util.spec_from_file_location("api_index", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


api_index = _load_api_index()

NS_RE = re.compile(r"^\s*namespace\s+([A-Za-z_][\w:]*)\s*;")
PLAIN_IMPORT_RE = re.compile(
    r"^([ \t]*)(import|export)[ \t]+([A-Za-z_][\w:]*)[ \t]*"
    r"(?:as[ \t]+([A-Za-z_]\w*)[ \t]*)?;[ \t]*$")
BRACE_HEAD_RE = re.compile(
    r"^([ \t]*)(?:import|export)[ \t]+([A-Za-z_][\w:]*)[ \t]*::[ \t]*\{")
STAR_IMPORT_RE = re.compile(r"^[ \t]*(?:import|export)[ \t]+([A-Za-z_][\w:]*)::\*[ \t]*;")
REEXPORT_MODULE_RE = re.compile(r"^\s*(?:public\s+|private\s+)?module\s+([A-Za-z_][\w:]*)\s*;")
IDENT_RE = re.compile(r"[A-Za-z_]\w*")

# Top-level declarations that bind an importable name. Visibility is the
# `private` KEYWORD and nothing else: `public` is the default at top level, so
# counting the keyword measures how the source is written, not what it exports.
DECL_RES = [
    re.compile(r"^\s*(?:(public|private)\s+)?type\s+(?:struct|enum|class|union|trait)\s+"
               r"([A-Za-z_]\w*)"),
    re.compile(r"^\s*(?:(public|private)\s+)?trait\s+([A-Za-z_]\w*)"),
    re.compile(r"^\s*(?:(public|private)\s+)?function\s+([A-Za-z_]\w*)"),
    re.compile(r"^\s*(?:(public|private)\s+)?(?:intrinsic\s+)?(?:const|static)\s+"
               r"([A-Za-z_]\w*)\s*:"),
]
# `intrinsic function` is DECLARED and never EXPORTED -- `forward_declare_node`
# calls `declare` for it and not `export_symbol` -- so no import of any form can
# bind one, and a braced import naming one falls into the sub-module branch and
# silently binds nothing. Counting them as offered is what put 23 such items in
# the tree; they are not demand and this scan must not see them.
INTRINSIC_FN_RE = re.compile(r"^\s*(?:(?:public|private)\s+)?intrinsic\s+function\s")
EXTERN_FN_RE = re.compile(r"^\s*(?:(public|private)\s+)?function\s+([A-Za-z_]\w*)")
# A method declared in a type body, and an enum variant: both introduce the
# name rather than use it.
METHOD_DECL_RE = re.compile(
    r"^\s*(?:(?:public|private|static|async|unsafe)\s+)*([A-Za-z_]\w*)\s*(?:<[^(]*>)?"
    r"\s*\(([^)]*)\)\s*(->|\{)?")
CONTROL_KEYWORDS = {"if", "while", "for", "switch", "match", "return", "else",
                    "catch", "new", "defer", "unsafe", "await"}
VARIANT_RE = re.compile(r"^\s*([A-Za-z_]\w*)\s*(?:=[^;]*)?;\s*$")
VARARGS_RE = re.compile(r"([A-Za-z_]\w*)\s*\.\.\.")


def free_idents(code):
    """The identifiers a line uses as BARE names -- the only positions an
    import can answer.

    Dropping the rest is what keeps the upper bound honest. `x.exists()` and
    `Mod::exists()` reach a member and a qualified name, neither of which
    consults imports, and `source_length: i64` is a field or parameter being
    declared. Counting those three as demand imports a free function whose
    name merely collides with a field, which is how an inert insertion turns
    into a contested one."""
    used = set()
    bound = set()
    m = METHOD_DECL_RE.match(code)
    if m and m.group(1) not in CONTROL_KEYWORDS:
        # A method HEAD, told from a call by its receiver, its return arrow or
        # the body it opens. A call's arguments carry none of those.
        args = m.group(2)
        if "this" in args or m.group(3) or code.rstrip().endswith("{"):
            bound.add(m.group(1))
    m = VARIANT_RE.match(code)
    if m:
        bound.add(m.group(1))
    if "=>" in code:
        # A match arm's pattern BINDS its parenthesised names: in
        # `Option::Some(os) =>`, `os` is introduced here and every bare `os`
        # below it is that binding.
        head = code.split("=>", 1)[0]
        for group in re.findall(r"\(([^()]*)\)", head):
            bound.update(IDENT_RE.findall(group))
    bound.update(VARARGS_RE.findall(code))
    for m in IDENT_RE.finditer(code):
        s, e = m.span()
        j = e
        while j < len(code) and code[j] in " \t":
            j += 1
        if j < len(code) and code[j] == ":" and code[j:j + 2] != "::":
            # `name:` is a binding site -- a field, a parameter, a local, a
            # type parameter. The file names it itself, so a bare use of it
            # elsewhere is that binding and not something an import supplies.
            bound.add(m.group(0))
            continue
        if s > 0 and code[s - 1] in ".:":
            continue
        used.add(m.group(0))
    return used, bound


def norm(ns):
    """Namespace key that survives the two spellings the tree uses for one
    module: `public module SymbolStr;` names `...::symbol_str`."""
    return ns.lower().replace("_", "")


def tracked_files(pathspecs):
    """Every tracked `.cryo` except `legacy/`, which nothing builds.

    `legacy/stdlib` declares the same namespaces as `stdlib` with different
    contents, so leaving it in makes one namespace offer the union of two
    libraries -- which reads as a name being offered by two modules at once.
    That is the shape this tool refuses, so it would refuse dozens of real
    files over a tree no target compiles."""
    args = ["git", "ls-files", "-z"] + (list(pathspecs) if pathspecs else ["*.cryo"])
    out = subprocess.run(args, cwd=ROOT, capture_output=True, text=True).stdout
    return [p for p in out.split("\0")
            if p.endswith(".cryo") and not p.startswith("legacy/")]


def project_root(path):
    """The directory whose `cryoconfig` governs this file, or its top-level
    directory. Two projects may declare one namespace -- every `examples/`
    entry is `namespace Main;` -- and they are separate compilations, so a
    name declared in one is not in scope in the other."""
    d = os.path.dirname(path)
    while d:
        if os.path.exists(os.path.join(ROOT, d, "cryoconfig")):
            return d
        parent = os.path.dirname(d)
        if parent == d:
            break
        d = parent
    return path.split("/")[0]


def read(path):
    with io.open(os.path.join(ROOT, path), encoding="utf-8",
                 errors="replace", newline="") as fh:
        return fh.read()


def code_lines(src):
    """The file's lines with comments and string literals blanked and block
    comments removed, one output line per input line so line numbers hold."""
    out = []
    in_block = False
    for raw in src.split("\n"):
        line = raw.rstrip("\r")
        if in_block:
            if "*/" in line:
                line = line.split("*/", 1)[1]
                in_block = False
            else:
                out.append("")
                continue
        # Line comments and string bodies go FIRST. A `/*` inside either is
        # not a block comment, and treating it as one swallows every line up
        # to the next `*/` -- which silently moves brace depth, so local
        # declarations then read as top-level exports.
        code = api_index.strip_noise(line)
        while "/*" in code:
            head, rest = code.split("/*", 1)
            if "*/" in rest:
                code = head + rest.split("*/", 1)[1]
            else:
                code = head
                in_block = True
                break
        out.append(code)
    return out


def scan(path):
    """(namespace, declared names, reexport targets, mentioned identifiers)."""
    lines = code_lines(read(path))
    ns = None
    declared = set()
    reexports = []
    mentioned = set()
    self_bound = set()
    depth = 0
    extern_depth = None
    in_brace_import = False
    for code in lines:
        if ns is None:
            m = NS_RE.match(code)
            if m:
                ns = m.group(1)
                depth += code.count("{") - code.count("}")
                continue
        # An import block is recognised BEFORE depth is consulted, and its
        # braces are not counted. A multi-line `import M::{` opens a brace, so
        # its closing `};` arrives at depth 1; a scan that only looks at depth
        # 0 never clears the flag, and from there every declaration in the file
        # reads as another line of the import list.
        if in_brace_import:
            in_brace_import = "}" not in code
            continue
        is_import = False
        if depth == 0:
            m = REEXPORT_MODULE_RE.match(code)
            pm = PLAIN_IMPORT_RE.match(code)
            bm = BRACE_HEAD_RE.match(code)
            if m:
                reexports.append((m.group(1), None))
                is_import = True
            elif pm:
                is_import = True
                # `export M;` is a re-export edge, not just an import. It is
                # the OTHER half of the re-export graph -- `public module M;`
                # is the half the stdlib facades use, and reading only that
                # one leaves every `export`-built facade offering nothing.
                if pm.group(2) == "export":
                    reexports.append((pm.group(3), None))
            elif bm:
                is_import = True
                in_brace_import = "}" not in code
                if code.lstrip().startswith("export"):
                    # `export M::{ A, B };` grants exactly A and B, so the
                    # names are carried rather than the whole module.
                    reexports.append(
                        (bm.group(2), IDENT_RE.findall(
                            code.split("{", 1)[1].split("}", 1)[0])))
                if in_brace_import:
                    continue
            elif INTRINSIC_FN_RE.match(code):
                pass
            else:
                for rx in DECL_RES:
                    m = rx.match(code)
                    if m:
                        if m.group(1) != "private":
                            declared.add(m.group(2))
                        break
            if code.strip().startswith("extern ") and "{" in code:
                extern_depth = depth
        elif extern_depth is not None and depth == extern_depth + 1:
            m = EXTERN_FN_RE.match(code)
            if m and m.group(1) != "private":
                declared.add(m.group(2))
        # An import line's own text is not a use of the names in it.
        if not is_import:
            used, bound = free_idents(code)
            mentioned |= used
            self_bound |= bound
        depth += code.count("{") - code.count("}")
        if extern_depth is not None and depth <= extern_depth:
            extern_depth = None
    return ns, declared, reexports, mentioned - self_bound


def resolve_ns(by_norm, home, written):
    """The namespace a written module path names, tried the way the loader
    does: relative to the writing module first, then absolute, then under
    `std`, then as a suffix of exactly one namespace."""
    cands = []
    if home:
        parts = home.split("::")
        for cut in range(len(parts), 0, -1):
            cands.append("::".join(parts[:cut]) + "::" + written)
    cands.append(written)
    cands.append("std::" + written)
    for c in cands:
        hit = by_norm.get(norm(c))
        if hit:
            return hit
    tail = "::" + norm(written)
    matches = sorted({v for k, v in by_norm.items() if k.endswith(tail)})
    return matches[0] if len(matches) == 1 else None


class World:
    def __init__(self, files):
        self.per_file = {}
        self.ns_decls = collections.defaultdict(set)
        self.ns_files = collections.defaultdict(list)
        self.ns_reexports = collections.defaultdict(set)
        self.by_norm = {}
        for path in files:
            ns, declared, reexports, mentioned = scan(path)
            self.per_file[path] = (ns, declared, reexports, mentioned)
            if ns is None:
                continue
            self.ns_decls[ns] |= declared
            self.ns_files[ns].append(path)
            self.by_norm.setdefault(norm(ns), ns)
            for written, only in reexports:
                self.ns_reexports[ns].add(
                    (written, tuple(sorted(only)) if only else None))
        self._offers = {}

    def resolve(self, home, written):
        return resolve_ns(self.by_norm, home, written)

    def offers(self, ns, seen=frozenset()):
        """Every name a module offers, mapped to the module that DECLARED it:
        its own public declarations plus, through its `export` /
        `public module` edges, everything they offer. The walk is transitive
        because `export` is, and cycle-safe because a facade of a facade is
        legal.

        The declaring module is the identity. Two paths to one declaration --
        `std::time` re-exporting `std::time::duration` -- are one name with one
        owner, not two modules competing for it."""
        if ns in self._offers:
            return self._offers[ns]
        if ns in seen:
            return {}
        out = {n: ns for n in self.ns_decls.get(ns, ())}
        for written, only in self.ns_reexports.get(ns, ()):
            target = self.resolve(ns, written)
            if target is None:
                continue
            for name, owner in self.offers(target, seen | {ns}).items():
                if only is not None and name not in only:
                    continue
                out.setdefault(name, owner)
        if not seen:
            self._offers[ns] = out
        return out


def wrap(indent, module, names, eol):
    """One import line, wrapped rather than run on forever."""
    joined = ", ".join(names)
    head = "%simport %s::{ " % (indent, module)
    if len(head) + len(joined) + 3 <= 96:
        return "%s%s };%s" % (head, joined, eol)
    rows, cur, width = [], [], len(indent) + 4
    for n in names:
        if cur and width + len(n) + 2 > 92:
            rows.append(", ".join(cur) + ",")
            cur, width = [], len(indent) + 4
        cur.append(n)
        width += len(n) + 2
    if cur:
        rows.append(", ".join(cur))
    body = ("%s    " % indent).join(r + eol for r in rows)
    return "%simport %s::{%s%s    %s%s};%s" % (
        indent, module, eol, indent, body, indent, eol)


def file_plan(world, path, prelude_names):
    """What this file needs written down, and what it cannot decide."""
    ns, _declared, _rx, mentioned = world.per_file[path]
    src = read(path)
    lines = code_lines(src)
    raw = src.split("\n")

    plains = []          # (line index, indent, module ns, written path)
    braced_names = set()
    braced_line = {}     # module ns -> line index of its brace head
    for i, code in enumerate(lines):
        m = PLAIN_IMPORT_RE.match(code)
        if m and not STAR_IMPORT_RE.match(code):
            target = world.resolve(ns, m.group(3))
            if target:
                plains.append((i, m.group(1), target, m.group(3)))
            if m.group(4):
                braced_names.add(m.group(4))
            continue
        m = BRACE_HEAD_RE.match(code)
        if m:
            target = world.resolve(ns, m.group(2))
            j, buf = i, ""
            while j < len(lines):
                buf += lines[j]
                if "}" in lines[j]:
                    break
                j += 1
            inner = buf.split("{", 1)[1].split("}", 1)[0]
            braced_names.update(IDENT_RE.findall(inner))
            if target and target not in braced_line:
                braced_line[target] = i

    # A file's own module is in scope by declaration, and re-importing a name
    # it declares is a redeclaration rather than a convenience.
    own = set()
    root = project_root(path)
    for sibling in world.ns_files.get(ns, ()):
        if project_root(sibling) == root:
            own |= world.per_file[sibling][1]

    # A spelling this file uses to name a MODULE is not demand for a symbol
    # of the same name. `std::sys::syscall` declares a function `syscall`, and
    # importing it would shadow the module in `syscall::MoveFileExA`.
    module_leaves = {w.split("::")[-1] for _i, _ind, _t, w in plains}
    for code in lines:
        m = BRACE_HEAD_RE.match(code)
        if m:
            module_leaves.add(m.group(2).split("::")[-1])

    wanted = {}
    declarer = {}
    for _i, _ind, target, _written in plains:
        offered = world.offers(target)
        cand = ((set(offered) & mentioned)
                - prelude_names - braced_names - own - module_leaves)
        if cand:
            wanted.setdefault(target, set())
            wanted[target] |= cand
            for n in cand:
                declarer.setdefault((target, n), offered[n])

    # A name DECLARED by two plainly-imported modules of one file is what the
    # single-namespace ruling exists to make unwriteable. Choosing an owner
    # inside a migration would put the defect back under a layer of automation.
    # Two paths to ONE declaration are not that, and get the one whose path is
    # the declaration's own.
    seen = {}
    contested = collections.defaultdict(set)
    for target in sorted(wanted):
        for n in sorted(wanted[target]):
            owner = declarer[(target, n)]
            if n not in seen:
                seen[n] = (target, owner)
                continue
            prev_target, prev_owner = seen[n]
            if prev_owner != owner:
                contested[n].update({prev_target, target})
            elif target == owner:
                wanted[prev_target].discard(n)
                seen[n] = (target, owner)
            else:
                wanted[target].discard(n)
    for n, owners in contested.items():
        for o in owners:
            wanted[o].discard(n)
    wanted = {k: v for k, v in wanted.items() if v}

    return raw, lines, plains, braced_line, wanted, contested


def apply_plan(path, raw, plains, braced_line, wanted):
    eol = "\r\n" if "\r\n" in "\n".join(raw[:80]) else "\n"
    plain_at = {target: (i, ind) for i, ind, target, _w in plains}
    inserts = collections.defaultdict(list)
    edits = {}
    for target, names in sorted(wanted.items()):
        ordered = sorted(names)
        if target in braced_line:
            i = braced_line[target]
            head = raw[i]
            indent = re.match(r"^([ \t]*)", head).group(1)
            j = i
            buf = ""
            while j < len(raw):
                buf += raw[j].rstrip("\r") + ("" if "}" in raw[j] else " ")
                if "}" in raw[j]:
                    break
                j += 1
            module = BRACE_HEAD_RE.match(buf).group(2)
            existing = IDENT_RE.findall(buf.split("{", 1)[1].split("}", 1)[0])
            merged = sorted(set(existing) | set(ordered))
            edits[(i, j)] = wrap(indent, module, merged, eol).rstrip("\r\n")
        else:
            i, indent = plain_at[target]
            module = [w for k, ind, t, w in plains if t == target][0]
            inserts[i].append(wrap(indent, module, ordered, eol).rstrip("\r\n"))

    out = []
    skip_to = -1
    for i, line in enumerate(raw):
        if i <= skip_to:
            continue
        replaced = False
        for (a, b), text in edits.items():
            if i == a:
                out.append(text)
                skip_to = b
                replaced = True
                break
        if not replaced:
            out.append(line.rstrip("\r"))
        for extra in inserts.get(i, ()):
            out.append(extra)
    text = eol.join(out)
    with io.open(os.path.join(ROOT, path), "w", encoding="utf-8", newline="") as fh:
        fh.write(text)


def main(argv):
    apply_mode = "--apply" in argv
    pathspecs = [a for a in argv if not a.startswith("--")]

    world = World(tracked_files([]))
    prelude = set(world.offers("std::prelude"))

    targets = tracked_files(pathspecs) if pathspecs else tracked_files([])
    edited = 0
    considered = 0
    all_contested = collections.defaultdict(set)
    for path in targets:
        ns = world.per_file[path][0]
        if ns is None:
            continue
        considered += 1
        # The prelude is injected only into modules that are not the stdlib's
        # own and did not opt out; for the rest its names are not in scope.
        src = read(path)
        in_scope = set() if (ns.startswith("std::") or "![no_std]" in src) else prelude
        raw, _lines, plains, braced_line, wanted, contested = file_plan(
            world, path, in_scope)
        for n, owners in contested.items():
            all_contested[(path, n)] = owners
        if not wanted:
            continue
        edited += 1
        if apply_mode:
            apply_plan(path, raw, plains, braced_line, wanted)
        else:
            for target, names in sorted(wanted.items()):
                print("%s\t%s\t%s" % (path, target, " ".join(sorted(names))))

    print("files considered: %d" % considered, file=sys.stderr)
    print("files %s: %d" % ("edited" if apply_mode else "needing edits", edited),
          file=sys.stderr)
    if all_contested:
        print("REFUSED -- a name offered by two plainly-imported modules:",
              file=sys.stderr)
        for (path, n), owners in sorted(all_contested.items()):
            print("  %s\t%s\t%s" % (path, n, " ".join(sorted(owners))), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
