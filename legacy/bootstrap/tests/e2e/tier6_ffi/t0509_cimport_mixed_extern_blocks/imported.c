#include "imported.h"
#include <stdio.h>

void imported_func(void) { printf("from CImport\n"); }

void manual_func(void) { printf("from extern C\n"); }
