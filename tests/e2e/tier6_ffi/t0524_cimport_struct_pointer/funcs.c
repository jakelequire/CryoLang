#include "funcs.h"
#include <stdlib.h>

struct Point *create_point(int x, int y) {
    struct Point *p = (struct Point *)malloc(sizeof(struct Point));
    p->x = x;
    p->y = y;
    return p;
}

int point_x(const struct Point *p) {
    return p->x;
}

int point_y(const struct Point *p) {
    return p->y;
}

void free_point(struct Point *p) {
    free(p);
}
