#!/usr/bin/env python3
"""Show that `--emit=facts` reports every call and comparison shape a
pattern-matching gate was once blind to, one shape per build.

    python scripts/ns-migration/8.365/facts_inversion.py --cryo compiler/build/cryo.exe

Each line of `shapes.list` is `id|what the shape is|the function body that
writes it`.  For each, a scratch project in `--work` (default
`.objcmp/facts-inversion`) gets `src/shapes/<id>.cryo` holding the body, a
`src/main.cryo` importing ONLY that module, and the helper types in
`project/src/` (a store owning a map, a reader declared in another file's
`implement` block, an array-backed store, a record-array member table).  The
project depends on the compiler library for `SymbolStr`, `QualifiedName`,
`ModuleGraph` and `DeclarationIndex`, so the key types are the compiler's
own.  It is built with `cryo build --emit=facts`; the link then fails (the
scratch project links no libclang), after the facts are written.

The count per shape is the records whose file is that shape's module.  `s00`
is the control: the same project with an empty body, which must report 0 -
an instrument that reports zero is first shown able to report non-zero, and
the non-zero must come from the shape and not from the scaffolding.
Exit 1 when the control is not 0 or any shape reports 0.
"""
import argparse, os, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))

CONFIG = """[project]
project_name = "forge_facts"
output_dir   = "build"
target_type  = "executable"
entry_point  = "src/main.cryo"

[dependencies]
compiler = { path = "%s", alias = "Compiler" }
"""

HEADER = """namespace forge_facts::shapes::%s;

import forge_facts::store::{ Ctx, LeafTable, MyStore, Owner, Rec, Takers };
import forge_facts::store_ext;
import compiler::module_graph::{ ModuleGraph };
import compiler::decl_index::{ DeclarationIndex };
import compiler::resolver::intern_table::{ InternTable };
import compiler::resolver::symbol_str::{ SymbolStr };
import compiler::resolver::qualified_name::{ QualifiedName };

/// %s
function run(ctx: Ctx*, o: &Owner, n: SymbolStr, text: string, graph: ModuleGraph*,
             intern: InternTable*, idx: DeclarationIndex*) -> i32 {
    %s
}
"""

MAIN = """namespace forge_facts;

import forge_facts::shapes::%s;

function main() -> i32 {
    return 0;
}
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cryo", required=True)
    ap.add_argument("--work", default=os.path.join(ROOT, ".objcmp", "facts-inversion"))
    ap.add_argument("only", nargs="*", help="shape ids to run (default: all)")
    args = ap.parse_args()
    cryo = os.path.abspath(args.cryo)
    work = os.path.abspath(args.work)
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(os.path.join(work, "src", "shapes"))
    for fn in os.listdir(os.path.join(HERE, "project", "src")):
        shutil.copyfile(os.path.join(HERE, "project", "src", fn), os.path.join(work, "src", fn))
    with open(os.path.join(work, "cryoconfig"), "w", encoding="utf-8") as fh:
        fh.write(CONFIG % os.path.join(ROOT, "compiler").replace("\\", "/"))
    env = dict(os.environ, CRYO_STDLIB=os.path.join(ROOT, "stdlib").replace("\\", "/"))
    failed = 0
    for line in open(os.path.join(HERE, "shapes.list"), encoding="utf-8"):
        line = line.rstrip("\n")
        if not line:
            continue
        sid, desc, body = line.split("|", 2)
        if args.only and sid not in args.only:
            continue
        for fn in os.listdir(os.path.join(work, "src", "shapes")):
            os.remove(os.path.join(work, "src", "shapes", fn))
        with open(os.path.join(work, "src", "shapes", sid + ".cryo"), "w", encoding="utf-8") as fh:
            fh.write(HEADER % (sid, desc, body))
        with open(os.path.join(work, "src", "main.cryo"), "w", encoding="utf-8") as fh:
            fh.write(MAIN % sid)
        out = os.path.join(work, "out", sid)
        facts = os.path.join(out, "forge_facts.facts")
        p = subprocess.run([cryo, "build", "--emit=facts", "--build-dir=" + out.replace("\\", "/")],
                           cwd=work, env=env, capture_output=True, text=True, errors="replace")
        with open(os.path.join(work, sid + ".log"), "w", encoding="utf-8") as fh:
            fh.write(p.stdout + p.stderr)
        mine = []
        if os.path.exists(facts):
            for rec in open(facts, encoding="utf-8"):
                f = rec.rstrip("\n").split("\t")
                if len(f) > 1 and f[1].endswith("shapes/%s.cryo" % sid):
                    mine.append(f)
        ok = (len(mine) == 0) if sid == "s00" else (len(mine) > 0)
        if not os.path.exists(facts):
            ok = False
        failed += 0 if ok else 1
        print("%s  %-4s records=%d  %s%s" % (sid, "ok" if ok else "FAIL", len(mine), desc,
                                             "" if os.path.exists(facts) else "  (no facts file)"))
        for f in mine:
            print("      %s L%s:%s idx=%s type=%s prov=%s recv=%s callee=%s"
                  % (f[0], f[2], f[3], f[4], f[5], f[6], f[7], f[12] if len(f) > 12 else "-"))
    print("facts-inversion: %s" % ("OK" if failed == 0 else "FAIL (%d)" % failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
