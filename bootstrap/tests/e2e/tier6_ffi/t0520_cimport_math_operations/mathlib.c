#include "mathlib.h"

int abs_val(int x) { return x < 0 ? -x : x; }
int max_val(int a, int b) { return a > b ? a : b; }
int min_val(int a, int b) { return a < b ? a : b; }
int clamp(int x, int lo, int hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}
int factorial(int n) {
    int result = 1;
    for (int i = 2; i <= n; i++) result *= i;
    return result;
}
