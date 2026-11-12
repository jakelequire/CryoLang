#!/usr/bin/env bash
# runtime-abi-check.sh - the lang-item registry against what is built and emitted.
#
# runtime/abi/lang_items.def names every `__cryo_` symbol that crosses the
# boundary between generated code (or the stdlib) and the runtime, and in its
# DEFINED-IN column the link kinds whose runtime must define it.  Nothing else
# ties the three together, so a renamed handler, a tier that stopped
# exporting one, or a new call emitted by codegen links or fails only in the
# configuration that happens to reach it.  This refuses, for every target
# whose runtime archives are built under runtime/.bin/<triple>/:
#
#   - a registry symbol missing from a link set that must define it - every
#     link set the driver composes (codegen/passes.cryo), freestanding and
#     hosted, under both panic strategies;
#   - a `__cryo_` symbol a link set exports that no registry row names;
#   - a `"__cryo_...` name spelled in compiler/src that no row names (a
#     runtime symbol, or one of the compiler's own, possibly by prefix);
#   - a `__cryo_...(` call in stdlib/ that is not a hosted runtime symbol.
#
# Usage: scripts/runtime-abi-check.sh            (NM=<nm> to pick the tool)
# The verdict is the last line: `runtime-abi-check: OK ...` or `... FAIL ...`.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
REGISTRY="$ROOT/runtime/abi/lang_items.def"
problems=0
problem() { echo "  FAIL  $*"; problems=$((problems + 1)); }

# Every OS's objects are read by one tool: llvm-nm reads ELF and COFF alike,
# a binutils nm only its own host's format.
NM=${NM:-}
if [ -z "$NM" ]; then
    for candidate in llvm-nm llvm-nm-20 nm; do
        if command -v "$candidate" >/dev/null 2>&1; then NM=$candidate; break; fi
    done
fi
if [ -z "$NM" ]; then
    echo "runtime-abi-check: FAIL (no nm found; set NM)"
    exit 1
fi

# "symbol|defined-in" for each registry row.
rows=$(grep -v '^#' "$REGISTRY" | grep '|' | awk -F'|' '{
    s = $1; gsub(/^[ \t]+|[ \t]+$/, "", s)
    w = $3; gsub(/^[ \t]+|[ \t]+$/, "", w); gsub(/[ \t]+/, "", w)
    print s "|" w }')
row_count=$(printf '%s\n' "$rows" | grep -c '|')
if [ "$row_count" -lt 10 ]; then
    echo "runtime-abi-check: FAIL (read $row_count registry rows from $REGISTRY; its format moved)"
    exit 1
fi

# The registry rows, a `--` line, then names, one per line on stdin; prints
# each name no row whose DEFINED-IN matches the regex `$1` names, exactly or
# under a `prefix*` row.  One process for a whole list: a fork per name is
# minutes on a Windows shell.
unregistered() {
    awk -F'|' -v where="$1" '
        !names && $0 == "--" { names = 1; next }
        !names { if ($2 ~ where) { if ($1 ~ /\*$/) prefix[substr($1, 1, length($1) - 1)] = 1; else exact[$1] = 1 }; next }
        $0 != "" {
            found = ($0 in exact)
            for (p in prefix) if (index($0, p) == 1) found = 1
            if (!found) print
        }'
}

# The rows, a `--` line, then the defined names; prints each row symbol whose
# DEFINED-IN names kind `$1` (on OS `$2`) and which is not defined.
undefined() {
    awk -F'|' -v kind="$1" -v os="$2" '
        !names && $0 == "--" { names = 1; next }
        !names { if (("," $2 ",") ~ ("," kind ",") || ("," $2 ",") ~ ("," kind ":" os ",")) want[$1] = 1; next }
        { have[$0] = 1 }
        END { for (s in want) { print (s in have ? "ok " : "missing ") s } }'
}

