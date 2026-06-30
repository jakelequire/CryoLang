// round3.cpp — out-of-line definitions for the round-3 probe.
#include "round3.hpp"

double Vec3::length_sq() const          { return x*x + y*y + z*z; }
Vec3   Vec3::add(const Vec3& o) const   { return Vec3{ x+o.x, y+o.y, z+o.z }; }
Vec3   Vec3::cross(const Vec3& o) const {
    return Vec3{ y*o.z - z*o.y, z*o.x - x*o.z, x*o.y - y*o.x };
}
Vec3   Vec3::operator+(const Vec3& o) const { return add(o); }
bool   Vec3::operator==(const Vec3& o) const { return x==o.x && y==o.y && z==o.z; }
double Vec3::operator[](int i) const    { return i==0 ? x : (i==1 ? y : z); }

double point3_x(const Point3& p) { return p.x; }

// Opaque Engine: a tiny counter behind a heap pointer.
struct Engine { int v; };
Engine* engine_create(int seed) { Engine* e = new Engine(); e->v = seed; return e; }
int     engine_tick(Engine* e)  { e->v += 1; return e->v; }
void    engine_destroy(Engine* e) { delete e; }

void split_halves(int n, int* lo, int* hi) { *lo = n / 2; *hi = n - n / 2; }
