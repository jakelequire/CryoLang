// cppstress.cpp — out-of-line definitions (every non-inline entity emits a symbol).
#include "cppstress.hpp"

namespace sc {
    signed char    s8(signed char x)            { return (signed char)(x + 1); }
    short          s16(short x)                  { return (short)(x + 1); }
    int            s32(int x)                    { return x + 1; }
    long long      s64(long long x)              { return x + 1; }
    unsigned char  u8(unsigned char x)           { return (unsigned char)(x + 1); }
    unsigned short u16(unsigned short x)         { return (unsigned short)(x + 1); }
    unsigned int   u32(unsigned int x)           { return x + 1u; }
    unsigned long long u64(unsigned long long x) { return x + 1ull; }
    float          f32(float x)                  { return x * 2.0f; }
    double         f64(double x)                 { return x * 2.0; }
    char           ch(char x)                    { return (char)(x + 1); }
    bool           flip(bool x)                  { return !x; }
}

int    mixed_b(const Mixed& m) { return m.b; }
double mixed_d(const Mixed& m) { return m.d; }
long   mixed_size()            { return (long)sizeof(Mixed); }

int witharr_sum(const WithArr& w) {
    int s = 0;
    for (int i = 0; i < w.n; i++) s += w.xs[i];
    return s;
}

int outer_mix(const Outer& o) { return o.in.u * 100 + o.in.v * 10 + o.tag; }

int  s8_sum(S8 v)            { return v.a + v.b; }
S8   s8_make(int a, int b)   { return S8{ a, b }; }
double s16_sum(S16 v)        { return v.x + v.y; }
S16  s16_scale(S16 v, double f) { return S16{ v.x * f, v.y * f }; }
S24  s24_make(double x, double y, double z) { return S24{ x, y, z }; }
S24  s24_add(S24 a, S24 b)   { return S24{ a.x + b.x, a.y + b.y, a.z + b.z }; }
S32  s32_make(long long a, long long b, long long c, long long d) { return S32{ a, b, c, d }; }
long long s32_sum(S32 v)     { return v.a + v.b + v.c + v.d; }

long long mode_val(Mode m)   { return (long long)m; }
Mode      mode_pick(int which) {
    if (which < 0) return Mode::Lo;
    if (which == 0) return Mode::Mid;
    return Mode::Hi;
}

double if_combine(IF v)        { return (double)v.a + (double)v.b; }
IF     if_make(int a, float b) { return IF{ a, b }; }
double di_combine(DI v)        { return v.x + (double)v.n; }
DI     di_make(double x, int n){ return DI{ x, n }; }
double iid_combine(IID v)      { return (double)v.a + (double)v.b + v.c; }
I3     i3_make(int a, int b, int c) { return I3{ a, b, c }; }
int    i3_sum(I3 v)            { return v.a + v.b + v.c; }

namespace a { namespace b { namespace c {
    int deep(int x) { return x + 333; }
} } }

long echo_long(long x) { return x; }

template <class T> T tpl_identity(T x) { return x; }
template int tpl_identity<int>(int);   // explicit instantiation (still no stable bind name)

int Poly::area() const  { return data * data; }
int Poly::plain() const { return data + 1; }

Life::Life()  { v = 42; }
Life::~Life() { }
int Life::get() const { return v; }

int Priv::secret() const { return n * 2; }
int Priv::ok() const     { return n + 7; }
