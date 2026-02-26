#include "funcs.h"
#include <stdio.h>

int compute(int a, int b, int c, int d, int e) {
    return a + b + c + d + e;
}

void print_five(const char *a, const char *b, const char *c, const char *d, const char *e) {
    printf("%s %s %s %s %s\n", a, b, c, d, e);
}
