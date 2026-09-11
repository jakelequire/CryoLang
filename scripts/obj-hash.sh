#!/usr/bin/env bash
# Hash every object the build produced, for comparing two trees.
#
# The OBJECTS, never the binary: a Windows PE carries a link timestamp, so two
# builds of one unchanged source produce different `cryo.exe` bytes and the
# same `.o` bytes.  Hashing the binary reports a difference that is only the
# clock.
#
# The count is printed because a find that matches nothing hashes nothing and
# prints nothing, which reads exactly like a tree with no changes.  Compare the
# count against the baseline's before believing a clean diff.
#
# Objects are host-specific - an ELF tree and a PE tree share no object - so a
# baseline is only comparable against the same host.
#
# Run it over a tree built for ONE target.  A checkout that has built both
# hosts keeps the other one's objects in `compiler/build` and a second triple
# under `stdlib/.bin`, and the count comes back several times the real figure -
# 1183 against 406 for one Windows build.  A detached worktree that has only
# ever built one target is the clean instrument.
set -u
cd "$(dirname "$0")/.." || exit 1
find compiler/build stdlib/.bin runtime/.bin -name '*.o' 2>/dev/null \
    | sort | xargs sha256sum
echo "# objects: $(find compiler/build stdlib/.bin runtime/.bin -name '*.o' 2>/dev/null | wc -l)" >&2
