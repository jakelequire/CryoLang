"""Delete every line of the identity-index shadow: each ends in `// PROBE`.

The shadow carried the old name-built identity beside the new index and
returned the old answer everywhere, printing a SHADOW line wherever the two
disagreed.  `index-shadow.patch` beside this script re-applies it to the
committed tree.  Run from the repo root.
"""
import subprocess

files = subprocess.run(["git", "grep", "-l", "// PROBE", "--", "compiler/src"],
                       capture_output=True, text=True).stdout.split()
total = 0
for f in files:
    lines = open(f, encoding="utf-8", newline="").read().split("\n")
    kept = [l for l in lines if not l.rstrip().rstrip("\r").endswith("// PROBE")]
    total += len(lines) - len(kept)
    open(f, "w", encoding="utf-8", newline="").write("\n".join(kept))
print("removed %d probe lines from %d files" % (total, len(files)))
