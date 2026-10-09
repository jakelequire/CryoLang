"""Source spans of Cryo function declarations.

span(lines, decl_line) -> (first, last), 1-based inclusive, or None.
`first` takes in the doc comment (`///`) and directive (`![...]`) lines
directly above the declaration; `last` is the line of the body's closing
brace (or the `;` of a body-less declaration).  Braces inside string and
character literals and comments are skipped.
"""


def _scan_to_end(text, pos):
    """From `pos` (start of the declaration), return the index just past the
    body's closing brace, or past the terminating `;` when there is no body."""
    depth = 0
    paren = 0
    i = pos
    n = len(text)
    while i < n:
        ch = text[i]
        if ch == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            i = n if j < 0 else j
            continue
        if ch == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
            continue
        if ch == '"':
            i += 1
            while i < n and text[i] != '"':
                if text[i] == "\\":
                    i += 1
                i += 1
            i += 1
            continue
        if ch == "'":
            # a character literal: 'x', '\n', '\''
            if i + 2 < n and text[i + 1] == "\\":
                j = text.find("'", i + 3)
                i = j + 1 if j >= 0 else i + 1
                continue
            if i + 2 < n and text[i + 2] == "'":
                i += 3
                continue
            i += 1
            continue
        if ch == "(":
            paren += 1
        elif ch == ")":
            paren -= 1
        elif ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return i + 1
            if depth < 0:
                return None
        elif ch == ";" and depth == 0 and paren == 0:
            return i + 1
        i += 1
    return None


def span(lines, decl_line):
    text = "\n".join(lines)
    offsets = [0]
    for l in lines:
        offsets.append(offsets[-1] + len(l) + 1)
    start = offsets[decl_line - 1]
    end = _scan_to_end(text, start)
    if end is None:
        return None
    last = decl_line
    while last < len(lines) and offsets[last] < end:
        last += 1
    first = decl_line
    while first > 1:
        prev = lines[first - 2].strip()
        if prev.startswith("///") or prev.startswith("![") or prev.startswith("#["):
            first -= 1
        else:
            break
    return first, last
