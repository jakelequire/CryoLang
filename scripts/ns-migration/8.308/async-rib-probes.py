#!/usr/bin/env python3
"""Probes for the async lowering's by-name ribs (section 8.308).

`scripts/lane-gate.py` excludes `AsyncLower` (`frame_locals`), `BindingCapture`
and `BindingRename` as "the lowering's OWN RIB" - tables keyed by a local's
spelling, sound only because `disambiguate_locals` first renames every local
in the body to a unique minted spelling.  Each probe below is an async
function in which two bindings share a spelling, or a local the rename may
not reach, with a value or a refusal that tells the two apart.  A `_c` probe
is its control: the same program written with the shape the rename is known
to handle.

Usage: python scripts/ns-migration/8.308/async-rib-probes.py <cryo compiler>

Builds each probe as its own project under `.objcmp/async-rib-probes/`
(gitignored) and prints, per probe, `ok` or `WRONG` against what the program
MEANS, and what was observed.  Not a gate: at section 8.308 three probes are
WRONG, on the pinned compiler as on HEAD.
"""
import os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

HEAD = """namespace RibProbe;
import std::fmt;
import std::future;
import std::future::{ PendingThenReady };

type struct Pair {
    a: i64;
    b: i64;
}

"""
W = "await PendingThenReady::<i64>::new(2, 1)"

def main_of(call, want):
    return 'function main() -> int { fmt::printf("%%lld\\n", %s); return 0; }\n' % call

# name -> (source body, expectation).  An expectation is ("run", "<stdout>")
# or ("refuse", "E<code>").
PROBES = {
    # frame_locals: the address of a frame local held across a suspend is
    # E0455.  A destructured local is never minted, so it is not in the table.
    "a1_address_of_destructured_local": (f"""async function f() -> i64 {{
    const {{a, b}}: Pair = Pair {{ a: 1, b: 2 }};
    const p: i64* = &a;
    const _z: i64 = {W};
    return *p;
}}
""" + main_of("future::block_on(f())", None), ("refuse", "E0455")),
    "a1_c_address_of_plain_local": (f"""async function f() -> i64 {{
    const a: i64 = 1;
    const p: i64* = &a;
    const _z: i64 = {W};
    return *p;
}}
""" + main_of("future::block_on(f())", None), ("refuse", "E0455")),
    # a destructured local spelled like a parameter, read after a suspend.
    "a2_destructured_local_shadowing_parameter": (f"""async function g(a: i64) -> i64 {{
    mut r: i64 = 0;
    {{
        const {{a, b}}: Pair = Pair {{ a: 100, b: 2 }};
        const _z: i64 = {W};
        r = a;
    }}
    return r;
}}
""" + main_of("future::block_on(g(7))", None), ("run", "100")),
    "a2_c_plain_local_shadowing_parameter": (f"""async function g(a: i64) -> i64 {{
    mut r: i64 = 0;
    {{
        const a: i64 = 100;
        const _z: i64 = {W};
        r = a;
    }}
    return r;
}}
""" + main_of("future::block_on(g(7))", None), ("run", "100")),
    # a renamed local read inside a destructure's initializer, spelled like a
    # parameter.  No suspend separates them: the initializer is never renamed.
    "a3_destructure_initializer_reads_shadowing_local": (f"""async function h(x: i64) -> i64 {{
    const _z: i64 = {W};
    mut r: i64 = 0;
    {{
        const x: i64 = 50;
        const {{a, b}}: Pair = Pair {{ a: x, b: 0 }};
        r = a + b;
    }}
    return r;
}}
""" + main_of("future::block_on(h(7))", None), ("run", "50")),
    "a3_c_struct_initializer_reads_shadowing_local": (f"""async function h(x: i64) -> i64 {{
    const _z: i64 = {W};
    mut r: i64 = 0;
    {{
        const x: i64 = 50;
        const p: Pair = Pair {{ a: x, b: 0 }};
        r = p.a + p.b;
    }}
    return r;
}}
""" + main_of("future::block_on(h(7))", None), ("run", "50")),
    # BindingCapture / BindingRename: an arm binding and a nested arm's
    # binding of one spelling, each read across a suspend.  Leaving arm
    # bindings un-renamed makes this print 40.
    "a4_nested_arm_bindings_one_spelling": (f"""async function m(o: Option<i64>, q: Option<i64>) -> i64 {{
    mut r: i64 = 0;
    match (o) {{
        Option::Some(v) => {{
            const _z: i64 = {W};
            match (q) {{
                Option::Some(v) => {{
                    const _y: i64 = {W};
                    r = r + v;
                }}
                Option::None => {{ }}
            }}
            r = r + v;
        }}
        Option::None => {{ }}
    }}
    return r;
}}
""" + main_of("future::block_on(m(Option::Some(20), Option::Some(1)))", None), ("run", "21")),
    # a local shadowing an arm binding, initialized from it.  Binding the
    # local before renaming its initializer makes this E0201.
    "a5_local_initialized_from_the_binding_it_shadows": (f"""async function s(o: Option<i64>) -> i64 {{
    match (o) {{
        Option::Some(v) => {{
            const v: i64 = v + 1;
            const _z: i64 = {W};
            return v;
        }}
        Option::None => {{ return 0; }}
    }}
}}
""" + main_of("future::block_on(s(Option::Some(5)))", None), ("run", "6")),
    # an or-pattern's shared binding.  Giving each alternative its own fresh
    # name makes this E0201.
    "a6_or_pattern_shared_binding": (f"""type enum E {{ A(i64); B(i64); C; }}
async function t(e: E) -> i64 {{
    match (e) {{
        E::A(v) | E::B(v) => {{
            const _z: i64 = {W};
            return v;
        }}
        E::C => {{ return 0; }}
    }}
}}
""" + main_of("future::block_on(t(E::B(9)))", None), ("run", "9")),
}

