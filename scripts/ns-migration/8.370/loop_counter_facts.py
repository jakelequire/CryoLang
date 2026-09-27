#!/usr/bin/env python3
"""Show where `--emit=facts` says an array element is read: by which loop.

    python scripts/ns-migration/8.370/loop_counter_facts.py --cryo <abs path to cryo>

A comparison's record carries its loop depth, and an element indexed by a
local carries `element@<d>`, the depth that local counts: a scan is the
element the INNERMOST loop reads.  A `for` header's counter counts the loop
it heads.  A counter declared before a `while` (or a `for` with no header
declaration) counts the loop that compares it in its condition AND steps it
(`i++`, `i += 1`, `i = ..`) in a statement directly in its body or its
update; a local the condition only compares is a bound and keeps its depth.

One scratch program (in `--work`, default `.objcmp/loop-counter-facts`),
built once; each function below is one shape, and the record of its one
string comparison must carry the provenance given.  Exit 1 on any mismatch
or a missing record.  `nested_step` is the documented limit: its counter is
stepped only inside an `if`, so it keeps its declaration's depth.
"""
import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))

CONFIG = """[project]
project_name = "loop_counters"
output_dir   = "build"
target_type  = "executable"
entry_point  = "src/main.cryo"
"""

MAIN = """namespace loop_counters;

function while_counter(names: string[], key: string) -> i64 {
    mut i: i64 = 0;
    while (i < names.length) {
        if (names[i] == key) { return i; }
        i++;
    }
    return -1;
}

function header_counter(names: string[], key: string) -> i64 {
    for (mut i: i64 = 0; i < names.length; i++) {
        if (names[i] == key) { return i; }
    }
    return -1;
}

function bound_not_counter(names: string[]) -> i64 {
    for (mut i: i64 = 0; i < names.length; i++) {
        const a: string = names[i];
        for (mut j: i64 = 0; j < i; j++) {
            if (names[j] == a) { return j; }
        }
    }
    return -1;
}

function headless_for(names: string[], key: string) -> i64 {
    mut i: i64 = 0;
    for (; i < names.length; i++) {
        if (names[i] == key) { return i; }
    }
    return -1;
}

function nested_step(names: string[], key: string) -> i64 {
    mut i: i64 = 0;
    while (i < names.length) {
        if (names[i] == key) { return i; }
        if (i >= 0) { i++; }
    }
    return -1;
}

function main() -> i32 {
    mut names: string[] = [];
    names.push("a");
    const n: i64 = while_counter(names, "a") + header_counter(names, "a") + bound_not_counter(names)
        + headless_for(names, "a") + nested_step(names, "b");
    return n as i32;
}
"""

# function -> (loop depth of the record, the provenances the record's two
# operands must be, in either order)
EXPECT = {
    "while_counter": ("1", {"element@1:param:names", "param:key"}),
    "header_counter": ("1", {"element@1:param:names", "param:key"}),
    "bound_not_counter": ("2", {"element@2:param:names", "local:a@1=element@1:param:names"}),
    "headless_for": ("1", {"element@1:param:names", "param:key"}),
    "nested_step": ("1", {"element@0:param:names", "param:key"}),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cryo", required=True)
    ap.add_argument("--work", default=os.path.join(ROOT, ".objcmp", "loop-counter-facts"))
    args = ap.parse_args()
    work = args.work
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(os.path.join(work, "src"))
    with open(os.path.join(work, "cryoconfig"), "w", encoding="utf-8") as fh:
        fh.write(CONFIG)
    with open(os.path.join(work, "src", "main.cryo"), "w", encoding="utf-8") as fh:
        fh.write(MAIN)
    env = dict(os.environ, CRYO_STDLIB=os.path.join(ROOT, "stdlib").replace("\\", "/"))
    out = os.path.join(work, "out")
    p = subprocess.run([args.cryo, "build", "--emit=facts", "--build-dir=" + out.replace("\\", "/")],
                       cwd=work, env=env, capture_output=True, text=True, errors="replace")
    facts = os.path.join(out, "loop_counters.facts")
    if not os.path.exists(facts):
        print(p.stdout + p.stderr)
        print("loop_counter_facts: no facts written at %s" % facts)
        return 1
    got = {}
    for rec in open(facts, encoding="utf-8"):
        f = rec.rstrip("\n").split("\t")
        if f[0] != "cmp" or not f[1].endswith("main.cryo"):
            continue
        fn = f[13].split("(")[0].rsplit("::", 1)[-1]
        got.setdefault(fn, []).append((f[14], {f[6], f[7]}))
    bad = 0
    for fn, (depth, ops) in sorted(EXPECT.items()):
        recs = got.get(fn, [])
        ok = recs == [(depth, ops)]
        bad += 0 if ok else 1
        print("%s %-18s want depth %s %s; got %s" % ("ok  " if ok else "FAIL", fn, depth, sorted(ops),
                                                    [(d, sorted(o)) for d, o in recs]))
    print("loop_counter_facts: %d of %d shapes as expected" % (len(EXPECT) - bad, len(EXPECT)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
