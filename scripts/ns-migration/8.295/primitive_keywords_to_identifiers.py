#!/usr/bin/env python3
"""D18's keyword half, the lexer: the 23 primitive-type keywords and the two
reserved words nothing reads (`tuple`, `optional`) stop being tokens.  Each
had a variant and four arms (`is_primitive_type`, `is_keyword`, the lexeme,
`from_keyword`); every line naming one is deleted, and `is_primitive_type`
goes with its last arm.  `void` stays: it is a return-position shape the
parser reads ahead of any type, not a type a value can have.

Run from the repository root.  Asserts every deletion it makes, so a tree
that has moved refuses rather than half-applies.
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
LEX = os.path.join(ROOT, "compiler", "src", "compiler", "lex", "_module.cryo")

KEYWORDS = ["Boolean", "Int", "I8", "I16", "I32", "I64", "I128", "Uint", "Uint8",
            "Uint16", "Uint32", "Uint64", "Uint128", "Isize", "Usize", "Float",
            "F32", "F64", "Double", "Char", "String", "Never", "VaList",
            "Tuple", "Optional"]


def main():
    text = io.open(LEX, encoding="utf-8").read()
    lines = text.split("\n")
    name_re = re.compile(r"\bKw(%s)\b" % "|".join(KEYWORDS))
    kept = []
    removed = 0
    for line in lines:
        if name_re.search(line):
            removed += 1
            continue
        kept.append(line)
    # 23 primitives with a variant and four arms each, and the two reserved
    # words with a variant and three (no `is_primitive_type` arm): 123 lines.
    if removed != 123:
        raise SystemExit("expected to delete 123 lines naming the 25 tokens, found %d" % removed)
    text = "\n".join(kept)
    # `is_primitive_type` has no arm left but the `_ => false`; the method
    # and its doc comment go.
    head = text.index("    /// Returns true if this token spells a primitive type.")
    tail = text.index("    /// Returns true if this token type is a keyword.")
    if head < 0 or tail < head:
        raise SystemExit("is_primitive_type's doc comment not where expected")
    text = text[:head] + text[tail:]
    io.open(LEX, "w", encoding="utf-8", newline="\n").write(text)
    print("lex/_module.cryo: 123 lines deleted (23 tokens x 5 + 2 x 4), is_primitive_type deleted")
    return 0


if __name__ == "__main__":
    sys.exit(main())
