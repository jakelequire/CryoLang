"""Tally MUTREF probe lines into distinct source places, by population and
by what the place would need once reference mutability is checked at calls.

Input: corpus lines `<label>\tSHADOW\tMUTREF\t<cat>\t<class>\t<file:line:col>`.
A place is keyed by (cat, class, location); stdlib locations are absolute, a
project's are relative to it, so those are keyed with the corpus label.

Usage: build a compiler from `mutref-probe.patch` (applied to 0aa16644),
compile `compiler/` with it (`cryo build --build-dir=build-mut`, stderr's
SHADOW lines prefixed `compiler<TAB>`), run `scripts/objcmp/corpus2.sh <tag>`
with it, then `python3 mutref-tally.py <compiler-lines> <corpus-lines>`.
Writes `refused-written.txt` in the current directory.  Run it with the
probe still applied: the probe adds lines to `sema.cryo` and
`call_resolver.cryo`, so their places read the wrong source line otherwise."""
import collections, os, re, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..')).replace(chr(92), '/')
_lines = {}
_last = [None]


def source_line(label, loc):
    """The source line a place points at, or None."""
    m = re.match(r'^(.*):(\d+):(\d+)$', loc.replace('\\', '/'))
    if not m:
        return None, 0
    f, ln, col = m.group(1), int(m.group(2)), int(m.group(3))
    if re.match(r'^[A-Za-z]:/', f):
        p = f
    elif label == 'compiler':
        p = ROOT + '/compiler/' + f
    elif label.startswith('tests/unit') or label.startswith('tests/tests/negative'):
        p = ROOT + '/tests/' + f
    else:
        p = ROOT + '/' + label.split('(')[0].rstrip('/') + '/' + f
    p = p.replace('/./', '/')
    if p not in _lines:
        try:
            _lines[p] = open(p, encoding='utf-8', errors='replace').read().split('\n')
        except OSError:
            _lines[p] = None
    src = _lines[p]
    _last[0] = (src, ln)
    if src is None or ln < 1 or ln > len(src):
        return None, col
    return src[ln - 1], col


def lowered(label, loc, cat):
    """True when the place is an `async` declaration's header line: the call
    there was written by the async lowering, not by the programmer."""
    line, col = source_line(label, loc)
    if line is None:
        return None
    if re.match(r'^\s*((public|private|protected|static|override)\s*:?\s+)*async\b', line):
        return True
    # A written receiver: the text at the place is `recv.method(` - the
    # receiver's own spelling followed by the call.  Anything else (an
    # `await` operand, a comment, a declaration) is a call the async
    # lowering synthesized and gave the nearest source span.
    at = line[col - 1:] if col > 0 else line
    if cat != 'recv':
        return not at.startswith('&')
    return re.match(r'(\*?\(?\*?)?(this|[A-Za-z_]\w*)\)?((\.\w+)|(\[[^\]]*\])|(\(\)))*\.\w+\s*(::<[^>]*>)?\s*\(', at) is None


def enclosing_header(label, loc):
    """The header line of the method a place sits in, or None."""
    line, col = source_line(label, loc)
    src, ln = _last[0]
    if src is None:
        return None
    for i in range(ln - 1, -1, -1):
        h = src[i]
        if re.match(r'^\s+((public|private|protected|static|override|async)\s*:?\s+)*[a-z_]\w*\s*(<[^>]*>)?\s*\(', h)                 and not re.match(r'^\s+(if|while|for|match|return|else)', h) and '=' not in h.split('(')[0]                 and ';' not in h:
            return h
    return None


def this_root(cls, label, loc):
    """For a receiver rooted at `this` classified immutable: what the method
    header says - `implicit` (no receiver written: the parser supplies `&this`),
    `mut-value` (`mut this`, a classifier miss), or `shared` (`&this` written)."""
    line, col = source_line(label, loc)
    if line is None or not line[col - 1:].startswith('this'):
        return None
    h = enclosing_header(label, loc)
    if h is None:
        return None
    if re.search(r'\bmut\s+this\b', h):
        return 'mut-value'
    if re.search(r'&\s*this\b|\bthis\b', h):
        return 'shared'
    return 'implicit'


places = {}
for path in sys.argv[1:]:
    for ln in open(path, encoding='utf-8', errors='replace'):
        parts = ln.rstrip('\r\n').split('\t')
        if len(parts) < 6 or parts[1] != 'SHADOW' or parts[2] != 'MUTREF':
            continue
        label, cat, cls, loc = parts[0], parts[3], parts[4], parts[5]
        l = loc.replace('\\', '/')
        if '/stdlib/' in l and not l.startswith('./'):
            pop, key = 'stdlib', l.split('/stdlib/', 1)[1]
        elif label == 'compiler':
            pop, key = 'compiler', l
        elif label.startswith('tools/CryoLSP'):
            pop, key = 'lsp', l
        elif label.startswith('examples/'):
            pop, key = 'examples', label + l
        elif label.startswith('tests/tests/negative'):
            pop, key = 'negative', label
        else:
            pop, key = 'tests', label.split('(')[0] + '|' + l
        places[(cat, cls, pop, key)] = (label, loc)


