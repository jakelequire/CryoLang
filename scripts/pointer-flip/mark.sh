#!/bin/sh
# Print an AUDIT line, in the audit compiler's format, for each
# "file|line|prefix|type" argument: the pointer type `type` written right
# after `prefix` on that line.  For the writes the audit cannot trace to a
# declaration (a generic method reached through an instance, a global).
for spec in "$@"; do
  file=$(echo "$spec" | cut -d'|' -f1)
  line=$(echo "$spec" | cut -d'|' -f2)
  prefix=$(echo "$spec" | cut -d'|' -f3)
  type=$(echo "$spec" | cut -d'|' -f4)
  text=$(sed -n "${line}p" "$file")
  awk -v t="$text" -v p="$prefix$type" -v pl="${#prefix}" -v tl="${#type}" -v f="$file" -v l="$line" 'BEGIN {
    i = index(t, p);
    if (i == 0) { print "NOTFOUND " f ":" l " " p; exit }
    c = i + pl;
    printf "AUDIT\t%s\t%d\t%d\t%d\t%d\n", f, l, c, l, c + tl
  }'
done
