// cpptest.hpp — C++ surface for the Cryo C++-bindgen / vendor interop test.
//
// Goal: exercise as much of the Phase-1 "direct mangled-symbol binding"
// (Strategy A) surface as possible, plus a few entities that MUST be
// skip-reported, so `cryo vendor` coverage is meaningfully tested end-to-end.
//
// Bindable (out-of-line, non-virtual, public):
//   demo::add / demo::sub_long / demo::scale / demo::is_even / demo::clamp_ptr
//   demo::math::mul                      (nested namespace -> flattened)
//   Point::sum (const) / Point::origin_x (static)
//   Box::get (int&) / Box::set (const int&)
//   Counter::inc (mut) / Counter::add_n (mut, param) / Counter::value (const)
//            Counter::make (static factory, returns Counter BY VALUE)
//   Vec2::dot (const, const-ref param) / Vec2::scaled (const, returns BY VALUE)
//   demo::color_code(Color)              (C-style enum param)
// Skip-reported (no directly-callable out-of-line symbol):
//   Point::doubled (inline) / Shape::area (virtual) / Counter ctor (Phase 2)
#pragma once

enum Color { ColRed = 0, ColGreen = 1, ColBlue = 2 };

namespace demo {
    int           add(int a, int b);
    long          sub_long(long a, long b);
    double        scale(double v, double by);
    bool          is_even(int n);
    int*          clamp_ptr(int* p, int lo);   // pointer param + return
    unsigned      color_code(Color c);          // C enum by value

    namespace math {
        int mul(int a, int b);                  // nested namespace
    }
}

struct Point {
    int x;
    int y;
    int sum() const;                            // bindable: non-virtual, out-of-line
    static int origin_x();                      // bindable: static
    int doubled() const { return (x + y) * 2; } // SKIP: inline
};

struct Box {
    int v;
    int& get();                                 // bindable: returns int& (-> int*)
    void set(const int& nv);                    // bindable: takes const int& (-> int*)
};

// Mutating methods (mut &this), a parameterised method, and a static factory
// that returns the struct BY VALUE (Itanium small-aggregate return ABI).
struct Counter {
    int n;
    void inc();                                 // bindable: mutating, no params
    void add_n(int k);                          // bindable: mutating, one param
    int  value() const;                         // bindable: const accessor
    static Counter make(int start);             // bindable: static, returns by value
};

// Two-double aggregate: by-value return (scaled) and const-ref struct param (dot).
struct Vec2 {
    double x;
    double y;
    double dot(const Vec2& o) const;            // bindable: const, const-ref param
    Vec2   scaled(double f) const;              // bindable: returns Vec2 by value
};

struct Shape {
    virtual int area() const;                   // SKIP: virtual
};
