#!/usr/bin/env python3
"""One-off proof that `[T]` becoming `Array<T>` renamed symbols and changed
no code.

Builds the test suite (unit tests and projects), every example and the
compiler itself once with OLD and once with NEW, from the same tree at the
same paths.  Each object NEW builds is compared with the object OLD built at
the same path: equal bytes are equal; otherwise both are read as COFF and
reduced to their functions and named data, each keyed by its demangled name:
the bytes with every relocated field blanked, and the relocations, each
naming its target by demangled name or, for a target in an unnamed constant
section, by the bytes it points at.  In OLD's names every dynamic array `[X]`
is spelled as the type it now is,
`std::collections::array::Array<X, std::alloc::allocator::GlobalAlloc>`.
The objects of one build (the test suite, a project, an example, the
compiler) are compared as one set, since a generic instance may land in
another object of the same build, and order is not compared: an object lays
out its functions and constants in the order they were emitted.  Every
entity OLD's build has must be in NEW's, unchanged; entities only NEW's
build has are listed apart.

  --compare OLD.o NEW.o   compare one pair the same way (the control)
  --no-map                skip the `[X]` respelling
  --reuse                 compare the kept OLD objects with what the build
                          dirs hold now, building nothing

Deleted in the commit after the one it proves.
"""

import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ARRAY = "std::collections::array::Array<%s, std::alloc::allocator::GlobalAlloc>"
# IMAGE_REL_AMD64_*: the width of the field each relocation type patches.
FIELD = {1: 8, 2: 4, 3: 4, 4: 4, 5: 4, 6: 4, 7: 4, 8: 4, 9: 4, 10: 2, 11: 4}
EXEC = 0x20000000


def run(cmd, cwd, env):
    p = subprocess.run(cmd, cwd=cwd, env=env, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, text=True, errors="replace")
    return p.returncode, p.stdout


def build_dirs():
    dirs = [os.path.join(ROOT, "tests", "build")]
    pdir = os.path.join(ROOT, "tests", "tests", "projects")
    for name in sorted(os.listdir(pdir)):
        dirs.append(os.path.join(pdir, name, "build"))
    edir = os.path.join(ROOT, "examples")
    for name in sorted(os.listdir(edir)):
        dirs.append(os.path.join(edir, name, "build"))
    dirs.append(os.path.join(ROOT, ".scratch", "rename-proof-compiler"))
    return dirs


def build_all(cryo, env):
    for d in build_dirs():
        shutil.rmtree(d, ignore_errors=True)
    code, out = run([cryo, "test"], os.path.join(ROOT, "tests"), env)
    summary = [l for l in out.splitlines() if "OVERALL" in l]
    print("  tests: exit %d %s" % (code, summary[-1].strip() if summary else "(no summary)"))
    edir = os.path.join(ROOT, "examples")
    for name in sorted(os.listdir(edir)):
        if os.path.exists(os.path.join(edir, name, "cryoconfig")):
            code, _ = run([cryo, "build"], os.path.join(edir, name), env)
            if code != 0:
                print("  example %s: exit %d" % (name, code))
    code, _ = run([cryo, "build", "--build-dir=" + build_dirs()[-1]],
                  os.path.join(ROOT, "compiler"), env)
    print("  compiler: exit %d" % code)


def objects():
    found = {}
    for d in build_dirs():
        for base, _, files in os.walk(d):
            for f in files:
                if f.endswith(".o"):
                    path = os.path.join(base, f)
                    found[os.path.relpath(path, ROOT).replace("\\", "/")] = path
    return found


