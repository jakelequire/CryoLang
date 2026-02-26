#include "funcs.h"
#include <stdio.h>
#include <stdarg.h>

int c_sprintf(char *buf, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int n = vsprintf(buf, fmt, args);
    va_end(args);
    return n;
}
