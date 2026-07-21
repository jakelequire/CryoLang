#!/usr/bin/env bash
# Acceptance check for the freestanding runtime workspace, on both supported
# OSes. Builds every tier once via the workspace `cryoconfig` (one `cryo build`
# → .bin/libcryort-*.a), then exercises two properties per target:
#
#   core   — a user `main` returning N, linked against libcryort-core.a alone,
#            exits with N (the entry round-trips main's return as exit code).
#   panic  — a user `main` calling __cryo_panic, linked against the panic-abort
#            tier + core, prints "panicked at ..." to stderr and exits 101.
#
#   Linux:   ELF, `-nostartfiles -nostdlib -static`, entry `_start`, syscalls.
#            Run natively.
#   Windows: PE, `-nostartfiles -nostdlib -Wl,-e,_start`, ntdll (core exit) +
#            kernel32 (panic stderr). Run under wine. Skipped when the mingw
#            toolchain or wine is unavailable.
#
# The link step is done by hand because the driver does not yet order tier
# archives onto a user link line (future runtime-driver work); this script is
# the standing proof that the archives are correct on each OS.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CRYO="${CRYO:-$REPO/compiler/build/cryo}"
export CRYO_STDLIB="${CRYO_STDLIB:-$REPO/stdlib}"
export CRYO_CC="${CRYO_CC:-gcc}"
RT_DIR="$REPO/runtime"

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

PANIC_MSG="panicked at verify.cryo:42: boom"
overall=0

# Build one throwaway user object from a given lib.cryo body. Emits the .o path.
build_app() {  # <lib.cryo-body-file> <target-flags...>
    local body="$1"; shift
    cp "$body" "$WORK/app/src/lib.cryo"
    ( cd "$WORK/app" && rm -rf .bin && "$CRYO" build "$@" >/dev/null )
    find "$WORK/app" -name '*.o' | head -1
}

# main returning a value — exercises the core entry's exit-code round-trip.
cat > "$WORK/ret.cryo" <<'APP'
namespace RtVerifyApp;
function main() -> int { return 42; }
APP
# main calling the panic funnel by its unmangled symbol.
cat > "$WORK/panic.cryo" <<'APP'
namespace RtVerifyApp;
extern "C" {
    function __cryo_panic(msg: string, file: string, line: u32) -> never;
}
function main() -> int {
    __cryo_panic("boom", "verify.cryo", 42);
    return 0;
}
APP

# --- Linux -----------------------------------------------------------------
run_linux() {
    ( cd "$RT_DIR" && rm -rf .bin && "$CRYO" build >/dev/null )
    local core_a="$RT_DIR/.bin/libcryort-core.a"
    local abort_a="$RT_DIR/.bin/libcryort-panic-abort.a"

    # The panic tier must reference no libc — fully freestanding.
    local undef; undef="$(nm -u "$abort_a" 2>/dev/null | sed -n 's/.* U //p' | sort -u | tr '\n' ' ' || true)"
    [ -z "${undef// }" ] || { echo "linux FAIL: panic tier has undefined syms: $undef"; return 1; }

    local fail=0

    # core: main -> exit code round-trip.
    for expect in 0 7 42 200; do
        printf 'namespace RtVerifyApp;\nfunction main() -> int { return %s; }\n' "$expect" > "$WORK/ret.cryo"
        local o; o="$(build_app "$WORK/ret.cryo")"
        "$CRYO_CC" -nostartfiles -nostdlib -static "$o" "$core_a" -o "$WORK/exe"
        set +e; "$WORK/exe"; local got=$?; set -e
        if [ "$got" -eq "$expect" ]; then echo "linux core ok: main $expect -> exit $got"
        else echo "linux core FAIL: main $expect -> exit $got"; fail=1; fi
    done

    # panic: message to stderr + exit 101.
    local o; o="$(build_app "$WORK/panic.cryo")"
    "$CRYO_CC" -nostartfiles -nostdlib -static "$o" "$abort_a" "$core_a" -o "$WORK/exe"
    set +e; local msg; msg="$("$WORK/exe" 2>&1 1>/dev/null)"; local got=$?; set -e
    [ "$got" -eq 101 ] || { echo "linux panic FAIL: exit $got (want 101)"; fail=1; }
    [ "$msg" = "$PANIC_MSG" ] || { echo "linux panic FAIL: stderr [$msg] (want [$PANIC_MSG])"; fail=1; }
    [ "$got" -eq 101 ] && [ "$msg" = "$PANIC_MSG" ] && echo "linux panic ok: exit 101, stderr [$msg]"

    return $fail
}

# --- Windows (cross + wine) ------------------------------------------------
run_windows() {
    local mingw="x86_64-w64-mingw32-gcc"
    if ! command -v "$mingw" >/dev/null || ! command -v wine >/dev/null; then
        echo "windows: SKIPPED (need $mingw and wine)"; return 0
    fi
    local tgt="x86_64-pc-windows-gnu"
    ( cd "$RT_DIR" && rm -rf .bin && "$CRYO" build --target="$tgt" --no-incremental >/dev/null )
    local core_a="$RT_DIR/.bin/libcryort-core.a"
    local abort_a="$RT_DIR/.bin/libcryort-panic-abort.a"

    local fail=0

    # core: main -> exit code round-trip.
    for expect in 0 7 42 200; do
        printf 'namespace RtVerifyApp;\nfunction main() -> int { return %s; }\n' "$expect" > "$WORK/ret.cryo"
        local o; o="$(build_app "$WORK/ret.cryo" --target="$tgt" --no-incremental)"
        "$mingw" -nostartfiles -nostdlib -Wl,-e,_start "$o" "$core_a" -lntdll -o "$WORK/app.exe"
        set +e; WINEDEBUG=-all wine "$WORK/app.exe" 2>/dev/null; local got=$?; set -e
        if [ "$got" -eq "$expect" ]; then echo "windows core ok: main $expect -> exit $got (wine)"
        else echo "windows core FAIL: main $expect -> exit $got (wine)"; fail=1; fi
    done

    # panic: message to stderr + exit 101.
    local o; o="$(build_app "$WORK/panic.cryo" --target="$tgt" --no-incremental)"
    "$mingw" -nostartfiles -nostdlib -Wl,-e,_start "$o" "$abort_a" "$core_a" \
        -lkernel32 -lntdll -o "$WORK/app.exe"
    set +e; local msg; msg="$(WINEDEBUG=-all wine "$WORK/app.exe" 2>&1 1>/dev/null)"; local got=$?; set -e
    [ "$got" -eq 101 ] || { echo "windows panic FAIL: exit $got (want 101)"; fail=1; }
    case "$msg" in
        *"$PANIC_MSG"*) [ "$got" -eq 101 ] && echo "windows panic ok: exit 101 (wine), stderr contains message" ;;
        *) echo "windows panic FAIL: stderr missing [$PANIC_MSG]; got [$msg]"; fail=1 ;;
    esac

    return $fail
}

run_linux   || overall=1
run_windows || overall=1

[ "$overall" -eq 0 ] && echo "verify-freestanding: OK" || { echo "verify-freestanding: FAILED"; exit 1; }
