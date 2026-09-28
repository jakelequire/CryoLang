#!/usr/bin/env bash
# The inversions for the link-symbol and keyword types, each alone over an
# otherwise unmodified tree, then the tree restored and re-checked as the
# control.  Run from the repo root with CRYO naming a compiler (any build of
# this tree; each mutation is one site).  MODES picks a subset (default all).
#   A: a raw, unmarked string literal handed where a link symbol is expected
#   B: a name (`SymbolStr`) handed where a link symbol is expected
#   C: a link symbol handed where the declaration index's interned symbol is
#      expected - the one place its bytes are interned
set -u
CRYO=${CRYO:-bin/cryo}
IE=compiler/src/compiler/codegen/ops/intrinsic_emitter.cryo
IE_ORIG='        mut fn: LValue = this.llvm_module.get_named_function(MangledName::new("abort"));'
DI=compiler/src/compiler/decl_index.cryo
DI_ORIG='SymbolStr::empty(), intern.intern(mangled.as_string()), DefId::invalid(), span);'
declare -A FILE ORIG NEW
FILE[A]=$IE; ORIG[A]=$IE_ORIG; NEW[A]=${IE_ORIG/MangledName::new(\"abort\")/\"abort\"}
SR=compiler/src/compiler/codegen/ops/symbol_resolver.cryo
SR_ORIG='        mut fn_val: LValue = this.llvm_module.get_named_function(MangledName::new(ir_name));'
FILE[B]=$SR; ORIG[B]=$SR_ORIG; NEW[B]=${SR_ORIG/MangledName::new(ir_name)/symbol}
FILE[C]=$DI; ORIG[C]=$DI_ORIG; NEW[C]=${DI_ORIG/intern.intern(mangled.as_string())/mangled}
run() {
    (cd compiler && "$CRYO" check src/main.cryo 2>&1) | grep -E -A6 '^error\[|No errors found|Check failed' | head -14
}
for m in ${MODES:-A B C}; do
    f=${FILE[$m]}
    cp "$f" "$f.orig"
    python -c "import sys; p,o,n=sys.argv[1:4]; s=open(p,encoding='utf-8',newline='').read(); assert s.count(o)==1, 'mutation site not unique'; open(p,'w',encoding='utf-8',newline='').write(s.replace(o,n))" "$f" "${ORIG[$m]}" "${NEW[$m]}"
    echo "=== mutation $m"
    run
    mv "$f.orig" "$f"
done
echo "=== control (restored tree)"
run
