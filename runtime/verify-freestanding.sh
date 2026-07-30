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
#   Linux:   ELF, `-nostartfiles -nostdlib -static -Wl,--gc-sections`, entry `_start`, syscalls.
#            Run natively.
#   Windows: PE, `-nostartfiles -nostdlib -Wl,-e,_start -Wl,--gc-sections`, ntdll (core exit) +
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

# Build <body>, link freestanding against the panic + core archives, run, and
# assert exit 101 with the expected substring on stderr. `abort_a`/`core_a` are
# the caller's locals (bash dynamic scope). Returns 0 on match, 1 on failure.
linux_expect() {  # <body-file> <label> <expect-substr>
    local o; o="$(build_app "$1")"
    "$CRYO_CC" -nostartfiles -nostdlib -static -Wl,--gc-sections "$o" "$abort_a" "$core_a" -o "$WORK/exe"
    set +e; local m; m="$("$WORK/exe" 2>&1 1>/dev/null)"; local g=$?; set -e
    if [ "$g" -eq 101 ] && [[ "$m" == *"$3"* ]]; then echo "linux $2 ok: [$m]"; return 0
    else echo "linux $2 FAIL: exit $g stderr [$m] (want 101 + *$3*)"; return 1; fi
}
windows_expect() {  # <body-file> <label> <expect-substr>
    local mingw="x86_64-w64-mingw32-gcc"; local tgt="x86_64-pc-windows-gnu"
    local o; o="$(build_app "$1" --target="$tgt" --no-incremental)"
    "$mingw" -nostartfiles -nostdlib -Wl,-e,_start -Wl,--gc-sections "$o" "$abort_a" "$core_a" \
        -lkernel32 -lntdll -o "$WORK/app.exe"
    set +e; local m; m="$(WINEDEBUG=-all wine "$WORK/app.exe" 2>&1 1>/dev/null)"; local g=$?; set -e
    if [ "$g" -eq 101 ] && [[ "$m" == *"$3"* ]]; then echo "windows $2 ok: [$m] (wine)"; return 0
    else echo "windows $2 FAIL: exit $g stderr [$m] (want 101 + *$3*)"; return 1; fi
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

# Each check handler: construct a SourceLoc (16-byte {string,u32,u32} POD,
# matching abi/layout.def) and call the handler by its unmangled symbol, passing
# its ADDRESS -- `lang_items.def` specifies SourceLoc by POINTER.
#
# It used to be declared here by VALUE, and that agreed with the tier only on
# Windows: Win64 already passes a 16-byte struct by hidden pointer, while SysV
# splits it across two registers. So the by-value form silently matched on one
# OS and made the tier read `file` as if it were the SourceLoc on the other --
# "panicked at " then a segfault. Passing the address is identical on both.
cat > "$WORK/bounds.cryo" <<'APP'
namespace RtVerifyApp;
type struct SourceLoc { file: string; line: u32; col: u32; }
extern "C" { function __cryo_panic_bounds_check(loc: SourceLoc*, index: u64, len: u64) -> never; }
function main() -> int {
    mut loc: SourceLoc = SourceLoc { file: "verify.cryo", line: 7, col: 3 };
    __cryo_panic_bounds_check(&loc, 5, 3);
    return 0;
}
APP
cat > "$WORK/divzero.cryo" <<'APP'
namespace RtVerifyApp;
type struct SourceLoc { file: string; line: u32; col: u32; }
extern "C" { function __cryo_panic_div_zero(loc: SourceLoc*) -> never; }
function main() -> int {
    mut loc: SourceLoc = SourceLoc { file: "verify.cryo", line: 7, col: 3 };
    __cryo_panic_div_zero(&loc);
    return 0;
}
APP
cat > "$WORK/nomatch.cryo" <<'APP'
namespace RtVerifyApp;
type struct SourceLoc { file: string; line: u32; col: u32; }
extern "C" { function __cryo_panic_no_match(loc: SourceLoc*) -> never; }
function main() -> int {
    mut loc: SourceLoc = SourceLoc { file: "verify.cryo", line: 7, col: 3 };
    __cryo_panic_no_match(&loc);
    return 0;
}
APP
cat > "$WORK/overflow.cryo" <<'APP'
namespace RtVerifyApp;
type struct SourceLoc { file: string; line: u32; col: u32; }
extern "C" { function __cryo_panic_overflow(loc: SourceLoc*, op: u32, lhs: u64, rhs: u64) -> never; }
function main() -> int {
    mut loc: SourceLoc = SourceLoc { file: "verify.cryo", line: 7, col: 3 };
    __cryo_panic_overflow(&loc, 2, 4000000000, 4000000000);
    return 0;
}
APP

# Argument capture: main returns argc, so the process exit code is the argument
# count (argv[0] included). Exercises __cryo_start's capture into the globals.
#
# Content is asserted too, not just the count: on Windows the arguments are
# parsed out of the PEB's single UTF-16 command line, so a correct argc proves
# very little on its own -- the quoting rules and the UTF-16 -> UTF-8 transcode
# are what can actually be wrong. `two words` arrives quoted, so it also pins
# the "quoted argument stays one argument" case on both OSes. A distinct exit
# code per failure mode identifies which argument was wrong.
cat > "$WORK/args.cryo" <<'APP'
namespace RtVerifyApp;
extern "C" {
    function __cryo_argc() -> i64;
    function __cryo_argv() -> i8**;
}
function streq(a: i8*, b: string) -> boolean {
    mut i: i64 = 0;
    while (a[i] == b[i]) {
        if (a[i] == 0) { return true; }
        i = i + 1;
    }
    return false;
}
function main() -> int {
    const n: i64 = __cryo_argc();
    const v: i8** = __cryo_argv();
    if (n != 4) { return 90; }
    if (v == null) { return 94; }
    if (!streq(v[1], "one")) { return 91; }
    if (!streq(v[2], "two words")) { return 92; }
    if (!streq(v[3], "three")) { return 93; }
    return n as int;
}
APP

# Backtrace: a 3-deep call chain into __cryo_backtrace. Built at -O0 (--dev) so
# the chain is not inlined away and the frames are actually present to walk.
cat > "$WORK/bt.cryo" <<'APP'
namespace RtVerifyApp;
extern "C" { function __cryo_backtrace() -> void; }
function bt_level1() -> void { __cryo_backtrace(); }
function bt_level2() -> void { bt_level1(); }
function bt_level3() -> void { bt_level2(); }
function main() -> int { bt_level3(); return 0; }
APP

# Allocator exercise: alloc, verify alignment, write+read a byte pattern, free,
# then alloc/free again to prove the allocator survives a round-trip. Returns 0
# on success; a distinct nonzero code per failure mode for diagnosis.
cat > "$WORK/alloc.cryo" <<'APP'
namespace RtVerifyApp;
extern "C" {
    function __cryo_alloc(size: u64, align: u64) -> u8*;
    function __cryo_dealloc(ptr: u8*, size: u64, align: u64) -> void;
}
function main() -> int {
    const n: u64 = 4096;
    const p: u8* = __cryo_alloc(n, 64);
    if (p == null) { return 1; }
    if ((p as u64) % 64 != 0) { return 2; }
    for (mut i: u64 = 0; i < n; i++) { p[i] = (i % 251) as u8; }
    for (mut i: u64 = 0; i < n; i++) { if (p[i] != (i % 251) as u8) { return 3; } }
    __cryo_dealloc(p, n, 64);
    const q: u8* = __cryo_alloc(64, 8);
    if (q == null) { return 4; }
    if ((q as u64) % 8 != 0) { return 5; }
    q[0] = 42;
    q[63] = 99;
    if (q[0] != 42 || q[63] != 99) { return 6; }
    __cryo_dealloc(q, 64, 8);
    return 0;
}
APP

# The driver's OWN link line. Every check above hand-links, which proves the
# archives are correct but says nothing about whether the compiler can produce a
# freestanding binary on its own -- it could not until the driver learned to
# locate and order the tiers. This project is built by `cryo build` with no
# linker arguments from us at all, so it exercises the locator, the archive
# ORDER (panic tier last, since core/backtrace bounds checks reach it), and the
# system libraries the tiers import. It exits with argc, and returns 70 if the
# allocator tier failed, so one exit code covers entry + alloc + args.
mkdir -p "$WORK/exeapp/src"
cat > "$WORK/exeapp/cryoconfig" <<CFG
[project]
project_name = "rt-verify-exe"
output_dir = ".bin"

[[bin]]
name = "rt-verify-exe"
entry_point = "src/main.cryo"

[compiler]
no_std = true
no_runtime = true
CFG
cat > "$WORK/exeapp/src/main.cryo" <<'APP'
namespace RtVerifyExe;
type struct Box { v: i64; }
extern "C" {
    function __cryo_argc() -> i64;
    function __cryo_backtrace() -> void;
    function __cryo_alloc(size: u64, align: u64) -> u8*;
    function __cryo_dealloc(ptr: u8*, size: u64, align: u64) -> void;
}
function main() -> int {
    __cryo_backtrace();
    const p: u8* = __cryo_alloc(256, 16);
    if (p == null) { return 70; }
    p[0] = 9;
    __cryo_dealloc(p, 256, 16);
    // Language-level heap, which reaches the same lang item through codegen's
    // allocation funnel rather than an explicit extern call. Freestanding, this
    // is the path that used to fail outright: there is no libc `malloc` to fall
    // back to, so every `new` was a codegen error until the funnel learned to
    // route to __cryo_alloc.
    mut b: Box* = new Box { v: 41 };
    if (b.v != 41) { return 71; }
    delete b;
    return __cryo_argc() as int;
}
APP

# --- Linux -----------------------------------------------------------------
run_linux() {
    # Hosted tier rebuilt too: the wipe takes it with the freestanding ones, and
    # every hosted link needs it to resolve `__cryo_panic`.
    ( cd "$RT_DIR" && rm -rf .bin && "$CRYO" build >/dev/null )
    ( cd "$RT_DIR/hosted" && "$CRYO" build >/dev/null )
    local core_a="$RT_DIR/.bin/libcryort-core.a"
    local abort_a="$RT_DIR/.bin/libcryort-panic-abort.a"
    local alloc_a="$RT_DIR/.bin/libcryort-alloc.a"
    local bt_a="$RT_DIR/.bin/libcryort-backtrace.a"

    # No-libc guarantee: every link below is `-nostdlib` (+ freestanding), so a
    # stray libc dependency would fail to resolve at link time. The link
    # succeeding IS the freestanding check. (Cross-tier Sys refs are weak and
    # resolved intra-archive, so a naive `nm -u` over-reports them.)

    local fail=0

    # core: main -> exit code round-trip.
    for expect in 0 7 42 200; do
        printf 'namespace RtVerifyApp;\nfunction main() -> int { return %s; }\n' "$expect" > "$WORK/ret.cryo"
        local o; o="$(build_app "$WORK/ret.cryo")"
        "$CRYO_CC" -nostartfiles -nostdlib -static -Wl,--gc-sections "$o" "$core_a" "$abort_a" -o "$WORK/exe"
        set +e; "$WORK/exe"; local got=$?; set -e
        if [ "$got" -eq "$expect" ]; then echo "linux core ok: main $expect -> exit $got"
        else echo "linux core FAIL: main $expect -> exit $got"; fail=1; fi
    done

    # panic: message to stderr + exit 101.
    local o; o="$(build_app "$WORK/panic.cryo")"
    "$CRYO_CC" -nostartfiles -nostdlib -static -Wl,--gc-sections "$o" "$abort_a" "$core_a" -o "$WORK/exe"
    set +e; local msg; msg="$("$WORK/exe" 2>&1 1>/dev/null)"; local got=$?; set -e
    [ "$got" -eq 101 ] || { echo "linux panic FAIL: exit $got (want 101)"; fail=1; }
    [ "$msg" = "$PANIC_MSG" ] || { echo "linux panic FAIL: stderr [$msg] (want [$PANIC_MSG])"; fail=1; }
    [ "$got" -eq 101 ] && [ "$msg" = "$PANIC_MSG" ] && echo "linux panic ok: exit 101, stderr [$msg]"

    # check handlers: raw operands + SourceLoc, formatted through the funnel.
    linux_expect "$WORK/bounds.cryo"   "bounds"   "panicked at verify.cryo:7: index 5 out of bounds for length 3" || fail=1
    linux_expect "$WORK/divzero.cryo"  "div_zero" "panicked at verify.cryo:7: attempt to divide by zero" || fail=1
    linux_expect "$WORK/nomatch.cryo"  "no_match" "panicked at verify.cryo:7: no matching pattern" || fail=1
    linux_expect "$WORK/overflow.cryo" "overflow" "panicked at verify.cryo:7: arithmetic overflow (op 2): 4000000000, 4000000000" || fail=1

    # alloc: exit 0 means alloc/align/write-read/free/re-alloc all held.
    local ao; ao="$(build_app "$WORK/alloc.cryo")"
    "$CRYO_CC" -nostartfiles -nostdlib -static -Wl,--gc-sections "$ao" "$alloc_a" "$core_a" "$abort_a" -o "$WORK/exe"
    set +e; "$WORK/exe"; local ag=$?; set -e
    if [ "$ag" -eq 0 ]; then echo "linux alloc ok: mmap alloc/free round-trip"
    else echo "linux alloc FAIL: exit $ag (0=ok; 1-6 = failure mode)"; fail=1; fi

    # backtrace: "stack backtrace:" + >=3 frame addresses from the call chain.
    local bo; bo="$(build_app "$WORK/bt.cryo" --dev)"
    # The panic tier is on the line because the backtrace tier's own formatting
    # helpers carry bounds checks, and a bounds check resolves through
    # __cryo_panic_bounds_check -- exactly the "strategy chosen by which archive
    # is linked" contract. Any tier containing checked code needs one.
    "$CRYO_CC" -nostartfiles -nostdlib -static -Wl,--gc-sections "$bo" "$bt_a" "$abort_a" "$core_a" -o "$WORK/exe"
    set +e; local bmsg; bmsg="$("$WORK/exe" 2>&1 1>/dev/null)"; local bg=$?; set -e
    local bframes; bframes="$(printf '%s\n' "$bmsg" | grep -c '0x')"
    if [ "$bg" -eq 0 ] && printf '%s' "$bmsg" | grep -q 'stack backtrace:' && [ "$bframes" -ge 3 ]; then
        echo "linux backtrace ok: $bframes frames"
    else echo "linux backtrace FAIL: exit $bg frames $bframes [$bmsg]"; fail=1; fi

    # args: exit code == argc. Run with 3 extra args -> argc 4 (argv[0] + 3).
    local go; go="$(build_app "$WORK/args.cryo")"
    "$CRYO_CC" -nostartfiles -nostdlib -static -Wl,--gc-sections "$go" "$core_a" "$abort_a" -o "$WORK/exe"
    set +e; "$WORK/exe" one "two words" three; local gg=$?; set -e
    if [ "$gg" -eq 4 ]; then echo "linux args ok: argc 4 + argv contents match"
    else echo "linux args FAIL: exit $gg (want 4; 90=argc 91-93=argv[n] 94=null)"; fail=1; fi

    # driver-link: no linker arguments from us — `cryo build` alone.
    set +e
    ( cd "$WORK/exeapp" && rm -rf .bin && "$CRYO" build ) > "$WORK/drv-linux.log" 2>&1
    "$WORK/exeapp/.bin/rt-verify-exe" one two; local dg=$?
    set -e
    if [ "$dg" -eq 3 ]; then echo "linux driver-link ok: cryo build produced a running freestanding exe"
    else
        echo "linux driver-link FAIL: exit $dg (want 3; 70=alloc)"
        sed 's/^/    | /' "$WORK/drv-linux.log" | tail -15
        fail=1
    fi

    return $fail
}

# --- Windows (cross + wine) ------------------------------------------------
run_windows() {
    local mingw="x86_64-w64-mingw32-gcc"
    if [ "${FREESTANDING_LINUX_ONLY:-0}" != "0" ]; then
        echo "windows: SKIPPED (FREESTANDING_LINUX_ONLY set)"; return 0
    fi
    if ! command -v "$mingw" >/dev/null || ! command -v wine >/dev/null; then
        echo "windows: SKIPPED (need $mingw and wine)"; return 0
    fi
    local tgt="x86_64-pc-windows-gnu"
    # The `rm -rf .bin` also removes the hosted abort tier, which lives in the
    # same directory and which every HOSTED link needs to resolve `__cryo_panic`.
    # Rebuild it alongside so this gate leaves a tree that can still link an
    # ordinary program, not just a freestanding one.
    ( cd "$RT_DIR" && rm -rf .bin && "$CRYO" build --target="$tgt" --no-incremental >/dev/null )
    ( cd "$RT_DIR/hosted" && "$CRYO" build --target="$tgt" --no-incremental >/dev/null )
    local core_a="$RT_DIR/.bin/libcryort-core.a"
    local abort_a="$RT_DIR/.bin/libcryort-panic-abort.a"
    local alloc_a="$RT_DIR/.bin/libcryort-alloc.a"
    local bt_a="$RT_DIR/.bin/libcryort-backtrace.a"

    local fail=0

    # core: main -> exit code round-trip.
    for expect in 0 7 42 200; do
        printf 'namespace RtVerifyApp;\nfunction main() -> int { return %s; }\n' "$expect" > "$WORK/ret.cryo"
        local o; o="$(build_app "$WORK/ret.cryo" --target="$tgt" --no-incremental)"
        "$mingw" -nostartfiles -nostdlib -Wl,-e,_start -Wl,--gc-sections "$o" "$core_a" "$abort_a" -lkernel32 -lntdll -o "$WORK/app.exe"
        set +e; WINEDEBUG=-all wine "$WORK/app.exe" 2>/dev/null; local got=$?; set -e
        if [ "$got" -eq "$expect" ]; then echo "windows core ok: main $expect -> exit $got (wine)"
        else echo "windows core FAIL: main $expect -> exit $got (wine)"; fail=1; fi
    done

    # panic: message to stderr + exit 101.
    local o; o="$(build_app "$WORK/panic.cryo" --target="$tgt" --no-incremental)"
    "$mingw" -nostartfiles -nostdlib -Wl,-e,_start -Wl,--gc-sections "$o" "$abort_a" "$core_a" \
        -lkernel32 -lntdll -o "$WORK/app.exe"
    set +e; local msg; msg="$(WINEDEBUG=-all wine "$WORK/app.exe" 2>&1 1>/dev/null)"; local got=$?; set -e
    [ "$got" -eq 101 ] || { echo "windows panic FAIL: exit $got (want 101)"; fail=1; }
    case "$msg" in
        *"$PANIC_MSG"*) [ "$got" -eq 101 ] && echo "windows panic ok: exit 101 (wine), stderr contains message" ;;
        *) echo "windows panic FAIL: stderr missing [$PANIC_MSG]; got [$msg]"; fail=1 ;;
    esac

    # A check handler cross-OS: exercises the SourceLoc by-value ABI + the
    # message-builder path under Win64 (indirect struct passing).
    windows_expect "$WORK/bounds.cryo" "bounds" "index 5 out of bounds for length 3" || fail=1

    # alloc: VirtualAlloc/VirtualFree round-trip under wine.
    local ao; ao="$(build_app "$WORK/alloc.cryo" --target="$tgt" --no-incremental)"
    "$mingw" -nostartfiles -nostdlib -Wl,-e,_start -Wl,--gc-sections "$ao" "$alloc_a" "$core_a" "$abort_a" \
        -lkernel32 -lntdll -o "$WORK/app.exe"
    set +e; WINEDEBUG=-all wine "$WORK/app.exe" 2>/dev/null; local ag=$?; set -e
    if [ "$ag" -eq 0 ]; then echo "windows alloc ok: VirtualAlloc/VirtualFree round-trip (wine)"
    else echo "windows alloc FAIL: exit $ag (0=ok; 1-6 = failure mode)"; fail=1; fi

    # backtrace: same assertion as Linux — ">=3 frames" is what proves the walk
    # actually unwinds rather than just printing a header. Windows gets there by
    # a bounded .pdata/RtlVirtualUnwind walk, since Win64 omits the rbp chain
    # the Linux walk follows. Delegating to RtlCaptureStackBackTrace is NOT an
    # option here: from a freestanding image it runs away and exhausts the whole
    # stack (the TEB is fine; the image carries no language exception handler,
    # so a fault inside the unwinder cannot be dispatched).
    local bo; bo="$(build_app "$WORK/bt.cryo" --dev --target="$tgt" --no-incremental)"
    "$mingw" -nostartfiles -nostdlib -Wl,-e,_start -Wl,--gc-sections "$bo" "$bt_a" "$abort_a" "$core_a" \
        -lkernel32 -lntdll -o "$WORK/app.exe"
    set +e; local bmsg; bmsg="$(WINEDEBUG=-all wine "$WORK/app.exe" 2>&1 1>/dev/null)"; local bg=$?; set -e
    local bframes; bframes="$(printf '%s\n' "$bmsg" | grep -c '0x')"
    if [ "$bg" -eq 0 ] && printf '%s' "$bmsg" | grep -q 'stack backtrace:' && [ "$bframes" -ge 3 ]; then
        echo "windows backtrace ok: $bframes frames"
    else echo "windows backtrace FAIL: exit $bg frames $bframes [$bmsg]"; fail=1; fi

    # args: parsed out of the PEB command line -- same assertion as Linux.
    local go; go="$(build_app "$WORK/args.cryo" --target="$tgt" --no-incremental)"
    "$mingw" -nostartfiles -nostdlib -Wl,-e,_start -Wl,--gc-sections "$go" "$core_a" "$abort_a" -lkernel32 -lntdll -o "$WORK/app.exe"
    set +e; WINEDEBUG=-all wine "$WORK/app.exe" one "two words" three 2>/dev/null; local gg=$?; set -e
    if [ "$gg" -eq 4 ]; then echo "windows args ok: argc 4 + argv contents match (wine)"
    else echo "windows args FAIL: exit $gg (want 4; 90=argc 91-93=argv[n] 94=null)"; fail=1; fi

    # driver-link: no linker arguments from us — `cryo build` alone.
    #
    # CRYO_CC is dropped for this one invocation. It is exported at the top of
    # this script as the driver for the HAND-links, which are host-native; but
    # it overrides the C driver for every target, so leaving it set would point
    # a windows cross-link at host gcc and fail on `cannot find -lkernel32`.
    # Choosing the per-target driver is part of what this check verifies, so it
    # has to be the compiler's choice and not ours.
    set +e
    ( cd "$WORK/exeapp" && rm -rf .bin && env -u CRYO_CC "$CRYO" build --target="$tgt" --no-incremental ) > "$WORK/drv-win.log" 2>&1
    WINEDEBUG=-all wine "$WORK/exeapp/.bin/rt-verify-exe.exe" one two 2>/dev/null; local dg=$?
    set -e
    if [ "$dg" -eq 3 ]; then echo "windows driver-link ok: cryo build produced a running freestanding exe (wine)"
    else
        echo "windows driver-link FAIL: exit $dg (want 3; 70=alloc)"
        sed 's/^/    | /' "$WORK/drv-win.log" | tail -15
        fail=1
    fi

    return $fail
}

run_linux   || overall=1
run_windows || overall=1

[ "$overall" -eq 0 ] && echo "verify-freestanding: OK" || { echo "verify-freestanding: FAILED"; exit 1; }
