#ifndef FUNCS_H
#define FUNCS_H

struct Point {
    int x;
    int y;
};

struct Point *create_point(int x, int y);
int point_x(const struct Point *p);
int point_y(const struct Point *p);
void free_point(struct Point *p);

#endif
