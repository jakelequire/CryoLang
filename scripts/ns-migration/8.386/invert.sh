#!/usr/bin/env bash
# The inversions, each alone over an otherwise unmodified tree, then the
# tree restored and re-checked as the control.  Run from the repo root with
# CRYO naming a compiler (any build of this tree; the cap does not matter -
# each mutation is one site).
#   A: a name (`n: SymbolStr`) handed where message text is expected
#   B: text (`site: Text`) handed where a name is expected
#   C: a raw, unmarked `string` handed where a file path (`Text`) is expected
#   D: a name (`SymbolStr`) handed where a file path is expected
set -u
CRYO=${CRYO:-bin/cryo}
DI=compiler/src/compiler/decl_index.cryo
IN=compiler/src/compiler/instance.cryo
DI_ORIG='                    Res::PrimTy(n) => { this.lookup_type(n) }'
IN_ORIG='        mut config: ProjectConfig = ProjectConfig::parse(config_path_text);
        if (config.error_count > 0) {
            // ProjectConfig::parse already'
declare -A FILE ORIG NEW
FILE[A]=$DI; ORIG[A]=$DI_ORIG
NEW[A]='                    Res::PrimTy(n) => { compiler::resolver::res::record_unregistered_def(n); this.lookup_type(n) }'
FILE[B]=$DI; ORIG[B]=$DI_ORIG
NEW[B]='                    Res::PrimTy(n) => { this.lookup_type(site) }'
FILE[C]=$IN; ORIG[C]=$IN_ORIG; NEW[C]=${IN_ORIG/parse(config_path_text)/parse(config_path)}
FILE[D]=$IN; ORIG[D]=$IN_ORIG; NEW[D]=${IN_ORIG/parse(config_path_text)/parse(SymbolStr::empty())}
run() {
    (cd compiler && "$CRYO" check src/main.cryo 2>&1) | grep -E -A6 '^error\[|No errors found|Check failed' | head -14
}
for m in A B C D; do
    f=${FILE[$m]}
    cp "$f" "$f.orig"
    python -c "import sys; p,o,n=sys.argv[1:4]; s=open(p,encoding='utf-8',newline='').read(); assert s.count(o)==1, 'mutation site not unique'; open(p,'w',encoding='utf-8',newline='').write(s.replace(o,n))" "$f" "${ORIG[$m]}" "${NEW[$m]}"
    echo "=== mutation $m"
    run
    mv "$f.orig" "$f"
done
echo "=== control (restored tree)"
run
