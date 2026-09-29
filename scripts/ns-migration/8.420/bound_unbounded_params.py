"""Adds a `where` bound to the declaration of each stdlib type whose own
methods call a method on a type parameter nothing bounded.

Each entry names the file, the declaration's exact first line, and the
bound.  A declaration not found exactly once is an error: nothing is
written unless every entry applies.

usage: python scripts/ns-migration/8.420/bound_unbounded_params.py [--check]
"""
import sys, os

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
EDITS = [
    ('stdlib/collections/raw_buffer.cryo', 'type struct RawBuffer<T, A = GlobalAlloc> {', 'A: Allocator'),
    ('stdlib/collections/array.cryo',      'type struct Array<T, A = GlobalAlloc> {',     'A: Allocator'),
    ('stdlib/collections/hashmap.cryo',    'type struct HashMap<K, V, A = GlobalAlloc> {', 'A: Allocator'),
    ('stdlib/sync/mutex.cryo',             'type struct Mutex<T, A = GlobalAlloc> {',     'A: Allocator'),
    ('stdlib/sync/rwlock.cryo',            'type struct RwLock<T, A = GlobalAlloc> {',    'A: Allocator'),
    ('stdlib/alloc/rc.cryo',               'type struct Rc<T, A = GlobalAlloc> {',        'A: Allocator'),
    ('stdlib/alloc/arc.cryo',              'type struct Arc<T, A = GlobalAlloc> {',       'A: Allocator'),
    ('stdlib/alloc/box.cryo',              'type struct Box<T, A = GlobalAlloc> {',       'A: Allocator'),
    ('stdlib/io/buf.cryo',                 'type struct BufWriter<W> {',                  'W: Write'),
    ('stdlib/io/buf.cryo',                 'type struct BufReader<R> {',                  'R: Read'),
]

def main():
    check = '--check' in sys.argv
    texts = {}
    for rel, head, bound in EDITS:
        p = os.path.join(ROOT, rel)
        if rel not in texts:
            texts[rel] = open(p, encoding='utf-8', newline='').read()
        t = texts[rel]
        done = head[:-1] + 'where ' + bound + ' {'
        if t.count(done) == 1:
            continue
        n = t.count(head)
        if n != 1:
            sys.exit('%s: %r found %d times' % (rel, head, n))
        if check:
            sys.exit('%s: %r not bounded' % (rel, head))
        texts[rel] = t.replace(head, done)
    if check:
        print('bound_unbounded_params: OK -- %d declarations bounded' % len(EDITS))
        return
    for rel, t in texts.items():
        open(os.path.join(ROOT, rel), 'w', encoding='utf-8', newline='').write(t)
    print('bound_unbounded_params: wrote %d declarations in %d files' % (len(EDITS), len(texts)))

main()
