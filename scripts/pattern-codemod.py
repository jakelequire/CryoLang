"""One-off codemod: the pattern header class `PatternNode` is named `Pattern`.

Only the spelling of the header moves; `EnumPatternNode`, whose header role
is gone and whose fields are now the `EnumPattern` payload, is left for the
type check to point at, one decision per use.

Usage: python scripts/pattern-codemod.py <root>...
"""

import os
import re
import sys

NAME = re.compile(r"\bPatternNode\b")


def main():
    changed = 0
    sites = 0
    for root in sys.argv[1:]:
        for dirpath, _, files in os.walk(root):
            for f in files:
                if not f.endswith(".cryo"):
                    continue
                path = os.path.join(dirpath, f)
                with open(path, "r", encoding="utf-8", newline="") as h:
                    text = h.read()
                n = len(NAME.findall(text))
                if n == 0:
                    continue
                with open(path, "w", encoding="utf-8", newline="") as h:
                    h.write(NAME.sub("Pattern", text))
                changed += 1
                sites += n
    print("files changed %d, names rewritten %d" % (changed, sites))


if __name__ == "__main__":
    main()
