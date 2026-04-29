#include "funcs.h"

const char *color_name(Color c) {
    switch (c) {
        case COLOR_RED:   return "red";
        case COLOR_GREEN: return "green";
        case COLOR_BLUE:  return "blue";
        default:          return "unknown";
    }
}

int color_value(Color c) {
    return (int)c;
}
