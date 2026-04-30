#ifndef FUNCS_H
#define FUNCS_H

typedef enum { COLOR_RED = 0, COLOR_GREEN = 1, COLOR_BLUE = 2 } Color;

const char *color_name(Color c);
int color_value(Color c);

#endif
