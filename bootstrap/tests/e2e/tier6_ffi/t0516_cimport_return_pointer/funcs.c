#include "funcs.h"
#include <stdlib.h>

const char *get_name(void) { return "CryoFFI"; }

int *create_int(int value) {
    int *p = (int *)malloc(sizeof(int));
    *p = value;
    return p;
}

void destroy_int(int *ptr) { free(ptr); }
