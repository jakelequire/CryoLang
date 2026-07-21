#!/usr/bin/env bash
# Acceptance check for the freestanding runtime core, on both supported OSes.
# For each target: build libcryort-core.a, build a throwaway user `main`, link
# them with the freestanding profile, run, and confirm main's return value
# round-trips as the process exit code through the runtime entry.
#
#   Linux:   ELF, `-nostartfiles -nostdlib -static`, entry `_start`, exit via
#            the exit_group syscall. Run natively.
#   Windows: PE, `-nostartfiles -nostdlib -Wl,-e,_start -lntdll`, exit via
#            ntdll NtTerminateProcess. Run under wine. Skipped when the mingw
#            toolchain or wine is unavailable.
#
# The link step is done by hand because the driver does not yet order tier
# archives onto a user link line (future runtime-driver work); this script is
# the standing proof that the archive itself is correct on each OS.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CRYO="${CRYO:-$REPO/compiler/build/cryo}"
export CRYO_STDLIB="${CRYO_STDLIB:-$REPO/stdlib}"
export CRYO_CC="${CRYO_CC:-gcc}"
CORE_DIR="$REPO/runtime/core"

[ -x "$CRYO" ] || { echo "no compiler at $CRYO (run 'make cryo')"; exit 1; }

WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/app/src"
cat > "$WORK/app/cryoconfig" <<CFG
[project]
project_name = "rt-verify-app"
output_dir = ".bin"
target_type = "library"
source_dir = "src"
[compiler]
no_std = true
no_runtime = true
CFG

overall=0

# build_app <target-flags...> ; emits the user object path on stdout
build_app() {
    ( cd "$WORK/app" && rm -rf .bin && "$CRYO" build "$@" >/dev/null )
    find "$WORK/app" -name '*.o' | head -1
}

# --- Linux -----------------------------------------------------------------
run_linux() {
    ( cd "$CORE_DIR" && rm -rf .bin && "$CRYO" build >/dev/null )
    local core_a="$CORE_DIR/.bin/libcryort-core.a"
    local undef; undef="$(nm -u "$core_a" 2>/dev/null | sed -n 's/.* U //p' | sort -u || true)"
    [ "$undef" = "main" ] || { echo "linux FAIL: core has unexpected undefined syms: $undef"; return 1; }

    local fail=0
    for expect in 0 7 42 200; do
        printf 'namespace RtVerifyApp;\nfunction main() -> int { return %s; }\n' "$expect" > "$WORK/app/src/lib.cryo"
        local o; o="$(build_app)"
        "$CRYO_CC" -nostartfiles -nostdlib -static "$o" "$core_a" -o "$WORK/exe"
        set +e; "$WORK/exe"; local got=$?; set -e
        if [ "$got" -eq "$expect" ]; then echo "linux ok: main $expect -> exit $got"
        else echo "linux FAIL: main $expect -> exit $got"; fail=1; fi
    done
    return $fail
}

# --- Windows (cross + wine) ------------------------------------------------
run_windows() {
    local mingw="x86_64-w64-mingw32-gcc"
    if ! command -v "$mingw" >/dev/null || ! command -v wine >/dev/null; then
        echo "windows: SKIPPED (need $mingw and wine)"; return 0
    fi
    local tgt="x86_64-pc-windows-gnu"
    ( cd "$CORE_DIR" && rm -rf .bin && "$CRYO" build --target="$tgt" --no-incremental >/dev/null )
    local core_a="$CORE_DIR/.bin/libcryort-core.a"
    # Expect only main + NtTerminateProcess undefined (ntdll import).
    local undef; undef="$("$mingw"-nm 2>/dev/null "$core_a" | sed -n 's/.* U //p' | sort -u | tr '\n' ' ' || true)"
    case "$undef" in
        *main*NtTerminateProcess*|*NtTerminateProcess*main*) : ;;
        *) echo "windows FAIL: core has unexpected undefined syms: $undef"; return 1 ;;
    esac

    local fail=0
    for expect in 0 7 42 200; do
        printf 'namespace RtVerifyApp;\nfunction main() -> int { return %s; }\n' "$expect" > "$WORK/app/src/lib.cryo"
        local o; o="$(build_app --target="$tgt" --no-incremental)"
        "$mingw" -nostartfiles -nostdlib -Wl,-e,_start "$o" "$core_a" -lntdll -o "$WORK/app.exe"
        set +e; WINEDEBUG=-all wine "$WORK/app.exe" 2>/dev/null; local got=$?; set -e
        if [ "$got" -eq "$expect" ]; then echo "windows ok: main $expect -> exit $got (wine)"
        else echo "windows FAIL: main $expect -> exit $got (wine)"; fail=1; fi
    done
    return $fail
}

run_linux   || overall=1
run_windows || overall=1

[ "$overall" -eq 0 ] && echo "verify-freestanding: OK" || { echo "verify-freestanding: FAILED"; exit 1; }
