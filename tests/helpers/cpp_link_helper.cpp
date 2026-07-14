// cpp_link_helper.cpp - out-of-line definitions so each entity emits a symbol.
#include "cpp_link_helper.hpp"

namespace cpplink {
    int add(int a, int b) { return a + b; }
}

int Counter::get() const { return v; }
int Counter::origin() { return 100; }
