#!/bin/bash
# The §8.241 mutation pairs: the OLD gate (1f852403's, against 1f852403's
# golden) and the NEW gate (this tree's, against its golden), over the real
# tree with one audit mutation in at a time, the tree restored after each.
# Run from the repo root; writes the old gate and golden to .objcmp/.
cd "$(dirname "$0")/../../.." || exit 2
mkdir -p .objcmp
git show 1f852403:tests/lane-baseline.txt > .objcmp/lane-baseline-old.txt
# The old gate has no --golden; its GOLDEN line is pointed at the old golden.
git show 1f852403:scripts/lane-gate.py \
  | sed 's|^GOLDEN = .*|GOLDEN = os.path.join(ROOT, ".objcmp", "lane-baseline-old.txt")|' \
  > .objcmp/lane-gate-old.py
grep -q 'lane-baseline-old' .objcmp/lane-gate-old.py || { echo "old gate not repointed"; exit 2; }
NEWG=tests/lane-baseline.txt
CALLER=compiler/src/compiler/sema/lane_probe_caller.cryo
EXT=compiler/src/compiler/types/registry_ext.cryo

caller() {
  printf 'type struct LaneProbeCaller {\n    ctx: CompilationContext*;\n\n    go(&this, name: SymbolStr) -> void {\n        %s\n    }\n}\n' "$1" > "$CALLER"
}
pair() {
  echo "---- $1"
  echo -n "OLD gate: "; python .objcmp/lane-gate-old.py 2>&1 | tail -1 | cut -c1-60; echo "  exit=${PIPESTATUS[0]}"
  echo -n "NEW gate: "; python scripts/lane-gate.py --golden "$NEWG" 2>&1 | grep -E 'TOTAL|OK --' | head -3; echo "  exit=${PIPESTATUS[0]}"
}

echo "==== control: unmutated tree"
pair "control"

echo "==== hole 1: cross-file implement struct block"
printf 'implement struct GenericRegistry {\n    probe(&this, name: SymbolStr) -> i64 { return 0; }\n}\n' > "$EXT"
caller 'this.ctx.generic_registry.probe(name);'
pair "hole 1"
rm -f "$EXT" "$CALLER"

echo "==== hole 2: a reader keyed by string"
python -c "
p='compiler/src/compiler/decl_index.cryo'
s=open(p,encoding='utf-8').read()
old='    lookup_type(&this, name: SymbolStr) -> TypeRef {'
assert s.count(old)==1
s=s.replace(old,'    by_spelling(&this, name: string) -> TypeRef { return TypeRef::invalid(); }\n'+old)
open(p,'w',encoding='utf-8',newline='').write(s)
"
caller 'this.ctx.decl_index.by_spelling("x");'
pair "hole 2"
git checkout -- compiler/src/compiler/decl_index.cryo
rm -f "$CALLER"

echo "==== hole 3: a reader on another store"
caller 'this.ctx.module_graph.find_module_index(name);'
pair "hole 3"
rm -f "$CALLER"

echo "==== after: tree status"
git status --short
