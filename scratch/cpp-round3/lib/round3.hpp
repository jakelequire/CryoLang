// round3.hpp — round-3 C++ bindgen stress: feature shapes not yet exercised.
//
// Probes (each either binds, or must be cleanly skip-reported — never a silent
// miscompile or a generator crash):
//   * operator overloads (operator+, operator==, operator[]) — odd method names
//   * a typedef / `using` alias of a bound struct
//   * an opaque, forward-declared type used only behind a pointer (handle API)
//   * a void-returning free function with a pointer out-param
//   * a small but realistic value type (Vec3) with by-value-returning methods
#pragma once

// ---- realistic value type with by-value-returning methods + operators ------
struct Vec3 {
    double x;
    double y;
    double z;
    double length_sq() const;             // bindable
    Vec3   add(const Vec3& o) const;       // bindable: by-value return
    Vec3   cross(const Vec3& o) const;     // bindable: by-value return
    Vec3   operator+(const Vec3& o) const; // PROBE: operator overload
    bool   operator==(const Vec3& o) const;// PROBE: operator overload
    double operator[](int i) const;        // PROBE: subscript operator
};

// ---- typedef / using alias of a bound type ---------------------------------
typedef Vec3 Point3;
using  Vec3Alias = Vec3;
double point3_x(const Point3& p);          // takes the typedef'd name

// ---- opaque handle: forward-declared, used only by pointer -----------------
struct Engine;                             // never defined in this header
Engine* engine_create(int seed);
int     engine_tick(Engine* e);
void    engine_destroy(Engine* e);

// ---- void return + pointer out-param ---------------------------------------
void split_halves(int n, int* lo, int* hi);