def sha(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


class Demangler:
    def __init__(self, cryo, env):
        self.cryo, self.env, self.cache = cryo, env, {}

    def fill(self, symbols):
        todo = sorted(s for s in symbols if s.startswith("C$") and s not in self.cache)
        for i in range(0, len(todo), 2000):
            batch = todo[i:i + 2000]
            p = subprocess.run([self.cryo, "demangle"], input="\n".join(batch) + "\n",
                               env=self.env, stdout=subprocess.PIPE, text=True)
            lines = [l for l in p.stdout.splitlines() if l.strip()]
            if len(lines) != len(batch):
                sys.exit("demangle answered %d lines for %d symbols" % (len(lines), len(batch)))
            for s, l in zip(batch, lines):
                self.cache[s] = l.strip()

    def name(self, s, respell):
        d = self.cache.get(s, s)
        return respell_arrays(d) if respell else d


def respell_arrays(text):
    """`[X]` (no `;` at its own level) becomes `Array<X, GlobalAlloc>`."""
    out, i = [], 0
    while i < len(text):
        if text[i] != "[":
            out.append(text[i])
            i += 1
            continue
        depth, j, fixed = 0, i, False
        while j < len(text):
            c = text[j]
            if c == ">" and text[j - 1] == "-":
                pass                        # a function type's `->`
            elif c in "[<(":
                depth += 1
            elif c in "]>)":
                depth -= 1
                if depth == 0:
                    break
            elif c == ";" and depth == 1:
                fixed = True
            j += 1
        if j >= len(text):
            out.append(text[i:])
            break
        inner = respell_arrays(text[i + 1:j])
        out.append(("[%s]" % inner) if fixed else (ARRAY % inner))
        i = j + 1
    return "".join(out)


class Coff:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.data = f.read()
        d = self.data
        nsec, _, symptr, nsym, opt = struct.unpack_from("<HIIIH", d, 2)
        strtab = symptr + nsym * 18
        self.strtab = d[strtab:]
        self.sections = []
        for k in range(nsec):
            off = 20 + opt + k * 40
            raw = d[off:off + 8]
            if raw.startswith(b"/"):
                name = self.string(int(raw[1:].rstrip(b"\0")))
            else:
                name = raw.rstrip(b"\0").decode()
            size, ptr, relptr = struct.unpack_from("<III", d, off + 16)
            nrel, = struct.unpack_from("<H", d, off + 32)
            chars, = struct.unpack_from("<I", d, off + 36)
            body = d[ptr:ptr + size] if ptr else b"\0" * size
            relocs = [struct.unpack_from("<IIH", d, relptr + r * 10) for r in range(nrel)]
            self.sections.append({"name": name, "body": body, "relocs": relocs,
                                  "exec": bool(chars & EXEC)})
        self.symbols = {}
        k = 0
        while k < nsym:
            off = symptr + k * 18
            raw = d[off:off + 8]
            if raw[:4] == b"\0\0\0\0":
                name = self.string(struct.unpack_from("<I", raw, 4)[0])
            else:
                name = raw.rstrip(b"\0").decode()
            value, secnum, typ, scl, naux = struct.unpack_from("<IhHBB", d, off + 8)
            self.symbols[k] = (name, value, secnum, typ, scl, naux)
            k += 1 + naux

    def string(self, off):
        end = self.strtab.index(b"\0", off)
        return self.strtab[off:end].decode()

    def is_section_symbol(self, sym):
        name, value, secnum, _, scl, naux = sym
        return scl == 3 and naux > 0 and secnum > 0 and value == 0

    def boundaries(self):
        """Per section, every offset something starts at: a symbol, or the
        target of a relocation against the section itself."""
        bounds = {k + 1: {len(s["body"])} for k, s in enumerate(self.sections)}
        for sym in self.symbols.values():
            if sym[2] > 0 and not self.is_section_symbol(sym):
                bounds[sym[2]].add(sym[1])
        for sec in self.sections:
            for (va, symidx, typ) in sec["relocs"]:
                target = self.symbols[symidx]
                if self.is_section_symbol(target):
                    width = FIELD.get(typ, 4)
                    stored = int.from_bytes(sec["body"][va:va + width], "little", signed=True)
                    bounds[target[2]].add(stored)
        return {k: sorted(v) for k, v in bounds.items()}

    def entities(self, bounds):
        """Every named symbol defined in a section, with its byte range.
        Unwind data is left out: it is derived from the code, and the
        tables in it are numbered in emission order."""
        for idx, sym in self.symbols.items():
            name, value, secnum, typ, scl, naux = sym
            if secnum <= 0 or self.is_section_symbol(sym) or name.startswith("$") or name == "@feat.00":
                continue
            sec = self.sections[secnum - 1]
            if sec["name"].startswith(".xdata") or sec["name"].startswith(".pdata"):
                continue
            end = next(b for b in bounds[secnum] if b > value) if value < len(sec["body"]) else value
            yield name, sec, value, end


def reduce(coff, demangler, respell):
    names = [s[0] for s in coff.symbols.values()]
    demangler.fill(names)
    out = {}
    bounds = coff.boundaries()
    for name, sec, start, end in coff.entities(bounds):
        body = bytearray(sec["body"][start:end])
        relocs = []
        for (va, symidx, typ) in sec["relocs"]:
            if va < start or va >= end:
                continue
            width = FIELD.get(typ, 4)
            at = va - start
            stored = int.from_bytes(sec["body"][va:va + width], "little", signed=True)
            body[at:at + width] = b"\0" * width
            target = coff.symbols[symidx]
            if coff.is_section_symbol(target):
                # Up to the next thing in the section or the first NUL: a
                # string constant's merged neighbours are not part of it.
                tsec = coff.sections[target[2] - 1]["body"]
                stop = next((b for b in bounds[target[2]] if b > stored), stored)
                nul = tsec.find(b"\0", stored, stop)
                what = "const:" + tsec[stored:(nul + 1 if nul >= 0 else stop)].hex()
            else:
                what = demangler.name(target[0], respell) + "+%d" % stored
            relocs.append((at, typ, what))
        key = (sec["name"].split("$")[0], demangler.name(name, respell))
        out.setdefault(key, []).append((bytes(body).hex(), tuple(sorted(relocs))))
    for v in out.values():
        v.sort()
    return out


REDUCED = {}


def reduced(path, demangler, respell):
    key = (sha(path), respell)
    if key not in REDUCED:
        REDUCED[key] = reduce(Coff(path), demangler, respell)
    return REDUCED[key]


def merged(paths, demangler, respell):
    """One entry per entity name: the distinct bodies it has across the
    objects.  An instance emitted into two objects of one build is one
    instance to the linker."""
    out = {}
    for p in paths:
        for k, v in reduced(p, demangler, respell).items():
            out.setdefault(k, set()).update(v)
    return {k: sorted(v) for k, v in out.items()}


def compare(demangler, old_paths, new_paths, respell, detail=False):
    """The entities of OLD's objects against NEW's, as one set: a generic
    instance may be emitted into another object of the same build.
    Returns (entities only NEW has, the report of every other difference)."""
    a = merged(old_paths, demangler, respell)
    b = merged(new_paths, demangler, False)
    added = sorted(k for k in b if k not in a)
    lines = []
    for key in sorted(set(a) | set(b)):
        if key in a and a.get(key) != b.get(key):
            what = "only OLD" if key not in b else "differs"
            lines.append("  %s %s %s" % (what, key[0], key[1][:240]))
            if detail and what == "differs":
                for (ob, orl), (nb, nrl) in zip(a[key], b[key]):
                    if ob != nb:
                        lines.append("    bytes differ")
                    for r in sorted(set(orl) ^ set(nrl))[:6]:
                        lines.append("    %s %s" % ("old" if r in orl else "new", str(r)[:200]))
    report = "\n".join(lines[:40]) + ("\n  ... %d lines" % len(lines) if len(lines) > 40 else "")
    return added, (report if lines else None)


def project_of(rel):
    if rel.startswith(".scratch/rename-proof-compiler/"):
        return "compiler"
    return rel.split("/build/")[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--old")
    ap.add_argument("--new", required=True)
    ap.add_argument("--compare", nargs=2)
    ap.add_argument("--no-map", action="store_true")
    ap.add_argument("--detail", action="store_true", help="show how each differing entity differs")
    ap.add_argument("--show-added", type=int, default=0, help="list this many added entities per build")
    ap.add_argument("--reuse", action="store_true",
                    help="compare OLD's kept objects with the objects in the build dirs, building nothing")
    args = ap.parse_args()
    env = dict(os.environ, CRYO_STDLIB=os.path.join(ROOT, "stdlib"))
    demangler = Demangler(os.path.abspath(args.new), env)
    respell = not args.no_map

    if args.compare:
        added, diff = compare(demangler, [args.compare[0]], [args.compare[1]], respell, args.detail)
        for k in added:
            print("  only NEW %s %s" % (k[0], k[1][:240]))
        print("identical after mapping" if diff is None and not added else "DIFFERENT\n" + (diff or ""))
        return 0 if diff is None and not added else 1

    keep = os.path.join(ROOT, ".scratch", "rename-proof-old")
    manifest = os.path.join(keep, "manifest.json")
    if args.reuse:
        with open(manifest) as f:
            old = {k: tuple(v) for k, v in json.load(f).items()}
    else:
        shutil.rmtree(keep, ignore_errors=True)
        os.makedirs(keep)
        print("building with OLD")
        build_all(os.path.abspath(args.old), env)
        old = {}
        for rel, path in objects().items():
            h = sha(path)
            stored = os.path.join(keep, h + ".o")
            if not os.path.exists(stored):
                shutil.copyfile(path, stored)
            old[rel] = (h, stored)
        with open(manifest, "w") as f:
            json.dump(old, f)
        print("building with NEW")
        build_all(os.path.abspath(args.new), env)
    new = {rel: (sha(path), path) for rel, path in objects().items()}

    only_old = sorted(set(old) - set(new))
    only_new = sorted(set(new) - set(old))
    same = moved = 0
    builds = {}
    for rel in sorted(set(old) & set(new)):
        if old[rel][0] == new[rel][0]:
            same += 1
            continue
        moved += 1
        olds, news = builds.setdefault(project_of(rel), ([], []))
        olds.append(old[rel][1])
        news.append(new[rel][1])
    print("objects: %d in both, %d byte-identical, %d differ, in %d builds; "
          "only OLD %d, only NEW %d" % (len(set(old) & set(new)), same, moved, len(builds),
                                        len(only_old), len(only_new)))
    for rel in only_old[:20] + only_new[:20]:
        print("  unpaired: " + rel)
    failures = grown = 0
    for name in sorted(builds):
        olds, news = builds[name]
        added, diff = compare(demangler, olds, news, respell, args.detail)
        if added:
            grown += 1
            print("ADDED IN %s: %d entities only NEW emits" % (name, len(added)))
            for k in added[:args.show_added]:
                print("  %s %s" % (k[0], k[1][:240]))
        if diff is not None:
            failures += 1
            if failures <= 10:
                print("DIFFERENT %s (%d objects)\n%s" % (name, len(olds), diff))
    print("builds whose every OLD entity NEW emits unchanged: %d of %d (%d of them emit more); "
          "different: %d" % (len(builds) - failures, len(builds), grown, failures))
    return 1 if failures or only_old or only_new else 0


if __name__ == "__main__":
    sys.exit(main())