CONFIG = """[project]
project_name = "ribprobe"
output_dir = "build"
target_type = "executable"
source_dir = "src"
entry_point = "src/main.cryo"

[compiler]

[dependencies]
"""


def run_probe(cryo, name, body):
    d = os.path.join(ROOT, ".objcmp", "async-rib-probes", name)
    os.makedirs(os.path.join(d, "src"), exist_ok=True)
    open(os.path.join(d, "cryoconfig"), "w").write(CONFIG)
    open(os.path.join(d, "src", "main.cryo"), "w").write(HEAD + body)
    env = dict(os.environ, CRYO_STDLIB=os.path.join(ROOT, "stdlib"), CRYO_CC="gcc")
    exe = os.path.join(d, "build", "ribprobe.exe" if os.name == "nt" else "ribprobe")
    if os.path.exists(exe):
        os.remove(exe)
    b = subprocess.run([cryo, "build", "."], cwd=d, env=env, capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    codes = sorted(set(re.findall(r"error\[(E\d+)\]", b.stdout + b.stderr)))
    if not os.path.exists(exe):
        return ("refuse", ",".join(codes) or "(no code, exit %d)" % b.returncode)
    r = subprocess.run([exe], cwd=d, capture_output=True, text=True)
    return ("run", r.stdout.strip())


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    cryo = os.path.abspath(sys.argv[1])
    wrong = 0
    for name, (body, want) in PROBES.items():
        got = run_probe(cryo, name, body)
        ok = got[0] == want[0] and (got[1] == want[1] if want[0] == "run" else want[1] in got[1].split(","))
        if not ok:
            wrong += 1
        print("%-5s %-52s want %s %s, got %s %s" % ("ok" if ok else "WRONG", name, want[0], want[1], got[0], got[1]))
    print("async-rib-probes: %d of %d WRONG" % (wrong, len(PROBES)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
