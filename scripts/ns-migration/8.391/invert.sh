#!/usr/bin/env bash
# The inversions for the link-symbol and keyword types, each alone over an
# otherwise unmodified tree, then the tree restored and re-checked as the
# control.  Run from the repo root with CRYO naming a compiler (any build of
# this tree; each mutation is one site).  MODES picks a subset (default all).
#   A: a raw, unmarked string literal handed where a link symbol is expected
#   B: a name (`SymbolStr`) handed where a link symbol is expected
#   C: a link symbol handed where the declaration index's interned symbol is
#      expected - the one place its bytes are interned
#   D: a raw, unmarked string handed where a keyword spelling is expected
#   E: a name (`SymbolStr`) handed where a keyword spelling is expected
#   F: the keyword table matched on the `Keyword` itself, not its bytes - a
#      string pattern against a struct, which compiled to arms that never
#      matched until the check refused it
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
LX=compiler/src/compiler/lex/lexer.cryo
LX_ORIG='        const kw_kind: TokenType = TokenType::from_keyword(Keyword::new(text));'
FILE[D]=$LX; ORIG[D]=$LX_ORIG; NEW[D]=${LX_ORIG/Keyword::new(text)/text}
PA=compiler/src/compiler/parser/parser.cryo
PA_ORIG='if (TokenType::from_keyword(Keyword::new(this.current_type_name)).is_primitive_type()) {'
FILE[E]=$PA; ORIG[E]=$PA_ORIG; NEW[E]=${PA_ORIG/Keyword::new(this.current_type_name)/SymbolStr::empty()}
LM=compiler/src/compiler/lex/_module.cryo
LM_ORIG='        const s: string = s_text.as_string();
        return match (s) {
            // Control Flow'
FILE[F]=$LM; ORIG[F]=$LM_ORIG; NEW[F]=${LM_ORIG/match (s)/match (s_text)}
run() {
    (cd compiler && "$CRYO" check src/main.cryo 2>&1) | grep -E -A6 '^error\[|No errors found|Check failed' | head -14
}
for m in ${MODES:-A B C D E F}; do
    f=${FILE[$m]}
    cp "$f" "$f.orig"
    python -c "import sys; p,o,n=sys.argv[1:4]; s=open(p,encoding='utf-8',newline='').read(); assert s.count(o)==1, 'mutation site not unique'; open(p,'w',encoding='utf-8',newline='').write(s.replace(o,n))" "$f" "${ORIG[$m]}" "${NEW[$m]}"
    echo "=== mutation $m"
    run
    mv "$f.orig" "$f"
done
echo "=== control (restored tree)"
run
