"""The operator table names its trait as a `LangItem`, not as a leaf string.

`OperatorTraitMap::mk("Add", ...)` becomes `OperatorTraitMap::mk(LangItem::Add, ...)`;
the field and its readers are edited by hand in the same commit.  Refuses the
run unless every rewritten call is found.
Run from the repo root: `python scripts/ns-migration/8.317/operator_map.py`.
"""
import re
import sys

PATH = "compiler/src/compiler/sema/sema.cryo"
CALL = re.compile(r'OperatorTraitMap::mk\("([A-Za-z]+)",')
EXPECTED = 15   # + - * / % == != & | ^ << >> (12), unary - ! ~ (3)


def main() -> int:
    with open(PATH, encoding="utf-8", newline="") as fh:
        src = fh.read()
    n = len(CALL.findall(src))
    if n != EXPECTED:
        print(f"REFUSED: {n} `mk(\"..\",` calls, expected {EXPECTED}")
        return 1
    src = CALL.sub(r"OperatorTraitMap::mk(LangItem::\1,", src)
    with open(PATH, "w", encoding="utf-8", newline="") as fh:
        fh.write(src)
    return 0


if __name__ == "__main__":
    sys.exit(main())
