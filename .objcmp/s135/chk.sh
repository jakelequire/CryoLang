#!/bin/bash
# cryo check a compiler tree with a given compiler; print rc and the error count.
#   usage: chk.sh <compiler-name> <tag> <tree-dir>
S=/c/Programming/apps/CryoLang/.objcmp/s135
cd "$3" || exit 1
export CRYO_STDLIB=C:/Programming/apps/CryoLang/stdlib CRYO_CC=gcc
"$S/bin/$1.exe" check src/main.cryo > "$S/chk-$2.log" 2>&1
echo "$2 rc=$? $(grep -a -c '^error' "$S/chk-$2.log") errors; $(tail -1 "$S/chk-$2.log")"
