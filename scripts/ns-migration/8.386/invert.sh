#!/usr/bin/env bash
# The two inversions, each alone over an otherwise unmodified tree, then the
# tree restored and re-checked as the control.  Run from the repo root with
# CRYO naming a compiler (any build of this tree; the cap does not matter -
# each mutation is one site).
#   A: a name (`n: SymbolStr`) handed where text is expected
#   B: text (`site: Text`) handed where a name is expected
set -u
CRYO=${CRYO:-bin/cryo}
F=compiler/src/compiler/decl_index.cryo
ORIG='                    Res::PrimTy(n) => { this.lookup_type(n) }'
A='                    Res::PrimTy(n) => { compiler::resolver::res::record_unregistered_def(n); this.lookup_type(n) }'
B='                    Res::PrimTy(n) => { this.lookup_type(site) }'
cp "$F" "$F.orig"
run() {
    (cd compiler && "$CRYO" check src/main.cryo 2>&1) | grep -E -A6 '^error\[|No errors found|Check failed' | head -14
}
for m in A B; do
    python -c "import sys; p,o,n=sys.argv[1:4]; s=open(p,encoding='utf-8',newline='').read(); assert s.count(o)==1; open(p,'w',encoding='utf-8',newline='').write(s.replace(o,n))" "$F" "$ORIG" "${!m}"
    echo "=== mutation $m"
    run
    cp "$F.orig" "$F"
done
rm "$F.orig"
echo "=== control (restored tree)"
run
