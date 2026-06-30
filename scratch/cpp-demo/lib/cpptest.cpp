// cpptest.cpp — out-of-line definitions so every non-inline entity emits a symbol.
#include "cpptest.hpp"

namespace demo {
    int  add(int a, int b)            { return a + b; }
    long sub_long(long a, long b)     { return a - b; }
    double scale(double v, double by) { return v * by; }
    bool is_even(int n)               { return (n % 2) == 0; }
    int* clamp_ptr(int* p, int lo)    { if (*p < lo) *p = lo; return p; }
    unsigned color_code(Color c)      { return (unsigned)c * 10u; }

    namespace math {
        int mul(int a, int b) { return a * b; }
    }
}

int Point::sum() const  { return x + y; }
int Point::origin_x()   { return 0; }

int& Box::get()                 { return v; }
void Box::set(const int& nv)    { v = nv; }

void Counter::inc()             { n += 1; }
void Counter::add_n(int k)      { n += k; }
int  Counter::value() const     { return n; }
Counter Counter::make(int start){ return Counter{ start }; }

double Vec2::dot(const Vec2& o) const { return x * o.x + y * o.y; }
Vec2   Vec2::scaled(double f) const   { return Vec2{ x * f, y * f }; }

int Shape::area() const { return 0; }
