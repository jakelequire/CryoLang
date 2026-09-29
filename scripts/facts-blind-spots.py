#!/usr/bin/env python3
"""Where the compiler's `--emit=facts` report cannot see a spelling, as a list
that fails when it stops being true.

Every gate that reads `.facts/` counts only what the facts writer
(`compiler/src/compiler/sema/call_facts.cryo`) records.  A shape of code it
does not record is a shape those gates are blind to, and nothing reports a
blind spot: a count over it reads zero.  This is the list of those shapes,
each with a probe line in `tests/fixtures/facts-blind-spots/src/main.cryo`
marked `// spot: <id>`, and the list is checked against the compiler under
test: the fixture is built with `--emit=facts`, and each entry's line must
have (SEEN) or lack (BLIND) the records the entry names.  An entry that no
longer holds fails in either direction - a BLIND line the writer now sees
means the list is stale and the entry moves, saying what closed it; a SEEN
line it no longer sees is a regression.  The SEEN entries are the controls:
each shows the instrument reporting a shape, so a BLIND line's zero is not
the zero of an instrument that reports nothing.

This file IS the list, and the reason for each blind entry is written
beside it here, not in any design document: it is what a gate over the facts
- that a spelling reaches a lookup only through a ruled door, that text an
identity hands back never becomes a lookup key - has to be read against.

What an entry asks of its line:

    arg, sarg, cmp, match
              a record of that kind there - what a SEEN entry names, so it
              is seen for the reason it states and not by another record on
              the same line
    spelling  any of those four - what a BLIND entry names: nothing there
              describes a spelling
    callee    the line's `call` record names the declaration it reached
              (its pin is not `none`)
    origin    the key record of the line's lookup (an `arg` or `sarg` of
              a `.get`) traces the key to `text_of` - the fixture's stand-in
              for text an identity hands back (`InternTable.resolve`,
              `DefTable.path_of`) - in its provenance.  What a check that
              identity text never becomes a lookup key has to read from each
              record, and where that trace stops
    assigned  an `assign` record on the line - an assignment to a local -
              traces the assigned value to `text_of`: what a reader
              following the local past its initializer reads

Usage:
    python scripts/facts-blind-spots.py --cryo compiler/build/cryo.exe
    python scripts/facts-blind-spots.py --list      the list, no build

Exit codes: 0 every entry holds; 1 otherwise, each failing entry named.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIXTURE = os.path.join(ROOT, "tests", "fixtures", "facts-blind-spots")
PROBES = "src/main.cryo"
SCRATCH = os.path.join(ROOT, ".verify", "facts-blind-spots")
STDLIB = os.path.join(ROOT, "stdlib").replace("\\", "/")
MARK = re.compile(r"//\s*spot:\s*(.+?)\s*$")
SPELLING_KINDS = ("arg", "sarg", "cmp", "match")

SEEN, BLIND = "SEEN", "BLIND"

# (id, question, status, the shape).  Keep BLIND entries' reasons: they are
# what a reader of a zero needs.
SPOTS = [
    ("string-to-string", "arg", SEEN,
     "a `string` passed to a parameter typed `string` (`arg`)"),
    ("name-to-generic", "arg", SEEN,
     "a key passed to a generic parameter given it by a written type argument (`arg`)"),
    ("equals-method", "arg", SEEN,
     "a key's `equals` (`arg` of its parameter)"),
    ("string-compare", "cmp", SEEN,
     "`==` between a string and a literal (`cmp`)"),
    ("number-compare", "cmp", SEEN,
     "`==` between the numbers read out of two keys, `name.id == other.id` (`cmp`)"),
    ("string-through-pointer", "arg", SEEN,
     "a `string` passed through a function pointer (`arg`)"),
    ("string-in-closure", "arg", SEEN,
     "a `string` passed from inside a closure's body (`arg`)"),
    ("string-in-generic-body", "arg", SEEN,
     "a `string` passed from inside a generic function's body (`arg`)"),
    ("number-to-generic-insert", "sarg", SEEN,
     "the number behind a spelling stored as a map key, `seen.insert(name.id, 1)` (`sarg`)"),
    ("number-to-generic-get", "sarg", SEEN,
     "a lookup keyed by the number behind a spelling, `seen.get(&name.id)` (`sarg`)"),
    ("number-to-number", "sarg", SEEN,
     "the number behind a spelling passed to a parameter typed `u32` (`sarg`)"),
    ("text-to-text", "sarg", SEEN,
     "a `Text` passed to a parameter typed `Text` (`sarg`)"),
    ("keyword-to-keyword", "sarg", SEEN,
     "a `Keyword` passed to a parameter typed `Keyword` (`sarg`)"),
    ("match-string", "match", SEEN,
     "a `match` over a string with literal patterns (`match`)"),
    ("number-via-local", "spelling", BLIND,
     "the number behind a spelling stored in a plain integer local, then used as a key: "
     "`const k: u32 = name.id; seen.get(&k)`.  A value is a spelling by its TYPE, and `u32` "
     "is not one; the local's initializer is in the provenance of a record that is never written"),
    ("number-composed-key", "spelling", BLIND,
     "a key composed arithmetically from the numbers behind spellings, "
     "`wide.get(&(((name.id as u64) << 32) | (other.id as u64)))` - the shape of the "
     "compiler's family slot, `(owner_path.id << 32) | leaf.id`: the result is a `u64`, not "
     "a number read out of a spelling, so no record is written and the key's parts are "
     "traced nowhere"),
    ("number-into-field", "spelling", BLIND,
     "the number behind a spelling stored into a struct literal's field, "
     "`Holder { key: name.id }`: a field initializer is not an argument"),
    ("callee-generic", "callee", BLIND,
     "a call to a generic function records no callee identity (pin `none`, callee `?`): "
     "a reader places it by the callee's spelling alone"),
    ("callee-pointer", "callee", BLIND,
     "a call through a function pointer records no callee identity"),
    ("callee-closure", "callee", BLIND,
     "a call to a closure records no callee identity"),
    ("identity-text-direct", "origin", SEEN,
     "identity text used as a key directly, `by_text.get(&name.text_of())`: the key's "
     "provenance is the call"),
    ("identity-text-via-local", "origin", SEEN,
     "identity text held in a `string` local, then a key: the provenance follows the local's "
     "initializer"),
    ("identity-text-reassigned", "assigned", SEEN,
     "identity text assigned to a `mut` local after its initializer, then a key: "
     "`mut moved: string = \"Option\"; if (..) { moved = name.text_of(); } "
     "by_text.get(&moved)`.  The key's provenance names the initializer only "
     "(`local:mut moved=literal:\"Option\"`); the assignment has its own `assign` record, "
     "which a reader following the local joins by function and name"),
    ("identity-text-concatenated", "origin", BLIND,
     "identity text concatenated into a key, `get(&(\"std::\" + name.text_of()))`: the "
     "key's provenance is `expr:BinaryExpression` and stops there"),
    ("identity-text-formatted", "origin", BLIND,
     "identity text formatted into a key, `get(&fmt::format(\"%s::%s\", \"std\", "
     "name.text_of()))`: the key's provenance names the format call and its FIRST argument "
     "only; the identity text is recorded as the format call's own argument, a separate "
     "record nothing links to the key"),
]


def markers():
    """{spot id: line} from the fixture; a line may carry several ids."""
    found = {}
    with open(os.path.join(FIXTURE, PROBES), encoding="utf-8") as fh:
        for n, line in enumerate(fh, 1):
            m = MARK.search(line)
            if m:
                for sid in (s.strip() for s in m.group(1).split(",")):
                    if sid in found:
                        raise SystemExit("facts-blind-spots: FAIL -- spot `%s` marked twice" % sid)
                    found[sid] = n
    return found


def build(cryo):
    shutil.rmtree(SCRATCH, ignore_errors=True)
    os.makedirs(SCRATCH)
    p = subprocess.run([cryo, "build", "--emit=facts", "--build-dir=" + SCRATCH.replace("\\", "/"),
                        "--stdlib=" + STDLIB],
                       cwd=FIXTURE, env=dict(os.environ, CRYO_STDLIB=STDLIB),
                       capture_output=True, text=True, errors="replace")
    facts = os.path.join(SCRATCH, "facts_blind_spots.facts")
    if p.returncode != 0 or not os.path.isfile(facts):
        sys.stdout.write(p.stdout + p.stderr)
        raise SystemExit("facts-blind-spots: FAIL -- the fixture did not build with --emit=facts "
                         "(exit %d)" % p.returncode)
    return facts


def read(facts):
    """{line: [record fields]} for the probe file."""
    by_line = {}
    with open(facts, encoding="utf-8") as fh:
        for line in fh:
            f = line.rstrip("\n").split("\t")
            if len(f) != 15:
                raise SystemExit("facts-blind-spots: %s: a record with %d fields, not 15; the "
                                 "facts format has changed and this reader has not"
                                 % (facts, len(f)))
            if f[1] == PROBES:
                by_line.setdefault(int(f[2]), []).append(f)
    return by_line


def answer(question, records):
    if question == "origin":
        return any(r[0] in ("arg", "sarg") and r[12].split("(", 1)[0].endswith(".get")
                   and "text_of" in r[6] for r in records)
    if question == "assigned":
        return any(r[0] == "assign" and "text_of" in r[6] for r in records)
    if question == "spelling":
        return any(r[0] in SPELLING_KINDS for r in records)
    if question in SPELLING_KINDS:
        return any(r[0] == question for r in records)
    calls = [r for r in records if r[0] == "call"]
    return bool(calls) and all(r[9] != "none" for r in calls)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cryo")
    ap.add_argument("--list", action="store_true")
    args = ap.parse_args()
    if args.list:
        for sid, question, status, what in SPOTS:
            print("%-6s %-8s %-26s %s" % (status, question, sid, what))
        return 0
    if not args.cryo:
        ap.error("--cryo is required unless --list")

    marks = markers()
    listed = {s[0] for s in SPOTS}
    problems = []
    for sid in sorted(set(marks) - listed):
        problems.append("`%s` is marked in the fixture and not in the list" % sid)
    for sid in sorted(listed - set(marks)):
        problems.append("`%s` is in the list and marked nowhere in the fixture" % sid)

    by_line = read(build(os.path.abspath(args.cryo)))
    seen = blind = 0
    for sid, question, status, what in SPOTS:
        if sid not in marks:
            continue
        line = marks[sid]
        got = answer(question, by_line.get(line, []))
        if status == SEEN and not got:
            problems.append("`%s` (line %d) is listed SEEN and the facts no longer see it: %s"
                            % (sid, line, what))
        elif status == BLIND and got:
            problems.append("`%s` (line %d) is listed BLIND and the facts now see it - the list "
                            "is stale: move the entry to SEEN, saying what closed it" % (sid, line))
        elif status == SEEN:
            seen += 1
        else:
            blind += 1
    if problems:
        print("facts-blind-spots: FAIL -- %d entr%s no longer hold%s:"
              % (len(problems), "y" if len(problems) == 1 else "ies", "s" if len(problems) == 1 else ""))
        for p in problems:
            print("  " + p)
        return 1
    print("facts-blind-spots: OK -- %d entries hold: %d seen, %d blind" % (seen + blind, seen, blind))
    return 0


if __name__ == "__main__":
    sys.exit(main())
