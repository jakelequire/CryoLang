// C-side helpers for the Cryo ABI test suite.  Compiled with the host
// toolchain to a static archive (`libabihelpers.a`) by the top-level
// Makefile, then linked into the test executable via cryoconfig's
// `link_paths` + `link_libs` settings.  Every function here exercises
// a specific SysV x86-64 calling-convention shape so that the Cryo
// extern declaration's lowering can be verified against what a real
// C compiler emits for the same signature.
//
// Keep this file dependency-free (no system headers beyond <stdint.h>)
// so the cross-toolchain build stays trivially portable — the tests
// only run on Linux x86-64 anyway, which is what we target.

#include <stdint.h>


// ----- §3.1 DirectPair returns (9–16 byte aggregates) ----------------------

// 12-byte struct (int * 3).  Returns as `{i64, i32}` under SysV.
typedef struct ThreeI32 { int32_t a, b, c; } ThreeI32;

ThreeI32 abi_make_three_i32(int32_t a, int32_t b, int32_t c) {
    ThreeI32 s = { a, b, c };
    return s;
}


// ----- §3.2 small-aggregate extern-C params --------------------------------

// 8-byte struct (int * 2).  Passed by value in a single INTEGER eightbyte.
typedef struct TwoI32 { int32_t a, b; } TwoI32;

int32_t abi_sum_two_i32(TwoI32 s) {
    return s.a + s.b;
}

// 12-byte struct (int * 3).  Passed by value as two INTEGER eightbytes
// (DirectPair on the param side).
int32_t abi_sum_three_i32(ThreeI32 s) {
    return s.a + s.b + s.c;
}


// ----- §3.3 SSE eightbyte ---------------------------------------------------

// All-double 16-byte struct.  Under SysV, both eightbytes are SSE class
// — the return value rides %xmm0:%xmm1 and the LLVM lowering should
// produce `{double, double}`, NOT `{i64, i64}`.
typedef struct TwoDoubles { double a, b; } TwoDoubles;

TwoDoubles abi_make_two_doubles(double a, double b) {
    TwoDoubles s = { a, b };
    return s;
}

double abi_sum_two_doubles(TwoDoubles s) {
    return s.a + s.b;
}
