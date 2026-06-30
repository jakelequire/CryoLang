#include "probe.hpp"
namespace demo {
    int add(int a, int b) { return a + b; }
    double add(double a, double b) { return a + b; }
    namespace math { int twice(int a) { return a * 2; } }
    int twice(int a) { return a + a; }
}
