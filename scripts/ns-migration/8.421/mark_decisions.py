"""Marks decision rows in docs/name-resolution.md's decisions table with
Jake's plain-text rulings of 2026-09-29: the old finish line and the four
conditions are SUPERSEDED by the five rules; the language-feature rows the
2026-09-28 freeze moved off the branch are DEFERRED to their own branches
after the merge.  The row's status cell gets the mark in front of what it
said; nothing else in the row changes.  Idempotent: a row already marked is
left alone.

usage: python scripts/ns-migration/8.421/mark_decisions.py [--check]
"""
import os, re, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
LEDGER = os.path.join(ROOT, 'docs', 'name-resolution.md')

SUPERSEDED = ('**SUPERSEDED by the five rules of the resolution-rules document** '
              '(Jake, plain text 2026-09-29) - it said: ')
DEFERRED = ('**DEFERRED to its own branch after the merge** (Jake, plain text '
            '2026-09-29: the 2026-09-28 freeze moved it off this branch; deferred, '
            'not dropped) - it said: ')
MARKS = {
    'D32': SUPERSEDED,   # the count finish line
    'D41': SUPERSEDED,   # condition two's approved list
    'D67': SUPERSEDED,   # condition four's audit rounds
    'D75': SUPERSEDED,   # condition three redefined
    'D18': DEFERRED,     # primitive keywords: the keyword half
    'D66': DEFERRED,     # primitive keywords
    'D36': DEFERRED,     # explicit receivers
    'D65': DEFERRED,     # explicit receivers, scheduled
    'D37': DEFERRED,     # reference mutability
    'D60': DEFERRED,     # the raw subcommand
    'D63': DEFERRED,     # the editor's no-project fallback
    'D49': DEFERRED,     # the deprecated attribute (its lint half is built)
    'D64': DEFERRED,     # the deprecated attribute's order
}
SPLIT = re.compile(r'(?<!\\)\|')

def main():
    check = '--check' in sys.argv
    text = open(LEDGER, encoding='utf-8', newline='').read()
    lines = text.split('\n')
    seen = set()
    for i, line in enumerate(lines):
        m = re.match(r'^\| (D\d+) \|', line)
        if not m or m.group(1) not in MARKS:
            continue
        d = m.group(1)
        if d in seen:
            sys.exit('%s: two rows' % d)
        seen.add(d)
        cells = SPLIT.split(line)
        # ['', ' D32 ', ' desc ', ' status ', ' check ', ' refs ', '']
        if len(cells) < 6:
            sys.exit('%s: %d cells' % (d, len(cells)))
        status = cells[3]
        if status.lstrip().startswith(MARKS[d][:12]):
            continue
        if check:
            sys.exit('%s: not marked' % d)
        cells[3] = ' ' + MARKS[d] + status.lstrip()
        lines[i] = '|'.join(cells)
    missing = set(MARKS) - seen
    if missing:
        sys.exit('rows not found: %s' % sorted(missing))
    if check:
        print('mark_decisions: OK -- %d rows marked' % len(MARKS))
        return
    open(LEDGER, 'w', encoding='utf-8', newline='').write('\n'.join(lines))
    print('mark_decisions: %d rows marked' % len(MARKS))

main()
