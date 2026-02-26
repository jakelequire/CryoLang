#include "funcs.h"
#include <string.h>

size_t my_strlen(const char *s) { return strlen(s); }

void *my_memset(void *s, int c, size_t n) { return memset(s, c, n); }
