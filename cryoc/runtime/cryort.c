// cryort.c — Cryo runtime support library.
//
// Provides C-linkage definitions for the intrinsics declared in
// stdlib/core/intrinsics.cryo that the C++ bootstrap inline-lowers but the
// self-hosted cryoc currently emits as plain function calls.  Linking this
// object alongside cryoc-emitted objects resolves the bare-name references
// (i32_to_i64, ptr_add, format, etc.).
//
// Long term, cryoc should match the bootstrap and inline-lower these in
// codegen; this file is a stop-gap until that work lands.

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <dirent.h>

// ---------------------------------------------------------------------------
// Pointer arithmetic
// ---------------------------------------------------------------------------

void *ptr_add(void *p, int64_t off) { return (char *)p + off; }
void *ptr_sub(void *p, int64_t off) { return (char *)p - off; }
int64_t ptr_diff(void *a, void *b) { return (int64_t)((char *)a - (char *)b); }

// ---------------------------------------------------------------------------
// Allocating format — Cryo-side replacement for sprintf with auto-malloc.
// Returns a heap-allocated null-terminated string; caller owns it.
// ---------------------------------------------------------------------------

char *format(const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) {
        va_end(ap2);
        return NULL;
    }
    char *buf = (char *)malloc((size_t)n + 1);
    if (buf != NULL) {
        vsnprintf(buf, (size_t)n + 1, fmt, ap2);
    }
    va_end(ap2);
    return buf;
}

// ---------------------------------------------------------------------------
// Signed integer widening / narrowing
// ---------------------------------------------------------------------------

int16_t i8_to_i16(int8_t x)   { return (int16_t)x; }
int32_t i8_to_i32(int8_t x)   { return (int32_t)x; }
int64_t i8_to_i64(int8_t x)   { return (int64_t)x; }
int32_t i16_to_i32(int16_t x) { return (int32_t)x; }
int64_t i16_to_i64(int16_t x) { return (int64_t)x; }
int64_t i32_to_i64(int32_t x) { return (int64_t)x; }

int32_t i64_to_i32(int64_t x) { return (int32_t)x; }
int16_t i64_to_i16(int64_t x) { return (int16_t)x; }
int8_t  i64_to_i8(int64_t x)  { return (int8_t)x; }
int16_t i32_to_i16(int32_t x) { return (int16_t)x; }
int8_t  i32_to_i8(int32_t x)  { return (int8_t)x; }
int8_t  i16_to_i8(int16_t x)  { return (int8_t)x; }

// ---------------------------------------------------------------------------
// Unsigned integer widening / narrowing
// ---------------------------------------------------------------------------

uint16_t u8_to_u16(uint8_t x)   { return (uint16_t)x; }
uint32_t u8_to_u32(uint8_t x)   { return (uint32_t)x; }
uint64_t u8_to_u64(uint8_t x)   { return (uint64_t)x; }
uint32_t u16_to_u32(uint16_t x) { return (uint32_t)x; }
uint64_t u16_to_u64(uint16_t x) { return (uint64_t)x; }
uint64_t u32_to_u64(uint32_t x) { return (uint64_t)x; }

uint32_t u64_to_u32(uint64_t x) { return (uint32_t)x; }
uint16_t u64_to_u16(uint64_t x) { return (uint16_t)x; }
uint8_t  u64_to_u8(uint64_t x)  { return (uint8_t)x; }
uint16_t u32_to_u16(uint32_t x) { return (uint16_t)x; }
uint8_t  u32_to_u8(uint32_t x)  { return (uint8_t)x; }
uint8_t  u16_to_u8(uint16_t x)  { return (uint8_t)x; }

// ---------------------------------------------------------------------------
// Sign conversions (same width)
// ---------------------------------------------------------------------------

uint32_t i32_to_u32(int32_t x)  { return (uint32_t)x; }
int32_t  u32_to_i32(uint32_t x) { return (int32_t)x; }
uint64_t i64_to_u64(int64_t x)  { return (uint64_t)x; }
int64_t  u64_to_i64(uint64_t x) { return (int64_t)x; }
int8_t   u8_to_i8(uint8_t x)    { return (int8_t)x; }
uint8_t  i8_to_u8(int8_t x)     { return (uint8_t)x; }

