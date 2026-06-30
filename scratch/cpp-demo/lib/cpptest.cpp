// cpptest.cpp — out-of-line definitions so every non-inline entity emits a symbol.
#include "cpptest.hpp"

namespace demo {
    int add(int a, int b) { return a + b; }
}

int Point::sum() const { return x + y; }
int Point::origin_x() { return 0; }

int& Box::get() { return v; }
void Box::set(const int& nv) { v = nv; }

int Shape::area() const { return 0; }