def verdict(cat, cls):
    """What the place needs once the check lands."""
    inner = cls
    m = re.match(r'amp<(.*)>$', cls)
    if cat in ('arg', 'amp') and m:
        inner = m.group(1)
        if 'local-const' in inner or 'ref-shared' in inner:
            return 'REFUSED: borrows an immutable place'
        if 'nonlocal' in inner or inner in ('index', 'expr', 'call', 'unary'):
            return 'UNSURE: `&x` of a global/untracked/indexed place'
        return 'REWRITE: `&x` of a mutable place, needs the new operator'
    if cat == 'arg':
        if 'ref-shared' in cls:
            return 'REFUSED: a shared reference passed on'
        return 'OTHER arg: ' + cls
    if cat == 'recv':
        root = re.sub(r'^(field<)+', '', cls).rstrip('>')
        if root in ('local-mut', 'ref-mut', 'ptr'):
            return 'FINE: receiver is mutable'
        if 'local-const' in cls or 'ref-shared' in cls:
            return 'REFUSED: mutates through an immutable receiver'
        return 'UNSURE receiver: ' + cls
    return 'OTHER ' + cat + ' ' + cls


tally = collections.Counter()
by_cls = collections.Counter()
unread = 0
for (cat, cls, pop, key), (label, loc) in places.items():
    v = verdict(cat, cls)
    lw = lowered(label, loc, cat)
    if lw is None:
        unread += 1
    if lw:
        v = v.split(':')[0] + ' [generated]'
    elif v.startswith('REFUSED') and cat == 'recv':
        tr = this_root(cls, label, loc)
        if tr == 'implicit':
            v = 'REFUSED: this, method writes no receiver (parser supplies &this)'
        elif tr == 'mut-value':
            v = 'FINE: this, by-value mut this (classifier miss)'
        elif tr == 'shared':
            v = 'REFUSED: this, method written &this'
    tally[(v, pop)] += 1
    by_cls[(cat, cls)] += 1
print('places whose source line could not be read: %d' % unread)
pops = ['compiler', 'stdlib', 'tests', 'examples', 'lsp', 'negative']
verds = sorted(set(v for v, _ in tally))
print('%-62s %s total' % ('distinct places', ' '.join('%8s' % p for p in pops)))
for v in verds:
    row = [tally[(v, p)] for p in pops]
    print('%-62s %s %6d' % (v[:62], ' '.join('%8d' % n for n in row), sum(row)))
print()
for (cat, cls), n in by_cls.most_common(40):
    print('%6d  %-5s %s' % (n, cat, cls))

# A fixed-seed sample of each verdict's places, with the source text there.
import random
rnd = random.Random(7)
groups = collections.defaultdict(list)
for (cat, cls, pop, key), (label, loc) in places.items():
    v = verdict(cat, cls)
    if lowered(label, loc, cat):
        v = v.split(':')[0] + ' [generated]'
    groups[v].append((pop, cls, label, loc))
for v in sorted(groups):
    if v.startswith('FINE'):
        continue
    print('\n== %s (%d)' % (v, len(groups[v])))
    for pop, cls, label, loc in rnd.sample(groups[v], min(12, len(groups[v]))):
        line, col = source_line(label, loc)
        text = (line or '?').strip()[:100]
        print('  %-8s %-22s %s  |  %s' % (pop, cls, loc.split('/')[-1], text))

# Full list of the written REFUSED places, for reading.
with open('refused-written.txt', 'w', encoding='utf-8') as out:
    for (cat, cls, pop, key), (label, loc) in sorted(places.items(), key=lambda kv: (kv[0][2], kv[1][1])):
        v = verdict(cat, cls)
        if not v.startswith('REFUSED') or lowered(label, loc, cat):
            continue
        tr = this_root(cls, label, loc) if cat == 'recv' else None
        line, col = source_line(label, loc)
        out.write('%s\t%s\t%s\t%s\t%s\t%s\n' % (pop, cat, cls, tr, label + '|' + loc, (line or '?').strip()[:120]))

# Distinct SOURCE locations per verdict, merging the populations that compile
# the same file (the LSP links the compiler; every program imports the stdlib).
loc_sets = collections.defaultdict(set)
for (cat, cls, pop, key), (label, loc) in places.items():
    v = verdict(cat, cls)
    lw = lowered(label, loc, cat)
    if lw:
        v = v.split(':')[0] + ' [generated]'
    elif v.startswith('REFUSED') and cat == 'recv':
        tr = this_root(cls, label, loc)
        if tr == 'implicit':
            v = 'REFUSED: this, no receiver written'
        elif tr == 'mut-value':
            v = 'FINE: mut this (classifier miss)'
        elif tr == 'shared':
            v = 'REFUSED: this, &this written'
        else:
            line, col = source_line(label, loc)
            if line and re.match(r'\w+\.drop\(\)', line[col - 1:]):
                v = 'REFUSED: explicit .drop()'
    m = re.match(r'^(.*):(\d+):(\d+)$', loc.replace(chr(92), '/'))
    base = m.group(1).split('/')[-1] if m else loc
    loc_sets[v].add((cat, base, m.group(2) if m else '', m.group(3) if m else ''))
print('\ndistinct source locations (populations merged):')
for v in sorted(loc_sets):
    print('%6d  %s' % (len(loc_sets[v]), v))
