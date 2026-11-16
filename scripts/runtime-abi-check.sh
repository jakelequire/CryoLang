#!/usr/bin/env bash
# runtime-abi-check.sh - the lang-item registry against what is built and emitted.
#
# runtime/abi/lang_items.def names every `__cryo_` symbol that crosses the
# boundary between generated code (or the stdlib) and the runtime, and in its
# DEFINED-IN column the link kinds whose runtime must define it.  Nothing else
# ties the three together, so a renamed handler, a configuration that stopped
# exporting one, or a new call emitted by codegen links or fails only in the
# configuration that happens to reach it.  This builds the runtime in every
# configuration a link names - freestanding, hosted under abort, hosted under
# unwind - for the compiler's own target, with the compiler under test, and
# refuses:
#
#   - a registry symbol missing from a configuration that must define it;
#   - a `__cryo_` symbol two members of one configuration's archive define;
#   - a `__cryo_` symbol a configuration exports that no registry row names;
#   - a `"__cryo_...` name spelled in compiler/src that no row names (a
#     runtime symbol, or one of the compiler's own, possibly by prefix);
#   - a `__cryo_...(` call in stdlib/ that is not a hosted runtime symbol.
#
# Usage: scripts/runtime-abi-check.sh    (CRYO=<compiler>, default the one
#                                         compiler/build holds; NM=<nm>)
# The archives are built under runtime/.bin/<triple>/abi-check/<configuration>/.
# The verdict is the last line: `runtime-abi-check: OK ...` or `... FAIL ...`.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
REGISTRY="$ROOT/runtime/abi/lang_items.def"
problems=0
problem() { echo "  FAIL  $*"; problems=$((problems + 1)); }

CRYO=${CRYO:-}
if [ -z "$CRYO" ]; then
    for candidate in "$ROOT/compiler/build/cryo.exe" "$ROOT/compiler/build/cryo"; do
        if [ -x "$candidate" ]; then CRYO=$candidate; break; fi
    done
fi
if [ -z "$CRYO" ]; then
    echo "runtime-abi-check: FAIL (no compiler under compiler/build; run make cryo, or set CRYO)"
    exit 1
fi

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

triple=$("$CRYO" version --triple | tr -d '\r')
case "$triple" in
    *windows*) os=windows ;;
    *linux*)   os=linux ;;
    *)         echo "runtime-abi-check: FAIL ($CRYO names its target \"$triple\")"; exit 1 ;;
esac
sets=0
symbols=0
for configuration in freestanding hosted-abort hosted-unwind; do
    case "$configuration" in
        freestanding)  kind=freestanding; option=--no-runtime ;;
        hosted-abort)  kind=hosted;       option=--panic=abort ;;
        hosted-unwind) kind=hosted;       option=--panic=unwind ;;
    esac
    build="$ROOT/runtime/.bin/$triple/abi-check/$configuration"
    if ! "$CRYO" build "$ROOT/runtime" --build-dir="$build" --no-incremental "$option"; then
        problem "$triple $configuration: the runtime does not build"
        continue
    fi
    archive="$build/libcryort.a"
    if [ ! -f "$archive" ]; then
        problem "$triple $configuration: the build wrote no $archive"
        continue
    fi
    sets=$((sets + 1))
    # The configuration's definitions, one line per defining member: a
    # lower-case type is local, `U` undefined.  A native Windows nm ends its
    # lines CRLF.
    members_defining=$("$NM" -g --defined-only "$archive" | tr -d '\r' \
                       | awk 'NF == 3 && $2 ~ /^[A-TV-Z]$/ { print $3 }')
    defined=$(printf '%s\n' "$members_defining" | sort -u)
    # Both panic strategies' modules are in every configuration's build, each
    # gated on the strategy it implements; a symbol two members define is one
    # a link takes from whichever member it reaches first.
    for symbol in $(printf '%s\n' "$members_defining" | grep '^__cryo_' | sort | uniq -d); do
        problem "$triple $configuration: $symbol is defined by more than one member of the archive"
    done
    verdicts=$(printf '%s\n--\n%s\n' "$rows" "$defined" | undefined "$kind" "$os")
    symbols=$((symbols + $(printf '%s\n' "$verdicts" | grep -c .)))
    for symbol in $(printf '%s\n' "$verdicts" | awk '$1 == "missing" { print $2 }'); do
        problem "$triple $configuration: $symbol is registered as defined here, and the archive does not define it"
    done
    for symbol in $(printf '%s\n--\n%s\n' "$rows" "$(printf '%s\n' "$defined" | grep '^__cryo_')" \
                    | unregistered '(freestanding|hosted)'); do
        problem "$triple $configuration: exports $symbol, which no registry row names"
    done
done

compiler_names=$(grep -rhoE '"__cryo_[A-Za-z0-9_]*' "$ROOT/compiler/src" --include='*.cryo' | tr -d '"' | sort -u)
for name in $(printf '%s\n--\n%s\n' "$rows" "$compiler_names" | unregistered '.'); do
    problem "compiler/src spells \"$name\", which no registry row names"
done
stdlib_calls=$(grep -rhoE '__cryo_[A-Za-z0-9_]*[A-Za-z0-9]\(' "$ROOT/stdlib" --include='*.cryo' | tr -d '(' | sort -u)
for name in $(printf '%s\n--\n%s\n' "$rows" "$stdlib_calls" | unregistered 'hosted'); do
    problem "stdlib/ calls $name, which is not a hosted runtime symbol in the registry"
done

summary="$sets configuration(s) on $triple, $symbols registered definition(s), $(printf '%s\n' "$compiler_names" | grep -c .) compiler name(s), $(printf '%s\n' "$stdlib_calls" | grep -c .) stdlib call(s), $NM"
if [ "$problems" -gt 0 ]; then
    echo "runtime-abi-check: FAIL ($problems problem(s); $summary)"
    exit 1
fi
echo "runtime-abi-check: OK ($summary)"
