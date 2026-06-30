// cpp_link_helper.hpp — a tiny C++ library for the ffi_cpp_link regression test.
//
// Provides a real C++ namespace function and a non-virtual out-of-line method so
// the test can bind them by their Itanium-mangled symbols via `![link("...")]`
// and call them ACROSS a module boundary — exercising the compiler's
// cross-module link-name propagation (the core of C++ Strategy-A binding).
#pragma once

namespace cpplink {
    int add(int a, int b);
}

struct Counter {
    int v;
    int get() const;              // non-virtual, out-of-line -> _ZNK7Counter3getEv
    static int origin();          // static                   -> _ZN7Counter6originEv
};
