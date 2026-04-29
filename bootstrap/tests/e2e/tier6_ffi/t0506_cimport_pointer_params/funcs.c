#include "funcs.h"

void write_to_ptr(int *out, int value) {
    *out = value;
}

int read_from_ptr(const int *in) {
    return *in;
}
