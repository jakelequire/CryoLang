#!/usr/bin/env bash
# release-smoke.sh - compile and run a program with a STAGED release tree.
#
# Nothing executed a release artifact before it was published.  release.yml
# builds the tarball, uploads it, and attaches it to the GitHub Release; the
# verify job beside it re-runs the test suite on the tagged commit, which
# builds a DIFFERENT binary by a different link - `make cryo` is not
# `--release-static`.  The one place an artifact does get run, the
# `windows-smoke` job in ci.yml, asks it for `--version` and `--help`: whether
# it starts, not whether it works, and on `main` rather than on the tag.
#
# So the archive's own reason for existing was ungated.  It ships a stdlib and
# the runtime tier archives because without them, in build-release.sh's own
# words, the compiler "cannot link ANY program" - it fails on an undefined
# `__cryo_panic` - and a missing hosted CORE tier is worse than that: the link
# quietly omits `-Wl,-e,__cryo_entry` and the program goes back to starting at
# crt0. Neither shows up in `--version`.
#
# This compiles a hello-world with the staged compiler, against the staged
# stdlib and the staged tiers, runs it, and checks its output and exit code.
# It is the smallest program that needs every piece the archive ships.
#
# Usage:
#   scripts/release-smoke.sh <staged-dir> <exe-name> [runner ...]
#
#   <staged-dir>  a staging tree: bin/<exe>, stdlib/, lib/cryo/<triple>/
#   <exe-name>    `cryo` or `cryo.exe`
#   [runner]      how to execute a foreign binary, e.g. `wine`.  Omit when the
#                 artifact is native to this host.
#
# Exit: 0 the program compiled, ran, and said what it should; 1 otherwise.
set -uo pipefail

if [ "$#" -lt 2 ]; then
    echo "usage: release-smoke.sh <staged-dir> <exe-name> [runner ...]" >&2
    exit 2
fi
STAGE="$1"; EXE="$2"; shift 2
RUNNER=("$@")

# Absolute before anything chdirs.  The build runs with the cwd set to a temp
# project, so a relative compiler or stdlib path silently stops existing there
# and the failure reads as "the compiler could not build a hello-world".
STAGE="$(cd "$STAGE" 2>/dev/null && pwd)" || { echo "release-smoke: FAIL -- no such staging dir" >&2; exit 1; }

fail() { echo "release-smoke: FAIL -- $*" >&2; exit 1; }

CRYO="${STAGE}/bin/${EXE}"
[ -f "$CRYO" ] || fail "no compiler at ${CRYO}"
[ -d "${STAGE}/stdlib" ] || fail "the archive ships no stdlib/; nothing could compile"
ls "${STAGE}"/lib/cryo/*/libcryort-*.a >/dev/null 2>&1 \
    || fail "the archive ships no runtime tier archives; every link would fail on __cryo_panic"

WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/app/src"
cat > "$WORK/app/cryoconfig" <<'CFG'
[project]
project_name = "release-smoke"
output_dir = "build"
target_type = "executable"
source_dir = "src"
entry_point = "src/main.cryo"

[compiler]

[dependencies]
CFG
cat > "$WORK/app/src/main.cryo" <<'APP'
namespace ReleaseSmoke;

import std::ffi::libc;

function main() -> int {
    libc::printf("release-smoke ok\n");
    return 7;
}
APP

# The stdlib comes from the ARCHIVE, not from a checkout.  Without this the
# staged compiler could resolve against the repository it was built in and the
# smoke would pass on a machine where the shipped stdlib is missing entirely.
export CRYO_STDLIB="${STAGE}/stdlib"

echo "release-smoke: building a hello-world with ${CRYO}"
if ! ( cd "$WORK/app" && "${RUNNER[@]}" "$CRYO" build > "$WORK/build.log" 2>&1 ); then
    echo "--- build output ---" >&2
    tail -30 "$WORK/build.log" >&2
    fail "the staged compiler could not build a hello-world"
fi

BIN="$WORK/app/build/release-smoke.exe"
[ -f "$BIN" ] || BIN="$WORK/app/build/release-smoke"
[ -f "$BIN" ] || fail "the build reported success but produced no binary"

set +e
OUT="$("${RUNNER[@]}" "$BIN" 2>&1)"
CODE=$?
set -e

case "$OUT" in
    *"release-smoke ok"*) ;;
    *) fail "the program ran but printed [$OUT]" ;;
esac
# 7 rather than 0: an exit code the runtime has to carry back from `main`, so a
# core tier that dropped the entry shim cannot pass by exiting 0 for its own
# reasons.
[ "$CODE" -eq 7 ] || fail "the program exited ${CODE}, expected 7 (the entry did not carry main's return)"

echo "release-smoke: OK -- built, ran, printed and exited 7 using only the staged tree"