# The archives the driver links for one kind and strategy, in its order.
link_set() {
    case "$1/$2" in
        freestanding/abort)  echo "libcryort-alloc.a libcryort-backtrace.a libcryort-core.a libcryort-panic-abort.a" ;;
        freestanding/unwind) echo "libcryort-alloc.a libcryort-backtrace.a libcryort-core.a libcryort-panic-unwind.a" ;;
        hosted/abort)        echo "libcryort-core-hosted.a libcryort-panic-abort-hosted.a" ;;
        hosted/unwind)       echo "libcryort-core-hosted.a libcryort-panic-unwind-hosted.a" ;;
    esac
}

sets=0
symbols=0
targets=""
for dir in "$ROOT"/runtime/.bin/*/; do
    [ -f "$dir/libcryort-core.a" ] || [ -f "$dir/libcryort-core-hosted.a" ] || continue
    triple=$(basename "$dir")
    targets="$targets $triple"
    case "$triple" in *windows*) os=windows ;; *) os=linux ;; esac
    # Every archive's global definitions, once, each line led by its
    # `<archive path>:<member>:`.  A native Windows nm ends its lines CRLF.
    every=$("$NM" -A -g --defined-only "$dir"libcryort-*.a | tr -d '\r')
    for kind in freestanding hosted; do
        for strategy in abort unwind; do
            archives=$(link_set "$kind" "$strategy")
            built=0
            for archive in $archives; do
                if [ -f "$dir$archive" ]; then
                    built=$((built + 1))
                else
                    problem "$triple $kind/$strategy: $archive is not built"
                fi
            done
            [ "$built" -gt 0 ] || continue
            sets=$((sets + 1))
            # The set's definitions: a lower-case type is local, `U` undefined.
            # The archive is matched by name: a native nm on a Windows shell
            # prints the path with a drive letter, so `:` does not end it.
            defined=$(printf '%s\n' "$every" | awk -v set=" $archives " '
                match($1, /libcryort[^:\/\\]*\.a:/) && NF == 4 && $3 ~ /^[A-TV-Z]$/ {
                    if (index(set, " " substr($1, RSTART, RLENGTH - 1) " ")) print $4 }' | sort -u)
            verdicts=$(printf '%s\n--\n%s\n' "$rows" "$defined" | undefined "$kind" "$os")
            symbols=$((symbols + $(printf '%s\n' "$verdicts" | grep -c .)))
            for symbol in $(printf '%s\n' "$verdicts" | awk '$1 == "missing" { print $2 }'); do
                problem "$triple $kind/$strategy: $symbol is registered as defined here, and no archive of the set defines it"
            done
            for symbol in $(printf '%s\n--\n%s\n' "$rows" "$(printf '%s\n' "$defined" | grep '^__cryo_')" \
                            | unregistered '(freestanding|hosted)'); do
                problem "$triple $kind/$strategy: exports $symbol, which no registry row names"
            done
        done
    done
done
if [ "$sets" -eq 0 ]; then
    echo "runtime-abi-check: FAIL (no runtime archives under runtime/.bin/<triple>/; run make runtime-tiers)"
    exit 1
fi

compiler_names=$(grep -rhoE '"__cryo_[A-Za-z0-9_]*' "$ROOT/compiler/src" --include='*.cryo' | tr -d '"' | sort -u)
for name in $(printf '%s\n--\n%s\n' "$rows" "$compiler_names" | unregistered '.'); do
    problem "compiler/src spells \"$name\", which no registry row names"
done
stdlib_calls=$(grep -rhoE '__cryo_[A-Za-z0-9_]*[A-Za-z0-9]\(' "$ROOT/stdlib" --include='*.cryo' | tr -d '(' | sort -u)
for name in $(printf '%s\n--\n%s\n' "$rows" "$stdlib_calls" | unregistered 'hosted'); do
    problem "stdlib/ calls $name, which is not a hosted runtime symbol in the registry"
done

summary="$sets link set(s) on$targets, $symbols registered definition(s), $(printf '%s\n' "$compiler_names" | grep -c .) compiler name(s), $(printf '%s\n' "$stdlib_calls" | grep -c .) stdlib call(s), $NM"
if [ "$problems" -gt 0 ]; then
    echo "runtime-abi-check: FAIL ($problems problem(s); $summary)"
    exit 1
fi
echo "runtime-abi-check: OK ($summary)"
