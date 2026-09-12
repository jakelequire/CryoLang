#!/usr/bin/env bash
# LSP built directly under the current compiler; SHADOW lines to $S/$1-lsp-lines.txt
R="$(git rev-parse --show-toplevel)"; S="${OBJCMP_OUT:-$R/.objcmp}"; mkdir -p "$S"; T="$1"; cd "$R" || exit 1
(cd tools/CryoLSP && rm -rf build/gate-direct && CRYO_STDLIB=$R/stdlib CRYO_CC=gcc $R/compiler/build/cryo.exe build --build-dir=build/gate-direct > "$S/$T-lsp.log" 2>&1; echo "lsp direct exit=$?")
grep -a 'Building cryolsp' "$S/$T-lsp.log" | head -1
grep -a '^SHADOW' "$S/$T-lsp.log" > "$S/$T-lsp-lines.txt"
echo "SHADOW lines: $(wc -l < "$S/$T-lsp-lines.txt")"
awk -F'\t' '{d=($5 ~ /::drop$/); r=$5; sub(/::[^:]*$/,"",r); a=(r ~ /\[\]$/); print $2, (d?"drop":"other"), (a?"array":"named")}' "$S/$T-lsp-lines.txt" | sort | uniq -c
grep -a 'error\[' "$S/$T-lsp.log" | head -5
