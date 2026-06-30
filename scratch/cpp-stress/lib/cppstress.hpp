// cppstress.hpp — heavy stress surface for the Cryo C++ bindgen / vendor path.
//
// Focus areas (the parts most likely to expose ABI / layout bugs):
//   * full scalar coverage (signed/unsigned 8..64, float, double, char, bool)
//   * struct-by-value params AND returns across every Itanium size class:
//       8 bytes  -> single register
//       16 bytes -> two eightbytes (INTEGER / SSE)
//       24/32 b  -> returned via hidden sret pointer, passed via memory
//   * struct layout fidelity (padding/alignment of a mixed-field record)
//   * fixed-array and nested-struct fields
//   * enum class with a 64-bit underlying type and a negative enumerator
//   * honesty at scale: templates / virtual / ctor+dtor / private members
//     must be skip-reported while the bindable members of the same class bind.
//
// Self-contained: no <cstdint>/<string> etc., so libclang parses it without a
// C++ stdlib include path.  Sized integers use the plain spellings.
#pragma once

// ---- scalar coverage ------------------------------------------------------
namespace sc {
    signed char    s8(signed char x);
    short          s16(short x);
    int            s32(int x);
    long long      s64(long long x);
    unsigned char  u8(unsigned char x);
    unsigned short u16(unsigned short x);
    unsigned int   u32(unsigned int x);
    unsigned long long u64(unsigned long long x);
    float          f32(float x);
    double         f64(double x);
    char           ch(char x);
    bool           flip(bool x);
}

// ---- layout fidelity ------------------------------------------------------
// char@0, pad, int@4, char@8, pad, double@16  => sizeof 24, align 8.
struct Mixed { char a; int b; char c; double d; };
int    mixed_b(const Mixed& m);
double mixed_d(const Mixed& m);
long   mixed_size();                 // sizeof(Mixed) from the C++ side

struct WithArr { int xs[4]; int n; };
int witharr_sum(const WithArr& w);   // sum xs[0..n)

struct Inner { int u; int v; };
struct Outer { Inner in; int tag; }; // nested struct field
int outer_mix(const Outer& o);       // in.u*100 + in.v*10 + tag

// ---- struct-by-value ABI at each size class -------------------------------
struct S8  { int a; int b; };                  // 8 bytes  -> one register
struct S16 { double x; double y; };            // 16 bytes -> two SSE eightbytes
struct S24 { double x; double y; double z; };  // 24 bytes -> sret
struct S32 { long long a, b, c, d; };          // 32 bytes -> sret

int  s8_sum(S8 v);                   // struct BY VALUE param
S8   s8_make(int a, int b);          // struct BY VALUE return (register)
double s16_sum(S16 v);               // 16-byte SSE param
S16  s16_scale(S16 v, double f);     // 16-byte SSE return
S24  s24_make(double x, double y, double z);   // sret return
S24  s24_add(S24 a, S24 b);          // memory params + sret return
S32  s32_make(long long a, long long b, long long c, long long d); // sret
long long s32_sum(S32 v);            // memory param

// ---- enum class, 64-bit underlying, negative enumerator -------------------
enum class Mode : long long { Lo = -1000, Mid = 7, Hi = 5000000000LL };
long long mode_val(Mode m);          // enum-class by value
Mode      mode_pick(int which);      // returns an enum class by value

// ---- round 2: mixed-class eightbytes, odd sizes, deep namespaces ----------
struct IF  { int a; float b; };                // 8B, one mixed eightbyte
struct DI  { double x; int n; };               // 16B: SSE eightbyte + INTEGER
struct IID { int a; int b; double c; };        // 16B: INTEGER eightbyte + SSE
struct I3  { int a; int b; int c; };           // 12B odd size

double if_combine(IF v);             // mixed by-value param
IF     if_make(int a, float b);      // mixed by-value return
double di_combine(DI v);             // SSE+INTEGER param
DI     di_make(double x, int n);     // SSE+INTEGER return
double iid_combine(IID v);           // INTEGER+SSE param
I3     i3_make(int a, int b, int c); // 12-byte return
int    i3_sum(I3 v);                 // 12-byte param

namespace a { namespace b { namespace c {
    int deep(int x);                 // 3-level nested namespace
} } }

// LLP64 hazard: C++ `long` is 32-bit on Win64 (mingw) but 64-bit on Linux.
// `echo_long` round-trips a value that does NOT fit in 32 bits, so a wrong
// width mapping truncates it and the check fails.
long echo_long(long x);

// ---- honesty at scale -----------------------------------------------------
template <class T> T tpl_identity(T x);          // SKIP: function template

struct Poly {
    int data;
    virtual int area() const;                    // SKIP: virtual
    int plain() const;                           // bindable: non-virtual
};

struct Life {
    int v;
    Life();                                      // SKIP: constructor (Phase 2)
    ~Life();                                     // SKIP: destructor (Phase 2)
    int get() const;                             // bindable
};

struct Priv {
    int n;
private:
    int secret() const;                          // SKIP: non-public
public:
    int ok() const;                              // bindable
};