// ---------------------------------------------------------------------------
// Float / int conversions
// ---------------------------------------------------------------------------

double  f32_to_f64(float x)    { return (double)x; }
float   f64_to_f32(double x)   { return (float)x; }
float   i32_to_f32(int32_t x)  { return (float)x; }
double  i32_to_f64(int32_t x)  { return (double)x; }
double  i64_to_f64(int64_t x)  { return (double)x; }
float   u32_to_f32(uint32_t x) { return (float)x; }
double  u32_to_f64(uint32_t x) { return (double)x; }
double  u64_to_f64(uint64_t x) { return (double)x; }
int32_t f32_to_i32(float x)    { return (int32_t)x; }
int32_t f64_to_i32(double x)   { return (int32_t)x; }
int64_t f64_to_i64(double x)   { return (int64_t)x; }
uint32_t f32_to_u32(float x)   { return (uint32_t)x; }
uint32_t f64_to_u32(double x)  { return (uint32_t)x; }
uint64_t f64_to_u64(double x)  { return (uint64_t)x; }

// ---------------------------------------------------------------------------
// Bit manipulation
// ---------------------------------------------------------------------------

uint16_t bswap16(uint16_t x) { return __builtin_bswap16(x); }
uint32_t bswap32(uint32_t x) { return __builtin_bswap32(x); }
uint64_t bswap64(uint64_t x) { return __builtin_bswap64(x); }

uint32_t clz32(uint32_t x) { return x == 0 ? 32u : (uint32_t)__builtin_clz(x); }
uint32_t clz64(uint64_t x) { return x == 0 ? 64u : (uint32_t)__builtin_clzll(x); }
uint32_t ctz32(uint32_t x) { return x == 0 ? 32u : (uint32_t)__builtin_ctz(x); }
uint32_t ctz64(uint64_t x) { return x == 0 ? 64u : (uint32_t)__builtin_ctzll(x); }

uint32_t popcount32(uint32_t x) { return (uint32_t)__builtin_popcount(x); }
uint32_t popcount64(uint64_t x) { return (uint32_t)__builtin_popcountll(x); }

uint32_t rotl32(uint32_t x, uint32_t n) { n &= 31; return (x << n) | (x >> ((32 - n) & 31)); }
uint64_t rotl64(uint64_t x, uint32_t n) { n &= 63; return (x << n) | (x >> ((64 - n) & 63)); }
uint32_t rotr32(uint32_t x, uint32_t n) { n &= 31; return (x >> n) | (x << ((32 - n) & 31)); }
uint64_t rotr64(uint64_t x, uint32_t n) { n &= 63; return (x >> n) | (x << ((64 - n) & 63)); }

// ---------------------------------------------------------------------------
// Atomic fence (sequentially consistent — strongest order)
// ---------------------------------------------------------------------------

void atomic_fence(int32_t order) {
    (void)order;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

// ---------------------------------------------------------------------------
// Math predicate.  `isfinite` is a macro in <math.h>, so we undef it and
// define a real function with C linkage that callers reference by name.
// ---------------------------------------------------------------------------

#undef isfinite
int32_t isfinite(double x) { return __builtin_isfinite(x) ? 1 : 0; }

// ---------------------------------------------------------------------------
// dirent field extractors
// ---------------------------------------------------------------------------

const char *dirent_name(void *entry) {
    if (entry == NULL) { return ""; }
    return ((struct dirent *)entry)->d_name;
}

int32_t dirent_type(void *entry) {
    if (entry == NULL) { return 0; }
    return (int32_t)((struct dirent *)entry)->d_type;
}

// ---------------------------------------------------------------------------
// Note on stdlib symbols.  cryoc emits v0.2-mangled calls into stdlib
// (e.g., `C$3std.4core.10primitives.6string-6append$F$s_S$RS`), but
// `libcryo.a` was compiled by the C++ bootstrap with old-style mangling.
// Bridging here via `__asm__("...")` aliasing fails because GAS rejects
// the `-` member separator in symbol names.  Closing this gap requires
// rebuilding stdlib with cryoc.
