#!/bin/sh
# Print a tests/started-passing declaration for every object a verify run
# found moved, each with REASON: `moved:<project>`, `moved:examples/<name>`,
# `<file>.cryo` for a unit-test file, `moved-instances:<module>` and
# `moved-archive:<module>`.  A one-off: run once for the change whose
# commit carries it, and deleted in the next commit.
#
#   scripts/declare-moved-objects.sh .verify/runs/<run> "<reason>"
run="$1"
reason="$2"
for population in tests examples stdlib; do
    awk 'NR==FNR { a[$2] = $1; next } ($2 in a) && a[$2] != $1 { print $2 }' \
        "$run/baseline/base.$population.sha256" "$run/tree.$population.sha256"
done | awk '
/^tests\/build\/target\/release\/host-windows\/local\/deps\// {
    s = $0; sub(/^.*\/local\/deps\//, "", s); sub(/\.o$/, "", s); gsub(/\//, "::", s); print "UNIT " s; next }
/^tests\/tests\/projects\// {
    s = $0; sub(/^tests\/tests\/projects\//, "", s); sub(/\/.*/, "", s); print "moved:" s; next }
/^examples\// {
    s = $0; sub(/^examples\//, "", s); sub(/\/.*/, "", s); print "moved:examples/" s; next }
/^tests\/build\/target\/release\/host-windows\/std\/deps\// {
    s = $0; sub(/^.*\/std\/deps\//, "", s); sub(/\.o$/, "", s); gsub(/\//, "::", s); print "moved-instances:" s; next }
/\/local\/deps\/std\// {
    s = $0; sub(/^.*\/local\/deps\//, "", s); sub(/\.o$/, "", s); gsub(/\//, "::", s); print "moved-archive:" s; next }
{ print "UNKNOWN " $0 }' | sort -u | while read -r name rest; do
    case "$name" in
        UNIT)
            file=$(grep -rl --include='*.cryo' "^namespace $rest;" tests/tests | head -n 1)
            echo "${file#tests/tests/} $reason" ;;
        UNKNOWN)
            echo "UNKNOWN $rest" ;;
        *)
            echo "$name $reason" ;;
    esac
done
