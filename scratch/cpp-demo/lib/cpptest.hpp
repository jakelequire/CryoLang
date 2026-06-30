// cpptest.hpp — a tiny C++ library for the Cryo C++-bindgen probe + report test.
//
// Bindable subset (direct mangled-symbol binding, Strategy A):
//   - demo::add        namespace free function
//   - Point::sum       non-virtual out-of-line const method
//   - Point::origin_x  static member function
//   - Box::get/set     methods taking/returning by reference (T& -> pointer)
// Reported-as-skipped (no directly-callable symbol):
//   - Point::doubled   inline method
//   - Shape::area      virtual method
//   - Point (ctor)     constructor (Phase 2)
#pragma once

namespace demo {
    int add(int a, int b);
}

struct Point {
    int x;
    int y;
    int sum() const;              // bindable: non-virtual, out-of-line
    static int origin_x();        // bindable: static
    int doubled() const { return (x + y) * 2; }   // SKIP: inline
};

struct Box {
    int v;
    int& get();                   // bindable: returns int& (-> int*)
    void set(const int& nv);      // bindable: takes const int& (-> int*)
};

struct Shape {
    virtual int area() const;     // SKIP: virtual
};
