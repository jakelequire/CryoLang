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

// 8-byte struct of two `float`s.  Both fields share a single eightbyte
// and SysV packs them into one SSE register as `<2 x float>` rather
// than two scalar SSE slots.  Exercises the multi-float SSE bucket in
// `eightbyte_slot_type`; the integer fallback would land the bytes on
// %rax and the test would read garbage out of %xmm0.
typedef struct TwoFloats { float a, b; } TwoFloats;

TwoFloats abi_make_two_floats(float a, float b) {
    TwoFloats s = { a, b };
    return s;
}

float abi_sum_two_floats(TwoFloats s) {
    return s.a + s.b;
}


// ----- §3.3 SSE eightbyte: floats nested inside member aggregates ----------
//
// These exercise the *recursive* SysV eightbyte classification: a float
// buried inside a nested struct / array still makes its eightbyte SSE
// class.  A direct-fields-only classifier wrongly demotes them to
// INTEGER, so Cryo would pass/return them in GP registers while clang
// (here) uses XMM — and both fields read back garbage.

// 16-byte struct whose first eightbyte is a *nested* struct holding one
// double, second eightbyte a plain double.  Both eightbytes are SSE.
typedef struct InnerD { double v; } InnerD;
typedef struct OuterD { InnerD inner; double y; } OuterD;

OuterD abi_make_outer_d(double a, double b) {
    OuterD s; s.inner.v = a; s.y = b;
    return s;
}

double abi_sum_outer_d(OuterD s) {
    return s.inner.v + s.y;
}

// 4-byte struct wrapping a nested single-float struct.  The whole value
// is one SSE eightbyte (a single `float`) rolling in %xmm0.
typedef struct InnerF { float v; } InnerF;
typedef struct WrapF  { InnerF inner; } WrapF;

WrapF abi_make_wrap_f(float a) {
    WrapF s; s.inner.v = a;
    return s;
}

float abi_wrap_f_get(WrapF s) {
    return s.inner.v;
}

// 8-byte struct holding a fixed array of two floats.  Both floats share
// one eightbyte → `<2 x float>` SSE, exercising the array branch of the
// recursive leaf walk.
typedef struct ArrF2 { float v[2]; } ArrF2;

ArrF2 abi_make_arr_f2(float a, float b) {
    ArrF2 s; s.v[0] = a; s.v[1] = b;
    return s;
}

float abi_sum_arr_f2(ArrF2 s) {
    return s.v[0] + s.v[1];
}
