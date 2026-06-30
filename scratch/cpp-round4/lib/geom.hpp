#pragma once
// enum WITHOUT explicit values + a gap + an implicitly-typed plain enum.
enum Quadrant { Q1, Q2, Q3 = 10, Q4 };   // Q1=0,Q2=1,Q3=10,Q4=11
struct Rect { int w; int h; int area() const; };
int quad_code(Quadrant q);
