#!/usr/bin/env bash
# One-off codemod: PendingThenReady moved from std::future::ready to
# std::test::pending. Rewrites every import of it under tests/ in place;
# the files reach it only by import. Run from the repository root.
#
# A brace import binds the module it names as well as the names in its
# braces, so `import std::future::{ PendingThenReady };` also made
# `future::...` reachable. Where a file spells such a path and has no other
# import that binds `future`, `import std::future;` is added beside the
# new import, so every path in the file resolves as before.
set -euo pipefail

files=$(git grep -l 'PendingThenReady' -- 'tests/*.cryo' 'tests/**/*.cryo')
for f in $files; do
    keep_future=''
    if grep -v '^import ' "$f" | grep -q '\bfuture::' \
       && ! grep -q '^import std::future;$\|^import std::future::{ [^}]*block_on' "$f"; then
        keep_future=$'import std::future;\n'
    fi
    KEEP="$keep_future" perl -pi -e '
        s/^import std::future::ready::\{ PendingThenReady \};$/import std::test::pending::{ PendingThenReady };/;
        s/^import std::future::\{ PendingThenReady \};$/$ENV{KEEP}import std::test::pending::{ PendingThenReady };/;
        s/^import std::future::ready::\{ PendingThenReady, Ready \};$/import std::future::ready::{ Ready };\nimport std::test::pending::{ PendingThenReady };/;
        s/^import std::future::\{ PendingThenReady, block_on \};$/import std::future::{ block_on };\nimport std::test::pending::{ PendingThenReady };/;
    ' "$f"
done

left=$(git grep -n 'import std::future.*PendingThenReady' -- 'tests/*.cryo' 'tests/**/*.cryo' || true)
if [ -n "$left" ]; then
    echo "imports of PendingThenReady through std::future remain:"
    echo "$left"
    exit 1
fi
echo "rewrote the imports in $(echo "$files" | wc -w) files"
