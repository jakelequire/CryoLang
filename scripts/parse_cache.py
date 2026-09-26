"""A parse shared by the checks of one `ns-status-check` run.

About thirty section-0 rows each run a script that parses the whole of
`compiler/src` the same way (`lane-gate.py --row KIND`, `residue.py
--check`, ...), and each parse takes ~10 s.  `memo` computes such a parse
once per run and hands every later caller the same answer.

It is inert unless `CRYO_PARSE_CACHE` names a directory, which
`ns-status-check.py` creates for its own run and deletes afterwards, so no
answer outlives the run that computed it.  The key is a hash of the BYTES of
every input - the scripts whose code decides the answer and every `.cryo`
file under the source tree - so a tree or a script edited mid-run misses
the cache and is parsed afresh rather than answered from the old parse.  A
computation that raises is not stored.
"""
import hashlib
import os
import pickle

ENV = "CRYO_PARSE_CACHE"


def _key(label, scripts, src):
    h = hashlib.sha256(label.encode("utf-8"))
    for path in scripts:
        with open(path, "rb") as fh:
            h.update(b"\0script\0" + os.path.basename(path).encode("utf-8") + b"\0" + fh.read())
    h.update(b"\0src\0" + os.path.abspath(src).encode("utf-8"))
    for dirpath, dirs, names in os.walk(src):
        dirs.sort()
        for fname in sorted(names):
            if not fname.endswith(".cryo"):
                continue
            full = os.path.join(dirpath, fname)
            rel = os.path.relpath(full, src).replace(os.sep, "/")
            with open(full, "rb") as fh:
                h.update(b"\0file\0" + rel.encode("utf-8") + b"\0" + fh.read())
    return h.hexdigest()


def memo(label, scripts, src, compute):
    """`compute()`, or the answer an earlier call with byte-identical
    `scripts` and `src` computed in this run."""
    cache = os.environ.get(ENV)
    if not cache or not os.path.isdir(cache):
        return compute()
    path = os.path.join(cache, "%s-%s.pickle" % (label, _key(label, scripts, src)))
    if os.path.exists(path):
        with open(path, "rb") as fh:
            return pickle.load(fh)
    value = compute()
    tmp = "%s.%d.tmp" % (path, os.getpid())
    with open(tmp, "wb") as fh:
        pickle.dump(value, fh)
    os.replace(tmp, path)
    return value


def selftest():
    """Every way the cache could answer from a stale parse, driven through a
    throwaway tree; the last line is the number of cases that behaved."""
    import shutil
    import tempfile
    work = tempfile.mkdtemp(prefix="parse-cache-selftest-")
    saved = os.environ.get(ENV)
    try:
        src = os.path.join(work, "src")
        os.makedirs(os.path.join(src, "sub"))
        script = os.path.join(work, "gate.py")
        cache = os.path.join(work, "cache")
        os.makedirs(cache)

        def write(path, text):
            with open(path, "w", encoding="utf-8", newline="\n") as fh:
                fh.write(text)

        write(os.path.join(src, "a.cryo"), "function a() -> i32 { return 1; }\n")
        write(os.path.join(src, "sub", "b.cryo"), "function b() -> i32 { return 2; }\n")
        write(script, "# v1\n")
        calls = []

        def ask():
            return memo("t", [script], src, lambda: calls.append(1) or len(calls))

        cases = []
        os.environ.pop(ENV, None)
        cases.append(("inert without the variable", ask() == 1 and ask() == 2))
        os.environ[ENV] = cache
        first = ask()
        cases.append(("an identical tree is answered from the cache", ask() == first))
        write(os.path.join(src, "sub", "b.cryo"), "function b() -> i32 { return 3; }\n")
        cases.append(("a changed source file misses", ask() == first + 1))
        write(os.path.join(src, "c.cryo"), "function c() -> i32 { return 4; }\n")
        cases.append(("an added source file misses", ask() == first + 2))
        os.remove(os.path.join(src, "c.cryo"))
        cases.append(("a removed source file misses", ask() == first + 1))
        write(script, "# v2\n")
        cases.append(("a changed script misses", ask() == first + 3))
        write(os.path.join(src, "notes.txt"), "not a source file\n")
        cases.append(("a non-.cryo file does not count", ask() == first + 3))

        def boom():
            raise ValueError("refused")
        try:
            memo("t2", [script], src, boom)
        except ValueError:
            pass
        cases.append(("a computation that raises is not stored",
                      memo("t2", [script], src, lambda: "ran") == "ran"))
        right = 0
        for name, ok in cases:
            print("  %-4s %s" % ("ok" if ok else "FAIL", name))
            right += ok
        print("parse_cache selftest: %d of %d cases right" % (right, len(cases)))
        print(right)
        return 0 if right == len(cases) else 1
    finally:
        if saved is None:
            os.environ.pop(ENV, None)
        else:
            os.environ[ENV] = saved
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    import sys
    if "--selftest" in sys.argv:
        sys.exit(selftest())
    sys.stderr.write(__doc__)
    sys.exit(2)
