#pragma once
namespace demo {
    int add(int a, int b);
    double add(double a, double b);   // OVERLOAD — same leaf name as add(int,int)
    namespace math { int twice(int a); }
    int twice(int a);                 // leaf COLLISION with demo::math::twice
}
