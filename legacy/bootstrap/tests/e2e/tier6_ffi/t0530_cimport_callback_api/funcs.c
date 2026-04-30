#include "funcs.h"
#include <stdio.h>

void run_with_value(int value) {
    printf("value: %d\n", value);
}

int compute_pair(int a, int b, int op) {
    switch (op) {
        case 0: return a + b;
        case 1: return a - b;
        case 2: return a * b;
        default: return 0;
    }
}
