#include "funcs.h"
#include <stdlib.h>

void *my_alloc(int size) { return malloc(size); }
void my_free(void *ptr) { free(ptr); }
void store_int(void *ptr, int value) { *(int *)ptr = value; }
int load_int(const void *ptr) { return *(const int *)ptr; }
