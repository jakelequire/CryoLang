#include "funcs.h"
#include <string.h>

int count_strings(const char **strings, int count) {
    int total_len = 0;
    for (int i = 0; i < count; i++) {
        total_len += strlen(strings[i]);
    }
    return total_len;
}
